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

#include "internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>

#include "box.h"
#include "mp4a.h"
#include "mp4sys.h"

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

void isom_init_box_common( void *_box, void *_parent, lsmash_box_type_t box_type, void *destructor, void *updater )
{
    isom_box_t *box    = (isom_box_t *)_box;
    isom_box_t *parent = (isom_box_t *)_parent;
    assert( box && parent && parent->root );
    box->class    = &lsmash_box_class;
    box->root     = parent->root;
    box->parent   = parent;
    box->destruct = destructor ? destructor : lsmash_free;
    box->update   = updater;
    box->size     = 0;
    box->type     = box_type;
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STSD ) || !isom_is_fullbox( box ) )
        return;
    box->version = 0;
    box->flags   = 0;
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

/* Return 1 if the sample type is uncompressed Y'CbCr video, Otherwise return 0. */
int isom_is_uncompressed_ycbcr( lsmash_box_type_t type )
{
    return lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_2VUY_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V210_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V216_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V308_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V408_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V410_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_YUV2_VIDEO );
}

size_t isom_skip_box_common( uint8_t **p_data )
{
    uint8_t *orig = *p_data;
    uint8_t *data = *p_data;
    uint64_t size = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    data += ISOM_BASEBOX_COMMON_SIZE;
    if( size == 1 )
    {
        size = ((uint64_t)data[0] << 56) | ((uint64_t)data[1] << 48) | ((uint64_t)data[2] << 40) | ((uint64_t)data[3] << 32)
             | ((uint64_t)data[4] << 24) | ((uint64_t)data[5] << 16) | ((uint64_t)data[6] <<  8) |  (uint64_t)data[7];
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
    lsmash_free( box );
}

int isom_add_extension_binary( void *parent_box, lsmash_box_type_t box_type, uint8_t *box_data, uint32_t box_size )
{
    if( !parent_box || !box_data || box_size < ISOM_BASEBOX_COMMON_SIZE
     || !lsmash_check_box_type_specified( &box_type ) )
        return -1;
    isom_box_t *ext = lsmash_malloc_zero( sizeof(isom_box_t) );
    if( !ext )
        return -1;
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( lsmash_add_entry( &parent->extensions, ext ) )
    {
        lsmash_free( ext );
        return -1;
    }
    ext->class    = &lsmash_box_class;
    ext->root     = parent->root;
    ext->parent   = parent;
    ext->manager  = LSMASH_BINARY_CODED_BOX;
    ext->size     = box_size;
    ext->type     = box_type;
    ext->binary   = box_data;
    ext->destruct = isom_destruct_extension_binary;
    ext->update   = NULL;
    return 0;
}

void isom_remove_extension_box( isom_box_t *ext )
{
    if( !ext )
        return;
    if( ext->destruct )
        ext->destruct( ext );
    else
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
    isom_remove_all_extension_boxes( &box->extensions );
    lsmash_free( box );
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
        lsmash_entry_list_t *list = *(lsmash_entry_list_t **)(((int8_t *)box->parent) + offset_of_list);
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
    /* Free this box actually here. */
    isom_remove_all_extension_boxes( &box->extensions );
    lsmash_free( box );
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
    isom_remove_all_extension_boxes( &unknown_box->extensions );
    lsmash_free( unknown_box );
}

void isom_remove_ftyp( isom_ftyp_t *ftyp )
{
    if( !ftyp )
        return;
    if( ftyp->compatible_brands )
        lsmash_free( ftyp->compatible_brands );
    isom_remove_box( ftyp, lsmash_root_t );
}

void isom_remove_iods( isom_iods_t *iods )
{
    if( !iods )
        return;
    mp4sys_remove_ObjectDescriptor( iods->OD );
    isom_remove_box( iods, isom_moov_t );
}

void isom_remove_trak( isom_trak_t *trak )
{
    if( !trak )
        return;
    if( trak->cache )
    {
        isom_remove_sample_pool( trak->cache->chunk.pool );
        lsmash_remove_list( trak->cache->roll.pool, NULL );
        if( trak->cache->rap )
            lsmash_free( trak->cache->rap );
        lsmash_free( trak->cache );
    }
    isom_remove_box_in_list( trak, isom_moov_t );
}

void isom_remove_tkhd( isom_tkhd_t *tkhd )
{
    if( !tkhd )
        return;
    isom_remove_box( tkhd, isom_trak_t );
}

void isom_remove_clef( isom_clef_t *clef )
{
    if( !clef )
        return;
    isom_remove_box( clef, isom_tapt_t );
}

void isom_remove_prof( isom_prof_t *prof )
{
    if( !prof )
        return;
    isom_remove_box( prof, isom_tapt_t );
}

void isom_remove_enof( isom_enof_t *enof )
{
    if( !enof )
        return;
    isom_remove_box( enof, isom_tapt_t );
}

void isom_remove_tapt( isom_tapt_t *tapt )
{
    if( !tapt )
        return;
    isom_remove_box( tapt, isom_trak_t );
}

void isom_remove_elst( isom_elst_t *elst )
{
    if( !elst )
        return;
    lsmash_remove_list( elst->list, NULL );
    isom_remove_box( elst, isom_edts_t );
}

void isom_remove_edts( isom_edts_t *edts )
{
    if( !edts )
        return;
    isom_remove_box( edts, isom_trak_t );
}

void isom_remove_track_reference_type( isom_tref_type_t *ref )
{
    if( !ref )
        return;
    if( ref->track_ID )
        lsmash_free( ref->track_ID );
    isom_remove_box_in_predefined_list( ref, offsetof( isom_tref_t, ref_list ) );
}

void isom_remove_tref( isom_tref_t *tref )
{
    if( !tref )
        return;
    isom_remove_box( tref, isom_trak_t );
}

void isom_remove_mdhd( isom_mdhd_t *mdhd )
{
    if( !mdhd )
        return;
    isom_remove_box( mdhd, isom_mdia_t );
}

void isom_remove_vmhd( isom_vmhd_t *vmhd )
{
    if( !vmhd )
        return;
    isom_remove_box( vmhd, isom_minf_t );
}

void isom_remove_smhd( isom_smhd_t *smhd )
{
    if( !smhd )
        return;
    isom_remove_box( smhd, isom_minf_t );
}

void isom_remove_hmhd( isom_hmhd_t *hmhd )
{
    if( !hmhd )
        return;
    isom_remove_box( hmhd, isom_minf_t );
}

void isom_remove_nmhd( isom_nmhd_t *nmhd )
{
    if( !nmhd )
        return;
    isom_remove_box( nmhd, isom_minf_t );
}

void isom_remove_gmin( isom_gmin_t *gmin )
{
    if( !gmin )
        return;
    isom_remove_box( gmin, isom_gmhd_t );
}

void isom_remove_text( isom_text_t *text )
{
    if( !text )
        return;
    isom_remove_box( text, isom_gmhd_t );
}

void isom_remove_gmhd( isom_gmhd_t *gmhd )
{
    if( !gmhd )
        return;
    isom_remove_box( gmhd, isom_minf_t );
}

void isom_remove_hdlr( isom_hdlr_t *hdlr )
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
    isom_remove_all_extension_boxes( &hdlr->extensions );
    lsmash_free( hdlr );
}

void isom_remove_clap( isom_clap_t *clap )
{
    if( !clap )
        return;
    isom_remove_all_extension_boxes( &clap->extensions );
    lsmash_free( clap );
}

void isom_remove_pasp( isom_pasp_t *pasp )
{
    if( !pasp )
        return;
    isom_remove_all_extension_boxes( &pasp->extensions );
    lsmash_free( pasp );
}

void isom_remove_glbl( isom_glbl_t *glbl )
{
    if( !glbl )
        return;
    if( glbl->header_data )
        free( glbl->header_data );
    isom_remove_all_extension_boxes( &glbl->extensions );
    lsmash_free( glbl );
}

void isom_remove_colr( isom_colr_t *colr )
{
    if( !colr )
        return;
    isom_remove_all_extension_boxes( &colr->extensions );
    lsmash_free( colr );
}

void isom_remove_gama( isom_gama_t *gama )
{
    if( !gama )
        return;
    isom_remove_all_extension_boxes( &gama->extensions );
    lsmash_free( gama );
}

void isom_remove_fiel( isom_fiel_t *fiel )
{
    if( !fiel )
        return;
    isom_remove_all_extension_boxes( &fiel->extensions );
    lsmash_free( fiel );
}

void isom_remove_cspc( isom_cspc_t *cspc )
{
    if( !cspc )
        return;
    isom_remove_all_extension_boxes( &cspc->extensions );
    lsmash_free( cspc );
}

void isom_remove_sgbt( isom_sgbt_t *sgbt )
{
    if( !sgbt )
        return;
    isom_remove_all_extension_boxes( &sgbt->extensions );
    lsmash_free( sgbt );
}

void isom_remove_stsl( isom_stsl_t *stsl )
{
    if( !stsl )
        return;
    isom_remove_all_extension_boxes( &stsl->extensions );
    lsmash_free( stsl );
}

void isom_remove_esds( isom_esds_t *esds )
{
    if( !esds )
        return;
    mp4sys_remove_ES_Descriptor( esds->ES );
    isom_remove_all_extension_boxes( &esds->extensions );
    lsmash_free( esds );
}

void isom_remove_btrt( isom_btrt_t *btrt )
{
    if( !btrt )
        return;
    isom_remove_all_extension_boxes( &btrt->extensions );
    lsmash_free( btrt );
}

static void isom_remove_font_record( isom_font_record_t *font_record )
{
    if( !font_record )
        return;
    if( font_record->font_name )
        lsmash_free( font_record->font_name );
    lsmash_free( font_record );
}

void isom_remove_ftab( isom_ftab_t *ftab )
{
    if( !ftab )
        return;
    lsmash_remove_list( ftab->list, isom_remove_font_record );
    isom_remove_box( ftab, isom_tx3g_entry_t );
}

void isom_remove_frma( isom_frma_t *frma )
{
    if( !frma )
        return;
    isom_remove_box( frma, isom_wave_t );
}

void isom_remove_enda( isom_enda_t *enda )
{
    if( !enda )
        return;
    isom_remove_box( enda, isom_wave_t );
}

void isom_remove_mp4a( isom_mp4a_t *mp4a )
{
    if( !mp4a )
        return;
    isom_remove_box( mp4a, isom_wave_t );
}

