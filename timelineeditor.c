/*****************************************************************************
 * timelineeditor.c:
 *****************************************************************************
 * Copyright (C) 2011-2012 L-SMASH project
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>

#include "lsmash.h"

#include "config.h"

#define LSMASH_MAX( a, b ) ((a) > (b) ? (a) : (b))

#define eprintf( ... ) fprintf( stderr, __VA_ARGS__ )
#define REFRESH_CONSOLE eprintf( "                                                                               \r" )

typedef struct
{
    int               active;
    lsmash_summary_t *summary;
} summary_t;

typedef struct
{
    int                       active;
    uint32_t                  track_ID;
    uint32_t                  last_sample_delta;
    uint32_t                  current_sample_number;
    int                       reach_end_of_media_timeline;
    uint32_t                 *summary_remap;
    uint32_t                  num_summaries;
    summary_t                *summaries;
    lsmash_track_parameters_t track_param;
    lsmash_media_parameters_t media_param;
} track_t;

typedef struct
{
    lsmash_root_t            *root;
    lsmash_itunes_metadata_t *itunes_metadata;
    track_t                  *track;
    lsmash_movie_parameters_t movie_param;
    uint32_t                  num_tracks;
    uint32_t                  num_itunes_metadata;
    uint32_t                  current_track_number;
} movie_t;

typedef struct
{
    FILE     *file;
    uint64_t *ts;
    uint32_t sample_count;
    int      auto_media_timescale;
    int      auto_media_timebase;
    uint64_t media_timescale;
    uint64_t media_timebase;
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
    if( movie->itunes_metadata )
    {
        for( uint32_t i = 0; i < movie->num_itunes_metadata; i++ )
        {
            lsmash_itunes_metadata_t *metadata = &movie->itunes_metadata[i];
            if( metadata->type == ITUNES_METADATA_TYPE_STRING )
            {
                if( metadata->value.string )
                    free( metadata->value.string );
            }
            else if( metadata->type == ITUNES_METADATA_TYPE_BINARY )
                if( metadata->value.binary.data )
                    free( metadata->value.binary.data );
            if( metadata->meaning )
                free( metadata->meaning );
            if( metadata->name )
                free( metadata->name );
        }
        free( movie->itunes_metadata );
    }
    if( movie->track )
        free( movie->track );
    lsmash_destroy_root( movie->root );
    movie->root            = NULL;
    movie->track           = NULL;
    movie->itunes_metadata = NULL;
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

static int error_message( const char* message, ... )
{
    REFRESH_CONSOLE;
    eprintf( "Error: " );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    return -1;
}

static int warning_message( const char* message, ... )
{
    REFRESH_CONSOLE;
    eprintf( "Warning: " );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    return -1;
}

static int timelineeditor_error( movie_io_t *io, const char *message, ... )
{
    cleanup_movie( io->input );
    cleanup_movie( io->output );
    cleanup_timecode( io->timecode );
    va_list args;
    va_start( args, message );
    error_message( message, args );
    va_end( args );
    return -1;
}

#define TIMELINEEDITOR_ERR( ... ) timelineeditor_error( &io, __VA_ARGS__ )
#define ERROR_MSG( ... ) error_message( __VA_ARGS__ )
#define WARNING_MSG( ... ) warning_message( __VA_ARGS__ )

static char *duplicate_string( char *src )
{
    if( !src )
        return NULL;
    int dst_size = strlen( src ) + 1;
    char *dst = malloc( dst_size );
    if( !dst )
        return NULL;
    memcpy( dst, src, dst_size );
    return dst;
}

static int get_itunes_metadata( lsmash_root_t *root, uint32_t metadata_number, lsmash_itunes_metadata_t *metadata )
{
    memset( metadata, 0, sizeof(lsmash_itunes_metadata_t) );
    if( lsmash_get_itunes_metadata( root, metadata_number, metadata ) )
        return -1;
    lsmash_itunes_metadata_t shadow = *metadata;
    metadata->meaning = NULL;
    metadata->name    = NULL;
    memset( &metadata->value, 0, sizeof(lsmash_itunes_metadata_value_t) );        
    if( shadow.meaning )
    {
        metadata->meaning = duplicate_string( shadow.meaning );
        if( !metadata->meaning )
            return -1;
    }
    if( shadow.name )
    {
        metadata->name = duplicate_string( shadow.name );
        if( !metadata->name )
            goto fail;
    }
    if( shadow.type == ITUNES_METADATA_TYPE_STRING )
    {
        metadata->value.string = duplicate_string( shadow.value.string );
        if( !metadata->value.string )
            goto fail;
    }
    else if( shadow.type == ITUNES_METADATA_TYPE_BINARY )
    {
        metadata->value.binary.data = malloc( shadow.value.binary.size );
        if( !metadata->value.binary.data )
            goto fail;
        memcpy( metadata->value.binary.data, shadow.value.binary.data, shadow.value.binary.size );
    }
    return 0;
fail:
    if( metadata->meaning )
        free( metadata->meaning );
    if( metadata->name )
        free( metadata->name );
    return -1;
}

static int get_summaries( movie_t *input, track_t *track )
{
    track->num_summaries = lsmash_count_summary( input->root, track->track_ID );
    if( track->num_summaries == 0 )
        return ERROR_MSG( "Failed to get find valid summaries.\n" );
    track->summaries = malloc( track->num_summaries * sizeof(summary_t) );
    if( !track->summaries )
        return ERROR_MSG( "failed to alloc input summaries.\n" );
    memset( track->summaries, 0, track->num_summaries * sizeof(summary_t) );
    for( uint32_t j = 0; j < track->num_summaries; j++ )
    {
        lsmash_summary_t *summary = lsmash_get_summary( input->root, track->track_ID, j + 1 );
        if( !summary )
        {
            WARNING_MSG( "failed to get a summary.\n" );
            continue;
        }
        track->summaries[j].summary = summary;
        track->summaries[j].active  = 1;
    }
    return 0;
}

static int get_movie( movie_t *input, char *input_name )
{
    if( !strcmp( input_name, "-" ) )
        return ERROR_MSG( "Standard input not supported.\n" );
    input->root = lsmash_open_movie( input_name, LSMASH_FILE_MODE_READ );
    if( !input->root )
        return ERROR_MSG( "Failed to open input file.\n" );
    input->num_itunes_metadata = lsmash_count_itunes_metadata( input->root );
    if( input->num_itunes_metadata )
    {
        input->itunes_metadata = malloc( input->num_itunes_metadata * sizeof(lsmash_itunes_metadata_t) );
        if( !input->itunes_metadata )
            return ERROR_MSG( "failed to alloc iTunes metadata.\n" );
        uint32_t itunes_metadata_count = 0;
        for( uint32_t i = 1; i <= input->num_itunes_metadata; i++ )
        {
            if( get_itunes_metadata( input->root, i, &input->itunes_metadata[itunes_metadata_count] ) )
            {
                WARNING_MSG( "failed to get an iTunes metadata.\n" );
                continue;
            }
            ++itunes_metadata_count;
        }
        input->num_itunes_metadata = itunes_metadata_count;
    }
    input->current_track_number = 1;
    lsmash_movie_parameters_t *movie_param = &input->movie_param;
    lsmash_initialize_movie_parameters( movie_param );
    lsmash_get_movie_parameters( input->root, movie_param );
    input->num_tracks = movie_param->number_of_tracks;
    /* Create tracks. */
    track_t *track = input->track = malloc( input->num_tracks * sizeof(track_t) );
    if( !track )
        return ERROR_MSG( "Failed to alloc input tracks.\n" );
    memset( track, 0, input->num_tracks * sizeof(track_t) );
    for( uint32_t i = 0; i < input->num_tracks; i++ )
    {
        track[i].track_ID = lsmash_get_track_ID( input->root, i + 1 );
        if( !track[i].track_ID )
            return ERROR_MSG( "Failed to get track_ID.\n" );
    }
    for( uint32_t i = 0; i < input->num_tracks; i++ )
    {
        lsmash_initialize_track_parameters( &track[i].track_param );
        if( lsmash_get_track_parameters( input->root, track[i].track_ID, &track[i].track_param ) )
        {
            WARNING_MSG( "failed to get track parameters.\n" );
            continue;
        }
        lsmash_initialize_media_parameters( &track[i].media_param );
        if( lsmash_get_media_parameters( input->root, track[i].track_ID, &track[i].media_param ) )
        {
            WARNING_MSG( "failed to get media parameters.\n" );
            continue;
        }
        if( lsmash_construct_timeline( input->root, track[i].track_ID ) )
        {
            WARNING_MSG( "failed to construct timeline.\n" );
            continue;
        }
        if( lsmash_get_last_sample_delta_from_media_timeline( input->root, track[i].track_ID, &track[i].last_sample_delta ) )
        {
            WARNING_MSG( "failed to get the last sample delta.\n" );
            continue;
        }
        if( get_summaries( input, &track[i] ) )
        {
            WARNING_MSG( "failed to get valid summaries.\n" );
            continue;
        }
        track[i].active                = 1;
        track[i].current_sample_number = 1;
    }
    lsmash_discard_boxes( input->root );
    return 0;
}

