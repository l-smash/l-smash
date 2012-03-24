/*****************************************************************************
 * dts.c:
 *****************************************************************************
 * Copyright (C) 2012 L-SMASH project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
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

#include <stdlib.h>
#include <string.h>

#include "box.h"

/***************************************************************************
    ETSI TS 102 114 V1.2.1 (2002-12)
    ETSI TS 102 114 V1.3.1 (2011-08)
***************************************************************************/
#include "dts.h"

#define DTS_MIN_CORE_SIZE           96
#define DTS_MAX_STREAM_CONSTRUCTION 21
#define DTS_SPECIFIC_BOX_LENGTH     28

typedef enum
{
    DTS_SYNCWORD_CORE           = 0x7FFE8001,
    DTS_SYNCWORD_XCH            = 0x5A5A5A5A,
    DTS_SYNCWORD_XXCH           = 0x47004A03,
    DTS_SYNCWORD_X96K           = 0x1D95F262,
    DTS_SYNCWORD_XBR            = 0x655E315E,
    DTS_SYNCWORD_LBR            = 0x0A801921,
    DTS_SYNCWORD_XLL            = 0x41A29547,
    DTS_SYNCWORD_SUBSTREAM      = 0x64582025,
    DTS_SYNCWORD_SUBSTREAM_CORE = 0x02b09261,
} dts_syncword;

typedef enum
{
    DTS_XXCH_LOUDSPEAKER_MASK_C    = 0x00000001,    /* Centre in front of listener */
    DTS_XXCH_LOUDSPEAKER_MASK_L    = 0x00000002,    /* Left in front */
    DTS_XXCH_LOUDSPEAKER_MASK_R    = 0x00000004,    /* Right in front */
    DTS_XXCH_LOUDSPEAKER_MASK_LS   = 0x00000008,    /* Left surround on side in rear */
    DTS_XXCH_LOUDSPEAKER_MASK_RS   = 0x00000010,    /* Right surround on side in rear */
    DTS_XXCH_LOUDSPEAKER_MASK_LFE1 = 0x00000020,    /* Low frequency effects subwoofer */
    DTS_XXCH_LOUDSPEAKER_MASK_CS   = 0x00000040,    /* Centre surround in rear */
    DTS_XXCH_LOUDSPEAKER_MASK_LSR  = 0x00000080,    /* Left surround in rear */
    DTS_XXCH_LOUDSPEAKER_MASK_RSR  = 0x00000100,    /* Right surround in rear */
    DTS_XXCH_LOUDSPEAKER_MASK_LSS  = 0x00000200,    /* Left surround on side */
    DTS_XXCH_LOUDSPEAKER_MASK_RSS  = 0x00000400,    /* Right surround on side */
    DTS_XXCH_LOUDSPEAKER_MASK_LC   = 0x00000800,    /* Between left and centre in front */
    DTS_XXCH_LOUDSPEAKER_MASK_RC   = 0x00001000,    /* Between right and centre in front */
    DTS_XXCH_LOUDSPEAKER_MASK_LH   = 0x00002000,    /* Left height in front */
    DTS_XXCH_LOUDSPEAKER_MASK_CH   = 0x00004000,    /* Centre Height in front */
    DTS_XXCH_LOUDSPEAKER_MASK_RH   = 0x00008000,    /* Right Height in front */
    DTS_XXCH_LOUDSPEAKER_MASK_LFE2 = 0x00010000,    /* Second low frequency effects subwoofer */
    DTS_XXCH_LOUDSPEAKER_MASK_LW   = 0x00020000,    /* Left on side in front */
    DTS_XXCH_LOUDSPEAKER_MASK_RW   = 0x00040000,    /* Right on side in front */
    DTS_XXCH_LOUDSPEAKER_MASK_OH   = 0x00080000,    /* Over the listener's head */
    DTS_XXCH_LOUDSPEAKER_MASK_LHS  = 0x00100000,    /* Left height on side */
    DTS_XXCH_LOUDSPEAKER_MASK_RHS  = 0x00200000,    /* Right height on side */
    DTS_XXCH_LOUDSPEAKER_MASK_CHR  = 0x00400000,    /* Centre height in rear */
    DTS_XXCH_LOUDSPEAKER_MASK_LHR  = 0x00800000,    /* Left height in rear */
    DTS_XXCH_LOUDSPEAKER_MASK_RHR  = 0x01000000,    /* Right height in rear */
    DTS_XXCH_LOUDSPEAKER_MASK_CL   = 0x02000000,    /* Centre in the plane lower than listener's ears */
    DTS_XXCH_LOUDSPEAKER_MASK_LL   = 0x04000000,    /* Left in the plane lower than listener's ears */
    DTS_XXCH_LOUDSPEAKER_MASK_RL   = 0x08000000,    /* Right in the plane lower than listener's ears */
} dts_loudspeaker_mask;

typedef enum
{
    DTS_CHANNEL_LAYOUT_C       = 0x0001,    /* Centre in front of listener */
    DTS_CHANNEL_LAYOUT_L_R     = 0x0002,    /* Left/Right in front */
    DTS_CHANNEL_LAYOUT_LS_RS   = 0x0004,    /* Left/Right surround on side in rear */
    DTS_CHANNEL_LAYOUT_LFE1    = 0x0008,    /* Low frequency effects subwoofer */
    DTS_CHANNEL_LAYOUT_CS      = 0x0010,    /* Centre surround in rear */
    DTS_CHANNEL_LAYOUT_LH_RH   = 0x0020,    /* Left/Right height in front */
    DTS_CHANNEL_LAYOUT_LSR_RSR = 0x0040,    /* Left/Right surround in rear */
    DTS_CHANNEL_LAYOUT_CH      = 0x0080,    /* Centre height in front */
    DTS_CHANNEL_LAYOUT_OH      = 0x0100,    /* Over the listener's head */
    DTS_CHANNEL_LAYOUT_LC_RC   = 0x0200,    /* Between left/right and centre in front */
    DTS_CHANNEL_LAYOUT_LW_RW   = 0x0400,    /* Left/Right on side in front */
    DTS_CHANNEL_LAYOUT_LSS_RSS = 0x0800,    /* Left/Right surround on side */
    DTS_CHANNEL_LAYOUT_LFE2    = 0x1000,    /* Second low frequency effects subwoofer */
    DTS_CHANNEL_LAYOUT_LHS_RHS = 0x2000,    /* Left/Right height on side */
    DTS_CHANNEL_LAYOUT_CHR     = 0x4000,    /* Centre height in rear */
    DTS_CHANNEL_LAYOUT_LHR_RHR = 0x8000,    /* Left/Right height in rear */
} dts_channel_layout;

uint8_t lsmash_dts_get_stream_construction( lsmash_dts_construction_flag flags )
{
    static const lsmash_dts_construction_flag construction_info[DTS_MAX_STREAM_CONSTRUCTION + 1] =
        {
            0,
            DTS_CORE_SUBSTREAM_CORE_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_CORE_SUBSTREAM_XCH_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_CORE_SUBSTREAM_XXCH_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_CORE_SUBSTREAM_X96_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_EXT_SUBSTREAM_XXCH_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_EXT_SUBSTREAM_XBR_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_CORE_SUBSTREAM_XCH_FLAG  | DTS_EXT_SUBSTREAM_XBR_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_CORE_SUBSTREAM_XXCH_FLAG | DTS_EXT_SUBSTREAM_XBR_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_EXT_SUBSTREAM_XXCH_FLAG  | DTS_EXT_SUBSTREAM_XBR_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_EXT_SUBSTREAM_X96_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_CORE_SUBSTREAM_XCH_FLAG  | DTS_EXT_SUBSTREAM_X96_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_CORE_SUBSTREAM_XXCH_FLAG | DTS_EXT_SUBSTREAM_X96_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_EXT_SUBSTREAM_XXCH_FLAG  | DTS_EXT_SUBSTREAM_X96_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_EXT_SUBSTREAM_XLL_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_CORE_SUBSTREAM_XCH_FLAG  | DTS_EXT_SUBSTREAM_XLL_FLAG,
            DTS_CORE_SUBSTREAM_CORE_FLAG | DTS_CORE_SUBSTREAM_X96_FLAG  | DTS_EXT_SUBSTREAM_XLL_FLAG,
            DTS_EXT_SUBSTREAM_XLL_FLAG,
            DTS_EXT_SUBSTREAM_LBR_FLAG,
            DTS_EXT_SUBSTREAM_CORE_FLAG,
            DTS_EXT_SUBSTREAM_CORE_FLAG  | DTS_EXT_SUBSTREAM_XXCH_FLAG,
            DTS_EXT_SUBSTREAM_CORE_FLAG  | DTS_EXT_SUBSTREAM_XLL_FLAG ,
        };
    uint8_t StreamConstruction;
    for( StreamConstruction = 1; StreamConstruction <= DTS_MAX_STREAM_CONSTRUCTION; StreamConstruction++ )
        if( flags == construction_info[StreamConstruction] )
            break;
    /* For any stream type not listed in the above table,
     * StreamConstruction shall be set to 0 and the codingname shall default to 'dtsh'. */
    return StreamConstruction <= DTS_MAX_STREAM_CONSTRUCTION ? StreamConstruction : 0;
}

