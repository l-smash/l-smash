/*****************************************************************************
 * mp4sys.c:
 *****************************************************************************
 * Copyright (C) 2010 L-SMASH project
 *
 * Authors: Takashi Hirata <felidlabo AT gmail DOT com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *****************************************************************************/

/* This file is available under an ISC license. */

#ifndef __MINGW32__
#define _FILE_OFFSET_BITS 64 /* FIXME: This is redundant. Should be concentrated in isom_util.h */
#endif

#include "isom_util.h"

#include <stdlib.h>
#include <string.h>

#define MP4SYS_INTERNAL
#define MP4A_INTERNAL
#include "mp4sys.h"

#define debug_if(x) if(x)

#ifdef __MINGW32__ /* FIXME: This is redundant. Should be concentrated in isom_util.h */
#define mp4sys_fseek fseeko64
#define mp4sys_ftell ftello64
#else
#define mp4sys_fseek fseek
#define mp4sys_ftell ftell
#endif

/***************************************************************************
    bitstream writer
****************************************************************************/

typedef struct {
    isom_bs_t* bs;
    uint8_t store;
    uint8_t cache;
} mp4sys_bits_t;

void mp4sys_bits_init( mp4sys_bits_t* bits, isom_bs_t *bs )
{
    debug_if( !bits || !bs )
        return;
    bits->bs = bs;
    bits->store = 0;
    bits->cache = 0;
}

mp4sys_bits_t* mp4sys_bits_create( isom_bs_t *bs )
{
    debug_if( !bs )
        return NULL;
    mp4sys_bits_t* bits = (mp4sys_bits_t*)malloc( sizeof(mp4sys_bits_t) );
    if( !bits )
        return NULL;
    mp4sys_bits_init( bits, bs );
    return bits;
}

#define BITS_IN_BYTE 8
void mp4sys_bits_align( mp4sys_bits_t *bits )
{
    debug_if( !bits )
        return;
    if( !bits->store )
        return;
    isom_bs_put_byte( bits->bs, bits->cache << ( BITS_IN_BYTE - bits->store ) );
}

/* Must be used ONLY for bits struct created with isom_create_bits.
   Otherwise, just free() the bits struct. */
void mp4sys_bits_cleanup( mp4sys_bits_t *bits )
{
    debug_if( !bits )
        return;
    mp4sys_bits_align( bits );
    free( bits );
}

/* we can change value's type to unsigned int for 64-bit operation if needed. */
static inline uint8_t mp4sys_bits_mask_lsb8( uint32_t value, uint32_t width )
{
    return (uint8_t)( value & ~( ~0U << width ) );
}

/* We can change value's type to unsigned int for 64-bit operation if needed. */
void mp4sys_bits_put( mp4sys_bits_t *bits, uint32_t value, uint32_t width )
{
    debug_if( !bits || !width )
        return;
    if( bits->store )
    {
        if( bits->store + width < BITS_IN_BYTE )
        {
            /* cache can contain all of value's bits. */
            bits->cache <<= width;
            bits->cache |= mp4sys_bits_mask_lsb8( value, width );
            bits->store += width;
            return;
        }
        /* flush cache with value's some leading bits. */
        uint32_t free_bits = BITS_IN_BYTE - bits->store;
        bits->cache <<= free_bits;
        bits->cache |= mp4sys_bits_mask_lsb8( value >> (width -= free_bits), free_bits );
        isom_bs_put_byte( bits->bs, bits->cache );
        bits->store = 0;
        bits->cache = 0;
    }
    /* cache is empty here. */
    /* byte unit operation. */
    while( width > BITS_IN_BYTE )
        isom_bs_put_byte( bits->bs, (uint8_t)(value >> (width -= BITS_IN_BYTE)) );
    /* bit unit operation for residual. */
    if( width )
    {
        bits->cache = mp4sys_bits_mask_lsb8( value, width );
        bits->store = width;
    }
}

/****
 bitstream with bytestream for adhoc operation
****/

mp4sys_bits_t* mp4sys_adhoc_bits_create()
{
    isom_bs_t* bs = isom_bs_create( NULL ); /* no file writing */
    if( !bs )
        return NULL;
    mp4sys_bits_t* bits = mp4sys_bits_create( bs );
    if( !bits )
    {
        isom_bs_cleanup( bs );
        return NULL;
    }
    return bits;
}

void mp4sys_adhoc_bits_cleanup( mp4sys_bits_t* bits )
{
    if( !bits )
        return;
    isom_bs_cleanup( bits->bs );
    mp4sys_bits_cleanup( bits );
}

void* mp4sys_bs_export_data( mp4sys_bits_t* bits, uint32_t* length )
{
    return isom_bs_export_data( bits->bs, length );
}

/***************************************************************************
    implementation of part of ISO/IEC 14496-3 (ISO/IEC 14496-1 relevant)
***************************************************************************/

/* ISO/IEC 14496-3 1.6.3.4 samplingFrequencyIndex, Table 1.16 Sampling Frequency Index */
/* ISO/IEC 14496-3 4.5 Overall data structure, Table 4.68 Sampling frequency mapping */
const uint32_t mp4a_AAC_frequency_table[13][4] = {
    /* threshold, exact, idx, idx_for_sbr */
    {      92017, 96000, 0x0,         0xF }, /* SBR is not allowed */
    {      75132, 88200, 0x1,         0xF }, /* SBR is not allowed */
    {      55426, 64000, 0x2,         0xF }, /* SBR is not allowed */
    {      46009, 48000, 0x3,         0x0 },
    {      37566, 44100, 0x4,         0x1 },
    {      27713, 32000, 0x5,         0x2 },
    {      23004, 24000, 0x6,         0x3 },
    {      18783, 22050, 0x7,         0x4 },
    {      13856, 16000, 0x8,         0x5 },
    {      11502, 12000, 0x9,         0x6 },
    {       9391, 11025, 0xA,         0x7 },
    {       8000,  8000, 0xB,         0x8 },
    {          0,  7350, 0xB,         0xF } /* samplingFrequencyIndex for GASpecificConfig is 0xB (same as 8000Hz). */
};

/* ISO/IEC 14496-3 1.6 Interface to ISO/IEC 14496-1 (MPEG-4 Systems), Table 1.13 Syntax of AudioSpecificConfig(). */
/* This structure is represent of regularized AudioSpecificConfig. */
/* for actual definition, see Table 1.14 Syntax of GetAudioObjectType() for audioObjectType and extensionAudioObjectType. */
typedef struct {
    mp4a_aac_sbr_mode sbr_mode; /* L-SMASH's original, including sbrPresent flag. */
    mp4a_AudioObjectType audioObjectType;
    uint8_t samplingFrequencyIndex;
    uint32_t samplingFrequency;
    uint8_t channelConfiguration;
    mp4a_AudioObjectType extensionAudioObjectType;
    uint8_t extensionSamplingFrequencyIndex;
    uint8_t extensionSamplingFrequency;
    /* if( audioObjectType in
        #[ 1, 2, 3, 4, 6, 7, *17, *19, *20, *21, *22, *23 ] // GASpecificConfig, AAC relatives and TwinVQ, BSAC
        [ 8 ]              // CelpSpecificConfig, not supported
        [ 9 ]              // HvxcSpecificConfig, not supported
        [ 12 ]             // TTSSpecificConfig, not supported
        [ 13, 14, 15, 16 ] // StructuredAudioSpecificConfig, notsupported
        [ 24 ]             // ErrorResilientCelpSpecificConfig, notsupported
        [ 25 ]             // ErrorResilientHvxcSpecificConfig, notsupported
        [ 26, 27 ]         // ParametricSpecificConfig, notsupported
        [ 28 ]             // SSCSpecificConfig, notsupported
        #[ 32, 33, 34 ]     // MPEG_1_2_SpecificConfig
        [ 35 ]             // DSTSpecificConfig, notsupported
    ){ */
        void* deepAudioSpecificConfig; // L-SMASH's original name, reperesents such as GASpecificConfig. */
    /* } */
    /*
    // error resilient stuff, not supported
    if( audioObjectType in [17, 19, 20, 21, 22, 23, 24, 25, 26, 27] ){
        uint8_t epConfig // 2bit
        if( epConfig == 2 || epConfig == 3 ){
            ErrorProtectionSpecificConfig();
        }
        if( epConfig == 3 ){
            uint8_t directMapping;  // 1bit, currently always 1.
            if( !directMapping ){
                // tbd
            }
        }
    }
    */
} mp4a_AudioSpecificConfig_t;

/* ISO/IEC 14496-3 4.4.1 Decoder configuration (GASpecificConfig), Table 4.1 Syntax of GASpecificConfig() */
/* ISO/IEC 14496-3 4.5.1.1 GASpecificConfig(), Table 4.68 Sampling frequency mapping */
typedef struct {
    uint8_t frameLengthFlag; /* FIXME: AAC_SSR: shall be 0, Others: depends, but noramally 0. */
    uint8_t dependsOnCoreCoder; /* FIXME: used if scalable AAC. */
    /*
    if( dependsOnCoreCoder ){
        uint16_t coreCoderDelay; // 14bits
    }
    */
    uint8_t extensionFlag; /* 1bit, 1 if ErrorResilience */
    /* if( !channelConfiguration ){ */
        void* program_config_element;  /* currently not supported. */
    /* } */
    /*
    // we do not support AAC_scalable
    if( (audioObjectType == MP4A_AUDIO_OBJECT_TYPE_AAC_scalable) || (audioObjectType == MP4A_AUDIO_OBJECT_TYPE_ER_AAC_scalable) ){
        uint8_t layerNr; // 3bits
    }
    */
    /*
    // we do not support special AACs
    if( extensionFlag ){
        if( audioObjectType == MP4A_AUDIO_OBJECT_TYPE_ER_BSAC ){
            uint8_t numOfSubFrame; // 5bits
            uint8_t layer_length;  // 11bits
        }
        if( audioObjectType == MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LC || audioObjectType == MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LTP
            || audioObjectType == MP4A_AUDIO_OBJECT_TYPE_ER_AAC_scalable || audioObjectType == MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LD
        ){
            uint8_t aacSectionDataResilienceFlag; // 1bit
            uint8_t aacScalefactorDataResilienceFlag; // 1bit
            uint8_t aacSpectralDataResilienceFlag; // 1bit
        }
        uint8_t extensionFlag3; // 1bit
        if( extensionFlag3 ){
            // tbd in version 3
        }
    }
    */
} mp4a_GASpecificConfig_t;

/* ISO/IEC 14496-3 9.2 MPEG_1_2_SpecificConfig, Table 9.1 MPEG_1_2_SpecificConfig() */
typedef struct {
    uint8_t extension; /* shall be 0. */
} mp4a_MPEG_1_2_SpecificConfig_t;

static inline void mp4a_remove_GASpecificConfig( mp4a_GASpecificConfig_t* gasc )
{
    debug_if( !gasc )
        return;
    if( gasc->program_config_element )
        free( gasc->program_config_element );
    free( gasc );
}

static inline void mp4a_remove_MPEG_1_2_SpecificConfig( mp4a_MPEG_1_2_SpecificConfig_t* mpeg_1_2_sc )
{
    debug_if( mpeg_1_2_sc )
        free( mpeg_1_2_sc );
}

