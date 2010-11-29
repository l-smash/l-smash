/*****************************************************************************
 * mp4a.c:
 *****************************************************************************
 * Copyright (C) 2010 L-SMASH project
 *
 * Authors: Takashi Hirata <silverfilain@gmail.com>
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

#define MP4A_INTERNAL
#include "mp4a.h"

#include <stdlib.h>
#include <string.h>

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
static void mp4a_put_GASpecificConfig( lsmash_bits_t* bits, mp4a_GASpecificConfig_t* gasc )
{
    debug_if( !bits || !gasc )
        return;
    lsmash_bits_put( bits, gasc->frameLengthFlag, 1);
    lsmash_bits_put( bits, gasc->dependsOnCoreCoder, 1);
    lsmash_bits_put( bits, gasc->extensionFlag, 1);
}

static void mp4a_put_MPEG_1_2_SpecificConfig( lsmash_bits_t* bits, mp4a_MPEG_1_2_SpecificConfig_t* mpeg_1_2_sc )
{
    debug_if( !bits || !mpeg_1_2_sc )
        return;
    lsmash_bits_put( bits, mpeg_1_2_sc->extension, 1); /* shall be 0 */
}

static inline void mp4a_put_AudioObjectType( lsmash_bits_t* bits, mp4a_AudioObjectType aot )
{
    if( aot > MP4A_AUDIO_OBJECT_TYPE_ESCAPE )
    {
        lsmash_bits_put( bits, MP4A_AUDIO_OBJECT_TYPE_ESCAPE, 5);
        lsmash_bits_put( bits, aot - MP4A_AUDIO_OBJECT_TYPE_ESCAPE - 1, 6);
    }
    else
        lsmash_bits_put( bits, aot, 5);
}

static inline void mp4a_put_SamplingFrequencyIndex( lsmash_bits_t* bits, uint8_t samplingFrequencyIndex, uint32_t samplingFrequency )
{
    lsmash_bits_put( bits, samplingFrequencyIndex, 4);
    if( samplingFrequencyIndex == 0xF )
        lsmash_bits_put( bits, samplingFrequency, 24);
}

/* Currently, only normal AAC, MPEG_1_2 are supported.
   For AAC, other than normal AAC, such as AAC_scalable, ER_AAC_xxx, are not supported.
   ADIF/PCE(program config element) style AudioSpecificConfig is not supported either. */
void mp4a_put_AudioSpecificConfig( lsmash_bs_t* bs, mp4a_AudioSpecificConfig_t* asc )
{
    debug_if( !bs || !asc )
        return;
    lsmash_bits_t bits;
    lsmash_bits_init( &bits, bs );

    if( asc->sbr_mode == MP4A_AAC_SBR_HIERARCHICAL )
        mp4a_put_AudioObjectType( &bits, asc->extensionAudioObjectType ); /* puts MP4A_AUDIO_OBJECT_TYPE_SBR */
    else
        mp4a_put_AudioObjectType( &bits, asc->audioObjectType );
    mp4a_put_SamplingFrequencyIndex( &bits, asc->samplingFrequencyIndex, asc->samplingFrequency );
    lsmash_bits_put( &bits, asc->channelConfiguration, 4 );
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
        lsmash_bits_put( &bits, 0x2b7, 11 );
        mp4a_put_AudioObjectType( &bits, asc->extensionAudioObjectType ); /* puts MP4A_AUDIO_OBJECT_TYPE_SBR */
        if( asc->extensionAudioObjectType == MP4A_AUDIO_OBJECT_TYPE_SBR ) /* this is always true, due to current spec */
        {
            /* sbrPresentFlag */
            if( asc->sbr_mode == MP4A_AAC_SBR_NONE )
                lsmash_bits_put( &bits, 0x0, 1 );
            else
            {
                lsmash_bits_put( &bits, 0x1, 1 );
                mp4a_put_SamplingFrequencyIndex( &bits, asc->extensionSamplingFrequencyIndex, asc->extensionSamplingFrequency );
            }
        }
    }
    lsmash_bits_put_align( &bits );
}
