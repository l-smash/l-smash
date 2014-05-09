/*****************************************************************************
 * bstream.c
 *****************************************************************************
 * Copyright (C) 2010-2014 L-SMASH project
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

#include <string.h>
#include <limits.h>

#include "utils.h"

uint64_t lsmash_bs_get_pos( lsmash_bs_t *bs )
{
    return bs->buffer.pos;
}

void lsmash_bs_empty( lsmash_bs_t *bs )
{
    if( !bs )
        return;
    memset( bs->buffer.data, 0, bs->buffer.alloc );
    bs->buffer.store = 0;
    bs->buffer.pos   = 0;
}

void lsmash_bs_free( lsmash_bs_t *bs )
{
    if( bs->buffer.data )
        lsmash_free( bs->buffer.data );
    bs->buffer.data  = NULL;
    bs->buffer.alloc = 0;
    bs->buffer.store = 0;
    bs->buffer.pos   = 0;
}

void lsmash_bs_alloc( lsmash_bs_t *bs, uint64_t size )
{
    if( (bs->buffer.alloc >= size) || bs->error )
        return;
    uint64_t alloc = size + (1<<16);
    uint8_t *data;
    if( !bs->buffer.data )
        data = lsmash_malloc( alloc );
    else
        data = lsmash_realloc( bs->buffer.data, alloc );
    if( !data )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return;
    }
    bs->buffer.data  = data;
    bs->buffer.alloc = alloc;
}

int64_t lsmash_bs_seek( lsmash_bs_t *bs, int64_t offset, int whence )
{
    if( bs->unseekable )
        return -1;
    int64_t ret = bs->seek( bs->stream, offset, whence );
    if( ret < 0 )
        return ret;
    if( whence == SEEK_SET )
    {
        assert( offset >= 0 );
        if( bs->written < offset )
            bs->offset = bs->written;
        else
            bs->offset = offset;
    }
    else if( whence == SEEK_CUR )
    {
        if( offset < 0 && bs->offset < -offset )
            bs->offset = 0;
        else if( offset > 0 && bs->written < bs->offset + offset )
            bs->offset = bs->written;
        else
            bs->offset += offset;
    }
    else if( whence == SEEK_END )
    {
        assert( offset <= 0 );
        if( bs->written < -offset )
            bs->offset = 0;
        else
            bs->offset = bs->written + offset;
    }
    return ret;
}

/*---- bitstream writer ----*/
void lsmash_bs_put_byte( lsmash_bs_t *bs, uint8_t value )
{
    lsmash_bs_alloc( bs, bs->buffer.store + 1 );
    if( bs->error )
        return;
    bs->buffer.data[ bs->buffer.store ++ ] = value;
}

void lsmash_bs_put_bytes( lsmash_bs_t *bs, uint32_t size, void *value )
{
    if( !size || !value )
        return;
    lsmash_bs_alloc( bs, bs->buffer.store + size );
    if( bs->error )
        return;
    memcpy( bs->buffer.data + bs->buffer.store, value, size );
    bs->buffer.store += size;
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

void lsmash_bs_put_le16( lsmash_bs_t *bs, uint16_t value )
{
    lsmash_bs_put_byte( bs, (uint8_t)(value&0xff) );
    lsmash_bs_put_byte( bs, (uint8_t)((value>>8)&0xff) );
}

void lsmash_bs_put_le32( lsmash_bs_t *bs, uint32_t value )
{
    lsmash_bs_put_le16( bs, (uint16_t)(value&0xffff) );
    lsmash_bs_put_le16( bs, (uint16_t)((value>>16)&0xffff) );
}

int lsmash_bs_flush_buffer( lsmash_bs_t *bs )
{
    if( !bs )
        return -1;
    if( bs->buffer.store == 0
     || bs->buffer.data  == NULL )
        return 0;
    if( bs->error || !bs->stream
     || bs->write( bs->stream, bs->buffer.data, bs->buffer.store ) != bs->buffer.store )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return -1;
    }
    bs->written += bs->buffer.store;
    bs->offset  += bs->buffer.store;
    bs->buffer.store = 0;
    return 0;
}

size_t lsmash_bs_write_data( lsmash_bs_t *bs, uint8_t *buf, size_t size )
{
    if( !bs || size > INT_MAX )
        return -1;
    if( !buf || size == 0 )
        return 0;
    if( bs->error || !bs->stream )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return -1;
    }
    int write_size = bs->write( bs->stream, buf, size );
    bs->written += write_size;
    bs->offset  += write_size;
    return write_size != size ? -1 : 0;
}

