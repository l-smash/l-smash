/*****************************************************************************
 * summary.c:
 *****************************************************************************
 * Copyright (C) 2010-2012 L-SMASH project
 *
 * Authors: Takashi Hirata <silverfilain@gmail.com>
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

#include "importer.h"
#include "mp4a.h"
#include "mp4sys.h"
#include "box.h"
#include "description.h"

/***************************************************************************
    summary and AudioSpecificConfig relative tools
***************************************************************************/

/* create AudioSpecificConfig as memory block from summary, and set it into that summary itself */
int lsmash_setup_AudioSpecificConfig( lsmash_audio_summary_t *summary )
{
    if( !summary )
        return -1;
    lsmash_bs_t* bs = lsmash_bs_create( NULL ); /* no file writing */
    if( !bs )
        return -1;
    mp4a_AudioSpecificConfig_t *asc =
        mp4a_create_AudioSpecificConfig( summary->aot,
                                         summary->frequency,
                                         summary->channels,
                                         summary->sbr_mode,
                                         NULL,/*FIXME*/
                                         0 /*FIXME*/);
    if( !asc )
    {
        lsmash_bs_cleanup( bs );
        return -1;
    }
    mp4a_put_AudioSpecificConfig( bs, asc );
    void *new_asc;
    uint32_t new_length;
    new_asc = lsmash_bs_export_data( bs, &new_length );
    mp4a_remove_AudioSpecificConfig( asc );
    lsmash_bs_cleanup( bs );
    if( !new_asc )
        return -1;
    lsmash_codec_specific_t *specific = lsmash_malloc_zero( sizeof(lsmash_codec_specific_t) );
    if( !specific )
    {
        free( new_asc );
        return -1;
    }
    specific->type              = LSMASH_CODEC_SPECIFIC_DATA_TYPE_UNKNOWN;
    specific->format            = LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED;
    specific->destruct          = (lsmash_codec_specific_destructor_t)free;
    specific->size              = new_length;
    specific->data.unstructured = lsmash_memdup( new_asc, new_length );
    if( !specific->data.unstructured
     || lsmash_add_entry( &summary->opaque->list, specific ) )
    {
        free( new_asc );
        lsmash_destroy_codec_specific_data( specific );
        return -1;
    }
    return 0;
}

lsmash_summary_t *lsmash_create_summary( lsmash_summary_type summary_type )
{
    size_t summary_size;
    switch( summary_type )
    {
        case LSMASH_SUMMARY_TYPE_VIDEO :
            summary_size = sizeof(lsmash_video_summary_t);
            break;
        case LSMASH_SUMMARY_TYPE_AUDIO :
            summary_size = sizeof(lsmash_audio_summary_t);
            break;
        default :
            summary_size = sizeof(lsmash_summary_t);
            return NULL;
    }
    lsmash_summary_t *summary = (lsmash_summary_t *)lsmash_malloc_zero( summary_size );
    if( !summary )
        return NULL;
    summary->opaque = (lsmash_codec_specific_list_t *)lsmash_malloc_zero( sizeof(lsmash_codec_specific_list_t) );
    if( !summary->opaque )
    {
        free( summary );
        return NULL;
    }
    summary->summary_type = summary_type;
    return summary;
}

void lsmash_cleanup_summary( lsmash_summary_t *summary )
{
    if( !summary )
        return;
    if( summary->opaque )
    {
        for( lsmash_entry_t *entry = summary->opaque->list.head; entry; )
        {
            lsmash_entry_t *next = entry->next;
            lsmash_destroy_codec_specific_data( (lsmash_codec_specific_t *)entry->data );
            free( entry );
            entry = next;
        }
        free( summary->opaque );
    }
    free( summary );
}

int lsmash_add_codec_specific_data( lsmash_summary_t *summary, lsmash_codec_specific_t *specific )
{
    if( !summary || !summary->opaque || !specific )
        return -1;
    lsmash_codec_specific_t *dup = isom_duplicate_codec_specific_data( specific );
    if( !dup )
        return -1;
    if( lsmash_add_entry( &summary->opaque->list, dup ) )
    {
        lsmash_destroy_codec_specific_data( dup );
        return -1;
    }
    return 0;
}

uint32_t lsmash_count_summary( lsmash_root_t *root, uint32_t track_ID )
{
    if( !root || track_ID == 0 )
        return 0;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->hdlr
     || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd
     || !trak->mdia->minf->stbl->stsd->list )
        return 0;
    return trak->mdia->minf->stbl->stsd->list->entry_count;
}

