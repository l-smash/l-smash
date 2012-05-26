/*****************************************************************************
 * h264.h:
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

#define H264_DEFAULT_BUFFER_SIZE      (1<<16)
#define H264_DEFAULT_NALU_LENGTH_SIZE 4     /* We always use 4 bytes length. */
#define H264_SHORT_START_CODE_LENGTH  3

typedef struct
{
    uint8_t  nal_ref_idc;
    uint8_t  nal_unit_type;
    uint16_t length;
} h264_nalu_header_t;

typedef struct
{
    uint16_t sar_width;
    uint16_t sar_height;
    uint8_t  video_full_range_flag;
    uint8_t  colour_primaries;
    uint8_t  transfer_characteristics;
    uint8_t  matrix_coefficients;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    uint8_t  fixed_frame_rate_flag;
} h264_vui_t;

typedef struct
{
    uint8_t  present;
    uint8_t  profile_idc;
    uint8_t  constraint_set_flags;
    uint8_t  level_idc;
    uint8_t  seq_parameter_set_id;
    uint8_t  chroma_format_idc;
    uint8_t  separate_colour_plane_flag;
    uint8_t  ChromaArrayType;
    uint8_t  bit_depth_luma_minus8;
    uint8_t  bit_depth_chroma_minus8;
    uint8_t  pic_order_cnt_type;
    uint8_t  delta_pic_order_always_zero_flag;
    uint8_t  num_ref_frames_in_pic_order_cnt_cycle;
    uint8_t  frame_mbs_only_flag;
    uint8_t  hrd_present;
    int32_t  offset_for_non_ref_pic;
    int32_t  offset_for_top_to_bottom_field;
    int32_t  offset_for_ref_frame[255];
    int64_t  ExpectedDeltaPerPicOrderCntCycle;
    uint32_t max_num_ref_frames;
    uint32_t log2_max_frame_num;
    uint32_t MaxFrameNum;
    uint32_t log2_max_pic_order_cnt_lsb;
    uint32_t MaxPicOrderCntLsb;
    uint32_t PicSizeInMapUnits;
    uint32_t cropped_width;
    uint32_t cropped_height;
    h264_vui_t vui;
} h264_sps_t;

typedef struct
{
    uint8_t  present;
    uint8_t  pic_parameter_set_id;
    uint8_t  seq_parameter_set_id;
    uint8_t  entropy_coding_mode_flag;
    uint8_t  bottom_field_pic_order_in_frame_present_flag;
    uint8_t  weighted_pred_flag;
    uint8_t  weighted_bipred_idc;
    uint8_t  deblocking_filter_control_present_flag;
    uint8_t  redundant_pic_cnt_present_flag;
    uint32_t SliceGroupChangeRate;
} h264_pps_t;

typedef struct
{
    uint8_t  present;
    uint8_t  random_accessible;
    uint32_t recovery_frame_cnt;
} h264_sei_t;

typedef struct
{
    uint8_t  present;
    uint8_t  type;
    uint8_t  pic_order_cnt_type;
    uint8_t  nal_ref_idc;
    uint8_t  IdrPicFlag;
    uint8_t  pic_parameter_set_id;
    uint8_t  field_pic_flag;
    uint8_t  bottom_field_flag;
    uint8_t  has_mmco5;
    uint8_t  has_redundancy;
    uint16_t idr_pic_id;
    uint32_t frame_num;
    int32_t  pic_order_cnt_lsb;
    int32_t  delta_pic_order_cnt_bottom;
    int32_t  delta_pic_order_cnt[2];
} h264_slice_info_t;

