/*****************************************************************************
 * av1.h
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

#ifndef LSMASH_AV1_H
#define LSMASH_AV1_H

#define AV1_CODEC_CONFIGURATION_RECORD_MARKER (1)
#define AV1_CODEC_CONFIGURATION_RECORD_VERSION_1 (1)

enum
{
    AV1_OBU_TYPE_SEQUENCE_HEADER        = 1,
    AV1_OBU_TYPE_TEMPORAL_DELIMITER     = 2,
    AV1_OBU_TYPE_FRAME_HEADER           = 3,
    AV1_OBU_TYPE_TILE_GROUP             = 4,
    AV1_OBU_TYPE_METADATA               = 5,
    AV1_OBU_TYPE_FRAME                  = 6,
    AV1_OBU_TYPE_REDUNDANT_FRAME_HEADER = 7,
    AV1_OBU_TYPE_TILE_LIST              = 8,
    AV1_OBU_TYPE_PADDING                = 15,
};

enum
{
    AV1_METADATA_TYPE_HDR_CLL     = 1,
    AV1_METADATA_TYPE_HDR_MDCV    = 2,
    AV1_METADATA_TYPE_SCALABILITY = 3,
    AV1_METADATA_TYPE_ITUT_T35    = 4,
    AV1_METADATA_TYPE_TIMECODE    = 5,
};

enum
{
    AV1_REFS_PER_FRAME   = 7,
    AV1_NUM_REF_FRAMES   = 8,
    AV1_TOTAL_REFS_COUNT = 8,

    AV1_MAX_SEGMENTS = 8,
    AV1_SEG_LVL_MAX  = 8,

    AV1_MAX_TILE_WIDTH = 4096,
    AV1_MAX_TILE_AREA  = 4096 * 2304,
    AV1_MAX_TILE_ROWS  = 64,
    AV1_MAX_TILE_COLS  = 64,

    /* frame_type */
    AV1_KEY_FRAME        = 0,
    AV1_INTER_FRAME      = 1,
    AV1_INTRA_ONLY_FRAME = 2,
    AV1_SWITCH_FRAME     = 3,
};

typedef struct
{
    uint8_t  high_bitdepth;
    uint8_t  twelve_bit;
    uint8_t  BitDepth;
    uint8_t  mono_chrome;
    uint8_t  NumPlanes;
    uint8_t  color_description_present_flag;
    uint8_t  color_primaries;
    uint8_t  transfer_characteristics;
    uint8_t  matrix_coefficients;
    uint8_t  color_range;
    uint8_t  subsampling_x;
    uint8_t  subsampling_y;
    uint8_t  chroma_sample_position;
    uint8_t  separate_uv_delta_q;
} av1_color_config_t;

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
    uint64_t decoder_buffer_delay;
    uint64_t encoder_buffer_delay;
    uint8_t  low_delay_mode_flag;
} av1_operating_parameters_info_t;

typedef struct
{
    uint16_t operating_point_idc;
    uint8_t  seq_level_idx;
    uint8_t  seq_tier;
    uint8_t  decoder_model_present_for_this_op;
    av1_operating_parameters_info_t operating_parameters_info;
    uint8_t  initial_display_delay_present_for_this_op;
    uint8_t  initial_display_delay_minus_1;
} av1_operating_point_t;

typedef struct
{
    uint8_t  seq_profile;
    uint8_t  still_picture;
    uint8_t  reduced_still_picture_header;
    uint8_t  timing_info_present_flag;
    av1_timing_info_t timing_info;
    uint8_t  decoder_model_info_present_flag;
    av1_decoder_model_info_t decoder_model_info;
    uint8_t  initial_display_delay_present_flag;
    uint8_t  operating_points_cnt_minus_1;
    av1_operating_point_t op[32];
    uint8_t  frame_width_bits_minus_1;
    uint8_t  frame_height_bits_minus_1;
    uint32_t max_frame_width_minus_1;
    uint32_t max_frame_height_minus_1;
    uint8_t  frame_id_numbers_present_flag;
    uint8_t  delta_frame_id_length_minus_2;
    uint8_t  additional_frame_id_length_minus_1;
    uint8_t  use_128x128_superblock;
    uint8_t  enable_filter_intra;
    uint8_t  enable_intra_edge_filter;
    uint8_t  enable_interintra_compound;
    uint8_t  enable_masked_compound;
    uint8_t  enable_warped_motion;
    uint8_t  enable_dual_filter;
    uint8_t  enable_order_hint;
    uint8_t  enable_jnt_comp;
    uint8_t  enable_ref_frame_mvs;
    uint8_t  seq_choose_screen_content_tools;
    uint8_t  seq_force_screen_content_tools;
    uint8_t  seq_choose_integer_mv;
    uint8_t  seq_force_integer_mv;
    uint8_t  order_hint_bits_minus_1;
    uint8_t  OrderHintBits;
    uint8_t  enable_superres;
    uint8_t  enable_cdef;
    uint8_t  enable_restoration;
    av1_color_config_t color_config;
    uint8_t  film_grain_params_present;
} av1_sequence_header_t;

