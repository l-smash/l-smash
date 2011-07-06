/*****************************************************************************
 * timeline.c:
 *****************************************************************************
 * Copyright (C) 2011 L-SMASH project
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

#ifdef LSMASH_DEMUXER_ENABLED

#include "internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "box.h"
#include "mp4a.h"
#include "mp4sys.h"


typedef struct
{
    uint64_t offset;
    uint64_t length;
    void *data;
    uint32_t number;
} isom_portable_chunk_t;

typedef struct
{
    uint64_t dts;
    uint64_t cts;
    uint64_t pos;
    uint32_t duration;
    uint32_t length;
    uint32_t index;
    isom_portable_chunk_t *chunk;
    lsmash_sample_property_t prop;
} isom_sample_info_t;

typedef struct
{
    uint32_t track_ID;
    uint32_t movie_timescale;
    uint32_t media_timescale;
    uint32_t last_accessed_chunk_number;
    uint64_t last_accessed_offset;
    uint64_t last_read_size;
    void    *last_accessed_chunk_data;
    lsmash_entry_list_t edit_list       [1];    /* list of edits */
    lsmash_entry_list_t description_list[1];    /* list of descriptions */
    lsmash_entry_list_t chunk_list      [1];    /* list of chunks */
    lsmash_entry_list_t info_list       [1];    /* list of sample info */
} isom_timeline_t;

static isom_timeline_t *isom_get_timeline( lsmash_root_t *root, uint32_t track_ID )
{
    if( !track_ID || !root || !root->timeline )
        return NULL;
    for( lsmash_entry_t *entry = root->timeline->head; entry; entry = entry->next )
    {
        isom_timeline_t *timeline = (isom_timeline_t *)entry->data;
        if( !timeline )
            return NULL;
        if( timeline->track_ID == track_ID )
            return timeline;
    }
    return NULL;
}

static isom_timeline_t *isom_create_timeline( void )
{
    isom_timeline_t *timeline = malloc( sizeof(isom_timeline_t) );
    if( !timeline )
        return NULL;
    timeline->track_ID                   = 0;
    timeline->last_accessed_chunk_number = 0;
    timeline->last_accessed_offset       = 0;
    timeline->last_read_size             = 0;
    timeline->last_accessed_chunk_data   = NULL;
    lsmash_init_entry_list( timeline->edit_list );
    lsmash_init_entry_list( timeline->description_list );
    lsmash_init_entry_list( timeline->chunk_list );
    lsmash_init_entry_list( timeline->info_list );
    return timeline;
}

static void isom_destruct_timeline_direct( isom_timeline_t *timeline )
{
    if( !timeline )
        return;
    if( timeline->last_accessed_chunk_data )
        free( timeline->last_accessed_chunk_data );
    lsmash_remove_list( timeline->edit_list,        NULL );
    lsmash_remove_list( timeline->description_list, isom_remove_sample_description );
    lsmash_remove_list( timeline->chunk_list,       NULL );     /* chunk data must be already freed. */
    lsmash_remove_list( timeline->info_list,        NULL );
    free( timeline );
}

void isom_remove_timelines( lsmash_root_t *root )
{
    if( !root || !root->timeline )
        return;
    lsmash_remove_list( root->timeline, isom_destruct_timeline_direct );
}

void lsmash_destruct_timeline( lsmash_root_t *root, uint32_t track_ID )
{
    if( !track_ID || !root || !root->timeline )
        return;
    for( lsmash_entry_t *entry = root->timeline->head; entry; entry = entry->next )
    {
        isom_timeline_t *timeline = (isom_timeline_t *)entry->data;
        if( !timeline )
            continue;
        if( timeline->track_ID == track_ID )
            lsmash_remove_entry_direct( root->timeline, entry, isom_destruct_timeline_direct );
    }
}

#if 0
static lsmash_video_summary_t *isom_create_video_summary_from_description( isom_visual_entry_t *visual )
{
    isom_visual_entry_t *visual;
    lsmash_video_summary_t *summary = malloc( sizeof(lsmash_video_summary_t) );
    if( !summary )
        return NULL;
    memset( summary, 0, sizeof(lsmash_video_summary_t) );
    summary->width  = visual->width;
    summary->height = visual->height;
    if( visual->clap )
    {
        double cleanApertureWidth  = (double)clap->cleanApertureWidthN  / cleanApertureWidthD;
        double cleanApertureHeight = (double)clap->cleanApertureHeightN / cleanApertureHeightD;
        double horizOff = (double)clap->horizOffN / clap->horizOffD;
        double vertOff  = (double)clap->vertOffN  / clap->vertOffD;
        summary->crop_top    = (summary->height - cleanApertureHeight + vertOff)  / 2;
        summary->crop_left   = (summary->width  - cleanApertureWidth  + horizOff) / 2;
        summary->crop_bottom = (summary->height - cleanApertureHeight - vertOff)  / 2;
        summary->crop_right  = (summary->width  - cleanApertureWidth  - horizOff) / 2;
    }
    if( visual->pasp )
    {
        summary->par_h = visual->pasp->hSpacing;
        summary->par_v = visual->pasp->vSpacing;
    }
    if( visual->stsl )
        summary->scaling_method = visual->stsl->scale_method;
    if( visual->colr )
    {
        summary->primaries = visual->colr->primaries_index;
        summary->transfer  = visual->colr->transfer_function_index;
        summary->matrix    = visual->colr->matrix_index;
    }
    return summary;
}
#endif