typedef struct
{
    uint8_t  type;
    uint8_t  idr;
    uint8_t  random_accessible;
    uint8_t  independent;
    uint8_t  disposable;        /* 1: nal_ref_idc == 0, 0: otherwise */
    uint8_t  has_redundancy;
    uint8_t  incomplete_au_has_primary;
    uint8_t  pic_parameter_set_id;
    uint8_t  field_pic_flag;
    uint8_t  bottom_field_flag;
    /* POC */
    uint8_t  has_mmco5;
    uint8_t  ref_pic_has_mmco5;
    uint8_t  ref_pic_bottom_field_flag;
    int32_t  ref_pic_TopFieldOrderCnt;
    int32_t  ref_pic_PicOrderCntMsb;
    int32_t  ref_pic_PicOrderCntLsb;
    int32_t  pic_order_cnt_lsb;
    int32_t  delta_pic_order_cnt_bottom;
    int32_t  delta_pic_order_cnt[2];
    int32_t  PicOrderCnt;
    uint32_t FrameNumOffset;
    /* */
    uint32_t recovery_frame_cnt;
    uint32_t frame_num;
    uint8_t *au;
    uint32_t au_length;
    uint8_t *incomplete_au;
    uint32_t incomplete_au_length;
    uint32_t au_number;
} h264_picture_info_t;

typedef struct h264_info_tag h264_info_t;

typedef struct
{
    lsmash_multiple_buffers_t *bank;
    uint8_t *rbsp;
    uint8_t *start;
    uint8_t *end;
    uint8_t *pos;
    uint32_t (*update)( h264_info_t *, void *, uint32_t );
} h264_stream_buffer_t;

struct h264_info_tag
{
    lsmash_h264_specific_parameters_t avcC_param;
    h264_nalu_header_t  nalu_header;
    h264_sps_t          sps;
    h264_pps_t          pps;
    h264_sei_t          sei;
    h264_slice_info_t   slice;
    h264_picture_info_t picture;
    uint8_t  prev_nalu_type;
    uint8_t  no_more_read;
    uint64_t ebsp_head_pos;
    lsmash_bits_t *bits;
    h264_stream_buffer_t buffer;
};

typedef enum
{
    H264_PICTURE_TYPE_I           = 0,
    H264_PICTURE_TYPE_I_P         = 1,
    H264_PICTURE_TYPE_I_P_B       = 2,
    H264_PICTURE_TYPE_SI          = 3,
    H264_PICTURE_TYPE_SI_SP       = 4,
    H264_PICTURE_TYPE_I_SI        = 5,
    H264_PICTURE_TYPE_I_SI_P_SP   = 6,
    H264_PICTURE_TYPE_I_SI_P_SP_B = 7,
    H264_PICTURE_TYPE_NONE        = 8,
} h264_picture_type;

int h264_setup_parser( h264_info_t *info, int parse_only, uint32_t (*update)( h264_info_t *, void *, uint32_t ) );
void h264_cleanup_parser( h264_info_t *info );
int h264_calculate_poc( h264_sps_t *sps, h264_picture_info_t *picture, h264_picture_info_t *prev_picture );
void h264_update_picture_info_for_slice( h264_picture_info_t *picture, h264_slice_info_t *slice );
void h264_update_picture_info( h264_picture_info_t *picture, h264_slice_info_t *slice, h264_sei_t *sei );
int h264_find_au_delimit_by_slice_info( h264_slice_info_t *slice, h264_slice_info_t *prev_slice );
int h264_find_au_delimit_by_nalu_type( uint8_t nalu_type, uint8_t prev_nalu_type );
int h264_supplement_buffer( h264_stream_buffer_t *buffer, h264_picture_info_t *picture, uint32_t size );
int h264_check_nalu_header( h264_nalu_header_t *nalu_header, uint8_t **p_buf_pos, int use_long_start_code );
int h264_parse_sps_nalu( lsmash_bits_t *bits, h264_sps_t *sps,
                         uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size, int easy_parse );
int h264_parse_pps_nalu( lsmash_bits_t *bits, h264_sps_t *sps, h264_pps_t *pps,
                         uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size );
int h264_parse_sei_nalu( lsmash_bits_t *bits, h264_sei_t *sei,
                         uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size );
int h264_parse_slice( lsmash_bits_t *bits, h264_sps_t *sps, h264_pps_t *pps,
                      h264_slice_info_t *slice, h264_nalu_header_t *nalu_header,
                      uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size );
int h264_try_to_append_parameter_set( h264_info_t *info, lsmash_h264_parameter_set_type ps_type, void *ps_data, uint32_t ps_length );

static inline int h264_check_next_short_start_code( uint8_t *buf_pos, uint8_t *buf_end )
{
    return ((buf_pos + 2) < buf_end) && !buf_pos[0] && !buf_pos[1] && (buf_pos[2] == 0x01);
}
