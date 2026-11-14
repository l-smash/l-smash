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
#include "core/box_default.h"
#include "core/movie.h"

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

    /* 13e. Non-compliant stsz with sample_size > 0 and trailing entry_size table bytes (pos < box->size) */
    {
        uint8_t *patched = (uint8_t *)lsmash_memdup( base_buf, base_len );
        int stsz_off = find_fourcc_offset( patched, base_len, LSMASH_4CC( 's', 't', 's', 'z' ) );
        TEST_ASSERT( stsz_off >= 0, "13e: locate stsz box" );
        /* stsz layout: size(4) + 'stsz'(4) + version/flags(4) + sample_size(4) + sample_count(4) + entry_size[4] */
        put_be32( &patched[stsz_off + 12], 16 ); /* Non-zero sample_size alongside explicit entry table */
        FILE *fp = fopen( bad_mp4, "wb" );
        fwrite( patched, 1, base_len, fp );
        fclose( fp );
        lsmash_free( patched );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13e: open bad_mp4 (stsz sample_size>0 with entries)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) >= 0, "13e: lsmash_read_file accepts stsz with sample_size>0 and entry table" );
        TEST_ASSERT( file->moov && file->moov->trak_list.head && file->moov->trak_list.head->data, "13e: video track present" );
        isom_trak_t *trak = (isom_trak_t *)file->moov->trak_list.head->data;
        isom_stsz_t *stsz = trak->mdia->minf->stbl->stsz;
        TEST_ASSERT( stsz->sample_size == 16 && stsz->list != NULL && lsm_list_count( stsz->list ) == 4,
                     "13e: read_stsz populates stsz->list when trailing entry bytes are present" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 1, "13e: 0 leak after reading stsz with sample_size>0 and entries" );
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

    /* 13j. Truncated elst (version 0 and version 1) with excessive entry_count */
    for( int ver = 0; ver <= 1; ver++ )
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_ISOM );
        lsmash_bs_put_be32( bs, 0 );
        uint32_t entry_sz = ver == 1 ? 20 : 12;
        uint32_t elst_sz  = 16 + entry_sz; /* header(12) + entry_count(4) + 1 entry */
        uint32_t edts_sz  = 8 + elst_sz;
        uint32_t trak_sz  = 8 + edts_sz;
        uint32_t moov_sz  = 8 + trak_sz;
        lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
        lsmash_bs_put_be32( bs, trak_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'a', 'k' ) );
        lsmash_bs_put_be32( bs, edts_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'e', 'd', 't', 's' ) );
        lsmash_bs_put_be32( bs, elst_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'e', 'l', 's', 't' ) );
        lsmash_bs_put_byte( bs, (uint8_t)ver );
        lsmash_bs_put_be24( bs, 0 );
        lsmash_bs_put_be32( bs, 0x10000000U ); /* excessive entry_count */
        if( ver == 1 )
        {
            lsmash_bs_put_be64( bs, 1000 );
            lsmash_bs_put_be64( bs, 0 );
        }
        else
        {
            lsmash_bs_put_be32( bs, 1000 );
            lsmash_bs_put_be32( bs, 0 );
        }
        lsmash_bs_put_be32( bs, 0x00010000U );

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( bad_mp4, raw, raw_len ) == 0, "13j: write bad_mp4 (truncated elst)" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13j: open bad_mp4 (truncated elst)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13j: lsmash_read_file rejects truncated elst with excessive entry_count" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13j: 100% rollback after truncated elst" );
#endif
        remove( bad_mp4 );
    }

    /* 13k. Malformed elst with invalid negative media_time (< -1) */
    for( int ver = 0; ver <= 1; ver++ )
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_ISOM );
        lsmash_bs_put_be32( bs, 0 );
        uint32_t entry_sz = ver == 1 ? 20 : 12;
        uint32_t elst_sz  = 16 + entry_sz;
        uint32_t edts_sz  = 8 + elst_sz;
        uint32_t trak_sz  = 8 + edts_sz;
        uint32_t moov_sz  = 8 + trak_sz;
        lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
        lsmash_bs_put_be32( bs, trak_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'a', 'k' ) );
        lsmash_bs_put_be32( bs, edts_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'e', 'd', 't', 's' ) );
        lsmash_bs_put_be32( bs, elst_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'e', 'l', 's', 't' ) );
        lsmash_bs_put_byte( bs, (uint8_t)ver );
        lsmash_bs_put_be24( bs, 0 );
        lsmash_bs_put_be32( bs, 1 );
        if( ver == 1 )
        {
            lsmash_bs_put_be64( bs, 1000 );
            lsmash_bs_put_be64( bs, (uint64_t)(int64_t)-2 ); /* invalid negative media_time = -2 */
        }
        else
        {
            lsmash_bs_put_be32( bs, 1000 );
            lsmash_bs_put_be32( bs, (uint32_t)(int32_t)-2 ); /* invalid negative media_time = -2 */
        }
        lsmash_bs_put_be32( bs, 0x00010000U );

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( bad_mp4, raw, raw_len ) == 0, "13k: write bad_mp4 (elst media_time=-2)" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13k: open bad_mp4 (elst media_time=-2)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13k: lsmash_read_file rejects elst with media_time < -1" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13k: 100% rollback after elst with media_time < -1" );
#endif
        remove( bad_mp4 );
    }

    /* 13l. Empty elst (entry_count == 0) and dwell edit (media_rate == 0) safety in timeline & track duration */
    {
        lsmash_root_t *mux_root = lsmash_create_root();
        lsmash_file_parameters_t mux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 0, &mux_param ) == 0, "13l: open file for elst edge cases" );
        lsmash_brand_type brands[2] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_MP42 };
        mux_param.major_brand = ISOM_BRAND_TYPE_MP42;
        mux_param.brands      = brands;
        mux_param.brand_count = 2;
        lsmash_file_t *mux_file = lsmash_set_file( mux_root, &mux_param );
        TEST_ASSERT( mux_file != NULL, "13l: set file" );
        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        movie_param.timescale = 600;
        TEST_ASSERT( lsmash_set_movie_parameters( mux_root, &movie_param ) == 0, "13l: set movie params" );
        uint32_t vtid = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        lsmash_track_parameters_t vtp;
        lsmash_initialize_track_parameters( &vtp );
        vtp.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        lsmash_set_track_parameters( mux_root, vtid, &vtp );
        lsmash_media_parameters_t vmp;
        lsmash_initialize_media_parameters( &vmp );
        vmp.timescale = 1000;
        lsmash_set_media_parameters( mux_root, vtid, &vmp );
        lsmash_video_summary_t *vsum = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, vtid, vsum ) == 1, "13l: add sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)vsum );
        for( uint32_t i = 0; i < 4; i++ )
        {
            lsmash_sample_t *s = lsmash_create_sample( 16 );
            memset( s->data, (int)(0x20 + i), 16 );
            s->dts = i * 100;
            s->cts = i * 100;
            s->index = 1;
            s->prop.ra_flags = (i == 0) ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC : ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
            TEST_ASSERT( lsmash_append_sample( mux_root, vtid, s ) == 0, "13l: append sample" );
        }
        TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, vtid, 100 ) == 0, "13l: flush samples" );
        /* Create an empty elst box (0 entries) directly on Track 1 and verify lsmash_update_track_duration falls back to mdhd duration */
        isom_trak_t *trak = (isom_trak_t *)mux_file->moov->trak_list.head->data;
        TEST_ASSERT( !LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_edts( trak ) ), "13l: add edts" );
        TEST_ASSERT( !LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_elst( trak->edts ) ), "13l: add empty elst" );
        TEST_ASSERT( lsmash_update_track_duration( mux_root, vtid, 100 ) == 0, "13l: update_track_duration with empty elst" );
        TEST_ASSERT( trak->tkhd->duration == 240, "13l: empty elst falls back to mdhd duration (400 * 600 / 1000 = 240)" );
        /* Now add a dwell edit (media_rate == 0) and finish movie */
        lsmash_edit_t dwell_edit = { .duration = 120, .start_time = 0, .rate = ISOM_EDIT_MODE_DWELL };
        TEST_ASSERT( lsmash_create_explicit_timeline_map( mux_root, vtid, dwell_edit ) == 0, "13l: add dwell edit (rate=0)" );
        TEST_ASSERT( lsmash_finish_movie( mux_root, NULL ) == 0, "13l: finish movie" );
        lsmash_close_file( &mux_param );
        lsmash_destroy_root( mux_root );

        lsmash_root_t *rd_root = lsmash_create_root();
        lsmash_file_parameters_t rd_param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &rd_param ) == 0, "13l: open dwell edit movie" );
        lsmash_file_t *rd_file = lsmash_set_file( rd_root, &rd_param );
        TEST_ASSERT( lsmash_read_file( rd_file, &rd_param ) >= 0, "13l: read dwell edit movie" );
        TEST_ASSERT( lsmash_count_explicit_timeline_map( rd_root, 1 ) == 1, "13l: count dwell edit" );
        lsmash_edit_t got_edit = { 0 };
        TEST_ASSERT( lsmash_get_explicit_timeline_map( rd_root, 1, 1, &got_edit ) == 0, "13l: get dwell edit" );
        TEST_ASSERT( got_edit.duration == 120 && got_edit.start_time == 0 && got_edit.rate == 0, "13l: dwell edit fields match" );
        TEST_ASSERT( lsmash_construct_timeline( rd_root, 1 ) == 0, "13l: construct timeline with dwell edit (rate=0) succeeds" );
        lsmash_close_file( &rd_param );
        lsmash_destroy_root( rd_root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13l: 0 leak after empty elst and dwell edit verification" );
#endif
        remove( bad_mp4 );
    }

    /* 13m. Malformed tref.chap (empty box, non-4-byte-aligned payload, and forbidden track_ID == 0) */
    for( int case_idx = 0; case_idx < 3; case_idx++ )
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_ISOM );
        lsmash_bs_put_be32( bs, 0 );
        uint32_t chap_sz = (case_idx == 0) ? 8 : (case_idx == 1) ? 10 : 12;
        uint32_t tref_sz = 8 + chap_sz;
        uint32_t trak_sz = 8 + tref_sz;
        uint32_t moov_sz = 8 + trak_sz;
        lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
        lsmash_bs_put_be32( bs, trak_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'a', 'k' ) );
        lsmash_bs_put_be32( bs, tref_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'e', 'f' ) );
        lsmash_bs_put_be32( bs, chap_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'c', 'h', 'a', 'p' ) );
        if( case_idx == 1 )
            lsmash_bs_put_be16( bs, 0x0001 ); /* 2 bytes (non-multiple of 4) */
        else if( case_idx == 2 )
            lsmash_bs_put_be32( bs, 0 );      /* forbidden track_ID == 0 */

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( bad_mp4, raw, raw_len ) == 0, "13m: write bad_mp4 (malformed tref.chap)" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13m: open bad_mp4 (malformed tref.chap)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13m: lsmash_read_file rejects empty, unaligned, or track_ID==0 tref.chap" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13m: 100% rollback after malformed tref.chap" );
#endif
        remove( bad_mp4 );
    }

    /* 13n. tref.chap with non-existent track_ID (999) or self-referential track_ID (1) */
    for( int self_ref = 0; self_ref <= 1; self_ref++ )
    {
        lsmash_root_t *mux_root = lsmash_create_root();
        lsmash_file_parameters_t mux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 0, &mux_param ) == 0, "13n: open file" );
        lsmash_brand_type brands[2] = { ISOM_BRAND_TYPE_MP42, ISOM_BRAND_TYPE_ISOM };
        mux_param.major_brand = ISOM_BRAND_TYPE_MP42;
        mux_param.brands      = brands;
        mux_param.brand_count = 2;
        lsmash_file_t *mux_file = lsmash_set_file( mux_root, &mux_param );
        TEST_ASSERT( mux_file != NULL, "13n: set file" );
        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        TEST_ASSERT( lsmash_set_movie_parameters( mux_root, &movie_param ) == 0, "13n: set movie params" );
        uint32_t vtid = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        lsmash_track_parameters_t vtp;
        lsmash_initialize_track_parameters( &vtp );
        vtp.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        lsmash_set_track_parameters( mux_root, vtid, &vtp );
        lsmash_media_parameters_t vmp;
        lsmash_initialize_media_parameters( &vmp );
        vmp.timescale = 1000;
        lsmash_set_media_parameters( mux_root, vtid, &vmp );
        lsmash_video_summary_t *vsum = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, vtid, vsum ) == 1, "13n: add sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)vsum );
        lsmash_sample_t *s = lsmash_create_sample( 16 );
        memset( s->data, 0x42, 16 );
        s->dts = 0; s->cts = 0; s->index = 1;
        s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
        TEST_ASSERT( lsmash_append_sample( mux_root, vtid, s ) == 0, "13n: append sample" );
        TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, vtid, 100 ) == 0, "13n: flush sample" );

        /* Attach tref.chap with non-existent track_ID=999 or self-referential track_ID=1 */
        isom_trak_t *trak = (isom_trak_t *)mux_file->moov->trak_list.head->data;
        TEST_ASSERT( !LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_tref( trak ) ), "13n: add tref" );
        isom_tref_type_t *chap = lsm_box_add_track_reference_type( trak->tref, QT_TREF_TYPE_CHAP );
        TEST_ASSERT( !LSMASH_IS_NON_EXISTING_BOX( chap ), "13n: add chap reference" );
        chap->ref_count = 1;
        chap->track_ID  = (uint32_t *)lsmash_malloc( sizeof(uint32_t) );
        TEST_ASSERT( chap->track_ID != NULL, "13n: alloc track_ID" );
        chap->track_ID[0] = self_ref ? 1 : 999;

        TEST_ASSERT( lsmash_finish_movie( mux_root, NULL ) == 0, "13n: finish movie" );
        lsmash_close_file( &mux_param );
        lsmash_destroy_root( mux_root );

        lsmash_root_t *rd_root = lsmash_create_root();
        lsmash_file_parameters_t rd_param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &rd_param ) == 0, "13n: open movie with dangling/self tref.chap" );
        rd_param.mode |= LSMASH_FILE_MODE_DUMP;
        lsmash_file_t *rd_file = lsmash_set_file( rd_root, &rd_param );
        TEST_ASSERT( lsmash_read_file( rd_file, &rd_param ) >= 0, "13n: read movie with dangling/self tref.chap" );
        TEST_ASSERT( lsmash_construct_timeline( rd_root, 1 ) == 0, "13n: construct timeline safely ignores invalid tref.chap target" );
        lsmash_destruct_timeline( rd_root, 1 );
        lsmash_close_file( &rd_param );
        lsmash_destroy_root( rd_root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13n: 0 leak after dangling/self-referential tref.chap" );
#endif
        remove( bad_mp4 );
    }

    /* 13o. Corrupted moov.udta.chpl (truncated entry_count & chapter_name_length exceeding box boundary) */
    for( int case_idx = 0; case_idx < 2; case_idx++ )
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_ISOM );
        lsmash_bs_put_be32( bs, 0 );
        /* chpl v1: header(12) + unknown(1) + entry_count(4) + 1 entry: start_time(8) + len(1) + "abc"(3) = 29 bytes */
        uint32_t chpl_sz = 29;
        uint32_t udta_sz = 8 + chpl_sz;
        uint32_t moov_sz = 8 + udta_sz;
        lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
        lsmash_bs_put_be32( bs, udta_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'u', 'd', 't', 'a' ) );
        lsmash_bs_put_be32( bs, chpl_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'c', 'h', 'p', 'l' ) );
        lsmash_bs_put_byte( bs, 1 ); /* version = 1 */
        lsmash_bs_put_be24( bs, 0 );
        lsmash_bs_put_byte( bs, 0 ); /* unknown */
        lsmash_bs_put_be32( bs, case_idx == 0 ? 50 : 1 ); /* case 0: excessive entry_count=50; case 1: entry_count=1 */
        lsmash_bs_put_be64( bs, 0 );                      /* start_time = 0 */
        lsmash_bs_put_byte( bs, case_idx == 0 ? 3 : 200 ); /* case 1: chapter_name_length=200 > remaining 3 bytes! */
        lsmash_bs_put_bytes( bs, 3, (void *)"abc" );

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( bad_mp4, raw, raw_len ) == 0, "13o: write bad_mp4 (corrupted chpl)" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13o: open bad_mp4 (corrupted chpl)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13o: lsmash_read_file rejects truncated chpl or oversized chapter_name_length" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13o: 100% rollback after corrupted chpl" );
#endif
        remove( bad_mp4 );
    }

    /* 13p. chpl edge cases: 0-length chapter_name, 1-byte 0xEF partial BOM chapter_name, and mvhd timescale == 0 guard */
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_ISOM );
        lsmash_bs_put_be32( bs, 0 );
        /* mvhd v0 (108 bytes) + udta -> chpl v0 with 2 entries:
         * entry 0: start_time=1000, len=0 ("") -> 9 bytes
         * entry 1: start_time=2000, len=1 ("\xEF") -> 10 bytes */
        uint32_t mvhd_sz = 108;
        uint32_t chpl_sz = 12 + 1 + 9 + 10; /* 32 bytes */
        uint32_t udta_sz = 8 + chpl_sz;
        uint32_t moov_sz = 8 + mvhd_sz + udta_sz;
        lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
        lsmash_bs_put_be32( bs, mvhd_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'v', 'h', 'd' ) );
        lsmash_bs_put_be32( bs, 0 );    /* version=0, flags=0 */
        lsmash_bs_put_be32( bs, 0 );    /* creation_time */
        lsmash_bs_put_be32( bs, 0 );    /* modification_time */
        lsmash_bs_put_be32( bs, 1000 ); /* timescale = 1000 */
        lsmash_bs_put_be32( bs, 5000 ); /* duration */
        lsmash_bs_put_be32( bs, 0x00010000U ); /* rate */
        lsmash_bs_put_be16( bs, 0x0100 );      /* volume */
        for( int i = 0; i < 70; i++ )
            lsmash_bs_put_byte( bs, 0 );
        lsmash_bs_put_be32( bs, 1 ); /* next_track_ID */

        lsmash_bs_put_be32( bs, udta_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'u', 'd', 't', 'a' ) );
        lsmash_bs_put_be32( bs, chpl_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'c', 'h', 'p', 'l' ) );
        lsmash_bs_put_be32( bs, 0 ); /* version=0, flags=0 */
        lsmash_bs_put_byte( bs, 2 ); /* entry_count = 2 */
        /* entry 0: 0-length chapter name */
        lsmash_bs_put_be64( bs, 1000 );
        lsmash_bs_put_byte( bs, 0 );
        /* entry 1: 1-byte 0xEF chapter name (< 3 bytes UTF-8 BOM) */
        lsmash_bs_put_be64( bs, 2000 );
        lsmash_bs_put_byte( bs, 1 );
        lsmash_bs_put_byte( bs, 0xEF );

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( bad_mp4, raw, raw_len ) == 0, "13p: write chpl short-name file" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13p: open chpl short-name file" );
        param.mode |= LSMASH_FILE_MODE_DUMP;
        lsmash_file_t *file = lsmash_set_file( root, &param );
        /* Note: moov without tracks returns incompatible at the end of lsm_read_file, but chpl is parsed into file->moov->udta->chpl */
        (void)lsmash_read_file( file, &param );
        file->root->file = file;
        file->initializer = file;
        TEST_ASSERT( lsmash_count_tyrant_chapter( root ) == 2, "13p: count_tyrant_chapter == 2" );
        double ts = -1.0;
        char *chap0 = lsmash_get_tyrant_chapter( root, 1, &ts );
        TEST_ASSERT( chap0 != NULL && strcmp( chap0, "" ) == 0 && ts == 1.0,
                     "13p: 0-length chapter name retrieved safely without BOM memcmp over-read" );
        char *chap1 = lsmash_get_tyrant_chapter( root, 2, &ts );
        TEST_ASSERT( chap1 != NULL && (uint8_t)chap1[0] == 0xEF && chap1[1] == '\0' && ts == 2.0,
                     "13p: 1-byte 0xEF chapter name retrieved safely without BOM memcmp over-read" );
        TEST_ASSERT( lsmash_get_tyrant_chapter( root, 1, NULL ) == NULL, "13p: NULL timestamp rejected safely" );
        /* Verify timescale == 0 guard on chpl v0 */
        file->moov->mvhd->timescale = 0;
        TEST_ASSERT( lsmash_get_tyrant_chapter( root, 1, &ts ) == NULL, "13p: timescale==0 guarded in lsmash_get_tyrant_chapter" );
        TEST_ASSERT( lsmash_print_chapter_list( root ) < 0, "13p: timescale==0 guarded in lsmash_print_chapter_list" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13p: 0 leak after chpl short-name and zero-timescale checks" );
#endif
        remove( bad_mp4 );
    }

    /* 13q. Corrupted meta / ilst / data / mean / name (payload exceeding parent box, header < 16B, and EOF truncation) */
    for( int case_idx = 0; case_idx < 3; case_idx++ )
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_MP42 );
        lsmash_bs_put_be32( bs, 0 );
        if( case_idx == 0 )
        {
            /* data box claiming size 500 inside a 32-byte metaitem box */
            uint32_t item_sz = 32;
            uint32_t ilst_sz = 8 + item_sz;
            uint32_t meta_sz = 12 + ilst_sz;
            uint32_t udta_sz = 8 + meta_sz;
            uint32_t moov_sz = 8 + udta_sz;
            lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
            lsmash_bs_put_be32( bs, udta_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'u', 'd', 't', 'a' ) );
            lsmash_bs_put_be32( bs, meta_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'e', 't', 'a' ) );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, ilst_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'i', 'l', 's', 't' ) );
            lsmash_bs_put_be32( bs, item_sz ); lsmash_bs_put_be32( bs, ITUNES_METADATA_ITEM_TITLE );
            lsmash_bs_put_be32( bs, 500 );     lsmash_bs_put_be32( bs, LSMASH_4CC( 'd', 'a', 't', 'a' ) );
            lsmash_bs_put_be64( bs, 0 );
            lsmash_bs_put_be64( bs, 0 );
        }
        else if( case_idx == 1 )
        {
            /* data box with size 12 (< 16 minimum required for 8-byte box header + 8-byte data header) */
            uint32_t data_sz = 12;
            uint32_t item_sz = 8 + data_sz;
            uint32_t ilst_sz = 8 + item_sz;
            uint32_t meta_sz = 12 + ilst_sz;
            uint32_t udta_sz = 8 + meta_sz;
            uint32_t moov_sz = 8 + udta_sz;
            lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
            lsmash_bs_put_be32( bs, udta_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'u', 'd', 't', 'a' ) );
            lsmash_bs_put_be32( bs, meta_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'e', 't', 'a' ) );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, ilst_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'i', 'l', 's', 't' ) );
            lsmash_bs_put_be32( bs, item_sz ); lsmash_bs_put_be32( bs, ITUNES_METADATA_ITEM_TITLE );
            lsmash_bs_put_be32( bs, data_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'd', 'a', 't', 'a' ) );
            lsmash_bs_put_be32( bs, 0 );
        }
        else
        {
            /* custom '----' metaitem where 'mean' box claims 100 bytes (fitting inside parent sizes) but stream is truncated at EOF after 16 bytes */
            uint32_t mean_sz = 100;
            uint32_t item_sz = 8 + mean_sz;
            uint32_t ilst_sz = 8 + item_sz;
            uint32_t meta_sz = 12 + ilst_sz;
            uint32_t udta_sz = 8 + meta_sz;
            uint32_t moov_sz = 8 + udta_sz;
            lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
            lsmash_bs_put_be32( bs, udta_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'u', 'd', 't', 'a' ) );
            lsmash_bs_put_be32( bs, meta_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'e', 't', 'a' ) );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, ilst_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'i', 'l', 's', 't' ) );
            lsmash_bs_put_be32( bs, item_sz ); lsmash_bs_put_be32( bs, ITUNES_METADATA_ITEM_CUSTOM );
            lsmash_bs_put_be32( bs, mean_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'e', 'a', 'n' ) );
            lsmash_bs_put_be32( bs, 0 ); /* version/flags */
            lsmash_bs_put_be32( bs, 0x636F6D2EU ); /* only 4 payload bytes instead of 88! */
        }

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( bad_mp4, raw, raw_len ) == 0, "13q: write bad_mp4 (corrupted ilst/data/mean)" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13q: open bad_mp4 (corrupted ilst/data/mean)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13q: lsmash_read_file rejects corrupted ilst/data/mean box" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13q: 100% rollback after corrupted ilst/data/mean" );
#endif
        remove( bad_mp4 );
    }

    /* 13r. Corrupted QTFF meta.keys (key_size < 8 underflow, key_size > box boundary, and excessive entry_count) */
    for( int case_idx = 0; case_idx < 3; case_idx++ )
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_QT );
        lsmash_bs_put_be32( bs, 0 );
        /* QTFF meta (not FullBox: next 4 bytes are hdlr size != 0) + keys */
        uint32_t hdlr_sz = 33;
        uint32_t keys_sz = 24; /* 12 (FullBox) + 4 (entry_count) + 8 (key_size + key_namespace) */
        uint32_t meta_sz = 8 + hdlr_sz + keys_sz;
        uint32_t moov_sz = 8 + meta_sz;
        lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
        lsmash_bs_put_be32( bs, meta_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'e', 't', 'a' ) );
        lsmash_bs_put_be32( bs, hdlr_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'h', 'd', 'l', 'r' ) );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'd', 't', 'a' ) );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_byte( bs, 0 );
        lsmash_bs_put_be32( bs, keys_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'k', 'e', 'y', 's' ) );
        lsmash_bs_put_be32( bs, 0 ); /* version/flags */
        lsmash_bs_put_be32( bs, case_idx == 2 ? 1000 : 1 ); /* entry_count */
        lsmash_bs_put_be32( bs, case_idx == 0 ? 4 : 200 );  /* case 0: key_size=4 (<8); case 1: key_size=200 (>box) */
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'd', 't', 'a' ) );

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( bad_mp4, raw, raw_len ) == 0, "13r: write bad_mp4 (corrupted keys)" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13r: open bad_mp4 (corrupted keys)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13r: lsmash_read_file rejects corrupted keys box" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13r: 100% rollback after corrupted keys" );
#endif
        remove( bad_mp4 );
    }

    /* 13s. Truncated ctab box with excessive color_table->size */
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_QT );
        lsmash_bs_put_be32( bs, 0 );
        uint32_t ctab_sz = 24; /* 8 (header) + 4 (seed) + 2 (flags) + 2 (size=255) + 8 (only 1 entry instead of 256!) */
        uint32_t moov_sz = 8 + ctab_sz;
        lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
        lsmash_bs_put_be32( bs, ctab_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'c', 't', 'a', 'b' ) );
        lsmash_bs_put_be32( bs, 0 );   /* color_table_seed */
        lsmash_bs_put_be16( bs, 0 );   /* color_table_flags */
        lsmash_bs_put_be16( bs, 255 ); /* color_table_size = 255 (256 entries = 2048 bytes, but only 8 bytes remain) */
        lsmash_bs_put_be64( bs, 0 );

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( bad_mp4, raw, raw_len ) == 0, "13s: write bad_mp4 (truncated ctab)" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13s: open bad_mp4 (truncated ctab)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13s: lsmash_read_file rejects truncated ctab with excessive color_table_size" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13s: 100% rollback after truncated ctab" );
#endif
        remove( bad_mp4 );
    }

    /* 13t. Truncated hdlr box with forged huge box->size (Heap OOB Write defense) */
    for( int case_idx = 0; case_idx < 2; case_idx++ )
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_ISOM );
        lsmash_bs_put_be32( bs, 0 );
        uint32_t hdlr_sz = 0x00002000;
        uint32_t mdia_sz = 8 + hdlr_sz;
        uint32_t trak_sz = 8 + mdia_sz;
        uint32_t moov_sz = 8 + trak_sz;
        lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
        lsmash_bs_put_be32( bs, trak_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'a', 'k' ) );
        lsmash_bs_put_be32( bs, mdia_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'd', 'i', 'a' ) );
        lsmash_bs_put_be32( bs, hdlr_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'h', 'd', 'l', 'r' ) );
        lsmash_bs_put_be32( bs, 0 ); /* version + flags */
        lsmash_bs_put_be32( bs, 0 ); /* componentType */
        lsmash_bs_put_be32( bs, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK ); /* componentSubtype */
        lsmash_bs_put_be32( bs, 0 ); /* componentManufacturer */
        lsmash_bs_put_be32( bs, 0 ); /* componentFlags */
        lsmash_bs_put_be32( bs, 0 ); /* componentFlagsMask */
        if( case_idx == 1 )
            lsmash_bs_put_be32( bs, LSMASH_4CC( 'v', 'i', 'd', 'e' ) ); /* partial componentName before EOF */

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( bad_mp4, raw, raw_len ) == 0, "13t: write bad_mp4 (truncated hdlr)" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13t: open bad_mp4 (truncated hdlr)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13t: lsmash_read_file rejects truncated hdlr with forged box->size" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13t: 100% rollback after truncated hdlr" );
#endif
        remove( bad_mp4 );
    }

    /* 13u. Truncated dref url box with forged huge box->size (Heap OOB Write defense) */
    for( int case_idx = 0; case_idx < 2; case_idx++ )
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_ISOM );
        lsmash_bs_put_be32( bs, 0 );
        uint32_t url_sz  = 0x00002000;
        uint32_t dref_sz = 16 + url_sz;
        uint32_t dinf_sz = 8 + dref_sz;
        uint32_t minf_sz = 8 + dinf_sz;
        uint32_t mdia_sz = 8 + minf_sz;
        uint32_t trak_sz = 8 + mdia_sz;
        uint32_t moov_sz = 8 + trak_sz;
        lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
        lsmash_bs_put_be32( bs, trak_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'a', 'k' ) );
        lsmash_bs_put_be32( bs, mdia_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'd', 'i', 'a' ) );
        lsmash_bs_put_be32( bs, minf_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'i', 'n', 'f' ) );
        lsmash_bs_put_be32( bs, dinf_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'd', 'i', 'n', 'f' ) );
        lsmash_bs_put_be32( bs, dref_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'd', 'r', 'e', 'f' ) );
        lsmash_bs_put_be32( bs, 0 ); /* version + flags */
        lsmash_bs_put_be32( bs, 1 ); /* entry_count */
        lsmash_bs_put_be32( bs, url_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'u', 'r', 'l', ' ' ) );
        lsmash_bs_put_be32( bs, 0 ); /* version + flags */
        if( case_idx == 1 )
            lsmash_bs_put_bytes( bs, 5, "http:" ); /* truncated mid-location */

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( bad_mp4, raw, raw_len ) == 0, "13u: write bad_mp4 (truncated dref url)" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13u: open bad_mp4 (truncated dref url)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13u: lsmash_read_file rejects truncated dref url with forged box->size" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13u: 100% rollback after truncated dref url" );
#endif
        remove( bad_mp4 );
    }

    /* 13v. Truncated svhd and emsg (v0 / v1) boxes with forged huge box->size */
    for( int case_idx = 0; case_idx < 3; case_idx++ )
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_ISOM );
        lsmash_bs_put_be32( bs, 0 );
        if( case_idx == 0 )
        {
            /* svhd inside moov.trak.mdia.minf.stbl.stsd.av01.sv3d.svhd */
            uint32_t hdlr_sz = 33;
            uint32_t svhd_sz = 0x00002000;
            uint32_t sv3d_sz = 8 + svhd_sz;
            uint32_t av01_sz = 86 + sv3d_sz;
            uint32_t stsd_sz = 16 + av01_sz;
            uint32_t stbl_sz = 8 + stsd_sz;
            uint32_t minf_sz = 8 + stbl_sz;
            uint32_t mdia_sz = 8 + hdlr_sz + minf_sz;
            uint32_t trak_sz = 8 + mdia_sz;
            uint32_t moov_sz = 8 + trak_sz;
            lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
            lsmash_bs_put_be32( bs, trak_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'a', 'k' ) );
            lsmash_bs_put_be32( bs, mdia_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'd', 'i', 'a' ) );
            lsmash_bs_put_be32( bs, hdlr_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'h', 'd', 'l', 'r' ) );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_byte( bs, 0 );
            lsmash_bs_put_be32( bs, minf_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'i', 'n', 'f' ) );
            lsmash_bs_put_be32( bs, stbl_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 't', 'b', 'l' ) );
            lsmash_bs_put_be32( bs, stsd_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 't', 's', 'd' ) );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 1 );
            lsmash_bs_put_be32( bs, av01_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'a', 'v', '0', '1' ) );
            for( int i = 0; i < 6; i++ )
                lsmash_bs_put_byte( bs, 0 );
            lsmash_bs_put_be16( bs, 1 ); /* data_reference_index */
            for( int i = 0; i < 16; i++ )
                lsmash_bs_put_byte( bs, 0 );
            lsmash_bs_put_be16( bs, 64 ); /* width */
            lsmash_bs_put_be16( bs, 64 ); /* height */
            lsmash_bs_put_be32( bs, 0x00480000 );
            lsmash_bs_put_be32( bs, 0x00480000 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be16( bs, 1 );
            for( int i = 0; i < 32; i++ )
                lsmash_bs_put_byte( bs, 0 );
            lsmash_bs_put_be16( bs, 0x0018 );
            lsmash_bs_put_be16( bs, 0xFFFF ); /* color_table_ID = -1 */
            lsmash_bs_put_be32( bs, sv3d_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 'v', '3', 'd' ) );
            lsmash_bs_put_be32( bs, svhd_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 'v', 'h', 'd' ) );
            lsmash_bs_put_be32( bs, 0 ); /* version + flags */
            lsmash_bs_put_bytes( bs, 4, "sv3d" ); /* truncated metadata_source */
        }
        else if( case_idx == 1 )
        {
            /* emsg v0 with forged box->size and non-terminated cstring at EOF */
            lsmash_bs_put_be32( bs, 0x00002000 );
            lsmash_bs_put_be32( bs, LSMASH_4CC( 'e', 'm', 's', 'g' ) );
            lsmash_bs_put_be32( bs, 0 ); /* version 0, flags 0 */
            lsmash_bs_put_bytes( bs, 9, "urn:test\0" );
            lsmash_bs_put_bytes( bs, 3, "val" ); /* missing NUL and remaining fields */
        }
        else
        {
            /* emsg v1 with forged box->size truncated inside message_data */
            lsmash_bs_put_be32( bs, 0x00002000 );
            lsmash_bs_put_be32( bs, LSMASH_4CC( 'e', 'm', 's', 'g' ) );
            lsmash_bs_put_be32( bs, 0x01000000 ); /* version 1, flags 0 */
            lsmash_bs_put_be32( bs, 1000 ); /* timescale */
            lsmash_bs_put_be64( bs, 0 );    /* presentation_time */
            lsmash_bs_put_be32( bs, 100 );  /* event_duration */
            lsmash_bs_put_be32( bs, 1 );    /* id */
            lsmash_bs_put_bytes( bs, 9, "urn:test\0" );
            lsmash_bs_put_bytes( bs, 4, "val\0" );
            lsmash_bs_put_be16( bs, 0x1234 ); /* partial message_data before EOF */
        }

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( bad_mp4, raw, raw_len ) == 0, "13v: write bad_mp4 (truncated svhd/emsg)" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13v: open bad_mp4 (truncated svhd/emsg)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13v: lsmash_read_file rejects truncated svhd/emsg with forged box->size" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13v: 100% rollback after truncated svhd/emsg" );
#endif
        remove( bad_mp4 );
    }

    /* 13w. Truncated sdtp and QT audio description (SoundDescriptionV1 / v0 extra-byte skip) with forged box->size */
    for( int case_idx = 0; case_idx < 3; case_idx++ )
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, case_idx == 0 ? ISOM_BRAND_TYPE_ISOM : ISOM_BRAND_TYPE_QT );
        lsmash_bs_put_be32( bs, 0 );
        if( case_idx == 0 )
        {
            /* sdtp inside moov.trak.mdia.minf.stbl.sdtp with forged size */
            uint32_t sdtp_sz = 0x00002000;
            uint32_t stbl_sz = 8 + sdtp_sz;
            uint32_t minf_sz = 8 + stbl_sz;
            uint32_t mdia_sz = 8 + minf_sz;
            uint32_t trak_sz = 8 + mdia_sz;
            uint32_t moov_sz = 8 + trak_sz;
            lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
            lsmash_bs_put_be32( bs, trak_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'a', 'k' ) );
            lsmash_bs_put_be32( bs, mdia_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'd', 'i', 'a' ) );
            lsmash_bs_put_be32( bs, minf_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'i', 'n', 'f' ) );
            lsmash_bs_put_be32( bs, stbl_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 't', 'b', 'l' ) );
            lsmash_bs_put_be32( bs, sdtp_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 'd', 't', 'p' ) );
            lsmash_bs_put_be32( bs, 0 ); /* version + flags */
            lsmash_bs_put_be16( bs, 0 ); /* 2 sample dependency bytes before EOF */
        }
        else
        {
            /* QT Sound Sample Description (case 1: SoundDescriptionV1 version==1; case 2: version==0 skip loop) */
            uint32_t hdlr_sz  = 33;
            uint32_t audio_sz = 0x00002000;
            uint32_t stsd_sz  = 16 + audio_sz;
            uint32_t stbl_sz  = 8 + stsd_sz;
            uint32_t minf_sz  = 8 + stbl_sz;
            uint32_t mdia_sz  = 8 + hdlr_sz + minf_sz;
            uint32_t trak_sz  = 8 + mdia_sz;
            uint32_t moov_sz  = 8 + trak_sz;
            lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
            lsmash_bs_put_be32( bs, trak_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'a', 'k' ) );
            lsmash_bs_put_be32( bs, mdia_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'd', 'i', 'a' ) );
            lsmash_bs_put_be32( bs, hdlr_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'h', 'd', 'l', 'r' ) );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_byte( bs, 0 );
            lsmash_bs_put_be32( bs, minf_sz );  lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'i', 'n', 'f' ) );
            lsmash_bs_put_be32( bs, stbl_sz );  lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 't', 'b', 'l' ) );
            lsmash_bs_put_be32( bs, stsd_sz );  lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 't', 's', 'd' ) );
            lsmash_bs_put_be32( bs, 0 ); /* stsd version 0 */
            lsmash_bs_put_be32( bs, 1 ); /* entry_count = 1 */
            lsmash_bs_put_be32( bs, audio_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 'o', 'w', 't' ) );
            for( int i = 0; i < 6; i++ )
                lsmash_bs_put_byte( bs, 0 );
            lsmash_bs_put_be16( bs, 1 ); /* data_reference_index */
            lsmash_bs_put_be16( bs, case_idx == 1 ? 1 : 0 ); /* version 1 (SoundDescriptionV1) or version 0 */
            lsmash_bs_put_be16( bs, 0 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be16( bs, 2 );  /* channelcount */
            lsmash_bs_put_be16( bs, 16 ); /* samplesize */
            lsmash_bs_put_be16( bs, 0 );
            lsmash_bs_put_be16( bs, 0 );
            lsmash_bs_put_be32( bs, 48000u << 16 );
            lsmash_bs_put_be32( bs, 0 ); /* partial v1 field or trailing zero bytes before EOF */
        }

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( bad_mp4, raw, raw_len ) == 0, "13w: write bad_mp4 (truncated sdtp/audio)" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13w: open bad_mp4 (truncated sdtp/audio)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13w: lsmash_read_file rejects truncated sdtp/SoundDescription with forged box->size" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13w: 100% rollback after truncated sdtp/SoundDescription" );
#endif
        remove( bad_mp4 );
    }

    /* 13x. Truncated ctab (color_table->size == 0xFFFF) and sample/index/font tables
     *      (stts, stco, sidx, ftab) with excessive entry_count and forged box->size */
    for( int case_idx = 0; case_idx < 5; case_idx++ )
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_ISOM );
        lsmash_bs_put_be32( bs, 0 );
        if( case_idx == 0 )
        {
            /* ctab with color_table->size == 0xFFFF and forged box->size */
            uint32_t ctab_sz = 16 + 65536 * 8;
            uint32_t moov_sz = 8 + ctab_sz;
            lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
            lsmash_bs_put_be32( bs, ctab_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'c', 't', 'a', 'b' ) );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be16( bs, 0 );
            lsmash_bs_put_be16( bs, 0xFFFF );
            lsmash_bs_put_be64( bs, 0 ); /* 1 entry before EOF */
        }
        else if( case_idx == 1 || case_idx == 2 )
        {
            /* stts or stco with excessive entry_count and forged box->size */
            uint32_t tbl_sz  = 0x00100000;
            uint32_t stbl_sz = 8 + tbl_sz;
            uint32_t minf_sz = 8 + stbl_sz;
            uint32_t mdia_sz = 8 + minf_sz;
            uint32_t trak_sz = 8 + mdia_sz;
            uint32_t moov_sz = 8 + trak_sz;
            lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
            lsmash_bs_put_be32( bs, trak_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'a', 'k' ) );
            lsmash_bs_put_be32( bs, mdia_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'd', 'i', 'a' ) );
            lsmash_bs_put_be32( bs, minf_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'i', 'n', 'f' ) );
            lsmash_bs_put_be32( bs, stbl_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 't', 'b', 'l' ) );
            lsmash_bs_put_be32( bs, tbl_sz );
            lsmash_bs_put_be32( bs, case_idx == 1 ? LSMASH_4CC( 's', 't', 't', 's' ) : LSMASH_4CC( 's', 't', 'c', 'o' ) );
            lsmash_bs_put_be32( bs, 0 );       /* version + flags */
            lsmash_bs_put_be32( bs, 0x10000 ); /* entry_count */
            lsmash_bs_put_be64( bs, 1 );       /* partial entry before EOF */
        }
        else if( case_idx == 3 )
        {
            /* sidx with reference_count == 0xFFFF and forged box->size */
            lsmash_bs_put_be32( bs, 0x00100000 );
            lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 'i', 'd', 'x' ) );
            lsmash_bs_put_be32( bs, 0 );      /* version 0, flags 0 */
            lsmash_bs_put_be32( bs, 1 );      /* reference_ID */
            lsmash_bs_put_be32( bs, 1000 );   /* timescale */
            lsmash_bs_put_be32( bs, 0 );      /* earliest_presentation_time */
            lsmash_bs_put_be32( bs, 0 );      /* first_offset */
            lsmash_bs_put_be16( bs, 0 );      /* reserved */
            lsmash_bs_put_be16( bs, 0xFFFF ); /* reference_count */
            lsmash_bs_put_be32( bs, 100 );
            lsmash_bs_put_be32( bs, 100 );
            lsmash_bs_put_be32( bs, 0 );      /* 1 reference before EOF */
        }
        else
        {
            /* ftab inside tx3g with entry_count == 0xFFFF and forged box->size */
            uint32_t hdlr_sz = 33;
            uint32_t ftab_sz = 0x00100000;
            uint32_t tx3g_sz = 46 + ftab_sz;
            uint32_t stsd_sz = 16 + tx3g_sz;
            uint32_t stbl_sz = 8 + stsd_sz;
            uint32_t minf_sz = 8 + stbl_sz;
            uint32_t mdia_sz = 8 + hdlr_sz + minf_sz;
            uint32_t trak_sz = 8 + mdia_sz;
            uint32_t moov_sz = 8 + trak_sz;
            lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
            lsmash_bs_put_be32( bs, trak_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'a', 'k' ) );
            lsmash_bs_put_be32( bs, mdia_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'd', 'i', 'a' ) );
            lsmash_bs_put_be32( bs, hdlr_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'h', 'd', 'l', 'r' ) );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, ISOM_MEDIA_HANDLER_TYPE_TEXT_TRACK );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_byte( bs, 0 );
            lsmash_bs_put_be32( bs, minf_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'i', 'n', 'f' ) );
            lsmash_bs_put_be32( bs, stbl_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 't', 'b', 'l' ) );
            lsmash_bs_put_be32( bs, stsd_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 't', 's', 'd' ) );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 1 );
            lsmash_bs_put_be32( bs, tx3g_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'x', '3', 'g' ) );
            for( int i = 0; i < 6; i++ )
                lsmash_bs_put_byte( bs, 0 );
            lsmash_bs_put_be16( bs, 1 ); /* data_reference_index */
            for( int i = 0; i < 30; i++ )
                lsmash_bs_put_byte( bs, 0 );
            lsmash_bs_put_be32( bs, ftab_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'a', 'b' ) );
            lsmash_bs_put_be16( bs, 0xFFFF ); /* entry_count */
            lsmash_bs_put_be16( bs, 1 );      /* font_ID */
            lsmash_bs_put_byte( bs, 4 );      /* font_name_length */
            lsmash_bs_put_bytes( bs, 4, "Sans" );
        }

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( bad_mp4, raw, raw_len ) == 0, "13x: write bad_mp4 (truncated table)" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13x: open bad_mp4 (truncated table)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13x: lsmash_read_file rejects truncated table with forged box->size" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13x: 100% rollback after truncated table" );
#endif
        remove( bad_mp4 );
    }

    /* 13y. Truncated esds / iods Descriptor streams with forged excessive header->size,
     *      child descriptor overrun (end_pos < pos), and overlong VLQ length headers */
    for( int case_idx = 0; case_idx < 4; case_idx++ )
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_ISOM );
        lsmash_bs_put_be32( bs, 0 );
        if( case_idx == 0 )
        {
            /* iods with forged box->size and MP4_IOD_Tag (0x10) with forged VLQ size truncated inside ES_ID_Inc */
            uint32_t iods_sz = 0x00010000;
            uint32_t moov_sz = 8 + iods_sz;
            lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
            lsmash_bs_put_be32( bs, iods_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'i', 'o', 'd', 's' ) );
            lsmash_bs_put_be32( bs, 0 );    /* version + flags */
            lsmash_bs_put_byte( bs, 0x10 ); /* MP4_IOD_Tag */
            lsmash_bs_put_byte( bs, 0x80 );
            lsmash_bs_put_byte( bs, 0x80 );
            lsmash_bs_put_byte( bs, 0x80 );
            lsmash_bs_put_byte( bs, 0x7F ); /* forged descriptor size */
            lsmash_bs_put_be16( bs, 0x004F ); /* ObjectDescriptorID = 1, includeInlineProfileLevelFlag = 0 */
            lsmash_bs_put_byte( bs, 0xFF ); /* ODProfileLevelIndication */
            lsmash_bs_put_byte( bs, 0xFF ); /* sceneProfileLevelIndication */
            lsmash_bs_put_byte( bs, 0xFF ); /* audioProfileLevelIndication */
            lsmash_bs_put_byte( bs, 0xFF ); /* visualProfileLevelIndication */
            lsmash_bs_put_byte( bs, 0xFF ); /* graphicsProfileLevelIndication */
            lsmash_bs_put_byte( bs, 0x0E ); /* ES_ID_IncTag */
            lsmash_bs_put_byte( bs, 0x04 ); /* size = 4 */
            lsmash_bs_put_be16( bs, 0x0001 ); /* truncated ES_ID before EOF */
        }
        else
        {
            /* esds inside moov.trak.mdia.minf.stbl.stsd.mp4a.esds */
            uint32_t hdlr_sz = 33;
            uint32_t esds_sz = case_idx == 1 ? 0x00010000 : 48;
            uint32_t mp4a_sz = 36 + esds_sz;
            uint32_t stsd_sz = 16 + mp4a_sz;
            uint32_t stbl_sz = 8 + stsd_sz;
            uint32_t minf_sz = 8 + stbl_sz;
            uint32_t mdia_sz = 8 + hdlr_sz + minf_sz;
            uint32_t trak_sz = 8 + mdia_sz;
            uint32_t moov_sz = 8 + trak_sz;
            lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
            lsmash_bs_put_be32( bs, trak_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 't', 'r', 'a', 'k' ) );
            lsmash_bs_put_be32( bs, mdia_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'd', 'i', 'a' ) );
            lsmash_bs_put_be32( bs, hdlr_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'h', 'd', 'l', 'r' ) );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_byte( bs, 0 );
            lsmash_bs_put_be32( bs, minf_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'i', 'n', 'f' ) );
            lsmash_bs_put_be32( bs, stbl_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 't', 'b', 'l' ) );
            lsmash_bs_put_be32( bs, stsd_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 's', 't', 's', 'd' ) );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 1 );
            lsmash_bs_put_be32( bs, mp4a_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'p', '4', 'a' ) );
            for( int i = 0; i < 6; i++ )
                lsmash_bs_put_byte( bs, 0 );
            lsmash_bs_put_be16( bs, 1 );
            lsmash_bs_put_be64( bs, 0 );
            lsmash_bs_put_be16( bs, 2 );
            lsmash_bs_put_be16( bs, 16 );
            lsmash_bs_put_be32( bs, 0 );
            lsmash_bs_put_be32( bs, 48000u << 16 );
            lsmash_bs_put_be32( bs, esds_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'e', 's', 'd', 's' ) );
            lsmash_bs_put_be32( bs, 0 ); /* version + flags */
            if( case_idx == 1 )
            {
                /* ES_DescrTag (0x03) + DecoderConfigDescrTag (0x04) + DecSpecificInfoTag (0x05) with forged VLQ sizes */
                lsmash_bs_put_byte( bs, 0x03 ); /* ES_DescrTag */
                lsmash_bs_put_byte( bs, 0x80 );
                lsmash_bs_put_byte( bs, 0x80 );
                lsmash_bs_put_byte( bs, 0x80 );
                lsmash_bs_put_byte( bs, 0x7F );
                lsmash_bs_put_be16( bs, 1 );    /* ES_ID */
                lsmash_bs_put_byte( bs, 0 );    /* flags */
                lsmash_bs_put_byte( bs, 0x04 ); /* DecoderConfigDescrTag */
                lsmash_bs_put_byte( bs, 0x80 );
                lsmash_bs_put_byte( bs, 0x80 );
                lsmash_bs_put_byte( bs, 0x40 );
                lsmash_bs_put_byte( bs, 0x00 );
                lsmash_bs_put_byte( bs, 0x40 ); /* objectTypeIndication = Audio ISO/IEC 14496-3 */
                lsmash_bs_put_byte( bs, 0x15 ); /* streamType = AudioStream */
                for( int i = 0; i < 11; i++ )
                    lsmash_bs_put_byte( bs, 0 );
                lsmash_bs_put_byte( bs, 0x05 ); /* DecSpecificInfoTag */
                lsmash_bs_put_byte( bs, 0x80 );
                lsmash_bs_put_byte( bs, 0x80 );
                lsmash_bs_put_byte( bs, 0x20 );
                lsmash_bs_put_byte( bs, 0x00 );
                lsmash_bs_put_be16( bs, 0x1190 ); /* truncated AudioSpecificConfig before EOF */
            }
            else if( case_idx == 2 )
            {
                /* ES_DescrTag (0x03) with small size = 5 containing DecoderConfigDescrTag (0x04) with larger size = 20 */
                lsmash_bs_put_byte( bs, 0x03 ); /* ES_DescrTag */
                lsmash_bs_put_byte( bs, 0x05 ); /* size = 5 (too small for child DecoderConfigDescriptor) */
                lsmash_bs_put_be16( bs, 1 );    /* ES_ID */
                lsmash_bs_put_byte( bs, 0 );    /* flags */
                lsmash_bs_put_byte( bs, 0x04 ); /* DecoderConfigDescrTag */
                lsmash_bs_put_byte( bs, 0x14 ); /* size = 20 (overruns parent end_pos) */
                for( int i = 0; i < 29; i++ )
                    lsmash_bs_put_byte( bs, 0 );
            }
            else
            {
                /* ES_DescrTag (0x03) with overlong 5-byte VLQ length header */
                lsmash_bs_put_byte( bs, 0x03 ); /* ES_DescrTag */
                for( int i = 0; i < 5; i++ )
                    lsmash_bs_put_byte( bs, 0x81 );
                for( int i = 0; i < 30; i++ )
                    lsmash_bs_put_byte( bs, 0 );
            }
        }

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( bad_mp4, raw, raw_len ) == 0, "13y: write bad_mp4 (malformed esds/iods)" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bad_mp4, 1, &param ) == 0, "13y: open bad_mp4 (malformed esds/iods)" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) == LSMASH_ERR_INVALID_DATA,
                     "13y: lsmash_read_file rejects malformed/truncated esds/iods descriptor" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13y: 100% rollback after malformed/truncated esds/iods descriptor" );
