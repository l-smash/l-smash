/*****************************************************************************
 * isom_util.c:
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

#include "isom_util.h"

uint64_t isom_bs_get_pos( isom_bs_t *bs )
{
    return bs->pos;
}

void isom_bs_empty( isom_bs_t *bs )
{
    if( !bs )
        return;
    memset( bs->data, 0, bs->alloc );
    bs->store = 0;
    bs->pos = 0;
}

void isom_bs_free( isom_bs_t *bs )
{
    if( bs->data )
        free( bs->data );
    bs->data = NULL;
    bs->alloc = 0;
    bs->store = 0;
    bs->pos = 0;
}

void isom_bs_alloc( isom_bs_t *bs, uint64_t size )
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
        isom_bs_free( bs );
        bs->error = 1;
        return;
    }
    bs->data  = data;
    bs->alloc = alloc;
}

/*---- bitstream writer ----*/
void isom_bs_put_byte( isom_bs_t *bs, uint8_t value )
{
    isom_bs_alloc( bs, bs->store + 1 );
    if( bs->error )
        return;
    bs->data[bs->store ++] = value;
}

void isom_bs_put_bytes( isom_bs_t *bs, void *value, uint32_t size )
{
    if( !size )
        return;
    isom_bs_alloc( bs, bs->store + size );
    if( bs->error )
        return;
    memcpy( bs->data + bs->store, value, size );
    bs->store += size;
}

void isom_bs_put_be16( isom_bs_t *bs, uint16_t value )
{
    isom_bs_put_byte( bs, (uint8_t)((value>>8)&0xff) );
    isom_bs_put_byte( bs, (uint8_t)(value&0xff) );
}

void isom_bs_put_be24( isom_bs_t *bs, uint32_t value )
{
    isom_bs_put_be16( bs, (uint16_t)((value>>8)&0xff) );
    isom_bs_put_byte( bs, (uint8_t)(value&0xff) );
}

void isom_bs_put_be32( isom_bs_t *bs, uint32_t value )
{
    isom_bs_put_be16( bs, (uint16_t)((value>>16)&0xffff) );
    isom_bs_put_be16( bs, (uint16_t)(value&0xffff) );
}

void isom_bs_put_be64( isom_bs_t *bs, uint64_t value )
{
    isom_bs_put_be32( bs, (uint32_t)((value>>32)&0xffffffff) );
    isom_bs_put_be32( bs, (uint32_t)(value&0xffffffff) );
}

int isom_bs_write_data( isom_bs_t *bs )
{
    if( !bs )
        return -1;
    if( !bs->data )
        return 0;
    if( bs->error || !bs->stream || fwrite( bs->data, 1, bs->store, bs->stream ) != bs->store )
    {
        isom_bs_free( bs );
        bs->error = 1;
        return -1;
    }
    bs->written += bs->store;
    bs->store = 0;
    return 0;
}

isom_bs_t* isom_bs_create( char* filename )
{
    isom_bs_t* bs = malloc( sizeof(isom_bs_t) );
    if( !bs )
        return NULL;
    memset( bs, 0, sizeof(isom_bs_t) );
    if( filename && (bs->stream = fopen( filename, "wb" )) == NULL )
    {
        free( bs );
        return NULL;
    }
    return bs;
}

void isom_bs_cleanup( isom_bs_t *bs )
{
    if( !bs )
        return;
    if( bs->stream )
        fclose( bs->stream );
    isom_bs_free( bs );
    free( bs );
}

void* isom_bs_export_data( isom_bs_t *bs, uint32_t* length )
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
uint8_t isom_bs_get_byte( isom_bs_t *bs )
{
    if( bs->error || !bs->data )
        return 0;
    if( bs->pos + 1 > bs->store )
    {
        isom_bs_free( bs );
        bs->error = 1;
        return 0;
    }
    return bs->data[bs->pos ++];
}

uint8_t *isom_bs_get_bytes( isom_bs_t *bs, uint32_t size )
{
    if( bs->error || !size )
        return NULL;
    uint8_t *value = malloc( size );
    if( !value || bs->pos + size > bs->store )
    {
        isom_bs_free( bs );
        bs->error = 1;
        return NULL;
    }
    memcpy( value, bs->data + bs->pos, size );
    bs->pos += size;
    return value;
}

