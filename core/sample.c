/*****************************************************************************
 * sample.c
 *****************************************************************************
 * Copyright (C) 2010-2017 L-SMASH project
 * Copyright (c) 2026 Yusuke Nakamura
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

#include "common/internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "box.h"
#include "box_default.h"
#include "fragment.h"
#include "write.h"
#include "movie.h"
#include "track.h"
#include "stbl.h"
#include "sample.h"

#define RTP_SAMPLE_HEADER_SIZE    4
#define RTP_PACKET_SIZE           12 /* a structure in Hint track sample */
#define RTP_HEADER_SIZE           12
#define RTP_CONSTRUCTOR_SIZE      16

lsmash_sample_t *lsmash_create_sample( uint32_t size )
{
    lsmash_sample_t *sample = lsmash_malloc_zero( sizeof(lsmash_sample_t) );
    if( !sample )
        return NULL;
    if( size == 0 )
        return sample;
    sample->data = lsmash_malloc( size );
    if( !sample->data )
    {
        lsmash_free( sample );
        return NULL;
    }
    sample->length = size;
    return sample;
}

int lsmash_sample_alloc( lsmash_sample_t *sample, uint32_t size )
{
    if( !sample )
        return LSMASH_ERR_FUNCTION_PARAM;
    if( size == 0 )
    {
        lsmash_free( sample->data );
        sample->data   = NULL;
        sample->length = 0;
        return 0;
    }
    if( size == sample->length )
        return 0;
    uint8_t *data;
    if( !sample->data )
        data = lsmash_malloc( size );
    else
        data = lsmash_realloc( sample->data, size );
    if( !data )
        return LSMASH_ERR_MEMORY_ALLOC;
    sample->data   = data;
    sample->length = size;
    return 0;
}

void lsmash_delete_sample( lsmash_sample_t *sample )
{
    if( !sample )
        return;
    lsmash_free( sample->data );
    lsmash_free( sample );
}

isom_sample_pool_t *lsm_sample_create_pool( uint64_t size )
{
    isom_sample_pool_t *pool = lsmash_malloc_zero( sizeof(isom_sample_pool_t) );
    if( !pool )
        return NULL;
    if( size == 0 )
        return pool;
    pool->data = lsmash_malloc( size );
    if( !pool->data )
    {
        lsmash_free( pool );
        return NULL;
    }
    pool->alloc = size;
    return pool;
}

void lsm_sample_clear_pool( void *opaque_pool )
{
    if( !opaque_pool )
        return;
    isom_sample_pool_t *pool = (isom_sample_pool_t *)opaque_pool;
    lsmash_freep( &pool->data );
}

void lsm_sample_remove_pool( isom_sample_pool_t *pool )
{
    if( !pool )
        return;
    lsmash_free( pool->data );
    lsmash_free( pool );
}

/* This function decides to put a give sample on the current chunk or the next new one.
 * Returns 1 if pooled samples must be flushed.
 *   FIXME: I wonder if this function should have a extra argument which indicates force_to_flush_cached_chunk.
 *          see lsmash_append_sample for detail. */
static int sample_add_to_chunk
(
    isom_trak_t     *trak,
    lsmash_sample_t *sample
)
{
    if( LSMASH_IS_NON_EXISTING_BOX( trak->file )
     || LSMASH_IS_NON_EXISTING_BOX( trak->mdia->mdhd )
     || LSMASH_IS_NON_EXISTING_BOX( trak->mdia->minf->dinf->dref )
     || LSMASH_IS_NON_EXISTING_BOX( trak->mdia->minf->stbl->stsd )
     || !trak->cache
     ||  trak->mdia->mdhd->timescale == 0
     || !trak->mdia->minf->stbl->stsc->list )
        return LSMASH_ERR_INVALID_DATA;
    isom_chunk_t *current = &trak->cache->chunk;
    if( !current->pool )
    {
        /* Very initial settings, just once per track */
        current->pool = lsm_sample_create_pool( 0 );
        if( !current->pool )
            return LSMASH_ERR_MEMORY_ALLOC;
    }
    if( current->pool->sample_count == 0 )
    {
        /* Cannot decide whether we should flush the current sample or not here yet. */
        current->chunk_number            += 1;
        current->sample_description_index = sample->index;
        current->first_dts                = sample->dts;
        return 0;
    }
    if( sample->dts < current->first_dts )
        return LSMASH_ERR_INVALID_DATA; /* easy error check. */
    lsmash_file_t *media_file = lsm_track_get_written_media_file( trak, current->sample_description_index );
    if( (current->sample_description_index == sample->index)
     && (media_file->max_chunk_duration >= ((double)(sample->dts - current->first_dts) / trak->mdia->mdhd->timescale))
     && (media_file->max_chunk_size     >= current->pool->size + sample->length) )
        return 0;   /* No need to flush current cached chunk, the current sample must be put into that. */
    /* NOTE: chunk relative stuff must be pushed into file after a chunk is fully determined with its contents.
     * Now the current cached chunk is fixed, actually add the chunk relative properties to its file accordingly. */
    int err = lsm_stbl_update_chunk_tables( trak->mdia->minf->stbl, media_file, current );
    if( err < 0 )
        return err;
    /* Update and re-initialize cache, using the current sample */
    current->chunk_number            += 1;
    current->sample_description_index = sample->index;
    current->first_dts                = sample->dts;
    /* current->pool must be flushed in sample_append_internal() */
    return 1;
}

