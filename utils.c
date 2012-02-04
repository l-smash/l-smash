/*****************************************************************************
 * utils.c:
 *****************************************************************************
 * Copyright (C) 2010-2012 L-SMASH project
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

#include "internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

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
    if( (bs->alloc >= size) || bs->error )
        return;
    uint64_t alloc = size + (1<<16);
    uint8_t *data;
    if( !bs->data )
        data = malloc( alloc );
    else
        data = realloc( bs->data, alloc );
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

void lsmash_bs_put_bytes( lsmash_bs_t *bs, uint32_t size, void *value )
{
    if( !size || !value )
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

void lsmash_bs_put_byte_from_64( lsmash_bs_t *bs, uint64_t value )
{
    lsmash_bs_put_byte( bs, (uint8_t)(value&0xff) );
}

void lsmash_bs_put_be16_from_64( lsmash_bs_t *bs, uint64_t value )
{
    lsmash_bs_put_be16( bs, (uint16_t)(value&0xffff) );
}

void lsmash_bs_put_be24_from_64( lsmash_bs_t *bs, uint64_t value )
{
    lsmash_bs_put_be24( bs, (uint32_t)(value&0xffffff) );
}

void lsmash_bs_put_be32_from_64( lsmash_bs_t *bs, uint64_t value )
{
    lsmash_bs_put_be32( bs, (uint32_t)(value&0xffffffff) );
}

int lsmash_bs_write_data( lsmash_bs_t *bs )
{
    if( !bs )
        return -1;
    if( !bs->store || !bs->data )
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
    lsmash_bs_t* bs = lsmash_malloc_zero( sizeof(lsmash_bs_t) );
    if( !bs )
        return NULL;
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
    void *buf = lsmash_memdup( bs->data, bs->store );
    if( !buf )
        return NULL;
    if( length )
        *length = bs->store;
    return buf;
}
/*---- ----*/

/*---- bitstream reader ----*/
uint8_t lsmash_bs_show_byte( lsmash_bs_t *bs, uint32_t offset )
{
    if( bs->error || !bs->data )
        return 0;
    if( bs->pos + offset > bs->store )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return 0;
    }
    return bs->data[bs->pos + offset];
}

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
    if( bs->pos + size > bs->store )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return NULL;
    }
    uint8_t *value = lsmash_memdup( bs->data + bs->pos, size );
    if( !value )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return NULL;
    }
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
    uint32_t     value = lsmash_bs_get_byte( bs );
    return (value<<16) | lsmash_bs_get_be16( bs );
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

uint64_t lsmash_bs_get_byte_to_64( lsmash_bs_t *bs )
{
    return lsmash_bs_get_byte( bs );
}

uint64_t lsmash_bs_get_be16_to_64( lsmash_bs_t *bs )
{
    return lsmash_bs_get_be16( bs );
}

uint64_t lsmash_bs_get_be24_to_64( lsmash_bs_t *bs )
{
    return lsmash_bs_get_be24( bs );
}

