/*****************************************************************************
 * cli.c:
 *****************************************************************************
 * Copyright (C) 2013-2015 L-SMASH project
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
