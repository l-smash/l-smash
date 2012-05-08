/*****************************************************************************
 * remuxer.c:
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
#include <stdarg.h>

#include "lsmash.h"

#include "config.h"

typedef struct
{
    uint32_t                  track_ID;
    uint32_t                  last_sample_delta;
    uint32_t                  current_sample_number;
    uint64_t                  skip_dt_interval;
    uint64_t                  last_sample_dts;
    lsmash_track_parameters_t track_param;
    lsmash_media_parameters_t media_param;
} output_track_t;

typedef struct
{
    lsmash_root_t            *root;
    output_track_t           *track;
    lsmash_movie_parameters_t movie_param;
    uint32_t                  num_tracks;
    uint32_t                  current_track_number;
} output_movie_t;

typedef struct
{
    int                       active;
    lsmash_sample_t          *sample;
    double                    dts;
    uint64_t                  composition_delay;
    uint64_t                  skip_duration;
    int                       reach_end_of_media_timeline;
    uint32_t                  track_ID;
    uint32_t                  last_sample_delta;
    uint32_t                  current_sample_number;
    lsmash_track_parameters_t track_param;
    lsmash_media_parameters_t media_param;
} input_track_t;

typedef struct
{
    lsmash_root_t                 *root;
    input_track_t                 *track;
    lsmash_itunes_metadata_list_t *itunes_meta_list;
    lsmash_movie_parameters_t      movie_param;
    uint32_t                       num_tracks;
    uint32_t                       current_track_number;
} input_movie_t;

typedef struct
{
    char    *raw_track_option;
    int16_t  alternate_group;
    uint16_t ISO_language;
    uint32_t seek;
    int      consider_rap;
    char    *handler_name;
} track_media_option;

typedef struct
{
    output_movie_t      *output;
    input_movie_t       *input;
    track_media_option **track_option;
    int                  num_input;
    int                  add_bom_to_chpl;
    int                  ref_chap_available;
    uint32_t             chap_track;
    char                *chap_file;
    uint16_t             default_language;
} remuxer_t;

typedef struct
{
    char *whole_track_option;
    int   num_track_delimiter;
} file_option;

static void cleanup_input_movie( input_movie_t *input )
{
    if( !input )
        return;
    lsmash_destroy_itunes_metadata( input->itunes_meta_list );
    if( input->track )
        free( input->track );
    lsmash_destroy_root( input->root );
    input->root = NULL;
    input->track = NULL;
    input->itunes_meta_list = NULL;
}

static void cleanup_output_movie( output_movie_t *output )
{
    if( !output )
        return;
    if( output->track )
        free( output->track );
    lsmash_destroy_root( output->root );
    output->root = NULL;
    output->track = NULL;
}

static void cleanup_remuxer( remuxer_t *remuxer )
{
    for( int i = 0; i < remuxer->num_input; i++ )
    {
        cleanup_input_movie( &remuxer->input[i] );
        if( remuxer->track_option[i] )
            free( remuxer->track_option[i] );
    }
    cleanup_output_movie( remuxer->output );
}

#define eprintf( ... ) fprintf( stderr, __VA_ARGS__ )
#define REFRESH_CONSOLE eprintf( "                                                                               \r" )

static int remuxer_error( remuxer_t *remuxer, const char* message, ... )
{
    cleanup_remuxer( remuxer );
    REFRESH_CONSOLE;
    eprintf( "Error: " );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    return -1;
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

#define REMUXER_ERR( ... ) remuxer_error( &remuxer, __VA_ARGS__ )
#define ERROR_MSG( ... ) error_message( __VA_ARGS__ )
#define WARNING_MSG( ... ) warning_message( __VA_ARGS__ )

static void display_help( void )
{
    eprintf( "\n"
             "L-SMASH isom/mov re-muliplexer rev%s  %s\n"
             "Built on %s %s\n"
             "Copyright (C) 2011-2012 L-SMASH project\n"
             "\n"
             "Usage: remuxer -i input1 [-i input2 -i input3 ...] -o output\n"
             "Global options:\n"
             "    --help                    Display help.\n"
             "    --chapter <string>        Set chapters from the file.\n"
             "    --chpl-with-bom           Add UTF-8 BOM to the chapter strings\n"
             "                              in the chapter list. (experimental)\n"
             "    --chapter-track <integer> Set which track the chapter applies to.\n"
             "                              This option takes effect only when reference\n"
             "                              chapter is available.\n"
             "                              If this option is not used, it defaults to 1.\n"
             "    --language <string>       Specify the default language for all the output tracks.\n"
             "                              This option is overridden by the track options.\n"
             "Track options:\n"
             "    language=<string>         Specify media language\n"
             "    alternate-group=<integer> Specify alternate group\n"
             "    handler=<string>          Set media handler name\n"
             "    seek=<integer>            Specify starting point in media\n"
             "    safe-seek=<integer>       Same as seek except for considering random accessible point\n"
             "                              Media starts from the closest random accessible point\n"
             "How to use track options:\n"
             "    -i input?[track_number1]:[track_option1],[track_option2]?[track_number2]:...\n"
             "For example:\n"
             "    remuxer -i input1 -i input2?2:alternate-group=1?3:language=jpn,alternate-group=1 -o output\n",
             LSMASH_REV, LSMASH_GIT_HASH, __DATE__, __TIME__ );
}

static int get_movie( input_movie_t *input, char *input_name )
{
    if( !strcmp( input_name, "-" ) )
        return ERROR_MSG( "standard input not supported.\n" );
    input->root = lsmash_open_movie( input_name, LSMASH_FILE_MODE_READ );
    if( !input->root )
        return ERROR_MSG( "failed to open input file.\n" );
    input->itunes_meta_list = lsmash_export_itunes_metadata( input->root );
    if( !input->itunes_meta_list )
        return ERROR_MSG( "failed to get iTunes metadata.\n" );
    input->current_track_number = 1;
    lsmash_movie_parameters_t *movie_param = &input->movie_param;
    lsmash_initialize_movie_parameters( movie_param );
    lsmash_get_movie_parameters( input->root, movie_param );
    uint32_t num_tracks = input->num_tracks = movie_param->number_of_tracks;
    /* Create tracks. */
    input_track_t *in_track = input->track = malloc( num_tracks * sizeof(input_track_t) );
    if( !in_track )
        return ERROR_MSG( "failed to alloc input tracks.\n" );
    memset( in_track, 0, num_tracks * sizeof(input_track_t) );
    for( uint32_t i = 0; i < num_tracks; i++ )
    {
        in_track[i].track_ID = lsmash_get_track_ID( input->root, i + 1 );
        if( !in_track[i].track_ID )
            return ERROR_MSG( "failed to get track_ID.\n" );
    }
    for( uint32_t i = 0; i < num_tracks; i++ )
    {
        lsmash_initialize_track_parameters( &in_track[i].track_param );
        if( lsmash_get_track_parameters( input->root, in_track[i].track_ID, &in_track[i].track_param ) )
        {
            WARNING_MSG( "failed to get track parameters.\n" );
            continue;
        }
        lsmash_initialize_media_parameters( &in_track[i].media_param );
        if( lsmash_get_media_parameters( input->root, in_track[i].track_ID, &in_track[i].media_param ) )
        {
            WARNING_MSG( "failed to get media parameters.\n" );
            continue;
        }
        if( lsmash_construct_timeline( input->root, in_track[i].track_ID ) )
        {
            WARNING_MSG( "failed to construct timeline.\n" );
            continue;
        }
        if( lsmash_get_last_sample_delta_from_media_timeline( input->root, in_track[i].track_ID, &in_track[i].last_sample_delta ) )
        {
            WARNING_MSG( "failed to get the last sample delta.\n" );
            continue;
        }
        in_track[i].active                = 1;
        in_track[i].current_sample_number = 1;
        in_track[i].sample                = NULL;
        in_track[i].dts                   = 0;
        in_track[i].composition_delay     = 0;
        in_track[i].skip_duration         = 0;
    }
    lsmash_discard_boxes( input->root );
    return 0;
}

