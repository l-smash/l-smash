/*****************************************************************************
 * test_raw_importers_streams.c
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
 * Test 20: ADTS, AC-3, E-AC-3, DTS, MP3, AMR and ALS raw streams through lsmash_read_file() and the Media Timeline
 *---------------------------------------------------------------------------*/
#define AUDIO_EXPECT_MAX_SAMPLES 6
#define AUDIO_EXPECT_MAX_LENGTH  512

typedef struct
{
    uint32_t count;
    uint32_t length[AUDIO_EXPECT_MAX_SAMPLES];
    uint64_t dts   [AUDIO_EXPECT_MAX_SAMPLES];
    uint32_t index [AUDIO_EXPECT_MAX_SAMPLES];
    uint8_t  data  [AUDIO_EXPECT_MAX_SAMPLES][AUDIO_EXPECT_MAX_LENGTH];
} audio_expect_t;

/* Add a frame to the expectation: its data is what a sample is expected to be. */
static void expect_add_sample( audio_expect_t *exp, const uint8_t *data, uint32_t length, uint64_t dts, uint32_t index )
{
    exp->length[exp->count] = length;
    exp->dts   [exp->count] = dts;
    exp->index [exp->count] = index;
    memcpy( exp->data[exp->count], data, length );
    ++exp->count;
}

typedef struct
{
    const char         *path;
    const audio_expect_t *exp;
    lsmash_codec_type_t sample_type;
    uint32_t            frequency;
    uint32_t            channels;
    uint32_t            channels2;      /* expected channels of the second sample entry if the stream changes its summary */
    uint32_t            summary_count;
    uint64_t            media_duration;
    uint32_t            last_delta;
} audio_stream_check_t;

static int check_raw_audio_stream( const audio_stream_check_t *chk )
{
    const audio_expect_t *exp = chk->exp;
    const char *path = chk->path;
    /* The image of the file tells where each sample is actually stored. */
    FILE *fp = fopen( path, "rb" );
    TEST_ASSERT( fp != NULL, "20: open the written stream" );
    fseek( fp, 0, SEEK_END );
    long image_size = ftell( fp );
    fseek( fp, 0, SEEK_SET );
    uint8_t *image = (uint8_t *)malloc( (size_t)image_size );
    TEST_ASSERT( image != NULL && fread( image, 1, (size_t)image_size, fp ) == (size_t)image_size, "20: load the stream image" );
    fclose( fp );

    /*---- The sequential reader is the reference of the stream. ----*/
    lsmash_sample_property_t ref_prop[AUDIO_EXPECT_MAX_SAMPLES] = { { 0 } };
    {
        lsmash_root_t *root = lsmash_create_root();
        importer_t *imp = lsmash_importer_open( root, path, "auto" );
        TEST_ASSERT( imp != NULL, "20: open the importer" );
        for( uint32_t i = 0; i < exp->count; i++ )
        {
            lsmash_sample_t *sample = NULL;
            TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &sample ) >= 0 && sample != NULL, "20: reference access unit" );
            TEST_ASSERT( sample->length == exp->length[i] && memcmp( sample->data, exp->data[i], exp->length[i] ) == 0
                      && sample->dts == exp->dts[i] && sample->cts == exp->dts[i], "20: reference access unit is the expected frame" );
            ref_prop[i] = sample->prop;
            lsmash_delete_sample( sample );
        }
        lsmash_sample_t *eof = NULL;
        TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &eof ) == 2 && eof == NULL, "20: reference reader reaches EOF" );
        TEST_ASSERT( lsmash_importer_get_last_delta( imp, 1 ) == chk->last_delta, "20: reference last delta" );
        lsmash_importer_close( imp );
        lsmash_destroy_root( root );
    }

    /*---- lsmash_read_file() and the Media Timeline ----*/
    {
        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( root != NULL && lsmash_open_file( path, 1, &param ) == 0, "20: open the stream" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( file != NULL, "20: set the stream file" );
        TEST_ASSERT( lsmash_read_file( file, &param ) >= 0, "20: read_file succeeds on the raw stream via fake boxes" );
        lsmash_movie_parameters_t movie_param;
        TEST_ASSERT( lsmash_get_movie_parameters( root, &movie_param ) == 0 && movie_param.number_of_tracks == 1, "20: one fake track" );
        TEST_ASSERT( movie_param.timescale == chk->frequency, "20: movie timescale is the sample rate" );
        uint32_t track_ID = lsmash_get_track_ID( root, 1 );
        TEST_ASSERT( track_ID != 0, "20: track ID" );
        TEST_ASSERT( lsmash_get_media_timescale( root, track_ID ) == chk->frequency, "20: media timescale is the sample rate" );
        TEST_ASSERT( lsmash_count_summary( root, track_ID ) == 1, "20: one sample entry before the timeline is built" );
        lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_get_summary( root, track_ID, 1 );
        TEST_ASSERT( summary != NULL, "20: first summary" );
        TEST_ASSERT( lsmash_check_codec_type_identical( summary->sample_type, chk->sample_type ), "20: sample entry type" );
        TEST_ASSERT( summary->frequency == chk->frequency && summary->channels == chk->channels, "20: first summary frequency and channels" );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        TEST_ASSERT( lsmash_construct_timeline( root, track_ID ) == 0, "20: construct timeline" );
        /* A change of the stream in the middle adds sample entries while the Timeline is built. */
        TEST_ASSERT( lsmash_count_summary( root, track_ID ) == chk->summary_count, "20: number of sample entries" );
        if( chk->channels2 )
        {
            summary = (lsmash_audio_summary_t *)lsmash_get_summary( root, track_ID, 2 );
            TEST_ASSERT( summary != NULL && summary->channels == chk->channels2, "20: second summary channels" );
            lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        }
        TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( root, track_ID ) == exp->count, "20: sample count" );
        uint32_t max_length = 0;
        for( uint32_t i = 0; i < exp->count; i++ )
            max_length = LSMASH_MAX( max_length, exp->length[i] );
        TEST_ASSERT( lsmash_get_max_sample_size_in_media_timeline( root, track_ID ) == max_length, "20: max sample size" );
        TEST_ASSERT( lsmash_get_media_duration_from_media_timeline( root, track_ID ) == chk->media_duration, "20: media duration" );
        uint32_t last_delta = 0;
        TEST_ASSERT( lsmash_get_last_sample_delta_from_media_timeline( root, track_ID, &last_delta ) == 0 && last_delta == chk->last_delta,
                     "20: last sample delta" );
        /* Random order access exercises the cached DTS accumulation and the stream seek of every read. */
        for( uint32_t k = 0; k < exp->count * 2; k++ )
        {
            uint32_t i = (k < exp->count) ? (exp->count - 1 - k) : (k - exp->count);
            lsmash_sample_t *sample = lsmash_get_sample_from_media_timeline( root, track_ID, i + 1 );
            TEST_ASSERT( sample != NULL, "20: timeline sample" );
            TEST_ASSERT( sample->length == exp->length[i] && memcmp( sample->data, exp->data[i], exp->length[i] ) == 0, "20: timeline sample data" );
            TEST_ASSERT( sample->dts == exp->dts[i] && sample->cts == exp->dts[i], "20: timeline sample timestamps" );
            TEST_ASSERT( sample->index == exp->index[i], "20: timeline sample entry index" );
            TEST_ASSERT( sample->prop.ra_flags == ref_prop[i].ra_flags && sample->prop.pre_roll.distance == ref_prop[i].pre_roll.distance,
                         "20: timeline sample property equals the sequential reader" );
            TEST_ASSERT( (long)(sample->pos + sample->length) <= image_size && memcmp( image + sample->pos, exp->data[i], exp->length[i] ) == 0,
                         "20: sample position points at the sample data in the stream" );
            lsmash_delete_sample( sample );
            lsmash_sample_t info = { 0 };
            uint64_t dts = UINT64_MAX;
            TEST_ASSERT( lsmash_get_sample_info_from_media_timeline( root, track_ID, i + 1, &info ) == 0
                      && info.dts == exp->dts[i] && info.length == exp->length[i] && info.index == exp->index[i], "20: sample info" );
            TEST_ASSERT( lsmash_get_dts_from_media_timeline( root, track_ID, i + 1, &dts ) == 0 && dts == exp->dts[i], "20: dts getter" );
        }
        TEST_ASSERT( lsmash_get_sample_from_media_timeline( root, track_ID, exp->count + 1 ) == NULL, "20: no sample past the end" );
        TEST_ASSERT( lsmash_construct_timeline( root, track_ID ) == 0
                  && lsmash_get_sample_count_in_media_timeline( root, track_ID ) == exp->count, "20: constructing twice is harmless" );
        lsmash_destruct_timeline( root, track_ID );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
    }

    /*---- The sequential reader must continue from where it stopped after the Media Timeline moved the stream. ----*/
    {
        lsmash_root_t *root = lsmash_create_root();
        importer_t *imp = lsmash_importer_open( root, path, "auto" );
        TEST_ASSERT( imp != NULL, "20: open the importer again" );
        uint32_t track_ID = lsmash_get_track_ID( root, 1 );
        uint32_t before = exp->count / 2;
        for( uint32_t i = 0; i < before; i++ )
        {
            lsmash_sample_t *sample = NULL;
            TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &sample ) >= 0 && sample != NULL, "20: access unit before timeline" );
            TEST_ASSERT( sample->length == exp->length[i] && memcmp( sample->data, exp->data[i], exp->length[i] ) == 0, "20: access unit data before timeline" );
            lsmash_delete_sample( sample );
        }
        TEST_ASSERT( lsmash_importer_construct_timeline( imp, 1 ) == 0, "20: construct timeline in the middle of reading" );
        for( uint32_t k = 0; k < exp->count; k++ )
        {
            uint32_t i = exp->count - 1 - k;
            lsmash_sample_t *sample = lsmash_get_sample_from_media_timeline( root, track_ID, i + 1 );
            TEST_ASSERT( sample != NULL && sample->length == exp->length[i] && memcmp( sample->data, exp->data[i], exp->length[i] ) == 0,
                         "20: timeline sample read in the middle of reading" );
            lsmash_delete_sample( sample );
        }
        for( uint32_t i = before; i < exp->count; i++ )
        {
            lsmash_sample_t *sample = NULL;
            TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &sample ) >= 0 && sample != NULL, "20: access unit after timeline" );
            TEST_ASSERT( sample->length == exp->length[i] && memcmp( sample->data, exp->data[i], exp->length[i] ) == 0
                      && sample->dts == exp->dts[i], "20: access unit continues from where it stopped" );
            lsmash_delete_sample( sample );
            if( i == before )
            {
                /* The timeline is also usable between sequential reads. */
                lsmash_sample_t *first = lsmash_get_sample_from_media_timeline( root, track_ID, 1 );
                TEST_ASSERT( first != NULL && first->length == exp->length[0] && memcmp( first->data, exp->data[0], exp->length[0] ) == 0,
                             "20: timeline between sequential reads" );
                lsmash_delete_sample( first );
            }
        }
        lsmash_sample_t *eof = NULL;
        TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &eof ) == 2, "20: EOF after the last frame" );
        TEST_ASSERT( lsmash_importer_get_last_delta( imp, 1 ) == chk->last_delta, "20: last delta of the sequential reader is unchanged" );
        lsmash_importer_close( imp );
        lsmash_destroy_root( root );
    }
    free( image );
    return 0;
}

