/*****************************************************************************
 * av1.c
 *****************************************************************************
 * Copyright (C) 2018 L-SMASH project
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

#include <string.h>

#include "common/internal.h" /* must be placed first */

#include "core/box.h"
#include "description.h"

/*********************************************************************************
    Alliance for Open Media AV1

    References:
        AV1 Bitstream & Decoding Process Specification
            Version 1.0.0
        AV1 Codec ISO Media File Format Binding
            v1.0.0, 7 September 2018
**********************************************************************************/
#include "av1.h"

#define AV1_CODEC_CONFIGURATION_RECORD_MARKER (1)
#define AV1_CODEC_CONFIGURATION_RECORD_VERSION_1 (1)

#define IF_OUT_OF_SPEC( x ) if( x )

typedef struct
{
    uint8_t SeenFrameHeader;
    uint8_t first_frame_in_temporal_unit;
    uint8_t frame_type;
    uint8_t show_frame;
    uint8_t showable_frame;
    uint8_t show_existing_frame;
    uint8_t frame_to_show_map_idx;
    //uint8_t with_sequence_header;

    uint8_t use_superres;
    uint8_t coded_denom;
    uint8_t SuperresDenom;

    uint16_t UpscaledWidth;
    uint16_t UpscaledHeight;

    uint16_t render_width_minus_1;
    uint16_t render_height_minus_1;
    uint16_t RenderWidth;
    uint16_t RenderHeight;

    uint16_t frame_width_minus_1;
    uint16_t frame_height_minus_1;
    uint16_t FrameWidth;
    uint16_t FrameHeight;

    uint8_t render_and_frame_size_different;

    int MiCols;
    int MiRows;

    int NumTiles;

    uint8_t frame_size_override_flag;
    uint8_t curFrameHint;
    uint8_t earliestOrderHint;
    uint8_t latestOrderHint;
    uint8_t goldOrderHint;
    uint8_t OrderHint;
    uint8_t lastOrderHint;

    int current_frame_id;
    int order_hint;

#define NUM_REF_FRAMES_ARRAY 8 // see below for NUM_REF_FRAMES
    int OrderHints[NUM_REF_FRAMES_ARRAY];
    int shiftedOrderHints[NUM_REF_FRAMES_ARRAY];
    int usedFrame[NUM_REF_FRAMES_ARRAY];
    int RefOrderHint[NUM_REF_FRAMES_ARRAY];
    int RefValid[NUM_REF_FRAMES_ARRAY];
    int ref_order_hint[NUM_REF_FRAMES_ARRAY];

    int ref_frame_idx[NUM_REF_FRAMES_ARRAY];
    uint8_t last_frame_idx;
    uint8_t gold_frame_idx;

    uint8_t is_filter_switchable;
    uint8_t interpolation_filter;
    uint8_t force_integer_mv;
    uint8_t disable_cdf_update;

    int TileColsLog2;
    int TileCols;
    int TileRowsLog2;
    int TileRows;

    uint8_t FrameIsIntra;
    int frame_presentation_time;
    uint8_t error_resilient_mode;
    uint8_t buffer_removal_time_present_flag;
    uint8_t allow_screen_content_tools;

    int primary_ref_frame;
    uint8_t refresh_frame_flags;
    uint8_t frame_refs_short_signaling;
    uint8_t allow_intrabc;
    uint8_t allow_high_precision_mv;
    uint8_t is_motion_mode_switchable;
    uint8_t use_ref_frame_mvs;
    uint8_t tile_start_and_end_present_flag;

    uint16_t delta_frame_id_minus_1;
    uint8_t disable_frame_end_update_cdf;
} av1_frame_t;

typedef struct
{
    av1_frame_t *active_frame;
    uint8_t      temporal_id;
    uint8_t      with_sequence_header;  /* should be in av1_frame_t? */
} av1_temporal_unit_t;

typedef struct
{
    uint32_t num_units_in_display_tick;
    uint32_t time_scale;
    uint8_t  equal_picture_interval;
    uint32_t num_ticks_per_picture_minus_1;
} av1_timing_info_t;

typedef struct
{
    uint8_t  buffer_delay_length_minus_1;
    uint32_t num_units_in_decoding_tick;
    uint8_t  buffer_removal_time_length_minus_1;
    uint8_t  frame_presentation_time_length_minus_1;
} av1_decoder_model_info_t;

typedef struct
{
    uint16_t operating_point_idc;
    uint8_t  seq_level_idx;
    uint8_t  seq_tier;
    uint8_t  decoder_model_present_for_this_op;
    uint8_t  initial_display_delay_present_for_this_op;
    uint8_t  initial_display_delay_minus_1;

    uint16_t decoder_buffer_delay;
    uint16_t encoder_buffer_delay;
    uint16_t low_delay_mode_flag;
} av1_operating_point_t;

typedef struct
{
    uint8_t high_bitdepth;
    uint8_t twelve_bit;
    uint8_t mono_chrome;
    uint8_t subsampling_x;
    uint8_t subsampling_y;
    uint8_t chroma_sample_position;
    uint8_t separate_uv_delta_q;

    uint8_t color_description_present_flag;
    uint16_t color_primaries;
    uint16_t transfer_characteristics;
    uint16_t matrix_coefficients;
    uint8_t color_range;
} av1_color_config_t;

typedef struct
{
    uint8_t  seq_profile;
    uint8_t  still_picture;
    uint8_t  reduced_still_picture_header;
    uint8_t  timing_info_present_flag;
    av1_timing_info_t ti;
    uint8_t  decoder_model_info_present_flag;
    av1_decoder_model_info_t dmi;
    uint8_t  initial_display_delay_present_flag;
    uint8_t  operating_points_cnt_minus_1;
    av1_operating_point_t op[32];
    uint16_t max_frame_width_minus_1;
    uint16_t max_frame_height_minus_1;
    uint8_t  frame_id_numbers_present_flag;
    uint8_t  delta_frame_id_length_minus_2;
    uint8_t  additional_frame_id_length_minus_1;
    uint8_t  use_128x128_superblock;
    uint8_t  enable_filter_intra;
    uint8_t  enable_intra_edge_filter;

    av1_color_config_t cc;
    uint32_t num_units_in_display_tick;
    uint8_t enable_warped_motion;
    uint8_t enable_dual_filter;
    uint8_t enable_order_hint;
    uint8_t enable_int_comp;
    uint8_t enable_ref_frame_mvs;
    uint8_t seq_force_screen_content_tools;
    uint8_t seq_force_integer_mv;
    uint8_t seq_choose_screen_content_tools;
    uint8_t seq_choose_integer_mv;
    uint8_t enable_interintra_compound;
    uint8_t enable_restoration;
    uint8_t enable_cdef;
    uint8_t enable_superres;
    uint8_t enable_masked_compound;

    uint8_t order_hint_bits_minus_1;

    uint16_t frame_width_bits_minus_1;
    uint16_t frame_height_bits_minus_1;

    int OrderHintBits;
} av1_sequence_header_t;

typedef struct
{
    lsmash_bits_t        *bits;
    av1_sequence_header_t sequence_header;
    /* */
    uint16_t MaxRenderWidth;
    uint16_t MaxRenderHeight;

    lsmash_av1_specific_parameters_t param;

    int RefValid[NUM_REF_FRAMES_ARRAY];
    int RefFrameId[NUM_REF_FRAMES_ARRAY];
    uint16_t RefUpscaledWidth[NUM_REF_FRAMES_ARRAY];
    uint16_t RefFrameHeight[NUM_REF_FRAMES_ARRAY];
    uint16_t RefRenderWidth[NUM_REF_FRAMES_ARRAY];
    uint16_t RefRenderHeight[NUM_REF_FRAMES_ARRAY];
} av1_parser_t;

enum
{
    CP_BT_709      = ISOM_PRIMARIES_INDEX_ITU_R709_5,
    CP_UNSPECIFIED = ISOM_PRIMARIES_INDEX_UNSPECIFIED,

    TC_UNSPECIFIED = ISOM_TRANSFER_INDEX_UNSPECIFIED,
    TC_SRGB        = ISOM_TRANSFER_INDEX_SRGB,

