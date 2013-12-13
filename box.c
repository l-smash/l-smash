/*****************************************************************************
 * box.c:
 *****************************************************************************
 * Copyright (C) 2012-2013 L-SMASH project
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

int lsmash_check_box_type_specified( lsmash_box_type_t *box_type )
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

void isom_init_box_common( void *_box, void *_parent, lsmash_box_type_t box_type, void *destructor )
{
    isom_box_t *box    = (isom_box_t *)_box;
    isom_box_t *parent = (isom_box_t *)_parent;
    assert( box && parent && parent->root );
    box->class    = &lsmash_box_class;
    box->root     = parent->root;
    box->parent   = parent;
    box->destruct = destructor ? destructor : lsmash_free;
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
    return 0;
}

void isom_remove_extension_box( isom_box_t *ext )
{
    if( !ext )
        return;
    if( ext->destruct )
        ext->destruct( ext );
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

#define isom_remove_box( box_name, parent_type )                  \
    do                                                            \
    {                                                             \
        parent_type *parent = (parent_type *)box_name->parent;    \
        isom_remove_all_extension_boxes( &box_name->extensions ); \
        lsmash_free( box_name );                                  \
        if( parent )                                              \
            parent->box_name = NULL;                              \
    } while( 0 )

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
    isom_remove_tkhd( trak->tkhd );
    isom_remove_tapt( trak->tapt );
    isom_remove_edts( trak->edts );
    isom_remove_tref( trak->tref );
    isom_remove_mdia( trak->mdia );
    isom_remove_udta( trak->udta );
    isom_remove_meta( trak->meta );
    if( trak->cache )
    {
        isom_remove_sample_pool( trak->cache->chunk.pool );
        lsmash_remove_list( trak->cache->roll.pool, NULL );
        if( trak->cache->rap )
            lsmash_free( trak->cache->rap );
        lsmash_free( trak->cache );
    }
    isom_remove_all_extension_boxes( &trak->extensions );
    lsmash_free( trak );    /* Note: the list that contains this trak still has the address of the entry. */
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
    isom_remove_clef( tapt->clef );
    isom_remove_prof( tapt->prof );
    isom_remove_enof( tapt->enof );
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
    isom_remove_elst( edts->elst );
    isom_remove_box( edts, isom_trak_t );
}

void isom_remove_track_reference_type( isom_tref_type_t *ref )
{
    if( !ref )
        return;
    if( ref->track_ID )
        lsmash_free( ref->track_ID );
    isom_remove_all_extension_boxes( &ref->extensions );
    lsmash_free( ref );
}

