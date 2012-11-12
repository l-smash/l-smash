/*****************************************************************************
 * timeline.c:
 *****************************************************************************
 * Copyright (C) 2011-2012 L-SMASH project
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
#include "description.h"

#define NO_RANDOM_ACCESS_POINT 0xffffffff

typedef struct
{
    uint64_t data_offset;
    uint64_t length;
    void    *data;
    uint32_t number;
} isom_portable_chunk_t;

typedef struct
{
    uint64_t pos;
    uint32_t duration;
    uint32_t offset;
    uint32_t length;
    uint32_t index;
    isom_portable_chunk_t *chunk;
    lsmash_sample_property_t prop;
} isom_sample_info_t;

typedef struct
{
    uint64_t pos;                   /* position of the first sample in this bunch */
    uint32_t duration;              /* duration in media timescale each sample has */
    uint32_t offset;                /* offset between composition time and decoding time each sample has */
    uint32_t length;                /* data size each sample has */
    uint32_t index;                 /* sample_description_index applied to each sample */
    isom_portable_chunk_t *chunk;   /* chunk samples belong to */
    lsmash_sample_property_t prop;  /* property applied to each sample */
    uint32_t sample_count;          /* number of samples in this bunch */
} isom_lpcm_bunch_t;

typedef struct isom_timeline_tag isom_timeline_t;

struct isom_timeline_tag
{
    uint32_t track_ID;
    uint32_t movie_timescale;
    uint32_t media_timescale;
    uint32_t sample_count;
    uint32_t max_sample_size;
    uint32_t ctd_shift;     /* shift from composition to decode timeline */
    uint64_t media_duration;
    uint32_t last_accessed_sample_number;
    uint32_t last_accessed_chunk_number;
    uint64_t last_accessed_sample_dts;
    uint64_t last_accessed_offset;
    uint32_t last_accessed_lpcm_bunch_number;
    uint32_t last_accessed_lpcm_bunch_duration;
    uint32_t last_accessed_lpcm_bunch_sample_count;
    uint32_t last_accessed_lpcm_bunch_first_sample_number;
    uint64_t last_accessed_lpcm_bunch_dts;
    uint64_t last_read_size;
    void    *last_accessed_chunk_data;
    lsmash_entry_list_t edit_list [1];  /* list of edits */
    lsmash_entry_list_t chunk_list[1];  /* list of chunks */
    lsmash_entry_list_t info_list [1];  /* list of sample info */
    lsmash_entry_list_t bunch_list[1];  /* list of LPCM bunch */
    int (*get_dts)( isom_timeline_t *timeline, uint32_t sample_number, uint64_t *dts );
    int (*get_cts)( isom_timeline_t *timeline, uint32_t sample_number, uint64_t *cts );
    int (*get_sample_duration)( isom_timeline_t *timeline, uint32_t sample_number, uint32_t *sample_duration );
    lsmash_sample_t *(*get_sample)( lsmash_root_t *root, isom_timeline_t *timeline, uint32_t sample_number );
    int (*get_sample_info)( isom_timeline_t *timeline, uint32_t sample_number, lsmash_sample_t *sample );
    int (*get_sample_property)( isom_timeline_t *timeline, uint32_t sample_number, lsmash_sample_property_t *prop );
    int (*check_sample_existence)( isom_timeline_t *timeline, uint32_t sample_number );
};

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
    isom_timeline_t *timeline = lsmash_malloc_zero( sizeof(isom_timeline_t) );
    if( !timeline )
        return NULL;
    lsmash_init_entry_list( timeline->edit_list );
    lsmash_init_entry_list( timeline->chunk_list );
    lsmash_init_entry_list( timeline->info_list );
    lsmash_init_entry_list( timeline->bunch_list );
    return timeline;
}

static void isom_destruct_timeline_direct( isom_timeline_t *timeline )
{
    if( !timeline )
        return;
    if( timeline->last_accessed_chunk_data )
        free( timeline->last_accessed_chunk_data );
    lsmash_remove_entries( timeline->edit_list,        NULL );
    lsmash_remove_entries( timeline->chunk_list,       NULL );     /* chunk data must be already freed. */
    lsmash_remove_entries( timeline->info_list,        NULL );
    lsmash_remove_entries( timeline->bunch_list,       NULL );
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
        {
            lsmash_remove_entry_direct( root->timeline, entry, isom_destruct_timeline_direct );
            break;
        }
    }
}

static uint32_t isom_get_lpcm_sample_size( isom_audio_entry_t *audio )
{
    if( audio->version == 0 )
        return (audio->samplesize * audio->channelcount) / 8;
    else if( audio->version == 1 )
        return audio->bytesPerFrame;
    return audio->constBytesPerAudioPacket;
}

static int isom_add_sample_info_entry( isom_timeline_t *timeline, isom_sample_info_t *src_info )
{
    isom_sample_info_t *dst_info = malloc( sizeof(isom_sample_info_t) );
    if( !dst_info )
        return -1;
    if( lsmash_add_entry( timeline->info_list, dst_info ) )
    {
        free( dst_info );
        return -1;
    }
    *dst_info = *src_info;
    return 0;
}

static int isom_add_lpcm_bunch_entry( isom_timeline_t *timeline, isom_lpcm_bunch_t *src_bunch )
{
    isom_lpcm_bunch_t *dst_bunch = malloc( sizeof(isom_lpcm_bunch_t) );
    if( !dst_bunch )
        return -1;
    if( lsmash_add_entry( timeline->bunch_list, dst_bunch ) )
    {
        free( dst_bunch );
        return -1;
    }
    *dst_bunch = *src_bunch;
    return 0;
}

static int isom_add_portable_chunk_entry( isom_timeline_t *timeline, isom_portable_chunk_t *src_chunk )
{
    isom_portable_chunk_t *dst_chunk = malloc( sizeof(isom_portable_chunk_t) );
    if( !dst_chunk )
        return -1;
    if( lsmash_add_entry( timeline->chunk_list, dst_chunk ) )
    {
        free( dst_chunk );
        return -1;
    }
    *dst_chunk = *src_chunk;
    return 0;
}

static int isom_compare_lpcm_sample_info( isom_lpcm_bunch_t *bunch, isom_sample_info_t *info )
{
    return info->duration != bunch->duration
        || info->offset   != bunch->offset
        || info->length   != bunch->length
        || info->index    != bunch->index
        || info->chunk    != bunch->chunk;
}

static void isom_update_bunch( isom_lpcm_bunch_t *bunch, isom_sample_info_t *info )
{
    bunch->pos          = info->pos;
    bunch->duration     = info->duration;
    bunch->offset       = info->offset;
    bunch->length       = info->length;
    bunch->index        = info->index;
    bunch->chunk        = info->chunk;
    bunch->prop         = info->prop;
    bunch->sample_count = 1;
}

