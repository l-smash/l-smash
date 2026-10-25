/*****************************************************************************
 * test_main.c
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

int g_tests_run    = 0;
int g_tests_failed = 0;

/*---------------------------------------------------------------------------
 * Helper: Write an OBU header + 1-byte LEB128 size + payload into a buffer
 *---------------------------------------------------------------------------*/
uint32_t build_obu
(
    uint8_t       *dst,
    uint8_t        obu_type,
    int            has_ext,
    uint8_t        temporal_id,
    uint8_t        spatial_id,
    const uint8_t *payload,
    uint32_t       payload_size
)
{
    uint32_t pos = 0;
    dst[pos++] = (uint8_t)(((obu_type & 0x0F) << 3) | ((has_ext ? 1 : 0) << 2) | (1 << 1));
    if( has_ext )
        dst[pos++] = (uint8_t)(((temporal_id & 0x07) << 5) | ((spatial_id & 0x03) << 3));
    /* Encode payload_size as LEB128 */
    uint32_t rem = payload_size;
    do
    {
        uint8_t byte = rem & 0x7F;
        rem >>= 7;
        if( rem > 0 )
            byte |= 0x80;
        dst[pos++] = byte;
    } while( rem > 0 );
    if( payload_size > 0 && payload )
    {
        memcpy( dst + pos, payload, payload_size );
        pos += payload_size;
    }
    return pos;
}

void put_uvlc( lsmash_bits_t *bits, uint32_t val )
{
    if( val == 0 )
    {
        lsmash_bits_put( bits, 1, 1 );
        return;
    }
    uint32_t v = val + 1;
    uint32_t leading_zeros = 0;
    uint32_t tmp = v;
    while( tmp > 1 )
    {
        tmp >>= 1;
        ++leading_zeros;
    }
    for( uint32_t i = 0; i < leading_zeros; i++ )
        lsmash_bits_put( bits, 1, 0 );
    lsmash_bits_put( bits, 1, 1 );
    lsmash_bits_put( bits, leading_zeros, v - (1u << leading_zeros) );
}

void put_trailing_bits( lsmash_bits_t *bits )
{
    lsmash_bits_put( bits, 1, 1 );
    lsmash_bits_put_align( bits );
}

/*---------------------------------------------------------------------------
 * Helper: Build a synthetic Sequence Header OBU payload
 *---------------------------------------------------------------------------*/
