/*****************************************************************************
 * box.c:
 *****************************************************************************
 * Copyright (C) 2012-2014 L-SMASH project
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

#include "common/internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>

#include "box.h"
#include "write.h"
#include "read.h"
#ifdef LSMASH_DEMUXER_ENABLED
#include "print.h"
#include "timeline.h"
#endif

#include "codecs/mp4a.h"
#include "codecs/mp4sys.h"

void isom_init_box_common
(
    void             *_box,
    void             *_parent,
    lsmash_box_type_t box_type,
    uint64_t          precedence,
    void             *destructor
)
{
    isom_box_t *box    = (isom_box_t *)_box;
    isom_box_t *parent = (isom_box_t *)_parent;
    assert( box && parent && parent->root );
    box->class      = &lsmash_box_class;
    box->root       = parent->root;
    box->file       = parent->file;
    box->parent     = parent;
    box->precedence = precedence;
    box->destruct   = destructor ? destructor : NULL;
    box->size       = 0;
    box->type       = box_type;
    if( !lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STSD ) && isom_is_fullbox( box ) )
    {
        box->version = 0;
        box->flags   = 0;
    }
    isom_set_box_writer( box );
}

static void isom_reorder_tail_box( isom_box_t *parent )
{
    /* Reorder the appended box by 'precedence'. */
    lsmash_entry_t *x = parent->extensions.tail;
    assert( x && x->data );
    uint64_t precedence = ((isom_box_t *)x->data)->precedence;
    for( lsmash_entry_t *y = x->prev; y; y = y->prev )
    {
        isom_box_t *box = (isom_box_t *)y->data;
        if( !box || precedence > box->precedence )
        {
            /* Exchange the entity data of adjacent two entries. */
            y->data = x->data;
            x->data = box;
            x = y;
        }
        else
            break;
    }
}

int isom_add_box_to_extension_list( void *parent_box, void *child_box )
{
    assert( parent_box && child_box );
    isom_box_t *parent = (isom_box_t *)parent_box;
    isom_box_t *child  = (isom_box_t *)child_box;
    /* Append at the end of the list. */
    if( lsmash_add_entry( &parent->extensions, child ) < 0 )
        return -1;
    /* Don't reorder the appended box when the file is opened for reading. */
    if( !parent->file || (parent->file->flags & LSMASH_FILE_MODE_READ) || parent->file->fake_file_mode )
        return 0;
    isom_reorder_tail_box( parent );
    return 0;
}

void isom_bs_put_basebox_common( lsmash_bs_t *bs, isom_box_t *box )
{
    if( box->size > UINT32_MAX )
    {
        lsmash_bs_put_be32( bs, 1 );
        lsmash_bs_put_be32( bs, box->type.fourcc );
        lsmash_bs_put_be64( bs, box->size );    /* largesize */
    }
    else
    {
        lsmash_bs_put_be32( bs, (uint32_t)box->size );
        lsmash_bs_put_be32( bs, box->type.fourcc );
    }
    if( box->type.fourcc == ISOM_BOX_TYPE_UUID.fourcc )
    {
        lsmash_bs_put_be32( bs, box->type.user.fourcc );
        lsmash_bs_put_bytes( bs, 12, box->type.user.id );
    }
}

void isom_bs_put_fullbox_common( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_bs_put_basebox_common( bs, box );
    lsmash_bs_put_byte( bs, box->version );
    lsmash_bs_put_be24( bs, box->flags );
}

void isom_bs_put_box_common( lsmash_bs_t *bs, void *box )
{
    if( !box )
    {
        bs->error = 1;
        return;
    }
    isom_box_t *parent = ((isom_box_t *)box)->parent;
    if( parent && lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STSD ) )
    {
        isom_bs_put_basebox_common( bs, (isom_box_t *)box );
        return;
    }
    if( isom_is_fullbox( box ) )
        isom_bs_put_fullbox_common( bs, (isom_box_t *)box );
    else
        isom_bs_put_basebox_common( bs, (isom_box_t *)box );
}

/* Return 1 if the box is fullbox, Otherwise return 0. */
int isom_is_fullbox( void *box )
{
    isom_box_t *current = (isom_box_t *)box;
    lsmash_box_type_t type = current->type;
    static lsmash_box_type_t fullbox_type_table[50] = { LSMASH_BOX_TYPE_INITIALIZER };
    if( !lsmash_check_box_type_specified( &fullbox_type_table[0] ) )
    {
        /* Initialize the table. */
        int i = 0;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_SIDX;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_MVHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_TKHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_IODS;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_ESDS;
        fullbox_type_table[i++] = QT_BOX_TYPE_ESDS;
        fullbox_type_table[i++] = QT_BOX_TYPE_CLEF;
        fullbox_type_table[i++] = QT_BOX_TYPE_PROF;
        fullbox_type_table[i++] = QT_BOX_TYPE_ENOF;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_ELST;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_MDHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_HDLR;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_VMHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_SMHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_HMHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_NMHD;
        fullbox_type_table[i++] = QT_BOX_TYPE_GMIN;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_DREF;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_URL;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_STSD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_STSL;
        fullbox_type_table[i++] = QT_BOX_TYPE_CHAN;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_SRAT;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_STTS;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_CTTS;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_CSLG;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_STSS;
        fullbox_type_table[i++] = QT_BOX_TYPE_STPS;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_SDTP;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_STSC;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_STSZ;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_STCO;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_CO64;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_SGPD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_SBGP;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_CHPL;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_META;
        fullbox_type_table[i++] = QT_BOX_TYPE_KEYS;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_MEAN;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_NAME;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_MEHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_TREX;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_MFHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_TFHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_TFDT;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_TRUN;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_TFRA;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_MFRO;
        fullbox_type_table[i]   = LSMASH_BOX_TYPE_UNSPECIFIED;
    }
    for( int i = 0; lsmash_check_box_type_specified( &fullbox_type_table[i] ); i++ )
        if( lsmash_check_box_type_identical( type, fullbox_type_table[i] ) )
            return 1;
    return lsmash_check_box_type_identical( type, ISOM_BOX_TYPE_CPRT )
        && current->parent && lsmash_check_box_type_identical( current->parent->type, ISOM_BOX_TYPE_UDTA );
}

/* Return 1 if the sample type is LPCM audio, Otherwise return 0. */
int isom_is_lpcm_audio( void *box )
{
    isom_box_t *current = (isom_box_t *)box;
    lsmash_box_type_t type = current->type;
    return lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_23NI_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_NONE_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_LPCM_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_SOWT_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_TWOS_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_FL32_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_FL64_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_IN24_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_IN32_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_NOT_SPECIFIED )
        || (lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_RAW_AUDIO ) && (current->manager & LSMASH_AUDIO_DESCRIPTION));
}

int isom_is_qt_audio( lsmash_codec_type_t type )
{
    return lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_23NI_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_MAC3_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_MAC6_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_NONE_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_QDM2_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_QDMC_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_QCLP_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_AC_3_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_AGSM_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_ALAC_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_ALAW_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_CDX2_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_CDX4_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_DVCA_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_DVI_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_FL32_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_FL64_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_IMA4_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_IN24_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_IN32_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_LPCM_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_MP4A_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_RAW_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_SOWT_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_TWOS_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_ULAW_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_VDVA_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_FULLMP3_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_MP3_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_ADPCM2_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_ADPCM17_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_GSM49_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_NOT_SPECIFIED );
}

/* Return 1 if the sample type is uncompressed Y'CbCr video, Otherwise return 0. */
int isom_is_uncompressed_ycbcr( lsmash_codec_type_t type )
{
    return lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_2VUY_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V210_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V216_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V308_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V408_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V410_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_YUV2_VIDEO );
}

int isom_is_waveform_audio( lsmash_box_type_t type )
{
    return lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_ADPCM2_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_ADPCM17_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_GSM49_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_FULLMP3_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_MP3_AUDIO );
}

size_t isom_skip_box_common( uint8_t **p_data )
{
    uint8_t *orig = *p_data;
    uint8_t *data = *p_data;
    uint64_t size = LSMASH_GET_BE32( data );
    data += ISOM_BASEBOX_COMMON_SIZE;
    if( size == 1 )
    {
        size = LSMASH_GET_BE64( data );
        data += 8;
    }
    *p_data = data;
    return data - orig;
}

static void isom_destruct_extension_binary( void *ext )
{
    if( !ext )
        return;
    isom_box_t *box = (isom_box_t *)ext;
    lsmash_free( box->binary );
}

int isom_add_extension_binary
(
    void             *parent_box,
    lsmash_box_type_t box_type,
    uint64_t          precedence,
    uint8_t          *box_data,
    uint32_t          box_size
)
{
    if( !parent_box || !box_data || box_size < ISOM_BASEBOX_COMMON_SIZE
     || !lsmash_check_box_type_specified( &box_type ) )
        return -1;
    isom_box_t *ext = lsmash_malloc_zero( sizeof(isom_box_t) );
    if( !ext )
        return -1;
    isom_box_t *parent = (isom_box_t *)parent_box;
    ext->class      = &lsmash_box_class;
    ext->root       = parent->root;
    ext->file       = parent->file;
    ext->parent     = parent;
    ext->manager    = LSMASH_BINARY_CODED_BOX;
    ext->precedence = precedence;
    ext->size       = box_size;
    ext->type       = box_type;
    ext->binary     = box_data;
    ext->destruct   = isom_destruct_extension_binary;
    if( isom_add_box_to_extension_list( parent, ext ) )
    {
        lsmash_free( ext );
        return -1;
    }
    isom_set_box_writer( ext );
    return 0;
}

static void isom_remove_extension_box( isom_box_t *ext )
{
    if( !ext )
        return;
    if( ext->destruct )
        ext->destruct( ext );
    isom_remove_all_extension_boxes( &ext->extensions );
    lsmash_free( ext );
}

void isom_remove_all_extension_boxes( lsmash_entry_list_t *extensions )
{
    lsmash_remove_entries( extensions, isom_remove_extension_box );
}

isom_box_t *isom_get_extension_box( lsmash_entry_list_t *extensions, lsmash_box_type_t box_type )
{
    for( lsmash_entry_t *entry = extensions->head; entry; entry = entry->next )
    {
        isom_box_t *ext = (isom_box_t *)entry->data;
        if( !ext )
            continue;
        if( lsmash_check_box_type_identical( ext->type, box_type ) )
            return ext;
    }
    return NULL;
}

void *isom_get_extension_box_format( lsmash_entry_list_t *extensions, lsmash_box_type_t box_type )
{
    for( lsmash_entry_t *entry = extensions->head; entry; entry = entry->next )
    {
        isom_box_t *ext = (isom_box_t *)entry->data;
        if( !ext || (ext->manager & LSMASH_BINARY_CODED_BOX) || !lsmash_check_box_type_identical( ext->type, box_type ) )
            continue;
        return ext;
    }
    return NULL;
}

lsmash_entry_t *isom_get_entry_of_box
(
    lsmash_box_t           *parent,
    const lsmash_box_path_t box_path[]
)
{
    if( !parent )
        return NULL;
    lsmash_entry_t *entry = NULL;
    const lsmash_box_path_t *path = &box_path[0];
    while( lsmash_check_box_type_specified( &path->type ) )
    {
        entry = parent->extensions.head;
        if( !entry )
            return NULL;
        parent = NULL;
        uint32_t i      = 1;
        uint32_t number = path->number ? path->number : 1;
        while( entry )
        {
            isom_box_t *box = entry->data;
            if( box && lsmash_check_box_type_identical( path->type, box->type ) )
            {
                if( i == number )
                {
                    /* Found a box. Move to a child box. */
                    parent = box;
                    ++path;
                    break;
                }
                ++i;
            }
            entry = entry->next;
        }
        if( !parent )
            return NULL;
    }
    return entry;
}