static isom_esds_t *isom_duplicate_esds( isom_box_t *dst_parent, isom_esds_t *src )
{
    if( !src || !src->ES )
        return NULL;
    isom_esds_t *dst = malloc( sizeof(isom_esds_t) );
    if( !dst )
        return NULL;
    isom_init_box_common( dst, dst_parent, ISOM_BOX_TYPE_ESDS );
    dst->ES = mp4sys_duplicate_ES_Descriptor( src->ES );
    if( !dst->ES )
    {
        free( dst );
        return NULL;
    }
    return dst;
}

#define isom_copy_fields( dst, src, box_name ) \
    lsmash_root_t *root = dst->box_name->root; \
    isom_box_t *parent = dst->box_name->parent; \
    *dst->box_name = *src->box_name; \
    dst->box_name->root = root; \
    dst->box_name->parent = parent

static int isom_copy_clap( isom_visual_entry_t *dst, isom_visual_entry_t *src )
{
    if( !dst )
        return 0;
    if( !src || !src->clap )
    {
        isom_remove_clap( dst->clap );
        return 0;
    }
    if( !dst->clap && isom_add_clap( dst ) )
        return -1;
    isom_copy_fields( dst, src, clap );
    return 0;
}

static int isom_copy_pasp( isom_visual_entry_t *dst, isom_visual_entry_t *src )
{
    if( !dst )
        return 0;
    if( !src || !src->pasp )
    {
        isom_remove_pasp( dst->pasp );
        return 0;
    }
    if( !dst->pasp && isom_add_pasp( dst ) )
        return -1;
    isom_copy_fields( dst, src, pasp );
    return 0;
}

static int isom_copy_colr( isom_visual_entry_t *dst, isom_visual_entry_t *src )
{
    if( !dst )
        return 0;
    if( !src || !src->colr )
    {
        isom_remove_colr( dst->colr );
        return 0;
    }
    if( !dst->colr && isom_add_colr( dst ) )
        return -1;
    isom_copy_fields( dst, src, colr );
    return 0;
}

static int isom_copy_stsl( isom_visual_entry_t *dst, isom_visual_entry_t *src )
{
    if( !dst )
        return 0;
    if( !src || !src->stsl )
    {
        isom_remove_stsl( dst->stsl );
        return 0;
    }
    if( !dst->stsl && isom_add_stsl( dst ) )
        return -1;
    isom_copy_fields( dst, src, stsl );
    return 0;
}

static int isom_copy_ps_entries( lsmash_entry_list_t *dst, lsmash_entry_list_t *src )
{
    if( !src )
        return 0;
    for( lsmash_entry_t *entry = src->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *src_ps = (isom_avcC_ps_entry_t *)entry->data;
        if( !src_ps )
            return -1;
        isom_avcC_ps_entry_t *dst_ps = isom_create_ps_entry( src_ps->parameterSetNALUnit, src_ps->parameterSetLength );
        if( !dst_ps )
            return -1;
        if( lsmash_add_entry( dst, dst_ps ) )
        {
            isom_remove_avcC_ps( dst_ps );
            return -1;
        }
    }
    return 0;
}

static int isom_copy_avcC( isom_visual_entry_t *dst, isom_visual_entry_t *src )
{
    if( !dst )
        return 0;
    isom_remove_avcC( dst->avcC );
    if( !src || !src->avcC )
        return 0;
    if( isom_add_avcC( dst ) )
        return -1;
    isom_avcC_t temp = *dst->avcC;      /* Hold created lists. */
    isom_copy_fields( dst, src, avcC );
    dst->avcC->sequenceParameterSets   = temp.sequenceParameterSets;
    dst->avcC->pictureParameterSets    = temp.pictureParameterSets;
    dst->avcC->sequenceParameterSetExt = temp.sequenceParameterSetExt;
    if( isom_copy_ps_entries( dst->avcC->sequenceParameterSets,   src->avcC->sequenceParameterSets   )
     || isom_copy_ps_entries( dst->avcC->pictureParameterSets,    src->avcC->pictureParameterSets    )
     || isom_copy_ps_entries( dst->avcC->sequenceParameterSetExt, src->avcC->sequenceParameterSetExt ) )
        return -1;
    return 0;
}

static int isom_copy_btrt( isom_visual_entry_t *dst, isom_visual_entry_t *src )
{
    if( !dst )
        return 0;
    if( !src || !src->btrt )
    {
        isom_remove_btrt( dst->btrt );
        return 0;
    }
    if( !dst->btrt && isom_add_btrt( dst ) )
        return -1;
    isom_copy_fields( dst, src, btrt );
    return 0;
}

static isom_visual_entry_t *isom_duplicate_visual_description( isom_visual_entry_t *src )
{
    isom_visual_entry_t *dst = lsmash_memdup( src, sizeof(isom_visual_entry_t) );
    if( !dst )
        return NULL;
    dst->clap = NULL;
    dst->pasp = NULL;
    dst->colr = NULL;
    dst->stsl = NULL;
    dst->esds = NULL;
    dst->avcC = NULL;
    dst->btrt = NULL;
    /* Copy children. */
    dst->esds = isom_duplicate_esds( (isom_box_t *)dst, src->esds );
    if( (src->esds && !dst->esds)   /* Check if copying failed. */
     || isom_copy_clap( dst, src )
     || isom_copy_pasp( dst, src )
     || isom_copy_colr( dst, src )
     || isom_copy_stsl( dst, src )
     || isom_copy_avcC( dst, src )
     || isom_copy_btrt( dst, src ) )
    {
        isom_remove_sample_description( (isom_sample_entry_t *)dst );
        return NULL;
    }
    return dst;
}

