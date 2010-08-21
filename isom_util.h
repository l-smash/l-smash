/*****************************************************************************
 * isom_util.h:
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
} isom_bs_t;

void isom_bs_free( isom_bs_t *bs );
void isom_bs_alloc( isom_bs_t *bs, uint64_t size );
isom_bs_t* isom_bs_create( char* filename );
void isom_bs_cleanup( isom_bs_t *bs );

/*---- bytestream writer ----*/

void isom_bs_put_byte( isom_bs_t *bs, uint8_t value );
void isom_bs_put_bytes( isom_bs_t *bs, void *value, uint32_t size );
void isom_bs_put_be16( isom_bs_t *bs, uint16_t value );
void isom_bs_put_be24( isom_bs_t *bs, uint32_t value );
void isom_bs_put_be32( isom_bs_t *bs, uint32_t value );
void isom_bs_put_be64( isom_bs_t *bs, uint64_t value );
int isom_bs_write_data( isom_bs_t *bs );

void* isom_bs_export_data( isom_bs_t *bs, uint32_t* length );

/*---- bytestream reader ----*/
uint8_t isom_bs_read_byte( isom_bs_t *bs );
uint8_t *isom_bs_read_bytes( isom_bs_t *bs, uint32_t size );
uint16_t isom_bs_read_be16( isom_bs_t *bs );
uint32_t isom_bs_read_be24( isom_bs_t *bs );
uint32_t isom_bs_read_be32( isom_bs_t *bs );
uint64_t isom_bs_read_be64( isom_bs_t *bs );
int isom_bs_read_data( isom_bs_t *bs, uint64_t size );

/*---- bitstream ----*/
typedef struct {
    isom_bs_t* bs;
    uint8_t store;
    uint8_t cache;
} mp4sys_bits_t;

void mp4sys_bits_init( mp4sys_bits_t* bits, isom_bs_t *bs );
mp4sys_bits_t* mp4sys_bits_create( isom_bs_t *bs );
void mp4sys_bits_align( mp4sys_bits_t *bits );
void mp4sys_bits_cleanup( mp4sys_bits_t *bits );

/*---- bitstream writer ----*/
void mp4sys_bits_put( mp4sys_bits_t *bits, uint32_t value, uint32_t width );
mp4sys_bits_t* mp4sys_adhoc_bits_create();
void mp4sys_adhoc_bits_cleanup( mp4sys_bits_t* bits );
void* mp4sys_bs_export_data( mp4sys_bits_t* bits, uint32_t* length );

/*---- list ----*/

typedef struct isom_entry_tag isom_entry_t;

struct isom_entry_tag
{
    isom_entry_t *next;
    isom_entry_t *prev;
    void *data;
};

typedef struct
{
    uint32_t entry_count;
    isom_entry_t *head;
    isom_entry_t *tail;
} isom_entry_list_t;

typedef void (*isom_entry_data_eliminator)(void* data); /* very same as free() of standard c lib; void free(void *); */

isom_entry_list_t *isom_create_entry_list( void );
int isom_add_entry( isom_entry_list_t *list, void *data );
int isom_remove_entry( isom_entry_list_t *list, uint32_t entry_number );
void isom_remove_entries( isom_entry_list_t *list, void* eliminator );
void isom_remove_list( isom_entry_list_t *list, void* eliminator );

isom_entry_t *isom_get_entry( isom_entry_list_t *list, uint32_t entry_number );
void *isom_get_entry_data( isom_entry_list_t *list, uint32_t entry_number );

#endif
