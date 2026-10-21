/*****************************************************************************
 * list.h
 *****************************************************************************
 * Copyright (C) 2010-2017 L-SMASH project
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

typedef void (*lsmash_entry_data_eliminator)( void *data ); /* very same as free() of standard C lib; void free( void * );
                                                             * If 'data' is equal to NULL, the eliminator must do nothing. */

typedef struct lsmash_entry_tag lsmash_entry_t;

struct lsmash_entry_tag
{
    lsmash_entry_t *next;
    lsmash_entry_t *prev;
    void           *data;
};

typedef struct
{
    lsmash_entry_t              *head;
    lsmash_entry_t              *tail;
    lsmash_entry_t              *last_accessed_entry;
    uint32_t                     last_accessed_number;
    uint32_t                     entry_count;
    lsmash_entry_data_eliminator eliminator;
} lsmash_entry_list_t;

/* Utility macros to avoid 'lsmash_entry_data_eliminator' casts to the 'eliminator' argument */
#define lsmash_list_init( list, eliminator ) \
        lsmash_list_init_orig( list, (lsmash_entry_data_eliminator)(eliminator) )
#define lsmash_list_init_simple( list ) \
        lsmash_list_init_orig( list, (lsmash_entry_data_eliminator)lsmash_free )

#define lsmash_list_create( eliminator ) \
        lsmash_list_create_orig( (lsmash_entry_data_eliminator)(eliminator) )
#define lsmash_list_create_simple() \
        lsmash_list_create_orig( (lsmash_entry_data_eliminator)lsmash_free )

/* functions for internal usage */
void lsmash_list_init_orig
(
    lsmash_entry_list_t         *list,
    lsmash_entry_data_eliminator eliminator
);

lsmash_entry_list_t *lsmash_list_create_orig
(
    lsmash_entry_data_eliminator eliminator
);

void lsmash_list_destroy
(
    lsmash_entry_list_t *list
);

int lsmash_list_add_entry
(
    lsmash_entry_list_t *list,
    void                *data
);

int lsmash_list_remove_entry_direct
(
    lsmash_entry_list_t *list,
    lsmash_entry_t      *entry
);

int lsmash_list_remove_entry
(
    lsmash_entry_list_t *list,
    uint32_t             entry_number
);

int lsmash_list_remove_entry_tail
(
    lsmash_entry_list_t *list
);

void lsmash_list_remove_entries
(
    lsmash_entry_list_t *list
);

void lsmash_list_move_entries
(
    lsmash_entry_list_t *dst,
    lsmash_entry_list_t *src
);

lsmash_entry_t *lsmash_list_get_entry
(
    lsmash_entry_list_t *list,
    uint32_t             entry_number
);

void *lsmash_list_get_entry_data
(
    lsmash_entry_list_t *list,
    uint32_t             entry_number
);

/*---------------------------------------------------------------------------
 * C11 Paged Block Array (lsm_list_t) and Unified Accessor / Iterator API
 *
 * Architectural Why & How:
 * High-frequency sample tables (`stts`, `ctts`, `stsz`, `stz2`, `stss`, `stps`,
 * `sdtp`, `stsc`, `stco`/`co64`, `sgpd`, `sbgp`, `trun`, `tfra`, `sidx`, `elst`)
 * and timeline caches (`sample_pool`, `chunk_list`, `roll.pool`, `fragment->pool`)
 * can easily grow to hundreds of thousands or millions of entries.
 *
 * Using a traditional doubly-linked list (`lsmash_entry_list_t`) for these tables
 * incurs two heap allocations per entry (`lsmash_entry_t` + payload), 24 bytes of
 * `next`/`prev`/`data` pointer overhead per element on 64-bit targets, poor CPU
 * cache locality, and O(N) random access when jumping across indices.
 * Conversely, a flat `realloc`-backed dynamic vector invalidates all existing
 * element addresses whenever capacity doubles—which would fatally corrupt active
 * pointers such as `trak->cache->chunk.current` pointing into `chunk_list`.
 *
 * `lsm_list_t` solves all three constraints simultaneously by partitioning elements
 * into fixed-size pages of 2^8 = 256 contiguous value slots (`LSM_LIST_PAGE_SIZE`):
 * 1. Zero double-malloc & zero node overhead: Elements are stored inline by value
 *    inside 256-slot page blocks (`256 * elem_size` bytes per page).
 * 2. True O(1) bit-shift addressing: Because the page size is a power of two (2^8),
 *    `lsm_list_at(list, index)` resolves the page via `index >> 8` and the slot
 *    offset via `index & 0xFF` in two CPU instructions without integer division.
 * 3. 100% pointer address stability on append: Growing the container only reallocates
 *    the top-level `uint8_t **pages` directory and allocates new 256-element pages;
 *    previously allocated pages are never moved or reallocated in memory, so raw
 *    element pointers (e.g., `chunk_list` entries referenced by `cache->chunk.current`)
 *    remain 100% valid across arbitrary appends.
 * 4. Zero-initialized `{ 0 }` or `NULL` containers safely report 0 elements.
 *---------------------------------------------------------------------------*/
#define LSM_LIST_PAGE_SHIFT 8U
#define LSM_LIST_PAGE_SIZE  (1U << LSM_LIST_PAGE_SHIFT)
#define LSM_LIST_PAGE_MASK  (LSM_LIST_PAGE_SIZE - 1U)