static uint32_t build_adts_frame( uint8_t *dst, uint32_t payload_length, int crc, int channel_config, uint8_t seed )
{
    uint32_t header_length = crc ? 9 : 7;
    uint32_t frame_length  = header_length + payload_length;
    dst[0] = 0xFF;
    dst[1] = crc ? 0xF0 : 0xF1;     /* MPEG-4, layer 0, protection_absent */
    dst[2] = (uint8_t)(0x50 | ((channel_config >> 2) & 1));    /* AAC LC, 44.1 kHz */
    dst[3] = (uint8_t)(((channel_config & 3) << 6) | ((frame_length >> 11) & 3));
    dst[4] = (uint8_t)(frame_length >> 3);
    dst[5] = (uint8_t)(((frame_length & 7) << 5) | 0x1F);
    dst[6] = 0xFC;
    if( crc )
    {
        dst[7] = 0xA5;
        dst[8] = 0x5A;
    }
    fill_pattern( dst + header_length, payload_length, seed );
    return frame_length;
}

static uint32_t build_ac3_frame( uint8_t *dst, uint32_t frmsizecod, uint8_t seed )
{
    static const uint32_t size_table[3] = { 128, 160, 192 };    /* 32, 40 and 48 kbit/s at 48 kHz */
    uint32_t size = size_table[frmsizecod >> 1];
    fill_pattern( dst, size, seed );
    memset( dst, 0, 8 );
    dst[0] = 0x0B;
    dst[1] = 0x77;
    test_bit_writer_t bw = { dst + 4, 0 };
    bw_put( &bw, 2, 0 );            /* fscod: 48 kHz */
    bw_put( &bw, 6, frmsizecod );
    bw_put( &bw, 5, 8 );            /* bsid */
    bw_put( &bw, 3, 0 );            /* bsmod */
    bw_put( &bw, 3, 2 );            /* acmod: 2/0 */
    bw_put( &bw, 2, 0 );            /* dsurmod */
    bw_put( &bw, 1, 0 );            /* lfeon */
    return size;
}

static uint32_t build_eac3_frame( uint8_t *dst, uint32_t strmtyp, uint32_t size, uint8_t seed )
{
    fill_pattern( dst, size, seed );
    memset( dst, 0, 12 );
    dst[0] = 0x0B;
    dst[1] = 0x77;
    test_bit_writer_t bw = { dst + 2, 0 };
    bw_put( &bw, 2, strmtyp );
    bw_put( &bw, 3, 0 );            /* substreamid */
    bw_put( &bw, 11, size / 2 - 1 );
    bw_put( &bw, 2, 0 );            /* fscod: 48 kHz */
    bw_put( &bw, 2, 3 );            /* numblkscod: 6 blocks */
    bw_put( &bw, 3, 2 );            /* acmod: 2/0 */
    bw_put( &bw, 1, 0 );            /* lfeon */
    bw_put( &bw, 5, 16 );           /* bsid */
    bw_put( &bw, 5, 27 );           /* dialnorm */
    bw_put( &bw, 1, 0 );            /* compre */
    if( strmtyp == 1 )
        bw_put( &bw, 1, 0 );        /* chanmape */
    bw_put( &bw, 1, 0 );            /* mixmdate */
    bw_put( &bw, 1, 0 );            /* infomdate */
    bw_put( &bw, 1, 0 );            /* addbsie */
    return size;
}

static uint32_t build_dts_core_frame( uint8_t *dst, uint32_t size, uint8_t seed )
{
    fill_pattern( dst, size, seed );
    memset( dst, 0, 16 );
    dst[0] = 0x7F;
    dst[1] = 0xFE;
    dst[2] = 0x80;
    dst[3] = 0x01;
    test_bit_writer_t bw = { dst + 4, 0 };
    bw_put( &bw, 1, 1 );            /* FTYPE: normal frame */
    bw_put( &bw, 5, 31 );           /* SHORT */
    bw_put( &bw, 1, 0 );            /* CPF */
    bw_put( &bw, 7, 15 );           /* NBLKS: 16 blocks, 512 samples */
    bw_put( &bw, 14, size - 1 );    /* FSIZE */
    bw_put( &bw, 6, 2 );            /* AMODE: stereo */
    bw_put( &bw, 4, 13 );           /* SFREQ: 48 kHz */
    bw_put( &bw, 10, 0 );           /* RATE, MIX, DYNF, TIMEF, AUXF, HDCD */
    bw_put( &bw, 3, 0 );            /* EXT_AUDIO_ID */
    bw_put( &bw, 1, 0 );            /* EXT_AUDIO */
    bw_put( &bw, 1, 0 );            /* ASPF */
    bw_put( &bw, 2, 0 );            /* LFF */
    bw_put( &bw, 8, 0 );            /* HFLAG, FILTS, VERNUM, CHIST */
    bw_put( &bw, 3, 0 );            /* PCMR: 16 bits */
    bw_put( &bw, 6, 0 );            /* SUMF, SUMS, DIALNORM */
    return size;
}

static uint32_t build_mp3_frame( uint8_t *dst, int padding, int xing, uint16_t main_data_begin, uint8_t seed )
{
    uint32_t size = 417 + (padding ? 1 : 0);    /* MPEG-1 Layer III, 128 kbit/s, 44.1 kHz */
    fill_pattern( dst, size, seed );
    dst[0] = 0xFF;
    dst[1] = 0xFB;
    dst[2] = (uint8_t)(0x90 | (padding ? 0x02 : 0x00));
    dst[3] = 0x00;                  /* stereo */
    dst[4] = (uint8_t)(main_data_begin >> 1);
    dst[5] = (uint8_t)((main_data_begin & 1) << 7);
    if( xing )
    {
        memset( dst + 6, 0, size - 6 );
        memcpy( dst + 36, "Info", 4 );
    }
    return size;
}

static uint32_t build_amr_frame( uint8_t *dst, uint32_t frame_type, uint32_t size, uint8_t seed )
{
    fill_pattern( dst, size, seed );
    dst[0] = (uint8_t)((frame_type << 3) | 0x04);
    return size;
}

static uint32_t build_als_header( uint8_t *dst, uint32_t samples, uint8_t random_access, uint8_t ra_flag, uint32_t unit_count, const uint32_t *unit_sizes )
{
    memset( dst, 0, 64 );
    memcpy( dst, "ALS\0", 4 );
    put_be32( dst + 4, 44100 );
    put_be32( dst + 8, samples );
    put_be16( dst + 12, 1 );         /* two channels */
    dst[14] = 0x04;                         /* 16 bits */
    put_be16( dst + 15, 1023 );      /* 1024 samples per frame */
    dst[17] = random_access;
    dst[18] = (uint8_t)(ra_flag << 6);
    uint32_t pos = 22;
    put_be32( dst + pos, 0xFFFFFFFF );   /* no original header */
    pos += 4;
    put_be32( dst + pos, 0xFFFFFFFF );   /* no original trailer */
    pos += 4;
    if( ra_flag == 2 && random_access )
        for( uint32_t i = 0; i < unit_count; i++, pos += 4 )
            put_be32( dst + pos, unit_sizes[i] );
    return pos;
}

