/*****************************************************************************
 * movie.c
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
#include "file.h"
#include "fragment.h"
#include "write.h"
#include "stbl.h"
#include "track.h"
#include "movie.h"

#include "codecs/mp4a.h"
#include "codecs/mp4sys.h"

int lsm_movie_check_initializer_present( lsmash_root_t *root )
{
    if( LSMASH_IS_NON_EXISTING_BOX( root )
     || LSMASH_IS_NON_EXISTING_BOX( root->file )
     || LSMASH_IS_NON_EXISTING_BOX( root->file->initializer ) )
        return LSMASH_ERR_NAMELESS;
    return 0;
}

int lsm_movie_update_mvhd_duration( isom_moov_t *moov )
{
    assert( LSMASH_IS_EXISTING_BOX( moov ) );
    if( LSMASH_IS_NON_EXISTING_BOX( moov->mvhd->file ) )
        return LSMASH_ERR_INVALID_DATA;
    isom_mvhd_t *mvhd = moov->mvhd;
    mvhd->duration = 0;
    for( lsmash_entry_t *entry = moov->trak_list.head; entry; entry = entry->next )
    {
        /* We pick maximum track duration as movie duration. */
        isom_trak_t *trak = (isom_trak_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( trak )
         || LSMASH_IS_NON_EXISTING_BOX( trak->tkhd ) )
            return LSMASH_ERR_INVALID_DATA;
        mvhd->duration = entry != moov->trak_list.head
                       ? LSMASH_MAX( mvhd->duration, trak->tkhd->duration )
                       : trak->tkhd->duration;
    }
    if( mvhd->duration > UINT32_MAX && !mvhd->file->undefined_64_ver )
        mvhd->version = 1;
    return 0;
}

static uint64_t movie_get_current_mp4time( void )
{
    return (uint64_t)time( NULL ) + ISOM_MAC_EPOCH_OFFSET;
}

static int movie_set_creation_time( lsmash_file_t *file )
{
    if( LSMASH_IS_NON_EXISTING_BOX( file->moov->mvhd ) )
        return LSMASH_ERR_NAMELESS;
    uint64_t current_mp4time = movie_get_current_mp4time();
    for( lsmash_entry_t *entry = file->moov->trak_list.head; entry; entry = entry->next )
    {
        isom_trak_t *trak = (isom_trak_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( trak ) )
            return LSMASH_ERR_INVALID_DATA;
        int err = lsm_track_set_creation_time( trak, current_mp4time );
        if( err < 0 )
            return err;
    }
    isom_mvhd_t *mvhd = file->moov->mvhd;
    if( mvhd->creation_time == 0 )
        mvhd->creation_time = mvhd->modification_time = current_mp4time;
    return 0;
}

isom_moov_t *lsm_movie_create( lsmash_file_t *file )
{
    isom_moov_t *moov = lsm_box_add_moov( file );
    isom_mvhd_t *mvhd = lsm_box_add_mvhd( moov );
    if( LSMASH_IS_NON_EXISTING_BOX( mvhd ) )
    {
        lsm_box_remove_by_itself( moov );
        return lsm_box_default_non_existing_moov();
    }
    /* Default Movie Header Box. */
    mvhd->rate          = 0x00010000;
    mvhd->volume        = 0x0100;
    mvhd->matrix[0]     = 0x00010000;
    mvhd->matrix[4]     = 0x00010000;
    mvhd->matrix[8]     = 0x40000000;
    mvhd->next_track_ID = 1;
    file->initializer = file;
    return moov;
}

/*******************************
    public interfaces
*******************************/

/*---- movie manipulators ----*/

void lsmash_initialize_movie_parameters( lsmash_movie_parameters_t *param )
{
    memset( param, 0, sizeof(lsmash_movie_parameters_t) );
    param->timescale           = 600;
    param->playback_rate       = 0x00010000;
    param->playback_volume     = 0x0100;
}