void isom_remove_terminator( isom_terminator_t *terminator )
{
    if( !terminator )
        return;
    isom_remove_box( terminator, isom_wave_t );
}

void isom_remove_wave( isom_wave_t *wave )
{
    if( !wave )
        return;
    isom_remove_all_extension_boxes( &wave->extensions );
    lsmash_free( wave );
}

void isom_remove_chan( isom_chan_t *chan )
{
    if( !chan )
        return;
    if( chan->channelDescriptions )
        lsmash_free( chan->channelDescriptions );
    isom_remove_all_extension_boxes( &chan->extensions );
    lsmash_free( chan );
}

void isom_remove_srat( isom_srat_t *srat )
{
    if( !srat )
        return;
    isom_remove_all_extension_boxes( &srat->extensions );
    lsmash_free( srat );
}

void isom_remove_stsd( isom_stsd_t *stsd )
{
    if( !stsd )
        return;
    isom_remove_box( stsd, isom_stbl_t );
}

void isom_remove_visual_description( isom_sample_entry_t *description )
{
    isom_visual_entry_t *visual = (isom_visual_entry_t *)description;
    isom_remove_all_extension_boxes( &visual->extensions );
    if( visual->color_table.array )
        lsmash_free( visual->color_table.array );
    lsmash_free( visual );
}

void isom_remove_audio_description( isom_sample_entry_t *description )
{
    isom_audio_entry_t *audio = (isom_audio_entry_t *)description;
    isom_remove_all_extension_boxes( &audio->extensions );
    lsmash_free( audio );
}

void isom_remove_hint_description( isom_sample_entry_t *description )
{
    isom_hint_entry_t *hint = (isom_hint_entry_t *)description;
    isom_remove_all_extension_boxes( &hint->extensions );
    if( hint->data )
        lsmash_free( hint->data );
    lsmash_free( hint );
}

void isom_remove_metadata_description( isom_sample_entry_t *description )
{
    isom_metadata_entry_t *metadata = (isom_metadata_entry_t *)description;
    isom_remove_all_extension_boxes( &metadata->extensions );
    lsmash_free( metadata );
}

void isom_remove_tx3g_description( isom_sample_entry_t *description )
{
    isom_tx3g_entry_t *tx3g = (isom_tx3g_entry_t *)description;
    isom_remove_all_extension_boxes( &tx3g->extensions );
    lsmash_free( tx3g );
}

void isom_remove_qt_text_description( isom_sample_entry_t *description )
{
    isom_text_entry_t *text = (isom_text_entry_t *)description;
    isom_remove_all_extension_boxes( &text->extensions );
    if( text->font_name )
        free( text->font_name );
    lsmash_free( text );
}

void isom_remove_mp4s_description( isom_sample_entry_t *description )
{
    isom_mp4s_entry_t *mp4s = (isom_mp4s_entry_t *)description;
    isom_remove_all_extension_boxes( &mp4s->extensions );
    lsmash_free( mp4s );
}

void isom_remove_stts( isom_stts_t *stts )
{
    if( !stts )
        return;
    lsmash_remove_list( stts->list, NULL );
    isom_remove_box( stts, isom_stbl_t );
}

void isom_remove_ctts( isom_ctts_t *ctts )
{
    if( !ctts )
        return;
    lsmash_remove_list( ctts->list, NULL );
    isom_remove_box( ctts, isom_stbl_t );
}

void isom_remove_cslg( isom_cslg_t *cslg )
{
    if( !cslg )
        return;
    isom_remove_box( cslg, isom_stbl_t );
}

void isom_remove_stsc( isom_stsc_t *stsc )
{
    if( !stsc )
        return;
    lsmash_remove_list( stsc->list, NULL );
    isom_remove_box( stsc, isom_stbl_t );
}

void isom_remove_stsz( isom_stsz_t *stsz )
{
    if( !stsz )
        return;
    lsmash_remove_list( stsz->list, NULL );
    isom_remove_box( stsz, isom_stbl_t );
}

void isom_remove_stss( isom_stss_t *stss )
{
    if( !stss )
        return;
    lsmash_remove_list( stss->list, NULL );
    isom_remove_box( stss, isom_stbl_t );
}

void isom_remove_stps( isom_stps_t *stps )
{
    if( !stps )
        return;
    lsmash_remove_list( stps->list, NULL );
    isom_remove_box( stps, isom_stbl_t );
}

void isom_remove_sdtp( isom_sdtp_t *sdtp )
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
    isom_remove_all_extension_boxes( &sdtp->extensions );
    lsmash_free( sdtp );
}

void isom_remove_stco( isom_stco_t *stco )
{
    if( !stco )
        return;
    lsmash_remove_list( stco->list, NULL );
    isom_remove_box( stco, isom_stbl_t );
}

void isom_remove_sgpd( isom_sgpd_t *sgpd )
{
    if( !sgpd )
        return;
    lsmash_remove_list( sgpd->list, NULL );
    isom_remove_box_in_list( sgpd, isom_stbl_t );
}

void isom_remove_sbgp( isom_sbgp_t *sbgp )
{
    if( !sbgp )
        return;
    lsmash_remove_list( sbgp->list, NULL );
    isom_remove_box_in_list( sbgp, isom_stbl_t );
}

void isom_remove_stbl( isom_stbl_t *stbl )
{
    if( !stbl )
        return;
    isom_remove_box( stbl, isom_minf_t );
}

void isom_remove_dref_entry( isom_dref_entry_t *data_entry )
{
    if( !data_entry )
        return;
    lsmash_free( data_entry->name );
    lsmash_free( data_entry->location );
    isom_remove_box_in_predefined_list( data_entry, offsetof( isom_dref_t, list ) );
}

void isom_remove_dref( isom_dref_t *dref )
{
    if( !dref )
        return;
    if( !dref->list )
    {
        lsmash_free( dref );
        return;
    }
    isom_remove_box( dref, isom_dinf_t );
}

void isom_remove_dinf( isom_dinf_t *dinf )
{
    if( !dinf )
        return;
    isom_remove_box( dinf, isom_minf_t );
}

void isom_remove_minf( isom_minf_t *minf )
{
    if( !minf )
        return;
    isom_remove_box( minf, isom_mdia_t );
}

void isom_remove_mdia( isom_mdia_t *mdia )
{
    if( !mdia )
        return;
    isom_remove_box( mdia, isom_trak_t );
}

void isom_remove_chpl( isom_chpl_t *chpl )
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

void isom_remove_keys( isom_keys_t *keys )
{
    if( !keys )
        return;
    lsmash_remove_list( keys->list, isom_remove_keys_entry );
    isom_remove_box( keys, isom_meta_t );
}

void isom_remove_mean( isom_mean_t *mean )
{
    if( !mean )
        return;
    if( mean->meaning_string )
        lsmash_free( mean->meaning_string );
    isom_remove_box( mean, isom_metaitem_t );
}

void isom_remove_name( isom_name_t *name )
{
    if( !name )
        return;
    if( name->name )
        lsmash_free( name->name );
    isom_remove_box( name, isom_metaitem_t );
}

void isom_remove_data( isom_data_t *data )
{
    if( !data )
        return;
    if( data->value )
        lsmash_free( data->value );
    isom_remove_box( data, isom_metaitem_t );
}

void isom_remove_metaitem( isom_metaitem_t *metaitem )
{
    if( !metaitem )
        return;
    isom_remove_box_in_predefined_list( metaitem, offsetof( isom_ilst_t, item_list ) );
}

void isom_remove_ilst( isom_ilst_t *ilst )
{
    if( !ilst )
        return;
    isom_remove_box( ilst, isom_meta_t );
}