void mp4a_remove_AudioSpecificConfig( mp4a_AudioSpecificConfig_t* asc )
{
    if( !asc )
        return;
    switch( asc->audioObjectType ){
    case MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN:
    case MP4A_AUDIO_OBJECT_TYPE_AAC_LC:
    case MP4A_AUDIO_OBJECT_TYPE_AAC_SSR:
    case MP4A_AUDIO_OBJECT_TYPE_AAC_LTP:
    case MP4A_AUDIO_OBJECT_TYPE_SBR:
    case MP4A_AUDIO_OBJECT_TYPE_AAC_scalable:
    case MP4A_AUDIO_OBJECT_TYPE_TwinVQ:
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LC:
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LTP:
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_scalable:
    case MP4A_AUDIO_OBJECT_TYPE_ER_Twin_VQ:
    case MP4A_AUDIO_OBJECT_TYPE_ER_BSAC:
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LD:
        mp4a_remove_GASpecificConfig( (mp4a_GASpecificConfig_t*)asc->deepAudioSpecificConfig );
        break;
    case MP4A_AUDIO_OBJECT_TYPE_Layer_1:
    case MP4A_AUDIO_OBJECT_TYPE_Layer_2:
    case MP4A_AUDIO_OBJECT_TYPE_Layer_3:
        mp4a_remove_MPEG_1_2_SpecificConfig( (mp4a_MPEG_1_2_SpecificConfig_t*)asc->deepAudioSpecificConfig );
        break;
    default:
        if( asc->deepAudioSpecificConfig )
            free( asc->deepAudioSpecificConfig );
        break;
    }
    free( asc );
}

/* ADIF/PCE(program config element) style GASpecificConfig is not not supported. */
/* channelConfig/samplingFrequencyIndex will be used when we support ADIF/PCE style GASpecificConfig. */
static mp4a_GASpecificConfig_t* mp4a_create_GASpecificConfig( uint8_t samplingFrequencyIndex, uint8_t channelConfig, mp4a_AudioObjectType aot )
{
    debug_if( aot != MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN && aot != MP4A_AUDIO_OBJECT_TYPE_AAC_LC
        && aot != MP4A_AUDIO_OBJECT_TYPE_AAC_SSR && aot != MP4A_AUDIO_OBJECT_TYPE_AAC_LTP
        && aot != MP4A_AUDIO_OBJECT_TYPE_TwinVQ )
        return NULL;
    if( samplingFrequencyIndex > 0xB || channelConfig == 0 || channelConfig == 7 )
        return NULL;
    mp4a_GASpecificConfig_t* gasc = (mp4a_GASpecificConfig_t*)malloc( sizeof(mp4a_GASpecificConfig_t) );
    if( !gasc )
        return NULL;
    memset( gasc, 0, sizeof(mp4a_GASpecificConfig_t) );
    gasc->frameLengthFlag = 0; /* FIXME: AAC_SSR: shall be 0, Others: depends, but noramally 0. */
    gasc->dependsOnCoreCoder = 0; /* FIXME: used if scalable AAC. */
    switch( aot ){
    case MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN:
    case MP4A_AUDIO_OBJECT_TYPE_AAC_LC:
    case MP4A_AUDIO_OBJECT_TYPE_AAC_SSR:
    case MP4A_AUDIO_OBJECT_TYPE_AAC_LTP:
    case MP4A_AUDIO_OBJECT_TYPE_AAC_scalable:
    case MP4A_AUDIO_OBJECT_TYPE_TwinVQ:
        gasc->extensionFlag = 0;
        break;
    /* currently never occures. */
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LC:
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LTP:
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_scalable:
    case MP4A_AUDIO_OBJECT_TYPE_ER_Twin_VQ:
    case MP4A_AUDIO_OBJECT_TYPE_ER_BSAC:
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LD:
        gasc->extensionFlag = 1;
        break;
    default:
        gasc->extensionFlag = 0;
        break;
    }
    return gasc;
}

static mp4a_MPEG_1_2_SpecificConfig_t* mp4a_create_MPEG_1_2_SpecificConfig()
{
    mp4a_MPEG_1_2_SpecificConfig_t* mpeg_1_2_sc = (mp4a_MPEG_1_2_SpecificConfig_t*)malloc( sizeof(mp4a_MPEG_1_2_SpecificConfig_t) );
    if( !mpeg_1_2_sc )
        return NULL;
    memset( mpeg_1_2_sc, 0, sizeof(mp4a_MPEG_1_2_SpecificConfig_t) );
    mpeg_1_2_sc->extension = 0; /* shall be 0. */
    return mpeg_1_2_sc;
}

/* Currently, only normal AAC, MPEG_1_2 are supported.
   For AAC, other than normal AAC, such as AAC_scalable, ER_AAC_xxx, are not supported.
   ADIF/PCE(program config element) style AudioSpecificConfig is not supported.
   aot shall not be MP4A_AUDIO_OBJECT_TYPE_SBR even if you wish to signal SBR explicitly, use sbr_mode instead.
   Frequency/channels shall be base AAC's one, even if SBR/PS.
   If other than AAC with SBR, sbr_mode shall be MP4A_AAC_SBR_NOT_SPECIFIED. */
mp4a_AudioSpecificConfig_t* mp4a_create_AudioSpecificConfig( mp4a_AudioObjectType aot, uint32_t frequency, uint32_t channels, mp4a_aac_sbr_mode sbr_mode )
{
    if( aot != MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN && aot != MP4A_AUDIO_OBJECT_TYPE_AAC_LC
        && aot != MP4A_AUDIO_OBJECT_TYPE_AAC_SSR && aot != MP4A_AUDIO_OBJECT_TYPE_AAC_LTP
        && aot != MP4A_AUDIO_OBJECT_TYPE_TwinVQ )
        return NULL;
    if( frequency == 0 )
        return NULL;

    uint8_t channelConfig;
    switch( channels ){
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
        channelConfig = channels;
        break;
    case 8:
        channelConfig = 7;
        break;
    default:
        return NULL;
    }

    mp4a_AudioSpecificConfig_t* asc = (mp4a_AudioSpecificConfig_t*)malloc( sizeof(mp4a_AudioSpecificConfig_t) );
    if( !asc )
        return NULL;
    memset( asc, 0, sizeof(mp4a_AudioSpecificConfig_t) );

    asc->sbr_mode = sbr_mode;
    asc->audioObjectType = aot;
    asc->channelConfiguration = channelConfig;

    uint8_t samplingFrequencyIndex;
    uint8_t i = 0x0;
    while( frequency < mp4a_AAC_frequency_table[i][0] )
        i++;
    asc->samplingFrequencyIndex = frequency == mp4a_AAC_frequency_table[i][1] ? i : 0xF;
    asc->samplingFrequency = frequency;
    samplingFrequencyIndex = mp4a_AAC_frequency_table[i][2];

    /* SBR settings */
    if( sbr_mode != MP4A_AAC_SBR_NOT_SPECIFIED )
    {
        /* SBR limitation */
        /* see ISO/IEC 14496-3 Table, 1.5.2.3 Levels within the profiles, 1.11 Levels for the High Efficiency AAC Profile */
        if( i < 0x3 )
        {
            free( asc );
            return NULL;
        }
        asc->extensionAudioObjectType = MP4A_AUDIO_OBJECT_TYPE_SBR;
    }
    else
        asc->extensionAudioObjectType = MP4A_AUDIO_OBJECT_TYPE_NULL;

    if( sbr_mode == MP4A_AAC_SBR_BACKWARD_COMPATIBLE || sbr_mode == MP4A_AAC_SBR_BACKWARD_COMPATIBLE )
    {
        asc->extensionSamplingFrequency = frequency * 2;
        asc->extensionSamplingFrequencyIndex = i == 0xC ? 0xF : mp4a_AAC_frequency_table[i][3];
    }
    else
    {
        asc->extensionSamplingFrequencyIndex = asc->samplingFrequencyIndex;
        asc->extensionSamplingFrequency = asc->samplingFrequency;
    }

    switch( aot ){
    case MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN:
    case MP4A_AUDIO_OBJECT_TYPE_AAC_LC:
    case MP4A_AUDIO_OBJECT_TYPE_AAC_SSR:
    case MP4A_AUDIO_OBJECT_TYPE_AAC_LTP:
    case MP4A_AUDIO_OBJECT_TYPE_SBR:
#if 0 /* FIXME: here, stop currently unsupported codecs. */
    case MP4A_AUDIO_OBJECT_TYPE_AAC_scalable:
    case MP4A_AUDIO_OBJECT_TYPE_TwinVQ: /* NOTE: I think we already have a support for TwinVQ, but how to test this? */
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LC:
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LTP:
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_scalable:
    case MP4A_AUDIO_OBJECT_TYPE_ER_Twin_VQ:
    case MP4A_AUDIO_OBJECT_TYPE_ER_BSAC:
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LD:
#endif
        asc->deepAudioSpecificConfig = mp4a_create_GASpecificConfig( samplingFrequencyIndex, channelConfig, aot );
        break;
    case MP4A_AUDIO_OBJECT_TYPE_Layer_1:
    case MP4A_AUDIO_OBJECT_TYPE_Layer_2:
    case MP4A_AUDIO_OBJECT_TYPE_Layer_3:
        asc->deepAudioSpecificConfig = mp4a_create_MPEG_1_2_SpecificConfig();
        break;
    default:
        break; /* this case is trapped below. */
    }
    if( !asc->deepAudioSpecificConfig ){
        free( asc );
        return NULL;
    }
    return asc;
}

/* ADIF/PCE(program config element) style GASpecificConfig is not supported. */
static void mp4a_put_GASpecificConfig( mp4sys_bits_t* bits, mp4a_GASpecificConfig_t* gasc )
{
    debug_if( !bits || !gasc )
        return;
    mp4sys_bits_put( bits, gasc->frameLengthFlag, 1);
    mp4sys_bits_put( bits, gasc->dependsOnCoreCoder, 1);
    mp4sys_bits_put( bits, gasc->extensionFlag, 1);
}

static void mp4a_put_MPEG_1_2_SpecificConfig( mp4sys_bits_t* bits, mp4a_MPEG_1_2_SpecificConfig_t* mpeg_1_2_sc )
{
    debug_if( !bits || !mpeg_1_2_sc )
        return;
    mp4sys_bits_put( bits, mpeg_1_2_sc->extension, 1); /* shall be 0 */
}

static inline void mp4a_put_AudioObjectType( mp4sys_bits_t* bits, mp4a_AudioObjectType aot )
{
    if( aot > MP4A_AUDIO_OBJECT_TYPE_ESCAPE )
    {
        mp4sys_bits_put( bits, MP4A_AUDIO_OBJECT_TYPE_ESCAPE, 5);
        mp4sys_bits_put( bits, aot - MP4A_AUDIO_OBJECT_TYPE_ESCAPE - 1, 6);
    }
    else
        mp4sys_bits_put( bits, aot, 5);
}

static inline void mp4a_put_SamplingFrequencyIndex( mp4sys_bits_t* bits, uint8_t samplingFrequencyIndex, uint32_t samplingFrequency )
{
    mp4sys_bits_put( bits, samplingFrequencyIndex, 4);
    if( samplingFrequencyIndex == 0xF )
        mp4sys_bits_put( bits, samplingFrequency, 24);
}

/* Currently, only normal AAC, MPEG_1_2 are supported.
   For AAC, other than normal AAC, such as AAC_scalable, ER_AAC_xxx, are not supported.
   ADIF/PCE(program config element) style AudioSpecificConfig is not supported either. */