static isom_lpcm_bunch_t *isom_get_bunch( isom_timeline_t *timeline, uint32_t sample_number )
{
    if( sample_number >= timeline->last_accessed_lpcm_bunch_first_sample_number
     && sample_number < timeline->last_accessed_lpcm_bunch_first_sample_number + timeline->last_accessed_lpcm_bunch_sample_count )
        /* Get from the last accessed LPCM bunch. */
        return (isom_lpcm_bunch_t *)lsmash_get_entry_data( timeline->bunch_list, timeline->last_accessed_lpcm_bunch_number );
    uint32_t first_sample_number_in_next_bunch;
    uint32_t bunch_number = 1;
    uint64_t bunch_dts;
    if( timeline->last_accessed_lpcm_bunch_first_sample_number
     && sample_number >= timeline->last_accessed_lpcm_bunch_first_sample_number )
    {
        first_sample_number_in_next_bunch = timeline->last_accessed_lpcm_bunch_first_sample_number + timeline->last_accessed_lpcm_bunch_sample_count;
        bunch_number += timeline->last_accessed_lpcm_bunch_number;
        bunch_dts = timeline->last_accessed_lpcm_bunch_dts + timeline->last_accessed_lpcm_bunch_duration * timeline->last_accessed_lpcm_bunch_sample_count;
    }
    else
    {
        /* Seek from the first LPCM bunch. */
        first_sample_number_in_next_bunch = 1;
        bunch_dts = 0;
    }
    isom_lpcm_bunch_t *bunch = (isom_lpcm_bunch_t *)lsmash_get_entry_data( timeline->bunch_list, bunch_number++ );
    if( !bunch )
        return NULL;
    first_sample_number_in_next_bunch += bunch->sample_count;
    while( sample_number >= first_sample_number_in_next_bunch )
    {
        bunch_dts += bunch->duration * bunch->sample_count;
        bunch = (isom_lpcm_bunch_t *)lsmash_get_entry_data( timeline->bunch_list, bunch_number++ );
        if( !bunch )
            return NULL;
        first_sample_number_in_next_bunch += bunch->sample_count;
    }
    timeline->last_accessed_lpcm_bunch_dts                 = bunch_dts;
    timeline->last_accessed_lpcm_bunch_number              = bunch_number - 1;
    timeline->last_accessed_lpcm_bunch_duration            = bunch->duration;
    timeline->last_accessed_lpcm_bunch_sample_count        = bunch->sample_count;
    timeline->last_accessed_lpcm_bunch_first_sample_number = first_sample_number_in_next_bunch - bunch->sample_count;
    return bunch;
}

static int isom_get_dts_from_info_list( isom_timeline_t *timeline, uint32_t sample_number, uint64_t *dts )
{
    if( sample_number == timeline->last_accessed_sample_number )
        *dts = timeline->last_accessed_sample_dts;
    else if( sample_number == 1 )
        *dts = 0;
    else if( sample_number == timeline->last_accessed_sample_number + 1 )
    {
        isom_sample_info_t *info = (isom_sample_info_t *)lsmash_get_entry_data( timeline->info_list, timeline->last_accessed_sample_number );
        if( !info )
            return -1;
        *dts = timeline->last_accessed_sample_dts + info->duration;
    }
    else if( sample_number == timeline->last_accessed_sample_number - 1 )
    {
        isom_sample_info_t *info = (isom_sample_info_t *)lsmash_get_entry_data( timeline->info_list, timeline->last_accessed_sample_number - 1 );
        if( !info )
            return -1;
        *dts = timeline->last_accessed_sample_dts - info->duration;
    }
    else
    {
        *dts = 0;
        uint32_t distance = sample_number - 1;
        lsmash_entry_t *entry;
        for( entry = timeline->info_list->head; entry; entry = entry->next )
        {
            isom_sample_info_t *info = (isom_sample_info_t *)entry->data;
            if( !info )
                return -1;
            if( distance-- == 0 )
                break;
            *dts += info->duration;
        }
        if( !entry )
            return -1;
    }
    /* Note: last_accessed_sample_number is always updated together with last_accessed_sample_dts, and vice versa. */
    timeline->last_accessed_sample_dts    = *dts;
    timeline->last_accessed_sample_number = sample_number;
    return 0;
}

static int isom_get_cts_from_info_list( isom_timeline_t *timeline, uint32_t sample_number, uint64_t *cts )
{
    if( isom_get_dts_from_info_list( timeline, sample_number, cts ) )
        return -1;
    isom_sample_info_t *info = (isom_sample_info_t *)lsmash_get_entry_data( timeline->info_list, sample_number );
    if( !info )
        return -1;
    *cts = timeline->ctd_shift ? (*cts + (int32_t)info->offset) : (*cts + info->offset);
    return 0;
}

static int isom_get_dts_from_bunch_list( isom_timeline_t *timeline, uint32_t sample_number, uint64_t *dts )
{
    isom_lpcm_bunch_t *bunch = isom_get_bunch( timeline, sample_number );
    if( !bunch )
        return -1;
    *dts = timeline->last_accessed_lpcm_bunch_dts + (sample_number - timeline->last_accessed_lpcm_bunch_first_sample_number) * bunch->duration;
    return 0;
}

static int isom_get_cts_from_bunch_list( isom_timeline_t *timeline, uint32_t sample_number, uint64_t *cts )
{
    isom_lpcm_bunch_t *bunch = isom_get_bunch( timeline, sample_number );
    if( !bunch )
        return -1;
    *cts = timeline->last_accessed_lpcm_bunch_dts + (sample_number - timeline->last_accessed_lpcm_bunch_first_sample_number) * bunch->duration + bunch->offset;
    return 0;
}

static int isom_get_sample_duration_from_info_list( isom_timeline_t *timeline, uint32_t sample_number, uint32_t *sample_duration )
{
    isom_sample_info_t *info = (isom_sample_info_t *)lsmash_get_entry_data( timeline->info_list, sample_number );
    if( !info )
        return -1;
    *sample_duration = info->duration;
    return 0;
}

static int isom_get_sample_duration_from_bunch_list( isom_timeline_t *timeline, uint32_t sample_number, uint32_t *sample_duration )
{
    isom_lpcm_bunch_t *bunch = isom_get_bunch( timeline, sample_number );
    if( !bunch )
        return -1;
    *sample_duration = bunch->duration;
    return 0;
}

static int isom_check_sample_existence_in_info_list( isom_timeline_t *timeline, uint32_t sample_number )
{
    return !!lsmash_get_entry_data( timeline->info_list, sample_number );
}

static int isom_check_sample_existence_in_bunch_list( isom_timeline_t *timeline, uint32_t sample_number )
{
    return !!isom_get_bunch( timeline, sample_number );
}