uint32_t lsmash_dts_get_codingname( lsmash_dts_specific_parameters_t *param )
{
    assert( param->StreamConstruction <= DTS_MAX_STREAM_CONSTRUCTION );
    if( param->MultiAssetFlag )
        return ISOM_CODEC_TYPE_DTSH_AUDIO;  /* Multiple asset streams shall use the 'dtsh' coding_name. */
    static const uint32_t codingname_table[DTS_MAX_STREAM_CONSTRUCTION + 1] =
        {
            ISOM_CODEC_TYPE_DTSH_AUDIO,     /* Undefined stream types shall be set to 0 and the codingname shall default to 'dtsh'. */
            ISOM_CODEC_TYPE_DTSC_AUDIO,
            ISOM_CODEC_TYPE_DTSC_AUDIO,
            ISOM_CODEC_TYPE_DTSH_AUDIO,
            ISOM_CODEC_TYPE_DTSC_AUDIO,
            ISOM_CODEC_TYPE_DTSH_AUDIO,
            ISOM_CODEC_TYPE_DTSH_AUDIO,
            ISOM_CODEC_TYPE_DTSH_AUDIO,
            ISOM_CODEC_TYPE_DTSH_AUDIO,
            ISOM_CODEC_TYPE_DTSH_AUDIO,
            ISOM_CODEC_TYPE_DTSH_AUDIO,
            ISOM_CODEC_TYPE_DTSH_AUDIO,
            ISOM_CODEC_TYPE_DTSH_AUDIO,
            ISOM_CODEC_TYPE_DTSH_AUDIO,
            ISOM_CODEC_TYPE_DTSL_AUDIO,
            ISOM_CODEC_TYPE_DTSL_AUDIO,
            ISOM_CODEC_TYPE_DTSL_AUDIO,
            ISOM_CODEC_TYPE_DTSL_AUDIO,
            ISOM_CODEC_TYPE_DTSE_AUDIO,
            ISOM_CODEC_TYPE_DTSH_AUDIO,
            ISOM_CODEC_TYPE_DTSH_AUDIO,
            ISOM_CODEC_TYPE_DTSL_AUDIO,
        };
    return codingname_table[ param->StreamConstruction ];
}

uint8_t *lsmash_create_dts_specific_info( lsmash_dts_specific_parameters_t *param, uint32_t *data_length )
{
    lsmash_bits_t bits = { 0 };
    lsmash_bs_t   bs   = { 0 };
    lsmash_bits_init( &bits, &bs );
    uint8_t buffer[DTS_SPECIFIC_BOX_LENGTH] = { 0 };
    bs.data  = buffer;
    bs.alloc = DTS_SPECIFIC_BOX_LENGTH;
    /* Create a DTSSpecificBox. */
    lsmash_bits_put( &bits, 32, 0 );                            /* box size */
    lsmash_bits_put( &bits, 32, ISOM_BOX_TYPE_DDTS );           /* box type: 'ddts' */
    lsmash_bits_put( &bits, 32, param->DTSSamplingFrequency );
    lsmash_bits_put( &bits, 32, param->maxBitrate );            /* maxBitrate; setup by isom_update_bitrate_description */
    lsmash_bits_put( &bits, 32, param->avgBitrate );            /* avgBitrate; setup by isom_update_bitrate_description */
    lsmash_bits_put( &bits, 8, param->pcmSampleDepth );
    lsmash_bits_put( &bits, 2, param->FrameDuration );
    lsmash_bits_put( &bits, 5, param->StreamConstruction );
    lsmash_bits_put( &bits, 1, param->CoreLFEPresent );
    lsmash_bits_put( &bits, 6, param->CoreLayout );
    lsmash_bits_put( &bits, 14, param->CoreSize );
    lsmash_bits_put( &bits, 1, param->StereoDownmix );
    lsmash_bits_put( &bits, 3, param->RepresentationType );
    lsmash_bits_put( &bits, 16, param->ChannelLayout );
    lsmash_bits_put( &bits, 1, param->MultiAssetFlag );
    lsmash_bits_put( &bits, 1, param->LBRDurationMod );
    lsmash_bits_put( &bits, 6, 0 );                             /* Reserved */
    uint8_t *data = lsmash_bits_export_data( &bits, data_length );
    /* Update box size. */
    data[0] = ((*data_length) >> 24) & 0xff;
    data[1] = ((*data_length) >> 16) & 0xff;
    data[2] = ((*data_length) >>  8) & 0xff;
    data[3] =  (*data_length)        & 0xff;
    return data;
}

int lsmash_setup_dts_specific_parameters_from_frame( lsmash_dts_specific_parameters_t *param, uint8_t *data, uint32_t data_length )
{
    lsmash_bits_t bits    = { 0 };
    lsmash_bs_t   bs      = { 0 };
    uint8_t buffer[DTS_MAX_EXTENSION_SIZE] = { 0 };
    bs.data  = buffer;
    bs.alloc = DTS_MAX_EXTENSION_SIZE;
    dts_info_t  handler = { 0 };
    dts_info_t *info    = &handler;
    uint32_t overall_wasted_data_length = 0;
    info->buffer_pos = info->buffer;
    info->buffer_end = info->buffer;
    info->bits = &bits;
    lsmash_bits_init( &bits, &bs );
    while( 1 )
    {
        /* Check the remainder length of the buffer.
         * If there is enough length, then continue to parse the frame in it.
         * The length 10 is the required byte length to get frame size. */
        uint32_t remainder_length = info->buffer_end - info->buffer_pos;
        if( !info->no_more_read && remainder_length < DTS_MAX_EXTENSION_SIZE )
        {
            if( remainder_length )
                memmove( info->buffer, info->buffer_pos, remainder_length );
            uint32_t wasted_data_length = LSMASH_MIN( data_length, DTS_MAX_EXTENSION_SIZE );
            memcpy( info->buffer + remainder_length, data + overall_wasted_data_length, wasted_data_length );
            data_length                -= wasted_data_length;
            overall_wasted_data_length += wasted_data_length;
            remainder_length           += wasted_data_length;
            info->buffer_pos = info->buffer;
            info->buffer_end = info->buffer + remainder_length;
            info->no_more_read = (data_length < 10);
        }
        if( remainder_length < 10 && info->no_more_read )
            goto setup_param;   /* No more valid data. */
        /* Parse substream frame. */
        dts_substream_type prev_substream_type = info->substream_type;
        info->substream_type = dts_get_substream_type( info );
        int (*dts_parse_frame)( dts_info_t *, uint8_t *, uint32_t ) = NULL;
        switch( info->substream_type )
        {
            /* Decide substream frame parser and check if this frame and the previous frame belong to the same AU. */
            case DTS_SUBSTREAM_TYPE_CORE :
                if( prev_substream_type != DTS_SUBSTREAM_TYPE_NONE )
                    goto setup_param;
                dts_parse_frame = dts_parse_core_substream;
                break;
            case DTS_SUBSTREAM_TYPE_EXTENSION :
            {
                uint8_t prev_extension_index = info->extension_index;
                if( dts_get_extension_index( info, &info->extension_index ) )
                    return -1;
                if( prev_substream_type == DTS_SUBSTREAM_TYPE_EXTENSION && info->extension_index <= prev_extension_index )
                    goto setup_param;
                dts_parse_frame = dts_parse_extension_substream;
                break;
            }
            default :
                return -1;
        }
        info->frame_size = 0;
        if( dts_parse_frame( info, info->buffer_pos, LSMASH_MIN( remainder_length, DTS_MAX_EXTENSION_SIZE ) ) )
            return -1;  /* Failed to parse. */
        info->buffer_pos += info->frame_size;
    }
setup_param:
    dts_update_specific_param( info );
    *param = info->ddts_param;
    return 0;
}

static uint32_t dts_bits_get( lsmash_bits_t *bits, uint32_t width, uint64_t *bits_pos )
{
    *bits_pos += width;
    return lsmash_bits_get( bits, width );
}

