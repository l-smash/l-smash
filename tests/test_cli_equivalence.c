/*****************************************************************************
 * test_cli_equivalence.c
 *****************************************************************************
 * Copyright (c) 2026 Yusuke Nakamura
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
 * THIS SOFTWARE.
 *****************************************************************************/

/* This file is available under an ISC license. */

#include "common/internal.h" /* must be placed first */

#include <time.h>

#include "test_common.h"

#ifdef _WIN32
#define CLI_MUXER          "cli\\muxer.exe"
#define CLI_REMUXER        "cli\\remuxer.exe"
#define CLI_TIMELINEEDITOR "cli\\timelineeditor.exe"
#define CLI_LSHMUXER       "cli\\lshmuxer.exe"
#define CLI_BOXDUMPER      "cli\\boxdumper.exe"
#define CLI_DEV_NULL       "NUL"
#else
#define CLI_MUXER          "./cli/muxer"
#define CLI_REMUXER        "./cli/remuxer"
#define CLI_TIMELINEEDITOR "./cli/timelineeditor"
#define CLI_LSHMUXER       "./cli/lshmuxer"
#define CLI_BOXDUMPER      "./cli/boxdumper"
#define CLI_DEV_NULL       "/dev/null"
#endif

static void append_annexb_ebsp( uint8_t *dst, uint32_t *pos, uint8_t nal_hdr, const uint8_t *rbsp, uint32_t rbsp_len )
{
    dst[(*pos)++] = 0x00;
    dst[(*pos)++] = 0x00;
    dst[(*pos)++] = 0x00;
    dst[(*pos)++] = 0x01;
    dst[(*pos)++] = nal_hdr;
    uint32_t zero_run = 0;
    for( uint32_t i = 0; i < rbsp_len; i++ )
    {
        if( zero_run == 2 && rbsp[i] <= 0x03 )
        {
            dst[(*pos)++] = 0x03;
            zero_run = 0;
        }
        dst[(*pos)++] = rbsp[i];
        zero_run = (rbsp[i] == 0x00) ? (zero_run + 1) : 0;
    }
}

static void append_hevc_annexb_ebsp( uint8_t *dst, uint32_t *pos, uint8_t nal_type, const uint8_t *rbsp, uint32_t rbsp_len )
{
    dst[(*pos)++] = 0x00;
    dst[(*pos)++] = 0x00;
    dst[(*pos)++] = 0x00;
    dst[(*pos)++] = 0x01;
    dst[(*pos)++] = (uint8_t)((nal_type << 1) & 0x7E);
    dst[(*pos)++] = 0x01; /* nuh_layer_id = 0, nuh_temporal_id_plus1 = 1 */
    uint32_t zero_run = 0;
    for( uint32_t i = 0; i < rbsp_len; i++ )
    {
        if( zero_run == 2 && rbsp[i] <= 0x03 )
        {
            dst[(*pos)++] = 0x03;
            zero_run = 0;
        }
        dst[(*pos)++] = rbsp[i];
        zero_run = (rbsp[i] == 0x00) ? (zero_run + 1) : 0;
    }
}

static void build_h264_sps_nal
(
    uint8_t  *dst,
    uint32_t *pos,
    uint32_t  width_mbs,
    uint32_t  height_mbs,
    uint16_t  sar_w,
    uint16_t  sar_h,
    uint32_t  num_units_in_tick,
    uint32_t  time_scale,
    int       fixed_frame_rate_flag
)
{
    uint8_t rbsp[64] = { 0 };
    test_bit_writer_t bw = { rbsp, 0 };
    bw_put( &bw, 8, 100 );      /* profile_idc = High */
    bw_put( &bw, 8, 0 );        /* constraint_set_flags */
    bw_put( &bw, 8, 30 );       /* level_idc = 3.0 */
    bw_put_ue( &bw, 0 );        /* seq_parameter_set_id = 0 */
    bw_put_ue( &bw, 1 );        /* chroma_format_idc = 1 (4:2:0) */
    bw_put_ue( &bw, 0 );        /* bit_depth_luma_minus8 = 0 */
    bw_put_ue( &bw, 0 );        /* bit_depth_chroma_minus8 = 0 */
    bw_put( &bw, 1, 0 );        /* qpprime_y_zero_transform_bypass_flag = 0 */
    bw_put( &bw, 1, 0 );        /* seq_scaling_matrix_present_flag = 0 */
    bw_put_ue( &bw, 0 );        /* log2_max_frame_num_minus4 = 0 */
    bw_put_ue( &bw, 0 );        /* pic_order_cnt_type = 0 */
    bw_put_ue( &bw, 0 );        /* log2_max_pic_order_cnt_lsb_minus4 = 0 */
    bw_put_ue( &bw, 2 );        /* max_num_ref_frames = 2 */
    bw_put( &bw, 1, 0 );        /* gaps_in_frame_num_value_allowed_flag = 0 */
    bw_put_ue( &bw, width_mbs - 1 );
    bw_put_ue( &bw, height_mbs - 1 );
    bw_put( &bw, 1, 1 );        /* frame_mbs_only_flag = 1 */
    bw_put( &bw, 1, 1 );        /* direct_8x8_inference_flag = 1 */
    bw_put( &bw, 1, 0 );        /* frame_cropping_flag = 0 */
    bw_put( &bw, 1, 1 );        /* vui_parameters_present_flag = 1 */
    if( sar_w && sar_h )
    {
        bw_put( &bw, 1, 1 );    /* aspect_ratio_info_present_flag = 1 */
        bw_put( &bw, 8, 255 );  /* Extended_SAR */
        bw_put( &bw, 16, sar_w );
        bw_put( &bw, 16, sar_h );
    }
    else
        bw_put( &bw, 1, 0 );    /* aspect_ratio_info_present_flag = 0 */
    bw_put( &bw, 1, 0 );        /* overscan_info_present_flag = 0 */
    bw_put( &bw, 1, 0 );        /* video_signal_type_present_flag = 0 */
    bw_put( &bw, 1, 0 );        /* chroma_loc_info_present_flag = 0 */
    if( time_scale > 0 )
    {
        bw_put( &bw, 1, 1 );    /* timing_info_present_flag = 1 */
        bw_put( &bw, 32, num_units_in_tick );
        bw_put( &bw, 32, time_scale );
        bw_put( &bw, 1, fixed_frame_rate_flag ? 1 : 0 );
    }
    else
        bw_put( &bw, 1, 0 );
    bw_put( &bw, 1, 0 );        /* nal_hrd_parameters_present_flag = 0 */
    bw_put( &bw, 1, 0 );        /* vcl_hrd_parameters_present_flag = 0 */
    bw_put( &bw, 1, 0 );        /* pic_struct_present_flag = 0 */
    bw_put( &bw, 1, 0 );        /* bitstream_restriction_flag = 0 */
    bw_put_trailing( &bw );
    append_annexb_ebsp( dst, pos, 0x67, rbsp, bw.pos >> 3 );
}

static void build_h264_pps_nal( uint8_t *dst, uint32_t *pos )
{
    uint8_t rbsp[32] = { 0 };
    test_bit_writer_t bw = { rbsp, 0 };
    bw_put_ue( &bw, 0 );        /* pic_parameter_set_id = 0 */
    bw_put_ue( &bw, 0 );        /* seq_parameter_set_id = 0 */
    bw_put( &bw, 1, 0 );        /* entropy_coding_mode_flag = 0 */
    bw_put( &bw, 1, 0 );        /* bottom_field_pic_order_in_frame_present_flag = 0 */
    bw_put_ue( &bw, 0 );        /* num_slice_groups_minus1 = 0 */
    bw_put_ue( &bw, 0 );        /* num_ref_idx_l0_default_active_minus1 = 0 */
    bw_put_ue( &bw, 0 );        /* num_ref_idx_l1_default_active_minus1 = 0 */
    bw_put( &bw, 1, 0 );        /* weighted_pred_flag = 0 */
    bw_put( &bw, 2, 0 );        /* weighted_bipred_idc = 0 */
    bw_put_ue( &bw, 0 );        /* pic_init_qp_minus26 = 0 */
    bw_put_ue( &bw, 0 );        /* pic_init_qs_minus26 = 0 */
    bw_put_ue( &bw, 0 );        /* chroma_qp_index_offset = 0 */
    bw_put( &bw, 1, 0 );        /* deblocking_filter_control_present_flag = 0 */
    bw_put( &bw, 1, 0 );        /* constrained_intra_pred_flag = 0 */
    bw_put( &bw, 1, 0 );        /* redundant_pic_cnt_present_flag = 0 */
    bw_put_trailing( &bw );
    append_annexb_ebsp( dst, pos, 0x68, rbsp, bw.pos >> 3 );
}

static void build_h264_slice_nal
(
    uint8_t  *dst,
    uint32_t *pos,
    int       is_idr,
    uint32_t  nal_ref_idc,
    uint32_t  slice_type,
    uint32_t  frame_num,
    uint32_t  idr_pic_id,
    uint32_t  poc_lsb
)
{
    uint8_t rbsp[32] = { 0 };
    test_bit_writer_t bw = { rbsp, 0 };
    bw_put_ue( &bw, 0 );            /* first_mb_in_slice = 0 */
    bw_put_ue( &bw, slice_type );   /* slice_type: 7=I, 5=P, 6=B */
    bw_put_ue( &bw, 0 );            /* pic_parameter_set_id = 0 */
    bw_put( &bw, 4, frame_num & 0x0F );
    if( is_idr )
        bw_put_ue( &bw, idr_pic_id );
    bw_put( &bw, 4, poc_lsb & 0x0F );
    if( slice_type == 6 || slice_type == 1 )
        bw_put( &bw, 1, 1 );        /* direct_spatial_mv_pred_flag = 1 */
    if( slice_type == 5 || slice_type == 0 || slice_type == 6 || slice_type == 1 )
    {
        bw_put( &bw, 1, 0 );        /* num_ref_idx_active_override_flag = 0 */
        bw_put( &bw, 1, 0 );        /* ref_pic_list_modification_flag_l0 = 0 */
        if( slice_type == 6 || slice_type == 1 )
            bw_put( &bw, 1, 0 );    /* ref_pic_list_modification_flag_l1 = 0 */
    }
    if( nal_ref_idc != 0 )
    {
        if( is_idr )
        {
            bw_put( &bw, 1, 0 );    /* no_output_of_prior_pics_flag = 0 */
            bw_put( &bw, 1, 0 );    /* long_term_reference_flag = 0 */
        }
        else
            bw_put( &bw, 1, 0 );    /* adaptive_ref_pic_marking_mode_flag = 0 */
    }
    bw_put_ue( &bw, 0 );            /* slice_qp_delta = 0 */
    bw_put_trailing( &bw );
    uint32_t rbsp_len = bw.pos >> 3;
    rbsp[rbsp_len++] = (uint8_t)(0x80 | (poc_lsb & 0x0F));
    uint8_t nal_hdr = (uint8_t)(((nal_ref_idc & 3) << 5) | (is_idr ? 5 : 1));
    append_annexb_ebsp( dst, pos, nal_hdr, rbsp, rbsp_len );
}

static uint32_t build_h264_stream
(
    uint8_t *dst,
    uint32_t num_units_in_tick,
    uint32_t time_scale,
    int      fixed_frame_rate_flag,
    int      with_b_frames,
    int      mid_stream_sar_change
)
{
    uint32_t pos = 0;
    build_h264_sps_nal( dst, &pos, 4, 4, 1, 1, num_units_in_tick, time_scale, fixed_frame_rate_flag );
    build_h264_pps_nal( dst, &pos );
    if( with_b_frames )
    {
        /* Decoding order: I0 (POC 0), P2 (POC 4), B1 (POC 2), P3 (POC 6) */
        build_h264_slice_nal( dst, &pos, 1, 3, 7, 0, 0, 0 );
        build_h264_slice_nal( dst, &pos, 0, 2, 5, 1, 0, 4 );
        build_h264_slice_nal( dst, &pos, 0, 0, 6, 2, 0, 2 );
        build_h264_slice_nal( dst, &pos, 0, 2, 5, 2, 0, 6 );
    }
    else
    {
        build_h264_slice_nal( dst, &pos, 1, 3, 7, 0, 0, 0 );
        build_h264_slice_nal( dst, &pos, 0, 2, 5, 1, 0, 2 );
        if( mid_stream_sar_change )
        {
            build_h264_sps_nal( dst, &pos, 4, 4, 16, 11, num_units_in_tick, time_scale, fixed_frame_rate_flag );
            build_h264_pps_nal( dst, &pos );
            build_h264_slice_nal( dst, &pos, 1, 3, 7, 0, 1, 0 );
            build_h264_slice_nal( dst, &pos, 0, 2, 5, 1, 1, 2 );
        }
        else
        {
            build_h264_slice_nal( dst, &pos, 0, 2, 5, 2, 0, 4 );
            build_h264_slice_nal( dst, &pos, 0, 2, 5, 3, 0, 6 );
        }
    }
    return pos;
}

static void bw_put_hevc_ptl( test_bit_writer_t *bw )
{
    bw_put( bw, 2, 0 );             /* general_profile_space = 0 */
    bw_put( bw, 1, 0 );             /* general_tier_flag = 0 */
    bw_put( bw, 5, 1 );             /* general_profile_idc = 1 (Main) */
    bw_put( bw, 32, 0x60000000u );  /* general_profile_compatibility_flags */
    bw_put( bw, 1, 1 );             /* general_progressive_source_flag = 1 */
    bw_put( bw, 1, 0 );             /* general_interlaced_source_flag = 0 */
    bw_put( bw, 1, 1 );             /* general_non_packed_constraint_flag = 1 */
    bw_put( bw, 1, 1 );             /* general_frame_only_constraint_flag = 1 */
    bw_put( bw, 32, 0 );            /* general_reserved_zero_44bits (hi 32) */
    bw_put( bw, 12, 0 );            /* general_reserved_zero_44bits (lo 12) */
    bw_put( bw, 8, 93 );            /* general_level_idc = 93 (Level 3.1) */
}

static uint32_t build_hevc_hdr10p_stream( uint8_t *dst )
{
    uint32_t pos = 0;

    /* VPS (nal_unit_type = 32) */
    {
        uint8_t rbsp[64] = { 0 };
        test_bit_writer_t bw = { rbsp, 0 };
        bw_put( &bw, 4, 0 );        /* vps_video_parameter_set_id = 0 */
        bw_put( &bw, 2, 3 );        /* vps_reserved_three_2bits = 3 */
        bw_put( &bw, 6, 0 );        /* vps_max_layers_minus1 = 0 */
        bw_put( &bw, 3, 0 );        /* vps_max_sub_layers_minus1 = 0 */
        bw_put( &bw, 1, 1 );        /* vps_temporal_id_nesting_flag = 1 */
        bw_put( &bw, 16, 0xFFFF );  /* vps_reserved_0xffff_16bits */
        bw_put_hevc_ptl( &bw );
        bw_put( &bw, 1, 0 );        /* vps_sub_layer_ordering_info_present_flag = 0 */
        bw_put_ue( &bw, 1 );        /* vps_max_dec_pic_buffering_minus1[0] = 1 */
        bw_put_ue( &bw, 0 );        /* vps_max_num_reorder_pics[0] = 0 */
        bw_put_ue( &bw, 0 );        /* vps_max_latency_increase_plus1[0] = 0 */
        bw_put( &bw, 6, 0 );        /* vps_max_layer_id = 0 */
        bw_put_ue( &bw, 0 );        /* vps_num_layer_sets_minus1 = 0 */
        bw_put( &bw, 1, 0 );        /* vps_timing_info_present_flag = 0 */
        bw_put( &bw, 1, 0 );        /* vps_extension_flag = 0 */
        bw_put_trailing( &bw );
        append_hevc_annexb_ebsp( dst, &pos, 32, rbsp, bw.pos >> 3 );
    }

    /* SPS (nal_unit_type = 33) */
    {
        uint8_t rbsp[64] = { 0 };
        test_bit_writer_t bw = { rbsp, 0 };
        bw_put( &bw, 4, 0 );        /* sps_video_parameter_set_id = 0 */
        bw_put( &bw, 3, 0 );        /* sps_max_sub_layers_minus1 = 0 */
        bw_put( &bw, 1, 1 );        /* sps_temporal_id_nesting_flag = 1 */
        bw_put_hevc_ptl( &bw );
        bw_put_ue( &bw, 0 );        /* sps_seq_parameter_set_id = 0 */
        bw_put_ue( &bw, 1 );        /* chroma_format_idc = 1 (4:2:0) */
        bw_put_ue( &bw, 64 );       /* pic_width_in_luma_samples = 64 */
        bw_put_ue( &bw, 64 );       /* pic_height_in_luma_samples = 64 */
        bw_put( &bw, 1, 0 );        /* conformance_window_flag = 0 */
        bw_put_ue( &bw, 0 );        /* bit_depth_luma_minus8 = 0 */
        bw_put_ue( &bw, 0 );        /* bit_depth_chroma_minus8 = 0 */
        bw_put_ue( &bw, 0 );        /* log2_max_pic_order_cnt_lsb_minus4 = 0 */
        bw_put( &bw, 1, 0 );        /* sps_sub_layer_ordering_info_present_flag = 0 */
        bw_put_ue( &bw, 1 );        /* sps_max_dec_pic_buffering_minus1[0] = 1 */
        bw_put_ue( &bw, 0 );        /* sps_max_num_reorder_pics[0] = 0 */
        bw_put_ue( &bw, 0 );        /* sps_max_latency_increase_plus1[0] = 0 */
        bw_put_ue( &bw, 0 );        /* log2_min_luma_coding_block_size_minus3 = 0 */
        bw_put_ue( &bw, 3 );        /* log2_diff_max_min_luma_coding_block_size = 3 (CTB=64) */
        bw_put_ue( &bw, 0 );        /* log2_min_transform_block_size_minus2 = 0 */
        bw_put_ue( &bw, 3 );        /* log2_diff_max_min_transform_block_size = 3 */
        bw_put_ue( &bw, 0 );        /* max_transform_hierarchy_depth_inter = 0 */
        bw_put_ue( &bw, 0 );        /* max_transform_hierarchy_depth_intra = 0 */
        bw_put( &bw, 1, 0 );        /* scaling_list_enabled_flag = 0 */
        bw_put( &bw, 1, 0 );        /* amp_enabled_flag = 0 */
        bw_put( &bw, 1, 0 );        /* sample_adaptive_offset_enabled_flag = 0 */
        bw_put( &bw, 1, 0 );        /* pcm_enabled_flag = 0 */
        bw_put_ue( &bw, 0 );        /* num_short_term_ref_pic_sets = 0 */
        bw_put( &bw, 1, 0 );        /* long_term_ref_pics_present_flag = 0 */
        bw_put( &bw, 1, 0 );        /* sps_temporal_mvp_enabled_flag = 0 */
        bw_put( &bw, 1, 0 );        /* strong_intra_smoothing_enabled_flag = 0 */
        bw_put( &bw, 1, 1 );        /* vui_parameters_present_flag = 1 */
        bw_put( &bw, 1, 0 );        /* aspect_ratio_info_present_flag = 0 */
        bw_put( &bw, 1, 0 );        /* overscan_info_present_flag = 0 */
        bw_put( &bw, 1, 0 );        /* video_signal_type_present_flag = 0 */
        bw_put( &bw, 1, 0 );        /* chroma_loc_info_present_flag = 0 */
        bw_put( &bw, 1, 0 );        /* neutral_chroma_indication_flag = 0 */
        bw_put( &bw, 1, 0 );        /* field_seq_flag = 0 */
        bw_put( &bw, 1, 0 );        /* frame_field_info_present_flag = 0 */
        bw_put( &bw, 1, 0 );        /* default_display_window_flag = 0 */
        bw_put( &bw, 1, 1 );        /* vui_timing_info_present_flag = 1 */
        bw_put( &bw, 32, 1001 );    /* vui_num_units_in_tick = 1001 */
        bw_put( &bw, 32, 24000 );   /* vui_time_scale = 24000 */
        bw_put( &bw, 1, 1 );        /* vui_poc_proportional_to_timing_flag = 1 */
        bw_put_ue( &bw, 0 );        /* vui_num_ticks_poc_diff_one_minus1 = 0 */
        bw_put( &bw, 1, 0 );        /* vui_hrd_parameters_present_flag = 0 */
        bw_put( &bw, 1, 0 );        /* bitstream_restriction_flag = 0 */
        bw_put( &bw, 1, 0 );        /* sps_extension_present_flag = 0 */
        bw_put_trailing( &bw );
        append_hevc_annexb_ebsp( dst, &pos, 33, rbsp, bw.pos >> 3 );
    }

    /* PPS (nal_unit_type = 34) */
    {
        uint8_t rbsp[32] = { 0 };
        test_bit_writer_t bw = { rbsp, 0 };
        bw_put_ue( &bw, 0 );        /* pps_pic_parameter_set_id = 0 */
        bw_put_ue( &bw, 0 );        /* pps_seq_parameter_set_id = 0 */
        bw_put( &bw, 1, 0 );        /* dependent_slice_segments_enabled_flag = 0 */
        bw_put( &bw, 1, 0 );        /* output_flag_present_flag = 0 */
        bw_put( &bw, 3, 0 );        /* num_extra_slice_header_bits = 0 */
        bw_put( &bw, 1, 0 );        /* sign_data_hiding_enabled_flag = 0 */
        bw_put( &bw, 1, 0 );        /* cabac_init_present_flag = 0 */
        bw_put_ue( &bw, 0 );        /* num_ref_idx_l0_default_active_minus1 = 0 */
        bw_put_ue( &bw, 0 );        /* num_ref_idx_l1_default_active_minus1 = 0 */
        bw_put_ue( &bw, 0 );        /* init_qp_minus26 = 0 */
        bw_put( &bw, 1, 0 );        /* constrained_intra_pred_flag = 0 */
        bw_put( &bw, 1, 0 );        /* transform_skip_enabled_flag = 0 */
        bw_put( &bw, 1, 0 );        /* cu_qp_delta_enabled_flag = 0 */
        bw_put_ue( &bw, 0 );        /* pps_cb_qp_offset = 0 */
        bw_put_ue( &bw, 0 );        /* pps_cr_qp_offset = 0 */
        bw_put( &bw, 1, 0 );        /* pps_slice_chroma_qp_offsets_present_flag = 0 */
        bw_put( &bw, 1, 0 );        /* weighted_pred_flag = 0 */
        bw_put( &bw, 1, 0 );        /* weighted_bipred_flag = 0 */
        bw_put( &bw, 1, 0 );        /* transquant_bypass_enabled_flag = 0 */
        bw_put( &bw, 1, 0 );        /* tiles_enabled_flag = 0 */
        bw_put( &bw, 1, 0 );        /* entropy_coding_sync_enabled_flag = 0 */
        bw_put( &bw, 1, 0 );        /* pps_loop_filter_across_slices_enabled_flag = 0 */
        bw_put( &bw, 1, 0 );        /* deblocking_filter_control_present_flag = 0 */
        bw_put( &bw, 1, 0 );        /* pps_scaling_list_data_present_flag = 0 */
        bw_put( &bw, 1, 0 );        /* lists_modification_present_flag = 0 */
        bw_put_ue( &bw, 0 );        /* log2_parallel_merge_level_minus2 = 0 */
        bw_put( &bw, 1, 0 );        /* slice_segment_header_extension_present_flag = 0 */
        bw_put( &bw, 1, 0 );        /* pps_extension_present_flag = 0 */
        bw_put_trailing( &bw );
        append_hevc_annexb_ebsp( dst, &pos, 34, rbsp, bw.pos >> 3 );
    }

    /* Prefix SEI (nal_unit_type = 39) with ITU-T T.35 HDR10+ payload (payloadType = 4, payloadSize = 9) */
    {
        static const uint8_t sei_rbsp[] =
        {
            0x04, 0x09,
            0xB5, 0x00, 0x3C, 0x00, 0x01, 0x04, 0x01, 0x00, 0x00,
            0x80
        };
        append_hevc_annexb_ebsp( dst, &pos, 39, sei_rbsp, sizeof(sei_rbsp) );
    }

    /* Frame 0: IDR_W_RADL (nal_unit_type = 19) */
    {
        uint8_t rbsp[16] = { 0 };
        test_bit_writer_t bw = { rbsp, 0 };
        bw_put( &bw, 1, 1 );        /* first_slice_segment_in_pic_flag = 1 */
        bw_put( &bw, 1, 0 );        /* no_output_of_prior_pics_flag = 0 */
        bw_put_ue( &bw, 0 );        /* slice_pic_parameter_set_id = 0 */
        bw_put_ue( &bw, 2 );        /* slice_type = 2 (I) */
        bw_put_trailing( &bw );
        uint32_t len = bw.pos >> 3;
        rbsp[len++] = 0x80;
        append_hevc_annexb_ebsp( dst, &pos, 19, rbsp, len );
    }

    /* Frame 1 & 2: TRAIL_R (nal_unit_type = 1), POC = 1, 2 */
    for( uint32_t poc = 1; poc <= 2; poc++ )
    {
        uint8_t rbsp[16] = { 0 };
        test_bit_writer_t bw = { rbsp, 0 };
        bw_put( &bw, 1, 1 );        /* first_slice_segment_in_pic_flag = 1 */
        bw_put_ue( &bw, 0 );        /* slice_pic_parameter_set_id = 0 */
        bw_put_ue( &bw, 1 );        /* slice_type = 1 (P) */
        bw_put( &bw, 4, poc );      /* slice_pic_order_cnt_lsb */
        bw_put( &bw, 1, 0 );        /* short_term_ref_pic_set_sps_flag = 0 */
        bw_put_ue( &bw, 0 );        /* num_negative_pics = 0 */
        bw_put_ue( &bw, 0 );        /* num_positive_pics = 0 */
        bw_put_trailing( &bw );
        uint32_t len = bw.pos >> 3;
        rbsp[len++] = (uint8_t)(0x80 | poc);
        append_hevc_annexb_ebsp( dst, &pos, 1, rbsp, len );
    }

    return pos;
}