static lsmash_sample_t *isom_read_sample_data_from_stream( lsmash_root_t *root, isom_timeline_t *timeline, isom_portable_chunk_t *chunk,
                                                           uint32_t sample_length, uint64_t sample_pos )
{
    if( (timeline->last_accessed_chunk_number != chunk->number)
     || (timeline->last_accessed_offset > sample_pos)
     || (timeline->last_read_size < (sample_pos + sample_length - timeline->last_accessed_offset)) )
    {
        /* Read data of a chunk in the stream. */
        uint64_t read_size;
        uint64_t seek_pos;
        if( root->max_read_size >= chunk->length )
        {
            read_size = chunk->length;
            seek_pos = chunk->data_offset;
        }
        else
        {
            read_size = LSMASH_MAX( root->max_read_size, sample_length );
            seek_pos = sample_pos;
        }
        lsmash_bs_t *bs = root->bs;
        lsmash_fseek( bs->stream, seek_pos, SEEK_SET );
        lsmash_bs_empty( bs );
        if( lsmash_bs_read_data( bs, read_size ) )
            return NULL;
        void *temp = lsmash_bs_export_data( bs, NULL );
        lsmash_bs_empty( bs );
        if( !temp )
            return NULL;
        chunk->data = temp;
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
    uint64_t offset_from_seek = sample_pos - timeline->last_accessed_offset;
    sample->data = lsmash_memdup( chunk->data + offset_from_seek, sample_length );
    if( !sample->data )
    {
        lsmash_delete_sample( sample );
        return NULL;
    }
    return sample;
}

static lsmash_sample_t *isom_get_lpcm_sample_from_media_timeline( lsmash_root_t *root, isom_timeline_t *timeline, uint32_t sample_number )
{
    isom_lpcm_bunch_t *bunch = isom_get_bunch( timeline, sample_number );
    if( !bunch )
        return NULL;
    /* Get data of a sample from the stream. */
    isom_portable_chunk_t *chunk = bunch->chunk;
    uint32_t sample_number_offset = sample_number - timeline->last_accessed_lpcm_bunch_first_sample_number;
    uint64_t sample_pos = bunch->pos + sample_number_offset * bunch->length;
    lsmash_sample_t *sample = isom_read_sample_data_from_stream( root, timeline, chunk, bunch->length, sample_pos );
    if( !sample )
        return NULL;
    /* Get sample info. */
    sample->dts    = timeline->last_accessed_lpcm_bunch_dts + sample_number_offset * bunch->duration;
    sample->cts    = timeline->ctd_shift ? (sample->dts + (int32_t)bunch->offset) : (sample->dts + bunch->offset);
    sample->length = bunch->length;
    sample->index  = bunch->index;
    sample->prop   = bunch->prop;
    return sample;
}

static lsmash_sample_t *isom_get_sample_from_media_timeline( lsmash_root_t *root, isom_timeline_t *timeline, uint32_t sample_number )
{
    uint64_t dts;
    if( isom_get_dts_from_info_list( timeline, sample_number, &dts ) )
        return NULL;
    isom_sample_info_t *info = (isom_sample_info_t *)lsmash_get_entry_data( timeline->info_list, sample_number );
    if( !info )
        return NULL;
    /* Get data of a sample from the stream. */
    isom_portable_chunk_t *chunk = info->chunk;
    lsmash_sample_t *sample = isom_read_sample_data_from_stream( root, timeline, chunk, info->length, info->pos );
    if( !sample )
        return NULL;
    /* Get sample info. */
    sample->dts    = dts;
    sample->cts    = timeline->ctd_shift ? (dts + (int32_t)info->offset) : (dts + info->offset);
    sample->length = info->length;
    sample->index  = info->index;
    sample->prop   = info->prop;
    return sample;
}

static int isom_get_lpcm_sample_info_from_media_timeline( isom_timeline_t *timeline, uint32_t sample_number, lsmash_sample_t *sample )
{
    isom_lpcm_bunch_t *bunch = isom_get_bunch( timeline, sample_number );
    if( !bunch )
        return -1;
    uint32_t sample_number_offset = sample_number - timeline->last_accessed_lpcm_bunch_first_sample_number;
    sample->dts    = timeline->last_accessed_lpcm_bunch_dts + sample_number_offset * bunch->duration;
    sample->cts    = timeline->ctd_shift ? (sample->dts + (int32_t)bunch->offset) : (sample->dts + bunch->offset);
    sample->length = bunch->length;
    sample->index  = bunch->index;
    sample->prop   = bunch->prop;
    return 0;
}

static int isom_get_sample_info_from_media_timeline( isom_timeline_t *timeline, uint32_t sample_number, lsmash_sample_t *sample )
{
    uint64_t dts;
    if( isom_get_dts_from_info_list( timeline, sample_number, &dts ) )
        return -1;
    isom_sample_info_t *info = (isom_sample_info_t *)lsmash_get_entry_data( timeline->info_list, sample_number );
    if( !info )
        return -1;
    sample->dts    = dts;
    sample->cts    = timeline->ctd_shift ? (dts + (int32_t)info->offset) : (dts + info->offset);
    sample->length = info->length;
    sample->index  = info->index;
    sample->prop   = info->prop;
    return 0;
}

static int isom_get_lpcm_sample_property_from_media_timeline( isom_timeline_t *timeline, uint32_t sample_number, lsmash_sample_property_t *prop )
{
    memset( prop, 0, sizeof(lsmash_sample_property_t) );
    prop->ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    return 0;
}

static int isom_get_sample_property_from_media_timeline( isom_timeline_t *timeline, uint32_t sample_number, lsmash_sample_property_t *prop )
{
    isom_sample_info_t *info = (isom_sample_info_t *)lsmash_get_entry_data( timeline->info_list, sample_number );
    if( !info )
        return -1;
    *prop = info->prop;
    return 0;
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
    int movie_framemts_present = (root->moov->mvex && root->moof_list && root->moof_list->head);
    if( !description )
        goto fail;
    if( !movie_framemts_present && (!stts_entry || !stsc_entry || !stco_entry || !stco_entry->data || (next_stsc_entry && !next_stsc_entry->data)) )
        goto fail;
    int all_sync = !stss;
    int large_presentation = stco->large_presentation || lsmash_check_box_type_identical( stco->type, ISOM_BOX_TYPE_CO64 );
    int is_lpcm_audio = isom_is_lpcm_audio( description );
    int iso_sdtp = root->max_isom_version >= 2 || root->avc_extensions;
    int allow_negative_sample_offset = ctts && ((root->max_isom_version >= 4 && ctts->version == 1) || root->qt_compatible);
    uint32_t sample_number = 1;
    uint32_t sample_number_in_stts_entry = 1;
    uint32_t sample_number_in_ctts_entry = 1;
    uint32_t sample_number_in_sbgp_roll_entry = 1;
    uint32_t sample_number_in_sbgp_rap_entry  = 1;
    uint32_t sample_number_in_chunk = 1;
    uint64_t dts = 0;
    uint32_t chunk_number = 1;
    uint64_t offset_from_chunk = 0;
    uint64_t data_offset = stco_entry && stco_entry->data
                         ? large_presentation
                             ? ((isom_co64_entry_t *)stco_entry->data)->chunk_offset
                             : ((isom_stco_entry_t *)stco_entry->data)->chunk_offset
                         : 0;
    uint32_t constant_sample_size = is_lpcm_audio
                                  ? isom_get_lpcm_sample_size( (isom_audio_entry_t *)description )
                                  : stsz->sample_size;
    /* Copy edits. */
    while( elst_entry )
    {
        isom_elst_entry_t *edit = (isom_elst_entry_t *)lsmash_memdup( elst_entry->data, sizeof(isom_elst_entry_t) );
        if( !edit
         || lsmash_add_entry( timeline->edit_list, edit ) )
            goto fail;
        elst_entry = elst_entry->next;
    }
    /* Check what the first 2-bits of sample dependency means.
     * This check is for chimera of ISO Base Media and QTFF. */
    if( iso_sdtp && sdtp_entry )
    {
        while( sdtp_entry )
        {
            isom_sdtp_entry_t *sdtp_data = (isom_sdtp_entry_t *)sdtp_entry->data;
            if( !sdtp_data )
                goto fail;
            if( sdtp_data->is_leading > 1 )
                break;      /* Apparently, it's defined under ISO Base Media. */
            if( (sdtp_data->is_leading == 1) && (sdtp_data->sample_depends_on == ISOM_SAMPLE_IS_INDEPENDENT) )
            {
                /* Obviously, it's not defined under ISO Base Media. */
                iso_sdtp = 0;
                break;
            }
            sdtp_entry = sdtp_entry->next;
        }
        sdtp_entry = sdtp->list->head;
    }
    /* Construct media timeline. */
    isom_portable_chunk_t chunk;
    chunk.data_offset = data_offset;
    chunk.length      = 0;
    chunk.data        = NULL;
    chunk.number = chunk_number;
    if( isom_add_portable_chunk_entry( timeline, &chunk ) )
        goto fail;
    uint32_t distance = NO_RANDOM_ACCESS_POINT;
    isom_lpcm_bunch_t bunch = { 0 };
    while( sample_number <= stsz->sample_count )
    {
        isom_sample_info_t info = { 0 };
        /* Get sample duration and sample offset. */
        isom_stts_entry_t *stts_data = (isom_stts_entry_t *)stts_entry->data;
        if( !stts_data )
            goto fail;
        INCREMENT_SAMPLE_NUMBER_IN_ENTRY( sample_number_in_stts_entry, stts_entry, stts_data );
        info.duration = stts_data->sample_delta;
        timeline->media_duration += info.duration;
        if( ctts_entry )
        {
            isom_ctts_entry_t *ctts_data = (isom_ctts_entry_t *)ctts_entry->data;
            if( !ctts_data )
                goto fail;
            INCREMENT_SAMPLE_NUMBER_IN_ENTRY( sample_number_in_ctts_entry, ctts_entry, ctts_data );
            info.offset = ctts_data->sample_offset;
        }
        else
            info.offset = 0;
        if( allow_negative_sample_offset )
        {
            uint64_t cts = dts + (int32_t)info.offset;
            if( (cts + timeline->ctd_shift) < dts )
                timeline->ctd_shift = dts - cts;
        }
        dts += info.duration;
        if( !is_lpcm_audio )
        {
            /* Check whether sync sample or not. */
            if( stss_entry )
            {
                isom_stss_entry_t *stss_data = (isom_stss_entry_t *)stss_entry->data;
                if( !stss_data )
                    goto fail;
                if( sample_number == stss_data->sample_number )
                {
                    info.prop.ra_flags |= ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
                    stss_entry = stss_entry->next;
                    distance = 0;
                }
            }
            else if( all_sync )
                /* Don't reset distance as 0 since MDCT-based audio frames need pre-roll for correct presentation
                 * though all of them could be marked as a sync sample. */
                info.prop.ra_flags |= ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            /* Check whether partial sync sample or not. */
            if( stps_entry )
            {
                isom_stps_entry_t *stps_data = (isom_stps_entry_t *)stps_entry->data;
                if( !stps_data )
                    goto fail;
                if( sample_number == stps_data->sample_number )
                {
                    info.prop.ra_flags |= QT_SAMPLE_RANDOM_ACCESS_FLAG_PARTIAL_SYNC | QT_SAMPLE_RANDOM_ACCESS_FLAG_RAP;
                    stps_entry = stps_entry->next;
                    distance = 0;
                }
            }
            /* Get sample dependency info. */
            if( sdtp_entry )
            {
                isom_sdtp_entry_t *sdtp_data = (isom_sdtp_entry_t *)sdtp_entry->data;
                if( !sdtp_data )
                    goto fail;
                if( iso_sdtp )
                    info.prop.leading       = sdtp_data->is_leading;
                else
                    info.prop.allow_earlier = sdtp_data->is_leading;
                info.prop.independent = sdtp_data->sample_depends_on;
                info.prop.disposable  = sdtp_data->sample_is_depended_on;
                info.prop.redundant   = sdtp_data->sample_has_redundancy;
                sdtp_entry = sdtp_entry->next;
            }
            /* Get roll recovery grouping info. */
            if( sbgp_roll_entry )
            {
                isom_group_assignment_entry_t *assignment = (isom_group_assignment_entry_t *)sbgp_roll_entry->data;
                if( !assignment )
                    goto fail;
                if( assignment->group_description_index )
                {
                    isom_roll_entry_t *roll_data = (isom_roll_entry_t *)lsmash_get_entry_data( sgpd_roll->list, assignment->group_description_index );
                    if( !roll_data )
                        goto fail;
                    if( roll_data->roll_distance > 0 )
                    {
                        /* post-roll */
                        info.prop.post_roll.complete = sample_number + roll_data->roll_distance;
                        if( info.prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE )
                            info.prop.ra_flags |= ISOM_SAMPLE_RANDOM_ACCESS_FLAG_POST_ROLL_START;
                    }
                    else if( roll_data->roll_distance < 0 )
                    {
                        /* pre-roll */
                        info.prop.pre_roll.distance = -roll_data->roll_distance;
                        if( info.prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE )
                            info.prop.ra_flags |= ISOM_SAMPLE_RANDOM_ACCESS_FLAG_PRE_ROLL_END;
                    }
                }
                INCREMENT_SAMPLE_NUMBER_IN_ENTRY( sample_number_in_sbgp_roll_entry, sbgp_roll_entry, assignment );
            }
            info.prop.post_roll.identifier = sample_number;
            /* Get random access point grouping info. */
            if( sbgp_rap_entry )
            {
                isom_group_assignment_entry_t *assignment = (isom_group_assignment_entry_t *)sbgp_rap_entry->data;
                if( !assignment )
                    goto fail;
                if( assignment->group_description_index && (info.prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE) )
                {
                    isom_rap_entry_t *rap_data = (isom_rap_entry_t *)lsmash_get_entry_data( sgpd_rap->list, assignment->group_description_index );
                    if( !rap_data )
                        goto fail;
                    /* If this is not an open RAP, we treat it as an unknown RAP since non-IDR sample could make a closed GOP. */
                    info.prop.ra_flags |= (rap_data->num_leading_samples_known && !!rap_data->num_leading_samples)
                                        ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP
                                        : ISOM_SAMPLE_RANDOM_ACCESS_FLAG_RAP;
                    distance = 0;
                }
                INCREMENT_SAMPLE_NUMBER_IN_ENTRY( sample_number_in_sbgp_rap_entry, sbgp_rap_entry, assignment );
            }
            if( distance != NO_RANDOM_ACCESS_POINT )
            {
                if( info.prop.pre_roll.distance == 0 )
                    info.prop.pre_roll.distance = distance;
                ++distance;
            }
        }
        else
            /* All LPCMFrame is a sync sample. */
            info.prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
        /* Get size of sample in the stream. */
        if( is_lpcm_audio || !stsz_entry )
            info.length = constant_sample_size;
        else
        {
            if( !stsz_entry->data )
                goto fail;
            info.length = ((isom_stsz_entry_t *)stsz_entry->data)->entry_size;
            stsz_entry = stsz_entry->next;
        }
        timeline->max_sample_size = LSMASH_MAX( timeline->max_sample_size, info.length );
        /* Get chunk info. */
        info.pos = data_offset;
        info.index = stsc_data->sample_description_index;
        info.chunk = (isom_portable_chunk_t *)timeline->chunk_list->tail->data;
        offset_from_chunk += info.length;
        if( sample_number_in_chunk == stsc_data->samples_per_chunk )
        {
            /* Set the length of the last chunk. */
            if( info.chunk )
                info.chunk->length = offset_from_chunk;
            /* Move the next chunk. */
            sample_number_in_chunk = 1;
            if( stco_entry )
                stco_entry = stco_entry->next;
            if( stco_entry && stco_entry->data )
                data_offset = large_presentation
                            ? ((isom_co64_entry_t *)stco_entry->data)->chunk_offset
                            : ((isom_stco_entry_t *)stco_entry->data)->chunk_offset;
            chunk.data_offset = data_offset;
            chunk.length      = 0;
            chunk.data        = NULL;
            chunk.number      = ++chunk_number;
            if( isom_add_portable_chunk_entry( timeline, &chunk ) )
                goto fail;
            offset_from_chunk = 0;
            /* Check if the next entry is broken. */
            while( next_stsc_entry && chunk_number > ((isom_stsc_entry_t *)next_stsc_entry->data)->first_chunk )
            {
                /* Just skip broken next entry. */
                lsmash_log( LSMASH_LOG_WARNING, "ignore broken entry in Sample To Chunk Box.\n" );
                lsmash_log( LSMASH_LOG_WARNING, "timeline might be corrupted.\n" );
                next_stsc_entry = next_stsc_entry->next;
                if( next_stsc_entry && !next_stsc_entry->data )
                    goto fail;
            }
            /* Check if the next chunk belongs to the next sequence of chunks. */
            if( next_stsc_entry && chunk_number == ((isom_stsc_entry_t *)next_stsc_entry->data)->first_chunk )
            {
                stsc_entry = next_stsc_entry;
                next_stsc_entry = next_stsc_entry->next;
                if( next_stsc_entry && !next_stsc_entry->data )
                    goto fail;
                stsc_data = (isom_stsc_entry_t *)stsc_entry->data;
                /* Update sample description. */
                description = (isom_sample_entry_t *)lsmash_get_entry_data( stsd->list, stsc_data->sample_description_index );
                is_lpcm_audio = isom_is_lpcm_audio( description );
                if( is_lpcm_audio )
                    constant_sample_size = isom_get_lpcm_sample_size( (isom_audio_entry_t *)description );
            }
        }
        else
        {
            ++sample_number_in_chunk;
            data_offset += info.length;
        }
        /* OK. Let's add its info. */
        if( is_lpcm_audio )
        {
            if( sample_number == 1 )
                isom_update_bunch( &bunch, &info );
            else if( isom_compare_lpcm_sample_info( &bunch, &info ) )
            {
                if( isom_add_lpcm_bunch_entry( timeline, &bunch ) )
                    goto fail;
                isom_update_bunch( &bunch, &info );
            }
            else
                ++ bunch.sample_count;
        }
        else if( isom_add_sample_info_entry( timeline, &info ) )
            goto fail;
        if( timeline->info_list->entry_count && timeline->bunch_list->entry_count )
        {
            lsmash_log( LSMASH_LOG_ERROR, "LPCM + non-LPCM track is not supported.\n" );
            goto fail;
        }
        ++sample_number;
    }
    isom_portable_chunk_t *last_chunk = lsmash_get_entry_data( timeline->chunk_list, timeline->chunk_list->entry_count );
    if( last_chunk )
    {
        if( offset_from_chunk )
            last_chunk->length = offset_from_chunk;
        else
        {
            /* Remove the last invalid chunk. */
            lsmash_remove_entry( timeline->chunk_list, timeline->chunk_list->entry_count, NULL );
            --chunk_number;
        }
    }
    uint32_t sample_count = sample_number - 1;
    if( movie_framemts_present )
    {
        isom_tfra_entry_t *tfra = isom_get_tfra( root->mfra, track_ID );
        lsmash_entry_t *tfra_entry = tfra && tfra->list ? tfra->list->head : NULL;
        isom_tfra_location_time_entry_t *rap = tfra_entry ? (isom_tfra_location_time_entry_t *)tfra_entry->data : NULL;
        chunk.data_offset = 0;
        chunk.length      = 0;
        /* Movie fragments */
        for( lsmash_entry_t *moof_entry = root->moof_list->head; moof_entry; moof_entry = moof_entry->next )
        {
            isom_moof_entry_t *moof = (isom_moof_entry_t *)moof_entry->data;
            if( !moof || !moof->traf_list )
                goto fail;
            uint64_t last_sample_end_pos = 0;
            /* Track fragments */
            uint32_t traf_number = 1;
            for( lsmash_entry_t *traf_entry = moof->traf_list->head; traf_entry; traf_entry = traf_entry->next )
            {
                isom_traf_entry_t *traf = (isom_traf_entry_t *)traf_entry->data;
                if( !traf )
                    goto fail;
                isom_tfhd_t *tfhd = traf->tfhd;
                if( !tfhd )
                    goto fail;
                isom_trex_entry_t *trex = isom_get_trex( root->moov->mvex, tfhd->track_ID );
                if( !trex )
                    goto fail;
                /* Ignore ISOM_TF_FLAGS_DURATION_IS_EMPTY flag even if set. */
                if( !traf->trun_list || !traf->trun_list->head )
                {
                    ++traf_number;
                    continue;
                }
                /* Get base_data_offset. */
                uint64_t base_data_offset;
                if( tfhd->flags & ISOM_TF_FLAGS_BASE_DATA_OFFSET_PRESENT )
                    base_data_offset = tfhd->base_data_offset;
                else if( traf_entry == moof->traf_list->head )
                    base_data_offset = moof->pos;
                else
                    base_data_offset = last_sample_end_pos;
                int need_data_offset_only = (tfhd->track_ID != track_ID);
                /* Track runs */
                uint32_t trun_number = 1;
                for( lsmash_entry_t *trun_entry = traf->trun_list->head; trun_entry; trun_entry = trun_entry->next )
                {
                    isom_trun_entry_t *trun = (isom_trun_entry_t *)trun_entry->data;
                    if( !trun )
                        goto fail;
                    if( trun->sample_count == 0 )
                    {
                        ++trun_number;
                        continue;
                    }
                    /* Get data_offset. */
                    if( trun->flags & ISOM_TR_FLAGS_DATA_OFFSET_PRESENT )
                        data_offset = trun->data_offset + base_data_offset;
                    else if( trun_entry == traf->trun_list->head )
                        data_offset = base_data_offset;
                    else
                        data_offset = last_sample_end_pos;
                    /* */
                    uint32_t sample_description_index = 0;
                    isom_sdtp_entry_t *sdtp_data = NULL;
                    if( !need_data_offset_only )
                    {
                        /* Each track run can be considered as a chunk.
                         * Here, we consider physically consecutive track runs as one chunk. */
                        if( chunk.data_offset + chunk.length != data_offset )
                        {
                            chunk.data_offset = data_offset;
                            chunk.length      = 0;
                            chunk.data        = NULL;
                            chunk.number      = ++chunk_number;
                            if( isom_add_portable_chunk_entry( timeline, &chunk ) )
                                goto fail;
                        }
                        /* Get sample_description_index of this track fragment. */
                        if( tfhd->flags & ISOM_TF_FLAGS_SAMPLE_DESCRIPTION_INDEX_PRESENT )
                            sample_description_index = tfhd->sample_description_index;
                        else
                            sample_description_index = trex->default_sample_description_index;
                        description = (isom_sample_entry_t *)lsmash_get_entry_data( stsd->list, sample_description_index );
                        is_lpcm_audio = isom_is_lpcm_audio( description );
                        if( is_lpcm_audio )
                            constant_sample_size = isom_get_lpcm_sample_size( (isom_audio_entry_t *)description );
                        /* Get dependency info for this track fragment. */
                        sdtp_entry = traf->sdtp && traf->sdtp->list ? traf->sdtp->list->head : NULL;
                        sdtp_data = sdtp_entry && sdtp_entry->data ? (isom_sdtp_entry_t *)sdtp_entry->data : NULL;
                    }
                    /* Get info of each sample. */
                    lsmash_entry_t *row_entry = trun->optional && trun->optional->head ? trun->optional->head : NULL;
                    sample_number = 1;
                    while( sample_number <= trun->sample_count )
                    {
                        isom_sample_info_t info = { 0 };
                        isom_trun_optional_row_t *row = row_entry && row_entry->data ? (isom_trun_optional_row_t *)row_entry->data : NULL;
                        /* Get sample_size */
                        if( row && (trun->flags & ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT) )
                            info.length = row->sample_size;
                        else if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_SIZE_PRESENT )
                            info.length = tfhd->default_sample_size;
                        else
                            info.length = trex->default_sample_size;
                        if( !need_data_offset_only )
                        {
                            info.pos = data_offset;
                            info.index = sample_description_index;
                            info.chunk = (isom_portable_chunk_t *)timeline->chunk_list->tail->data;
                            info.chunk->length += info.length;
                            /* Get sample_duration. */
                            if( row && (trun->flags & ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT) )
                                info.duration = row->sample_duration;
                            else if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT )
                                info.duration = tfhd->default_sample_duration;
                            else
                                info.duration = trex->default_sample_duration;
                            /* Get composition time offset. */
                            if( row && (trun->flags & ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT) )
                            {
                                info.offset = row->sample_composition_time_offset;
                                /* Check composition to decode timeline shift. */
                                if( root->max_isom_version >= 6 && trun->version != 0 )
                                {
                                    uint64_t cts = dts + (int32_t)info.offset;
                                    if( (cts + timeline->ctd_shift) < dts )
                                        timeline->ctd_shift = dts - cts;
                                }
                            }
                            else
                                info.offset = 0;
                            dts += info.duration;
                            /* Update media duration and maximun sample size. */
                            timeline->media_duration += info.duration;
                            timeline->max_sample_size = LSMASH_MAX( timeline->max_sample_size, info.length );
                            if( !is_lpcm_audio )
                            {
                                /* Get sample_flags. */
                                isom_sample_flags_t sample_flags;
                                if( sample_number == 1 && (trun->flags & ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT) )
                                    sample_flags = trun->first_sample_flags;
                                else if( row && (trun->flags & ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT) )
                                    sample_flags = row->sample_flags;
                                else if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT )
                                    sample_flags = tfhd->default_sample_flags;
                                else
                                    sample_flags = trex->default_sample_flags;
                                if( sdtp_data )
                                {
                                    /* Independent and Disposable Samples Box overrides the information from sample_flags.
                                     * There is no description in the specification about this, but the intention should be such a thing.
                                     * The ground is that sample_flags is placed in media layer
                                     * while Independent and Disposable Samples Box is placed in track or presentation layer. */
                                    info.prop.leading     = sdtp_data->is_leading;
                                    info.prop.independent = sdtp_data->sample_depends_on;
                                    info.prop.disposable  = sdtp_data->sample_is_depended_on;
                                    info.prop.redundant   = sdtp_data->sample_has_redundancy;
                                    if( sdtp_entry )
                                        sdtp_entry = sdtp_entry->next;
                                    sdtp_data = sdtp_entry ? (isom_sdtp_entry_t *)sdtp_entry->data : NULL;
                                }
                                else
                                {
                                    info.prop.leading     = sample_flags.is_leading;
                                    info.prop.independent = sample_flags.sample_depends_on;
                                    info.prop.disposable  = sample_flags.sample_is_depended_on;
                                    info.prop.redundant   = sample_flags.sample_has_redundancy;
                                }
                                /* Check this sample is a sync sample or not.
                                 * Note: all sync sample shall be independent. */
                                if( !sample_flags.sample_is_non_sync_sample
                                 && info.prop.independent != ISOM_SAMPLE_IS_NOT_INDEPENDENT )
                                {
                                    info.prop.ra_flags |= ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
                                    distance = 0;
                                }
                                /* Get the location of the sync sample from 'tfra' if it is not set up yet.
                                 * Note: there is no guarantee that its entries are placed in a specific order. */
                                if( tfra )
                                {
                                    if( tfra->number_of_entry == 0
                                     && info.prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE )
                                        info.prop.ra_flags |= ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
                                    if( rap && rap->moof_offset == moof->pos && rap->traf_number == traf_number
                                     && rap->trun_number == trun_number && rap->sample_number == sample_number )
                                    {
                                        if( info.prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE )
                                            info.prop.ra_flags |= ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
                                        if( tfra_entry )
                                            tfra_entry = tfra_entry->next;
                                        rap = tfra_entry ? (isom_tfra_location_time_entry_t *)tfra_entry->data : NULL;
                                    }
                                }
                                /* Set up distance from the previous random access point. */
                                if( distance != NO_RANDOM_ACCESS_POINT )
                                {
                                    if( info.prop.pre_roll.distance == 0 )
                                        info.prop.pre_roll.distance = distance;
                                    ++distance;
                                }
                                /* OK. Let's add its info. */
                                if( isom_add_sample_info_entry( timeline, &info ) )
                                    goto fail;
                            }
                            else
                            {
                                /* All LPCMFrame is a sync sample. */
                                info.prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
                                /* OK. Let's add its info. */
                                if( sample_count == 0 && sample_number == 1 )
                                    isom_update_bunch( &bunch, &info );
                                else if( isom_compare_lpcm_sample_info( &bunch, &info ) )
                                {
                                    if( isom_add_lpcm_bunch_entry( timeline, &bunch ) )
                                        goto fail;
                                    isom_update_bunch( &bunch, &info );
                                }
                                else
                                    ++ bunch.sample_count;
                            }
                            if( timeline->info_list->entry_count && timeline->bunch_list->entry_count )
                            {
                                lsmash_log( LSMASH_LOG_ERROR, "LPCM + non-LPCM track is not supported.\n" );
                                goto fail;
                            }
                        }
                        data_offset += info.length;
                        last_sample_end_pos = data_offset;
                        if( row_entry )
                            row_entry = row_entry->next;
                        ++sample_number;
                    }
                    if( !need_data_offset_only )
                        sample_count += sample_number - 1;
                    ++trun_number;
                }   /* Track runs */
                ++traf_number;
            }   /* Track fragments */
        }   /* Movie fragments */
    }
    else if( timeline->chunk_list->entry_count == 0 )
        goto fail;  /* No samples in this track. */
    if( bunch.sample_count && isom_add_lpcm_bunch_entry( timeline, &bunch ) )
        goto fail;
    if( lsmash_add_entry( root->timeline, timeline ) )
        goto fail;
    /* Finish timeline construction. */
    timeline->sample_count = sample_count;
    if( timeline->info_list->entry_count )
    {
        timeline->get_dts                = isom_get_dts_from_info_list;
        timeline->get_cts                = isom_get_cts_from_info_list;
        timeline->get_sample_duration    = isom_get_sample_duration_from_info_list;
        timeline->check_sample_existence = isom_check_sample_existence_in_info_list;
        timeline->get_sample             = isom_get_sample_from_media_timeline;
        timeline->get_sample_info        = isom_get_sample_info_from_media_timeline;
        timeline->get_sample_property    = isom_get_sample_property_from_media_timeline;
    }
    else
    {
        timeline->get_dts                = isom_get_dts_from_bunch_list;
        timeline->get_cts                = isom_get_cts_from_bunch_list;
        timeline->get_sample_duration    = isom_get_sample_duration_from_bunch_list;
        timeline->check_sample_existence = isom_check_sample_existence_in_bunch_list;
        timeline->get_sample             = isom_get_lpcm_sample_from_media_timeline;
        timeline->get_sample_info        = isom_get_lpcm_sample_info_from_media_timeline;
        timeline->get_sample_property    = isom_get_lpcm_sample_property_from_media_timeline;
    }
    return 0;
