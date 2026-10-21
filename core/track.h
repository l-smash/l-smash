/*****************************************************************************
 * track.h
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

#ifndef LSMASH_TRACK_H
#define LSMASH_TRACK_H

isom_trak_t *lsm_track_get_trak( lsmash_file_t *file, uint32_t track_ID );
isom_trex_t *lsm_track_get_trex( isom_mvex_t *mvex, uint32_t track_ID );
isom_traf_t *lsm_track_get_traf( isom_moof_t *moof, uint32_t track_ID );
isom_tfra_t *lsm_track_get_tfra( isom_mfra_t *mfra, uint32_t track_ID );

int lsm_track_update_tkhd_duration( isom_trak_t *trak );
int lsm_track_set_creation_time( isom_trak_t *trak, uint64_t current_mp4time );
int lsm_track_setup_handler_reference( isom_hdlr_t *hdlr, uint32_t media_type );
isom_trak_t *lsm_track_create( lsmash_file_t *file, lsmash_media_type media_type );

int lsm_track_complement_data_reference( isom_minf_t *minf );
lsmash_file_t *lsm_track_get_written_media_file
(
    isom_trak_t *trak,
    uint32_t     sample_description_index
);

#endif
