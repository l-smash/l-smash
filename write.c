/*****************************************************************************
 * write.c:
 *****************************************************************************
 * Copyright (C) 2010-2012 L-SMASH project
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
#include <inttypes.h>

#include "box.h"
#include "isom.h"
#include "mp4a.h"
#include "mp4sys.h"
#include "write.h"

static void isom_bs_put_basebox_common( lsmash_bs_t *bs, isom_box_t *box )
{
    if( box->size > UINT32_MAX )
    {
        lsmash_bs_put_be32( bs, 1 );
        lsmash_bs_put_be32( bs, box->type );
        lsmash_bs_put_be64( bs, box->size );    /* largesize */
    }
    else
    {
        lsmash_bs_put_be32( bs, (uint32_t)box->size );
        lsmash_bs_put_be32( bs, box->type );
    }
    if( box->type == ISOM_BOX_TYPE_UUID )
        lsmash_bs_put_bytes( bs, 16, box->usertype );
}

static void isom_bs_put_fullbox_common( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_bs_put_basebox_common( bs, box );
    lsmash_bs_put_byte( bs, box->version );
    lsmash_bs_put_be24( bs, box->flags );
}

static void isom_bs_put_box_common( lsmash_bs_t *bs, void *box )
{
    if( !box )
    {
        bs->error = 1;
        return;
    }
    isom_box_t *parent = ((isom_box_t *)box)->parent;
    if( parent && parent->type == ISOM_BOX_TYPE_STSD )
    {
        isom_bs_put_basebox_common( bs, (isom_box_t *)box );
        return;
    }
    if( isom_is_fullbox( box ) )
        isom_bs_put_fullbox_common( bs, (isom_box_t *)box );
    else
        isom_bs_put_basebox_common( bs, (isom_box_t *)box );
}

static int isom_write_tkhd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_tkhd_t *tkhd = trak->tkhd;
    if( !tkhd )
        return -1;
    isom_bs_put_box_common( bs, tkhd );
    if( tkhd->version )
    {
        lsmash_bs_put_be64( bs, tkhd->creation_time );
        lsmash_bs_put_be64( bs, tkhd->modification_time );
        lsmash_bs_put_be32( bs, tkhd->track_ID );
        lsmash_bs_put_be32( bs, tkhd->reserved1 );
        lsmash_bs_put_be64( bs, tkhd->duration );
    }
    else
    {
        lsmash_bs_put_be32( bs, (uint32_t)tkhd->creation_time );
        lsmash_bs_put_be32( bs, (uint32_t)tkhd->modification_time );
        lsmash_bs_put_be32( bs, tkhd->track_ID );
        lsmash_bs_put_be32( bs, tkhd->reserved1 );
        lsmash_bs_put_be32( bs, (uint32_t)tkhd->duration );
    }
    lsmash_bs_put_be32( bs, tkhd->reserved2[0] );
    lsmash_bs_put_be32( bs, tkhd->reserved2[1] );
    lsmash_bs_put_be16( bs, tkhd->layer );
    lsmash_bs_put_be16( bs, tkhd->alternate_group );
    lsmash_bs_put_be16( bs, tkhd->volume );
    lsmash_bs_put_be16( bs, tkhd->reserved3 );
    for( uint32_t i = 0; i < 9; i++ )
        lsmash_bs_put_be32( bs, tkhd->matrix[i] );
    lsmash_bs_put_be32( bs, tkhd->width );
    lsmash_bs_put_be32( bs, tkhd->height );
    return lsmash_bs_write_data( bs );
}

static int isom_write_clef( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_clef_t *clef = trak->tapt->clef;
    if( !clef )
        return 0;
    isom_bs_put_box_common( bs, clef );
    lsmash_bs_put_be32( bs, clef->width );
    lsmash_bs_put_be32( bs, clef->height );
    return lsmash_bs_write_data( bs );
}

static int isom_write_prof( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_prof_t *prof = trak->tapt->prof;
    if( !prof )
        return 0;
    isom_bs_put_box_common( bs, prof );
    lsmash_bs_put_be32( bs, prof->width );
    lsmash_bs_put_be32( bs, prof->height );
    return lsmash_bs_write_data( bs );
}

static int isom_write_enof( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_enof_t *enof = trak->tapt->enof;
    if( !enof )
        return 0;
    isom_bs_put_box_common( bs, enof );
    lsmash_bs_put_be32( bs, enof->width );
    lsmash_bs_put_be32( bs, enof->height );
    return lsmash_bs_write_data( bs );
}

static int isom_write_tapt( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_tapt_t *tapt = trak->tapt;
    if( !tapt )
        return 0;
    isom_bs_put_box_common( bs, tapt );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_clef( bs, trak )
     || isom_write_prof( bs, trak )
     || isom_write_enof( bs, trak ) )
        return -1;
    return 0;
}

static int isom_write_elst( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_elst_t *elst = trak->edts->elst;
    if( !elst )
        return -1;
    if( !elst->list->entry_count )
        return 0;
    if( elst->root->fragment && elst->root->bs->stream != stdout )
        elst->pos = elst->root->bs->written;    /* Remember to rewrite entries. */
    isom_bs_put_box_common( bs, elst );
    lsmash_bs_put_be32( bs, elst->list->entry_count );
    for( lsmash_entry_t *entry = elst->list->head; entry; entry = entry->next )
    {
        isom_elst_entry_t *data = (isom_elst_entry_t *)entry->data;
        if( !data )
            return -1;
        if( elst->version )
        {
            lsmash_bs_put_be64( bs, data->segment_duration );
            lsmash_bs_put_be64( bs, data->media_time );
        }
        else
        {
            lsmash_bs_put_be32( bs, (uint32_t)data->segment_duration );
            lsmash_bs_put_be32( bs, (uint32_t)data->media_time );
        }
        lsmash_bs_put_be32( bs, data->media_rate );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_edts( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_edts_t *edts = trak->edts;
    if( !edts )
        return 0;
    isom_bs_put_box_common( bs, edts );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    return isom_write_elst( bs, trak );
}

static int isom_write_tref( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_tref_t *tref = trak->tref;
    if( !tref )
        return 0;
    isom_bs_put_box_common( bs, tref );
    if( tref->ref_list )
        for( lsmash_entry_t *entry = tref->ref_list->head; entry; entry = entry->next )
        {
            isom_tref_type_t *ref = (isom_tref_type_t *)entry->data;
            if( !ref )
                return -1;
            isom_bs_put_box_common( bs, ref );
            for( uint32_t i = 0; i < ref->ref_count; i++ )
                lsmash_bs_put_be32( bs, ref->track_ID[i] );
        }
    return lsmash_bs_write_data( bs );
}

static int isom_write_mdhd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    if( !mdhd )
        return -1;
    isom_bs_put_box_common( bs, mdhd );
    if( mdhd->version )
    {
        lsmash_bs_put_be64( bs, mdhd->creation_time );
        lsmash_bs_put_be64( bs, mdhd->modification_time );
        lsmash_bs_put_be32( bs, mdhd->timescale );
        lsmash_bs_put_be64( bs, mdhd->duration );
    }
    else
    {
        lsmash_bs_put_be32( bs, (uint32_t)mdhd->creation_time );
        lsmash_bs_put_be32( bs, (uint32_t)mdhd->modification_time );
        lsmash_bs_put_be32( bs, mdhd->timescale );
        lsmash_bs_put_be32( bs, (uint32_t)mdhd->duration );
    }
    lsmash_bs_put_be16( bs, mdhd->language );
    lsmash_bs_put_be16( bs, mdhd->quality );
    return lsmash_bs_write_data( bs );
}

static int isom_write_hdlr( lsmash_bs_t *bs, isom_hdlr_t *hdlr, uint32_t parent_type )
{
    if( !hdlr )
        return parent_type == ISOM_BOX_TYPE_MINF ? 0 : -1;
    isom_bs_put_box_common( bs, hdlr );
    lsmash_bs_put_be32( bs, hdlr->componentType );
    lsmash_bs_put_be32( bs, hdlr->componentSubtype );
    lsmash_bs_put_be32( bs, hdlr->componentManufacturer );
    lsmash_bs_put_be32( bs, hdlr->componentFlags );
    lsmash_bs_put_be32( bs, hdlr->componentFlagsMask );
    lsmash_bs_put_bytes( bs, hdlr->componentName_length, hdlr->componentName );
    return lsmash_bs_write_data( bs );
}

static int isom_write_vmhd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_vmhd_t *vmhd = trak->mdia->minf->vmhd;
    if( !vmhd )
        return -1;
    isom_bs_put_box_common( bs, vmhd );
    lsmash_bs_put_be16( bs, vmhd->graphicsmode );
    for( uint32_t i = 0; i < 3; i++ )
        lsmash_bs_put_be16( bs, vmhd->opcolor[i] );
    return lsmash_bs_write_data( bs );
}

