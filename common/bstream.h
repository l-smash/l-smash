/*****************************************************************************
 * bstream.h
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

/*---- bytestream ----*/
#define BS_MAX_DEFAULT_READ_SIZE (4 * 1024 * 1024)

typedef struct
{
    int      unseekable;    /* If set to 1, the buffer is unseekable. */
    int      internal;      /* If set to 1, the buffer is allocated on heap internally.
                             * The pointer to the buffer shall not be changed by any method other than internal allocation. */
    uint8_t *data;          /* the pointer to the buffer for reading/writing */
    size_t   store;         /* valid data size on the buffer */
    size_t   alloc;         /* total buffer size including invalid area */
    size_t   pos;           /* the data position on the buffer to be read next */
    size_t   max_size;      /* the maximum number of bytes for reading from the stream at one time */
    uint64_t count;         /* counter for arbitrary usage */
} lsmash_buffer_t;

typedef struct
{
    void           *stream;         /* I/O stream */
    uint8_t         eof;            /* If set to 1, the stream reached EOF. */
    uint8_t         eob;            /* if set to 1, we cannot read more bytes from the stream and the buffer until any seek. */
    uint8_t         error;          /* If set to 1, any error is detected. */
    uint8_t         unseekable;     /* If set to 1, the stream is unseekable. */
    uint64_t        written;        /* the number of bytes written into 'stream' already */
    uint64_t        offset;         /* the current position in the 'stream'
                                     * the number of bytes from the beginning */
    lsmash_buffer_t buffer;
    int     (*read) ( void *opaque, uint8_t *buf, int size );
    int     (*write)( void *opaque, uint8_t *buf, int size );
    int64_t (*seek) ( void *opaque, int64_t offset, int whence );
} lsmash_bs_t;

static inline void lsmash_bs_reset_counter( lsmash_bs_t *bs )
{
    bs->buffer.count = 0;
}

static inline uint64_t lsmash_bs_count( lsmash_bs_t *bs )
{
    return bs->buffer.count;
}

static inline uint64_t lsmash_bs_get_remaining_buffer_size( lsmash_bs_t *bs )
{
    assert( bs->buffer.store >= bs->buffer.pos );
    return bs->buffer.store - bs->buffer.pos;
}

static inline uint8_t *lsmash_bs_get_buffer_data( lsmash_bs_t *bs )
{
    return bs->buffer.data + (uintptr_t)bs->buffer.pos;
}

static inline uint8_t *lsmash_bs_get_buffer_data_start( lsmash_bs_t *bs )
{
    return bs->buffer.data;
}

static inline uint8_t *lsmash_bs_get_buffer_data_end( lsmash_bs_t *bs )
{
    return bs->buffer.data + (uintptr_t)bs->buffer.store;
}

static inline uint64_t lsmash_bs_get_pos( lsmash_bs_t *bs )
{
    return bs->buffer.pos;
}

static inline uint64_t lsmash_bs_get_stream_pos( lsmash_bs_t *bs )
{
    assert( bs->buffer.store <= bs->offset );
    return bs->offset - lsmash_bs_get_remaining_buffer_size( bs );
}

static inline size_t lsmash_bs_get_valid_data_size( lsmash_bs_t *bs )
{
    return bs->buffer.store;
}

lsmash_bs_t *lsmash_bs_create( void );
void lsmash_bs_cleanup( lsmash_bs_t *bs );
int lsmash_bs_set_empty_stream( lsmash_bs_t *bs, uint8_t *data, size_t size );
void lsmash_bs_empty( lsmash_bs_t *bs );
int64_t lsmash_bs_write_seek( lsmash_bs_t *bs, int64_t offset, int whence );
int64_t lsmash_bs_read_seek( lsmash_bs_t *bs, int64_t offset, int whence );

