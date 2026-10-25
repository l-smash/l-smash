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

    for( int attempt = 0; attempt < 3; attempt++ )
    {
        lsmash_freep( &te_box );
        lsmash_freep( &lshp_box );
        lsmash_freep( &lsht_box );
        lsmash_freep( &te_ts );
        lsmash_freep( &lshp_ts );
        lsmash_freep( &lsht_ts );

        time_t t0 = time( NULL );

        /* 1. cli/timelineeditor [options] input output */
        remove( out_path );
        snprintf( cmd, sizeof(cmd), "%s %s %s %s >%s 2>&1",
                  CLI_TIMELINEEDITOR, pos_opts ? pos_opts : "", in_path, out_path, CLI_DEV_NULL );
        rc_te = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, out_path, te_box_path, CLI_DEV_NULL );
        rc_bd1 = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s 2>%s", CLI_BOXDUMPER, out_path, te_ts_path, CLI_DEV_NULL );
        rc_bd2 = system( cmd );

        /* 2. cli/lshmuxer [options] input output (positional mode) */
        remove( out_path );
        snprintf( cmd, sizeof(cmd), "%s %s %s %s >%s 2>&1",
                  CLI_LSHMUXER, pos_opts ? pos_opts : "", in_path, out_path, CLI_DEV_NULL );
        rc_lshp = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, out_path, lshp_box_path, CLI_DEV_NULL );
        rc_bd3 = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s 2>%s", CLI_BOXDUMPER, out_path, lshp_ts_path, CLI_DEV_NULL );
        rc_bd4 = system( cmd );

        /* 3. cli/lshmuxer -i input?track_opts -o output (track-option mode) */
        if( track_opts )
        {
            remove( out_path );
            snprintf( cmd, sizeof(cmd), "%s -i %s?%s -o %s >%s 2>&1",
                      CLI_LSHMUXER, in_path, track_opts, out_path, CLI_DEV_NULL );
            rc_lsht = system( cmd );
            snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, out_path, lsht_box_path, CLI_DEV_NULL );
            rc_bd5 = system( cmd );
            snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s 2>%s", CLI_BOXDUMPER, out_path, lsht_ts_path, CLI_DEV_NULL );
            rc_bd6 = system( cmd );
        }

        time_t t1 = time( NULL );

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

    for( int attempt = 0; attempt < 3; attempt++ )
    {
        lsmash_freep( &pipe_box );
        lsmash_freep( &lsh_box );
        lsmash_freep( &pipe_ts );
        lsmash_freep( &lsh_ts );

        time_t t0 = time( NULL );

        remove( s1_mp4 );
        remove( s2_mp4 );
        remove( out_path );

        snprintf( cmd, sizeof(cmd), "%s -i %s -i %s -o %s >%s 2>&1", CLI_MUXER, raw_v, raw_a, s1_mp4, CLI_DEV_NULL );
        rc_m1 = system( cmd );
        if( remux_opts )
        {
            snprintf( cmd, sizeof(cmd), "%s --track 1 %s %s %s >%s 2>&1", CLI_TIMELINEEDITOR, te_opts, s1_mp4, s2_mp4, CLI_DEV_NULL );
            rc_te = system( cmd );
            snprintf( cmd, sizeof(cmd), "%s %s -i %s -o %s >%s 2>&1",
                      CLI_REMUXER, remux_opts, s2_mp4, out_path, CLI_DEV_NULL );
            rc_rem = system( cmd );
        }
        else
        {
            snprintf( cmd, sizeof(cmd), "%s --track 1 %s %s %s >%s 2>&1", CLI_TIMELINEEDITOR, te_opts, s1_mp4, out_path, CLI_DEV_NULL );
            rc_te  = system( cmd );
            rc_rem = 0;
        }
        snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, out_path, pipe_box_p, CLI_DEV_NULL );
        rc_bd1 = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s 2>%s", CLI_BOXDUMPER, out_path, pipe_ts_p, CLI_DEV_NULL );
        rc_bd2 = system( cmd );

        remove( out_path );
        snprintf( cmd, sizeof(cmd), "%s %s -o %s >%s 2>&1", CLI_LSHMUXER, lsh_1pass_args, out_path, CLI_DEV_NULL );
        rc_lsh = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --box %s > %s 2>%s", CLI_BOXDUMPER, out_path, lsh_box_p, CLI_DEV_NULL );
        rc_bd3 = system( cmd );
        snprintf( cmd, sizeof(cmd), "%s --timestamp %s > %s 2>%s", CLI_BOXDUMPER, out_path, lsh_ts_p, CLI_DEV_NULL );
        rc_bd4 = system( cmd );

        time_t t1 = time( NULL );

        pipe_box = read_text_file( pipe_box_p, &pipe_box_len );
        lsh_box  = read_text_file( lsh_box_p,  &lsh_box_len );
        pipe_ts  = read_text_file( pipe_ts_p,  &pipe_ts_len );
        lsh_ts   = read_text_file( lsh_ts_p,   &lsh_ts_len );

        if( t0 == t1 || (pipe_box && lsh_box && pipe_box_len == lsh_box_len && memcmp( pipe_box, lsh_box, pipe_box_len ) == 0) )
            break;
    }

    remove( s1_mp4 );
    remove( s2_mp4 );
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