_Static_assert( (LSM_LIST_PAGE_SIZE & (LSM_LIST_PAGE_SIZE - 1U)) == 0,
                "LSM_LIST_PAGE_SIZE must be a power of two" );
_Static_assert( LSM_LIST_PAGE_MASK == (LSM_LIST_PAGE_SIZE - 1U),
                "LSM_LIST_PAGE_MASK must match LSM_LIST_PAGE_SIZE - 1" );

typedef struct
{
    uint8_t                    **pages;
    uint32_t                     entry_count;
    uint32_t                     page_count;
    uint32_t                     page_capacity;
    uint32_t                     elem_size;
    lsmash_entry_data_eliminator eliminator;
} lsm_list_t;

#define lsm_list_init( list, elem_size, eliminator ) \
        lsm_list_init_orig( list, elem_size, (lsmash_entry_data_eliminator)(eliminator) )
#define lsm_list_init_simple( list, elem_size ) \
        lsm_list_init_orig( list, elem_size, NULL )

#define lsm_list_create( elem_size, eliminator ) \
        lsm_list_create_orig( elem_size, (lsmash_entry_data_eliminator)(eliminator) )
#define lsm_list_create_simple( elem_size ) \
        lsm_list_create_orig( elem_size, NULL )

void lsm_list_init_orig
(
    lsm_list_t                  *list,
    uint32_t                     elem_size,
    lsmash_entry_data_eliminator eliminator
);

lsm_list_t *lsm_list_create_orig
(
    uint32_t                     elem_size,
    lsmash_entry_data_eliminator eliminator
);

void lsm_list_destroy
(
    lsm_list_t *list
);

void lsm_list_clear
(
    lsm_list_t *list
);

void *lsm_list_append
(
    lsm_list_t *list
);

int lsm_list_add
(
    lsm_list_t *list,
    const void *elem
);

int lsm_list_remove_at
(
    lsm_list_t *list,
    uint32_t    index
);

int lsm_list_remove
(
    lsm_list_t *list,
    uint32_t    entry_number
);

int lsm_list_remove_tail
(
    lsm_list_t *list
);

void lsm_list_move
(
    lsm_list_t *dst,
    lsm_list_t *src
);

static inline uint32_t lsm_list_count( const lsm_list_t *list )
{
    return list ? list->entry_count : 0;
}

static inline void *lsm_list_at( const lsm_list_t *list, uint32_t index )
{
    if( !list || index >= list->entry_count )
        return NULL;
    return list->pages[index >> LSM_LIST_PAGE_SHIFT]
         + (size_t)(index & LSM_LIST_PAGE_MASK) * list->elem_size;
}

static inline void *lsm_list_get( const lsm_list_t *list, uint32_t entry_number )
{
    if( !entry_number )
        return NULL;
    return lsm_list_at( list, entry_number - 1 );
}

static inline void *lsm_list_head( const lsm_list_t *list )
{
    return lsm_list_at( list, 0 );
}

static inline void *lsm_list_tail( const lsm_list_t *list )
{
    if( !list || !list->entry_count )
        return NULL;
    return lsm_list_at( list, list->entry_count - 1 );
}

/* Lockstep cursor for simultaneous multi-table traversal */
typedef struct
{
    const lsm_list_t *list;
    void             *data;
    uint32_t          index;
} lsm_list_cursor_t;

static inline void lsm_list_cursor_init( lsm_list_cursor_t *cur, const lsm_list_t *list )
{
    cur->list  = list;
    cur->index = 0;
    cur->data  = lsm_list_at( list, 0 );
}

static inline void lsm_list_cursor_init_at( lsm_list_cursor_t *cur, const lsm_list_t *list, uint32_t index )
{
    cur->list  = list;
    cur->index = index;
    cur->data  = lsm_list_at( list, index );
}

static inline void *lsm_list_cursor_next( lsm_list_cursor_t *cur )
{
    if( !cur || !cur->data )
        return NULL;
    cur->index += 1;
    cur->data   = lsm_list_at( cur->list, cur->index );
    return cur->data;
}

static inline void *lsm_list_cursor_prev( lsm_list_cursor_t *cur )
{
    if( !cur || !cur->data || cur->index == 0 )
    {
        if( cur )
            cur->data = NULL;
        return NULL;
    }
    cur->index -= 1;
    cur->data   = lsm_list_at( cur->list, cur->index );
    return cur->data;
}

static inline lsmash_entry_t *lsm_entry_list_head( const lsmash_entry_list_t *list )
{
    return list ? list->head : NULL;
}

/* Single-loop iteration macros (safe with break and continue) */
#define LSM_LIST_FOR_EACH( var, list_ptr ) \
    for( uint32_t lsm_idx_##var = 0; \
         ((var) = lsm_list_at( (list_ptr), lsm_idx_##var )) != NULL; \
         ++lsm_idx_##var )

#define LSM_ENTRY_LIST_FOR_EACH( var, list_ptr ) \
    for( lsmash_entry_t *lsm_ent_##var = lsm_entry_list_head( (list_ptr) ); \
         ((var) = (lsm_ent_##var ? lsm_ent_##var->data : NULL)) != NULL; \
         lsm_ent_##var = lsm_ent_##var->next )