static int isom_copy_frma( isom_wave_t *dst, isom_wave_t *src )
{
    if( !dst )
        return 0;
    if( !src || !src->frma )
    {
        isom_remove_frma( dst->frma );
        return 0;
    }
    if( !dst->frma && isom_add_frma( dst ) )
        return -1;
    isom_copy_fields( dst, src, frma );
    return 0;
}

static int isom_copy_enda( isom_wave_t *dst, isom_wave_t *src )
{
    if( !dst )
        return 0;
    if( !src || !src->enda )
    {
        isom_remove_enda( dst->enda );
        return 0;
    }
    if( !dst->enda && isom_add_enda( dst ) )
        return -1;
    isom_copy_fields( dst, src, enda );
    return 0;
}

static int isom_copy_mp4a( isom_wave_t *dst, isom_wave_t *src )
{
    if( !dst )
        return 0;
    if( !src || !src->mp4a )
    {
        isom_remove_mp4a( dst->mp4a );
        return 0;
    }
    if( !dst->mp4a && isom_add_mp4a( dst ) )
        return -1;
    isom_copy_fields( dst, src, mp4a );
    return 0;
}

static int isom_copy_terminator( isom_wave_t *dst, isom_wave_t *src )
{
    if( !dst )
        return 0;
    if( !src || !src->terminator )
    {
        isom_remove_terminator( dst->terminator );
        return 0;
    }
    if( dst->terminator )
        return 0;
    return isom_add_terminator( dst );
}

static int isom_copy_wave( isom_audio_entry_t *dst, isom_audio_entry_t *src )
{
    if( !dst )
        return 0;
    isom_remove_wave( dst->wave );
    if( !src || !src->wave )
        return 0;
    if( isom_add_wave( dst ) )
        return -1;
    if( src->wave->exdata && src->wave->exdata_length )
    {
        dst->wave->exdata = lsmash_memdup( src->wave->exdata, src->wave->exdata_length );
        if( !dst->wave->exdata )
            return -1;
        dst->wave->exdata_length = src->wave->exdata_length;
    }
    /* Copy children. */
    dst->wave->esds = isom_duplicate_esds( (isom_box_t *)dst->wave, src->wave->esds );
    if( (src->wave->esds && !dst->wave->esds)   /* Check if copying failed. */
     || isom_copy_frma( dst->wave, src->wave )
     || isom_copy_enda( dst->wave, src->wave )
     || isom_copy_mp4a( dst->wave, src->wave )
     || isom_copy_terminator( dst->wave, src->wave ) )
        return -1;
    return 0;
}

static int isom_copy_chan( isom_audio_entry_t *dst, isom_audio_entry_t *src )
{
    if( !dst )
        return 0;
    isom_remove_chan( dst->chan );
    if( !src || !src->chan )
        return 0;
    if( isom_add_chan( dst ) )
        return -1;
    dst->chan->channelLayoutTag          = src->chan->channelLayoutTag;
    dst->chan->channelBitmap             = src->chan->channelBitmap;
    dst->chan->numberChannelDescriptions = src->chan->numberChannelDescriptions;
    if( src->chan->numberChannelDescriptions && src->chan->channelDescriptions )
    {
        uint32_t numberChannelDescriptions = src->chan->numberChannelDescriptions;
        dst->chan->channelDescriptions = malloc( numberChannelDescriptions * sizeof(isom_channel_description_t) );
        if( !dst->chan->channelDescriptions )
            return -1;
        for( uint32_t i = 0; i < numberChannelDescriptions; i++ )
        {
            dst->chan->channelDescriptions[i].channelLabel   = src->chan->channelDescriptions[i].channelLabel;
            dst->chan->channelDescriptions[i].channelFlags   = src->chan->channelDescriptions[i].channelFlags;
            dst->chan->channelDescriptions[i].coordinates[0] = src->chan->channelDescriptions[i].coordinates[0];
            dst->chan->channelDescriptions[i].coordinates[1] = src->chan->channelDescriptions[i].coordinates[1];
            dst->chan->channelDescriptions[i].coordinates[2] = src->chan->channelDescriptions[i].coordinates[2];
        }
        dst->chan->numberChannelDescriptions = src->chan->numberChannelDescriptions;
    }
    else
    {
        if( dst->chan->channelDescriptions )
            free( dst->chan->channelDescriptions );
        dst->chan->channelDescriptions = NULL;
        dst->chan->numberChannelDescriptions = 0;
    }
    return 0;
}

static isom_audio_entry_t *isom_duplicate_audio_description( isom_audio_entry_t *src )
{
    isom_audio_entry_t *dst = lsmash_memdup( src, sizeof(isom_audio_entry_t) );
    if( !dst )
        return NULL;
    dst->esds = NULL;
    dst->wave = NULL;
    dst->chan = NULL;
    if( isom_is_lpcm_audio( src->type ) )
    {
        if( !src->version )
            dst->constBytesPerAudioPacket = (src->samplesize * src->channelcount) / 8;
        else if( src->version == 1 )
            dst->constBytesPerAudioPacket = src->bytesPerFrame;
    }
    if( src->exdata && src->exdata_length )
    {
        dst->exdata = lsmash_memdup( src->exdata, src->exdata_length );
        if( !dst->exdata )
        {
            isom_remove_sample_description( (isom_sample_entry_t *)dst );
            return NULL;
        }
        dst->exdata_length = src->exdata_length;
    }
    /* Copy children. */
    dst->esds = isom_duplicate_esds( (isom_box_t *)dst, src->esds );
    if( (src->esds && !dst->esds)   /* Check if copying failed. */
     || isom_copy_wave( dst, src )
     || isom_copy_chan( dst, src ) )
    {
        isom_remove_sample_description( (isom_sample_entry_t *)dst );
        return NULL;
    }
    return dst;
}