static int sample_write_pooled( lsmash_file_t *file, isom_sample_pool_t *pool )
{
    if( LSMASH_IS_NON_EXISTING_BOX( file )
     || !file->bs
     || !file->bs->stream
     || !(file->flags & LSMASH_FILE_MODE_WRITE)
     || !(file->flags & LSMASH_FILE_MODE_MEDIA)
     || ((file->flags & LSMASH_FILE_MODE_BOX) && LSMASH_IS_NON_EXISTING_BOX( file->mdat )) )
        return LSMASH_ERR_INVALID_DATA;
    lsmash_bs_put_bytes( file->bs, pool->size, pool->data );
    int err = lsmash_bs_flush_buffer( file->bs );
    if( err < 0 )
        return err;
    if( LSMASH_IS_EXISTING_BOX( file->mdat ) )
        file->mdat->media_size += pool->size;
    file->size += pool->size;
    pool->sample_count = 0;
    pool->size         = 0;
    return 0;
}

int lsm_sample_update_tables
(
    isom_trak_t         *trak,
    lsmash_sample_t     *sample,
    uint32_t            *samples_per_packet,
    isom_sample_entry_t *sample_entry
)
{
    int err = lsm_stbl_update_sample_tables( trak, sample, samples_per_packet, sample_entry );
    if( err < 0 )
        return err;
    /* Add a chunk if needed. */
    return sample_add_to_chunk( trak, sample );
}

static int sample_output_cached_chunk( isom_trak_t *trak )
{
    isom_chunk_t      *chunk          = &trak->cache->chunk;
    isom_stbl_t       *stbl           = trak->mdia->minf->stbl;
    isom_stsc_entry_t *last_stsc_data = (isom_stsc_entry_t *)lsm_list_tail( stbl->stsc->list );
    /* Create a new chunk sequence in this track if needed. */
    int err;
    if( (!last_stsc_data
      || chunk->pool->sample_count       != last_stsc_data->samples_per_chunk
      || chunk->sample_description_index != last_stsc_data->sample_description_index)
     && (err = lsm_stbl_add_stsc_entry( stbl, chunk->chunk_number,
                                              chunk->pool->sample_count,
                                              chunk->sample_description_index )) < 0 )
        return err;
    lsmash_file_t *file = lsm_track_get_written_media_file( trak, chunk->sample_description_index );
    if( file->fragment )
    {
        /* Add a new chunk offset in this track. */
        if( (err = lsm_stbl_add_stco_entry( stbl, file->size + ISOM_BASEBOX_COMMON_SIZE + file->fragment->pool_size )) < 0 )
            return err;
        return lsm_fragment_append_track_run( file, chunk );
    }
    /* Add a new chunk offset in this track. */
    if( (err = lsm_stbl_add_stco_entry( stbl, file->size )) < 0 )
        return err;
    /* Output pooled samples in this track. */
    return sample_write_pooled( file, chunk->pool );
}

