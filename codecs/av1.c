/*****************************************************************************
 * av1.c
 *****************************************************************************
 * Copyright (C) 2017-2020 L-SMASH project
 * Copyright (c) 2026 Yusuke Nakamura
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *          Derek Buitenhuis <derek.buitenhuis@gmail.com>
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

#include "common/internal.h" /* must be placed first */

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "core/box.h"

/*********************************************************************************
    Alliance for Open Media AV1

    References:
        AV1 Bitstream & Decoding Process Specification
            Version 1.0.0 with Errata 1
        AV1 Codec ISO Media File Format Binding
            v1.2.0, 12 December 2019
**********************************************************************************/
#include "av1.h"

#define IF_OUT_OF_SPEC( x ) if( x )

enum
{
    CP_BT_709      = ISOM_PRIMARIES_INDEX_ITU_R709_5,
    CP_UNSPECIFIED = ISOM_PRIMARIES_INDEX_UNSPECIFIED,

    TC_UNSPECIFIED = ISOM_TRANSFER_INDEX_UNSPECIFIED,
    TC_SRGB        = ISOM_TRANSFER_INDEX_SRGB,

    MC_IDENTITY    = ISOM_MATRIX_INDEX_NO_MATRIX,
    MC_UNSPECIFIED = ISOM_MATRIX_INDEX_UNSPECIFIED,

    BufferPoolMaxSize = 10,

    SELECT_SCREEN_CONTENT_TOOLS = 2,
    SELECT_INTEGER_MV           = 2,
    SUPERRES_NUM                = 8,
    SUPERRES_DENOM_MIN          = 9,
    SUPERRES_DENOM_BITS         = 3,
    PRIMARY_REF_NONE            = 7,
    SWITCHABLE                  = 4,

    /* RefFrame[*] */
    NONE          = -1,
    INTRA_FRAME   = 0,
    LAST_FRAME    = 1,
    LAST2_FRAME   = 2,
    LAST3_FRAME   = 3,
    GOLDEN_FRAME  = 4,
    BWDREF_FRAME  = 5,
    ALTREF2_FRAME = 6,
    ALTREF_FRAME  = 7,
};

uint32_t lsm_av1_get_uvlc
(
    lsmash_bits_t *bits
)
{
    if( !bits || !bits->bs )
        return UINT32_MAX;
    unsigned int leadingZeros = 0;
    while( !bits->bs->eob && !bits->bs->error )
    {
        if( lsmash_bits_get( bits, 1 ) )
            break;
        ++leadingZeros;
        if( leadingZeros >= 32 )
        {
            while( !bits->bs->eob && !bits->bs->error && !lsmash_bits_get( bits, 1 ) )
                ;
            return UINT32_MAX;
        }
    }
    if( bits->bs->eob || bits->bs->error || leadingZeros >= 32 )
        return UINT32_MAX;
    if( leadingZeros == 0 )
        return 0;
    uint32_t value = (uint32_t)lsmash_bits_get( bits, leadingZeros );
    if( bits->bs->eob || bits->bs->error )
        return UINT32_MAX;
    /* The max valid value is 0xFFFFFFFE i.e. (UINT32_MAX - 1). */
    return value + ((1u << leadingZeros) - 1);
}

uint64_t lsm_av1_get_leb128
(
    lsmash_bs_t *bs,
    uint8_t     *Leb128Bytes
)
{
    assert( Leb128Bytes );
    *Leb128Bytes = 0;
    if( !bs || bs->eob || bs->error )
        return UINT64_MAX;
    uint64_t value = 0;
    for( int i = 0; i < 8; i++ )
    {
        /* The length of value of leb128() is split into 7-bit groups. */
        uint8_t leb128_byte = lsmash_bs_get_byte( bs );
        if( bs->eob || bs->error )
        {
            *Leb128Bytes = 0;
            return UINT64_MAX;
        }
        value |= ((uint64_t)(leb128_byte & 0x7F) << (i * 7));
        ++(*Leb128Bytes);
        if( (leb128_byte & 0x80) == 0 )
            break;
        if( i == 7 )
        {
            /* The bitstream is not conformant with the spec (> 8 bytes). */
            *Leb128Bytes = 0;
            return UINT64_MAX;
        }
    }
    /* AV1 Spec Section 4.10.5: value returned from leb128 parsing must be <= (1 << 32) - 1. */
    if( value > UINT32_MAX )
    {
        *Leb128Bytes = 0;
        return UINT64_MAX;
    }
    return value;
}

uint64_t lsm_av1_show_leb128
(
    lsmash_bs_t *bs,
    uint32_t     offset,
    uint8_t     *Leb128Bytes
)
{
    assert( Leb128Bytes );
    *Leb128Bytes = 0;
    if( !bs || bs->eob || bs->error )
        return UINT64_MAX;
    uint64_t value = 0;
    for( int i = 0; i < 8; i++ )
    {
        /* The length of value of leb128() is split into 7-bit groups. */
        uint8_t leb128_byte = lsmash_bs_show_byte( bs, offset + i );
        if( bs->error || offset + i >= lsmash_bs_get_remaining_buffer_size( bs ) )
        {
            *Leb128Bytes = 0;
            return UINT64_MAX;
        }
        value |= ((uint64_t)(leb128_byte & 0x7F) << (i * 7));
        ++(*Leb128Bytes);
        if( (leb128_byte & 0x80) == 0 )
            break;
        if( i == 7 )
        {
            /* The bitstream is not conformant with the spec (> 8 bytes). */
            *Leb128Bytes = 0;
            return UINT64_MAX;
        }
    }
    /* AV1 Spec Section 4.10.5: value returned from leb128 parsing must be <= (1 << 32) - 1. */
    if( value > UINT32_MAX )
    {
        *Leb128Bytes = 0;
        return UINT64_MAX;
    }
    return value;
}

int64_t lsm_av1_get_su
(
    lsmash_bits_t *bits,
    unsigned int   n
)
{
    if( n == 0 )
        return 0;
    uint64_t value    = lsmash_bits_get( bits, n );
    uint64_t signMask = ((uint64_t)1) << (n - 1);
    if( value & signMask )
        return (int64_t)(value - 2 * signMask);
    return (int64_t)value;
}

uint64_t lsm_av1_get_ns
(
    lsmash_bits_t *bits,
    unsigned int   n
)
{
    if( n <= 1 )
        return 0;
    uint64_t w = lsmash_floor_log2( n ) + 1;
    uint64_t m = (((uint64_t)1) << w) - n;
    uint64_t v = (w > 1) ? lsmash_bits_get( bits, (uint32_t)(w - 1) ) : 0;
    if( v < m )
        return v;
    uint64_t extra_bit = lsmash_bits_get( bits, 1 );
    return (v << 1) - m + extra_bit;
}

void lsm_av1_cleanup_parser
(
    av1_parser_t *parser
)
{
    if( !parser )
        return;
    lsmash_bits_cleanup( parser->bits );
    parser->bits = NULL;
}

int lsm_av1_setup_parser
(
    av1_parser_t *parser,
    lsmash_bs_t  *bs
)
{
    if( !parser )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_bits_t *bits = lsmash_malloc_zero( sizeof(lsmash_bits_t) );
    if( !bits )
        return LSMASH_ERR_MEMORY_ALLOC;
    if( bs )
        lsmash_bits_init( bits, bs );
    memset( parser, 0, sizeof(av1_parser_t) );
    parser->bits = bits;
    return 0;
}

void lsm_av1_reset_parser
(
    av1_parser_t *parser
)
{
    if( !parser )
        return;
    lsmash_bits_t *bits = parser->bits;
    if( bits )
    {
        bits->store = 0;
        bits->cache = 0;
    }
    memset( parser, 0, sizeof(av1_parser_t) );
    parser->bits = bits;
}

static void av1_parser_color_config
(
    av1_parser_t *parser
)
{
    lsmash_bits_t         *bits = parser->bits;
    av1_sequence_header_t *sh   = &parser->sequence_header;
    av1_color_config_t    *cc   = &sh->color_config;
    cc->high_bitdepth = (uint8_t)lsmash_bits_get( bits, 1 );
    if( sh->seq_profile == 2 && cc->high_bitdepth )
    {
        cc->twelve_bit = (uint8_t)lsmash_bits_get( bits, 1 );
        cc->BitDepth   = cc->twelve_bit ? 12 : 10;
    }
    else
    {
        cc->twelve_bit = 0; /* The spec does not define this! Anyway 'av1C' requires so do this. */
        cc->BitDepth   = cc->high_bitdepth ? 10 : 8;
    }
    cc->mono_chrome = sh->seq_profile == 1 ? 0 : (uint8_t)lsmash_bits_get( bits, 1 );
    cc->NumPlanes   = cc->mono_chrome ? 1 : 3;
    cc->color_description_present_flag = (uint8_t)lsmash_bits_get( bits, 1 );
    if( cc->color_description_present_flag )
    {
        cc->color_primaries          = (uint8_t)lsmash_bits_get( bits, 8 );
        cc->transfer_characteristics = (uint8_t)lsmash_bits_get( bits, 8 );
        cc->matrix_coefficients      = (uint8_t)lsmash_bits_get( bits, 8 );
    }
    else
    {
        cc->color_primaries          = CP_UNSPECIFIED;
        cc->transfer_characteristics = TC_UNSPECIFIED;
        cc->matrix_coefficients      = MC_UNSPECIFIED;
    }
    if( cc->mono_chrome )
    {
        cc->color_range            = (uint8_t)lsmash_bits_get( bits, 1 );
        cc->subsampling_x          = 1;
        cc->subsampling_y          = 1;
        cc->chroma_sample_position = LSMASH_AV1_CSP_UNKNOWN;
        cc->separate_uv_delta_q    = 0;
        return;
    }
    else if( cc->color_primaries          == CP_BT_709
          && cc->transfer_characteristics == TC_SRGB
          && cc->matrix_coefficients      == MC_IDENTITY )
    {
        cc->color_range            = 1;
        cc->subsampling_x          = 0;
        cc->subsampling_y          = 0;
        cc->chroma_sample_position = LSMASH_AV1_CSP_UNKNOWN;    /* The spec does not define this! Anyway 'av1C' requires so do this. */
    }
    else
    {
        cc->color_range = (uint8_t)lsmash_bits_get( bits, 1 );
        if( sh->seq_profile == 0 )
        {
            cc->subsampling_x = 1;
            cc->subsampling_y = 1;
        }
        else if( sh->seq_profile == 1 )
        {
            cc->subsampling_x = 0;
            cc->subsampling_y = 0;
        }
        else
        {
            if( cc->twelve_bit )
            {
                cc->subsampling_x = (uint8_t)lsmash_bits_get( bits, 1 );
                cc->subsampling_y = cc->subsampling_x ? (uint8_t)lsmash_bits_get( bits, 1 ) : 0;
            }
            else
            {
                cc->subsampling_x = 1;
                cc->subsampling_y = 0;
            }
        }
        if( cc->subsampling_x && cc->subsampling_y )
            cc->chroma_sample_position = (uint8_t)lsmash_bits_get( bits, 2 );
        else
            cc->chroma_sample_position = LSMASH_AV1_CSP_UNKNOWN;    /* The spec does not define this! Anyway 'av1C' requires so do this. */
    }
    cc->separate_uv_delta_q = (uint8_t)lsmash_bits_get( bits, 1 );
}