fail:
    isom_destruct_timeline_direct( timeline );
    return -1;
}

int lsmash_get_dts_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number, uint64_t *dts )
{
    if( !sample_number || !dts )
        return -1;
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    if( !timeline || sample_number > timeline->sample_count )
        return -1;
     return timeline->get_dts( timeline, sample_number, dts );
}

int lsmash_get_cts_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number, uint64_t *cts )
{
    if( !sample_number || !cts )
        return -1;
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    if( !timeline || sample_number > timeline->sample_count )
        return -1;
     return timeline->get_cts( timeline, sample_number, cts );
}

lsmash_sample_t *lsmash_get_sample_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number )
{
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    return timeline ? timeline->get_sample( root, timeline, sample_number ) : NULL;
}

int lsmash_get_sample_info_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number, lsmash_sample_t *sample )
{
    if( !sample )
        return -1;
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    return timeline ? timeline->get_sample_info( timeline, sample_number, sample ) : -1;
}

int lsmash_get_sample_property_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number, lsmash_sample_property_t *prop )
{
    if( !prop )
        return -1;
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    return timeline ? timeline->get_sample_property( timeline, sample_number, prop ) : -1;
}

int lsmash_get_composition_to_decode_shift_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t *ctd_shift )
{
    if( !ctd_shift )
        return -1;
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    if( !timeline )
        return -1;
    *ctd_shift = timeline->ctd_shift;
    return 0;
}

