/*****************************************************************************
 * remuxer.c:
 *****************************************************************************
 * Copyright (C) 2011-2015 L-SMASH project
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

#include "cli.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>

typedef struct
{
    uint32_t                  track_ID;
    uint32_t                  last_sample_delta;
    uint32_t                  current_sample_number;
    uint32_t                 *summary_remap;
    uint64_t                  skip_dt_interval;
    uint64_t                  last_sample_dts;
    lsmash_track_parameters_t track_param;
    lsmash_media_parameters_t media_param;
} output_track_t;

typedef struct
{
    output_track_t           *track;
    lsmash_movie_parameters_t param;
    uint32_t                  num_tracks;
    uint32_t                  current_track_number;
} output_movie_t;

typedef struct
{
    const char              *name;
    lsmash_file_t           *fh;
    lsmash_file_parameters_t param;
    lsmash_file_parameters_t seg_param;
    output_movie_t           movie;
    uint32_t                 current_subseg_number;
} output_file_t;

typedef struct
{
    lsmash_root_t *root;
    output_file_t  file;
    uint32_t       current_seg_number;
} output_t;

typedef struct
{
    int               active;
    lsmash_summary_t *summary;
} input_summary_t;

typedef struct
{
    lsmash_file_t           *fh;
    lsmash_file_parameters_t param;
} input_data_ref_t;

typedef struct
{
    lsmash_media_parameters_t param;
    uint32_t                  num_data_refs;
    input_data_ref_t         *data_refs;
} input_media_t;

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
    uint32_t                  current_sample_index;
    uint32_t                  num_summaries;
    input_summary_t          *summaries;
    lsmash_track_parameters_t track_param;
    input_media_t             media;
} input_track_t;

typedef struct
{
    input_track_t                 *track;
    lsmash_itunes_metadata_t      *itunes_metadata;
    lsmash_movie_parameters_t      param;
    uint32_t                       movie_ID;
    uint32_t                       num_tracks;
    uint32_t                       num_itunes_metadata;
    uint32_t                       current_track_number;
} input_movie_t;

typedef struct
{
    lsmash_file_t           *fh;
    lsmash_file_parameters_t param;
    input_movie_t            movie;
} input_file_t;

typedef struct
{
    lsmash_root_t *root;
    input_file_t   file;
} input_t;

typedef struct
{
    char    *raw_track_option;
    int      remove;
    int      disable;
    int16_t  alternate_group;
    uint16_t ISO_language;
    uint32_t seek;
    int      consider_rap;
    char    *handler_name;
} track_media_option;

typedef struct
{
    output_t            *output;
    input_t             *input;
    track_media_option **track_option;
    int                  num_input;
    int                  add_bom_to_chpl;
    int                  ref_chap_available;
    uint32_t             chap_track;
    char                *chap_file;
    uint16_t             default_language;
    uint32_t             frag_base_track;
    uint32_t             subseg_per_seg;
    int                  dash;
} remuxer_t;

typedef struct
{
    char *whole_track_option;
    int   num_track_delimiter;
} file_option;

static void cleanup_input_movie( input_t *input )
{
    if( !input )
        return;
    input_movie_t *in_movie = &input->file.movie;
    if( in_movie->itunes_metadata )
    {
        for( uint32_t i = 0; i < in_movie->num_itunes_metadata; i++ )
            lsmash_cleanup_itunes_metadata( &in_movie->itunes_metadata[i] );
        lsmash_freep( &in_movie->itunes_metadata );
    }
    if( in_movie->track )
    {
        for( uint32_t i = 0; i < in_movie->num_tracks; i++ )
        {
            input_track_t *in_track = &in_movie->track[i];
            if( in_track->summaries )
            {
                for( uint32_t j = 0; j < in_track->num_summaries; j++ )
                    lsmash_cleanup_summary( in_track->summaries[j].summary );
                lsmash_free( in_track->summaries );
            }
            input_media_t *in_media = &in_track->media;
            for( uint32_t j = 0; j < in_media->num_data_refs; j++ )
                if( input->file.fh != in_media->data_refs[j].fh )
                    lsmash_close_file( &in_media->data_refs[j].param );
            lsmash_free( in_media->data_refs );
        }
        lsmash_freep( &in_movie->track );
    }
    lsmash_close_file( &input->file.param );
    lsmash_destroy_root( input->root );
    input->root = NULL;
}

static void cleanup_output_movie( output_t *output )
{
    if( !output )
        return;
    output_movie_t *out_movie = &output->file.movie;
    if( out_movie->track )
    {
        for( uint32_t i = 0; i < out_movie->num_tracks; i++ )
            lsmash_free( out_movie->track[i].summary_remap );
        lsmash_freep( &out_movie->track );
    }
    if( !(output->file.seg_param.mode & LSMASH_FILE_MODE_INITIALIZATION) )
    {
        lsmash_freep( &output->file.seg_param.brands );
        lsmash_close_file( &output->file.seg_param );
    }
    lsmash_freep( &output->file.param.brands );
    lsmash_close_file( &output->file.param );
    lsmash_destroy_root( output->root );
    output->root = NULL;
}

static void cleanup_remuxer( remuxer_t *remuxer )
{
    for( int i = 0; i < remuxer->num_input; i++ )
    {
        cleanup_input_movie( &remuxer->input[i] );
        lsmash_free( remuxer->track_option[i] );
    }
    lsmash_free( remuxer->track_option );
    lsmash_free( remuxer->input );
    cleanup_output_movie( remuxer->output );
}

#define eprintf( ... ) fprintf( stderr, __VA_ARGS__ )
#define REFRESH_CONSOLE eprintf( "                                                                               \r" )

static int remuxer_error( remuxer_t *remuxer, const char *message, ... )
{
    cleanup_remuxer( remuxer );
    REFRESH_CONSOLE;
    eprintf( "[Error] " );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    return -1;
}

static int error_message( const char *message, ... )
{
    REFRESH_CONSOLE;
    eprintf( "[Error] " );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    return -1;
}

static int warning_message( const char *message, ... )
{
    REFRESH_CONSOLE;
    eprintf( "[Warning] " );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    return -1;
}

#define REMUXER_ERR( ... ) remuxer_error( &remuxer, __VA_ARGS__ )
#define ERROR_MSG( ... ) error_message( __VA_ARGS__ )
#define WARNING_MSG( ... ) warning_message( __VA_ARGS__ )

static void display_version( void )
{
    eprintf( "\n"
             "L-SMASH isom/mov re-muliplexer rev%s  %s\n"
             "Built on %s %s\n"
             "Copyright (C) 2011-2015 L-SMASH project\n",
             LSMASH_REV, LSMASH_GIT_HASH, __DATE__, __TIME__ );
}

static void display_help( void )
{
    display_version();
    eprintf( "\n"
             "Usage: remuxer -i input1 [-i input2 -i input3 ...] -o output\n"
             "Global options:\n"
             "    --help                    Display help.\n"
             "    --version                 Display version information.\n"
             "    --chapter <string>        Set chapters from the file.\n"
             "    --chpl-with-bom           Add UTF-8 BOM to the chapter strings\n"
             "                              in the chapter list. (experimental)\n"
             "    --chapter-track <integer> Set which track the chapter applies to.\n"
             "                              This option takes effect only when reference\n"
             "                              chapter is available.\n"
             "                              If this option is not used, it defaults to 1.\n"
             "    --language <string>       Specify the default language for all the output tracks.\n"
             "                              This option is overridden by the track options.\n"
             "    --fragment <integer>      Enable fragmentation per random accessible point.\n"
             "                              Set which track the fragmentation is based on.\n"
             "    --dash <integer>          Enable DASH ISOBMFF-based Media segmentation.\n"
             "                              The value is the number of subsegments per segment.\n"
             "                              If zero, Indexed self-initializing Media Segment.\n"
             "                              This option requires --fragment.\n"
             "Track options:\n"
             "    remove                    Remove this track\n"
             "    disable                   Disable this track\n"
             "    language=<string>         Specify media language\n"
             "    alternate-group=<integer> Specify alternate group\n"
             "    handler=<string>          Set media handler name\n"
             "    seek=<integer>            Specify starting point in media\n"
             "    safe-seek=<integer>       Same as seek except for considering random accessible point\n"
             "                              Media starts from the closest random accessible point\n"
             "How to use track options:\n"
             "    -i input?[track_number1]:[track_option1],[track_option2]?[track_number2]:...\n"
             "For example:\n"
             "    remuxer -i input1 -i input2?2:alternate-group=1?3:language=jpn,alternate-group=1 -o output\n" );
}

static char *duplicate_string( char *src )
{
    if( !src )
        return NULL;
    int dst_size = strlen( src ) + 1;
    char *dst = lsmash_malloc( dst_size );
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
        metadata->value.binary.data = lsmash_malloc( shadow.value.binary.size );
        if( !metadata->value.binary.data )
            goto fail;
        memcpy( metadata->value.binary.data, shadow.value.binary.data, shadow.value.binary.size );
        metadata->value.binary.size    = shadow.value.binary.size;
        metadata->value.binary.subtype = shadow.value.binary.subtype;
    }
    else
        metadata->value = shadow.value;
    return 0;
fail:
    lsmash_freep( &metadata->meaning );
    lsmash_freep( &metadata->name );
    return -1;
}

static inline int is_relative_path( const char *path )
{
    return path[0] == '/' || path[0] == '\\' || (path[0] != '\0' && path[1] == ':') ? 0 : 1;
}

static int input_data_reference
(
    input_t                 *input,
    uint32_t                 track_ID,
    input_data_ref_t        *in_data_ref,
    lsmash_data_reference_t *data_ref
)
{
    if( lsmash_open_file( data_ref->location, 1, &in_data_ref->param ) < 0 )
    {
        WARNING_MSG( "failed to open an external media file.\n" );
        return -1;
    }
    in_data_ref->param.mode |= LSMASH_FILE_MODE_MEDIA;
    in_data_ref->fh = lsmash_set_file( input->root, &in_data_ref->param );
    if( !in_data_ref->fh )
    {
        WARNING_MSG( "failed to set an external media file as a data reference.\n" );
        return -1;
    }
    if( lsmash_assign_data_reference( input->root, track_ID, data_ref->index, in_data_ref->fh ) < 0 )
    {
        WARNING_MSG( "failed to assign an external media a data reference.\n" );
        return -1;
    }
    return 0;
}

static int get_movie( input_t *input, char *input_name )
{
    if( !strcmp( input_name, "-" ) )
        return ERROR_MSG( "standard input not supported.\n" );
    /* Read an input file. */
    input->root = lsmash_create_root();
    if( !input->root )
        return ERROR_MSG( "failed to create a ROOT for an input file.\n" );
    input_file_t *in_file = &input->file;
    if( lsmash_open_file( input_name, 1, &in_file->param ) < 0 )
        return ERROR_MSG( "failed to open an input file.\n" );
    in_file->fh = lsmash_set_file( input->root, &in_file->param );
    if( !in_file->fh )
        return ERROR_MSG( "failed to add an input file into a ROOT.\n" );
    if( lsmash_read_file( in_file->fh, &in_file->param ) < 0 )
        return ERROR_MSG( "failed to read an input file\n" );
    /* Get iTunes metadata. */
    input_movie_t *in_movie = &in_file->movie;
    in_movie->num_itunes_metadata = lsmash_count_itunes_metadata( input->root );
    if( in_movie->num_itunes_metadata )
    {
        in_movie->itunes_metadata = lsmash_malloc( in_movie->num_itunes_metadata * sizeof(lsmash_itunes_metadata_t) );
        if( !in_movie->itunes_metadata )
            return ERROR_MSG( "failed to alloc iTunes metadata.\n" );
        uint32_t itunes_metadata_count = 0;
        for( uint32_t i = 1; i <= in_movie->num_itunes_metadata; i++ )
        {
            if( get_itunes_metadata( input->root, i, &in_movie->itunes_metadata[itunes_metadata_count] ) )
            {
                WARNING_MSG( "failed to get an iTunes metadata.\n" );
                continue;
            }
            ++itunes_metadata_count;
        }
        in_movie->num_itunes_metadata = itunes_metadata_count;
    }
    in_movie->current_track_number = 1;
    lsmash_initialize_movie_parameters( &in_movie->param );
    if( lsmash_get_movie_parameters( input->root, &in_movie->param ) )
        return ERROR_MSG( "failed to get movie parameters.\n" );
    uint32_t num_tracks = in_movie->num_tracks = in_movie->param.number_of_tracks;
    /* Create tracks. */
    input_track_t *in_track = in_movie->track = lsmash_malloc_zero( num_tracks * sizeof(input_track_t) );
    if( !in_track )
        return ERROR_MSG( "failed to alloc input tracks.\n" );
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
        lsmash_initialize_media_parameters( &in_track[i].media.param );
        if( lsmash_get_media_parameters( input->root, in_track[i].track_ID, &in_track[i].media.param ) )
        {
            WARNING_MSG( "failed to get media parameters.\n" );
            continue;
        }
        uint32_t data_ref_count = lsmash_count_data_reference( input->root, in_track[i].track_ID );
        if( data_ref_count == 0 )
        {
            WARNING_MSG( "failed to get the number of data references.\n" );
            continue;
        }
        in_track[i].media.data_refs = lsmash_malloc_zero( data_ref_count * sizeof(input_data_ref_t) );
        if( !in_track[i].media.data_refs )
        {
            WARNING_MSG( "failed to allocate handles of data reference.\n" );
            continue;
        }
        for( uint32_t j = 0; j < data_ref_count; j++ )
        {
            input_data_ref_t *in_data_ref = &in_track[i].media.data_refs[j];
            lsmash_data_reference_t data_ref = { .index = j + 1 };
            if( lsmash_get_data_reference( input->root, in_track[i].track_ID, &data_ref ) < 0 )
            {
                WARNING_MSG( "failed to get a data references.\n" );
                continue;
            }
            if( data_ref.location )
            {
                if( is_relative_path( data_ref.location ) && !is_relative_path( input_name ) )
                {
                    /* Append the directory path from the referencing file. */
                    int location_length   = strlen( data_ref.location );
                    int input_name_length = strlen( input_name );
                    char *p = input_name + input_name_length;
                    while( p != input_name && *p != '/' && *p != '\\' )
                        --p;
                    int relative_path_length = p == input_name ? 2 : p - input_name;
                    char *location = lsmash_malloc( relative_path_length + location_length + 2 );
                    if( location )
                    {
                        memcpy( location, input_name, relative_path_length );
                        memcpy( location + relative_path_length + 1, data_ref.location, location_length );
                        location[relative_path_length]                       = '/';
                        location[relative_path_length + location_length + 1] = '\0';
                        lsmash_cleanup_data_reference( &data_ref );
                        data_ref.location = location;
                    }
                }
                int ret = input_data_reference( input, in_track[i].track_ID, in_data_ref, &data_ref );
                lsmash_cleanup_data_reference( &data_ref );
                if( ret < 0 )
                    continue;
            }
            else
            {
                in_data_ref->fh    = in_file->fh;
                in_data_ref->param = in_file->param;
            }
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
        in_track[i].num_summaries = lsmash_count_summary( input->root, in_track[i].track_ID );
        if( in_track[i].num_summaries == 0 )
        {
            WARNING_MSG( "failed to find valid summaries.\n" );
            continue;
        }
        in_track[i].summaries = lsmash_malloc_zero( in_track[i].num_summaries * sizeof(input_summary_t) );
        if( !in_track[i].summaries )
            return ERROR_MSG( "failed to alloc input summaries.\n" );
        for( uint32_t j = 0; j < in_track[i].num_summaries; j++ )
        {
            lsmash_summary_t *summary = lsmash_get_summary( input->root, in_track[i].track_ID, j + 1 );
            if( !summary )
            {
                WARNING_MSG( "failed to get a summary.\n" );
                continue;
            }
            if( !LSMASH_FLAGS_SATISFIED( lsmash_check_codec_support( summary->sample_type ), LSMASH_CODEC_SUPPORT_FLAG_REMUX ) )
            {
                lsmash_cleanup_summary( summary );
                WARNING_MSG( "no support to remux this stream.\n" );
                continue;
            }
            in_track[i].summaries[j].summary = summary;
            in_track[i].summaries[j].active  = 1;
        }
        in_track[i].active                = 1;
        in_track[i].current_sample_number = 1;
        in_track[i].sample                = NULL;
        in_track[i].dts                   = 0;
        in_track[i].composition_delay     = 0;
        in_track[i].skip_duration         = 0;
    }
    lsmash_destroy_children( lsmash_file_as_box( in_file->fh ) );
    return 0;
}