int lsm_av1_parse_sequence_header
(
    av1_parser_t *parser
)
{
    if( !parser || !parser->bits || !parser->bits->bs )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_bits_t         *bits = parser->bits;
    av1_sequence_header_t *sh   = &parser->sequence_header;
    memset( sh, 0, sizeof(av1_sequence_header_t) );
    sh->seq_profile                  = (uint8_t)lsmash_bits_get( bits, 3 );
    IF_OUT_OF_SPEC( sh->seq_profile > 2 )
        return LSMASH_ERR_INVALID_DATA;
    sh->still_picture                = (uint8_t)lsmash_bits_get( bits, 1 );
    sh->reduced_still_picture_header = (uint8_t)lsmash_bits_get( bits, 1 );
    IF_OUT_OF_SPEC( !sh->still_picture && sh->reduced_still_picture_header )
        return LSMASH_ERR_INVALID_DATA;
    if( sh->reduced_still_picture_header )
    {
        sh->timing_info_present_flag           = 0;
        sh->decoder_model_info_present_flag    = 0;
        sh->initial_display_delay_present_flag = 0;
        sh->operating_points_cnt_minus_1       = 0;
        sh->op[0].operating_point_idc                       = 0;
        sh->op[0].seq_level_idx                             = (uint8_t)lsmash_bits_get( bits, 5 );
        sh->op[0].seq_tier                                  = 0;
        sh->op[0].decoder_model_present_for_this_op         = 0;
        sh->op[0].initial_display_delay_present_for_this_op = 0;
    }
    else
    {
        sh->timing_info_present_flag = (uint8_t)lsmash_bits_get( bits, 1 );
        if( sh->timing_info_present_flag )
        {
            /* timing_info() */
            sh->timing_info.num_units_in_display_tick = (uint32_t)lsmash_bits_get( bits, 32 );
            sh->timing_info.time_scale                = (uint32_t)lsmash_bits_get( bits, 32 );
            IF_OUT_OF_SPEC( sh->timing_info.num_units_in_display_tick == 0 || sh->timing_info.time_scale == 0 )
                return LSMASH_ERR_INVALID_DATA;
            sh->timing_info.equal_picture_interval    = (uint8_t)lsmash_bits_get( bits, 1 );
            if( sh->timing_info.equal_picture_interval )
            {
                sh->timing_info.num_ticks_per_picture_minus_1 = lsm_av1_get_uvlc( bits );
                IF_OUT_OF_SPEC( sh->timing_info.num_ticks_per_picture_minus_1 == UINT32_MAX )
                    return LSMASH_ERR_INVALID_DATA;
            }
            sh->decoder_model_info_present_flag = (uint8_t)lsmash_bits_get( bits, 1 );
            if( sh->decoder_model_info_present_flag )
            {
                /* decoder_model_info() */
                sh->decoder_model_info.buffer_delay_length_minus_1            = (uint8_t)lsmash_bits_get( bits, 5 );
                sh->decoder_model_info.num_units_in_decoding_tick             = (uint32_t)lsmash_bits_get( bits, 32 );
                sh->decoder_model_info.buffer_removal_time_length_minus_1     = (uint8_t)lsmash_bits_get( bits, 5 );
                sh->decoder_model_info.frame_presentation_time_length_minus_1 = (uint8_t)lsmash_bits_get( bits, 5 );
            }
        }
        else
            sh->decoder_model_info_present_flag = 0;
        sh->initial_display_delay_present_flag = (uint8_t)lsmash_bits_get( bits, 1 );
        sh->operating_points_cnt_minus_1       = (uint8_t)lsmash_bits_get( bits, 5 );
        for( uint8_t i = 0; i <= sh->operating_points_cnt_minus_1; i++ )
        {
            sh->op[i].operating_point_idc = (uint16_t)lsmash_bits_get( bits, 12 );
            sh->op[i].seq_level_idx       = (uint8_t)lsmash_bits_get( bits, 5 );
            sh->op[i].seq_tier            = sh->op[i].seq_level_idx > 7 ? (uint8_t)lsmash_bits_get( bits, 1 ) : 0;
            if( sh->decoder_model_info_present_flag )
            {
                sh->op[i].decoder_model_present_for_this_op = (uint8_t)lsmash_bits_get( bits, 1 );
                if( sh->op[i].decoder_model_present_for_this_op )
                {
                    /* operating_parameters_info( i ) */
                    uint32_t n = sh->decoder_model_info.buffer_delay_length_minus_1 + 1;
                    sh->op[i].operating_parameters_info.decoder_buffer_delay = lsmash_bits_get( bits, n );
                    sh->op[i].operating_parameters_info.encoder_buffer_delay = lsmash_bits_get( bits, n );
                    sh->op[i].operating_parameters_info.low_delay_mode_flag  = (uint8_t)lsmash_bits_get( bits, 1 );
                }
            }
            else
                sh->op[i].decoder_model_present_for_this_op = 0;
            if( sh->initial_display_delay_present_flag )
            {
                sh->op[i].initial_display_delay_present_for_this_op = (uint8_t)lsmash_bits_get( bits, 1 );
                if( sh->op[i].initial_display_delay_present_for_this_op )
                    sh->op[i].initial_display_delay_minus_1 = (uint8_t)lsmash_bits_get( bits, 4 );
                else
                    sh->op[i].initial_display_delay_minus_1 = BufferPoolMaxSize - 1;
            }
        }
    }
    sh->frame_width_bits_minus_1  = (uint8_t)lsmash_bits_get( bits, 4 );
    sh->frame_height_bits_minus_1 = (uint8_t)lsmash_bits_get( bits, 4 );
    sh->max_frame_width_minus_1   = (uint32_t)lsmash_bits_get( bits, sh->frame_width_bits_minus_1  + 1 );
    sh->max_frame_height_minus_1  = (uint32_t)lsmash_bits_get( bits, sh->frame_height_bits_minus_1 + 1 );
    sh->frame_id_numbers_present_flag = sh->reduced_still_picture_header ? 0 : (uint8_t)lsmash_bits_get( bits, 1 );
    if( sh->frame_id_numbers_present_flag )
    {
        sh->delta_frame_id_length_minus_2      = (uint8_t)lsmash_bits_get( bits, 4 );
        sh->additional_frame_id_length_minus_1 = (uint8_t)lsmash_bits_get( bits, 3 );
        IF_OUT_OF_SPEC( sh->additional_frame_id_length_minus_1 + sh->delta_frame_id_length_minus_2 + 3 > 16 )
            return LSMASH_ERR_INVALID_DATA;
    }
    sh->use_128x128_superblock   = (uint8_t)lsmash_bits_get( bits, 1 );
    sh->enable_filter_intra      = (uint8_t)lsmash_bits_get( bits, 1 );
    sh->enable_intra_edge_filter = (uint8_t)lsmash_bits_get( bits, 1 );
    if( sh->reduced_still_picture_header )
    {
        sh->enable_interintra_compound     = 0;
        sh->enable_masked_compound         = 0;
        sh->enable_warped_motion           = 0;
        sh->enable_dual_filter             = 0;
        sh->enable_order_hint              = 0;
        sh->enable_jnt_comp                = 0;
        sh->enable_ref_frame_mvs           = 0;
        sh->seq_force_screen_content_tools = SELECT_SCREEN_CONTENT_TOOLS;
        sh->seq_force_integer_mv           = SELECT_INTEGER_MV;
        sh->OrderHintBits                  = 0;
    }
    else
    {
        sh->enable_interintra_compound = (uint8_t)lsmash_bits_get( bits, 1 );
        sh->enable_masked_compound     = (uint8_t)lsmash_bits_get( bits, 1 );
        sh->enable_warped_motion       = (uint8_t)lsmash_bits_get( bits, 1 );
        sh->enable_dual_filter         = (uint8_t)lsmash_bits_get( bits, 1 );
        sh->enable_order_hint          = (uint8_t)lsmash_bits_get( bits, 1 );
        if( sh->enable_order_hint )
        {
            sh->enable_jnt_comp      = (uint8_t)lsmash_bits_get( bits, 1 );
            sh->enable_ref_frame_mvs = (uint8_t)lsmash_bits_get( bits, 1 );
        }
        else
        {
            sh->enable_jnt_comp      = 0;
            sh->enable_ref_frame_mvs = 0;
        }
        sh->seq_choose_screen_content_tools = (uint8_t)lsmash_bits_get( bits, 1 );
        if( sh->seq_choose_screen_content_tools )
            sh->seq_force_screen_content_tools = SELECT_SCREEN_CONTENT_TOOLS;
        else
            sh->seq_force_screen_content_tools = (uint8_t)lsmash_bits_get( bits, 1 );
        if( sh->seq_force_screen_content_tools > 0 )
        {
            sh->seq_choose_integer_mv = (uint8_t)lsmash_bits_get( bits, 1 );
            if( sh->seq_choose_integer_mv )
                sh->seq_force_integer_mv = SELECT_INTEGER_MV;
            else
                sh->seq_force_integer_mv = (uint8_t)lsmash_bits_get( bits, 1 );
        }
        else
            sh->seq_force_integer_mv = SELECT_INTEGER_MV;
        if( sh->enable_order_hint )
        {
            sh->order_hint_bits_minus_1 = (uint8_t)lsmash_bits_get( bits, 3 );
            sh->OrderHintBits           = sh->order_hint_bits_minus_1 + 1;
        }
        else
            sh->OrderHintBits = 0;
    }
    sh->enable_superres    = (uint8_t)lsmash_bits_get( bits, 1 );
    sh->enable_cdef        = (uint8_t)lsmash_bits_get( bits, 1 );
    sh->enable_restoration = (uint8_t)lsmash_bits_get( bits, 1 );
    av1_parser_color_config( parser );
    sh->film_grain_params_present = (uint8_t)lsmash_bits_get( bits, 1 );
    if( bits->bs->eob || bits->bs->error )
        return LSMASH_ERR_INVALID_DATA;
    return 0;
}

static void av1_parse_superres_params
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    lsmash_bits_t         *bits = parser->bits;
    av1_sequence_header_t *sh   = &parser->sequence_header;
    frame->use_superres = sh->enable_superres ? (uint8_t)lsmash_bits_get( bits, 1 ) : 0;
    if( frame->use_superres )
    {
        frame->coded_denom   = (uint8_t)lsmash_bits_get( bits, SUPERRES_DENOM_BITS );
        frame->SuperresDenom = frame->coded_denom + SUPERRES_DENOM_MIN;
    }
    else
        frame->SuperresDenom = SUPERRES_NUM;
    frame->UpscaledWidth = frame->FrameWidth;
    frame->FrameWidth    = (frame->UpscaledWidth * SUPERRES_NUM + (frame->SuperresDenom / 2)) / frame->SuperresDenom;
}

static void av1_compute_image_size
(
    av1_frame_t *frame
)
{
    frame->MiCols = 2 * ((frame->FrameWidth  + 7) >> 3);
    frame->MiRows = 2 * ((frame->FrameHeight + 7) >> 3);
}

static int av1_parse_frame_size
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    lsmash_bits_t         *bits = parser->bits;
    av1_sequence_header_t *sh   = &parser->sequence_header;
    if( frame->frame_size_override_flag )
    {
        frame->frame_width_minus_1  = (uint32_t)lsmash_bits_get( bits, sh->frame_width_bits_minus_1  + 1 );
        frame->frame_height_minus_1 = (uint32_t)lsmash_bits_get( bits, sh->frame_height_bits_minus_1 + 1 );
        IF_OUT_OF_SPEC( frame->frame_width_minus_1  > sh->max_frame_width_minus_1
                     || frame->frame_height_minus_1 > sh->max_frame_height_minus_1 )
            return LSMASH_ERR_INVALID_DATA;
        frame->FrameWidth  = frame->frame_width_minus_1  + 1;
        frame->FrameHeight = frame->frame_height_minus_1 + 1;
    }
    else
    {
        frame->FrameWidth  = sh->max_frame_width_minus_1  + 1;
        frame->FrameHeight = sh->max_frame_height_minus_1 + 1;
    }
    av1_parse_superres_params( parser, frame );
    av1_compute_image_size( frame );
    return 0;
}

static int av1_parse_render_size
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    lsmash_bits_t *bits = parser->bits;
    frame->render_and_frame_size_different = (uint8_t)lsmash_bits_get( bits, 1 );
    if( frame->render_and_frame_size_different )
    {
        frame->render_width_minus_1  = (uint32_t)lsmash_bits_get( bits, 16 );
        frame->render_height_minus_1 = (uint32_t)lsmash_bits_get( bits, 16 );
        frame->RenderWidth  = frame->render_width_minus_1  + 1;
        frame->RenderHeight = frame->render_height_minus_1 + 1;
    }
    else
    {
        frame->RenderWidth  = frame->UpscaledWidth;
        frame->RenderHeight = frame->FrameHeight;
    }
    return 0;
}

static int av1_parse_frame_size_with_refs
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    lsmash_bits_t *bits = parser->bits;
    uint8_t found_ref = 0;
    for( int i = 0; i < AV1_REFS_PER_FRAME; i++ )
    {
        found_ref = (uint8_t)lsmash_bits_get( bits, 1 );
        if( found_ref )
        {
            int idx = frame->ref_frame_idx[i];
            IF_OUT_OF_SPEC( idx < 0 || idx >= AV1_NUM_REF_FRAMES || !parser->RefValid[idx] )
                return LSMASH_ERR_INVALID_DATA;
            frame->UpscaledWidth = parser->RefUpscaledWidth[idx];
            frame->FrameWidth    = frame->UpscaledWidth;
            frame->FrameHeight   = parser->RefFrameHeight  [idx];
            frame->RenderWidth   = parser->RefRenderWidth  [idx];
            frame->RenderHeight  = parser->RefRenderHeight [idx];
            break;
        }
    }
    if( found_ref == 0 )
    {
        int err;
        if( (err = av1_parse_frame_size( parser, frame )) < 0
         || (err = av1_parse_render_size( parser, frame )) < 0 )
            return err;
    }
    else
    {
        av1_parse_superres_params( parser, frame );
        av1_compute_image_size( frame );
    }
    return 0;
}

