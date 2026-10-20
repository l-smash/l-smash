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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "core/box.h"
#include "codecs/av1.h"
#include "importer/importer.h"

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT( cond, msg )                                                      \
    do                                                                                \
    {                                                                                 \
        ++g_tests_run;                                                                \
        if( !(cond) )                                                                 \
        {                                                                             \
            fprintf( stderr, "[FAIL] %s:%d: %s (%s)\n", __FILE__, __LINE__, msg, #cond ); \
            ++g_tests_failed;                                                         \
            return -1;                                                                \
        }                                                                             \
    } while( 0 )

/*---------------------------------------------------------------------------
 * Helper: Write an OBU header + 1-byte LEB128 size + payload into a buffer
 *---------------------------------------------------------------------------*/
static uint32_t build_obu
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

static void put_uvlc( lsmash_bits_t *bits, uint32_t val )
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

static void put_trailing_bits( lsmash_bits_t *bits )
{
    lsmash_bits_put( bits, 1, 1 );
    lsmash_bits_put_align( bits );
}

/*---------------------------------------------------------------------------
 * Test 1: Byte & Bitstream Primitives (le64, uvlc, leb128, su, ns)
 *---------------------------------------------------------------------------*/
static int test_bitstream_primitives( void )
{
    /* 1a. Verify lsmash_bs_get_le64 (regression test for << 16 vs << 32 bug) */
    {
        uint8_t raw_le64[8] = { 0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01 };
        lsmash_bs_t bs = { 0 };
        TEST_ASSERT( lsmash_bs_set_empty_stream( &bs, raw_le64, sizeof(raw_le64) ) == 0, "init le64 bs" );
        uint64_t val = lsmash_bs_get_le64( &bs );
        TEST_ASSERT( val == 0x0123456789ABCDEFULL, "lsmash_bs_get_le64 64-bit shift" );
    }

    /* 1b. Verify av1_get_uvlc across standard, boundary, and error cases */
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        TEST_ASSERT( bs != NULL, "create bs for uvlc" );
        lsmash_bits_t bits = { 0 };
        lsmash_bits_init( &bits, bs );

        put_uvlc( &bits, 0 );
        put_uvlc( &bits, 1 );
        put_uvlc( &bits, 2 );
        put_uvlc( &bits, 3 );
        put_uvlc( &bits, 6 );
        put_uvlc( &bits, 7 );
        put_uvlc( &bits, 42 );
        /* Max valid 32-bit uvlc value: 31 zeros + '1' + 31 ones -> 0xFFFFFFFE */
        for( int i = 0; i < 31; i++ )
            lsmash_bits_put( &bits, 1, 0 );
        lsmash_bits_put( &bits, 1, 1 );
        lsmash_bits_put( &bits, 31, 0x7FFFFFFFULL );
        /* leadingZeros >= 32 -> returns UINT32_MAX */
        for( int i = 0; i < 32; i++ )
            lsmash_bits_put( &bits, 1, 0 );
        lsmash_bits_put( &bits, 1, 1 );
        lsmash_bits_put_align( &bits );

        uint32_t buf_len = 0;
        uint8_t *buf = lsmash_bs_export_data( bs, &buf_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( buf != NULL && buf_len > 0, "export uvlc bitstream" );

        lsmash_bs_t rbs = { 0 };
        lsmash_bs_set_empty_stream( &rbs, buf, buf_len );
        lsmash_bits_t rbits = { 0 };
        lsmash_bits_init( &rbits, &rbs );

        TEST_ASSERT( av1_get_uvlc( &rbits ) == 0, "uvlc(0)" );
        TEST_ASSERT( av1_get_uvlc( &rbits ) == 1, "uvlc(1)" );
        TEST_ASSERT( av1_get_uvlc( &rbits ) == 2, "uvlc(2)" );
        TEST_ASSERT( av1_get_uvlc( &rbits ) == 3, "uvlc(3)" );
        TEST_ASSERT( av1_get_uvlc( &rbits ) == 6, "uvlc(6)" );
        TEST_ASSERT( av1_get_uvlc( &rbits ) == 7, "uvlc(7)" );
        TEST_ASSERT( av1_get_uvlc( &rbits ) == 42, "uvlc(42)" );
        TEST_ASSERT( av1_get_uvlc( &rbits ) == 0xFFFFFFFEU, "uvlc(0xFFFFFFFE max valid)" );
        TEST_ASSERT( av1_get_uvlc( &rbits ) == UINT32_MAX, "uvlc(leadingZeros >= 32)" );
        lsmash_free( buf );

        /* Truncated all-zero stream at EOF should not hang and must return UINT32_MAX */
        uint8_t zero_byte[1] = { 0x00 };
        lsmash_bs_set_empty_stream( &rbs, zero_byte, 1 );
        lsmash_bits_init( &rbits, &rbs );
        TEST_ASSERT( av1_get_uvlc( &rbits ) == UINT32_MAX, "uvlc truncated zero stream" );
    }

    /* 1c. Verify av1_get_leb128 and av1_show_leb128 */
    {
        /* 1-byte: 0 and 127 */
        uint8_t b1[2] = { 0x00, 0x7F };
        lsmash_bs_t bs = { 0 };
        uint8_t nbytes = 0;
        lsmash_bs_set_empty_stream( &bs, b1, sizeof(b1) );
        TEST_ASSERT( av1_show_leb128( &bs, 0, &nbytes ) == 0 && nbytes == 1, "show_leb128(0)" );
        TEST_ASSERT( av1_show_leb128( &bs, 1, &nbytes ) == 127 && nbytes == 1, "show_leb128(127)" );
        TEST_ASSERT( av1_get_leb128( &bs, &nbytes ) == 0 && nbytes == 1, "get_leb128(0)" );
        TEST_ASSERT( av1_get_leb128( &bs, &nbytes ) == 127 && nbytes == 1, "get_leb128(127)" );

        /* 5-byte: 0xFFFFFFFF (tests i = 4 shift into bits 28..31 without 32-bit signed UB) */
        uint8_t b_max32[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x0F };
        lsmash_bs_set_empty_stream( &bs, b_max32, sizeof(b_max32) );
        TEST_ASSERT( av1_show_leb128( &bs, 0, &nbytes ) == 0xFFFFFFFFULL && nbytes == 5, "show_leb128(UINT32_MAX)" );
        TEST_ASSERT( av1_get_leb128( &bs, &nbytes ) == 0xFFFFFFFFULL && nbytes == 5, "get_leb128(UINT32_MAX)" );

        /* 8-byte padded representation of 0xFFFFFFFF (legal 8-byte LEB128 <= UINT32_MAX) */
        uint8_t b_pad8[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x8F, 0x80, 0x80, 0x00 };
        lsmash_bs_set_empty_stream( &bs, b_pad8, sizeof(b_pad8) );
        TEST_ASSERT( av1_show_leb128( &bs, 0, &nbytes ) == 0xFFFFFFFFULL && nbytes == 8, "show_leb128 8-byte padded" );
        TEST_ASSERT( av1_get_leb128( &bs, &nbytes ) == 0xFFFFFFFFULL && nbytes == 8, "get_leb128 8-byte padded" );

        /* Overflow > UINT32_MAX: 0x100000000 (1ULL << 32) -> must fail with UINT64_MAX and nbytes == 0 */
        uint8_t b_over32[5] = { 0x80, 0x80, 0x80, 0x80, 0x10 };
        lsmash_bs_set_empty_stream( &bs, b_over32, sizeof(b_over32) );
        TEST_ASSERT( av1_show_leb128( &bs, 0, &nbytes ) == UINT64_MAX && nbytes == 0, "show_leb128 > UINT32_MAX rejected" );
        TEST_ASSERT( av1_get_leb128( &bs, &nbytes ) == UINT64_MAX && nbytes == 0, "get_leb128 > UINT32_MAX rejected" );

        /* > 8 bytes (8th byte still has MSB 0x80 set) -> must fail */
        uint8_t b_9bytes[9] = { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00 };
        lsmash_bs_set_empty_stream( &bs, b_9bytes, sizeof(b_9bytes) );
        TEST_ASSERT( av1_show_leb128( &bs, 0, &nbytes ) == UINT64_MAX && nbytes == 0, "show_leb128 > 8 bytes rejected" );
        TEST_ASSERT( av1_get_leb128( &bs, &nbytes ) == UINT64_MAX && nbytes == 0, "get_leb128 > 8 bytes rejected" );

        /* Truncated LEB128 at EOF -> must fail */
        uint8_t b_trunc[2] = { 0x81, 0x82 };
        lsmash_bs_set_empty_stream( &bs, b_trunc, sizeof(b_trunc) );
        TEST_ASSERT( av1_show_leb128( &bs, 0, &nbytes ) == UINT64_MAX && nbytes == 0, "show_leb128 truncated rejected" );
        TEST_ASSERT( av1_get_leb128( &bs, &nbytes ) == UINT64_MAX && nbytes == 0, "get_leb128 truncated rejected" );
    }

    printf( "[PASS] test_bitstream_primitives\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Helper: Build a synthetic Sequence Header OBU payload
 *---------------------------------------------------------------------------*/
typedef struct
{
    uint8_t  seq_profile;
    uint8_t  seq_level_idx_0;
    uint8_t  seq_tier_0;
    uint32_t width;
    uint32_t height;
    uint8_t  high_bitdepth;
    uint8_t  twelve_bit;
    uint8_t  mono_chrome;
    uint8_t  color_description_present;
    uint8_t  color_primaries;
    uint8_t  transfer_characteristics;
    uint8_t  matrix_coefficients;
    uint8_t  color_range;
    uint8_t  subsampling_x;
    uint8_t  subsampling_y;
    uint8_t  chroma_sample_position;
    uint8_t  enable_order_hint;
    uint8_t  order_hint_bits_minus_1;
    uint8_t  initial_display_delay_present;
    uint8_t  initial_display_delay_minus_1;
    uint8_t  timing_info_present;
} synth_seq_cfg_t;

static uint32_t build_seq_hdr_obu( uint8_t *dst, const synth_seq_cfg_t *cfg )
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
 * Test 2: Sequence Header OBU (8/10/12-bit, BT.709 vs sRGB) & av1C Round-Trip
 *---------------------------------------------------------------------------*/
static int test_sequence_header_and_av1c( void )
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
        TEST_ASSERT( av1_construct_specific_parameters( &dst_cs, &src_cs ) == 0, "reconstruct av1C" );
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
 * Helper: Build synthetic Frame / FrameHeader / TileGroup OBUs
 *---------------------------------------------------------------------------*/
static uint32_t build_frame_obu
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
        /* Dummy quantization/tile payload byte */
        lsmash_bits_put_align( &bits );
        lsmash_bs_put_byte( bs, 0xAC );
    }

    uint32_t payload_len = 0;
    uint8_t *payload = lsmash_bs_export_data( bs, &payload_len );
    lsmash_bs_cleanup( bs );
    uint32_t obu_len = build_obu( dst, obu_type, 0, 0, 0, payload, payload_len );
    lsmash_free( payload );
    return obu_len;
}

/*---------------------------------------------------------------------------
 * Test 3: Frame Types, Reference Updates, show_existing_frame, OBU Stripping
 *---------------------------------------------------------------------------*/
static int test_frame_headers_and_tu_assembly( void )
{
    av1_parser_t parser;
    TEST_ASSERT( av1_setup_parser( &parser, NULL ) == 0, "setup parser" );

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

        TEST_ASSERT( av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, &param ) == 0,
                     "parse TU 0 (delayed KEY_FRAME + TD + Padding + HDR10+)" );
        /* TD and Padding must be stripped; SH + Metadata + Frame must remain */
        TEST_ASSERT( out_len == sh_len + meta_len + frm_len, "TD and Padding stripped from TU 0" );
        TEST_ASSERT( param.has_hdr10p == 1, "HDR10+ ITU-T T.35 detected" );
        TEST_ASSERT( prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP, "Delayed KEY_FRAME marked as OPEN_RAP" );
        TEST_ASSERT( parser.RefValid[0] == 1 && parser.RefValid[1] == 0, "Only RefValid[0] refreshed by 0x01" );
        TEST_ASSERT( parser.RefFrameType[0] == AV1_KEY_FRAME && parser.RefOrderHint[0] == 10, "Slot 0 stores KEY_FRAME" );
    }

    /* TU 1: INTER_FRAME (show_frame=1, refresh_frame_flags=0x04 -> slot 2) */
    {
        uint32_t pos = build_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME, 1, 7, 0, 0, AV1_INTER_FRAME, 1, 0, 0x04, 11 );
        TEST_ASSERT( av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse TU 1 (INTER_FRAME)" );
        TEST_ASSERT( prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE, "INTER_FRAME is not RAP" );
        TEST_ASSERT( parser.RefValid[2] == 1 && parser.RefOrderHint[2] == 11, "Slot 2 refreshed by 8-bit refresh_frame_flags=0x04" );
    }

    /* TU 2: INTRA_ONLY_FRAME (show_frame=1, refresh_frame_flags=0x08 -> slot 3) */
    {
        uint32_t pos = build_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME, 1, 7, 0, 0, AV1_INTRA_ONLY_FRAME, 1, 0, 0x08, 12 );
        TEST_ASSERT( av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse TU 2 (INTRA_ONLY_FRAME)" );
        TEST_ASSERT( prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP, "INTRA_ONLY_FRAME marked as OPEN_RAP" );
        TEST_ASSERT( parser.RefValid[3] == 1 && parser.RefFrameType[3] == AV1_INTRA_ONLY_FRAME,
                     "Slot 3 refreshed by INTRA_ONLY_FRAME" );
    }

    /* TU 3: SWITCH_FRAME (show_frame=1 -> refreshes all 8 slots) */
    {
        uint32_t pos = build_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME, 1, 7, 0, 0, AV1_SWITCH_FRAME, 1, 0, 0xFF, 13 );
        TEST_ASSERT( av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse TU 3 (SWITCH_FRAME)" );
        TEST_ASSERT( prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP, "SWITCH_FRAME marked as OPEN_RAP" );
        for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
            TEST_ASSERT( parser.RefValid[i] == 1 && parser.RefOrderHint[i] == 13, "All slots refreshed by SWITCH_FRAME" );
    }

    /* Re-store a delayed KEY_FRAME into slot 0 (with OrderHint=20), then test show_existing_frame=1 pointing to slot 0 */
    {
        uint32_t pos = build_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME, 1, 7, 0, 0, AV1_KEY_FRAME, 0, 1, 0x01, 20 );
        TEST_ASSERT( av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse delayed KEY_FRAME into slot 0" );
        TEST_ASSERT( parser.RefOrderHint[0] == 20 && parser.RefOrderHint[1] == 13, "Slot 0 has OrderHint 20, Slot 1 has 13" );

        /* Now send OBU_FRAME_HEADER with show_existing_frame=1, frame_to_show_map_idx=0.
         * Since RefFrameType[0] == AV1_KEY_FRAME, it must refresh all 8 reference slots with slot 0's state! */
        pos = build_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME_HEADER, 1, 7, 1, 0, 0, 0, 0, 0, 0 );
        TEST_ASSERT( av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse show_existing_frame=1 pointing to slot 0 KEY_FRAME" );
        for( int i = 0; i < AV1_NUM_REF_FRAMES; i++ )
            TEST_ASSERT( parser.RefValid[i] == 1 && parser.RefOrderHint[i] == 20 && parser.RefFrameType[i] == AV1_KEY_FRAME,
                         "show_existing_frame on KEY_FRAME refreshed all 8 reference slots" );
    }

    /* Also test enable_order_hint == 0 stream (regression test for unguarded order_hint read) */
    {
        av1_reset_parser( &parser );
        synth_seq_cfg_t no_oh_cfg = seq_cfg;
        no_oh_cfg.enable_order_hint = 0;
        uint32_t pos = build_seq_hdr_obu( tu_buf, &no_oh_cfg );
        pos += build_frame_obu( tu_buf + pos, AV1_OBU_TYPE_FRAME, 0, 0, 0, 0, AV1_KEY_FRAME, 1, 0, 0xFF, 0 );
        TEST_ASSERT( av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse KEY_FRAME with enable_order_hint=0" );
        TEST_ASSERT( prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC, "KEY_FRAME + show_frame=1 is SYNC" );

        pos = build_frame_obu( tu_buf, AV1_OBU_TYPE_FRAME, 0, 0, 0, 0, AV1_INTER_FRAME, 1, 0, 0x02, 0 );
        TEST_ASSERT( av1_parse_temporal_unit( &parser, tu_buf, pos, out_buf, &out_len, &prop, NULL ) == 0,
                     "parse INTER_FRAME with enable_order_hint=0" );
        TEST_ASSERT( parser.frame.order_hint == 0 && parser.RefValid[1] == 1, "INTER_FRAME parsed cleanly with enable_order_hint=0" );
    }

    lsmash_destroy_av1_headers( &param );
    av1_cleanup_parser( &parser );
    printf( "[PASS] test_frame_headers_and_tu_assembly\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 4: Synthetic IVF File Import & MP4 Mux/Demux End-to-End Round-Trip
 *---------------------------------------------------------------------------*/
static void put_le16( uint8_t *p, uint16_t v )
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put_le32( uint8_t *p, uint32_t v )
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void put_le64( uint8_t *p, uint64_t v )
{
    put_le32( p, (uint32_t)(v & 0xFFFFFFFFULL) );
    put_le32( p + 4, (uint32_t)(v >> 32) );
}

static int test_ivf_mux_demux_roundtrip( void )
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
        int has_av01 = (nread > 0 && strstr( dump_buf, "[av01: Visual Description]" ) != NULL);
        int has_av1c = (nread > 0 && strstr( dump_buf, "[av1C: AV1 Configuration Box]" ) != NULL);
        free( dump_buf );
        TEST_ASSERT( has_av01, "box dump contains [av01: Visual Description]" );
        TEST_ASSERT( has_av1c, "box dump contains [av1C: AV1 Configuration Box]" );
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
 * Test 5: C99 Self-Contained Memory Audit Allocator Verification
 *---------------------------------------------------------------------------*/
static int test_memory_audit_allocator( void )
{
#ifdef LSMASH_DEBUG_ALLOC
    TEST_ASSERT( lsm_alloc_get_active_count() == 0, "initial active block count == 0" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 0, "initial active byte count == 0" );

    uint8_t *buf1 = (uint8_t *)lsmash_malloc( 128 );
    TEST_ASSERT( buf1 != NULL, "lsmash_malloc(128) succeeds" );
    TEST_ASSERT( ((uintptr_t)buf1 & 0xF) == 0, "lsmash_malloc returns 16-byte aligned pointer" );
    TEST_ASSERT( buf1[0] == 0xCD && buf1[127] == 0xCD, "lsmash_malloc poisons uninitialized memory with 0xCD" );
    TEST_ASSERT( lsm_alloc_get_active_count() == 1, "active block count == 1" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 128, "active byte count == 128" );

    uint8_t *buf2 = (uint8_t *)lsmash_malloc_zero( 64 );
    TEST_ASSERT( buf2 != NULL, "lsmash_malloc_zero(64) succeeds" );
    for( int i = 0; i < 64; i++ )
        if( buf2[i] != 0 )
            TEST_ASSERT( 0, "lsmash_malloc_zero zeroes all bytes" );
    TEST_ASSERT( lsm_alloc_get_active_count() == 2, "active block count == 2" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 192, "active byte count == 192" );

    for( int i = 0; i < 64; i++ )
        buf2[i] = (uint8_t)(i + 1);
    uint8_t *buf3 = (uint8_t *)lsmash_memdup( buf2, 64 );
    TEST_ASSERT( buf3 != NULL, "lsmash_memdup(buf2, 64) succeeds" );
    TEST_ASSERT( memcmp( buf2, buf3, 64 ) == 0, "lsmash_memdup copies payload" );
    TEST_ASSERT( lsm_alloc_get_active_count() == 3, "active block count == 3" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 256, "active byte count == 256" );

    buf1 = (uint8_t *)lsmash_realloc( buf1, 256 );
    TEST_ASSERT( buf1 != NULL, "lsmash_realloc(buf1, 256) succeeds" );
    TEST_ASSERT( lsm_alloc_get_active_count() == 3, "active block count == 3 after realloc" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 384, "active byte count == 384 after realloc" );

    lsmash_free( buf1 );
    TEST_ASSERT( lsm_alloc_get_active_count() == 2, "active block count == 2 after lsmash_free" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 128, "active byte count == 128 after lsmash_free" );

    lsmash_freep( &buf2 );
    TEST_ASSERT( buf2 == NULL, "lsmash_freep sets pointer to NULL" );
    TEST_ASSERT( lsm_alloc_get_active_count() == 1, "active block count == 1 after lsmash_freep" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 64, "active byte count == 64 after lsmash_freep" );

    lsmash_freep( &buf3 );
    TEST_ASSERT( buf3 == NULL, "lsmash_freep(buf3) sets pointer to NULL" );
    TEST_ASSERT( lsm_alloc_get_active_count() == 0, "active block count == 0 after all frees" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 0, "active byte count == 0 after all frees" );
    TEST_ASSERT( lsm_alloc_verify_no_leaks() == 0, "lsm_alloc_verify_no_leaks() == 0" );
#endif
    printf( "[PASS] test_memory_audit_allocator\n" );
    return 0;
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
    RUN_TEST( test_bitstream_primitives );
    RUN_TEST( test_sequence_header_and_av1c );
    RUN_TEST( test_frame_headers_and_tu_assembly );
    RUN_TEST( test_ivf_mux_demux_roundtrip );
#ifdef LSMASH_DEBUG_ALLOC
    assert( lsm_alloc_verify_no_leaks() == 0 );
    assert( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0 );
#endif
    printf( "=== Assertions run: %d, Failures: %d ===\n", g_tests_run, g_tests_failed );
    return g_tests_failed == 0 ? 0 : 1;
}