#endif
        remove( bad_mp4 );
    }

    /* 13z. Verify color_table->size == 0xFFFF boundary across read_ctab, write_ctab, print_ctab,
     *      and visual sample entry color_table clamping */
    {
        const char *ctab_mp4  = "tests/tmp_test_ctab_max.mov";
        const char *ctab_dump = "tests/tmp_test_ctab_max_dump.txt";
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_QT );
        lsmash_bs_put_be32( bs, 0 );
        uint32_t ctab_sz = 16 + 65536 * 8;
        uint32_t moov_sz = 8 + ctab_sz;
        lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
        lsmash_bs_put_be32( bs, ctab_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'c', 't', 'a', 'b' ) );
        lsmash_bs_put_be32( bs, 0 );      /* seed */
        lsmash_bs_put_be16( bs, 0x8000 ); /* flags */
        lsmash_bs_put_be16( bs, 0xFFFF ); /* size = 65535 (65536 entries) */
        for( uint32_t i = 0; i <= 0xFFFF; i++ )
        {
            lsmash_bs_put_be16( bs, (uint16_t)i );
            lsmash_bs_put_be16( bs, (uint16_t)(i ^ 0x1111) );
            lsmash_bs_put_be16( bs, (uint16_t)(i ^ 0x2222) );
            lsmash_bs_put_be16( bs, (uint16_t)(i ^ 0x3333) );
        }
        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( ctab_mp4, raw, raw_len ) == 0, "13z: write ctab_mp4 (size == 0xFFFF)" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( ctab_mp4, 1, &param ) == 0, "13z: open ctab_mp4" );
        param.mode |= LSMASH_FILE_MODE_DUMP;
        lsmash_file_t *file = lsmash_set_file( root, &param );
        (void)lsmash_read_file( file, &param );
        file->root->file  = file;
        file->initializer = file;
        TEST_ASSERT( LSMASH_IS_EXISTING_BOX( file->moov->ctab )
                  && file->moov->ctab->color_table.size == 0xFFFF
                  && file->moov->ctab->color_table.array != NULL
                  && file->moov->ctab->color_table.array[0xFFFF].value == 0xFFFF,
                     "13z: moov.ctab with size == 0xFFFF parsed accurately" );
        /* Verify print_qt_color_table does not wrap around 16-bit counter at size == 0xFFFF */
        TEST_ASSERT( lsmash_print_movie( root, ctab_dump ) == 0, "13z: lsmash_print_movie dumps 65536-entry ctab without hang" );
        remove( ctab_dump );
        /* Verify write_ctab -> write_bs_put_qt_color_table does not wrap around 16-bit counter at size == 0xFFFF */
        lsmash_bs_t *wbs = lsmash_bs_create();
        TEST_ASSERT( file->moov->ctab->write != NULL
                  && file->moov->ctab->write( wbs, (isom_box_t *)file->moov->ctab ) == 0
                  && wbs->buffer.store == ctab_sz,
                     "13z: write_ctab serializes all 65536 entries without 16-bit wraparound" );
        lsmash_bs_cleanup( wbs );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
        remove( ctab_mp4 );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "13z: 0 bytes leaked after 65536-entry ctab read/print/write" );
