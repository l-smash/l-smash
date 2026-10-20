/*****************************************************************************
 * av1.c
 *****************************************************************************
 * Copyright (C) 2017 L-SMASH project
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
            Version 1.0.0
        AV1 Codec ISO Media File Format Binding
            v1.0.0, 7 September 2018
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

uint32_t av1_get_uvlc
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

uint64_t av1_get_leb128
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

uint64_t av1_show_leb128
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

int64_t av1_get_su
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

uint64_t av1_get_ns
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

void av1_cleanup_parser
(
    av1_parser_t *parser
)
{
    if( !parser )
        return;
    lsmash_bits_cleanup( parser->bits );
    parser->bits = NULL;
}

int av1_setup_parser
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

void av1_reset_parser
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
    av1_color_config_t    *cc   = &sh->cc;
    cc->high_bitdepth = (uint8_t)lsmash_bits_get( bits, 1 );
    if( sh->seq_profile == 2 && cc->high_bitdepth )
        cc->twelve_bit = (uint8_t)lsmash_bits_get( bits, 1 );
    else
        cc->twelve_bit = 0; /* The spec does not define this! Anyway 'av1C' requires so do this. */
    cc->mono_chrome = sh->seq_profile == 1 ? 0 : (uint8_t)lsmash_bits_get( bits, 1 );
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

