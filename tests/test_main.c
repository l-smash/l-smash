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
#include "codecs/nalu.h"
#include "importer/importer.h"

int lsm_qt_wfex_update_bitrate( isom_stbl_t *stbl, isom_mdhd_t *mdhd, uint32_t sample_description_index );
int lsm_wma_print_codec_specific( FILE *fp, lsmash_file_t *file, isom_box_t *box, int level );
int lsm_opus_print_codec_specific( FILE *fp, lsmash_file_t *file, isom_box_t *box, int level );

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

        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 0, "uvlc(0)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 1, "uvlc(1)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 2, "uvlc(2)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 3, "uvlc(3)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 6, "uvlc(6)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 7, "uvlc(7)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 42, "uvlc(42)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 0xFFFFFFFEU, "uvlc(0xFFFFFFFE max valid)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == UINT32_MAX, "uvlc(leadingZeros >= 32)" );
        lsmash_free( buf );

        /* Truncated all-zero stream at EOF should not hang and must return UINT32_MAX */
        uint8_t zero_byte[1] = { 0x00 };
        lsmash_bs_set_empty_stream( &rbs, zero_byte, 1 );
        lsmash_bits_init( &rbits, &rbs );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == UINT32_MAX, "uvlc truncated zero stream" );
    }

    /* 1c. Verify lsm_av1_get_leb128 and lsm_av1_show_leb128 */
    {
        /* 1-byte: 0 and 127 */
        uint8_t b1[2] = { 0x00, 0x7F };
        lsmash_bs_t bs = { 0 };
        uint8_t nbytes = 0;
        lsmash_bs_set_empty_stream( &bs, b1, sizeof(b1) );
        TEST_ASSERT( lsm_av1_show_leb128( &bs, 0, &nbytes ) == 0 && nbytes == 1, "show_leb128(0)" );
        TEST_ASSERT( lsm_av1_show_leb128( &bs, 1, &nbytes ) == 127 && nbytes == 1, "show_leb128(127)" );
        TEST_ASSERT( lsm_av1_get_leb128( &bs, &nbytes ) == 0 && nbytes == 1, "get_leb128(0)" );
        TEST_ASSERT( lsm_av1_get_leb128( &bs, &nbytes ) == 127 && nbytes == 1, "get_leb128(127)" );

        /* 5-byte: 0xFFFFFFFF (tests i = 4 shift into bits 28..31 without 32-bit signed UB) */
        uint8_t b_max32[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x0F };
        lsmash_bs_set_empty_stream( &bs, b_max32, sizeof(b_max32) );
        TEST_ASSERT( lsm_av1_show_leb128( &bs, 0, &nbytes ) == 0xFFFFFFFFULL && nbytes == 5, "show_leb128(UINT32_MAX)" );
        TEST_ASSERT( lsm_av1_get_leb128( &bs, &nbytes ) == 0xFFFFFFFFULL && nbytes == 5, "get_leb128(UINT32_MAX)" );

        /* 8-byte padded representation of 0xFFFFFFFF (legal 8-byte LEB128 <= UINT32_MAX) */
        uint8_t b_pad8[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x8F, 0x80, 0x80, 0x00 };
        lsmash_bs_set_empty_stream( &bs, b_pad8, sizeof(b_pad8) );
        TEST_ASSERT( lsm_av1_show_leb128( &bs, 0, &nbytes ) == 0xFFFFFFFFULL && nbytes == 8, "show_leb128 8-byte padded" );
        TEST_ASSERT( lsm_av1_get_leb128( &bs, &nbytes ) == 0xFFFFFFFFULL && nbytes == 8, "get_leb128 8-byte padded" );

        /* Overflow > UINT32_MAX: 0x100000000 (1ULL << 32) -> must fail with UINT64_MAX and nbytes == 0 */
        uint8_t b_over32[5] = { 0x80, 0x80, 0x80, 0x80, 0x10 };
        lsmash_bs_set_empty_stream( &bs, b_over32, sizeof(b_over32) );
        TEST_ASSERT( lsm_av1_show_leb128( &bs, 0, &nbytes ) == UINT64_MAX && nbytes == 0, "show_leb128 > UINT32_MAX rejected" );
        TEST_ASSERT( lsm_av1_get_leb128( &bs, &nbytes ) == UINT64_MAX && nbytes == 0, "get_leb128 > UINT32_MAX rejected" );

        /* > 8 bytes (8th byte still has MSB 0x80 set) -> must fail */
        uint8_t b_9bytes[9] = { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00 };
        lsmash_bs_set_empty_stream( &bs, b_9bytes, sizeof(b_9bytes) );
        TEST_ASSERT( lsm_av1_show_leb128( &bs, 0, &nbytes ) == UINT64_MAX && nbytes == 0, "show_leb128 > 8 bytes rejected" );
        TEST_ASSERT( lsm_av1_get_leb128( &bs, &nbytes ) == UINT64_MAX && nbytes == 0, "get_leb128 > 8 bytes rejected" );

        /* Truncated LEB128 at EOF -> must fail */
        uint8_t b_trunc[2] = { 0x81, 0x82 };
        lsmash_bs_set_empty_stream( &bs, b_trunc, sizeof(b_trunc) );
        TEST_ASSERT( lsm_av1_show_leb128( &bs, 0, &nbytes ) == UINT64_MAX && nbytes == 0, "show_leb128 truncated rejected" );
        TEST_ASSERT( lsm_av1_get_leb128( &bs, &nbytes ) == UINT64_MAX && nbytes == 0, "get_leb128 truncated rejected" );
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

/*---------------------------------------------------------------------------
 * Test 3: Frame Types, Reference Updates, show_existing_frame, OBU Stripping
 *---------------------------------------------------------------------------*/
static int test_frame_headers_and_tu_assembly( void )
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
 * Test 5: Self-Contained Memory Audit Allocator Verification
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

/*---------------------------------------------------------------------------
 * Test 6: ISO/IEC 23003-5 ipcm/fpcm/pcmC + ISO/IEC 14496-12 chnl/srat Round-Trip
 *---------------------------------------------------------------------------*/
static int test_lpcm_pcmc_chnl_srat_roundtrip( void )
{
    const char *mp4_path  = "tests/tmp_test_lpcm_isobmff.mp4";
    const char *dump_path = "tests/tmp_test_lpcm_dump.txt";

    lsmash_root_t *mux_root = lsmash_create_root();
    TEST_ASSERT( mux_root != NULL, "create mux root" );

    lsmash_file_parameters_t mux_param = { 0 };
    TEST_ASSERT( lsmash_open_file( mp4_path, 0, &mux_param ) == 0, "open output mp4" );
    mux_param.major_brand   = ISOM_BRAND_TYPE_ISOM;
    mux_param.minor_version = 512;
    lsmash_brand_type brands[2] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_ISO2 };
    mux_param.brands      = brands;
    mux_param.brand_count = 2;
    TEST_ASSERT( lsmash_set_file( mux_root, &mux_param ) != NULL, "set output file" );

    lsmash_movie_parameters_t movie_param;
    lsmash_initialize_movie_parameters( &movie_param );
    TEST_ASSERT( lsmash_set_movie_parameters( mux_root, &movie_param ) == 0, "set movie params" );

    uint32_t track_id[4];
    lsmash_audio_summary_t *orig_summary[4] = { NULL, NULL, NULL, NULL };

    for( int t = 0; t < 4; t++ )
    {
        track_id[t] = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
        TEST_ASSERT( track_id[t] != 0, "create audio track" );

        lsmash_track_parameters_t track_param;
        lsmash_initialize_track_parameters( &track_param );
        track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
        TEST_ASSERT( lsmash_set_track_parameters( mux_root, track_id[t], &track_param ) == 0, "set track params" );
    }

    /* 6a. ISO/IEC 23003-5 validation checks: reject invalid PCM_sample_size or missing pcmC */
    {
        lsmash_audio_summary_t *bad_sum = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
        TEST_ASSERT( bad_sum != NULL, "create bad_sum" );
        bad_sum->sample_type = ISOM_CODEC_TYPE_IPCM_AUDIO;
        bad_sum->frequency   = 48000;
        bad_sum->channels    = 2;
        /* Missing pcmC must be rejected */
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, track_id[0], bad_sum ) == 0,
                     "reject ipcm without pcmC" );

        /* Invalid 8-bit sample size for ipcm must be rejected */
        lsmash_codec_specific_t *bad_pcmc = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_PCM_CONFIG,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        TEST_ASSERT( bad_pcmc != NULL, "create bad_pcmc" );
        lsmash_isom_pcm_config_t *cfg = (lsmash_isom_pcm_config_t *)bad_pcmc->data.structured;
        cfg->format_flags    = LSMASH_PCM_FORMAT_FLAG_LITTLE_ENDIAN;
        cfg->PCM_sample_size = 8;
        TEST_ASSERT( lsmash_add_codec_specific_data( (lsmash_summary_t *)bad_sum, bad_pcmc ) == 0, "add bad_pcmc" );
        lsmash_destroy_codec_specific_data( bad_pcmc );
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, track_id[0], bad_sum ) == 0,
                     "reject 8-bit ipcm" );

        /* Invalid 24-bit sample size for fpcm must be rejected */
        bad_sum->sample_type = ISOM_CODEC_TYPE_FPCM_AUDIO;
        cfg = (lsmash_isom_pcm_config_t *)lsmash_get_codec_specific_data( (lsmash_summary_t *)bad_sum, 1 )->data.structured;
        cfg->PCM_sample_size = 24;
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, track_id[0], bad_sum ) == 0,
                     "reject 24-bit fpcm" );
        lsmash_cleanup_summary( (lsmash_summary_t *)bad_sum );
    }

    /* Track 0: ipcm 16-bit Big-Endian, 48000 Hz, 1ch Mono (chnl definedLayout=1) */
    {
        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale = 48000;
        TEST_ASSERT( lsmash_set_media_parameters( mux_root, track_id[0], &media_param ) == 0, "set media params t0" );

        orig_summary[0] = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
        orig_summary[0]->sample_type      = ISOM_CODEC_TYPE_IPCM_AUDIO;
        orig_summary[0]->frequency        = 48000;
        orig_summary[0]->channels         = 1;
        orig_summary[0]->sample_size      = 16;
        orig_summary[0]->samples_in_frame = 1;
        orig_summary[0]->bytes_per_frame  = 2;

        lsmash_codec_specific_t *pcmc = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_PCM_CONFIG,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        lsmash_isom_pcm_config_t *cfg = (lsmash_isom_pcm_config_t *)pcmc->data.structured;
        cfg->format_flags    = LSMASH_PCM_FORMAT_FLAG_BIG_ENDIAN;
        cfg->PCM_sample_size = 16;
        TEST_ASSERT( lsmash_add_codec_specific_data( (lsmash_summary_t *)orig_summary[0], pcmc ) == 0, "add pcmc t0" );
        lsmash_destroy_codec_specific_data( pcmc );

        lsmash_codec_specific_t *chnl = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_CHANNEL_LAYOUT,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        lsmash_isom_channel_layout_t *cl = (lsmash_isom_channel_layout_t *)chnl->data.structured;
        cl->stream_structure   = LSMASH_CHANNEL_STREAM_STRUCTURE_CHANNEL;
        cl->definedLayout      = 1;
        cl->omittedChannelsMap = 0;
        cl->channel_count      = 1;
        TEST_ASSERT( lsmash_add_codec_specific_data( (lsmash_summary_t *)orig_summary[0], chnl ) == 0, "add chnl t0" );
        lsmash_destroy_codec_specific_data( chnl );

        TEST_ASSERT( lsmash_add_sample_entry( mux_root, track_id[0], orig_summary[0] ) == 1, "add sample entry t0" );
    }

    /* Track 1: ipcm 24-bit Little-Endian, 96000 Hz (>65535 -> AudioSampleEntryV1 + srat), 2ch Stereo (definedLayout=2) */
    {
        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale = 96000;
        TEST_ASSERT( lsmash_set_media_parameters( mux_root, track_id[1], &media_param ) == 0, "set media params t1" );

        orig_summary[1] = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
        orig_summary[1]->sample_type      = ISOM_CODEC_TYPE_IPCM_AUDIO;
        orig_summary[1]->frequency        = 96000;
        orig_summary[1]->channels         = 2;
        orig_summary[1]->sample_size      = 24;
        orig_summary[1]->samples_in_frame = 1;
        orig_summary[1]->bytes_per_frame  = 6;

        lsmash_codec_specific_t *pcmc = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_PCM_CONFIG,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        lsmash_isom_pcm_config_t *cfg = (lsmash_isom_pcm_config_t *)pcmc->data.structured;
        cfg->format_flags    = LSMASH_PCM_FORMAT_FLAG_LITTLE_ENDIAN;
        cfg->PCM_sample_size = 24;
        TEST_ASSERT( lsmash_add_codec_specific_data( (lsmash_summary_t *)orig_summary[1], pcmc ) == 0, "add pcmc t1" );
        lsmash_destroy_codec_specific_data( pcmc );

        lsmash_codec_specific_t *chnl = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_CHANNEL_LAYOUT,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        lsmash_isom_channel_layout_t *cl = (lsmash_isom_channel_layout_t *)chnl->data.structured;
        cl->stream_structure   = LSMASH_CHANNEL_STREAM_STRUCTURE_CHANNEL;
        cl->definedLayout      = 2;
        cl->omittedChannelsMap = 0;
        cl->channel_count      = 2;
        TEST_ASSERT( lsmash_add_codec_specific_data( (lsmash_summary_t *)orig_summary[1], chnl ) == 0, "add chnl t1" );
        lsmash_destroy_codec_specific_data( chnl );

        TEST_ASSERT( lsmash_add_sample_entry( mux_root, track_id[1], orig_summary[1] ) == 1, "add sample entry t1" );
    }

    /* Track 2: fpcm 32-bit LE, 192000 Hz (>65535 -> AudioSampleEntryV1 + srat), 3ch explicit speaker_position=126 + objects */
    {
        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale = 192000;
        TEST_ASSERT( lsmash_set_media_parameters( mux_root, track_id[2], &media_param ) == 0, "set media params t2" );

        orig_summary[2] = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
        orig_summary[2]->sample_type      = ISOM_CODEC_TYPE_FPCM_AUDIO;
        orig_summary[2]->frequency        = 192000;
        orig_summary[2]->channels         = 3;
        orig_summary[2]->sample_size      = 32;
        orig_summary[2]->samples_in_frame = 1;
        orig_summary[2]->bytes_per_frame  = 12;

        lsmash_codec_specific_t *pcmc = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_PCM_CONFIG,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        lsmash_isom_pcm_config_t *cfg = (lsmash_isom_pcm_config_t *)pcmc->data.structured;
        cfg->format_flags    = LSMASH_PCM_FORMAT_FLAG_LITTLE_ENDIAN;
        cfg->PCM_sample_size = 32;
        TEST_ASSERT( lsmash_add_codec_specific_data( (lsmash_summary_t *)orig_summary[2], pcmc ) == 0, "add pcmc t2" );
        lsmash_destroy_codec_specific_data( pcmc );

        lsmash_codec_specific_t *chnl = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_CHANNEL_LAYOUT,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        lsmash_isom_channel_layout_t *cl = (lsmash_isom_channel_layout_t *)chnl->data.structured;
        cl->stream_structure = LSMASH_CHANNEL_STREAM_STRUCTURE_CHANNEL | LSMASH_CHANNEL_STREAM_STRUCTURE_OBJECT;
        cl->definedLayout    = 0;
        cl->channel_count    = 3;
        cl->object_count     = 2;
        cl->positions[0].speaker_position = 0;   /* Front Left */
        cl->positions[1].speaker_position = 1;   /* Front Right */
        cl->positions[2].speaker_position = 126; /* Explicit azimuth / elevation */
        cl->positions[2].azimuth          = -135;
        cl->positions[2].elevation        = 45;
        TEST_ASSERT( lsmash_add_codec_specific_data( (lsmash_summary_t *)orig_summary[2], chnl ) == 0, "add chnl t2" );
        lsmash_destroy_codec_specific_data( chnl );

        TEST_ASSERT( lsmash_add_sample_entry( mux_root, track_id[2], orig_summary[2] ) == 1, "add sample entry t2" );
    }

    /* Track 3: fpcm 64-bit BE, 44100 Hz, 8ch (7.1ch definedLayout=12), also test ipcm 32-bit specific conversion */
    {
        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale = 44100;
        TEST_ASSERT( lsmash_set_media_parameters( mux_root, track_id[3], &media_param ) == 0, "set media params t3" );

        orig_summary[3] = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
        orig_summary[3]->sample_type      = ISOM_CODEC_TYPE_FPCM_AUDIO;
        orig_summary[3]->frequency        = 44100;
        orig_summary[3]->channels         = 8;
        orig_summary[3]->sample_size      = 64;
        orig_summary[3]->samples_in_frame = 1;
        orig_summary[3]->bytes_per_frame  = 64;

        lsmash_codec_specific_t *pcmc = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_PCM_CONFIG,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        lsmash_isom_pcm_config_t *cfg = (lsmash_isom_pcm_config_t *)pcmc->data.structured;
        cfg->format_flags    = LSMASH_PCM_FORMAT_FLAG_BIG_ENDIAN;
        cfg->PCM_sample_size = 64;
        /* Also test structured -> unstructured -> structured round-trip for pcmC */
        lsmash_codec_specific_t *pcmc_unstr = lsmash_convert_codec_specific_format( pcmc, LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
        TEST_ASSERT( pcmc_unstr != NULL && pcmc_unstr->size == 14, "convert pcmC to unstructured" );
        TEST_ASSERT( lsmash_add_codec_specific_data( (lsmash_summary_t *)orig_summary[3], pcmc_unstr ) == 0, "add unstructured pcmc t3" );
        lsmash_destroy_codec_specific_data( pcmc_unstr );
        lsmash_destroy_codec_specific_data( pcmc );

        lsmash_codec_specific_t *chnl = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_CHANNEL_LAYOUT,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        lsmash_isom_channel_layout_t *cl = (lsmash_isom_channel_layout_t *)chnl->data.structured;
        cl->stream_structure   = LSMASH_CHANNEL_STREAM_STRUCTURE_CHANNEL;
        cl->definedLayout      = 12;
        cl->omittedChannelsMap = 0;
        cl->channel_count      = 8;
        TEST_ASSERT( lsmash_add_codec_specific_data( (lsmash_summary_t *)orig_summary[3], chnl ) == 0, "add chnl t3" );
        lsmash_destroy_codec_specific_data( chnl );

        TEST_ASSERT( lsmash_add_sample_entry( mux_root, track_id[3], orig_summary[3] ) == 1, "add sample entry t3" );
    }

    /* ISO/IEC 23003-5 validation & additional bit-depth/endianness combinations (ipcm 32-bit BE/LE, fpcm 32-bit BE, fpcm 64-bit LE) */
    {
        static const struct
        {
            int     is_float;
            uint8_t flags;
            uint8_t depth;
        } valid_combos[] = {
            { 0, LSMASH_PCM_FORMAT_FLAG_BIG_ENDIAN,    32 },
            { 0, LSMASH_PCM_FORMAT_FLAG_LITTLE_ENDIAN, 32 },
            { 1, LSMASH_PCM_FORMAT_FLAG_BIG_ENDIAN,    32 },
            { 1, LSMASH_PCM_FORMAT_FLAG_LITTLE_ENDIAN, 64 },
        };
        for( size_t c = 0; c < sizeof(valid_combos) / sizeof(valid_combos[0]); c++ )
        {
            lsmash_codec_specific_t *cs = lsmash_create_codec_specific_data(
                LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_PCM_CONFIG,
                LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
            lsmash_isom_pcm_config_t *pc = (lsmash_isom_pcm_config_t *)cs->data.structured;
            pc->format_flags    = valid_combos[c].flags;
            pc->PCM_sample_size = valid_combos[c].depth;
            lsmash_codec_specific_t *unstr = lsmash_convert_codec_specific_format( cs, LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
            TEST_ASSERT( unstr != NULL && unstr->size == 14, "pcmC valid combo to unstructured" );
            lsmash_codec_specific_t *back = lsmash_convert_codec_specific_format( unstr, LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
            TEST_ASSERT( back != NULL, "pcmC valid combo back to structured" );
            lsmash_isom_pcm_config_t *pc2 = (lsmash_isom_pcm_config_t *)back->data.structured;
            TEST_ASSERT( pc2->format_flags == valid_combos[c].flags && pc2->PCM_sample_size == valid_combos[c].depth,
                         "pcmC roundtrip fields match" );
            lsmash_destroy_codec_specific_data( back );
            lsmash_destroy_codec_specific_data( unstr );
            lsmash_destroy_codec_specific_data( cs );
        }

        /* ISO/IEC 23003-5 rejection: ipcm without pcmC must fail lsmash_add_sample_entry */
        lsmash_audio_summary_t *bad_sum = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
        bad_sum->sample_type      = ISOM_CODEC_TYPE_IPCM_AUDIO;
        bad_sum->frequency        = 48000;
        bad_sum->channels         = 2;
        bad_sum->sample_size      = 32;
        bad_sum->samples_in_frame = 1;
        bad_sum->bytes_per_frame  = 8;
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, track_id[0], bad_sum ) == 0,
                     "reject ipcm without mandatory pcmC" );

        /* ISO/IEC 23003-5 rejection: fpcm with 24-bit integer sample size must fail lsmash_add_sample_entry */
        bad_sum->sample_type = ISOM_CODEC_TYPE_FPCM_AUDIO;
        bad_sum->sample_size = 24;
        lsmash_codec_specific_t *bad_pcmc = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_PCM_CONFIG,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        ((lsmash_isom_pcm_config_t *)bad_pcmc->data.structured)->format_flags    = LSMASH_PCM_FORMAT_FLAG_LITTLE_ENDIAN;
        ((lsmash_isom_pcm_config_t *)bad_pcmc->data.structured)->PCM_sample_size = 24;
        TEST_ASSERT( lsmash_add_codec_specific_data( (lsmash_summary_t *)bad_sum, bad_pcmc ) == 0, "add bad_pcmc" );
        lsmash_destroy_codec_specific_data( bad_pcmc );
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, track_id[0], bad_sum ) == 0,
                     "reject fpcm with non-32/64-bit PCM_sample_size" );
        lsmash_cleanup_summary( (lsmash_summary_t *)bad_sum );
    }

    /* Append 10 LPCM frames per track */
    const uint32_t frame_sizes[4] = { 2, 6, 12, 64 };
    for( int t = 0; t < 4; t++ )
    {
        uint32_t fsize = frame_sizes[t];
        for( uint32_t i = 0; i < 10; i++ )
        {
            lsmash_sample_t *s = lsmash_create_sample( fsize );
            TEST_ASSERT( s != NULL, "create lpcm sample" );
            for( uint32_t b = 0; b < fsize; b++ )
                s->data[b] = (uint8_t)((t * 37) + (i * 13) + b);
            s->dts           = i;
            s->cts           = i;
            s->index         = 1;
            s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            TEST_ASSERT( lsmash_append_sample( mux_root, track_id[t], s ) == 0, "append lpcm sample" );
        }
        TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, track_id[t], 1 ) == 0, "flush pooled samples" );
        TEST_ASSERT( lsmash_update_track_duration( mux_root, track_id[t], 0 ) == 0, "update track duration" );
    }

    TEST_ASSERT( lsmash_finish_movie( mux_root, NULL ) == 0, "finish lpcm movie" );
    lsmash_close_file( &mux_param );
    lsmash_destroy_root( mux_root );

    /* Re-open, dump boxes, verify summaries and payloads */
    lsmash_root_t *demux_root = lsmash_create_root();
    TEST_ASSERT( demux_root != NULL, "create demux root" );
    lsmash_file_parameters_t demux_param = { 0 };
    TEST_ASSERT( lsmash_open_file( mp4_path, 1, &demux_param ) == 0, "open mp4 for reading" );
    demux_param.mode |= LSMASH_FILE_MODE_DUMP;
    lsmash_file_t *demux_file = lsmash_set_file( demux_root, &demux_param );
    TEST_ASSERT( demux_file != NULL, "set demux file" );
    TEST_ASSERT( lsmash_read_file( demux_file, &demux_param ) >= 0, "read lpcm mp4" );

    TEST_ASSERT( lsmash_print_movie( demux_root, dump_path ) == 0, "print lpcm movie" );
    {
        FILE *fp = fopen( dump_path, "rb" );
        TEST_ASSERT( fp != NULL, "open lpcm box dump" );
        fseek( fp, 0, SEEK_END );
        long fsz = ftell( fp );
        fseek( fp, 0, SEEK_SET );
        char *dump_buf = (char *)lsmash_malloc( (size_t)fsz + 1 );
        TEST_ASSERT( dump_buf != NULL, "alloc dump_buf" );
        size_t nread = fread( dump_buf, 1, (size_t)fsz, fp );
        dump_buf[nread] = '\0';
        fclose( fp );

        TEST_ASSERT( strstr( dump_buf, "[ipcm: Audio Description]" ) != NULL, "dump has ipcm" );
        TEST_ASSERT( strstr( dump_buf, "[fpcm: Audio Description]" ) != NULL, "dump has fpcm" );
        TEST_ASSERT( strstr( dump_buf, "[pcmC: PCM Configuration Box]" ) != NULL, "dump has pcmC" );
        TEST_ASSERT( strstr( dump_buf, "format_flags = 0x00 (big-endian)" ) != NULL, "dump has BE pcmC" );
        TEST_ASSERT( strstr( dump_buf, "format_flags = 0x01 (little-endian)" ) != NULL, "dump has LE pcmC" );
        TEST_ASSERT( strstr( dump_buf, "[chnl: Channel Layout Box]" ) != NULL, "dump has chnl" );
        TEST_ASSERT( strstr( dump_buf, "azimuth = -135" ) != NULL && strstr( dump_buf, "elevation = 45" ) != NULL, "dump has explicit pos 126" );
        TEST_ASSERT( strstr( dump_buf, "object_count = 2" ) != NULL, "dump has chnl object_count" );
        TEST_ASSERT( strstr( dump_buf, "[srat: Sampling Rate Box]" ) != NULL, "dump has srat" );
        TEST_ASSERT( strstr( dump_buf, "sampling_rate = 96000" ) != NULL, "dump has 96000 Hz srat" );
        TEST_ASSERT( strstr( dump_buf, "sampling_rate = 192000" ) != NULL, "dump has 192000 Hz srat" );
        TEST_ASSERT( strstr( dump_buf, "samplesPerPacket" ) == NULL, "ISOBMFF AudioSampleEntryV1 has no QTFF fields" );
        lsmash_free( dump_buf );
    }
    remove( dump_path );

    for( int t = 0; t < 4; t++ )
    {
        uint32_t tid = t + 1;
        lsmash_audio_summary_t *read_sum = (lsmash_audio_summary_t *)lsmash_get_summary( demux_root, tid, 1 );
        TEST_ASSERT( read_sum != NULL, "get audio summary" );
        TEST_ASSERT( read_sum->frequency == orig_summary[t]->frequency, "summary frequency matches (including >65535 srat)" );
        TEST_ASSERT( read_sum->channels == orig_summary[t]->channels, "summary channels match" );
        TEST_ASSERT( read_sum->sample_size == orig_summary[t]->sample_size, "summary sample_size matches pcmC" );
        TEST_ASSERT( read_sum->bytes_per_frame == frame_sizes[t], "summary bytes_per_frame matches" );
        TEST_ASSERT( lsmash_compare_summary( (lsmash_summary_t *)orig_summary[t], (lsmash_summary_t *)read_sum ) == 0,
                     "lsmash_compare_summary matches original and read summary" );
        lsmash_cleanup_summary( (lsmash_summary_t *)read_sum );
        lsmash_cleanup_summary( (lsmash_summary_t *)orig_summary[t] );

        TEST_ASSERT( lsmash_construct_timeline( demux_root, tid ) == 0, "construct timeline" );
        TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( demux_root, tid ) == 10, "10 samples in timeline" );
        uint32_t fsize = frame_sizes[t];
        for( uint32_t i = 1; i <= 10; i++ )
        {
            lsmash_sample_t *s = lsmash_get_sample_from_media_timeline( demux_root, tid, i );
            TEST_ASSERT( s != NULL && s->length == fsize, "sample length matches" );
            for( uint32_t b = 0; b < fsize; b++ )
                TEST_ASSERT( s->data[b] == (uint8_t)((t * 37) + ((i - 1) * 13) + b), "sample byte matches" );
            lsmash_delete_sample( s );
        }
        lsmash_destruct_timeline( demux_root, tid );
    }

    lsmash_close_file( &demux_param );
    lsmash_destroy_root( demux_root );
    remove( mp4_path );

    printf( "[PASS] test_lpcm_pcmc_chnl_srat_roundtrip\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 7: Multi-Track 5.1ch LPCM Fragmented ISOBMFF Round-Trip & trun Compaction
 *---------------------------------------------------------------------------*/
static int test_multitrack_fragmented_lpcm_roundtrip( void )
{
    const char *fmp4_path = "tests/tmp_test_multitrack_frag_lpcm.mp4";

    lsmash_root_t *mux_root = lsmash_create_root();
    TEST_ASSERT( mux_root != NULL, "create fragmented mux root" );

    lsmash_file_parameters_t mux_param = { 0 };
    TEST_ASSERT( lsmash_open_file( fmp4_path, 0, &mux_param ) == 0, "open fragmented mp4" );
    mux_param.mode         |= LSMASH_FILE_MODE_FRAGMENTED;
    mux_param.major_brand   = ISOM_BRAND_TYPE_ISO6;
    mux_param.minor_version = 0;
    lsmash_brand_type brands[2] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_ISO6 };
    mux_param.brands      = brands;
    mux_param.brand_count = 2;
    TEST_ASSERT( lsmash_set_file( mux_root, &mux_param ) != NULL, "set fragmented file" );

    lsmash_movie_parameters_t movie_param;
    lsmash_initialize_movie_parameters( &movie_param );
    TEST_ASSERT( lsmash_set_movie_parameters( mux_root, &movie_param ) == 0, "set movie params" );

    /* Track 1: 5.1ch (6ch) ipcm 24-bit LE @ 48000 Hz (18 bytes/frame)
     * Track 2: 5.1ch (6ch) fpcm 32-bit LE @ 96000 Hz (24 bytes/frame) */
    uint32_t tid1 = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
    uint32_t tid2 = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
    TEST_ASSERT( tid1 == 1 && tid2 == 2, "create 2 audio tracks" );

    const uint32_t freqs[2]       = { 48000, 96000 };
    const uint8_t  depths[2]      = { 24, 32 };
    const uint32_t frame_sizes[2] = { 18, 24 };
    const uint32_t tids[2]        = { tid1, tid2 };

    for( int t = 0; t < 2; t++ )
    {
        lsmash_track_parameters_t track_param;
        lsmash_initialize_track_parameters( &track_param );
        track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
        TEST_ASSERT( lsmash_set_track_parameters( mux_root, tids[t], &track_param ) == 0, "set track params" );

        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale = freqs[t];
        TEST_ASSERT( lsmash_set_media_parameters( mux_root, tids[t], &media_param ) == 0, "set media params" );

        lsmash_audio_summary_t *sum = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
        sum->sample_type      = (t == 0) ? ISOM_CODEC_TYPE_IPCM_AUDIO : ISOM_CODEC_TYPE_FPCM_AUDIO;
        sum->frequency        = freqs[t];
        sum->channels         = 6;
        sum->sample_size      = depths[t];
        sum->samples_in_frame = 1;
        sum->bytes_per_frame  = frame_sizes[t];

        lsmash_codec_specific_t *pcmc = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_PCM_CONFIG,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        lsmash_isom_pcm_config_t *cfg = (lsmash_isom_pcm_config_t *)pcmc->data.structured;
        cfg->format_flags    = LSMASH_PCM_FORMAT_FLAG_LITTLE_ENDIAN;
        cfg->PCM_sample_size = depths[t];
        TEST_ASSERT( lsmash_add_codec_specific_data( (lsmash_summary_t *)sum, pcmc ) == 0, "add pcmc" );
        lsmash_destroy_codec_specific_data( pcmc );

        lsmash_codec_specific_t *chnl = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_CHANNEL_LAYOUT,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        lsmash_isom_channel_layout_t *cl = (lsmash_isom_channel_layout_t *)chnl->data.structured;
        cl->stream_structure   = LSMASH_CHANNEL_STREAM_STRUCTURE_CHANNEL;
        cl->definedLayout      = 6; /* 5.1ch CICP */
        cl->omittedChannelsMap = 0;
        cl->channel_count      = 6;
        TEST_ASSERT( lsmash_add_codec_specific_data( (lsmash_summary_t *)sum, chnl ) == 0, "add chnl" );
        lsmash_destroy_codec_specific_data( chnl );

        TEST_ASSERT( lsmash_add_sample_entry( mux_root, tids[t], sum ) == 1, "add sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)sum );
    }

    /* Flush initial moov and write 2 fragments (200 samples/frag for Track 1, 400 samples/frag for Track 2) */
    for( uint32_t frag = 0; frag < 2; frag++ )
    {
        TEST_ASSERT( lsmash_create_fragment_movie( mux_root ) == 0, "create fragment movie" );

        for( uint32_t i = 0; i < 200; i++ )
        {
            uint64_t idx = frag * 200 + i;
            lsmash_sample_t *s1 = lsmash_create_sample( 18 );
            TEST_ASSERT( s1 != NULL, "create frag sample t1" );
            for( uint32_t b = 0; b < 18; b++ )
                s1->data[b] = (uint8_t)((idx * 7 + b) & 0xFF);
            s1->dts           = idx;
            s1->cts           = idx;
            s1->index         = 1;
            s1->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            TEST_ASSERT( lsmash_append_sample( mux_root, tid1, s1 ) == 0, "append frag sample t1" );
        }

        for( uint32_t i = 0; i < 400; i++ )
        {
            uint64_t idx = frag * 400 + i;
            lsmash_sample_t *s2 = lsmash_create_sample( 24 );
            TEST_ASSERT( s2 != NULL, "create frag sample t2" );
            for( uint32_t b = 0; b < 24; b++ )
                s2->data[b] = (uint8_t)((idx * 11 + b + 0x55) & 0xFF);
            s2->dts           = idx;
            s2->cts           = idx;
            s2->index         = 1;
            s2->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            TEST_ASSERT( lsmash_append_sample( mux_root, tid2, s2 ) == 0, "append frag sample t2" );
        }

        TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, tid1, 1 ) == 0, "flush pooled samples t1" );
        TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, tid2, 1 ) == 0, "flush pooled samples t2" );
    }

    TEST_ASSERT( lsmash_finish_movie( mux_root, NULL ) == 0, "finish fragmented movie" );
    lsmash_close_file( &mux_param );
    lsmash_destroy_root( mux_root );

    /* Re-open fragmented MP4, verify trun->optional == NULL on BOTH tracks, and verify timeline samples */
    lsmash_root_t *demux_root = lsmash_create_root();
    TEST_ASSERT( demux_root != NULL, "create fragmented demux root" );
    lsmash_file_parameters_t demux_param = { 0 };
    TEST_ASSERT( lsmash_open_file( fmp4_path, 1, &demux_param ) == 0, "open fragmented mp4 for reading" );
    lsmash_file_t *file = lsmash_set_file( demux_root, &demux_param );
    TEST_ASSERT( file != NULL, "set fragmented demux file" );
    TEST_ASSERT( lsmash_read_file( file, &demux_param ) >= 0, "read fragmented mp4" );

    TEST_ASSERT( file->moof_list.entry_count == 2, "2 moof boxes read" );
    for( lsmash_entry_t *moof_e = file->moof_list.head; moof_e; moof_e = moof_e->next )
    {
        isom_moof_t *moof = (isom_moof_t *)moof_e->data;
        TEST_ASSERT( moof != NULL && moof->traf_list.entry_count == 2, "moof has 2 traf boxes" );
        int traf_idx = 0;
        for( lsmash_entry_t *traf_e = moof->traf_list.head; traf_e; traf_e = traf_e->next, traf_idx++ )
        {
            isom_traf_t *traf = (isom_traf_t *)traf_e->data;
            TEST_ASSERT( traf != NULL && LSMASH_IS_EXISTING_BOX( traf->tfhd ), "traf has tfhd" );
            isom_trex_t *trex = (isom_trex_t *)lsmash_list_get_entry_data( &file->moov->mvex->trex_list, traf_idx + 1 );
            TEST_ASSERT( trex != NULL, "trex exists" );
            uint32_t eff_duration = (traf->tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT)
                                  ? traf->tfhd->default_sample_duration
                                  : trex->default_sample_duration;
            uint32_t eff_size     = (traf->tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_SIZE_PRESENT)
                                  ? traf->tfhd->default_sample_size
                                  : trex->default_sample_size;
            TEST_ASSERT( eff_duration == 1, "effective default_sample_duration == 1" );
            TEST_ASSERT( eff_size == frame_sizes[traf_idx], "effective default_sample_size matches frame size" );
            TEST_ASSERT( traf->trun_list.entry_count == 1, "traf has 1 trun" );
            isom_trun_t *trun = (isom_trun_t *)traf->trun_list.head->data;
            TEST_ASSERT( trun != NULL, "trun exists" );
            TEST_ASSERT( (trun->flags & 0x000f00) == 0, "trun has zero per-sample flags" );
            TEST_ASSERT( trun->optional == NULL, "trun->optional is NULL (no per-sample rows allocated)" );
        }
    }

    for( int t = 0; t < 2; t++ )
    {
        uint32_t tid      = tids[t];
        uint32_t expected = (t == 0) ? 400 : 800;
        uint32_t fsize    = frame_sizes[t];

        TEST_ASSERT( lsmash_construct_timeline( demux_root, tid ) == 0, "construct fragmented timeline" );
        TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( demux_root, tid ) == expected,
                     "fragmented sample count matches" );
        for( uint32_t i = 1; i <= expected; i++ )
        {
            uint64_t idx = i - 1;
            lsmash_sample_t *s = lsmash_get_sample_from_media_timeline( demux_root, tid, i );
            TEST_ASSERT( s != NULL && s->length == fsize, "fragmented sample length matches" );
            TEST_ASSERT( s->dts == idx && s->cts == idx, "fragmented sample timestamps match" );
            for( uint32_t b = 0; b < fsize; b++ )
            {
                uint8_t exp_b = (t == 0)
                              ? (uint8_t)((idx * 7 + b) & 0xFF)
                              : (uint8_t)((idx * 11 + b + 0x55) & 0xFF);
                if( s->data[b] != exp_b )
                    TEST_ASSERT( 0, "fragmented sample payload byte matches" );
            }
            lsmash_delete_sample( s );
        }
        lsmash_destruct_timeline( demux_root, tid );
    }

    lsmash_close_file( &demux_param );
    lsmash_destroy_root( demux_root );
    remove( fmp4_path );

    printf( "[PASS] test_multitrack_fragmented_lpcm_roundtrip\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 8: WAVE Importer (WAVE_FORMAT_EXTENSIBLE 5.1ch PCM & IEEE Float) to ISOBMFF
 *---------------------------------------------------------------------------*/
static int test_wave_importer_isobmff_lpcm( void )
{
    const char *wav1_path = "tests/tmp_test_51ch_pcm24.wav";
    const char *wav2_path = "tests/tmp_test_stereo_f32.wav";
    const char *mp4_path  = "tests/tmp_test_wave_isobmff.mp4";

    /* Synthesize WAV 1: WAVE_FORMAT_EXTENSIBLE (0xFFFE), 6ch, 48000 Hz, 24-bit PCM (50 frames = 900 bytes) */
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        TEST_ASSERT( bs != NULL, "create bs for wav1" );
        uint32_t data_size = 50 * 18;
        uint32_t fmt_size  = 40;
        uint32_t riff_size = 4 + (8 + fmt_size) + (8 + data_size);
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'R', 'I', 'F', 'F' ) );
        lsmash_bs_put_le32( bs, riff_size );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'W', 'A', 'V', 'E' ) );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 'm', 't', ' ' ) );
        lsmash_bs_put_le32( bs, fmt_size );
        lsmash_bs_put_le16( bs, 0xFFFE );        /* WAVE_FORMAT_EXTENSIBLE */
        lsmash_bs_put_le16( bs, 6 );             /* nChannels = 6 */
        lsmash_bs_put_le32( bs, 48000 );         /* nSamplesPerSec = 48000 */
        lsmash_bs_put_le32( bs, 48000 * 18 );    /* nAvgBytesPerSec */
        lsmash_bs_put_le16( bs, 18 );            /* nBlockAlign = 18 */
        lsmash_bs_put_le16( bs, 24 );            /* wBitsPerSample = 24 */
        lsmash_bs_put_le16( bs, 22 );            /* cbSize = 22 */
        lsmash_bs_put_le16( bs, 24 );            /* wValidBitsPerSample = 24 */
        lsmash_bs_put_le32( bs, 0x0000003F );    /* dwChannelMask = 5.1ch (0x3F) */
        static const uint8_t pcm_guid[16] = {
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
            0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
        };
        lsmash_bs_put_bytes( bs, 16, (void *)pcm_guid );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'd', 'a', 't', 'a' ) );
        lsmash_bs_put_le32( bs, data_size );
        for( uint32_t i = 0; i < data_size; i++ )
            lsmash_bs_put_byte( bs, (uint8_t)((i * 5 + 3) & 0xFF) );

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        FILE *fp = fopen( wav1_path, "wb" );
        TEST_ASSERT( fp != NULL, "open wav1_path" );
        fwrite( raw, 1, raw_len, fp );
        fclose( fp );
        lsmash_free( raw );
    }

    /* Synthesize WAV 2: WAVE_FORMAT_IEEE_FLOAT (0x0003), 2ch, 96000 Hz, 32-bit float (50 frames = 400 bytes) */
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        TEST_ASSERT( bs != NULL, "create bs for wav2" );
        uint32_t data_size = 50 * 8;
        uint32_t fmt_size  = 18; /* includes cbSize = 0 */
        uint32_t riff_size = 4 + (8 + fmt_size) + (8 + data_size);
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'R', 'I', 'F', 'F' ) );
        lsmash_bs_put_le32( bs, riff_size );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'W', 'A', 'V', 'E' ) );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 'm', 't', ' ' ) );
        lsmash_bs_put_le32( bs, fmt_size );
        lsmash_bs_put_le16( bs, 0x0003 );        /* WAVE_FORMAT_IEEE_FLOAT */
        lsmash_bs_put_le16( bs, 2 );             /* nChannels = 2 */
        lsmash_bs_put_le32( bs, 96000 );         /* nSamplesPerSec = 96000 */
        lsmash_bs_put_le32( bs, 96000 * 8 );     /* nAvgBytesPerSec */
        lsmash_bs_put_le16( bs, 8 );             /* nBlockAlign = 8 */
        lsmash_bs_put_le16( bs, 32 );            /* wBitsPerSample = 32 */
        lsmash_bs_put_le16( bs, 0 );             /* cbSize = 0 */
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'd', 'a', 't', 'a' ) );
        lsmash_bs_put_le32( bs, data_size );
        for( uint32_t i = 0; i < data_size; i++ )
            lsmash_bs_put_byte( bs, (uint8_t)((i * 9 + 0xA0) & 0xFF) );

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        FILE *fp = fopen( wav2_path, "wb" );
        TEST_ASSERT( fp != NULL, "open wav2_path" );
        fwrite( raw, 1, raw_len, fp );
        fclose( fp );
        lsmash_free( raw );
    }

    /* Import both WAVs and mux into an ISOBMFF MP4 */
    lsmash_root_t *mux_root = lsmash_create_root();
    lsmash_file_parameters_t mux_param = { 0 };
    TEST_ASSERT( lsmash_open_file( mp4_path, 0, &mux_param ) == 0, "open output mp4" );
    mux_param.major_brand   = ISOM_BRAND_TYPE_ISOM;
    mux_param.minor_version = 512;
    lsmash_brand_type brands[2] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_ISO2 };
    mux_param.brands      = brands;
    mux_param.brand_count = 2;
    TEST_ASSERT( lsmash_set_file( mux_root, &mux_param ) != NULL, "set output file" );

    lsmash_movie_parameters_t movie_param;
    lsmash_initialize_movie_parameters( &movie_param );
    TEST_ASSERT( lsmash_set_movie_parameters( mux_root, &movie_param ) == 0, "set movie params" );

    const char *wav_paths[2] = { wav1_path, wav2_path };
    for( int t = 0; t < 2; t++ )
    {
        lsmash_root_t *imp_root = lsmash_create_root();
        importer_t *imp = lsmash_importer_open( imp_root, wav_paths[t], "auto" );
        TEST_ASSERT( imp != NULL, "open wave importer" );
        TEST_ASSERT( lsmash_importer_construct_timeline( imp, 1 ) == 0, "construct wave timeline" );

        lsmash_audio_summary_t *sum = (lsmash_audio_summary_t *)lsmash_duplicate_summary( imp, 1 );
        TEST_ASSERT( sum != NULL, "duplicate wave summary" );

        uint32_t tid = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
        TEST_ASSERT( tid == (uint32_t)(t + 1), "create track from wave" );

        lsmash_track_parameters_t track_param;
        lsmash_initialize_track_parameters( &track_param );
        track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
        TEST_ASSERT( lsmash_set_track_parameters( mux_root, tid, &track_param ) == 0, "set track params" );

        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale = sum->frequency;
        TEST_ASSERT( lsmash_set_media_parameters( mux_root, tid, &media_param ) == 0, "set media params" );

        TEST_ASSERT( lsmash_add_sample_entry( mux_root, tid, sum ) == 1, "add wave sample entry to ISOBMFF" );
        lsmash_cleanup_summary( (lsmash_summary_t *)sum );

        lsmash_sample_t *sample = NULL;
        while( lsmash_importer_get_access_unit( imp, 1, &sample ) == 0 && sample )
        {
            sample->index = 1;
            TEST_ASSERT( lsmash_append_sample( mux_root, tid, sample ) == 0, "append wave packet to ISOBMFF" );
            sample = NULL;
        }
        TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, tid, 1 ) == 0, "flush pooled samples" );
        TEST_ASSERT( lsmash_update_track_duration( mux_root, tid, 0 ) == 0, "update track duration" );

        lsmash_importer_close( imp );
        lsmash_destroy_root( imp_root );
    }

    TEST_ASSERT( lsmash_finish_movie( mux_root, NULL ) == 0, "finish wave->isobmff movie" );
    lsmash_close_file( &mux_param );
    lsmash_destroy_root( mux_root );

    /* Demux and verify that Track 1 is ipcm (24-bit, chnl definedLayout=6) and Track 2 is fpcm (32-bit, 96000 Hz) */
    lsmash_root_t *demux_root = lsmash_create_root();
    lsmash_file_parameters_t demux_param = { 0 };
    TEST_ASSERT( lsmash_open_file( mp4_path, 1, &demux_param ) == 0, "open wave->isobmff mp4" );
    lsmash_file_t *demux_file = lsmash_set_file( demux_root, &demux_param );
    TEST_ASSERT( demux_file != NULL, "set demux file" );
    TEST_ASSERT( lsmash_read_file( demux_file, &demux_param ) >= 0, "read wave->isobmff mp4" );

    lsmash_audio_summary_t *s1 = (lsmash_audio_summary_t *)lsmash_get_summary( demux_root, 1, 1 );
    TEST_ASSERT( s1 != NULL, "get summary track 1" );
    TEST_ASSERT( lsmash_check_codec_type_identical( s1->sample_type, ISOM_CODEC_TYPE_IPCM_AUDIO ), "track 1 converted to ipcm" );
    TEST_ASSERT( s1->channels == 6 && s1->sample_size == 24 && s1->frequency == 48000, "track 1 6ch 24-bit 48kHz" );
    lsmash_cleanup_summary( (lsmash_summary_t *)s1 );

    lsmash_audio_summary_t *s2 = (lsmash_audio_summary_t *)lsmash_get_summary( demux_root, 2, 1 );
    TEST_ASSERT( s2 != NULL, "get summary track 2" );
    TEST_ASSERT( lsmash_check_codec_type_identical( s2->sample_type, ISOM_CODEC_TYPE_FPCM_AUDIO ), "track 2 converted to fpcm" );
    TEST_ASSERT( s2->channels == 2 && s2->sample_size == 32 && s2->frequency == 96000, "track 2 2ch 32-bit 96kHz" );
    lsmash_cleanup_summary( (lsmash_summary_t *)s2 );

    for( int t = 0; t < 2; t++ )
    {
        uint32_t tid   = t + 1;
        uint32_t fsize = (t == 0) ? 18 : 8;
        TEST_ASSERT( lsmash_construct_timeline( demux_root, tid ) == 0, "construct timeline" );
        TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( demux_root, tid ) == 50, "50 LPCM frames in timeline" );
        for( uint32_t i = 1; i <= 50; i++ )
        {
            lsmash_sample_t *samp = lsmash_get_sample_from_media_timeline( demux_root, tid, i );
            TEST_ASSERT( samp != NULL && samp->length == fsize, "frame length matches" );
            for( uint32_t b = 0; b < fsize; b++ )
            {
                uint32_t byte_pos = (i - 1) * fsize + b;
                uint8_t exp_b = (t == 0)
                              ? (uint8_t)((byte_pos * 5 + 3) & 0xFF)
                              : (uint8_t)((byte_pos * 9 + 0xA0) & 0xFF);
                if( samp->data[b] != exp_b )
                    TEST_ASSERT( 0, "wave->isobmff frame payload matches bit-for-bit" );
            }
            lsmash_delete_sample( samp );
        }
        lsmash_destruct_timeline( demux_root, tid );
    }

    lsmash_close_file( &demux_param );
    lsmash_destroy_root( demux_root );

    remove( wav1_path );
    remove( wav2_path );
    remove( mp4_path );

    printf( "[PASS] test_wave_importer_isobmff_lpcm\n" );
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