static uint32_t build_av1_keyframe_obu_with_render_size( uint8_t *dst, uint32_t order_hint, uint32_t render_w, uint32_t render_h )
{
    uint8_t payload[64] = { 0 };
    lsmash_bs_t *bs = lsmash_bs_create();
    lsmash_bits_t bits = { 0 };
    lsmash_bits_init( &bits, bs );

    lsmash_bits_put( &bits, 1, 0 ); /* show_existing_frame = 0 */
    lsmash_bits_put( &bits, 2, AV1_KEY_FRAME );
    lsmash_bits_put( &bits, 1, 1 ); /* show_frame = 1 */
    lsmash_bits_put( &bits, 1, 1 ); /* disable_cdf_update = 1 */
    lsmash_bits_put( &bits, 1, 0 ); /* allow_screen_content_tools = 0 */
    lsmash_bits_put( &bits, 1, 0 ); /* frame_size_override_flag = 0 */
    lsmash_bits_put( &bits, 8, order_hint );
    if( render_w && render_h )
    {
        lsmash_bits_put( &bits, 1, 1 ); /* render_and_frame_size_different = 1 */
        lsmash_bits_put( &bits, 16, render_w - 1 );
        lsmash_bits_put( &bits, 16, render_h - 1 );
    }
    else
        lsmash_bits_put( &bits, 1, 0 ); /* render_and_frame_size_different = 0 */
    lsmash_bits_put( &bits, 1, 1 ); /* uniform_tile_spacing_flag = 1 */
    lsmash_bits_put( &bits, 8, 0 ); /* base_q_idx = 0 (AllLossless = 1) */
    lsmash_bits_put( &bits, 1, 0 ); /* DeltaQYDc: delta_coded = 0 */
    lsmash_bits_put( &bits, 1, 0 ); /* DeltaQUDc: delta_coded = 0 */
    lsmash_bits_put( &bits, 1, 0 ); /* DeltaQUAc: delta_coded = 0 */
    lsmash_bits_put( &bits, 1, 0 ); /* using_qmatrix = 0 */
    lsmash_bits_put( &bits, 1, 0 ); /* segmentation_enabled = 0 */
    lsmash_bits_put( &bits, 1, 0 ); /* reduced_tx_set = 0 */
    lsmash_bits_put_align( &bits );
    lsmash_bs_put_byte( bs, 0xAA );
    lsmash_bs_put_byte( bs, 0x55 );

    uint32_t payload_len = 0;
    uint8_t *exp = lsmash_bs_export_data( bs, &payload_len );
    memcpy( payload, exp, payload_len );
    lsmash_free( exp );
    lsmash_bs_cleanup( bs );
    return build_obu( dst, AV1_OBU_TYPE_FRAME, 0, 0, 0, payload, payload_len );
}

static uint32_t build_ivf_stream_with_render_change( uint8_t *dst )
{
    synth_seq_cfg_t cfg = { 0 };
    cfg.seq_profile           = 0;
    cfg.seq_level_idx_0       = 4;
    cfg.width                 = 64;
    cfg.height                = 64;
    cfg.subsampling_x         = 1;
    cfg.subsampling_y         = 1;
    cfg.enable_order_hint     = 1;
    cfg.order_hint_bits_minus_1 = 7;

    uint32_t pos = 0;
    memcpy( dst, "DKIF", 4 );
    put_le16( dst + 4, 0 );
    put_le16( dst + 6, 32 );
    memcpy( dst + 8, "AV01", 4 );
    put_le16( dst + 12, 64 );
    put_le16( dst + 14, 64 );
    put_le32( dst + 16, 30 );
    put_le32( dst + 20, 1 );
    put_le32( dst + 24, 2 );
    put_le32( dst + 28, 0 );
    pos = 32;

    /* Frame 0: Sequence Header + Keyframe (render size 64x64 -> SAR 1:1) */
    uint8_t tu0[256] = { 0 };
    uint32_t tu0_len = 0;
    tu0_len += build_obu( tu0 + tu0_len, 2 /* OBU_TEMPORAL_DELIMITER */, 0, 0, 0, NULL, 0 );
    tu0_len += build_seq_hdr_obu( tu0 + tu0_len, &cfg );
    tu0_len += build_av1_keyframe_obu_with_render_size( tu0 + tu0_len, 0, 0, 0 );
    put_le32( dst + pos, tu0_len );
    put_le64( dst + pos + 4, 0 );
    pos += 12;
    memcpy( dst + pos, tu0, tu0_len );
    pos += tu0_len;

    /* Frame 1: Keyframe with RenderWidth=96, RenderHeight=64 (SAR increases to 3:2) */
    uint8_t tu1[256] = { 0 };
    uint32_t tu1_len = 0;
    tu1_len += build_obu( tu1 + tu1_len, 2 /* OBU_TEMPORAL_DELIMITER */, 0, 0, 0, NULL, 0 );
    tu1_len += build_seq_hdr_obu( tu1 + tu1_len, &cfg );
    tu1_len += build_av1_keyframe_obu_with_render_size( tu1 + tu1_len, 1, 96, 64 );
    put_le32( dst + pos, tu1_len );
    put_le64( dst + pos + 4, 1 );
    pos += 12;
    memcpy( dst + pos, tu1, tu1_len );
    pos += tu1_len;

    return pos;
}

static uint32_t build_adts_stream( uint8_t *dst, uint32_t num_frames )
{
    static const uint8_t aac_raw_block[] = { 0x21, 0x10, 0x04, 0x60, 0x8C, 0x1C };
    uint32_t frame_len = 7 + sizeof(aac_raw_block);
    uint32_t pos = 0;
    for( uint32_t i = 0; i < num_frames; i++ )
    {
        uint8_t hdr[7] = { 0 };
        test_bit_writer_t bw = { hdr, 0 };
        bw_put( &bw, 12, 0xFFF );       /* syncword */
        bw_put( &bw, 1, 0 );            /* ID = MPEG-4 */
        bw_put( &bw, 2, 0 );            /* layer = 0 */
        bw_put( &bw, 1, 1 );            /* protection_absent = 1 */
        bw_put( &bw, 2, 1 );            /* profile = AAC LC (AOT 2 - 1) */
        bw_put( &bw, 4, 4 );            /* sampling_frequency_index = 4 (44100 Hz) */
        bw_put( &bw, 1, 0 );            /* private_bit = 0 */
        bw_put( &bw, 3, 2 );            /* channel_configuration = 2 (stereo) */
        bw_put( &bw, 1, 0 );            /* original_copy = 0 */
        bw_put( &bw, 1, 0 );            /* home = 0 */
        bw_put( &bw, 1, 0 );            /* copyright_identification_bit = 0 */
        bw_put( &bw, 1, 0 );            /* copyright_identification_start = 0 */
        bw_put( &bw, 13, frame_len );   /* aac_frame_length */
        bw_put( &bw, 11, 0x7FF );       /* adts_buffer_fullness */
        bw_put( &bw, 2, 0 );            /* number_of_raw_data_blocks_in_frame = 0 */
        memcpy( dst + pos, hdr, 7 );
        pos += 7;
        memcpy( dst + pos, aac_raw_block, sizeof(aac_raw_block) );
        pos += sizeof(aac_raw_block);
    }
    return pos;
}

static uint32_t build_amr_nb_stream( uint8_t *dst, uint32_t num_frames )
{
    uint32_t pos = 0;
    memcpy( dst, "#!AMR\n", 6 );
    pos += 6;
    for( uint32_t i = 0; i < num_frames; i++ )
    {
        dst[pos++] = 0x3C; /* FT = 7 (12.2 kbps, 31 payload bytes), Q = 1 */
        for( uint32_t j = 0; j < 31; j++ )
            dst[pos++] = (uint8_t)((i + j * 3) & 0xFF);
    }
    return pos;
}

static uint32_t build_wave_pcm16_stream( uint8_t *dst, uint32_t num_samples )
{
    uint32_t data_bytes = num_samples * 4; /* 2ch * 16-bit */
    memcpy( dst, "RIFF", 4 );
    put_le32( dst + 4, 36 + data_bytes );
    memcpy( dst + 8, "WAVEfmt ", 8 );
    put_le32( dst + 16, 16 );       /* fmt chunk size */
    put_le16( dst + 20, 1 );        /* WAVE_FORMAT_PCM */
    put_le16( dst + 22, 2 );        /* nChannels = 2 */
    put_le32( dst + 24, 48000 );    /* nSamplesPerSec = 48000 */
    put_le32( dst + 28, 192000 );   /* nAvgBytesPerSec */
    put_le16( dst + 32, 4 );        /* nBlockAlign = 4 */
    put_le16( dst + 34, 16 );       /* wBitsPerSample = 16 */
    memcpy( dst + 36, "data", 4 );
    put_le32( dst + 40, data_bytes );
    fill_pattern( dst + 44, data_bytes, 0x23 );
    return 44 + data_bytes;
}

static char *read_text_file( const char *path, size_t *out_len )
{
    *out_len = 0;
    FILE *fp = fopen( path, "rb" );
    if( !fp )
        return NULL;
    if( fseek( fp, 0, SEEK_END ) != 0 )
    {
        fclose( fp );
        return NULL;
    }
    long sz = ftell( fp );
    if( sz < 0 || fseek( fp, 0, SEEK_SET ) != 0 )
    {
        fclose( fp );
        return NULL;
    }
    char *buf = (char *)lsmash_malloc( (size_t)sz + 1 );
    if( !buf )
    {
        fclose( fp );
        return NULL;
    }
    size_t rd = fread( buf, 1, (size_t)sz, fp );
    fclose( fp );
    buf[rd] = '\0';
    *out_len = rd;
    return buf;
}

static void report_first_diff( const char *label, const char *args, const char *a, const char *b )
{
    size_t line = 1;
    size_t col  = 1;
    size_t i    = 0;
    while( a[i] && b[i] && a[i] == b[i] )
    {
        if( a[i] == '\n' )
        {
            ++line;
            col = 1;
        }
        else
            ++col;
        ++i;
    }
    fprintf( stderr, "[DIFF] %s mismatch for args [%s] at line %zu, col %zu:\n  ref     : %.200s\n  lshmuxer: %.200s\n",
             label, args, line, col, a + i, b + i );
    const char *pa = a;
    const char *pb = b;
    size_t lno = 1;
    int printed = 0;
    while( *pa && *pb && printed < 15 )
    {
        const char *na = strchr( pa, '\n' );
        const char *nb = strchr( pb, '\n' );
        size_t la = na ? (size_t)(na - pa) : strlen( pa );
        size_t lb = nb ? (size_t)(nb - pb) : strlen( pb );
        if( la != lb || memcmp( pa, pb, la ) != 0 )
        {
            fprintf( stderr, "  L%zu ref: %.*s\n  L%zu lsh: %.*s\n", lno, (int)la, pa, lno, (int)lb, pb );
            ++printed;
        }
        pa = na ? na + 1 : pa + la;
        pb = nb ? nb + 1 : pb + lb;
        ++lno;
    }
}

static int assert_cli_equivalent( const char *args_without_output, const char *out_path )
{
    const char *mux_box_path = "test_cli_eq_mux_box.txt";
    const char *lsh_box_path = "test_cli_eq_lsh_box.txt";
    const char *mux_ts_path  = "test_cli_eq_mux_ts.txt";
    const char *lsh_ts_path  = "test_cli_eq_lsh_ts.txt";

    char cmd[1024];
    char *mux_box = NULL;
    char *lsh_box = NULL;
    char *mux_ts  = NULL;
    char *lsh_ts  = NULL;
    size_t mux_box_len = 0, lsh_box_len = 0, mux_ts_len = 0, lsh_ts_len = 0;
    int rc_mux = -1, rc_lsh = -1, rc_bd1 = -1, rc_bd2 = -1, rc_bd3 = -1, rc_bd4 = -1;

    /* Run both CLIs against the identical out_path within the same wall-clock second so that
     * even mvhd/tkhd/mdhd creation_time and modification_time and [File] headers match 100%. */
    for( int attempt = 0; attempt < 3; attempt++ )
    {
        lsmash_freep( &mux_box );
        lsmash_freep( &lsh_box );
        lsmash_freep( &mux_ts );
        lsmash_freep( &lsh_ts );

        time_t t0 = time( NULL );

        remove( out_path );
        snprintf( cmd, sizeof(cmd), "%s %s -o %s >%s 2>&1", CLI_MUXER, args_without_output, out_path, CLI_DEV_NULL );
        rc_mux = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, out_path, mux_box_path, CLI_DEV_NULL );
        rc_bd1 = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s 2>%s", CLI_BOXDUMPER, out_path, mux_ts_path, CLI_DEV_NULL );
        rc_bd2 = system( cmd );

        remove( out_path );
        snprintf( cmd, sizeof(cmd), "%s %s -o %s >%s 2>&1", CLI_LSHMUXER, args_without_output, out_path, CLI_DEV_NULL );
        rc_lsh = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, out_path, lsh_box_path, CLI_DEV_NULL );
        rc_bd3 = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s 2>%s", CLI_BOXDUMPER, out_path, lsh_ts_path, CLI_DEV_NULL );
        rc_bd4 = system( cmd );

        time_t t1 = time( NULL );

        mux_box = read_text_file( mux_box_path, &mux_box_len );
        lsh_box = read_text_file( lsh_box_path, &lsh_box_len );
        mux_ts  = read_text_file( mux_ts_path,  &mux_ts_len );
        lsh_ts  = read_text_file( lsh_ts_path,  &lsh_ts_len );

        if( t0 == t1 || (mux_box && lsh_box && mux_box_len == lsh_box_len && memcmp( mux_box, lsh_box, mux_box_len ) == 0) )
            break;
    }

    remove( out_path );
    remove( mux_box_path );
    remove( lsh_box_path );
    remove( mux_ts_path );
    remove( lsh_ts_path );

    int box_match = (mux_box && lsh_box && mux_box_len > 0 && mux_box_len == lsh_box_len
                  && memcmp( mux_box, lsh_box, mux_box_len ) == 0);
    int ts_match  = (mux_ts && lsh_ts && mux_ts_len > 0 && mux_ts_len == lsh_ts_len
                  && memcmp( mux_ts, lsh_ts, mux_ts_len ) == 0);

    if( !box_match && mux_box && lsh_box )
        report_first_diff( "boxdumper --box", args_without_output, mux_box, lsh_box );
    if( !ts_match && mux_ts && lsh_ts )
        report_first_diff( "boxdumper --timestamp", args_without_output, mux_ts, lsh_ts );

    lsmash_free( mux_box );
    lsmash_free( lsh_box );
    lsmash_free( mux_ts );
    lsmash_free( lsh_ts );

    TEST_ASSERT( rc_mux == 0, "cli/muxer succeeded" );
    TEST_ASSERT( rc_lsh == 0, "cli/lshmuxer succeeded" );
    TEST_ASSERT( rc_bd1 == 0 && rc_bd2 == 0 && rc_bd3 == 0 && rc_bd4 == 0, "cli/boxdumper succeeded" );
    TEST_ASSERT( box_match, "boxdumper --box output is 100% identical between muxer and lshmuxer" );
    TEST_ASSERT( ts_match,  "boxdumper --timestamp output is 100% identical between muxer and lshmuxer" );
    return 0;
}