    MC_IDENTITY    = ISOM_MATRIX_INDEX_NO_MATRIX,
    MC_UNSPECIFIED = ISOM_MATRIX_INDEX_UNSPECIFIED,

    BufferPoolMaxSize = 10,

    REFS_PER_FRAME              = 7,
    NUM_REF_FRAMES              = 8,
    SELECT_SCREEN_CONTENT_TOOLS = 2,
    SELECT_INTEGER_MV           = 2,
    SUPERRES_NUM                = 8,
    SUPERRES_DENOM_MIN          = 9,
    SUPERRES_DENOM_BITS         = 3,
    PRIMARY_REF_NONE            = 7,

    /* RefFrame[*] */
    NONE_FRAME = 0,
    LAST_FRAME = 1,
    LAST2_FRAME = 2,
    LAST3_FRAME = 3,
    GOLDEN_FRAME = 4,
    BWDREF_FRAME = 5,
    ALTREF2_FRAME = 6,
    ALTREF_FRAME = 7,

    /* frame_type */
    KEY_FRAME        = 0,
    INTER_FRAME      = 1,
    INTRA_ONLY_FRAME = 2,
    SWITCH_FRAME     = 3,

    /* filter */
    SWITCHABLE = 3,

    /* tile */
    MAX_TILE_WIDTH = 4096,
    MAX_TILE_AREA = 4096 * 2304,
    MAX_TILE_ROWS = 64,
    MAX_TILE_COLS = 64,
};

static inline void *av1_allocate_obu( size_t sz )
{
    return lsmash_malloc_zero( sz );
}

static inline void av1_deallocate_obu( void *obu )
{
    lsmash_free( obu );
}

static av1_config_obus_entry_t *av1_create_config_obus_entry
(
    uint8_t *obu,
    uint32_t sz
)
{
    av1_config_obus_entry_t *config_obu = lsmash_malloc( sizeof(av1_config_obus_entry_t) );
    if( !config_obu )
        return NULL;
    config_obu->obu = obu ? lsmash_memdup( obu, sz ) : av1_allocate_obu( sz );
    if( !config_obu->obu )
    {
        lsmash_free( config_obu );
        return NULL;
    }
    config_obu->sz     = sz;
    config_obu->unused = 0;
    return config_obu;
}

static void av1_destroy_config_obus_entry
(
    av1_config_obus_entry_t *config_obu
)
{
    if( !config_obu )
        return;
    av1_deallocate_obu( config_obu->obu );
    lsmash_free( config_obu );
}

static uint32_t av1_get_uvlc
(
    lsmash_bits_t *bits
)
{
    unsigned int leadingZeros = 0;
    while( 1 )
    {
        if( lsmash_bits_get( bits, leadingZeros ) )
            break;
        ++leadingZeros;
    }
    if( leadingZeros >= 32 )
        return UINT32_MAX;
    uint32_t value = lsmash_bits_get( bits, leadingZeros );
    /* The max value is 0xFFFFFFFE i.e. (UINT32_MAX - 1). */
    return value + ((1u << leadingZeros) - 1);
}

static uint64_t av1_get_leb128
(
    lsmash_bs_t *bs,
    uint8_t     *Leb128Bytes
)
{
    assert( Leb128Bytes );
    uint64_t value = 0;
    *Leb128Bytes = 0;
    for( int i = 0; ; i++ )
    {
        /* The length of value of leb128() is splitted into 7 bits. */
        uint8_t leb128_byte = lsmash_bs_get_byte( bs );
        value |= ((leb128_byte & 0x7F) << (i * 7));
        ++(*Leb128Bytes);
        if( (leb128_byte & 0x80) == 0 )
            break;
        if( i == 7 )
            /* The bitstream is not conformant with the spec. */
            break;
    }
    return value;
}

static uint64_t av1_show_leb128
(
    lsmash_bs_t *bs,
    uint8_t     *Leb128Bytes,
    uint32_t     offset
)
{
    assert( Leb128Bytes );
    uint64_t value = 0;
    *Leb128Bytes = 0;
    for( int i = 0; ; i++ )
    {
        /* The length of value of leb128() is splitted into 7 bits. */
        uint8_t leb128_byte = lsmash_bs_show_byte( bs, offset + i );
        value |= ((leb128_byte & 0x7F) << (i * 7));
        ++(*Leb128Bytes);
        if( (leb128_byte & 0x80) == 0 )
            break;
        if( i == 7 )
            /* The bitstream is not conformant with the spec. */
            break;
    }
    return value;
}

static int64_t av1_get_su
(
    lsmash_bits_t *bits,
    unsigned int   n
)
{
    uint64_t value    = lsmash_bits_get( bits, n );
    uint64_t signMask = ((uint64_t)1) << (n - 1);
    if( value & signMask )
        value -= 2 * signMask;
    return value;
}

static uint64_t av1_get_ns
(
    lsmash_bits_t *bits,
    unsigned int   n
)
{
    uint64_t w = lsmash_floor_log2( n ) + 1;
    uint64_t m = (((uint64_t)1) << w) - n;
    uint64_t v = lsmash_bits_get( bits, w - 1 );
    if( v < m )
        return v;
    uint64_t extra_bit = lsmash_bits_get( bits, 1 );
    return (v << 1) - m + extra_bit;
}

static lsmash_av1_config_obus_t *av1_allocate_configOBUs( void )
{
    lsmash_av1_config_obus_t *configOBUs = lsmash_malloc_zero( sizeof(lsmash_av1_config_obus_t) );
    if( !configOBUs )
        return NULL;
    lsmash_list_init( configOBUs->sequence_header_list, av1_deallocate_obu );
    lsmash_list_init( configOBUs->metadata_list,        av1_deallocate_obu );
    return configOBUs;
}

static void av1_deallocate_configOBUs
(
    lsmash_av1_specific_parameters_t *param
)
{
    if( !param || !param->configOBUs )
        return;
    lsmash_list_remove_entries( param->configOBUs->sequence_header_list );
    lsmash_list_remove_entries( param->configOBUs->metadata_list );
    lsmash_freep( &param->configOBUs );
}

static void av1_destruct_specific_data
(
    void *data
)
{
    if( !data )
        return;
    av1_deallocate_configOBUs( data );
    lsmash_free( data );
}

static void av1_cleanup_parser
(
    av1_parser_t *parser
)
{
    if( !parser )
        return;
    lsmash_bits_cleanup( parser->bits );
    parser->bits = NULL;
}

static int av1_setup_parser
(
    av1_parser_t *parser,
    lsmash_bs_t  *bs
)
{
    lsmash_bits_t *bits = lsmash_bits_create( bs );
    if( !bits )
        return LSMASH_ERR_MEMORY_ALLOC;
    memset( parser, 0, sizeof(av1_parser_t) );
    parser->bits = bits;
    return 0;
}

static int av1_is_shown_frame
(
)
{
    int show_frame = 1;
    int show_existing_frame = 1;

    //XXX: TODO

    return show_frame || show_existing_frame;
}

static inline void av1_compute_pixel_aspect_ratio
(
    av1_parser_t *parser,
    uint64_t *par_h,
    uint64_t *par_v
)
{
    av1_sequence_header_t *sh = &parser->sequence_header;
    uint64_t hSpacing = parser->MaxRenderWidth  * (sh->max_frame_height_minus_1 + 1);
    uint64_t vSpacing = parser->MaxRenderHeight * (sh->max_frame_width_minus_1  + 1);
    lsmash_reduce_fraction( &hSpacing, &vSpacing );
    *par_h = hSpacing;
    *par_v = vSpacing;
}