int lsm_sample_pool_push( isom_sample_pool_t *pool, lsmash_sample_t *sample, uint32_t samples_per_packet )
{
    uint64_t pool_size = pool->size + sample->length;
    if( pool->alloc < pool_size )
    {
        uint8_t *data;
        uint64_t alloc = pool_size + (1<<16);
        if( !pool->data )
            data = lsmash_malloc( alloc );
        else
            data = lsmash_realloc( pool->data, alloc );
        if( !data )
            return LSMASH_ERR_MEMORY_ALLOC;
        pool->data  = data;
        pool->alloc = alloc;
    }
    memcpy( pool->data + pool->size, sample->data, sample->length );
    pool->size          = pool_size;
    pool->sample_count += samples_per_packet;
    lsmash_delete_sample( sample );
    return 0;
}

static int sample_append_internal
(
    isom_trak_t         *trak,
    lsmash_sample_t     *sample,
    isom_sample_entry_t *sample_entry
)
{
    uint32_t samples_per_packet;
    int ret = lsm_sample_update_tables( trak, sample, &samples_per_packet, sample_entry );
    if( ret < 0 )
        return ret;
    /* ret == 1 means pooled samples must be flushed. */
    isom_sample_pool_t *current_pool = trak->cache->chunk.pool;
    if( ret == 1 )
    {
        /* The sample_description_index in the cache is one of the next written chunk.
         * Therefore, it cannot be referenced here. */
        lsm_list_t        *stsc_list      = trak->mdia->minf->stbl->stsc->list;
        isom_stsc_entry_t *last_stsc_data = (isom_stsc_entry_t *)lsm_list_tail( stsc_list );
        lsmash_file_t     *file           = lsm_track_get_written_media_file( trak, last_stsc_data->sample_description_index );
        if( (ret = sample_write_pooled( file, current_pool )) < 0 )
            return ret;
    }
    /* Arbitration system between tracks with extremely scattering dts.
     * Here, we check whether asynchronization between the tracks exceeds the tolerance.
     * If a track has too old "first DTS" in its cached chunk than current sample's DTS, then its pooled samples must be flushed.
     * We don't consider presentation of media since any edit can pick an arbitrary portion of media in track.
     * Note: you needn't read this loop until you grasp the basic handling of chunks. */
    lsmash_file_t *file = trak->file;
    double tolerance = file->max_async_tolerance;
    for( lsmash_entry_t *entry = file->moov->trak_list.head; entry; entry = entry->next )
    {
        isom_trak_t *other = (isom_trak_t *)entry->data;
        if( trak == other )
            continue;
        if( LSMASH_IS_NON_EXISTING_BOX( other )
         || LSMASH_IS_NON_EXISTING_BOX( other->mdia->mdhd )
         || !other->cache
         ||  other->mdia->mdhd->timescale == 0
         || !other->mdia->minf->stbl->stsc->list )
            return LSMASH_ERR_INVALID_DATA;
        isom_chunk_t *chunk = &other->cache->chunk;
        if( !chunk->pool || chunk->pool->sample_count == 0 )
            continue;
        double diff = ((double)sample->dts      /  trak->mdia->mdhd->timescale)
                    - ((double)chunk->first_dts / other->mdia->mdhd->timescale);
        if( diff > tolerance && (ret = sample_output_cached_chunk( other )) < 0 )
            return ret;
        /* Note: we don't flush the cached chunk in the current track and the current sample here
         * even if the conditional expression of '-diff > tolerance' meets.
         * That's useless because appending a sample to another track would be a good equivalent.
         * It's even harmful because it causes excess chunk division by calling
         * sample_output_cached_chunk() which always generates a new chunk.
         * Anyway some excess chunk division will be there, but rather less without it.
         * To completely avoid this, we need to observe at least whether the current sample will be placed
         * right next to the previous chunk of the same track or not. */
    }
    /* anyway the current sample must be pooled. */
    return lsm_sample_pool_push( current_pool, sample, samples_per_packet );
}