static int assert_cli_both_fail( const char *args_without_output, const char *out_path )
{
    char cmd[1024];
    remove( out_path );
    snprintf( cmd, sizeof(cmd), "%s %s -o %s >%s 2>&1", CLI_MUXER, args_without_output, out_path, CLI_DEV_NULL );
    int rc_mux = system( cmd );
    remove( out_path );
    snprintf( cmd, sizeof(cmd), "%s %s -o %s >%s 2>&1", CLI_LSHMUXER, args_without_output, out_path, CLI_DEV_NULL );
    int rc_lsh = system( cmd );
    remove( out_path );
    TEST_ASSERT( rc_mux != 0, "cli/muxer rejected invalid invocation" );
    TEST_ASSERT( rc_lsh != 0, "cli/lshmuxer rejected invalid invocation" );
    return 0;
}

static int assert_remuxer_equivalent_ex( const char *rem_args, const char *lsh_args, const char *out_path, const char *seg_path )
{
    if( !lsh_args )
        lsh_args = rem_args;
    const char *rem_box_path = "test_cli_eq_rem_box.txt";
    const char *lsh_box_path = "test_cli_eq_lsh_box.txt";
    const char *rem_ts_path  = "test_cli_eq_rem_ts.txt";
    const char *lsh_ts_path  = "test_cli_eq_lsh_ts.txt";
    const char *rem_seg_path = "test_cli_eq_rem_seg.txt";
    const char *lsh_seg_path = "test_cli_eq_lsh_seg.txt";

    char cmd[1024];
    char *rem_box = NULL, *lsh_box = NULL;
    char *rem_ts  = NULL, *lsh_ts  = NULL;
    char *rem_seg = NULL, *lsh_seg = NULL;
    size_t rem_box_len = 0, lsh_box_len = 0, rem_ts_len = 0, lsh_ts_len = 0, rem_seg_len = 0, lsh_seg_len = 0;
    int rc_rem = -1, rc_lsh = -1, rc_bd1 = -1, rc_bd2 = 0, rc_bd3 = -1, rc_bd4 = 0, rc_bd5 = 0, rc_bd6 = 0;

    for( int attempt = 0; attempt < 3; attempt++ )
    {
        lsmash_freep( &rem_box );
        lsmash_freep( &lsh_box );
        lsmash_freep( &rem_ts );
        lsmash_freep( &lsh_ts );
        lsmash_freep( &rem_seg );
        lsmash_freep( &lsh_seg );

        time_t t0 = time( NULL );

        remove( out_path );
        if( seg_path )
            remove( seg_path );
        snprintf( cmd, sizeof(cmd), "%s %s -o %s >%s 2>&1", CLI_REMUXER, rem_args, out_path, CLI_DEV_NULL );
        rc_rem = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, out_path, rem_box_path, CLI_DEV_NULL );
        rc_bd1 = system( cmd );
        if( !seg_path )
        {
            snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s 2>%s", CLI_BOXDUMPER, out_path, rem_ts_path, CLI_DEV_NULL );
            rc_bd2 = system( cmd );
        }
        else
        {
            snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, seg_path, rem_seg_path, CLI_DEV_NULL );
            rc_bd5 = system( cmd );
        }

        remove( out_path );
        if( seg_path )
            remove( seg_path );
        snprintf( cmd, sizeof(cmd), "%s %s -o %s >%s 2>&1", CLI_LSHMUXER, lsh_args, out_path, CLI_DEV_NULL );
        rc_lsh = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, out_path, lsh_box_path, CLI_DEV_NULL );
        rc_bd3 = system( cmd );
        if( !seg_path )
        {
            snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s 2>%s", CLI_BOXDUMPER, out_path, lsh_ts_path, CLI_DEV_NULL );
            rc_bd4 = system( cmd );
        }
        else
        {
            snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, seg_path, lsh_seg_path, CLI_DEV_NULL );
            rc_bd6 = system( cmd );
        }

        time_t t1 = time( NULL );

        rem_box = read_text_file( rem_box_path, &rem_box_len );
        lsh_box = read_text_file( lsh_box_path, &lsh_box_len );
        if( !seg_path )
        {
            rem_ts = read_text_file( rem_ts_path, &rem_ts_len );
            lsh_ts = read_text_file( lsh_ts_path, &lsh_ts_len );
        }
        else
        {
            rem_seg = read_text_file( rem_seg_path, &rem_seg_len );
            lsh_seg = read_text_file( lsh_seg_path, &lsh_seg_len );
        }

        if( t0 == t1 || (rem_box && lsh_box && rem_box_len == lsh_box_len && memcmp( rem_box, lsh_box, rem_box_len ) == 0) )
            break;
    }

    remove( out_path );
    if( seg_path )
        remove( seg_path );
    remove( rem_box_path );
    remove( lsh_box_path );
    remove( rem_ts_path );
    remove( lsh_ts_path );
    remove( rem_seg_path );
    remove( lsh_seg_path );

    int box_match = (rem_box && lsh_box && rem_box_len > 0 && rem_box_len == lsh_box_len
                  && memcmp( rem_box, lsh_box, rem_box_len ) == 0);
    int ts_match  = seg_path ? 1 : (rem_ts && lsh_ts && rem_ts_len > 0 && rem_ts_len == lsh_ts_len
                  && memcmp( rem_ts, lsh_ts, rem_ts_len ) == 0);
    int seg_match = !seg_path ? 1 : (rem_seg && lsh_seg && rem_seg_len > 0 && rem_seg_len == lsh_seg_len
                  && memcmp( rem_seg, lsh_seg, rem_seg_len ) == 0);

    if( !box_match && rem_box && lsh_box )
        report_first_diff( "remuxer boxdumper --box", lsh_args, rem_box, lsh_box );
    if( !ts_match && rem_ts && lsh_ts )
        report_first_diff( "remuxer boxdumper --timestamp", lsh_args, rem_ts, lsh_ts );
    if( !seg_match && rem_seg && lsh_seg )
        report_first_diff( "remuxer segment boxdumper --box", lsh_args, rem_seg, lsh_seg );

    lsmash_free( rem_box );
    lsmash_free( lsh_box );
    lsmash_free( rem_ts );
    lsmash_free( lsh_ts );
    lsmash_free( rem_seg );
    lsmash_free( lsh_seg );

    TEST_ASSERT( rc_rem == 0, "cli/remuxer succeeded" );
    TEST_ASSERT( rc_lsh == 0, "cli/lshmuxer succeeded" );
    TEST_ASSERT( rc_bd1 == 0 && rc_bd2 == 0 && rc_bd3 == 0 && rc_bd4 == 0 && rc_bd5 == 0 && rc_bd6 == 0, "cli/boxdumper succeeded" );
    TEST_ASSERT( box_match, "boxdumper --box output is 100% identical between remuxer and lshmuxer" );
    TEST_ASSERT( ts_match,  "boxdumper --timestamp output is 100% identical between remuxer and lshmuxer" );
    TEST_ASSERT( seg_match, "segment boxdumper --box output is 100% identical between remuxer and lshmuxer" );
    return 0;
}

static int assert_remuxer_equivalent( const char *args_without_output, const char *out_path, const char *seg_path )
{
    return assert_remuxer_equivalent_ex( args_without_output, NULL, out_path, seg_path );
}

static int assert_remuxer_both_fail( const char *args_without_output, const char *out_path )
{
    char cmd[1024];
    remove( out_path );
    snprintf( cmd, sizeof(cmd), "%s %s -o %s >%s 2>&1", CLI_REMUXER, args_without_output, out_path, CLI_DEV_NULL );
    int rc_rem = system( cmd );
    remove( out_path );
    snprintf( cmd, sizeof(cmd), "%s %s -o %s >%s 2>&1", CLI_LSHMUXER, args_without_output, out_path, CLI_DEV_NULL );
    int rc_lsh = system( cmd );
    remove( out_path );
    TEST_ASSERT( rc_rem != 0, "cli/remuxer rejected invalid invocation" );
    TEST_ASSERT( rc_lsh != 0, "cli/lshmuxer rejected invalid invocation" );
    return 0;
}

static int assert_timelineeditor_3way_equivalent
(
    const char *pos_opts,
    const char *in_path,
    const char *track_opts,
    const char *out_path
)
{
    const char *te_mp4_path   = "test_cli_eq_te_out.mp4";
    const char *lshp_mp4_path = "test_cli_eq_lshp_out.mp4";
    const char *te_box_path   = "test_cli_eq_te_box.txt";
    const char *lshp_box_path = "test_cli_eq_lshp_box.txt";
    const char *lsht_box_path = "test_cli_eq_lsht_box.txt";
    const char *te_ts_path    = "test_cli_eq_te_ts.txt";
    const char *lshp_ts_path  = "test_cli_eq_lshp_ts.txt";
    const char *lsht_ts_path  = "test_cli_eq_lsht_ts.txt";

    char cmd[1024];
    char *te_box = NULL, *lshp_box = NULL, *lsht_box = NULL;
    char *te_ts  = NULL, *lshp_ts  = NULL, *lsht_ts  = NULL;
    size_t te_box_len = 0, lshp_box_len = 0, lsht_box_len = 0;
    size_t te_ts_len  = 0, lshp_ts_len  = 0, lsht_ts_len  = 0;
    int rc_te = -1, rc_lshp = -1, rc_lsht = 0;
    int rc_bd1 = -1, rc_bd2 = -1, rc_bd3 = -1, rc_bd4 = -1, rc_bd5 = 0, rc_bd6 = 0;

    for( int attempt = 0; attempt < 5; attempt++ )
    {
        lsmash_freep( &te_box );
        lsmash_freep( &lshp_box );
        lsmash_freep( &lsht_box );
        lsmash_freep( &te_ts );
        lsmash_freep( &lshp_ts );
        lsmash_freep( &lsht_ts );

        remove( te_mp4_path );
        remove( lshp_mp4_path );
        remove( out_path );

        time_t t0 = time( NULL );

        /* 1. cli/timelineeditor [options] input output */
        snprintf( cmd, sizeof(cmd), "%s %s %s %s >%s 2>&1",
                  CLI_TIMELINEEDITOR, pos_opts ? pos_opts : "", in_path, te_mp4_path, CLI_DEV_NULL );
        rc_te = system( cmd );

        /* 2. cli/lshmuxer [options] input output (positional mode) */
        snprintf( cmd, sizeof(cmd), "%s %s %s %s >%s 2>&1",
                  CLI_LSHMUXER, pos_opts ? pos_opts : "", in_path, lshp_mp4_path, CLI_DEV_NULL );
        rc_lshp = system( cmd );

        /* 3. cli/lshmuxer -i input?track_opts -o output (track-option mode) */
        if( track_opts )
        {
            snprintf( cmd, sizeof(cmd), "%s -i %s?%s -o %s >%s 2>&1",
                      CLI_LSHMUXER, in_path, track_opts, out_path, CLI_DEV_NULL );
            rc_lsht = system( cmd );
        }

        time_t t1 = time( NULL );
        if( t0 != t1 && attempt < 4 )
            continue;

        snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, te_mp4_path, te_box_path, CLI_DEV_NULL );
        rc_bd1 = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s 2>%s", CLI_BOXDUMPER, te_mp4_path, te_ts_path, CLI_DEV_NULL );
        rc_bd2 = system( cmd );

        snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, lshp_mp4_path, lshp_box_path, CLI_DEV_NULL );
        rc_bd3 = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s 2>%s", CLI_BOXDUMPER, lshp_mp4_path, lshp_ts_path, CLI_DEV_NULL );
        rc_bd4 = system( cmd );

        if( track_opts )
        {
            snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, out_path, lsht_box_path, CLI_DEV_NULL );
            rc_bd5 = system( cmd );
            snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s 2>%s", CLI_BOXDUMPER, out_path, lsht_ts_path, CLI_DEV_NULL );
            rc_bd6 = system( cmd );
        }

        te_box   = read_text_file( te_box_path,   &te_box_len );
        lshp_box = read_text_file( lshp_box_path, &lshp_box_len );
        te_ts    = read_text_file( te_ts_path,    &te_ts_len );
        lshp_ts  = read_text_file( lshp_ts_path,  &lshp_ts_len );
        if( track_opts )
        {
            lsht_box = read_text_file( lsht_box_path, &lsht_box_len );
            lsht_ts  = read_text_file( lsht_ts_path,  &lsht_ts_len );
        }

        if( t0 == t1
         || (te_box && lshp_box && te_box_len == lshp_box_len && memcmp( te_box, lshp_box, te_box_len ) == 0
          && (!track_opts || (lsht_box && te_box_len == lsht_box_len && memcmp( te_box, lsht_box, te_box_len ) == 0))) )
            break;
    }

    remove( te_mp4_path );
    remove( lshp_mp4_path );
    remove( out_path );
    remove( te_box_path );
    remove( lshp_box_path );
    remove( lsht_box_path );
    remove( te_ts_path );
    remove( lshp_ts_path );
    remove( lsht_ts_path );

    int box_match_pos = (te_box && lshp_box && te_box_len > 0 && te_box_len == lshp_box_len
                      && memcmp( te_box, lshp_box, te_box_len ) == 0);
    int ts_match_pos  = (te_ts && lshp_ts && te_ts_len > 0 && te_ts_len == lshp_ts_len
                      && memcmp( te_ts, lshp_ts, te_ts_len ) == 0);
    int box_match_trk = !track_opts ? 1 : (te_box && lsht_box && te_box_len > 0 && te_box_len == lsht_box_len
                      && memcmp( te_box, lsht_box, te_box_len ) == 0);
    int ts_match_trk  = !track_opts ? 1 : (te_ts && lsht_ts && te_ts_len > 0 && te_ts_len == lsht_ts_len
                      && memcmp( te_ts, lsht_ts, te_ts_len ) == 0);

    if( !box_match_pos && te_box && lshp_box )
        report_first_diff( "timelineeditor vs lshmuxer(positional) --box", pos_opts ? pos_opts : "", te_box, lshp_box );
    if( !ts_match_pos && te_ts && lshp_ts )
        report_first_diff( "timelineeditor vs lshmuxer(positional) --timestamp", pos_opts ? pos_opts : "", te_ts, lshp_ts );
    if( !box_match_trk && te_box && lsht_box )
        report_first_diff( "timelineeditor vs lshmuxer(?track_opts) --box", track_opts ? track_opts : "", te_box, lsht_box );
    if( !ts_match_trk && te_ts && lsht_ts )
        report_first_diff( "timelineeditor vs lshmuxer(?track_opts) --timestamp", track_opts ? track_opts : "", te_ts, lsht_ts );

    lsmash_free( te_box );
    lsmash_free( lshp_box );
    lsmash_free( lsht_box );
    lsmash_free( te_ts );
    lsmash_free( lshp_ts );
    lsmash_free( lsht_ts );

    TEST_ASSERT( rc_te == 0, "cli/timelineeditor succeeded" );
    TEST_ASSERT( rc_lshp == 0, "cli/lshmuxer (positional) succeeded" );
    TEST_ASSERT( rc_lsht == 0, "cli/lshmuxer (track-option) succeeded" );
    TEST_ASSERT( rc_bd1 == 0 && rc_bd2 == 0 && rc_bd3 == 0 && rc_bd4 == 0 && rc_bd5 == 0 && rc_bd6 == 0, "cli/boxdumper succeeded" );
    TEST_ASSERT( box_match_pos, "boxdumper --box is 100% identical between timelineeditor and lshmuxer (positional)" );
    TEST_ASSERT( ts_match_pos,  "boxdumper --timestamp is 100% identical between timelineeditor and lshmuxer (positional)" );
    TEST_ASSERT( box_match_trk, "boxdumper --box is 100% identical between timelineeditor and lshmuxer (-i input?opt)" );
    TEST_ASSERT( ts_match_trk,  "boxdumper --timestamp is 100% identical between timelineeditor and lshmuxer (-i input?opt)" );
    return 0;
}

static int assert_timelineeditor_both_fail( const char *pos_opts, const char *in_path, const char *out_path )
{
    char cmd[1024];
    remove( out_path );
    snprintf( cmd, sizeof(cmd), "%s %s %s %s >%s 2>&1", CLI_TIMELINEEDITOR, pos_opts, in_path, out_path, CLI_DEV_NULL );
    int rc_te = system( cmd );
    remove( out_path );
    snprintf( cmd, sizeof(cmd), "%s %s %s %s >%s 2>&1", CLI_LSHMUXER, pos_opts, in_path, out_path, CLI_DEV_NULL );
    int rc_lsh = system( cmd );
    remove( out_path );
    TEST_ASSERT( rc_te != 0, "cli/timelineeditor rejected invalid invocation" );
    TEST_ASSERT( rc_lsh != 0, "cli/lshmuxer rejected invalid invocation" );
    return 0;
}

static int assert_pipeline_vs_1pass_equivalent
(
    const char *raw_v,
    const char *raw_a,
    const char *te_opts,
    const char *remux_opts,
    const char *lsh_1pass_args,
    const char *out_path
)
{
    const char *s1_mp4       = "test_cli_eq_pipe_s1.mp4";
    const char *s2_mp4       = "test_cli_eq_pipe_s2.mp4";
    const char *pipe_mp4     = "test_cli_eq_pipe_out.mp4";
    const char *pipe_box_p   = "test_cli_eq_pipe_box.txt";
    const char *lsh_box_p    = "test_cli_eq_1pass_box.txt";
    const char *pipe_ts_p    = "test_cli_eq_pipe_ts.txt";
    const char *lsh_ts_p     = "test_cli_eq_1pass_ts.txt";

    char cmd[1024];
    char *pipe_box = NULL, *lsh_box = NULL;
    char *pipe_ts  = NULL, *lsh_ts  = NULL;
    size_t pipe_box_len = 0, lsh_box_len = 0, pipe_ts_len = 0, lsh_ts_len = 0;
    int rc_m1 = -1, rc_te = -1, rc_rem = 0, rc_lsh = -1;
    int rc_bd1 = -1, rc_bd2 = -1, rc_bd3 = -1, rc_bd4 = -1;

    for( int attempt = 0; attempt < 5; attempt++ )
    {
        lsmash_freep( &pipe_box );
        lsmash_freep( &lsh_box );
        lsmash_freep( &pipe_ts );
        lsmash_freep( &lsh_ts );

        remove( s1_mp4 );
        remove( s2_mp4 );
        remove( pipe_mp4 );
        remove( out_path );

        time_t t0 = time( NULL );

        snprintf( cmd, sizeof(cmd), "%s -i %s -i %s -o %s >%s 2>&1", CLI_MUXER, raw_v, raw_a, s1_mp4, CLI_DEV_NULL );
        rc_m1 = system( cmd );
        if( remux_opts )
        {
            snprintf( cmd, sizeof(cmd), "%s --track 1 %s %s %s >%s 2>&1", CLI_TIMELINEEDITOR, te_opts, s1_mp4, s2_mp4, CLI_DEV_NULL );
            rc_te = system( cmd );
            snprintf( cmd, sizeof(cmd), "%s %s -i %s -o %s >%s 2>&1",
                      CLI_REMUXER, remux_opts, s2_mp4, pipe_mp4, CLI_DEV_NULL );
            rc_rem = system( cmd );
        }
        else
        {
            snprintf( cmd, sizeof(cmd), "%s --track 1 %s %s %s >%s 2>&1", CLI_TIMELINEEDITOR, te_opts, s1_mp4, pipe_mp4, CLI_DEV_NULL );
            rc_te  = system( cmd );
            rc_rem = 0;
        }

        snprintf( cmd, sizeof(cmd), "%s %s -o %s >%s 2>&1", CLI_LSHMUXER, lsh_1pass_args, out_path, CLI_DEV_NULL );
        rc_lsh = system( cmd );

        time_t t1 = time( NULL );
        if( t0 != t1 && attempt < 4 )
            continue;

        snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, pipe_mp4, pipe_box_p, CLI_DEV_NULL );
        rc_bd1 = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s 2>%s", CLI_BOXDUMPER, pipe_mp4, pipe_ts_p, CLI_DEV_NULL );
        rc_bd2 = system( cmd );

        snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, out_path, lsh_box_p, CLI_DEV_NULL );
        rc_bd3 = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s 2>%s", CLI_BOXDUMPER, out_path, lsh_ts_p, CLI_DEV_NULL );
        rc_bd4 = system( cmd );

        pipe_box = read_text_file( pipe_box_p, &pipe_box_len );
        lsh_box  = read_text_file( lsh_box_p,  &lsh_box_len );
        pipe_ts  = read_text_file( pipe_ts_p,  &pipe_ts_len );
        lsh_ts   = read_text_file( lsh_ts_p,   &lsh_ts_len );

        if( t0 == t1 || (pipe_box && lsh_box && pipe_box_len == lsh_box_len && memcmp( pipe_box, lsh_box, pipe_box_len ) == 0) )
            break;
    }

    remove( s1_mp4 );
    remove( s2_mp4 );
    remove( pipe_mp4 );
    remove( out_path );
    remove( pipe_box_p );
    remove( lsh_box_p );
    remove( pipe_ts_p );
    remove( lsh_ts_p );

    int box_match = (pipe_box && lsh_box && pipe_box_len > 0 && pipe_box_len == lsh_box_len
                  && memcmp( pipe_box, lsh_box, pipe_box_len ) == 0);
    int ts_match  = (pipe_ts && lsh_ts && pipe_ts_len > 0 && pipe_ts_len == lsh_ts_len
                  && memcmp( pipe_ts, lsh_ts, pipe_ts_len ) == 0);

    if( !box_match && pipe_box && lsh_box )
        report_first_diff( "multi-stage pipeline vs 1-pass lshmuxer --box", lsh_1pass_args, pipe_box, lsh_box );
    if( !ts_match && pipe_ts && lsh_ts )
        report_first_diff( "multi-stage pipeline vs 1-pass lshmuxer --timestamp", lsh_1pass_args, pipe_ts, lsh_ts );

    lsmash_free( pipe_box );
    lsmash_free( lsh_box );
    lsmash_free( pipe_ts );
    lsmash_free( lsh_ts );

    TEST_ASSERT( rc_m1 == 0 && rc_te == 0 && rc_rem == 0, "multi-stage legacy pipeline succeeded" );
    TEST_ASSERT( rc_lsh == 0, "1-pass cli/lshmuxer succeeded" );
    TEST_ASSERT( rc_bd1 == 0 && rc_bd2 == 0 && rc_bd3 == 0 && rc_bd4 == 0, "cli/boxdumper succeeded" );
    TEST_ASSERT( box_match, "boxdumper --box is 100% identical between multi-stage pipeline and 1-pass lshmuxer" );
    TEST_ASSERT( ts_match,  "boxdumper --timestamp is 100% identical between multi-stage pipeline and 1-pass lshmuxer" );
    return 0;
}