static int isom_copy_ftab( isom_tx3g_entry_t *dst, isom_tx3g_entry_t *src )
{
    if( !dst )
        return 0;
    isom_remove_ftab( dst->ftab );
    if( !src || !src->ftab )
        return 0;
    if( isom_add_ftab( dst ) )
        return -1;
    if( src->ftab->list )
    {
        dst->ftab->list = lsmash_create_entry_list();
        if( !dst->ftab->list )
            return -1;
        for( lsmash_entry_t *entry = src->ftab->list->head; entry; entry = entry->next )
        {
            isom_font_record_t *src_record = (isom_font_record_t *)entry->data;
            if( !src_record )
                return -1;
            isom_font_record_t *dst_record = lsmash_memdup( src_record, sizeof(isom_font_record_t) );
            dst_record->font_name = lsmash_memdup( src_record->font_name, src_record->font_name_length );
            if( lsmash_add_entry( dst->ftab->list, dst_record ) )
            {
                free( dst_record->font_name );
                free( dst_record );
                return -1;
            }
        }
    }
    return 0;
}

static isom_tx3g_entry_t *isom_duplicate_tx3g_description( isom_tx3g_entry_t *src )
{
    isom_tx3g_entry_t *dst = lsmash_memdup( src, sizeof(isom_tx3g_entry_t) );
    if( !dst )
        return NULL;
    dst->ftab = NULL;
    if( isom_copy_ftab( dst, src ) )
    {
        isom_remove_sample_description( (isom_sample_entry_t *)dst );
        return NULL;
    }
    return dst;
}

static isom_text_entry_t *isom_duplicate_text_description( isom_text_entry_t *src )
{
    isom_text_entry_t *dst = lsmash_memdup( src, sizeof(isom_text_entry_t) );
    if( !dst )
        return NULL;
    dst->font_name = NULL;
    dst->font_name_length = 0;
    if( src->font_name && src->font_name_length )
    {
        dst->font_name = lsmash_memdup( src->font_name, src->font_name_length );
        if( !dst->font_name )
        {
            isom_remove_sample_description( (isom_sample_entry_t *)dst );
            return NULL;
        }
        dst->font_name_length = src->font_name_length;
    }
    return dst;
}

static isom_sample_entry_t *isom_duplicate_description( isom_sample_entry_t *entry, isom_stsd_t *dst_parent )
{
    if( !entry )
        return NULL;
    void *description = NULL;
    int is_visual = 0;
    switch( entry->type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
            description = isom_duplicate_visual_description( (isom_visual_entry_t *)entry );
            is_visual = 1;
            break;
        case ISOM_CODEC_TYPE_MP4A_AUDIO :
        case ISOM_CODEC_TYPE_AC_3_AUDIO :
        case ISOM_CODEC_TYPE_ALAC_AUDIO :
        case ISOM_CODEC_TYPE_EC_3_AUDIO :
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
            description = isom_duplicate_audio_description( (isom_audio_entry_t *)entry );
            break;
        case ISOM_CODEC_TYPE_TX3G_TEXT :
            description = isom_duplicate_tx3g_description( (isom_tx3g_entry_t *)entry );
            break;
        case QT_CODEC_TYPE_TEXT_TEXT :
            description = isom_duplicate_text_description( (isom_text_entry_t *)entry );
            break;
        default :
            return NULL;
    }
    if( description )
        ((isom_sample_entry_t *)description)->parent = (isom_box_t *)dst_parent;
    if( dst_parent && is_visual )
    {
        /* Check if needed Track Aperture Modes. */
        isom_trak_entry_t *trak = (isom_trak_entry_t *)dst_parent->parent->parent->parent->parent;
        isom_tapt_t *tapt = trak->tapt;
        if( !trak->root->qt_compatible                              /* Track Aperture Modes is only available under QuickTime file format. */
         || ((isom_visual_entry_t *)description)->stsl              /* Sample scaling method might conflict with this feature. */
         || !tapt || !tapt->clef || !tapt->prof || !tapt->enof      /* Check if required boxes exist. */
         || dst_parent->list->entry_count != 0 )                    /* Multiple sample description might conflict with this. */
            isom_remove_tapt( trak->tapt );
    }
    return (isom_sample_entry_t *)description;
}

#define INCREMENT_SAMPLE_NUMBER_IN_ENTRY( sample_number_in_entry, entry, entry_data ) \
    if( sample_number_in_entry == entry_data->sample_count ) \
    { \
        sample_number_in_entry = 1; \
        entry = entry->next; \
    } \
    else \
        ++sample_number_in_entry

