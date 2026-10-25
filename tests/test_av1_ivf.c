/*****************************************************************************
 * test_av1_ivf.c
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
 * Test 2: Sequence Header OBU (8/10/12-bit, BT.709 vs sRGB) & av1C Round-Trip
 *---------------------------------------------------------------------------*/
int test_sequence_header_and_av1c( void )
{
    uint8_t obu_buf[256];

    /* 2a. 8-bit Main Profile (Profile 0, 4:2:0, BT.709) - tests color_config && fix */
    {
        synth_seq_cfg_t cfg = {
            .seq_profile = 0,
            .seq_level_idx_0 = 8,
            .seq_tier_0 = 0,
            .width = 640,
            .height = 360,
            .high_bitdepth = 0,
            .twelve_bit = 0,
            .mono_chrome = 0,
            .color_description_present = 1,
            .color_primaries = 1,          /* CP_BT_709 */
            .transfer_characteristics = 1, /* BT.709 (NOT sRGB) */
            .matrix_coefficients = 1,      /* BT.709 (NOT Identity) */
            .color_range = 0,
            .subsampling_x = 1,
            .subsampling_y = 1,
            .chroma_sample_position = LSMASH_AV1_CSP_COLOCATED,
            .enable_order_hint = 1,
            .order_hint_bits_minus_1 = 6,
            .initial_display_delay_present = 1,
            .initial_display_delay_minus_1 = 3,
            .timing_info_present = 1,
        };
        uint32_t len = build_seq_hdr_obu( obu_buf, &cfg );
        lsmash_av1_specific_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_setup_av1_specific_parameters_from_access_unit( &param, obu_buf, len ) == 0,
                     "setup 8-bit BT.709 sequence header" );
        TEST_ASSERT( param.seq_profile == 0, "seq_profile == 0" );
        TEST_ASSERT( param.seq_level_idx_0 == 8, "seq_level_idx_0 == 8" );
        TEST_ASSERT( param.high_bitdepth == 0 && param.twelve_bit == 0, "8-bit flags" );
        TEST_ASSERT( param.chroma_subsampling_x == 1 && param.chroma_subsampling_y == 1,
                     "BT.709 4:2:0 subsampling not clobbered by sRGB check" );
        TEST_ASSERT( param.chroma_sample_position == LSMASH_AV1_CSP_COLOCATED, "CSP colocated" );
        TEST_ASSERT( param.initial_presentation_delay_present == 1 && param.initial_presentation_delay_minus_one == 3,
                     "initial_presentation_delay preserved" );
        TEST_ASSERT( param.configOBUs.sz == len && memcmp( param.configOBUs.data, obu_buf, len ) == 0,
                     "configOBUs matches Sequence Header OBU" );
        lsmash_destroy_av1_headers( &param );
    }

    /* 2b. 12-bit Professional Profile (Profile 2, high_bitdepth=1, twelve_bit=1) + av1C round-trip */
    {
        synth_seq_cfg_t cfg = {
            .seq_profile = 2,
            .seq_level_idx_0 = 13,
            .seq_tier_0 = 1,
            .width = 1920,
            .height = 1080,
            .high_bitdepth = 1,
            .twelve_bit = 1,
            .mono_chrome = 0,
            .color_description_present = 0,
            .color_range = 1,
            .subsampling_x = 1,
            .subsampling_y = 0,
            .chroma_sample_position = LSMASH_AV1_CSP_UNKNOWN,
            .enable_order_hint = 0,
        };
        uint32_t len = build_seq_hdr_obu( obu_buf, &cfg );
        lsmash_av1_specific_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_setup_av1_specific_parameters_from_access_unit( &param, obu_buf, len ) == 0,
                     "setup 12-bit Profile 2 sequence header" );
        TEST_ASSERT( param.seq_profile == 2, "seq_profile == 2" );
        TEST_ASSERT( param.seq_level_idx_0 == 13 && param.seq_tier_0 == 1, "level 13 tier 1" );
        TEST_ASSERT( param.high_bitdepth == 1 && param.twelve_bit == 1, "twelve_bit == 1" );
        TEST_ASSERT( param.chroma_subsampling_x == 1 && param.chroma_subsampling_y == 0, "4:2:2 subsampling" );

        /* Create raw av1C box and reconstruct specific parameters from it */
        uint32_t av1c_len = 0;
        uint8_t *av1c_box = lsmash_create_av1_specific_info( &param, &av1c_len );
        TEST_ASSERT( av1c_box != NULL && av1c_len == 8 + 4 + len, "create av1C box" );

        lsmash_av1_specific_parameters_t reconstructed = { 0 };
        lsmash_codec_specific_t src_cs = {
            .type = LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_AV1,
            .format = LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED,
            .data.unstructured = av1c_box,
            .size = av1c_len,
        };
        lsmash_codec_specific_t dst_cs = {
            .type = LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_AV1,
            .format = LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED,
            .data.structured = &reconstructed,
            .size = sizeof(reconstructed),
        };
        TEST_ASSERT( lsm_av1_construct_specific_parameters( &dst_cs, &src_cs ) == 0, "reconstruct av1C" );
        TEST_ASSERT( reconstructed.seq_profile == 2 && reconstructed.twelve_bit == 1 && reconstructed.seq_tier_0 == 1,
                     "reconstructed 12-bit fields match" );
        TEST_ASSERT( reconstructed.configOBUs.sz == len && memcmp( reconstructed.configOBUs.data, obu_buf, len ) == 0,
                     "reconstructed configOBUs match" );

        lsmash_destroy_av1_headers( &reconstructed );
        lsmash_destroy_av1_headers( &param );
        lsmash_free( av1c_box );
    }

    printf( "[PASS] test_sequence_header_and_av1c\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 3: Frame Types, Reference Updates, show_existing_frame, OBU Stripping
 *---------------------------------------------------------------------------*/
int test_frame_headers_and_tu_assembly( void )
{
    av1_parser_t parser;
    TEST_ASSERT( lsm_av1_setup_parser( &parser, NULL ) == 0, "setup parser" );

    synth_seq_cfg_t seq_cfg = {
        .seq_profile = 0,
        .seq_level_idx_0 = 4,
        .width = 64,
        .height = 64,
        .color_range = 1,
        .subsampling_x = 1,
        .subsampling_y = 1,
        .enable_order_hint = 1,
        .order_hint_bits_minus_1 = 6, /* OrderHintBits = 7 */
    };

    uint8_t tu_buf[512];
    uint8_t out_buf[512];
    uint32_t out_len = 0;
    lsmash_sample_property_t prop = { 0 };
    lsmash_av1_specific_parameters_t param = { 0 };

    /* TU 0: Temporal Delimiter + Sequence Header + HDR10+ Metadata + Delayed KEY_FRAME (show_frame=0, refresh=0x01) + Padding */
    {
        uint32_t pos = 0;
        pos += build_obu( tu_buf + pos, AV1_OBU_TYPE_TEMPORAL_DELIMITER, 0, 0, 0, NULL, 0 );
        uint32_t sh_len = build_seq_hdr_obu( tu_buf + pos, &seq_cfg );
        pos += sh_len;
        uint8_t hdr10p_payload[9] = { 0x04, 0xB5, 0x00, 0x3C, 0x00, 0x01, 0x04, 0x01, 0x00 };
        uint32_t meta_len = build_obu( tu_buf + pos, AV1_OBU_TYPE_METADATA, 0, 0, 0, hdr10p_payload, sizeof(hdr10p_payload) );
        pos += meta_len;
        uint32_t frm_len = build_frame_obu( tu_buf + pos, AV1_OBU_TYPE_FRAME, 1, 7, 0, 0, AV1_KEY_FRAME, 0, 1, 0x01, 10 );
        pos += frm_len;
        uint8_t pad_bytes[4] = { 0, 0, 0, 0 };
        pos += build_obu( tu_buf + pos, AV1_OBU_TYPE_PADDING, 0, 0, 0, pad_bytes, sizeof(pad_bytes) );

        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, &param ) == 0,
                     "parse TU 0 (delayed KEY_FRAME + TD + Padding + HDR10+)" );
        /* TD and Padding must be stripped; SH + Metadata + Frame must remain */
        TEST_ASSERT( out_len == sh_len + meta_len + frm_len, "TD and Padding stripped from TU 0" );
        TEST_ASSERT( param.has_hdr10p == 1, "HDR10+ ITU-T T.35 detected" );
        /* AV1-ISOBMFF v1.2.0 Section 2.3.3: configOBUs holds initial Sequence Header + Metadata OBUs */
        TEST_ASSERT( param.configOBUs.sz == sh_len + meta_len
                  && memcmp( param.configOBUs.data, tu_buf + 2, sh_len + meta_len ) == 0,
                     "configOBUs preserves initial Sequence Header + Metadata OBU" );
        TEST_ASSERT( prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP, "Delayed KEY_FRAME marked as OPEN_RAP" );
        TEST_ASSERT( parser.RefValid[0] == 1 && parser.RefValid[1] == 0, "Only RefValid[0] refreshed by 0x01" );
        TEST_ASSERT( parser.RefFrameType[0] == AV1_KEY_FRAME && parser.RefOrderHint[0] == 10, "Slot 0 stores KEY_FRAME" );
    }

    /* TU 1: INTER_FRAME (show_frame=1, refresh_frame_flags=0x04 -> slot 2) */
    {
        uint32_t pos = build_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME, 1, 7, 0, 0, AV1_INTER_FRAME, 1, 0, 0x04, 11 );
        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse TU 1 (INTER_FRAME)" );
        TEST_ASSERT( prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE, "INTER_FRAME is not RAP" );
        TEST_ASSERT( parser.RefValid[2] == 1 && parser.RefOrderHint[2] == 11, "Slot 2 refreshed by 8-bit refresh_frame_flags=0x04" );
    }

    /* TU 2: INTRA_ONLY_FRAME (show_frame=1, refresh_frame_flags=0x08 -> slot 3) */
    {
        uint32_t pos = build_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME, 1, 7, 0, 0, AV1_INTRA_ONLY_FRAME, 1, 0, 0x08, 12 );
        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse TU 2 (INTRA_ONLY_FRAME)" );
        TEST_ASSERT( prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP, "INTRA_ONLY_FRAME marked as OPEN_RAP" );
        TEST_ASSERT( parser.RefValid[3] == 1 && parser.RefFrameType[3] == AV1_INTRA_ONLY_FRAME,
                     "Slot 3 refreshed by INTRA_ONLY_FRAME" );
    }

    /* TU 3: SWITCH_FRAME (show_frame=1 -> refreshes all 8 slots) */
    {
        uint32_t pos = build_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME, 1, 7, 0, 0, AV1_SWITCH_FRAME, 1, 0, 0xFF, 13 );
        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse TU 3 (SWITCH_FRAME)" );
        TEST_ASSERT( prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP, "SWITCH_FRAME marked as OPEN_RAP" );
        for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
            TEST_ASSERT( parser.RefValid[i] == 1 && parser.RefOrderHint[i] == 13, "All slots refreshed by SWITCH_FRAME" );
    }

    /* Re-store a delayed KEY_FRAME into slot 0 (with OrderHint=20), then test show_existing_frame=1 pointing to slot 0 */
    {
        uint32_t pos = build_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME, 1, 7, 0, 0, AV1_KEY_FRAME, 0, 1, 0x01, 20 );
        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse delayed KEY_FRAME into slot 0" );
        TEST_ASSERT( parser.RefOrderHint[0] == 20 && parser.RefOrderHint[1] == 13, "Slot 0 has OrderHint 20, Slot 1 has 13" );

        /* Now send OBU_FRAME_HEADER with show_existing_frame=1, frame_to_show_map_idx=0.
         * Since RefFrameType[0] == AV1_KEY_FRAME, it must refresh all 8 reference slots with slot 0's state! */
        pos = build_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME_HEADER, 1, 7, 1, 0, 0, 0, 0, 0, 0 );
        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse show_existing_frame=1 pointing to slot 0 KEY_FRAME" );
        for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
            TEST_ASSERT( parser.RefValid[i] == 1 && parser.RefOrderHint[i] == 20 && parser.RefFrameType[i] == AV1_KEY_FRAME,
                         "show_existing_frame on KEY_FRAME refreshed all 8 reference slots" );
    }

    /* Also test enable_order_hint == 0 stream (regression test for unguarded order_hint read) */
    {
        lsm_av1_reset_parser( &parser );
        synth_seq_cfg_t no_oh_cfg = seq_cfg;
        no_oh_cfg.enable_order_hint = 0;
        uint32_t pos = build_seq_hdr_obu( tu_buf, &no_oh_cfg );
        pos += build_frame_obu( tu_buf + pos, AV1_OBU_TYPE_FRAME, 0, 0, 0, 0, AV1_KEY_FRAME, 1, 0, 0xFF, 0 );
        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse KEY_FRAME with enable_order_hint=0" );
        TEST_ASSERT( prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC, "KEY_FRAME + show_frame=1 is SYNC" );

        pos = build_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME, 0, 0, 0, 0, AV1_INTER_FRAME, 1, 0, 0x02, 0 );
        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse INTER_FRAME with enable_order_hint=0" );
        TEST_ASSERT( parser.frame.order_hint == 0 && parser.RefValid[1] == 1, "INTER_FRAME parsed cleanly with enable_order_hint=0" );
    }

    lsmash_destroy_av1_headers( &param );
    lsm_av1_cleanup_parser( &parser );
    printf( "[PASS] test_frame_headers_and_tu_assembly\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 4: Synthetic IVF File Import & MP4 Mux/Demux End-to-End Round-Trip
 *---------------------------------------------------------------------------*/
int test_ivf_mux_demux_roundtrip( void )
{
    const char *ivf_path = "tests/synth_test.ivf";
    const char *mp4_path = "tests/synth_test.mp4";

    synth_seq_cfg_t seq_cfg = {
        .seq_profile = 0,
        .seq_level_idx_0 = 4,
        .width = 64,
        .height = 64,
        .color_description_present = 1,
        .color_primaries = 1,
        .transfer_characteristics = 1,
        .matrix_coefficients = 1,
        .color_range = 1,
        .subsampling_x = 1,
        .subsampling_y = 1,
        .enable_order_hint = 1,
        .order_hint_bits_minus_1 = 6,
    };

    uint8_t frame_raw[3][256];
    uint32_t frame_raw_len[3] = { 0 };
    uint8_t frame_expected[3][256];
    uint32_t frame_expected_len[3] = { 0 };

    /* Frame 0 (PTS = 0): TD + SH + KEY_FRAME (show_frame=1) + Padding */
    {
        uint32_t rpos = 0, epos = 0;
        rpos += build_obu( frame_raw[0] + rpos, AV1_OBU_TYPE_TEMPORAL_DELIMITER, 0, 0, 0, NULL, 0 );
        uint32_t sh_len = build_seq_hdr_obu( frame_raw[0] + rpos, &seq_cfg );
        memcpy( frame_expected[0] + epos, frame_raw[0] + rpos, sh_len );
        rpos += sh_len;
        epos += sh_len;
        uint32_t kf_len = build_frame_obu( frame_raw[0] + rpos, AV1_OBU_TYPE_FRAME, 1, 7, 0, 0, AV1_KEY_FRAME, 1, 0, 0xFF, 0 );
        memcpy( frame_expected[0] + epos, frame_raw[0] + rpos, kf_len );
        rpos += kf_len;
        epos += kf_len;
        uint8_t pad[2] = { 0, 0 };
        rpos += build_obu( frame_raw[0] + rpos, AV1_OBU_TYPE_PADDING, 0, 0, 0, pad, sizeof(pad) );
        frame_raw_len[0]      = rpos;
        frame_expected_len[0] = epos;
    }

    /* Frame 1 (PTS = 1): TD + INTER_FRAME (show_frame=0, showable=1, refresh=0x02 -> slot 1) */
    {
        uint32_t rpos = 0, epos = 0;
        rpos += build_obu( frame_raw[1] + rpos, AV1_OBU_TYPE_TEMPORAL_DELIMITER, 0, 0, 0, NULL, 0 );
        uint32_t f_len = build_frame_obu( frame_raw[1] + rpos, AV1_OBU_TYPE_FRAME, 1, 7, 0, 0, AV1_INTER_FRAME, 0, 1, 0x02, 1 );
        memcpy( frame_expected[1] + epos, frame_raw[1] + rpos, f_len );
        rpos += f_len;
        epos += f_len;
        frame_raw_len[1]      = rpos;
        frame_expected_len[1] = epos;
    }

    /* Frame 2 (PTS = 2): TD + FRAME_HEADER (show_existing_frame=1, idx=1) */
    {
        uint32_t rpos = 0, epos = 0;
        rpos += build_obu( frame_raw[2] + rpos, AV1_OBU_TYPE_TEMPORAL_DELIMITER, 0, 0, 0, NULL, 0 );
        uint32_t f_len = build_frame_obu( frame_raw[2] + rpos, AV1_OBU_TYPE_FRAME_HEADER, 1, 7, 1, 1, 0, 0, 0, 0, 0 );
        memcpy( frame_expected[2] + epos, frame_raw[2] + rpos, f_len );
        rpos += f_len;
        epos += f_len;
        frame_raw_len[2]      = rpos;
        frame_expected_len[2] = epos;
    }

    /* Write synthetic IVF file */
    FILE *fp = fopen( ivf_path, "wb" );
    TEST_ASSERT( fp != NULL, "create synthetic IVF file" );
    uint8_t ivf_hdr[32] = { 0 };
    memcpy( ivf_hdr, "DKIF", 4 );
    put_le16( ivf_hdr + 4, 0 );
    put_le16( ivf_hdr + 6, 32 );
    memcpy( ivf_hdr + 8, "AV01", 4 );
    put_le16( ivf_hdr + 12, 64 );
    put_le16( ivf_hdr + 14, 64 );
    put_le32( ivf_hdr + 16, 25 ); /* frame_rate */
    put_le32( ivf_hdr + 20, 1 );  /* time_scale */
    put_le32( ivf_hdr + 24, 3 );  /* number_of_frames */
    fwrite( ivf_hdr, 1, 32, fp );
    for( int i = 0; i < 3; i++ )
    {
        uint8_t fhdr[12];
        put_le32( fhdr, frame_raw_len[i] );
        put_le64( fhdr + 4, (uint64_t)i );
        fwrite( fhdr, 1, 12, fp );
        fwrite( frame_raw[i], 1, frame_raw_len[i], fp );
    }
    fclose( fp );

    /* Import IVF and Mux to MP4 */
    lsmash_root_t *in_root = lsmash_create_root();
    TEST_ASSERT( in_root != NULL, "create in_root" );
    importer_t *importer = lsmash_importer_open( in_root, ivf_path, "auto" );
    TEST_ASSERT( importer != NULL, "lsmash_importer_open on synthetic IVF" );
    TEST_ASSERT( lsmash_importer_get_track_count( importer ) == 1, "IVF track count == 1" );

    lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_duplicate_summary( importer, 1 );
    TEST_ASSERT( summary != NULL, "duplicate IVF summary" );
    TEST_ASSERT( summary->width == 64 && summary->height == 64, "summary dimensions 64x64" );
    TEST_ASSERT( summary->timescale == 25 && summary->timebase == 1, "summary 25/1 timescale" );

    lsmash_root_t *out_root = lsmash_create_root();
    lsmash_file_parameters_t out_param = { 0 };
    TEST_ASSERT( lsmash_open_file( mp4_path, 0, &out_param ) == 0, "open output MP4" );
    lsmash_brand_type brands[4] = {
        ISOM_BRAND_TYPE_ISOM,
        ISOM_BRAND_TYPE_ISO6,
        ISOM_BRAND_TYPE_AV01,
        ISOM_BRAND_TYPE_MP41
    };
    out_param.major_brand   = ISOM_BRAND_TYPE_MP41;
    out_param.brands        = brands;
    out_param.brand_count   = 4;
    out_param.minor_version = 0;
    TEST_ASSERT( lsmash_set_file( out_root, &out_param ) != NULL, "set output MP4 file" );

    lsmash_movie_parameters_t movie_param;
    lsmash_initialize_movie_parameters( &movie_param );
    lsmash_set_movie_parameters( out_root, &movie_param );

    uint32_t track_ID = lsmash_create_track( out_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
    TEST_ASSERT( track_ID != 0, "create video track" );
    lsmash_track_parameters_t track_param;
    lsmash_initialize_track_parameters( &track_param );
    track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
    track_param.display_width  = summary->width  << 16;
    track_param.display_height = summary->height << 16;
    TEST_ASSERT( lsmash_set_track_parameters( out_root, track_ID, &track_param ) == 0, "set track params" );

    lsmash_media_parameters_t media_param;
    lsmash_initialize_media_parameters( &media_param );
    media_param.timescale = summary->timescale;
    TEST_ASSERT( lsmash_set_media_parameters( out_root, track_ID, &media_param ) == 0, "set media params" );

    uint32_t sample_entry = lsmash_add_sample_entry( out_root, track_ID, summary );
    TEST_ASSERT( sample_entry == 1, "add AV01 sample entry" );

    for( int i = 0; i < 3; i++ )
    {
        lsmash_sample_t *sample = NULL;
        int ret = lsmash_importer_get_access_unit( importer, 1, &sample );
        TEST_ASSERT( ret == 0 && sample != NULL, "get_access_unit from IVF" );
        TEST_ASSERT( sample->length == frame_expected_len[i], "assembled sample length matches expected" );
        TEST_ASSERT( memcmp( sample->data, frame_expected[i], frame_expected_len[i] ) == 0,
                     "assembled sample bytes match expected" );
        sample->index = sample_entry;
        sample->dts  *= summary->timebase;
        sample->cts  *= summary->timebase;
        TEST_ASSERT( lsmash_append_sample( out_root, track_ID, sample ) == 0, "append sample to MP4" );
    }
    lsmash_sample_t *eof_sample = NULL;
    TEST_ASSERT( lsmash_importer_get_access_unit( importer, 1, &eof_sample ) == 2, "IVF importer returns EOF (2)" );
    uint32_t last_delta = lsmash_importer_get_last_delta( importer, 1 );
    TEST_ASSERT( last_delta == 1, "IVF last_delta == 1" );
    TEST_ASSERT( lsmash_flush_pooled_samples( out_root, track_ID, last_delta * summary->timebase ) == 0, "flush pooled samples" );
    TEST_ASSERT( lsmash_finish_movie( out_root, NULL ) == 0, "finish MP4 movie" );

    lsmash_cleanup_summary( (lsmash_summary_t *)summary );
    lsmash_importer_close( importer );
    lsmash_destroy_root( in_root );
    lsmash_close_file( &out_param );
    lsmash_destroy_root( out_root );

    /* Demux MP4 and verify round-trip + box dump (av1C print) */
    const char *dump_path = "tests/tmp_synth_av1.dump.txt";
    lsmash_root_t *demux_root = lsmash_create_root();
    lsmash_file_parameters_t demux_param = { 0 };
    TEST_ASSERT( lsmash_open_file( mp4_path, 1, &demux_param ) == 0, "open MP4 for demux" );
    demux_param.mode |= LSMASH_FILE_MODE_DUMP;
    lsmash_file_t *demux_file = lsmash_set_file( demux_root, &demux_param );
    TEST_ASSERT( demux_file != NULL, "set demux file" );
    TEST_ASSERT( lsmash_read_file( demux_file, &demux_param ) >= 0, "read MP4 file" );
    TEST_ASSERT( lsmash_print_movie( demux_root, dump_path ) == 0, "print MP4 movie box hierarchy (av1C)" );
    {
        FILE *dfp = fopen( dump_path, "rb" );
        TEST_ASSERT( dfp != NULL, "open box dump file" );
        char *dump_buf = (char *)calloc( 1, 16384 );
        TEST_ASSERT( dump_buf != NULL, "alloc dump_buf" );
        size_t nread = fread( dump_buf, 1, 16383, dfp );
        fclose( dfp );
        int has_av01      = (nread > 0 && strstr( dump_buf, "[av01: Visual Description]" ) != NULL);
        int has_av1c      = (nread > 0 && strstr( dump_buf, "[av1C: AV1CodecConfigurationBox]" ) != NULL);
        int has_csp_str   = (nread > 0 && strstr( dump_buf, "chroma_sample_position = unknown" ) != NULL);
        int has_cfg_obus  = (nread > 0 && strstr( dump_buf, "configOBUs" ) != NULL);
        free( dump_buf );
        TEST_ASSERT( has_av01,     "box dump contains [av01: Visual Description]" );
        TEST_ASSERT( has_av1c,     "box dump contains [av1C: AV1CodecConfigurationBox]" );
        TEST_ASSERT( has_csp_str,  "box dump prints string chroma_sample_position" );
        TEST_ASSERT( has_cfg_obus, "box dump prints configOBUs size" );
    }
    remove( dump_path );
    TEST_ASSERT( lsmash_construct_timeline( demux_root, 1 ) == 0, "construct timeline" );
    TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( demux_root, 1 ) == 3, "demux sample count == 3" );

    for( uint32_t i = 1; i <= 3; i++ )
    {
        lsmash_sample_t *s = lsmash_get_sample_from_media_timeline( demux_root, 1, i );
        TEST_ASSERT( s != NULL, "get sample from timeline" );
        TEST_ASSERT( s->length == frame_expected_len[i - 1], "demux sample length matches" );
        TEST_ASSERT( memcmp( s->data, frame_expected[i - 1], frame_expected_len[i - 1] ) == 0,
                     "demux sample payload matches bit-for-bit" );
        if( i == 1 )
            TEST_ASSERT( (s->prop.ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC) != 0, "sample 1 is sync sample" );
        else
            TEST_ASSERT( (s->prop.ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC) == 0, "samples 2..3 are non-sync" );
        lsmash_delete_sample( s );
    }

    lsmash_destruct_timeline( demux_root, 1 );
    lsmash_close_file( &demux_param );
    lsmash_destroy_root( demux_root );

    remove( ivf_path );
    remove( mp4_path );

    printf( "[PASS] test_ivf_mux_demux_roundtrip\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 9: AV1 Multi-Tile OBU_FRAME/TILE_GROUP, Sync Sample & OBU Whitelist
 *---------------------------------------------------------------------------*/
static uint32_t build_multitile_2x2_frame_obu
(
    uint8_t *dst,
    uint8_t  obu_type, /* AV1_OBU_TYPE_FRAME or AV1_OBU_TYPE_REDUNDANT_FRAME_HEADER */
    uint8_t  frame_type,
    int      show_frame,
    uint8_t  refresh_frame_flags,
    uint32_t order_hint,
    int      present_flag,
    uint32_t tg_start,
    uint32_t tg_end
)
{
    lsmash_bs_t *bs = lsmash_bs_create();
    lsmash_bits_t bits = { 0 };
    lsmash_bits_init( &bits, bs );

    int is_intra = (frame_type == AV1_KEY_FRAME || frame_type == AV1_INTRA_ONLY_FRAME);
    lsmash_bits_put( &bits, 1, 0 ); /* show_existing_frame = 0 */
    lsmash_bits_put( &bits, 2, frame_type );
    lsmash_bits_put( &bits, 1, show_frame ? 1 : 0 );
    if( !show_frame )
        lsmash_bits_put( &bits, 1, 1 ); /* showable_frame = 1 */
    if( !(frame_type == AV1_SWITCH_FRAME || (frame_type == AV1_KEY_FRAME && show_frame)) )
        lsmash_bits_put( &bits, 1, 1 ); /* error_resilient_mode = 1 */
    lsmash_bits_put( &bits, 1, 1 ); /* disable_cdf_update = 1 */
    lsmash_bits_put( &bits, 1, 0 ); /* allow_screen_content_tools = 0 */
    if( frame_type != AV1_SWITCH_FRAME )
        lsmash_bits_put( &bits, 1, 0 ); /* frame_size_override_flag = 0 */
    lsmash_bits_put( &bits, 7, order_hint ); /* order_hint (7 bits) */
    if( !(frame_type == AV1_SWITCH_FRAME || (frame_type == AV1_KEY_FRAME && show_frame)) )
        lsmash_bits_put( &bits, 8, refresh_frame_flags );
    if( !is_intra || refresh_frame_flags != 0xFF )
    {
        for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
            lsmash_bits_put( &bits, 7, 0 );
    }
    if( is_intra )
        lsmash_bits_put( &bits, 1, 0 ); /* render_and_frame_size_different = 0 */
    else
    {
        lsmash_bits_put( &bits, 1, 0 ); /* frame_refs_short_signaling = 0 */
        for( int i = 0; i < AV1_REFS_PER_FRAME; i++ )
            lsmash_bits_put( &bits, 3, 0 ); /* ref_frame_idx[i] = 0 */
        lsmash_bits_put( &bits, 1, 0 ); /* render_and_frame_size_different = 0 */
        lsmash_bits_put( &bits, 1, 0 ); /* allow_high_precision_mv = 0 */
        lsmash_bits_put( &bits, 1, 1 ); /* is_filter_switchable = 1 */
        lsmash_bits_put( &bits, 1, 0 ); /* is_motion_mode_switchable = 0 */
    }
    /* tile_info() for 128x128 frame (sbCols=2, sbRows=2 -> maxLog2TileCols=1, maxLog2TileRows=1):
     * uniform_tile_spacing_flag=1, increment_tile_cols_log2=1 (TileCols=2), increment_tile_rows_log2=1 (TileRows=2) */
    lsmash_bits_put( &bits, 1, 1 ); /* uniform_tile_spacing_flag = 1 */
    lsmash_bits_put( &bits, 1, 1 ); /* increment_tile_cols_log2 = 1 -> TileColsLog2 = 1, TileCols = 2 */
    lsmash_bits_put( &bits, 1, 1 ); /* increment_tile_rows_log2 = 1 -> TileRowsLog2 = 1, TileRows = 2 */
    lsmash_bits_put( &bits, 2, 0 ); /* context_update_tile_id = 0 */
    lsmash_bits_put( &bits, 2, 0 ); /* tile_size_bytes_minus_1 = 0 */
    /* quantization_params(): non-lossless base_q_idx = 64 -> CodedLossless = 0, AllLossless = 0 */
    lsmash_bits_put( &bits, 8, 64 ); /* base_q_idx = 64 */
    lsmash_bits_put( &bits, 1, 0 );  /* DeltaQYDc: delta_coded = 0 */
    lsmash_bits_put( &bits, 1, 0 );  /* DeltaQUDc: delta_coded = 0 */
    lsmash_bits_put( &bits, 1, 0 );  /* DeltaQUAc: delta_coded = 0 */
    lsmash_bits_put( &bits, 1, 0 );  /* using_qmatrix = 0 */
    /* segmentation_params() */
    lsmash_bits_put( &bits, 1, 0 );  /* segmentation_enabled = 0 */
    /* delta_q_params(): base_q_idx > 0 -> delta_q_present = 0 */
    lsmash_bits_put( &bits, 1, 0 );  /* delta_q_present = 0 */
    /* loop_filter_params(): CodedLossless == 0 */
    lsmash_bits_put( &bits, 6, 0 );  /* loop_filter_level[0] = 0 */
    lsmash_bits_put( &bits, 6, 0 );  /* loop_filter_level[1] = 0 */
    lsmash_bits_put( &bits, 3, 0 );  /* loop_filter_sharpness = 0 */
    lsmash_bits_put( &bits, 1, 0 );  /* loop_filter_delta_enabled = 0 */
    /* cdef_params(): CodedLossless == 0 && enable_cdef == 1 */
    lsmash_bits_put( &bits, 2, 0 );  /* cdef_damping_minus_3 = 0 */
    lsmash_bits_put( &bits, 2, 0 );  /* cdef_bits = 0 (1 entry) */
    lsmash_bits_put( &bits, 4, 0 );  /* cdef_y_pri_strength[0] */
    lsmash_bits_put( &bits, 2, 0 );  /* cdef_y_sec_strength[0] */
    lsmash_bits_put( &bits, 4, 0 );  /* cdef_uv_pri_strength[0] */
    lsmash_bits_put( &bits, 2, 0 );  /* cdef_uv_sec_strength[0] */
    /* lr_params(): AllLossless == 0 && enable_restoration == 1 */
    lsmash_bits_put( &bits, 2, 0 );  /* lr_type[0] = RESTORE_NONE */
    lsmash_bits_put( &bits, 2, 0 );  /* lr_type[1] = RESTORE_NONE */
    lsmash_bits_put( &bits, 2, 0 );  /* lr_type[2] = RESTORE_NONE */
    /* read_tx_mode(): CodedLossless == 0 */
    lsmash_bits_put( &bits, 1, 0 );  /* tx_mode_select = 0 */
    /* frame_reference_mode() */
    if( !is_intra )
        lsmash_bits_put( &bits, 1, 0 ); /* reference_select = 0 */
    /* reduced_tx_set */
    lsmash_bits_put( &bits, 1, 0 );  /* reduced_tx_set = 0 */
    /* global_motion_params() */
    if( !is_intra )
    {
        for( int ref = 1; ref <= 7; ref++ )
            lsmash_bits_put( &bits, 1, 0 ); /* is_global[ref] = 0 */
    }
    if( obu_type == AV1_OBU_TYPE_FRAME )
    {
        /* byte_alignment() before tile_group_obu() */
        lsmash_bits_put_align( &bits );
        /* tile_group_obu() with NumTiles == 4 (tileBits == 2) */
        lsmash_bits_put( &bits, 1, present_flag ? 1 : 0 );
        if( present_flag )
        {
            lsmash_bits_put( &bits, 2, tg_start );
            lsmash_bits_put( &bits, 2, tg_end );
        }
        lsmash_bits_put_align( &bits );
        lsmash_bs_put_byte( bs, 0xA5 );
        lsmash_bs_put_byte( bs, 0x5A );
    }
    else
        put_trailing_bits( &bits );

    uint32_t payload_len = 0;
    uint8_t *payload = lsmash_bs_export_data( bs, &payload_len );
    lsmash_bs_cleanup( bs );
    uint32_t obu_len = build_obu( dst, obu_type, 0, 0, 0, payload, payload_len );
    lsmash_free( payload );
    return obu_len;
}

static uint32_t build_tile_group_obu
(
    uint8_t *dst,
    int      present_flag,
    uint32_t tg_start,
    uint32_t tg_end
)
{
    lsmash_bs_t *bs = lsmash_bs_create();
    lsmash_bits_t bits = { 0 };
    lsmash_bits_init( &bits, bs );
    lsmash_bits_put( &bits, 1, present_flag ? 1 : 0 );
    if( present_flag )
    {
        lsmash_bits_put( &bits, 2, tg_start );
        lsmash_bits_put( &bits, 2, tg_end );
    }
    lsmash_bits_put_align( &bits );
    lsmash_bs_put_byte( bs, 0x55 );
    lsmash_bs_put_byte( bs, 0xAA );
    uint32_t payload_len = 0;
    uint8_t *payload = lsmash_bs_export_data( bs, &payload_len );
    lsmash_bs_cleanup( bs );
    uint32_t obu_len = build_obu( dst, AV1_OBU_TYPE_TILE_GROUP, 0, 0, 0, payload, payload_len );
    lsmash_free( payload );
    return obu_len;
}

int test_av1_multitile_and_sync_sample_compliance( void )
{
    av1_parser_t parser;
    TEST_ASSERT( lsm_av1_setup_parser( &parser, NULL ) == 0, "setup parser for multi-tile and sync sample tests" );

    synth_seq_cfg_t seq_cfg = {
        .seq_profile = 0,
        .seq_level_idx_0 = 4,
        .width = 128,
        .height = 128,
        .color_range = 1,
        .subsampling_x = 1,
        .subsampling_y = 1,
        .enable_order_hint = 1,
        .order_hint_bits_minus_1 = 6,
    };

    uint8_t tu_buf[512];
    uint8_t out_buf[512];
    uint32_t out_len = 0;
    lsmash_sample_property_t prop = { 0 };

    /* 9a. Sync Sample (SYNC) vs Key Frame without Sequence Header in same TU (OPEN_RAP) */
    {
        /* TU 0: Sequence Header + Multi-tile OBU_FRAME (NumTiles=4, present_flag=1, tg_start=0, tg_end=3) */
        uint32_t pos = 0;
        pos += build_seq_hdr_obu( tu_buf + pos, &seq_cfg );
        pos += build_multitile_2x2_frame_obu( tu_buf + pos, AV1_OBU_TYPE_FRAME, AV1_KEY_FRAME, 1, 0xFF, 0, 1, 0, 3 );
        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse TU 0 with SH + 2x2 multi-tile OBU_FRAME (present_flag=1)" );
        TEST_ASSERT( prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC,
                     "TU with Sequence Header before Key Frame (show_frame=1) is SYNC" );
        TEST_ASSERT( parser.frame.SeenFrameHeader == 0 && parser.frame.NumTiles == 4,
                     "Multi-tile OBU_FRAME completed all 4 tiles and reset SeenFrameHeader" );

        /* TU 1: Key Frame (show_frame=1) WITHOUT Sequence Header in TU 1 -> must be OPEN_RAP, NOT SYNC! */
        pos = build_multitile_2x2_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME, AV1_KEY_FRAME, 1, 0xFF, 1, 0, 0, 3 );
        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse TU 1 with Key Frame (show_frame=1) but no SH in TU 1" );
        TEST_ASSERT( prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP,
                     "Key Frame without Sequence Header in same TU is OPEN_RAP (AV1-ISOBMFF v1.2.0 Sec 2.4)" );

        /* TU 2: Key Frame (show_frame=1) followed by Sequence Header AFTER the frame -> must be OPEN_RAP, NOT SYNC! */
        pos = build_multitile_2x2_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME, AV1_KEY_FRAME, 1, 0xFF, 2, 0, 0, 3 );
        pos += build_seq_hdr_obu( tu_buf + pos, &seq_cfg );
        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse TU 2 with Key Frame before SH" );
        TEST_ASSERT( prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP,
                     "Key Frame with Sequence Header appearing after first frame is OPEN_RAP" );
    }

    /* 9b. Split Multi-Tile: OBU_FRAME (tiles 0..1) + OBU_REDUNDANT_FRAME_HEADER + OBU_TILE_GROUP (tiles 2..3) */
    {
        uint32_t pos = 0;
        uint32_t f_len = build_multitile_2x2_frame_obu( tu_buf + pos, AV1_OBU_TYPE_FRAME, AV1_INTER_FRAME, 1, 0x02, 42, 1, 0, 1 );
        pos += f_len;
        uint32_t rfh_len = build_multitile_2x2_frame_obu( tu_buf + pos, AV1_OBU_TYPE_REDUNDANT_FRAME_HEADER, AV1_INTER_FRAME, 1, 0x02, 42, 0, 0, 0 );
        pos += rfh_len;
        uint32_t tg_len = build_tile_group_obu( tu_buf + pos, 1, 2, 3 );
        pos += tg_len;

        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse split multi-tile OBU_FRAME (0..1) + REDUNDANT_FRAME_HEADER + OBU_TILE_GROUP (2..3)" );
        TEST_ASSERT( out_len == f_len + tg_len, "REDUNDANT_FRAME_HEADER stripped from split multi-tile TU" );
        TEST_ASSERT( parser.frame.SeenFrameHeader == 0 && parser.frame.TileNum == 4,
                     "Split multi-tile frame completed all 4 tiles" );
        TEST_ASSERT( parser.RefOrderHint[1] == 42, "Reference slot 1 updated after final tile group (tg_end == 3)" );
    }

    /* 9c. Negative multi-tile conformance tests (incomplete tiles & discontinuous tg_start) */
    {
        /* Incomplete multi-tile frame at end of TU (only tiles 0..1 of 4) */
        uint32_t pos = build_multitile_2x2_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME, AV1_INTER_FRAME, 1, 0x04, 50, 1, 0, 1 );
        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == LSMASH_ERR_INVALID_DATA,
                     "reject incomplete multi-tile frame at TU end" );
        parser.frame.SeenFrameHeader = 0;

        /* Discontinuous tg_start (tiles 0..1 followed by tile 3..3, skipping tile 2) */
        pos = build_multitile_2x2_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME, AV1_INTER_FRAME, 1, 0x04, 50, 1, 0, 1 );
        pos += build_tile_group_obu( tu_buf + pos, 1, 3, 3 );
        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == LSMASH_ERR_INVALID_DATA,
                     "reject discontinuous tg_start in OBU_TILE_GROUP" );
        parser.frame.SeenFrameHeader = 0;
    }

    /* 9d. Sample OBU Whitelist (Reserved OBU stripped, TILE_LIST rejected) & Compound TU */
    {
        uint32_t pos = 0;
        pos += build_obu( tu_buf + pos, AV1_OBU_TYPE_TEMPORAL_DELIMITER, 0, 0, 0, NULL, 0 );
        uint8_t rsv_payload[3] = { 0x11, 0x22, 0x33 };
        pos += build_obu( tu_buf + pos, 10 /* reserved OBU type */, 0, 0, 0, rsv_payload, sizeof(rsv_payload) );
        /* Frame 1 in compound TU: delayed INTER_FRAME (show_frame=0, refresh=0x04 -> slot 2, OrderHint=77) */
        uint32_t f1_len = build_multitile_2x2_frame_obu( tu_buf + pos, AV1_OBU_TYPE_FRAME, AV1_INTER_FRAME, 0, 0x04, 77, 1, 0, 3 );
        pos += f1_len;
        /* Frame 2 in compound TU: FRAME_HEADER (show_existing_frame=1, idx=2) */
        uint32_t f2_len = build_frame_obu( tu_buf + pos, AV1_OBU_TYPE_FRAME_HEADER, 1, 7, 1, 2, 0, 0, 0, 0, 0 );
        pos += f2_len;

        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse compound TU with reserved OBU type 10" );
        TEST_ASSERT( out_len == f1_len + f2_len,
                     "OBU whitelist strips TD and reserved OBU type 10 while keeping FRAME and FRAME_HEADER" );
        TEST_ASSERT( parser.RefOrderHint[2] == 77, "Compound TU updated slot 2 and displayed it via show_existing_frame" );

        /* TILE_LIST OBU (type 8) must be rejected */
        uint32_t tl_len = build_obu( tu_buf, AV1_OBU_TYPE_TILE_LIST, 0, 0, 0, rsv_payload, sizeof(rsv_payload) );
        TEST_ASSERT( lsm_av1_parse_temporal_unit( &parser, tu_buf, tl_len, out_buf, &out_len, &prop, NULL ) == LSMASH_ERR_INVALID_DATA,
                     "reject OBU_TILE_LIST in AV1-ISOBMFF sample" );
    }

    lsm_av1_cleanup_parser( &parser );
    printf( "[PASS] test_av1_multitile_and_sync_sample_compliance\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 12: AV1 Fragmented ISOBMFF Cross-Feature Mux/Demux Round-Trip
 *---------------------------------------------------------------------------*/
int test_av1_fragmented_isobmff_roundtrip( void )
{
    const char *ivf_path  = "tests/tmp_test_av1_frag.ivf";
    const char *fmp4_path = "tests/tmp_test_av1_frag.mp4";

    synth_seq_cfg_t seq_cfg = {
        .seq_profile               = 0,
        .seq_level_idx_0           = 4,
        .width                     = 128,
        .height                    = 128,
        .color_description_present = 1,
        .color_primaries           = 1,
        .transfer_characteristics  = 1,
        .matrix_coefficients       = 1,
        .color_range               = 1,
        .subsampling_x             = 1,
        .subsampling_y             = 1,
        .enable_order_hint         = 1,
        .order_hint_bits_minus_1   = 6, /* OrderHintBits = 7 */
    };

    uint8_t  frame_raw[6][512];
    uint32_t frame_raw_len[6]      = { 0 };
    uint8_t  frame_expected[6][512];
    uint32_t frame_expected_len[6] = { 0 };

    /* TU 0 (PTS = 0, Initial moov sample 1):
     * TD + Sequence Header + 2x2 Multi-tile KEY_FRAME (show_frame=1, refresh=0xFF, order_hint=0) + Padding -> SYNC */
    {
        uint32_t rpos = 0, epos = 0;
        rpos += build_obu( frame_raw[0] + rpos, AV1_OBU_TYPE_TEMPORAL_DELIMITER, 0, 0, 0, NULL, 0 );
        uint32_t sh_len = build_seq_hdr_obu( frame_raw[0] + rpos, &seq_cfg );
        memcpy( frame_expected[0] + epos, frame_raw[0] + rpos, sh_len );
        rpos += sh_len;
        epos += sh_len;
        uint32_t kf_len = build_multitile_2x2_frame_obu( frame_raw[0] + rpos, AV1_OBU_TYPE_FRAME, AV1_KEY_FRAME, 1, 0xFF, 0, 1, 0, 3 );
        memcpy( frame_expected[0] + epos, frame_raw[0] + rpos, kf_len );
        rpos += kf_len;
        epos += kf_len;
        uint8_t pad[3] = { 0, 0, 0 };
        rpos += build_obu( frame_raw[0] + rpos, AV1_OBU_TYPE_PADDING, 0, 0, 0, pad, sizeof(pad) );
        frame_raw_len[0]      = rpos;
        frame_expected_len[0] = epos;
    }

    /* TU 1 (PTS = 1, Initial moov sample 2):
     * TD + Split Multi-Tile INTER_FRAME: OBU_FRAME (tiles 0..1) + OBU_REDUNDANT_FRAME_HEADER + OBU_TILE_GROUP (tiles 2..3) -> NONE */
    {
        uint32_t rpos = 0, epos = 0;
        rpos += build_obu( frame_raw[1] + rpos, AV1_OBU_TYPE_TEMPORAL_DELIMITER, 0, 0, 0, NULL, 0 );
        uint32_t f_len = build_multitile_2x2_frame_obu( frame_raw[1] + rpos, AV1_OBU_TYPE_FRAME, AV1_INTER_FRAME, 1, 0x01, 1, 1, 0, 1 );
        memcpy( frame_expected[1] + epos, frame_raw[1] + rpos, f_len );
        rpos += f_len;
        epos += f_len;
        /* Redundant Frame Header must be stripped by importer/parser */
        rpos += build_multitile_2x2_frame_obu( frame_raw[1] + rpos, AV1_OBU_TYPE_REDUNDANT_FRAME_HEADER, AV1_INTER_FRAME, 1, 0x01, 1, 0, 0, 0 );
        uint32_t tg_len = build_tile_group_obu( frame_raw[1] + rpos, 1, 2, 3 );
        memcpy( frame_expected[1] + epos, frame_raw[1] + rpos, tg_len );
        rpos += tg_len;
        epos += tg_len;
        frame_raw_len[1]      = rpos;
        frame_expected_len[1] = epos;
    }

    /* TU 2 (PTS = 2, Fragment 1 sample 1):
     * TD + Non-displayed ARF (INTER_FRAME, show_frame=0, refresh=0x02 -> slot 1, order_hint=2)
     *    + OBU_FRAME_HEADER (show_existing_frame=1, frame_to_show_map_idx=1) -> NONE */
    {
        uint32_t rpos = 0, epos = 0;
        rpos += build_obu( frame_raw[2] + rpos, AV1_OBU_TYPE_TEMPORAL_DELIMITER, 0, 0, 0, NULL, 0 );
        uint32_t arf_len = build_multitile_2x2_frame_obu( frame_raw[2] + rpos, AV1_OBU_TYPE_FRAME, AV1_INTER_FRAME, 0, 0x02, 2, 1, 0, 3 );
        memcpy( frame_expected[2] + epos, frame_raw[2] + rpos, arf_len );
        rpos += arf_len;
        epos += arf_len;
        uint32_t sef_len = build_frame_obu( frame_raw[2] + rpos, AV1_OBU_TYPE_FRAME_HEADER, 1, 7, 1, 1, 0, 0, 0, 0, 0 );
        memcpy( frame_expected[2] + epos, frame_raw[2] + rpos, sef_len );
        rpos += sef_len;
        epos += sef_len;
        frame_raw_len[2]      = rpos;
        frame_expected_len[2] = epos;
    }

    /* TU 3 (PTS = 3, Fragment 1 sample 2):
     * TD + KEY_FRAME (show_frame=1) WITHOUT Sequence Header in TU 3 -> OPEN_RAP (NOT SYNC) */
    {
        uint32_t rpos = 0, epos = 0;
        rpos += build_obu( frame_raw[3] + rpos, AV1_OBU_TYPE_TEMPORAL_DELIMITER, 0, 0, 0, NULL, 0 );
        uint32_t kf_no_sh_len = build_multitile_2x2_frame_obu( frame_raw[3] + rpos, AV1_OBU_TYPE_FRAME, AV1_KEY_FRAME, 1, 0xFF, 3, 1, 0, 3 );
        memcpy( frame_expected[3] + epos, frame_raw[3] + rpos, kf_no_sh_len );
        rpos += kf_no_sh_len;
        epos += kf_no_sh_len;
        frame_raw_len[3]      = rpos;
        frame_expected_len[3] = epos;
    }

    /* TU 4 (PTS = 4, Fragment 2 sample 1):
     * TD + INTRA_ONLY_FRAME (show_frame=1, refresh=0xFF, order_hint=4) -> OPEN_RAP */
    {
        uint32_t rpos = 0, epos = 0;
        rpos += build_obu( frame_raw[4] + rpos, AV1_OBU_TYPE_TEMPORAL_DELIMITER, 0, 0, 0, NULL, 0 );
        uint32_t io_len = build_multitile_2x2_frame_obu( frame_raw[4] + rpos, AV1_OBU_TYPE_FRAME, AV1_INTRA_ONLY_FRAME, 1, 0xFF, 4, 1, 0, 3 );
        memcpy( frame_expected[4] + epos, frame_raw[4] + rpos, io_len );
        rpos += io_len;
        epos += io_len;
        frame_raw_len[4]      = rpos;
        frame_expected_len[4] = epos;
    }

    /* TU 5 (PTS = 5, Fragment 2 sample 2):
     * TD + Sequence Header + Split Multi-Tile KEY_FRAME (tiles 0..1 in OBU_FRAME + tiles 2..3 in OBU_TILE_GROUP) -> SYNC */
    {
        uint32_t rpos = 0, epos = 0;
        rpos += build_obu( frame_raw[5] + rpos, AV1_OBU_TYPE_TEMPORAL_DELIMITER, 0, 0, 0, NULL, 0 );
        uint32_t sh_len = build_seq_hdr_obu( frame_raw[5] + rpos, &seq_cfg );
        memcpy( frame_expected[5] + epos, frame_raw[5] + rpos, sh_len );
        rpos += sh_len;
        epos += sh_len;
        uint32_t kf_part1 = build_multitile_2x2_frame_obu( frame_raw[5] + rpos, AV1_OBU_TYPE_FRAME, AV1_KEY_FRAME, 1, 0xFF, 5, 1, 0, 1 );
        memcpy( frame_expected[5] + epos, frame_raw[5] + rpos, kf_part1 );
        rpos += kf_part1;
        epos += kf_part1;
        uint32_t kf_part2 = build_tile_group_obu( frame_raw[5] + rpos, 1, 2, 3 );
        memcpy( frame_expected[5] + epos, frame_raw[5] + rpos, kf_part2 );
        rpos += kf_part2;
        epos += kf_part2;
        frame_raw_len[5]      = rpos;
        frame_expected_len[5] = epos;
    }

    /* Write synthetic 6-frame IVF file */
    FILE *fp = fopen( ivf_path, "wb" );
    TEST_ASSERT( fp != NULL, "12: create synthetic AV1 IVF file" );
    uint8_t ivf_hdr[32] = { 0 };
    memcpy( ivf_hdr, "DKIF", 4 );
    put_le16( ivf_hdr + 4, 0 );
    put_le16( ivf_hdr + 6, 32 );
    memcpy( ivf_hdr + 8, "AV01", 4 );
    put_le16( ivf_hdr + 12, 128 );
    put_le16( ivf_hdr + 14, 128 );
    put_le32( ivf_hdr + 16, 30 ); /* frame_rate */
    put_le32( ivf_hdr + 20, 1 );  /* time_scale */
    put_le32( ivf_hdr + 24, 6 );  /* number_of_frames */
    fwrite( ivf_hdr, 1, 32, fp );
    for( int i = 0; i < 6; i++ )
    {
        uint8_t fhdr[12];
        put_le32( fhdr, frame_raw_len[i] );
        put_le64( fhdr + 4, (uint64_t)i );
        fwrite( fhdr, 1, 12, fp );
        fwrite( frame_raw[i], 1, frame_raw_len[i], fp );
    }
    fclose( fp );

    /* Import IVF and Mux into a Fragmented ISOBMFF MP4:
     * - Initial moov: TU 0 (SYNC -> stss) and TU 1 (NONE)
     * - Fragment 1 (moof 1): TU 2 (ARF + show_existing_frame=1 -> NONE) and TU 3 (KEY_FRAME without SH -> OPEN_RAP)
     * - Fragment 2 (moof 2): TU 4 (INTRA_ONLY -> OPEN_RAP) and TU 5 (SH + split multi-tile KEY_FRAME -> SYNC) */
    lsmash_root_t *in_root = lsmash_create_root();
    TEST_ASSERT( in_root != NULL, "12: create in_root" );
    importer_t *importer = lsmash_importer_open( in_root, ivf_path, "auto" );
    TEST_ASSERT( importer != NULL, "12: open IVF importer" );

    lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_duplicate_summary( importer, 1 );
    TEST_ASSERT( summary != NULL && summary->width == 128 && summary->height == 128, "12: IVF summary 128x128" );

    lsmash_root_t *mux_root = lsmash_create_root();
    lsmash_file_parameters_t mux_param = { 0 };
    TEST_ASSERT( lsmash_open_file( fmp4_path, 0, &mux_param ) == 0, "12: open output fragmented MP4" );
    mux_param.mode |= LSMASH_FILE_MODE_FRAGMENTED;
    lsmash_brand_type brands[4] = {
        ISOM_BRAND_TYPE_ISOM,
        ISOM_BRAND_TYPE_ISO6,
        ISOM_BRAND_TYPE_AV01,
        ISOM_BRAND_TYPE_MP42
    };
    mux_param.major_brand   = ISOM_BRAND_TYPE_AV01;
    mux_param.brands        = brands;
    mux_param.brand_count   = 4;
    mux_param.minor_version = 0;
    TEST_ASSERT( lsmash_set_file( mux_root, &mux_param ) != NULL, "12: set fragmented MP4 file" );

    lsmash_movie_parameters_t movie_param;
    lsmash_initialize_movie_parameters( &movie_param );
    TEST_ASSERT( lsmash_set_movie_parameters( mux_root, &movie_param ) == 0, "12: set movie params" );

    uint32_t track_ID = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
    TEST_ASSERT( track_ID == 1, "12: create video track" );
    lsmash_track_parameters_t track_param;
    lsmash_initialize_track_parameters( &track_param );
    track_param.mode           = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
    track_param.display_width  = summary->width  << 16;
    track_param.display_height = summary->height << 16;
    TEST_ASSERT( lsmash_set_track_parameters( mux_root, track_ID, &track_param ) == 0, "12: set track params" );

    lsmash_media_parameters_t media_param;
    lsmash_initialize_media_parameters( &media_param );
    media_param.timescale    = summary->timescale;
    media_param.rap_grouping = 1;
    TEST_ASSERT( lsmash_set_media_parameters( mux_root, track_ID, &media_param ) == 0, "12: set media params (rap_grouping=1)" );

    uint32_t sample_entry = lsmash_add_sample_entry( mux_root, track_ID, summary );
    TEST_ASSERT( sample_entry == 1, "12: add AV01 sample entry" );

    static const lsmash_random_access_flag expected_ra[6] = {
        ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC,     /* TU 0: SH + KEY_FRAME */
        ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE,     /* TU 1: Split multi-tile INTER_FRAME */
        ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE,     /* TU 2: ARF (show_frame=0) + show_existing_frame=1 */
        ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP, /* TU 3: KEY_FRAME without SH */
        ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP, /* TU 4: INTRA_ONLY_FRAME */
        ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC,     /* TU 5: SH + Split multi-tile KEY_FRAME */
    };

    for( int i = 0; i < 6; i++ )
    {
        if( i == 2 || i == 4 )
        {
            TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, track_ID, summary->timebase ) == 0, "12: flush before new fragment" );
            TEST_ASSERT( lsmash_create_fragment_movie( mux_root ) == 0, "12: create movie fragment" );
        }
        lsmash_sample_t *sample = NULL;
        int ret = lsmash_importer_get_access_unit( importer, 1, &sample );
        TEST_ASSERT( ret == 0 && sample != NULL, "12: get_access_unit from AV1 IVF" );
        TEST_ASSERT( sample->length == frame_expected_len[i], "12: imported TU length matches expected" );
        TEST_ASSERT( memcmp( sample->data, frame_expected[i], frame_expected_len[i] ) == 0,
                     "12: imported TU payload matches expected" );
        TEST_ASSERT( sample->prop.ra_flags == expected_ra[i], "12: imported TU ra_flags match expected" );
        sample->index = sample_entry;
        sample->dts  *= summary->timebase;
        sample->cts  *= summary->timebase;
        TEST_ASSERT( lsmash_append_sample( mux_root, track_ID, sample ) == 0, "12: append sample to fragmented MP4" );
    }
    uint32_t last_delta = lsmash_importer_get_last_delta( importer, 1 );
    TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, track_ID, last_delta * summary->timebase ) == 0, "12: flush final fragment" );
    TEST_ASSERT( lsmash_finish_movie( mux_root, NULL ) == 0, "12: finish fragmented AV1 movie" );

    lsmash_cleanup_summary( (lsmash_summary_t *)summary );
    lsmash_importer_close( importer );
    lsmash_destroy_root( in_root );
    lsmash_close_file( &mux_param );
    lsmash_destroy_root( mux_root );

    /* Demux Fragmented AV1 MP4 and verify stss, trun sample_flags, traf RAP sample groups, tfra, and timeline */
    lsmash_root_t *demux_root = lsmash_create_root();
    lsmash_file_parameters_t demux_param = { 0 };
    TEST_ASSERT( lsmash_open_file( fmp4_path, 1, &demux_param ) == 0, "12: open fragmented AV1 MP4 for demux" );
    lsmash_file_t *demux_file = lsmash_set_file( demux_root, &demux_param );
    TEST_ASSERT( demux_file != NULL, "12: set demux file" );
    TEST_ASSERT( lsmash_read_file( demux_file, &demux_param ) >= 0, "12: read fragmented AV1 MP4" );

    /* Verify initial moov stss: only sample 1 (TU 0) is a sync sample */
    isom_trak_t *trak = (isom_trak_t *)demux_file->moov->trak_list.head->data;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    TEST_ASSERT( LSMASH_IS_EXISTING_BOX( stbl->stss ) && lsm_list_count( stbl->stss->list ) == 1,
                 "12: initial moov stss has 1 sync sample" );
    isom_stss_entry_t *stss0 = (isom_stss_entry_t *)lsm_list_at( stbl->stss->list, 0 );
    TEST_ASSERT( stss0 != NULL && stss0->sample_number == 1, "12: stss[0].sample_number == 1 (TU 0)" );

    /* Verify 2 moof fragments and their trun sample_flags & RAP sample groups */
    TEST_ASSERT( demux_file->moof_list.entry_count == 2, "12: 2 moof fragments present" );
    isom_moof_t *moof1 = (isom_moof_t *)demux_file->moof_list.head->data;
    isom_moof_t *moof2 = (isom_moof_t *)demux_file->moof_list.tail->data;
    isom_traf_t *traf1 = (isom_traf_t *)moof1->traf_list.head->data;
    isom_traf_t *traf2 = (isom_traf_t *)moof2->traf_list.head->data;
    isom_trun_t *trun1 = (isom_trun_t *)traf1->trun_list.head->data;
    isom_trun_t *trun2 = (isom_trun_t *)traf2->trun_list.head->data;
    TEST_ASSERT( trun1 != NULL && trun1->sample_count == 2, "12: fragment 1 trun has 2 samples (TU 2, TU 3)" );
    TEST_ASSERT( trun2 != NULL && trun2->sample_count == 2, "12: fragment 2 trun has 2 samples (TU 4, TU 5)" );
    TEST_ASSERT( traf1->sbgp_list.head != NULL, "12: fragment 1 traf has sbgp (RAP grouping for TU 3 OPEN_RAP)" );
    TEST_ASSERT( traf2->sbgp_list.head != NULL, "12: fragment 2 traf has sbgp (RAP grouping for TU 4 OPEN_RAP)" );

    /* Verify Fragment 2 trun sample flags: sample 1 (TU 4, OPEN_RAP) is non-sync; sample 2 (TU 5, SYNC) is sync! */
    isom_trex_t *trex = (isom_trex_t *)demux_file->moov->mvex->trex_list.head->data;
    isom_trun_optional_row_t *f2_row1 = (isom_trun_optional_row_t *)lsm_list_at( trun2->optional, 0 );
    isom_trun_optional_row_t *f2_row2 = (isom_trun_optional_row_t *)lsm_list_at( trun2->optional, 1 );
    TEST_ASSERT( trex != NULL && f2_row1 != NULL && f2_row2 != NULL, "12: fragment 2 trun optional rows exist" );
    isom_sample_flags_t def_flags   = (traf2->tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT)
                                    ? traf2->tfhd->default_sample_flags : trex->default_sample_flags;
    isom_sample_flags_t f2_s1_flags = (trun2->flags & ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT)
                                    ? trun2->first_sample_flags
                                    : ((trun2->flags & ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT) ? f2_row1->sample_flags : def_flags);
    isom_sample_flags_t f2_s2_flags = (trun2->flags & ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT)
                                    ? f2_row2->sample_flags : def_flags;
    TEST_ASSERT( f2_s1_flags.sample_is_non_sync_sample == 1, "12: TU 4 (INTRA_ONLY OPEN_RAP) has sample_is_non_sync_sample=1 in trun" );
    TEST_ASSERT( f2_s2_flags.sample_is_non_sync_sample == 0 && f2_s2_flags.sample_depends_on != ISOM_SAMPLE_IS_NOT_INDEPENDENT,
                 "12: TU 5 (SH + split multi-tile KEY_FRAME) has sample_is_non_sync_sample=0 in trun" );

    /* Verify timeline reconstruction across initial moov + both fragments */
    TEST_ASSERT( lsmash_construct_timeline( demux_root, 1 ) == 0, "12: construct fragmented AV1 timeline" );
    TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( demux_root, 1 ) == 6, "12: timeline has all 6 AV1 samples" );

    for( uint32_t i = 1; i <= 6; i++ )
    {
        lsmash_sample_t *s = lsmash_get_sample_from_media_timeline( demux_root, 1, i );
        TEST_ASSERT( s != NULL, "12: get sample from fragmented AV1 timeline" );
        TEST_ASSERT( s->dts == (uint64_t)(i - 1) && s->cts == (uint64_t)(i - 1), "12: sample dts/cts match" );
        TEST_ASSERT( s->length == frame_expected_len[i - 1], "12: sample length matches expected" );
        TEST_ASSERT( memcmp( s->data, frame_expected[i - 1], frame_expected_len[i - 1] ) == 0,
                     "12: sample payload matches expected bit-for-bit" );
        if( i == 1 || i == 6 )
        {
            TEST_ASSERT( (s->prop.ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC) != 0,
                         "12: TU 0 and TU 5 reconstructed as SYNC" );
        }
        else if( i == 4 || i == 5 )
        {
            TEST_ASSERT( (s->prop.ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC) == 0
                      && (s->prop.ra_flags & (ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP | ISOM_SAMPLE_RANDOM_ACCESS_FLAG_RAP)) != 0,
                         "12: TU 3 (KEY_FRAME no SH) and TU 4 (INTRA_ONLY) reconstructed as non-sync RAP" );
        }
        else
        {
            TEST_ASSERT( s->prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE,
                         "12: TU 1 (split INTER) and TU 2 (ARF + show_existing_frame) reconstructed as NONE" );
        }
        lsmash_delete_sample( s );
    }

    lsmash_destruct_timeline( demux_root, 1 );
    lsmash_close_file( &demux_param );
    lsmash_destroy_root( demux_root );

    remove( ivf_path );
    remove( fmp4_path );

    printf( "[PASS] test_av1_fragmented_isobmff_roundtrip\n" );
    return 0;
}