typedef struct
{
    uint32_t mdhd_timescale;
    uint64_t mdhd_duration;
    uint64_t tkhd_duration;
    uint64_t stts_sum;
    uint32_t stts_last_delta;
    uint32_t d_comp;
    uint64_t min_cts;
    uint64_t max_cts;
    uint64_t last_dts;
    uint32_t elst_count;
    uint64_t elst_dur[2];
    int64_t  elst_mt[2];
    int      has_cslg;
    uint8_t  ctts_version;
    int32_t  cslg_ctd_shift;
    int32_t  cslg_least_delta;
    int32_t  cslg_greatest_delta;
    int32_t  cslg_start_time;
    int32_t  cslg_end_time;
    uint64_t sample_dts[8];
    uint64_t sample_cts[8];
} track_timeline_metrics_t;

static int inspect_track_timeline_metrics( const char *path, uint32_t track_number, track_timeline_metrics_t *m )
{
    memset( m, 0, sizeof(*m) );
    lsmash_root_t *root = lsmash_create_root();
    TEST_ASSERT( root != NULL, "create root for metrics inspection" );
    lsmash_file_parameters_t fp = { 0 };
    TEST_ASSERT( lsmash_open_file( path, 1, &fp ) == 0, "open file for metrics inspection" );
    lsmash_file_t *file = lsmash_set_file( root, &fp );
    TEST_ASSERT( file != NULL, "set file for metrics inspection" );
    TEST_ASSERT( lsmash_read_file( file, &fp ) >= 0, "read file for metrics inspection" );

    uint32_t track_ID = lsmash_get_track_ID( root, track_number );
    TEST_ASSERT( track_ID != 0, "get track_ID for metrics inspection" );

    isom_trak_t *trak = NULL;
    isom_trak_t *cur_trak = NULL;
    LSM_ENTRY_LIST_FOR_EACH( cur_trak, &file->moov->trak_list )
    {
        if( cur_trak->tkhd && cur_trak->tkhd->track_ID == track_ID )
        {
            trak = cur_trak;
            break;
        }
    }
    TEST_ASSERT( trak && trak->tkhd && trak->mdia && trak->mdia->mdhd && trak->mdia->minf && trak->mdia->minf->stbl && trak->mdia->minf->stbl->stts,
                 "trak/mdhd/stts boxes exist" );

    m->mdhd_timescale = trak->mdia->mdhd->timescale;
    m->mdhd_duration  = trak->mdia->mdhd->duration;
    m->tkhd_duration  = trak->tkhd->duration;
    if( LSMASH_IS_EXISTING_BOX( trak->mdia->minf->stbl->ctts ) )
        m->ctts_version = trak->mdia->minf->stbl->ctts->version;
    if( LSMASH_IS_EXISTING_BOX( trak->mdia->minf->stbl->cslg ) )
    {
        isom_cslg_t *cslg = trak->mdia->minf->stbl->cslg;
        m->has_cslg            = 1;
        m->cslg_ctd_shift      = cslg->compositionToDTSShift;
        m->cslg_least_delta    = cslg->leastDecodeToDisplayDelta;
        m->cslg_greatest_delta = cslg->greatestDecodeToDisplayDelta;
        m->cslg_start_time     = cslg->compositionStartTime;
        m->cslg_end_time       = cslg->compositionEndTime;
    }

    isom_stts_entry_t *data = NULL;
    LSM_LIST_FOR_EACH( data, trak->mdia->minf->stbl->stts->list )
    {
        m->stts_sum += (uint64_t)data->sample_count * data->sample_delta;
        m->stts_last_delta = data->sample_delta;
    }

    TEST_ASSERT( lsmash_construct_timeline( root, track_ID ) == 0, "construct timeline for metrics inspection" );
    uint32_t sample_count = lsmash_get_sample_count_in_media_timeline( root, track_ID );
    TEST_ASSERT( sample_count > 0, "timeline sample_count > 0" );
    uint32_t ctd_shift = 0;
    TEST_ASSERT( lsmash_get_composition_to_decode_shift_from_media_timeline( root, track_ID, &ctd_shift ) == 0,
                 "get ctd_shift from media timeline" );

    m->min_cts = UINT64_MAX;
    m->max_cts = 0;
    for( uint32_t i = 1; i <= sample_count; i++ )
    {
        uint64_t dts = 0, cts = 0;
        TEST_ASSERT( lsmash_get_dts_from_media_timeline( root, track_ID, i, &dts ) == 0, "get sample dts" );
        TEST_ASSERT( lsmash_get_cts_from_media_timeline( root, track_ID, i, &cts ) == 0, "get sample cts" );
        TEST_ASSERT( dts <= cts + ctd_shift, "DTS <= CTS + ctd_shift invariant holds for every sample" );
        if( i <= 8 )
        {
            m->sample_dts[i - 1] = dts;
            m->sample_cts[i - 1] = cts;
        }
        if( cts < m->min_cts )
            m->min_cts = cts;
        if( cts > m->max_cts )
            m->max_cts = cts;
        if( i == sample_count )
            m->last_dts = dts;
    }

    TEST_ASSERT( lsmash_get_last_sample_delta_from_media_timeline( root, track_ID, &m->d_comp ) == 0,
                 "get last_sample_delta (D_comp) from media timeline" );

    TEST_ASSERT( m->stts_sum == m->mdhd_duration, "sum(stts) == mdhd->duration invariant holds" );
    TEST_ASSERT( m->last_dts + m->stts_last_delta == m->mdhd_duration, "last_dts + stts_last_delta == mdhd->duration invariant holds" );
    TEST_ASSERT( (m->max_cts - m->min_cts) + m->d_comp == m->mdhd_duration, "(max_cts - min_cts) + D_comp == mdhd->duration invariant holds" );
    if( m->has_cslg )
    {
        TEST_ASSERT( m->cslg_ctd_shift == (int32_t)ctd_shift, "cslg->compositionToDTSShift matches timeline ctd_shift" );
        TEST_ASSERT( (uint64_t)((int64_t)m->cslg_end_time - (int64_t)m->cslg_start_time) == m->mdhd_duration,
                     "cslg composition span (compositionEndTime - compositionStartTime) == mdhd->duration" );
    }

    m->elst_count = lsmash_count_explicit_timeline_map( root, track_ID );
    for( uint32_t i = 1; i <= m->elst_count && i <= 2; i++ )
    {
        lsmash_edit_t edit = { 0 };
        TEST_ASSERT( lsmash_get_explicit_timeline_map( root, track_ID, i, &edit ) == 0, "get explicit timeline map" );
        m->elst_dur[i - 1] = edit.duration;
        m->elst_mt[i - 1]  = edit.start_time;
    }

    lsmash_destruct_timeline( root, track_ID );
    lsmash_close_file( &fp );
    lsmash_destroy_root( root );
    return 0;
}

static int assert_remux_idempotency_and_invariants( const char *raw_v, const char *raw_a, const char *tcv2_path )
{
    const char *gen0 = "test_cli_eq_idem_gen0.mp4";
    const char *gen1 = "test_cli_eq_idem_gen1.mp4";
    const char *gen2 = "test_cli_eq_idem_gen2.mp4";
    const char *gen3 = "test_cli_eq_idem_gen3.mp4";
    char cmd[1024];

    remove( gen0 );
    remove( gen1 );
    remove( gen2 );
    remove( gen3 );

    snprintf( cmd, sizeof(cmd),
              "%s -i %s?timecode=%s,dts-compression,skip=25/1000,delay=50/1000 -i %s -o %s >%s 2>&1",
              CLI_LSHMUXER, raw_v, tcv2_path, raw_a, gen0, CLI_DEV_NULL );
    int rc0 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s -i %s -o %s >%s 2>&1", CLI_REMUXER, gen0, gen1, CLI_DEV_NULL );
    int rc1 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s -i %s -o %s >%s 2>&1", CLI_REMUXER, gen1, gen2, CLI_DEV_NULL );
    int rc2 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s -i %s -o %s >%s 2>&1", CLI_LSHMUXER, gen2, gen3, CLI_DEV_NULL );
    int rc3 = system( cmd );

    track_timeline_metrics_t m0, m1, m2, m3;
    int ok = (rc0 == 0 && rc1 == 0 && rc2 == 0 && rc3 == 0)
          && inspect_track_timeline_metrics( gen0, 1, &m0 ) == 0
          && inspect_track_timeline_metrics( gen1, 1, &m1 ) == 0
          && inspect_track_timeline_metrics( gen2, 1, &m2 ) == 0
          && inspect_track_timeline_metrics( gen3, 1, &m3 ) == 0;

    remove( gen0 );
    remove( gen1 );
    remove( gen2 );
    remove( gen3 );

    TEST_ASSERT( ok, "all 4 remux generations succeeded and satisfied DTS <= CTS and sum(stts) == mdhd->duration" );
    TEST_ASSERT( m0.stts_last_delta > m0.d_comp, "B-frame + dts-compression track has stretched stts tail delta (> D_comp)" );
    TEST_ASSERT( m0.mdhd_duration == m1.mdhd_duration && m1.mdhd_duration == m2.mdhd_duration && m2.mdhd_duration == m3.mdhd_duration,
                 "mdhd->duration is 100% idempotent across repeated remuxer/lshmuxer passes (0 ticks inflation)" );
    TEST_ASSERT( m0.tkhd_duration == m1.tkhd_duration && m1.tkhd_duration == m2.tkhd_duration && m2.tkhd_duration == m3.tkhd_duration,
                 "tkhd->duration is 100% idempotent across repeated remuxer/lshmuxer passes" );
    TEST_ASSERT( m0.stts_last_delta == m1.stts_last_delta && m1.stts_last_delta == m2.stts_last_delta && m2.stts_last_delta == m3.stts_last_delta,
                 "stts last sample_delta is 100% idempotent across repeated remuxer/lshmuxer passes" );
    TEST_ASSERT( m0.d_comp == m1.d_comp && m1.d_comp == m2.d_comp && m2.d_comp == m3.d_comp,
                 "recovered D_comp (last_sample_delta) is 100% idempotent across repeated remuxer/lshmuxer passes" );
    TEST_ASSERT( m0.elst_count == m1.elst_count && m1.elst_count == m2.elst_count && m2.elst_count == m3.elst_count
              && memcmp( m0.elst_dur, m3.elst_dur, sizeof(m0.elst_dur) ) == 0
              && memcmp( m0.elst_mt,  m3.elst_mt,  sizeof(m0.elst_mt) ) == 0,
                 "elst entries are 100% idempotent across repeated remuxer/lshmuxer passes" );
    return 0;
}

static int assert_shift_timeline_vfr_remux_idempotency( const char *raw_v, const char *tcv2_path )
{
    const char *gen0 = "test_cli_eq_shift_gen0.mp4";
    const char *gen1 = "test_cli_eq_shift_gen1.mp4";
    const char *gen2 = "test_cli_eq_shift_gen2.mp4";
    const char *gen3 = "test_cli_eq_shift_gen3.mp4";
    const char *gen4 = "test_cli_eq_shift_gen4.mp4";
    char cmd[1024];

    remove( gen0 );
    remove( gen1 );
    remove( gen2 );
    remove( gen3 );
    remove( gen4 );

    /* Gen 0: B-frame VFR container with --isom-version 6 --shift-timeline (ctts v1 negative offsets + cslg) */
    snprintf( cmd, sizeof(cmd),
              "%s --isom-version 6 --shift-timeline -i %s?timecode=%s -o %s >%s 2>&1",
              CLI_LSHMUXER, raw_v, tcv2_path, gen0, CLI_DEV_NULL );
    int rc0 = system( cmd );
    /* Gen 1: remuxer pass */
    snprintf( cmd, sizeof(cmd), "%s -i %s -o %s >%s 2>&1", CLI_REMUXER, gen0, gen1, CLI_DEV_NULL );
    int rc1 = system( cmd );
    /* Gen 2: timelineeditor re-edit pass (no-option timescale reduction / re-edit) */
    snprintf( cmd, sizeof(cmd), "%s %s %s >%s 2>&1", CLI_TIMELINEEDITOR, gen1, gen2, CLI_DEV_NULL );
    int rc2 = system( cmd );
    /* Gen 3: lshmuxer remux pass */
    snprintf( cmd, sizeof(cmd), "%s -i %s -o %s >%s 2>&1", CLI_LSHMUXER, gen2, gen3, CLI_DEV_NULL );
    int rc3 = system( cmd );
    /* Gen 4: lshmuxer positional re-edit pass */
    snprintf( cmd, sizeof(cmd), "%s %s %s >%s 2>&1", CLI_LSHMUXER, gen3, gen4, CLI_DEV_NULL );
    int rc4 = system( cmd );

    track_timeline_metrics_t m0, m1, m2, m3, m4;
    int ok = (rc0 == 0 && rc1 == 0 && rc2 == 0 && rc3 == 0 && rc4 == 0)
          && inspect_track_timeline_metrics( gen0, 1, &m0 ) == 0
          && inspect_track_timeline_metrics( gen1, 1, &m1 ) == 0
          && inspect_track_timeline_metrics( gen2, 1, &m2 ) == 0
          && inspect_track_timeline_metrics( gen3, 1, &m3 ) == 0
          && inspect_track_timeline_metrics( gen4, 1, &m4 ) == 0;

    remove( gen0 );
    remove( gen1 );
    remove( gen2 );
    remove( gen3 );
    remove( gen4 );

    TEST_ASSERT( ok, "all 5 --shift-timeline VFR generations succeeded across lshmuxer, remuxer, and timelineeditor" );
    TEST_ASSERT( m0.has_cslg && m0.ctts_version == 1 && m0.cslg_ctd_shift > 0 && m0.cslg_least_delta < 0,
                 "Gen 0 has ctts v1 negative offset and cslg with positive compositionToDTSShift" );
    const track_timeline_metrics_t *gens[4] = { &m1, &m2, &m3, &m4 };
    for( int g = 0; g < 4; g++ )
    {
        TEST_ASSERT( gens[g]->mdhd_duration  == m0.mdhd_duration
                  && gens[g]->tkhd_duration  == m0.tkhd_duration
                  && gens[g]->stts_last_delta == m0.stts_last_delta
                  && gens[g]->d_comp         == m0.d_comp,
                     "--shift-timeline VFR mdhd->duration, tkhd->duration, stts final delta, and D_comp match with 0 ticks drift" );
        TEST_ASSERT( gens[g]->has_cslg            == 1
                  && gens[g]->ctts_version        == 1
                  && gens[g]->cslg_ctd_shift      == m0.cslg_ctd_shift
                  && gens[g]->cslg_least_delta    == m0.cslg_least_delta
                  && gens[g]->cslg_greatest_delta == m0.cslg_greatest_delta
                  && gens[g]->cslg_start_time     == m0.cslg_start_time
                  && gens[g]->cslg_end_time       == m0.cslg_end_time,
                     "--shift-timeline VFR ctts v1 and all 5 cslg fields match with 0 ticks drift" );
    }
    return 0;
}

static int create_synth_negative_poc_mp4( const char *path, int shifted )
{
    remove( path );
    lsmash_root_t *root = lsmash_create_root();
    TEST_ASSERT( root != NULL, "neg_poc: create root" );
    lsmash_file_parameters_t fp = { 0 };
    TEST_ASSERT( lsmash_open_file( path, 0, &fp ) == 0, "neg_poc: open file" );
    fp.major_brand   = ISOM_BRAND_TYPE_MP42;
    fp.minor_version = 0;
    lsmash_brand_type brands[4] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_ISO6, ISOM_BRAND_TYPE_AV01, ISOM_BRAND_TYPE_MP42 };
    fp.brands        = brands;
    fp.brand_count   = 4;
    TEST_ASSERT( lsmash_set_file( root, &fp ) != NULL, "neg_poc: set file" );

    lsmash_movie_parameters_t mp;
    lsmash_initialize_movie_parameters( &mp );
    mp.timescale = 1000;
    TEST_ASSERT( lsmash_set_movie_parameters( root, &mp ) == 0, "neg_poc: set movie params" );

    uint32_t tid = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
    TEST_ASSERT( tid == 1, "neg_poc: create track" );
    lsmash_track_parameters_t tp;
    lsmash_initialize_track_parameters( &tp );
    tp.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
    tp.display_width  = 16 << 16;
    tp.display_height = 16 << 16;
    TEST_ASSERT( lsmash_set_track_parameters( root, tid, &tp ) == 0, "neg_poc: set track params" );

    lsmash_media_parameters_t mdp;
    lsmash_initialize_media_parameters( &mdp );
    mdp.timescale = 1000;
    TEST_ASSERT( lsmash_set_media_parameters( root, tid, &mdp ) == 0, "neg_poc: set media params" );

    lsmash_video_summary_t *summary = create_synth_av01_summary( 16, 16 );
    TEST_ASSERT( summary != NULL, "neg_poc: create synth summary" );
    uint32_t s_idx = lsmash_add_sample_entry( root, tid, summary );
    lsmash_cleanup_summary( (lsmash_summary_t *)summary );
    TEST_ASSERT( s_idx == 1, "neg_poc: add sample entry" );

    /* 4 samples where decoding-order sample 0 (IDR, POC = 0, display index c(0) = 1) has a
     * larger CTS than decoding-order sample 1 (negative POC picture, display index c(1) = 0):
     * - Shifted   (ctts v1 + cslg, ctd_shift = 40): DTS = [0, 40, 80, 120], CTS = [40, 0, 120, 80]
     * - Unshifted (ctts v0,        ctd_shift = 0):  DTS = [0, 40, 80, 120], CTS = [80, 40, 160, 120] */
    const uint64_t dts_arr[4] = { 0, 40, 80, 120 };
    const uint64_t cts_arr[4] = {
        shifted ?  40 :  80,
        shifted ?   0 :  40,
        shifted ? 120 : 160,
        shifted ?  80 : 120
    };
    for( int i = 0; i < 4; i++ )
    {
        lsmash_sample_t *sample = lsmash_create_sample( 4 );
        memset( sample->data, 0, 4 );
        sample->dts   = dts_arr[i];
        sample->cts   = cts_arr[i];
        sample->index = s_idx;
        sample->prop.ra_flags = (i == 0) ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC : ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
        TEST_ASSERT( lsmash_append_sample( root, tid, sample ) == 0, "neg_poc: append sample" );
    }
    TEST_ASSERT( lsmash_flush_pooled_samples( root, tid, 40 ) == 0, "neg_poc: flush pooled samples" );
    TEST_ASSERT( lsmash_update_track_duration( root, tid, 0 ) == 0, "neg_poc: update track duration" );

    lsmash_edit_t edit;
    edit.duration   = 160;
    edit.start_time = shifted ? 0 : 40;
    edit.rate       = ISOM_EDIT_MODE_NORMAL;
    TEST_ASSERT( lsmash_create_explicit_timeline_map( root, tid, edit ) == 0, "neg_poc: create elst" );
    TEST_ASSERT( lsmash_finish_movie( root, NULL ) == 0, "neg_poc: finish movie" );
    lsmash_close_file( &fp );
    lsmash_destroy_root( root );
    return 0;
}

