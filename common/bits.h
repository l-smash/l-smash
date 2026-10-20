/*****************************************************************************
 * bits.h
 *****************************************************************************
 * Copyright (C) 2010-2017 L-SMASH project
 *
 * Authors: Takashi Hirata <silverfilain@gmail.com>
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

/*****************************************************************************
 * Note: Bitstream Ownership & Borrowing Contract for lsmash_bits_t
 * ---------------------------------------------------------------------------
 * 1. Non-Owning Borrow (`lsmash_bits_init` / `lsmash_bits_create`):
 *    - `lsmash_bits_init( &bits, bs )` attaches `bits` to an existing
 *      `lsmash_bs_t` instance as a lightweight bit-level view/adapter.
 *      `bits` does NOT own `bs` or `bs->buffer`.
 *    - NEVER call `lsmash_bits_empty( &bits )` when `bits` borrows an active
 *      `lsmash_bs_t` (e.g., `file->bs` during Box reading/writing), because
 *      `lsmash_bits_empty()` invokes `lsmash_bs_empty( bits->bs )`, which
 *      wipes/resets the parent `bs`'s internal buffer and read/write state!
 *    - When finishing bit-level I/O on a borrowed `lsmash_bs_t`:
 *        * On read:  call `lsmash_bits_get_align( &bits )` (or simply let a
 *          stack-allocated `lsmash_bits_t` go out of scope if already byte-aligned).
 *        * On write: call `lsmash_bits_put_align( &bits )` to flush any cached
 *          partial byte to `bs`.
 *
 * 2. Dedicated / Ad-hoc Ownership (`lsmash_bits_adhoc_create` / codec parsers):
 *    - `lsmash_bits_empty( bits )` is ONLY valid when `bits->bs` is a dedicated
 *      scratch bitstream owned exclusively by that `bits` context (such as one
 *      created by `lsmash_bits_adhoc_create()` or a codec parser's private `bs`)
 *      and the caller intentionally wants to discard and reset the underlying
 *      `lsmash_bs_t` buffer.
 *****************************************************************************/

typedef struct
{
    lsmash_bs_t *bs;    /* Non-owning pointer when initialized via lsmash_bits_init() */
    uint8_t      store;
    uint8_t      cache;
} lsmash_bits_t;

/* Attach `bits` to an existing `bs` (non-owning borrow).
 * See Note: above: do NOT call lsmash_bits_empty() on a borrowed active `bs`. */
void lsmash_bits_init( lsmash_bits_t* bits, lsmash_bs_t *bs );
lsmash_bits_t *lsmash_bits_create( lsmash_bs_t *bs );
void lsmash_bits_cleanup( lsmash_bits_t *bits );
/* Reset both `bits` bit-cache AND the underlying `bits->bs` buffer via lsmash_bs_empty().
 * WARNING: Never call on a `lsmash_bits_t` borrowing an active parent `lsmash_bs_t`! */
void lsmash_bits_empty( lsmash_bits_t *bits );
void lsmash_bits_put_align( lsmash_bits_t *bits );
void lsmash_bits_get_align( lsmash_bits_t *bits );
void lsmash_bits_put( lsmash_bits_t *bits, uint32_t width, uint64_t value );
uint64_t lsmash_bits_get( lsmash_bits_t *bits, uint32_t width );
void *lsmash_bits_export_data( lsmash_bits_t *bits, uint32_t *length );
int lsmash_bits_import_data( lsmash_bits_t *bits, void *data, uint32_t length );

lsmash_bits_t *lsmash_bits_adhoc_create(void);
void lsmash_bits_adhoc_cleanup( lsmash_bits_t *bits );