static inline int isom_get_closest_past_random_accessible_point_from_media_timeline( isom_timeline_t *timeline, uint32_t sample_number, uint32_t *rap_number )
{
    lsmash_entry_t *entry = lsmash_get_entry( timeline->info_list, sample_number-- );
    if( !entry || !entry->data )
        return -1;
    isom_sample_info_t *info = (isom_sample_info_t *)entry->data;
    while( info->prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE )
    {
        entry = entry->prev;
        if( !entry || !entry->data )
            return -1;
        info = (isom_sample_info_t *)entry->data;
        --sample_number;
    }
    *rap_number = sample_number + 1;
    return 0;
}

static inline int isom_get_closest_future_random_accessible_point_from_media_timeline( isom_timeline_t *timeline, uint32_t sample_number, uint32_t *rap_number )
{
    lsmash_entry_t *entry = lsmash_get_entry( timeline->info_list, sample_number++ );
    if( !entry || !entry->data )
        return -1;
    isom_sample_info_t *info = (isom_sample_info_t *)entry->data;
    while( info->prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE )
    {
        entry = entry->next;
        if( !entry || !entry->data )
            return -1;
        info = (isom_sample_info_t *)entry->data;
        ++sample_number;
    }
    *rap_number = sample_number - 1;
    return 0;
}