static lsmash_video_summary_t *av1_create_summary
(
    av1_parser_t            *parser
)
{
    lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_VIDEO );
    if( !summary )
        return NULL;
    /* Create the CODEC specific data structure from the sequence header.
     * TODO: supporting delayed muxing which counts AV1 samples to determine initial_presentation_delay_minus_one.
     *   The initial_presentation_delay_minus_one is counted in units of AV1 sample while the initial_display_delay_minus_1 is
     *   counted in units of AV1 frame. A AV1 sample could contain multiple frames so the initial_presentation_delay_minus_one
     *   may be smaller than initial_presentation_delay_minus if an AV1 sample contains multiple frames in the delay interval. */
    av1_sequence_header_t *sh = &parser->sequence_header;
    lsmash_av1_specific_parameters_t param;
    param.seq_profile            = sh->seq_profile;
    param.seq_level_idx_0        = sh->op[0].seq_level_idx;
    param.seq_tier_0             = sh->op[0].seq_tier;
    param.high_bitdepth          = sh->cc.high_bitdepth;
    param.twelve_bit             = sh->cc.twelve_bit;
    param.monochrome             = sh->cc.mono_chrome;
    param.chroma_subsampling_x   = sh->cc.subsampling_x;
    param.chroma_subsampling_y   = sh->cc.subsampling_y;
    param.chroma_sample_position = sh->cc.chroma_sample_position;
    param.initial_presentation_delay_present   = 0;
    param.initial_presentation_delay_minus_one = 0;
    if( param.seq_profile            == parser->param.seq_profile
     || param.seq_level_idx_0        <= parser->param.seq_level_idx_0
     || param.seq_tier_0             == parser->param.seq_tier_0
     || param.high_bitdepth          == parser->param.high_bitdepth
     || param.twelve_bit             == parser->param.twelve_bit
     || param.monochrome             == parser->param.monochrome
     || param.chroma_subsampling_x   == parser->param.chroma_subsampling_x
     || param.chroma_subsampling_y   == parser->param.chroma_subsampling_y
     || param.chroma_sample_position == parser->param.chroma_sample_position
     || (param.initial_presentation_delay_present   == parser->param.chroma_sample_position
      && (!param.initial_presentation_delay_present
       || (param.initial_presentation_delay_minus_one == parser->param.initial_presentation_delay_minus_one))) )
        /* No need create the new one. */
        return NULL;
    /* TODO: copy param here. */
    lsmash_codec_specific_t *cs = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_AV1,
                                                                     LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
    if( !cs )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
    cs->data.unstructured = lsmash_create_av1_specific_info( &param, &cs->size );
    if( !cs->data.unstructured
     || lsmash_list_add_entry( &summary->opaque->list, cs ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( cs );
        return NULL;
    }
    /* Set up the summary. */
    uint64_t par_h;
    uint64_t par_v;
    av1_compute_pixel_aspect_ratio( parser, &par_h, &par_v );
    summary->sample_type           = ISOM_CODEC_TYPE_AV01_VIDEO;
    summary->timescale             = sh->num_units_in_display_tick ? sh->num_units_in_display_tick : 0;
    summary->timebase              = sh->ti.time_scale             ? sh->ti.time_scale             : 0;
    summary->vfr                   = !sh->ti.equal_picture_interval;
    summary->sample_per_field      = 0;
    summary->width                 = sh->max_frame_height_minus_1 + 1;
    summary->height                = sh->max_frame_width_minus_1  + 1;
    summary->par_h                 = par_h;
    summary->par_v                 = par_v;
    summary->color.primaries_index = sh->cc.color_primaries;
    summary->color.transfer_index  = sh->cc.transfer_characteristics;
    summary->color.matrix_index    = sh->cc.matrix_coefficients;
    summary->max_au_length         = UINT32_MAX; /* unused */
    return summary;
fail:
    lsmash_cleanup_summary( (lsmash_summary_t *)summary );
    return NULL;
}

static void av1_parser_color_config
(
    av1_parser_t *parser
)
{
    lsmash_bits_t         *bits = parser->bits;
    av1_sequence_header_t *sh   = &parser->sequence_header;
    av1_color_config_t    *cc   = &sh->cc;
    cc->high_bitdepth = lsmash_bits_get( bits, 1 );
    if( sh->seq_profile == 2 && cc->high_bitdepth )
        cc->twelve_bit = lsmash_bits_get( bits, 1 );
    else
        cc->twelve_bit = 0; /* The spec does not define this! Anyway 'av1C' requires so do this. */
    cc->mono_chrome = sh->seq_profile == 1 ? 0 : lsmash_bits_get( bits, 1 );
    cc->color_description_present_flag = lsmash_bits_get( bits, 1 );
    if( cc->color_description_present_flag )
    {
        cc->color_primaries          = lsmash_bits_get( bits, 8 );
        cc->transfer_characteristics = lsmash_bits_get( bits, 8 );
        cc->matrix_coefficients      = lsmash_bits_get( bits, 8 );
    }
    else
    {
        cc->color_primaries          = CP_UNSPECIFIED;
        cc->transfer_characteristics = TC_UNSPECIFIED;
        cc->matrix_coefficients      = MC_UNSPECIFIED;
    }
    if( cc->mono_chrome )
    {
        cc->color_range            = lsmash_bits_get( bits, 1 );
        cc->subsampling_x          = 1;
        cc->subsampling_y          = 1;
        cc->chroma_sample_position = LSMASH_AV1_CSP_UNKNOWN;
        cc->separate_uv_delta_q    = 0;
        return;
    }
    else if( cc->color_primaries          == CP_BT_709
          || cc->transfer_characteristics == TC_SRGB
          || cc->matrix_coefficients      == MC_IDENTITY )
    {
        cc->color_range            = 1;
        cc->subsampling_x          = 0;
        cc->subsampling_y          = 0;
        cc->chroma_sample_position = LSMASH_AV1_CSP_UNKNOWN;    /* The spec does not define this! Anyway 'av1C' requires so do this. */
    }
    else
    {
        cc->color_range = lsmash_bits_get( bits, 1 );
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
                cc->subsampling_x = lsmash_bits_get( bits, 1 );
                cc->subsampling_y = cc->subsampling_x ? lsmash_bits_get( bits, 1 ) : 0;
            }
            else
            {
                cc->subsampling_x = 1;
                cc->subsampling_y = 0;
            }
        }
        if( cc->subsampling_x && cc->subsampling_y )
            cc->chroma_sample_position = lsmash_bits_get( bits, 2 );
        else
            cc->chroma_sample_position = LSMASH_AV1_CSP_UNKNOWN;    /* The spec does not define this! Anyway 'av1C' requires so do this. */
    }
    cc->separate_uv_delta_q = lsmash_bits_get( bits, 1 );
}

/* Return 1 if no error, and summary changed and created.
 * Return 0 if no error.
 * Return negative value if any error. */