static int av1_get_relative_dist
(
    av1_sequence_header_t *sh,
    int a,
    int b
)
{
    if( !sh->enable_order_hint || sh->OrderHintBits == 0 )
        return 0;
    int diff = a - b;
    int m = 1 << (sh->OrderHintBits - 1);
    diff = (diff & (m - 1)) - (diff & m);
    return diff;
}

static int av1_find_latest_backward
(
    av1_frame_t *frame
)
{
    int ref = -1;
    for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
    {
        int hint = frame->shiftedOrderHints[i];
        if( !frame->usedFrame[i] && hint >= frame->curFrameHint && (ref < 0 || hint >= frame->latestOrderHint) )
        {
            ref = i;
            frame->latestOrderHint = hint;
        }
    }
    return ref;
}

static int av1_find_earliest_backward
(
    av1_frame_t *frame
)
{
    int ref = -1;
    for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
    {
        int hint = frame->shiftedOrderHints[i];
        if( !frame->usedFrame[i] && hint >= frame->curFrameHint && (ref < 0 || hint < frame->earliestOrderHint) )
        {
            ref = i;
            frame->earliestOrderHint = hint;
        }
    }
    return ref;
}

static int av1_find_latest_forward
(
    av1_frame_t *frame
)
{
    int ref = -1;
    for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
    {
        int hint = frame->shiftedOrderHints[i];
        if( !frame->usedFrame[i] && hint < frame->curFrameHint && (ref < 0 || hint >= frame->latestOrderHint) )
        {
            ref = i;
            frame->latestOrderHint = hint;
        }
    }
    return ref;
}

static int av1_set_frame_refs
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    av1_sequence_header_t *sh = &parser->sequence_header;
    /* The reference frames used for the LAST_FRAME and GOLDEN_FRAME references. */
    for( int i = 0; i < AV1_REFS_PER_FRAME; i++ )
        frame->ref_frame_idx[i] = -1;
    frame->ref_frame_idx[LAST_FRAME   - LAST_FRAME] = frame->last_frame_idx;
    frame->ref_frame_idx[GOLDEN_FRAME - LAST_FRAME] = frame->gold_frame_idx;
    /* An array usedFrame marking which reference frames have been used is prepared. */
    for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
        frame->usedFrame[i] = 0;
    frame->usedFrame[ frame->last_frame_idx ] = 1;
    frame->usedFrame[ frame->gold_frame_idx ] = 1;
    frame->curFrameHint = 1 << (sh->OrderHintBits - 1);
    for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
        frame->shiftedOrderHints[i] = frame->curFrameHint + av1_get_relative_dist( sh, (int)parser->RefOrderHint[i], (int)frame->OrderHint );
    frame->lastOrderHint = frame->shiftedOrderHints[ frame->last_frame_idx ];
    frame->goldOrderHint = frame->shiftedOrderHints[ frame->gold_frame_idx ];
    IF_OUT_OF_SPEC( frame->lastOrderHint >= frame->curFrameHint || frame->goldOrderHint >= frame->curFrameHint )
        return LSMASH_ERR_INVALID_DATA;
    /* The ALTREF_FRAME reference is set to be a backward reference to the frame with highest output order */
    int ref = av1_find_latest_backward( frame );
    if( ref >= 0 )
    {
        frame->ref_frame_idx[ALTREF_FRAME - LAST_FRAME] = ref;
        frame->usedFrame[ref] = 1;
    }
    /* The BWDREF_FRAME reference is set to be a backward reference to the closest frame. */
    ref = av1_find_earliest_backward( frame );
    if( ref >= 0 )
    {
        frame->ref_frame_idx[BWDREF_FRAME - LAST_FRAME] = ref;
        frame->usedFrame[ref] = 1;
    }
    /* The ALTREF2_FRAME reference is set to the next closest backward reference. */
    ref = av1_find_earliest_backward( frame );
    if( ref >= 0 )
    {
        frame->ref_frame_idx[ALTREF2_FRAME - LAST_FRAME] = ref;
        frame->usedFrame[ref] = 1;
    }
    /* The remaining references are set to be forward references in anti-chronological order. */
    for( int i = 0; i < AV1_REFS_PER_FRAME - 2; i++ )
    {
        static const int Ref_Frame_List[AV1_REFS_PER_FRAME - 2] =
        {
            LAST2_FRAME, LAST3_FRAME, BWDREF_FRAME, ALTREF2_FRAME, ALTREF_FRAME
        };
        int refFrame = Ref_Frame_List[i];
        if( frame->ref_frame_idx[refFrame - LAST_FRAME] < 0 )
        {
            ref = av1_find_latest_forward( frame );
            if( ref >= 0 )
            {
                frame->ref_frame_idx[refFrame - LAST_FRAME] = ref;
                frame->usedFrame[ref] = 1;
            }
        }
    }
    /* Finally, any remaining references are set to the reference frame with smallest output order. */
    ref = -1;
    for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
    {
        int hint = frame->shiftedOrderHints[i];
        if( ref < 0 || hint < frame->earliestOrderHint )
        {
            ref = i;
            frame->earliestOrderHint = hint;
        }
    }
    for( int i = 0; i < AV1_REFS_PER_FRAME; i++ )
        if( frame->ref_frame_idx[i] < 0 )
            frame->ref_frame_idx[i] = ref;
    return 0;
}

static void av1_read_interpolation_filter
(
    lsmash_bits_t *bits,
    av1_frame_t   *frame
)
{
    frame->is_filter_switchable = (uint8_t)lsmash_bits_get( bits, 1 );
    frame->interpolation_filter = frame->is_filter_switchable ? SWITCHABLE : (uint8_t)lsmash_bits_get( bits, 2 );
}

static uint32_t av1_tile_log2
(
    uint32_t blkSize,
    uint32_t target
)
{
    uint32_t k;
    for( k = 0; (blkSize << k) < target; k++ )
        ;
    return k;
}

static int av1_parse_tile_info
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    lsmash_bits_t         *bits = parser->bits;
    av1_sequence_header_t *sh   = &parser->sequence_header;
    /* To compute NumTiles, we need TileCols and TileRows. */
    uint32_t sbCols  = sh->use_128x128_superblock ? ((frame->MiCols + 31) >> 5) : ((frame->MiCols + 15) >> 4);
    uint32_t sbRows  = sh->use_128x128_superblock ? ((frame->MiRows + 31) >> 5) : ((frame->MiRows + 15) >> 4);
    uint32_t sbShift = sh->use_128x128_superblock ? 5 : 4;
    uint32_t sbSize  = sbShift + 2;
    uint32_t maxTileWidthSb  = AV1_MAX_TILE_WIDTH >> sbSize;
    uint32_t maxTileAreaSb   = AV1_MAX_TILE_AREA  >> (2 * sbSize);
    uint32_t minLog2TileCols = av1_tile_log2( maxTileWidthSb, sbCols );
    uint32_t maxLog2TileCols = av1_tile_log2( 1, LSMASH_MIN( sbCols, AV1_MAX_TILE_COLS ) );
    uint32_t maxLog2TileRows = av1_tile_log2( 1, LSMASH_MIN( sbRows, AV1_MAX_TILE_ROWS ) );
    uint32_t minLog2Tiles    = LSMASH_MAX( minLog2TileCols, av1_tile_log2( maxTileAreaSb, sbRows * sbCols ) );
    uint8_t uniform_tile_spacing_flag = (uint8_t)lsmash_bits_get( bits, 1 );
    if( uniform_tile_spacing_flag )
    {
        frame->TileColsLog2 = minLog2TileCols;
        while( frame->TileColsLog2 < maxLog2TileCols )
        {
            uint8_t increment_tile_cols_log2 = (uint8_t)lsmash_bits_get( bits, 1 );
            if( increment_tile_cols_log2 == 1 )
                ++ frame->TileColsLog2;
            else
                break;
        }
        uint32_t tileWidthSb = (sbCols + (1u << frame->TileColsLog2) - 1) >> frame->TileColsLog2;
        IF_OUT_OF_SPEC( tileWidthSb == 0 )
            return LSMASH_ERR_INVALID_DATA;
        uint32_t i = 0;
        for( uint32_t startSb = 0; startSb < sbCols; startSb += tileWidthSb )
        {
            IF_OUT_OF_SPEC( i >= AV1_MAX_TILE_COLS )
                return LSMASH_ERR_INVALID_DATA;
            frame->MiColStarts[i++] = startSb << sbShift;
        }
        frame->MiColStarts[i] = frame->MiCols;
        frame->TileCols = i;
        uint32_t minLog2TileRows = minLog2Tiles > frame->TileColsLog2 ? (minLog2Tiles - frame->TileColsLog2) : 0;
        frame->TileRowsLog2 = minLog2TileRows;
        while( frame->TileRowsLog2 < maxLog2TileRows )
        {
            uint8_t increment_tile_rows_log2 = (uint8_t)lsmash_bits_get( bits, 1 );
            if( increment_tile_rows_log2 == 1 )
                ++ frame->TileRowsLog2;
            else
                break;
        }
        uint32_t tileHeightSb = (sbRows + (1u << frame->TileRowsLog2) - 1) >> frame->TileRowsLog2;
        IF_OUT_OF_SPEC( tileHeightSb == 0 )
            return LSMASH_ERR_INVALID_DATA;
        i = 0;
        for( uint32_t startSb = 0; startSb < sbRows; startSb += tileHeightSb )
        {
            IF_OUT_OF_SPEC( i >= AV1_MAX_TILE_ROWS )
                return LSMASH_ERR_INVALID_DATA;
            frame->MiRowStarts[i++] = startSb << sbShift;
        }
        frame->MiRowStarts[i] = frame->MiRows;
        frame->TileRows = i;
    }
    else
    {
        uint32_t widestTileSb = 0;
        uint32_t i = 0;
        uint32_t sizeSb = 0;
        for( uint32_t startSb = 0; startSb < sbCols; startSb += sizeSb )
        {
            IF_OUT_OF_SPEC( i >= AV1_MAX_TILE_COLS )
                return LSMASH_ERR_INVALID_DATA;
            frame->MiColStarts[i++] = startSb << sbShift;
            uint32_t maxWidth = LSMASH_MIN( sbCols - startSb, maxTileWidthSb );
            uint32_t width_in_sbs_minus_1 = (uint32_t)lsm_av1_get_ns( bits, maxWidth );
            sizeSb = width_in_sbs_minus_1 + 1;
            widestTileSb = LSMASH_MAX( sizeSb, widestTileSb );
        }
        frame->MiColStarts[i]   = frame->MiCols;
        frame->TileCols         = i;
        frame->TileColsLog2     = av1_tile_log2( 1, frame->TileCols );
        if( minLog2Tiles > 0 )
            maxTileAreaSb = (sbRows * sbCols) >> (minLog2Tiles + 1);
        else
            maxTileAreaSb = sbRows * sbCols;
        uint32_t maxTileHeightSb = LSMASH_MAX( widestTileSb ? (maxTileAreaSb / widestTileSb) : 1, 1 );
        i = 0;
        for( uint32_t startSb = 0; startSb < sbRows; startSb += sizeSb )
        {
            IF_OUT_OF_SPEC( i >= AV1_MAX_TILE_ROWS )
                return LSMASH_ERR_INVALID_DATA;
            frame->MiRowStarts[i++] = startSb << sbShift;
            uint32_t maxHeight = LSMASH_MIN( sbRows - startSb, maxTileHeightSb );
            uint32_t height_in_sbs_minus_1 = (uint32_t)lsm_av1_get_ns( bits, maxHeight );
            sizeSb = height_in_sbs_minus_1 + 1;
        }
        frame->MiRowStarts[i] = frame->MiRows;
        frame->TileRows       = i;
        frame->TileRowsLog2   = av1_tile_log2( 1, frame->TileRows );
    }
    if( frame->TileColsLog2 > 0 || frame->TileRowsLog2 > 0 )
    {
        frame->context_update_tile_id  = (uint32_t)lsmash_bits_get( bits, frame->TileRowsLog2 + frame->TileColsLog2 );
        frame->tile_size_bytes_minus_1 = (uint8_t)lsmash_bits_get( bits, 2 );
        frame->TileSizeBytes           = frame->tile_size_bytes_minus_1 + 1;
    }
    else
        frame->context_update_tile_id = 0;
    frame->NumTiles = frame->TileCols * frame->TileRows;
    return 0;
}

static int8_t av1_read_delta_q
(
    lsmash_bits_t *bits
)
{
    uint8_t delta_coded = (uint8_t)lsmash_bits_get( bits, 1 );
    return delta_coded ? (int8_t)lsm_av1_get_su( bits, 1 + 6 ) : 0;
}