int dts_get_channel_count_from_channel_layout( uint16_t channel_layout )
{
#define DTS_CHANNEL_PAIR_MASK      \
       (DTS_CHANNEL_LAYOUT_L_R     \
      | DTS_CHANNEL_LAYOUT_LS_RS   \
      | DTS_CHANNEL_LAYOUT_LH_RH   \
      | DTS_CHANNEL_LAYOUT_LSR_RSR \
      | DTS_CHANNEL_LAYOUT_LC_RC   \
      | DTS_CHANNEL_LAYOUT_LW_RW   \
      | DTS_CHANNEL_LAYOUT_LSS_RSS \
      | DTS_CHANNEL_LAYOUT_LHS_RHS \
      | DTS_CHANNEL_LAYOUT_LHR_RHR)
    return lsmash_count_bits( channel_layout )
         + lsmash_count_bits( channel_layout & DTS_CHANNEL_PAIR_MASK );
#undef DTS_CHANNEL_PAIR_MASK
}

static uint32_t dts_get_channel_layout_from_xxch_mask( uint32_t mask )
{
    uint32_t layout = 0;
    if( mask & DTS_XXCH_LOUDSPEAKER_MASK_C )
        layout |= DTS_CHANNEL_LAYOUT_C;
    if( mask & (DTS_XXCH_LOUDSPEAKER_MASK_L | DTS_XXCH_LOUDSPEAKER_MASK_R) )
        layout |= DTS_CHANNEL_LAYOUT_L_R;
    if( mask & (DTS_XXCH_LOUDSPEAKER_MASK_LS | DTS_XXCH_LOUDSPEAKER_MASK_RS) )
        layout |= DTS_CHANNEL_LAYOUT_LS_RS;
    if( mask & DTS_XXCH_LOUDSPEAKER_MASK_LFE1 )
        layout |= DTS_CHANNEL_LAYOUT_LFE1;
    if( mask & DTS_XXCH_LOUDSPEAKER_MASK_CS )
        layout |= DTS_CHANNEL_LAYOUT_CS;
    if( mask & (DTS_XXCH_LOUDSPEAKER_MASK_LH | DTS_XXCH_LOUDSPEAKER_MASK_RH) )
        layout |= DTS_CHANNEL_LAYOUT_LH_RH;
    if( mask & (DTS_XXCH_LOUDSPEAKER_MASK_LSR | DTS_XXCH_LOUDSPEAKER_MASK_RSR) )
        layout |= DTS_CHANNEL_LAYOUT_LSR_RSR;
    if( mask & DTS_XXCH_LOUDSPEAKER_MASK_CH )
        layout |= DTS_CHANNEL_LAYOUT_CH;
    if( mask & DTS_XXCH_LOUDSPEAKER_MASK_OH )
        layout |= DTS_CHANNEL_LAYOUT_OH;
    if( mask & (DTS_XXCH_LOUDSPEAKER_MASK_LC | DTS_XXCH_LOUDSPEAKER_MASK_RC) )
        layout |= DTS_CHANNEL_LAYOUT_LC_RC;
    if( mask & (DTS_XXCH_LOUDSPEAKER_MASK_LW | DTS_XXCH_LOUDSPEAKER_MASK_RW) )
        layout |= DTS_CHANNEL_LAYOUT_LW_RW;
    if( mask & (DTS_XXCH_LOUDSPEAKER_MASK_LSS | DTS_XXCH_LOUDSPEAKER_MASK_RSS) )
        layout |= DTS_CHANNEL_LAYOUT_LSS_RSS;
    if( mask & DTS_XXCH_LOUDSPEAKER_MASK_LFE2 )
        layout |= DTS_CHANNEL_LAYOUT_LFE2;
    if( mask & (DTS_XXCH_LOUDSPEAKER_MASK_LHS | DTS_XXCH_LOUDSPEAKER_MASK_RHS) )
        layout |= DTS_CHANNEL_LAYOUT_LHS_RHS;
    if( mask & DTS_XXCH_LOUDSPEAKER_MASK_CHR )
        layout |= DTS_CHANNEL_LAYOUT_CHR;
    if( mask & (DTS_XXCH_LOUDSPEAKER_MASK_LHR | DTS_XXCH_LOUDSPEAKER_MASK_RHR) )
        layout |= DTS_CHANNEL_LAYOUT_LHR_RHR;
    return layout;
}

