/*****************************************************************************
 * utils.h:
 *****************************************************************************
 * Copyright (C) 2010 L-SMASH project
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

#ifndef ISOM_UTIL_H
#define ISOM_UTIL_H

#include <stdio.h>
#include <inttypes.h>

#define debug_if(x) if(x)

/*---- bytestream ----*/

typedef struct
{
    FILE *stream;     /* I/O stream */
    uint8_t error;
    uint8_t *data;    /* buffer for reading/writing */
    uint64_t store;   /* valid data size on buffer */
    uint64_t alloc;   /* total buffer size including invalid area */
    uint64_t pos;     /* data position on buffer to be read next */
    uint64_t written; /* data size written into "stream" already */
} lsmash_bs_t;

uint64_t lsmash_bs_get_pos( lsmash_bs_t *bs );
void lsmash_bs_empty( lsmash_bs_t *bs );
void lsmash_bs_free( lsmash_bs_t *bs );
void lsmash_bs_alloc( lsmash_bs_t *bs, uint64_t size );
lsmash_bs_t* lsmash_bs_create( char* filename );
void lsmash_bs_cleanup( lsmash_bs_t *bs );

/*---- bytestream writer ----*/

void lsmash_bs_put_byte( lsmash_bs_t *bs, uint8_t value );
void lsmash_bs_put_bytes( lsmash_bs_t *bs, void *value, uint32_t size );
void lsmash_bs_put_be16( lsmash_bs_t *bs, uint16_t value );
void lsmash_bs_put_be24( lsmash_bs_t *bs, uint32_t value );
void lsmash_bs_put_be32( lsmash_bs_t *bs, uint32_t value );
void lsmash_bs_put_be64( lsmash_bs_t *bs, uint64_t value );
int lsmash_bs_write_data( lsmash_bs_t *bs );

void* lsmash_bs_export_data( lsmash_bs_t *bs, uint32_t* length );

/*---- bytestream reader ----*/
uint8_t lsmash_bs_get_byte( lsmash_bs_t *bs );
uint8_t *lsmash_bs_get_bytes( lsmash_bs_t *bs, uint32_t size );
uint16_t lsmash_bs_get_be16( lsmash_bs_t *bs );
uint32_t lsmash_bs_get_be24( lsmash_bs_t *bs );
uint32_t lsmash_bs_get_be32( lsmash_bs_t *bs );
uint64_t lsmash_bs_get_be64( lsmash_bs_t *bs );
int lsmash_bs_read_data( lsmash_bs_t *bs, uint32_t size );

/*---- bitstream ----*/
typedef struct {
    lsmash_bs_t* bs;
    uint8_t store;
    uint8_t cache;
} lsmash_bits_t;

void lsmash_bits_init( lsmash_bits_t* bits, lsmash_bs_t *bs );
lsmash_bits_t* lsmash_bits_create( lsmash_bs_t *bs );
void lsmash_bits_put_align( lsmash_bits_t *bits );
void lsmash_bits_get_align( lsmash_bits_t *bits );
void lsmash_bits_cleanup( lsmash_bits_t *bits );

/*---- bitstream writer ----*/
void lsmash_bits_put( lsmash_bits_t *bits, uint32_t value, uint32_t width );
uint32_t lsmash_bits_get( lsmash_bits_t *bits, uint32_t width );
lsmash_bits_t* lsmash_bits_adhoc_create();
void lsmash_bits_adhoc_cleanup( lsmash_bits_t* bits );
void* lsmash_bits_export_data( lsmash_bits_t* bits, uint32_t* length );
int lsmash_bits_import_data( lsmash_bits_t* bits, void* data, uint32_t length );

/*---- list ----*/

typedef struct lsmash_entry_tag lsmash_entry_t;

struct lsmash_entry_tag
{
    lsmash_entry_t *next;
    lsmash_entry_t *prev;
    void *data;
};

typedef struct
{
    uint32_t entry_count;
    lsmash_entry_t *head;
    lsmash_entry_t *tail;
} lsmash_entry_list_t;

typedef void (*lsmash_entry_data_eliminator)(void* data); /* very same as free() of standard c lib; void free(void *); */

lsmash_entry_list_t *lsmash_create_entry_list( void );
int lsmash_add_entry( lsmash_entry_list_t *list, void *data );
int lsmash_remove_entry_direct( lsmash_entry_list_t *list, lsmash_entry_t *entry );
int lsmash_remove_entry( lsmash_entry_list_t *list, uint32_t entry_number );
void lsmash_remove_entries( lsmash_entry_list_t *list, void* eliminator );
void lsmash_remove_list( lsmash_entry_list_t *list, void* eliminator );

lsmash_entry_t *lsmash_get_entry( lsmash_entry_list_t *list, uint32_t entry_number );
void *lsmash_get_entry_data( lsmash_entry_list_t *list, uint32_t entry_number );

#endif