int test_read_file_audio_raw_streams( void )
{
    uint8_t        stream[4096];
    uint8_t        frame[AUDIO_EXPECT_MAX_LENGTH];
    uint32_t       pos;
    audio_expect_t *exp = (audio_expect_t *)malloc( sizeof(audio_expect_t) );
    TEST_ASSERT( exp != NULL, "20: allocate the expectation" );

    /*---- ADTS: a CRC protected frame and a mono frame at the end, which makes the stream change its sample entry ----*/
    {
        const char *path = "tests/tmp_test_fake_box.aac";
        static const uint32_t payload[4] = { 20, 24, 18, 30 };
        static const int      crc[4]     = { 0, 1, 0, 1 };
        static const int      config[4]  = { 2, 2, 2, 1 };
        memset( exp, 0, sizeof(*exp) );
        pos = 0;
        for( int i = 0; i < 4; i++ )
        {
            uint32_t length = build_adts_frame( frame, payload[i], crc[i], config[i], (uint8_t)(0x10 + i) );
            uint32_t header = crc[i] ? 9 : 7;
            expect_add_sample( exp, frame + header, length - header, (uint64_t)i * 1024, config[i] == 2 ? 1 : 2 );
            append_bytes( stream, &pos, frame, length );
        }
        TEST_ASSERT( write_test_file( path, stream, pos ) == 0, "20a: write adts" );
        audio_stream_check_t chk =
        {
            .path = path, .exp = exp, .sample_type = ISOM_CODEC_TYPE_MP4A_AUDIO, .frequency = 44100, .channels = 2, .channels2 = 1,
            .summary_count = 2, .media_duration = 4096, .last_delta = 1024
        };
        int ret = check_raw_audio_stream( &chk );
        remove( path );
        if( ret < 0 )
        {
            free( exp );
            return ret;
        }
    }

    /*---- AC-3: the bit rate of the stream changes in the middle ----*/
    {
        const char *path = "tests/tmp_test_fake_box.ac3";
        static const uint32_t frmsizecod[4] = { 0, 0, 2, 2 };
        memset( exp, 0, sizeof(*exp) );
        pos = 0;
        for( int i = 0; i < 4; i++ )
        {
            uint32_t length = build_ac3_frame( frame, frmsizecod[i], (uint8_t)(0x20 + i) );
            expect_add_sample( exp, frame, length, (uint64_t)i * 1536, i < 2 ? 1 : 2 );
            append_bytes( stream, &pos, frame, length );
        }
        TEST_ASSERT( write_test_file( path, stream, pos ) == 0, "20b: write ac-3" );
        audio_stream_check_t chk =
        {
            .path = path, .exp = exp, .sample_type = ISOM_CODEC_TYPE_AC_3_AUDIO, .frequency = 48000, .channels = 2, .channels2 = 2,
            .summary_count = 2, .media_duration = 6144, .last_delta = 1536
        };
        int ret = check_raw_audio_stream( &chk );
        remove( path );
        if( ret < 0 )
        {
            free( exp );
            return ret;
        }
    }

    /*---- E-AC-3: the second access unit has a dependent substream, so does its sample entry ----*/
    {
        const char *path = "tests/tmp_test_fake_box.eac3";
        memset( exp, 0, sizeof(*exp) );
        pos = 0;
        static const uint32_t indep_size[4] = { 128, 128, 160, 128 };
        static const uint32_t dep_size[4]   = { 0, 64, 0, 0 };
        for( int i = 0; i < 4; i++ )
        {
            uint8_t  au[AUDIO_EXPECT_MAX_LENGTH];
            uint32_t au_length = build_eac3_frame( au, 0, indep_size[i], (uint8_t)(0x30 + i) );
            if( dep_size[i] )
                au_length += build_eac3_frame( au + au_length, 1, dep_size[i], (uint8_t)(0x40 + i) );
            expect_add_sample( exp, au, au_length, (uint64_t)i * 1536, (i == 1) ? 2 : 1 );
            append_bytes( stream, &pos, au, au_length );
        }
        TEST_ASSERT( write_test_file( path, stream, pos ) == 0, "20c: write e-ac-3" );
        audio_stream_check_t chk =
        {
            .path = path, .exp = exp, .sample_type = ISOM_CODEC_TYPE_EC_3_AUDIO, .frequency = 48000, .channels = 2, .channels2 = 0,
            .summary_count = 2, .media_duration = 6144, .last_delta = 1536
        };
        int ret = check_raw_audio_stream( &chk );
        remove( path );
        if( ret < 0 )
        {
            free( exp );
            return ret;
        }
    }

    /*---- DTS: core substreams of different sizes ----*/
    {
        const char *path = "tests/tmp_test_fake_box.dts";
        static const uint32_t size[4] = { 128, 128, 160, 128 };
        memset( exp, 0, sizeof(*exp) );
        pos = 0;
        for( int i = 0; i < 4; i++ )
        {
            uint32_t length = build_dts_core_frame( frame, size[i], (uint8_t)(0x50 + i) );
            expect_add_sample( exp, frame, length, (uint64_t)i * 512, 1 );
            append_bytes( stream, &pos, frame, length );
        }
        TEST_ASSERT( write_test_file( path, stream, pos ) == 0, "20d: write dts" );
        audio_stream_check_t chk =
        {
            .path = path, .exp = exp, .sample_type = ISOM_CODEC_TYPE_DTSC_AUDIO, .frequency = 48000, .channels = 2, .channels2 = 0,
            .summary_count = 1, .media_duration = 2048, .last_delta = 512
        };
        int ret = check_raw_audio_stream( &chk );
        remove( path );
        if( ret < 0 )
        {
            free( exp );
            return ret;
        }
    }

    /*---- MP3: ID3v2 tag, Xing frame that is not a sample, bit reservoir dependency, ID3v1 tag ----*/
    {
        const char *path = "tests/tmp_test_fake_box.mp3";
        static const int      padding[4]         = { 0, 1, 0, 1 };
        static const uint16_t main_data_begin[4] = { 0, 1, 0, 3 };
        memset( exp, 0, sizeof(*exp) );
        pos = 0;
        static const uint8_t id3v2[16] = { 'I', 'D', '3', 4, 0, 0, 0, 0, 0, 6, 1, 2, 3, 4, 5, 6 };
        append_bytes( stream, &pos, id3v2, sizeof(id3v2) );
        uint32_t length = build_mp3_frame( frame, 0, 1, 0, 0x60 );     /* Xing */
        append_bytes( stream, &pos, frame, length );
        for( int i = 0; i < 4; i++ )
        {
            length = build_mp3_frame( frame, padding[i], 0, main_data_begin[i], (uint8_t)(0x61 + i) );
            expect_add_sample( exp, frame, length, (uint64_t)i * 1152, 1 );
            append_bytes( stream, &pos, frame, length );
        }
        uint8_t id3v1[128] = { 'T', 'A', 'G' };
        append_bytes( stream, &pos, id3v1, sizeof(id3v1) );
        TEST_ASSERT( write_test_file( path, stream, pos ) == 0, "20e: write mp3" );
        audio_stream_check_t chk =
        {
            .path = path, .exp = exp, .sample_type = ISOM_CODEC_TYPE_MP4A_AUDIO, .frequency = 44100, .channels = 2, .channels2 = 0,
            .summary_count = 1, .media_duration = 4608, .last_delta = 1152
        };
        int ret = check_raw_audio_stream( &chk );
        remove( path );
        if( ret < 0 )
        {
            free( exp );
            return ret;
        }
    }

    /*---- AMR-NB and AMR-WB ----*/
    {
        const char *path = "tests/tmp_test_fake_box.amr";
        static const uint32_t nb_type[4] = { 0, 7, 3, 7 };
        static const uint32_t nb_size[4] = { 13, 32, 18, 32 };
        memset( exp, 0, sizeof(*exp) );
        pos = 0;
        append_bytes( stream, &pos, (const uint8_t *)"#!AMR\n", 6 );
        for( int i = 0; i < 4; i++ )
        {
            uint32_t length = build_amr_frame( frame, nb_type[i], nb_size[i], (uint8_t)(0x70 + i) );
            expect_add_sample( exp, frame, length, (uint64_t)i * 160, 1 );
            append_bytes( stream, &pos, frame, length );
        }
        TEST_ASSERT( write_test_file( path, stream, pos ) == 0, "20f: write amr-nb" );
        audio_stream_check_t chk =
        {
            .path = path, .exp = exp, .sample_type = ISOM_CODEC_TYPE_SAMR_AUDIO, .frequency = 8000, .channels = 1, .channels2 = 0,
            .summary_count = 1, .media_duration = 640, .last_delta = 160
        };
        int ret = check_raw_audio_stream( &chk );
        remove( path );
        if( ret < 0 )
        {
            free( exp );
            return ret;
        }

        static const uint32_t wb_type[3] = { 0, 8, 2 };
        static const uint32_t wb_size[3] = { 18, 61, 33 };
        memset( exp, 0, sizeof(*exp) );
        pos = 0;
        append_bytes( stream, &pos, (const uint8_t *)"#!AMR-WB\n", 9 );
        for( int i = 0; i < 3; i++ )
        {
            uint32_t length = build_amr_frame( frame, wb_type[i], wb_size[i], (uint8_t)(0x78 + i) );
            expect_add_sample( exp, frame, length, (uint64_t)i * 320, 1 );
            append_bytes( stream, &pos, frame, length );
        }
        TEST_ASSERT( write_test_file( path, stream, pos ) == 0, "20f: write amr-wb" );
        audio_stream_check_t chk_wb =
        {
            .path = path, .exp = exp, .sample_type = ISOM_CODEC_TYPE_SAWB_AUDIO, .frequency = 16000, .channels = 1, .channels2 = 0,
            .summary_count = 1, .media_duration = 960, .last_delta = 320
        };
        ret = check_raw_audio_stream( &chk_wb );
        remove( path );
        if( ret < 0 )
        {
            free( exp );
            return ret;
        }
    }

    /*---- ALS: random access units with the size in the stream, with the size table in the header, and the whole stream as one unit ----*/
    {
        const char *path = "tests/tmp_test_fake_box.als";
        static const uint32_t unit_size[3] = { 50, 60, 40 };
        for( int variant = 0; variant < 3; variant++ )
        {
            memset( exp, 0, sizeof(*exp) );
            uint8_t  header[64];
            uint32_t header_length;
            pos = 0;
            if( variant == 2 )
            {
                header_length = build_als_header( header, 2500, 0, 1, 0, NULL );
                append_bytes( stream, &pos, header, header_length );
                uint32_t length = 90;
                fill_pattern( frame, length, 0x90 );
                expect_add_sample( exp, frame, length, 0, 1 );
                append_bytes( stream, &pos, frame, length );
            }
            else
            {
                header_length = build_als_header( header, 2500, 1, (uint8_t)(variant + 1), 3, unit_size );
                append_bytes( stream, &pos, header, header_length );
                for( int i = 0; i < 3; i++ )
                {
                    fill_pattern( frame, unit_size[i], (uint8_t)(0x80 + i) );
                    if( variant == 0 )
                    {
                        uint8_t size_field[4];
                        put_be32( size_field, unit_size[i] );
                        append_bytes( stream, &pos, size_field, 4 );
                    }
                    expect_add_sample( exp, frame, unit_size[i], (uint64_t)i * 1024, 1 );
                    append_bytes( stream, &pos, frame, unit_size[i] );
                }
            }
            TEST_ASSERT( write_test_file( path, stream, pos ) == 0, "20g: write als" );
            audio_stream_check_t chk =
            {
                .path = path, .exp = exp, .sample_type = ISOM_CODEC_TYPE_MP4A_AUDIO, .frequency = 44100, .channels = 2, .channels2 = 0,
                .summary_count = 1, .media_duration = 2500, .last_delta = (variant == 2) ? 2500 : 452
            };
            int ret = check_raw_audio_stream( &chk );
            remove( path );
            if( ret < 0 )
            {
                free( exp );
                return ret;
            }
        }
    }
    free( exp );
    printf( "[PASS] test_read_file_audio_raw_streams\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 21: H.264, HEVC and VC-1 raw streams through lsmash_read_file() and the Media Timeline
 *---------------------------------------------------------------------------*/
#define VIDEO_EXPECT_MAX_SAMPLES 12
#define VIDEO_EXPECT_MAX_LENGTH  192

typedef struct
{
    uint32_t count;
    uint32_t length[VIDEO_EXPECT_MAX_SAMPLES];
    uint32_t index [VIDEO_EXPECT_MAX_SAMPLES];  /* expected sample entry number of each sample */
    uint64_t pos   [VIDEO_EXPECT_MAX_SAMPLES];  /* where the first byte of each access unit is in the stream */
    uint8_t  data  [VIDEO_EXPECT_MAX_SAMPLES][VIDEO_EXPECT_MAX_LENGTH];
} video_expect_t;

typedef struct
{
    uint8_t        stream[VIDEO_EXPECT_MAX_SAMPLES * VIDEO_EXPECT_MAX_LENGTH * 2];
    uint32_t       size;
    video_expect_t exp;
} video_builder_t;

static void vb_begin_au( video_builder_t *vb, uint32_t index )
{
    video_expect_t *exp = &vb->exp;
    exp->index [exp->count] = index;
    exp->pos   [exp->count] = vb->size;
    exp->length[exp->count] = 0;
    ++exp->count;
}

static void vb_put_zero( video_builder_t *vb, uint32_t count )
{
    memset( vb->stream + vb->size, 0, count );
    vb->size += count;
}

/* Insert emulation_prevention_three_byte so that no start code appears in the payload. */
static uint32_t escape_rbsp( uint8_t *dst, const uint8_t *src, uint32_t length )
{
    uint32_t out   = 0;
    uint32_t zeros = 0;
    for( uint32_t i = 0; i < length; i++ )
    {
        if( zeros >= 2 && src[i] <= 3 )
        {
            dst[out++] = 3;
            zeros      = 0;
        }
        dst[out++] = src[i];
        zeros      = src[i] == 0 ? zeros + 1 : 0;
    }
    return out;
}

/* Put a NAL unit into the Annex B stream. If it is a part of the sample, the expectation gets it with the 4 byte length. */
static void vb_add_nal
(
    video_builder_t *vb,
    int              long_start_code,
    const uint8_t   *header,
    uint32_t         header_length,
    const uint8_t   *rbsp,
    uint32_t         rbsp_length,
    int              in_sample,
    uint32_t         zeros_after
)
{
    uint8_t  nal[VIDEO_EXPECT_MAX_LENGTH];
    memcpy( nal, header, header_length );
    uint32_t nal_length = header_length + escape_rbsp( nal + header_length, rbsp, rbsp_length );
    if( long_start_code )
        vb->stream[vb->size++] = 0;
    vb->stream[vb->size++] = 0;
    vb->stream[vb->size++] = 0;
    vb->stream[vb->size++] = 1;
    memcpy( vb->stream + vb->size, nal, nal_length );
    vb->size += nal_length;
    if( in_sample )
    {
        uint32_t i = vb->exp.count - 1;
        put_be32( vb->exp.data[i] + vb->exp.length[i], nal_length );
        memcpy( vb->exp.data[i] + vb->exp.length[i] + 4, nal, nal_length );
        vb->exp.length[i] += 4 + nal_length;
    }
    vb_put_zero( vb, zeros_after );
}

/* Put an EBDU into the VC-1 stream. Every EBDU is a part of the sample as it is. */
static void vb_add_ebdu( video_builder_t *vb, uint8_t bdu_type, const uint8_t *rbdu, uint32_t rbdu_length, uint32_t zeros_after )
{
    uint8_t  ebdu[VIDEO_EXPECT_MAX_LENGTH];
    ebdu[0] = 0;
    ebdu[1] = 0;
    ebdu[2] = 1;
    ebdu[3] = bdu_type;
    uint32_t ebdu_length = 4 + escape_rbsp( ebdu + 4, rbdu, rbdu_length );
    memcpy( vb->stream + vb->size, ebdu, ebdu_length );
    vb->size += ebdu_length;
    uint32_t i = vb->exp.count - 1;
    memcpy( vb->exp.data[i] + vb->exp.length[i], ebdu, ebdu_length );
    vb->exp.length[i] += ebdu_length;
    vb_put_zero( vb, zeros_after );
}

static uint32_t build_h264_sps( uint8_t *rbsp, uint32_t width_in_mbs )
{
    memset( rbsp, 0, 64 );
    test_bit_writer_t bw = { rbsp, 0 };
    bw_put( &bw, 8, 77 );                   /* profile_idc: Main */
    bw_put( &bw, 8, 0 );                    /* constraint_set_flags */
    bw_put( &bw, 8, 30 );                   /* level_idc */
    bw_put_ue( &bw, 0 );                    /* seq_parameter_set_id */
    bw_put_ue( &bw, 0 );                    /* log2_max_frame_num_minus4 */
    bw_put_ue( &bw, 0 );                    /* pic_order_cnt_type */
    bw_put_ue( &bw, 4 );                    /* log2_max_pic_order_cnt_lsb_minus4 */
    bw_put_ue( &bw, 2 );                    /* max_num_ref_frames */
    bw_put( &bw, 1, 0 );                    /* gaps_in_frame_num_value_allowed_flag */
    bw_put_ue( &bw, width_in_mbs - 1 );     /* pic_width_in_mbs_minus1 */
    bw_put_ue( &bw, 3 );                    /* pic_height_in_map_units_minus1 */
    bw_put( &bw, 1, 1 );                    /* frame_mbs_only_flag */
    bw_put( &bw, 1, 1 );                    /* direct_8x8_inference_flag */
    bw_put( &bw, 1, 0 );                    /* frame_cropping_flag */
    bw_put( &bw, 1, 1 );                    /* vui_parameters_present_flag */
    bw_put( &bw, 1, 0 );                    /* aspect_ratio_info_present_flag */
    bw_put( &bw, 1, 0 );                    /* overscan_info_present_flag */
    bw_put( &bw, 1, 0 );                    /* video_signal_type_present_flag */
    bw_put( &bw, 1, 0 );                    /* chroma_loc_info_present_flag */
    bw_put( &bw, 1, 1 );                    /* timing_info_present_flag */
    bw_put( &bw, 32, 1001 );                /* num_units_in_tick */
    bw_put( &bw, 32, 60000 );               /* time_scale */
    bw_put( &bw, 1, 1 );                    /* fixed_frame_rate_flag */
    bw_put( &bw, 1, 0 );                    /* nal_hrd_parameters_present_flag */
    bw_put( &bw, 1, 0 );                    /* vcl_hrd_parameters_present_flag */
    bw_put( &bw, 1, 0 );                    /* pic_struct_present_flag */
    bw_put( &bw, 1, 0 );                    /* bitstream_restriction_flag */
    bw_put_trailing( &bw );
    return bw.pos / 8;
}

static uint32_t build_h264_pps( uint8_t *rbsp )
{
    memset( rbsp, 0, 32 );
    test_bit_writer_t bw = { rbsp, 0 };
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
    return bw.pos / 8;
}

/* slice_type: 7 = I, 5 = P, 6 = B. A B slice is not a reference picture (nal_ref_idc is 0). */
static uint32_t build_h264_slice( uint8_t *rbsp, int idr, uint32_t slice_type, uint32_t frame_num, uint32_t idr_pic_id, uint32_t poc_lsb, uint8_t seed )
{
    memset( rbsp, 0, 32 );
    test_bit_writer_t bw = { rbsp, 0 };
    bw_put_ue( &bw, 0 );                    /* first_mb_in_slice */
    bw_put_ue( &bw, slice_type );           /* slice_type */
    bw_put_ue( &bw, 0 );                    /* pic_parameter_set_id */
    bw_put( &bw, 4, frame_num );            /* frame_num */
    if( idr )
        bw_put_ue( &bw, idr_pic_id );       /* idr_pic_id */
    bw_put( &bw, 8, poc_lsb );              /* pic_order_cnt_lsb */
    if( slice_type == 6 )
        bw_put( &bw, 1, 1 );                /* direct_spatial_mv_pred_flag */
    if( slice_type != 7 )
        bw_put( &bw, 1, 0 );                /* num_ref_idx_active_override_flag */
    if( slice_type != 7 )
        bw_put( &bw, 1, 0 );                /* ref_pic_list_modification_flag_l0 */
    if( slice_type == 6 )
        bw_put( &bw, 1, 0 );                /* ref_pic_list_modification_flag_l1 */
    if( idr )
    {
        bw_put( &bw, 1, 0 );                /* no_output_of_prior_pics_flag */
        bw_put( &bw, 1, 0 );                /* long_term_reference_flag */
    }
    else if( slice_type != 6 )
        bw_put( &bw, 1, 0 );                /* adaptive_ref_pic_marking_mode_flag */
    bw_put( &bw, 1, 1 );                    /* slice_qp_delta (se: 0) */
    bw_put_ue( &bw, 1 );                    /* disable_deblocking_filter_idc */
    bw_put( &bw, 8, seed );                 /* slice data */
    bw_put( &bw, 8, (uint32_t)(seed ^ 0x5A) );
    bw_put_trailing( &bw );
    return bw.pos / 8;
}

/* One access unit of H.264: a slice with the NAL units around it. The parameter sets are not a part of the sample. */
static void build_h264_au
(
    video_builder_t *vb,
    uint32_t         index,
    int              with_aud,
    uint32_t         width_in_mbs,      /* the parameter sets are put if this is not 0 */
    int              idr,
    uint32_t         slice_type,
    uint32_t         frame_num,
    uint32_t         idr_pic_id,
    uint32_t         poc_lsb,
    uint8_t          seed,
    uint32_t         zeros_after
)
{
    uint8_t  rbsp[96];
    uint32_t length;
    vb_begin_au( vb, index );
    if( with_aud )
    {
        static const uint8_t aud_header[1]  = { 0x09 };
        static const uint8_t aud_payload[1] = { 0xF0 };    /* primary_pic_type: any, and the stop bit */
        vb_add_nal( vb, 1, aud_header, 1, aud_payload, 1, 0, 0 );
    }
    if( width_in_mbs )
    {
        static const uint8_t sps_header[1] = { 0x67 };
        static const uint8_t pps_header[1] = { 0x68 };
        length = build_h264_sps( rbsp, width_in_mbs );
        vb_add_nal( vb, 1, sps_header, 1, rbsp, length, 0, 0 );
        length = build_h264_pps( rbsp );
        vb_add_nal( vb, 1, pps_header, 1, rbsp, length, 0, 0 );
    }
    if( slice_type != 6 )
    {
        /* A slice of a reference picture follows a SEI. A B slice is followed by filler data instead. */
        static const uint8_t sei_header[1] = { 0x06 };
        static const uint8_t sei_payload[] = { 0x05, 0x04, 0x4C, 0x53, 0x4D, 0x41, 0x80 };  /* user_data_unregistered of 4 bytes */
        vb_add_nal( vb, 0, sei_header, 1, sei_payload, sizeof(sei_payload), 1, 0 );
    }
    uint8_t slice_header[1] = { (uint8_t)(slice_type == 6 ? 0x01 : idr ? 0x65 : 0x41) };
    length = build_h264_slice( rbsp, idr, slice_type, frame_num, idr_pic_id, poc_lsb, seed );
    vb_add_nal( vb, 0, slice_header, 1, rbsp, length, 1, 0 );
    if( slice_type == 6 )
    {
        static const uint8_t fd_header[1]  = { 0x0C };
        static const uint8_t fd_payload[3] = { 0xFF, 0xFF, 0x80 };     /* filler data is not a part of the sample */
        vb_add_nal( vb, 0, fd_header, 1, fd_payload, sizeof(fd_payload), 0, 0 );
    }
    vb_put_zero( vb, zeros_after );
}
static void build_hevc_profile_tier_level( test_bit_writer_t *bw )
{
    bw_put( bw, 2, 0 );             /* general_profile_space */
    bw_put( bw, 1, 0 );             /* general_tier_flag */
    bw_put( bw, 5, 1 );             /* general_profile_idc: Main */
    bw_put( bw, 32, 0x60000000 );   /* general_profile_compatibility_flag[] */
    bw_put( bw, 1, 1 );             /* general_progressive_source_flag */
    bw_put( bw, 1, 0 );             /* general_interlaced_source_flag */
    bw_put( bw, 1, 0 );             /* general_non_packed_constraint_flag */
    bw_put( bw, 1, 1 );             /* general_frame_only_constraint_flag */
    bw_put( bw, 22, 0 );            /* general_reserved_zero_44bits */
    bw_put( bw, 22, 0 );
    bw_put( bw, 8, 93 );            /* general_level_idc */
}

static uint32_t build_hevc_vps( uint8_t *rbsp )
{
    memset( rbsp, 0, 64 );
    test_bit_writer_t bw = { rbsp, 0 };
    bw_put( &bw, 4, 0 );            /* vps_video_parameter_set_id */
    bw_put( &bw, 2, 3 );            /* vps_reserved_three_2bits */
    bw_put( &bw, 6, 0 );            /* vps_max_layers_minus1 */
    bw_put( &bw, 3, 0 );            /* vps_max_sub_layers_minus1 */
    bw_put( &bw, 1, 1 );            /* vps_temporal_id_nesting_flag */
    bw_put( &bw, 16, 0xFFFF );      /* vps_reserved_0xffff_16bits */
    build_hevc_profile_tier_level( &bw );
    bw_put( &bw, 1, 1 );            /* vps_sub_layer_ordering_info_present_flag */
    bw_put_ue( &bw, 2 );            /* vps_max_dec_pic_buffering_minus1 */
    bw_put_ue( &bw, 1 );            /* vps_max_num_reorder_pics */
    bw_put_ue( &bw, 0 );            /* vps_max_latency_increase_plus1 */
    bw_put( &bw, 6, 0 );            /* vps_max_layer_id */
    bw_put_ue( &bw, 0 );            /* vps_num_layer_sets_minus1 */
    bw_put( &bw, 1, 0 );            /* vps_timing_info_present_flag */
    bw_put( &bw, 1, 0 );            /* vps_extension_flag */
    bw_put_trailing( &bw );
    return bw.pos / 8;
}

static uint32_t build_hevc_sps( uint8_t *rbsp )
{
    memset( rbsp, 0, 96 );
    test_bit_writer_t bw = { rbsp, 0 };
    bw_put( &bw, 4, 0 );            /* sps_video_parameter_set_id */
    bw_put( &bw, 3, 0 );            /* sps_max_sub_layers_minus1 */
    bw_put( &bw, 1, 1 );            /* sps_temporal_id_nesting_flag */
    build_hevc_profile_tier_level( &bw );
    bw_put_ue( &bw, 0 );            /* sps_seq_parameter_set_id */
    bw_put_ue( &bw, 1 );            /* chroma_format_idc */
    bw_put_ue( &bw, 64 );           /* pic_width_in_luma_samples */
    bw_put_ue( &bw, 64 );           /* pic_height_in_luma_samples */
    bw_put( &bw, 1, 0 );            /* conformance_window_flag */
    bw_put_ue( &bw, 0 );            /* bit_depth_luma_minus8 */
    bw_put_ue( &bw, 0 );            /* bit_depth_chroma_minus8 */
    bw_put_ue( &bw, 4 );            /* log2_max_pic_order_cnt_lsb_minus4 */
    bw_put( &bw, 1, 1 );            /* sps_sub_layer_ordering_info_present_flag */
    bw_put_ue( &bw, 2 );            /* sps_max_dec_pic_buffering_minus1 */
    bw_put_ue( &bw, 1 );            /* sps_max_num_reorder_pics */
    bw_put_ue( &bw, 0 );            /* sps_max_latency_increase_plus1 */
    bw_put_ue( &bw, 0 );            /* log2_min_luma_coding_block_size_minus3 */
    bw_put_ue( &bw, 1 );            /* log2_diff_max_min_luma_coding_block_size */
    bw_put_ue( &bw, 0 );            /* log2_min_luma_transform_block_size_minus2 */
    bw_put_ue( &bw, 1 );            /* log2_diff_max_min_luma_transform_block_size */
    bw_put_ue( &bw, 0 );            /* max_transform_hierarchy_depth_inter */
    bw_put_ue( &bw, 0 );            /* max_transform_hierarchy_depth_intra */
    bw_put( &bw, 1, 0 );            /* scaling_list_enabled_flag */
    bw_put( &bw, 1, 0 );            /* amp_enabled_flag */
    bw_put( &bw, 1, 0 );            /* sample_adaptive_offset_enabled_flag */
    bw_put( &bw, 1, 0 );            /* pcm_enabled_flag */
    bw_put_ue( &bw, 1 );            /* num_short_term_ref_pic_sets */
    bw_put_ue( &bw, 0 );            /* num_negative_pics */
    bw_put_ue( &bw, 0 );            /* num_positive_pics */
    bw_put( &bw, 1, 0 );            /* long_term_ref_pics_present_flag */
    bw_put( &bw, 1, 0 );            /* sps_temporal_mvp_enabled_flag */
    bw_put( &bw, 1, 0 );            /* strong_intra_smoothing_enabled_flag */
    bw_put( &bw, 1, 1 );            /* vui_parameters_present_flag */
    bw_put( &bw, 1, 0 );            /* aspect_ratio_info_present_flag */
    bw_put( &bw, 1, 0 );            /* overscan_info_present_flag */
    bw_put( &bw, 1, 0 );            /* video_signal_type_present_flag */
    bw_put( &bw, 1, 0 );            /* chroma_loc_info_present_flag */
    bw_put( &bw, 1, 0 );            /* neutral_chroma_indication_flag */
    bw_put( &bw, 1, 0 );            /* field_seq_flag */
    bw_put( &bw, 1, 0 );            /* frame_field_info_present_flag */
    bw_put( &bw, 1, 0 );            /* default_display_window_flag */
    bw_put( &bw, 1, 1 );            /* vui_timing_info_present_flag */
    bw_put( &bw, 32, 1001 );        /* vui_num_units_in_tick */
    bw_put( &bw, 32, 30000 );       /* vui_time_scale */
    bw_put( &bw, 1, 0 );            /* vui_poc_proportional_to_timing_flag */
    bw_put( &bw, 1, 0 );            /* vui_hrd_parameters_present_flag */
    bw_put( &bw, 1, 0 );            /* bitstream_restriction_flag */
    bw_put( &bw, 1, 0 );            /* sps_extension_present_flag */
    bw_put_trailing( &bw );
    return bw.pos / 8;
}

static uint32_t build_hevc_pps( uint8_t *rbsp )
{
    memset( rbsp, 0, 32 );
    test_bit_writer_t bw = { rbsp, 0 };
    bw_put_ue( &bw, 0 );            /* pps_pic_parameter_set_id */
    bw_put_ue( &bw, 0 );            /* pps_seq_parameter_set_id */
    bw_put( &bw, 1, 0 );            /* dependent_slice_segments_enabled_flag */
    bw_put( &bw, 1, 0 );            /* output_flag_present_flag */
    bw_put( &bw, 3, 0 );            /* num_extra_slice_header_bits */
    bw_put( &bw, 1, 0 );            /* sign_data_hiding_enabled_flag */
    bw_put( &bw, 1, 0 );            /* cabac_init_present_flag */
    bw_put_ue( &bw, 0 );            /* num_ref_idx_l0_default_active_minus1 */
    bw_put_ue( &bw, 0 );            /* num_ref_idx_l1_default_active_minus1 */
    bw_put( &bw, 1, 1 );            /* init_qp_minus26 (se: 0) */
    bw_put( &bw, 1, 0 );            /* constrained_intra_pred_flag */
    bw_put( &bw, 1, 0 );            /* transform_skip_enabled_flag */
    bw_put( &bw, 1, 0 );            /* cu_qp_delta_enabled_flag */
    bw_put( &bw, 1, 1 );            /* pps_cb_qp_offset (se: 0) */
    bw_put( &bw, 1, 1 );            /* pps_cr_qp_offset (se: 0) */
    bw_put( &bw, 1, 0 );            /* pps_slice_chroma_qp_offsets_present_flag */
    bw_put( &bw, 1, 0 );            /* weighted_pred_flag */
    bw_put( &bw, 1, 0 );            /* weighted_bipred_flag */
    bw_put( &bw, 1, 0 );            /* transquant_bypass_enabled_flag */
    bw_put( &bw, 1, 0 );            /* tiles_enabled_flag */
    bw_put( &bw, 1, 0 );            /* entropy_coding_sync_enabled_flag */
    bw_put( &bw, 1, 0 );            /* pps_loop_filter_across_slices_enabled_flag */
    bw_put( &bw, 1, 0 );            /* deblocking_filter_control_present_flag */
    bw_put( &bw, 1, 0 );            /* pps_scaling_list_data_present_flag */
    bw_put( &bw, 1, 0 );            /* lists_modification_present_flag */
    bw_put_ue( &bw, 0 );            /* log2_parallel_merge_level_minus2 */
    bw_put( &bw, 1, 0 );            /* slice_segment_header_extension_present_flag */
    bw_put( &bw, 1, 0 );            /* pps_extension_present_flag */
    bw_put_trailing( &bw );
    return bw.pos / 8;
}

/* slice_type: 2 = I, 1 = P, 0 = B */
static uint32_t build_hevc_slice( uint8_t *rbsp, int irap, uint32_t slice_type, uint32_t poc_lsb, uint8_t seed )
{
    memset( rbsp, 0, 32 );
    test_bit_writer_t bw = { rbsp, 0 };
    bw_put( &bw, 1, 1 );                    /* first_slice_segment_in_pic_flag */
    if( irap )
        bw_put( &bw, 1, 0 );                /* no_output_of_prior_pics_flag */
    bw_put_ue( &bw, 0 );                    /* slice_pic_parameter_set_id */
    bw_put_ue( &bw, slice_type );           /* slice_type */
    if( !irap )
    {
        bw_put( &bw, 8, poc_lsb );          /* slice_pic_order_cnt_lsb */
        bw_put( &bw, 1, 1 );                /* short_term_ref_pic_set_sps_flag */
    }
    bw_put( &bw, 8, seed );                 /* slice data */
    bw_put( &bw, 8, (uint32_t)(seed ^ 0x5A) );
    bw_put_trailing( &bw );
    return bw.pos / 8;
}

static void build_hevc_sei
(
    video_builder_t *vb,
    int              suffix,
    uint8_t          payload_type,
    const uint8_t   *payload,
    uint32_t         payload_size,
    int              in_sample
)
{
    uint8_t rbsp[64];
    uint32_t length = 0;
    rbsp[length++] = payload_type;
    rbsp[length++] = (uint8_t)payload_size;
    memcpy( rbsp + length, payload, payload_size );
    length += payload_size;
    rbsp[length++] = 0x80;
    static const uint8_t prefix_header[2] = { 0x4E, 0x01 };
    static const uint8_t suffix_header[2] = { 0x50, 0x01 };
    vb_add_nal( vb, 0, suffix ? suffix_header : prefix_header, 2, rbsp, length, in_sample, 0 );
}

static void build_hevc_au
(
    video_builder_t *vb,
    uint32_t         index,
    int              first,
    uint8_t          nal_type,
    uint32_t         slice_type,
    uint32_t         poc_lsb,
    uint8_t          seed,
    uint32_t         zeros_after
)
{
    uint8_t rbsp[96];
    uint32_t length;
    vb_begin_au( vb, index );
    static const uint8_t aud_header[2]  = { 0x46, 0x01 };
    static const uint8_t aud_payload[1] = { 0x50 };     /* pic_cnt_type and the stop bit */
    vb_add_nal( vb, 1, aud_header, 2, aud_payload, 1, 0, 0 );
    if( first )
    {
        static const uint8_t vps_header[2] = { 0x40, 0x01 };
        static const uint8_t sps_header[2] = { 0x42, 0x01 };
        static const uint8_t pps_header[2] = { 0x44, 0x01 };
        length = build_hevc_vps( rbsp );
        vb_add_nal( vb, 1, vps_header, 2, rbsp, length, 0, 0 );
        length = build_hevc_sps( rbsp );
        vb_add_nal( vb, 1, sps_header, 2, rbsp, length, 0, 0 );
        length = build_hevc_pps( rbsp );
        vb_add_nal( vb, 1, pps_header, 2, rbsp, length, 0, 0 );
        /* The mastering display colour volume goes into the configuration record, the other SEI stays in the sample. */
        static const uint8_t mastering_display[24] =
        {
            0x34, 0xC2, 0x86, 0x20, 0x1D, 0x4C, 0x0F, 0xA0, 0x42, 0x68, 0x3D, 0x13,
            0x3D, 0x13, 0x40, 0x42, 0x00, 0x98, 0x96, 0x80, 0x00, 0x00, 0x00, 0x32
        };
        build_hevc_sei( vb, 0, 137, mastering_display, sizeof(mastering_display), 0 );
        static const uint8_t user_data[6] = { 0x4C, 0x53, 0x4D, 0x41, 0x53, 0x48 };
        build_hevc_sei( vb, 0, 5, user_data, sizeof(user_data), 1 );
    }
    uint8_t slice_header[2] = { (uint8_t)(nal_type << 1), 0x01 };
    length = build_hevc_slice( rbsp, nal_type == 19, slice_type, poc_lsb, seed );
    vb_add_nal( vb, 0, slice_header, 2, rbsp, length, 1, 0 );
    if( nal_type == 1 && slice_type == 1 )
    {
        /* A suffix SEI of a P picture, which is a part of the sample. */
        static const uint8_t suffix_data[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
        build_hevc_sei( vb, 1, 132, suffix_data, sizeof(suffix_data), 1 );
    }
    if( nal_type == 0 && poc_lsb == 5 )
    {
        static const uint8_t fd_header[2]  = { 0x4C, 0x01 };
        static const uint8_t fd_payload[3] = { 0xFF, 0xFF, 0x80 };     /* filler data is not a part of the sample */
        vb_add_nal( vb, 0, fd_header, 2, fd_payload, sizeof(fd_payload), 0, 0 );
    }
    vb_put_zero( vb, zeros_after );
}

/* BDU type 0x0F (sequence header), 0x0E (entry point header), 0x0D (frame) and 0x0B (slice) of the Advanced Profile */
static uint32_t build_vc1_sequence_header( uint8_t *rbdu )
{
    memset( rbdu, 0, 32 );
    test_bit_writer_t bw = { rbdu, 0 };
    bw_put( &bw, 2, 3 );            /* profile: Advanced */
    bw_put( &bw, 3, 2 );            /* level */
    bw_put( &bw, 2, 1 );            /* colordiff_format */
    bw_put( &bw, 9, 0 );            /* frmrtq_postproc, bitrtq_postproc and postproc_flag */
    bw_put( &bw, 12, 31 );          /* max_coded_width */
    bw_put( &bw, 12, 31 );          /* max_coded_height */
    bw_put( &bw, 1, 0 );            /* pulldown_flag */
    bw_put( &bw, 1, 0 );            /* interlace */
    bw_put( &bw, 4, 0 );            /* tfcntrflag, finterpflag, reserved and psf */
    bw_put( &bw, 1, 1 );            /* display_ext */
    bw_put( &bw, 14, 63 );          /* disp_horiz_size_minus1 */
    bw_put( &bw, 14, 47 );          /* disp_vert_size_minus1 */
    bw_put( &bw, 1, 0 );            /* aspect_ratio_flag */
    bw_put( &bw, 1, 1 );            /* framerate_flag */
    bw_put( &bw, 1, 0 );            /* framerateind */
    bw_put( &bw, 8, 3 );            /* frameratenr: 30 */
    bw_put( &bw, 4, 2 );            /* frameratedr: 1001, that is 30000/1001 */
    bw_put( &bw, 1, 0 );            /* color_format_flag */
    bw_put( &bw, 1, 0 );            /* hrd_param_flag */
    bw_put_trailing( &bw );
    return bw.pos / 8;
}

static uint32_t build_vc1_entry_point_header( uint8_t *rbdu )
{
    memset( rbdu, 0, 16 );
    test_bit_writer_t bw = { rbdu, 0 };
    bw_put( &bw, 1, 0 );            /* broken_link_flag */
    bw_put( &bw, 1, 1 );            /* closed_entry_point */
    bw_put( &bw, 4, 0 );            /* panscan_flag, refdist_flag, loopfilter and fastuvmc */
    bw_put( &bw, 1, 0 );            /* extended_mv */
    bw_put( &bw, 6, 0 );            /* dquant, vstransform, overlap and quantizer */
    bw_put( &bw, 1, 0 );            /* coded_size_flag */
    bw_put( &bw, 1, 0 );            /* range_mapy_flag */
    bw_put( &bw, 1, 0 );            /* range_mapuv_flag */
    bw_put_trailing( &bw );
    return bw.pos / 8;
}

/* ptype: 0 = P, 2 = B, 6 = I as they are decoded from the variable length code */
static void build_vc1_au( video_builder_t *vb, uint32_t index, int with_sequence, uint32_t ptype, uint8_t seed, int with_slice, uint32_t zeros_after )
{
    uint8_t rbdu[40];
    vb_begin_au( vb, index );
    if( with_sequence )
    {
        uint32_t length = build_vc1_sequence_header( rbdu );
        vb_add_ebdu( vb, 0x0F, rbdu, length, 0 );
        length = build_vc1_entry_point_header( rbdu );
        vb_add_ebdu( vb, 0x0E, rbdu, length, 0 );
    }
    /* PTYPE is the variable length code 110 (I), 10 (B) or 0 (P), followed by the other fields of the picture layer. */
    uint8_t frame[4];
    frame[0] = ptype == 6 ? (uint8_t)(0xC0 | (seed & 0x1F)) : ptype == 2 ? (uint8_t)(0x80 | (seed & 0x3F)) : (uint8_t)(seed & 0x7F);
    frame[0] |= 0x01;
    frame[1] = (uint8_t)(seed ^ 0xA5);
    frame[2] = 0;       /* a single zero byte is allowed inside an EBDU */
    frame[3] = (uint8_t)(seed | 0x80);
    vb_add_ebdu( vb, 0x0D, frame, sizeof(frame), with_slice ? 0 : zeros_after );
    if( with_slice )
    {
        static const uint8_t slice[3] = { 0x77, 0x00, 0x81 };
        vb_add_ebdu( vb, 0x0B, slice, sizeof(slice), zeros_after );
    }
}

typedef struct
{
    const char           *path;
    const video_expect_t *exp;
    lsmash_codec_type_t   sample_type;
    uint32_t              timescale;        /* of the first summary, which is the media timescale of the fake track */
    uint32_t              timebase;
    uint32_t              last_delta;       /* of the sequential reader in the timebase */
    uint32_t              summary_count;
    uint32_t              width;
    uint32_t              height;
    uint32_t              width2;           /* width of the second sample entry if the stream changes its summary */
    int                   reordering;       /* some samples have a CTS that differs from the DTS */
} video_stream_check_t;

static int video_prop_equal( const lsmash_sample_property_t *a, const lsmash_sample_property_t *b )
{
    return a->ra_flags == b->ra_flags
        && a->post_roll.identifier == b->post_roll.identifier
        && a->post_roll.complete   == b->post_roll.complete
        && a->pre_roll.distance    == b->pre_roll.distance
        && a->allow_earlier == b->allow_earlier
        && a->leading       == b->leading
        && a->independent   == b->independent
        && a->disposable    == b->disposable
        && a->redundant     == b->redundant;
}

static int check_raw_video_stream( const video_stream_check_t *chk )
{
    const video_expect_t *exp = chk->exp;
    const char *path = chk->path;
    uint32_t max_length = 0;
    for( uint32_t i = 0; i < exp->count; i++ )
        max_length = LSMASH_MAX( max_length, exp->length[i] );
    FILE *fp = fopen( path, "rb" );
    TEST_ASSERT( fp != NULL, "21: open the written stream" );
    fclose( fp );

    /*---- The sequential reader is the reference of the stream. ----*/
    uint64_t                 ref_dts [VIDEO_EXPECT_MAX_SAMPLES] = { 0 };
    uint64_t                 ref_cts [VIDEO_EXPECT_MAX_SAMPLES] = { 0 };
    lsmash_sample_property_t ref_prop[VIDEO_EXPECT_MAX_SAMPLES] = { { 0 } };
    uint32_t                 ref_last_delta = 0;
    {
        lsmash_root_t *root = lsmash_create_root();
        importer_t *imp = lsmash_importer_open( root, path, "auto" );
        TEST_ASSERT( imp != NULL, "21: open the importer" );
        lsmash_video_summary_t *first = (lsmash_video_summary_t *)lsmash_duplicate_summary( imp, 1 );
        TEST_ASSERT( first != NULL, "21: summary of the importer" );
        TEST_ASSERT( first->timescale == chk->timescale && first->timebase == chk->timebase, "21: timescale and timebase of the summary" );
        TEST_ASSERT( lsmash_check_codec_type_identical( first->sample_type, chk->sample_type ) && first->width == chk->width && first->height == chk->height, "21: summary codec and size" );
        lsmash_cleanup_summary( (lsmash_summary_t *)first );
        for( uint32_t i = 0; i < exp->count; i++ )
        {
            lsmash_sample_t *sample = NULL;
            int ret = lsmash_importer_get_access_unit( imp, 1, &sample );
            TEST_ASSERT( ret >= 0 && sample != NULL, "21: reference access unit" );
            TEST_ASSERT( sample->length == exp->length[i] && memcmp( sample->data, exp->data[i], exp->length[i] ) == 0,
                         "21: reference access unit is the expected Length-Prefixed sample" );
            TEST_ASSERT( sample->pos == exp->pos[i], "21: reference access unit position is where it starts in the stream" );
            TEST_ASSERT( (ret > 0) == (i > 0 && exp->index[i] != exp->index[i - 1]), "21: the importer reports the change of the summary at the right sample" );
            ref_dts [i] = sample->dts;
            ref_cts [i] = sample->cts;
            ref_prop[i] = sample->prop;
            lsmash_delete_sample( sample );
        }
        lsmash_sample_t *eof = NULL;
        TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &eof ) == 2 && eof == NULL, "21: reference reader reaches EOF" );
        ref_last_delta = lsmash_importer_get_last_delta( imp, 1 );
        TEST_ASSERT( ref_last_delta == chk->last_delta, "21: reference last delta" );
        lsmash_importer_close( imp );
        lsmash_destroy_root( root );
    }
    TEST_ASSERT( ref_dts[0] == 0, "21: the first DTS is 0" );
    int reordered = 0;
    for( uint32_t i = 0; i < exp->count; i++ )
    {
        TEST_ASSERT( ref_cts[i] >= ref_dts[i], "21: CTS is not earlier than DTS" );
        reordered |= ref_cts[i] != ref_dts[i];
        if( i )
            TEST_ASSERT( ref_dts[i] > ref_dts[i - 1], "21: DTS increases" );
    }
    TEST_ASSERT( reordered == chk->reordering, "21: composition reordering of the stream" );

    /*---- lsmash_read_file() and the Media Timeline ----*/
    {
        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        TEST_ASSERT( root != NULL && lsmash_open_file( path, 1, &param ) == 0, "21: open the stream" );
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( file != NULL, "21: set the stream file" );
        TEST_ASSERT( lsmash_read_file( file, &param ) >= 0, "21: read_file succeeds on the raw stream via fake boxes" );
        lsmash_movie_parameters_t movie_param;
        TEST_ASSERT( lsmash_get_movie_parameters( root, &movie_param ) == 0 && movie_param.number_of_tracks == 1, "21: one fake track" );
        TEST_ASSERT( movie_param.timescale == chk->timescale, "21: movie timescale is the timescale of the summary" );
        uint32_t track_ID = lsmash_get_track_ID( root, 1 );
        TEST_ASSERT( track_ID != 0, "21: track ID" );
        TEST_ASSERT( lsmash_get_media_timescale( root, track_ID ) == chk->timescale, "21: media timescale is the timescale of the summary" );
        TEST_ASSERT( lsmash_count_summary( root, track_ID ) == 1, "21: one sample entry before the timeline is built" );
        lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_get_summary( root, track_ID, 1 );
        TEST_ASSERT( summary != NULL && lsmash_check_codec_type_identical( summary->sample_type, chk->sample_type ), "21: sample entry type" );
        TEST_ASSERT( summary->width == chk->width && summary->height == chk->height, "21: first summary size" );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        TEST_ASSERT( lsmash_construct_timeline( root, track_ID ) == 0, "21: construct timeline" );
        TEST_ASSERT( lsmash_count_summary( root, track_ID ) == chk->summary_count, "21: number of sample entries after the timeline is built" );
        if( chk->width2 )
        {
            summary = (lsmash_video_summary_t *)lsmash_get_summary( root, track_ID, 2 );
            TEST_ASSERT( summary != NULL && summary->width == chk->width2, "21: second summary size" );
            lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        }
        TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( root, track_ID ) == exp->count, "21: sample count" );
        TEST_ASSERT( lsmash_get_max_sample_size_in_media_timeline( root, track_ID ) == max_length, "21: max sample size" );
        uint32_t last_delta = 0;
        TEST_ASSERT( lsmash_get_last_sample_delta_from_media_timeline( root, track_ID, &last_delta ) == 0
                  && last_delta == ref_last_delta * chk->timebase, "21: last sample delta is the last delta of the sequential reader in the media timescale" );
        TEST_ASSERT( lsmash_get_media_duration_from_media_timeline( root, track_ID ) == (ref_dts[exp->count - 1] + ref_last_delta) * chk->timebase,
                     "21: media duration" );
        for( uint32_t k = 0; k < exp->count * 2; k++ )
        {
            uint32_t i = (k < exp->count) ? (exp->count - 1 - k) : (k - exp->count);
            lsmash_sample_t *sample = lsmash_get_sample_from_media_timeline( root, track_ID, i + 1 );
            TEST_ASSERT( sample != NULL, "21: timeline sample" );
            TEST_ASSERT( sample->length == exp->length[i] && memcmp( sample->data, exp->data[i], exp->length[i] ) == 0, "21: timeline sample data is the sequential access unit" );
            TEST_ASSERT( sample->dts == ref_dts[i] * chk->timebase && sample->cts == ref_cts[i] * chk->timebase, "21: timeline time stamps are the timebase multiples of the sequential ones" );
            TEST_ASSERT( sample->index == exp->index[i], "21: timeline sample entry number" );
            TEST_ASSERT( video_prop_equal( &sample->prop, &ref_prop[i] ), "21: timeline sample property equals the sequential reader" );
            TEST_ASSERT( sample->pos == exp->pos[i], "21: sample position is where the access unit starts in the stream" );
            lsmash_delete_sample( sample );
            lsmash_sample_t info = { 0 };
            uint64_t dts = UINT64_MAX;
            uint64_t cts = UINT64_MAX;
            lsmash_sample_property_t prop = { 0 };
            TEST_ASSERT( lsmash_get_sample_info_from_media_timeline( root, track_ID, i + 1, &info ) == 0
                      && info.dts == ref_dts[i] * chk->timebase && info.length == exp->length[i] && info.index == exp->index[i], "21: sample info" );
            TEST_ASSERT( lsmash_get_dts_from_media_timeline( root, track_ID, i + 1, &dts ) == 0 && dts == ref_dts[i] * chk->timebase, "21: dts getter" );
            TEST_ASSERT( lsmash_get_cts_from_media_timeline( root, track_ID, i + 1, &cts ) == 0 && cts == ref_cts[i] * chk->timebase, "21: cts getter" );
            TEST_ASSERT( lsmash_get_sample_property_from_media_timeline( root, track_ID, i + 1, &prop ) == 0 && video_prop_equal( &prop, &ref_prop[i] ), "21: property getter" );
        }
        TEST_ASSERT( lsmash_get_sample_from_media_timeline( root, track_ID, exp->count + 1 ) == NULL, "21: no sample past the end" );
        TEST_ASSERT( lsmash_construct_timeline( root, track_ID ) == 0
                  && lsmash_get_sample_count_in_media_timeline( root, track_ID ) == exp->count, "21: constructing twice is harmless" );
        lsmash_destruct_timeline( root, track_ID );
        lsmash_close_file( &param );
        lsmash_destroy_root( root );
    }

    /*---- The sequential reader must continue from where it stopped after the Media Timeline moved the stream. ----*/
    {
        lsmash_root_t *root = lsmash_create_root();
        importer_t *imp = lsmash_importer_open( root, path, "auto" );
        TEST_ASSERT( imp != NULL, "21: open the importer again" );
        uint32_t track_ID = lsmash_get_track_ID( root, 1 );
        uint32_t before = exp->count / 2;
        for( uint32_t i = 0; i < before; i++ )
        {
            lsmash_sample_t *sample = NULL;
            TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &sample ) >= 0 && sample != NULL, "21: access unit before timeline" );
            TEST_ASSERT( sample->length == exp->length[i] && memcmp( sample->data, exp->data[i], exp->length[i] ) == 0, "21: access unit data before timeline" );
            lsmash_delete_sample( sample );
        }
        TEST_ASSERT( lsmash_importer_construct_timeline( imp, 1 ) == 0, "21: construct timeline in the middle of reading" );
        for( uint32_t k = 0; k < exp->count; k++ )
        {
            uint32_t i = exp->count - 1 - k;
            lsmash_sample_t *sample = lsmash_get_sample_from_media_timeline( root, track_ID, i + 1 );
            TEST_ASSERT( sample != NULL && sample->length == exp->length[i] && memcmp( sample->data, exp->data[i], exp->length[i] ) == 0,
                         "21: timeline sample read in the middle of reading" );
            lsmash_delete_sample( sample );
        }
        for( uint32_t i = before; i < exp->count; i++ )
        {
            lsmash_sample_t *sample = NULL;
            int ret = lsmash_importer_get_access_unit( imp, 1, &sample );
            TEST_ASSERT( ret >= 0 && sample != NULL, "21: access unit after timeline" );
            TEST_ASSERT( sample->length == exp->length[i] && memcmp( sample->data, exp->data[i], exp->length[i] ) == 0
                      && sample->dts == ref_dts[i] && sample->cts == ref_cts[i] && video_prop_equal( &sample->prop, &ref_prop[i] ),
                         "21: access unit continues from where it stopped with the same time stamps and properties" );
            TEST_ASSERT( (ret > 0) == (i > 0 && exp->index[i] != exp->index[i - 1]), "21: change of the summary is still reported after the timeline" );
            lsmash_delete_sample( sample );
            if( i == before )
            {
                lsmash_sample_t *first = lsmash_get_sample_from_media_timeline( root, track_ID, 1 );
                TEST_ASSERT( first != NULL && first->length == exp->length[0] && memcmp( first->data, exp->data[0], exp->length[0] ) == 0,
                             "21: timeline between sequential reads" );
                lsmash_delete_sample( first );
            }
        }
        lsmash_sample_t *eof = NULL;
        TEST_ASSERT( lsmash_importer_get_access_unit( imp, 1, &eof ) == 2, "21: EOF after the last access unit" );
        TEST_ASSERT( lsmash_importer_get_last_delta( imp, 1 ) == ref_last_delta, "21: last delta of the sequential reader is unchanged" );
        lsmash_importer_close( imp );
        lsmash_destroy_root( root );
    }
    return 0;
}

