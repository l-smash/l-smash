/*****************************************************************************
 * meta.c:
 *****************************************************************************
 * Copyright (C) 2012 L-SMASH project
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

#include "internal.h"

#include <string.h>
#include <stdlib.h>

#include "box.h"

static isom_data_t *isom_add_metadata( lsmash_root_t *root,
                                       lsmash_itunes_metadata_item item,
                                       char *meaning_string, char *name_string )
{
    assert( root && root->moov );
    if( ((item == ITUNES_METADATA_ITEM_CUSTOM) && (!meaning_string || !meaning_string[0]) )
     || (!root->moov->udta             && isom_add_udta( root, 0 ))
     || (!root->moov->udta->meta       && isom_add_meta( (isom_box_t *)root->moov->udta ))
     || (!root->moov->udta->meta->hdlr && isom_add_hdlr( NULL, root->moov->udta->meta, NULL, ISOM_META_HANDLER_TYPE_ITUNES_METADATA ))
     || (!root->moov->udta->meta->ilst && isom_add_ilst( root->moov )) )
        return NULL;
    isom_ilst_t *ilst = root->moov->udta->meta->ilst;
    if( isom_add_metaitem( ilst, item ) )
        return NULL;
    isom_metaitem_t *metaitem = (isom_metaitem_t *)ilst->item_list->tail->data;
    if( item == ITUNES_METADATA_ITEM_CUSTOM )
    {
        if( isom_add_mean( metaitem ) )
            goto fail;
        isom_mean_t *mean = metaitem->mean;
        mean->meaning_string_length = strlen( meaning_string );    /* No null terminator */
        mean->meaning_string = lsmash_memdup( meaning_string, mean->meaning_string_length );
        if( !mean->meaning_string )
            goto fail;
        if( name_string && name_string[0] )
        {
            if( isom_add_name( metaitem ) )
                goto fail;
            isom_name_t *name = metaitem->name;
            name->name_length = strlen( name_string );    /* No null terminator */
            name->name = lsmash_memdup( name_string, name->name_length );
            if( !name->name )
                goto fail;
        }
    }
    if( isom_add_data( metaitem ) )
        goto fail;
    return metaitem->data;
fail:
    lsmash_remove_entry_direct( ilst->item_list, ilst->item_list->tail, isom_remove_metaitem );
    return NULL;
}

static int isom_set_itunes_metadata_string( lsmash_root_t *root,
                                            lsmash_itunes_metadata_item item,
                                            lsmash_itunes_metadata_t value, char *meaning, char *name )
{
    uint32_t value_length = strlen( value.string );
    if( item == ITUNES_METADATA_ITEM_DESCRIPTION && value_length > 255 )
        item = ITUNES_METADATA_ITEM_LONG_DESCRIPTION;
    isom_data_t *data = isom_add_metadata( root, item, meaning, name );
    if( !data )
        return -1;
    data->type_code = 1;
    data->value_length = value_length;      /* No null terminator */
    data->value = lsmash_memdup( value.string, data->value_length );
    if( !data->value )
    {
        isom_ilst_t *ilst = root->moov->udta->meta->ilst;
        lsmash_remove_entry_direct( ilst->item_list, ilst->item_list->tail, isom_remove_metaitem );
        return -1;
    }
    return 0;
}

