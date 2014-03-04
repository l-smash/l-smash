/*****************************************************************************
 * utils.h:
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

#ifndef LSMASH_UTIL_H
#define LSMASH_UTIL_H

#define debug_if(x) if(x)

#define LSMASH_MAX( a, b ) ((a) > (b) ? (a) : (b))
#define LSMASH_MIN( a, b ) ((a) < (b) ? (a) : (b))

typedef struct
{
    char *name;
} lsmash_class_t;

/*---- bytestream ----*/
typedef struct
{
    uint8_t *data;      /* buffer for reading/writing */
    uint64_t store;     /* valid data size on buffer */
    uint64_t alloc;     /* total buffer size including invalid area */
    uint64_t pos;       /* data position on buffer to be read next */
} lsmash_buffer_t;

typedef struct
{
    FILE           *stream;     /* I/O stream */
    uint8_t         error;
    uint8_t         unseekable;
    uint64_t        written;    /* the number of bytes written into 'stream' already */
    uint64_t        offset;     /* the current position in the 'stream'
                                 * the number of bytes from the beginning */
    lsmash_buffer_t buffer;
} lsmash_bs_t;

uint64_t lsmash_bs_get_pos( lsmash_bs_t *bs );
void lsmash_bs_empty( lsmash_bs_t *bs );
void lsmash_bs_free( lsmash_bs_t *bs );
void lsmash_bs_alloc( lsmash_bs_t *bs, uint64_t size );
lsmash_bs_t *lsmash_bs_create( char *filename );
void lsmash_bs_cleanup( lsmash_bs_t *bs );
int lsmash_bs_seek( lsmash_bs_t *bs, int64_t offset, int whence );

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
int lsmash_bs_flush_buffer( lsmash_bs_t *bs );
size_t lsmash_bs_write_data( lsmash_bs_t *bs, uint8_t *buf, size_t size );
void *lsmash_bs_export_data( lsmash_bs_t *bs, uint32_t *length );

/*---- bytestream reader ----*/
uint8_t lsmash_bs_show_byte( lsmash_bs_t *bs, uint32_t offset );
uint8_t lsmash_bs_get_byte( lsmash_bs_t *bs );
void lsmash_bs_skip_bytes( lsmash_bs_t *bs, uint32_t size );
uint8_t *lsmash_bs_get_bytes( lsmash_bs_t *bs, uint32_t size );
uint16_t lsmash_bs_get_be16( lsmash_bs_t *bs );
uint32_t lsmash_bs_get_be24( lsmash_bs_t *bs );
uint32_t lsmash_bs_get_be32( lsmash_bs_t *bs );
uint64_t lsmash_bs_get_be64( lsmash_bs_t *bs );
uint64_t lsmash_bs_get_byte_to_64( lsmash_bs_t *bs );
uint64_t lsmash_bs_get_be16_to_64( lsmash_bs_t *bs );
uint64_t lsmash_bs_get_be24_to_64( lsmash_bs_t *bs );
uint64_t lsmash_bs_get_be32_to_64( lsmash_bs_t *bs );
int lsmash_bs_read( lsmash_bs_t *bs, uint32_t size );
int lsmash_bs_read_data( lsmash_bs_t *bs, uint8_t *buf, size_t *size );
int lsmash_bs_import_data( lsmash_bs_t *bs, void *data, uint32_t length );

/*---- bitstream ----*/
typedef struct {
    lsmash_bs_t* bs;
    uint8_t store;
    uint8_t cache;
} lsmash_bits_t;

void lsmash_bits_init( lsmash_bits_t* bits, lsmash_bs_t *bs );
lsmash_bits_t *lsmash_bits_create( lsmash_bs_t *bs );
void lsmash_bits_empty( lsmash_bits_t *bits );
void lsmash_bits_put_align( lsmash_bits_t *bits );
void lsmash_bits_get_align( lsmash_bits_t *bits );
void lsmash_bits_cleanup( lsmash_bits_t *bits );

