/*****************************************************************************
 * h264muxer.c:
 *****************************************************************************
 * Copyright (C) 2011 L-SMASH project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *****************************************************************************/

/* This file is available under an ISC license. */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "lsmash.h"
#include "importer.h"

#define eprintf( ... ) fprintf( stderr, __VA_ARGS__ )

typedef struct
{
    mp4sys_importer_t *importer;
    lsmash_video_summary_t *summary;
    lsmash_root_t *root;
} structs_t;

static void cleanup_structs( structs_t *structs )
{
    if( !structs )
        return;
    if( structs->root )
        lsmash_destroy_root( structs->root );
    if( structs->summary )
        lsmash_cleanup_video_summary( structs->summary );
    if( structs->importer )
        mp4sys_importer_close( structs->importer );
}

static int h264mux_error( structs_t *structs, char *msg )
{
    cleanup_structs( structs );
    eprintf( msg );
    return -1;
}

#define H264MUX_ERR( msg ) h264mux_error( &structs, msg )
#define H264MUX_USAGE_ERR() H264MUX_ERR( \
    "Usage: h264muxer [options] input output\n" \
    "Options:\n" \
    "    --isom-version <integer> Specify maximum compatible ISO Base Media version\n" \
    "    --3gp                    Enable 3GPP muxing mode\n" \
    "    --3g2                    Enable 3GPP2 muxing mode\n" \
    "    --qt                     Enable QuickTime file format muxing mode\n" \
    "    --chimera                Allow chimera of ISO Base Media and QTFF\n" \
    "    --fps <int/int>          Specify video framerate\n" \
    "    --shift-timeline         Enable composition to decode timeline shift\n" \
    "  Note: --3gp and 3g2 are exclusive.\n" \
    "        --qt overrides all brands by itself unless you specify --chimera.\n" \
)

static const struct
{
    uint32_t timescale;
    uint32_t timebase;
} well_known_fps[]
    = {
        { 24000, 1001 }, { 30000, 1001 }, { 60000, 1001 }, { 120000, 1001 }, { 72000, 1001 },
        { 25, 1 }, { 50, 1 }, { 24, 1 }, { 30, 1 }, { 60, 1 }, { 120, 1 }, { 72, 1 }, { 0, 0 }
      };