int lsmash_set_movie_parameters( lsmash_root_t *root, lsmash_movie_parameters_t *param )
{
    if( LSMASH_IS_NON_EXISTING_BOX( root ) )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file;
    if( LSMASH_IS_NON_EXISTING_BOX( file->moov->mvhd ) )
        return LSMASH_ERR_NAMELESS;
    isom_mvhd_t *mvhd = file->moov->mvhd;
    mvhd->timescale = param->timescale;
    if( file->qt_compatible || file->itunes_movie )
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
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file->initializer;
    if( LSMASH_IS_NON_EXISTING_BOX( file->moov->mvhd ) )
        return LSMASH_ERR_NAMELESS;
    isom_mvhd_t *mvhd = file->moov->mvhd;
    param->timescale           = mvhd->timescale;
    param->duration            = mvhd->duration;
    param->playback_rate       = mvhd->rate;
    param->playback_volume     = mvhd->volume;
    param->preview_time        = mvhd->previewTime;
    param->preview_duration    = mvhd->previewDuration;
    param->poster_time         = mvhd->posterTime;
    param->number_of_tracks    = file->moov->trak_list.entry_count;
    return 0;
}

uint32_t lsmash_get_movie_timescale( lsmash_root_t *root )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return 0;
    return root->file->initializer->moov->mvhd->timescale;
}

int lsmash_reserve_media_data_size
(
    lsmash_root_t        *root,
    uint64_t              media_data_size
)
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file->initializer;
    if( LSMASH_IS_EXISTING_BOX( file->mdat )    /* whether the Media Data Box is already written or not */
     || file->fragment )                        /* For fragmented movies, this function makes no sense. */
        return LSMASH_ERR_NAMELESS;
    if( LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_mdat( file ) ) )
        return LSMASH_ERR_NAMELESS;
    file->mdat->reserved_size = media_data_size;
    return 0;
}

static int movie_scan_trak_profileLevelIndication
(
    isom_trak_t                         *trak,
    mp4a_audioProfileLevelIndication    *audio_pli,
    mp4sys_visualProfileLevelIndication *visual_pli
)
{
    isom_stsd_t *stsd = trak->mdia->minf->stbl->stsd;
    if( !stsd->list.head )
        return LSMASH_ERR_INVALID_DATA;
    for( lsmash_entry_t *entry = stsd->list.head; entry; entry = entry->next )
    {
        isom_sample_entry_t *sample_entry = (isom_sample_entry_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( sample_entry ) )
            return LSMASH_ERR_INVALID_DATA;
        lsmash_codec_type_t sample_type = sample_entry->type;
        if( LSMASH_IS_EXISTING_BOX( trak->mdia->minf->vmhd ) )
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
                    *visual_pli = MP4SYS_VISUAL_PLI_H264_AVC;
            }
            else
                *visual_pli = MP4SYS_VISUAL_PLI_NOT_SPECIFIED;
        }
        else if( LSMASH_IS_EXISTING_BOX( trak->mdia->minf->smhd ) )
        {
            if( lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_MP4A_AUDIO ) )
            {
                isom_audio_entry_t *audio = (isom_audio_entry_t *)sample_entry;
                isom_esds_t *esds = (isom_esds_t *)lsm_box_get_extension_box_format( &audio->extensions, ISOM_BOX_TYPE_ESDS );
                if( LSMASH_IS_NON_EXISTING_BOX( esds ) || !esds->ES )
                    return LSMASH_ERR_INVALID_DATA;
                lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
                if( !summary )
                    continue;
                if( lsm_mp4sys_setup_summary_from_DecoderSpecificInfo( summary, esds->ES ) < 0 )
                    *audio_pli = MP4A_AUDIO_PLI_NOT_SPECIFIED;
                else
                    *audio_pli = lsm_mp4a_max_audioProfileLevelIndication( *audio_pli, lsm_mp4a_get_audioProfileLevelIndication( summary ) );
                lsmash_cleanup_summary( (lsmash_summary_t *)summary );
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

int lsm_movie_setup_iods( isom_moov_t *moov )
{
    if( LSMASH_IS_NON_EXISTING_BOX( moov->iods ) && LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_iods( moov ) ) )
        return LSMASH_ERR_NAMELESS;
    isom_iods_t *iods = moov->iods;
    int err = LSMASH_ERR_NAMELESS;
    iods->OD = lsm_mp4sys_create_ObjectDescriptor( 1 ); /* NOTE: Use 1 for ObjectDescriptorID of IOD. */
    if( !iods->OD )
        goto fail;
    mp4a_audioProfileLevelIndication     audio_pli = MP4A_AUDIO_PLI_NONE_REQUIRED;
    mp4sys_visualProfileLevelIndication visual_pli = MP4SYS_VISUAL_PLI_NONE_REQUIRED;
    for( lsmash_entry_t *entry = moov->trak_list.head; entry; entry = entry->next )
    {
        isom_trak_t *trak = (isom_trak_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( trak )
         || LSMASH_IS_NON_EXISTING_BOX( trak->tkhd ) )
            goto fail;
        if( (err = movie_scan_trak_profileLevelIndication( trak, &audio_pli, &visual_pli )) < 0 )
            goto fail;
        if( (err = lsm_mp4sys_create_ES_ID_Inc( iods->OD, trak->tkhd->track_ID )) < 0 )
            goto fail;
    }
    if( (err = lsm_mp4sys_to_InitialObjectDescriptor( iods->OD,
                                                      0, /* FIXME: I'm not quite sure what the spec says. */
                                                      MP4SYS_OD_PLI_NONE_REQUIRED, MP4SYS_SCENE_PLI_NONE_REQUIRED,
                                                      audio_pli, visual_pli,
                                                      MP4SYS_GRAPHICS_PLI_NONE_REQUIRED )) < 0 )
        goto fail;
    return 0;
fail:
    lsm_box_remove_by_itself( iods );
    return err;
}

