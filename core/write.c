/*****************************************************************************
 * write.c
 *****************************************************************************
 * Copyright (C) 2010-2017 L-SMASH project
 * Copyright (c) 2026 Yusuke Nakamura
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
#include <inttypes.h>

#include "box.h"
#include "write.h"
#include "sample.h"

#include "codecs/mp4a.h"
#include "codecs/mp4sys.h"
#include "codecs/description.h"

static int write_children( lsmash_bs_t *bs, isom_box_t *box )
{
    for( lsmash_entry_t *entry = box->extensions.head; entry; entry = entry->next )
    {
        isom_box_t *child = (isom_box_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( child ) )
            continue;
        int ret = lsm_write_box( bs, child );
        if( ret < 0 )
            return ret;
    }
    return 0;
}

static int write_binary_coded_box( lsmash_bs_t *bs, isom_box_t *box )
{
    lsmash_bs_put_bytes( bs, box->size, box->binary );
    return 0;
}

static int write_unknown_box( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_unknown_box_t *unknown_box = (isom_unknown_box_t *)box;
    lsm_box_bs_put_common( bs, unknown_box );
    if( unknown_box->unknown_field
     && unknown_box->unknown_size )
        lsmash_bs_put_bytes( bs, unknown_box->unknown_size, unknown_box->unknown_field );
    return 0;
}

static void write_bs_put_qt_color_table( lsmash_bs_t *bs, isom_qt_color_table_t *color_table )
{
    lsmash_bs_put_be32( bs, color_table->seed );
    lsmash_bs_put_be16( bs, color_table->flags );
    lsmash_bs_put_be16( bs, color_table->size );
    isom_qt_color_array_t *array = color_table->array;
    if( array )
        for( uint16_t i = 0; i <= color_table->size; i++ )
        {
            lsmash_bs_put_be16( bs, array[i].value );
            lsmash_bs_put_be16( bs, array[i].r );
            lsmash_bs_put_be16( bs, array[i].g );
            lsmash_bs_put_be16( bs, array[i].b );
        }
}

static int write_ctab( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_ctab_t *ctab = (isom_ctab_t *)box;
    lsm_box_bs_put_common( bs, ctab );
    write_bs_put_qt_color_table( bs, &ctab->color_table );
    return 0;
}

static int write_tkhd( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_tkhd_t *tkhd = (isom_tkhd_t *)box;
    /* Check the version. */
    if( (LSMASH_IS_EXISTING_BOX( tkhd->file ) && !tkhd->file->undefined_64_ver)
     && (tkhd->creation_time     > UINT32_MAX
      || tkhd->modification_time > UINT32_MAX
      || tkhd->duration          > UINT32_MAX) )
        tkhd->version = 1;
    else
        tkhd->version = 0;
    /* Write. */
    lsm_box_bs_put_common( bs, tkhd );
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
        lsmash_bs_put_be32( bs, LSMASH_MIN( tkhd->creation_time,     UINT32_MAX ) );
        lsmash_bs_put_be32( bs, LSMASH_MIN( tkhd->modification_time, UINT32_MAX ) );
        lsmash_bs_put_be32( bs, tkhd->track_ID );
        lsmash_bs_put_be32( bs, tkhd->reserved1 );
        lsmash_bs_put_be32( bs, LSMASH_MIN( tkhd->duration,          UINT32_MAX ) );
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
    return 0;
}

static int write_clef( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_clef_t *clef = (isom_clef_t *)box;
    lsm_box_bs_put_common( bs, clef );
    lsmash_bs_put_be32( bs, clef->width );
    lsmash_bs_put_be32( bs, clef->height );
    return 0;
}

static int write_prof( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_prof_t *prof = (isom_prof_t *)box;
    lsm_box_bs_put_common( bs, prof );
    lsmash_bs_put_be32( bs, prof->width );
    lsmash_bs_put_be32( bs, prof->height );
    return 0;
}

static int write_enof( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_enof_t *enof = (isom_enof_t *)box;
    lsm_box_bs_put_common( bs, enof );
    lsmash_bs_put_be32( bs, enof->width );
    lsmash_bs_put_be32( bs, enof->height );
    return 0;
}

