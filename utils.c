/*****************************************************************************
 * utils.c:
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

#include <stdlib.h>
#include <string.h>

#include "utils.h"

uint64_t lsmash_bs_get_pos( lsmash_bs_t *bs )
{
    return bs->pos;
}

void lsmash_bs_empty( lsmash_bs_t *bs )
{
    if( !bs )
        return;
    memset( bs->data, 0, bs->alloc );
    bs->store = 0;
    bs->pos = 0;
}

void lsmash_bs_free( lsmash_bs_t *bs )
{
    if( bs->data )
        free( bs->data );
    bs->data = NULL;
    bs->alloc = 0;
    bs->store = 0;
    bs->pos = 0;
}

void lsmash_bs_alloc( lsmash_bs_t *bs, uint64_t size )
{
    if( bs->error )
        return;
    uint64_t alloc = size + (1<<16);
    uint8_t *data;
    if( !bs->data )
        data = malloc( alloc );
    else if( bs->alloc < size )
        data = realloc( bs->data, alloc );
    else
        return;
    if( !data )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return;
    }
    bs->data  = data;
    bs->alloc = alloc;
}

/*---- bitstream writer ----*/
void lsmash_bs_put_byte( lsmash_bs_t *bs, uint8_t value )
{
    lsmash_bs_alloc( bs, bs->store + 1 );
    if( bs->error )
        return;
    bs->data[bs->store ++] = value;
}

void lsmash_bs_put_bytes( lsmash_bs_t *bs, void *value, uint32_t size )
{
    if( !size )
        return;
    lsmash_bs_alloc( bs, bs->store + size );
    if( bs->error )
        return;
    memcpy( bs->data + bs->store, value, size );
    bs->store += size;
}

void lsmash_bs_put_be16( lsmash_bs_t *bs, uint16_t value )
{
    lsmash_bs_put_byte( bs, (uint8_t)((value>>8)&0xff) );
    lsmash_bs_put_byte( bs, (uint8_t)(value&0xff) );
}

void lsmash_bs_put_be24( lsmash_bs_t *bs, uint32_t value )
{
    lsmash_bs_put_byte( bs, (uint8_t)((value>>16)&0xff) );
    lsmash_bs_put_be16( bs, (uint16_t)(value&0xffff) );
}

void lsmash_bs_put_be32( lsmash_bs_t *bs, uint32_t value )
{
    lsmash_bs_put_be16( bs, (uint16_t)((value>>16)&0xffff) );
    lsmash_bs_put_be16( bs, (uint16_t)(value&0xffff) );
}

void lsmash_bs_put_be64( lsmash_bs_t *bs, uint64_t value )
{
    lsmash_bs_put_be32( bs, (uint32_t)((value>>32)&0xffffffff) );
    lsmash_bs_put_be32( bs, (uint32_t)(value&0xffffffff) );
}

int lsmash_bs_write_data( lsmash_bs_t *bs )
{
    if( !bs )
        return -1;
    if( !bs->data )
        return 0;
    if( bs->error || !bs->stream || fwrite( bs->data, 1, bs->store, bs->stream ) != bs->store )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return -1;
    }
    bs->written += bs->store;
    bs->store = 0;
    return 0;
}

lsmash_bs_t* lsmash_bs_create( char* filename )
{
    lsmash_bs_t* bs = malloc( sizeof(lsmash_bs_t) );
    if( !bs )
        return NULL;
    memset( bs, 0, sizeof(lsmash_bs_t) );
    if( filename && (bs->stream = fopen( filename, "wb" )) == NULL )
    {
        free( bs );
        return NULL;
    }
    return bs;
}

void lsmash_bs_cleanup( lsmash_bs_t *bs )
{
    if( !bs )
        return;
    if( bs->stream )
        fclose( bs->stream );
    lsmash_bs_free( bs );
    free( bs );
}

void* lsmash_bs_export_data( lsmash_bs_t *bs, uint32_t* length )
{
    if( !bs || !bs->data || bs->store == 0 || bs->error )
        return NULL;
    void* buf = malloc( bs->store );
    if( !buf )
        return NULL;
    memcpy( buf, bs->data, bs->store );
    if( length )
        *length = bs->store;
    return buf;
}
/*---- ----*/

/*---- bitstream reader ----*/
uint8_t lsmash_bs_get_byte( lsmash_bs_t *bs )
{
    if( bs->error || !bs->data )
        return 0;
    if( bs->pos + 1 > bs->store )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return 0;
    }
    return bs->data[bs->pos ++];
}

uint8_t *lsmash_bs_get_bytes( lsmash_bs_t *bs, uint32_t size )
{
    if( bs->error || !size )
        return NULL;
    uint8_t *value = malloc( size );
    if( !value || bs->pos + size > bs->store )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return NULL;
    }
    memcpy( value, bs->data + bs->pos, size );
    bs->pos += size;
    return value;
}