uint32_t build_seq_hdr_obu( uint8_t *dst, const synth_seq_cfg_t *cfg )
{
    lsmash_bs_t *bs = lsmash_bs_create();
    lsmash_bits_t bits = { 0 };
    lsmash_bits_init( &bits, bs );

    lsmash_bits_put( &bits, 3, cfg->seq_profile );
    lsmash_bits_put( &bits, 1, 0 ); /* still_picture = 0 */
    lsmash_bits_put( &bits, 1, 0 ); /* reduced_still_picture_header = 0 */
    lsmash_bits_put( &bits, 1, cfg->timing_info_present );
    if( cfg->timing_info_present )
    {
        lsmash_bits_put( &bits, 32, 1001 );  /* num_units_in_display_tick */
        lsmash_bits_put( &bits, 32, 24000 ); /* time_scale */
        lsmash_bits_put( &bits, 1, 1 );      /* equal_picture_interval = 1 */
        put_uvlc( &bits, 0 );                /* num_ticks_per_picture_minus_1 = 0 */
        lsmash_bits_put( &bits, 1, 0 );      /* decoder_model_info_present_flag = 0 */
    }
    lsmash_bits_put( &bits, 1, cfg->initial_display_delay_present );
    lsmash_bits_put( &bits, 5, 0 ); /* operating_points_cnt_minus_1 = 0 */
    lsmash_bits_put( &bits, 12, 0 ); /* operating_point_idc[0] = 0 */
    lsmash_bits_put( &bits, 5, cfg->seq_level_idx_0 );
    if( cfg->seq_level_idx_0 > 7 )
        lsmash_bits_put( &bits, 1, cfg->seq_tier_0 );
    if( cfg->initial_display_delay_present )
    {
        lsmash_bits_put( &bits, 1, 1 ); /* initial_display_delay_present_for_this_op[0] = 1 */
        lsmash_bits_put( &bits, 4, cfg->initial_display_delay_minus_1 );
    }

    /* frame_width_bits_minus_1 = 15 (16 bits), frame_height_bits_minus_1 = 15 (16 bits) */
    lsmash_bits_put( &bits, 4, 15 );
    lsmash_bits_put( &bits, 4, 15 );
    lsmash_bits_put( &bits, 16, cfg->width - 1 );
    lsmash_bits_put( &bits, 16, cfg->height - 1 );
    lsmash_bits_put( &bits, 1, 0 ); /* frame_id_numbers_present_flag = 0 */
    lsmash_bits_put( &bits, 1, 0 ); /* use_128x128_superblock = 0 (64x64) */
    lsmash_bits_put( &bits, 1, 1 ); /* enable_filter_intra */
    lsmash_bits_put( &bits, 1, 1 ); /* enable_intra_edge_filter */
    lsmash_bits_put( &bits, 1, 1 ); /* enable_interintra_compound */
    lsmash_bits_put( &bits, 1, 1 ); /* enable_masked_compound */
    lsmash_bits_put( &bits, 1, 1 ); /* enable_warped_motion */
    lsmash_bits_put( &bits, 1, 1 ); /* enable_dual_filter */
    lsmash_bits_put( &bits, 1, cfg->enable_order_hint );
    if( cfg->enable_order_hint )
    {
        lsmash_bits_put( &bits, 1, 1 ); /* enable_jnt_comp */
        lsmash_bits_put( &bits, 1, 1 ); /* enable_ref_frame_mvs */
    }
    lsmash_bits_put( &bits, 1, 1 ); /* seq_choose_screen_content_tools = 1 */
    lsmash_bits_put( &bits, 1, 1 ); /* seq_choose_integer_mv = 1 */
    if( cfg->enable_order_hint )
        lsmash_bits_put( &bits, 3, cfg->order_hint_bits_minus_1 );
    lsmash_bits_put( &bits, 1, 0 ); /* enable_superres = 0 */
    lsmash_bits_put( &bits, 1, 1 ); /* enable_cdef = 1 */
    lsmash_bits_put( &bits, 1, 1 ); /* enable_restoration = 1 */

    /* color_config() */
    lsmash_bits_put( &bits, 1, cfg->high_bitdepth );
    if( cfg->seq_profile == 2 && cfg->high_bitdepth )
        lsmash_bits_put( &bits, 1, cfg->twelve_bit );
    if( cfg->seq_profile != 1 )
        lsmash_bits_put( &bits, 1, cfg->mono_chrome );
    lsmash_bits_put( &bits, 1, cfg->color_description_present );
    if( cfg->color_description_present )
    {
        lsmash_bits_put( &bits, 8, cfg->color_primaries );
        lsmash_bits_put( &bits, 8, cfg->transfer_characteristics );
        lsmash_bits_put( &bits, 8, cfg->matrix_coefficients );
    }
    if( cfg->mono_chrome )
    {
        lsmash_bits_put( &bits, 1, cfg->color_range );
    }
    else if( !(cfg->color_description_present
            && cfg->color_primaries == 1
            && cfg->transfer_characteristics == 13
            && cfg->matrix_coefficients == 0) )
    {
        lsmash_bits_put( &bits, 1, cfg->color_range );
        if( cfg->seq_profile == 2 && cfg->twelve_bit )
        {
            lsmash_bits_put( &bits, 1, cfg->subsampling_x );
            if( cfg->subsampling_x )
                lsmash_bits_put( &bits, 1, cfg->subsampling_y );
        }
        uint8_t sx = (cfg->seq_profile == 0) ? 1 : (cfg->seq_profile == 1) ? 0 : (cfg->twelve_bit ? cfg->subsampling_x : 1);
        uint8_t sy = (cfg->seq_profile == 0) ? 1 : (cfg->seq_profile == 1) ? 0 : (cfg->twelve_bit ? (cfg->subsampling_x ? cfg->subsampling_y : 0) : 0);
        if( sx && sy )
            lsmash_bits_put( &bits, 2, cfg->chroma_sample_position );
    }
    if( !cfg->mono_chrome )
        lsmash_bits_put( &bits, 1, 0 ); /* separate_uv_delta_q = 0 */

    lsmash_bits_put( &bits, 1, 0 ); /* film_grain_params_present = 0 */
    put_trailing_bits( &bits );

    uint32_t payload_len = 0;
    uint8_t *payload = lsmash_bs_export_data( bs, &payload_len );
    lsmash_bs_cleanup( bs );
    uint32_t obu_len = build_obu( dst, AV1_OBU_TYPE_SEQUENCE_HEADER, 0, 0, 0, payload, payload_len );
    lsmash_free( payload );
    return obu_len;
}