/* box destructors */
static void isom_remove_predefined_box( void *opaque_box, size_t offset_of_box )
{
    assert( opaque_box );
    isom_box_t *box = (isom_box_t *)opaque_box;
    if( box->parent )
    {
        isom_box_t **p = (isom_box_t **)(((int8_t *)box->parent) + offset_of_box);
        if( *p == box )
            *p = NULL;
    }
}
#define isom_remove_box( box_name, parent_type ) \
        isom_remove_predefined_box( box_name, offsetof( parent_type, box_name ) )

/* We always free boxes through the extension list of the parent box.
 * Therefore, don't free boxes through any list other than the extension list. */
static void isom_remove_box_in_predefined_list( void *opaque_box, size_t offset_of_list )
{
    assert( opaque_box );
    isom_box_t *box = (isom_box_t *)opaque_box;
    if( box->parent )
    {
        lsmash_entry_list_t *list = (lsmash_entry_list_t *)(((int8_t *)box->parent) + offset_of_list);
        if( list )
            for( lsmash_entry_t *entry = list->head; entry; entry = entry->next )
                if( box == entry->data )
                {
                    /* We don't free this box here.
                     * Because of freeing an entry of the list here, don't pass the list to free this box.
                     * Or double free. */
                    entry->data = NULL;
                    lsmash_remove_entry_direct( list, entry, NULL );
                    break;
                }
    }
}