static int test_av1_multitile_and_sync_sample_compliance( void )
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
 * Test 10: Paged Block Array (lsm_list_t) & Unified Accessor / Iterator
 *---------------------------------------------------------------------------*/
typedef struct
{
    uint64_t id;
    uint32_t tag;
    uint32_t flags;
    void    *owned_buf;
} test_paged_elem_t;

static uint32_t g_test_eliminator_calls = 0;

static void test_paged_elem_eliminator( void *data )
{
    if( !data )
        return;
    test_paged_elem_t *elem = (test_paged_elem_t *)data;
    if( elem->owned_buf )
    {
        lsmash_free( elem->owned_buf );
        elem->owned_buf = NULL;
    }
    ++g_test_eliminator_calls;
}

static int test_paged_block_array( void )
{
    /* 10a. Zero-Initialization & NULL Container Safety */
    {
        lsm_list_t zero_list = { 0 };
        TEST_ASSERT( lsm_list_count( NULL ) == 0, "lsm_list_count(NULL) == 0" );
        TEST_ASSERT( lsm_list_count( &zero_list ) == 0, "lsm_list_count(&zero_list) == 0" );
        TEST_ASSERT( lsm_list_at( NULL, 0 ) == NULL, "lsm_list_at(NULL, 0) == NULL" );
        TEST_ASSERT( lsm_list_at( &zero_list, 0 ) == NULL, "lsm_list_at(&zero_list, 0) == NULL" );
        TEST_ASSERT( lsm_list_get( NULL, 1 ) == NULL, "lsm_list_get(NULL, 1) == NULL" );
        TEST_ASSERT( lsm_list_get( &zero_list, 0 ) == NULL, "lsm_list_get(&zero_list, 0) == NULL" );
        TEST_ASSERT( lsm_list_get( &zero_list, 1 ) == NULL, "lsm_list_get(&zero_list, 1) == NULL" );
        TEST_ASSERT( lsm_list_head( NULL ) == NULL, "lsm_list_head(NULL) == NULL" );
        TEST_ASSERT( lsm_list_head( &zero_list ) == NULL, "lsm_list_head(&zero_list) == NULL" );
        TEST_ASSERT( lsm_list_tail( NULL ) == NULL, "lsm_list_tail(NULL) == NULL" );
        TEST_ASSERT( lsm_list_tail( &zero_list ) == NULL, "lsm_list_tail(&zero_list) == NULL" );
        TEST_ASSERT( lsm_list_remove_tail( &zero_list ) < 0, "remove_tail on empty fails safely" );
        TEST_ASSERT( lsm_list_remove( &zero_list, 1 ) < 0, "remove on empty fails safely" );

        uint32_t iter_count = 0;
        test_paged_elem_t *it = NULL;
        LSM_LIST_FOR_EACH( it, &zero_list )
        {
            ++iter_count;
        }
        TEST_ASSERT( iter_count == 0 && it == NULL, "LSM_LIST_FOR_EACH on zero-init list executes 0 times" );
        LSM_LIST_FOR_EACH( it, (lsm_list_t *)NULL )
        {
            ++iter_count;
        }
        TEST_ASSERT( iter_count == 0 && it == NULL, "LSM_LIST_FOR_EACH on NULL list executes 0 times" );

        lsmash_entry_list_t zero_elist = { 0 };
        void *eit = NULL;
        LSM_ENTRY_LIST_FOR_EACH( eit, &zero_elist )
        {
            ++iter_count;
        }
        TEST_ASSERT( iter_count == 0 && eit == NULL, "LSM_ENTRY_LIST_FOR_EACH on zero-init list executes 0 times" );

        lsm_list_clear( &zero_list );
        lsm_list_clear( NULL );
        lsm_list_destroy( NULL );
    }

    /* 10b. Multi-Page Append, O(1) Random Access & 100% Pointer Stability Across 40+ Pages */
    {
        const uint32_t N = 10500; /* crosses >41 pages of 256 elements */
        lsm_list_t list = { 0 };
        lsm_list_init_simple( &list, sizeof(test_paged_elem_t) );

        test_paged_elem_t **saved_ptrs = (test_paged_elem_t **)lsmash_malloc( N * sizeof(test_paged_elem_t *) );
        TEST_ASSERT( saved_ptrs != NULL, "alloc saved_ptrs" );

        for( uint32_t i = 0; i < N; i++ )
        {
            test_paged_elem_t *slot = (test_paged_elem_t *)lsm_list_append( &list );
            TEST_ASSERT( slot != NULL, "lsm_list_append succeeds" );
            TEST_ASSERT( slot->id == 0 && slot->tag == 0 && slot->flags == 0 && slot->owned_buf == NULL,
                         "newly appended slot is zero-initialized" );
            slot->id    = (uint64_t)i * 1000003ULL + 7ULL;
            slot->tag   = i ^ 0xA5A55A5AU;
            slot->flags = i + 1U;
            saved_ptrs[i] = slot;

            /* Verify page boundary transition stability right after crossing a 256-element boundary */
            if( (i & LSM_LIST_PAGE_MASK) == 0 && i > 0 )
            {
                TEST_ASSERT( lsm_list_at( &list, 0 ) == saved_ptrs[0],
                             "page 0 element 0 pointer never moves when new page is allocated" );
                TEST_ASSERT( lsm_list_at( &list, i - 1 ) == saved_ptrs[i - 1],
                             "previous page tail pointer never moves when new page is allocated" );
            }
        }

        TEST_ASSERT( lsm_list_count( &list ) == N, "entry_count == 10500" );
        TEST_ASSERT( list.page_count == ((N + LSM_LIST_PAGE_MASK) >> LSM_LIST_PAGE_SHIFT),
                     "exact number of 256-element pages allocated" );
        TEST_ASSERT( lsm_list_head( &list ) == saved_ptrs[0], "head matches element 0" );
        TEST_ASSERT( lsm_list_tail( &list ) == saved_ptrs[N - 1], "tail matches element N-1" );

        /* Verify 100% pointer stability and O(1) random access for all 10,500 elements */
        for( uint32_t i = 0; i < N; i++ )
        {
            uint32_t perm = (i * 4099U + 123U) % N; /* pseudo-random index */
            test_paged_elem_t *by_at  = (test_paged_elem_t *)lsm_list_at( &list, perm );
            test_paged_elem_t *by_get = (test_paged_elem_t *)lsm_list_get( &list, perm + 1 );
            TEST_ASSERT( by_at == saved_ptrs[perm], "pointer stability 100% preserved for all elements" );
            TEST_ASSERT( by_get == by_at, "1-based lsm_list_get matches 0-based lsm_list_at" );
            TEST_ASSERT( by_at->id == (uint64_t)perm * 1000003ULL + 7ULL && by_at->tag == (perm ^ 0xA5A55A5AU),
                         "element payload intact across 42 pages" );
        }

        /* Verify LSM_LIST_FOR_EACH with continue and break */
        uint32_t visited = 0;
        test_paged_elem_t *elem = NULL;
        LSM_LIST_FOR_EACH( elem, &list )
        {
            if( elem->flags == 10 )
            {
                ++visited;
                continue;
            }
            if( elem->flags == 600 )
                break;
            ++visited;
        }
        TEST_ASSERT( visited == 599 && elem != NULL && elem->flags == 600,
                     "LSM_LIST_FOR_EACH supports single-loop continue and break" );

        /* Verify lockstep cursor forward & backward across page boundaries */
        lsm_list_cursor_t cur;
        lsm_list_cursor_init_at( &cur, &list, 254 );
        TEST_ASSERT( cur.data == saved_ptrs[254], "cursor init at 254" );
        TEST_ASSERT( lsm_list_cursor_next( &cur ) == saved_ptrs[255], "cursor next to 255 (page 0 end)" );
        TEST_ASSERT( lsm_list_cursor_next( &cur ) == saved_ptrs[256], "cursor next to 256 (page 1 start)" );
        TEST_ASSERT( lsm_list_cursor_prev( &cur ) == saved_ptrs[255], "cursor prev back to 255" );

        lsmash_free( saved_ptrs );
        lsm_list_clear( &list );
        TEST_ASSERT( lsm_list_count( &list ) == 0 && list.pages == NULL, "lsm_list_clear frees pages" );
    }

    /* 10c. Tail Removal, Mid-List Removal Across Pages, O(1) Move & Custom Eliminator Leak Check */
    {
        g_test_eliminator_calls = 0;
        lsm_list_t *src = lsm_list_create( sizeof(test_paged_elem_t), test_paged_elem_eliminator );
        TEST_ASSERT( src != NULL, "lsm_list_create succeeds" );

        const uint32_t M = 600; /* 3 pages (0..255, 256..511, 512..599) */
        for( uint32_t i = 0; i < M; i++ )
        {
            test_paged_elem_t item;
            item.id        = i + 100;
            item.tag       = i;
            item.flags     = 1;
            item.owned_buf = lsmash_malloc( 16 );
            TEST_ASSERT( item.owned_buf != NULL, "alloc owned_buf" );
            TEST_ASSERT( lsm_list_add( src, &item ) == 0, "lsm_list_add succeeds" );
        }
        TEST_ASSERT( lsm_list_count( src ) == M, "count == 600" );

        /* Pop 10 elements from tail */
        for( uint32_t i = 0; i < 10; i++ )
            TEST_ASSERT( lsm_list_remove_tail( src ) == 0, "remove_tail succeeds" );
        TEST_ASSERT( lsm_list_count( src ) == 590, "count == 590 after 10 tail pops" );
        TEST_ASSERT( g_test_eliminator_calls == 10, "eliminator called 10 times for tail pops" );

        /* Remove element at 1-based entry_number = 250 (in page 0, shifting elements from page 1 and page 2!) */
        TEST_ASSERT( lsm_list_remove( src, 250 ) == 0, "mid-list remove at entry 250 across pages" );
        TEST_ASSERT( lsm_list_count( src ) == 589, "count == 589 after mid-list removal" );
        TEST_ASSERT( g_test_eliminator_calls == 11, "eliminator called on removed mid-list element" );

        for( uint32_t i = 0; i < 589; i++ )
        {
            uint32_t expected_tag = (i < 249) ? i : (i + 1);
            test_paged_elem_t *e = (test_paged_elem_t *)lsm_list_at( src, i );
            TEST_ASSERT( e != NULL && e->tag == expected_tag && e->owned_buf != NULL,
                         "cross-page shift after mid-list removal preserved exact order and pointers" );
        }

        /* O(1) move from src to dst */
        lsm_list_t dst = { 0 };
        lsm_list_init( &dst, sizeof(test_paged_elem_t), test_paged_elem_eliminator );
        lsm_list_move( &dst, src );
        TEST_ASSERT( lsm_list_count( src ) == 0 && src->pages == NULL, "src is empty after lsm_list_move" );
        TEST_ASSERT( lsm_list_count( &dst ) == 589, "dst has all 589 elements after lsm_list_move" );

        lsm_list_destroy( src );
        lsm_list_clear( &dst );
        TEST_ASSERT( g_test_eliminator_calls == M, "eliminator called for all 600 elements with 0 leaks" );
    }

    printf( "[PASS] test_paged_block_array\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 11: Large-Scale Paged Sample Tables, Roll Dedup Hazard & Fragmented Stress
 *---------------------------------------------------------------------------*/
static lsmash_video_summary_t *create_synth_av01_summary( uint32_t width, uint32_t height )
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

static int test_large_scale_paged_tables_and_roll_dedup( void )
{
    /* 11a. 10,500-Sample VFR Movie + Roll Group Mid-Table Deduplication + Timeline Pointer Stability */
    {
        const char *mp4_path  = "tests/tmp_test_vfr_10500.mp4";
        const char *dump_path = "tests/tmp_test_vfr_10500_dump.txt";
        const uint32_t N = 10500; /* >41 pages of 256 elements */

        lsmash_root_t *mux_root = lsmash_create_root();
        TEST_ASSERT( mux_root != NULL, "11a: create mux root" );
        lsmash_file_parameters_t mux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( mp4_path, 0, &mux_param ) == 0, "11a: open output mp4" );
        lsmash_brand_type brands[5] = {
            ISOM_BRAND_TYPE_ISOM,
            ISOM_BRAND_TYPE_ISO6,
            ISOM_BRAND_TYPE_AVC1,
            ISOM_BRAND_TYPE_AV01,
            ISOM_BRAND_TYPE_MP42
        };
        mux_param.major_brand   = ISOM_BRAND_TYPE_MP42;
        mux_param.brands        = brands;
        mux_param.brand_count   = 5;
        mux_param.minor_version = 0;
        TEST_ASSERT( lsmash_set_file( mux_root, &mux_param ) != NULL, "11a: set output file" );

        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        movie_param.timescale = 1000;
        TEST_ASSERT( lsmash_set_movie_parameters( mux_root, &movie_param ) == 0, "11a: set movie params" );

        uint32_t tid = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        TEST_ASSERT( tid == 1, "11a: create video track" );

        lsmash_track_parameters_t track_param;
        lsmash_initialize_track_parameters( &track_param );
        track_param.mode           = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
        track_param.display_width  = 64 << 16;
        track_param.display_height = 64 << 16;
        TEST_ASSERT( lsmash_set_track_parameters( mux_root, tid, &track_param ) == 0, "11a: set track params" );

        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale     = 90000;
        media_param.roll_grouping = 1;
        media_param.rap_grouping  = 1;
        TEST_ASSERT( lsmash_set_media_parameters( mux_root, tid, &media_param ) == 0, "11a: set media params" );

        lsmash_video_summary_t *summary = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( summary != NULL, "11a: create synth av01 summary" );
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, tid, summary ) == 1, "11a: add sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );

        uint64_t cur_dts = 0;
        for( uint32_t i = 0; i < N; i++ )
        {
            uint32_t sample_num = i + 1;
            uint32_t sz         = 4 + (i % 13);
            uint32_t delta      = 100 + (i % 5); /* every sample changes delta -> N stts entries */
            uint32_t cts_off    = (i + 1 < N) ? ((i % 3) * 10) : 0; /* every sample changes offset -> N ctts entries */

            lsmash_sample_t *s = lsmash_create_sample( sz );
            TEST_ASSERT( s != NULL, "11a: create sample" );
            for( uint32_t b = 0; b < sz; b++ )
                s->data[b] = (uint8_t)((i * 17 + b) & 0xFF);
            s->dts   = cur_dts;
            s->cts   = cur_dts + cts_off;
            s->index = 1;
            s->prop.leading     = ISOM_SAMPLE_IS_NOT_LEADING;
            s->prop.independent = (i % 2 == 0) ? ISOM_SAMPLE_IS_INDEPENDENT : ISOM_SAMPLE_IS_NOT_INDEPENDENT;
            s->prop.disposable  = (i % 4 == 0) ? ISOM_SAMPLE_IS_DISPOSABLE  : ISOM_SAMPLE_IS_NOT_DISPOSABLE;
            s->prop.redundant   = ISOM_SAMPLE_HAS_NO_REDUNDANCY;
            s->prop.post_roll.identifier = sample_num;

            /* Exercise SYNC, OPEN_RAP, and consecutive POST_ROLL_START groups across page boundaries */
            if( i % 300 == 0 )
            {
                s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            }
            else if( i % 300 == 150 )
            {
                s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP;
            }
            else if( (i % 300) >= 10 && (i % 300) < 40 && i + 2 < N )
            {
                /* 30 consecutive post-roll start samples with identical roll_distance = 2!
                 * Each creates an sbgp entry and a pool entry; as recovery completes while later
                 * entries are still in cache->roll.pool, isom_deduplicate_roll_group merges them
                 * via mid-table lsm_list_remove(sbgp->list, current_group_number)! */
                s->prop.ra_flags           = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_POST_ROLL_START;
                s->prop.post_roll.complete = sample_num + 2;
            }
            else
            {
                s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
            }

            TEST_ASSERT( lsmash_append_sample( mux_root, tid, s ) == 0, "11a: append VFR sample" );
            cur_dts += delta;
        }

        uint32_t last_delta = 100 + (N % 5);
        TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, tid, last_delta ) == 0, "11a: flush pooled samples" );
        TEST_ASSERT( lsmash_update_track_duration( mux_root, tid, last_delta ) == 0, "11a: update track duration" );

        /* Add 2 explicit edit list entries */
        lsmash_edit_t edit1 = { .duration = 5000, .start_time = 0,   .rate = ISOM_EDIT_MODE_NORMAL };
        lsmash_edit_t edit2 = { .duration = 6000, .start_time = 100, .rate = ISOM_EDIT_MODE_NORMAL };
        TEST_ASSERT( lsmash_create_explicit_timeline_map( mux_root, tid, edit1 ) == 0, "11a: add edit 1" );
        TEST_ASSERT( lsmash_create_explicit_timeline_map( mux_root, tid, edit2 ) == 0, "11a: add edit 2" );

        TEST_ASSERT( lsmash_finish_movie( mux_root, NULL ) == 0, "11a: finish 10,500-sample movie" );
        lsmash_close_file( &mux_param );
        lsmash_destroy_root( mux_root );

        /* Demux, verify multi-page stbl tables, construct timeline, and verify pointer stability */
        lsmash_root_t *demux_root = lsmash_create_root();
        TEST_ASSERT( demux_root != NULL, "11a: create demux root" );
        lsmash_file_parameters_t demux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( mp4_path, 1, &demux_param ) == 0, "11a: open mp4 for demux" );
        demux_param.mode |= LSMASH_FILE_MODE_DUMP;
        lsmash_file_t *demux_file = lsmash_set_file( demux_root, &demux_param );
        TEST_ASSERT( demux_file != NULL, "11a: set demux file" );
        TEST_ASSERT( lsmash_read_file( demux_file, &demux_param ) >= 0, "11a: read 10,500-sample mp4" );
        TEST_ASSERT( lsmash_print_movie( demux_root, dump_path ) == 0, "11a: print movie dump" );
        remove( dump_path );

        isom_trak_t *trak = (isom_trak_t *)demux_file->moov->trak_list.head->data;
        isom_stbl_t *stbl = trak->mdia->minf->stbl;
        TEST_ASSERT( lsm_list_count( stbl->stts->list ) == N, "11a: stts has 10,500 entries across 42 pages" );
        TEST_ASSERT( lsm_list_count( stbl->ctts->list ) == N, "11a: ctts has 10,500 entries across 42 pages" );
        TEST_ASSERT( lsm_list_count( stbl->stsz->list ) == N, "11a: stsz has 10,500 entries across 42 pages" );
        TEST_ASSERT( lsm_list_count( stbl->sdtp->list ) == N, "11a: sdtp has 10,500 entries across 42 pages" );
        TEST_ASSERT( lsm_list_count( trak->edts->elst->list ) == 2, "11a: elst has 2 entries" );

        isom_sgpd_t *sgpd_roll = (isom_sgpd_t *)stbl->sgpd_list.head->data;
        isom_sbgp_t *sbgp_roll = (isom_sbgp_t *)stbl->sbgp_list.head->data;
        TEST_ASSERT( sgpd_roll != NULL && lsm_list_count( sgpd_roll->list ) == 1,
                     "11a: sgpd roll deduplicated to 1 unique roll_distance (+2)" );
        /* 35 cycles of (non-roll -> 30 merged post-roll samples -> non-roll) = 71 sbgp entries (instead of 35*30 = 1050!) */
        TEST_ASSERT( sbgp_roll != NULL && lsm_list_count( sbgp_roll->list ) == 71,
                     "11a: sbgp roll deduplicated 30 consecutive post-roll groups per cycle into 1 entry" );

        TEST_ASSERT( lsmash_construct_timeline( demux_root, tid ) == 0, "11a: construct 10,500-sample timeline" );
        TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( demux_root, tid ) == N, "11a: timeline sample count == 10,500" );
        TEST_ASSERT( lsmash_count_explicit_timeline_map( demux_root, tid ) == 2, "11a: timeline edit count == 2" );

        uint64_t exp_dts = 0;
        for( uint32_t i = 0; i < N; i++ )
        {
            uint32_t sample_num = i + 1;
            uint32_t exp_sz     = 4 + (i % 13);
            uint32_t exp_delta  = (i + 1 < N) ? (100 + (i % 5)) : last_delta;
            uint32_t exp_off    = (i + 1 < N) ? ((i % 3) * 10) : 0;

            uint64_t got_dts = 0, got_cts = 0;
            uint32_t got_delta = 0;
            lsmash_sample_property_t got_prop = { 0 };
            TEST_ASSERT( lsmash_get_dts_from_media_timeline( demux_root, tid, sample_num, &got_dts ) == 0, "11a: get dts" );
            TEST_ASSERT( lsmash_get_cts_from_media_timeline( demux_root, tid, sample_num, &got_cts ) == 0, "11a: get cts" );
            TEST_ASSERT( lsmash_get_sample_delta_from_media_timeline( demux_root, tid, sample_num, &got_delta ) == 0, "11a: get delta" );
            TEST_ASSERT( lsmash_get_sample_property_from_media_timeline( demux_root, tid, sample_num, &got_prop ) == 0, "11a: get prop" );
            TEST_ASSERT( got_dts == exp_dts && got_cts == exp_dts + exp_off && got_delta == exp_delta,
                         "11a: timeline dts/cts/delta match across 42 pages" );

            if( (i % 300) >= 10 && (i % 300) < 40 && i + 2 < N )
            {
                TEST_ASSERT( (got_prop.ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_POST_ROLL_START) != 0
                          && got_prop.post_roll.complete == sample_num + 2,
                             "11a: deduplicated roll group preserved exact post_roll.complete" );
            }

            /* Spot-check payload bytes across page boundaries */
            if( (i & 0xFF) == 0 || (i & 0xFF) == 0xFF || i == N - 1 )
            {
                lsmash_sample_t *s = lsmash_get_sample_from_media_timeline( demux_root, tid, sample_num );
                TEST_ASSERT( s != NULL && s->length == exp_sz, "11a: boundary sample length matches" );
                TEST_ASSERT( s->data[0] == (uint8_t)((i * 17) & 0xFF), "11a: boundary sample payload matches" );
                lsmash_delete_sample( s );
            }
            exp_dts += exp_delta;
        }

        /* Verify lsmash_get_media_timestamps & lsmash_set_media_timestamps on 10,500-sample timeline */
        lsmash_media_ts_list_t ts_list = { 0 };
        TEST_ASSERT( lsmash_get_media_timestamps( demux_root, tid, &ts_list ) == 0, "11a: get_media_timestamps" );
        TEST_ASSERT( ts_list.sample_count == N && ts_list.timestamp != NULL, "11a: ts_list count == 10,500" );
        TEST_ASSERT( lsmash_set_media_timestamps( demux_root, tid, &ts_list ) == 0, "11a: set_media_timestamps" );
        lsmash_delete_media_timestamps( &ts_list );

        lsmash_destruct_timeline( demux_root, tid );
        lsmash_close_file( &demux_param );
        lsmash_destroy_root( demux_root );
        remove( mp4_path );
    }

    /* 11b. Compact Sample Size Table (stz2 4-bit field_size & lsm_list_move across 3 pages) */
    {
        const char *mp4_path = "tests/tmp_test_stz2_600.mp4";
        const uint32_t M = 600; /* 3 pages */

        lsmash_root_t *mux_root = lsmash_create_root();
        lsmash_file_parameters_t mux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( mp4_path, 0, &mux_param ) == 0, "11b: open stz2 mp4" );
        lsmash_brand_type brands[3] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_ISO6, ISOM_BRAND_TYPE_AV01 };
        mux_param.major_brand = ISOM_BRAND_TYPE_ISOM;
        mux_param.brands      = brands;
        mux_param.brand_count = 3;
        TEST_ASSERT( lsmash_set_file( mux_root, &mux_param ) != NULL, "11b: set stz2 file" );

        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        TEST_ASSERT( lsmash_set_movie_parameters( mux_root, &movie_param ) == 0, "11b: set movie params" );

        uint32_t tid = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        lsmash_track_parameters_t track_param;
        lsmash_initialize_track_parameters( &track_param );
        track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        TEST_ASSERT( lsmash_set_track_parameters( mux_root, tid, &track_param ) == 0, "11b: set track params" );

        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale                 = 1000;
        media_param.compact_sample_size_table = 1; /* Enable stsz -> stz2 compaction via lsm_list_move */
        TEST_ASSERT( lsmash_set_media_parameters( mux_root, tid, &media_param ) == 0, "11b: set media params with compact_sample_size_table" );

        lsmash_video_summary_t *summary = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, tid, summary ) == 1, "11b: add sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );

        for( uint32_t i = 0; i < M; i++ )
        {
            uint32_t sz = 1 + (i % 15); /* 1..15 bytes -> 4-bit stz2 field_size */
            lsmash_sample_t *s = lsmash_create_sample( sz );
            memset( s->data, (int)(i & 0xFF), sz );
            s->dts   = i * 10;
            s->cts   = i * 10;
            s->index = 1;
            s->prop.ra_flags = (i % 60 == 0) ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC : ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
            TEST_ASSERT( lsmash_append_sample( mux_root, tid, s ) == 0, "11b: append 4-bit size sample" );
        }
        TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, tid, 10 ) == 0, "11b: flush pooled samples" );
        TEST_ASSERT( lsmash_finish_movie( mux_root, NULL ) == 0, "11b: finish movie (triggers stsz -> stz2 lsm_list_move)" );
        lsmash_close_file( &mux_param );
        lsmash_destroy_root( mux_root );

        lsmash_root_t *demux_root = lsmash_create_root();
        lsmash_file_parameters_t demux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( mp4_path, 1, &demux_param ) == 0, "11b: open stz2 mp4 for demux" );
        lsmash_file_t *demux_file = lsmash_set_file( demux_root, &demux_param );
        TEST_ASSERT( lsmash_read_file( demux_file, &demux_param ) >= 0, "11b: read stz2 mp4" );

        isom_trak_t *trak = (isom_trak_t *)demux_file->moov->trak_list.head->data;
        isom_stbl_t *stbl = trak->mdia->minf->stbl;
        TEST_ASSERT( LSMASH_IS_NON_EXISTING_BOX( stbl->stsz ) && LSMASH_IS_EXISTING_BOX( stbl->stz2 ),
                     "11b: stsz compacted into stz2" );
        TEST_ASSERT( stbl->stz2->field_size == 4 && lsm_list_count( stbl->stz2->list ) == M,
                     "11b: stz2 has field_size=4 and 600 entries across 3 pages" );

        TEST_ASSERT( lsmash_construct_timeline( demux_root, tid ) == 0, "11b: construct timeline from stz2" );
        for( uint32_t i = 0; i < M; i++ )
        {
            lsmash_sample_t info = { 0 };
            TEST_ASSERT( lsmash_get_sample_info_from_media_timeline( demux_root, tid, i + 1, &info ) == 0, "11b: get sample info" );
            TEST_ASSERT( info.length == 1 + (i % 15), "11b: 4-bit stz2 entry_size matches across 3 pages" );
        }
        lsmash_destruct_timeline( demux_root, tid );
        lsmash_close_file( &demux_param );
        lsmash_destroy_root( demux_root );
        remove( mp4_path );
    }

    /* 11c. Multi-Page Fragmented Movie (fragment->pool, trun->optional, tfra->list, sidx->list) */
    for( int is_segment = 0; is_segment <= 1; is_segment++ )
    {
        const char *fmp4_path = "tests/tmp_test_frag_vfr_900.mp4";
        const uint32_t FRAGS = 3;
        const uint32_t SAMPLES_PER_FRAG = 300; /* >256 elements per fragment -> crosses page boundary in pool & trun->optional */

        lsmash_root_t *mux_root = lsmash_create_root();
        lsmash_file_parameters_t mux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( fmp4_path, 0, &mux_param ) == 0, "11c: open fragmented mp4" );
        mux_param.mode |= LSMASH_FILE_MODE_FRAGMENTED;
        if( is_segment )
            mux_param.mode |= LSMASH_FILE_MODE_INDEX | LSMASH_FILE_MODE_SEGMENT;
        lsmash_brand_type brands[3] = { ISOM_BRAND_TYPE_ISO6, ISOM_BRAND_TYPE_DASH, ISOM_BRAND_TYPE_AV01 };
        mux_param.major_brand = ISOM_BRAND_TYPE_DASH;
        mux_param.brands      = brands;
        mux_param.brand_count = 3;
        TEST_ASSERT( lsmash_set_file( mux_root, &mux_param ) != NULL, "11c: set fragmented file" );

        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        TEST_ASSERT( lsmash_set_movie_parameters( mux_root, &movie_param ) == 0, "11c: set movie params" );

        uint32_t tid = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        lsmash_track_parameters_t track_param;
        lsmash_initialize_track_parameters( &track_param );
        track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        TEST_ASSERT( lsmash_set_track_parameters( mux_root, tid, &track_param ) == 0, "11c: set track params" );

        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale = 90000;
        TEST_ASSERT( lsmash_set_media_parameters( mux_root, tid, &media_param ) == 0, "11c: set media params" );

        lsmash_video_summary_t *summary = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, tid, summary ) == 1, "11c: add sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );

        uint64_t cur_dts = 0;
        for( uint32_t f = 0; f < FRAGS; f++ )
        {
            TEST_ASSERT( lsmash_create_fragment_movie( mux_root ) == 0, "11c: create fragment" );
            for( uint32_t s_idx = 0; s_idx < SAMPLES_PER_FRAG; s_idx++ )
            {
                uint32_t g_idx = f * SAMPLES_PER_FRAG + s_idx;
                uint32_t sz    = 8 + (g_idx % 19);
                uint32_t delta = 300 + (g_idx % 7);
                uint32_t off   = (g_idx % 4) * 50;

                lsmash_sample_t *s = lsmash_create_sample( sz );
                memset( s->data, (int)((g_idx * 13 + 5) & 0xFF), sz );
                s->dts   = cur_dts;
                s->cts   = cur_dts + off;
                s->index = 1;
                s->prop.ra_flags = (s_idx == 0) ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC : ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
                TEST_ASSERT( lsmash_append_sample( mux_root, tid, s ) == 0, "11c: append fragmented VFR sample" );
                cur_dts += delta;
            }
            uint32_t last_frag_delta = 300 + (((f + 1) * SAMPLES_PER_FRAG - 1) % 7);
            TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, tid, last_frag_delta ) == 0, "11c: flush fragment pool" );
        }
        lsmash_adhoc_remux_t remux = { .buffer_size = 65536 };
        TEST_ASSERT( lsmash_finish_movie( mux_root, is_segment ? &remux : NULL ) == 0, "11c: finish fragmented movie" );
        lsmash_close_file( &mux_param );
        lsmash_destroy_root( mux_root );

        lsmash_root_t *demux_root = lsmash_create_root();
        lsmash_file_parameters_t demux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( fmp4_path, 1, &demux_param ) == 0, "11c: open fragmented mp4 for demux" );
        lsmash_file_t *demux_file = lsmash_set_file( demux_root, &demux_param );
        TEST_ASSERT( lsmash_read_file( demux_file, &demux_param ) >= 0, "11c: read fragmented mp4" );

        if( is_segment )
        {
            TEST_ASSERT( demux_file->sidx_list.head != NULL, "11c: sidx_list present" );
            isom_sidx_t *sidx = (isom_sidx_t *)demux_file->sidx_list.head->data;
            TEST_ASSERT( lsm_list_count( sidx->list ) == FRAGS, "11c: sidx->list has 3 subsegment entries" );
        }
        else
        {
            TEST_ASSERT( LSMASH_IS_EXISTING_BOX( demux_file->mfra ) && demux_file->mfra->tfra_list.head != NULL, "11c: mfra/tfra present" );
            isom_tfra_t *tfra = (isom_tfra_t *)demux_file->mfra->tfra_list.head->data;
            TEST_ASSERT( lsm_list_count( tfra->list ) == FRAGS, "11c: tfra->list has 3 sync sample entries" );
        }

        TEST_ASSERT( lsmash_construct_timeline( demux_root, tid ) == 0, "11c: construct fragmented VFR timeline" );
        TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( demux_root, tid ) == FRAGS * SAMPLES_PER_FRAG,
                     "11c: fragmented timeline has 900 samples" );

        uint64_t exp_dts = 0;
        for( uint32_t g_idx = 0; g_idx < FRAGS * SAMPLES_PER_FRAG; g_idx++ )
        {
            uint32_t sz    = 8 + (g_idx % 19);
            uint32_t delta = 300 + (g_idx % 7);
            uint32_t off   = (g_idx % 4) * 50;
            uint64_t got_dts = 0, got_cts = 0;
            lsmash_sample_t info = { 0 };
            TEST_ASSERT( lsmash_get_dts_from_media_timeline( demux_root, tid, g_idx + 1, &got_dts ) == 0, "11c: get frag dts" );
            TEST_ASSERT( lsmash_get_cts_from_media_timeline( demux_root, tid, g_idx + 1, &got_cts ) == 0, "11c: get frag cts" );
            TEST_ASSERT( lsmash_get_sample_info_from_media_timeline( demux_root, tid, g_idx + 1, &info ) == 0, "11c: get frag info" );
            TEST_ASSERT( got_dts == exp_dts && got_cts == exp_dts + off && info.length == sz,
                         "11c: fragmented VFR sample dts/cts/length match across page boundaries" );
            exp_dts += delta;
        }

        lsmash_destruct_timeline( demux_root, tid );
        lsmash_close_file( &demux_param );
        lsmash_destroy_root( demux_root );
        remove( fmp4_path );
    }

    printf( "[PASS] test_large_scale_paged_tables_and_roll_dedup\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 12: AV1 Fragmented ISOBMFF Cross-Feature Mux/Demux Round-Trip
 *---------------------------------------------------------------------------*/
static int test_av1_fragmented_isobmff_roundtrip( void )
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

/*---------------------------------------------------------------------------
 * Test 13: Malformed Box & Sample Table Negative Tests (Rollback & 0-Leak Audit)
 *---------------------------------------------------------------------------*/
static void put_be32( uint8_t *p, uint32_t v )
{
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >>  8) & 0xFF);
    p[3] = (uint8_t)( v        & 0xFF);
}