int lsmash_construct_timeline( lsmash_root_t *root, uint32_t track_ID )
{
    if( !root || !root->moov || !root->moov->mvhd || !root->moov->mvhd->timescale )
        return -1;
    /* Get track by track_ID. */
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->mdhd->timescale || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    /* Create a timeline list if it doesn't exist. */
    if( !root->timeline )
    {
        root->timeline = lsmash_create_entry_list();
        if( !root->timeline )
            return -1;
    }
    /* Create a timeline. */
    isom_timeline_t *timeline = isom_create_timeline();
    if( !timeline )
        return -1;
    timeline->track_ID        = track_ID;
    timeline->movie_timescale = root->moov->mvhd->timescale;
    timeline->media_timescale = trak->mdia->mdhd->timescale;
    /* Preparation for construction. */
    isom_elst_t *elst = trak->edts ? trak->edts->elst : NULL;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_stsd_t *stsd = stbl->stsd;
    isom_stts_t *stts = stbl->stts;
    isom_ctts_t *ctts = stbl->ctts;
    isom_stss_t *stss = stbl->stss;
    isom_stps_t *stps = stbl->stps;
    isom_sdtp_t *sdtp = stbl->sdtp;
    isom_stsc_t *stsc = stbl->stsc;
    isom_stsz_t *stsz = stbl->stsz;
    isom_stco_t *stco = stbl->stco;
    isom_sgpd_entry_t *sgpd_roll = isom_get_sample_group_description( stbl, ISOM_GROUP_TYPE_ROLL );
    isom_sgpd_entry_t *sgpd_rap  = isom_get_sample_group_description( stbl, ISOM_GROUP_TYPE_RAP );
    isom_sbgp_entry_t *sbgp_roll = isom_get_sample_to_group( stbl, ISOM_GROUP_TYPE_ROLL );
    isom_sbgp_entry_t *sbgp_rap  = isom_get_sample_to_group( stbl, ISOM_GROUP_TYPE_RAP );
    lsmash_entry_t *elst_entry = elst && elst->list ? elst->list->head : NULL;
    lsmash_entry_t *stsd_entry = stsd && stsd->list ? stsd->list->head : NULL;
    lsmash_entry_t *stts_entry = stts && stts->list ? stts->list->head : NULL;
    lsmash_entry_t *ctts_entry = ctts && ctts->list ? ctts->list->head : NULL;
    lsmash_entry_t *stss_entry = stss && stss->list ? stss->list->head : NULL;
    lsmash_entry_t *stps_entry = stps && stps->list ? stps->list->head : NULL;
    lsmash_entry_t *sdtp_entry = sdtp && sdtp->list ? sdtp->list->head : NULL;
    lsmash_entry_t *stsz_entry = stsz && stsz->list ? stsz->list->head : NULL;
    lsmash_entry_t *stsc_entry = stsc && stsc->list ? stsc->list->head : NULL;
    lsmash_entry_t *stco_entry = stco && stco->list ? stco->list->head : NULL;
    lsmash_entry_t *sbgp_roll_entry = sbgp_roll && sbgp_roll->list ? sbgp_roll->list->head : NULL;
    lsmash_entry_t *sbgp_rap_entry  = sbgp_rap  && sbgp_rap->list  ? sbgp_rap->list->head  : NULL;
    lsmash_entry_t *next_stsc_entry = stsc_entry ? stsc_entry->next : NULL;
    isom_stsc_entry_t *stsc_data = stsc_entry ? (isom_stsc_entry_t *)stsc_entry->data : NULL;
    isom_sample_entry_t *description = stsd_entry ? (isom_sample_entry_t *)stsd_entry->data : NULL;
    isom_sample_info_t *info = NULL;        /* shut up 'uninitialized' warning */
    isom_portable_chunk_t *chunk = NULL;    /* shut up 'uninitialized' warning */
    if( !description || !stts_entry || !stsc_entry || !stco_entry || !stco_entry->data )
        goto fail;
    chunk = malloc( sizeof(isom_portable_chunk_t) );
    if( !chunk || lsmash_add_entry( timeline->chunk_list, chunk ) )
        goto fail;
    chunk->number = 1;
    chunk->data = NULL;
    /* Copy edits. */
    while( elst_entry )
    {
        isom_elst_entry_t *edit = (isom_elst_entry_t *)lsmash_memdup( elst_entry->data, sizeof(isom_elst_entry_t) );
        if( !edit
         || lsmash_add_entry( timeline->edit_list, edit ) )
            goto fail;
        elst_entry = elst_entry->next;
    }
    /* Copy sample descriptions. */
    while( stsd_entry )
    {
        description = isom_duplicate_description( (isom_sample_entry_t *)stsd_entry->data, NULL );
        if( !description
         || lsmash_add_entry( timeline->description_list, description ) )
            goto fail;
        stsd_entry = stsd_entry->next;
    }
    stsd_entry = stsd->list->head;
    description = (isom_sample_entry_t *)stsd_entry->data;
    int all_sync = !stss;
    int large_presentation = stco->large_presentation;
    int is_lpcm_audio = isom_is_lpcm_audio( description->type );
    uint64_t dts = 0;
    uint64_t cts = 0;
    uint32_t sample_number = 1;
    uint32_t sample_number_in_stts_entry = 1;
    uint32_t sample_number_in_ctts_entry = 1;
    uint32_t sample_number_in_sbgp_roll_entry = 1;
    uint32_t sample_number_in_sbgp_rap_entry  = 1;
    uint32_t sample_number_in_chunk = 1;
    uint32_t chunk_number = 1;
    uint64_t offset_from_chunk = 0;
    uint64_t offset = chunk->offset
                    = large_presentation
                    ? ((isom_co64_entry_t *)stco_entry->data)->chunk_offset
                    : ((isom_stco_entry_t *)stco_entry->data)->chunk_offset;
    uint32_t constant_sample_size = is_lpcm_audio
                                  ? ((isom_audio_entry_t *)description)->constBytesPerAudioPacket
                                  : stsz->sample_size;
    /* Construct media timeline. */
    while( sample_number <= stsz->sample_count )
    {
        info = malloc( sizeof(isom_sample_info_t) );
        if( !info )
            goto fail;
        memset( info, 0, sizeof(isom_sample_info_t) );
        /* Get timestamp. */
        isom_stts_entry_t *stts_data = (isom_stts_entry_t *)stts_entry->data;
        if( !stts_data )
            goto fail;
        INCREMENT_SAMPLE_NUMBER_IN_ENTRY( sample_number_in_stts_entry, stts_entry, stts_data );
        if( ctts_entry )
        {
            isom_ctts_entry_t *ctts_data = (isom_ctts_entry_t *)ctts_entry->data;
            if( !ctts_data )
                goto fail;
            INCREMENT_SAMPLE_NUMBER_IN_ENTRY( sample_number_in_ctts_entry, ctts_entry, ctts_data );
            cts = dts + ctts_data->sample_offset;
        }
        else
            cts = dts;
        info->dts      = dts;
        info->cts      = cts;
        info->duration = stts_data->sample_delta;
        dts += info->duration;
        /* Check whether sync sample or not. */
        if( stss_entry )
        {
            isom_stss_entry_t *stss_data = (isom_stss_entry_t *)stss_entry->data;
            if( !stss_data )
                goto fail;
            if( sample_number == stss_data->sample_number )
            {
                info->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
                stss_entry = stss_entry->next;
            }
        }
        else if( all_sync )
            info->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
        /* Check whether partial sync sample or not. */
        if( stps_entry )
        {
            isom_stps_entry_t *stps_data = (isom_stps_entry_t *)stps_entry->data;
            if( !stps_data )
                goto fail;
            if( sample_number == stps_data->sample_number )
            {
                info->prop.random_access_type = QT_SAMPLE_RANDOM_ACCESS_TYPE_PARTIAL_SYNC;
                stps_entry = stps_entry->next;
            }
        }
        /* Get independent and disposable info. */
        if( !is_lpcm_audio && sdtp_entry )
        {
            isom_sdtp_entry_t *sdtp_data = (isom_sdtp_entry_t *)sdtp_entry->data;
            if( !sdtp_data )
                goto fail;
            info->prop.leading     = sdtp_data->is_leading;
            info->prop.independent = sdtp_data->sample_depends_on;
            info->prop.disposable  = sdtp_data->sample_is_depended_on;
            info->prop.redundant   = sdtp_data->sample_has_redundancy;
            sdtp_entry = sdtp_entry->next;
        }
        /* Get roll recovery grouping info. */
        if( sbgp_roll_entry )
        {
            isom_group_assignment_entry_t *assignment = (isom_group_assignment_entry_t *)sbgp_roll_entry->data;
            if( !assignment )
                goto fail;
            if( sample_number_in_sbgp_roll_entry == 1 && assignment->group_description_index )
            {
                isom_roll_entry_t *roll_data = (isom_roll_entry_t *)lsmash_get_entry_data( sgpd_roll->list, assignment->group_description_index );
                if( !roll_data )
                    goto fail;
                info->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_RECOVERY;
                info->prop.recovery.complete  = sample_number + roll_data->roll_distance;
            }
            INCREMENT_SAMPLE_NUMBER_IN_ENTRY( sample_number_in_sbgp_roll_entry, sbgp_roll_entry, assignment );
        }
        info->prop.recovery.identifier = sample_number;
        /* Get random access point grouping info. */
        if( sbgp_rap_entry )
        {
            isom_group_assignment_entry_t *assignment = (isom_group_assignment_entry_t *)sbgp_rap_entry->data;
            if( !assignment )
                goto fail;
            if( assignment->group_description_index && (info->prop.random_access_type == ISOM_SAMPLE_RANDOM_ACCESS_TYPE_NONE) )
            {
                isom_rap_entry_t *rap_data = (isom_rap_entry_t *)lsmash_get_entry_data( sgpd_rap->list, assignment->group_description_index );
                if( !rap_data )
                    goto fail;
                /* If this is not an open RAP, we treat it as an unknown RAP since non-IDR sample could make a closed GOP. */
                info->prop.random_access_type = (rap_data->num_leading_samples_known && !!rap_data->num_leading_samples)
                                              ? ISOM_SAMPLE_RANDOM_ACCESS_TYPE_OPEN_RAP
                                              : ISOM_SAMPLE_RANDOM_ACCESS_TYPE_UNKNOWN_RAP;
            }
            INCREMENT_SAMPLE_NUMBER_IN_ENTRY( sample_number_in_sbgp_rap_entry, sbgp_rap_entry, assignment );
        }
        /* Get size of sample in the stream. */
        if( is_lpcm_audio || !stsz_entry )
            info->length = constant_sample_size;
        else
        {
            if( !stsz_entry->data )
                goto fail;
            info->length = ((isom_stsz_entry_t *)stsz_entry->data)->entry_size;
            stsz_entry = stsz_entry->next;
        }
        /* Get chunk info. */
        info->pos = offset;
        info->index = stsc_data->sample_description_index;
        info->chunk = chunk;
        offset_from_chunk += info->length;
        if( sample_number_in_chunk == stsc_data->samples_per_chunk )
        {
            /* Move the next chunk. */
            sample_number_in_chunk = 1;
            stco_entry = stco_entry->next;
            if( stco_entry && stco_entry->data )
                offset = large_presentation
                       ? ((isom_co64_entry_t *)stco_entry->data)->chunk_offset
                       : ((isom_stco_entry_t *)stco_entry->data)->chunk_offset;
            chunk->length = offset_from_chunk;      /* the length of the previous chunk */
            chunk = malloc( sizeof(isom_portable_chunk_t) );
            if( !chunk )
                goto fail;
            chunk->number = ++chunk_number;
            chunk->offset = offset;
            chunk->data = NULL;
            offset_from_chunk = 0;
            if( lsmash_add_entry( timeline->chunk_list, chunk ) )
                goto fail;
            if( next_stsc_entry
             && chunk_number == ((isom_stsc_entry_t *)next_stsc_entry->data)->first_chunk )
            {
                stsc_entry = next_stsc_entry;
                next_stsc_entry = stsc_entry->next;
                if( !stsc_entry->data )
                    goto fail;
                stsc_data = (isom_stsc_entry_t *)stsc_entry->data;
                /* Update sample description. */
                description = (isom_sample_entry_t *)lsmash_get_entry_data( stsd->list, stsc_data->sample_description_index );
                is_lpcm_audio = isom_is_lpcm_audio( description->type );
                if( is_lpcm_audio )
                    constant_sample_size = ((isom_audio_entry_t *)description)->constBytesPerAudioPacket;
            }
        }
        else
        {
            ++sample_number_in_chunk;
            offset += info->length;
        }
        /* OK. Let's add its info. */
        if( lsmash_add_entry( timeline->info_list, info ) )
            goto fail;
        ++sample_number;
    }
    chunk->length = offset_from_chunk;
    if( lsmash_add_entry( root->timeline, timeline ) )
    {
        isom_destruct_timeline_direct( timeline );
        return -1;
    }
    return 0;
fail:
    isom_destruct_timeline_direct( timeline );
    if( info )
        free( info );
    if( chunk )
        free( chunk );
    return -1;
}

