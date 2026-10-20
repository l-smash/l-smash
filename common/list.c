/*****************************************************************************
 * list.c
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

#include "internal.h" /* must be placed first */

#include <string.h>

static void lsmash_list_clear
(
    lsmash_entry_list_t *list
)
{
    list->head                 = NULL;
    list->tail                 = NULL;
    list->last_accessed_entry  = NULL;
    list->last_accessed_number = 0;
    list->entry_count          = 0;
    list->eliminator           = NULL;
}

void lsmash_list_init_orig
(
    lsmash_entry_list_t         *list,
    lsmash_entry_data_eliminator eliminator
)
{
    assert( eliminator != NULL );
    list->head                 = NULL;
    list->tail                 = NULL;
    list->last_accessed_entry  = NULL;
    list->last_accessed_number = 0;
    list->entry_count          = 0;
    list->eliminator           = eliminator;
}

lsmash_entry_list_t *lsmash_list_create_orig
(
    lsmash_entry_data_eliminator eliminator
)
{
    lsmash_entry_list_t *list = lsmash_malloc( sizeof(lsmash_entry_list_t) );
    if( !list )
        return NULL;
    lsmash_list_init( list, eliminator );
    return list;
}

void lsmash_list_destroy
(
    lsmash_entry_list_t *list
)
{
    lsmash_list_remove_entries( list );
    lsmash_free( list );
}

int lsmash_list_add_entry
(
    lsmash_entry_list_t *list,
    void                *data
)
{
    if( !list )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_entry_t *entry = lsmash_malloc( sizeof(lsmash_entry_t) );
    if( !entry )
        return LSMASH_ERR_MEMORY_ALLOC;
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

int lsmash_list_remove_entry_direct
(
    lsmash_entry_list_t *list,
    lsmash_entry_t      *entry
)
{
    if( !list || !entry )
        return LSMASH_ERR_FUNCTION_PARAM;
    assert( !entry->data || list->eliminator );
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
        list->eliminator( entry->data );
    if( entry == list->last_accessed_entry )
    {
        if( next )
            list->last_accessed_entry = next;
        else if( prev )
        {
            list->last_accessed_entry   = prev;
            list->last_accessed_number -= 1;
        }
        else
        {
            list->last_accessed_entry  = NULL;
            list->last_accessed_number = 0;
        }
    }
    else
    {
        /* We can't know the current entry number immediately,
         * so discard the last accessed entry info because time is wasted to know it. */
        list->last_accessed_entry  = NULL;
        list->last_accessed_number = 0;
    }
    lsmash_free( entry );
    list->entry_count -= 1;
    return 0;
}

int lsmash_list_remove_entry
(
    lsmash_entry_list_t *list,
    uint32_t             entry_number
)
{
    lsmash_entry_t *entry = lsmash_list_get_entry( list, entry_number );
    return lsmash_list_remove_entry_direct( list, entry );
}

int lsmash_list_remove_entry_tail
(
    lsmash_entry_list_t *list
)
{
    return lsmash_list_remove_entry_direct( list, list->tail );
}

void lsmash_list_remove_entries
(
    lsmash_entry_list_t *list
)
{
    if( !list )
        return;
    /* Note that it's valid that list contain no entries or no data to be eliminated. */
    for( lsmash_entry_t *entry = list->head; entry; )
    {
        lsmash_entry_t *next = entry->next;
        if( entry->data )
            list->eliminator( entry->data );
        lsmash_free( entry );
        entry = next;
    }
    lsmash_entry_data_eliminator eliminator = list->eliminator;
    lsmash_list_clear( list );
    list->eliminator = eliminator;
}

void lsmash_list_move_entries
(
    lsmash_entry_list_t *dst,
    lsmash_entry_list_t *src
)
{
    *dst = *src;
    lsmash_entry_data_eliminator eliminator = src->eliminator;
    lsmash_list_clear( src );
    src->eliminator = eliminator;
}

lsmash_entry_t *lsmash_list_get_entry
(
    lsmash_entry_list_t *list,
    uint32_t             entry_number
)
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
        list->last_accessed_entry  = entry;
        list->last_accessed_number = entry_number;
    }
    return entry;
}

void *lsmash_list_get_entry_data
(
    lsmash_entry_list_t *list,
    uint32_t             entry_number
)
{
    lsmash_entry_t *entry = lsmash_list_get_entry( list, entry_number );
    return entry ? entry->data : NULL;
}

/*---------------------------------------------------------------------------
 * C99 Paged Block Array (lsm_list_t) Implementation
 *---------------------------------------------------------------------------*/
#define LSM_LIST_INIT_PAGE_CAPACITY 8U

static int list_ensure_page
(
    lsm_list_t *list,
    uint32_t    page_index
)
{
    if( page_index < list->page_count )
        return 0;
    if( page_index >= list->page_capacity )
    {
        uint32_t new_cap = list->page_capacity ? (list->page_capacity << 1) : LSM_LIST_INIT_PAGE_CAPACITY;
        while( new_cap <= page_index )
            new_cap <<= 1;
        uint8_t **new_pages = lsmash_realloc( list->pages, (size_t)new_cap * sizeof(uint8_t *) );
        if( !new_pages )
            return LSMASH_ERR_MEMORY_ALLOC;
        memset( new_pages + list->page_capacity, 0, (size_t)(new_cap - list->page_capacity) * sizeof(uint8_t *) );
        list->pages         = new_pages;
        list->page_capacity = new_cap;
    }
    while( list->page_count <= page_index )
    {
        uint8_t *page = lsmash_malloc( (size_t)LSM_LIST_PAGE_SIZE * list->elem_size );
        if( !page )
            return LSMASH_ERR_MEMORY_ALLOC;
        list->pages[list->page_count++] = page;
    }
    return 0;
}

