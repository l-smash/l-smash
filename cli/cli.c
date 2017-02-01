/*****************************************************************************
 * cli.c
 *****************************************************************************
 * Copyright (C) 2013-2017 L-SMASH project
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

#include "common/internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <io.h>     /* for _setmode() */
#include <fcntl.h>  /* for O_BINARY */
#endif

#ifdef _WIN32
void lsmash_get_mainargs( int *argc, char ***argv )
{
    wchar_t **wargv = CommandLineToArgvW( GetCommandLineW(), argc );
    *argv = lsmash_malloc_zero( (*argc + 1) * sizeof(char *) );
    for( int i = 0; i < *argc; ++i )
        lsmash_string_from_wchar( CP_UTF8, wargv[i], &(*argv)[i] );
}
#endif

int lsmash_write_lsmash_indicator( lsmash_root_t *root )
{
    /* Write a tag in a free space to indicate the output file is written by L-SMASH. */
    char *string = "Multiplexed by L-SMASH";
    int   length = strlen( string );
    lsmash_box_type_t type = lsmash_form_iso_box_type( LSMASH_4CC( 'f', 'r', 'e', 'e' ) );
    lsmash_box_t *free_box = lsmash_create_box( type, (uint8_t *)string, length, LSMASH_BOX_PRECEDENCE_N );
    if( !free_box )
        return 0;
    if( lsmash_add_box_ex( lsmash_root_as_box( root ), &free_box ) < 0 )
    {
        lsmash_destroy_box( free_box );
        return 0;
    }
    return lsmash_write_top_level_box( free_box );
}

/*** Dry Run tools ***/

typedef struct
{
    uint64_t pos;
    uint64_t size;
} dry_run_stream_t;

static dry_run_stream_t dry_stream = { .pos = 0, .size = 0 };

static int dry_read( void *opaque, uint8_t *buf, int size )
{
    dry_run_stream_t *stream = (dry_run_stream_t *)opaque;
    int read_size;
    if( stream->pos + size > stream->size )
        read_size = stream->size - stream->pos;
    else
        read_size = size;
    stream->pos += read_size;
    return read_size;
}

static int dry_write( void *opaque, uint8_t *buf, int size )
{
    dry_run_stream_t *stream = (dry_run_stream_t *)opaque;
    stream->pos += size;
    if( stream->size < stream->pos )
        stream->size = stream->pos;
    return size;
}

static int64_t dry_seek( void *opaque, int64_t offset, int whence )
{
    dry_run_stream_t *stream = (dry_run_stream_t *)opaque;
    if( whence == SEEK_SET )
        stream->pos = offset;
    else if( whence == SEEK_CUR )
        stream->pos += offset;
    else if( whence == SEEK_END )
        stream->pos = stream->size + offset;
    return stream->pos;
}

int dry_open_file
(
    const char               *filename,
    int                       open_mode,
    lsmash_file_parameters_t *param
)
{
    if( !filename || !param )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_mode file_mode = 0;
    if( open_mode == 0 )
        file_mode = LSMASH_FILE_MODE_WRITE
                  | LSMASH_FILE_MODE_BOX
                  | LSMASH_FILE_MODE_INITIALIZATION
                  | LSMASH_FILE_MODE_MEDIA;
    else if( open_mode == 1 )
        file_mode = LSMASH_FILE_MODE_READ;
    if( file_mode == 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
#ifdef _WIN32
    _setmode( _fileno( stdin ),  _O_BINARY );
    _setmode( _fileno( stdout ), _O_BINARY );
    _setmode( _fileno( stderr ), _O_BINARY );
#endif
    int seekable = 1;
    if( !strcmp( filename, "-" ) )
    {
        if( file_mode & LSMASH_FILE_MODE_READ )
            seekable = 0;
        else if( file_mode & LSMASH_FILE_MODE_WRITE )
        {
            seekable   = 0;
            file_mode |= LSMASH_FILE_MODE_FRAGMENTED;
        }
    }
    memset( param, 0, sizeof(lsmash_file_parameters_t) );
    param->mode                = file_mode;
    param->opaque              = (void *)&dry_stream;
    param->read                = dry_read;
    param->write               = dry_write;
    param->seek                = seekable ? dry_seek : NULL;
    param->major_brand         = 0;
    param->brands              = NULL;
    param->brand_count         = 0;
    param->minor_version       = 0;
    param->max_chunk_duration  = 0.5;
    param->max_async_tolerance = 2.0;
    param->max_chunk_size      = 4 * 1024 * 1024;
    param->max_read_size       = 4 * 1024 * 1024;
    return 0;
}

int dry_close_file
(
    lsmash_file_parameters_t *param
)
{
    if( !param )
        return LSMASH_ERR_NAMELESS;
    param->opaque = NULL;
    return 0;
}