int lsm_sample_append_by_type
(
    void                *track,
    lsmash_sample_t     *sample,
    isom_sample_entry_t *sample_entry,
    int (*func_append_sample)( void *, lsmash_sample_t *, isom_sample_entry_t * )
)
{
    if( lsm_box_is_lpcm_audio( sample_entry ) )
    {
        uint32_t frame_size = ((isom_audio_entry_t *)sample_entry)->constBytesPerAudioPacket;
        if( sample->length == frame_size )
            return func_append_sample( track, sample, sample_entry );
        else if( sample->length < frame_size || sample->cts == LSMASH_TIMESTAMP_UNDEFINED )
            return LSMASH_ERR_INVALID_DATA;
        /* Append samples splitted into each LPCMFrame. */
        uint64_t dts = sample->dts;
        uint64_t cts = sample->cts;
        for( uint32_t offset = 0; offset < sample->length; offset += frame_size )
        {
            lsmash_sample_t *lpcm_sample = lsmash_create_sample( frame_size );
            if( !lpcm_sample )
                return LSMASH_ERR_MEMORY_ALLOC;
            memcpy( lpcm_sample->data, sample->data + offset, frame_size );
            lpcm_sample->dts   = dts++;
            lpcm_sample->cts   = cts++;
            lpcm_sample->prop  = sample->prop;
            lpcm_sample->index = sample->index;
            int err = func_append_sample( track, lpcm_sample, sample_entry );
            if( err < 0 )
            {
                lsmash_delete_sample( lpcm_sample );
                return err;
            }
        }
        lsmash_delete_sample( sample );
        return 0;
    }
    else if( lsmash_check_codec_type_identical( sample_entry->type, ISOM_CODEC_TYPE_RTP_HINT  )
          || lsmash_check_codec_type_identical( sample_entry->type, ISOM_CODEC_TYPE_RRTP_HINT ) )
    {
        /* calculate PDU statistics for hmhd box.
         * It requires accessing sample data to get the number of packets per sample. */
        isom_trak_t *trak = (isom_trak_t*)track;
        isom_hmhd_t *hmhd = trak->mdia->minf->hmhd;
        uint16_t packetcount = LSMASH_GET_BE16( sample->data );
        uint8_t *data = sample->data + RTP_SAMPLE_HEADER_SIZE + RTP_PACKET_SIZE;
        /* Calculate only packet headers and packet payloads sizes int PDU size. Later use these two to get avgPDUsize. */
        hmhd->combinedPDUsize += sample->length - (packetcount * RTP_CONSTRUCTOR_SIZE) - RTP_SAMPLE_HEADER_SIZE;
        hmhd->PDUcount        += packetcount;
        for( unsigned int i = 0; i < packetcount; i++ )
        {
            /* constructor type */
            if( *data == 2 )
            {
                /* payload size*/
                uint16_t length = *(data + 2);
                /* Check if this packet is larger than any of the previous ones. */
                hmhd->maxPDUsize = hmhd->maxPDUsize > length + RTP_HEADER_SIZE ? hmhd->maxPDUsize : length + RTP_HEADER_SIZE;
                data += RTP_CONSTRUCTOR_SIZE + RTP_PACKET_SIZE;
            } /* TODO: other constructor types */
        }
    } /* TODO: add other hint tracks that have a hmhd box */
    return func_append_sample( track, sample, sample_entry );
}

/* This function is for non-fragmented movie. */
static int sample_append
(
    lsmash_file_t       *file,
    isom_trak_t         *trak,
    lsmash_sample_t     *sample,
    isom_sample_entry_t *sample_entry
)
{
    /* If there is no available Media Data Box to write samples, add and write a new one before any chunk offset is decided. */
    int err;
    int mdat_absent = LSMASH_IS_NON_EXISTING_BOX( file->mdat );
    if( mdat_absent || !(file->mdat->manager & LSMASH_INCOMPLETE_BOX) )
    {
        if( mdat_absent && LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_mdat( file ) ) )
            return LSMASH_ERR_NAMELESS;
        file->mdat->manager |= LSMASH_PLACEHOLDER;
        if( (err = lsm_write_box( file->bs, (isom_box_t *)file->mdat )) < 0 )
            return err;
        file->size += file->mdat->size;
    }
    return lsm_sample_append_by_type( trak, sample, sample_entry, (int (*)( void *, lsmash_sample_t *, isom_sample_entry_t * ))sample_append_internal );
}