void isom_remove_meta( isom_meta_t *meta )
{
    if( !meta )
        return;
    if( meta->parent )
    {
        if( lsmash_check_box_type_identical( meta->parent->type, LSMASH_BOX_TYPE_UNSPECIFIED ) )
            isom_remove_box( meta, lsmash_root_t );
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
    isom_remove_all_extension_boxes( &meta->extensions );
    lsmash_free( meta );
}

void isom_remove_cprt( isom_cprt_t *cprt )
{
    if( !cprt )
        return;
    if( cprt->notice )
        lsmash_free( cprt->notice );
    isom_remove_box_in_list( cprt, isom_udta_t );
}

void isom_remove_udta( isom_udta_t *udta )
{
    if( !udta )
        return;
    lsmash_remove_list( udta->cprt_list, isom_remove_cprt );
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
    isom_remove_all_extension_boxes( &udta->extensions );
    lsmash_free( udta );
}

void isom_remove_WLOC( isom_WLOC_t *WLOC )
{
    if( !WLOC )
        return;
    isom_remove_box( WLOC, isom_udta_t );
}

void isom_remove_LOOP( isom_LOOP_t *LOOP )
{
    if( !LOOP )
        return;
    isom_remove_box( LOOP, isom_udta_t );
}

void isom_remove_SelO( isom_SelO_t *SelO )
{
    if( !SelO )
        return;
    isom_remove_box( SelO, isom_udta_t );
}

void isom_remove_AllF( isom_AllF_t *AllF )
{
    if( !AllF )
        return;
    isom_remove_box( AllF, isom_udta_t );
}

void isom_remove_ctab( isom_ctab_t *ctab )
{
    if( !ctab )
        return;
    if( ctab->color_table.array )
        lsmash_free( ctab->color_table.array );
    if( ctab->parent && lsmash_check_box_type_identical( ctab->parent->type, ISOM_BOX_TYPE_MOOV ) )
        isom_remove_box( ctab, isom_moov_t );
    else
    {
        isom_remove_all_extension_boxes( &ctab->extensions );
        lsmash_free( ctab );
    }
}

void isom_remove_mehd( isom_mehd_t *mehd )
{
    if( !mehd )
        return;
    isom_remove_box( mehd, isom_mvex_t );
}

void isom_remove_trex( isom_trex_t *trex )
{
    if( !trex )
        return;
    isom_remove_box_in_list( trex, isom_mvex_t );
}

void isom_remove_mvex( isom_mvex_t *mvex )
{
    if( !mvex )
        return;
    isom_remove_box( mvex, isom_moov_t );
}

void isom_remove_mvhd( isom_mvhd_t *mvhd )
{
    if( !mvhd )
        return;
    isom_remove_box( mvhd, isom_moov_t );
}

void isom_remove_moov( isom_moov_t *moov )
{
    if( !moov )
        return;
    isom_remove_box( moov, lsmash_root_t );
}

void isom_remove_mfhd( isom_mfhd_t *mfhd )
{
    if( !mfhd )
        return;
    isom_remove_box( mfhd, isom_moof_t );
}

void isom_remove_tfhd( isom_tfhd_t *tfhd )
{
    if( !tfhd )
        return;
    isom_remove_box( tfhd, isom_traf_t );
}

void isom_remove_tfdt( isom_tfdt_t *tfdt )
{
    if( !tfdt )
        return;
    isom_remove_box( tfdt, isom_traf_t );
}

void isom_remove_trun( isom_trun_t *trun )
{
    if( !trun )
        return;
    lsmash_remove_list( trun->optional, NULL );
    isom_remove_box_in_list( trun, isom_traf_t );
}

void isom_remove_traf( isom_traf_t *traf )
{
    if( !traf )
        return;
    isom_remove_box_in_list( traf, isom_moof_t );
}

void isom_remove_moof( isom_moof_t *moof )
{
    if( !moof )
        return;
    isom_remove_box_in_list( moof, lsmash_root_t );
}

void isom_remove_mdat( isom_mdat_t *mdat )
{
    if( !mdat )
        return;
    isom_remove_box( mdat, lsmash_root_t );
}

void isom_remove_free( isom_free_t *skip )
{
    if( !skip )
        return;
    if( skip->data )
        lsmash_free( skip->data );
    isom_remove_predefined_box( skip, offsetof( lsmash_root_t, free ) );
}
#define isom_remove_skip isom_remove_free

void isom_remove_tfra( isom_tfra_t *tfra )
{
    if( !tfra )
        return;
    lsmash_remove_list( tfra->list, NULL );
    isom_remove_box_in_list( tfra, isom_mfra_t );
}

void isom_remove_mfro( isom_mfro_t *mfro )
{
    if( !mfro )
        return;
    isom_remove_box( mfro, isom_mfra_t );
}

void isom_remove_mfra( isom_mfra_t *mfra )
{
    if( !mfra )
        return;
    isom_remove_box( mfra, lsmash_root_t );
}

/* box size updaters */
#define CHECK_LARGESIZE( x )                       \
    (x)->size += isom_update_extension_boxes( x ); \
    if( (x)->size > UINT32_MAX )                   \
        (x)->size += 8

static uint64_t isom_update_extension_boxes( void *box );

uint64_t isom_update_unknown_box_size( isom_unknown_box_t *unknown_box )
{
    if( !unknown_box )
        return 0;
    unknown_box->size = ISOM_BASEBOX_COMMON_SIZE + unknown_box->unknown_size;
    CHECK_LARGESIZE( unknown_box );
    return unknown_box->size;
}

uint64_t isom_update_ftyp_size( isom_ftyp_t *ftyp )
{
    if( !ftyp )
        return 0;
    ftyp->size = ISOM_BASEBOX_COMMON_SIZE + 8 + ftyp->brand_count * 4;
    CHECK_LARGESIZE( ftyp );
    return ftyp->size;
}

uint64_t isom_update_moov_size( isom_moov_t *moov )
{
    if( !moov )
        return 0;
    moov->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( moov );
    return moov->size;
}

uint64_t isom_update_mvhd_size( isom_mvhd_t *mvhd )
{
    if( !mvhd )
        return 0;
    mvhd->version = 0;
    if( mvhd->creation_time     > UINT32_MAX
     || mvhd->modification_time > UINT32_MAX
     || mvhd->duration          > UINT32_MAX )
        mvhd->version = 1;
    mvhd->size = ISOM_FULLBOX_COMMON_SIZE + 96 + (uint64_t)mvhd->version * 12;
    CHECK_LARGESIZE( mvhd );
    return mvhd->size;
}

uint64_t isom_update_iods_size( isom_iods_t *iods )
{
    if( !iods
     || !iods->OD )
        return 0;
    iods->size = ISOM_FULLBOX_COMMON_SIZE + mp4sys_update_ObjectDescriptor_size( iods->OD );
    CHECK_LARGESIZE( iods );
    return iods->size;
}

uint64_t isom_update_ctab_size( isom_ctab_t *ctab )
{
    if( !ctab )
        return 0;
    ctab->size = ISOM_BASEBOX_COMMON_SIZE + (uint64_t)(1 + ctab->color_table.size + !!ctab->color_table.array) * 8;
    CHECK_LARGESIZE( ctab );
    return ctab->size;
}

uint64_t isom_update_trak_size( isom_trak_t *trak )
{
    if( !trak )
        return 0;
    trak->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( trak );
    return trak->size;
}

uint64_t isom_update_tkhd_size( isom_tkhd_t *tkhd )
{
    if( !tkhd )
        return 0;
    tkhd->version = 0;
    if( tkhd->creation_time     > UINT32_MAX
     || tkhd->modification_time > UINT32_MAX
     || tkhd->duration          > UINT32_MAX )
        tkhd->version = 1;
    tkhd->size = ISOM_FULLBOX_COMMON_SIZE + 80 + (uint64_t)tkhd->version * 12;
    CHECK_LARGESIZE( tkhd );
    return tkhd->size;
}

uint64_t isom_update_tapt_size( isom_tapt_t *tapt )
{
    if( !tapt )
        return 0;
    tapt->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( tapt );
    return tapt->size;
}

uint64_t isom_update_clef_size( isom_clef_t *clef )
{
    if( !clef )
        return 0;
    clef->size = ISOM_FULLBOX_COMMON_SIZE + 8;
    CHECK_LARGESIZE( clef );
    return clef->size;
}

uint64_t isom_update_prof_size( isom_prof_t *prof )
{
    if( !prof )
        return 0;
    prof->size = ISOM_FULLBOX_COMMON_SIZE + 8;
    CHECK_LARGESIZE( prof );
    return prof->size;
}

uint64_t isom_update_enof_size( isom_enof_t *enof )
{
    if( !enof )
        return 0;
    enof->size = ISOM_FULLBOX_COMMON_SIZE + 8;
    CHECK_LARGESIZE( enof );
    return enof->size;
}

uint64_t isom_update_edts_size( isom_edts_t *edts )
{
    if( !edts )
        return 0;
    edts->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( edts );
    return edts->size;
}

uint64_t isom_update_elst_size( isom_elst_t *elst )
{
    if( !elst
     || !elst->list )
        return 0;
    uint32_t i = 0;
    elst->version = 0;
    for( lsmash_entry_t *entry = elst->list->head; entry; entry = entry->next, i++ )
    {
        isom_elst_entry_t *data = (isom_elst_entry_t *)entry->data;
        if( data->segment_duration > UINT32_MAX
         || data->media_time       >  INT32_MAX
         || data->media_time       <  INT32_MIN )
            elst->version = 1;
    }
    elst->size = ISOM_LIST_FULLBOX_COMMON_SIZE + (uint64_t)i * ( elst->version ? 20 : 12 );
    CHECK_LARGESIZE( elst );
    return elst->size;
}

uint64_t isom_update_tref_size( isom_tref_t *tref )
{
    if( !tref )
        return 0;
    tref->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( tref );
    return tref->size;
}

uint64_t isom_update_track_reference_type_size( isom_tref_type_t *ref )
{
    ref->size = ISOM_BASEBOX_COMMON_SIZE + (uint64_t)ref->ref_count * 4;
    CHECK_LARGESIZE( ref );
    return ref->size;
}

uint64_t isom_update_mdia_size( isom_mdia_t *mdia )
{
    if( !mdia )
        return 0;
    mdia->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( mdia );
    return mdia->size;
}

uint64_t isom_update_mdhd_size( isom_mdhd_t *mdhd )
{
    if( !mdhd )
        return 0;
    mdhd->version = 0;
    if( mdhd->creation_time     > UINT32_MAX
     || mdhd->modification_time > UINT32_MAX
     || mdhd->duration          > UINT32_MAX )
        mdhd->version = 1;
    mdhd->size = ISOM_FULLBOX_COMMON_SIZE + 20 + (uint64_t)mdhd->version * 12;
    CHECK_LARGESIZE( mdhd );
    return mdhd->size;
}

uint64_t isom_update_hdlr_size( isom_hdlr_t *hdlr )
{
    if( !hdlr )
        return 0;
    hdlr->size = ISOM_FULLBOX_COMMON_SIZE + 20 + (uint64_t)hdlr->componentName_length;
    CHECK_LARGESIZE( hdlr );
    return hdlr->size;
}

uint64_t isom_update_minf_size( isom_minf_t *minf )
{
    if( !minf )
        return 0;
    minf->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( minf );
    return minf->size;
}

uint64_t isom_update_vmhd_size( isom_vmhd_t *vmhd )
{
    if( !vmhd )
        return 0;
    vmhd->size = ISOM_FULLBOX_COMMON_SIZE + 8;
    CHECK_LARGESIZE( vmhd );
    return vmhd->size;
}

uint64_t isom_update_smhd_size( isom_smhd_t *smhd )
{
    if( !smhd )
        return 0;
    smhd->size = ISOM_FULLBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( smhd );
    return smhd->size;
}

uint64_t isom_update_hmhd_size( isom_hmhd_t *hmhd )
{
    if( !hmhd )
        return 0;
    hmhd->size = ISOM_FULLBOX_COMMON_SIZE + 16;
    CHECK_LARGESIZE( hmhd );
    return hmhd->size;
}

uint64_t isom_update_nmhd_size( isom_nmhd_t *nmhd )
{
    if( !nmhd )
        return 0;
    nmhd->size = ISOM_FULLBOX_COMMON_SIZE;
    CHECK_LARGESIZE( nmhd );
    return nmhd->size;
}

uint64_t isom_update_gmhd_size( isom_gmhd_t *gmhd )
{
    if( !gmhd )
        return 0;
    gmhd->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( gmhd );
    return gmhd->size;
}

uint64_t isom_update_gmin_size( isom_gmin_t *gmin )
{
    if( !gmin )
        return 0;
    gmin->size = ISOM_FULLBOX_COMMON_SIZE + 12;
    CHECK_LARGESIZE( gmin );
    return gmin->size;
}

uint64_t isom_update_text_size( isom_text_t *text )
{
    if( !text )
        return 0;
    text->size = ISOM_BASEBOX_COMMON_SIZE + 36;
    CHECK_LARGESIZE( text );
    return text->size;
}

uint64_t isom_update_dinf_size( isom_dinf_t *dinf )
{
    if( !dinf )
        return 0;
    dinf->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( dinf );
    return dinf->size;
}

uint64_t isom_update_dref_size( isom_dref_t *dref )
{
    if( !dref
     || !dref->list )
        return 0;
    dref->size = ISOM_LIST_FULLBOX_COMMON_SIZE;
    CHECK_LARGESIZE( dref );
    return dref->size;
}

uint64_t isom_update_dref_entry_size( isom_dref_entry_t *urln )
{
    if( !urln )
        return 0;
    urln->size = ISOM_FULLBOX_COMMON_SIZE + (uint64_t)urln->name_length + urln->location_length;
    CHECK_LARGESIZE( urln );
    return urln->size;
}

uint64_t isom_update_stbl_size( isom_stbl_t *stbl )
{
    if( !stbl )
        return 0;
    stbl->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( stbl );
    return stbl->size;
}

uint64_t isom_update_pasp_size( isom_pasp_t *pasp )
{
    if( !pasp )
        return 0;
    pasp->size = ISOM_BASEBOX_COMMON_SIZE + 8;
    CHECK_LARGESIZE( pasp );
    return pasp->size;
}

uint64_t isom_update_clap_size( isom_clap_t *clap )
{
    if( !clap )
        return 0;
    clap->size = ISOM_BASEBOX_COMMON_SIZE + 32;
    CHECK_LARGESIZE( clap );
    return clap->size;
}

uint64_t isom_update_glbl_size( isom_glbl_t *glbl )
{
    if( !glbl )
        return 0;
    glbl->size = ISOM_BASEBOX_COMMON_SIZE + (uint64_t)glbl->header_size;
    CHECK_LARGESIZE( glbl );
    return glbl->size;
}

uint64_t isom_update_colr_size( isom_colr_t *colr )
{
    if( !colr
     || (colr->color_parameter_type != ISOM_COLOR_PARAMETER_TYPE_NCLX
      && colr->color_parameter_type !=   QT_COLOR_PARAMETER_TYPE_NCLC) )
        return 0;
    colr->size = ISOM_BASEBOX_COMMON_SIZE + 10 + (colr->color_parameter_type == ISOM_COLOR_PARAMETER_TYPE_NCLX);
    CHECK_LARGESIZE( colr );
    return colr->size;
}

uint64_t isom_update_gama_size( isom_gama_t *gama )
{
    if( !gama || !gama->parent )
        return 0;
    /* Note: 'gama' box is superseded by 'colr' box.
     * Therefore, writers of QTFF should never write both 'colr' and 'gama' box into an Image Description. */
    if( isom_get_extension_box_format( &((isom_visual_entry_t *)gama->parent)->extensions, QT_BOX_TYPE_COLR ) )
        return 0;
    gama->size = ISOM_BASEBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( gama );
    return gama->size;
}

uint64_t isom_update_fiel_size( isom_fiel_t *fiel )
{
    if( !fiel )
        return 0;
    fiel->size = ISOM_BASEBOX_COMMON_SIZE + 2;
    CHECK_LARGESIZE( fiel );
    return fiel->size;
}

uint64_t isom_update_cspc_size( isom_cspc_t *cspc )
{
    if( !cspc )
        return 0;
    cspc->size = ISOM_BASEBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( cspc );
    return cspc->size;
}

uint64_t isom_update_sgbt_size( isom_sgbt_t *sgbt )
{
    if( !sgbt )
        return 0;
    sgbt->size = ISOM_BASEBOX_COMMON_SIZE + 1;
    CHECK_LARGESIZE( sgbt );
    return sgbt->size;
}

uint64_t isom_update_stsl_size( isom_stsl_t *stsl )
{
    if( !stsl )
        return 0;
    stsl->size = ISOM_FULLBOX_COMMON_SIZE + 6;
    CHECK_LARGESIZE( stsl );
    return stsl->size;
}

uint64_t isom_update_esds_size( isom_esds_t *esds )
{
    if( !esds )
        return 0;
    esds->size = ISOM_FULLBOX_COMMON_SIZE + mp4sys_update_ES_Descriptor_size( esds->ES );
    CHECK_LARGESIZE( esds );
    return esds->size;
}

uint64_t isom_update_btrt_size( isom_btrt_t *btrt )
{
    if( !btrt )
        return 0;
    btrt->size = ISOM_BASEBOX_COMMON_SIZE + 12;
    CHECK_LARGESIZE( btrt );
    return btrt->size;
}

uint64_t isom_update_visual_entry_size( isom_sample_entry_t *description )
{
    if( !description )
        return 0;
    isom_visual_entry_t *visual = (isom_visual_entry_t *)description;
    visual->size = ISOM_BASEBOX_COMMON_SIZE + 78;
    if( visual->color_table_ID == 0 )
        visual->size += (uint64_t)(1 + visual->color_table.size + !!visual->color_table.array) * 8;
    CHECK_LARGESIZE( visual );
    return visual->size;
}

#if 0
static uint64_t isom_update_mp4s_entry_size( isom_sample_entry_t *description )
{
    if( !description || !lsmash_check_box_type_identical( description->type, ISOM_CODEC_TYPE_MP4S_SYSTEM ) )
        return 0;
    isom_mp4s_entry_t *mp4s = (isom_mp4s_entry_t *)description;
    mp4s->size = ISOM_BASEBOX_COMMON_SIZE + 8;
    CHECK_LARGESIZE( mp4s );
    return mp4s->size;
}
#endif

uint64_t isom_update_frma_size( isom_frma_t *frma )
{
    if( !frma )
        return 0;
    frma->size = ISOM_BASEBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( frma );
    return frma->size;
}

uint64_t isom_update_enda_size( isom_enda_t *enda )
{
    if( !enda )
        return 0;
    enda->size = ISOM_BASEBOX_COMMON_SIZE + 2;
    CHECK_LARGESIZE( enda );
    return enda->size;
}

uint64_t isom_update_mp4a_size( isom_mp4a_t *mp4a )
{
    if( !mp4a )
        return 0;
    mp4a->size = ISOM_BASEBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( mp4a );
    return mp4a->size;
}

uint64_t isom_update_terminator_size( isom_terminator_t *terminator )
{
    if( !terminator )
        return 0;
    terminator->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( terminator );
    return terminator->size;
}

uint64_t isom_update_wave_size( isom_wave_t *wave )
{
    if( !wave )
        return 0;
    wave->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( wave );
    return wave->size;
}

uint64_t isom_update_chan_size( isom_chan_t *chan )
{
    if( !chan )
        return 0;
    chan->size = ISOM_FULLBOX_COMMON_SIZE + 12 + 20 * (uint64_t)chan->numberChannelDescriptions;
    CHECK_LARGESIZE( chan );
    return chan->size;
}

uint64_t isom_update_srat_size( isom_srat_t *srat )
{
    if( !srat )
        return 0;
    srat->size = ISOM_FULLBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( srat );
    return srat->size;
}

uint64_t isom_update_audio_entry_size( isom_sample_entry_t *description )
{
    if( !description )
        return 0;
    isom_audio_entry_t *audio = (isom_audio_entry_t *)description;
    audio->size = ISOM_BASEBOX_COMMON_SIZE + 28;
    if( audio->version == 1 )
        audio->size += 16;
    else if( audio->version == 2 )
        audio->size += 36;
    CHECK_LARGESIZE( audio );
    return audio->size;
}

uint64_t isom_update_text_entry_size( isom_sample_entry_t *description )
{
    if( !description )
        return 0;
    isom_text_entry_t *text = (isom_text_entry_t *)description;
    text->size = ISOM_BASEBOX_COMMON_SIZE + 51 + (uint64_t)text->font_name_length;
    CHECK_LARGESIZE( text );
    return text->size;
}

uint64_t isom_update_ftab_size( isom_ftab_t *ftab )
{
    if( !ftab || !ftab->list )
        return 0;
    ftab->size = ISOM_BASEBOX_COMMON_SIZE + 2;
    for( lsmash_entry_t *entry = ftab->list->head; entry; entry = entry->next )
    {
        isom_font_record_t *data = (isom_font_record_t *)entry->data;
        ftab->size += 3 + data->font_name_length;
    }
    CHECK_LARGESIZE( ftab );
    return ftab->size;
}

uint64_t isom_update_tx3g_entry_size( isom_sample_entry_t *description )
{
    if( !description )
        return 0;
    isom_tx3g_entry_t *tx3g = (isom_tx3g_entry_t *)description;
    tx3g->size = ISOM_BASEBOX_COMMON_SIZE + 38;
    CHECK_LARGESIZE( tx3g );
    return tx3g->size;
}

uint64_t isom_update_stsd_size( isom_stsd_t *stsd )
{
    if( !stsd
     || !stsd->list )
        return 0;
    uint64_t size = ISOM_LIST_FULLBOX_COMMON_SIZE;
    for( lsmash_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_sample_entry_t *data        = (isom_sample_entry_t *)entry->data;
        lsmash_codec_type_t  sample_type = (lsmash_codec_type_t)data->type;
        if( lsmash_check_codec_type_identical( sample_type, LSMASH_CODEC_TYPE_RAW ) )
        {
            if( data->manager & LSMASH_VIDEO_DESCRIPTION )
                size += isom_update_visual_entry_size( data );
            else if( data->manager & LSMASH_AUDIO_DESCRIPTION )
                size += isom_update_audio_entry_size( data );
            continue;
        }
        static struct description_update_size_table_tag
        {
            lsmash_codec_type_t type;
            uint64_t (*func)( isom_sample_entry_t * );
        } description_update_size_table[160] = { { LSMASH_CODEC_TYPE_INITIALIZER, NULL } };
        if( !description_update_size_table[0].func )
        {
            /* Initialize the table. */
            int i = 0;
#define ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( type, func ) \
    description_update_size_table[i++] = (struct description_update_size_table_tag){ type, func }
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVC1_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVC2_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVC3_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVC4_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_HVC1_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_HEV1_VIDEO, isom_update_visual_entry_size );
#ifdef LSMASH_DEMUXER_ENABLED
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_MP4V_VIDEO, isom_update_visual_entry_size );
#endif
#if 0
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVCP_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_SVC1_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_MVC1_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_MVC2_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_DRAC_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_ENCV_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_MJP2_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_S263_VIDEO, isom_update_visual_entry_size );
#endif
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_VC_1_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_2VUY_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_APCH_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_APCN_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_APCS_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_APCO_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_AP4H_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_DVC_VIDEO,  isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_DVCP_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_DVPP_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_DV5N_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_DV5P_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_DVH2_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_DVH3_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_DVH5_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_DVH6_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_DVHP_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_DVHQ_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_ULRA_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_ULRG_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_ULY2_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_ULY0_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_ULH2_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_ULH0_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_V210_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_V216_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_V308_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_V408_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_V410_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_YUV2_VIDEO, isom_update_visual_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_MP4A_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_AC_3_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_ALAC_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSC_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSE_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSH_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSL_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_EC_3_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_SAMR_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_SAWB_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_ALAC_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_MP4A_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_23NI_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_NONE_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_LPCM_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_SOWT_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_TWOS_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_FL32_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_FL64_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_IN24_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_IN32_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_NOT_SPECIFIED, isom_update_audio_entry_size );
#if 0
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_DRA1_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_ENCA_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_G719_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_G726_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_M4AE_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_MLPA_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_RAW_AUDIO,  isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_SAWP_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_SEVC_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_SQCP_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_SSMV_AUDIO, isom_update_audio_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_TWOS_AUDIO, isom_update_audio_entry_size );
#endif
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_TX3G_TEXT, isom_update_tx3g_entry_size );
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( QT_CODEC_TYPE_TEXT_TEXT, isom_update_text_entry_size );
#if 0
            ADD_DESCRIPTION_UPDATE_SIZE_TABLE_ELEMENT( ISOM_CODEC_TYPE_MP4S_SYSTEM, isom_update_mp4s_entry_size );