#endif
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

    /* 17d. Monotonic chunk_number advancement in lsm_movie_add_preceding_box_size when external dref is followed by non-monotonic stsc */
    {
        const char *mono_path = "tests/tmp_test_dref_mono_movie.mp4";
        lsmash_root_t *root = lsmash_create_root();
        TEST_ASSERT( root != NULL, "17d: create root" );
        lsmash_file_parameters_t movie_param = { 0 };
        TEST_ASSERT( lsmash_open_file( mono_path, 0, &movie_param ) == 0, "17d: open movie file" );
        lsmash_file_t *movie_file = lsmash_set_file( root, &movie_param );
        TEST_ASSERT( movie_file != NULL, "17d: set movie file" );
        lsmash_file_parameters_t media_param = { 0 };
        TEST_ASSERT( lsmash_open_file( media_path, 0, &media_param ) == 0, "17d: open external media file" );
        media_param.mode = LSMASH_FILE_MODE_WRITE | LSMASH_FILE_MODE_MEDIA;
        lsmash_file_t *media_file = lsmash_set_file( root, &media_param );
        TEST_ASSERT( media_file != NULL, "17d: set external media file" );
        lsmash_movie_parameters_t movie_pm;
        lsmash_initialize_movie_parameters( &movie_pm );
        TEST_ASSERT( lsmash_set_movie_parameters( root, &movie_pm ) == 0, "17d: set movie parameters" );
        uint32_t mono_tid = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        TEST_ASSERT( mono_tid != 0, "17d: create track" );

        lsmash_data_reference_t self_ref = { .index = 1, .location = NULL };
        TEST_ASSERT( lsmash_create_data_reference( root, mono_tid, &self_ref, movie_file ) == 0, "17d: self-contained dref" );
        lsmash_data_reference_t ext_ref = { .index = 0, .location = (char *)media_name };
        TEST_ASSERT( lsmash_create_data_reference( root, mono_tid, &ext_ref, media_file ) == 0, "17d: external dref" );

        lsmash_video_summary_t *sum_ext = create_synth_av01_summary( 64, 64 );
        sum_ext->data_ref_index = ext_ref.index;
        TEST_ASSERT( lsmash_add_sample_entry( root, mono_tid, sum_ext ) == 1, "17d: sample entry 1 (external)" );
        lsmash_cleanup_summary( (lsmash_summary_t *)sum_ext );

        lsmash_video_summary_t *sum_self = create_synth_av01_summary( 64, 64 );
        sum_self->data_ref_index = self_ref.index;
        TEST_ASSERT( lsmash_add_sample_entry( root, mono_tid, sum_self ) == 2, "17d: sample entry 2 (self-contained)" );
        lsmash_cleanup_summary( (lsmash_summary_t *)sum_self );

        isom_trak_t *trak = (isom_trak_t *)movie_file->moov->trak_list.head->data;
        isom_stbl_t *stbl = trak->mdia->minf->stbl;
        isom_stco_entry_t *c1 = (isom_stco_entry_t *)lsm_list_append( stbl->stco->list );
        isom_stco_entry_t *c2 = (isom_stco_entry_t *)lsm_list_append( stbl->stco->list );
        TEST_ASSERT( c1 && c2, "17d: append stco entries" );
        c1->chunk_offset = 100;
        c2->chunk_offset = 200;

        /* Duplicate/non-monotonic first_chunk=1 after external dref entry */
        isom_stsc_entry_t *s1 = (isom_stsc_entry_t *)lsm_list_append( stbl->stsc->list );
        isom_stsc_entry_t *s2 = (isom_stsc_entry_t *)lsm_list_append( stbl->stsc->list );
        TEST_ASSERT( s1 && s2, "17d: append stsc entries" );
        s1->first_chunk = 1; s1->samples_per_chunk = 1; s1->sample_description_index = 1; /* external */
        s2->first_chunk = 1; s2->samples_per_chunk = 1; s2->sample_description_index = 2; /* self-contained */

        lsm_movie_add_preceding_box_size( movie_file->moov, 50 );
        TEST_ASSERT( c1->chunk_offset == 150 && c2->chunk_offset == 250,
                     "17d: monotonic chunk_number advancement applies preceding_size without underflow or infinite loop" );

        lsmash_close_file( &media_param );
        lsmash_close_file( &movie_param );
        lsmash_destroy_root( root );
        remove( mono_path );
    }
    remove( movie_path );
    remove( media_path );
    printf( "[PASS] test_external_data_reference_roundtrip\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 23: Container Metadata, Chapter, Track Reference & Edit List Round-Trip
 *---------------------------------------------------------------------------*/
int test_container_metadata_and_edit_list_roundtrip( void )
{
    const char *mp4_path  = "tests/tmp_test_container_meta_elst.mp4";
    const char *bom_path  = "tests/tmp_test_container_chpl_bom.mp4";
    const char *qt_path   = "tests/tmp_test_container_qt_ctab_keys.mov";
    const char *chap_file = "tests/tmp_test_chapters.txt";
    const char *dump_path = "tests/tmp_test_container_dump.txt";

    /* Prepare a synthetic chapter file with 3 entries */
    {
        const char *chap_text =
            "CHAPTER01=00:00:00.000\n"
            "CHAPTER01NAME=Opening Scene\n"
            "CHAPTER02=00:00:01.200\n"
            "CHAPTER02NAME=Main Part\n"
            "CHAPTER03=00:00:02.500\n"
            "CHAPTER03NAME=Finale\n";
        TEST_ASSERT( write_test_file( chap_file, (const uint8_t *)chap_text, strlen( chap_text ) ) == 0,
                     "23: write synthetic chapter file" );
    }

    /* 23a. Build MP4 with elst v0 (Empty Edit + media offset), elst v1 (64-bit duration),
     *      tref.chap reference chapter track, moov.udta.chpl, and iTunes metadata (meta/ilst) */
    char long_desc[300];
    memset( long_desc, 'D', sizeof(long_desc) - 1 );
    long_desc[sizeof(long_desc) - 1] = '\0';
    static const uint8_t png_sig[8]  = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    static const uint8_t trkn_bin[8] = { 0x00, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00 };
    {
        lsmash_root_t *root = lsmash_create_root();
        TEST_ASSERT( root != NULL, "23a: create root" );
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( mp4_path, 0, &param ) == 0, "23a: open mp4_path" );
        lsmash_brand_type brands[3] = { ISOM_BRAND_TYPE_M4V, ISOM_BRAND_TYPE_MP42, ISOM_BRAND_TYPE_ISOM };
        param.major_brand   = ISOM_BRAND_TYPE_M4V;
        param.brands        = brands;
        param.brand_count   = 3;
        param.minor_version = 0;
        TEST_ASSERT( lsmash_set_file( root, &param ) != NULL, "23a: set file" );

        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        movie_param.timescale = 600;
        TEST_ASSERT( lsmash_set_movie_parameters( root, &movie_param ) == 0, "23a: set movie params" );

        /* Track 1: Video track (will carry elst v0 + tref.chap reference) */
        uint32_t vtid1 = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        TEST_ASSERT( vtid1 == 1, "23a: create video track 1" );
        lsmash_track_parameters_t vtp;
        lsmash_initialize_track_parameters( &vtp );
        vtp.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
        TEST_ASSERT( lsmash_set_track_parameters( root, vtid1, &vtp ) == 0, "23a: set track 1 params" );
        lsmash_media_parameters_t vmp;
        lsmash_initialize_media_parameters( &vmp );
        vmp.timescale = 1000;
        TEST_ASSERT( lsmash_set_media_parameters( root, vtid1, &vmp ) == 0, "23a: set media 1 params" );
        lsmash_video_summary_t *vsum1 = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( lsmash_add_sample_entry( root, vtid1, vsum1 ) == 1, "23a: add sample entry on track 1" );
        lsmash_cleanup_summary( (lsmash_summary_t *)vsum1 );

        for( uint32_t i = 0; i < 6; i++ )
        {
            lsmash_sample_t *s = lsmash_create_sample( 24 );
            fill_pattern( s->data, 24, (uint8_t)(0x30 + i) );
            s->dts   = i * 500;
            s->cts   = i * 500;
            s->index = 1;
            s->prop.ra_flags = (i % 3 == 0) ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC : ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
            TEST_ASSERT( lsmash_append_sample( root, vtid1, s ) == 0, "23a: append sample to track 1" );
        }
        TEST_ASSERT( lsmash_flush_pooled_samples( root, vtid1, 500 ) == 0, "23a: flush track 1" );

        /* Track 2: Second video track (will carry 64-bit elst v1) */
        uint32_t vtid2 = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        TEST_ASSERT( vtid2 == 2, "23a: create video track 2" );
        TEST_ASSERT( lsmash_set_track_parameters( root, vtid2, &vtp ) == 0, "23a: set track 2 params" );
        TEST_ASSERT( lsmash_set_media_parameters( root, vtid2, &vmp ) == 0, "23a: set media 2 params" );
        lsmash_video_summary_t *vsum2 = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( lsmash_add_sample_entry( root, vtid2, vsum2 ) == 1, "23a: add sample entry on track 2" );
        lsmash_cleanup_summary( (lsmash_summary_t *)vsum2 );
        for( uint32_t i = 0; i < 2; i++ )
        {
            lsmash_sample_t *s = lsmash_create_sample( 16 );
            fill_pattern( s->data, 16, (uint8_t)(0x70 + i) );
            s->dts   = i * 500;
            s->cts   = i * 500;
            s->index = 1;
            s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            TEST_ASSERT( lsmash_append_sample( root, vtid2, s ) == 0, "23a: append sample to track 2" );
        }
        TEST_ASSERT( lsmash_flush_pooled_samples( root, vtid2, 500 ) == 0, "23a: flush track 2" );

        /* Explicit Edit List on Track 1 (v0): Empty Edit (-1) + media start offset (500), plus modify test */
        lsmash_edit_t e1 = { .duration = 300, .start_time = ISOM_EDIT_MODE_EMPTY, .rate = ISOM_EDIT_MODE_NORMAL };
        lsmash_edit_t e2 = { .duration = 999, .start_time = 250,                  .rate = ISOM_EDIT_MODE_NORMAL };
        TEST_ASSERT( lsmash_create_explicit_timeline_map( root, vtid1, e1 ) == 0, "23a: create edit 1 (empty edit) on track 1" );
        TEST_ASSERT( lsmash_create_explicit_timeline_map( root, vtid1, e2 ) == 0, "23a: create edit 2 (offset edit) on track 1" );
        e2.duration   = 1500;
        e2.start_time = 500;
        TEST_ASSERT( lsmash_modify_explicit_timeline_map( root, vtid1, 2, e2 ) == 0, "23a: modify edit 2 on track 1" );
        TEST_ASSERT( lsmash_count_explicit_timeline_map( root, vtid1 ) == 2, "23a: count edits on track 1 == 2" );

        /* Test delete_explicit_timeline_map on Track 2, then re-populate with an offset + dwell edit */
        TEST_ASSERT( lsmash_create_explicit_timeline_map( root, vtid2, e1 ) == 0, "23a: create temp edit on track 2" );
        TEST_ASSERT( lsmash_delete_explicit_timeline_map( root, vtid2 ) == 0, "23a: delete edits on track 2" );
        TEST_ASSERT( lsmash_count_explicit_timeline_map( root, vtid2 ) == 0, "23a: count edits on track 2 after delete == 0" );
        lsmash_edit_t e_t2 = { .duration = 600, .start_time = 100, .rate = ISOM_EDIT_MODE_NORMAL };
        TEST_ASSERT( lsmash_create_explicit_timeline_map( root, vtid2, e_t2 ) == 0, "23a: create edit on track 2" );

        /* Track Reference Chapter (tref.chap) on Track 1 -> creates Track 3 (text chapter track) */
        TEST_ASSERT( lsmash_create_reference_chapter_track( root, vtid1, (char *)chap_file ) == 0,
                     "23a: create reference chapter track (tref.chap)" );

        /* User Data Chapter List (moov.udta.chpl) */
        TEST_ASSERT( lsmash_set_tyrant_chapter( root, (char *)chap_file, 0 ) == 0,
                     "23a: set moov.udta.chpl chapter list" );

        /* iTunes Metadata (moov.udta.meta.ilst): string, >255B long description, integer, boolean, binary, and custom '----' */
        lsmash_itunes_metadata_t meta_title = {
            .item = ITUNES_METADATA_ITEM_TITLE,
            .type = ITUNES_METADATA_TYPE_STRING,
            .value.string = "Container Unit Test Title"
        };
        lsmash_itunes_metadata_t meta_desc = {
            .item = ITUNES_METADATA_ITEM_DESCRIPTION,
            .type = ITUNES_METADATA_TYPE_STRING,
            .value.string = long_desc /* > 255 bytes -> promoted to ITUNES_METADATA_ITEM_LONG_DESCRIPTION ('ldes') */
        };
        lsmash_itunes_metadata_t meta_bpm = {
            .item = ITUNES_METADATA_ITEM_BEATS_PER_MINUTE,
            .type = ITUNES_METADATA_TYPE_INTEGER,
            .value.integer = 140
        };
        lsmash_itunes_metadata_t meta_compil = {
            .item = ITUNES_METADATA_ITEM_DISC_COMPILATION,
            .type = ITUNES_METADATA_TYPE_BOOLEAN,
            .value.boolean = LSMASH_BOOLEAN_TRUE
        };
        lsmash_itunes_metadata_t meta_cover = {
            .item = ITUNES_METADATA_ITEM_COVER_ART,
            .type = ITUNES_METADATA_TYPE_BINARY,
            .value.binary = { .subtype = ITUNES_METADATA_SUBTYPE_PNG, .size = sizeof(png_sig), .data = (uint8_t *)png_sig }
        };
        lsmash_itunes_metadata_t meta_trkn = {
            .item = ITUNES_METADATA_ITEM_TRACK_NUMBER,
            .type = ITUNES_METADATA_TYPE_BINARY,
            .value.binary = { .subtype = ITUNES_METADATA_SUBTYPE_IMPLICIT, .size = sizeof(trkn_bin), .data = (uint8_t *)trkn_bin }
        };
        lsmash_itunes_metadata_t meta_custom = {
            .item = ITUNES_METADATA_ITEM_CUSTOM,
            .type = ITUNES_METADATA_TYPE_STRING,
            .meaning = "com.apple.iTunes",
            .name    = "REPLAYGAIN_TRACK_GAIN",
            .value.string = "-4.50 dB"
        };
        TEST_ASSERT( lsmash_set_itunes_metadata( root, meta_title )  == 0, "23a: set title metadata" );
        TEST_ASSERT( lsmash_set_itunes_metadata( root, meta_desc )   == 0, "23a: set >255B description metadata" );
        TEST_ASSERT( lsmash_set_itunes_metadata( root, meta_bpm )    == 0, "23a: set BPM integer metadata" );
        TEST_ASSERT( lsmash_set_itunes_metadata( root, meta_compil ) == 0, "23a: set compilation boolean metadata" );
        TEST_ASSERT( lsmash_set_itunes_metadata( root, meta_cover )  == 0, "23a: set PNG cover art binary metadata" );
        TEST_ASSERT( lsmash_set_itunes_metadata( root, meta_trkn )   == 0, "23a: set track number binary metadata" );
        TEST_ASSERT( lsmash_set_itunes_metadata( root, meta_custom ) == 0, "23a: set custom '----' metadata" );

        /* Negative API validation: invalid string/binary metadata must fail without leaving dangling metaitem in ilst */
        lsmash_itunes_metadata_t bad_str = {
            .item = ITUNES_METADATA_ITEM_ARTIST,
            .type = ITUNES_METADATA_TYPE_STRING,
            .value.string = NULL
        };
        lsmash_itunes_metadata_t bad_cover = {
            .item = ITUNES_METADATA_ITEM_COVER_ART,
            .type = ITUNES_METADATA_TYPE_BINARY,
            .value.binary = { .subtype = ITUNES_METADATA_SUBTYPE_UTF8, .size = 4, .data = (uint8_t *)png_sig }
        };
        lsmash_itunes_metadata_t bad_uuid = {
            .item = ITUNES_METADATA_ITEM_CUSTOM,
            .type = ITUNES_METADATA_TYPE_BINARY,
            .meaning = "com.apple.iTunes",
            .name    = "BAD_UUID",
            .value.binary = { .subtype = ITUNES_METADATA_SUBTYPE_UUID, .size = 8, .data = (uint8_t *)png_sig }
        };
        TEST_ASSERT( lsmash_set_itunes_metadata( root, bad_str )   == LSMASH_ERR_FUNCTION_PARAM, "23a: reject NULL string metadata" );
        TEST_ASSERT( lsmash_set_itunes_metadata( root, bad_cover ) == LSMASH_ERR_FUNCTION_PARAM, "23a: reject invalid COVER_ART subtype" );
        TEST_ASSERT( lsmash_set_itunes_metadata( root, bad_uuid )  == LSMASH_ERR_FUNCTION_PARAM, "23a: reject invalid UUID size != 16" );

        TEST_ASSERT( lsmash_finish_movie( root, NULL ) == 0, "23a: finish movie" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
    }

    /* 23b. Re-open MP4 and verify full round-trip of elst (v0/v1), tref.chap, chpl, and meta/ilst */
    {
        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( mp4_path, 1, &param ) == 0, "23b: open mp4_path for reading" );
        param.mode |= LSMASH_FILE_MODE_DUMP;
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) >= 0, "23b: read mp4_path" );
        TEST_ASSERT( lsmash_print_movie( root, dump_path ) == 0, "23b: print movie box hierarchy" );
        remove( dump_path );

        /* Verify Track 1 elst (version 0: Empty Edit + modified offset edit) */
        TEST_ASSERT( lsmash_count_explicit_timeline_map( root, 1 ) == 2, "23b: track 1 has 2 edits" );
        lsmash_edit_t got1 = { 0 }, got2 = { 0 };
        TEST_ASSERT( lsmash_get_explicit_timeline_map( root, 1, 1, &got1 ) == 0, "23b: get track 1 edit 1" );
        TEST_ASSERT( lsmash_get_explicit_timeline_map( root, 1, 2, &got2 ) == 0, "23b: get track 1 edit 2" );
        TEST_ASSERT( got1.duration == 300 && got1.start_time == ISOM_EDIT_MODE_EMPTY && got1.rate == ISOM_EDIT_MODE_NORMAL,
                     "23b: track 1 edit 1 (Empty Edit media_time == -1) round-trip" );
        TEST_ASSERT( got2.duration == 1500 && got2.start_time == 500 && got2.rate == ISOM_EDIT_MODE_NORMAL,
                     "23b: track 1 edit 2 (media start offset) round-trip" );

        /* Verify Track 2 elst after delete + re-create */
        TEST_ASSERT( lsmash_count_explicit_timeline_map( root, 2 ) == 1, "23b: track 2 has 1 edit" );
        lsmash_edit_t got_t2 = { 0 };
        TEST_ASSERT( lsmash_get_explicit_timeline_map( root, 2, 1, &got_t2 ) == 0, "23b: get track 2 edit" );
        TEST_ASSERT( got_t2.duration == 600 && got_t2.start_time == 100 && got_t2.rate == ISOM_EDIT_MODE_NORMAL,
                     "23b: track 2 elst round-trip after delete + re-create" );

        /* Verify Track 1 tref.chap points to Track 3 (text chapter track) and Track 3 samples round-trip */
        isom_trak_t *trak1 = (isom_trak_t *)file->moov->trak_list.head->data;
        TEST_ASSERT( LSMASH_IS_EXISTING_BOX( trak1->tref ) && trak1->tref->ref_list.head != NULL, "23b: track 1 tref exists" );
        isom_tref_type_t *chap_ref = (isom_tref_type_t *)trak1->tref->ref_list.head->data;
        TEST_ASSERT( chap_ref != NULL
                  && chap_ref->type.fourcc == QT_TREF_TYPE_CHAP
                  && chap_ref->ref_count == 1
                  && chap_ref->track_ID != NULL
                  && chap_ref->track_ID[0] == 3,
                     "23b: track 1 tref.chap references chapter track 3" );
        TEST_ASSERT( lsmash_construct_timeline( root, 3 ) == 0, "23b: construct timeline for chapter track 3" );
        lsmash_sample_t *chap_s1 = lsmash_get_sample_from_media_timeline( root, 3, 1 );
        lsmash_sample_t *chap_s2 = lsmash_get_sample_from_media_timeline( root, 3, 2 );
        lsmash_sample_t *chap_s3 = lsmash_get_sample_from_media_timeline( root, 3, 3 );
        TEST_ASSERT( chap_s1 && chap_s2 && chap_s3, "23b: retrieve all 3 chapter text samples" );
        TEST_ASSERT( chap_s1->length >= 2 + 13 && memcmp( chap_s1->data + 2, "Opening Scene", 13 ) == 0,
                     "23b: chapter track sample 1 payload matches 'Opening Scene'" );
        TEST_ASSERT( chap_s2->dts == 1200 && chap_s2->length >= 2 + 9 && memcmp( chap_s2->data + 2, "Main Part", 9 ) == 0,
                     "23b: chapter track sample 2 dts and payload match 'Main Part'" );
        TEST_ASSERT( chap_s3->dts == 2500 && chap_s3->length >= 2 + 6 && memcmp( chap_s3->data + 2, "Finale", 6 ) == 0,
                     "23b: chapter track sample 3 dts and payload match 'Finale'" );
        lsmash_delete_sample( chap_s1 );
        lsmash_delete_sample( chap_s2 );
        lsmash_delete_sample( chap_s3 );
        lsmash_destruct_timeline( root, 3 );

        /* Verify moov.udta.chpl chapter list */
        TEST_ASSERT( lsmash_count_tyrant_chapter( root ) == 3, "23b: chpl count == 3" );
        double ts1 = -1.0, ts2 = -1.0, ts3 = -1.0;
        char *cname1 = lsmash_get_tyrant_chapter( root, 1, &ts1 );
        char *cname2 = lsmash_get_tyrant_chapter( root, 2, &ts2 );
        char *cname3 = lsmash_get_tyrant_chapter( root, 3, &ts3 );
        TEST_ASSERT( cname1 && strcmp( cname1, "Opening Scene" ) == 0 && ts1 == 0.0, "23b: chpl entry 1 matches" );
        TEST_ASSERT( cname2 && strcmp( cname2, "Main Part" ) == 0     && ts2 == 1.2, "23b: chpl entry 2 matches" );
        TEST_ASSERT( cname3 && strcmp( cname3, "Finale" ) == 0        && ts3 == 2.5, "23b: chpl entry 3 matches" );
        lsmash_delete_tyrant_chapter( root );
        TEST_ASSERT( lsmash_count_tyrant_chapter( root ) == 0, "23b: chpl count == 0 after delete" );

        /* Verify iTunes metadata (meta/ilst) round-trip (7 valid items; the 3 rejected calls left no items) */
        TEST_ASSERT( lsmash_count_itunes_metadata( root ) == 7, "23b: count_itunes_metadata == 7" );
        lsmash_itunes_metadata_t got_meta;

        TEST_ASSERT( lsmash_get_itunes_metadata( root, 1, &got_meta ) == 0, "23b: get metadata 1 (title)" );
        TEST_ASSERT( got_meta.item == ITUNES_METADATA_ITEM_TITLE
                  && got_meta.type == ITUNES_METADATA_TYPE_STRING
                  && got_meta.value.string != NULL
                  && strcmp( got_meta.value.string, "Container Unit Test Title" ) == 0,
                     "23b: title metadata matches" );
        lsmash_cleanup_itunes_metadata( &got_meta );

        TEST_ASSERT( lsmash_get_itunes_metadata( root, 2, &got_meta ) == 0, "23b: get metadata 2 (long description)" );
        TEST_ASSERT( got_meta.item == ITUNES_METADATA_ITEM_LONG_DESCRIPTION
                  && got_meta.type == ITUNES_METADATA_TYPE_STRING
                  && got_meta.value.string != NULL
                  && strcmp( got_meta.value.string, long_desc ) == 0,
                     "23b: >255B description promoted to LONG_DESCRIPTION and matches" );
        lsmash_cleanup_itunes_metadata( &got_meta );

        TEST_ASSERT( lsmash_get_itunes_metadata( root, 3, &got_meta ) == 0, "23b: get metadata 3 (BPM)" );
        TEST_ASSERT( got_meta.item == ITUNES_METADATA_ITEM_BEATS_PER_MINUTE
                  && got_meta.type == ITUNES_METADATA_TYPE_INTEGER
                  && got_meta.value.integer == 140,
                     "23b: BPM integer metadata matches" );
        lsmash_cleanup_itunes_metadata( &got_meta );

        TEST_ASSERT( lsmash_get_itunes_metadata( root, 4, &got_meta ) == 0, "23b: get metadata 4 (compilation)" );
        TEST_ASSERT( got_meta.item == ITUNES_METADATA_ITEM_DISC_COMPILATION
                  && got_meta.type == ITUNES_METADATA_TYPE_BOOLEAN
                  && got_meta.value.boolean == LSMASH_BOOLEAN_TRUE,
                     "23b: compilation boolean metadata matches" );
        lsmash_cleanup_itunes_metadata( &got_meta );

        TEST_ASSERT( lsmash_get_itunes_metadata( root, 5, &got_meta ) == 0, "23b: get metadata 5 (cover art)" );
        TEST_ASSERT( got_meta.item == ITUNES_METADATA_ITEM_COVER_ART
                  && got_meta.type == ITUNES_METADATA_TYPE_BINARY
                  && got_meta.value.binary.subtype == ITUNES_METADATA_SUBTYPE_PNG
                  && got_meta.value.binary.size == sizeof(png_sig)
                  && memcmp( got_meta.value.binary.data, png_sig, sizeof(png_sig) ) == 0,
                     "23b: cover art binary metadata matches" );
        lsmash_cleanup_itunes_metadata( &got_meta );

        TEST_ASSERT( lsmash_get_itunes_metadata( root, 6, &got_meta ) == 0, "23b: get metadata 6 (track number)" );
        TEST_ASSERT( got_meta.item == ITUNES_METADATA_ITEM_TRACK_NUMBER
                  && got_meta.type == ITUNES_METADATA_TYPE_BINARY
                  && got_meta.value.binary.subtype == ITUNES_METADATA_SUBTYPE_IMPLICIT
                  && got_meta.value.binary.size == sizeof(trkn_bin)
                  && memcmp( got_meta.value.binary.data, trkn_bin, sizeof(trkn_bin) ) == 0,
                     "23b: track number binary metadata matches" );
        lsmash_cleanup_itunes_metadata( &got_meta );

        TEST_ASSERT( lsmash_get_itunes_metadata( root, 7, &got_meta ) == 0, "23b: get metadata 7 (custom)" );
        TEST_ASSERT( got_meta.item == ITUNES_METADATA_ITEM_CUSTOM
                  && got_meta.type == ITUNES_METADATA_TYPE_STRING
                  && got_meta.meaning != NULL && strcmp( got_meta.meaning, "com.apple.iTunes" ) == 0
                  && got_meta.name    != NULL && strcmp( got_meta.name, "REPLAYGAIN_TRACK_GAIN" ) == 0
                  && got_meta.value.string != NULL && strcmp( got_meta.value.string, "-4.50 dB" ) == 0,
                     "23b: custom '----' mean/name/data metadata matches" );
        lsmash_cleanup_itunes_metadata( &got_meta );

        TEST_ASSERT( lsmash_get_itunes_metadata( root, 8, &got_meta ) < 0, "23b: out-of-range metadata index rejected" );

        lsmash_close_file( &param );
        lsmash_destroy_root( root );
    }

    /* 23c. Verify 64-bit elst version 1 (pure ISOBMFF MP42+ISOM) and moov.udta.chpl with UTF-8 BOM (add_bom = 1) round-trip */
    {
        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( bom_path, 0, &param ) == 0, "23c: open bom_path" );
        lsmash_brand_type brands[2] = { ISOM_BRAND_TYPE_MP42, ISOM_BRAND_TYPE_ISOM };
        param.major_brand = ISOM_BRAND_TYPE_MP42;
        param.brands      = brands;
        param.brand_count = 2;
        TEST_ASSERT( lsmash_set_file( root, &param ) != NULL, "23c: set file" );
        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        TEST_ASSERT( lsmash_set_movie_parameters( root, &movie_param ) == 0, "23c: set movie params" );
        uint32_t vtid = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        lsmash_track_parameters_t vtp;
        lsmash_initialize_track_parameters( &vtp );
        vtp.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        lsmash_set_track_parameters( root, vtid, &vtp );
        lsmash_media_parameters_t vmp;
        lsmash_initialize_media_parameters( &vmp );
        vmp.timescale = 1000;
        lsmash_set_media_parameters( root, vtid, &vmp );
        lsmash_video_summary_t *vsum = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( lsmash_add_sample_entry( root, vtid, vsum ) == 1, "23c: add sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)vsum );
        lsmash_sample_t *s = lsmash_create_sample( 16 );
        memset( s->data, 0x55, 16 );
        s->dts = 0; s->cts = 0; s->index = 1;
        s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
        TEST_ASSERT( lsmash_append_sample( root, vtid, s ) == 0, "23c: append sample" );
        TEST_ASSERT( lsmash_flush_pooled_samples( root, vtid, 1000 ) == 0, "23c: flush sample" );
        lsmash_edit_t e_v1 = { .duration = 0x100000200ULL, .start_time = 0x100000100LL, .rate = ISOM_EDIT_MODE_NORMAL };
        TEST_ASSERT( lsmash_create_explicit_timeline_map( root, vtid, e_v1 ) == 0, "23c: create 64-bit elst v1 edit" );
        TEST_ASSERT( lsmash_set_tyrant_chapter( root, (char *)chap_file, 1 ) == 0, "23c: set chpl with BOM" );
        TEST_ASSERT( lsmash_finish_movie( root, NULL ) == 0, "23c: finish movie" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );

        lsmash_root_t *rd_root = lsmash_create_root();
        lsmash_file_parameters_t rd_param = { 0 };
        TEST_ASSERT( lsmash_open_file( bom_path, 1, &rd_param ) == 0, "23c: open bom_path for reading" );
        rd_param.mode |= LSMASH_FILE_MODE_DUMP;
        lsmash_file_t *rd_file = lsmash_set_file( rd_root, &rd_param );
        TEST_ASSERT( lsmash_read_file( rd_file, &rd_param ) >= 0, "23c: read bom_path" );
        TEST_ASSERT( lsmash_print_movie( rd_root, dump_path ) == 0, "23c: print movie with BOM chpl and elst v1" );
        remove( dump_path );
        TEST_ASSERT( lsmash_count_explicit_timeline_map( rd_root, 1 ) == 1, "23c: count 64-bit elst v1 edit == 1" );
        lsmash_edit_t got_v1 = { 0 };
        TEST_ASSERT( lsmash_get_explicit_timeline_map( rd_root, 1, 1, &got_v1 ) == 0, "23c: get 64-bit elst v1 edit" );
        TEST_ASSERT( got_v1.duration == 0x100000200ULL && got_v1.start_time == 0x100000100LL && got_v1.rate == ISOM_EDIT_MODE_NORMAL,
                     "23c: 64-bit elst v1 segment_duration and media_time round-trip" );
        TEST_ASSERT( lsmash_count_tyrant_chapter( rd_root ) == 3, "23c: chpl with BOM count == 3" );
        double ts = -1.0;
        char *cname = lsmash_get_tyrant_chapter( rd_root, 1, &ts );
        TEST_ASSERT( cname && strcmp( cname, "Opening Scene" ) == 0 && ts == 0.0,
                     "23c: lsmash_get_tyrant_chapter strips 3-byte UTF-8 BOM" );
        lsmash_close_file( &rd_param );
        lsmash_destroy_root( rd_root );
    }

    /* 23d. Verify QTFF moov.ctab and moov.meta.keys + ilst parsing and dump round-trip */
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_QT );
        lsmash_bs_put_be32( bs, 0 );
        /* ctab with 2 entries (size_field = 1): 8 (header) + 8 (seed/flags/size) + 2*8 (entries) = 32 bytes */
        uint32_t ctab_sz = 32;
        /* QTFF meta: hdlr (33 bytes) + keys (1 entry "com.apple.quicktime.author" = 26 chars -> key_size = 34, keys_sz = 16 + 34 = 50)
         * + ilst (8 + metaitem(index=1, size=8 + data(16 + 6 = 22) = 30) = 38) -> meta_sz = 8 + 33 + 50 + 38 = 129 */
        const char *key_str = "com.apple.quicktime.author";
        uint32_t key_len  = (uint32_t)strlen( key_str ); /* 26 */
        uint32_t key_sz   = 8 + key_len;                 /* 34 */
        uint32_t keys_sz  = 16 + key_sz;                 /* 50 */
        uint32_t hdlr_sz  = 33;
        uint32_t data_sz  = 16 + 6;                      /* "LSMASH" */
        uint32_t item_sz  = 8 + data_sz;                 /* 30 */
        uint32_t ilst_sz  = 8 + item_sz;                 /* 38 */
        uint32_t meta_sz  = 8 + hdlr_sz + keys_sz + ilst_sz;
        uint32_t moov_sz  = 8 + ctab_sz + meta_sz;

        lsmash_bs_put_be32( bs, moov_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );
        /* ctab */
        lsmash_bs_put_be32( bs, ctab_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'c', 't', 'a', 'b' ) );
        lsmash_bs_put_be32( bs, 0 );      /* seed */
        lsmash_bs_put_be16( bs, 0x8000 ); /* flags */
        lsmash_bs_put_be16( bs, 1 );      /* size = 1 (2 entries) */
        lsmash_bs_put_be16( bs, 0 ); lsmash_bs_put_be16( bs, 0x1111 ); lsmash_bs_put_be16( bs, 0x2222 ); lsmash_bs_put_be16( bs, 0x3333 );
        lsmash_bs_put_be16( bs, 1 ); lsmash_bs_put_be16( bs, 0xAAAA ); lsmash_bs_put_be16( bs, 0xBBBB ); lsmash_bs_put_be16( bs, 0xCCCC );
        /* QTFF meta */
        lsmash_bs_put_be32( bs, meta_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'e', 't', 'a' ) );
        lsmash_bs_put_be32( bs, hdlr_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'h', 'd', 'l', 'r' ) );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'd', 't', 'a' ) );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_byte( bs, 0 );
        /* keys */
        lsmash_bs_put_be32( bs, keys_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'k', 'e', 'y', 's' ) );
        lsmash_bs_put_be32( bs, 0 ); /* version/flags */
        lsmash_bs_put_be32( bs, 1 ); /* entry_count = 1 */
        lsmash_bs_put_be32( bs, key_sz );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'd', 't', 'a' ) );
        lsmash_bs_put_bytes( bs, key_len, (void *)key_str );
        /* ilst */
        lsmash_bs_put_be32( bs, ilst_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'i', 'l', 's', 't' ) );
        lsmash_bs_put_be32( bs, item_sz ); lsmash_bs_put_be32( bs, 1 ); /* 1-based key index = 1 */
        lsmash_bs_put_be32( bs, data_sz ); lsmash_bs_put_be32( bs, LSMASH_4CC( 'd', 'a', 't', 'a' ) );
        lsmash_bs_put_be16( bs, 0 );
        lsmash_bs_put_byte( bs, 0 );
        lsmash_bs_put_byte( bs, ITUNES_METADATA_SUBTYPE_UTF8 );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_bytes( bs, 6, (void *)"LSMASH" );

        uint32_t raw_len = 0;
        uint8_t *raw = lsmash_bs_export_data( bs, &raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( qt_path, raw, raw_len ) == 0, "23d: write qt_path" );
        lsmash_free( raw );

        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( qt_path, 1, &param ) == 0, "23d: open qt_path" );
        param.mode |= LSMASH_FILE_MODE_DUMP;
        lsmash_file_t *file = lsmash_set_file( root, &param );
        (void)lsmash_read_file( file, &param );
        file->root->file = file;
        file->initializer = file;
        TEST_ASSERT( LSMASH_IS_EXISTING_BOX( file->moov->ctab )
                  && file->moov->ctab->color_table.size == 1
                  && file->moov->ctab->color_table.array != NULL
                  && file->moov->ctab->color_table.array[1].r == 0xAAAA,
                     "23d: moov.ctab parsed accurately" );
        TEST_ASSERT( LSMASH_IS_EXISTING_BOX( file->moov->meta )
                  && LSMASH_IS_EXISTING_BOX( file->moov->meta->keys )
                  && file->moov->meta->keys->list->entry_count == 1,
                     "23d: QTFF moov.meta.keys parsed accurately" );
        TEST_ASSERT( lsmash_print_movie( root, dump_path ) == 0, "23d: print_movie dumps ctab and keys cleanly" );
        remove( dump_path );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
    }

    /*-----------------------------------------------------------------------
     * 23e. Regression tests for QT >255-byte handler names, handler type setup,
     *      LPCM fractional/zero frame_size rejection, GDR pre-roll without
     *      preceding RAP, and lsmash_set_media_timestamps atomic validation
     *-----------------------------------------------------------------------*/
    {
        lsmash_root_t *root = lsmash_create_root();
        TEST_ASSERT( root != NULL, "23e: create root" );
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( lsmash_open_file( qt_path, 0, &param ) == 0, "23e: open qt_path for write" );
        param.major_brand   = ISOM_BRAND_TYPE_QT;
        param.minor_version = 0x00000000;
        lsmash_brand_type qt_brands[1] = { ISOM_BRAND_TYPE_QT };
        param.brands      = qt_brands;
        param.brand_count = 1;
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( file != NULL, "23e: set qt file" );

        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        TEST_ASSERT( lsmash_set_movie_parameters( root, &movie_param ) == 0, "23e: set movie params" );

        uint32_t a_track = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
        TEST_ASSERT( a_track != 0, "23e: create QT audio track" );

        lsmash_track_parameters_t track_param;
        lsmash_initialize_track_parameters( &track_param );
        track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
        TEST_ASSERT( lsmash_set_track_parameters( root, a_track, &track_param ) == 0, "23e: set QT audio track params" );

        /* Verify default handler reference names distinguish Media vs Data handler */
        lsmash_media_parameters_t init_media_param;
        lsmash_initialize_media_parameters( &init_media_param );
        TEST_ASSERT( lsmash_get_media_parameters( root, a_track, &init_media_param ) == 0, "23e: get initial media params" );
        TEST_ASSERT( init_media_param.media_handler_name != NULL
                  && strcmp( init_media_param.media_handler_name, "L-SMASH Sound Media Handler" ) == 0,
                     "23e: default media handler name is 'L-SMASH Sound Media Handler'" );
        TEST_ASSERT( init_media_param.data_handler_name != NULL
                  && strcmp( init_media_param.data_handler_name, "L-SMASH URL Data Handler" ) == 0,
                     "23e: default data handler name is 'L-SMASH URL Data Handler'" );

        /* Set >255-byte media_handler_name and data_handler_name in QTFF mode (255-byte buffer: 1 length + 254 chars) */
        char long_handler[301];
        memset( long_handler, 'Q', 300 );
        long_handler[300] = '\0';
        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale          = 48000;
        media_param.media_handler_name = long_handler;
        media_param.data_handler_name  = long_handler;
        TEST_ASSERT( lsmash_set_media_parameters( root, a_track, &media_param ) == 0,
                     "23e: set >255-byte QT handler names safely without heap overflow" );

        lsmash_media_parameters_t read_media_param;
        lsmash_initialize_media_parameters( &read_media_param );
        TEST_ASSERT( lsmash_get_media_parameters( root, a_track, &read_media_param ) == 0,
                     "23e: get truncated QT handler names" );
        TEST_ASSERT( read_media_param.media_handler_name != NULL
                  && strlen( read_media_param.media_handler_name ) == 254,
                     "23e: QT media_handler_name safely clamped to 254 payload bytes" );
        TEST_ASSERT( read_media_param.data_handler_name != NULL
                  && strlen( read_media_param.data_handler_name ) == 254,
                     "23e: QT data_handler_name safely clamped to 254 payload bytes" );

        /* Add 16-bit 2ch stereo LPCM sample entry (4 bytes per frame) */
        lsmash_audio_summary_t *a_sum = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
        TEST_ASSERT( a_sum != NULL, "23e: create audio summary" );
        a_sum->sample_type      = QT_CODEC_TYPE_LPCM_AUDIO;
        a_sum->frequency        = 48000;
        a_sum->channels         = 2;
        a_sum->sample_size      = 16;
        a_sum->samples_in_frame = 1;
        a_sum->bytes_per_frame  = 4;
        lsmash_codec_specific_t *spec = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_QT_AUDIO_FORMAT_SPECIFIC_FLAGS,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        TEST_ASSERT( spec != NULL, "23e: create qt audio format flags" );
        lsmash_qt_audio_format_specific_flags_t *flags = (lsmash_qt_audio_format_specific_flags_t *)spec->data.structured;
        flags->format_flags = QT_AUDIO_FORMAT_FLAG_SIGNED_INTEGER | QT_AUDIO_FORMAT_FLAG_PACKED;
        TEST_ASSERT( lsmash_add_codec_specific_data( (lsmash_summary_t *)a_sum, spec ) == 0, "23e: add qt audio flags" );
        lsmash_destroy_codec_specific_data( spec );
        uint32_t a_idx = lsmash_add_sample_entry( root, a_track, a_sum );
        lsmash_cleanup_summary( (lsmash_summary_t *)a_sum );
        TEST_ASSERT( a_idx == 1, "23e: add QT LPCM sample entry" );

        /* Fractional LPCM sample length (6 bytes, not divisible by frame_size=4) must be rejected */
        lsmash_sample_t *bad_pcm = lsmash_create_sample( 6 );
        TEST_ASSERT( bad_pcm != NULL, "23e: create fractional LPCM sample" );
        memset( bad_pcm->data, 0, 6 );
        bad_pcm->index = a_idx;
        bad_pcm->dts   = 0;
        bad_pcm->cts   = 0;
        TEST_ASSERT( lsmash_append_sample( root, a_track, bad_pcm ) == LSMASH_ERR_INVALID_DATA,
                     "23e: reject LPCM sample whose length is not a multiple of frame_size" );

        /* Zero frame_size in corrupted audio entry must also be rejected without infinite loop */
        isom_trak_t *trak_box = (isom_trak_t *)file->moov->trak_list.head->data;
        isom_audio_entry_t *aud_entry = (isom_audio_entry_t *)trak_box->mdia->minf->stbl->stsd->list.head->data;
        uint32_t saved_cbp = aud_entry->constBytesPerAudioPacket;
        aud_entry->constBytesPerAudioPacket = 0;
        bad_pcm->length                     = 4;
        TEST_ASSERT( lsmash_append_sample( root, a_track, bad_pcm ) == LSMASH_ERR_INVALID_DATA,
                     "23e: reject LPCM sample when frame_size == 0" );
        aud_entry->constBytesPerAudioPacket = saved_cbp;
        lsmash_delete_sample( bad_pcm );

        /* Append 2 valid LPCM samples (8 bytes = 2 frames) */
        lsmash_sample_t *valid_pcm = lsmash_create_sample( 8 );
        TEST_ASSERT( valid_pcm != NULL, "23e: create valid LPCM packet" );
        memset( valid_pcm->data, 0, 8 );
        valid_pcm->index = a_idx;
        valid_pcm->dts   = 0;
        valid_pcm->cts   = 0;
        TEST_ASSERT( lsmash_append_sample( root, a_track, valid_pcm ) == 0, "23e: append valid LPCM packet" );
        TEST_ASSERT( lsmash_flush_pooled_samples( root, a_track, 1 ) == 0, "23e: flush pooled LPCM samples" );
        /* Verify lsmash_set_last_sample_delta rejects sample_delta < stts_data->sample_delta on QT v1 fixed-compression audio */
        int16_t  saved_ver = aud_entry->version;
        int16_t  saved_cid = aud_entry->compression_ID;
        uint32_t saved_spp = aud_entry->samplesPerPacket;
        aud_entry->version          = 1;
        aud_entry->compression_ID   = QT_AUDIO_COMPRESSION_ID_NOT_COMPRESSED;
        aud_entry->samplesPerPacket = 2;
        TEST_ASSERT( lsmash_set_last_sample_delta( root, a_track, 0 ) == LSMASH_ERR_INVALID_DATA,
                     "23e: reject last_sample_delta smaller than preceding stts delta on QT v1 fixed-compression audio" );
        aud_entry->version          = saved_ver;
        aud_entry->compression_ID   = saved_cid;
        aud_entry->samplesPerPacket = saved_spp;
        TEST_ASSERT( lsmash_finish_movie( root, NULL ) == 0, "23e: finish QT movie" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );

        /* Create ISOBMFF movie with Video track (roll_grouping) to test GDR pre-roll without preceding RAP */
        root = lsmash_create_root();
        memset( &param, 0, sizeof(param) );
        TEST_ASSERT( lsmash_open_file( mp4_path, 0, &param ) == 0, "23e: open mp4_path for write" );
        lsmash_brand_type iso_brands[4] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_ISO6, ISOM_BRAND_TYPE_AVC1, ISOM_BRAND_TYPE_AV01 };
        param.major_brand   = ISOM_BRAND_TYPE_ISOM;
        param.brands        = iso_brands;
        param.brand_count   = 4;
        file = lsmash_set_file( root, &param );
        TEST_ASSERT( file != NULL, "23e: set mp4 file" );
        TEST_ASSERT( lsmash_set_movie_parameters( root, &movie_param ) == 0, "23e: set mp4 movie params" );

        uint32_t v_track = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        TEST_ASSERT( v_track != 0, "23e: create video track" );
        TEST_ASSERT( lsmash_set_track_parameters( root, v_track, &track_param ) == 0, "23e: set video track params" );
        lsmash_media_parameters_t v_media_param;
        lsmash_initialize_media_parameters( &v_media_param );
        v_media_param.timescale     = 1000;
        v_media_param.roll_grouping = 1;
        TEST_ASSERT( lsmash_set_media_parameters( root, v_track, &v_media_param ) == 0, "23e: set video media params" );
        lsmash_video_summary_t *v_sum = create_synth_av01_summary( 64, 64 );
        uint32_t v_idx = lsmash_add_sample_entry( root, v_track, v_sum );
        lsmash_cleanup_summary( (lsmash_summary_t *)v_sum );
        TEST_ASSERT( v_idx == 1, "23e: add video sample entry" );

        for( int i = 0; i < 4; i++ )
        {
            lsmash_sample_t *s = lsmash_create_sample( 8 );
            TEST_ASSERT( s != NULL, "23e: create video sample" );
            memset( s->data, i, 8 );
            s->index = v_idx;
            s->dts   = (uint64_t)i * 10;
            s->cts   = (uint64_t)i * 10;
            if( i == 2 )
            {
                s->prop.ra_flags          = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_PRE_ROLL_END;
                s->prop.pre_roll.distance = 2;
            }
            else if( i == 3 )
                s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            else
                s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
            TEST_ASSERT( lsmash_append_sample( root, v_track, s ) == 0, "23e: append video sample" );
        }
        TEST_ASSERT( lsmash_flush_pooled_samples( root, v_track, 10 ) == 0, "23e: flush pooled video samples" );
        TEST_ASSERT( lsmash_finish_movie( root, NULL ) == 0, "23e: finish mp4 movie" );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );

        /* Re-open mp4 file, construct timeline, and test GDR pre-roll & lsmash_set_media_timestamps */
        root = lsmash_create_root();
        memset( &param, 0, sizeof(param) );
        TEST_ASSERT( lsmash_open_file( mp4_path, 1, &param ) == 0, "23e: reopen mp4_path for read" );
        file = lsmash_set_file( root, &param );
        TEST_ASSERT( lsmash_read_file( file, &param ) >= 0, "23e: read mp4_path" );
        TEST_ASSERT( lsmash_construct_timeline( root, v_track ) == 0, "23e: construct video timeline" );

        uint32_t rap_number = 0;
        lsmash_random_access_flag ra_flags = 0;
        uint32_t leading  = 0;
        uint32_t distance = 0;
        TEST_ASSERT( lsmash_get_closest_random_accessible_point_detail_from_media_timeline(
                         root, v_track, 3, &rap_number, &ra_flags, &leading, &distance ) == 0,
                     "23e: GDR pre-roll lookup without preceding RAP returns cleanly without infinite loop" );
        TEST_ASSERT( rap_number == 3 && distance == 2 && (ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_GDR),
                     "23e: GDR pre-roll rap_number == 3 and distance == 2" );

        /* Verify lsmash_set_media_timestamps NULL check and atomic pre-validation */
        lsmash_media_ts_list_t null_ts_list = { .sample_count = 4, .timestamp = NULL };
        TEST_ASSERT( lsmash_set_media_timestamps( root, v_track, &null_ts_list ) == LSMASH_ERR_FUNCTION_PARAM,
                     "23e: lsmash_set_media_timestamps rejects NULL ts" );
        lsmash_media_ts_list_t ts_list = { 0 };
        TEST_ASSERT( lsmash_get_media_timestamps( root, v_track, &ts_list ) == 0 && ts_list.sample_count == 4,
                     "23e: get media timestamps" );
        /* Mutate sample 0 valid, sample 2 invalid (cts - dts > INT32_MAX) */
        ts_list.timestamp[0].dts = 0;
        ts_list.timestamp[0].cts = 100;
        ts_list.timestamp[1].dts = 50;
        ts_list.timestamp[1].cts = 50;
        ts_list.timestamp[2].dts = 100;
        ts_list.timestamp[2].cts = 100ULL + (uint64_t)INT32_MAX + 10ULL;
        ts_list.timestamp[3].dts = 150;
        ts_list.timestamp[3].cts = 150;
        TEST_ASSERT( lsmash_set_media_timestamps( root, v_track, &ts_list ) == LSMASH_ERR_INVALID_DATA,
                     "23e: lsmash_set_media_timestamps rejects cts - dts > INT32_MAX" );
        uint64_t check_dts = 999, check_cts = 999;
        TEST_ASSERT( lsmash_get_dts_from_media_timeline( root, v_track, 2, &check_dts ) == 0
                  && lsmash_get_cts_from_media_timeline( root, v_track, 1, &check_cts ) == 0
                  && check_dts == 10 && check_cts == 0,
                     "23e: failed lsmash_set_media_timestamps preserves original timeline state" );
        lsmash_delete_media_timestamps( &ts_list );

        lsmash_destruct_timeline( root, v_track );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
    }

    /*-----------------------------------------------------------------------
     * 23f. Regression tests for media segment compatibility flag inheritance,
     *      lsmash_set_copyright / lsmash_set_sdp sentinel & duplicate guards,
     *      and duplicate single-child Box (ctab / iods) stream resilience
     *-----------------------------------------------------------------------*/
    {
        /* 1) Media segment compatibility flags inheritance (brand_count == 0) */
        const char *compat_init_path = "tests/tmp_test_compat_init.mp4";
        const char *compat_seg_path  = "tests/tmp_test_compat_seg.m4s";
        const lsmash_brand_type init_brands[3] = {
            ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_ISO6, ISOM_BRAND_TYPE_MP42
        };
        lsmash_root_t *seg_root = lsmash_create_root();
        TEST_ASSERT( seg_root != NULL, "23f: create root for segment compat test" );

        lsmash_file_parameters_t init_param = { 0 };
        TEST_ASSERT( lsmash_open_file( compat_init_path, 0, &init_param ) == 0, "23f: open compat init segment" );
        init_param.mode &= ~LSMASH_FILE_MODE_MEDIA;
        init_param.mode |= LSMASH_FILE_MODE_FRAGMENTED | LSMASH_FILE_MODE_SEGMENT;
        init_param.major_brand = ISOM_BRAND_TYPE_ISOM;
        init_param.brands      = (lsmash_brand_type *)init_brands;
        init_param.brand_count = 3;
        lsmash_file_t *init_file = lsmash_set_file( seg_root, &init_param );
        TEST_ASSERT( init_file != NULL, "23f: set compat init segment" );
        TEST_ASSERT( init_file->qt_compatible == 0
                  && init_file->isom_compatible == 1
                  && init_file->avc_extensions == 1
                  && init_file->mp4_version1 == 0
                  && init_file->mp4_version2 == 1
                  && init_file->itunes_movie == 0
                  && init_file->max_3gpp_version == 0
                  && init_file->max_isom_version == 6
                  && init_file->min_isom_version == 1
                  && init_file->forbid_tref == 0
                  && init_file->undefined_64_ver == 0,
                     "23f: init_file compatibility flags established accurately" );

        uint32_t seg_tid = seg_create_video_track( seg_root );
        TEST_ASSERT( seg_tid != 0, "23f: create video track in compat init segment" );

        lsmash_file_parameters_t media_seg_param = { 0 };
        TEST_ASSERT( lsmash_open_file( compat_seg_path, 0, &media_seg_param ) == 0, "23f: open compat media segment" );
        media_seg_param.mode &= ~LSMASH_FILE_MODE_INITIALIZATION;
        media_seg_param.mode |= LSMASH_FILE_MODE_FRAGMENTED | LSMASH_FILE_MODE_SEGMENT;
        media_seg_param.major_brand = 0;
        media_seg_param.brands      = NULL;
        media_seg_param.brand_count = 0;
        lsmash_file_t *media_seg_file = lsmash_set_file( seg_root, &media_seg_param );
        TEST_ASSERT( media_seg_file != NULL, "23f: set brandless compat media segment" );

        lsmash_adhoc_remux_t seg_remux = { .buffer_size = 65536 };
        TEST_ASSERT( lsmash_switch_media_segment( seg_root, media_seg_file, &seg_remux ) == 0,
                     "23f: switch to brandless media segment inherits initializer compat flags" );
        TEST_ASSERT( media_seg_file->qt_compatible == 0
                  && media_seg_file->isom_compatible == 1
                  && media_seg_file->avc_extensions == 1
                  && media_seg_file->mp4_version1 == 0
                  && media_seg_file->mp4_version2 == 1
                  && media_seg_file->itunes_movie == 0
                  && media_seg_file->max_3gpp_version == 0
                  && media_seg_file->max_isom_version == 6
                  && media_seg_file->min_isom_version == 5
                  && media_seg_file->forbid_tref == 0
                  && media_seg_file->undefined_64_ver == 0
                  && media_seg_file->allow_moof_base == 1
                  && media_seg_file->media_segment == 1,
                     "23f: media segment inherits exact initializer compat flags without pointer-byte corruption" );
        TEST_ASSERT( seg_write_fragment( seg_root, seg_tid, 0 ) == 0, "23f: write media segment fragment" );
        TEST_ASSERT( lsmash_finish_movie( seg_root, &seg_remux ) == 0, "23f: finish brandless media segment" );
        lsmash_close_file( &init_param );
        lsmash_close_file( &media_seg_param );
        lsmash_destroy_root( seg_root );
        remove( compat_init_path );
        remove( compat_seg_path );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "23f: 0 bytes leaked after segment compat flag inheritance test" );