static int assert_negative_poc_shift_timeline_vfr_reedit( const char *tcv2_path )
{
    const char *f_base_shifted   = "test_cli_eq_negpoc_shifted.mp4";
    const char *f_base_unshifted = "test_cli_eq_negpoc_unshifted.mp4";
    const char *f_te_bare        = "test_cli_eq_negpoc_te_bare.mp4";
    const char *f_lsh_pos        = "test_cli_eq_negpoc_lsh_pos.mp4";
    const char *f_lsh_remux      = "test_cli_eq_negpoc_lsh_remux.mp4";
    const char *f_lsh_shift      = "test_cli_eq_negpoc_lsh_shift.mp4";
    const char *f_remuxer        = "test_cli_eq_negpoc_remuxer.mp4";
    const char *f_unsh_to_shift  = "test_cli_eq_negpoc_unsh_to_shift.mp4";
    const char *f_vfr_te         = "test_cli_eq_negpoc_vfr_te.mp4";
    const char *f_vfr_lsh_pos    = "test_cli_eq_negpoc_vfr_lsh_pos.mp4";
    const char *f_vfr_lsh_opt    = "test_cli_eq_negpoc_vfr_lsh_opt.mp4";
    const char *f_vfr_lsh_shift  = "test_cli_eq_negpoc_vfr_lsh_shift.mp4";
    const char *f_vfr_unsh_shift = "test_cli_eq_negpoc_vfr_unsh_shift.mp4";
    const char *f_vfr_te_reedit  = "test_cli_eq_negpoc_vfr_te_reedit.mp4";
    const char *f_vfr_lsh_from   = "test_cli_eq_negpoc_vfr_lsh_from.mp4";
    const char *f_vfr_remux_from = "test_cli_eq_negpoc_vfr_remux_from.mp4";
    const char *all_files[] = {
        f_base_shifted, f_base_unshifted,
        f_te_bare, f_lsh_pos, f_lsh_remux, f_lsh_shift, f_remuxer, f_unsh_to_shift,
        f_vfr_te, f_vfr_lsh_pos, f_vfr_lsh_opt, f_vfr_lsh_shift, f_vfr_unsh_shift,
        f_vfr_te_reedit, f_vfr_lsh_from, f_vfr_remux_from
    };
    char cmd[1024];
    int rc = -1;

    for( size_t i = 0; i < sizeof(all_files) / sizeof(all_files[0]); i++ )
        remove( all_files[i] );

    if( create_synth_negative_poc_mp4( f_base_shifted, 1 ) < 0
     || create_synth_negative_poc_mp4( f_base_unshifted, 0 ) < 0 )
        goto cleanup;

    /* Part A: Re-edit and remux without a timecode file (preserving negative-POC CTS < timestamp[0].cts) */
    snprintf( cmd, sizeof(cmd), "%s %s %s >%s 2>&1", CLI_TIMELINEEDITOR, f_base_shifted, f_te_bare, CLI_DEV_NULL );
    int rc_a1 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s %s %s >%s 2>&1", CLI_LSHMUXER, f_base_shifted, f_lsh_pos, CLI_DEV_NULL );
    int rc_a2 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s -i %s -o %s >%s 2>&1", CLI_LSHMUXER, f_base_shifted, f_lsh_remux, CLI_DEV_NULL );
    int rc_a3 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s --shift-timeline -i %s -o %s >%s 2>&1", CLI_LSHMUXER, f_base_shifted, f_lsh_shift, CLI_DEV_NULL );
    int rc_a4 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s -i %s -o %s >%s 2>&1", CLI_REMUXER, f_base_shifted, f_remuxer, CLI_DEV_NULL );
    int rc_a5 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s --isom-version 6 --shift-timeline -i %s -o %s >%s 2>&1", CLI_LSHMUXER, f_base_unshifted, f_unsh_to_shift, CLI_DEV_NULL );
    int rc_a6 = system( cmd );

    TEST_ASSERT( rc_a1 == 0 && rc_a2 == 0 && rc_a3 == 0 && rc_a4 == 0 && rc_a5 == 0 && rc_a6 == 0,
                 "Negative-POC Part A: all no-timecode re-edit and remux passes succeeded" );

    track_timeline_metrics_t mb, ma1, ma2, ma3, ma4, ma5, ma6;
    TEST_ASSERT( inspect_track_timeline_metrics( f_base_shifted,  1, &mb  ) == 0
              && inspect_track_timeline_metrics( f_te_bare,       1, &ma1 ) == 0
              && inspect_track_timeline_metrics( f_lsh_pos,       1, &ma2 ) == 0
              && inspect_track_timeline_metrics( f_lsh_remux,     1, &ma3 ) == 0
              && inspect_track_timeline_metrics( f_lsh_shift,     1, &ma4 ) == 0
              && inspect_track_timeline_metrics( f_remuxer,       1, &ma5 ) == 0
              && inspect_track_timeline_metrics( f_unsh_to_shift, 1, &ma6 ) == 0,
                 "Negative-POC Part A: inspect timeline metrics" );

    /* Verify 1000 Hz remuxes (ma3, ma4, ma5, ma6) match f_base_shifted (mb) with 0 ticks drift */
    const track_timeline_metrics_t *remux_1000[4] = { &ma3, &ma4, &ma5, &ma6 };
    for( int g = 0; g < 4; g++ )
    {
        TEST_ASSERT( remux_1000[g]->mdhd_timescale      == 1000
                  && remux_1000[g]->mdhd_duration       == 160
                  && remux_1000[g]->tkhd_duration       == 160
                  && remux_1000[g]->stts_last_delta     == 40
                  && remux_1000[g]->d_comp              == 40
                  && remux_1000[g]->has_cslg            == 1
                  && remux_1000[g]->ctts_version        == 1
                  && remux_1000[g]->cslg_ctd_shift      == 40
                  && remux_1000[g]->cslg_least_delta    == -40
                  && remux_1000[g]->cslg_greatest_delta == 40
                  && remux_1000[g]->cslg_start_time     == 0
                  && remux_1000[g]->cslg_end_time       == 160
                  && remux_1000[g]->elst_count          == 1
                  && remux_1000[g]->elst_dur[0]         == 160
                  && remux_1000[g]->elst_mt[0]          == 0
                  && memcmp( remux_1000[g]->sample_dts, mb.sample_dts, 4 * sizeof(uint64_t) ) == 0
                  && memcmp( remux_1000[g]->sample_cts, mb.sample_cts, 4 * sizeof(uint64_t) ) == 0
                  && remux_1000[g]->sample_cts[1] == 0
                  && remux_1000[g]->sample_cts[1] < remux_1000[g]->sample_cts[0],
                     "Negative-POC Part A (1000 Hz remux): mdhd, tkhd, stts, ctts v1, cslg, elst, and per-sample DTS/CTS match with 0 ticks drift" );
    }

    /* Verify GCD-reduced 25 Hz bare re-edits (ma1: timelineeditor, ma2: lshmuxer positional) */
    const track_timeline_metrics_t *reedit_25[2] = { &ma1, &ma2 };
    for( int g = 0; g < 2; g++ )
    {
        TEST_ASSERT( reedit_25[g]->mdhd_timescale      == 25
                  && reedit_25[g]->mdhd_duration       == 4
                  && reedit_25[g]->tkhd_duration       == 160
                  && reedit_25[g]->stts_last_delta     == 1
                  && reedit_25[g]->d_comp              == 1
                  && reedit_25[g]->has_cslg            == 1
                  && reedit_25[g]->ctts_version        == 1
                  && reedit_25[g]->cslg_ctd_shift      == 1
                  && reedit_25[g]->cslg_least_delta    == -1
                  && reedit_25[g]->cslg_greatest_delta == 1
                  && reedit_25[g]->cslg_start_time     == 0
                  && reedit_25[g]->cslg_end_time       == 4
                  && reedit_25[g]->elst_count          == 1
                  && reedit_25[g]->elst_dur[0]         == 160
                  && reedit_25[g]->elst_mt[0]          == 0
                  && reedit_25[g]->sample_dts[0] == 0 && reedit_25[g]->sample_dts[1] == 1
                  && reedit_25[g]->sample_dts[2] == 2 && reedit_25[g]->sample_dts[3] == 3
                  && reedit_25[g]->sample_cts[0] == 1 && reedit_25[g]->sample_cts[1] == 0
                  && reedit_25[g]->sample_cts[2] == 3 && reedit_25[g]->sample_cts[3] == 2,
                     "Negative-POC Part A (25 Hz bare re-edit): mdhd, tkhd, stts, ctts v1, cslg, elst, and per-sample DTS/CTS match with 0 ticks drift" );
    }

    /* Part B: Re-edit with VFR Timecode v2 (50ms, 90ms, 140ms, 180ms -> 100 Hz timescale)
     * across timelineeditor, lshmuxer (positional, -i ?timecode=, --shift-timeline), and multi-generation re-edits */
    snprintf( cmd, sizeof(cmd), "%s --timecode %s %s %s >%s 2>&1",
              CLI_TIMELINEEDITOR, tcv2_path, f_base_shifted, f_vfr_te, CLI_DEV_NULL );
    int rc_b1 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s --timecode %s %s %s >%s 2>&1",
              CLI_LSHMUXER, tcv2_path, f_base_shifted, f_vfr_lsh_pos, CLI_DEV_NULL );
    int rc_b2 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s -i %s?timecode=%s -o %s >%s 2>&1",
              CLI_LSHMUXER, f_base_shifted, tcv2_path, f_vfr_lsh_opt, CLI_DEV_NULL );
    int rc_b3 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s --shift-timeline -i %s?timecode=%s -o %s >%s 2>&1",
              CLI_LSHMUXER, f_base_shifted, tcv2_path, f_vfr_lsh_shift, CLI_DEV_NULL );
    int rc_b4 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s --isom-version 6 --shift-timeline -i %s?timecode=%s -o %s >%s 2>&1",
              CLI_LSHMUXER, f_base_unshifted, tcv2_path, f_vfr_unsh_shift, CLI_DEV_NULL );
    int rc_b5 = system( cmd );
    /* Multi-generation re-edit and remux on top of the VFR-edited container */
    snprintf( cmd, sizeof(cmd), "%s %s %s >%s 2>&1",
              CLI_TIMELINEEDITOR, f_vfr_te, f_vfr_te_reedit, CLI_DEV_NULL );
    int rc_b6 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s -i %s -o %s >%s 2>&1",
              CLI_LSHMUXER, f_vfr_te, f_vfr_lsh_from, CLI_DEV_NULL );
    int rc_b7 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s -i %s -o %s >%s 2>&1",
              CLI_REMUXER, f_vfr_te, f_vfr_remux_from, CLI_DEV_NULL );
    int rc_b8 = system( cmd );

    TEST_ASSERT( rc_b1 == 0 && rc_b2 == 0 && rc_b3 == 0 && rc_b4 == 0
              && rc_b5 == 0 && rc_b6 == 0 && rc_b7 == 0 && rc_b8 == 0,
                 "Negative-POC Part B: all 8 VFR timecode re-edit and multi-generation remux passes succeeded without uint64_t underflow" );

    track_timeline_metrics_t mv[8];
    const char *vfr_files[8] = {
        f_vfr_te, f_vfr_lsh_pos, f_vfr_lsh_opt, f_vfr_lsh_shift,
        f_vfr_unsh_shift, f_vfr_te_reedit, f_vfr_lsh_from, f_vfr_remux_from
    };
    for( int g = 0; g < 8; g++ )
    {
        TEST_ASSERT( inspect_track_timeline_metrics( vfr_files[g], 1, &mv[g] ) == 0,
                     "Negative-POC Part B: inspect VFR timeline metrics" );
        /* Expected at 100 Hz (10ms timebase):
         * timecode.ts = [0, 4, 9, 13], sample_delay = 1, sample_delay_time = 4, D_comp = 4, mdhd_duration = 17
         * Decoding order c(i) = [1, 0, 3, 2]:
         *   i = 0: DTS = 0,  CTS = 4  (ctts = +4)
         *   i = 1: DTS = 4,  CTS = 0  (ctts = -4, earliest presentation CTS = 0 < timestamp[0].cts = 4)
         *   i = 2: DTS = 8,  CTS = 13 (ctts = +5)
         *   i = 3: DTS = 13, CTS = 9  (ctts = -4)
         * cslg: ctd_shift = 4, least_delta = -4, greatest_delta = 5, start_time = 0, end_time = 17
         * elst: [0] empty duration = 50, media_time = -1; [1] segment_duration = 170, media_time = 0
         * tkhd_duration = 50 + 170 = 220 */
        /* Note: g = 0..4 take the 1000 Hz base container as input and preserve movie_timescale = 1000 Hz
         * (tkhd = 220, elst = [50, 170]), whereas g = 5..7 take f_vfr_te (100 Hz) as input and set
         * movie_timescale = 100 Hz (tkhd = 22, elst = [5, 17]). */
        uint64_t exp_tkhd  = (g >= 5) ?  22 : 220;
        uint64_t exp_elst0 = (g >= 5) ?   5 :  50;
        uint64_t exp_elst1 = (g >= 5) ?  17 : 170;
        TEST_ASSERT( mv[g].mdhd_timescale      == 100
                  && mv[g].mdhd_duration       == 17
                  && mv[g].tkhd_duration       == exp_tkhd
                  && mv[g].stts_last_delta     == 4
                  && mv[g].d_comp              == 4
                  && mv[g].has_cslg            == 1
                  && mv[g].ctts_version        == 1
                  && mv[g].cslg_ctd_shift      == 4
                  && mv[g].cslg_least_delta    == -4
                  && mv[g].cslg_greatest_delta == 5
                  && mv[g].cslg_start_time     == 0
                  && mv[g].cslg_end_time       == 17
                  && mv[g].elst_count          == 2
                  && mv[g].elst_dur[0]         == exp_elst0
                  && mv[g].elst_mt[0]          == ISOM_EDIT_MODE_EMPTY
                  && mv[g].elst_dur[1]         == exp_elst1
                  && mv[g].elst_mt[1]          == 0
                  && mv[g].sample_dts[0] == 0  && mv[g].sample_dts[1] == 4
                  && mv[g].sample_dts[2] == 8  && mv[g].sample_dts[3] == 13
                  && mv[g].sample_cts[0] == 4  && mv[g].sample_cts[1] == 0
                  && mv[g].sample_cts[2] == 13 && mv[g].sample_cts[3] == 9,
                     "Negative-POC Part B: VFR mdhd, tkhd, stts, ctts v1, cslg, elst, and per-sample DTS/CTS match with 0 ticks drift" );
    }

    rc = 0;

cleanup:
    for( size_t i = 0; i < sizeof(all_files) / sizeof(all_files[0]); i++ )
        remove( all_files[i] );
    TEST_ASSERT( rc == 0, "Negative-POC --shift-timeline VFR re-edit and multi-generation remux verified" );
    return 0;
}

typedef struct
{
    uint32_t moof_count;
    uint32_t trun_count;
    uint8_t  trun_version[4];
    int32_t  trun_min_offset[4];
    int32_t  trun_max_offset[4];
    uint64_t mehd_duration;
    uint32_t mdhd_timescale;
    uint64_t media_duration;
    uint32_t d_comp;
    uint32_t ctd_shift;
    uint32_t elst_count;
    uint64_t elst_dur[2];
    int64_t  elst_mt[2];
    uint64_t sample_dts[8];
    uint64_t sample_cts[8];
} frag_track_metrics_t;

static int create_synth_negative_poc_2gop_mp4( const char *path, int shifted )
{
    remove( path );
    lsmash_root_t *root = lsmash_create_root();
    TEST_ASSERT( root != NULL, "neg_poc_2gop: create root" );
    lsmash_file_parameters_t fp = { 0 };
    TEST_ASSERT( lsmash_open_file( path, 0, &fp ) == 0, "neg_poc_2gop: open file" );
    fp.major_brand   = ISOM_BRAND_TYPE_MP42;
    fp.minor_version = 0;
    lsmash_brand_type brands[4] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_ISO4, ISOM_BRAND_TYPE_AV01, ISOM_BRAND_TYPE_MP42 };
    fp.brands        = brands;
    fp.brand_count   = 4;
    TEST_ASSERT( lsmash_set_file( root, &fp ) != NULL, "neg_poc_2gop: set file" );

    lsmash_movie_parameters_t mp;
    lsmash_initialize_movie_parameters( &mp );
    mp.timescale = 1000;
    TEST_ASSERT( lsmash_set_movie_parameters( root, &mp ) == 0, "neg_poc_2gop: set movie params" );

    uint32_t tid = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
    TEST_ASSERT( tid == 1, "neg_poc_2gop: create track" );
    lsmash_track_parameters_t tp;
    lsmash_initialize_track_parameters( &tp );
    tp.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
    tp.display_width  = 16 << 16;
    tp.display_height = 16 << 16;
    TEST_ASSERT( lsmash_set_track_parameters( root, tid, &tp ) == 0, "neg_poc_2gop: set track params" );

    lsmash_media_parameters_t mdp;
    lsmash_initialize_media_parameters( &mdp );
    mdp.timescale = 1000;
    TEST_ASSERT( lsmash_set_media_parameters( root, tid, &mdp ) == 0, "neg_poc_2gop: set media params" );

    lsmash_video_summary_t *summary = create_synth_av01_summary( 16, 16 );
    TEST_ASSERT( summary != NULL, "neg_poc_2gop: create synth summary" );
    uint32_t s_idx = lsmash_add_sample_entry( root, tid, summary );
    lsmash_cleanup_summary( (lsmash_summary_t *)summary );
    TEST_ASSERT( s_idx == 1, "neg_poc_2gop: add sample entry" );

    /* 8 samples across 2 GOPs (RAP at sample 0 and sample 4) with Negative POC in both GOPs:
     * - Shifted   (ctd_shift = 40): DTS = [0, 40, 80, 120, 160, 200, 240, 280],
     *                               CTS = [40, 0, 120, 80, 200, 160, 280, 240]
     * - Unshifted (ctd_shift = 0):  DTS = [0, 40, 80, 120, 160, 200, 240, 280],
     *                               CTS = [80, 40, 160, 120, 240, 200, 320, 280] */
    const uint64_t dts_arr[8] = { 0, 40, 80, 120, 160, 200, 240, 280 };
    const uint64_t cts_arr[8] = {
        shifted ?  40 :  80,
        shifted ?   0 :  40,
        shifted ? 120 : 160,
        shifted ?  80 : 120,
        shifted ? 200 : 240,
        shifted ? 160 : 200,
        shifted ? 280 : 320,
        shifted ? 240 : 280
    };
    for( int i = 0; i < 8; i++ )
    {
        lsmash_sample_t *sample = lsmash_create_sample( 4 );
        memset( sample->data, 0, 4 );
        sample->dts   = dts_arr[i];
        sample->cts   = cts_arr[i];
        sample->index = s_idx;
        sample->prop.ra_flags = (i == 0 || i == 4)
                              ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC
                              : ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
        TEST_ASSERT( lsmash_append_sample( root, tid, sample ) == 0, "neg_poc_2gop: append sample" );
    }
    TEST_ASSERT( lsmash_flush_pooled_samples( root, tid, 40 ) == 0, "neg_poc_2gop: flush pooled samples" );
    TEST_ASSERT( lsmash_update_track_duration( root, tid, 0 ) == 0, "neg_poc_2gop: update track duration" );

    lsmash_edit_t edit;
    edit.duration   = 320;
    edit.start_time = shifted ? 0 : 40;
    edit.rate       = ISOM_EDIT_MODE_NORMAL;
    TEST_ASSERT( lsmash_create_explicit_timeline_map( root, tid, edit ) == 0, "neg_poc_2gop: create elst" );
    TEST_ASSERT( lsmash_finish_movie( root, NULL ) == 0, "neg_poc_2gop: finish movie" );
    lsmash_close_file( &fp );
    lsmash_destroy_root( root );
    return 0;
}