/*---------------------------------------------------------------------------
 * Helper: Build synthetic Frame / FrameHeader / TileGroup OBUs
 *---------------------------------------------------------------------------*/
uint32_t build_frame_obu
(
    uint8_t       *dst,
    uint8_t        obu_type, /* AV1_OBU_TYPE_FRAME or AV1_OBU_TYPE_FRAME_HEADER */
    int            enable_order_hint,
    uint8_t        order_hint_bits,
    int            show_existing_frame,
    uint8_t        frame_to_show_map_idx,
    uint8_t        frame_type,
    int            show_frame,
    int            showable_frame,
    uint8_t        refresh_frame_flags,
    uint32_t       order_hint
)
{
    lsmash_bs_t *bs = lsmash_bs_create();
    lsmash_bits_t bits = { 0 };
    lsmash_bits_init( &bits, bs );

    lsmash_bits_put( &bits, 1, show_existing_frame ? 1 : 0 );
    if( show_existing_frame )
    {
        lsmash_bits_put( &bits, 3, frame_to_show_map_idx );
        put_trailing_bits( &bits );
    }
    else
    {
        int is_intra = (frame_type == AV1_KEY_FRAME || frame_type == AV1_INTRA_ONLY_FRAME);
        lsmash_bits_put( &bits, 2, frame_type );
        lsmash_bits_put( &bits, 1, show_frame ? 1 : 0 );
        if( !show_frame )
            lsmash_bits_put( &bits, 1, showable_frame ? 1 : 0 );
        if( !(frame_type == AV1_SWITCH_FRAME || (frame_type == AV1_KEY_FRAME && show_frame)) )
            lsmash_bits_put( &bits, 1, 1 ); /* error_resilient_mode = 1 */
        lsmash_bits_put( &bits, 1, 1 ); /* disable_cdf_update = 1 */
        lsmash_bits_put( &bits, 1, 0 ); /* allow_screen_content_tools = 0 */
        if( frame_type != AV1_SWITCH_FRAME )
            lsmash_bits_put( &bits, 1, 0 ); /* frame_size_override_flag = 0 */
        if( enable_order_hint )
            lsmash_bits_put( &bits, order_hint_bits, order_hint );
        if( !(frame_type == AV1_SWITCH_FRAME || (frame_type == AV1_KEY_FRAME && show_frame)) )
            lsmash_bits_put( &bits, 8, refresh_frame_flags );
        if( (!is_intra || refresh_frame_flags != 0xFF) && enable_order_hint )
        {
            /* error_resilient_mode == 1 && enable_order_hint == 1 -> 8 ref_order_hint values */
            for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
                lsmash_bits_put( &bits, order_hint_bits, 0 );
        }
        if( frame_type == AV1_KEY_FRAME || frame_type == AV1_INTRA_ONLY_FRAME )
        {
            lsmash_bits_put( &bits, 1, 0 ); /* render_and_frame_size_different = 0 */
        }
        else
        {
            if( enable_order_hint )
                lsmash_bits_put( &bits, 1, 0 ); /* frame_refs_short_signaling = 0 */
            for( int i = 0; i < AV1_REFS_PER_FRAME; i++ )
                lsmash_bits_put( &bits, 3, 0 ); /* ref_frame_idx[i] = 0 */
            if( frame_type == AV1_SWITCH_FRAME )
            {
                /* frame_size_override_flag == 1 && error_resilient_mode == 1 -> explicit width/height */
                lsmash_bits_put( &bits, 16, 64 - 1 );
                lsmash_bits_put( &bits, 16, 64 - 1 );
            }
            lsmash_bits_put( &bits, 1, 0 ); /* render_and_frame_size_different = 0 */
            lsmash_bits_put( &bits, 1, 0 ); /* allow_high_precision_mv = 0 */
            lsmash_bits_put( &bits, 1, 1 ); /* is_filter_switchable = 1 */
            lsmash_bits_put( &bits, 1, 0 ); /* is_motion_mode_switchable = 0 */
        }
        /* tile_info(): 64x64 frame has 1 superblock -> minLog2TileCols=0, maxLog2TileCols=0, maxLog2TileRows=0 */
        lsmash_bits_put( &bits, 1, 1 ); /* uniform_tile_spacing_flag = 1 */
        /* quantization_params(): base_q_idx = 0 (CodedLossless = 1, AllLossless = 1) */
        lsmash_bits_put( &bits, 8, 0 ); /* base_q_idx = 0 */
        lsmash_bits_put( &bits, 1, 0 ); /* DeltaQYDc: delta_coded = 0 */
        lsmash_bits_put( &bits, 1, 0 ); /* DeltaQUDc: delta_coded = 0 */
        lsmash_bits_put( &bits, 1, 0 ); /* DeltaQUAc: delta_coded = 0 */
        lsmash_bits_put( &bits, 1, 0 ); /* using_qmatrix = 0 */
        /* segmentation_params() */
        lsmash_bits_put( &bits, 1, 0 ); /* segmentation_enabled = 0 */
        /* frame_reference_mode() */
        if( !is_intra )
            lsmash_bits_put( &bits, 1, 0 ); /* reference_select = 0 */
        /* reduced_tx_set */
        lsmash_bits_put( &bits, 1, 0 ); /* reduced_tx_set = 0 */
        /* global_motion_params() */
        if( !is_intra )
        {
            for( int ref = 1; ref <= 7; ref++ )
                lsmash_bits_put( &bits, 1, 0 ); /* is_global[ref] = 0 */
        }
        if( obu_type == AV1_OBU_TYPE_FRAME )
        {
            /* byte_alignment() + tile_group_obu() (NumTiles == 1 -> 1-byte tile payload) */
            lsmash_bits_put_align( &bits );
            lsmash_bs_put_byte( bs, 0xAC );
        }
        else
            put_trailing_bits( &bits );
    }

    uint32_t payload_len = 0;
    uint8_t *payload = lsmash_bs_export_data( bs, &payload_len );
    lsmash_bs_cleanup( bs );
    uint32_t obu_len = build_obu( dst, obu_type, 0, 0, 0, payload, payload_len );
    lsmash_free( payload );
    return obu_len;
}