static int isom_set_itunes_metadata_integer( lsmash_root_t *root,
                                             lsmash_itunes_metadata_item item,
                                             lsmash_itunes_metadata_t value, char *meaning, char *name )
{
    static const struct
    {
        lsmash_itunes_metadata_item item;
        int                         length;
    } metadata_code_type_table[] =
        {
            { ITUNES_METADATA_ITEM_EPISODE_GLOBAL_ID,          1 },
            { ITUNES_METADATA_ITEM_PREDEFINED_GENRE,           4 },
            { ITUNES_METADATA_ITEM_CONTENT_RATING,             1 },
            { ITUNES_METADATA_ITEM_MEDIA_TYPE,                 1 },
            { ITUNES_METADATA_ITEM_BEATS_PER_MINUTE,           2 },
            { ITUNES_METADATA_ITEM_TV_EPISODE,                 4 },
            { ITUNES_METADATA_ITEM_TV_SEASON,                  4 },
            { ITUNES_METADATA_ITEM_ITUNES_ACCOUNT_TYPE,        1 },
            { ITUNES_METADATA_ITEM_ITUNES_ARTIST_ID,           4 },
            { ITUNES_METADATA_ITEM_ITUNES_COMPOSER_ID,         4 },
            { ITUNES_METADATA_ITEM_ITUNES_CATALOG_ID,          4 },
            { ITUNES_METADATA_ITEM_ITUNES_TV_GENRE_ID,         4 },
            { ITUNES_METADATA_ITEM_ITUNES_PLAYLIST_ID,         8 },
            { ITUNES_METADATA_ITEM_ITUNES_COUNTRY_CODE,        4 },
            { ITUNES_METADATA_ITEM_CUSTOM,                     8 },
            { 0,                                               0 }
        };
    int i;
    for( i = 0; metadata_code_type_table[i].item; i++ )
        if( item == metadata_code_type_table[i].item )
            break;
    if( metadata_code_type_table[i].length == 0 )
        return -1;
    isom_data_t *data = isom_add_metadata( root, item, meaning, name );
    if( !data )
        return -1;
    data->type_code = 21;
    data->value_length = metadata_code_type_table[i].length;
    uint8_t temp[8];
    for( i = 0; i < data->value_length; i++ )
    {
        int shift = (data->value_length - i - 1) * 8;
        temp[i] = (value.integer >> shift) & 0xff;
    }
    data->value = lsmash_memdup( temp, data->value_length );
    if( !data->value )
    {
        isom_ilst_t *ilst = root->moov->udta->meta->ilst;
        lsmash_remove_entry_direct( ilst->item_list, ilst->item_list->tail, isom_remove_metaitem );
        return -1;
    }
    return 0;
}

static int isom_set_itunes_metadata_boolean( lsmash_root_t *root,
                                             lsmash_itunes_metadata_item item,
                                             lsmash_itunes_metadata_t value, char *meaning, char *name )
{
    isom_data_t *data = isom_add_metadata( root, item, meaning, name );
    if( !data )
        return -1;
    data->type_code = 21;
    data->value_length = 1;
    uint8_t temp = (uint8_t)value.boolean;
    data->value = lsmash_memdup( &temp, 1 );
    if( !data->value )
    {
        isom_ilst_t *ilst = root->moov->udta->meta->ilst;
        lsmash_remove_entry_direct( ilst->item_list, ilst->item_list->tail, isom_remove_metaitem );
        return -1;
    }
    return 0;
}

