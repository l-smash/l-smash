/*****************************************************************************
 * list.h
 *****************************************************************************
 * Copyright (C) 2010-2015 L-SMASH project
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

#define lsmash_remove_entry_direct( list, entry, eliminator ) \
        lsmash_remove_entry_direct_orig( list, entry, (lsmash_entry_data_eliminator)(eliminator) )

#define lsmash_remove_entry( list, entry_number, eliminator ) \
        lsmash_remove_entry_orig( list, entry_number, (lsmash_entry_data_eliminator)(eliminator) )

#define lsmash_remove_entry_tail( list, eliminator ) \
        lsmash_remove_entry_tail_orig( list, (lsmash_entry_data_eliminator)(eliminator) )

#define lsmash_remove_entries( list, eliminator ) \
        lsmash_remove_entries_orig( list, (lsmash_entry_data_eliminator)(eliminator) )

#define lsmash_remove_list( list, eliminator ) \
        lsmash_remove_list_orig( list, (lsmash_entry_data_eliminator)(eliminator) )

void lsmash_init_entry_list( lsmash_entry_list_t *list );
lsmash_entry_list_t *lsmash_create_entry_list( void );
int lsmash_add_entry( lsmash_entry_list_t *list, void *data );
int lsmash_remove_entry_direct_orig( lsmash_entry_list_t *list, lsmash_entry_t *entry, lsmash_entry_data_eliminator eliminator );
int lsmash_remove_entry_orig( lsmash_entry_list_t *list, uint32_t entry_number, lsmash_entry_data_eliminator eliminator );
int lsmash_remove_entry_tail_orig( lsmash_entry_list_t *list, lsmash_entry_data_eliminator eliminator );
void lsmash_remove_entries_orig( lsmash_entry_list_t *list, lsmash_entry_data_eliminator eliminator );
void lsmash_remove_list_orig( lsmash_entry_list_t *list, lsmash_entry_data_eliminator eliminator );

lsmash_entry_t *lsmash_get_entry( lsmash_entry_list_t *list, uint32_t entry_number );
void *lsmash_get_entry_data( lsmash_entry_list_t *list, uint32_t entry_number );