typedef struct
{
    uint8_t  SeenFrameHeader;
    uint8_t  show_existing_frame;
    uint8_t  frame_to_show_map_idx;
    uint32_t display_frame_id;
    uint8_t  frame_type;
    uint8_t  FrameIsIntra;
    uint8_t  show_frame;
    uint64_t frame_presentation_time;
    uint8_t  showable_frame;
    uint8_t  error_resilient_mode;
    uint8_t  disable_cdf_update;
    uint8_t  allow_screen_content_tools;
    uint8_t  force_integer_mv;
    uint32_t current_frame_id;
    uint8_t  frame_size_override_flag;
    uint32_t order_hint;
    uint32_t OrderHint;
    uint8_t  primary_ref_frame;
    uint8_t  buffer_removal_time_present_flag;
    uint64_t buffer_removal_time[32];
    uint8_t  refresh_frame_flags;
    uint32_t ref_order_hint[AV1_NUM_REF_FRAMES];
    uint32_t frame_width_minus_1;
    uint32_t frame_height_minus_1;
    uint8_t  use_superres;
    uint8_t  coded_denom;
    uint32_t SuperresDenom;
    uint32_t UpscaledWidth;
    uint32_t FrameWidth;
    uint32_t FrameHeight;
    uint8_t  render_and_frame_size_different;
    uint32_t render_width_minus_1;
    uint32_t render_height_minus_1;
    uint32_t RenderWidth;
    uint32_t RenderHeight;
    uint32_t MiCols;
    uint32_t MiRows;
    uint8_t  allow_intrabc;
    uint8_t  frame_refs_short_signaling;
    uint8_t  last_frame_idx;
    uint8_t  gold_frame_idx;
    int      ref_frame_idx[AV1_REFS_PER_FRAME];
    uint32_t delta_frame_id_minus_1;
    uint8_t  allow_high_precision_mv;
    uint8_t  is_filter_switchable;
    uint8_t  interpolation_filter;
    uint8_t  is_motion_mode_switchable;
    uint8_t  use_ref_frame_mvs;
    uint8_t  disable_frame_end_update_cdf;
    /* Reference frame setting helpers */
    uint8_t  usedFrame[AV1_NUM_REF_FRAMES];
    int      curFrameHint;
    int      shiftedOrderHints[AV1_NUM_REF_FRAMES];
    int      lastOrderHint;
    int      goldOrderHint;
    int      latestOrderHint;
    int      earliestOrderHint;
    uint32_t OrderHints[AV1_TOTAL_REFS_COUNT];
    uint8_t  RefFrameSignBias[AV1_TOTAL_REFS_COUNT];
    /* Tile info */
    uint32_t TileColsLog2;
    uint32_t TileCols;
    uint32_t TileRowsLog2;
    uint32_t TileRows;
    uint32_t MiColStarts[AV1_MAX_TILE_COLS + 1];
    uint32_t MiRowStarts[AV1_MAX_TILE_ROWS + 1];
    uint32_t context_update_tile_id;
    uint8_t  tile_size_bytes_minus_1;
    uint8_t  TileSizeBytes;
    uint32_t NumTiles;
    uint32_t TileNum;
    uint8_t  tile_start_and_end_present_flag;
    /* Quantization / Segmentation / Lossless state */
    uint8_t  base_q_idx;
    int8_t   DeltaQYDc;
    int8_t   DeltaQUDc;
    int8_t   DeltaQUAc;
    int8_t   DeltaQVDc;
    int8_t   DeltaQVAc;
    uint8_t  segmentation_enabled;
    uint8_t  FeatureEnabled[AV1_MAX_SEGMENTS][AV1_SEG_LVL_MAX];
    int16_t  FeatureData   [AV1_MAX_SEGMENTS][AV1_SEG_LVL_MAX];
    uint8_t  LosslessArray [AV1_MAX_SEGMENTS];
    uint8_t  CodedLossless;
    uint8_t  AllLossless;
    uint8_t  delta_q_present;
    uint8_t  delta_q_res;
    uint8_t  delta_lf_present;
    uint8_t  delta_lf_res;
    uint8_t  delta_lf_multi;
    uint8_t  reference_select;
    uint8_t  skipModeAllowed;
    uint8_t  skip_mode_present;
    uint8_t  allow_warped_motion;
    uint8_t  reduced_tx_set;
} av1_frame_t;

