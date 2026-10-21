/*****************************************************************************
 * sample.h
 *****************************************************************************
 * Copyright (C) 2010-2017 L-SMASH project
 * Copyright (c) 2026 Yusuke Nakamura
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 * Contributors: Takashi Hirata <silverfilain@gmail.com>
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

#ifndef LSMASH_SAMPLE_H
#define LSMASH_SAMPLE_H

struct isom_sample_pool_tag
{
    uint64_t alloc;             /* total buffer size for the pool */
    uint64_t size;              /* total size of samples in the pool */
    uint32_t sample_count;      /* number of samples in the pool */
    uint8_t *data;              /* actual data of samples in the pool */
};

isom_sample_pool_t *lsm_sample_create_pool( uint64_t size );
void lsm_sample_clear_pool( void *opaque_pool );
void lsm_sample_remove_pool( isom_sample_pool_t *pool );

int lsm_sample_update_tables
(
    isom_trak_t         *trak,
    lsmash_sample_t     *sample,
    uint32_t            *samples_per_packet,
    isom_sample_entry_t *sample_entry
);

int lsm_sample_pool_push
(
    isom_sample_pool_t *pool,
    lsmash_sample_t    *sample,
    uint32_t            samples_per_packet
);

int lsm_sample_append_by_type
(
    void                *track,
    lsmash_sample_t     *sample,
    isom_sample_entry_t *sample_entry,
    int (*func_append_sample)( void *, lsmash_sample_t *, isom_sample_entry_t * )
);

#endif