void isom_remove_tref( isom_tref_t *tref )
{
    if( !tref )
        return;
    lsmash_remove_list( tref->ref_list, isom_remove_track_reference_type );
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
    isom_remove_gmin( gmhd->gmin );
    isom_remove_text( gmhd->text );
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
    isom_remove_frma( wave->frma );
    isom_remove_enda( wave->enda );
    isom_remove_mp4a( wave->mp4a );
    isom_remove_terminator( wave->terminator );
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

void isom_remove_stsd( isom_stsd_t *stsd )
{
    if( !stsd )
        return;
    lsmash_remove_list( stsd->list, isom_remove_sample_description );
    isom_remove_box( stsd, isom_stbl_t );
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
    isom_remove_all_extension_boxes( &sgpd->extensions );
    lsmash_free( sgpd );
}

void isom_remove_sbgp( isom_sbgp_t *sbgp )
{
    if( !sbgp )
        return;
    lsmash_remove_list( sbgp->list, NULL );
    isom_remove_all_extension_boxes( &sbgp->extensions );
    lsmash_free( sbgp );
}

void isom_remove_stbl( isom_stbl_t *stbl )
{
    if( !stbl )
        return;
    isom_remove_stsd( stbl->stsd );
    isom_remove_stts( stbl->stts );
    isom_remove_ctts( stbl->ctts );
    isom_remove_cslg( stbl->cslg );
    isom_remove_stsc( stbl->stsc );
    isom_remove_stsz( stbl->stsz );
    isom_remove_stss( stbl->stss );
    isom_remove_stps( stbl->stps );
    isom_remove_sdtp( stbl->sdtp );
    isom_remove_stco( stbl->stco );
    lsmash_remove_list( stbl->sgpd_list, isom_remove_sgpd );
    lsmash_remove_list( stbl->sbgp_list, isom_remove_sbgp );
    isom_remove_box( stbl, isom_minf_t );
}

void isom_remove_dref_entry( isom_dref_entry_t *data_entry )
{
    if( !data_entry )
        return;
    lsmash_free( data_entry->name );
    lsmash_free( data_entry->location );
    isom_remove_all_extension_boxes( &data_entry->extensions );
    lsmash_free( data_entry );
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
    lsmash_remove_list( dref->list, isom_remove_dref_entry );
    isom_remove_box( dref, isom_dinf_t );
}

void isom_remove_dinf( isom_dinf_t *dinf )
{
    if( !dinf )
        return;
    isom_remove_dref( dinf->dref );
    isom_remove_box( dinf, isom_minf_t );
}

void isom_remove_minf( isom_minf_t *minf )
{
    if( !minf )
        return;
    isom_remove_vmhd( minf->vmhd );
    isom_remove_smhd( minf->smhd );
    isom_remove_hmhd( minf->hmhd );
    isom_remove_nmhd( minf->nmhd );
    isom_remove_gmhd( minf->gmhd );
    isom_remove_hdlr( minf->hdlr );
    isom_remove_dinf( minf->dinf );
    isom_remove_stbl( minf->stbl );
    isom_remove_box( minf, isom_mdia_t );
}

void isom_remove_mdia( isom_mdia_t *mdia )
{
    if( !mdia )
        return;
    isom_remove_mdhd( mdia->mdhd );
    isom_remove_minf( mdia->minf );
    isom_remove_hdlr( mdia->hdlr );
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
    isom_remove_mean( metaitem->mean );
    isom_remove_name( metaitem->name );
    isom_remove_data( metaitem->data );
    isom_remove_all_extension_boxes( &metaitem->extensions );
    lsmash_free( metaitem );
}

void isom_remove_ilst( isom_ilst_t *ilst )
{
    if( !ilst )
        return;
    lsmash_remove_list( ilst->item_list, isom_remove_metaitem );
    isom_remove_box( ilst, isom_meta_t );
}

void isom_remove_meta( isom_meta_t *meta )
{
    if( !meta )
        return;
    isom_remove_hdlr( meta->hdlr );
    isom_remove_dinf( meta->dinf );
    isom_remove_keys( meta->keys );
    isom_remove_ilst( meta->ilst );
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
    isom_remove_all_extension_boxes( &cprt->extensions );
    lsmash_free( cprt );
}

void isom_remove_udta( isom_udta_t *udta )
{
    if( !udta )
        return;
    isom_remove_chpl( udta->chpl );
    isom_remove_meta( udta->meta );
    isom_remove_WLOC( udta->WLOC );
    isom_remove_LOOP( udta->LOOP );
    isom_remove_SelO( udta->SelO );
    isom_remove_AllF( udta->AllF );
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
    isom_remove_all_extension_boxes( &trex->extensions );
    lsmash_free( trex );    /* Note: the list that contains this trex still has the address of the entry.
                             *       Should not use this function solely. */
}

void isom_remove_mvex( isom_mvex_t *mvex )
{
    if( !mvex )
        return;
    isom_remove_mehd( mvex->mehd );
    lsmash_remove_list( mvex->trex_list, isom_remove_trex );
    isom_remove_box( mvex, isom_moov_t );
}

void isom_remove_mvhd( isom_mvhd_t *mvhd )
{
    if( !mvhd )
        return;
    isom_remove_box( mvhd, isom_moov_t );
}

void isom_remove_moov( lsmash_root_t *root )
{
    if( !root
     || !root->moov )
        return;
    isom_moov_t *moov = root->moov;
    isom_remove_mvhd( moov->mvhd );
    isom_remove_iods( moov->iods );
    lsmash_remove_list( moov->trak_list, isom_remove_trak );
    isom_remove_udta( moov->udta );
    isom_remove_ctab( moov->ctab );
    isom_remove_meta( moov->meta );
    isom_remove_mvex( moov->mvex );
    isom_remove_all_extension_boxes( &moov->extensions );
    lsmash_free( moov );
    root->moov = NULL;
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
    isom_remove_all_extension_boxes( &trun->extensions );
    lsmash_free( trun );    /* Note: the list that contains this trun still has the address of the entry. */
}

void isom_remove_traf( isom_traf_t *traf )
{
    if( !traf )
        return;
    isom_remove_tfhd( traf->tfhd );
    isom_remove_tfdt( traf->tfdt );
    lsmash_remove_list( traf->trun_list, isom_remove_trun );
    isom_remove_sdtp( traf->sdtp );
    isom_remove_all_extension_boxes( &traf->extensions );
    lsmash_free( traf );    /* Note: the list that contains this traf still has the address of the entry. */
}

void isom_remove_moof( isom_moof_t *moof )
{
    if( !moof )
        return;
    isom_remove_mfhd( moof->mfhd );
    lsmash_remove_list( moof->traf_list, isom_remove_traf );
    isom_remove_all_extension_boxes( &moof->extensions );
    lsmash_free( moof );
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
    lsmash_root_t *root = (lsmash_root_t *)skip->parent;
    isom_remove_all_extension_boxes( &skip->extensions );
    lsmash_free( skip );
    root->free = NULL;
}

void isom_remove_tfra( isom_tfra_t *tfra )
{
    if( !tfra )
        return;
    lsmash_remove_list( tfra->list, NULL );
    isom_remove_all_extension_boxes( &tfra->extensions );
    lsmash_free( tfra );
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
    lsmash_remove_list( mfra->tfra_list, isom_remove_tfra );
    isom_remove_mfro( mfra->mfro );
    isom_remove_box( mfra, lsmash_root_t );
}

isom_tref_type_t *isom_add_track_reference_type( isom_tref_t *tref, isom_track_reference_type type, uint32_t ref_count, uint32_t *track_ID )
{
    if( !tref
     || !tref->ref_list )
        return NULL;
    isom_tref_type_t *ref = lsmash_malloc_zero( sizeof(isom_tref_type_t) );
    if( !ref )
        return NULL;
    /* Initialize common fields. */
    ref->root   = tref->root;
    ref->parent = (isom_box_t *)tref;
    ref->size   = 0;
    ref->type   = lsmash_form_iso_box_type( type );
    /* */
    ref->ref_count = ref_count;
    ref->track_ID  = track_ID;
    if( lsmash_add_entry( tref->ref_list, ref ) )
    {
        lsmash_free( ref );
        return NULL;
    }
    return ref;
}

static int isom_add_dref_entry( isom_dref_t *dref, uint32_t flags, char *name, char *location )
{
    if( !dref
     || !dref->list )
        return -1;
    isom_dref_entry_t *data = lsmash_malloc_zero( sizeof(isom_dref_entry_t) );
    if( !data )
        return -1;
    isom_init_box_common( data, dref, name ? ISOM_BOX_TYPE_URN : ISOM_BOX_TYPE_URL, isom_remove_dref_entry );
    data->flags = flags;
    if( location )
    {
        data->location_length = strlen( location ) + 1;
        data->location        = lsmash_memdup( location, data->location_length );
        if( !data->location )
        {
            lsmash_free( data );
            return -1;
        }
    }
    if( name )
    {
        data->name_length = strlen( name ) + 1;
        data->name        = lsmash_memdup( name, data->name_length );
        if( !data->name )
        {
            if( data->location )
                lsmash_free( data->location );
            lsmash_free( data );
            return -1;
        }
    }
    if( lsmash_add_entry( dref->list, data ) )
    {
        if( data->location )
            lsmash_free( data->location );
        if( data->name )
            lsmash_free( data->name );
        lsmash_free( data );
        return -1;
    }
    return 0;
}

int isom_add_frma( isom_wave_t *wave )
{
    if( !wave || wave->frma )
        return -1;
    isom_create_box( frma, wave, QT_BOX_TYPE_FRMA );
    wave->frma = frma;
    return 0;
}

int isom_add_enda( isom_wave_t *wave )
{
    if( !wave || wave->enda )
        return -1;
    isom_create_box( enda, wave, QT_BOX_TYPE_ENDA );
    wave->enda = enda;
    return 0;
}

int isom_add_mp4a( isom_wave_t *wave )
{
    if( !wave || wave->mp4a )
        return -1;
    isom_create_box( mp4a, wave, QT_BOX_TYPE_MP4A );
    wave->mp4a = mp4a;
    return 0;
}

int isom_add_terminator( isom_wave_t *wave )
{
    if( !wave || wave->terminator )
        return -1;
    isom_create_box( terminator, wave, QT_BOX_TYPE_TERMINATOR );
    wave->terminator = terminator;
    return 0;
}

int isom_add_ftab( isom_tx3g_entry_t *tx3g )
{
    if( !tx3g )
        return -1;
    isom_create_list_box( ftab, tx3g, ISOM_BOX_TYPE_FTAB );
    tx3g->ftab = ftab;
    return 0;
}

int isom_add_stco( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stco )
        return -1;
    isom_create_list_box( stco, stbl, ISOM_BOX_TYPE_STCO );
    stco->large_presentation = 0;
    stbl->stco = stco;
    return 0;
}

