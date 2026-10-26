/*****************************************************************************
 * test_raw_importers_timeline.c
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

#include "common/internal.h"

#include "test_common.h"

/*---------------------------------------------------------------------------
 * Test 18: Fake Box / Timeline flow of raw streams through lsmash_read_file(), and rejection of data that is not a stream
 *---------------------------------------------------------------------------*/
int test_read_file_fake_box_and_raw_stream_acceptance( void )
{
    const char *wav_path = "tests/tmp_test_fake_box.wav";
    const char *h264_path = "tests/tmp_test_unsupported.h264";

    /* WAVE: mono 16-bit PCM, 8000 Hz, 20 frames. The importer builds a fake moov and trak. */
    uint8_t wav[44 + 40];
    {
        static const uint8_t head[44] =
        {
            'R', 'I', 'F', 'F', 76, 0, 0, 0, 'W', 'A', 'V', 'E',
            'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
            0x40, 0x1F, 0, 0, 0x80, 0x3E, 0, 0, 2, 0, 16, 0,
            'd', 'a', 't', 'a', 40, 0, 0, 0
        };
        memcpy( wav, head, sizeof(head) );
        for( int i = 0; i < 40; i++ )
            wav[44 + i] = (uint8_t)(i * 7 + 1);
    }
    TEST_ASSERT( write_test_file( wav_path, wav, sizeof(wav) ) == 0, "18a: write wav" );

    lsmash_root_t *root = lsmash_create_root();
    TEST_ASSERT( root != NULL, "18a: create root" );
    lsmash_file_parameters_t param = { 0 };
    TEST_ASSERT( lsmash_open_file( wav_path, 1, &param ) == 0, "18a: open wav" );
    lsmash_file_t *file = lsmash_set_file( root, &param );
    TEST_ASSERT( file != NULL, "18a: set wav file" );
    TEST_ASSERT( lsmash_read_file( file, &param ) >= 0, "18a: read_file succeeds on WAVE via fake boxes" );

    lsmash_movie_parameters_t movie_param;
    TEST_ASSERT( lsmash_get_movie_parameters( root, &movie_param ) == 0, "18a: movie parameters from fake moov" );
    TEST_ASSERT( movie_param.number_of_tracks == 1, "18a: one fake track" );
    uint32_t track_ID = lsmash_get_track_ID( root, 1 );
    TEST_ASSERT( track_ID != 0, "18a: fake track ID" );
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_get_summary( root, track_ID, 1 );
    TEST_ASSERT( summary != NULL, "18a: summary from fake track" );
    if( summary )
    {
        TEST_ASSERT( summary->channels == 1 && summary->sample_size == 16 && summary->frequency == 8000,
                     "18a: summary is mono 16-bit 8kHz" );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
    }
    TEST_ASSERT( lsmash_construct_timeline( root, track_ID ) == 0, "18a: construct timeline" );
    uint32_t count = lsmash_get_sample_count_in_media_timeline( root, track_ID );
    TEST_ASSERT( count > 0, "18a: timeline has samples" );
    uint32_t total = 0;
    for( uint32_t i = 1; i <= count; i++ )
    {
        lsmash_sample_t *samp = lsmash_get_sample_from_media_timeline( root, track_ID, i );
        TEST_ASSERT( samp != NULL, "18a: get sample" );
        if( !samp )
            break;
        for( uint32_t b = 0; b < samp->length && total < 40; b++, total++ )
            if( samp->data[b] != wav[44 + total] )
                TEST_ASSERT( 0, "18a: PCM payload matches" );
        lsmash_delete_sample( samp );
    }
    TEST_ASSERT( total == 40, "18a: all 40 payload bytes read" );
    lsmash_destruct_timeline( root, track_ID );
    lsmash_close_file( &param );
    lsmash_destroy_root( root );
    remove( wav_path );

    /* Raw H.264 Annex B: every registered raw stream importer builds the fake boxes, so that lsmash_read_file() accepts the stream. */
    uint8_t  h264[256] = { 0 };
    uint32_t h264_length = 0;
    {
        static const uint8_t start_code[4] = { 0, 0, 0, 1 };
        uint8_t           nal[64];
        test_bit_writer_t bw;
        /* Sequence Parameter Set: Baseline profile, 64x64, picture order count type 2 */
        memset( nal, 0, sizeof(nal) );
        nal[0] = 0x67;
        bw = (test_bit_writer_t){ nal + 1, 0 };
        bw_put( &bw, 8, 66 );       /* profile_idc */
        bw_put( &bw, 8, 0xC0 );     /* constraint_set0_flag and constraint_set1_flag */
        bw_put( &bw, 8, 30 );       /* level_idc */
        bw_put_ue( &bw, 0 );        /* seq_parameter_set_id */
        bw_put_ue( &bw, 0 );        /* log2_max_frame_num_minus4 */
        bw_put_ue( &bw, 2 );        /* pic_order_cnt_type */
        bw_put_ue( &bw, 1 );        /* max_num_ref_frames */
        bw_put( &bw, 1, 0 );        /* gaps_in_frame_num_value_allowed_flag */
        bw_put_ue( &bw, 3 );        /* pic_width_in_mbs_minus1 */
        bw_put_ue( &bw, 3 );        /* pic_height_in_map_units_minus1 */
        bw_put( &bw, 1, 1 );        /* frame_mbs_only_flag */
        bw_put( &bw, 1, 1 );        /* direct_8x8_inference_flag */
        bw_put( &bw, 1, 0 );        /* frame_cropping_flag */
        bw_put( &bw, 1, 0 );        /* vui_parameters_present_flag */
        bw_put_trailing( &bw );
        append_bytes( h264, &h264_length, start_code, 4 );
        append_bytes( h264, &h264_length, nal, 1 + bw.pos / 8 );
        /* Picture Parameter Set */
        memset( nal, 0, sizeof(nal) );
        nal[0] = 0x68;
        bw = (test_bit_writer_t){ nal + 1, 0 };
        bw_put_ue( &bw, 0 );        /* pic_parameter_set_id */
        bw_put_ue( &bw, 0 );        /* seq_parameter_set_id */
        bw_put( &bw, 1, 0 );        /* entropy_coding_mode_flag */
        bw_put( &bw, 1, 0 );        /* bottom_field_pic_order_in_frame_present_flag */
        bw_put_ue( &bw, 0 );        /* num_slice_groups_minus1 */
        bw_put_ue( &bw, 0 );        /* num_ref_idx_l0_default_active_minus1 */
        bw_put_ue( &bw, 0 );        /* num_ref_idx_l1_default_active_minus1 */
        bw_put( &bw, 1, 0 );        /* weighted_pred_flag */
        bw_put( &bw, 2, 0 );        /* weighted_bipred_idc */
        bw_put( &bw, 1, 1 );        /* pic_init_qp_minus26 (se: 0) */
        bw_put( &bw, 1, 1 );        /* pic_init_qs_minus26 (se: 0) */
        bw_put( &bw, 1, 1 );        /* chroma_qp_index_offset (se: 0) */
        bw_put( &bw, 1, 1 );        /* deblocking_filter_control_present_flag */
        bw_put( &bw, 1, 0 );        /* constrained_intra_pred_flag */
        bw_put( &bw, 1, 0 );        /* redundant_pic_cnt_present_flag */
        bw_put_trailing( &bw );
        append_bytes( h264, &h264_length, start_code, 4 );
        append_bytes( h264, &h264_length, nal, 1 + bw.pos / 8 );
        /* IDR slice, then a non-IDR slice of the next picture */
        for( int i = 0; i < 2; i++ )
        {
            memset( nal, 0, sizeof(nal) );
            nal[0] = i == 0 ? 0x65 : 0x41;
            bw = (test_bit_writer_t){ nal + 1, 0 };
            bw_put_ue( &bw, 0 );                    /* first_mb_in_slice */
            bw_put_ue( &bw, i == 0 ? 7 : 5 );       /* slice_type: I or P */
            bw_put_ue( &bw, 0 );                    /* pic_parameter_set_id */
            bw_put( &bw, 4, (uint32_t)i );          /* frame_num */
            if( i == 0 )
            {
                bw_put_ue( &bw, 0 );                /* idr_pic_id */
                bw_put( &bw, 1, 0 );                /* no_output_of_prior_pics_flag */
                bw_put( &bw, 1, 0 );                /* long_term_reference_flag */
            }
            else
            {
                bw_put( &bw, 1, 0 );                /* num_ref_idx_active_override_flag */
                bw_put( &bw, 1, 0 );                /* ref_pic_list_modification_flag_l0 */
                bw_put( &bw, 1, 0 );                /* adaptive_ref_pic_marking_mode_flag */
            }
            bw_put( &bw, 1, 1 );                    /* slice_qp_delta (se: 0) */
            bw_put_ue( &bw, 1 );                    /* disable_deblocking_filter_idc */
            bw_put( &bw, 8, 0xA5 );                 /* slice data */
            bw_put_trailing( &bw );
            append_bytes( h264, &h264_length, start_code, 4 );
            append_bytes( h264, &h264_length, nal, 1 + bw.pos / 8 );
        }
    }
    TEST_ASSERT( write_test_file( h264_path, h264, h264_length ) == 0, "18b: write h264" );
    root = lsmash_create_root();
    memset( &param, 0, sizeof(param) );
    TEST_ASSERT( lsmash_open_file( h264_path, 1, &param ) == 0, "18b: open h264" );
    file = lsmash_set_file( root, &param );
    TEST_ASSERT( file != NULL, "18b: set h264 file" );
    TEST_ASSERT( lsmash_read_file( file, &param ) >= 0, "18b: raw H.264 stream is accepted through fake boxes" );
    track_ID = lsmash_get_track_ID( root, 1 );
    TEST_ASSERT( track_ID != 0 && lsmash_count_summary( root, track_ID ) == 1, "18b: one fake H.264 track with one sample entry" );
    TEST_ASSERT( lsmash_construct_timeline( root, track_ID ) == 0 && lsmash_get_sample_count_in_media_timeline( root, track_ID ) == 2,
                 "18b: the H.264 timeline has the two pictures" );
    lsmash_destruct_timeline( root, track_ID );
    lsmash_close_file( &param );
    lsmash_destroy_root( root );

    /* Data that no importer recognizes is rejected with an error, and nothing is left behind by the failed probes.
     * The cases are: random noise without any start code, a stream with a start code but without any picture,
     * a truncated stream that ends in the middle of the parameter sets, and zero bytes only. */
    {
        uint8_t noise[300];
        uint32_t seed = 0x2545F491u;
        for( size_t i = 0; i < sizeof(noise); i++ )
        {
            seed = seed * 1664525u + 1013904223u;
            noise[i] = (uint8_t)(0x10 | (seed >> 24));     /* never zero, so that no start code can appear */
        }
        uint8_t zeros[64] = { 0 };
        uint8_t zeros_4k[4096] = { 0 };
        uint8_t parameter_sets_only[64];
        uint32_t truncated_length = 0;
        {
            /* The head of the H.264 stream written above: start code, SPS and PPS, but no slice. */
            static const uint8_t sps_pps[] = { 0, 0, 0, 1, 0x67, 66, 0xC0, 30, 0xF4, 0x40, 0x80 };
            memcpy( parameter_sets_only, sps_pps, sizeof(sps_pps) );
            truncated_length = sizeof(sps_pps);
        }
        /* H.264 Annex-B with missing SPS (PPS + IDR slice only) and corrupted SPS */
        uint8_t  h264_no_sps[128]      = { 0 };
        uint32_t h264_no_sps_len       = 0;
        uint8_t  h264_corrupt_sps[128] = { 0 };
        uint32_t h264_corrupt_sps_len  = 0;
        uint8_t  h264_sei_only[32]     = { 0, 0, 0, 1, 0x06, 0x05, 0x01, 0x00, 0x80 };
        {
            /* Find second start code in h264[] (which is the PPS) to build PPS + slice without SPS */
            uint32_t pps_offset = 4;
            while( pps_offset + 4 < h264_length
                && !(h264[pps_offset] == 0 && h264[pps_offset + 1] == 0 && h264[pps_offset + 2] == 0 && h264[pps_offset + 3] == 1) )
                ++pps_offset;
            h264_no_sps_len = h264_length - pps_offset;
            memcpy( h264_no_sps, h264 + pps_offset, h264_no_sps_len );

            /* Corrupted SPS (truncated 1-byte SPS header 0x67 followed by PPS + slices) */
            static const uint8_t bad_sps[] = { 0, 0, 0, 1, 0x67, 0xFF };
            append_bytes( h264_corrupt_sps, &h264_corrupt_sps_len, bad_sps, sizeof(bad_sps) );
            append_bytes( h264_corrupt_sps, &h264_corrupt_sps_len, h264 + pps_offset, h264_no_sps_len );
        }
        /* HEVC Annex-B negative streams:
         * 1) Missing SPS (PPS + IDR slice only)
         * 2) Corrupted SPS + PPS + IDR slice
         * 3) Valid SPS + PPS + IDR slice but missing VPS (triggers nalu_imp_hevc_setup_first_summary failure after hvcC_list append) */
        uint8_t  hevc_no_sps[128]      = { 0 };
        uint32_t hevc_no_sps_len       = 0;
        uint8_t  hevc_corrupt_sps[128] = { 0 };
        uint32_t hevc_corrupt_sps_len  = 0;
        uint8_t  hevc_no_vps[256]      = { 0 };
        uint32_t hevc_no_vps_len       = 0;
        {
            static const uint8_t sc[4] = { 0, 0, 0, 1 };
            /* Build minimal HEVC SPS RBSP and convert to EBSP (nal_unit_type = 33 -> header 0x42, 0x01) */
            uint8_t sps_rbsp[64] = { 0 };
            test_bit_writer_t bw = { sps_rbsp, 0 };
            bw_put( &bw, 4, 0 );            /* sps_video_parameter_set_id = 0 */
            bw_put( &bw, 3, 0 );            /* sps_max_sub_layers_minus1 = 0 */
            bw_put( &bw, 1, 1 );            /* sps_temporal_id_nesting_flag = 1 */
            bw_put( &bw, 2, 0 );            /* general_profile_space = 0 */
            bw_put( &bw, 1, 0 );            /* general_tier_flag = 0 */
            bw_put( &bw, 5, 1 );            /* general_profile_idc = 1 */
            bw_put( &bw, 32, 0x60000000u ); /* general_profile_compatibility_flags */
            bw_put( &bw, 1, 1 );            /* general_progressive_source_flag */
            bw_put( &bw, 1, 0 );            /* general_interlaced_source_flag */
            bw_put( &bw, 1, 1 );            /* general_non_packed_constraint_flag */
            bw_put( &bw, 1, 1 );            /* general_frame_only_constraint_flag */
            bw_put( &bw, 32, 0 );           /* general_reserved_zero_44bits (hi) */
            bw_put( &bw, 12, 0 );           /* general_reserved_zero_44bits (lo) */
            bw_put( &bw, 8, 93 );           /* general_level_idc = 93 */
            bw_put_ue( &bw, 0 );            /* sps_seq_parameter_set_id = 0 */
            bw_put_ue( &bw, 1 );            /* chroma_format_idc = 1 */
            bw_put_ue( &bw, 64 );           /* pic_width_in_luma_samples = 64 */
            bw_put_ue( &bw, 64 );           /* pic_height_in_luma_samples = 64 */
            bw_put( &bw, 1, 0 );            /* conformance_window_flag = 0 */
            bw_put_ue( &bw, 0 );            /* bit_depth_luma_minus8 = 0 */
            bw_put_ue( &bw, 0 );            /* bit_depth_chroma_minus8 = 0 */
            bw_put_ue( &bw, 0 );            /* log2_max_pic_order_cnt_lsb_minus4 = 0 */
            bw_put( &bw, 1, 0 );            /* sps_sub_layer_ordering_info_present_flag = 0 */
            bw_put_ue( &bw, 1 );            /* sps_max_dec_pic_buffering_minus1[0] = 1 */
            bw_put_ue( &bw, 0 );            /* sps_max_num_reorder_pics[0] = 0 */
            bw_put_ue( &bw, 0 );            /* sps_max_latency_increase_plus1[0] = 0 */
            bw_put_ue( &bw, 0 );            /* log2_min_luma_coding_block_size_minus3 = 0 */
            bw_put_ue( &bw, 3 );            /* log2_diff_max_min_luma_coding_block_size = 3 */
            bw_put_ue( &bw, 0 );            /* log2_min_transform_block_size_minus2 = 0 */
            bw_put_ue( &bw, 3 );            /* log2_diff_max_min_transform_block_size = 3 */
            bw_put_ue( &bw, 0 );            /* max_transform_hierarchy_depth_inter = 0 */
            bw_put_ue( &bw, 0 );            /* max_transform_hierarchy_depth_intra = 0 */
            bw_put( &bw, 1, 0 );            /* scaling_list_enabled_flag = 0 */
            bw_put( &bw, 1, 0 );            /* amp_enabled_flag = 0 */
            bw_put( &bw, 1, 0 );            /* sample_adaptive_offset_enabled_flag = 0 */
            bw_put( &bw, 1, 0 );            /* pcm_enabled_flag = 0 */
            bw_put_ue( &bw, 0 );            /* num_short_term_ref_pic_sets = 0 */
            bw_put( &bw, 1, 0 );            /* long_term_ref_pics_present_flag = 0 */
            bw_put( &bw, 1, 0 );            /* sps_temporal_mvp_enabled_flag = 0 */
            bw_put( &bw, 1, 0 );            /* strong_intra_smoothing_enabled_flag = 0 */
            bw_put( &bw, 1, 0 );            /* vui_parameters_present_flag = 0 */
            bw_put( &bw, 1, 0 );            /* sps_extension_present_flag = 0 */
            bw_put_trailing( &bw );
            uint32_t sps_rbsp_len = bw.pos >> 3;
            uint8_t  sps_nal[80]  = { 0x42, 0x01 };
            uint32_t sps_nal_len  = 2;
            uint32_t zero_run     = 0;
            for( uint32_t i = 0; i < sps_rbsp_len; i++ )
            {
                if( zero_run == 2 && sps_rbsp[i] <= 0x03 )
                {
                    sps_nal[sps_nal_len++] = 0x03;
                    zero_run = 0;
                }
                sps_nal[sps_nal_len++] = sps_rbsp[i];
                zero_run = (sps_rbsp[i] == 0x00) ? (zero_run + 1) : 0;
            }

            /* Build minimal HEVC PPS (nal_unit_type = 34 -> header 0x44, 0x01) */
            uint8_t pps_nal[32] = { 0x44, 0x01 };
            bw = (test_bit_writer_t){ pps_nal + 2, 0 };
            bw_put_ue( &bw, 0 );            /* pps_pic_parameter_set_id = 0 */
            bw_put_ue( &bw, 0 );            /* pps_seq_parameter_set_id = 0 */
            bw_put( &bw, 1, 0 );            /* dependent_slice_segments_enabled_flag = 0 */
            bw_put( &bw, 1, 0 );            /* output_flag_present_flag = 0 */
            bw_put( &bw, 3, 0 );            /* num_extra_slice_header_bits = 0 */
            bw_put( &bw, 1, 0 );            /* sign_data_hiding_enabled_flag = 0 */
            bw_put( &bw, 1, 0 );            /* cabac_init_present_flag = 0 */
            bw_put_ue( &bw, 0 );            /* num_ref_idx_l0_default_active_minus1 = 0 */
            bw_put_ue( &bw, 0 );            /* num_ref_idx_l1_default_active_minus1 = 0 */
            bw_put_ue( &bw, 0 );            /* init_qp_minus26 = 0 */
            bw_put( &bw, 1, 0 );            /* constrained_intra_pred_flag = 0 */
            bw_put( &bw, 1, 0 );            /* transform_skip_enabled_flag = 0 */
            bw_put( &bw, 1, 0 );            /* cu_qp_delta_enabled_flag = 0 */
            bw_put_ue( &bw, 0 );            /* pps_cb_qp_offset = 0 */
            bw_put_ue( &bw, 0 );            /* pps_cr_qp_offset = 0 */
            bw_put( &bw, 1, 0 );            /* pps_slice_chroma_qp_offsets_present_flag = 0 */
            bw_put( &bw, 1, 0 );            /* weighted_pred_flag = 0 */
            bw_put( &bw, 1, 0 );            /* weighted_bipred_flag = 0 */
            bw_put( &bw, 1, 0 );            /* transquant_bypass_enabled_flag = 0 */
            bw_put( &bw, 1, 0 );            /* tiles_enabled_flag = 0 */
            bw_put( &bw, 1, 0 );            /* entropy_coding_sync_enabled_flag = 0 */
            bw_put( &bw, 1, 0 );            /* pps_loop_filter_across_slices_enabled_flag = 0 */
            bw_put( &bw, 1, 0 );            /* deblocking_filter_control_present_flag = 0 */
            bw_put( &bw, 1, 0 );            /* pps_scaling_list_data_present_flag = 0 */
            bw_put( &bw, 1, 0 );            /* lists_modification_present_flag = 0 */
            bw_put_ue( &bw, 0 );            /* log2_parallel_merge_level_minus2 = 0 */
            bw_put( &bw, 1, 0 );            /* slice_segment_header_extension_present_flag = 0 */
            bw_put( &bw, 1, 0 );            /* pps_extension_present_flag = 0 */
            bw_put_trailing( &bw );
            uint32_t pps_nal_len = 2 + (bw.pos >> 3);

            /* Build minimal HEVC IDR_W_RADL slice (nal_unit_type = 19 -> header 0x26, 0x01) */
            uint8_t idr_nal[16] = { 0x26, 0x01 };
            bw = (test_bit_writer_t){ idr_nal + 2, 0 };
            bw_put( &bw, 1, 1 );            /* first_slice_segment_in_pic_flag = 1 */
            bw_put( &bw, 1, 0 );            /* no_output_of_prior_pics_flag = 0 */
            bw_put_ue( &bw, 0 );            /* slice_pic_parameter_set_id = 0 */
            bw_put_ue( &bw, 2 );            /* slice_type = 2 (I) */
            bw_put_trailing( &bw );
            uint32_t idr_nal_len = 2 + (bw.pos >> 3);
            idr_nal[idr_nal_len++] = 0x80;

            /* 1) HEVC missing SPS (PPS + IDR slice) */
            append_bytes( hevc_no_sps, &hevc_no_sps_len, sc, 4 );
            append_bytes( hevc_no_sps, &hevc_no_sps_len, pps_nal, pps_nal_len );
            append_bytes( hevc_no_sps, &hevc_no_sps_len, sc, 4 );
            append_bytes( hevc_no_sps, &hevc_no_sps_len, idr_nal, idr_nal_len );

            /* 2) HEVC corrupted SPS + PPS + IDR slice */
            static const uint8_t bad_hevc_sps[] = { 0, 0, 0, 1, 0x42, 0x01, 0x01, 0xFF };
            append_bytes( hevc_corrupt_sps, &hevc_corrupt_sps_len, bad_hevc_sps, sizeof(bad_hevc_sps) );
            append_bytes( hevc_corrupt_sps, &hevc_corrupt_sps_len, hevc_no_sps, hevc_no_sps_len );

            /* 3) HEVC with valid SPS + PPS + IDR slice but missing VPS */
            append_bytes( hevc_no_vps, &hevc_no_vps_len, sc, 4 );
            append_bytes( hevc_no_vps, &hevc_no_vps_len, sps_nal, sps_nal_len );
            append_bytes( hevc_no_vps, &hevc_no_vps_len, hevc_no_sps, hevc_no_sps_len );
        }
        /* VC-1 and IVF (AV1) 0-sample / truncated header negative streams:
         * 1) VC-1 Sequence Header (0x0F) + Entry Point (0x0E) without any Frame EBDU (0x0D)
         * 2) VC-1 truncated Sequence Header
         * 3) IVF 32-byte global header + 12-byte frame header + Sequence Header OBU without Frame OBU (0 output sample bytes)
         * 4) IVF 32-byte global header + 12-byte frame header with truncated frame payload */
        uint8_t  vc1_seq_ep_no_frame[64] = { 0 };
        uint32_t vc1_seq_ep_no_frame_len = 0;
        uint8_t  vc1_truncated_hdr[8]    = { 0x00, 0x00, 0x01, 0x0F, 0xDF };
        uint8_t  ivf_seq_no_frame[128]   = { 0 };
        uint32_t ivf_seq_no_frame_len    = 0;
        uint8_t  ivf_truncated_frame[64] = { 0 };
        uint32_t ivf_truncated_frame_len = 0;
        {
            static const uint8_t vc1_seq_sc[4] = { 0x00, 0x00, 0x01, 0x0F };
            static const uint8_t vc1_ep_sc[4]  = { 0x00, 0x00, 0x01, 0x0E };
            uint8_t seq_rbdu[16] = { 0 };
            test_bit_writer_t bw = { seq_rbdu, 0 };
            bw_put( &bw, 2, 3 );        /* profile = 3 (Advanced) */
            bw_put( &bw, 3, 2 );        /* level = 2 */
            bw_put( &bw, 2, 1 );        /* colordiff_format = 1 */
            bw_put( &bw, 9, 0x1FF );    /* frmrtq_postproc, bitrtq_postproc, postproc_flag (non-zero to avoid 0x0000) */
            bw_put( &bw, 12, 0x11F );   /* max_coded_width */
            bw_put( &bw, 12, 0x11F );   /* max_coded_height */
            bw_put( &bw, 6, 0x01 );     /* pulldown..psf */
            bw_put( &bw, 1, 0 );        /* display_ext = 0 */
            bw_put_trailing( &bw );
            append_bytes( vc1_seq_ep_no_frame, &vc1_seq_ep_no_frame_len, vc1_seq_sc, 4 );
            append_bytes( vc1_seq_ep_no_frame, &vc1_seq_ep_no_frame_len, seq_rbdu, bw.pos >> 3 );

            uint8_t ep_rbdu[8] = { 0 };
            bw = (test_bit_writer_t){ ep_rbdu, 0 };
            bw_put( &bw, 1, 0 );        /* broken_link_flag = 0 */
            bw_put( &bw, 1, 1 );        /* closed_entry_point = 1 */
            bw_put( &bw, 4, 0 );        /* panscan..fastuvmc */
            bw_put( &bw, 1, 0 );        /* extended_mv = 0 */
            bw_put( &bw, 6, 1 );        /* dquant..quantizer */
            bw_put( &bw, 1, 0 );        /* coded_size_flag = 0 */
            bw_put( &bw, 1, 0 );        /* range_mapy_flag = 0 */
            bw_put( &bw, 1, 0 );        /* range_mapuv_flag = 0 */
            bw_put_trailing( &bw );
            append_bytes( vc1_seq_ep_no_frame, &vc1_seq_ep_no_frame_len, vc1_ep_sc, 4 );
            append_bytes( vc1_seq_ep_no_frame, &vc1_seq_ep_no_frame_len, ep_rbdu, bw.pos >> 3 );

            synth_seq_cfg_t seq_cfg =
            {
                .seq_profile = 0, .seq_level_idx_0 = 4, .width = 64, .height = 64,
                .subsampling_x = 1, .subsampling_y = 1, .enable_order_hint = 1, .order_hint_bits_minus_1 = 6,
            };
            uint8_t sh_obu[64] = { 0 };
            uint32_t sh_len = build_seq_hdr_obu( sh_obu, &seq_cfg );
            memcpy( ivf_seq_no_frame, "DKIF", 4 );
            put_le16( ivf_seq_no_frame + 6, 32 );
            memcpy( ivf_seq_no_frame + 8, "AV01", 4 );
            put_le16( ivf_seq_no_frame + 12, 64 );
            put_le16( ivf_seq_no_frame + 14, 64 );
            put_le32( ivf_seq_no_frame + 16, 30 );
            put_le32( ivf_seq_no_frame + 20, 1 );
            put_le32( ivf_seq_no_frame + 24, 1 );
            put_le32( ivf_seq_no_frame + 32, sh_len );
            put_le64( ivf_seq_no_frame + 36, 0 );
            memcpy( ivf_seq_no_frame + 44, sh_obu, sh_len );
            ivf_seq_no_frame_len = 44 + sh_len;

            memcpy( ivf_truncated_frame, ivf_seq_no_frame, 44 );
            put_le32( ivf_truncated_frame + 32, 200 ); /* Claims 200-byte frame payload, but only 8 bytes follow */
            memcpy( ivf_truncated_frame + 44, sh_obu, 8 );
            ivf_truncated_frame_len = 44 + 8;
        }
        struct
        {
            const char    *label;
            const uint8_t *data;
            size_t         size;
        } cases[] =
        {
            { "18c: noise is rejected",                                 noise,               sizeof(noise)            },
            { "18c: a lone start code is rejected",                     h264,                4                        },
            { "18c: parameter sets without picture are rejected",       parameter_sets_only, truncated_length         },
            { "18c: zero bytes (64B) are rejected",                     zeros,               sizeof(zeros)            },
            { "18c: zero bytes (4KB) are rejected without hang",        zeros_4k,            sizeof(zeros_4k)         },
            { "18c: H.264 stream missing SPS is rejected safely",       h264_no_sps,         h264_no_sps_len          },
            { "18c: H.264 stream with corrupted SPS is rejected",       h264_corrupt_sps,    h264_corrupt_sps_len     },
            { "18c: H.264 SEI-only stream without slice is rejected",   h264_sei_only,       9                        },
            { "18c: HEVC stream missing SPS is rejected safely",        hevc_no_sps,         hevc_no_sps_len          },
            { "18c: HEVC stream with corrupted SPS is rejected",        hevc_corrupt_sps,    hevc_corrupt_sps_len     },
            { "18c: HEVC stream missing VPS is rejected without UAF",   hevc_no_vps,         hevc_no_vps_len          },
            { "18c: VC-1 SeqHdr + EntryPoint without Frame is rejected",vc1_seq_ep_no_frame, vc1_seq_ep_no_frame_len  },
            { "18c: VC-1 truncated Sequence Header is rejected",        vc1_truncated_hdr,   sizeof(vc1_truncated_hdr)},
            { "18c: IVF Sequence Header OBU without Frame is rejected", ivf_seq_no_frame,    ivf_seq_no_frame_len     },
            { "18c: IVF truncated frame payload is rejected",           ivf_truncated_frame, ivf_truncated_frame_len  },
        };
        for( size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++ )
        {
            const char *garbage_path = "tests/tmp_test_garbage.bin";
            TEST_ASSERT( write_test_file( garbage_path, cases[k].data, cases[k].size ) == 0, "18c: write garbage" );
            root = lsmash_create_root();
            memset( &param, 0, sizeof(param) );
            TEST_ASSERT( lsmash_open_file( garbage_path, 1, &param ) == 0, "18c: open garbage" );
            file = lsmash_set_file( root, &param );
            TEST_ASSERT( file != NULL, cases[k].label );
            TEST_ASSERT( lsmash_read_file( file, &param ) < 0, cases[k].label );
            lsmash_movie_parameters_t rejected_movie;
            TEST_ASSERT( lsmash_get_movie_parameters( root, &rejected_movie ) != 0 || rejected_movie.number_of_tracks == 0,
                         "18c: a rejected stream leaves no fake track behind" );
            lsmash_close_file( &param );
            lsmash_destroy_root( root );
            root = lsmash_create_root();
            TEST_ASSERT( lsmash_importer_open( root, garbage_path, "auto" ) == NULL, "18c: the sequential importer rejects it, too" );
            lsmash_destroy_root( root );
            remove( garbage_path );
        }
    }
    remove( h264_path );
    printf( "[PASS] test_read_file_fake_box_and_raw_stream_acceptance\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 19: FLAC and IVF (AV1) raw streams through lsmash_read_file() and the Media Timeline
 *---------------------------------------------------------------------------*/
static int check_timeline_sample_matches
(
    lsmash_root_t *root,
    uint32_t       track_ID,
    uint32_t       sample_number,
    uint64_t       expected_dts,
    const uint8_t *expected_data,
    uint32_t       expected_length,
    const char    *label
)
{
    lsmash_sample_t *sample = lsmash_get_sample_from_media_timeline( root, track_ID, sample_number );
    TEST_ASSERT( sample != NULL, label );
    TEST_ASSERT( sample->dts == expected_dts && sample->cts == expected_dts, label );
    TEST_ASSERT( sample->length == expected_length, label );
    TEST_ASSERT( sample->index == 1, label );
    TEST_ASSERT( memcmp( sample->data, expected_data, expected_length ) == 0, label );
    lsmash_delete_sample( sample );
    /* The info API and the timestamp APIs must agree with the sample itself. */
    lsmash_sample_t info = { 0 };
    uint64_t dts = UINT64_MAX;
    uint64_t cts = UINT64_MAX;
    TEST_ASSERT( lsmash_get_sample_info_from_media_timeline( root, track_ID, sample_number, &info ) == 0, label );
    TEST_ASSERT( info.dts == expected_dts && info.length == expected_length && info.index == 1, label );
    TEST_ASSERT( lsmash_get_dts_from_media_timeline( root, track_ID, sample_number, &dts ) == 0 && dts == expected_dts, label );
    TEST_ASSERT( lsmash_get_cts_from_media_timeline( root, track_ID, sample_number, &cts ) == 0 && cts == expected_dts, label );
    return 0;
}

int test_read_file_flac_ivf_timeline( void )
{
    /*---- FLAC: 96 kHz 24-bit stereo, 4 variable block size frames (the last one uses an uncommon 16-bit block size) ----*/
    const char *flac_path = "tests/tmp_test_fake_box.flac";
    uint8_t     frames[4][128];
    uint32_t    frame_sizes[4];
    uint32_t    block_sizes[4]   = { 4096, 4096, 4096, 1500 };
    uint64_t    sample_nums[4]   = { 0, 4096, 8192, 12288 };
    uint32_t    max_frame_size   = 0;
    for( int i = 0; i < 4; i++ )
    {
        frame_sizes[i] = build_synth_flac_frame( frames[i], 1, block_sizes[i], sample_nums[i], 32 );
        max_frame_size = LSMASH_MAX( max_frame_size, frame_sizes[i] );
    }
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        TEST_ASSERT( bs != NULL, "19a: create bs for flac" );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 'L', 'a', 'C' ) );
        lsmash_bs_put_byte( bs, 0x00 );
        lsmash_bs_put_be24( bs, 34 );
        lsmash_bs_put_be16( bs, 1024 );
        lsmash_bs_put_be16( bs, 4096 );
        lsmash_bs_put_be24( bs, 32 );
        lsmash_bs_put_be24( bs, 128 );
        lsmash_bs_put_be64( bs, ((uint64_t)96000 << 44) | ((uint64_t)1 << 41) | ((uint64_t)23 << 36) | (uint64_t)13788 );
        for( int i = 0; i < 16; i++ )
            lsmash_bs_put_byte( bs, (uint8_t)(0xA0 + i) );
        lsmash_bs_put_byte( bs, 0x84 );
        lsmash_bs_put_be24( bs, 12 );
        lsmash_bs_put_le32( bs, 4 );
        lsmash_bs_put_bytes( bs, 4, (void *)"L-SM" );
        lsmash_bs_put_le32( bs, 0 );
        for( int i = 0; i < 4; i++ )
            lsmash_bs_put_bytes( bs, frame_sizes[i], frames[i] );
        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( raw != NULL && write_test_file( flac_path, raw, raw_len ) == 0, "19a: write flac" );
        lsmash_free( raw );
    }
    {
        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( root != NULL && lsmash_open_file( flac_path, 1, &param ) == 0, "19a: open flac" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( file != NULL, "19a: set flac file" );
        TEST_ASSERT( lsmash_read_file( file, &param ) >= 0, "19a: read_file succeeds on FLAC via fake boxes" );
        lsmash_movie_parameters_t movie_param;
        TEST_ASSERT( lsmash_get_movie_parameters( root, &movie_param ) == 0 && movie_param.number_of_tracks == 1,
                     "19a: one fake flac track" );
        TEST_ASSERT( movie_param.timescale == 96000, "19a: movie timescale is the sample rate" );
        uint32_t track_ID = lsmash_get_track_ID( root, 1 );
        TEST_ASSERT( track_ID != 0, "19a: flac track ID" );
        lsmash_media_parameters_t media_param;
        TEST_ASSERT( lsmash_get_media_parameters( root, track_ID, &media_param ) == 0 && media_param.timescale == 96000,
                     "19a: media timescale is the sample rate" );
        lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_get_summary( root, track_ID, 1 );
        TEST_ASSERT( summary != NULL, "19a: summary from the fake fLaC entry" );
        if( summary )
        {
            TEST_ASSERT( lsmash_check_codec_type_identical( summary->sample_type, ISOM_CODEC_TYPE_FLAC_AUDIO ), "19a: fLaC sample entry" );
            TEST_ASSERT( summary->frequency == 96000 && summary->channels == 2 && summary->sample_size == 24,
                         "19a: summary is 96 kHz 2ch 24-bit" );
            lsmash_codec_specific_t *dfla = lsmash_get_codec_specific_data( (lsmash_summary_t *)summary, 1 );
            TEST_ASSERT( dfla != NULL && dfla->type == LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_FLAC, "19a: dfLa is attached" );
            lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        }
        TEST_ASSERT( lsmash_construct_timeline( root, track_ID ) == 0, "19a: construct flac timeline" );
        TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( root, track_ID ) == 4, "19a: 4 samples" );
        TEST_ASSERT( lsmash_get_max_sample_size_in_media_timeline( root, track_ID ) == max_frame_size, "19a: max sample size" );
        TEST_ASSERT( lsmash_get_media_duration_from_media_timeline( root, track_ID ) == 13788, "19a: media duration" );
        uint32_t last_delta = 0;
        TEST_ASSERT( lsmash_get_last_sample_delta_from_media_timeline( root, track_ID, &last_delta ) == 0 && last_delta == 1500,
                     "19a: last sample delta is the last block size" );
        /* Random order access exercises the cached DTS accumulation and the stream seek of every read. */
        static const int order[] = { 3, 0, 2, 1, 3, 0 };
        for( int k = 0; k < 6; k++ )
        {
            int i = order[k];
            TEST_ASSERT( check_timeline_sample_matches( root, track_ID, (uint32_t)(i + 1), sample_nums[i], frames[i], frame_sizes[i], "19a: flac sample" ) == 0, "19a: flac sample" );
        }
        TEST_ASSERT( lsmash_get_sample_from_media_timeline( root, track_ID, 5 ) == NULL, "19a: no sample past the end" );
        TEST_ASSERT( lsmash_check_sample_existence_in_media_timeline( root, track_ID, 4 ) == 1
                  && lsmash_check_sample_existence_in_media_timeline( root, track_ID, 5 ) == 0, "19a: sample existence" );
        TEST_ASSERT( lsmash_construct_timeline( root, track_ID ) == 0
                  && lsmash_get_sample_count_in_media_timeline( root, track_ID ) == 4, "19a: constructing twice is harmless" );
        lsmash_destruct_timeline( root, track_ID );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
    }
    /* The sequential reader used by the muxer must stay consistent with the Media Timeline that moved the stream. */
    {
        lsmash_root_t *root = lsmash_create_root();
        importer_t *imp = lsmash_importer_open( root, flac_path, "auto" );
        TEST_ASSERT( imp != NULL, "19b: open flac importer" );
        uint32_t track_ID = lsmash_get_track_ID( root, 1 );
        lsmash_sample_t *sample = NULL;
        for( int i = 0; i < 2; i++ )
        {
            TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &sample ) == 0 && sample != NULL, "19b: access unit before timeline" );
            TEST_ASSERT( sample && sample->length == frame_sizes[i] && memcmp( sample->data, frames[i], frame_sizes[i] ) == 0
                      && sample->dts == sample_nums[i], "19b: access unit payload and dts before timeline" );
            lsmash_delete_sample( sample );
            sample = NULL;
        }
        TEST_ASSERT( lsmash_importer_construct_timeline( imp, 1 ) == 0, "19b: construct timeline in the middle of reading" );
        TEST_ASSERT( check_timeline_sample_matches( root, track_ID, 4, sample_nums[3], frames[3], frame_sizes[3], "19b: timeline sample 4" ) == 0, "19b: timeline sample 4" );
        TEST_ASSERT( check_timeline_sample_matches( root, track_ID, 1, sample_nums[0], frames[0], frame_sizes[0], "19b: timeline sample 1" ) == 0, "19b: timeline sample 1" );
        for( int i = 2; i < 4; i++ )
        {
            TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &sample ) == 0 && sample != NULL, "19b: access unit after timeline" );
            TEST_ASSERT( sample && sample->length == frame_sizes[i] && memcmp( sample->data, frames[i], frame_sizes[i] ) == 0
                      && sample->dts == sample_nums[i], "19b: access unit continues from where it stopped" );
            lsmash_delete_sample( sample );
            sample = NULL;
            if( i == 2 )
                TEST_ASSERT( check_timeline_sample_matches( root, track_ID, 2, sample_nums[1], frames[1], frame_sizes[1], "19b: timeline between reads" ) == 0, "19b: timeline between reads" );
        }
        TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &sample ) == 2, "19b: EOF after the last frame" );
        TEST_ASSERT( lsmash_importer_get_last_delta( imp, 1 ) == 1500, "19b: last delta" );
        lsmash_importer_close( imp );
        lsmash_destroy_root( root );
    }
    remove( flac_path );

    /*---- IVF (AV1): 3 temporal units with a temporal delimiter and padding that a sample must not keep ----*/
    const char *ivf_path = "tests/tmp_test_fake_box.ivf";
    synth_seq_cfg_t seq_cfg =
    {
        .seq_profile = 0, .seq_level_idx_0 = 4, .width = 64, .height = 64,
        .color_description_present = 1, .color_primaries = 1, .transfer_characteristics = 1, .matrix_coefficients = 1,
        .color_range = 1, .subsampling_x = 1, .subsampling_y = 1, .enable_order_hint = 1, .order_hint_bits_minus_1 = 6,
    };
    uint8_t  raw[3][256];
    uint32_t raw_len[3] = { 0 };
    uint8_t  expected[3][256];
    uint32_t expected_len[3] = { 0 };
    {
        uint32_t rpos = 0, epos = 0;
        rpos += build_obu( raw[0] + rpos, AV1_OBU_TYPE_TEMPORAL_DELIMITER, 0, 0, 0, NULL, 0 );
        uint32_t sh_len = build_seq_hdr_obu( raw[0] + rpos, &seq_cfg );
        memcpy( expected[0] + epos, raw[0] + rpos, sh_len );
        rpos += sh_len;
        epos += sh_len;
        uint32_t kf_len = build_frame_obu( raw[0] + rpos, AV1_OBU_TYPE_FRAME, 1, 7, 0, 0, AV1_KEY_FRAME, 1, 0, 0xFF, 0 );
        memcpy( expected[0] + epos, raw[0] + rpos, kf_len );
        rpos += kf_len;
        epos += kf_len;
        uint8_t pad[2] = { 0, 0 };
        rpos += build_obu( raw[0] + rpos, AV1_OBU_TYPE_PADDING, 0, 0, 0, pad, sizeof(pad) );
        raw_len[0]      = rpos;
        expected_len[0] = epos;
        rpos = epos = 0;
        rpos += build_obu( raw[1] + rpos, AV1_OBU_TYPE_TEMPORAL_DELIMITER, 0, 0, 0, NULL, 0 );
        uint32_t f_len = build_frame_obu( raw[1] + rpos, AV1_OBU_TYPE_FRAME, 1, 7, 0, 0, AV1_INTER_FRAME, 0, 1, 0x02, 1 );
        memcpy( expected[1], raw[1] + rpos, f_len );
        raw_len[1]      = rpos + f_len;
        expected_len[1] = f_len;
        rpos = 0;
        rpos += build_obu( raw[2] + rpos, AV1_OBU_TYPE_TEMPORAL_DELIMITER, 0, 0, 0, NULL, 0 );
        f_len = build_frame_obu( raw[2] + rpos, AV1_OBU_TYPE_FRAME_HEADER, 1, 7, 1, 1, 0, 0, 0, 0, 0 );
        memcpy( expected[2], raw[2] + rpos, f_len );
        raw_len[2]      = rpos + f_len;
        expected_len[2] = f_len;
    }
    /* The IVF time base is 2/30 s per tick, so the media timescale is 30 and every tick lasts 2 units. */
    {
        FILE *fp = fopen( ivf_path, "wb" );
        TEST_ASSERT( fp != NULL, "19c: create ivf" );
        uint8_t hdr[32] = { 0 };
        memcpy( hdr, "DKIF", 4 );
        put_le16( hdr + 6, 32 );
        memcpy( hdr + 8, "AV01", 4 );
        put_le16( hdr + 12, 64 );
        put_le16( hdr + 14, 64 );
        put_le32( hdr + 16, 30 );
        put_le32( hdr + 20, 2 );
        put_le32( hdr + 24, 3 );
        fwrite( hdr, 1, 32, fp );
        for( int i = 0; i < 3; i++ )
        {
            uint8_t fhdr[12];
            put_le32( fhdr, raw_len[i] );
            put_le64( fhdr + 4, (uint64_t)i );
            fwrite( fhdr, 1, 12, fp );
            fwrite( raw[i], 1, raw_len[i], fp );
        }
        fclose( fp );
    }
    /* Reference sample properties from the sequential reader. */
    lsmash_random_access_flag ref_ra_flags[3] = { 0 };
    {
        lsmash_root_t *root = lsmash_create_root();
        importer_t *imp = lsmash_importer_open( root, ivf_path, "auto" );
        TEST_ASSERT( imp != NULL, "19c: open ivf importer" );
        for( int i = 0; i < 3; i++ )
        {
            lsmash_sample_t *sample = NULL;
            TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &sample ) == 0 && sample != NULL, "19c: reference access unit" );
            if( sample )
            {
                ref_ra_flags[i] = sample->prop.ra_flags;
                lsmash_delete_sample( sample );
            }
        }
        lsmash_importer_close( imp );
        lsmash_destroy_root( root );
    }
    TEST_ASSERT( ref_ra_flags[0] == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC, "19c: first temporal unit is a sync sample" );
    {
        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( root != NULL && lsmash_open_file( ivf_path, 1, &param ) == 0, "19c: open ivf" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( file != NULL, "19c: set ivf file" );
        TEST_ASSERT( lsmash_read_file( file, &param ) >= 0, "19c: read_file succeeds on IVF via fake boxes" );
        lsmash_movie_parameters_t movie_param;
        TEST_ASSERT( lsmash_get_movie_parameters( root, &movie_param ) == 0 && movie_param.number_of_tracks == 1,
                     "19c: one fake ivf track" );
        uint32_t track_ID = lsmash_get_track_ID( root, 1 );
        TEST_ASSERT( track_ID != 0, "19c: ivf track ID" );
        TEST_ASSERT( lsmash_get_media_timescale( root, track_ID ) == 30, "19c: media timescale is the IVF frame rate field" );
        lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_get_summary( root, track_ID, 1 );
        TEST_ASSERT( summary != NULL, "19c: summary from the fake av01 entry" );
        if( summary )
        {
            TEST_ASSERT( lsmash_check_codec_type_identical( summary->sample_type, ISOM_CODEC_TYPE_AV01_VIDEO ), "19c: av01 sample entry" );
            TEST_ASSERT( summary->width == 64 && summary->height == 64, "19c: summary is 64x64" );
            lsmash_codec_specific_t *av1c = lsmash_get_codec_specific_data( (lsmash_summary_t *)summary, 1 );
            TEST_ASSERT( av1c != NULL && av1c->type == LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_AV1, "19c: av1C is attached" );
            lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        }
        TEST_ASSERT( lsmash_construct_timeline( root, track_ID ) == 0, "19c: construct ivf timeline" );
        TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( root, track_ID ) == 3, "19c: 3 samples" );
        TEST_ASSERT( lsmash_get_max_sample_size_in_media_timeline( root, track_ID ) == LSMASH_MAX( expected_len[0], LSMASH_MAX( expected_len[1], expected_len[2] ) ),
                     "19c: max sample size excludes the OBUs dropped from samples" );
        TEST_ASSERT( lsmash_get_media_duration_from_media_timeline( root, track_ID ) == 6, "19c: media duration" );
        uint32_t last_delta = 0;
        TEST_ASSERT( lsmash_get_last_sample_delta_from_media_timeline( root, track_ID, &last_delta ) == 0 && last_delta == 2,
                     "19c: last sample delta" );
        static const int order[] = { 2, 0, 1, 2, 0 };
        for( int k = 0; k < 5; k++ )
        {
            int i = order[k];
            TEST_ASSERT( check_timeline_sample_matches( root, track_ID, (uint32_t)(i + 1), (uint64_t)i * 2, expected[i], expected_len[i], "19c: ivf sample" ) == 0, "19c: ivf sample" );
            lsmash_sample_property_t prop = { 0 };
            TEST_ASSERT( lsmash_get_sample_property_from_media_timeline( root, track_ID, (uint32_t)(i + 1), &prop ) == 0
                      && prop.ra_flags == ref_ra_flags[i], "19c: random access property equals the sequential reader" );
        }
        TEST_ASSERT( raw_len[0] > expected_len[0], "19c: the stored frame is larger than the sample, so the OBU filter ran" );
        lsmash_sample_t info = { 0 };
        TEST_ASSERT( lsmash_get_sample_info_from_media_timeline( root, track_ID, 1, &info ) == 0 && info.pos == 32 + 12,
                     "19c: sample position points at the IVF frame payload" );
        TEST_ASSERT( lsmash_get_sample_from_media_timeline( root, track_ID, 4 ) == NULL, "19c: no sample past the end" );
        lsmash_destruct_timeline( root, track_ID );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
    }
    {
        lsmash_root_t *root = lsmash_create_root();
        importer_t *imp = lsmash_importer_open( root, ivf_path, "auto" );
        TEST_ASSERT( imp != NULL, "19d: open ivf importer" );
        uint32_t track_ID = lsmash_get_track_ID( root, 1 );
        lsmash_sample_t *sample = NULL;
        TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &sample ) == 0 && sample != NULL, "19d: access unit before timeline" );
        TEST_ASSERT( sample && sample->length == expected_len[0] && memcmp( sample->data, expected[0], expected_len[0] ) == 0,
                     "19d: access unit payload before timeline" );
        lsmash_delete_sample( sample );
        sample = NULL;
        TEST_ASSERT( lsmash_importer_construct_timeline( imp, 1 ) == 0, "19d: construct timeline in the middle of reading" );
        TEST_ASSERT( check_timeline_sample_matches( root, track_ID, 3, 4, expected[2], expected_len[2], "19d: timeline sample 3" ) == 0, "19d: timeline sample 3" );
        for( int i = 1; i < 3; i++ )
        {
            TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &sample ) >= 0 && sample != NULL, "19d: access unit after timeline" );
            TEST_ASSERT( sample && sample->length == expected_len[i] && memcmp( sample->data, expected[i], expected_len[i] ) == 0
                      && sample->dts == (uint64_t)i, "19d: access unit continues from where it stopped" );
            lsmash_delete_sample( sample );
            sample = NULL;
        }
        TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &sample ) == 2, "19d: EOF after the last frame" );
        TEST_ASSERT( lsmash_importer_get_last_delta( imp, 1 ) == 1, "19d: last delta of the sequential reader is unchanged" );
        lsmash_importer_close( imp );
        lsmash_destroy_root( root );
    }
    remove( ivf_path );

    printf( "[PASS] test_read_file_flac_ivf_timeline\n" );
    return 0;
}
