/*****************************************************************************
 * timelineeditor.c:
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

#include "lsmash.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#define LSMASH_MAX( a, b ) ((a) > (b) ? (a) : (b))

#define eprintf( ... ) fprintf( stderr, __VA_ARGS__ )

typedef struct
{
    uint32_t track_ID;
    uint32_t last_sample_delta;
    uint32_t current_sample_number;
    int      reach_end_of_media_timeline;
    lsmash_track_parameters_t track_param;
    lsmash_media_parameters_t media_param;
} track_t;

typedef struct
{
    lsmash_root_t                 *root;
    lsmash_itunes_metadata_list_t *itunes_meta_list;
    track_t                       *track;
    lsmash_movie_parameters_t movie_param;
} movie_t;

typedef struct
{
    FILE     *file;
    uint64_t *ts;
    uint32_t sample_count;
    uint64_t duration;
    uint64_t composition_delay;
    uint64_t empty_delay;
} timecode_t;

typedef struct
{
    movie_t *output;
    movie_t *input;
    timecode_t *timecode;
} movie_io_t;

typedef struct
{
    uint32_t track_number;
    uint32_t media_timescale;
    uint32_t media_timebase;
    uint32_t skip_duration;
    uint32_t empty_delay;
    int      dts_compression;
} opt_t;

static void cleanup_movie( movie_t *movie )
{
    if( !movie )
        return;
    if( movie->itunes_meta_list )
        lsmash_destroy_itunes_metadata( movie->itunes_meta_list );
    if( movie->track )
        free( movie->track );
    lsmash_destroy_root( movie->root );
    movie->root = NULL;
    movie->itunes_meta_list = NULL;
    movie->track = NULL;
}

static void cleanup_timecode( timecode_t *timecode )
{
    if( !timecode )
        return;
    if( timecode->file )
        fclose( timecode->file );
    if( timecode->ts )
        free( timecode->ts );
    timecode->file = NULL;
    timecode->ts = NULL;
}

static int timelineeditor_error( movie_io_t *io, char *message )
{
    cleanup_movie( io->input );
    cleanup_movie( io->output );
    cleanup_timecode( io->timecode );
    eprintf( message );
    return -1;
}

static int error_message( char *message )
{
    eprintf( message );
    return -1;
}

#define TIMELINEEDITOR_ERR( message ) timelineeditor_error( &io, message )
#define ERROR_MSG( message ) error_message( message )

static int get_movie( movie_t *input, char *input_name, uint32_t *num_tracks )
{
    if( !strcmp( input_name, "-" ) )
        return ERROR_MSG( "Standard input not supported.\n" );
    input->root = lsmash_open_movie( input_name, LSMASH_FILE_MODE_READ );
    if( !input->root )
        return ERROR_MSG( "Failed to open input file.\n" );
    lsmash_movie_parameters_t *movie_param = &input->movie_param;
    lsmash_initialize_movie_parameters( movie_param );
    lsmash_get_movie_parameters( input->root, movie_param );
    *num_tracks = movie_param->number_of_tracks;
    /* Create tracks. */
    track_t *track = input->track = malloc( *num_tracks * sizeof(track_t) );
    if( !track )
        return ERROR_MSG( "Failed to alloc input tracks.\n" );
    memset( track, 0, *num_tracks * sizeof(track_t) );
    for( uint32_t i = 0; i < *num_tracks; i++ )
    {
        track[i].track_ID = lsmash_get_track_ID( input->root, i + 1 );
        if( !track[i].track_ID )
            return ERROR_MSG( "Failed to get track_ID.\n" );
    }
    for( uint32_t i = 0; i < *num_tracks; i++ )
    {
        input->itunes_meta_list = lsmash_export_itunes_metadata( input->root );
        if( !input->itunes_meta_list )
            return ERROR_MSG( "Failed to get iTunes metadata.\n" );
        lsmash_initialize_track_parameters( &track[i].track_param );
        if( lsmash_get_track_parameters( input->root, track[i].track_ID, &track[i].track_param ) )
            return ERROR_MSG( "Failed to get track parameters.\n" );
        lsmash_initialize_media_parameters( &track[i].media_param );
        if( lsmash_get_media_parameters( input->root, track[i].track_ID, &track[i].media_param ) )
            return ERROR_MSG( "Failed to get media parameters.\n" );
        if( lsmash_construct_timeline( input->root, track[i].track_ID ) )
            return ERROR_MSG( "Failed to construct timeline.\n" );
        if( lsmash_get_last_sample_delta_from_media_timeline( input->root, track[i].track_ID, &track[i].last_sample_delta ) )
            return ERROR_MSG( "Failed to get the last sample delta.\n" );
        track[i].current_sample_number = 1;
    }
    lsmash_discard_boxes( input->root );
    return 0;
}