static int isom_get_closest_random_accessible_point_from_media_timeline_internal( isom_timeline_t *timeline, uint32_t sample_number, uint32_t *rap_number )
{
    if( !timeline )
        return -1;
    if( isom_get_closest_past_random_accessible_point_from_media_timeline( timeline, sample_number, rap_number )
     && isom_get_closest_future_random_accessible_point_from_media_timeline( timeline, sample_number + 1, rap_number ) )
        return -1;
    return 0;
}

int lsmash_get_closest_random_accessible_point_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number, uint32_t *rap_number )
{
    if( sample_number == 0 || !rap_number )
        return -1;
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    if( timeline->info_list->entry_count == 0 )
    {
        *rap_number = sample_number;    /* All LPCM is sync sample. */
        return 0;
    }
    return isom_get_closest_random_accessible_point_from_media_timeline_internal( timeline, sample_number, rap_number );
}

int lsmash_get_closest_random_accessible_point_detail_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number,
                                                                           uint32_t *rap_number, lsmash_random_access_flag *ra_flags, uint32_t *leading, uint32_t *distance )
{
    if( sample_number == 0 )
        return -1;
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    if( timeline->info_list->entry_count == 0 )
    {
        /* All LPCM is sync sample. */
        *rap_number = sample_number;
        if( ra_flags )
            *ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
        if( leading )
            *leading  = 0;
        if( distance )
            *distance = 0;
        return 0;
    }
    if( isom_get_closest_random_accessible_point_from_media_timeline_internal( timeline, sample_number, rap_number ) )
        return -1;
    isom_sample_info_t *info = (isom_sample_info_t *)lsmash_get_entry_data( timeline->info_list, *rap_number );
    if( !info )
        return -1;
    if( ra_flags )
        *ra_flags = info->prop.ra_flags;
    if( leading )
        *leading  = 0;
    if( distance )
        *distance = 0;
    if( sample_number < *rap_number )
        /* Impossible to desire to decode the sample of given number correctly. */
        return 0;
    else if( !(info->prop.ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_GDR) )
    {
        if( leading )
        {
            /* Count leading samples. */
            sample_number = *rap_number + 1;
            uint64_t dts;
            if( isom_get_dts_from_info_list( timeline, *rap_number, &dts ) )
                return -1;
            uint64_t rap_cts = timeline->ctd_shift ? (dts + (int32_t)info->offset + timeline->ctd_shift) : (dts + info->offset);
            do
            {
                dts += info->duration;
                if( rap_cts <= dts )
                    break;  /* leading samples of this random accessible point must not be present more. */
                info = (isom_sample_info_t *)lsmash_get_entry_data( timeline->info_list, sample_number++ );
                if( !info )
                    break;
                uint64_t cts = timeline->ctd_shift ? (dts + (int32_t)info->offset + timeline->ctd_shift) : (dts + info->offset);
                if( rap_cts > cts )
                    ++ *leading;
            } while( 1 );
        }
        if( !distance )
            return 0;
        /* Measure distance from the first closest non-recovery random accessible point to the second. */
        uint32_t prev_rap_number = *rap_number;
        do
        {
            if( isom_get_closest_past_random_accessible_point_from_media_timeline( timeline, prev_rap_number - 1, &prev_rap_number ) )
                /* The previous random accessible point is not present. */
                return 0;
            info = (isom_sample_info_t *)lsmash_get_entry_data( timeline->info_list, prev_rap_number );
            if( !info )
                return -1;
            if( !(info->prop.ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_GDR) )
            {
                /* Decode shall already complete at the first closest non-recovery random accessible point if starting to decode from the second. */
                *distance = *rap_number - prev_rap_number;
                return 0;
            }
        } while( 1 );
    }
    if( !distance )
        return 0;
    /* Calculate roll-distance. */
    if( info->prop.pre_roll.distance )
    {
        /* Pre-roll recovery */
        uint32_t prev_rap_number = *rap_number;
        do
        {
            if( isom_get_closest_past_random_accessible_point_from_media_timeline( timeline, prev_rap_number - 1, &prev_rap_number )
             && *rap_number < info->prop.pre_roll.distance )
            {
                /* The previous random accessible point is not present.
                 * And sample of given number might be not able to decoded correctly. */
                *distance = 0;
                return 0;
            }
            if( prev_rap_number + info->prop.pre_roll.distance <= *rap_number )
            {
                /*
                 *                                          |<---- pre-roll distance ---->|
                 *                                          |<--------- distance -------->|
                 * media +++++++++++++++++++++++++ *** +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                 *                  ^                       ^                             ^                    ^
                 *       random accessible point         starting point        random accessible point   given sample
                 *                                                                   (complete)
                 */
                *distance = info->prop.pre_roll.distance;
                return 0;
            }
            else if( !(info->prop.ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_GDR) )
            {
                /*
                 *            |<------------ pre-roll distance ------------------>|
                 *                                      |<------ distance ------->|
                 * media ++++++++++++++++ *** ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                 *            ^                         ^                         ^                     ^
                 *                            random accessible point   random accessible point   given sample
                 *                                (starting point)            (complete)
                 */
                *distance = *rap_number - prev_rap_number;
                return 0;
            }
        } while( 1 );
    }
    /* Post-roll recovery */
    if( sample_number >= info->prop.post_roll.complete )
        /*
         *                  |<----- post-roll distance ----->|
         *            (distance = 0)
         * media +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
         *                  ^                                ^            ^
         *       random accessible point                 complete     given sample
         *          (starting point)
         */
        return 0;
    uint32_t prev_rap_number = *rap_number;
    do
    {
        if( isom_get_closest_past_random_accessible_point_from_media_timeline( timeline, prev_rap_number - 1, &prev_rap_number ) )
            /* The previous random accessible point is not present. */
            return 0;
        info = (isom_sample_info_t *)lsmash_get_entry_data( timeline->info_list, prev_rap_number );
        if( !info )
            return -1;
        if( !(info->prop.ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_GDR) || sample_number >= info->prop.post_roll.complete )
        {
            *distance = *rap_number - prev_rap_number;
            return 0;
        }
    } while( 1 );
}