static int av1_parse_sequence_header
(
    av1_parser_t            *parser,
    lsmash_video_summary_t **summary
)
{
    lsmash_bits_t         *bits = parser->bits;
    av1_sequence_header_t *sh   = &parser->sequence_header;
    sh->seq_profile                  = lsmash_bits_get( bits, 3 );
    sh->still_picture                = lsmash_bits_get( bits, 1 );
    sh->reduced_still_picture_header = lsmash_bits_get( bits, 1 );
    if( sh->reduced_still_picture_header )
    {
        sh->timing_info_present_flag           = 0;
        sh->decoder_model_info_present_flag    = 0;
        sh->initial_display_delay_present_flag = 0;
        sh->operating_points_cnt_minus_1       = 0;
        sh->op[0].operating_point_idc                       = 0;
        sh->op[0].seq_level_idx                             = lsmash_bits_get( bits, 5 );
        sh->op[0].seq_tier                                  = 0;
        sh->op[0].decoder_model_present_for_this_op         = 0;
        sh->op[0].initial_display_delay_present_for_this_op = 0;
        /* Is sh->op[0].initial_display_delay_minus_1 equal to 0 ? */
    }
    else
    {
        sh->timing_info_present_flag = lsmash_bits_get( bits, 1 );
        if( sh->timing_info_present_flag )
        {
            /* timing_info() */
            sh->ti.num_units_in_display_tick = lsmash_bits_get( bits, 32 );
            sh->ti.time_scale                = lsmash_bits_get( bits, 32 );
            sh->ti.equal_picture_interval    = lsmash_bits_get( bits, 1 );
            if( sh->ti.equal_picture_interval )
                sh->ti.num_ticks_per_picture_minus_1 = av1_get_uvlc( bits );
            sh->decoder_model_info_present_flag = lsmash_bits_get( bits, 1 );
            if( sh->decoder_model_info_present_flag )
            {
                /* decoder_model_info() */
                sh->dmi.buffer_delay_length_minus_1            = lsmash_bits_get( bits, 5 );
                sh->dmi.num_units_in_decoding_tick             = lsmash_bits_get( bits, 32 );
                sh->dmi.buffer_removal_time_length_minus_1     = lsmash_bits_get( bits, 5 );
                sh->dmi.frame_presentation_time_length_minus_1 = lsmash_bits_get( bits, 5 );
            }
        }
        else
            sh->decoder_model_info_present_flag = 0;
        sh->initial_display_delay_present_flag = lsmash_bits_get( bits, 1 );
        sh->operating_points_cnt_minus_1       = lsmash_bits_get( bits, 5 );
        for( uint8_t i = 0; i <= sh->operating_points_cnt_minus_1; i++ )
        {
            sh->op[i].operating_point_idc = lsmash_bits_get( bits, 12 );
            sh->op[i].seq_level_idx       = lsmash_bits_get( bits, 5 );
            sh->op[i].seq_tier            = sh->op[i].seq_level_idx > 7 ? lsmash_bits_get( bits, 1 ) : 0;
            if( sh->decoder_model_info_present_flag )
            {
                sh->op[i].decoder_model_present_for_this_op = lsmash_bits_get( bits, 1 );
                if( sh->op[i].decoder_model_present_for_this_op )
                {
                    /* operating_parameters_info( i ) */
                    int n = sh->dmi.buffer_delay_length_minus_1 + 1;
                    sh->op[i].decoder_buffer_delay = lsmash_bits_get( bits, n );
                    sh->op[i].encoder_buffer_delay = lsmash_bits_get( bits, n );
                    sh->op[i].low_delay_mode_flag  = lsmash_bits_get( bits, 1 );
                }
            }
            else
                sh->op[i].decoder_model_present_for_this_op = 0;
            if( sh->initial_display_delay_present_flag )
            {
                sh->op[i].initial_display_delay_present_for_this_op = lsmash_bits_get( bits, 1 );
                if( sh->op[i].initial_display_delay_present_for_this_op )
                    sh->op[i].initial_display_delay_minus_1 = lsmash_bits_get( bits, 4 );
                else
                    sh->op[i].initial_display_delay_minus_1 = BufferPoolMaxSize - 1;
            }
        }
    }
    sh->frame_width_bits_minus_1  = lsmash_bits_get( bits, 4 );
    sh->frame_height_bits_minus_1 = lsmash_bits_get( bits, 4 );
    sh->max_frame_width_minus_1  = lsmash_bits_get( bits, sh->frame_width_bits_minus_1  + 1 );
    sh->max_frame_height_minus_1 = lsmash_bits_get( bits, sh->frame_height_bits_minus_1 + 1 );

    sh->frame_id_numbers_present_flag = sh->reduced_still_picture_header ? 0 : lsmash_bits_get( bits, 1 );
    if( sh->frame_id_numbers_present_flag )
    {
        sh->delta_frame_id_length_minus_2      = lsmash_bits_get( bits, 4 );
        sh->additional_frame_id_length_minus_1 = lsmash_bits_get( bits, 3 );
    }
    sh->use_128x128_superblock   = lsmash_bits_get( bits, 1 );
    sh->enable_filter_intra      = lsmash_bits_get( bits, 1 );
    sh->enable_intra_edge_filter = lsmash_bits_get( bits, 1 );
    if( sh->reduced_still_picture_header )
    {
        sh->enable_interintra_compound     = 0;
        sh->enable_masked_compound         = 0;
        sh->enable_warped_motion           = 0;
        sh->enable_dual_filter             = 0;
        sh->enable_order_hint              = 0;
        sh->enable_int_comp                = 0;
        sh->enable_ref_frame_mvs           = 0;
        sh->seq_force_screen_content_tools = SELECT_SCREEN_CONTENT_TOOLS;
        sh->seq_force_integer_mv           = SELECT_INTEGER_MV;
        sh->OrderHintBits                  = 0;
    }
    else
    {
        sh->enable_interintra_compound = lsmash_bits_get( bits, 1 );
        sh->enable_masked_compound     = lsmash_bits_get( bits, 1 );
        sh->enable_warped_motion       = lsmash_bits_get( bits, 1 );
        sh->enable_dual_filter         = lsmash_bits_get( bits, 1 );
        sh->enable_order_hint          = lsmash_bits_get( bits, 1 );
        if( sh->enable_order_hint )
        {
            sh->enable_int_comp      = lsmash_bits_get( bits, 1 );
            sh->enable_ref_frame_mvs = lsmash_bits_get( bits, 1 );
        }
        else
        {
            sh->enable_int_comp      = 0;
            sh->enable_ref_frame_mvs = 0;
        }
        sh->seq_choose_screen_content_tools = lsmash_bits_get( bits, 1 );
        if( sh->seq_choose_screen_content_tools )
            sh->seq_force_screen_content_tools = SELECT_SCREEN_CONTENT_TOOLS;
        else
            sh->seq_force_screen_content_tools = lsmash_bits_get( bits, 1 );
        if( sh->seq_force_screen_content_tools > 0 )
        {
            sh->seq_choose_integer_mv = lsmash_bits_get( bits, 1 );
            if( sh->seq_choose_integer_mv )
                sh->seq_force_integer_mv = SELECT_INTEGER_MV;
            else
                sh->seq_force_integer_mv = lsmash_bits_get( bits, 1 );
        }
        else
            sh->seq_force_integer_mv = SELECT_INTEGER_MV;
        if( sh->enable_order_hint )
            sh->order_hint_bits_minus_1 = lsmash_bits_get( bits, 3 );
    }
    sh->enable_superres    = lsmash_bits_get( bits, 1 );
    sh->enable_cdef        = lsmash_bits_get( bits, 1 );
    sh->enable_restoration = lsmash_bits_get( bits, 1 );
    av1_parser_color_config( parser );
    /* film_grain_params_present */
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
    frame->use_superres = sh->enable_superres ? lsmash_bits_get( bits, 1 ) : 0;
    if( frame->use_superres )
    {
        frame->coded_denom = lsmash_bits_get( bits, SUPERRES_DENOM_BITS );
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
        frame->frame_width_minus_1  = lsmash_bits_get( bits, sh->frame_width_bits_minus_1  + 1 );
        frame->frame_height_minus_1 = lsmash_bits_get( bits, sh->frame_height_bits_minus_1 + 1 );
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
    lsmash_bits_t         *bits = parser->bits;
    //av1_sequence_header_t *sh   = &parser->sequence_header;
    frame->render_and_frame_size_different = lsmash_bits_get( bits, 1 );
    if( frame->render_and_frame_size_different )
    {
        frame->render_width_minus_1  = lsmash_bits_get( bits, 16 );
        frame->render_height_minus_1 = lsmash_bits_get( bits, 16 );
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
    uint8_t found_ref;
    for ( int i = 0; i < REFS_PER_FRAME; i++ )
    {
        lsmash_bits_t *bits = parser->bits;
        found_ref = lsmash_bits_get( bits, 1 );
        if( found_ref )
        {
            /* To set up Ref*s, we need call av1_decode_frame_wrapup().
             * To set up ref_frame_idx[i], we may need to call av1_set_frame_refs(). */
            frame->UpscaledWidth = parser->RefUpscaledWidth[ frame->ref_frame_idx[i] ];
            frame->FrameWidth    = frame->UpscaledWidth; // XXX
            frame->FrameHeight   = parser->RefFrameHeight[ frame->ref_frame_idx[i] ];
            frame->RenderWidth   = parser->RefRenderWidth[ frame->ref_frame_idx[i] ];
            frame->RenderHeight  = parser->RefRenderHeight[ frame->ref_frame_idx[i] ];
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
    if( !sh->enable_order_hint )
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
    for( int i = 0; i < NUM_REF_FRAMES; i++ )
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
    for( int i = 0; i < NUM_REF_FRAMES; i++ )
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
    for( int i = 0; i < NUM_REF_FRAMES; i++ )
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
    av1_sequence_header_t *sh,
    av1_frame_t           *frame
)
{
    /* The reference frames used for the LAST_FRAME and GOLDEN_FRAME references. */
    for( int i = 0; i < REFS_PER_FRAME; i++ )
        frame->ref_frame_idx[i] = -1;
    frame->ref_frame_idx[LAST_FRAME   - LAST_FRAME] = frame->last_frame_idx;
    frame->ref_frame_idx[GOLDEN_FRAME - LAST_FRAME] = frame->gold_frame_idx;
    /* An array usedFrame marking which reference frames have been used is prepared. */
    for( int i = 0; i < NUM_REF_FRAMES; i++ )
        frame->usedFrame[i] = 0;
    frame->usedFrame[ frame->last_frame_idx ] = 1;
    frame->usedFrame[ frame->gold_frame_idx ] = 1;
    frame->curFrameHint = 1 << (sh->OrderHintBits - 1);
    for ( int i = 0; i < NUM_REF_FRAMES; i++ )
        frame->shiftedOrderHints[i] = frame->curFrameHint + av1_get_relative_dist( sh, frame->RefOrderHint[i], frame->OrderHint );
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
    if( ref >= 0 ) {
        frame->ref_frame_idx[ALTREF2_FRAME - LAST_FRAME] = ref;
        frame->usedFrame[ref] = 1;
    }
    /* The remaining references are set to be forward references in anti-chronological order. */
    for( int i = 0; i < REFS_PER_FRAME - 2; i++ )
    {
        static const int Ref_Frame_List[REFS_PER_FRAME - 2] =
        {
            LAST2_FRAME, LAST3_FRAME, BWDREF_FRAME, ALTREF2_FRAME, ALTREF_FRAME
        };
        int refFrame = Ref_Frame_List[i];
        if( frame->ref_frame_idx[refFrame - LAST_FRAME] < 0 )
        {
            ref = av1_find_latest_forward( frame );
            if( ref >= 0 ) {
                frame->ref_frame_idx[refFrame - LAST_FRAME] = ref;
                frame->usedFrame[ref] = 1;
            }
        }
    }
    /* Finally, any remaining references are set to the reference frame with smallest output order. */
    ref = -1;
    for( int i = 0; i < NUM_REF_FRAMES; i++ )
    {
        int hint = frame->shiftedOrderHints[i];
        if( ref < 0 || hint < frame->earliestOrderHint )
        {
            ref = i;
            frame->earliestOrderHint = hint;
        }
    }
    for( int i = 0; i < REFS_PER_FRAME; i++ )
        if ( frame->ref_frame_idx[i] < 0 )
            frame->ref_frame_idx[i] = ref;
    return 0;
}

static inline void av1_read_interpolation_filter
(
    lsmash_bits_t *bits,
    av1_frame_t   *frame
)
{
    frame->is_filter_switchable = lsmash_bits_get( bits, 1 );
    frame->interpolation_filter = frame->is_filter_switchable ? SWITCHABLE : lsmash_bits_get( bits, 2 );
}

static int av1_parse_tile_info
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{
    av1_sequence_header_t *sh = &parser->sequence_header;

    /* To compute NumTiles, we need TileCols and TileRows. */
    int sbCols  = sh->use_128x128_superblock ? ((frame->MiCols + 31) >> 5) : ((frame->MiCols + 15) >> 4);
    int sbRows  = sh->use_128x128_superblock ? ((frame->MiRows + 31) >> 5) : ((frame->MiRows + 15) >> 4);
    int sbShift = sh->use_128x128_superblock ? 5 : 4;
    int sbSize  = sbShift + 2;
    int maxTileWidthSb = MAX_TILE_WIDTH >> sbSize;
    int maxTileAreaSb = MAX_TILE_AREA >> (2 * sbSize);
    int minLog2TileCols = tile_log2(  maxTileWidthSb, sbCols );
    int maxLog2TileCols = tile_log2( 1, LSMASH_MIN( sbCols, MAX_TILE_COLS ) );
    int maxLog2TileRows = tile_log2( 1, LSMASH_MIN( sbRows, MAX_TILE_ROWS ) );
    int minLog2Tiles = LSMASH_MAX( minLog2TileCols, tile_log2( maxTileAreaSb, sbRows * sbCols ) );

    lsmash_bits_t *bits = parser->bits;
    uint8_t uniform_tile_spacing_flag = lsmash_bits_get( bits, 1 );
    if( uniform_tile_spacing_flag )
    {
        frame->TileColsLog2 = minLog2TileCols;
        while( frame->TileColsLog2 < maxLog2TileCols )
        {
            uint8_t increment_tile_cols_log2 = lsmash_bits_get( bits, 1 );
            if( increment_tile_cols_log2 == 1 )
                ++ frame->TileColsLog2;
            else
                break;
        }
        int tileWidthSb = (sbCols + (1 << frame->TileColsLog2) - 1) >> frame->TileColsLog2;
        int i = 0;
        for( int startSb = 0; startSb < sbCols; startSb += tileWidthSb )
            i++;
        frame->TileCols = i;
        int minLog2TileRows = LSMASH_MAX( minLog2Tiles - frame->TileColsLog2, 0 );
        frame->TileRowsLog2 = minLog2TileRows;
        while( frame->TileRowsLog2 < maxLog2TileRows )
        {
            int increment_tile_rows_log2 = lsmash_bits_get( bits, 1 );
            if( increment_tile_rows_log2 == 1 )
                ++ frame->TileRowsLog2;
            else
                break;
        }
        int tileHeightSb = (sbRows + (1 << frame->TileRowsLog2) - 1) >> frame->TileRowsLog2;
        i = 0;
        for( int startSb = 0; startSb < sbRows; startSb += tileHeightSb )
            i++;
        frame->TileRows = i;
    }
    else
    {
        int sizeSb = 0;
        int widestTileSb = 0;
        int i = 0;
        for( int startSb = 0; startSb < sbCols; startSb += sizeSb )
        {
            i++;
            int maxWidth = LSMASH_MIN( sbCols - startSb, maxTileWidthSb );
            int width_in_sbs_minus_1 = av1_get_ns( bits, maxWidth );
            sizeSb = width_in_sbs_minus_1 + 1;
            widestTileSb = LSMASH_MAX( sizeSb, widestTileSb );
        }
        frame->TileCols = i;
        frame->TileColsLog2 = tile_log2( 1, frame->TileCols );
        if ( minLog2Tiles > 0 )
            maxTileAreaSb = (sbRows * sbCols) >> (minLog2Tiles + 1);
        else
            maxTileAreaSb = sbRows * sbCols;
        int maxTileHeightSb = LSMASH_MAX( maxTileAreaSb / widestTileSb, 1 );
        i = 0;
        for( int startSb = 0; startSb < sbRows; startSb += sizeSb )
        {
            i++;
            int maxHeight = LSMASH_MIN( sbRows - startSb, maxTileHeightSb );
            int height_in_sbs_minus_1 = av1_get_ns( bits, maxHeight );
            sizeSb = height_in_sbs_minus_1 + 1;
        }
        frame->TileRows = i;
        frame->TileRowsLog2 = tile_log2( 1, frame->TileRows );
    }
    int context_update_tile_id; // XXX: unused
    if( frame->TileColsLog2 > 0 || frame->TileRowsLog2 > 0 )
    {
        context_update_tile_id  = lsmash_bits_get( bits, frame->TileRowsLog2 + frame->TileColsLog2 );
        int tile_size_bytes_minus_1 = lsmash_bits_get( bits, 2 );
        int TileSizeBytes = tile_size_bytes_minus_1 + 1; // XXX: unused
    }
    else
        context_update_tile_id = 0;
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
    if( sh->reduced_still_picture_header )
    {
        frame->show_existing_frame = 0;
        frame->frame_type          = KEY_FRAME;
        frame->FrameIsIntra        = 1;
        frame->show_frame          = 1;
        frame->showable_frame      = 0;
    }
    else
    {
        frame->show_existing_frame = lsmash_bits_get( bits, 1 );
        IF_OUT_OF_SPEC( frame->show_existing_frame && is_frame_obu )
            return LSMASH_ERR_INVALID_DATA;
        if( frame->show_existing_frame )
            return 0;   /* TODO: check the first frame or not. */
        frame->frame_type = lsmash_bits_get( bits, 2 );
        frame->FrameIsIntra = (frame->frame_type == INTRA_ONLY_FRAME || frame->frame_type == KEY_FRAME);
        frame->show_frame = lsmash_bits_get( bits, 1 );
        if( frame->show_frame && sh->decoder_model_info_present_flag && !sh->ti.equal_picture_interval )
            frame->frame_presentation_time = lsmash_bits_get( bits, sh->dmi.frame_presentation_time_length_minus_1 + 1 );
        frame->showable_frame = frame->show_frame ? frame->frame_type != KEY_FRAME : lsmash_bits_get( bits, 1 );
        if( frame->frame_type == SWITCH_FRAME
         || (frame->frame_type == KEY_FRAME && frame->show_frame) )
            frame->error_resilient_mode = 1;
        else
            frame->error_resilient_mode = lsmash_bits_get( bits, 1 );
    }
    if( frame->frame_type == KEY_FRAME && frame->show_frame )
    {
        for( int i = 0; i < NUM_REF_FRAMES; i++ )
        {
            frame->RefValid    [i] = 0;
            frame->RefOrderHint[i] = 0;
        }
        for( int i = 0; i < REFS_PER_FRAME; i++ )
            frame->OrderHints[LAST_FRAME + i] = 0;
    }
    frame->disable_cdf_update = lsmash_bits_get( bits, 1 );
    if( sh->seq_force_screen_content_tools == SELECT_SCREEN_CONTENT_TOOLS )
        frame->allow_screen_content_tools = lsmash_bits_get( bits, 1 );
    else
        frame->allow_screen_content_tools = sh->seq_force_screen_content_tools;
    if( frame->allow_screen_content_tools )
    {
        if( sh->seq_force_integer_mv == SELECT_INTEGER_MV )
            frame->force_integer_mv = lsmash_bits_get( bits, 1 );
        else
            frame->force_integer_mv = sh->seq_force_integer_mv;
    }
    else
        frame->force_integer_mv = 0;
    if( frame->FrameIsIntra )
        frame->force_integer_mv = 1;
    if( sh->frame_id_numbers_present_flag )
    {
        uint32_t idLen = sh->additional_frame_id_length_minus_1 + sh->delta_frame_id_length_minus_2 + 3;
        frame->current_frame_id = lsmash_bits_get( bits, idLen );
    }
    else
        frame->current_frame_id = 0;
    if( frame->frame_type == SWITCH_FRAME )
        frame->frame_size_override_flag = 1;
    else if( sh->reduced_still_picture_header )
        frame->frame_size_override_flag = 0;
    else
        frame->frame_size_override_flag = lsmash_bits_get( bits, 1 );
    frame->order_hint = lsmash_bits_get( bits, sh->order_hint_bits_minus_1 + 1 );
    if( frame->FrameIsIntra || frame->error_resilient_mode )
        frame->primary_ref_frame = PRIMARY_REF_NONE;
    else
        frame->primary_ref_frame = lsmash_bits_get( bits, 3 );
    if( sh->decoder_model_info_present_flag )
    {
        frame->buffer_removal_time_present_flag = lsmash_bits_get( bits, 1 );
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
                        /*uint32_t buffer_removal_time[opNum] =*/ lsmash_bits_get( bits, n );
                    }
                }
            }
    }
    uint8_t allFrames = (1 << NUM_REF_FRAMES) - 1;
    if( frame->frame_type == SWITCH_FRAME || (frame->frame_type == KEY_FRAME && frame->show_frame) )
        frame->refresh_frame_flags = allFrames;
    else
        frame->refresh_frame_flags = lsmash_bits_get( bits, 1 );
    if( !frame->FrameIsIntra || frame->refresh_frame_flags != allFrames ) {
        if( frame->error_resilient_mode && sh->enable_order_hint )
            for( int i = 0; i < NUM_REF_FRAMES; i++ )
            {
                frame->ref_order_hint[i] = lsmash_bits_get( bits, sh->order_hint_bits_minus_1 + 1 );
                if( frame->ref_order_hint[i] != frame->RefOrderHint[i] )
                    frame->RefValid[i] = 0;
            }
    }
    int err;
    if( frame->frame_type == KEY_FRAME )
    {
        if( (err = av1_parse_frame_size( parser, frame )) < 0 ||
            (err = av1_parse_render_size( parser, frame )) < 0 )
            return err;
        if( frame->allow_screen_content_tools && frame->UpscaledWidth == frame->FrameWidth )
            frame->allow_intrabc = lsmash_bits_get( bits, 1 );
    }
    else
    {
        if( frame->frame_type == INTRA_ONLY_FRAME )
        {
            if( (err = av1_parse_frame_size( parser, frame )) < 0 ||
                (err = av1_parse_render_size( parser, frame )) < 0 )
                return err;
            if( frame->allow_screen_content_tools && frame->UpscaledWidth == frame->FrameWidth )
                frame->allow_intrabc = lsmash_bits_get( bits, 1 );
        }
        else
        {
            if( sh->enable_order_hint )
            {
                frame->frame_refs_short_signaling = lsmash_bits_get( bits, 1 );
                if( frame->frame_refs_short_signaling )
                {
                    frame->last_frame_idx = lsmash_bits_get( bits, 3 );
                    frame->gold_frame_idx = lsmash_bits_get( bits, 3 );
                    if( (err = av1_set_frame_refs( sh, frame )) < 0 )
                        return err;
                }
            }
            else
                frame->frame_refs_short_signaling = 0;
            for( int i = 0; i < REFS_PER_FRAME; i++ )
            {
                if( !frame->frame_refs_short_signaling )
                    frame->ref_frame_idx[i] = lsmash_bits_get( bits, 3 );
                if( sh->frame_id_numbers_present_flag )
                    frame->delta_frame_id_minus_1 = lsmash_bits_get( bits, sh->delta_frame_id_length_minus_2 + 2 );
            }
            if( frame->frame_size_override_flag && !frame->error_resilient_mode )
            {
                if( (err = av1_parse_frame_size_with_refs( parser, frame )) < 0 )
                    return err;
            }
            else
            {
                if( (err = av1_parse_frame_size( parser, frame )) < 0 ||
                    (err = av1_parse_render_size( parser, frame )) < 0 )
                    return err;
            }
            frame->allow_high_precision_mv = frame->force_integer_mv ? 0 : lsmash_bits_get( bits, 1 );
            av1_read_interpolation_filter( bits, frame );
            frame->is_motion_mode_switchable = lsmash_bits_get( bits, 1 );
            if( frame->error_resilient_mode || !sh->enable_ref_frame_mvs )
                frame->use_ref_frame_mvs = 0;
            else
                frame->use_ref_frame_mvs = lsmash_bits_get( bits, 1 );
        }
    }
    int RefFrameSignBias[NUM_REF_FRAMES_ARRAY];
    if( !frame->FrameIsIntra )
        for( int i = 0; i < REFS_PER_FRAME; i++ )
        {
            int refFrame = LAST_FRAME + i;
            int hint     = frame->RefOrderHint[ frame->ref_frame_idx[i] ];
            frame->OrderHints[refFrame] = hint;
            if( !sh->enable_order_hint )
                RefFrameSignBias[refFrame] = 0;
            else
                RefFrameSignBias[refFrame] = av1_get_relative_dist( sh, hint, frame->OrderHint ) > 0;
        }
    /* To get tile_info(), we need parse more. */
    if( sh->reduced_still_picture_header || frame->disable_cdf_update )
        frame->disable_frame_end_update_cdf = 1;
    else
        frame->disable_frame_end_update_cdf = lsmash_bits_get( bits, 1 );
    if( frame->primary_ref_frame == PRIMARY_REF_NONE )
    {
        /* init_non_coeff_cdfs()
         * setup_past_independence() */;
    }
    else
    {
        /* load_cdfs( frame->ref_frame_idx[ frame->primary_ref_frame ] )
         * load_previous() */;
    }
    if( frame->use_ref_frame_mvs )
        av1_motion_field_estimation();
    /* To call av1_decode_frame_wrapup() at the end of the tile group. We need parse tile_info(). */
    av1_parse_tile_info( parser, frame );
    if( !is_frame_obu )
        return 0;
    /* Parse until the end of the frame header to reach the next tile group OBU in a frame OBU. */
    return 0;
}

static int av1_decode_frame_wrapup
(
    av1_parser_t *parser,
    av1_frame_t  *frame
)
{

    /* 7.20. Reference frame update process */
    for( int i; i < NUM_REF_FRAMES; i++ )
        if( (frame->refresh_frame_flags >> i) & 1 )
        {
            parser->RefValid[i]         = 1;
            parser->RefFrameId[i]       = frame->current_frame_id;
            parser->RefUpscaledWidth[i] = frame->UpscaledWidth;
            parser->RefFrameHeight[i]   = frame->FrameHeight;
            parser->RefRenderWidth[i]   = frame->RenderWidth;
            parser->RefRenderHeight[i]  = frame->RenderHeight;
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
    lsmash_bits_t *bits = parser->bits;
    int tg_start, tg_end;

    if( !frame->SeenFrameHeader )
        return LSMASH_ERR_INVALID_DATA;
    frame->NumTiles = frame->TileCols * frame->TileRows;
    if( frame->NumTiles > 1 )
    {
        frame->tile_start_and_end_present_flag = lsmash_bits_get( bits, 1 );
        IF_OUT_OF_SPEC( frame->tile_start_and_end_present_flag && is_frame_obu )
            return LSMASH_ERR_INVALID_DATA;
    }
    if( frame->NumTiles == 1 || !frame->tile_start_and_end_present_flag )
    {
        tg_start = 0;
        tg_end   = frame->NumTiles - 1;
    }
    else
    {
        uint32_t tileBits = frame->TileColsLog2 + frame->TileRowsLog2;
        tg_start = lsmash_bits_get( bits, tileBits );
        tg_end   = lsmash_bits_get( bits, tileBits );
    }
    IF_OUT_OF_SPEC( tg_end < tg_start )
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
    lsmash_bits_get_align( parser->bits );
    return av1_parse_tile_group( parser, frame, 1 );
}

/* Return 1 if no error, and summary changed and created.
 * Return 0 if no error.
 * Return negative value if any error. */
int av1_get_access_unit
(
    lsmash_bs_t               *bs,
    lsmash_sample_property_t  *prop,
    lsmash_video_summary_t   **summary,
    uint8_t                   *au_data,
    uint32_t                   au_length,
    av1_parser_t              *parser
)
{
    int err = 0;
    uint32_t remaining_bytes = au_length;
    av1_temporal_unit_t tu = { .active_frame = NULL, .temporal_id = 1 << 3 };
    /* Here, we do not treat Length delimited bitstream. Therefore, temporal_unit_size, frame_unit_size and
     * obu_length are not present at all. */
    while( remaining_bytes )
    {
        uint8_t temp8 = lsmash_bs_show_byte( bs, 0 );
        uint8_t obu_forbidden_bit  = (temp8 >> 7) & 0x01;
        uint8_t obu_type           = (temp8 >> 3) & 0x0F;
        uint8_t obu_extension_flag = (temp8 >> 2) & 0x01;
        uint8_t obu_has_size_field = (temp8 >> 1) & 0x01;
        if( obu_forbidden_bit != 0 )
        {
            err = LSMASH_ERR_INVALID_DATA;
            goto fail;
        }
        if( obu_type == AV1_OBU_TYPE_TILE_LIST )
        {
            /* The tile list is not supported in the v1.0.0. */
            err = LSMASH_ERR_INVALID_DATA;
            goto fail;
        }
        uint32_t sz;
        uint32_t obu_size;
        uint32_t obu_header_size = 1 + obu_extension_flag;
        if( obu_has_size_field )
        {
            uint8_t num_leb128bytes;
            obu_size = av1_show_leb128( bs, &num_leb128bytes, obu_header_size );
            sz       = obu_header_size + num_leb128bytes + obu_size;
        }
        else
        {
            /* The current OBU is the last one in the temporal unit. This structure is allowed by the spec of AV1-in-ISOBMFF. */
            sz       = remaining_bytes;
            obu_size = sz - obu_header_size;
        }
        if( remaining_bytes < sz )
            return LSMASH_ERR_INVALID_DATA;
        /* Copy OBU. */
        ptrdiff_t offset   = au_length - remaining_bytes;
        uint8_t  *obu_data = au_data + offset;
        int64_t err64 = lsmash_bs_get_bytes_ex( bs, sz, obu_data );
        if( err64 < 0 )
            return (int)err64;
        remaining_bytes -= sz;
        if( obu_type == AV1_OBU_TYPE_SEQUENCE_HEADER
         || obu_type == AV1_OBU_TYPE_FRAME_HEADER
         || obu_type == AV1_OBU_TYPE_REDUNDANT_FRAME_HEADER
         || obu_type == AV1_OBU_TYPE_TILE_GROUP
         || obu_type == AV1_OBU_TYPE_FRAME )
        {
            /* Make a bytestream from the OBU without its header to parse. */
            lsmash_bs_t *obu_bs   = &(lsmash_bs_t){ 0 };
            if( (err = lsmash_bs_set_empty_stream( obu_bs, obu_data + obu_header_size, obu_size )) < 0 )
                return err;
            lsmash_bits_init( parser->bits, obu_bs );
            switch( obu_type )
            {
                case AV1_OBU_TYPE_SEQUENCE_HEADER :
                    if( (err = av1_parse_sequence_header( parser, summary )) < 0 )
                        goto fail;
                    tu.with_sequence_header = 1;
                    break;
                case AV1_OBU_TYPE_FRAME_HEADER :
                case AV1_OBU_TYPE_REDUNDANT_FRAME_HEADER :
                case AV1_OBU_TYPE_FRAME :
                {
                    /* To get RenderWidth and RenderHeight, we parse the frame header. */
                    int is_redundant_header = (obu_type == AV1_OBU_TYPE_REDUNDANT_FRAME_HEADER);
                    int is_frame_obu        = (obu_type == AV1_OBU_TYPE_FRAME);
                    uint8_t temporal_id = obu_extension_flag ? ((obu_data[1] >> 5) & 0x7) : 0;
                    uint8_t spatial_id  = obu_extension_flag ? ((obu_data[1] >> 3) & 0x3) : 0;
                    /* The temporal_id must be identical within the same temporal unit. */
                    if( tu.temporal_id == (1 << 3) )
                        tu.temporal_id = temporal_id;
                    else if( tu.temporal_id != temporal_id )
                        return LSMASH_ERR_INVALID_DATA;
                    int (*parse_func)( av1_parser_t *, av1_frame_t *, int, int, uint8_t, uint8_t ) =
                        obu_type != AV1_OBU_TYPE_FRAME ? av1_parse_frame_header
                                                       : av1_parse_frame;
                    if( (err = parse_func( parser, frame, is_redundant_header, is_frame_obu, temporal_id, spatial_id )) < 0 )
                        goto fail;
                    parser->MaxRenderWidth  = LSMASH_MAX( parser->MaxRenderWidth,  frame->RenderWidth );
                    parser->MaxRenderHeight = LSMASH_MAX( parser->MaxRenderHeight, frame->RenderHeight );
                    break;
                }
                case AV1_OBU_TYPE_TILE_GROUP :
                    if( (err = av1_parse_tile_group( parser, frame )) < 0 )
                        goto fail;
                    break;
                default :
                    assert( 0 );
            }
            /* No need byte alignment. */
            lsmash_bits_empty( parser->bits );  /* redundant though */
        }
        /* TODO: Temporal delimiter OBU. */
    }
    //prop->ra_flags |= tu.with_sequence_header ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC : 0;
fail:
    return err;
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
    lsmash_bs_t *bs = &(lsmash_bs_t){ 0 };
    int err = lsmash_bs_set_empty_stream( bs, data, data_length );
    if( err < 0 )
        return err;

    /* TODO */
    while( 0 )
    {
        uint8_t temp8 = lsmash_bs_show_byte( bs, 0 );
        uint8_t obu_forbidden_bit  = (temp8 >> 7) & 0x01;
        uint8_t obu_type           = (temp8 >> 3) & 0x0F;
        uint8_t obu_extension_flag = (temp8 >> 2) & 0x01;
        uint8_t obu_has_size_field = (temp8 >> 1) & 0x01;
        if( obu_forbidden_bit != 0 || obu_has_size_field != 1 )
        {
            err = LSMASH_ERR_INVALID_DATA;
            goto fail;
        }
        uint8_t num_leb128bytes;
        uint32_t obu_size = av1_show_leb128( bs, &num_leb128bytes, 1 + obu_extension_flag );
        uint32_t sz = 1 + obu_extension_flag + num_leb128bytes + obu_size;
        if( obu_type != AV1_OBU_TYPE_SEQUENCE_HEADER )
        {
            lsmash_bs_skip_bytes( bs, sz );
            continue;
        }
        lsmash_bs_skip_bytes( bs, 1 + obu_extension_flag );
        av1_parser_t parser;
        av1_setup_parser(&parser, bs);
        av1_parse_sequence_header();
    }

    return 0;
fail:
    return err;
}

uint8_t *lsmash_create_av1_specific_info
(
    lsmash_av1_specific_parameters_t *param,
    uint32_t                         *data_length
)
{
    if( !param || !param->configOBUs || !data_length )
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
    lsmash_bs_put_be32( bs, temp8 );
    temp8 = ((param->seq_tier_0           << 7))
          | ((param->high_bitdepth        << 6) & 0x01)
          | ((param->twelve_bit           << 5) & 0x01)
          | ((param->monochrome           << 4) & 0x01)
          | ((param->chroma_subsampling_x << 3) & 0x01)
          | ((param->chroma_subsampling_y << 2) & 0x01)
          | ( param->chroma_sample_position     & 0x03);
    lsmash_bs_put_be32( bs, temp8 );
    if( param->initial_presentation_delay_present )
        lsmash_bs_put_be32( bs, 0x10 | (param->initial_presentation_delay_minus_one & 0x0F) );
    else
        lsmash_bs_put_be32( bs, 0 );
    /* configOBUs */
    for( int i = 0; i < 2; i++ )
    {
        lsmash_entry_list_t *obu_list = (i == 0 ? param->configOBUs->sequence_header_list : param->configOBUs->metadata_list);
        assert( obu_list );
        for( lsmash_entry_t *entry = obu_list->head; entry; entry = entry->next )
        {
            av1_config_obus_entry_t *config_obu = (av1_config_obus_entry_t *)entry->data;
            if( !config_obu || config_obu->unused )
                continue;
            lsmash_bs_put_bytes( bs, config_obu->sz, config_obu->obu );
        }
    }
    uint8_t *data = lsmash_bs_export_data( bs, data_length );
    lsmash_bs_cleanup( bs );
    /* Update box size. */
    LSMASH_SET_BE32( data, *data_length );
    return data;
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
    uint8_t *data = src->data.unstructured;
    uint64_t size = LSMASH_GET_BE32( data );
    data += ISOM_BASEBOX_COMMON_SIZE;
    if( size == 1 )
    {
        size = LSMASH_GET_BE64( data );
        data += 8;
    }
    if( size != src->size )
        return LSMASH_ERR_INVALID_DATA;
    if( !param->configOBUs )
    {
        param->configOBUs = av1_allocate_configOBUs();
        if( !param->configOBUs )
            return LSMASH_ERR_MEMORY_ALLOC;
    }
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
        return LSMASH_ERR_MEMORY_ALLOC;
    uint32_t cr_size = src->size - (data - src->data.unstructured);
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
    int sequence_header_obu_present = 0;
    uint32_t pos = 4;
    while( pos < cr_size )
    {
        temp8 = lsmash_bs_show_byte( bs, 0 );
        uint8_t obu_forbidden_bit  = (temp8 >> 7) & 0x01;
        uint8_t obu_type           = (temp8 >> 3) & 0x0F;
        uint8_t obu_extension_flag = (temp8 >> 2) & 0x01;
        uint8_t obu_has_size_field = (temp8 >> 1) & 0x01;
        if( obu_forbidden_bit != 0 || obu_has_size_field != 1 )
        {
            err = LSMASH_ERR_INVALID_DATA;
            goto fail;
        }
        uint8_t num_leb128bytes;
        uint32_t obu_size = av1_show_leb128( bs, &num_leb128bytes, 1 + obu_extension_flag );
        uint32_t sz = 1 + obu_extension_flag + num_leb128bytes + obu_size;
        /* Add to the list configOBUs if Sequence Header OBU or Metadata OBUs. */
        if( obu_type == AV1_OBU_TYPE_SEQUENCE_HEADER || obu_type == AV1_OBU_TYPE_METADATA )
        {
            lsmash_entry_list_t *list = NULL;
            if( obu_type == AV1_OBU_TYPE_SEQUENCE_HEADER )
            {
                if( sequence_header_obu_present )
                    /* At most one Sequence Header OBU shall be preset. Here, just skip. */
                    goto skip_config_obu;
                sequence_header_obu_present = 1;
                list = param->configOBUs->sequence_header_list;
            }
            else if( obu_type == AV1_OBU_TYPE_METADATA )
                list = param->configOBUs->metadata_list;
            else
                assert( 0 );
            /* Add this OBU to the selected list. */
            av1_config_obus_entry_t *config_obu = av1_create_config_obus_entry( NULL, sz );
            if( !config_obu )
            {
                err = LSMASH_ERR_MEMORY_ALLOC;
                goto fail;
            }
            if( (err = lsmash_bs_get_bytes_ex( bs, sz, config_obu->obu )) < 0
             || (err = lsmash_list_add_entry( list, config_obu )) < 0 )
            {
                av1_destroy_config_obus_entry( config_obu );
                goto fail;
            }
        }
        else
        {
skip_config_obu:
            lsmash_bs_skip_bytes( bs, sz );
        }
        pos += sz;
    }
    lsmash_bs_cleanup( bs );
    return 0;
fail:
    lsmash_bs_cleanup( bs );
    return err;
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
    av1_deallocate_configOBUs( dst_data );
    *dst_data = *src_data;
    if( !src_data->configOBUs )
        return 0;
    dst_data->configOBUs = av1_allocate_configOBUs();
    if( !dst_data->configOBUs )
        return LSMASH_ERR_MEMORY_ALLOC;
    for( int i = 0; i < 2; i++ )
    {
        lsmash_entry_list_t *src_obu_list = (i == 0 ? src_data->configOBUs->sequence_header_list : src_data->configOBUs->metadata_list);
        lsmash_entry_list_t *dst_obu_list = (i == 0 ? dst_data->configOBUs->sequence_header_list : dst_data->configOBUs->metadata_list);
        assert( src_obu_list && dst_obu_list );
        for( lsmash_entry_t *entry = src_obu_list->head; entry; entry = entry->next )
        {
            av1_config_obus_entry_t *src_obu = (av1_config_obus_entry_t *)entry->data;
            if( !src_obu || src_obu->unused )
                continue;
            av1_config_obus_entry_t *dst_obu = av1_create_config_obus_entry( src_obu->obu, src_obu->sz );
            if( !dst_obu )
            {
                av1_deallocate_configOBUs( dst_data );
                return LSMASH_ERR_MEMORY_ALLOC;
            }
            if( lsmash_list_add_entry( dst_obu_list, dst_obu ) < 0 )
            {
                av1_deallocate_configOBUs( dst_data );
                av1_destroy_config_obus_entry( dst_obu );
                return LSMASH_ERR_MEMORY_ALLOC;
            }
        }
    }
    return 0;
}
