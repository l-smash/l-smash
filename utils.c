/*****************************************************************************
 * utils.c
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

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "utils.h"

/*---- list ----*/
void lsmash_init_entry_list( lsmash_entry_list_t *list )
{
    list->head                 = NULL;
    list->tail                 = NULL;
    list->last_accessed_entry  = NULL;
    list->last_accessed_number = 0;
    list->entry_count          = 0;
}

lsmash_entry_list_t *lsmash_create_entry_list( void )
{
    lsmash_entry_list_t *list = lsmash_malloc( sizeof(lsmash_entry_list_t) );
    if( !list )
        return NULL;
    lsmash_init_entry_list( list );
    return list;
}

int lsmash_add_entry( lsmash_entry_list_t *list, void *data )
{
    if( !list )
        return -1;
    lsmash_entry_t *entry = lsmash_malloc( sizeof(lsmash_entry_t) );
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

int lsmash_remove_entry_direct( lsmash_entry_list_t *list, lsmash_entry_t *entry, void *eliminator )
{
    if( !list || !entry )
        return -1;
    if( !eliminator )
        eliminator = lsmash_free;
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

int lsmash_remove_entry( lsmash_entry_list_t *list, uint32_t entry_number, void *eliminator )
{
    lsmash_entry_t *entry = lsmash_get_entry( list, entry_number );
    return lsmash_remove_entry_direct( list, entry, eliminator );
}

int lsmash_remove_entry_tail( lsmash_entry_list_t *list, void *eliminator )
{
    return lsmash_remove_entry_direct( list, list->tail, eliminator );
}

void lsmash_remove_entries( lsmash_entry_list_t *list, void *eliminator )
{
    if( !list )
        return;
    if( !eliminator )
        eliminator = lsmash_free;
    for( lsmash_entry_t *entry = list->head; entry; )
    {
        lsmash_entry_t *next = entry->next;
        if( entry->data )
            ((lsmash_entry_data_eliminator)eliminator)( entry->data );
        lsmash_free( entry );
        entry = next;
    }
    lsmash_init_entry_list( list );
}

void lsmash_remove_list( lsmash_entry_list_t *list, void *eliminator )
{
    if( !list )
        return;
    lsmash_remove_entries( list, eliminator );
    lsmash_free( list );
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
        list->last_accessed_entry  = entry;
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
void *lsmash_malloc( size_t size )
{
    return malloc( size );
}

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

void *lsmash_realloc( void *ptr, size_t size )
{
    return realloc( ptr, size );
}

void *lsmash_memdup( void *ptr, size_t size )
{
    if( !ptr || size == 0 )
        return NULL;
    void *dst = malloc( size );
    if( !dst )
        return NULL;
    memcpy( dst, ptr, size );
    return dst;
}

void lsmash_free( void *ptr )
{
    /* free() shall do nothing if a given address is NULL. */
    free( ptr );
}

void lsmash_freep( void *ptrptr )
{
    if( !ptrptr )
        return;
    void **ptr = (void **)ptrptr;
    free( *ptr );
    *ptr = NULL;
}
/*---- ----*/

/*---- others ----*/
void lsmash_log
(
    void            *hp,
    lsmash_log_level level,
    const char      *message,
    ...
)
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
    /* Dereference lsmash_class_t pointer if hp is non-NULL. */
    lsmash_class_t *class = hp ? (lsmash_class_t *)*(intptr_t *)hp : NULL;
    if( class )
        fprintf( stderr, "[%s: %s]: ", class->name, prefix );
    else
        fprintf( stderr, "[%s]: ", prefix );
    vfprintf( stderr, message, args );
    va_end( args );
}

uint32_t lsmash_count_bits
(
    uint32_t bits
)
{
    bits = (bits & 0x55555555) + ((bits >>  1) & 0x55555555);
    bits = (bits & 0x33333333) + ((bits >>  2) & 0x33333333);
    bits = (bits & 0x0f0f0f0f) + ((bits >>  4) & 0x0f0f0f0f);
    bits = (bits & 0x00ff00ff) + ((bits >>  8) & 0x00ff00ff);
    return (bits & 0x0000ffff) + ((bits >> 16) & 0x0000ffff);
}

void lsmash_ifprintf
(
    FILE       *fp,
    int         indent,
    const char *format, ...
)
{
    va_list args;
    va_start( args, format );
    if( indent <= 10 )
    {
        static const char *indent_string[] =
            {
                "",
                "    ",
                "        ",
                "            ",
                "                ",
                "                    ",
                "                        ",
                "                            ",
                "                                ",
                "                                    ",
                "                                        "
            };
        fprintf( fp, "%s", indent_string[indent] );
    }
    else
        for( int i = 0; i < indent; i++ )
            fprintf( fp, "    " );
    vfprintf( fp, format, args );
    va_end( args );
}

int lsmash_ceil_log2
(
    uint64_t value
)
{
    int length = 0;
    while( value > (1ULL << length) )
        ++length;
    return length;
}

/* for qsort function */
int lsmash_compare_dts
(
    const lsmash_media_ts_t *a,
    const lsmash_media_ts_t *b
)
{
    int64_t diff = (int64_t)(a->dts - b->dts);
    return diff > 0 ? 1 : (diff == 0 ? 0 : -1);
}

int lsmash_compare_cts
(
    const lsmash_media_ts_t *a,
    const lsmash_media_ts_t *b
)
{
    int64_t diff = (int64_t)(a->cts - b->cts);
    return diff > 0 ? 1 : (diff == 0 ? 0 : -1);
}

#ifdef _WIN32
int lsmash_convert_ansi_to_utf8( const char *ansi, char *utf8, int length )
{
    int len0 = MultiByteToWideChar( CP_THREAD_ACP, 0, ansi, -1, 0, 0 );
    wchar_t *buff = lsmash_malloc( len0 * sizeof(wchar_t) );
    if( !buff )
        return 0;
    int len1 = MultiByteToWideChar( CP_THREAD_ACP, 0, ansi, -1, buff, len0 );
    if( len0 != len1 )
        goto convert_fail;
    len0 = WideCharToMultiByte( CP_UTF8, 0, buff, -1, 0, 0, 0, 0 );
    if( len0 > length - 1 )
        goto convert_fail;
    len1 = WideCharToMultiByte( CP_UTF8, 0, buff, -1, utf8, length, 0, 0 );
    lsmash_free( buff );
    if( len0 != len1 )
        return 0;
    return len1;
convert_fail:
    lsmash_free( buff );
    return 0;
}
#endif