#endif
        }
        for( int i = 0; description_update_size_table[i].func; i++ )
            if( lsmash_check_codec_type_identical( sample_type, description_update_size_table[i].type ) )
            {
                size += description_update_size_table[i].func( data );
                break;
            }
    }
    stsd->size = size;
    CHECK_LARGESIZE( stsd );
    return stsd->size;
}

uint64_t isom_update_stts_size( isom_stts_t *stts )
{
    if( !stts
     || !stts->list )
        return 0;
    stts->size = ISOM_LIST_FULLBOX_COMMON_SIZE + (uint64_t)stts->list->entry_count * 8;
    CHECK_LARGESIZE( stts );
    return stts->size;
}

uint64_t isom_update_ctts_size( isom_ctts_t *ctts )
{
    if( !ctts
     || !ctts->list )
        return 0;
    ctts->size = ISOM_LIST_FULLBOX_COMMON_SIZE + (uint64_t)ctts->list->entry_count * 8;
    CHECK_LARGESIZE( ctts );
    return ctts->size;
}

uint64_t isom_update_cslg_size( isom_cslg_t *cslg )
{
    if( !cslg )
        return 0;
    cslg->size = ISOM_FULLBOX_COMMON_SIZE + 20;
    CHECK_LARGESIZE( cslg );
    return cslg->size;
}