static int inspect_fragmented_track_metrics( const char *path, uint32_t track_number, frag_track_metrics_t *m )
{
    memset( m, 0, sizeof(*m) );
    lsmash_root_t *root = lsmash_create_root();
    TEST_ASSERT( root != NULL, "frag_metrics: create root" );
    lsmash_file_parameters_t fp = { 0 };
    TEST_ASSERT( lsmash_open_file( path, 1, &fp ) == 0, "frag_metrics: open file" );
    lsmash_file_t *file = lsmash_set_file( root, &fp );
    TEST_ASSERT( file != NULL, "frag_metrics: set file" );
    TEST_ASSERT( lsmash_read_file( file, &fp ) >= 0, "frag_metrics: read file" );

    uint32_t track_ID = lsmash_get_track_ID( root, track_number );
    TEST_ASSERT( track_ID != 0, "frag_metrics: get track_ID" );

    isom_trak_t *trak = NULL;
    isom_trak_t *cur_trak = NULL;
    LSM_ENTRY_LIST_FOR_EACH( cur_trak, &file->moov->trak_list )
    {
        if( cur_trak->tkhd && cur_trak->tkhd->track_ID == track_ID )
        {
            trak = cur_trak;
            break;
        }
    }
    TEST_ASSERT( trak && trak->mdia && trak->mdia->mdhd && LSMASH_IS_EXISTING_BOX( file->moov->mvex ),
                 "frag_metrics: trak, mdhd, and mvex exist" );
    m->mdhd_timescale = trak->mdia->mdhd->timescale;
    if( LSMASH_IS_EXISTING_BOX( file->moov->mvex->mehd ) )
        m->mehd_duration = file->moov->mvex->mehd->fragment_duration;

    isom_moof_t *moof = NULL;
    LSM_ENTRY_LIST_FOR_EACH( moof, &file->moof_list )
    {
        ++m->moof_count;
        isom_traf_t *traf = NULL;
        LSM_ENTRY_LIST_FOR_EACH( traf, &moof->traf_list )
        {
            if( LSMASH_IS_NON_EXISTING_BOX( traf->tfhd ) || traf->tfhd->track_ID != track_ID )
                continue;
            isom_trun_t *trun = NULL;
            LSM_ENTRY_LIST_FOR_EACH( trun, &traf->trun_list )
            {
                if( m->trun_count < 4 )
                {
                    m->trun_version[m->trun_count]    = trun->version;
                    m->trun_min_offset[m->trun_count] = INT32_MAX;
                    m->trun_max_offset[m->trun_count] = INT32_MIN;
                    isom_trun_optional_row_t *row = NULL;
                    LSM_LIST_FOR_EACH( row, trun->optional )
                    {
                        int32_t signed_off = (int32_t)row->sample_composition_time_offset;
                        if( signed_off < m->trun_min_offset[m->trun_count] )
                            m->trun_min_offset[m->trun_count] = signed_off;
                        if( signed_off > m->trun_max_offset[m->trun_count] )
                            m->trun_max_offset[m->trun_count] = signed_off;
                    }
                }
                ++m->trun_count;
            }
        }
    }

    TEST_ASSERT( lsmash_construct_timeline( root, track_ID ) == 0, "frag_metrics: construct timeline" );
    uint32_t sample_count = lsmash_get_sample_count_in_media_timeline( root, track_ID );
    TEST_ASSERT( sample_count == 8, "frag_metrics: timeline sample_count == 8" );
    TEST_ASSERT( lsmash_get_composition_to_decode_shift_from_media_timeline( root, track_ID, &m->ctd_shift ) == 0,
                 "frag_metrics: get ctd_shift" );
    m->media_duration = lsmash_get_media_duration_from_media_timeline( root, track_ID );
    TEST_ASSERT( lsmash_get_last_sample_delta_from_media_timeline( root, track_ID, &m->d_comp ) == 0,
                 "frag_metrics: get d_comp" );

    lsmash_media_ts_list_t ts_list = { 0 };
    TEST_ASSERT( lsmash_get_media_timestamps( root, track_ID, &ts_list ) == 0 && ts_list.sample_count == 8,
                 "frag_metrics: get media timestamps" );
    for( uint32_t i = 1; i <= sample_count; i++ )
    {
        uint64_t dts = 0, cts = 0;
        TEST_ASSERT( lsmash_get_dts_from_media_timeline( root, track_ID, i, &dts ) == 0, "frag_metrics: get dts" );
        TEST_ASSERT( lsmash_get_cts_from_media_timeline( root, track_ID, i, &cts ) == 0, "frag_metrics: get cts" );
        TEST_ASSERT( dts == ts_list.timestamp[i - 1].dts && cts == ts_list.timestamp[i - 1].cts,
                     "frag_metrics: lsmash_get_media_timestamps matches per-sample timeline queries" );
        TEST_ASSERT( dts <= cts + m->ctd_shift, "frag_metrics: DTS <= CTS + ctd_shift invariant holds" );
        m->sample_dts[i - 1] = dts;
        m->sample_cts[i - 1] = cts;
    }
    lsmash_delete_media_timestamps( &ts_list );

    m->elst_count = lsmash_count_explicit_timeline_map( root, track_ID );
    for( uint32_t k = 1; k <= m->elst_count && k <= 2; k++ )
    {
        lsmash_edit_t edit = { 0 };
        TEST_ASSERT( lsmash_get_explicit_timeline_map( root, track_ID, k, &edit ) == 0, "frag_metrics: get elst" );
        m->elst_dur[k - 1] = edit.duration;
        m->elst_mt[k - 1]  = edit.start_time;
    }

    lsmash_close_file( &fp );
    lsmash_destroy_root( root );
    return 0;
}

static int assert_fragmented_negative_poc_trun_v1_roundtrip( void )
{
    const char *f_base_shifted    = "test_cli_eq_frag_negpoc_shifted.mp4";
    const char *f_base_unshifted  = "test_cli_eq_frag_negpoc_unshifted.mp4";
    const char *f_tcv2_8          = "test_cli_eq_frag_negpoc_tcv2_8.txt";
    const char *f_frag_from_unsh  = "test_cli_eq_frag_from_unsh.mp4";
    const char *f_frag_from_sh_l  = "test_cli_eq_frag_from_sh_lsh.mp4";
    const char *f_frag_from_sh_r  = "test_cli_eq_frag_from_sh_rem.mp4";
    const char *f_frag_refrag_l   = "test_cli_eq_frag_refrag_lsh.mp4";
    const char *f_frag_refrag_r   = "test_cli_eq_frag_refrag_rem.mp4";
    const char *f_dump_box        = "test_cli_eq_frag_dump_box.txt";
    const char *f_dump_ts         = "test_cli_eq_frag_dump_ts.txt";
    const char *f_defrag_lsh      = "test_cli_eq_defrag_lsh.mp4";
    const char *f_defrag_rem      = "test_cli_eq_defrag_rem.mp4";
    const char *f_defrag_te_bare  = "test_cli_eq_defrag_te_bare.mp4";
    const char *f_defrag_lsh_pos  = "test_cli_eq_defrag_lsh_pos.mp4";
    const char *f_vfr_te          = "test_cli_eq_frag_vfr_te.mp4";
    const char *f_vfr_lsh         = "test_cli_eq_frag_vfr_lsh.mp4";
    const char *f_vfr_frag        = "test_cli_eq_frag_vfr_frag.mp4";
    const char *f_vfr_frag_shift  = "test_cli_eq_frag_vfr_frag_shift.mp4";
    const char *all_files[] = {
        f_base_shifted, f_base_unshifted, f_tcv2_8,
        f_frag_from_unsh, f_frag_from_sh_l, f_frag_from_sh_r, f_frag_refrag_l, f_frag_refrag_r,
        f_dump_box, f_dump_ts,
        f_defrag_lsh, f_defrag_rem, f_defrag_te_bare, f_defrag_lsh_pos,
        f_vfr_te, f_vfr_lsh, f_vfr_frag, f_vfr_frag_shift
    };
    char cmd[1024];
    int rc = -1;

    for( size_t i = 0; i < sizeof(all_files) / sizeof(all_files[0]); i++ )
        remove( all_files[i] );

    /* 8-frame VFR Timecode v2 with 50ms initial offset and intervals [40, 50, 40, 40, 40, 50, 40] ms */
    static const char tcv2_8_text[] =
        "# timecode format v2\n"
        "50.0\n"
        "90.0\n"
        "140.0\n"
        "180.0\n"
        "220.0\n"
        "260.0\n"
        "310.0\n"
        "350.0\n";
    TEST_ASSERT( write_test_file( f_tcv2_8, (const uint8_t *)tcv2_8_text, sizeof(tcv2_8_text) - 1 ) == 0,
                 "write 8-frame timecode v2 file" );

    if( create_synth_negative_poc_2gop_mp4( f_base_shifted, 1 ) < 0
     || create_synth_negative_poc_2gop_mp4( f_base_unshifted, 0 ) < 0 )
        goto cleanup;

    /* Part A: Fragmented MP4 generation (2 moof/trun fragments) across lshmuxer and remuxer */
    snprintf( cmd, sizeof(cmd), "%s --fragment 1 --shift-timeline -i %s -o %s >%s 2>&1",
              CLI_LSHMUXER, f_base_unshifted, f_frag_from_unsh, CLI_DEV_NULL );
    int rc_f1 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s --fragment 1 -i %s -o %s >%s 2>&1",
              CLI_LSHMUXER, f_base_shifted, f_frag_from_sh_l, CLI_DEV_NULL );
    int rc_f2 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s --fragment 1 -i %s -o %s >%s 2>&1",
              CLI_REMUXER, f_base_shifted, f_frag_from_sh_r, CLI_DEV_NULL );
    int rc_f3 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s --fragment 1 -i %s -o %s >%s 2>&1",
              CLI_LSHMUXER, f_frag_from_unsh, f_frag_refrag_l, CLI_DEV_NULL );
    int rc_f4 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s --fragment 1 -i %s -o %s >%s 2>&1",
              CLI_REMUXER, f_frag_from_unsh, f_frag_refrag_r, CLI_DEV_NULL );
    int rc_f5 = system( cmd );

    TEST_ASSERT( rc_f1 == 0 && rc_f2 == 0 && rc_f3 == 0 && rc_f4 == 0 && rc_f5 == 0,
                 "Frag Negative-POC Part A: all 5 fragmented MP4 generation and re-fragmentation passes succeeded" );

    const char *frag_files[5] = {
        f_frag_from_unsh, f_frag_from_sh_l, f_frag_from_sh_r, f_frag_refrag_l, f_frag_refrag_r
    };
    const uint64_t exp_dts[8] = {  0, 40,  80, 120, 160, 200, 240, 280 };
    const uint64_t exp_cts[8] = { 40,  0, 120,  80, 200, 160, 280, 240 };
    for( int g = 0; g < 5; g++ )
    {
        frag_track_metrics_t fm;
        TEST_ASSERT( inspect_fragmented_track_metrics( frag_files[g], 1, &fm ) == 0,
                     "Frag Negative-POC Part A: inspect fragmented metrics" );
        TEST_ASSERT( fm.moof_count          == 2
                  && fm.trun_count          == 2
                  && fm.trun_version[0]     == 1
                  && fm.trun_version[1]     == 1
                  && fm.trun_min_offset[0]  == -40
                  && fm.trun_max_offset[0]  ==  40
                  && fm.trun_min_offset[1]  == -40
                  && fm.trun_max_offset[1]  ==  40
                  && fm.mehd_duration       == 320
                  && fm.mdhd_timescale      == 1000
                  && fm.media_duration      == 320
                  && fm.d_comp              == 40
                  && fm.ctd_shift           == 40
                  && fm.elst_count          == 1
                  && fm.elst_mt[0]          == 0
                  && memcmp( fm.sample_dts, exp_dts, sizeof(exp_dts) ) == 0
                  && memcmp( fm.sample_cts, exp_cts, sizeof(exp_cts) ) == 0,
                     "Frag Negative-POC Part A: both moof #1 and moof #2 have trun v1 (-40/+40), ctd_shift=40, elst media_time=0, and exact 8-sample DTS/CTS" );
    }

    /* Verify boxdumper --box and boxdumper --timestamp on the multi-fragment trun v1 container */
    snprintf( cmd, sizeof(cmd), "%s --box %s > %s", CLI_BOXDUMPER, f_frag_from_unsh, f_dump_box );
    TEST_ASSERT( system( cmd ) == 0, "Frag Negative-POC Part A: boxdumper --box succeeded" );
    snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s", CLI_BOXDUMPER, f_frag_from_unsh, f_dump_ts );
    TEST_ASSERT( system( cmd ) == 0, "Frag Negative-POC Part A: boxdumper --timestamp succeeded" );
    {
        size_t box_len = 0, ts_len = 0;
        char *box_txt = read_text_file( f_dump_box, &box_len );
        char *ts_txt  = read_text_file( f_dump_ts,  &ts_len );
        int box_ok = box_txt
                  && strstr( box_txt, "sample_composition_time_offset = -40" ) != NULL
                  && strstr( box_txt, "4294967256" ) == NULL;
        int ts_ok  = ts_txt
                  && strstr( ts_txt, "Media timescale: 1000" ) != NULL
                  && strstr( ts_txt, "DTS = 0, CTS = 80" ) != NULL
                  && strstr( ts_txt, "DTS = 40, CTS = 40" ) != NULL
                  && strstr( ts_txt, "DTS = 200, CTS = 200" ) != NULL;
        lsmash_free( box_txt );
        lsmash_free( ts_txt );
        TEST_ASSERT( box_ok && ts_ok,
                     "Frag Negative-POC Part A: boxdumper --box and --timestamp format signed trun v1 offsets (-40) and ctd_shift (40) without unsigned wrap" );
    }

    /* Part B: Fragmented MP4 -> Non-Fragmented MP4 round-trip via lshmuxer, remuxer, and timelineeditor */
    snprintf( cmd, sizeof(cmd), "%s -i %s -o %s >%s 2>&1",
              CLI_LSHMUXER, f_frag_from_unsh, f_defrag_lsh, CLI_DEV_NULL );
    int rc_d1 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s -i %s -o %s >%s 2>&1",
              CLI_REMUXER, f_frag_from_unsh, f_defrag_rem, CLI_DEV_NULL );
    int rc_d2 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s %s %s >%s 2>&1",
              CLI_TIMELINEEDITOR, f_frag_from_unsh, f_defrag_te_bare, CLI_DEV_NULL );
    int rc_d3 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s %s %s >%s 2>&1",
              CLI_LSHMUXER, f_frag_from_unsh, f_defrag_lsh_pos, CLI_DEV_NULL );
    int rc_d4 = system( cmd );

    TEST_ASSERT( rc_d1 == 0 && rc_d2 == 0 && rc_d3 == 0 && rc_d4 == 0,
                 "Frag Negative-POC Part B: all 4 defragmenting remux and bare re-edit passes succeeded" );

    track_timeline_metrics_t mb2, md1, md2, md3, md4;
    TEST_ASSERT( inspect_track_timeline_metrics( f_base_shifted,   1, &mb2 ) == 0
              && inspect_track_timeline_metrics( f_defrag_lsh,     1, &md1 ) == 0
              && inspect_track_timeline_metrics( f_defrag_rem,     1, &md2 ) == 0
              && inspect_track_timeline_metrics( f_defrag_te_bare, 1, &md3 ) == 0
              && inspect_track_timeline_metrics( f_defrag_lsh_pos, 1, &md4 ) == 0,
                 "Frag Negative-POC Part B: inspect defragmented non-fragmented timeline metrics" );

    const track_timeline_metrics_t *defrag_1000[2] = { &md1, &md2 };
    for( int g = 0; g < 2; g++ )
    {
        TEST_ASSERT( defrag_1000[g]->mdhd_timescale      == 1000
                  && defrag_1000[g]->mdhd_duration       == 320
                  && defrag_1000[g]->tkhd_duration       == 320
                  && defrag_1000[g]->stts_last_delta     == 40
                  && defrag_1000[g]->d_comp              == 40
                  && defrag_1000[g]->has_cslg            == 1
                  && defrag_1000[g]->ctts_version        == 1
                  && defrag_1000[g]->cslg_ctd_shift      == 40
                  && defrag_1000[g]->cslg_least_delta    == -40
                  && defrag_1000[g]->cslg_greatest_delta == 40
                  && defrag_1000[g]->cslg_start_time     == 0
                  && defrag_1000[g]->cslg_end_time       == 320
                  && defrag_1000[g]->elst_count          == 1
                  && defrag_1000[g]->elst_dur[0]         == 320
                  && defrag_1000[g]->elst_mt[0]          == 0
                  && memcmp( defrag_1000[g]->sample_dts, mb2.sample_dts, 8 * sizeof(uint64_t) ) == 0
                  && memcmp( defrag_1000[g]->sample_cts, mb2.sample_cts, 8 * sizeof(uint64_t) ) == 0,
                     "Frag Negative-POC Part B (1000 Hz defrag): mdhd, tkhd, stts, ctts v1, cslg, elst, and 8-sample DTS/CTS match with 0 ticks drift" );
    }

    const track_timeline_metrics_t *defrag_25[2] = { &md3, &md4 };
    for( int g = 0; g < 2; g++ )
    {
        TEST_ASSERT( defrag_25[g]->mdhd_timescale      == 25
                  && defrag_25[g]->mdhd_duration       == 8
                  && defrag_25[g]->tkhd_duration       == 320
                  && defrag_25[g]->stts_last_delta     == 1
                  && defrag_25[g]->d_comp              == 1
                  && defrag_25[g]->has_cslg            == 1
                  && defrag_25[g]->ctts_version        == 1
                  && defrag_25[g]->cslg_ctd_shift      == 1
                  && defrag_25[g]->cslg_least_delta    == -1
                  && defrag_25[g]->cslg_greatest_delta == 1
                  && defrag_25[g]->cslg_start_time     == 0
                  && defrag_25[g]->cslg_end_time       == 8
                  && defrag_25[g]->elst_count          == 1
                  && defrag_25[g]->elst_dur[0]         == 320
                  && defrag_25[g]->elst_mt[0]          == 0
                  && defrag_25[g]->sample_dts[1] == 1 && defrag_25[g]->sample_cts[1] == 0
                  && defrag_25[g]->sample_dts[5] == 5 && defrag_25[g]->sample_cts[5] == 4,
                     "Frag Negative-POC Part B (25 Hz bare re-edit): mdhd, tkhd, stts, ctts v1, cslg, elst, and 8-sample DTS/CTS match with 0 ticks drift" );
    }

    /* Part C: Fragmented MP4 -> VFR Timecode v2 re-edit into both Non-Fragmented and Fragmented MP4 */
    snprintf( cmd, sizeof(cmd), "%s --timecode %s %s %s >%s 2>&1",
              CLI_TIMELINEEDITOR, f_tcv2_8, f_frag_from_unsh, f_vfr_te, CLI_DEV_NULL );
    int rc_c1 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s -i %s?timecode=%s -o %s >%s 2>&1",
              CLI_LSHMUXER, f_frag_from_unsh, f_tcv2_8, f_vfr_lsh, CLI_DEV_NULL );
    int rc_c2 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s --fragment 1 -i %s?timecode=%s -o %s >%s 2>&1",
              CLI_LSHMUXER, f_frag_from_unsh, f_tcv2_8, f_vfr_frag, CLI_DEV_NULL );
    int rc_c3 = system( cmd );
    snprintf( cmd, sizeof(cmd), "%s --fragment 1 --shift-timeline -i %s?timecode=%s -o %s >%s 2>&1",
              CLI_LSHMUXER, f_base_unshifted, f_tcv2_8, f_vfr_frag_shift, CLI_DEV_NULL );
    int rc_c4 = system( cmd );

    TEST_ASSERT( rc_c1 == 0 && rc_c2 == 0 && rc_c3 == 0 && rc_c4 == 0,
                 "Frag Negative-POC Part C: all 4 VFR timecode re-edits (non-fragmented and fragmented) succeeded" );

    /* Expected VFR timestamps at 100 Hz (10ms timebase):
     * timecode.ts = [0, 4, 9, 13, 17, 21, 26, 30], sample_delay = 1, sample_delay_time = 4, D_comp = 4, media_duration = 34
     * Decoding order c(i) = [1, 0, 3, 2, 5, 4, 7, 6]:
     *   DTS = [0, 4,  8, 13, 17, 21, 25, 30]
     *   CTS = [4, 0, 13,  9, 21, 17, 30, 26]
     *   offsets = [+4, -4, +5, -4, +4, -4, +5, -4] */
    const uint64_t exp_vfr_dts[8] = { 0, 4,  8, 13, 17, 21, 25, 30 };
    const uint64_t exp_vfr_cts[8] = { 4, 0, 13,  9, 21, 17, 30, 26 };

    track_timeline_metrics_t mc1, mc2;
    TEST_ASSERT( inspect_track_timeline_metrics( f_vfr_te,  1, &mc1 ) == 0
              && inspect_track_timeline_metrics( f_vfr_lsh, 1, &mc2 ) == 0,
                 "Frag Negative-POC Part C: inspect non-fragmented VFR outputs" );
    const track_timeline_metrics_t *vfr_nonfrag[2] = { &mc1, &mc2 };
    for( int g = 0; g < 2; g++ )
    {
        TEST_ASSERT( vfr_nonfrag[g]->mdhd_timescale      == 100
                  && vfr_nonfrag[g]->mdhd_duration       == 34
                  && vfr_nonfrag[g]->tkhd_duration       == 390
                  && vfr_nonfrag[g]->stts_last_delta     == 4
                  && vfr_nonfrag[g]->d_comp              == 4
                  && vfr_nonfrag[g]->has_cslg            == 1
                  && vfr_nonfrag[g]->ctts_version        == 1
                  && vfr_nonfrag[g]->cslg_ctd_shift      == 4
                  && vfr_nonfrag[g]->cslg_least_delta    == -4
                  && vfr_nonfrag[g]->cslg_greatest_delta == 5
                  && vfr_nonfrag[g]->cslg_start_time     == 0
                  && vfr_nonfrag[g]->cslg_end_time       == 34
                  && vfr_nonfrag[g]->elst_count          == 2
                  && vfr_nonfrag[g]->elst_dur[0]         == 50
                  && vfr_nonfrag[g]->elst_mt[0]          == ISOM_EDIT_MODE_EMPTY
                  && vfr_nonfrag[g]->elst_dur[1]         == 340
                  && vfr_nonfrag[g]->elst_mt[1]          == 0
                  && memcmp( vfr_nonfrag[g]->sample_dts, exp_vfr_dts, sizeof(exp_vfr_dts) ) == 0
                  && memcmp( vfr_nonfrag[g]->sample_cts, exp_vfr_cts, sizeof(exp_vfr_cts) ) == 0,
                     "Frag Negative-POC Part C (non-fragmented VFR): mdhd, tkhd, stts, ctts v1, cslg, elst, and 8-sample VFR DTS/CTS match with 0 ticks drift" );
    }

    const char *vfr_frag_files[2] = { f_vfr_frag, f_vfr_frag_shift };
    for( int g = 0; g < 2; g++ )
    {
        frag_track_metrics_t fmc;
        TEST_ASSERT( inspect_fragmented_track_metrics( vfr_frag_files[g], 1, &fmc ) == 0,
                     "Frag Negative-POC Part C: inspect fragmented VFR outputs" );
        TEST_ASSERT( fmc.moof_count          == 2
                  && fmc.trun_count          == 2
                  && fmc.trun_version[0]     == 1
                  && fmc.trun_version[1]     == 1
                  && fmc.trun_min_offset[0]  == -4
                  && fmc.trun_max_offset[0]  ==  5
                  && fmc.trun_min_offset[1]  == -4
                  && fmc.trun_max_offset[1]  ==  5
                  && fmc.mdhd_timescale      == 100
                  && fmc.media_duration      == 34
                  && fmc.d_comp              == 4
                  && fmc.ctd_shift           == 4
                  && fmc.elst_count          == 2
                  && fmc.elst_dur[0]         == 50
                  && fmc.elst_mt[0]          == ISOM_EDIT_MODE_EMPTY
                  && fmc.elst_dur[1]         == 0
                  && fmc.elst_mt[1]          == 0
                  && memcmp( fmc.sample_dts, exp_vfr_dts, sizeof(exp_vfr_dts) ) == 0
                  && memcmp( fmc.sample_cts, exp_vfr_cts, sizeof(exp_vfr_cts) ) == 0,
                     "Frag Negative-POC Part C (fragmented VFR): both moof #1 and moof #2 have trun v1 (-4/+5), ctd_shift=4, elst=[50/-1, 0/0], and exact VFR DTS/CTS" );
    }

    rc = 0;