#define isom_remove_box_in_list( box_name, parent_type ) \
        isom_remove_box_in_predefined_list( box_name, offsetof( parent_type, box_name##_list ) )

/* Remove a box by the pointer containing its address.
 * In addition, remove from the extension list of the parent box if possible.
 * Don't call this function within a function freeing one or more entries of any extension list because of double free.
 * Basically, don't use this function as a callback function. */
void isom_remove_box_by_itself( void *opaque_box )
{
    if( !opaque_box )
        return;
    isom_box_t *box = (isom_box_t *)opaque_box;
    if( box->parent )
    {
        isom_box_t *parent = box->parent;
        for( lsmash_entry_t *entry = parent->extensions.head; entry; entry = entry->next )
            if( box == entry->data )
            {
                /* Free the corresponding entry here, therefore don't call this function as a callback function
                 * if a function frees the same entry later and calls this function. */
                lsmash_remove_entry_direct( &parent->extensions, entry, isom_remove_extension_box );
                return;
            }
    }
    isom_remove_extension_box( box );
}

void isom_remove_unknown_box( isom_unknown_box_t *unknown_box )
{
    if( !unknown_box )
        return;
    if( unknown_box->unknown_field )
        lsmash_free( unknown_box->unknown_field );
}

static void isom_remove_root( lsmash_root_t *root )
{
}

static void isom_remove_file( lsmash_file_t *file )
{
    if( !file )
        return;
#ifdef LSMASH_DEMUXER_ENABLED
    isom_remove_print_funcs( file );
    isom_remove_timelines( file );
#endif
    lsmash_free( file->compatible_brands );
    if( file->bs )
    {
        if( file->bc_fclose && file->bs->stream )
            fclose( file->bs->stream );
        lsmash_bs_cleanup( file->bs );
    }
    if( file->fragment )
    {
        lsmash_remove_list( file->fragment->pool, isom_remove_sample_pool );
        lsmash_free( file->fragment );
    }
    isom_remove_box_in_list( file, lsmash_root_t );
}

static void isom_remove_ftyp( isom_ftyp_t *ftyp )
{
    if( !ftyp )
        return;
    if( ftyp->compatible_brands )
        lsmash_free( ftyp->compatible_brands );
    isom_remove_box( ftyp, lsmash_file_t );
}

static void isom_remove_iods( isom_iods_t *iods )
{
    if( !iods )
        return;
    mp4sys_remove_ObjectDescriptor( iods->OD );
    isom_remove_box( iods, isom_moov_t );
}

static void isom_remove_trak( isom_trak_t *trak )
{
    if( !trak )
        return;
    if( trak->cache )
    {
        isom_remove_sample_pool( trak->cache->chunk.pool );
        lsmash_remove_list( trak->cache->roll.pool, NULL );
        lsmash_free( trak->cache->rap );
        lsmash_free( trak->cache->fragment );
        lsmash_free( trak->cache );
    }
    isom_remove_box_in_list( trak, isom_moov_t );
}

static void isom_remove_tkhd( isom_tkhd_t *tkhd )
{
    if( !tkhd )
        return;
    isom_remove_box( tkhd, isom_trak_t );
}

static void isom_remove_clef( isom_clef_t *clef )
{
    if( !clef )
        return;
    isom_remove_box( clef, isom_tapt_t );
}

static void isom_remove_prof( isom_prof_t *prof )
{
    if( !prof )
        return;
    isom_remove_box( prof, isom_tapt_t );
}

static void isom_remove_enof( isom_enof_t *enof )
{
    if( !enof )
        return;
    isom_remove_box( enof, isom_tapt_t );
}

static void isom_remove_tapt( isom_tapt_t *tapt )
{
    if( !tapt )
        return;
    isom_remove_box( tapt, isom_trak_t );
}

static void isom_remove_elst( isom_elst_t *elst )
{
    if( !elst )
        return;
    lsmash_remove_list( elst->list, NULL );
    isom_remove_box( elst, isom_edts_t );
}

static void isom_remove_edts( isom_edts_t *edts )
{
    if( !edts )
        return;
    isom_remove_box( edts, isom_trak_t );
}

static void isom_remove_track_reference_type( isom_tref_type_t *ref )
{
    if( !ref )
        return;
    if( ref->track_ID )
        lsmash_free( ref->track_ID );
    isom_remove_box_in_predefined_list( ref, offsetof( isom_tref_t, ref_list ) );
}

static void isom_remove_tref( isom_tref_t *tref )
{
    if( !tref )
        return;
    isom_remove_box( tref, isom_trak_t );
}

static void isom_remove_mdhd( isom_mdhd_t *mdhd )
{
    if( !mdhd )
        return;
    isom_remove_box( mdhd, isom_mdia_t );
}

static void isom_remove_vmhd( isom_vmhd_t *vmhd )
{
    if( !vmhd )
        return;
    isom_remove_box( vmhd, isom_minf_t );
}

static void isom_remove_smhd( isom_smhd_t *smhd )
{
    if( !smhd )
        return;
    isom_remove_box( smhd, isom_minf_t );
}

static void isom_remove_hmhd( isom_hmhd_t *hmhd )
{
    if( !hmhd )
        return;
    isom_remove_box( hmhd, isom_minf_t );
}

static void isom_remove_nmhd( isom_nmhd_t *nmhd )
{
    if( !nmhd )
        return;
    isom_remove_box( nmhd, isom_minf_t );
}

static void isom_remove_gmin( isom_gmin_t *gmin )
{
    if( !gmin )
        return;
    isom_remove_box( gmin, isom_gmhd_t );
}

static void isom_remove_text( isom_text_t *text )
{
    if( !text )
        return;
    isom_remove_box( text, isom_gmhd_t );
}

static void isom_remove_gmhd( isom_gmhd_t *gmhd )
{
    if( !gmhd )
        return;
    isom_remove_box( gmhd, isom_minf_t );
}

static void isom_remove_hdlr( isom_hdlr_t *hdlr )
{
    if( !hdlr )
        return;
    if( hdlr->componentName )
        lsmash_free( hdlr->componentName );
    if( hdlr->parent )
    {
        if( lsmash_check_box_type_identical( hdlr->parent->type, ISOM_BOX_TYPE_MDIA ) )
            isom_remove_box( hdlr, isom_mdia_t );
        else if( lsmash_check_box_type_identical( hdlr->parent->type, ISOM_BOX_TYPE_META )
              || lsmash_check_box_type_identical( hdlr->parent->type,   QT_BOX_TYPE_META ) )
            isom_remove_box( hdlr, isom_meta_t );
        else if( lsmash_check_box_type_identical( hdlr->parent->type, ISOM_BOX_TYPE_MINF ) )
            isom_remove_box( hdlr, isom_minf_t );
        else
            assert( 0 );
        return;
    }
}

static void isom_remove_clap( isom_clap_t *clap )
{
}

static void isom_remove_pasp( isom_pasp_t *pasp )
{
}

static void isom_remove_glbl( isom_glbl_t *glbl )
{
    if( !glbl )
        return;
    if( glbl->header_data )
        free( glbl->header_data );
}

static void isom_remove_colr( isom_colr_t *colr )
{
}

static void isom_remove_gama( isom_gama_t *gama )
{
}

static void isom_remove_fiel( isom_fiel_t *fiel )
{
}

static void isom_remove_cspc( isom_cspc_t *cspc )
{
}

static void isom_remove_sgbt( isom_sgbt_t *sgbt )
{
}

static void isom_remove_stsl( isom_stsl_t *stsl )
{
}

static void isom_remove_esds( isom_esds_t *esds )
{
    if( !esds )
        return;
    mp4sys_remove_ES_Descriptor( esds->ES );
}

static void isom_remove_btrt( isom_btrt_t *btrt )
{
}

static void isom_remove_font_record( isom_font_record_t *font_record )
{
    if( !font_record )
        return;
    if( font_record->font_name )
        lsmash_free( font_record->font_name );
    lsmash_free( font_record );
}

static void isom_remove_ftab( isom_ftab_t *ftab )
{
    if( !ftab )
        return;
    lsmash_remove_list( ftab->list, isom_remove_font_record );
    isom_remove_box( ftab, isom_tx3g_entry_t );
}

static void isom_remove_frma( isom_frma_t *frma )
{
    if( !frma )
        return;
    isom_remove_box( frma, isom_wave_t );
}

static void isom_remove_enda( isom_enda_t *enda )
{
    if( !enda )
        return;
    isom_remove_box( enda, isom_wave_t );
}

static void isom_remove_mp4a( isom_mp4a_t *mp4a )
{
    if( !mp4a )
        return;
    isom_remove_box( mp4a, isom_wave_t );
}

static void isom_remove_terminator( isom_terminator_t *terminator )
{
    if( !terminator )
        return;
    isom_remove_box( terminator, isom_wave_t );
}

static void isom_remove_wave( isom_wave_t *wave )
{
}

static void isom_remove_chan( isom_chan_t *chan )
{
    if( !chan )
        return;
    if( chan->channelDescriptions )
        lsmash_free( chan->channelDescriptions );
}

static void isom_remove_srat( isom_srat_t *srat )
{
}

static void isom_remove_stsd( isom_stsd_t *stsd )
{
    if( !stsd )
        return;
    isom_remove_box( stsd, isom_stbl_t );
}

static void isom_remove_visual_description( isom_sample_entry_t *description )
{
    isom_visual_entry_t *visual = (isom_visual_entry_t *)description;
    if( visual->color_table.array )
        lsmash_free( visual->color_table.array );
    isom_remove_box_in_predefined_list( visual, offsetof( isom_stsd_t, list ) );
}

static void isom_remove_audio_description( isom_sample_entry_t *description )
{
    isom_remove_box_in_predefined_list( description, offsetof( isom_stsd_t, list ) );
}

static void isom_remove_hint_description( isom_sample_entry_t *description )
{
    isom_hint_entry_t *hint = (isom_hint_entry_t *)description;
    if( hint->data )
        lsmash_free( hint->data );
    isom_remove_box_in_predefined_list( hint, offsetof( isom_stsd_t, list ) );
}

static void isom_remove_metadata_description( isom_sample_entry_t *description )
{
    isom_remove_box_in_predefined_list( description, offsetof( isom_stsd_t, list ) );
}

static void isom_remove_tx3g_description( isom_sample_entry_t *description )
{
    isom_remove_box_in_predefined_list( description, offsetof( isom_stsd_t, list ) );
}

static void isom_remove_qt_text_description( isom_sample_entry_t *description )
{
    isom_qt_text_entry_t *text = (isom_qt_text_entry_t *)description;
    if( text->font_name )
        free( text->font_name );
    isom_remove_box_in_predefined_list( text, offsetof( isom_stsd_t, list ) );
}

static void isom_remove_mp4s_description( isom_sample_entry_t *description )
{
    isom_remove_box_in_predefined_list( description, offsetof( isom_stsd_t, list ) );
}

void isom_remove_sample_description( isom_sample_entry_t *sample )
{
    if( !sample )
        return;
    lsmash_codec_type_t sample_type = sample->type;
    if( lsmash_check_box_type_identical( sample_type, LSMASH_CODEC_TYPE_RAW ) )
    {
        if( sample->manager & LSMASH_VIDEO_DESCRIPTION )
        {
            isom_remove_visual_description( sample );
            return;
        }
        else if( sample->manager & LSMASH_AUDIO_DESCRIPTION )
        {
            isom_remove_audio_description( sample );
            return;
        }
    }
    static struct description_remover_table_tag
    {
        lsmash_codec_type_t type;
        void (*func)( isom_sample_entry_t * );
    } description_remover_table[160] = { { LSMASH_CODEC_TYPE_INITIALIZER, NULL } };
    if( !description_remover_table[0].func )
    {
        /* Initialize the table. */
        int i = 0;
#define ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( type, func ) \
    description_remover_table[i++] = (struct description_remover_table_tag){ type, func }
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVC1_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVC2_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVC3_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVC4_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVCP_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_HVC1_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_HEV1_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SVC1_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MVC1_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MVC2_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MP4V_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DRAC_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_ENCV_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MJP2_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_S263_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_VC_1_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_2VUY_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_CFHD_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DV10_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVOO_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVOR_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVTV_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVVT_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_HD10_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_M105_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_PNTG_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SVQ1_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SVQ3_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SHR0_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SHR1_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SHR2_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SHR3_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SHR4_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_WRLE_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_APCH_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_APCN_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_APCS_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_APCO_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_AP4H_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_CIVD_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DRAC_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVC_VIDEO,  isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVCP_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVPP_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DV5N_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DV5P_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVH2_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVH3_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVH5_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVH6_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVHP_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVHQ_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_FLIC_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_GIF_VIDEO,  isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_H261_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_H263_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_JPEG_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_MJPA_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_MJPB_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_PNG_VIDEO,  isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_RLE_VIDEO,  isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_RPZA_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_TGA_VIDEO,  isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_TIFF_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_ULRA_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_ULRG_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_ULY2_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_ULY0_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_ULH2_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_ULH0_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_V210_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_V216_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_V308_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_V408_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_V410_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_YUV2_VIDEO, isom_remove_visual_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MP4A_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_AC_3_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_ALAC_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSC_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSE_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSH_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSL_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_EC_3_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SAMR_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SAWB_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_23NI_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_NONE_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_LPCM_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SOWT_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_TWOS_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_FL32_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_FL64_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_IN24_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_IN32_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_NOT_SPECIFIED, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DRA1_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_ENCA_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_G719_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_G726_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_M4AE_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MLPA_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SAWP_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SEVC_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SQCP_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SSMV_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_TWOS_AUDIO, isom_remove_audio_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_FDP_HINT,  isom_remove_hint_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_M2TS_HINT, isom_remove_hint_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_PM2T_HINT, isom_remove_hint_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_PRTP_HINT, isom_remove_hint_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_RM2T_HINT, isom_remove_hint_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_RRTP_HINT, isom_remove_hint_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_RSRP_HINT, isom_remove_hint_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_RTP_HINT , isom_remove_hint_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SM2T_HINT, isom_remove_hint_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SRTP_HINT, isom_remove_hint_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_IXSE_META, isom_remove_metadata_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_METT_META, isom_remove_metadata_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_METX_META, isom_remove_metadata_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MLIX_META, isom_remove_metadata_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_OKSD_META, isom_remove_metadata_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SVCM_META, isom_remove_metadata_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_TEXT_META, isom_remove_metadata_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_URIM_META, isom_remove_metadata_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_XML_META,  isom_remove_metadata_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_TX3G_TEXT, isom_remove_tx3g_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_TEXT_TEXT, isom_remove_qt_text_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MP4S_SYSTEM, isom_remove_mp4s_description );
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( LSMASH_CODEC_TYPE_UNSPECIFIED, NULL );
    }
    for( int i = 0; description_remover_table[i].func; i++ )
        if( lsmash_check_codec_type_identical( sample_type, description_remover_table[i].type ) )
        {
            description_remover_table[i].func( sample );
            return;
        }
}

static void isom_remove_stts( isom_stts_t *stts )
{
    if( !stts )
        return;
    lsmash_remove_list( stts->list, NULL );
    isom_remove_box( stts, isom_stbl_t );
}

static void isom_remove_ctts( isom_ctts_t *ctts )
{
    if( !ctts )
        return;
    lsmash_remove_list( ctts->list, NULL );
    isom_remove_box( ctts, isom_stbl_t );
}

static void isom_remove_cslg( isom_cslg_t *cslg )
{
    if( !cslg )
        return;
    isom_remove_box( cslg, isom_stbl_t );
}

static void isom_remove_stsc( isom_stsc_t *stsc )
{
    if( !stsc )
        return;
    lsmash_remove_list( stsc->list, NULL );
    isom_remove_box( stsc, isom_stbl_t );
}

static void isom_remove_stsz( isom_stsz_t *stsz )
{
    if( !stsz )
        return;
    lsmash_remove_list( stsz->list, NULL );
    isom_remove_box( stsz, isom_stbl_t );
}

static void isom_remove_stss( isom_stss_t *stss )
{
    if( !stss )
        return;
    lsmash_remove_list( stss->list, NULL );
    isom_remove_box( stss, isom_stbl_t );
}

static void isom_remove_stps( isom_stps_t *stps )
{
    if( !stps )
        return;
    lsmash_remove_list( stps->list, NULL );
    isom_remove_box( stps, isom_stbl_t );
}

static void isom_remove_sdtp( isom_sdtp_t *sdtp )
{
    if( !sdtp )
        return;
    lsmash_remove_list( sdtp->list, NULL );
    if( sdtp->parent )
    {
        if( lsmash_check_box_type_identical( sdtp->parent->type, ISOM_BOX_TYPE_STBL ) )
            isom_remove_box( sdtp, isom_stbl_t );
        else if( lsmash_check_box_type_identical( sdtp->parent->type, ISOM_BOX_TYPE_TRAF ) )
            isom_remove_box( sdtp, isom_traf_t );
        else
            assert( 0 );
        return;
    }
}

static void isom_remove_stco( isom_stco_t *stco )
{
    if( !stco )
        return;
    lsmash_remove_list( stco->list, NULL );
    isom_remove_box( stco, isom_stbl_t );
}

static void isom_remove_sgpd( isom_sgpd_t *sgpd )
{
    if( !sgpd )
        return;
    lsmash_remove_list( sgpd->list, NULL );
    isom_remove_box_in_list( sgpd, isom_stbl_t );
}

static void isom_remove_sbgp( isom_sbgp_t *sbgp )
{
    if( !sbgp )
        return;
    lsmash_remove_list( sbgp->list, NULL );
    isom_remove_box_in_list( sbgp, isom_stbl_t );
}

static void isom_remove_stbl( isom_stbl_t *stbl )
{
    if( !stbl )
        return;
    isom_remove_box( stbl, isom_minf_t );
}

static void isom_remove_dref_entry( isom_dref_entry_t *data_entry )
{
    if( !data_entry )
        return;
    lsmash_free( data_entry->name );
    lsmash_free( data_entry->location );
    isom_remove_box_in_predefined_list( data_entry, offsetof( isom_dref_t, list ) );
}

static void isom_remove_dref( isom_dref_t *dref )
{
    if( !dref )
        return;
    isom_remove_box( dref, isom_dinf_t );
}

static void isom_remove_dinf( isom_dinf_t *dinf )
{
    if( !dinf )
        return;
    isom_remove_box( dinf, isom_minf_t );
}

static void isom_remove_minf( isom_minf_t *minf )
{
    if( !minf )
        return;
    isom_remove_box( minf, isom_mdia_t );
}

static void isom_remove_mdia( isom_mdia_t *mdia )
{
    if( !mdia )
        return;
    isom_remove_box( mdia, isom_trak_t );
}

static void isom_remove_chpl( isom_chpl_t *chpl )
{
    if( !chpl )
        return;
    if( !chpl->list )
    {
        lsmash_free( chpl );
        return;
    }
    for( lsmash_entry_t *entry = chpl->list->head; entry; )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        if( data )
        {
            if( data->chapter_name )
                lsmash_free( data->chapter_name );
            lsmash_free( data );
        }
        lsmash_entry_t *next = entry->next;
        lsmash_free( entry );
        entry = next;
    }
    lsmash_free( chpl->list );
    isom_remove_box( chpl, isom_udta_t );
}

static void isom_remove_keys_entry( isom_keys_entry_t *data )
{
    if( !data )
        return;
    if( data->key_value )
        lsmash_free( data->key_value );
    lsmash_free( data );
}

static void isom_remove_keys( isom_keys_t *keys )
{
    if( !keys )
        return;
    lsmash_remove_list( keys->list, isom_remove_keys_entry );
    isom_remove_box( keys, isom_meta_t );
}

static void isom_remove_mean( isom_mean_t *mean )
{
    if( !mean )
        return;
    if( mean->meaning_string )
        lsmash_free( mean->meaning_string );
    isom_remove_box( mean, isom_metaitem_t );
}

static void isom_remove_name( isom_name_t *name )
{
    if( !name )
        return;
    if( name->name )
        lsmash_free( name->name );
    isom_remove_box( name, isom_metaitem_t );
}

static void isom_remove_data( isom_data_t *data )
{
    if( !data )
        return;
    if( data->value )
        lsmash_free( data->value );
    isom_remove_box( data, isom_metaitem_t );
}

static void isom_remove_metaitem( isom_metaitem_t *metaitem )
{
    if( !metaitem )
        return;
    isom_remove_box_in_predefined_list( metaitem, offsetof( isom_ilst_t, metaitem_list ) );
}

static void isom_remove_ilst( isom_ilst_t *ilst )
{
    if( !ilst )
        return;
    isom_remove_box( ilst, isom_meta_t );
}