/*---- bitstream writer ----*/
void lsmash_bits_put( lsmash_bits_t *bits, uint32_t width, uint64_t value );
uint64_t lsmash_bits_get( lsmash_bits_t *bits, uint32_t width );
lsmash_bits_t *lsmash_bits_adhoc_create();
void lsmash_bits_adhoc_cleanup( lsmash_bits_t *bits );
void* lsmash_bits_export_data( lsmash_bits_t *bits, uint32_t *length );
int lsmash_bits_import_data( lsmash_bits_t *bits, void *data, uint32_t length );

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
    lsmash_entry_t *head;
    lsmash_entry_t *tail;
    lsmash_entry_t *last_accessed_entry;
    uint32_t last_accessed_number;
    uint32_t entry_count;
} lsmash_entry_list_t;

typedef void (*lsmash_entry_data_eliminator)(void *data); /* very same as free() of standard c lib; void free(void *); */

void lsmash_init_entry_list( lsmash_entry_list_t *list );
lsmash_entry_list_t *lsmash_create_entry_list( void );
int lsmash_add_entry( lsmash_entry_list_t *list, void *data );
int lsmash_remove_entry_direct( lsmash_entry_list_t *list, lsmash_entry_t *entry, void *eliminator );
int lsmash_remove_entry( lsmash_entry_list_t *list, uint32_t entry_number, void *eliminator );
int lsmash_remove_entry_tail( lsmash_entry_list_t *list, void *eliminator );
void lsmash_remove_entries( lsmash_entry_list_t *list, void *eliminator );
void lsmash_remove_list( lsmash_entry_list_t *list, void *eliminator );

lsmash_entry_t *lsmash_get_entry( lsmash_entry_list_t *list, uint32_t entry_number );
void *lsmash_get_entry_data( lsmash_entry_list_t *list, uint32_t entry_number );

/*---- type ----*/
double lsmash_fixed2double( uint64_t value, int frac_width );
float lsmash_int2float32( uint32_t value );
double lsmash_int2float64( uint64_t value );

/*---- allocator ----*/
typedef struct
{
    uint32_t number_of_buffers;
    uint32_t buffer_size;
    void    *buffers;
} lsmash_multiple_buffers_t;

lsmash_multiple_buffers_t *lsmash_create_multiple_buffers( uint32_t number_of_buffers, uint32_t buffer_size );
void *lsmash_withdraw_buffer( lsmash_multiple_buffers_t *multiple_buffer, uint32_t buffer_number );
lsmash_multiple_buffers_t *lsmash_resize_multiple_buffers( lsmash_multiple_buffers_t *multiple_buffer, uint32_t buffer_size );
void lsmash_destroy_multiple_buffers( lsmash_multiple_buffers_t *multiple_buffer );

typedef enum
{
    LSMASH_STREAM_BUFFERS_TYPE_NONE = 0,
    LSMASH_STREAM_BUFFERS_TYPE_FILE,            /* -> FILE */
    LSMASH_STREAM_BUFFERS_TYPE_DATA_STRING,     /* -> lsmash_data_string_handler_t */
} lsmash_stream_buffers_type;

typedef struct lsmash_stream_buffers_tag lsmash_stream_buffers_t;
struct lsmash_stream_buffers_tag
{
    lsmash_stream_buffers_type type;
    void                      *stream;
    lsmash_multiple_buffers_t *bank;
    uint8_t                   *start;
    uint8_t                   *end;
    uint8_t                   *pos;
    size_t (*update)( lsmash_stream_buffers_t *, uint32_t );
    int                        no_more_read;
};

typedef struct
{
    uint8_t *data;
    uint32_t data_length;
    uint32_t remainder_length;
    uint32_t consumed_length;   /* overall consumed length */
} lsmash_data_string_handler_t;