int test_read_file_video_raw_streams( void )
{
    video_builder_t *vb = (video_builder_t *)calloc( 1, sizeof(video_builder_t) );
    TEST_ASSERT( vb != NULL, "21: allocate the builder" );

    /*---- H.264: POC reordering with B pictures, AUD, SEI, filler data, 3 and 4 byte start codes, trailing zero bytes and a change of SPS ----*/
    {
        const char *path = "tests/tmp_test_fake_box.h264";
        vb_put_zero( vb, 1 );   /* a leading zero byte in front of the first start code */
        /* decoding order: I0 P3 B1 B2 P6 B4 B5, then another sequence with another picture size: I0 P1 */
        static const struct { int idr; uint32_t type; uint32_t frame_num; uint32_t poc; uint32_t zeros; } au[9] =
        {
            { 1, 7, 0,  0, 2 }, { 0, 5, 1,  6, 0 }, { 0, 6, 2,  2, 0 }, { 0, 6, 2,  4, 3 }, { 0, 5, 2, 12, 0 },
            { 0, 6, 3,  8, 0 }, { 0, 6, 3, 10, 0 }, { 1, 7, 0,  0, 0 }, { 0, 5, 1,  2, 0 }
        };
        for( int i = 0; i < 9; i++ )
            build_h264_au( vb, i < 7 ? 1 : 2, 1, i == 0 ? 4 : i == 7 ? 5 : 0, au[i].idr, au[i].type, au[i].frame_num,
                           i < 7 ? 0 : 1, au[i].poc, (uint8_t)(0x31 + i), au[i].zeros );
        TEST_ASSERT( vb->exp.pos[0] == 1, "21a: the first access unit starts at the start code" );
        TEST_ASSERT( write_test_file( path, vb->stream, vb->size ) == 0, "21a: write h264" );
        video_stream_check_t chk =
        {
            .path = path, .exp = &vb->exp, .sample_type = ISOM_CODEC_TYPE_AVC1_VIDEO, .timescale = 30000, .timebase = 1001, .last_delta = 1,
            .summary_count = 2, .width = 64, .height = 64, .width2 = 80, .reordering = 1
        };
        int ret = check_raw_video_stream( &chk );
        remove( path );
        if( ret < 0 )
        {
            free( vb );
            return ret;
        }
        memset( vb, 0, sizeof(*vb) );
    }

    /*---- HEVC: VPS, SPS and PPS, mastering display SEI that goes to the configuration record, prefix and suffix SEI, filler data ----*/
    {
        const char *path = "tests/tmp_test_fake_box.hevc";
        /* decoding order: IDR0 P3 B1 B2 P6 B4 B5 */
        static const struct { uint8_t nal_type; uint32_t slice_type; uint32_t poc; uint32_t zeros; } au[7] =
        {
            { 19, 2, 0, 2 }, { 1, 1, 3, 0 }, { 0, 0, 1, 0 }, { 0, 0, 2, 3 }, { 1, 1, 6, 0 }, { 0, 0, 4, 0 }, { 0, 0, 5, 0 }
        };
        for( int i = 0; i < 7; i++ )
            build_hevc_au( vb, 1, i == 0, au[i].nal_type, au[i].slice_type, au[i].poc, (uint8_t)(0x41 + i), au[i].zeros );
        TEST_ASSERT( write_test_file( path, vb->stream, vb->size ) == 0, "21b: write hevc" );
        video_stream_check_t chk =
        {
            .path = path, .exp = &vb->exp, .sample_type = ISOM_CODEC_TYPE_HVC1_VIDEO, .timescale = 30000, .timebase = 1001, .last_delta = 1,
            .summary_count = 1, .width = 64, .height = 64, .width2 = 0, .reordering = 1
        };
        int ret = check_raw_video_stream( &chk );
        remove( path );
        if( ret < 0 )
        {
            free( vb );
            return ret;
        }
        memset( vb, 0, sizeof(*vb) );
    }

    /*---- VC-1 Advanced Profile: sequence header, entry point header, frames and a slice with zero bytes between EBDUs ----*/
    {
        const char *path = "tests/tmp_test_fake_box.vc1";
        vb_put_zero( vb, 3 );   /* leading zero bytes in front of the first start code */
        /* decoding order: I P B B P B */
        static const struct { uint32_t ptype; int slice; uint32_t zeros; } au[6] =
        {
            { 6, 0, 2 }, { 0, 0, 0 }, { 2, 1, 5 }, { 2, 0, 0 }, { 0, 0, 1 }, { 2, 0, 0 }
        };
        for( int i = 0; i < 6; i++ )
            build_vc1_au( vb, 1, i == 0, au[i].ptype, (uint8_t)(0x21 + i), au[i].slice, au[i].zeros );
        TEST_ASSERT( vb->exp.pos[0] == 3, "21c: the first access unit starts at the start code" );
        TEST_ASSERT( write_test_file( path, vb->stream, vb->size ) == 0, "21c: write vc-1" );
        video_stream_check_t chk =
        {
            .path = path, .exp = &vb->exp, .sample_type = ISOM_CODEC_TYPE_VC_1_VIDEO, .timescale = 30000, .timebase = 1001, .last_delta = 1,
            .summary_count = 1, .width = 64, .height = 48, .width2 = 0, .reordering = 1
        };
        int ret = check_raw_video_stream( &chk );
        remove( path );
        if( ret < 0 )
        {
            free( vb );
            return ret;
        }
    }
    free( vb );
    printf( "[PASS] test_read_file_video_raw_streams\n" );
    return 0;
}