static int parse_track_option( remuxer_t *remuxer )
{
    track_media_option **track = remuxer->track_option;
    for( int i = 0; i < remuxer->num_input; i++ )
        for( uint32_t j = 0; j < remuxer->input[i].num_tracks; j++ )
        {
            track_media_option *current_track_opt = &track[i][j];
            if( current_track_opt->raw_track_option == NULL )
                break;
            if( !strchr( current_track_opt->raw_track_option, ':' )
             || strchr( current_track_opt->raw_track_option, ':' ) == current_track_opt->raw_track_option )
                return ERROR_MSG( "track number is not specified in %s\n", current_track_opt->raw_track_option );
            if( strchr( current_track_opt->raw_track_option, ':' ) != strrchr( current_track_opt->raw_track_option, ':' ) )
                return ERROR_MSG( "multiple colons inside one track option in %s.\n", current_track_opt->raw_track_option );
            uint32_t track_number = atoi( strtok( current_track_opt->raw_track_option, ":" ) );
            if( track_number == 0 )
                return ERROR_MSG( "%s is an invalid track number.\n", strtok( current_track_opt->raw_track_option, ":" ) );
            if( track_number > remuxer->input[i].num_tracks )
                return ERROR_MSG( "%d is an invalid track number.\n", track_number );
            char *track_option;
            while( (track_option = strtok( NULL, "," )) != NULL )
            {
                if( strchr( track_option, '=' ) != strrchr( track_option, '=' ) )
                    return ERROR_MSG( "multiple equal signs inside one track option in %s\n", track_option );
                if( strstr( track_option, "alternate-group=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    track[i][track_number - 1].alternate_group = atoi( track_parameter );
                }
                else if( strstr( track_option, "language=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    track[i][track_number - 1].ISO_language = lsmash_pack_iso_language( track_parameter );
                }
                else if( strstr( track_option, "handler=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    track[i][track_number - 1].handler_name = track_parameter;
                }
                else if( strstr( track_option, "safe-seek=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    track[i][track_number - 1].seek = atoi( track_parameter );
                    track[i][track_number - 1].consider_rap = 1;
                }
                else if( strstr( track_option, "seek=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    track[i][track_number - 1].seek = atoi( track_parameter );
                }
                else
                    return ERROR_MSG( "unknown track option %s\n", track_option );
            }
        }
    return 0;
}

static int parse_cli_option( int argc, char **argv, remuxer_t *remuxer )
{
    input_movie_t       *input = remuxer->input;
    track_media_option **track_option = remuxer->track_option;
    file_option          input_file_option[remuxer->num_input];
    int input_movie_number = 0;
    for( int i = 1; i < argc ; i++ )
    {
        /* Get input movies. */
        if( !strcasecmp( argv[i], "-i" ) || !strcasecmp( argv[i], "--input" ) )    /* input file */
        {
            if( ++i == argc )
                return ERROR_MSG( "-i requires an argument.\n" );
            input_file_option[input_movie_number].num_track_delimiter = 0;
            char *p = argv[i];
            while( *p )
                input_file_option[input_movie_number].num_track_delimiter += (*p++ == '?');
            if( get_movie( &input[input_movie_number], strtok( argv[i], "?" ) ) )
                return ERROR_MSG( "failed to get input movie.\n" );
            uint32_t num_tracks = input[input_movie_number].num_tracks;
            track_option[input_movie_number] = malloc( num_tracks * sizeof(track_media_option) );
            if( !track_option[input_movie_number] )
                return ERROR_MSG( "couldn't allocate memory.\n" );
            memset( track_option[input_movie_number], 0, num_tracks * sizeof(track_media_option) );
            input_file_option[input_movie_number].whole_track_option = strtok( NULL, "" );
            input_movie_number++;
        }
        /* Create output movie. */
        else if( !strcasecmp( argv[i], "-o" ) || !strcasecmp( argv[i], "--output" ) )    /* output file */
        {
            if( ++i == argc )
                return ERROR_MSG( "-o requires an argument.\n" );
            remuxer->output->root = lsmash_open_movie( argv[i], LSMASH_FILE_MODE_WRITE );
            if( !remuxer->output->root )
                return ERROR_MSG( "failed to open output movie.\n" );
        }
        else if( !strcasecmp( argv[i], "--chapter" ) )    /* chapter file */
        {
            if( ++i == argc )
                return ERROR_MSG( "--chapter requires an argument.\n" );
            remuxer->chap_file = argv[i];
        }
        else if( !strcasecmp( argv[i], "--chpl-with-bom" ) )
            remuxer->add_bom_to_chpl = 1;
        else if( !strcasecmp( argv[i], "--chapter-track" ) )    /* track to apply reference chapter to */
        {
            if( ++i == argc )
                return ERROR_MSG( "--chapter-track requires an argument.\n" );
            remuxer->chap_track = atoi( argv[i] );
            if( !remuxer->chap_track )
                return ERROR_MSG( "%s is an invalid track number.\n", argv[i] );
        }
        else if( !strcasecmp( argv[i], "--language" ) )
        {
            if( ++i == argc )
                return ERROR_MSG( "--chapter requires an argument.\n" );
            remuxer->default_language = lsmash_pack_iso_language( argv[i] );
        }
        else
            return ERROR_MSG( "unkown option found: %s\n", argv[i] );
    }
    if( !remuxer->output->root )
        return ERROR_MSG( "output file name is not specified.\n" );
    /* Parse track options */
    /* Get the current track and media parameters */
    for( int i = 0; i < remuxer->num_input; i++ )
        for( uint32_t j = 0; j < input[i].num_tracks; j++ )
        {
            input_track_t *in_track = &input[i].track[j];
            if( !in_track->active )
                continue;
            track_option[i][j].alternate_group = in_track->track_param.alternate_group;
            track_option[i][j].ISO_language = in_track->media_param.ISO_language;
            track_option[i][j].handler_name = in_track->media_param.media_handler_name;
        }
    /* Set the default language */
    if( remuxer->default_language )
        for( int i = 0; i < remuxer->num_input; i++ )
            for( uint32_t j = 0; j < input[i].num_tracks; j++ )
                track_option[i][j].ISO_language = remuxer->default_language;
    /* Get the track and media parameters specified by users */
    for( int i = 0; i < remuxer->num_input; i++ )
    {
        if( input_file_option[i].num_track_delimiter > input[i].num_tracks )
            return ERROR_MSG( "more track options specified than the actual number of the tracks (%"PRIu32").\n", input[i].num_tracks );
        if( input_file_option[i].num_track_delimiter )
        {
            track_option[i][0].raw_track_option = strtok( input_file_option[i].whole_track_option, "?" );
            for( int j = 1; j < input_file_option[i].num_track_delimiter ; j++ )
                track_option[i][j].raw_track_option = strtok( NULL, "?" );
        }
    }
    if( parse_track_option( remuxer ) )
        return ERROR_MSG( "failed to parse track options.\n" );
    return 0;
}

static int set_movie_parameters( remuxer_t *remuxer )
{
    int             num_input = remuxer->num_input;
    input_movie_t  *input     = remuxer->input;
    output_movie_t *output    = remuxer->output;
    lsmash_initialize_movie_parameters( &output->movie_param );
    /* Pick the most used major_brands. */
    lsmash_brand_type major_brand      [num_input];
    uint32_t          minor_version    [num_input];
    uint32_t          major_brand_count[num_input];
    uint32_t          num_major_brand = 0;
    for( int i = 0; i < num_input; i++ )
    {
        major_brand      [num_major_brand] = input[i].movie_param.major_brand;
        minor_version    [num_major_brand] = input[i].movie_param.minor_version;
        major_brand_count[num_major_brand] = 0;
        for( int j = 0; j < num_input; j++ )
            if( (major_brand  [num_major_brand] == input[j].movie_param.major_brand)
             && (minor_version[num_major_brand] == input[j].movie_param.minor_version) )
            {
                if( i <= j )
                    ++major_brand_count[num_major_brand];
                else
                {
                    /* This major_brand already exists. Skip this. */
                    major_brand_count[num_major_brand] = 0;
                    --num_major_brand;
                    break;
                }
            }
        ++num_major_brand;
    }
    uint32_t most_used_count = 0;
    for( uint32_t i = 0; i < num_major_brand; i++ )
        if( major_brand_count[i] > most_used_count )
        {
            most_used_count = major_brand_count[i];
            output->movie_param.major_brand   = major_brand  [i];
            output->movie_param.minor_version = minor_version[i];
        }
    /* Deduplicate compatible brands. */
    uint32_t num_input_brands = 0;
    for( int i = 0; i < num_input; i++ )
        num_input_brands += input[i].movie_param.number_of_brands;
    lsmash_brand_type input_brands[num_input_brands];
    num_input_brands = 0;
    for( int i = 0; i < num_input; i++ )
        for( uint32_t j = 0; j < input[i].movie_param.number_of_brands; j++ )
            input_brands[num_input_brands++] = input[i].movie_param.brands[j];
    lsmash_brand_type output_brands[num_input_brands];
    uint32_t num_output_brands = 0;
    for( uint32_t i = 0; i < num_input_brands; i++ )
    {
        if( !input_brands[i] )
            continue;
        output_brands[num_output_brands] = input_brands[i];
        for( uint32_t j = 0; j < num_output_brands; j++ )
            if( output_brands[num_output_brands] == output_brands[j] )
            {
                /* This brand already exists. Skip this. */
                --num_output_brands;
                break;
            }
        ++num_output_brands;
    }
    output->movie_param.number_of_brands = num_output_brands;
    output->movie_param.brands           = output_brands;
    if( remuxer->chap_file )
        for( uint32_t i = 0; i < output->movie_param.number_of_brands; i++ )
            if( output->movie_param.brands[i] == ISOM_BRAND_TYPE_QT  || output->movie_param.brands[i] == ISOM_BRAND_TYPE_M4A
             || output->movie_param.brands[i] == ISOM_BRAND_TYPE_M4B || output->movie_param.brands[i] == ISOM_BRAND_TYPE_M4P
             || output->movie_param.brands[i] == ISOM_BRAND_TYPE_M4V )
            {
                remuxer->ref_chap_available = 1;
                break;
            }
    return lsmash_set_movie_parameters( output->root, &output->movie_param );
}

static int set_itunes_metadata( output_movie_t *output, input_movie_t *input, int num_input )
{
    for( int i = 0; i < num_input; i++ )
        if( lsmash_import_itunes_metadata( output->root, input[i].itunes_meta_list ) )
            return -1;
    return 0;
}

static int set_starting_point( input_movie_t *input, input_track_t *in_track, uint32_t seek_point, int consider_rap )
{
    if( seek_point == 0 )
        return 0;
    uint32_t rap_number;
    if( lsmash_get_closest_random_accessible_point_from_media_timeline( input->root, in_track->track_ID, 1, &rap_number ) )
    {
        if( consider_rap )
            return ERROR_MSG( "failed to get the first random accessible point.\n" );
        else
        {
            WARNING_MSG( "no random access point!\n" );
            /* Set number of the first sample to be muxed. */
            in_track->current_sample_number = seek_point;
            return 0;
        }
    }
    /* Get composition delay. */
    uint64_t rap_dts;
    uint64_t rap_cts;
    uint32_t ctd_shift;
    if( lsmash_get_dts_from_media_timeline( input->root, in_track->track_ID, rap_number, &rap_dts ) )
        return ERROR_MSG( "failed to get CTS of the first random accessible sample of seek point.\n" );
    if( lsmash_get_cts_from_media_timeline( input->root, in_track->track_ID, rap_number, &rap_cts ) )
        return ERROR_MSG( "failed to get CTS of the first random accessible sample of seek point.\n" );
    if( lsmash_get_composition_to_decode_shift_from_media_timeline( input->root, in_track->track_ID, &ctd_shift ) )
        return ERROR_MSG( "failed to get composition to decode timeline shfit.\n" );
    in_track->composition_delay = rap_cts - rap_dts + ctd_shift;
    /* Check if starting point is random accessible. */
    if( lsmash_get_closest_random_accessible_point_from_media_timeline( input->root, in_track->track_ID, seek_point, &rap_number ) )
        return ERROR_MSG( "failed to get a random accessible point.\n" );
    if( rap_number != seek_point )
    {
        WARNING_MSG( "Warning: starting point you specified is not a random accessible point.\n" );
        if( consider_rap )
        {
            /* Get duration that should be skipped. */
            if( lsmash_get_cts_from_media_timeline( input->root, in_track->track_ID, rap_number, &rap_cts ) )
                return ERROR_MSG( "failed to get CTS of the closest and past random accessible sample of starting point.\n" );
            uint64_t seek_cts;
            if( lsmash_get_cts_from_media_timeline( input->root, in_track->track_ID, seek_point, &seek_cts ) )
                return ERROR_MSG( "failed to get CTS of starting point.\n" );
            if( rap_cts < seek_cts )
                in_track->skip_duration = seek_cts - rap_cts;
        }
    }
    /* Set number of the first sample to be muxed. */
    in_track->current_sample_number = consider_rap ? rap_number : seek_point;
    return 0;
}

static int prepare_output( remuxer_t *remuxer )
{
    if( set_movie_parameters( remuxer ) )
        return ERROR_MSG( "failed to set output movie parameters.\n" );
    output_movie_t *output = remuxer->output;
    input_movie_t  *input  = remuxer->input;
    if( set_itunes_metadata( output, input, remuxer->num_input ) )
        return ERROR_MSG( "failed to set iTunes metadata.\n" );
    /* Allocate output tracks. */
    for( int i = 0; i < remuxer->num_input; i++ )
        output->num_tracks += input[i].num_tracks;
    output->track = malloc( output->num_tracks * sizeof(output_track_t) );
    if( !output->track )
        return ERROR_MSG( "failed to alloc output tracks.\n" );
    track_media_option **track_option = remuxer->track_option;
    output->current_track_number = 1;
    for( int i = 0; i < remuxer->num_input; i++ )
        for( uint32_t j = 0; j < input[i].num_tracks; j++ )
        {
            input_track_t *in_track = &input[i].track[j];
            if( !in_track->active )
            {
                -- output->num_tracks;
                continue;
            }
            output_track_t *out_track = &output->track[output->current_track_number - 1];
            out_track->track_ID = lsmash_create_track( output->root, in_track->media_param.handler_type );
            if( !out_track->track_ID )
                return ERROR_MSG( "failed to create a track.\n" );
            /* Copy track and media parameters except for track_ID. */
            out_track->track_param = in_track->track_param;
            out_track->media_param = in_track->media_param;
            /* Set track and media parameters specified by users */
            out_track->track_param.alternate_group    = track_option[i][j].alternate_group;
            out_track->media_param.ISO_language       = track_option[i][j].ISO_language;
            out_track->media_param.media_handler_name = track_option[i][j].handler_name;
            out_track->track_param.track_ID           = out_track->track_ID;
            if( lsmash_set_track_parameters( output->root, out_track->track_ID, &out_track->track_param ) )
            {
                WARNING_MSG( "failed to set track parameters.\n" );
                lsmash_delete_track( output->root, out_track->track_ID );
                in_track->active = 0;
                -- output->num_tracks;
                continue;
            }
            if( lsmash_set_media_parameters( output->root, out_track->track_ID, &out_track->media_param ) )
            {
                WARNING_MSG( "failed to set media parameters.\n" );
                lsmash_delete_track( output->root, out_track->track_ID );
                in_track->active = 0;
                -- output->num_tracks;
                continue;
            }
            if( lsmash_copy_decoder_specific_info( output->root, out_track->track_ID, input[i].root, in_track->track_ID ) )
            {
                WARNING_MSG( "failed to copy a Decoder Specific Info.\n" );
                lsmash_delete_track( output->root, out_track->track_ID );
                in_track->active = 0;
                -- output->num_tracks;
                continue;
            }
            out_track->last_sample_delta = in_track->last_sample_delta;
            if( set_starting_point( input, in_track, track_option[i][j].seek, track_option[i][j].consider_rap ) )
            {
                WARNING_MSG( "failed to set starting point.\n" );
                lsmash_delete_track( output->root, out_track->track_ID );
                in_track->active = 0;
                -- output->num_tracks;
                continue;
            }
            out_track->current_sample_number = 1;
            out_track->skip_dt_interval      = 0;
            out_track->last_sample_dts       = 0;
            ++ output->current_track_number;
        }
    output->current_track_number = 1;
    return 0;
}

static void set_reference_chapter_track( remuxer_t *remuxer )
{
    if( remuxer->ref_chap_available )
        lsmash_create_reference_chapter_track( remuxer->output->root, remuxer->chap_track, remuxer->chap_file );
}

static int do_remux( remuxer_t *remuxer )
{
#define LSMASH_MAX( a, b ) ((a) > (b) ? (a) : (b))
    output_movie_t *out_movie = remuxer->output;
    input_movie_t  *inputs    = remuxer->input;
    set_reference_chapter_track( remuxer );
    double   largest_dts = 0;
    uint32_t input_movie_number = 1;
    uint32_t num_consecutive_sample_skip = 0;
    uint32_t num_active_input_tracks = out_movie->num_tracks;
    uint64_t total_media_size = 0;
    uint8_t  sample_count = 0;
    while( 1 )
    {
        input_movie_t *in_movie = &inputs[input_movie_number - 1];
        input_track_t *in_track = &in_movie->track[in_movie->current_track_number - 1];
        if( !in_track->active )
        {
            /* Move the next track. */
            if( ++ in_movie->current_track_number > in_movie->num_tracks )
            {
                /* Move the next input movie. */
                in_movie->current_track_number = 1;
                ++input_movie_number;
            }
            if( input_movie_number > remuxer->num_input )
                input_movie_number = 1;                 /* Back the first input movie. */
            continue;
        }
        /* Try append a sample in an input track where we didn't reach the end of media timeline. */
        if( !in_track->reach_end_of_media_timeline )
        {
            lsmash_sample_t *sample = in_track->sample;
            /* Get a new sample data if the track doesn't hold any one. */
            if( !sample )
            {
                sample = lsmash_get_sample_from_media_timeline( in_movie->root, in_track->track_ID, in_track->current_sample_number );
                if( sample )
                {
                    in_track->sample = sample;
                    in_track->dts = (double)sample->dts / in_track->media_param.timescale;
                }
                else
                {
                    if( lsmash_check_sample_existence_in_media_timeline( in_movie->root, in_track->track_ID, in_track->current_sample_number ) )
                    {
                        ERROR_MSG( "failed to get a sample.\n" );
                        break;
                    }
                    /* No more appendable samples in this track. */
                    in_track->sample = NULL;
                    in_track->reach_end_of_media_timeline = 1;
                    if( --num_active_input_tracks == 0 )
                        break;      /* end of muxing */
                }
            }
            if( sample )
            {
                /* Append a sample if meeting a condition. */
                if( in_track->dts <= largest_dts || num_consecutive_sample_skip == num_active_input_tracks )
                {
                    /* The first DTS must be 0. */
                    output_track_t *out_track = &out_movie->track[out_movie->current_track_number - 1];
                    if( out_track->current_sample_number == 1 )
                        out_track->skip_dt_interval = sample->dts;
                    if( out_track->skip_dt_interval )
                    {
                        sample->dts -= out_track->skip_dt_interval;
                        sample->cts -= out_track->skip_dt_interval;
                    }
                    uint64_t sample_size     = sample->length;      /* sample might be deleted internally after appending. */
                    uint64_t last_sample_dts = sample->dts;         /* same as above */
                    /* Append a sample into output movie. */
                    if( lsmash_append_sample( out_movie->root, out_track->track_ID, sample ) )
                    {
                        lsmash_delete_sample( sample );
                        return ERROR_MSG( "failed to append a sample.\n" );
                    }
                    largest_dts                       = LSMASH_MAX( largest_dts, in_track->dts );
                    in_track->sample                  = NULL;
                    in_track->current_sample_number  += 1;
                    out_track->current_sample_number += 1;
                    out_track->last_sample_dts        = last_sample_dts;
                    num_consecutive_sample_skip       = 0;
                    total_media_size                 += sample_size;
                    /* Print, per 256 samples, total size of imported media. */
                    if( ++sample_count == 0 )
                        eprintf( "Importing: %"PRIu64" bytes\r", total_media_size );
                }
                else
                    ++num_consecutive_sample_skip;      /* Skip appendig sample. */
            }
        }
        /* Move the next track. */
        if( ++ in_movie->current_track_number > in_movie->num_tracks )
        {
            /* Move the next input movie. */
            in_movie->current_track_number = 1;
            ++input_movie_number;
        }
        if( input_movie_number > remuxer->num_input )
            input_movie_number = 1;                 /* Back the first input movie. */
        if( ++ out_movie->current_track_number > out_movie->num_tracks )
            out_movie->current_track_number = 1;    /* Back the first track in the output movie. */
    }
    for( uint32_t i = 0; i < out_movie->num_tracks; i++ )
        if( lsmash_flush_pooled_samples( out_movie->root, out_movie->track[i].track_ID, out_movie->track[i].last_sample_delta ) )
            return ERROR_MSG( "failed to flush samples.\n" );
    return 0;
#undef LSMASH_MAX
}

static int construct_timeline_maps( remuxer_t *remuxer )
{
    output_movie_t      *output       = remuxer->output;
    input_movie_t       *input        = remuxer->input;
    track_media_option **track_option = remuxer->track_option;
    output->current_track_number = 1;
    for( int i = 0; i < remuxer->num_input; i++ )
        for( uint32_t j = 0; j < input[i].num_tracks; j++ )
        {
            input_track_t *in_track  = &input[i].track[j];
            if( !in_track->active )
                continue;
            output_track_t *out_track = &output->track[output->current_track_number ++ - 1];
            if( track_option[i][j].seek )
            {
                /* Reconstruct timeline maps. */
                if( lsmash_delete_explicit_timeline_map( output->root, out_track->track_ID ) )
                    return ERROR_MSG( "failed to delete explicit timeline maps.\n" );
                uint32_t movie_timescale = lsmash_get_movie_timescale( output->root );
                uint32_t media_timescale = lsmash_get_media_timescale( output->root, out_track->track_ID );
                if( !media_timescale )
                    return ERROR_MSG( "media timescale is broken.\n" );
                double timescale_convert_multiplier = (double)movie_timescale / media_timescale;
                lsmash_edit_t edit;
                edit.start_time = in_track->composition_delay + in_track->skip_duration;
                if( edit.start_time )
                {
                    uint64_t empty_duration = edit.start_time + lsmash_get_composition_to_decode_shift( output->root, out_track->track_ID );
                    lsmash_edit_t empty_edit;
                    empty_edit.duration   = empty_duration * timescale_convert_multiplier + 0.5;
                    empty_edit.start_time = ISOM_EDIT_MODE_EMPTY;
                    empty_edit.rate       = ISOM_EDIT_MODE_NORMAL;
                    if( lsmash_create_explicit_timeline_map( output->root, out_track->track_ID, empty_edit ) )
                        return ERROR_MSG( "failed to create a empty duration.\n" );
                }
                edit.duration = (out_track->last_sample_dts + out_track->last_sample_delta - in_track->skip_duration) * timescale_convert_multiplier;
                edit.rate     = ISOM_EDIT_MODE_NORMAL;
                if( lsmash_create_explicit_timeline_map( output->root, out_track->track_ID, edit ) )
                    return ERROR_MSG( "failed to create a explicit timeline map.\n" );
            }
            else if( lsmash_copy_timeline_map( output->root, out_track->track_ID, input[i].root, in_track->track_ID ) )
                return ERROR_MSG( "failed to copy timeline maps.\n" );
        }
    return 0;
}

static int moov_to_front_callback( void *param, uint64_t written_movie_size, uint64_t total_movie_size )
{
    REFRESH_CONSOLE;
    eprintf( "Finalizing: [%5.2lf%%]\r", ((double)written_movie_size / total_movie_size) * 100.0 );
    return 0;
}

static int finish_movie( remuxer_t *remuxer )
{
    output_movie_t *output = remuxer->output;
    /* Set chapter list */
    if( remuxer->chap_file )
        lsmash_set_tyrant_chapter( output->root, remuxer->chap_file, remuxer->add_bom_to_chpl );
    /* Finish muxing. */
    lsmash_adhoc_remux_t moov_to_front;
    moov_to_front.func        = moov_to_front_callback;
    moov_to_front.buffer_size = 4*1024*1024;    /* 4MiB */
    moov_to_front.param       = NULL;
    REFRESH_CONSOLE;
    return lsmash_finish_movie( output->root, &moov_to_front );
}

int main( int argc, char *argv[] )
{
    if( argc < 5 || !strcasecmp( argv[1], "-h" ) || !strcasecmp( argv[1], "--help" ) )
    {
        display_help();
        return argc < 5 ? -1 : 0;
    }
    int num_input = 0;
    for( int i = 1 ; i < argc ; i++ )
        if( !strcasecmp( argv[i], "-i" ) || !strcasecmp( argv[i], "--input" ) )
            num_input++;
    if( !num_input )
        return ERROR_MSG( "no input file specified.\n" );
    output_movie_t      output = { 0 };
    input_movie_t       input[ num_input ];
    track_media_option *track_option[ num_input ];
    remuxer_t           remuxer = { &output, input, track_option, num_input, 0, 0, 1, NULL, 0 };
    memset( input, 0, num_input * sizeof(input_movie_t) );
    memset( track_option, 0, num_input * sizeof(track_media_option *) );
    if( parse_cli_option( argc, argv, &remuxer ) )
        return REMUXER_ERR( "failed to parse command line options.\n" );
    if( prepare_output( &remuxer ) )
        return REMUXER_ERR( "failed to set up preparation for output.\n" );
    if( do_remux( &remuxer ) )
        return REMUXER_ERR( "failed to remux movies.\n" );
    if( construct_timeline_maps( &remuxer ) )
        return REMUXER_ERR( "failed to construct timeline maps.\n" );
    if( finish_movie( &remuxer ) )
        return REMUXER_ERR( "failed to finish output movie.\n" );
    REFRESH_CONSOLE;
    eprintf( "Remuxing completed!\n" );
    cleanup_remuxer( &remuxer );
    return 0;
}