void mp4a_put_AudioSpecificConfig( isom_bs_t* bs, mp4a_AudioSpecificConfig_t* asc )
{
    debug_if( !bs || !asc )
        return;
    mp4sys_bits_t bits;
    mp4sys_bits_init( &bits, bs );

    if( asc->sbr_mode == MP4A_AAC_SBR_HIERARCHICAL )
        mp4a_put_AudioObjectType( &bits, asc->extensionAudioObjectType ); /* puts MP4A_AUDIO_OBJECT_TYPE_SBR */
    else
        mp4a_put_AudioObjectType( &bits, asc->audioObjectType );
    mp4a_put_SamplingFrequencyIndex( &bits, asc->samplingFrequencyIndex, asc->samplingFrequency );
    mp4sys_bits_put( &bits, asc->channelConfiguration, 4 );
    if( asc->sbr_mode == MP4A_AAC_SBR_HIERARCHICAL )
    {
        mp4a_put_SamplingFrequencyIndex( &bits, asc->extensionSamplingFrequencyIndex, asc->extensionSamplingFrequency );
        mp4a_put_AudioObjectType( &bits, asc->audioObjectType );
    }
    switch( asc->audioObjectType ){
    case MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN:
    case MP4A_AUDIO_OBJECT_TYPE_AAC_LC:
    case MP4A_AUDIO_OBJECT_TYPE_AAC_SSR:
    case MP4A_AUDIO_OBJECT_TYPE_AAC_LTP:
    case MP4A_AUDIO_OBJECT_TYPE_SBR:
#if 0 /* FIXME: here, stop currently unsupported codecs */
    case MP4A_AUDIO_OBJECT_TYPE_AAC_scalable:
    case MP4A_AUDIO_OBJECT_TYPE_TwinVQ: /* NOTE: I think we already have a support for TwinVQ, but how to test this? */
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LC:
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LTP:
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_scalable:
    case MP4A_AUDIO_OBJECT_TYPE_ER_Twin_VQ:
    case MP4A_AUDIO_OBJECT_TYPE_ER_BSAC:
    case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LD:
#endif
        mp4a_put_GASpecificConfig( &bits, (mp4a_GASpecificConfig_t*)asc->deepAudioSpecificConfig );
        break;
    case MP4A_AUDIO_OBJECT_TYPE_Layer_1:
    case MP4A_AUDIO_OBJECT_TYPE_Layer_2:
    case MP4A_AUDIO_OBJECT_TYPE_Layer_3:
        mp4a_put_MPEG_1_2_SpecificConfig( &bits, (mp4a_MPEG_1_2_SpecificConfig_t*)asc->deepAudioSpecificConfig );
        break;
    default:
        break; /* FIXME: do we have to return error? */
    }

    /* FIXME: Error Resiliant stuff omitted here. */

    if( asc->sbr_mode == MP4A_AAC_SBR_BACKWARD_COMPATIBLE || asc->sbr_mode == MP4A_AAC_SBR_NONE )
    {
        mp4sys_bits_put( &bits, 0x2b7, 11 );
        mp4a_put_AudioObjectType( &bits, asc->extensionAudioObjectType ); /* puts MP4A_AUDIO_OBJECT_TYPE_SBR */
        if( asc->extensionAudioObjectType == MP4A_AUDIO_OBJECT_TYPE_SBR ) /* this is always true, due to current spec */
        {
            /* sbrPresentFlag */
            if( asc->sbr_mode == MP4A_AAC_SBR_NONE )
                mp4sys_bits_put( &bits, 0x0, 1 );
            else
            {
                mp4sys_bits_put( &bits, 0x1, 1 );
                mp4a_put_SamplingFrequencyIndex( &bits, asc->extensionSamplingFrequencyIndex, asc->extensionSamplingFrequency );
            }
        }
    }
    mp4sys_bits_align( &bits );
}

/***************************************************************************
    MPEG-4 Systems
***************************************************************************/

/* 8.2.1 Overview Table 1 - List of Class Tags for Descriptors */
typedef enum {
    MP4SYS_DESCRIPTOR_TAG_Forbidden                           = 0x00, /* Forbidden */
    MP4SYS_DESCRIPTOR_TAG_ObjectDescrTag                      = 0x01, /* ObjectDescrTag */
    MP4SYS_DESCRIPTOR_TAG_InitialObjectDescrTag               = 0x02, /* InitialObjectDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ES_DescrTag                         = 0x03, /* ES_DescrTag */
    MP4SYS_DESCRIPTOR_TAG_DecoderConfigDescrTag               = 0x04, /* DecoderConfigDescrTag */
    MP4SYS_DESCRIPTOR_TAG_DecSpecificInfoTag                  = 0x05, /* DecSpecificInfoTag */
    MP4SYS_DESCRIPTOR_TAG_SLConfigDescrTag                    = 0x06, /* SLConfigDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ContentIdentDescrTag                = 0x07, /* ContentIdentDescrTag */
    MP4SYS_DESCRIPTOR_TAG_SupplContentIdentDescrTag           = 0x08, /* SupplContentIdentDescrTag */
    MP4SYS_DESCRIPTOR_TAG_IPI_DescrPointerTag                 = 0x09, /* IPI_DescrPointerTag */
    MP4SYS_DESCRIPTOR_TAG_IPMP_DescrPointerTag                = 0x0A, /* IPMP_DescrPointerTag */
    MP4SYS_DESCRIPTOR_TAG_IPMP_DescrTag                       = 0x0B, /* IPMP_DescrTag */
    MP4SYS_DESCRIPTOR_TAG_QoS_DescrTag                        = 0x0C, /* QoS_DescrTag */
    MP4SYS_DESCRIPTOR_TAG_RegistrationDescrTag                = 0x0D, /* RegistrationDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ES_ID_IncTag                        = 0x0E, /* ES_ID_IncTag */
    MP4SYS_DESCRIPTOR_TAG_ES_ID_RefTag                        = 0x0F, /* ES_ID_RefTag */
    MP4SYS_DESCRIPTOR_TAG_MP4_IOD_Tag                         = 0x10, /* MP4_IOD_Tag, InitialObjectDescriptor for MP4 */
    MP4SYS_DESCRIPTOR_TAG_MP4_OD_Tag                          = 0x11, /* MP4_OD_Tag, ObjectDescriptor for MP4 */
    MP4SYS_DESCRIPTOR_TAG_IPI_DescrPointerRefTag              = 0x12, /* IPI_DescrPointerRefTag */
    MP4SYS_DESCRIPTOR_TAG_ExtendedProfileLevelDescrTag        = 0x13, /* ExtendedProfileLevelDescrTag */
    MP4SYS_DESCRIPTOR_TAG_profileLevelIndicationIndexDescrTag = 0x14, /* profileLevelIndicationIndexDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ContentClassificationDescrTag       = 0x40, /* ContentClassificationDescrTag */
    MP4SYS_DESCRIPTOR_TAG_KeyWordDescrTag                     = 0x41, /* KeyWordDescrTag */
    MP4SYS_DESCRIPTOR_TAG_RatingDescrTag                      = 0x42, /* RatingDescrTag */
    MP4SYS_DESCRIPTOR_TAG_LanguageDescrTag                    = 0x43, /* LanguageDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ShortTextualDescrTag                = 0x44, /* ShortTextualDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ExpandedTextualDescrTag             = 0x45, /* ExpandedTextualDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ContentCreatorNameDescrTag          = 0x46, /* ContentCreatorNameDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ContentCreationDateDescrTag         = 0x47, /* ContentCreationDateDescrTag */
    MP4SYS_DESCRIPTOR_TAG_OCICreatorNameDescrTag              = 0x48, /* OCICreatorNameDescrTag */
    MP4SYS_DESCRIPTOR_TAG_OCICreationDateDescrTag             = 0x49, /* OCICreationDateDescrTag */
    MP4SYS_DESCRIPTOR_TAG_SmpteCameraPositionDescrTag         = 0x4A, /* SmpteCameraPositionDescrTag */
    MP4SYS_DESCRIPTOR_TAG_Forbidden1                          = 0xFF, /* Forbidden */
} mp4sys_descriptor_tag;
//    MP4SYS_DESCRIPTOR_TAG_ES_DescrRemoveRefTag                = 0x07, /* FIXME: (command tag), see 14496-14 3.1.3 Object Descriptors */

typedef struct {
    uint32_t size; // 2^28 at most
    mp4sys_descriptor_tag tag;
} mp4sys_descriptor_head_t;

/* 8.6.7 DecoderSpecificInfo */
/* contents varies depends on ObjectTypeIndication and StreamType. */
typedef struct {
    mp4sys_descriptor_head_t header;
    uint8_t* data;
} mp4sys_DecoderSpecificInfo_t;

/* 8.6.6 DecoderConfigDescriptor */
typedef struct {
    mp4sys_descriptor_head_t header;
    mp4sys_object_type_indication objectTypeIndication;
    mp4sys_stream_type streamType;
    // uint8_t upStream; /* bit(1), always 0 in this muxer, used for interactive contents. */
    // uint8_t reserved=1; /* const bit(1) */
    uint32_t bufferSizeDB; /* maybe CPB size in bytes, NOT bits. */
    uint32_t maxBitrate;
    uint32_t avgBitrate; /* 0 if VBR */
    mp4sys_DecoderSpecificInfo_t* decSpecificInfo;  /* can be NULL. */
    /* 14496-1 seems to say if we are in IOD(InitialObjectDescriptor), we might use this.
       See 8.6.20, 8.6.19 ExtensionProfileLevelDescr, 8.7.3.2 The Initial Object Descriptor.
       But I don't think this is mandatory despite 14496-1, because 14496-14 says, in OD or IOD,
       we have to use ES_ID_Inc instead of ES_Descriptor, which does not have DecoderConfigDescriptor. */
    // profileLevelIndicationIndexDescriptor profileLevelIndicationIndexDescr [0..255];
} mp4sys_DecoderConfigDescriptor_t;

/* 8.6.8 SLConfigDescriptor */
typedef struct {
    mp4sys_descriptor_head_t header;
    uint8_t predefined; /* MP4 file that does not use URL_Flag shall have constant value 0x02 */
    /* custom values may be placed here if predefined == 0, but we need not care in MP4 files
       without external references */
} mp4sys_SLConfigDescriptor_t;

/* 8.6.5 ES_Descriptor */
typedef struct
{
    mp4sys_descriptor_head_t header;
    uint16_t ES_ID;
    // uint8_t ESD_flags; /* this 8bit value is always 0 in this muxer, see below for detail */
    /*
    typedef struct TAG_ESD_flags{
        unsigned streamDependenceFlag : 1; // no stream depencies between streams in this muxer, ES_ID of another elementary stream
        unsigned URL_Flag             : 1; // no external URL referencing stream in MP4
        unsigned OCRstreamFlag        : 1; // no Object Clock Reference stream in this muxer (shall be false in MP4, useful if we're importing from MPEG-2?)
        unsigned streamPriority       : 5; // no priority among streams in this muxer, higher is important
    } ESD_flags;
    // These are omitted according to ESD_flags == 0
    //if(streamDependenceFlag)
        uint16_t dependsOn_ES_ID; // bit(16)
    //if(URL_Flag)
        uint8_t URLlength; // bit(8)
        char URLstring[256]; // bit(8)[], UTF-8 encoded URL
    //if(OCR_ES_Flag)
        uint16_t OCR_ES_Id;
    */
    mp4sys_DecoderConfigDescriptor_t* decConfigDescr; /* cannot be NULL. */
    mp4sys_SLConfigDescriptor_t* slConfigDescr;
    /* descriptors below are not mandatory, I think Language Descriptor may somewhat useful */
    /*
    IPI_DescrPointer ipiPtr[0 .. 1];               // used to indicate using other ES's IP_IdentificationDataSet
    IP_IdentificationDataSet ipIDS[0 .. 255];      // abstract class, actually ContentIdentificationDescriptor(for commercial contents management),
                                                   // or SupplementaryContentIdentificationDescriptor(for embedding titles)
    IPMP_DescriptorPointer ipmpDescrPtr[0 .. 255]; // used to intellectual property / protection management
    LanguageDescriptor langDescr[0 .. 255];        // used to identify the language of the audio/speech or text object
    QoS_Descriptor qosDescr[0 .. 1];               // used to achieve QoS
    RegistrationDescriptor regDescr[0 .. 1];       // used to carry elementary streams with data whose format is not recognized by ISO/IEC 14496-1
    ExtensionDescriptor extDescr[0 .. 255];        // abstract class, actually defined no subclass, maybe useless
    */
} mp4sys_ES_Descriptor_t;