static int dts_parse_asset_descriptor( dts_info_t *info, uint64_t *bits_pos )
{
    lsmash_bits_t *bits = info->bits;
    /* Audio asset descriptor */
    uint64_t asset_descriptor_pos = *bits_pos;
    int nuAssetDescriptFsize = dts_bits_get( bits, 9, bits_pos );                                   /* nuAssetDescriptFsize          (9) */
    dts_bits_get( bits, 3, bits_pos );                                                              /* nuAssetIndex                  (3) */
    /* Static metadata */
    int bEmbeddedStereoFlag = 0;
    int bEmbeddedSixChFlag  = 0;
    int nuTotalNumChs       = 0;
    if( info->extension.bStaticFieldsPresent )
    {
        if( dts_bits_get( bits, 1, bits_pos ) )                                                     /* bAssetTypeDescrPresent        (1)*/
            dts_bits_get( bits, 4, bits_pos );                                                      /* nuAssetTypeDescriptor         (4) */
        if( dts_bits_get( bits, 1, bits_pos ) )                                                     /* bLanguageDescrPresent         (1) */
            dts_bits_get( bits, 24, bits_pos );                                                     /* LanguageDescriptor            (24) */
        if( dts_bits_get( bits, 1, bits_pos ) )
        {
            int nuInfoTextByteSize = dts_bits_get( bits, 10, bits_pos ) + 1;                        /* nuInfoTextByteSize            (10) */
            dts_bits_get( bits, nuInfoTextByteSize * 8, bits_pos );                                 /* InfoTextString                (nuInfoTextByteSize) */
        }
        int nuBitResolution = dts_bits_get( bits, 5, bits_pos ) + 1;                                /* nuBitResolution               (5) */
        info->extension.bit_resolution = LSMASH_MAX( info->extension.bit_resolution, nuBitResolution );
        int nuMaxSampleRate = dts_bits_get( bits, 4, bits_pos );                                    /* nuMaxSampleRate               (4) */
        static const uint32_t source_sample_rate_table[16] =
            {
                 8000, 16000, 32000, 64000, 128000,
                       22050, 44100, 88200, 176400, 352800,
                12000, 24000, 48000, 96000, 192000, 384000
            };
        info->extension.sampling_frequency = LSMASH_MAX( info->extension.sampling_frequency, source_sample_rate_table[nuMaxSampleRate] );
        nuTotalNumChs = dts_bits_get( bits, 8, bits_pos ) + 1;                                      /* nuTotalNumChs                 (8) */
        info->extension.bOne2OneMapChannels2Speakers = dts_bits_get( bits, 1, bits_pos );           /* bOne2OneMapChannels2Speakers  (1) */
        if( info->extension.bOne2OneMapChannels2Speakers )
        {
            if( nuTotalNumChs > 2 )
            {
                bEmbeddedStereoFlag = dts_bits_get( bits, 1, bits_pos );                            /* bEmbeddedStereoFlag           (1) */
                info->extension.stereo_downmix |= bEmbeddedStereoFlag;
            }
            if( nuTotalNumChs > 6 )
                bEmbeddedSixChFlag = dts_bits_get( bits, 1, bits_pos );                             /* bEmbeddedSixChFlag            (1) */
            int nuNumBits4SAMask;
            if( dts_bits_get( bits, 1, bits_pos ) )                                                 /* bSpkrMaskEnabled              (1) */
            {
                nuNumBits4SAMask = (dts_bits_get( bits, 2, bits_pos ) + 1) << 2;                    /* nuNumBits4SAMask              (2) */
                info->extension.channel_layout |= dts_bits_get( bits, nuNumBits4SAMask, bits_pos ); /* nuSpkrActivityMask            (nuNumBits4SAMask) */
            }
            else
                /* The specification doesn't mention the value of nuNumBits4SAMask if bSpkrMaskEnabled is set to 0. */
                nuNumBits4SAMask = 0;
            int nuNumSpkrRemapSets = dts_bits_get( bits, 3, bits_pos );
            int nuStndrSpkrLayoutMask[8] = { 0 };
            for( int ns = 0; ns < nuNumSpkrRemapSets; ns++ )
                nuStndrSpkrLayoutMask[ns] = dts_bits_get( bits, nuNumBits4SAMask, bits_pos );
            for( int ns = 0; ns < nuNumSpkrRemapSets; ns++ )
            {
                int nuNumSpeakers = dts_get_channel_count_from_channel_layout( nuStndrSpkrLayoutMask[ns] );
                int nuNumDecCh4Remap = dts_bits_get( bits, 5, bits_pos ) + 1;                       /* nuNumDecCh4Remap[ns]          (5) */
                for( int nCh = 0; nCh < nuNumSpeakers; nCh++ )
                {
                    uint32_t nuRemapDecChMask = dts_bits_get( bits, nuNumDecCh4Remap, bits_pos );
                    int nCoef = lsmash_count_bits( nuRemapDecChMask );
                    for( int nc = 0; nc < nCoef; nc++ )
                        dts_bits_get( bits, 5, bits_pos );                                          /* nuSpkrRemapCodes[ns][nCh][nc] (5) */
                }
            }
        }
        else
        {
            info->extension.representation_type = dts_bits_get( bits, 3, bits_pos );                /* nuRepresentationType          (3) */
            if( info->extension.representation_type == 2 || info->extension.representation_type == 3 )
                nuTotalNumChs = 2;
        }
    }
    /* Dynamic metadata */
    int bDRCCoefPresent = dts_bits_get( bits, 1, bits_pos );                                        /* bDRCCoefPresent               (1) */
    if( bDRCCoefPresent )
        dts_bits_get( bits, 8, bits_pos );                                                          /* nuDRCCode                     (8) */
    if( dts_bits_get( bits, 1, bits_pos ) )                                                         /* bDialNormPresent              (1) */
        dts_bits_get( bits, 5, bits_pos );                                                          /* nuDialNormCode                (5) */
    if( bDRCCoefPresent && bEmbeddedStereoFlag )
        dts_bits_get( bits, 8, bits_pos );                                                          /* nuDRC2ChDmixCode              (8) */
    int bMixMetadataPresent;
    if( info->extension.bMixMetadataEnbl )
        bMixMetadataPresent = dts_bits_get( bits, 1, bits_pos );                                    /* bMixMetadataPresent           (1) */
    else
        bMixMetadataPresent = 0;
    if( bMixMetadataPresent )
    {
        dts_bits_get( bits, 7, bits_pos );                                                          /* bExternalMixFlag              (1)
                                                                                                     * nuPostMixGainAdjCode          (7) */
        if( dts_bits_get( bits, 2, bits_pos ) < 3 )                                                 /* nuControlMixerDRC             (2) */
            dts_bits_get( bits, 3, bits_pos );                                                      /* nuLimit4EmbeddedDRC           (3) */
        else
            dts_bits_get( bits, 8, bits_pos );                                                      /* nuCustomDRCCode               (8) */
        int bEnblPerChMainAudioScale = dts_bits_get( bits, 1, bits_pos );                           /* bEnblPerChMainAudioScale      (1) */
        for( uint8_t ns = 0; ns < info->extension.nuNumMixOutConfigs; ns++ )
            if( bEnblPerChMainAudioScale )
                for( uint8_t nCh = 0; nCh < info->extension.nNumMixOutCh[ns]; nCh++ )
                    dts_bits_get( bits, 6, bits_pos );                                              /* nuMainAudioScaleCode[ns][nCh] (6) */
            else
                dts_bits_get( bits, 6, bits_pos );                                                  /* nuMainAudioScaleCode[ns][0]   (6) */
        int nEmDM = 1;
        int nDecCh[3] = { nuTotalNumChs, 0, 0 };
        if( bEmbeddedSixChFlag )
        {
            nDecCh[nEmDM] = 6;
            ++nEmDM;
        }
        if( bEmbeddedStereoFlag )
        {
            nDecCh[nEmDM] = 2;
            ++nEmDM;
        }
        for( uint8_t ns = 0; ns < info->extension.nuNumMixOutConfigs; ns++ )
            for( int nE = 0; nE < nEmDM; nE++ )
                for( int nCh = 0; nCh < nDecCh[nE]; nCh++ )
                {
                    int nuMixMapMask = dts_bits_get( bits, info->extension.nNumMixOutCh[ns], bits_pos );    /* nuMixMapMask          (nNumMixOutCh[ns]) */
                    int nuNumMixCoefs = lsmash_count_bits( nuMixMapMask );
                    for( int nC = 0; nC < nuNumMixCoefs; nC++ )
                        dts_bits_get( bits, 6, bits_pos );                                          /* nuMixCoeffs[ns][nE][nCh][nC]  (6) */
                }
    }
    /* Decoder navigation data */
    if( dts_bits_get( bits, 2, bits_pos ) == 0 )                                                    /* nuCodingMode                  (2) */
    {
        int nuCoreExtensionMask = dts_bits_get( bits, 12, bits_pos );                               /* nuCoreExtensionMask           (12) */
        if( nuCoreExtensionMask & DTS_EXT_SUBSTREAM_CORE_FLAG )
            info->flags |= DTS_EXT_SUBSTREAM_CORE_FLAG;
    }
    dts_bits_get( bits, nuAssetDescriptFsize * 8 - (*bits_pos - asset_descriptor_pos), bits_pos );  /* Skip remaining part of Audio asset descriptor. */
    return bits->bs->error ? -1 : 0;
}

static int dts_parse_xxch( dts_info_t *info, uint64_t *bits_pos, int extension )
{
    lsmash_bits_t *bits = info->bits;
    /* XXCH Frame Header */
    uint64_t xxch_pos = *bits_pos - 32;                                                             /* SYNCXXCh                    (32) */
    if( !extension && (info->core.extension_audio_descriptor == 0 || info->core.extension_audio_descriptor == 3) )
        return -1;
    uint64_t nuHeaderSizeXXCh       = dts_bits_get( bits, 6, bits_pos ) + 1;                        /* nuHeaderSizeXXCh            (6) */
    dts_bits_get( bits, 1, bits_pos );                                                              /* bCRCPresent4ChSetHeaderXXCh (1) */
    int nuBits4SpkrMaskXXCh         = dts_bits_get( bits, 5, bits_pos ) + 1;                        /* nuBits4SpkrMaskXXCh         (5) */
    int nuNumChSetsInXXCh           = dts_bits_get( bits, 2, bits_pos ) + 1;                        /* nuNumChSetsInXXCh           (2) */
    int pnuChSetFsizeXXCh[4];
    for( int nChSet = 0; nChSet < nuNumChSetsInXXCh; nChSet++ )
        pnuChSetFsizeXXCh[nChSet] = dts_bits_get( bits, 14, bits_pos ) + 1;                         /* pnuChSetFsizeXXCh[nChSet]   (14) */
    uint32_t xxch_mask = dts_bits_get( bits, nuBits4SpkrMaskXXCh, bits_pos );                       /* nuCoreSpkrActivityMask      (nuBits4SpkrMaskXXCh) */
    uint16_t *channel_layout = extension ? &info->extension.channel_layout : &info->core.channel_layout;
    *channel_layout |= dts_get_channel_layout_from_xxch_mask( xxch_mask );
    uint8_t *xxch_lower_planes = extension ? &info->extension.xxch_lower_planes : &info->core.xxch_lower_planes;
    *xxch_lower_planes = (xxch_mask >> 25) & 0x7;
    dts_bits_get( bits, nuHeaderSizeXXCh * 8 - (*bits_pos - xxch_pos), bits_pos );      /* Skip remaining part of XXCH Frame Header. */
    for( int nChSet = 0; nChSet < nuNumChSetsInXXCh; nChSet++ )
    {
        /* XXCH Channel Set Header */
        xxch_pos = *bits_pos;
        uint64_t nuXXChChSetHeaderSize = dts_bits_get( bits, 7, bits_pos ) + 1;                     /* nuXXChChSetHeaderSize       (7)*/
        dts_bits_get( bits, 3, bits_pos );                                                          /* nuChInChSetXXCh             (3) */
        if( nuBits4SpkrMaskXXCh > 6 )
        {
            xxch_mask = dts_bits_get( bits, nuBits4SpkrMaskXXCh - 6, bits_pos ) << 6;               /* nuXXChSpkrLayoutMask        (nuBits4SpkrMaskXXCh - 6) */
            *channel_layout |= dts_get_channel_layout_from_xxch_mask( xxch_mask );
            *xxch_lower_planes |= (xxch_mask >> 25) & 0x7;
        }
#if 0   /* FIXME: Can we detect stereo downmixing from only XXCH data within the core substream? */
        if( dts_bits_get( bits, 1, bits_pos ) )                                                     /* bDownMixCoeffCodeEmbedded   (1) */
        {
            int bDownMixEmbedded = dts_bits_get( bits, 1, bits_pos );                               /* bDownMixEmbedded            (1) */
            dts_bits_get( bits, 6, bits_pos );                                                      /* nDmixScaleFactor            (6) */
            uint32_t DownMixChMapMask[8];
            for( int nCh = 0; nCh < nuChInChSetXXCh; nCh++ )
                DownMixChMapMask[nCh] = dts_bits_get( bits, nuBits4SpkrMaskXXCh, bits_pos );
        }
#endif
        dts_bits_get( bits, nuXXChChSetHeaderSize * 8 - (*bits_pos - xxch_pos), bits_pos );     /* Skip remaining part of XXCH Channel Set Header. */
    }
    info->flags |= extension ? DTS_EXT_SUBSTREAM_XXCH_FLAG : DTS_CORE_SUBSTREAM_XXCH_FLAG;
    return bits->bs->error ? -1 : 0;
}

