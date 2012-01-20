/*****************************************************************************
 * isom.h:
 *****************************************************************************
 * Copyright (C) 2011-2012 L-SMASH project
 *
 * Authors: Hiroki Taniura <boiled.sugar@gmail.com>
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

#ifndef LSMASH_ISOM_H
#define LSMASH_ISOM_H

isom_tref_type_t *isom_add_track_reference_type( isom_tref_t *tref, isom_track_reference_type type, uint32_t ref_count, uint32_t *track_ID );
int isom_add_chpl_entry( isom_chpl_t *chpl, isom_chapter_entry_t *chap_data );
int isom_add_tref( isom_trak_entry_t *trak );
int isom_add_chpl( isom_moov_t *moov );
int isom_add_udta( lsmash_root_t *root, uint32_t track_ID );
void isom_remove_track_reference_type( isom_tref_type_t *ref );
void isom_remove_tref( isom_tref_t *tref );
void isom_remove_trak( isom_trak_entry_t *trak );

#endif