/* 14496-14 3.1.3 Object Descriptors (ES_ID_Inc) */
typedef struct {
    mp4sys_descriptor_head_t header;
    uint32_t Track_ID;
} mp4sys_ES_ID_Inc_t;

/* 14496-1 8.6.3 ObjectDescriptor / 8.6.4 InitialObjectDescriptor */
typedef struct {
    mp4sys_descriptor_head_t header;
    uint16_t ObjectDescriptorID;
    // uint8_t URL_Flag; /* bit(1) */
    uint8_t includeInlineProfileLevelFlag; /* bit(1) */
    //const uint8_t reserved=0x0F(0b1111) or 0x1F(0b1.1111); /* bit(4 or 5), width is 4 for IOD, 5 for OD */
    /* if (URL_Flag) {
        uint8_t URLlength; // bit(8)
        char URLstring[256]; // bit(8)[]
    }*/
    /* else { */
        mp4sys_ODProfileLevelIndication ODProfileLevelIndication;
        mp4sys_sceneProfileLevelIndication sceneProfileLevelIndication;
        mp4sys_audioProfileLevelIndication audioProfileLevelIndication;
        mp4sys_visualProfileLevelIndication visualProfileLevelIndication;
        mp4sys_graphicsProfileLevelIndication graphicsProfileLevelIndication;
        isom_entry_list_t* esDescr; /* List of ES_ID_Inc, not ES_Descriptor defined in 14496-1. 14496-14 overrides. */
        // OCI_Descriptor ociDescr[0 .. 255];
        // IPMP_DescriptorPointer ipmpDescrPtr[0 .. 255];
    /* } */
    // ExtensionDescriptor extDescr[0 .. 255];
} mp4sys_ObjectDescriptor_t;

int mp4sys_remove_DecoderSpecificInfo( mp4sys_ES_Descriptor_t* esd )
{
    if( !esd || !esd->decConfigDescr )
        return -1;
    if( !esd->decConfigDescr->decSpecificInfo )
        return 0;
    if( esd->decConfigDescr->decSpecificInfo->data )
        free( esd->decConfigDescr->decSpecificInfo->data );
    free( esd->decConfigDescr->decSpecificInfo );
    esd->decConfigDescr->decSpecificInfo = NULL;
    return 0;
}

int mp4sys_remove_DecoderConfigDescriptor( mp4sys_ES_Descriptor_t* esd )
{
    if( !esd )
        return -1;
    if( !esd->decConfigDescr )
        return 0;
    mp4sys_remove_DecoderSpecificInfo( esd );
    free( esd->decConfigDescr );
    esd->decConfigDescr = NULL;
    return 0;
}

int mp4sys_remove_SLConfigDescriptor( mp4sys_ES_Descriptor_t* esd )
{
    if( !esd )
        return -1;
    if( !esd->slConfigDescr )
        return 0;
    free( esd->slConfigDescr );
    esd->slConfigDescr = NULL;
    return 0;
}

int mp4sys_remove_ES_Descriptor( mp4sys_ES_Descriptor_t* esd )
{
    if( !esd )
        return 0;
    mp4sys_remove_DecoderConfigDescriptor( esd );
    mp4sys_remove_SLConfigDescriptor( esd );
    free( esd );
    return 0;
}

int mp4sys_remove_ES_ID_Incs( mp4sys_ObjectDescriptor_t* od )
{
    if( !od )
        return -1;
    if( od->esDescr )
    {
        isom_remove_list( od->esDescr, NULL );
        od->esDescr = NULL;
    }
    return 0;
}

int mp4sys_remove_ObjectDescriptor( mp4sys_ObjectDescriptor_t* od )
{
    if( !od )
        return 0;
    mp4sys_remove_ES_ID_Incs( od );
    free( od );
    return 0;
}

int mp4sys_add_DecoderSpecificInfo( mp4sys_ES_Descriptor_t* esd, void* dsi_payload, uint32_t dsi_payload_length )
{
    if( !esd || !esd->decConfigDescr || dsi_payload == NULL || dsi_payload_length == 0 )
        return -1;
    mp4sys_DecoderSpecificInfo_t* dsi = (mp4sys_DecoderSpecificInfo_t*)malloc( sizeof(mp4sys_DecoderSpecificInfo_t) );
    if( !dsi )
        return -1;
    memset( dsi, 0, sizeof(mp4sys_DecoderSpecificInfo_t) );
    dsi->header.tag = MP4SYS_DESCRIPTOR_TAG_DecSpecificInfoTag;
    dsi->data = malloc( dsi_payload_length );
    if( !dsi->data )
    {
        free( dsi );
        return -1;
    }
    memcpy( dsi->data, dsi_payload, dsi_payload_length );
    dsi->header.size = dsi_payload_length;
    debug_if( mp4sys_remove_DecoderSpecificInfo( esd ) )
    {
        free( dsi->data );
        free( dsi );
        return -1;
    }
    esd->decConfigDescr->decSpecificInfo = dsi;
    return 0;
}

/*
    bufferSizeDB is byte unit, NOT bit unit.
    avgBitrate is 0 if VBR
*/
int mp4sys_add_DecoderConfigDescriptor(
    mp4sys_ES_Descriptor_t* esd,
    mp4sys_object_type_indication objectTypeIndication,
    mp4sys_stream_type streamType,
    uint32_t bufferSizeDB,
    uint32_t maxBitrate,
    uint32_t avgBitrate
){
    if( !esd )
        return -1;
    mp4sys_DecoderConfigDescriptor_t* dcd = (mp4sys_DecoderConfigDescriptor_t*)malloc( sizeof(mp4sys_DecoderConfigDescriptor_t) );
    if( !dcd )
        return -1;
    memset( dcd, 0, sizeof(mp4sys_DecoderConfigDescriptor_t) );
    dcd->header.tag = MP4SYS_DESCRIPTOR_TAG_DecoderConfigDescrTag;
    dcd->objectTypeIndication = objectTypeIndication;
    dcd->streamType = streamType;
    dcd->bufferSizeDB = bufferSizeDB;
    dcd->maxBitrate = maxBitrate;
    dcd->avgBitrate = avgBitrate;
    debug_if( mp4sys_remove_DecoderConfigDescriptor( esd ) )
    {
        free( dcd );
        return -1;
    }
    esd->decConfigDescr = dcd;
    return 0;
}

/*
    bufferSizeDB is byte unit, NOT bit unit.
    avgBitrate is 0 if VBR
*/
int mp4sys_update_DecoderConfigDescriptor( mp4sys_ES_Descriptor_t* esd, uint32_t bufferSizeDB, uint32_t maxBitrate, uint32_t avgBitrate )
{
    if( !esd || !esd->decConfigDescr )
        return -1;
    mp4sys_DecoderConfigDescriptor_t* dcd = esd->decConfigDescr;
    dcd->bufferSizeDB = bufferSizeDB;
    dcd->maxBitrate = maxBitrate;
    dcd->avgBitrate = avgBitrate;
    return 0;
}

int mp4sys_add_SLConfigDescriptor( mp4sys_ES_Descriptor_t* esd )
{
    if( !esd )
        return -1;
    mp4sys_SLConfigDescriptor_t* slcd = (mp4sys_SLConfigDescriptor_t*)malloc( sizeof(mp4sys_SLConfigDescriptor_t) );
    if( !slcd )
        return -1;
    memset( slcd, 0, sizeof(mp4sys_SLConfigDescriptor_t) );
    slcd->header.tag = MP4SYS_DESCRIPTOR_TAG_SLConfigDescrTag;
    slcd->predefined = 0x02; /* MP4 file which does not use URL_Flag shall have constant value 0x02 */
    debug_if( mp4sys_remove_SLConfigDescriptor( esd ) )
    {
        free( slcd );
        return -1;
    }
    esd->slConfigDescr = slcd;
    return 0;
}

/* ES_ID might be usually 0 or lower 16 bits of the TrackID
   14496-14 says, "set to 0 as stored; when built into a stream, the lower 16 bits of the TrackID are used."
   I'm not sure about actual meaning of "stored" and "built into a stream", but maybe 0 will do in stsd(esds). */
mp4sys_ES_Descriptor_t* mp4sys_create_ES_Descriptor( uint16_t ES_ID )
{
    mp4sys_ES_Descriptor_t* esd = (mp4sys_ES_Descriptor_t*)malloc( sizeof(mp4sys_ES_Descriptor_t) );
    if( !esd )
        return NULL;
    memset( esd, 0, sizeof(mp4sys_ES_Descriptor_t) );
    esd->header.tag = MP4SYS_DESCRIPTOR_TAG_ES_DescrTag;
    esd->ES_ID = ES_ID;
    return esd;
}

/* NOTE: This is only for MP4_IOD and MP4_OD, not for Iso Base Media's ObjectDescriptor and InitialObjectDescriptor */
int mp4sys_add_ES_ID_Inc( mp4sys_ObjectDescriptor_t* od, uint32_t Track_ID )
{
    if( !od )
        return -1;
    if( !od->esDescr && ( od->esDescr = isom_create_entry_list() ) == NULL )
        return -1;
    mp4sys_ES_ID_Inc_t* es_id_inc = (mp4sys_ES_ID_Inc_t*)malloc( sizeof(mp4sys_ES_ID_Inc_t) );
    if( !es_id_inc )
        return -1;
    memset( es_id_inc, 0, sizeof(mp4sys_ES_ID_Inc_t) );
    es_id_inc->header.tag = MP4SYS_DESCRIPTOR_TAG_ES_ID_IncTag;
    es_id_inc->Track_ID = Track_ID;
    if( isom_add_entry( od->esDescr, es_id_inc ) )
    {
        free( es_id_inc );
        return -1;
    }
    return 0;
}