static int isom_write_smhd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_smhd_t *smhd = trak->mdia->minf->smhd;
    if( !smhd )
        return -1;
    isom_bs_put_box_common( bs, smhd );
    lsmash_bs_put_be16( bs, smhd->balance );
    lsmash_bs_put_be16( bs, smhd->reserved );
    return lsmash_bs_write_data( bs );
}

static int isom_write_hmhd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_hmhd_t *hmhd = trak->mdia->minf->hmhd;
    if( !hmhd )
        return -1;
    isom_bs_put_box_common( bs, hmhd );
    lsmash_bs_put_be16( bs, hmhd->maxPDUsize );
    lsmash_bs_put_be16( bs, hmhd->avgPDUsize );
    lsmash_bs_put_be32( bs, hmhd->maxbitrate );
    lsmash_bs_put_be32( bs, hmhd->avgbitrate );
    lsmash_bs_put_be32( bs, hmhd->reserved );
    return lsmash_bs_write_data( bs );
}

static int isom_write_nmhd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_nmhd_t *nmhd = trak->mdia->minf->nmhd;
    if( !nmhd )
        return -1;
    isom_bs_put_box_common( bs, nmhd );
    return lsmash_bs_write_data( bs );
}

static int isom_write_gmin( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_gmin_t *gmin = trak->mdia->minf->gmhd->gmin;
    if( !gmin )
        return -1;
    isom_bs_put_box_common( bs, gmin );
    lsmash_bs_put_be16( bs, gmin->graphicsmode );
    for( uint32_t i = 0; i < 3; i++ )
        lsmash_bs_put_be16( bs, gmin->opcolor[i] );
    lsmash_bs_put_be16( bs, gmin->balance );
    lsmash_bs_put_be16( bs, gmin->reserved );
    return lsmash_bs_write_data( bs );
}

static int isom_write_text( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_text_t *text = trak->mdia->minf->gmhd->text;
    if( !text )
        return -1;
    isom_bs_put_box_common( bs, text );
    for( uint32_t i = 0; i < 9; i++ )
        lsmash_bs_put_be32( bs, text->matrix[i] );
    return lsmash_bs_write_data( bs );
}

static int isom_write_gmhd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_gmhd_t *gmhd = trak->mdia->minf->gmhd;
    if( !gmhd )
        return -1;
    isom_bs_put_box_common( bs, gmhd );
    if( isom_write_gmin( bs, trak ) ||
        isom_write_text( bs, trak ) )
        return -1;
    return 0;
}