uint64_t isom_update_stsz_size( isom_stsz_t *stsz )
{
    if( !stsz )
        return 0;
    stsz->size = ISOM_FULLBOX_COMMON_SIZE + 8 + ( stsz->list ? (uint64_t)stsz->list->entry_count * 4 : 0 );
    CHECK_LARGESIZE( stsz );
    return stsz->size;
}

uint64_t isom_update_stss_size( isom_stss_t *stss )
{
    if( !stss
     || !stss->list )
        return 0;
    stss->size = ISOM_LIST_FULLBOX_COMMON_SIZE + (uint64_t)stss->list->entry_count * 4;
    CHECK_LARGESIZE( stss );
    return stss->size;
}

uint64_t isom_update_stps_size( isom_stps_t *stps )
{
    if( !stps
     || !stps->list )
        return 0;
    stps->size = ISOM_LIST_FULLBOX_COMMON_SIZE + (uint64_t)stps->list->entry_count * 4;
    CHECK_LARGESIZE( stps );
    return stps->size;
}

uint64_t isom_update_sdtp_size( isom_sdtp_t *sdtp )
{
    if( !sdtp
     || !sdtp->list )
        return 0;
    sdtp->size = ISOM_FULLBOX_COMMON_SIZE + (uint64_t)sdtp->list->entry_count;
    CHECK_LARGESIZE( sdtp );
    return sdtp->size;
}

uint64_t isom_update_stsc_size( isom_stsc_t *stsc )
{
    if( !stsc
     || !stsc->list )
        return 0;
    stsc->size = ISOM_LIST_FULLBOX_COMMON_SIZE + (uint64_t)stsc->list->entry_count * 12;
    CHECK_LARGESIZE( stsc );
    return stsc->size;
}

uint64_t isom_update_stco_size( isom_stco_t *stco )
{
    if( !stco
     || !stco->list )
        return 0;
    stco->size = ISOM_LIST_FULLBOX_COMMON_SIZE + (uint64_t)stco->list->entry_count * (stco->large_presentation ? 8 : 4);
    CHECK_LARGESIZE( stco );
    return stco->size;
}

uint64_t isom_update_sbgp_size( isom_sbgp_t *sbgp )
{
    if( !sbgp
     || !sbgp->list )
        return 0;
    sbgp->size = ISOM_LIST_FULLBOX_COMMON_SIZE + 4 + (uint64_t)sbgp->list->entry_count * 8;
    CHECK_LARGESIZE( sbgp );
    return sbgp->size;
}

uint64_t isom_update_sgpd_size( isom_sgpd_t *sgpd )
{
    if( !sgpd
     || !sgpd->list )
        return 0;
    uint64_t size = ISOM_LIST_FULLBOX_COMMON_SIZE + (1 + (sgpd->version == 1)) * 4;
    size += (uint64_t)sgpd->list->entry_count * ((sgpd->version == 1) && !sgpd->default_length) * 4;
    switch( sgpd->grouping_type )
    {
        case ISOM_GROUP_TYPE_RAP :
            size += sgpd->list->entry_count;
            break;
        case ISOM_GROUP_TYPE_ROLL :
            size += (uint64_t)sgpd->list->entry_count * 2;
            break;
        default :
            /* We don't consider other grouping types currently. */
            break;
    }
    sgpd->size = size;
    CHECK_LARGESIZE( sgpd );
    return sgpd->size;
}

uint64_t isom_update_chpl_size( isom_chpl_t *chpl )
{
    if( !chpl )
        return 0;
    chpl->size = ISOM_FULLBOX_COMMON_SIZE + 4 * (chpl->version == 1) + 1;
    for( lsmash_entry_t *entry = chpl->list->head; entry; entry = entry->next )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        chpl->size += 9 + data->chapter_name_length;
    }
    CHECK_LARGESIZE( chpl );
    return chpl->size;
}

uint64_t isom_update_mean_size( isom_mean_t *mean )
{
    if( !mean )
        return 0;
    mean->size = ISOM_FULLBOX_COMMON_SIZE + mean->meaning_string_length;
    CHECK_LARGESIZE( mean );
    return mean->size;
}

uint64_t isom_update_name_size( isom_name_t *name )
{
    if( !name )
        return 0;
    name->size = ISOM_FULLBOX_COMMON_SIZE + name->name_length;
    CHECK_LARGESIZE( name );
    return name->size;
}

uint64_t isom_update_data_size( isom_data_t *data )
{
    if( !data )
        return 0;
    data->size = ISOM_BASEBOX_COMMON_SIZE + 8 + data->value_length;
    CHECK_LARGESIZE( data );
    return data->size;
}

uint64_t isom_update_metaitem_size( isom_metaitem_t *metaitem )
{
    if( !metaitem )
        return 0;
    metaitem->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( metaitem );
    return metaitem->size;
}

uint64_t isom_update_keys_size( isom_keys_t *keys )
{
    if( !keys )
        return 0;
    keys->size = ISOM_FULLBOX_COMMON_SIZE + 4;
    for( lsmash_entry_t *entry = keys->list->head; entry; entry = entry->next )
    {
        isom_keys_entry_t *data = (isom_keys_entry_t *)entry->data;
        if( !data )
            continue;
        keys->size += data->key_size;
    }
    CHECK_LARGESIZE( keys );
    return keys->size;
}

uint64_t isom_update_ilst_size( isom_ilst_t *ilst )
{
    if( !ilst )
        return 0;
    ilst->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( ilst );
    return ilst->size;
}

uint64_t isom_update_meta_size( isom_meta_t *meta )
{
    if( !meta )
        return 0;
    meta->size = ISOM_FULLBOX_COMMON_SIZE;
    CHECK_LARGESIZE( meta );
    return meta->size;
}

uint64_t isom_update_cprt_size( isom_cprt_t *cprt )
{
    if( !cprt )
        return 0;
    cprt->size = ISOM_FULLBOX_COMMON_SIZE + 2 + cprt->notice_length;
    CHECK_LARGESIZE( cprt );
    return cprt->size;
}

uint64_t isom_update_udta_size( isom_udta_t *udta )
{
    if( !udta || !udta->parent )
        return 0;
    udta->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( udta );
    return udta->size;
}

uint64_t isom_update_WLOC_size( isom_WLOC_t *WLOC )
{
    if( !WLOC )
        return 0;
    WLOC->size = ISOM_BASEBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( WLOC );
    return WLOC->size;
}

uint64_t isom_update_LOOP_size( isom_LOOP_t *LOOP )
{
    if( !LOOP )
        return 0;
    LOOP->size = ISOM_BASEBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( LOOP );
    return LOOP->size;
}

uint64_t isom_update_SelO_size( isom_SelO_t *SelO )
{
    if( !SelO )
        return 0;
    SelO->size = ISOM_BASEBOX_COMMON_SIZE + 1;
    CHECK_LARGESIZE( SelO );
    return SelO->size;
}

uint64_t isom_update_AllF_size( isom_AllF_t *AllF )
{
    if( !AllF )
        return 0;
    AllF->size = ISOM_BASEBOX_COMMON_SIZE + 1;
    CHECK_LARGESIZE( AllF );
    return AllF->size;
}