static int find_fourcc_offset( const uint8_t *buf, size_t len, uint32_t fourcc )
{
    uint8_t pat[4] = {
        (uint8_t)((fourcc >> 24) & 0xFF),
        (uint8_t)((fourcc >> 16) & 0xFF),
        (uint8_t)((fourcc >>  8) & 0xFF),
        (uint8_t)( fourcc        & 0xFF),
    };
    if( len < 8 )
        return -1;
    for( size_t i = 4; i + 4 <= len; i++ )
        if( memcmp( buf + i, pat, 4 ) == 0 )
            return (int)(i - 4); /* offset of box size field */
    return -1;
}

static int test_malformed_boxes_and_sample_tables( void )
{
    const char *base_mp4 = "tests/tmp_test_malformed_base.mp4";
    const char *bad_mp4  = "tests/tmp_test_malformed_bad.mp4";

    /* Step 0: Generate a valid baseline MP4 containing video (with roll/rap sbgp+sgpd and variable stsz)
     * and audio (ipcm with pcmC) so we can binary-patch specific boxes or synthesize malformed boxes. */
    {
        lsmash_root_t *mux_root = lsmash_create_root();
        lsmash_file_parameters_t mux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( base_mp4, 0, &mux_param ) == 0, "13: open base_mp4" );
        lsmash_brand_type brands[3] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_ISO6, ISOM_BRAND_TYPE_AV01 };
        mux_param.major_brand = ISOM_BRAND_TYPE_ISOM;
        mux_param.brands      = brands;
        mux_param.brand_count = 3;
        TEST_ASSERT( lsmash_set_file( mux_root, &mux_param ) != NULL, "13: set base_mp4" );

        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        TEST_ASSERT( lsmash_set_movie_parameters( mux_root, &movie_param ) == 0, "13: set movie params" );

        /* Track 1: Video with variable sample sizes + roll_grouping + rap_grouping */
        uint32_t vtid = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        lsmash_track_parameters_t vtp;
        lsmash_initialize_track_parameters( &vtp );
        vtp.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        lsmash_set_track_parameters( mux_root, vtid, &vtp );
        lsmash_media_parameters_t vmp;
        lsmash_initialize_media_parameters( &vmp );
        vmp.timescale     = 1000;
        vmp.roll_grouping = 1;
        vmp.rap_grouping  = 1;
        lsmash_set_media_parameters( mux_root, vtid, &vmp );
        lsmash_video_summary_t *vsum = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, vtid, vsum ) == 1, "13: add video sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)vsum );

        for( uint32_t i = 0; i < 4; i++ )
        {
            uint32_t sz = 10 + i * 4;
            lsmash_sample_t *s = lsmash_create_sample( sz );
            memset( s->data, (int)(0x10 + i), sz );
            s->dts   = i * 10;
            s->cts   = i * 10;
            s->index = 1;
            s->prop.post_roll.identifier = i + 1;
            if( i == 0 )
                s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            else if( i == 1 )
            {
                s->prop.ra_flags           = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_POST_ROLL_START;
                s->prop.post_roll.complete = i + 2;
            }
            else if( i == 2 )
                s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP;
            else
                s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
            TEST_ASSERT( lsmash_append_sample( mux_root, vtid, s ) == 0, "13: append base video sample" );
        }
        lsmash_flush_pooled_samples( mux_root, vtid, 10 );

        /* Track 2: Audio ipcm 16-bit with pcmC */
        uint32_t atid = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
        lsmash_track_parameters_t atp;
        lsmash_initialize_track_parameters( &atp );
        atp.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        lsmash_set_track_parameters( mux_root, atid, &atp );
        lsmash_media_parameters_t amp;
        lsmash_initialize_media_parameters( &amp );
        amp.timescale = 48000;
        lsmash_set_media_parameters( mux_root, atid, &amp );

        lsmash_audio_summary_t *asum = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
        asum->sample_type      = ISOM_CODEC_TYPE_IPCM_AUDIO;
        asum->frequency        = 48000;
        asum->channels         = 1;
        asum->sample_size      = 16;
        asum->samples_in_frame = 1;
        asum->bytes_per_frame  = 2;
        lsmash_codec_specific_t *pcmc = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_PCM_CONFIG,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        ((lsmash_isom_pcm_config_t *)pcmc->data.structured)->format_flags    = LSMASH_PCM_FORMAT_FLAG_LITTLE_ENDIAN;
        ((lsmash_isom_pcm_config_t *)pcmc->data.structured)->PCM_sample_size = 16;
        lsmash_add_codec_specific_data( (lsmash_summary_t *)asum, pcmc );
        lsmash_destroy_codec_specific_data( pcmc );
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, atid, asum ) == 1, "13: add ipcm sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)asum );

        for( uint32_t i = 0; i < 4; i++ )
        {
            lsmash_sample_t *s = lsmash_create_sample( 2 );
            s->data[0] = (uint8_t)i;
            s->data[1] = (uint8_t)(i + 1);
            s->dts     = i;
            s->cts     = i;
            s->index   = 1;
            s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            TEST_ASSERT( lsmash_append_sample( mux_root, atid, s ) == 0, "13: append base audio sample" );
        }
        lsmash_flush_pooled_samples( mux_root, atid, 1 );
        TEST_ASSERT( lsmash_finish_movie( mux_root, NULL ) == 0, "13: finish base_mp4" );
        lsmash_close_file( &mux_param );
        lsmash_destroy_root( mux_root );
    }

    FILE *bfp = fopen( base_mp4, "rb" );
    TEST_ASSERT( bfp != NULL, "13: open base_mp4 for reading raw bytes" );
    fseek( bfp, 0, SEEK_END );
    size_t base_len = (size_t)ftell( bfp );
    fseek( bfp, 0, SEEK_SET );
    uint8_t *base_buf = (uint8_t *)lsmash_malloc( base_len );
    TEST_ASSERT( base_buf != NULL && fread( base_buf, 1, base_len, bfp ) == base_len, "13: read base_mp4 bytes" );
    fclose( bfp );

    /* 13a. Malformed stsc: first_chunk = 0 (invalid 1-based chunk index) */
    {
        uint8_t *patched = (uint8_t *)lsmash_memdup( base_buf, base_len );
        int stsc_off = find_fourcc_offset( patched, base_len, LSMASH_4CC( 's', 't', 's', 'c' ) );
        TEST_ASSERT( stsc_off >= 0, "13a: locate stsc box" );
        /* stsc layout: size(4) + 'stsc'(4) + version/flags(4) + entry_count(4) + first_chunk(4) */
        put_be32( &patched[stsc_off + 16], 0 ); /* Corrupt first_chunk from 1 to 0 */
        FILE *fp = fopen( bad_mp4, "wb" );
        fwrite( patched, 1, base_len, fp );
        fclose( fp );
        lsmash_free( patched );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13a: open bad_mp4 (stsc first_chunk=0)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) < 0, "13a: lsmash_read_file rejects stsc with first_chunk=0" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 1, "13a: 100% rollback after malformed stsc (only base_buf active)" );
