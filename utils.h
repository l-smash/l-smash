/*****************************************************************************
 * utils.h
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

#include "bstream.h"
#include "list.h"

/*---- type ----*/
double lsmash_fixed2double( uint64_t value, int frac_width );
float lsmash_int2float32( uint32_t value );
double lsmash_int2float64( uint64_t value );

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