static void av1_parse_quantization_params
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    lsmash_bits_t      *bits = parser->bits;
    av1_color_config_t *cc   = &parser->sequence_header.color_config;
    frame->base_q_idx = (uint8_t)lsmash_bits_get( bits, 8 );
    frame->DeltaQYDc  = av1_read_delta_q( bits );
    if( cc->NumPlanes > 1 )
    {
        uint8_t diff_uv_delta = cc->separate_uv_delta_q ? (uint8_t)lsmash_bits_get( bits, 1 ) : 0;
        frame->DeltaQUDc = av1_read_delta_q( bits );
        frame->DeltaQUAc = av1_read_delta_q( bits );
        if( diff_uv_delta )
        {
            frame->DeltaQVDc = av1_read_delta_q( bits );
            frame->DeltaQVAc = av1_read_delta_q( bits );
        }
        else
        {
            frame->DeltaQVDc = frame->DeltaQUDc;
            frame->DeltaQVAc = frame->DeltaQUAc;
        }
    }
    else
    {
        frame->DeltaQUDc = 0;
        frame->DeltaQUAc = 0;
        frame->DeltaQVDc = 0;
        frame->DeltaQVAc = 0;
    }
    uint8_t using_qmatrix = (uint8_t)lsmash_bits_get( bits, 1 );
    if( using_qmatrix )
    {
        lsmash_bits_get( bits, 4 ); /* qm_y */
        lsmash_bits_get( bits, 4 ); /* qm_u */
        if( cc->separate_uv_delta_q )
            lsmash_bits_get( bits, 4 ); /* qm_v */
    }
}

static void av1_parse_segmentation_params
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    static const uint8_t Segmentation_Feature_Bits  [AV1_SEG_LVL_MAX] = { 8, 6, 6, 6, 6, 3, 0, 0 };
    static const uint8_t Segmentation_Feature_Signed[AV1_SEG_LVL_MAX] = { 1, 1, 1, 1, 1, 0, 0, 0 };
    static const int16_t Segmentation_Feature_Max   [AV1_SEG_LVL_MAX] = { 255, 63, 63, 63, 63, 7, 0, 0 };
    lsmash_bits_t *bits = parser->bits;
    frame->segmentation_enabled = (uint8_t)lsmash_bits_get( bits, 1 );
    if( frame->segmentation_enabled )
    {
        uint8_t segmentation_update_map      = 1;
        uint8_t segmentation_temporal_update = 0;
        uint8_t segmentation_update_data     = 1;
        if( frame->primary_ref_frame != PRIMARY_REF_NONE )
        {
            segmentation_update_map = (uint8_t)lsmash_bits_get( bits, 1 );
            if( segmentation_update_map )
                segmentation_temporal_update = (uint8_t)lsmash_bits_get( bits, 1 );
            (void)segmentation_temporal_update;
            segmentation_update_data = (uint8_t)lsmash_bits_get( bits, 1 );
        }
        if( segmentation_update_data )
        {
            for( int i = 0; i < AV1_MAX_SEGMENTS; i++ )
                for( int j = 0; j < AV1_SEG_LVL_MAX; j++ )
                {
                    uint8_t feature_enabled = (uint8_t)lsmash_bits_get( bits, 1 );
                    frame->FeatureEnabled[i][j] = feature_enabled;
                    int16_t clippedValue = 0;
                    if( feature_enabled )
                    {
                        uint8_t bitsToRead = Segmentation_Feature_Bits[j];
                        int16_t limit      = Segmentation_Feature_Max[j];
                        int16_t feature_value;
                        if( Segmentation_Feature_Signed[j] )
                        {
                            feature_value = (int16_t)lsm_av1_get_su( bits, 1 + bitsToRead );
                            clippedValue  = LSMASH_MIN( LSMASH_MAX( feature_value, -limit ), limit );
                        }
                        else
                        {
                            feature_value = bitsToRead ? (int16_t)lsmash_bits_get( bits, bitsToRead ) : 0;
                            clippedValue  = LSMASH_MIN( LSMASH_MAX( feature_value, 0 ), limit );
                        }
                    }
                    frame->FeatureData[i][j] = clippedValue;
                }
        }
        else if( frame->primary_ref_frame < AV1_REFS_PER_FRAME )
        {
            int prev_idx = frame->ref_frame_idx[ frame->primary_ref_frame ];
            if( prev_idx >= 0 && prev_idx < AV1_NUM_REF_FRAMES )
            {
                memcpy( frame->FeatureEnabled, parser->RefFeatureEnabled[prev_idx], sizeof(frame->FeatureEnabled) );
                memcpy( frame->FeatureData,    parser->RefFeatureData   [prev_idx], sizeof(frame->FeatureData) );
            }
        }
    }
    else
    {
        memset( frame->FeatureEnabled, 0, sizeof(frame->FeatureEnabled) );
        memset( frame->FeatureData,    0, sizeof(frame->FeatureData) );
    }
}

static void av1_parse_delta_q_params
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    lsmash_bits_t *bits = parser->bits;
    frame->delta_q_res     = 0;
    frame->delta_q_present = frame->base_q_idx > 0 ? (uint8_t)lsmash_bits_get( bits, 1 ) : 0;
    if( frame->delta_q_present )
        frame->delta_q_res = (uint8_t)lsmash_bits_get( bits, 2 );
}

static void av1_parse_delta_lf_params
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    lsmash_bits_t *bits = parser->bits;
    frame->delta_lf_present = 0;
    frame->delta_lf_res     = 0;
    frame->delta_lf_multi   = 0;
    if( frame->delta_q_present )
    {
        if( !frame->allow_intrabc )
            frame->delta_lf_present = (uint8_t)lsmash_bits_get( bits, 1 );
        if( frame->delta_lf_present )
        {
            frame->delta_lf_res   = (uint8_t)lsmash_bits_get( bits, 2 );
            frame->delta_lf_multi = (uint8_t)lsmash_bits_get( bits, 1 );
        }
    }
}

static void av1_parse_loop_filter_params
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    if( frame->CodedLossless || frame->allow_intrabc )
        return;
    lsmash_bits_t      *bits = parser->bits;
    av1_color_config_t *cc   = &parser->sequence_header.color_config;
    uint8_t loop_filter_level_0 = (uint8_t)lsmash_bits_get( bits, 6 );
    uint8_t loop_filter_level_1 = (uint8_t)lsmash_bits_get( bits, 6 );
    if( cc->NumPlanes > 1 && (loop_filter_level_0 || loop_filter_level_1) )
    {
        lsmash_bits_get( bits, 6 ); /* loop_filter_level[2] */
        lsmash_bits_get( bits, 6 ); /* loop_filter_level[3] */
    }
    lsmash_bits_get( bits, 3 ); /* loop_filter_sharpness */
    uint8_t loop_filter_delta_enabled = (uint8_t)lsmash_bits_get( bits, 1 );
    if( loop_filter_delta_enabled )
    {
        uint8_t loop_filter_delta_update = (uint8_t)lsmash_bits_get( bits, 1 );
        if( loop_filter_delta_update )
        {
            for( int i = 0; i < AV1_TOTAL_REFS_COUNT; i++ )
                if( lsmash_bits_get( bits, 1 ) ) /* update_ref_delta */
                    lsm_av1_get_su( bits, 1 + 6 );   /* loop_filter_ref_deltas[i] */
            for( int i = 0; i < 2; i++ )
                if( lsmash_bits_get( bits, 1 ) ) /* update_mode_delta */
                    lsm_av1_get_su( bits, 1 + 6 );   /* loop_filter_mode_deltas[i] */
        }
    }
}

static void av1_parse_cdef_params
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    av1_sequence_header_t *sh = &parser->sequence_header;
    if( frame->CodedLossless || frame->allow_intrabc || !sh->enable_cdef )
        return;
    lsmash_bits_t      *bits = parser->bits;
    av1_color_config_t *cc   = &sh->color_config;
    lsmash_bits_get( bits, 2 ); /* cdef_damping_minus_3 */
    uint8_t cdef_bits = (uint8_t)lsmash_bits_get( bits, 2 );
    for( int i = 0; i < (1 << cdef_bits); i++ )
    {
        lsmash_bits_get( bits, 4 ); /* cdef_y_pri_strength[i] */
        lsmash_bits_get( bits, 2 ); /* cdef_y_sec_strength[i] */
        if( cc->NumPlanes > 1 )
        {
            lsmash_bits_get( bits, 4 ); /* cdef_uv_pri_strength[i] */
            lsmash_bits_get( bits, 2 ); /* cdef_uv_sec_strength[i] */
        }
    }
}

static void av1_parse_lr_params
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    av1_sequence_header_t *sh = &parser->sequence_header;
    if( frame->AllLossless || frame->allow_intrabc || !sh->enable_restoration )
        return;
    lsmash_bits_t      *bits = parser->bits;
    av1_color_config_t *cc   = &sh->color_config;
    uint8_t UsesLr       = 0;
    uint8_t usesChromaLr = 0;
    for( int i = 0; i < cc->NumPlanes; i++ )
    {
        uint8_t lr_type = (uint8_t)lsmash_bits_get( bits, 2 );
        if( lr_type != 0 )
        {
            UsesLr = 1;
            if( i > 0 )
                usesChromaLr = 1;
        }
    }
    if( UsesLr )
    {
        if( sh->use_128x128_superblock )
            lsmash_bits_get( bits, 1 ); /* lr_unit_shift */
        else
        {
            uint8_t lr_unit_shift = (uint8_t)lsmash_bits_get( bits, 1 );
            if( lr_unit_shift )
                lsmash_bits_get( bits, 1 ); /* lr_unit_extra_shift */
        }
        if( cc->subsampling_x && cc->subsampling_y && usesChromaLr )
            lsmash_bits_get( bits, 1 ); /* lr_uv_shift */
    }
}

static void av1_parse_skip_mode_params
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    av1_sequence_header_t *sh = &parser->sequence_header;
    if( frame->FrameIsIntra || !frame->reference_select || !sh->enable_order_hint )
        frame->skipModeAllowed = 0;
    else
    {
        int forwardIdx   = -1;
        int backwardIdx  = -1;
        int forwardHint  = 0;
        int backwardHint = 0;
        for( int i = 0; i < AV1_REFS_PER_FRAME; i++ )
        {
            int refHint = (int)parser->RefOrderHint[ frame->ref_frame_idx[i] ];
            if( av1_get_relative_dist( sh, refHint, (int)frame->OrderHint ) < 0 )
            {
                if( forwardIdx < 0 || av1_get_relative_dist( sh, refHint, forwardHint ) > 0 )
                {
                    forwardIdx  = i;
                    forwardHint = refHint;
                }
            }
            else if( av1_get_relative_dist( sh, refHint, (int)frame->OrderHint ) > 0 )
            {
                if( backwardIdx < 0 || av1_get_relative_dist( sh, refHint, backwardHint ) < 0 )
                {
                    backwardIdx  = i;
                    backwardHint = refHint;
                }
            }
        }
        if( forwardIdx < 0 )
            frame->skipModeAllowed = 0;
        else if( backwardIdx >= 0 )
            frame->skipModeAllowed = 1;
        else
        {
            int secondForwardIdx  = -1;
            int secondForwardHint = 0;
            for( int i = 0; i < AV1_REFS_PER_FRAME; i++ )
            {
                int refHint = (int)parser->RefOrderHint[ frame->ref_frame_idx[i] ];
                if( av1_get_relative_dist( sh, refHint, forwardHint ) < 0 )
                {
                    if( secondForwardIdx < 0 || av1_get_relative_dist( sh, refHint, secondForwardHint ) > 0 )
                    {
                        secondForwardIdx  = i;
                        secondForwardHint = refHint;
                    }
                }
            }
            frame->skipModeAllowed = secondForwardIdx >= 0 ? 1 : 0;
        }
    }
    frame->skip_mode_present = frame->skipModeAllowed ? (uint8_t)lsmash_bits_get( parser->bits, 1 ) : 0;
}

static uint32_t av1_decode_subexp
(
    lsmash_bits_t *bits,
    uint32_t       numSyms
)
{
    uint32_t i  = 0;
    uint32_t mk = 0;
    uint32_t k  = 3;
    while( 1 )
    {
        uint32_t b2 = i ? k + i - 1 : k;
        uint32_t a  = 1u << b2;
        if( numSyms <= mk + 3 * a )
        {
            uint32_t subexp_final_bits = (uint32_t)lsm_av1_get_ns( bits, numSyms - mk );
            return subexp_final_bits + mk;
        }
        else
        {
            uint8_t subexp_more_bits = (uint8_t)lsmash_bits_get( bits, 1 );
            if( subexp_more_bits )
            {
                i++;
                mk += a;
            }
            else
            {
                uint32_t subexp_bits = (uint32_t)lsmash_bits_get( bits, b2 );
                return subexp_bits + mk;
            }
        }
    }
}