#endif

        /* 2) lsmash_set_copyright & lsmash_set_sdp boundary and sentinel safety */
        const char *cprt_path = "tests/tmp_test_cprt_boundary.mp4";
        char notice_bad_track[] = "Invalid Track Copyright";
        char notice_movie[]     = "Movie Copyright 2026";
        char notice_track[]     = "Track Copyright 2026";
        char sdp_movie[]        = "v=0\r\ns=MovieSession\r\n";
        char sdp_track[]        = "m=video 0 RTP/AVP 96\r\n";

        lsmash_root_t *cprt_root = lsmash_create_root();
        TEST_ASSERT( cprt_root != NULL, "23f: create root for cprt test" );
        lsmash_file_parameters_t cprt_param = { 0 };
        TEST_ASSERT( lsmash_open_file( cprt_path, 0, &cprt_param ) == 0, "23f: open cprt_path" );
        lsmash_brand_type mp42_brands[2] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_MP42 };
        cprt_param.major_brand = ISOM_BRAND_TYPE_MP42;
        cprt_param.brands      = mp42_brands;
        cprt_param.brand_count = 2;
        lsmash_file_t *cprt_file = lsmash_set_file( cprt_root, &cprt_param );
        TEST_ASSERT( cprt_file != NULL, "23f: set cprt_file" );

        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        TEST_ASSERT( lsmash_set_movie_parameters( cprt_root, &movie_param ) == 0, "23f: set cprt movie params" );

        uint32_t cprt_tid = lsmash_create_track( cprt_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        TEST_ASSERT( cprt_tid != 0, "23f: create video track for cprt test" );
        lsmash_track_parameters_t track_param;
        lsmash_initialize_track_parameters( &track_param );
        track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        TEST_ASSERT( lsmash_set_track_parameters( cprt_root, cprt_tid, &track_param ) == 0, "23f: set cprt track params" );
        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale = 1000;
        TEST_ASSERT( lsmash_set_media_parameters( cprt_root, cprt_tid, &media_param ) == 0, "23f: set cprt media params" );
        lsmash_video_summary_t *v_sum = create_synth_av01_summary( 64, 64 );
        uint32_t v_idx = lsmash_add_sample_entry( cprt_root, cprt_tid, v_sum );
        lsmash_cleanup_summary( (lsmash_summary_t *)v_sum );
        TEST_ASSERT( v_idx == 1, "23f: add sample entry for cprt test" );

        /* Non-existent track_ID must fail cleanly without polluting the static non-existing trak sentinel */
        TEST_ASSERT( lsmash_set_copyright( cprt_root, 9999, ISOM_LANGUAGE_CODE_ENGLISH, notice_bad_track ) < 0,
                     "23f: lsmash_set_copyright rejects non-existent track_ID=9999" );
        TEST_ASSERT( LSMASH_IS_NON_EXISTING_BOX( lsm_box_default_non_existing_trak()->udta ),
                     "23f: static non-existing trak sentinel udta pointer remains unpolluted" );
        TEST_ASSERT( lsmash_set_copyright( cprt_root, 0, ISOM_LANGUAGE_CODE_ENGLISH, NULL ) == LSMASH_ERR_FUNCTION_PARAM,
                     "23f: lsmash_set_copyright rejects NULL notice" );
        TEST_ASSERT( lsmash_set_copyright( cprt_root, 0, 0x0001, notice_movie ) == LSMASH_ERR_FUNCTION_PARAM,
                     "23f: lsmash_set_copyright rejects invalid ISO_language < 0x800" );

        /* Valid Movie and Track cprt setup + duplicate language rejection */
        TEST_ASSERT( lsmash_set_copyright( cprt_root, 0, ISOM_LANGUAGE_CODE_ENGLISH, notice_movie ) == 0,
                     "23f: set movie-level cprt" );
        TEST_ASSERT( lsmash_set_copyright( cprt_root, 0, ISOM_LANGUAGE_CODE_ENGLISH, notice_movie ) < 0,
                     "23f: reject duplicate language movie-level cprt" );
        TEST_ASSERT( lsmash_set_copyright( cprt_root, cprt_tid, ISOM_LANGUAGE_CODE_JAPANESE, notice_track ) == 0,
                     "23f: set track-level cprt" );
        TEST_ASSERT( lsmash_set_copyright( cprt_root, cprt_tid, ISOM_LANGUAGE_CODE_JAPANESE, notice_track ) < 0,
                     "23f: reject duplicate language track-level cprt" );

        /* Also verify lsmash_set_sdp sentinel and duplicate sdp/rtp rejection */
        TEST_ASSERT( lsmash_set_sdp( cprt_root, 9999, sdp_track ) < 0,
                     "23f: lsmash_set_sdp rejects non-existent track_ID=9999" );
        TEST_ASSERT( lsmash_set_sdp( cprt_root, 0, sdp_movie ) == 0,
                     "23f: set movie-level sdp (rtp)" );
        TEST_ASSERT( lsmash_set_sdp( cprt_root, 0, sdp_movie ) < 0,
                     "23f: reject duplicate movie-level sdp (rtp) without leak" );
        TEST_ASSERT( lsmash_set_sdp( cprt_root, cprt_tid, sdp_track ) == 0,
                     "23f: set track-level sdp" );
        TEST_ASSERT( lsmash_set_sdp( cprt_root, cprt_tid, sdp_track ) < 0,
                     "23f: reject duplicate track-level sdp without leak" );

        lsmash_sample_t *s = lsmash_create_sample( 8 );
        TEST_ASSERT( s != NULL, "23f: create sample for cprt movie" );
        memset( s->data, 0x42, 8 );
        s->index = v_idx;
        s->dts   = 0;
        s->cts   = 0;
        s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
        TEST_ASSERT( lsmash_append_sample( cprt_root, cprt_tid, s ) == 0, "23f: append sample" );
        TEST_ASSERT( lsmash_flush_pooled_samples( cprt_root, cprt_tid, 40 ) == 0, "23f: flush samples" );
        TEST_ASSERT( lsmash_finish_movie( cprt_root, NULL ) == 0, "23f: finish cprt movie" );
        lsmash_close_file( &cprt_param );
        lsmash_destroy_root( cprt_root );

        /* Read back and verify both Movie and Track cprt boxes */
        cprt_root = lsmash_create_root();
        memset( &cprt_param, 0, sizeof(cprt_param) );
        TEST_ASSERT( lsmash_open_file( cprt_path, 1, &cprt_param ) == 0, "23f: reopen cprt_path" );
        cprt_file = lsmash_set_file( cprt_root, &cprt_param );
        TEST_ASSERT( lsmash_read_file( cprt_file, &cprt_param ) >= 0, "23f: read cprt_path" );
        TEST_ASSERT( LSMASH_IS_EXISTING_BOX( cprt_file->moov->udta )
                  && cprt_file->moov->udta->cprt_list.entry_count == 1,
                     "23f: movie udta.cprt persisted" );
        isom_cprt_t *rd_movie_cprt = (isom_cprt_t *)cprt_file->moov->udta->cprt_list.head->data;
        TEST_ASSERT( rd_movie_cprt != NULL
                  && rd_movie_cprt->language == ISOM_LANGUAGE_CODE_ENGLISH
                  && rd_movie_cprt->notice != NULL
                  && strcmp( (const char *)rd_movie_cprt->notice, notice_movie ) == 0,
                     "23f: movie cprt language and notice match" );
        isom_trak_t *rd_trak = (isom_trak_t *)cprt_file->moov->trak_list.head->data;
        TEST_ASSERT( LSMASH_IS_EXISTING_BOX( rd_trak )
                  && LSMASH_IS_EXISTING_BOX( rd_trak->udta )
                  && rd_trak->udta->cprt_list.entry_count == 1,
                     "23f: track udta.cprt persisted" );
        isom_cprt_t *rd_track_cprt = (isom_cprt_t *)rd_trak->udta->cprt_list.head->data;
        TEST_ASSERT( rd_track_cprt != NULL
                  && rd_track_cprt->language == ISOM_LANGUAGE_CODE_JAPANESE
                  && rd_track_cprt->notice != NULL
                  && strcmp( (const char *)rd_track_cprt->notice, notice_track ) == 0,
                     "23f: track cprt language and notice match" );
        lsmash_close_file( &cprt_param );
        lsmash_destroy_root( cprt_root );
        remove( cprt_path );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "23f: 0 bytes leaked after cprt/sdp boundary and roundtrip test" );