static void isom_remove_meta( isom_meta_t *meta )
{
    if( !meta )
        return;
    if( meta->parent )
    {
        if( lsmash_check_box_type_identical( meta->parent->type, LSMASH_BOX_TYPE_UNSPECIFIED ) )
            isom_remove_box( meta, lsmash_file_t );
        else if( lsmash_check_box_type_identical( meta->parent->type, ISOM_BOX_TYPE_MOOV ) )
            isom_remove_box( meta, isom_moov_t );
        else if( lsmash_check_box_type_identical( meta->parent->type, ISOM_BOX_TYPE_TRAK ) )
            isom_remove_box( meta, isom_trak_t );
        else if( lsmash_check_box_type_identical( meta->parent->type, ISOM_BOX_TYPE_UDTA ) )
            isom_remove_box( meta, isom_udta_t );
        else
            assert( 0 );
        return;
    }
}

static void isom_remove_cprt( isom_cprt_t *cprt )
{
    if( !cprt )
        return;
    if( cprt->notice )
        lsmash_free( cprt->notice );
    isom_remove_box_in_list( cprt, isom_udta_t );
}

static void isom_remove_udta( isom_udta_t *udta )
{
    if( !udta )
        return;
    if( udta->parent )
    {
        if( lsmash_check_box_type_identical( udta->parent->type, ISOM_BOX_TYPE_MOOV ) )
            isom_remove_box( udta, isom_moov_t );
        else if( lsmash_check_box_type_identical( udta->parent->type, ISOM_BOX_TYPE_TRAK ) )
            isom_remove_box( udta, isom_trak_t );
        else
            assert( 0 );
        return;
    }
}

static void isom_remove_WLOC( isom_WLOC_t *WLOC )
{
    if( !WLOC )
        return;
    isom_remove_box( WLOC, isom_udta_t );
}

static void isom_remove_LOOP( isom_LOOP_t *LOOP )
{
    if( !LOOP )
        return;
    isom_remove_box( LOOP, isom_udta_t );
}

static void isom_remove_SelO( isom_SelO_t *SelO )
{
    if( !SelO )
        return;
    isom_remove_box( SelO, isom_udta_t );
}

static void isom_remove_AllF( isom_AllF_t *AllF )
{
    if( !AllF )
        return;
    isom_remove_box( AllF, isom_udta_t );
}

static void isom_remove_ctab( isom_ctab_t *ctab )
{
    if( !ctab )
        return;
    if( ctab->color_table.array )
        lsmash_free( ctab->color_table.array );
    if( ctab->parent && lsmash_check_box_type_identical( ctab->parent->type, ISOM_BOX_TYPE_MOOV ) )
        isom_remove_box( ctab, isom_moov_t );
}

static void isom_remove_mehd( isom_mehd_t *mehd )
{
    if( !mehd )
        return;
    isom_remove_box( mehd, isom_mvex_t );
}

static void isom_remove_trex( isom_trex_t *trex )
{
    if( !trex )
        return;
    isom_remove_box_in_list( trex, isom_mvex_t );
}

static void isom_remove_mvex( isom_mvex_t *mvex )
{
    if( !mvex )
        return;
    isom_remove_box( mvex, isom_moov_t );
}

static void isom_remove_mvhd( isom_mvhd_t *mvhd )
{
    if( !mvhd )
        return;
    isom_remove_box( mvhd, isom_moov_t );
}

static void isom_remove_moov( isom_moov_t *moov )
{
    if( !moov )
        return;
    isom_remove_box( moov, lsmash_file_t );
}

static void isom_remove_mfhd( isom_mfhd_t *mfhd )
{
    if( !mfhd )
        return;
    isom_remove_box( mfhd, isom_moof_t );
}

static void isom_remove_tfhd( isom_tfhd_t *tfhd )
{
    if( !tfhd )
        return;
    isom_remove_box( tfhd, isom_traf_t );
}

static void isom_remove_tfdt( isom_tfdt_t *tfdt )
{
    if( !tfdt )
        return;
    isom_remove_box( tfdt, isom_traf_t );
}

static void isom_remove_trun( isom_trun_t *trun )
{
    if( !trun )
        return;
    lsmash_remove_list( trun->optional, NULL );
    isom_remove_box_in_list( trun, isom_traf_t );
}

static void isom_remove_traf( isom_traf_t *traf )
{
    if( !traf )
        return;
    isom_remove_box_in_list( traf, isom_moof_t );
}

static void isom_remove_moof( isom_moof_t *moof )
{
    if( !moof )
        return;
    isom_remove_box_in_list( moof, lsmash_file_t );
}

static void isom_remove_mdat( isom_mdat_t *mdat )
{
    if( !mdat )
        return;
    isom_remove_box( mdat, lsmash_file_t );
}

static void isom_remove_free( isom_free_t *skip )
{
    if( !skip )
        return;
    if( skip->data )
        lsmash_free( skip->data );
    isom_remove_predefined_box( skip, offsetof( lsmash_file_t, free ) );
}
#define isom_remove_skip isom_remove_free

static void isom_remove_tfra( isom_tfra_t *tfra )
{
    if( !tfra )
        return;
    lsmash_remove_list( tfra->list, NULL );
    isom_remove_box_in_list( tfra, isom_mfra_t );
}

static void isom_remove_mfro( isom_mfro_t *mfro )
{
    if( !mfro )
        return;
    isom_remove_box( mfro, isom_mfra_t );
}

static void isom_remove_mfra( isom_mfra_t *mfra )
{
    if( !mfra )
        return;
    isom_remove_box( mfra, lsmash_file_t );
}

static void isom_remove_styp( isom_styp_t *styp )
{
    if( !styp )
        return;
    if( styp->compatible_brands )
        lsmash_free( styp->compatible_brands );
    isom_remove_box_in_list( styp, lsmash_file_t );
}

static void isom_remove_sidx( isom_sidx_t *sidx )
{
    if( !sidx )
        return;
    lsmash_remove_list( sidx->list, NULL );
    isom_remove_box_in_list( sidx, lsmash_file_t );
}

/* box size updater */
uint64_t isom_update_box_size( void *opaque_box )
{
    assert( opaque_box );
    isom_box_t *box = (isom_box_t *)opaque_box;
    if( box->manager & LSMASH_WRITTEN_BOX )
        /* No need to calculate the size of this box since the size is already decided and fixed. */
        return box->size;
    uint64_t size = 0;
    if( box->write )
    {
        /* Calculate the size of this box excluding its children with a fake bytestream writer. */
        {
            lsmash_bs_t fake_bs = { NULL };
            if( box->write( &fake_bs, box ) == 0 )
                size = lsmash_bs_get_valid_data_size( &fake_bs );
        }
        /* Calculate the size of the children if no error. */
        if( size >= ISOM_BASEBOX_COMMON_SIZE )
        {
            for( lsmash_entry_t *entry = box->extensions.head; entry; entry = entry->next )
                if( entry->data )
                    size += isom_update_box_size( entry->data );
            /* Check large size. */
            if( size > UINT32_MAX )
                size += 8;
        }
        else
            /* TODO: add error handling. */
            size = 0;
    }
    box->size = size;
    return size;
}