int isom_add_co64( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stco )
        return -1;
    isom_create_list_box( stco, stbl, ISOM_BOX_TYPE_CO64 );
    stco->large_presentation = 1;
    stbl->stco = stco;
    return 0;
}

int isom_add_ftyp( lsmash_root_t *root )
{
    if( root->ftyp )
        return -1;
    isom_create_box( ftyp, root, ISOM_BOX_TYPE_FTYP );
    ftyp->size = ISOM_BASEBOX_COMMON_SIZE + 8;
    root->ftyp = ftyp;
    return 0;
}

int isom_add_moov( lsmash_root_t *root )
{
    if( root->moov )
        return -1;
    isom_create_box( moov, root, ISOM_BOX_TYPE_MOOV );
    root->moov = moov;
    return 0;
}

int isom_add_mvhd( isom_moov_t *moov )
{
    if( !moov || moov->mvhd )
        return -1;
    isom_create_box( mvhd, moov, ISOM_BOX_TYPE_MVHD );
    mvhd->rate          = 0x00010000;
    mvhd->volume        = 0x0100;
    mvhd->matrix[0]     = 0x00010000;
    mvhd->matrix[4]     = 0x00010000;
    mvhd->matrix[8]     = 0x40000000;
    mvhd->next_track_ID = 1;
    moov->mvhd = mvhd;
    return 0;
}

static int isom_scan_trak_profileLevelIndication( isom_trak_t *trak, mp4a_audioProfileLevelIndication *audio_pli, mp4sys_visualProfileLevelIndication *visual_pli )
{
    if( !trak
     || !trak->mdia
     || !trak->mdia->minf
     || !trak->mdia->minf->stbl )
        return -1;
    isom_stsd_t *stsd = trak->mdia->minf->stbl->stsd;
    if( !stsd
     || !stsd->list
     || !stsd->list->head )
        return -1;
    for( lsmash_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_sample_entry_t *sample_entry = (isom_sample_entry_t *)entry->data;
        if( !sample_entry )
            return -1;
        lsmash_codec_type_t sample_type = (lsmash_codec_type_t)sample_entry->type;
        if( trak->mdia->minf->vmhd )
        {
            if( lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_AVC1_VIDEO )
             || lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_AVC2_VIDEO )
             || lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_AVC3_VIDEO )
             || lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_AVC4_VIDEO )
             || lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_AVCP_VIDEO )
             || lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_SVC1_VIDEO )
             || lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_MVC1_VIDEO )
             || lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_MVC2_VIDEO ) )
            {
                /* FIXME: Do we have to arbitrate like audio? */
                if( *visual_pli == MP4SYS_VISUAL_PLI_NONE_REQUIRED )
                    *visual_pli = lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_AVCP_VIDEO )
                                ? MP4SYS_OBJECT_TYPE_Parameter_Sets_H_264_ISO_14496_10
                                : MP4SYS_VISUAL_PLI_H264_AVC;
            }
            else
                *visual_pli = MP4SYS_VISUAL_PLI_NOT_SPECIFIED;
        }
        else if( trak->mdia->minf->smhd )
        {
            if( lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_MP4A_AUDIO ) )
            {
                isom_audio_entry_t *audio = (isom_audio_entry_t *)sample_entry;
#ifdef LSMASH_DEMUXER_ENABLED
                isom_esds_t *esds = (isom_esds_t *)isom_get_extension_box_format( &audio->extensions, ISOM_BOX_TYPE_ESDS );
                if( !esds || !esds->ES )
                    return -1;
                if( !lsmash_check_codec_type_identical( audio->summary.sample_type, ISOM_CODEC_TYPE_MP4A_AUDIO ) )
                    /* This is needed when copying descriptions. */
                    mp4sys_setup_summary_from_DecoderSpecificInfo( &audio->summary, esds->ES );
#endif
                *audio_pli = mp4a_max_audioProfileLevelIndication( *audio_pli, mp4a_get_audioProfileLevelIndication( &audio->summary ) );
            }
            else
                /* NOTE: Audio CODECs other than 'mp4a' does not have appropriate pli. */
                *audio_pli = MP4A_AUDIO_PLI_NOT_SPECIFIED;
        }
        else
            ;   /* FIXME: Do we have to set OD_profileLevelIndication? */
    }
    return 0;
}