int lsmash_set_itunes_metadata( lsmash_root_t *root,
                                lsmash_itunes_metadata_item item, lsmash_itunes_metadata_type type,
                                lsmash_itunes_metadata_t value, char *meaning, char *name )
{
    static const struct
    {
        lsmash_itunes_metadata_item item;
        int (*func_set_itunes_metadata)( lsmash_root_t *, lsmash_itunes_metadata_item, lsmash_itunes_metadata_t, char *, char * );
    } itunes_metadata_function_mapping[] =
        {
            { ITUNES_METADATA_ITEM_ALBUM_NAME,                 isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_ARTIST,                     isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_USER_COMMENT,               isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_RELEASE_DATE,               isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_ENCODED_BY,                 isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_USER_GENRE,                 isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_0XA9_GROUPING,              isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_LYRICS,                     isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_TITLE,                      isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_TRACK_SUBTITLE,             isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_ENCODING_TOOL,              isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_COMPOSER,                   isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_ALBUM_ARTIST,               isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_PODCAST_CATEGORY,           isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_COPYRIGHT,                  isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_DESCRIPTION,                isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_GROUPING,                   isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_PODCAST_KEYWORD,            isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_LONG_DESCRIPTION,           isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_PURCHASE_DATE,              isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_TV_EPISODE_ID,              isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_TV_NETWORK,                 isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_TV_SHOW_NAME,               isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_ITUNES_PURCHASE_ACCOUNT_ID, isom_set_itunes_metadata_string },
            { ITUNES_METADATA_ITEM_EPISODE_GLOBAL_ID,          isom_set_itunes_metadata_integer },
            { ITUNES_METADATA_ITEM_PREDEFINED_GENRE,           isom_set_itunes_metadata_integer },
            { ITUNES_METADATA_ITEM_CONTENT_RATING,             isom_set_itunes_metadata_integer },
            { ITUNES_METADATA_ITEM_MEDIA_TYPE,                 isom_set_itunes_metadata_integer },
            { ITUNES_METADATA_ITEM_BEATS_PER_MINUTE,           isom_set_itunes_metadata_integer },
            { ITUNES_METADATA_ITEM_TV_EPISODE,                 isom_set_itunes_metadata_integer },
            { ITUNES_METADATA_ITEM_TV_SEASON,                  isom_set_itunes_metadata_integer },
            { ITUNES_METADATA_ITEM_ITUNES_ACCOUNT_TYPE,        isom_set_itunes_metadata_integer },
            { ITUNES_METADATA_ITEM_ITUNES_ARTIST_ID,           isom_set_itunes_metadata_integer },
            { ITUNES_METADATA_ITEM_ITUNES_COMPOSER_ID,         isom_set_itunes_metadata_integer },
            { ITUNES_METADATA_ITEM_ITUNES_CATALOG_ID,          isom_set_itunes_metadata_integer },
            { ITUNES_METADATA_ITEM_ITUNES_TV_GENRE_ID,         isom_set_itunes_metadata_integer },
            { ITUNES_METADATA_ITEM_ITUNES_PLAYLIST_ID,         isom_set_itunes_metadata_integer },
            { ITUNES_METADATA_ITEM_ITUNES_COUNTRY_CODE,        isom_set_itunes_metadata_integer },
            { ITUNES_METADATA_ITEM_DISC_COMPILATION,           isom_set_itunes_metadata_boolean },
            { ITUNES_METADATA_ITEM_HIGH_DEFINITION_VIDEO,      isom_set_itunes_metadata_boolean },
            { ITUNES_METADATA_ITEM_PODCAST,                    isom_set_itunes_metadata_boolean },
            { ITUNES_METADATA_ITEM_GAPLESS_PLAYBACK,           isom_set_itunes_metadata_boolean },
            { 0,                                               NULL }
        };
    for( int i = 0; itunes_metadata_function_mapping[i].func_set_itunes_metadata; i++ )
        if( item == itunes_metadata_function_mapping[i].item )
            return itunes_metadata_function_mapping[i].func_set_itunes_metadata( root, item, value, meaning, name );
    if( item == ITUNES_METADATA_ITEM_CUSTOM )
        switch( type )
        {
            case ITUNES_METADATA_TYPE_STRING :
                return isom_set_itunes_metadata_string( root, item, value, meaning, name );
            case ITUNES_METADATA_TYPE_INTEGER :
                return isom_set_itunes_metadata_integer( root, item, value, meaning, name );
            case ITUNES_METADATA_TYPE_BOOLEAN :
                return isom_set_itunes_metadata_boolean( root, item, value, meaning, name );
            default :
                break;
        }
    return -1;
}

#ifdef LSMASH_DEMUXER_ENABLED
static int isom_copy_mean( isom_metaitem_t *dst, isom_metaitem_t *src )
{
    if( !dst )
        return 0;
    isom_remove_mean( dst->mean );
    if( !src || !src->mean )
        return 0;
    if( isom_add_mean( dst ) )
        return -1;
    if( src->mean->meaning_string )
    {
        dst->mean->meaning_string = lsmash_memdup( src->mean->meaning_string, src->mean->meaning_string_length );
        if( !dst->mean->meaning_string )
            return -1;
        dst->mean->meaning_string_length = src->mean->meaning_string_length;
    }
    return 0;
}

static int isom_copy_name( isom_metaitem_t *dst, isom_metaitem_t *src )
{
    if( !dst )
        return 0;
    isom_remove_name( dst->name );
    if( !src || !src->name )
        return 0;
    if( isom_add_name( dst ) )
        return -1;
    if( src->name->name )
    {
        dst->name->name = lsmash_memdup( src->name->name, src->name->name_length );
        if( !dst->name->name )
            return -1;
        dst->name->name_length = src->name->name_length;
    }
    return 0;
}