lsmash_summary_t *lsmash_get_summary( lsmash_root_t *root, uint32_t track_ID, uint32_t description_number )
{
    if( !root || track_ID == 0 || description_number == 0 )
        return NULL;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->hdlr
     || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd
     || !trak->mdia->minf->stbl->stsd->list )
        return NULL;
    isom_stsd_t *stsd = trak->mdia->minf->stbl->stsd;
    uint32_t j = 1;
    for( lsmash_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        if( j++ != description_number )
            continue;
        isom_sample_entry_t *sample_entry = entry->data;
        if( !sample_entry )
            return NULL;
        lsmash_codec_type_t sample_type = sample_entry->type;
        static struct create_summary_table_tag
        {
            lsmash_codec_type_t type;
            lsmash_summary_t *(*func)( isom_sample_entry_t * );
        } create_summary_table[128] = { { LSMASH_CODEC_TYPE_INITIALIZER, NULL } };
        if( !create_summary_table[0].func )
        {
            /* Initialize the table. */
            int i = 0;
#define ADD_CREATE_SUMMARY_TABLE_ELEMENT( type, func ) \
    create_summary_table[i++] = (struct create_summary_table_tag){ type, func }
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVC1_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( ISOM_CODEC_TYPE_VC_1_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_APCH_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_APCN_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_APCS_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_APCO_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_AP4H_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_DV10_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_DVOO_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_DVC_VIDEO,  isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_DVCP_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_DVPP_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_DV5N_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_DV5P_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_DVH2_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_DVH3_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_DVH5_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_DVH6_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_DVHP_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_DVHQ_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_FLIC_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_H261_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_H263_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_JPEG_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_MJPA_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_MJPB_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_PNG_VIDEO,  isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_RAW_VIDEO,  isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_RLE_VIDEO,  isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_RPZA_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_TGA_VIDEO,  isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_TIFF_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_ULRA_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_ULRG_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_ULY2_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_ULY0_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_V210_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_V216_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_V308_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_V408_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_V410_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_YUV2_VIDEO, isom_create_video_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( ISOM_CODEC_TYPE_MP4A_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( ISOM_CODEC_TYPE_AC_3_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( ISOM_CODEC_TYPE_ALAC_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( ISOM_CODEC_TYPE_EC_3_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( ISOM_CODEC_TYPE_SAMR_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( ISOM_CODEC_TYPE_SAWB_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSC_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSE_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSH_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSL_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_MP4A_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_23NI_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_NONE_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_LPCM_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_SOWT_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_TWOS_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_FL32_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_FL64_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_IN24_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_IN32_AUDIO, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( QT_CODEC_TYPE_NOT_SPECIFIED, isom_create_audio_summary_from_description );
            ADD_CREATE_SUMMARY_TABLE_ELEMENT( LSMASH_CODEC_TYPE_UNSPECIFIED, NULL );
        }
        for( int i = 0; create_summary_table[i].func; i++ )
            if( lsmash_check_codec_type_identical( sample_type, create_summary_table[i].type ) )
                return create_summary_table[i].func( sample_entry );
        return NULL;
    }
    return NULL;
}

int lsmash_compare_summary( lsmash_summary_t *a, lsmash_summary_t *b )
{
    if( !a || !b )
        return -1;
    if( a->summary_type != b->summary_type
     || !lsmash_check_box_type_identical( a->sample_type, b->sample_type ) )
        return 1;
    if( a->summary_type == LSMASH_SUMMARY_TYPE_VIDEO )
    {
        lsmash_video_summary_t *in_video  = (lsmash_video_summary_t *)a;
        lsmash_video_summary_t *out_video = (lsmash_video_summary_t *)b;
        if( in_video->width  != out_video->width
         || in_video->height != out_video->height
         || in_video->depth  != out_video->depth
         || in_video->par_h  != out_video->par_h
         || in_video->par_v  != out_video->par_v
         || memcmp( in_video->compressorname, out_video->compressorname, strlen( in_video->compressorname ) )
         || in_video->clap.width.n             != out_video->clap.width.n
         || in_video->clap.width.d             != out_video->clap.width.d
         || in_video->clap.height.n            != out_video->clap.height.n
         || in_video->clap.height.d            != out_video->clap.height.d
         || in_video->clap.horizontal_offset.n != out_video->clap.horizontal_offset.n
         || in_video->clap.horizontal_offset.d != out_video->clap.horizontal_offset.d
         || in_video->clap.vertical_offset.n   != out_video->clap.vertical_offset.n
         || in_video->clap.vertical_offset.d   != out_video->clap.vertical_offset.d
         || in_video->color.primaries_index != out_video->color.primaries_index
         || in_video->color.transfer_index  != out_video->color.transfer_index
         || in_video->color.matrix_index    != out_video->color.matrix_index
         || in_video->color.full_range      != out_video->color.full_range )
            return 1;
    }
    else if( a->summary_type == LSMASH_SUMMARY_TYPE_AUDIO )
    {
        lsmash_audio_summary_t *in_audio  = (lsmash_audio_summary_t *)a;
        lsmash_audio_summary_t *out_audio = (lsmash_audio_summary_t *)b;
        if( in_audio->frequency        != out_audio->frequency
         || in_audio->channels         != out_audio->channels
         || in_audio->sample_size      != out_audio->sample_size
         || in_audio->samples_in_frame != out_audio->samples_in_frame )
            return 1;
    }
    return isom_compare_opaque_extensions( a, b );
}