#endif

        /* 3) Duplicate single-child Box (iods / ctab) stream resilience & 0-leak verification */
        const char *dup_box_path  = "tests/tmp_test_dup_ctab_iods.mp4";
        const char *dup_dump_path = "tests/tmp_test_dup_ctab_iods_dump.txt";
        lsmash_bs_t *bs = lsmash_bs_create();
        TEST_ASSERT( bs != NULL, "23f: create bs for duplicate ctab/iods stream" );

        /* ftyp (16 bytes) */
        lsmash_bs_put_be32( bs, 16 );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'f', 't', 'y', 'p' ) );
        lsmash_bs_put_be32( bs, ISOM_BRAND_TYPE_MP42 );
        lsmash_bs_put_be32( bs, 0 );

        uint32_t mvhd_sz  = 108;
        uint32_t iods1_sz = 21; /* 12 (FullBox) + 2 (tag=0x10, len=7) + 2 (OD_ID) + 5 (PLIs) */
        uint32_t iods2_sz = 21;
        uint32_t ctab1_sz = 16 + 2 * 8; /* 2 color entries */
        uint32_t ctab2_sz = 16 + 3 * 8; /* 3 color entries */
        uint32_t moov_sz  = 8 + mvhd_sz + iods1_sz + iods2_sz + ctab1_sz + ctab2_sz;

        lsmash_bs_put_be32( bs, moov_sz );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'o', 'o', 'v' ) );

        /* mvhd v0 (108 bytes) */
        lsmash_bs_put_be32( bs, mvhd_sz );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'm', 'v', 'h', 'd' ) );
        lsmash_bs_put_be32( bs, 0 );          /* version + flags */
        lsmash_bs_put_be32( bs, 0 );          /* creation_time */
        lsmash_bs_put_be32( bs, 0 );          /* modification_time */
        lsmash_bs_put_be32( bs, 600 );        /* timescale */
        lsmash_bs_put_be32( bs, 600 );        /* duration */
        lsmash_bs_put_be32( bs, 0x00010000 ); /* rate */
        lsmash_bs_put_be16( bs, 0x0100 );     /* volume */
        lsmash_bs_put_be16( bs, 0 );          /* reserved */
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be32( bs, 0x00010000 );
        for( int i = 1; i < 9; i++ )
            lsmash_bs_put_be32( bs, i == 4 ? 0x00010000 : (i == 8 ? 0x40000000 : 0) );
        for( int i = 0; i < 6; i++ )
            lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be32( bs, 1 );          /* next_track_ID */

        /* 1st iods (canonical) */
        lsmash_bs_put_be32( bs, iods1_sz );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'i', 'o', 'd', 's' ) );
        lsmash_bs_put_be32( bs, 0 );          /* version + flags */
        lsmash_bs_put_byte( bs, 0x10 );       /* MP4_IOD_Tag */
        lsmash_bs_put_byte( bs, 7 );          /* length = 7 */
        lsmash_bs_put_be16( bs, 0x004F );     /* ObjectDescriptorID=1, URL_Flag=0, includeInlineProfileLevelFlag=0, reserved=0xF */
        lsmash_bs_put_byte( bs, 0xFF );       /* OD_PLI */
        lsmash_bs_put_byte( bs, 0xFF );       /* scene_PLI */
        lsmash_bs_put_byte( bs, 0x29 );       /* audio_PLI */
        lsmash_bs_put_byte( bs, 0x15 );       /* visual_PLI */
        lsmash_bs_put_byte( bs, 0xFF );       /* graphics_PLI */

        /* 2nd iods (duplicate -> must fall back to unknown_box without overwriting moov->iods or leaking OD) */
        lsmash_bs_put_be32( bs, iods2_sz );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'i', 'o', 'd', 's' ) );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_byte( bs, 0x10 );
        lsmash_bs_put_byte( bs, 7 );
        lsmash_bs_put_be16( bs, 0x008F );
        lsmash_bs_put_byte( bs, 0x01 );
        lsmash_bs_put_byte( bs, 0x02 );
        lsmash_bs_put_byte( bs, 0x03 );
        lsmash_bs_put_byte( bs, 0x04 );
        lsmash_bs_put_byte( bs, 0x05 );

        /* 1st ctab (canonical: 2 entries, entry[1].r == 0x1234) */
        lsmash_bs_put_be32( bs, ctab1_sz );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'c', 't', 'a', 'b' ) );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be16( bs, 0x8000 );
        lsmash_bs_put_be16( bs, 1 ); /* size = 1 -> 2 entries */
        lsmash_bs_put_be64( bs, 0x0000111122223333ULL );
        lsmash_bs_put_be64( bs, 0x0001123456789ABCULL );

        /* 2nd ctab (duplicate -> must fall back to unknown_box without leaking color_table.array) */
        lsmash_bs_put_be32( bs, ctab2_sz );
        lsmash_bs_put_be32( bs, LSMASH_4CC( 'c', 't', 'a', 'b' ) );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be16( bs, 0x8000 );
        lsmash_bs_put_be16( bs, 2 ); /* size = 2 -> 3 entries */
        lsmash_bs_put_be64( bs, 0x0000AAAA00000000ULL );
        lsmash_bs_put_be64( bs, 0x0001BBBB00000000ULL );
        lsmash_bs_put_be64( bs, 0x0002CCCC00000000ULL );

        uint32_t dup_raw_len = 0;
        uint8_t *dup_raw = lsmash_bs_export_data( bs, &dup_raw_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( write_test_file( dup_box_path, dup_raw, dup_raw_len ) == 0,
                     "23f: write duplicate ctab/iods stream" );
        lsmash_free( dup_raw );

        lsmash_root_t *dup_root = lsmash_create_root();
        TEST_ASSERT( dup_root != NULL, "23f: create root for duplicate ctab/iods stream" );
        lsmash_file_parameters_t dup_param = { 0 };
        TEST_ASSERT( lsmash_open_file( dup_box_path, 1, &dup_param ) == 0, "23f: open dup_box_path" );
        dup_param.mode |= LSMASH_FILE_MODE_DUMP;
        lsmash_file_t *dup_file = lsmash_set_file( dup_root, &dup_param );
        TEST_ASSERT( dup_file != NULL, "23f: set dup_file" );
        /* Note: moov has no trak_list so lsm_importer_find may report invalid mandatory boxes,
         * while read_children has parsed moov's children cleanly into dup_file->moov. */
        (void)lsmash_read_file( dup_file, &dup_param );
        dup_file->root->file  = dup_file;
        dup_file->initializer = dup_file;
        TEST_ASSERT( LSMASH_IS_EXISTING_BOX( dup_file->moov )
                  && LSMASH_IS_EXISTING_BOX( dup_file->moov->iods )
                  && dup_file->moov->iods->OD != NULL,
                     "23f: first moov.iods retained intact when duplicate iods is present" );
        TEST_ASSERT( LSMASH_IS_EXISTING_BOX( dup_file->moov->ctab )
                  && dup_file->moov->ctab->color_table.size == 1
                  && dup_file->moov->ctab->color_table.array != NULL
                  && dup_file->moov->ctab->color_table.array[1].r == 0x1234,
                     "23f: first moov.ctab retained intact when duplicate ctab is present" );
        TEST_ASSERT( lsmash_print_movie( dup_root, dup_dump_path ) == 0,
                     "23f: print_movie dumps canonical and unknown-fallback duplicate ctab/iods cleanly" );
        remove( dup_dump_path );
        lsmash_close_file( &dup_param );
        lsmash_destroy_root( dup_root );
        remove( dup_box_path );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "23f: 0 bytes leaked after reading and destroying duplicate ctab/iods stream" );
