/*****************************************************************************
 * alloc.h
 *****************************************************************************
 * Copyright (C) 2012-2017 L-SMASH project
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

#ifndef LSMASH_ALLOC_H
#define LSMASH_ALLOC_H

#include <stddef.h>

#ifdef LSMASH_DEBUG_ALLOC

void *lsmash_malloc_debug( size_t size, const char *file, int line, const char *arg );
void *lsmash_malloc_zero_debug( size_t size, const char *file, int line, const char *arg );
void *lsmash_realloc_debug( void *ptr, size_t size, const char *file, int line, const char *arg );
void *lsmash_memdup_debug( const void *ptr, size_t size, const char *file, int line, const char *arg );
void  lsmash_free_debug( void *ptr, const char *file, int line, const char *arg );
void  lsmash_freep_debug( void *ptrptr, const char *file, int line, const char *arg );

size_t lsm_alloc_get_active_count( void );
size_t lsm_alloc_get_active_bytes( void );
void   lsm_alloc_dump_leaks( void );
int    lsm_alloc_verify_no_leaks( void );

#ifndef LSMASH_ALLOC_IMPLEMENTATION
#define lsmash_malloc( size )       lsmash_malloc_debug( size, __FILE__, __LINE__, #size )
#define lsmash_malloc_zero( size )  lsmash_malloc_zero_debug( size, __FILE__, __LINE__, #size )
#define lsmash_realloc( ptr, size ) lsmash_realloc_debug( ptr, size, __FILE__, __LINE__, #ptr )
#define lsmash_memdup( ptr, size )  lsmash_memdup_debug( ptr, size, __FILE__, __LINE__, #ptr )
#define lsmash_free( ptr )          lsmash_free_debug( ptr, __FILE__, __LINE__, #ptr )
#define lsmash_freep( ptrptr )      lsmash_freep_debug( ptrptr, __FILE__, __LINE__, #ptrptr )
#endif /* !LSMASH_ALLOC_IMPLEMENTATION */

#endif /* LSMASH_DEBUG_ALLOC */

#endif /* LSMASH_ALLOC_H */