/* box adding functions */
#define CREATE_BOX( box_name, parent, box_type, precedence )                                \
    assert( parent );                                                                       \
    isom_##box_name##_t *box_name = lsmash_malloc_zero( sizeof(isom_##box_name##_t) );      \
    if( !box_name )                                                                         \
        return NULL;                                                                        \
    isom_init_box_common( box_name, parent, box_type, precedence, isom_remove_##box_name ); \
    if( isom_add_box_to_extension_list( parent, box_name ) < 0 )                            \
    {                                                                                       \
        lsmash_free( box_name );                                                            \
        return NULL;                                                                        \
    }

#define CREATE_LIST_BOX( box_name, parent, box_type, precedence )                  \
    CREATE_BOX( box_name, parent, box_type, precedence );                          \
    box_name->list = lsmash_create_entry_list();                                   \
    if( !box_name->list )                                                          \
    {                                                                              \
        lsmash_remove_entry_tail( &(parent)->extensions, isom_remove_##box_name ); \
        return NULL;                                                               \
    }

#define ADD_BOX_TEMPLATE( box_name, parent, box_type, precedence, BOX_CREATOR ) \
    if( !(parent) )                                                             \
        return NULL;                                                            \
    BOX_CREATOR( box_name, parent, box_type, precedence );                      \
    if( !(parent)->box_name )                                                   \
        (parent)->box_name = box_name

#define ADD_BOX_IN_LIST_TEMPLATE( box_name, parent, box_type, precedence, BOX_CREATOR ) \
    BOX_CREATOR( box_name, parent, box_type, precedence );                              \
    if( lsmash_add_entry( &(parent)->box_name##_list, box_name ) < 0 )                  \
    {                                                                                   \
        lsmash_remove_entry_tail( &(parent)->extensions, isom_remove_##box_name );      \
        return NULL;                                                                    \
    }

#define ADD_BOX( box_name, parent, box_type, precedence ) \
        ADD_BOX_TEMPLATE( box_name, parent, box_type, precedence, CREATE_BOX )
#define ADD_BOX_IN_LIST( box_name, parent, box_type, precedence ) \
        ADD_BOX_IN_LIST_TEMPLATE( box_name, parent, box_type, precedence, CREATE_BOX )
#define ADD_LIST_BOX( box_name, parent, box_type, precedence ) \
        ADD_BOX_TEMPLATE( box_name, parent, box_type, precedence, CREATE_LIST_BOX )
#define ADD_LIST_BOX_IN_LIST( box_name, parent, box_type, precedence ) \
        ADD_BOX_IN_LIST_TEMPLATE( box_name, parent, box_type, precedence, CREATE_LIST_BOX )

lsmash_file_t *isom_add_file( lsmash_root_t *root )
{
    lsmash_file_t *file = lsmash_malloc_zero( sizeof(lsmash_file_t) );
    if( !file )
        return NULL;
    file->class    = &lsmash_box_class;
    file->root     = root;
    file->file     = file;
    file->parent   = (isom_box_t *)root;
    file->destruct = (isom_extension_destructor_t)isom_remove_file;
    file->size     = 0;
    file->type     = LSMASH_BOX_TYPE_UNSPECIFIED;
    if( isom_add_box_to_extension_list( root, file ) < 0 )
    {
        lsmash_free( file );
        return NULL;
    }
    if( lsmash_add_entry( &root->file_list, file ) < 0 )
    {
        lsmash_remove_entry_tail( &root->extensions, isom_remove_file );
        return NULL;
    }
    return file;
}

isom_tref_type_t *isom_add_track_reference_type( isom_tref_t *tref, isom_track_reference_type type )
{
    if( !tref )
        return NULL;
    isom_tref_type_t *ref = lsmash_malloc_zero( sizeof(isom_tref_type_t) );
    if( !ref )
        return NULL;
    /* Initialize common fields. */
    ref->root       = tref->root;
    ref->file       = tref->file;
    ref->parent     = (isom_box_t *)tref;
    ref->size       = 0;
    ref->type       = lsmash_form_iso_box_type( type );
    ref->precedence = LSMASH_BOX_PRECEDENCE_ISOM_TREF_TYPE;
    ref->destruct   = (isom_extension_destructor_t)isom_remove_track_reference_type;
    isom_set_box_writer( (isom_box_t *)ref );
    if( isom_add_box_to_extension_list( tref, ref ) < 0 )
    {
        lsmash_free( ref );
        return NULL;
    }
    if( lsmash_add_entry( &tref->ref_list, ref ) < 0 )
    {
        lsmash_remove_entry_tail( &tref->extensions, isom_remove_track_reference_type );
        return NULL;
    }
    return ref;
}

isom_frma_t *isom_add_frma( isom_wave_t *wave )
{
    ADD_BOX( frma, wave, QT_BOX_TYPE_FRMA, LSMASH_BOX_PRECEDENCE_QTFF_FRMA );
    return frma;
}

isom_enda_t *isom_add_enda( isom_wave_t *wave )
{
    ADD_BOX( enda, wave, QT_BOX_TYPE_ENDA, LSMASH_BOX_PRECEDENCE_QTFF_ENDA );
    return enda;
}

isom_mp4a_t *isom_add_mp4a( isom_wave_t *wave )
{
    ADD_BOX( mp4a, wave, QT_BOX_TYPE_MP4A, LSMASH_BOX_PRECEDENCE_QTFF_MP4A );
    return mp4a;
}

isom_terminator_t *isom_add_terminator( isom_wave_t *wave )
{
    ADD_BOX( terminator, wave, QT_BOX_TYPE_TERMINATOR, LSMASH_BOX_PRECEDENCE_QTFF_TERMINATOR );
    return terminator;
}

isom_ftab_t *isom_add_ftab( isom_tx3g_entry_t *tx3g )
{
    ADD_LIST_BOX( ftab, tx3g, ISOM_BOX_TYPE_FTAB, LSMASH_BOX_PRECEDENCE_ISOM_FTAB );
    return ftab;
}

isom_stco_t *isom_add_stco( isom_stbl_t *stbl )
{
    ADD_LIST_BOX( stco, stbl, ISOM_BOX_TYPE_STCO, LSMASH_BOX_PRECEDENCE_ISOM_STCO );
    stco->large_presentation = 0;
    return stco;
}

isom_stco_t *isom_add_co64( isom_stbl_t *stbl )
{
    ADD_LIST_BOX( stco, stbl, ISOM_BOX_TYPE_CO64, LSMASH_BOX_PRECEDENCE_ISOM_CO64 );
    stco->large_presentation = 1;
    return stco;
}

isom_ftyp_t *isom_add_ftyp( lsmash_file_t *file )
{
    ADD_BOX( ftyp, file, ISOM_BOX_TYPE_FTYP, LSMASH_BOX_PRECEDENCE_ISOM_FTYP );
    return ftyp;
}

isom_moov_t *isom_add_moov( lsmash_file_t *file )
{
    ADD_BOX( moov, file, ISOM_BOX_TYPE_MOOV, LSMASH_BOX_PRECEDENCE_ISOM_MOOV );
    return moov;
}

isom_mvhd_t *isom_add_mvhd( isom_moov_t *moov )
{
    ADD_BOX( mvhd, moov, ISOM_BOX_TYPE_MVHD, LSMASH_BOX_PRECEDENCE_ISOM_MVHD );
    return mvhd;
}

isom_iods_t *isom_add_iods( isom_moov_t *moov )
{
    ADD_BOX( iods, moov, ISOM_BOX_TYPE_IODS, LSMASH_BOX_PRECEDENCE_ISOM_IODS );
    return iods;
}

isom_ctab_t *isom_add_ctab( void *parent_box )
{
    /* According to QuickTime File Format Specification, this box is placed inside Movie Box if present.
     * However, sometimes this box occurs inside an image description entry or the end of Sample Description Box. */
    if( !parent_box )
        return NULL;
    isom_box_t *parent = (isom_box_t *)parent_box;
    CREATE_BOX( ctab, parent, QT_BOX_TYPE_CTAB, LSMASH_BOX_PRECEDENCE_QTFF_CTAB );
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MOOV ) )
    {
        isom_moov_t *moov = (isom_moov_t *)parent;
        if( !moov->ctab )
            moov->ctab = ctab;
    }
    return ctab;
}

isom_trak_t *isom_add_trak( isom_moov_t *moov )
{
    if( !moov || !moov->file )
        return NULL;
    CREATE_BOX( trak, moov, ISOM_BOX_TYPE_TRAK, LSMASH_BOX_PRECEDENCE_ISOM_TRAK );
    isom_fragment_t *fragment = NULL;
    isom_cache_t    *cache    = lsmash_malloc_zero( sizeof(isom_cache_t) );
    if( !cache )
        goto fail;
    if( moov->file->fragment )
    {
        fragment = lsmash_malloc_zero( sizeof(isom_fragment_t) );
        if( !fragment )
            goto fail;
        cache->fragment = fragment;
    }
    if( lsmash_add_entry( &moov->trak_list, trak ) < 0 )
        goto fail;
    trak->cache = cache;
    return trak;
fail:
    lsmash_free( fragment );
    lsmash_free( cache );
    lsmash_remove_entry_tail( &moov->extensions, isom_remove_trak );
    return NULL;
}

isom_tkhd_t *isom_add_tkhd( isom_trak_t *trak )
{
    ADD_BOX( tkhd, trak, ISOM_BOX_TYPE_TKHD, LSMASH_BOX_PRECEDENCE_ISOM_TKHD );
    return tkhd;
}

isom_tapt_t *isom_add_tapt( isom_trak_t *trak )
{
    ADD_BOX( tapt, trak, QT_BOX_TYPE_TAPT, LSMASH_BOX_PRECEDENCE_QTFF_TAPT );
    return tapt;
}

isom_clef_t *isom_add_clef( isom_tapt_t *tapt )
{
    ADD_BOX( clef, tapt, QT_BOX_TYPE_CLEF, LSMASH_BOX_PRECEDENCE_QTFF_CLEF );
    return clef;
}

isom_prof_t *isom_add_prof( isom_tapt_t *tapt )
{
    ADD_BOX( prof, tapt, QT_BOX_TYPE_PROF, LSMASH_BOX_PRECEDENCE_QTFF_PROF );
    return prof;
}

isom_enof_t *isom_add_enof( isom_tapt_t *tapt )
{
    ADD_BOX( enof, tapt, QT_BOX_TYPE_ENOF, LSMASH_BOX_PRECEDENCE_QTFF_ENOF );
    return enof;
}

isom_edts_t *isom_add_edts( isom_trak_t *trak )
{
    ADD_BOX( edts, trak, ISOM_BOX_TYPE_EDTS, LSMASH_BOX_PRECEDENCE_ISOM_EDTS );
    return edts;
}

isom_elst_t *isom_add_elst( isom_edts_t *edts )
{
    ADD_LIST_BOX( elst, edts, ISOM_BOX_TYPE_ELST, LSMASH_BOX_PRECEDENCE_ISOM_ELST );
    return elst;
}

isom_tref_t *isom_add_tref( isom_trak_t *trak )
{
    ADD_BOX( tref, trak, ISOM_BOX_TYPE_TREF, LSMASH_BOX_PRECEDENCE_ISOM_TREF );
    return tref;
}

isom_mdia_t *isom_add_mdia( isom_trak_t *trak )
{
    ADD_BOX( mdia, trak, ISOM_BOX_TYPE_MDIA, LSMASH_BOX_PRECEDENCE_ISOM_MDIA );
    return mdia;
}


isom_mdhd_t *isom_add_mdhd( isom_mdia_t *mdia )
{
    ADD_BOX( mdhd, mdia, ISOM_BOX_TYPE_MDHD, LSMASH_BOX_PRECEDENCE_ISOM_MDHD );
    return mdhd;
}

isom_hdlr_t *isom_add_hdlr( void *parent_box )
{
    if( !parent_box )
        return NULL;
    isom_box_t *parent = (isom_box_t *)parent_box;
    CREATE_BOX( hdlr, parent, ISOM_BOX_TYPE_HDLR, LSMASH_BOX_PRECEDENCE_ISOM_HDLR );
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MDIA ) )
    {
        isom_mdia_t *mdia = (isom_mdia_t *)parent;
        if( !mdia->hdlr )
            mdia->hdlr = hdlr;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_META )
          || lsmash_check_box_type_identical( parent->type,   QT_BOX_TYPE_META ) )
    {
        isom_meta_t *meta = (isom_meta_t *)parent;
        if( !meta->hdlr )
            meta->hdlr = hdlr;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MINF ) )
    {
        isom_minf_t *minf = (isom_minf_t *)parent;
        if( !minf->hdlr )
            minf->hdlr = hdlr;
    }
    else
        assert( 0 );
    return hdlr;
}

isom_minf_t *isom_add_minf( isom_mdia_t *mdia )
{
    ADD_BOX( minf, mdia, ISOM_BOX_TYPE_MINF, LSMASH_BOX_PRECEDENCE_ISOM_MINF );
    return minf;
}

isom_vmhd_t *isom_add_vmhd( isom_minf_t *minf )
{
    ADD_BOX( vmhd, minf, ISOM_BOX_TYPE_VMHD, LSMASH_BOX_PRECEDENCE_ISOM_VMHD );
    return vmhd;
}

isom_smhd_t *isom_add_smhd( isom_minf_t *minf )
{
    ADD_BOX( smhd, minf, ISOM_BOX_TYPE_SMHD, LSMASH_BOX_PRECEDENCE_ISOM_SMHD );
    return smhd;
}

isom_hmhd_t *isom_add_hmhd( isom_minf_t *minf )
{
    ADD_BOX( hmhd, minf, ISOM_BOX_TYPE_HMHD, LSMASH_BOX_PRECEDENCE_ISOM_HMHD );
    return hmhd;
}

isom_nmhd_t *isom_add_nmhd( isom_minf_t *minf )
{
    ADD_BOX( nmhd, minf, ISOM_BOX_TYPE_NMHD, LSMASH_BOX_PRECEDENCE_ISOM_NMHD );
    return nmhd;
}

isom_gmhd_t *isom_add_gmhd( isom_minf_t *minf )
{
    ADD_BOX( gmhd, minf, QT_BOX_TYPE_GMHD, LSMASH_BOX_PRECEDENCE_QTFF_GMHD );
    return gmhd;
}

isom_gmin_t *isom_add_gmin( isom_gmhd_t *gmhd )
{
    ADD_BOX( gmin, gmhd, QT_BOX_TYPE_GMIN, LSMASH_BOX_PRECEDENCE_QTFF_GMIN );
    return gmin;
}

isom_text_t *isom_add_text( isom_gmhd_t *gmhd )
{
    ADD_BOX( text, gmhd, QT_BOX_TYPE_TEXT, LSMASH_BOX_PRECEDENCE_QTFF_TEXT );
    return text;
}

isom_dinf_t *isom_add_dinf( void *parent_box )
{
    if( !parent_box )
        return NULL;
    isom_box_t *parent = (isom_box_t *)parent_box;
    CREATE_BOX( dinf, parent, ISOM_BOX_TYPE_DINF, LSMASH_BOX_PRECEDENCE_ISOM_DINF );
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MINF ) )
    {
        isom_minf_t *minf = (isom_minf_t *)parent;
        if( !minf->dinf )
            minf->dinf = dinf;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_META )
          || lsmash_check_box_type_identical( parent->type,   QT_BOX_TYPE_META ) )
    {
        isom_meta_t *meta = (isom_meta_t *)parent;
        if( !meta->dinf )
            meta->dinf = dinf;
    }
    else
        assert( 0 );
    return dinf;
}