cleanup:
    for( size_t i = 0; i < sizeof(all_files) / sizeof(all_files[0]); i++ )
        remove( all_files[i] );
    TEST_ASSERT( rc == 0, "Fragmented MP4 (moof/trun v1) Negative-POC dump, round-trip remux, and VFR re-edit verified" );
    return 0;
}

static int assert_hybrid_vfr_last_duration( const char *f_in_v )
{
    const char *f_tcv2_np1   = "test_cli_eq_tcv2_np1.txt";
    const char *f_tcv1_still = "test_cli_eq_tcv1_still.txt";
    const char *f_tcv2_still = "test_cli_eq_tcv2_still.txt";
    const char *f_out        = "test_cli_eq_hybrid_out.mp4";
    const char *f_out_reedit = "test_cli_eq_hybrid_reedit.mp4";
    const char *f_api_noctts = "test_cli_eq_api_noctts.mp4";
    const char *f_api_ctts   = "test_cli_eq_api_ctts.mp4";
    char cmd[1024];
    int rc = -1;

    /* Stage 1: Timecode v2 with (N + 1)-th line explicit end timestamp (t_4 = 250.0 -> explicit D_comp = 70ms vs prev 40ms) */
    static const char tcv2_np1_text[] =
        "# timecode format v2\n"
        "50.0\n"
        "90.0\n"
        "140.0\n"
        "180.0\n"
        "250.0\n";
    TEST_ASSERT( write_test_file( f_tcv2_np1, (const uint8_t *)tcv2_np1_text, sizeof(tcv2_np1_text) - 1 ) == 0,
                 "write (N+1)-line timecode v2 file" );

    /* Stage 2: Timecode v1 with Assume 25.0 where frame N-2 (frame 2) is a 1.0 fps still frame (1000ms) and frame N-1 (frame 3) uses Assume 25.0 (40ms) */
    static const char tcv1_still_text[] =
        "# timecode format v1\n"
        "Assume 25.0\n"
        "2,2,1.0\n";
    TEST_ASSERT( write_test_file( f_tcv1_still, (const uint8_t *)tcv1_still_text, sizeof(tcv1_still_text) - 1 ) == 0,
                 "write still-frame timecode v1 file" );

    /* Stage 3a: N-line Timecode v2 where frame N-2 has a 1000ms still-frame duration vs 40ms median (1000 > 4 * 40 -> falls back to 40ms) */
    static const char tcv2_still_text[] =
        "# timecode format v2\n"
        "0.0\n"
        "40.0\n"
        "80.0\n"
        "1080.0\n";
    TEST_ASSERT( write_test_file( f_tcv2_still, (const uint8_t *)tcv2_still_text, sizeof(tcv2_still_text) - 1 ) == 0,
                 "write N-line still-frame timecode v2 file" );

    /* Verify 3-way CLI equivalence for all 3 stages */
    if( assert_timelineeditor_3way_equivalent(
            "--timecode test_cli_eq_tcv2_np1.txt --dts-compression",
            f_in_v,
            "timecode=test_cli_eq_tcv2_np1.txt,dts-compression",
            f_out ) < 0 )
        goto cleanup;

    if( assert_timelineeditor_3way_equivalent(
            "--timecode test_cli_eq_tcv1_still.txt",
            f_in_v,
            "timecode=test_cli_eq_tcv1_still.txt",
            f_out ) < 0 )
        goto cleanup;

    if( assert_timelineeditor_3way_equivalent(
            "--timecode test_cli_eq_tcv2_still.txt",
            f_in_v,
            "timecode=test_cli_eq_tcv2_still.txt",
            f_out ) < 0 )
        goto cleanup;

    /* Verify exact metrics for Stage 1 ((N+1)-line Timecode v2: span = 180 - 50 = 130ms, explicit D_comp = 250 - 180 = 70ms, total = 200ms) */
    {
        remove( f_out );
        remove( f_out_reedit );
        snprintf( cmd, sizeof(cmd), "%s --timecode %s %s %s >%s 2>&1",
                  CLI_TIMELINEEDITOR, f_tcv2_np1, f_in_v, f_out, CLI_DEV_NULL );
        TEST_ASSERT( system( cmd ) == 0, "Stage 1: timelineeditor succeeded" );
        track_timeline_metrics_t m1;
        TEST_ASSERT( inspect_track_timeline_metrics( f_out, 1, &m1 ) == 0, "Stage 1: inspect metrics" );
        /* GCD of 40, 50, 40, 70ms is 10ms -> reduced timescale = 100 Hz, D_comp = 7, mdhd_duration = 13 + 7 = 20 */
        TEST_ASSERT( m1.mdhd_timescale == 100 && m1.d_comp == 7 && m1.mdhd_duration == 20,
                     "Stage 1: (N+1)-line Timecode v2 sets explicit D_comp = 70ms and mdhd_duration = 200ms" );

        /* Re-running timelineeditor without a timecode file must preserve the explicit D_comp = 7 */
        snprintf( cmd, sizeof(cmd), "%s %s %s >%s 2>&1",
                  CLI_TIMELINEEDITOR, f_out, f_out_reedit, CLI_DEV_NULL );
        TEST_ASSERT( system( cmd ) == 0, "Stage 1 re-edit: timelineeditor succeeded" );
        track_timeline_metrics_t m1_re;
        TEST_ASSERT( inspect_track_timeline_metrics( f_out_reedit, 1, &m1_re ) == 0, "Stage 1 re-edit: inspect metrics" );
        TEST_ASSERT( m1_re.d_comp == 7 && m1_re.mdhd_duration == 20,
                     "Stage 1 re-edit: explicit D_comp and mdhd_duration are preserved across container re-edit" );
    }

    /* Verify exact metrics for Stage 2 (Timecode v1 with N-2 still frame: 40ms + 40ms + 1000ms + 40ms = 1120ms -> 56 ticks at 50 Hz, D_comp = 2) */
    {
        remove( f_out );
        snprintf( cmd, sizeof(cmd), "%s --timecode %s %s %s >%s 2>&1",
                  CLI_TIMELINEEDITOR, f_tcv1_still, f_in_v, f_out, CLI_DEV_NULL );
        TEST_ASSERT( system( cmd ) == 0, "Stage 2: timelineeditor succeeded" );
        track_timeline_metrics_t m2;
        TEST_ASSERT( inspect_track_timeline_metrics( f_out, 1, &m2 ) == 0, "Stage 2: inspect metrics" );
        TEST_ASSERT( m2.mdhd_timescale == 50 && m2.d_comp == 2 && m2.mdhd_duration == 56,
                     "Stage 2: Timecode v1 N-2 still frame does not drag N-1 duration (D_comp = 2/50s = 40ms, mdhd_duration = 56)" );
    }

    /* Verify exact metrics for Stage 3a (N-line Timecode v2 with N-2 still frame: 1000ms > 4 * 40ms -> falls back to D_med = 40ms -> 56 ticks at 50 Hz) */
    {
        remove( f_out );
        snprintf( cmd, sizeof(cmd), "%s --timecode %s %s %s >%s 2>&1",
                  CLI_TIMELINEEDITOR, f_tcv2_still, f_in_v, f_out, CLI_DEV_NULL );
        TEST_ASSERT( system( cmd ) == 0, "Stage 3a: timelineeditor succeeded" );
        track_timeline_metrics_t m3;
        TEST_ASSERT( inspect_track_timeline_metrics( f_out, 1, &m3 ) == 0, "Stage 3a: inspect metrics" );
        TEST_ASSERT( m3.mdhd_timescale == 50 && m3.d_comp == 2 && m3.mdhd_duration == 56,
                     "Stage 3a: N-line Timecode v2 outlier guard falls back from 1000ms to D_med = 40ms (mdhd_duration = 56)" );
    }

    /* Stage 3b: Direct C API (core/track.c:track_update_mdhd_duration with last_sample_delta == 0)
     * Test both non-ctts and ctts (B-frame reordered) tracks where the second-to-last composition interval is 1000 vs median 40 */
    for( int use_ctts = 0; use_ctts <= 1; use_ctts++ )
    {
        const char *api_path = use_ctts ? f_api_ctts : f_api_noctts;
        remove( api_path );

        lsmash_root_t *root = lsmash_create_root();
        TEST_ASSERT( root != NULL, "Stage 3b: create root" );
        lsmash_file_parameters_t fp = { 0 };
        TEST_ASSERT( lsmash_open_file( api_path, 0, &fp ) == 0, "Stage 3b: open file" );
        fp.major_brand   = ISOM_BRAND_TYPE_MP42;
        fp.minor_version = 0;
        lsmash_brand_type brands[3] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_AV01, ISOM_BRAND_TYPE_MP42 };
        fp.brands        = brands;
        fp.brand_count   = 3;
        TEST_ASSERT( lsmash_set_file( root, &fp ) != NULL, "Stage 3b: set file" );

        lsmash_movie_parameters_t mp;
        lsmash_initialize_movie_parameters( &mp );
        mp.timescale = 1000;
        TEST_ASSERT( lsmash_set_movie_parameters( root, &mp ) == 0, "Stage 3b: set movie params" );

        uint32_t tid = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        TEST_ASSERT( tid == 1, "Stage 3b: create track" );
        lsmash_track_parameters_t tp;
        lsmash_initialize_track_parameters( &tp );
        tp.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
        tp.display_width  = 16 << 16;
        tp.display_height = 16 << 16;
        TEST_ASSERT( lsmash_set_track_parameters( root, tid, &tp ) == 0, "Stage 3b: set track params" );

        lsmash_media_parameters_t mdp;
        lsmash_initialize_media_parameters( &mdp );
        mdp.timescale = 1000;
        TEST_ASSERT( lsmash_set_media_parameters( root, tid, &mdp ) == 0, "Stage 3b: set media params" );

        lsmash_video_summary_t *summary = create_synth_av01_summary( 16, 16 );
        TEST_ASSERT( summary != NULL, "Stage 3b: create synth av01 summary" );
        uint32_t s_idx = lsmash_add_sample_entry( root, tid, summary );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        TEST_ASSERT( s_idx == 1, "Stage 3b: add sample entry" );

        /* 4 samples:
         * Non-ctts: DTS = CTS = [0, 40, 80, 1080]
         * Ctts (I0, P2, B1, P3): DTS = [0, 40, 80, 120], CTS = [40, 120, 80, 1120] -> display CTS = [40, 80, 120, 1120] */
        const uint64_t dts_arr[4] = { 0, 40, 80, use_ctts ? 120 : 1080 };
        const uint64_t cts_arr[4] = { use_ctts ? 40 : 0, use_ctts ? 120 : 40, 80, use_ctts ? 1120 : 1080 };
        for( int i = 0; i < 4; i++ )
        {
            lsmash_sample_t *sample = lsmash_create_sample( 4 );
            memset( sample->data, 0, 4 );
            sample->dts   = dts_arr[i];
            sample->cts   = cts_arr[i];
            sample->index = s_idx;
            sample->prop.ra_flags = (i == 0) ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC : ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
            TEST_ASSERT( lsmash_append_sample( root, tid, sample ) == 0, "Stage 3b: append sample" );
        }
        /* Pass last_sample_delta = 0 to trigger automatic estimation with outlier guard in track_update_mdhd_duration */
        TEST_ASSERT( lsmash_flush_pooled_samples( root, tid, 0 ) == 0, "Stage 3b: flush pooled samples with delta=0" );
        TEST_ASSERT( lsmash_update_track_duration( root, tid, 0 ) == 0, "Stage 3b: update track duration with delta=0" );
        TEST_ASSERT( lsmash_finish_movie( root, NULL ) == 0, "Stage 3b: finish movie" );
        lsmash_close_file( &fp );
        lsmash_destroy_root( root );

        track_timeline_metrics_t m_api;
        TEST_ASSERT( inspect_track_timeline_metrics( api_path, 1, &m_api ) == 0, "Stage 3b: inspect metrics" );
        /* Preceding composition interval is 1000, median D_med is 40 (1000 > 4 * 40) -> falls back to D_comp = 40, mdhd_duration = 1080 + 40 = 1120 */
        TEST_ASSERT( m_api.d_comp == 40 && m_api.mdhd_duration == 1120,
                     "Stage 3b: core/track.c outlier guard falls back from 1000 to D_med = 40 (mdhd_duration = 1120)" );
    }

    rc = 0;

cleanup:
    remove( f_tcv2_np1 );
    remove( f_tcv1_still );
    remove( f_tcv2_still );
    remove( f_out );
    remove( f_out_reedit );
    remove( f_api_noctts );
    remove( f_api_ctts );
    TEST_ASSERT( rc == 0, "3-stage hybrid VFR last-frame composition duration (D_comp) verified across CLI and core API" );
    return 0;
}