uint64_t lsmash_bs_get_be32_to_64( lsmash_bs_t *bs )
{
    return lsmash_bs_get_be32( bs );
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
void lsmash_bits_init( lsmash_bits_t *bits, lsmash_bs_t *bs )
{
    debug_if( !bits || !bs )
        return;
    bits->bs = bs;
    bits->store = 0;
    bits->cache = 0;
}

lsmash_bits_t *lsmash_bits_create( lsmash_bs_t *bs )
{
    debug_if( !bs )
        return NULL;
    lsmash_bits_t *bits = (lsmash_bits_t *)malloc( sizeof(lsmash_bits_t) );
    if( !bits )
        return NULL;
    lsmash_bits_init( bits, bs );
    return bits;
}

void lsmash_bits_empty( lsmash_bits_t *bits )
{
    debug_if( !bits )
        return;
    lsmash_bs_empty( bits->bs );
    bits->store = 0;
    bits->cache = 0;
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
void lsmash_bits_put( lsmash_bits_t *bits, uint32_t width, uint32_t value )
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
void lsmash_init_entry_list( lsmash_entry_list_t *list )
{
    list->head = NULL;
    list->tail = NULL;
    list->last_accessed_entry = NULL;
    list->last_accessed_number = 0;
    list->entry_count = 0;
}

lsmash_entry_list_t *lsmash_create_entry_list( void )
{
    lsmash_entry_list_t *list = malloc( sizeof(lsmash_entry_list_t) );
    if( !list )
        return NULL;
    lsmash_init_entry_list( list );
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
    if( entry == list->last_accessed_entry )
    {
        if( next )
            list->last_accessed_entry = next;
        else if( prev )
        {
            list->last_accessed_entry = prev;
            list->last_accessed_number -= 1;
        }
        else
        {
            list->last_accessed_entry = NULL;
            list->last_accessed_number = 0;
        }
    }
    else
    {
        /* We can't know the current entry number immediately,
         * so discard the last accessed entry info because time is wasted to know it. */
        list->last_accessed_entry = NULL;
        list->last_accessed_number = 0;
    }
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
    lsmash_init_entry_list( list );
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
    int shortcut = 1;
    lsmash_entry_t *entry = NULL;
    if( list->last_accessed_entry )
    {
        if( entry_number == list->last_accessed_number )
            entry = list->last_accessed_entry;
        else if( entry_number == list->last_accessed_number + 1 )
            entry = list->last_accessed_entry->next;
        else if( entry_number == list->last_accessed_number - 1 )
            entry = list->last_accessed_entry->prev;
        else
            shortcut = 0;
    }
    else
        shortcut = 0;
    if( !shortcut )
    {
        if( entry_number <= (list->entry_count >> 1) )
        {
            /* Look for from the head. */
            uint32_t distance_plus_one = entry_number;
            for( entry = list->head; entry && --distance_plus_one; entry = entry->next );
        }
        else
        {
            /* Look for from the tail. */
            uint32_t distance = list->entry_count - entry_number;
            for( entry = list->tail; entry && distance--; entry = entry->prev );
        }
    }
    if( entry )
    {
        list->last_accessed_entry = entry;
        list->last_accessed_number = entry_number;
    }
    return entry;
}

void *lsmash_get_entry_data( lsmash_entry_list_t *list, uint32_t entry_number )
{
    lsmash_entry_t *entry = lsmash_get_entry( list, entry_number );
    return entry ? entry->data : NULL;
}
/*---- ----*/

/*---- type ----*/
double lsmash_fixed2double( uint64_t value, int frac_width )
{
    return value / (double)(1ULL << frac_width);
}

float lsmash_int2float32( uint32_t value )
{
    return (union {uint32_t i; float f;}){value}.f;
}

double lsmash_int2float64( uint64_t value )
{
    return (union {uint64_t i; double d;}){value}.d;
}
/*---- ----*/

/*---- allocator ----*/
void *lsmash_malloc_zero( size_t size )
{
    if( !size )
        return NULL;
    void *p = malloc( size );
    if( !p )
        return NULL;
    memset( p, 0, size );
    return p;
}

void *lsmash_memdup( void *src, size_t size )
{
    if( !size )
        return NULL;
    void *dst = malloc( size );
    if( !dst )
        return NULL;
    memcpy( dst, src, size );
    return dst;
}

lsmash_multiple_buffers_t *lsmash_create_multiple_buffers( uint32_t number_of_buffers, uint32_t buffer_size )
{
    if( (uint64_t)number_of_buffers * buffer_size > UINT32_MAX )
        return NULL;
    lsmash_multiple_buffers_t *multiple_buffer = malloc( sizeof(lsmash_multiple_buffers_t) );
    if( !multiple_buffer )
        return NULL;
    multiple_buffer->buffers = malloc( number_of_buffers * buffer_size );
    if( !multiple_buffer->buffers )
    {
        free( multiple_buffer );
        return NULL;
    }
    multiple_buffer->number_of_buffers = number_of_buffers;
    multiple_buffer->buffer_size = buffer_size;
    return multiple_buffer;
}

void *lsmash_withdraw_buffer( lsmash_multiple_buffers_t *multiple_buffer, uint32_t buffer_number )
{
    if( !multiple_buffer || !buffer_number || buffer_number > multiple_buffer->number_of_buffers )
        return NULL;
    return multiple_buffer->buffers + (buffer_number - 1) * multiple_buffer->buffer_size;
}

lsmash_multiple_buffers_t *lsmash_resize_multiple_buffers( lsmash_multiple_buffers_t *multiple_buffer, uint32_t buffer_size )
{
    if( !multiple_buffer )
        return NULL;
    if( buffer_size == multiple_buffer->buffer_size )
        return multiple_buffer;
    if( (uint64_t)multiple_buffer->number_of_buffers * buffer_size > UINT32_MAX )
        return NULL;
    void *temp;
    if( buffer_size > multiple_buffer->buffer_size )
    {
        temp = realloc( multiple_buffer->buffers, multiple_buffer->number_of_buffers * buffer_size );
        if( !temp )
            return NULL;
        for( uint32_t i = multiple_buffer->number_of_buffers - 1; i ; i-- )
            memmove( temp + buffer_size, temp + i * multiple_buffer->buffer_size, multiple_buffer->buffer_size );
    }
    else
    {
        for( uint32_t i = 1; i < multiple_buffer->number_of_buffers; i++ )
            memmove( multiple_buffer->buffers + buffer_size, multiple_buffer->buffers + i * multiple_buffer->buffer_size, multiple_buffer->buffer_size );
        temp = realloc( multiple_buffer->buffers, multiple_buffer->number_of_buffers * buffer_size );
        if( !temp )
            return NULL;
    }
    multiple_buffer->buffers = temp;
    multiple_buffer->buffer_size = buffer_size;
    return multiple_buffer;
}

void lsmash_destroy_multiple_buffers( lsmash_multiple_buffers_t *multiple_buffer )
{
    if( !multiple_buffer )
        return;
    if( multiple_buffer->buffers )
        free( multiple_buffer->buffers );
    free( multiple_buffer );
}
/*---- ----*/

/*---- others ----*/
void lsmash_log( lsmash_log_level level, const char* message, ... )
{
    char *prefix;
    va_list args;

    va_start( args, message );
    switch( level )
    {
        case LSMASH_LOG_ERROR:
            prefix = "Error";
            break;
        case LSMASH_LOG_WARNING:
            prefix = "Warning";
            break;
        case LSMASH_LOG_INFO:
            prefix = "Info";
            break;
        default:
            prefix = "Unknown";
            break;
    }

    fprintf( stderr, "[%s]: ", prefix );
    vfprintf( stderr, message, args );
    va_end( args );
}

uint32_t lsmash_count_bits( uint32_t bits )
{
    bits = (bits & 0x55555555) + ((bits >>  1) & 0x55555555);
    bits = (bits & 0x33333333) + ((bits >>  2) & 0x33333333);
    bits = (bits & 0x0f0f0f0f) + ((bits >>  4) & 0x0f0f0f0f);
    bits = (bits & 0x00ff00ff) + ((bits >>  8) & 0x00ff00ff);
    return (bits & 0x0000ffff) + ((bits >> 16) & 0x0000ffff);
}

/* for qsort function */
int lsmash_compare_dts( const lsmash_media_ts_t *a, const lsmash_media_ts_t *b )
{
    int64_t diff = (int64_t)(a->dts - b->dts);
    return diff > 0 ? 1 : (diff == 0 ? 0 : -1);
}

int lsmash_compare_cts( const lsmash_media_ts_t *a, const lsmash_media_ts_t *b )
{
    int64_t diff = (int64_t)(a->cts - b->cts);
    return diff > 0 ? 1 : (diff == 0 ? 0 : -1);
}