int isom_add_iods( isom_moov_t *moov )
{
    if( !moov
     || !moov->trak_list
     ||  moov->iods )
        return -1;
    isom_create_box( iods, moov, ISOM_BOX_TYPE_IODS );
    iods->OD = mp4sys_create_ObjectDescriptor( 1 ); /* NOTE: Use 1 for ObjectDescriptorID of IOD. */
    if( !iods->OD )
    {
        lsmash_free( iods );
        return -1;
    }
    mp4a_audioProfileLevelIndication     audio_pli = MP4A_AUDIO_PLI_NONE_REQUIRED;
    mp4sys_visualProfileLevelIndication visual_pli = MP4SYS_VISUAL_PLI_NONE_REQUIRED;
    for( lsmash_entry_t *entry = moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_t* trak = (isom_trak_t*)entry->data;
        if( !trak
         || !trak->tkhd )
        {
            lsmash_free( iods );
            return -1;
        }
        if( isom_scan_trak_profileLevelIndication( trak, &audio_pli, &visual_pli ) )
        {
            lsmash_free( iods );
            return -1;
        }
        if( mp4sys_add_ES_ID_Inc( iods->OD, trak->tkhd->track_ID ) )
        {
            lsmash_free( iods );
            return -1;
        }
    }
    if( mp4sys_to_InitialObjectDescriptor( iods->OD,
                                           0, /* FIXME: I'm not quite sure what the spec says. */
                                           MP4SYS_OD_PLI_NONE_REQUIRED, MP4SYS_SCENE_PLI_NONE_REQUIRED,
                                           audio_pli, visual_pli,
                                           MP4SYS_GRAPHICS_PLI_NONE_REQUIRED ) )
    {
        lsmash_free( iods );
        return -1;
    }
    moov->iods = iods;
    return 0;
}

isom_trak_t *isom_add_trak( lsmash_root_t *root )
{
    if( !root
     || !root->moov )
        return NULL;
    isom_moov_t *moov = root->moov;
    if( !moov->trak_list )
    {
        moov->trak_list = lsmash_create_entry_list();
        if( !moov->trak_list )
            return NULL;
    }
    isom_trak_t *trak = lsmash_malloc_zero( sizeof(isom_trak_t) );
    if( !trak )
        return NULL;
    isom_init_box_common( trak, moov, ISOM_BOX_TYPE_TRAK, isom_remove_trak );
    isom_cache_t *cache = lsmash_malloc_zero( sizeof(isom_cache_t) );
    if( !cache )
    {
        lsmash_free( trak );
        return NULL;
    }
    isom_fragment_t *fragment = NULL;
    if( root->fragment )
    {
        fragment = lsmash_malloc_zero( sizeof(isom_fragment_t) );
        if( !fragment )
        {
            lsmash_free( cache );
            lsmash_free( trak );
            return NULL;
        }
        cache->fragment = fragment;
    }
    if( lsmash_add_entry( moov->trak_list, trak ) )
    {
        if( fragment )
            lsmash_free( fragment );
        lsmash_free( cache );
        lsmash_free( trak );
        return NULL;
    }
    trak->cache = cache;
    return trak;
}

int isom_add_tkhd( isom_trak_t *trak, uint32_t handler_type )
{
    if( !trak
     || !trak->root
     || !trak->root->moov
     || !trak->root->moov->mvhd
     || !trak->root->moov->trak_list )
        return -1;
    if( !trak->tkhd )
    {
        isom_create_box( tkhd, trak, ISOM_BOX_TYPE_TKHD );
        if( handler_type == ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK )
            tkhd->volume = 0x0100;
        tkhd->matrix[0] = 0x00010000;
        tkhd->matrix[4] = 0x00010000;
        tkhd->matrix[8] = 0x40000000;
        tkhd->duration  = 0xffffffff;
        tkhd->track_ID  = trak->root->moov->mvhd->next_track_ID;
        ++ trak->root->moov->mvhd->next_track_ID;
        trak->tkhd = tkhd;
    }
    return 0;
}