int av1_parse_sequence_header
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
            sh->ti.num_units_in_display_tick = (uint32_t)lsmash_bits_get( bits, 32 );
            sh->ti.time_scale                = (uint32_t)lsmash_bits_get( bits, 32 );
            IF_OUT_OF_SPEC( sh->ti.num_units_in_display_tick == 0 || sh->ti.time_scale == 0 )
                return LSMASH_ERR_INVALID_DATA;
            sh->ti.equal_picture_interval    = (uint8_t)lsmash_bits_get( bits, 1 );
            if( sh->ti.equal_picture_interval )
            {
                sh->ti.num_ticks_per_picture_minus_1 = av1_get_uvlc( bits );
                IF_OUT_OF_SPEC( sh->ti.num_ticks_per_picture_minus_1 == UINT32_MAX )
                    return LSMASH_ERR_INVALID_DATA;
            }
            sh->decoder_model_info_present_flag = (uint8_t)lsmash_bits_get( bits, 1 );
            if( sh->decoder_model_info_present_flag )
            {
                /* decoder_model_info() */
                sh->dmi.buffer_delay_length_minus_1            = (uint8_t)lsmash_bits_get( bits, 5 );
                sh->dmi.num_units_in_decoding_tick             = (uint32_t)lsmash_bits_get( bits, 32 );
                sh->dmi.buffer_removal_time_length_minus_1     = (uint8_t)lsmash_bits_get( bits, 5 );
                sh->dmi.frame_presentation_time_length_minus_1 = (uint8_t)lsmash_bits_get( bits, 5 );
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
                    uint32_t n = sh->dmi.buffer_delay_length_minus_1 + 1;
                    sh->op[i].opi.decoder_buffer_delay = lsmash_bits_get( bits, n );
                    sh->op[i].opi.encoder_buffer_delay = lsmash_bits_get( bits, n );
                    sh->op[i].opi.low_delay_mode_flag  = (uint8_t)lsmash_bits_get( bits, 1 );
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

static inline void av1_read_interpolation_filter
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
            uint32_t width_in_sbs_minus_1 = (uint32_t)av1_get_ns( bits, maxWidth );
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
            uint32_t height_in_sbs_minus_1 = (uint32_t)av1_get_ns( bits, maxHeight );
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
            if( sh->decoder_model_info_present_flag && !sh->ti.equal_picture_interval )
                frame->frame_presentation_time = lsmash_bits_get( bits, sh->dmi.frame_presentation_time_length_minus_1 + 1 );
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
        if( frame->show_frame && sh->decoder_model_info_present_flag && !sh->ti.equal_picture_interval )
            frame->frame_presentation_time = lsmash_bits_get( bits, sh->dmi.frame_presentation_time_length_minus_1 + 1 );
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
        frame->current_frame_id = (uint32_t)lsmash_bits_get( bits, idLen );
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
                        uint32_t n = sh->dmi.buffer_removal_time_length_minus_1 + 1;
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
            frame->SeenFrameHeader = 1;
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
    lsmash_bits_t *bits = parser->bits;
    if( !frame->SeenFrameHeader )
        return LSMASH_ERR_INVALID_DATA;
    frame->NumTiles = frame->TileCols * frame->TileRows;
    IF_OUT_OF_SPEC( frame->NumTiles == 0 )
        return LSMASH_ERR_INVALID_DATA;
    frame->tile_start_and_end_present_flag = 0;
    if( frame->NumTiles > 1 )
    {
        frame->tile_start_and_end_present_flag = (uint8_t)lsmash_bits_get( bits, 1 );
        IF_OUT_OF_SPEC( frame->tile_start_and_end_present_flag && is_frame_obu )
            return LSMASH_ERR_INVALID_DATA;
    }
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
    IF_OUT_OF_SPEC( tg_end < tg_start || tg_end >= frame->NumTiles )
        return LSMASH_ERR_INVALID_DATA;
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
    /* An OBU_FRAME always contains the complete frame (tg_end == NumTiles - 1).
     * Because av1_uncompressed_header() stops after tile_info(), update the reference
     * frame slots directly and reset SeenFrameHeader. */
    av1_decode_frame_wrapup( parser, frame );
    frame->SeenFrameHeader = 0;
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
    uint8_t leb_bytes = 0;
    uint64_t metadata_type = av1_get_leb128( &bs, &leb_bytes );
    if( leb_bytes == 0 || payload_size <= leb_bytes )
        return;
    if( metadata_type == AV1_METADATA_TYPE_ITUT_T35 )
    {
        uint32_t rem = payload_size - leb_bytes;
        const uint8_t *p = payload + leb_bytes;
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
    param->high_bitdepth          = sh->cc.high_bitdepth;
    param->twelve_bit             = sh->cc.twelve_bit;
    param->monochrome             = sh->cc.mono_chrome;
    param->chroma_subsampling_x   = sh->cc.subsampling_x;
    param->chroma_subsampling_y   = sh->cc.subsampling_y;
    param->chroma_sample_position = sh->cc.chroma_sample_position;
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

int av1_parse_temporal_unit
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
            /* Tile list OBU is not supported in AV1-ISOBMFF v1.0.0. */
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
        uint8_t num_leb128bytes = 0;
        uint64_t obu_size64 = av1_show_leb128( &bs, obu_header_size, &num_leb128bytes );
        if( num_leb128bytes == 0
         || obu_header_size + num_leb128bytes > remaining_bytes
         || obu_size64 > remaining_bytes - (obu_header_size + num_leb128bytes) )
            return LSMASH_ERR_INVALID_DATA;
        uint32_t obu_size = (uint32_t)obu_size64;
        uint32_t sz       = obu_header_size + num_leb128bytes + obu_size;
        const uint8_t *obu_data    = src_data + (src_length - remaining_bytes);
        const uint8_t *obu_payload = obu_data + obu_header_size + num_leb128bytes;
        lsmash_bs_skip_bytes( &bs, sz );
        remaining_bytes -= sz;
        int keep_obu = 1;
        switch( obu_type )
        {
            case AV1_OBU_TYPE_TEMPORAL_DELIMITER :
                IF_OUT_OF_SPEC( obu_size != 0 )
                    return LSMASH_ERR_INVALID_DATA;
                parser->frame.SeenFrameHeader = 0;
                keep_obu = 0;
                break;
            case AV1_OBU_TYPE_PADDING :
                keep_obu = 0;
                break;
            case AV1_OBU_TYPE_SEQUENCE_HEADER :
            {
                lsmash_bs_t obu_bs = { 0 };
                if( (err = lsmash_bs_set_empty_stream( &obu_bs, (uint8_t *)obu_payload, obu_size )) < 0 )
                    return err;
                lsmash_bs_t *orig_bs = parser->bits->bs;
                lsmash_bits_init( parser->bits, &obu_bs );
                err = av1_parse_sequence_header( parser );
                parser->bits->bs    = orig_bs;
                parser->bits->store = 0;
                parser->bits->cache = 0;
                if( err < 0 )
                    return err;
                parser->seen_seq_header = 1;
                tu.with_sequence_header = 1;
                if( param )
                {
                    av1_populate_specific_parameters_from_sequence_header( param, &parser->sequence_header );
                    if( !param->configOBUs.data )
                    {
                        param->configOBUs.data = lsmash_memdup( obu_data, sz );
                        if( !param->configOBUs.data )
                            return LSMASH_ERR_MEMORY_ALLOC;
                        param->configOBUs.sz = sz;
                    }
                }
                break;
            }
            case AV1_OBU_TYPE_METADATA :
                av1_parse_metadata_obu( parser, obu_payload, obu_size );
                if( param && parser->has_hdr10p )
                    param->has_hdr10p = 1;
                break;
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
                if( is_redundant_header )
                    keep_obu = 0;
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
    if( dst_length )
        *dst_length = out_offset;
    if( prop )
    {
        prop->ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
        if( tu.seen_frame_header && !tu.first_show_existing )
        {
            if( tu.first_frame_type == AV1_KEY_FRAME && tu.first_show_frame && parser->seen_seq_header )
                prop->ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            else if( tu.first_frame_type == AV1_INTRA_ONLY_FRAME
                  || tu.first_frame_type == AV1_SWITCH_FRAME
                  || (tu.first_frame_type == AV1_KEY_FRAME && !tu.first_show_frame) )
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

void av1_destruct_specific_data
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
    int err = av1_setup_parser( &parser, NULL );
    if( err < 0 )
        return err;
    err = av1_parse_temporal_unit( &parser, data, data_length, NULL, NULL, NULL, param );
    int seen_seq = parser.seen_seq_header;
    av1_cleanup_parser( &parser );
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

int av1_copy_codec_specific
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

int av1_construct_specific_parameters
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

int av1_print_codec_specific
(
    FILE          *fp,
    lsmash_file_t *file,
    isom_box_t    *box,
    int            level
)
{
    assert( box->manager & LSMASH_BINARY_CODED_BOX );
    int indent = level;
    lsmash_ifprintf( fp, indent++, "[%s: AV1 Configuration Box]\n", isom_4cc2str( box->type.fourcc ) );
    lsmash_ifprintf( fp, indent, "position = %"PRIu64"\n", box->pos );
    lsmash_ifprintf( fp, indent, "size = %"PRIu64"\n", box->size );
    if( box->size < ISOM_BASEBOX_COMMON_SIZE + 4 )
        return LSMASH_ERR_INVALID_DATA;
    uint8_t *data = box->binary;
    uint32_t offset = isom_skip_box_common( &data );
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
        return LSMASH_ERR_MEMORY_ALLOC;
    int err = lsmash_bs_import_data( bs, data, box->size - offset );
    if( err < 0 )
    {
        lsmash_bs_cleanup( bs );
        return err;
    }
    uint8_t temp8 = lsmash_bs_get_byte( bs );
    lsmash_ifprintf( fp, indent, "marker = %"PRIu8"\n", (temp8 >> 7) & 0x01 );
    lsmash_ifprintf( fp, indent, "version = %"PRIu8"\n", temp8 & 0x7F );
    temp8 = lsmash_bs_get_byte( bs );
    lsmash_ifprintf( fp, indent, "seq_profile = %"PRIu8"\n", (temp8 >> 5) & 0x07 );
    lsmash_ifprintf( fp, indent, "seq_level_idx_0 = %"PRIu8"\n", temp8 & 0x1F );
    temp8 = lsmash_bs_get_byte( bs );
    lsmash_ifprintf( fp, indent, "seq_tier_0 = %"PRIu8"\n", (temp8 >> 7) & 0x01 );
    lsmash_ifprintf( fp, indent, "high_bitdepth = %"PRIu8"\n", (temp8 >> 6) & 0x01 );
    lsmash_ifprintf( fp, indent, "twelve_bit = %"PRIu8"\n", (temp8 >> 5) & 0x01 );
    lsmash_ifprintf( fp, indent, "monochrome = %"PRIu8"\n", (temp8 >> 4) & 0x01 );
    lsmash_ifprintf( fp, indent, "chroma_subsampling_x = %"PRIu8"\n", (temp8 >> 3) & 0x01 );
    lsmash_ifprintf( fp, indent, "chroma_subsampling_y = %"PRIu8"\n", (temp8 >> 2) & 0x01 );
    lsmash_ifprintf( fp, indent, "chroma_sample_position = %"PRIu8"\n", temp8 & 0x03 );
    temp8 = lsmash_bs_get_byte( bs );
    uint8_t initial_presentation_delay_present = (temp8 >> 4) & 0x01;
    lsmash_ifprintf( fp, indent, "initial_presentation_delay_present = %"PRIu8"\n", initial_presentation_delay_present );
    if( initial_presentation_delay_present )
        lsmash_ifprintf( fp, indent, "initial_presentation_delay_minus_one = %"PRIu8"\n", temp8 & 0x0F );
    lsmash_bs_cleanup( bs );
    return 0;
}
