/*****************************************************************************
 * osdep.c
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

#include "internal.h" /* must be placed first */

#include <stdlib.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32

int lsmash_string_to_wchar( int cp, const char *from, wchar_t **to )
{
    if( !to )
        return 0;
    *to = NULL;
    if( !from )
        return 0;
    int nc = MultiByteToWideChar( cp, 0, from, -1, NULL, 0 );
    if( nc <= 0 || (size_t)nc > SIZE_MAX / sizeof(wchar_t) )
        return 0;
    wchar_t *buf = lsmash_malloc( (size_t)nc * sizeof(wchar_t) );
    if( !buf )
        return 0;
    int ret = MultiByteToWideChar( cp, 0, from, -1, buf, nc );
    if( ret <= 0 )
    {
        lsmash_free( buf );
        return 0;
    }
    *to = buf;
    return ret;
}

int lsmash_string_from_wchar( int cp, const wchar_t *from, char **to )
{
    if( !to )
        return 0;
    *to = NULL;
    if( !from )
        return 0;
    int nc = WideCharToMultiByte( cp, 0, from, -1, NULL, 0, NULL, NULL );
    if( nc <= 0 )
        return 0;
    char *buf = lsmash_malloc( (size_t)nc * sizeof(char) );
    if( !buf )
        return 0;
    int ret = WideCharToMultiByte( cp, 0, from, -1, buf, nc, NULL, NULL );
    if( ret <= 0 )
    {
        lsmash_free( buf );
        return 0;
    }
    *to = buf;
    return ret;
}

FILE *lsmash_win32_fopen( const char *name, const char *mode )
{
    if( !name || !mode )
        return NULL;
    wchar_t *wname = NULL, *wmode = NULL;
    FILE *fp = NULL;
    if( lsmash_string_to_wchar( CP_UTF8, name, &wname ) > 0
     && lsmash_string_to_wchar( CP_UTF8, mode, &wmode ) > 0 )
        fp = _wfopen( wname, wmode );
    if( !fp )
        fp = fopen( name, mode );
    lsmash_free( wname );
    lsmash_free( wmode );
    return fp;
}

#endif