#endif
        remove( bad_mp4 );
    }

    /* 13b. Malformed stsz: sample_size = 0, sample_count = 500, but box only has 4 entries -> insufficient size */
    {
        uint8_t *patched = (uint8_t *)lsmash_memdup( base_buf, base_len );
        int stsz_off = find_fourcc_offset( patched, base_len, LSMASH_4CC( 's', 't', 's', 'z' ) );
        TEST_ASSERT( stsz_off >= 0, "13b: locate stsz box" );
        /* stsz layout: size(4) + 'stsz'(4) + version/flags(4) + sample_size(4) + sample_count(4) */
        put_be32( &patched[stsz_off + 16], 500 ); /* Claim 500 entries in a 4-entry box */
        FILE *fp = fopen( bad_mp4, "wb" );
        fwrite( patched, 1, base_len, fp );
        fclose( fp );
        lsmash_free( patched );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13b: open bad_mp4 (stsz truncated)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) < 0, "13b: lsmash_read_file rejects truncated stsz" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 1, "13b: 100% rollback after truncated stsz" );
#endif
        remove( bad_mp4 );
    }

    /* 13c. Malformed sbgp: out-of-bounds group_description_index = 99 (when sgpd has only 1 entry) */
    {
        uint8_t *patched = (uint8_t *)lsmash_memdup( base_buf, base_len );
        int sbgp_off = find_fourcc_offset( patched, base_len, LSMASH_4CC( 's', 'b', 'g', 'p' ) );
        TEST_ASSERT( sbgp_off >= 0, "13c: locate sbgp box" );
        /* sbgp layout: size(4) + 'sbgp'(4) + version/flags(4) + grouping_type(4) + entry_count(4)
         * + entry[0]: sample_count(4) + group_description_index(4)
         * + entry[1]: sample_count(4) + group_description_index(4) */
        put_be32( &patched[sbgp_off + 24], 99 ); /* Corrupt entry[0].group_description_index to 99 */
        FILE *fp = fopen( bad_mp4, "wb" );
        fwrite( patched, 1, base_len, fp );
        fclose( fp );
        lsmash_free( patched );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13c: open bad_mp4 (sbgp invalid index)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) >= 0, "13c: read_file succeeds before timeline validation" );
        TEST_ASSERT( lsmash_construct_timeline( root, 1 ) < 0,
                     "13c: lsmash_construct_timeline rejects sbgp with out-of-bounds group_description_index=99" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 1, "13c: 100% rollback after invalid sbgp index" );