void lsmash_stream_buffers_setup( lsmash_stream_buffers_t *sb, lsmash_stream_buffers_type type, void *stream );
void lsmash_stream_buffers_cleanup( lsmash_stream_buffers_t *sb );  /* 'type' and 'stream' are not touched. */
size_t lsmash_stream_buffers_update( lsmash_stream_buffers_t *sb, uint32_t anticipation_bytes );
int lsmash_stream_buffers_is_eos( lsmash_stream_buffers_t *sb );
uint32_t lsmash_stream_buffers_get_buffer_size( lsmash_stream_buffers_t *sb );
size_t lsmash_stream_buffers_get_valid_size( lsmash_stream_buffers_t *sb );
uint8_t lsmash_stream_buffers_get_byte( lsmash_stream_buffers_t *sb );
void lsmash_stream_buffers_seek( lsmash_stream_buffers_t *sb, intptr_t offset, int whence );
void lsmash_stream_buffers_set_pos( lsmash_stream_buffers_t *sb, uint8_t *pos );
uint8_t *lsmash_stream_buffers_get_pos( lsmash_stream_buffers_t *sb );
size_t lsmash_stream_buffers_get_offset( lsmash_stream_buffers_t *sb );
size_t lsmash_stream_buffers_get_remainder( lsmash_stream_buffers_t *sb );
size_t lsmash_stream_buffers_read( lsmash_stream_buffers_t *sb, size_t read_size );
void lsmash_stream_buffers_memcpy( uint8_t *data, lsmash_stream_buffers_t *sb, size_t size );
void lsmash_data_string_copy( lsmash_stream_buffers_t *sb, lsmash_data_string_handler_t *dsh, size_t size, uint32_t pos );

/*---- others ----*/
typedef enum
{
    LSMASH_LOG_ERROR,
    LSMASH_LOG_WARNING,
    LSMASH_LOG_INFO,
} lsmash_log_level;

typedef struct
{
    uint64_t n;
    uint64_t d;
} lsmash_rational_u64_t;

typedef struct
{
    int64_t  n;
    uint64_t d;
} lsmash_rational_s64_t;

void lsmash_log
(
    void            *hp,
    lsmash_log_level level,
    const char      *message,
    ...
);

uint32_t lsmash_count_bits
(
    uint32_t bits
);

void lsmash_ifprintf
(
    FILE       *fp,
    int         indent,
    const char *format, ...
);

int lsmash_ceil_log2
(
    uint64_t value
);

int lsmash_compare_dts
(
    const lsmash_media_ts_t *a,
    const lsmash_media_ts_t *b
);

int lsmash_compare_cts
(
    const lsmash_media_ts_t *a,
    const lsmash_media_ts_t *b
);

static inline uint64_t lsmash_get_gcd
(
    uint64_t a,
    uint64_t b
)
{
    if( !b )
        return a;
    while( 1 )
    {
        uint64_t c = a % b;
        if( !c )
            return b;
        a = b;
        b = c;
    }
}

static inline uint64_t lsmash_get_lcm
(
    uint64_t a,
    uint64_t b
)
{
    if( !a )
        return 0;
    return (a / lsmash_get_gcd( a, b )) * b;
}

static inline void lsmash_reduce_fraction
(
    uint64_t *a,
    uint64_t *b
)
{
    if( !a || !b )
        return;
    uint64_t gcd = lsmash_get_gcd( *a, *b );
    if( gcd )
    {
        *a /= gcd;
        *b /= gcd;
    }
}

static inline void lsmash_reduce_fraction_su
(
    int64_t  *a,
    uint64_t *b
)
{
    if( !a || !b )
        return;
    uint64_t c = *a > 0 ? *a : -(*a);
    uint64_t gcd = lsmash_get_gcd( c, *b );
    if( gcd )
    {
        c /= gcd;
        *b /= gcd;
        *a = *a > 0 ? c : -c;
    }
}

#endif