lsmash_bs_t *lsmash_bs_create( void )
{
    lsmash_bs_t* bs = lsmash_malloc_zero( sizeof(lsmash_bs_t) );
    if( !bs )
        return NULL;
    bs->unseekable = 1;
    return bs;
}

void lsmash_bs_cleanup( lsmash_bs_t *bs )
{
    if( !bs )
        return;
    lsmash_bs_free( bs );
    lsmash_free( bs );
}

void *lsmash_bs_export_data( lsmash_bs_t *bs, uint32_t *length )
{
    if( !bs || !bs->buffer.data || bs->buffer.store == 0 || bs->error )
        return NULL;
    void *buf = lsmash_memdup( bs->buffer.data, bs->buffer.store );
    if( !buf )
        return NULL;
    if( length )
        *length = bs->buffer.store;
    return buf;
}
/*---- ----*/

/*---- bitstream reader ----*/
uint8_t lsmash_bs_show_byte( lsmash_bs_t *bs, uint32_t offset )
{
    if( bs->error || !bs->buffer.data )
        return 0;
    if( bs->buffer.pos + offset > bs->buffer.store )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return 0;
    }
    return bs->buffer.data[ bs->buffer.pos + offset ];
}

uint8_t lsmash_bs_get_byte( lsmash_bs_t *bs )
{
    if( bs->error || !bs->buffer.data )
        return 0;
    if( bs->buffer.pos + 1 > bs->buffer.store )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return 0;
    }
    return bs->buffer.data[ bs->buffer.pos ++ ];
}

void lsmash_bs_skip_bytes( lsmash_bs_t *bs, uint32_t size )
{
    if( bs->error || size == 0 )
        return;
    if( bs->buffer.pos + size > bs->buffer.store )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return;
    }
    bs->buffer.pos += size;
}

uint8_t *lsmash_bs_get_bytes( lsmash_bs_t *bs, uint32_t size )
{
    if( bs->error || size == 0 )
        return NULL;
    if( bs->buffer.pos + size > bs->buffer.store )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return NULL;
    }
    uint8_t *value = lsmash_memdup( bs->buffer.data + bs->buffer.pos, size );
    if( !value )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return NULL;
    }
    bs->buffer.pos += size;
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

int lsmash_bs_read( lsmash_bs_t *bs, uint32_t size )
{
    if( !bs || size > INT_MAX )
        return -1;
    if( size == 0 )
        return 0;
    lsmash_bs_alloc( bs, bs->buffer.store + size );
    if( bs->error || !bs->stream )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return -1;
    }
    int read_size = bs->read( bs->stream, bs->buffer.data + bs->buffer.store, size );
    if( read_size < 0 )
    {
        bs->error = 1;
        return -1;
    }
    bs->buffer.store += read_size;
    bs->offset       += read_size;
    return read_size;
}

int lsmash_bs_read_data( lsmash_bs_t *bs, uint8_t *buf, size_t *size )
{
    if( !bs || !size || *size > INT_MAX )
        return -1;
    if( !buf || *size == 0 )
        return 0;
    if( bs->error || !bs->stream )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return -1;
    }
    int read_size = bs->read( bs->stream, buf, *size );
    if( read_size < 0 )
    {
        bs->error = 1;
        return -1;
    }
    bs->offset += read_size;
    *size       = read_size;
    return 0;
}

int lsmash_bs_read_c( lsmash_bs_t *bs )
{
    if( !bs )
        return -1;
    if( bs->error || !bs->stream )
    {
        lsmash_bs_free( bs );
        bs->error = 1;
        return -1;
    }
    uint8_t c;
    int read_size = bs->read( bs->stream, &c, 1 );
    if( read_size != 1 )
    {
        if( read_size == 0 )
            return EOF;
        bs->error = 1;
        return -1;
    }
    bs->offset += 1;
    return c;
}

int lsmash_bs_import_data( lsmash_bs_t *bs, void* data, uint32_t length )
{
    if( !bs || bs->error || !data || length == 0 )
        return -1;
    lsmash_bs_alloc( bs, bs->buffer.store + length );
    if( bs->error || !bs->buffer.data ) /* means, failed to alloc. */
    {
        lsmash_bs_free( bs );
        return -1;
    }
    memcpy( bs->buffer.data + bs->buffer.store, data, length );
    bs->buffer.store += length;
    return 0;
}
/*---- ----*/

/*---- bitstream ----*/
void lsmash_bits_init( lsmash_bits_t *bits, lsmash_bs_t *bs )
{
    debug_if( !bits || !bs )
        return;
    bits->bs    = bs;
    bits->store = 0;
    bits->cache = 0;
}