static int sample_output_cache( isom_trak_t *trak )
{
    int err;
    isom_cache_t *cache = trak->cache;
    if( cache->chunk.pool
     && cache->chunk.pool->sample_count
     && (err = sample_output_cached_chunk( trak )) < 0 )
        return err;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    for( lsmash_entry_t *entry = stbl->sgpd_list.head; entry; entry = entry->next )
    {
        isom_sgpd_t *sgpd = (isom_sgpd_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( sgpd ) )
            return LSMASH_ERR_INVALID_DATA;
        switch( sgpd->grouping_type )
        {
            case ISOM_GROUP_TYPE_RAP :
            {
                isom_rap_group_t *group = cache->rap;
                if( !group )
                {
                    if( stbl->file->fragment )
                        continue;
                    else
                        return LSMASH_ERR_NAMELESS;
                }
                if( !group->random_access )
                    continue;
                group->random_access->num_leading_samples_known = 1;
                break;
            }
            case ISOM_GROUP_TYPE_ROLL :
            case ISOM_GROUP_TYPE_PROL :
                if( !cache->roll.pool )
                {
                    if( stbl->file->fragment )
                        continue;
                    else
                        return LSMASH_ERR_NAMELESS;
                }
                isom_sbgp_t *sbgp = lsm_stbl_get_roll_recovery_sample_to_group( &stbl->sbgp_list );
                if( LSMASH_IS_NON_EXISTING_BOX( sbgp ) )
                    return LSMASH_ERR_NAMELESS;
                if( (err = lsm_stbl_all_recovery_completed( sbgp, cache->roll.pool )) < 0 )
                    return err;
                break;
            default :
                break;
        }
    }
    return 0;
}

int lsmash_set_last_sample_delta( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_delta )
{
    if( lsm_movie_check_initializer_present( root ) < 0 || track_ID == 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file;
    if( file->fragment
     && file->fragment->movie )
    {
        isom_traf_t *traf = lsm_track_get_traf( file->fragment->movie, track_ID );
        if( LSMASH_IS_NON_EXISTING_BOX( traf )
         || LSMASH_IS_NON_EXISTING_BOX( traf->tfhd )
         || !traf->cache )
            return LSMASH_ERR_NAMELESS;
        return lsm_fragment_set_last_duration( traf, sample_delta );
    }
    if( file != file->initializer )
        return LSMASH_ERR_INVALID_DATA;
    isom_trak_t *trak = lsm_track_get_trak( file, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->mdia->mdhd )
     || LSMASH_IS_NON_EXISTING_BOX( trak->mdia->minf->stbl->stsd )
     || (LSMASH_IS_NON_EXISTING_BOX( trak->mdia->minf->stbl->stsz )
      && LSMASH_IS_NON_EXISTING_BOX( trak->mdia->minf->stbl->stz2 ))
     || !trak->cache
     || !trak->mdia->minf->stbl->stts->list )
        return LSMASH_ERR_NAMELESS;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_stts_t *stts = stbl->stts;
    uint32_t sample_count = lsm_stbl_get_sample_count( trak );
    int err;
    isom_stts_entry_t *last_stts_data = (isom_stts_entry_t *)lsm_list_tail( stts->list );
    if( !last_stts_data )
    {
        if( sample_count == 0 )
            return 0;       /* no samples */
        if( sample_count > 1 )
            return LSMASH_ERR_INVALID_DATA; /* irregular sample_count */
        /* Set the duration of the first sample.
         * This duration is also the duration of the last sample. */
        if( (err = lsm_stbl_add_stts_entry( stbl, sample_delta )) < 0 )
            return err;
        return lsmash_update_track_duration( root, track_ID, 0 );
    }
    uint32_t i = 0;
    isom_stts_entry_t *stts_elem;
    LSM_LIST_FOR_EACH( stts_elem, stts->list )
        i += stts_elem->sample_count;
    if( sample_count < i )
        return LSMASH_ERR_INVALID_DATA;
    int no_last = (sample_count > i);
    /* Consider QuikcTime fixed compression audio. */
    isom_audio_entry_t *audio = (isom_audio_entry_t *)lsmash_list_get_entry_data( &trak->mdia->minf->stbl->stsd->list,
                                                                                  trak->cache->chunk.sample_description_index );
    if( LSMASH_IS_NON_EXISTING_BOX( audio ) )
        return LSMASH_ERR_INVALID_DATA;
    if( (audio->manager & LSMASH_AUDIO_DESCRIPTION)
     && (audio->manager & LSMASH_QTFF_BASE)
     && (audio->version == 1)
     && (audio->compression_ID != QT_AUDIO_COMPRESSION_ID_VARIABLE_COMPRESSION) )
    {
        if( audio->samplesPerPacket == 0 )
            return LSMASH_ERR_INVALID_DATA;
        int exclude_last_sample = no_last ? 0 : 1;
        uint32_t j = audio->samplesPerPacket;
        for( uint32_t idx = lsm_list_count( stts->list ); idx > 0 && j > 1; idx-- )
        {
            isom_stts_entry_t *stts_data = (isom_stts_entry_t *)lsm_list_at( stts->list, idx - 1 );
            if( !stts_data )
                return LSMASH_ERR_INVALID_DATA;
            for( uint32_t k = exclude_last_sample; k < stts_data->sample_count && j > 1; k++ )
            {
                sample_delta -= stts_data->sample_delta;
                --j;
            }
            exclude_last_sample = 0;
        }
    }
    /* Set sample_delta. */
    if( no_last )
    {
        /* The duration of the last sample is not set yet. */
        if( sample_count - i > 1 )
            return LSMASH_ERR_INVALID_DATA;
        /* Add a sample_delta. */
        if( sample_delta == last_stts_data->sample_delta )
            ++ last_stts_data->sample_count;
        else if( (err = lsm_stbl_add_stts_entry( stbl, sample_delta )) < 0 )
            return err;
    }
    /* The duration of the last sample is already set. Replace it with a new one. */
    else if( (err = lsm_stbl_replace_last_sample_delta( stbl, sample_delta )) < 0 )
        return err;
    return lsmash_update_track_duration( root, track_ID, sample_delta );
}