int test_muxer_lshmuxer_cli_equivalence( void )
{
    const char *f_h264_cfr   = "test_cli_eq_cfr.h264";
    const char *f_h264_15fps = "test_cli_eq_15fps.h264";
    const char *f_h264_vfr   = "test_cli_eq_vfr.h264";
    const char *f_h264_sar   = "test_cli_eq_sar_change.h264";
    const char *f_hevc_hdr10 = "test_cli_eq_hdr10p.h265";
    const char *f_av1_ivf    = "test_cli_eq_render_change.ivf";
    const char *f_adts       = "test_cli_eq_audio.aac";
    const char *f_amr        = "test_cli_eq_audio.amr";
    const char *f_wave       = "test_cli_eq_audio.wav";
    const char *f_chap       = "test_cli_eq_chapters.txt";
    const char *f_tcv1       = "test_cli_eq_tcv1.txt";
    const char *f_tcv2       = "test_cli_eq_tcv2.txt";
    const char *f_in_v       = "test_cli_eq_in_v.mp4";
    const char *f_in_a       = "test_cli_eq_in_a.mp4";
    const char *f_in_multi   = "test_cli_eq_in_multi.mp4";
    const char *f_in_m4a     = "test_cli_eq_in_meta.m4a";
    const char *f_in_mov     = "test_cli_eq_in_qt.mov";

    uint8_t *buf = (uint8_t *)lsmash_malloc( 32768 );
    TEST_ASSERT( buf != NULL, "allocate synthesis scratch buffer" );

    /* 1. Synthesize raw test media and chapter files */
    uint32_t sz = build_h264_stream( buf, 1001, 48000, 1, 1, 0 ); /* 24000/1001 CFR with B-frames */
    TEST_ASSERT( write_test_file( f_h264_cfr, buf, sz ) == 0, "write CFR H.264 stream" );

    sz = build_h264_stream( buf, 1, 30, 1, 0, 0 ); /* 15/1 CFR (not in well_known_fps -> falls back to 25/1) */
    TEST_ASSERT( write_test_file( f_h264_15fps, buf, sz ) == 0, "write 15fps H.264 stream" );

    sz = build_h264_stream( buf, 1, 60, 0, 0, 0 ); /* 30/1 VFR (fixed_frame_rate_flag = 0 -> falls back to 25/1, no btrt) */
    TEST_ASSERT( write_test_file( f_h264_vfr, buf, sz ) == 0, "write VFR H.264 stream" );

    sz = build_h264_stream( buf, 1, 50, 1, 0, 1 ); /* 25/1 CFR with mid-stream SPS SAR change (1:1 -> 16:11) */
    TEST_ASSERT( write_test_file( f_h264_sar, buf, sz ) == 0, "write multi-SPS H.264 stream" );

    sz = build_hevc_hdr10p_stream( buf );
    TEST_ASSERT( write_test_file( f_hevc_hdr10, buf, sz ) == 0, "write HDR10+ HEVC stream" );

    sz = build_ivf_stream_with_render_change( buf );
    TEST_ASSERT( write_test_file( f_av1_ivf, buf, sz ) == 0, "write AV1 IVF stream with RenderWidth increase" );

    sz = build_adts_stream( buf, 6 );
    TEST_ASSERT( write_test_file( f_adts, buf, sz ) == 0, "write ADTS AAC stream" );

    sz = build_amr_nb_stream( buf, 5 );
    TEST_ASSERT( write_test_file( f_amr, buf, sz ) == 0, "write AMR-NB stream" );

    sz = build_wave_pcm16_stream( buf, 2400 );
    TEST_ASSERT( write_test_file( f_wave, buf, sz ) == 0, "write WAVE LPCM stream" );

    static const char chap_text[] =
        "CHAPTER01=00:00:00.000\n"
        "CHAPTER01NAME=Intro\n"
        "CHAPTER02=00:00:00.050\n"
        "CHAPTER02NAME=Main Part\n";
    TEST_ASSERT( write_test_file( f_chap, (const uint8_t *)chap_text, sizeof(chap_text) - 1 ) == 0, "write chapter file" );

    static const char tcv1_text[] =
        "# timecode format v1\n"
        "# comment line before assume\n"
        "\n"
        "Assume 23.976\n"
        "# VFR override section\n"
        "1,1,29.97\n";
    TEST_ASSERT( write_test_file( f_tcv1, (const uint8_t *)tcv1_text, sizeof(tcv1_text) - 1 ) == 0, "write timecode v1 file" );

    /* 4 frames with non-zero initial offset (50ms -> empty edit) and VFR intervals (40ms, 50ms, 40ms) */
    static const char tcv2_text[] =
        "# timecode format v2\n"
        "# initial comment\n"
        "50.0\n"
        "90.0\n"
        "# mid-file comment\n"
        "140.0\n"
        "180.0\n";
    TEST_ASSERT( write_test_file( f_tcv2, (const uint8_t *)tcv2_text, sizeof(tcv2_text) - 1 ) == 0, "write timecode v2 file" );

    lsmash_free( buf );

    int rc = -1;

    /* 2. Differential Equivalence Assertions (muxer vs lshmuxer) */

    /* Case 1: H.264 24000/1001 CFR with B-frames (verifies sdtp, ctts, stss, btrt, roll grouping, elst) */
    if( assert_cli_equivalent( "-i test_cli_eq_cfr.h264", "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 2: H.264 15fps non-whitelisted CFR (verifies well_known_fps[] fallback to 25/1 with btrt) */
    if( assert_cli_equivalent( "-i test_cli_eq_15fps.h264", "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 3: H.264 VFR (verifies !summary->vfr guard -> 25/1 fallback and btrt omission) */
    if( assert_cli_equivalent( "-i test_cli_eq_vfr.h264", "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 4: Video track options + ISOBMFF v6 shift-timeline + compact-size-table + copyright notices
     * (verifies ?fps= omitting btrt, ?constant-delay= scaled by timebase, ?par=, ?disable, ?alternate-group=,
     * ?language=, ?handler=, ?copyright=notice/lang, and --copyright-notice notice/lang) */
    if( assert_cli_equivalent(
            "--isom-version 6 --shift-timeline --compact-size-table --copyright-notice MovieCopy/eng "
            "-i test_cli_eq_cfr.h264?fps=30000/1001,par=16:9,constant-delay=2,language=jpn,alternate-group=2,disable,handler=CustomVideo,copyright=TrackCopy/jpn",
            "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 5: Implicit track 1 option syntax (?opt=val without track_num:) and copyright notice without /<lang> */
    if( assert_cli_equivalent(
            "--copyright-notice PlainMovieCopyright -i test_cli_eq_cfr.h264?1:fps=24,copyright=PlainTrackCopyright",
            "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 6: Multi-SPS H.264 stream with ?par=4:3 (verifies ?par= and btrt apply only to first sequence) */
    if( assert_cli_equivalent( "-i test_cli_eq_sar_change.h264?par=4:3", "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 7: HEVC with HDR10+ SEI (cdm4 brand), Dolby Vision (dby1 + dvcC), st3d, and 360 projection (prhd + equi) */
    if( assert_cli_equivalent(
            "-i test_cli_eq_hdr10p.h265?dv-profile=8.1,stereo-mode=left-right,projection=equirectangular,ypr=10.5/-5.25/1.0",
            "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 8: AV1 IVF with mid-stream RenderWidth increase + ?par=4:3 (verifies first sequence override & second sequence pasp) */
    if( assert_cli_equivalent( "-i test_cli_eq_render_change.ivf?par=4:3", "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 9: ADTS AAC in M4A with ?sbr, ?encoder-delay=1024, iTunes metadata, chpl with BOM, and reference chapter track */
    if( assert_cli_equivalent(
            "--file-format m4a --chapter test_cli_eq_chapters.txt --chpl-with-bom --chapter-track 1 "
            "--album-name Album --artist Artist --comment Comment --release-date 2026 --encoder Encoder "
            "--genre Genre --lyrics Lyrics --title Title --composer Composer --album-artist AlbumArtist "
            "--copyright Copyright --description Desc --grouping Group --tempo 128 "
            "-i test_cli_eq_audio.aac?sbr,encoder-delay=1024,language=eng",
            "test_cli_eq_out.m4a" ) < 0 )
        goto cleanup;

    /* Case 10: Raw WAVE LPCM into default MP4 (ipcm + pcmC) and QuickTime MOV (lpcm v2 + chan) */
    if( assert_cli_equivalent( "-i test_cli_eq_audio.wav", "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;
    if( assert_cli_equivalent( "--file-format mov -i test_cli_eq_audio.wav", "test_cli_eq_out.mov" ) < 0 )
        goto cleanup;

    /* Case 11: Multi-track (H.264 + AAC) with --optimize-pd, --interleave, --movie-timescale, and format branches (mov, 3gp, 3g2)
     * plus single-track --movie-timescale reset compatibility */
    if( assert_cli_equivalent(
            "--optimize-pd --interleave 100 --movie-timescale 90000 "
            "-i test_cli_eq_cfr.h264 -i test_cli_eq_audio.aac",
            "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;
    if( assert_cli_equivalent(
            "--file-format mov --chapter test_cli_eq_chapters.txt -i test_cli_eq_cfr.h264 -i test_cli_eq_audio.aac",
            "test_cli_eq_out.mov" ) < 0 )
        goto cleanup;
    if( assert_cli_equivalent(
            "--file-format 3gp --chapter test_cli_eq_chapters.txt -i test_cli_eq_cfr.h264 -i test_cli_eq_audio.aac",
            "test_cli_eq_out.3gp" ) < 0 )
        goto cleanup;
    if( assert_cli_equivalent(
            "--file-format 3g2 -i test_cli_eq_cfr.h264 -i test_cli_eq_audio.aac",
            "test_cli_eq_out.3g2" ) < 0 )
        goto cleanup;
    if( assert_cli_equivalent( "--movie-timescale 90000 -i test_cli_eq_cfr.h264", "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 12: Raw AMR-NB in --file-format 3gp (positive) and default MP4 without --file-format (negative) */
    if( assert_cli_equivalent( "--file-format 3gp -i test_cli_eq_audio.amr", "test_cli_eq_out.3gp" ) < 0 )
        goto cleanup;
    if( assert_cli_both_fail( "-i test_cli_eq_audio.amr", "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* 3. Prepare baseline container inputs for remuxer and timelineeditor equivalence suites */
    {
        char cmd[1024];
        snprintf( cmd, sizeof(cmd), "%s -i %s -o %s >%s 2>&1", CLI_LSHMUXER, f_h264_cfr, f_in_v, CLI_DEV_NULL );
        if( system( cmd ) != 0 )
            goto cleanup;
        snprintf( cmd, sizeof(cmd), "%s -i %s?encoder-delay=1024 -o %s >%s 2>&1", CLI_LSHMUXER, f_adts, f_in_a, CLI_DEV_NULL );
        if( system( cmd ) != 0 )
            goto cleanup;
        snprintf( cmd, sizeof(cmd), "%s --compact-size-table --title MultiTitle -i %s -i %s -o %s >%s 2>&1",
                  CLI_LSHMUXER, f_h264_cfr, f_adts, f_in_multi, CLI_DEV_NULL );
        if( system( cmd ) != 0 )
            goto cleanup;
        snprintf( cmd, sizeof(cmd),
                  "%s --file-format m4a --title SongTitle --artist SongArtist --album-name SongAlbum --tempo 120 -i %s -o %s >%s 2>&1",
                  CLI_LSHMUXER, f_adts, f_in_m4a, CLI_DEV_NULL );
        if( system( cmd ) != 0 )
            goto cleanup;
        snprintf( cmd, sizeof(cmd), "%s --file-format mov -i %s -i %s -o %s >%s 2>&1",
                  CLI_LSHMUXER, f_h264_cfr, f_adts, f_in_mov, CLI_DEV_NULL );
        if( system( cmd ) != 0 )
            goto cleanup;
    }

    /* 4. Differential Equivalence Assertions (remuxer vs lshmuxer) */

    /* Case 13: Multi-input container remuxing with max-chunk-duration and max-chunk-size */
    if( assert_remuxer_equivalent(
            "--max-chunk-duration 100 --max-chunk-size 1048576 -i test_cli_eq_in_v.mp4 -i test_cli_eq_in_a.mp4",
            "test_cli_eq_out.mp4", NULL ) < 0 )
        goto cleanup;

    /* Case 14: Multi-track container track options (?1:..., ?2:..., language=, handler=, alternate-group=, disable, and --language) */
    if( assert_remuxer_equivalent(
            "--language und -i test_cli_eq_in_multi.mp4?1:language=jpn,handler=RemuxVideo,alternate-group=1?2:language=eng,handler=RemuxAudio,alternate-group=2,disable",
            "test_cli_eq_out.mp4", NULL ) < 0 )
        goto cleanup;

    /* Case 15: Track removal (?2:remove) reducing a 2-track container to a 1-track container (verifies single-track mvhd timescale)
     * plus implicit track 1 option syntax (?opt=val without track_num:) */
    if( assert_remuxer_equivalent(
            "-i test_cli_eq_in_multi.mp4?1:handler=ExplicitTrack1?2:remove",
            "test_cli_eq_out.mp4", NULL ) < 0 )
        goto cleanup;
    if( assert_remuxer_equivalent_ex(
            "-i test_cli_eq_in_multi.mp4?1:handler=ImplicitTrack1?2:remove",
            "-i test_cli_eq_in_multi.mp4?handler=ImplicitTrack1?2:remove",
            "test_cli_eq_out.mp4", NULL ) < 0 )
        goto cleanup;

    /* Case 16: Track seeking with ?1:seek= and ?1:safe-seek= (consider-rap) on B-frame H.264 + AAC container */
    if( assert_remuxer_equivalent(
            "-i test_cli_eq_in_v.mp4?1:seek=2",
            "test_cli_eq_out.mp4", NULL ) < 0 )
        goto cleanup;
    if( assert_remuxer_equivalent(
            "-i test_cli_eq_in_multi.mp4?1:safe-seek=3?2:seek=2",
            "test_cli_eq_out.mp4", NULL ) < 0 )
        goto cleanup;

    /* Case 17: M4A container remuxing preserving iTunes metadata without duplicate encoding tool + reference/Nero chapters with BOM */
    if( assert_remuxer_equivalent(
            "--chapter test_cli_eq_chapters.txt --chpl-with-bom --chapter-track 1 -i test_cli_eq_in_meta.m4a",
            "test_cli_eq_out.m4a", NULL ) < 0 )
        goto cleanup;

    /* Case 18: QuickTime .mov container remuxing preserving pure 'qt  ' brand + reference chapter track + --compact-size-table */
    if( assert_remuxer_equivalent(
            "--chapter test_cli_eq_chapters.txt --chapter-track 2 -i test_cli_eq_in_qt.mov",
            "test_cli_eq_out.mov", NULL ) < 0 )
        goto cleanup;
    if( assert_remuxer_equivalent(
            "--compact-size-table -i test_cli_eq_in_v.mp4",
            "test_cli_eq_out.mp4", NULL ) < 0 )
        goto cleanup;

    /* Case 19: Fragmented MP4 remuxing (--fragment, --min-frag-duration, --last-frag-merge-thresh) + seek in fragmented mode */
    if( assert_remuxer_equivalent(
            "--fragment 1 --min-frag-duration 0.02 --last-frag-merge-thresh 0.5 -i test_cli_eq_in_v.mp4?1:safe-seek=2 -i test_cli_eq_in_a.mp4",
            "test_cli_eq_out.mp4", NULL ) < 0 )
        goto cleanup;

    /* Case 20: MPEG-DASH self-initializing (--dash 0) and segmented (--dash 1, verifying both init and media segment _1.mp4) */
    if( assert_remuxer_equivalent(
            "--fragment 1 --dash 0 -i test_cli_eq_in_v.mp4 -i test_cli_eq_in_a.mp4",
            "test_cli_eq_out.mp4", NULL ) < 0 )
        goto cleanup;
    if( assert_remuxer_equivalent(
            "--fragment 1 --dash 1 -i test_cli_eq_in_v.mp4 -i test_cli_eq_in_a.mp4",
            "test_cli_eq_out.mp4", "test_cli_eq_out_1.mp4" ) < 0 )
        goto cleanup;

    /* Case 21: Negative test — invalid track number rejected by both remuxer and lshmuxer */
    if( assert_remuxer_both_fail( "-i test_cli_eq_in_v.mp4?9:disable", "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* 5. Differential 3-Way Equivalence Assertions (timelineeditor vs lshmuxer positional vs lshmuxer -i input?opt) */

    /* Case 22: Timecode v2 (VFR + non-zero initial timestamp offset -> Empty Edit + composition_delay + ring-buffer DTS) */
    if( assert_timelineeditor_3way_equivalent(
            "--timecode test_cli_eq_tcv2.txt",
            f_in_v,
            "timecode=test_cli_eq_tcv2.txt",
            "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 23: Timecode v1 (Assume + VFR range + comments/blank lines) + --dts-compression */
    if( assert_timelineeditor_3way_equivalent(
            "--timecode test_cli_eq_tcv1.txt --dts-compression",
            f_in_v,
            "timecode=test_cli_eq_tcv1.txt,dts-compression",
            "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 24: --media-timescale + --media-timebase + --skip + --delay (rational & integer) without timecode file */
    if( assert_timelineeditor_3way_equivalent(
            "--media-timescale 30000 --media-timebase 1001 --skip 1001/30000 --delay 2002/30000",
            f_in_v,
            "media-timescale=30000,media-timebase=1001,skip=1001/30000,delay=2002/30000",
            "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 25: --dts-compression + --media-timescale (disables dts_compression_multiplier and synthesizes sub-frame initial DTS) */
    if( assert_timelineeditor_3way_equivalent(
            "--dts-compression --media-timescale 24000 --skip 500 --delay 1000",
            f_in_v,
            "dts-compression,media-timescale=24000,skip=500,delay=1000",
            "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 26: Multi-track container (--track 1) with compact sample size table (stz2) and iTunes metadata preserved */
    if( assert_timelineeditor_3way_equivalent(
            "--track 1 --timecode test_cli_eq_tcv2.txt --dts-compression",
            f_in_multi,
            "1:timecode=test_cli_eq_tcv2.txt,dts-compression",
            "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 27: Positional no-option invocation (argc == 3: GCD timescale/timebase reduction while copying original elst) */
    if( assert_timelineeditor_3way_equivalent(
            NULL,
            f_in_v,
            NULL,
            "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 28: Negative test — out-of-range --track number rejected by both timelineeditor and lshmuxer */
    if( assert_timelineeditor_both_fail( "--track 99", f_in_v, "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* 6. Legacy Multi-Stage Pipeline (muxer -> timelineeditor [-> remuxer]) vs 1-Pass lshmuxer Equivalence */

    /* Case 29: Non-fragmented 3-stage pipeline (muxer -> timelineeditor -> remuxer) vs 1-pass lshmuxer
     * (raw B-frame H.264 + raw AAC + Timecode v2 + skip + delay + chapter) */
    if( assert_pipeline_vs_1pass_equivalent(
            f_h264_cfr,
            f_adts,
            "--timecode test_cli_eq_tcv2.txt --skip 25/1000 --delay 50/1000",
            "--chapter test_cli_eq_chapters.txt",
            "--chapter test_cli_eq_chapters.txt -i test_cli_eq_cfr.h264?timecode=test_cli_eq_tcv2.txt,skip=25/1000,delay=50/1000 -i test_cli_eq_audio.aac",
            "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 30: 3-stage pipeline (muxer -> timelineeditor -> remuxer) vs 1-pass lshmuxer
     * (raw B-frame H.264 + raw AAC + Timecode v1 + skip + delay + compact-size-table + max-chunk-duration) */
    if( assert_pipeline_vs_1pass_equivalent(
            f_h264_cfr,
            f_adts,
            "--timecode test_cli_eq_tcv1.txt --skip 10/1000 --delay 20/1000",
            "--compact-size-table --max-chunk-duration 100",
            "--compact-size-table --max-chunk-duration 100 -i test_cli_eq_cfr.h264?timecode=test_cli_eq_tcv1.txt,skip=10/1000,delay=20/1000 -i test_cli_eq_audio.aac",
            "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 31: Multi-stage pipeline (muxer -> timelineeditor --dts-compression) vs 1-pass lshmuxer
     * (raw B-frame H.264 + raw AAC + Timecode v2 + dts-compression + skip + delay) */
    if( assert_pipeline_vs_1pass_equivalent(
            f_h264_cfr,
            f_adts,
            "--timecode test_cli_eq_tcv2.txt --dts-compression --skip 25/1000 --delay 50/1000",
            NULL,
            "-i test_cli_eq_cfr.h264?timecode=test_cli_eq_tcv2.txt,dts-compression,skip=25/1000,delay=50/1000 -i test_cli_eq_audio.aac",
            "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 32: 3-stage pipeline (muxer -> timelineeditor --dts-compression -> remuxer) vs 1-pass lshmuxer
     * (verifies remuxer preserves exact mdhd->duration and stts tail delta without double-adding CTS-DTS span) */
    if( assert_pipeline_vs_1pass_equivalent(
            f_h264_cfr,
            f_adts,
            "--timecode test_cli_eq_tcv2.txt --dts-compression --skip 25/1000 --delay 50/1000",
            "--chapter test_cli_eq_chapters.txt",
            "--chapter test_cli_eq_chapters.txt -i test_cli_eq_cfr.h264?timecode=test_cli_eq_tcv2.txt,dts-compression,skip=25/1000,delay=50/1000 -i test_cli_eq_audio.aac",
            "test_cli_eq_out.mp4" ) < 0 )
        goto cleanup;

    /* Case 33: Multi-generation Remux idempotency & ISOBMFF timeline invariants (DTS <= CTS, sum(stts) == mdhd->duration) */
    if( assert_remux_idempotency_and_invariants( f_h264_cfr, f_adts, f_tcv2 ) < 0 )
        goto cleanup;

    /* Case 34: 3-stage hybrid VFR last-frame composition duration (D_comp) across Timecode v2 (N+1), Timecode v1 still-frame, and statistical outlier guard */
    if( assert_hybrid_vfr_last_duration( f_in_v ) < 0 )
        goto cleanup;

    /* Case 35: --shift-timeline (ctts v1 + cslg) B-frame VFR container Remux & re-edit idempotency across remuxer, timelineeditor, and lshmuxer */
    if( assert_shift_timeline_vfr_remux_idempotency( f_h264_cfr, f_tcv2 ) < 0 )
        goto cleanup;

    /* Case 36: Negative-POC (leading picture where decode sample 1 has CTS < decode sample 0) --shift-timeline VFR re-edit & multi-generation remux */
    if( assert_negative_poc_shift_timeline_vfr_reedit( f_tcv2 ) < 0 )
        goto cleanup;

    /* Case 37: Fragmented MP4 (moof/trun v1) Negative-POC + --shift-timeline multi-fragment generation, boxdumper --timestamp, and round-trip remux/VFR re-edit */
    if( assert_fragmented_negative_poc_trun_v1_roundtrip() < 0 )
        goto cleanup;

    rc = 0;

cleanup:
    remove( f_h264_cfr );
    remove( f_h264_15fps );
    remove( f_h264_vfr );
    remove( f_h264_sar );
    remove( f_hevc_hdr10 );
    remove( f_av1_ivf );
    remove( f_adts );
    remove( f_amr );
    remove( f_wave );
    remove( f_chap );
    remove( f_tcv1 );
    remove( f_tcv2 );
    remove( f_in_v );
    remove( f_in_a );
    remove( f_in_multi );
    remove( f_in_m4a );
    remove( f_in_mov );
    if( rc == 0 )
        printf( "[PASS] test_muxer_lshmuxer_cli_equivalence\n" );
    return rc;
}