/* NOTE: This is only for MP4_OD, not for Iso Base Media's ObjectDescriptor */
mp4sys_ObjectDescriptor_t* mp4sys_create_ObjectDescriptor( uint16_t ObjectDescriptorID )
{
    mp4sys_ObjectDescriptor_t* od = (mp4sys_ObjectDescriptor_t*)malloc( sizeof(mp4sys_ObjectDescriptor_t) );
    if( !od )
        return NULL;
    memset( od, 0, sizeof(mp4sys_ObjectDescriptor_t) );
    od->header.tag = MP4SYS_DESCRIPTOR_TAG_MP4_OD_Tag;
    od->ObjectDescriptorID = ObjectDescriptorID;
    od->includeInlineProfileLevelFlag = 1; /* 1 as part of reserved flag. */
    od->ODProfileLevelIndication = MP4SYS_OD_PLI_NONE_REQUIRED;
    od->sceneProfileLevelIndication = MP4SYS_SCENE_PLI_NONE_REQUIRED;
    od->audioProfileLevelIndication = MP4SYS_AUDIO_PLI_NONE_REQUIRED;
    od->visualProfileLevelIndication = MP4SYS_VISUAL_PLI_NONE_REQUIRED;
    od->graphicsProfileLevelIndication = MP4SYS_GRAPHICS_PLI_NONE_REQUIRED;
    return od;
}

/* NOTE: This is only for MP4_IOD, not for Iso Base Media's InitialObjectDescriptor */
int mp4sys_to_InitialObjectDescriptor(
    mp4sys_ObjectDescriptor_t* od,
    uint8_t include_inline_pli,
    mp4sys_ODProfileLevelIndication od_pli,
    mp4sys_sceneProfileLevelIndication scene_pli,
    mp4sys_audioProfileLevelIndication audio_pli,
    mp4sys_visualProfileLevelIndication visual_pli,
    mp4sys_graphicsProfileLevelIndication graph_pli
){
    if( !od )
        return -1;
    od->header.tag = MP4SYS_DESCRIPTOR_TAG_MP4_IOD_Tag;
    od->includeInlineProfileLevelFlag = include_inline_pli;
    od->ODProfileLevelIndication = od_pli;
    od->sceneProfileLevelIndication = scene_pli;
    od->audioProfileLevelIndication = audio_pli;
    od->visualProfileLevelIndication = visual_pli;
    od->graphicsProfileLevelIndication = graph_pli;
    return 0;
}

/* returns total size of descriptor, including header, 2 at least */
static inline uint32_t mp4sys_get_descriptor_size( uint32_t payload_size_in_byte )
{
    /* descriptor length will be split into 7bits
       see 14496-1 16.3.3 Expandable classes and J.1 Length encoding of descriptors and commands */
    uint32_t i;
    for( i = 1; payload_size_in_byte >> ( 7 * i ) ; i++);
    return payload_size_in_byte + i + 1; /* +1 means tag's space */
}

static uint32_t mp4sys_update_DecoderSpecificInfo_size( mp4sys_ES_Descriptor_t* esd )
{
    debug_if( !esd || !esd->decConfigDescr )
        return 0;
    if( !esd->decConfigDescr->decSpecificInfo )
        return 0;
    /* no need to update, header.size is already set */
    return mp4sys_get_descriptor_size( esd->decConfigDescr->decSpecificInfo->header.size );
}

static uint32_t mp4sys_update_DecoderConfigDescriptor_size( mp4sys_ES_Descriptor_t* esd )
{
    debug_if( !esd )
        return 0;
    if( !esd->decConfigDescr )
        return 0;
    uint32_t size = 13;
    size += mp4sys_update_DecoderSpecificInfo_size( esd );
    esd->decConfigDescr->header.size = size;
    return mp4sys_get_descriptor_size( size );
}

static uint32_t mp4sys_update_SLConfigDescriptor_size( mp4sys_ES_Descriptor_t* esd )
{
    debug_if( !esd )
        return 0;
    if( !esd->slConfigDescr )
        return 0;
    esd->slConfigDescr->header.size = 1;
    return mp4sys_get_descriptor_size( esd->slConfigDescr->header.size );
}

uint32_t mp4sys_update_ES_Descriptor_size( mp4sys_ES_Descriptor_t* esd )
{
    if( !esd )
        return 0;
    uint32_t size = 3;
    size += mp4sys_update_DecoderConfigDescriptor_size( esd );
    size += mp4sys_update_SLConfigDescriptor_size( esd );
    esd->header.size = size;
    return mp4sys_get_descriptor_size( size );
}

static uint32_t mp4sys_update_ES_ID_Inc_size( mp4sys_ES_ID_Inc_t* es_id_inc )
{
    debug_if( !es_id_inc )
        return 0;
    es_id_inc->header.size = 4;
    return mp4sys_get_descriptor_size( es_id_inc->header.size );
}

/* This function works as aggregate of ES_ID_Incs, so this function itself updates no size information */
static uint32_t mp4sys_update_ES_ID_Incs_size( mp4sys_ObjectDescriptor_t* od )
{
    debug_if( !od )
        return 0;
    if( !od->esDescr )
        return 0;
    uint32_t size = 0;
    for( isom_entry_t *entry = od->esDescr->head; entry; entry = entry->next )
        size += mp4sys_update_ES_ID_Inc_size( (mp4sys_ES_ID_Inc_t*)entry->data );
    return size;
}

uint32_t mp4sys_update_ObjectDescriptor_size( mp4sys_ObjectDescriptor_t* od )
{
    if( !od )
        return 0;
    uint32_t size = od->header.tag == MP4SYS_DESCRIPTOR_TAG_MP4_IOD_Tag ? 7 : 2;
    size += mp4sys_update_ES_ID_Incs_size( od );
    od->header.size = size;
    return mp4sys_get_descriptor_size( size );
}

static int mp4sys_put_descriptor_header( isom_bs_t *bs, mp4sys_descriptor_head_t* header )
{
    debug_if( !bs || !header )
        return -1;
    isom_bs_put_byte( bs, header->tag );
    /* descriptor length will be splitted into 7bits
       see 14496-1 16.3.3 Expandable classes and J.1 Length encoding of descriptors and commands */
    for( uint32_t i = mp4sys_get_descriptor_size( header->size ) - header->size - 2; i; i-- ){
        isom_bs_put_byte( bs, ( header->size >> ( 7 * i ) ) | 0x80 );
    }
    isom_bs_put_byte( bs, header->size );
    return 0;
}

static int mp4sys_write_DecoderSpecificInfo( isom_bs_t *bs, mp4sys_DecoderSpecificInfo_t* dsi )
{
    debug_if( !bs )
        return -1;
    if( !dsi )
        return 0; /* can be NULL */
    debug_if( mp4sys_put_descriptor_header( bs, &dsi->header ) )
        return -1;
    if( dsi->data && dsi->header.size != 0 )
        isom_bs_put_bytes( bs, dsi->data, dsi->header.size );
    return isom_bs_write_data( bs );
}

static int mp4sys_write_DecoderConfigDescriptor( isom_bs_t *bs, mp4sys_DecoderConfigDescriptor_t* dcd )
{
    debug_if( !bs )
        return -1;
    if( !dcd )
        return -1; /* cannot be NULL */
    debug_if( mp4sys_put_descriptor_header( bs, &dcd->header ) )
        return -1;
    isom_bs_put_byte( bs, dcd->objectTypeIndication );
    isom_bs_put_byte( bs, (dcd->streamType << 2) | 0x01 ); /* contains upStream<1> = 0, reserved<1> = 1 */
    isom_bs_put_be24( bs, dcd->bufferSizeDB );
    isom_bs_put_be32( bs, dcd->maxBitrate );
    isom_bs_put_be32( bs, dcd->avgBitrate );
    if( isom_bs_write_data( bs ) )
        return -1;
    return mp4sys_write_DecoderSpecificInfo( bs, dcd->decSpecificInfo );
    /* here, profileLevelIndicationIndexDescriptor is omitted */
}

static int mp4sys_write_SLConfigDescriptor( isom_bs_t *bs, mp4sys_SLConfigDescriptor_t* slcd )
{
    debug_if( !bs )
        return -1;
    if( !slcd )
        return 0;
    debug_if( mp4sys_put_descriptor_header( bs, &slcd->header ) )
        return -1;
    isom_bs_put_byte( bs, slcd->predefined );
    return isom_bs_write_data( bs );
}

int mp4sys_write_ES_Descriptor( isom_bs_t *bs, mp4sys_ES_Descriptor_t* esd )
{
    if( !bs || !esd )
        return -1;
    debug_if( mp4sys_put_descriptor_header( bs, &esd->header ) )
        return -1;
    isom_bs_put_be16( bs, esd->ES_ID );
    isom_bs_put_byte( bs, 0 ); /* streamDependenceFlag<1>, URL_Flag<1>, OCRstreamFlag<1>, streamPriority<5> */
    /* here, some syntax elements are omitted due to previous flags (all 0) */
    if( isom_bs_write_data( bs ) )
        return -1;
    if( mp4sys_write_DecoderConfigDescriptor( bs, esd->decConfigDescr ) )
        return -1;
    return mp4sys_write_SLConfigDescriptor( bs, esd->slConfigDescr );
}

static int mp4sys_put_ES_ID_Inc( isom_bs_t *bs, mp4sys_ES_ID_Inc_t* es_id_inc )
{
    debug_if( !es_id_inc )
        return 0;
    debug_if( mp4sys_put_descriptor_header( bs, &es_id_inc->header ) )
        return -1;
    isom_bs_put_be32( bs, es_id_inc->Track_ID );
    return 0;
}

/* This function works as aggregate of ES_ID_Incs */
static int mp4sys_write_ES_ID_Incs( isom_bs_t *bs, mp4sys_ObjectDescriptor_t* od )
{
    debug_if( !od )
        return 0;
    if( !od->esDescr )
        return 0; /* This may violate the spec, but some muxer do this */
    for( isom_entry_t *entry = od->esDescr->head; entry; entry = entry->next )
        mp4sys_put_ES_ID_Inc( bs, (mp4sys_ES_ID_Inc_t*)entry->data );
    return isom_bs_write_data( bs );
}

int mp4sys_write_ObjectDescriptor( isom_bs_t *bs, mp4sys_ObjectDescriptor_t* od )
{
    if( !bs || !od )
        return -1;
    debug_if( mp4sys_put_descriptor_header( bs, &od->header ) )
        return -1;
    uint16_t temp = (od->ObjectDescriptorID << 6);
    // temp |= (0x0 << 5); /* URL_Flag */
    temp |= (od->includeInlineProfileLevelFlag << 4); /* if MP4_OD, includeInlineProfileLevelFlag is 0x1. */
    temp |= 0xF;  /* reserved */
    isom_bs_put_be16( bs, temp );
    /* here, since we don't support URL_Flag, we put ProfileLevelIndications */
    if( od->header.tag == MP4SYS_DESCRIPTOR_TAG_MP4_IOD_Tag )
    {
        isom_bs_put_byte( bs, od->ODProfileLevelIndication );
        isom_bs_put_byte( bs, od->sceneProfileLevelIndication );
        isom_bs_put_byte( bs, od->audioProfileLevelIndication );
        isom_bs_put_byte( bs, od->visualProfileLevelIndication );
        isom_bs_put_byte( bs, od->graphicsProfileLevelIndication );
    }
    if( isom_bs_write_data( bs ) )
        return -1;
    return mp4sys_write_ES_ID_Incs( bs, od );
}

/**** following functions are for facilitation purpose ****/

mp4sys_ES_Descriptor_t* mp4sys_setup_ES_Descriptor( mp4sys_ES_Descriptor_params_t* params )
{
    if( !params )
        return NULL;
    mp4sys_ES_Descriptor_t* esd = mp4sys_create_ES_Descriptor( params->ES_ID );
    if( !esd )
        return NULL;
    if( mp4sys_add_SLConfigDescriptor( esd )
        || mp4sys_add_DecoderConfigDescriptor( esd, params->objectTypeIndication, params->streamType, params->bufferSizeDB, params->maxBitrate, params->avgBitrate )
        || ( params->dsi_payload && params->dsi_payload_length != 0 && mp4sys_add_DecoderSpecificInfo( esd, params->dsi_payload, params->dsi_payload_length ) )
    ){
        mp4sys_remove_ES_Descriptor( esd );
        return NULL;
    }
    return esd;
}

