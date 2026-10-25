/*****************************************************************************
 * test_audio_lpcm_flac.c
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
 * Test 6: ISO/IEC 23003-5 ipcm/fpcm/pcmC + ISO/IEC 14496-12 chnl/srat Round-Trip
 *---------------------------------------------------------------------------*/
int test_lpcm_pcmc_chnl_srat_roundtrip( void )
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
int test_multitrack_fragmented_lpcm_roundtrip( void )
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
int test_wave_importer_isobmff_lpcm( void )
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
 * Test 15: FLAC-in-ISOBMFF (fLaC / dfLa) Importer & Mux/Demux Round-Trip
 *---------------------------------------------------------------------------*/
int test_flac_isobmff_roundtrip( void )
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

    /* 6. ALAC ISOBMFF, QTFF v1, and Chimera (ISOBMFF + QTFF brands) round-trip verification */
    {
        const char *alac_isom_path    = "tests/tmp_test_alac_isom.mp4";
        const char *alac_qtff_path    = "tests/tmp_test_alac_qtff.mov";
        const char *alac_chimera_path = "tests/tmp_test_alac_chimera.mp4";
        const char *paths[3] = { alac_isom_path, alac_qtff_path, alac_chimera_path };
        for( int mode = 0; mode < 3; mode++ )
        {
            lsmash_root_t *mroot = lsmash_create_root();
            lsmash_file_parameters_t mparam = { 0 };
            TEST_ASSERT( lsmash_open_file( paths[mode], 0, &mparam ) == 0, "open alac output" );
            lsmash_brand_type brands[3];
            if( mode == 0 )
            {
                brands[0] = ISOM_BRAND_TYPE_MP42;
                brands[1] = ISOM_BRAND_TYPE_ISOM;
                mparam.major_brand = ISOM_BRAND_TYPE_MP42;
                mparam.brands      = brands;
                mparam.brand_count = 2;
            }
            else if( mode == 1 )
            {
                brands[0] = ISOM_BRAND_TYPE_QT;
                mparam.major_brand = ISOM_BRAND_TYPE_QT;
                mparam.brands      = brands;
                mparam.brand_count = 1;
            }
            else
            {
                brands[0] = ISOM_BRAND_TYPE_MP42;
                brands[1] = ISOM_BRAND_TYPE_ISOM;
                brands[2] = ISOM_BRAND_TYPE_QT;
                mparam.major_brand = ISOM_BRAND_TYPE_MP42;
                mparam.brands      = brands;
                mparam.brand_count = 3;
            }
            TEST_ASSERT( lsmash_set_file( mroot, &mparam ) != NULL, "set alac output file" );
            lsmash_movie_parameters_t mv;
            lsmash_initialize_movie_parameters( &mv );
            mv.timescale = 48000;
            TEST_ASSERT( lsmash_set_movie_parameters( mroot, &mv ) == 0, "set alac movie params" );
            uint32_t tid = lsmash_create_track( mroot, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
            lsmash_track_parameters_t tp;
            lsmash_initialize_track_parameters( &tp );
            tp.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
            TEST_ASSERT( lsmash_set_track_parameters( mroot, tid, &tp ) == 0, "set alac track params" );
            lsmash_media_parameters_t mp;
            lsmash_initialize_media_parameters( &mp );
            mp.timescale = 48000;
            TEST_ASSERT( lsmash_set_media_parameters( mroot, tid, &mp ) == 0, "set alac media params" );

            lsmash_audio_summary_t *asum = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
            asum->sample_type      = (mode == 1) ? QT_CODEC_TYPE_ALAC_AUDIO : ISOM_CODEC_TYPE_ALAC_AUDIO;
            asum->frequency        = 48000;
            asum->channels         = 2;
            asum->sample_size      = 24;
            asum->samples_in_frame = 4096;
            asum->bytes_per_frame  = 0;
            lsmash_codec_specific_t *cs = lsmash_create_codec_specific_data(
                LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_ALAC,
                LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
            lsmash_alac_specific_parameters_t *ap = (lsmash_alac_specific_parameters_t *)cs->data.structured;
            ap->frameLength   = 4096;
            ap->bitDepth      = 24;
            ap->numChannels   = 2;
            ap->maxFrameBytes = 64;
            ap->avgBitrate    = 384000;
            ap->sampleRate    = 48000;
            TEST_ASSERT( lsmash_add_codec_specific_data( (lsmash_summary_t *)asum, cs ) == 0, "add alac specific" );
            lsmash_destroy_codec_specific_data( cs );
            TEST_ASSERT( lsmash_add_sample_entry( mroot, tid, asum ) == 1, "add alac sample entry" );
            lsmash_cleanup_summary( (lsmash_summary_t *)asum );

            const uint32_t pkt_sizes[2] = { 28, 36 };
            for( int i = 0; i < 2; i++ )
            {
                lsmash_sample_t *s = lsmash_create_sample( pkt_sizes[i] );
                memset( s->data, 0xA0 + i, pkt_sizes[i] );
                s->dts           = (uint64_t)i * 4096;
                s->cts           = s->dts;
                s->index         = 1;
                s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
                TEST_ASSERT( lsmash_append_sample( mroot, tid, s ) == 0, "append alac sample" );
            }
            TEST_ASSERT( lsmash_flush_pooled_samples( mroot, tid, 4096 ) == 0, "flush alac samples" );
            TEST_ASSERT( lsmash_update_track_duration( mroot, tid, 4096 ) == 0, "update alac track duration" );
            TEST_ASSERT( lsmash_finish_movie( mroot, NULL ) == 0, "finish alac movie" );
            lsmash_close_file( &mparam );
            lsmash_destroy_root( mroot );

            lsmash_root_t *rroot = lsmash_create_root();
            lsmash_file_parameters_t rparam = { 0 };
            TEST_ASSERT( lsmash_open_file( paths[mode], 1, &rparam ) == 0, "open alac for read" );
            lsmash_file_t *rfile = lsmash_set_file( rroot, &rparam );
            TEST_ASSERT( lsmash_read_file( rfile, &rparam ) >= 0, "read alac movie" );
            lsmash_audio_summary_t *rsum = (lsmash_audio_summary_t *)lsmash_get_summary( rroot, 1, 1 );
            TEST_ASSERT( rsum != NULL, "get alac summary" );
            lsmash_codec_type_t exp_type = (mode == 1) ? QT_CODEC_TYPE_ALAC_AUDIO : ISOM_CODEC_TYPE_ALAC_AUDIO;
            TEST_ASSERT( lsmash_check_codec_type_identical( rsum->sample_type, exp_type ), "alac sample_type matches" );
            TEST_ASSERT( rsum->frequency == 48000 && rsum->channels == 2 && rsum->sample_size == 24 && rsum->samples_in_frame == 4096,
                         "alac summary fields recovered from alac specific box" );
            lsmash_cleanup_summary( (lsmash_summary_t *)rsum );
            TEST_ASSERT( lsmash_construct_timeline( rroot, 1 ) == 0, "construct alac timeline" );
            TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( rroot, 1 ) == 2, "2 alac packets in timeline" );
            for( uint32_t i = 1; i <= 2; i++ )
            {
                lsmash_sample_t *s = lsmash_get_sample_from_media_timeline( rroot, 1, i );
                TEST_ASSERT( s != NULL && s->length == pkt_sizes[i - 1], "alac packet length preserved" );
                lsmash_delete_sample( s );
            }
            lsmash_destruct_timeline( rroot, 1 );
            lsmash_close_file( &rparam );
            lsmash_destroy_root( rroot );
            remove( paths[mode] );
        }
    }

    remove( flac_path );
    remove( mp4_path );

    printf( "[PASS] test_flac_isobmff_roundtrip\n" );
    return 0;
}