#endif
        remove( bad_mp4 );
    }

    /* 13d. Malformed pcmC: invalid bit depth PCM_sample_size = 13 on ipcm */
    {
        uint8_t *patched = (uint8_t *)lsmash_memdup( base_buf, base_len );
        int pcmc_off = find_fourcc_offset( patched, base_len, LSMASH_4CC( 'p', 'c', 'm', 'C' ) );
        TEST_ASSERT( pcmc_off >= 0, "13d: locate pcmC box" );
        /* pcmC layout: size(4) + 'pcmC'(4) + version/flags(4) + format_flags(1) + PCM_sample_size(1) */
        patched[pcmc_off + 13] = 13; /* Invalid 13-bit PCM_sample_size */
        FILE *fp = fopen( bad_mp4, "wb" );
        fwrite( patched, 1, base_len, fp );
        fclose( fp );
        lsmash_free( patched );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13d: open bad_mp4 (pcmC invalid bit depth)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) < 0, "13d: lsmash_read_file rejects pcmC with PCM_sample_size=13" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 1, "13d: 100% rollback after malformed pcmC" );
#endif
        remove( bad_mp4 );
    }

    lsmash_free( base_buf );
    remove( base_mp4 );

    /* 13e. Truncated emsg box (underflow hazard on box->size - pos & string cleanup on destruction) */
    {
        /* Build a synthetic file with ftyp + truncated version-0 emsg box where box->size is 20 bytes,
         * but scheme_id_uri("urn:test\0"=9) + value("v\0"=2) + 4*be32(16) consumes 12 + 27 = 39 > 20 bytes! */
        lsmash_bs_t *bs = lsmash_bs_create();
        /* ftyp (16 bytes) */
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_ISOM );
        lsmash_bs_put_be32( bs, 0 );
        /* truncated emsg (declared size = 20, version = 0, flags = 0) */
        lsmash_bs_put_be32( bs, 20 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'e', 'm', 's', 'g' ) );
        lsmash_bs_put_be32( bs, 0 ); /* version=0, flags=0 (12 bytes so far) */
        lsmash_bs_put_bytes( bs, 6, (void *)"urn:a" ); /* 6 bytes including '\0' -> pos=18 */
        lsmash_bs_put_bytes( bs, 2, (void *)"v" );     /* 2 bytes including '\0' -> pos=20, followed by 4 be32 reads -> pos=36 > 20! */

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        FILE *fp = fopen( bad_mp4, "wb" );
        fwrite( raw, 1, raw_len, fp );
        fclose( fp );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13e: open bad_mp4 (truncated emsg)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) < 0,
                     "13e: lsmash_read_file rejects truncated emsg without uint64 underflow" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13e: 100% rollback of emsg->scheme_id_uri and emsg->value on error" );
#endif
        remove( bad_mp4 );
    }

    /* 13f. Truncated sbgp box & malformed must box inside stsd/visual/vexu/eyes */
    {
        /* Truncated sbgp: declares entry_count = 10 but box ends after 1 entry */
        lsmash_bs_t *bs = lsmash_bs_create();
        /* ftyp (16 bytes) */
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_ISOM );
        lsmash_bs_put_be32( bs, 0 );
        /* moov -> trak -> mdia -> minf -> stbl -> sbgp (truncated) */
        uint32_t sbgp_sz = 28; /* 12 (fullbox) + 4 (grouping_type) + 4 (entry_count=10) + 8 (only 1 entry!) */
        uint32_t stbl_sz = 8 + sbgp_sz;
        uint32_t minf_sz = 8 + stbl_sz;
        uint32_t mdia_sz = 8 + minf_sz;
        uint32_t trak_sz = 8 + mdia_sz;
        uint32_t moov_sz = 8 + trak_sz;
        lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
        lsmash_bs_put_be32( bs, trak_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'a', 'k' ) );
        lsmash_bs_put_be32( bs, mdia_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'd', 'i', 'a' ) );
        lsmash_bs_put_be32( bs, minf_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'i', 'n', 'f' ) );
        lsmash_bs_put_be32( bs, stbl_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 't', 'b', 'l' ) );
        lsmash_bs_put_be32( bs, sbgp_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 'b', 'g', 'p' ) );
        lsmash_bs_put_be32( bs, 0 ); /* version=0, flags=0 */
        lsmash_bs_put_be32( bs, ISOM_GROUP_TYPE_ROLL );
        lsmash_bs_put_be32( bs, 10 ); /* entry_count = 10, but only 1 entry follows */
        lsmash_bs_put_be32( bs, 1 );
        lsmash_bs_put_be32( bs, 1 );

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        FILE *fp = fopen( bad_mp4, "wb" );
        fwrite( raw, 1, raw_len, fp );
        fclose( fp );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13f: open bad_mp4 (truncated sbgp)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) < 0, "13f: lsmash_read_file rejects truncated sbgp" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13f: 100% rollback after truncated sbgp" );
#endif
        remove( bad_mp4 );
    }

    /* 13g. Excessive box nesting depth (> ISOM_MAX_BOX_NESTING_LEVEL = 64) */
    {
        const int depth = 70;
        lsmash_bs_t *bs = lsmash_bs_create();
        /* ftyp (16 bytes) */
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_ISOM );
        lsmash_bs_put_be32( bs, 0 );
        /* 70 nested proj boxes: each box header is 8 bytes, innermost has 0 payload */
        for( int d = depth; d >= 1; d-- )
        {
            lsmash_bs_put_be32( bs, (uint32_t)(d * 8) );
            lsmash_bs_put_be32( bs, LSMASH_4CC( 'p', 'r', 'o', 'j' ) );
        }

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        FILE *fp = fopen( bad_mp4, "wb" );
        fwrite( raw, 1, raw_len, fp );
        fclose( fp );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13g: open bad_mp4 (deeply nested boxes)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13g: lsmash_read_file rejects box nesting depth > 64" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13g: 100% rollback after excessive box nesting depth" );
#endif
        remove( bad_mp4 );
    }

    /* 13h. Child box size exceeding parent box boundary & invalid largesize (< 16) */
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        /* ftyp (16 bytes) */
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_ISOM );
        lsmash_bs_put_be32( bs, 0 );
        /* moov claiming 24 bytes total, containing a child trak claiming 1024 bytes */
        lsmash_bs_put_be32( bs, 24 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
        lsmash_bs_put_be32( bs, 1024 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'a', 'k' ) );
        lsmash_bs_put_be64( bs, 0 );

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        FILE *fp = fopen( bad_mp4, "wb" );
        fwrite( raw, 1, raw_len, fp );
        fclose( fp );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13h: open bad_mp4 (child size > parent size)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13h: lsmash_read_file rejects child box exceeding parent boundary" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13h: 100% rollback after child box exceeding parent boundary" );
#endif
        remove( bad_mp4 );
    }

    /* 13i. Truncated chan box with excessive numberChannelDescriptions */
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        /* ftyp (16 bytes) */
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_QT );
        lsmash_bs_put_be32( bs, 0 );
        /* moov -> trak -> mdia -> (hdlr + minf -> stbl -> stsd -> lpcm -> chan) */
        uint32_t chan_sz = 24; /* 12 (FullBox) + 4 (layoutTag) + 4 (bitmap) + 4 (numberChannelDescriptions=0x10000000) */
        uint32_t lpcm_sz = 36 + chan_sz;
        uint32_t stsd_sz = 16 + lpcm_sz;
        uint32_t stbl_sz = 8 + stsd_sz;
        uint32_t minf_sz = 8 + stbl_sz;
        uint32_t hdlr_sz = 33;
        uint32_t mdia_sz = 8 + hdlr_sz + minf_sz;
        uint32_t trak_sz = 8 + mdia_sz;
        uint32_t moov_sz = 8 + trak_sz;
        lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
        lsmash_bs_put_be32( bs, trak_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'a', 'k' ) );
        lsmash_bs_put_be32( bs, mdia_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'd', 'i', 'a' ) );
        /* hdlr (soun) */
        lsmash_bs_put_be32( bs, hdlr_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'h', 'd', 'l', 'r' ) );
        lsmash_bs_put_be32( bs, 0 ); /* version/flags */
        lsmash_bs_put_be32( bs, 0 ); /* componentType */
        lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 'o', 'u', 'n' ) );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_byte( bs, 0 );
        /* minf -> stbl -> stsd -> lpcm -> chan */
        lsmash_bs_put_be32( bs, minf_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'i', 'n', 'f' ) );
        lsmash_bs_put_be32( bs, stbl_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 't', 'b', 'l' ) );
        lsmash_bs_put_be32( bs, stsd_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 't', 's', 'd' ) );
        lsmash_bs_put_be32( bs, 0 ); /* version/flags */
        lsmash_bs_put_be32( bs, 1 ); /* entry_count */
        lsmash_bs_put_be32( bs, lpcm_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'l', 'p', 'c', 'm' ) );
        lsmash_bs_put_be32( bs, 0 ); lsmash_bs_put_be16( bs, 0 ); /* reserved[6] */
        lsmash_bs_put_be16( bs, 1 ); /* data_reference_index */
        lsmash_bs_put_be16( bs, 0 ); /* version = 0 */
        lsmash_bs_put_be16( bs, 0 ); /* revision_level */
        lsmash_bs_put_be32( bs, 0 ); /* vendor */
        lsmash_bs_put_be16( bs, 2 ); /* channelcount */
        lsmash_bs_put_be16( bs, 16 ); /* samplesize */
        lsmash_bs_put_be16( bs, 0 ); /* compression_ID */
        lsmash_bs_put_be16( bs, 0 ); /* packet_size */
        lsmash_bs_put_be32( bs, 48000U << 16 ); /* samplerate */
        /* chan box claiming 0x10000000 channel descriptions in a 24-byte box */
        lsmash_bs_put_be32( bs, chan_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'c', 'h', 'a', 'n' ) );
        lsmash_bs_put_be32( bs, 0 ); /* version/flags */
        lsmash_bs_put_be32( bs, 0 ); /* channelLayoutTag */
        lsmash_bs_put_be32( bs, 0 ); /* channelBitmap */
        lsmash_bs_put_be32( bs, 0x10000000U ); /* numberChannelDescriptions */

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        FILE *fp = fopen( bad_mp4, "wb" );
        fwrite( raw, 1, raw_len, fp );
        fclose( fp );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13i: open bad_mp4 (truncated chan)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13i: lsmash_read_file rejects truncated chan with excessive numberChannelDescriptions" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13i: 100% rollback after truncated chan" );