uint16_t lsmash_bs_get_be16( lsmash_bs_t *bs )
{
    uint16_t    value = lsmash_bs_get_byte( bs );
    return (value<<8) | lsmash_bs_get_byte( bs );
}

uint32_t lsmash_bs_get_be24( lsmash_bs_t *bs )
{
    uint32_t    value = lsmash_bs_get_be16( bs );
    return (value<<8) | lsmash_bs_get_byte( bs );
}

uint32_t lsmash_bs_get_be32( lsmash_bs_t *bs )
{
    uint32_t     value = lsmash_bs_get_be16( bs );
    return (value<<16) | lsmash_bs_get_be16( bs );
}

uint64_t lsmash_bs_get_be64( lsmash_bs_t *bs )
{
    uint64_t     value = lsmash_bs_get_be32( bs );
    return (value<<32) | lsmash_bs_get_be32( bs );
}

int lsmash_bs_read_data( lsmash_bs_t *bs, uint32_t size )
{
    if( !bs )
        return -1;
    if( !size )
        return 0;
    lsmash_bs_alloc( bs, bs->store + size );
    if( bs->error || !bs->stream )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return -1;
    }
    uint64_t read_size = fread( bs->data + bs->store, 1, size, bs->stream );
    if( read_size != size && !feof( bs->stream ) )
    {
        bs->error = 1;
        return -1;
    }
    bs->store += read_size;
    return 0;
}

int lsmash_bs_import_data( lsmash_bs_t *bs, void* data, uint32_t length )
{
    if( !bs || bs->error || !data || length == 0 )
        return -1;
    lsmash_bs_alloc( bs, bs->store + length );
    if( bs->error || !bs->data ) /* means, failed to alloc. */
    {
        lsmash_bs_free( bs );
        return -1;
    }
    memcpy( bs->data + bs->store, data, length );
    bs->store += length;
    return 0;
}
/*---- ----*/

/*---- bitstream ----*/
void lsmash_bits_init( lsmash_bits_t* bits, lsmash_bs_t *bs )
{
    debug_if( !bits || !bs )
        return;
    bits->bs = bs;
    bits->store = 0;
    bits->cache = 0;
}

lsmash_bits_t* lsmash_bits_create( lsmash_bs_t *bs )
{
    debug_if( !bs )
        return NULL;
    lsmash_bits_t* bits = (lsmash_bits_t*)malloc( sizeof(lsmash_bits_t) );
    if( !bits )
        return NULL;
    lsmash_bits_init( bits, bs );
    return bits;
}

#define BITS_IN_BYTE 8
void lsmash_bits_put_align( lsmash_bits_t *bits )
{
    debug_if( !bits )
        return;
    if( !bits->store )
        return;
    lsmash_bs_put_byte( bits->bs, bits->cache << ( BITS_IN_BYTE - bits->store ) );
}

void lsmash_bits_get_align( lsmash_bits_t *bits )
{
    debug_if( !bits )
        return;
    bits->store = 0;
    bits->cache = 0;
}

/* Must be used ONLY for bits struct created with isom_create_bits.
   Otherwise, just free() the bits struct. */
void lsmash_bits_cleanup( lsmash_bits_t *bits )
{
    debug_if( !bits )
        return;
    free( bits );
}

/* we can change value's type to unsigned int for 64-bit operation if needed. */
static inline uint8_t lsmash_bits_mask_lsb8( uint32_t value, uint32_t width )
{
    return (uint8_t)( value & ~( ~0U << width ) );
}

/* We can change value's type to unsigned int for 64-bit operation if needed. */
void lsmash_bits_put( lsmash_bits_t *bits, uint32_t value, uint32_t width )
{
    debug_if( !bits || !width )
        return;
    if( bits->store )
    {
        if( bits->store + width < BITS_IN_BYTE )
        {
            /* cache can contain all of value's bits. */
            bits->cache <<= width;
            bits->cache |= lsmash_bits_mask_lsb8( value, width );
            bits->store += width;
            return;
        }
        /* flush cache with value's some leading bits. */
        uint32_t free_bits = BITS_IN_BYTE - bits->store;
        bits->cache <<= free_bits;
        bits->cache |= lsmash_bits_mask_lsb8( value >> (width -= free_bits), free_bits );
        lsmash_bs_put_byte( bits->bs, bits->cache );
        bits->store = 0;
        bits->cache = 0;
    }
    /* cache is empty here. */
    /* byte unit operation. */
    while( width > BITS_IN_BYTE )
        lsmash_bs_put_byte( bits->bs, (uint8_t)(value >> (width -= BITS_IN_BYTE)) );
    /* bit unit operation for residual. */
    if( width )
    {
        bits->cache = lsmash_bits_mask_lsb8( value, width );
        bits->store = width;
    }
}