static uint64_t get_gcd( uint64_t a, uint64_t b )
{
    if( !b )
        return a;
    while( 1 )
    {
        uint64_t c = a % b;
        if( !c )
            return b;
        a = b;
        b = c;
    }
}

static uint64_t get_media_timebase( lsmash_media_ts_list_t *ts_list )
{
    uint64_t timebase = ts_list->timestamp[0].cts;
    for( uint32_t i = 1; i < ts_list->sample_count; i++ )
        timebase = get_gcd( timebase, ts_list->timestamp[i].cts );
    for( uint32_t i = 0; i < ts_list->sample_count; i++ )
        timebase = get_gcd( timebase, ts_list->timestamp[i].dts );
    return timebase;
}

static int parse_timecode( movie_io_t *io, lsmash_media_ts_list_t *ts_list, uint32_t timescale, uint32_t timebase )
{
    int tcfv;
    timecode_t *timecode = io->timecode;
    int ret = fscanf( timecode->file, "# timecode format v%d", &tcfv );
    if( ret != 1 || tcfv != 2 )
        return ERROR_MSG( "Unsupported timecode format\n" );
    char buff[256];
    uint32_t num_timecodes = 0;
    uint64_t file_pos = ftell( timecode->file );
    while( fgets( buff, sizeof(buff), timecode->file ) )
    {
        if( buff[0] == '#' || buff[0] == '\n' || buff[0] == '\r' )
        {
            if( !num_timecodes )
                file_pos = ftell( timecode->file );
            continue;
        }
        ++num_timecodes;
    }
    if( !num_timecodes )
        return ERROR_MSG( "No timecodes!\n" );
    if( ts_list->sample_count > num_timecodes )
        return ERROR_MSG( "Lack number of timecodes!\n" );
    fseek( timecode->file, file_pos, SEEK_SET );
    timecode->ts = malloc( ts_list->sample_count * sizeof(uint64_t) );
    if( !timecode->ts )
        return ERROR_MSG( "Failed to alloc timestamps.\n" );
    fgets( buff, sizeof(buff), timecode->file );
    double tc;
    ret = sscanf( buff, "%lf", &tc );
    if( ret != 1 )
        return ERROR_MSG( "Invalid timecode number: 0\n" );
    double delay_tc = tc * 1e-3;    /* Timecode format v2 is expressed in milliseconds. */
    timecode->empty_delay = ((uint64_t)(delay_tc * ((double)timescale / timebase) + 0.5)) * timebase;
    timecode->ts[0] = 0;
    for( uint32_t i = 1; i < ts_list->sample_count; )
    {
        fgets( buff, sizeof(buff), timecode->file );
        if( buff[0] == '#' || buff[0] == '\n' || buff[0] == '\r' )
            continue;
        ret = sscanf( buff, "%lf", &tc );
        if( ret != 1 )
            return ERROR_MSG( "Invalid timecode\n" );
        tc *= 1e-3;     /* Timecode format v2 is expressed in milliseconds. */
        timecode->ts[i] = ((uint64_t)((tc - delay_tc) * ((double)timescale / timebase) + 0.5)) * timebase;
        if( timecode->ts[i] <= timecode->ts[i - 1] )
            return ERROR_MSG( "Invalid timecode.\n" );
        ++i;
    }
    return 0;
}

