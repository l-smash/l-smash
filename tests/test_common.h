/*****************************************************************************
 * test_common.h
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
uint8_t lsm_flac_crc8( const uint8_t *data, uint32_t len );

extern int g_tests_run;
extern int g_tests_failed;

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

typedef struct
{
    uint8_t *buf;
    uint32_t pos;   /* in bits */
} test_bit_writer_t;

static inline void put_le16( uint8_t *p, uint16_t v )
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void put_le32( uint8_t *p, uint32_t v )
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static inline void put_le64( uint8_t *p, uint64_t v )
{
    put_le32( p, (uint32_t)(v & 0xFFFFFFFFULL) );
    put_le32( p + 4, (uint32_t)(v >> 32) );
}

static inline void put_be16( uint8_t *p, uint16_t v )
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static inline void put_be32( uint8_t *p, uint32_t v )
{
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >>  8) & 0xFF);
    p[3] = (uint8_t)( v        & 0xFF);
}

static inline int write_test_file( const char *path, const uint8_t *data, size_t size )
{
    FILE *fp = fopen( path, "wb" );
    if( !fp )
        return -1;
    size_t written = fwrite( data, 1, size, fp );
    fclose( fp );
    return written == size ? 0 : -1;
}

static inline void bw_put( test_bit_writer_t *bw, uint32_t bits, uint32_t value )
{
    for( int i = (int)bits - 1; i >= 0; i-- )
    {
        if( (value >> i) & 1 )
            bw->buf[bw->pos >> 3] |= (uint8_t)(0x80 >> (bw->pos & 7));
        ++bw->pos;
    }
}

static inline void bw_put_ue( test_bit_writer_t *bw, uint32_t value )
{
    uint32_t v    = value + 1;
    uint32_t bits = 0;
    while( (v >> (bits + 1)) != 0 )
        ++bits;
    bw_put( bw, bits, 0 );
    bw_put( bw, bits + 1, v );
}

static inline void bw_put_trailing( test_bit_writer_t *bw )
{
    bw_put( bw, 1, 1 );
    while( bw->pos & 7 )
        bw_put( bw, 1, 0 );
}

static inline void fill_pattern( uint8_t *dst, uint32_t size, uint8_t seed )
{
    for( uint32_t i = 0; i < size; i++ )
        dst[i] = (uint8_t)(seed + i * 7 + (i >> 3));
}

static inline void append_bytes( uint8_t *dst, uint32_t *pos, const uint8_t *src, uint32_t size )
{
    memcpy( dst + *pos, src, size );
    *pos += size;
}

/* Shared test helper functions implemented in test_main.c */
uint32_t build_obu
(
    uint8_t       *dst,
    uint8_t        obu_type,
    int            has_ext,
    uint8_t        temporal_id,
    uint8_t        spatial_id,
    const uint8_t *payload,
    uint32_t       payload_size
);
void put_uvlc( lsmash_bits_t *bits, uint32_t val );
void put_trailing_bits( lsmash_bits_t *bits );
uint32_t build_seq_hdr_obu( uint8_t *dst, const synth_seq_cfg_t *cfg );
uint32_t build_frame_obu
(
    uint8_t *dst,
    uint8_t  obu_type,
    int      enable_order_hint,
    uint8_t  order_hint_bits,
    int      show_existing_frame,
    uint8_t  frame_to_show_map_idx,
    uint8_t  frame_type,
    int      show_frame,
    int      showable_frame,
    uint8_t  refresh_frame_flags,
    uint32_t order_hint
);
lsmash_video_summary_t *create_synth_av01_summary( uint32_t width, uint32_t height );
uint32_t build_synth_flac_frame
(
    uint8_t *dst,
    int      variable_block_strategy,
    uint32_t block_size,
    uint64_t coded_number,
    uint32_t payload_bytes
);

/* Subsystem test suite prototypes */
int test_bitstream_primitives( void );
int test_sequence_header_and_av1c( void );
int test_frame_headers_and_tu_assembly( void );
int test_ivf_mux_demux_roundtrip( void );
int test_memory_audit_allocator( void );
int test_lpcm_pcmc_chnl_srat_roundtrip( void );
int test_multitrack_fragmented_lpcm_roundtrip( void );
int test_wave_importer_isobmff_lpcm( void );
int test_av1_multitile_and_sync_sample_compliance( void );
int test_paged_block_array( void );
int test_large_scale_paged_tables_and_roll_dedup( void );
int test_av1_fragmented_isobmff_roundtrip( void );
int test_malformed_boxes_and_sample_tables( void );
int test_multibuf_resize_and_codec_regressions( void );
int test_flac_isobmff_roundtrip( void );
int test_segmented_isobmff_and_abort_callback( void );
int test_external_data_reference_roundtrip( void );
int test_read_file_fake_box_and_raw_stream_acceptance( void );
int test_read_file_flac_ivf_timeline( void );
int test_read_file_audio_raw_streams( void );
int test_read_file_video_raw_streams( void );
