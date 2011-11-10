/*****************************************************************************
 * mp4a.c:
 *****************************************************************************
 * Copyright (C) 2010-2011 L-SMASH project
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

#include "internal.h" /* must be placed first */

#define MP4A_INTERNAL
#include "mp4a.h"

#include <stdlib.h>
#include <string.h>

/***************************************************************************
    implementation of part of ISO/IEC 14496-3 (ISO/IEC 14496-1 relevant)
***************************************************************************/

/* ISO/IEC 14496-3 samplingFrequencyIndex */
/* ISO/IEC 14496-3 Sampling frequency mapping */
const uint32_t mp4a_sampling_frequency_table[13][5] = {
    /* threshold, exact, idx_for_ga, idx_for_sbr, idx */
    {      92017, 96000,        0x0,         0xF, 0x0 }, /* SBR is not allowed */
    {      75132, 88200,        0x1,         0xF, 0x1 }, /* SBR is not allowed */
    {      55426, 64000,        0x2,         0xF, 0x2 }, /* SBR is not allowed */
    {      46009, 48000,        0x3,         0x0, 0x3 },
    {      37566, 44100,        0x4,         0x1, 0x4 },
    {      27713, 32000,        0x5,         0x2, 0x5 },
    {      23004, 24000,        0x6,         0x3, 0x6 },
    {      18783, 22050,        0x7,         0x4, 0x7 },
    {      13856, 16000,        0x8,         0x5, 0x8 },
    {      11502, 12000,        0x9,         0x6, 0x9 },
    {       9391, 11025,        0xA,         0x7, 0xA },
    {       8000,  8000,        0xB,         0x8, 0xB },
    {          0,  7350,        0xB,         0xF, 0xC } /* samplingFrequencyIndex for GASpecificConfig is 0xB (same as 8000Hz). */
};