#endif
        remove( bad_mp4 );
    }

    printf( "[PASS] test_malformed_boxes_and_sample_tables\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 14: Multi-Buffer Resize & Codec / Metadata Regressions
 *---------------------------------------------------------------------------*/
static int test_multibuf_resize_and_codec_regressions( void )
{
    /* 14a. lsmash_multiple_buffers_t Creation, Expansion, Shrinkage & Data Integrity */
    {
        TEST_ASSERT( lsmash_create_multiple_buffers( 0, 32 ) == NULL, "14a: reject 0 buffers" );
        TEST_ASSERT( lsmash_create_multiple_buffers( 4, 0 ) == NULL, "14a: reject 0 buffer_size" );
        TEST_ASSERT( lsmash_create_multiple_buffers( 0x100000, 0x10000 ) == NULL, "14a: reject uint32 overflow" );

        lsmash_multiple_buffers_t *bank = lsmash_create_multiple_buffers( 4, 32 );
        TEST_ASSERT( bank != NULL && bank->number_of_buffers == 4 && bank->buffer_size == 32,
                     "14a: create 4x32 multiple buffers" );
        TEST_ASSERT( lsmash_withdraw_buffer( bank, 0 ) == NULL, "14a: reject buffer_number 0" );
        TEST_ASSERT( lsmash_withdraw_buffer( bank, 5 ) == NULL, "14a: reject buffer_number > 4" );
        TEST_ASSERT( lsmash_resize_multiple_buffers( bank, 0 ) == NULL, "14a: reject resize to 0" );

        /* Fill each of the 4 buffers with a unique byte pattern */
        for( uint32_t b = 1; b <= 4; b++ )
        {
            uint8_t *buf = (uint8_t *)lsmash_withdraw_buffer( bank, b );
            TEST_ASSERT( buf != NULL, "14a: withdraw buffer 1..4" );
            for( uint32_t k = 0; k < 32; k++ )
                buf[k] = (uint8_t)((b * 53U + k * 7U + 0x11U) & 0xFFU);
        }

        /* No-op resize to same size */
        TEST_ASSERT( lsmash_resize_multiple_buffers( bank, 32 ) == bank, "14a: same-size resize is no-op" );

        /* Expand from 32 to 96 bytes per buffer */
        TEST_ASSERT( lsmash_resize_multiple_buffers( bank, 96 ) == bank && bank->buffer_size == 96,
                     "14a: expand multiple buffers from 32 to 96 bytes" );
        for( uint32_t b = 1; b <= 4; b++ )
        {
            uint8_t *buf = (uint8_t *)lsmash_withdraw_buffer( bank, b );
            TEST_ASSERT( buf != NULL, "14a: withdraw expanded buffer" );
            for( uint32_t k = 0; k < 32; k++ )
            {
                uint8_t expected = (uint8_t)((b * 53U + k * 7U + 0x11U) & 0xFFU);
                TEST_ASSERT( buf[k] == expected,
                             "14a: expanded buffer preserves exact original data in [0..31] across all 4 buffers" );
            }
            for( uint32_t k = 32; k < 96; k++ )
            {
                TEST_ASSERT( buf[k] == 0, "14a: expanded tail [32..95] is zero-initialized" );
                buf[k] = (uint8_t)((b * 29U + k) & 0xFFU);
            }
        }

        /* Shrink from 96 to 20 bytes per buffer */
        TEST_ASSERT( lsmash_resize_multiple_buffers( bank, 20 ) == bank && bank->buffer_size == 20,
                     "14a: shrink multiple buffers from 96 to 20 bytes" );
        for( uint32_t b = 1; b <= 4; b++ )
        {
            uint8_t *buf = (uint8_t *)lsmash_withdraw_buffer( bank, b );
            TEST_ASSERT( buf != NULL, "14a: withdraw shrunk buffer" );
            for( uint32_t k = 0; k < 20; k++ )
            {
                uint8_t expected = (uint8_t)((b * 53U + k * 7U + 0x11U) & 0xFFU);
                TEST_ASSERT( buf[k] == expected,
                             "14a: shrunk buffer preserves exact prefix [0..19] across all 4 buffers" );
            }
        }

        lsmash_destroy_multiple_buffers( bank );
    }

    /* 14b. lsm_nalu_get_codeNum Exp-Golomb Valid Decoding, EOF & >=64 Zero Bits Non-Hang */
    {
        /* Valid Exp-Golomb codes packed into 1 byte:
         * '1' (0) + '010' (1) + '011' (2) + '1' (0) = 0b10100111 = 0xA7 */
        uint8_t valid_eg[1] = { 0xA7 };
        lsmash_bits_t *bits = lsmash_bits_adhoc_create();
        TEST_ASSERT( bits != NULL, "14b: create adhoc bits" );
        TEST_ASSERT( lsmash_bits_import_data( bits, valid_eg, sizeof(valid_eg) ) == 0, "14b: import valid Exp-Golomb" );
        TEST_ASSERT( lsm_nalu_get_codeNum( bits ) == 0, "14b: Exp-Golomb '1' -> 0" );
        TEST_ASSERT( lsm_nalu_get_codeNum( bits ) == 1, "14b: Exp-Golomb '010' -> 1" );
        TEST_ASSERT( lsm_nalu_get_codeNum( bits ) == 2, "14b: Exp-Golomb '011' -> 2" );
        TEST_ASSERT( lsm_nalu_get_codeNum( bits ) == 0, "14b: Exp-Golomb '1' -> 0" );
        lsmash_bits_adhoc_cleanup( bits );

        /* Truncated 1-byte all-zero stream -> hits EOB and returns 0 without infinite loop */
        uint8_t eof_zeros[1] = { 0x00 };
        bits = lsmash_bits_adhoc_create();
        TEST_ASSERT( lsmash_bits_import_data( bits, eof_zeros, sizeof(eof_zeros) ) == 0, "14b: import 1 zero byte" );
        TEST_ASSERT( lsm_nalu_get_codeNum( bits ) == 0 && bits->bs->eob == 1,
                     "14b: lsm_nalu_get_codeNum terminates on EOB and returns 0" );
        lsmash_bits_adhoc_cleanup( bits );

        /* 16-byte all-zero stream (128 zero bits) -> hits leadingZeroBits >= 64 before EOB and returns 0 */
        uint8_t long_zeros[16] = { 0 };
        bits = lsmash_bits_adhoc_create();
        TEST_ASSERT( lsmash_bits_import_data( bits, long_zeros, sizeof(long_zeros) ) == 0, "14b: import 16 zero bytes" );
        TEST_ASSERT( lsm_nalu_get_codeNum( bits ) == 0 && bits->bs->eob == 0,
                     "14b: lsm_nalu_get_codeNum terminates at 64 leading zero bits without hanging" );
        lsmash_bits_adhoc_cleanup( bits );
    }

    /* 14c. MP3 nBlockSize Calculation in lsm_qt_wfex_update_bitrate */
    {
        /* Build a minimal in-memory stbl + mdhd with a QT_CODEC_TYPE_FULLMP3_AUDIO sample entry and MPEGLAYER3WAVEFORMAT */
        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        const char *tmp_mov = "tests/tmp_wfex_mp3.mov";
        TEST_ASSERT( lsmash_open_file( tmp_mov, 0, &param ) == 0, "14c: open tmp mov" );
        param.major_brand = ISOM_BRAND_TYPE_QT;
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( file != NULL, "14c: set file" );
        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        movie_param.timescale = 600;
        TEST_ASSERT( lsmash_set_movie_parameters( root, &movie_param ) == 0, "14c: set movie params" );
        uint32_t tid = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
        TEST_ASSERT( tid == 1, "14c: create audio track" );
        isom_trak_t *trak = (isom_trak_t *)file->moov->trak_list.head->data;
        isom_mdhd_t *mdhd = trak->mdia->mdhd;
        mdhd->timescale   = 48000;
        mdhd->duration    = 1152;
        isom_stbl_t *stbl = trak->mdia->minf->stbl;

        isom_audio_entry_t *mp3_entry = lsm_box_add_audio_description( stbl->stsd, QT_CODEC_TYPE_FULLMP3_AUDIO );
        TEST_ASSERT( LSMASH_IS_EXISTING_BOX( mp3_entry ), "14c: add .mp3 audio description" );
        isom_wave_t *wave = lsm_box_add_wave( mp3_entry );
        TEST_ASSERT( LSMASH_IS_EXISTING_BOX( wave ), "14c: add wave box" );

        /* MPEGLAYER3WAVEFORMAT: 8 (box header) + 18 (WAVEFORMATEX) + 12 (cbSize=12) = 38 bytes
         * Note: lsm_box_add_extension_binary takes ownership of wfex_buf and frees it on root destruction. */
        uint8_t *wfex_buf = (uint8_t *)lsmash_malloc_zero( 38 );
        TEST_ASSERT( wfex_buf != NULL, "14c: alloc wfex_buf" );
        put_be32( &wfex_buf[0], 38 );
        put_be32( &wfex_buf[4], QT_CODEC_TYPE_FULLMP3_AUDIO.fourcc );
        uint8_t *ex = &wfex_buf[8];
        put_le16( &ex[0],  0x0055 ); /* wFormatTag = WAVE_FORMAT_MPEGLAYER3 */
        put_le16( &ex[2],  2 );      /* nChannels = 2 */
        put_le32( &ex[4],  48000 );  /* nSamplesPerSec = 48000 */
        put_le16( &ex[16], 12 );     /* cbSize = 12 */
        put_le16( &ex[26], 1 );      /* nFramesPerBlock = 1 */
        TEST_ASSERT( lsm_box_add_extension_binary( wave, QT_CODEC_TYPE_FULLMP3_AUDIO, 0, wfex_buf, 38 ) == 0,
                     "14c: attach MPEGLAYER3WAVEFORMAT binary box" );

        /* Add 1 sample of 384 bytes with duration 1152 at 48000 Hz -> avgBitrate = 384 * 8 * 48000 / 1152 = 128000 bps */
        isom_stts_t *stts = stbl->stts;
        isom_stsz_t *stsz = stbl->stsz;
        isom_stsc_t *stsc = stbl->stsc;
        isom_stts_entry_t *stts_e = (isom_stts_entry_t *)lsm_list_append( stts->list );
        stts_e->sample_count = 1;
        stts_e->sample_delta = 1152;
        stsz->sample_size  = 384;
        stsz->sample_count = 1;
        isom_stsc_entry_t *stsc_e = (isom_stsc_entry_t *)lsm_list_append( stsc->list );
        stsc_e->first_chunk              = 1;
        stsc_e->samples_per_chunk        = 1;
        stsc_e->sample_description_index = 1;

        TEST_ASSERT( lsm_qt_wfex_update_bitrate( stbl, mdhd, 1 ) == 0,
                     "14c: lsm_qt_wfex_update_bitrate succeeds" );
        isom_box_t *wave_ext = lsm_box_get_extension_box( &wave->extensions, QT_CODEC_TYPE_FULLMP3_AUDIO );
        TEST_ASSERT( wave_ext != NULL && wave_ext->binary != NULL, "14c: get updated wave_ext" );
        uint16_t nBlockSize = LSMASH_GET_LE16( &wave_ext->binary[8 + 24] );
        /* 144 * 128000 / 48000 = 384 (previously truncated to 144 * (128000 / 48000) = 288!) */
        TEST_ASSERT( nBlockSize == 384, "14c: MP3 nBlockSize is 384 (not 288 from premature integer division)" );

        /* Also verify cbSize < 12 is rejected without out-of-bounds write */
        put_le16( &wave_ext->binary[8 + 16], 4 );
        TEST_ASSERT( lsm_qt_wfex_update_bitrate( stbl, mdhd, 1 ) == LSMASH_ERR_INVALID_DATA,
                     "14c: lsm_qt_wfex_update_bitrate rejects MP3 wfex with cbSize < 12" );

        lsmash_close_file( &param );
        lsmash_destroy_root( root );
        remove( tmp_mov );
    }

    /* 14d. WMA cbSize 16-bit LE Reading & Bounds Check in lsm_wma_print_codec_specific */
    {
        uint8_t wma_bin[36] = { 0 };
        put_be32( &wma_bin[0], 36 );
        put_be32( &wma_bin[4], LSMASH_4CC( 'w', 'f', 'e', 'x' ) );
        uint8_t *d = &wma_bin[8];
        put_le16( &d[0],  0x0161 ); /* WAVE_FORMAT_TAG_ID_WMA_V2 */
        put_le16( &d[2],  2 );
        put_le32( &d[4],  44100 );
        put_le32( &d[8],  16000 );
        put_le16( &d[12], 743 );
        put_le16( &d[14], 16 );
        put_le16( &d[16], 10 );     /* cbSize = 10 in LE16 */
        put_le32( &d[18], 2048 );   /* dwSamplesPerBlock */

        isom_box_t box = { 0 };
        box.manager = LSMASH_BINARY_CODED_BOX;
        box.type    = ISOM_BOX_TYPE_WFEX;
        box.size    = 36;
        box.binary  = wma_bin;

        const char *tmp_wma_dump = "tests/tmp_wma_dump.txt";
        FILE *fp = fopen( tmp_wma_dump, "wb" );
        TEST_ASSERT( fp != NULL, "14d: open tmp_wma_dump" );
        TEST_ASSERT( lsm_wma_print_codec_specific( fp, NULL, &box, 0 ) == 0,
                     "14d: lsm_wma_print_codec_specific succeeds for valid WMA v2 wfex" );
        /* Now set cbSize = 0x010A (266 in LE16, whose low byte is 10) while box.size remains 36 */
        put_le16( &d[16], 0x010A );
        TEST_ASSERT( lsm_wma_print_codec_specific( fp, NULL, &box, 0 ) == LSMASH_ERR_INVALID_DATA,
                     "14d: lsm_wma_print_codec_specific reads 16-bit LE cbSize=266 and rejects truncated box" );
        fclose( fp );
        remove( tmp_wma_dump );
    }

    /* 14e. Opus 2ch & 8ch Channel Order Lookup in lsm_opus_print_codec_specific */
    {
        lsmash_opus_specific_parameters_t opus_param = { 0 };
        opus_param.Version              = 0;
        opus_param.OutputChannelCount   = 8;
        opus_param.PreSkip              = 312;
        opus_param.InputSampleRate      = 48000;
        opus_param.OutputGain           = 0;
        opus_param.ChannelMappingFamily = 1;
        opus_param.StreamCount          = 5;
        opus_param.CoupledCount         = 3;
        for( uint8_t i = 0; i < 8; i++ )
            opus_param.ChannelMapping[i] = i;

        uint32_t dops_len = 0;
        uint8_t *dops_bin = lsmash_create_opus_specific_info( &opus_param, &dops_len );
        TEST_ASSERT( dops_bin != NULL && dops_len == 29, "14e: create 8ch dOps box" );

        lsmash_file_t dummy_file = { 0 };
        isom_box_t box = { 0 };
        box.manager = LSMASH_BINARY_CODED_BOX;
        box.type    = ISOM_BOX_TYPE_DOPS;
        box.size    = dops_len;
        box.binary  = dops_bin;

        const char *tmp_opus_dump = "tests/tmp_opus_dump.txt";
        FILE *fp = fopen( tmp_opus_dump, "wb" );
        TEST_ASSERT( fp != NULL, "14e: open tmp_opus_dump" );
        TEST_ASSERT( lsm_opus_print_codec_specific( fp, &dummy_file, &box, 0 ) == 0,
                     "14e: lsm_opus_print_codec_specific succeeds for 8ch ChannelMappingFamily=1" );
        fclose( fp );
        lsmash_free( dops_bin );

        FILE *rfp = fopen( tmp_opus_dump, "rb" );
        TEST_ASSERT( rfp != NULL, "14e: open opus dump for read" );
        fseek( rfp, 0, SEEK_END );
        size_t dump_len = (size_t)ftell( rfp );
        fseek( rfp, 0, SEEK_SET );
        char *dump_str = (char *)lsmash_malloc_zero( dump_len + 1 );
        TEST_ASSERT( dump_str != NULL && fread( dump_str, 1, dump_len, rfp ) == dump_len, "14e: read opus dump" );
        fclose( rfp );
        TEST_ASSERT( strstr( dump_str, "1 -> 1: front center" ) != NULL,
                     "14e: 8ch Opus channel 1 printed as 'front center' (not 'fron center')" );
        TEST_ASSERT( strstr( dump_str, "7 -> 7: LFE" ) != NULL,
                     "14e: 8ch Opus channel 7 printed as 'LFE' via channel_order[OutputChannelCount - 1]" );
        lsmash_free( dump_str );
        remove( tmp_opus_dump );
    }

    /* 14f. Custom iTunes Metadata Retrieval Type & lsmash_count_tyrant_chapter */
    {
        TEST_ASSERT( lsmash_count_tyrant_chapter( NULL ) == 0,
                     "14f: lsmash_count_tyrant_chapter(NULL) safely returns 0 without NULL deref" );

        const char *chap_path = "tests/tmp_tyrant_chapters.txt";
        const char *m4a_path  = "tests/tmp_itunes_meta.m4a";
        FILE *cfp = fopen( chap_path, "wb" );
        TEST_ASSERT( cfp != NULL, "14f: create chapter file" );
        fprintf( cfp, "CHAPTER01=00:00:00.000\nCHAPTER01NAME=Intro\nCHAPTER02=00:00:01.000\nCHAPTER02NAME=Main\n" );
        fclose( cfp );

        lsmash_root_t *mux_root = lsmash_create_root();
        lsmash_file_parameters_t mux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( m4a_path, 0, &mux_param ) == 0, "14f: open m4a for mux" );
        lsmash_brand_type brands[3] = { ISOM_BRAND_TYPE_M4A, ISOM_BRAND_TYPE_MP42, ISOM_BRAND_TYPE_ISOM };
        mux_param.major_brand = ISOM_BRAND_TYPE_M4A;
        mux_param.brands      = brands;
        mux_param.brand_count = 3;
        TEST_ASSERT( lsmash_set_file( mux_root, &mux_param ) != NULL, "14f: set m4a file" );

        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        lsmash_set_movie_parameters( mux_root, &movie_param );

        uint32_t tid = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        lsmash_track_parameters_t track_param;
        lsmash_initialize_track_parameters( &track_param );
        track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        lsmash_set_track_parameters( mux_root, tid, &track_param );
        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale = 1000;
        lsmash_set_media_parameters( mux_root, tid, &media_param );

        lsmash_video_summary_t *vsum = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, tid, vsum ) == 1, "14f: add sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)vsum );

        for( uint32_t i = 0; i < 2; i++ )
        {
            lsmash_sample_t *s = lsmash_create_sample( 8 );
            memset( s->data, (int)(i + 1), 8 );
            s->dts   = (uint64_t)i * 1000;
            s->cts   = (uint64_t)i * 1000;
            s->index = 1;
            s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            TEST_ASSERT( lsmash_append_sample( mux_root, tid, s ) == 0, "14f: append sample" );
        }
        TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, tid, 1000 ) == 0, "14f: flush samples" );
        TEST_ASSERT( lsmash_update_track_duration( mux_root, tid, 1000 ) == 0, "14f: update duration" );

        /* Set 2 chapters in chpl */
        TEST_ASSERT( lsmash_set_tyrant_chapter( mux_root, (char *)chap_path, 0 ) == 0, "14f: set tyrant chapter" );

        /* Set custom STRING and custom INTEGER iTunes metadata */
        lsmash_itunes_metadata_t custom_str = { 0 };
        custom_str.item         = ITUNES_METADATA_ITEM_CUSTOM;
        custom_str.type         = ITUNES_METADATA_TYPE_STRING;
        custom_str.meaning      = "com.apple.iTunes";
        custom_str.name         = "CUSTOM_TAG_STR";
        custom_str.value.string = "L-SMASH Custom String";
        TEST_ASSERT( lsmash_set_itunes_metadata( mux_root, custom_str ) == 0, "14f: set custom string metadata" );

        lsmash_itunes_metadata_t custom_int = { 0 };
        custom_int.item          = ITUNES_METADATA_ITEM_CUSTOM;
        custom_int.type          = ITUNES_METADATA_TYPE_INTEGER;
        custom_int.meaning       = "com.apple.iTunes";
        custom_int.name          = "CUSTOM_TAG_INT";
        custom_int.value.integer = 20261021ULL;
        TEST_ASSERT( lsmash_set_itunes_metadata( mux_root, custom_int ) == 0, "14f: set custom int metadata" );

        TEST_ASSERT( lsmash_finish_movie( mux_root, NULL ) == 0, "14f: finish m4a" );
        lsmash_close_file( &mux_param );
        lsmash_destroy_root( mux_root );
        remove( chap_path );

        /* Reopen and verify lsmash_count_tyrant_chapter & lsmash_get_itunes_metadata */
        lsmash_root_t *demux_root = lsmash_create_root();
        lsmash_file_parameters_t demux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( m4a_path, 1, &demux_param ) == 0, "14f: open m4a for read" );
        lsmash_file_t *demux_file = lsmash_set_file( demux_root, &demux_param );
        TEST_ASSERT( lsmash_read_file( demux_file, &demux_param ) >= 0, "14f: read m4a" );

        TEST_ASSERT( lsmash_count_tyrant_chapter( demux_root ) == 2,
                     "14f: lsmash_count_tyrant_chapter returns 2" );
        double chap_ts = -1.0;
        char *chap_name = lsmash_get_tyrant_chapter( demux_root, 2, &chap_ts );
        TEST_ASSERT( chap_name != NULL && strcmp( chap_name, "Main" ) == 0 && chap_ts == 1.0,
                     "14f: lsmash_get_tyrant_chapter(2) matches 'Main' at 1.0s" );

        TEST_ASSERT( lsmash_count_itunes_metadata( demux_root ) == 2, "14f: count_itunes_metadata == 2" );

        lsmash_itunes_metadata_t got_str = { 0 };
        TEST_ASSERT( lsmash_get_itunes_metadata( demux_root, 1, &got_str ) == 0, "14f: get custom string metadata" );
        TEST_ASSERT( got_str.item == ITUNES_METADATA_ITEM_CUSTOM
                  && got_str.type == ITUNES_METADATA_TYPE_STRING
                  && got_str.value.string != NULL
                  && strcmp( got_str.value.string, "L-SMASH Custom String" ) == 0,
                     "14f: custom string metadata retrieved with ITUNES_METADATA_TYPE_STRING" );
        lsmash_cleanup_itunes_metadata( &got_str );

        lsmash_itunes_metadata_t got_int = { 0 };
        TEST_ASSERT( lsmash_get_itunes_metadata( demux_root, 2, &got_int ) == 0, "14f: get custom int metadata" );
        TEST_ASSERT( got_int.item == ITUNES_METADATA_ITEM_CUSTOM
                  && got_int.type == ITUNES_METADATA_TYPE_INTEGER
                  && got_int.value.integer == 20261021ULL,
                     "14f: custom integer metadata retrieved with ITUNES_METADATA_TYPE_INTEGER" );
        lsmash_cleanup_itunes_metadata( &got_int );

        lsmash_close_file( &demux_param );
        lsmash_destroy_root( demux_root );
        remove( m4a_path );
    }

    printf( "[PASS] test_multibuf_resize_and_codec_regressions\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 15: FLAC-in-ISOBMFF (fLaC / dfLa) Importer & Mux/Demux Round-Trip
 *---------------------------------------------------------------------------*/
extern uint8_t lsm_flac_crc8( const uint8_t *data, uint32_t len );

static uint32_t build_synth_flac_frame
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

static int test_flac_isobmff_roundtrip( void )
{
    /* 1. Verify CRC-8 lookup table against bit-by-bit reference */
    for( uint32_t b = 0; b < 256; b++ )
    {
        uint8_t byte_val = (uint8_t)b;
        uint8_t ref = byte_val;
        for( int j = 0; j < 8; j++ )
            ref = (ref & 0x80) ? (uint8_t)((ref << 1) ^ 0x07) : (uint8_t)(ref << 1);
        TEST_ASSERT( lsm_flac_crc8( &byte_val, 1 ) == ref, "FLAC CRC-8 single-byte table match" );
    }

    /* 2. Negative checks for lsmash_create_flac_specific_info */
    uint32_t dummy_len = 0;
    uint8_t bad_meta[40] = { 0x84, 0x00, 0x00, 0x22 }; /* BlockType=4 instead of 0 (STREAMINFO) */
    TEST_ASSERT( lsmash_create_flac_specific_info( NULL, 38, &dummy_len ) == NULL, "reject NULL metadata_blocks" );
    TEST_ASSERT( lsmash_create_flac_specific_info( bad_meta, 20, &dummy_len ) == NULL, "reject short metadata_blocks" );
    TEST_ASSERT( lsmash_create_flac_specific_info( bad_meta, 38, &dummy_len ) == NULL, "reject non-STREAMINFO first block" );

    /* 3. Synthesize a raw .flac bitstream with 96 kHz 24-bit 2ch STREAMINFO, VORBIS_COMMENT, and 4 variable-block frames */
    const char *flac_path = "test_synth_flac.flac";
    const char *mp4_path  = "test_synth_flac.mp4";
    const char *dump_path = "test_synth_flac_dump.txt";

    uint8_t  frames[4][128];
    uint32_t frame_sizes[4];
    uint32_t block_sizes[4]  = { 4096, 4096, 4096, 1500 }; /* last frame uses uncommon 16-bit block size (bs_code=7) */
    uint64_t sample_nums[4]  = { 0, 4096, 8192, 12288 };
    for( int i = 0; i < 4; i++ )
        frame_sizes[i] = build_synth_flac_frame( frames[i], 1, block_sizes[i], sample_nums[i], 32 );

    {
        lsmash_bs_t *bs = lsmash_bs_create();
        TEST_ASSERT( bs != NULL, "create bs for synth flac" );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 'L', 'a', 'C' ) );

        /* Metadata Block 0: STREAMINFO (last=0, type=0, len=34) */
        lsmash_bs_put_byte( bs, 0x00 );
        lsmash_bs_put_be24( bs, 34 );
        lsmash_bs_put_be16( bs, 1024 ); /* min_block_size = 1024 */
        lsmash_bs_put_be16( bs, 4096 ); /* max_block_size = 4096 */
        lsmash_bs_put_be24( bs, 32 );   /* min_frame_size */
        lsmash_bs_put_be24( bs, 128 );  /* max_frame_size */
        /* sample_rate (20b) = 96000 (0x17700), channels-1 (3b) = 1, bps-1 (5b) = 23 (0x17), total_samples (36b) = 13788 */
        uint64_t sr_ch_bps_tot = ((uint64_t)96000 << 44)
                               | ((uint64_t)1     << 41)
                               | ((uint64_t)23    << 36)
                               |  (uint64_t)13788;
        lsmash_bs_put_be64( bs, sr_ch_bps_tot );
        for( int i = 0; i < 16; i++ )
            lsmash_bs_put_byte( bs, (uint8_t)(0xA0 + i) ); /* MD5 signature */

        /* Metadata Block 1: VORBIS_COMMENT (last=1, type=4, len=12) */
        lsmash_bs_put_byte( bs, 0x84 );
        lsmash_bs_put_be24( bs, 12 );
        lsmash_bs_put_le32( bs, 4 );
        lsmash_bs_put_bytes( bs, 4, (void *)"L-SM" );
        lsmash_bs_put_le32( bs, 0 );

        /* Audio Frames */
        for( int i = 0; i < 4; i++ )
            lsmash_bs_put_bytes( bs, frame_sizes[i], frames[i] );

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        FILE *fp = fopen( flac_path, "wb" );
        TEST_ASSERT( fp != NULL, "open synth flac file" );
        fwrite( raw, 1, raw_len, fp );
        fclose( fp );
        lsmash_free( raw );
    }

    /* 4. Import raw FLAC and mux into ISOBMFF MP4 */
    {
        lsmash_root_t *imp_root = lsmash_create_root();
        importer_t *imp = lsmash_importer_open( imp_root, flac_path, "auto" );
        TEST_ASSERT( imp != NULL, "open flac importer" );

        lsmash_audio_summary_t *sum = (lsmash_audio_summary_t *)lsmash_duplicate_summary( imp, 1 );
        TEST_ASSERT( sum != NULL, "duplicate flac summary" );
        TEST_ASSERT( lsmash_check_codec_type_identical( sum->sample_type, ISOM_CODEC_TYPE_FLAC_AUDIO ), "summary is fLaC" );
        TEST_ASSERT( sum->frequency == 96000 && sum->channels == 2 && sum->sample_size == 24 && sum->samples_in_frame == 4096,
                     "flac importer parsed 96kHz 2ch 24-bit max_bs=4096 from STREAMINFO" );

        lsmash_root_t *mux_root = lsmash_create_root();
        lsmash_file_parameters_t mux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( mp4_path, 0, &mux_param ) == 0, "open flac mp4 for write" );
        mux_param.major_brand   = ISOM_BRAND_TYPE_ISOM;
        mux_param.minor_version = 512;
        lsmash_brand_type brands[2] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_ISO2 };
        mux_param.brands      = brands;
        mux_param.brand_count = 2;
        TEST_ASSERT( lsmash_set_file( mux_root, &mux_param ) != NULL, "set flac mp4 file" );

        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        TEST_ASSERT( lsmash_set_movie_parameters( mux_root, &movie_param ) == 0, "set movie params" );

        uint32_t tid = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
        TEST_ASSERT( tid == 1, "create audio track" );

        lsmash_track_parameters_t track_param;
        lsmash_initialize_track_parameters( &track_param );
        track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
        TEST_ASSERT( lsmash_set_track_parameters( mux_root, tid, &track_param ) == 0, "set track params" );

        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale = sum->frequency;
        TEST_ASSERT( lsmash_set_media_parameters( mux_root, tid, &media_param ) == 0, "set media params" );

        TEST_ASSERT( lsmash_add_sample_entry( mux_root, tid, sum ) == 1, "add fLaC sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)sum );

        uint32_t imported_frames = 0;
        lsmash_sample_t *sample = NULL;
        while( lsmash_importer_get_access_unit( imp, 1, &sample ) == 0 && sample )
        {
            sample->index = 1;
            TEST_ASSERT( lsmash_append_sample( mux_root, tid, sample ) == 0, "append flac sample" );
            sample = NULL;
            imported_frames++;
        }
        TEST_ASSERT( imported_frames == 4, "flac importer extracted all 4 frames" );
        uint32_t last_delta = lsmash_importer_get_last_delta( imp, 1 );
        TEST_ASSERT( last_delta == 1500, "flac importer last_delta == 1500 (uncommon 16-bit block size)" );
        TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, tid, last_delta ) == 0, "flush flac samples" );
        TEST_ASSERT( lsmash_update_track_duration( mux_root, tid, last_delta ) == 0, "update flac track duration" );
        TEST_ASSERT( lsmash_finish_movie( mux_root, NULL ) == 0, "finish flac mp4" );

        lsmash_close_file( &mux_param );
        lsmash_destroy_root( mux_root );
        lsmash_importer_close( imp );
        lsmash_destroy_root( imp_root );
    }

    /* 5. Demux and dump the FLAC MP4 and verify dfLa boxdumper output, summary recovery, and samples */
    {
        lsmash_root_t *demux_root = lsmash_create_root();
        lsmash_file_parameters_t demux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( mp4_path, 1, &demux_param ) == 0, "open flac mp4 for read" );
        demux_param.mode |= LSMASH_FILE_MODE_DUMP;
        lsmash_file_t *demux_file = lsmash_set_file( demux_root, &demux_param );
        TEST_ASSERT( lsmash_read_file( demux_file, &demux_param ) >= 0, "read flac mp4" );

        TEST_ASSERT( lsmash_print_movie( demux_root, dump_path ) == 0, "print flac movie" );
        FILE *dfp = fopen( dump_path, "rb" );
        TEST_ASSERT( dfp != NULL, "open flac dump" );
        fseek( dfp, 0, SEEK_END );
        long fsz = ftell( dfp );
        fseek( dfp, 0, SEEK_SET );
        char *dump_buf = (char *)lsmash_malloc( (size_t)fsz + 1 );
        TEST_ASSERT( dump_buf != NULL, "alloc flac dump_buf" );
        size_t nread = fread( dump_buf, 1, (size_t)fsz, dfp );
        dump_buf[nread] = '\0';
        fclose( dfp );
        remove( dump_path );
        TEST_ASSERT( nread > 0, "non-empty flac dump" );
        TEST_ASSERT( strstr( dump_buf, "[fLaC: Audio Description]" ) != NULL, "dump contains fLaC Audio Description" );
        TEST_ASSERT( strstr( dump_buf, "[dfLa: FLAC Specific Box]" ) != NULL, "dump contains dfLa FLAC Specific Box" );
        TEST_ASSERT( strstr( dump_buf, "SampleRate = 96000" ) != NULL, "dump contains SampleRate = 96000" );
        TEST_ASSERT( strstr( dump_buf, "VORBIS_COMMENT" ) != NULL, "dump contains VORBIS_COMMENT block" );
        lsmash_free( dump_buf );

        lsmash_audio_summary_t *dsum = (lsmash_audio_summary_t *)lsmash_get_summary( demux_root, 1, 1 );
        TEST_ASSERT( dsum != NULL, "get demuxed flac summary" );
        TEST_ASSERT( lsmash_check_codec_type_identical( dsum->sample_type, ISOM_CODEC_TYPE_FLAC_AUDIO ), "demuxed sample_type is fLaC" );
        TEST_ASSERT( dsum->frequency == 96000 && dsum->channels == 2 && dsum->sample_size == 24 && dsum->samples_in_frame == 4096,
                     "demuxed summary recovers exact 96000 Hz from dfLa STREAMINFO" );
        lsmash_cleanup_summary( (lsmash_summary_t *)dsum );

        TEST_ASSERT( lsmash_construct_timeline( demux_root, 1 ) == 0, "construct flac timeline" );
        TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( demux_root, 1 ) == 4, "4 flac frames in timeline" );
        for( uint32_t i = 1; i <= 4; i++ )
        {
            lsmash_sample_t *s = lsmash_get_sample_from_media_timeline( demux_root, 1, i );
            TEST_ASSERT( s != NULL && s->length == frame_sizes[i - 1], "demuxed flac frame length matches" );
            TEST_ASSERT( s->dts == sample_nums[i - 1], "demuxed flac frame DTS matches" );
            TEST_ASSERT( memcmp( s->data, frames[i - 1], frame_sizes[i - 1] ) == 0, "demuxed flac frame data matches bit-for-bit" );
            lsmash_delete_sample( s );
        }
        lsmash_destruct_timeline( demux_root, 1 );

        lsmash_close_file( &demux_param );
        lsmash_destroy_root( demux_root );
    }

    remove( flac_path );
    remove( mp4_path );

    printf( "[PASS] test_flac_isobmff_roundtrip\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 16: Segmented ISOBMFF (Self-Initializing / Separate Segments),
 *          Progressive Download Abort and sidx Brand Validation
 *---------------------------------------------------------------------------*/
#define SEG_TRACK_TIMESCALE 1000
#define SEG_SAMPLE_DURATION 40
#define SEG_FRAGMENT_COUNT  3
#define SEG_FRAGMENT_SAMPLES 10

static uint32_t seg_sample_size( uint32_t g_idx )
{
    return 16 + (g_idx % 11);
}

static int seg_scan_top_level_boxes( const char *path, uint32_t *types, int max_types )
{
    FILE *fp = fopen( path, "rb" );
    if( !fp )
        return -1;
    int count = 0;
    uint8_t hdr[16];
    while( fread( hdr, 1, 8, fp ) == 8 )
    {
        uint64_t size = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | hdr[3];
        uint32_t type = ((uint32_t)hdr[4] << 24) | ((uint32_t)hdr[5] << 16) | ((uint32_t)hdr[6] << 8) | hdr[7];
        uint64_t header_size = 8;
        if( size == 1 )
        {
            if( fread( hdr + 8, 1, 8, fp ) != 8 )
                break;
            size = 0;
            for( int i = 8; i < 16; i++ )
                size = (size << 8) | hdr[i];
            header_size = 16;
        }
        if( count < max_types )
            types[count] = type;
        count++;
        if( size < header_size )
            break;
        if( fseek( fp, (long)(size - header_size), SEEK_CUR ) != 0 )
            break;
    }
    fclose( fp );
    return count;
}

static int seg_boxes_match( const char *path, const uint32_t *expected, int expected_count )
{
    uint32_t types[16];
    int count = seg_scan_top_level_boxes( path, types, 16 );
    if( count != expected_count )
        return 0;
    for( int i = 0; i < count; i++ )
        if( types[i] != expected[i] )
            return 0;
    return 1;
}

static uint32_t seg_create_video_track( lsmash_root_t *root )
{
    lsmash_movie_parameters_t movie_param;
    lsmash_initialize_movie_parameters( &movie_param );
    if( lsmash_set_movie_parameters( root, &movie_param ) < 0 )
        return 0;
    uint32_t tid = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
    if( !tid )
        return 0;
    lsmash_track_parameters_t track_param;
    lsmash_initialize_track_parameters( &track_param );
    track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
    if( lsmash_set_track_parameters( root, tid, &track_param ) < 0 )
        return 0;
    lsmash_media_parameters_t media_param;
    lsmash_initialize_media_parameters( &media_param );
    media_param.timescale = SEG_TRACK_TIMESCALE;
    if( lsmash_set_media_parameters( root, tid, &media_param ) < 0 )
        return 0;
    lsmash_video_summary_t *summary = create_synth_av01_summary( 64, 64 );
    if( !summary )
        return 0;
    int index = lsmash_add_sample_entry( root, tid, summary );
    lsmash_cleanup_summary( (lsmash_summary_t *)summary );
    return index == 1 ? tid : 0;
}

/* Append SEG_FRAGMENT_SAMPLES samples as one movie fragment, starting from the global sample index 'first'. */
static int seg_write_fragment( lsmash_root_t *root, uint32_t tid, uint32_t first )
{
    if( lsmash_create_fragment_movie( root ) < 0 )
        return -1;
    for( uint32_t i = 0; i < SEG_FRAGMENT_SAMPLES; i++ )
    {
        uint32_t g_idx = first + i;
        lsmash_sample_t *s = lsmash_create_sample( seg_sample_size( g_idx ) );
        if( !s )
            return -1;
        memset( s->data, (int)((g_idx * 7 + 3) & 0xFF), s->length );
        s->dts   = (uint64_t)g_idx * SEG_SAMPLE_DURATION;
        s->cts   = s->dts;
        s->index = 1;
        s->prop.ra_flags = (i == 0) ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC : ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
        if( lsmash_append_sample( root, tid, s ) < 0 )
            return -1;
    }
    return lsmash_flush_pooled_samples( root, tid, SEG_SAMPLE_DURATION );
}

static int seg_abort_callback( void *param, uint64_t done, uint64_t total )
{
    (void)done;
    (void)total;
    int *calls = (int *)param;
    ++*calls;
    return 1;
}

static int test_segmented_isobmff_and_abort_callback( void )
{
    const uint32_t T_FTYP = LSMASH_4CC( 'f', 't', 'y', 'p' );
    const uint32_t T_STYP = LSMASH_4CC( 's', 't', 'y', 'p' );
    const uint32_t T_SIDX = LSMASH_4CC( 's', 'i', 'd', 'x' );
    const uint32_t T_MOOV = LSMASH_4CC( 'm', 'o', 'o', 'v' );
    const uint32_t T_MOOF = LSMASH_4CC( 'm', 'o', 'o', 'f' );
    const uint32_t T_MDAT = LSMASH_4CC( 'm', 'd', 'a', 't' );
    const lsmash_brand_type dash_brands[2] = { ISOM_BRAND_TYPE_ISO6, ISOM_BRAND_TYPE_DASH };

    /* 16a. Self-Initializing Media Segment: sidx must be generated and all samples must be restored. */
    {
        const char *path = "tests/tmp_test_self_init_segment.mp4";
        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( path, 0, &param ) == 0, "16a: open self-initializing segment" );
        param.mode |= LSMASH_FILE_MODE_FRAGMENTED | LSMASH_FILE_MODE_INDEX | LSMASH_FILE_MODE_SEGMENT;
        param.major_brand = ISOM_BRAND_TYPE_ISO6;
        param.brands      = (lsmash_brand_type *)dash_brands;
        param.brand_count = 2;
        TEST_ASSERT( lsmash_set_file( root, &param ) != NULL, "16a: set file with iso6/dash and INDEX" );
        uint32_t tid = seg_create_video_track( root );
        TEST_ASSERT( tid != 0, "16a: create track" );
        for( uint32_t f = 0; f < SEG_FRAGMENT_COUNT; f++ )
            TEST_ASSERT( seg_write_fragment( root, tid, f * SEG_FRAGMENT_SAMPLES ) == 0, "16a: write fragment" );
        lsmash_adhoc_remux_t remux = { .buffer_size = 65536 };
        TEST_ASSERT( lsmash_finish_movie( root, &remux ) == 0, "16a: finish self-initializing segment" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );

        uint32_t types[16];
        int count = seg_scan_top_level_boxes( path, types, 16 );
        int sidx_pos = -1, first_moof = -1, has_ftyp = 0, has_moov = 0;
        for( int i = 0; i < count && i < 16; i++ )
        {
            has_ftyp |= (types[i] == T_FTYP);
            has_moov |= (types[i] == T_MOOV);
            if( types[i] == T_SIDX && sidx_pos < 0 )
                sidx_pos = i;
            if( types[i] == T_MOOF && first_moof < 0 )
                first_moof = i;
        }
        TEST_ASSERT( has_ftyp && has_moov, "16a: ftyp and moov are present" );
        TEST_ASSERT( sidx_pos >= 0 && first_moof > sidx_pos, "16a: sidx precedes the first moof" );

        lsmash_root_t *demux_root = lsmash_create_root();
        lsmash_file_parameters_t demux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( path, 1, &demux_param ) == 0, "16a: open for demux" );
        lsmash_file_t *demux_file = lsmash_set_file( demux_root, &demux_param );
        TEST_ASSERT( lsmash_read_file( demux_file, &demux_param ) >= 0, "16a: read self-initializing segment" );
        TEST_ASSERT( demux_file->sidx_list.head != NULL, "16a: sidx parsed" );
        isom_sidx_t *sidx = (isom_sidx_t *)demux_file->sidx_list.head->data;
        TEST_ASSERT( lsm_list_count( sidx->list ) == SEG_FRAGMENT_COUNT, "16a: one sidx entry per fragment" );
        TEST_ASSERT( lsmash_construct_timeline( demux_root, tid ) == 0, "16a: construct timeline" );
        TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( demux_root, tid ) == SEG_FRAGMENT_COUNT * SEG_FRAGMENT_SAMPLES,
                     "16a: all samples restored" );
        for( uint32_t g_idx = 0; g_idx < SEG_FRAGMENT_COUNT * SEG_FRAGMENT_SAMPLES; g_idx++ )
        {
            uint64_t dts = 0;
            lsmash_sample_t *s = lsmash_get_sample_from_media_timeline( demux_root, tid, g_idx + 1 );
            TEST_ASSERT( s != NULL, "16a: get sample" );
            TEST_ASSERT( lsmash_get_dts_from_media_timeline( demux_root, tid, g_idx + 1, &dts ) == 0, "16a: get dts" );
            int data_ok = s->length == seg_sample_size( g_idx );
            for( uint32_t b = 0; data_ok && b < s->length; b++ )
                data_ok = s->data[b] == (uint8_t)((g_idx * 7 + 3) & 0xFF);
            TEST_ASSERT( data_ok && dts == (uint64_t)g_idx * SEG_SAMPLE_DURATION, "16a: sample payload and dts match" );
            lsmash_delete_sample( s );
        }
        lsmash_destruct_timeline( demux_root, tid );
        lsmash_close_file( &demux_param );
        lsmash_destroy_root( demux_root );
        remove( path );
    }

    /* 16b. Initialization Segment + Media Segments switched by lsmash_switch_media_segment(). */
    {
        const char *init_path = "tests/tmp_test_seg_init.mp4";
        const char *seg_path[2] = { "tests/tmp_test_seg_1.m4s", "tests/tmp_test_seg_2.m4s" };
        const lsmash_brand_type seg_brands[3] = { ISOM_BRAND_TYPE_MSDH, ISOM_BRAND_TYPE_MSIX, ISOM_BRAND_TYPE_ISO6 };
        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t init_param = { 0 };
        TEST_ASSERT( lsmash_open_file( init_path, 0, &init_param ) == 0, "16b: open init segment" );
        init_param.mode &= ~LSMASH_FILE_MODE_MEDIA;
        init_param.mode |= LSMASH_FILE_MODE_FRAGMENTED | LSMASH_FILE_MODE_SEGMENT;
        init_param.major_brand = ISOM_BRAND_TYPE_ISO6;
        init_param.brands      = (lsmash_brand_type *)dash_brands;
        init_param.brand_count = 2;
        TEST_ASSERT( lsmash_set_file( root, &init_param ) != NULL, "16b: set init segment" );
        uint32_t tid = seg_create_video_track( root );
        TEST_ASSERT( tid != 0, "16b: create track in init segment" );

        lsmash_file_parameters_t seg_param[2] = { { 0 }, { 0 } };
        lsmash_file_t *seg_file[2];
        lsmash_adhoc_remux_t remux = { .buffer_size = 65536 };
        for( int s = 0; s < 2; s++ )
        {
            TEST_ASSERT( lsmash_open_file( seg_path[s], 0, &seg_param[s] ) == 0, "16b: open media segment" );
            seg_param[s].mode &= ~LSMASH_FILE_MODE_INITIALIZATION;
            seg_param[s].mode |= LSMASH_FILE_MODE_FRAGMENTED | LSMASH_FILE_MODE_INDEX | LSMASH_FILE_MODE_SEGMENT;
            seg_param[s].major_brand = ISOM_BRAND_TYPE_MSDH;
            seg_param[s].brands      = (lsmash_brand_type *)seg_brands;
            seg_param[s].brand_count = 3;
            seg_file[s] = lsmash_set_file( root, &seg_param[s] );
            TEST_ASSERT( seg_file[s] != NULL, "16b: set media segment" );
            TEST_ASSERT( lsmash_switch_media_segment( root, seg_file[s], &remux ) == 0, "16b: switch media segment" );
            TEST_ASSERT( seg_write_fragment( root, tid, (uint32_t)s * SEG_FRAGMENT_SAMPLES ) == 0, "16b: write fragment into segment" );
        }
        TEST_ASSERT( lsmash_finish_movie( root, &remux ) == 0, "16b: finish last media segment" );
        lsmash_close_file( &init_param );
        lsmash_close_file( &seg_param[0] );
        lsmash_close_file( &seg_param[1] );
        lsmash_destroy_root( root );

        const uint32_t init_expect[2] = { T_FTYP, T_MOOV };
        const uint32_t seg_expect[4]  = { T_STYP, T_SIDX, T_MOOF, T_MDAT };
        TEST_ASSERT( seg_boxes_match( init_path, init_expect, 2 ), "16b: init segment is ftyp+moov" );
        TEST_ASSERT( seg_boxes_match( seg_path[0], seg_expect, 4 ), "16b: media segment 1 is styp+sidx+moof+mdat" );
        TEST_ASSERT( seg_boxes_match( seg_path[1], seg_expect, 4 ), "16b: media segment 2 is styp+sidx+moof+mdat" );
        remove( init_path );
        remove( seg_path[0] );
        remove( seg_path[1] );
    }

    /* 16c. A non-zero return value of the remux callback aborts the operation safely. */
    for( int mode = 0; mode <= 1; mode++ )
    {
        const char *path = "tests/tmp_test_remux_abort.mp4";
        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( path, 0, &param ) == 0, "16c: open output" );
        if( mode == 0 )
        {
            /* Segment Index insertion path. */
            param.mode |= LSMASH_FILE_MODE_FRAGMENTED | LSMASH_FILE_MODE_INDEX | LSMASH_FILE_MODE_SEGMENT;
            param.major_brand = ISOM_BRAND_TYPE_ISO6;
            param.brands      = (lsmash_brand_type *)dash_brands;
            param.brand_count = 2;
        }
        TEST_ASSERT( lsmash_set_file( root, &param ) != NULL, "16c: set file" );
        uint32_t tid = seg_create_video_track( root );
        TEST_ASSERT( tid != 0, "16c: create track" );
        if( mode == 0 )
            for( uint32_t f = 0; f < SEG_FRAGMENT_COUNT; f++ )
                TEST_ASSERT( seg_write_fragment( root, tid, f * SEG_FRAGMENT_SAMPLES ) == 0, "16c: write fragment" );
        else
        {
            /* moov relocation path (FastStart). */
            for( uint32_t g_idx = 0; g_idx < SEG_FRAGMENT_COUNT * SEG_FRAGMENT_SAMPLES; g_idx++ )
            {
                lsmash_sample_t *s = lsmash_create_sample( seg_sample_size( g_idx ) );
                TEST_ASSERT( s != NULL, "16c: create sample" );
                memset( s->data, (int)(g_idx & 0xFF), s->length );
                s->dts   = (uint64_t)g_idx * SEG_SAMPLE_DURATION;
                s->cts   = s->dts;
                s->index = 1;
                s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
                TEST_ASSERT( lsmash_append_sample( root, tid, s ) == 0, "16c: append sample" );
            }
            TEST_ASSERT( lsmash_flush_pooled_samples( root, tid, SEG_SAMPLE_DURATION ) == 0, "16c: flush samples" );
        }
        int calls = 0;
        lsmash_adhoc_remux_t remux = { .buffer_size = 64, .func = seg_abort_callback, .param = &calls };
        TEST_ASSERT( lsmash_finish_movie( root, &remux ) < 0, "16c: finish is aborted by the callback" );
        TEST_ASSERT( calls == 1, "16c: callback is not called again after aborting" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
        remove( path );
    }

    /* 16d. LSMASH_FILE_MODE_INDEX must be rejected when the brands cannot allow sidx. */
    {
        const lsmash_brand_type old_brands[2] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_ISO2 };
        const char *path = "tests/tmp_test_index_brand.mp4";
        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( path, 0, &param ) == 0, "16d: open output" );
        param.mode |= LSMASH_FILE_MODE_FRAGMENTED | LSMASH_FILE_MODE_INDEX | LSMASH_FILE_MODE_SEGMENT;
        param.major_brand = ISOM_BRAND_TYPE_ISOM;
        param.brands      = (lsmash_brand_type *)old_brands;
        param.brand_count = 2;
        TEST_ASSERT( lsmash_set_file( root, &param ) == NULL, "16d: INDEX with isom/iso2 is rejected" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );

        root = lsmash_create_root();
        memset( &param, 0, sizeof(param) );
        TEST_ASSERT( lsmash_open_file( path, 0, &param ) == 0, "16d: open output again" );
        param.mode |= LSMASH_FILE_MODE_FRAGMENTED | LSMASH_FILE_MODE_INDEX | LSMASH_FILE_MODE_SEGMENT;
        param.major_brand = ISOM_BRAND_TYPE_ISOM;
        TEST_ASSERT( lsmash_set_file( root, &param ) == NULL, "16d: INDEX with the isom brand only is rejected" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
        remove( path );
    }

    printf( "[PASS] test_segmented_isobmff_and_abort_callback\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 17: External data reference ('dref' with a non-self-contained 'url ')
 *---------------------------------------------------------------------------*/
#define DREF_SAMPLE_COUNT    12
#define DREF_SAMPLE_DURATION 40

static uint32_t dref_sample_size( uint32_t idx )
{
    return 24 + (idx % 5) * 11;
}

static int test_external_data_reference_roundtrip( void )
{
    const char *movie_path = "tests/tmp_test_dref_movie.mp4";
    const char *media_name = "tmp_test_dref_media.bin";
    const char *media_path = "tests/tmp_test_dref_media.bin";

    /* 17a. Write: the movie boxes stay in the movie file while the sample data goes into a separate media file. */
    uint32_t tid = 0;
    {
        lsmash_root_t *root = lsmash_create_root();
        TEST_ASSERT( root != NULL, "17a: create root" );
        lsmash_file_parameters_t movie_param = { 0 };
        TEST_ASSERT( lsmash_open_file( movie_path, 0, &movie_param ) == 0, "17a: open movie file" );
        lsmash_file_t *movie_file = lsmash_set_file( root, &movie_param );
        TEST_ASSERT( movie_file != NULL, "17a: set movie file" );
        /* The referenced file is a raw media data container, i.e. it has neither box structure nor movie sample tables. */
        lsmash_file_parameters_t media_param = { 0 };
        TEST_ASSERT( lsmash_open_file( media_path, 0, &media_param ) == 0, "17a: open external media file" );
        media_param.mode = LSMASH_FILE_MODE_WRITE | LSMASH_FILE_MODE_MEDIA;
        lsmash_file_t *media_file = lsmash_set_file( root, &media_param );
        TEST_ASSERT( media_file != NULL, "17a: set external media file" );

        lsmash_movie_parameters_t movie_pm;
        lsmash_initialize_movie_parameters( &movie_pm );
        TEST_ASSERT( lsmash_set_movie_parameters( root, &movie_pm ) == 0, "17a: set movie parameters" );
        tid = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        TEST_ASSERT( tid != 0, "17a: create track" );
        lsmash_track_parameters_t track_pm;
        lsmash_initialize_track_parameters( &track_pm );
        track_pm.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        TEST_ASSERT( lsmash_set_track_parameters( root, tid, &track_pm ) == 0, "17a: set track parameters" );
        lsmash_media_parameters_t media_pm;
        lsmash_initialize_media_parameters( &media_pm );
        media_pm.timescale = 1000;
        TEST_ASSERT( lsmash_set_media_parameters( root, tid, &media_pm ) == 0, "17a: set media parameters" );

        /* Index 1: self-contained, index 2: external file. */
        lsmash_data_reference_t self_ref = { .index = 1, .location = NULL };
        TEST_ASSERT( lsmash_create_data_reference( root, tid, &self_ref, movie_file ) == 0, "17a: create self-contained data reference" );
        TEST_ASSERT( self_ref.index == 1, "17a: self-contained data reference is index 1" );
        lsmash_data_reference_t ext_ref = { .index = 0, .location = (char *)media_name };
        TEST_ASSERT( lsmash_create_data_reference( root, tid, &ext_ref, media_file ) == 0, "17a: create external data reference" );
        TEST_ASSERT( ext_ref.index == 2, "17a: external data reference is index 2" );
        TEST_ASSERT( lsmash_count_data_reference( root, tid ) == 2, "17a: two data references in the track" );

        lsmash_video_summary_t *summary = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( summary != NULL, "17a: create summary" );
        summary->data_ref_index = ext_ref.index;
        int entry = lsmash_add_sample_entry( root, tid, summary );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        TEST_ASSERT( entry == 1, "17a: add sample entry referencing the external file" );

        for( uint32_t i = 0; i < DREF_SAMPLE_COUNT; i++ )
        {
            lsmash_sample_t *s = lsmash_create_sample( dref_sample_size( i ) );
            TEST_ASSERT( s != NULL, "17a: create sample" );
            memset( s->data, (int)((i * 13 + 5) & 0xFF), s->length );
            s->dts   = (uint64_t)i * DREF_SAMPLE_DURATION;
            s->cts   = s->dts;
            s->index = 1;
            s->prop.ra_flags = (i == 0) ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC : ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
            TEST_ASSERT( lsmash_append_sample( root, tid, s ) == 0, "17a: append sample" );
        }
        TEST_ASSERT( lsmash_flush_pooled_samples( root, tid, DREF_SAMPLE_DURATION ) == 0, "17a: flush samples" );
        lsmash_adhoc_remux_t remux = { .buffer_size = 4096 };
        TEST_ASSERT( lsmash_finish_movie( root, &remux ) == 0, "17a: finish movie" );
        lsmash_close_file( &media_param );
        lsmash_close_file( &movie_param );
        lsmash_destroy_root( root );

        /* The sample data must be in the external file only. */
        uint32_t total = 0;
        for( uint32_t i = 0; i < DREF_SAMPLE_COUNT; i++ )
            total += dref_sample_size( i );
        FILE *fp = fopen( media_path, "rb" );
        TEST_ASSERT( fp != NULL, "17a: external media file exists" );
        fseek( fp, 0, SEEK_END );
        long media_size = ftell( fp );
        fclose( fp );
        TEST_ASSERT( media_size == (long)total, "17a: external media file holds exactly the sample payloads" );
        /* The first sample is a run of 24 bytes filled with 0x05. It must not appear anywhere in the movie file. */
        fp = fopen( movie_path, "rb" );
        TEST_ASSERT( fp != NULL, "17a: movie file exists" );
        int run = 0, found = 0, c;
        while( !found && (c = fgetc( fp )) != EOF )
        {
            run = (c == 0x05) ? run + 1 : 0;
            found = run >= (int)dref_sample_size( 0 );
        }
        fclose( fp );
        TEST_ASSERT( !found, "17a: the movie file does not contain the sample payloads" );
    }

    /* 17b. Read: the data reference location is recovered and the Timeline restores samples from the external file. */
    {
        lsmash_root_t *root = lsmash_create_root();
        TEST_ASSERT( root != NULL, "17b: create root" );
        lsmash_file_parameters_t movie_param = { 0 };
        TEST_ASSERT( lsmash_open_file( movie_path, 1, &movie_param ) == 0, "17b: open movie file" );
        lsmash_file_t *movie_file = lsmash_set_file( root, &movie_param );
        TEST_ASSERT( movie_file != NULL, "17b: set movie file" );
        TEST_ASSERT( lsmash_read_file( movie_file, &movie_param ) >= 0, "17b: read movie file" );
        TEST_ASSERT( lsmash_count_data_reference( root, tid ) == 2, "17b: two data references recovered" );

        lsmash_data_reference_t ref = { .index = 1, .location = NULL };
        TEST_ASSERT( lsmash_get_data_reference( root, tid, &ref ) == 0, "17b: get self-contained data reference" );
        TEST_ASSERT( ref.location == NULL, "17b: self-contained data reference has no location" );
        lsmash_cleanup_data_reference( &ref );
        ref.index = 2;
        TEST_ASSERT( lsmash_get_data_reference( root, tid, &ref ) == 0, "17b: get external data reference" );
        TEST_ASSERT( ref.location != NULL && strcmp( ref.location, media_name ) == 0, "17b: external location is recovered" );
        lsmash_cleanup_data_reference( &ref );

        /* Without the referenced file assigned, the sample data cannot be restored. */
        TEST_ASSERT( lsmash_assign_data_reference( root, tid, 99, movie_file ) < 0, "17b: out-of-range data reference index is rejected" );

        lsmash_file_parameters_t media_param = { 0 };
        TEST_ASSERT( lsmash_open_file( media_path, 1, &media_param ) == 0, "17b: open external media file" );
        media_param.mode |= LSMASH_FILE_MODE_MEDIA;
        lsmash_file_t *media_file = lsmash_set_file( root, &media_param );
        TEST_ASSERT( media_file != NULL, "17b: set external media file" );
        TEST_ASSERT( lsmash_assign_data_reference( root, tid, 2, media_file ) == 0, "17b: assign external media file" );

        TEST_ASSERT( lsmash_construct_timeline( root, tid ) == 0, "17b: construct timeline" );
        TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( root, tid ) == DREF_SAMPLE_COUNT, "17b: all samples are in the timeline" );
        for( uint32_t i = 0; i < DREF_SAMPLE_COUNT; i++ )
        {
            uint64_t dts = 0;
            lsmash_sample_t *s = lsmash_get_sample_from_media_timeline( root, tid, i + 1 );
            TEST_ASSERT( s != NULL, "17b: get sample from the external file" );
            TEST_ASSERT( lsmash_get_dts_from_media_timeline( root, tid, i + 1, &dts ) == 0, "17b: get dts" );
            int data_ok = s->length == dref_sample_size( i );
            for( uint32_t b = 0; data_ok && b < s->length; b++ )
                data_ok = s->data[b] == (uint8_t)((i * 13 + 5) & 0xFF);
            TEST_ASSERT( data_ok && dts == (uint64_t)i * DREF_SAMPLE_DURATION, "17b: sample payload and dts match" );
            lsmash_delete_sample( s );
        }
        lsmash_destruct_timeline( root, tid );
        lsmash_close_file( &media_param );
        lsmash_close_file( &movie_param );
        lsmash_destroy_root( root );
    }

    /* 17c. External data references are not supported for movie fragments: reject them at creation time
     *      instead of producing a file whose 'dref' disagrees with the actual sample location. */
    {
        const char *frag_path = "tests/tmp_test_dref_frag_movie.mp4";
        lsmash_root_t *root = lsmash_create_root();
        TEST_ASSERT( root != NULL, "17c: create root" );
        lsmash_file_parameters_t movie_param = { 0 };
        TEST_ASSERT( lsmash_open_file( frag_path, 0, &movie_param ) == 0, "17c: open fragmented movie file" );
        movie_param.mode |= LSMASH_FILE_MODE_FRAGMENTED;
        lsmash_file_t *movie_file = lsmash_set_file( root, &movie_param );
        TEST_ASSERT( movie_file != NULL, "17c: set fragmented movie file" );
        lsmash_file_parameters_t media_param = { 0 };
        TEST_ASSERT( lsmash_open_file( media_path, 0, &media_param ) == 0, "17c: open external media file" );
        media_param.mode = LSMASH_FILE_MODE_WRITE | LSMASH_FILE_MODE_MEDIA;
        lsmash_file_t *media_file = lsmash_set_file( root, &media_param );
        TEST_ASSERT( media_file != NULL, "17c: set external media file" );
        lsmash_movie_parameters_t movie_pm;
        lsmash_initialize_movie_parameters( &movie_pm );
        TEST_ASSERT( lsmash_set_movie_parameters( root, &movie_pm ) == 0, "17c: set movie parameters" );
        uint32_t frag_tid = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        TEST_ASSERT( frag_tid != 0, "17c: create track" );
        lsmash_data_reference_t ext_ref = { .index = 0, .location = (char *)media_name };
        TEST_ASSERT( lsmash_create_data_reference( root, frag_tid, &ext_ref, media_file ) < 0,
                     "17c: external data reference in a fragmented movie is rejected" );
        TEST_ASSERT( lsmash_count_data_reference( root, frag_tid ) == 0, "17c: no data reference is left behind" );
        lsmash_data_reference_t self_ref = { .index = 0, .location = NULL };
        TEST_ASSERT( lsmash_create_data_reference( root, frag_tid, &self_ref, movie_file ) == 0,
                     "17c: self-contained data reference in a fragmented movie is accepted" );
        lsmash_close_file( &media_param );
        lsmash_close_file( &movie_param );
        lsmash_destroy_root( root );
        remove( frag_path );
    }
    remove( movie_path );
    remove( media_path );
    printf( "[PASS] test_external_data_reference_roundtrip\n" );
    return 0;
}
/*---------------------------------------------------------------------------
 * Test 18: Fake Box / Timeline flow of a raw stream through lsmash_read_file()
 *---------------------------------------------------------------------------*/