#endif
    }

    /*-----------------------------------------------------------------------
     * 23g. Regression tests for summary compressorname symmetry/bounds,
     *      chapter parsing & UTF-8 BOM truncation bounds, corrupted
     *      compressorname/chan/ftab printing, and iTunes metadata string copy
     *-----------------------------------------------------------------------*/
    {
        /* 1) lsmash_compare_summary(): symmetric comparison & non-NUL 33-byte safety */
        lsmash_video_summary_t *sum_a = (lsmash_video_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_VIDEO );
        lsmash_video_summary_t *sum_b = (lsmash_video_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_VIDEO );
        TEST_ASSERT( sum_a != NULL && sum_b != NULL, "23g: create video summaries" );
        sum_a->sample_type = sum_b->sample_type = ISOM_CODEC_TYPE_AVC1_VIDEO;
        sum_a->width       = sum_b->width       = 1920;
        sum_a->height      = sum_b->height      = 1080;
        sum_a->depth       = sum_b->depth       = 24;
        sum_a->par_h       = sum_b->par_h       = 1;
        sum_a->par_v       = sum_b->par_v       = 1;

        /* Empty vs non-empty must differ in both argument orders */
        sum_a->compressorname[0] = '\0';
        memcpy( sum_b->compressorname, "\012AVC Coding", 12 );
        TEST_ASSERT( lsmash_compare_summary( (lsmash_summary_t *)sum_a, (lsmash_summary_t *)sum_b ) != 0,
                     "23g: empty vs non-empty compressorname differs (a, b)" );
        TEST_ASSERT( lsmash_compare_summary( (lsmash_summary_t *)sum_b, (lsmash_summary_t *)sum_a ) != 0,
                     "23g: empty vs non-empty compressorname differs (b, a)" );

        /* Prefix vs full name must differ in both argument orders */
        memset( sum_a->compressorname, 0, sizeof(sum_a->compressorname) );
        memset( sum_b->compressorname, 0, sizeof(sum_b->compressorname) );
        memcpy( sum_a->compressorname, "AVC", 4 );
        memcpy( sum_b->compressorname, "AVC Coding", 11 );
        TEST_ASSERT( lsmash_compare_summary( (lsmash_summary_t *)sum_a, (lsmash_summary_t *)sum_b ) != 0,
                     "23g: prefix 'AVC' vs 'AVC Coding' differs (a, b)" );
        TEST_ASSERT( lsmash_compare_summary( (lsmash_summary_t *)sum_b, (lsmash_summary_t *)sum_a ) != 0,
                     "23g: prefix 'AVC' vs 'AVC Coding' differs (b, a)" );

        /* Pascal prefix vs full Pascal name must also differ symmetrically */
        memset( sum_a->compressorname, 0, sizeof(sum_a->compressorname) );
        memset( sum_b->compressorname, 0, sizeof(sum_b->compressorname) );
        memcpy( sum_a->compressorname, "\003AVC", 5 );
        memcpy( sum_b->compressorname, "\012AVC Coding", 12 );
        TEST_ASSERT( lsmash_compare_summary( (lsmash_summary_t *)sum_a, (lsmash_summary_t *)sum_b ) != 0
                  && lsmash_compare_summary( (lsmash_summary_t *)sum_b, (lsmash_summary_t *)sum_a ) != 0,
                     "23g: Pascal prefix vs full compressorname differs symmetrically" );

        /* Identical names match */
        memcpy( sum_a->compressorname, "\012AVC Coding", 12 );
        TEST_ASSERT( lsmash_compare_summary( (lsmash_summary_t *)sum_a, (lsmash_summary_t *)sum_b ) == 0,
                     "23g: identical compressorname matches" );

        /* Unterminated 33-byte buffers filled with non-NUL bytes must not OOB read */
        memset( sum_a->compressorname, 'X', sizeof(sum_a->compressorname) );
        memset( sum_b->compressorname, 'X', sizeof(sum_b->compressorname) );
        TEST_ASSERT( lsmash_compare_summary( (lsmash_summary_t *)sum_a, (lsmash_summary_t *)sum_b ) == 0,
                     "23g: identical 33-byte non-NUL compressorname compares equal without OOB" );
        sum_b->compressorname[31] = 'Y';
        TEST_ASSERT( lsmash_compare_summary( (lsmash_summary_t *)sum_a, (lsmash_summary_t *)sum_b ) != 0
                  && lsmash_compare_summary( (lsmash_summary_t *)sum_b, (lsmash_summary_t *)sum_a ) != 0,
                     "23g: non-NUL 32nd byte difference detected safely" );
        sum_b->compressorname[31] = '\0';
        TEST_ASSERT( lsmash_compare_summary( (lsmash_summary_t *)sum_a, (lsmash_summary_t *)sum_b ) != 0
                  && lsmash_compare_summary( (lsmash_summary_t *)sum_b, (lsmash_summary_t *)sum_a ) != 0,
                     "23g: 31-byte NUL-terminated vs 33-byte non-NUL differs safely" );

        lsmash_cleanup_summary( (lsmash_summary_t *)sum_a );
        lsmash_cleanup_summary( (lsmash_summary_t *)sum_b );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "23g: 0 bytes leaked after summary compressorname tests" );
