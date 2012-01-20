/*****************************************************************************
 * write.h:
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

#ifndef LSMASH_WRITE_H
#define LSMASH_WRITE_H

int isom_write_meta( lsmash_bs_t *bs, isom_meta_t *meta );
int isom_write_udta( lsmash_bs_t *bs, isom_moov_t *moov, isom_trak_entry_t *trak );
int isom_write_trak( lsmash_bs_t *bs, isom_trak_entry_t *trak );
int isom_write_iods( lsmash_root_t *root );
int isom_write_mvhd( lsmash_root_t *root );
int isom_write_mehd( lsmash_bs_t *bs, isom_mehd_t *mehd );
int isom_write_moof( lsmash_bs_t *bs, isom_moof_entry_t *moof );
int isom_write_mfra( lsmash_bs_t *bs, isom_mfra_t *mfra );
int isom_write_mdat_header( lsmash_root_t *root, uint64_t media_size );
int isom_write_mdat_size( lsmash_root_t *root );
int isom_write_ftyp( lsmash_root_t *root );
int isom_write_moov( lsmash_root_t *root );

#endif
