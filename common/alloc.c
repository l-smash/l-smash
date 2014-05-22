/*****************************************************************************
 * alloc.c
 *****************************************************************************
 * Copyright (C) 2011-2014 L-SMASH project
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

#ifdef LSMASH_ALLOC_DEBUG
    #undef LSMASH_ALLOC_DEBUG
    #include "internal.h" /* must be placed first */
    #define LSMASH_ALLOC_DEBUG
#else
    #include "internal.h" /* must be placed first */
#endif

#include <stdlib.h>
#include <string.h>

#ifdef LSMASH_ALLOC_DEBUG
static size_t malloc_count = 0;
static size_t heap_count   = 0;
#endif

void *lsmash_malloc( size_t size )
{
#ifdef LSMASH_ALLOC_DEBUG
    void *p = malloc( size );
    if( p )
    {
        ++malloc_count;
        ++heap_count;
    }
    return p;
#else
    return malloc( size );
#endif
}

void *lsmash_malloc_zero( size_t size )
{
    if( !size )
        return NULL;
    void *p = lsmash_malloc( size );
    if( !p )
        return NULL;
    memset( p, 0, size );
    return p;
}

void *lsmash_realloc( void *ptr, size_t size )
{
#ifdef LSMASH_ALLOC_DEBUG
    if( !ptr )
        return lsmash_malloc( size );
#endif
    return realloc( ptr, size );
}

void *lsmash_memdup( const void *ptr, size_t size )
{
    if( !ptr || size == 0 )
        return NULL;
    void *dst = lsmash_malloc( size );
    if( !dst )
        return NULL;
    memcpy( dst, ptr, size );
    return dst;
}

void lsmash_free( void *ptr )
{
#ifdef LSMASH_ALLOC_DEBUG
    if( ptr )
    {
        free( ptr );
        --heap_count;
    }
#else
    /* free() shall do nothing if a given address is NULL. */
    free( ptr );
#endif
}

void lsmash_freep( void *ptrptr )
{
    if( !ptrptr )
        return;
    void **ptr = (void **)ptrptr;
    lsmash_free( *ptr );
    *ptr = NULL;
}

#ifdef LSMASH_ALLOC_DEBUG
void *lsmash_malloc_debug( size_t size, const char *file, int line )
{
    void *p = lsmash_malloc( size );
    if( p )
        fprintf( stderr, "malloc: <%d> file=%s(%d), address=0x%p, heap=%d\n", malloc_count, file, line, p, heap_count );
    return p;
}

void *lsmash_malloc_zero_debug( size_t size, const char *file, int line )
{
    void *p = lsmash_malloc_zero( size );
    if( p )
        fprintf( stderr, "malloc: <%d> file=%s(%d), address=0x%p, heap=%d\n", malloc_count, file, line, p, heap_count );
    return p;
}

void *lsmash_realloc_debug( void *ptr, size_t size, const char *file, int line, const char *arg )
{
    void *p;
    if( ptr )
    {
        void *temp = ptr;
        p = lsmash_realloc( ptr, size );
        if( p )
        {
            fprintf( stderr, "free: file=%s(%d), address=0x%p, heap=%d, arg=%s\n", file, line, temp, heap_count - 1, arg );
            fprintf( stderr, "malloc: <%d> file=%s(%d), address=0x%p, heap=%d\n", malloc_count, file, line, p, heap_count );
        }
    }
    else
    {
        p = lsmash_realloc( ptr, size );
        if( p )
            fprintf( stderr, "malloc: <%d> file=%s(%d), address=0x%p, heap=%d\n", malloc_count, file, line, p, heap_count );
    }
    return p;
}

void *lsmash_memdup_debug( const void *ptr, size_t size, const char *file, int line )
{
    void *p = lsmash_memdup( ptr, size );
    fprintf( stderr, "malloc: <%d> file=%s(%d), address=0x%p, heap=%d\n", malloc_count, file, line, p, heap_count );
    return p;
}

void lsmash_free_debug( void *ptr, const char *file, int line, const char *arg )
{
    void *p = ptr;
    lsmash_free( ptr );
    fprintf( stderr, "free: file=%s(%d), address=0x%p, heap=%d, arg=%s\n", file, line, p, heap_count, arg );
}

void lsmash_freep_debug( void *ptrptr, const char *file, int line, const char *arg )
{
    if( !ptrptr )
        return;
    void **p = (void **)ptrptr;
    lsmash_freep( ptrptr );
    fprintf( stderr, "free: file=%s(%d), address=0x%p, heap=%d, arg=%s\n", file, line, *p, heap_count, arg );
}
#endif