static int isom_copy_data( isom_metaitem_t *dst, isom_metaitem_t *src )
{
    if( !dst )
        return 0;
    isom_remove_data( dst->data );
    if( !src || !src->data )
        return 0;
    if( isom_add_data( dst ) )
        return -1;
    isom_copy_fields( dst, src, data );
    if( src->data->value )
    {
        dst->data->value = lsmash_memdup( src->data->value, src->data->value_length );
        if( !dst->data->value )
            return -1;
        dst->data->value_length = src->data->value_length;
    }
    return 0;
}

static isom_metaitem_t *isom_duplicate_metaitem( isom_metaitem_t *src )
{
    isom_metaitem_t *dst = lsmash_memdup( src, sizeof(isom_metaitem_t) );
    if( !dst )
        return NULL;
    dst->mean = NULL;
    dst->name = NULL;
    dst->data = NULL;
    /* Copy children. */
    if( isom_copy_mean( dst, src )
     || isom_copy_name( dst, src )
     || isom_copy_data( dst, src ) )
    {
        isom_remove_metaitem( dst );
        return NULL;
    }
    return dst;
}

lsmash_itunes_metadata_list_t *lsmash_export_itunes_metadata( lsmash_root_t *root )
{
    if( !root || !root->moov )
        return NULL;
    if( !root->moov->udta || !root->moov->udta->meta || !root->moov->udta->meta->ilst )
    {
        isom_ilst_t *dst = lsmash_malloc_zero( sizeof(isom_ilst_t) );
        if( !dst )
            return NULL;
        return (lsmash_itunes_metadata_list_t *)dst;
    }
    isom_ilst_t *src = root->moov->udta->meta->ilst;
    isom_ilst_t *dst = lsmash_memdup( src, sizeof(isom_ilst_t) );
    if( !dst )
        return NULL;
    dst->root      = NULL;
    dst->parent    = NULL;
    dst->item_list = NULL;
    if( src->item_list )
    {
        dst->item_list = lsmash_create_entry_list();
        if( !dst->item_list )
        {
            free( dst );
            return NULL;
        }
        for( lsmash_entry_t *entry = src->item_list->head; entry; entry = entry->next )
        {
            isom_metaitem_t *dst_metaitem = isom_duplicate_metaitem( (isom_metaitem_t *)entry->data );
            if( !dst_metaitem )
            {
                isom_remove_ilst( dst );
                return NULL;
            }
            if( lsmash_add_entry( dst->item_list, dst_metaitem ) )
            {
                isom_remove_metaitem( dst_metaitem );
                isom_remove_ilst( dst );
                return NULL;
            }
        }
    }
    return (lsmash_itunes_metadata_list_t *)dst;
}

int lsmash_import_itunes_metadata( lsmash_root_t *root, lsmash_itunes_metadata_list_t *list )
{
    if( !root || !list )
        return -1;
    if( !root->itunes_movie )
        return 0;
    isom_ilst_t *src = (isom_ilst_t *)list;
    if( !src->item_list || !src->item_list->entry_count )
        return 0;
    if( (!root->moov->udta             && isom_add_udta( root, 0 ))
     || (!root->moov->udta->meta       && isom_add_meta( (isom_box_t *)root->moov->udta ))
     || (!root->moov->udta->meta->hdlr && isom_add_hdlr( NULL, root->moov->udta->meta, NULL, ISOM_META_HANDLER_TYPE_ITUNES_METADATA ))
     || (!root->moov->udta->meta->ilst && isom_add_ilst( root->moov )) )
        return -1;
    isom_ilst_t *dst = root->moov->udta->meta->ilst;
    for( lsmash_entry_t *entry = src->item_list->head; entry; entry = entry->next )
    {
        isom_metaitem_t *dst_metaitem = isom_duplicate_metaitem( (isom_metaitem_t *)entry->data );
        if( !dst_metaitem )
            return -1;
        if( lsmash_add_entry( dst->item_list, dst_metaitem ) )
        {
            isom_remove_metaitem( dst_metaitem );
            return -1;
        }
    }
    return 0;
}

void lsmash_destroy_itunes_metadata( lsmash_itunes_metadata_list_t *list )
{
    isom_remove_ilst( (isom_ilst_t *)list );
}
#endif /* LSMASH_DEMUXER_ENABLED */