uint64_t isom_update_mvex_size( isom_mvex_t *mvex )
{
    if( !mvex )
        return 0;
    mvex->size = ISOM_BASEBOX_COMMON_SIZE;
    if( mvex->root->bs->stream != stdout && !mvex->mehd )
        mvex->size += 20;    /* 20 bytes is of placeholder. */
    CHECK_LARGESIZE( mvex );
    return mvex->size;
}

uint64_t isom_update_mehd_size( isom_mehd_t *mehd )
{
    if( !mehd )
        return 0;
    if( mehd->fragment_duration > UINT32_MAX )
        mehd->version = 1;
    mehd->size = ISOM_FULLBOX_COMMON_SIZE + 4 * (1 + (mehd->version == 1));
    CHECK_LARGESIZE( mehd );
    return mehd->size;
}

uint64_t isom_update_trex_size( isom_trex_t *trex )
{
    if( !trex )
        return 0;
    trex->size = ISOM_FULLBOX_COMMON_SIZE + 20;
    CHECK_LARGESIZE( trex );
    return trex->size;
}

uint64_t isom_update_moof_size( isom_moof_t *moof )
{
    if( !moof )
        return 0;
    moof->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( moof );
    return moof->size;
}

uint64_t isom_update_mfhd_size( isom_mfhd_t *mfhd )
{
    if( !mfhd )
        return 0;
    mfhd->size = ISOM_FULLBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( mfhd );
    return mfhd->size;
}

uint64_t isom_update_traf_size( isom_traf_t *traf )
{
    if( !traf )
        return 0;
    traf->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( traf );
    return traf->size;
}

uint64_t isom_update_tfhd_size( isom_tfhd_t *tfhd )
{
    if( !tfhd )
        return 0;
    tfhd->size = ISOM_FULLBOX_COMMON_SIZE
               + 4
               + 8 * !!( tfhd->flags & ISOM_TF_FLAGS_BASE_DATA_OFFSET_PRESENT         )
               + 4 * !!( tfhd->flags & ISOM_TF_FLAGS_SAMPLE_DESCRIPTION_INDEX_PRESENT )
               + 4 * !!( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT  )
               + 4 * !!( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_SIZE_PRESENT      )
               + 4 * !!( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT     );
    CHECK_LARGESIZE( tfhd );
    return tfhd->size;
}

uint64_t isom_update_tfdt_size( isom_tfdt_t *tfdt )
{
    if( !tfdt )
        return 0;
    tfdt->size = ISOM_FULLBOX_COMMON_SIZE + 4 * (1 + (tfdt->version == 1));
    CHECK_LARGESIZE( tfdt );
    return tfdt->size;
}

uint64_t isom_update_trun_size( isom_trun_t *trun )
{
    if( !trun )
        return 0;
    trun->size = ISOM_FULLBOX_COMMON_SIZE
               + 4
               + 4 * !!( trun->flags & ISOM_TR_FLAGS_DATA_OFFSET_PRESENT        )
               + 4 * !!( trun->flags & ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT );
    uint64_t row_size = 4 * !!( trun->flags & ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT                )
                      + 4 * !!( trun->flags & ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT                    )
                      + 4 * !!( trun->flags & ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT                   )
                      + 4 * !!( trun->flags & ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT );
    trun->size += row_size * trun->sample_count;
    CHECK_LARGESIZE( trun );
    return trun->size;
}

uint64_t isom_update_mfra_size( isom_mfra_t *mfra )
{
    if( !mfra )
        return 0;
    mfra->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( mfra );
    if( mfra->mfro )
        mfra->mfro->length = mfra->size;
    return mfra->size;
}

uint64_t isom_update_tfra_size( isom_tfra_t *tfra )
{
    if( !tfra )
        return 0;
    tfra->size = ISOM_FULLBOX_COMMON_SIZE + 12;
    uint32_t entry_size = 8 * (1 + (tfra->version == 1))
                        + tfra->length_size_of_traf_num   + 1
                        + tfra->length_size_of_trun_num   + 1
                        + tfra->length_size_of_sample_num + 1;
    tfra->size += entry_size * tfra->number_of_entry;
    CHECK_LARGESIZE( tfra );
    return tfra->size;
}

uint64_t isom_update_mfro_size( isom_mfro_t *mfro )
{
    if( !mfro )
        return 0;
    mfro->size = ISOM_FULLBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( mfro );
    return mfro->size;
}

uint64_t isom_update_mdat_size( isom_mdat_t *mdat )
{
    if( !mdat )
        return 0;
    /* Do nothing */
    //mdat->size = ISOM_BASEBOX_COMMON_SIZE;
    //CHECK_LARGESIZE( mdat );
    return mdat->size;
}

uint64_t isom_update_skip_size( isom_skip_t *skip )
{
    if( !skip )
        return 0;
    skip->size = ISOM_BASEBOX_COMMON_SIZE + (skip->data ? skip->length : 0ULL);
    CHECK_LARGESIZE( skip );
    return skip->size;
}

static uint64_t isom_update_extension_boxes( void *box )
{
    assert( box );
    uint64_t size = 0;
    lsmash_entry_list_t *extensions = &((isom_box_t *)box)->extensions;
    for( lsmash_entry_t *entry = extensions->head; entry; entry = entry->next )
    {
        isom_box_t *ext = (isom_box_t *)entry->data;
        if( !ext )
            continue;
        if( ext->update )
            size += ext->update( ext );
        else if( ext->manager & LSMASH_BINARY_CODED_BOX )
            size += ext->size;
        else if( ext->manager & LSMASH_UNKNOWN_BOX )
            size += isom_update_unknown_box_size( (isom_unknown_box_t *)ext );
    }
    return size;
}

/* box adding functions */
#define isom_add_box_template( box_name, parent_name, box_type, create_func ) \
    if( !parent_name )                                                        \
        return -1;                                                            \
    create_func( box_name, parent_name, box_type );                           \
    if( !parent_name->box_name )                                              \
        parent_name->box_name = box_name

#define isom_add_box( box_name, parent, box_type ) \
        isom_add_box_template( box_name, parent, box_type, isom_create_box )
#define isom_add_list_box( box_name, parent, box_type ) \
        isom_add_box_template( box_name, parent, box_type, isom_create_list_box )

isom_tref_type_t *isom_add_track_reference_type( isom_tref_t *tref, isom_track_reference_type type )
{
    if( !tref
     || !tref->ref_list )
        return NULL;
    isom_tref_type_t *ref = lsmash_malloc_zero( sizeof(isom_tref_type_t) );
    if( !ref )
        return NULL;
    /* Initialize common fields. */
    ref->root     = tref->root;
    ref->parent   = (isom_box_t *)tref;
    ref->size     = 0;
    ref->type     = lsmash_form_iso_box_type( type );
    ref->destruct = (isom_extension_destructor_t)isom_remove_track_reference_type;
    ref->update   = (isom_extension_updater_t)isom_update_track_reference_type_size;
    if( lsmash_add_entry( &tref->extensions, ref ) )
    {
        lsmash_free( ref );
        return NULL;
    }
    if( lsmash_add_entry( tref->ref_list, ref ) )
    {
        lsmash_remove_entry_tail( &tref->extensions, isom_remove_track_reference_type );
        return NULL;
    }
    return ref;
}

int isom_add_frma( isom_wave_t *wave )
{
    isom_add_box( frma, wave, QT_BOX_TYPE_FRMA );
    return 0;
}

int isom_add_enda( isom_wave_t *wave )
{
    isom_add_box( enda, wave, QT_BOX_TYPE_ENDA );
    return 0;
}

int isom_add_mp4a( isom_wave_t *wave )
{
    isom_add_box( mp4a, wave, QT_BOX_TYPE_MP4A );
    return 0;
}

int isom_add_terminator( isom_wave_t *wave )
{
    isom_add_box( terminator, wave, QT_BOX_TYPE_TERMINATOR );
    return 0;
}

int isom_add_ftab( isom_tx3g_entry_t *tx3g )
{
    isom_add_list_box( ftab, tx3g, ISOM_BOX_TYPE_FTAB );
    return 0;
}

int isom_add_stco( isom_stbl_t *stbl )
{
    isom_add_list_box( stco, stbl, ISOM_BOX_TYPE_STCO );
    stco->large_presentation = 0;
    return 0;
}

int isom_add_co64( isom_stbl_t *stbl )
{
    isom_add_list_box( stco, stbl, ISOM_BOX_TYPE_CO64 );
    stco->large_presentation = 1;
    return 0;
}

int isom_add_ftyp( lsmash_root_t *root )
{
    isom_add_box( ftyp, root, ISOM_BOX_TYPE_FTYP );
    return 0;
}

int isom_add_moov( lsmash_root_t *root )
{
    isom_add_box( moov, root, ISOM_BOX_TYPE_MOOV );
    return 0;
}

int isom_add_mvhd( isom_moov_t *moov )
{
    isom_add_box( mvhd, moov, ISOM_BOX_TYPE_MVHD );
    return 0;
}

int isom_add_iods( isom_moov_t *moov )
{
    isom_add_box( iods, moov, ISOM_BOX_TYPE_IODS );
    return 0;
}

int isom_add_ctab( void *parent_box )
{
    /* According to QuickTime File Format Specification, this box is placed inside Movie Box if present.
     * However, sometimes this box occurs inside an image description entry or the end of Sample Description Box. */
    if( !parent_box )
        return -1;
    isom_box_t *parent = (isom_box_t *)parent_box;
    isom_create_box( ctab, parent, QT_BOX_TYPE_CTAB );
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MOOV ) )
    {
        isom_moov_t *moov = (isom_moov_t *)parent;
        if( !moov->ctab )
            moov->ctab = ctab;
    }
    return 0;
}

isom_trak_t *isom_add_trak( isom_moov_t *moov )
{
    if( !moov || !moov->root )
        return NULL;
    if( !moov->trak_list )
    {
        moov->trak_list = lsmash_create_entry_list();
        if( !moov->trak_list )
            return NULL;
    }
    isom_create_box_pointer( trak, moov, ISOM_BOX_TYPE_TRAK );
    isom_cache_t *cache = lsmash_malloc_zero( sizeof(isom_cache_t) );
    if( !cache )
        goto fail;
    isom_fragment_t *fragment = NULL;
    if( moov->root->fragment )
    {
        fragment = lsmash_malloc_zero( sizeof(isom_fragment_t) );
        if( !fragment )
            goto fail;
        cache->fragment = fragment;
    }
    if( lsmash_add_entry( moov->trak_list, trak ) )
        goto fail;
    trak->cache = cache;
    return trak;
fail:
    lsmash_free( fragment );
    lsmash_free( cache );
    lsmash_remove_entry_tail( &moov->extensions, isom_remove_trak );
    return NULL;
}