static int dts_parse_core_x96( dts_info_t *info, uint64_t *bits_pos )
{
    lsmash_bits_t *bits = info->bits;
    /* DTS_BCCORE_X96 Frame Header */
                                            /* SYNCX96 (32) */
    if( info->core.extension_audio_descriptor != 2 && info->core.extension_audio_descriptor != 3 )
        return 0;   /* Probably, encountered four emulation bytes (pseudo sync word). */
    dts_bits_get( bits, 16, bits_pos );     /* FSIZE96 (12)
                                             * REVNO   (4) */
    info->core.sampling_frequency *= 2;
    info->core.frame_duration     *= 2;
    info->flags |= DTS_CORE_SUBSTREAM_X96_FLAG;
    return bits->bs->error ? -1 : 0;
}

static int dts_parse_core_xch( dts_info_t *info, uint64_t *bits_pos )
{
    lsmash_bits_t *bits = info->bits;
    /* XCH Frame Header */
                                                                                /* XChSYNC  (32) */
    uint64_t XChFSIZE = (lsmash_bs_show_byte( bits->bs, 0 ) << 2)
                      | ((lsmash_bs_show_byte( bits->bs, 1 ) >> 6) & 0x03);     /* XChFSIZE (10) */
    if( (*bits_pos - 32 + (XChFSIZE + 1) * 8) != info->frame_size * 8 )
        return 0;       /* Encountered four emulation bytes (pseudo sync word). */
    if( info->core.extension_audio_descriptor != 0 && info->core.extension_audio_descriptor != 3 )
        return -1;
    dts_bits_get( bits, 10, bits_pos );
    if( dts_bits_get( bits, 4, bits_pos ) != 1 )                                /* AMODE    (4) */
        return -1;      /* At present, only centre surround channel extension is defined. */
    dts_bits_get( bits, 2, bits_pos );      /* for bytes align */
    info->core.channel_layout |= DTS_CHANNEL_LAYOUT_CS;
    info->flags |= DTS_CORE_SUBSTREAM_XCH_FLAG;
    return bits->bs->error ? -1 : 0;
}

static int dts_parse_exsub_xbr( dts_info_t *info, uint64_t *bits_pos )
{
    lsmash_bits_t *bits = info->bits;
    /* XBR Frame Header */
    uint64_t xbr_pos = *bits_pos - 32;                                      /* SYNCXBR        (32) */
    uint64_t nHeaderSizeXBR = dts_bits_get( bits, 6, bits_pos ) + 1;        /* nHeaderSizeXBR (6) */
    dts_bits_get( bits, nHeaderSizeXBR * 8 - (*bits_pos - xbr_pos), bits_pos );     /* Skip the remaining bits in XBR Frame Header. */
    info->flags |= DTS_EXT_SUBSTREAM_XBR_FLAG;
    return bits->bs->error ? -1 : 0;
}

static int dts_parse_exsub_x96( dts_info_t *info, uint64_t *bits_pos )
{
    lsmash_bits_t *bits = info->bits;
    /* DTS_EXSUB_STREAM_X96 Frame Header */
    uint64_t x96_pos = *bits_pos - 32;                                      /* SYNCX96        (32) */
    uint64_t nHeaderSizeX96 = dts_bits_get( bits, 6, bits_pos ) + 1;        /* nHeaderSizeXBR (6) */
    dts_bits_get( bits, nHeaderSizeX96 * 8 - (*bits_pos - x96_pos), bits_pos );     /* Skip the remaining bits in DTS_EXSUB_STREAM_X96 Frame Header. */
    /* What the fuck! The specification drops 'if' sentence.
     * We assume the same behaviour for core substream. */
    info->core.sampling_frequency *= 2;
    info->core.frame_duration     *= 2;
    info->flags |= DTS_EXT_SUBSTREAM_X96_FLAG;
    return bits->bs->error ? -1 : 0;
}

static int dts_parse_exsub_lbr( dts_info_t *info, uint64_t *bits_pos )
{
    lsmash_bits_t *bits = info->bits;
    int ucFmtInfoCode = dts_bits_get( bits, 8, bits_pos );
    if( ucFmtInfoCode == 2 )
    {
        /* LBR decoder initialization data */
        int nLBRSampleRateCode  = dts_bits_get( bits, 8, bits_pos );    /* nLBRSampleRateCode      (8) */
        int usLBRSpkrMask       = dts_bits_get( bits, 16, bits_pos );   /* usLBRSpkrMask           (16) */
        dts_bits_get( bits, 16, bits_pos );                             /* nLBRversion             (16) */
        int nLBRCompressedFlags = dts_bits_get( bits, 8, bits_pos );    /* nLBRCompressedFlags     (8) */
        dts_bits_get( bits, 40, bits_pos );                             /* nLBRBitRateMSnybbles    (8)
                                                                         * nLBROriginalBitRate_LSW (16)
                                                                         * nLBRScaledBitRate_LSW   (16) */
        static const uint32_t source_sample_rate_table[16] =
            {
                 8000, 16000, 32000, 0, 0,
                11025, 22050, 44100, 0, 0,
                12000, 24000, 48000, 0, 0, 0
            };
        info->lbr.sampling_frequency = source_sample_rate_table[nLBRSampleRateCode];
        if( info->lbr.sampling_frequency < 16000 )
            info->lbr.frame_duration = 1024;
        else if( info->lbr.sampling_frequency < 32000 )
            info->lbr.frame_duration = 2048;
        else
            info->lbr.frame_duration = 4096;
        info->lbr.channel_layout     = ((usLBRSpkrMask >> 8) & 0xff) | ((usLBRSpkrMask << 8) & 0xff00);     /* usLBRSpkrMask is little-endian. */
        info->lbr.stereo_downmix    |= !!(nLBRCompressedFlags & 0x20);
        info->lbr.lfe_present       |= !!(nLBRCompressedFlags & 0x02);
        info->lbr.duration_modifier |= (nLBRCompressedFlags & 0x04) || (nLBRCompressedFlags & 0x0C);
        info->lbr.sample_size        = nLBRCompressedFlags & 0x01 ? 24 : 16;
    }
    else if( ucFmtInfoCode != 1 )
        return -1;      /* unknown */
    info->flags |= DTS_EXT_SUBSTREAM_LBR_FLAG;
    return bits->bs->error ? -1 : 0;
}