/* We can change value's type to unsigned int for 64-bit operation if needed. */
uint32_t lsmash_bits_get( lsmash_bits_t *bits, uint32_t width )
{
    debug_if( !bits || !width )
        return 0;
    uint32_t value = 0;
    if( bits->store )
    {
        if( bits->store >= width )
        {
            /* cache contains all of bits required. */
            bits->store -= width;
            return lsmash_bits_mask_lsb8( bits->cache >> bits->store, width );
        }
        /* fill value's leading bits with cache's residual. */
        value = lsmash_bits_mask_lsb8( bits->cache, bits->store );
        width -= bits->store;
        bits->store = 0;
        bits->cache = 0;
    }
    /* cache is empty here. */
    /* byte unit operation. */
    while( width > BITS_IN_BYTE )
    {
        value <<= BITS_IN_BYTE;
        width -= BITS_IN_BYTE;
        value |= lsmash_bs_get_byte( bits->bs );
    }
    /* bit unit operation for residual. */
    if( width )
    {
        bits->cache = lsmash_bs_get_byte( bits->bs );
        bits->store = BITS_IN_BYTE - width;
        value <<= width;
        value |= lsmash_bits_mask_lsb8( bits->cache >> bits->store, width );
    }
    return value;
}

/****
 bitstream with bytestream for adhoc operation
****/

lsmash_bits_t* lsmash_bits_adhoc_create()
{
    lsmash_bs_t* bs = lsmash_bs_create( NULL ); /* no file writing */
    if( !bs )
        return NULL;
    lsmash_bits_t* bits = lsmash_bits_create( bs );
    if( !bits )
    {
        lsmash_bs_cleanup( bs );
        return NULL;
    }
    return bits;
}

void lsmash_bits_adhoc_cleanup( lsmash_bits_t* bits )
{
    if( !bits )
        return;
    lsmash_bs_cleanup( bits->bs );
    lsmash_bits_cleanup( bits );
}

void* lsmash_bits_export_data( lsmash_bits_t* bits, uint32_t* length )
{
    lsmash_bits_put_align( bits );
    return lsmash_bs_export_data( bits->bs, length );
}

int lsmash_bits_import_data( lsmash_bits_t* bits, void* data, uint32_t length )
{
    return lsmash_bs_import_data( bits->bs, data, length );
}
/*---- ----*/

/*---- list ----*/
lsmash_entry_list_t *lsmash_create_entry_list( void )
{
    lsmash_entry_list_t *list = malloc( sizeof(lsmash_entry_list_t) );
    if( !list )
        return NULL;
    list->entry_count = 0;
    list->head = NULL;
    list->tail = NULL;
    return list;
}

int lsmash_add_entry( lsmash_entry_list_t *list, void *data )
{
    if( !list )
        return -1;
    lsmash_entry_t *entry = malloc( sizeof(lsmash_entry_t) );
    if( !entry )
        return -1;
    entry->next = NULL;
    entry->prev = list->tail;
    entry->data = data;
    if( list->head )
        list->tail->next = entry;
    else
        list->head = entry;
    list->tail = entry;
    list->entry_count += 1;
    return 0;
}

int lsmash_remove_entry_direct( lsmash_entry_list_t *list, lsmash_entry_t *entry, void* eliminator )
{
    if( !entry )
        return -1;
    if( !eliminator )
        eliminator = free;
    lsmash_entry_t *next = entry->next;
    lsmash_entry_t *prev = entry->prev;
    if( entry == list->head )
        list->head = next;
    else
        prev->next = next;
    if( entry == list->tail )
        list->tail = prev;
    else
        next->prev = prev;
    if( entry->data )
        ((lsmash_entry_data_eliminator)eliminator)( entry->data );
    free( entry );
    list->entry_count -= 1;
    return 0;
}

int lsmash_remove_entry( lsmash_entry_list_t *list, uint32_t entry_number, void* eliminator )
{
    lsmash_entry_t *entry = lsmash_get_entry( list, entry_number );
    return lsmash_remove_entry_direct( list, entry, eliminator );
}

void lsmash_remove_entries( lsmash_entry_list_t *list, void* eliminator )
{
    if( !list )
        return;
    if( !eliminator )
        eliminator = free;
    for( lsmash_entry_t *entry = list->head; entry; )
    {
        lsmash_entry_t *next = entry->next;
        if( entry->data )
            ((lsmash_entry_data_eliminator)eliminator)( entry->data );
        free( entry );
        entry = next;
    }
    list->entry_count = 0;
    list->head = NULL;
    list->tail = NULL;
}

void lsmash_remove_list( lsmash_entry_list_t *list, void* eliminator )
{
    if( !list )
        return;
    lsmash_remove_entries( list, eliminator );
    free( list );
}

lsmash_entry_t *lsmash_get_entry( lsmash_entry_list_t *list, uint32_t entry_number )
{
    if( !list || !entry_number || entry_number > list->entry_count )
        return NULL;
    lsmash_entry_t *entry;
    for( entry = list->head; entry && --entry_number; entry = entry->next );
    return entry;
}

void *lsmash_get_entry_data( lsmash_entry_list_t *list, uint32_t entry_number )
{
    lsmash_entry_t *entry = lsmash_get_entry( list, entry_number );
    return entry ? entry->data : NULL;
}
