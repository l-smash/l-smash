/*****************************************************************************
 * isom.c:
 *****************************************************************************
 * Copyright (C) 2010-2012 L-SMASH project
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

#include "box.h"
#include "isom.h"
#include "mp4a.h"
#include "mp4sys.h"
#include "write.h"
#ifdef LSMASH_DEMUXER_ENABLED
#include "read.h"
#include "print.h"
#include "timeline.h"
#endif


/*---- ----*/
/* Return 1 if the box is fullbox, Otherwise return 0. */
int isom_is_fullbox( void *box )
{
    isom_box_t *current = (isom_box_t *)box;
    uint32_t type = current->type;
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
        ISOM_BOX_TYPE_META,
        QT_BOX_TYPE_KEYS,
        ISOM_BOX_TYPE_MEAN,
        ISOM_BOX_TYPE_NAME,
        ISOM_BOX_TYPE_MEHD,
        ISOM_BOX_TYPE_TREX,
        ISOM_BOX_TYPE_MFHD,
        ISOM_BOX_TYPE_TFHD,
        ISOM_BOX_TYPE_TRUN,
        ISOM_BOX_TYPE_TFRA,
        ISOM_BOX_TYPE_MFRO,
        0
    };
    for( int i = 0; fullbox_table[i]; i++ )
        if( type == fullbox_table[i] )
            return 1;
    return (type == ISOM_BOX_TYPE_CPRT) && current->parent && (current->parent->type == ISOM_BOX_TYPE_UDTA);
}

/* Return 1 if the sample type is LPCM audio, Otherwise return 0. */
int isom_is_lpcm_audio( void *box )
{
    isom_box_t *current = (isom_box_t *)box;
    uint32_t type = current->type;
    return type == QT_CODEC_TYPE_23NI_AUDIO
        || type == QT_CODEC_TYPE_NONE_AUDIO
        || type == QT_CODEC_TYPE_LPCM_AUDIO
        || type == QT_CODEC_TYPE_SOWT_AUDIO
        || type == QT_CODEC_TYPE_TWOS_AUDIO
        || type == QT_CODEC_TYPE_FL32_AUDIO
        || type == QT_CODEC_TYPE_FL64_AUDIO
        || type == QT_CODEC_TYPE_IN24_AUDIO
        || type == QT_CODEC_TYPE_IN32_AUDIO
        || type == QT_CODEC_TYPE_NOT_SPECIFIED
        || (type == QT_CODEC_TYPE_RAW_AUDIO && (current->manager & LSMASH_AUDIO_DESCRIPTION));
}

/* Return 1 if the sample type is uncompressed Y'CbCr video, Otherwise return 0. */
int isom_is_uncompressed_ycbcr( uint32_t type )
{
    return type == QT_CODEC_TYPE_V210_VIDEO
        || type == QT_CODEC_TYPE_V216_VIDEO
        || type == QT_CODEC_TYPE_V308_VIDEO
        || type == QT_CODEC_TYPE_V408_VIDEO
        || type == QT_CODEC_TYPE_V410_VIDEO
        || type == QT_CODEC_TYPE_YUV2_VIDEO;
}

char *isom_4cc2str( uint32_t fourcc )
{
    static char str[5];
    str[0] = (fourcc >> 24) & 0xff;
    str[1] = (fourcc >> 16) & 0xff;
    str[2] = (fourcc >>  8) & 0xff;
    str[3] =  fourcc        & 0xff;
    str[4] = 0;
    return str;
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

void isom_init_box_common( void *box, void *parent, uint32_t type )
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

isom_trak_entry_t *isom_get_trak( lsmash_root_t *root, uint32_t track_ID )
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
    if( data->segment_duration > UINT32_MAX || data->media_time > INT32_MAX || data->media_time < INT32_MIN )
        elst->version = 1;
    return 0;
}