int isom_add_tkhd( isom_trak_t *trak )
{
    isom_add_box( tkhd, trak, ISOM_BOX_TYPE_TKHD );
    return 0;
}

int isom_add_tapt( isom_trak_t *trak )
{
    isom_add_box( tapt, trak, QT_BOX_TYPE_TAPT );
    return 0;
}

int isom_add_clef( isom_tapt_t *tapt )
{
    isom_add_box( clef, tapt, QT_BOX_TYPE_CLEF );
    return 0;
}

int isom_add_prof( isom_tapt_t *tapt )
{
    isom_add_box( prof, tapt, QT_BOX_TYPE_PROF );
    return 0;
}

int isom_add_enof( isom_tapt_t *tapt )
{
    isom_add_box( enof, tapt, QT_BOX_TYPE_ENOF );
    return 0;
}

int isom_add_elst( isom_edts_t *edts )
{
    isom_add_list_box( elst, edts, ISOM_BOX_TYPE_ELST );
    return 0;
}

int isom_add_edts( isom_trak_t *trak )
{
    isom_add_box( edts, trak, ISOM_BOX_TYPE_EDTS );
    return 0;
}

int isom_add_tref( isom_trak_t *trak )
{
    isom_add_box( tref, trak, ISOM_BOX_TYPE_TREF );
    tref->ref_list = lsmash_create_entry_list();
    if( !tref->ref_list )
    {
        lsmash_remove_entry_tail( &trak->extensions, isom_remove_tref );
        return -1;
    }
    return 0;
}

int isom_add_mdia( isom_trak_t *trak )
{
    isom_add_box( mdia, trak, ISOM_BOX_TYPE_MDIA );
    return 0;
}


int isom_add_mdhd( isom_mdia_t *mdia )
{
    isom_add_box( mdhd, mdia, ISOM_BOX_TYPE_MDHD );
    return 0;
}

int isom_add_hdlr( void *parent_box )
{
    if( !parent_box )
        return -1;
    isom_box_t *parent = (isom_box_t *)parent_box;
    isom_create_box( hdlr, parent, ISOM_BOX_TYPE_HDLR );
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
    return 0;
}

int isom_add_minf( isom_mdia_t *mdia )
{
    isom_add_box( minf, mdia, ISOM_BOX_TYPE_MINF );
    return 0;
}

int isom_add_vmhd( isom_minf_t *minf )
{
    isom_add_box( vmhd, minf, ISOM_BOX_TYPE_VMHD );
    return 0;
}

int isom_add_smhd( isom_minf_t *minf )
{
    isom_add_box( smhd, minf, ISOM_BOX_TYPE_SMHD );
    return 0;
}

int isom_add_hmhd( isom_minf_t *minf )
{
    isom_add_box( hmhd, minf, ISOM_BOX_TYPE_HMHD );
    return 0;
}

int isom_add_nmhd( isom_minf_t *minf )
{
    isom_add_box( nmhd, minf, ISOM_BOX_TYPE_NMHD );
    return 0;
}

int isom_add_gmhd( isom_minf_t *minf )
{
    isom_add_box( gmhd, minf, QT_BOX_TYPE_GMHD );
    return 0;
}

int isom_add_gmin( isom_gmhd_t *gmhd )
{
    isom_add_box( gmin, gmhd, QT_BOX_TYPE_GMIN );
    return 0;
}

int isom_add_text( isom_gmhd_t *gmhd )
{
    isom_add_box( text, gmhd, QT_BOX_TYPE_TEXT );
    return 0;
}

int isom_add_dinf( void *parent_box )
{
    if( !parent_box )
        return -1;
    isom_box_t *parent = (isom_box_t *)parent_box;
    isom_create_box( dinf, parent, ISOM_BOX_TYPE_DINF );
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
    return 0;
}

isom_dref_entry_t *isom_add_dref_entry( isom_dref_t *dref )
{
    if( !dref
     || !dref->list )
        return NULL;
    isom_dref_entry_t *data = lsmash_malloc_zero( sizeof(isom_dref_entry_t) );
    if( !data )
        return NULL;
    isom_init_box_common( data, dref, ISOM_BOX_TYPE_URL, isom_remove_dref_entry, isom_update_dref_entry_size );
    if( lsmash_add_entry( &dref->extensions, data ) )
    {
        lsmash_free( data );
        return NULL;
    }
    if( lsmash_add_entry( dref->list, data ) )
    {
        lsmash_remove_entry_tail( &dref->extensions, isom_remove_dref_entry );
        return NULL;
    }
    return data;
}

int isom_add_dref( isom_dinf_t *dinf )
{
    isom_add_list_box( dref, dinf, ISOM_BOX_TYPE_DREF );
    return 0;
}

int isom_add_stbl( isom_minf_t *minf )
{
    isom_add_box( stbl, minf, ISOM_BOX_TYPE_STBL );
    return 0;
}

int isom_add_stsd( isom_stbl_t *stbl )
{
    isom_add_list_box( stsd, stbl, ISOM_BOX_TYPE_STSD );
    return 0;
}

isom_esds_t *isom_add_esds( void *parent_box )
{
    isom_box_t *parent = (isom_box_t *)parent_box;
    lsmash_box_type_t box_type = !lsmash_check_box_type_identical( parent->type, QT_BOX_TYPE_WAVE )
                               ? ISOM_BOX_TYPE_ESDS
                               :   QT_BOX_TYPE_ESDS;
    isom_create_box_pointer( esds, parent, box_type );
    return esds;
}

isom_glbl_t *isom_add_glbl( void *parent_box )
{
    isom_create_box_pointer( glbl, (isom_box_t *)parent_box, QT_BOX_TYPE_GLBL );
    return glbl;
}

isom_clap_t *isom_add_clap( isom_visual_entry_t *visual )
{
    isom_create_box_pointer( clap, visual, ISOM_BOX_TYPE_CLAP );
    return clap;
}

isom_pasp_t *isom_add_pasp( isom_visual_entry_t *visual )
{
    isom_create_box_pointer( pasp, visual, ISOM_BOX_TYPE_PASP );
    return pasp;
}

isom_colr_t *isom_add_colr( isom_visual_entry_t *visual )
{
    isom_create_box_pointer( colr, visual, ISOM_BOX_TYPE_COLR );
    return colr;
}

isom_gama_t *isom_add_gama( isom_visual_entry_t *visual )
{
    isom_create_box_pointer( gama, visual, QT_BOX_TYPE_GAMA );
    return gama;
}

isom_fiel_t *isom_add_fiel( isom_visual_entry_t *visual )
{
    isom_create_box_pointer( fiel, visual, QT_BOX_TYPE_FIEL );
    return fiel;
}

isom_cspc_t *isom_add_cspc( isom_visual_entry_t *visual )
{
    isom_create_box_pointer( cspc, visual, QT_BOX_TYPE_CSPC );
    return cspc;
}

isom_sgbt_t *isom_add_sgbt( isom_visual_entry_t *visual )
{
    isom_create_box_pointer( sgbt, visual, QT_BOX_TYPE_SGBT );
    return sgbt;
}

isom_stsl_t *isom_add_stsl( isom_visual_entry_t *visual )
{
    isom_create_box_pointer( stsl, visual, ISOM_BOX_TYPE_STSL );
    return stsl;
}

isom_btrt_t *isom_add_btrt( isom_visual_entry_t *visual )
{
    isom_create_box_pointer( btrt, visual, ISOM_BOX_TYPE_BTRT );
    return btrt;
}

isom_wave_t *isom_add_wave( isom_audio_entry_t *audio )
{
    isom_create_box_pointer( wave, audio, QT_BOX_TYPE_WAVE );
    return wave;
}

isom_chan_t *isom_add_chan( isom_audio_entry_t *audio )
{
    isom_create_box_pointer( chan, audio, QT_BOX_TYPE_CHAN );
    return chan;
}

isom_srat_t *isom_add_srat( isom_audio_entry_t *audio )
{
    isom_create_box_pointer( srat, audio, ISOM_BOX_TYPE_SRAT );
    return srat;
}

int isom_add_stts( isom_stbl_t *stbl )
{
    isom_add_list_box( stts, stbl, ISOM_BOX_TYPE_STTS );
    return 0;
}

int isom_add_ctts( isom_stbl_t *stbl )
{
    isom_add_list_box( ctts, stbl, ISOM_BOX_TYPE_CTTS );
    return 0;
}

int isom_add_cslg( isom_stbl_t *stbl )
{
    isom_add_box( cslg, stbl, ISOM_BOX_TYPE_CSLG );
    return 0;
}

int isom_add_stsc( isom_stbl_t *stbl )
{
    isom_add_list_box( stsc, stbl, ISOM_BOX_TYPE_STSC );
    return 0;
}

int isom_add_stsz( isom_stbl_t *stbl )
{
    /* We don't create a list here. */
    isom_add_box( stsz, stbl, ISOM_BOX_TYPE_STSZ );
    return 0;
}

int isom_add_stss( isom_stbl_t *stbl )
{
    isom_add_list_box( stss, stbl, ISOM_BOX_TYPE_STSS );
    return 0;
}

int isom_add_stps( isom_stbl_t *stbl )
{
    isom_add_list_box( stps, stbl, QT_BOX_TYPE_STPS );
    return 0;
}

int isom_add_sdtp( isom_box_t *parent )
{
    if( !parent )
        return -1;
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STBL ) )
    {
        isom_stbl_t *stbl = (isom_stbl_t *)parent;
        isom_add_list_box( sdtp, stbl, ISOM_BOX_TYPE_SDTP );
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAF ) )
    {
        isom_traf_t *traf = (isom_traf_t *)parent;
        isom_add_list_box( sdtp, traf, ISOM_BOX_TYPE_SDTP );
    }
    else
        assert( 0 );
    return 0;
}