uint16_t isom_bs_get_be16( isom_bs_t *bs )
{
    uint16_t    value = isom_bs_get_byte( bs );
    return (value<<8) | isom_bs_get_byte( bs );
}

uint32_t isom_bs_get_be24( isom_bs_t *bs )
{
    uint32_t    value = isom_bs_get_be16( bs );
    return (value<<8) | isom_bs_get_byte( bs );
}

uint32_t isom_bs_get_be32( isom_bs_t *bs )
{
    uint32_t     value = isom_bs_get_be16( bs );
    return (value<<16) | isom_bs_get_be16( bs );
}

uint64_t isom_bs_get_be64( isom_bs_t *bs )
{
    uint64_t     value = isom_bs_get_be32( bs );
    return (value<<32) | isom_bs_get_be32( bs );
}

int isom_bs_read_data( isom_bs_t *bs, uint32_t size )
{
    if( !bs )
        return -1;
    if( !size )
        return 0;
    isom_bs_alloc( bs, bs->store + size );
    if( bs->error || !bs->stream )
    {
        isom_bs_free( bs );
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

int isom_bs_import_data( isom_bs_t *bs, void* data, uint32_t length )
{
    if( !bs || bs->error || !data || length == 0 )
        return -1;
    isom_bs_alloc( bs, bs->store + length );
    if( bs->error || !bs->data ) /* means, failed to alloc. */
    {
        isom_bs_free( bs );
        return -1;
    }
    memcpy( bs->data + bs->store, data, length );
    bs->store += length;
    return 0;
}
/*---- ----*/

/*---- bitstream ----*/
void mp4sys_bits_init( mp4sys_bits_t* bits, isom_bs_t *bs )
{
    debug_if( !bits || !bs )
        return;
    bits->bs = bs;
    bits->store = 0;
    bits->cache = 0;
}

mp4sys_bits_t* mp4sys_bits_create( isom_bs_t *bs )
{
    debug_if( !bs )
        return NULL;
    mp4sys_bits_t* bits = (mp4sys_bits_t*)malloc( sizeof(mp4sys_bits_t) );
    if( !bits )
        return NULL;
    mp4sys_bits_init( bits, bs );
    return bits;
}

#define BITS_IN_BYTE 8
void mp4sys_bits_put_align( mp4sys_bits_t *bits )
{
    debug_if( !bits )
        return;
    if( !bits->store )
        return;
    isom_bs_put_byte( bits->bs, bits->cache << ( BITS_IN_BYTE - bits->store ) );
}

void mp4sys_bits_get_align( mp4sys_bits_t *bits )
{
    debug_if( !bits )
        return;
    bits->store = 0;
    bits->cache = 0;
}

/* Must be used ONLY for bits struct created with isom_create_bits.
   Otherwise, just free() the bits struct. */
void mp4sys_bits_cleanup( mp4sys_bits_t *bits )
{
    debug_if( !bits )
        return;
    free( bits );
}

/* we can change value's type to unsigned int for 64-bit operation if needed. */
static inline uint8_t mp4sys_bits_mask_lsb8( uint32_t value, uint32_t width )
{
    return (uint8_t)( value & ~( ~0U << width ) );
}

/* We can change value's type to unsigned int for 64-bit operation if needed. */
void mp4sys_bits_put( mp4sys_bits_t *bits, uint32_t value, uint32_t width )
{
    debug_if( !bits || !width )
        return;
    if( bits->store )
    {
        if( bits->store + width < BITS_IN_BYTE )
        {
            /* cache can contain all of value's bits. */
            bits->cache <<= width;
            bits->cache |= mp4sys_bits_mask_lsb8( value, width );
            bits->store += width;
            return;
        }
        /* flush cache with value's some leading bits. */
        uint32_t free_bits = BITS_IN_BYTE - bits->store;
        bits->cache <<= free_bits;
        bits->cache |= mp4sys_bits_mask_lsb8( value >> (width -= free_bits), free_bits );
        isom_bs_put_byte( bits->bs, bits->cache );
        bits->store = 0;
        bits->cache = 0;
    }
    /* cache is empty here. */
    /* byte unit operation. */
    while( width > BITS_IN_BYTE )
        isom_bs_put_byte( bits->bs, (uint8_t)(value >> (width -= BITS_IN_BYTE)) );
    /* bit unit operation for residual. */
    if( width )
    {
        bits->cache = mp4sys_bits_mask_lsb8( value, width );
        bits->store = width;
    }
}

/* We can change value's type to unsigned int for 64-bit operation if needed. */
uint32_t mp4sys_bits_get( mp4sys_bits_t *bits, uint32_t width )
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
            return mp4sys_bits_mask_lsb8( bits->cache >> bits->store, width );
        }
        /* fill value's leading bits with cache's residual. */
        value = mp4sys_bits_mask_lsb8( bits->cache, bits->store );
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
        value |= isom_bs_get_byte( bits->bs );
    }
    /* bit unit operation for residual. */
    if( width )
    {
        bits->cache = isom_bs_get_byte( bits->bs );
        bits->store = BITS_IN_BYTE - width;
        value <<= width;
        value |= mp4sys_bits_mask_lsb8( bits->cache >> bits->store, width );
    }
    return value;
}