int lsmash_create_object_descriptor( lsmash_root_t *root )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file;
    /* Return error if this file is not compatible with MP4 file format. */
    if( !file->mp4_version1
     && !file->mp4_version2 )
        return LSMASH_ERR_FUNCTION_PARAM;
    return lsm_movie_setup_iods( file->moov );
}

/*---- finishing functions ----*/

int lsm_movie_check_large_offset_requirement
(
    isom_moov_t *moov,
    uint64_t     meta_size
)
{
    for( lsmash_entry_t *entry = moov->trak_list.head; entry; )
    {
        isom_trak_t *trak = (isom_trak_t *)entry->data;
        isom_stco_t *stco = trak->mdia->minf->stbl->stco;
        isom_stco_entry_t *last_stco = (isom_stco_entry_t *)lsm_list_tail( stco->list );
        if( !last_stco   /* no samples */
         || stco->large_presentation
         || (last_stco->chunk_offset + moov->size + meta_size) <= UINT32_MAX )
        {
            entry = entry->next;
            continue;   /* no need to convert stco into co64 */
        }
        /* stco->co64 conversion */
        int err = lsm_stbl_convert_stco_to_co64( trak->mdia->minf->stbl );
        if( err < 0 )
            return err;
        if( lsm_box_update_size( moov ) == 0 )
            return LSMASH_ERR_INVALID_DATA;
        entry = moov->trak_list.head;   /* whenever any conversion, re-check all traks */
    }
    return 0;
}

