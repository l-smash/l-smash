/*****************************************************************************
 * importer.h:
 *****************************************************************************
 * Copyright (C) 2010-2014 L-SMASH project
 *
 * Authors: Takashi Hirata <silverfilain@gmail.com>
 * Contributors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
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

#ifndef LSMASH_IMPORTER_H
#define LSMASH_IMPORTER_H

/***************************************************************************
    importer
***************************************************************************/

#ifdef LSMASH_IMPORTER_INTERNAL

#include "core/box.h"
#include "codecs/description.h"

struct importer_tag;

typedef void     ( *importer_cleanup )          ( struct importer_tag * );
typedef int      ( *importer_get_accessunit )   ( struct importer_tag *, uint32_t, lsmash_sample_t * );
typedef int      ( *importer_probe )            ( struct importer_tag * );
typedef uint32_t ( *importer_get_last_duration )( struct importer_tag *, uint32_t );

typedef enum
{
    IMPORTER_ERROR  = -1,
    IMPORTER_OK     = 0,
    IMPORTER_CHANGE = 1,
    IMPORTER_EOF    = 2,
} importer_status;

typedef struct
{
    lsmash_class_t             class;
    int                        detectable;
    importer_probe             probe;
    importer_get_accessunit    get_accessunit;
    importer_get_last_duration get_last_delta;
    importer_cleanup           cleanup;
} importer_functions;

typedef struct importer_tag
{
    const lsmash_class_t   *class;
    lsmash_log_level        log_level;
    importer_status         status;
    lsmash_bs_t            *bs;
    int                     is_stdin;
    void                   *info;      /* importer internal status information. */
    importer_functions      funcs;
    lsmash_entry_list_t    *summaries;
} importer_t;

#else

typedef void importer_t;

/* importing functions */
importer_t *lsmash_importer_open
(
    const char *identifier,
    const char *format
);

void lsmash_importer_close
(
    importer_t *importer
);

int lsmash_importer_get_access_unit
(
    importer_t      *importer,
    uint32_t         track_number,
    lsmash_sample_t *buffered_sample
);

uint32_t lsmash_importer_get_last_delta
(
    importer_t *importer,
    uint32_t    track_number
);

uint32_t lsmash_importer_get_track_count
(
    importer_t *importer
);

lsmash_summary_t *lsmash_duplicate_summary
(
    importer_t *importer,
    uint32_t    track_number
);

#endif /* #ifdef LSMASH_IMPORTER_INTERNAL */

#endif /* #ifndef LSMASH_IMPORTER_H */
