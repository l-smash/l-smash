/*****************************************************************************
 * test_isom_container.c
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
 * Test 13: Malformed Box & Sample Table Negative Tests (Rollback & 0-Leak Audit)
 *---------------------------------------------------------------------------*/
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

int test_malformed_boxes_and_sample_tables( void )
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

int test_segmented_isobmff_and_abort_callback( void )
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

int test_external_data_reference_roundtrip( void )
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