int isom_add_tapt( isom_trak_t *trak )
{
    if( trak->tapt )
        return 0;
    isom_create_box( tapt, trak, QT_BOX_TYPE_TAPT );
    trak->tapt = tapt;
    return 0;
}

int isom_add_clef( isom_tapt_t *tapt )
{
    if( tapt->clef )
        return 0;
    isom_create_box( clef, tapt, QT_BOX_TYPE_CLEF );
    tapt->clef = clef;
    return 0;
}

int isom_add_prof( isom_tapt_t *tapt )
{
    if( tapt->prof )
        return 0;
    isom_create_box( prof, tapt, QT_BOX_TYPE_PROF );
    tapt->prof = prof;
    return 0;
}

int isom_add_enof( isom_tapt_t *tapt )
{
    if( tapt->enof )
        return 0;
    isom_create_box( enof, tapt, QT_BOX_TYPE_ENOF );
    tapt->enof = enof;
    return 0;
}

int isom_add_elst( isom_edts_t *edts )
{
    if( edts->elst )
        return 0;
    isom_create_list_box( elst, edts, ISOM_BOX_TYPE_ELST );
    edts->elst = elst;
    return 0;
}

int isom_add_edts( isom_trak_t *trak )
{
    if( trak->edts )
        return 0;
    isom_create_box( edts, trak, ISOM_BOX_TYPE_EDTS );
    trak->edts = edts;
    return 0;
}

int isom_add_tref( isom_trak_t *trak )
{
    if( trak->tref )
        return 0;
    isom_create_box( tref, trak, ISOM_BOX_TYPE_TREF );
    tref->ref_list = lsmash_create_entry_list();
    if( !tref->ref_list )
    {
        lsmash_free( tref );
        return -1;
    }
    trak->tref = tref;
    return 0;
}

int isom_add_mdia( isom_trak_t *trak )
{
    if( !trak || trak->mdia )
        return -1;
    isom_create_box( mdia, trak, ISOM_BOX_TYPE_MDIA );
    trak->mdia = mdia;
    return 0;
}


int isom_add_mdhd( isom_mdia_t *mdia, uint16_t default_language )
{
    if( !mdia || mdia->mdhd )
        return -1;
    isom_create_box( mdhd, mdia, ISOM_BOX_TYPE_MDHD );
    mdhd->language = default_language;
    mdia->mdhd = mdhd;
    return 0;
}

int isom_add_hdlr( isom_mdia_t *mdia, isom_meta_t *meta, isom_minf_t *minf, uint32_t media_type )
{
    if( (!mdia && !meta && !minf)
     || (mdia && meta)
     || (meta && minf)
     || (minf && mdia) )
        return -1;    /* Either one must be given. */
    if( (mdia && mdia->hdlr)
     || (meta && meta->hdlr)
     || (minf && minf->hdlr) )
        return -1;    /* Selected one must not have hdlr yet. */
    isom_box_t *parent = mdia ? (isom_box_t *)mdia : meta ? (isom_box_t *)meta : (isom_box_t *)minf;
    isom_create_box( hdlr, parent, ISOM_BOX_TYPE_HDLR );
    lsmash_root_t *root = hdlr->root;
    uint32_t type    = mdia ? (root->qt_compatible ? QT_HANDLER_TYPE_MEDIA : 0) : (meta ? 0 : QT_HANDLER_TYPE_DATA);
    uint32_t subtype = media_type;
    hdlr->componentType    = type;
    hdlr->componentSubtype = subtype;
    char *type_name    = NULL;
    char *subtype_name = NULL;
    uint8_t type_name_length    = 0;
    uint8_t subtype_name_length = 0;
    if( mdia )
        type_name = "Media ";
    else if( meta )
        type_name = "Metadata ";
    else    /* if( minf ) */
        type_name = "Data ";
    type_name_length = strlen( type_name );
    struct
    {
        uint32_t subtype;
        char    *subtype_name;
        uint8_t  subtype_name_length;
    } subtype_table[] =
        {
            { ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK,          "Sound ",    6 },
            { ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK,          "Video",     6 },
            { ISOM_MEDIA_HANDLER_TYPE_HINT_TRACK,           "Hint ",     5 },
            { ISOM_MEDIA_HANDLER_TYPE_TIMED_METADATA_TRACK, "Metadata ", 9 },
            { ISOM_MEDIA_HANDLER_TYPE_TEXT_TRACK,           "Text ",     5 },
            { ISOM_META_HANDLER_TYPE_ITUNES_METADATA,       "iTunes ",   7 },
            { QT_REFERENCE_HANDLER_TYPE_ALIAS,              "Alias ",    6 },
            { QT_REFERENCE_HANDLER_TYPE_RESOURCE,           "Resource ", 9 },
            { QT_REFERENCE_HANDLER_TYPE_URL,                "URL ",      4 },
            { subtype,                                      "Unknown ",  8 }
        };
    for( int i = 0; subtype_table[i].subtype; i++ )
        if( subtype == subtype_table[i].subtype )
        {
            subtype_name        = subtype_table[i].subtype_name;
            subtype_name_length = subtype_table[i].subtype_name_length;
            break;
        }
    uint32_t name_length = 15 + subtype_name_length + type_name_length + root->isom_compatible + root->qt_compatible;
    uint8_t *name = lsmash_malloc( name_length );
    if( !name )
    {
        lsmash_free( hdlr );
        return -1;
    }
    if( root->qt_compatible )
        name[0] = name_length & 0xff;
    memcpy( name + root->qt_compatible, "L-SMASH ", 8 );
    memcpy( name + root->qt_compatible + 8, subtype_name, subtype_name_length );
    memcpy( name + root->qt_compatible + 8 + subtype_name_length, type_name, type_name_length );
    memcpy( name + root->qt_compatible + 8 + subtype_name_length + type_name_length, "Handler", 7 );
    if( root->isom_compatible )
        name[name_length - 1] = 0;
    hdlr->componentName        = name;
    hdlr->componentName_length = name_length;
    if( mdia )
        mdia->hdlr = hdlr;
    else if( meta )
        meta->hdlr = hdlr;
    else
        minf->hdlr = hdlr;
    return 0;
}