static inline uint64_t get_gcd( uint64_t a, uint64_t b )
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

static inline uint64_t get_lcm( uint64_t a, uint64_t b )
{
    if( !a )
        return 0;
    return (a / get_gcd( a, b )) * b;
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

static inline double sigexp10( double value, double *exponent )
{
    /* This function separates significand and exp10 from double floating point. */
    *exponent = 1;
    while( value < 1 )
    {
        value *= 10;
        *exponent /= 10;
    }
    while( value >= 10 )
    {
        value /= 10;
        *exponent *= 10;
    }
    return value;
}

#define DOUBLE_EPSILON 5e-6
#define MATROSKA_TIMESCALE 1000000000
#define SKIP_LINE_CHARACTER( x ) ((x) == '#' || (x) == '\n' || (x) == '\r')

static double correct_fps( double fps, timecode_t *timecode )
{
    int i = 1;
    uint64_t fps_num, fps_den;
    double exponent;
    double fps_sig = sigexp10( fps, &exponent );
    while( 1 )
    {
        fps_den = i * timecode->media_timebase;
        fps_num = round( fps_den * fps_sig ) * exponent;
        if( fps_num > UINT32_MAX )
            return ERROR_MSG( "framerate correction failed.\n"
                              "Specify an appropriate timebase manually or remake timecode file.\n" );
        if( fabs( ((double)fps_num / fps_den) / exponent - fps_sig ) < DOUBLE_EPSILON )
            break;
        ++i;
    }
    if( timecode->auto_media_timescale )
    {
        timecode->media_timescale = timecode->media_timescale
                                  ? get_lcm( timecode->media_timescale, fps_num )
                                  : fps_num;
        if( timecode->media_timescale > UINT32_MAX )
            timecode->auto_media_timescale = 0;
    }
    return (double)fps_num / fps_den;
}

static int try_matroska_timescale( double *fps_array, timecode_t *timecode, uint32_t num_loops )
{
    timecode->media_timebase  = 0;
    timecode->media_timescale = MATROSKA_TIMESCALE;
    for( uint32_t i = 0; i < num_loops; i++ )
    {
        uint64_t fps_den;
        double exponent;
        double fps_sig = sigexp10( fps_array[i], &exponent );
        fps_den = round( MATROSKA_TIMESCALE / fps_sig ) / exponent;
        timecode->media_timebase = fps_den && timecode->media_timebase
                                 ? get_gcd( timecode->media_timebase, fps_den )
                                 : fps_den;
        if( timecode->media_timebase > UINT32_MAX || !timecode->media_timebase )
            return ERROR_MSG( "Automatic media timescale generation failed.\n"
                              "Specify media timescale manually.\n" );
    }
    return 0;
}

static int parse_timecode( timecode_t *timecode, uint32_t sample_count )
{
    int tcfv;
    int ret = fscanf( timecode->file, "# timecode format v%d", &tcfv );
    if( ret != 1 || (tcfv != 1 && tcfv != 2) )
        return ERROR_MSG( "Unsupported timecode format\n" );
    char buff[256];
    double *timecode_array = NULL;
    if( tcfv == 1 )
    {
        double  assume_fps = 0;
        /* Get assumed framerate. */
        while( fgets( buff, sizeof(buff), timecode->file ) )
        {
            if( SKIP_LINE_CHARACTER( buff[0] ) )
                continue;
            if( sscanf( buff, "assume %lf", &assume_fps ) != 1
             && sscanf( buff, "Assume %lf", &assume_fps ) != 1 )
                return ERROR_MSG( "Assumed fps not found\n" );
            break;
        }
        if( assume_fps <= 0 )
            return ERROR_MSG( "Invalid assumed fps\n" );
        uint64_t file_pos = ftell( timecode->file );
        /* Check whether valid or not and count number of sequences. */
        uint32_t num_sequences = 0;
        int64_t  start, end;
        int64_t  prev_start = -1, prev_end = -1;
        double   sequence_fps;
        while( fgets( buff, sizeof(buff), timecode->file ) )
        {
            if( SKIP_LINE_CHARACTER( buff[0] ) )
                continue;
            ret = sscanf( buff, "%"SCNd64",%"SCNd64",%lf", &start, &end, &sequence_fps );
            if( ret != 3 && ret != EOF )
                return ERROR_MSG( "Invalid input timecode file\n" );
            if( start > end || start <= prev_start || end <= prev_end || sequence_fps <= 0 )
                return ERROR_MSG( "Invalid input timecode file\n" );
            prev_start = start;
            prev_end = end;
            if( timecode->auto_media_timescale || timecode->auto_media_timebase )
                ++num_sequences;
        }
        fseek( timecode->file, file_pos, SEEK_SET );
        /* Preparation storing timecodes. */
        double fps_array[ (timecode->auto_media_timescale || timecode->auto_media_timebase) * num_sequences + 1 ];
        double corrected_assume_fps = correct_fps( assume_fps, timecode );
        if( corrected_assume_fps < 0 )
            return ERROR_MSG( "Failed to correct the assumed framerate\n" );
        timecode_array = malloc( sample_count * sizeof(double) );
        if( !timecode_array )
            return ERROR_MSG( "Failed to alloc timecodes\n" );
        timecode_array[0] = 0;
        num_sequences = 0;
        uint32_t i = 0;
        while( i < sample_count - 1 && fgets( buff, sizeof(buff), timecode->file ) )
        {
            if( SKIP_LINE_CHARACTER( buff[0] ) )
                continue;
            ret = sscanf( buff, "%"SCNd64",%"SCNd64",%lf", &start, &end, &sequence_fps );
            if( ret != 3 )
                start = end = sample_count - 1;
            for( ; i < start && i < sample_count - 1; i++ )
                timecode_array[i + 1] = timecode_array[i] + 1 / corrected_assume_fps;
            if( i < sample_count - 1 )
            {
                if( timecode->auto_media_timescale || timecode->auto_media_timebase )
                    fps_array[num_sequences++] = sequence_fps;
                sequence_fps = correct_fps( sequence_fps, timecode );
                if( sequence_fps < 0 )
                {
                    free( timecode_array );
                    return ERROR_MSG( "Failed to correct the framerate of a sequence.\n" );
                }
                for( i = start; i <= end && i < sample_count - 1; i++ )
                    timecode_array[i + 1] = timecode_array[i] + 1 / sequence_fps;
            }
        }
        for( ; i < sample_count - 1; i++ )
            timecode_array[i + 1] = timecode_array[i] + 1 / corrected_assume_fps;
        if( timecode->auto_media_timescale || timecode->auto_media_timebase )
            fps_array[num_sequences] = assume_fps;
        /* Assume matroska timebase if automatic timescale generation isn't done yet. */
        if( timecode->auto_media_timebase && !timecode->auto_media_timescale )
        {
            double exponent;
            double assume_fps_sig, sequence_fps_sig;
            if( try_matroska_timescale( fps_array, timecode, num_sequences + 1 ) < 0 )
            {
                free( timecode_array );
                return ERROR_MSG( "Failed to try matroska timescale.\n" );
            }
            fseek( timecode->file, file_pos, SEEK_SET );
            assume_fps_sig = sigexp10( assume_fps, &exponent );
            corrected_assume_fps = MATROSKA_TIMESCALE / ( round( MATROSKA_TIMESCALE / assume_fps_sig ) / exponent );
            for( i = 0; i < sample_count - 1 && fgets( buff, sizeof(buff), timecode->file ); )
            {
                if( SKIP_LINE_CHARACTER( buff[0] ) )
                    continue;
                ret = sscanf( buff, "%"SCNd64",%"SCNd64",%lf", &start, &end, &sequence_fps );
                if( ret != 3 )
                    start = end = sample_count - 1;
                sequence_fps_sig = sigexp10( sequence_fps, &exponent );
                sequence_fps = MATROSKA_TIMESCALE / ( round( MATROSKA_TIMESCALE / sequence_fps_sig ) / exponent );
                for( ; i < start && i < sample_count - 1; i++ )
                    timecode_array[i + 1] = timecode_array[i] + 1 / corrected_assume_fps;
                for( i = start; i <= end && i < sample_count - 1; i++ )
                    timecode_array[i + 1] = timecode_array[i] + 1 / sequence_fps;
            }
            for( ; i < sample_count - 1; i++ )
                timecode_array[i + 1] = timecode_array[i] + 1 / corrected_assume_fps;
        }
    }
    else    /* tcfv == 2 */
    {
        uint32_t num_timecodes = 0;
        uint64_t file_pos = ftell( timecode->file );
        while( fgets( buff, sizeof(buff), timecode->file ) )
        {
            if( SKIP_LINE_CHARACTER( buff[0] ) )
            {
                if( !num_timecodes )
                    file_pos = ftell( timecode->file );
                continue;
            }
            ++num_timecodes;
        }
        if( !num_timecodes )
            return ERROR_MSG( "No timecodes!\n" );
        if( sample_count > num_timecodes )
            return ERROR_MSG( "Lack number of timecodes.\n" );
        fseek( timecode->file, file_pos, SEEK_SET );
        timecode_array = malloc( sample_count * sizeof(uint64_t) );
        if( !timecode_array )
            return ERROR_MSG( "Failed to alloc timecodes.\n" );
        uint32_t i = 0;
        if( fgets( buff, sizeof(buff), timecode->file ) )
        {
            ret = sscanf( buff, "%lf", &timecode_array[0] );
            if( ret != 1 )
            {
                free( timecode_array );
                return ERROR_MSG( "Invalid timecode number: 0\n" );
            }
            timecode_array[i++] *= 1e-3;        /* Timescale of timecode format v2 is 1000. */
            while( i < sample_count && fgets( buff, sizeof(buff), timecode->file ) )
            {
                if( SKIP_LINE_CHARACTER( buff[0] ) )
                    continue;
                ret = sscanf( buff, "%lf", &timecode_array[i] );
                timecode_array[i] *= 1e-3;      /* Timescale of timecode format v2 is 1000. */
                if( ret != 1 || timecode_array[i] <= timecode_array[i - 1] )
                {
                    free( timecode_array );
                    return ERROR_MSG( "Invalid input timecode.\n" );
                }
                ++i;
            }
        }
        if( i < sample_count )
        {
            free( timecode_array );
            return ERROR_MSG( "Failed to get timecodes.\n" );
        }
        /* Generate media timescale automatically if needed. */
        if( sample_count != 1 && timecode->auto_media_timescale )
        {
            double fps_array[sample_count - 1];
            for( i = 0; i < sample_count - 1; i++ )
            {
                fps_array[i] = 1 / (timecode_array[i + 1] - timecode_array[i]);
                if( timecode->auto_media_timescale )
                {
                    int j = 1;
                    uint64_t fps_num, fps_den;
                    double exponent;
                    double fps_sig = sigexp10( fps_array[i], &exponent );
                    while( 1 )
                    {
                        fps_den = j * timecode->media_timebase;
                        fps_num = round( fps_den * fps_sig ) * exponent;
                        if( fps_num > UINT32_MAX
                         || fabs( ((double)fps_num / fps_den) / exponent - fps_sig ) < DOUBLE_EPSILON )
                            break;
                        ++j;
                    }
                    timecode->media_timescale = fps_num && timecode->media_timescale
                                              ? get_lcm( timecode->media_timescale, fps_num )
                                              : fps_num;
                    if( timecode->media_timescale > UINT32_MAX )
                    {
                        timecode->auto_media_timescale = 0;
                        continue;   /* Don't break because all framerate is needed for try_matroska_timescale. */
                    }
                }
            }
            if( timecode->auto_media_timebase && !timecode->auto_media_timescale
             && try_matroska_timescale( fps_array, timecode, sample_count - 1 ) < 0 )
            {
                free( timecode_array );
                return ERROR_MSG( "Failed to try matroska timescale.\n" );
            }
        }
    }
    if( timecode->auto_media_timescale || timecode->auto_media_timebase )
    {
        uint64_t reduce = get_gcd( timecode->media_timebase, timecode->media_timescale );
        timecode->media_timebase  /= reduce;
        timecode->media_timescale /= reduce;
    }
    else if( timecode->media_timescale > UINT32_MAX || !timecode->media_timescale )
    {
        free( timecode_array );
        return ERROR_MSG( "Failed to generate media timescale automatically.\n"
                          "Specify an appropriate media timescale manually.\n" );
    }
    uint32_t timescale = timecode->media_timescale;
    uint32_t timebase  = timecode->media_timebase;
    double delay_tc = timecode_array[0];
    timecode->empty_delay = ((uint64_t)(delay_tc * ((double)timescale / timebase) + 0.5)) * timebase;
    timecode->ts = malloc( sample_count * sizeof(uint64_t) );
    if( !timecode->ts )
    {
        free( timecode_array );
        return ERROR_MSG( "Failed to allocate timestamps.\n" );
    }
    timecode->ts[0] = 0;
    for( uint32_t i = 1; i < sample_count; i++ )
    {
        timecode->ts[i] = ((uint64_t)((timecode_array[i] - delay_tc) * ((double)timescale / timebase) + 0.5)) * timebase;
        if( timecode->ts[i] <= timecode->ts[i - 1] )
        {
            free( timecode_array );
            free( timecode->ts );
            timecode->ts = NULL;
            return ERROR_MSG( "Invalid timecode.\n" );
        }
    }
    free( timecode_array );
    return 0;
}

#undef DOUBLE_EPSILON
#undef MATROSKA_TIMESCALE
#undef SKIP_LINE_CHARACTER

static int edit_media_timeline( movie_t *input, timecode_t *timecode, opt_t *opt )
{
    if( !timecode->file && !opt->media_timescale && !opt->media_timebase && !opt->dts_compression )
        return 0;
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
    /* Parse timecode file. */
    if( timecode->file )
    {
        timecode->auto_media_timescale = !opt->media_timescale;
        timecode->auto_media_timebase  = !opt->media_timebase;
        timecode->media_timescale = timecode->auto_media_timescale ? 0 : timescale;
        timecode->media_timebase  = timebase;
        if( parse_timecode( timecode, sample_count ) )
            return ERROR_MSG( "Failed to parse timecode file.\n" );
        timescale = timecode->media_timescale;
        timebase  = timecode->media_timebase;
    }
    /* Get maximum composition sample delay for DTS generation. */
    uint32_t sample_delay;
    if( lsmash_get_max_sample_delay( &ts_list, &sample_delay ) )
        return ERROR_MSG( "Failed to get maximum composition sample delay.\n" );
    if( sample_delay )      /* Reorder composition order. */
        lsmash_sort_timestamps_composition_order( &ts_list );
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
        timescale *= dts_compression_multiplier;
        if( dts_compression_multiplier > 1 )
            for( uint32_t i = 0; i < sample_count; i++ )
                timecode->ts[i] *= dts_compression_multiplier;
        /* Generate CTS. */
        uint64_t sample_delay_time = timecode->composition_delay = opt->dts_compression ? 0 : timecode->ts[sample_delay];
        for( uint32_t i = 0; i < sample_count; i++ )
            timestamp[i].cts = timecode->ts[i] + sample_delay_time;
        /* Reorder decode order and generate new DTS from CTS. */
        lsmash_sort_timestamps_decoding_order( &ts_list );
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
    in_track->media_param.timescale = timescale;
    if( lsmash_set_media_timestamps( input->root, track_ID, &ts_list ) )
        return ERROR_MSG( "Failed to set media timestamps.\n" );
    lsmash_delete_media_timestamps( &ts_list );
    return 0;
}

static int moov_to_front_callback( void *param, uint64_t written_movie_size, uint64_t total_movie_size )
{
    eprintf( "Finalizing: [%5.2lf%%]\r", ((double)written_movie_size / total_movie_size) * 100.0 );
    return 0;
}

static void print_help( void )
{
    eprintf( "\n"
             "L-SMASH isom/mov timeline editor rev%s  %s\n"
             "Built on %s %s\n"
             "Copyright (C) 2011-2012 L-SMASH project\n"
             "\n"
             "Usage: timelineeditor [options] input output\n"
             "  options:\n"
             "    --track           <integer>  Specify track number to edit [1]\n"
             "    --timecode        <string>   Specify timecode file to edit timeline\n"
             "    --media-timescale <integer>  Specify media timescale to convert\n"
             "    --media-timebase  <integer>  Specify media timebase to convert\n"
             "    --skip            <integer>  Skip start of media presentation in milliseconds\n"
             "    --delay           <integer>  Insert blank clip before actual media presentation in milliseconds\n"
             "    --dts-compression            Eliminate composition delay with DTS hack\n"
             "                                 Multiply media timescale and timebase automatically\n",
             LSMASH_REV, LSMASH_GIT_HASH, __DATE__, __TIME__ );
}

int main( int argc, char *argv[] )
{
    if( argc < 3 )
    {
        print_help();
        return -1;
    }
    if( !strcasecmp( argv[1], "-h" ) || !strcasecmp( argv[1], "--help" ) )
    {
        print_help();
        return 0;
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
    /* Get input movies. */
    if( get_movie( &input, argv[argn++] ) )
        return TIMELINEEDITOR_ERR( "Failed to get input movie.\n" );
    if( opt.track_number && (opt.track_number > input.num_tracks) )
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
    for( uint32_t i = 0; i < input.num_itunes_metadata; i++ )
        if( lsmash_set_itunes_metadata( output.root, input.itunes_metadata[i] ) )
        {
            WARNING_MSG( "failed to set an iTunes metadata.\n" );
            continue;
        }
    /* Create tracks of the output movie. */
    output.track = malloc( input.num_tracks * sizeof(track_t) );
    if( !output.track )
        return TIMELINEEDITOR_ERR( "Failed to alloc output tracks.\n" );
    /* Edit timeline. */
    if( edit_media_timeline( &input, &timecode, &opt ) )
        return TIMELINEEDITOR_ERR( "Failed to edit timeline.\n" );
    output.num_tracks = input.num_tracks;
    output.current_track_number = 1;
    for( uint32_t i = 0; i < input.num_tracks; i++ )
    {
        track_t *in_track = &input.track[i];
        if( !in_track->active )
        {
            -- output.num_tracks;
            continue;
        }
        track_t *out_track = &output.track[i];
        out_track->summary_remap = malloc( in_track->num_summaries * sizeof(uint32_t) );
        if( !out_track->summary_remap )
            return TIMELINEEDITOR_ERR( "failed to create summary mapping for a track.\n" );
        memset( out_track->summary_remap, 0, in_track->num_summaries * sizeof(uint32_t) );
        out_track->track_ID = lsmash_create_track( output.root, in_track->media_param.handler_type );
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
        uint32_t valid_summary_count = 0;
        for( uint32_t k = 0; k < in_track->num_summaries; k++ )
        {
            if( !in_track->summaries[k].active )
            {
                out_track->summary_remap[k] = 0;
                continue;
            }
            lsmash_summary_t *summary = in_track->summaries[k].summary;
            if( lsmash_add_sample_entry( output.root, out_track->track_ID, summary ) == 0 )
            {
                WARNING_MSG( "failed to append a summary.\n" );
                lsmash_cleanup_summary( summary );
                in_track->summaries[k].summary = NULL;
                in_track->summaries[k].active  = 0;
                out_track->summary_remap[k] = 0;
                continue;
            }
            out_track->summary_remap[k] = ++valid_summary_count;
        }
        if( valid_summary_count == 0 )
            return TIMELINEEDITOR_ERR( "failed to append all summaries.\n" );
        out_track->last_sample_delta           = in_track->last_sample_delta;
        out_track->current_sample_number       = 1;
        out_track->reach_end_of_media_timeline = 0;
    }
    /* Start muxing. */
    double   largest_dts = 0;
    uint32_t num_consecutive_sample_skip = 0;
    uint32_t num_active_input_tracks = output.num_tracks;
    uint64_t total_media_size = 0;
    uint8_t  sample_count = 0;
    while( 1 )
    {
        track_t *in_track = &input.track[input.current_track_number - 1];
        /* Try append a sample in an input track where we didn't reach the end of media timeline. */
        if( !in_track->reach_end_of_media_timeline )
        {
            track_t *out_track = &output.track[output.current_track_number - 1];
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
                sample->index = sample->index > in_track->num_summaries ? in_track->num_summaries
                              : sample->index == 0 ? 1
                              : sample->index;
                sample->index = out_track->summary_remap[ sample->index - 1 ];
                if( sample->index )
                {
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
            }
            else
                ++num_consecutive_sample_skip;      /* Skip appendig sample. */
        }
        /* Move the next track. */
        if( ++ input.current_track_number > input.num_tracks )
            input.current_track_number = 1;     /* Back the first track. */
        if( ++ output.current_track_number > output.num_tracks )
            output.current_track_number = 1;    /* Back the first track in the output movie. */
    }
    for( uint32_t i = 0; i < output.num_tracks; i++ )
        if( lsmash_flush_pooled_samples( output.root, output.track[i].track_ID, output.track[i].last_sample_delta ) )
            return TIMELINEEDITOR_ERR( "Failed to flush samples.\n" );
    /* Copy timeline maps. */
    for( uint32_t i = 0; i < output.num_tracks; i++ )
        if( lsmash_copy_timeline_map( output.root, output.track[i].track_ID, input.root, input.track[i].track_ID ) )
            return TIMELINEEDITOR_ERR( "Failed to copy a timeline map.\n" );
    /* Edit timeline map. */
    if( argc > 3 )
    {
        track_t *out_track       = &output.track[opt.track_number - 1];
        uint32_t track_ID        = out_track->track_ID;
        uint32_t movie_timescale = lsmash_get_movie_timescale( output.root );
        uint32_t media_timescale = lsmash_get_media_timescale( output.root, track_ID );
        uint64_t empty_delay     = timecode.empty_delay + (uint64_t)(opt.empty_delay * (1e-3 * media_timescale) + 0.5);
        uint64_t duration        = timecode.duration + empty_delay;
        if( lsmash_delete_explicit_timeline_map( output.root, track_ID ) )
            return TIMELINEEDITOR_ERR( "Failed to delete explicit timeline maps.\n" );
        if( timecode.empty_delay )
        {
            lsmash_edit_t empty_edit;
            empty_edit.duration   = ((double)timecode.empty_delay / media_timescale) * movie_timescale;
            empty_edit.start_time = ISOM_EDIT_MODE_EMPTY;
            empty_edit.rate       = ISOM_EDIT_MODE_NORMAL;
            if( lsmash_create_explicit_timeline_map( output.root, track_ID, empty_edit ) )
                return TIMELINEEDITOR_ERR( "Failed to create a empty duration.\n" );
            duration  = ((double)duration / media_timescale) * movie_timescale;
            duration -= empty_edit.duration;
        }
        else
            duration  = ((double)duration / media_timescale) * movie_timescale;
        lsmash_edit_t edit;
        edit.duration   = duration;
        edit.start_time = timecode.composition_delay + (uint64_t)(opt.skip_duration * (1e-3 * media_timescale) + 0.5);
        edit.rate       = ISOM_EDIT_MODE_NORMAL;
        if( lsmash_create_explicit_timeline_map( output.root, track_ID, edit ) )
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
