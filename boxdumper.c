/*****************************************************************************
 * boxdumper.c:
 *****************************************************************************
 * Copyright (C) 2010 L-SMASH project
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

#include "isom.h"

int main( int argc, char *argv[] )
{
    if( argc != 2 || !strcasecmp( argv[1], "-h" ) || !strcasecmp( argv[1], "--help" ) )
    {
        fprintf( stderr, "Usage: boxdumper input\n" );
        return -1;
    }
    char *filename = argv[1];
    isom_root_t *root = isom_open_movie( filename, ISOM_FILE_MODE_READ | ISOM_FILE_MODE_DUMP );
    if( !root )
    {
        fprintf( stderr, "Failed to open input file.\n" );
        return -1;
    }
    int ret = isom_print_movie( root );
    if( ret )
        fprintf( stderr, "Failed to dump input file.\n" );;
    isom_destroy_root( root );
    return ret;
}