lsmash_sample_t *lsmash_get_sample_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number )
{
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    isom_sample_info_t *info = (isom_sample_info_t *)lsmash_get_entry_data( timeline->info_list, sample_number );
    if( !info )
        return NULL;
    /* Get data of a sample from a chunk. */
    isom_portable_chunk_t *chunk = info->chunk;
    lsmash_bs_t *bs = root->bs;
    if( (timeline->last_accessed_chunk_number != chunk->number)
     || (timeline->last_accessed_offset > info->pos)
     || (timeline->last_read_size < (info->pos + info->length - timeline->last_accessed_offset)) )
    {
        /* Read data of a chunk in the stream. */
        uint64_t read_size;
        uint64_t seek_pos;
        if( root->max_read_size >= chunk->length )
        {
            read_size = chunk->length;
            seek_pos = chunk->offset;
        }
        else
        {
            read_size = LSMASH_MAX( root->max_read_size, info->length );
            seek_pos = info->pos;
        }
        lsmash_fseek( bs->stream, seek_pos, SEEK_SET );
        lsmash_bs_empty( bs );
        if( lsmash_bs_read_data( bs, read_size ) )
            return NULL;
        chunk->data = lsmash_bs_export_data( bs, NULL );
        if( !chunk->data )
            return NULL;
        lsmash_bs_empty( bs );
        if( timeline->last_accessed_chunk_data )
        {
            free( timeline->last_accessed_chunk_data );
            timeline->last_accessed_chunk_data = NULL;
        }
        timeline->last_accessed_chunk_number = chunk->number;
        timeline->last_accessed_chunk_data   = chunk->data;
        timeline->last_accessed_offset       = seek_pos;
        timeline->last_read_size             = read_size;
    }
    lsmash_sample_t *sample = lsmash_create_sample( 0 );
    if( !sample )
        return NULL;
    uint64_t offset_from_seek = info->pos - timeline->last_accessed_offset;
    sample->data = lsmash_memdup( chunk->data + offset_from_seek, info->length );
    if( !sample->data )
    {
        lsmash_delete_sample( sample );
        return NULL;
    }
    /* Get sample info. */
    sample->length = info->length;
    sample->dts    = info->dts;
    sample->cts    = info->cts;
    sample->index  = info->index;
    sample->prop   = info->prop;
    return sample;
}