int lsmash_flush_pooled_samples( lsmash_root_t *root, uint32_t track_ID, uint32_t last_sample_delta )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file;
    if( file->fragment
     && file->fragment->movie )
        return lsm_fragment_flush_pooled_samples( file, track_ID, last_sample_delta );
    if( file != file->initializer )
        return LSMASH_ERR_INVALID_DATA;
    isom_trak_t *trak = lsm_track_get_trak( file, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak )
     || !trak->cache
     || !trak->mdia->minf->stbl->stsc->list )
        return LSMASH_ERR_NAMELESS;
    int err = sample_output_cache( trak );
    if( err < 0 )
        return err;
    return lsmash_set_last_sample_delta( root, track_ID, last_sample_delta );
}

int lsmash_append_sample( lsmash_root_t *root, uint32_t track_ID, lsmash_sample_t *sample )
{
    if( lsm_movie_check_initializer_present( root ) < 0
     || track_ID     == 0
     || sample       == NULL
     || sample->data == NULL
     || sample->dts  == LSMASH_TIMESTAMP_UNDEFINED )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file;
    /* We think max_chunk_duration == 0, which means all samples will be cached on memory, should be prevented.
     * This means removal of a feature that we used to have, but anyway very alone chunk does not make sense. */
    if( !file->bs
     || !(file->flags & LSMASH_FILE_MODE_BOX)
     || file->max_chunk_duration  == 0
     || file->max_async_tolerance == 0 )
        return LSMASH_ERR_NAMELESS;
    /* Write File Type Box here if it was not written yet. */
    if( file->flags & LSMASH_FILE_MODE_INITIALIZATION )
    {
        if( LSMASH_IS_EXISTING_BOX( file->ftyp ) && !(file->ftyp->manager & LSMASH_WRITTEN_BOX) )
        {
            int err = lsm_write_box( file->bs, (isom_box_t *)file->ftyp );
            if( err < 0 )
                return err;
            file->size += file->ftyp->size;
        }
    }
    /* Get a sample initializer. */
    isom_trak_t *trak = lsm_track_get_trak( file->initializer, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->file )
     || LSMASH_IS_NON_EXISTING_BOX( trak->tkhd )
     ||  trak->mdia->mdhd->timescale == 0
     || !trak->cache
     || !trak->mdia->minf->stbl->stsc->list )
        return LSMASH_ERR_NAMELESS;
    isom_sample_entry_t *sample_entry = (isom_sample_entry_t *)lsmash_list_get_entry_data( &trak->mdia->minf->stbl->stsd->list, sample->index );
    if( LSMASH_IS_NON_EXISTING_BOX( sample_entry ) )
        return LSMASH_ERR_NAMELESS;
    /* Append a sample. */
    if( (file->flags & LSMASH_FILE_MODE_FRAGMENTED)
     && file->fragment
     && file->fragment->pool )
        return lsm_fragment_append_sample( file, trak, sample, sample_entry );
    if( file != file->initializer )
        return LSMASH_ERR_INVALID_DATA;
    return sample_append( file, trak, sample, sample_entry );
}