static int dts_parse_exsub_xll( dts_info_t *info, uint64_t *bits_pos )
{
    lsmash_bits_t *bits = info->bits;
    /* Common Header */
    uint64_t xll_pos = *bits_pos - 32;                                                          /* SYNCXLL                        (32) */
    dts_bits_get( bits, 4, bits_pos );                                                          /* nVersion                       (4) */
    uint64_t nHeaderSize       = dts_bits_get( bits, 8, bits_pos ) + 1;                         /* nHeaderSize                    (8) */
    int      nBits4FrameFsize  = dts_bits_get( bits, 5, bits_pos ) + 1;                         /* nBits4FrameFsize               (5) */
    dts_bits_get( bits, nBits4FrameFsize, bits_pos );                                           /* nLLFrameSize                   (nBits4FrameFsize) */
    int      nNumChSetsInFrame = dts_bits_get( bits, 4, bits_pos ) + 1;                         /* nNumChSetsInFrame              (4) */
    uint16_t nSegmentsInFrame  = 1 << dts_bits_get( bits, 4, bits_pos );                        /* nSegmentsInFrame               (4) */
    uint16_t nSmplInSeg        = 1 << dts_bits_get( bits, 4, bits_pos );                        /* nSmplInSeg                     (4) */
    dts_bits_get( bits, 5, bits_pos );                                                          /* nBits4SSize                    (5) */
    dts_bits_get( bits, 3, bits_pos );                                                          /* nBandDataCRCEn                 (2)
                                                                                                 * bScalableLSBs                  (1) */
    int nBits4ChMask = dts_bits_get( bits, 5, bits_pos ) + 1;                                   /* nBits4ChMask                   (5) */
    dts_bits_get( bits, nHeaderSize * 8 - (*bits_pos - xll_pos), bits_pos );    /* Skip the remaining bits in Common Header. */
    int sum_nChSetLLChannel = 0;
    uint32_t nFs1 = 0;
    int nNumFreqBands1 = 0;
    for( int nChSet = 0; nChSet < nNumChSetsInFrame; nChSet++ )
    {
        /* Channel Set Sub-Header */
        xll_pos = *bits_pos;
        uint64_t nChSetHeaderSize = dts_bits_get( bits, 10, bits_pos ) + 1;                     /* nChSetHeaderSize               (10) */
        int nChSetLLChannel = dts_bits_get( bits, 4, bits_pos ) + 1;                            /* nChSetLLChannel                (4) */
        dts_bits_get( bits, nChSetLLChannel + 5, bits_pos );                                    /* nResidualChEncode              (nChSetLLChannel)
                                                                                                 * nBitResolution                 (5) */
        int nBitWidth = dts_bits_get( bits, 5, bits_pos ) < 16 ? 16 : 24;                       /* nBitWidth                      (5) */
        info->lossless.bit_width = LSMASH_MAX( info->lossless.bit_width, nBitWidth );
        static const uint32_t source_sample_rate_table[16] =
            {
                 8000, 16000, 32000, 64000, 128000,
                       22050, 44100, 88200, 176400, 352800,
                12000, 24000, 48000, 96000, 192000, 384000
            };
        int sFreqIndex = dts_bits_get( bits, 4, bits_pos );                                     /* sFreqIndex                     (4) */
        uint32_t nFs = source_sample_rate_table[sFreqIndex];
        dts_bits_get( bits, 2, bits_pos );                                                      /* nFsInterpolate                 (2) */
        int nReplacementSet = dts_bits_get( bits, 2, bits_pos );                                /* nReplacementSet                (2) */
        if( nReplacementSet > 0 )
            dts_bits_get( bits, 1, bits_pos );                                                  /* bActiveReplaceSet              (1) */
        info->lossless.channel_layout = 0;
        if( info->extension.bOne2OneMapChannels2Speakers )
        {
            int bPrimaryChSet = dts_bits_get( bits, 1, bits_pos );                              /* bPrimaryChSet                  (1) */
            int bDownmixCoeffCodeEmbedded = dts_bits_get( bits, 1, bits_pos );                  /* bDownmixCoeffCodeEmbedded      (1) */
            int nLLDownmixType = 0x7;
            if( bDownmixCoeffCodeEmbedded )
            {
                dts_bits_get( bits, 1, bits_pos );                                              /* bDownmixEmbedded               (1) */
                if( bPrimaryChSet )
                    nLLDownmixType = dts_bits_get( bits, 3, bits_pos );                         /* nLLDownmixType                 (3) */
            }
            dts_bits_get( bits, 1, bits_pos );                                                  /* bHierChSet                     (1) */
            if( bDownmixCoeffCodeEmbedded )
            {
                static const int downmix_channel_count_table[8] = { 1, 2, 2, 3, 3, 4, 4, 0 };
                int N = nChSetLLChannel + 1;
                int M = bPrimaryChSet ? downmix_channel_count_table[nLLDownmixType] : sum_nChSetLLChannel;
                int nDownmixCoeffs = N * M;
                dts_bits_get( bits, nDownmixCoeffs, bits_pos );                                 /* DownmixCoeffs                  (nDownmixCoeffs * 9) */
            }
            sum_nChSetLLChannel += nChSetLLChannel;
            if( dts_bits_get( bits, 1, bits_pos ) )                                             /* bChMaskEnabled                 (1) */
                info->lossless.channel_layout |= dts_bits_get( bits, nBits4ChMask, bits_pos );  /* nSpkrMask[nSpkrConf]           (nBits4ChMask) */
        }
        else
        {
            if( dts_bits_get( bits, 1, bits_pos ) )                                             /* bMappingCoeffsPresent          (1) */
            {
                int nBitsCh2SpkrCoef = 6 + 2 * dts_bits_get( bits, 3, bits_pos );               /* nBitsCh2SpkrCoef               (3) */
                int nNumSpeakerConfigs = dts_bits_get( bits, 2, bits_pos ) + 1;                 /* nNumSpeakerConfigs             (2) */
                for( int nSpkrConf = 0; nSpkrConf < nNumSpeakerConfigs; nSpkrConf++ )
                {
                    int pnActiveChannelMask = dts_bits_get( bits, nChSetLLChannel, bits_pos );  /* pnActiveChannelMask[nSpkrConf] (nChSetLLChannel) */
                    int pnNumSpeakers = dts_bits_get( bits, 6, bits_pos ) + 1;                  /* pnNumSpeakers[nSpkrConf]       (6) */
                    int bSpkrMaskEnabled = dts_bits_get( bits, 1, bits_pos );                   /* bSpkrMaskEnabled               (1) */
                    if( bSpkrMaskEnabled )
                        info->lossless.channel_layout |= dts_bits_get( bits, nBits4ChMask, bits_pos );  /* nSpkrMask[nSpkrConf]   (nBits4ChMask) */
                    for( int nSpkr = 0; nSpkr < pnNumSpeakers; nSpkr++ )
                    {
                        if( !bSpkrMaskEnabled )
                            dts_bits_get( bits, 25, bits_pos );                                 /* ChSetSpeakerConfiguration      (25) */
                        for( int nCh = 0; nCh < nChSetLLChannel; nCh++ )
                            if( pnActiveChannelMask & (1 << nCh) )
                                dts_bits_get( bits, nBitsCh2SpkrCoef, bits_pos );               /* pnCh2SpkrMapCoeff              (nBitsCh2SpkrCoef) */
                    }
                }
            }
        }
        int nNumFreqBands;
        if( nFs > 96000 )
        {
            if( dts_bits_get( bits, 1, bits_pos ) )                                             /* bXtraFreqBands                 (1) */
                nNumFreqBands = nFs > 192000 ? 4 : 2;
            else
                nNumFreqBands = nFs > 192000 ? 2 : 1;
        }
        else
            nNumFreqBands = 1;
        uint32_t nSmplInSeg_nChSet;
        if( nChSet == 0 )
        {
            nFs1              = nFs;
            nNumFreqBands1    = nNumFreqBands;
            nSmplInSeg_nChSet = nSmplInSeg;
        }
        else
            nSmplInSeg_nChSet = (nSmplInSeg * (nFs * nNumFreqBands1)) / (nFs1 * nNumFreqBands);
        if( info->lossless.sampling_frequency < nFs )
        {
            info->lossless.sampling_frequency = nFs;
            uint32_t samples_per_band_in_frame = nSegmentsInFrame * nSmplInSeg_nChSet;
            info->lossless.frame_duration = samples_per_band_in_frame * nNumFreqBands;
        }
        dts_bits_get( bits, nChSetHeaderSize * 8 - (*bits_pos - xll_pos), bits_pos );   /* Skip the remaining bits in Channel Set Sub-Header. */
    }
    info->flags |= DTS_EXT_SUBSTREAM_XLL_FLAG;
    return bits->bs->error ? -1 : 0;
}

static uint16_t dts_generate_channel_layout_from_core( int channel_arrangement )
{
    static const uint16_t channel_layout_map_table[] =
        {
            DTS_CHANNEL_LAYOUT_C,
            DTS_CHANNEL_LAYOUT_L_R,     /* dual mono */
            DTS_CHANNEL_LAYOUT_L_R,     /* stereo */
            DTS_CHANNEL_LAYOUT_L_R,     /* sum-difference */
            DTS_CHANNEL_LAYOUT_L_R,     /* Lt/Rt */
            DTS_CHANNEL_LAYOUT_C     | DTS_CHANNEL_LAYOUT_L_R,
            DTS_CHANNEL_LAYOUT_L_R   | DTS_CHANNEL_LAYOUT_CS,
            DTS_CHANNEL_LAYOUT_C     | DTS_CHANNEL_LAYOUT_L_R   | DTS_CHANNEL_LAYOUT_CS,
            DTS_CHANNEL_LAYOUT_L_R   | DTS_CHANNEL_LAYOUT_LS_RS,
            DTS_CHANNEL_LAYOUT_C     | DTS_CHANNEL_LAYOUT_L_R   | DTS_CHANNEL_LAYOUT_LS_RS,
            DTS_CHANNEL_LAYOUT_LC_RC | DTS_CHANNEL_LAYOUT_L_R   | DTS_CHANNEL_LAYOUT_LS_RS,
            DTS_CHANNEL_LAYOUT_C     | DTS_CHANNEL_LAYOUT_L_R   | DTS_CHANNEL_LAYOUT_LSR_RSR | DTS_CHANNEL_LAYOUT_OH,
            DTS_CHANNEL_LAYOUT_C     | DTS_CHANNEL_LAYOUT_CS    | DTS_CHANNEL_LAYOUT_L_R     | DTS_CHANNEL_LAYOUT_LSR_RSR,
            DTS_CHANNEL_LAYOUT_C     | DTS_CHANNEL_LAYOUT_L_R   | DTS_CHANNEL_LAYOUT_LC_RC   | DTS_CHANNEL_LAYOUT_LS_RS,
            DTS_CHANNEL_LAYOUT_L_R   | DTS_CHANNEL_LAYOUT_LC_RC | DTS_CHANNEL_LAYOUT_LS_RS   | DTS_CHANNEL_LAYOUT_LSR_RSR,
            DTS_CHANNEL_LAYOUT_C     | DTS_CHANNEL_LAYOUT_CS    | DTS_CHANNEL_LAYOUT_L_R     | DTS_CHANNEL_LAYOUT_LC_RC | DTS_CHANNEL_LAYOUT_LS_RS
        };
    return channel_arrangement < 16 ? channel_layout_map_table[channel_arrangement] : 0;
}