/****
 bitstream with bytestream for adhoc operation
****/

mp4sys_bits_t* mp4sys_adhoc_bits_create()
{
    isom_bs_t* bs = isom_bs_create( NULL ); /* no file writing */
    if( !bs )
        return NULL;
    mp4sys_bits_t* bits = mp4sys_bits_create( bs );
    if( !bits )
    {
        isom_bs_cleanup( bs );
        return NULL;
    }
    return bits;
}

void mp4sys_adhoc_bits_cleanup( mp4sys_bits_t* bits )
{
    if( !bits )
        return;
    isom_bs_cleanup( bits->bs );
    mp4sys_bits_cleanup( bits );
}

void* mp4sys_bits_export_data( mp4sys_bits_t* bits, uint32_t* length )
{
    mp4sys_bits_put_align( bits );
    return isom_bs_export_data( bits->bs, length );
}

int mp4sys_bits_import_data( mp4sys_bits_t* bits, void* data, uint32_t length )
{
    return isom_bs_import_data( bits->bs, data, length );
}
/*---- ----*/

/*---- list ----*/
isom_entry_list_t *isom_create_entry_list( void )
{
    isom_entry_list_t *list = malloc( sizeof(isom_entry_list_t) );
    if( !list )
        return NULL;
    list->entry_count = 0;
    list->head = NULL;
    list->tail = NULL;
    return list;
}

int isom_add_entry( isom_entry_list_t *list, void *data )
{
    if( !list )
        return -1;
    isom_entry_t *entry = malloc( sizeof(isom_entry_t) );
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

int isom_remove_entry( isom_entry_list_t *list, uint32_t entry_number )
{
    if( !list )
        return -1;
    isom_entry_t *entry = list->head;
    uint32_t i = 0;
    for( i = 0; i < entry_number && entry; i++ )
        entry = entry->next;
    if( i < entry_number )
        return -1;
    if( entry )
    {
        isom_entry_t *next = entry->next;
        isom_entry_t *prev = entry->prev;
        if( entry->data )
            free( entry->data );
        free( entry );
        entry = next;
        if( entry )
        {
            if( prev )
                prev->next = entry;
            entry->prev = prev;
        }
    }
    return 0;
}

void isom_remove_entries( isom_entry_list_t *list, void* eliminator )
{
    if( !list )
        return;
    if( !eliminator )
        eliminator = free;
    for( isom_entry_t *entry = list->head; entry; )
    {
        isom_entry_t *next = entry->next;
        if( entry->data )
            ((isom_entry_data_eliminator)eliminator)( entry->data );
        free( entry );
        entry = next;
    }
    list->entry_count = 0;
    list->head = NULL;
    list->tail = NULL;
}

void isom_remove_list( isom_entry_list_t *list, void* eliminator )
{
    if( !list )
        return;
    isom_remove_entries( list, eliminator );
    free( list );
}

isom_entry_t *isom_get_entry( isom_entry_list_t *list, uint32_t entry_number )
{
    if( !list || !entry_number || entry_number > list->entry_count )
        return NULL;
    isom_entry_t *entry;
    for( entry = list->head; entry && --entry_number; entry = entry->next );
    return entry;
}

void *isom_get_entry_data( isom_entry_list_t *list, uint32_t entry_number )
{
    isom_entry_t *entry = isom_get_entry( list, entry_number );
    return entry ? entry->data : NULL;
}