isom_dref_entry_t *isom_add_dref_entry( isom_dref_t *dref )
{
    if( !dref )
        return NULL;
    isom_dref_entry_t *data = lsmash_malloc_zero( sizeof(isom_dref_entry_t) );
    if( !data )
        return NULL;
    isom_init_box_common( data, dref, ISOM_BOX_TYPE_URL, LSMASH_BOX_PRECEDENCE_ISOM_URL, isom_remove_dref_entry );
    if( isom_add_box_to_extension_list( dref, data ) < 0 )
    {
        lsmash_free( data );
        return NULL;
    }
    if( lsmash_add_entry( &dref->list, data ) < 0 )
    {
        lsmash_remove_entry_tail( &dref->extensions, isom_remove_dref_entry );
        return NULL;
    }
    return data;
}

isom_dref_t *isom_add_dref( isom_dinf_t *dinf )
{
    ADD_BOX( dref, dinf, ISOM_BOX_TYPE_DREF, LSMASH_BOX_PRECEDENCE_ISOM_DREF );
    return dref;
}

isom_stbl_t *isom_add_stbl( isom_minf_t *minf )
{
    ADD_BOX( stbl, minf, ISOM_BOX_TYPE_STBL, LSMASH_BOX_PRECEDENCE_ISOM_STBL );
    return stbl;
}

isom_stsd_t *isom_add_stsd( isom_stbl_t *stbl )
{
    ADD_BOX( stsd, stbl, ISOM_BOX_TYPE_STSD, LSMASH_BOX_PRECEDENCE_ISOM_STSD );
    return stsd;
}

static int isom_add_sample_description_entry( isom_stsd_t *stsd, void *description, void *destructor )
{
    if( isom_add_box_to_extension_list( stsd, description ) < 0 )
    {
        lsmash_free( description );
        return -1;
    }
    if( lsmash_add_entry( &stsd->list, description ) < 0 )
    {
        lsmash_remove_entry_tail( &stsd->extensions, destructor );
        return -1;
    }
    return 0;
}

isom_visual_entry_t *isom_add_visual_description( isom_stsd_t *stsd, lsmash_codec_type_t sample_type )
{
    assert( stsd );
    isom_visual_entry_t *visual = lsmash_malloc_zero( sizeof(isom_visual_entry_t) );
    if( !visual )
        return NULL;
    isom_init_box_common( visual, stsd, sample_type, LSMASH_BOX_PRECEDENCE_HM, isom_remove_visual_description );
    visual->manager |= LSMASH_VIDEO_DESCRIPTION;
    return isom_add_sample_description_entry( stsd, visual, isom_remove_visual_description ) ? NULL : visual;
}

isom_audio_entry_t *isom_add_audio_description( isom_stsd_t *stsd, lsmash_codec_type_t sample_type )
{
    assert( stsd );
    isom_audio_entry_t *audio = lsmash_malloc_zero( sizeof(isom_audio_entry_t) );
    if( !audio )
        return NULL;
    isom_init_box_common( audio, stsd, sample_type, LSMASH_BOX_PRECEDENCE_HM, isom_remove_audio_description );
    audio->manager |= LSMASH_AUDIO_DESCRIPTION;
    return isom_add_sample_description_entry( stsd, audio, isom_remove_audio_description ) ? NULL : audio;
}

isom_qt_text_entry_t *isom_add_qt_text_description( isom_stsd_t *stsd )
{
    assert( stsd );
    isom_qt_text_entry_t *text = lsmash_malloc_zero( sizeof(isom_qt_text_entry_t) );
    if( !text )
        return NULL;
    isom_init_box_common( text, stsd, QT_CODEC_TYPE_TEXT_TEXT, LSMASH_BOX_PRECEDENCE_HM, isom_remove_qt_text_description );
    return isom_add_sample_description_entry( stsd, text, isom_remove_qt_text_description ) ? NULL : text;
}

isom_tx3g_entry_t *isom_add_tx3g_description( isom_stsd_t *stsd )
{
    assert( stsd );
    isom_tx3g_entry_t *tx3g = lsmash_malloc_zero( sizeof(isom_tx3g_entry_t) );
    if( !tx3g )
        return NULL;
    isom_init_box_common( tx3g, stsd, ISOM_CODEC_TYPE_TX3G_TEXT, LSMASH_BOX_PRECEDENCE_HM, isom_remove_tx3g_description );
    return isom_add_sample_description_entry( stsd, tx3g, isom_remove_tx3g_description ) ? NULL : tx3g;
}

isom_esds_t *isom_add_esds( void *parent_box )
{
    isom_box_t *parent = (isom_box_t *)parent_box;
    int is_qt = lsmash_check_box_type_identical( parent->type, QT_BOX_TYPE_WAVE );
    lsmash_box_type_t box_type   = is_qt ? QT_BOX_TYPE_ESDS : ISOM_BOX_TYPE_ESDS;
    uint64_t          precedence = is_qt ? LSMASH_BOX_PRECEDENCE_QTFF_ESDS : LSMASH_BOX_PRECEDENCE_ISOM_ESDS;
    CREATE_BOX( esds, parent, box_type, precedence );
    return esds;
}

isom_glbl_t *isom_add_glbl( void *parent_box )
{
    CREATE_BOX( glbl, (isom_box_t *)parent_box, QT_BOX_TYPE_GLBL, LSMASH_BOX_PRECEDENCE_QTFF_GLBL );
    return glbl;
}

isom_clap_t *isom_add_clap( isom_visual_entry_t *visual )
{
    CREATE_BOX( clap, visual, ISOM_BOX_TYPE_CLAP, LSMASH_BOX_PRECEDENCE_ISOM_CLAP );
    return clap;
}

isom_pasp_t *isom_add_pasp( isom_visual_entry_t *visual )
{
    CREATE_BOX( pasp, visual, ISOM_BOX_TYPE_PASP, LSMASH_BOX_PRECEDENCE_ISOM_PASP );
    return pasp;
}

isom_colr_t *isom_add_colr( isom_visual_entry_t *visual )
{
    CREATE_BOX( colr, visual, ISOM_BOX_TYPE_COLR, LSMASH_BOX_PRECEDENCE_ISOM_COLR );
    return colr;
}

isom_gama_t *isom_add_gama( isom_visual_entry_t *visual )
{
    CREATE_BOX( gama, visual, QT_BOX_TYPE_GAMA, LSMASH_BOX_PRECEDENCE_QTFF_GAMA );
    return gama;
}

isom_fiel_t *isom_add_fiel( isom_visual_entry_t *visual )
{
    CREATE_BOX( fiel, visual, QT_BOX_TYPE_FIEL, LSMASH_BOX_PRECEDENCE_QTFF_FIEL );
    return fiel;
}

isom_cspc_t *isom_add_cspc( isom_visual_entry_t *visual )
{
    CREATE_BOX( cspc, visual, QT_BOX_TYPE_CSPC, LSMASH_BOX_PRECEDENCE_QTFF_CSPC );
    return cspc;
}

isom_sgbt_t *isom_add_sgbt( isom_visual_entry_t *visual )
{
    CREATE_BOX( sgbt, visual, QT_BOX_TYPE_SGBT, LSMASH_BOX_PRECEDENCE_QTFF_SGBT );
    return sgbt;
}

isom_stsl_t *isom_add_stsl( isom_visual_entry_t *visual )
{
    CREATE_BOX( stsl, visual, ISOM_BOX_TYPE_STSL, LSMASH_BOX_PRECEDENCE_ISOM_STSL );
    return stsl;
}

isom_btrt_t *isom_add_btrt( isom_visual_entry_t *visual )
{
    CREATE_BOX( btrt, visual, ISOM_BOX_TYPE_BTRT, LSMASH_BOX_PRECEDENCE_ISOM_BTRT );
    return btrt;
}

isom_wave_t *isom_add_wave( isom_audio_entry_t *audio )
{
    CREATE_BOX( wave, audio, QT_BOX_TYPE_WAVE, LSMASH_BOX_PRECEDENCE_QTFF_WAVE );
    return wave;
}

isom_chan_t *isom_add_chan( isom_audio_entry_t *audio )
{
    CREATE_BOX( chan, audio, QT_BOX_TYPE_CHAN, LSMASH_BOX_PRECEDENCE_QTFF_CHAN );
    return chan;
}

isom_srat_t *isom_add_srat( isom_audio_entry_t *audio )
{
    CREATE_BOX( srat, audio, ISOM_BOX_TYPE_SRAT, LSMASH_BOX_PRECEDENCE_ISOM_SRAT );
    return srat;
}

isom_stts_t *isom_add_stts( isom_stbl_t *stbl )
{
    ADD_LIST_BOX( stts, stbl, ISOM_BOX_TYPE_STTS, LSMASH_BOX_PRECEDENCE_ISOM_STTS );
    return stts;
}

isom_ctts_t *isom_add_ctts( isom_stbl_t *stbl )
{
    ADD_LIST_BOX( ctts, stbl, ISOM_BOX_TYPE_CTTS, LSMASH_BOX_PRECEDENCE_ISOM_CTTS );
    return ctts;
}

isom_cslg_t *isom_add_cslg( isom_stbl_t *stbl )
{
    ADD_BOX( cslg, stbl, ISOM_BOX_TYPE_CSLG, LSMASH_BOX_PRECEDENCE_ISOM_CSLG );
    return cslg;
}

isom_stsc_t *isom_add_stsc( isom_stbl_t *stbl )
{
    ADD_LIST_BOX( stsc, stbl, ISOM_BOX_TYPE_STSC, LSMASH_BOX_PRECEDENCE_ISOM_STSC );
    return stsc;
}

isom_stsz_t *isom_add_stsz( isom_stbl_t *stbl )
{
    /* We don't create a list here. */
    ADD_BOX( stsz, stbl, ISOM_BOX_TYPE_STSZ, LSMASH_BOX_PRECEDENCE_ISOM_STSZ );
    return stsz;
}

isom_stss_t *isom_add_stss( isom_stbl_t *stbl )
{
    ADD_LIST_BOX( stss, stbl, ISOM_BOX_TYPE_STSS, LSMASH_BOX_PRECEDENCE_ISOM_STSS );
    return stss;
}

isom_stps_t *isom_add_stps( isom_stbl_t *stbl )
{
    ADD_LIST_BOX( stps, stbl, QT_BOX_TYPE_STPS, LSMASH_BOX_PRECEDENCE_QTFF_STPS );
    return stps;
}

isom_sdtp_t *isom_add_sdtp( isom_box_t *parent )
{
    if( !parent )
        return NULL;
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STBL ) )
    {
        isom_stbl_t *stbl = (isom_stbl_t *)parent;
        ADD_LIST_BOX( sdtp, stbl, ISOM_BOX_TYPE_SDTP, LSMASH_BOX_PRECEDENCE_ISOM_SDTP );
        return sdtp;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAF ) )
    {
        isom_traf_t *traf = (isom_traf_t *)parent;
        ADD_LIST_BOX( sdtp, traf, ISOM_BOX_TYPE_SDTP, LSMASH_BOX_PRECEDENCE_ISOM_SDTP );
        return sdtp;
    }
    assert( 0 );
    return NULL;
}

isom_sgpd_t *isom_add_sgpd( void *parent_box )
{
    if( !parent_box )
        return NULL;
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STBL ) )
    {
        isom_stbl_t *stbl = (isom_stbl_t *)parent;
        ADD_LIST_BOX_IN_LIST( sgpd, stbl, ISOM_BOX_TYPE_SGPD, LSMASH_BOX_PRECEDENCE_ISOM_SGPD );
        return sgpd;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAF ) )
    {
        isom_traf_t *traf = (isom_traf_t *)parent;
        ADD_LIST_BOX_IN_LIST( sgpd, traf, ISOM_BOX_TYPE_SGPD, LSMASH_BOX_PRECEDENCE_ISOM_SGPD );
        return sgpd;
    }
    assert( 0 );
    return NULL;
}