static void av1_read_global_param
(
    lsmash_bits_t *bits,
    av1_frame_t   *frame,
    uint8_t        type,
    int            idx
)
{
    uint32_t absBits = 12; /* GM_ABS_ALPHA_BITS */
    if( idx < 2 )
    {
        if( type == 1 /* TRANSLATION */ )
            absBits = 9 - !frame->allow_high_precision_mv; /* GM_ABS_TRANS_ONLY_BITS - !allow_high_precision_mv */
        else
            absBits = 12; /* GM_ABS_TRANS_BITS */
    }
    uint32_t mx = (1u << (absBits + 1)) + 1;
    av1_decode_subexp( bits, mx );
}

static void av1_parse_global_motion_params
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    if( frame->FrameIsIntra )
        return;
    lsmash_bits_t *bits = parser->bits;
    for( int ref = LAST_FRAME; ref <= ALTREF_FRAME; ref++ )
    {
        uint8_t is_global = (uint8_t)lsmash_bits_get( bits, 1 );
        if( is_global )
        {
            uint8_t is_rot_zoom = (uint8_t)lsmash_bits_get( bits, 1 );
            uint8_t type;
            if( is_rot_zoom )
                type = 2; /* ROTZOOM */
            else
            {
                uint8_t is_translation = (uint8_t)lsmash_bits_get( bits, 1 );
                type = is_translation ? 1 /* TRANSLATION */ : 3 /* AFFINE */;
            }
            if( type >= 2 )
            {
                av1_read_global_param( bits, frame, type, 2 );
                av1_read_global_param( bits, frame, type, 3 );
                if( type == 3 )
                {
                    av1_read_global_param( bits, frame, type, 4 );
                    av1_read_global_param( bits, frame, type, 5 );
                }
            }
            if( type >= 1 )
            {
                av1_read_global_param( bits, frame, type, 0 );
                av1_read_global_param( bits, frame, type, 1 );
            }
        }
    }
}

static int av1_parse_film_grain_params
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    av1_sequence_header_t *sh = &parser->sequence_header;
    if( !sh->film_grain_params_present || (!frame->show_frame && !frame->showable_frame) )
        return 0;
    lsmash_bits_t      *bits = parser->bits;
    av1_color_config_t *cc   = &sh->color_config;
    uint8_t apply_grain = (uint8_t)lsmash_bits_get( bits, 1 );
    if( !apply_grain )
        return 0;
    lsmash_bits_get( bits, 16 ); /* grain_seed */
    uint8_t update_grain = (frame->frame_type == AV1_INTER_FRAME) ? (uint8_t)lsmash_bits_get( bits, 1 ) : 1;
    if( !update_grain )
    {
        lsmash_bits_get( bits, 3 ); /* film_grain_params_ref_idx */
        return 0;
    }
    uint8_t num_y_points = (uint8_t)lsmash_bits_get( bits, 4 );
    IF_OUT_OF_SPEC( num_y_points > 14 )
        return LSMASH_ERR_INVALID_DATA;
    for( uint8_t i = 0; i < num_y_points; i++ )
    {
        lsmash_bits_get( bits, 8 ); /* point_y_value[i] */
        lsmash_bits_get( bits, 8 ); /* point_y_scaling[i] */
    }
    uint8_t chroma_scaling_from_luma = cc->mono_chrome ? 0 : (uint8_t)lsmash_bits_get( bits, 1 );
    uint8_t num_cb_points = 0;
    uint8_t num_cr_points = 0;
    if( !cc->mono_chrome && !chroma_scaling_from_luma
     && !(cc->subsampling_x == 1 && cc->subsampling_y == 1 && num_y_points == 0) )
    {
        num_cb_points = (uint8_t)lsmash_bits_get( bits, 4 );
        IF_OUT_OF_SPEC( num_cb_points > 10 )
            return LSMASH_ERR_INVALID_DATA;
        for( uint8_t i = 0; i < num_cb_points; i++ )
        {
            lsmash_bits_get( bits, 8 ); /* point_cb_value[i] */
            lsmash_bits_get( bits, 8 ); /* point_cb_scaling[i] */
        }
        num_cr_points = (uint8_t)lsmash_bits_get( bits, 4 );
        IF_OUT_OF_SPEC( num_cr_points > 10 )
            return LSMASH_ERR_INVALID_DATA;
        for( uint8_t i = 0; i < num_cr_points; i++ )
        {
            lsmash_bits_get( bits, 8 ); /* point_cr_value[i] */
            lsmash_bits_get( bits, 8 ); /* point_cr_scaling[i] */
        }
    }
    lsmash_bits_get( bits, 2 ); /* grain_scaling_minus_8 */
    uint8_t  ar_coeff_lag   = (uint8_t)lsmash_bits_get( bits, 2 );
    uint32_t num_pos_luma   = 2 * ar_coeff_lag * (ar_coeff_lag + 1);
    uint32_t num_pos_chroma = num_pos_luma + (num_y_points > 0 ? 1 : 0);
    if( num_y_points > 0 )
        for( uint32_t i = 0; i < num_pos_luma; i++ )
            lsmash_bits_get( bits, 8 ); /* ar_coeffs_y_plus_128[i] */
    if( chroma_scaling_from_luma || num_cb_points > 0 )
        for( uint32_t i = 0; i < num_pos_chroma; i++ )
            lsmash_bits_get( bits, 8 ); /* ar_coeffs_cb_plus_128[i] */
    if( chroma_scaling_from_luma || num_cr_points > 0 )
        for( uint32_t i = 0; i < num_pos_chroma; i++ )
            lsmash_bits_get( bits, 8 ); /* ar_coeffs_cr_plus_128[i] */
    lsmash_bits_get( bits, 2 ); /* ar_coeff_shift_minus_6 */
    lsmash_bits_get( bits, 2 ); /* grain_scale_shift */
    if( num_cb_points > 0 )
    {
        lsmash_bits_get( bits, 8 ); /* cb_mult */
        lsmash_bits_get( bits, 8 ); /* cb_luma_mult */
        lsmash_bits_get( bits, 9 ); /* cb_offset */
    }
    if( num_cr_points > 0 )
    {
        lsmash_bits_get( bits, 8 ); /* cr_mult */
        lsmash_bits_get( bits, 8 ); /* cr_luma_mult */
        lsmash_bits_get( bits, 9 ); /* cr_offset */
    }
    lsmash_bits_get( bits, 1 ); /* overlap_flag */
    lsmash_bits_get( bits, 1 ); /* clip_to_restricted_range */
    return 0;
}

