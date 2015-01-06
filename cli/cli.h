/*****************************************************************************
 * cli.h:
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

#ifndef CLI_H
#define CLI_H

#include "config.h"
#include "common/osdep.h"
#include "lsmash.h"

#ifdef _MSC_VER
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#ifdef _WIN32
   void lsmash_get_mainargs( int *argc, char ***argv );
#else
#  define lsmash_get_mainargs( argc, argv ) (void)0
#endif

int lsmash_write_lsmash_indicator( lsmash_root_t *root );

#endif