int dts_parse_core_substream( dts_info_t *info, uint8_t *data, uint32_t data_length )
{
    lsmash_bits_t *bits = info->bits;
    if( lsmash_bits_import_data( info->bits, data, data_length ) )
        return -1;
    uint64_t bits_pos = 0;
    dts_bits_get( bits, 32, &bits_pos );                                        /* SYNC            (32) */
    int frame_type = dts_bits_get( bits, 1, &bits_pos );                        /* FTYPE           (1) */
    int deficit_sample_count = dts_bits_get( bits, 5, &bits_pos );              /* SHORT           (5) */
    if( frame_type == 1 && deficit_sample_count != 31 )
        goto parse_fail;    /* A normal frame (FTYPE == 1) must have SHORT == 31. */
    int crc_present_flag = dts_bits_get( bits, 1, &bits_pos );                  /* CPF             (1) */
    int num_of_pcm_sample_blocks = dts_bits_get( bits, 7, &bits_pos ) + 1;      /* NBLKS           (7) */
    if( num_of_pcm_sample_blocks <= 5 )
        goto parse_fail;
    info->core.frame_duration = 32 * num_of_pcm_sample_blocks;
    info->core.frame_size = dts_bits_get( bits, 14, &bits_pos );                /* FSIZE           (14) */
    info->frame_size = info->core.frame_size + 1;
    if( info->frame_size < DTS_MIN_CORE_SIZE )
        goto parse_fail;
    info->core.channel_arrangement = dts_bits_get( bits, 6, &bits_pos );        /* AMODE           (6) */
    info->core.channel_layout = dts_generate_channel_layout_from_core( info->core.channel_arrangement );
    int core_audio_sampling_frequency = dts_bits_get( bits, 4, &bits_pos );     /* SFREQ           (4) */
    static const uint32_t sampling_frequency_table[16] =
        {
                0,
             8000, 16000, 32000, 0, 0,
            11025, 22050, 44100, 0, 0,
            12000, 24000, 48000, 0, 0
        };
    info->core.sampling_frequency = sampling_frequency_table[core_audio_sampling_frequency];
    if( info->core.sampling_frequency == 0 )
        goto parse_fail;    /* invalid */
    dts_bits_get( bits, 10, &bits_pos );                                        /* Skip remainder 10 bits.
                                                                                 * RATE            (5)
                                                                                 * MIX             (1)
                                                                                 * DYNF            (1)
                                                                                 * TIMEF           (1)
                                                                                 * AUXF            (1)
                                                                                 * HDCD            (1) */
    info->core.extension_audio_descriptor = dts_bits_get( bits, 3, &bits_pos ); /* EXT_AUDIO_ID    (3)
                                                                                 * Note: EXT_AUDIO_ID == 3 is defined in V1.2.1.
                                                                                 * However, its definition disappears and is reserved in V1.3.1. */
    int extended_coding_flag = dts_bits_get( bits, 1, &bits_pos );              /* EXT_AUDIO       (1) */
    dts_bits_get( bits, 1, &bits_pos );                                         /* ASPF            (1) */
    int low_frequency_effects_flag = dts_bits_get( bits, 2, &bits_pos );        /* LFF             (2) */
    if( low_frequency_effects_flag == 0x3 )
        goto parse_fail;    /* invalid */
    if( low_frequency_effects_flag )
        info->core.channel_layout |= DTS_CHANNEL_LAYOUT_LFE1;
    dts_bits_get( bits, 8 + crc_present_flag * 16, &bits_pos );                 /* HFLAG           (1)
                                                                                 * HCRC            (16)
                                                                                 * FILTS           (1)
                                                                                 * VERNUM          (4)
                                                                                 * CHIST           (2) */
    int PCMR = dts_bits_get( bits, 3, &bits_pos );                              /* PCMR            (3) */
    static const uint8_t source_resolution_table[8] = { 16, 16, 20, 20, 0, 24, 24, 0 };
    info->core.pcm_resolution = source_resolution_table[PCMR];
    if( info->core.pcm_resolution == 0 )
        goto parse_fail;    /* invalid */
    dts_bits_get( bits, 6, &bits_pos );                                         /* SUMF            (1)
                                                                                 * SUMS            (1)
                                                                                 * DIALNORM/UNSPEC (4) */
    if( extended_coding_flag )
    {
        uint32_t syncword = dts_bits_get( bits, 24, &bits_pos );
        uint64_t frame_size_bits = info->frame_size * 8;
        while( (bits_pos + 24) < frame_size_bits )
        {
            syncword = ((syncword << 8) & 0xffffff00) | dts_bits_get( bits, 8, &bits_pos );
            switch( syncword )
            {
                case DTS_SYNCWORD_XXCH :
                    if( dts_parse_xxch( info, &bits_pos, 0 ) )
                        goto parse_fail;
                    syncword = dts_bits_get( bits, 24, &bits_pos );
                    break;
                case DTS_SYNCWORD_X96K :
                    if( dts_parse_core_x96( info, &bits_pos ) )
                        goto parse_fail;
                    syncword = dts_bits_get( bits, 24, &bits_pos );
                    break;
                case DTS_SYNCWORD_XCH :
                    if( dts_parse_core_xch( info, &bits_pos ) )
                        goto parse_fail;
                    break;
                default :
                    continue;
            }
        }
    }
    info->flags |= DTS_CORE_SUBSTREAM_CORE_FLAG;
    info->extension_substream_count = 0;
    lsmash_bits_empty( bits );
    return 0;
parse_fail:
    lsmash_bits_empty( bits );
    return -1;
}