typedef struct
{
    uint8_t temporal_id;
    uint8_t with_sequence_header;
    uint8_t recording_config_obus;
    uint8_t seen_frame_header;
    uint8_t first_frame_type;
    uint8_t first_show_frame;
    uint8_t first_show_existing;
} av1_temporal_unit_t;

typedef struct
{
    lsmash_bits_t        *bits;
    av1_sequence_header_t sequence_header;
    av1_frame_t           frame;
    uint8_t               seen_seq_header;
    uint8_t               has_hdr10p;
    /* Reference slot state (AV1 Spec Section 7.20) */
    uint8_t               RefValid         [AV1_NUM_REF_FRAMES];
    uint32_t              RefFrameId       [AV1_NUM_REF_FRAMES];
    uint32_t              RefUpscaledWidth [AV1_NUM_REF_FRAMES];
    uint32_t              RefFrameWidth    [AV1_NUM_REF_FRAMES];
    uint32_t              RefFrameHeight   [AV1_NUM_REF_FRAMES];
    uint32_t              RefRenderWidth   [AV1_NUM_REF_FRAMES];
    uint32_t              RefRenderHeight  [AV1_NUM_REF_FRAMES];
    uint32_t              RefOrderHint     [AV1_NUM_REF_FRAMES];
    uint8_t               RefFrameType     [AV1_NUM_REF_FRAMES];
    uint8_t               RefFeatureEnabled[AV1_NUM_REF_FRAMES][AV1_MAX_SEGMENTS][AV1_SEG_LVL_MAX];
    int16_t               RefFeatureData   [AV1_NUM_REF_FRAMES][AV1_MAX_SEGMENTS][AV1_SEG_LVL_MAX];
    /* Max render dimensions across stream */
    uint32_t              MaxRenderWidth;
    uint32_t              MaxRenderHeight;
} av1_parser_t;

uint32_t av1_get_uvlc( lsmash_bits_t *bits );
uint64_t av1_get_leb128( lsmash_bs_t *bs, uint8_t *Leb128Bytes );
uint64_t av1_show_leb128( lsmash_bs_t *bs, uint32_t offset, uint8_t *Leb128Bytes );
int64_t  av1_get_su( lsmash_bits_t *bits, unsigned int n );
uint64_t av1_get_ns( lsmash_bits_t *bits, unsigned int n );

int  av1_setup_parser( av1_parser_t *parser, lsmash_bs_t *bs );
void av1_cleanup_parser( av1_parser_t *parser );
void av1_reset_parser( av1_parser_t *parser );
int  av1_parse_sequence_header( av1_parser_t *parser );
int  av1_parse_temporal_unit
(
    av1_parser_t                     *parser,
    const uint8_t                    *src_data,
    uint32_t                          src_length,
    uint8_t                          *dst_data,
    uint32_t                         *dst_length,
    lsmash_sample_property_t         *prop,
    lsmash_av1_specific_parameters_t *param
);

int av1_construct_specific_parameters( lsmash_codec_specific_t *dst, lsmash_codec_specific_t *src );
int av1_copy_codec_specific( lsmash_codec_specific_t *dst, lsmash_codec_specific_t *src );
int av1_print_codec_specific( FILE *fp, lsmash_file_t *file, isom_box_t *box, int level );

#endif /* LSMASH_AV1_H */