int main( int argc, char *argv[] )
{
    structs_t structs = { 0 };

    if( argc < 3 )
        return H264MUX_USAGE_ERR();

    int i = 1;
    int isom_version = 1;
    int timeline_shift = 0;
    int brand_3gx = 0;
    int chimera = 0;
    int qtff = 0;
    int user_fps = 0;
    uint32_t fps_num = 0, fps_den = 0;
    uint32_t brands[12] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_AVC1 };
    uint32_t major_brand = ISOM_BRAND_TYPE_MP42;
    int num_of_brands = 2;
    uint32_t minor_version = 0;
    while( argc > i && *argv[i] == '-' )
    {
        if( !strcasecmp( argv[i], "--isom-version" ) )
        {
            if( argc == ++i )
                return H264MUX_USAGE_ERR();
            isom_version = atoi( argv[i] );
        }
        else if( !strcasecmp( argv[i], "--3gp" ) )
        {
            if( brand_3gx )
                return H264MUX_USAGE_ERR();
            brand_3gx = 1;
        }
        else if( !strcasecmp( argv[i], "--3g2" ) )
        {
            if( brand_3gx )
                return H264MUX_USAGE_ERR();
            brand_3gx = 2;
        }
        else if( !strcasecmp( argv[i], "--qt" ) )
            qtff = 1;
        else if( !strcasecmp( argv[i], "--chimera" ) )
            chimera = 1;
        else if( !strcasecmp( argv[i], "--fps" ) )
        {
            if( argc == ++i )
                return H264MUX_USAGE_ERR();
            if( sscanf( argv[i], "%"SCNu32"/%"SCNu32, &fps_num, &fps_den ) == 1 )
            {
                fps_num = atoi( argv[i] );
                fps_den = 1;
            }
            user_fps = 1;
        }
        else if( !strcasecmp( argv[i], "--shift-timeline" ) )
            timeline_shift = 1;
        else
            return H264MUX_USAGE_ERR();
        i++;
    }
    if( argc != i + 2 )
        return H264MUX_USAGE_ERR();

    if( timeline_shift && !qtff && isom_version < 4 )
        return H264MUX_ERR( "Timeline shift requires --qt, or --isom-version 4 or later.\n" );

    if( !qtff || chimera )
    {
        if( isom_version > 6 )
        {
            eprintf( "Unknown ISO Base Media version.\n" );
            return H264MUX_USAGE_ERR();
        }
        if( isom_version >= 2 )
            brands[num_of_brands++] = ISOM_BRAND_TYPE_ISO2;
        if( isom_version >= 3 )
            brands[num_of_brands++] = ISOM_BRAND_TYPE_ISO3;
        if( isom_version >= 4 )
            brands[num_of_brands++] = ISOM_BRAND_TYPE_ISO4;
        if( isom_version >= 5 )
            brands[num_of_brands++] = ISOM_BRAND_TYPE_ISO5;
        if( isom_version == 6 )
            brands[num_of_brands++] = ISOM_BRAND_TYPE_ISO6;

        brands[num_of_brands++] = ISOM_BRAND_TYPE_MP41;
        brands[num_of_brands++] = ISOM_BRAND_TYPE_MP42;

        if( qtff && chimera )
            eprintf( "Using ISOM+QTFF muxing mode.\n" );
        else if( brand_3gx == 1 )
        {
            major_brand = ISOM_BRAND_TYPE_3GP6;
            brands[num_of_brands++] = ISOM_BRAND_TYPE_3GP6;
            minor_version = 0x00000000; /* means, 3gp(3gp6) 6.0.0 : "6" is not included in minor_version. */
            eprintf( "Using 3gp muxing mode.\n" );
        }
        else if( brand_3gx == 2 )
        {
            major_brand = ISOM_BRAND_TYPE_3G2A;
            brands[num_of_brands++] = ISOM_BRAND_TYPE_3GP6;
            brands[num_of_brands++] = ISOM_BRAND_TYPE_3G2A;
            minor_version = 0x00010000; /* means, 3g2(3g2a) 1.0.0 : a == 1 */
            eprintf( "Using 3g2 muxing mode.\n" );
        }
    }

    if( qtff )
    {
        if( chimera )
            brands[num_of_brands++] = ISOM_BRAND_TYPE_QT;
        else
        {
            for( int j = 1; j < num_of_brands; j++ )
                brands[j] = 0;
            num_of_brands = 1;
            brands[0] = ISOM_BRAND_TYPE_QT;
            major_brand = ISOM_BRAND_TYPE_QT;
            minor_version = 0;      /* We don't know exact version of the spec to use QTFF features. */
            eprintf( "Using QTFF muxing mode.\n" );
        }
    }

    /* Initialize importer framework */
    structs.importer = mp4sys_importer_open( argv[i++], "auto" );
    if( !structs.importer || !(structs.summary = mp4sys_duplicate_video_summary( structs.importer, 1 )) )
        return H264MUX_ERR( "Failed to open input file.\n" );

    /* check codec type. */
    lsmash_codec_type codec_type = structs.summary->sample_type;
    if( codec_type != ISOM_CODEC_TYPE_AVC1_VIDEO )
        return H264MUX_ERR( "Unknown sample_type.\n" );

    /* Initialize L-SMASH muxer */
    structs.root = lsmash_open_movie( argv[i++], LSMASH_FILE_MODE_WRITE );
    if( !structs.root )
        return H264MUX_ERR( "Failed to create root.\n" );

    /* Initialize movie */
    lsmash_movie_parameters_t movie_param;
    lsmash_initialize_movie_parameters( &movie_param );
    movie_param.major_brand = major_brand;
    movie_param.brands = brands;
    movie_param.number_of_brands = num_of_brands;
    movie_param.minor_version = minor_version;
    if( lsmash_set_movie_parameters( structs.root, &movie_param ) )
        return H264MUX_ERR( "Failed to set movie parameters.\n" );

    uint32_t track = lsmash_create_track( structs.root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
    if( !track )
        return H264MUX_ERR( "Failed to create a track.\n" );

    /* Initialize track */
    lsmash_track_parameters_t track_param;
    lsmash_initialize_track_parameters( &track_param );
    track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
    if( qtff )
        track_param.mode |= QT_TRACK_IN_POSTER;
    uint64_t display_width  = structs.summary->width  << 16;
    uint64_t display_height = structs.summary->height << 16;
    if( structs.summary->par_h && structs.summary->par_v )
    {
        double sar = (double)structs.summary->par_h / structs.summary->par_h;
        if( sar > 1.0 )
            display_width *= sar;
        else
            display_height /= sar;
    }
    track_param.display_width  = display_width;
    track_param.display_height = display_height;
    if( lsmash_set_track_parameters( structs.root, track, &track_param ) )
        return H264MUX_ERR( "Failed to set track parameters.\n" );

    /* Initialize media */
    lsmash_media_parameters_t media_param;
    lsmash_initialize_media_parameters( &media_param );
    media_param.timescale = 25;     /* default value */
    uint32_t timebase = 1;
    if( user_fps )
    {
        media_param.timescale = fps_num;
        timebase              = fps_den;
    }
    else if( !structs.summary->assumed_vfr )
        for( i = 0; well_known_fps[i].timescale; i++ )
            if( well_known_fps[i].timescale == structs.summary->timescale
             && well_known_fps[i].timebase  == structs.summary->timebase )
            {
                media_param.timescale = well_known_fps[i].timescale;
                timebase              = well_known_fps[i].timebase;
                break;
            }
    media_param.media_handler_name = "L-SMASH Video Handler";
    media_param.roll_grouping = 1;
    media_param.rap_grouping = isom_version >= 6;
    if( lsmash_set_media_parameters( structs.root, track, &media_param ) )
        return H264MUX_ERR( "Failed to set media parameters.\n" );

    uint32_t sample_entry = lsmash_add_sample_entry( structs.root, track, codec_type, structs.summary );
    if( !sample_entry )
        return H264MUX_ERR( "Failed to add sample_entry.\n" );

    /* transfer */
    uint32_t numframe = 0;
    uint32_t last_delta = UINT32_MAX;
    uint64_t prev_dts = 0;
    uint64_t ctd_shift = 0;
    int64_t start_offset = 0;
    while( 1 )
    {
        /* allocate sample buffer */
        lsmash_sample_t *sample = lsmash_create_sample( structs.summary->max_au_length );
        if( !sample )
            return H264MUX_ERR( "Failed to alloc memory for buffer.\n" );
        /* read a video frame */
        /* FIXME: mp4sys_importer_get_access_unit() returns 1 if there're any changes in stream's properties.
           If you want to support them, you have to retrieve summary again, and make some operation accordingly. */
        sample->length = structs.summary->max_au_length;
        if( mp4sys_importer_get_access_unit( structs.importer, 1, sample ) )
        {
            lsmash_delete_sample( sample );
            // return H264MUX_ERR( "Failed to get a frame from input file. Maybe corrupted.\n" );
            eprintf( "Failed to get a frame from input file. Maybe corrupted.\n" );
            eprintf( "Aborting muxing operation and trying to let output be valid file.\n" );
            break; /* error */
        }
        if( sample->length == 0 )
        {
            lsmash_delete_sample( sample );
            break; /* end of stream */
        }
        sample->index = sample_entry;
        sample->dts *= timebase;
        sample->cts *= timebase;
        if( timeline_shift )
        {
            if( numframe == 0 && sample->cts )
                ctd_shift = sample->cts;
            sample->cts -= ctd_shift;
        }
        if( lsmash_append_sample( structs.root, track, sample ) )
            return H264MUX_ERR( "Failed to write a frame.\n" );
        if( numframe == 0 )
            start_offset = sample->cts;
        else
            last_delta = sample->dts - prev_dts;
        prev_dts = sample->dts;
        numframe++;
        eprintf( "frame = %d\r", numframe );
    }
    eprintf( "total frames = %d\n", numframe );

    /* close track */
    if( lsmash_flush_pooled_samples( structs.root, track, last_delta ) )
        eprintf( "Failed to flush the rest of samples.\n" );
    /* use edit list
     * segment_duration == 0 means an appropriate one will be applied. */
    if( lsmash_create_explicit_timeline_map( structs.root, track, 0, start_offset, ISOM_EDIT_MODE_NORMAL ) )
        eprintf( "Failed to set timeline map.\n" );

    /* close movie */
    if( lsmash_finish_movie( structs.root, NULL ) )
        eprintf( "Failed to finish movie.\n" );

    cleanup_structs( &structs ); /* including lsmash_destroy_root() */
    return 0;
}