isom_sbgp_t *isom_add_sbgp( void *parent_box )
{
    if( !parent_box )
        return NULL;
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STBL ) )
    {
        isom_stbl_t *stbl = (isom_stbl_t *)parent;
        ADD_LIST_BOX_IN_LIST( sbgp, stbl, ISOM_BOX_TYPE_SBGP, LSMASH_BOX_PRECEDENCE_ISOM_SBGP );
        return sbgp;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAF ) )
    {
        isom_traf_t *traf = (isom_traf_t *)parent;
        ADD_LIST_BOX_IN_LIST( sbgp, traf, ISOM_BOX_TYPE_SBGP, LSMASH_BOX_PRECEDENCE_ISOM_SBGP );
        return sbgp;
    }
    assert( 0 );
    return NULL;
}

isom_chpl_t *isom_add_chpl( isom_udta_t *udta )
{
    ADD_LIST_BOX( chpl, udta, ISOM_BOX_TYPE_CHPL, LSMASH_BOX_PRECEDENCE_ISOM_CHPL );
    return chpl;
}

isom_metaitem_t *isom_add_metaitem( isom_ilst_t *ilst, lsmash_itunes_metadata_item item )
{
    if( !ilst )
        return NULL;
    lsmash_box_type_t type = lsmash_form_iso_box_type( item );
    ADD_BOX_IN_LIST( metaitem, ilst, type, LSMASH_BOX_PRECEDENCE_ISOM_METAITEM );
    return metaitem;
}

isom_mean_t *isom_add_mean( isom_metaitem_t *metaitem )
{
    ADD_BOX( mean, metaitem, ISOM_BOX_TYPE_MEAN, LSMASH_BOX_PRECEDENCE_ISOM_MEAN );
    return mean;
}

isom_name_t *isom_add_name( isom_metaitem_t *metaitem )
{
    ADD_BOX( name, metaitem, ISOM_BOX_TYPE_NAME, LSMASH_BOX_PRECEDENCE_ISOM_NAME );
    return name;
}

isom_data_t *isom_add_data( isom_metaitem_t *metaitem )
{
    ADD_BOX( data, metaitem, ISOM_BOX_TYPE_DATA, LSMASH_BOX_PRECEDENCE_ISOM_DATA );
    return data;
}

isom_ilst_t *isom_add_ilst( isom_meta_t *meta )
{
    ADD_BOX( ilst, meta, ISOM_BOX_TYPE_ILST, LSMASH_BOX_PRECEDENCE_ISOM_ILST );
    return ilst;
}

isom_keys_t *isom_add_keys( isom_meta_t *meta )
{
    ADD_LIST_BOX( keys, meta, QT_BOX_TYPE_KEYS, LSMASH_BOX_PRECEDENCE_QTFF_KEYS );
    return keys;
}

isom_meta_t *isom_add_meta( void *parent_box )
{
    if( !parent_box )
        return NULL;
    isom_box_t *parent = (isom_box_t *)parent_box;
    CREATE_BOX( meta, parent, ISOM_BOX_TYPE_META, LSMASH_BOX_PRECEDENCE_ISOM_META );
    if( parent->file == (lsmash_file_t *)parent )
    {
        lsmash_file_t *file = (lsmash_file_t *)parent;
        if( !file->meta )
            file->meta = meta;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MOOV ) )
    {
        isom_moov_t *moov = (isom_moov_t *)parent;
        if( !moov->meta )
            moov->meta = meta;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAK ) )
    {
        isom_trak_t *trak = (isom_trak_t *)parent;
        if( !trak->meta )
            trak->meta = meta;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_UDTA ) )
    {
        isom_udta_t *udta = (isom_udta_t *)parent;
        if( !udta->meta )
            udta->meta = meta;
    }
    else
        assert( 0 );
    return meta;
}

isom_cprt_t *isom_add_cprt( isom_udta_t *udta )
{
    if( !udta )
        return NULL;
    ADD_BOX_IN_LIST( cprt, udta, ISOM_BOX_TYPE_CPRT, LSMASH_BOX_PRECEDENCE_ISOM_CPRT );
    return cprt;
}

isom_udta_t *isom_add_udta( void *parent_box )
{
    if( !parent_box )
        return NULL;
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MOOV ) )
    {
        isom_moov_t *moov = (isom_moov_t *)parent;
        ADD_BOX( udta, moov, ISOM_BOX_TYPE_UDTA, LSMASH_BOX_PRECEDENCE_ISOM_UDTA );
        return udta;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAK ) )
    {
        isom_trak_t *trak = (isom_trak_t *)parent;
        ADD_BOX( udta, trak, ISOM_BOX_TYPE_UDTA, LSMASH_BOX_PRECEDENCE_ISOM_UDTA );
        return udta;
    }
    assert( 0 );
    return NULL;
}

isom_WLOC_t *isom_add_WLOC( isom_udta_t *udta )
{
    ADD_BOX( WLOC, udta, QT_BOX_TYPE_WLOC, LSMASH_BOX_PRECEDENCE_QTFF_WLOC );
    return WLOC;
}

isom_LOOP_t *isom_add_LOOP( isom_udta_t *udta )
{
    ADD_BOX( LOOP, udta, QT_BOX_TYPE_LOOP, LSMASH_BOX_PRECEDENCE_QTFF_LOOP );
    return LOOP;
}

isom_SelO_t *isom_add_SelO( isom_udta_t *udta )
{
    ADD_BOX( SelO, udta, QT_BOX_TYPE_SELO, LSMASH_BOX_PRECEDENCE_QTFF_SELO );
    return SelO;
}

isom_AllF_t *isom_add_AllF( isom_udta_t *udta )
{
    ADD_BOX( AllF, udta, QT_BOX_TYPE_ALLF, LSMASH_BOX_PRECEDENCE_QTFF_ALLF );
    return AllF;
}

isom_mvex_t *isom_add_mvex( isom_moov_t *moov )
{
    ADD_BOX( mvex, moov, ISOM_BOX_TYPE_MVEX, LSMASH_BOX_PRECEDENCE_ISOM_MVEX );
    return mvex;
}

isom_mehd_t *isom_add_mehd( isom_mvex_t *mvex )
{
    ADD_BOX( mehd, mvex, ISOM_BOX_TYPE_MEHD, LSMASH_BOX_PRECEDENCE_ISOM_MEHD );
    return mehd;
}

isom_trex_t *isom_add_trex( isom_mvex_t *mvex )
{
    if( !mvex )
        return NULL;
    ADD_BOX_IN_LIST( trex, mvex, ISOM_BOX_TYPE_TREX, LSMASH_BOX_PRECEDENCE_ISOM_TREX );
    return trex;
}

isom_moof_t *isom_add_moof( lsmash_file_t *file )
{
    if( !file )
        return NULL;
    ADD_BOX_IN_LIST( moof, file, ISOM_BOX_TYPE_MOOF, LSMASH_BOX_PRECEDENCE_ISOM_MOOF );
    return moof;
}

isom_mfhd_t *isom_add_mfhd( isom_moof_t *moof )
{
    ADD_BOX( mfhd, moof, ISOM_BOX_TYPE_MFHD, LSMASH_BOX_PRECEDENCE_ISOM_MFHD );
    return mfhd;
}

isom_traf_t *isom_add_traf( isom_moof_t *moof )
{
    if( !moof )
        return NULL;
    ADD_BOX_IN_LIST( traf, moof, ISOM_BOX_TYPE_TRAF, LSMASH_BOX_PRECEDENCE_ISOM_TRAF );
    return traf;
}

isom_tfhd_t *isom_add_tfhd( isom_traf_t *traf )
{
    ADD_BOX( tfhd, traf, ISOM_BOX_TYPE_TFHD, LSMASH_BOX_PRECEDENCE_ISOM_TFHD );
    return tfhd;
}

isom_tfdt_t *isom_add_tfdt( isom_traf_t *traf )
{
    ADD_BOX( tfdt, traf, ISOM_BOX_TYPE_TFDT, LSMASH_BOX_PRECEDENCE_ISOM_TFDT );
    return tfdt;
}

isom_trun_t *isom_add_trun( isom_traf_t *traf )
{
    if( !traf )
        return NULL;
    ADD_BOX_IN_LIST( trun, traf, ISOM_BOX_TYPE_TRUN, LSMASH_BOX_PRECEDENCE_ISOM_TRUN );
    return trun;
}

isom_mfra_t *isom_add_mfra( lsmash_file_t *file )
{
    ADD_BOX( mfra, file, ISOM_BOX_TYPE_MFRA, LSMASH_BOX_PRECEDENCE_ISOM_MFRA );
    return mfra;
}

isom_tfra_t *isom_add_tfra( isom_mfra_t *mfra )
{
    if( !mfra )
        return NULL;
    ADD_BOX_IN_LIST( tfra, mfra, ISOM_BOX_TYPE_TFRA, LSMASH_BOX_PRECEDENCE_ISOM_TFRA );
    return tfra;
}

isom_mfro_t *isom_add_mfro( isom_mfra_t *mfra )
{
    ADD_BOX( mfro, mfra, ISOM_BOX_TYPE_MFRO, LSMASH_BOX_PRECEDENCE_ISOM_MFRO );
    return mfro;
}

isom_mdat_t *isom_add_mdat( lsmash_file_t *file )
{
    assert( !file->mdat );
    CREATE_BOX( mdat, file, ISOM_BOX_TYPE_MDAT, LSMASH_BOX_PRECEDENCE_ISOM_MDAT );
    file->mdat = mdat;
    return mdat;
}

isom_free_t *isom_add_free( void *parent_box )
{
    if( !parent_box )
        return NULL;
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( parent->file == (lsmash_file_t *)parent )
    {
        lsmash_file_t *file = (lsmash_file_t *)parent;
        CREATE_BOX( skip, file, ISOM_BOX_TYPE_FREE, LSMASH_BOX_PRECEDENCE_ISOM_FREE );
        if( !file->free )
            file->free = skip;
        return skip;
    }
    CREATE_BOX( skip, parent, ISOM_BOX_TYPE_FREE, LSMASH_BOX_PRECEDENCE_ISOM_FREE );
    return skip;
}

isom_styp_t *isom_add_styp( lsmash_file_t *file )
{
    ADD_BOX_IN_LIST( styp, file, ISOM_BOX_TYPE_STYP, LSMASH_BOX_PRECEDENCE_ISOM_STYP );
    return styp;
}

isom_sidx_t *isom_add_sidx( lsmash_file_t *file )
{
    ADD_LIST_BOX_IN_LIST( sidx, file, ISOM_BOX_TYPE_SIDX, LSMASH_BOX_PRECEDENCE_ISOM_SIDX );
    return sidx;
}

#undef CREATE_BOX
#undef CREATE_LIST_BOX
#undef ADD_BOX_TEMPLATE
#undef ADD_BOX_IN_LIST_TEMPLATE
#undef ADD_BOX
#undef ADD_BOX_IN_LIST
#undef ADD_LIST_BOX
#undef ADD_LIST_BOX_IN_LIST