static uint32_t get_max_sample_delay( lsmash_media_ts_list_t *ts_list )
{
    uint32_t sample_delay = 0;
    uint32_t max_sample_delay = 0;
    lsmash_media_ts_t *ts = ts_list->timestamp;
    for( uint32_t i = 1; i < ts_list->sample_count; i++ )
    {
        if( ts[i].cts < ts[i - 1].cts )
        {
            ++sample_delay;
            max_sample_delay = LSMASH_MAX( max_sample_delay, sample_delay );
        }
        else
            sample_delay = 0;
    }
    return max_sample_delay;
}

static int compare_cts( const lsmash_media_ts_t *a, const lsmash_media_ts_t *b )
{
    int64_t diff = (int64_t)(a->cts - b->cts);
    return diff > 0 ? 1 : (diff == 0 ? 0 : -1);
}

static int compare_dts( const lsmash_media_ts_t *a, const lsmash_media_ts_t *b )
{
    int64_t diff = (int64_t)(a->dts - b->dts);
    return diff > 0 ? 1 : (diff == 0 ? 0 : -1);
}

static int edit_media_timeline( movie_io_t *io, opt_t *opt )
{
    timecode_t *timecode = io->timecode;
    if( !timecode->file && !opt->media_timescale && !opt->media_timebase && !opt->dts_compression )
        return 0;
    movie_t *input = io->input;
    track_t *in_track = &input->track[opt->track_number - 1];
    uint32_t track_ID = in_track->track_ID;
    lsmash_media_ts_list_t ts_list;
    if( lsmash_get_media_timestamps( input->root, track_ID, &ts_list ) )
        return ERROR_MSG( "Failed to get media timestamps.\n" );
    uint64_t timebase = get_media_timebase( &ts_list );
    if( !timebase )
        return ERROR_MSG( "Failed to get media timebase.\n" );
    lsmash_media_ts_t *timestamp = ts_list.timestamp;
    uint32_t sample_count = ts_list.sample_count;
    uint32_t orig_timebase = timebase;
    uint32_t timescale;
    double   timebase_convert_multiplier;
    if( opt->media_timescale || opt->media_timebase )
    {
        uint32_t orig_timescale = in_track->media_param.timescale;
        timescale = opt->media_timescale ? opt->media_timescale : orig_timescale;
        timebase  = opt->media_timebase  ? opt->media_timebase  : orig_timebase;
        if( !opt->media_timescale && opt->media_timebase && (timebase > orig_timebase) )
            timescale = timescale * ((double)timebase / orig_timebase) + 0.5;
        timebase_convert_multiplier = ((double)timescale / orig_timescale) * ((double)orig_timebase / timebase);
    }
    else
    {
        /* Reduce timescale and timebase. */
        timescale = in_track->media_param.timescale;
        uint64_t reduce = get_gcd( timescale, timebase );
        timescale /= reduce;
        timebase  /= reduce;
        timebase_convert_multiplier = 1;
    }
    in_track->media_param.timescale = timescale;
    /* Parse timecode file. */
    if( timecode->file && parse_timecode( io, &ts_list, timescale, timebase ) )
        return ERROR_MSG( "Failed to parse timecode file.\n" );
    /* Get maximum composition sample delay for DTS generation. */
    uint32_t sample_delay = get_max_sample_delay( &ts_list );
    if( sample_delay )      /* Reorder composition order. */
        qsort( timestamp, sample_count, sizeof(lsmash_media_ts_t), (int(*)( const void *, const void * ))compare_cts );
    if( !timecode->file )
    {
        /* Genarate timestamps timescale converted. */
        timecode->ts = malloc( sample_count * sizeof(uint64_t) );
        if( !timecode->ts )
            return ERROR_MSG( "Failed to alloc timestamps\n" );
        for( uint32_t i = 0; i < sample_count; i++ )
        {
            timecode->ts[i] = (timestamp[i].cts - timestamp[0].cts) / orig_timebase;
            timecode->ts[i] = ((uint64_t)(timecode->ts[i] * timebase_convert_multiplier + 0.5)) * timebase;
            if( i && (timecode->ts[i] <= timecode->ts[i - 1]) )
                return ERROR_MSG( "Invalid timescale conversion.\n" );
        }
    }
    if( sample_delay )
    {
        /* If media timescale is specified, disable DTS compression multiplier. */
        uint32_t dts_compression_multiplier = opt->dts_compression * !opt->media_timescale * sample_delay + 1;
        uint64_t initial_delta = timecode->ts[1];
        in_track->media_param.timescale *= dts_compression_multiplier;
        if( dts_compression_multiplier > 1 )
            for( uint32_t i = 0; i < sample_count; i++ )
                timecode->ts[i] *= dts_compression_multiplier;
        /* Generate CTS. */
        uint64_t sample_delay_time = timecode->composition_delay = opt->dts_compression ? 0 : timecode->ts[sample_delay];
        for( uint32_t i = 0; i < sample_count; i++ )
            timestamp[i].cts = timecode->ts[i] + sample_delay_time;
        /* Reorder decode order and generate new DTS from CTS. */
        qsort( timestamp, sample_count, sizeof(lsmash_media_ts_t), (int(*)( const void *, const void * ))compare_dts );
        uint64_t prev_reordered_cts[sample_delay];
        for( uint32_t i = 0; i <= sample_delay; i++ )
        {
            if( !opt->dts_compression )
                timestamp[i].dts = timecode->ts[i];
            else
            {
                timestamp[i].dts = (i * initial_delta) / (!!opt->media_timescale * sample_delay + 1);
                if( i && (timestamp[i].dts <= timestamp[i - 1].dts) )
                    return ERROR_MSG( "Failed to do DTS compression.\n" );
            }
            prev_reordered_cts[ i % sample_delay ] = timecode->ts[i] + sample_delay_time;
        }
        for( uint32_t i = sample_delay + 1; i < sample_count; i++ )
        {
            timestamp[i].dts = prev_reordered_cts[ (i - sample_delay) % sample_delay ];
            prev_reordered_cts[ i % sample_delay ] = timecode->ts[i] + sample_delay_time;
        }
    }
    else
        for( uint32_t i = 0; i < sample_count; i++ )
            timestamp[i].cts = timestamp[i].dts = timecode->ts[i];
    if( sample_count > 1 )
    {
        in_track->last_sample_delta = timecode->ts[sample_count - 1] - timecode->ts[sample_count - 2];
        timecode->duration = timecode->ts[sample_count - 1] + in_track->last_sample_delta;
    }
    else    /* still image */
        timecode->duration = in_track->last_sample_delta = UINT32_MAX;
    return lsmash_set_media_timestamps( input->root, track_ID, &ts_list );
}