static isom_sample_info_t *isom_get_sample_info_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number )
{
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    if( !timeline )
        return NULL;
    return (isom_sample_info_t *)lsmash_get_entry_data( timeline->info_list, sample_number );
}

int lsmash_get_dts_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number, uint64_t *dts )
{
    if( !dts )
        return -1;
    isom_sample_info_t *info = isom_get_sample_info_from_media_timeline( root, track_ID, sample_number );
    if( !info )
        return -1;
    *dts = info->dts;
    return 0;
}

int lsmash_check_sample_existence_in_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number )
{
    return !!isom_get_sample_info_from_media_timeline( root, track_ID, sample_number );
}

int lsmash_get_last_sample_delta_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t *last_sample_delta )
{
    if( !last_sample_delta )
        return -1;
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    if( !timeline || !timeline->info_list
     || !timeline->info_list->tail || !timeline->info_list->tail->data )
        return -1;
    *last_sample_delta = ((isom_sample_info_t *)timeline->info_list->tail->data)->duration;
    return 0;
}

int lsmash_copy_timeline_map( lsmash_root_t *dst, uint32_t dst_track_ID, lsmash_root_t *src, uint32_t src_track_ID )
{
    isom_trak_entry_t *dst_trak = isom_get_trak( dst, dst_track_ID );
    if( !dst_trak || !dst_trak->mdia || !dst_trak->mdia->mdhd || !dst_trak->mdia->mdhd->timescale
     || !dst->moov || !dst->moov->mvhd || !dst->moov->mvhd->timescale )
        return -1;
    if( dst_trak->edts && dst_trak->edts->elst )
        lsmash_remove_entries( dst_trak->edts->elst->list, NULL );
    uint32_t src_movie_timescale;
    uint32_t src_media_timescale;
    lsmash_entry_t *src_entry;
    isom_trak_entry_t *src_trak = isom_get_trak( src, src_track_ID );
    if( !src_trak || !src_trak->edts || !src_trak->edts->elst || !src_trak->edts->elst->list )
    {
        /* Get from timeline instead of boxes. */
        isom_timeline_t *src_timeline = isom_get_timeline( src, src_track_ID );
        if( !src_timeline ||!src_timeline->movie_timescale || !src_timeline->media_timescale || !src_timeline->edit_list )
            return -1;
        src_movie_timescale = src_timeline->movie_timescale;
        src_media_timescale = src_timeline->media_timescale;
        src_entry = src_timeline->edit_list->head;
    }
    else
    {
        if( !src->moov || !src->moov->mvhd || !src->moov->mvhd->timescale
         || !src_trak->mdia || !src_trak->mdia->mdhd || !src_trak->mdia->mdhd->timescale )
            return -1;
        src_movie_timescale = src->moov->mvhd->timescale;
        src_media_timescale = src_trak->mdia->mdhd->timescale;
        src_entry = src_trak->edts->elst->list->head;
    }
    if( !src_entry )
        return 0;
    /* Generate edit list if absent in destination. */
    if( (!dst_trak->edts       && isom_add_edts( dst_trak ))
     || (!dst_trak->edts->elst && isom_add_elst( dst_trak->edts )) )
        return -1;
    uint32_t dst_movie_timescale = dst->moov->mvhd->timescale;
    uint32_t dst_media_timescale = dst_trak->mdia->mdhd->timescale;
    lsmash_entry_list_t *dst_list = dst_trak->edts->elst->list;
    while( src_entry )
    {
        isom_elst_entry_t *src_data = (isom_elst_entry_t *)src_entry->data;
        if( !src_data )
            return -1;
        isom_elst_entry_t *dst_data = (isom_elst_entry_t *)malloc( sizeof(isom_elst_entry_t) );
        if( !dst_data )
            return -1;
        dst_data->segment_duration = src_data->segment_duration * ((double)dst_movie_timescale / src_movie_timescale) + 0.5;
        dst_data->media_time       = src_data->media_time       * ((double)dst_media_timescale / src_media_timescale) + 0.5;
        dst_data->media_rate       = src_data->media_rate;
        if( lsmash_add_entry( dst_list, dst_data ) )
        {
            free( dst_data );
            return -1;
        }
        src_entry = src_entry->next;
    }
    return 0;
}