/* ISO/IEC 14496-3 Interface to ISO/IEC 14496-1 (MPEG-4 Systems), Syntax of AudioSpecificConfig(). */
/* This structure is represent of regularized AudioSpecificConfig. */
/* for actual definition, see Syntax of GetAudioObjectType() for audioObjectType and extensionAudioObjectType. */
typedef struct {
    lsmash_mp4a_aac_sbr_mode sbr_mode; /* L-SMASH's original, including sbrPresent flag. */
    lsmash_mp4a_AudioObjectType audioObjectType;
    uint8_t samplingFrequencyIndex;
    uint32_t samplingFrequency;
    uint8_t channelConfiguration;
    lsmash_mp4a_AudioObjectType extensionAudioObjectType;
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

/* ISO/IEC 14496-3 Decoder configuration (GASpecificConfig), Syntax of GASpecificConfig() */
/* ISO/IEC 14496-3 GASpecificConfig(), Sampling frequency mapping */
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

/* ISO/IEC 14496-3 MPEG_1_2_SpecificConfig */
typedef struct {
    uint8_t extension; /* shall be 0. */
} mp4a_MPEG_1_2_SpecificConfig_t;

/* ISO/IEC 14496-3 ALSSpecificConfig */
typedef struct
{
    uint32_t size;
    uint8_t *data;
    uint32_t samp_freq;
    uint16_t channels;
    uint8_t  resolution;
    uint8_t  floating;
    uint16_t frame_length;
    uint16_t max_order;
    uint8_t  block_switching;
    uint8_t  bgmc_mode;
    uint8_t  RLSLMS;
} mp4a_ALSSpecificConfig_t;

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
static mp4a_GASpecificConfig_t* mp4a_create_GASpecificConfig( uint8_t samplingFrequencyIndex, uint8_t channelConfig, lsmash_mp4a_AudioObjectType aot )
{
    debug_if( aot != MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN && aot != MP4A_AUDIO_OBJECT_TYPE_AAC_LC
        && aot != MP4A_AUDIO_OBJECT_TYPE_AAC_SSR && aot != MP4A_AUDIO_OBJECT_TYPE_AAC_LTP
        && aot != MP4A_AUDIO_OBJECT_TYPE_TwinVQ )
        return NULL;
    if( samplingFrequencyIndex > 0xB || channelConfig == 0 || channelConfig == 7 )
        return NULL;
    mp4a_GASpecificConfig_t *gasc = (mp4a_GASpecificConfig_t *)lsmash_malloc_zero( sizeof(mp4a_GASpecificConfig_t) );
    if( !gasc )
        return NULL;
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
    mp4a_MPEG_1_2_SpecificConfig_t *mpeg_1_2_sc = (mp4a_MPEG_1_2_SpecificConfig_t *)lsmash_malloc_zero( sizeof(mp4a_MPEG_1_2_SpecificConfig_t) );
    if( !mpeg_1_2_sc )
        return NULL;
    mpeg_1_2_sc->extension = 0; /* shall be 0. */
    return mpeg_1_2_sc;
}

static mp4a_ALSSpecificConfig_t *mp4a_create_ALSSpecificConfig( uint8_t *exdata, uint32_t exdata_length )
{
    mp4a_ALSSpecificConfig_t *alssc = (mp4a_ALSSpecificConfig_t *)lsmash_malloc_zero( sizeof(mp4a_ALSSpecificConfig_t) );
    if( !alssc )
        return NULL;
    alssc->data = lsmash_memdup( exdata, exdata_length );
    if( !alssc->data )
        return NULL;
    alssc->size = exdata_length;
    return alssc;
}

/* Currently, only normal AAC, MPEG_1_2 are supported.
   For AAC, other than normal AAC, such as AAC_scalable, ER_AAC_xxx, are not supported.
   ADIF/PCE(program config element) style AudioSpecificConfig is not supported.
   aot shall not be MP4A_AUDIO_OBJECT_TYPE_SBR even if you wish to signal SBR explicitly, use sbr_mode instead.
   Frequency/channels shall be base AAC's one, even if SBR/PS.
   If other than AAC with SBR, sbr_mode shall be MP4A_AAC_SBR_NOT_SPECIFIED. */
mp4a_AudioSpecificConfig_t *mp4a_create_AudioSpecificConfig(
    lsmash_mp4a_AudioObjectType aot,
    uint32_t frequency,
    uint32_t channels,
    lsmash_mp4a_aac_sbr_mode sbr_mode,
    uint8_t *exdata,
    uint32_t exdata_length
)
{
    if( aot != MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN && aot != MP4A_AUDIO_OBJECT_TYPE_AAC_LC
        && aot != MP4A_AUDIO_OBJECT_TYPE_AAC_SSR && aot != MP4A_AUDIO_OBJECT_TYPE_AAC_LTP
        && aot != MP4A_AUDIO_OBJECT_TYPE_TwinVQ && aot != MP4A_AUDIO_OBJECT_TYPE_ALS )
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

    mp4a_AudioSpecificConfig_t *asc = (mp4a_AudioSpecificConfig_t *)lsmash_malloc_zero( sizeof(mp4a_AudioSpecificConfig_t) );
    if( !asc )
        return NULL;

    asc->sbr_mode = sbr_mode;
    asc->audioObjectType = aot;
    asc->channelConfiguration = channelConfig;

    uint8_t samplingFrequencyIndex = 0xF;
    uint8_t i = 0x0;
    if( sbr_mode != MP4A_AAC_SBR_NOT_SPECIFIED
     || aot == MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN
     || aot == MP4A_AUDIO_OBJECT_TYPE_AAC_LC
     || aot == MP4A_AUDIO_OBJECT_TYPE_AAC_SSR
     || aot == MP4A_AUDIO_OBJECT_TYPE_AAC_LTP
     || aot == MP4A_AUDIO_OBJECT_TYPE_SBR )
    {
        while( frequency < mp4a_sampling_frequency_table[i][0] )
            i++;
        asc->samplingFrequencyIndex = frequency == mp4a_sampling_frequency_table[i][1] ? i : 0xF;
        asc->samplingFrequency = frequency;
        samplingFrequencyIndex = mp4a_sampling_frequency_table[i][2];
        /* SBR settings */
        if( sbr_mode != MP4A_AAC_SBR_NOT_SPECIFIED )
        {
            /* SBR limitation */
            /* see ISO/IEC 14496-3 Levels within the profiles / Levels for the High Efficiency AAC Profile */
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
            asc->extensionSamplingFrequencyIndex = i == 0xC ? 0xF : mp4a_sampling_frequency_table[i][3];
        }
        else
        {
            asc->extensionSamplingFrequencyIndex = asc->samplingFrequencyIndex;
            asc->extensionSamplingFrequency = asc->samplingFrequency;
        }
    }
    else
    {
        while( i < 0xD && frequency != mp4a_sampling_frequency_table[i][1] )
            i++;
        asc->samplingFrequencyIndex          = i != 0xD ? i : 0xF;
        asc->samplingFrequency               = frequency;
        asc->extensionAudioObjectType        = MP4A_AUDIO_OBJECT_TYPE_NULL;
        asc->extensionSamplingFrequencyIndex = asc->samplingFrequencyIndex;
        asc->extensionSamplingFrequency      = asc->samplingFrequency;
    }

    switch( aot )
    {
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
        case MP4A_AUDIO_OBJECT_TYPE_ALS:
            asc->deepAudioSpecificConfig = mp4a_create_ALSSpecificConfig( exdata, exdata_length );
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
    lsmash_bits_put( bits, gasc->frameLengthFlag, 1 );
    lsmash_bits_put( bits, gasc->dependsOnCoreCoder, 1 );
    lsmash_bits_put( bits, gasc->extensionFlag, 1 );
}

static void mp4a_put_MPEG_1_2_SpecificConfig( lsmash_bits_t* bits, mp4a_MPEG_1_2_SpecificConfig_t* mpeg_1_2_sc )
{
    debug_if( !bits || !mpeg_1_2_sc )
        return;
    lsmash_bits_put( bits, mpeg_1_2_sc->extension, 1 ); /* shall be 0 */
}

static void mp4a_put_ALSSpecificConfig( lsmash_bits_t *bits, mp4a_ALSSpecificConfig_t *alssc )
{
    debug_if( !bits || !alssc )
        return;
    lsmash_bits_put( bits, 0, 5 );      /* fillBits for byte alignment */
    lsmash_bits_import_data( bits, alssc->data, alssc->size );
}

static inline void mp4a_put_AudioObjectType( lsmash_bits_t* bits, lsmash_mp4a_AudioObjectType aot )
{
    if( aot > MP4A_AUDIO_OBJECT_TYPE_ESCAPE )
    {
        lsmash_bits_put( bits, MP4A_AUDIO_OBJECT_TYPE_ESCAPE, 5 );
        lsmash_bits_put( bits, aot - MP4A_AUDIO_OBJECT_TYPE_ESCAPE - 1, 6 );
    }
    else
        lsmash_bits_put( bits, aot, 5 );
}

static inline void mp4a_put_SamplingFrequencyIndex( lsmash_bits_t* bits, uint8_t samplingFrequencyIndex, uint32_t samplingFrequency )
{
    lsmash_bits_put( bits, samplingFrequencyIndex, 4 );
    if( samplingFrequencyIndex == 0xF )
        lsmash_bits_put( bits, samplingFrequency, 24 );
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
    case MP4A_AUDIO_OBJECT_TYPE_ALS:
        mp4a_put_ALSSpecificConfig( &bits, (mp4a_ALSSpecificConfig_t *)asc->deepAudioSpecificConfig );
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

static int mp4a_get_GASpecificConfig( lsmash_bits_t *bits, mp4a_AudioSpecificConfig_t *asc )
{
    mp4a_GASpecificConfig_t *gasc = (mp4a_GASpecificConfig_t *)lsmash_malloc_zero( sizeof(mp4a_GASpecificConfig_t) );
    if( !gasc )
        return -1;
    asc->deepAudioSpecificConfig = gasc;
    gasc->frameLengthFlag = lsmash_bits_get( bits, 1 );
    gasc->dependsOnCoreCoder = lsmash_bits_get( bits, 1 );
    if( gasc->dependsOnCoreCoder )
        lsmash_bits_get( bits, 14 );    /* coreCoderDelay */
    gasc->extensionFlag = lsmash_bits_get( bits, 1 );
    return 0;
}

static int mp4a_get_MPEG_1_2_SpecificConfig( lsmash_bits_t *bits, mp4a_AudioSpecificConfig_t *asc )
{
    lsmash_bits_get( bits, 1 );
    return 0;
}

static int mp4a_get_ALSSpecificConfig( lsmash_bits_t *bits, mp4a_AudioSpecificConfig_t *asc )
{
    mp4a_ALSSpecificConfig_t *alssc = (mp4a_ALSSpecificConfig_t *)lsmash_malloc_zero( sizeof(mp4a_ALSSpecificConfig_t) );
    if( !alssc )
        return -1;
    asc->deepAudioSpecificConfig = alssc;
    lsmash_bits_get( bits, 32 );    /* als_id */
    alssc->samp_freq = lsmash_bits_get( bits, 32 );
    lsmash_bits_get( bits, 32 );    /* samples */
    alssc->channels = lsmash_bits_get( bits, 16 );
    lsmash_bits_get( bits, 3 );     /* file_type */
    alssc->resolution = lsmash_bits_get( bits, 3 );
    alssc->floating = lsmash_bits_get( bits, 1 );
    lsmash_bits_get( bits, 1 );     /* msb_first */
    alssc->frame_length = lsmash_bits_get( bits, 16 );
    lsmash_bits_get( bits, 8 );     /* random_access */
    lsmash_bits_get( bits, 2 );     /* ra_flag */
    lsmash_bits_get( bits, 1 );     /* adapt_order */
    lsmash_bits_get( bits, 2 );     /* coef_table */
    lsmash_bits_get( bits, 1 );     /* long_term_prediction */
    alssc->max_order = lsmash_bits_get( bits, 10 );
    alssc->block_switching = lsmash_bits_get( bits, 2 );
    alssc->bgmc_mode = lsmash_bits_get( bits, 1 );
    lsmash_bits_get( bits, 1 );     /* sb_part */
    lsmash_bits_get( bits, 1 );     /* joint_stereo */
    lsmash_bits_get( bits, 1 );     /* mc_coding */
    lsmash_bits_get( bits, 1 );     /* chan_config */
    lsmash_bits_get( bits, 1 );     /* chan_sort */
    lsmash_bits_get( bits, 1 );     /* crc_enabled */
    alssc->RLSLMS = lsmash_bits_get( bits, 1 );
    return 0;
}

static mp4a_AudioSpecificConfig_t * mp4a_get_AudioSpecificConfig( lsmash_bits_t *bits, uint8_t *dsi_payload, uint32_t dsi_payload_length )
{
    if( lsmash_bits_import_data( bits, dsi_payload, dsi_payload_length ) )
        return NULL;
    mp4a_AudioSpecificConfig_t *asc = (mp4a_AudioSpecificConfig_t *)lsmash_malloc_zero( sizeof(mp4a_AudioSpecificConfig_t) );
    if( !asc )
        return NULL;
    asc->audioObjectType = lsmash_bits_get( bits, 5 );
    if( asc->audioObjectType == 31 )
        asc->extensionAudioObjectType = asc->audioObjectType += 1 + lsmash_bits_get( bits, 6 );
    asc->samplingFrequencyIndex = lsmash_bits_get( bits, 4 );
    if( asc->samplingFrequencyIndex == 0xf )
        asc->samplingFrequency = lsmash_bits_get( bits, 24 );
    asc->channelConfiguration = lsmash_bits_get( bits, 4 );
    int ret = 0;
    switch( asc->audioObjectType )
    {
        case MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN :
        case MP4A_AUDIO_OBJECT_TYPE_AAC_LC :
        case MP4A_AUDIO_OBJECT_TYPE_AAC_SSR :
        case MP4A_AUDIO_OBJECT_TYPE_AAC_LTP :
        case MP4A_AUDIO_OBJECT_TYPE_AAC_scalable :
        case MP4A_AUDIO_OBJECT_TYPE_TwinVQ :
        case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LC :
        case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LTP :
        case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_scalable :
        case MP4A_AUDIO_OBJECT_TYPE_ER_Twin_VQ :
        case MP4A_AUDIO_OBJECT_TYPE_ER_BSAC :
        case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LD :
            ret = mp4a_get_GASpecificConfig( bits, asc );
            break;
        case MP4A_AUDIO_OBJECT_TYPE_Layer_1 :
        case MP4A_AUDIO_OBJECT_TYPE_Layer_2 :
        case MP4A_AUDIO_OBJECT_TYPE_Layer_3 :
            ret = mp4a_get_MPEG_1_2_SpecificConfig( bits, asc );
            break;
        case MP4A_AUDIO_OBJECT_TYPE_ALS :
            lsmash_bits_get( bits, 5 );
            ret = mp4a_get_ALSSpecificConfig( bits, asc );
            break;
        default :
            break;
    }
    return ret ? NULL : asc;
}

int mp4a_setup_summary_from_AudioSpecificConfig( lsmash_audio_summary_t *summary, uint8_t *dsi_payload, uint32_t dsi_payload_length )
{
    lsmash_bits_t *bits = lsmash_bits_adhoc_create();
    if( !bits )
        return -1;
    mp4a_AudioSpecificConfig_t *asc = mp4a_get_AudioSpecificConfig( bits, dsi_payload, dsi_payload_length );
    if( !asc )
        goto fail;
    summary->sample_type            = ISOM_CODEC_TYPE_MP4A_AUDIO;
    summary->object_type_indication = MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3;
    summary->stream_type            = MP4SYS_STREAM_TYPE_AudioStream;
    summary->exdata                 = NULL;
    summary->exdata_length          = 0;
    summary->aot                    = asc->audioObjectType;
    switch( asc->audioObjectType )
    {
        case MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN :
        case MP4A_AUDIO_OBJECT_TYPE_AAC_LC :
        case MP4A_AUDIO_OBJECT_TYPE_AAC_SSR :
        case MP4A_AUDIO_OBJECT_TYPE_AAC_LTP :
        case MP4A_AUDIO_OBJECT_TYPE_AAC_scalable :
        case MP4A_AUDIO_OBJECT_TYPE_TwinVQ :
        case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LC :
        case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LTP :
        case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_scalable :
        case MP4A_AUDIO_OBJECT_TYPE_ER_Twin_VQ :
        case MP4A_AUDIO_OBJECT_TYPE_ER_BSAC :
        case MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LD :
        case MP4A_AUDIO_OBJECT_TYPE_Layer_1 :
        case MP4A_AUDIO_OBJECT_TYPE_Layer_2 :
        case MP4A_AUDIO_OBJECT_TYPE_Layer_3 :
            if( asc->samplingFrequencyIndex == 0xf )
                summary->frequency = asc->samplingFrequency;
            else
            {
                uint8_t i = 0x0;
                while( i != 0xc )
                {
                    if( mp4a_sampling_frequency_table[i][2] == asc->samplingFrequencyIndex )
                    {
                        summary->frequency = mp4a_sampling_frequency_table[i][1];
                        break;
                    }
                    ++i;
                }
                if( i == 0xc )
                    goto fail;
            }
            if( asc->channelConfiguration < 8 )
                summary->channels = asc->channelConfiguration != 7 ? asc->channelConfiguration : 8;
            else
                summary->channels = 0;      /* reserved */
            summary->bit_depth = 16;
            switch( asc->audioObjectType )
            {
                case MP4A_AUDIO_OBJECT_TYPE_AAC_SSR :
                    summary->samples_in_frame = 1024;
                    break;
                case MP4A_AUDIO_OBJECT_TYPE_Layer_1 :
                    summary->samples_in_frame = 384;
                    break;
                case MP4A_AUDIO_OBJECT_TYPE_Layer_2 :
                case MP4A_AUDIO_OBJECT_TYPE_Layer_3 :
                    summary->samples_in_frame = 1152;
                    break;
                default :
                    summary->samples_in_frame = !((mp4a_GASpecificConfig_t *)asc->deepAudioSpecificConfig)->frameLengthFlag ? 1024 : 960;
                    break;
            }
            break;
        case MP4A_AUDIO_OBJECT_TYPE_ALS :
        {
            mp4a_ALSSpecificConfig_t *alssc = (mp4a_ALSSpecificConfig_t *)asc->deepAudioSpecificConfig;
            summary->frequency        = alssc->samp_freq;
            summary->channels         = alssc->channels + 1;
            summary->bit_depth        = (alssc->resolution + 1) * 8;
            summary->samples_in_frame = alssc->frame_length + 1;
            break;
        }
        default :
            break;
    }
    mp4a_remove_AudioSpecificConfig( asc );
    lsmash_bits_adhoc_cleanup( bits );
    return 0;
fail:
    mp4a_remove_AudioSpecificConfig( asc );
    lsmash_bits_adhoc_cleanup( bits );
    return -1;
}

/***************************************************************************
    audioProfileLevelIndication
***************************************************************************/
/* NOTE: This function is not strictly preferable, but accurate.
   The spec of audioProfileLevelIndication is too much complicated. */
mp4a_audioProfileLevelIndication mp4a_get_audioProfileLevelIndication( lsmash_audio_summary_t *summary )
{
    if( !summary || summary->stream_type != MP4SYS_STREAM_TYPE_AudioStream )
        return MP4A_AUDIO_PLI_NONE_REQUIRED; /* means error. */
    if( summary->object_type_indication != MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3 )
        return MP4A_AUDIO_PLI_NOT_SPECIFIED; /* This is of audio stream, but not described in ISO/IEC 14496-3. */
    if( summary->channels == 0 || summary->frequency == 0 )
        return MP4A_AUDIO_PLI_NONE_REQUIRED; /* means error. */

    mp4a_audioProfileLevelIndication pli = MP4A_AUDIO_PLI_NOT_SPECIFIED;
    switch( summary->aot )
    {
    case MP4A_AUDIO_OBJECT_TYPE_AAC_LC:
        if( summary->sbr_mode == MP4A_AAC_SBR_HIERARCHICAL )
        {
            /* NOTE: This is not strictly preferable, but accurate; just possibly over-estimated.
               We do not expect to use MP4A_AAC_SBR_HIERARCHICAL mode without SBR, nor downsampled mode with SBR. */
            if( summary->channels <= 2 && summary->frequency <= 24000 )
                pli = MP4A_AUDIO_PLI_HE_AAC_L2;
            else if( summary->channels <= 5 && summary->frequency <= 48000 )
                pli = MP4A_AUDIO_PLI_HE_AAC_L5;
            else
                pli = MP4A_AUDIO_PLI_NOT_SPECIFIED;
            break;
        }
        /* pretending plain AAC-LC, if actually HE-AAC. */
        static const uint32_t mp4sys_aac_pli_table[5][3] = {
            /* channels, frequency,    audioProfileLevelIndication */
            {         6,     96000,        MP4A_AUDIO_PLI_AAC_L5 }, /* FIXME: 6ch is not strictly correct, but works in many case. */
            {         6,     48000,        MP4A_AUDIO_PLI_AAC_L4 }, /* FIXME: 6ch is not strictly correct, but works in many case. */
            {         2,     48000,        MP4A_AUDIO_PLI_AAC_L2 },
            {         2,     24000,        MP4A_AUDIO_PLI_AAC_L1 },
            {         0,         0, MP4A_AUDIO_PLI_NOT_SPECIFIED }
        };
        for( int i = 0; summary->channels <= mp4sys_aac_pli_table[i][0] && summary->frequency <= mp4sys_aac_pli_table[i][1] ; i++ )
            pli = mp4sys_aac_pli_table[i][2];
        break;
    case MP4A_AUDIO_OBJECT_TYPE_ALS:
        /* FIXME: this is not stricly. Summary shall carry max_order, block_switching, bgmc_mode and RLSLMS. */
        if( summary->channels <= 2 && summary->frequency <= 48000 && summary->bit_depth <= 16 && summary->samples_in_frame <= 4096 )
            pli = MP4A_AUDIO_PLI_ALS_Simple_L1;
        else
            pli = MP4A_AUDIO_PLI_NOT_SPECIFIED;
        break;
    case MP4A_AUDIO_OBJECT_TYPE_Layer_1:
    case MP4A_AUDIO_OBJECT_TYPE_Layer_2:
    case MP4A_AUDIO_OBJECT_TYPE_Layer_3:
        pli = MP4A_AUDIO_PLI_NOT_SPECIFIED; /* 14496-3, Audio profiles and levels, does not allow any pli. */
        break;
    default:
        pli = MP4A_AUDIO_PLI_NOT_SPECIFIED; /* something we don't know/support, or what the spec never covers. */
        break;
    }
    return pli;
}

static int mp4sys_is_same_profile( mp4a_audioProfileLevelIndication a, mp4a_audioProfileLevelIndication b )
{
    switch( a )
    {
    case MP4A_AUDIO_PLI_Main_L1:
    case MP4A_AUDIO_PLI_Main_L2:
    case MP4A_AUDIO_PLI_Main_L3:
    case MP4A_AUDIO_PLI_Main_L4:
        if( MP4A_AUDIO_PLI_Main_L1 <= b && b <= MP4A_AUDIO_PLI_Main_L4 )
            return 1;
        return 0;
        break;
    case MP4A_AUDIO_PLI_Scalable_L1:
    case MP4A_AUDIO_PLI_Scalable_L2:
    case MP4A_AUDIO_PLI_Scalable_L3:
    case MP4A_AUDIO_PLI_Scalable_L4:
        if( MP4A_AUDIO_PLI_Scalable_L1 <= b && b <= MP4A_AUDIO_PLI_Scalable_L4 )
            return 1;
        return 0;
        break;
    case MP4A_AUDIO_PLI_Speech_L1:
    case MP4A_AUDIO_PLI_Speech_L2:
        if( MP4A_AUDIO_PLI_Speech_L1 <= b && b <= MP4A_AUDIO_PLI_Speech_L2 )
            return 1;
        return 0;
        break;
    case MP4A_AUDIO_PLI_Synthetic_L1:
    case MP4A_AUDIO_PLI_Synthetic_L2:
    case MP4A_AUDIO_PLI_Synthetic_L3:
        if( MP4A_AUDIO_PLI_Synthetic_L1 <= b && b <= MP4A_AUDIO_PLI_Synthetic_L3 )
            return 1;
        return 0;
        break;
    case MP4A_AUDIO_PLI_HighQuality_L1:
    case MP4A_AUDIO_PLI_HighQuality_L2:
    case MP4A_AUDIO_PLI_HighQuality_L3:
    case MP4A_AUDIO_PLI_HighQuality_L4:
    case MP4A_AUDIO_PLI_HighQuality_L5:
    case MP4A_AUDIO_PLI_HighQuality_L6:
    case MP4A_AUDIO_PLI_HighQuality_L7:
    case MP4A_AUDIO_PLI_HighQuality_L8:
        if( MP4A_AUDIO_PLI_HighQuality_L1 <= b && b <= MP4A_AUDIO_PLI_HighQuality_L8 )
            return 1;
        return 0;
        break;
    case MP4A_AUDIO_PLI_LowDelay_L1:
    case MP4A_AUDIO_PLI_LowDelay_L2:
    case MP4A_AUDIO_PLI_LowDelay_L3:
    case MP4A_AUDIO_PLI_LowDelay_L4:
    case MP4A_AUDIO_PLI_LowDelay_L5:
    case MP4A_AUDIO_PLI_LowDelay_L6:
    case MP4A_AUDIO_PLI_LowDelay_L7:
    case MP4A_AUDIO_PLI_LowDelay_L8:
        if( MP4A_AUDIO_PLI_LowDelay_L1 <= b && b <= MP4A_AUDIO_PLI_LowDelay_L8 )
            return 1;
        return 0;
        break;
    case MP4A_AUDIO_PLI_Natural_L1:
    case MP4A_AUDIO_PLI_Natural_L2:
    case MP4A_AUDIO_PLI_Natural_L3:
    case MP4A_AUDIO_PLI_Natural_L4:
        if( MP4A_AUDIO_PLI_Natural_L1 <= b && b <= MP4A_AUDIO_PLI_Natural_L4 )
            return 1;
        return 0;
        break;
    case MP4A_AUDIO_PLI_MobileInternetworking_L1:
    case MP4A_AUDIO_PLI_MobileInternetworking_L2:
    case MP4A_AUDIO_PLI_MobileInternetworking_L3:
    case MP4A_AUDIO_PLI_MobileInternetworking_L4:
    case MP4A_AUDIO_PLI_MobileInternetworking_L5:
    case MP4A_AUDIO_PLI_MobileInternetworking_L6:
        if( MP4A_AUDIO_PLI_MobileInternetworking_L1 <= b && b <= MP4A_AUDIO_PLI_MobileInternetworking_L6 )
            return 1;
        return 0;
        break;
    case MP4A_AUDIO_PLI_AAC_L1:
    case MP4A_AUDIO_PLI_AAC_L2:
    case MP4A_AUDIO_PLI_AAC_L4:
    case MP4A_AUDIO_PLI_AAC_L5:
        if( MP4A_AUDIO_PLI_AAC_L1 <= b && b <= MP4A_AUDIO_PLI_AAC_L5 )
            return 1;
        return 0;
        break;
    case MP4A_AUDIO_PLI_HE_AAC_L2:
    case MP4A_AUDIO_PLI_HE_AAC_L3:
    case MP4A_AUDIO_PLI_HE_AAC_L4:
    case MP4A_AUDIO_PLI_HE_AAC_L5:
        if( MP4A_AUDIO_PLI_HE_AAC_L2 <= b && b <= MP4A_AUDIO_PLI_HE_AAC_L5 )
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
mp4a_audioProfileLevelIndication mp4a_max_audioProfileLevelIndication( mp4a_audioProfileLevelIndication a, mp4a_audioProfileLevelIndication b )
{
    /* NONE_REQUIRED is minimal priotity, and NOT_SPECIFIED is max priority. */
    if( a == MP4A_AUDIO_PLI_NOT_SPECIFIED || b == MP4A_AUDIO_PLI_NONE_REQUIRED )
        return a;
    if( a == MP4A_AUDIO_PLI_NONE_REQUIRED || b == MP4A_AUDIO_PLI_NOT_SPECIFIED )
        return b;
    mp4a_audioProfileLevelIndication c, d;
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
    if( MP4A_AUDIO_PLI_AAC_L1 <= c && c <= MP4A_AUDIO_PLI_AAC_L5
        && MP4A_AUDIO_PLI_HE_AAC_L2 <= d && d <= MP4A_AUDIO_PLI_HE_AAC_L5 )
    {
        if( c <= MP4A_AUDIO_PLI_AAC_L2 )
            return d;
        c += 4; /* upgrade to HE-AAC */
        return c > d ? c : d;
    }
    /* General */
    if( mp4sys_is_same_profile( c, d ) )
        return d;
    return MP4A_AUDIO_PLI_NOT_SPECIFIED;
}