int isom_add_minf( isom_mdia_t *mdia )
{
    if( !mdia || mdia->minf )
        return -1;
    isom_create_box( minf, mdia, ISOM_BOX_TYPE_MINF );
    mdia->minf = minf;
    return 0;
}

int isom_add_vmhd( isom_minf_t *minf )
{
    if( !minf || minf->vmhd )
        return -1;
    isom_create_box( vmhd, minf, ISOM_BOX_TYPE_VMHD );
    vmhd->flags = 0x000001;
    minf->vmhd = vmhd;
    return 0;
}

int isom_add_smhd( isom_minf_t *minf )
{
    if( !minf || minf->smhd )
        return -1;
    isom_create_box( smhd, minf, ISOM_BOX_TYPE_SMHD );
    minf->smhd = smhd;
    return 0;
}

int isom_add_hmhd( isom_minf_t *minf )
{
    if( !minf || minf->hmhd )
        return -1;
    isom_create_box( hmhd, minf, ISOM_BOX_TYPE_HMHD );
    minf->hmhd = hmhd;
    return 0;
}

int isom_add_nmhd( isom_minf_t *minf )
{
    if( !minf || minf->nmhd )
        return -1;
    isom_create_box( nmhd, minf, ISOM_BOX_TYPE_NMHD );
    minf->nmhd = nmhd;
    return 0;
}

int isom_add_gmhd( isom_minf_t *minf )
{
    if( !minf || minf->gmhd )
        return -1;
    isom_create_box( gmhd, minf, QT_BOX_TYPE_GMHD );
    minf->gmhd = gmhd;
    return 0;
}

int isom_add_gmin( isom_gmhd_t *gmhd )
{
    if( !gmhd || gmhd->gmin )
        return -1;
    isom_create_box( gmin, gmhd, QT_BOX_TYPE_GMIN );
    gmhd->gmin = gmin;
    return 0;
}

int isom_add_text( isom_gmhd_t *gmhd )
{
    if( !gmhd || gmhd->text )
        return -1;
    isom_create_box( text, gmhd, QT_BOX_TYPE_TEXT );
    text->matrix[0] = 0x00010000;
    text->matrix[4] = 0x00010000;
    text->matrix[8] = 0x40000000;
    gmhd->text = text;
    return 0;
}

int isom_add_dinf( isom_minf_t *minf )
{
    if( !minf || minf->dinf )
        return -1;
    isom_create_box( dinf, minf, ISOM_BOX_TYPE_DINF );
    minf->dinf = dinf;
    return 0;
}

int isom_add_dref( isom_dinf_t *dinf )
{
    if( !dinf || dinf->dref )
        return -1;
    isom_create_list_box( dref, dinf, ISOM_BOX_TYPE_DREF );
    dinf->dref = dref;
    if( isom_add_dref_entry( dref, 0x000001, NULL, NULL ) )
        return -1;
    return 0;
}

int isom_add_stbl( isom_minf_t *minf )
{
    if( !minf || minf->stbl )
        return -1;
    isom_create_box( stbl, minf, ISOM_BOX_TYPE_STBL );
    minf->stbl = stbl;
    return 0;
}

int isom_add_stsd( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stsd )
        return -1;
    isom_create_list_box( stsd, stbl, ISOM_BOX_TYPE_STSD );
    stbl->stsd = stsd;
    return 0;
}

int isom_add_stts( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stts )
        return -1;
    isom_create_list_box( stts, stbl, ISOM_BOX_TYPE_STTS );
    stbl->stts = stts;
    return 0;
}

int isom_add_ctts( isom_stbl_t *stbl )
{
    if( !stbl || stbl->ctts )
        return -1;
    isom_create_list_box( ctts, stbl, ISOM_BOX_TYPE_CTTS );
    stbl->ctts = ctts;
    return 0;
}

int isom_add_cslg( isom_stbl_t *stbl )
{
    if( !stbl || stbl->cslg )
        return -1;
    isom_create_box( cslg, stbl, ISOM_BOX_TYPE_CSLG );
    stbl->cslg = cslg;
    return 0;
}

int isom_add_stsc( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stsc )
        return -1;
    isom_create_list_box( stsc, stbl, ISOM_BOX_TYPE_STSC );
    stbl->stsc = stsc;
    return 0;
}

int isom_add_stsz( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stsz )
        return -1;
    isom_create_box( stsz, stbl, ISOM_BOX_TYPE_STSZ );  /* We don't create a list here. */
    stbl->stsz = stsz;
    return 0;
}

int isom_add_stss( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stss )
        return -1;
    isom_create_list_box( stss, stbl, ISOM_BOX_TYPE_STSS );
    stbl->stss = stss;
    return 0;
}

int isom_add_stps( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stps )
        return -1;
    isom_create_list_box( stps, stbl, QT_BOX_TYPE_STPS );
    stbl->stps = stps;
    return 0;
}

