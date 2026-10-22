/*****************************************************************************
 * timeline.h
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

typedef struct isom_timeline_tag isom_timeline_t;

typedef struct
{
    uint64_t       data_offset;
    uint64_t       length;
    uint32_t       number;  /* useless at present */
    lsmash_file_t *file;
} isom_portable_chunk_t;

typedef struct
{
    uint64_t pos;                   /* position of the first sample in this bunch */
    uint32_t duration;              /* duration in media timescale each sample has */
    uint32_t offset;                /* offset between composition time and decoding time each sample has */
    uint32_t length;                /* data size each sample has */
    uint32_t index;                 /* sample_description_index applied to each sample */
    isom_portable_chunk_t *chunk;   /* chunk samples belong to */
    lsmash_sample_property_t prop;  /* property applied to each sample */
    uint32_t sample_count;          /* number of samples in this bunch */
} isom_lpcm_bunch_t;

typedef struct
{
    uint64_t pos;
    uint32_t duration;
    uint32_t offset;
    uint32_t length;        /* data size of the sample as users get it */
    uint32_t index;
    isom_portable_chunk_t *chunk;
    lsmash_sample_property_t prop;
    uint32_t raw_length;    /* data size stored at 'pos'. Set it only if it differs from 'length', that is, when a sample
                             * filter set by lsm_timeline_set_sample_filter() shrinks the stored data. 0 means 'length'. */
} isom_sample_info_t;

/* Convert the data stored in the stream into the sample data users get. The filter is applied to the raw data of
 * every sample read from the stream and shall set sample->length to the size of the converted data.
 * The raw data is in sample->data and its size is in sample->length on entry, and sample->pos is the position of the
 * raw data in the stream. The filter may keep the buffer and convert in place, or replace sample->data with a buffer
 * allocated by lsmash_malloc() after it frees the old one, which is required if the converted data is larger.
 * It must not depend on the order of the calls since samples are read in arbitrary order. The context is the one
 * passed to lsm_timeline_set_sample_filter() and is read-only for the filter. */
typedef int (*lsm_timeline_sample_filter)( lsmash_sample_t *sample, const void *context );

/* Release the context of a sample filter when the timeline is destroyed. */
typedef void (*lsm_timeline_filter_context_destructor)( void *context );

isom_timeline_t *lsm_timeline_create( void );

void lsm_timeline_destroy
(
    isom_timeline_t *timeline
);

int lsm_timeline_set_track_ID
(
    isom_timeline_t *timeline,
    uint32_t         track_ID
);

int lsm_timeline_set_movie_timescale
(
    isom_timeline_t *timeline,
    uint32_t         movie_timescale
);

int lsm_timeline_set_media_timescale
(
    isom_timeline_t *timeline,
    uint32_t         media_timescale
);

int lsm_timeline_set_sample_count
(
    isom_timeline_t *timeline,
    uint32_t         sample_count
);

int lsm_timeline_set_max_sample_size
(
    isom_timeline_t *timeline,
    uint32_t         max_sample_size
);

int lsm_timeline_set_media_duration
(
    isom_timeline_t *timeline,
    uint64_t         media_duration
);

int lsm_timeline_set_track_duration
(
    isom_timeline_t *timeline,
    uint64_t         track_duration
);

/* Timeline construction helpers for raw streams that an importer exposes as a fake track.
 * Movie and media share the same timescale, so the track duration equals the media duration.
 * The timeline reads each sample from info_list with the getters that lsm_timeline_setup_raw() installs. */
int lsm_timeline_setup_raw
(
    isom_timeline_t *timeline,
    uint32_t         track_ID,
    uint32_t         timescale
);

int lsm_timeline_add_sample_info_entry
(
    isom_timeline_t    *timeline,
    isom_sample_info_t *src_info
);

/* Append a chunk that belongs to the timeline and return it. The returned chunk stays valid as long as the timeline does,
 * so that a raw stream importer need not keep a chunk of its own alive. The caller sets the extent of the chunk. */
isom_portable_chunk_t *lsm_timeline_add_raw_chunk
(
    isom_timeline_t *timeline,
    lsmash_file_t   *file
);

/* The timeline owns the context from the call on and releases it by the destructor, even if the timeline is destroyed
 * without being registered. Passing NULL as the context needs no destructor. */
void lsm_timeline_set_sample_filter
(
    isom_timeline_t                       *timeline,
    lsm_timeline_sample_filter             filter,
    void                                  *context,
    lsm_timeline_filter_context_destructor destructor
);

int lsm_timeline_finish_raw
(
    isom_timeline_t *timeline,
    uint32_t         sample_count,
    uint32_t         max_sample_size,
    uint64_t         media_duration
);

/* Register the finished timeline to the file. The file owns the timeline on success. */
int lsm_timeline_register
(
    lsmash_file_t   *file,
    isom_timeline_t *timeline
);

void lsm_timeline_set_lpcm_sample_getter_funcs
(
    isom_timeline_t *timeline
);

isom_timeline_t *lsm_timeline_get
(
    lsmash_root_t *root,
    uint32_t       track_ID
);

void lsm_timeline_remove_all
(
    lsmash_file_t *file
);

int lsm_timeline_construct
(
    lsmash_root_t *root,
    uint32_t       track_ID
);

int lsm_timeline_add_lpcm_bunch_entry
(
    isom_timeline_t   *timeline,
    isom_lpcm_bunch_t *src_bunch
);

isom_elst_entry_t *lsm_timeline_get_explicit_timeline_map
(
    lsmash_root_t *root,
    uint32_t       track_ID,
    uint32_t       edit_number
);

uint32_t lsm_timeline_count_explicit_timeline_map
(
    lsmash_root_t *root,
    uint32_t       track_ID
);