static int parse_track_option( remuxer_t *remuxer )
{
    track_media_option **track = remuxer->track_option;
    for( int i = 0; i < remuxer->num_input; i++ )
        for( uint32_t j = 0; j < remuxer->input[i].file.movie.num_tracks; j++ )
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
            if( track_number > remuxer->input[i].file.movie.num_tracks )
                return ERROR_MSG( "%d is an invalid track number.\n", track_number );
            char *track_option;
            while( (track_option = strtok( NULL, "," )) != NULL )
            {
                if( strchr( track_option, '=' ) != strrchr( track_option, '=' ) )
                    return ERROR_MSG( "multiple equal signs inside one track option in %s\n", track_option );
                current_track_opt = &track[i][track_number - 1];
                if( strstr( track_option, "remove" ) )
                {
                    current_track_opt->remove = 1;
                    /* No need to parse track options for this track anymore. */
                    break;
                }
                else if( strstr( track_option, "disable" ) )
                    current_track_opt->disable = 1;
                else if( strstr( track_option, "alternate-group=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->alternate_group = atoi( track_parameter );
                }
                else if( strstr( track_option, "language=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->ISO_language = lsmash_pack_iso_language( track_parameter );
                }
                else if( strstr( track_option, "handler=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->handler_name = track_parameter;
                }
                else if( strstr( track_option, "safe-seek=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->seek = atoi( track_parameter );
                    current_track_opt->consider_rap = 1;
                }
                else if( strstr( track_option, "seek=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->seek = atoi( track_parameter );
                }
                else
                    return ERROR_MSG( "unknown track option %s\n", track_option );
            }
        }
    return 0;
}

static int parse_cli_option( int argc, char **argv, remuxer_t *remuxer )
{
#define FAILED_PARSE_CLI_OPTION( ... ) \
    do                                     \
    {                                      \
        lsmash_free( input_file_option );  \
        return ERROR_MSG( __VA_ARGS__ );   \
    }                                      \
    while( 0 )
    input_t             *input             = remuxer->input;
    track_media_option **track_option      = remuxer->track_option;
    file_option         *input_file_option = lsmash_malloc( remuxer->num_input * sizeof(file_option) );
    if( !input_file_option )
        return ERROR_MSG( "failed to allocate per-file option.\n" );
    int input_movie_number = 0;
    for( int i = 1; i < argc ; i++ )
    {
        /* Get input movies. */
        if( !strcasecmp( argv[i], "-i" ) || !strcasecmp( argv[i], "--input" ) )    /* input file */
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "-i requires an argument.\n" );
            input_file_option[input_movie_number].num_track_delimiter = 0;
            char *p = argv[i];
            while( *p )
                input_file_option[input_movie_number].num_track_delimiter += (*p++ == '?');
            if( get_movie( &input[input_movie_number], strtok( argv[i], "?" ) ) )
                FAILED_PARSE_CLI_OPTION( "failed to get input movie.\n" );
            uint32_t num_tracks = input[input_movie_number].file.movie.num_tracks;
            track_option[input_movie_number] = lsmash_malloc_zero( num_tracks * sizeof(track_media_option) );
            if( !track_option[input_movie_number] )
                FAILED_PARSE_CLI_OPTION( "couldn't allocate memory.\n" );
            input_file_option[input_movie_number].whole_track_option = strtok( NULL, "" );
            input[input_movie_number].file.movie.movie_ID = input_movie_number + 1;
            ++input_movie_number;
        }
        /* Create output movie. */
        else if( !strcasecmp( argv[i], "-o" ) || !strcasecmp( argv[i], "--output" ) )    /* output file */
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "-o requires an argument.\n" );
            output_t *output = remuxer->output;
            output->root = lsmash_create_root();
            if( !output->root )
                FAILED_PARSE_CLI_OPTION( "failed to create a ROOT.\n" );
            if( lsmash_open_file( argv[i], 0, &output->file.param ) < 0 )
                FAILED_PARSE_CLI_OPTION( "failed to open an output file.\n" );
            output->file.name = argv[i];
        }
        else if( !strcasecmp( argv[i], "--chapter" ) )    /* chapter file */
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--chapter requires an argument.\n" );
            remuxer->chap_file = argv[i];
        }
        else if( !strcasecmp( argv[i], "--chpl-with-bom" ) )
            remuxer->add_bom_to_chpl = 1;
        else if( !strcasecmp( argv[i], "--chapter-track" ) )    /* track to apply reference chapter to */
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--chapter-track requires an argument.\n" );
            remuxer->chap_track = atoi( argv[i] );
            if( !remuxer->chap_track )
                FAILED_PARSE_CLI_OPTION( "%s is an invalid track number.\n", argv[i] );
        }
        else if( !strcasecmp( argv[i], "--language" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--chapter requires an argument.\n" );
            remuxer->default_language = lsmash_pack_iso_language( argv[i] );
        }
        else if( !strcasecmp( argv[i], "--fragment" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--fragment requires an argument.\n" );
            remuxer->frag_base_track = atoi( argv[i] );
            if( remuxer->frag_base_track == 0 )
                FAILED_PARSE_CLI_OPTION( "%s is an invalid track number.\n", argv[i] );
        }
        else if( !strcasecmp( argv[i], "--dash" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--dash requires an argument.\n" );
            remuxer->subseg_per_seg = atoi( argv[i] );
            remuxer->dash           = 1;
        }
        else
            FAILED_PARSE_CLI_OPTION( "unkown option found: %s\n", argv[i] );
    }
    if( !remuxer->output->root )
        FAILED_PARSE_CLI_OPTION( "output file name is not specified.\n" );
    /* Parse track options */
    /* Get the current track and media parameters */
    for( int i = 0; i < remuxer->num_input; i++ )
        for( uint32_t j = 0; j < input[i].file.movie.num_tracks; j++ )
        {
            input_track_t *in_track = &input[i].file.movie.track[j];
            if( !in_track->active )
                continue;
            track_option[i][j].alternate_group = in_track->track_param.alternate_group;
            track_option[i][j].ISO_language    = in_track->media.param.ISO_language;
            track_option[i][j].handler_name    = in_track->media.param.media_handler_name;
        }
    /* Set the default language */
    if( remuxer->default_language )
        for( int i = 0; i < remuxer->num_input; i++ )
            for( uint32_t j = 0; j < input[i].file.movie.num_tracks; j++ )
                track_option[i][j].ISO_language = remuxer->default_language;
    /* Get the track and media parameters specified by users */
    for( int i = 0; i < remuxer->num_input; i++ )
    {
        if( input_file_option[i].num_track_delimiter > input[i].file.movie.num_tracks )
            FAILED_PARSE_CLI_OPTION( "more track options specified than the actual number of the tracks (%"PRIu32").\n",
                                     input[i].file.movie.num_tracks );
        if( input_file_option[i].num_track_delimiter )
        {
            track_option[i][0].raw_track_option = strtok( input_file_option[i].whole_track_option, "?" );
            for( int j = 1; j < input_file_option[i].num_track_delimiter ; j++ )
                track_option[i][j].raw_track_option = strtok( NULL, "?" );
        }
    }
    if( parse_track_option( remuxer ) )
        FAILED_PARSE_CLI_OPTION( "failed to parse track options.\n" );
    lsmash_free( input_file_option );
    return 0;
#undef FAILED_PARSE_CLI_OPTION
}

static void replace_with_valid_brand( remuxer_t *remuxer )
{
    static const lsmash_brand_type brand_filter_list[] =
        {
            ISOM_BRAND_TYPE_3G2A,
            ISOM_BRAND_TYPE_3GG6,
            ISOM_BRAND_TYPE_3GG9,
            ISOM_BRAND_TYPE_3GP4,
            ISOM_BRAND_TYPE_3GP5,
            ISOM_BRAND_TYPE_3GP6,
            ISOM_BRAND_TYPE_3GP7,
            ISOM_BRAND_TYPE_3GP8,
            ISOM_BRAND_TYPE_3GP9,
            ISOM_BRAND_TYPE_3GR6,
            ISOM_BRAND_TYPE_3GR9,
            ISOM_BRAND_TYPE_M4A ,
            ISOM_BRAND_TYPE_M4B ,
            ISOM_BRAND_TYPE_M4V ,
            ISOM_BRAND_TYPE_AVC1,
            ISOM_BRAND_TYPE_DBY1,
            ISOM_BRAND_TYPE_ISO2,
            ISOM_BRAND_TYPE_ISO3,
            ISOM_BRAND_TYPE_ISO4,
            ISOM_BRAND_TYPE_ISO5,
            ISOM_BRAND_TYPE_ISO6,
            ISOM_BRAND_TYPE_ISO7,
            ISOM_BRAND_TYPE_ISOM,
            ISOM_BRAND_TYPE_MP41,
            ISOM_BRAND_TYPE_MP42,
            ISOM_BRAND_TYPE_QT  ,
            0
        };
    input_t *input = remuxer->input;
    /* Check the number of video and audio tracks, and the number of video
     * and audio sample descriptions for the restrictions of 3GPP Basic Profile.
     *   - the maximum number of tracks shall be one for video (or alternatively
     *     one for scene description), one for audio and one for text
     *   - the maximum number of sample entries shall be one per track for video
     *      and audio (but unrestricted for text and scene description) */
    uint32_t video_track_count   = 0;
    uint32_t audio_track_count   = 0;
    uint32_t video_num_summaries = 0;
    uint32_t audio_num_summaries = 0;
    for( int i = 0; i < remuxer->num_input; i++ )
    {
        input_movie_t *movie = &input[i].file.movie;
        for( int j = 0; j < movie->num_tracks; j++ )
        {
            if( movie->track[j].media.param.handler_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK )
            {
                if( ++video_track_count == 1 )
                    video_num_summaries = movie->track[j].num_summaries;
            }
            else if( movie->track[j].media.param.handler_type == ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK )
            {
                if( ++audio_track_count == 1 )
                    audio_num_summaries = movie->track[j].num_summaries;
            }
        }
    }
    for( int i = 0; i < remuxer->num_input; i++ )
        for( uint32_t j = 0; j <= input[i].file.param.brand_count; j++ )
        {
            int       invalid = 1;
            uint32_t *brand   = j ? &input[i].file.param.brands[j - 1] : &input[i].file.param.major_brand;
            uint32_t *version = j ? NULL                               : &input[i].file.param.minor_version;
            for( int k = 0; brand_filter_list[k]; k++ )
            {
                if( *brand == brand_filter_list[k] )
                {
                    if( ((*brand >> 24) & 0xFF) == '3'
                     && ((*brand >> 16) & 0xFF) == 'g'
                     && (((*brand >>  8) & 0xFF) == 'p' || ((*brand >>  8) & 0xFF) == 'r') )
                    {
                        if( remuxer->frag_base_track == 0   /* Movie fragments are not allowed in '3gp4' and '3gp5'. */
                         && video_track_count   <= 1 && audio_track_count   <= 1
                         && video_num_summaries <= 1 && audio_num_summaries <= 1 )
                            continue;
                        /* Replace with the General Profile for maximum compatibility. */
                        if( (*brand & 0xFF) < '6' )
                        {
                            /* 3GPP version 6.7.0 General Profile */
                            *brand = ISOM_BRAND_TYPE_3GG6;
                            if( version )
                                *version = 0x00000700;
                        }
                        else
                            *brand = LSMASH_4CC( '3', 'g', 'g', *brand & 0xFF );
                    }
                    if( remuxer->dash
                     && (*brand == ISOM_BRAND_TYPE_AVC1
                      || (((*brand >> 24) & 0xFF) == 'i'
                       && ((*brand >> 16) & 0xFF) == 's'
                       && ((*brand >>  8) & 0xFF) == 'o'
                       && ((*brand & 0xFF) == 'm' || (*brand & 0xFF) < '6'))) )
                        *brand = ISOM_BRAND_TYPE_ISO6;
                    invalid = 0;
                    break;
                }
            }
            if( invalid )
            {
                /* Replace with the 'mp42' brand. */
                *brand = ISOM_BRAND_TYPE_MP42;
                if( version )
                    *version = 0;
            }
        }
}

static int pick_most_used_major_brand( input_t *input, output_file_t *out_file, int num_input )
{
    void *heap = lsmash_malloc( num_input * (sizeof(lsmash_brand_type) + 2 * sizeof(uint32_t)) );
    if( !heap )
        return ERROR_MSG( "failed to allocate memory.\n" );
    lsmash_brand_type *major_brand       = (lsmash_brand_type *)heap;
    uint32_t          *minor_version     = (uint32_t *)((lsmash_brand_type *)heap + num_input);
    uint32_t          *major_brand_count = minor_version + num_input;
    uint32_t           num_major_brand   = 0;
    for( int i = 0; i < num_input; i++ )
    {
        major_brand      [num_major_brand] = input[i].file.param.major_brand;
        minor_version    [num_major_brand] = input[i].file.param.minor_version;
        major_brand_count[num_major_brand] = 0;
        for( int j = 0; j < num_input; j++ )
            if( (major_brand  [num_major_brand] == input[j].file.param.major_brand)
             && (minor_version[num_major_brand] == input[j].file.param.minor_version) )
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
            out_file->param.major_brand   = major_brand  [i];
            out_file->param.minor_version = minor_version[i];
        }
    lsmash_free( heap );
    return 0;
}

static int set_movie_parameters( remuxer_t *remuxer )
{
    int            num_input = remuxer->num_input;
    input_t       *input     = remuxer->input;
    output_t      *output    = remuxer->output;
    output_file_t *out_file  = &output->file;
    if( remuxer->frag_base_track )
        out_file->param.mode |= LSMASH_FILE_MODE_FRAGMENTED;
    int self_containd_segment = (remuxer->dash && remuxer->subseg_per_seg == 0);
    if( remuxer->dash )
    {
        if( remuxer->frag_base_track )
        {
            if( self_containd_segment )
                out_file->param.mode |= LSMASH_FILE_MODE_INDEX;
            else
            {
                out_file->param.mode &= ~LSMASH_FILE_MODE_MEDIA;
                out_file->param.mode |= LSMASH_FILE_MODE_SEGMENT;
            }
        }
        else
            WARNING_MSG( "--dash requires --fragment.\n" );
    }
    replace_with_valid_brand( remuxer );
    if( self_containd_segment )
    {
        out_file->param.major_brand   = ISOM_BRAND_TYPE_DASH;
        out_file->param.minor_version = 0;
    }
    else if( pick_most_used_major_brand( input, out_file, num_input ) < 0 )
        return ERROR_MSG( "failed to pick the most used major brand.\n" );
    /* Deduplicate compatible brands. */
    uint32_t num_input_brands = num_input + (self_containd_segment ? 1 : 0);
    for( int i = 0; i < num_input; i++ )
        num_input_brands += input[i].file.param.brand_count;
    lsmash_brand_type *input_brands = lsmash_malloc( num_input_brands * sizeof(lsmash_brand_type) );
    if( !input_brands )
        return ERROR_MSG( "failed to allocate brands for an input file.\n" );
    num_input_brands = 0;
    if( self_containd_segment )
        input_brands[num_input_brands++] = ISOM_BRAND_TYPE_DASH;
    for( int i = 0; i < num_input; i++ )
    {
        input_brands[num_input_brands++] = input[i].file.param.major_brand;
        for( uint32_t j = 0; j < input[i].file.param.brand_count; j++ )
            if( input[i].file.param.brands[j] )
                input_brands[num_input_brands++] = input[i].file.param.brands[j];
    }
    lsmash_brand_type *output_brands = lsmash_malloc_zero( num_input_brands * sizeof(lsmash_brand_type) );
    if( !output_brands )
    {
        lsmash_free( input_brands );
        return ERROR_MSG( "failed to allocate brands for an output file.\n" );
    }
    uint32_t num_output_brands = 0;
    for( uint32_t i = 0; i < num_input_brands; i++ )
    {
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
    lsmash_free( input_brands );
    out_file->param.brand_count = num_output_brands;
    out_file->param.brands      = output_brands;
    /* Set up a file. */
    out_file->fh = lsmash_set_file( output->root, &out_file->param );
    if( !out_file->fh )
        return ERROR_MSG( "failed to add an output file into a ROOT.\n" );
    out_file->seg_param = out_file->param;
    /* Check whether a reference chapter track is allowed or not. */
    if( remuxer->chap_file )
        for( uint32_t i = 0; i < out_file->param.brand_count; i++ )
        {
            uint32_t brand = out_file->param.brands[i];
            /* According to the restrictions of 3GPP Basic Profile,
             *   - there shall be no references between tracks, e.g., a scene description track
             *     shall not refer to a media track since all tracks are on equal footing and
             *     played in parallel by a conforming player.
             * Therefore, the referenced chapter track is forbidden to use for 3GPP Basic Profile. */
            if( ((brand >> 24) & 0xFF) == '3'
             && ((brand >> 16) & 0xFF) == 'g'
             && ((brand >>  8) & 0xFF) == 'p' )
                break;
            /* QuickTime file and iTunes MP4 file can contain the referenced chapter track. */
            if( brand == ISOM_BRAND_TYPE_QT  || brand == ISOM_BRAND_TYPE_M4A
             || brand == ISOM_BRAND_TYPE_M4B || brand == ISOM_BRAND_TYPE_M4P
             || brand == ISOM_BRAND_TYPE_M4V )
            {
                remuxer->ref_chap_available = 1;
                break;
            }
        }
    /* Set the movie timescale in order to match the media timescale if only one track is there. */
    lsmash_initialize_movie_parameters( &out_file->movie.param );
    if( out_file->movie.num_tracks == 1 )
        for( int i = 0; i < remuxer->num_input; i++ )
        {
            input_movie_t *in_movie = &input[i].file.movie;
            for( uint32_t j = 0; j < in_movie->num_tracks; j++ )
                if( in_movie->track[j].active )
                {
                    out_file->movie.param.timescale = in_movie->track[j].media.param.timescale;
                    break;
                }
        }
    return lsmash_set_movie_parameters( output->root, &out_file->movie.param );
}

static void set_itunes_metadata( output_t *output, input_t *input, int num_input )
{
    for( int i = 0; i < num_input; i++ )
        for( uint32_t j = 0; j < input[i].file.movie.num_itunes_metadata; j++ )
            if( lsmash_set_itunes_metadata( output->root, input[i].file.movie.itunes_metadata[j] ) )
            {
                WARNING_MSG( "failed to set an iTunes metadata.\n" );
                continue;
            }
}

static int set_starting_point( input_t *input, input_track_t *in_track, uint32_t seek_point, int consider_rap )
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
        WARNING_MSG( "starting point you specified is not a random accessible point.\n" );
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

static void exclude_invalid_output_track( output_t *output, output_track_t *out_track,
                                          input_movie_t *in_movie, input_track_t *in_track,
                                          const char *message, ... )
{
    REFRESH_CONSOLE;
    eprintf( "[Warning] in %"PRIu32"/%"PRIu32" -> out %"PRIu32": ", in_movie->movie_ID, in_track->track_ID, out_track->track_ID );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    lsmash_delete_track( output->root, out_track->track_ID );
    -- output->file.movie.num_tracks;
    in_track->active = 0;
}

static int prepare_output( remuxer_t *remuxer )
{
    input_t        *input     = remuxer->input;
    output_t       *output    = remuxer->output;
    output_movie_t *out_movie = &output->file.movie;
    /* Count the number of output tracks. */
    for( int i = 0; i < remuxer->num_input; i++ )
        out_movie->num_tracks += input[i].file.movie.num_tracks;
    for( int i = 0; i < remuxer->num_input; i++ )
    {
        input_movie_t *in_movie = &input[i].file.movie;
        for( uint32_t j = 0; j < in_movie->num_tracks; j++ )
        {
            /* Don't remux tracks specified as 'remove' by a user. */
            if( remuxer->track_option[i][j].remove )
                in_movie->track[j].active = 0;
            if( !in_movie->track[j].active )
                -- out_movie->num_tracks;
        }
    }
    if( set_movie_parameters( remuxer ) < 0 )
        return ERROR_MSG( "failed to set output movie parameters.\n" );
    set_itunes_metadata( output, input, remuxer->num_input );
    /* Allocate output tracks. */
    out_movie->track = lsmash_malloc( out_movie->num_tracks * sizeof(output_track_t) );
    if( !out_movie->track )
        return ERROR_MSG( "failed to alloc output tracks.\n" );
    out_movie->current_track_number = 1;
    for( int i = 0; i < remuxer->num_input; i++ )
    {
        input_movie_t *in_movie = &input[i].file.movie;
        for( uint32_t j = 0; j < in_movie->num_tracks; j++ )
        {
            track_media_option *current_track_opt = &remuxer->track_option[i][j];
            input_track_t *in_track = &in_movie->track[j];
            if( !in_track->active )
                continue;
            output_track_t *out_track = &out_movie->track[ out_movie->current_track_number - 1 ];
            out_track->summary_remap = lsmash_malloc_zero( in_track->num_summaries * sizeof(uint32_t) );
            if( !out_track->summary_remap )
                return ERROR_MSG( "failed to create summary mapping for a track.\n" );
            out_track->track_ID = lsmash_create_track( output->root, in_track->media.param.handler_type );
            if( !out_track->track_ID )
                return ERROR_MSG( "failed to create a track.\n" );
            /* Copy track and media parameters except for track_ID. */
            out_track->track_param = in_track->track_param;
            out_track->media_param = in_track->media.param;
            /* Set track and media parameters specified by users */
            out_track->track_param.alternate_group    = current_track_opt->alternate_group;
            out_track->media_param.ISO_language       = current_track_opt->ISO_language;
            out_track->media_param.media_handler_name = current_track_opt->handler_name;
            out_track->track_param.track_ID           = out_track->track_ID;
            if( current_track_opt->disable )
                out_track->track_param.mode &= ~ISOM_TRACK_ENABLED;
            if( lsmash_set_track_parameters( output->root, out_track->track_ID, &out_track->track_param ) < 0 )
            {
                exclude_invalid_output_track( output, out_track, in_movie, in_track, "failed to set track parameters.\n" );
                continue;
            }
            if( lsmash_set_media_parameters( output->root, out_track->track_ID, &out_track->media_param ) < 0 )
            {
                exclude_invalid_output_track( output, out_track, in_movie, in_track, "failed to set media parameters.\n" );
                continue;
            }
            lsmash_data_reference_t data_ref = { .index = 1, .location = NULL };
            if( lsmash_create_data_reference( output->root, out_track->track_ID, &data_ref, output->file.fh ) < 0 )
                return ERROR_MSG( "failed to create a data reference for output movie.\n" );
            uint32_t valid_summary_count = 0;
            for( uint32_t k = 0; k < in_track->num_summaries; k++ )
            {
                if( !in_track->summaries[k].active )
                {
                    out_track->summary_remap[k] = 0;
                    continue;
                }
                lsmash_summary_t *summary = in_track->summaries[k].summary;
                summary->data_ref_index = 1;
                if( lsmash_add_sample_entry( output->root, out_track->track_ID, summary ) == 0 )
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
            {
                exclude_invalid_output_track( output, out_track, in_movie, in_track, "failed to append all summaries.\n" );
                continue;
            }
            out_track->last_sample_delta = in_track->last_sample_delta;
            if( set_starting_point( input, in_track, current_track_opt->seek, current_track_opt->consider_rap ) < 0 )
            {
                exclude_invalid_output_track( output, out_track, in_movie, in_track, "failed to set starting point.\n" );
                continue;
            }
            out_track->current_sample_number = 1;
            out_track->skip_dt_interval      = 0;
            out_track->last_sample_dts       = 0;
            ++ out_movie->current_track_number;
        }
    }
    if( out_movie->num_tracks == 0 )
        return ERROR_MSG( "failed to create the output movie.\n" );
    out_movie->current_track_number = 1;
    output->current_seg_number = 1;
    return 0;
}

static void set_reference_chapter_track( remuxer_t *remuxer )
{
    if( remuxer->ref_chap_available )
        lsmash_create_reference_chapter_track( remuxer->output->root, remuxer->chap_track, remuxer->chap_file );
}

static int flush_movie_fragment( remuxer_t *remuxer )
{
    input_t        *inputs    = remuxer->input;
    output_t       *output    = remuxer->output;
    output_movie_t *out_movie = &output->file.movie;
    uint32_t out_current_track_number = 1;
    for( uint32_t i = 1; i <= remuxer->num_input; i++ )
    {
        input_t       *in       = &inputs[i - 1];
        input_movie_t *in_movie = &in->file.movie;
        for( uint32_t j = 1; j <= in_movie->num_tracks; j++ )
        {
            input_track_t *in_track = &in_movie->track[j - 1];
            if( !in_track->active )
                continue;
            output_track_t *out_track = &out_movie->track[out_current_track_number - 1];
            if( !in_track->reach_end_of_media_timeline )
            {
                lsmash_sample_t sample;
                if( lsmash_get_sample_info_from_media_timeline( in->root, in_track->track_ID, in_track->current_sample_number, &sample ) < 0 )
                    return ERROR_MSG( "failed to get the information of the next sample.\n" );
                uint64_t sample_dts = sample.dts - out_track->skip_dt_interval;
                if( lsmash_flush_pooled_samples( output->root, out_track->track_ID, sample_dts - out_track->last_sample_dts ) < 0 )
                    return ERROR_MSG( "failed to flush the rest of samples in a fragment.\n" );
            }
            else
                if( lsmash_flush_pooled_samples( output->root, out_track->track_ID, out_track->last_sample_delta ) < 0 )
                    return ERROR_MSG( "failed to flush the rest of samples in a fragment.\n" );
            if( ++out_current_track_number > out_movie->num_tracks )
                break;
        }
    }
    return 0;
}

static int moov_to_front_callback( void *param, uint64_t written_movie_size, uint64_t total_movie_size )
{
    REFRESH_CONSOLE;
    eprintf( "Finalizing: [%5.2lf%%]\r", ((double)written_movie_size / total_movie_size) * 100.0 );
    return 0;
}

static lsmash_adhoc_remux_t moov_to_front =
{
    .func        = moov_to_front_callback,
    .buffer_size = 4 * 1024 * 1024, /* 4MiB */
    .param       = NULL
};

static int open_media_segment( output_t *output, lsmash_file_parameters_t *seg_param )
{
    /* Open a media segment file.
     * Each file is named as follows.
     *   a.mp4
     *   a_1.mp4
     *   a_2.mp4
     *    ...
     *   a_N.mp4
     * N is the number of segment files excluding the initialization segment file.
     */
    output_file_t *out_file = &output->file;
    int out_file_name_length = strlen( out_file->name );
    const char *end = &out_file->name[ out_file_name_length ];
    const char *p   = end;
    while( p >= out_file->name && *p != '.' && *p != '/' && *p != '\\' )
        --p;
    if( p < out_file->name )
        ++p;
    if( *p != '.' )
        p = end;
    int suffix_length = 1;
    for( uint32_t i = output->current_seg_number; i; i /= 10 )
        ++suffix_length;
    int seg_name_length   = out_file_name_length + suffix_length;
    int suffixless_length = p - out_file->name;
    char *seg_name = lsmash_malloc( (seg_name_length + 1) * sizeof(char) );
    if( !seg_name )
        return ERROR_MSG( "failed to allocate to store a segment filename.\n" );
    seg_name[ seg_name_length ] = '\0';
    memcpy( seg_name, out_file->name, suffixless_length );
    sprintf( seg_name + suffixless_length, "_%"PRIu32, output->current_seg_number );
    if( *p == '.' )
        memcpy( seg_name + suffixless_length + suffix_length, p, end - p );
    int ret = lsmash_open_file( seg_name, 0, seg_param );
    if( ret == 0 )
        eprintf( "[Segment] out: %s\n", seg_name );
    lsmash_free( seg_name );
    return ret;
}

static int switch_segment( remuxer_t *remuxer )
{
    output_t      *output   = remuxer->output;
    output_file_t *out_file = &output->file;
    lsmash_file_parameters_t seg_param = { 0 };
    if( open_media_segment( output, &seg_param ) < 0 )
        return ERROR_MSG( "failed to open an output file for segmentation.\n" );
    /* Set up the media segment file.
     * Copy the parameters of the previous segment if the previous is not the initialization segment. */
    if( out_file->seg_param.mode & LSMASH_FILE_MODE_INITIALIZATION )
    {
        uint32_t           brand_count = out_file->param.brand_count + 2;
        lsmash_brand_type *brands      = lsmash_malloc_zero( brand_count * sizeof(lsmash_brand_type) );
        if( !brands )
            return ERROR_MSG( "failed to allocate brands for an output segment file.\n" );
        brands[0] = ISOM_BRAND_TYPE_MSDH;
        brands[1] = ISOM_BRAND_TYPE_MSIX;
        for( uint32_t i = 0; i < out_file->param.brand_count; i++ )
            brands[i + 2] = out_file->param.brands[i];
        seg_param.major_brand = ISOM_BRAND_TYPE_MSDH;
        seg_param.brand_count = brand_count;
        seg_param.brands      = brands;
        seg_param.mode        = LSMASH_FILE_MODE_WRITE | LSMASH_FILE_MODE_FRAGMENTED
                              | LSMASH_FILE_MODE_BOX   | LSMASH_FILE_MODE_MEDIA
                              | LSMASH_FILE_MODE_INDEX | LSMASH_FILE_MODE_SEGMENT;
    }
    else
    {
        void *opaque = seg_param.opaque;
        seg_param = out_file->seg_param;
        seg_param.opaque = opaque;
    }
    lsmash_file_t *segment = lsmash_set_file( output->root, &seg_param );
    if( !segment )
        return ERROR_MSG( "failed to add an output segment file into a ROOT.\n" );
    /* Switch to the next segment.
     * After switching, close the previous segment if the previous is not the initialization segment. */
    if( lsmash_switch_media_segment( output->root, segment, &moov_to_front ) < 0 )
        return ERROR_MSG( "failed to switch to the next segment.\n" );
    if( !(out_file->seg_param.mode & LSMASH_FILE_MODE_INITIALIZATION) )
        return lsmash_close_file( &out_file->seg_param );
    out_file->seg_param = seg_param;
    return 0;
}

static int handle_segmentation( remuxer_t *remuxer )
{
    if( remuxer->subseg_per_seg == 0 )
        return 0;
    output_t *output = remuxer->output;
    if( remuxer->subseg_per_seg == output->file.current_subseg_number
     || output->current_seg_number == 1 )
    {
        if( switch_segment( remuxer ) < 0 )
        {
            ERROR_MSG( "failed to switch to a segment.\n" );
            return -1;
        }
        output->file.current_subseg_number = 1;
        ++ output->current_seg_number;
    }
    else
        ++ output->file.current_subseg_number;
    return 0;
}

static void adapt_description_index( output_track_t *out_track, input_track_t *in_track, lsmash_sample_t *sample )
{
    sample->index = sample->index > in_track->num_summaries ? in_track->num_summaries
                  : sample->index == 0 ? 1
                  : sample->index;
    sample->index = out_track->summary_remap[ sample->index - 1 ];
    if( in_track->current_sample_index == 0 )
        in_track->current_sample_index = sample->index;
}

static void adjust_timestamp( output_track_t *out_track, lsmash_sample_t *sample )
{
    /* The first DTS must be 0. */
    if( out_track->current_sample_number == 1 )
        out_track->skip_dt_interval = sample->dts;
    if( out_track->skip_dt_interval )
    {
        sample->dts -= out_track->skip_dt_interval;
        sample->cts -= out_track->skip_dt_interval;
    }
}

static int do_remux( remuxer_t *remuxer )
{
#define LSMASH_MAX( a, b ) ((a) > (b) ? (a) : (b))
    input_t        *inputs    = remuxer->input;
    output_t       *output    = remuxer->output;
    output_movie_t *out_movie = &output->file.movie;
    set_reference_chapter_track( remuxer );
    double   largest_dts                 = 0;   /* in seconds */
    double   frag_base_dts               = 0;   /* in seconds */
    uint32_t input_movie_number          = 1;
    uint32_t num_consecutive_sample_skip = 0;
    uint32_t num_active_input_tracks     = out_movie->num_tracks;
    uint64_t total_media_size            = 0;
    uint8_t  sample_count                = 0;
    uint8_t  pending_flush_fragments     = (remuxer->frag_base_track != 0); /* For non-fragmented movie, always set to 0. */
    while( 1 )
    {
        input_t       *in       = &inputs[input_movie_number - 1];
        input_movie_t *in_movie = &in->file.movie;
        input_track_t *in_track = &in_movie->track[ in_movie->current_track_number - 1 ];
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
                sample = lsmash_get_sample_from_media_timeline( in->root, in_track->track_ID, in_track->current_sample_number );
                if( sample )
                {
                    output_track_t *out_track = &out_movie->track[ out_movie->current_track_number - 1 ];
                    adapt_description_index( out_track, in_track, sample );
                    adjust_timestamp( out_track, sample );
                    in_track->sample = sample;
                    in_track->dts    = (double)sample->dts / in_track->media.param.timescale;
                }
                else
                {
                    if( lsmash_check_sample_existence_in_media_timeline( in->root, in_track->track_ID, in_track->current_sample_number ) )
                    {
                        ERROR_MSG( "failed to get a sample.\n" );
                        break;
                    }
                    lsmash_sample_t sample_info = { 0 };
                    if( lsmash_get_sample_info_from_media_timeline( in->root, in_track->track_ID, in_track->current_sample_number, &sample_info ) < 0 )
                    {
                        /* No more appendable samples in this track. */
                        in_track->sample = NULL;
                        in_track->reach_end_of_media_timeline = 1;
                        if( --num_active_input_tracks == 0 )
                            break;      /* end of muxing */
                    }
                    else
                    {
                        ERROR_MSG( "failed to get a sample.\n" );
                        break;
                    }
                }
            }
            if( sample )
            {
                /* Flushing the active movie fragment is pending until random accessible point sample within all active tracks are ready. */
                if( remuxer->frag_base_track )
                {
                    if( pending_flush_fragments == 0 )
                    {
                        if( remuxer->frag_base_track == out_movie->current_track_number
                         && sample->prop.ra_flags != ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE )
                        {
                            pending_flush_fragments = 1;
                            frag_base_dts = in_track->dts;
                        }
                    }
                    else if( num_consecutive_sample_skip == num_active_input_tracks || total_media_size == 0 )
                    {
                        if( flush_movie_fragment( remuxer ) < 0 )
                        {
                            ERROR_MSG( "failed to flush a movie fragment.\n" );
                            break;
                        }
                        if( handle_segmentation( remuxer ) < 0 )
                            break;
                        if( lsmash_create_fragment_movie( output->root ) < 0 )
                        {
                            ERROR_MSG( "failed to create a movie fragment.\n" );
                            break;
                        }
                        pending_flush_fragments = 0;
                    }
                }
                /* Append a sample if meeting a condition. */
                int append = 0;
                int need_new_fragment = (remuxer->frag_base_track && sample->index != in_track->current_sample_index);
                if( pending_flush_fragments == 0 )
                    append = (in_track->dts <= largest_dts || num_consecutive_sample_skip == num_active_input_tracks) && !need_new_fragment;
                else if( remuxer->frag_base_track != out_movie->current_track_number && !need_new_fragment )
                {
                    /* Wait as much as possible both to make the last sample within each track fragment close to the DTS of
                     * the first sample within the track fragment corresponding to the base track within the next movie
                     * fragment and to make all the track fragments within the next movie fragment start with RAP. */
                    if( sample->prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE )
                        append = 1;
                    else
                    {
                        /* Check the DTS and random accessibilities of the next sample. */
                        lsmash_sample_t info;
                        if( lsmash_get_sample_info_from_media_timeline( in->root, in_track->track_ID, in_track->current_sample_number + 1, &info ) < 0 )
                            append = 0;
                        else
                            append = (info.prop.ra_flags != ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE)
                                  && ((double)info.dts / in_track->media.param.timescale <= frag_base_dts);
                    }
                }
                if( append )
                {
                    if( sample->index )
                    {
                        output_track_t *out_track = &out_movie->track[ out_movie->current_track_number - 1 ];
                        uint64_t sample_size     = sample->length;      /* sample might be deleted internally after appending. */
                        uint64_t last_sample_dts = sample->dts;         /* same as above */
                        uint32_t sample_index    = sample->index;       /* same as above */
                        /* Append a sample into output movie. */
                        if( lsmash_append_sample( output->root, out_track->track_ID, sample ) < 0 )
                        {
                            lsmash_delete_sample( sample );
                            return ERROR_MSG( "failed to append a sample.\n" );
                        }
                        largest_dts                       = LSMASH_MAX( largest_dts, in_track->dts );
                        in_track->sample                  = NULL;
                        in_track->current_sample_number  += 1;
                        in_track->current_sample_index    = sample_index;
                        out_track->current_sample_number += 1;
                        out_track->last_sample_dts        = last_sample_dts;
                        num_consecutive_sample_skip       = 0;
                        total_media_size                 += sample_size;
                        /* Print, per 256 samples, total size of imported media. */
                        if( ++sample_count == 0 )
                            eprintf( "Importing: %"PRIu64" bytes\r", total_media_size );
                    }
                    else
                    {
                        lsmash_delete_sample( sample );
                        in_track->sample = NULL;
                        in_track->current_sample_number += 1;
                    }
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
        if( lsmash_flush_pooled_samples( output->root, out_movie->track[i].track_ID, out_movie->track[i].last_sample_delta ) )
            return ERROR_MSG( "failed to flush samples.\n" );
    return 0;
#undef LSMASH_MAX
}

static int construct_timeline_maps( remuxer_t *remuxer )
{
    input_t             *input        = remuxer->input;
    output_t            *output       = remuxer->output;
    output_movie_t      *out_movie    = &output->file.movie;
    track_media_option **track_option = remuxer->track_option;
    out_movie->current_track_number = 1;
    for( int i = 0; i < remuxer->num_input; i++ )
        for( uint32_t j = 0; j < input[i].file.movie.num_tracks; j++ )
        {
            input_track_t *in_track  = &input[i].file.movie.track[j];
            if( !in_track->active )
                continue;
            output_track_t *out_track = &out_movie->track[ out_movie->current_track_number ++ - 1 ];
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
                if( remuxer->frag_base_track == 0 )
                    edit.duration = (out_track->last_sample_dts + out_track->last_sample_delta - in_track->skip_duration) * timescale_convert_multiplier;
                else
                    edit.duration = ISOM_EDIT_DURATION_IMPLICIT;
                edit.rate = ISOM_EDIT_MODE_NORMAL;
                if( lsmash_create_explicit_timeline_map( output->root, out_track->track_ID, edit ) )
                    return ERROR_MSG( "failed to create a explicit timeline map.\n" );
            }
            else if( lsmash_copy_timeline_map( output->root, out_track->track_ID, input[i].root, in_track->track_ID ) )
                return ERROR_MSG( "failed to copy timeline maps.\n" );
        }
    out_movie->current_track_number = 1;
    return 0;
}

static int finish_movie( remuxer_t *remuxer )
{
    output_t *output = remuxer->output;
    /* Set chapter list */
    if( remuxer->chap_file )
        lsmash_set_tyrant_chapter( output->root, remuxer->chap_file, remuxer->add_bom_to_chpl );
    /* Finish muxing. */
    REFRESH_CONSOLE;
    if( lsmash_finish_movie( output->root, &moov_to_front ) )
        return -1;
    return remuxer->frag_base_track ? 0 : lsmash_write_lsmash_indicator( output->root );
}

int main( int argc, char *argv[] )
{
    if ( argc < 2 )
    {
        display_help();
        return -1;
    }
    else if( !strcasecmp( argv[1], "-h" ) || !strcasecmp( argv[1], "--help" ) )
    {
        display_help();
        return 0;
    }
    else if( !strcasecmp( argv[1], "-v" ) || !strcasecmp( argv[1], "--version" ) )
    {
        display_version();
        return 0;
    }
    else if( argc < 5 )
    {
        display_help();
        return -1;
    }

    lsmash_get_mainargs( &argc, &argv );
    int num_input = 0;
    for( int i = 1 ; i < argc ; i++ )
        if( !strcasecmp( argv[i], "-i" ) || !strcasecmp( argv[i], "--input" ) )
            num_input++;
    if( !num_input )
        return ERROR_MSG( "no input file specified.\n" );
    output_t output = { 0 };
    input_t *input = lsmash_malloc_zero( num_input * sizeof(input_t) );
    if( !input )
        return ERROR_MSG( "failed to allocate the input handler.\n" );
    track_media_option **track_option = lsmash_malloc_zero( num_input * sizeof(track_media_option *) );
    if( !track_option )
    {
        lsmash_free( input );
        return ERROR_MSG( "failed to allocate the track option handler.\n" );
    }
    remuxer_t           remuxer =
    {
        .output             = &output,
        .input              = input,
        .track_option       = track_option,
        .num_input          = num_input,
        .add_bom_to_chpl    = 0,
        .ref_chap_available = 0,
        .chap_track         = 1,
        .chap_file          = NULL,
        .default_language   = 0,
        .frag_base_track    = 0,
        .subseg_per_seg     = 0,
        .dash               = 0
    };
    if( parse_cli_option( argc, argv, &remuxer ) )
        return REMUXER_ERR( "failed to parse command line options.\n" );
    if( prepare_output( &remuxer ) )
        return REMUXER_ERR( "failed to set up preparation for output.\n" );
    if( remuxer.frag_base_track && construct_timeline_maps( &remuxer ) )
        return REMUXER_ERR( "failed to construct timeline maps.\n" );
    if( do_remux( &remuxer ) )
        return REMUXER_ERR( "failed to remux movies.\n" );
    if( remuxer.frag_base_track == 0 && construct_timeline_maps( &remuxer ) )
        return REMUXER_ERR( "failed to construct timeline maps.\n" );
    if( finish_movie( &remuxer ) )
        return REMUXER_ERR( "failed to finish output movie.\n" );
    REFRESH_CONSOLE;
    eprintf( "%s completed!\n", !remuxer.dash || remuxer.subseg_per_seg == 0 ? "Remuxing" : "Segmentation" );
    cleanup_remuxer( &remuxer );
    return 0;
}