static int moov_to_front_callback( void *param, uint64_t written_movie_size, uint64_t total_movie_size )
{
    eprintf( "Finalizing: [%5.2lf%%]\r", ((double)written_movie_size / total_movie_size) * 100.0 );
    return 0;
}

int main( int argc, char *argv[] )
{
    if( argc < 3 || !strcasecmp( argv[1], "-h" ) || !strcasecmp( argv[1], "--help" ) )
    {
        fprintf( stderr, "Usage: timelineeditor [options] input output\n"
             "  options:\n"
             "    --track           <integer>  Specify track number to edit [1]\n"
             "    --timecode        <string>   Specify timecode file to edit timeline\n"
             "    --media-timescale <integer>  Specify media timescale to convert\n"
             "    --media-timebase  <integer>  Specify media timebase to convert\n"
             "    --skip            <integer>  Skip start of media presentation in milliseconds\n"
             "    --delay           <integer>  Insert blank clip before actual media presentation in milliseconds\n"
             "    --dts-compression            Eliminate composition delay with DTS hack\n"
             "                                 Multiply media timescale and timebase automatically\n" );
        return -1;
    }
    movie_t    output   = { 0 };
    movie_t    input    = { 0 };
    timecode_t timecode = { 0 };
    movie_io_t io = { &output, &input, &timecode };
    opt_t opt = { 1, 0, 0, 0, 0, 0 };
    /* Parse options. */
    int argn = 1;
    while( argn < argc - 2 )
    {
        if( !strcasecmp( argv[argn], "--track" ) )
        {
            opt.track_number = atoi( argv[++argn] );
            if( !opt.track_number )
                return TIMELINEEDITOR_ERR( "Invalid track number.\n" );
            ++argn;
        }
        else if( !strcasecmp( argv[argn], "--timecode" ) )
        {
            timecode.file = fopen( argv[++argn], "rb" );
            if( !timecode.file )
                return TIMELINEEDITOR_ERR( "Failed to open timecode file.\n" );
            ++argn;
        }
        else if( !strcasecmp( argv[argn], "--media-timescale" ) )
        {
            opt.media_timescale = atoi( argv[++argn] );
            if( !opt.media_timescale )
                return TIMELINEEDITOR_ERR( "Invalid media timescale.\n" );
            ++argn;
        }
        else if( !strcasecmp( argv[argn], "--media-timebase" ) )
        {
            opt.media_timebase = atoi( argv[++argn] );
            if( !opt.media_timebase )
                return TIMELINEEDITOR_ERR( "Invalid media timebase.\n" );
            ++argn;
        }
        else if( !strcasecmp( argv[argn], "--skip" ) )
        {
            opt.skip_duration = atoi( argv[++argn] );
            if( !opt.skip_duration )
                return TIMELINEEDITOR_ERR( "Invalid skip duration.\n" );
            ++argn;
        }
        else if( !strcasecmp( argv[argn], "--delay" ) )
        {
            opt.empty_delay = atoi( argv[++argn] );
            if( !opt.empty_delay )
                return TIMELINEEDITOR_ERR( "Invalid delay time.\n" );
            ++argn;
        }
        else if( !strcasecmp( argv[argn], "--dts-compression" ) )
        {
            opt.dts_compression = 1;
            ++argn;
        }
        else
            return TIMELINEEDITOR_ERR( "Invalid option.\n" );
    }
    if( argn > argc - 2 )
        return TIMELINEEDITOR_ERR( "Invalid arguments.\n" );
    uint32_t num_tracks;
    /* Get input movies. */
    if( get_movie( &input, argv[argn++], &num_tracks ) )
        return TIMELINEEDITOR_ERR( "Failed to get input movie.\n" );
    if( opt.track_number && (opt.track_number > num_tracks) )
        return TIMELINEEDITOR_ERR( "Invalid track number.\n" );
    /* Create output movie. */
    output.root = lsmash_open_movie( argv[argn], LSMASH_FILE_MODE_WRITE );
    if( !output.root )
        return TIMELINEEDITOR_ERR( "Failed to open output movie.\n" );
    output.movie_param = input.movie_param;
    output.movie_param.max_chunk_duration  = 0.5;
    output.movie_param.max_async_tolerance = 2.0;
    output.movie_param.max_chunk_size      = 4*1024*1024;
    output.movie_param.max_read_size       = 4*1024*1024;
    if( lsmash_set_movie_parameters( output.root, &output.movie_param ) )
        return TIMELINEEDITOR_ERR( "Failed to set output movie parameters.\n" );
    /* Set iTunes metadata. */
    if( lsmash_import_itunes_metadata( output.root, input.itunes_meta_list ) )
        return TIMELINEEDITOR_ERR( "Failed to set iTunes metadata.\n" );
    /* Create tracks of the output movie. */
    output.track = malloc( num_tracks * sizeof(track_t) );
    if( !output.track )
        return TIMELINEEDITOR_ERR( "Failed to alloc output tracks.\n" );
    /* Edit timeline. */
    if( edit_media_timeline( &io, &opt ) )
        return TIMELINEEDITOR_ERR( "Failed to edit timeline.\n" );
    for( uint32_t i = 0; i < num_tracks; i++ )
    {
        track_t *in_track = &input.track[i];
        track_t *out_track = &output.track[i];
        uint32_t handler_type = in_track->media_param.handler_type;
        out_track->track_ID = lsmash_create_track( output.root, handler_type );
        if( !out_track->track_ID )
            return TIMELINEEDITOR_ERR( "Failed to create a track.\n" );
        /* Copy track and media parameters except for track_ID. */
        out_track->track_param = in_track->track_param;
        out_track->media_param = in_track->media_param;
        out_track->track_param.track_ID = out_track->track_ID;
        if( lsmash_set_track_parameters( output.root, out_track->track_ID, &out_track->track_param ) )
            return TIMELINEEDITOR_ERR( "Failed to set track parameters.\n" );
        if( lsmash_set_media_parameters( output.root, out_track->track_ID, &out_track->media_param ) )
            return TIMELINEEDITOR_ERR( "Failed to set media parameters.\n" );
        if( lsmash_copy_decoder_specific_info( output.root, out_track->track_ID, input.root, in_track->track_ID ) )
            return TIMELINEEDITOR_ERR( "Failed to copy a Decoder Specific Info.\n" );
        out_track->last_sample_delta = in_track->last_sample_delta;
        out_track->current_sample_number = 1;
        out_track->reach_end_of_media_timeline = 0;
    }
    /* Start muxing. */
    double   largest_dts = 0;
    uint32_t current_track_number = 1;
    uint32_t num_consecutive_sample_skip = 0;
    uint32_t num_active_input_tracks = num_tracks;
    uint64_t total_media_size = 0;
    uint8_t  sample_count = 0;
    while( 1 )
    {
        track_t *in_track = &input.track[current_track_number - 1];
        /* Try append a sample in an input track where we didn't reach the end of media timeline. */
        if( !in_track->reach_end_of_media_timeline )
        {
            track_t *out_track = &output.track[current_track_number - 1];
            uint32_t in_track_ID = in_track->track_ID;
            uint32_t out_track_ID = out_track->track_ID;
            uint32_t input_media_timescale = in_track->media_param.timescale;
            /* Get a DTS from a track in an input movie. */
            uint64_t dts;
            if( lsmash_get_dts_from_media_timeline( input.root, in_track_ID, in_track->current_sample_number, &dts ) )
            {
                if( lsmash_check_sample_existence_in_media_timeline( input.root, in_track_ID, in_track->current_sample_number ) )
                    return TIMELINEEDITOR_ERR( "Failed to get the DTS.\n" );
                else
                {
                    in_track->reach_end_of_media_timeline = 1;
                    if( --num_active_input_tracks == 0 )
                        break;      /* end of muxing */
                }
            }
            /* Get and append a sample if it's good time. */
            else if( ((double)dts / input_media_timescale) <= largest_dts
             || num_consecutive_sample_skip == num_active_input_tracks )
            {
                /* Get an actual sample data from a track in an input movie. */
                lsmash_sample_t *sample = lsmash_get_sample_from_media_timeline( input.root, in_track_ID, in_track->current_sample_number );
                if( !sample )
                    return TIMELINEEDITOR_ERR( "Failed to get sample.\n" );
                /* Append sample into output movie. */
                uint64_t sample_size = sample->length;      /* sample will be deleted internally after appending. */
                if( lsmash_append_sample( output.root, out_track_ID, sample ) )
                {
                    lsmash_delete_sample( sample );
                    return TIMELINEEDITOR_ERR( "Failed to append a sample.\n" );
                }
                largest_dts = LSMASH_MAX( largest_dts, (double)dts / input_media_timescale );
                total_media_size += sample_size;
                ++ in_track->current_sample_number;
                num_consecutive_sample_skip = 0;
                /* Print, per 256 samples, total size of imported media. */
                if( ++sample_count == 0 )
                    eprintf( "Importing: %"PRIu64" bytes\r", total_media_size );
            }
            else
                ++num_consecutive_sample_skip;      /* Skip appendig sample. */
        }
        /* Move the next track. */
        ++current_track_number;
        if( current_track_number > num_tracks )
            current_track_number = 1;   /* Back the first track. */
    }
    for( uint32_t i = 0; i < num_tracks; i++ )
        if( lsmash_flush_pooled_samples( output.root, output.track[i].track_ID, output.track[i].last_sample_delta ) )
            return TIMELINEEDITOR_ERR( "Failed to flush samples.\n" );
    /* Copy timeline maps. */
    for( uint32_t i = 0; i < num_tracks; i++ )
        if( lsmash_copy_timeline_map( output.root, output.track[i].track_ID, input.root, input.track[i].track_ID ) )
            return TIMELINEEDITOR_ERR( "Failed to copy a timeline map.\n" );
    /* Edit timeline map. */
    if( argc > 3 )
    {
        track_t *out_track       = &output.track[opt.track_number - 1];
        uint32_t track_ID        = out_track->track_ID;
        uint32_t movie_timescale = lsmash_get_movie_timescale( output.root );
        uint32_t media_timescale = lsmash_get_media_timescale( output.root, track_ID );
        uint32_t empty_delay     = timecode.empty_delay + (uint64_t)(opt.empty_delay * (1e-3 * media_timescale) + 0.5);
        uint32_t duration        = timecode.duration + empty_delay;
        if( lsmash_delete_explicit_timeline_map( output.root, track_ID ) )
            return TIMELINEEDITOR_ERR( "Failed to delete explicit timeline maps.\n" );
        if( timecode.empty_delay )
        {
            uint32_t empty_duration = ((double)timecode.empty_delay / media_timescale) * movie_timescale;
            if( lsmash_create_explicit_timeline_map( output.root, track_ID, empty_duration, ISOM_EDIT_MODE_EMPTY, ISOM_EDIT_MODE_NORMAL ) )
                return TIMELINEEDITOR_ERR( "Failed to create a empty duration.\n" );
            duration  = ((double)duration / media_timescale) * movie_timescale;
            duration -= empty_duration;
        }
        else
            duration  = ((double)duration / media_timescale) * movie_timescale;
        uint64_t start_time = timecode.composition_delay + (uint64_t)(opt.skip_duration * (1e-3 * media_timescale) + 0.5);
        if( lsmash_create_explicit_timeline_map( output.root, track_ID, duration, start_time, ISOM_EDIT_MODE_NORMAL ) )
            return TIMELINEEDITOR_ERR( "Failed to create a explicit timeline map.\n" );
    }
    /* Finish muxing. */
    lsmash_adhoc_remux_t moov_to_front;
    moov_to_front.func = moov_to_front_callback;
    moov_to_front.buffer_size = 4*1024*1024;
    moov_to_front.param = NULL;
    eprintf( "                                                                               \r" );
    if( lsmash_finish_movie( output.root, &moov_to_front ) )
        return TIMELINEEDITOR_ERR( "Failed to finish output movie.\n" );
    cleanup_movie( io.input );
    cleanup_movie( io.output );
    eprintf( "Timeline editing completed!                                                    \n" );
    return 0;
}