/***************************************************************************
    summary and AudioSpecificConfig relative tools
***************************************************************************/

/* create AudioSpecificConfig as memory block from summary, and set it into that summary itself */
int mp4sys_setup_AudioSpecificConfig( mp4sys_audio_summary_t* summary )
{
    if( !summary )
        return -1;
    isom_bs_t* bs = isom_bs_create( NULL ); /* no file writing */
    if( !bs )
        return -1;
    mp4a_AudioSpecificConfig_t* asc = mp4a_create_AudioSpecificConfig( summary->aot, summary->frequency, summary->channels, summary->sbr_mode );
    if( !asc )
    {
        isom_bs_cleanup( bs );
        return -1;
    }

    mp4a_put_AudioSpecificConfig( bs, asc );
    void* new_asc;
    uint32_t new_length;
    new_asc = isom_bs_export_data( bs, &new_length );
    mp4a_remove_AudioSpecificConfig( asc );
    isom_bs_cleanup( bs );

    if( !new_asc )
        return -1;
    if( summary->exdata )
        free( summary->exdata );
    summary->exdata = new_asc;
    summary->exdata_length = new_length;
    return 0 ;
}

/* Set AudioSpecificConfig into summary from memory block */
int mp4sys_summary_add_AudioSpecificConfig( mp4sys_audio_summary_t* summary, void* asc, uint32_t asc_length )
{
    if( !summary )
        return -1;
    /* atomic operation */
    void* new_asc = NULL;
    if( asc && asc_length != 0 )
    {
        new_asc = malloc( asc_length );
        if( !new_asc )
            return -1;
        memcpy( new_asc, asc, asc_length );
        summary->exdata_length = asc_length;
    }
    else
        summary->exdata_length = 0;

    if( summary->exdata )
        free( summary->exdata );
    summary->exdata = new_asc;
    return 0;
}

void mp4sys_cleanup_audio_summary( mp4sys_audio_summary_t* summary )
{
    if( !summary )
        return;
    if( summary->exdata )
        free( summary->exdata );
    free( summary );
}

/* NOTE: This function is not strictly preferable, but accurate.
   The spec of audioProfileLevelIndication is too much complicated. */
mp4sys_audioProfileLevelIndication mp4sys_get_audioProfileLevelIndication( mp4sys_audio_summary_t* summary )
{
    if( !summary || summary->stream_type != MP4SYS_STREAM_TYPE_AudioStream
        || summary->channels == 0 || summary->frequency == 0 )
        return MP4SYS_AUDIO_PLI_NONE_REQUIRED; /* means error. */
    if( summary->object_type_indication != MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3 )
        return MP4SYS_AUDIO_PLI_NOT_SPECIFIED; /* This is of audio stream, but not described in ISO/IEC 14496-3. */

    mp4sys_audioProfileLevelIndication pli = MP4SYS_AUDIO_PLI_NOT_SPECIFIED;
    switch( summary->aot )
    {
    case MP4A_AUDIO_OBJECT_TYPE_AAC_LC:
        if( summary->sbr_mode == MP4A_AAC_SBR_HIERARCHICAL )
        {
            /* NOTE: This is not strictly preferable, but accurate; just possibly over-estimated.
               We do not expect to use MP4A_AAC_SBR_HIERARCHICAL mode without SBR, nor downsampled mode with SBR. */
            if( summary->channels <= 2 && summary->frequency <= 24 )
                pli = MP4SYS_AUDIO_PLI_HE_AAC_L2;
            else if( summary->channels <= 5 && summary->frequency <= 48 )
                pli = MP4SYS_AUDIO_PLI_HE_AAC_L5;
            else
                pli = MP4SYS_AUDIO_PLI_NOT_SPECIFIED;
            break;
        }
        /* pretending plain AAC-LC, if actually HE-AAC. */
        static const uint32_t mp4sys_aac_pli_table[5][3] = {
            /* channels, frequency,    audioProfileLevelIndication */
            {         6,     96000,        MP4SYS_AUDIO_PLI_AAC_L5 }, /* FIXME: 6ch is not strictly correct, but works in many case. */
            {         6,     48000,        MP4SYS_AUDIO_PLI_AAC_L4 }, /* FIXME: 6ch is not strictly correct, but works in many case. */
            {         2,     48000,        MP4SYS_AUDIO_PLI_AAC_L2 },
            {         2,     24000,        MP4SYS_AUDIO_PLI_AAC_L1 },
            {         0,         0, MP4SYS_AUDIO_PLI_NOT_SPECIFIED }
        };
        for( int i = 0; summary->channels <= mp4sys_aac_pli_table[i][0] && summary->frequency <= mp4sys_aac_pli_table[i][1] ; i++ )
            pli = mp4sys_aac_pli_table[i][2];
        break;
    case MP4A_AUDIO_OBJECT_TYPE_Layer_1:
    case MP4A_AUDIO_OBJECT_TYPE_Layer_2:
    case MP4A_AUDIO_OBJECT_TYPE_Layer_3:
        pli = MP4SYS_AUDIO_PLI_NOT_SPECIFIED; /* 14496-3, 1.5.2 Audio profiles and levels, does not allow any pli. */
        break;
    default:
        pli = MP4SYS_AUDIO_PLI_NOT_SPECIFIED; /* something we don't know/support, or what the spec never covers. */
        break;
    }
    return pli;
}