static int isom_write_dref( lsmash_bs_t *bs, isom_dref_t *dref )
{
    if( !dref || !dref->list )
        return -1;
    isom_bs_put_box_common( bs, dref );
    lsmash_bs_put_be32( bs, dref->list->entry_count );
    for( lsmash_entry_t *entry = dref->list->head; entry; entry = entry->next )
    {
        isom_dref_entry_t *data = (isom_dref_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_box_common( bs, data );
        if( data->type == ISOM_BOX_TYPE_URN )
            lsmash_bs_put_bytes( bs, data->name_length, data->name );
        lsmash_bs_put_bytes( bs, data->location_length, data->location );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_dinf( lsmash_bs_t *bs, isom_dinf_t *dinf, uint32_t parent_type )
{
    if( !dinf )
        return parent_type == ISOM_BOX_TYPE_MINF ? -1 : 0;
    isom_bs_put_box_common( bs, dinf );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    return isom_write_dref( bs, dinf->dref );
}

static int isom_write_pasp( lsmash_bs_t *bs, isom_pasp_t *pasp )
{
    if( !pasp )
        return 0;
    isom_bs_put_box_common( bs, pasp );
    lsmash_bs_put_be32( bs, pasp->hSpacing );
    lsmash_bs_put_be32( bs, pasp->vSpacing );
    return lsmash_bs_write_data( bs );
}

static int isom_write_clap( lsmash_bs_t *bs, isom_clap_t *clap )
{
    if( !clap )
        return 0;
    isom_bs_put_box_common( bs, clap );
    lsmash_bs_put_be32( bs, clap->cleanApertureWidthN );
    lsmash_bs_put_be32( bs, clap->cleanApertureWidthD );
    lsmash_bs_put_be32( bs, clap->cleanApertureHeightN );
    lsmash_bs_put_be32( bs, clap->cleanApertureHeightD );
    lsmash_bs_put_be32( bs, clap->horizOffN );
    lsmash_bs_put_be32( bs, clap->horizOffD );
    lsmash_bs_put_be32( bs, clap->vertOffN );
    lsmash_bs_put_be32( bs, clap->vertOffD );
    return lsmash_bs_write_data( bs );
}

static int isom_write_colr( lsmash_bs_t *bs, isom_colr_t *colr )
{
    if( !colr || colr->color_parameter_type == QT_COLOR_PARAMETER_TYPE_PROF )
        return 0;
    isom_bs_put_box_common( bs, colr );
    lsmash_bs_put_be32( bs, colr->color_parameter_type );
    lsmash_bs_put_be16( bs, colr->primaries_index );
    lsmash_bs_put_be16( bs, colr->transfer_function_index );
    lsmash_bs_put_be16( bs, colr->matrix_index );
    return lsmash_bs_write_data( bs );
}

static int isom_write_gama( lsmash_bs_t *bs, isom_gama_t *gama )
{
    /* Note: 'gama' box is superseded by 'colr' box.
     * Therefore, writers of QTFF should never write both 'colr' and 'gama' box into an Image Description. */
    if( !gama || (gama->parent && ((isom_visual_entry_t *)gama->parent)->colr) )
        return 0;
    isom_bs_put_box_common( bs, gama );
    lsmash_bs_put_be32( bs, gama->level );
    return lsmash_bs_write_data( bs );
}

static int isom_write_fiel( lsmash_bs_t *bs, isom_fiel_t *fiel )
{
    if( !fiel )
        return 0;
    isom_bs_put_box_common( bs, fiel );
    lsmash_bs_put_byte( bs, fiel->fields );
    lsmash_bs_put_byte( bs, fiel->detail );
    return lsmash_bs_write_data( bs );
}

static int isom_write_cspc( lsmash_bs_t *bs, isom_cspc_t *cspc )
{
    if( !cspc )
        return 0;
    isom_bs_put_box_common( bs, cspc );
    lsmash_bs_put_be32( bs, cspc->pixel_format );
    return lsmash_bs_write_data( bs );
}

static int isom_write_sgbt( lsmash_bs_t *bs, isom_sgbt_t *sgbt )
{
    if( !sgbt )
        return 0;
    isom_bs_put_box_common( bs, sgbt );
    lsmash_bs_put_byte( bs, sgbt->significantBits );
    return lsmash_bs_write_data( bs );
}

static int isom_write_stsl( lsmash_bs_t *bs, isom_stsl_t *stsl )
{
    if( !stsl )
        return 0;
    isom_bs_put_box_common( bs, stsl );
    lsmash_bs_put_byte( bs, stsl->constraint_flag );
    lsmash_bs_put_byte( bs, stsl->scale_method );
    lsmash_bs_put_be16( bs, stsl->display_center_x );
    lsmash_bs_put_be16( bs, stsl->display_center_y );
    return lsmash_bs_write_data( bs );
}

static int isom_write_esds( lsmash_bs_t *bs, isom_esds_t *esds )
{
    if( !esds )
        return 0;
    isom_bs_put_box_common( bs, esds );
    return mp4sys_write_ES_Descriptor( bs, esds->ES );
}

static int isom_put_ps_entries( lsmash_bs_t *bs, lsmash_entry_list_t *list )
{
    for( lsmash_entry_t *entry = list->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be16( bs, data->parameterSetLength );
        lsmash_bs_put_bytes( bs, data->parameterSetLength, data->parameterSetNALUnit );
    }
    return 0;
}

static int isom_write_avcC( lsmash_bs_t *bs, isom_avcC_t *avcC )
{
    if( !avcC )
        return 0;
    if( !avcC->sequenceParameterSets || !avcC->pictureParameterSets )
        return -1;
    isom_bs_put_box_common( bs, avcC );
    lsmash_bs_put_byte( bs, avcC->configurationVersion );
    lsmash_bs_put_byte( bs, avcC->AVCProfileIndication );
    lsmash_bs_put_byte( bs, avcC->profile_compatibility );
    lsmash_bs_put_byte( bs, avcC->AVCLevelIndication );
    lsmash_bs_put_byte( bs, avcC->lengthSizeMinusOne | 0xfc );            /* upper 6-bits are reserved as 111111b */
    lsmash_bs_put_byte( bs, avcC->numOfSequenceParameterSets | 0xe0 );    /* upper 3-bits are reserved as 111b */
    if( isom_put_ps_entries( bs, avcC->sequenceParameterSets ) )
        return -1;
    lsmash_bs_put_byte( bs, avcC->numOfPictureParameterSets );
    if( isom_put_ps_entries( bs, avcC->pictureParameterSets ) )
        return -1;
    if( ISOM_REQUIRES_AVCC_EXTENSION( avcC->AVCProfileIndication ) )
    {
        lsmash_bs_put_byte( bs, avcC->chroma_format | 0xfc );             /* upper 6-bits are reserved as 111111b */
        lsmash_bs_put_byte( bs, avcC->bit_depth_luma_minus8 | 0xf8 );     /* upper 5-bits are reserved as 11111b */
        lsmash_bs_put_byte( bs, avcC->bit_depth_chroma_minus8 | 0xf8 );   /* upper 5-bits are reserved as 11111b */
        lsmash_bs_put_byte( bs, avcC->numOfSequenceParameterSetExt );
        if( isom_put_ps_entries( bs, avcC->sequenceParameterSetExt ) )
            return -1;
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_btrt( lsmash_bs_t *bs, isom_btrt_t *btrt )
{
    if( !btrt )
        return 0;
    isom_bs_put_box_common( bs, btrt );
    lsmash_bs_put_be32( bs, btrt->bufferSizeDB );
    lsmash_bs_put_be32( bs, btrt->maxBitrate );
    lsmash_bs_put_be32( bs, btrt->avgBitrate );
    return lsmash_bs_write_data( bs );
}

static int isom_write_glbl( lsmash_bs_t *bs, isom_glbl_t *glbl )
{
    if( !glbl )
        return 0;
    isom_bs_put_box_common( bs, glbl );
    if( glbl->header_data && glbl->header_size )
        lsmash_bs_put_bytes( bs, glbl->header_size, glbl->header_data );
    return lsmash_bs_write_data( bs );
}

static int isom_write_visual_extensions( lsmash_bs_t *bs, isom_visual_entry_t *visual )
{
    if( !visual )
        return 0;
    if( isom_write_avcC( bs, visual->avcC )
     || isom_write_btrt( bs, visual->btrt )
     || isom_write_esds( bs, visual->esds )
     || isom_write_glbl( bs, visual->glbl )
     || isom_write_colr( bs, visual->colr )
     || isom_write_gama( bs, visual->gama )
     || isom_write_fiel( bs, visual->fiel )
     || isom_write_cspc( bs, visual->cspc )
     || isom_write_sgbt( bs, visual->sgbt )
     || isom_write_stsl( bs, visual->stsl )
     || isom_write_clap( bs, visual->clap )
     || isom_write_pasp( bs, visual->pasp ) )
        return -1;
    return 0;
}

static int isom_write_frma( lsmash_bs_t *bs, isom_frma_t *frma )
{
    if( !frma )
        return -1;
    isom_bs_put_box_common( bs, frma );
    lsmash_bs_put_be32( bs, frma->data_format );
    return lsmash_bs_write_data( bs );
}

static int isom_write_enda( lsmash_bs_t *bs, isom_enda_t *enda )
{
    if( !enda )
        return 0;
    isom_bs_put_box_common( bs, enda );
    lsmash_bs_put_be16( bs, enda->littleEndian );
    return lsmash_bs_write_data( bs );
}

static int isom_write_mp4a( lsmash_bs_t *bs, isom_mp4a_t *mp4a )
{
    if( !mp4a )
        return 0;
    isom_bs_put_box_common( bs, mp4a );
    lsmash_bs_put_be32( bs, mp4a->unknown );
    return lsmash_bs_write_data( bs );
}

static int isom_write_terminator( lsmash_bs_t *bs, isom_terminator_t *terminator )
{
    if( !terminator )
        return -1;
    isom_bs_put_box_common( bs, terminator );
    return lsmash_bs_write_data( bs );
}

static int isom_write_wave( lsmash_bs_t *bs, isom_wave_t *wave )
{
    if( !wave )
        return 0;
    isom_bs_put_box_common( bs, wave );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_frma( bs, wave->frma )
     || isom_write_enda( bs, wave->enda )
     || isom_write_mp4a( bs, wave->mp4a ) )
        return -1;
    lsmash_bs_put_bytes( bs, wave->exdata_length, wave->exdata );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_esds( bs, wave->esds ) )
        return -1;
    return isom_write_terminator( bs, wave->terminator );
}

static int isom_write_chan( lsmash_bs_t *bs, isom_chan_t *chan )
{
    if( !chan )
        return 0;
    isom_bs_put_box_common( bs, chan );
    lsmash_bs_put_be32( bs, chan->channelLayoutTag );
    lsmash_bs_put_be32( bs, chan->channelBitmap );
    lsmash_bs_put_be32( bs, chan->numberChannelDescriptions );
    if( chan->channelDescriptions )
        for( uint32_t i = 0; i < chan->numberChannelDescriptions; i++ )
        {
            isom_channel_description_t *channelDescriptions = (isom_channel_description_t *)(&chan->channelDescriptions[i]);
            if( !channelDescriptions )
                return -1;
            lsmash_bs_put_be32( bs, channelDescriptions->channelLabel );
            lsmash_bs_put_be32( bs, channelDescriptions->channelFlags );
            lsmash_bs_put_be32( bs, channelDescriptions->coordinates[0] );
            lsmash_bs_put_be32( bs, channelDescriptions->coordinates[1] );
            lsmash_bs_put_be32( bs, channelDescriptions->coordinates[2] );
        }
    return lsmash_bs_write_data( bs );
}

static int isom_write_audio_extensions( lsmash_bs_t *bs, isom_audio_entry_t *audio )
{
    if( !audio )
        return 0;
    if( isom_write_esds( bs, audio->esds )
     || isom_write_wave( bs, audio->wave )
     || isom_write_chan( bs, audio->chan ) )
        return -1;
    return 0;
}

static int isom_write_visual_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_visual_entry_t *data = (isom_visual_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_box_common( bs, data );
    lsmash_bs_put_bytes( bs, 6, data->reserved );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    lsmash_bs_put_be16( bs, data->version );
    lsmash_bs_put_be16( bs, data->revision_level );
    lsmash_bs_put_be32( bs, data->vendor );
    lsmash_bs_put_be32( bs, data->temporalQuality );
    lsmash_bs_put_be32( bs, data->spatialQuality );
    lsmash_bs_put_be16( bs, data->width );
    lsmash_bs_put_be16( bs, data->height );
    lsmash_bs_put_be32( bs, data->horizresolution );
    lsmash_bs_put_be32( bs, data->vertresolution );
    lsmash_bs_put_be32( bs, data->dataSize );
    lsmash_bs_put_be16( bs, data->frame_count );
    lsmash_bs_put_bytes( bs, 32, data->compressorname );
    lsmash_bs_put_be16( bs, data->depth );
    lsmash_bs_put_be16( bs, data->color_table_ID );
    lsmash_bs_put_bytes( bs, data->exdata_length, data->exdata );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    return isom_write_visual_extensions( bs, data );
}

static int isom_write_audio_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_audio_entry_t *data = (isom_audio_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_box_common( bs, data );
    lsmash_bs_put_bytes( bs, 6, data->reserved );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    lsmash_bs_put_be16( bs, data->version );
    lsmash_bs_put_be16( bs, data->revision_level );
    lsmash_bs_put_be32( bs, data->vendor );
    lsmash_bs_put_be16( bs, data->channelcount );
    lsmash_bs_put_be16( bs, data->samplesize );
    lsmash_bs_put_be16( bs, data->compression_ID );
    lsmash_bs_put_be16( bs, data->packet_size );
    lsmash_bs_put_be32( bs, data->samplerate );
    if( data->version == 1 )
    {
        lsmash_bs_put_be32( bs, data->samplesPerPacket );
        lsmash_bs_put_be32( bs, data->bytesPerPacket );
        lsmash_bs_put_be32( bs, data->bytesPerFrame );
        lsmash_bs_put_be32( bs, data->bytesPerSample );
    }
    else if( data->version == 2 )
    {
        lsmash_bs_put_be32( bs, data->sizeOfStructOnly );
        lsmash_bs_put_be64( bs, data->audioSampleRate );
        lsmash_bs_put_be32( bs, data->numAudioChannels );
        lsmash_bs_put_be32( bs, data->always7F000000 );
        lsmash_bs_put_be32( bs, data->constBitsPerChannel );
        lsmash_bs_put_be32( bs, data->formatSpecificFlags );
        lsmash_bs_put_be32( bs, data->constBytesPerAudioPacket );
        lsmash_bs_put_be32( bs, data->constLPCMFramesPerAudioPacket );
    }
    lsmash_bs_put_bytes( bs, data->exdata_length, data->exdata );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    return isom_write_audio_extensions( bs, data );
}

#if 0
static int isom_write_hint_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_hint_entry_t *data = (isom_hint_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_box_common( bs, data );
    lsmash_bs_put_bytes( bs, 6, data->reserved );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    if( data->data && data->data_length )
        lsmash_bs_put_bytes( bs, data->data_length, data->data );
    return lsmash_bs_write_data( bs );
}

static int ( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_metadata_entry_t *data = (isom_metadata_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_box_common( bs, data );
    lsmash_bs_put_bytes( bs, 6, data->reserved );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    return lsmash_bs_write_data( bs );
}
#endif

static int isom_write_text_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_text_entry_t *data = (isom_text_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_box_common( bs, data );
    lsmash_bs_put_bytes( bs, 6, data->reserved );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    lsmash_bs_put_be32( bs, data->displayFlags );
    lsmash_bs_put_be32( bs, data->textJustification );
    for( uint32_t i = 0; i < 3; i++ )
        lsmash_bs_put_be16( bs, data->bgColor[i] );
    lsmash_bs_put_be16( bs, data->top );
    lsmash_bs_put_be16( bs, data->left );
    lsmash_bs_put_be16( bs, data->bottom );
    lsmash_bs_put_be16( bs, data->right );
    lsmash_bs_put_be32( bs, data->scrpStartChar );
    lsmash_bs_put_be16( bs, data->scrpHeight );
    lsmash_bs_put_be16( bs, data->scrpAscent );
    lsmash_bs_put_be16( bs, data->scrpFont );
    lsmash_bs_put_be16( bs, data->scrpFace );
    lsmash_bs_put_be16( bs, data->scrpSize );
    for( uint32_t i = 0; i < 3; i++ )
        lsmash_bs_put_be16( bs, data->scrpColor[i] );
    lsmash_bs_put_byte( bs, data->font_name_length );
    if( data->font_name && data->font_name_length )
        lsmash_bs_put_bytes( bs, data->font_name_length, data->font_name );
    return lsmash_bs_write_data( bs );
}

static int isom_put_ftab( lsmash_bs_t *bs, isom_ftab_t *ftab )
{
    if( !ftab || !ftab->list )
        return -1;
    isom_bs_put_box_common( bs, ftab );
    lsmash_bs_put_be16( bs, ftab->list->entry_count );
    for( lsmash_entry_t *entry = ftab->list->head; entry; entry = entry->next )
    {
        isom_font_record_t *data = (isom_font_record_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be16( bs, data->font_ID );
        lsmash_bs_put_byte( bs, data->font_name_length );
        if( data->font_name && data->font_name_length )
            lsmash_bs_put_bytes( bs, data->font_name_length, data->font_name );
    }
    return 0;
}

static int isom_write_tx3g_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_tx3g_entry_t *data = (isom_tx3g_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_box_common( bs, data );
    lsmash_bs_put_bytes( bs, 6, data->reserved );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    lsmash_bs_put_be32( bs, data->displayFlags );
    lsmash_bs_put_byte( bs, data->horizontal_justification );
    lsmash_bs_put_byte( bs, data->vertical_justification );
    for( uint32_t i = 0; i < 4; i++ )
        lsmash_bs_put_byte( bs, data->background_color_rgba[i] );
    lsmash_bs_put_be16( bs, data->top );
    lsmash_bs_put_be16( bs, data->left );
    lsmash_bs_put_be16( bs, data->bottom );
    lsmash_bs_put_be16( bs, data->right );
    lsmash_bs_put_be16( bs, data->startChar );
    lsmash_bs_put_be16( bs, data->endChar );
    lsmash_bs_put_be16( bs, data->font_ID );
    lsmash_bs_put_byte( bs, data->face_style_flags );
    lsmash_bs_put_byte( bs, data->font_size );
    for( uint32_t i = 0; i < 4; i++ )
        lsmash_bs_put_byte( bs, data->text_color_rgba[i] );
    isom_put_ftab( bs, data->ftab );
    return lsmash_bs_write_data( bs );
}

static int isom_write_stsd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stsd_t *stsd = trak->mdia->minf->stbl->stsd;
    if( !stsd || !stsd->list || !stsd->list->head )
        return -1;
    isom_bs_put_box_common( bs, stsd );
    lsmash_bs_put_be32( bs, stsd->list->entry_count );
    int ret = -1;
    for( lsmash_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_sample_entry_t *sample = (isom_sample_entry_t *)entry->data;
        if( !sample )
            return -1;
        switch( sample->type )
        {
            case ISOM_CODEC_TYPE_AVC1_VIDEO :
            case ISOM_CODEC_TYPE_VC_1_VIDEO :
            case QT_CODEC_TYPE_APCH_VIDEO :
            case QT_CODEC_TYPE_APCN_VIDEO :
            case QT_CODEC_TYPE_APCS_VIDEO :
            case QT_CODEC_TYPE_APCO_VIDEO :
            case QT_CODEC_TYPE_AP4H_VIDEO :
            case QT_CODEC_TYPE_DVC_VIDEO :
            case QT_CODEC_TYPE_DVCP_VIDEO :
            case QT_CODEC_TYPE_DVPP_VIDEO :
            case QT_CODEC_TYPE_DV5N_VIDEO :
            case QT_CODEC_TYPE_DV5P_VIDEO :
            case QT_CODEC_TYPE_DVH2_VIDEO :
            case QT_CODEC_TYPE_DVH3_VIDEO :
            case QT_CODEC_TYPE_DVH5_VIDEO :
            case QT_CODEC_TYPE_DVH6_VIDEO :
            case QT_CODEC_TYPE_DVHP_VIDEO :
            case QT_CODEC_TYPE_DVHQ_VIDEO :
            case QT_CODEC_TYPE_ULRA_VIDEO :
            case QT_CODEC_TYPE_ULRG_VIDEO :
            case QT_CODEC_TYPE_ULY2_VIDEO :
            case QT_CODEC_TYPE_ULY0_VIDEO :
            case QT_CODEC_TYPE_V210_VIDEO :
            case QT_CODEC_TYPE_V216_VIDEO :
            case QT_CODEC_TYPE_V308_VIDEO :
            case QT_CODEC_TYPE_V408_VIDEO :
            case QT_CODEC_TYPE_V410_VIDEO :
            case QT_CODEC_TYPE_YUV2_VIDEO :
#ifdef LSMASH_DEMUXER_ENABLED
            case ISOM_CODEC_TYPE_MP4V_VIDEO :
#endif
#if 0
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
            case ISOM_CODEC_TYPE_SVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC2_VIDEO :
            case ISOM_CODEC_TYPE_DRAC_VIDEO :
            case ISOM_CODEC_TYPE_ENCV_VIDEO :
            case ISOM_CODEC_TYPE_MJP2_VIDEO :
            case ISOM_CODEC_TYPE_S263_VIDEO :
#endif
                ret = isom_write_visual_entry( bs, entry );
                break;
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_DTSC_AUDIO :
            case ISOM_CODEC_TYPE_DTSE_AUDIO :
            case ISOM_CODEC_TYPE_DTSH_AUDIO :
            case ISOM_CODEC_TYPE_DTSL_AUDIO :
            case ISOM_CODEC_TYPE_EC_3_AUDIO :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
            case QT_CODEC_TYPE_23NI_AUDIO :
            case QT_CODEC_TYPE_NONE_AUDIO :
            case QT_CODEC_TYPE_LPCM_AUDIO :
            case QT_CODEC_TYPE_SOWT_AUDIO :
            case QT_CODEC_TYPE_TWOS_AUDIO :
            case QT_CODEC_TYPE_FL32_AUDIO :
            case QT_CODEC_TYPE_FL64_AUDIO :
            case QT_CODEC_TYPE_IN24_AUDIO :
            case QT_CODEC_TYPE_IN32_AUDIO :
            case QT_CODEC_TYPE_NOT_SPECIFIED :
#if 0
            case ISOM_CODEC_TYPE_DRA1_AUDIO :
            case ISOM_CODEC_TYPE_ENCA_AUDIO :
            case ISOM_CODEC_TYPE_G719_AUDIO :
            case ISOM_CODEC_TYPE_G726_AUDIO :
            case ISOM_CODEC_TYPE_M4AE_AUDIO :
            case ISOM_CODEC_TYPE_MLPA_AUDIO :
            case ISOM_CODEC_TYPE_RAW_AUDIO :
            case ISOM_CODEC_TYPE_SAWP_AUDIO :
            case ISOM_CODEC_TYPE_SEVC_AUDIO :
            case ISOM_CODEC_TYPE_SQCP_AUDIO :
            case ISOM_CODEC_TYPE_SSMV_AUDIO :
            case ISOM_CODEC_TYPE_TWOS_AUDIO :
#endif
                ret = isom_write_audio_entry( bs, entry );
                break;
#if 0
            case ISOM_CODEC_TYPE_FDP_HINT :
            case ISOM_CODEC_TYPE_M2TS_HINT :
            case ISOM_CODEC_TYPE_PM2T_HINT :
            case ISOM_CODEC_TYPE_PRTP_HINT :
            case ISOM_CODEC_TYPE_RM2T_HINT :
            case ISOM_CODEC_TYPE_RRTP_HINT :
            case ISOM_CODEC_TYPE_RSRP_HINT :
            case ISOM_CODEC_TYPE_RTP_HINT  :
            case ISOM_CODEC_TYPE_SM2T_HINT :
            case ISOM_CODEC_TYPE_SRTP_HINT :
                ret = isom_write_hint_entry( bs, entry );
                break;
            case ISOM_CODEC_TYPE_IXSE_META :
            case ISOM_CODEC_TYPE_METT_META :
            case ISOM_CODEC_TYPE_METX_META :
            case ISOM_CODEC_TYPE_MLIX_META :
            case ISOM_CODEC_TYPE_OKSD_META :
            case ISOM_CODEC_TYPE_SVCM_META :
            case ISOM_CODEC_TYPE_TEXT_META :
            case ISOM_CODEC_TYPE_URIM_META :
            case ISOM_CODEC_TYPE_XML_META  :
                ret = isom_write_metadata_entry( bs, entry );
                break;
#endif
            case ISOM_CODEC_TYPE_TX3G_TEXT :
                ret = isom_write_tx3g_entry( bs, entry );
                break;
            case QT_CODEC_TYPE_TEXT_TEXT :
                ret = isom_write_text_entry( bs, entry );
                break;
            case LSMASH_CODEC_TYPE_RAW :
                if( sample->manager & LSMASH_VIDEO_DESCRIPTION )
                    ret = isom_write_visual_entry( bs, entry );
                else if( sample->manager & LSMASH_AUDIO_DESCRIPTION )
                    ret = isom_write_audio_entry( bs, entry );
                break;
            default :
                break;
        }
        if( ret )
            break;
    }
    return ret;
}

static int isom_write_stts( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stts_t *stts = trak->mdia->minf->stbl->stts;
    if( !stts || !stts->list )
        return -1;
    isom_bs_put_box_common( bs, stts );
    lsmash_bs_put_be32( bs, stts->list->entry_count );
    for( lsmash_entry_t *entry = stts->list->head; entry; entry = entry->next )
    {
        isom_stts_entry_t *data = (isom_stts_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be32( bs, data->sample_count );
        lsmash_bs_put_be32( bs, data->sample_delta );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_ctts( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_ctts_t *ctts = trak->mdia->minf->stbl->ctts;
    if( !ctts )
        return 0;
    if( !ctts->list )
        return -1;
    isom_bs_put_box_common( bs, ctts );
    lsmash_bs_put_be32( bs, ctts->list->entry_count );
    for( lsmash_entry_t *entry = ctts->list->head; entry; entry = entry->next )
    {
        isom_ctts_entry_t *data = (isom_ctts_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be32( bs, data->sample_count );
        lsmash_bs_put_be32( bs, data->sample_offset );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_cslg( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_cslg_t *cslg = trak->mdia->minf->stbl->cslg;
    if( !cslg )
        return 0;
    isom_bs_put_box_common( bs, cslg );
    lsmash_bs_put_be32( bs, cslg->compositionToDTSShift );
    lsmash_bs_put_be32( bs, cslg->leastDecodeToDisplayDelta );
    lsmash_bs_put_be32( bs, cslg->greatestDecodeToDisplayDelta );
    lsmash_bs_put_be32( bs, cslg->compositionStartTime );
    lsmash_bs_put_be32( bs, cslg->compositionEndTime );
    return lsmash_bs_write_data( bs );
}

static int isom_write_stsz( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stsz_t *stsz = trak->mdia->minf->stbl->stsz;
    if( !stsz )
        return -1;
    isom_bs_put_box_common( bs, stsz );
    lsmash_bs_put_be32( bs, stsz->sample_size );
    lsmash_bs_put_be32( bs, stsz->sample_count );
    if( stsz->sample_size == 0 && stsz->list )
        for( lsmash_entry_t *entry = stsz->list->head; entry; entry = entry->next )
        {
            isom_stsz_entry_t *data = (isom_stsz_entry_t *)entry->data;
            if( !data )
                return -1;
            lsmash_bs_put_be32( bs, data->entry_size );
        }
    return lsmash_bs_write_data( bs );
}

static int isom_write_stss( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stss_t *stss = trak->mdia->minf->stbl->stss;
    if( !stss )
        return 0;   /* If the sync sample box is not present, every sample is a random access point. */
    if( !stss->list )
        return -1;
    isom_bs_put_box_common( bs, stss );
    lsmash_bs_put_be32( bs, stss->list->entry_count );
    for( lsmash_entry_t *entry = stss->list->head; entry; entry = entry->next )
    {
        isom_stss_entry_t *data = (isom_stss_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be32( bs, data->sample_number );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_stps( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stps_t *stps = trak->mdia->minf->stbl->stps;
    if( !stps )
        return 0;
    if( !stps->list )
        return -1;
    isom_bs_put_box_common( bs, stps );
    lsmash_bs_put_be32( bs, stps->list->entry_count );
    for( lsmash_entry_t *entry = stps->list->head; entry; entry = entry->next )
    {
        isom_stps_entry_t *data = (isom_stps_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be32( bs, data->sample_number );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_sdtp( lsmash_bs_t *bs, isom_sdtp_t *sdtp )
{
    if( !sdtp )
        return 0;
    if( !sdtp->list )
        return -1;
    isom_bs_put_box_common( bs, sdtp );
    for( lsmash_entry_t *entry = sdtp->list->head; entry; entry = entry->next )
    {
        isom_sdtp_entry_t *data = (isom_sdtp_entry_t *)entry->data;
        if( !data )
            return -1;
        uint8_t temp = (data->is_leading            << 6)
                     | (data->sample_depends_on     << 4)
                     | (data->sample_is_depended_on << 2)
                     |  data->sample_has_redundancy;
        lsmash_bs_put_byte( bs, temp );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_stsc( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stsc_t *stsc = trak->mdia->minf->stbl->stsc;
    if( !stsc || !stsc->list )
        return -1;
    isom_bs_put_box_common( bs, stsc );
    lsmash_bs_put_be32( bs, stsc->list->entry_count );
    for( lsmash_entry_t *entry = stsc->list->head; entry; entry = entry->next )
    {
        isom_stsc_entry_t *data = (isom_stsc_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be32( bs, data->first_chunk );
        lsmash_bs_put_be32( bs, data->samples_per_chunk );
        lsmash_bs_put_be32( bs, data->sample_description_index );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_co64( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stco_t *co64 = trak->mdia->minf->stbl->stco;
    if( !co64 || !co64->list )
        return -1;
    isom_bs_put_box_common( bs, co64 );
    lsmash_bs_put_be32( bs, co64->list->entry_count );
    for( lsmash_entry_t *entry = co64->list->head; entry; entry = entry->next )
    {
        isom_co64_entry_t *data = (isom_co64_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be64( bs, data->chunk_offset );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_stco( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stco_t *stco = trak->mdia->minf->stbl->stco;
    if( !stco || !stco->list )
        return -1;
    if( stco->large_presentation )
        return isom_write_co64( bs, trak );
    isom_bs_put_box_common( bs, stco );
    lsmash_bs_put_be32( bs, stco->list->entry_count );
    for( lsmash_entry_t *entry = stco->list->head; entry; entry = entry->next )
    {
        isom_stco_entry_t *data = (isom_stco_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be32( bs, data->chunk_offset );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_sgpd( lsmash_bs_t *bs, isom_trak_entry_t *trak, uint32_t grouping_number )
{
    isom_sgpd_entry_t *sgpd = (isom_sgpd_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->sgpd_list, grouping_number );
    if( !sgpd || !sgpd->list )
        return -1;
    isom_bs_put_box_common( bs, sgpd );
    lsmash_bs_put_be32( bs, sgpd->grouping_type );
    if( sgpd->version == 1 )
        lsmash_bs_put_be32( bs, sgpd->default_length );
    lsmash_bs_put_be32( bs, sgpd->list->entry_count );
    for( lsmash_entry_t *entry = sgpd->list->head; entry; entry = entry->next )
    {
        if( !entry->data )
            return -1;
        switch( sgpd->grouping_type )
        {
            case ISOM_GROUP_TYPE_RAP :
            {
                isom_rap_entry_t *rap = (isom_rap_entry_t *)entry->data;
                uint8_t temp = (rap->num_leading_samples_known << 7)
                             |  rap->num_leading_samples;
                lsmash_bs_put_byte( bs, temp );
                break;
            }
            case ISOM_GROUP_TYPE_ROLL :
                lsmash_bs_put_be16( bs, ((isom_roll_entry_t *)entry->data)->roll_distance );
                break;
            default :
                /* We don't consider other grouping types currently. */
                // if( sgpd->version == 1 && !sgpd->default_length )
                //     lsmash_bs_put_be32( bs, ((isom_sgpd_entry_t *)entry->data)->description_length );
                break;
        }
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_sbgp( lsmash_bs_t *bs, isom_trak_entry_t *trak, uint32_t grouping_number )
{
    isom_sbgp_entry_t *sbgp = (isom_sbgp_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->sbgp_list, grouping_number );
    if( !sbgp || !sbgp->list )
        return -1;
    isom_bs_put_box_common( bs, sbgp );
    lsmash_bs_put_be32( bs, sbgp->grouping_type );
    if( sbgp->version == 1 )
        lsmash_bs_put_be32( bs, sbgp->grouping_type_parameter );
    lsmash_bs_put_be32( bs, sbgp->list->entry_count );
    for( lsmash_entry_t *entry = sbgp->list->head; entry; entry = entry->next )
    {
        isom_group_assignment_entry_t *data = (isom_group_assignment_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be32( bs, data->sample_count );
        lsmash_bs_put_be32( bs, data->group_description_index );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_stbl( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    if( !stbl )
        return -1;
    isom_bs_put_box_common( bs, stbl );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_stsd( bs, trak )
     || isom_write_stts( bs, trak )
     || isom_write_ctts( bs, trak )
     || isom_write_cslg( bs, trak )
     || isom_write_stss( bs, trak )
     || isom_write_stps( bs, trak )
     || isom_write_sdtp( bs, trak->mdia->minf->stbl->sdtp )
     || isom_write_stsc( bs, trak )
     || isom_write_stsz( bs, trak )
     || isom_write_stco( bs, trak ) )
        return -1;
    if( stbl->sgpd_list )
        for( uint32_t i = 1; i <= stbl->sgpd_list->entry_count; i++ )
            if( isom_write_sgpd( bs, trak, i ) )
                return -1;
    if( stbl->sbgp_list )
        for( uint32_t i = 1; i <= stbl->sbgp_list->entry_count; i++ )
            if( isom_write_sbgp( bs, trak, i ) )
                return -1;
    return 0;
}

static int isom_write_minf( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_minf_t *minf = trak->mdia->minf;
    if( !minf )
        return -1;
    isom_bs_put_box_common( bs, minf );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( (minf->vmhd && isom_write_vmhd( bs, trak ))
     || (minf->smhd && isom_write_smhd( bs, trak ))
     || (minf->hmhd && isom_write_hmhd( bs, trak ))
     || (minf->nmhd && isom_write_nmhd( bs, trak ))
     || (minf->gmhd && isom_write_gmhd( bs, trak )) )
        return -1;
    if( isom_write_hdlr( bs, minf->hdlr, minf->type )
     || isom_write_dinf( bs, minf->dinf, minf->type )
     || isom_write_stbl( bs, trak ) )
        return -1;
    return 0;
}

static int isom_write_mdia( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_mdia_t *mdia = trak->mdia;
    if( !mdia )
        return -1;
    isom_bs_put_box_common( bs, mdia );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_mdhd( bs, trak )
     || isom_write_hdlr( bs, mdia->hdlr, mdia->type )
     || isom_write_minf( bs, trak ) )
        return -1;
    return 0;
}

static int isom_write_chpl( lsmash_bs_t *bs, isom_chpl_t *chpl )
{
    if( !chpl )
        return 0;
    if( !chpl->list || chpl->version > 1 )
        return -1;
    isom_bs_put_box_common( bs, chpl );
    if( chpl->version == 1 )
    {
        lsmash_bs_put_byte( bs, chpl->unknown );
        lsmash_bs_put_be32( bs, chpl->list->entry_count );
    }
    else    /* chpl->version == 0 */
        lsmash_bs_put_byte( bs, (uint8_t)chpl->list->entry_count );
    for( lsmash_entry_t *entry = chpl->list->head; entry; entry = entry->next )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be64( bs, data->start_time );
        lsmash_bs_put_byte( bs, data->chapter_name_length );
        lsmash_bs_put_bytes( bs, data->chapter_name_length, data->chapter_name );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_mean( lsmash_bs_t *bs, isom_mean_t *mean )
{
    if( !mean )
        return 0;
    isom_bs_put_box_common( bs, mean );
    if( mean->meaning_string && mean->meaning_string_length )
        lsmash_bs_put_bytes( bs, mean->meaning_string_length, mean->meaning_string );
    return lsmash_bs_write_data( bs );
}

static int isom_write_name( lsmash_bs_t *bs, isom_name_t *name )
{
    if( !name )
        return 0;
    isom_bs_put_box_common( bs, name );
    if( name->name && name->name_length )
        lsmash_bs_put_bytes( bs, name->name_length, name->name );
    return lsmash_bs_write_data( bs );
}

static int isom_write_data( lsmash_bs_t *bs, isom_data_t *data )
{
    if( !data || data->size < 16 )
        return -1;
    isom_bs_put_box_common( bs, data );
    lsmash_bs_put_be16( bs, data->reserved );
    lsmash_bs_put_byte( bs, data->type_set_identifier );
    lsmash_bs_put_byte( bs, data->type_code );
    lsmash_bs_put_be32( bs, data->the_locale );
    if( data->value && data->value_length )
        lsmash_bs_put_bytes( bs, data->value_length, data->value );
    return lsmash_bs_write_data( bs );
}

static int isom_write_metaitem( lsmash_bs_t *bs, isom_metaitem_t *metaitem )
{
    if( !metaitem )
        return -1;
    isom_bs_put_box_common( bs, metaitem );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_mean( bs, metaitem->mean )
     || isom_write_name( bs, metaitem->name )
     || isom_write_data( bs, metaitem->data ) )
        return -1;
    return 0;
}

static int isom_write_ilst( lsmash_bs_t *bs, isom_ilst_t *ilst )
{
    if( !ilst )
        return 0;
    isom_bs_put_box_common( bs, ilst );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( ilst->item_list )
        for( lsmash_entry_t *entry = ilst->item_list->head; entry; entry = entry->next )
            if( isom_write_metaitem( bs, (isom_metaitem_t *)entry->data ) )
                return -1;
    return 0;
}

int isom_write_meta( lsmash_bs_t *bs, isom_meta_t *meta )
{
    if( !meta )
        return 0;
    isom_bs_put_box_common( bs, meta );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_hdlr( bs, meta->hdlr, meta->type )
     || isom_write_dinf( bs, meta->dinf, meta->type )
     || isom_write_ilst( bs, meta->ilst ) )
        return -1;
    return 0;
}

static int isom_write_cprt( lsmash_bs_t *bs, isom_cprt_t *cprt )
{
    if( !cprt )
        return -1;
    isom_bs_put_box_common( bs, cprt );
    lsmash_bs_put_be16( bs, cprt->language );
    lsmash_bs_put_bytes( bs, cprt->notice_length, cprt->notice );
    return lsmash_bs_write_data( bs );
}

int isom_write_udta( lsmash_bs_t *bs, isom_moov_t *moov, isom_trak_entry_t *trak )
{
    /* Setting non-NULL pointer to trak means trak->udta data will be written in stream.
     * If trak is set by NULL while moov is set by non-NULL pointer, moov->udta data will be written in stream. */
    isom_udta_t *udta = trak ? trak->udta : moov ? moov->udta : NULL;
    if( !udta )
        return 0;
    isom_bs_put_box_common( bs, udta );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( moov && isom_write_chpl( bs, udta->chpl ) )
        return -1;
    if( isom_write_meta( bs, udta->meta ) )
        return -1;
    if( udta->cprt_list )
        for( lsmash_entry_t *entry = udta->cprt_list->head; entry; entry = entry->next )
            if( isom_write_cprt( bs, (isom_cprt_t *)entry->data ) )
                return -1;
    return 0;
}

int isom_write_trak( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    if( !trak )
        return -1;
    isom_bs_put_box_common( bs, trak );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_tkhd( bs, trak )
     || isom_write_tapt( bs, trak )
     || isom_write_edts( bs, trak )
     || isom_write_tref( bs, trak )
     || isom_write_mdia( bs, trak )
     || isom_write_udta( bs, NULL, trak )
     || isom_write_meta( bs, trak->meta ) )
        return -1;
    return 0;
}

int isom_write_iods( lsmash_root_t *root )
{
    if( !root || !root->moov )
        return -1;
    if( !root->moov->iods )
        return 0;
    isom_iods_t *iods = root->moov->iods;
    lsmash_bs_t *bs = root->bs;
    isom_bs_put_box_common( bs, iods );
    return mp4sys_write_ObjectDescriptor( bs, iods->OD );
}

int isom_write_mvhd( lsmash_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return -1;
    isom_mvhd_t *mvhd = root->moov->mvhd;
    lsmash_bs_t *bs = root->bs;
    isom_bs_put_box_common( bs, mvhd );
    if( mvhd->version )
    {
        lsmash_bs_put_be64( bs, mvhd->creation_time );
        lsmash_bs_put_be64( bs, mvhd->modification_time );
        lsmash_bs_put_be32( bs, mvhd->timescale );
        lsmash_bs_put_be64( bs, mvhd->duration );
    }
    else
    {
        lsmash_bs_put_be32( bs, (uint32_t)mvhd->creation_time );
        lsmash_bs_put_be32( bs, (uint32_t)mvhd->modification_time );
        lsmash_bs_put_be32( bs, mvhd->timescale );
        lsmash_bs_put_be32( bs, (uint32_t)mvhd->duration );
    }
    lsmash_bs_put_be32( bs, mvhd->rate );
    lsmash_bs_put_be16( bs, mvhd->volume );
    lsmash_bs_put_be16( bs, mvhd->reserved );
    lsmash_bs_put_be32( bs, mvhd->preferredLong[0] );
    lsmash_bs_put_be32( bs, mvhd->preferredLong[1] );
    for( int i = 0; i < 9; i++ )
        lsmash_bs_put_be32( bs, mvhd->matrix[i] );
    lsmash_bs_put_be32( bs, mvhd->previewTime );
    lsmash_bs_put_be32( bs, mvhd->previewDuration );
    lsmash_bs_put_be32( bs, mvhd->posterTime );
    lsmash_bs_put_be32( bs, mvhd->selectionTime );
    lsmash_bs_put_be32( bs, mvhd->selectionDuration );
    lsmash_bs_put_be32( bs, mvhd->currentTime );
    lsmash_bs_put_be32( bs, mvhd->next_track_ID );
    return lsmash_bs_write_data( bs );
}

static void isom_bs_put_sample_flags( lsmash_bs_t *bs, isom_sample_flags_t *flags )
{
    uint32_t temp = (flags->reserved                  << 28)
                  | (flags->is_leading                << 26)
                  | (flags->sample_depends_on         << 24)
                  | (flags->sample_is_depended_on     << 22)
                  | (flags->sample_has_redundancy     << 20)
                  | (flags->sample_padding_value      << 17)
                  | (flags->sample_is_non_sync_sample << 16)
                  |  flags->sample_degradation_priority;
    lsmash_bs_put_be32( bs, temp );
}

int isom_write_mehd( lsmash_bs_t *bs, isom_mehd_t *mehd )
{
    if( !mehd )
        return -1;
    isom_bs_put_box_common( bs, mehd );
    if( mehd->version == 1 )
        lsmash_bs_put_be64( bs, mehd->fragment_duration );
    else
        lsmash_bs_put_be32( bs, (uint32_t)mehd->fragment_duration );
    return lsmash_bs_write_data( bs );
}

static int isom_write_trex( lsmash_bs_t *bs, isom_trex_entry_t *trex )
{
    if( !trex )
        return -1;
    isom_bs_put_box_common( bs, trex );
    lsmash_bs_put_be32( bs, trex->track_ID );
    lsmash_bs_put_be32( bs, trex->default_sample_description_index );
    lsmash_bs_put_be32( bs, trex->default_sample_duration );
    lsmash_bs_put_be32( bs, trex->default_sample_size );
    isom_bs_put_sample_flags( bs, &trex->default_sample_flags );
    return lsmash_bs_write_data( bs );
}

static int isom_bs_write_movie_extends_placeholder( lsmash_bs_t *bs )
{
    /* The following will be overwritten by Movie Extends Header Box.
     * We use version 1 Movie Extends Header Box since it causes extra 4 bytes region
     * we cannot replace with empty Free Space Box as we place version 0 one.  */
    lsmash_bs_put_be32( bs, ISOM_FULLBOX_COMMON_SIZE + 8 );
    lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_FREE );
    lsmash_bs_put_be32( bs, 0 );
    lsmash_bs_put_be64( bs, 0 );
    return lsmash_bs_write_data( bs );
}

static int isom_write_mvex( lsmash_bs_t *bs, isom_mvex_t *mvex )
{
    if( !mvex )
        return 0;
    isom_bs_put_box_common( bs, mvex );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    /* Movie Extends Header Box is not written immediately.
     * It's done after finishing all movie fragments. */
    if( mvex->mehd )
    {
        if( isom_write_mehd( bs, mvex->mehd ) )
            return -1;
    }
    else if( bs->stream != stdout )
    {
        /*
            [ROOT]
             |--[ftyp]
             |--[moov]
                 |--[mvhd]
                 |--[trak]
                 *
                 *
                 *
                 |--[mvex]
                     |--[mehd] <--- mehd->pos == mvex->placeholder_pos
        */
        mvex->placeholder_pos = mvex->root->bs->written;
        if( isom_bs_write_movie_extends_placeholder( bs ) )
            return -1;
    }
    if( mvex->trex_list )
        for( lsmash_entry_t *entry = mvex->trex_list->head; entry; entry = entry->next )
            if( isom_write_trex( bs, (isom_trex_entry_t *)entry->data ) )
                return -1;
    return 0;
}

static int isom_write_mfhd( lsmash_bs_t *bs, isom_mfhd_t *mfhd )
{
    if( !mfhd )
        return -1;
    isom_bs_put_box_common( bs, mfhd );
    lsmash_bs_put_be32( bs, mfhd->sequence_number );
    return lsmash_bs_write_data( bs );
}

static int isom_write_tfhd( lsmash_bs_t *bs, isom_tfhd_t *tfhd )
{
    if( !tfhd )
        return -1;
    isom_bs_put_box_common( bs, tfhd );
    lsmash_bs_put_be32( bs, tfhd->track_ID );
    if( tfhd->flags & ISOM_TF_FLAGS_BASE_DATA_OFFSET_PRESENT         ) lsmash_bs_put_be64( bs, tfhd->base_data_offset );
    if( tfhd->flags & ISOM_TF_FLAGS_SAMPLE_DESCRIPTION_INDEX_PRESENT ) lsmash_bs_put_be32( bs, tfhd->sample_description_index );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT  ) lsmash_bs_put_be32( bs, tfhd->default_sample_duration );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_SIZE_PRESENT      ) lsmash_bs_put_be32( bs, tfhd->default_sample_size );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT     ) isom_bs_put_sample_flags( bs, &tfhd->default_sample_flags );
    return lsmash_bs_write_data( bs );
}

static int isom_write_trun( lsmash_bs_t *bs, isom_trun_entry_t *trun )
{
    if( !trun )
        return -1;
    isom_bs_put_box_common( bs, trun );
    lsmash_bs_put_be32( bs, trun->sample_count );
    if( trun->flags & ISOM_TR_FLAGS_DATA_OFFSET_PRESENT        ) lsmash_bs_put_be32( bs, trun->data_offset );
    if( trun->flags & ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT ) isom_bs_put_sample_flags( bs, &trun->first_sample_flags );
    if( trun->optional )
        for( lsmash_entry_t *entry = trun->optional->head; entry; entry = entry->next )
        {
            isom_trun_optional_row_t *data = (isom_trun_optional_row_t *)entry->data;
            if( !data )
                return -1;
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT                ) lsmash_bs_put_be32( bs, data->sample_duration );
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT                    ) lsmash_bs_put_be32( bs, data->sample_size );
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT                   ) isom_bs_put_sample_flags( bs, &data->sample_flags );
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT ) lsmash_bs_put_be32( bs, data->sample_composition_time_offset );
        }
    return lsmash_bs_write_data( bs );
}

static int isom_write_traf( lsmash_bs_t *bs, isom_traf_entry_t *traf )
{
    if( !traf )
        return -1;
    isom_bs_put_box_common( bs, traf );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_tfhd( bs, traf->tfhd ) )
        return -1;
    if( traf->trun_list )
        for( lsmash_entry_t *entry = traf->trun_list->head; entry; entry = entry->next )
            if( isom_write_trun( bs, (isom_trun_entry_t *)entry->data ) )
                return -1;
    if( isom_write_sdtp( bs, traf->sdtp ) )
        return -1;
    return 0;
}

int isom_write_moof( lsmash_bs_t *bs, isom_moof_entry_t *moof )
{
    if( !moof )
        return -1;
    isom_bs_put_box_common( bs, moof );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_mfhd( bs, moof->mfhd ) )
        return -1;
    if( moof->traf_list )
        for( lsmash_entry_t *entry = moof->traf_list->head; entry; entry = entry->next )
            if( isom_write_traf( bs, (isom_traf_entry_t *)entry->data ) )
                return -1;
    return 0;
}

static int isom_write_tfra( lsmash_bs_t *bs, isom_tfra_entry_t *tfra )
{
    if( !tfra )
        return -1;
    isom_bs_put_box_common( bs, tfra );
    uint32_t temp = (tfra->reserved                << 6)
                  | (tfra->length_size_of_traf_num << 4)
                  | (tfra->length_size_of_trun_num << 2)
                  |  tfra->length_size_of_sample_num;
    lsmash_bs_put_be32( bs, tfra->track_ID );
    lsmash_bs_put_be32( bs, temp );
    lsmash_bs_put_be32( bs, tfra->number_of_entry );
    if( tfra->list )
    {
        void (*bs_put_funcs[5])( lsmash_bs_t *, uint64_t ) =
            {
                lsmash_bs_put_byte_from_64,
                lsmash_bs_put_be16_from_64,
                lsmash_bs_put_be24_from_64,
                lsmash_bs_put_be32_from_64,
                lsmash_bs_put_be64
            };
        void (*bs_put_time)         ( lsmash_bs_t *, uint64_t ) = bs_put_funcs[ 3 + (tfra->version == 1)        ];
        void (*bs_put_moof_offset)  ( lsmash_bs_t *, uint64_t ) = bs_put_funcs[ 3 + (tfra->version == 1)        ];
        void (*bs_put_traf_number)  ( lsmash_bs_t *, uint64_t ) = bs_put_funcs[ tfra->length_size_of_traf_num   ];
        void (*bs_put_trun_number)  ( lsmash_bs_t *, uint64_t ) = bs_put_funcs[ tfra->length_size_of_trun_num   ];
        void (*bs_put_sample_number)( lsmash_bs_t *, uint64_t ) = bs_put_funcs[ tfra->length_size_of_sample_num ];
        for( lsmash_entry_t *entry = tfra->list->head; entry; entry = entry->next )
        {
            isom_tfra_location_time_entry_t *data = (isom_tfra_location_time_entry_t *)entry->data;
            if( !data )
                return -1;
            bs_put_time         ( bs, data->time          );
            bs_put_moof_offset  ( bs, data->moof_offset   );
            bs_put_traf_number  ( bs, data->traf_number   );
            bs_put_trun_number  ( bs, data->trun_number   );
            bs_put_sample_number( bs, data->sample_number );
        }
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_mfro( lsmash_bs_t *bs, isom_mfro_t *mfro )
{
    if( !mfro )
        return -1;
    isom_bs_put_box_common( bs, mfro );
    lsmash_bs_put_be32( bs, mfro->length );
    return lsmash_bs_write_data( bs );
}

int isom_write_mfra( lsmash_bs_t *bs, isom_mfra_t *mfra )
{
    if( !mfra )
        return -1;
    isom_bs_put_box_common( bs, mfra );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( mfra->tfra_list )
        for( lsmash_entry_t *entry = mfra->tfra_list->head; entry; entry = entry->next )
            if( isom_write_tfra( bs, (isom_tfra_entry_t *)entry->data ) )
                return -1;
    return isom_write_mfro( bs, mfra->mfro );
}

static int isom_bs_write_largesize_placeholder( lsmash_bs_t *bs )
{
    lsmash_bs_put_be32( bs, ISOM_BASEBOX_COMMON_SIZE );
    lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_FREE );
    return lsmash_bs_write_data( bs );
}

int isom_write_mdat_header( lsmash_root_t *root, uint64_t media_size )
{
    if( !root || !root->bs || !root->mdat )
        return -1;
    isom_mdat_t *mdat = root->mdat;
    lsmash_bs_t *bs = root->bs;
    if( media_size )
    {
        mdat->size = ISOM_BASEBOX_COMMON_SIZE + media_size;
        if( mdat->size > UINT32_MAX )
            mdat->size += 8;    /* large_size */
        isom_bs_put_box_common( bs, mdat );
        return 0;
    }
    mdat->placeholder_pos = lsmash_ftell( bs->stream );
    if( isom_bs_write_largesize_placeholder( bs ) )
        return -1;
    mdat->size = ISOM_BASEBOX_COMMON_SIZE;
    isom_bs_put_box_common( bs, mdat );
    return lsmash_bs_write_data( bs );
}

int isom_write_mdat_size( lsmash_root_t *root )
{
    if( !root || !root->bs || !root->bs->stream )
        return -1;
    if( !root->mdat )
        return 0;
    isom_mdat_t *mdat = root->mdat;
    uint8_t large_flag = mdat->size > UINT32_MAX;
    lsmash_bs_t *bs = root->bs;
    FILE *stream = bs->stream;
    uint64_t current_pos = lsmash_ftell( stream );
    if( large_flag )
    {
        lsmash_fseek( stream, mdat->placeholder_pos, SEEK_SET );
        lsmash_bs_put_be32( bs, 1 );
        lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_MDAT );
        lsmash_bs_put_be64( bs, mdat->size + ISOM_BASEBOX_COMMON_SIZE );
    }
    else
    {
        lsmash_fseek( stream, mdat->placeholder_pos + ISOM_BASEBOX_COMMON_SIZE, SEEK_SET );
        lsmash_bs_put_be32( bs, mdat->size );
        lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_MDAT );
    }
    int ret = lsmash_bs_write_data( bs );
    lsmash_fseek( stream, current_pos, SEEK_SET );
    return ret;
}

int isom_write_ftyp( lsmash_root_t *root )
{
    isom_ftyp_t *ftyp = root->ftyp;
    if( !ftyp || !ftyp->brand_count )
        return 0;
    lsmash_bs_t *bs = root->bs;
    isom_bs_put_box_common( bs, ftyp );
    lsmash_bs_put_be32( bs, ftyp->major_brand );
    lsmash_bs_put_be32( bs, ftyp->minor_version );
    for( uint32_t i = 0; i < ftyp->brand_count; i++ )
        lsmash_bs_put_be32( bs, ftyp->compatible_brands[i] );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    root->size += ftyp->size;
    root->file_type_written = 1;
    return 0;
}

int isom_write_moov( lsmash_root_t *root )
{
    if( !root || !root->moov )
        return -1;
    lsmash_bs_t *bs = root->bs;
    isom_moov_t *moov = root->moov;
    isom_bs_put_box_common( bs, moov );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_mvhd( root )
     || isom_write_iods( root ) )
        return -1;
    if( moov->trak_list )
        for( lsmash_entry_t *entry = moov->trak_list->head; entry; entry = entry->next )
            if( isom_write_trak( bs, (isom_trak_entry_t *)entry->data ) )
                return -1;
    if( isom_write_udta( bs, moov, NULL )
     || isom_write_meta( bs, moov->meta ) )
        return -1;
    return isom_write_mvex( bs, moov->mvex );
}

int lsmash_write_free( lsmash_root_t *root )
{
    if( !root || !root->bs || !root->free )
        return -1;
    isom_free_t *skip = root->free;
    lsmash_bs_t *bs = root->bs;
    skip->size = 8 + skip->length;
    isom_bs_put_box_common( bs, skip );
    if( skip->data && skip->length )
        lsmash_bs_put_bytes( bs, skip->length, skip->data );
    return lsmash_bs_write_data( bs );
}