isom_sgpd_t *isom_add_sgpd( isom_stbl_t *stbl )
{
    if( !stbl )
        return NULL;
    if( !stbl->sgpd_list )
    {
        stbl->sgpd_list = lsmash_create_entry_list();
        if( !stbl->sgpd_list )
            return NULL;
    }
    isom_create_list_box_null( sgpd, stbl, ISOM_BOX_TYPE_SGPD );
    if( lsmash_add_entry( stbl->sgpd_list, sgpd ) )
    {
        lsmash_remove_entry_tail( &stbl->extensions, isom_remove_sgpd );
        return NULL;
    }
    return sgpd;
}

isom_sbgp_t *isom_add_sbgp( void *parent_box )
{
    if( !parent_box )
        return NULL;
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STBL ) )
    {
        isom_stbl_t *stbl = (isom_stbl_t *)parent;
        if( !stbl->sbgp_list )
        {
            stbl->sbgp_list = lsmash_create_entry_list();
            if( !stbl->sbgp_list )
                return NULL;
        }
        isom_create_list_box_null( sbgp, stbl, ISOM_BOX_TYPE_SBGP );
        if( lsmash_add_entry( stbl->sbgp_list, sbgp ) )
        {
            lsmash_remove_entry_tail( &stbl->extensions, isom_remove_sbgp );
            return NULL;
        }
        return sbgp;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAF ) )
    {
        isom_traf_t *traf = (isom_traf_t *)parent;
        isom_create_list_box_null( sbgp, traf, ISOM_BOX_TYPE_SBGP );
        return sbgp;
    }
    assert( 0 );
    return NULL;
}

int isom_add_chpl( isom_udta_t *udta )
{
    isom_add_list_box( chpl, udta, ISOM_BOX_TYPE_CHPL );
    return 0;
}

int isom_add_metaitem( isom_ilst_t *ilst, lsmash_itunes_metadata_item item )
{
    if( !ilst
     || !ilst->item_list )
        return -1;
    lsmash_box_type_t type = lsmash_form_iso_box_type( item );
    isom_create_box( metaitem, ilst, type );
    if( lsmash_add_entry( ilst->item_list, metaitem ) )
    {
        lsmash_remove_entry_tail( &ilst->extensions, isom_remove_metaitem );
        return -1;
    }
    return 0;
}

int isom_add_mean( isom_metaitem_t *metaitem )
{
    isom_add_box( mean, metaitem, ISOM_BOX_TYPE_MEAN );
    return 0;
}

int isom_add_name( isom_metaitem_t *metaitem )
{
    isom_add_box( name, metaitem, ISOM_BOX_TYPE_NAME );
    return 0;
}

int isom_add_data( isom_metaitem_t *metaitem )
{
    isom_add_box( data, metaitem, ISOM_BOX_TYPE_DATA );
    return 0;
}

int isom_add_ilst( isom_meta_t *meta )
{
    isom_add_box( ilst, meta, ISOM_BOX_TYPE_ILST );
    ilst->item_list = lsmash_create_entry_list();
    if( !ilst->item_list )
    {
        lsmash_remove_entry_tail( &meta->extensions, isom_remove_ilst );
        return -1;
    }
    return 0;
}

int isom_add_keys( isom_meta_t *meta )
{
    isom_add_list_box( keys, meta, QT_BOX_TYPE_KEYS );
    return 0;
}

int isom_add_meta( void *parent_box )
{
    if( !parent_box )
        return -1;
    isom_box_t *parent = (isom_box_t *)parent_box;
    isom_create_box( meta, parent, ISOM_BOX_TYPE_META );
    if( lsmash_check_box_type_identical( parent->type, LSMASH_BOX_TYPE_UNSPECIFIED ) )
    {
        lsmash_root_t *root = (lsmash_root_t *)parent;
        if( !root->meta )
            root->meta = meta;
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
    return 0;
}

int isom_add_cprt( isom_udta_t *udta )
{
    if( !udta )
        return -1;
    if( !udta->cprt_list )
    {
        udta->cprt_list = lsmash_create_entry_list();
        if( !udta->cprt_list )
            return -1;
    }
    isom_create_box( cprt, udta, ISOM_BOX_TYPE_CPRT );
    if( lsmash_add_entry( udta->cprt_list, cprt ) )
    {
        lsmash_remove_entry_tail( &udta->extensions, isom_remove_cprt );
        return -1;
    }
    return 0;
}

int isom_add_udta( void *parent_box )
{
    if( !parent_box )
        return -1;
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MOOV ) )
    {
        isom_moov_t *moov = (isom_moov_t *)parent;
        isom_add_box( udta, moov, ISOM_BOX_TYPE_UDTA );
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAK ) )
    {
        isom_trak_t *trak = (isom_trak_t *)parent;
        isom_add_box( udta, trak, ISOM_BOX_TYPE_UDTA );
    }
    else
        assert( 0 );
    return 0;
}

int isom_add_WLOC( isom_udta_t *udta )
{
    isom_add_box( WLOC, udta, QT_BOX_TYPE_WLOC );
    return 0;
}

int isom_add_LOOP( isom_udta_t *udta )
{
    isom_add_box( LOOP, udta, QT_BOX_TYPE_LOOP );
    return 0;
}

int isom_add_SelO( isom_udta_t *udta )
{
    isom_add_box( SelO, udta, QT_BOX_TYPE_SELO );
    return 0;
}

int isom_add_AllF( isom_udta_t *udta )
{
    isom_add_box( AllF, udta, QT_BOX_TYPE_ALLF );
    return 0;
}

int isom_add_mvex( isom_moov_t *moov )
{
    isom_add_box( mvex, moov, ISOM_BOX_TYPE_MVEX );
    return 0;
}

int isom_add_mehd( isom_mvex_t *mvex )
{
    isom_add_box( mehd, mvex, ISOM_BOX_TYPE_MEHD );
    return 0;
}

isom_trex_t *isom_add_trex( isom_mvex_t *mvex )
{
    if( !mvex )
        return NULL;
    if( !mvex->trex_list )
    {
        mvex->trex_list = lsmash_create_entry_list();
        if( !mvex->trex_list )
            return NULL;
    }
    isom_create_box_pointer( trex, mvex, ISOM_BOX_TYPE_TREX );
    if( lsmash_add_entry( mvex->trex_list, trex ) )
    {
        lsmash_remove_entry_tail( &mvex->extensions, isom_remove_trex );
        return NULL;
    }
    return trex;
}

isom_moof_t *isom_add_moof( lsmash_root_t *root )
{
    if( !root )
        return NULL;
    if( !root->moof_list )
    {
        root->moof_list = lsmash_create_entry_list();
        if( !root->moof_list )
            return NULL;
    }
    isom_create_box_pointer( moof, root, ISOM_BOX_TYPE_MOOF );
    if( lsmash_add_entry( root->moof_list, moof ) )
    {
        lsmash_remove_entry_tail( &root->extensions, isom_remove_moof );
        return NULL;
    }
    return moof;
}

int isom_add_mfhd( isom_moof_t *moof )
{
    isom_add_box( mfhd, moof, ISOM_BOX_TYPE_MFHD );
    return 0;
}

isom_traf_t *isom_add_traf( isom_moof_t *moof )
{
    if( !moof )
        return NULL;
    if( !moof->traf_list )
    {
        moof->traf_list = lsmash_create_entry_list();
        if( !moof->traf_list )
            return NULL;
    }
    isom_create_box_pointer( traf, moof, ISOM_BOX_TYPE_TRAF );
    if( lsmash_add_entry( moof->traf_list, traf ) )
    {
        lsmash_remove_entry_tail( &moof->extensions, isom_remove_traf );
        return NULL;
    }
    isom_cache_t *cache = lsmash_malloc( sizeof(isom_cache_t) );
    if( !cache )
    {
        lsmash_remove_entry_tail( &moof->extensions, isom_remove_traf );
        return NULL;
    }
    memset( cache, 0, sizeof(isom_cache_t) );
    traf->cache = cache;
    return traf;
}

int isom_add_tfhd( isom_traf_t *traf )
{
    isom_add_box( tfhd, traf, ISOM_BOX_TYPE_TFHD );
    return 0;
}

int isom_add_tfdt( isom_traf_t *traf )
{
    isom_add_box( tfdt, traf, ISOM_BOX_TYPE_TFDT );
    return 0;
}

isom_trun_t *isom_add_trun( isom_traf_t *traf )
{
    if( !traf )
        return NULL;
    if( !traf->trun_list )
    {
        traf->trun_list = lsmash_create_entry_list();
        if( !traf->trun_list )
            return NULL;
    }
    isom_create_box_pointer( trun, traf, ISOM_BOX_TYPE_TRUN );
    if( lsmash_add_entry( traf->trun_list, trun ) )
    {
        lsmash_remove_entry_tail( &traf->extensions, isom_remove_trun );
        return NULL;
    }
    return trun;
}

int isom_add_mfra( lsmash_root_t *root )
{
    isom_add_box( mfra, root, ISOM_BOX_TYPE_MFRA );
    return 0;
}

isom_tfra_t *isom_add_tfra( isom_mfra_t *mfra )
{
    if( !mfra )
        return NULL;
    if( !mfra->tfra_list )
    {
        mfra->tfra_list = lsmash_create_entry_list();
        if( !mfra->tfra_list )
            return NULL;
    }
    isom_create_box_pointer( tfra, mfra, ISOM_BOX_TYPE_TFRA );
    if( lsmash_add_entry( mfra->tfra_list, tfra ) )
    {
        lsmash_remove_entry_tail( &mfra->extensions, isom_remove_tfra );
        return NULL;
    }
    return tfra;
}

int isom_add_mfro( isom_mfra_t *mfra )
{
    isom_add_box( mfro, mfra, ISOM_BOX_TYPE_MFRO );
    return 0;
}

int isom_add_mdat( lsmash_root_t *root )
{
    assert( !root->mdat );
    isom_create_box( mdat, root, ISOM_BOX_TYPE_MDAT );
    root->mdat = mdat;
    return 0;
}


int isom_add_free( void *parent_box )
{
    if( !parent_box )
        return -1;
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MOOV ) )
    {
        lsmash_root_t *root = (lsmash_root_t *)parent;
        isom_create_box( skip, root, ISOM_BOX_TYPE_FREE );
        if( !root->free )
            root->free = skip;
        return 0;
    }
    isom_create_box( skip, parent, ISOM_BOX_TYPE_FREE );
    return 0;
}