#endif

        /* 2) Chapter parsing: NUL-prefixed lines, missing '=' / ' ' separators,
         *    and add_bom = 1 UTF-8 boundary truncation preventing uint8_t wraparound */
        const char *chap_edge_mp4  = "tests/tmp_test_chap_edge.mp4";
        const char *chap_edge_txt  = "tests/tmp_test_chap_edge.txt";
        const char *chap_edge_dump = "tests/tmp_test_chap_edge_dump.txt";

        lsmash_root_t *c_root = lsmash_create_root();
        lsmash_file_parameters_t c_param = { 0 };
        TEST_ASSERT( lsmash_open_file( chap_edge_mp4, 0, &c_param ) == 0, "23g: open chap_edge_mp4" );
        lsmash_brand_type mp42_brands[3] = { ISOM_BRAND_TYPE_M4V, ISOM_BRAND_TYPE_MP42, ISOM_BRAND_TYPE_ISOM };
        c_param.major_brand = ISOM_BRAND_TYPE_M4V;
        c_param.brands      = mp42_brands;
        c_param.brand_count = 3;
        TEST_ASSERT( lsmash_set_file( c_root, &c_param ) != NULL, "23g: set chap_edge file" );
        lsmash_movie_parameters_t c_mparam;
        lsmash_initialize_movie_parameters( &c_mparam );
        c_mparam.timescale = 1000;
        TEST_ASSERT( lsmash_set_movie_parameters( c_root, &c_mparam ) == 0, "23g: set chap_edge movie params" );
        uint32_t c_tid = lsmash_create_track( c_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        lsmash_track_parameters_t c_tp;
        lsmash_initialize_track_parameters( &c_tp );
        c_tp.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        TEST_ASSERT( lsmash_set_track_parameters( c_root, c_tid, &c_tp ) == 0, "23g: set chap_edge track params" );
        lsmash_media_parameters_t c_mp;
        lsmash_initialize_media_parameters( &c_mp );
        c_mp.timescale = 1000;
        TEST_ASSERT( lsmash_set_media_parameters( c_root, c_tid, &c_mp ) == 0, "23g: set chap_edge media params" );
        lsmash_video_summary_t *c_vsum = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( lsmash_add_sample_entry( c_root, c_tid, c_vsum ) == 1, "23g: add sample entry for chap_edge" );
        lsmash_cleanup_summary( (lsmash_summary_t *)c_vsum );
        lsmash_sample_t *c_s = lsmash_create_sample( 8 );
        memset( c_s->data, 0x33, 8 );
        c_s->dts = 0; c_s->cts = 0; c_s->index = 1;
        c_s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
        TEST_ASSERT( lsmash_append_sample( c_root, c_tid, c_s ) == 0, "23g: append chap_edge sample" );
        TEST_ASSERT( lsmash_flush_pooled_samples( c_root, c_tid, 10000 ) == 0, "23g: flush chap_edge sample" );
        TEST_ASSERT( lsmash_update_track_duration( c_root, c_tid, 0 ) == 0, "23g: update chap_edge track duration" );

        /* 2a) Leading NUL / short 0xEF / missing '=' / missing ' ' must not UB or crash */
        static const uint8_t nul_hdr[5] = { '\0', '\0', 'B', 'A', '\n' };
        TEST_ASSERT( write_test_file( chap_edge_txt, nul_hdr, sizeof(nul_hdr) ) == 0, "23g: write nul_hdr chapter" );
        TEST_ASSERT( lsmash_set_tyrant_chapter( c_root, (char *)chap_edge_txt, 0 ) < 0,
                     "23g: leading NUL line chapter file rejected cleanly" );

        static const uint8_t short_ef[1] = { 0xEF };
        TEST_ASSERT( write_test_file( chap_edge_txt, short_ef, sizeof(short_ef) ) == 0, "23g: write 1-byte 0xEF" );
        TEST_ASSERT( lsmash_set_tyrant_chapter( c_root, (char *)chap_edge_txt, 0 ) < 0,
                     "23g: 1-byte 0xEF chapter file rejected cleanly without stack overread" );

        const char *bad_min_sep = "00:00:00.000NoSpaceSeparator\n";
        TEST_ASSERT( write_test_file( chap_edge_txt, (const uint8_t *)bad_min_sep, strlen( bad_min_sep ) ) == 0,
                     "23g: write minimum chapter missing space" );
        TEST_ASSERT( lsmash_set_tyrant_chapter( c_root, (char *)chap_edge_txt, 0 ) == 0
                  && lsmash_count_tyrant_chapter( c_root ) == 0,
                     "23g: minimum chapter missing space exits loop safely without NULL+1 UB" );
        lsmash_delete_tyrant_chapter( c_root );

        const char *bad_simp_eq =
            "CHAPTER01=00:00:00.000\n"
            "CHAPTER01NAME_MISSING_EQUALS\n";
        TEST_ASSERT( write_test_file( chap_edge_txt, (const uint8_t *)bad_simp_eq, strlen( bad_simp_eq ) ) == 0,
                     "23g: write simple chapter missing '='" );
        TEST_ASSERT( lsmash_set_tyrant_chapter( c_root, (char *)chap_edge_txt, 0 ) == 0
                  && lsmash_count_tyrant_chapter( c_root ) == 0,
                     "23g: simple chapter missing '=' exits loop safely without NULL+1 UB" );
        lsmash_delete_tyrant_chapter( c_root );

        /* Simple chapter containing an embedded NUL-prefixed line and CRLF blank line before entry 2 */
        static const char nul_mid[] =
            "CHAPTER01=00:00:00.000\n"
            "CHAPTER01NAME=First\n"
            "\0\0ignored_nul_line\n"
            "\r\n"
            "CHAPTER02=00:00:02.000\n"
            "CHAPTER02NAME=Second\n";
        TEST_ASSERT( write_test_file( chap_edge_txt, (const uint8_t *)nul_mid, sizeof(nul_mid) - 1 ) == 0,
                     "23g: write chapter file with NUL-prefixed line between entries" );
        TEST_ASSERT( lsmash_set_tyrant_chapter( c_root, (char *)chap_edge_txt, 0 ) == 0
                  && lsmash_count_tyrant_chapter( c_root ) == 2,
                     "23g: chapter_lumber_line skips NUL-prefixed and blank lines without size_t underflow" );
        lsmash_delete_tyrant_chapter( c_root );

        /* 2b) 252-256 byte chapter names with UTF-8 boundary crossings and non-UTF-8 fallback */
        /* First verify add_bom = 0 on a 256-byte non-UTF-8 (0x82 0xA0 repeated) title:
         * bounded 3-byte lookback must fall back to max_len (255) instead of stripping to 0. */
        char cp932_chap_buf[512];
        size_t cp_pos = (size_t)snprintf( cp932_chap_buf, sizeof(cp932_chap_buf),
                                          "CHAPTER01=00:00:00.000\nCHAPTER01NAME=" );
        for( size_t k = 0; k < 128; k++ )
        {
            cp932_chap_buf[cp_pos++] = (char)0x82;
            cp932_chap_buf[cp_pos++] = (char)0xA0;
        }
        cp932_chap_buf[cp_pos++] = '\n';
        TEST_ASSERT( write_test_file( chap_edge_txt, (const uint8_t *)cp932_chap_buf, cp_pos ) == 0,
                     "23g: write 256B non-UTF-8 chapter file" );
        TEST_ASSERT( lsmash_set_tyrant_chapter( c_root, (char *)chap_edge_txt, 0 ) == 0
                  && lsmash_count_tyrant_chapter( c_root ) == 1,
                     "23g: set 256B non-UTF-8 chapter with add_bom=0" );
        {
            double ts0 = -1.0;
            const char *raw_cp = lsmash_get_tyrant_chapter( c_root, 1, &ts0 );
            TEST_ASSERT( raw_cp && strlen( raw_cp ) == 255
                      && (uint8_t)raw_cp[0] == 0x82 && (uint8_t)raw_cp[254] == 0x82,
                         "23g: add_bom=0 non-UTF-8 0x80..0xBF run falls back to 255B instead of stripping to 0" );
        }
        lsmash_delete_tyrant_chapter( c_root );

        char chap_bom_buf[4096];
        size_t pos = 0;
        /* Chapter 1: 254 ASCII 'A' -> truncated to 252 + 3B BOM == 255 bytes */
        pos += (size_t)snprintf( chap_bom_buf + pos, sizeof(chap_bom_buf) - pos,
                                 "CHAPTER01=00:00:00.000\nCHAPTER01NAME=" );
        memset( chap_bom_buf + pos, 'A', 254 );
        pos += 254;
        chap_bom_buf[pos++] = '\n';

        /* Chapter 2: 251 'B' + 3-byte UTF-8 (0xE3 0x81 0x82) = 254 bytes -> cut at 251 (+3B BOM == 254) */
        pos += (size_t)snprintf( chap_bom_buf + pos, sizeof(chap_bom_buf) - pos,
                                 "CHAPTER02=00:00:01.000\nCHAPTER02NAME=" );
        memset( chap_bom_buf + pos, 'B', 251 );
        pos += 251;
        chap_bom_buf[pos++] = (char)0xE3;
        chap_bom_buf[pos++] = (char)0x81;
        chap_bom_buf[pos++] = (char)0x82;
        chap_bom_buf[pos++] = '\n';

        /* Chapter 3: 249 'C' + 4-byte UTF-8 (0xF0 0x9F 0x98 0x80) + "XY" = 255 bytes -> cut at 249 (+3B BOM == 252) */
        pos += (size_t)snprintf( chap_bom_buf + pos, sizeof(chap_bom_buf) - pos,
                                 "CHAPTER03=00:00:02.000\nCHAPTER03NAME=" );
        memset( chap_bom_buf + pos, 'C', 249 );
        pos += 249;
        chap_bom_buf[pos++] = (char)0xF0;
        chap_bom_buf[pos++] = (char)0x9F;
        chap_bom_buf[pos++] = (char)0x98;
        chap_bom_buf[pos++] = (char)0x80;
        chap_bom_buf[pos++] = 'X';
        chap_bom_buf[pos++] = 'Y';
        chap_bom_buf[pos++] = '\n';

        /* Chapter 4: 249 'D' + 3-byte UTF-8 (0xE3 0x81 0x82) + "ZZ" = 254 bytes -> keeps full 252 (+3B BOM == 255) */
        pos += (size_t)snprintf( chap_bom_buf + pos, sizeof(chap_bom_buf) - pos,
                                 "CHAPTER04=00:00:03.000\nCHAPTER04NAME=" );
        memset( chap_bom_buf + pos, 'D', 249 );
        pos += 249;
        chap_bom_buf[pos++] = (char)0xE3;
        chap_bom_buf[pos++] = (char)0x81;
        chap_bom_buf[pos++] = (char)0x82;
        chap_bom_buf[pos++] = 'Z';
        chap_bom_buf[pos++] = 'Z';
        chap_bom_buf[pos++] = '\n';

        /* Chapter 5: exact len == max_len == 252 (249 'E' + 0xE3 0x81 0x82) -> unchanged 252 (+3B BOM == 255) */
        pos += (size_t)snprintf( chap_bom_buf + pos, sizeof(chap_bom_buf) - pos,
                                 "CHAPTER05=00:00:04.000\nCHAPTER05NAME=" );
        memset( chap_bom_buf + pos, 'E', 249 );
        pos += 249;
        chap_bom_buf[pos++] = (char)0xE3;
        chap_bom_buf[pos++] = (char)0x81;
        chap_bom_buf[pos++] = (char)0x82;
        chap_bom_buf[pos++] = '\n';

        /* Chapter 6: 252 'F' + 3-byte UTF-8 lead at str[252] (0xE3 0x81 0x82) = 255 bytes -> keeps all 252 'F' */
        pos += (size_t)snprintf( chap_bom_buf + pos, sizeof(chap_bom_buf) - pos,
                                 "CHAPTER06=00:00:05.000\nCHAPTER06NAME=" );
        memset( chap_bom_buf + pos, 'F', 252 );
        pos += 252;
        chap_bom_buf[pos++] = (char)0xE3;
        chap_bom_buf[pos++] = (char)0x81;
        chap_bom_buf[pos++] = (char)0x82;
        chap_bom_buf[pos++] = '\n';

        /* Chapter 7: 256B repeated 0x82 0xA0 (all bytes 0x80..0xBF, non-UTF-8) -> falls back to 252 (+3B BOM == 255) */
        pos += (size_t)snprintf( chap_bom_buf + pos, sizeof(chap_bom_buf) - pos,
                                 "CHAPTER07=00:00:06.000\nCHAPTER07NAME=" );
        for( size_t k = 0; k < 128; k++ )
        {
            chap_bom_buf[pos++] = (char)0x82;
            chap_bom_buf[pos++] = (char)0xA0;
        }
        chap_bom_buf[pos++] = '\n';

        TEST_ASSERT( write_test_file( chap_edge_txt, (const uint8_t *)chap_bom_buf, pos ) == 0,
                     "23g: write 252-256B UTF-8 & non-UTF-8 chapter file" );
        TEST_ASSERT( lsmash_set_tyrant_chapter( c_root, (char *)chap_edge_txt, 1 ) == 0,
                     "23g: set 252-256B chapters with add_bom=1" );
        TEST_ASSERT( lsmash_finish_movie( c_root, NULL ) == 0, "23g: finish chap_edge_mp4" );
        lsmash_close_file( &c_param );
        lsmash_destroy_root( c_root );

        /* Reopen, verify UTF-8 boundary lengths without uint8_t wraparound, and dump */
        lsmash_root_t *c_rd_root = lsmash_create_root();
        lsmash_file_parameters_t c_rd_param = { 0 };
        TEST_ASSERT( lsmash_open_file( chap_edge_mp4, 1, &c_rd_param ) == 0, "23g: reopen chap_edge_mp4" );
        c_rd_param.mode |= LSMASH_FILE_MODE_DUMP;
        lsmash_file_t *c_rd_file = lsmash_set_file( c_rd_root, &c_rd_param );
        TEST_ASSERT( lsmash_read_file( c_rd_file, &c_rd_param ) >= 0, "23g: read chap_edge_mp4" );
        TEST_ASSERT( lsmash_count_tyrant_chapter( c_rd_root ) == 7, "23g: chapter count == 7 (no wraparound)" );
        double ch_ts = -1.0;
        const char *ch1 = lsmash_get_tyrant_chapter( c_rd_root, 1, &ch_ts );
        const char *ch2 = lsmash_get_tyrant_chapter( c_rd_root, 2, &ch_ts );
        const char *ch3 = lsmash_get_tyrant_chapter( c_rd_root, 3, &ch_ts );
        const char *ch4 = lsmash_get_tyrant_chapter( c_rd_root, 4, &ch_ts );
        const char *ch5 = lsmash_get_tyrant_chapter( c_rd_root, 5, &ch_ts );
        const char *ch6 = lsmash_get_tyrant_chapter( c_rd_root, 6, &ch_ts );
        const char *ch7 = lsmash_get_tyrant_chapter( c_rd_root, 7, &ch_ts );
        TEST_ASSERT( ch1 && strlen( ch1 ) == 252 && ch1[0] == 'A' && ch1[251] == 'A',
                     "23g: 254B ASCII chapter with BOM truncated to 252B body (stored 255B)" );
        TEST_ASSERT( ch2 && strlen( ch2 ) == 251 && ch2[0] == 'B' && ch2[250] == 'B',
                     "23g: 254B 3-byte UTF-8 boundary chapter truncated cleanly at 251B" );
        TEST_ASSERT( ch3 && strlen( ch3 ) == 249 && ch3[0] == 'C' && ch3[248] == 'C',
                     "23g: 255B 4-byte UTF-8 boundary chapter truncated cleanly at 249B" );
        TEST_ASSERT( ch4 && strlen( ch4 ) == 252
                  && (uint8_t)ch4[249] == 0xE3 && (uint8_t)ch4[250] == 0x81 && (uint8_t)ch4[251] == 0x82,
                     "23g: 254B fitting 3-byte UTF-8 chapter preserves full 252B body" );
        TEST_ASSERT( ch5 && strlen( ch5 ) == 252
                  && ch5[0] == 'E' && ch5[248] == 'E'
                  && (uint8_t)ch5[249] == 0xE3 && (uint8_t)ch5[250] == 0x81 && (uint8_t)ch5[251] == 0x82,
                     "23g: exact 252B UTF-8 chapter preserved unchanged" );
        TEST_ASSERT( ch6 && strlen( ch6 ) == 252 && ch6[0] == 'F' && ch6[251] == 'F',
                     "23g: UTF-8 lead byte at str[max_len] returns exact max_len without off-by-one cut" );
        TEST_ASSERT( ch7 && strlen( ch7 ) == 252
                  && (uint8_t)ch7[0] == 0x82 && (uint8_t)ch7[1] == 0xA0
                  && (uint8_t)ch7[250] == 0x82 && (uint8_t)ch7[251] == 0xA0,
                     "23g: add_bom=1 non-UTF-8 0x80..0xBF run falls back to 252B instead of stripping to 0" );
        TEST_ASSERT( lsmash_print_movie( c_rd_root, chap_edge_dump ) == 0,
                     "23g: lsmash_print_movie dumps BOM-clamped chapters cleanly" );
        remove( chap_edge_dump );

        /* Verify NULL chpl->list guard in both lsmash_print_chapter_list and print_chpl */
        {
            lsmash_entry_list_t *saved_chpl_list = c_rd_file->moov->udta->chpl->list;
            c_rd_file->moov->udta->chpl->list = NULL;
            TEST_ASSERT( lsmash_print_chapter_list( c_rd_root ) < 0,
                         "23g: lsmash_print_chapter_list rejects NULL chpl->list safely" );
            TEST_ASSERT( lsmash_print_movie( c_rd_root, chap_edge_dump ) < 0,
                         "23g: print_chpl rejects NULL chpl->list safely" );
            c_rd_file->moov->udta->chpl->list = saved_chpl_list;
            remove( chap_edge_dump );
        }
        lsmash_close_file( &c_rd_param );
        lsmash_destroy_root( c_rd_root );
        remove( chap_edge_mp4 );
        remove( chap_edge_txt );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "23g: 0 bytes leaked after chapter boundary tests" );
