/*****************************************************************************
 * alloc.c
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

#define LSMASH_ALLOC_IMPLEMENTATION
#include "common/internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#ifdef LSMASH_DEBUG_ALLOC

#define LSM_ALLOC_MAGIC_ACTIVE 0xA110CA7E5A17BEEFULL
#define LSM_ALLOC_MAGIC_FREED  0xDEAD7EEDDEAD7EEDULL
#define LSM_ALLOC_CANARY_FRONT 0xC0DEC0DEC0DEC0DEULL
#define LSM_ALLOC_CANARY_BYTE  0xA5U
#define LSM_ALLOC_POISON_ALLOC 0xCDU
#define LSM_ALLOC_POISON_FREE  0xDDU
#define LSM_ALLOC_REDZONE_SIZE 16U

typedef struct lsm_alloc_header_tag
{
    uint64_t                     magic;
    size_t                       size;
    struct lsm_alloc_header_tag *prev;
    struct lsm_alloc_header_tag *next;
    const char                  *file;
    const char                  *arg;
    int                          line;
    uint32_t                     reserved;
    uint64_t                     front_canary;
} lsm_alloc_header_t;

static lsm_alloc_header_t *alloc_active_head  = NULL;
static size_t              alloc_active_count = 0;
static size_t              alloc_active_bytes = 0;

static void alloc_link_block( lsm_alloc_header_t *hdr )
{
    hdr->prev = NULL;
    hdr->next = alloc_active_head;
    if( alloc_active_head )
        alloc_active_head->prev = hdr;
    alloc_active_head = hdr;
    alloc_active_count++;
    alloc_active_bytes += hdr->size;
}

static void alloc_unlink_block( lsm_alloc_header_t *hdr )
{
    if( hdr->prev )
        hdr->prev->next = hdr->next;
    else
        alloc_active_head = hdr->next;
    if( hdr->next )
        hdr->next->prev = hdr->prev;
    hdr->prev = NULL;
    hdr->next = NULL;
    alloc_active_count--;
    alloc_active_bytes -= hdr->size;
}

static void alloc_verify_block
(
    lsm_alloc_header_t *hdr,
    const void         *user_ptr,
    const char         *op,
    const char         *file,
    int                 line,
    const char         *arg
)
{
    const char *src_file = file ? file : "<unknown>";
    const char *src_arg  = arg  ? arg  : "<unknown>";
    if( hdr->magic == LSM_ALLOC_MAGIC_FREED )
    {
        fprintf( stderr,
                 "[LSMASH_MEMORY_AUDIT] DOUBLE FREE detected in %s at %s(%d): expr='%s', ptr=%p "
                 "(originally allocated at %s(%d): expr='%s', size=%"PRIu64")\n",
                 op, src_file, line, src_arg, user_ptr,
                 hdr->file ? hdr->file : "<unknown>", hdr->line,
                 hdr->arg  ? hdr->arg  : "<unknown>", (uint64_t)hdr->size );
        fflush( stderr );
        abort();
    }
    if( hdr->magic != LSM_ALLOC_MAGIC_ACTIVE )
    {
        fprintf( stderr,
                 "[LSMASH_MEMORY_AUDIT] INVALID POINTER / CORRUPTED HEADER in %s at %s(%d): expr='%s', ptr=%p "
                 "(magic=0x%016"PRIx64")\n",
                 op, src_file, line, src_arg, user_ptr, hdr->magic );
        fflush( stderr );
        abort();
    }
    if( hdr->front_canary != LSM_ALLOC_CANARY_FRONT )
    {
        fprintf( stderr,
                 "[LSMASH_MEMORY_AUDIT] BUFFER UNDERRUN (front canary corrupted) in %s at %s(%d): expr='%s', ptr=%p "
                 "(allocated at %s(%d): expr='%s', size=%"PRIu64")\n",
                 op, src_file, line, src_arg, user_ptr,
                 hdr->file ? hdr->file : "<unknown>", hdr->line,
                 hdr->arg  ? hdr->arg  : "<unknown>", (uint64_t)hdr->size );
        fflush( stderr );
        abort();
    }
    const uint8_t *rear = (const uint8_t *)user_ptr + hdr->size;
    for( size_t i = 0; i < LSM_ALLOC_REDZONE_SIZE; i++ )
    {
        if( rear[i] != LSM_ALLOC_CANARY_BYTE )
        {
            fprintf( stderr,
                     "[LSMASH_MEMORY_AUDIT] BUFFER OVERRUN (rear canary corrupted at +%u) in %s at %s(%d): expr='%s', ptr=%p "
                     "(allocated at %s(%d): expr='%s', size=%"PRIu64")\n",
                     (unsigned)i, op, src_file, line, src_arg, user_ptr,
                     hdr->file ? hdr->file : "<unknown>", hdr->line,
                     hdr->arg  ? hdr->arg  : "<unknown>", (uint64_t)hdr->size );
            fflush( stderr );
            abort();
        }
    }
}

void *lsmash_malloc_debug( size_t size, const char *file, int line, const char *arg )
{
    if( size > SIZE_MAX - sizeof(lsm_alloc_header_t) - LSM_ALLOC_REDZONE_SIZE )
        return NULL;
    size_t total_size = sizeof(lsm_alloc_header_t) + size + LSM_ALLOC_REDZONE_SIZE;
    lsm_alloc_header_t *hdr = (lsm_alloc_header_t *)malloc( total_size );
    if( !hdr )
        return NULL;
    hdr->magic        = LSM_ALLOC_MAGIC_ACTIVE;
    hdr->size         = size;
    hdr->prev         = NULL;
    hdr->next         = NULL;
    hdr->file         = file;
    hdr->arg          = arg;
    hdr->line         = line;
    hdr->reserved     = 0;
    hdr->front_canary = LSM_ALLOC_CANARY_FRONT;
    uint8_t *user_ptr = (uint8_t *)hdr + sizeof(lsm_alloc_header_t);
    if( size > 0 )
        memset( user_ptr, LSM_ALLOC_POISON_ALLOC, size );
    memset( user_ptr + size, LSM_ALLOC_CANARY_BYTE, LSM_ALLOC_REDZONE_SIZE );
    alloc_link_block( hdr );
    return user_ptr;
}

void *lsmash_malloc_zero_debug( size_t size, const char *file, int line, const char *arg )
{
    void *ptr = lsmash_malloc_debug( size, file, line, arg );
    if( ptr && size > 0 )
        memset( ptr, 0, size );
    return ptr;
}

void *lsmash_realloc_debug( void *ptr, size_t size, const char *file, int line, const char *arg )
{
    if( !ptr )
        return lsmash_malloc_debug( size, file, line, arg );
    lsm_alloc_header_t *old_hdr = (lsm_alloc_header_t *)((uint8_t *)ptr - sizeof(lsm_alloc_header_t));
    alloc_verify_block( old_hdr, ptr, "lsmash_realloc", file, line, arg );
    void *new_ptr = lsmash_malloc_debug( size, file, line, arg );
    if( !new_ptr )
        return NULL;
    size_t copy_size = old_hdr->size < size ? old_hdr->size : size;
    if( copy_size > 0 )
        memcpy( new_ptr, ptr, copy_size );
    lsmash_free_debug( ptr, file, line, arg );
    return new_ptr;
}

void *lsmash_memdup_debug( const void *ptr, size_t size, const char *file, int line, const char *arg )
{
    if( !ptr )
        return NULL;
    void *dst = lsmash_malloc_debug( size, file, line, arg );
    if( !dst )
        return NULL;
    if( size > 0 )
        memcpy( dst, ptr, size );
    return dst;
}

void lsmash_free_debug( void *ptr, const char *file, int line, const char *arg )
{
    if( !ptr )
        return;
    lsm_alloc_header_t *hdr = (lsm_alloc_header_t *)((uint8_t *)ptr - sizeof(lsm_alloc_header_t));
    alloc_verify_block( hdr, ptr, "lsmash_free", file, line, arg );
    alloc_unlink_block( hdr );
    hdr->magic = LSM_ALLOC_MAGIC_FREED;
    if( hdr->size > 0 )
        memset( ptr, LSM_ALLOC_POISON_FREE, hdr->size );
    free( hdr );
}

void lsmash_freep_debug( void *ptrptr, const char *file, int line, const char *arg )
{
    if( !ptrptr )
        return;
    void *ptr = *(void **)ptrptr;
    lsmash_free_debug( ptr, file, line, arg );
    *(void **)ptrptr = NULL;
}

size_t lsm_alloc_get_active_count( void )
{
    return alloc_active_count;
}

size_t lsm_alloc_get_active_bytes( void )
{
    return alloc_active_bytes;
}

void lsm_alloc_dump_leaks( void )
{
    if( !alloc_active_head )
        return;
    fprintf( stderr,
             "[LSMASH_MEMORY_AUDIT] MEMORY LEAK DETECTED: %"PRIu64" active block(s), %"PRIu64" byte(s) leaked:\n",
             (uint64_t)alloc_active_count, (uint64_t)alloc_active_bytes );
    for( const lsm_alloc_header_t *hdr = alloc_active_head; hdr; hdr = hdr->next )
    {
        const void *user_ptr = (const uint8_t *)hdr + sizeof(lsm_alloc_header_t);
        fprintf( stderr,
                 "  - leak ptr=%p, size=%"PRIu64" bytes, allocated at %s(%d): expr='%s'\n",
                 user_ptr, (uint64_t)hdr->size,
                 hdr->file ? hdr->file : "<unknown>",
                 hdr->line,
                 hdr->arg  ? hdr->arg  : "<unknown>" );
    }
    fflush( stderr );
}

int lsm_alloc_verify_no_leaks( void )
{
    /* Verify canaries on any remaining blocks first. */
    for( lsm_alloc_header_t *hdr = alloc_active_head; hdr; hdr = hdr->next )
    {
        const void *user_ptr = (const uint8_t *)hdr + sizeof(lsm_alloc_header_t);
        alloc_verify_block( hdr, user_ptr, "lsm_alloc_verify_no_leaks", hdr->file, hdr->line, hdr->arg );
    }
    if( alloc_active_count != 0 || alloc_active_bytes != 0 )
    {
        lsm_alloc_dump_leaks();
        return -1;
    }
    return 0;
}