static int write_tapt( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_elst( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_elst_t *elst = (isom_elst_t *)box;
    assert( elst->list );
    if( lsm_list_count( elst->list ) == 0 )
        return 0;
    elst->version = 0;
    lsmash_file_t *file = elst->file;
    isom_elst_entry_t *data;
    if( LSMASH_IS_EXISTING_BOX( file ) )
    {
        /* Check the version. */
        if( !file->undefined_64_ver )
            LSM_LIST_FOR_EACH( data, elst->list )
            {
                if( data->segment_duration > UINT32_MAX
                 || data->media_time       >  INT32_MAX
                 || data->media_time       <  INT32_MIN )
                    elst->version = 1;
            }
        /* Remember to rewrite entries. */
        if( file->fragment && !file->bs->unseekable )
            elst->pos = file->bs->written;
    }
    /* Write. */
    lsm_box_bs_put_common( bs, elst );
    lsmash_bs_put_be32( bs, lsm_list_count( elst->list ) );
    LSM_LIST_FOR_EACH( data, elst->list )
    {
        if( elst->version )
        {
            lsmash_bs_put_be64( bs, data->segment_duration );
            lsmash_bs_put_be64( bs, data->media_time );
        }
        else
        {
            lsmash_bs_put_be32( bs, LSMASH_MIN( data->segment_duration, UINT32_MAX ) );
            lsmash_bs_put_be32( bs, data->media_time < 0 ? (uint32_t)data->media_time : LSMASH_MIN( data->media_time, INT32_MAX ) );
        }
        lsmash_bs_put_be32( bs, data->media_rate );
    }
    return 0;
}

static int write_edts( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_tref( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_track_reference_type( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_tref_type_t *ref = (isom_tref_type_t *)box;
    lsm_box_bs_put_common( bs, ref );
    for( uint32_t i = 0; i < ref->ref_count; i++ )
        lsmash_bs_put_be32( bs, ref->track_ID[i] );
    return 0;
}

static int write_mdhd( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_mdhd_t *mdhd = (isom_mdhd_t *)box;
    /* Check the version. */
    if( (LSMASH_IS_EXISTING_BOX( mdhd->file ) && !mdhd->file->undefined_64_ver)
     && (mdhd->creation_time     > UINT32_MAX
      || mdhd->modification_time > UINT32_MAX
      || mdhd->duration          > UINT32_MAX) )
        mdhd->version = 1;
    else
        mdhd->version = 0;
    /* Write. */
    lsm_box_bs_put_common( bs, mdhd );
    if( mdhd->version )
    {
        lsmash_bs_put_be64( bs, mdhd->creation_time );
        lsmash_bs_put_be64( bs, mdhd->modification_time );
        lsmash_bs_put_be32( bs, mdhd->timescale );
        lsmash_bs_put_be64( bs, mdhd->duration );
    }
    else
    {
        lsmash_bs_put_be32( bs, LSMASH_MIN( mdhd->creation_time,     UINT32_MAX ) );
        lsmash_bs_put_be32( bs, LSMASH_MIN( mdhd->modification_time, UINT32_MAX ) );
        lsmash_bs_put_be32( bs, mdhd->timescale );
        lsmash_bs_put_be32( bs, LSMASH_MIN( mdhd->duration,          UINT32_MAX ) );
    }
    lsmash_bs_put_be16( bs, mdhd->language );
    lsmash_bs_put_be16( bs, mdhd->quality );
    return 0;
}

static int write_hdlr( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_hdlr_t *hdlr = (isom_hdlr_t *)box;
    lsm_box_bs_put_common( bs, hdlr );
    lsmash_bs_put_be32( bs, hdlr->componentType );
    lsmash_bs_put_be32( bs, hdlr->componentSubtype );
    lsmash_bs_put_be32( bs, hdlr->componentManufacturer );
    lsmash_bs_put_be32( bs, hdlr->componentFlags );
    lsmash_bs_put_be32( bs, hdlr->componentFlagsMask );
    lsmash_bs_put_bytes( bs, hdlr->componentName_length, hdlr->componentName );
    return 0;
}

static int write_vmhd( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_vmhd_t *vmhd = (isom_vmhd_t *)box;
    lsm_box_bs_put_common( bs, vmhd );
    lsmash_bs_put_be16( bs, vmhd->graphicsmode );
    for( uint32_t i = 0; i < 3; i++ )
        lsmash_bs_put_be16( bs, vmhd->opcolor[i] );
    return 0;
}

static int write_smhd( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_smhd_t *smhd = (isom_smhd_t *)box;
    lsm_box_bs_put_common( bs, smhd );
    lsmash_bs_put_be16( bs, smhd->balance );
    lsmash_bs_put_be16( bs, smhd->reserved );
    return 0;
}

static int write_hmhd( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_hmhd_t *hmhd = (isom_hmhd_t *)box;
    lsm_box_bs_put_common( bs, hmhd );
    lsmash_bs_put_be16( bs, hmhd->maxPDUsize );
    lsmash_bs_put_be16( bs, hmhd->avgPDUsize );
    lsmash_bs_put_be32( bs, hmhd->maxbitrate );
    lsmash_bs_put_be32( bs, hmhd->avgbitrate );
    lsmash_bs_put_be32( bs, hmhd->reserved );
    return 0;
}

static int write_nmhd( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_nmhd_t *nmhd = (isom_nmhd_t *)box;
    lsm_box_bs_put_common( bs, nmhd );
    return 0;
}

static int write_gmin( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_gmin_t *gmin = (isom_gmin_t *)box;
    lsm_box_bs_put_common( bs, gmin );
    lsmash_bs_put_be16( bs, gmin->graphicsmode );
    for( uint32_t i = 0; i < 3; i++ )
        lsmash_bs_put_be16( bs, gmin->opcolor[i] );
    lsmash_bs_put_be16( bs, gmin->balance );
    lsmash_bs_put_be16( bs, gmin->reserved );
    return 0;
}

static int write_text( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_text_t *text = (isom_text_t *)box;
    lsm_box_bs_put_common( bs, text );
    for( uint32_t i = 0; i < 9; i++ )
        lsmash_bs_put_be32( bs, text->matrix[i] );
    return 0;
}

static int write_gmhd( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_dref( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_dref_t *dref = (isom_dref_t *)box;
    lsm_box_bs_put_common( bs, dref );
    lsmash_bs_put_be32( bs, dref->list.entry_count );
    return 0;
}

static int write_url( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_dref_entry_t *url = (isom_dref_entry_t *)box;
    lsm_box_bs_put_common( bs, url );
    lsmash_bs_put_bytes( bs, url->location_length, url->location );
    return 0;
}

static int write_dinf( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_pasp( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_pasp_t *pasp = (isom_pasp_t *)box;
    lsm_box_bs_put_common( bs, pasp );
    lsmash_bs_put_be32( bs, pasp->hSpacing );
    lsmash_bs_put_be32( bs, pasp->vSpacing );
    return 0;
}

static int write_clap( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_clap_t *clap = (isom_clap_t *)box;
    lsm_box_bs_put_common( bs, clap );
    lsmash_bs_put_be32( bs, clap->cleanApertureWidthN );
    lsmash_bs_put_be32( bs, clap->cleanApertureWidthD );
    lsmash_bs_put_be32( bs, clap->cleanApertureHeightN );
    lsmash_bs_put_be32( bs, clap->cleanApertureHeightD );
    lsmash_bs_put_be32( bs, clap->horizOffN );
    lsmash_bs_put_be32( bs, clap->horizOffD );
    lsmash_bs_put_be32( bs, clap->vertOffN );
    lsmash_bs_put_be32( bs, clap->vertOffD );
    return 0;
}

static int write_colr( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_colr_t *colr = (isom_colr_t *)box;
    if( colr->color_parameter_type != ISOM_COLOR_PARAMETER_TYPE_NCLX
     && colr->color_parameter_type !=   QT_COLOR_PARAMETER_TYPE_NCLC )
        return 0;
    lsm_box_bs_put_common( bs, colr );
    lsmash_bs_put_be32( bs, colr->color_parameter_type );
    lsmash_bs_put_be16( bs, colr->primaries_index );
    lsmash_bs_put_be16( bs, colr->transfer_function_index );
    lsmash_bs_put_be16( bs, colr->matrix_index );
    if( colr->color_parameter_type == ISOM_COLOR_PARAMETER_TYPE_NCLX )
        lsmash_bs_put_byte( bs, (colr->full_range_flag << 7) | colr->reserved );
    return 0;
}

static int write_gama( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_gama_t *gama = (isom_gama_t *)box;
    if( !gama->parent )
        return 0;
    /* Note: 'gama' box is superseded by 'colr' box.
     * Therefore, writers of QTFF should never write both 'colr' and 'gama' box into an Image Description. */
    if( lsm_box_get_extension_box_format( &((isom_visual_entry_t *)gama->parent)->extensions, QT_BOX_TYPE_COLR ) )
        return 0;
    lsm_box_bs_put_common( bs, gama );
    lsmash_bs_put_be32( bs, gama->level );
    return 0;
}

static int write_fiel( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_fiel_t *fiel = (isom_fiel_t *)box;
    lsm_box_bs_put_common( bs, fiel );
    lsmash_bs_put_byte( bs, fiel->fields );
    lsmash_bs_put_byte( bs, fiel->detail );
    return 0;
}

static int write_clli( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_clli_t *clli = (isom_clli_t *)box;
    lsm_box_bs_put_common( bs, clli );
    lsmash_bs_put_be16( bs, clli->max_content_light_level );
    lsmash_bs_put_be16( bs, clli->max_pic_average_light_level );
    return 0;
}

static int write_mdcv( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_mdcv_t *mdcv = (isom_mdcv_t *)box;
    lsm_box_bs_put_common( bs, mdcv );
    lsmash_bs_put_be16( bs, mdcv->display_primaries_g_x );
    lsmash_bs_put_be16( bs, mdcv->display_primaries_g_y );
    lsmash_bs_put_be16( bs, mdcv->display_primaries_b_x );
    lsmash_bs_put_be16( bs, mdcv->display_primaries_b_y );
    lsmash_bs_put_be16( bs, mdcv->display_primaries_r_x );
    lsmash_bs_put_be16( bs, mdcv->display_primaries_r_y );
    lsmash_bs_put_be16( bs, mdcv->white_point_x );
    lsmash_bs_put_be16( bs, mdcv->white_point_y );
    lsmash_bs_put_be32( bs, mdcv->max_display_mastering_luminance );
    lsmash_bs_put_be32( bs, mdcv->min_display_mastering_luminance );
    return 0;
}

static int write_cspc( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_cspc_t *cspc = (isom_cspc_t *)box;
    lsm_box_bs_put_common( bs, cspc );
    lsmash_bs_put_be32( bs, cspc->pixel_format );
    return 0;
}

static int write_sgbt( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_sgbt_t *sgbt = (isom_sgbt_t *)box;
    lsm_box_bs_put_common( bs, sgbt );
    lsmash_bs_put_byte( bs, sgbt->significantBits );
    return 0;
}

static int write_stsl( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_stsl_t *stsl = (isom_stsl_t *)box;
    lsm_box_bs_put_common( bs, stsl );
    lsmash_bs_put_byte( bs, stsl->constraint_flag );
    lsmash_bs_put_byte( bs, stsl->scale_method );
    lsmash_bs_put_be16( bs, stsl->display_center_x );
    lsmash_bs_put_be16( bs, stsl->display_center_y );
    return 0;
}

static int write_esds( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_esds_t *esds = (isom_esds_t *)box;
    lsm_box_bs_put_common( bs, esds );
    lsm_mp4sys_update_descriptor_size( esds->ES );
    return lsm_mp4sys_write_descriptor( bs, esds->ES );
}

static int write_btrt( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_btrt_t *btrt = (isom_btrt_t *)box;
    lsm_box_bs_put_common( bs, btrt );
    lsmash_bs_put_be32( bs, btrt->bufferSizeDB );
    lsmash_bs_put_be32( bs, btrt->maxBitrate );
    lsmash_bs_put_be32( bs, btrt->avgBitrate );
    return 0;
}

static int write_dovi( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_dovi_t *dovi = (isom_dovi_t *)box;
    lsmash_bs_put_be32( bs, 32 );
    lsmash_bs_put_be32( bs, dovi->dv_profile <= 7 ? ISOM_BOX_TYPE_DVCC.fourcc : ISOM_BOX_TYPE_DVVC.fourcc );
    lsmash_bs_put_byte( bs, dovi->dv_version_major );
    lsmash_bs_put_byte( bs, dovi->dv_version_minor );

    uint16_t temp16 = 0;
    temp16 |= ((uint16_t) dovi->dv_profile) << 9;
    temp16 |= ((uint16_t) dovi->dv_level) << 3;
    temp16 |= ((uint16_t) dovi->rpu_present_flag) << 2;
    temp16 |= ((uint16_t) dovi->el_present_flag) << 1;
    temp16 |= ((uint16_t) dovi->bl_present_flag);
    lsmash_bs_put_be16( bs, temp16 );

    uint32_t temp32 = 0;
    temp32 |= ((uint32_t) dovi->dv_bl_signal_compatibility_id) << 28;
    temp32 |= dovi->reserved1;
    lsmash_bs_put_be32( bs, temp32 );

    lsmash_bs_put_be32( bs, dovi->reserved2[0] );
    lsmash_bs_put_be32( bs, dovi->reserved2[1] );
    lsmash_bs_put_be32( bs, dovi->reserved2[2] );
    lsmash_bs_put_be32( bs, dovi->reserved2[3] );
    return 0;
}

static int write_tims( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_tims_t *tims = (isom_tims_t *)box;
    lsm_box_bs_put_common( bs, tims );
    lsmash_bs_put_be32( bs, tims->timescale );
    return 0;
}

static int write_tsro( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_tsro_t *tsro = (isom_tsro_t *)box;
    lsm_box_bs_put_common( bs, tsro );
    lsmash_bs_put_be32( bs, tsro->offset );
    return 0;
}

static int write_tssy( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_tssy_t *tssy = (isom_tssy_t *)box;
    lsm_box_bs_put_common( bs, tssy );
    uint8_t data = 0;
    data  = tssy->reserved << 2;
    data |= tssy->timestamp_sync;
    lsmash_bs_put_byte( bs, data );
    return 0;
}

static int write_glbl( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_glbl_t *glbl = (isom_glbl_t *)box;
    lsm_box_bs_put_common( bs, glbl );
    if( glbl->header_data && glbl->header_size )
        lsmash_bs_put_bytes( bs, glbl->header_size, glbl->header_data );
    return 0;
}

static int write_frma( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_frma_t *frma = (isom_frma_t *)box;
    lsm_box_bs_put_common( bs, frma );
    lsmash_bs_put_be32( bs, frma->data_format );
    return 0;
}

static int write_enda( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_enda_t *enda = (isom_enda_t *)box;
    lsm_box_bs_put_common( bs, enda );
    lsmash_bs_put_be16( bs, enda->littleEndian );
    return 0;
}

static int write_mp4a( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_mp4a_t *mp4a = (isom_mp4a_t *)box;
    lsm_box_bs_put_common( bs, mp4a );
    lsmash_bs_put_be32( bs, mp4a->unknown );
    return 0;
}

static int write_chan( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_chan_t *chan = (isom_chan_t *)box;
    lsm_box_bs_put_common( bs, chan );
    lsmash_bs_put_be32( bs, chan->channelLayoutTag );
    lsmash_bs_put_be32( bs, chan->channelBitmap );
    lsmash_bs_put_be32( bs, chan->numberChannelDescriptions );
    if( chan->channelDescriptions )
        for( uint32_t i = 0; i < chan->numberChannelDescriptions; i++ )
        {
            isom_channel_description_t *channelDescriptions = (isom_channel_description_t *)(&chan->channelDescriptions[i]);
            lsmash_bs_put_be32( bs, channelDescriptions->channelLabel );
            lsmash_bs_put_be32( bs, channelDescriptions->channelFlags );
            lsmash_bs_put_be32( bs, channelDescriptions->coordinates[0] );
            lsmash_bs_put_be32( bs, channelDescriptions->coordinates[1] );
            lsmash_bs_put_be32( bs, channelDescriptions->coordinates[2] );
        }
    return 0;
}

static int write_terminator( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_wave( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_visual_description( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_visual_entry_t *data = (isom_visual_entry_t *)box;
    if( LSMASH_IS_NON_EXISTING_BOX( data ) )
        return LSMASH_ERR_NAMELESS;
    lsm_box_bs_put_common( bs, data );
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
    if( data->color_table_ID == 0 )
        write_bs_put_qt_color_table( bs, &data->color_table );
    return 0;
}

static int write_audio_description( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_audio_entry_t *data = (isom_audio_entry_t *)box;
    if( LSMASH_IS_NON_EXISTING_BOX( data ) )
        return LSMASH_ERR_NAMELESS;
    lsm_box_bs_put_common( bs, data );
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
    if( data->version == 1 && (data->manager & LSMASH_QTFF_BASE) )
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
    return 0;
}

static int write_srat( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_srat_t *srat = (isom_srat_t *)box;
    lsm_box_bs_put_common( bs, srat );
    lsmash_bs_put_be32( bs, srat->sampling_rate );
    return 0;
}

static int write_pcmC( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_pcmC_t *pcmC = (isom_pcmC_t *)box;
    lsm_box_bs_put_common( bs, pcmC );
    lsmash_bs_put_byte( bs, pcmC->format_flags );
    lsmash_bs_put_byte( bs, pcmC->PCM_sample_size );
    return 0;
}

static int write_chnl( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_chnl_t *chnl = (isom_chnl_t *)box;
    lsm_box_bs_put_common( bs, chnl );
    lsmash_bs_put_byte( bs, chnl->stream_structure );
    if( chnl->stream_structure & ISOM_CHNL_STREAM_STRUCTURE_CHANNELS )
    {
        lsmash_bs_put_byte( bs, chnl->definedLayout );
        if( chnl->definedLayout == 0 )
        {
            uint16_t channel_count = chnl->channel_count;
            if( !channel_count && chnl->parent && (chnl->parent->manager & LSMASH_AUDIO_DESCRIPTION) )
                channel_count = ((isom_audio_entry_t *)chnl->parent)->channelcount;
            if( channel_count && chnl->positions )
            {
                lsmash_bits_t bits = { 0 };
                lsmash_bits_init( &bits, bs );
                for( uint16_t i = 0; i < channel_count; i++ )
                {
                    uint8_t speaker_position = chnl->positions[i].speaker_position & 0x7f;
                    lsmash_bits_put( &bits, 7, speaker_position );
                    if( speaker_position == 126 )
                    {
                        lsmash_bits_put( &bits, 16, (uint16_t)chnl->positions[i].azimuth );
                        lsmash_bits_put( &bits, 8,  (uint8_t)chnl->positions[i].elevation );
                    }
                }
                lsmash_bits_put_align( &bits );
            }
        }
        else
            lsmash_bs_put_be64( bs, chnl->omittedChannelsMap );
    }
    if( chnl->stream_structure & ISOM_CHNL_STREAM_STRUCTURE_OBJECTS )
        lsmash_bs_put_byte( bs, chnl->object_count );
    return 0;
}

static int write_hint_description( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_hint_entry_t *data = (isom_hint_entry_t *)box;
    if( LSMASH_IS_NON_EXISTING_BOX( data ) )
        return LSMASH_ERR_NAMELESS;
    lsm_box_bs_put_common( bs, data );
    lsmash_bs_put_bytes( bs, 6, data->reserved );
    lsmash_bs_put_be16( bs, data->hinttrackversion );
    lsmash_bs_put_be16( bs, data->highestcompatibleversion );
    lsmash_bs_put_be32( bs, data->maxpacketsize );
    return 0;
}

#if 0
static int write_metadata_description( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_metadata_entry_t *data = (isom_metadata_entry_t *)entry->data;
    if( LSMASH_IS_NON_EXISTING_BOX( data ) )
        return LSMASH_ERR_NAMELESS;
    lsm_box_bs_put_common( bs, data );
    lsmash_bs_put_bytes( bs, 6, data->reserved );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    return 0;
}
#endif

static int write_qt_text_description( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_qt_text_entry_t *data = (isom_qt_text_entry_t *)box;
    if( LSMASH_IS_NON_EXISTING_BOX( data ) )
        return LSMASH_ERR_NAMELESS;
    lsm_box_bs_put_common( bs, data );
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
    return 0;
}

static int write_ftab( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_ftab_t *ftab = (isom_ftab_t *)box;
    assert( ftab->list );
    lsm_box_bs_put_common( bs, ftab );
    lsmash_bs_put_be16( bs, ftab->list->entry_count );
    for( lsmash_entry_t *entry = ftab->list->head; entry; entry = entry->next )
    {
        isom_font_record_t *data = (isom_font_record_t *)entry->data;
        if( !data )
            return LSMASH_ERR_NAMELESS;
        lsmash_bs_put_be16( bs, data->font_ID );
        lsmash_bs_put_byte( bs, data->font_name_length );
        if( data->font_name && data->font_name_length )
            lsmash_bs_put_bytes( bs, data->font_name_length, data->font_name );
    }
    return 0;
}

static int write_tx3g_description( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_tx3g_entry_t *data = (isom_tx3g_entry_t *)box;
    if( LSMASH_IS_NON_EXISTING_BOX( data ) )
        return LSMASH_ERR_NAMELESS;
    lsm_box_bs_put_common( bs, data );
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
    return 0;
}

static int write_SA3D( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_SA3D_t *SA3D = (isom_SA3D_t *)box;

    lsm_box_bs_put_common( bs, SA3D );
    lsmash_bs_put_byte( bs, SA3D->version );
    lsmash_bs_put_byte( bs, (SA3D->head_locked_stereo << 7) | SA3D->ambisonic_type );
    lsmash_bs_put_be32( bs, SA3D->ambisonic_order );
    lsmash_bs_put_byte( bs, SA3D->ambisonic_channel_ordering );
    lsmash_bs_put_byte( bs, SA3D->ambisonic_normalization );
    lsmash_bs_put_be32( bs, SA3D->num_channels );
    for( lsmash_entry_t *entry = SA3D->channel_map.head; entry; entry = entry->next )
    {
        uint32_t *data = (uint32_t *)entry->data;
        if( !data )
            return LSMASH_ERR_NAMELESS;
        lsmash_bs_put_be32( bs, *data );
    }

    return 0;
}

static int write_st3d( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_st3d_t *st3d = (isom_st3d_t *)box;
    lsm_box_bs_put_common( bs, st3d );
    lsmash_bs_put_byte( bs, st3d->stereo_mode );
    return 0;
}

static int write_sv3d( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_svhd( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_svhd_t *svhd = (isom_svhd_t *)box;
    lsm_box_bs_put_common( bs, svhd );
    lsmash_bs_put_byte( bs, svhd->metadata_source_length );
    if( svhd->metadata_source_length )
        lsmash_bs_put_bytes( bs, svhd->metadata_source_length, svhd->metadata_source );
    return 0;
}

static int write_proj( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_prhd( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_prhd_t *prhd = (isom_prhd_t *)box;
    lsm_box_bs_put_common( bs, prhd );
    lsmash_bs_put_be32( bs, prhd->pose_yaw_degrees );
    lsmash_bs_put_be32( bs, prhd->pose_pitch_degrees );
    lsmash_bs_put_be32( bs, prhd->pose_roll_degrees );
    return 0;
}

static int write_equi( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_equi_t *equi = (isom_equi_t *)box;
    lsm_box_bs_put_common( bs, equi );
    lsmash_bs_put_be32( bs, equi->projection_bounds_top );
    lsmash_bs_put_be32( bs, equi->projection_bounds_bottom );
    lsmash_bs_put_be32( bs, equi->projection_bounds_left );
    lsmash_bs_put_be32( bs, equi->projection_bounds_right );
    return 0;
}

static int write_cbmp( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_cbmp_t *cbmp = (isom_cbmp_t *)box;
    lsm_box_bs_put_common( bs, cbmp );
    lsmash_bs_put_be32( bs, cbmp->layout );
    lsmash_bs_put_be32( bs, cbmp->padding );
    return 0;
}

static int write_stsd( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_stsd_t *stsd = (isom_stsd_t *)box;
    lsm_box_bs_put_common( bs, stsd );
    lsmash_bs_put_be32( bs, stsd->list.entry_count );
    return 0;
}

static int write_stts( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_stts_t *stts = (isom_stts_t *)box;
    assert( stts->list );
    lsm_box_bs_put_common( bs, stts );
    lsmash_bs_put_be32( bs, lsm_list_count( stts->list ) );
    isom_stts_entry_t *data;
    LSM_LIST_FOR_EACH( data, stts->list )
    {
        lsmash_bs_put_be32( bs, data->sample_count );
        lsmash_bs_put_be32( bs, data->sample_delta );
    }
    return 0;
}

static int write_ctts( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_ctts_t *ctts = (isom_ctts_t *)box;
    assert( ctts->list );
    lsm_box_bs_put_common( bs, ctts );
    lsmash_bs_put_be32( bs, lsm_list_count( ctts->list ) );
    isom_ctts_entry_t *data;
    LSM_LIST_FOR_EACH( data, ctts->list )
    {
        lsmash_bs_put_be32( bs, data->sample_count );
        lsmash_bs_put_be32( bs, data->sample_offset );
    }
    return 0;
}

static int write_cslg( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_cslg_t *cslg = (isom_cslg_t *)box;
    lsm_box_bs_put_common( bs, cslg );
    lsmash_bs_put_be32( bs, cslg->compositionToDTSShift );
    lsmash_bs_put_be32( bs, cslg->leastDecodeToDisplayDelta );
    lsmash_bs_put_be32( bs, cslg->greatestDecodeToDisplayDelta );
    lsmash_bs_put_be32( bs, cslg->compositionStartTime );
    lsmash_bs_put_be32( bs, cslg->compositionEndTime );
    return 0;
}

static int write_stsz( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_stsz_t *stsz = (isom_stsz_t *)box;
    lsm_box_bs_put_common( bs, stsz );
    lsmash_bs_put_be32( bs, stsz->sample_size );
    lsmash_bs_put_be32( bs, stsz->sample_count );
    if( stsz->sample_size == 0 && stsz->list )
    {
        isom_stsz_entry_t *data;
        LSM_LIST_FOR_EACH( data, stsz->list )
            lsmash_bs_put_be32( bs, data->entry_size );
    }
    return 0;
}

static int write_stz2( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_stz2_t *stz2 = (isom_stz2_t *)box;
    lsm_box_bs_put_common( bs, stz2 );
    lsmash_bs_put_be32( bs, (stz2->reserved << 8) | stz2->field_size );
    lsmash_bs_put_be32( bs, stz2->sample_count );
    if( stz2->field_size == 16 )
    {
        isom_stsz_entry_t *data;
        LSM_LIST_FOR_EACH( data, stz2->list )
        {
            assert( data->entry_size <= 0xffff );
            lsmash_bs_put_be16( bs, data->entry_size );
        }
    }
    else if( stz2->field_size == 8 )
    {
        isom_stsz_entry_t *data;
        LSM_LIST_FOR_EACH( data, stz2->list )
        {
            assert( data->entry_size <= 0xff );
            lsmash_bs_put_byte( bs, data->entry_size );
        }
    }
    else if( stz2->field_size == 4 )
    {
        isom_stsz_entry_t zero_padding = { .entry_size = 0 };
        uint32_t count = lsm_list_count( stz2->list );
        for( uint32_t i = 0; i < count; i += 2 )
        {
            isom_stsz_entry_t *data_o = (isom_stsz_entry_t *)lsm_list_at( stz2->list, i );
            isom_stsz_entry_t *data_e = (i + 1 < count) ? (isom_stsz_entry_t *)lsm_list_at( stz2->list, i + 1 ) : &zero_padding;
            if( !data_o || !data_e )
                return LSMASH_ERR_NAMELESS;
            assert( data_o->entry_size <= 0xf && data_e->entry_size <= 0xf );
            lsmash_bs_put_byte( bs, (data_o->entry_size << 4) | data_e->entry_size );
        }
    }
    else
        return LSMASH_ERR_NAMELESS;
    return 0;
}

static int write_stss( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_stss_t *stss = (isom_stss_t *)box;
    assert( stss->list );
    lsm_box_bs_put_common( bs, stss );
    lsmash_bs_put_be32( bs, lsm_list_count( stss->list ) );
    isom_stss_entry_t *data;
    LSM_LIST_FOR_EACH( data, stss->list )
        lsmash_bs_put_be32( bs, data->sample_number );
    return 0;
}

static int write_stps( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_stps_t *stps = (isom_stps_t *)box;
    assert( stps->list );
    lsm_box_bs_put_common( bs, stps );
    lsmash_bs_put_be32( bs, lsm_list_count( stps->list ) );
    isom_stps_entry_t *data;
    LSM_LIST_FOR_EACH( data, stps->list )
        lsmash_bs_put_be32( bs, data->sample_number );
    return 0;
}

static int write_sdtp( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_sdtp_t *sdtp = (isom_sdtp_t *)box;
    assert( sdtp->list );
    lsm_box_bs_put_common( bs, sdtp );
    isom_sdtp_entry_t *data;
    LSM_LIST_FOR_EACH( data, sdtp->list )
    {
        uint8_t temp = (data->is_leading            << 6)
                     | (data->sample_depends_on     << 4)
                     | (data->sample_is_depended_on << 2)
                     |  data->sample_has_redundancy;
        lsmash_bs_put_byte( bs, temp );
    }
    return 0;
}

static int write_stsc( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_stsc_t *stsc = (isom_stsc_t *)box;
    assert( stsc->list );
    lsm_box_bs_put_common( bs, stsc );
    lsmash_bs_put_be32( bs, lsm_list_count( stsc->list ) );
    isom_stsc_entry_t *data;
    LSM_LIST_FOR_EACH( data, stsc->list )
    {
        lsmash_bs_put_be32( bs, data->first_chunk );
        lsmash_bs_put_be32( bs, data->samples_per_chunk );
        lsmash_bs_put_be32( bs, data->sample_description_index );
    }
    return 0;
}

static int write_co64( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_stco_t *co64 = (isom_stco_t *)box;
    assert( co64->list );
    lsm_box_bs_put_common( bs, co64 );
    lsmash_bs_put_be32( bs, lsm_list_count( co64->list ) );
    isom_co64_entry_t *data;
    LSM_LIST_FOR_EACH( data, co64->list )
        lsmash_bs_put_be64( bs, data->chunk_offset );
    return 0;
}

static int write_stco( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_stco_t *stco = (isom_stco_t *)box;
    if( stco->large_presentation )
        return write_co64( bs, box );
    assert( stco->list );
    lsm_box_bs_put_common( bs, stco );
    lsmash_bs_put_be32( bs, lsm_list_count( stco->list ) );
    isom_stco_entry_t *data;
    LSM_LIST_FOR_EACH( data, stco->list )
        lsmash_bs_put_be32( bs, data->chunk_offset );
    return 0;
}

static int write_sgpd( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_sgpd_t *sgpd = (isom_sgpd_t *)box;
    assert( sgpd->list );
    lsm_box_bs_put_common( bs, sgpd );
    lsmash_bs_put_be32( bs, sgpd->grouping_type );
    if( sgpd->version == 1 )
        lsmash_bs_put_be32( bs, sgpd->default_length );
    lsmash_bs_put_be32( bs, lsm_list_count( sgpd->list ) );
    void *data;
    LSM_LIST_FOR_EACH( data, sgpd->list )
    {
        switch( sgpd->grouping_type )
        {
            case ISOM_GROUP_TYPE_RAP :
            {
                isom_rap_entry_t *rap = (isom_rap_entry_t *)data;
                uint8_t temp = (rap->num_leading_samples_known << 7)
                             |  rap->num_leading_samples;
                lsmash_bs_put_byte( bs, temp );
                break;
            }
            case ISOM_GROUP_TYPE_ROLL :
            case ISOM_GROUP_TYPE_PROL :
                lsmash_bs_put_be16( bs, ((isom_roll_entry_t *)data)->roll_distance );
                break;
            default :
                /* We don't consider other grouping types currently. */
                // if( sgpd->version == 1 && !sgpd->default_length )
                //     lsmash_bs_put_be32( bs, ((isom_sgpd_t *)entry->data)->description_length );
                break;
        }
    }
    return 0;
}

static int write_sbgp( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_sbgp_t *sbgp = (isom_sbgp_t *)box;
    assert( sbgp->list );
    lsm_box_bs_put_common( bs, sbgp );
    lsmash_bs_put_be32( bs, sbgp->grouping_type );
    if( sbgp->version == 1 )
        lsmash_bs_put_be32( bs, sbgp->grouping_type_parameter );
    lsmash_bs_put_be32( bs, lsm_list_count( sbgp->list ) );
    isom_group_assignment_entry_t *data;
    LSM_LIST_FOR_EACH( data, sbgp->list )
    {
        lsmash_bs_put_be32( bs, data->sample_count );
        lsmash_bs_put_be32( bs, data->group_description_index );
    }
    return 0;
}

static int write_stbl( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_minf( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_mdia( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_chpl( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_chpl_t *chpl = (isom_chpl_t *)box;
    assert( chpl->list );
    lsm_box_bs_put_common( bs, chpl );
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
            return LSMASH_ERR_NAMELESS;
        lsmash_bs_put_be64( bs, data->start_time );
        lsmash_bs_put_byte( bs, data->chapter_name_length );
        lsmash_bs_put_bytes( bs, data->chapter_name_length, data->chapter_name );
    }
    return 0;
}

static int write_mean( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_mean_t *mean = (isom_mean_t *)box;
    lsm_box_bs_put_common( bs, mean );
    if( mean->meaning_string && mean->meaning_string_length )
        lsmash_bs_put_bytes( bs, mean->meaning_string_length, mean->meaning_string );
    return 0;
}

static int write_name( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_name_t *name = (isom_name_t *)box;
    lsm_box_bs_put_common( bs, name );
    if( name->name && name->name_length )
        lsmash_bs_put_bytes( bs, name->name_length, name->name );
    return 0;
}

static int write_data( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_data_t *data = (isom_data_t *)box;
    lsm_box_bs_put_common( bs, data );
    lsmash_bs_put_be16( bs, data->reserved );
    lsmash_bs_put_byte( bs, data->type_set_identifier );
    lsmash_bs_put_byte( bs, data->type_code );
    lsmash_bs_put_be32( bs, data->the_locale );
    if( data->value && data->value_length )
        lsmash_bs_put_bytes( bs, data->value_length, data->value );
    return 0;
}

static int write_metaitem( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_ilst( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_meta( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_cprt( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_cprt_t *cprt = (isom_cprt_t *)box;
    lsm_box_bs_put_common( bs, cprt );
    lsmash_bs_put_be16( bs, cprt->language );
    lsmash_bs_put_bytes( bs, cprt->notice_length, cprt->notice );
    return 0;
}

static int write_udta( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_hnti( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_rtp( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_rtp_t *rtp = (isom_rtp_t *)box;
    lsm_box_bs_put_common( bs, rtp );
    lsmash_bs_put_be32( bs, rtp->descriptionformat );
    lsmash_bs_put_bytes( bs, rtp->sdp_length, rtp->sdptext );
    return 0;
}
static int write_sdp( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_sdp_t *sdp = (isom_sdp_t *)box;
    lsm_box_bs_put_common( bs, sdp );
    lsmash_bs_put_bytes( bs, sdp->sdp_length, sdp->sdptext );
    return 0;
}

static int write_trak( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_iods( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_iods_t *iods = (isom_iods_t *)box;
    lsm_box_bs_put_common( bs, iods );
    lsm_mp4sys_update_descriptor_size( iods->OD );
    return lsm_mp4sys_write_descriptor( bs, iods->OD );
}

static int write_mvhd( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_mvhd_t *mvhd = (isom_mvhd_t *)box;
    /* Check the version. */
    if( (LSMASH_IS_EXISTING_BOX( mvhd->file ) && !mvhd->file->undefined_64_ver)
     && (mvhd->creation_time     > UINT32_MAX
      || mvhd->modification_time > UINT32_MAX
      || mvhd->duration          > UINT32_MAX) )
        mvhd->version = 1;
    else
        mvhd->version = 0;
    /* Write. */
    lsm_box_bs_put_common( bs, mvhd );
    if( mvhd->version )
    {
        lsmash_bs_put_be64( bs, mvhd->creation_time );
        lsmash_bs_put_be64( bs, mvhd->modification_time );
        lsmash_bs_put_be32( bs, mvhd->timescale );
        lsmash_bs_put_be64( bs, mvhd->duration );
    }
    else
    {
        lsmash_bs_put_be32( bs, LSMASH_MIN( mvhd->creation_time,     UINT32_MAX ) );
        lsmash_bs_put_be32( bs, LSMASH_MIN( mvhd->modification_time, UINT32_MAX ) );
        lsmash_bs_put_be32( bs, mvhd->timescale );
        lsmash_bs_put_be32( bs, LSMASH_MIN( mvhd->duration,          UINT32_MAX ) );
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
    return 0;
}

static void write_bs_put_sample_flags( lsmash_bs_t *bs, isom_sample_flags_t *flags )
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

static int write_mehd( lsmash_bs_t *bs, isom_box_t *box )
{
    if( box->manager & LSMASH_PLACEHOLDER )
    {
        /* Movie Extends Header Box is not written immediately.
         * It's done after finishing all movie fragments.
         * The following will be overwritten by Movie Extends Header Box.
         * We use version 1 Movie Extends Header Box since it causes extra 4 bytes region
         * we cannot replace with empty Free Space Box as we place version 0 one.  */
        box->pos = box->file->bs->written;
        lsmash_bs_put_be32( bs, ISOM_BASEBOX_COMMON_SIZE + 12 );
        lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_FREE.fourcc );
        lsmash_bs_put_be32( bs, 0 );
        lsmash_bs_put_be64( bs, 0 );
    }
    else
    {
        isom_mehd_t *mehd = (isom_mehd_t *)box;
        //mehd->version = mehd->fragment_duration > UINT32_MAX ? 1 : 0;
        lsm_box_bs_put_common( bs, mehd );
        if( mehd->version == 1 )
            lsmash_bs_put_be64( bs, mehd->fragment_duration );
        else
            lsmash_bs_put_be32( bs, LSMASH_MIN( mehd->fragment_duration, UINT32_MAX ) );
    }
    return 0;
}

static int write_trex( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_trex_t *trex = (isom_trex_t *)box;
    lsm_box_bs_put_common( bs, trex );
    lsmash_bs_put_be32( bs, trex->track_ID );
    lsmash_bs_put_be32( bs, trex->default_sample_description_index );
    lsmash_bs_put_be32( bs, trex->default_sample_duration );
    lsmash_bs_put_be32( bs, trex->default_sample_size );
    write_bs_put_sample_flags( bs, &trex->default_sample_flags );
    return 0;
}

static int write_mvex( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_mfhd( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_mfhd_t *mfhd = (isom_mfhd_t *)box;
    lsm_box_bs_put_common( bs, mfhd );
    lsmash_bs_put_be32( bs, mfhd->sequence_number );
    return 0;
}

static int write_tfhd( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_tfhd_t *tfhd = (isom_tfhd_t *)box;
    lsm_box_bs_put_common( bs, tfhd );
    lsmash_bs_put_be32( bs, tfhd->track_ID );
    if( tfhd->flags & ISOM_TF_FLAGS_BASE_DATA_OFFSET_PRESENT         ) lsmash_bs_put_be64( bs, tfhd->base_data_offset );
    if( tfhd->flags & ISOM_TF_FLAGS_SAMPLE_DESCRIPTION_INDEX_PRESENT ) lsmash_bs_put_be32( bs, tfhd->sample_description_index );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT  ) lsmash_bs_put_be32( bs, tfhd->default_sample_duration );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_SIZE_PRESENT      ) lsmash_bs_put_be32( bs, tfhd->default_sample_size );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT     ) write_bs_put_sample_flags( bs, &tfhd->default_sample_flags );
    return 0;
}

static int write_tfdt( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_tfdt_t *tfdt = (isom_tfdt_t *)box;
    /* Check the version. */
    tfdt->version = tfdt->baseMediaDecodeTime > UINT32_MAX ? 1 : 0;
    /* Write. */
    lsm_box_bs_put_common( bs, tfdt );
    if( tfdt->version == 1 )
        lsmash_bs_put_be64( bs, tfdt->baseMediaDecodeTime );
    else
        lsmash_bs_put_be32( bs, tfdt->baseMediaDecodeTime );
    return 0;
}

static int write_trun( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_trun_t *trun = (isom_trun_t *)box;
    lsm_box_bs_put_common( bs, trun );
    lsmash_bs_put_be32( bs, trun->sample_count );
    if( trun->flags & ISOM_TR_FLAGS_DATA_OFFSET_PRESENT        ) lsmash_bs_put_be32( bs, trun->data_offset );
    if( trun->flags & ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT ) write_bs_put_sample_flags( bs, &trun->first_sample_flags );
    if( trun->optional )
    {
        isom_trun_optional_row_t *data;
        LSM_LIST_FOR_EACH( data, trun->optional )
        {
            if( !data )
                return LSMASH_ERR_NAMELESS;
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT                ) lsmash_bs_put_be32( bs, data->sample_duration );
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT                    ) lsmash_bs_put_be32( bs, data->sample_size );
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT                   ) write_bs_put_sample_flags( bs, &data->sample_flags );
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT ) lsmash_bs_put_be32( bs, data->sample_composition_time_offset );
        }
    }
    return 0;
}

static int write_traf( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_moof( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_tfra( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_tfra_t *tfra = (isom_tfra_t *)box;
    lsm_box_bs_put_common( bs, tfra );
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
        void (*bs_put_time)         ( lsmash_bs_t *, uint64_t ) = bs_put_funcs[ tfra->version == 1 ? 4 : 3      ];
        void (*bs_put_moof_offset)  ( lsmash_bs_t *, uint64_t ) = bs_put_funcs[ tfra->version == 1 ? 4 : 3      ];
        void (*bs_put_traf_number)  ( lsmash_bs_t *, uint64_t ) = bs_put_funcs[ tfra->length_size_of_traf_num   ];
        void (*bs_put_trun_number)  ( lsmash_bs_t *, uint64_t ) = bs_put_funcs[ tfra->length_size_of_trun_num   ];
        void (*bs_put_sample_number)( lsmash_bs_t *, uint64_t ) = bs_put_funcs[ tfra->length_size_of_sample_num ];
        isom_tfra_location_time_entry_t *data;
        LSM_LIST_FOR_EACH( data, tfra->list )
        {
            if( !data )
                return LSMASH_ERR_NAMELESS;
            bs_put_time         ( bs, data->time          );
            bs_put_moof_offset  ( bs, data->moof_offset   );
            bs_put_traf_number  ( bs, data->traf_number   );
            bs_put_trun_number  ( bs, data->trun_number   );
            bs_put_sample_number( bs, data->sample_number );
        }
    }
    return 0;
}

static int write_mfro( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_mfro_t *mfro = (isom_mfro_t *)box;
    lsm_box_bs_put_common( bs, mfro );
    lsmash_bs_put_be32( bs, mfro->length ); /* determined at write_mfra(). */
    return 0;
}

static int write_mfra( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_mfra_t *mfra = (isom_mfra_t *)box;
    if( mfra->mfro )
        mfra->mfro->length = mfra->size;
    lsm_box_bs_put_common( bs, mfra );
    return 0;
}

static int write_mdat( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_mdat_t   *mdat = (isom_mdat_t *)box;
    lsmash_file_t *file = mdat->file;
    /* If any fragment, write the Media Data Box all at once. */
    if( file->fragment )
    {
        /* Write the size and type fields of the Media Data Box. */
        mdat->size = ISOM_BASEBOX_COMMON_SIZE + file->fragment->pool_size;
        if( mdat->size > UINT32_MAX )
            mdat->size += 8;    /* large_size */
        lsm_box_bs_put_common( bs, mdat );
        /* Write the samples in the current movie fragment. */
        isom_sample_pool_t *pool;
        LSM_LIST_FOR_EACH( pool, file->fragment->pool )
        {
            if( !pool )
                return LSMASH_ERR_NAMELESS;
            lsmash_bs_put_bytes( bs, pool->size, pool->data );
        }
        mdat->media_size = file->fragment->pool_size;
        return 0;
    }
    if( mdat->manager & LSMASH_PLACEHOLDER )
    {
        /* Write an incomplete Media Data Box.
         * Braindead implementation might check box order and return an error if an expected box does not come the
         * next. Placement of eight 0x00 byte string as a simple large_size placeholder passes such silly box order
         * checks. This placement is more compatible than placement of a Free Space Box ('free' or 'skip') or a
         * Placeholder Atom ('wide') as a large_size placeholder since Media Data Box can store any data and any
         * implementation surely do not check what contents are stored in it until taking samples out according to
         * chunk offsets, and the placeholder is placed before any chunk offset thus it won't be touched. */
        mdat->pos      = bs->offset;
        mdat->size     = ISOM_BASEBOX_COMMON_SIZE + 8 + mdat->reserved_size;
        mdat->manager |= LSMASH_INCOMPLETE_BOX;
        mdat->manager &= ~LSMASH_PLACEHOLDER;
        lsm_box_bs_put_common( bs, mdat );
        if( mdat->size <= UINT32_MAX )
            lsmash_bs_put_be64( bs, 0x0000000000000000 );
        mdat->size     = ISOM_BASEBOX_COMMON_SIZE + 8;
        return 0;
    }
    assert( !(mdat->manager & (LSMASH_INCOMPLETE_BOX | LSMASH_PLACEHOLDER)) );
    uint64_t actual_size   = ISOM_BASEBOX_COMMON_SIZE + 8 + mdat->media_size;
    uint64_t reserved_size = ISOM_BASEBOX_COMMON_SIZE + 8 + mdat->reserved_size;
    if( actual_size < reserved_size )
    {
        /* Write padding zero bytes until end of this box.
         * This code path is invoked when the size of a Media Data Box was reserved. */
        mdat->size = reserved_size;
        int err = lsmash_bs_flush_buffer( bs );
        if( err )
            return err;
        uint64_t padding_size = reserved_size - actual_size;
        static const uint8_t zero_bytes[64] = { 0 };
        while( padding_size > sizeof(zero_bytes) )
        {
            if( (err = lsmash_bs_write_data( bs, zero_bytes, sizeof(zero_bytes) )) < 0 )
                return err;
            padding_size -= sizeof(zero_bytes);
        }
        return lsmash_bs_write_data( bs, zero_bytes, padding_size );
    }
    if( !bs->unseekable )
    {
        /* Write the actual size. */
        uint64_t current_pos = bs->offset;
        mdat->size = actual_size;
        lsmash_bs_write_seek( bs, mdat->pos, SEEK_SET );
        lsm_box_bs_put_common( bs, mdat );
        /* lsm_write_box() also calls lsmash_bs_flush_buffer() but it must do nothing. */
        int ret = lsmash_bs_flush_buffer( bs );
        lsmash_bs_write_seek( bs, current_pos, SEEK_SET );
        return ret;
    }
    return LSMASH_ERR_NAMELESS;
}

static int write_ftyp( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_ftyp_t *ftyp = (isom_ftyp_t *)box;
    if( ftyp->brand_count == 0 )
        return 0;
    lsm_box_bs_put_common( bs, ftyp );
    lsmash_bs_put_be32( bs, ftyp->major_brand );
    lsmash_bs_put_be32( bs, ftyp->minor_version );
    for( uint32_t i = 0; i < ftyp->brand_count; i++ )
        lsmash_bs_put_be32( bs, ftyp->compatible_brands[i] );
    return 0;
}

static int write_moov( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_common( bs, box );
    return 0;
}

static int write_free( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_free_t *skip = (isom_free_t *)box;
    lsm_box_bs_put_common( bs, skip );
    if( skip->data && skip->length )
        lsmash_bs_put_bytes( bs, skip->length, skip->data );
    return 0;
}

static int write_sidx( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_sidx_t *sidx = (isom_sidx_t *)box;
    /* Check the version. */
    if( sidx->earliest_presentation_time > UINT32_MAX
     || sidx->first_offset               > UINT32_MAX )
        sidx->version = 1;
    else
        sidx->version = 0;
    /* Write. */
    lsm_box_bs_put_common( bs, sidx );
    lsmash_bs_put_be32( bs, sidx->reference_ID );
    lsmash_bs_put_be32( bs, sidx->timescale );
    if( sidx->version == 0 )
    {
        lsmash_bs_put_be32( bs, LSMASH_MIN( sidx->earliest_presentation_time, UINT32_MAX ) );
        lsmash_bs_put_be32( bs, LSMASH_MIN( sidx->first_offset,               UINT32_MAX ) );
    }
    else
    {
        lsmash_bs_put_be64( bs, sidx->earliest_presentation_time );
        lsmash_bs_put_be64( bs, sidx->first_offset );
    }
    lsmash_bs_put_be16( bs, sidx->reserved );
    lsmash_bs_put_be16( bs, sidx->reference_count );
    isom_sidx_referenced_item_t *data;
    LSM_LIST_FOR_EACH( data, sidx->list )
    {
        if( !data )
            return LSMASH_ERR_NAMELESS;
        uint32_t temp32;
        temp32 = (data->reference_type << 31)
               |  data->reference_size;
        lsmash_bs_put_be32( bs, temp32 );
        lsmash_bs_put_be32( bs, data->subsegment_duration );
        temp32 = (data->starts_with_SAP << 31)
               | (data->SAP_type        << 28)
               |  data->SAP_delta_time;
        lsmash_bs_put_be32( bs, temp32 );
    }
    return 0;
}

int lsm_write_box( lsmash_bs_t *bs, isom_box_t *box )
{
    assert( bs );
    /* Don't write any incomplete or already written box to a file. */
    if( LSMASH_IS_NON_EXISTING_BOX( box )
     || !box->write
     || (bs->stream && (box->manager & (LSMASH_INCOMPLETE_BOX | LSMASH_WRITTEN_BOX))) )
        return 0;
    int ret = box->write( bs, box );
    if( ret < 0 )
        return ret;
    if( bs->stream )
    {
        if( (ret = lsmash_bs_flush_buffer( bs )) < 0 )
            return ret;
        /* Don't write any child box if this box is a placeholder or an incomplete box. */
        if( box->manager & (LSMASH_PLACEHOLDER | LSMASH_INCOMPLETE_BOX) )
            return 0;
        else
            box->manager |= LSMASH_WRITTEN_BOX;
    }
    return write_children( bs, box );
}

void lsm_write_set_box_writer( isom_box_t *box )
{
    if( box->manager & LSMASH_BINARY_CODED_BOX )
    {
        box->write = write_binary_coded_box;
        return;
    }
    else if( box->manager & LSMASH_UNKNOWN_BOX )
    {
        box->write = write_unknown_box;
        return;
    }
    assert( LSMASH_IS_EXISTING_BOX( box->parent ) );
    isom_box_t *parent = box->parent;
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STSD ) )
    {
        /* OK, this box is a sample entry.
         * Here, determine the suitable sample entry writer by media type if possible. */
        if( !lsm_box_check_media_hdlr_from_stsd( (isom_stsd_t *)parent ) )
            return;
        lsmash_media_type media_type = lsm_box_get_media_type_from_stsd( (isom_stsd_t *)parent );
        if( media_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK )
            box->write = write_visual_description;
        else if( media_type == ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK )
            box->write = write_audio_description;
        else if( media_type == ISOM_MEDIA_HANDLER_TYPE_HINT_TRACK )
            box->write = write_hint_description;
        else if( media_type == ISOM_MEDIA_HANDLER_TYPE_TEXT_TRACK )
        {
            if( lsmash_check_box_type_identical( box->type, QT_CODEC_TYPE_TEXT_TEXT ) )
                box->write = write_qt_text_description;
            else if( lsmash_check_box_type_identical( box->type, ISOM_CODEC_TYPE_TX3G_TEXT ) )
                box->write = write_tx3g_description;
        }
        if( box->write )
            return;
    }
    if( lsmash_check_box_type_identical( parent->type, QT_BOX_TYPE_WAVE ) )
    {
             if( lsmash_check_box_type_identical( box->type, QT_BOX_TYPE_FRMA ) )       box->write = write_frma;
        else if( lsmash_check_box_type_identical( box->type, QT_BOX_TYPE_ENDA ) )       box->write = write_enda;
        else if( lsmash_check_box_type_identical( box->type, QT_BOX_TYPE_MP4A ) )       box->write = write_mp4a;
        else if( lsmash_check_box_type_identical( box->type, QT_BOX_TYPE_ESDS ) )       box->write = write_esds;
        else if( lsmash_check_box_type_identical( box->type, QT_BOX_TYPE_CHAN ) )       box->write = write_chan;
        else if( lsmash_check_box_type_identical( box->type, QT_BOX_TYPE_TERMINATOR ) ) box->write = write_terminator;
        else                                                                            box->write = NULL;
        return;
    }
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TREF ) )
    {
        box->write = write_track_reference_type;
        return;
    }
    static const struct box_writer_table_tag
    {
        const lsmash_box_type_t *type;
        isom_extension_writer_t  writer_func;
    } box_writer_table[] =
    {
#define ADD_BOX_WRITER_TABLE_ELEMENT( type, writer_func ) { &(type), writer_func }
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_FTYP, write_ftyp ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_STYP, write_ftyp ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_SIDX, write_sidx ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_MOOV, write_moov ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_MVHD, write_mvhd ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_IODS, write_iods ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_CTAB, write_ctab ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_ESDS, write_esds ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_TRAK, write_trak ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_TKHD, write_tkhd ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_TAPT, write_tapt ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_CLEF, write_clef ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_PROF, write_prof ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_ENOF, write_enof ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_EDTS, write_edts ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_ELST, write_elst ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_TREF, write_tref ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_MDIA, write_mdia ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_MDHD, write_mdhd ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_HDLR, write_hdlr ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_MINF, write_minf ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_VMHD, write_vmhd ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_SMHD, write_smhd ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_HMHD, write_hmhd ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_NMHD, write_nmhd ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_GMHD, write_gmhd ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_GMIN, write_gmin ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_TEXT, write_text ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_DINF, write_dinf ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_DREF, write_dref ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_URL,  write_url  ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_STBL, write_stbl ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_SA3D, write_SA3D ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_ST3D, write_st3d ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_SV3D, write_sv3d ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_SVHD, write_svhd ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_PROJ, write_proj ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_PRHD, write_prhd ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_EQUI, write_equi ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_CBMP, write_cbmp ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_STSD, write_stsd ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_DVCC, write_dovi ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_DVVC, write_dovi ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_BTRT, write_btrt ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_TIMS, write_tims ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_TSRO, write_tsro ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_TSSY, write_tssy ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_COLR, write_colr ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_COLR, write_colr ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_CLAP, write_clap ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_PASP, write_pasp ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_GLBL, write_glbl ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_GAMA, write_gama ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_FIEL, write_fiel ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_CLLI, write_clli ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_MDCV, write_mdcv ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_CSPC, write_cspc ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_SGBT, write_sgbt ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_STSL, write_stsl ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_WAVE, write_wave ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_MP4A, write_mp4a ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_CHAN, write_chan ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_SRAT, write_srat ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_PCMC, write_pcmC ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_CHNL, write_chnl ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_FTAB, write_ftab ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_STTS, write_stts ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_CTTS, write_ctts ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_CSLG, write_cslg ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_STSS, write_stss ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_STPS, write_stps ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_SDTP, write_sdtp ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_STSC, write_stsc ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_STSZ, write_stsz ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_STZ2, write_stz2 ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_STCO, write_stco ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_CO64, write_stco ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_SGPD, write_sgpd ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_SBGP, write_sbgp ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_UDTA, write_udta ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_HNTI, write_hnti ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_RTP,  write_rtp  ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_SDP,  write_sdp  ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_CHPL, write_chpl ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_MVEX, write_mvex ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_MEHD, write_mehd ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_TREX, write_trex ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_MOOF, write_moof ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_MFHD, write_mfhd ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_TRAF, write_traf ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_TFHD, write_tfhd ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_TFDT, write_tfdt ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_TRUN, write_trun ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_MDAT, write_mdat ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_FREE, write_free ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_SKIP, write_free ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_META, write_meta ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_META, write_meta ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_ILST, write_ilst ),
        ADD_BOX_WRITER_TABLE_ELEMENT(   QT_BOX_TYPE_ILST, write_ilst ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_MFRA, write_mfra ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_TFRA, write_tfra ),
        ADD_BOX_WRITER_TABLE_ELEMENT( ISOM_BOX_TYPE_MFRO, write_mfro ),
        ADD_BOX_WRITER_TABLE_ELEMENT( LSMASH_BOX_TYPE_UNSPECIFIED, NULL )
#undef ADD_BOX_WRITER_TABLE_ELEMENT
    };
    for( int i = 0; box_writer_table[i].writer_func; i++ )
        if( lsmash_check_box_type_identical( box->type, *box_writer_table[i].type ) )
        {
            box->write = box_writer_table[i].writer_func;
            return;
        }
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_ILST )
     || lsmash_check_box_type_identical( parent->type,   QT_BOX_TYPE_ILST ) )
    {
        box->write = write_metaitem;
        return;
    }
    if( lsmash_check_box_type_identical( parent->parent->type, ISOM_BOX_TYPE_ILST ) )
    {
             if( lsmash_check_box_type_identical( box->type, ISOM_BOX_TYPE_MEAN ) )
            box->write = write_mean;
        else if( lsmash_check_box_type_identical( box->type, ISOM_BOX_TYPE_NAME ) )
            box->write = write_name;
        else if( lsmash_check_box_type_identical( box->type, ISOM_BOX_TYPE_DATA ) )
            box->write = write_data;
        if( box->write )
            return;
    }
    else if( lsmash_check_box_type_identical( box->type, ISOM_BOX_TYPE_CPRT ) )
    {
        /* Avoid confusing udta.cprt with ilst.cprt. */
        box->write = write_cprt;
        return;
    }
    box->write = write_unknown_box;
}