void lsm_movie_add_preceding_box_size
(
    isom_moov_t *moov,
    uint64_t     preceding_size
)
{
    for( lsmash_entry_t *entry = moov->trak_list.head; entry; entry = entry->next )
    {
        /* Apply to the chunks in the same file. */
        isom_trak_t *trak = (isom_trak_t *)entry->data;
        isom_stsc_t *stsc = trak->mdia->minf->stbl->stsc;
        isom_stco_t *stco = trak->mdia->minf->stbl->stco;
        uint32_t           stsc_idx  = 0;
        isom_stsc_entry_t *stsc_data = (isom_stsc_entry_t *)lsm_list_at( stsc->list, stsc_idx );
        uint32_t chunk_number = 1;
        uint32_t stco_count   = lsm_list_count( stco->list );
        while( chunk_number <= stco_count )
        {
            if( stsc_data
             && stsc_data->first_chunk == chunk_number )
            {
                lsmash_file_t *ref_file = lsm_track_get_written_media_file( trak, stsc_data->sample_description_index );
                stsc_data = (isom_stsc_entry_t *)lsm_list_at( stsc->list, ++stsc_idx );
                if( ref_file != trak->file )
                {
                    /* The chunks are not contained in the same file. Skip applying the offset.
                     * If no more stsc entries, the rest of the chunks is not contained in the same file. */
                    if( !stsc_data )
                        break;
                    chunk_number = stsc_data->first_chunk;
                    continue;
                }
            }
            void *stco_elem = lsm_list_at( stco->list, chunk_number - 1 );
            if( stco->large_presentation )
                ((isom_co64_entry_t *)stco_elem)->chunk_offset += preceding_size;
            else
                ((isom_stco_entry_t *)stco_elem)->chunk_offset += preceding_size;
            ++chunk_number;
        }
    }
}

int lsm_movie_establish( lsmash_file_t *file )
{
    assert( file == file->initializer );
    int err;
    if( (err = lsm_file_check_mandatory_boxes( file )) < 0
     || (err = movie_set_creation_time( file ))        < 0 )
        return err;
    if( lsm_box_update_size( file->moov ) == 0 )
        return LSMASH_ERR_INVALID_DATA;
    return 0;
}