void *lsmash_malloc( size_t size )
{
    return lsmash_malloc_debug( size, "<callback>", 0, "lsmash_malloc" );
}

void *lsmash_malloc_zero( size_t size )
{
    return lsmash_malloc_zero_debug( size, "<callback>", 0, "lsmash_malloc_zero" );
}

void *lsmash_realloc( void *ptr, size_t size )
{
    return lsmash_realloc_debug( ptr, size, "<callback>", 0, "lsmash_realloc" );
}

void *lsmash_memdup( const void *ptr, size_t size )
{
    return lsmash_memdup_debug( ptr, size, "<callback>", 0, "lsmash_memdup" );
}

void lsmash_free( void *ptr )
{
    lsmash_free_debug( ptr, "<callback>", 0, "lsmash_free" );
}

void lsmash_freep( void *ptrptr )
{
    lsmash_freep_debug( ptrptr, "<callback>", 0, "lsmash_freep" );
}

#else /* !LSMASH_DEBUG_ALLOC */

void *lsmash_malloc( size_t size )
{
    return malloc( size );
}

void *lsmash_malloc_zero( size_t size )
{
    return calloc( 1, size );
}

void *lsmash_realloc( void *ptr, size_t size )
{
    return realloc( ptr, size );
}

void *lsmash_memdup( const void *ptr, size_t size )
{
    if( !ptr )
        return NULL;
    void *dst = malloc( size );
    if( !dst )
        return NULL;
    memcpy( dst, ptr, size );
    return dst;
}

void lsmash_free( void *ptr )
{
    free( ptr );
}

void lsmash_freep( void *ptrptr )
{
    if( !ptrptr )
        return;
    void *ptr = *(void **)ptrptr;
    free( ptr );
    *(void **)ptrptr = NULL;
}

#endif /* LSMASH_DEBUG_ALLOC */
