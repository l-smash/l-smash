/*****************************************************************************
 * nalu.c
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

#include "common/internal.h" /* must be placed first */

#include <string.h>

#include "nalu.h"

isom_dcr_ps_entry_t *isom_create_ps_entry
(
    uint8_t *ps,
    uint32_t ps_size
)
{
    isom_dcr_ps_entry_t *entry = lsmash_malloc( sizeof(isom_dcr_ps_entry_t) );
    if( !entry )
        return NULL;
    entry->nalUnit = lsmash_memdup( ps, ps_size );
    if( !entry->nalUnit )
    {
        lsmash_free( entry );
        return NULL;
    }
    entry->nalUnitLength = ps_size;
    entry->unused        = 0;
    return entry;
}

void isom_remove_dcr_ps
(
    isom_dcr_ps_entry_t *ps
)
{
    if( !ps )
        return;
    lsmash_free( ps->nalUnit );
    lsmash_free( ps );
}