/*---- bytestream writer ----*/
void lsmash_bs_put_byte( lsmash_bs_t *bs, uint8_t value );
void lsmash_bs_put_bytes( lsmash_bs_t *bs, uint32_t size, void *value );
void lsmash_bs_put_be16( lsmash_bs_t *bs, uint16_t value );
void lsmash_bs_put_be24( lsmash_bs_t *bs, uint32_t value );
void lsmash_bs_put_be32( lsmash_bs_t *bs, uint32_t value );
void lsmash_bs_put_be64( lsmash_bs_t *bs, uint64_t value );
void lsmash_bs_put_byte_from_64( lsmash_bs_t *bs, uint64_t value );
void lsmash_bs_put_be16_from_64( lsmash_bs_t *bs, uint64_t value );
void lsmash_bs_put_be24_from_64( lsmash_bs_t *bs, uint64_t value );
void lsmash_bs_put_be32_from_64( lsmash_bs_t *bs, uint64_t value );
void lsmash_bs_put_le16( lsmash_bs_t *bs, uint16_t value );
void lsmash_bs_put_le32( lsmash_bs_t *bs, uint32_t value );
int lsmash_bs_flush_buffer( lsmash_bs_t *bs );
int lsmash_bs_write_data( lsmash_bs_t *bs, uint8_t *buf, size_t size );
void *lsmash_bs_export_data( lsmash_bs_t *bs, uint32_t *length );

/*---- bytestream reader ----*/
uint8_t lsmash_bs_show_byte( lsmash_bs_t *bs, uint32_t offset );
uint16_t lsmash_bs_show_be16( lsmash_bs_t *bs, uint32_t offset );
uint32_t lsmash_bs_show_be24( lsmash_bs_t *bs, uint32_t offset );
uint32_t lsmash_bs_show_be32( lsmash_bs_t *bs, uint32_t offset );
uint64_t lsmash_bs_show_be64( lsmash_bs_t *bs, uint32_t offset );
uint8_t lsmash_bs_get_byte( lsmash_bs_t *bs );
void lsmash_bs_skip_bytes( lsmash_bs_t *bs, uint32_t size );
void lsmash_bs_skip_bytes_64( lsmash_bs_t *bs, uint64_t size );
uint8_t *lsmash_bs_get_bytes( lsmash_bs_t *bs, uint32_t size );
int64_t lsmash_bs_get_bytes_ex( lsmash_bs_t *bs, uint32_t size, uint8_t *value );
uint16_t lsmash_bs_get_be16( lsmash_bs_t *bs );
uint32_t lsmash_bs_get_be24( lsmash_bs_t *bs );
uint32_t lsmash_bs_get_be32( lsmash_bs_t *bs );
uint64_t lsmash_bs_get_be64( lsmash_bs_t *bs );
uint64_t lsmash_bs_get_byte_to_64( lsmash_bs_t *bs );
uint64_t lsmash_bs_get_be16_to_64( lsmash_bs_t *bs );
uint64_t lsmash_bs_get_be24_to_64( lsmash_bs_t *bs );
uint64_t lsmash_bs_get_be32_to_64( lsmash_bs_t *bs );
uint16_t lsmash_bs_get_le16( lsmash_bs_t *bs );
uint32_t lsmash_bs_get_le32( lsmash_bs_t *bs );
int lsmash_bs_read( lsmash_bs_t *bs, uint32_t size );
int lsmash_bs_read_data( lsmash_bs_t *bs, uint8_t *buf, size_t *size );
int lsmash_bs_import_data( lsmash_bs_t *bs, void *data, uint32_t length );

/* Check if the given offset reaches both EOF of the stream and the end of the buffer. */
static inline int lsmash_bs_is_end( lsmash_bs_t *bs, uint32_t offset )
{
    lsmash_bs_show_byte( bs, offset );
    return bs->eof && (offset >= lsmash_bs_get_remaining_buffer_size( bs ));
}

/*---- basic I/O ----*/
int lsmash_fread_wrapper( void *opaque, uint8_t *buf, int size );
int lsmash_fwrite_wrapper( void *opaque, uint8_t *buf, int size );
int64_t lsmash_fseek_wrapper( void *opaque, int64_t offset, int whence );

/*---- memory writers ----*/
#define LSMASH_SET_BYTE( p, x )          \
    do                                   \
    {                                    \
        ((uint8_t *)(p))[0] = (x);       \
    } while( 0 )
#define LSMASH_SET_BE16( p, x )          \
    do                                   \
    {                                    \
        ((uint8_t *)(p))[0] = (x) >> 8;  \
        ((uint8_t *)(p))[1] = (x);       \
    } while( 0 )