static int mp4sys_is_same_profile( mp4sys_audioProfileLevelIndication a, mp4sys_audioProfileLevelIndication b )
{
    switch( a )
    {
    case MP4SYS_AUDIO_PLI_Main_L1:
    case MP4SYS_AUDIO_PLI_Main_L2:
    case MP4SYS_AUDIO_PLI_Main_L3:
    case MP4SYS_AUDIO_PLI_Main_L4:
        if( MP4SYS_AUDIO_PLI_Main_L1 <= b && b <= MP4SYS_AUDIO_PLI_Main_L4 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_Scalable_L1:
    case MP4SYS_AUDIO_PLI_Scalable_L2:
    case MP4SYS_AUDIO_PLI_Scalable_L3:
    case MP4SYS_AUDIO_PLI_Scalable_L4:
        if( MP4SYS_AUDIO_PLI_Scalable_L1 <= b && b <= MP4SYS_AUDIO_PLI_Scalable_L4 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_Speech_L1:
    case MP4SYS_AUDIO_PLI_Speech_L2:
        if( MP4SYS_AUDIO_PLI_Speech_L1 <= b && b <= MP4SYS_AUDIO_PLI_Speech_L2 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_Synthetic_L1:
    case MP4SYS_AUDIO_PLI_Synthetic_L2:
    case MP4SYS_AUDIO_PLI_Synthetic_L3:
        if( MP4SYS_AUDIO_PLI_Synthetic_L1 <= b && b <= MP4SYS_AUDIO_PLI_Synthetic_L3 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_HighQuality_L1:
    case MP4SYS_AUDIO_PLI_HighQuality_L2:
    case MP4SYS_AUDIO_PLI_HighQuality_L3:
    case MP4SYS_AUDIO_PLI_HighQuality_L4:
    case MP4SYS_AUDIO_PLI_HighQuality_L5:
    case MP4SYS_AUDIO_PLI_HighQuality_L6:
    case MP4SYS_AUDIO_PLI_HighQuality_L7:
    case MP4SYS_AUDIO_PLI_HighQuality_L8:
        if( MP4SYS_AUDIO_PLI_HighQuality_L1 <= b && b <= MP4SYS_AUDIO_PLI_HighQuality_L8 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_LowDelay_L1:
    case MP4SYS_AUDIO_PLI_LowDelay_L2:
    case MP4SYS_AUDIO_PLI_LowDelay_L3:
    case MP4SYS_AUDIO_PLI_LowDelay_L4:
    case MP4SYS_AUDIO_PLI_LowDelay_L5:
    case MP4SYS_AUDIO_PLI_LowDelay_L6:
    case MP4SYS_AUDIO_PLI_LowDelay_L7:
    case MP4SYS_AUDIO_PLI_LowDelay_L8:
        if( MP4SYS_AUDIO_PLI_LowDelay_L1 <= b && b <= MP4SYS_AUDIO_PLI_LowDelay_L8 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_Natural_L1:
    case MP4SYS_AUDIO_PLI_Natural_L2:
    case MP4SYS_AUDIO_PLI_Natural_L3:
    case MP4SYS_AUDIO_PLI_Natural_L4:
        if( MP4SYS_AUDIO_PLI_Natural_L1 <= b && b <= MP4SYS_AUDIO_PLI_Natural_L4 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_MobileInternetworking_L1:
    case MP4SYS_AUDIO_PLI_MobileInternetworking_L2:
    case MP4SYS_AUDIO_PLI_MobileInternetworking_L3:
    case MP4SYS_AUDIO_PLI_MobileInternetworking_L4:
    case MP4SYS_AUDIO_PLI_MobileInternetworking_L5:
    case MP4SYS_AUDIO_PLI_MobileInternetworking_L6:
        if( MP4SYS_AUDIO_PLI_MobileInternetworking_L1 <= b && b <= MP4SYS_AUDIO_PLI_MobileInternetworking_L6 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_AAC_L1:
    case MP4SYS_AUDIO_PLI_AAC_L2:
    case MP4SYS_AUDIO_PLI_AAC_L4:
    case MP4SYS_AUDIO_PLI_AAC_L5:
        if( MP4SYS_AUDIO_PLI_AAC_L1 <= b && b <= MP4SYS_AUDIO_PLI_AAC_L5 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_HE_AAC_L2:
    case MP4SYS_AUDIO_PLI_HE_AAC_L3:
    case MP4SYS_AUDIO_PLI_HE_AAC_L4:
    case MP4SYS_AUDIO_PLI_HE_AAC_L5:
        if( MP4SYS_AUDIO_PLI_HE_AAC_L2 <= b && b <= MP4SYS_AUDIO_PLI_HE_AAC_L5 )
            return 1;
        return 0;
        break;
    default:
        break;
    }
    return 0;
}

/* NOTE: This function is not strictly preferable, but accurate.
   The spec of audioProfileLevelIndication is too much complicated. */
mp4sys_audioProfileLevelIndication mp4sys_max_audioProfileLevelIndication( mp4sys_audioProfileLevelIndication a, mp4sys_audioProfileLevelIndication b )
{
    /* NONE_REQUIRED is minimal priotity, and NOT_SPECIFIED is max priority. */
    if( a == MP4SYS_AUDIO_PLI_NOT_SPECIFIED || b == MP4SYS_AUDIO_PLI_NONE_REQUIRED )
        return a;
    if( a == MP4SYS_AUDIO_PLI_NONE_REQUIRED || b == MP4SYS_AUDIO_PLI_NOT_SPECIFIED )
        return b;
    mp4sys_audioProfileLevelIndication c, d;
    if( a < b )
    {
        c = a;
        d = b;
    }
    else
    {
        c = b;
        d = a;
    }
    /* AAC-LC and SBR specific; If mixtured there, use correspond HE_AAC profile. */
    if( MP4SYS_AUDIO_PLI_AAC_L1 <= c && c <= MP4SYS_AUDIO_PLI_AAC_L5
        && MP4SYS_AUDIO_PLI_HE_AAC_L2 <= d && d <= MP4SYS_AUDIO_PLI_HE_AAC_L5 )
    {
        if( c <= MP4SYS_AUDIO_PLI_AAC_L2 )
            return d;
        c += 4; /* upgrade to HE-AAC */
        return c > d ? c : d;
    }
    /* General */
    if( mp4sys_is_same_profile( c, d ) )
        return d;
    return MP4SYS_AUDIO_PLI_NOT_SPECIFIED;
}

/***************************************************************************
    importer framework
***************************************************************************/
struct mp4sys_importer_tag;

typedef void ( *mp4sys_importer_cleanup )( struct mp4sys_importer_tag* importer );
typedef int ( *mp4sys_importer_get_accessunit )( struct mp4sys_importer_tag*, uint32_t track_number , void* buf, uint32_t* size );
typedef int ( *mp4sys_importer_probe )( struct mp4sys_importer_tag* importer );

typedef struct
{
    mp4sys_importer_probe          probe;
    mp4sys_importer_get_accessunit get_accessunit;
    mp4sys_importer_cleanup        cleanup;
} mp4sys_importer_functions;

typedef struct mp4sys_importer_tag
{
    FILE* stream;
    void* info; /* importer internal status information. */
    mp4sys_importer_functions funcs;
    isom_entry_list_t* summaries;
} mp4sys_importer_t;

/***************************************************************************
    ADTS importer
***************************************************************************/
#define MP4SYS_ADTS_FIXED_HEADER_LENGTH 4 /* this is partly a lie. actually 28 bits. */
#define MP4SYS_ADTS_BASIC_HEADER_LENGTH 7
#define MP4SYS_ADTS_MAX_FRAME_LENGTH ( ( 1 << 13 ) - 1 )
#define MP4SYS_ADTS_MAX_RAW_DATA_BLOCKS 4

typedef struct
{
    unsigned syncword                           : 12;
    unsigned ID                                 :  1;
    unsigned layer                              :  2;
    unsigned protection_absent                  :  1;
    unsigned profile_ObjectType                 :  2;
    unsigned sampling_frequency_index           :  4;
//  unsigned private_bit                        :  1; /* we don't care currently. */
    unsigned channel_configuration              :  3;
//  unsigned original_copy                      :  1; /* we don't care currently. */
//  unsigned home                               :  1; /* we don't care currently. */

} mp4sys_adts_fixed_header_t;

typedef struct
{
//  unsigned copyright_identification_bit       :  1; /* we don't care. */
//  unsigned copyright_identification_start     :  1; /* we don't care. */
    unsigned frame_length                       : 13;
//  unsigned adts_buffer_fullness               : 11; /* we don't care. */
    unsigned number_of_raw_data_blocks_in_frame :  2;
//  uint16_t adts_error_check;                        /* we don't support */
//  uint16_t raw_data_block_position[MP4SYS_ADTS_MAX_RAW_DATA_BLOCKS-1]; /* we don't use this directly, and... */
    uint16_t raw_data_block_size[MP4SYS_ADTS_MAX_RAW_DATA_BLOCKS];       /* use this instead of above. */
//  uint16_t adts_header_error_check;                 /* we don't support, actually crc_check within this */
//  uint16_t adts_raw_data_block_error_check[MP4SYS_ADTS_MAX_RAW_DATA_BLOCKS]; /* we don't support */
} mp4sys_adts_variable_header_t;

static void mp4sys_adts_parse_fixed_header( uint8_t* buf, mp4sys_adts_fixed_header_t* header )
{
    /* FIXME: should we rewrite these code using bitstream reader? */
    header->syncword                 = (buf[0] << 4) | (buf[1] >> 4);
    header->ID                       = (buf[1] >> 3) & 0x1;
    header->layer                    = (buf[1] >> 1) & 0x3;
    header->protection_absent        = buf[1] & 0x1;
    header->profile_ObjectType       = buf[2] >> 6;
    header->sampling_frequency_index = (buf[2] >> 2) & 0xF;
//  header->private_bit              = (buf[2] >> 1) & 0x1; /* we don't care currently. */
    header->channel_configuration    = ((buf[2] << 2) | (buf[3] >> 6)) & 0x07;
//  header->original_copy            = (buf[3] >> 5) & 0x1; /* we don't care currently. */
//  header->home                     = (buf[3] >> 4) & 0x1; /* we don't care currently. */
}

static int mp4sys_adts_check_fixed_header( mp4sys_adts_fixed_header_t* header )
{
    if( header->syncword != 0xFFF )              return -1;
//  if( header->ID != 0x0 )                      return -1; /* we don't care. */
    if( header->layer != 0x0 )                   return -1; /* must be 0b00 for any type of AAC */
//  if( header->protection_absent != 0x1 )       return -1; /* we don't care. */
    if( header->profile_ObjectType != 0x1 )      return -1; /* FIXME: 0b00=Main, 0b01=LC, 0b10=SSR, 0b11=LTP. */
    if( header->sampling_frequency_index > 0xB ) return -1; /* must not be > 0xB. */
    if( header->channel_configuration == 0x0 )   return -1; /* FIXME: we do not support 0b000 currently. */
    if( header->profile_ObjectType == 0x3 && header->ID != 0x0 ) return -1; /* LTP is valid only if ID==0. */
    return 0;
}

static int mp4sys_adts_parse_variable_header( FILE* stream, uint8_t* buf, unsigned int protection_absent, mp4sys_adts_variable_header_t* header )
{
    /* FIXME: should we rewrite these code using bitstream reader? */
//  header->copyright_identification_bit       = (buf[3] >> 3) & 0x1; /* we don't care. */
//  header->copyright_identification_start     = (buf[3] >> 2) & 0x1; /* we don't care. */
    header->frame_length                       = ((buf[3] << 11) | (buf[4] << 3) | (buf[5] >> 5)) & 0x1FFF ;
//  header->adts_buffer_fullness               = ((buf[5] << 6) | (buf[6] >> 2)) 0x7FF ;  /* we don't care. */
    header->number_of_raw_data_blocks_in_frame = buf[6] & 0x3;

    if( header->frame_length <= MP4SYS_ADTS_BASIC_HEADER_LENGTH + 2 * (protection_absent == 0) )
        return -1; /* easy error check */

    /* protection_absent and number_of_raw_data_blocks_in_frame relatives */

    uint8_t buf2[2];
    unsigned int number_of_blocks = header->number_of_raw_data_blocks_in_frame;
    if( number_of_blocks == 0 )
    {
        header->raw_data_block_size[0] = header->frame_length - MP4SYS_ADTS_BASIC_HEADER_LENGTH;
        /* skip adts_error_check() and subtract that from block_size */
        if( protection_absent == 0 )
        {
            header->raw_data_block_size[0] -= 2;
            if( fread( buf2, 1, 2, stream ) != 2 )
                return -1;
        }
        return 0;
    }

    /* now we have multiple raw_data_block()s, so evaluate adts_header_error_check() */

    uint16_t raw_data_block_position[MP4SYS_ADTS_MAX_RAW_DATA_BLOCKS];
    uint16_t first_offset = MP4SYS_ADTS_BASIC_HEADER_LENGTH;
    if( protection_absent == 0 )
    {
        /* process adts_header_error_check() */
        for( int i = 0 ; i < number_of_blocks ; i++ ) /* 1-based in the spec, but we use 0-based */
        {
            if( fread( buf2, 1, 2, stream ) != 2 )
                return -1;
            raw_data_block_position[i] = (buf2[0] << 8) | buf2[1];
        }
        /* skip crc_check in adts_header_error_check().
           Or might be sizeof( adts_error_check() ) if we share with the case number_of_raw_data_blocks_in_frame == 0 */
        if( fread( buf2, 1, 2, stream ) != 2 )
            return -1;
        first_offset += ( 2 * number_of_blocks ) + 2; /* according to above */
    }
    else
    {
        /*
         * NOTE: We never support the case where number_of_raw_data_blocks_in_frame != 0 && protection_absent != 0,
         * because we have to parse the raw AAC bitstream itself to find boundaries of raw_data_block()s in this case.
         * Which is to say, that braindamaged spec requires us (mp4 muxer) to decode AAC once to split frames.
         * L-SMASH is NOT AAC DECODER, so that we've just given up for this case.
         * This is ISO/IEC 13818-7's sin which defines ADTS format originally.
         */
        return -1;
    }

    /* convert raw_data_block_position --> raw_data_block_size */

    /* do conversion for first */
    header->raw_data_block_size[0] = raw_data_block_position[0] - first_offset;
    /* set dummy offset to tail for loop, do coversion for rest. */
    raw_data_block_position[number_of_blocks] = header->frame_length;
    for( int i = 1 ; i <= number_of_blocks ; i++ )
        header->raw_data_block_size[i] = raw_data_block_position[i] - raw_data_block_position[i-1];

    /* adjustment for adts_raw_data_block_error_check() */
    if( protection_absent == 0 && number_of_blocks != 0 )
        for( int i = 0 ; i <= number_of_blocks ; i++ )
            header->raw_data_block_size[i] -= 2;

    return 0;
}

static int mp4sys_adts_parse_headers( FILE* stream, uint8_t* buf, mp4sys_adts_fixed_header_t* header, mp4sys_adts_variable_header_t* variable_header )
{
    mp4sys_adts_parse_fixed_header( buf, header );
    if( mp4sys_adts_check_fixed_header( header ) )
        return -1;
    /* get payload length & skip extra(crc) header */
    return mp4sys_adts_parse_variable_header( stream, buf, header->protection_absent, variable_header );
}

static mp4sys_audio_summary_t* mp4sys_adts_create_summary( mp4sys_adts_fixed_header_t* header )
{
    mp4sys_audio_summary_t* summary = (mp4sys_audio_summary_t*)malloc( sizeof(mp4sys_audio_summary_t) );
    if( !summary )
        return NULL;
    memset( summary, 0, sizeof(mp4sys_audio_summary_t) );
    summary->object_type_indication = MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3;
    summary->stream_type            = MP4SYS_STREAM_TYPE_AudioStream;
    summary->max_au_length          = MP4SYS_ADTS_MAX_FRAME_LENGTH;
    summary->frequency              = mp4a_AAC_frequency_table[header->sampling_frequency_index][1];
    summary->channels               = header->channel_configuration + ( header->channel_configuration == 0x07 ); /* 0x07 means 7.1ch */
    summary->bit_depth              = 16;
    summary->samples_in_frame       = 1024;
    summary->aot                    = header->profile_ObjectType + MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN;
    summary->sbr_mode               = MP4A_AAC_SBR_NOT_SPECIFIED;
#if 0 /* FIXME: This is very unstable. So many players crash with this. */
    if( header->ID != 0 )
    {
        /*
         * NOTE: This ADTS seems of ISO/IEC 13818-7 (MPEG-2 AAC).
         * It has special object_type_indications, depending on it's profile.
         * It shall not have decoder specific information, so AudioObjectType neither.
         * see ISO/IEC 14496-1, 8.6.7 DecoderSpecificInfo.
         */
        summary->object_type_indication = header->profile_ObjectType + MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_Main_Profile;
        summary->aot                    = MP4A_AUDIO_OBJECT_TYPE_NULL;
        summary->asc                    = NULL;
        summary->asc_length             = 0;
        // summary->sbr_mode            = MP4A_AAC_SBR_NONE; /* MPEG-2 AAC should not be HE-AAC, but we forgive them. */
        return summary;
    }
#endif
    if( mp4sys_setup_AudioSpecificConfig( summary ) )
    {
        mp4sys_cleanup_audio_summary( summary );
        return NULL;
    }
    return summary;
}

typedef enum
{
    MP4SYS_ADTS_ERROR  = -1,
    MP4SYS_ADTS_OK     = 0,
    MP4SYS_ADTS_CHANGE = 1,
    MP4SYS_ADTS_EOF    = 2,
} mp4sys_adts_status;

typedef struct
{
    mp4sys_adts_status status;
    unsigned int raw_data_block_idx;
    mp4sys_adts_fixed_header_t header;
    mp4sys_adts_variable_header_t variable_header;
} mp4sys_adts_info_t;

static int mp4sys_adts_get_accessunit( mp4sys_importer_t* importer, uint32_t track_number , void* userbuf, uint32_t *size )
{
    debug_if( !importer || !importer->info || !userbuf || !size )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    mp4sys_adts_info_t* info = (mp4sys_adts_info_t*)importer->info;
    mp4sys_adts_status current_status = info->status;
    uint16_t raw_data_block_size = info->variable_header.raw_data_block_size[info->raw_data_block_idx];
    if( current_status == MP4SYS_ADTS_ERROR || *size < raw_data_block_size )
        return -1;
    if( current_status == MP4SYS_ADTS_EOF )
    {
        *size = 0;
        return 0;
    }
    if( current_status == MP4SYS_ADTS_CHANGE )
    {
        mp4sys_audio_summary_t* summary = mp4sys_adts_create_summary( &info->header );
        if( !summary )
            return -1;
        isom_entry_t* entry = isom_get_entry( importer->summaries, track_number );
        if( !entry || !entry->data )
            return -1;
        mp4sys_cleanup_audio_summary( entry->data );
        entry->data = summary;
    }

    /* read a raw_data_block(), typically == payload of a ADTS frame */
    if( fread( userbuf, 1, raw_data_block_size, importer->stream ) != raw_data_block_size )
    {
        info->status = MP4SYS_ADTS_ERROR;
        return -1;
    }
    *size = raw_data_block_size;

    /* now we succeeded to read current frame, so "return" takes 0 always below. */

    /* skip adts_raw_data_block_error_check() */
    if( info->header.protection_absent == 0
        && info->variable_header.number_of_raw_data_blocks_in_frame != 0
        && fread( userbuf, 1, 2, importer->stream ) != 2 )
    {
        info->status = MP4SYS_ADTS_ERROR;
        return 0;
    }
    /* current adts_frame() has any more raw_data_block()? */
    if( info->raw_data_block_idx < info->variable_header.number_of_raw_data_blocks_in_frame )
    {
        info->raw_data_block_idx++;
        info->status = MP4SYS_ADTS_OK;
        return 0;
    }
    info->raw_data_block_idx = 0;

    /* preparation for next frame */

    uint8_t buf[MP4SYS_ADTS_MAX_FRAME_LENGTH];
    size_t ret = fread( buf, 1, MP4SYS_ADTS_BASIC_HEADER_LENGTH, importer->stream );
    if( ret == 0 )
    {
        info->status = MP4SYS_ADTS_EOF;
        return 0;
    }
    if( ret != MP4SYS_ADTS_BASIC_HEADER_LENGTH )
    {
        info->status = MP4SYS_ADTS_ERROR;
        return 0;
    }
    /*
     * NOTE: About the spec of ADTS headers.
     * By the spec definition, ADTS's fixed header cannot change in the middle of stream.
     * But spec of MP4 allows that a stream(track) changes its properties in the middle of it.
     */
    /*
     * NOTE: About detailed check for ADTS headers.
     * We do not ommit detailed check for fixed header by simply testing bits' identification,
     * because there're some flags which does not matter to audio_summary (so AudioSpecificConfig neither)
     * so that we can take them as no change and never make new ObjectDescriptor.
     * I know that can be done with/by bitmask also and that should be fast, but L-SMASH project prefers
     * even foolishly straightforward way.
     */
    /*
     * NOTE: About our reading algorithm for ADTS.
     * It's rather simple if we retrieve payload of ADTS (i.e. raw AAC frame) at the same time to
     * retrieve headers.
     * But then we have to cache and memcpy every frame so that it requires more clocks and memory.
     * To avoid them, I adopted this separate retrieving method.
     */
    mp4sys_adts_fixed_header_t header = {0};
    mp4sys_adts_variable_header_t variable_header = {0};
    if( mp4sys_adts_parse_headers( importer->stream, buf, &header, &variable_header ) )
    {
        info->status = MP4SYS_ADTS_ERROR;
        return 0;
    }
    info->variable_header = variable_header;

    /*
     * NOTE: About our support for change(s) of properties within an ADTS stream.
     * We have to modify these conditions depending on the features we support.
     * For example, if we support copyright_identification_* in any way within any feature
     * defined by/in any specs, such as ISO/IEC 14496-1 (MPEG-4 Systems), like...
     * "8.3 Intellectual Property Management and Protection (IPMP)", or something similar,
     * we have to check copyright_identification_* and treat them in audio_summary.
     * "Change(s)" may result in MP4SYS_ADTS_ERROR or MP4SYS_ADTS_CHANGE
     * depending on the features we support, and what the spec allows.
     * Sometimes the "change(s)" can be allowed, while sometimes they're forbidden.
     */
    /* currently UNsupported "change(s)". */
    if( info->header.profile_ObjectType != header.profile_ObjectType /* currently unsupported. */
        || info->header.ID != header.ID /* In strict, this means change of object_type_indication. */
        || info->header.sampling_frequency_index != header.sampling_frequency_index ) /* This may change timebase. */
    {
        info->status = MP4SYS_ADTS_ERROR;
        return 0;
    }
    /* currently supported "change(s)". */
    if( info->header.channel_configuration != header.channel_configuration )
    {
        /*
         * FIXME: About conditions of VALID "change(s)".
         * we have to check whether any "change(s)" affect to audioProfileLevelIndication
         * in InitialObjectDescriptor (MP4_IOD) or not.
         * If another type or upper level is required by the change(s), that is forbidden.
         * Because ObjectDescriptor does not have audioProfileLevelIndication,
         * so that it seems impossible to change audioProfileLevelIndication in the middle of the stream.
         * Note also any other properties, such as AudioObjectType, object_type_indication.
         */
        /*
         * NOTE: updating summary must be done on next call,
         * because user may retrieve summary right after this function call of this time,
         * and that should be of current, before change, one.
         */
        info->header = header;
        info->status = MP4SYS_ADTS_CHANGE;
        return 0;
    }
    /* no change which matters to mp4 muxing was found */
    info->status = MP4SYS_ADTS_OK;
    return 0;
}

static void mp4sys_adts_cleanup( mp4sys_importer_t* importer )
{
    debug_if( importer && importer->info )
        free( importer->info );
}

static int mp4sys_adts_probe( mp4sys_importer_t* importer )
{
    uint8_t buf[MP4SYS_ADTS_MAX_FRAME_LENGTH];
    if( fread( buf, 1, MP4SYS_ADTS_BASIC_HEADER_LENGTH, importer->stream ) != MP4SYS_ADTS_BASIC_HEADER_LENGTH )
        return -1;

    mp4sys_adts_fixed_header_t header = {0};
    mp4sys_adts_variable_header_t variable_header = {0};
    if( mp4sys_adts_parse_headers( importer->stream, buf, &header, &variable_header ) )
        return -1;

    /* now the stream seems valid ADTS */

    mp4sys_audio_summary_t* summary = mp4sys_adts_create_summary( &header );
    if( !summary )
        return -1;

    /* importer status */
    mp4sys_adts_info_t* info = malloc( sizeof(mp4sys_adts_info_t) );
    if( !info )
    {
        mp4sys_cleanup_audio_summary( summary );
        return -1;
    }
    memset( info, 0, sizeof(mp4sys_adts_info_t) );
    info->status = MP4SYS_ADTS_OK;
    info->raw_data_block_idx = 0;
    info->header = header;
    info->variable_header = variable_header;

    if( isom_add_entry( importer->summaries, summary ) )
    if( !info )
    {
        free( info );
        mp4sys_cleanup_audio_summary( summary );
        return -1;
    }
    importer->info = info;

    return 0;
}

const static mp4sys_importer_functions mp4sys_adts_importer = {
    mp4sys_adts_probe,
    mp4sys_adts_get_accessunit,
    mp4sys_adts_cleanup
};

/***************************************************************************
    importer public interfaces
***************************************************************************/

/******** importer listing table ********/
const static mp4sys_importer_functions* mp4sys_importer_tbl[] = {
    &mp4sys_adts_importer,
    NULL,
};

/******** importer public functions ********/
void mp4sys_importer_close( mp4sys_importer_t* importer )
{
    if( !importer )
        return;
    if( importer->stream )
        fclose( importer->stream );
    if( importer->funcs.cleanup )
        importer->funcs.cleanup( importer );
    /* FIXME: To be extended to support visual summary. */
    isom_remove_list( importer->summaries, mp4sys_cleanup_audio_summary );
    free( importer );
}

mp4sys_importer_t* mp4sys_importer_open( char* identifier )
{
    mp4sys_importer_t* importer = (mp4sys_importer_t*)malloc( sizeof(mp4sys_importer_t) );
    if( !importer )
        return NULL;
    memset( importer, 0, sizeof(mp4sys_importer_t) );
    if( (importer->stream = fopen( identifier, "rb" )) == NULL )
    {
        free( importer );
        return NULL;
    }
    importer->summaries = isom_create_entry_list();
    if( !importer->summaries )
    {
        mp4sys_importer_close( importer );
        return NULL;
    }
    const mp4sys_importer_functions* funcs;
    for( int i = 0; (funcs = mp4sys_importer_tbl[i]) && funcs->probe && funcs->probe( importer ); i++ )
        mp4sys_fseek( importer->stream, 0, SEEK_SET );
    if( !funcs )
    {
        mp4sys_importer_close( importer );
        return NULL;
    }
    importer->funcs = *funcs;
    return importer;
}

/* 0 if success, positive if changed, negative if failed */
int mp4sys_importer_get_access_unit( mp4sys_importer_t* importer, uint32_t track_number, void* buf, uint32_t* size )
{
    if( !importer || !importer->funcs.get_accessunit || !buf || !size || *size == 0 )
        return -1;
    return importer->funcs.get_accessunit( importer, track_number, buf, size );
}

mp4sys_audio_summary_t* mp4sys_duplicate_audio_summary( mp4sys_importer_t* importer, uint32_t track_number )
{
    if( !importer )
        return NULL;
    mp4sys_audio_summary_t* summary = (mp4sys_audio_summary_t*)malloc( sizeof(mp4sys_audio_summary_t) );
    if( !summary )
        return NULL;
    mp4sys_audio_summary_t* src_summary = isom_get_entry_data( importer->summaries, track_number );
    if( !src_summary )
        return NULL;
    memcpy( summary, src_summary, sizeof(mp4sys_audio_summary_t) );
    summary->exdata = NULL;
    summary->exdata_length = 0;
    if( mp4sys_summary_add_AudioSpecificConfig( summary, src_summary->exdata, src_summary->exdata_length ) )
    {
        free( summary );
        return NULL;
    }
    return summary;
}