int lsmash_copy_decoder_specific_info( lsmash_root_t *dst, uint32_t dst_track_ID, lsmash_root_t *src, uint32_t src_track_ID )
{
    isom_trak_entry_t *dst_trak = isom_get_trak( dst, dst_track_ID );
    if( !dst_trak || !dst_trak->mdia || !dst_trak->mdia->minf || !dst_trak->mdia->minf->stbl
     || !dst_trak->mdia->minf->stbl->stsd || !dst_trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_stsd_t *dst_stsd = dst_trak->mdia->minf->stbl->stsd;
    lsmash_remove_entries( dst_stsd->list, isom_remove_sample_description );
    lsmash_entry_t *src_entry = NULL;
    isom_trak_entry_t *src_trak = isom_get_trak( src, src_track_ID );
    if( !src_trak || !src_trak->mdia || !src_trak->mdia->minf || !src_trak->mdia->minf->stbl
     || !src_trak->mdia->minf->stbl->stsd || !src_trak->mdia->minf->stbl->stsd->list )
    {
        /* Get source entry from media timeline instead of Sample Description Box. */
        isom_timeline_t *src_timeline = isom_get_timeline( src, src_track_ID );
        if( !src_timeline || !src_timeline->description_list )
            return -1;
        src_entry = src_timeline->description_list->head;
    }
    else
        src_entry = src_trak->mdia->minf->stbl->stsd->list->head;
    if( !src_entry )
        return -1;      /* Required at least one entry. */
    while( src_entry )
    {
        isom_sample_entry_t *src_data = (isom_sample_entry_t *)src_entry->data;
        if( !src_data )
            return -1;
        isom_sample_entry_t *dst_data = isom_duplicate_description( src_data, dst_stsd );
        if( !dst_data )
            return -1;
        if( lsmash_add_entry( dst_stsd->list, dst_data ) )
        {
            isom_remove_sample_description( dst_data );
            return -1;
        }
        src_entry = src_entry->next;
    }
    return 0;
}

#endif /* LSMASH_DEMUXER_ENABLED */