static int av1_uncompressed_header
(
    av1_parser_t *parser,
    av1_frame_t  *frame,
    int           is_frame_obu,
    uint8_t       temporal_id,
    uint8_t       spatial_id
)
{
    lsmash_bits_t         *bits = parser->bits;
    av1_sequence_header_t *sh   = &parser->sequence_header;
    uint8_t allFrames = (1 << AV1_NUM_REF_FRAMES) - 1;
    uint32_t idLen = sh->frame_id_numbers_present_flag
                   ? (sh->additional_frame_id_length_minus_1 + sh->delta_frame_id_length_minus_2 + 3)
                   : 0;
    if( sh->reduced_still_picture_header )
    {
        frame->show_existing_frame = 0;
        frame->frame_type          = AV1_KEY_FRAME;
        frame->FrameIsIntra        = 1;
        frame->show_frame          = 1;
        frame->showable_frame      = 0;
    }
    else
    {
        frame->show_existing_frame = (uint8_t)lsmash_bits_get( bits, 1 );
        IF_OUT_OF_SPEC( frame->show_existing_frame && is_frame_obu )
            return LSMASH_ERR_INVALID_DATA;
        if( frame->show_existing_frame )
        {
            frame->frame_to_show_map_idx = (uint8_t)lsmash_bits_get( bits, 3 );
            uint8_t idx = frame->frame_to_show_map_idx;
            IF_OUT_OF_SPEC( !parser->RefValid[idx] )
                return LSMASH_ERR_INVALID_DATA;
            if( sh->decoder_model_info_present_flag && !sh->timing_info.equal_picture_interval )
                frame->frame_presentation_time = lsmash_bits_get( bits, sh->decoder_model_info.frame_presentation_time_length_minus_1 + 1 );
            frame->refresh_frame_flags = 0;
            if( sh->frame_id_numbers_present_flag )
            {
                frame->display_frame_id = (uint32_t)lsmash_bits_get( bits, idLen );
                IF_OUT_OF_SPEC( frame->display_frame_id != parser->RefFrameId[idx] )
                    return LSMASH_ERR_INVALID_DATA;
            }
            frame->frame_type   = parser->RefFrameType[idx];
            frame->FrameIsIntra = (frame->frame_type == AV1_INTRA_ONLY_FRAME || frame->frame_type == AV1_KEY_FRAME);
            frame->show_frame   = 0;
            if( frame->frame_type == AV1_KEY_FRAME )
            {
                frame->refresh_frame_flags = allFrames;
                frame->current_frame_id    = parser->RefFrameId[idx];
                memcpy( frame->FeatureEnabled, parser->RefFeatureEnabled[idx], sizeof(frame->FeatureEnabled) );
                memcpy( frame->FeatureData,    parser->RefFeatureData   [idx], sizeof(frame->FeatureData) );
            }
            frame->UpscaledWidth = parser->RefUpscaledWidth[idx];
            frame->FrameWidth    = parser->RefFrameWidth   [idx];
            frame->FrameHeight   = parser->RefFrameHeight  [idx];
            frame->RenderWidth   = parser->RefRenderWidth  [idx];
            frame->RenderHeight  = parser->RefRenderHeight [idx];
            frame->OrderHint     = parser->RefOrderHint    [idx];
            if( bits->bs->eob || bits->bs->error )
                return LSMASH_ERR_INVALID_DATA;
            return 0;
        }
        frame->frame_type   = (uint8_t)lsmash_bits_get( bits, 2 );
        frame->FrameIsIntra = (frame->frame_type == AV1_INTRA_ONLY_FRAME || frame->frame_type == AV1_KEY_FRAME);
        frame->show_frame   = (uint8_t)lsmash_bits_get( bits, 1 );
        if( frame->show_frame && sh->decoder_model_info_present_flag && !sh->timing_info.equal_picture_interval )
            frame->frame_presentation_time = lsmash_bits_get( bits, sh->decoder_model_info.frame_presentation_time_length_minus_1 + 1 );
        frame->showable_frame = frame->show_frame ? (frame->frame_type != AV1_KEY_FRAME) : (uint8_t)lsmash_bits_get( bits, 1 );
        if( frame->frame_type == AV1_SWITCH_FRAME
         || (frame->frame_type == AV1_KEY_FRAME && frame->show_frame) )
            frame->error_resilient_mode = 1;
        else
            frame->error_resilient_mode = (uint8_t)lsmash_bits_get( bits, 1 );
    }
    if( frame->frame_type == AV1_KEY_FRAME && frame->show_frame )
    {
        for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
        {
            parser->RefValid    [i] = 0;
            parser->RefOrderHint[i] = 0;
        }
        for( int i = 0; i < AV1_REFS_PER_FRAME; i++ )
            frame->OrderHints[LAST_FRAME + i] = 0;
    }
    frame->disable_cdf_update = (uint8_t)lsmash_bits_get( bits, 1 );
    if( sh->seq_force_screen_content_tools == SELECT_SCREEN_CONTENT_TOOLS )
        frame->allow_screen_content_tools = (uint8_t)lsmash_bits_get( bits, 1 );
    else
        frame->allow_screen_content_tools = sh->seq_force_screen_content_tools;
    if( frame->allow_screen_content_tools )
    {
        if( sh->seq_force_integer_mv == SELECT_INTEGER_MV )
            frame->force_integer_mv = (uint8_t)lsmash_bits_get( bits, 1 );
        else
            frame->force_integer_mv = sh->seq_force_integer_mv;
    }
    else
        frame->force_integer_mv = 0;
    if( frame->FrameIsIntra )
        frame->force_integer_mv = 1;
    if( sh->frame_id_numbers_present_flag )
    {
        frame->current_frame_id = (uint32_t)lsmash_bits_get( bits, idLen );
        /* mark_ref_frames( idLen ) (AV1 Spec Section 5.9.4) */
        uint32_t diffLen = sh->delta_frame_id_length_minus_2 + 2;
        for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
        {
            if( frame->current_frame_id > (1u << diffLen) )
            {
                if( parser->RefFrameId[i] > frame->current_frame_id
                 || parser->RefFrameId[i] < (frame->current_frame_id - (1u << diffLen)) )
                    parser->RefValid[i] = 0;
            }
            else
            {
                if( parser->RefFrameId[i] > frame->current_frame_id
                 && parser->RefFrameId[i] < ((1u << idLen) + frame->current_frame_id - (1u << diffLen)) )
                    parser->RefValid[i] = 0;
            }
        }
    }
    else
        frame->current_frame_id = 0;
    if( frame->frame_type == AV1_SWITCH_FRAME )
        frame->frame_size_override_flag = 1;
    else if( sh->reduced_still_picture_header )
        frame->frame_size_override_flag = 0;
    else
        frame->frame_size_override_flag = (uint8_t)lsmash_bits_get( bits, 1 );
    if( sh->enable_order_hint )
        frame->order_hint = (uint32_t)lsmash_bits_get( bits, sh->OrderHintBits );
    else
        frame->order_hint = 0;
    frame->OrderHint = frame->order_hint;
    if( frame->FrameIsIntra || frame->error_resilient_mode )
        frame->primary_ref_frame = PRIMARY_REF_NONE;
    else
        frame->primary_ref_frame = (uint8_t)lsmash_bits_get( bits, 3 );
    if( sh->decoder_model_info_present_flag )
    {
        frame->buffer_removal_time_present_flag = (uint8_t)lsmash_bits_get( bits, 1 );
        if( frame->buffer_removal_time_present_flag )
            for( uint8_t opNum = 0; opNum <= sh->operating_points_cnt_minus_1; opNum++ )
            {
                if( sh->op[opNum].decoder_model_present_for_this_op )
                {
                    uint16_t opPtIdc    = sh->op[opNum].operating_point_idc;
                    int inTemporalLayer = (opPtIdc >> temporal_id)      & 1;
                    int inSpatialLayer  = (opPtIdc >> (spatial_id + 8)) & 1;
                    if( opPtIdc == 0 || (inTemporalLayer && inSpatialLayer) )
                    {
                        uint32_t n = sh->decoder_model_info.buffer_removal_time_length_minus_1 + 1;
                        frame->buffer_removal_time[opNum] = lsmash_bits_get( bits, n );
                    }
                }
            }
    }
    if( frame->frame_type == AV1_SWITCH_FRAME || (frame->frame_type == AV1_KEY_FRAME && frame->show_frame) )
        frame->refresh_frame_flags = allFrames;
    else
        frame->refresh_frame_flags = (uint8_t)lsmash_bits_get( bits, 8 );
    if( !frame->FrameIsIntra || frame->refresh_frame_flags != allFrames )
    {
        if( frame->error_resilient_mode && sh->enable_order_hint )
            for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
            {
                frame->ref_order_hint[i] = (uint32_t)lsmash_bits_get( bits, sh->OrderHintBits );
                if( frame->ref_order_hint[i] != parser->RefOrderHint[i] )
                    parser->RefValid[i] = 0;
            }
    }
    int err;
    if( frame->frame_type == AV1_KEY_FRAME )
    {
        if( (err = av1_parse_frame_size( parser, frame ))  < 0
         || (err = av1_parse_render_size( parser, frame )) < 0 )
            return err;
        if( frame->allow_screen_content_tools && frame->UpscaledWidth == frame->FrameWidth )
            frame->allow_intrabc = (uint8_t)lsmash_bits_get( bits, 1 );
        else
            frame->allow_intrabc = 0;
    }
    else
    {
        if( frame->frame_type == AV1_INTRA_ONLY_FRAME )
        {
            if( (err = av1_parse_frame_size( parser, frame ))  < 0
             || (err = av1_parse_render_size( parser, frame )) < 0 )
                return err;
            if( frame->allow_screen_content_tools && frame->UpscaledWidth == frame->FrameWidth )
                frame->allow_intrabc = (uint8_t)lsmash_bits_get( bits, 1 );
            else
                frame->allow_intrabc = 0;
        }
        else
        {
            frame->allow_intrabc = 0;
            if( sh->enable_order_hint )
            {
                frame->frame_refs_short_signaling = (uint8_t)lsmash_bits_get( bits, 1 );
                if( frame->frame_refs_short_signaling )
                {
                    frame->last_frame_idx = (uint8_t)lsmash_bits_get( bits, 3 );
                    frame->gold_frame_idx = (uint8_t)lsmash_bits_get( bits, 3 );
                    if( (err = av1_set_frame_refs( parser, frame )) < 0 )
                        return err;
                }
            }
            else
                frame->frame_refs_short_signaling = 0;
            for( int i = 0; i < AV1_REFS_PER_FRAME; i++ )
            {
                if( !frame->frame_refs_short_signaling )
                    frame->ref_frame_idx[i] = (int)lsmash_bits_get( bits, 3 );
                if( sh->frame_id_numbers_present_flag )
                {
                    uint32_t n = sh->delta_frame_id_length_minus_2 + 2;
                    frame->delta_frame_id_minus_1 = (uint32_t)lsmash_bits_get( bits, n );
                    uint32_t DeltaFrameId    = frame->delta_frame_id_minus_1 + 1;
                    uint32_t expectedFrameId = (frame->current_frame_id + (1u << idLen) - DeltaFrameId) & ((1u << idLen) - 1);
                    int idx = frame->ref_frame_idx[i];
                    IF_OUT_OF_SPEC( idx < 0 || idx >= AV1_NUM_REF_FRAMES
                                 || !parser->RefValid[idx]
                                 || parser->RefFrameId[idx] != expectedFrameId )
                        return LSMASH_ERR_INVALID_DATA;
                }
            }
            if( frame->frame_size_override_flag && !frame->error_resilient_mode )
            {
                if( (err = av1_parse_frame_size_with_refs( parser, frame )) < 0 )
                    return err;
            }
            else
            {
                if( (err = av1_parse_frame_size( parser, frame ))  < 0
                 || (err = av1_parse_render_size( parser, frame )) < 0 )
                    return err;
            }
            frame->allow_high_precision_mv = frame->force_integer_mv ? 0 : (uint8_t)lsmash_bits_get( bits, 1 );
            av1_read_interpolation_filter( bits, frame );
            frame->is_motion_mode_switchable = (uint8_t)lsmash_bits_get( bits, 1 );
            if( frame->error_resilient_mode || !sh->enable_ref_frame_mvs )
                frame->use_ref_frame_mvs = 0;
            else
                frame->use_ref_frame_mvs = (uint8_t)lsmash_bits_get( bits, 1 );
        }
    }
    if( !frame->FrameIsIntra )
        for( int i = 0; i < AV1_REFS_PER_FRAME; i++ )
        {
            int refFrame = LAST_FRAME + i;
            int idx      = frame->ref_frame_idx[i];
            IF_OUT_OF_SPEC( idx < 0 || idx >= AV1_NUM_REF_FRAMES )
                return LSMASH_ERR_INVALID_DATA;
            uint32_t hint = parser->RefOrderHint[idx];
            frame->OrderHints[refFrame] = hint;
            if( !sh->enable_order_hint )
                frame->RefFrameSignBias[refFrame] = 0;
            else
                frame->RefFrameSignBias[refFrame] = av1_get_relative_dist( sh, (int)hint, (int)frame->OrderHint ) > 0;
        }
    if( sh->reduced_still_picture_header || frame->disable_cdf_update )
        frame->disable_frame_end_update_cdf = 1;
    else
        frame->disable_frame_end_update_cdf = (uint8_t)lsmash_bits_get( bits, 1 );
    if( (err = av1_parse_tile_info( parser, frame )) < 0 )
        return err;
    av1_parse_quantization_params( parser, frame );
    av1_parse_segmentation_params( parser, frame );
    av1_parse_delta_q_params( parser, frame );
    av1_parse_delta_lf_params( parser, frame );
    frame->CodedLossless = 1;
    for( int i = 0; i < AV1_MAX_SEGMENTS; i++ )
    {
        int qindex = frame->base_q_idx;
        if( frame->segmentation_enabled && frame->FeatureEnabled[i][0 /* SEG_LVL_ALT_Q */] )
        {
            qindex += frame->FeatureData[i][0];
            qindex  = LSMASH_MIN( LSMASH_MAX( qindex, 0 ), 255 );
        }
        frame->LosslessArray[i] = (qindex == 0
                                && frame->DeltaQYDc == 0
                                && frame->DeltaQUAc == 0 && frame->DeltaQUDc == 0
                                && frame->DeltaQVAc == 0 && frame->DeltaQVDc == 0);
        if( !frame->LosslessArray[i] )
            frame->CodedLossless = 0;
    }
    frame->AllLossless = frame->CodedLossless && (frame->FrameWidth == frame->UpscaledWidth);
    av1_parse_loop_filter_params( parser, frame );
    av1_parse_cdef_params( parser, frame );
    av1_parse_lr_params( parser, frame );
    if( !frame->CodedLossless )
        lsmash_bits_get( bits, 1 ); /* tx_mode_select */
    frame->reference_select = frame->FrameIsIntra ? 0 : (uint8_t)lsmash_bits_get( bits, 1 );
    av1_parse_skip_mode_params( parser, frame );
    if( frame->FrameIsIntra || frame->error_resilient_mode || !sh->enable_warped_motion )
        frame->allow_warped_motion = 0;
    else
        frame->allow_warped_motion = (uint8_t)lsmash_bits_get( bits, 1 );
    frame->reduced_tx_set = (uint8_t)lsmash_bits_get( bits, 1 );
    av1_parse_global_motion_params( parser, frame );
    if( (err = av1_parse_film_grain_params( parser, frame )) < 0 )
        return err;
    if( bits->bs->eob || bits->bs->error )
        return LSMASH_ERR_INVALID_DATA;
    return 0;
}

static int av1_decode_frame_wrapup
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    /* 7.20. Reference frame update process */
    for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
        if( (frame->refresh_frame_flags >> i) & 1 )
        {
            parser->RefValid        [i] = 1;
            parser->RefFrameId      [i] = frame->current_frame_id;
            parser->RefUpscaledWidth[i] = frame->UpscaledWidth;
            parser->RefFrameWidth   [i] = frame->FrameWidth;
            parser->RefFrameHeight  [i] = frame->FrameHeight;
            parser->RefRenderWidth  [i] = frame->RenderWidth;
            parser->RefRenderHeight [i] = frame->RenderHeight;
            parser->RefOrderHint    [i] = frame->OrderHint;
            parser->RefFrameType    [i] = frame->frame_type;
            memcpy( parser->RefFeatureEnabled[i], frame->FeatureEnabled, sizeof(frame->FeatureEnabled) );
            memcpy( parser->RefFeatureData   [i], frame->FeatureData,    sizeof(frame->FeatureData) );
        }
    return 0;
}

static int av1_parse_frame_header
(
    av1_parser_t *parser,
    av1_frame_t  *frame,
    int           is_redundant_header,
    int           is_frame_obu,
    uint8_t       temporal_id,
    uint8_t       spatial_id
)
{
    if( frame->SeenFrameHeader )
    {
        if( !is_redundant_header )
            return LSMASH_ERR_INVALID_DATA;
    }
    else
    {
        if( is_redundant_header )
            return LSMASH_ERR_INVALID_DATA;
        frame->SeenFrameHeader = 1;
        int err = av1_uncompressed_header( parser, frame, is_frame_obu, temporal_id, spatial_id );
        if( err < 0 )
            return err;
        if( frame->show_existing_frame )
        {
            av1_decode_frame_wrapup( parser, frame );
            frame->SeenFrameHeader = 0;
        }
        else
        {
            frame->TileNum         = 0;
            frame->SeenFrameHeader = 1;
        }
    }
    return 0;
}

static int av1_parse_tile_group
(
    av1_parser_t *parser,
    av1_frame_t  *frame,
    int           is_frame_obu
)
{
    (void)is_frame_obu;
    lsmash_bits_t *bits = parser->bits;
    if( !frame->SeenFrameHeader )
        return LSMASH_ERR_INVALID_DATA;
    frame->NumTiles = frame->TileCols * frame->TileRows;
    IF_OUT_OF_SPEC( frame->NumTiles == 0 )
        return LSMASH_ERR_INVALID_DATA;
    frame->tile_start_and_end_present_flag = 0;
    if( frame->NumTiles > 1 )
        frame->tile_start_and_end_present_flag = (uint8_t)lsmash_bits_get( bits, 1 );
    uint32_t tg_start;
    uint32_t tg_end;
    if( frame->NumTiles == 1 || !frame->tile_start_and_end_present_flag )
    {
        tg_start = 0;
        tg_end   = frame->NumTiles - 1;
    }
    else
    {
        uint32_t tileBits = frame->TileColsLog2 + frame->TileRowsLog2;
        tg_start = (uint32_t)lsmash_bits_get( bits, tileBits );
        tg_end   = (uint32_t)lsmash_bits_get( bits, tileBits );
    }
    IF_OUT_OF_SPEC( tg_start != frame->TileNum || tg_end < tg_start || tg_end >= frame->NumTiles )
        return LSMASH_ERR_INVALID_DATA;
    lsmash_bits_get_align( bits );
    IF_OUT_OF_SPEC( bits->bs->eob || bits->bs->error || lsmash_bs_get_remaining_buffer_size( bits->bs ) == 0 )
        return LSMASH_ERR_INVALID_DATA;
    frame->TileNum = tg_end + 1;
    if( tg_end == frame->NumTiles - 1 )
    {
        av1_decode_frame_wrapup( parser, frame );
        frame->SeenFrameHeader = 0;
    }
    return 0;
}