lsmash_bits_t *lsmash_bits_create( lsmash_bs_t *bs )
{
    debug_if( !bs )
        return NULL;
    lsmash_bits_t *bits = (lsmash_bits_t *)lsmash_malloc( sizeof(lsmash_bits_t) );
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
   Otherwise, just lsmash_free() the bits struct. */
void lsmash_bits_cleanup( lsmash_bits_t *bits )
{
    debug_if( !bits )
        return;
    lsmash_free( bits );
}

/* we can change value's type to unsigned int for 64-bit operation if needed. */
static inline uint8_t lsmash_bits_mask_lsb8( uint32_t value, uint32_t width )
{
    return (uint8_t)( value & ~( ~0U << width ) );
}

void lsmash_bits_put( lsmash_bits_t *bits, uint32_t width, uint64_t value )
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

uint64_t lsmash_bits_get( lsmash_bits_t *bits, uint32_t width )
{
    debug_if( !bits || !width )
        return 0;
    uint64_t value = 0;
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
    lsmash_bs_t* bs = lsmash_bs_create();
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

lsmash_multiple_buffers_t *lsmash_create_multiple_buffers( uint32_t number_of_buffers, uint32_t buffer_size )
{
    if( (uint64_t)number_of_buffers * buffer_size > UINT32_MAX )
        return NULL;
    lsmash_multiple_buffers_t *multiple_buffer = lsmash_malloc( sizeof(lsmash_multiple_buffers_t) );
    if( !multiple_buffer )
        return NULL;
    multiple_buffer->buffers = lsmash_malloc( number_of_buffers * buffer_size );
    if( !multiple_buffer->buffers )
    {
        lsmash_free( multiple_buffer );
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
        temp = lsmash_realloc( multiple_buffer->buffers, multiple_buffer->number_of_buffers * buffer_size );
        if( !temp )
            return NULL;
        for( uint32_t i = multiple_buffer->number_of_buffers - 1; i ; i-- )
            memmove( temp + buffer_size, temp + i * multiple_buffer->buffer_size, multiple_buffer->buffer_size );
    }
    else
    {
        for( uint32_t i = 1; i < multiple_buffer->number_of_buffers; i++ )
            memmove( multiple_buffer->buffers + buffer_size, multiple_buffer->buffers + i * multiple_buffer->buffer_size, multiple_buffer->buffer_size );
        temp = lsmash_realloc( multiple_buffer->buffers, multiple_buffer->number_of_buffers * buffer_size );
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
        lsmash_free( multiple_buffer->buffers );
    lsmash_free( multiple_buffer );
}

void lsmash_stream_buffers_cleanup( lsmash_stream_buffers_t *sb )
{
    if( !sb )
        return;
    lsmash_destroy_multiple_buffers( sb->bank );
    sb->bank         = NULL;
    sb->start        = NULL;
    sb->end          = NULL;
    sb->pos          = NULL;
    sb->update       = NULL;
    sb->no_more_read = 0;
}

size_t lsmash_stream_buffers_update( lsmash_stream_buffers_t *sb, uint32_t anticipation_bytes )
{
    assert( sb && sb->update );
    return sb->update( sb, anticipation_bytes );
}

int lsmash_stream_buffers_is_eos( lsmash_stream_buffers_t *sb )
{
    assert( sb );
    return sb->no_more_read;
}

uint32_t lsmash_stream_buffers_get_buffer_size( lsmash_stream_buffers_t *sb )
{
    assert( sb );
    return sb->bank ? sb->bank->buffer_size : 0;
}

size_t lsmash_stream_buffers_get_valid_size( lsmash_stream_buffers_t *sb )
{
    assert( sb && sb->start && sb->end );
    return (uintptr_t)sb->end - (uintptr_t)sb->start;
}

uint8_t lsmash_stream_buffers_get_byte( lsmash_stream_buffers_t *sb )
{
    assert( sb && sb->pos );
    return *(sb->pos ++);
}

void lsmash_stream_buffers_seek( lsmash_stream_buffers_t *sb, intptr_t offset, int whence )
{
    assert( sb && sb->pos );
    if( whence == SEEK_SET )
    {
        assert( sb->start && offset >= 0 );
        sb->pos = sb->start + offset;
    }
    else if( whence == SEEK_CUR )
        sb->pos += offset;
    else if( whence == SEEK_END )
    {
        assert( sb->end && offset <= 0 );
        sb->pos = sb->end + offset;
    }
}

void lsmash_stream_buffers_set_pos( lsmash_stream_buffers_t *sb, uint8_t *pos )
{
    assert( sb && sb->pos );
    sb->pos = pos;
}

uint8_t *lsmash_stream_buffers_get_pos( lsmash_stream_buffers_t *sb )
{
    assert( sb && sb->pos );
    return sb->pos;
}

size_t lsmash_stream_buffers_get_offset( lsmash_stream_buffers_t *sb )
{
    assert( sb && sb->pos && sb->start );
    return (uintptr_t)sb->pos - (uintptr_t)sb->start;
}

size_t lsmash_stream_buffers_get_remainder( lsmash_stream_buffers_t *sb )
{
    assert( sb && sb->pos && sb->end );
    return sb->end > sb->pos ? (uintptr_t)sb->end - (uintptr_t)sb->pos : 0;
}

size_t lsmash_stream_buffers_read( lsmash_stream_buffers_t *sb, size_t read_size )
{
    assert( sb && sb->pos && sb->bank && sb->stream && sb->type == LSMASH_STREAM_BUFFERS_TYPE_FILE );
    if( read_size == 0 )
        read_size = sb->bank->buffer_size;
    size_t size = fread( sb->pos, 1, read_size, sb->stream );
    sb->end = sb->pos + size;
    sb->no_more_read = size == 0 ? feof( (FILE *)sb->stream ) : 0;
    return size;
}

void lsmash_data_string_copy( lsmash_stream_buffers_t *sb, lsmash_data_string_handler_t *dsh, size_t size, uint32_t pos )
{
    assert( sb && sb->pos && dsh && dsh->data );
    if( pos + size > dsh->data_length )
        size = dsh->data_length - pos;
    if( size > 0 )
        memcpy( sb->pos, dsh->data + pos, size );
    dsh->consumed_length  = pos + size;
    dsh->remainder_length = dsh->data_length - dsh->consumed_length;
    sb->end = sb->pos + size;
    sb->no_more_read = (dsh->remainder_length == 0);
}

void lsmash_stream_buffers_memcpy( uint8_t *data, lsmash_stream_buffers_t *sb, size_t size )
{
    assert( sb && sb->pos && sb->end );
    if( sb->pos + size > sb->end )
        size = sb->end - sb->pos;
    if( size == 0 )
        return;
    memcpy( data, sb->pos, size );
    sb->pos += size;
}

static size_t stream_buffers_update_file( lsmash_stream_buffers_t *sb, uint32_t anticipation_bytes )
{
    assert( anticipation_bytes < sb->bank->buffer_size );
    uint32_t remainder_bytes = sb->end - sb->pos;
    if( sb->no_more_read || remainder_bytes > anticipation_bytes )
        return remainder_bytes;
    if( sb->start != sb->pos )
        /* Move unused data to the head of buffer. */
        memmove( sb->start, sb->pos, remainder_bytes );
    /* Read and store the next data into the buffer.
     * Move the position of buffer on the head. */
    lsmash_stream_buffers_seek( sb, remainder_bytes, SEEK_SET );
    size_t read_size = lsmash_stream_buffers_read( sb, sb->bank->buffer_size - remainder_bytes );
    lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
    sb->no_more_read = read_size == 0 ? feof( (FILE *)sb->stream ) : 0;
    return lsmash_stream_buffers_get_remainder( sb );
}

static size_t stream_buffers_update_data_string( lsmash_stream_buffers_t *sb, uint32_t anticipation_bytes )
{
    assert( anticipation_bytes < sb->bank->buffer_size );
    uint32_t remainder_bytes = sb->end - sb->pos;
    if( sb->no_more_read || remainder_bytes > anticipation_bytes )
        return remainder_bytes;
    if( sb->start != sb->pos )
        /* Move unused data to the head of buffer. */
        memmove( sb->start, sb->pos, remainder_bytes );
    /* Read and store the next data into the buffer.
     * Move the position of buffer on the head. */
    lsmash_data_string_handler_t *dsh = (lsmash_data_string_handler_t *)sb->stream;
    uint32_t consumed_data_length = LSMASH_MIN( dsh->remainder_length, sb->bank->buffer_size - remainder_bytes );
    lsmash_stream_buffers_seek( sb, remainder_bytes, SEEK_SET );
    lsmash_data_string_copy( sb, dsh, consumed_data_length, dsh->consumed_length );
    lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
    sb->no_more_read = (dsh->remainder_length == 0);
    return lsmash_stream_buffers_get_remainder( sb );
}

void lsmash_stream_buffers_setup( lsmash_stream_buffers_t *sb, lsmash_stream_buffers_type type, void *stream )
{
    assert( sb );
    sb->type   = type;
    sb->stream = stream;
    if( type == LSMASH_STREAM_BUFFERS_TYPE_FILE )
        sb->update = stream_buffers_update_file;
    else if( type == LSMASH_STREAM_BUFFERS_TYPE_DATA_STRING )
        sb->update = stream_buffers_update_data_string;
}