void lsm_list_init_orig
(
    lsm_list_t                  *list,
    uint32_t                     elem_size,
    lsmash_entry_data_eliminator eliminator
)
{
    if( !list )
        return;
    assert( elem_size > 0 );
    list->pages         = NULL;
    list->entry_count   = 0;
    list->page_count    = 0;
    list->page_capacity = 0;
    list->elem_size     = elem_size;
    list->eliminator    = eliminator;
}

lsm_list_t *lsm_list_create_orig
(
    uint32_t                     elem_size,
    lsmash_entry_data_eliminator eliminator
)
{
    if( !elem_size )
        return NULL;
    lsm_list_t *list = lsmash_malloc( sizeof(lsm_list_t) );
    if( !list )
        return NULL;
    lsm_list_init_orig( list, elem_size, eliminator );
    return list;
}

void lsm_list_clear
(
    lsm_list_t *list
)
{
    if( !list )
        return;
    if( list->eliminator )
        for( uint32_t i = 0; i < list->entry_count; i++ )
            list->eliminator( lsm_list_at( list, i ) );
    for( uint32_t p = 0; p < list->page_count; p++ )
        lsmash_free( list->pages[p] );
    lsmash_freep( &list->pages );
    list->entry_count   = 0;
    list->page_count    = 0;
    list->page_capacity = 0;
}

void lsm_list_destroy
(
    lsm_list_t *list
)
{
    if( !list )
        return;
    lsm_list_clear( list );
    lsmash_free( list );
}

void *lsm_list_append
(
    lsm_list_t *list
)
{
    if( !list || !list->elem_size || list->entry_count == UINT32_MAX )
        return NULL;
    uint32_t index      = list->entry_count;
    uint32_t page_index = index >> LSM_LIST_PAGE_SHIFT;
    if( list_ensure_page( list, page_index ) < 0 )
        return NULL;
    void *slot = list->pages[page_index] + (size_t)(index & LSM_LIST_PAGE_MASK) * list->elem_size;
    memset( slot, 0, list->elem_size );
    list->entry_count = index + 1;
    return slot;
}

int lsm_list_add
(
    lsm_list_t *list,
    const void *elem
)
{
    if( !list || !list->elem_size )
        return LSMASH_ERR_FUNCTION_PARAM;
    void *slot = lsm_list_append( list );
    if( !slot )
        return LSMASH_ERR_MEMORY_ALLOC;
    if( elem )
        memcpy( slot, elem, list->elem_size );
    return 0;
}

int lsm_list_remove_at
(
    lsm_list_t *list,
    uint32_t    index
)
{
    if( !list || index >= list->entry_count )
        return LSMASH_ERR_FUNCTION_PARAM;
    void *target = lsm_list_at( list, index );
    if( list->eliminator )
        list->eliminator( target );
    uint32_t last_index = list->entry_count - 1;
    if( index < last_index )
    {
        uint32_t start_page = index >> LSM_LIST_PAGE_SHIFT;
        uint32_t end_page   = last_index >> LSM_LIST_PAGE_SHIFT;
        uint32_t start_off  = index & LSM_LIST_PAGE_MASK;
        size_t   elem_size  = list->elem_size;
        for( uint32_t p = start_page; p <= end_page; p++ )
        {
            uint32_t first_slot = (p == start_page) ? start_off : 0;
            uint32_t last_slot  = (p == end_page)   ? (last_index & LSM_LIST_PAGE_MASK) : LSM_LIST_PAGE_MASK;
            if( first_slot < last_slot )
                memmove( list->pages[p] + (size_t)first_slot * elem_size,
                         list->pages[p] + (size_t)(first_slot + 1) * elem_size,
                         (size_t)(last_slot - first_slot) * elem_size );
            if( p < end_page )
                memcpy( list->pages[p] + (size_t)LSM_LIST_PAGE_MASK * elem_size,
                        list->pages[p + 1],
                        elem_size );
        }
    }
    list->entry_count = last_index;
    return 0;
}

int lsm_list_remove
(
    lsm_list_t *list,
    uint32_t    entry_number
)
{
    if( !entry_number )
        return LSMASH_ERR_FUNCTION_PARAM;
    return lsm_list_remove_at( list, entry_number - 1 );
}

int lsm_list_remove_tail
(
    lsm_list_t *list
)
{
    if( !list || !list->entry_count )
        return LSMASH_ERR_FUNCTION_PARAM;
    uint32_t last_index = list->entry_count - 1;
    if( list->eliminator )
        list->eliminator( lsm_list_at( list, last_index ) );
    list->entry_count = last_index;
    return 0;
}

void lsm_list_move
(
    lsm_list_t *dst,
    lsm_list_t *src
)
{
    if( !dst || !src || dst == src )
        return;
    lsm_list_clear( dst );
    *dst = *src;
    src->pages         = NULL;
    src->entry_count   = 0;
    src->page_count    = 0;
    src->page_capacity = 0;
}