int lsmash_finish_movie
(
    lsmash_root_t        *root,
    lsmash_adhoc_remux_t *remux
)
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file;
    if( !file->bs
     || LSMASH_IS_NON_EXISTING_BOX( file->initializer->moov ) )
        return LSMASH_ERR_INVALID_DATA;
    if( file->fragment )
        return lsm_fragment_finish_final_movie( file, remux );
    if( file != file->initializer )
        return LSMASH_ERR_INVALID_DATA;
    int err;
    isom_moov_t *moov = file->moov;
    for( lsmash_entry_t *entry = moov->trak_list.head; entry; entry = entry->next )
    {
        isom_trak_t *trak = (isom_trak_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( trak )
         || LSMASH_IS_NON_EXISTING_BOX( trak->tkhd )
         || !trak->cache
         || !trak->mdia->minf->stbl->stsd->list.head
         || !trak->mdia->minf->stbl->stsd->list.head->data
         || !lsm_list_count( trak->mdia->minf->stbl->stco->list ) )
            return LSMASH_ERR_INVALID_DATA;
        if( (err = lsm_track_complement_data_reference( trak->mdia->minf )) < 0 )
            return err;
        uint32_t track_ID         = trak->tkhd->track_ID;
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
            if( (err = lsmash_create_explicit_timeline_map( root, track_ID, edit )) < 0 )
                return err;
        }
        isom_stbl_t *stbl = trak->mdia->minf->stbl;
        /* Compress sample size table. */
        if( stbl->compress_sample_size_table
         && (err = stbl->compress_sample_size_table( stbl )) < 0 )
            return err;
        /* Add stss box if any samples aren't sync sample. */
        if( !trak->cache->all_sync && !stbl->stss && !lsm_box_add_stss( stbl ) )
            return LSMASH_ERR_NAMELESS;
        if( (err = lsm_track_update_tkhd_duration( trak ))         < 0
         || (err = lsm_stbl_update_bitrate_description( trak->mdia )) < 0 )
            return err;
    }
    if( file->mp4_version1 == 1 && (err = lsm_movie_setup_iods( moov )) < 0 )
        return err;
    if( (err = lsm_movie_establish( file )) < 0 )
        return err;
    /* Write the size of Media Data Box here. */
    lsmash_bs_t *bs = file->bs;
    file->mdat->manager &= ~LSMASH_INCOMPLETE_BOX;
    if( (err = lsm_write_box( bs, (isom_box_t *)file->mdat )) < 0 )
        return err;
    /* Write the Movie Box and a Meta Box if no optimization for progressive download. */
    uint64_t meta_size = LSMASH_IS_EXISTING_BOX( file->meta ) ? file->meta->size : 0;
    if( !remux )
    {
        if( (err = lsm_write_box( bs, (isom_box_t *)file->moov )) < 0
         || (err = lsm_write_box( bs, (isom_box_t *)file->meta )) < 0 )
            return err;
        file->size += moov->size + meta_size;
        return 0;
    }
    /* stco->co64 conversion, depending on last chunk's offset */
    if( (err = lsm_movie_check_large_offset_requirement( moov, meta_size )) < 0 )
        return err;
    /* now the amount of offset is fixed. */
    uint64_t mtf_size = moov->size + meta_size;     /* sum of size of boxes moved to front */
    /* buffer size must be at least mtf_size * 2 */
    remux->buffer_size = LSMASH_MAX( remux->buffer_size, mtf_size * 2 );
    /* Split to 2 buffers. */
    uint8_t *buf[2] = { NULL, NULL };
    if( (buf[0] = (uint8_t*)lsmash_malloc( remux->buffer_size )) == NULL )
        return LSMASH_ERR_MEMORY_ALLOC; /* NOTE: I think we still can fallback to "return lsm_movie_write_moov();" here. */
    size_t size = remux->buffer_size / 2;
    buf[1] = buf[0] + size;
    /* Now, the amount of the offset is fixed. apply it to stco/co64 */
    lsm_movie_add_preceding_box_size( moov, mtf_size );
    /* Backup starting area of mdat and write moov + meta there instead. */
    isom_mdat_t *mdat            = file->mdat;
    uint64_t     total           = file->size + mtf_size;
    uint64_t     placeholder_pos = mdat->pos;
    if( (err = lsmash_bs_write_seek( bs, placeholder_pos, SEEK_SET )) < 0 )
        goto fail;
    size_t read_num = size;
    lsmash_bs_read_data( bs, buf[0], &read_num );
    uint64_t read_pos = bs->offset;
    /* Write moov + meta there instead. */
    if( (err = lsmash_bs_write_seek( bs, placeholder_pos, SEEK_SET )) < 0
     || (err = lsm_write_box( bs, (isom_box_t *)file->moov ))         < 0
     || (err = lsm_write_box( bs, (isom_box_t *)file->meta ))         < 0 )
        goto fail;
    uint64_t write_pos = bs->offset;
    /* Update the positions */
    mdat->pos += mtf_size;
    /* Move Media Data Box. */
    if( (err = lsm_file_rearrange_data( file, remux, buf, read_num, size, read_pos, write_pos, total )) < 0 )
        goto fail;
    file->size += mtf_size;
    lsmash_free( buf[0] );
    return 0;
fail:
    lsmash_free( buf[0] );
    return err;
}

int lsmash_update_movie_modification_time( lsmash_root_t *root )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file->initializer;
    if( LSMASH_IS_NON_EXISTING_BOX( file->moov->mvhd ) )
        return LSMASH_ERR_INVALID_DATA;
    isom_mvhd_t *mvhd = file->moov->mvhd;
    mvhd->modification_time = movie_get_current_mp4time();
    /* overwrite strange creation_time */
    if( mvhd->creation_time > mvhd->modification_time )
        mvhd->creation_time = mvhd->modification_time;
    return 0;
}