#define LSMASH_SET_BE24( p, x )          \
    do                                   \
    {                                    \
        ((uint8_t *)(p))[0] = (x) >> 16; \
        ((uint8_t *)(p))[1] = (x) >>  8; \
        ((uint8_t *)(p))[2] = (x);       \
    } while( 0 )
#define LSMASH_SET_BE32( p, x )          \
    do                                   \
    {                                    \
        ((uint8_t *)(p))[0] = (x) >> 24; \
        ((uint8_t *)(p))[1] = (x) >> 16; \
        ((uint8_t *)(p))[2] = (x) >>  8; \
        ((uint8_t *)(p))[3] = (x);       \
    } while( 0 )
#define LSMASH_SET_BE64( p, x )          \
    do                                   \
    {                                    \
        ((uint8_t *)(p))[0] = (x) >> 56; \
        ((uint8_t *)(p))[1] = (x) >> 48; \
        ((uint8_t *)(p))[2] = (x) >> 40; \
        ((uint8_t *)(p))[3] = (x) >> 32; \
        ((uint8_t *)(p))[4] = (x) >> 24; \
        ((uint8_t *)(p))[5] = (x) >> 16; \
        ((uint8_t *)(p))[6] = (x) >>  8; \
        ((uint8_t *)(p))[7] = (x);       \
    } while( 0 )
#define LSMASH_SET_LE16( p, x )          \
    do                                   \
    {                                    \
        ((uint8_t *)(p))[0] = (x);       \
        ((uint8_t *)(p))[1] = (x) >> 8;  \
    } while( 0 )
#define LSMASH_SET_LE32( p, x )          \
    do                                   \
    {                                    \
        ((uint8_t *)(p))[0] = (x);       \
        ((uint8_t *)(p))[1] = (x) >>  8; \
        ((uint8_t *)(p))[2] = (x) >> 16; \
        ((uint8_t *)(p))[3] = (x) >> 24; \
    } while( 0 )

/*---- memory readers ----*/
#define LSMASH_GET_BYTE( p )                      \
    (((const uint8_t *)(p))[0])
#define LSMASH_GET_BE16( p )                      \
     (((uint16_t)((const uint8_t *)(p))[0] << 8)  \
    | ((uint16_t)((const uint8_t *)(p))[1]))
#define LSMASH_GET_BE24( p )                      \
     (((uint32_t)((const uint8_t *)(p))[0] << 16) \
    | ((uint32_t)((const uint8_t *)(p))[1] <<  8) \
    | ((uint32_t)((const uint8_t *)(p))[2]))
#define LSMASH_GET_BE32( p )                      \
     (((uint32_t)((const uint8_t *)(p))[0] << 24) \
    | ((uint32_t)((const uint8_t *)(p))[1] << 16) \
    | ((uint32_t)((const uint8_t *)(p))[2] <<  8) \
    | ((uint32_t)((const uint8_t *)(p))[3]))
#define LSMASH_GET_BE64( p )                      \
     (((uint64_t)((const uint8_t *)(p))[0] << 56) \
    | ((uint64_t)((const uint8_t *)(p))[1] << 48) \
    | ((uint64_t)((const uint8_t *)(p))[2] << 40) \
    | ((uint64_t)((const uint8_t *)(p))[3] << 32) \
    | ((uint64_t)((const uint8_t *)(p))[4] << 24) \
    | ((uint64_t)((const uint8_t *)(p))[5] << 16) \
    | ((uint64_t)((const uint8_t *)(p))[6] <<  8) \
    | ((uint64_t)((const uint8_t *)(p))[7]))
#define LSMASH_GET_LE16( p )                      \
     (((uint16_t)((const uint8_t *)(p))[0])       \
    | ((uint16_t)((const uint8_t *)(p))[1] << 8))
#define LSMASH_GET_LE32( p )                      \
     (((uint32_t)((const uint8_t *)(p))[0])       \
    | ((uint32_t)((const uint8_t *)(p))[1] <<  8) \
    | ((uint32_t)((const uint8_t *)(p))[2] << 16) \
    | ((uint32_t)((const uint8_t *)(p))[3] << 24))