static int av1_parse_frame
(
    av1_parser_t *parser,
    av1_frame_t  *frame,
    int           is_redundant_header,
    int           is_frame_obu,
    uint8_t       temporal_id,
    uint8_t       spatial_id
)
{
    assert( is_frame_obu == 1 );
    int err = av1_parse_frame_header( parser, frame, is_redundant_header, 1, temporal_id, spatial_id );
    if( err < 0 )
        return err;
    if( !frame->show_existing_frame )
    {
        lsmash_bits_get_align( parser->bits );
        return av1_parse_tile_group( parser, frame, 1 );
    }
    return 0;
}

static void av1_parse_metadata_obu
(
    av1_parser_t *parser,
    const uint8_t *payload,
    uint32_t       payload_size
)
{
    if( !parser || !payload || payload_size == 0 )
        return;
    lsmash_bs_t bs = { 0 };
    if( lsmash_bs_set_empty_stream( &bs, (uint8_t *)payload, payload_size ) < 0 )
        return;
    uint8_t Leb128Bytes = 0;
    uint64_t metadata_type = lsm_av1_get_leb128( &bs, &Leb128Bytes );
    if( Leb128Bytes == 0 || payload_size <= Leb128Bytes )
        return;
    if( metadata_type == AV1_METADATA_TYPE_ITUT_T35 )
    {
        uint32_t rem = payload_size - Leb128Bytes;
        const uint8_t *p = payload + Leb128Bytes;
        /* ITU-T T.35 HDR10+ dynamic metadata header (ST 2094-40):
         * itu_t_t35_country_code == 0xB5 (1 byte)
         * itu_t_t35_terminal_provider_code == 0x003C (2 bytes BE)
         * itu_t_t35_terminal_provider_oriented_code == 0x0001 (2 bytes BE)
         * application_identifier == 4 (1 byte)
         * application_version == 1 (1 byte) */
        if( rem >= 7
         && p[0] == 0xB5
         && p[1] == 0x00 && p[2] == 0x3C
         && p[3] == 0x00 && p[4] == 0x01
         && p[5] == 0x04 && p[6] == 0x01 )
            parser->has_hdr10p = 1;
    }
}

static void av1_populate_specific_parameters_from_sequence_header
(
    lsmash_av1_specific_parameters_t *param,
    const av1_sequence_header_t      *sh
)
{
    param->seq_profile            = sh->seq_profile;
    param->seq_level_idx_0        = sh->op[0].seq_level_idx;
    param->seq_tier_0             = sh->op[0].seq_tier;
    param->high_bitdepth          = sh->color_config.high_bitdepth;
    param->twelve_bit             = sh->color_config.twelve_bit;
    param->monochrome             = sh->color_config.mono_chrome;
    param->chroma_subsampling_x   = sh->color_config.subsampling_x;
    param->chroma_subsampling_y   = sh->color_config.subsampling_y;
    param->chroma_sample_position = sh->color_config.chroma_sample_position;
    if( sh->initial_display_delay_present_flag && sh->op[0].initial_display_delay_present_for_this_op )
    {
        param->initial_presentation_delay_present   = 1;
        param->initial_presentation_delay_minus_one = sh->op[0].initial_display_delay_minus_1;
    }
    else
    {
        param->initial_presentation_delay_present   = 0;
        param->initial_presentation_delay_minus_one = 0;
    }
}

static int av1_is_sample_obu
(
    uint8_t obu_type
)
{
    /* AV1-ISOBMFF v1.2.0 Section 2.2: Sample Format whitelist */
    switch( obu_type )
    {
        case AV1_OBU_TYPE_SEQUENCE_HEADER :
        case AV1_OBU_TYPE_FRAME_HEADER :
        case AV1_OBU_TYPE_TILE_GROUP :
        case AV1_OBU_TYPE_METADATA :
        case AV1_OBU_TYPE_FRAME :
            return 1;
        default :
            return 0;
    }
}