isom_tref_type_t *isom_add_track_reference_type( isom_tref_t *tref, isom_track_reference_type type, uint32_t ref_count, uint32_t *track_ID )
{
    if( !tref || !tref->ref_list )
        return NULL;
    isom_tref_type_t *ref = malloc( sizeof(isom_tref_type_t) );
    if( !ref )
        return NULL;
    isom_init_basebox_common( (isom_box_t *)ref, (isom_box_t *)tref, type );
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
    isom_dref_entry_t *data = lsmash_malloc_zero( sizeof(isom_dref_entry_t) );
    if( !data )
        return -1;
    isom_init_box_common( data, dref, name ? ISOM_BOX_TYPE_URN : ISOM_BOX_TYPE_URL );
    data->flags = flags;
    if( location )
    {
        data->location_length = strlen( location ) + 1;
        data->location = lsmash_memdup( location, data->location_length );
        if( !data->location )
        {
            free( data );
            return -1;
        }
    }
    if( name )
    {
        data->name_length = strlen( name ) + 1;
        data->name = lsmash_memdup( name, data->name_length );
        if( !data->name )
        {
            if( data->location )
                free( data->location );
            free( data );
            return -1;
        }
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

isom_avcC_ps_entry_t *isom_create_ps_entry( uint8_t *ps, uint32_t ps_size )
{
    isom_avcC_ps_entry_t *entry = malloc( sizeof(isom_avcC_ps_entry_t) );
    if( !entry )
        return NULL;
    entry->parameterSetNALUnit = lsmash_memdup( ps, ps_size );
    if( !entry->parameterSetNALUnit )
    {
        free( entry );
        return NULL;
    }
    entry->parameterSetLength = ps_size;
    return entry;
}

void isom_remove_avcC_ps( isom_avcC_ps_entry_t *ps )
{
    if( !ps )
        return;
    if( ps->parameterSetNALUnit )
        free( ps->parameterSetNALUnit );
    free( ps );
}

int isom_add_avcC( isom_visual_entry_t *visual )
{
    if( !visual )
        return -1;
    isom_create_box( avcC, visual, ISOM_BOX_TYPE_AVCC );
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
    visual->avcC = avcC;
    return 0;
}

int isom_add_clap( isom_visual_entry_t *visual )
{
    if( !visual || visual->clap )
        return -1;
    isom_create_box( clap, visual, ISOM_BOX_TYPE_CLAP );
    clap->cleanApertureWidthN = visual->width;
    clap->cleanApertureWidthD = 1;
    clap->cleanApertureHeightN = visual->height;
    clap->cleanApertureHeightD = 1;
    clap->horizOffN = 0;
    clap->horizOffD = 1;
    clap->vertOffN = 0;
    clap->vertOffD = 1;
    visual->clap = clap;
    return 0;
}

int isom_add_pasp( isom_visual_entry_t *visual )
{
    if( !visual || visual->pasp )
        return -1;
    isom_create_box( pasp, visual, ISOM_BOX_TYPE_PASP );
    pasp->hSpacing = 1;
    pasp->vSpacing = 1;
    visual->pasp = pasp;
    return 0;
}

int isom_add_glbl( isom_visual_entry_t *visual )
{
    if( !visual || visual->glbl )
        return -1;
    isom_create_box( glbl, visual, QT_BOX_TYPE_GLBL );
    visual->glbl = glbl;
    return 0;
}

int isom_add_colr( isom_visual_entry_t *visual )
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

int isom_add_gama( isom_visual_entry_t *visual )
{
    if( !visual || visual->gama )
        return -1;
    isom_create_box( gama, visual, QT_BOX_TYPE_GAMA );
    gama->level = 0;    /* platform's standard gamma */
    visual->gama = gama;
    return 0;
}

int isom_add_fiel( isom_visual_entry_t *visual )
{
    if( !visual || visual->fiel )
        return -1;
    isom_create_box( fiel, visual, QT_BOX_TYPE_FIEL );
    fiel->fields = 1;
    visual->fiel = fiel;
    return 0;
}

int isom_add_cspc( isom_visual_entry_t *visual )
{
    if( !visual || visual->cspc )
        return -1;
    isom_create_box( cspc, visual, QT_BOX_TYPE_CSPC );
    visual->cspc = cspc;
    return 0;
}

int isom_add_sgbt( isom_visual_entry_t *visual )
{
    if( !visual || visual->sgbt )
        return -1;
    isom_create_box( sgbt, visual, QT_BOX_TYPE_SGBT );
    sgbt->significantBits = 8;
    visual->sgbt = sgbt;
    return 0;
}

int isom_add_stsl( isom_visual_entry_t *visual )
{
    if( !visual || visual->stsl )
        return -1;
    isom_create_box( stsl, visual, ISOM_BOX_TYPE_STSL );
    stsl->scale_method = ISOM_SCALING_METHOD_HIDDEN;
    visual->stsl = stsl;
    return 0;
}

static void isom_remove_esds( isom_esds_t *esds );
static void isom_remove_visual_extensions( isom_visual_entry_t *visual );

static int isom_add_visual_extensions( isom_visual_entry_t *visual, lsmash_video_summary_t *summary )
{
    /* Check if set up Track Aperture Modes. */
    isom_trak_entry_t *trak = (isom_trak_entry_t *)visual->parent->parent->parent->parent->parent;
    int qt_compatible = trak->root->qt_compatible;
    isom_tapt_t *tapt = trak->tapt;
    int set_aperture_modes = qt_compatible                      /* Track Aperture Modes is only available under QuickTime file format. */
        && !summary->scaling_method                             /* Sample scaling method might conflict with this feature. */
        && tapt && tapt->clef && tapt->prof && tapt->enof       /* Check if required boxes exist. */
        && !((isom_stsd_t *)visual->parent)->list->entry_count; /* Multiple sample description might conflict with this, so in that case, disable this feature.
                                                                 * Note: this sample description isn't added yet here. */
    if( !set_aperture_modes )
        isom_remove_tapt( trak->tapt );
    int uncompressed_ycbcr = qt_compatible && isom_is_uncompressed_ycbcr( visual->type );
    /* Set up Clean Aperture. */
    if( set_aperture_modes || uncompressed_ycbcr || summary->crop_top || summary->crop_left || summary->crop_bottom || summary->crop_right )
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
    /* Set up Pixel Aspect Ratio. */
    if( set_aperture_modes || (summary->par_h && summary->par_v) )
    {
        if( isom_add_pasp( visual ) )
        {
            isom_remove_visual_extensions( visual );
            return -1;
        }
        isom_pasp_t *pasp = visual->pasp;
        pasp->hSpacing = LSMASH_MAX( summary->par_h, 1 );
        pasp->vSpacing = LSMASH_MAX( summary->par_v, 1 );
    }
    /* Set up Color Parameter. */
    if( qt_compatible && (uncompressed_ycbcr || summary->primaries || summary->transfer || summary->matrix) )
    {
        if( isom_add_colr( visual ) )
        {
            isom_remove_visual_extensions( visual );
            return -1;
        }
        isom_colr_t *colr = visual->colr;
        uint16_t primaries = summary->primaries;
        uint16_t transfer  = summary->transfer;
        uint16_t matrix    = summary->matrix;
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
            colr->matrix_index = (matrix == 1 || matrix == 6 || matrix == 7) ? matrix : 2;
    }
    /* Set up Field/Frame Information. */
    if( qt_compatible && (uncompressed_ycbcr || summary->field_orderings != QT_FIELD_ORDERINGS_PROGRESSIVE) )
    {
        if( isom_add_fiel( visual ) )
        {
            isom_remove_visual_extensions( visual );
            return -1;
        }
        visual->fiel->fields = summary->field_orderings == QT_FIELD_ORDERINGS_PROGRESSIVE ? 1 : 2;
        visual->fiel->detail = summary->field_orderings;
    }
    /* Set up the pixel format type. */
    if( qt_compatible && summary->pixel_format )
    {
        if( isom_add_cspc( visual ) )
        {
            isom_remove_visual_extensions( visual );
            return -1;
        }
        visual->cspc->pixel_format = summary->pixel_format;
    }
    /* Set up the number of significant bits per component. */
    if( qt_compatible && (visual->type == QT_CODEC_TYPE_V216_VIDEO || summary->significant_bits) )
    {
        if( summary->significant_bits == 0 || isom_add_sgbt( visual ) )
        {
            isom_remove_visual_extensions( visual );
            return -1;
        }
        visual->sgbt->significantBits = summary->significant_bits;
    }
    /* Set up Sample Scaling. */
    if( !qt_compatible && summary->scaling_method )
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
    /* Set up Decoder Specific Information. */
    static const struct
    {
        uint32_t codec_type;
        uint32_t minimum_length;
        uint32_t fourcc;
        int (*add_func)( isom_visual_entry_t * );
    } dsi_table[] =
        {
            { ISOM_CODEC_TYPE_AVC1_VIDEO, 15, ISOM_BOX_TYPE_AVCC, isom_add_avcC },
            { ISOM_CODEC_TYPE_AVC2_VIDEO, 15, ISOM_BOX_TYPE_AVCC, isom_add_avcC },
            { ISOM_CODEC_TYPE_AVCP_VIDEO, 15, ISOM_BOX_TYPE_AVCC, isom_add_avcC },
            { ISOM_CODEC_TYPE_VC_1_VIDEO, 11, ISOM_BOX_TYPE_DVC1, NULL },
            { 0 }
        };
    for( int i = 0; dsi_table[i].codec_type; i++ )
        if( visual->type == dsi_table[i].codec_type )
        {
            if( summary->exdata_length >= dsi_table[i].minimum_length )
            {
                /* Chech if Decoder Specific Information is constructed as exdata. */
                uint8_t *exdata = (uint8_t *)summary->exdata;
                uint32_t length = (exdata[0] << 24) | (exdata[1] << 16) | (exdata[2] << 8) | exdata[3];
                if( length == summary->exdata_length
                 && LSMASH_4CC( exdata[4], exdata[5], exdata[6], exdata[7] ) == dsi_table[i].fourcc )
                {
                    visual->exdata = lsmash_memdup( summary->exdata, summary->exdata_length );
                    if( !visual->exdata )
                        return -1;
                    visual->exdata_length = summary->exdata_length;
                    break;
                }
            }
            if( dsi_table[i].add_func && dsi_table[i].add_func( visual ) )
                return -1;
            break;
        }
    /* Set up Track Apeture Modes. */
    if( set_aperture_modes )
    {
        uint32_t width  = visual->width  << 16;
        uint32_t height = visual->height << 16;
        double clap_width  = ((double)visual->clap->cleanApertureWidthN  / visual->clap->cleanApertureWidthD)  * (1<<16);
        double clap_height = ((double)visual->clap->cleanApertureHeightN / visual->clap->cleanApertureHeightD) * (1<<16);
        double par = (double)visual->pasp->hSpacing / visual->pasp->vSpacing;
        if( par >= 1.0 )
        {
            tapt->clef->width  = clap_width * par;
            tapt->clef->height = clap_height;
            tapt->prof->width  = width * par;
            tapt->prof->height = height;
        }
        else
        {
            tapt->clef->width  = clap_width;
            tapt->clef->height = clap_height / par;
            tapt->prof->width  = width;
            tapt->prof->height = height / par;
        }
        tapt->enof->width  = width;
        tapt->enof->height = height;
    }
    return 0;
}

static int isom_add_visual_entry( isom_stsd_t *stsd, uint32_t sample_type, lsmash_video_summary_t *summary )
{
    if( !stsd || !stsd->list || !summary )
        return -1;
    lsmash_entry_list_t *list = stsd->list;
    isom_visual_entry_t *visual = lsmash_malloc_zero( sizeof(isom_visual_entry_t) );
    if( !visual )
        return -1;
    isom_init_box_common( visual, stsd, sample_type );
    visual->manager |= LSMASH_VIDEO_DESCRIPTION;
    visual->data_reference_index = 1;
    visual->width = (uint16_t)summary->width;
    visual->height = (uint16_t)summary->height;
    visual->horizresolution = visual->vertresolution = 0x00480000;
    visual->frame_count = 1;
    switch( sample_type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
        case ISOM_CODEC_TYPE_AVC2_VIDEO :
            strcpy( visual->compressorname, "\012AVC Coding" );
            break;
        case ISOM_CODEC_TYPE_AVCP_VIDEO :
            strcpy( visual->compressorname, "\016AVC Parameters" );
            break;
        default :
            break;
    }
    visual->depth = 0x0018;
    visual->color_table_ID = -1;
    if( isom_add_visual_extensions( visual, summary )
     || lsmash_add_entry( list, visual ) )
    {
        isom_remove_visual_extensions( visual );
        free( visual );
        return -1;
    }
    return 0;
}

#if 0
static int isom_add_mp4s_entry( isom_stsd_t *stsd )
{
    if( !stsd || !stsd->list )
        return -1;
    isom_mp4s_entry_t *mp4s = lsmash_malloc_zero( sizeof(isom_mp4s_entry_t) );
    if( !mp4s )
        return -1;
    isom_init_box_common( mp4s, stsd, ISOM_CODEC_TYPE_MP4S_SYSTEM );
    mp4s->data_reference_index = 1;
    if( lsmash_add_entry( stsd->list, mp4s ) )
    {
        free( mp4s );
        return -1;
    }
    return 0;
}
#endif

int isom_add_wave( isom_audio_entry_t *audio )
{
    if( !audio || audio->wave )
        return -1;
    isom_create_box( wave, audio, QT_BOX_TYPE_WAVE );
    audio->wave = wave;
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

int isom_add_chan( isom_audio_entry_t *audio )
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
    isom_esds_t *esds = lsmash_malloc_zero( sizeof(isom_esds_t) );
    if( !esds )
        return -1;
    isom_init_box_common( esds, audio->wave, ISOM_BOX_TYPE_ESDS );
    mp4sys_ES_Descriptor_params_t esd_param;
    memset( &esd_param, 0, sizeof(mp4sys_ES_Descriptor_params_t) );
    esd_param.objectTypeIndication = summary->object_type_indication;
    esd_param.streamType = summary->stream_type;
    esd_param.dsi_payload = summary->exdata;
    esd_param.dsi_payload_length = summary->exdata_length;
    esds->ES = mp4sys_setup_ES_Descriptor( &esd_param );
    if( !esds->ES )
    {
        free( esds );
        return -1;
    }
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
    if( audio->type == ISOM_CODEC_TYPE_DTSC_AUDIO
     || audio->type == ISOM_CODEC_TYPE_DTSE_AUDIO
     || audio->type == ISOM_CODEC_TYPE_DTSH_AUDIO
     || audio->type == ISOM_CODEC_TYPE_DTSL_AUDIO )
    {
        switch( summary->frequency )
        {
            case 12000 :    /* Invalid? (No reference in the spec) */
            case 24000 :
            case 48000 :
            case 96000 :
            case 192000 :
            case 384000 :   /* Invalid? (No reference in the spec) */
                audio->samplerate = 48000 << 16;
                break;
            case 22050 :
            case 44100 :
            case 88200 :
            case 176400 :
            case 352800 :   /* Invalid? (No reference in the spec) */
                audio->samplerate = 44100 << 16;
                break;
            case 8000 :     /* Invalid? (No reference in the spec) */
            case 16000 :
            case 32000 :
            case 64000 :
            case 128000 :
                audio->samplerate = 32000 << 16;
                break;
            default :
                audio->samplerate = 0;
                break;
        }
        audio->channelcount = summary->channels;
    }
    else
    {
        audio->samplerate = summary->frequency <= UINT16_MAX ? summary->frequency << 16 : 0;
        audio->channelcount = 2;
    }
    audio->samplesize = 16;
    if( summary->exdata )
    {
        audio->exdata = lsmash_memdup( summary->exdata, summary->exdata_length );
        if( !audio->exdata )
            return -1;
        audio->exdata_length = summary->exdata_length;
    }
    else
    {
        /* No CODEC Specific Info */
        switch( audio->type )
        {
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_DTSC_AUDIO :
            case ISOM_CODEC_TYPE_DTSE_AUDIO :
            case ISOM_CODEC_TYPE_DTSH_AUDIO :
            case ISOM_CODEC_TYPE_DTSL_AUDIO :
            case ISOM_CODEC_TYPE_EC_3_AUDIO :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
                return -1;
            default :
                break;
        }
        audio->exdata = NULL;
        audio->exdata_length = 0;
    }
    return 0;
}

static int isom_add_audio_entry( isom_stsd_t *stsd, uint32_t sample_type, lsmash_audio_summary_t *summary )
{
    if( !stsd || !stsd->list || !summary )
        return -1;
    isom_audio_entry_t *audio = lsmash_malloc_zero( sizeof(isom_audio_entry_t) );
    if( !audio )
        return -1;
    isom_init_box_common( audio, stsd, sample_type );
    audio->manager |= LSMASH_AUDIO_DESCRIPTION;
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
    else if( isom_is_lpcm_audio( audio ) )
        ret = isom_set_qtff_lpcm_description( audio );
    else
        ret = isom_set_extra_description( audio );
    if( ret )
        goto fail;
    if( root->qt_compatible )
    {
        lsmash_channel_layout_tag layout_tag = summary->layout_tag;
        lsmash_channel_bitmap bitmap = summary->bitmap;
        if( layout_tag == QT_CHANNEL_LAYOUT_USE_CHANNEL_DESCRIPTIONS    /* We don't support the feature of Channel Descriptions. */
         || (layout_tag == QT_CHANNEL_LAYOUT_USE_CHANNEL_BITMAP && (!bitmap || bitmap > QT_CHANNEL_BIT_FULL)) )
        {
            layout_tag = summary->layout_tag = QT_CHANNEL_LAYOUT_UNKNOWN | summary->channels;
            bitmap = summary->bitmap = 0;
        }
        /* Don't create Audio Channel Layout Box if the channel layout is unknown. */
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
    isom_text_entry_t *text = lsmash_malloc_zero( sizeof(isom_text_entry_t) );
    if( !text )
        return -1;
    isom_init_box_common( text, stsd, QT_CODEC_TYPE_TEXT_TEXT );
    text->data_reference_index = 1;
    if( lsmash_add_entry( stsd->list, text ) )
    {
        free( text );
        return -1;
    }
    return 0;
}

int isom_add_ftab( isom_tx3g_entry_t *tx3g )
{
    if( !tx3g )
        return -1;
    isom_ftab_t *ftab = lsmash_malloc_zero( sizeof(isom_ftab_t) );
    if( !ftab )
        return -1;
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
    isom_tx3g_entry_t *tx3g = lsmash_malloc_zero( sizeof(isom_tx3g_entry_t) );
    if( !tx3g )
        return -1;
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
#endif
            ret = isom_add_visual_entry( stsd, sample_type, (lsmash_video_summary_t *)summary );
            break;
#if 0
        case ISOM_CODEC_TYPE_MP4S_SYSTEM :
            ret = isom_add_mp4s_entry( stsd );
            break;
#endif
        case ISOM_CODEC_TYPE_MP4A_AUDIO :
        case ISOM_CODEC_TYPE_AC_3_AUDIO :
        case ISOM_CODEC_TYPE_ALAC_AUDIO :
        case ISOM_CODEC_TYPE_EC_3_AUDIO :
        case ISOM_CODEC_TYPE_SAMR_AUDIO :
        case ISOM_CODEC_TYPE_SAWB_AUDIO :
        case ISOM_CODEC_TYPE_DTSC_AUDIO :
        case ISOM_CODEC_TYPE_DTSE_AUDIO :
        case ISOM_CODEC_TYPE_DTSH_AUDIO :
        case ISOM_CODEC_TYPE_DTSL_AUDIO :
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
        case LSMASH_CODEC_TYPE_RAW :
            if( trak->mdia->minf->vmhd )
                ret = isom_add_visual_entry( stsd, sample_type, (lsmash_video_summary_t *)summary );
            else if( trak->mdia->minf->smhd )
                ret = isom_add_audio_entry( stsd, sample_type, (lsmash_audio_summary_t *)summary );
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

static int isom_add_sdtp_entry( isom_box_t *parent, lsmash_sample_property_t *prop, uint8_t avc_extensions )
{
    if( !prop || !parent )
        return -1;
    isom_sdtp_t *sdtp = NULL;
    if( parent->type == ISOM_BOX_TYPE_STBL )
        sdtp = ((isom_stbl_t *)parent)->sdtp;
    else if( parent->type == ISOM_BOX_TYPE_TRAF )
        sdtp = ((isom_traf_entry_t *)parent)->sdtp;
    else
        assert( 0 );
    if( !sdtp || !sdtp->list )
        return -1;
    isom_sdtp_entry_t *data = malloc( sizeof(isom_sdtp_entry_t) );
    if( !data )
        return -1;
    /* isom_sdtp_entry_t is smaller than lsmash_sample_property_t. */
    data->is_leading            = (avc_extensions ? prop->leading : prop->allow_earlier) & 0x03;
    data->sample_depends_on     = prop->independent & 0x03;
    data->sample_is_depended_on = prop->disposable & 0x03;
    data->sample_has_redundancy = prop->redundant & 0x03;
    if( lsmash_add_entry( sdtp->list, data ) )
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

isom_sgpd_entry_t *isom_get_sample_group_description( isom_stbl_t *stbl, uint32_t grouping_type )
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

isom_sbgp_entry_t *isom_get_sample_to_group( isom_stbl_t *stbl, uint32_t grouping_type )
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
    data->description_length = 0;
    data->num_leading_samples_known = 0;
    data->num_leading_samples = 0;
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

int isom_add_chpl_entry( isom_chpl_t *chpl, isom_chapter_entry_t *chap_data )
{
    if( !chap_data->chapter_name || !chpl || !chpl->list )
        return -1;
    isom_chpl_entry_t *data = malloc( sizeof(isom_chpl_entry_t) );
    if( !data )
        return -1;
    data->start_time = chap_data->start_time;
    data->chapter_name_length = strlen( chap_data->chapter_name );
    data->chapter_name = (char *)malloc( data->chapter_name_length + 1 );
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
    isom_trex_entry_t *trex = lsmash_malloc_zero( sizeof(isom_trex_entry_t) );
    if( !trex )
        return NULL;
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
    isom_trun_entry_t *trun = lsmash_malloc_zero( sizeof(isom_trun_entry_t) );
    if( !trun )
        return NULL;
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
    isom_traf_entry_t *traf = lsmash_malloc_zero( sizeof(isom_traf_entry_t) );
    if( !traf )
        return NULL;
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
    isom_moof_entry_t *moof = lsmash_malloc_zero( sizeof(isom_moof_entry_t) );
    if( !moof )
        return NULL;
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
    isom_tfra_entry_t *tfra = lsmash_malloc_zero( sizeof(isom_tfra_entry_t) );
    if( !tfra )
        return NULL;
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
    ftyp->size = ISOM_BASEBOX_COMMON_SIZE + 8;
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
            case ISOM_CODEC_TYPE_VC_1_VIDEO :
                *visual_pli = MP4SYS_VISUAL_PLI_NOT_SPECIFIED;
                break;
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
            {
                isom_audio_entry_t *audio = (isom_audio_entry_t *)sample_entry;
#ifdef LSMASH_DEMUXER_ENABLED
                if( !audio->esds || !audio->esds->ES )
                    return -1;
                if( audio->summary.sample_type != ISOM_CODEC_TYPE_MP4A_AUDIO )
                    /* This is needed when copying descriptions. */
                    mp4sys_setup_summary_from_DecoderSpecificInfo( &audio->summary, audio->esds->ES );
#endif
                *audio_pli = mp4a_max_audioProfileLevelIndication( *audio_pli, mp4a_get_audioProfileLevelIndication( &audio->summary ) );
                break;
            }
#if 0
            case ISOM_CODEC_TYPE_DRAC_VIDEO :
            case ISOM_CODEC_TYPE_ENCV_VIDEO :
            case ISOM_CODEC_TYPE_MJP2_VIDEO :
            case ISOM_CODEC_TYPE_S263_VIDEO :
                /* FIXME: Do we have to arbitrate like audio? */
                if( *visual_pli == MP4SYS_VISUAL_PLI_NONE_REQUIRED )
                    *visual_pli = MP4SYS_VISUAL_PLI_NOT_SPECIFIED;
                break;
#endif
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_DTSC_AUDIO :
            case ISOM_CODEC_TYPE_DTSE_AUDIO :
            case ISOM_CODEC_TYPE_DTSH_AUDIO :
            case ISOM_CODEC_TYPE_DTSL_AUDIO :
            case ISOM_CODEC_TYPE_EC_3_AUDIO :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
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
        {
            free( iods );
            return -1;
        }
        if( isom_scan_trak_profileLevelIndication( trak, &audio_pli, &visual_pli ) )
        {
            free( iods );
            return -1;
        }
        if( mp4sys_add_ES_ID_Inc( iods->OD, trak->tkhd->track_ID ) )
        {
            free( iods );
            return -1;
        }
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
        if( handler_type == ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK )
            tkhd->volume = 0x0100;
        tkhd->matrix[0] = 0x00010000;
        tkhd->matrix[4] = 0x00010000;
        tkhd->matrix[8] = 0x40000000;
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

int isom_add_elst( isom_edts_t *edts )
{
    if( edts->elst )
        return 0;
    isom_create_list_box( elst, edts, ISOM_BOX_TYPE_ELST );
    edts->elst = elst;
    return 0;
}

int isom_add_edts( isom_trak_entry_t *trak )
{
    if( trak->edts )
        return 0;
    isom_create_box( edts, trak, ISOM_BOX_TYPE_EDTS );
    trak->edts = edts;
    return 0;
}

int isom_add_tref( isom_trak_entry_t *trak )
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

int isom_add_hdlr( isom_mdia_t *mdia, isom_meta_t *meta, isom_minf_t *minf, uint32_t media_type )
{
    if( (!mdia && !meta && !minf) || (mdia && meta) || (meta && minf) || (minf && mdia) )
        return -1;    /* Either one must be given. */
    if( (mdia && mdia->hdlr) || (meta && meta->hdlr) || (minf && minf->hdlr) )
        return -1;    /* Selected one must not have hdlr yet. */
    isom_box_t *parent = mdia ? (isom_box_t *)mdia : meta ? (isom_box_t *)meta : (isom_box_t *)minf;
    isom_create_box( hdlr, parent, ISOM_BOX_TYPE_HDLR );
    lsmash_root_t *root = hdlr->root;
    uint32_t type = mdia ? (root->qt_compatible ? QT_HANDLER_TYPE_MEDIA : 0) : (meta ? 0 : QT_HANDLER_TYPE_DATA);
    uint32_t subtype = media_type;
    hdlr->componentType = type;
    hdlr->componentSubtype = subtype;
    char *type_name = NULL;
    char *subtype_name = NULL;
    uint8_t type_name_length = 0;
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
    uint8_t *name = malloc( name_length );
    if( !name )
    {
        free( hdlr );
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
    hdlr->componentName = name;
    hdlr->componentName_length = name_length;
    if( mdia )
        mdia->hdlr = hdlr;
    else if( meta )
        meta->hdlr = hdlr;
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

int isom_add_btrt( isom_visual_entry_t *visual )
{
    if( !visual || visual->btrt )
        return -1;
    isom_create_box( btrt, visual, ISOM_BOX_TYPE_BTRT );
    visual->btrt = btrt;
    return 0;
}

int lsmash_add_btrt( lsmash_root_t *root, uint32_t track_ID, uint32_t entry_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_visual_entry_t *data = (isom_visual_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    return isom_add_btrt( data );
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

static int isom_add_sdtp( isom_box_t *parent )
{
    if( !parent )
        return -1;
    if( parent->type == ISOM_BOX_TYPE_STBL )
    {
        isom_stbl_t *stbl = (isom_stbl_t *)parent;
        if( stbl->sdtp )
            return -1;
        isom_create_list_box( sdtp, stbl, ISOM_BOX_TYPE_SDTP );
        stbl->sdtp = sdtp;
    }
    else if( parent->type == ISOM_BOX_TYPE_TRAF )
    {
        isom_traf_entry_t *traf = (isom_traf_entry_t *)parent;
        if( traf->sdtp )
            return -1;
        isom_create_list_box( sdtp, traf, ISOM_BOX_TYPE_SDTP );
        traf->sdtp = sdtp;
    }
    else
        assert( 0 );
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
    isom_sgpd_entry_t *sgpd = lsmash_malloc_zero( sizeof(isom_sgpd_entry_t) );
    if( !sgpd )
        return NULL;
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
    isom_sbgp_entry_t *sbgp = lsmash_malloc_zero( sizeof(isom_sbgp_entry_t) );
    if( !sbgp )
        return NULL;
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

int isom_add_chpl( isom_moov_t *moov )
{
    if( !moov || !moov->udta || moov->udta->chpl )
        return -1;
    isom_create_list_box( chpl, moov->udta, ISOM_BOX_TYPE_CHPL );
    chpl->version = 1;      /* version = 1 is popular. */
    moov->udta->chpl = chpl;
    return 0;
}

int isom_add_metaitem( isom_ilst_t *ilst, uint32_t type )
{
    if( !ilst || !ilst->item_list )
        return -1;
    isom_create_box( metaitem, ilst, type );
    if( lsmash_add_entry( ilst->item_list, metaitem ) )
    {
        free( metaitem );
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
    if( !moov || !moov->udta || !moov->udta->meta || moov->udta->meta->ilst )
        return -1;
    isom_create_box( ilst, moov->udta->meta, ISOM_BOX_TYPE_ILST );
    ilst->item_list = lsmash_create_entry_list();
    if( !ilst->item_list )
    {
        free( ilst );
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
    if( !parent->type )
    {
        lsmash_root_t *root = (lsmash_root_t *)parent;
        if( root->meta )
        {
            free( meta );
            return -1;
        }
        root->meta = meta;
    }
    else if( parent->type == ISOM_BOX_TYPE_MOOV )
    {
        isom_moov_t *moov = (isom_moov_t *)parent;
        if( moov->meta )
        {
            free( meta );
            return -1;
        }
        moov->meta = meta;
    }
    else if( parent->type == ISOM_BOX_TYPE_TRAK )
    {
        isom_trak_entry_t *trak = (isom_trak_entry_t *)parent;
        if( trak->meta )
        {
            free( meta );
            return -1;
        }
        trak->meta = meta;
    }
    else
    {
        isom_udta_t *udta = (isom_udta_t *)parent;
        if( udta->meta )
        {
            free( meta );
            return -1;
        }
        udta->meta = meta;
    }
    return 0;
}

static int isom_add_cprt( isom_udta_t *udta )
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
        free( cprt );
        return -1;
    }
    return 0;
}

int isom_add_udta( lsmash_root_t *root, uint32_t track_ID )
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
    isom_trak_entry_t *trak = lsmash_malloc_zero( sizeof(isom_trak_entry_t) );
    if( !trak )
        return NULL;
    isom_init_box_common( trak, moov, ISOM_BOX_TYPE_TRAK );
    isom_cache_t *cache = lsmash_malloc_zero( sizeof(isom_cache_t) );
    if( !cache )
    {
        free( trak );
        return NULL;
    }
    isom_fragment_t *fragment = NULL;
    if( root->fragment )
    {
        fragment = lsmash_malloc_zero( sizeof(isom_fragment_t) );
        if( !fragment )
        {
            free( cache );
            free( trak );
            return NULL;
        }
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

#define isom_remove_box( box_name, parent_type ) \
    do \
    { \
        parent_type *parent = (parent_type *)box_name->parent; \
        free( box_name ); \
        if( parent ) \
            parent->box_name = NULL; \
    } while( 0 )

static void isom_remove_ftyp( isom_ftyp_t *ftyp )
{
    if( !ftyp )
        return;
    if( ftyp->compatible_brands )
        free( ftyp->compatible_brands );
    isom_remove_box( ftyp, lsmash_root_t );
}

static void isom_remove_tkhd( isom_tkhd_t *tkhd )
{
    if( !tkhd )
        return;
    isom_remove_box( tkhd, isom_trak_entry_t );
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

void isom_remove_tapt( isom_tapt_t *tapt )
{
    if( !tapt )
        return;
    isom_remove_clef( tapt->clef );
    isom_remove_prof( tapt->prof );
    isom_remove_enof( tapt->enof );
    isom_remove_box( tapt, isom_trak_entry_t );
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
    isom_remove_elst( edts->elst );
    isom_remove_box( edts, isom_trak_entry_t );
}

void isom_remove_track_reference_type( isom_tref_type_t *ref )
{
    if( !ref )
        return;
    if( ref->track_ID )
        free( ref->track_ID );
    free( ref );
}

void isom_remove_tref( isom_tref_t *tref )
{
    if( !tref )
        return;
    lsmash_remove_list( tref->ref_list, isom_remove_track_reference_type );
    isom_remove_box( tref, isom_trak_entry_t );
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
    isom_remove_gmin( gmhd->gmin );
    isom_remove_text( gmhd->text );
    isom_remove_box( gmhd, isom_minf_t );
}

static void isom_remove_hdlr( isom_hdlr_t *hdlr )
{
    if( !hdlr )
        return;
    if( hdlr->componentName )
        free( hdlr->componentName );
    if( hdlr->parent )
    {
        if( hdlr->parent->type == ISOM_BOX_TYPE_MDIA )
            isom_remove_box( hdlr, isom_mdia_t );
        else if( hdlr->parent->type == ISOM_BOX_TYPE_META )
            isom_remove_box( hdlr, isom_meta_t );
        else if( hdlr->parent->type == ISOM_BOX_TYPE_MINF )
            isom_remove_box( hdlr, isom_minf_t );
        else
            assert( 0 );
        return;
    }
    free( hdlr );
}

void isom_remove_clap( isom_clap_t *clap )
{
    if( !clap )
        return;
    isom_remove_box( clap, isom_visual_entry_t );
}

void isom_remove_pasp( isom_pasp_t *pasp )
{
    if( !pasp )
        return;
    isom_remove_box( pasp, isom_visual_entry_t );
}

void isom_remove_glbl( isom_glbl_t *glbl )
{
    if( !glbl )
        return;
    if( glbl->header_data )
        free( glbl->header_data );
    isom_remove_box( glbl, isom_visual_entry_t );
}

void isom_remove_colr( isom_colr_t *colr )
{
    if( !colr )
        return;
    isom_remove_box( colr, isom_visual_entry_t );
}

void isom_remove_gama( isom_gama_t *gama )
{
    if( !gama )
        return;
    isom_remove_box( gama, isom_visual_entry_t );
}

void isom_remove_fiel( isom_fiel_t *fiel )
{
    if( !fiel )
        return;
    isom_remove_box( fiel, isom_visual_entry_t );
}

void isom_remove_cspc( isom_cspc_t *cspc )
{
    if( !cspc )
        return;
    isom_remove_box( cspc, isom_visual_entry_t );
}

void isom_remove_sgbt( isom_sgbt_t *sgbt )
{
    if( !sgbt )
        return;
    isom_remove_box( sgbt, isom_visual_entry_t );
}

void isom_remove_stsl( isom_stsl_t *stsl )
{
    if( !stsl )
        return;
    isom_remove_box( stsl, isom_visual_entry_t );
}

static void isom_remove_esds( isom_esds_t *esds )
{
    if( !esds )
        return;
    mp4sys_remove_ES_Descriptor( esds->ES );
    if( esds->parent )
    {
        switch( esds->parent->type )
        {
            case ISOM_CODEC_TYPE_MP4V_VIDEO :
                isom_remove_box( esds, isom_visual_entry_t );
                break;
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
            case ISOM_CODEC_TYPE_M4AE_AUDIO :
                isom_remove_box( esds, isom_audio_entry_t );
                break;
            case QT_BOX_TYPE_WAVE :
                isom_remove_box( esds, isom_wave_t );
                break;
            case ISOM_CODEC_TYPE_MP4S_SYSTEM :
                isom_remove_box( esds, isom_mp4s_entry_t );
                break;
            default :
                assert( 0 );
        }
        return;
    }
    free( esds );
}

void isom_remove_avcC( isom_avcC_t *avcC )
{
    if( !avcC )
        return;
    lsmash_remove_list( avcC->sequenceParameterSets,   isom_remove_avcC_ps );
    lsmash_remove_list( avcC->pictureParameterSets,    isom_remove_avcC_ps );
    lsmash_remove_list( avcC->sequenceParameterSetExt, isom_remove_avcC_ps );
    isom_remove_box( avcC, isom_visual_entry_t );
}

void isom_remove_btrt( isom_btrt_t *btrt )
{
    if( !btrt )
        return;
    isom_remove_box( btrt, isom_visual_entry_t );
}

static void isom_remove_visual_extensions( isom_visual_entry_t *visual )
{
    if( !visual )
        return;
    isom_remove_avcC( visual->avcC );
    isom_remove_btrt( visual->btrt );
    isom_remove_esds( visual->esds );
    isom_remove_glbl( visual->glbl );
    isom_remove_colr( visual->colr );
    isom_remove_gama( visual->gama );
    isom_remove_fiel( visual->fiel );
    isom_remove_cspc( visual->cspc );
    isom_remove_sgbt( visual->sgbt );
    isom_remove_stsl( visual->stsl );
    isom_remove_clap( visual->clap );
    isom_remove_pasp( visual->pasp );
}

static void isom_remove_font_record( isom_font_record_t *font_record )
{
    if( !font_record )
        return;
    if( font_record->font_name )
        free( font_record->font_name );
    free( font_record );
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
    isom_remove_esds( wave->esds );
    isom_remove_terminator( wave->terminator );
    if( wave->exdata )
        free( wave->exdata );
    isom_remove_box( wave, isom_audio_entry_t );
}

void isom_remove_chan( isom_chan_t *chan )
{
    if( !chan )
        return;
    if( chan->channelDescriptions )
        free( chan->channelDescriptions );
    isom_remove_box( chan, isom_audio_entry_t );
}

static void isom_remove_visual_description( isom_visual_entry_t *visual )
{
    isom_remove_visual_extensions( visual );
    free( visual );
}

static void isom_remove_audio_description( isom_audio_entry_t *audio )
{
    isom_remove_esds( audio->esds );
    isom_remove_wave( audio->wave );
    isom_remove_chan( audio->chan );
    if( audio->exdata )
        free( audio->exdata );
    free( audio );
}

void isom_remove_sample_description( isom_sample_entry_t *sample )
{
    if( !sample )
        return;
    switch( sample->type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
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
        case QT_CODEC_TYPE_CFHD_VIDEO :
        case QT_CODEC_TYPE_DV10_VIDEO :
        case QT_CODEC_TYPE_DVOO_VIDEO :
        case QT_CODEC_TYPE_DVOR_VIDEO :
        case QT_CODEC_TYPE_DVTV_VIDEO :
        case QT_CODEC_TYPE_DVVT_VIDEO :
        case QT_CODEC_TYPE_HD10_VIDEO :
        case QT_CODEC_TYPE_M105_VIDEO :
        case QT_CODEC_TYPE_PNTG_VIDEO :
        case QT_CODEC_TYPE_SVQ1_VIDEO :
        case QT_CODEC_TYPE_SVQ3_VIDEO :
        case QT_CODEC_TYPE_SHR0_VIDEO :
        case QT_CODEC_TYPE_SHR1_VIDEO :
        case QT_CODEC_TYPE_SHR2_VIDEO :
        case QT_CODEC_TYPE_SHR3_VIDEO :
        case QT_CODEC_TYPE_SHR4_VIDEO :
        case QT_CODEC_TYPE_WRLE_VIDEO :
        case QT_CODEC_TYPE_APCH_VIDEO :
        case QT_CODEC_TYPE_APCN_VIDEO :
        case QT_CODEC_TYPE_APCS_VIDEO :
        case QT_CODEC_TYPE_APCO_VIDEO :
        case QT_CODEC_TYPE_AP4H_VIDEO :
        case QT_CODEC_TYPE_CIVD_VIDEO :
        //case QT_CODEC_TYPE_DRAC_VIDEO :
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
        case QT_CODEC_TYPE_FLIC_VIDEO :
        case QT_CODEC_TYPE_GIF_VIDEO :
        case QT_CODEC_TYPE_H261_VIDEO :
        case QT_CODEC_TYPE_H263_VIDEO :
        case QT_CODEC_TYPE_JPEG_VIDEO :
        case QT_CODEC_TYPE_MJPA_VIDEO :
        case QT_CODEC_TYPE_MJPB_VIDEO :
        case QT_CODEC_TYPE_PNG_VIDEO :
        case QT_CODEC_TYPE_RLE_VIDEO :
        case QT_CODEC_TYPE_RPZA_VIDEO :
        case QT_CODEC_TYPE_TGA_VIDEO :
        case QT_CODEC_TYPE_TIFF_VIDEO :
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
            isom_remove_visual_description( (isom_visual_entry_t *)sample );
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
        case ISOM_CODEC_TYPE_DRA1_AUDIO :
        case ISOM_CODEC_TYPE_ENCA_AUDIO :
        case ISOM_CODEC_TYPE_G719_AUDIO :
        case ISOM_CODEC_TYPE_G726_AUDIO :
        case ISOM_CODEC_TYPE_M4AE_AUDIO :
        case ISOM_CODEC_TYPE_MLPA_AUDIO :
        case ISOM_CODEC_TYPE_SAWP_AUDIO :
        case ISOM_CODEC_TYPE_SEVC_AUDIO :
        case ISOM_CODEC_TYPE_SQCP_AUDIO :
        case ISOM_CODEC_TYPE_SSMV_AUDIO :
        //case ISOM_CODEC_TYPE_TWOS_AUDIO :
            isom_remove_audio_description( (isom_audio_entry_t *)sample );
            break;
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
            isom_hint_entry_t *hint = (isom_hint_entry_t *)sample;
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
        //case ISOM_CODEC_TYPE_TEXT_META :
        case ISOM_CODEC_TYPE_URIM_META :
        case ISOM_CODEC_TYPE_XML_META  :
        {
            isom_metadata_entry_t *metadata = (isom_metadata_entry_t *)sample;
            free( metadata );
            break;
        }
        case ISOM_CODEC_TYPE_TX3G_TEXT :
        {
            isom_tx3g_entry_t *tx3g = (isom_tx3g_entry_t *)sample;
            if( tx3g->ftab )
                isom_remove_ftab( tx3g->ftab );
            free( tx3g );
            break;
        }
        case QT_CODEC_TYPE_TEXT_TEXT :
        {
            isom_text_entry_t *text = (isom_text_entry_t *)sample;
            if( text->font_name )
                free( text->font_name );
            free( text );
            break;
        }
        case ISOM_CODEC_TYPE_MP4S_SYSTEM :
        {
            isom_mp4s_entry_t *mp4s = (isom_mp4s_entry_t *)sample;
            isom_remove_esds( mp4s->esds );
            free( mp4s );
            break;
        }
        case LSMASH_CODEC_TYPE_RAW :
            if( sample->manager & LSMASH_VIDEO_DESCRIPTION )
                isom_remove_visual_description( (isom_visual_entry_t *)sample );
            else if( sample->manager & LSMASH_AUDIO_DESCRIPTION )
                isom_remove_audio_description( (isom_audio_entry_t *)sample );
            break;
        default :
            break;
    }
}

static void isom_remove_stsd( isom_stsd_t *stsd )
{
    if( !stsd )
        return;
    lsmash_remove_list( stsd->list, isom_remove_sample_description );
    isom_remove_box( stsd, isom_stbl_t );
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
        if( sdtp->parent->type == ISOM_BOX_TYPE_STBL )
            isom_remove_box( sdtp, isom_stbl_t );
        else if( sdtp->parent->type == ISOM_BOX_TYPE_TRAF )
            isom_remove_box( sdtp, isom_traf_entry_t );
        else
            assert( 0 );
        return;
    }
    free( sdtp );
}

static void isom_remove_stco( isom_stco_t *stco )
{
    if( !stco )
        return;
    lsmash_remove_list( stco->list, NULL );
    isom_remove_box( stco, isom_stbl_t );
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
    isom_remove_box( dref, isom_dinf_t );
}

static void isom_remove_dinf( isom_dinf_t *dinf )
{
    if( !dinf )
        return;
    isom_remove_dref( dinf->dref );
    isom_remove_box( dinf, isom_minf_t );
}

static void isom_remove_minf( isom_minf_t *minf )
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

static void isom_remove_mdia( isom_mdia_t *mdia )
{
    if( !mdia )
        return;
    isom_remove_mdhd( mdia->mdhd );
    isom_remove_minf( mdia->minf );
    isom_remove_hdlr( mdia->hdlr );
    isom_remove_box( mdia, isom_trak_entry_t );
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
    isom_remove_box( chpl, isom_udta_t );
}

static void isom_remove_keys_entry( isom_keys_entry_t *data )
{
    if( !data )
        return;
    if( data->key_value )
        free( data->key_value );
    free( data );
}

static void isom_remove_keys( isom_keys_t *keys )
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
        free( mean->meaning_string );
    isom_remove_box( mean, isom_metaitem_t );
}

void isom_remove_name( isom_name_t *name )
{
    if( !name )
        return;
    if( name->name )
        free( name->name );
    isom_remove_box( name, isom_metaitem_t );
}

void isom_remove_data( isom_data_t *data )
{
    if( !data )
        return;
    if( data->value )
        free( data->value );
    isom_remove_box( data, isom_metaitem_t );
}

void isom_remove_metaitem( isom_metaitem_t *metaitem )
{
    if( !metaitem )
        return;
    isom_remove_mean( metaitem->mean );
    isom_remove_name( metaitem->name );
    isom_remove_data( metaitem->data );
    free( metaitem );
}

void isom_remove_ilst( isom_ilst_t *ilst )
{
    if( !ilst )
        return;
    lsmash_remove_list( ilst->item_list, isom_remove_metaitem );
    isom_remove_box( ilst, isom_meta_t );
}

static void isom_remove_meta( isom_meta_t *meta )
{
    if( !meta )
        return;
    isom_remove_hdlr( meta->hdlr );
    isom_remove_dinf( meta->dinf );
    isom_remove_keys( meta->keys );
    isom_remove_ilst( meta->ilst );
    if( meta->parent )
    {
        if( !meta->parent->type )
            isom_remove_box( meta, lsmash_root_t );
        else if( meta->parent->type == ISOM_BOX_TYPE_MOOV )
            isom_remove_box( meta, isom_moov_t );
        else if( meta->parent->type == ISOM_BOX_TYPE_TRAK )
            isom_remove_box( meta, isom_trak_entry_t );
        else if( meta->parent->type == ISOM_BOX_TYPE_UDTA )
            isom_remove_box( meta, isom_udta_t );
        else
            assert( 0 );
        return;
    }
    free( meta );
}

static void isom_remove_cprt( isom_cprt_t *cprt )
{
    if( !cprt )
        return;
    if( cprt->notice )
        free( cprt->notice );
    free( cprt );
}

static void isom_remove_udta( isom_udta_t *udta )
{
    if( !udta )
        return;
    isom_remove_chpl( udta->chpl );
    isom_remove_meta( udta->meta );
    free( udta->WLOC );
    free( udta->LOOP );
    free( udta->SelO );
    free( udta->AllF );
    lsmash_remove_list( udta->cprt_list, isom_remove_cprt );
    if( udta->parent )
    {
        if( udta->parent->type == ISOM_BOX_TYPE_MOOV )
            isom_remove_box( udta, isom_moov_t );
        else if( udta->parent->type == ISOM_BOX_TYPE_TRAK )
            isom_remove_box( udta, isom_trak_entry_t );
        else
            assert( 0 );
        return;
    }
    free( udta );
}

static void isom_remove_sample_pool( isom_sample_pool_t *pool );

void isom_remove_trak( isom_trak_entry_t *trak )
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
    isom_remove_box( iods, isom_moov_t );
}

static void isom_remove_mehd( isom_mehd_t *mehd )
{
    if( !mehd )
        return;
    isom_remove_box( mehd, isom_mvex_t );
}

static void isom_remove_mvex( isom_mvex_t *mvex )
{
    if( !mvex )
        return;
    isom_remove_mehd( mvex->mehd );
    lsmash_remove_list( mvex->trex_list, NULL );
    isom_remove_box( mvex, isom_moov_t );
}

static void isom_remove_moov( lsmash_root_t *root )
{
    if( !root || !root->moov )
        return;
    isom_moov_t *moov = root->moov;
    if( moov->mvhd )
        free( moov->mvhd );
    isom_remove_iods( moov->iods );
    lsmash_remove_list( moov->trak_list, isom_remove_trak );
    isom_remove_udta( moov->udta );
    isom_remove_meta( moov->meta );
    isom_remove_mvex( moov->mvex );
    free( moov );
    root->moov = NULL;
}

static void isom_remove_mfhd( isom_mfhd_t *mfhd )
{
    if( !mfhd )
        return;
    isom_remove_box( mfhd, isom_moof_entry_t );
}

static void isom_remove_tfhd( isom_tfhd_t *tfhd )
{
    if( !tfhd )
        return;
    isom_remove_box( tfhd, isom_traf_entry_t );
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
    isom_remove_tfhd( traf->tfhd );
    lsmash_remove_list( traf->trun_list, isom_remove_trun );
    isom_remove_sdtp( traf->sdtp );
    free( traf );   /* Note: the list that contains this traf still has the address of the entry. */
}

static void isom_remove_moof( isom_moof_entry_t *moof )
{
    if( !moof )
        return;
    isom_remove_mfhd( moof->mfhd );
    lsmash_remove_list( moof->traf_list, isom_remove_traf );
    free( moof );
}

static void isom_remove_mdat( isom_mdat_t *mdat )
{
    if( !mdat )
        return;
    isom_remove_box( mdat, lsmash_root_t );
}

static void isom_remove_free( isom_free_t *skip )
{
    if( !skip )
        return;
    if( skip->data )
        free( skip->data );
    lsmash_root_t *root = (lsmash_root_t *)skip->parent;
    free( skip );
    root->free = NULL;
}

static void isom_remove_tfra( isom_tfra_entry_t *tfra )
{
    if( !tfra )
        return;
    lsmash_remove_list( tfra->list, NULL );
    free( tfra );
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
    lsmash_remove_list( mfra->tfra_list, isom_remove_tfra );
    isom_remove_mfro( mfra->mfro );
    isom_remove_box( mfra, lsmash_root_t );
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

int isom_check_compatibility( lsmash_root_t *root )
{
    if( !root )
        return -1;
    root->qt_compatible = 0;
    /* Check brand to decide mandatory boxes. */
    if( !root->ftyp || !root->ftyp->brand_count )
    {
        /* No brand declaration means this file is a MP4 version 1 or QuickTime file format. */
        if( root->moov && root->moov->iods )
        {
            root->mp4_version1 = 1;
            root->isom_compatible = 1;
        }
        else
            root->qt_compatible = 1;
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
            case ISOM_BRAND_TYPE_AVC1 :
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
            case ISOM_BRAND_TYPE_M4P :
            case ISOM_BRAND_TYPE_M4V :
                root->itunes_movie = 1;
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
    root->isom_compatible = !root->qt_compatible || root->mp4_version1 || root->mp4_version2 || root->itunes_movie || root->max_3gpp_version;
    return 0;
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
        dts += (uint64_t)data->sample_delta * data->sample_count;
        i += data->sample_count;
    }
    if( !entry )
        return 0;
    dts += (uint64_t)data->sample_delta * (sample_number - i);
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
    if( !trak || !trak->root || !trak->cache || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->minf
     || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list )
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
        int32_t  ctd_shift = trak->cache->timestamp.ctd_shift;
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
            uint64_t cts;
            if( ctd_shift )
            {
                /* Anyway, add composition to decode timeline shift for calculating maximum and minimum CTS correctly. */
                int32_t sample_offset = (int32_t)ctts_data->sample_offset;
                cts = dts + sample_offset + ctd_shift;
                max_offset = LSMASH_MAX( (int32_t)max_offset, sample_offset );
                min_offset = LSMASH_MIN( (int32_t)min_offset, sample_offset );
            }
            else
            {
                cts = dts + ctts_data->sample_offset;
                max_offset = LSMASH_MAX( max_offset, ctts_data->sample_offset );
                min_offset = LSMASH_MIN( min_offset, ctts_data->sample_offset );
            }
            min_cts = LSMASH_MIN( min_cts, cts );
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
        /* Explicit composition information and timeline shifting  */
        if( cslg || root->qt_compatible || root->max_isom_version >= 4 )
        {
            if( ctd_shift )
            {
                /* Remove composition to decode timeline shift. */
                max_cts  -= ctd_shift;
                max2_cts -= ctd_shift;
                min_cts  -= ctd_shift;
            }
            int64_t composition_end_time = max_cts + (max_cts - max2_cts);
            if( !root->fragment
             && ((int32_t)min_offset <= INT32_MAX) && ((int32_t)max_offset <= INT32_MAX)
             && ((int64_t)min_cts <= INT32_MAX) && (composition_end_time <= INT32_MAX) )
            {
                if( !cslg )
                {
                    if( isom_add_cslg( trak->mdia->minf->stbl ) )
                        return -1;
                    cslg = stbl->cslg;
                }
                cslg->compositionToDTSShift = ctd_shift;
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
    /* If the presentation won't be extended and this track has any edit, we don't change or update duration in tkhd. */
    return (!root->fragment && trak->edts && trak->edts->elst)
         ? isom_update_mvhd_duration( root->moov )  /* Only update movie duration. */
         : isom_update_tkhd_duration( trak );       /* Also update movie duration internally. */
}

static inline int isom_increment_sample_number_in_entry( uint32_t *sample_number_in_entry, uint32_t sample_count_in_entry, lsmash_entry_t **entry )
{
    if( *sample_number_in_entry != sample_count_in_entry )
    {
        *sample_number_in_entry += 1;
        return 0;
    }
    /* Precede the next entry. */
    *sample_number_in_entry = 1;
    if( *entry )
    {
        *entry = (*entry)->next;
        if( *entry && !(*entry)->data )
            return -1;
    }
    return 0;
}

static int isom_calculate_bitrate_description( isom_mdia_t *mdia, uint32_t *bufferSizeDB, uint32_t *maxBitrate, uint32_t *avgBitrate, uint32_t sample_description_index )
{
    isom_stsz_t *stsz               = mdia->minf->stbl->stsz;
    lsmash_entry_t *stsz_entry      = stsz->list ? stsz->list->head : NULL;
    lsmash_entry_t *stts_entry      = mdia->minf->stbl->stts->list->head;
    lsmash_entry_t *stsc_entry      = NULL;
    lsmash_entry_t *next_stsc_entry = mdia->minf->stbl->stsc->list->head;
    isom_stts_entry_t *stts_data    = NULL;
    isom_stsc_entry_t *stsc_data    = NULL;
    if( next_stsc_entry && !next_stsc_entry->data )
        return -1;
    uint32_t rate                   = 0;
    uint64_t dts                    = 0;
    uint32_t time_wnd               = 0;
    uint32_t timescale              = mdia->mdhd->timescale;
    uint32_t chunk_number           = 0;
    uint32_t sample_number_in_stts  = 1;
    uint32_t sample_number_in_chunk = 1;
    *bufferSizeDB = 0;
    *maxBitrate   = 0;
    *avgBitrate   = 0;
    while( stts_entry )
    {
        if( !stsc_data || sample_number_in_chunk == stsc_data->samples_per_chunk )
        {
            /* Move the next chunk. */
            sample_number_in_chunk = 1;
            ++chunk_number;
            /* Check if the next entry is broken. */
            while( next_stsc_entry && ((isom_stsc_entry_t *)next_stsc_entry->data)->first_chunk < chunk_number )
            {
                /* Just skip broken next entry. */
                next_stsc_entry = next_stsc_entry->next;
                if( next_stsc_entry && !next_stsc_entry->data )
                    return -1;
            }
            /* Check if the next chunk belongs to the next sequence of chunks. */
            if( next_stsc_entry && ((isom_stsc_entry_t *)next_stsc_entry->data)->first_chunk == chunk_number )
            {
                stsc_entry = next_stsc_entry;
                next_stsc_entry = next_stsc_entry->next;
                if( next_stsc_entry && !next_stsc_entry->data )
                    return -1;
                stsc_data = (isom_stsc_entry_t *)stsc_entry->data;
                /* Check if the next contiguous chunks belong to given sample description. */
                if( stsc_data->sample_description_index != sample_description_index )
                {
                    /* Skip chunks which don't belong to given sample description. */
                    uint32_t number_of_skips   = 0;
                    uint32_t first_chunk       = stsc_data->first_chunk;
                    uint32_t samples_per_chunk = stsc_data->samples_per_chunk;
                    while( next_stsc_entry )
                    {
                        if( ((isom_stsc_entry_t *)next_stsc_entry->data)->sample_description_index != sample_description_index )
                        {
                            stsc_data = (isom_stsc_entry_t *)next_stsc_entry->data;
                            number_of_skips  += (stsc_data->first_chunk - first_chunk) * samples_per_chunk;
                            first_chunk       = stsc_data->first_chunk;
                            samples_per_chunk = stsc_data->samples_per_chunk;
                        }
                        else if( ((isom_stsc_entry_t *)next_stsc_entry->data)->first_chunk <= first_chunk )
                            ;   /* broken entry */
                        else
                            break;
                        /* Just skip the next entry. */
                        next_stsc_entry = next_stsc_entry->next;
                        if( next_stsc_entry && !next_stsc_entry->data )
                            return -1;
                    }
                    if( !next_stsc_entry )
                        break;      /* There is no more chunks which don't belong to given sample description. */
                    number_of_skips += (((isom_stsc_entry_t *)next_stsc_entry->data)->first_chunk - first_chunk) * samples_per_chunk;
                    for( uint32_t i = 0; i < number_of_skips; i++ )
                    {
                        if( stsz->list )
                        {
                            if( !stsz_entry )
                                break;
                            stsz_entry = stsz_entry->next;
                        }
                        if( !stts_entry )
                            break;
                        if( isom_increment_sample_number_in_entry( &sample_number_in_stts, ((isom_stts_entry_t *)stts_entry->data)->sample_count, &stts_entry ) )
                            return -1;
                    }
                    if( (stsz->list && !stsz_entry) || !stts_entry )
                        break;
                    chunk_number = stsc_data->first_chunk;
                }
            }
        }
        else
            ++sample_number_in_chunk;
        /* Get current sample's size. */
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
        /* Get current sample's DTS. */
        if( stts_data )
            dts += stts_data->sample_delta;
        stts_data = (isom_stts_entry_t *)stts_entry->data;
        if( !stts_data )
            return -1;
        isom_increment_sample_number_in_entry( &sample_number_in_stts, stts_data->sample_count, &stts_entry );
        /* Calculate bitrate description. */
        if( *bufferSizeDB < size )
            *bufferSizeDB = size;
        *avgBitrate += size;
        rate += size;
        if( dts > time_wnd + timescale )
        {
            if( rate > *maxBitrate )
                *maxBitrate = rate;
            time_wnd = dts;
            rate = 0;
        }
    }
    double duration = (double)mdia->mdhd->duration / timescale;
    *avgBitrate = (uint32_t)(*avgBitrate / duration);
    if( !*maxBitrate )
        *maxBitrate = *avgBitrate;
    /* Convert to bits per second. */
    *maxBitrate *= 8;
    *avgBitrate *= 8;
    return 0;
}

static int isom_update_bitrate_description( isom_mdia_t *mdia )
{
    if( !mdia || !mdia->mdhd || !mdia->minf || !mdia->minf->stbl )
        return -1;
    isom_stbl_t *stbl = mdia->minf->stbl;
    if( !stbl->stsd || !stbl->stsd->list
     || !stbl->stsz
     || !stbl->stsc || !stbl->stsc->list
     || !stbl->stts || !stbl->stts->list )
        return -1;
    uint32_t sample_description_index = 0;
    for( lsmash_entry_t *entry = stbl->stsd->list->head; entry; entry = entry->next )
    {
        isom_sample_entry_t *sample_entry = (isom_sample_entry_t *)entry->data;
        if( !sample_entry )
            return -1;
        ++sample_description_index;
        uint32_t bufferSizeDB;
        uint32_t maxBitrate;
        uint32_t avgBitrate;
        /* set bitrate info */
        switch( sample_entry->type )
        {
            case ISOM_CODEC_TYPE_AVC1_VIDEO :
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
            {
                isom_visual_entry_t *stsd_data = (isom_visual_entry_t *)sample_entry;
                if( !stsd_data )
                    return -1;
                isom_btrt_t *btrt = stsd_data->btrt;
                if( btrt )
                {
                    if( isom_calculate_bitrate_description( mdia, &bufferSizeDB, &maxBitrate, &avgBitrate, sample_description_index ) )
                        return -1;
                    btrt->bufferSizeDB = bufferSizeDB;
                    btrt->maxBitrate   = maxBitrate;
                    btrt->avgBitrate   = avgBitrate;
                }
                break;
            }
            case ISOM_CODEC_TYPE_MP4V_VIDEO :
            {
                isom_visual_entry_t *stsd_data = (isom_visual_entry_t *)sample_entry;
                if( !stsd_data || !stsd_data->esds || !stsd_data->esds->ES )
                    return -1;
                isom_esds_t *esds = stsd_data->esds;
                if( isom_calculate_bitrate_description( mdia, &bufferSizeDB, &maxBitrate, &avgBitrate, sample_description_index ) )
                    return -1;
                /* FIXME: avgBitrate is 0 only if VBR in proper. */
                if( mp4sys_update_DecoderConfigDescriptor( esds->ES, bufferSizeDB, maxBitrate, 0 ) )
                    return -1;
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
                if( isom_calculate_bitrate_description( mdia, &bufferSizeDB, &maxBitrate, &avgBitrate, sample_description_index ) )
                    return -1;
                /* FIXME: avgBitrate is 0 only if VBR in proper. */
                if( mp4sys_update_DecoderConfigDescriptor( esds->ES, bufferSizeDB, maxBitrate, 0 ) )
                    return -1;
                break;
            }
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            {
                isom_audio_entry_t *alac = (isom_audio_entry_t *)sample_entry;
                if( !alac )
                    return -1;
                if( alac->exdata_length < 36 || !alac->exdata )
                {
                    isom_wave_t *wave = alac->wave;
                    if( !wave || wave->exdata_length < 36 || !wave->exdata )
                        return -1;
                    break;      /* Apparently, average bitrate field is 0. */
                }
                if( isom_calculate_bitrate_description( mdia, &bufferSizeDB, &maxBitrate, &avgBitrate, sample_description_index ) )
                    return -1;
                uint8_t *exdata = (uint8_t *)alac->exdata + 28;
                exdata[0] = (avgBitrate >> 24) & 0xff;
                exdata[1] = (avgBitrate >> 16) & 0xff;
                exdata[2] = (avgBitrate >>  8) & 0xff;
                exdata[3] =  avgBitrate        & 0xff;
                break;
            }
            case ISOM_CODEC_TYPE_DTSC_AUDIO :
            case ISOM_CODEC_TYPE_DTSE_AUDIO :
            case ISOM_CODEC_TYPE_DTSH_AUDIO :
            case ISOM_CODEC_TYPE_DTSL_AUDIO :
            {
                isom_audio_entry_t *dts_audio = (isom_audio_entry_t *)sample_entry;
                if( !dts_audio )
                    return -1;
                if( dts_audio->exdata_length < 28 || !dts_audio->exdata )
                    return -1;
                if( isom_calculate_bitrate_description( mdia, &bufferSizeDB, &maxBitrate, &avgBitrate, sample_description_index ) )
                    return -1;
                if( !stbl->stsz->list )
                    maxBitrate = avgBitrate;
                uint8_t *exdata = (uint8_t *)dts_audio->exdata + 12;
                exdata[0] = (maxBitrate >> 24) & 0xff;
                exdata[1] = (maxBitrate >> 16) & 0xff;
                exdata[2] = (maxBitrate >>  8) & 0xff;
                exdata[3] =  maxBitrate        & 0xff;
                exdata[4] = (avgBitrate >> 24) & 0xff;
                exdata[5] = (avgBitrate >> 16) & 0xff;
                exdata[6] = (avgBitrate >>  8) & 0xff;
                exdata[7] =  avgBitrate        & 0xff;
                break;
            }
            case ISOM_CODEC_TYPE_EC_3_AUDIO :
            {
                isom_audio_entry_t *eac3 = (isom_audio_entry_t *)sample_entry;
                if( !eac3 )
                    return -1;
                if( eac3->exdata_length < 10 || !eac3->exdata )
                    return -1;
                uint16_t bitrate;
                if( stbl->stsz->list )
                {
                    if( isom_calculate_bitrate_description( mdia, &bufferSizeDB, &maxBitrate, &avgBitrate, sample_description_index ) )
                        return -1;
                    bitrate = maxBitrate / 1000;    /* Use maximum bitrate if VBR. */
                }
                else
                    bitrate = stbl->stsz->sample_size * (eac3->samplerate >> 16) / 192000;      /* 192000 == 1536 * 1000 / 8 */
                uint8_t *exdata = (uint8_t *)eac3->exdata + 8;
                exdata[0] = (bitrate >> 5) & 0xff;
                exdata[1] = (bitrate & 0x1f) << 3;
                break;
            }
            default :
                break;
        }
    }
    return sample_description_index ? 0 : -1;
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
    mvhd->size = ISOM_FULLBOX_COMMON_SIZE + 96 + (uint64_t)mvhd->version * 12;
    CHECK_LARGESIZE( mvhd->size );
    return mvhd->size;
}

static uint64_t isom_update_iods_size( isom_iods_t *iods )
{
    if( !iods || !iods->OD )
        return 0;
    iods->size = ISOM_FULLBOX_COMMON_SIZE + mp4sys_update_ObjectDescriptor_size( iods->OD );
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
    tkhd->size = ISOM_FULLBOX_COMMON_SIZE + 80 + (uint64_t)tkhd->version * 12;
    CHECK_LARGESIZE( tkhd->size );
    return tkhd->size;
}

static uint64_t isom_update_clef_size( isom_clef_t *clef )
{
    if( !clef )
        return 0;
    clef->size = ISOM_FULLBOX_COMMON_SIZE + 8;
    CHECK_LARGESIZE( clef->size );
    return clef->size;
}

static uint64_t isom_update_prof_size( isom_prof_t *prof )
{
    if( !prof )
        return 0;
    prof->size = ISOM_FULLBOX_COMMON_SIZE + 8;
    CHECK_LARGESIZE( prof->size );
    return prof->size;
}

static uint64_t isom_update_enof_size( isom_enof_t *enof )
{
    if( !enof )
        return 0;
    enof->size = ISOM_FULLBOX_COMMON_SIZE + 8;
    CHECK_LARGESIZE( enof->size );
    return enof->size;
}

static uint64_t isom_update_tapt_size( isom_tapt_t *tapt )
{
    if( !tapt )
        return 0;
    tapt->size = ISOM_BASEBOX_COMMON_SIZE
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
        if( data->segment_duration > UINT32_MAX || data->media_time > INT32_MAX || data->media_time < INT32_MIN )
            elst->version = 1;
    }
    elst->size = ISOM_LIST_FULLBOX_COMMON_SIZE + (uint64_t)i * ( elst->version ? 20 : 12 );
    CHECK_LARGESIZE( elst->size );
    return elst->size;
}

static uint64_t isom_update_edts_size( isom_edts_t *edts )
{
    if( !edts )
        return 0;
    edts->size = ISOM_BASEBOX_COMMON_SIZE + isom_update_elst_size( edts->elst );
    CHECK_LARGESIZE( edts->size );
    return edts->size;
}

static uint64_t isom_update_tref_size( isom_tref_t *tref )
{
    if( !tref )
        return 0;
    tref->size = ISOM_BASEBOX_COMMON_SIZE;
    if( tref->ref_list )
        for( lsmash_entry_t *entry = tref->ref_list->head; entry; entry = entry->next )
        {
            isom_tref_type_t *ref = (isom_tref_type_t *)entry->data;
            ref->size = ISOM_BASEBOX_COMMON_SIZE + (uint64_t)ref->ref_count * 4;
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
    mdhd->size = ISOM_FULLBOX_COMMON_SIZE + 20 + (uint64_t)mdhd->version * 12;
    CHECK_LARGESIZE( mdhd->size );
    return mdhd->size;
}

static uint64_t isom_update_hdlr_size( isom_hdlr_t *hdlr )
{
    if( !hdlr )
        return 0;
    hdlr->size = ISOM_FULLBOX_COMMON_SIZE + 20 + (uint64_t)hdlr->componentName_length;
    CHECK_LARGESIZE( hdlr->size );
    return hdlr->size;
}

static uint64_t isom_update_dref_entry_size( isom_dref_entry_t *urln )
{
    if( !urln )
        return 0;
    urln->size = ISOM_FULLBOX_COMMON_SIZE + (uint64_t)urln->name_length + urln->location_length;
    CHECK_LARGESIZE( urln->size );
    return urln->size;
}

static uint64_t isom_update_dref_size( isom_dref_t *dref )
{
    if( !dref || !dref->list )
        return 0;
    dref->size = ISOM_LIST_FULLBOX_COMMON_SIZE;
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
    dinf->size = ISOM_BASEBOX_COMMON_SIZE + isom_update_dref_size( dinf->dref );
    CHECK_LARGESIZE( dinf->size );
    return dinf->size;
}

static uint64_t isom_update_vmhd_size( isom_vmhd_t *vmhd )
{
    if( !vmhd )
        return 0;
    vmhd->size = ISOM_FULLBOX_COMMON_SIZE + 8;
    CHECK_LARGESIZE( vmhd->size );
    return vmhd->size;
}

static uint64_t isom_update_smhd_size( isom_smhd_t *smhd )
{
    if( !smhd )
        return 0;
    smhd->size = ISOM_FULLBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( smhd->size );
    return smhd->size;
}

static uint64_t isom_update_hmhd_size( isom_hmhd_t *hmhd )
{
    if( !hmhd )
        return 0;
    hmhd->size = ISOM_FULLBOX_COMMON_SIZE + 16;
    CHECK_LARGESIZE( hmhd->size );
    return hmhd->size;
}

static uint64_t isom_update_nmhd_size( isom_nmhd_t *nmhd )
{
    if( !nmhd )
        return 0;
    nmhd->size = ISOM_FULLBOX_COMMON_SIZE;
    CHECK_LARGESIZE( nmhd->size );
    return nmhd->size;
}

static uint64_t isom_update_gmin_size( isom_gmin_t *gmin )
{
    if( !gmin )
        return 0;
    gmin->size = ISOM_FULLBOX_COMMON_SIZE + 12;
    CHECK_LARGESIZE( gmin->size );
    return gmin->size;
}

static uint64_t isom_update_text_size( isom_text_t *text )
{
    if( !text )
        return 0;
    text->size = ISOM_BASEBOX_COMMON_SIZE + 36;
    CHECK_LARGESIZE( text->size );
    return text->size;
}

static uint64_t isom_update_gmhd_size( isom_gmhd_t *gmhd )
{
    if( !gmhd )
        return 0;
    gmhd->size = ISOM_BASEBOX_COMMON_SIZE
        + isom_update_gmin_size( gmhd->gmin )
        + isom_update_text_size( gmhd->text );
    CHECK_LARGESIZE( gmhd->size );
    return gmhd->size;
}

static uint64_t isom_update_pasp_size( isom_pasp_t *pasp )
{
    if( !pasp )
        return 0;
    pasp->size = ISOM_BASEBOX_COMMON_SIZE + 8;
    CHECK_LARGESIZE( pasp->size );
    return pasp->size;
}

static uint64_t isom_update_clap_size( isom_clap_t *clap )
{
    if( !clap )
        return 0;
    clap->size = ISOM_BASEBOX_COMMON_SIZE + 32;
    CHECK_LARGESIZE( clap->size );
    return clap->size;
}

static uint64_t isom_update_glbl_size( isom_glbl_t *glbl )
{
    if( !glbl )
        return 0;
    glbl->size = ISOM_BASEBOX_COMMON_SIZE + (uint64_t)glbl->header_size;
    CHECK_LARGESIZE( glbl->size );
    return glbl->size;
}

static uint64_t isom_update_colr_size( isom_colr_t *colr )
{
    if( !colr || colr->color_parameter_type == QT_COLOR_PARAMETER_TYPE_PROF )
        return 0;
    colr->size = ISOM_BASEBOX_COMMON_SIZE + 10;
    CHECK_LARGESIZE( colr->size );
    return colr->size;
}

static uint64_t isom_update_gama_size( isom_gama_t *gama )
{
    /* Note: 'gama' box is superseded by 'colr' box.
     * Therefore, writers of QTFF should never write both 'colr' and 'gama' box into an Image Description. */
    if( !gama || (gama->parent && ((isom_visual_entry_t *)gama->parent)->colr) )
        return 0;
    gama->size = ISOM_BASEBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( gama->size );
    return gama->size;
}

static uint64_t isom_update_fiel_size( isom_fiel_t *fiel )
{
    if( !fiel )
        return 0;
    fiel->size = ISOM_BASEBOX_COMMON_SIZE + 2;
    CHECK_LARGESIZE( fiel->size );
    return fiel->size;
}

static uint64_t isom_update_cspc_size( isom_cspc_t *cspc )
{
    if( !cspc )
        return 0;
    cspc->size = ISOM_BASEBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( cspc->size );
    return cspc->size;
}

static uint64_t isom_update_sgbt_size( isom_sgbt_t *sgbt )
{
    if( !sgbt )
        return 0;
    sgbt->size = ISOM_BASEBOX_COMMON_SIZE + 1;
    CHECK_LARGESIZE( sgbt->size );
    return sgbt->size;
}

static uint64_t isom_update_stsl_size( isom_stsl_t *stsl )
{
    if( !stsl )
        return 0;
    stsl->size = ISOM_FULLBOX_COMMON_SIZE + 6;
    CHECK_LARGESIZE( stsl->size );
    return stsl->size;
}

static uint64_t isom_update_esds_size( isom_esds_t *esds )
{
    if( !esds )
        return 0;
    esds->size = ISOM_FULLBOX_COMMON_SIZE + mp4sys_update_ES_Descriptor_size( esds->ES );
    CHECK_LARGESIZE( esds->size );
    return esds->size;
}

static uint64_t isom_update_avcC_size( isom_avcC_t *avcC )
{
    if( !avcC || !avcC->sequenceParameterSets || !avcC->pictureParameterSets )
        return 0;
    uint64_t size = ISOM_BASEBOX_COMMON_SIZE + 7;
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

static uint64_t isom_update_btrt_size( isom_btrt_t *btrt )
{
    if( !btrt )
        return 0;
    btrt->size = ISOM_BASEBOX_COMMON_SIZE + 12;
    CHECK_LARGESIZE( btrt->size );
    return btrt->size;
}

static uint64_t isom_update_visual_entry_size( isom_visual_entry_t *visual )
{
    if( !visual )
        return 0;
    visual->size = ISOM_BASEBOX_COMMON_SIZE + 78
        + isom_update_avcC_size( visual->avcC )
        + isom_update_btrt_size( visual->btrt )
        + isom_update_esds_size( visual->esds )
        + isom_update_glbl_size( visual->glbl )
        + isom_update_colr_size( visual->colr )
        + isom_update_gama_size( visual->gama )
        + isom_update_fiel_size( visual->fiel )
        + isom_update_cspc_size( visual->cspc )
        + isom_update_sgbt_size( visual->sgbt )
        + isom_update_stsl_size( visual->stsl )
        + isom_update_clap_size( visual->clap )
        + isom_update_pasp_size( visual->pasp )
        + (uint64_t)visual->exdata_length;
    CHECK_LARGESIZE( visual->size );
    return visual->size;
}

#if 0
static uint64_t isom_update_mp4s_entry_size( isom_mp4s_entry_t *mp4s )
{
    if( !mp4s || mp4s->type != ISOM_CODEC_TYPE_MP4S_SYSTEM )
        return 0;
    mp4s->size = ISOM_BASEBOX_COMMON_SIZE + 8 + isom_update_esds_size( mp4s->esds );
    CHECK_LARGESIZE( mp4s->size );
    return mp4s->size;
}
#endif

static uint64_t isom_update_frma_size( isom_frma_t *frma )
{
    if( !frma )
        return 0;
    frma->size = ISOM_BASEBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( frma->size );
    return frma->size;
}

static uint64_t isom_update_enda_size( isom_enda_t *enda )
{
    if( !enda )
        return 0;
    enda->size = ISOM_BASEBOX_COMMON_SIZE + 2;
    CHECK_LARGESIZE( enda->size );
    return enda->size;
}

static uint64_t isom_update_mp4a_size( isom_mp4a_t *mp4a )
{
    if( !mp4a )
        return 0;
    mp4a->size = ISOM_BASEBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( mp4a->size );
    return mp4a->size;
}

static uint64_t isom_update_terminator_size( isom_terminator_t *terminator )
{
    if( !terminator )
        return 0;
    terminator->size = ISOM_BASEBOX_COMMON_SIZE;
    CHECK_LARGESIZE( terminator->size );
    return terminator->size;
}

static uint64_t isom_update_wave_size( isom_wave_t *wave )
{
    if( !wave )
        return 0;
    wave->size = ISOM_BASEBOX_COMMON_SIZE
        + isom_update_frma_size( wave->frma )
        + isom_update_enda_size( wave->enda )
        + isom_update_mp4a_size( wave->mp4a )
        + isom_update_esds_size( wave->esds )
        + isom_update_terminator_size( wave->terminator )
        + (uint64_t)wave->exdata_length;
    CHECK_LARGESIZE( wave->size );
    return wave->size;
}

static uint64_t isom_update_chan_size( isom_chan_t *chan )
{
    if( !chan )
        return 0;
    chan->size = ISOM_FULLBOX_COMMON_SIZE + 12 + 20 * (uint64_t)chan->numberChannelDescriptions;
    CHECK_LARGESIZE( chan->size );
    return chan->size;
}

static uint64_t isom_update_audio_entry_size( isom_audio_entry_t *audio )
{
    if( !audio )
        return 0;
    audio->size = ISOM_BASEBOX_COMMON_SIZE + 28
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
    text->size = ISOM_BASEBOX_COMMON_SIZE + 51 + (uint64_t)text->font_name_length;
    CHECK_LARGESIZE( text->size );
    return text->size;
}

static uint64_t isom_update_ftab_size( isom_ftab_t *ftab )
{
    if( !ftab || !ftab->list )
        return 0;
    ftab->size = ISOM_BASEBOX_COMMON_SIZE + 2;
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
    tx3g->size = ISOM_BASEBOX_COMMON_SIZE + 38 + isom_update_ftab_size( tx3g->ftab );
    CHECK_LARGESIZE( tx3g->size );
    return tx3g->size;
}

static uint64_t isom_update_stsd_size( isom_stsd_t *stsd )
{
    if( !stsd || !stsd->list )
        return 0;
    uint64_t size = ISOM_LIST_FULLBOX_COMMON_SIZE;
    for( lsmash_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_sample_entry_t *data = (isom_sample_entry_t *)entry->data;
        switch( data->type )
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
                size += isom_update_visual_entry_size( (isom_visual_entry_t *)data );
                break;
#if 0
            case ISOM_CODEC_TYPE_MP4S_SYSTEM :
                size += isom_update_mp4s_entry_size( (isom_mp4s_entry_t *)data );
                break;
#endif
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
                size += isom_update_audio_entry_size( (isom_audio_entry_t *)data );
                break;
            case ISOM_CODEC_TYPE_TX3G_TEXT :
                size += isom_update_tx3g_entry_size( (isom_tx3g_entry_t *)data );
                break;
            case QT_CODEC_TYPE_TEXT_TEXT :
                size += isom_update_text_entry_size( (isom_text_entry_t *)data );
                break;
            case LSMASH_CODEC_TYPE_RAW :
                if( data->manager & LSMASH_VIDEO_DESCRIPTION )
                    size += isom_update_visual_entry_size( (isom_visual_entry_t *)data );
                else if( data->manager & LSMASH_AUDIO_DESCRIPTION )
                    size += isom_update_audio_entry_size( (isom_audio_entry_t *)data );
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
    stts->size = ISOM_LIST_FULLBOX_COMMON_SIZE + (uint64_t)stts->list->entry_count * 8;
    CHECK_LARGESIZE( stts->size );
    return stts->size;
}

static uint64_t isom_update_ctts_size( isom_ctts_t *ctts )
{
    if( !ctts || !ctts->list )
        return 0;
    ctts->size = ISOM_LIST_FULLBOX_COMMON_SIZE + (uint64_t)ctts->list->entry_count * 8;
    CHECK_LARGESIZE( ctts->size );
    return ctts->size;
}

static uint64_t isom_update_cslg_size( isom_cslg_t *cslg )
{
    if( !cslg )
        return 0;
    cslg->size = ISOM_FULLBOX_COMMON_SIZE + 20;
    CHECK_LARGESIZE( cslg->size );
    return cslg->size;
}

static uint64_t isom_update_stsz_size( isom_stsz_t *stsz )
{
    if( !stsz )
        return 0;
    stsz->size = ISOM_FULLBOX_COMMON_SIZE + 8 + ( stsz->list ? (uint64_t)stsz->list->entry_count * 4 : 0 );
    CHECK_LARGESIZE( stsz->size );
    return stsz->size;
}

static uint64_t isom_update_stss_size( isom_stss_t *stss )
{
    if( !stss || !stss->list )
        return 0;
    stss->size = ISOM_LIST_FULLBOX_COMMON_SIZE + (uint64_t)stss->list->entry_count * 4;
    CHECK_LARGESIZE( stss->size );
    return stss->size;
}

static uint64_t isom_update_stps_size( isom_stps_t *stps )
{
    if( !stps || !stps->list )
        return 0;
    stps->size = ISOM_LIST_FULLBOX_COMMON_SIZE + (uint64_t)stps->list->entry_count * 4;
    CHECK_LARGESIZE( stps->size );
    return stps->size;
}

static uint64_t isom_update_sdtp_size( isom_sdtp_t *sdtp )
{
    if( !sdtp || !sdtp->list )
        return 0;
    sdtp->size = ISOM_FULLBOX_COMMON_SIZE + (uint64_t)sdtp->list->entry_count;
    CHECK_LARGESIZE( sdtp->size );
    return sdtp->size;
}

static uint64_t isom_update_stsc_size( isom_stsc_t *stsc )
{
    if( !stsc || !stsc->list )
        return 0;
    stsc->size = ISOM_LIST_FULLBOX_COMMON_SIZE + (uint64_t)stsc->list->entry_count * 12;
    CHECK_LARGESIZE( stsc->size );
    return stsc->size;
}

static uint64_t isom_update_stco_size( isom_stco_t *stco )
{
    if( !stco || !stco->list )
        return 0;
    stco->size = ISOM_LIST_FULLBOX_COMMON_SIZE + (uint64_t)stco->list->entry_count * (stco->large_presentation ? 8 : 4);
    CHECK_LARGESIZE( stco->size );
    return stco->size;
}

static uint64_t isom_update_sbgp_size( isom_sbgp_entry_t *sbgp )
{
    if( !sbgp || !sbgp->list )
        return 0;
    sbgp->size = ISOM_LIST_FULLBOX_COMMON_SIZE + 4 + (uint64_t)sbgp->list->entry_count * 8;
    CHECK_LARGESIZE( sbgp->size );
    return sbgp->size;
}

static uint64_t isom_update_sgpd_size( isom_sgpd_entry_t *sgpd )
{
    if( !sgpd || !sgpd->list )
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
    CHECK_LARGESIZE( sgpd->size );
    return sgpd->size;
}

static uint64_t isom_update_stbl_size( isom_stbl_t *stbl )
{
    if( !stbl )
        return 0;
    stbl->size = ISOM_BASEBOX_COMMON_SIZE
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
    minf->size = ISOM_BASEBOX_COMMON_SIZE
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
    mdia->size = ISOM_BASEBOX_COMMON_SIZE
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
    chpl->size = ISOM_FULLBOX_COMMON_SIZE + 4 * (chpl->version == 1) + 1;
    for( lsmash_entry_t *entry = chpl->list->head; entry; entry = entry->next )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        chpl->size += 9 + data->chapter_name_length;
    }
    CHECK_LARGESIZE( chpl->size );
    return chpl->size;
}

static uint64_t isom_update_mean_size( isom_mean_t *mean )
{
    if( !mean )
        return 0;
    mean->size = ISOM_FULLBOX_COMMON_SIZE + mean->meaning_string_length;
    CHECK_LARGESIZE( mean->size );
    return mean->size;
}

static uint64_t isom_update_name_size( isom_name_t *name )
{
    if( !name )
        return 0;
    name->size = ISOM_FULLBOX_COMMON_SIZE + name->name_length;
    CHECK_LARGESIZE( name->size );
    return name->size;
}

static uint64_t isom_update_data_size( isom_data_t *data )
{
    if( !data )
        return 0;
    data->size = ISOM_BASEBOX_COMMON_SIZE + 8 + data->value_length;
    CHECK_LARGESIZE( data->size );
    return data->size;
}

static uint64_t isom_update_metaitem_size( isom_metaitem_t *metaitem )
{
    if( !metaitem )
        return 0;
    metaitem->size = ISOM_BASEBOX_COMMON_SIZE
        + isom_update_mean_size( metaitem->mean )
        + isom_update_name_size( metaitem->name )
        + isom_update_data_size( metaitem->data );
    CHECK_LARGESIZE( metaitem->size );
    return metaitem->size;
}

static uint64_t isom_update_ilst_size( isom_ilst_t *ilst )
{
    if( !ilst )
        return 0;
    ilst->size = ISOM_BASEBOX_COMMON_SIZE;
    for( lsmash_entry_t *entry = ilst->item_list->head; entry; entry = entry->next )
        ilst->size += isom_update_metaitem_size( (isom_metaitem_t *)entry->data );
    CHECK_LARGESIZE( ilst->size );
    return ilst->size;
}

static uint64_t isom_update_meta_size( isom_meta_t *meta )
{
    if( !meta )
        return 0;
    meta->size = ISOM_FULLBOX_COMMON_SIZE
        + isom_update_hdlr_size( meta->hdlr )
        + isom_update_dinf_size( meta->dinf )
        + isom_update_ilst_size( meta->ilst );
    CHECK_LARGESIZE( meta->size );
    return meta->size;
}

static uint64_t isom_update_cprt_size( isom_cprt_t *cprt )
{
    if( !cprt )
        return 0;
    cprt->size = ISOM_FULLBOX_COMMON_SIZE + 2 + cprt->notice_length;
    CHECK_LARGESIZE( cprt->size );
    return cprt->size;
}

static uint64_t isom_update_udta_size( isom_udta_t *udta_moov, isom_udta_t *udta_trak )
{
    isom_udta_t *udta = udta_trak ? udta_trak : udta_moov ? udta_moov : NULL;
    if( !udta )
        return 0;
    udta->size = ISOM_BASEBOX_COMMON_SIZE
        + (udta_moov ? isom_update_chpl_size( udta->chpl ) : 0)
        + isom_update_meta_size( udta->meta );
    if( udta->cprt_list )
        for( lsmash_entry_t *entry = udta->cprt_list->head; entry; entry = entry->next )
            udta->size += isom_update_cprt_size( (isom_cprt_t *)entry->data );
    CHECK_LARGESIZE( udta->size );
    return udta->size;
}

static uint64_t isom_update_trak_entry_size( isom_trak_entry_t *trak )
{
    if( !trak )
        return 0;
    trak->size = ISOM_BASEBOX_COMMON_SIZE
        + isom_update_tkhd_size( trak->tkhd )
        + isom_update_tapt_size( trak->tapt )
        + isom_update_edts_size( trak->edts )
        + isom_update_tref_size( trak->tref )
        + isom_update_mdia_size( trak->mdia )
        + isom_update_udta_size( NULL, trak->udta )
        + isom_update_meta_size( trak->meta );
    CHECK_LARGESIZE( trak->size );
    return trak->size;
}

static uint64_t isom_update_mehd_size( isom_mehd_t *mehd )
{
    if( !mehd )
        return 0;
    if( mehd->fragment_duration > UINT32_MAX )
        mehd->version = 1;
    mehd->size = ISOM_FULLBOX_COMMON_SIZE + 4 * (1 + (mehd->version == 1));
    CHECK_LARGESIZE( mehd->size );
    return mehd->size;
}

static uint64_t isom_update_trex_entry_size( isom_trex_entry_t *trex )
{
    if( !trex )
        return 0;
    trex->size = ISOM_FULLBOX_COMMON_SIZE + 20;
    CHECK_LARGESIZE( trex->size );
    return trex->size;
}

static uint64_t isom_update_mvex_size( isom_mvex_t *mvex )
{
    if( !mvex )
        return 0;
    mvex->size = ISOM_BASEBOX_COMMON_SIZE;
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
    moov->size = ISOM_BASEBOX_COMMON_SIZE
        + isom_update_mvhd_size( moov->mvhd )
        + isom_update_iods_size( moov->iods )
        + isom_update_udta_size( moov->udta, NULL )
        + isom_update_meta_size( moov->meta )
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
    mfhd->size = ISOM_FULLBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( mfhd->size );
    return mfhd->size;
}

static uint64_t isom_update_tfhd_size( isom_tfhd_t *tfhd )
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
    CHECK_LARGESIZE( tfhd->size );
    return tfhd->size;
}

static uint64_t isom_update_trun_entry_size( isom_trun_entry_t *trun )
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
    CHECK_LARGESIZE( trun->size );
    return trun->size;
}

static uint64_t isom_update_traf_entry_size( isom_traf_entry_t *traf )
{
    if( !traf )
        return 0;
    traf->size = ISOM_BASEBOX_COMMON_SIZE
               + isom_update_tfhd_size( traf->tfhd )
               + isom_update_sdtp_size( traf->sdtp );
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
    moof->size = ISOM_BASEBOX_COMMON_SIZE + isom_update_mfhd_size( moof->mfhd );
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
    tfra->size = ISOM_FULLBOX_COMMON_SIZE + 12;
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
    mfro->size = ISOM_FULLBOX_COMMON_SIZE + 4;
    CHECK_LARGESIZE( mfro->size );
    return mfro->size;
}

static int isom_update_mfra_size( isom_mfra_t *mfra )
{
    if( !mfra )
        return -1;
    mfra->size = ISOM_BASEBOX_COMMON_SIZE;
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

uint32_t lsmash_create_track( lsmash_root_t *root, lsmash_media_type media_type )
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
    if( isom_add_hdlr( trak->mdia, NULL, NULL, media_type ) )
        return 0;
    if( root->qt_compatible && isom_add_hdlr( NULL, NULL, trak->mdia->minf, QT_REFERENCE_HANDLER_TYPE_URL ) )
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
            if( root->qt_compatible || root->itunes_movie )
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

uint32_t lsmash_get_track_ID( lsmash_root_t *root, uint32_t track_number )
{
    if( !root || !root->moov )
        return 0;
    isom_trak_entry_t *trak = (isom_trak_entry_t *)lsmash_get_entry_data( root->moov->trak_list, track_number );
    if( !trak || !trak->tkhd )
        return 0;
    return trak->tkhd->track_ID;
}

void lsmash_initialize_track_parameters( lsmash_track_parameters_t *param )
{
    memset( param, 0, sizeof(lsmash_track_parameters_t) );
    param->audio_volume = 0x0100;
    param->matrix[0] = 0x00010000;
    param->matrix[4] = 0x00010000;
    param->matrix[8] = 0x40000000;
}

int lsmash_set_track_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_track_parameters_t *param )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->hdlr || !root->moov->mvhd )
        return -1;
    /* Prepare Track Aperture Modes if required. */
    if( root->qt_compatible && param->aperture_modes )
    {
        if( !trak->tapt && isom_add_tapt( trak ) )
            return -1;
        isom_tapt_t *tapt = trak->tapt;
        if( (!tapt->clef && isom_add_clef( tapt ))
         || (!tapt->prof && isom_add_prof( tapt ))
         || (!tapt->enof && isom_add_enof( tapt )) )
            return -1;
    }
    else
        isom_remove_tapt( trak->tapt );
    /* Set up Track Header. */
    uint32_t media_type = trak->mdia->hdlr->componentSubtype;
    isom_tkhd_t *tkhd = trak->tkhd;
    tkhd->flags    = param->mode;
    tkhd->track_ID = param->track_ID ? param->track_ID : tkhd->track_ID;
    tkhd->duration = !trak->edts || !trak->edts->elst ? param->duration : tkhd->duration;
    /* Template fields
     *   alternate_group, layer, volume and matrix
     * According to 14496-14, these value are all set to defaut values in 14496-12.
     * And when a file is read as an MPEG-4 file, these values shall be ignored.
     * If a file complies with other specifications, then those fields may have non-default values
     * as required by those other specifications. */
    if( param->alternate_group )
    {
        if( root->qt_compatible || root->itunes_movie || root->max_3gpp_version >= 4 )
            tkhd->alternate_group = param->alternate_group;
        else
        {
            tkhd->alternate_group = 0;
            lsmash_log( LSMASH_LOG_WARNING, "alternate_group is specified but not compatible with any of the brands. It won't be set.\n" );
        }
    }
    else
        tkhd->alternate_group = 0;
    if( root->qt_compatible || root->itunes_movie )
    {
        tkhd->layer  = media_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK ? param->video_layer  : 0;
        tkhd->volume = media_type == ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK ? param->audio_volume : 0;
        if( media_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK )
            for( int i = 0; i < 9; i++ )
                tkhd->matrix[i] = param->matrix[i];
        else
            for( int i = 0; i < 9; i++ )
                tkhd->matrix[i] = 0;
    }
    else
    {
        tkhd->layer     = 0;
        tkhd->volume    = media_type == ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK ? 0x0100 : 0;
        tkhd->matrix[0] = 0x00010000;
        tkhd->matrix[1] = tkhd->matrix[2] = tkhd->matrix[3] = 0;
        tkhd->matrix[4] = 0x00010000;
        tkhd->matrix[5] = tkhd->matrix[6] = tkhd->matrix[7] = 0;
        tkhd->matrix[8] = 0x40000000;
    }
    /* visual presentation size */
    tkhd->width  = media_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK ? param->display_width  : 0;
    tkhd->height = media_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK ? param->display_height : 0;
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
    for( int i = 0; i < 9; i++ )
        param->matrix[i]   = tkhd->matrix[i];
    param->display_width   = tkhd->width;
    param->display_height  = tkhd->height;
    param->aperture_modes  = !!trak->tapt;
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
     || !trak->mdia->minf->stbl->stts->list->tail || !trak->mdia->minf->stbl->stts->list->tail->data )
        return 0;
    return ((isom_stts_entry_t *)trak->mdia->minf->stbl->stts->list->tail->data)->sample_delta;
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

uint32_t lsmash_get_composition_to_decode_shift( lsmash_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return 0;
    uint32_t sample_count = isom_get_sample_count( trak );
    if( sample_count == 0 )
        return 0;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    if( !stbl->stts || !stbl->stts->list || !stbl->ctts || !stbl->ctts->list )
        return 0;
    if( !(root->max_isom_version >= 4 && stbl->ctts->version == 1) && !root->qt_compatible )
        return 0;   /* This movie shall not have composition to decode timeline shift. */
    lsmash_entry_t *stts_entry = stbl->stts->list->head;
    lsmash_entry_t *ctts_entry = stbl->ctts->list->head;
    if( !stts_entry || !ctts_entry )
        return 0;
    uint64_t dts = 0;
    uint64_t cts = 0;
    uint32_t ctd_shift = 0;
    uint32_t i = 0;
    uint32_t j = 0;
    for( uint32_t k = 0; k < sample_count; i++ )
    {
        isom_stts_entry_t *stts_data = (isom_stts_entry_t *)stts_entry->data;
        isom_ctts_entry_t *ctts_data = (isom_ctts_entry_t *)ctts_entry->data;
        if( !stts_data || !ctts_data )
            return 0;
        cts = dts + (int32_t)ctts_data->sample_offset;
        if( dts > cts + ctd_shift )
            ctd_shift = dts - cts;
        dts += stts_data->sample_delta;
        if( ++i == stts_data->sample_count )
        {
            stts_entry = stts_entry->next;
            if( !stts_entry )
                return 0;
            i = 0;
        }
        if( ++j == ctts_data->sample_count )
        {
            ctts_entry = ctts_entry->next;
            if( !ctts_entry )
                return 0;
            j = 0;
        }
    }
    return ctd_shift;
}

uint16_t lsmash_pack_iso_language( char *iso_language )
{
    if( !iso_language || strlen( iso_language ) != 3 )
        return 0;
    return (uint16_t)LSMASH_PACK_ISO_LANGUAGE( iso_language[0], iso_language[1], iso_language[2] );
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

static int isom_set_media_language( lsmash_root_t *root, uint32_t track_ID, uint16_t ISO_language, uint16_t MAC_language )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return -1;
    uint16_t language = 0;
    if( root->isom_compatible )
    {
        if( ISO_language )
            language = ISO_language;
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
        if( ISO_language )
        {
            if( isom_iso2mac_language( ISO_language, &language ) )
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

static int isom_create_grouping( isom_trak_entry_t *trak, isom_grouping_type grouping_type )
{
    lsmash_root_t *root = trak->root;
    switch( grouping_type )
    {
        case ISOM_GROUP_TYPE_RAP :
            assert( root->max_isom_version >= 6 );
            break;
        case ISOM_GROUP_TYPE_ROLL :
            assert( root->avc_extensions || root->qt_compatible );
            break;
        default :
            assert( 0 );
            break;
    }
    if( !isom_add_sgpd( trak->mdia->minf->stbl, grouping_type )
     || !isom_add_sbgp( trak->mdia->minf->stbl, grouping_type ) )
        return -1;
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
    if( !trak || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    trak->mdia->mdhd->timescale = param->timescale;
    if( isom_set_media_language( root, track_ID, param->ISO_language, param->MAC_language ) )
        return -1;
    if( param->media_handler_name
     && isom_set_media_handler_name( root, track_ID, param->media_handler_name ) )
        return -1;
    if( root->qt_compatible && param->data_handler_name
     && isom_set_data_handler_name( root, track_ID, param->data_handler_name ) )
        return -1;
    if( (root->avc_extensions || root->qt_compatible) && param->roll_grouping
     && isom_create_grouping( trak, ISOM_GROUP_TYPE_ROLL ) )
        return -1;
    if( (root->max_isom_version >= 6) && param->rap_grouping
     && isom_create_grouping( trak, ISOM_GROUP_TYPE_RAP ) )
        return -1;
    return 0;
}

int lsmash_get_media_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_media_parameters_t *param )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->hdlr
     || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_sbgp_entry_t *sbgp;
    isom_sgpd_entry_t *sgpd;
    param->timescale    = mdhd->timescale;
    param->handler_type = trak->mdia->hdlr->componentSubtype;
    param->duration     = mdhd->duration;
    /* Whether sample grouping present. */
    sbgp = isom_get_sample_to_group( stbl, ISOM_GROUP_TYPE_ROLL );
    sgpd = isom_get_sample_group_description( stbl, ISOM_GROUP_TYPE_ROLL );
    param->roll_grouping = sbgp && sgpd;
    sbgp = isom_get_sample_to_group( stbl, ISOM_GROUP_TYPE_RAP );
    sgpd = isom_get_sample_group_description( stbl, ISOM_GROUP_TYPE_RAP );
    param->rap_grouping = sbgp && sgpd;
    /* Get media language. */
    if( mdhd->language >= 0x800 )
    {
        param->MAC_language = 0;
        param->ISO_language = mdhd->language;
    }
    else
    {
        param->MAC_language = mdhd->language;
        param->ISO_language = 0;
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

/*---- movie manipulators ----*/

lsmash_root_t *lsmash_open_movie( const char *filename, lsmash_file_mode mode )
{
    if( !filename )
        return NULL;
    char open_mode[4] = { 0 };
    if( mode & LSMASH_FILE_MODE_WRITE )
        memcpy( open_mode, "w+b", 4 );
#ifdef LSMASH_DEMUXER_ENABLED
    else if( mode & LSMASH_FILE_MODE_READ )
        memcpy( open_mode, "rb", 3 );
#endif
    if( !open_mode[0] )
        return NULL;
    lsmash_root_t *root = lsmash_malloc_zero( sizeof(lsmash_root_t) );
    if( !root )
        return NULL;
    root->root = root;
    root->bs = lsmash_malloc_zero( sizeof(lsmash_bs_t) );
    if( !root->bs )
        goto fail;
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
    if( mode & LSMASH_FILE_MODE_WRITE )
    {
        if( isom_add_moov( root ) || isom_add_mvhd( root->moov ) )
            goto fail;
        root->qt_compatible = 1;    /* QTFF is default file format. */
    }
#ifdef LSMASH_DEMUXER_ENABLED
    if( (mode & (LSMASH_FILE_MODE_READ | LSMASH_FILE_MODE_DUMP)) )
    {
        if( isom_read_root( root ) )
            goto fail;
        root->max_read_size = 4 * 1024 * 1024;
    }
#endif
    if( mode & LSMASH_FILE_MODE_FRAGMENTED )
    {
        root->fragment = lsmash_malloc_zero( sizeof(isom_fragment_manager_t) );
        if( !root->fragment )
            goto fail;
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

static int isom_set_brands( lsmash_root_t *root, lsmash_brand_type major_brand, uint32_t minor_version, lsmash_brand_type *brands, uint32_t brand_count )
{
    if( brand_count > 50 )
        return -1;      /* We support setting brands up to 50. */
    if( !brand_count )
    {
        /* Absence of File Type Box means this file is a QuickTime or MP4 version 1 format file. */
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
    lsmash_brand_type *compatible_brands;
    if( !ftyp->compatible_brands )
        compatible_brands = malloc( brand_count * sizeof(uint32_t) );
    else
        compatible_brands = realloc( ftyp->compatible_brands, brand_count * sizeof(uint32_t) );
    if( !compatible_brands )
        return -1;
    ftyp->compatible_brands = compatible_brands;
    for( uint32_t i = 0; i < brand_count; i++ )
    {
        ftyp->compatible_brands[i] = brands[i];
        ftyp->size += 4;
    }
    ftyp->brand_count = brand_count;
    return isom_check_compatibility( root );
}

void lsmash_initialize_movie_parameters( lsmash_movie_parameters_t *param )
{
    memset( param, 0, sizeof(lsmash_movie_parameters_t) );
    param->max_chunk_duration  = 0.5;
    param->max_async_tolerance = 2.0;
    param->max_chunk_size      = 4 * 1024 * 1024;
    param->max_read_size       = 4 * 1024 * 1024;
    param->timescale           = 600;
    param->playback_rate       = 0x00010000;
    param->playback_volume     = 0x0100;
}

int lsmash_set_movie_parameters( lsmash_root_t *root, lsmash_movie_parameters_t *param )
{
    if( !root || !root->moov || !root->moov->mvhd
     || isom_set_brands( root, param->major_brand, param->minor_version, param->brands, param->number_of_brands ) )
        return -1;
    isom_mvhd_t *mvhd = root->moov->mvhd;
    root->max_chunk_duration  = param->max_chunk_duration;
    root->max_async_tolerance = LSMASH_MAX( param->max_async_tolerance, 2 * param->max_chunk_duration );
    root->max_chunk_size      = param->max_chunk_size;
    root->max_read_size       = param->max_read_size;
    mvhd->timescale           = param->timescale;
    if( root->qt_compatible || root->itunes_movie )
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
    if( root->ftyp )
    {
        isom_ftyp_t *ftyp = root->ftyp;
        uint32_t brand_count = LSMASH_MIN( ftyp->brand_count, 50 );     /* brands up to 50 */
        for( uint32_t i = 0; i < brand_count; i++ )
            param->brands_shadow[i] = ftyp->compatible_brands[i];
        param->major_brand      = ftyp->major_brand;
        param->brands           = param->brands_shadow;
        param->number_of_brands = brand_count;
        param->minor_version    = ftyp->minor_version;
    }
    param->max_chunk_duration  = root->max_chunk_duration;
    param->max_async_tolerance = root->max_async_tolerance;
    param->max_chunk_size      = root->max_chunk_size;
    param->max_read_size       = root->max_read_size;
    param->timescale           = mvhd->timescale;
    param->duration            = mvhd->duration;
    param->playback_rate       = mvhd->rate;
    param->playback_volume     = mvhd->volume;
    param->preview_time        = mvhd->previewTime;
    param->preview_duration    = mvhd->previewDuration;
    param->poster_time         = mvhd->posterTime;
    param->number_of_tracks    = root->moov->trak_list ? root->moov->trak_list->entry_count : 0;
    return 0;
}

uint32_t lsmash_get_movie_timescale( lsmash_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return 0;
    return root->moov->mvhd->timescale;
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
            lsmash_edit_t edit;
            edit.duration   = LSMASH_MIN( trak->tkhd->duration, lsmash_get_track_duration( root, related_track_ID ) );
            edit.start_time = 0;
            edit.rate       = ISOM_EDIT_MODE_NORMAL;
            if( lsmash_create_explicit_timeline_map( root, track_ID, edit ) )
                return -1;
        }
        /* Add stss box if any samples aren't sync sample. */
        isom_stbl_t *stbl = trak->mdia->minf->stbl;
        if( !trak->cache->all_sync && !stbl->stss && isom_add_stss( stbl ) )
            return -1;
        if( isom_update_bitrate_description( trak->mdia ) )
            return -1;
    }
    if( root->mp4_version1 == 1 && isom_add_iods( moov ) )
        return -1;
    if( isom_check_mandatory_boxes( root )
     || isom_set_movie_creation_time( root )
     || isom_update_moov_size( moov )
     || isom_write_mdat_size( root ) )
        return -1;

    lsmash_bs_t *bs = root->bs;
    uint64_t meta_size = root->meta ? root->meta->size : 0;
    if( !remux )
    {
        if( isom_write_moov( root )
         || isom_write_meta( bs, root->meta ) )
            return -1;
        root->size += moov->size + meta_size;
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
         || (((isom_stco_entry_t*)stco->list->tail->data)->chunk_offset + moov->size + meta_size) <= UINT32_MAX )
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
    uint64_t mtf_size = moov->size + meta_size;     /* sum of size of boxes moved to front */

    /* buffer size must be at least mtf_size * 2 */
    remux->buffer_size = LSMASH_MAX( remux->buffer_size, mtf_size * 2 );

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
                ((isom_co64_entry_t*)co64_entry->data)->chunk_offset += mtf_size;
        else
            for( lsmash_entry_t* stco_entry = stco->list->head ; stco_entry ; stco_entry = stco_entry->next )
                ((isom_stco_entry_t*)stco_entry->data)->chunk_offset += mtf_size;
    }

    FILE *stream = bs->stream;
    isom_mdat_t *mdat = root->mdat;
    uint64_t total = root->size + mtf_size;
    uint64_t readnum;
    /* backup starting area of mdat and write moov + meta there instead */
    if( lsmash_fseek( stream, mdat->placeholder_pos, SEEK_SET ) )
        goto fail;
    readnum = fread( buf[0], 1, size, stream );
    uint64_t read_pos = lsmash_ftell( stream );

    /* write moov + meta there instead */
    if( lsmash_fseek( stream, mdat->placeholder_pos, SEEK_SET )
     || isom_write_moov( root )
     || isom_write_meta( bs, root->meta ) )
        goto fail;
    uint64_t write_pos = lsmash_ftell( stream );

    mdat->placeholder_pos += mtf_size; /* update placeholder */

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

    root->size += mtf_size;
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
        trex->default_sample_flags.sample_is_non_sync_sample = !trak->cache->all_sync;
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
        isom_sample_pool_t *pool = (isom_sample_pool_t *)entry->data;
        if( !pool )
            return -1;
        lsmash_bs_put_bytes( root->bs, pool->size, pool->data );
    }
    if( lsmash_bs_write_data( root->bs ) )
        return -1;
    root->size += root->mdat->size;
    lsmash_remove_entries( fragment->pool, isom_remove_sample_pool );
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
        if( isom_update_bitrate_description( trak->mdia ) )
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
    uint64_t meta_size = root->meta ? root->meta->size : 0;
    for( lsmash_entry_t* entry = moov->trak_list->head; entry; )
    {
        isom_trak_entry_t* trak = (isom_trak_entry_t*)entry->data;
        isom_stco_t* stco = trak->mdia->minf->stbl->stco;
        if( !stco->list->tail   /* no samples */
         || stco->large_presentation
         || (((isom_stco_entry_t*)stco->list->tail->data)->chunk_offset + moov->size + meta_size) <= UINT32_MAX )
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
    uint64_t preceding_size = moov->size + meta_size;
    for( lsmash_entry_t* entry = moov->trak_list->head; entry; entry = entry->next )
    {
        isom_stco_t* stco = ((isom_trak_entry_t*)entry->data)->mdia->minf->stbl->stco;
        if( stco->large_presentation )
            for( lsmash_entry_t* co64_entry = stco->list->head ; co64_entry ; co64_entry = co64_entry->next )
                ((isom_co64_entry_t*)co64_entry->data)->chunk_offset += preceding_size;
        else
            for( lsmash_entry_t* stco_entry = stco->list->head ; stco_entry ; stco_entry = stco_entry->next )
                ((isom_stco_entry_t*)stco_entry->data)->chunk_offset += preceding_size;
    }
    /* Write File Type Box here if it was not written yet. */
    if( !root->file_type_written && isom_write_ftyp( root ) )
        return -1;
    /* Write Movie Box. */
    if( isom_write_moov( root )
     || isom_write_meta( root->bs, root->meta ) )
        return -1;
    root->size += preceding_size;
    /* Output samples. */
    return isom_output_fragment_media_data( root );
}

/* Return 1 if there is diffrence, otherwise return 0. */
static int isom_compare_sample_flags( isom_sample_flags_t *a, isom_sample_flags_t *b )
{
    return (a->reserved                    != b->reserved)
        || (a->is_leading                  != b->is_leading)
        || (a->sample_depends_on           != b->sample_depends_on)
        || (a->sample_is_depended_on       != b->sample_is_depended_on)
        || (a->sample_has_redundancy       != b->sample_has_redundancy)
        || (a->sample_padding_value        != b->sample_padding_value)
        || (a->sample_is_non_sync_sample   != b->sample_is_non_sync_sample)
        || (a->sample_degradation_priority != b->sample_degradation_priority);
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
            uint32_t is_leading               [4];
            uint32_t sample_depends_on        [4];
            uint32_t sample_is_depended_on    [4];
            uint32_t sample_has_redundancy    [4];
            uint32_t sample_is_non_sync_sample[2];
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
                    ++ stats.is_leading               [ sample_flags->is_leading                ];
                    ++ stats.sample_depends_on        [ sample_flags->sample_depends_on         ];
                    ++ stats.sample_is_depended_on    [ sample_flags->sample_is_depended_on     ];
                    ++ stats.sample_has_redundancy    [ sample_flags->sample_has_redundancy     ];
                    ++ stats.sample_is_non_sync_sample[ sample_flags->sample_is_non_sync_sample ];
                }
            }
            else
            {
                sample_flags = &tfhd->default_sample_flags;
                stats.is_leading               [ sample_flags->is_leading                ] += trun->sample_count;
                stats.sample_depends_on        [ sample_flags->sample_depends_on         ] += trun->sample_count;
                stats.sample_is_depended_on    [ sample_flags->sample_is_depended_on     ] += trun->sample_count;
                stats.sample_has_redundancy    [ sample_flags->sample_has_redundancy     ] += trun->sample_count;
                stats.sample_is_non_sync_sample[ sample_flags->sample_is_non_sync_sample ] += trun->sample_count;
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
                GET_MOST_USED( tfhd, 4, sample_is_non_sync_sample );
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
                /* It is enough to check only if first_sample_flags equals default_sample_flags or not.
                 * If it is equal, just use default_sample_flags.
                 * If not, just use first_sample_flags of this run. */
                if( !isom_compare_sample_flags( &trun->first_sample_flags, &tfhd->default_sample_flags ) )
                    useful_first_sample_flags = 0;
            }
            else if( trun->optional && trun->optional->head )
            {
                lsmash_entry_t *optional_entry = trun->optional->head->next;
                isom_trun_optional_row_t *row = (isom_trun_optional_row_t *)optional_entry->data;
                isom_sample_flags_t representative_sample_flags = row->sample_flags;
                if( isom_compare_sample_flags( &tfhd->default_sample_flags, &representative_sample_flags ) )
                    useful_default_sample_flags = 0;
                if( !isom_compare_sample_flags( &trun->first_sample_flags, &representative_sample_flags ) )
                    useful_first_sample_flags = 0;
                if( useful_default_sample_flags )
                    for( optional_entry = optional_entry->next; optional_entry; optional_entry = optional_entry->next )
                    {
                        row = (isom_trun_optional_row_t *)optional_entry->data;
                        if( isom_compare_sample_flags( &representative_sample_flags, &row->sample_flags ) )
                        {
                            useful_default_sample_flags = 0;
                            break;
                        }
                    }
            }
            if( useful_default_sample_flags )
            {
                tfhd->flags |= ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT;
                trun->flags &= ~ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT;
            }
            else
            {
                useful_first_sample_flags = 0;
                trun->flags |= ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT;
            }
            if( useful_first_sample_flags )
                trun->flags |= ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT;
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
        else if( !isom_compare_sample_flags( &tfhd->default_sample_flags, &trex->default_sample_flags ) )
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
        traf->tfhd->base_data_offset = root->size + moof->size + ISOM_BASEBOX_COMMON_SIZE;
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
    /* Update the last sample_duration if needed. */
    isom_trun_entry_t *trun = (isom_trun_entry_t *)traf->trun_list->tail->data;
    if( trun->sample_count == 1 && traf->trun_list->entry_count == 1 )
    {
        isom_trex_entry_t *trex = isom_get_trex( traf->root->moov->mvex, tfhd->track_ID );
        if( !trex )
            return -1;
        if( last_duration != trex->default_sample_duration )
            tfhd->flags |= ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT;
        tfhd->default_sample_duration = last_duration;
    }
    else if( last_duration != tfhd->default_sample_duration )
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

void lsmash_discard_boxes( lsmash_root_t *root )
{
    if( !root )
        return;
    isom_remove_ftyp( root->ftyp );
    isom_remove_moov( root );
    lsmash_remove_list( root->moof_list, isom_remove_moof );
    isom_remove_mdat( root->mdat );
    isom_remove_free( root->free );
    isom_remove_meta( root->meta );
    isom_remove_mfra( root->mfra );
    root->ftyp = NULL;
    root->moov = NULL;
    root->moof_list = NULL;
    root->mdat = NULL;
    root->free = NULL;
    root->mfra = NULL;
}

void lsmash_destroy_root( lsmash_root_t *root )
{
    if( !root )
        return;
#ifdef LSMASH_DEMUXER_ENABLED
    isom_remove_print_funcs( root );
    isom_remove_timelines( root );
#endif
    lsmash_discard_boxes( root );
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

int lsmash_modify_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID, uint32_t edit_number, lsmash_edit_t edit )
{
    if( !edit.duration || edit.start_time < -1 )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->edts || !trak->edts->elst || !trak->edts->elst->list )
        return -1;
    isom_elst_t *elst = trak->edts->elst;
    isom_elst_entry_t *data = (isom_elst_entry_t *)lsmash_get_entry_data( elst->list, edit_number );
    if( !data )
        return -1;
    data->segment_duration = edit.duration;
    data->media_time       = edit.start_time;
    data->media_rate       = edit.rate;
    if( !elst->pos || !root->fragment || root->bs->stream == stdout )
        return isom_update_tkhd_duration( trak );
    /* Rewrite the specified entry.
     * Note: we don't update the version of the Edit List Box. */
    lsmash_bs_t *bs = root->bs;
    FILE *stream = bs->stream;
    uint64_t current_pos = lsmash_ftell( stream );
    uint64_t entry_pos = elst->pos + ISOM_LIST_FULLBOX_COMMON_SIZE + ((uint64_t)edit_number - 1) * (elst->version == 1 ? 20 : 12);
    lsmash_fseek( stream, entry_pos, SEEK_SET );
    if( elst->version )
    {
        lsmash_bs_put_be64( bs, data->segment_duration );
        lsmash_bs_put_be64( bs, data->media_time );
    }
    else
    {
        lsmash_bs_put_be32( bs, (uint32_t)LSMASH_MIN( data->segment_duration, UINT32_MAX ) );
        lsmash_bs_put_be32( bs, (uint32_t)data->media_time );
    }
    lsmash_bs_put_be32( bs, data->media_rate );
    int ret = lsmash_bs_write_data( bs );
    lsmash_fseek( stream, current_pos, SEEK_SET );
    return ret;
}

int lsmash_create_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID, lsmash_edit_t edit )
{
    if( edit.start_time < -1 )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->tkhd )
        return -1;
    edit.duration = (edit.duration || root->fragment) ? edit.duration
                  : trak->tkhd->duration ? trak->tkhd->duration
                  : isom_update_tkhd_duration( trak ) ? 0
                  : trak->tkhd->duration;
    if( isom_add_edts( trak )
     || isom_add_elst( trak->edts )
     || isom_add_elst_entry( trak->edts->elst, edit.duration, edit.start_time, edit.rate ) )
        return -1;
    return isom_update_tkhd_duration( trak );
}

int lsmash_get_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID, uint32_t edit_number, lsmash_edit_t *edit )
{
    if( !edit )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak )
        return -1;
    if( !trak->edts || !trak->edts->elst )
    {
        /* no edits */
        edit->duration   = 0;
        edit->start_time = 0;
        edit->rate       = 0;
        return 0;
    }
    isom_elst_entry_t *elst = (isom_elst_entry_t *)lsmash_get_entry_data( trak->edts->elst->list, edit_number );
    if( !elst )
        return -1;
    edit->duration   = elst->segment_duration;
    edit->start_time = elst->media_time;
    edit->rate       = elst->media_rate;
    return 0;
}

uint32_t lsmash_count_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->edts || !trak->edts->elst || !trak->edts->elst->list )
        return 0;
    return trak->edts->elst->list->entry_count;
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
    lsmash_sample_t *sample = lsmash_malloc_zero( sizeof(lsmash_sample_t) );
    if( !sample )
        return NULL;
    if( !size )
        return sample;
    sample->data = malloc( size );
    if( !sample->data )
    {
        free( sample );
        return NULL;
    }
    sample->length = size;
    return sample;
}

int lsmash_sample_alloc( lsmash_sample_t *sample, uint32_t size )
{
    if( !sample )
        return -1;
    if( !size )
    {
        if( sample->data )
            free( sample->data );
        sample->data = NULL;
        sample->length = 0;
        return 0;
    }
    if( size == sample->length )
        return 0;
    uint8_t *data;
    if( !sample->data )
        data = malloc( size );
    else
        data = realloc( sample->data, size );
    if( !data )
        return -1;
    sample->data = data;
    sample->length = size;
    return 0;
}

void lsmash_delete_sample( lsmash_sample_t *sample )
{
    if( !sample )
        return;
    if( sample->data )
        free( sample->data );
    free( sample );
}

isom_sample_pool_t *isom_create_sample_pool( uint64_t size )
{
    isom_sample_pool_t *pool = lsmash_malloc_zero( sizeof(isom_sample_pool_t) );
    if( !pool )
        return NULL;
    if( !size )
        return pool;
    pool->data = malloc( size );
    if( !pool->data )
    {
        free( pool );
        return NULL;
    }
    pool->alloc = size;
    return pool;
}

static void isom_remove_sample_pool( isom_sample_pool_t *pool )
{
    if( !pool )
        return;
    if( pool->data )
        free( pool->data );
    free( pool );
}

static uint32_t isom_add_size( isom_trak_entry_t *trak, uint32_t sample_size )
{
    if( isom_add_stsz_entry( trak->mdia->minf->stbl, sample_size ) )
        return 0;
    return isom_get_sample_count( trak );
}

static uint32_t isom_add_dts( isom_stbl_t *stbl, isom_timestamp_t *cache, uint64_t dts )
{
    isom_stts_t *stts = stbl->stts;
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

static int isom_add_cts( isom_stbl_t *stbl, isom_timestamp_t *cache, uint64_t cts )
{
    isom_ctts_t *ctts = stbl->ctts;
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
        ctts = stbl->ctts;
        isom_ctts_entry_t *data = (isom_ctts_entry_t *)ctts->list->head->data;
        uint32_t sample_count = stbl->stsz->sample_count;
        if( sample_count != 1 )
        {
            data->sample_count = sample_count - 1;
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
    if( !trak->cache || !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list )
        return -1;
    lsmash_root_t *root = trak->root;
    if( root->isom_compatible && root->qt_compatible && (cts - dts) > INT32_MAX )
        return -1;      /* sample_offset is not compatible. */
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_timestamp_t *ts_cache = &trak->cache->timestamp;
    uint32_t sample_count = isom_get_sample_count( trak );
    uint32_t sample_delta = sample_count > 1 ? isom_add_dts( stbl, ts_cache, dts ) : 0;
    if( sample_count > 1 && !sample_delta )
        return -1;
    if( isom_add_cts( stbl, ts_cache, cts ) )
        return -1;
    if( (cts + ts_cache->ctd_shift) < dts )
    {
        if( (root->max_isom_version < 4 && !root->qt_compatible)        /* Negative sample offset is not supported. */
         || (root->max_isom_version >= 4 && trak->root->qt_compatible)  /* ctts version 1 is not defined in QTFF. */
         || root->fragment                                              /* Composition time offset is positive. */
         || ((dts - cts) > INT32_MAX) )                                 /* Overflow */
            return -1;
        ts_cache->ctd_shift = dts - cts;
        if( !stbl->ctts->version && !trak->root->qt_compatible )
            stbl->ctts->version = 1;
    }
    if( trak->cache->fragment )
    {
        isom_fragment_t *fragment_cache = trak->cache->fragment;
        fragment_cache->last_duration = sample_delta;
        fragment_cache->largest_cts = LSMASH_MAX( ts_cache->cts, fragment_cache->largest_cts );
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
     && !(prop->random_access_type == ISOM_SAMPLE_RANDOM_ACCESS_TYPE_POST_ROLL && prop->post_roll.identifier == prop->post_roll.complete) )
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
        return isom_add_sdtp_entry( (isom_box_t *)stbl, prop, avc_extensions );
    if( !prop->allow_earlier && !prop->leading && !prop->independent && !prop->disposable && !prop->redundant )  /* no null check for prop */
        return 0;
    if( isom_add_sdtp( (isom_box_t *)stbl ) )
        return -1;
    uint32_t count = isom_get_sample_count( trak );
    /* fill past samples with ISOM_SAMPLE_*_UNKNOWN */
    lsmash_sample_property_t null_prop = { 0 };
    for( uint32_t i = 1; i < count; i++ )
        if( isom_add_sdtp_entry( (isom_box_t *)stbl, &null_prop, avc_extensions ) )
            return -1;
    return isom_add_sdtp_entry( (isom_box_t *)stbl, prop, avc_extensions );
}

static int isom_rap_grouping_established( isom_rap_group_t *group, int num_leading_samples_known, isom_sgpd_entry_t *sgpd )
{
    isom_rap_entry_t *rap = group->random_access;
    if( !rap )
        return 0;
    assert( rap == (isom_rap_entry_t *)sgpd->list->tail->data );
    rap->num_leading_samples_known = num_leading_samples_known;
    /* Avoid duplication of sample group descriptions. */
    uint32_t group_description_index = 1;
    for( lsmash_entry_t *entry = sgpd->list->head; entry != sgpd->list->tail; entry = entry->next )
    {
        isom_rap_entry_t *data = (isom_rap_entry_t *)entry->data;
        if( !data )
            return -1;
        if( rap->num_leading_samples_known == data->num_leading_samples_known
         && rap->num_leading_samples       == data->num_leading_samples )
        {
            /* The same description already exists.
             * Remove the latest random access entry. */
            lsmash_remove_entry_direct( sgpd->list, sgpd->list->tail, NULL );
            /* Replace assigned group_description_index with the one corresponding the same description. */
            if( group->assignment->group_description_index == 0 )
            {
                if( group->prev_assignment )
                    group->prev_assignment->group_description_index = group_description_index;
            }
            else
                group->assignment->group_description_index = group_description_index;
            break;
        }
        ++group_description_index;
    }
    group->random_access = NULL;
    return 0;
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
                  || prop->random_access_type == ISOM_SAMPLE_RANDOM_ACCESS_TYPE_UNKNOWN_RAP
                  || (prop->random_access_type == ISOM_SAMPLE_RANDOM_ACCESS_TYPE_POST_ROLL && prop->post_roll.identifier == prop->post_roll.complete);
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
            group->assignment    = isom_add_group_assignment_entry( sbgp, 1, sgpd->list->entry_count );
        }
        else
        {
            /* The first sample is not always random access point. */
            group->random_access = NULL;
            group->assignment    = isom_add_group_assignment_entry( sbgp, 1, 0 );
        }
        if( !group->assignment )
        {
            free( group );
            return -1;
        }
        group->prev_assignment = NULL;
        group->is_prev_rap     = is_rap;
        trak->cache->rap       = group;
        return 0;
    }
    if( group->is_prev_rap )
    {
        /* OK. here, the previous sample is a menber of 'rap '. */
        if( !is_rap )
        {
            /* This sample isn't a member of 'rap ' and the previous sample is.
             * So we create a new group and set 0 on its group_description_index. */
            group->prev_assignment = group->assignment;
            group->assignment      = isom_add_group_assignment_entry( sbgp, 1, 0 );
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
            if( isom_rap_grouping_established( group, 1, sgpd ) )
                return -1;
            group->random_access   = isom_add_rap_group_entry( sgpd );
            group->prev_assignment = group->assignment;
            group->assignment      = isom_add_group_assignment_entry( sbgp, 1, sgpd->list->entry_count );
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
        if( isom_rap_grouping_established( group, 1, sgpd ) )
            return -1;
        group->random_access   = isom_add_rap_group_entry( sgpd );
        group->prev_assignment = group->assignment;
        group->assignment      = isom_add_group_assignment_entry( sbgp, 1, sgpd->list->entry_count );
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
            if( isom_rap_grouping_established( group, 0, sgpd ) )
                return -1;
        }
        else
        {
            if( prop->leading == ISOM_SAMPLE_IS_UNDECODABLE_LEADING || prop->leading == ISOM_SAMPLE_IS_DECODABLE_LEADING )
                ++ group->random_access->num_leading_samples;
            /* no more consecutive leading samples in this group */
            else if( isom_rap_grouping_established( group, 1, sgpd ) )
                return -1;
        }
    }
    group->is_prev_rap = is_rap;
    return 0;
}

static int isom_roll_grouping_established( isom_roll_group_t *group, int16_t roll_distance, isom_sgpd_entry_t *sgpd )
{
    /* Avoid duplication of sample group descriptions. */
    uint32_t group_description_index = 1;
    for( lsmash_entry_t *entry = sgpd->list->head; entry; entry = entry->next )
    {
        isom_roll_entry_t *data = (isom_roll_entry_t *)entry->data;
        if( !data )
            return -1;
        if( roll_distance == data->roll_distance )
        {
            /* The same description already exists.
             * Set the group_description_index corresponding the same description. */
            group->assignment->group_description_index = group_description_index;
            group->described = 1;
            return 0;
        }
        ++group_description_index;
    }
    /* Add a new roll recovery description. */
    if( !isom_add_roll_group_entry( sgpd, roll_distance ) )
        return -1;
    group->assignment->group_description_index = sgpd->list->entry_count;
    group->described = 1;
    return 0;
}

static int isom_deduplicate_roll_group( isom_sbgp_entry_t *sbgp, lsmash_entry_list_t *pool )
{
    /* Deduplication */
    uint32_t current_group_number = sbgp->list->entry_count - pool->entry_count + 1;
    isom_group_assignment_entry_t *prev_assignment = (isom_group_assignment_entry_t *)lsmash_get_entry_data( sbgp->list, current_group_number - 1 );
    for( lsmash_entry_t *entry = pool->head; entry; )
    {
        isom_roll_group_t *group = (isom_roll_group_t *)entry->data;
        if( !group || !group->assignment )
            return -1;
        if( !group->delimited || !group->described )
            return 0;
        if( prev_assignment && prev_assignment->group_description_index == group->assignment->group_description_index )
        {
            /* Merge the current group with the previous. */
            lsmash_entry_t *next_entry = entry->next;
            prev_assignment->sample_count += group->assignment->sample_count;
            if( lsmash_remove_entry( sbgp->list, current_group_number, NULL )
             || lsmash_remove_entry_direct( pool, entry, NULL ) )
                return -1;
            entry = next_entry;
        }
        else
        {
            entry = entry->next;
            prev_assignment = group->assignment;
            ++current_group_number;
        }
    }
    return 0;
}

/* Remove pooled caches that has become unnecessary. */
static int isom_clean_roll_pool( isom_sbgp_entry_t *sbgp, lsmash_entry_list_t *pool )
{
    for( lsmash_entry_t *entry = pool->head; entry; entry = pool->head )
    {
        isom_roll_group_t *group = (isom_roll_group_t *)entry->data;
        if( !group )
            return -1;
        if( !group->delimited || !group->described )
            return 0;
        if( lsmash_remove_entry_direct( pool, entry, NULL ) )
            return -1;
    }
    return 0;
}

static int isom_flush_roll_pool( isom_sbgp_entry_t *sbgp, lsmash_entry_list_t *pool )
{
    if( isom_deduplicate_roll_group( sbgp, pool ) )
        return -1;
    return isom_clean_roll_pool( sbgp, pool );
}

static int isom_all_recovery_described( isom_sbgp_entry_t *sbgp, lsmash_entry_list_t *pool )
{
    for( lsmash_entry_t *entry = pool->head; entry; entry = entry->next )
    {
        isom_roll_group_t *group = (isom_roll_group_t *)entry->data;
        if( !group )
            return -1;
        group->described = 1;
    }
    return isom_flush_roll_pool( sbgp, pool );
}

static int isom_group_roll_recovery( isom_trak_entry_t *trak, lsmash_sample_property_t *prop )
{
    if( !trak->root->avc_extensions && !trak->root->qt_compatible )
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
    int is_recovery_start = (prop->random_access_type == ISOM_SAMPLE_RANDOM_ACCESS_TYPE_POST_ROLL);
    int valid_pre_roll = !is_recovery_start && (prop->random_access_type != ISOM_SAMPLE_RANDOM_ACCESS_TYPE_NONE)
                      && (prop->pre_roll.distance > 0) && (prop->pre_roll.distance <= -INT16_MIN);
    int new_group = !group || is_recovery_start || (group->prev_is_recovery_start != is_recovery_start);
    if( !new_group )
    {
        /* Check pre-roll distance. */
        if( !group->assignment )
            return -1;
        isom_roll_entry_t *prev_roll = (isom_roll_entry_t *)lsmash_get_entry_data( sgpd->list, group->assignment->group_description_index );
        if( !prev_roll )
            new_group = valid_pre_roll;
        else if( !valid_pre_roll || (prop->pre_roll.distance != -prev_roll->roll_distance) )
            /* Pre-roll distance is different from the previous. */
            new_group = 1;
    }
    if( new_group )
    {
        if( group )
            group->delimited = 1;
        else
            assert( sample_count == 1 );
        /* Create a new group. */
        group = lsmash_malloc_zero( sizeof(isom_roll_group_t) );
        if( !group )
            return -1;
        group->prev_is_recovery_start = is_recovery_start;
        group->assignment = isom_add_group_assignment_entry( sbgp, 1, 0 );
        if( !group->assignment || lsmash_add_entry( pool, group ) )
        {
            free( group );
            return -1;
        }
        if( is_recovery_start )
        {
            /* a member of non-roll or post-roll group */
            group->first_sample   = sample_count;
            group->recovery_point = prop->post_roll.complete;
        }
        else
        {
            if( valid_pre_roll )
            {
                /* a member of pre-roll group */
                if( isom_roll_grouping_established( group, -prop->pre_roll.distance, sgpd ) )
                    return -1;
            }
            else
                /* a member of non-roll group */
                group->described = 1;
        }
    }
    else
    {
        group->prev_is_recovery_start = is_recovery_start;
        ++ group->assignment->sample_count;
    }
    /* If encountered a sync sample, all recovery is completed here. */
    if( prop->random_access_type == ISOM_SAMPLE_RANDOM_ACCESS_TYPE_CLOSED_RAP )
        return isom_all_recovery_described( sbgp, pool );
    /* Check whether this sample is a random access recovery point or not. */
    for( lsmash_entry_t *entry = pool->head; entry; entry = entry->next )
    {
        group = (isom_roll_group_t *)entry->data;
        if( !group )
            return -1;
        if( group->described )
            continue;
        if( prop->post_roll.identifier == group->recovery_point )
        {
            int16_t distance = sample_count - group->first_sample;
            /* Add a roll recovery entry only when roll_distance isn't zero since roll_distance = 0 must not be used. */
            if( distance )
            {
                /* Now, this group is a 'roll'. */
                if( isom_roll_grouping_established( group, distance, sgpd ) )
                    return -1;
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
            else
                group->described = 1;
            break;      /* Avoid evaluating groups, in the pool, having the same identifier for recovery point again. */
        }
    }
    return isom_flush_roll_pool( sbgp, pool );
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
        current->pool = isom_create_sample_pool( 0 );
        if( !current->pool )
            return -1;
    }
    if( !current->pool->sample_count )
    {
        /* Cannot decide whether we should flush the current sample or not here yet. */
        ++ current->chunk_number;
        current->sample_description_index = sample->index;
        current->first_dts = sample->dts;
        return 0;
    }
    if( sample->dts < current->first_dts )
        return -1;  /* easy error check. */
    if( (root->max_chunk_duration >= ((double)(sample->dts - current->first_dts) / trak->mdia->mdhd->timescale))
     && (root->max_chunk_size >= current->pool->size + sample->length)
     && (current->sample_description_index == sample->index) )
        return 0;   /* No need to flush current cached chunk, the current sample must be put into that. */
    /* NOTE: chunk relative stuff must be pushed into root after a chunk is fully determined with its contents. */
    /* now current cached chunk is fixed, actually add chunk relative properties to root accordingly. */
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    lsmash_entry_t *last_stsc_entry = stbl->stsc->list->tail;
    /* Create a new chunk sequence in this track if needed. */
    if( (!last_stsc_entry || current->pool->sample_count != ((isom_stsc_entry_t *)last_stsc_entry->data)->samples_per_chunk)
     && isom_add_stsc_entry( stbl, current->chunk_number, current->pool->sample_count, current->sample_description_index ) )
        return -1;
    /* Add a new chunk offset in this track. */
    uint64_t offset = root->size;
    if( root->fragment )
        offset += ISOM_BASEBOX_COMMON_SIZE + root->fragment->pool_size;
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

static int isom_write_pooled_samples( lsmash_root_t *root, isom_sample_pool_t *pool )
{
    if( !root || !root->mdat || !root->bs || !root->bs->stream )
        return -1;
    lsmash_bs_put_bytes( root->bs, pool->size, pool->data );
    if( lsmash_bs_write_data( root->bs ) )
        return -1;
    root->mdat->size  += pool->size;
    root->size        += pool->size;
    pool->sample_count = 0;
    pool->size         = 0;
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

static int isom_append_fragment_track_run( lsmash_root_t *root, isom_chunk_t *chunk )
{
    if( !chunk->pool || !chunk->pool->size )
        return 0;
    isom_fragment_manager_t *fragment = root->fragment;
    /* Move data in the pool of the current track fragment to the pool of the current movie fragment.
     * Empty the pool of current track. We don't delete data of samples here. */
    if( lsmash_add_entry( fragment->pool, chunk->pool ) )
        return -1;
    fragment->pool->entry_count += chunk->pool->sample_count;
    fragment->pool_size         += chunk->pool->size;
    chunk->pool = isom_create_sample_pool( chunk->pool->size );
    return chunk->pool ? 0 : -1;
}

static int isom_output_cached_chunk( isom_trak_entry_t *trak )
{
    lsmash_root_t *root = trak->root;
    isom_chunk_t *chunk = &trak->cache->chunk;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    lsmash_entry_t *last_stsc_entry = stbl->stsc->list->tail;
    /* Create a new chunk sequence in this track if needed. */
    if( (!last_stsc_entry || chunk->pool->sample_count != ((isom_stsc_entry_t *)last_stsc_entry->data)->samples_per_chunk)
     && isom_add_stsc_entry( stbl, chunk->chunk_number, chunk->pool->sample_count, chunk->sample_description_index ) )
        return -1;
    if( root->fragment )
    {
        /* Add a new chunk offset in this track. */
        if( isom_add_stco_entry( stbl, root->size + ISOM_BASEBOX_COMMON_SIZE + root->fragment->pool_size ) )
            return -1;
        return isom_append_fragment_track_run( root, chunk );
    }
    /* Add a new chunk offset in this track. */
    if( isom_add_stco_entry( stbl, root->size ) )
        return -1;
    /* Output pooled samples in this track. */
    return isom_write_pooled_samples( root, chunk->pool );
}

static int isom_pool_sample( isom_sample_pool_t *pool, lsmash_sample_t *sample )
{
    uint64_t pool_size = pool->size + sample->length;
    if( pool->alloc < pool_size )
    {
        uint8_t *data;
        uint64_t alloc = pool_size + (1<<16);
        if( !pool->data )
            data = malloc( alloc );
        else
            data = realloc( pool->data, alloc );
        if( !data )
            return -1;
        pool->data = data;
        pool->alloc = alloc;
    }
    memcpy( pool->data + pool->size, sample->data, sample->length );
    pool->size = pool_size;
    pool->sample_count += 1;
    lsmash_delete_sample( sample );
    return 0;
}

static int isom_append_sample_internal( isom_trak_entry_t *trak, lsmash_sample_t *sample )
{
    int flush = isom_update_sample_tables( trak, sample );
    if( flush < 0 )
        return -1;
    /* flush == 1 means pooled samples must be flushed. */
    lsmash_root_t *root = trak->root;
    isom_sample_pool_t *current_pool = trak->cache->chunk.pool;
    if( flush == 1 && isom_write_pooled_samples( root, current_pool ) )
        return -1;
    /* Arbitration system between tracks with extremely scattering dts.
     * Here, we check whether asynchronization between the tracks exceeds the tolerance.
     * If a track has too old "first DTS" in its cached chunk than current sample's DTS, then its pooled samples must be flushed.
     * We don't consider presentation of media since any edit can pick an arbitrary portion of media in track.
     * Note: you needn't read this loop until you grasp the basic handling of chunks. */
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
        if( !chunk->pool || !chunk->pool->sample_count )
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
    return isom_pool_sample( current_pool, sample );
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
        root->size += 2 * ISOM_BASEBOX_COMMON_SIZE;
    }
    isom_sample_entry_t *sample_entry = (isom_sample_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, sample->index );
    if( !sample_entry )
        return -1;
    if( isom_is_lpcm_audio( sample_entry ) )
    {
        uint32_t frame_size = ((isom_audio_entry_t *)sample_entry)->constBytesPerAudioPacket;
        if( sample->length == frame_size )
            return isom_append_sample_internal( trak, sample );
        else if( sample->length < frame_size )
            return -1;
        /* Append samples splitted into each LPCMFrame. */
        uint64_t dts = sample->dts;
        uint64_t cts = sample->cts;
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
    if( trak->cache->chunk.pool && trak->cache->chunk.pool->sample_count
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
                    group->delimited = 1;
                }
                isom_sbgp_entry_t *sbgp = isom_get_sample_to_group( stbl, ISOM_GROUP_TYPE_ROLL );
                if( isom_flush_roll_pool( sbgp, trak->cache->roll.pool ) )
                    return -1;
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
        if( root->fragment->pool_size )
            trun->flags |= ISOM_TR_FLAGS_DATA_OFFSET_PRESENT;
        trun->data_offset = root->fragment->pool_size;
    }
    if( isom_append_fragment_track_run( root, &traf->cache->chunk ) )
        return -1;
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
    flags.sample_is_non_sync_sample   = sample->prop.random_access_type != ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
    flags.sample_degradation_priority = 0;
    return flags;
}

static int isom_update_fragment_sample_tables( isom_traf_entry_t *traf, lsmash_sample_t *sample )
{
    isom_tfhd_t *tfhd = traf->tfhd;
    isom_trex_entry_t *trex = isom_get_trex( traf->root->moov->mvex, tfhd->track_ID );
    if( !trex )
        return -1;
    lsmash_root_t *root   = traf->root;
    isom_cache_t *cache   = traf->cache;
    isom_chunk_t *current = &cache->chunk;
    /* Create a new track run if the duration exceeds max_chunk_duration.
     * Old one will be appended to the pool of this movie fragment. */
    int delimit = (root->max_chunk_duration < ((double)(sample->dts - current->first_dts) / lsmash_get_media_timescale( root, tfhd->track_ID )))
               || (root->max_chunk_size < (current->pool->size + sample->length));
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
        if( !current->pool )
        {
            /* Very initial settings, just once per track */
            current->pool = isom_create_sample_pool( 0 );
            if( !current->pool )
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
            tfhd->sample_description_index = current->sample_description_index = sample->index;
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
                {
                    free( rap );
                    return -1;
                }
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
        current->first_dts = sample->dts;
    }
    /* Update the optional rows in the current track run except for sample_duration if needed. */
    if( sample->length != tfhd->default_sample_size )
        trun->flags |= ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT;
    if( isom_compare_sample_flags( &sample_flags, &tfhd->default_sample_flags ) )
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
    if( isom_pool_sample( trak->cache->chunk.pool, sample ) )
        return -1;
    trak->cache->fragment->has_samples = 1;
    return 0;
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
    if( isom_pool_sample( traf->cache->chunk.pool, sample ) )
        return -1;
    traf->cache->fragment->has_samples = 1;
    return 0;
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
    if( isom_is_lpcm_audio( sample_entry ) )
    {
        uint32_t frame_size = ((isom_audio_entry_t *)sample_entry)->constBytesPerAudioPacket;
        if( sample->length == frame_size )
            return append_sample_func( track_fragment, sample );
        else if( sample->length < frame_size )
            return -1;
        /* Append samples splitted into each LPCMFrame. */
        uint64_t dts = sample->dts;
        uint64_t cts = sample->cts;
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
    /* Write File Type Box here if it was not written yet. */
    if( !root->file_type_written && isom_write_ftyp( root ) )
        return -1;
    if( root->fragment && root->fragment->pool )
        return isom_append_fragment_sample( root, track_ID, sample );
    return isom_append_sample( root, track_ID, sample );
}

/*---- misc functions ----*/

int lsmash_delete_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak )
        return -1;
    isom_remove_edts( trak->edts );
    trak->edts = NULL;
    return isom_update_tkhd_duration( trak );
}

void lsmash_delete_tyrant_chapter( lsmash_root_t *root )
{
    if( !root || !root->moov || !root->moov->udta )
        return;
    isom_remove_chpl( root->moov->udta->chpl );
    root->moov->udta->chpl = NULL;
}

int lsmash_set_copyright( lsmash_root_t *root, uint32_t track_ID, uint16_t ISO_language, char *notice )
{
    if( !root || !root->moov || !root->isom_compatible || (ISO_language && ISO_language < 0x800) || !notice )
        return -1;
    isom_udta_t *udta;
    if( track_ID )
    {
        isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
        if( !trak || (!trak->udta && isom_add_udta( root, track_ID )) )
            return -1;
        udta = trak->udta;
    }
    else
    {
        if( !root->moov->udta && isom_add_udta( root, 0 ) )
            return -1;
        udta = root->moov->udta;
    }
    assert( udta );
    if( udta->cprt_list )
        for( lsmash_entry_t *entry = udta->cprt_list->head; entry; entry = entry->next )
        {
            isom_cprt_t *cprt = (isom_cprt_t *)entry->data;
            if( !cprt || cprt->language == ISO_language )
                return -1;
        }
    if( isom_add_cprt( udta ) )
        return -1;
    isom_cprt_t *cprt = (isom_cprt_t *)udta->cprt_list->tail->data;
    cprt->language = ISO_language;
    cprt->notice_length = strlen( notice ) + 1;
    cprt->notice = lsmash_memdup( notice, cprt->notice_length );
    return 0;
}
