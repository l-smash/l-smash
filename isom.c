/*****************************************************************************
 * isom.c:
 *****************************************************************************
 * Copyright (C) 2010 L-SMASH project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 * Contributors: Takashi Hirata <silverfilain@gmail.com>
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
#include <ctype.h> /* for chapter handling */

#include "box.h"
#include "mp4a.h"
#include "mp4sys.h"


#define isom_create_box( box_name, parent_name, box_4cc ) \
    isom_##box_name##_t *(box_name) = malloc( sizeof(isom_##box_name##_t) ); \
    if( !box_name ) \
        return -1; \
    memset( box_name, 0, sizeof(isom_##box_name##_t) ); \
    isom_init_box_common( box_name, parent_name, box_4cc )

#define isom_create_list_box( box_name, parent_name, box_4cc ) \
    isom_create_box( box_name, parent_name, box_4cc ); \
    box_name->list = lsmash_create_entry_list(); \
    if( !box_name->list ) \
    { \
        free( box_name ); \
        return -1; \
    }

/*---- ----*/
/* Return 1 if the box is fullbox, Otherwise return 0. */
static int isom_is_fullbox( void *box )
{
    uint32_t type = ((isom_box_t *)box)->type;
    static const uint32_t fullbox_table[] = {
        ISOM_BOX_TYPE_MVHD,
        ISOM_BOX_TYPE_IODS,
        ISOM_BOX_TYPE_ESDS,
        ISOM_BOX_TYPE_TKHD,
        QT_BOX_TYPE_CLEF,
        QT_BOX_TYPE_PROF,
        QT_BOX_TYPE_ENOF,
        ISOM_BOX_TYPE_ELST,
        ISOM_BOX_TYPE_MDHD,
        ISOM_BOX_TYPE_HDLR,
        ISOM_BOX_TYPE_VMHD,
        ISOM_BOX_TYPE_SMHD,
        ISOM_BOX_TYPE_HMHD,
        ISOM_BOX_TYPE_NMHD,
        QT_BOX_TYPE_GMIN,
        ISOM_BOX_TYPE_DREF,
        ISOM_BOX_TYPE_URL ,
        ISOM_BOX_TYPE_STSD,
        ISOM_BOX_TYPE_STSL,
        QT_BOX_TYPE_CHAN,
        ISOM_BOX_TYPE_STTS,
        ISOM_BOX_TYPE_CTTS,
        ISOM_BOX_TYPE_CSLG,
        ISOM_BOX_TYPE_STSS,
        QT_BOX_TYPE_STPS,
        ISOM_BOX_TYPE_SDTP,
        ISOM_BOX_TYPE_STSC,
        ISOM_BOX_TYPE_STSZ,
        ISOM_BOX_TYPE_STCO,
        ISOM_BOX_TYPE_CO64,
        ISOM_BOX_TYPE_SGPD,
        ISOM_BOX_TYPE_SBGP,
        ISOM_BOX_TYPE_CHPL,
        ISOM_BOX_TYPE_MEHD,
        ISOM_BOX_TYPE_TREX,
        ISOM_BOX_TYPE_MFHD,
        ISOM_BOX_TYPE_TFHD,
        ISOM_BOX_TYPE_TRUN,
        ISOM_BOX_TYPE_TFRA,
        ISOM_BOX_TYPE_MFRO
    };
    for( int i = 0; i < sizeof(fullbox_table)/sizeof(uint32_t); i++ )
        if( type == fullbox_table[i] )
            return 1;
    return 0;
}

/* Return 1 if the sample type is LPCM audio, Otherwise return 0. */
static int isom_is_lpcm_audio( uint32_t type )
{
    return type == QT_CODEC_TYPE_23NI_AUDIO
        || type == QT_CODEC_TYPE_NONE_AUDIO
        || type == QT_CODEC_TYPE_LPCM_AUDIO
        || type == QT_CODEC_TYPE_RAW_AUDIO
        || type == QT_CODEC_TYPE_SOWT_AUDIO
        || type == QT_CODEC_TYPE_TWOS_AUDIO
        || type == QT_CODEC_TYPE_FL32_AUDIO
        || type == QT_CODEC_TYPE_FL64_AUDIO
        || type == QT_CODEC_TYPE_IN24_AUDIO
        || type == QT_CODEC_TYPE_IN32_AUDIO
        || type == QT_CODEC_TYPE_NOT_SPECIFIED;
}


static inline void isom_init_basebox_common( isom_box_t *box, isom_box_t *parent, uint32_t type )
{
    box->root     = parent->root;
    box->parent   = parent;
    box->size     = 0;
    box->type     = type;
    box->usertype = NULL;
}

static inline void isom_init_fullbox_common( isom_box_t *box, isom_box_t *parent, uint32_t type )
{
    box->root     = parent->root;
    box->parent   = parent;
    box->size     = 0;
    box->type     = type;
    box->usertype = NULL;
    box->version  = 0;
    box->flags    = 0;
}

static void isom_init_box_common( void *box, void *parent, uint32_t type )
{
    assert( parent && ((isom_box_t *)parent)->root );
    if( ((isom_box_t *)parent)->type == ISOM_BOX_TYPE_STSD )
    {
        isom_init_basebox_common( (isom_box_t *)box, (isom_box_t *)parent, type );
        return;
    }
    if( isom_is_fullbox( box ) )
        isom_init_fullbox_common( (isom_box_t *)box, (isom_box_t *)parent, type );
    else
        isom_init_basebox_common( (isom_box_t *)box, (isom_box_t *)parent, type );
}

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
        lsmash_bs_put_bytes( bs, box->usertype, 16 );
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

static isom_trak_entry_t *isom_get_trak( lsmash_root_t *root, uint32_t track_ID )
{
    if( !track_ID || !root || !root->moov || !root->moov->trak_list )
        return NULL;
    for( lsmash_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
        if( !trak || !trak->tkhd )
            return NULL;
        if( trak->tkhd->track_ID == track_ID )
            return trak;
    }
    return NULL;
}

static isom_trex_entry_t *isom_get_trex( isom_mvex_t *mvex, uint32_t track_ID )
{
    if( !track_ID || !mvex || !mvex->trex_list )
        return NULL;
    for( lsmash_entry_t *entry = mvex->trex_list->head; entry; entry = entry->next )
    {
        isom_trex_entry_t *trex = (isom_trex_entry_t *)entry->data;
        if( !trex )
            return NULL;
        if( trex->track_ID == track_ID )
            return trex;
    }
    return NULL;
}

static isom_traf_entry_t *isom_get_traf( isom_moof_entry_t *moof, uint32_t track_ID )
{
    if( !track_ID || !moof || !moof->traf_list )
        return NULL;
    for( lsmash_entry_t *entry = moof->traf_list->head; entry; entry = entry->next )
    {
        isom_traf_entry_t *traf = (isom_traf_entry_t *)entry->data;
        if( !traf || !traf->tfhd )
            return NULL;
        if( traf->tfhd->track_ID == track_ID )
            return traf;
    }
    return NULL;
}

static isom_tfra_entry_t *isom_get_tfra( isom_mfra_t *mfra, uint32_t track_ID )
{
    if( !track_ID || !mfra || !mfra->tfra_list )
        return NULL;
    for( lsmash_entry_t *entry = mfra->tfra_list->head; entry; entry = entry->next )
    {
        isom_tfra_entry_t *tfra = (isom_tfra_entry_t *)entry->data;
        if( !tfra )
            return NULL;
        if( tfra->track_ID == track_ID )
            return tfra;
    }
    return NULL;
}

static int isom_add_elst_entry( isom_elst_t *elst, uint64_t segment_duration, int64_t media_time, int32_t media_rate )
{
    isom_elst_entry_t *data = malloc( sizeof(isom_elst_entry_t) );
    if( !data )
        return -1;
    data->segment_duration = segment_duration;
    data->media_time = media_time;
    data->media_rate = media_rate;
    if( lsmash_add_entry( elst->list, data ) )
    {
        free( data );
        return -1;
    }
    if( data->segment_duration > UINT32_MAX || data->media_time > UINT32_MAX )
        elst->version = 1;
    return 0;
}

static isom_tref_type_t *isom_add_track_reference_type( isom_tref_t *tref, lsmash_track_reference_type_code type, uint32_t ref_count, uint32_t *track_ID )
{
    if( !tref || !tref->ref_list )
        return NULL;
    isom_tref_type_t *ref = malloc( sizeof(isom_tref_type_t) );
    if( !ref )
        return NULL;
    isom_init_box_common( ref, tref, type );
    ref->ref_count = ref_count;
    ref->track_ID = track_ID;
    if( lsmash_add_entry( tref->ref_list, ref ) )
    {
        free( ref );
        return NULL;
    }
    return ref;
}

static int isom_add_dref_entry( isom_dref_t *dref, uint32_t flags, char *name, char *location )
{
    if( !dref || !dref->list )
        return -1;
    isom_dref_entry_t *data = malloc( sizeof(isom_dref_entry_t) );
    if( !data )
        return -1;
    memset( data, 0, sizeof(isom_dref_entry_t) );
    isom_init_box_common( data, dref, name ? ISOM_BOX_TYPE_URN : ISOM_BOX_TYPE_URL );
    data->flags = flags;
    if( location )
    {
        data->location_length = strlen( location ) + 1;
        data->location = malloc( data->location_length );
        if( !data->location )
        {
            free( data );
            return -1;
        }
        memcpy( data->location, location, data->location_length );
    }
    if( name )
    {
        data->name_length = strlen( name ) + 1;
        data->name = malloc( data->name_length );
        if( !data->name )
        {
            if( data->location )
                free( data->location );
            free( data );
            return -1;
        }
        memcpy( data->name, name, data->name_length );
    }
    if( lsmash_add_entry( dref->list, data ) )
    {
        if( data->location )
            free( data->location );
        if( data->name )
            free( data->name );
        free( data );
        return -1;
    }
    return 0;
}

static isom_avcC_ps_entry_t *isom_create_ps_entry( uint8_t *ps, uint32_t ps_size )
{
    isom_avcC_ps_entry_t *entry = malloc( sizeof(isom_avcC_ps_entry_t) );
    if( !entry )
        return NULL;
    entry->parameterSetLength = ps_size;
    entry->parameterSetNALUnit = malloc( ps_size );
    if( !entry->parameterSetNALUnit )
    {
        free( entry );
        return NULL;
    }
    memcpy( entry->parameterSetNALUnit, ps, ps_size );
    return entry;
}

static void isom_remove_avcC_ps( isom_avcC_ps_entry_t *ps )
{
    if( !ps )
        return;
    if( ps->parameterSetNALUnit )
        free( ps->parameterSetNALUnit );
    free( ps );
}

int lsmash_add_sps_entry( lsmash_root_t *root, uint32_t track_ID, uint32_t entry_number, uint8_t *sps, uint32_t sps_size )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_avc_entry_t *data = (isom_avc_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_avcC_t *avcC = (isom_avcC_t *)data->avcC;
    if( !avcC )
        return -1;
    isom_avcC_ps_entry_t *ps = isom_create_ps_entry( sps, sps_size );
    if( !ps )
        return -1;
    if( lsmash_add_entry( avcC->sequenceParameterSets, ps ) )
    {
        isom_remove_avcC_ps( ps );
        return -1;
    }
    avcC->numOfSequenceParameterSets = avcC->sequenceParameterSets->entry_count;
    return 0;
}

int lsmash_add_pps_entry( lsmash_root_t *root, uint32_t track_ID, uint32_t entry_number, uint8_t *pps, uint32_t pps_size )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_avc_entry_t *data = (isom_avc_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_avcC_t *avcC = (isom_avcC_t *)data->avcC;
    if( !avcC )
        return -1;
    isom_avcC_ps_entry_t *ps = isom_create_ps_entry( pps, pps_size );
    if( !ps )
        return -1;
    if( lsmash_add_entry( avcC->pictureParameterSets, ps ) )
    {
        isom_remove_avcC_ps( ps );
        return -1;
    }
    avcC->numOfPictureParameterSets = avcC->pictureParameterSets->entry_count;
    return 0;
}

int lsmash_add_spsext_entry( lsmash_root_t *root, uint32_t track_ID, uint32_t entry_number, uint8_t *spsext, uint32_t spsext_size )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_avc_entry_t *data = (isom_avc_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_avcC_t *avcC = (isom_avcC_t *)data->avcC;
    if( !avcC )
        return -1;
    isom_avcC_ps_entry_t *ps = isom_create_ps_entry( spsext, spsext_size );
    if( !ps )
        return -1;
    if( lsmash_add_entry( avcC->sequenceParameterSetExt, ps ) )
    {
        isom_remove_avcC_ps( ps );
        return -1;
    }
    avcC->numOfSequenceParameterSetExt = avcC->sequenceParameterSetExt->entry_count;
    return 0;
}

static void isom_remove_avcC( isom_avcC_t *avcC )
{
    if( !avcC )
        return;
    lsmash_remove_list( avcC->sequenceParameterSets,   isom_remove_avcC_ps );
    lsmash_remove_list( avcC->pictureParameterSets,    isom_remove_avcC_ps );
    lsmash_remove_list( avcC->sequenceParameterSetExt, isom_remove_avcC_ps );
    free( avcC );
}

static int isom_add_avcC( lsmash_entry_list_t *list, void *parent )
{
    if( !list )
        return -1;
    isom_avc_entry_t *data = (isom_avc_entry_t *)lsmash_get_entry_data( list, list->entry_count );
    if( !data )
        return -1;
    isom_create_box( avcC, parent, ISOM_BOX_TYPE_AVCC );
    avcC->sequenceParameterSets = lsmash_create_entry_list();
    if( !avcC->sequenceParameterSets )
    {
        free( avcC );
        return -1;
    }
    avcC->pictureParameterSets = lsmash_create_entry_list();
    if( !avcC->pictureParameterSets )
    {
        isom_remove_avcC( avcC );
        return -1;
    }
    avcC->sequenceParameterSetExt = lsmash_create_entry_list();
    if( !avcC->sequenceParameterSetExt )
    {
        isom_remove_avcC( avcC );
        return -1;
    }
    data->avcC = avcC;
    return 0;
}

static int isom_add_pasp( isom_visual_entry_t *visual )
{
    if( !visual || visual->pasp )
        return -1;
    isom_create_box( pasp, visual, ISOM_BOX_TYPE_PASP );
    pasp->hSpacing = 1;
    pasp->vSpacing = 1;
    visual->pasp = pasp;
    return 0;
}

static int isom_add_clap( isom_visual_entry_t *visual )
{
    if( !visual || visual->clap )
        return -1;
    isom_create_box( clap, visual, ISOM_BOX_TYPE_CLAP );
    clap->cleanApertureWidthN = 1;
    clap->cleanApertureWidthD = 1;
    clap->cleanApertureHeightN = 1;
    clap->cleanApertureHeightD = 1;
    clap->horizOffN = 0;
    clap->horizOffD = 1;
    clap->vertOffN = 0;
    clap->vertOffD = 1;
    visual->clap = clap;
    return 0;
}

static int isom_add_colr( isom_visual_entry_t *visual )
{
    if( !visual || visual->colr )
        return -1;
    isom_create_box( colr, visual, QT_BOX_TYPE_COLR );
    isom_color_parameter_t *param = (isom_color_parameter_t *)(&isom_color_parameter_tbl[0]);
    colr->color_parameter_type = QT_COLOR_PARAMETER_TYPE_NCLC;
    colr->primaries_index = param->primaries;
    colr->transfer_function_index = param->transfer;
    colr->matrix_index = param->matrix;
    visual->colr = colr;
    return 0;
}

static int isom_add_stsl( isom_visual_entry_t *visual )
{
    if( !visual || visual->stsl )
        return -1;
    isom_create_box( stsl, visual, ISOM_BOX_TYPE_STSL );
    stsl->scale_method = ISOM_SCALING_METHOD_HIDDEN;
    visual->stsl = stsl;
    return 0;
}

static void isom_remove_visual_extensions( isom_visual_entry_t *visual )
{
    if( !visual )
        return;
    if( visual->clap )
        free( visual->clap );
    if( visual->pasp )
        free( visual->pasp );
    if( visual->colr )
        free( visual->colr );
    if( visual->stsl )
        free( visual->stsl );
}

static int isom_add_visual_extensions( isom_visual_entry_t *visual, lsmash_video_summary_t *summary )
{
    if( summary->crop_top || summary->crop_left || summary->crop_bottom || summary->crop_right )
    {
        if( isom_add_clap( visual ) )
        {
            isom_remove_visual_extensions( visual );
            return -1;
        }
        isom_clap_t *clap = visual->clap;
        clap->cleanApertureWidthN = summary->width - (summary->crop_left + summary->crop_right);
        clap->cleanApertureHeightN = summary->height - (summary->crop_top + summary->crop_bottom);
        clap->horizOffN = (int64_t)summary->crop_left - summary->crop_right;
        clap->vertOffN = (int64_t)summary->crop_top - summary->crop_bottom;
        if( !(clap->horizOffN & 0x1) )
        {
            clap->horizOffN /= 2;
            clap->horizOffD = 1;
        }
        else
            clap->horizOffD = 2;
        if( !(clap->vertOffN & 0x1) )
        {
            clap->vertOffN /= 2;
            clap->vertOffD = 1;
        }
        else
            clap->vertOffD = 2;
    }
    if( summary->par_h && summary->par_v )
    {
        if( isom_add_pasp( visual ) )
        {
            isom_remove_visual_extensions( visual );
            return -1;
        }
        isom_pasp_t *pasp = visual->pasp;
        pasp->hSpacing = summary->par_h;
        pasp->vSpacing = summary->par_v;
    }
    if( summary->primaries || summary->transfer || summary->matrix )
    {
        if( isom_add_colr( visual ) )
        {
            isom_remove_visual_extensions( visual );
            return -1;
        }
        isom_colr_t *colr = visual->colr;
        uint16_t primaries = summary->primaries;
        uint16_t transfer = summary->transfer;
        uint16_t matrix = summary->matrix;
        /* Set 'nclc' to parameter type, we don't support 'prof'. */
        colr->color_parameter_type = QT_COLOR_PARAMETER_TYPE_NCLC;
        /* primaries */
        if( primaries >= QT_COLOR_PARAMETER_END )
            return -1;
        else if( primaries > UINT16_MAX )
            colr->primaries_index = isom_color_parameter_tbl[primaries - UINT16_MAX_PLUS_ONE].primaries;
        else
            colr->primaries_index = (primaries == 1 || primaries == 5 || primaries == 6) ? primaries : 2;
        /* transfer */
        if( transfer >= QT_COLOR_PARAMETER_END )
            return -1;
        else if( transfer > UINT16_MAX )
            colr->transfer_function_index = isom_color_parameter_tbl[transfer - UINT16_MAX_PLUS_ONE].transfer;
        else
            colr->transfer_function_index = (transfer == 1 || transfer == 7) ? transfer : 2;
        /* matrix */
        if( matrix >= QT_COLOR_PARAMETER_END )
            return -1;
        else if( matrix > UINT16_MAX )
            colr->matrix_index = isom_color_parameter_tbl[matrix - UINT16_MAX_PLUS_ONE].matrix;
        else
            colr->matrix_index = (matrix == 1 || matrix == 6 || matrix == 7 ) ? matrix : 2;
    }
    if( summary->scaling_method )
    {
        if( isom_add_stsl( visual ) )
        {
            isom_remove_visual_extensions( visual );
            return -1;
        }
        isom_stsl_t *stsl = visual->stsl;
        stsl->constraint_flag = 1;
        stsl->scale_method = summary->scaling_method;
    }
    return 0;
}

static int isom_add_avc_entry( isom_stsd_t *stsd, uint32_t sample_type, lsmash_video_summary_t *summary )
{
    if( !stsd || !stsd->list || !summary )
        return -1;
    lsmash_entry_list_t *list = stsd->list;
    isom_avc_entry_t *avc = malloc( sizeof(isom_avc_entry_t) );
    if( !avc )
        return -1;
    memset( avc, 0, sizeof(isom_avc_entry_t) );
    isom_init_box_common( avc, stsd, sample_type );
    avc->data_reference_index = 1;
    avc->width = (uint16_t)summary->width;
    avc->height = (uint16_t)summary->height;
    avc->horizresolution = avc->vertresolution = 0x00480000;
    avc->frame_count = 1;
    switch( sample_type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
        case ISOM_CODEC_TYPE_AVC2_VIDEO :
            strcpy( avc->compressorname, "\012AVC Coding" );
            break;
        case ISOM_CODEC_TYPE_AVCP_VIDEO :
            strcpy( avc->compressorname, "\016AVC Parameters" );
            break;
        default :
            return -1;
    }
    avc->depth = 0x0018;
    avc->color_table_ID = -1;
    if( lsmash_add_entry( list, avc ) || isom_add_avcC( list, avc ) )
    {
        free( avc );
        return -1;
    }
    return isom_add_visual_extensions( (isom_visual_entry_t *)avc, summary );
}

#if 0
static int isom_add_mp4v_entry( isom_stsd_t *stsd, lsmash_video_summary_t *summary )
{
    if( !stsd || !stsd->list )
        return -1;
    isom_mp4v_entry_t *mp4v = malloc( sizeof(isom_visual_entry_t) );
    if( !mp4v )
        return -1;
    memset( mp4v, 0, sizeof(isom_mp4v_entry_t) );
    isom_init_box_common( mp4v, stsd, ISOM_CODEC_TYPE_MP4V_VIDEO );
    mp4v->data_reference_index = 1;
    mp4v->width = (uint16_t)summary->width;
    mp4v->height = (uint16_t)summary->height;
    mp4v->horizresolution = mp4v->vertresolution = 0x00480000;
    mp4v->frame_count = 1;
    mp4v->compressorname[32] = '\0';
    mp4v->depth = 0x0018;
    mp4v->color_table_ID = -1;
    if( lsmash_add_entry( stsd->list, mp4v ) )
    {
        free( mp4v );
        return -1;
    }
    return isom_add_visual_extensions( (isom_visual_entry_t *)mp4v, summary );
}

static int isom_add_mp4s_entry( isom_stsd_t *stsd )
{
    if( !stsd || !stsd->list )
        return -1;
    isom_mp4s_entry_t *mp4s = malloc( sizeof(isom_mp4s_entry_t) );
    if( !mp4s )
        return -1;
    memset( mp4s, 0, sizeof(isom_mp4s_entry_t) );
    isom_init_box_common( mp4s, stsd, ISOM_CODEC_TYPE_MP4S_SYSTEM );
    mp4s->data_reference_index = 1;
    if( lsmash_add_entry( stsd->list, mp4s ) )
    {
        free( mp4s );
        return -1;
    }
    return 0;
}

static int isom_add_visual_entry( isom_stsd_t *stsd, uint32_t sample_type, lsmash_video_summary_t *summary )
{
    if( !stsd || !stsd->list )
        return -1;
    isom_visual_entry_t *visual = malloc( sizeof(isom_visual_entry_t) );
    if( !visual )
        return -1;
    memset( visual, 0, sizeof(isom_visual_entry_t) );
    isom_init_box_common( visual, stsd, sample_type );
    visual->data_reference_index = 1;
    visual->width = (uint16_t)summary->width;
    visual->height = (uint16_t)summary->height;
    visual->horizresolution = visual->vertresolution = 0x00480000;
    visual->frame_count = 1;
    visual->compressorname[32] = '\0';
    visual->depth = 0x0018;
    visual->color_table_ID = -1;
    if( lsmash_add_entry( list, visual ) )
    {
        free( visual );
        return -1;
    }
    return isom_add_visual_extensions( visual, summary );;
}
#endif

static void isom_remove_esds( isom_esds_t *esds );
static void isom_remove_wave( isom_wave_t *wave );
static void isom_remove_chan( isom_chan_t *chan );

static int isom_add_wave( isom_audio_entry_t *audio )
{
    if( !audio || audio->wave )
        return -1;
    isom_create_box( wave, audio, QT_BOX_TYPE_WAVE );
    audio->wave = wave;
    return 0;
}

static int isom_add_frma( isom_wave_t *wave )
{
    if( !wave || wave->frma )
        return -1;
    isom_create_box( frma, wave, QT_BOX_TYPE_FRMA );
    wave->frma = frma;
    return 0;
}

static int isom_add_enda( isom_wave_t *wave )
{
    if( !wave || wave->enda )
        return -1;
    isom_create_box( enda, wave, QT_BOX_TYPE_ENDA );
    wave->enda = enda;
    return 0;
}

static int isom_add_mp4a( isom_wave_t *wave )
{
    if( !wave || wave->mp4a )
        return -1;
    isom_create_box( mp4a, wave, QT_BOX_TYPE_MP4A );
    wave->mp4a = mp4a;
    return 0;
}

static int isom_add_terminator( isom_wave_t *wave )
{
    if( !wave || wave->terminator )
        return -1;
    isom_create_box( terminator, wave, QT_BOX_TYPE_TERMINATOR );
    wave->terminator = terminator;
    return 0;
}

static int isom_add_chan( isom_audio_entry_t *audio )
{
    if( !audio || audio->chan )
        return -1;
    isom_create_box( chan, audio, QT_BOX_TYPE_CHAN );
    chan->channelLayoutTag = QT_CHANNEL_LAYOUT_UNKNOWN;
    audio->chan = chan;
    return 0;
}

static int isom_set_qtff_mp4a_description( isom_audio_entry_t *audio )
{
    lsmash_audio_summary_t *summary = &audio->summary;
    if( isom_add_wave( audio )
     || isom_add_frma( audio->wave )
     || isom_add_mp4a( audio->wave )
     || isom_add_terminator( audio->wave ) )
        return -1;
    audio->data_reference_index = 1;
    audio->version = (summary->channels > 2 || summary->frequency > UINT16_MAX) ? 2 : 1;
    audio->channelcount = audio->version == 2 ? 3 : LSMASH_MIN( summary->channels, 2 );
    audio->samplesize = 16;
    audio->compression_ID = QT_COMPRESSION_ID_VARIABLE_COMPRESSION;
    audio->packet_size = 0;
    if( audio->version == 1 )
    {
        audio->samplerate = summary->frequency << 16;
        audio->samplesPerPacket = summary->samples_in_frame;
        audio->bytesPerPacket = 1;      /* Apparently, this field is set to 1. */
        audio->bytesPerFrame = audio->bytesPerPacket * summary->channels;
        audio->bytesPerSample = 1 + (summary->bit_depth != 8);
    }
    else    /* audio->version == 2 */
    {
        audio->samplerate = 0x00010000;
        audio->sizeOfStructOnly = 72;
        audio->audioSampleRate = (union {double d; uint64_t i;}){summary->frequency}.i;
        audio->numAudioChannels = summary->channels;
        audio->always7F000000 = 0x7F000000;
        audio->constBitsPerChannel = 0;         /* compressed audio */
        audio->formatSpecificFlags = 0;
        audio->constBytesPerAudioPacket = 0;    /* variable */
        audio->constLPCMFramesPerAudioPacket = summary->samples_in_frame;
    }
    audio->wave->frma->data_format = audio->type;
    /* create ES Descriptor */
    isom_esds_t *esds = malloc( sizeof(isom_esds_t) );
    if( !esds )
        return -1;
    memset( esds, 0, sizeof(isom_esds_t) );
    isom_init_box_common( esds, audio->wave, ISOM_BOX_TYPE_ESDS );
    mp4sys_ES_Descriptor_params_t esd_param;
    memset( &esd_param, 0, sizeof(mp4sys_ES_Descriptor_params_t) );
    esd_param.objectTypeIndication = summary->object_type_indication;
    esd_param.streamType = summary->stream_type;
    esd_param.dsi_payload = summary->exdata;
    esd_param.dsi_payload_length = summary->exdata_length;
    esds->ES = mp4sys_setup_ES_Descriptor( &esd_param );
    if( !esds->ES )
        return -1;
    audio->wave->esds = esds;
    return 0;
}

static int isom_set_isom_mp4a_description( isom_audio_entry_t *audio )
{
    lsmash_audio_summary_t *summary = &audio->summary;
    if( summary->stream_type != MP4SYS_STREAM_TYPE_AudioStream )
        return -1;
    switch( summary->object_type_indication )
    {
        case MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3:
        case MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_Main_Profile:
        case MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_LC_Profile:
        case MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_SSR_Profile:
        case MP4SYS_OBJECT_TYPE_Audio_ISO_13818_3:      /* Legacy Interface */
        case MP4SYS_OBJECT_TYPE_Audio_ISO_11172_3:      /* Legacy Interface */
            break;
        default:
            return -1;
    }
    isom_create_box( esds, audio, ISOM_BOX_TYPE_ESDS );
    mp4sys_ES_Descriptor_params_t esd_param;
    esd_param.ES_ID = 0;              /* This is esds internal, so 0 is allowed. */
    esd_param.objectTypeIndication = summary->object_type_indication;
    esd_param.streamType = summary->stream_type;
    esd_param.bufferSizeDB = 0;       /* NOTE: ISO/IEC 14496-3 does not mention this, so we use 0. */
    esd_param.maxBitrate = 0;         /* This will be updated later if needed. or... I think this can be arbitrary value. */
    esd_param.avgBitrate = 0;         /* FIXME: 0 if VBR. */
    esd_param.dsi_payload = summary->exdata;
    esd_param.dsi_payload_length = summary->exdata_length;
    esds->ES = mp4sys_setup_ES_Descriptor( &esd_param );
    if( !esds->ES )
        return -1;
    audio->data_reference_index = 1;
    /* WARNING: This field cannot retain frequency above 65535Hz.
       This is not "FIXME", I just honestly implemented what the spec says.
       BTW, who ever expects sampling frequency takes fixed-point decimal??? */
    audio->samplerate = summary->frequency <= UINT16_MAX ? summary->frequency << 16 : 0;
    /* In pure mp4 file, these "template" fields shall be default values according to the spec.
       But not pure - hybrid with other spec - mp4 file can take other values.
       Which is to say, these template values shall be ignored in terms of mp4, except some object_type_indications.
       see 14496-14, "Template fields used". */
    audio->channelcount = 2;
    audio->samplesize = 16;
    audio->esds = esds;
    return 0;
}

static int isom_set_qtff_lpcm_description( isom_audio_entry_t *audio )
{
    uint32_t sample_type = audio->type;
    lsmash_audio_summary_t *summary = &audio->summary;
    /* Convert the sample type into 'lpcm' if the description doesn't match the format or version = 2 fields are needed. */
    if( (sample_type == QT_CODEC_TYPE_RAW_AUDIO && (summary->bit_depth != 8 || summary->sample_format))
     || (sample_type == QT_CODEC_TYPE_FL32_AUDIO && (summary->bit_depth != 32 || !summary->sample_format))
     || (sample_type == QT_CODEC_TYPE_FL64_AUDIO && (summary->bit_depth != 64 || !summary->sample_format))
     || (sample_type == QT_CODEC_TYPE_IN24_AUDIO && (summary->bit_depth != 24 || summary->sample_format))
     || (sample_type == QT_CODEC_TYPE_IN32_AUDIO && (summary->bit_depth != 32 || summary->sample_format))
     || (sample_type == QT_CODEC_TYPE_23NI_AUDIO && (summary->bit_depth != 32 || summary->sample_format || !summary->endianness))
     || (sample_type == QT_CODEC_TYPE_SOWT_AUDIO && (summary->bit_depth != 16 || summary->sample_format || !summary->endianness))
     || (sample_type == QT_CODEC_TYPE_TWOS_AUDIO && ((summary->bit_depth != 16 && summary->bit_depth != 8) || summary->sample_format || summary->endianness))
     || (sample_type == QT_CODEC_TYPE_NONE_AUDIO && ((summary->bit_depth != 16 && summary->bit_depth != 8) || summary->sample_format || summary->endianness))
     || (sample_type == QT_CODEC_TYPE_NOT_SPECIFIED && ((summary->bit_depth != 16 && summary->bit_depth != 8) || summary->sample_format || summary->endianness))
     || (summary->channels > 2 || summary->frequency > UINT16_MAX || summary->bit_depth % 8) )
    {
        audio->type = QT_CODEC_TYPE_LPCM_AUDIO;
        audio->version = 2;
    }
    else if( sample_type == QT_CODEC_TYPE_LPCM_AUDIO )
        audio->version = 2;
    else if( summary->bit_depth > 16
     || (sample_type != QT_CODEC_TYPE_RAW_AUDIO && sample_type != QT_CODEC_TYPE_TWOS_AUDIO
     && sample_type != QT_CODEC_TYPE_NONE_AUDIO && sample_type != QT_CODEC_TYPE_NOT_SPECIFIED) )
        audio->version = 1;
    audio->data_reference_index = 1;
    /* Set up constBytesPerAudioPacket field.
     * We use constBytesPerAudioPacket as the actual size of audio frame even when version is not 2. */
    audio->constBytesPerAudioPacket = (summary->bit_depth * summary->channels) / 8;
    /* Set up other fields in this description by its version. */
    if( audio->version == 2 )
    {
        audio->channelcount = 3;
        audio->samplesize = 16;
        audio->compression_ID = -2;
        audio->samplerate = 0x00010000;
        audio->sizeOfStructOnly = 72;
        audio->audioSampleRate = (union {double d; uint64_t i;}){summary->frequency}.i;
        audio->numAudioChannels = summary->channels;
        audio->always7F000000 = 0x7F000000;
        audio->constBitsPerChannel = summary->bit_depth;
        audio->constLPCMFramesPerAudioPacket = 1;
        if( summary->sample_format )
            audio->formatSpecificFlags |= QT_LPCM_FORMAT_FLAG_FLOAT;
        if( sample_type == QT_CODEC_TYPE_TWOS_AUDIO || !summary->endianness )
            audio->formatSpecificFlags |= QT_LPCM_FORMAT_FLAG_BIG_ENDIAN;
        if( !summary->sample_format && summary->signedness )
            audio->formatSpecificFlags |= QT_LPCM_FORMAT_FLAG_SIGNED_INTEGER;
        if( summary->packed )
            audio->formatSpecificFlags |= QT_LPCM_FORMAT_FLAG_PACKED;
        if( !summary->packed && summary->alignment )
            audio->formatSpecificFlags |= QT_LPCM_FORMAT_FLAG_ALIGNED_HIGH;
        if( !summary->interleaved )
            audio->formatSpecificFlags |= QT_LPCM_FORMAT_FLAG_NON_INTERLEAVED;
    }
    else if( audio->version == 1 )
    {
        audio->channelcount = summary->channels;
        audio->samplesize = 16;
        /* Audio formats other than 'raw ' and 'twos' are treated as compressed audio. */
        if( sample_type == QT_CODEC_TYPE_RAW_AUDIO || sample_type == QT_CODEC_TYPE_TWOS_AUDIO )
            audio->compression_ID = QT_COMPRESSION_ID_NOT_COMPRESSED;
        else
            audio->compression_ID = QT_COMPRESSION_ID_FIXED_COMPRESSION;
        audio->samplerate = summary->frequency << 16;
        audio->samplesPerPacket = 1;
        audio->bytesPerPacket = summary->bit_depth / 8;
        audio->bytesPerFrame = audio->bytesPerPacket * summary->channels;   /* sample_size field in stsz box is NOT used. */
        audio->bytesPerSample = 1 + (summary->bit_depth != 8);
        if( sample_type == QT_CODEC_TYPE_FL32_AUDIO || sample_type == QT_CODEC_TYPE_FL64_AUDIO
         || sample_type == QT_CODEC_TYPE_IN24_AUDIO || sample_type == QT_CODEC_TYPE_IN32_AUDIO )
        {
            if( isom_add_wave( audio )
             || isom_add_frma( audio->wave )
             || isom_add_enda( audio->wave )
             || isom_add_terminator( audio->wave ) )
                return -1;
            audio->wave->frma->data_format = sample_type;
            audio->wave->enda->littleEndian = summary->endianness;
        }
    }
    else    /* audio->version == 0 */
    {
        audio->channelcount = summary->channels;
        audio->samplesize = summary->bit_depth;
        audio->compression_ID = QT_COMPRESSION_ID_NOT_COMPRESSED;
        audio->samplerate = summary->frequency << 16;
    }
    return 0;
}

static int isom_set_extra_description( isom_audio_entry_t *audio )
{
    lsmash_audio_summary_t *summary = &audio->summary;
    audio->data_reference_index = 1;
    audio->samplerate = summary->frequency <= UINT16_MAX ? summary->frequency << 16 : 0;
    audio->channelcount = 2;
    audio->samplesize = 16;
    if( summary->exdata )
    {
        audio->exdata_length = summary->exdata_length;
        audio->exdata = malloc( audio->exdata_length );
        if( !audio->exdata )
            return -1;
        memcpy( audio->exdata, summary->exdata, audio->exdata_length );
    }
    else
        audio->exdata = NULL;
    return 0;
}

static int isom_add_audio_entry( isom_stsd_t *stsd, uint32_t sample_type, lsmash_audio_summary_t *summary )
{
    if( !stsd || !stsd->list || !summary )
        return -1;
    isom_audio_entry_t *audio = malloc( sizeof(isom_audio_entry_t) );
    if( !audio )
        return -1;
    memset( audio, 0, sizeof(isom_audio_entry_t) );
    isom_init_box_common( audio, stsd, sample_type );
    memcpy( &audio->summary, summary, sizeof(lsmash_audio_summary_t) );
    int ret = 0;
    lsmash_root_t *root = stsd->root;
    if( sample_type == ISOM_CODEC_TYPE_MP4A_AUDIO )
    {
        if( root->ftyp && root->ftyp->major_brand == ISOM_BRAND_TYPE_QT )
            ret = isom_set_qtff_mp4a_description( audio );
        else
            ret = isom_set_isom_mp4a_description( audio );
    }
    else if( isom_is_lpcm_audio( sample_type ) )
        ret = isom_set_qtff_lpcm_description( audio );
    else
        ret = isom_set_extra_description( audio );
    if( ret )
        goto fail;
    if( root->qt_compatible )
    {
        lsmash_channel_layout_tag_code layout_tag = summary->layout_tag;
        lsmash_channel_bitmap_code bitmap = summary->bitmap;
        if( layout_tag == QT_CHANNEL_LAYOUT_USE_CHANNEL_DESCRIPTIONS    /* We don't support the feature of Channel Descriptions. */
         || (layout_tag == QT_CHANNEL_LAYOUT_USE_CHANNEL_BITMAP && (!bitmap || bitmap > QT_CHANNEL_BIT_FULL)) )
        {
            layout_tag = summary->layout_tag = QT_CHANNEL_LAYOUT_UNKNOWN | summary->channels;
            bitmap = summary->bitmap = 0;
        }
        /* Don't create Channel Compositor Box if the channel layout is unknown. */
        if( (layout_tag ^ QT_CHANNEL_LAYOUT_UNKNOWN) >> 16 )
        {
            if( isom_add_chan( audio ) )
                goto fail;
            audio->chan->channelLayoutTag = layout_tag;
            audio->chan->channelBitmap = bitmap;
        }
    }
    if( lsmash_add_entry( stsd->list, audio ) )
        goto fail;
    return 0;
fail:
    isom_remove_esds( audio->esds );
    isom_remove_wave( audio->wave );
    isom_remove_chan( audio->chan );
    if( audio->exdata )
        free( audio->exdata );
    free( audio );
    return -1;
}

static int isom_add_text_entry( isom_stsd_t *stsd )
{
    if( !stsd || !stsd->list )
        return -1;
    isom_text_entry_t *text = malloc( sizeof(isom_text_entry_t) );
    if( !text )
        return -1;
    memset( text, 0, sizeof(isom_text_entry_t) );
    isom_init_box_common( text, stsd, QT_CODEC_TYPE_TEXT_TEXT );
    text->data_reference_index = 1;
    if( lsmash_add_entry( stsd->list, text ) )
    {
        free( text );
        return -1;
    }
    return 0;
}

static int isom_add_ftab( isom_tx3g_entry_t *tx3g )
{
    if( !tx3g )
        return -1;
    isom_ftab_t *ftab = malloc( sizeof(isom_ftab_t) );
    if( !ftab )
        return -1;
    memset( ftab, 0, sizeof(isom_ftab_t) );
    isom_init_box_common( ftab, tx3g, ISOM_BOX_TYPE_FTAB );
    ftab->list = lsmash_create_entry_list();
    if( !ftab->list )
    {
        free( ftab );
        return -1;
    }
    tx3g->ftab = ftab;
    return 0;
}

static int isom_add_tx3g_entry( isom_stsd_t *stsd )
{
    if( !stsd || !stsd->list )
        return -1;
    isom_tx3g_entry_t *tx3g = malloc( sizeof(isom_tx3g_entry_t) );
    if( !tx3g )
        return -1;
    memset( tx3g, 0, sizeof(isom_tx3g_entry_t) );
    isom_init_box_common( tx3g, stsd, ISOM_CODEC_TYPE_TX3G_TEXT );
    tx3g->data_reference_index = 1;
    if( isom_add_ftab( tx3g ) ||
        lsmash_add_entry( stsd->list, tx3g ) )
    {
        free( tx3g );
        return -1;
    }
    return 0;
}

/* This function returns 0 if failed, sample_entry_number if succeeded. */
int lsmash_add_sample_entry( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_type, void *summary )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->root || !trak->root->ftyp || !trak->mdia || !trak->mdia->minf
     || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return 0;
    isom_stsd_t *stsd = trak->mdia->minf->stbl->stsd;
    lsmash_entry_list_t *list = stsd->list;
    int ret = -1;
    switch( sample_type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
#if 0
        case ISOM_CODEC_TYPE_AVC2_VIDEO :
        case ISOM_CODEC_TYPE_AVCP_VIDEO :
        case ISOM_CODEC_TYPE_SVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC2_VIDEO :
#endif
            ret = isom_add_avc_entry( stsd, sample_type, (lsmash_video_summary_t *)summary );
            break;
#if 0
        case ISOM_CODEC_TYPE_MP4V_VIDEO :
            ret = isom_add_mp4v_entry( stsd, (lsmash_video_summary_t *)summary );
            break;
        case ISOM_CODEC_TYPE_MP4S_SYSTEM :
            ret = isom_add_mp4s_entry( stsd );
            break;
        case ISOM_CODEC_TYPE_DRAC_VIDEO :
        case ISOM_CODEC_TYPE_ENCV_VIDEO :
        case ISOM_CODEC_TYPE_MJP2_VIDEO :
        case ISOM_CODEC_TYPE_S263_VIDEO :
        case ISOM_CODEC_TYPE_VC_1_VIDEO :
            ret = isom_add_visual_entry( stsd, sample_type, (lsmash_video_summary_t *)summary );
            break;
#endif
        case ISOM_CODEC_TYPE_MP4A_AUDIO :
        case ISOM_CODEC_TYPE_AC_3_AUDIO :
        case ISOM_CODEC_TYPE_ALAC_AUDIO :
        case ISOM_CODEC_TYPE_SAMR_AUDIO :
        case ISOM_CODEC_TYPE_SAWB_AUDIO :
        case QT_CODEC_TYPE_23NI_AUDIO :
        case QT_CODEC_TYPE_NONE_AUDIO :
        case QT_CODEC_TYPE_LPCM_AUDIO :
        case QT_CODEC_TYPE_RAW_AUDIO :
        case QT_CODEC_TYPE_SOWT_AUDIO :
        case QT_CODEC_TYPE_TWOS_AUDIO :
        case QT_CODEC_TYPE_FL32_AUDIO :
        case QT_CODEC_TYPE_FL64_AUDIO :
        case QT_CODEC_TYPE_IN24_AUDIO :
        case QT_CODEC_TYPE_IN32_AUDIO :
        case QT_CODEC_TYPE_NOT_SPECIFIED :
#if 0
        case ISOM_CODEC_TYPE_DRA1_AUDIO :
        case ISOM_CODEC_TYPE_DTSC_AUDIO :
        case ISOM_CODEC_TYPE_DTSH_AUDIO :
        case ISOM_CODEC_TYPE_DTSL_AUDIO :
        case ISOM_CODEC_TYPE_EC_3_AUDIO :
        case ISOM_CODEC_TYPE_ENCA_AUDIO :
        case ISOM_CODEC_TYPE_G719_AUDIO :
        case ISOM_CODEC_TYPE_G726_AUDIO :
        case ISOM_CODEC_TYPE_M4AE_AUDIO :
        case ISOM_CODEC_TYPE_MLPA_AUDIO :
        case ISOM_CODEC_TYPE_RAW_AUDIO  :
        case ISOM_CODEC_TYPE_SAWP_AUDIO :
        case ISOM_CODEC_TYPE_SEVC_AUDIO :
        case ISOM_CODEC_TYPE_SQCP_AUDIO :
        case ISOM_CODEC_TYPE_SSMV_AUDIO :
        case ISOM_CODEC_TYPE_TWOS_AUDIO :
#endif
            ret = isom_add_audio_entry( stsd, sample_type, (lsmash_audio_summary_t *)summary );
            break;
        case ISOM_CODEC_TYPE_TX3G_TEXT :
            ret = isom_add_tx3g_entry( stsd );
            break;
        case QT_CODEC_TYPE_TEXT_TEXT :
            ret = isom_add_text_entry( stsd );
            break;
        default :
            return 0;
    }
    return ret ? 0 : list->entry_count;
}

static int isom_add_stts_entry( isom_stbl_t *stbl, uint32_t sample_delta )
{
    if( !stbl || !stbl->stts || !stbl->stts->list )
        return -1;
    isom_stts_entry_t *data = malloc( sizeof(isom_stts_entry_t) );
    if( !data )
        return -1;
    data->sample_count = 1;
    data->sample_delta = sample_delta;
    if( lsmash_add_entry( stbl->stts->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_add_ctts_entry( isom_stbl_t *stbl, uint32_t sample_offset )
{
    if( !stbl || !stbl->ctts || !stbl->ctts->list )
        return -1;
    isom_ctts_entry_t *data = malloc( sizeof(isom_ctts_entry_t) );
    if( !data )
        return -1;
    data->sample_count = 1;
    data->sample_offset = sample_offset;
    if( lsmash_add_entry( stbl->ctts->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_add_stsc_entry( isom_stbl_t *stbl, uint32_t first_chunk, uint32_t samples_per_chunk, uint32_t sample_description_index )
{
    if( !stbl || !stbl->stsc || !stbl->stsc->list )
        return -1;
    isom_stsc_entry_t *data = malloc( sizeof(isom_stsc_entry_t) );
    if( !data )
        return -1;
    data->first_chunk = first_chunk;
    data->samples_per_chunk = samples_per_chunk;
    data->sample_description_index = sample_description_index;
    if( lsmash_add_entry( stbl->stsc->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_add_stsz_entry( isom_stbl_t *stbl, uint32_t entry_size )
{
    if( !stbl || !stbl->stsz )
        return -1;
    isom_stsz_t *stsz = stbl->stsz;
    /* retrieve initial sample_size */
    if( !stsz->sample_count )
        stsz->sample_size = entry_size;
    /* if it seems constant access_unit size at present, update sample_count only */
    if( !stsz->list && stsz->sample_size == entry_size )
    {
        ++ stsz->sample_count;
        return 0;
    }
    /* found sample_size varies, create sample_size list */
    if( !stsz->list )
    {
        stsz->list = lsmash_create_entry_list();
        if( !stsz->list )
            return -1;
        for( uint32_t i = 0; i < stsz->sample_count; i++ )
        {
            isom_stsz_entry_t *data = malloc( sizeof(isom_stsz_entry_t) );
            if( !data )
                return -1;
            data->entry_size = stsz->sample_size;
            if( lsmash_add_entry( stsz->list, data ) )
            {
                free( data );
                return -1;
            }
        }
        stsz->sample_size = 0;
    }
    isom_stsz_entry_t *data = malloc( sizeof(isom_stsz_entry_t) );
    if( !data )
        return -1;
    data->entry_size = entry_size;
    if( lsmash_add_entry( stsz->list, data ) )
    {
        free( data );
        return -1;
    }
    ++ stsz->sample_count;
    return 0;
}

static int isom_add_stss_entry( isom_stbl_t *stbl, uint32_t sample_number )
{
    if( !stbl || !stbl->stss || !stbl->stss->list )
        return -1;
    isom_stss_entry_t *data = malloc( sizeof(isom_stss_entry_t) );
    if( !data )
        return -1;
    data->sample_number = sample_number;
    if( lsmash_add_entry( stbl->stss->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_add_stps_entry( isom_stbl_t *stbl, uint32_t sample_number )
{
    if( !stbl || !stbl->stps || !stbl->stps->list )
        return -1;
    isom_stps_entry_t *data = malloc( sizeof(isom_stps_entry_t) );
    if( !data )
        return -1;
    data->sample_number = sample_number;
    if( lsmash_add_entry( stbl->stps->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_add_sdtp_entry( isom_stbl_t *stbl, lsmash_sample_property_t *prop, uint8_t avc_extensions )
{
    if( !prop )
        return -1;
    if( !stbl || !stbl->sdtp || !stbl->sdtp->list )
        return -1;
    isom_sdtp_entry_t *data = malloc( sizeof(isom_sdtp_entry_t) );
    if( !data )
        return -1;
    /* isom_sdtp_entry_t is smaller than lsmash_sample_property_t. */
    data->is_leading = (avc_extensions ? prop->leading : prop->allow_earlier) & 0x03;
    data->sample_depends_on = prop->independent & 0x03;
    data->sample_is_depended_on = prop->disposable & 0x03;
    data->sample_has_redundancy = prop->redundant & 0x03;
    if( lsmash_add_entry( stbl->sdtp->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_add_co64( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stco )
        return -1;
    isom_create_list_box( stco, stbl, ISOM_BOX_TYPE_CO64 );
    stco->large_presentation = 1;
    stbl->stco = stco;
    return 0;
}

static int isom_add_stco( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stco )
        return -1;
    isom_create_list_box( stco, stbl, ISOM_BOX_TYPE_STCO );
    stco->large_presentation = 0;
    stbl->stco = stco;
    return 0;
}

static int isom_add_co64_entry( isom_stbl_t *stbl, uint64_t chunk_offset )
{
    if( !stbl || !stbl->stco || !stbl->stco->list )
        return -1;
    isom_co64_entry_t *data = malloc( sizeof(isom_co64_entry_t) );
    if( !data )
        return -1;
    data->chunk_offset = chunk_offset;
    if( lsmash_add_entry( stbl->stco->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_convert_stco_to_co64( isom_stbl_t* stbl )
{
    /* backup stco */
    isom_stco_t *stco = stbl->stco;
    stbl->stco = NULL;
    if( isom_add_co64( stbl ) )
        return -1;
    /* move chunk_offset to co64 from stco */
    for( lsmash_entry_t *entry = stco->list->head; entry; entry = entry->next )
    {
        isom_stco_entry_t *data = (isom_stco_entry_t*)entry->data;
        if( isom_add_co64_entry( stbl, data->chunk_offset ) )
            return -1;
    }
    lsmash_remove_list( stco->list, NULL );
    free( stco );
    return 0;
}

static int isom_add_stco_entry( isom_stbl_t *stbl, uint64_t chunk_offset )
{
    if( !stbl || !stbl->stco || !stbl->stco->list )
        return -1;
    if( stbl->stco->large_presentation )
        return isom_add_co64_entry( stbl, chunk_offset );
    if( chunk_offset > UINT32_MAX )
    {
        if( isom_convert_stco_to_co64( stbl ) )
            return -1;
        return isom_add_co64_entry( stbl, chunk_offset );
    }
    isom_stco_entry_t *data = malloc( sizeof(isom_stco_entry_t) );
    if( !data )
        return -1;
    data->chunk_offset = (uint32_t)chunk_offset;
    if( lsmash_add_entry( stbl->stco->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static isom_sgpd_entry_t *isom_get_sample_group_description( isom_stbl_t *stbl, uint32_t grouping_type )
{
    if( !stbl->sgpd_list )
        return NULL;
    for( lsmash_entry_t *entry = stbl->sgpd_list->head; entry; entry = entry->next )
    {
        isom_sgpd_entry_t *sgpd = (isom_sgpd_entry_t *)entry->data;
        if( !sgpd || !sgpd->list )
            return NULL;
        if( sgpd->grouping_type == grouping_type )
            return sgpd;
    }
    return NULL;
}

static isom_sbgp_entry_t *isom_get_sample_to_group( isom_stbl_t *stbl, uint32_t grouping_type )
{
    if( !stbl->sbgp_list )
        return NULL;
    for( lsmash_entry_t *entry = stbl->sbgp_list->head; entry; entry = entry->next )
    {
        isom_sbgp_entry_t *sbgp = (isom_sbgp_entry_t *)entry->data;
        if( !sbgp || !sbgp->list )
            return NULL;
        if( sbgp->grouping_type == grouping_type )
            return sbgp;
    }
    return NULL;
}

static isom_rap_entry_t *isom_add_rap_group_entry( isom_sgpd_entry_t *sgpd )
{
    if( !sgpd )
        return NULL;
    isom_rap_entry_t *data = malloc( sizeof(isom_rap_entry_t) );
     if( !data )
        return NULL;
    memset( data, 0, sizeof(isom_rap_entry_t) );
    if( lsmash_add_entry( sgpd->list, data ) )
    {
        free( data );
        return NULL;
    }
    return data;
}

static isom_roll_entry_t *isom_add_roll_group_entry( isom_sgpd_entry_t *sgpd, int16_t roll_distance )
{
    if( !sgpd )
        return NULL;
    isom_roll_entry_t *data = malloc( sizeof(isom_roll_entry_t) );
     if( !data )
        return NULL;
    data->description_length = 0;
    data->roll_distance = roll_distance;
    if( lsmash_add_entry( sgpd->list, data ) )
    {
        free( data );
        return NULL;
    }
    return data;
}

static isom_group_assignment_entry_t *isom_add_group_assignment_entry( isom_sbgp_entry_t *sbgp, uint32_t sample_count, uint32_t group_description_index )
{
    if( !sbgp )
        return NULL;
    isom_group_assignment_entry_t *data = malloc( sizeof(isom_group_assignment_entry_t) );
    if( !data )
        return NULL;
    data->sample_count = sample_count;
    data->group_description_index = group_description_index;
    if( lsmash_add_entry( sbgp->list, data ) )
    {
        free( data );
        return NULL;
    }
    return data;
}

static int isom_add_chpl_entry( isom_chpl_t *chpl, isom_chapter_entry_t *chap_data )
{
    if( !chap_data->chapter_name || !chpl || !chpl->list )
        return -1;
    isom_chpl_entry_t *data = malloc( sizeof(isom_chpl_entry_t) );
    if( !data )
        return -1;
    data->start_time = chap_data->start_time;
    data->chapter_name_length = strlen( chap_data->chapter_name );
    data->chapter_name = ( char* )malloc( data->chapter_name_length + 1 );
    if( !data->chapter_name )
    {
        free( data );
        return -1;
    }
    memcpy( data->chapter_name, chap_data->chapter_name, data->chapter_name_length );
    data->chapter_name[data->chapter_name_length] = '\0';
    if( lsmash_add_entry( chpl->list, data ) )
    {
        free( data->chapter_name );
        free( data );
        return -1;
    }
    return 0;
}

static isom_trex_entry_t *isom_add_trex( isom_mvex_t *mvex )
{
    if( !mvex )
        return NULL;
    if( !mvex->trex_list )
    {
        mvex->trex_list = lsmash_create_entry_list();
        if( !mvex->trex_list )
            return NULL;
    }
    isom_trex_entry_t *trex = malloc( sizeof(isom_trex_entry_t) );
    if( !trex )
        return NULL;
    memset( trex, 0, sizeof(isom_trex_entry_t) );
    isom_init_box_common( trex, mvex, ISOM_BOX_TYPE_TREX );
    if( lsmash_add_entry( mvex->trex_list, trex ) )
    {
        free( trex );
        return NULL;
    }
    return trex;
}

static isom_trun_entry_t *isom_add_trun( isom_traf_entry_t *traf )
{
    if( !traf )
        return NULL;
    if( !traf->trun_list )
    {
        traf->trun_list = lsmash_create_entry_list();
        if( !traf->trun_list )
            return NULL;
    }
    isom_trun_entry_t *trun = malloc( sizeof(isom_trun_entry_t) );
    if( !trun )
        return NULL;
    memset( trun, 0, sizeof(isom_trun_entry_t) );
    isom_init_box_common( trun, traf, ISOM_BOX_TYPE_TRUN );
    if( lsmash_add_entry( traf->trun_list, trun ) )
    {
        free( trun );
        return NULL;
    }
    return trun;
}

static isom_traf_entry_t *isom_add_traf( lsmash_root_t *root, isom_moof_entry_t *moof )
{
    if( !root || !root->moof_list || !moof )
        return NULL;
    if( !moof->traf_list )
    {
        moof->traf_list = lsmash_create_entry_list();
        if( !moof->traf_list )
            return NULL;
    }
    isom_traf_entry_t *traf = malloc( sizeof(isom_traf_entry_t) );
    if( !traf )
        return NULL;
    memset( traf, 0, sizeof(isom_traf_entry_t) );
    isom_init_box_common( traf, moof, ISOM_BOX_TYPE_TRAF );
    isom_cache_t *cache = malloc( sizeof(isom_cache_t) );
    if( !cache )
    {
        free( traf );
        return NULL;
    }
    memset( cache, 0, sizeof(isom_cache_t) );
    if( lsmash_add_entry( moof->traf_list, traf ) )
    {
        free( cache );
        free( traf );
        return NULL;
    }
    traf->cache = cache;
    return traf;
}

static isom_moof_entry_t *isom_add_moof( lsmash_root_t *root )
{
    if( !root )
        return NULL;
    if( !root->moof_list )
    {
        root->moof_list = lsmash_create_entry_list();
        if( !root->moof_list )
            return NULL;
    }
    isom_moof_entry_t *moof = malloc( sizeof(isom_moof_entry_t) );
    if( !moof )
        return NULL;
    memset( moof, 0, sizeof(isom_moof_entry_t) );
    isom_init_box_common( moof, root, ISOM_BOX_TYPE_MOOF );
    if( lsmash_add_entry( root->moof_list, moof ) )
    {
        free( moof );
        return NULL;
    }
    return moof;
}

static isom_tfra_entry_t *isom_add_tfra( isom_mfra_t *mfra )
{
    if( !mfra )
        return NULL;
    if( !mfra->tfra_list )
    {
        mfra->tfra_list = lsmash_create_entry_list();
        if( !mfra->tfra_list )
            return NULL;
    }
    isom_tfra_entry_t *tfra = malloc( sizeof(isom_tfra_entry_t) );
    if( !tfra )
        return NULL;
    memset( tfra, 0, sizeof(isom_tfra_entry_t) );
    isom_init_box_common( tfra, mfra, ISOM_BOX_TYPE_TFRA );
    if( lsmash_add_entry( mfra->tfra_list, tfra ) )
    {
        free( tfra );
        return NULL;
    }
    return tfra;
}

static int isom_add_ftyp( lsmash_root_t *root )
{
    if( root->ftyp )
        return -1;
    isom_create_box( ftyp, root, ISOM_BOX_TYPE_FTYP );
    ftyp->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 8;
    root->ftyp = ftyp;
    return 0;
}

static int isom_add_moov( lsmash_root_t *root )
{
    if( root->moov )
        return -1;
    isom_create_box( moov, root, ISOM_BOX_TYPE_MOOV );
    root->moov = moov;
    return 0;
}

static int isom_add_mvhd( isom_moov_t *moov )
{
    if( !moov || moov->mvhd )
        return -1;
    isom_create_box( mvhd, moov, ISOM_BOX_TYPE_MVHD );
    mvhd->rate = 0x00010000;
    mvhd->volume = 0x0100;
    mvhd->matrix[0] = 0x00010000;
    mvhd->matrix[4] = 0x00010000;
    mvhd->matrix[8] = 0x40000000;
    mvhd->next_track_ID = 1;
    moov->mvhd = mvhd;
    return 0;
}

static int isom_scan_trak_profileLevelIndication( isom_trak_entry_t* trak, mp4a_audioProfileLevelIndication* audio_pli, mp4sys_visualProfileLevelIndication* visual_pli )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    isom_stsd_t* stsd = trak->mdia->minf->stbl->stsd;
    if( !stsd || !stsd->list || !stsd->list->head )
        return -1;
    for( lsmash_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_sample_entry_t* sample_entry = (isom_sample_entry_t*)entry->data;
        if( !sample_entry )
            return -1;
        switch( sample_entry->type )
        {
            case ISOM_CODEC_TYPE_AVC1_VIDEO :
#if 0
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
            case ISOM_CODEC_TYPE_SVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC2_VIDEO :
#endif
                /* FIXME: Do we have to arbitrate like audio? */
                if( *visual_pli == MP4SYS_VISUAL_PLI_NONE_REQUIRED )
                    *visual_pli = MP4SYS_VISUAL_PLI_H264_AVC;
                break;
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
                *audio_pli = mp4a_max_audioProfileLevelIndication( *audio_pli, mp4a_get_audioProfileLevelIndication( &((isom_audio_entry_t*)sample_entry)->summary ) );
                break;
#if 0
            case ISOM_CODEC_TYPE_DRAC_VIDEO :
            case ISOM_CODEC_TYPE_ENCV_VIDEO :
            case ISOM_CODEC_TYPE_MJP2_VIDEO :
            case ISOM_CODEC_TYPE_S263_VIDEO :
            case ISOM_CODEC_TYPE_VC_1_VIDEO :
                /* FIXME: Do we have to arbitrate like audio? */
                if( *visual_pli == MP4SYS_VISUAL_PLI_NONE_REQUIRED )
                    *visual_pli = MP4SYS_VISUAL_PLI_NOT_SPECIFIED;
                break;
#endif
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
#if 0
            case ISOM_CODEC_TYPE_DRA1_AUDIO :
            case ISOM_CODEC_TYPE_DTSC_AUDIO :
            case ISOM_CODEC_TYPE_DTSH_AUDIO :
            case ISOM_CODEC_TYPE_DTSL_AUDIO :
            case ISOM_CODEC_TYPE_EC_3_AUDIO :
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
                /* NOTE: These audio codecs other than mp4a does not have appropriate pli. */
                *audio_pli = MP4A_AUDIO_PLI_NOT_SPECIFIED;
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
                /* FIXME: Do we have to set OD_profileLevelIndication? */
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
                /* FIXME: Do we have to set OD_profileLevelIndication? */
                break;
#endif
        }
    }
    return 0;
}

static int isom_add_iods( isom_moov_t *moov )
{
    if( !moov || !moov->trak_list || moov->iods )
        return -1;
    isom_create_box( iods, moov, ISOM_BOX_TYPE_IODS );
    iods->OD = mp4sys_create_ObjectDescriptor( 1 ); /* NOTE: Use 1 for ObjectDescriptorID of IOD. */
    if( !iods->OD )
    {
        free( iods );
        return -1;
    }
    mp4a_audioProfileLevelIndication audio_pli = MP4A_AUDIO_PLI_NONE_REQUIRED;
    mp4sys_visualProfileLevelIndication visual_pli = MP4SYS_VISUAL_PLI_NONE_REQUIRED;
    for( lsmash_entry_t *entry = moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_entry_t* trak = (isom_trak_entry_t*)entry->data;
        if( !trak || !trak->tkhd )
            return -1;
        if( isom_scan_trak_profileLevelIndication( trak, &audio_pli, &visual_pli ) )
            return -1;
        if( mp4sys_add_ES_ID_Inc( iods->OD, trak->tkhd->track_ID ) )
            return -1;
    }
    if( mp4sys_to_InitialObjectDescriptor( iods->OD,
                                           0, /* FIXME: I'm not quite sure what the spec says. */
                                           MP4SYS_OD_PLI_NONE_REQUIRED, MP4SYS_SCENE_PLI_NONE_REQUIRED,
                                           audio_pli, visual_pli,
                                           MP4SYS_GRAPHICS_PLI_NONE_REQUIRED ) )
    {
        free( iods );
        return -1;
    }
    moov->iods = iods;
    return 0;
}

static int isom_add_tkhd( isom_trak_entry_t *trak, uint32_t handler_type )
{
    if( !trak || !trak->root || !trak->root->moov || !trak->root->moov->mvhd || !trak->root->moov->trak_list )
        return -1;
    if( !trak->tkhd )
    {
        isom_create_box( tkhd, trak, ISOM_BOX_TYPE_TKHD );
        switch( handler_type )
        {
            case ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK :
                tkhd->matrix[0] = 0x00010000;
                tkhd->matrix[4] = 0x00010000;
                tkhd->matrix[8] = 0x40000000;
                break;
            case ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK :
                tkhd->volume = 0x0100;
                break;
            default :
                break;
        }
        tkhd->duration = 0xffffffff;
        tkhd->track_ID = trak->root->moov->mvhd->next_track_ID;
        ++ trak->root->moov->mvhd->next_track_ID;
        trak->tkhd = tkhd;
    }
    return 0;
}

static int isom_add_clef( isom_tapt_t *tapt )
{
    if( tapt->clef )
        return 0;
    isom_create_box( clef, tapt, QT_BOX_TYPE_CLEF );
    tapt->clef = clef;
    return 0;
}

static int isom_add_prof( isom_tapt_t *tapt )
{
    if( tapt->prof )
        return 0;
    isom_create_box( prof, tapt, QT_BOX_TYPE_PROF );
    tapt->prof = prof;
    return 0;
}

static int isom_add_enof( isom_tapt_t *tapt )
{
    if( tapt->enof )
        return 0;
    isom_create_box( enof, tapt, QT_BOX_TYPE_ENOF );
    tapt->enof = enof;
    return 0;
}

static int isom_add_tapt( isom_trak_entry_t *trak )
{
    if( trak->tapt )
        return 0;
    isom_create_box( tapt, trak, QT_BOX_TYPE_TAPT );
    trak->tapt = tapt;
    return 0;
}

static int isom_add_elst( isom_edts_t *edts )
{
    if( edts->elst )
        return 0;
    isom_create_list_box( elst, edts, ISOM_BOX_TYPE_ELST );
    edts->elst = elst;
    return 0;
}

static int isom_add_edts( isom_trak_entry_t *trak )
{
    if( trak->edts )
        return 0;
    isom_create_box( edts, trak, ISOM_BOX_TYPE_EDTS );
    trak->edts = edts;
    return 0;
}

static int isom_add_tref( isom_trak_entry_t *trak )
{
    if( trak->tref )
        return 0;
    isom_create_box( tref, trak, ISOM_BOX_TYPE_TREF );
    tref->ref_list = lsmash_create_entry_list();
    if( !tref->ref_list )
    {
        free( tref );
        return -1;
    }
    trak->tref = tref;
    return 0;
}

static int isom_add_mdhd( isom_mdia_t *mdia, uint16_t default_language )
{
    if( !mdia || mdia->mdhd )
        return -1;
    isom_create_box( mdhd, mdia, ISOM_BOX_TYPE_MDHD );
    mdhd->language = default_language;
    mdia->mdhd = mdhd;
    return 0;
}

static int isom_add_mdia( isom_trak_entry_t *trak )
{
    if( !trak || trak->mdia )
        return -1;
    isom_create_box( mdia, trak, ISOM_BOX_TYPE_MDIA );
    trak->mdia = mdia;
    return 0;
}

static int isom_add_hdlr( isom_mdia_t *mdia, isom_minf_t *minf, uint32_t media_type )
{
    if( (!mdia && !minf) || (mdia && minf) )
        return -1;    /* Either one must be given. */
    if( (mdia && mdia->hdlr) || (minf && minf->hdlr) )
        return -1;    /* Selected one must not have hdlr yet. */
    isom_box_t *parent = mdia ? (isom_box_t *)mdia : (isom_box_t *)minf;
    isom_create_box( hdlr, parent, ISOM_BOX_TYPE_HDLR );
    lsmash_root_t *root = hdlr->root;
    uint32_t type = mdia ? (root->qt_compatible ? QT_HANDLER_TYPE_MEDIA : 0) : QT_HANDLER_TYPE_DATA;
    uint32_t subtype = media_type;
    hdlr->componentType = type;
    hdlr->componentSubtype = subtype;
    char *type_name = NULL;
    char *subtype_name = NULL;
    uint8_t type_name_length = 0;
    uint8_t subtype_name_length = 0;
    switch( type )
    {
        case QT_HANDLER_TYPE_DATA :
            type_name = "Data ";
            type_name_length = 5;
            break;
        default :
            type_name = "Media ";
            type_name_length = 6;
            break;
    }
    switch( subtype )
    {
        case ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK :
            subtype_name = "Sound ";
            subtype_name_length = 6;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK :
            subtype_name = "Video ";
            subtype_name_length = 6;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_HINT_TRACK :
            subtype_name = "Hint ";
            subtype_name_length = 5;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_TIMED_METADATA_TRACK :
            subtype_name = "Meta ";
            subtype_name_length = 5;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_TEXT_TRACK :
            subtype_name = "Text ";
            subtype_name_length = 5;
            break;
        case QT_REFERENCE_HANDLER_TYPE_ALIAS :
            subtype_name = "Alias ";
            subtype_name_length = 6;
            break;
        case QT_REFERENCE_HANDLER_TYPE_RESOURCE :
            subtype_name = "Resource ";
            subtype_name_length = 9;
            break;
        case QT_REFERENCE_HANDLER_TYPE_URL :
            subtype_name = "URL ";
            subtype_name_length = 4;
            break;
        default :
            subtype_name = "Unknown ";
            subtype_name_length = 8;
            break;
    }
    uint32_t name_length = 15 + subtype_name_length + type_name_length + root->isom_compatible + root->qt_compatible;
    uint8_t *name = malloc( name_length );
    if( !name )
        return -1;
    if( root->qt_compatible )
        name[0] = name_length & 0xff;
    memcpy( name + root->qt_compatible, "L-SMASH ", 8 );
    memcpy( name + root->qt_compatible + 8, subtype_name, subtype_name_length );
    memcpy( name + root->qt_compatible + 8 + subtype_name_length, type_name, type_name_length );
    memcpy( name + root->qt_compatible + 8 + subtype_name_length + type_name_length, "Handler", 7 );
    if( root->isom_compatible )
        name[name_length - 1] = 0;
    hdlr->componentName = name;
    hdlr->componentName_length = name_length;
    if( mdia )
        mdia->hdlr = hdlr;
    else
        minf->hdlr = hdlr;
    return 0;
}

static int isom_add_minf( isom_mdia_t *mdia )
{
    if( !mdia || mdia->minf )
        return -1;
    isom_create_box( minf, mdia, ISOM_BOX_TYPE_MINF );
    mdia->minf = minf;
    return 0;
}

static int isom_add_vmhd( isom_minf_t *minf )
{
    if( !minf || minf->vmhd )
        return -1;
    isom_create_box( vmhd, minf, ISOM_BOX_TYPE_VMHD );
    vmhd->flags = 0x000001;
    minf->vmhd = vmhd;
    return 0;
}

static int isom_add_smhd( isom_minf_t *minf )
{
    if( !minf || minf->smhd )
        return -1;
    isom_create_box( smhd, minf, ISOM_BOX_TYPE_SMHD );
    minf->smhd = smhd;
    return 0;
}

static int isom_add_hmhd( isom_minf_t *minf )
{
    if( !minf || minf->hmhd )
        return -1;
    isom_create_box( hmhd, minf, ISOM_BOX_TYPE_HMHD );
    minf->hmhd = hmhd;
    return 0;
}

static int isom_add_nmhd( isom_minf_t *minf )
{
    if( !minf || minf->nmhd )
        return -1;
    isom_create_box( nmhd, minf, ISOM_BOX_TYPE_NMHD );
    minf->nmhd = nmhd;
    return 0;
}

static int isom_add_gmin( isom_gmhd_t *gmhd )
{
    if( !gmhd || gmhd->gmin )
        return -1;
    isom_create_box( gmin, gmhd, QT_BOX_TYPE_GMIN );
    gmhd->gmin = gmin;
    return 0;
}

static int isom_add_text( isom_gmhd_t *gmhd )
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

static int isom_add_gmhd( isom_minf_t *minf )
{
    if( !minf || minf->gmhd )
        return -1;
    isom_create_box( gmhd, minf, QT_BOX_TYPE_GMHD );
    minf->gmhd = gmhd;
    return 0;
}

static int isom_add_dinf( isom_minf_t *minf )
{
    if( !minf || minf->dinf )
        return -1;
    isom_create_box( dinf, minf, ISOM_BOX_TYPE_DINF );
    minf->dinf = dinf;
    return 0;
}

static int isom_add_dref( isom_dinf_t *dinf )
{
    if( !dinf || dinf->dref )
        return -1;
    isom_create_list_box( dref, dinf, ISOM_BOX_TYPE_DREF );
    dinf->dref = dref;
    if( isom_add_dref_entry( dref, 0x000001, NULL, NULL ) )
        return -1;
    return 0;
}

static int isom_add_stsd( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stsd )
        return -1;
    isom_create_list_box( stsd, stbl, ISOM_BOX_TYPE_STSD );
    stbl->stsd = stsd;
    return 0;
}

int lsmash_add_btrt( lsmash_root_t *root, uint32_t track_ID, uint32_t entry_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_avc_entry_t *data = lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_create_box( btrt, data, ISOM_BOX_TYPE_BTRT );
    data->btrt = btrt;
    return 0;
}

static int isom_add_stts( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stts )
        return -1;
    isom_create_list_box( stts, stbl, ISOM_BOX_TYPE_STTS );
    stbl->stts = stts;
    return 0;
}

static int isom_add_ctts( isom_stbl_t *stbl )
{
    if( !stbl || stbl->ctts )
        return -1;
    isom_create_list_box( ctts, stbl, ISOM_BOX_TYPE_CTTS );
    stbl->ctts = ctts;
    return 0;
}

static int isom_add_cslg( isom_stbl_t *stbl )
{
    if( !stbl || stbl->cslg )
        return -1;
    isom_create_box( cslg, stbl, ISOM_BOX_TYPE_CSLG );
    stbl->cslg = cslg;
    return 0;
}

static int isom_add_stsc( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stsc )
        return -1;
    isom_create_list_box( stsc, stbl, ISOM_BOX_TYPE_STSC );
    stbl->stsc = stsc;
    return 0;
}

static int isom_add_stsz( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stsz )
        return -1;
    isom_create_box( stsz, stbl, ISOM_BOX_TYPE_STSZ );  /* We don't create a list here. */
    stbl->stsz = stsz;
    return 0;
}

static int isom_add_stss( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stss )
        return -1;
    isom_create_list_box( stss, stbl, ISOM_BOX_TYPE_STSS );
    stbl->stss = stss;
    return 0;
}

static int isom_add_stps( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stps )
        return -1;
    isom_create_list_box( stps, stbl, QT_BOX_TYPE_STPS );
    stbl->stps = stps;
    return 0;
}

static int isom_add_sdtp( isom_stbl_t *stbl )
{
    if( !stbl || stbl->sdtp )
        return -1;
    isom_create_list_box( sdtp, stbl, ISOM_BOX_TYPE_SDTP );
    stbl->sdtp = sdtp;
    return 0;
}

static isom_sgpd_entry_t *isom_add_sgpd( isom_stbl_t *stbl, uint32_t grouping_type )
{
    if( !stbl )
        return NULL;
    if( !stbl->sgpd_list )
    {
        stbl->sgpd_list = lsmash_create_entry_list();
        if( !stbl->sgpd_list )
            return NULL;
    }
    isom_sgpd_entry_t *sgpd = malloc( sizeof(isom_sgpd_entry_t) );
    if( !sgpd )
        return NULL;
    memset( sgpd, 0, sizeof(isom_sgpd_entry_t) );
    isom_init_box_common( sgpd, stbl, ISOM_BOX_TYPE_SGPD );
    sgpd->list = lsmash_create_entry_list();
    if( !sgpd->list || lsmash_add_entry( stbl->sgpd_list, sgpd ) )
    {
        free( sgpd );
        return NULL;
    }
    sgpd->grouping_type = grouping_type;
    sgpd->version = 1;      /* We use version 1 because it is recommended in the spec. */
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

static isom_sbgp_entry_t *isom_add_sbgp( isom_stbl_t *stbl, uint32_t grouping_type )
{
    if( !stbl )
        return NULL;
    if( !stbl->sbgp_list )
    {
        stbl->sbgp_list = lsmash_create_entry_list();
        if( !stbl->sbgp_list )
            return NULL;
    }
    isom_sbgp_entry_t *sbgp = malloc( sizeof(isom_sbgp_entry_t) );
    if( !sbgp )
        return NULL;
    memset( sbgp, 0, sizeof(isom_sbgp_entry_t) );
    isom_init_box_common( sbgp, stbl, ISOM_BOX_TYPE_SBGP );
    sbgp->list = lsmash_create_entry_list();
    if( !sbgp->list || lsmash_add_entry( stbl->sbgp_list, sbgp ) )
    {
        free( sbgp );
        return NULL;
    }
    sbgp->grouping_type = grouping_type;
    return sbgp;
}

static int isom_add_stbl( isom_minf_t *minf )
{
    if( !minf || minf->stbl )
        return -1;
    isom_create_box( stbl, minf, ISOM_BOX_TYPE_STBL );
    minf->stbl = stbl;
    return 0;
}

static int isom_add_chpl( isom_moov_t *moov )
{
    if( !moov || !moov->udta || moov->udta->chpl )
        return -1;
    isom_create_list_box( chpl, moov, ISOM_BOX_TYPE_CHPL );
    chpl->version = 1;      /* version = 1 is popular. */
    moov->udta->chpl = chpl;
    return 0;
}

static int isom_add_udta( lsmash_root_t *root, uint32_t track_ID )
{
    /* track_ID == 0 means the direct addition to moov box */
    if( !track_ID )
    {
        if( !root || !root->moov )
            return -1;
        if( root->moov->udta )
            return 0;
        isom_create_box( udta, root->moov, ISOM_BOX_TYPE_UDTA );
        root->moov->udta = udta;
        return 0;
    }
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak )
        return -1;
    if( trak->udta )
        return 0;
    isom_create_box( udta, trak, ISOM_BOX_TYPE_UDTA );
    trak->udta = udta;
    return 0;
}

static isom_trak_entry_t *isom_add_trak( lsmash_root_t *root )
{
    if( !root || !root->moov )
        return NULL;
    isom_moov_t *moov = root->moov;
    if( !moov->trak_list )
    {
        moov->trak_list = lsmash_create_entry_list();
        if( !moov->trak_list )
            return NULL;
    }
    isom_trak_entry_t *trak = malloc( sizeof(isom_trak_entry_t) );
    if( !trak )
        return NULL;
    memset( trak, 0, sizeof(isom_trak_entry_t) );
    isom_init_box_common( trak, moov, ISOM_BOX_TYPE_TRAK );
    isom_cache_t *cache = malloc( sizeof(isom_cache_t) );
    if( !cache )
    {
        free( trak );
        return NULL;
    }
    memset( cache, 0, sizeof(isom_cache_t) );
    isom_fragment_t *fragment = NULL;
    if( root->fragment )
    {
        fragment = malloc( sizeof(isom_fragment_t) );
        if( !fragment )
        {
            free( cache );
            free( trak );
            return NULL;
        }
        memset( fragment, 0, sizeof(isom_fragment_t) );
        cache->fragment = fragment;
    }
    if( lsmash_add_entry( moov->trak_list, trak ) )
    {
        if( fragment )
            free( fragment );
        free( cache );
        free( trak );
        return NULL;
    }
    trak->cache = cache;
    return trak;
}

static int isom_add_mvex( isom_moov_t *moov )
{
    if( !moov || moov->mvex )
        return -1;
    isom_create_box( mvex, moov, ISOM_BOX_TYPE_MVEX );
    moov->mvex = mvex;
    return 0;
}

static int isom_add_mehd( isom_mvex_t *mvex )
{
    if( !mvex || mvex->mehd )
        return -1;
    isom_create_box( mehd, mvex, ISOM_BOX_TYPE_MEHD );
    mvex->mehd = mehd;
    return 0;
}

static int isom_add_tfhd( isom_traf_entry_t *traf )
{
    if( !traf || traf->tfhd )
        return -1;
    isom_create_box( tfhd, traf, ISOM_BOX_TYPE_TFHD );
    traf->tfhd = tfhd;
    return 0;
}

static int isom_add_mfhd( isom_moof_entry_t *moof )
{
    if( !moof || moof->mfhd )
        return -1;
    isom_create_box( mfhd, moof, ISOM_BOX_TYPE_MFHD );
    moof->mfhd = mfhd;
    return 0;
}

static int isom_add_mfra( lsmash_root_t *root )
{
    if( !root || root->mfra )
        return -1;
    isom_create_box( mfra, root, ISOM_BOX_TYPE_MFRA );
    root->mfra = mfra;
    return 0;
}

static int isom_add_mfro( isom_mfra_t *mfra )
{
    if( !mfra || mfra->mfro )
        return -1;
    isom_create_box( mfro, mfra, ISOM_BOX_TYPE_MFRO );
    mfra->mfro = mfro;
    return 0;
}

#define isom_remove_fullbox( box_type, container ) \
    isom_##box_type##_t *box_type = container->box_type; \
    if( box_type ) \
        free( box_type )

#define isom_remove_list_fullbox( box_type, container ) \
    isom_##box_type##_t *box_type = container->box_type; \
    if( box_type ) \
    { \
        lsmash_remove_list( box_type->list, NULL ); \
        free( box_type ); \
    }

static void isom_remove_ftyp( isom_ftyp_t *ftyp )
{
    if( !ftyp )
        return;
    if( ftyp->compatible_brands )
        free( ftyp->compatible_brands );
    free( ftyp );
}

static void isom_remove_tapt( isom_tapt_t *tapt )
{
    if( !tapt )
        return;
    isom_remove_fullbox( clef, tapt );
    isom_remove_fullbox( prof, tapt );
    isom_remove_fullbox( enof, tapt );
    free( tapt );
}

static void isom_remove_edts( isom_edts_t *edts )
{
    if( !edts )
        return;
    isom_remove_list_fullbox( elst, edts );
    free( edts );
}

static void isom_remove_track_reference_type( isom_tref_type_t *ref )
{
    if( !ref )
        return;
    if( ref->track_ID )
        free( ref->track_ID );
    free( ref );
}

static void isom_remove_tref( isom_tref_t *tref )
{
    if( !tref )
        return;
    lsmash_remove_list( tref->ref_list, isom_remove_track_reference_type );
    free( tref );
}

static void isom_remove_esds( isom_esds_t *esds )
{
    if( !esds )
        return;
    mp4sys_remove_ES_Descriptor( esds->ES );
    free( esds );
}

static void isom_remove_font_record( isom_font_record_t *font_record )
{
    if( !font_record )
        return;
    if( font_record->font_name )
        free( font_record->font_name );
    free( font_record );
}

static void isom_remove_ftab( isom_ftab_t *ftab )
{
    if( !ftab )
        return;
    lsmash_remove_list( ftab->list, isom_remove_font_record );
    free( ftab );
}

static void isom_remove_wave( isom_wave_t *wave )
{
    if( !wave )
        return;
    free( wave->frma );
    free( wave->enda );
    free( wave->mp4a );
    isom_remove_esds( wave->esds );
    free( wave->terminator );
    free( wave );
}

static void isom_remove_chan( isom_chan_t *chan )
{
    if( !chan )
        return;
    if( chan->channelDescriptions )
        free( chan->channelDescriptions );
    free( chan );
}

static void isom_remove_stsd( isom_stsd_t *stsd )
{
    if( !stsd )
        return;
    if( !stsd->list )
    {
        free( stsd );
        return;
    }
    for( lsmash_entry_t *entry = stsd->list->head; entry; )
    {
        isom_sample_entry_t *sample = (isom_sample_entry_t *)entry->data;
        if( !sample )
        {
            lsmash_entry_t *next = entry->next;
            free( entry );
            entry = next;
            continue;
        }
        switch( sample->type )
        {
            case ISOM_CODEC_TYPE_AVC1_VIDEO :
#if 0
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
            case ISOM_CODEC_TYPE_SVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC2_VIDEO :
#endif
            {
                isom_avc_entry_t *avc = (isom_avc_entry_t *)entry->data;
                isom_remove_visual_extensions( (isom_visual_entry_t *)avc );
                if( avc->avcC )
                    isom_remove_avcC( avc->avcC );
                if( avc->btrt )
                    free( avc->btrt );
                free( avc );
                break;
            }
#if 0
            case ISOM_CODEC_TYPE_MP4V_VIDEO :
            {
                isom_mp4v_entry_t *mp4v = (isom_mp4v_entry_t *)entry->data;
                isom_remove_visual_extensions( (isom_visual_entry_t *)mp4v );
                isom_remove_esds( mp4v->esds );
                free( mp4v );
                break;
            }
            case ISOM_CODEC_TYPE_MP4S_SYSTEM :
            {
                isom_mp4s_entry_t *mp4s = (isom_mp4s_entry_t *)entry->data;
                isom_remove_esds( mp4s->esds );
                free( mp4s );
                break;
            }
            case ISOM_CODEC_TYPE_DRAC_VIDEO :
            case ISOM_CODEC_TYPE_ENCV_VIDEO :
            case ISOM_CODEC_TYPE_MJP2_VIDEO :
            case ISOM_CODEC_TYPE_S263_VIDEO :
            case ISOM_CODEC_TYPE_VC_1_VIDEO :
            {
                isom_visual_entry_t *visual = (isom_visual_entry_t *)entry->data;
                isom_remove_visual_extensions( visual );
                free( visual );
                break;
            }
#endif
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
            case QT_CODEC_TYPE_23NI_AUDIO :
            case QT_CODEC_TYPE_NONE_AUDIO :
            case QT_CODEC_TYPE_LPCM_AUDIO :
            case QT_CODEC_TYPE_RAW_AUDIO :
            case QT_CODEC_TYPE_SOWT_AUDIO :
            case QT_CODEC_TYPE_TWOS_AUDIO :
            case QT_CODEC_TYPE_FL32_AUDIO :
            case QT_CODEC_TYPE_FL64_AUDIO :
            case QT_CODEC_TYPE_IN24_AUDIO :
            case QT_CODEC_TYPE_IN32_AUDIO :
            case QT_CODEC_TYPE_NOT_SPECIFIED :
#if 0
            case ISOM_CODEC_TYPE_DRA1_AUDIO :
            case ISOM_CODEC_TYPE_DTSC_AUDIO :
            case ISOM_CODEC_TYPE_DTSH_AUDIO :
            case ISOM_CODEC_TYPE_DTSL_AUDIO :
            case ISOM_CODEC_TYPE_EC_3_AUDIO :
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
            {
                isom_audio_entry_t *audio = (isom_audio_entry_t *)entry->data;
                isom_remove_esds( audio->esds );
                isom_remove_wave( audio->wave );
                isom_remove_chan( audio->chan );
                if( audio->exdata )
                    free( audio->exdata );
                free( audio );
                break;
            }
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
            {
                isom_hint_entry_t *hint = (isom_hint_entry_t *)entry->data;
                if( hint->data )
                    free( hint->data );
                free( hint );
                break;
            }
            case ISOM_CODEC_TYPE_IXSE_META :
            case ISOM_CODEC_TYPE_METT_META :
            case ISOM_CODEC_TYPE_METX_META :
            case ISOM_CODEC_TYPE_MLIX_META :
            case ISOM_CODEC_TYPE_OKSD_META :
            case ISOM_CODEC_TYPE_SVCM_META :
            case ISOM_CODEC_TYPE_TEXT_META :
            case ISOM_CODEC_TYPE_URIM_META :
            case ISOM_CODEC_TYPE_XML_META  :
            {
                isom_metadata_entry_t *metadata = (isom_metadata_entry_t *)entry->data;
                free( metadata );
                break;
            }
#endif
            case ISOM_CODEC_TYPE_TX3G_TEXT :
            {
                isom_tx3g_entry_t *tx3g = (isom_tx3g_entry_t *)entry->data;
                if( tx3g->ftab )
                    isom_remove_ftab( tx3g->ftab );
                free( tx3g );
                break;
            }
            case QT_CODEC_TYPE_TEXT_TEXT :
            {
                isom_text_entry_t *text = (isom_text_entry_t *)entry->data;
                if( text->font_name )
                    free( text->font_name );
                free( text );
                break;
            }
            default :
                break;
        }
        lsmash_entry_t *next = entry->next;
        free( entry );
        entry = next;
    }
    free( stsd->list );
    free( stsd );
}

static void isom_remove_sgpd( isom_sgpd_entry_t *sgpd )
{
    if( !sgpd )
        return;
    lsmash_remove_list( sgpd->list, NULL );
    free( sgpd );
}

static void isom_remove_sbgp( isom_sbgp_entry_t *sbgp )
{
    if( !sbgp )
        return;
    lsmash_remove_list( sbgp->list, NULL );
    free( sbgp );
}

static void isom_remove_stbl( isom_stbl_t *stbl )
{
    if( !stbl )
        return;
    isom_remove_stsd( stbl->stsd );
    isom_remove_fullbox( cslg, stbl );
    isom_remove_list_fullbox( stts, stbl );
    isom_remove_list_fullbox( ctts, stbl );
    isom_remove_list_fullbox( stsc, stbl );
    isom_remove_list_fullbox( stsz, stbl );
    isom_remove_list_fullbox( stss, stbl );
    isom_remove_list_fullbox( stps, stbl );
    isom_remove_list_fullbox( sdtp, stbl );
    isom_remove_list_fullbox( stco, stbl );
    lsmash_remove_list( stbl->sgpd_list, isom_remove_sgpd );
    lsmash_remove_list( stbl->sbgp_list, isom_remove_sbgp );
    free( stbl );
}

static void isom_remove_dref( isom_dref_t *dref )
{
    if( !dref )
        return;
    if( !dref->list )
    {
        free( dref );
        return;
    }
    for( lsmash_entry_t *entry = dref->list->head; entry; )
    {
        isom_dref_entry_t *data = (isom_dref_entry_t *)entry->data;
        if( data )
        {
            if( data->name )
                free( data->name );
            if( data->location )
                free( data->location );
            free( data );
        }
        lsmash_entry_t *next = entry->next;
        free( entry );
        entry = next;
    }
    free( dref->list );
    free( dref );
}

static void isom_remove_dinf( isom_dinf_t *dinf )
{
    if( !dinf )
        return;
    isom_remove_dref( dinf->dref );
    free( dinf );
}

static void isom_remove_hdlr( isom_hdlr_t *hdlr )
{
    if( !hdlr )
        return;
    if( hdlr->componentName )
        free( hdlr->componentName );
    free( hdlr );
}

static void isom_remove_gmhd( isom_gmhd_t *gmhd )
{
    if( !gmhd )
        return;
    isom_remove_fullbox( gmin, gmhd );
    if( gmhd->text )
        free( gmhd->text );
    free( gmhd );
}

static void isom_remove_minf( isom_minf_t *minf )
{
    if( !minf )
        return;
    isom_remove_fullbox( vmhd, minf );
    isom_remove_fullbox( smhd, minf );
    isom_remove_fullbox( hmhd, minf );
    isom_remove_fullbox( nmhd, minf );
    isom_remove_gmhd( minf->gmhd );
    isom_remove_hdlr( minf->hdlr );
    isom_remove_dinf( minf->dinf );
    isom_remove_stbl( minf->stbl );
    free( minf );
}

static void isom_remove_mdia( isom_mdia_t *mdia )
{
    if( !mdia )
        return;
    isom_remove_fullbox( mdhd, mdia );
    isom_remove_minf( mdia->minf );
    isom_remove_hdlr( mdia->hdlr );
    free( mdia );
}

static void isom_remove_chpl( isom_chpl_t *chpl )
{
    if( !chpl )
        return;
    if( !chpl->list )
    {
        free( chpl );
        return;
    }
    for( lsmash_entry_t *entry = chpl->list->head; entry; )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        if( data )
        {
            if( data->chapter_name )
                free( data->chapter_name );
            free( data );
        }
        lsmash_entry_t *next = entry->next;
        free( entry );
        entry = next;
    }
    free( chpl->list );
    free( chpl );
}

static void isom_remove_udta( isom_udta_t *udta )
{
    if( !udta )
        return;
    isom_remove_chpl( udta->chpl );
    free( udta );
}

static void isom_remove_trak( isom_trak_entry_t *trak )
{
    if( !trak )
        return;
    isom_remove_fullbox( tkhd, trak );
    isom_remove_tapt( trak->tapt );
    isom_remove_edts( trak->edts );
    isom_remove_tref( trak->tref );
    isom_remove_mdia( trak->mdia );
    isom_remove_udta( trak->udta );
    if( trak->cache )
    {
        lsmash_remove_list( trak->cache->chunk.pool, lsmash_delete_sample );
        lsmash_remove_list( trak->cache->roll.pool, NULL );
        if( trak->cache->rap )
            free( trak->cache->rap );
        free( trak->cache );
    }
    free( trak );   /* Note: the list that contains this trak still has the address of the entry. */
}

static void isom_remove_iods( isom_iods_t *iods )
{
    if( !iods )
        return;
    mp4sys_remove_ObjectDescriptor( iods->OD );
    free( iods );
}

static void isom_remove_mvex( isom_mvex_t *mvex )
{
    if( !mvex )
        return;
    isom_remove_fullbox( mehd, mvex );
    lsmash_remove_list( mvex->trex_list, NULL );
    free( mvex );
}

static void isom_remove_moov( lsmash_root_t *root )
{
    if( !root || !root->moov )
        return;
    isom_moov_t *moov = root->moov;
    if( moov->mvhd )
        free( moov->mvhd );
    isom_remove_iods( moov->iods );
    isom_remove_udta( moov->udta );
    lsmash_remove_list( moov->trak_list, isom_remove_trak );
    isom_remove_mvex( moov->mvex );
    free( moov );
    root->moov = NULL;
}

static void isom_remove_trun( isom_trun_entry_t *trun )
{
    if( !trun )
        return;
    lsmash_remove_list( trun->optional, NULL );
    free( trun );   /* Note: the list that contains this trun still has the address of the entry. */
}

static void isom_remove_traf( isom_traf_entry_t *traf )
{
    if( !traf )
        return;
    isom_remove_fullbox( tfhd, traf );
    lsmash_remove_list( traf->trun_list, isom_remove_trun );
    free( traf );   /* Note: the list that contains this traf still has the address of the entry. */
}

static void isom_remove_moof( isom_moof_entry_t *moof )
{
    if( !moof )
        return;
    isom_remove_fullbox( mfhd, moof );
    lsmash_remove_list( moof->traf_list, isom_remove_traf );
    free( moof );
}

static void isom_remove_mdat( isom_mdat_t *mdat )
{
    if( mdat )
        free( mdat );
}

static void isom_remove_free( isom_free_t *skip )
{
    if( skip )
    {
        if( skip->data )
            free( skip->data );
        free( skip );
    }
}

static void isom_remove_tfra( isom_tfra_entry_t *tfra )
{
    if( !tfra )
        return;
    lsmash_remove_list( tfra->list, NULL );
    free( tfra );
}

static void isom_remove_mfra( isom_mfra_t *mfra )
{
    if( !mfra )
        return;
    lsmash_remove_list( mfra->tfra_list, isom_remove_tfra );
    isom_remove_fullbox( mfro, mfra );
    free( mfra );
}

/* Box writers */
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

static int isom_write_hdlr( lsmash_bs_t *bs, isom_trak_entry_t *trak, uint8_t is_media_handler )
{
    isom_hdlr_t *hdlr = is_media_handler ? trak->mdia->hdlr : trak->mdia->minf->hdlr;
    if( !hdlr )
        return 0;
    isom_bs_put_box_common( bs, hdlr );
    lsmash_bs_put_be32( bs, hdlr->componentType );
    lsmash_bs_put_be32( bs, hdlr->componentSubtype );
    lsmash_bs_put_be32( bs, hdlr->componentManufacturer );
    lsmash_bs_put_be32( bs, hdlr->componentFlags );
    lsmash_bs_put_be32( bs, hdlr->componentFlagsMask );
    lsmash_bs_put_bytes( bs, hdlr->componentName, hdlr->componentName_length );
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

static int isom_write_dref( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_dref_t *dref = trak->mdia->minf->dinf->dref;
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
            lsmash_bs_put_bytes( bs, data->name, data->name_length );
        lsmash_bs_put_bytes( bs, data->location, data->location_length );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_dinf( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_dinf_t *dinf = trak->mdia->minf->dinf;
    if( !dinf )
        return -1;
    isom_bs_put_box_common( bs, dinf );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    return isom_write_dref( bs, trak );
}

static void isom_put_pasp( lsmash_bs_t *bs, isom_pasp_t *pasp )
{
    if( !pasp )
        return;
    isom_bs_put_box_common( bs, pasp );
    lsmash_bs_put_be32( bs, pasp->hSpacing );
    lsmash_bs_put_be32( bs, pasp->vSpacing );
}

static void isom_put_clap( lsmash_bs_t *bs, isom_clap_t *clap )
{
    if( !clap )
        return;
    isom_bs_put_box_common( bs, clap );
    lsmash_bs_put_be32( bs, clap->cleanApertureWidthN );
    lsmash_bs_put_be32( bs, clap->cleanApertureWidthD );
    lsmash_bs_put_be32( bs, clap->cleanApertureHeightN );
    lsmash_bs_put_be32( bs, clap->cleanApertureHeightD );
    lsmash_bs_put_be32( bs, clap->horizOffN );
    lsmash_bs_put_be32( bs, clap->horizOffD );
    lsmash_bs_put_be32( bs, clap->vertOffN );
    lsmash_bs_put_be32( bs, clap->vertOffD );
}

static void isom_put_colr( lsmash_bs_t *bs, isom_colr_t *colr )
{
    if( !colr || colr->color_parameter_type == QT_COLOR_PARAMETER_TYPE_PROF )
        return;
    isom_bs_put_box_common( bs, colr );
    lsmash_bs_put_be32( bs, colr->color_parameter_type );
    lsmash_bs_put_be16( bs, colr->primaries_index );
    lsmash_bs_put_be16( bs, colr->transfer_function_index );
    lsmash_bs_put_be16( bs, colr->matrix_index );
}

static void isom_put_stsl( lsmash_bs_t *bs, isom_stsl_t *stsl )
{
    if( !stsl )
        return;
    isom_bs_put_box_common( bs, stsl );
    lsmash_bs_put_byte( bs, stsl->constraint_flag );
    lsmash_bs_put_byte( bs, stsl->scale_method );
    lsmash_bs_put_be16( bs, stsl->display_center_x );
    lsmash_bs_put_be16( bs, stsl->display_center_y );
}

static void isom_put_visual_extensions( lsmash_bs_t *bs, isom_visual_entry_t *visual )
{
    if( !visual )
        return;
    isom_put_clap( bs, visual->clap );
    isom_put_pasp( bs, visual->pasp );
    isom_put_colr( bs, visual->colr );
    isom_put_stsl( bs, visual->stsl );
}

static int isom_put_ps_entries( lsmash_bs_t *bs, lsmash_entry_list_t *list )
{
    for( lsmash_entry_t *entry = list->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be16( bs, data->parameterSetLength );
        lsmash_bs_put_bytes( bs, data->parameterSetNALUnit, data->parameterSetLength );
    }
    return 0;
}

static int isom_put_avcC( lsmash_bs_t *bs, isom_avcC_t *avcC )
{
    if( !bs || !avcC || !avcC->sequenceParameterSets || !avcC->pictureParameterSets )
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
    return 0;
}

static void isom_put_btrt( lsmash_bs_t *bs, isom_btrt_t *btrt )
{
    if( !btrt )
        return;
    isom_bs_put_box_common( bs, btrt );
    lsmash_bs_put_be32( bs, btrt->bufferSizeDB );
    lsmash_bs_put_be32( bs, btrt->maxBitrate );
    lsmash_bs_put_be32( bs, btrt->avgBitrate );
}

static int isom_write_esds( lsmash_bs_t *bs, isom_esds_t *esds )
{
    if( !esds )
        return 0;
    isom_bs_put_box_common( bs, esds );
    return mp4sys_write_ES_Descriptor( bs, esds->ES );
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
     || isom_write_mp4a( bs, wave->mp4a )
     || isom_write_esds( bs, wave->esds ) )
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

static int isom_write_avc_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_avc_entry_t *data = (isom_avc_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_box_common( bs, data );
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
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
    lsmash_bs_put_bytes( bs, data->compressorname, 32 );
    lsmash_bs_put_be16( bs, data->depth );
    lsmash_bs_put_be16( bs, data->color_table_ID );
    isom_put_visual_extensions( bs, (isom_visual_entry_t *)data );
    if( !data->avcC )
        return -1;
    isom_put_avcC( bs, data->avcC );
    if( data->btrt )
        isom_put_btrt( bs, data->btrt );
    return lsmash_bs_write_data( bs );
}

static int isom_write_audio_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_audio_entry_t *data = (isom_audio_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_box_common( bs, data );
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
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
    lsmash_bs_put_bytes( bs, data->exdata, data->exdata_length );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_esds( bs, data->esds )
     || isom_write_wave( bs, data->wave )
     || isom_write_chan( bs, data->chan ) )
        return -1;
    return 0;
}

#if 0
static int isom_write_visual_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_visual_entry_t *data = (isom_visual_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_box_common( bs, data );
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
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
    lsmash_bs_put_bytes( bs, data->compressorname, 32 );
    lsmash_bs_put_be16( bs, data->depth );
    lsmash_bs_put_be16( bs, data->color_table_ID );
    isom_put_visual_extensions( bs, data );
    if( data->type == ISOM_CODEC_TYPE_AVC1_VIDEO )
    {
        isom_avc_entry_t *avc = (isom_avc_entry_t *)data;
        if( !avc || !avc->avcC )
            return -1;
        isom_put_avcC( bs, avc->avcC );
        if( avc->btrt )
            isom_put_btrt( bs, avc->btrt );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_hint_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_hint_entry_t *data = (isom_hint_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_box_common( bs, data );
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    if( data->data && data->data_length )
        lsmash_bs_put_bytes( bs, data->data, data->data_length );
    return lsmash_bs_write_data( bs );
}

static int isom_write_metadata_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_metadata_entry_t *data = (isom_metadata_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_box_common( bs, data );
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
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
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
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
        lsmash_bs_put_bytes( bs, data->font_name, data->font_name_length );
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
            lsmash_bs_put_bytes( bs, data->font_name, data->font_name_length );
    }
    return 0;
}

static int isom_write_tx3g_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_tx3g_entry_t *data = (isom_tx3g_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_box_common( bs, data );
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
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
#if 0
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
            case ISOM_CODEC_TYPE_SVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC2_VIDEO :
#endif
                ret = isom_write_avc_entry( bs, entry );
                break;
#if 0
            case ISOM_CODEC_TYPE_DRAC_VIDEO :
            case ISOM_CODEC_TYPE_ENCV_VIDEO :
            case ISOM_CODEC_TYPE_MJP2_VIDEO :
            case ISOM_CODEC_TYPE_S263_VIDEO :
            case ISOM_CODEC_TYPE_VC_1_VIDEO :
                ret = isom_write_visual_entry( bs, entry );
                break;
#endif
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
            case QT_CODEC_TYPE_23NI_AUDIO :
            case QT_CODEC_TYPE_NONE_AUDIO :
            case QT_CODEC_TYPE_LPCM_AUDIO :
            case QT_CODEC_TYPE_RAW_AUDIO :
            case QT_CODEC_TYPE_SOWT_AUDIO :
            case QT_CODEC_TYPE_TWOS_AUDIO :
            case QT_CODEC_TYPE_FL32_AUDIO :
            case QT_CODEC_TYPE_FL64_AUDIO :
            case QT_CODEC_TYPE_IN24_AUDIO :
            case QT_CODEC_TYPE_IN32_AUDIO :
            case QT_CODEC_TYPE_NOT_SPECIFIED :
#if 0
            case ISOM_CODEC_TYPE_DRA1_AUDIO :
            case ISOM_CODEC_TYPE_DTSC_AUDIO :
            case ISOM_CODEC_TYPE_DTSH_AUDIO :
            case ISOM_CODEC_TYPE_DTSL_AUDIO :
            case ISOM_CODEC_TYPE_EC_3_AUDIO :
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

static int isom_write_sdtp( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_sdtp_t *sdtp = trak->mdia->minf->stbl->sdtp;
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
     || isom_write_sdtp( bs, trak )
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
    if( isom_write_hdlr( bs, trak, 0 )
     || isom_write_dinf( bs, trak )
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
     || isom_write_hdlr( bs, trak, 1 )
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
        lsmash_bs_put_bytes( bs, data->chapter_name, data->chapter_name_length );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_udta( lsmash_bs_t *bs, isom_moov_t *moov, isom_trak_entry_t *trak )
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
    return 0;
}

static int isom_write_trak( lsmash_bs_t *bs, isom_trak_entry_t *trak )
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
     || isom_write_udta( bs, NULL, trak ) )
        return -1;
    return 0;
}

static int isom_write_iods( lsmash_root_t *root )
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

static int isom_write_mvhd( lsmash_root_t *root )
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
    uint32_t temp = (flags->reserved                    << 28)
                  | (flags->is_leading                  << 26)
                  | (flags->sample_depends_on           << 24)
                  | (flags->sample_is_depended_on       << 22)
                  | (flags->sample_has_redundancy       << 20)
                  | (flags->sample_padding_value        << 17)
                  | (flags->sample_is_difference_sample << 16)
                  |  flags->sample_degradation_priority;
    lsmash_bs_put_be32( bs, temp );
}

static int isom_write_mehd( lsmash_bs_t *bs, isom_mehd_t *mehd )
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
    lsmash_bs_put_be32( bs, ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 8 );
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
    /* Movie Extends Header Box is not written immediatly.
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
    return 0;
}

static int isom_write_moof( lsmash_bs_t *bs, isom_moof_entry_t *moof )
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

static int isom_write_mfra( lsmash_bs_t *bs, isom_mfra_t *mfra )
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
    lsmash_bs_put_be32( bs, ISOM_DEFAULT_BOX_HEADER_SIZE );
    lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_FREE );
    return lsmash_bs_write_data( bs );
}

static int isom_write_mdat_header( lsmash_root_t *root, uint64_t media_size )
{
    if( !root || !root->bs || !root->mdat )
        return -1;
    isom_mdat_t *mdat = root->mdat;
    lsmash_bs_t *bs = root->bs;
    if( media_size )
    {
        mdat->size = ISOM_DEFAULT_BOX_HEADER_SIZE + media_size;
        if( mdat->size > UINT32_MAX )
            mdat->size += 8;    /* large_size */
        isom_bs_put_box_common( bs, mdat );
        return 0;
    }
    mdat->placeholder_pos = lsmash_ftell( bs->stream );
    if( isom_bs_write_largesize_placeholder( bs ) )
        return -1;
    mdat->size = ISOM_DEFAULT_BOX_HEADER_SIZE;
    isom_bs_put_box_common( bs, mdat );
    return lsmash_bs_write_data( bs );
}

static int isom_write_mdat_size( lsmash_root_t *root )
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
        lsmash_bs_put_be64( bs, mdat->size + ISOM_DEFAULT_BOX_HEADER_SIZE );
    }
    else
    {
        lsmash_fseek( stream, mdat->placeholder_pos + ISOM_DEFAULT_BOX_HEADER_SIZE, SEEK_SET );
        lsmash_bs_put_be32( bs, mdat->size );
        lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_MDAT );
    }
    int ret = lsmash_bs_write_data( bs );
    lsmash_fseek( stream, current_pos, SEEK_SET );
    return ret;
}

/* We put a placeholder for 64-bit media data if the media_size of the argument is set to 0.
 * If a Media Data Box already exists and we don't pick movie fragments structure,
 * write the actual size of the current one and start a new one. */
static int isom_new_mdat( lsmash_root_t *root, uint64_t media_size )
{
    if( !root )
        return 0;
    if( root->mdat )
    {
        /* Write the actual size of the current Media Data Box. */
        if( !root->fragment && isom_write_mdat_size( root ) )
            return -1;
    }
    else
    {
        isom_create_box( mdat, root, ISOM_BOX_TYPE_MDAT );
        root->mdat = mdat;
    }
    /* Start a new Media Data Box. */
    return isom_write_mdat_header( root, media_size );
}

static int isom_check_compatibility( lsmash_root_t *root )
{
    if( !root )
        return -1;
    /* Check brand to decide mandatory boxes. */
    if( !root->ftyp || !root->ftyp->brand_count )
    {
        /* We assume this file is not a QuickTime but MP4 version 1 format file. */
        root->mp4_version1 = 1;
        return 0;
    }
    for( uint32_t i = 0; i < root->ftyp->brand_count; i++ )
    {
        switch( root->ftyp->compatible_brands[i] )
        {
            case ISOM_BRAND_TYPE_QT :
                root->qt_compatible = 1;
                break;
            case ISOM_BRAND_TYPE_MP41 :
                root->mp4_version1 = 1;
                break;
            case ISOM_BRAND_TYPE_MP42 :
                root->mp4_version2 = 1;
                break;
            case ISOM_BRAND_TYPE_ISOM :
                root->max_isom_version = LSMASH_MAX( root->max_isom_version, 1 );
                break;
            case ISOM_BRAND_TYPE_ISO2 :
                root->max_isom_version = LSMASH_MAX( root->max_isom_version, 2 );
                break;
            case ISOM_BRAND_TYPE_ISO3 :
                root->max_isom_version = LSMASH_MAX( root->max_isom_version, 3 );
                break;
            case ISOM_BRAND_TYPE_ISO4 :
                root->max_isom_version = LSMASH_MAX( root->max_isom_version, 4 );
                break;
            case ISOM_BRAND_TYPE_ISO5 :
                root->max_isom_version = LSMASH_MAX( root->max_isom_version, 5 );
                break;
            case ISOM_BRAND_TYPE_ISO6 :
                root->max_isom_version = LSMASH_MAX( root->max_isom_version, 6 );
                break;
            case ISOM_BRAND_TYPE_M4A :
            case ISOM_BRAND_TYPE_M4B :
                root->itunes_audio = 1;
                break;
            case ISOM_BRAND_TYPE_3GP4 :
                root->max_3gpp_version = LSMASH_MAX( root->max_3gpp_version, 4 );
                break;
            case ISOM_BRAND_TYPE_3GP5 :
                root->max_3gpp_version = LSMASH_MAX( root->max_3gpp_version, 5 );
                break;
            case ISOM_BRAND_TYPE_3GE6 :
            case ISOM_BRAND_TYPE_3GG6 :
            case ISOM_BRAND_TYPE_3GP6 :
            case ISOM_BRAND_TYPE_3GR6 :
            case ISOM_BRAND_TYPE_3GS6 :
                root->max_3gpp_version = LSMASH_MAX( root->max_3gpp_version, 6 );
                break;
            default :
                break;
        }
        switch( root->ftyp->compatible_brands[i] )
        {
            case ISOM_BRAND_TYPE_AVC1 :
            case ISOM_BRAND_TYPE_ISO2 :
            case ISOM_BRAND_TYPE_ISO3 :
            case ISOM_BRAND_TYPE_ISO4 :
            case ISOM_BRAND_TYPE_ISO5 :
            case ISOM_BRAND_TYPE_ISO6 :
                root->avc_extensions = 1;
                break;
            default :
                break;
        }
    }
    root->isom_compatible = !root->qt_compatible || root->mp4_version1 || root->mp4_version2 || root->itunes_audio || root->max_3gpp_version;
    return 0;
}

static char *isom_4cc2str( uint32_t fourcc )
{
    static char str[5];
    str[0] = (fourcc >> 24) & 0xff;
    str[1] = (fourcc >> 16) & 0xff;
    str[2] = (fourcc >>  8) & 0xff;
    str[3] =  fourcc        & 0xff;
    str[4] = 0;
    return str;
}

static void isom_iprintf( int indent, const char *format, ... )
{
#include <stdarg.h>
    va_list args;
    va_start( args, format );
    for( int i = 0; i < indent; i++ )
        printf( "    " );
    vprintf( format, args );
    va_end( args );
}

static double isom_fixed2double( uint64_t value, int frac_width )
{
    return value / (double)(1ULL << frac_width);
}

static float isom_int2float32( uint32_t value )
{
    return (union {uint32_t i; float f;}){value}.f;
}

static double isom_int2float64( uint64_t value )
{
    return (union {uint64_t i; double d;}){value}.d;
}

static void isom_iprintf_duration( int indent, char *field_name, uint64_t duration, uint32_t timescale )
{
    if( !timescale )
    {
        isom_iprintf( indent, "duration = %"PRIu64"\n", duration );
        return;
    }
    int dur = duration / timescale;
    int hour = (dur / 3600) % 24;
    int min  = (dur /   60) % 60;
    int sec  =  dur         % 60;
    int ms   = ((double)duration / timescale - (hour * 3600 + min * 60 + sec)) * 1e3 + 0.5;
    static char str[32];
    sprintf( str, "%02d:%02d:%02d.%03d", hour, min, sec, ms );
    isom_iprintf( indent, "%s = %"PRIu64" (%s)\n", field_name, duration, str );
}

static char *isom_mp4time2utc( uint64_t mp4time )
{
    int year_offset = mp4time / 31536000;
    int leap_years = year_offset / 4 + ((mp4time / 86400) > 366);   /* 1904 itself is leap year */
    int day = (mp4time / 86400) - (year_offset * 365) - leap_years + 1;
    while( day < 1 )
    {
        --year_offset;
        leap_years = year_offset / 4 + ((mp4time / 86400) > 366);
        day = (mp4time / 86400) - (year_offset * 365) - leap_years + 1;
    }
    int year = 1904 + year_offset;
    int is_leap = (!(year % 4) && (year % 100)) || !(year % 400);
    static const int month_days[13] = { 29, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int month;
    for( month = 1; month <= 12; month++ )
    {
        int i = (month == 2 && is_leap) ? 0 : month;
        if( day <= month_days[i] )
            break;
        day -= month_days[i];
    }
    int hour = (mp4time / 3600) % 24;
    int min  = (mp4time /   60) % 60;
    int sec  =  mp4time         % 60;
    static char utc[64];
    sprintf( utc, "UTC %d/%02d/%02d, %02d:%02d:%02d\n", year, month, day, hour, min, sec );
    return utc;
}

static char *isom_unpack_iso_language( uint16_t language )
{
    static char unpacked[4];
    unpacked[0] = ((language >> 10) & 0x1f) + 0x60;
    unpacked[1] = ((language >>  5) & 0x1f) + 0x60;
    unpacked[2] = ( language        & 0x1f) + 0x60;
    unpacked[3] = 0;
    return unpacked;
}

static void isom_iprint_matrix( int indent, int32_t *matrix )
{
    isom_iprintf( indent, "| a, b, u |   | %f, %f, %f |\n", isom_fixed2double( matrix[0], 16 ),
                                                            isom_fixed2double( matrix[1], 16 ),
                                                            isom_fixed2double( matrix[2], 30 ) );
    isom_iprintf( indent, "| c, d, v | = | %f, %f, %f |\n", isom_fixed2double( matrix[3], 16 ),
                                                            isom_fixed2double( matrix[4], 16 ),
                                                            isom_fixed2double( matrix[5], 30 ) );
    isom_iprintf( indent, "| x, y, z |   | %f, %f, %f |\n", isom_fixed2double( matrix[6], 16 ),
                                                            isom_fixed2double( matrix[7], 16 ),
                                                            isom_fixed2double( matrix[8], 30 ) );
}

static void isom_iprint_rgb_color( int indent, uint16_t *color )
{
    isom_iprintf( indent, "{ R, G, B } = { %"PRIu16", %"PRIu16", %"PRIu16" }\n", color[0], color[1], color[2] );
}

static void isom_iprint_rgba_color( int indent, uint8_t *color )
{
    isom_iprintf( indent, "{ R, G, B, A } = { %"PRIu8", %"PRIu8", %"PRIu8", %"PRIu8" }\n", color[0], color[1], color[2], color[3] );
}

static void isom_iprint_sample_description_common_reserved( int indent, uint8_t *reserved )
{
    uint64_t temp = ((uint64_t)reserved[0] << 40)
                  | ((uint64_t)reserved[1] << 32)
                  | ((uint64_t)reserved[2] << 24)
                  | ((uint64_t)reserved[3] << 16)
                  | ((uint64_t)reserved[4] <<  8)
                  |  (uint64_t)reserved[5];
    isom_iprintf( indent, "reserved = 0x%012"PRIx64"\n", temp );
}

static void isom_iprint_sample_flags( int indent, char *field_name, isom_sample_flags_t *flags )
{
    uint32_t temp = (flags->reserved                    << 28)
                  | (flags->is_leading                  << 26)
                  | (flags->sample_depends_on           << 24)
                  | (flags->sample_is_depended_on       << 22)
                  | (flags->sample_has_redundancy       << 20)
                  | (flags->sample_padding_value        << 17)
                  | (flags->sample_is_difference_sample << 16)
                  |  flags->sample_degradation_priority;
    isom_iprintf( indent++, "%s = 0x%08"PRIx32"\n", field_name, temp );
         if( flags->is_leading & ISOM_SAMPLE_IS_UNDECODABLE_LEADING       ) isom_iprintf( indent, "undecodable leading\n" );
    else if( flags->is_leading & ISOM_SAMPLE_IS_NOT_LEADING               ) isom_iprintf( indent, "non-leading\n" );
    else if( flags->is_leading & ISOM_SAMPLE_IS_DECODABLE_LEADING         ) isom_iprintf( indent, "decodable leading\n" );
         if( flags->sample_depends_on & ISOM_SAMPLE_IS_INDEPENDENT        ) isom_iprintf( indent, "independent\n" );
    else if( flags->sample_depends_on & ISOM_SAMPLE_IS_NOT_INDEPENDENT    ) isom_iprintf( indent, "dependent\n" );
         if( flags->sample_is_depended_on & ISOM_SAMPLE_IS_NOT_DISPOSABLE ) isom_iprintf( indent, "non-disposable\n" );
    else if( flags->sample_is_depended_on & ISOM_SAMPLE_IS_DISPOSABLE     ) isom_iprintf( indent, "disposable\n" );
         if( flags->sample_has_redundancy & ISOM_SAMPLE_HAS_REDUNDANCY    ) isom_iprintf( indent, "redundant\n" );
    else if( flags->sample_has_redundancy & ISOM_SAMPLE_HAS_NO_REDUNDANCY ) isom_iprintf( indent, "non-redundant\n" );
    if( flags->sample_padding_value )
        isom_iprintf( indent, "padding_bits = %"PRIu8"\n", flags->sample_padding_value );
    isom_iprintf( indent, flags->sample_is_difference_sample ? "non-sync sample\n" : "sync sample\n" );
    isom_iprintf( indent, "degradation_priority = %"PRIu16"\n", flags->sample_degradation_priority );
}

static inline int isom_print_simple( isom_box_t *box, int level, char *name )
{
    if( !box )
        return -1;
    int indent = level;
    isom_iprintf( indent++, "[%s: %s]\n", isom_4cc2str( box->type ), name );
    isom_iprintf( indent, "position = %"PRIu64"\n", box->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", box->size );
    return 0;
}

static void isom_print_basebox_common( int indent, isom_box_t *box, char *name )
{
    isom_print_simple( box, indent, name );
}

static void isom_print_fullbox_common( int indent, isom_box_t *box, char *name )
{
    isom_iprintf( indent++, "[%s: %s]\n", isom_4cc2str( box->type ), name );
    isom_iprintf( indent, "position = %"PRIu64"\n", box->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", box->size );
    isom_iprintf( indent, "version = %"PRIu8"\n", box->version );
    isom_iprintf( indent, "flags = 0x%06"PRIx32"\n", box->flags & 0x00ffffff );
}

static void isom_print_box_common( int indent, isom_box_t *box, char *name )
{
    isom_box_t *parent = box->parent;
    if( parent && parent->type == ISOM_BOX_TYPE_STSD )
    {
        isom_print_basebox_common( indent, box, name );
        return;
    }
    if( isom_is_fullbox( box ) )
        isom_print_fullbox_common( indent, box, name );
    else
        isom_print_basebox_common( indent, box, name );
}

static int isom_print_unknown( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    int indent = level;
    isom_iprintf( indent++, "[%s]\n", isom_4cc2str( box->type ) );
    isom_iprintf( indent, "size = %"PRIu64"\n", box->size );
    return 0;
}

static int isom_print_ftyp( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_ftyp_t *ftyp = (isom_ftyp_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "File Type Box" );
    isom_iprintf( indent, "major_brand = %s\n", isom_4cc2str( ftyp->major_brand ) );
    isom_iprintf( indent, "minor_version = %"PRIu32"\n", ftyp->minor_version );
    isom_iprintf( indent++, "compatible_brands\n" );
    for( uint32_t i = 0; i < ftyp->brand_count; i++ )
        isom_iprintf( indent, "brand[%"PRIu32"] = %s\n", i, isom_4cc2str( ftyp->compatible_brands[i] ) );
    return 0;
}

static int isom_print_moov( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Movie Box" );
}

static int isom_print_mvhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_mvhd_t *mvhd = (isom_mvhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Movie Header Box" );
    isom_iprintf( indent, "creation_time = %s", isom_mp4time2utc( mvhd->creation_time ) );
    isom_iprintf( indent, "modification_time = %s", isom_mp4time2utc( mvhd->modification_time ) );
    isom_iprintf( indent, "timescale = %"PRIu32"\n", mvhd->timescale );
    isom_iprintf_duration( indent, "duration", mvhd->duration, mvhd->timescale );
    isom_iprintf( indent, "rate = %f\n", isom_fixed2double( mvhd->rate, 16 ) );
    isom_iprintf( indent, "volume = %f\n", isom_fixed2double( mvhd->volume, 8 ) );
    isom_iprintf( indent, "reserved = 0x%04"PRIx16"\n", mvhd->reserved );
    if( root->qt_compatible )
    {
        isom_iprintf( indent, "preferredLong1 = 0x%08"PRIx32"\n", mvhd->preferredLong[0] );
        isom_iprintf( indent, "preferredLong2 = 0x%08"PRIx32"\n", mvhd->preferredLong[1] );
        isom_iprintf( indent, "transformation matrix\n" );
        isom_iprint_matrix( indent + 1, mvhd->matrix );
        isom_iprintf( indent, "previewTime = %"PRId32"\n", mvhd->previewTime );
        isom_iprintf( indent, "previewDuration = %"PRId32"\n", mvhd->previewDuration );
        isom_iprintf( indent, "posterTime = %"PRId32"\n", mvhd->posterTime );
        isom_iprintf( indent, "selectionTime = %"PRId32"\n", mvhd->selectionTime );
        isom_iprintf( indent, "selectionDuration = %"PRId32"\n", mvhd->selectionDuration );
        isom_iprintf( indent, "currentTime = %"PRId32"\n", mvhd->currentTime );
    }
    else
    {
        isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", mvhd->preferredLong[0] );
        isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", mvhd->preferredLong[1] );
        isom_iprintf( indent, "transformation matrix\n" );
        isom_iprint_matrix( indent + 1, mvhd->matrix );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", mvhd->previewTime );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", mvhd->previewDuration );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", mvhd->posterTime );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", mvhd->selectionTime );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", mvhd->selectionDuration );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", mvhd->currentTime );
    }
    isom_iprintf( indent, "next_track_ID = %"PRIu32"\n", mvhd->next_track_ID );
    return 0;
}

static int isom_print_iods( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Object Descriptor Box" );
}

static int isom_print_esds( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "ES Descriptor Box" );
}

static int isom_print_trak( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Track Box" );
}

static int isom_print_tkhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_tkhd_t *tkhd = (isom_tkhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Header Box" );
    ++indent;
    if( tkhd->flags & ISOM_TRACK_ENABLED )
        isom_iprintf( indent, "Track enabled\n" );
    else
        isom_iprintf( indent, "Track disabled\n" );
    if( tkhd->flags & ISOM_TRACK_IN_MOVIE )
        isom_iprintf( indent, "Track in movie\n" );
    if( tkhd->flags & ISOM_TRACK_IN_PREVIEW )
        isom_iprintf( indent, "Track in preview\n" );
    if( root->qt_compatible && (tkhd->flags & QT_TRACK_IN_POSTER) )
        isom_iprintf( indent, "Track in poster\n" );
    isom_iprintf( --indent, "creation_time = %s", isom_mp4time2utc( tkhd->creation_time ) );
    isom_iprintf( indent, "modification_time = %s", isom_mp4time2utc( tkhd->modification_time ) );
    isom_iprintf( indent, "track_ID = %"PRIu32"\n", tkhd->track_ID );
    isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", tkhd->reserved1 );
    if( root && root->moov && root->moov->mvhd )
        isom_iprintf_duration( indent, "duration", tkhd->duration, root->moov->mvhd->timescale );
    else
        isom_iprintf_duration( indent, "duration", tkhd->duration, 0 );
    isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", tkhd->reserved2[0] );
    isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", tkhd->reserved2[1] );
    isom_iprintf( indent, "layer = %"PRId16"\n", tkhd->layer );
    isom_iprintf( indent, "alternate_group = %"PRId16"\n", tkhd->alternate_group );
    isom_iprintf( indent, "volume = %f\n", isom_fixed2double( tkhd->volume, 8 ) );
    isom_iprintf( indent, "reserved = 0x%04"PRIx16"\n", tkhd->reserved3 );
    isom_iprintf( indent, "transformation matrix\n" );
    isom_iprint_matrix( indent + 1, tkhd->matrix );
    isom_iprintf( indent, "width = %f\n", isom_fixed2double( tkhd->width, 16 ) );
    isom_iprintf( indent, "height = %f\n", isom_fixed2double( tkhd->height, 16 ) );
    return 0;
}

static int isom_print_tapt( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Track Aperture Mode Dimensions Box" );
}

static int isom_print_clef( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_clef_t *clef = (isom_clef_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Clean Aperture Dimensions Box" );
    isom_iprintf( indent, "width = %f\n", isom_fixed2double( clef->width, 16 ) );
    isom_iprintf( indent, "height = %f\n", isom_fixed2double( clef->height, 16 ) );
    return 0;
}

static int isom_print_prof( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_prof_t *prof = (isom_prof_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Production Aperture Dimensions Box" );
    isom_iprintf( indent, "width = %f\n", isom_fixed2double( prof->width, 16 ) );
    isom_iprintf( indent, "height = %f\n", isom_fixed2double( prof->height, 16 ) );
    return 0;
}

static int isom_print_enof( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_enof_t *enof = (isom_enof_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Encoded Pixels Dimensions Box" );
    isom_iprintf( indent, "width = %f\n", isom_fixed2double( enof->width, 16 ) );
    isom_iprintf( indent, "height = %f\n", isom_fixed2double( enof->height, 16 ) );
    return 0;
}

static int isom_print_edts( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Edit Box" );
}

static int isom_print_elst( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_elst_t *elst = (isom_elst_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Edit List Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", elst->list->entry_count );
    for( lsmash_entry_t *entry = elst->list->head; entry; entry = entry->next )
    {
        isom_elst_entry_t *data = (isom_elst_entry_t *)entry->data;
        isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
        isom_iprintf( indent, "segment_duration = %"PRIu64"\n", data->segment_duration );
        if( box->version == 1 )
            isom_iprintf( indent, "media_time = %"PRId64"\n", data->media_time );
        else
            isom_iprintf( indent, "media_time = %"PRId32"\n", data->media_time );
        isom_iprintf( indent--, "media_rate = %f\n", isom_fixed2double( data->media_rate, 16 ) );
    }
    return 0;
}

static int isom_print_tref( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Track Reference Box" );
}

static int isom_print_track_reference_type( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_tref_type_t *ref = (isom_tref_type_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Reference Type Box" );
    for( uint32_t i = 0; i < ref->ref_count; i++ )
        isom_iprintf( indent, "track_ID[%"PRIu32"] = %"PRIu32"\n", i, ref->track_ID[i] );
    return 0;


    return isom_print_simple( box, level, "Track Reference Type Box" );
}

static int isom_print_mdia( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Media Box" );
}

static int isom_print_mdhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_mdhd_t *mdhd = (isom_mdhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Media Header Box" );
    isom_iprintf( indent, "creation_time = %s", isom_mp4time2utc( mdhd->creation_time ) );
    isom_iprintf( indent, "modification_time = %s", isom_mp4time2utc( mdhd->modification_time ) );
    isom_iprintf( indent, "timescale = %"PRIu32"\n", mdhd->timescale );
    isom_iprintf_duration( indent, "duration", mdhd->duration, mdhd->timescale );
    if( mdhd->language >= 0x800 )
        isom_iprintf( indent, "language = %s\n", isom_unpack_iso_language( mdhd->language ) );
    else
        isom_iprintf( indent, "language = %"PRIu16"\n", mdhd->language );
    if( root->qt_compatible )
        isom_iprintf( indent, "quality = %"PRId16"\n", mdhd->quality );
    else
        isom_iprintf( indent, "pre_defined = 0x%04"PRIx16"\n", mdhd->quality );
    return 0;
}

static int isom_print_hdlr( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_hdlr_t *hdlr = (isom_hdlr_t *)box;
    int indent = level;
    char str[hdlr->componentName_length + 1];
    memcpy( str, hdlr->componentName, hdlr->componentName_length );
    str[hdlr->componentName_length] = 0;
    isom_print_box_common( indent++, box, "Handler Reference Box" );
    if( root->qt_compatible )
    {
        isom_iprintf( indent, "componentType = %s\n", isom_4cc2str( hdlr->componentType ) );
        isom_iprintf( indent, "componentSubtype = %s\n", isom_4cc2str( hdlr->componentSubtype ) );
        isom_iprintf( indent, "componentManufacturer = %s\n", isom_4cc2str( hdlr->componentManufacturer ) );
        isom_iprintf( indent, "componentFlags = 0x%08"PRIx32"\n", hdlr->componentFlags );
        isom_iprintf( indent, "componentFlagsMask = 0x%08"PRIx32"\n", hdlr->componentFlagsMask );
        if( hdlr->componentName_length )
            isom_iprintf( indent, "componentName = %s\n", &str[1] );
    }
    else
    {
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", hdlr->componentType );
        isom_iprintf( indent, "handler_type = %s\n", isom_4cc2str( hdlr->componentSubtype ) );
        isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", hdlr->componentManufacturer );
        isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", hdlr->componentFlags );
        isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", hdlr->componentFlagsMask );
        isom_iprintf( indent, "name = %s\n", str );
    }
    return 0;
}

static int isom_print_minf( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Media Information Box" );
}

static int isom_print_vmhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_vmhd_t *vmhd = (isom_vmhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Video Media Header Box" );
    isom_iprintf( indent, "graphicsmode = %"PRIu16"\n", vmhd->graphicsmode );
    isom_iprintf( indent, "opcolor\n" ); 
    isom_iprint_rgb_color( indent + 1, vmhd->opcolor );
    return 0;
}

static int isom_print_smhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_smhd_t *smhd = (isom_smhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Sound Media Header Box" );
    isom_iprintf( indent, "balance = %f\n", isom_fixed2double( smhd->balance, 8 ) );
    isom_iprintf( indent, "reserved = 0x%04"PRIx16"\n", smhd->reserved );
    return 0;
}

static int isom_print_hmhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_hmhd_t *hmhd = (isom_hmhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Hint Media Header Box" );
    isom_iprintf( indent, "maxPDUsize = %"PRIu16"\n", hmhd->maxPDUsize );
    isom_iprintf( indent, "avgPDUsize = %"PRIu16"\n", hmhd->avgPDUsize );
    isom_iprintf( indent, "maxbitrate = %"PRIu32"\n", hmhd->maxbitrate );
    isom_iprintf( indent, "avgbitrate = %"PRIu32"\n", hmhd->avgbitrate );
    isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", hmhd->reserved );
    return 0;
}

static int isom_print_nmhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Null Media Header Box" );
}

static int isom_print_gmhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Generic Media Information Header Box" );
}

static int isom_print_gmin( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_gmin_t *gmin = (isom_gmin_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Generic Media Information Box" );
    isom_iprintf( indent, "graphicsmode = %"PRIu16"\n", gmin->graphicsmode );
    isom_iprintf( indent, "opcolor\n" ); 
    isom_iprint_rgb_color( indent + 1, gmin->opcolor );
    isom_iprintf( indent, "balance = %f\n", isom_fixed2double( gmin->balance, 8 ) );
    isom_iprintf( indent, "reserved = 0x%04"PRIx16"\n", gmin->reserved );
    return 0;
}

static int isom_print_text( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_text_t *text = (isom_text_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Text Media Information Box" );
    isom_iprintf( indent, "Unknown matrix\n" );
    isom_iprint_matrix( indent + 1, text->matrix );
    return 0;
}

static int isom_print_dinf( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Data Information Box" );
}

static int isom_print_dref( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_dref_t *dref = (isom_dref_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Data Reference Box" );
    isom_iprintf( indent, "entry_count = %"PRIu16"\n", dref->list->entry_count );
    return 0;
}

static int isom_print_url( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_dref_entry_t *url = (isom_dref_entry_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Data Entry Url Box" );
    if( url->flags & 0x000001 )
        isom_iprintf( indent, "location = in the same file\n" );
    else
        isom_iprintf( indent, "location = %s\n", url->location );
    return 0;
}

static int isom_print_stbl( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Sample Table Box" );
}

static int isom_print_stsd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_stsd_t *)box)->list )
        return -1;
    isom_stsd_t *stsd = (isom_stsd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Sample Description Box" );
    isom_iprintf( indent, "entry_count = %"PRIu16"\n", stsd->list->entry_count );
    return 0;
}

static int isom_print_visual_description( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_visual_entry_t *visual = (isom_visual_entry_t *)box;
    int indent = level;
    isom_iprintf( indent++, "[%s: Visual Description]\n", isom_4cc2str( visual->type ) );
    isom_iprintf( indent, "position = %"PRIu64"\n", visual->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", visual->size );
    isom_iprint_sample_description_common_reserved( indent, visual->reserved );
    isom_iprintf( indent, "data_reference_index = %"PRIu16"\n", visual->data_reference_index );
    if( root->qt_compatible )
    {
        isom_iprintf( indent, "version = %"PRId16"\n", visual->version );
        isom_iprintf( indent, "revision_level = %"PRId16"\n", visual->revision_level );
        isom_iprintf( indent, "vendor = %s\n", isom_4cc2str( visual->vendor ) );
        isom_iprintf( indent, "temporalQuality = %"PRIu32"\n", visual->temporalQuality );
        isom_iprintf( indent, "spatialQuality = %"PRIu32"\n", visual->spatialQuality );
        isom_iprintf( indent, "width = %"PRIu16"\n", visual->width );
        isom_iprintf( indent, "height = %"PRIu16"\n", visual->height );
        isom_iprintf( indent, "horizresolution = %f\n", isom_fixed2double( visual->horizresolution, 16 ) );
        isom_iprintf( indent, "vertresolution = %f\n", isom_fixed2double( visual->vertresolution, 16 ) );
        isom_iprintf( indent, "dataSize = %"PRIu32"\n", visual->dataSize );
        isom_iprintf( indent, "frame_count = %"PRIu16"\n", visual->frame_count );
        isom_iprintf( indent, "compressorname_length = %"PRIu8"\n", visual->compressorname[0] );
        isom_iprintf( indent, "compressorname = %s\n", visual->compressorname + 1 );
        isom_iprintf( indent, "depth = 0x%04"PRIx16, visual->depth );
        if( visual->depth == 32 )
            printf( " (colour with alpha)\n" );
        else if( visual->depth >= 33 && visual->depth <= 40 )
            printf( " (grayscale with no alpha)\n" );
        else
            printf( "\n" );
        isom_iprintf( indent, "color_table_ID = %"PRId16"\n", visual->color_table_ID );
    }
    else
    {
        isom_iprintf( indent, "pre_defined = 0x%04"PRIx16"\n", visual->version );
        isom_iprintf( indent, "reserved = 0x%04"PRIx16"\n", visual->revision_level );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", visual->vendor );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", visual->temporalQuality );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", visual->spatialQuality );
        isom_iprintf( indent, "width = %"PRIu16"\n", visual->width );
        isom_iprintf( indent, "height = %"PRIu16"\n", visual->height );
        isom_iprintf( indent, "horizresolution = %f\n", isom_fixed2double( visual->horizresolution, 16 ) );
        isom_iprintf( indent, "vertresolution = %f\n", isom_fixed2double( visual->vertresolution, 16 ) );
        isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", visual->dataSize );
        isom_iprintf( indent, "frame_count = %"PRIu16"\n", visual->frame_count );
        isom_iprintf( indent, "compressorname_length = %"PRIu8"\n", visual->compressorname[0] );
        isom_iprintf( indent, "compressorname = %s\n", visual->compressorname + 1 );
        isom_iprintf( indent, "depth = 0x%04"PRIx16, visual->depth );
        if( visual->depth == 0x0018 )
            printf( " (colour with no alpha)\n" );
        else if( visual->depth == 0x0028 )
            printf( " (grayscale with no alpha)\n" );
        else if( visual->depth == 0x0020 )
            printf( " (gray or colour with alpha)\n" );
        else
            printf( "\n" );
        isom_iprintf( indent, "pre_defined = 0x%04"PRIx16"\n", visual->color_table_ID );
    }
    return 0;
}

static int isom_print_btrt( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_btrt_t *btrt = (isom_btrt_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Bit Rate Box" );
    isom_iprintf( indent, "bufferSizeDB = %"PRIu32"\n", btrt->bufferSizeDB );
    isom_iprintf( indent, "maxBitrate = %"PRIu32"\n", btrt->maxBitrate );
    isom_iprintf( indent, "avgBitrate = %"PRIu32"\n", btrt->avgBitrate );
    return 0;
}

static int isom_print_clap( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_clap_t *clap = (isom_clap_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Clean Aperture Box" );
    isom_iprintf( indent, "cleanApertureWidthN = %"PRIu32"\n", clap->cleanApertureWidthN );
    isom_iprintf( indent, "cleanApertureWidthD = %"PRIu32"\n", clap->cleanApertureWidthD );
    isom_iprintf( indent, "cleanApertureHeightN = %"PRIu32"\n", clap->cleanApertureHeightN );
    isom_iprintf( indent, "cleanApertureHeightD = %"PRIu32"\n", clap->cleanApertureHeightD );
    isom_iprintf( indent, "horizOffN = %"PRId32"\n", clap->horizOffN );
    isom_iprintf( indent, "horizOffD = %"PRIu32"\n", clap->horizOffD );
    isom_iprintf( indent, "vertOffN = %"PRId32"\n", clap->vertOffN );
    isom_iprintf( indent, "vertOffD = %"PRIu32"\n", clap->vertOffD );
    return 0;
}

static int isom_print_pasp( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_pasp_t *pasp = (isom_pasp_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Pixel Aspect Ratio Box" );
    isom_iprintf( indent, "hSpacing = %"PRIu32"\n", pasp->hSpacing );
    isom_iprintf( indent, "vSpacing = %"PRIu32"\n", pasp->vSpacing );
    return 0;
}

static int isom_print_colr( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_colr_t *colr = (isom_colr_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Color Parameter Box" );
    isom_iprintf( indent, "color_parameter_type = %s\n", isom_4cc2str( colr->color_parameter_type ) );
    if( colr->color_parameter_type == QT_COLOR_PARAMETER_TYPE_NCLC )
    {
        isom_iprintf( indent, "primaries_index = %"PRIu16"\n", colr->primaries_index );
        isom_iprintf( indent, "transfer_function_index = %"PRIu16"\n", colr->transfer_function_index );
        isom_iprintf( indent, "matrix_index = %"PRIu16"\n", colr->matrix_index );
    }
    return 0;
}

static int isom_print_stsl( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_stsl_t *stsl = (isom_stsl_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Sample Scale Box" );
    isom_iprintf( indent, "constraint_flag = %s\n", (stsl->constraint_flag & 0x01) ? "on" : "off" );
    isom_iprintf( indent, "scale_method = " );
    if( stsl->scale_method == ISOM_SCALING_METHOD_FILL )
        printf( "'fill'\n" );
    else if( stsl->scale_method == ISOM_SCALING_METHOD_HIDDEN )
        printf( "'hidden'\n" );
    else if( stsl->scale_method == ISOM_SCALING_METHOD_MEET )
        printf( "'meet'\n" );
    else if( stsl->scale_method == ISOM_SCALING_METHOD_SLICE_X )
        printf( "'slice' in the x-coodinate\n" );
    else if( stsl->scale_method == ISOM_SCALING_METHOD_SLICE_Y )
        printf( "'slice' in the y-coodinate\n" );
    isom_iprintf( indent, "display_center_x = %"PRIu16"\n", stsl->display_center_x );
    isom_iprintf( indent, "display_center_y = %"PRIu16"\n", stsl->display_center_y );
    return 0;
}

static int isom_print_avcC( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_avcC_t *avcC = (isom_avcC_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "AVC Configuration Box" );
    isom_iprintf( indent, "configurationVersion = %"PRIu8"\n", avcC->configurationVersion );
    isom_iprintf( indent, "AVCProfileIndication = %"PRIu8"\n", avcC->AVCProfileIndication );
    isom_iprintf( indent, "profile_compatibility = 0x%02"PRIu8"\n", avcC->profile_compatibility );
    isom_iprintf( indent, "AVCLevelIndication = %"PRIu8"\n", avcC->AVCLevelIndication );
    isom_iprintf( indent, "lengthSizeMinusOne = %"PRIu8"\n", avcC->lengthSizeMinusOne & 0x03 );
    isom_iprintf( indent, "numOfSequenceParameterSets = %"PRIu8"\n", avcC->numOfSequenceParameterSets & 0x1f );
    isom_iprintf( indent, "numOfPictureParameterSets = %"PRIu8"\n", avcC->numOfPictureParameterSets );
    if( ISOM_REQUIRES_AVCC_EXTENSION( avcC->AVCProfileIndication ) )
    {
        isom_iprintf( indent, "chroma_format = %"PRIu8"\n", avcC->chroma_format & 0x03 );
        isom_iprintf( indent, "bit_depth_luma_minus8 = %"PRIu8"\n", avcC->bit_depth_luma_minus8 & 0x7 );
        isom_iprintf( indent, "bit_depth_chroma_minus8 = %"PRIu8"\n", avcC->bit_depth_chroma_minus8 & 0x7 );
        isom_iprintf( indent, "numOfSequenceParameterSetExt = %"PRIu8"\n", avcC->numOfSequenceParameterSetExt );
    }
    return 0;
}

static int isom_print_audio_description( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_audio_entry_t *audio = (isom_audio_entry_t *)box;
    int indent = level;
    isom_iprintf( indent++, "[%s: Audio Description]\n", isom_4cc2str( audio->type ) );
    isom_iprintf( indent, "position = %"PRIu64"\n", audio->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", audio->size );
    isom_iprint_sample_description_common_reserved( indent, audio->reserved );
    isom_iprintf( indent, "data_reference_index = %"PRIu16"\n", audio->data_reference_index );
    if( root->qt_compatible )
    {
        isom_iprintf( indent, "version = %"PRId16"\n", audio->version );
        isom_iprintf( indent, "revision_level = %"PRId16"\n", audio->revision_level );
        isom_iprintf( indent, "vendor = %s\n", isom_4cc2str( audio->vendor ) );
        isom_iprintf( indent, "channelcount = %"PRIu16"\n", audio->channelcount );
        isom_iprintf( indent, "samplesize = %"PRIu16"\n", audio->samplesize );
        isom_iprintf( indent, "compression_ID = %"PRId16"\n", audio->compression_ID );
        isom_iprintf( indent, "packet_size = %"PRIu16"\n", audio->packet_size );
    }
    else
    {
        isom_iprintf( indent, "reserved = 0x%04"PRIx16"\n", audio->version );
        isom_iprintf( indent, "reserved = 0x%04"PRIx16"\n", audio->revision_level );
        isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", audio->vendor );
        isom_iprintf( indent, "channelcount = %"PRIu16"\n", audio->channelcount );
        isom_iprintf( indent, "samplesize = %"PRIu16"\n", audio->samplesize );
        isom_iprintf( indent, "pre_defined = %"PRId16"\n", audio->compression_ID );
        isom_iprintf( indent, "reserved = %"PRIu16"\n", audio->packet_size );
    }
    isom_iprintf( indent, "samplerate = %f\n", isom_fixed2double( audio->samplerate, 16 ) );
    if( audio->version == 1 )
    {
        isom_iprintf( indent, "samplesPerPacket = %"PRIu32"\n", audio->samplesPerPacket );
        isom_iprintf( indent, "bytesPerPacket = %"PRIu32"\n", audio->bytesPerPacket );
        isom_iprintf( indent, "bytesPerFrame = %"PRIu32"\n", audio->bytesPerFrame );
        isom_iprintf( indent, "bytesPerSample = %"PRIu32"\n", audio->bytesPerSample );
    }
    else if( audio->version == 2 )
    {
        isom_iprintf( indent, "sizeOfStructOnly = %"PRIu32"\n", audio->sizeOfStructOnly );
        isom_iprintf( indent, "audioSampleRate = %lf\n", isom_int2float64( audio->audioSampleRate ) );
        isom_iprintf( indent, "numAudioChannels = %"PRIu32"\n", audio->numAudioChannels );
        isom_iprintf( indent, "always7F000000 = 0x%08"PRIx32"\n", audio->always7F000000 );
        isom_iprintf( indent, "constBitsPerChannel = %"PRIu32"\n", audio->constBitsPerChannel );
        isom_iprintf( indent++, "formatSpecificFlags = 0x%08"PRIx32"\n", audio->formatSpecificFlags );
        if( isom_is_lpcm_audio( audio->type ) )
        {
            isom_iprintf( indent, "sample format: " );
            if( audio->formatSpecificFlags & QT_LPCM_FORMAT_FLAG_FLOAT )
                printf( "floating point\n" );
            else
            {
                printf( "integer\n" );
                isom_iprintf( indent, "signedness: " );
                printf( audio->formatSpecificFlags & QT_LPCM_FORMAT_FLAG_SIGNED_INTEGER ? "signed\n" : "unsigned\n" );
            }
            if( audio->constBytesPerAudioPacket != 1 )
            {
                isom_iprintf( indent, "endianness: " );
                printf( audio->formatSpecificFlags & QT_LPCM_FORMAT_FLAG_BIG_ENDIAN ? "big\n" : "little\n" );
            }
            isom_iprintf( indent, "packed: " );
            if( audio->formatSpecificFlags & QT_LPCM_FORMAT_FLAG_PACKED )
                printf( "yes\n" );
            else
            {
                printf( "no\n" );
                isom_iprintf( indent, "alignment: " );
                printf( audio->formatSpecificFlags & QT_LPCM_FORMAT_FLAG_ALIGNED_HIGH ? "high\n" : "low\n" );
            }
            if( audio->numAudioChannels > 1 )
            {
                isom_iprintf( indent, "interleved: " );
                printf( audio->formatSpecificFlags & QT_LPCM_FORMAT_FLAG_NON_INTERLEAVED ? "no\n" : "yes\n" );
            }
        }
        isom_iprintf( --indent, "constBytesPerAudioPacket = %"PRIu32"\n", audio->constBytesPerAudioPacket );
        isom_iprintf( indent, "constLPCMFramesPerAudioPacket = %"PRIu32"\n", audio->constLPCMFramesPerAudioPacket );
    }
    return 0;
}

static int isom_print_wave( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Sound Information Decompression Parameters Box" );
}

static int isom_print_frma( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_frma_t *frma = (isom_frma_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Format Box" );
    isom_iprintf( indent, "data_format = %s\n", isom_4cc2str( frma->data_format ) );
    return 0;
}

static int isom_print_enda( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_enda_t *enda = (isom_enda_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Audio Endian Box" );
    isom_iprintf( indent, "littleEndian = %s\n", enda->littleEndian ? "yes" : "no" );
    return 0;
}

static int isom_print_terminator( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_terminator_t *terminator = (isom_terminator_t *)box;
    int indent = level;
    isom_iprintf( indent++, "[0x00000000: Terminator Box]\n" );
    isom_iprintf( indent, "position = %"PRIu64"\n", terminator->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", terminator->size );
    return 0;
}

static int isom_print_chan( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_chan_t *chan = (isom_chan_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Channel Compositor Box" );
    isom_iprintf( indent, "channelLayoutTag = 0x%08"PRIx32"\n", chan->channelLayoutTag );
    isom_iprintf( indent, "channelBitmap = 0x%08"PRIx32"\n", chan->channelBitmap );
    isom_iprintf( indent, "numberChannelDescriptions = %"PRIu32"\n", chan->numberChannelDescriptions );
    if( chan->numberChannelDescriptions )
    {
        isom_channel_description_t *desc = chan->channelDescriptions;
        for( uint32_t i = 0; i < chan->numberChannelDescriptions; i++ )
        {
            isom_iprintf( indent++, "ChannelDescriptions[%"PRIu32"]\n", i );
            isom_iprintf( indent, "channelLabel = 0x%08"PRIx32"\n", desc->channelLabel );
            isom_iprintf( indent, "channelFlags = 0x%08"PRIx32"\n", desc->channelFlags );
            for( int j = 0; j < 3; j++ )
                isom_iprintf( indent, "coordinates[%d] = %f\n", j, isom_int2float32( desc->coordinates[j] ) );
            --indent;
        }
    }
    return 0;
}

static int isom_print_text_description( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_text_entry_t *text = (isom_text_entry_t *)box;
    int indent = level;
    isom_iprintf( indent++, "[text: QuickTime Text Description]\n" );
    isom_iprintf( indent, "position = %"PRIu64"\n", text->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", text->size );
    isom_iprint_sample_description_common_reserved( indent, text->reserved );
    isom_iprintf( indent, "data_reference_index = %"PRIu16"\n", text->data_reference_index );
    isom_iprintf( indent, "displayFlags = 0x%08"PRId32"\n", text->displayFlags );
    isom_iprintf( indent, "textJustification = %"PRId32"\n", text->textJustification );
    isom_iprintf( indent, "bgColor\n" );
    isom_iprint_rgb_color( indent + 1, text->bgColor );
    isom_iprintf( indent, "top = %"PRId16"\n", text->top );
    isom_iprintf( indent, "left = %"PRId16"\n", text->left );
    isom_iprintf( indent, "bottom = %"PRId16"\n", text->bottom );
    isom_iprintf( indent, "right = %"PRId16"\n", text->right );
    isom_iprintf( indent, "scrpStartChar = %"PRId32"\n", text->scrpStartChar );
    isom_iprintf( indent, "scrpHeight = %"PRId16"\n", text->scrpHeight );
    isom_iprintf( indent, "scrpAscent = %"PRId16"\n", text->scrpAscent );
    isom_iprintf( indent, "scrpFont = %"PRId16"\n", text->scrpFont );
    isom_iprintf( indent, "scrpFace = %"PRIu16"\n", text->scrpFace );
    isom_iprintf( indent, "scrpSize = %"PRId16"\n", text->scrpSize );
    isom_iprintf( indent, "scrpColor\n" );
    isom_iprint_rgb_color( indent + 1, text->scrpColor );
    if( text->font_name_length )
        isom_iprintf( indent, "font_name = %s\n", text->font_name );
    return 0;
}

static int isom_print_tx3g_description( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_tx3g_entry_t *tx3g = (isom_tx3g_entry_t *)box;
    int indent = level;
    isom_iprintf( indent++, "[tx3g: Timed Text Description]\n" );
    isom_iprintf( indent, "position = %"PRIu64"\n", tx3g->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", tx3g->size );
    isom_iprint_sample_description_common_reserved( indent, tx3g->reserved );
    isom_iprintf( indent, "data_reference_index = %"PRIu16"\n", tx3g->data_reference_index );
    isom_iprintf( indent, "displayFlags = 0x%08"PRId32"\n", tx3g->displayFlags );
    isom_iprintf( indent, "horizontal_justification = %"PRId8"\n", tx3g->horizontal_justification );
    isom_iprintf( indent, "vertical_justification = %"PRId8"\n", tx3g->vertical_justification );
    isom_iprintf( indent, "background_color_rgba\n" );
    isom_iprint_rgba_color( indent + 1, tx3g->background_color_rgba );
    isom_iprintf( indent, "top = %"PRId16"\n", tx3g->top );
    isom_iprintf( indent, "left = %"PRId16"\n", tx3g->left );
    isom_iprintf( indent, "bottom = %"PRId16"\n", tx3g->bottom );
    isom_iprintf( indent, "right = %"PRId16"\n", tx3g->right );
    isom_iprintf( indent, "startChar = %"PRIu16"\n", tx3g->startChar );
    isom_iprintf( indent, "endChar = %"PRIu16"\n", tx3g->endChar );
    isom_iprintf( indent, "font_ID = %"PRIu16"\n", tx3g->font_ID );
    isom_iprintf( indent, "face_style_flags = %"PRIu8"\n", tx3g->face_style_flags );
    isom_iprintf( indent, "font_size = %"PRIu8"\n", tx3g->font_size );
    isom_iprintf( indent, "text_color_rgba\n" );
    isom_iprint_rgba_color( indent + 1, tx3g->text_color_rgba );
    return 0;
}

static int isom_print_ftab( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_ftab_t *)box)->list )
        return -1;
    isom_ftab_t *ftab = (isom_ftab_t *)box;
    int indent = level;
    uint16_t i = 0;
    isom_print_box_common( indent++, box, "Font Table Box" );
    isom_iprintf( indent, "entry_count = %"PRIu16"\n", ftab->list->entry_count );
    for( lsmash_entry_t *entry = ftab->list->head; entry; entry = entry->next )
    {
        isom_font_record_t *data = (isom_font_record_t *)entry->data;
        isom_iprintf( indent++, "entry[%"PRIu16"]\n", i++ );
        isom_iprintf( indent, "font_ID = %"PRIu16"\n", data->font_ID );
        if( data->font_name_length )
            isom_iprintf( indent, "font_name = %s\n", data->font_name );
        --indent;
    }
    return 0;
}

static int isom_print_stts( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_stts_t *)box)->list )
        return -1;
    isom_stts_t *stts = (isom_stts_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Decoding Time to Sample Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", stts->list->entry_count );
    for( lsmash_entry_t *entry = stts->list->head; entry; entry = entry->next )
    {
        isom_stts_entry_t *data = (isom_stts_entry_t *)entry->data;
        isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
        isom_iprintf( indent, "sample_count = %"PRIu32"\n", data->sample_count );
        isom_iprintf( indent--, "sample_delta = %"PRIu32"\n", data->sample_delta );
    }
    return 0;
}

static int isom_print_ctts( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_ctts_t *)box)->list )
        return -1;
    isom_ctts_t *ctts = (isom_ctts_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Composition Time to Sample Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", ctts->list->entry_count );
    if( root->qt_compatible || ctts->version == 1 )
        for( lsmash_entry_t *entry = ctts->list->head; entry; entry = entry->next )
        {
            isom_ctts_entry_t *data = (isom_ctts_entry_t *)entry->data;
            isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
            isom_iprintf( indent, "sample_count = %"PRIu32"\n", data->sample_count );
            isom_iprintf( indent--, "sample_offset = %"PRId32"\n", (union {uint32_t ui; int32_t si;}){data->sample_offset}.si );
        }
    else
        for( lsmash_entry_t *entry = ctts->list->head; entry; entry = entry->next )
        {
            isom_ctts_entry_t *data = (isom_ctts_entry_t *)entry->data;
            isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
            isom_iprintf( indent, "sample_count = %"PRIu32"\n", data->sample_count );
            isom_iprintf( indent--, "sample_offset = %"PRIu32"\n", data->sample_offset );
        }
    return 0;
}

static int isom_print_cslg( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_cslg_t *cslg = (isom_cslg_t *)box;
    int indent = level;
    if( root->qt_compatible )
    {
        isom_print_box_common( indent++, box, "Composition Shift Least Greatest Box" );
        isom_iprintf( indent, "compositionOffsetToDTDDeltaShift = %"PRId32"\n", cslg->compositionToDTSShift );
        isom_iprintf( indent, "leastDecodeToDisplayDelta = %"PRId32"\n", cslg->leastDecodeToDisplayDelta );
        isom_iprintf( indent, "greatestDecodeToDisplayDelta = %"PRId32"\n", cslg->greatestDecodeToDisplayDelta );
        isom_iprintf( indent, "displayStartTime = %"PRId32"\n", cslg->compositionStartTime );
        isom_iprintf( indent, "displayEndTime = %"PRId32"\n", cslg->compositionEndTime );
    }
    else
    {
        isom_print_box_common( indent++, box, "Composition to Decode Box" );
        isom_iprintf( indent, "compositionToDTSShift = %"PRId32"\n", cslg->compositionToDTSShift );
        isom_iprintf( indent, "leastDecodeToDisplayDelta = %"PRId32"\n", cslg->leastDecodeToDisplayDelta );
        isom_iprintf( indent, "greatestDecodeToDisplayDelta = %"PRId32"\n", cslg->greatestDecodeToDisplayDelta );
        isom_iprintf( indent, "compositionStartTime = %"PRId32"\n", cslg->compositionStartTime );
        isom_iprintf( indent, "compositionEndTime = %"PRId32"\n", cslg->compositionEndTime );
    }
    return 0;
}

static int isom_print_stss( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_stss_t *)box)->list )
        return -1;
    isom_stss_t *stss = (isom_stss_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Sync Sample Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", stss->list->entry_count );
    for( lsmash_entry_t *entry = stss->list->head; entry; entry = entry->next )
        isom_iprintf( indent, "sample_number[%"PRIu32"] = %"PRIu32"\n", i++, ((isom_stss_entry_t *)entry->data)->sample_number );
    return 0;
}

static int isom_print_stps( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_stps_t *)box)->list )
        return -1;
    isom_stps_t *stps = (isom_stps_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Partial Sync Sample Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", stps->list->entry_count );
    for( lsmash_entry_t *entry = stps->list->head; entry; entry = entry->next )
        isom_iprintf( indent, "sample_number[%"PRIu32"] = %"PRIu32"\n", i++, ((isom_stps_entry_t *)entry->data)->sample_number );
    return 0;
}

static int isom_print_sdtp( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_sdtp_t *)box)->list )
        return -1;
    isom_sdtp_t *sdtp = (isom_sdtp_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Independent and Disposable Samples Box" );
    for( lsmash_entry_t *entry = sdtp->list->head; entry; entry = entry->next )
    {
        isom_sdtp_entry_t *data = (isom_sdtp_entry_t *)entry->data;
        isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
        if( data->is_leading || data->sample_depends_on || data->sample_is_depended_on || data->sample_has_redundancy )
        {
            if( root->avc_extensions )
            {
                if( data->is_leading & ISOM_SAMPLE_IS_UNDECODABLE_LEADING )
                    isom_iprintf( indent, "undecodable leading\n" );
                else if( data->is_leading & ISOM_SAMPLE_IS_NOT_LEADING )
                    isom_iprintf( indent, "non-leading\n" );
                else if( data->is_leading & ISOM_SAMPLE_IS_DECODABLE_LEADING )
                    isom_iprintf( indent, "decodable leading\n" );
            }
            else if( data->is_leading & QT_SAMPLE_EARLIER_PTS_ALLOWED )
                isom_iprintf( indent, "early display times allowed\n" );
            if( data->sample_depends_on & ISOM_SAMPLE_IS_INDEPENDENT )
                isom_iprintf( indent, "independent\n" );
            else if( data->sample_depends_on & ISOM_SAMPLE_IS_NOT_INDEPENDENT )
                isom_iprintf( indent, "dependent\n" );
            if( data->sample_is_depended_on & ISOM_SAMPLE_IS_NOT_DISPOSABLE )
                isom_iprintf( indent, "non-disposable\n" );
            else if( data->sample_is_depended_on & ISOM_SAMPLE_IS_DISPOSABLE )
                isom_iprintf( indent, "disposable\n" );
            if( data->sample_has_redundancy & ISOM_SAMPLE_HAS_REDUNDANCY )
                isom_iprintf( indent, "redundant\n" );
            else if( data->sample_has_redundancy & ISOM_SAMPLE_HAS_NO_REDUNDANCY )
                isom_iprintf( indent, "non-redundant\n" );
        }
        else
            isom_iprintf( indent, "no description\n" );
        --indent;
    }
    return 0;
}

static int isom_print_stsc( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_stsc_t *)box)->list )
        return -1;
    isom_stsc_t *stsc = (isom_stsc_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Sample To Chunk Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", stsc->list->entry_count );
    for( lsmash_entry_t *entry = stsc->list->head; entry; entry = entry->next )
    {
        isom_stsc_entry_t *data = (isom_stsc_entry_t *)entry->data;
        isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
        isom_iprintf( indent, "first_chunk = %"PRIu32"\n", data->first_chunk );
        isom_iprintf( indent, "samples_per_chunk = %"PRIu32"\n", data->samples_per_chunk );
        isom_iprintf( indent--, "sample_description_index = %"PRIu32"\n", data->sample_description_index );
    }
    return 0;
}

static int isom_print_stsz( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_stsz_t *stsz = (isom_stsz_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Sample Size Box" );
    if( !stsz->sample_size )
        isom_iprintf( indent, "sample_size = 0 (variable)\n" );
    else
        isom_iprintf( indent, "sample_size = %"PRIu32" (constant)\n", stsz->sample_size );
    isom_iprintf( indent, "sample_count = %"PRIu32"\n", stsz->sample_count );
    if( !stsz->sample_size && stsz->list )
        for( lsmash_entry_t *entry = stsz->list->head; entry; entry = entry->next )
        {
            isom_stsz_entry_t *data = (isom_stsz_entry_t *)entry->data;
            isom_iprintf( indent, "entry_size[%"PRIu32"] = %"PRIu32"\n", i++, data->entry_size );
        }
    return 0;
}

static int isom_print_stco( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_stco_t *)box)->list )
        return -1;
    isom_stco_t *stco = (isom_stco_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Chunk Offset Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", stco->list->entry_count );
    if( stco->type == ISOM_BOX_TYPE_STCO )
    {
        for( lsmash_entry_t *entry = stco->list->head; entry; entry = entry->next )
            isom_iprintf( indent, "chunk_offset[%"PRIu32"] = %"PRIu32"\n", i++, ((isom_stco_entry_t *)entry->data)->chunk_offset );
    }
    else
    {
        for( lsmash_entry_t *entry = stco->list->head; entry; entry = entry->next )
            isom_iprintf( indent, "chunk_offset[%"PRIu32"] = %"PRIu64"\n", i++, ((isom_co64_entry_t *)entry->data)->chunk_offset );
    }
    return 0;
}

static int isom_print_sgpd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_sgpd_entry_t *)box)->list )
        return -1;
    isom_sgpd_entry_t *sgpd = (isom_sgpd_entry_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Sample Group Description Box" );
    isom_iprintf( indent, "grouping_type = %s\n", isom_4cc2str( sgpd->grouping_type ) );
    if( sgpd->version == 1 )
    {
        isom_iprintf( indent, "default_length = %"PRIu32, sgpd->default_length );
        printf( " %s\n", sgpd->default_length ? "(constant)" : "(variable)" );
    }
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", sgpd->list->entry_count );
    switch( sgpd->grouping_type )
    {
        case ISOM_GROUP_TYPE_RAP :
            for( lsmash_entry_t *entry = sgpd->list->head; entry; entry = entry->next )
            {
                if( sgpd->version == 1 && !sgpd->default_length )
                    isom_iprintf( indent, "description_length[%"PRIu32"] = %"PRIu32"\n", i++, ((isom_rap_entry_t *)entry->data)->description_length );
                else
                {
                    isom_rap_entry_t *rap = (isom_rap_entry_t *)entry->data;
                    isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
                    isom_iprintf( indent, "num_leading_samples_known = %"PRIu8"\n", rap->num_leading_samples_known );
                    isom_iprintf( indent--, "num_leading_samples = %"PRIu8"\n", rap->num_leading_samples );
                }
            }
            break;
        case ISOM_GROUP_TYPE_ROLL :
            for( lsmash_entry_t *entry = sgpd->list->head; entry; entry = entry->next )
            {
                if( sgpd->version == 1 && !sgpd->default_length )
                    isom_iprintf( indent, "description_length[%"PRIu32"] = %"PRIu32"\n", i++, ((isom_roll_entry_t *)entry->data)->description_length );
                else
                    isom_iprintf( indent, "roll_distance[%"PRIu32"] = %"PRId16"\n", i++, ((isom_roll_entry_t *)entry->data)->roll_distance );
            }
            break;
        default :
            break;
    }
    return 0;
}

static int isom_print_sbgp( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_sbgp_entry_t *)box)->list )
        return -1;
    isom_sbgp_entry_t *sbgp = (isom_sbgp_entry_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Sample to Group Box" );
    isom_iprintf( indent, "grouping_type = %s\n", isom_4cc2str( sbgp->grouping_type ) );
    if( sbgp->version == 1 )
        isom_iprintf( indent, "grouping_type_parameter = %s\n", isom_4cc2str( sbgp->grouping_type_parameter ) );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", sbgp->list->entry_count );
    for( lsmash_entry_t *entry = sbgp->list->head; entry; entry = entry->next )
    {
        isom_group_assignment_entry_t *data = (isom_group_assignment_entry_t *)entry->data;
        isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
        isom_iprintf( indent, "sample_count = %"PRIu32"\n", data->sample_count );
        isom_iprintf( indent--, "group_description_index = %"PRIu32, data->group_description_index );
        if( !data->group_description_index )
            printf( " (not in this grouping type)\n" );
        else
            printf( "\n" );
    }
    return 0;
}

static int isom_print_udta( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "User Data Box" );
}

static int isom_print_chpl( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_chpl_t *chpl = (isom_chpl_t *)box;
    uint32_t timescale;
    if( !chpl->version )
    {
        if( !root->moov && !root->moov->mvhd )
            return -1;
        timescale = root->moov->mvhd->timescale;
    }
    else
        timescale = 10000000;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Chapter List Box" );
    if( chpl->version == 1 )
    {
        isom_iprintf( indent, "unknown = 0x%02"PRIx8"\n", chpl->unknown );
        isom_iprintf( indent, "entry_count = %"PRIu32"\n", chpl->list->entry_count );
    }
    else
        isom_iprintf( indent, "entry_count = %"PRIu8"\n", (uint8_t)chpl->list->entry_count );
    for( lsmash_entry_t *entry = chpl->list->head; entry; entry = entry->next )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        int64_t start_time = data->start_time / timescale;
        int hh =  start_time / 3600;
        int mm = (start_time /   60) % 60;
        int ss =  start_time         % 60;
        int ms = ((data->start_time / (double)timescale) - hh * 3600 - mm * 60 - ss) * 1e3 + 0.5; 
        char str[256];
        memset( str, 0, sizeof(str) );
        memcpy( str, data->chapter_name, data->chapter_name_length );
        isom_iprintf( indent++, "chapter[%"PRIu32"]\n", i++ );
        isom_iprintf( indent, "start_time = %02d:%02d:%02d.%03d\n", hh, mm, ss, ms );
        isom_iprintf( indent--, "chapter_name = %s\n", data->chapter_name );
    }
    return 0;
}

static int isom_print_mvex( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Movie Extends Box" );
}

static int isom_print_mehd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_mehd_t *mehd = (isom_mehd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Movie Extends Header Box" );
    if( root && root->moov && root->moov->mvhd )
        isom_iprintf_duration( indent, "fragment_duration", mehd->fragment_duration, root->moov->mvhd->timescale );
    else
        isom_iprintf_duration( indent, "fragment_duration", mehd->fragment_duration, 0 );
    return 0;
}

static int isom_print_trex( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_trex_entry_t *trex = (isom_trex_entry_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Extends Box" );
    isom_iprintf( indent, "track_ID = %"PRIu32"\n", trex->track_ID );
    isom_iprintf( indent, "default_sample_description_index = %"PRIu32"\n", trex->default_sample_description_index );
    isom_iprintf( indent, "default_sample_duration = %"PRIu32"\n", trex->default_sample_duration );
    isom_iprintf( indent, "default_sample_size = %"PRIu32"\n", trex->default_sample_size );
    isom_iprint_sample_flags( indent, "default_sample_flags", &trex->default_sample_flags );
    return 0;
}

static int isom_print_moof( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Movie Fragment Box" );
}

static int isom_print_mfhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_mfhd_t *mfhd = (isom_mfhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Movie Fragment Header Box" );
    isom_iprintf( indent, "sequence_number = %"PRIu32"\n", mfhd->sequence_number );
    return 0;
}

static int isom_print_traf( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Track Fragment Box" );
}

static int isom_print_tfhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_tfhd_t *tfhd = (isom_tfhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Fragment Header Box" );
    ++indent;
    if( tfhd->flags & ISOM_TF_FLAGS_BASE_DATA_OFFSET_PRESENT         ) isom_iprintf( indent, "base-data-offset-present\n" );
    if( tfhd->flags & ISOM_TF_FLAGS_SAMPLE_DESCRIPTION_INDEX_PRESENT ) isom_iprintf( indent, "sample-description-index-present\n" );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT  ) isom_iprintf( indent, "default-sample-duration-present\n" );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_SIZE_PRESENT      ) isom_iprintf( indent, "default-sample-size-present\n" );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT     ) isom_iprintf( indent, "default-sample-flags-present\n" );
    isom_iprintf( --indent, "track_ID = %"PRIu32"\n", tfhd->track_ID );
    if( tfhd->flags & ISOM_TF_FLAGS_BASE_DATA_OFFSET_PRESENT )
        isom_iprintf( indent, "base_data_offset = %"PRIu64"\n", tfhd->base_data_offset );
    if( tfhd->flags & ISOM_TF_FLAGS_SAMPLE_DESCRIPTION_INDEX_PRESENT )
        isom_iprintf( indent, "sample_description_index = %"PRIu32"\n", tfhd->sample_description_index );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT )
        isom_iprintf( indent, "default_sample_duration = %"PRIu32"\n", tfhd->default_sample_duration );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_SIZE_PRESENT )
        isom_iprintf( indent, "default_sample_size = %"PRIu32"\n", tfhd->default_sample_size );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT )
        isom_iprint_sample_flags( indent, "default_sample_flags", &tfhd->default_sample_flags );
    return 0;
}

static int isom_print_trun( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_trun_entry_t *trun = (isom_trun_entry_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Fragment Run Box" );
    ++indent;
    if( trun->flags & ISOM_TR_FLAGS_DATA_OFFSET_PRESENT                    ) isom_iprintf( indent, "data-offset-present\n" );
    if( trun->flags & ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT             ) isom_iprintf( indent, "first-sample-flags-present\n" );
    if( trun->flags & ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT                ) isom_iprintf( indent, "sample-duration-present\n" );
    if( trun->flags & ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT                    ) isom_iprintf( indent, "sample-size-present\n" );
    if( trun->flags & ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT                   ) isom_iprintf( indent, "sample-flags-present\n" );
    if( trun->flags & ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT ) isom_iprintf( indent, "sample-composition-time-offsets-present\n" );
    isom_iprintf( --indent, "sample_count = %"PRIu32"\n", trun->sample_count );
    if( trun->flags & ISOM_TR_FLAGS_DATA_OFFSET_PRESENT )
        isom_iprintf( indent, "data_offset = %"PRId32"\n", trun->data_offset );
    if( trun->flags & ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT )
        isom_iprint_sample_flags( indent, "first_sample_flags", &trun->first_sample_flags );
    if( trun->optional )
    {
        uint32_t i = 0;
        for( lsmash_entry_t *entry = trun->optional->head; entry; entry = entry->next )
        {
            isom_trun_optional_row_t *row = (isom_trun_optional_row_t *)entry->data;
            isom_iprintf( indent++, "sample[%"PRIu32"]\n", i++ );
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT )
                isom_iprintf( indent, "sample_duration = %"PRIu32"\n", row->sample_duration );
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT )
                isom_iprintf( indent, "sample_size = %"PRIu32"\n", row->sample_size );
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT )
                isom_iprint_sample_flags( indent, "sample_flags", &row->sample_flags );
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT )
                isom_iprintf( indent, "sample_composition_time_offset = %"PRIu32"\n", row->sample_composition_time_offset );
            --indent;
        }
    }
    return 0;
}

static int isom_print_free( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Free Space Box" );
}

static int isom_print_mdat( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Media Data Box" );
}

static int isom_print_mfra( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Movie Fragment Random Access Box" );
}

static int isom_print_tfra( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_tfra_entry_t *tfra = (isom_tfra_entry_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Fragment Random Access Box" );
    isom_iprintf( indent, "track_ID = %"PRIu32"\n", tfra->track_ID );
    isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", tfra->reserved );
    isom_iprintf( indent, "length_size_of_traf_num = %"PRIu8"\n", tfra->length_size_of_traf_num );
    isom_iprintf( indent, "length_size_of_trun_num = %"PRIu8"\n", tfra->length_size_of_trun_num );
    isom_iprintf( indent, "length_size_of_sample_num = %"PRIu8"\n", tfra->length_size_of_sample_num );
    isom_iprintf( indent, "number_of_entry = %"PRIu32"\n", tfra->number_of_entry );
    if( tfra->list )
    {
        uint32_t i = 0;
        for( lsmash_entry_t *entry = tfra->list->head; entry; entry = entry->next )
        {
            isom_tfra_location_time_entry_t *data = (isom_tfra_location_time_entry_t *)entry->data;
            isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
            isom_iprintf( indent, "time = %"PRIu64"\n", data->time );
            isom_iprintf( indent, "moof_offset = %"PRIu64"\n", data->moof_offset );
            isom_iprintf( indent, "traf_number = %"PRIu32"\n", data->traf_number );
            isom_iprintf( indent, "trun_number = %"PRIu32"\n", data->trun_number );
            isom_iprintf( indent, "sample_number = %"PRIu32"\n", data->sample_number );
            --indent;
        }
    }
    return 0;
}

static int isom_print_mfro( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_mfro_t *mfro = (isom_mfro_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Movie Fragment Random Access Offset Box" );
    isom_iprintf( indent, "size = %"PRIu32"\n", mfro->length );
    return 0;
}

int lsmash_print_movie( lsmash_root_t *root )
{
    if( !root || !root->print || !(root->flags & LSMASH_FILE_MODE_DUMP) )
        return -1;
    printf( "[ROOT]\n" );
    printf( "    size = %"PRIu64"\n", root->size );
    for( lsmash_entry_t *entry = root->print->head; entry; entry = entry->next )
    {
        isom_print_entry_t *data = (isom_print_entry_t *)entry->data;
        if( !data || data->func( root, data->box, data->level ) )
            return -1;
    }
    return 0;
}

static isom_print_box_t isom_select_print_func( isom_box_t *box )
{
    if( box->manager & 0x01 )
        return isom_print_unknown;
    if( box->parent )
    {
        if( box->parent->type == ISOM_BOX_TYPE_STSD )
            switch( box->type )
            {
                case ISOM_CODEC_TYPE_AVC1_VIDEO :
                case ISOM_CODEC_TYPE_AVC2_VIDEO :
                case ISOM_CODEC_TYPE_AVCP_VIDEO :
                case ISOM_CODEC_TYPE_DRAC_VIDEO :
                case ISOM_CODEC_TYPE_ENCV_VIDEO :
                case ISOM_CODEC_TYPE_MJP2_VIDEO :
                case ISOM_CODEC_TYPE_MP4V_VIDEO :
                case ISOM_CODEC_TYPE_MVC1_VIDEO :
                case ISOM_CODEC_TYPE_MVC2_VIDEO :
                case ISOM_CODEC_TYPE_S263_VIDEO :
                case ISOM_CODEC_TYPE_SVC1_VIDEO :
                case ISOM_CODEC_TYPE_VC_1_VIDEO :
                    return isom_print_visual_description;
                case ISOM_CODEC_TYPE_AC_3_AUDIO :
                case ISOM_CODEC_TYPE_ALAC_AUDIO :
                case ISOM_CODEC_TYPE_DRA1_AUDIO :
                case ISOM_CODEC_TYPE_DTSC_AUDIO :
                case ISOM_CODEC_TYPE_DTSH_AUDIO :
                case ISOM_CODEC_TYPE_DTSL_AUDIO :
                case ISOM_CODEC_TYPE_DTSE_AUDIO :
                case ISOM_CODEC_TYPE_EC_3_AUDIO :
                case ISOM_CODEC_TYPE_ENCA_AUDIO :
                case ISOM_CODEC_TYPE_G719_AUDIO :
                case ISOM_CODEC_TYPE_G726_AUDIO :
                case ISOM_CODEC_TYPE_M4AE_AUDIO :
                case ISOM_CODEC_TYPE_MLPA_AUDIO :
                case ISOM_CODEC_TYPE_MP4A_AUDIO :
                //case ISOM_CODEC_TYPE_RAW_AUDIO  :
                case ISOM_CODEC_TYPE_SAMR_AUDIO :
                case ISOM_CODEC_TYPE_SAWB_AUDIO :
                case ISOM_CODEC_TYPE_SAWP_AUDIO :
                case ISOM_CODEC_TYPE_SEVC_AUDIO :
                case ISOM_CODEC_TYPE_SQCP_AUDIO :
                case ISOM_CODEC_TYPE_SSMV_AUDIO :
                //case ISOM_CODEC_TYPE_TWOS_AUDIO :
                case QT_CODEC_TYPE_23NI_AUDIO :
                case QT_CODEC_TYPE_MAC3_AUDIO :
                case QT_CODEC_TYPE_MAC6_AUDIO :
                case QT_CODEC_TYPE_NONE_AUDIO :
                case QT_CODEC_TYPE_QDM2_AUDIO :
                case QT_CODEC_TYPE_QDMC_AUDIO :
                case QT_CODEC_TYPE_QCLP_AUDIO :
                case QT_CODEC_TYPE_AGSM_AUDIO :
                case QT_CODEC_TYPE_ALAW_AUDIO :
                case QT_CODEC_TYPE_CDX2_AUDIO :
                case QT_CODEC_TYPE_CDX4_AUDIO :
                case QT_CODEC_TYPE_DVCA_AUDIO :
                case QT_CODEC_TYPE_DVI_AUDIO  :
                case QT_CODEC_TYPE_FL32_AUDIO :
                case QT_CODEC_TYPE_FL64_AUDIO :
                case QT_CODEC_TYPE_IMA4_AUDIO :
                case QT_CODEC_TYPE_IN24_AUDIO :
                case QT_CODEC_TYPE_IN32_AUDIO :
                case QT_CODEC_TYPE_LPCM_AUDIO :
                case QT_CODEC_TYPE_RAW_AUDIO :
                case QT_CODEC_TYPE_SOWT_AUDIO :
                case QT_CODEC_TYPE_TWOS_AUDIO :
                case QT_CODEC_TYPE_ULAW_AUDIO :
                case QT_CODEC_TYPE_VDVA_AUDIO :
                case QT_CODEC_TYPE_FULLMP3_AUDIO :
                case QT_CODEC_TYPE_MP3_AUDIO :
                case QT_CODEC_TYPE_ADPCM2_AUDIO :
                case QT_CODEC_TYPE_ADPCM17_AUDIO :
                case QT_CODEC_TYPE_GSM49_AUDIO :
                case QT_CODEC_TYPE_NOT_SPECIFIED :
                    return isom_print_audio_description;
                case QT_CODEC_TYPE_TEXT_TEXT :
                    return isom_print_text_description;
                case ISOM_CODEC_TYPE_TX3G_TEXT :
                    return isom_print_tx3g_description;
                default :
                    return isom_print_unknown;
            }
        if( box->parent->type == QT_BOX_TYPE_WAVE )
            switch( box->type )
            {
                case QT_BOX_TYPE_FRMA :
                    return isom_print_frma;
                case QT_BOX_TYPE_ENDA :
                    return isom_print_enda;
                case ISOM_BOX_TYPE_ESDS :
                    return isom_print_esds;
                case QT_BOX_TYPE_TERMINATOR :
                    return isom_print_terminator;
                default :
                    return isom_print_unknown;
            }
        if( box->parent->type == ISOM_BOX_TYPE_TREF )
            return isom_print_track_reference_type;
    }
    switch( box->type )
    {
        case ISOM_BOX_TYPE_FTYP :
            return isom_print_ftyp;
        case ISOM_BOX_TYPE_MOOV :
            return isom_print_moov;
        case ISOM_BOX_TYPE_MVHD :
            return isom_print_mvhd;
        case ISOM_BOX_TYPE_IODS :
            return isom_print_iods;
        case ISOM_BOX_TYPE_ESDS :
            return isom_print_esds;
        case ISOM_BOX_TYPE_TRAK :
            return isom_print_trak;
        case ISOM_BOX_TYPE_TKHD :
            return isom_print_tkhd;
        case QT_BOX_TYPE_TAPT :
            return isom_print_tapt;
        case QT_BOX_TYPE_CLEF :
            return isom_print_clef;
        case QT_BOX_TYPE_PROF :
            return isom_print_prof;
        case QT_BOX_TYPE_ENOF :
            return isom_print_enof;
        case ISOM_BOX_TYPE_EDTS :
            return isom_print_edts;
        case ISOM_BOX_TYPE_ELST :
            return isom_print_elst;
        case ISOM_BOX_TYPE_TREF :
            return isom_print_tref;
        case ISOM_BOX_TYPE_MDIA :
            return isom_print_mdia;
        case ISOM_BOX_TYPE_MDHD :
            return isom_print_mdhd;
        case ISOM_BOX_TYPE_HDLR :
            return isom_print_hdlr;
        case ISOM_BOX_TYPE_MINF :
            return isom_print_minf;
        case ISOM_BOX_TYPE_VMHD :
            return isom_print_vmhd;
        case ISOM_BOX_TYPE_SMHD :
            return isom_print_smhd;
        case ISOM_BOX_TYPE_HMHD :
            return isom_print_hmhd;
        case ISOM_BOX_TYPE_NMHD :
            return isom_print_nmhd;
        case QT_BOX_TYPE_GMHD :
            return isom_print_gmhd;
        case QT_BOX_TYPE_GMIN :
            return isom_print_gmin;
        case QT_BOX_TYPE_TEXT :
            return isom_print_text;
        case ISOM_BOX_TYPE_DINF :
            return isom_print_dinf;
        case ISOM_BOX_TYPE_DREF :
            return isom_print_dref;
        case ISOM_BOX_TYPE_URL  :
            return isom_print_url;
        case ISOM_BOX_TYPE_STBL :
            return isom_print_stbl;
        case ISOM_BOX_TYPE_STSD :
            return isom_print_stsd;
        case ISOM_BOX_TYPE_BTRT :
            return isom_print_btrt;
        case ISOM_BOX_TYPE_CLAP :
            return isom_print_clap;
        case ISOM_BOX_TYPE_PASP :
            return isom_print_pasp;
        case QT_BOX_TYPE_COLR :
            return isom_print_colr;
        case ISOM_BOX_TYPE_STSL :
            return isom_print_stsl;
        case ISOM_BOX_TYPE_AVCC :
            return isom_print_avcC;
        case QT_BOX_TYPE_WAVE :
            return isom_print_wave;
        case QT_BOX_TYPE_CHAN :
            return isom_print_chan;
        case ISOM_BOX_TYPE_FTAB :
            return isom_print_ftab;
        case ISOM_BOX_TYPE_STTS :
            return isom_print_stts;
        case ISOM_BOX_TYPE_CTTS :
            return isom_print_ctts;
        case ISOM_BOX_TYPE_CSLG :
            return isom_print_cslg;
        case ISOM_BOX_TYPE_STSS :
            return isom_print_stss;
        case QT_BOX_TYPE_STPS :
            return isom_print_stps;
        case ISOM_BOX_TYPE_SDTP :
            return isom_print_sdtp;
        case ISOM_BOX_TYPE_STSC :
            return isom_print_stsc;
        case ISOM_BOX_TYPE_STSZ :
            return isom_print_stsz;
        case ISOM_BOX_TYPE_STCO :
        case ISOM_BOX_TYPE_CO64 :
            return isom_print_stco;
        case ISOM_BOX_TYPE_SGPD :
            return isom_print_sgpd;
        case ISOM_BOX_TYPE_SBGP :
            return isom_print_sbgp;
        case ISOM_BOX_TYPE_UDTA :
            return isom_print_udta;
        case ISOM_BOX_TYPE_CHPL :
            return isom_print_chpl;
        case ISOM_BOX_TYPE_MVEX :
            return isom_print_mvex;
        case ISOM_BOX_TYPE_MEHD :
            return isom_print_mehd;
        case ISOM_BOX_TYPE_TREX :
            return isom_print_trex;
        case ISOM_BOX_TYPE_MOOF :
            return isom_print_moof;
        case ISOM_BOX_TYPE_MFHD :
            return isom_print_mfhd;
        case ISOM_BOX_TYPE_TRAF :
            return isom_print_traf;
        case ISOM_BOX_TYPE_TFHD :
            return isom_print_tfhd;
        case ISOM_BOX_TYPE_TRUN :
            return isom_print_trun;
        case ISOM_BOX_TYPE_FREE :
        case ISOM_BOX_TYPE_SKIP :
            return isom_print_free;
        case ISOM_BOX_TYPE_MDAT :
            return isom_print_mdat;
        case ISOM_BOX_TYPE_MFRA :
            return isom_print_mfra;
        case ISOM_BOX_TYPE_TFRA :
            return isom_print_tfra;
        case ISOM_BOX_TYPE_MFRO :
            return isom_print_mfro;
        default :
            return isom_print_unknown;
    }
}

static int isom_add_print_func( lsmash_root_t *root, void *box, int level )
{
    if( !(root->flags & LSMASH_FILE_MODE_DUMP) )
        return 0;
    isom_print_entry_t *data = malloc( sizeof(isom_print_entry_t) );
    if( !data )
        return -1;
    data->level = level;
    data->box = (isom_box_t *)box;
    data->func = isom_select_print_func( (isom_box_t *)box );
    if( !data->func || lsmash_add_entry( root->print, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static void isom_remove_print_func( isom_print_entry_t *data )
{
    if( !data || !data->box )
        return;
    if( data->box->manager & 0x02 )
        free( data->box );      /* free flagged box */
    free( data );
}

static void isom_remove_print_funcs( lsmash_root_t *root )
{
    lsmash_remove_list( root->print, isom_remove_print_func );
    root->print = NULL;
}

static int isom_read_box( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, uint64_t parent_pos, int level );

static int isom_bs_read_box_common( lsmash_bs_t *bs, isom_box_t *box )
{
    box->pos = lsmash_ftell( bs->stream );
    /* read size and type */
    if( lsmash_bs_read_data( bs, ISOM_DEFAULT_BOX_HEADER_SIZE ) )
        return -1;
    if( feof( bs->stream ) )
        return 1;
    box->size = lsmash_bs_get_be32( bs );
    box->type = lsmash_bs_get_be32( bs );
    if( box->size == 1 )
    {
        if( lsmash_bs_read_data( bs, sizeof(uint64_t) ) )
            return -1;
        box->size = lsmash_bs_get_be64( bs );
    }
    if( !box->size )
        box->size = UINT64_MAX;
    /* read version and flags */
    if( isom_is_fullbox( box ) )
    {
        if( lsmash_bs_read_data( bs, sizeof(uint32_t) ) )
            return -1;
        box->version = lsmash_bs_get_byte( bs );
        box->flags   = lsmash_bs_get_be24( bs );
    }
    return 0;
}

static void isom_basebox_common_copy( isom_box_t *dst, isom_box_t *src )
{
    dst->root     = src->root;
    dst->parent   = src->parent;
    dst->manager  = src->manager;
    dst->pos      = src->pos;
    dst->size     = src->size;
    dst->type     = src->type;
    dst->usertype = src->usertype;
}

static void isom_fullbox_common_copy( isom_box_t *dst, isom_box_t *src )
{
    dst->root     = src->root;
    dst->parent   = src->parent;
    dst->manager  = src->manager;
    dst->pos      = src->pos;
    dst->size     = src->size;
    dst->type     = src->type;
    dst->usertype = src->usertype;
    dst->version  = src->version;
    dst->flags    = src->flags;
}

static void isom_box_common_copy( void *dst, void *src )
{
    if( src && ((isom_box_t *)src)->type == ISOM_BOX_TYPE_STSD )
    {
        isom_basebox_common_copy( (isom_box_t *)dst, (isom_box_t *)src );
        return;
    }
    if( isom_is_fullbox( src ) )
        isom_fullbox_common_copy( (isom_box_t *)dst, (isom_box_t *)src );
    else
        isom_basebox_common_copy( (isom_box_t *)dst, (isom_box_t *)src );
}

static void isom_read_box_rest( lsmash_bs_t *bs, isom_box_t *box )
{
    if( lsmash_bs_read_data( bs, box->size - lsmash_bs_get_pos( bs ) ) )
        return;
    if( box->size != bs->store )  /* not match size */
        bs->error = 1;
}

static void isom_skip_box_rest( lsmash_bs_t *bs, isom_box_t *box )
{
    uint64_t skip_bytes = box->size - lsmash_bs_get_pos( bs );
    if( bs->stream != stdin )
        lsmash_fseek( bs->stream, skip_bytes, SEEK_CUR );
    else
        for( uint64_t i = 0; i < skip_bytes; i++ )
            if( fgetc( stdin ) == EOF )
                break;
}

static int isom_read_children( lsmash_root_t *root, isom_box_t *box, void *parent, int level )
{
    int ret;
    lsmash_bs_t *bs = root->bs;
    isom_box_t *parent_box = (isom_box_t *)parent;
    uint64_t parent_pos = lsmash_bs_get_pos( bs );
    while( !(ret = isom_read_box( root, box, parent_box, parent_pos, level )) )
    {
        parent_pos += box->size;
        if( parent_box->size <= parent_pos || bs->error )
            break;
    }
    box->size = parent_pos;    /* for ROOT size */
    return ret;
}

static int isom_read_unknown_box( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    isom_box_t *unknown = malloc( sizeof(isom_box_t) );
    if( !unknown )
        return -1;
    memset( unknown, 0, sizeof(isom_box_t) );
    lsmash_bs_t *bs = root->bs;
    isom_skip_box_rest( bs, box );
    unknown->parent = parent;
    unknown->size = box->size;
    unknown->type = box->type;
    unknown->manager = 0x03;   /* add unknown box flag + free flag */
    if( isom_add_print_func( root, unknown, level ) )
    {
        free( unknown );
        return -1;
    }
    return 0;
}

static int isom_read_ftyp( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( !!parent->type )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( ftyp, parent, box->type );
    ((lsmash_root_t *)parent)->ftyp = ftyp;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    ftyp->major_brand              = lsmash_bs_get_be32( bs );
    ftyp->minor_version            = lsmash_bs_get_be32( bs );
    uint64_t pos = lsmash_bs_get_pos( bs );
    ftyp->brand_count = box->size > pos ? (box->size - pos) / sizeof(uint32_t) : 0;
    ftyp->compatible_brands = ftyp->brand_count ? malloc( ftyp->brand_count * sizeof(uint32_t) ) : NULL;
    if( !ftyp->compatible_brands )
        return -1;
    for( uint32_t i = 0; i < ftyp->brand_count; i++ )
        ftyp->compatible_brands[i] = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( ftyp, box );
    return isom_add_print_func( root, ftyp, level );
}

static int isom_read_moov( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( !!parent->type )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( moov, parent, box->type );
    ((lsmash_root_t *)parent)->moov = moov;
    isom_box_common_copy( moov, box );
    if( isom_add_print_func( root, moov, level ) )
        return -1;
    return isom_read_children( root, box, moov, level );
}

static int isom_read_mvhd( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MOOV )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( mvhd, parent, box->type );
    ((isom_moov_t *)parent)->mvhd = mvhd;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    if( box->version )
    {
        mvhd->creation_time     = lsmash_bs_get_be64( bs );
        mvhd->modification_time = lsmash_bs_get_be64( bs );
        mvhd->timescale         = lsmash_bs_get_be32( bs );
        mvhd->duration          = lsmash_bs_get_be64( bs );
    }
    else
    {
        mvhd->creation_time     = lsmash_bs_get_be32( bs );
        mvhd->modification_time = lsmash_bs_get_be32( bs );
        mvhd->timescale         = lsmash_bs_get_be32( bs );
        mvhd->duration          = lsmash_bs_get_be32( bs );
    }
    mvhd->rate              = lsmash_bs_get_be32( bs );
    mvhd->volume            = lsmash_bs_get_be16( bs );
    mvhd->reserved          = lsmash_bs_get_be16( bs );
    mvhd->preferredLong[0]  = lsmash_bs_get_be32( bs );
    mvhd->preferredLong[1]  = lsmash_bs_get_be32( bs );
    for( int i = 0; i < 9; i++ )
        mvhd->matrix[i]     = lsmash_bs_get_be32( bs );
    mvhd->previewTime       = lsmash_bs_get_be32( bs );
    mvhd->previewDuration   = lsmash_bs_get_be32( bs );
    mvhd->posterTime        = lsmash_bs_get_be32( bs );
    mvhd->selectionTime     = lsmash_bs_get_be32( bs );
    mvhd->selectionDuration = lsmash_bs_get_be32( bs );
    mvhd->currentTime       = lsmash_bs_get_be32( bs );
    mvhd->next_track_ID     = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( mvhd, box );
    return isom_add_print_func( root, mvhd, level );
}

static int isom_read_iods( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MOOV )
        return isom_read_unknown_box( root, box, parent, level );
    isom_box_t *iods = malloc( sizeof(isom_box_t) );
    if( !iods )
        return -1;
    memset( iods, 0, sizeof(isom_box_t) );
    lsmash_bs_t *bs = root->bs;
    isom_skip_box_rest( bs, box );
    box->manager |= 0x02;       /* add free flag */
    isom_box_common_copy( iods, box );
    if( isom_add_print_func( root, iods, level ) )
    {
        free( iods );
        return -1;
    }
    return 0;
}

static int isom_read_esds( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    isom_box_t *esds = malloc( sizeof(isom_box_t) );
    if( !esds )
        return -1;
    memset( esds, 0, sizeof(isom_box_t) );
    lsmash_bs_t *bs = root->bs;
    isom_skip_box_rest( bs, box );
    box->manager |= 0x02;       /* add free flag */
    isom_box_common_copy( esds, box );
    if( isom_add_print_func( root, esds, level ) )
    {
        free( esds );
        return -1;
    }
    return 0;
}

static int isom_read_trak( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MOOV )
        return isom_read_unknown_box( root, box, parent, level );
    lsmash_entry_list_t *list = ((isom_moov_t *)parent)->trak_list;
    if( !list )
    {
        list = lsmash_create_entry_list();
        if( !list )
            return -1;
        ((isom_moov_t *)parent)->trak_list = list;
    }
    isom_trak_entry_t *trak = malloc( sizeof(isom_trak_entry_t) );
    if( !trak )
        return -1;
    memset( trak, 0, sizeof(isom_trak_entry_t) );
    isom_cache_t *cache = malloc( sizeof(isom_cache_t) );
    if( !cache )
    {
        free( trak );
        return -1;
    }
    memset( cache, 0, sizeof(isom_cache_t) );
    trak->root = root;
    trak->cache = cache;
    if( lsmash_add_entry( list, trak ) )
    {
        free( trak->cache );
        free( trak );
        return -1;
    }
    box->parent = parent;
    isom_box_common_copy( trak, box );
    if( isom_add_print_func( root, trak, level ) )
        return -1;
    return isom_read_children( root, box, trak, level );
}

static int isom_read_tkhd( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_TRAK )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( tkhd, parent, box->type );
    ((isom_trak_entry_t *)parent)->tkhd = tkhd;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    if( box->version )
    {
        tkhd->creation_time     = lsmash_bs_get_be64( bs );
        tkhd->modification_time = lsmash_bs_get_be64( bs );
        tkhd->track_ID          = lsmash_bs_get_be32( bs );
        tkhd->reserved1         = lsmash_bs_get_be32( bs );
        tkhd->duration          = lsmash_bs_get_be64( bs );
    }
    else
    {
        tkhd->creation_time     = lsmash_bs_get_be32( bs );
        tkhd->modification_time = lsmash_bs_get_be32( bs );
        tkhd->track_ID          = lsmash_bs_get_be32( bs );
        tkhd->reserved1         = lsmash_bs_get_be32( bs );
        tkhd->duration          = lsmash_bs_get_be32( bs );
    }
    tkhd->reserved2[0]    = lsmash_bs_get_be32( bs );
    tkhd->reserved2[1]    = lsmash_bs_get_be32( bs );
    tkhd->layer           = lsmash_bs_get_be16( bs );
    tkhd->alternate_group = lsmash_bs_get_be16( bs );
    tkhd->volume          = lsmash_bs_get_be16( bs );
    tkhd->reserved3       = lsmash_bs_get_be16( bs );
    for( int i = 0; i < 9; i++ )
        tkhd->matrix[i]   = lsmash_bs_get_be32( bs );
    tkhd->width           = lsmash_bs_get_be32( bs );
    tkhd->height          = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( tkhd, box );
    return isom_add_print_func( root, tkhd, level );
}

static int isom_read_tapt( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_TRAK )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( tapt, parent, box->type );
    ((isom_trak_entry_t *)parent)->tapt = tapt;
    isom_box_common_copy( tapt, box );
    if( isom_add_print_func( root, tapt, level ) )
        return -1;
    return isom_read_children( root, box, tapt, level );
}

static int isom_read_clef( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != QT_BOX_TYPE_TAPT )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( clef, parent, box->type );
    ((isom_tapt_t *)parent)->clef = clef;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    clef->width  = lsmash_bs_get_be32( bs );
    clef->height = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( clef, box );
    return isom_add_print_func( root, clef, level );
}

static int isom_read_prof( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != QT_BOX_TYPE_TAPT )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( prof, parent, box->type );
    ((isom_tapt_t *)parent)->prof = prof;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    prof->width  = lsmash_bs_get_be32( bs );
    prof->height = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( prof, box );
    return isom_add_print_func( root, prof, level );
}

static int isom_read_enof( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != QT_BOX_TYPE_TAPT )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( enof, parent, box->type );
    ((isom_tapt_t *)parent)->enof = enof;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    enof->width  = lsmash_bs_get_be32( bs );
    enof->height = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( enof, box );
    return isom_add_print_func( root, enof, level );
}

static int isom_read_edts( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_TRAK )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( edts, parent, box->type );
    ((isom_trak_entry_t *)parent)->edts = edts;
    isom_box_common_copy( edts, box );
    if( isom_add_print_func( root, edts, level ) )
        return -1;
    return isom_read_children( root, box, edts, level );
}

static int isom_read_elst( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_EDTS )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_list_box( elst, parent, box->type );
    ((isom_edts_t *)parent)->elst = elst;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    uint32_t entry_count = lsmash_bs_get_be32( bs );
    uint64_t pos;
    for( pos = lsmash_bs_get_pos( bs ); pos < box->size; pos = lsmash_bs_get_pos( bs ) )
    {
        isom_elst_entry_t *data = malloc( sizeof(isom_elst_entry_t) );
        if( !data || lsmash_add_entry( elst->list, data ) )
        {
            if( data )
                free( data );
            return -1;
        }
        memset( data, 0, sizeof(isom_elst_entry_t) );
        if( box->version == 1 )
        {
            data->segment_duration = lsmash_bs_get_be64( bs );
            data->media_time       = lsmash_bs_get_be64( bs );
        }
        else
        {
            data->segment_duration = lsmash_bs_get_be32( bs );
            data->media_time       = lsmash_bs_get_be32( bs );
        }
        data->media_rate = lsmash_bs_get_be32( bs );
    }
    if( entry_count != elst->list->entry_count || box->size < pos )
        printf( "[elst] box has extra bytes: %"PRId64"\n", pos - box->size );
    box->size = pos;
    isom_box_common_copy( elst, box );
    return isom_add_print_func( root, elst, level );
}

static int isom_read_tref( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_TRAK )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( tref, parent, box->type );
    ((isom_trak_entry_t *)parent)->tref = tref;
    isom_box_common_copy( tref, box );
    if( isom_add_print_func( root, tref, level ) )
        return -1;
    return isom_read_children( root, box, tref, level );
}

static int isom_read_track_reference_type( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_TREF )
        return isom_read_unknown_box( root, box, parent, level );
    isom_tref_t *tref = (isom_tref_t *)parent;
    lsmash_entry_list_t *list = tref->ref_list;
    if( !list )
    {
        list = lsmash_create_entry_list();
        if( !list )
            return -1;
        tref->ref_list = list;
    }
    isom_tref_type_t *ref = malloc( sizeof(isom_tref_type_t) );
    if( !ref )
        return -1;
    memset( ref, 0, sizeof(isom_tref_type_t) );
    if( lsmash_add_entry( list, ref ) )
    {
        free( ref );
        return -1;
    }
    lsmash_bs_t *bs = root->bs;
    ref->ref_count = (box->size - lsmash_bs_get_pos( bs ) ) / sizeof(uint32_t);
    if( ref->ref_count )
    {
        ref->track_ID = malloc( ref->ref_count * sizeof(uint32_t) );
        if( !ref->track_ID )
        {
            ref->ref_count = 0;
            return -1;
        }
        isom_read_box_rest( bs, box );
        for( uint32_t i = 0; i < ref->ref_count; i++ )
            ref->track_ID[i] = lsmash_bs_get_be32( bs );
    }
    uint64_t pos = lsmash_bs_get_pos( bs );
    if( box->size != pos )
        printf( "[%s] box has extra bytes: %"PRId64"\n", isom_4cc2str( box->type ), box->size - pos );
    box->size = pos;
    isom_box_common_copy( ref, box );
    return isom_add_print_func( root, ref, level );
}

static int isom_read_mdia( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_TRAK )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( mdia, parent, box->type );
    ((isom_trak_entry_t *)parent)->mdia = mdia;
    isom_box_common_copy( mdia, box );
    if( isom_add_print_func( root, mdia, level ) )
        return -1;
    return isom_read_children( root, box, mdia, level );
}

static int isom_read_mdhd( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MDIA )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( mdhd, parent, box->type );
    ((isom_mdia_t *)parent)->mdhd = mdhd;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    if( box->version )
    {
        mdhd->creation_time     = lsmash_bs_get_be64( bs );
        mdhd->modification_time = lsmash_bs_get_be64( bs );
        mdhd->timescale         = lsmash_bs_get_be32( bs );
        mdhd->duration          = lsmash_bs_get_be64( bs );
    }
    else
    {
        mdhd->creation_time     = lsmash_bs_get_be32( bs );
        mdhd->modification_time = lsmash_bs_get_be32( bs );
        mdhd->timescale         = lsmash_bs_get_be32( bs );
        mdhd->duration          = lsmash_bs_get_be32( bs );
    }
    mdhd->language = lsmash_bs_get_be16( bs );
    mdhd->quality  = lsmash_bs_get_be16( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( mdhd, box );
    return isom_add_print_func( root, mdhd, level );
}

static int isom_read_hdlr( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MDIA
     //&& parent->type != ISOM_BOX_TYPE_META
     && parent->type != ISOM_BOX_TYPE_MINF )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( hdlr, parent, box->type );
    if( parent->type == ISOM_BOX_TYPE_MDIA )
        ((isom_mdia_t *)parent)->hdlr = hdlr;
    else
        ((isom_minf_t *)parent)->hdlr = hdlr;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    hdlr->componentType         = lsmash_bs_get_be32( bs );
    hdlr->componentSubtype      = lsmash_bs_get_be32( bs );
    hdlr->componentManufacturer = lsmash_bs_get_be32( bs );
    hdlr->componentFlags        = lsmash_bs_get_be32( bs );
    hdlr->componentFlagsMask    = lsmash_bs_get_be32( bs );
    uint64_t pos = lsmash_bs_get_pos( bs );
    hdlr->componentName_length = box->size - pos;
    if( hdlr->componentName_length )
    {
        hdlr->componentName = malloc( hdlr->componentName_length );
        if( !hdlr->componentName )
            return -1;
        for( uint32_t i = 0; pos < box->size; pos = lsmash_bs_get_pos( bs ) )
            hdlr->componentName[i++] = lsmash_bs_get_byte( bs );
    }
    box->size = pos;
    isom_box_common_copy( hdlr, box );
    return isom_add_print_func( root, hdlr, level );
}

static int isom_read_minf( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MDIA )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( minf, parent, box->type );
    ((isom_mdia_t *)parent)->minf = minf;
    isom_box_common_copy( minf, box );
    if( isom_add_print_func( root, minf, level ) )
        return -1;
    return isom_read_children( root, box, minf, level );
}

static int isom_read_vmhd( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MINF )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( vmhd, parent, box->type );
    ((isom_minf_t *)parent)->vmhd = vmhd;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    vmhd->graphicsmode   = lsmash_bs_get_be16( bs );
    for( int i = 0; i < 3; i++ )
        vmhd->opcolor[i] = lsmash_bs_get_be16( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( vmhd, box );
    return isom_add_print_func( root, vmhd, level );
}

static int isom_read_smhd( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MINF )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( smhd, parent, box->type );
    ((isom_minf_t *)parent)->smhd = smhd;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    smhd->balance  = lsmash_bs_get_be16( bs );
    smhd->reserved = lsmash_bs_get_be16( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( smhd, box );
    return isom_add_print_func( root, smhd, level );
}

static int isom_read_hmhd( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MINF )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( hmhd, parent, box->type );
    ((isom_minf_t *)parent)->hmhd = hmhd;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    hmhd->maxPDUsize = lsmash_bs_get_be16( bs );
    hmhd->avgPDUsize = lsmash_bs_get_be16( bs );
    hmhd->maxbitrate = lsmash_bs_get_be32( bs );
    hmhd->avgbitrate = lsmash_bs_get_be32( bs );
    hmhd->reserved   = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( hmhd, box );
    return isom_add_print_func( root, hmhd, level );
}

static int isom_read_nmhd( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MINF )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( nmhd, parent, box->type );
    ((isom_minf_t *)parent)->nmhd = nmhd;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( nmhd, box );
    return isom_add_print_func( root, nmhd, level );
}

static int isom_read_gmhd( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MINF )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( gmhd, parent, box->type );
    ((isom_minf_t *)parent)->gmhd = gmhd;
    isom_box_common_copy( gmhd, box );
    if( isom_add_print_func( root, gmhd, level ) )
        return -1;
    return isom_read_children( root, box, gmhd, level );
}

static int isom_read_gmin( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != QT_BOX_TYPE_GMHD )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( gmin, parent, box->type );
    ((isom_gmhd_t *)parent)->gmin = gmin;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    gmin->graphicsmode   = lsmash_bs_get_be16( bs );
    for( int i = 0; i < 3; i++ )
        gmin->opcolor[i] = lsmash_bs_get_be16( bs );
    gmin->balance        = lsmash_bs_get_be16( bs );
    gmin->reserved       = lsmash_bs_get_be16( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( gmin, box );
    return isom_add_print_func( root, gmin, level );
}

static int isom_read_text( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != QT_BOX_TYPE_GMHD )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( text, parent, box->type );
    ((isom_gmhd_t *)parent)->text = text;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    for( int i = 0; i < 9; i++ )
        text->matrix[i] = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( text, box );
    return isom_add_print_func( root, text, level );
}

static int isom_read_dinf( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MINF )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( dinf, parent, box->type );
    ((isom_minf_t *)parent)->dinf = dinf;
    isom_box_common_copy( dinf, box );
    if( isom_add_print_func( root, dinf, level ) )
        return -1;
    return isom_read_children( root, box, dinf, level );
}

static int isom_read_dref( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_DINF /*&& parent->type != ISOM_BOX_TYPE_META*/ )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_list_box( dref, parent, box->type );
    ((isom_dinf_t *)parent)->dref = dref;
    lsmash_bs_t *bs = root->bs;
    if( lsmash_bs_read_data( bs, sizeof(uint32_t) ) )
        return -1;
    dref->list->entry_count = lsmash_bs_get_be32( bs );
    isom_box_common_copy( dref, box );
    if( isom_add_print_func( root, dref, level ) )
        return -1;
    return isom_read_children( root, box, dref, level );
}

static int isom_read_url( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_DREF )
        return isom_read_unknown_box( root, box, parent, level );
    lsmash_entry_list_t *list = ((isom_dref_t *)parent)->list;
    if( !list )
        return -1;
    isom_dref_entry_t *url = malloc( sizeof(isom_dref_entry_t) );
    if( !url )
        return -1;
    memset( url, 0, sizeof(isom_dref_entry_t) );
    if( !list->head )
        list->entry_count = 0;      /* discard entry_count gotten from the file */
    if( lsmash_add_entry( list, url ) )
    {
        free( url );
        return -1;
    }
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    uint64_t pos = lsmash_bs_get_pos( bs );
    url->location_length = box->size - pos;
    if( url->location_length )
    {
        url->location = malloc( url->location_length );
        if( !url->location )
            return -1;
        for( uint32_t i = 0; pos < box->size; pos = lsmash_bs_get_pos( bs ) )
            url->location[i++] = lsmash_bs_get_byte( bs );
    }
    box->size = pos;
    box->parent = parent;
    isom_box_common_copy( url, box );
    return isom_add_print_func( root, url, level );
}

static int isom_read_stbl( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MINF )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( stbl, parent, box->type );
    ((isom_minf_t *)parent)->stbl = stbl;
    isom_box_common_copy( stbl, box );
    if( isom_add_print_func( root, stbl, level ) )
        return -1;
    return isom_read_children( root, box, stbl, level );
}

static int isom_read_stsd( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STBL )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_list_box( stsd, parent, box->type );
    ((isom_stbl_t *)parent)->stsd = stsd;
    lsmash_bs_t *bs = root->bs;
    if( lsmash_bs_read_data( bs, sizeof(uint32_t) ) )
        return -1;
    stsd->list->entry_count = lsmash_bs_get_be32( bs );
    isom_box_common_copy( stsd, box );
    if( isom_add_print_func( root, stsd, level ) )
        return -1;
    return isom_read_children( root, box, stsd, level );
}

static void *isom_sample_description_alloc( uint32_t sample_type )
{
    switch( sample_type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
        case ISOM_CODEC_TYPE_AVC2_VIDEO :
        case ISOM_CODEC_TYPE_AVCP_VIDEO :
        case ISOM_CODEC_TYPE_MVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC2_VIDEO :
            return malloc( sizeof(isom_avc_entry_t) );
        case ISOM_CODEC_TYPE_MP4V_VIDEO :
            return malloc( sizeof(isom_mp4v_entry_t) );
        case ISOM_CODEC_TYPE_DRAC_VIDEO :
        case ISOM_CODEC_TYPE_ENCV_VIDEO :
        case ISOM_CODEC_TYPE_MJP2_VIDEO :
        case ISOM_CODEC_TYPE_S263_VIDEO :
        case ISOM_CODEC_TYPE_SVC1_VIDEO :
        case ISOM_CODEC_TYPE_VC_1_VIDEO :
            return malloc( sizeof(isom_visual_entry_t) );
        case ISOM_CODEC_TYPE_MP4A_AUDIO :
            return malloc( sizeof(isom_audio_entry_t) );
        case ISOM_CODEC_TYPE_AC_3_AUDIO :
        case ISOM_CODEC_TYPE_ALAC_AUDIO :
        case ISOM_CODEC_TYPE_DRA1_AUDIO :
        case ISOM_CODEC_TYPE_DTSC_AUDIO :
        case ISOM_CODEC_TYPE_DTSH_AUDIO :
        case ISOM_CODEC_TYPE_DTSL_AUDIO :
        case ISOM_CODEC_TYPE_DTSE_AUDIO :
        case ISOM_CODEC_TYPE_EC_3_AUDIO :
        case ISOM_CODEC_TYPE_ENCA_AUDIO :
        case ISOM_CODEC_TYPE_G719_AUDIO :
        case ISOM_CODEC_TYPE_G726_AUDIO :
        case ISOM_CODEC_TYPE_M4AE_AUDIO :
        case ISOM_CODEC_TYPE_MLPA_AUDIO :
        //case ISOM_CODEC_TYPE_RAW_AUDIO  :
        case ISOM_CODEC_TYPE_SAMR_AUDIO :
        case ISOM_CODEC_TYPE_SAWB_AUDIO :
        case ISOM_CODEC_TYPE_SAWP_AUDIO :
        case ISOM_CODEC_TYPE_SEVC_AUDIO :
        case ISOM_CODEC_TYPE_SQCP_AUDIO :
        case ISOM_CODEC_TYPE_SSMV_AUDIO :
        //case ISOM_CODEC_TYPE_TWOS_AUDIO :
        case QT_CODEC_TYPE_23NI_AUDIO :
        case QT_CODEC_TYPE_MAC3_AUDIO :
        case QT_CODEC_TYPE_MAC6_AUDIO :
        case QT_CODEC_TYPE_NONE_AUDIO :
        case QT_CODEC_TYPE_QDM2_AUDIO :
        case QT_CODEC_TYPE_QDMC_AUDIO :
        case QT_CODEC_TYPE_QCLP_AUDIO :
        case QT_CODEC_TYPE_AGSM_AUDIO :
        case QT_CODEC_TYPE_ALAW_AUDIO :
        case QT_CODEC_TYPE_CDX2_AUDIO :
        case QT_CODEC_TYPE_CDX4_AUDIO :
        case QT_CODEC_TYPE_DVCA_AUDIO :
        case QT_CODEC_TYPE_DVI_AUDIO  :
        case QT_CODEC_TYPE_FL32_AUDIO :
        case QT_CODEC_TYPE_FL64_AUDIO :
        case QT_CODEC_TYPE_IMA4_AUDIO :
        case QT_CODEC_TYPE_IN24_AUDIO :
        case QT_CODEC_TYPE_IN32_AUDIO :
        case QT_CODEC_TYPE_LPCM_AUDIO :
        case QT_CODEC_TYPE_RAW_AUDIO :
        case QT_CODEC_TYPE_SOWT_AUDIO :
        case QT_CODEC_TYPE_TWOS_AUDIO :
        case QT_CODEC_TYPE_ULAW_AUDIO :
        case QT_CODEC_TYPE_VDVA_AUDIO :
        case QT_CODEC_TYPE_FULLMP3_AUDIO :
        case QT_CODEC_TYPE_MP3_AUDIO :
        case QT_CODEC_TYPE_ADPCM2_AUDIO :
        case QT_CODEC_TYPE_ADPCM17_AUDIO :
        case QT_CODEC_TYPE_GSM49_AUDIO :
        case QT_CODEC_TYPE_NOT_SPECIFIED :
            return malloc( sizeof(isom_audio_entry_t) );
        case ISOM_CODEC_TYPE_TX3G_TEXT :
            return malloc( sizeof(isom_tx3g_entry_t) );
        case QT_CODEC_TYPE_TEXT_TEXT :
            return malloc( sizeof(isom_text_entry_t) );
        default :
            return NULL;
    }
}

static void isom_sample_description_init( void *sample, uint32_t sample_type )
{
    switch( sample_type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
        case ISOM_CODEC_TYPE_AVC2_VIDEO :
        case ISOM_CODEC_TYPE_AVCP_VIDEO :
        case ISOM_CODEC_TYPE_MVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC2_VIDEO :
            memset( sample, 0, sizeof(isom_avc_entry_t) );
            break;
        case ISOM_CODEC_TYPE_MP4V_VIDEO :
            memset( sample, 0, sizeof(isom_mp4v_entry_t) );
            break;
        case ISOM_CODEC_TYPE_DRAC_VIDEO :
        case ISOM_CODEC_TYPE_ENCV_VIDEO :
        case ISOM_CODEC_TYPE_MJP2_VIDEO :
        case ISOM_CODEC_TYPE_S263_VIDEO :
        case ISOM_CODEC_TYPE_SVC1_VIDEO :
        case ISOM_CODEC_TYPE_VC_1_VIDEO :
            memset( sample, 0, sizeof(isom_visual_entry_t) );
            break;
        case ISOM_CODEC_TYPE_MP4A_AUDIO :
            memset( sample, 0, sizeof(isom_audio_entry_t) );
            break;
        case ISOM_CODEC_TYPE_AC_3_AUDIO :
        case ISOM_CODEC_TYPE_ALAC_AUDIO :
        case ISOM_CODEC_TYPE_DRA1_AUDIO :
        case ISOM_CODEC_TYPE_DTSC_AUDIO :
        case ISOM_CODEC_TYPE_DTSH_AUDIO :
        case ISOM_CODEC_TYPE_DTSL_AUDIO :
        case ISOM_CODEC_TYPE_DTSE_AUDIO :
        case ISOM_CODEC_TYPE_EC_3_AUDIO :
        case ISOM_CODEC_TYPE_ENCA_AUDIO :
        case ISOM_CODEC_TYPE_G719_AUDIO :
        case ISOM_CODEC_TYPE_G726_AUDIO :
        case ISOM_CODEC_TYPE_M4AE_AUDIO :
        case ISOM_CODEC_TYPE_MLPA_AUDIO :
        //case ISOM_CODEC_TYPE_RAW_AUDIO  :
        case ISOM_CODEC_TYPE_SAMR_AUDIO :
        case ISOM_CODEC_TYPE_SAWB_AUDIO :
        case ISOM_CODEC_TYPE_SAWP_AUDIO :
        case ISOM_CODEC_TYPE_SEVC_AUDIO :
        case ISOM_CODEC_TYPE_SQCP_AUDIO :
        case ISOM_CODEC_TYPE_SSMV_AUDIO :
        //case ISOM_CODEC_TYPE_TWOS_AUDIO :
        case QT_CODEC_TYPE_23NI_AUDIO :
        case QT_CODEC_TYPE_MAC3_AUDIO :
        case QT_CODEC_TYPE_MAC6_AUDIO :
        case QT_CODEC_TYPE_NONE_AUDIO :
        case QT_CODEC_TYPE_QDM2_AUDIO :
        case QT_CODEC_TYPE_QDMC_AUDIO :
        case QT_CODEC_TYPE_QCLP_AUDIO :
        case QT_CODEC_TYPE_AGSM_AUDIO :
        case QT_CODEC_TYPE_ALAW_AUDIO :
        case QT_CODEC_TYPE_CDX2_AUDIO :
        case QT_CODEC_TYPE_CDX4_AUDIO :
        case QT_CODEC_TYPE_DVCA_AUDIO :
        case QT_CODEC_TYPE_DVI_AUDIO  :
        case QT_CODEC_TYPE_FL32_AUDIO :
        case QT_CODEC_TYPE_FL64_AUDIO :
        case QT_CODEC_TYPE_IMA4_AUDIO :
        case QT_CODEC_TYPE_IN24_AUDIO :
        case QT_CODEC_TYPE_IN32_AUDIO :
        case QT_CODEC_TYPE_LPCM_AUDIO :
        case QT_CODEC_TYPE_RAW_AUDIO :
        case QT_CODEC_TYPE_SOWT_AUDIO :
        case QT_CODEC_TYPE_TWOS_AUDIO :
        case QT_CODEC_TYPE_ULAW_AUDIO :
        case QT_CODEC_TYPE_VDVA_AUDIO :
        case QT_CODEC_TYPE_FULLMP3_AUDIO :
        case QT_CODEC_TYPE_MP3_AUDIO :
        case QT_CODEC_TYPE_ADPCM2_AUDIO :
        case QT_CODEC_TYPE_ADPCM17_AUDIO :
        case QT_CODEC_TYPE_GSM49_AUDIO :
        case QT_CODEC_TYPE_NOT_SPECIFIED :
            memset( sample, 0, sizeof(isom_audio_entry_t) );
            break;
        case ISOM_CODEC_TYPE_TX3G_TEXT :
            memset( sample, 0, sizeof(isom_tx3g_entry_t) );
            break;
        case QT_CODEC_TYPE_TEXT_TEXT :
            memset( sample, 0, sizeof(isom_text_entry_t) );
            break;
        default :
            break;
    }
}

static void *isom_add_description( uint32_t sample_type, lsmash_entry_list_t *list )
{
    if( !list )
        return NULL;
    void *sample = isom_sample_description_alloc( sample_type );
    if( !sample )
        return NULL;
    isom_sample_description_init( sample, sample_type );
    if( !list->head )
        list->entry_count = 0;      /* discard entry_count gotten from the file */
    if( lsmash_add_entry( list, sample ) )
    {
        free( sample );
        return NULL;
    }
    return sample;
}

static int isom_read_visual_description( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STSD )
        return isom_read_unknown_box( root, box, parent, level );
    isom_visual_entry_t *visual = (isom_visual_entry_t *)isom_add_description( box->type, ((isom_stsd_t *)parent)->list );
    if( !visual )
        return -1;
    lsmash_bs_t *bs = root->bs;
    if( lsmash_bs_read_data( bs, 78 ) )
        return -1;
    for( int i = 0; i < 6; i++ )
        visual->reserved[i]       = lsmash_bs_get_byte( bs );
    visual->data_reference_index  = lsmash_bs_get_be16( bs );
    visual->version               = lsmash_bs_get_be16( bs );
    visual->revision_level        = lsmash_bs_get_be16( bs );
    visual->vendor                = lsmash_bs_get_be32( bs );
    visual->temporalQuality       = lsmash_bs_get_be32( bs );
    visual->spatialQuality        = lsmash_bs_get_be32( bs );
    visual->width                 = lsmash_bs_get_be16( bs );
    visual->height                = lsmash_bs_get_be16( bs );
    visual->horizresolution       = lsmash_bs_get_be32( bs );
    visual->vertresolution        = lsmash_bs_get_be32( bs );
    visual->dataSize              = lsmash_bs_get_be32( bs );
    visual->frame_count           = lsmash_bs_get_be16( bs );
    for( int i = 0; i < 32; i++ )
        visual->compressorname[i] = lsmash_bs_get_byte( bs );
    visual->depth                 = lsmash_bs_get_be16( bs );
    visual->color_table_ID        = lsmash_bs_get_be16( bs );
    box->parent = parent;
    isom_box_common_copy( visual, box );
    if( isom_add_print_func( root, visual, level ) )
        return -1;
    return isom_read_children( root, box, visual, level );
}

static int isom_read_btrt( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    isom_create_box( btrt, parent, box->type );
    ((isom_avc_entry_t *)parent)->btrt = btrt;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    btrt->bufferSizeDB = lsmash_bs_get_be32( bs );
    btrt->maxBitrate   = lsmash_bs_get_be32( bs );
    btrt->avgBitrate   = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( btrt, box );
    return isom_add_print_func( root, btrt, level );
}

static int isom_read_clap( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    isom_create_box( clap, parent, box->type );
    ((isom_visual_entry_t *)parent)->clap = clap;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    clap->cleanApertureWidthN  = lsmash_bs_get_be32( bs );
    clap->cleanApertureWidthD  = lsmash_bs_get_be32( bs );
    clap->cleanApertureHeightN = lsmash_bs_get_be32( bs );
    clap->cleanApertureHeightD = lsmash_bs_get_be32( bs );
    clap->horizOffN            = lsmash_bs_get_be32( bs );
    clap->horizOffD            = lsmash_bs_get_be32( bs );
    clap->vertOffN             = lsmash_bs_get_be32( bs );
    clap->vertOffD             = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( clap, box );
    return isom_add_print_func( root, clap, level );
}

static int isom_read_pasp( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    isom_create_box( pasp, parent, box->type );
    ((isom_visual_entry_t *)parent)->pasp = pasp;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    pasp->hSpacing = lsmash_bs_get_be32( bs );
    pasp->vSpacing = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( pasp, box );
    return isom_add_print_func( root, pasp, level );
}

static int isom_read_colr( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    isom_create_box( colr, parent, box->type );
    ((isom_visual_entry_t *)parent)->colr = colr;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    colr->color_parameter_type = lsmash_bs_get_be32( bs );
    if( colr->color_parameter_type == QT_COLOR_PARAMETER_TYPE_NCLC )
    {
        colr->primaries_index         = lsmash_bs_get_be16( bs );
        colr->transfer_function_index = lsmash_bs_get_be16( bs );
        colr->matrix_index            = lsmash_bs_get_be16( bs );
    }
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( colr, box );
    return isom_add_print_func( root, colr, level );
}

static int isom_read_stsl( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    isom_create_box( stsl, parent, box->type );
    ((isom_visual_entry_t *)parent)->stsl = stsl;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    stsl->constraint_flag  = lsmash_bs_get_byte( bs );
    stsl->scale_method     = lsmash_bs_get_byte( bs );
    stsl->display_center_x = lsmash_bs_get_be16( bs );
    stsl->display_center_y = lsmash_bs_get_be16( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( stsl, box );
    return isom_add_print_func( root, stsl, level );
}

static int isom_read_avcC_ps( lsmash_bs_t *bs, lsmash_entry_list_t *list, uint8_t entry_count )
{
    if( !list )
        return -1;
    for( uint8_t i = 0; i < entry_count; i++ )
    {
        isom_avcC_ps_entry_t *data = malloc( sizeof(isom_avcC_ps_entry_t) );
        if( !data || lsmash_add_entry( list, data ) )
            return -1;      /* don't free list, here */
        data->parameterSetLength  = lsmash_bs_get_be16( bs );
        data->parameterSetNALUnit = lsmash_bs_get_bytes( bs, data->parameterSetLength );
        if( !data->parameterSetNALUnit )
            return -1;      /* don't free list, here */
    }
    return 0;
}

static int isom_read_avcC( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    isom_create_box( avcC, parent, box->type );
    isom_avc_entry_t *avc = (isom_avc_entry_t *)parent;
    avc->avcC = avcC;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    avcC->configurationVersion       = lsmash_bs_get_byte( bs );
    avcC->AVCProfileIndication       = lsmash_bs_get_byte( bs );
    avcC->profile_compatibility      = lsmash_bs_get_byte( bs );
    avcC->AVCLevelIndication         = lsmash_bs_get_byte( bs );
    avcC->lengthSizeMinusOne         = lsmash_bs_get_byte( bs );
    avcC->numOfSequenceParameterSets = lsmash_bs_get_byte( bs );
    if( avcC->numOfSequenceParameterSets & 0x1f )
    {
        avcC->sequenceParameterSets = lsmash_create_entry_list();
        if( !avcC->sequenceParameterSets ||
            isom_read_avcC_ps( bs, avcC->sequenceParameterSets, avcC->numOfSequenceParameterSets & 0x1f ) )
            goto fail;
    }
    avcC->numOfPictureParameterSets  = lsmash_bs_get_byte( bs );
    if( avcC->numOfPictureParameterSets )
    {
        avcC->pictureParameterSets = lsmash_create_entry_list();
        if( !avcC->pictureParameterSets ||
            isom_read_avcC_ps( bs, avcC->pictureParameterSets, avcC->numOfPictureParameterSets ) )
            goto fail;
    }
    /* Note: there are too many files, in the world, that don't contain the following fields.*/
    if( ISOM_REQUIRES_AVCC_EXTENSION( avcC->AVCProfileIndication ) && lsmash_bs_get_pos( bs ) < box->size )
    {
        avcC->chroma_format                = lsmash_bs_get_byte( bs );
        avcC->bit_depth_luma_minus8        = lsmash_bs_get_byte( bs );
        avcC->bit_depth_chroma_minus8      = lsmash_bs_get_byte( bs );
        avcC->numOfSequenceParameterSetExt = lsmash_bs_get_byte( bs );
        if( avcC->numOfSequenceParameterSetExt )
        {
            avcC->sequenceParameterSetExt = lsmash_create_entry_list();
            if( !avcC->sequenceParameterSetExt ||
                isom_read_avcC_ps( bs, avcC->sequenceParameterSetExt, avcC->numOfSequenceParameterSetExt ) )
                goto fail;
        }
    }
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( avcC, box );
    return isom_add_print_func( root, avcC, level );
fail:
    lsmash_remove_list( avcC->sequenceParameterSets,   isom_remove_avcC_ps );
    lsmash_remove_list( avcC->pictureParameterSets,    isom_remove_avcC_ps );
    lsmash_remove_list( avcC->sequenceParameterSetExt, isom_remove_avcC_ps );
    free( avcC );
    avc->avcC = NULL;
    return -1;
}

static int isom_read_audio_description( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STSD )
        return isom_read_unknown_box( root, box, parent, level );
    isom_audio_entry_t *audio = (isom_audio_entry_t *)isom_add_description( box->type, ((isom_stsd_t *)parent)->list );
    if( !audio )
        return -1;
    lsmash_bs_t *bs = root->bs;
    if( lsmash_bs_read_data( bs, 28 ) )
        return -1;
    for( int i = 0; i < 6; i++ )
        audio->reserved[i]      = lsmash_bs_get_byte( bs );
    audio->data_reference_index = lsmash_bs_get_be16( bs );
    audio->version              = lsmash_bs_get_be16( bs );
    audio->revision_level       = lsmash_bs_get_be16( bs );
    audio->vendor               = lsmash_bs_get_be32( bs );
    audio->channelcount         = lsmash_bs_get_be16( bs );
    audio->samplesize           = lsmash_bs_get_be16( bs );
    audio->compression_ID       = lsmash_bs_get_be16( bs );
    audio->packet_size          = lsmash_bs_get_be16( bs );
    audio->samplerate           = lsmash_bs_get_be32( bs );
    if( audio->version == 1 )
    {
        if( lsmash_bs_read_data( bs, 16 ) )
            return -1;
        audio->samplesPerPacket = lsmash_bs_get_be32( bs );
        audio->bytesPerPacket   = lsmash_bs_get_be32( bs );
        audio->bytesPerFrame    = lsmash_bs_get_be32( bs );
        audio->bytesPerSample   = lsmash_bs_get_be32( bs );
    }
    else if( audio->version == 2 )
    {
        if( lsmash_bs_read_data( bs, 36 ) )
            return -1;
        audio->sizeOfStructOnly              = lsmash_bs_get_be32( bs );
        audio->audioSampleRate               = lsmash_bs_get_be64( bs );
        audio->numAudioChannels              = lsmash_bs_get_be32( bs );
        audio->always7F000000                = lsmash_bs_get_be32( bs );
        audio->constBitsPerChannel           = lsmash_bs_get_be32( bs );
        audio->formatSpecificFlags           = lsmash_bs_get_be32( bs );
        audio->constBytesPerAudioPacket      = lsmash_bs_get_be32( bs );
        audio->constLPCMFramesPerAudioPacket = lsmash_bs_get_be32( bs );
    }
    box->parent = parent;
    isom_box_common_copy( audio, box );
    if( isom_add_print_func( root, audio, level ) )
        return -1;
    return isom_read_children( root, box, audio, level );
}

static int isom_read_wave( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    isom_create_box( wave, parent, box->type );
    ((isom_audio_entry_t *)parent)->wave = wave;
    isom_box_common_copy( wave, box );
    if( isom_add_print_func( root, wave, level ) )
        return -1;
    return isom_read_children( root, box, wave, level );
}

static int isom_read_frma( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != QT_BOX_TYPE_WAVE )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( frma, parent, box->type );
    ((isom_wave_t *)parent)->frma = frma;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    frma->data_format = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( frma, box );
    return isom_add_print_func( root, frma, level );
}

static int isom_read_enda( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != QT_BOX_TYPE_WAVE )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( enda, parent, box->type );
    ((isom_wave_t *)parent)->enda = enda;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    enda->littleEndian = lsmash_bs_get_be16( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( enda, box );
    return isom_add_print_func( root, enda, level );
}

static int isom_read_audio_specific( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != QT_BOX_TYPE_WAVE )
        return isom_read_unknown_box( root, box, parent, level );
    isom_box_t *specific = malloc( sizeof(isom_box_t) );
    if( !specific )
        return -1;
    memset( specific, 0, sizeof(isom_box_t) );
    lsmash_bs_t *bs = root->bs;
    isom_skip_box_rest( bs, box );
    box->manager |= 0x02;       /* add free flag */
    isom_box_common_copy( specific, box );
    if( isom_add_print_func( root, specific, level ) )
    {
        free( specific );
        return -1;
    }
    return 0;
}

static int isom_read_terminator( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != QT_BOX_TYPE_WAVE )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( terminator, parent, box->type );
    ((isom_wave_t *)parent)->terminator = terminator;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( terminator, box );
    return isom_add_print_func( root, terminator, level );
}

static int isom_read_chan( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    isom_create_box( chan, parent, box->type );
    isom_audio_entry_t *audio = (isom_audio_entry_t *)parent;
    audio->chan = chan;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    chan->channelLayoutTag          = lsmash_bs_get_be32( bs );
    chan->channelBitmap             = lsmash_bs_get_be32( bs );
    chan->numberChannelDescriptions = lsmash_bs_get_be32( bs );
    if( chan->numberChannelDescriptions )
    {
        isom_channel_description_t *desc = malloc( chan->numberChannelDescriptions * sizeof(isom_channel_description_t) );
        if( !desc )
        {
            free( chan );
            audio->chan = NULL;
            return -1;
        }
        chan->channelDescriptions = desc;
        for( uint32_t i = 0; i < chan->numberChannelDescriptions; i++ )
        {
            desc->channelLabel       = lsmash_bs_get_be32( bs );
            desc->channelFlags       = lsmash_bs_get_be32( bs );
            for( int j = 0; j < 3; j++ )
                desc->coordinates[j] = lsmash_bs_get_be32( bs );
        }
    }
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( chan, box );
    return isom_add_print_func( root, chan, level );
}

static int isom_read_text_description( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STSD )
        return isom_read_unknown_box( root, box, parent, level );
    isom_text_entry_t *text = (isom_text_entry_t *)isom_add_description( box->type, ((isom_stsd_t *)parent)->list );
    if( !text )
        return -1;
    lsmash_bs_t *bs = root->bs;
    if( lsmash_bs_read_data( bs, 51 ) )
        return -1;
    for( int i = 0; i < 6; i++ )
        text->reserved[i]        = lsmash_bs_get_byte( bs );
    text->data_reference_index   = lsmash_bs_get_be16( bs );
    text->displayFlags           = lsmash_bs_get_be32( bs );
    text->textJustification      = lsmash_bs_get_be32( bs );
    for( int i = 0; i < 3; i++ )
        text->bgColor[i]         = lsmash_bs_get_be16( bs );
    text->top                    = lsmash_bs_get_be16( bs );
    text->left                   = lsmash_bs_get_be16( bs );
    text->bottom                 = lsmash_bs_get_be16( bs );
    text->right                  = lsmash_bs_get_be16( bs );
    text->scrpStartChar          = lsmash_bs_get_be32( bs );
    text->scrpHeight             = lsmash_bs_get_be16( bs );
    text->scrpAscent             = lsmash_bs_get_be16( bs );
    text->scrpFont               = lsmash_bs_get_be16( bs );
    text->scrpFace               = lsmash_bs_get_be16( bs );
    text->scrpSize               = lsmash_bs_get_be16( bs );
    for( int i = 0; i < 3; i++ )
        text->scrpColor[i]       = lsmash_bs_get_be16( bs );
    text->font_name_length       = lsmash_bs_get_byte( bs );
    if( text->font_name_length )
    {
        if( lsmash_bs_read_data( bs, text->font_name_length ) )
            return -1;
        text->font_name = malloc( text->font_name_length + 1 );
        if( !text->font_name )
            return -1;
        for( uint8_t i = 0; i < text->font_name_length; i++ )
            text->font_name[i] = lsmash_bs_get_byte( bs );
        text->font_name[text->font_name_length] = '\0';
    }
    box->parent = parent;
    isom_box_common_copy( text, box );
    if( isom_add_print_func( root, text, level ) )
        return -1;
    return isom_read_children( root, box, text, level );
}

static int isom_read_tx3g_description( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STSD )
        return isom_read_unknown_box( root, box, parent, level );
    isom_tx3g_entry_t *tx3g = (isom_tx3g_entry_t *)isom_add_description( box->type, ((isom_stsd_t *)parent)->list );
    if( !tx3g )
        return -1;
    lsmash_bs_t *bs = root->bs;
    if( lsmash_bs_read_data( bs, 38 ) )
        return -1;
    for( int i = 0; i < 6; i++ )
        tx3g->reserved[i]              = lsmash_bs_get_byte( bs );
    tx3g->data_reference_index         = lsmash_bs_get_be16( bs );
    tx3g->displayFlags                 = lsmash_bs_get_be32( bs );
    tx3g->horizontal_justification     = lsmash_bs_get_byte( bs );
    tx3g->vertical_justification       = lsmash_bs_get_byte( bs );
    for( int i = 0; i < 4; i++ )
        tx3g->background_color_rgba[i] = lsmash_bs_get_byte( bs );
    tx3g->top                          = lsmash_bs_get_be16( bs );
    tx3g->left                         = lsmash_bs_get_be16( bs );
    tx3g->bottom                       = lsmash_bs_get_be16( bs );
    tx3g->right                        = lsmash_bs_get_be16( bs );
    tx3g->startChar                    = lsmash_bs_get_be16( bs );
    tx3g->endChar                      = lsmash_bs_get_be16( bs );
    tx3g->font_ID                      = lsmash_bs_get_be16( bs );
    tx3g->face_style_flags             = lsmash_bs_get_byte( bs );
    tx3g->font_size                    = lsmash_bs_get_byte( bs );
    for( int i = 0; i < 4; i++ )
        tx3g->text_color_rgba[i]       = lsmash_bs_get_byte( bs );
    box->parent = parent;
    isom_box_common_copy( tx3g, box );
    if( isom_add_print_func( root, tx3g, level ) )
        return -1;
    return isom_read_children( root, box, tx3g, level );
}

static int isom_read_ftab( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_CODEC_TYPE_TX3G_TEXT )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_list_box( ftab, parent, box->type );
    ((isom_tx3g_entry_t *)parent)->ftab = ftab;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    uint32_t entry_count = lsmash_bs_get_be16( bs );
    uint64_t pos;
    for( pos = lsmash_bs_get_pos( bs ); pos < box->size; pos = lsmash_bs_get_pos( bs ) )
    {
        isom_font_record_t *data = malloc( sizeof(isom_font_record_t) );
        if( !data || lsmash_add_entry( ftab->list, data ) )
        {
            if( data )
                free( data );
            return -1;
        }
        memset( data, 0, sizeof(isom_font_record_t) );
        data->font_ID          = lsmash_bs_get_be16( bs );
        data->font_name_length = lsmash_bs_get_byte( bs );
        if( data->font_name_length )
        {
            data->font_name = malloc( data->font_name_length + 1 );
            if( !data->font_name )
                return -1;
            for( uint8_t i = 0; i < data->font_name_length; i++ )
                data->font_name[i] = lsmash_bs_get_byte( bs );
            data->font_name[data->font_name_length] = '\0';
        }
    }
    if( entry_count != ftab->list->entry_count || box->size < pos )
        printf( "[ftab] box has extra bytes: %"PRId64"\n", pos - box->size );
    box->size = pos;
    isom_box_common_copy( ftab, box );
    return isom_add_print_func( root, ftab, level );
}

static int isom_read_stts( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STBL )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_list_box( stts, parent, box->type );
    ((isom_stbl_t *)parent)->stts = stts;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    uint32_t entry_count = lsmash_bs_get_be32( bs );
    uint64_t pos;
    for( pos = lsmash_bs_get_pos( bs ); pos < box->size; pos = lsmash_bs_get_pos( bs ) )
    {
        isom_stts_entry_t *data = malloc( sizeof(isom_stts_entry_t) );
        if( !data || lsmash_add_entry( stts->list, data ) )
        {
            if( data )
                free( data );
            return -1;
        }
        memset( data, 0, sizeof(isom_stts_entry_t) );
        data->sample_count = lsmash_bs_get_be32( bs );
        data->sample_delta = lsmash_bs_get_be32( bs );
    }
    if( entry_count != stts->list->entry_count || box->size < pos )
        printf( "[stts] box has extra bytes: %"PRId64"\n", pos - box->size );
    box->size = pos;
    isom_box_common_copy( stts, box );
    return isom_add_print_func( root, stts, level );
}

static int isom_read_ctts( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STBL )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_list_box( ctts, parent, box->type );
    ((isom_stbl_t *)parent)->ctts = ctts;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    uint32_t entry_count = lsmash_bs_get_be32( bs );
    uint64_t pos;
    for( pos = lsmash_bs_get_pos( bs ); pos < box->size; pos = lsmash_bs_get_pos( bs ) )
    {
        isom_ctts_entry_t *data = malloc( sizeof(isom_ctts_entry_t) );
        if( !data || lsmash_add_entry( ctts->list, data ) )
        {
            if( data )
                free( data );
            return -1;
        }
        memset( data, 0, sizeof(isom_ctts_entry_t) );
        data->sample_count  = lsmash_bs_get_be32( bs );
        data->sample_offset = lsmash_bs_get_be32( bs );
    }
    if( entry_count != ctts->list->entry_count || box->size < pos )
        printf( "[ctts] box has extra bytes: %"PRId64"\n", pos - box->size );
    box->size = pos;
    isom_box_common_copy( ctts, box );
    return isom_add_print_func( root, ctts, level );
}

static int isom_read_cslg( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STBL )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( cslg, parent, box->type );
    ((isom_stbl_t *)parent)->cslg = cslg;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    cslg->compositionToDTSShift        = lsmash_bs_get_be32( bs );
    cslg->leastDecodeToDisplayDelta    = lsmash_bs_get_be32( bs );
    cslg->greatestDecodeToDisplayDelta = lsmash_bs_get_be32( bs );
    cslg->compositionStartTime         = lsmash_bs_get_be32( bs );
    cslg->compositionEndTime           = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( cslg, box );
    return isom_add_print_func( root, cslg, level );
}

static int isom_read_stss( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STBL )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_list_box( stss, parent, box->type );
    ((isom_stbl_t *)parent)->stss = stss;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    uint32_t entry_count = lsmash_bs_get_be32( bs );
    uint64_t pos;
    for( pos = lsmash_bs_get_pos( bs ); pos < box->size; pos = lsmash_bs_get_pos( bs ) )
    {
        isom_stss_entry_t *data = malloc( sizeof(isom_stss_entry_t) );
        if( !data || lsmash_add_entry( stss->list, data ) )
        {
            if( data )
                free( data );
            return -1;
        }
        memset( data, 0, sizeof(isom_stss_entry_t) );
        data->sample_number = lsmash_bs_get_be32( bs );
    }
    if( entry_count != stss->list->entry_count || box->size < pos )
        printf( "[stss] box has extra bytes: %"PRId64"\n", pos - box->size );
    box->size = pos;
    isom_box_common_copy( stss, box );
    return isom_add_print_func( root, stss, level );
}

static int isom_read_stps( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STBL )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_list_box( stps, parent, box->type );
    ((isom_stbl_t *)parent)->stps = stps;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    uint32_t entry_count = lsmash_bs_get_be32( bs );
    uint64_t pos;
    for( pos = lsmash_bs_get_pos( bs ); pos < box->size; pos = lsmash_bs_get_pos( bs ) )
    {
        isom_stps_entry_t *data = malloc( sizeof(isom_stps_entry_t) );
        if( !data || lsmash_add_entry( stps->list, data ) )
        {
            if( data )
                free( data );
            return -1;
        }
        memset( data, 0, sizeof(isom_stps_entry_t) );
        data->sample_number = lsmash_bs_get_be32( bs );
    }
    if( entry_count != stps->list->entry_count || box->size < pos )
        printf( "[stps] box has extra bytes: %"PRId64"\n", pos - box->size );
    box->size = pos;
    isom_box_common_copy( stps, box );
    return isom_add_print_func( root, stps, level );
}

static int isom_read_sdtp( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STBL )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_list_box( sdtp, parent, box->type );
    ((isom_stbl_t *)parent)->sdtp = sdtp;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    uint64_t pos;
    for( pos = lsmash_bs_get_pos( bs ); pos < box->size; pos = lsmash_bs_get_pos( bs ) )
    {
        isom_sdtp_entry_t *data = malloc( sizeof(isom_sdtp_entry_t) );
        if( !data || lsmash_add_entry( sdtp->list, data ) )
        {
            if( data )
                free( data );
            return -1;
        }
        memset( data, 0, sizeof(isom_sdtp_entry_t) );
        uint8_t temp = lsmash_bs_get_byte( bs );
        data->is_leading            = (temp >> 6) & 0x3;
        data->sample_depends_on     = (temp >> 4) & 0x3;
        data->sample_is_depended_on = (temp >> 2) & 0x3;
        data->sample_has_redundancy =  temp       & 0x3;
    }
    box->size = pos;
    isom_box_common_copy( sdtp, box );
    return isom_add_print_func( root, sdtp, level );
}

static int isom_read_stsc( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STBL )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_list_box( stsc, parent, box->type );
    ((isom_stbl_t *)parent)->stsc = stsc;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    uint32_t entry_count = lsmash_bs_get_be32( bs );
    uint64_t pos;
    for( pos = lsmash_bs_get_pos( bs ); pos < box->size; pos = lsmash_bs_get_pos( bs ) )
    {
        isom_stsc_entry_t *data = malloc( sizeof(isom_stsc_entry_t) );
        if( !data || lsmash_add_entry( stsc->list, data ) )
        {
            if( data )
                free( data );
            return -1;
        }
        memset( data, 0, sizeof(isom_stsc_entry_t) );
        data->first_chunk              = lsmash_bs_get_be32( bs );
        data->samples_per_chunk        = lsmash_bs_get_be32( bs );
        data->sample_description_index = lsmash_bs_get_be32( bs );
    }
    if( entry_count != stsc->list->entry_count || box->size < pos )
        printf( "[stsc] box has extra bytes: %"PRId64"\n", pos - box->size );
    box->size = pos;
    isom_box_common_copy( stsc, box );
    return isom_add_print_func( root, stsc, level );
}

static int isom_read_stsz( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STBL )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( stsz, parent, box->type );
    ((isom_stbl_t *)parent)->stsz = stsz;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    stsz->sample_size  = lsmash_bs_get_be32( bs );
    stsz->sample_count = lsmash_bs_get_be32( bs );
    uint64_t pos = lsmash_bs_get_pos( bs );
    if( pos < box->size )
    {
        stsz->list = lsmash_create_entry_list();
        if( !stsz->list )
            return -1;
        for( ; pos < box->size; pos = lsmash_bs_get_pos( bs ) )
        {
            isom_stsz_entry_t *data = malloc( sizeof(isom_stsz_entry_t) );
            if( !data || lsmash_add_entry( stsz->list, data ) )
            {
                if( data )
                    free( data );
                return -1;
            }
            memset( data, 0, sizeof(isom_stsz_entry_t) );
            data->entry_size = lsmash_bs_get_be32( bs );
        }
    }
    if( (stsz->list && stsz->sample_count != stsz->list->entry_count) || box->size < pos )
        printf( "[stsz] box has extra bytes: %"PRId64"\n", pos - box->size );
    box->size = pos;
    isom_box_common_copy( stsz, box );
    return isom_add_print_func( root, stsz, level );
}

static int isom_read_stco( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STBL )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_list_box( stco, parent, box->type );
    ((isom_stbl_t *)parent)->stco = stco;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    uint32_t entry_count = lsmash_bs_get_be32( bs );
    uint64_t pos;
    if( box->type == ISOM_BOX_TYPE_STCO )
        for( pos = lsmash_bs_get_pos( bs ); pos < box->size; pos = lsmash_bs_get_pos( bs ) )
        {
            isom_stco_entry_t *data = malloc( sizeof(isom_stco_entry_t) );
            if( !data || lsmash_add_entry( stco->list, data ) )
            {
                if( data )
                    free( data );
                return -1;
            }
            memset( data, 0, sizeof(isom_stco_entry_t) );
            data->chunk_offset = lsmash_bs_get_be32( bs );
        }
    else
        for( pos = lsmash_bs_get_pos( bs ); pos < box->size; pos = lsmash_bs_get_pos( bs ) )
        {
            isom_co64_entry_t *data = malloc( sizeof(isom_co64_entry_t) );
            if( !data || lsmash_add_entry( stco->list, data ) )
            {
                if( data )
                    free( data );
                return -1;
            }
            memset( data, 0, sizeof(isom_co64_entry_t) );
            data->chunk_offset = lsmash_bs_get_be64( bs );
        }
    if( entry_count != stco->list->entry_count || box->size < pos )
        printf( "[%s] box has extra bytes: %"PRId64"\n", isom_4cc2str( box->type ), pos - box->size );
    box->size = pos;
    isom_box_common_copy( stco, box );
    return isom_add_print_func( root, stco, level );
}

static int isom_read_sgpd( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STBL )
        return isom_read_unknown_box( root, box, parent, level );
    isom_stbl_t *stbl = (isom_stbl_t *)parent;
    lsmash_entry_list_t *list = stbl->sgpd_list;
    if( !list )
    {
        list = lsmash_create_entry_list();
        if( !list )
            return -1;
        stbl->sgpd_list = list;
    }
    isom_sgpd_entry_t *sgpd = malloc( sizeof(isom_sgpd_entry_t) );
    if( !sgpd )
        return -1;
    memset( sgpd, 0, sizeof(isom_sgpd_entry_t) );
    sgpd->list = lsmash_create_entry_list();
    if( !sgpd->list || lsmash_add_entry( list, sgpd ) )
    {
        free( sgpd );
        return -1;
    }
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    sgpd->grouping_type      = lsmash_bs_get_be32( bs );
    if( box->version == 1 )
        sgpd->default_length = lsmash_bs_get_be32( bs );
    uint32_t entry_count     = lsmash_bs_get_be32( bs );
    switch( sgpd->grouping_type )
    {
        case ISOM_GROUP_TYPE_RAP :
        {
            uint64_t pos;
            for( pos = lsmash_bs_get_pos( bs ); pos < box->size; pos = lsmash_bs_get_pos( bs ) )
            {
                isom_rap_entry_t *data = malloc( sizeof(isom_rap_entry_t) );
                if( !data || lsmash_add_entry( sgpd->list, data ) )
                {
                    if( data )
                        free( data );
                    return -1;
                }
                memset( data, 0, sizeof(isom_rap_entry_t) );
                /* We don't know groups decided by variable description length. If encountering, skip getting of bytes of it. */
                if( box->version == 1 && !sgpd->default_length )
                    data->description_length = lsmash_bs_get_be32( bs );
                else
                {
                    uint8_t temp = lsmash_bs_get_byte( bs );
                    data->num_leading_samples_known = (temp >> 7) & 0x01;
                    data->num_leading_samples       =  temp       & 0x7f;
                }
            }
            if( entry_count != sgpd->list->entry_count || box->size < pos )
                printf( "[sgpd] box has extra bytes: %"PRId64"\n", pos - box->size );
            box->size = pos;
            break;
        }
        case ISOM_GROUP_TYPE_ROLL :
        {
            uint64_t pos;
            for( pos = lsmash_bs_get_pos( bs ); pos < box->size; pos = lsmash_bs_get_pos( bs ) )
            {
                isom_roll_entry_t *data = malloc( sizeof(isom_roll_entry_t) );
                if( !data || lsmash_add_entry( sgpd->list, data ) )
                {
                    if( data )
                        free( data );
                    return -1;
                }
                memset( data, 0, sizeof(isom_roll_entry_t) );
                /* We don't know groups decided by variable description length. If encountering, skip getting of bytes of it. */
                if( box->version == 1 && !sgpd->default_length )
                    data->description_length = lsmash_bs_get_be32( bs );
                else
                    data->roll_distance      = lsmash_bs_get_be16( bs );
            }
            if( entry_count != sgpd->list->entry_count || box->size < pos )
                printf( "[sgpd] box has extra bytes: %"PRId64"\n", pos - box->size );
            box->size = pos;
            break;
        }
        default :
            break;
    }
    isom_box_common_copy( sgpd, box );
    return isom_add_print_func( root, sgpd, level );
}

static int isom_read_sbgp( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_STBL && parent->type != ISOM_BOX_TYPE_TRAF )
        return isom_read_unknown_box( root, box, parent, level );
    isom_stbl_t *stbl = (isom_stbl_t *)parent;
    lsmash_entry_list_t *list = stbl->sbgp_list;
    if( !list )
    {
        list = lsmash_create_entry_list();
        if( !list )
            return -1;
        stbl->sbgp_list = list;
    }
    isom_sbgp_entry_t *sbgp = malloc( sizeof(isom_sbgp_entry_t) );
    if( !sbgp )
        return -1;
    memset( sbgp, 0, sizeof(isom_sbgp_entry_t) );
    sbgp->list = lsmash_create_entry_list();
    if( !sbgp->list || lsmash_add_entry( list, sbgp ) )
    {
        free( sbgp );
        return -1;
    }
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    sbgp->grouping_type  = lsmash_bs_get_be32( bs );
    if( sbgp->version == 1 )
        sbgp->grouping_type_parameter = lsmash_bs_get_be32( bs );
    uint32_t entry_count = lsmash_bs_get_be32( bs );
    uint64_t pos;
    for( pos = lsmash_bs_get_pos( bs ); pos < box->size; pos = lsmash_bs_get_pos( bs ) )
    {
        isom_group_assignment_entry_t *data = malloc( sizeof(isom_group_assignment_entry_t) );
        if( !data || lsmash_add_entry( sbgp->list, data ) )
        {
            if( data )
                free( data );
            return -1;
        }
        memset( data, 0, sizeof(isom_group_assignment_entry_t) );
        data->sample_count            = lsmash_bs_get_be32( bs );
        data->group_description_index = lsmash_bs_get_be32( bs );
    }
    if( entry_count != sbgp->list->entry_count || box->size < pos )
        printf( "[sbgp] box has extra bytes: %"PRId64"\n", pos - box->size );
    box->size = pos;
    isom_box_common_copy( sbgp, box );
    return isom_add_print_func( root, sbgp, level );
}

static int isom_read_udta( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MOOV && parent->type != ISOM_BOX_TYPE_TRAK )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( udta, parent, box->type );
    if( parent->type == ISOM_BOX_TYPE_MOOV )
        ((isom_moov_t *)parent)->udta = udta;
    else
        ((isom_trak_entry_t *)parent)->udta = udta;
    isom_box_common_copy( udta, box );
    if( isom_add_print_func( root, udta, level ) )
        return -1;
    return isom_read_children( root, box, udta, level );
}

static int isom_read_chpl( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_UDTA )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_list_box( chpl, parent, box->type );
    ((isom_udta_t *)parent)->chpl = chpl;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    uint32_t entry_count;
    if( box->version == 1 )
    {
        chpl->unknown = lsmash_bs_get_byte( bs );
        entry_count   = lsmash_bs_get_be32( bs );
    }
    else
        entry_count   = lsmash_bs_get_byte( bs );
    uint64_t pos;
    for( pos = lsmash_bs_get_pos( bs ); pos < box->size; pos = lsmash_bs_get_pos( bs ) )
    {
        isom_chpl_entry_t *data = malloc( sizeof(isom_chpl_entry_t) );
        if( !data || lsmash_add_entry( chpl->list, data ) )
        {
            if( data )
                free( data );
            return -1;
        }
        memset( data, 0, sizeof(isom_chpl_entry_t) );
        data->start_time          = lsmash_bs_get_be64( bs );
        data->chapter_name_length = lsmash_bs_get_byte( bs );
        data->chapter_name = malloc( data->chapter_name_length + 1 );
        if( !data->chapter_name )
        {
            free( data );
            return -1;
        }
        for( uint8_t i = 0; i < data->chapter_name_length; i++ )
            data->chapter_name[i] = lsmash_bs_get_byte( bs );
        data->chapter_name[data->chapter_name_length] = '\0';
    }
    if( entry_count != chpl->list->entry_count || box->size < pos )
        printf( "[chpl] box has extra bytes: %"PRId64"\n", pos - box->size );
    box->size = pos;
    isom_box_common_copy( chpl, box );
    return isom_add_print_func( root, chpl, level );
}

static int isom_read_mvex( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MOOV )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( mvex, parent, box->type );
    ((isom_moov_t *)parent)->mvex = mvex;
    isom_box_common_copy( mvex, box );
    if( isom_add_print_func( root, mvex, level ) )
        return -1;
    return isom_read_children( root, box, mvex, level );
}

static int isom_read_mehd( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MVEX )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( mehd, parent, box->type );
    ((isom_mvex_t *)parent)->mehd = mehd;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    if( box->version == 1 )
        mehd->fragment_duration = lsmash_bs_get_be64( bs );
    else
        mehd->fragment_duration = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( mehd, box );
    return isom_add_print_func( root, mehd, level );
}

static isom_sample_flags_t isom_bs_get_sample_flags( lsmash_bs_t *bs )
{
    uint32_t temp = lsmash_bs_get_be32( bs );
    isom_sample_flags_t flags;
    flags.reserved                    = (temp >> 28) & 0xf;
    flags.is_leading                  = (temp >> 26) & 0x3;
    flags.sample_depends_on           = (temp >> 24) & 0x3;
    flags.sample_is_depended_on       = (temp >> 22) & 0x3;
    flags.sample_has_redundancy       = (temp >> 20) & 0x3;
    flags.sample_padding_value        = (temp >> 17) & 0x7;
    flags.sample_is_difference_sample = (temp >> 16) & 0x1;
    flags.sample_degradation_priority =  temp        & 0xffff;
    return flags;
}

static int isom_read_trex( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MVEX )
        return isom_read_unknown_box( root, box, parent, level );
    lsmash_entry_list_t *list = ((isom_mvex_t *)parent)->trex_list;
    if( !list )
    {
        list = lsmash_create_entry_list();
        if( !list )
            return -1;
        ((isom_mvex_t *)parent)->trex_list = list;
    }
    isom_trex_entry_t *trex = malloc( sizeof(isom_trex_entry_t) );
    if( !trex )
        return -1;
    memset( trex, 0, sizeof(isom_trex_entry_t) );
    if( lsmash_add_entry( list, trex ) )
    {
        free( trex );
        return -1;
    }
    box->parent = parent;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    trex->track_ID                         = lsmash_bs_get_be32( bs );
    trex->default_sample_description_index = lsmash_bs_get_be32( bs );
    trex->default_sample_duration          = lsmash_bs_get_be32( bs );
    trex->default_sample_size              = lsmash_bs_get_be32( bs );
    trex->default_sample_flags             = isom_bs_get_sample_flags( bs );
    isom_box_common_copy( trex, box );
    return isom_add_print_func( root, trex, level );
}

static int isom_read_moof( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( !!parent->type )
        return isom_read_unknown_box( root, box, parent, level );
    lsmash_entry_list_t *list = ((lsmash_root_t *)parent)->moof_list;
    if( !list )
    {
        list = lsmash_create_entry_list();
        if( !list )
            return -1;
        ((lsmash_root_t *)parent)->moof_list = list;
    }
    isom_moof_entry_t *moof = malloc( sizeof(isom_moof_entry_t) );
    if( !moof )
        return -1;
    memset( moof, 0, sizeof(isom_moof_entry_t) );
    if( lsmash_add_entry( list, moof ) )
    {
        free( moof );
        return -1;
    }
    box->parent = parent;
    isom_box_common_copy( moof, box );
    if( isom_add_print_func( root, moof, level ) )
        return -1;
    return isom_read_children( root, box, moof, level );
}

static int isom_read_mfhd( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MOOF )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( mfhd, parent, box->type );
    ((isom_moof_entry_t *)parent)->mfhd = mfhd;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    mfhd->sequence_number = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( mfhd, box );
    return isom_add_print_func( root, mfhd, level );
}

static int isom_read_traf( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MOOF )
        return isom_read_unknown_box( root, box, parent, level );
    lsmash_entry_list_t *list = ((isom_moof_entry_t *)parent)->traf_list;
    if( !list )
    {
        list = lsmash_create_entry_list();
        if( !list )
            return -1;
        ((isom_moof_entry_t *)parent)->traf_list = list;
    }
    isom_traf_entry_t *traf = malloc( sizeof(isom_traf_entry_t) );
    if( !traf )
        return -1;
    memset( traf, 0, sizeof(isom_traf_entry_t) );
    if( lsmash_add_entry( list, traf ) )
    {
        free( traf );
        return -1;
    }
    box->parent = parent;
    isom_box_common_copy( traf, box );
    if( isom_add_print_func( root, traf, level ) )
        return -1;
    return isom_read_children( root, box, traf, level );
}

static int isom_read_tfhd( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_TRAF )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( tfhd, parent, box->type );
    ((isom_traf_entry_t *)parent)->tfhd = tfhd;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    tfhd->track_ID = lsmash_bs_get_be32( bs );
    if( box->flags & ISOM_TF_FLAGS_BASE_DATA_OFFSET_PRESENT         ) tfhd->base_data_offset         = lsmash_bs_get_be64( bs );
    if( box->flags & ISOM_TF_FLAGS_SAMPLE_DESCRIPTION_INDEX_PRESENT ) tfhd->sample_description_index = lsmash_bs_get_be32( bs );
    if( box->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT  ) tfhd->default_sample_duration  = lsmash_bs_get_be32( bs );
    if( box->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_SIZE_PRESENT      ) tfhd->default_sample_size      = lsmash_bs_get_be32( bs );
    if( box->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT     ) tfhd->default_sample_flags     = isom_bs_get_sample_flags( bs );
    uint32_t pos = lsmash_bs_get_pos( bs );
    if( box->size < pos )
        printf( "[tfhd] box has extra bytes: %"PRId64"\n", pos - box->size );
    box->size = pos;
    isom_box_common_copy( tfhd, box );
    return isom_add_print_func( root, tfhd, level );
}

static int isom_read_trun( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_TRAF )
        return isom_read_unknown_box( root, box, parent, level );
    lsmash_entry_list_t *list = ((isom_traf_entry_t *)parent)->trun_list;
    if( !list )
    {
        list = lsmash_create_entry_list();
        if( !list )
            return -1;
        ((isom_traf_entry_t *)parent)->trun_list = list;
    }
    isom_trun_entry_t *trun = malloc( sizeof(isom_trun_entry_t) );
    if( !trun )
        return -1;
    memset( trun, 0, sizeof(isom_trun_entry_t) );
    if( lsmash_add_entry( list, trun ) )
    {
        free( trun );
        return -1;
    }
    box->parent = parent;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    int has_optional_rows = ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT
                          | ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT
                          | ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT
                          | ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT;
    has_optional_rows &= box->flags;
    trun->sample_count = lsmash_bs_get_be32( bs );
    if( box->flags & ISOM_TR_FLAGS_DATA_OFFSET_PRESENT        ) trun->data_offset        = lsmash_bs_get_be32( bs );
    if( box->flags & ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT ) trun->first_sample_flags = isom_bs_get_sample_flags( bs );
    if( trun->sample_count && has_optional_rows )
    {
        trun->optional = lsmash_create_entry_list();
        if( !trun->optional )
            return -1;
        for( uint32_t i = 0; i < trun->sample_count; i++ )
        {
            isom_trun_optional_row_t *data = malloc( sizeof(isom_trun_optional_row_t) );
            if( !data || lsmash_add_entry( trun->optional, data ) )
            {
                if( data )
                    free( data );
                return -1;
            }
            memset( data, 0, sizeof(isom_trun_optional_row_t) );
            if( box->flags & ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT                ) data->sample_duration                = lsmash_bs_get_be32( bs );
            if( box->flags & ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT                    ) data->sample_size                    = lsmash_bs_get_be32( bs );
            if( box->flags & ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT                   ) data->sample_flags                   = isom_bs_get_sample_flags( bs );
            if( box->flags & ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT ) data->sample_composition_time_offset = lsmash_bs_get_be32( bs );
        }
    }
    uint32_t pos = lsmash_bs_get_pos( bs );
    if( box->size < pos )
        printf( "[trun] box has extra bytes: %"PRId64"\n", pos - box->size );
    box->size = pos;
    isom_box_common_copy( trun, box );
    return isom_add_print_func( root, trun, level );
}

static int isom_read_free( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    isom_box_t *skip = malloc( sizeof(isom_box_t) );
    if( !skip )
        return -1;
    memset( skip, 0, sizeof(isom_box_t) );
    lsmash_bs_t *bs = root->bs;
    isom_skip_box_rest( bs, box );
    box->manager |= 0x02;       /* add free flag */
    isom_box_common_copy( skip, box );
    if( isom_add_print_func( root, skip, level ) )
    {
        free( skip );
        return -1;
    }
    return 0;
}

static int isom_read_mdat( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( !!parent->type )
        return isom_read_unknown_box( root, box, parent, level );
    isom_box_t *mdat = malloc( sizeof(isom_box_t) );
    if( !mdat )
        return -1;
    memset( mdat, 0, sizeof(isom_box_t) );
    lsmash_bs_t *bs = root->bs;
    isom_skip_box_rest( bs, box );
    box->manager |= 0x02;       /* add free flag */
    isom_box_common_copy( mdat, box );
    if( isom_add_print_func( root, mdat, level ) )
    {
        free( mdat );
        return -1;
    }
    return 0;
}

static int isom_read_mfra( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( !!parent->type )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( mfra, parent, box->type );
    ((lsmash_root_t *)parent)->mfra = mfra;
    isom_box_common_copy( mfra, box );
    if( isom_add_print_func( root, mfra, level ) )
        return -1;
    return isom_read_children( root, box, mfra, level );
}

static int isom_read_tfra( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MFRA )
        return isom_read_unknown_box( root, box, parent, level );
    lsmash_entry_list_t *list = ((isom_mfra_t *)parent)->tfra_list;
    if( !list )
    {
        list = lsmash_create_entry_list();
        if( !list )
            return -1;
        ((isom_mfra_t *)parent)->tfra_list = list;
    }
    isom_tfra_entry_t *tfra = malloc( sizeof(isom_tfra_entry_t) );
    if( !tfra )
        return -1;
    memset( tfra, 0, sizeof(isom_tfra_entry_t) );
    if( lsmash_add_entry( list, tfra ) )
        goto fail;
    box->parent = parent;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    tfra->track_ID        = lsmash_bs_get_be32( bs );
    uint32_t temp         = lsmash_bs_get_be32( bs );
    tfra->number_of_entry = lsmash_bs_get_be32( bs );
    tfra->reserved                  = (temp >> 6) & 0x3ffffff;
    tfra->length_size_of_traf_num   = (temp >> 4) & 0x3;
    tfra->length_size_of_trun_num   = (temp >> 2) & 0x3;
    tfra->length_size_of_sample_num =  temp       & 0x3;
    if( tfra->number_of_entry )
    {
        tfra->list = lsmash_create_entry_list();
        if( !tfra->list )
            goto fail;
        uint64_t (*bs_get_funcs[5])( lsmash_bs_t * ) =
            {
              lsmash_bs_get_byte_to_64,
              lsmash_bs_get_be16_to_64,
              lsmash_bs_get_be24_to_64,
              lsmash_bs_get_be32_to_64,
              lsmash_bs_get_be64
            };
        uint64_t (*bs_put_time)         ( lsmash_bs_t * ) = bs_get_funcs[ 3 + (tfra->version == 1)        ];
        uint64_t (*bs_put_moof_offset)  ( lsmash_bs_t * ) = bs_get_funcs[ 3 + (tfra->version == 1)        ];
        uint64_t (*bs_put_traf_number)  ( lsmash_bs_t * ) = bs_get_funcs[ tfra->length_size_of_traf_num   ];
        uint64_t (*bs_put_trun_number)  ( lsmash_bs_t * ) = bs_get_funcs[ tfra->length_size_of_trun_num   ];
        uint64_t (*bs_put_sample_number)( lsmash_bs_t * ) = bs_get_funcs[ tfra->length_size_of_sample_num ];
        for( uint32_t i = 0; i < tfra->number_of_entry; i++ )
        {
            isom_tfra_location_time_entry_t *data = malloc( sizeof(isom_tfra_location_time_entry_t) );
            if( !data || lsmash_add_entry( tfra->list, data ) )
            {
                if( data )
                    free( data );
                goto fail;
            }
            memset( data, 0, sizeof(isom_tfra_location_time_entry_t) );
            data->time          = bs_put_time         ( bs );
            data->moof_offset   = bs_put_moof_offset  ( bs );
            data->traf_number   = bs_put_traf_number  ( bs );
            data->trun_number   = bs_put_trun_number  ( bs );
            data->sample_number = bs_put_sample_number( bs );
        }
    }
    uint32_t pos = lsmash_bs_get_pos( bs );
    if( (tfra->list && tfra->number_of_entry != tfra->list->entry_count) || box->size < pos )
        printf( "[tfra] box has extra bytes: %"PRId64"\n", pos - box->size );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( tfra, box );
    return isom_add_print_func( root, tfra, level );
fail:
    if( tfra->list )
        free( tfra->list );
    free( tfra );
    return -1;
}

static int isom_read_mfro( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, int level )
{
    if( parent->type != ISOM_BOX_TYPE_MFRA )
        return isom_read_unknown_box( root, box, parent, level );
    isom_create_box( mfro, parent, box->type );
    ((isom_mfra_t *)parent)->mfro = mfro;
    lsmash_bs_t *bs = root->bs;
    isom_read_box_rest( bs, box );
    mfro->length = lsmash_bs_get_be32( bs );
    box->size = lsmash_bs_get_pos( bs );
    isom_box_common_copy( mfro, box );
    return isom_add_print_func( root, mfro, level );
}

static int isom_read_box( lsmash_root_t *root, isom_box_t *box, isom_box_t *parent, uint64_t parent_pos, int level )
{
    lsmash_bs_t *bs = root->bs;
    memset( box, 0, sizeof(isom_box_t) );
    assert( parent && parent->root );
    box->root = parent->root;
    box->parent = parent;
    if( parent->size < parent_pos + ISOM_DEFAULT_BOX_HEADER_SIZE )
    {
        /* skip extra bytes */
        uint64_t rest_size = parent->size - parent_pos;
        lsmash_fseek( bs->stream, rest_size, SEEK_CUR );
        box->size = rest_size;
        return 0;
    }
    lsmash_bs_empty( bs );
    int ret = -1;
    if( !!(ret = isom_bs_read_box_common( bs, box )) )
        return ret;     /* return if reached EOF */
    ++level;
    if( parent->type == ISOM_BOX_TYPE_STSD )
        switch( box->type )
        {
            case ISOM_CODEC_TYPE_AVC1_VIDEO :
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
            case ISOM_CODEC_TYPE_DRAC_VIDEO :
            case ISOM_CODEC_TYPE_ENCV_VIDEO :
            case ISOM_CODEC_TYPE_MJP2_VIDEO :
            case ISOM_CODEC_TYPE_MP4V_VIDEO :
            case ISOM_CODEC_TYPE_MVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC2_VIDEO :
            case ISOM_CODEC_TYPE_S263_VIDEO :
            case ISOM_CODEC_TYPE_SVC1_VIDEO :
            case ISOM_CODEC_TYPE_VC_1_VIDEO :
                return isom_read_visual_description( root, box, parent, level );
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_DRA1_AUDIO :
            case ISOM_CODEC_TYPE_DTSC_AUDIO :
            case ISOM_CODEC_TYPE_DTSH_AUDIO :
            case ISOM_CODEC_TYPE_DTSL_AUDIO :
            case ISOM_CODEC_TYPE_DTSE_AUDIO :
            case ISOM_CODEC_TYPE_EC_3_AUDIO :
            case ISOM_CODEC_TYPE_ENCA_AUDIO :
            case ISOM_CODEC_TYPE_G719_AUDIO :
            case ISOM_CODEC_TYPE_G726_AUDIO :
            case ISOM_CODEC_TYPE_M4AE_AUDIO :
            case ISOM_CODEC_TYPE_MLPA_AUDIO :
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
            //case ISOM_CODEC_TYPE_RAW_AUDIO  :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
            case ISOM_CODEC_TYPE_SAWP_AUDIO :
            case ISOM_CODEC_TYPE_SEVC_AUDIO :
            case ISOM_CODEC_TYPE_SQCP_AUDIO :
            case ISOM_CODEC_TYPE_SSMV_AUDIO :
            //case ISOM_CODEC_TYPE_TWOS_AUDIO :
            case QT_CODEC_TYPE_23NI_AUDIO :
            case QT_CODEC_TYPE_MAC3_AUDIO :
            case QT_CODEC_TYPE_MAC6_AUDIO :
            case QT_CODEC_TYPE_NONE_AUDIO :
            case QT_CODEC_TYPE_QDM2_AUDIO :
            case QT_CODEC_TYPE_QDMC_AUDIO :
            case QT_CODEC_TYPE_QCLP_AUDIO :
            case QT_CODEC_TYPE_AGSM_AUDIO :
            case QT_CODEC_TYPE_ALAW_AUDIO :
            case QT_CODEC_TYPE_CDX2_AUDIO :
            case QT_CODEC_TYPE_CDX4_AUDIO :
            case QT_CODEC_TYPE_DVCA_AUDIO :
            case QT_CODEC_TYPE_DVI_AUDIO  :
            case QT_CODEC_TYPE_FL32_AUDIO :
            case QT_CODEC_TYPE_FL64_AUDIO :
            case QT_CODEC_TYPE_IMA4_AUDIO :
            case QT_CODEC_TYPE_IN24_AUDIO :
            case QT_CODEC_TYPE_IN32_AUDIO :
            case QT_CODEC_TYPE_LPCM_AUDIO :
            case QT_CODEC_TYPE_RAW_AUDIO :
            case QT_CODEC_TYPE_SOWT_AUDIO :
            case QT_CODEC_TYPE_TWOS_AUDIO :
            case QT_CODEC_TYPE_ULAW_AUDIO :
            case QT_CODEC_TYPE_VDVA_AUDIO :
            case QT_CODEC_TYPE_FULLMP3_AUDIO :
            case QT_CODEC_TYPE_MP3_AUDIO :
            case QT_CODEC_TYPE_ADPCM2_AUDIO :
            case QT_CODEC_TYPE_ADPCM17_AUDIO :
            case QT_CODEC_TYPE_GSM49_AUDIO :
            case QT_CODEC_TYPE_NOT_SPECIFIED :
                return isom_read_audio_description( root, box, parent, level );
            case QT_CODEC_TYPE_TEXT_TEXT :
                return isom_read_text_description( root, box, parent, level );
            case ISOM_CODEC_TYPE_TX3G_TEXT :
                return isom_read_tx3g_description( root, box, parent, level );
            default :
                return isom_read_unknown_box( root, box, parent, level );
        }
    if( parent->type == QT_BOX_TYPE_WAVE )
        switch( box->type )
        {
            case QT_BOX_TYPE_FRMA :
                return isom_read_frma( root, box, parent, level );
            case QT_BOX_TYPE_ENDA :
                return isom_read_enda( root, box, parent, level );
            case ISOM_BOX_TYPE_ESDS :
                return isom_read_esds( root, box, parent, level );
            case QT_BOX_TYPE_TERMINATOR :
                return isom_read_terminator( root, box, parent, level );
            default :
                return isom_read_audio_specific( root, box, parent, level );
        }
    if( parent->type == ISOM_BOX_TYPE_TREF )
        return isom_read_track_reference_type( root, box, parent, level );
    switch( box->type )
    {
        case ISOM_BOX_TYPE_FTYP :
            return isom_read_ftyp( root, box, parent, level );
        case ISOM_BOX_TYPE_MOOV :
            return isom_read_moov( root, box, parent, level );
        case ISOM_BOX_TYPE_MVHD :
            return isom_read_mvhd( root, box, parent, level );
        case ISOM_BOX_TYPE_IODS :
            return isom_read_iods( root, box, parent, level );
        case ISOM_BOX_TYPE_ESDS :
            return isom_read_esds( root, box, parent, level );
        case ISOM_BOX_TYPE_TRAK :
            return isom_read_trak( root, box, parent, level );
        case ISOM_BOX_TYPE_TKHD :
            return isom_read_tkhd( root, box, parent, level );
        case QT_BOX_TYPE_TAPT :
            return isom_read_tapt( root, box, parent, level );
        case QT_BOX_TYPE_CLEF :
            return isom_read_clef( root, box, parent, level );
        case QT_BOX_TYPE_PROF :
            return isom_read_prof( root, box, parent, level );
        case QT_BOX_TYPE_ENOF :
            return isom_read_enof( root, box, parent, level );
        case ISOM_BOX_TYPE_EDTS :
            return isom_read_edts( root, box, parent, level );
        case ISOM_BOX_TYPE_ELST :
            return isom_read_elst( root, box, parent, level );
        case ISOM_BOX_TYPE_TREF :
            return isom_read_tref( root, box, parent, level );
        case ISOM_BOX_TYPE_MDIA :
            return isom_read_mdia( root, box, parent, level );
        case ISOM_BOX_TYPE_MDHD :
            return isom_read_mdhd( root, box, parent, level );
        case ISOM_BOX_TYPE_HDLR :
            return isom_read_hdlr( root, box, parent, level );
        case ISOM_BOX_TYPE_MINF :
            return isom_read_minf( root, box, parent, level );
        case ISOM_BOX_TYPE_VMHD :
            return isom_read_vmhd( root, box, parent, level );
        case ISOM_BOX_TYPE_SMHD :
            return isom_read_smhd( root, box, parent, level );
        case ISOM_BOX_TYPE_HMHD :
            return isom_read_hmhd( root, box, parent, level );
        case ISOM_BOX_TYPE_NMHD :
            return isom_read_nmhd( root, box, parent, level );
        case QT_BOX_TYPE_GMHD :
            return isom_read_gmhd( root, box, parent, level );
        case QT_BOX_TYPE_GMIN :
            return isom_read_gmin( root, box, parent, level );
        case QT_BOX_TYPE_TEXT :
            return isom_read_text( root, box, parent, level );
        case ISOM_BOX_TYPE_DINF :
            return isom_read_dinf( root, box, parent, level );
        case ISOM_BOX_TYPE_DREF :
            return isom_read_dref( root, box, parent, level );
        case ISOM_BOX_TYPE_URL  :
            return isom_read_url ( root, box, parent, level );
        case ISOM_BOX_TYPE_STBL :
            return isom_read_stbl( root, box, parent, level );
        case ISOM_BOX_TYPE_STSD :
            return isom_read_stsd( root, box, parent, level );
        case ISOM_BOX_TYPE_BTRT :
            return isom_read_btrt( root, box, parent, level );
        case ISOM_BOX_TYPE_CLAP :
            return isom_read_clap( root, box, parent, level );
        case ISOM_BOX_TYPE_PASP :
            return isom_read_pasp( root, box, parent, level );
        case QT_BOX_TYPE_COLR :
            return isom_read_colr( root, box, parent, level );
        case ISOM_BOX_TYPE_STSL :
            return isom_read_stsl( root, box, parent, level );
        case ISOM_BOX_TYPE_AVCC :
            return isom_read_avcC( root, box, parent, level );
        case QT_BOX_TYPE_WAVE :
            return isom_read_wave( root, box, parent, level );
        case QT_BOX_TYPE_CHAN :
            return isom_read_chan( root, box, parent, level );
        case ISOM_BOX_TYPE_FTAB :
            return isom_read_ftab( root, box, parent, level );
        case ISOM_BOX_TYPE_STTS :
            return isom_read_stts( root, box, parent, level );
        case ISOM_BOX_TYPE_CTTS :
            return isom_read_ctts( root, box, parent, level );
        case ISOM_BOX_TYPE_CSLG :
            return isom_read_cslg( root, box, parent, level );
        case ISOM_BOX_TYPE_STSS :
            return isom_read_stss( root, box, parent, level );
        case QT_BOX_TYPE_STPS :
            return isom_read_stps( root, box, parent, level );
        case ISOM_BOX_TYPE_SDTP :
            return isom_read_sdtp( root, box, parent, level );
        case ISOM_BOX_TYPE_STSC :
            return isom_read_stsc( root, box, parent, level );
        case ISOM_BOX_TYPE_STSZ :
            return isom_read_stsz( root, box, parent, level );
        case ISOM_BOX_TYPE_STCO :
        case ISOM_BOX_TYPE_CO64 :
            return isom_read_stco( root, box, parent, level );
        case ISOM_BOX_TYPE_SGPD :
            return isom_read_sgpd( root, box, parent, level );
        case ISOM_BOX_TYPE_SBGP :
            return isom_read_sbgp( root, box, parent, level );
        case ISOM_BOX_TYPE_UDTA :
            return isom_read_udta( root, box, parent, level );
        case ISOM_BOX_TYPE_CHPL :
            return isom_read_chpl( root, box, parent, level );
        case ISOM_BOX_TYPE_MVEX :
            return isom_read_mvex( root, box, parent, level );
        case ISOM_BOX_TYPE_MEHD :
            return isom_read_mehd( root, box, parent, level );
        case ISOM_BOX_TYPE_TREX :
            return isom_read_trex( root, box, parent, level );
        case ISOM_BOX_TYPE_MOOF :
            return isom_read_moof( root, box, parent, level );
        case ISOM_BOX_TYPE_MFHD :
            return isom_read_mfhd( root, box, parent, level );
        case ISOM_BOX_TYPE_TRAF :
            return isom_read_traf( root, box, parent, level );
        case ISOM_BOX_TYPE_TFHD :
            return isom_read_tfhd( root, box, parent, level );
        case ISOM_BOX_TYPE_TRUN :
            return isom_read_trun( root, box, parent, level );
        case ISOM_BOX_TYPE_FREE :
        case ISOM_BOX_TYPE_SKIP :
            return isom_read_free( root, box, parent, level );
        case ISOM_BOX_TYPE_MDAT :
            return isom_read_mdat( root, box, parent, level );
        case ISOM_BOX_TYPE_MFRA :
            return isom_read_mfra( root, box, parent, level );
        case ISOM_BOX_TYPE_TFRA :
            return isom_read_tfra( root, box, parent, level );
        case ISOM_BOX_TYPE_MFRO :
            return isom_read_mfro( root, box, parent, level );
        default :
            return isom_read_unknown_box( root, box, parent, level );
    }
}

static int isom_read_root( lsmash_root_t *root )
{
    lsmash_bs_t *bs = root->bs;
    if( !bs )
        return -1;
    isom_box_t box;
    if( root->flags & LSMASH_FILE_MODE_DUMP )
    {
        root->print = lsmash_create_entry_list();
        if( !root->print )
            return -1;
    }
    root->size = UINT64_MAX;
    int ret = isom_read_children( root, &box, root, 0 );
    root->size = box.size;
    lsmash_bs_empty( bs );
    if( ret < 0 )
        return ret;
    return isom_check_compatibility( root );
}

static uint32_t isom_get_sample_count( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsz )
        return 0;
    return trak->mdia->minf->stbl->stsz->sample_count;
}

static uint64_t isom_get_dts( isom_stts_t *stts, uint32_t sample_number )
{
    if( !stts || !stts->list )
        return 0;
    uint64_t dts = 0;
    uint32_t i = 1;
    lsmash_entry_t *entry;
    isom_stts_entry_t *data;
    for( entry = stts->list->head; entry; entry = entry->next )
    {
        data = (isom_stts_entry_t *)entry->data;
        if( !data )
            return 0;
        if( i + data->sample_count > sample_number )
            break;
        for( uint32_t j = 0; j < data->sample_count; j++ )
            dts += data->sample_delta;
        i += data->sample_count;
    }
    if( !entry )
        return 0;
    while( i++ < sample_number )
        dts += data->sample_delta;
    return dts;
}

#if 0
static uint64_t isom_get_cts( isom_stts_t *stts, isom_ctts_t *ctts, uint32_t sample_number )
{
    if( !stts || !stts->list )
        return 0;
    if( !ctts )
        return isom_get_dts( stts, sample_number );
    uint32_t i = 1;     /* This can be 0 (and then condition below shall be changed) but I dare use same algorithm with isom_get_dts. */
    lsmash_entry_t *entry;
    isom_ctts_entry_t *data;
    if( sample_number == 0 )
        return 0;
    for( entry = ctts->list->head; entry; entry = entry->next )
    {
        data = (isom_ctts_entry_t *)entry->data;
        if( !data )
            return 0;
        if( i + data->sample_count > sample_number )
            break;
        i += data->sample_count;
    }
    if( !entry )
        return 0;
    return isom_get_dts( stts, sample_number ) + data->sample_offset;
}
#endif

static int isom_replace_last_sample_delta( isom_stbl_t *stbl, uint32_t sample_delta )
{
    if( !stbl || !stbl->stts || !stbl->stts->list || !stbl->stts->list->tail || !stbl->stts->list->tail->data )
        return -1;
    isom_stts_entry_t *last_stts_data = (isom_stts_entry_t *)stbl->stts->list->tail->data;
    if( sample_delta != last_stts_data->sample_delta )
    {
        if( last_stts_data->sample_count > 1 )
        {
            last_stts_data->sample_count -= 1;
            if( isom_add_stts_entry( stbl, sample_delta ) )
                return -1;
        }
        else
            last_stts_data->sample_delta = sample_delta;
    }
    return 0;
}

static int isom_update_mdhd_duration( isom_trak_entry_t *trak, uint32_t last_sample_delta )
{
    if( !trak || !trak->root || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->minf || !trak->mdia->minf->stbl
     || !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list )
        return -1;
    lsmash_root_t *root = trak->root;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_stts_t *stts = stbl->stts;
    isom_ctts_t *ctts = stbl->ctts;
    isom_cslg_t *cslg = stbl->cslg;
    mdhd->duration = 0;
    uint32_t sample_count = isom_get_sample_count( trak );
    if( !sample_count )
    {
        /* Return error if non-fragmented movie has no samples. */
        if( !root->fragment && !stts->list->entry_count )
            return -1;
        return 0;
    }
    /* Now we have at least 1 sample, so do stts_entry. */
    lsmash_entry_t *last_stts = stts->list->tail;
    isom_stts_entry_t *last_stts_data = (isom_stts_entry_t *)last_stts->data;
    if( sample_count == 1 )
        mdhd->duration = last_stts_data->sample_delta;
    /* Now we have at least 2 samples,
     * but dunno whether 1 stts_entry which has 2 samples or 2 stts_entry which has 1 samle each. */
    else if( !ctts )
    {
        /* use dts instead of cts */
        mdhd->duration = isom_get_dts( stts, sample_count );
        if( last_sample_delta )
        {
            mdhd->duration += last_sample_delta;
            if( isom_replace_last_sample_delta( stbl, last_sample_delta ) )
                return -1;
        }
        else if( last_stts_data->sample_count > 1 )
            mdhd->duration += last_stts_data->sample_delta; /* no need to update last_stts_data->sample_delta */
        else
        {
            /* Remove the last entry. */
            if( lsmash_remove_entry( stts->list, stts->list->entry_count, NULL ) )
                return -1;
            /* copy the previous sample_delta. */
            ++ ((isom_stts_entry_t *)stts->list->tail->data)->sample_count;
            mdhd->duration += ((isom_stts_entry_t *)stts->list->tail->data)->sample_delta;
        }
    }
    else
    {
        if( !ctts->list || ctts->list->entry_count == 0 )
            return -1;
        uint64_t dts = 0;
        uint64_t max_cts = 0, max2_cts = 0, min_cts = UINT64_MAX;
        uint32_t max_offset = 0, min_offset = UINT32_MAX;
        uint32_t j, k;
        lsmash_entry_t *stts_entry = stts->list->head;
        lsmash_entry_t *ctts_entry = ctts->list->head;
        j = k = 0;
        for( uint32_t i = 0; i < sample_count; i++ )
        {
            if( !ctts_entry || !stts_entry )
                return -1;
            isom_stts_entry_t *stts_data = (isom_stts_entry_t *)stts_entry->data;
            isom_ctts_entry_t *ctts_data = (isom_ctts_entry_t *)ctts_entry->data;
            if( !stts_data || !ctts_data )
                return -1;
            uint64_t cts = dts + ctts_data->sample_offset;
            min_cts = LSMASH_MIN( min_cts, cts );
            max_offset = LSMASH_MAX( max_offset, ctts_data->sample_offset );
            min_offset = LSMASH_MIN( min_offset, ctts_data->sample_offset );
            if( max_cts < cts )
            {
                max2_cts = max_cts;
                max_cts = cts;
            }
            else if( max2_cts < cts )
                max2_cts = cts;
            dts += stts_data->sample_delta;
            /* If finished sample_count of current entry, move to next. */
            if( ++j == ctts_data->sample_count )
            {
                ctts_entry = ctts_entry->next;
                j = 0;
            }
            if( ++k == stts_data->sample_count )
            {
                stts_entry = stts_entry->next;
                k = 0;
            }
        }
        dts -= last_stts_data->sample_delta;
        if( root->fragment )
            /* Overall presentation is extended exceeding this initial movie.
             * So, any players shall display the movie exceeding the durations
             * indicated in Movie Header Box, Track Header Boxes and Media Header Boxes.
             * Samples up to the duration indicated in Movie Extends Header Box shall be displayed.
             * In the absence of Movie Extends Header Box, all samples shall be displayed. */
            mdhd->duration += dts + last_sample_delta;
        else
        {
            if( !last_sample_delta )
            {
                /* The spec allows an arbitrary value for the duration of the last sample. So, we pick last-1 sample's. */
                last_sample_delta = max_cts - max2_cts;
            }
            mdhd->duration = max_cts - min_cts + last_sample_delta;
            /* To match dts and media duration, update stts and mdhd relatively. */
            if( mdhd->duration > dts )
                last_sample_delta = mdhd->duration - dts;
            else
                mdhd->duration = dts + last_sample_delta;   /* media duration must not less than last dts. */
        }
        if( isom_replace_last_sample_delta( stbl, last_sample_delta ) )
            return -1;
        /* Explicit composition information and DTS shifting  */
        if( cslg || root->qt_compatible || root->max_isom_version >= 4 )
        {
            int64_t composition_end_time = max_cts + (max_cts - max2_cts);
            if( !root->fragment
             && (min_offset <= INT32_MAX) && (max_offset <= INT32_MAX)
             && (min_cts <= INT32_MAX) && (composition_end_time <= INT32_MAX) )
            {
                if( !cslg )
                {
                    if( isom_add_cslg( trak->mdia->minf->stbl ) )
                        return -1;
                    cslg = stbl->cslg;
                }
                cslg->compositionToDTSShift = 0;    /* We don't consider DTS shifting at present. */
                cslg->leastDecodeToDisplayDelta = min_offset;
                cslg->greatestDecodeToDisplayDelta = max_offset;
                cslg->compositionStartTime = min_cts;
                cslg->compositionEndTime = composition_end_time;
            }
            else
            {
                if( cslg )
                    free( cslg );
                stbl->cslg = NULL;
            }
        }
    }
    if( mdhd->duration > UINT32_MAX )
        mdhd->version = 1;
    return 0;
}

static int isom_update_mvhd_duration( isom_moov_t *moov )
{
    if( !moov || !moov->mvhd )
        return -1;
    isom_mvhd_t *mvhd = moov->mvhd;
    mvhd->duration = 0;
    for( lsmash_entry_t *entry = moov->trak_list->head; entry; entry = entry->next )
    {
        /* We pick maximum track duration as movie duration. */
        isom_trak_entry_t *data = (isom_trak_entry_t *)entry->data;
        if( !data || !data->tkhd )
            return -1;
        mvhd->duration = entry != moov->trak_list->head ? LSMASH_MAX( mvhd->duration, data->tkhd->duration ) : data->tkhd->duration;
    }
    if( mvhd->duration > UINT32_MAX )
        mvhd->version = 1;
    return 0;
}

static int isom_update_tkhd_duration( isom_trak_entry_t *trak )
{
    if( !trak || !trak->tkhd || !trak->root || !trak->root->moov )
        return -1;
    lsmash_root_t *root = trak->root;
    isom_tkhd_t *tkhd = trak->tkhd;
    tkhd->duration = 0;
    if( root->fragment || !trak->edts || !trak->edts->elst )
    {
        /* If this presentation might be extended or this track doesn't have edit list, calculate track duration from media duration. */
        if( !trak->mdia || !trak->mdia->mdhd || !root->moov->mvhd || !trak->mdia->mdhd->timescale )
            return -1;
        if( !trak->mdia->mdhd->duration && isom_update_mdhd_duration( trak, 0 ) )
            return -1;
        tkhd->duration = trak->mdia->mdhd->duration * ((double)root->moov->mvhd->timescale / trak->mdia->mdhd->timescale);
    }
    else
    {
        /* If the presentation won't be extended and this track has any edit, then track duration is just the sum of the segment_duartions. */
        for( lsmash_entry_t *entry = trak->edts->elst->list->head; entry; entry = entry->next )
        {
            isom_elst_entry_t *data = (isom_elst_entry_t *)entry->data;
            if( !data )
                return -1;
            tkhd->duration += data->segment_duration;
        }
    }
    if( tkhd->duration > UINT32_MAX )
        tkhd->version = 1;
    if( !root->fragment && !tkhd->duration )
        tkhd->duration = tkhd->version == 1 ? 0xffffffffffffffff : 0xffffffff;
    return isom_update_mvhd_duration( root->moov );
}

int lsmash_update_track_duration( lsmash_root_t *root, uint32_t track_ID, uint32_t last_sample_delta )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak )
        return -1;
    if( isom_update_mdhd_duration( trak, last_sample_delta ) )
        return -1;
    /* If the presentation won't be extended and this track has any edit, we don't change or update duration in tkhd and mvhd. */
    return (!root->fragment && trak->edts && trak->edts->elst) ? 0 : isom_update_tkhd_duration( trak );
}

int lsmash_set_avc_config( lsmash_root_t *root, uint32_t track_ID, uint32_t entry_number,
    uint8_t configurationVersion, uint8_t AVCProfileIndication, uint8_t profile_compatibility, uint8_t AVCLevelIndication, uint8_t lengthSizeMinusOne,
    uint8_t chroma_format, uint8_t bit_depth_luma_minus8, uint8_t bit_depth_chroma_minus8 )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_avc_entry_t *data = (isom_avc_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_avcC_t *avcC = (isom_avcC_t *)data->avcC;
    if( !avcC )
        return -1;
    avcC->configurationVersion = configurationVersion;
    avcC->AVCProfileIndication = AVCProfileIndication;
    avcC->profile_compatibility = profile_compatibility;
    avcC->AVCLevelIndication = AVCLevelIndication;
    avcC->lengthSizeMinusOne = lengthSizeMinusOne;
    if( ISOM_REQUIRES_AVCC_EXTENSION( AVCProfileIndication ) )
    {
        avcC->chroma_format = chroma_format;
        avcC->bit_depth_luma_minus8 = bit_depth_luma_minus8;
        avcC->bit_depth_chroma_minus8 = bit_depth_chroma_minus8;
    }
    return 0;
}

static int isom_update_bitrate_info( isom_mdia_t *mdia )
{
    if( !mdia || !mdia->mdhd || !mdia->minf || !mdia->minf->stbl
     || !mdia->minf->stbl->stsd || !mdia->minf->stbl->stsd->list
     || !mdia->minf->stbl->stsz || !mdia->minf->stbl->stts || !mdia->minf->stbl->stts->list )
        return -1;
    /* Not supporting multi sample entries yet. */
    isom_sample_entry_t *sample_entry = (isom_sample_entry_t *)lsmash_get_entry_data( mdia->minf->stbl->stsd->list, 1 );
    if( !sample_entry )
        return -1;
    struct bitrate_info_t
    {
        uint32_t bufferSizeDB;
        uint32_t maxBitrate;
        uint32_t avgBitrate;
    } info = { 0, 0, 0 };
    uint32_t i = 0;
    uint32_t rate = 0;
    uint32_t time_wnd = 0;
    uint32_t timescale = mdia->mdhd->timescale;
    uint64_t dts = 0;
    isom_stsz_t *stsz = mdia->minf->stbl->stsz;
    lsmash_entry_t *stsz_entry = stsz->list ? stsz->list->head : NULL;
    lsmash_entry_t *stts_entry = mdia->minf->stbl->stts->list->head;
    isom_stts_entry_t *stts_data = NULL;
    while( stts_entry )
    {
        uint32_t size;
        if( stsz->list )
        {
            if( !stsz_entry )
                break;
            isom_stsz_entry_t *stsz_data = (isom_stsz_entry_t *)stsz_entry->data;
            if( !stsz_data )
                return -1;
            size = stsz_data->entry_size;
            stsz_entry = stsz_entry->next;
        }
        else
            size = stsz->sample_size;
        if( stts_data )
            dts += stts_data->sample_delta;
        stts_data = (isom_stts_entry_t *)stts_entry->data;
        if( ++i == stts_data->sample_count )
        {
            stts_entry = stts_entry->next;
            i = 0;
        }
        if( info.bufferSizeDB < size )
            info.bufferSizeDB = size;
        info.avgBitrate += size;
        rate += size;
        if( dts > time_wnd + timescale )
        {
            if( rate > info.maxBitrate )
                info.maxBitrate = rate;
            time_wnd = dts;
            rate = 0;
        }
    }
    double duration = (double)mdia->mdhd->duration / timescale;
    info.avgBitrate = (uint32_t)(info.avgBitrate / duration);
    if( !info.maxBitrate )
        info.maxBitrate = info.avgBitrate;
    /* move to bps */
    info.maxBitrate *= 8;
    info.avgBitrate *= 8;
    /* set bitrate info */
    switch( sample_entry->type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
        case ISOM_CODEC_TYPE_AVC2_VIDEO :
        case ISOM_CODEC_TYPE_AVCP_VIDEO :
        {
            isom_avc_entry_t *stsd_data = (isom_avc_entry_t *)sample_entry;
            if( !stsd_data )
                return -1;
            //isom_btrt_t *btrt = (isom_btrt_t *)stsd_data->btrt;
            isom_btrt_t *btrt = stsd_data->btrt;
            if( btrt )
            {
                btrt->bufferSizeDB = info.bufferSizeDB;
                btrt->maxBitrate   = info.maxBitrate;
                btrt->avgBitrate   = info.avgBitrate;
            }
            break;
        }
        case ISOM_CODEC_TYPE_MP4A_AUDIO :
        {
            isom_esds_t *esds = NULL;
            if( ((isom_audio_entry_t *)sample_entry)->version )
            {
                /* MPEG-4 Audio in QTFF */
                isom_audio_entry_t *stsd_data = (isom_audio_entry_t *)sample_entry;
                if( !stsd_data || !stsd_data->wave || !stsd_data->wave->esds || !stsd_data->wave->esds->ES )
                    return -1;
                esds = stsd_data->wave->esds;
            }
            else
            {
                isom_audio_entry_t *stsd_data = (isom_audio_entry_t *)sample_entry;
                if( !stsd_data || !stsd_data->esds || !stsd_data->esds->ES )
                    return -1;
                esds = stsd_data->esds;
            }
            /* FIXME: avgBitrate is 0 only if VBR in proper. */
            if( mp4sys_update_DecoderConfigDescriptor( esds->ES, info.bufferSizeDB, info.maxBitrate, 0 ) )
                return -1;
            break;
        }
        case ISOM_CODEC_TYPE_ALAC_AUDIO :
        {
            isom_audio_entry_t *alac = (isom_audio_entry_t *)sample_entry;
            if( !alac || alac->exdata_length < 36 || !alac->exdata )
                return -1;
            uint8_t *exdata = (uint8_t *)alac->exdata + 28;
            exdata[0] = (info.avgBitrate >> 24) & 0xff;
            exdata[1] = (info.avgBitrate >> 16) & 0xff;
            exdata[2] = (info.avgBitrate >>  8) & 0xff;
            exdata[3] =  info.avgBitrate        & 0xff;
            break;
        }
        default :
            break;
    }
    return 0;
}

static int isom_check_mandatory_boxes( lsmash_root_t *root )
{
    if( !root )
        return -1;
    if( !root->moov || !root->moov->mvhd )
        return -1;
    if( !root->moov->trak_list )
        return -1;
    /* A movie requires at least one track. */
    if( !root->moov->trak_list->head )
        return -1;
    for( lsmash_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
        if( !trak
         || !trak->tkhd
         || !trak->mdia
         || !trak->mdia->mdhd
         || !trak->mdia->hdlr
         || !trak->mdia->minf
         || !trak->mdia->minf->dinf
         || !trak->mdia->minf->dinf->dref
         || !trak->mdia->minf->stbl
         || !trak->mdia->minf->stbl->stsd
         || !trak->mdia->minf->stbl->stsz
         || !trak->mdia->minf->stbl->stts
         || !trak->mdia->minf->stbl->stsc
         || !trak->mdia->minf->stbl->stco )
            return -1;
        if( root->qt_compatible && !trak->mdia->minf->hdlr )
            return -1;
        isom_stbl_t *stbl = trak->mdia->minf->stbl;
        if( !stbl->stsd->list || !stbl->stsd->list->head )
            return -1;
        if( !root->fragment
         && (!stbl->stsd->list || !stbl->stsd->list->head
         || !stbl->stts->list || !stbl->stts->list->head
         || !stbl->stsc->list || !stbl->stsc->list->head
         || !stbl->stco->list || !stbl->stco->list->head) )
            return -1;
    }
    if( !root->fragment )
        return 0;
    if( !root->moov->mvex || !root->moov->mvex->trex_list )
        return -1;
    for( lsmash_entry_t *entry = root->moov->mvex->trex_list->head; entry; entry = entry->next )
        if( !entry->data )  /* trex */
            return -1;
    return 0;
}

static inline uint64_t isom_get_current_mp4time( void )
{
    return (uint64_t)time( NULL ) + ISOM_MAC_EPOCH_OFFSET;
}

static int isom_set_media_creation_time( isom_trak_entry_t *trak, uint64_t current_mp4time )
{
    if( !trak->mdia || !trak->mdia->mdhd )
        return -1;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    if( !mdhd->creation_time )
        mdhd->creation_time = mdhd->modification_time = current_mp4time;
    return 0;
}

static int isom_set_track_creation_time( isom_trak_entry_t *trak, uint64_t current_mp4time )
{
    if( !trak || !trak->tkhd )
        return -1;
    isom_tkhd_t *tkhd = trak->tkhd;
    if( !tkhd->creation_time )
        tkhd->creation_time = tkhd->modification_time = current_mp4time;
    if( isom_set_media_creation_time( trak, current_mp4time ) )
        return -1;
    return 0;
}

static int isom_set_movie_creation_time( lsmash_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd || !root->moov->trak_list )
        return -1;
    uint64_t current_mp4time = isom_get_current_mp4time();
    for( uint32_t i = 1; i <= root->moov->trak_list->entry_count; i++ )
        if( isom_set_track_creation_time( isom_get_trak( root, i ), current_mp4time ) )
            return -1;
    isom_mvhd_t *mvhd = root->moov->mvhd;
    if( !mvhd->creation_time )
        mvhd->creation_time = mvhd->modification_time = current_mp4time;
    return 0;
}

#define CHECK_LARGESIZE( size ) if( (size) > UINT32_MAX ) (size) += 8

static uint64_t isom_update_mvhd_size( isom_mvhd_t *mvhd )
{
    if( !mvhd )
        return 0;
    mvhd->version = 0;
    if( mvhd->creation_time > UINT32_MAX || mvhd->modification_time > UINT32_MAX || mvhd->duration > UINT32_MAX )
        mvhd->version = 1;
    mvhd->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 96 + (uint64_t)mvhd->version * 12;
    CHECK_LARGESIZE( mvhd->size );
    return mvhd->size;
}

static uint64_t isom_update_iods_size( isom_iods_t *iods )
{
    if( !iods || !iods->OD )
        return 0;
    iods->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + mp4sys_update_ObjectDescriptor_size( iods->OD );
    CHECK_LARGESIZE( iods->size );
    return iods->size;
}

static uint64_t isom_update_tkhd_size( isom_tkhd_t *tkhd )
{
    if( !tkhd )
        return 0;
    tkhd->version = 0;
    if( tkhd->creation_time > UINT32_MAX || tkhd->modification_time > UINT32_MAX || tkhd->duration > UINT32_MAX )
        tkhd->version = 1;
    tkhd->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 80 + (uint64_t)tkhd->version * 12;
    CHECK_LARGESIZE( tkhd->size );
    return tkhd->size;
}

static uint64_t isom_update_clef_size( isom_clef_t *clef )
{
    if( !clef )
        return 0;
    clef->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 8;
    CHECK_LARGESIZE( clef->size );
    return clef->size;
}

static uint64_t isom_update_prof_size( isom_prof_t *prof )
{
    if( !prof )
        return 0;
    prof->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 8;
    CHECK_LARGESIZE( prof->size );
    return prof->size;
}

static uint64_t isom_update_enof_size( isom_enof_t *enof )
{
    if( !enof )
        return 0;
    enof->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 8;
    CHECK_LARGESIZE( enof->size );
    return enof->size;
}

static uint64_t isom_update_tapt_size( isom_tapt_t *tapt )
{
    if( !tapt )
        return 0;
    tapt->size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_clef_size( tapt->clef )
        + isom_update_prof_size( tapt->prof )
        + isom_update_enof_size( tapt->enof );
    CHECK_LARGESIZE( tapt->size );
    return tapt->size;
}

static uint64_t isom_update_elst_size( isom_elst_t *elst )
{
    if( !elst || !elst->list )
        return 0;
    uint32_t i = 0;
    elst->version = 0;
    for( lsmash_entry_t *entry = elst->list->head; entry; entry = entry->next, i++ )
    {
        isom_elst_entry_t *data = (isom_elst_entry_t *)entry->data;
        if( data->segment_duration > UINT32_MAX || data->media_time > UINT32_MAX )
            elst->version = 1;
    }
    elst->size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (uint64_t)i * ( elst->version ? 20 : 12 );
    CHECK_LARGESIZE( elst->size );
    return elst->size;
}

static uint64_t isom_update_edts_size( isom_edts_t *edts )
{
    if( !edts )
        return 0;
    edts->size = ISOM_DEFAULT_BOX_HEADER_SIZE + isom_update_elst_size( edts->elst );
    CHECK_LARGESIZE( edts->size );
    return edts->size;
}

static uint64_t isom_update_tref_size( isom_tref_t *tref )
{
    if( !tref )
        return 0;
    tref->size = ISOM_DEFAULT_BOX_HEADER_SIZE;
    if( tref->ref_list )
        for( lsmash_entry_t *entry = tref->ref_list->head; entry; entry = entry->next )
        {
            isom_tref_type_t *ref = (isom_tref_type_t *)entry->data;
            ref->size = ISOM_DEFAULT_BOX_HEADER_SIZE + (uint64_t)ref->ref_count * 4;
            CHECK_LARGESIZE( ref->size );
            tref->size += ref->size;
        }
    CHECK_LARGESIZE( tref->size );
    return tref->size;
}

static uint64_t isom_update_mdhd_size( isom_mdhd_t *mdhd )
{
    if( !mdhd )
        return 0;
    mdhd->version = 0;
    if( mdhd->creation_time > UINT32_MAX || mdhd->modification_time > UINT32_MAX || mdhd->duration > UINT32_MAX )
        mdhd->version = 1;
    mdhd->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 20 + (uint64_t)mdhd->version * 12;
    CHECK_LARGESIZE( mdhd->size );
    return mdhd->size;
}

static uint64_t isom_update_hdlr_size( isom_hdlr_t *hdlr )
{
    if( !hdlr )
        return 0;
    hdlr->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 20 + (uint64_t)hdlr->componentName_length;
    CHECK_LARGESIZE( hdlr->size );
    return hdlr->size;
}

static uint64_t isom_update_dref_entry_size( isom_dref_entry_t *urln )
{
    if( !urln )
        return 0;
    urln->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + (uint64_t)urln->name_length + urln->location_length;
    CHECK_LARGESIZE( urln->size );
    return urln->size;
}

static uint64_t isom_update_dref_size( isom_dref_t *dref )
{
    if( !dref || !dref->list )
        return 0;
    dref->size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE;
    if( dref->list )
        for( lsmash_entry_t *entry = dref->list->head; entry; entry = entry->next )
        {
            isom_dref_entry_t *data = (isom_dref_entry_t *)entry->data;
            dref->size += isom_update_dref_entry_size( data );
        }
    CHECK_LARGESIZE( dref->size );
    return dref->size;
}

static uint64_t isom_update_dinf_size( isom_dinf_t *dinf )
{
    if( !dinf )
        return 0;
    dinf->size = ISOM_DEFAULT_BOX_HEADER_SIZE + isom_update_dref_size( dinf->dref );
    CHECK_LARGESIZE( dinf->size );
    return dinf->size;
}

static uint64_t isom_update_vmhd_size( isom_vmhd_t *vmhd )
{
    if( !vmhd )
        return 0;
    vmhd->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 8;
    CHECK_LARGESIZE( vmhd->size );
    return vmhd->size;
}

static uint64_t isom_update_smhd_size( isom_smhd_t *smhd )
{
    if( !smhd )
        return 0;
    smhd->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 4;
    CHECK_LARGESIZE( smhd->size );
    return smhd->size;
}

static uint64_t isom_update_hmhd_size( isom_hmhd_t *hmhd )
{
    if( !hmhd )
        return 0;
    hmhd->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 16;
    CHECK_LARGESIZE( hmhd->size );
    return hmhd->size;
}

static uint64_t isom_update_nmhd_size( isom_nmhd_t *nmhd )
{
    if( !nmhd )
        return 0;
    nmhd->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE;
    CHECK_LARGESIZE( nmhd->size );
    return nmhd->size;
}

static uint64_t isom_update_gmin_size( isom_gmin_t *gmin )
{
    if( !gmin )
        return 0;
    gmin->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 12;
    CHECK_LARGESIZE( gmin->size );
    return gmin->size;
}

static uint64_t isom_update_text_size( isom_text_t *text )
{
    if( !text )
        return 0;
    text->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 36;
    CHECK_LARGESIZE( text->size );
    return text->size;
}

static uint64_t isom_update_gmhd_size( isom_gmhd_t *gmhd )
{
    if( !gmhd )
        return 0;
    gmhd->size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_gmin_size( gmhd->gmin )
        + isom_update_text_size( gmhd->text );
    CHECK_LARGESIZE( gmhd->size );
    return gmhd->size;
}

static uint64_t isom_update_btrt_size( isom_btrt_t *btrt )
{
    if( !btrt )
        return 0;
    btrt->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 12;
    CHECK_LARGESIZE( btrt->size );
    return btrt->size;
}

static uint64_t isom_update_pasp_size( isom_pasp_t *pasp )
{
    if( !pasp )
        return 0;
    pasp->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 8;
    CHECK_LARGESIZE( pasp->size );
    return pasp->size;
}

static uint64_t isom_update_clap_size( isom_clap_t *clap )
{
    if( !clap )
        return 0;
    clap->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 32;
    CHECK_LARGESIZE( clap->size );
    return clap->size;
}

static uint64_t isom_update_colr_size( isom_colr_t *colr )
{
    if( !colr || colr->color_parameter_type == QT_COLOR_PARAMETER_TYPE_PROF )
        return 0;
    colr->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 10;
    CHECK_LARGESIZE( colr->size );
    return colr->size;
}

static uint64_t isom_update_stsl_size( isom_stsl_t *stsl )
{
    if( !stsl )
        return 0;
    stsl->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 6;
    CHECK_LARGESIZE( stsl->size );
    return stsl->size;
}

static uint64_t isom_update_visual_extension_size( isom_visual_entry_t *visual )
{
    if( !visual )
        return 0;
    return isom_update_clap_size( visual->clap )
         + isom_update_pasp_size( visual->pasp )
         + isom_update_colr_size( visual->colr )
         + isom_update_stsl_size( visual->stsl );
}

static uint64_t isom_update_avcC_size( isom_avcC_t *avcC )
{
    if( !avcC || !avcC->sequenceParameterSets || !avcC->pictureParameterSets )
        return 0;
    uint64_t size = ISOM_DEFAULT_BOX_HEADER_SIZE + 7;
    for( lsmash_entry_t *entry = avcC->sequenceParameterSets->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
        size += 2 + data->parameterSetLength;
    }
    for( lsmash_entry_t *entry = avcC->pictureParameterSets->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
        size += 2 + data->parameterSetLength;
    }
    if( ISOM_REQUIRES_AVCC_EXTENSION( avcC->AVCProfileIndication ) )
    {
        size += 4;
        for( lsmash_entry_t *entry = avcC->sequenceParameterSetExt->head; entry; entry = entry->next )
        {
            isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
            size += 2 + data->parameterSetLength;
        }
    }
    avcC->size = size;
    CHECK_LARGESIZE( avcC->size );
    return avcC->size;
}

static uint64_t isom_update_avc_entry_size( isom_avc_entry_t *avc )
{
    if( !avc
     || ((avc->type != ISOM_CODEC_TYPE_AVC1_VIDEO)
     && (avc->type != ISOM_CODEC_TYPE_AVC2_VIDEO)
     && (avc->type != ISOM_CODEC_TYPE_AVCP_VIDEO)) )
        return 0;
    avc->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 78
        + isom_update_visual_extension_size( (isom_visual_entry_t *)avc )
        + isom_update_avcC_size( avc->avcC )
        + isom_update_btrt_size( avc->btrt );
    CHECK_LARGESIZE( avc->size );
    return avc->size;
}

static uint64_t isom_update_esds_size( isom_esds_t *esds )
{
    if( !esds )
        return 0;
    esds->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + mp4sys_update_ES_Descriptor_size( esds->ES );
    CHECK_LARGESIZE( esds->size );
    return esds->size;
}

#if 0
static uint64_t isom_update_visual_entry_size( isom_visual_entry_t *visual )
{
    if( !visual || visual->type != ISOM_CODEC_TYPE_visual_VIDEO )
        return 0;
    visual->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 78 + isom_update_visual_extension_size( visual );
    CHECK_LARGESIZE( visual->size );
    return visual->size;
}

static uint64_t isom_update_mp4v_entry_size( isom_mp4v_entry_t *mp4v )
{
    if( !mp4v || mp4v->type != ISOM_CODEC_TYPE_MP4V_VIDEO )
        return 0;
    mp4v->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 78
        + isom_update_visual_extension_size( (isom_visual_entry_t *)mp4v )
        + isom_update_esds_size( mp4v->esds );
    CHECK_LARGESIZE( mp4v->size );
    return mp4v->size;
}

static uint64_t isom_update_mp4s_entry_size( isom_mp4s_entry_t *mp4s )
{
    if( !mp4s || mp4s->type != ISOM_CODEC_TYPE_MP4S_SYSTEM )
        return 0;
    mp4s->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 8 + isom_update_esds_size( mp4s->esds );
    CHECK_LARGESIZE( mp4s->size );
    return mp4s->size;
}
#endif

static uint64_t isom_update_frma_size( isom_frma_t *frma )
{
    if( !frma )
        return 0;
    frma->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 4;
    CHECK_LARGESIZE( frma->size );
    return frma->size;
}

static uint64_t isom_update_enda_size( isom_enda_t *enda )
{
    if( !enda )
        return 0;
    enda->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 2;
    CHECK_LARGESIZE( enda->size );
    return enda->size;
}

static uint64_t isom_update_mp4a_size( isom_mp4a_t *mp4a )
{
    if( !mp4a )
        return 0;
    mp4a->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 4;
    CHECK_LARGESIZE( mp4a->size );
    return mp4a->size;
}

static uint64_t isom_update_terminator_size( isom_terminator_t *terminator )
{
    if( !terminator )
        return 0;
    terminator->size = ISOM_DEFAULT_BOX_HEADER_SIZE;
    CHECK_LARGESIZE( terminator->size );
    return terminator->size;
}

static uint64_t isom_update_wave_size( isom_wave_t *wave )
{
    if( !wave )
        return 0;
    wave->size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_frma_size( wave->frma )
        + isom_update_enda_size( wave->enda )
        + isom_update_mp4a_size( wave->mp4a )
        + isom_update_esds_size( wave->esds )
        + isom_update_terminator_size( wave->terminator );
    CHECK_LARGESIZE( wave->size );
    return wave->size;
}

static uint64_t isom_update_chan_size( isom_chan_t *chan )
{
    if( !chan )
        return 0;
    chan->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 12 + 20 * (uint64_t)chan->numberChannelDescriptions;
    CHECK_LARGESIZE( chan->size );
    return chan->size;
}

static uint64_t isom_update_audio_entry_size( isom_audio_entry_t *audio )
{
    if( !audio )
        return 0;
    audio->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 28
        + isom_update_esds_size( audio->esds )
        + isom_update_wave_size( audio->wave )
        + isom_update_chan_size( audio->chan )
        + (uint64_t)audio->exdata_length;
    if( audio->version == 1 )
        audio->size += 16;
    else if( audio->version == 2 )
        audio->size += 36;
    CHECK_LARGESIZE( audio->size );
    return audio->size;
}

static uint64_t isom_update_text_entry_size( isom_text_entry_t *text )
{
    if( !text )
        return 0;
    text->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 51 + (uint64_t)text->font_name_length;
    CHECK_LARGESIZE( text->size );
    return text->size;
}

static uint64_t isom_update_ftab_size( isom_ftab_t *ftab )
{
    if( !ftab || !ftab->list )
        return 0;
    ftab->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 2;
    for( lsmash_entry_t *entry = ftab->list->head; entry; entry = entry->next )
    {
        isom_font_record_t *data = (isom_font_record_t *)entry->data;
        ftab->size += 3 + data->font_name_length;
    }
    CHECK_LARGESIZE( ftab->size );
    return ftab->size;
}

static uint64_t isom_update_tx3g_entry_size( isom_tx3g_entry_t *tx3g )
{
    if( !tx3g )
        return 0;
    tx3g->size = ISOM_DEFAULT_BOX_HEADER_SIZE + 38 + isom_update_ftab_size( tx3g->ftab );
    CHECK_LARGESIZE( tx3g->size );
    return tx3g->size;
}

static uint64_t isom_update_stsd_size( isom_stsd_t *stsd )
{
    if( !stsd || !stsd->list )
        return 0;
    uint64_t size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE;
    for( lsmash_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_sample_entry_t *data = (isom_sample_entry_t *)entry->data;
        switch( data->type )
        {
            case ISOM_CODEC_TYPE_AVC1_VIDEO :
#if 0
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
            case ISOM_CODEC_TYPE_SVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC2_VIDEO :
#endif
                size += isom_update_avc_entry_size( (isom_avc_entry_t *)data );
                break;
#if 0
            case ISOM_CODEC_TYPE_MP4V_VIDEO :
                size += isom_update_mp4v_entry_size( (isom_mp4v_entry_t *)data );
                break;
            case ISOM_CODEC_TYPE_MP4S_SYSTEM :
                size += isom_update_mp4s_entry_size( (isom_mp4s_entry_t *)data );
                break;
            case ISOM_CODEC_TYPE_DRAC_VIDEO :
            case ISOM_CODEC_TYPE_ENCV_VIDEO :
            case ISOM_CODEC_TYPE_MJP2_VIDEO :
            case ISOM_CODEC_TYPE_S263_VIDEO :
            case ISOM_CODEC_TYPE_VC_1_VIDEO :
                size += isom_update_visual_entry_size( (isom_visual_entry_t *)data );
                break;
#endif
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
            case QT_CODEC_TYPE_23NI_AUDIO :
            case QT_CODEC_TYPE_NONE_AUDIO :
            case QT_CODEC_TYPE_LPCM_AUDIO :
            case QT_CODEC_TYPE_RAW_AUDIO :
            case QT_CODEC_TYPE_SOWT_AUDIO :
            case QT_CODEC_TYPE_TWOS_AUDIO :
            case QT_CODEC_TYPE_FL32_AUDIO :
            case QT_CODEC_TYPE_FL64_AUDIO :
            case QT_CODEC_TYPE_IN24_AUDIO :
            case QT_CODEC_TYPE_IN32_AUDIO :
            case QT_CODEC_TYPE_NOT_SPECIFIED :
#if 0
            case ISOM_CODEC_TYPE_DRA1_AUDIO :
            case ISOM_CODEC_TYPE_DTSC_AUDIO :
            case ISOM_CODEC_TYPE_DTSH_AUDIO :
            case ISOM_CODEC_TYPE_DTSL_AUDIO :
            case ISOM_CODEC_TYPE_EC_3_AUDIO :
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
                size += isom_update_audio_entry_size( (isom_audio_entry_t *)data );
                break;
            case ISOM_CODEC_TYPE_TX3G_TEXT :
                size += isom_update_tx3g_entry_size( (isom_tx3g_entry_t *)data );
                break;
            case QT_CODEC_TYPE_TEXT_TEXT :
                size += isom_update_text_entry_size( (isom_text_entry_t *)data );
                break;
            default :
                break;
        }
    }
    stsd->size = size;
    CHECK_LARGESIZE( stsd->size );
    return stsd->size;
}

static uint64_t isom_update_stts_size( isom_stts_t *stts )
{
    if( !stts || !stts->list )
        return 0;
    stts->size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (uint64_t)stts->list->entry_count * 8;
    CHECK_LARGESIZE( stts->size );
    return stts->size;
}

static uint64_t isom_update_ctts_size( isom_ctts_t *ctts )
{
    if( !ctts || !ctts->list )
        return 0;
    ctts->size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (uint64_t)ctts->list->entry_count * 8;
    CHECK_LARGESIZE( ctts->size );
    return ctts->size;
}

static uint64_t isom_update_cslg_size( isom_cslg_t *cslg )
{
    if( !cslg )
        return 0;
    cslg->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 20;
    CHECK_LARGESIZE( cslg->size );
    return cslg->size;
}

static uint64_t isom_update_stsz_size( isom_stsz_t *stsz )
{
    if( !stsz )
        return 0;
    stsz->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 8 + ( stsz->list ? (uint64_t)stsz->list->entry_count * 4 : 0 );
    CHECK_LARGESIZE( stsz->size );
    return stsz->size;
}

static uint64_t isom_update_stss_size( isom_stss_t *stss )
{
    if( !stss || !stss->list )
        return 0;
    stss->size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (uint64_t)stss->list->entry_count * 4;
    CHECK_LARGESIZE( stss->size );
    return stss->size;
}

static uint64_t isom_update_stps_size( isom_stps_t *stps )
{
    if( !stps || !stps->list )
        return 0;
    stps->size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (uint64_t)stps->list->entry_count * 4;
    CHECK_LARGESIZE( stps->size );
    return stps->size;
}

static uint64_t isom_update_sdtp_size( isom_sdtp_t *sdtp )
{
    if( !sdtp || !sdtp->list )
        return 0;
    sdtp->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + (uint64_t)sdtp->list->entry_count;
    CHECK_LARGESIZE( sdtp->size );
    return sdtp->size;
}

static uint64_t isom_update_stsc_size( isom_stsc_t *stsc )
{
    if( !stsc || !stsc->list )
        return 0;
    stsc->size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (uint64_t)stsc->list->entry_count * 12;
    CHECK_LARGESIZE( stsc->size );
    return stsc->size;
}

static uint64_t isom_update_stco_size( isom_stco_t *stco )
{
    if( !stco || !stco->list )
        return 0;
    stco->size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (uint64_t)stco->list->entry_count * (stco->large_presentation ? 8 : 4);
    CHECK_LARGESIZE( stco->size );
    return stco->size;
}

static uint64_t isom_update_sbgp_size( isom_sbgp_entry_t *sbgp )
{
    if( !sbgp || !sbgp->list )
        return 0;
    sbgp->size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + 4 + (uint64_t)sbgp->list->entry_count * 8;
    CHECK_LARGESIZE( sbgp->size );
    return sbgp->size;
}

static uint64_t isom_update_sgpd_size( isom_sgpd_entry_t *sgpd )
{
    if( !sgpd || !sgpd->list )
        return 0;
    uint64_t size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (1 + (sgpd->version == 1)) * 4;
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
    CHECK_LARGESIZE( sgpd->size );
    return sgpd->size;
}

static uint64_t isom_update_stbl_size( isom_stbl_t *stbl )
{
    if( !stbl )
        return 0;
    stbl->size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_stsd_size( stbl->stsd )
        + isom_update_stts_size( stbl->stts )
        + isom_update_ctts_size( stbl->ctts )
        + isom_update_cslg_size( stbl->cslg )
        + isom_update_stsz_size( stbl->stsz )
        + isom_update_stss_size( stbl->stss )
        + isom_update_stps_size( stbl->stps )
        + isom_update_sdtp_size( stbl->sdtp )
        + isom_update_stsc_size( stbl->stsc )
        + isom_update_stco_size( stbl->stco );
    if( stbl->sgpd_list )
        for( lsmash_entry_t *entry = stbl->sgpd_list->head; entry; entry = entry->next )
            stbl->size += isom_update_sgpd_size( (isom_sgpd_entry_t *)entry->data );
    if( stbl->sbgp_list )
        for( lsmash_entry_t *entry = stbl->sbgp_list->head; entry; entry = entry->next )
            stbl->size += isom_update_sbgp_size( (isom_sbgp_entry_t *)entry->data );
    CHECK_LARGESIZE( stbl->size );
    return stbl->size;
}

static uint64_t isom_update_minf_size( isom_minf_t *minf )
{
    if( !minf )
        return 0;
    minf->size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_vmhd_size( minf->vmhd )
        + isom_update_smhd_size( minf->smhd )
        + isom_update_hmhd_size( minf->hmhd )
        + isom_update_nmhd_size( minf->nmhd )
        + isom_update_gmhd_size( minf->gmhd )
        + isom_update_hdlr_size( minf->hdlr )
        + isom_update_dinf_size( minf->dinf )
        + isom_update_stbl_size( minf->stbl );
    CHECK_LARGESIZE( minf->size );
    return minf->size;
}

static uint64_t isom_update_mdia_size( isom_mdia_t *mdia )
{
    if( !mdia )
        return 0;
    mdia->size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_mdhd_size( mdia->mdhd )
        + isom_update_hdlr_size( mdia->hdlr )
        + isom_update_minf_size( mdia->minf );
    CHECK_LARGESIZE( mdia->size );
    return mdia->size;
}

static uint64_t isom_update_chpl_size( isom_chpl_t *chpl )
{
    if( !chpl )
        return 0;
    chpl->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 4 * (chpl->version == 1) + 1;
    for( lsmash_entry_t *entry = chpl->list->head; entry; entry = entry->next )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        chpl->size += 9 + data->chapter_name_length;
    }
    CHECK_LARGESIZE( chpl->size );
    return chpl->size;
}

static uint64_t isom_update_udta_size( isom_udta_t *udta_moov, isom_udta_t *udta_trak )
{
    isom_udta_t *udta = udta_trak ? udta_trak : udta_moov ? udta_moov : NULL;
    if( !udta )
        return 0;
    udta->size = ISOM_DEFAULT_BOX_HEADER_SIZE + ( udta_moov ? isom_update_chpl_size( udta->chpl ) : 0 );
    CHECK_LARGESIZE( udta->size );
    return udta->size;
}

static uint64_t isom_update_trak_entry_size( isom_trak_entry_t *trak )
{
    if( !trak )
        return 0;
    trak->size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_tkhd_size( trak->tkhd )
        + isom_update_tapt_size( trak->tapt )
        + isom_update_edts_size( trak->edts )
        + isom_update_tref_size( trak->tref )
        + isom_update_mdia_size( trak->mdia )
        + isom_update_udta_size( NULL, trak->udta );
    CHECK_LARGESIZE( trak->size );
    return trak->size;
}

static uint64_t isom_update_mehd_size( isom_mehd_t *mehd )
{
    if( !mehd )
        return 0;
    if( mehd->fragment_duration > UINT32_MAX )
        mehd->version = 1;
    mehd->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 4 * (1 + (mehd->version == 1));
    CHECK_LARGESIZE( mehd->size );
    return mehd->size;
}

static uint64_t isom_update_trex_entry_size( isom_trex_entry_t *trex )
{
    if( !trex )
        return 0;
    trex->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 20;
    CHECK_LARGESIZE( trex->size );
    return trex->size;
}

static uint64_t isom_update_mvex_size( isom_mvex_t *mvex )
{
    if( !mvex )
        return 0;
    mvex->size = ISOM_DEFAULT_BOX_HEADER_SIZE;
    if( mvex->trex_list )
        for( lsmash_entry_t *entry = mvex->trex_list->head; entry; entry = entry->next )
        {
            isom_trex_entry_t *trex = (isom_trex_entry_t *)entry->data;
            mvex->size += isom_update_trex_entry_size( trex );
        }
    if( mvex->root->bs->stream != stdout )
        mvex->size += mvex->mehd ? isom_update_mehd_size( mvex->mehd ) : 20;    /* 20 bytes is of placeholder. */
    CHECK_LARGESIZE( mvex->size );
    return mvex->size;
}

static int isom_update_moov_size( isom_moov_t *moov )
{
    if( !moov )
        return -1;
    moov->size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_mvhd_size( moov->mvhd )
        + isom_update_iods_size( moov->iods )
        + isom_update_udta_size( moov->udta, NULL )
        + isom_update_mvex_size( moov->mvex );
    if( moov->trak_list )
        for( lsmash_entry_t *entry = moov->trak_list->head; entry; entry = entry->next )
        {
            isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
            moov->size += isom_update_trak_entry_size( trak );
        }
    CHECK_LARGESIZE( moov->size );
    return 0;
}

static uint64_t isom_update_mfhd_size( isom_mfhd_t *mfhd )
{
    if( !mfhd )
        return 0;
    mfhd->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 4;
    CHECK_LARGESIZE( mfhd->size );
    return mfhd->size;
}

static uint64_t isom_update_tfhd_size( isom_tfhd_t *tfhd )
{
    if( !tfhd )
        return 0;
    tfhd->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE
               + 4
               + 8 * !!( tfhd->flags & ISOM_TF_FLAGS_BASE_DATA_OFFSET_PRESENT         )
               + 4 * !!( tfhd->flags & ISOM_TF_FLAGS_SAMPLE_DESCRIPTION_INDEX_PRESENT )
               + 4 * !!( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT  )
               + 4 * !!( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_SIZE_PRESENT      )
               + 4 * !!( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT     );
    CHECK_LARGESIZE( tfhd->size );
    return tfhd->size;
}

static uint64_t isom_update_trun_entry_size( isom_trun_entry_t *trun )
{
    if( !trun )
        return 0;
    trun->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE
               + 4
               + 4 * !!( trun->flags & ISOM_TR_FLAGS_DATA_OFFSET_PRESENT        )
               + 4 * !!( trun->flags & ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT );
    uint64_t row_size = 4 * !!( trun->flags & ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT                )
                      + 4 * !!( trun->flags & ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT                    )
                      + 4 * !!( trun->flags & ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT                   )
                      + 4 * !!( trun->flags & ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT );
    trun->size += row_size * trun->sample_count;
    CHECK_LARGESIZE( trun->size );
    return trun->size;
}

static uint64_t isom_update_traf_entry_size( isom_traf_entry_t *traf )
{
    if( !traf )
        return 0;
    traf->size = ISOM_DEFAULT_BOX_HEADER_SIZE + isom_update_tfhd_size( traf->tfhd );
    if( traf->trun_list )
        for( lsmash_entry_t *entry = traf->trun_list->head; entry; entry = entry->next )
        {
            isom_trun_entry_t *trun = (isom_trun_entry_t *)entry->data;
            traf->size += isom_update_trun_entry_size( trun );
        }
    CHECK_LARGESIZE( traf->size );
    return traf->size;
}

static int isom_update_moof_entry_size( isom_moof_entry_t *moof )
{
    if( !moof )
        return -1;
    moof->size = ISOM_DEFAULT_BOX_HEADER_SIZE + isom_update_mfhd_size( moof->mfhd );
    if( moof->traf_list )
        for( lsmash_entry_t *entry = moof->traf_list->head; entry; entry = entry->next )
        {
            isom_traf_entry_t *traf = (isom_traf_entry_t *)entry->data;
            moof->size += isom_update_traf_entry_size( traf );
        }
    CHECK_LARGESIZE( moof->size );
    return 0;
}

static uint64_t isom_update_tfra_entry_size( isom_tfra_entry_t *tfra )
{
    if( !tfra )
        return 0;
    tfra->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 12;
    uint32_t entry_size = 8 * (1 + (tfra->version == 1))
                        + tfra->length_size_of_traf_num   + 1
                        + tfra->length_size_of_trun_num   + 1
                        + tfra->length_size_of_sample_num + 1;
    tfra->size += entry_size * tfra->number_of_entry;
    CHECK_LARGESIZE( tfra->size );
    return tfra->size;
}

static uint64_t isom_update_mfro_size( isom_mfro_t *mfro )
{
    if( !mfro )
        return 0;
    mfro->size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 4;
    CHECK_LARGESIZE( mfro->size );
    return mfro->size;
}

static int isom_update_mfra_size( isom_mfra_t *mfra )
{
    if( !mfra )
        return -1;
    mfra->size = ISOM_DEFAULT_BOX_HEADER_SIZE;
    if( mfra->tfra_list )
        for( lsmash_entry_t *entry = mfra->tfra_list->head; entry; entry = entry->next )
        {
            isom_tfra_entry_t *tfra = (isom_tfra_entry_t *)entry->data;
            mfra->size += isom_update_tfra_entry_size( tfra );
        }
    CHECK_LARGESIZE( mfra->size );
    if( mfra->mfro )
    {
        mfra->size += isom_update_mfro_size( mfra->mfro );
        mfra->mfro->length = mfra->size;
    }
    return 0;
}

/*******************************
    public interfaces
*******************************/

/*---- track manipulators ----*/

void lsmash_delete_track( lsmash_root_t *root, uint32_t track_ID )
{
    if( !root || !root->moov || !root->moov->trak_list )
        return;
    for( lsmash_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
        if( !trak || !trak->tkhd )
            return;
        if( trak->tkhd->track_ID == track_ID )
        {
            lsmash_entry_t *next = entry->next;
            lsmash_entry_t *prev = entry->prev;
            isom_remove_trak( trak );
            free( entry );
            entry = next;
            if( entry )
            {
                if( prev )
                    prev->next = entry;
                entry->prev = prev;
            }
            return;
        }
    }
}

uint32_t lsmash_create_track( lsmash_root_t *root, uint32_t media_type )
{
    isom_trak_entry_t *trak = isom_add_trak( root );
    if( !trak )
        return 0;
    if( isom_add_tkhd( trak, media_type )
     || isom_add_mdia( trak )
     || isom_add_mdhd( trak->mdia, root->qt_compatible ? 0 : ISOM_LANGUAGE_CODE_UNDEFINED )
     || isom_add_minf( trak->mdia )
     || isom_add_stbl( trak->mdia->minf )
     || isom_add_dinf( trak->mdia->minf )
     || isom_add_dref( trak->mdia->minf->dinf )
     || isom_add_stsd( trak->mdia->minf->stbl )
     || isom_add_stts( trak->mdia->minf->stbl )
     || isom_add_stsc( trak->mdia->minf->stbl )
     || isom_add_stco( trak->mdia->minf->stbl )
     || isom_add_stsz( trak->mdia->minf->stbl ) )
        return 0;
    if( isom_add_hdlr( trak->mdia, NULL, media_type ) )
        return 0;
    if( root->qt_compatible && isom_add_hdlr( NULL, trak->mdia->minf, QT_REFERENCE_HANDLER_TYPE_URL ) )
        return 0;
    switch( media_type )
    {
        case ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK :
            if( isom_add_vmhd( trak->mdia->minf ) )
                return 0;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK :
            if( isom_add_smhd( trak->mdia->minf ) )
                return 0;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_HINT_TRACK :
            if( isom_add_hmhd( trak->mdia->minf ) )
                return 0;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_TEXT_TRACK :
            if( root->qt_compatible || root->itunes_audio )
            {
                if( isom_add_gmhd( trak->mdia->minf )
                 || isom_add_gmin( trak->mdia->minf->gmhd )
                 || isom_add_text( trak->mdia->minf->gmhd ) )
                    return 0;
            }
            else
                return 0;   /* We support only reference text media track for chapter yet. */
            break;
        default :
            if( isom_add_nmhd( trak->mdia->minf ) )
                return 0;
            break;
    }
    return trak->tkhd->track_ID;
}

void lsmash_initialize_track_parameters( lsmash_track_parameters_t *param )
{
    param->mode            = 0;
    param->track_ID        = 0;
    param->duration        = 0;
    param->video_layer     = 0;
    param->alternate_group = 0;
    param->audio_volume    = 0x0100;
    param->display_width   = 0;
    param->display_height  = 0;
}

int lsmash_set_track_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_track_parameters_t *param )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->hdlr || !root->moov->mvhd )
        return -1;
    uint32_t media_type = trak->mdia->hdlr->componentSubtype;
    isom_tkhd_t *tkhd = trak->tkhd;
    tkhd->flags           = param->mode;
    tkhd->track_ID        = param->track_ID ? param->track_ID : tkhd->track_ID;
    tkhd->duration        = !trak->edts || !trak->edts->elst ? param->duration : tkhd->duration;
    tkhd->alternate_group = root->qt_compatible || root->itunes_audio || root->max_3gpp_version >= 4 ? param->alternate_group : 0;
    if( root->qt_compatible || root->itunes_audio )
    {
        tkhd->layer       = media_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK ? param->video_layer    : 0;
        tkhd->volume      = media_type == ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK ? param->audio_volume   : 0;
    }
    else
    {
        tkhd->layer       = 0;
        tkhd->volume      = media_type == ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK ? 0x0100                : 0;
    }
    tkhd->width           = media_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK ? param->display_width  : 0;
    tkhd->height          = media_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK ? param->display_height : 0;
    /* Update next_track_ID if needed. */
    if( root->moov->mvhd->next_track_ID <= tkhd->track_ID )
        root->moov->mvhd->next_track_ID = tkhd->track_ID + 1;
    return 0;
}

int lsmash_get_track_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_track_parameters_t *param )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak )
        return -1;
    isom_tkhd_t *tkhd = trak->tkhd;
    param->mode            = tkhd->flags;
    param->track_ID        = tkhd->track_ID;
    param->duration        = tkhd->duration;
    param->video_layer     = tkhd->layer;
    param->alternate_group = tkhd->alternate_group;
    param->audio_volume    = tkhd->volume;
    param->display_width   = tkhd->width;
    param->display_height  = tkhd->height;
    return 0;
}

static int isom_set_media_handler_name( lsmash_root_t *root, uint32_t track_ID, char *handler_name )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->hdlr )
        return -1;
    isom_hdlr_t *hdlr = trak->mdia->hdlr;
    uint8_t *name = NULL;
    uint32_t name_length = strlen( handler_name ) + root->isom_compatible + root->qt_compatible;
    if( root->qt_compatible )
        name_length = LSMASH_MIN( name_length, 255 );
    if( name_length > hdlr->componentName_length && hdlr->componentName )
        name = realloc( hdlr->componentName, name_length );
    else if( !hdlr->componentName )
        name = malloc( name_length );
    else
        name = hdlr->componentName;
    if( !name )
        return -1;
    if( root->qt_compatible )
        name[0] = name_length & 0xff;
    memcpy( name + root->qt_compatible, handler_name, strlen( handler_name ) );
    if( root->isom_compatible )
        name[name_length - 1] = 0;
    hdlr->componentName = name;
    hdlr->componentName_length = name_length;
    return 0;
}

static int isom_set_data_handler_name( lsmash_root_t *root, uint32_t track_ID, char *handler_name )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->hdlr )
        return -1;
    isom_hdlr_t *hdlr = trak->mdia->minf->hdlr;
    uint8_t *name = NULL;
    uint32_t name_length = strlen( handler_name ) + root->isom_compatible + root->qt_compatible;
    if( root->qt_compatible )
        name_length = LSMASH_MIN( name_length, 255 );
    if( name_length > hdlr->componentName_length && hdlr->componentName )
        name = realloc( hdlr->componentName, name_length );
    else if( !hdlr->componentName )
        name = malloc( name_length );
    else
        name = hdlr->componentName;
    if( !name )
        return -1;
    if( root->qt_compatible )
        name[0] = name_length & 0xff;
    memcpy( name + root->qt_compatible, handler_name, strlen( handler_name ) );
    if( root->isom_compatible )
        name[name_length - 1] = 0;
    hdlr->componentName = name;
    hdlr->componentName_length = name_length;
    return 0;
}

uint32_t lsmash_get_media_timescale( lsmash_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return 0;
    return trak->mdia->mdhd->timescale;
}

uint64_t lsmash_get_media_duration( lsmash_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return 0;
    return trak->mdia->mdhd->duration;
}

uint64_t lsmash_get_track_duration( lsmash_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->tkhd )
        return 0;
    return trak->tkhd->duration;
}

uint32_t lsmash_get_last_sample_delta( lsmash_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl
     || !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list
     || !trak->mdia->minf->stbl->stts->list->head || !trak->mdia->minf->stbl->stts->list->head->data )
        return 0;
    return ((isom_stts_entry_t *)trak->mdia->minf->stbl->stts->list->head->data)->sample_delta;
}

uint32_t lsmash_get_start_time_offset( lsmash_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl
     || !trak->mdia->minf->stbl->ctts || !trak->mdia->minf->stbl->ctts->list
     || !trak->mdia->minf->stbl->ctts->list->head || !trak->mdia->minf->stbl->ctts->list->head->data )
        return 0;
    return ((isom_ctts_entry_t *)trak->mdia->minf->stbl->ctts->list->head->data)->sample_offset;
}

static int isom_iso2mac_language( uint16_t ISO_language, uint16_t *MAC_language )
{
    if( !MAC_language )
        return -1;
    int i = 0;
    for( ; isom_languages[i].iso_name; i++ )
        if( ISO_language == isom_languages[i].iso_name )
            break;
    if( !isom_languages[i].iso_name )
        return -1;
    *MAC_language = isom_languages[i].mac_value;
    return 0;
}

static int isom_mac2iso_language( uint16_t MAC_language, uint16_t *ISO_language )
{
    if( !ISO_language )
        return -1;
    int i = 0;
    for( ; isom_languages[i].iso_name; i++ )
        if( MAC_language == isom_languages[i].mac_value )
            break;
    *ISO_language = isom_languages[i].iso_name ? isom_languages[i].iso_name : ISOM_LANGUAGE_CODE_UNDEFINED;
    return 0;
}

static int isom_set_media_language( lsmash_root_t *root, uint32_t track_ID, char *ISO_language, uint16_t MAC_language )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return -1;
    uint16_t language = 0;
    if( root->isom_compatible )
    {
        if( ISO_language && (strlen( ISO_language ) == 3) )
            language = ISOM_LANG( ISO_language );
        else if( MAC_language )
        {
            if( isom_mac2iso_language( MAC_language, &language ) )
                return -1;
        }
        else
            language = ISOM_LANGUAGE_CODE_UNDEFINED;
    }
    else if( root->qt_compatible )
    {
        if( ISO_language && (strlen( ISO_language ) == 3) )
        {
            if( isom_iso2mac_language( ISOM_LANG( ISO_language ), &language ) )
                return -1;
        }
        else
            language = MAC_language;
    }
    else
        return -1;
    trak->mdia->mdhd->language = language;
    return 0;
}

void lsmash_initialize_media_parameters( lsmash_media_parameters_t *param )
{
    memset( param, 0, sizeof(lsmash_media_parameters_t) );
    param->timescale = 1;
}

int lsmash_set_media_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_media_parameters_t *param )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return -1;
    trak->mdia->mdhd->timescale = param->timescale;
    if( isom_set_media_language( root, track_ID, param->ISO_language, param->MAC_language ) )
        return -1;
    if( param->media_handler_name && isom_set_media_handler_name( root, track_ID, param->media_handler_name ) )
        return -1;
    if( root->qt_compatible && param->data_handler_name && isom_set_data_handler_name( root, track_ID, param->data_handler_name ) )
        return -1;
    return 0;
}

int lsmash_get_media_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_media_parameters_t *param )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->hdlr || !trak->mdia->minf )
        return -1;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    param->timescale = mdhd->timescale;
    param->duration  = mdhd->duration;
    /* Get media language. */
    if( mdhd->language >= 0x800 )
    {
        param->MAC_language = 0;
        param->ISO_language = isom_unpack_iso_language( mdhd->language );
        memcpy( param->language_shadow, param->ISO_language, sizeof(param->language_shadow) );
        param->ISO_language = param->language_shadow;
    }
    else
    {
        param->MAC_language = mdhd->language;
        param->ISO_language = NULL;
        memset( param->language_shadow, 0, sizeof(param->language_shadow) );
    }
    /* Get handler name(s). */
    isom_hdlr_t *hdlr = trak->mdia->hdlr;
    int length = LSMASH_MIN( 255, hdlr->componentName_length );
    if( length )
    {
        memcpy( param->media_handler_name_shadow, hdlr->componentName + root->qt_compatible, length );
        param->media_handler_name_shadow[length - 2 + root->isom_compatible + root->qt_compatible] = '\0';
        param->media_handler_name = param->media_handler_name_shadow;
    }
    else
    {
        param->media_handler_name = NULL;
        memset( param->media_handler_name_shadow, 0, sizeof(param->media_handler_name_shadow) );
    }
    if( trak->mdia->minf->hdlr )
    {
        hdlr = trak->mdia->minf->hdlr;
        length = LSMASH_MIN( 255, hdlr->componentName_length );
        if( length )
        {
            memcpy( param->data_handler_name_shadow, hdlr->componentName + root->qt_compatible, length );
            param->data_handler_name_shadow[length - 2 + root->isom_compatible + root->qt_compatible] = '\0';
            param->data_handler_name = param->data_handler_name_shadow;
        }
        else
        {
            param->data_handler_name = NULL;
            memset( param->data_handler_name_shadow, 0, sizeof(param->data_handler_name_shadow) );
        }
    }
    else
    {
        param->data_handler_name = NULL;
        memset( param->data_handler_name_shadow, 0, sizeof(param->data_handler_name_shadow) );
    }
    return 0;
}

static int isom_confirm_visual_type( uint32_t sample_type )
{
    switch( sample_type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
#if 0
        case ISOM_CODEC_TYPE_AVC2_VIDEO :
        case ISOM_CODEC_TYPE_AVCP_VIDEO :
        case ISOM_CODEC_TYPE_SVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC2_VIDEO :
        case ISOM_CODEC_TYPE_MP4V_VIDEO :
        case ISOM_CODEC_TYPE_DRAC_VIDEO :
        case ISOM_CODEC_TYPE_ENCV_VIDEO :
        case ISOM_CODEC_TYPE_MJP2_VIDEO :
        case ISOM_CODEC_TYPE_S263_VIDEO :
        case ISOM_CODEC_TYPE_VC_1_VIDEO :
#endif
            break;
        default :
            return -1;
    }
    return 0;
}

int lsmash_set_track_aperture_modes( lsmash_root_t *root, uint32_t track_ID, uint32_t entry_number )
{
    if( !root->qt_compatible )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_visual_entry_t *data = (isom_visual_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data || isom_confirm_visual_type( data->type ) )
        return -1;
    uint32_t width = data->width << 16;
    uint32_t height = data->height << 16;
    if( !trak->tapt && isom_add_tapt( trak ) )
        return -1;
    isom_tapt_t *tapt = trak->tapt;
    if( (!tapt->clef && isom_add_clef( tapt ))
     || (!tapt->prof && isom_add_prof( tapt ))
     || (!tapt->enof && isom_add_enof( tapt )) )
        return -1;
    if( !data->pasp && isom_add_pasp( data ) )
        return -1;
    isom_pasp_t *pasp = data->pasp;
    isom_clap_t *clap = data->clap;
    if( !clap )
    {
        if( isom_add_clap( data ) )
            return -1;
        clap = data->clap;
        clap->cleanApertureWidthN = data->width;
        clap->cleanApertureHeightN = data->height;
    }
    if( !pasp->hSpacing || !pasp->vSpacing
     || !clap->cleanApertureWidthN || !clap->cleanApertureWidthD
     || !clap->cleanApertureHeightN || !clap->cleanApertureHeightD
     || !clap->horizOffD || !clap->vertOffD )
        return -1;
    double par = (double)pasp->hSpacing / pasp->vSpacing;
    double clap_width = ((double)clap->cleanApertureWidthN / clap->cleanApertureWidthD) * (1<<16);
    double clap_height = ((double)clap->cleanApertureHeightN / clap->cleanApertureHeightD) * (1<<16);
    if( par >= 1.0 )
    {
        tapt->clef->width = clap_width * par;
        tapt->clef->height = clap_height;
        tapt->prof->width = width * par;
        tapt->prof->height = height;
    }
    else
    {
        tapt->clef->width = clap_width;
        tapt->clef->height = clap_height / par;
        tapt->prof->width = width;
        tapt->prof->height = height / par;
    }
    tapt->enof->width = width;
    tapt->enof->height = height;
    return 0;
}

int lsmash_create_grouping( lsmash_root_t *root, uint32_t track_ID, lsmash_grouping_type_code grouping_type )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    switch( grouping_type )
    {
        case ISOM_GROUP_TYPE_RAP :
            if( root->max_isom_version < 6 )
                return -1;
            break;
        case ISOM_GROUP_TYPE_ROLL :
            if( !root->avc_extensions )
                return -1;
            break;
        default :
            break;
    }
    if( !isom_add_sgpd( trak->mdia->minf->stbl, grouping_type )
     || !isom_add_sbgp( trak->mdia->minf->stbl, grouping_type ) )
        return -1;
    return 0;
}

/*---- movie manipulators ----*/

lsmash_root_t *lsmash_open_movie( const char *filename, lsmash_file_mode_code mode )
{
    char open_mode[4] = { 0 };
    if( mode & LSMASH_FILE_MODE_WRITE )
        memcpy( open_mode, "w+b", 4 );
    else if( mode & LSMASH_FILE_MODE_READ )
        memcpy( open_mode, "rb", 3 );
    if( !open_mode[0] )
        return NULL;
    lsmash_root_t *root = malloc( sizeof(lsmash_root_t) );
    if( !root )
        return NULL;
    memset( root, 0, sizeof(lsmash_root_t) );
    root->root = root;
    root->bs = malloc( sizeof(lsmash_bs_t) );
    if( !root->bs )
        goto fail;
    memset( root->bs, 0, sizeof(lsmash_bs_t) );
    if( !strcmp( filename, "-" ) )
    {
        if( mode & LSMASH_FILE_MODE_READ )
            root->bs->stream = stdin;
        else if( (mode & LSMASH_FILE_MODE_WRITE) && (mode & LSMASH_FILE_MODE_FRAGMENTED) )
            root->bs->stream = stdout;
    }
    else
        root->bs->stream = fopen( filename, open_mode );
    if( !root->bs->stream )
        goto fail;
    root->flags = mode;
    if( (mode & LSMASH_FILE_MODE_WRITE) && (isom_add_moov( root ) || isom_add_mvhd( root->moov )) )
        goto fail;
    if( (mode & (LSMASH_FILE_MODE_READ | LSMASH_FILE_MODE_DUMP)) && isom_read_root( root ) )
        goto fail;
    if( mode & LSMASH_FILE_MODE_FRAGMENTED )
    {
        root->fragment = malloc( sizeof(isom_fragment_manager_t) );
        if( !root->fragment )
            goto fail;
        memset( root->fragment, 0, sizeof(isom_fragment_manager_t) );
        root->fragment->pool = lsmash_create_entry_list();
        if( !root->fragment->pool )
            goto fail;
    }
    return root;
fail:
    lsmash_destroy_root( root );
    return NULL;
}

static int isom_finish_fragment_movie( lsmash_root_t *root );

/* A movie fragment cannot switch a sample description to another.
 * So you must call this function before switching sample descriptions. */
int lsmash_create_fragment_movie( lsmash_root_t *root )
{
    if( !root || !root->bs || !root->fragment || !root->moov || !root->moov->trak_list )
        return -1;
    /* Finish the previous movie fragment before starting a new one. */
    if( isom_finish_fragment_movie( root ) )
        return -1;
    /* We always hold only one movie fragment except for the initial movie (a pair of moov and mdat). */
    if( root->fragment->movie && root->moof_list->entry_count != 1 )
        return -1;
    isom_moof_entry_t *moof = isom_add_moof( root );
    if( isom_add_mfhd( moof ) )
        return -1;
    root->fragment->movie = moof;
    moof->mfhd->sequence_number = ++ root->fragment->fragment_count;
    if( root->moof_list->entry_count == 1 )
        return 0;
    /* Remove the previous movie fragment. */
    return lsmash_remove_entry( root->moof_list, 1, isom_remove_moof );
}

void lsmash_initialize_movie_parameters( lsmash_movie_parameters_t *param )
{
    param->max_chunk_duration  = 0.5;
    param->max_async_tolerance = 2.0;
    param->timescale           = 600;
    param->duration            = 0;
    param->playback_rate       = 0x00010000;
    param->playback_volume     = 0x0100;
    param->preview_time        = 0;
    param->preview_duration    = 0;
    param->poster_time         = 0;
}

int lsmash_set_movie_parameters( lsmash_root_t *root, lsmash_movie_parameters_t *param )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return -1;
    isom_mvhd_t *mvhd = root->moov->mvhd;
    root->max_chunk_duration  = param->max_chunk_duration;
    root->max_async_tolerance = LSMASH_MAX( param->max_async_tolerance, 2 * param->max_chunk_duration );
    mvhd->timescale           = param->timescale;
    if( root->qt_compatible || root->itunes_audio )
    {
        mvhd->rate            = param->playback_rate;
        mvhd->volume          = param->playback_volume;
        mvhd->previewTime     = param->preview_time;
        mvhd->previewDuration = param->preview_duration;
        mvhd->posterTime      = param->poster_time;
    }
    else
    {
        mvhd->rate            = 0x00010000;
        mvhd->volume          = 0x0100;
        mvhd->previewTime     = 0;
        mvhd->previewDuration = 0;
        mvhd->posterTime      = 0;
    }
    return 0;
}

int lsmash_get_movie_parameters( lsmash_root_t *root, lsmash_movie_parameters_t *param )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return -1;
    isom_mvhd_t *mvhd = root->moov->mvhd;
    param->max_chunk_duration  = root->max_chunk_duration;
    param->max_async_tolerance = root->max_async_tolerance;
    param->timescale           = mvhd->timescale;
    param->duration            = mvhd->duration;
    param->playback_rate       = mvhd->rate;
    param->playback_volume     = mvhd->volume;
    param->preview_time        = mvhd->previewTime;
    param->preview_duration    = mvhd->previewDuration;
    param->poster_time         = mvhd->posterTime;
    return 0;
}

uint32_t lsmash_get_movie_timescale( lsmash_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return 0;
    return root->moov->mvhd->timescale;
}

int lsmash_set_brands( lsmash_root_t *root, lsmash_brand_type_code major_brand, uint32_t minor_version, lsmash_brand_type_code *brands, uint32_t brand_count )
{
    if( !root )
        return -1;
    if( !brand_count )
    {
        /* Absence of ftyp box means this file is a QuickTime or MP4 version 1 format file. */
        if( root->ftyp )
        {
            if( root->ftyp->compatible_brands )
                free( root->ftyp->compatible_brands );
            free( root->ftyp );
            root->ftyp = NULL;
        }
        return 0;
    }
    if( !root->ftyp && isom_add_ftyp( root ) )
        return -1;
    isom_ftyp_t *ftyp = root->ftyp;
    ftyp->major_brand = major_brand;
    ftyp->minor_version = minor_version;
    ftyp->compatible_brands = malloc( brand_count * sizeof(uint32_t) );
    if( !ftyp->compatible_brands )
        return -1;
    for( uint32_t i = 0; i < brand_count; i++ )
    {
        ftyp->compatible_brands[i] = brands[i];
        ftyp->size += 4;
    }
    ftyp->brand_count = brand_count;
    return isom_check_compatibility( root );
}

int lsmash_write_ftyp( lsmash_root_t *root )
{
    if( !root )
        return -1;
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
    return 0;
}

static int isom_write_moov( lsmash_root_t *root )
{
    if( !root || !root->moov )
        return -1;
    lsmash_bs_t *bs = root->bs;
    isom_bs_put_box_common( bs, root->moov );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_mvhd( root )
     || isom_write_iods( root ) )
        return -1;
    if( root->moov->trak_list )
        for( lsmash_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
            if( isom_write_trak( bs, (isom_trak_entry_t *)entry->data ) )
                return -1;
    if( isom_write_udta( bs, root->moov, NULL ) )
        return -1;
    return isom_write_mvex( bs, root->moov->mvex );
}

int lsmash_set_free( lsmash_root_t *root, uint8_t *data, uint64_t data_length )
{
    if( !root || !root->free || !data || !data_length )
        return -1;
    isom_free_t *skip = root->free;
    uint8_t *tmp = NULL;
    if( !skip->data )
        tmp = malloc( data_length );
    else if( skip->length < data_length )
        tmp = realloc( skip->data, data_length );
    if( !tmp )
        return -1;
    memcpy( tmp, data, data_length );
    skip->data = tmp;
    skip->length = data_length;
    return 0;
}

int lsmash_add_free( lsmash_root_t *root, uint8_t *data, uint64_t data_length )
{
    if( !root )
        return -1;
    if( !root->free )
    {
        isom_create_box( skip, root, ISOM_BOX_TYPE_FREE );
        root->free = skip;
    }
    if( data && data_length )
        return lsmash_set_free( root, data, data_length );
    return 0;
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
        lsmash_bs_put_bytes( bs, skip->data, skip->length );
    return lsmash_bs_write_data( bs );
}

int lsmash_create_object_descriptor( lsmash_root_t *root )
{
    if( !root )
        return -1;
    /* Return error if this file is not compatible with MP4 file format. */
    if( !root->mp4_version1 && !root->mp4_version2 )
        return -1;
    return isom_add_iods( root->moov );
}

/*---- finishing functions ----*/

static int isom_set_fragment_overall_duration( lsmash_root_t *root )
{
    if( root->bs->stream == stdout )
        return 0;
    isom_mvex_t *mvex = root->moov->mvex;
    if( isom_add_mehd( mvex ) )
        return -1;
    /* Get the longest duration of the tracks. */
    uint64_t longest_duration = 0;
    for( lsmash_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
        if( !trak || !trak->cache || !trak->cache->fragment || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->mdhd->timescale )
            return -1;
        uint64_t duration;
        if( !trak->edts || !trak->edts->elst || !trak->edts->elst->list )
        {
            duration = trak->cache->fragment->largest_cts + trak->cache->fragment->last_duration;
            duration = (uint64_t)(((double)duration / trak->mdia->mdhd->timescale) * root->moov->mvhd->timescale);
        }
        else
        {
            duration = 0;
            for( lsmash_entry_t *elst_entry = trak->edts->elst->list->head; elst_entry; elst_entry = elst_entry->next )
            {
                isom_elst_entry_t *data = (isom_elst_entry_t *)elst_entry->data;
                if( !data )
                    return -1;
                duration += data->segment_duration;
            }
        }
        longest_duration = LSMASH_MAX( duration, longest_duration );
    }
    mvex->mehd->fragment_duration = longest_duration;
    mvex->mehd->version = 1;
    isom_update_mehd_size( mvex->mehd );
    /* Write Movie Extends Header Box here. */
    lsmash_bs_t *bs = root->bs;
    FILE *stream = bs->stream;
    uint64_t current_pos = lsmash_ftell( stream );
    lsmash_fseek( stream, mvex->placeholder_pos, SEEK_SET );
    int ret = isom_write_mehd( bs, mvex->mehd );
    if( !ret )
        ret = lsmash_bs_write_data( bs );
    lsmash_fseek( stream, current_pos, SEEK_SET );
    return ret;
}

static int isom_write_fragment_random_access_info( lsmash_root_t *root )
{
    if( root->bs->stream == stdout )
        return 0;
    if( isom_update_mfra_size( root->mfra ) )
        return -1;
    return isom_write_mfra( root->bs, root->mfra );
}

int lsmash_finish_movie( lsmash_root_t *root, lsmash_adhoc_remux_t* remux )
{
    if( !root || !root->bs || !root->moov || !root->moov->trak_list )
        return -1;
    if( root->fragment )
    {
        /* Output the final movie fragment. */
        if( isom_finish_fragment_movie( root ) )
            return -1;
        /* Write the overall random access information at the tail of the movie. */
        if( isom_write_fragment_random_access_info( root ) )
            return -1;
        /* Set overall duration of the movie. */
        return isom_set_fragment_overall_duration( root );
    }
    isom_moov_t *moov = root->moov;
    for( lsmash_entry_t *entry = moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
        if( !trak || !trak->cache || !trak->tkhd || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
            return -1;
        uint32_t track_ID = trak->tkhd->track_ID;
        uint32_t related_track_ID = trak->related_track_ID;
        /* Disable the track if the track is a track reference chapter. */
        if( trak->is_chapter )
            trak->tkhd->flags &= ~ISOM_TRACK_ENABLED;
        if( trak->is_chapter && related_track_ID )
        {
            /* In order that the track duration of the chapter track doesn't exceed that of the related track. */
            uint64_t track_duration = LSMASH_MIN( trak->tkhd->duration, lsmash_get_track_duration( root, related_track_ID ) );
            if( lsmash_create_explicit_timeline_map( root, track_ID, track_duration, 0, ISOM_EDIT_MODE_NORMAL ) )
                return -1;
        }
        /* Add stss box if any samples aren't sync sample. */
        isom_stbl_t *stbl = trak->mdia->minf->stbl;
        if( !trak->cache->all_sync && !stbl->stss && isom_add_stss( stbl ) )
            return -1;
        if( isom_update_bitrate_info( trak->mdia ) )
            return -1;
    }
    if( root->mp4_version1 == 1 && isom_add_iods( moov ) )
        return -1;
    if( isom_check_mandatory_boxes( root )
     || isom_set_movie_creation_time( root )
     || isom_update_moov_size( moov )
     || isom_write_mdat_size( root ) )
        return -1;

    if( !remux )
    {
        if( isom_write_moov( root ) )
            return -1;
        root->size += moov->size;
        return 0;
    }

    /* stco->co64 conversion, depending on last chunk's offset */
    for( lsmash_entry_t* entry = moov->trak_list->head; entry; )
    {
        isom_trak_entry_t* trak = (isom_trak_entry_t*)entry->data;
        isom_stco_t* stco = trak->mdia->minf->stbl->stco;
        if( !stco->list->tail )
            return -1;
        if( stco->large_presentation
            || ((isom_stco_entry_t*)stco->list->tail->data)->chunk_offset + moov->size <= UINT32_MAX )
        {
            entry = entry->next;
            continue;   /* no need to convert stco into co64 */
        }
        /* stco->co64 conversion */
        if( isom_convert_stco_to_co64( trak->mdia->minf->stbl )
            || isom_update_moov_size( moov ) )
            return -1;
        entry = moov->trak_list->head; /* whenever any conversion, re-check all traks */
    }

    /* now the amount of offset is fixed. */

    /* buffer size must be at least sizeof(moov)*2 */
    remux->buffer_size = LSMASH_MAX( remux->buffer_size, moov->size * 2 );

    uint8_t* buf[2];
    if( (buf[0] = (uint8_t*)malloc( remux->buffer_size )) == NULL )
        return -1; /* NOTE: i think we still can fallback to "return isom_write_moov( root );" here. */
    uint64_t size = remux->buffer_size / 2;
    buf[1] = buf[0] + size; /* split to 2 buffers */

    /* now the amount of offset is fixed. apply that to stco/co64 */
    for( lsmash_entry_t* entry = moov->trak_list->head; entry; entry = entry->next )
    {
        isom_stco_t* stco = ((isom_trak_entry_t*)entry->data)->mdia->minf->stbl->stco;
        if( stco->large_presentation )
            for( lsmash_entry_t* co64_entry = stco->list->head ; co64_entry ; co64_entry = co64_entry->next )
                ((isom_co64_entry_t*)co64_entry->data)->chunk_offset += moov->size;
        else
            for( lsmash_entry_t* stco_entry = stco->list->head ; stco_entry ; stco_entry = stco_entry->next )
                ((isom_stco_entry_t*)stco_entry->data)->chunk_offset += moov->size;
    }

    FILE *stream = root->bs->stream;
    isom_mdat_t *mdat = root->mdat;
    uint64_t total = lsmash_ftell( stream ) + moov->size; // FIXME:
    uint64_t readnum;
    /* backup starting area of mdat and write moov there instead */
    if( lsmash_fseek( stream, mdat->placeholder_pos, SEEK_SET ) )
        goto fail;
    readnum = fread( buf[0], 1, size, stream );
    uint64_t read_pos = lsmash_ftell( stream );

    /* write moov there instead */
    if( lsmash_fseek( stream, mdat->placeholder_pos, SEEK_SET )
        || isom_write_moov( root ) )
        goto fail;
    uint64_t write_pos = lsmash_ftell( stream );

    mdat->placeholder_pos += moov->size; /* update placeholder */

    /* copy-pastan */
    int buf_switch = 1;
    while( readnum == size )
    {
        if( lsmash_fseek( stream, read_pos, SEEK_SET ) )
            goto fail;
        readnum = fread( buf[buf_switch], 1, size, stream );
        read_pos = lsmash_ftell( stream );

        buf_switch ^= 0x1;

        if( lsmash_fseek( stream, write_pos, SEEK_SET )
            || fwrite( buf[buf_switch], 1, size, stream ) != size )
            goto fail;
        write_pos = lsmash_ftell( stream );
        if( remux->func ) remux->func( remux->param, write_pos, total ); // FIXME:
    }
    if( fwrite( buf[buf_switch^0x1], 1, readnum, stream ) != readnum )
        goto fail;
    if( remux->func ) remux->func( remux->param, total, total ); // FIXME:

    root->size += moov->size;
    free( buf[0] );
    return 0;

fail:
    free( buf[0] );
    return -1;
}

#define GET_MOST_USED( box_name, index, flag_name ) \
    if( most_used[index] < stats.flag_name[i] ) \
    { \
        most_used[index] = stats.flag_name[i]; \
        box_name->default_sample_flags.flag_name = i; \
    }

static int isom_create_fragment_overall_default_settings( lsmash_root_t *root )
{
    if( isom_add_mvex( root->moov ) )
        return -1;
    for( lsmash_entry_t *trak_entry = root->moov->trak_list->head; trak_entry; trak_entry = trak_entry->next )
    {
        isom_trak_entry_t *trak = (isom_trak_entry_t *)trak_entry->data;
        if( !trak || !trak->cache || !trak->tkhd || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
            return -1;
        isom_stbl_t *stbl = trak->mdia->minf->stbl;
        if( !stbl->stts || !stbl->stts->list || !stbl->stsz
         || (stbl->stts->list->tail && !stbl->stts->list->tail->data)
         || (stbl->stsz->list && stbl->stsz->list->head && !stbl->stsz->list->head->data) )
            return -1;
        isom_trex_entry_t *trex = isom_add_trex( root->moov->mvex );
        if( !trex )
            return -1;
        trex->track_ID = trak->tkhd->track_ID;
        /* Set up defaults. */
        trex->default_sample_description_index = trak->cache->chunk.sample_description_index ? trak->cache->chunk.sample_description_index : 1;
        trex->default_sample_duration = stbl->stts->list->tail ? ((isom_stts_entry_t *)stbl->stts->list->tail->data)->sample_delta : 1;
        trex->default_sample_size = !stbl->stsz->list
                                  ? stbl->stsz->sample_size : stbl->stsz->list->head
                                  ? ((isom_stsz_entry_t *)stbl->stsz->list->head->data)->entry_size : 0;
        if( stbl->sdtp && stbl->sdtp->list )
        {
            struct sample_flags_stats_t
            {
                uint32_t is_leading           [4];
                uint32_t sample_depends_on    [4];
                uint32_t sample_is_depended_on[4];
                uint32_t sample_has_redundancy[4];
            } stats = { { 0 }, { 0 }, { 0 }, { 0 } };
            for( lsmash_entry_t *sdtp_entry = stbl->sdtp->list->head; sdtp_entry; sdtp_entry = sdtp_entry->next )
            {
                isom_sdtp_entry_t *data = (isom_sdtp_entry_t *)sdtp_entry->data;
                if( !data )
                    return -1;
                ++ stats.is_leading           [ data->is_leading            ];
                ++ stats.sample_depends_on    [ data->sample_depends_on     ];
                ++ stats.sample_is_depended_on[ data->sample_is_depended_on ];
                ++ stats.sample_has_redundancy[ data->sample_has_redundancy ];
            }
            uint32_t most_used[4] = { 0, 0, 0, 0 };
            for( int i = 0; i < 4; i++ )
            {
                GET_MOST_USED( trex, 0, is_leading            );
                GET_MOST_USED( trex, 1, sample_depends_on     );
                GET_MOST_USED( trex, 2, sample_is_depended_on );
                GET_MOST_USED( trex, 3, sample_has_redundancy );
            }
        }
        trex->default_sample_flags.sample_is_difference_sample = !trak->cache->all_sync;
    }
    return 0;
}

static int isom_prepare_random_access_info( lsmash_root_t *root )
{
    if( root->bs->stream == stdout )
        return 0;
    if( isom_add_mfra( root )
     || isom_add_mfro( root->mfra ) )
        return -1;
    return 0;
}

static int isom_output_fragment_media_data( lsmash_root_t *root )
{
    isom_fragment_manager_t *fragment = root->fragment;
    if( !fragment->pool->entry_count )
    {
        /* no need to write media data */
        lsmash_remove_entries( fragment->pool, lsmash_delete_sample );
        fragment->pool_size = 0;
        return 0;
    }
    /* If there is no available Media Data Box to write samples, add and write a new one. */
    if( isom_new_mdat( root, fragment->pool_size ) )
        return -1;
    /* Write samples in the current movie fragment. */
    for( lsmash_entry_t* entry = fragment->pool->head; entry; entry = entry->next )
    {
        lsmash_sample_t *sample = (lsmash_sample_t *)entry->data;
        if( !sample || !sample->data )
            return -1;
        lsmash_bs_put_bytes( root->bs, sample->data, sample->length );
        if( lsmash_bs_write_data( root->bs ) )
            return -1;
    }
    lsmash_remove_entries( fragment->pool, lsmash_delete_sample );
    root->size += root->mdat->size;
    fragment->pool_size = 0;
    return 0;
}

static int isom_finish_fragment_initial_movie( lsmash_root_t *root )
{
    if( !root->moov || !root->moov->trak_list )
        return -1;
    isom_moov_t *moov = root->moov;
    for( lsmash_entry_t *entry = moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
        if( !trak || !trak->cache || !trak->tkhd || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->minf || !trak->mdia->minf->stbl )
            return -1;
        if( isom_get_sample_count( trak ) )
        {
            /* Add stss box if any samples aren't sync sample. */
            isom_stbl_t *stbl = trak->mdia->minf->stbl;
            if( !trak->cache->all_sync && !stbl->stss && isom_add_stss( stbl ) )
                return -1;
        }
        else
            trak->tkhd->duration = 0;
        if( isom_update_bitrate_info( trak->mdia ) )
            return -1;
    }
    if( root->mp4_version1 == 1 && isom_add_iods( moov ) )
        return -1;
    if( isom_create_fragment_overall_default_settings( root )
     || isom_prepare_random_access_info( root )
     || isom_check_mandatory_boxes( root )
     || isom_set_movie_creation_time( root )
     || isom_update_moov_size( moov ) )
        return -1;
    /* stco->co64 conversion, depending on last chunk's offset */
    for( lsmash_entry_t* entry = moov->trak_list->head; entry; )
    {
        isom_trak_entry_t* trak = (isom_trak_entry_t*)entry->data;
        isom_stco_t* stco = trak->mdia->minf->stbl->stco;
        if( !stco->list->tail   /* no samples */
         || stco->large_presentation
         || ((isom_stco_entry_t*)stco->list->tail->data)->chunk_offset + moov->size <= UINT32_MAX )
        {
            entry = entry->next;
            continue;   /* no need to convert stco into co64 */
        }
        /* stco->co64 conversion */
        if( isom_convert_stco_to_co64( trak->mdia->minf->stbl )
            || isom_update_moov_size( moov ) )
            return -1;
        entry = moov->trak_list->head;  /* whenever any conversion, re-check all traks */
    }
    /* Now, the amount of offset is fixed. Apply that to stco/co64. */
    for( lsmash_entry_t* entry = moov->trak_list->head; entry; entry = entry->next )
    {
        isom_stco_t* stco = ((isom_trak_entry_t*)entry->data)->mdia->minf->stbl->stco;
        if( stco->large_presentation )
            for( lsmash_entry_t* co64_entry = stco->list->head ; co64_entry ; co64_entry = co64_entry->next )
                ((isom_co64_entry_t*)co64_entry->data)->chunk_offset += moov->size;
        else
            for( lsmash_entry_t* stco_entry = stco->list->head ; stco_entry ; stco_entry = stco_entry->next )
                ((isom_stco_entry_t*)stco_entry->data)->chunk_offset += moov->size;
    }
    if( isom_write_moov( root ) )
        return -1;
    root->size += moov->size;
    /* Output samples. */
    return isom_output_fragment_media_data( root );
}

static int isom_finish_fragment_movie( lsmash_root_t *root )
{
    if( !root->moov || !root->moov->trak_list || !root->fragment || !root->fragment->pool )
        return -1;
    isom_moof_entry_t *moof = root->fragment->movie;
    if( !moof )
        return isom_finish_fragment_initial_movie( root );
    /* Calculate appropriate default_sample_flags of each Track Fragment Header Box.
     * And check whether that default_sample_flags is useful or not. */
    for( lsmash_entry_t *entry = moof->traf_list->head; entry; entry = entry->next )
    {
        isom_traf_entry_t *traf = (isom_traf_entry_t *)entry->data;
        if( !traf || !traf->tfhd || !traf->root || !traf->root->moov || !traf->root->moov->mvex )
            return -1;
        isom_tfhd_t *tfhd = traf->tfhd;
        isom_trex_entry_t *trex = isom_get_trex( root->moov->mvex, tfhd->track_ID );
        if( !trex )
            return -1;
        struct sample_flags_stats_t
        {
            uint32_t is_leading                 [4];
            uint32_t sample_depends_on          [4];
            uint32_t sample_is_depended_on      [4];
            uint32_t sample_has_redundancy      [4];
            uint32_t sample_is_difference_sample[2];
        } stats = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };
        for( lsmash_entry_t *trun_entry = traf->trun_list->head; trun_entry; trun_entry = trun_entry->next )
        {
            isom_trun_entry_t *trun = (isom_trun_entry_t *)trun_entry->data;
            if( !trun || !trun->sample_count )
                return -1;
            isom_sample_flags_t *sample_flags;
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT )
            {
                if( !trun->optional )
                    return -1;
                for( lsmash_entry_t *optional_entry = trun->optional->head; optional_entry; optional_entry = optional_entry->next )
                {
                    isom_trun_optional_row_t *row = (isom_trun_optional_row_t *)optional_entry->data;
                    if( !row )
                        return -1;
                    sample_flags = &row->sample_flags;
                    ++ stats.is_leading                 [ sample_flags->is_leading                  ];
                    ++ stats.sample_depends_on          [ sample_flags->sample_depends_on           ];
                    ++ stats.sample_is_depended_on      [ sample_flags->sample_is_depended_on       ];
                    ++ stats.sample_has_redundancy      [ sample_flags->sample_has_redundancy       ];
                    ++ stats.sample_is_difference_sample[ sample_flags->sample_is_difference_sample ];
                }
            }
            else
            {
                sample_flags = &tfhd->default_sample_flags;
                stats.is_leading                 [ sample_flags->is_leading                  ] += trun->sample_count;
                stats.sample_depends_on          [ sample_flags->sample_depends_on           ] += trun->sample_count;
                stats.sample_is_depended_on      [ sample_flags->sample_is_depended_on       ] += trun->sample_count;
                stats.sample_has_redundancy      [ sample_flags->sample_has_redundancy       ] += trun->sample_count;
                stats.sample_is_difference_sample[ sample_flags->sample_is_difference_sample ] += trun->sample_count;
            }
        }
        uint32_t most_used[5] = { 0, 0, 0, 0, 0 };
        for( int i = 0; i < 4; i++ )
        {
            GET_MOST_USED( tfhd, 0, is_leading            );
            GET_MOST_USED( tfhd, 1, sample_depends_on     );
            GET_MOST_USED( tfhd, 2, sample_is_depended_on );
            GET_MOST_USED( tfhd, 3, sample_has_redundancy );
            if( i < 2 )
                GET_MOST_USED( tfhd, 4, sample_is_difference_sample );
        }
        int useful_default_sample_duration = 0;
        int useful_default_sample_size = 0;
        for( lsmash_entry_t *trun_entry = traf->trun_list->head; trun_entry; trun_entry = trun_entry->next )
        {
            isom_trun_entry_t *trun = (isom_trun_entry_t *)trun_entry->data;
            if( !(trun->flags & ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT) )
                useful_default_sample_duration = 1;
            if( !(trun->flags & ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT) )
                useful_default_sample_size = 1;
            int useful_first_sample_flags = 1;
            int useful_default_sample_flags = 1;
            if( trun->sample_count == 1 )
            {
                if( !memcmp( &trun->first_sample_flags, &tfhd->default_sample_flags, sizeof(isom_sample_flags_t) ) )
                    useful_first_sample_flags = 0;
            }
            else if( trun->optional && trun->optional->head )
            {
                lsmash_entry_t *optional_entry = trun->optional->head->next;
                isom_trun_optional_row_t *row = (isom_trun_optional_row_t *)optional_entry->data;
                isom_sample_flags_t representative_sample_flags = row->sample_flags;
                if( !memcmp( &trun->first_sample_flags, &representative_sample_flags, sizeof(isom_sample_flags_t) ) )
                    useful_first_sample_flags = 0;
                if( memcmp( &tfhd->default_sample_flags, &representative_sample_flags, sizeof(isom_sample_flags_t) ) )
                {
                    useful_first_sample_flags = 0;
                    useful_default_sample_flags = 0;
                }
                if( useful_default_sample_flags )
                    for( optional_entry = optional_entry->next; optional_entry; optional_entry = optional_entry->next )
                    {
                        row = (isom_trun_optional_row_t *)optional_entry->data;
                        if( memcmp( &representative_sample_flags, &row->sample_flags, sizeof(isom_sample_flags_t) ) )
                        {
                            useful_first_sample_flags = 0;
                            useful_default_sample_flags = 0;
                            break;
                        }
                    }
            }
            if( useful_first_sample_flags )
            {
                assert( useful_default_sample_flags );
                trun->flags |= ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT;
            }
            if( useful_default_sample_flags )
            {
                tfhd->flags |= ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT;
                trun->flags &= ~ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT;
            }
            else
                trun->flags |= ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT;
        }
        if( useful_default_sample_duration && tfhd->default_sample_duration != trex->default_sample_duration )
            tfhd->flags |= ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT;
        else
            tfhd->default_sample_duration = trex->default_sample_duration;      /* This might be redundant, but is to be more natural. */
        if( useful_default_sample_size && tfhd->default_sample_size != trex->default_sample_size )
            tfhd->flags |= ISOM_TF_FLAGS_DEFAULT_SAMPLE_SIZE_PRESENT;
        else
            tfhd->default_sample_size = trex->default_sample_size;              /* This might be redundant, but is to be more natural. */
        if( !(tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT) )
            tfhd->default_sample_flags = trex->default_sample_flags;            /* This might be redundant, but is to be more natural. */
        else if( !memcmp( &tfhd->default_sample_flags, &trex->default_sample_flags, sizeof(isom_sample_flags_t) ) )
            tfhd->flags &= ~ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT;
    }
    /* When using for live streaming, setting explicit base_data_offset is not preferable.
     * However, it's OK because we haven't supported this yet.
     * Implicit base_data_offsets that originate in the first byte of each Movie Fragment Box will be implemented
     * by the feature of ISO Base Media File Format version 5 or later. 
     * Media Data Box starts immediately after Movie Fragment Box. */
    for( lsmash_entry_t *entry = moof->traf_list->head; entry; entry = entry->next )
    {
        isom_traf_entry_t *traf = (isom_traf_entry_t *)entry->data;
        traf->tfhd->flags |= ISOM_TF_FLAGS_BASE_DATA_OFFSET_PRESENT;
    }
    /* Consider the update of tf_flags here. */
    if( isom_update_moof_entry_size( moof ) )
        return -1;
    /* Now, we can calculate offsets in the current movie fragment, so do it. */
    for( lsmash_entry_t *entry = moof->traf_list->head; entry; entry = entry->next )
    {
        isom_traf_entry_t *traf = (isom_traf_entry_t *)entry->data;
        traf->tfhd->base_data_offset = root->size + moof->size + ISOM_DEFAULT_BOX_HEADER_SIZE;
    }
    if( isom_write_moof( root->bs, moof ) )
        return -1;
    root->size += moof->size;
    /* Output samples. */
    return isom_output_fragment_media_data( root );
}

#undef GET_MOST_USED

static isom_trun_optional_row_t *isom_request_trun_optional_row( isom_trun_entry_t *trun, isom_tfhd_t *tfhd, uint32_t sample_number )
{
    isom_trun_optional_row_t *row = NULL;
    if( !trun->optional )
    {
        trun->optional = lsmash_create_entry_list();
        if( !trun->optional )
            return NULL;
    }
    if( trun->optional->entry_count < sample_number )
    {
        while( trun->optional->entry_count < sample_number )
        {
            row = malloc( sizeof(isom_trun_optional_row_t) );
            if( !row )
                return NULL;
            /* Copy from default. */
            row->sample_duration                = tfhd->default_sample_duration;
            row->sample_size                    = tfhd->default_sample_size;
            row->sample_flags                   = tfhd->default_sample_flags;
            row->sample_composition_time_offset = 0;
            if( lsmash_add_entry( trun->optional, row ) )
            {
                free( row );
                return NULL;
            }
        }
        return row;
    }
    uint32_t i = 0;
    for( lsmash_entry_t *entry = trun->optional->head; entry; entry = entry->next )
    {
        row = (isom_trun_optional_row_t *)entry->data;
        if( !row )
            return NULL;
        if( ++i == sample_number )
            return row;
    }
    return NULL;
}

int lsmash_create_fragment_empty_duration( lsmash_root_t *root, uint32_t track_ID, uint32_t duration )
{
    if( !root || !root->fragment || !root->fragment->movie || !root->moov )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->tkhd )
        return -1;
    isom_trex_entry_t *trex = isom_get_trex( root->moov->mvex, track_ID );
    if( !trex )
        return -1;
    isom_moof_entry_t *moof = root->fragment->movie;
    isom_traf_entry_t *traf = isom_get_traf( moof, track_ID );
    if( traf )
        return -1;
    traf = isom_add_traf( root, moof );
    if( isom_add_tfhd( traf ) )
        return -1;
    isom_tfhd_t *tfhd = traf->tfhd;
    tfhd->flags = ISOM_TF_FLAGS_DURATION_IS_EMPTY;          /* no samples for this track fragment yet */
    tfhd->track_ID = trak->tkhd->track_ID;
    tfhd->default_sample_duration = duration;
    if( duration != trex->default_sample_duration )
        tfhd->flags |= ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT;
    traf->cache = trak->cache;
    traf->cache->fragment->traf_number = moof->traf_list->entry_count;
    traf->cache->fragment->last_duration += duration;       /* The duration of the last sample includes this empty-duration. */
    return 0;
}

static int isom_set_fragment_last_duration( isom_traf_entry_t *traf, uint32_t last_duration )
{
    isom_tfhd_t *tfhd = traf->tfhd;
    if( !traf->trun_list || !traf->trun_list->tail || !traf->trun_list->tail->data )
    {
        /* There are no track runs in this track fragment, so it is a empty-duration. */
        isom_trex_entry_t *trex = isom_get_trex( traf->root->moov->mvex, tfhd->track_ID );
        if( !trex )
            return -1;
        tfhd->flags |= ISOM_TF_FLAGS_DURATION_IS_EMPTY;
        if( last_duration != trex->default_sample_duration )
            tfhd->flags |= ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT;
        tfhd->default_sample_duration = last_duration;
        traf->cache->fragment->last_duration = last_duration;
        return 0;
    }
    isom_trun_entry_t *trun = (isom_trun_entry_t *)traf->trun_list->tail->data;
    /* Update the last sample_duration if needed. */
    if( last_duration != tfhd->default_sample_duration )
        trun->flags |= ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT;
    if( trun->flags )
    {
        isom_trun_optional_row_t *row = isom_request_trun_optional_row( trun, tfhd, trun->sample_count );
        if( !row )
            return -1;
        row->sample_duration = last_duration;
    }
    traf->cache->fragment->last_duration = last_duration;
    return 0;
}

int lsmash_set_last_sample_delta( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_delta )
{
    if( !root || !track_ID )
        return -1;
    if( root->fragment && root->fragment->movie )
    {
        isom_traf_entry_t *traf = isom_get_traf( root->fragment->movie, track_ID );
        if( !traf || !traf->cache || !traf->tfhd || !traf->trun_list )
            return -1;
        return isom_set_fragment_last_duration( traf, sample_delta );
    }
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->minf || !trak->mdia->minf->stbl
     || !trak->mdia->minf->stbl->stsz || !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list )
        return -1;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_stts_t *stts = stbl->stts;
    uint32_t sample_count = isom_get_sample_count( trak );
    if( !stts->list->tail )
    {
        if( !sample_count )
            return 0;       /* no samples */
        if( sample_count > 1 )
            return -1;      /* irregular sample_count */
        if( isom_add_stts_entry( stbl, sample_delta ) )
            return -1;
        return lsmash_update_track_duration( root, track_ID, 0 );
    }
    uint32_t i = 0;
    for( lsmash_entry_t *entry = stts->list->head; entry; entry = entry->next )
        i += ((isom_stts_entry_t *)entry->data)->sample_count;
    if( sample_count < i )
        return -1;
    isom_stts_entry_t *last_stts_data = (isom_stts_entry_t *)stts->list->tail->data;
    if( !last_stts_data )
        return -1;
    if( sample_count > i )
    {
        if( sample_count - i > 1 )
            return -1;
        /* Add a sample_delta. */
        if( sample_delta == last_stts_data->sample_delta )
            ++ last_stts_data->sample_count;
        else if( isom_add_stts_entry( stbl, sample_delta ) )
            return -1;
    }
    else if( sample_count == i && isom_replace_last_sample_delta( stbl, sample_delta ) )
        return -1;
    return lsmash_update_track_duration( root, track_ID, sample_delta );
}

void lsmash_destroy_root( lsmash_root_t *root )
{
    if( !root )
        return;
    isom_remove_print_funcs( root );
    isom_remove_ftyp( root->ftyp );
    isom_remove_moov( root );
    lsmash_remove_list( root->moof_list, isom_remove_moof );
    isom_remove_mdat( root->mdat );
    isom_remove_free( root->free );
    isom_remove_mfra( root->mfra );
    if( root->bs )
    {
        if( root->bs->stream )
            fclose( root->bs->stream );
        if( root->bs->data )
            free( root->bs->data );
        free( root->bs );
    }
    if( root->fragment )
    {
        lsmash_remove_list( root->fragment->pool, lsmash_delete_sample );
        free( root->fragment );
    }
    free( root );
}

/*---- timeline manipulator ----*/

int lsmash_modify_timeline_map( lsmash_root_t *root, uint32_t track_ID, uint32_t entry_number, uint64_t segment_duration, int64_t media_time, int32_t media_rate )
{
    if( !segment_duration || media_time < -1 )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->edts || !trak->edts->elst || !trak->edts->elst->list )
        return -1;
    isom_elst_t *elst = trak->edts->elst;
    isom_elst_entry_t *data = (isom_elst_entry_t *)lsmash_get_entry_data( elst->list, entry_number );
    if( !data )
        return -1;
    data->segment_duration = segment_duration;
    data->media_time = media_time;
    data->media_rate = media_rate;
    if( !elst->pos || !elst->root->fragment || elst->root->bs->stream == stdout )
        return isom_update_tkhd_duration( trak );
    /* Rewrite the specified entry. */
    lsmash_bs_t *bs = root->bs;
    FILE *stream = bs->stream;
    uint64_t current_pos = lsmash_ftell( stream );
    uint64_t entry_pos = elst->pos + ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + ((uint64_t)entry_number - 1) * (elst->version == 1 ? 20 : 12);
    lsmash_fseek( stream, entry_pos, SEEK_SET );
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
    int ret = lsmash_bs_write_data( bs );
    lsmash_fseek( stream, current_pos, SEEK_SET );
    return ret;
}

int lsmash_create_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID, uint64_t segment_duration, int64_t media_time, int32_t media_rate )
{
    if( media_time < -1 )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->tkhd )
        return -1;
    segment_duration = (segment_duration || root->fragment) ? segment_duration
                     : trak->tkhd->duration ? trak->tkhd->duration
                     : isom_update_tkhd_duration( trak ) ? 0
                     : trak->tkhd->duration;
    if( isom_add_edts( trak )
     || isom_add_elst( trak->edts )
     || isom_add_elst_entry( trak->edts->elst, segment_duration, media_time, media_rate ) )
        return -1;
    return isom_update_tkhd_duration( trak );
}

/*---- create / modification time fields manipulators ----*/

int lsmash_update_media_modification_time( lsmash_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return -1;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    mdhd->modification_time = isom_get_current_mp4time();
    /* overwrite strange creation_time */
    if( mdhd->creation_time > mdhd->modification_time )
        mdhd->creation_time = mdhd->modification_time;
    return 0;
}

int lsmash_update_track_modification_time( lsmash_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->tkhd )
        return -1;
    isom_tkhd_t *tkhd = trak->tkhd;
    tkhd->modification_time = isom_get_current_mp4time();
    /* overwrite strange creation_time */
    if( tkhd->creation_time > tkhd->modification_time )
        tkhd->creation_time = tkhd->modification_time;
    return 0;
}

int lsmash_update_movie_modification_time( lsmash_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return -1;
    isom_mvhd_t *mvhd = root->moov->mvhd;
    mvhd->modification_time = isom_get_current_mp4time();
    /* overwrite strange creation_time */
    if( mvhd->creation_time > mvhd->modification_time )
        mvhd->creation_time = mvhd->modification_time;
    return 0;
}

/*---- sample manipulators ----*/
lsmash_sample_t *lsmash_create_sample( uint32_t size )
{
    lsmash_sample_t *sample = malloc( sizeof(lsmash_sample_t) );
    if( !sample )
        return NULL;
    memset( sample, 0, sizeof(lsmash_sample_t) );
    sample->data = malloc( size );
    if( !sample->data )
    {
        free( sample );
        return NULL;
    }
    sample->length = size;
    return sample;
}

void lsmash_delete_sample( lsmash_sample_t *sample )
{
    if( !sample )
        return;
    if( sample->data )
        free( sample->data );
    free( sample );
}

static uint32_t isom_add_size( isom_trak_entry_t *trak, uint32_t sample_size )
{
    if( !sample_size || isom_add_stsz_entry( trak->mdia->minf->stbl, sample_size ) )
        return 0;
    return isom_get_sample_count( trak );
}

static uint32_t isom_add_dts( isom_trak_entry_t *trak, uint64_t dts )
{
    if( !trak->cache || !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list )
        return 0;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_stts_t *stts = stbl->stts;
    isom_timestamp_t *cache = &trak->cache->timestamp;
    if( !stts->list->entry_count )
    {
        if( isom_add_stts_entry( stbl, dts ) )
            return 0;
        cache->dts = dts;
        return dts;
    }
    if( dts <= cache->dts )
        return 0;
    uint32_t sample_delta = dts - cache->dts;
    isom_stts_entry_t *data = (isom_stts_entry_t *)stts->list->tail->data;
    if( data->sample_delta == sample_delta )
        ++ data->sample_count;
    else if( isom_add_stts_entry( stbl, sample_delta ) )
        return 0;
    cache->dts = dts;
    return sample_delta;
}

static int isom_add_cts( isom_trak_entry_t *trak, uint64_t cts )
{
    if( !trak->cache )
        return -1;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_ctts_t *ctts = stbl->ctts;
    isom_timestamp_t *cache = &trak->cache->timestamp;
    if( !ctts )
    {
        if( cts == cache->dts )
        {
            cache->cts = cts;
            return 0;
        }
        /* Add ctts box and the first ctts entry. */
        if( isom_add_ctts( stbl ) || isom_add_ctts_entry( stbl, 0 ) )
            return -1;
        uint32_t sample_count = isom_get_sample_count( trak );
        ctts = stbl->ctts;
        isom_ctts_entry_t *data = (isom_ctts_entry_t *)ctts->list->head->data;
        if( sample_count != 1 )
        {
            data->sample_count = isom_get_sample_count( trak ) - 1;
            if( isom_add_ctts_entry( stbl, cts - cache->dts ) )
                return -1;
        }
        else
            data->sample_offset = cts;
        cache->cts = cts;
        return 0;
    }
    if( !ctts->list )
        return -1;
    isom_ctts_entry_t *data = (isom_ctts_entry_t *)ctts->list->tail->data;
    uint32_t sample_offset = cts - cache->dts;
    if( data->sample_offset == sample_offset )
        ++ data->sample_count;
    else if( isom_add_ctts_entry( stbl, sample_offset ) )
        return -1;
    cache->cts = cts;
    return 0;
}

static int isom_add_timestamp( isom_trak_entry_t *trak, uint64_t dts, uint64_t cts )
{
    if( cts < dts )
        return -1;
    uint32_t sample_count = isom_get_sample_count( trak );
    uint32_t sample_delta = sample_count > 1 ? isom_add_dts( trak, dts ) : 0;
    if( sample_count > 1 && !sample_delta )
        return -1;
    if( isom_add_cts( trak, cts ) )
        return -1;
    if( trak->cache->fragment )
    {
        isom_cache_t *cache = trak->cache;
        cache->fragment->last_duration = sample_delta;
        cache->fragment->largest_cts = LSMASH_MAX( cache->timestamp.cts, cache->fragment->largest_cts );
    }
    return 0;
}

static int isom_add_sync_point( isom_trak_entry_t *trak, uint32_t sample_number, lsmash_sample_property_t *prop )
{
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_cache_t *cache = trak->cache;
    if( prop->random_access_type != ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC )       /* no null check for prop */
    {
        if( !cache->all_sync )
            return 0;
        if( !stbl->stss && isom_add_stss( stbl ) )
            return -1;
        if( isom_add_stss_entry( stbl, 1 ) )    /* Declare here the first sample is a sync sample. */
            return -1;
        cache->all_sync = 0;
        return 0;
    }
    if( cache->all_sync )     /* We don't need stss box if all samples are sync sample. */
        return 0;
    if( !stbl->stss )
    {
        if( isom_get_sample_count( trak ) == 1 )
        {
            cache->all_sync = 1;    /* Also the first sample is a sync sample. */
            return 0;
        }
        if( isom_add_stss( stbl ) )
            return -1;
    }
    return isom_add_stss_entry( stbl, sample_number );
}

static int isom_add_partial_sync( isom_trak_entry_t *trak, uint32_t sample_number, lsmash_sample_property_t *prop )
{
    if( !trak->root->qt_compatible )
        return 0;
    if( prop->random_access_type != QT_SAMPLE_RANDOM_ACCESS_TYPE_PARTIAL_SYNC
     && !(prop->random_access_type == ISOM_SAMPLE_RANDOM_ACCESS_TYPE_RECOVERY && prop->recovery.identifier == prop->recovery.complete) )
        return 0;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    if( !stbl->stps && isom_add_stps( stbl ) )
        return -1;
    return isom_add_stps_entry( stbl, sample_number );
}

static int isom_add_dependency_type( isom_trak_entry_t *trak, lsmash_sample_property_t *prop )
{
    if( !trak->root->qt_compatible && !trak->root->avc_extensions )
        return 0;
    uint8_t avc_extensions = trak->root->avc_extensions;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    if( stbl->sdtp )
        return isom_add_sdtp_entry( stbl, prop, avc_extensions );
    if( !prop->allow_earlier && !prop->leading && !prop->independent && !prop->disposable && !prop->redundant )  /* no null check for prop */
        return 0;
    if( isom_add_sdtp( stbl ) )
        return -1;
    uint32_t count = isom_get_sample_count( trak );
    /* fill past samples with ISOM_SAMPLE_*_UNKNOWN */
    lsmash_sample_property_t null_prop = { 0 };
    for( uint32_t i = 1; i < count; i++ )
        if( isom_add_sdtp_entry( stbl, &null_prop, avc_extensions ) )
            return -1;
    return isom_add_sdtp_entry( stbl, prop, avc_extensions );
}

static int isom_group_random_access( isom_trak_entry_t *trak, lsmash_sample_property_t *prop )
{
    if( trak->root->max_isom_version < 6 )
        return 0;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_sbgp_entry_t *sbgp = isom_get_sample_to_group( stbl, ISOM_GROUP_TYPE_RAP );
    isom_sgpd_entry_t *sgpd = isom_get_sample_group_description( stbl, ISOM_GROUP_TYPE_RAP );
    if( !sbgp || !sgpd )
        return 0;
    uint8_t is_rap = prop->random_access_type == ISOM_SAMPLE_RANDOM_ACCESS_TYPE_CLOSED_RAP
                  || prop->random_access_type == ISOM_SAMPLE_RANDOM_ACCESS_TYPE_OPEN_RAP
                  || (prop->random_access_type == ISOM_SAMPLE_RANDOM_ACCESS_TYPE_RECOVERY && prop->recovery.identifier == prop->recovery.complete);
    isom_rap_group_t *group = trak->cache->rap;
    if( !group )
    {
        /* This sample is the first sample, create a grouping cache. */
        assert( isom_get_sample_count( trak ) == 1 );
        group = malloc( sizeof(isom_rap_group_t) );
        if( !group )
            return -1;
        if( is_rap )
        {
            group->random_access = isom_add_rap_group_entry( sgpd );
            group->assignment = isom_add_group_assignment_entry( sbgp, 1, sgpd->list->entry_count );
        }
        else
        {
            /* The first sample is not always random access point. */
            group->random_access = NULL;
            group->assignment = isom_add_group_assignment_entry( sbgp, 1, 0 );
        }
        if( !group->assignment )
        {
            free( group );
            return -1;
        }
        /* No need checking if group->assignment exists from here. */
        group->is_prev_rap = is_rap;
        trak->cache->rap = group;
        return 0;
    }
    if( group->is_prev_rap )
    {
        /* OK. here, the previous sample is a menber of 'rap '. */
        if( !is_rap )
        {
            /* This sample isn't a member of 'rap ' and the previous sample is.
             * So we create a new group and set 0 on its group_description_index. */
            group->assignment = isom_add_group_assignment_entry( sbgp, 1, 0 );
            if( !group->assignment )
            {
                free( group );
                return -1;
            }
        }
        else if( prop->random_access_type != ISOM_SAMPLE_RANDOM_ACCESS_TYPE_CLOSED_RAP )
        {
            /* Create a new group since there is the possibility the next sample is a leading sample.
             * This sample is a member of 'rap ', so we set appropriate value on its group_description_index. */
            if( group->random_access )
                group->random_access->num_leading_samples_known = 1;
            group->random_access = isom_add_rap_group_entry( sgpd );
            group->assignment = isom_add_group_assignment_entry( sbgp, 1, sgpd->list->entry_count );
            if( !group->assignment )
            {
                free( group );
                return -1;
            }
        }
        else    /* The previous and current sample are a member of 'rap ', and the next sample must not be a leading sample. */
            ++ group->assignment->sample_count;
    }
    else if( is_rap )
    {
        /* This sample is a member of 'rap ' and the previous sample isn't.
         * So we create a new group and set appropriate value on its group_description_index. */
        if( group->random_access )
            group->random_access->num_leading_samples_known = 1;
        group->random_access = isom_add_rap_group_entry( sgpd );
        group->assignment = isom_add_group_assignment_entry( sbgp, 1, sgpd->list->entry_count );
        if( !group->assignment )
        {
            free( group );
            return -1;
        }
    }
    else    /* The previous and current sample aren't a member of 'rap '. */
        ++ group->assignment->sample_count;
    /* Obtain the property of the latest random access point group. */
    if( !is_rap && group->random_access )
    {
        if( prop->leading == ISOM_SAMPLE_LEADING_UNKNOWN )
        {
            /* We can no longer know num_leading_samples in this group. */
            group->random_access->num_leading_samples_known = 0;
            group->random_access = NULL;
        }
        else
        {
            if( prop->leading == ISOM_SAMPLE_IS_UNDECODABLE_LEADING || prop->leading == ISOM_SAMPLE_IS_DECODABLE_LEADING )
                ++ group->random_access->num_leading_samples;
            else
            {
                /* no more consecutive leading samples in this group */
                group->random_access->num_leading_samples_known = 1;
                group->random_access = NULL;
            }
        }
    }
    group->is_prev_rap = is_rap;
    return 0;
}

static int isom_group_roll_recovery( isom_trak_entry_t *trak, lsmash_sample_property_t *prop )
{
    if( !trak->root->avc_extensions )
        return 0;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_sbgp_entry_t *sbgp = isom_get_sample_to_group( stbl, ISOM_GROUP_TYPE_ROLL );
    isom_sgpd_entry_t *sgpd = isom_get_sample_group_description( stbl, ISOM_GROUP_TYPE_ROLL );
    if( !sbgp || !sgpd )
        return 0;
    lsmash_entry_list_t *pool = trak->cache->roll.pool;
    if( !pool )
    {
        pool = lsmash_create_entry_list();
        if( !pool )
            return -1;
        trak->cache->roll.pool = pool;
    }
    isom_roll_group_t *group = (isom_roll_group_t *)lsmash_get_entry_data( pool, pool->entry_count );
    uint32_t sample_count = isom_get_sample_count( trak );
    if( !group || prop->random_access_type == ISOM_SAMPLE_RANDOM_ACCESS_TYPE_RECOVERY )
    {
        if( group )
            group->delimited = 1;
        else
            assert( sample_count == 1 );
        /* Create a new group. This group is not 'roll' yet, so we set 0 on its group_description_index. */
        group = malloc( sizeof(isom_roll_group_t) );
        if( !group )
            return -1;
        memset( group, 0, sizeof(isom_roll_group_t) );
        group->first_sample = sample_count;
        group->recovery_point = prop->recovery.complete;
        group->assignment = isom_add_group_assignment_entry( sbgp, 1, 0 );
        if( !group->assignment || lsmash_add_entry( pool, group ) )
        {
            free( group );
            return -1;
        }
    }
    else
        ++ group->assignment->sample_count;
    /* If encountered a sync sample, all recoveries are completed here. */
    if( prop->random_access_type == ISOM_SAMPLE_RANDOM_ACCESS_TYPE_CLOSED_RAP )
    {
        for( lsmash_entry_t *entry = pool->head; entry; entry = entry->next )
        {
            group = (isom_roll_group_t *)entry->data;
            if( !group )
                return -1;
            group->described = 1;
        }
        return 0;
    }
    for( lsmash_entry_t *entry = pool->head; entry; entry = entry->next )
    {
        group = (isom_roll_group_t *)entry->data;
        if( !group )
            return -1;
        if( group->described )
            continue;
        if( prop->recovery.identifier == group->recovery_point )
        {
            group->described = 1;
            int16_t distance = sample_count - group->first_sample;
            /* Add a roll recovery entry only when roll_distance isn't zero since roll_distance = 0 must not be used. */
            if( distance )
            {
                /* Now, this group is a 'roll'. */
                if( !isom_add_roll_group_entry( sgpd, distance ) )
                    return -1;
                group->assignment->group_description_index = sgpd->list->entry_count;
                /* All groups before the current group are described. */
                lsmash_entry_t *current = entry;
                for( entry = pool->head; entry != current; entry = entry->next )
                {
                    group = (isom_roll_group_t *)entry->data;
                    if( !group )
                        return -1;
                    group->described = 1;
                }
            }
            break;      /* Avoid evaluating groups, in the pool, having the same identifier for recovery point again. */
        }
    }
    /* Remove pooled caches that has become unnecessary. */
    for( lsmash_entry_t *entry = pool->head; entry; entry = pool->head )
    {
        group = (isom_roll_group_t *)entry->data;
        if( !group )
            return -1;
        if( !group->delimited || !group->described )
            break;
        if( lsmash_remove_entry_direct( pool, entry, NULL ) )
            return -1;
    }
    return 0;
}

/* returns 1 if pooled samples must be flushed. */
/* FIXME: I wonder if this function should have a extra argument which indicates force_to_flush_cached_chunk.
   see lsmash_append_sample for detail. */
static int isom_add_chunk( isom_trak_entry_t *trak, lsmash_sample_t *sample )
{
    if( !trak->root || !trak->cache || !trak->mdia->mdhd || !trak->mdia->mdhd->timescale
     || !trak->mdia->minf->stbl->stsc || !trak->mdia->minf->stbl->stsc->list )
        return -1;
    lsmash_root_t *root = trak->root;
    isom_chunk_t *current = &trak->cache->chunk;
    if( !current->pool )
    {
        /* Very initial settings, just once per track */
        current->pool = lsmash_create_entry_list();
        if( !current->pool )
            return -1;
    }
    if( !current->pool->entry_count )
    {
        /* Cannot decide whether we should flush the current sample or not here yet. */
        ++ current->chunk_number;
        current->sample_description_index = sample->index;
        current->first_dts = sample->dts;
        return 0;
    }
    if( sample->dts < current->first_dts )
        return -1; /* easy error check. */
    double chunk_duration = (double)(sample->dts - current->first_dts) / trak->mdia->mdhd->timescale;
    if( root->max_chunk_duration >= chunk_duration && current->sample_description_index == sample->index )
        return 0; /* no need to flush current cached chunk, the current sample must be put into that. */

    /* NOTE: chunk relative stuff must be pushed into root after a chunk is fully determined with its contents. */
    /* now current cached chunk is fixed, actually add chunk relative properties to root accordingly. */

    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    lsmash_entry_t *last_stsc_entry = stbl->stsc->list->tail;
    /* Create a new chunk sequence in this track if needed. */
    if( (!last_stsc_entry || current->pool->entry_count != ((isom_stsc_entry_t *)last_stsc_entry->data)->samples_per_chunk)
     && isom_add_stsc_entry( stbl, current->chunk_number, current->pool->entry_count, current->sample_description_index ) )
        return -1;
    /* Add a new chunk offset in this track. */
    uint64_t offset = root->size;
    if( root->fragment )
        offset += ISOM_DEFAULT_BOX_HEADER_SIZE + root->fragment->pool_size;
    if( isom_add_stco_entry( stbl, offset ) )
        return -1;
    /* update cache information */
    ++ current->chunk_number;
    /* re-initialize cache, using the current sample */
    current->sample_description_index = sample->index;
    current->first_dts = sample->dts;
    /* current->pool must be flushed in isom_append_sample_internal() */
    return 1;
}

static int isom_write_sample_data( lsmash_root_t *root, lsmash_sample_t *sample )
{
    if( !root->mdat || !root->bs || !root->bs->stream )
        return -1;
    lsmash_bs_put_bytes( root->bs, sample->data, sample->length );
    if( lsmash_bs_write_data( root->bs ) )
        return -1;
    root->mdat->size += sample->length;
    return 0;
}

static int isom_write_pooled_samples( isom_trak_entry_t *trak, lsmash_entry_list_t *pool )
{
    if( !trak->root || !trak->cache || !trak->tkhd )
        return -1;
    lsmash_root_t *root = trak->root;
    for( lsmash_entry_t *entry = pool->head; entry; entry = entry->next )
    {
        lsmash_sample_t *sample = (lsmash_sample_t *)entry->data;
        if( !sample || !sample->data
         || isom_write_sample_data( root, sample ) )
            return -1;
        root->size += sample->length;
    }
    lsmash_remove_entries( pool, lsmash_delete_sample );
    return 0;
}

static int isom_update_sample_tables( isom_trak_entry_t *trak, lsmash_sample_t *sample )
{
    /* Add a sample_size and increment sample_count. */
    uint32_t sample_count = isom_add_size( trak, sample->length );
    if( !sample_count )
        return -1;
    /* Add a decoding timestamp and a composition timestamp. */
    if( isom_add_timestamp( trak, sample->dts, sample->cts ) )
        return -1;
    /* Add a sync point if needed. */
    if( isom_add_sync_point( trak, sample_count, &sample->prop ) )
        return -1;
    /* Add a partial sync point if needed. */
    if( isom_add_partial_sync( trak, sample_count, &sample->prop ) )
        return -1;
    /* Add leading, independent, disposable and redundant information if needed. */
    if( isom_add_dependency_type( trak, &sample->prop ) )
        return -1;
    /* Group samples into random access point type if needed. */
    if( isom_group_random_access( trak, &sample->prop ) )
        return -1;
    /* Group samples into random access recovery point type if needed. */
    if( isom_group_roll_recovery( trak, &sample->prop ) )
        return -1;
    /* Add a chunk if needed. */
    return isom_add_chunk( trak, sample );
}

static void isom_append_fragment_track_run( lsmash_root_t *root, isom_chunk_t *chunk )
{
    if( !chunk->pool || !chunk->pool->head )
        return;
    isom_fragment_manager_t *fragment = root->fragment;
    /* Move samples in the pool of the current track fragment to the pool of the current movie fragment.
     * Empty the pool of current track. We don't delete data of samples here. */
    if( fragment->pool->tail )
    {
        fragment->pool->tail->next = chunk->pool->head;
        fragment->pool->tail->next->prev = fragment->pool->tail;
    }
    else
        fragment->pool->head = chunk->pool->head;
    fragment->pool->tail = chunk->pool->tail;
    fragment->pool->entry_count += chunk->pool->entry_count;
    fragment->pool_size += chunk->pool_size;
    chunk->pool_size = 0;
    chunk->pool->entry_count = 0;
    chunk->pool->head = NULL;
    chunk->pool->tail = NULL;
}

static int isom_output_cached_chunk( isom_trak_entry_t *trak )
{
    lsmash_root_t *root = trak->root;
    isom_chunk_t *chunk = &trak->cache->chunk;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    lsmash_entry_t *last_stsc_entry = stbl->stsc->list->tail;
    /* Create a new chunk sequence in this track if needed. */
    if( (!last_stsc_entry || chunk->pool->entry_count != ((isom_stsc_entry_t *)last_stsc_entry->data)->samples_per_chunk)
     && isom_add_stsc_entry( stbl, chunk->chunk_number, chunk->pool->entry_count, chunk->sample_description_index ) )
        return -1;
    if( root->fragment )
    {
        /* Add a new chunk offset in this track. */
        if( isom_add_stco_entry( stbl, root->size + ISOM_DEFAULT_BOX_HEADER_SIZE + root->fragment->pool_size ) )
            return -1;
        isom_append_fragment_track_run( root, chunk );
        return 0;
    }
    /* Add a new chunk offset in this track. */
    if( isom_add_stco_entry( stbl, root->size ) )
        return -1;
    /* Output pooled samples in this track. */
    return isom_write_pooled_samples( trak, chunk->pool );
}

static int isom_append_sample_internal( isom_trak_entry_t *trak, lsmash_sample_t *sample )
{
    int flush = isom_update_sample_tables( trak, sample );
    if( flush < 0 )
        return -1;
    /* flush == 1 means pooled samples must be flushed. */
    isom_chunk_t *current = &trak->cache->chunk;
    if( flush == 1 && isom_write_pooled_samples( trak, current->pool ) )
        return -1;
    /* Arbitration system between tracks with extremely scattering dts.
     * Here, we check whether asynchronization between the tracks exceeds the tolerance.
     * If a track has too old "first DTS" in its cached chunk than current sample's DTS, then its pooled samples must be flushed.
     * We don't consider presentation of media since any edit can pick an arbitrary portion of media in track.
     * Note: you needn't read this loop until you grasp the basic handling of chunks. */
    lsmash_root_t *root = trak->root;
    double tolerance = root->max_async_tolerance;
    for( lsmash_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_entry_t *other = (isom_trak_entry_t *)entry->data;
        if( trak == other )
            continue;
        if( !other || !other->cache || !other->mdia || !other->mdia->mdhd || !other->mdia->mdhd->timescale
         || !other->mdia->minf || !other->mdia->minf->stbl || !other->mdia->minf->stbl->stsc || !other->mdia->minf->stbl->stsc->list )
            return -1;
        isom_chunk_t *chunk = &other->cache->chunk;
        if( !chunk->pool || !chunk->pool->entry_count )
            continue;
        double diff = ((double)sample->dts / trak->mdia->mdhd->timescale)
                    - ((double)chunk->first_dts / other->mdia->mdhd->timescale);
        if( diff > tolerance && isom_output_cached_chunk( other ) )
            return -1;
        /* Note: we don't flush the cached chunk in the current track and the current sample here
         * even if the conditional expression of '-diff > tolerance' meets.
         * That's useless because appending a sample to another track would be a good equivalent.
         * It's even harmful because it causes excess chunk division by calling
         * isom_output_cached_chunk() which always generates a new chunk. 
         * Anyway some excess chunk division will be there, but rather less without it.
         * To completely avoid this, we need to observe at least whether the current sample will be placed
         * right next to the previous chunk of the same track or not. */
    }
    /* anyway the current sample must be pooled. */
    return lsmash_add_entry( current->pool, sample );
}

static int isom_append_sample( lsmash_root_t *root, uint32_t track_ID, lsmash_sample_t *sample )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->root || !trak->cache || !trak->mdia
     || !trak->mdia->mdhd || !trak->mdia->mdhd->timescale
     || !trak->mdia->minf || !trak->mdia->minf->stbl
     || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list
     || !trak->mdia->minf->stbl->stsc || !trak->mdia->minf->stbl->stsc->list )
        return -1;
    /* If there is no available Media Data Box to write samples, add and write a new one before any chunk offset is decided. */
    if( !root->mdat )
    {
        if( isom_new_mdat( root, 0 ) )
            return -1;
        /* Add the size of the Media Data Box and the placeholder. */
        root->size += 2 * ISOM_DEFAULT_BOX_HEADER_SIZE;
    }
    isom_sample_entry_t *sample_entry = (isom_sample_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, sample->index );
    if( !sample_entry )
        return -1;
    if( isom_is_lpcm_audio( sample_entry->type ) )
    {
        uint32_t frame_size = ((isom_audio_entry_t *)sample_entry)->constBytesPerAudioPacket;
        uint64_t dts = sample->dts;
        uint64_t cts = sample->cts;
        /* Append samples splitted into each LPCMFrame. */
        for( uint32_t offset = 0; offset < sample->length; offset += frame_size )
        {
            lsmash_sample_t *lpcm_sample = lsmash_create_sample( frame_size );
            if( !lpcm_sample )
                return -1;
            memcpy( lpcm_sample->data, sample->data + offset, frame_size );
            lpcm_sample->dts = dts++;
            lpcm_sample->cts = cts++;
            lpcm_sample->prop = sample->prop;
            lpcm_sample->index = sample->index;
            if( isom_append_sample_internal( trak, lpcm_sample ) )
            {
                lsmash_delete_sample( lpcm_sample );
                return -1;
            }
        }
        lsmash_delete_sample( sample );
        return 0;
    }
    return isom_append_sample_internal( trak, sample );
}

static int isom_output_cache( isom_trak_entry_t *trak )
{
    if( trak->cache->chunk.pool && trak->cache->chunk.pool->entry_count
     && isom_output_cached_chunk( trak ) )
        return -1;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    if( !stbl->sgpd_list )
        return 0;
    for( lsmash_entry_t *entry = stbl->sgpd_list->head; entry; entry = entry->next )
    {
        isom_sgpd_entry_t *sgpd = (isom_sgpd_entry_t *)entry->data;
        if( !sgpd )
            return -1;
        switch( sgpd->grouping_type )
        {
            case ISOM_GROUP_TYPE_RAP :
            {
                isom_rap_group_t *group = trak->cache->rap;
                if( !group )
                {
                    if( trak->root->fragment )
                        continue;
                    else
                        return -1;
                }
                if( !group->random_access )
                    continue;
                group->random_access->num_leading_samples_known = 1;
                break;
            }
            case ISOM_GROUP_TYPE_ROLL :
                if( !trak->cache->roll.pool )
                {
                    if( trak->root->fragment )
                        continue;
                    else
                        return -1;
                }
                for( lsmash_entry_t *roll_entry = trak->cache->roll.pool->head; roll_entry; roll_entry = roll_entry->next )
                {
                    isom_roll_group_t *group = (isom_roll_group_t *)roll_entry->data;
                    if( !group )
                        return -1;
                    group->described = 1;
                }
                break;
            default :
                break;
        }
    }
    return 0;
}

static int isom_flush_fragment_pooled_samples( lsmash_root_t *root, uint32_t track_ID, uint32_t last_sample_duration )
{
    isom_traf_entry_t *traf = isom_get_traf( root->fragment->movie, track_ID );
    if( !traf )
        return 0;   /* no samples */
    if( !traf->cache || !traf->cache->fragment )
        return -1;
    if( traf->trun_list && traf->trun_list->entry_count && traf->trun_list->tail && traf->trun_list->tail->data )
    {
        /* Media Data Box preceded by Movie Fragment Box could change base_data_offsets in each track fragments later.
         * We can't consider this here because the length of Movie Fragment Box is unknown at this step yet. */
        isom_trun_entry_t *trun = (isom_trun_entry_t *)traf->trun_list->tail->data;
        trun->flags |= ISOM_TR_FLAGS_DATA_OFFSET_PRESENT;
        trun->data_offset = root->fragment->pool_size;
    }
    isom_append_fragment_track_run( root, &traf->cache->chunk );
    return isom_set_fragment_last_duration( traf, last_sample_duration );
}

int lsmash_flush_pooled_samples( lsmash_root_t *root, uint32_t track_ID, uint32_t last_sample_delta )
{
    if( !root )
        return -1;
    if( root->fragment && root->fragment->movie )
        return isom_flush_fragment_pooled_samples( root, track_ID, last_sample_delta );
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->cache || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl
     || !trak->mdia->minf->stbl->stsc || !trak->mdia->minf->stbl->stsc->list )
        return -1;
    if( isom_output_cache( trak ) )
        return -1;
    return lsmash_set_last_sample_delta( root, track_ID, last_sample_delta );
}

/* This function doesn't update sample_duration of the last sample in the previous movie fragment.
 * Instead of this, isom_finish_movie_fragment undertakes this task. */
static int isom_update_fragment_previous_sample_duration( isom_traf_entry_t *traf, isom_trex_entry_t *trex, uint32_t duration )
{
    isom_tfhd_t *tfhd = traf->tfhd;
    isom_trun_entry_t *trun = (isom_trun_entry_t *)traf->trun_list->tail->data;
    int previous_run_has_previous_sample = 0;
    if( trun->sample_count == 1 )
    {
        if( traf->trun_list->entry_count == 1 )
            return 0;       /* The previous track run belongs to the previous movie fragment if it exists. */
        if( !traf->trun_list->tail->prev || !traf->trun_list->tail->prev->data )
            return -1;
        /* OK. The previous sample exists in the previous track run in the same track fragment. */
        trun = (isom_trun_entry_t *)traf->trun_list->tail->prev->data;
        previous_run_has_previous_sample = 1;
    }
    /* Update default_sample_duration of the Track Fragment Header Box
     * if this duration is what the first sample in the current track fragment owns. */
    if( (trun->sample_count == 2 && traf->trun_list->entry_count == 1)
     || (trun->sample_count == 1 && traf->trun_list->entry_count == 2) )
    {
        if( duration != trex->default_sample_duration )
            tfhd->flags |= ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT;
        tfhd->default_sample_duration = duration;
    }
    /* Update the previous sample_duration if needed. */
    if( duration != tfhd->default_sample_duration )
        trun->flags |= ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT;
    if( trun->flags )
    {
        uint32_t sample_number = trun->sample_count - !previous_run_has_previous_sample;
        isom_trun_optional_row_t *row = isom_request_trun_optional_row( trun, tfhd, sample_number );
        if( !row )
            return -1;
        row->sample_duration = duration;
    }
    traf->cache->fragment->last_duration = duration;
    return 0;
}

static isom_sample_flags_t isom_generate_fragment_sample_flags( lsmash_sample_t *sample )
{
    isom_sample_flags_t flags;
    flags.reserved                    = 0;
    flags.is_leading                  = sample->prop.leading     & 0x3;
    flags.sample_depends_on           = sample->prop.independent & 0x3;
    flags.sample_is_depended_on       = sample->prop.disposable  & 0x3;
    flags.sample_has_redundancy       = sample->prop.redundant   & 0x3;
    flags.sample_padding_value        = 0;
    flags.sample_is_difference_sample = sample->prop.random_access_type != ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
    flags.sample_degradation_priority = 0;
    return flags;
}

static int isom_update_fragment_sample_tables( isom_traf_entry_t *traf, lsmash_sample_t *sample )
{
    isom_tfhd_t *tfhd = traf->tfhd;
    isom_trex_entry_t *trex = isom_get_trex( traf->root->moov->mvex, tfhd->track_ID );
    if( !trex )
        return -1;
    lsmash_root_t *root = traf->root;
    isom_cache_t *cache = traf->cache;
    /* Create a new track run if the duration exceeds max_chunk_duration.
     * Old one will be appended to the pool of this movie fragment. */
    int delimit = root->max_chunk_duration < (double)(sample->dts - traf->cache->chunk.first_dts) / lsmash_get_media_timescale( root, tfhd->track_ID );
    isom_trun_entry_t *trun = NULL;
    if( !traf->trun_list || !traf->trun_list->entry_count || delimit )
    {
        if( delimit && traf->trun_list && traf->trun_list->entry_count && traf->trun_list->tail && traf->trun_list->tail->data )
        {
            /* Media Data Box preceded by Movie Fragment Box could change base data offsets in each track fragments later.
             * We can't consider this here because the length of Movie Fragment Box is unknown at this step yet. */
            trun = (isom_trun_entry_t *)traf->trun_list->tail->data;
            if( root->fragment->pool_size )
                trun->flags |= ISOM_TR_FLAGS_DATA_OFFSET_PRESENT;
            trun->data_offset = root->fragment->pool_size;
        }
        trun = isom_add_trun( traf );
        if( !trun )
            return -1;
        if( !cache->chunk.pool )
        {
            /* Very initial settings, just once per track */
            cache->chunk.pool = lsmash_create_entry_list();
            if( !cache->chunk.pool )
                return -1;
        }
    }
    else
    {
        if( !traf->trun_list->tail || !traf->trun_list->tail->data )
            return -1;
        trun = (isom_trun_entry_t *)traf->trun_list->tail->data;
    }
    uint32_t sample_composition_time_offset = sample->cts - sample->dts;
    isom_sample_flags_t sample_flags = isom_generate_fragment_sample_flags( sample );
    if( ++trun->sample_count == 1 )
    {
        if( traf->trun_list->entry_count == 1 )
        {
            /* This track fragment isn't empty-duration-fragment any more. */
            tfhd->flags &= ~ISOM_TF_FLAGS_DURATION_IS_EMPTY;
            /* Set up sample_description_index in this track fragment. */
            if( sample->index != trex->default_sample_description_index )
                tfhd->flags |= ISOM_TF_FLAGS_SAMPLE_DESCRIPTION_INDEX_PRESENT;
            tfhd->sample_description_index = cache->chunk.sample_description_index = sample->index;
            /* Set up default_sample_size used in this track fragment. */
            tfhd->default_sample_size = sample->length;
            /* Set up default_sample_flags used in this track fragment.
             * Note: we decide an appropriate default value at the end of this movie fragment. */
            tfhd->default_sample_flags = sample_flags;
            /* Set up random access information if this sample is random accessible sample.
             * We inform only the first sample in each movie fragment. */
            if( root->bs->stream != stdout && sample->prop.random_access_type )
            {
                isom_tfra_entry_t *tfra = isom_get_tfra( root->mfra, tfhd->track_ID );
                if( !tfra )
                {
                    tfra = isom_add_tfra( root->mfra );
                    if( !tfra )
                        return -1;
                    tfra->track_ID = tfhd->track_ID;
                }
                if( !tfra->list )
                {
                    tfra->list = lsmash_create_entry_list();
                    if( !tfra->list )
                        return -1;
                }
                isom_tfra_location_time_entry_t *rap = malloc( sizeof(isom_tfra_location_time_entry_t) );
                if( !rap )
                    return -1;
                rap->time          = sample->cts;   /* If this is wrong, blame vague descriptions of 'presentation time' in the spec. */
                rap->moof_offset   = root->size;    /* We place Movie Fragment Box in the head of each movie fragment. */
                rap->traf_number   = cache->fragment->traf_number;
                rap->trun_number   = traf->trun_list->entry_count;
                rap->sample_number = trun->sample_count;
                if( lsmash_add_entry( tfra->list, rap ) )
                    return -1;
                tfra->number_of_entry = tfra->list->entry_count;
                int length;
                for( length = 1; rap->traf_number >> (length * 8); length++ );
                tfra->length_size_of_traf_num = LSMASH_MAX( length - 1, tfra->length_size_of_traf_num );
                for( length = 1; rap->traf_number >> (length * 8); length++ );
                tfra->length_size_of_trun_num = LSMASH_MAX( length - 1, tfra->length_size_of_trun_num );
                for( length = 1; rap->sample_number >> (length * 8); length++ );
                tfra->length_size_of_sample_num = LSMASH_MAX( length - 1, tfra->length_size_of_sample_num );
            }
        }
        trun->first_sample_flags = sample_flags;
        cache->chunk.first_dts = sample->dts;
    }
    /* Update the optional rows in the current track run except for sample_duration if needed. */
    if( sample->length != tfhd->default_sample_size )
        trun->flags |= ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT;
    if( memcmp( &sample_flags, &tfhd->default_sample_flags, sizeof(isom_sample_flags_t) ) )
        trun->flags |= ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT;
    if( sample_composition_time_offset )
        trun->flags |= ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT;
    if( trun->flags )
    {
        isom_trun_optional_row_t *row = isom_request_trun_optional_row( trun, tfhd, trun->sample_count );
        if( !row )
            return -1;
        row->sample_size                    = sample->length;
        row->sample_flags                   = sample_flags;
        row->sample_composition_time_offset = sample_composition_time_offset;
    }
    /* Set up the previous sample_duration if this sample is not the first sample in the overall movie. */
    if( cache->fragment->has_samples )
    {
        /* Note: when using for live streaming, it is not good idea to return error (-1) by sample->dts < prev_dts
         * since that's trivial for such semi-permanent presentation. */
        uint64_t prev_dts = cache->timestamp.dts;
        if( sample->dts <= prev_dts || sample->dts > prev_dts + UINT32_MAX )
            return -1;
        uint32_t sample_duration = sample->dts - prev_dts;
        if( isom_update_fragment_previous_sample_duration( traf, trex, sample_duration ) )
            return -1;
    }
    cache->timestamp.dts = sample->dts;
    cache->fragment->largest_cts = LSMASH_MAX( sample->cts, cache->fragment->largest_cts );
    return delimit;
}

static int isom_append_fragment_sample_internal_initial( isom_trak_entry_t *trak, lsmash_sample_t *sample )
{
    int delimit = 0;
    /* Update the sample tables of this track fragment.
     * If a new chunk was created, append the previous one to the pool of this movie fragment. */
    delimit = isom_update_sample_tables( trak, sample );
    if( delimit < 0 )
        return -1;
    else if( delimit == 1 )
        isom_append_fragment_track_run( trak->root, &trak->cache->chunk );
    /* Add a new sample into the pool of this track fragment. */
    trak->cache->chunk.pool_size += sample->length;
    trak->cache->fragment->has_samples = 1;
    return lsmash_add_entry( trak->cache->chunk.pool, sample );
}

static int isom_append_fragment_sample_internal( isom_traf_entry_t *traf, lsmash_sample_t *sample )
{
    int delimit = 0;
    /* Update the sample tables of this track fragment.
     * If a new track run was created, append the previous one to the pool of this movie fragment. */
    delimit = isom_update_fragment_sample_tables( traf, sample );
    if( delimit < 0 )
        return -1;
    else if( delimit == 1 )
        isom_append_fragment_track_run( traf->root, &traf->cache->chunk );
    /* Add a new sample into the pool of this track fragment. */
    traf->cache->chunk.pool_size += sample->length;
    traf->cache->fragment->has_samples = 1;
    return lsmash_add_entry( traf->cache->chunk.pool, sample );
}

static int isom_append_fragment_sample( lsmash_root_t *root, uint32_t track_ID, lsmash_sample_t *sample )
{
    isom_fragment_manager_t *fragment = root->fragment;
    if( !fragment || !fragment->pool )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->root || !trak->cache || !trak->cache->fragment || !trak->tkhd || !trak->mdia
     || !trak->mdia->mdhd || !trak->mdia->mdhd->timescale
     || !trak->mdia->minf || !trak->mdia->minf->stbl
     || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list
     || !trak->mdia->minf->stbl->stsc || !trak->mdia->minf->stbl->stsc->list )
        return -1;
    int (*append_sample_func)( void *, lsmash_sample_t * ) = NULL;
    void *track_fragment = NULL;
    if( !fragment->movie )
    {
        append_sample_func = (int (*)( void *, lsmash_sample_t * ))isom_append_fragment_sample_internal_initial;
        track_fragment = trak;
    }
    else
    {
        isom_traf_entry_t *traf = isom_get_traf( fragment->movie, track_ID );
        if( !traf )
        {
            traf = isom_add_traf( root, fragment->movie );
            if( isom_add_tfhd( traf ) )
                return -1;
            traf->tfhd->flags = ISOM_TF_FLAGS_DURATION_IS_EMPTY;        /* no samples for this track fragment yet */
            traf->tfhd->track_ID = trak->tkhd->track_ID;
            traf->cache = trak->cache;
            traf->cache->fragment->traf_number = fragment->movie->traf_list->entry_count;
        }
        else if( !traf->root || !traf->root->moov || !traf->root->moov->mvex || !traf->cache || !traf->tfhd )
            return -1;
        append_sample_func = (int (*)( void *, lsmash_sample_t * ))isom_append_fragment_sample_internal;
        track_fragment = traf;
    }
    isom_sample_entry_t *sample_entry = (isom_sample_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, sample->index );
    if( !sample_entry )
        return -1;
    if( isom_is_lpcm_audio( sample_entry->type ) )
    {
        uint32_t frame_size = ((isom_audio_entry_t *)sample_entry)->constBytesPerAudioPacket;
        uint64_t dts = sample->dts;
        uint64_t cts = sample->cts;
        /* Append samples splitted into each LPCMFrame. */
        for( uint32_t offset = 0; offset < sample->length; offset += frame_size )
        {
            lsmash_sample_t *lpcm_sample = lsmash_create_sample( frame_size );
            if( !lpcm_sample )
                return -1;
            memcpy( lpcm_sample->data, sample->data + offset, frame_size );
            lpcm_sample->dts = dts++;
            lpcm_sample->cts = cts++;
            lpcm_sample->prop = sample->prop;
            lpcm_sample->index = sample->index;
            if( append_sample_func( track_fragment, lpcm_sample ) )
            {
                lsmash_delete_sample( lpcm_sample );
                return -1;
            }
        }
        lsmash_delete_sample( sample );
        return 0;
    }
    return append_sample_func( track_fragment, sample );
}

int lsmash_append_sample( lsmash_root_t *root, uint32_t track_ID, lsmash_sample_t *sample )
{
    /* We think max_chunk_duration == 0, which means all samples will be cached on memory, should be prevented.
     * This means removal of a feature that we used to have, but anyway very alone chunk does not make sense. */
    if( !root || !root->bs || !sample || !sample->data || !track_ID
     || root->max_chunk_duration == 0 || root->max_async_tolerance == 0 )
        return -1;
    if( root->fragment && root->fragment->pool )
        return isom_append_fragment_sample( root, track_ID, sample );
    return isom_append_sample( root, track_ID, sample );
}

/*---- misc functions ----*/

#define CHAPTER_BUFSIZE 512 /* for chapter handling */

static int isom_get_start_time( char *chap_time, isom_chapter_entry_t *data )
{
    uint64_t hh, mm;
    double ss;
    if( sscanf( chap_time, "%"SCNu64":%2"SCNu64":%lf", &hh, &mm, &ss ) != 3 )
        return -1;
    /* check overflow */
    if( hh >= 5124095 || mm >= 60 || ss >= 60 )
        return -1;
    /* 1ns timescale */
    data->start_time = (hh * 3600 + mm * 60 + ss) * 1e9;
    return 0;
}

static int isom_lumber_line( char *buff, int bufsize, FILE *chapter  )
{
    char *tail;
    /* remove newline codes and skip empty line */
    do{
        if( fgets( buff, bufsize, chapter ) == NULL )
            return -1;
        tail = &buff[ strlen( buff ) - 1 ];
        while( tail >= buff && ( *tail == '\n' || *tail == '\r' ) )
            *tail-- = '\0';
    }while( tail < buff );
    return 0;
}

static int isom_read_simple_chapter( FILE *chapter, isom_chapter_entry_t *data )
{
    char buff[CHAPTER_BUFSIZE];
    int len;

    /* get start_time */
    if( isom_lumber_line( buff, CHAPTER_BUFSIZE, chapter ) )
        return -1;
    char *chapter_time = strchr( buff, '=' );   /* find separator */
    if( !chapter_time++
        || isom_get_start_time( chapter_time, data )
        || isom_lumber_line( buff, CHAPTER_BUFSIZE, chapter ) ) /* get chapter_name */
        return -1;
    char *chapter_name = strchr( buff, '=' );   /* find separator */
    if( !chapter_name++ )
        return -1;
    len = LSMASH_MIN( 255, strlen( chapter_name ) );  /* We support length of chapter_name up to 255 */
    data->chapter_name = ( char* )malloc( len + 1 );
    if( !data->chapter_name )
        return -1;
    memcpy( data->chapter_name, chapter_name, len );
    data->chapter_name[len] = '\0';
    return 0;
}

static int isom_read_minimum_chapter( FILE *chapter, isom_chapter_entry_t *data )
{
    char buff[CHAPTER_BUFSIZE];
    int len;

    if( isom_lumber_line( buff, CHAPTER_BUFSIZE, chapter ) /* read newline */
        || isom_get_start_time( buff, data ) ) /* get start_time */
        return -1;
    /* get chapter_name */
    char *chapter_name = strchr( buff, ' ' );   /* find separator */
    if( !chapter_name++ )
        return -1;
    len = LSMASH_MIN( 255, strlen( chapter_name ) );  /* We support length of chapter_name up to 255 */
    data->chapter_name = ( char* )malloc( len + 1 );
    if( !data->chapter_name )
        return -1;
    memcpy( data->chapter_name, chapter_name, len );
    data->chapter_name[len] = '\0';
    return 0;
}

typedef int (*fn_get_chapter_data)( FILE *, isom_chapter_entry_t * );

static fn_get_chapter_data isom_check_chap_line( char *file_name )
{
    char buff[CHAPTER_BUFSIZE];
    FILE *fp = fopen( file_name, "rb" );
    if( !fp )
        return NULL;
    fn_get_chapter_data fnc = NULL;
    if( fgets( buff, CHAPTER_BUFSIZE, fp ) != NULL )
    {
        if( strncmp( buff, "CHAPTER", 7 ) == 0 )
            fnc = isom_read_simple_chapter;
        else if( isdigit( buff[0] ) && isdigit( buff[1] ) && buff[2] == ':'
             && isdigit( buff[3] ) && isdigit( buff[4] ) && buff[5] == ':' )
            fnc = isom_read_minimum_chapter;
    }
    fclose( fp );
    return fnc;
}

int lsmash_set_tyrant_chapter( lsmash_root_t *root, char *file_name )
{
    /* This function should be called after updating of the latest movie duration. */
    if( !root || !root->moov || !root->moov->mvhd || !root->moov->mvhd->timescale || !root->moov->mvhd->duration )
        return -1;
    /* check each line format */
    fn_get_chapter_data fnc = isom_check_chap_line( file_name );
    if( !fnc )
        return -1;
    FILE *chapter = fopen( file_name, "rb" );
    if( !chapter )
        return -1;
    if( isom_add_udta( root, 0 ) || isom_add_chpl( root->moov ) )
        goto fail;
    isom_chapter_entry_t data;
    while( !fnc( chapter, &data ) )
    {
        data.start_time = (data.start_time + 50) / 100;    /* convert to 100ns unit */
        if( data.start_time / 1e7 > (double)root->moov->mvhd->duration / root->moov->mvhd->timescale
            || isom_add_chpl_entry( root->moov->udta->chpl, &data ) )
            goto fail;
        free( data.chapter_name );
        data.chapter_name = NULL;
    }
    fclose( chapter );
    return 0;
fail:
    if( data.chapter_name )
        free( data.chapter_name );
    fclose( chapter );
    return -1;
}

int lsmash_create_reference_chapter_track( lsmash_root_t *root, uint32_t track_ID, char *file_name )
{
    if( !root || (!root->qt_compatible && !root->itunes_audio) || !root->moov || !root->moov->mvhd )
        return -1;
    FILE *chapter = NULL;       /* shut up 'uninitialized' warning */
    /* Create a Track Reference Box. */
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || isom_add_tref( trak ) )
        return -1;
    /* Create a track_ID for a new chapter track. */
    uint32_t *id = (uint32_t *)malloc( sizeof(uint32_t) );
    if( !id )
        return -1;
    uint32_t chapter_track_ID = *id = root->moov->mvhd->next_track_ID;
    /* Create a Track Reference Type Box. */
    isom_tref_type_t *chap = isom_add_track_reference_type( trak->tref, QT_TREF_TYPE_CHAP, 1, id );
    if( !chap )
        return -1;      /* no need to free id */
    /* Create a reference chapter track. */
    if( chapter_track_ID != lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_TEXT_TRACK ) )
        return -1;
    /* Set track parameters. */
    lsmash_track_parameters_t track_param;
    lsmash_initialize_track_parameters( &track_param );
    track_param.mode = ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
    if( lsmash_set_track_parameters( root, chapter_track_ID, &track_param ) )
        goto fail;
    /* Set media parameters. */
    uint64_t media_timescale = lsmash_get_media_timescale( root, track_ID );
    if( !media_timescale )
        goto fail;
    lsmash_media_parameters_t media_param;
    lsmash_initialize_media_parameters( &media_param );
    media_param.timescale = media_timescale;
    media_param.ISO_language = root->max_3gpp_version >= 6 || root->itunes_audio ? "und" : NULL;
    media_param.MAC_language = 0;
    if( lsmash_set_media_parameters( root, chapter_track_ID, &media_param ) )
        goto fail;
    /* Create a sample description. */
    uint32_t sample_type = root->max_3gpp_version >= 6 || root->itunes_audio ? ISOM_CODEC_TYPE_TX3G_TEXT : QT_CODEC_TYPE_TEXT_TEXT;
    uint32_t sample_entry = lsmash_add_sample_entry( root, chapter_track_ID, sample_type, NULL );
    if( !sample_entry )
        goto fail;
    /* Check each line format. */
    fn_get_chapter_data fnc = isom_check_chap_line( file_name );
    if( !fnc )
        return -1;
    /* Open chapter format file. */
    chapter = fopen( file_name, "rb" );
    if( !chapter )
        goto fail;
    /* Parse the file and write text samples. */
    isom_chapter_entry_t data;
    while( !fnc( chapter, &data ) )
    {
        /* set start_time */
        data.start_time = data.start_time * 1e-9 * media_timescale + 0.5;
        /* write a text sample here */
        uint16_t name_length = strlen( data.chapter_name );
        lsmash_sample_t *sample = lsmash_create_sample( 2 + name_length + 12 * (sample_type == QT_CODEC_TYPE_TEXT_TEXT) );
        if( !sample )
            goto fail;
        sample->data[0] = (name_length >> 8) & 0xff;
        sample->data[1] =  name_length       & 0xff;
        memcpy( sample->data + 2, data.chapter_name, name_length );
        if( sample_type == QT_CODEC_TYPE_TEXT_TEXT )
        {
            /* QuickTime Player requires Text Encoding Attribute Box ('encd') if media language is ISO language codes : undefined.
             * Also this box can avoid garbling if the QuickTime text sample is encoded by Unicode characters.
             * Note: 3GPP Timed Text supports only UTF-8 or UTF-16, so this box isn't needed. */
            static const uint8_t encd[12] = { 0x00, 0x00, 0x00, 0x0C,       /* size: 12 */
                                              0x65, 0x6E, 0x63, 0x64,       /* type: 'encd' */
                                              0x00, 0x00, 0x01, 0x00 };     /* Unicode Encoding */
            memcpy( sample->data + 2 + name_length, encd, 12 );
        }
        sample->dts = sample->cts = data.start_time;
        sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
        sample->index = sample_entry;
        if( lsmash_append_sample( root, chapter_track_ID, sample ) )
            goto fail;
        free( data.chapter_name );
        data.chapter_name = NULL;
    }
    if( lsmash_flush_pooled_samples( root, chapter_track_ID, 0 ) )
        goto fail;
    trak = isom_get_trak( root, chapter_track_ID );
    if( !trak )
        goto fail;
    fclose( chapter );
    trak->is_chapter = 1;
    trak->related_track_ID = track_ID;
    return 0;
fail:
    if( chapter )
        fclose( chapter );
    if( data.chapter_name )
        free( data.chapter_name );
    free( chap->track_ID );
    chap->track_ID = NULL;
    /* Remove the reference chapter track attached at tail of the list. */
    lsmash_remove_entry_direct( root->moov->trak_list, root->moov->trak_list->tail, isom_remove_trak );
    return -1;
}

void lsmash_delete_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak )
        return;
    isom_remove_edts( trak->edts );
    trak->edts = NULL;
}

void lsmash_delete_tyrant_chapter( lsmash_root_t *root )
{
    if( !root || !root->moov || !root->moov->udta )
        return;
    isom_remove_chpl( root->moov->udta->chpl );
    root->moov->udta->chpl = NULL;
}