static int write_test_file( const char *path, const uint8_t *data, size_t size )
{
    FILE *fp = fopen( path, "wb" );
    if( !fp )
        return -1;
    size_t written = fwrite( data, 1, size, fp );
    fclose( fp );
    return written == size ? 0 : -1;
}

static int test_read_file_fake_box_and_unsupported_stream( void )
{
    const char *wav_path = "tests/tmp_test_fake_box.wav";
    const char *aac_path = "tests/tmp_test_unsupported.aac";

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

    /* Raw ADTS: a seekable raw stream without fake box support must not yield a half-built success. */
    uint8_t aac[16 * 5] = { 0 };
    for( int f = 0; f < 5; f++ )
    {
        static const uint8_t adts[7] = { 0xFF, 0xF1, 0x50, 0x80, 0x02, 0x1F, 0xFC };
        memcpy( aac + 16 * f, adts, sizeof(adts) );
    }
    TEST_ASSERT( write_test_file( aac_path, aac, sizeof(aac) ) == 0, "18b: write aac" );
    root = lsmash_create_root();
    memset( &param, 0, sizeof(param) );
    TEST_ASSERT( lsmash_open_file( aac_path, 1, &param ) == 0, "18b: open aac" );
    file = lsmash_set_file( root, &param );
    TEST_ASSERT( file != NULL, "18b: set aac file" );
    TEST_ASSERT( lsmash_read_file( file, &param ) < 0, "18b: raw stream without fake movie is rejected" );
    lsmash_close_file( &param );
    lsmash_destroy_root( root );
    remove( aac_path );

    printf( "[PASS] test_read_file_fake_box_and_unsupported_stream\n" );
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

static int test_read_file_flac_ivf_timeline( void )
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
    RUN_TEST( test_read_file_fake_box_and_unsupported_stream );
    RUN_TEST( test_read_file_flac_ivf_timeline );
#ifdef LSMASH_DEBUG_ALLOC
    assert( lsm_alloc_verify_no_leaks() == 0 );
    assert( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0 );
#endif
    printf( "=== Assertions run: %d, Failures: %d ===\n", g_tests_run, g_tests_failed );
    return g_tests_failed == 0 ? 0 : 1;
}