int lsmash_check_sample_existence_in_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number )
{
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    return timeline ? timeline->check_sample_existence( timeline, sample_number ) : 0;
}

int lsmash_get_last_sample_delta_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t *last_sample_delta )
{
    if( !last_sample_delta )
        return -1;
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    return timeline ? timeline->get_sample_duration( timeline, timeline->sample_count, last_sample_delta ) : -1;
}

int lsmash_get_sample_delta_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number, uint32_t *sample_delta )
{
    if( !sample_delta )
        return -1;
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    return timeline ? timeline->get_sample_duration( timeline, sample_number, sample_delta ) : -1;
}

uint32_t lsmash_get_sample_count_in_media_timeline( lsmash_root_t *root, uint32_t track_ID )
{
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    if( !timeline )
        return 0;
    return timeline->sample_count;
}

uint32_t lsmash_get_max_sample_size_in_media_timeline( lsmash_root_t *root, uint32_t track_ID )
{
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    if( !timeline )
        return 0;
    return timeline->max_sample_size;
}

uint64_t lsmash_get_media_duration_from_media_timeline( lsmash_root_t *root, uint32_t track_ID )
{
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    if( !timeline )
        return 0;
    return timeline->media_duration;
}

int lsmash_copy_timeline_map( lsmash_root_t *dst, uint32_t dst_track_ID, lsmash_root_t *src, uint32_t src_track_ID )
{
    isom_trak_entry_t *dst_trak = isom_get_trak( dst, dst_track_ID );
    if( !dst->moov || !dst->moov->mvhd || !dst->moov->mvhd->timescale
     || !dst_trak || !dst_trak->mdia || !dst_trak->mdia->mdhd || !dst_trak->mdia->mdhd->timescale
     || !dst_trak->mdia->minf || !dst_trak->mdia->minf->stbl )
        return -1;
    if( dst_trak->edts && dst_trak->edts->elst )
        lsmash_remove_entries( dst_trak->edts->elst->list, NULL );
    uint32_t src_movie_timescale;
    uint32_t src_media_timescale;
    int32_t  src_ctd_shift;     /* Add timeline shift difference between src and dst to each media_time.
                                 * Therefore, call this function as later as possible. */
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
        src_ctd_shift       = src_timeline->ctd_shift;
        src_entry = src_timeline->edit_list->head;
    }
    else
    {
        if( !src->moov || !src->moov->mvhd || !src->moov->mvhd->timescale
         || !src_trak->mdia || !src_trak->mdia->mdhd || !src_trak->mdia->mdhd->timescale
         || !src_trak->mdia->minf || !src_trak->mdia->minf->stbl )
            return -1;
        src_movie_timescale = src->moov->mvhd->timescale;
        src_media_timescale = src_trak->mdia->mdhd->timescale;
        src_ctd_shift       = src_trak->mdia->minf->stbl->cslg ? src_trak->mdia->minf->stbl->cslg->compositionToDTSShift : 0;
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
    int32_t dst_ctd_shift = dst_trak->mdia->minf->stbl->cslg ? dst_trak->mdia->minf->stbl->cslg->compositionToDTSShift : 0;
    int32_t media_time_shift = src_ctd_shift - dst_ctd_shift;
    lsmash_entry_list_t *dst_list = dst_trak->edts->elst->list;
    while( src_entry )
    {
        isom_elst_entry_t *src_data = (isom_elst_entry_t *)src_entry->data;
        if( !src_data )
            return -1;
        isom_elst_entry_t *dst_data = (isom_elst_entry_t *)malloc( sizeof(isom_elst_entry_t) );
        if( !dst_data )
            return -1;
        dst_data->segment_duration = src_data->segment_duration                * ((double)dst_movie_timescale / src_movie_timescale) + 0.5;
        dst_data->media_time       = (src_data->media_time + media_time_shift) * ((double)dst_media_timescale / src_media_timescale) + 0.5;
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

int lsmash_set_media_timestamps( lsmash_root_t *root, uint32_t track_ID, lsmash_media_ts_list_t *ts_list )
{
    if( !ts_list )
        return -1;
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    if( !timeline )
        return -1;
    if( timeline->info_list->entry_count == 0 )
    {
        lsmash_log( LSMASH_LOG_ERROR, "Changing timestamps of LPCM track is not supported.\n" );
        return -1;
    }
    if( ts_list->sample_count != timeline->info_list->entry_count )
        return -1;      /* Number of samples must be same. */
    lsmash_media_ts_t *ts = ts_list->timestamp;
    if( ts[0].dts )
        return -1;      /* DTS must start from value zero. */
    /* Update DTSs. */
    uint32_t sample_count  = ts_list->sample_count;
    uint32_t i;
    if( timeline->info_list->entry_count > 1 )
    {
        i = 1;
        lsmash_entry_t *entry = timeline->info_list->head;
        isom_sample_info_t *info;
        while( i < sample_count )
        {
            info = (isom_sample_info_t *)entry->data;
            if( !info || (ts[i].dts < ts[i - 1].dts) )
                return -1;
            info->duration = ts[i].dts - ts[i - 1].dts;
            entry = entry->next;
            ++i;
        }
        if( i > 1 )
        {
            if( !entry || !entry->data )
                return -1;
            /* Copy the previous duration. */
            ((isom_sample_info_t *)entry->data)->duration = info->duration;
        }
        else
            return -1;      /* Irregular case: sample_count this timeline has is incorrect. */
    }
    else    /* still image */
        ((isom_sample_info_t *)timeline->info_list->head->data)->duration = UINT32_MAX;
    /* Update CTSs.
     * ToDo: hint track must not have any sample_offset. */
    i = 0;
    timeline->ctd_shift = 0;
    for( lsmash_entry_t *entry = timeline->info_list->head; entry; entry = entry->next )
    {
        isom_sample_info_t *info = (isom_sample_info_t *)entry->data;
        if( (ts[i].cts + timeline->ctd_shift) < ts[i].dts )
            timeline->ctd_shift = ts[i].dts - ts[i].cts;
        info->offset = ts[i].cts - ts[i].dts;
        ++i;
    }
    if( timeline->ctd_shift && (!root->qt_compatible || root->max_isom_version < 4) )
        return -1;      /* Don't allow composition to decode timeline shift. */
    return 0;
}

int lsmash_get_media_timestamps( lsmash_root_t *root, uint32_t track_ID, lsmash_media_ts_list_t *ts_list )
{
    if( !ts_list )
        return -1;
    isom_timeline_t *timeline = isom_get_timeline( root, track_ID );
    if( !timeline )
        return -1;
    uint32_t sample_count = timeline->info_list->entry_count;
    if( !sample_count )
    {
        ts_list->sample_count = 0;
        ts_list->timestamp    = NULL;
        return 0;
    }
    lsmash_media_ts_t *ts = malloc( sample_count * sizeof(lsmash_media_ts_t) );
    if( !ts )
        return -1;
    uint64_t dts = 0;
    uint32_t i = 0;
    if( timeline->info_list->entry_count )
        for( lsmash_entry_t *entry = timeline->info_list->head; entry; entry = entry->next )
        {
            isom_sample_info_t *info = (isom_sample_info_t *)entry->data;
            if( !info )
            {
                free( ts );
                return -1;
            }
            ts[i].dts = dts;
            ts[i].cts = timeline->ctd_shift ? (dts + (int32_t)info->offset) : (dts + info->offset);
            dts += info->duration;
            ++i;
        }
    else
        for( lsmash_entry_t *entry = timeline->bunch_list->head; entry; entry = entry->next )
        {
            isom_lpcm_bunch_t *bunch = (isom_lpcm_bunch_t *)entry->data;
            if( !bunch )
            {
                free( ts );
                return -1;
            }
            for( uint32_t j = 0; j < bunch->sample_count; j++ )
            {
                ts[i].dts = dts;
                ts[i].cts = timeline->ctd_shift ? (dts + (int32_t)bunch->offset) : (dts + bunch->offset);
                dts += bunch->duration;
                ++i;
            }
        }
    ts_list->sample_count = sample_count;
    ts_list->timestamp    = ts;
    return 0;
}

void lsmash_delete_media_timestamps( lsmash_media_ts_list_t *ts_list )
{
    if( !ts_list )
        return;
    if( ts_list->timestamp )
    {
        free( ts_list->timestamp );
        ts_list->timestamp = NULL;
    }
    ts_list->sample_count = 0;
}

static int isom_compare_dts( const lsmash_media_ts_t *a, const lsmash_media_ts_t *b )
{
    int64_t diff = (int64_t)(a->dts - b->dts);
    return diff > 0 ? 1 : (diff == 0 ? 0 : -1);
}

void lsmash_sort_timestamps_decoding_order( lsmash_media_ts_list_t *ts_list )
{
    if( !ts_list )
        return;
    qsort( ts_list->timestamp, ts_list->sample_count, sizeof(lsmash_media_ts_t), (int(*)( const void *, const void * ))isom_compare_dts );
}

static int isom_compare_cts( const lsmash_media_ts_t *a, const lsmash_media_ts_t *b )
{
    int64_t diff = (int64_t)(a->cts - b->cts);
    return diff > 0 ? 1 : (diff == 0 ? 0 : -1);
}

void lsmash_sort_timestamps_composition_order( lsmash_media_ts_list_t *ts_list )
{
    if( !ts_list )
        return;
    qsort( ts_list->timestamp, ts_list->sample_count, sizeof(lsmash_media_ts_t), (int(*)( const void *, const void * ))isom_compare_cts );
}

int lsmash_get_max_sample_delay( lsmash_media_ts_list_t *ts_list, uint32_t *max_sample_delay )
{
    if( !ts_list || !max_sample_delay )
        return -1;
    lsmash_media_ts_t *orig_ts = ts_list->timestamp;
    lsmash_media_ts_t *ts = malloc( ts_list->sample_count * sizeof(lsmash_media_ts_t) );
    if( !ts )
        return -1;
    ts_list->timestamp = ts;
    *max_sample_delay = 0;
    for( uint32_t i = 0; i < ts_list->sample_count; i++ )
    {
        ts[i].cts = orig_ts[i].cts;     /* for sorting */
        ts[i].dts = i;
    }
    lsmash_sort_timestamps_composition_order( ts_list );
    for( uint32_t i = 0; i < ts_list->sample_count; i++ )
        if( i < ts[i].dts )
        {
            uint32_t sample_delay = ts[i].dts - i;
            *max_sample_delay = LSMASH_MAX( *max_sample_delay, sample_delay );
        }
    free( ts );
    ts_list->timestamp = orig_ts;
    return 0;
}

#endif /* LSMASH_DEMUXER_ENABLED */