int isom_add_sdtp( isom_box_t *parent )
{
    if( !parent )
        return -1;
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STBL ) )
    {
        isom_stbl_t *stbl = (isom_stbl_t *)parent;
        if( stbl->sdtp )
            return -1;
        isom_create_list_box( sdtp, stbl, ISOM_BOX_TYPE_SDTP );
        stbl->sdtp = sdtp;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAF ) )
    {
        isom_traf_t *traf = (isom_traf_t *)parent;
        if( traf->sdtp )
            return -1;
        isom_create_list_box( sdtp, traf, ISOM_BOX_TYPE_SDTP );
        traf->sdtp = sdtp;
    }
    else
        assert( 0 );
    return 0;
}

isom_sgpd_t *isom_add_sgpd( isom_stbl_t *stbl, uint32_t grouping_type )
{
    if( !stbl )
        return NULL;
    if( !stbl->sgpd_list )
    {
        stbl->sgpd_list = lsmash_create_entry_list();
        if( !stbl->sgpd_list )
            return NULL;
    }
    isom_sgpd_t *sgpd = lsmash_malloc_zero( sizeof(isom_sgpd_t) );
    if( !sgpd )
        return NULL;
    isom_init_box_common( sgpd, stbl, ISOM_BOX_TYPE_SGPD, isom_remove_sgpd );
    sgpd->list = lsmash_create_entry_list();
    if( !sgpd->list || lsmash_add_entry( stbl->sgpd_list, sgpd ) )
    {
        lsmash_free( sgpd );
        return NULL;
    }
    sgpd->grouping_type = grouping_type;
    sgpd->version       = 1;    /* We use version 1 because it is recommended in the spec. */
    switch( grouping_type )
    {
        case ISOM_GROUP_TYPE_RAP :
            sgpd->default_length = 1;
            break;
        case ISOM_GROUP_TYPE_ROLL :
            sgpd->default_length = 2;
            break;
        default :
            /* We don't consider other grouping types currently. */
            break;
    }
    return sgpd;
}

isom_sbgp_t *isom_add_sbgp( isom_stbl_t *stbl, uint32_t grouping_type )
{
    if( !stbl )
        return NULL;
    if( !stbl->sbgp_list )
    {
        stbl->sbgp_list = lsmash_create_entry_list();
        if( !stbl->sbgp_list )
            return NULL;
    }
    isom_sbgp_t *sbgp = lsmash_malloc_zero( sizeof(isom_sbgp_t) );
    if( !sbgp )
        return NULL;
    isom_init_box_common( sbgp, stbl, ISOM_BOX_TYPE_SBGP, isom_remove_sbgp );
    sbgp->list = lsmash_create_entry_list();
    if( !sbgp->list || lsmash_add_entry( stbl->sbgp_list, sbgp ) )
    {
        lsmash_free( sbgp );
        return NULL;
    }
    sbgp->grouping_type = grouping_type;
    return sbgp;
}

int isom_add_chpl( isom_moov_t *moov )
{
    if( !moov
     || !moov->udta
     ||  moov->udta->chpl )
        return -1;
    isom_create_list_box( chpl, moov->udta, ISOM_BOX_TYPE_CHPL );
    chpl->version = 1;      /* version = 1 is popular. */
    moov->udta->chpl = chpl;
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
        lsmash_free( metaitem );
        return -1;
    }
    return 0;
}

int isom_add_mean( isom_metaitem_t *metaitem )
{
    if( !metaitem || metaitem->mean )
        return -1;
    isom_create_box( mean, metaitem, ISOM_BOX_TYPE_MEAN );
    metaitem->mean = mean;
    return 0;
}

int isom_add_name( isom_metaitem_t *metaitem )
{
    if( !metaitem || metaitem->name )
        return -1;
    isom_create_box( name, metaitem, ISOM_BOX_TYPE_NAME );
    metaitem->name = name;
    return 0;
}

int isom_add_data( isom_metaitem_t *metaitem )
{
    if( !metaitem || metaitem->data )
        return -1;
    isom_create_box( data, metaitem, ISOM_BOX_TYPE_DATA );
    metaitem->data = data;
    return 0;
}

int isom_add_ilst( isom_moov_t *moov )
{
    if( !moov
     || !moov->udta
     || !moov->udta->meta
     ||  moov->udta->meta->ilst )
        return -1;
    isom_create_box( ilst, moov->udta->meta, ISOM_BOX_TYPE_ILST );
    ilst->item_list = lsmash_create_entry_list();
    if( !ilst->item_list )
    {
        lsmash_free( ilst );
        return -1;
    }
    moov->udta->meta->ilst = ilst;
    return 0;
}

int isom_add_meta( isom_box_t *parent )
{
    if( !parent )
        return -1;
    isom_create_box( meta, parent, ISOM_BOX_TYPE_META );
    if( lsmash_check_box_type_identical( parent->type, LSMASH_BOX_TYPE_UNSPECIFIED ) )
    {
        lsmash_root_t *root = (lsmash_root_t *)parent;
        if( root->meta )
        {
            lsmash_free( meta );
            return -1;
        }
        root->meta = meta;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MOOV ) )
    {
        isom_moov_t *moov = (isom_moov_t *)parent;
        if( moov->meta )
        {
            lsmash_free( meta );
            return -1;
        }
        moov->meta = meta;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAK ) )
    {
        isom_trak_t *trak = (isom_trak_t *)parent;
        if( trak->meta )
        {
            lsmash_free( meta );
            return -1;
        }
        trak->meta = meta;
    }
    else
    {
        isom_udta_t *udta = (isom_udta_t *)parent;
        if( udta->meta )
        {
            lsmash_free( meta );
            return -1;
        }
        udta->meta = meta;
    }
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
        lsmash_free( cprt );
        return -1;
    }
    return 0;
}