int lsmash_set_sdp( lsmash_root_t *root, uint32_t track_ID, char *sdptext )
{
    if( lsm_movie_check_initializer_present( root ) < 0 || sdptext == NULL )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file;
    if( LSMASH_IS_NON_EXISTING_BOX( file->moov ) || !file->isom_compatible )
        return LSMASH_ERR_NAMELESS;
    isom_udta_t *udta;
    if( track_ID )
    {
        isom_trak_t *trak = lsm_track_get_trak( file, track_ID );
        if( LSMASH_IS_NON_EXISTING_BOX( trak ) )
            return LSMASH_ERR_NAMELESS;
        if( LSMASH_IS_NON_EXISTING_BOX( trak->udta )
         && LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_udta( trak ) ) )
            return LSMASH_ERR_NAMELESS;
        udta = trak->udta;
    }
    else
    {
        if( LSMASH_IS_NON_EXISTING_BOX( file->moov->udta )
         && LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_udta( file->moov ) ) )
            return LSMASH_ERR_NAMELESS;
        udta = file->moov->udta;
    }
    assert( LSMASH_IS_EXISTING_BOX( udta ) );
    if( LSMASH_IS_NON_EXISTING_BOX( udta->hnti )
     && LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_hnti( udta ) ) )
        return LSMASH_ERR_NAMELESS;
    isom_hnti_t *hnti = udta->hnti;
    /* If track ID is given, text is meant for track hnti box,
     * otherwise it is meant for movie 'hnti' box. */
    if( track_ID )
    {
        if( LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_sdp( hnti ) ) )
            return LSMASH_ERR_NAMELESS;
        isom_sdp_t *sdp = hnti->sdp;
        sdp->sdp_length = strlen( sdptext );    /* leaves \0 out*/
        sdp->sdptext    = lsmash_memdup( sdptext, sdp->sdp_length );
        if( !sdp->sdptext )
            return LSMASH_ERR_MEMORY_ALLOC;
    }
    else
    {
        if( LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_rtp( hnti ) ) )
            return LSMASH_ERR_NAMELESS;
        isom_rtp_t *rtp = hnti->rtp;
        rtp->descriptionformat = LSMASH_4CC( 's', 'd', 'p', ' ' );
        rtp->sdp_length        = strlen( sdptext );     /* leaves \0 out */
        rtp->sdptext           = lsmash_memdup( sdptext, rtp->sdp_length );
        if( !rtp->sdptext )
            return LSMASH_ERR_MEMORY_ALLOC;
    }
    return 0;
}

int lsmash_set_copyright( lsmash_root_t *root, uint32_t track_ID, uint16_t ISO_language, char *notice )
{
    if( lsm_movie_check_initializer_present( root ) < 0
     || (ISO_language && ISO_language < 0x800)
     || !notice )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file;
    if( !file->isom_compatible )
        return LSMASH_ERR_NAMELESS;
    isom_udta_t *udta;
    if( track_ID )
    {
        isom_trak_t *trak = lsm_track_get_trak( file, track_ID );
        if( LSMASH_IS_NON_EXISTING_BOX( trak->udta ) && LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_udta( trak ) ) )
            return LSMASH_ERR_NAMELESS;
        udta = trak->udta;
    }
    else
    {
        if( LSMASH_IS_NON_EXISTING_BOX( file->moov->udta ) && LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_udta( file->moov ) ) )
            return LSMASH_ERR_NAMELESS;
        udta = file->moov->udta;
    }
    assert( LSMASH_IS_EXISTING_BOX( udta ) );
    for( lsmash_entry_t *entry = udta->cprt_list.head; entry; entry = entry->next )
    {
        isom_cprt_t *cprt = (isom_cprt_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( cprt ) || cprt->language == ISO_language )
            return LSMASH_ERR_NAMELESS;
    }
    if( LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_cprt( udta ) ) )
        return LSMASH_ERR_NAMELESS;
    isom_cprt_t *cprt = (isom_cprt_t *)udta->cprt_list.tail->data;
    cprt->language      = ISO_language;
    cprt->notice_length = strlen( notice ) + 1;
    cprt->notice        = lsmash_memdup( notice, cprt->notice_length );
    return 0;
}