lsmash_video_summary_t *create_synth_av01_summary( uint32_t width, uint32_t height )
{
    lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_VIDEO );
    if( !summary )
        return NULL;
    summary->sample_type = ISOM_CODEC_TYPE_AV01_VIDEO;
    summary->width       = width;
    summary->height      = height;
    summary->par_h       = 1;
    summary->par_v       = 1;
    summary->depth       = 0x0018;

    lsmash_codec_specific_t *cs = lsmash_create_codec_specific_data(
        LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_AV1,
        LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
    if( !cs )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
    lsmash_av1_specific_parameters_t *param = (lsmash_av1_specific_parameters_t *)cs->data.structured;
    param->seq_profile           = 0;
    param->seq_level_idx_0       = 4;
    param->seq_tier_0            = 0;
    param->high_bitdepth         = 0;
    param->twelve_bit            = 0;
    param->monochrome            = 0;
    param->chroma_subsampling_x  = 1;
    param->chroma_subsampling_y  = 1;
    param->chroma_sample_position = LSMASH_AV1_CSP_UNKNOWN;
    if( lsmash_add_codec_specific_data( (lsmash_summary_t *)summary, cs ) < 0 )
    {
        lsmash_destroy_codec_specific_data( cs );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
    lsmash_destroy_codec_specific_data( cs );
    return summary;
}

uint32_t build_synth_flac_frame
(
    uint8_t *dst,
    int      variable_block_strategy,
    uint32_t block_size,
    uint64_t coded_number,
    uint32_t payload_bytes
)
{
    uint32_t pos = 0;
    dst[pos++] = 0xFF;
    dst[pos++] = (uint8_t)(0xF8 | (variable_block_strategy & 1));
    uint8_t bs_code = (block_size == 4096) ? 12 : 7; /* 12 -> 4096, 7 -> 16-bit uncommon (bs-1) after UTF-8 */
    uint8_t sr_code = 10; /* 96 kHz */
    dst[pos++] = (uint8_t)((bs_code << 4) | sr_code);
    dst[pos++] = (uint8_t)((1 << 4) | (6 << 1) | 0); /* stereo (ch=1), 24-bit (bps=6), reserved=0 */

    /* Encode UTF-8 frame/sample number */
    if( coded_number < 0x80 )
    {
        dst[pos++] = (uint8_t)coded_number;
    }
    else if( coded_number < 0x800 )
    {
        dst[pos++] = (uint8_t)(0xC0 | ((coded_number >> 6) & 0x1F));
        dst[pos++] = (uint8_t)(0x80 |  (coded_number       & 0x3F));
    }
    else
    {
        dst[pos++] = (uint8_t)(0xE0 | ((coded_number >> 12) & 0x0F));
        dst[pos++] = (uint8_t)(0x80 | ((coded_number >>  6) & 0x3F));
        dst[pos++] = (uint8_t)(0x80 |  (coded_number        & 0x3F));
    }

    if( bs_code == 7 )
    {
        uint16_t bs_m1 = (uint16_t)(block_size - 1);
        dst[pos++] = (uint8_t)((bs_m1 >> 8) & 0xFF);
        dst[pos++] = (uint8_t)( bs_m1       & 0xFF);
    }

    dst[pos] = lsm_flac_crc8( dst, pos );
    pos++;

    for( uint32_t i = 0; i < payload_bytes; i++ )
    {
        /* Embed a false sync word in the middle of the payload to test CRC-8 + frame number sync rejection */
        if( i == 8 && payload_bytes >= 16 )
        {
            dst[pos++] = 0xFF;
            dst[pos++] = (uint8_t)(0xF8 | (variable_block_strategy & 1));
            dst[pos++] = 0xCA;
            dst[pos++] = 0x1C;
            dst[pos++] = 0x00;
            dst[pos++] = 0x55; /* wrong CRC-8 */
            i += 5;
        }
        else
            dst[pos++] = (uint8_t)(((i * 37) + (uint32_t)coded_number) & 0x7F);
    }
    return pos;
}

#ifdef LSMASH_DEBUG_ALLOC
#define RUN_TEST( test_fn )                                                       \
    do                                                                            \
    {                                                                             \
        if( (test_fn)() < 0 )                                                     \
            return 1;                                                             \
        if( lsm_alloc_verify_no_leaks() != 0 )                                    \
        {                                                                         \
            fprintf( stderr, "[FAIL] Memory leak detected after %s\n", #test_fn );\
            ++g_tests_failed;                                                     \
        }                                                                         \
        assert( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0 ); \
    } while( 0 )
#else
#define RUN_TEST( test_fn )                                                       \
    do                                                                            \
    {                                                                             \
        if( (test_fn)() < 0 )                                                     \
            return 1;                                                             \
    } while( 0 )
#endif

int main( void )
{
    printf( "=== L-SMASH Synthetic Test Suite ===\n" );
    RUN_TEST( test_memory_audit_allocator );
    RUN_TEST( test_paged_block_array );
    RUN_TEST( test_large_scale_paged_tables_and_roll_dedup );
    RUN_TEST( test_bitstream_primitives );
    RUN_TEST( test_sequence_header_and_av1c );
    RUN_TEST( test_frame_headers_and_tu_assembly );
    RUN_TEST( test_av1_multitile_and_sync_sample_compliance );
    RUN_TEST( test_ivf_mux_demux_roundtrip );
    RUN_TEST( test_av1_fragmented_isobmff_roundtrip );
    RUN_TEST( test_lpcm_pcmc_chnl_srat_roundtrip );
    RUN_TEST( test_multitrack_fragmented_lpcm_roundtrip );
    RUN_TEST( test_wave_importer_isobmff_lpcm );
    RUN_TEST( test_malformed_boxes_and_sample_tables );
    RUN_TEST( test_multibuf_resize_and_codec_regressions );
    RUN_TEST( test_flac_isobmff_roundtrip );
    RUN_TEST( test_segmented_isobmff_and_abort_callback );
    RUN_TEST( test_external_data_reference_roundtrip );
    RUN_TEST( test_read_file_fake_box_and_raw_stream_acceptance );
    RUN_TEST( test_read_file_flac_ivf_timeline );
    RUN_TEST( test_read_file_audio_raw_streams );
    RUN_TEST( test_read_file_video_raw_streams );
    RUN_TEST( test_muxer_lshmuxer_cli_equivalence );
#ifdef LSMASH_DEBUG_ALLOC
    assert( lsm_alloc_verify_no_leaks() == 0 );
    assert( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0 );
#endif
    printf( "=== Assertions run: %d, Failures: %d ===\n", g_tests_run, g_tests_failed );
    return g_tests_failed == 0 ? 0 : 1;
}
