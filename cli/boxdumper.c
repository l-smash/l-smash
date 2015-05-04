/*****************************************************************************
 * boxdumper.c:
 *****************************************************************************
 * Copyright (C) 2010-2015 L-SMASH project
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#define eprintf( ... ) fprintf( stderr, __VA_ARGS__ )

static void display_version( void )
{
    eprintf( "\n"
             "L-SMASH isom/mov structual analyzer rev%s  %s\n"
             "Built on %s %s\n"
             "Copyright (C) 2010-2015 L-SMASH project\n",
             LSMASH_REV, LSMASH_GIT_HASH, __DATE__, __TIME__ );
}

static void display_help( void )
{
    display_version();
    eprintf( "\n"
             "Usage: boxdumper [option] input\n"
             "  options:\n"
             "    --help         Display help\n"
             "    --version      Display version information\n"
             "    --box          Dump box structure\n"
             "    --chapter      Extract chapter list\n"
             "    --timestamp    Dump media timestamps\n" );
}

static int boxdumper_error
(
    lsmash_root_t            *root,
    lsmash_file_parameters_t *file_param,
    const char               *message
)
{
    lsmash_close_file( file_param );
    lsmash_destroy_root( root );
    eprintf( "%s", message );
    return -1;
}

#define BOXDUMPER_ERR( message ) boxdumper_error( root, &file_param, message )
#define DO_NOTHING

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
    int dump_box = 1;
    int chapter = 0;
    char *filename;
    lsmash_get_mainargs( &argc, &argv );
    if( argc > 2 )
    {
        if( !strcasecmp( argv[1], "--box" ) )
            DO_NOTHING;
        else if( !strcasecmp( argv[1], "--chapter" ) )
            chapter = 1;
        else if( !strcasecmp( argv[1], "--timestamp" ) )
            dump_box = 0;
        else
        {
            display_help();
            return -1;
        }
        filename = argv[2];
    }
    else
    {
        filename = argv[1];
    }
    /* Open the input file. */
    lsmash_root_t *root = lsmash_create_root();
    if( !root )
    {
        fprintf( stderr, "Failed to create a ROOT.\n" );
        return -1;
    }
    lsmash_file_parameters_t file_param = { 0 };
    if( lsmash_open_file( filename, 1, &file_param ) < 0 )
        return BOXDUMPER_ERR( "Failed to open an input file.\n" );
    if( dump_box )
        file_param.mode |= LSMASH_FILE_MODE_DUMP;
    lsmash_file_t *file = lsmash_set_file( root, &file_param );
    if( !file )
        return BOXDUMPER_ERR( "Failed to add a file into a ROOT.\n" );
    if( lsmash_read_file( file, &file_param ) < 0 )
        return BOXDUMPER_ERR( "Failed to read a file\n" );
    /* Dump the input file. */
    if( chapter )
    {
        if( lsmash_print_chapter_list( root ) )
            return BOXDUMPER_ERR( "Failed to extract chapter.\n" );
    }
    else if( dump_box )
    {
        if( lsmash_print_movie( root, "-" ) )
            return BOXDUMPER_ERR( "Failed to dump box structure.\n" );
    }
    else
    {
        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        lsmash_get_movie_parameters( root, &movie_param );
        uint32_t num_tracks = movie_param.number_of_tracks;
        for( uint32_t track_number = 1; track_number <= num_tracks; track_number++ )
        {
            uint32_t track_ID = lsmash_get_track_ID( root, track_number );
            if( !track_ID )
                return BOXDUMPER_ERR( "Failed to get track_ID.\n" );
            lsmash_media_parameters_t media_param;
            lsmash_initialize_media_parameters( &media_param );
            if( lsmash_get_media_parameters( root, track_ID, &media_param ) )
                return BOXDUMPER_ERR( "Failed to get media parameters.\n" );
            if( lsmash_construct_timeline( root, track_ID ) )
                return BOXDUMPER_ERR( "Failed to construct timeline.\n" );
            uint32_t timeline_shift;
            if( lsmash_get_composition_to_decode_shift_from_media_timeline( root, track_ID, &timeline_shift ) )
                return BOXDUMPER_ERR( "Failed to get timestamps.\n" );
            lsmash_media_ts_list_t ts_list;
            if( lsmash_get_media_timestamps( root, track_ID, &ts_list ) )
                return BOXDUMPER_ERR( "Failed to get timestamps.\n" );
            fprintf( stdout, "track_ID: %"PRIu32"\n", track_ID );
            fprintf( stdout, "Media timescale: %"PRIu32"\n", media_param.timescale );
            lsmash_media_ts_t *ts_array = ts_list.timestamp;
            if( !ts_array )
            {
                fprintf( stdout, "\n" );
                continue;
            }
            for( uint32_t i = 0; i < ts_list.sample_count; i++ )
                fprintf( stdout, "DTS = %"PRIu64", CTS = %"PRIu64"\n", ts_array[i].dts, ts_array[i].cts + timeline_shift );
            lsmash_free( ts_array );
            fprintf( stdout, "\n" );
        }
    }
    lsmash_destroy_root( root );
    return 0;
}