#endif

        /* 3) Corrupted compressorname (> 31 length & non-zero padding) + print_chan NULL guard */
        const char *comp_mov  = "tests/tmp_test_comp_print.mov";
        const char *comp_mp4  = "tests/tmp_test_comp_print.mp4";
        const char *comp_dump = "tests/tmp_test_comp_print_dump.txt";

        /* 3a) QTFF (.mov) branch: compressorname[0] = 200, compressorname[0] = 5 + garbage, and chan NULL guard */
        lsmash_root_t *p_root = lsmash_create_root();
        lsmash_file_parameters_t p_param = { 0 };
        TEST_ASSERT( lsmash_open_file( comp_mov, 0, &p_param ) == 0, "23g: open comp_mov" );
        lsmash_brand_type qt_brands[1] = { ISOM_BRAND_TYPE_QT };
        p_param.major_brand = ISOM_BRAND_TYPE_QT;
        p_param.brands      = qt_brands;
        p_param.brand_count = 1;
        lsmash_file_t *p_wr_file = lsmash_set_file( p_root, &p_param );
        TEST_ASSERT( p_wr_file != NULL, "23g: set comp_mov file" );
        lsmash_movie_parameters_t p_mparam;
        lsmash_initialize_movie_parameters( &p_mparam );
        TEST_ASSERT( lsmash_set_movie_parameters( p_root, &p_mparam ) == 0, "23g: set comp_mov movie params" );

        /* Track 1: compressorname[0] = 200 (> 31), 31 non-NUL bytes */
        uint32_t ptid1 = lsmash_create_track( p_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        lsmash_track_parameters_t p_tp;
        lsmash_initialize_track_parameters( &p_tp );
        p_tp.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        lsmash_set_track_parameters( p_root, ptid1, &p_tp );
        lsmash_media_parameters_t p_mp;
        lsmash_initialize_media_parameters( &p_mp );
        p_mp.timescale = 1000;
        lsmash_set_media_parameters( p_root, ptid1, &p_mp );
        lsmash_video_summary_t *pv1 = (lsmash_video_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_VIDEO );
        pv1->sample_type = QT_CODEC_TYPE_APCH_VIDEO;
        pv1->width = 64; pv1->height = 64; pv1->depth = 24;
        pv1->compressorname[0] = (char)200;
        memcpy( pv1->compressorname + 1, "0123456789ABCDEFGHIJKLMNOPQRSTU", 31 );
        TEST_ASSERT( lsmash_add_sample_entry( p_root, ptid1, pv1 ) == 1, "23g: add QT comp=200 entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)pv1 );
        lsmash_sample_t *ps1 = lsmash_create_sample( 8 );
        memset( ps1->data, 0x11, 8 );
        ps1->dts = 0; ps1->cts = 0; ps1->index = 1; ps1->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
        TEST_ASSERT( lsmash_append_sample( p_root, ptid1, ps1 ) == 0, "23g: append sample 1" );
        TEST_ASSERT( lsmash_flush_pooled_samples( p_root, ptid1, 1000 ) == 0, "23g: flush sample 1" );

        /* Track 2: compressorname[0] = 5 ("Hello"), non-zero padding garbage in [6..31] */
        uint32_t ptid2 = lsmash_create_track( p_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        lsmash_set_track_parameters( p_root, ptid2, &p_tp );
        lsmash_set_media_parameters( p_root, ptid2, &p_mp );
        lsmash_video_summary_t *pv2 = (lsmash_video_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_VIDEO );
        pv2->sample_type = QT_CODEC_TYPE_APCH_VIDEO;
        pv2->width = 64; pv2->height = 64; pv2->depth = 24;
        pv2->compressorname[0] = 5;
        memcpy( pv2->compressorname + 1, "HelloGARBAGE_BYTES_AFTER_LENGTH", 31 );
        TEST_ASSERT( lsmash_add_sample_entry( p_root, ptid2, pv2 ) == 1, "23g: add QT comp=5+garbage entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)pv2 );
        lsmash_sample_t *ps2 = lsmash_create_sample( 8 );
        memset( ps2->data, 0x22, 8 );
        ps2->dts = 0; ps2->cts = 0; ps2->index = 1; ps2->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
        TEST_ASSERT( lsmash_append_sample( p_root, ptid2, ps2 ) == 0, "23g: append sample 2" );
        TEST_ASSERT( lsmash_flush_pooled_samples( p_root, ptid2, 1000 ) == 0, "23g: flush sample 2" );

        /* Track 3: QTFF LPCM audio with chan box to exercise print_chan NULL guard */
        uint32_t ptid3 = lsmash_create_track( p_root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
        lsmash_set_track_parameters( p_root, ptid3, &p_tp );
        lsmash_media_parameters_t a_mp;
        lsmash_initialize_media_parameters( &a_mp );
        a_mp.timescale = 48000;
        lsmash_set_media_parameters( p_root, ptid3, &a_mp );
        lsmash_audio_summary_t *pa = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
        pa->sample_type      = QT_CODEC_TYPE_LPCM_AUDIO;
        pa->frequency        = 48000;
        pa->channels         = 2;
        pa->sample_size      = 16;
        pa->samples_in_frame = 1;
        pa->bytes_per_frame  = 4;
        lsmash_codec_specific_t *qfmt = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_QT_AUDIO_FORMAT_SPECIFIC_FLAGS,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        ((lsmash_qt_audio_format_specific_flags_t *)qfmt->data.structured)->format_flags =
            QT_AUDIO_FORMAT_FLAG_SIGNED_INTEGER | QT_AUDIO_FORMAT_FLAG_PACKED;
        lsmash_add_codec_specific_data( (lsmash_summary_t *)pa, qfmt );
        lsmash_destroy_codec_specific_data( qfmt );
        lsmash_codec_specific_t *qcl = lsmash_create_codec_specific_data(
            LSMASH_CODEC_SPECIFIC_DATA_TYPE_QT_AUDIO_CHANNEL_LAYOUT,
            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        ((lsmash_qt_audio_channel_layout_t *)qcl->data.structured)->channelLayoutTag = QT_CHANNEL_LAYOUT_STEREO;
        lsmash_add_codec_specific_data( (lsmash_summary_t *)pa, qcl );
        lsmash_destroy_codec_specific_data( qcl );
        TEST_ASSERT( lsmash_add_sample_entry( p_root, ptid3, pa ) == 1, "23g: add QT audio entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)pa );
        /* Attach chan directly on the QT audio entry if codec-specific setup skipped it so
         * the written MOV genuinely carries a chan box for read_chan + print_chan coverage. */
        isom_trak_t *a_trak_w = (isom_trak_t *)p_wr_file->moov->trak_list.tail->data;
        isom_audio_entry_t *a_entry_w = (isom_audio_entry_t *)a_trak_w->mdia->minf->stbl->stsd->list.head->data;
        if( LSMASH_IS_NON_EXISTING_BOX( (isom_chan_t *)lsm_box_get_extension_box_format( &a_entry_w->extensions, QT_BOX_TYPE_CHAN ) ) )
        {
            isom_chan_t *chan_w = lsm_box_add_chan( a_entry_w );
            TEST_ASSERT( LSMASH_IS_EXISTING_BOX( chan_w ), "23g: attach chan box on QT audio entry" );
            chan_w->channelLayoutTag = QT_CHANNEL_LAYOUT_STEREO;
        }
        lsmash_sample_t *pas = lsmash_create_sample( 4 );
        memset( pas->data, 0, 4 );
        pas->dts = 0; pas->cts = 0; pas->index = 1; pas->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
        TEST_ASSERT( lsmash_append_sample( p_root, ptid3, pas ) == 0, "23g: append audio sample" );
        TEST_ASSERT( lsmash_flush_pooled_samples( p_root, ptid3, 1 ) == 0, "23g: flush audio sample" );

        TEST_ASSERT( lsmash_finish_movie( p_root, NULL ) == 0, "23g: finish comp_mov" );
        lsmash_close_file( &p_param );
        lsmash_destroy_root( p_root );

        lsmash_root_t *p_rd_root = lsmash_create_root();
        lsmash_file_parameters_t p_rd_param = { 0 };
        TEST_ASSERT( lsmash_open_file( comp_mov, 1, &p_rd_param ) == 0, "23g: reopen comp_mov" );
        p_rd_param.mode |= LSMASH_FILE_MODE_DUMP;
        lsmash_file_t *p_rd_file = lsmash_set_file( p_rd_root, &p_rd_param );
        TEST_ASSERT( lsmash_read_file( p_rd_file, &p_rd_param ) >= 0, "23g: read comp_mov" );

        /* Mutate chan->numberChannelDescriptions > 0 with channelDescriptions == NULL to verify print_chan NULL guard */
        isom_trak_t *a_trak = (isom_trak_t *)p_rd_file->moov->trak_list.tail->data;
        TEST_ASSERT( LSMASH_IS_EXISTING_BOX( a_trak ), "23g: get audio trak" );
        isom_audio_entry_t *a_entry = (isom_audio_entry_t *)a_trak->mdia->minf->stbl->stsd->list.head->data;
        isom_chan_t *chan = (isom_chan_t *)lsm_box_get_extension_box_format( &a_entry->extensions, QT_BOX_TYPE_CHAN );
        TEST_ASSERT( LSMASH_IS_EXISTING_BOX( chan ), "23g: chan box exists on QT audio entry" );
        chan->numberChannelDescriptions = 2;
        chan->channelDescriptions       = NULL;

        TEST_ASSERT( lsmash_print_movie( p_rd_root, comp_dump ) == 0,
                     "23g: lsmash_print_movie dumps corrupted QT compressorname and NULL chan safely" );
        chan->numberChannelDescriptions = 0;

        /* Verify dump output clamped compressorname to 31 bytes and excluded trailing garbage */
        FILE *dfp = fopen( comp_dump, "rb" );
        TEST_ASSERT( dfp != NULL, "23g: open QT comp_dump" );
        char dump_buf[16384];
        size_t dump_len = fread( dump_buf, 1, sizeof(dump_buf) - 1, dfp );
        fclose( dfp );
        dump_buf[dump_len] = '\0';
        TEST_ASSERT( strstr( dump_buf, "compressorname_length = 200\n" ) != NULL,
                     "23g: QT raw unsigned compressorname_length = 200 printed without sign extension" );
        TEST_ASSERT( strstr( dump_buf, "compressorname = 0123456789ABCDEFGHIJKLMNOPQRSTU\n" ) != NULL,
                     "23g: QT compressorname clamped to 31 bytes" );
        TEST_ASSERT( strstr( dump_buf, "compressorname_length = 5\n" ) != NULL,
                     "23g: QT compressorname_length = 5 printed" );
        TEST_ASSERT( strstr( dump_buf, "compressorname = Hello\n" ) != NULL,
                     "23g: QT compressorname truncated to Pascal length 5" );
        TEST_ASSERT( strstr( dump_buf, "GARBAGE" ) == NULL,
                     "23g: QT non-zero padding garbage after Pascal length is not leaked into dump" );

        remove( comp_dump );
        lsmash_close_file( &p_rd_param );
        lsmash_destroy_root( p_rd_root );
        remove( comp_mov );

        /* 3b) ISOBMFF (.mp4) branch: compressorname[0] = 200 & non-zero padding */
        lsmash_root_t *iso_root = lsmash_create_root();
        lsmash_file_parameters_t iso_param = { 0 };
        TEST_ASSERT( lsmash_open_file( comp_mp4, 0, &iso_param ) == 0, "23g: open comp_mp4" );
        iso_param.major_brand = ISOM_BRAND_TYPE_MP42;
        iso_param.brands      = mp42_brands;
        iso_param.brand_count = 3;
        TEST_ASSERT( lsmash_set_file( iso_root, &iso_param ) != NULL, "23g: set comp_mp4 file" );
        TEST_ASSERT( lsmash_set_movie_parameters( iso_root, &p_mparam ) == 0, "23g: set comp_mp4 movie params" );
        uint32_t itid = lsmash_create_track( iso_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        lsmash_set_track_parameters( iso_root, itid, &p_tp );
        lsmash_set_media_parameters( iso_root, itid, &p_mp );
        lsmash_video_summary_t *iv = create_synth_av01_summary( 64, 64 );
        iv->compressorname[0] = (char)200;
        memcpy( iv->compressorname + 1, "0123456789ABCDEFGHIJKLMNOPQRSTU", 31 );
        TEST_ASSERT( lsmash_add_sample_entry( iso_root, itid, iv ) == 1, "23g: add ISOBMFF comp=200 entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)iv );
        lsmash_sample_t *is = lsmash_create_sample( 8 );
        memset( is->data, 0x44, 8 );
        is->dts = 0; is->cts = 0; is->index = 1; is->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
        TEST_ASSERT( lsmash_append_sample( iso_root, itid, is ) == 0, "23g: append ISOBMFF sample" );
        TEST_ASSERT( lsmash_flush_pooled_samples( iso_root, itid, 1000 ) == 0, "23g: flush ISOBMFF sample" );
        TEST_ASSERT( lsmash_finish_movie( iso_root, NULL ) == 0, "23g: finish comp_mp4" );
        lsmash_close_file( &iso_param );
        lsmash_destroy_root( iso_root );

        lsmash_root_t *iso_rd_root = lsmash_create_root();
        lsmash_file_parameters_t iso_rd_param = { 0 };
        TEST_ASSERT( lsmash_open_file( comp_mp4, 1, &iso_rd_param ) == 0, "23g: reopen comp_mp4" );
        iso_rd_param.mode |= LSMASH_FILE_MODE_DUMP;
        lsmash_file_t *iso_rd_file = lsmash_set_file( iso_rd_root, &iso_rd_param );
        TEST_ASSERT( lsmash_read_file( iso_rd_file, &iso_rd_param ) >= 0, "23g: read comp_mp4" );
        TEST_ASSERT( lsmash_print_movie( iso_rd_root, comp_dump ) == 0,
                     "23g: lsmash_print_movie dumps corrupted ISOBMFF compressorname safely" );
        dfp = fopen( comp_dump, "rb" );
        TEST_ASSERT( dfp != NULL, "23g: open ISOBMFF comp_dump" );
        dump_len = fread( dump_buf, 1, sizeof(dump_buf) - 1, dfp );
        fclose( dfp );
        dump_buf[dump_len] = '\0';
        TEST_ASSERT( strstr( dump_buf, "compressorname_length = 200\n" ) != NULL
                  && strstr( dump_buf, "compressorname = 0123456789ABCDEFGHIJKLMNOPQRSTU\n" ) != NULL,
                     "23g: ISOBMFF compressorname clamped to 31 bytes without sign extension" );
        remove( comp_dump );
        lsmash_close_file( &iso_rd_param );
        lsmash_destroy_root( iso_rd_root );
        remove( comp_mp4 );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "23g: 0 bytes leaked after corrupted compressorname & chan print tests" );
#endif

        /* 4) lsmash_set_itunes_metadata() sentinel guard when active file has non-existing moov */
        const char *meta_init_path = "tests/tmp_test_meta_seg_init.mp4";
        const char *meta_seg_path  = "tests/tmp_test_meta_seg.m4s";
        const lsmash_brand_type meta_seg_brands[3] = {
            ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_ISO6, ISOM_BRAND_TYPE_MP42
        };
        lsmash_root_t *m_seg_root = lsmash_create_root();
        lsmash_file_parameters_t m_init_param = { 0 };
        TEST_ASSERT( lsmash_open_file( meta_init_path, 0, &m_init_param ) == 0, "23g: open meta_init_path" );
        m_init_param.mode &= ~LSMASH_FILE_MODE_MEDIA;
        m_init_param.mode |= LSMASH_FILE_MODE_FRAGMENTED | LSMASH_FILE_MODE_SEGMENT;
        m_init_param.major_brand = ISOM_BRAND_TYPE_ISOM;
        m_init_param.brands      = (lsmash_brand_type *)meta_seg_brands;
        m_init_param.brand_count = 3;
        TEST_ASSERT( lsmash_set_file( m_seg_root, &m_init_param ) != NULL, "23g: set meta init segment" );
        uint32_t m_seg_tid = seg_create_video_track( m_seg_root );
        TEST_ASSERT( m_seg_tid != 0, "23g: create track for meta segment test" );

        lsmash_file_parameters_t m_seg_param = { 0 };
        TEST_ASSERT( lsmash_open_file( meta_seg_path, 0, &m_seg_param ) == 0, "23g: open meta_seg_path" );
        m_seg_param.mode &= ~LSMASH_FILE_MODE_INITIALIZATION;
        m_seg_param.mode |= LSMASH_FILE_MODE_FRAGMENTED | LSMASH_FILE_MODE_SEGMENT;
        lsmash_file_t *m_seg_file = lsmash_set_file( m_seg_root, &m_seg_param );
        TEST_ASSERT( m_seg_file != NULL, "23g: set meta media segment" );
        lsmash_adhoc_remux_t m_remux = { .buffer_size = 65536 };
        TEST_ASSERT( lsmash_switch_media_segment( m_seg_root, m_seg_file, &m_remux ) == 0,
                     "23g: switch to media segment (active file has non-existing moov)" );

        lsmash_itunes_metadata_t seg_md = { 0 };
        seg_md.item         = ITUNES_METADATA_ITEM_TITLE;
        seg_md.type         = ITUNES_METADATA_TYPE_STRING;
        seg_md.value.string = "SegmentTitle";
        TEST_ASSERT( lsmash_set_itunes_metadata( m_seg_root, seg_md ) < 0,
                     "23g: lsmash_set_itunes_metadata rejects media segment lacking moov cleanly" );
        TEST_ASSERT( LSMASH_IS_NON_EXISTING_BOX( lsm_box_default_non_existing_moov()->udta ),
                     "23g: static non-existing moov sentinel udta remains unpolluted" );

        TEST_ASSERT( seg_write_fragment( m_seg_root, m_seg_tid, 0 ) == 0, "23g: write fragment after metadata check" );
        TEST_ASSERT( lsmash_finish_movie( m_seg_root, &m_remux ) == 0, "23g: finish meta media segment" );
        lsmash_close_file( &m_init_param );
        lsmash_close_file( &m_seg_param );
        lsmash_destroy_root( m_seg_root );
        remove( meta_init_path );
        remove( meta_seg_path );
#ifdef LSMASH_DEBUG_ALLOC
        TEST_ASSERT( lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0,
                     "23g: 0 bytes leaked after metadata non-existing moov sentinel test" );
#endif
    }

    remove( mp4_path );
    remove( bom_path );
    remove( qt_path );
    remove( chap_file );
    printf( "[PASS] test_container_metadata_and_edit_list_roundtrip\n" );
    return 0;
}