int lsm_av1_parse_temporal_unit
(
    av1_parser_t                     *parser,
    const uint8_t                    *src_data,
    uint32_t                          src_length,
    uint8_t                          *dst_data,
    uint32_t                         *dst_length,
    lsmash_sample_property_t         *prop,
    lsmash_av1_specific_parameters_t *param
)
{
    if( !parser || !parser->bits || !src_data || src_length == 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_bs_t bs = { 0 };
    int err = lsmash_bs_set_empty_stream( &bs, (uint8_t *)src_data, src_length );
    if( err < 0 )
        return err;
    uint32_t remaining_bytes = src_length;
    uint32_t out_offset      = 0;
    av1_temporal_unit_t tu = { .temporal_id = 1 << 3 };
    while( remaining_bytes > 0 )
    {
        uint8_t temp8 = lsmash_bs_show_byte( &bs, 0 );
        uint8_t obu_forbidden_bit  = (temp8 >> 7) & 0x01;
        uint8_t obu_type           = (temp8 >> 3) & 0x0F;
        uint8_t obu_extension_flag = (temp8 >> 2) & 0x01;
        uint8_t obu_has_size_field = (temp8 >> 1) & 0x01;
        uint8_t obu_reserved_1bit  =  temp8       & 0x01;
        if( obu_forbidden_bit != 0 || obu_reserved_1bit != 0 )
            return LSMASH_ERR_INVALID_DATA;
        if( obu_type == AV1_OBU_TYPE_TILE_LIST )
            /* Tile list OBU is not supported in AV1-ISOBMFF v1.2.0. */
            return LSMASH_ERR_INVALID_DATA;
        uint32_t obu_header_size = 1 + obu_extension_flag;
        if( remaining_bytes < obu_header_size )
            return LSMASH_ERR_INVALID_DATA;
        uint8_t temporal_id = 0;
        uint8_t spatial_id  = 0;
        if( obu_extension_flag )
        {
            uint8_t ext_byte = lsmash_bs_show_byte( &bs, 1 );
            temporal_id = (ext_byte >> 5) & 0x07;
            spatial_id  = (ext_byte >> 3) & 0x03;
            uint8_t extension_header_reserved_3bits = ext_byte & 0x07;
            if( extension_header_reserved_3bits != 0 )
                return LSMASH_ERR_INVALID_DATA;
        }
        if( !obu_has_size_field )
            return LSMASH_ERR_INVALID_DATA;
        uint8_t Leb128Bytes = 0;
        uint64_t obu_size64 = lsm_av1_show_leb128( &bs, obu_header_size, &Leb128Bytes );
        if( Leb128Bytes == 0
         || obu_header_size + Leb128Bytes > remaining_bytes
         || obu_size64 > remaining_bytes - (obu_header_size + Leb128Bytes) )
            return LSMASH_ERR_INVALID_DATA;
        uint32_t obu_size = (uint32_t)obu_size64;
        uint32_t sz       = obu_header_size + Leb128Bytes + obu_size;
        const uint8_t *obu_data    = src_data + (src_length - remaining_bytes);
        const uint8_t *obu_payload = obu_data + obu_header_size + Leb128Bytes;
        lsmash_bs_skip_bytes( &bs, sz );
        remaining_bytes -= sz;
        int keep_obu = av1_is_sample_obu( obu_type );
        switch( obu_type )
        {
            case AV1_OBU_TYPE_TEMPORAL_DELIMITER :
                IF_OUT_OF_SPEC( obu_size != 0 || parser->frame.SeenFrameHeader != 0 )
                    return LSMASH_ERR_INVALID_DATA;
                parser->frame.SeenFrameHeader = 0;
                break;
            case AV1_OBU_TYPE_PADDING :
                break;
            case AV1_OBU_TYPE_SEQUENCE_HEADER :
            {
                IF_OUT_OF_SPEC( parser->frame.SeenFrameHeader != 0 )
                    return LSMASH_ERR_INVALID_DATA;
                lsmash_bs_t obu_bs = { 0 };
                if( (err = lsmash_bs_set_empty_stream( &obu_bs, (uint8_t *)obu_payload, obu_size )) < 0 )
                    return err;
                lsmash_bs_t *orig_bs = parser->bits->bs;
                lsmash_bits_init( parser->bits, &obu_bs );
                err = lsm_av1_parse_sequence_header( parser );
                parser->bits->bs    = orig_bs;
                parser->bits->store = 0;
                parser->bits->cache = 0;
                if( err < 0 )
                    return err;
                parser->seen_seq_header = 1;
                if( !tu.seen_frame_header )
                    tu.with_sequence_header = 1;
                if( param )
                {
                    av1_populate_specific_parameters_from_sequence_header( param, &parser->sequence_header );
                    if( !param->configOBUs.data )
                    {
                        param->configOBUs.data = lsmash_memdup( obu_data, sz );
                        if( !param->configOBUs.data )
                            return LSMASH_ERR_MEMORY_ALLOC;
                        param->configOBUs.sz     = sz;
                        tu.recording_config_obus = 1;
                    }
                }
                break;
            }
            case AV1_OBU_TYPE_METADATA :
            {
                if( !parser->seen_seq_header )
                    return LSMASH_ERR_INVALID_DATA;
                av1_parse_metadata_obu( parser, obu_payload, obu_size );
                if( param )
                {
                    if( parser->has_hdr10p )
                        param->has_hdr10p = 1;
                    if( tu.recording_config_obus )
                    {
                        uint8_t *temp = lsmash_realloc( param->configOBUs.data, param->configOBUs.sz + sz );
                        if( !temp )
                            return LSMASH_ERR_MEMORY_ALLOC;
                        param->configOBUs.data = temp;
                        memcpy( param->configOBUs.data + param->configOBUs.sz, obu_data, sz );
                        param->configOBUs.sz += sz;
                    }
                }
                break;
            }
            case AV1_OBU_TYPE_FRAME_HEADER :
            case AV1_OBU_TYPE_REDUNDANT_FRAME_HEADER :
            case AV1_OBU_TYPE_FRAME :
            {
                if( !parser->seen_seq_header )
                    return LSMASH_ERR_INVALID_DATA;
                if( tu.temporal_id == (1 << 3) )
                    tu.temporal_id = temporal_id;
                else if( tu.temporal_id != temporal_id )
                    return LSMASH_ERR_INVALID_DATA;
                int is_redundant_header = (obu_type == AV1_OBU_TYPE_REDUNDANT_FRAME_HEADER);
                int is_frame_obu        = (obu_type == AV1_OBU_TYPE_FRAME);
                lsmash_bs_t obu_bs = { 0 };
                if( (err = lsmash_bs_set_empty_stream( &obu_bs, (uint8_t *)obu_payload, obu_size )) < 0 )
                    return err;
                lsmash_bs_t *orig_bs = parser->bits->bs;
                lsmash_bits_init( parser->bits, &obu_bs );
                int (*parse_func)( av1_parser_t *, av1_frame_t *, int, int, uint8_t, uint8_t ) =
                    obu_type != AV1_OBU_TYPE_FRAME ? av1_parse_frame_header
                                                   : av1_parse_frame;
                err = parse_func( parser, &parser->frame, is_redundant_header, is_frame_obu, temporal_id, spatial_id );
                parser->bits->bs    = orig_bs;
                parser->bits->store = 0;
                parser->bits->cache = 0;
                if( err < 0 )
                    return err;
                parser->MaxRenderWidth  = LSMASH_MAX( parser->MaxRenderWidth,  parser->frame.RenderWidth );
                parser->MaxRenderHeight = LSMASH_MAX( parser->MaxRenderHeight, parser->frame.RenderHeight );
                if( !tu.seen_frame_header )
                {
                    tu.seen_frame_header   = 1;
                    tu.first_frame_type    = parser->frame.frame_type;
                    tu.first_show_frame    = parser->frame.show_frame;
                    tu.first_show_existing = parser->frame.show_existing_frame;
                }
                break;
            }
            case AV1_OBU_TYPE_TILE_GROUP :
            {
                if( !parser->seen_seq_header )
                    return LSMASH_ERR_INVALID_DATA;
                lsmash_bs_t obu_bs = { 0 };
                if( (err = lsmash_bs_set_empty_stream( &obu_bs, (uint8_t *)obu_payload, obu_size )) < 0 )
                    return err;
                lsmash_bs_t *orig_bs = parser->bits->bs;
                lsmash_bits_init( parser->bits, &obu_bs );
                err = av1_parse_tile_group( parser, &parser->frame, 0 );
                parser->bits->bs    = orig_bs;
                parser->bits->store = 0;
                parser->bits->cache = 0;
                if( err < 0 )
                    return err;
                break;
            }
            default :
                break;
        }
        if( keep_obu )
        {
            if( dst_data )
                memcpy( dst_data + out_offset, obu_data, sz );
            out_offset += sz;
        }
    }
    IF_OUT_OF_SPEC( parser->frame.SeenFrameHeader != 0 )
        return LSMASH_ERR_INVALID_DATA;
    if( dst_length )
        *dst_length = out_offset;
    if( prop )
    {
        prop->ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
        if( tu.seen_frame_header && !tu.first_show_existing )
        {
            if( tu.first_frame_type == AV1_KEY_FRAME && tu.first_show_frame && tu.with_sequence_header )
                prop->ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            else if( tu.first_frame_type == AV1_KEY_FRAME
                  || tu.first_frame_type == AV1_INTRA_ONLY_FRAME
                  || tu.first_frame_type == AV1_SWITCH_FRAME )
                prop->ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP;
        }
    }
    return 0;
}

void lsmash_destroy_av1_headers
(
    lsmash_av1_specific_parameters_t *param
)
{
    if( !param )
        return;
    lsmash_freep( &param->configOBUs.data );
    param->configOBUs.sz = 0;
}

void lsm_av1_destruct_specific_data
(
    void *data
)
{
    if( !data )
        return;
    lsmash_destroy_av1_headers( data );
    lsmash_free( data );
}

int lsmash_setup_av1_specific_parameters_from_access_unit
(
    lsmash_av1_specific_parameters_t *param,
    uint8_t                          *data,
    uint32_t                          data_length
)
{
    if( !param || !data || data_length == 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_destroy_av1_headers( param );
    memset( param, 0, sizeof(lsmash_av1_specific_parameters_t) );
    av1_parser_t parser;
    int err = lsm_av1_setup_parser( &parser, NULL );
    if( err < 0 )
        return err;
    err = lsm_av1_parse_temporal_unit( &parser, data, data_length, NULL, NULL, NULL, param );
    int seen_seq = parser.seen_seq_header;
    lsm_av1_cleanup_parser( &parser );
    if( err < 0 )
    {
        lsmash_destroy_av1_headers( param );
        return err;
    }
    if( !seen_seq )
    {
        lsmash_destroy_av1_headers( param );
        return LSMASH_ERR_INVALID_DATA;
    }
    return 0;
}

uint8_t *lsmash_create_av1_specific_info
(
    lsmash_av1_specific_parameters_t *param,
    uint32_t                         *data_length
)
{
    if( !param || !data_length )
        return NULL;
    if( param->configOBUs.sz != 0 && !param->configOBUs.data )
        return NULL;
    /* Create an AV1CodecConfigurationBox */
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
        return NULL;
    lsmash_bs_put_be32( bs, 0 );                            /* box size */
    lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_AV1C.fourcc );    /* box type: 'av1C' */
    uint8_t temp8;
    temp8 = (AV1_CODEC_CONFIGURATION_RECORD_MARKER << 7)
          |  AV1_CODEC_CONFIGURATION_RECORD_VERSION_1;
    lsmash_bs_put_byte( bs, temp8 );
    temp8 = (param->seq_profile << 5)
          | (param->seq_level_idx_0 & 0x1F);
    lsmash_bs_put_byte( bs, temp8 );
    temp8 = ((param->seq_tier_0           << 7) & (1 << 7))
          | ((param->high_bitdepth        << 6) & (1 << 6))
          | ((param->twelve_bit           << 5) & (1 << 5))
          | ((param->monochrome           << 4) & (1 << 4))
          | ((param->chroma_subsampling_x << 3) & (1 << 3))
          | ((param->chroma_subsampling_y << 2) & (1 << 2))
          | ( param->chroma_sample_position     & 0x03);
    lsmash_bs_put_byte( bs, temp8 );
    if( param->initial_presentation_delay_present )
        lsmash_bs_put_byte( bs, 0x10 | (param->initial_presentation_delay_minus_one & 0x0F) );
    else
        lsmash_bs_put_byte( bs, 0 );
    /* configOBUs */
    if( param->configOBUs.sz )
        lsmash_bs_put_bytes( bs, param->configOBUs.sz, param->configOBUs.data );
    uint8_t *data = lsmash_bs_export_data( bs, data_length );
    lsmash_bs_cleanup( bs );
    if( !data )
        return NULL;
    /* Update box size. */
    LSMASH_SET_BE32( data, *data_length );
    return data;
}

int lsm_av1_copy_codec_specific
(
    lsmash_codec_specific_t *dst,
    lsmash_codec_specific_t *src
)
{
    assert( src && src->format == LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED && src->data.structured );
    assert( dst && dst->format == LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED && dst->data.structured );
    lsmash_av1_specific_parameters_t *src_data = (lsmash_av1_specific_parameters_t *)src->data.structured;
    lsmash_av1_specific_parameters_t *dst_data = (lsmash_av1_specific_parameters_t *)dst->data.structured;
    lsmash_destroy_av1_headers( dst_data );
    *dst_data = *src_data;
    dst_data->configOBUs.data = NULL;
    dst_data->configOBUs.sz   = 0;
    if( !src_data->configOBUs.data || src_data->configOBUs.sz == 0 )
        return 0;
    dst_data->configOBUs.data = lsmash_memdup( src_data->configOBUs.data, src_data->configOBUs.sz );
    if( !dst_data->configOBUs.data )
        return LSMASH_ERR_MEMORY_ALLOC;
    dst_data->configOBUs.sz = src_data->configOBUs.sz;
    return 0;
}

int lsm_av1_construct_specific_parameters
(
    lsmash_codec_specific_t *dst,
    lsmash_codec_specific_t *src
)
{
    assert( dst && dst->data.structured && src && src->data.unstructured );
    if( src->size < ISOM_BASEBOX_COMMON_SIZE + 4 )
        return LSMASH_ERR_INVALID_DATA;
    lsmash_av1_specific_parameters_t *param = (lsmash_av1_specific_parameters_t *)dst->data.structured;
    lsmash_destroy_av1_headers( param );
    uint8_t *data = src->data.unstructured;
    uint64_t size = LSMASH_GET_BE32( data );
    data += ISOM_BASEBOX_COMMON_SIZE;
    if( size == 1 )
    {
        if( src->size < ISOM_BASEBOX_COMMON_SIZE + 8 + 4 )
            return LSMASH_ERR_INVALID_DATA;
        size = LSMASH_GET_BE64( data );
        data += 8;
    }
    if( size != src->size )
        return LSMASH_ERR_INVALID_DATA;
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
        return LSMASH_ERR_MEMORY_ALLOC;
    uint32_t cr_size = src->size - (uint32_t)(data - src->data.unstructured);
    int err = lsmash_bs_import_data( bs, data, cr_size );
    if( err < 0 )
        goto fail;
    uint8_t temp8 = lsmash_bs_get_byte( bs );
    if( (temp8 >> 7) != AV1_CODEC_CONFIGURATION_RECORD_MARKER )
    {
        /* The marker bit shall be set to 1. */
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }
    if( (temp8 & 0x7F) != AV1_CODEC_CONFIGURATION_RECORD_VERSION_1 )
    {
        /* We don't support 'version' other than 1. */
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }
    temp8 = lsmash_bs_get_byte( bs );
    param->seq_profile     = temp8 >> 5;
    param->seq_level_idx_0 = temp8 & 0x1F;
    temp8 = lsmash_bs_get_byte( bs );
    param->seq_tier_0             =  temp8 >> 7;
    param->high_bitdepth          = (temp8 >> 6) & 0x01;
    param->twelve_bit             = (temp8 >> 5) & 0x01;
    param->monochrome             = (temp8 >> 4) & 0x01;
    param->chroma_subsampling_x   = (temp8 >> 3) & 0x01;
    param->chroma_subsampling_y   = (temp8 >> 2) & 0x01;
    param->chroma_sample_position =  temp8       & 0x03;
    temp8 = lsmash_bs_get_byte( bs );
    param->initial_presentation_delay_present = (temp8 >> 4) & 0x01;
    if( param->initial_presentation_delay_present )
        param->initial_presentation_delay_minus_one = temp8 & 0x0F;
    else
        param->initial_presentation_delay_minus_one = 0;
    uint32_t config_obus_sz = cr_size - 4;
    if( config_obus_sz )
    {
        param->configOBUs.data = lsmash_bs_get_bytes( bs, config_obus_sz );
        if( !param->configOBUs.data )
        {
            err = LSMASH_ERR_MEMORY_ALLOC;
            goto fail;
        }
        param->configOBUs.sz = config_obus_sz;
    }
    lsmash_bs_cleanup( bs );
    return 0;
fail:
    lsmash_bs_cleanup( bs );
    return err;
}

int lsm_av1_print_codec_specific
(
    FILE          *fp,
    lsmash_file_t *file,
    isom_box_t    *box,
    int            level
)
{
    assert( file && box && (box->manager & LSMASH_BINARY_CODED_BOX) );
    int indent = level;
    lsmash_ifprintf( fp, indent++, "[%s: AV1CodecConfigurationBox]\n", lsm_box_4cc2str( box->type.fourcc ) );
    lsmash_ifprintf( fp, indent, "position = %"PRIu64"\n", box->pos );
    lsmash_ifprintf( fp, indent, "size = %"PRIu64"\n", box->size );
    if( box->size < ISOM_BASEBOX_COMMON_SIZE + 4 )
        return LSMASH_ERR_INVALID_DATA;
    uint8_t *data = box->binary;
    uint32_t offset = lsm_box_skip_common( &data );
    uint8_t marker                             = (data[0] >> 7) & 0x01;
    uint8_t version                            =  data[0]       & 0x7F;
    uint8_t seq_profile                        = (data[1] >> 5) & 0x07;
    uint8_t seq_level_idx_0                    =  data[1]       & 0x1F;
    uint8_t seq_tier_0                         = (data[2] >> 7) & 0x01;
    uint8_t high_bitdepth                      = (data[2] >> 6) & 0x01;
    uint8_t twelve_bit                         = (data[2] >> 5) & 0x01;
    uint8_t monochrome                         = (data[2] >> 4) & 0x01;
    uint8_t chroma_subsampling_x               = (data[2] >> 3) & 0x01;
    uint8_t chroma_subsampling_y               = (data[2] >> 2) & 0x01;
    uint8_t chroma_sample_position             =  data[2]       & 0x03;
    uint8_t reserved                           = (data[3] >> 5) & 0x07;
    uint8_t initial_presentation_delay_present = (data[3] >> 4) & 0x01;
    uint8_t last_4bits                         =  data[3]       & 0x0F;
    if( marker != AV1_CODEC_CONFIGURATION_RECORD_MARKER
     || version != AV1_CODEC_CONFIGURATION_RECORD_VERSION_1 )
        return LSMASH_ERR_INVALID_DATA;
    lsmash_ifprintf( fp, indent, "marker = %"PRIu8"\n", marker );
    lsmash_ifprintf( fp, indent, "version = %"PRIu8"\n", version );
    lsmash_ifprintf( fp, indent, "seq_profile = %"PRIu8"\n", seq_profile );
    lsmash_ifprintf( fp, indent, "seq_level_idx_0 = %"PRIu8"\n", seq_level_idx_0 );
    lsmash_ifprintf( fp, indent, "seq_tier_0 = %"PRIu8"\n", seq_tier_0 );
    lsmash_ifprintf( fp, indent, "high_bitdepth = %"PRIu8"\n", high_bitdepth );
    lsmash_ifprintf( fp, indent, "twelve_bit = %"PRIu8"\n", twelve_bit );
    lsmash_ifprintf( fp, indent, "monochrome = %"PRIu8"\n", monochrome );
    lsmash_ifprintf( fp, indent, "chroma_subsampling_x = %"PRIu8"\n", chroma_subsampling_x );
    lsmash_ifprintf( fp, indent, "chroma_subsampling_y = %"PRIu8"\n", chroma_subsampling_y );
    lsmash_ifprintf( fp, indent, "chroma_sample_position = %s\n", chroma_sample_position == 1 ? "vertical"
                                                                : chroma_sample_position == 2 ? "colocated"
                                                                : chroma_sample_position == 3 ? "reserved (invalid)"
                                                                :                               "unknown" );
    lsmash_ifprintf( fp, indent, "reserved = 0x%01"PRIx8"\n", reserved );
    lsmash_ifprintf( fp, indent, "initial_presentation_delay_present = %"PRIu8"\n", initial_presentation_delay_present );
    if( initial_presentation_delay_present )
        lsmash_ifprintf( fp, indent, "initial_presentation_delay_minus_one = %"PRIu8"\n", last_4bits );
    else
        lsmash_ifprintf( fp, indent, "reserved = 0x%01"PRIx8"\n", last_4bits );
    if( box->size - offset - 4 > 0 )
    {
        lsmash_ifprintf( fp, indent++, "configOBUs\n" );
        lsmash_ifprintf( fp, indent, "size = %"PRIu32"\n", (uint32_t)(box->size - offset - 4) );
    }
    return 0;
}