int isom_add_udta( lsmash_root_t *root, uint32_t track_ID )
{
    /* track_ID == 0 means the direct addition to moov box */
    if( track_ID == 0 )
    {
        if( !root
         || !root->moov )
            return -1;
        if( root->moov->udta )
            return 0;
        isom_create_box( udta, root->moov, ISOM_BOX_TYPE_UDTA );
        root->moov->udta = udta;
        return 0;
    }
    isom_trak_t *trak = isom_get_trak( root, track_ID );
    if( !trak )
        return -1;
    if( trak->udta )
        return 0;
    isom_create_box( udta, trak, ISOM_BOX_TYPE_UDTA );
    trak->udta = udta;
    return 0;
}

int isom_add_mvex( isom_moov_t *moov )
{
    if( !moov || moov->mvex )
        return -1;
    isom_create_box( mvex, moov, ISOM_BOX_TYPE_MVEX );
    moov->mvex = mvex;
    return 0;
}

int isom_add_mehd( isom_mvex_t *mvex )
{
    if( !mvex || mvex->mehd )
        return -1;
    isom_create_box( mehd, mvex, ISOM_BOX_TYPE_MEHD );
    mvex->mehd = mehd;
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
    isom_trex_t *trex = lsmash_malloc_zero( sizeof(isom_trex_t) );
    if( !trex )
        return NULL;
    isom_init_box_common( trex, mvex, ISOM_BOX_TYPE_TREX, isom_remove_trex );
    if( lsmash_add_entry( mvex->trex_list, trex ) )
    {
        lsmash_free( trex );
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
    isom_moof_t *moof = lsmash_malloc_zero( sizeof(isom_moof_t) );
    if( !moof )
        return NULL;
    isom_init_box_common( moof, root, ISOM_BOX_TYPE_MOOF, isom_remove_moof );
    if( lsmash_add_entry( root->moof_list, moof ) )
    {
        lsmash_free( moof );
        return NULL;
    }
    return moof;
}

int isom_add_mfhd( isom_moof_t *moof )
{
    if( !moof || moof->mfhd )
        return -1;
    isom_create_box( mfhd, moof, ISOM_BOX_TYPE_MFHD );
    moof->mfhd = mfhd;
    return 0;
}

isom_traf_t *isom_add_traf( lsmash_root_t *root, isom_moof_t *moof )
{
    if( !root
     || !root->moof_list
     || !moof )
        return NULL;
    if( !moof->traf_list )
    {
        moof->traf_list = lsmash_create_entry_list();
        if( !moof->traf_list )
            return NULL;
    }
    isom_traf_t *traf = lsmash_malloc_zero( sizeof(isom_traf_t) );
    if( !traf )
        return NULL;
    isom_init_box_common( traf, moof, ISOM_BOX_TYPE_TRAF, isom_remove_traf );
    isom_cache_t *cache = lsmash_malloc( sizeof(isom_cache_t) );
    if( !cache )
    {
        lsmash_free( traf );
        return NULL;
    }
    memset( cache, 0, sizeof(isom_cache_t) );
    if( lsmash_add_entry( moof->traf_list, traf ) )
    {
        lsmash_free( cache );
        lsmash_free( traf );
        return NULL;
    }
    traf->cache = cache;
    return traf;
}

int isom_add_tfhd( isom_traf_t *traf )
{
    if( !traf || traf->tfhd )
        return -1;
    isom_create_box( tfhd, traf, ISOM_BOX_TYPE_TFHD );
    traf->tfhd = tfhd;
    return 0;
}

int isom_add_tfdt( isom_traf_t *traf )
{
    if( !traf || traf->tfdt )
        return -1;
    isom_create_box( tfdt, traf, ISOM_BOX_TYPE_TFDT );
    traf->tfdt = tfdt;
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
    isom_trun_t *trun = lsmash_malloc_zero( sizeof(isom_trun_t) );
    if( !trun )
        return NULL;
    isom_init_box_common( trun, traf, ISOM_BOX_TYPE_TRUN, isom_remove_trun );
    if( lsmash_add_entry( traf->trun_list, trun ) )
    {
        lsmash_free( trun );
        return NULL;
    }
    return trun;
}

int isom_add_mfra( lsmash_root_t *root )
{
    if( !root || root->mfra )
        return -1;
    isom_create_box( mfra, root, ISOM_BOX_TYPE_MFRA );
    root->mfra = mfra;
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
    isom_tfra_t *tfra = lsmash_malloc_zero( sizeof(isom_tfra_t) );
    if( !tfra )
        return NULL;
    isom_init_box_common( tfra, mfra, ISOM_BOX_TYPE_TFRA, isom_remove_tfra );
    if( lsmash_add_entry( mfra->tfra_list, tfra ) )
    {
        lsmash_free( tfra );
        return NULL;
    }
    return tfra;
}

int isom_add_mfro( isom_mfra_t *mfra )
{
    if( !mfra || mfra->mfro )
        return -1;
    isom_create_box( mfro, mfra, ISOM_BOX_TYPE_MFRO );
    mfra->mfro = mfro;
    return 0;
}