int dts_parse_extension_substream( dts_info_t *info, uint8_t *data, uint32_t data_length )
{
    lsmash_bits_t *bits = info->bits;
    if( lsmash_bits_import_data( info->bits, data, data_length ) )
        return -1;
    uint64_t bits_pos = 0;
    dts_bits_get( bits, 40, &bits_pos );                                                    /* SYNCEXTSSH                    (32)
                                                                                             * UserDefinedBits               (8) */
    int nExtSSIndex = info->extension_index = dts_bits_get( bits, 2, &bits_pos );           /* nExtSSIndex                   (2) */
    int bHeaderSizeType = dts_bits_get( bits, 1, &bits_pos );                               /* bHeaderSizeType               (1) */
    int nuBits4Header    =  8 + bHeaderSizeType * 4;
    int nuBits4ExSSFsize = 16 + bHeaderSizeType * 4;
    uint32_t nuExtSSHeaderSize = dts_bits_get( bits, nuBits4Header, &bits_pos ) + 1;        /* nuExtSSHeaderSize             (8 or 12) */
    info->frame_size = dts_bits_get( bits, nuBits4ExSSFsize, &bits_pos ) + 1;               /* nuExtSSFsize                  (16 or 20) */
    if( info->frame_size < 10 )
        return -1;
    int nuNumAssets;
    info->extension.bStaticFieldsPresent = dts_bits_get( bits, 1, &bits_pos );              /* bStaticFieldsPresent          (1) */
    if( info->extension.bStaticFieldsPresent )
    {
        dts_bits_get( bits, 2, &bits_pos );                                                 /* nuRefClockCode                (2) */
        info->extension.frame_duration = 512 * (dts_bits_get( bits, 3, &bits_pos ) + 1);    /* nuExSSFrameDurationCode       (3) */
        if( dts_bits_get( bits, 1, &bits_pos ) )                                            /* bTimeStampFlag                (1) */
            dts_bits_get( bits, 36, &bits_pos );                                            /* nuTimeStamp                   (32)
                                                                                             * nLSB                          (4) */
        int nuNumAudioPresnt = dts_bits_get( bits, 3, &bits_pos ) + 1;                      /* nuNumAudioPresnt              (3) */
        nuNumAssets = dts_bits_get( bits, 3, &bits_pos ) + 1;                               /* nuNumAssets                   (3) */
        int nuActiveExSSMask[nuNumAudioPresnt];
        for( int nAuPr = 0; nAuPr < nuNumAudioPresnt; nAuPr++ )
            nuActiveExSSMask[nAuPr] = dts_bits_get( bits, nExtSSIndex + 1, &bits_pos );     /* nuActiveExSSMask[nAuPr]       (nExtSSIndex + 1) */
        int nuActiveAssetMask[nuNumAudioPresnt][nExtSSIndex + 1];
        for( int nAuPr = 0; nAuPr < nuNumAudioPresnt; nAuPr++ )
            for( int nSS = 0; nSS < nExtSSIndex + 1; nSS++ )
                if( ((nuActiveExSSMask[nAuPr] >> nSS) & 0x1) == 1 )
                    nuActiveAssetMask[nAuPr][nSS] = dts_bits_get( bits, 8, &bits_pos );     /* nuActiveAssetMask[nAuPr][nSS] (8) */
                else
                    nuActiveAssetMask[nAuPr][nSS] = 0;
        info->extension.bMixMetadataEnbl = dts_bits_get( bits, 1, &bits_pos );              /* bMixMetadataEnbl              (1) */
        if( info->extension.bMixMetadataEnbl )
        {
            dts_bits_get( bits, 2, &bits_pos );                                             /* nuMixMetadataAdjLevel         (2) */
            int nuBits4MixOutMask = (dts_bits_get( bits, 2, &bits_pos ) + 1) << 2;          /* nuBits4MixOutMask             (2) */
            info->extension.nuNumMixOutConfigs = dts_bits_get( bits, 2, &bits_pos ) + 1;    /* nuNumMixOutConfigs            (2) */
            for( int ns = 0; ns < info->extension.nuNumMixOutConfigs; ns++ )
            {
                int nuMixOutChMask = dts_bits_get( bits, nuBits4MixOutMask, &bits_pos );    /* nuMixOutChMask[ns]            (nuBits4MixOutMask) */
                info->extension.nNumMixOutCh[ns] = dts_get_channel_count_from_channel_layout( nuMixOutChMask );
            }
        }
    }
    else
    {
        nuNumAssets = 1;
        info->extension.bMixMetadataEnbl   = 0;
        info->extension.nuNumMixOutConfigs = 0;
    }
    info->extension.number_of_assets = nuNumAssets;
    uint32_t nuAssetFsize[8];
    for( int nAst = 0; nAst < nuNumAssets; nAst++ )
        nuAssetFsize[nAst] = dts_bits_get( bits, nuBits4ExSSFsize, &bits_pos ) + 1;         /* nuAssetFsize[nAst]            (nuBits4ExSSFsize) */
    for( int nAst = 0; nAst < nuNumAssets; nAst++ )
        if( dts_parse_asset_descriptor( info, &bits_pos ) )
            goto parse_fail;
    dts_bits_get( bits, nuExtSSHeaderSize * 8 - bits_pos, &bits_pos );
    uint32_t syncword = dts_bits_get( bits, 24, &bits_pos );
    uint64_t frame_size_bits = info->frame_size * 8;
    while( (bits_pos + 24) < frame_size_bits )
    {
        syncword = ((syncword << 8) & 0xffffff00) | dts_bits_get( bits, 8, &bits_pos );
        switch( syncword )
        {
            case DTS_SYNCWORD_XBR :
                if( dts_parse_exsub_xbr( info, &bits_pos ) )
                    goto parse_fail;
                break;
            case DTS_SYNCWORD_XXCH :
                if( dts_parse_xxch( info, &bits_pos, 1 ) )
                    goto parse_fail;
                break;
            case DTS_SYNCWORD_X96K :
                if( dts_parse_exsub_x96( info, &bits_pos ) )
                    goto parse_fail;
                break;
            case DTS_SYNCWORD_LBR :
                if( dts_parse_exsub_lbr( info, &bits_pos ) )
                    goto parse_fail;
                break;
            case DTS_SYNCWORD_XLL :
                if( dts_parse_exsub_xll( info, &bits_pos ) )
                    goto parse_fail;
                break;
            default :
                continue;
        }
        syncword = dts_bits_get( bits, 24, &bits_pos );
    }
    ++ info->extension_substream_count;
    lsmash_bits_empty( bits );
    return 0;
parse_fail:
    lsmash_bits_empty( bits );
    return -1;
}

dts_substream_type dts_get_substream_type( dts_info_t *info )
{
    if( info->buffer_end - info->buffer_pos < 4 )
        return DTS_SUBSTREAM_TYPE_NONE;
    uint8_t *buffer = info->buffer_pos;
    uint32_t syncword = LSMASH_4CC( buffer[0], buffer[1], buffer[2], buffer[3] );
    switch( syncword )
    {
        case DTS_SYNCWORD_CORE :
            return DTS_SUBSTREAM_TYPE_CORE;
        case DTS_SYNCWORD_SUBSTREAM :
            return DTS_SUBSTREAM_TYPE_EXTENSION;
        default :
            return DTS_SUBSTREAM_TYPE_NONE;
    }
}

int dts_get_extension_index( dts_info_t *info, uint8_t *extension_index )
{
    if( info->buffer_end - info->buffer_pos < 6 )
        return -1;
    *extension_index = info->buffer_pos[5] >> 6;
    return 0;
}

void dts_update_specific_param( dts_info_t *info )
{
    lsmash_dts_specific_parameters_t *param = &info->ddts_param;
    /* DTSSamplingFrequency and FrameDuration */
    if( info->flags & DTS_CORE_SUBSTREAM_CORE_FLAG )
    {
        param->DTSSamplingFrequency = info->core.sampling_frequency;
        info->frame_duration        = info->core.frame_duration;
    }
    else
    {
        param->DTSSamplingFrequency = info->extension.sampling_frequency;
        info->frame_duration        = info->extension.frame_duration;
    }
    if( param->DTSSamplingFrequency <= info->lbr.sampling_frequency )
    {
        param->DTSSamplingFrequency = info->lbr.sampling_frequency;
        info->frame_duration        = info->lbr.frame_duration;
    }
    if( param->DTSSamplingFrequency <= info->lossless.sampling_frequency )
    {
        param->DTSSamplingFrequency = info->lossless.sampling_frequency;
        info->frame_duration        = info->lossless.frame_duration;
    }
    param->FrameDuration = 0;
    for( uint32_t frame_duration = info->frame_duration >> 10; frame_duration; frame_duration >>= 1 )
        ++ param->FrameDuration;
    /* pcmSampleDepth */
    param->pcmSampleDepth = info->core.pcm_resolution;
    param->pcmSampleDepth = LSMASH_MAX( param->pcmSampleDepth, info->extension.bit_resolution );
    param->pcmSampleDepth = LSMASH_MAX( param->pcmSampleDepth, info->lbr.sample_size );
    param->pcmSampleDepth = LSMASH_MAX( param->pcmSampleDepth, info->lossless.bit_width );
    param->pcmSampleDepth = param->pcmSampleDepth > 16 ? 24 : 16;
    /* StreamConstruction */
    param->StreamConstruction = lsmash_dts_get_stream_construction( info->flags );
    /* CoreLFEPresent */
    param->CoreLFEPresent = !!(info->core.channel_layout & DTS_CHANNEL_LAYOUT_LFE1);
    /* CoreLayout */
    if( param->StreamConstruction == 0 || param->StreamConstruction >= 19 )
        param->CoreLayout = 31;         /* Use ChannelLayout. */
    else
    {
        if( info->core.channel_arrangement != 1
         && info->core.channel_arrangement != 3
         && info->core.channel_arrangement <= 9 )
            param->CoreLayout = info->core.channel_arrangement;
        else
            param->CoreLayout = 31;     /* Use ChannelLayout. */
    }
    /* CoreSize
     * The specification says this field is the size of a core substream AU in bytes.
     * If we don't assume CoreSize is the copy of FSIZE, when FSIZE equals 0x3FFF, this field overflows and becomes 0. */
    param->CoreSize = LSMASH_MIN( info->core.frame_size, 0x3FFF );
    /* StereoDownmix */
    param->StereoDownmix = info->extension.stereo_downmix | info->lbr.stereo_downmix;
    /* RepresentationType */
    param->RepresentationType = info->extension.representation_type;
    /* ChannelLayout */
    param->ChannelLayout = info->core.channel_layout
                         | info->extension.channel_layout
                         | info->lbr.channel_layout
                         | info->lossless.channel_layout;
    /* MultiAssetFlag
     * When multiple assets exist, the remaining parameters in the DTSSpecificBox only reflect the coding parameters of the first asset. */
    param->MultiAssetFlag = 1 < info->extension.number_of_assets;
    /* LBRDurationMod */
    param->LBRDurationMod = param->MultiAssetFlag
                          ? info->lbr.duration_modifier && !(info->flags & DTS_CORE_SUBSTREAM_CORE_FLAG)
                          : info->lbr.duration_modifier;
    info->ddts_param_initialized = 1;
}