static int fake_file_read
(
    void    *opaque,
    uint8_t *buf,
    int      size
)
{
    fake_file_stream_t *stream = (fake_file_stream_t *)opaque;
    int read_size;
    if( stream->pos + size > stream->size )
        read_size = stream->size - stream->pos;
    else
        read_size = size;
    memcpy( buf, stream->data + stream->pos, read_size );
    stream->pos += read_size;
    return read_size;
}

static int64_t fake_file_seek
(
    void   *opaque,
    int64_t offset,
    int     whence
)
{
    fake_file_stream_t *stream = (fake_file_stream_t *)opaque;
    if( whence == SEEK_SET )
        stream->pos = offset;
    else if( whence == SEEK_CUR )
        stream->pos += offset;
    else if( whence == SEEK_END )
        stream->pos = stream->size + offset;
    return stream->pos;
}

/* Public functions */
lsmash_root_t *lsmash_create_root( void )
{
    lsmash_root_t *root = lsmash_malloc_zero( sizeof(lsmash_root_t) );
    if( !root )
        return NULL;
    root->destruct = (isom_extension_destructor_t)isom_remove_root;
    root->root     = root;
    return root;
}

void lsmash_destroy_root( lsmash_root_t *root )
{
    isom_remove_box_by_itself( root );
}

lsmash_extended_box_type_t lsmash_form_extended_box_type( uint32_t fourcc, const uint8_t id[12] )
{
    return (lsmash_extended_box_type_t){ fourcc, { id[0], id[1], id[2], id[3], id[4],  id[5],
                                                   id[6], id[7], id[8], id[9], id[10], id[11] } };
}

lsmash_box_type_t lsmash_form_iso_box_type( uint32_t fourcc )
{
    return (lsmash_box_type_t){ fourcc, lsmash_form_extended_box_type( fourcc, LSMASH_ISO_12_BYTES ) };
}

lsmash_box_type_t lsmash_form_qtff_box_type( uint32_t fourcc )
{
    return (lsmash_box_type_t){ fourcc, lsmash_form_extended_box_type( fourcc, LSMASH_QTFF_12_BYTES ) };
}

#define CHECK_BOX_TYPE_IDENTICAL( a, b ) \
       a.fourcc      == b.fourcc         \
    && a.user.fourcc == b.user.fourcc    \
    && a.user.id[0]  == b.user.id[0]     \
    && a.user.id[1]  == b.user.id[1]     \
    && a.user.id[2]  == b.user.id[2]     \
    && a.user.id[3]  == b.user.id[3]     \
    && a.user.id[4]  == b.user.id[4]     \
    && a.user.id[5]  == b.user.id[5]     \
    && a.user.id[6]  == b.user.id[6]     \
    && a.user.id[7]  == b.user.id[7]     \
    && a.user.id[8]  == b.user.id[8]     \
    && a.user.id[9]  == b.user.id[9]     \
    && a.user.id[10] == b.user.id[10]    \
    && a.user.id[11] == b.user.id[11]

int lsmash_check_box_type_identical( lsmash_box_type_t a, lsmash_box_type_t b )
{
    return CHECK_BOX_TYPE_IDENTICAL( a, b );
}

int lsmash_check_codec_type_identical( lsmash_codec_type_t a, lsmash_codec_type_t b )
{
    return CHECK_BOX_TYPE_IDENTICAL( a, b );
}

int lsmash_check_box_type_specified( const lsmash_box_type_t *box_type )
{
    assert( box_type );
    if( !box_type )
        return 0;
    return !!(box_type->fourcc
            | box_type->user.fourcc
            | box_type->user.id[0] | box_type->user.id[1] | box_type->user.id[2]  | box_type->user.id[3]
            | box_type->user.id[4] | box_type->user.id[5] | box_type->user.id[6]  | box_type->user.id[7]
            | box_type->user.id[8] | box_type->user.id[9] | box_type->user.id[10] | box_type->user.id[11]);
}

lsmash_box_t *lsmash_get_box
(
    lsmash_box_t           *parent,
    const lsmash_box_path_t box_path[]
)
{
    lsmash_entry_t *entry = isom_get_entry_of_box( parent, box_path );
    return (lsmash_box_t *)(entry ? entry->data : NULL);
}

lsmash_box_t *lsmash_create_box
(
    lsmash_box_type_t type,
    uint8_t          *data,
    uint32_t          size,
    uint64_t          precedence
)
{
    if( !lsmash_check_box_type_specified( &type ) )
        return NULL;
    isom_unknown_box_t *box = lsmash_malloc_zero( sizeof(isom_unknown_box_t) );
    if( !box )
        return NULL;
    if( size && data )
    {
        box->unknown_size  = size;
        box->unknown_field = lsmash_memdup( data, size );
        if( !box->unknown_field )
        {
            lsmash_free( box );
            return NULL;
        }
    }
    else
    {
        box->unknown_size  = 0;
        box->unknown_field = NULL;
        size = 0;
    }
    box->class      = &lsmash_box_class;
    box->root       = NULL;
    box->file       = NULL;
    box->parent     = NULL;
    box->destruct   = (isom_extension_destructor_t)isom_remove_unknown_box;
    box->manager    = LSMASH_UNKNOWN_BOX;
    box->precedence = precedence;
    box->size       = ISOM_BASEBOX_COMMON_SIZE + size + (type.fourcc == ISOM_BOX_TYPE_UUID.fourcc ? 16 : 0);
    box->type       = type;
    isom_set_box_writer( (isom_box_t *)box );
    return (lsmash_box_t *)box;
}

int lsmash_add_box
(
    lsmash_box_t *parent,
    lsmash_box_t *box
)
{
    if( !parent )
        /* You cannot add any box without a box being its parent. */
        return -1;
    if( !box || box->size < ISOM_BASEBOX_COMMON_SIZE )
        return -1;
    if( parent->root == (lsmash_root_t *)parent )
    {
        /* Only files can be added into any ROOT.
         * For backward compatibility, use the active file as the parent. */
        if( parent->file )
            parent = (isom_box_t *)parent->file;
        else
            return -1;
    }
    /* Add a box as a child box. */
    box->root   = parent->root;
    box->file   = parent->file;
    box->parent = parent;
    return isom_add_box_to_extension_list( parent, box );
}

int lsmash_add_box_ex
(
    lsmash_box_t  *parent,
    lsmash_box_t **p_box
)
{
    if( !parent )
        /* You cannot add any box without a box being its parent. */
        return -1;
    isom_unknown_box_t *box = (isom_unknown_box_t *)*p_box;
    if( !box || box->size < ISOM_BASEBOX_COMMON_SIZE )
        return -1;
    if( !(box->manager & LSMASH_UNKNOWN_BOX) )
        /* Simply add the box. */
        return lsmash_add_box( parent, *p_box );
    /* Check if the size of the box to be added is valid. */
    if( box->size != ISOM_BASEBOX_COMMON_SIZE + box->unknown_size + (box->type.fourcc == ISOM_BOX_TYPE_UUID.fourcc ? 16 : 0) )
        return -1;
    if( !parent->file || parent->file == (lsmash_file_t *)box )
        return -1;
    if( parent->root == (lsmash_root_t *)parent )
        /* Only files can be added into any ROOT.
         * For backward compatibility, use the active file as the parent. */
        parent = (isom_box_t *)parent->file;
    /* Switch to the fake-file stream mode. */
    lsmash_file_t *file      = parent->file;
    lsmash_bs_t   *bs_backup = file->bs;
    lsmash_bs_t   *bs        = lsmash_bs_create();
    if( !bs )
        return -1;
    uint8_t *buf = lsmash_malloc( box->size );
    if( !buf )
    {
        lsmash_bs_cleanup( bs );
        return -1;
    }
    fake_file_stream_t fake_file =
        {
            .size = box->size,
            .data = buf,
            .pos  = 0
        };
    bs->stream = &fake_file;
    bs->read   = fake_file_read;
    bs->write  = NULL;
    bs->seek   = fake_file_seek;
    file->bs             = bs;
    file->fake_file_mode = 1;
    /* Make the byte string representing the given box. */
    LSMASH_SET_BE32( &buf[0], box->size );
    LSMASH_SET_BE32( &buf[4], box->type.fourcc );
    if( box->type.fourcc == ISOM_BOX_TYPE_UUID.fourcc )
    {
        LSMASH_SET_BE32( &buf[8], box->type.user.fourcc );
        memcpy( &buf[12], box->type.user.id, 12 );
    }
    memcpy( buf + (uintptr_t)(box->size - box->unknown_size), box->unknown_field, box->unknown_size );
    /* Add a box as a child box and try to expand into struct format. */
    lsmash_box_t dummy = { 0 };
    int ret = isom_read_box( file, &dummy, parent, 0, 0 );
    lsmash_free( buf );
    lsmash_bs_cleanup( bs );
    file->bs             = bs_backup;   /* Switch back to the normal file stream mode. */
    file->fake_file_mode = 0;
    if( ret < 0 )
        return -1;
    /* Reorder the added box by 'precedence'. */
    *p_box = (lsmash_box_t *)parent->extensions.tail->data;
    (*p_box)->precedence = box->precedence;
    isom_reorder_tail_box( parent );
    /* Do also its children by the same way. */
    lsmash_entry_list_t extensions = box->extensions;
    lsmash_init_entry_list( &box->extensions ); /* to avoid freeing the children */
    isom_remove_box_by_itself( box );
    for( lsmash_entry_t *entry = extensions.head; entry; entry = entry->next )
    {
        if( !entry->data )
            continue;
        lsmash_box_t *child = (lsmash_box_t *)entry->data;
        if( lsmash_add_box_ex( *p_box, &child ) == 0 )
        {
            (*p_box)->size += child->size;
            /* Avoid freeing at the end of this function. */
            entry->data = NULL;
        }
    }
    isom_remove_all_extension_boxes( &extensions );
    return 0;
}

void lsmash_destroy_box
(
    lsmash_box_t *box
)
{
    isom_remove_box_by_itself( box );
}

void lsmash_destroy_children
(
    lsmash_box_t *box
)
{
    if( box )
        isom_remove_all_extension_boxes( &box->extensions );
}

int lsmash_get_box_precedence
(
    lsmash_box_t *box,
    uint64_t     *precedence
)
{
    if( !box || !precedence )
        return -1;
    *precedence = box->precedence;
    return 0;
}

lsmash_box_t *lsmash_root_as_box
(
    lsmash_root_t *root
)
{
    return (lsmash_box_t *)root;
}

lsmash_box_t *lsmash_file_as_box
(
    lsmash_file_t *file
)
{
    return (lsmash_box_t *)file;
}

int lsmash_write_top_level_box
(
    lsmash_box_t *box
)
{
    if( !box || (isom_box_t *)box->file != box->parent )
        return -1;
    if( isom_write_box( box->file->bs, box ) < 0 )
        return -1;
    box->file->size += box->size;
    return 0;
}

uint8_t *lsmash_export_box
(
    lsmash_box_t *box,
    uint32_t     *size
)
{
    if( !box || !size )
        return NULL;
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
        return NULL;
    if( isom_write_box( bs, box ) < 0 )
    {
        lsmash_bs_cleanup( bs );
        return NULL;
    }
    *size = bs->buffer.store;
    uint8_t *data = bs->buffer.data;
    bs->buffer.data = NULL;
    lsmash_bs_cleanup( bs );
    return data;
}
