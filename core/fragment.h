/*****************************************************************************
 * fragment.h
 *****************************************************************************
 * Copyright (C) 2011-2017 L-SMASH project
 * Copyright (c) 2026 Yusuke Nakamura
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

int lsm_fragment_finish_final_movie
(
    lsmash_file_t        *file,
    lsmash_adhoc_remux_t *remux
);

int lsm_fragment_set_last_duration
(
    isom_traf_t *traf,
    uint32_t     last_duration
);

int lsm_fragment_append_track_run
(
    lsmash_file_t *file,
    isom_chunk_t  *chunk
);

int lsm_fragment_flush_pooled_samples
(
    lsmash_file_t *file,
    uint32_t       track_ID,
    uint32_t       last_sample_duration
);

int lsm_fragment_append_sample
(
    lsmash_file_t       *file,
    isom_trak_t         *trak,
    lsmash_sample_t     *sample,
    isom_sample_entry_t *sample_entry
);
