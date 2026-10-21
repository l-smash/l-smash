/*****************************************************************************
 * importer.c
 *****************************************************************************
 * Copyright (C) 2010-2017 L-SMASH project
 * Copyright (c) 2026 Yusuke Nakamura
 *
 * Authors: Takashi Hirata <silverfilain@gmail.com>
 * Contributors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
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

#include <string.h>

#define LSMASH_IMPORTER_INTERNAL
#include "importer.h"
#include "core/movie.h"
#include "core/track.h"

/***************************************************************************
    importer classes
***************************************************************************/
static const lsmash_class_t lsmash_importer_class =
{
    "importer",
    offsetof( importer_t, log_level )
};

extern const importer_functions mp4sys_adts_importer;
extern const importer_functions mp4sys_mp3_importer;
extern const importer_functions amr_importer;
extern const importer_functions ac3_importer;
extern const importer_functions eac3_importer;
extern const importer_functions mp4a_als_importer;
extern const importer_functions dts_importer;
extern const importer_functions flac_importer;
extern const importer_functions wave_importer;
extern const importer_functions ivf_importer;
extern const importer_functions h264_importer;
extern const importer_functions hevc_importer;
extern const importer_functions vc1_importer;
extern const importer_functions isobm_importer;

/******** importer listing table ********/
static const importer_functions *importer_func_table[] =
{
    &mp4sys_adts_importer,
    &mp4sys_mp3_importer,
    &amr_importer,
    &ac3_importer,
    &eac3_importer,
    &mp4a_als_importer,
    &dts_importer,
    &flac_importer,
    &wave_importer,
    &ivf_importer,
    &h264_importer,
    &hevc_importer,
    &vc1_importer,
    &isobm_importer,
    NULL,
};

/***************************************************************************
    importer public interfaces
***************************************************************************/

/******** importer public functions ********/
importer_t *lsm_importer_alloc( lsmash_root_t *root )
{
    if( LSMASH_IS_NON_EXISTING_BOX( root ) )
        return NULL;
    importer_t *importer = (importer_t *)lsmash_malloc_zero( sizeof(importer_t) );
    if( !importer )
        return NULL;
    importer->root = root;
    importer->summaries = lsmash_list_create( lsmash_cleanup_summary );
    if( !importer->summaries )
    {
        lsmash_free( importer );
        return NULL;
    }
    return importer;
}

void lsm_importer_destroy( importer_t *importer )
{
    if( !importer )
        return;
    lsmash_file_t *file = importer->file;
    if( importer->funcs.cleanup )
        importer->funcs.cleanup( importer );
    lsmash_list_destroy( importer->summaries );
    lsmash_free( importer );
    /* Prevent freeing this already freed importer in file's destructor again. */
    if( file && file->importer )
        file->importer = NULL;
}

int lsm_importer_set_file( importer_t *importer, lsmash_file_t *file )
{
    if( !importer
     || lsmash_activate_file( importer->root, file ) < 0
     || !file->bs )
        return LSMASH_ERR_NAMELESS;
    importer->file = file;
    importer->bs   = file->bs;
    file->importer = importer;
    return 0;
}

void lsmash_importer_close( importer_t *importer )
{
    if( !importer )
        return;
    lsmash_close_file( &importer->file_param );
    lsm_importer_destroy( importer );
}

/* Check the leading 8 bytes for the Box / Atom header of a top-level box that a real ISOBMFF/QTFF file begins with.
 * The 4CC at offset 4 is a magic number strong enough to tell a container from a raw elementary stream.
 * This is only a hint to choose the probing order, so a false positive costs one failed probe at most and
 * a false negative (e.g. a file starting with an unlisted box) still reaches isobm_importer at the table tail. */
static int importer_has_isobmff_signature( importer_t *importer )
{
    lsmash_bs_t *bs = importer->bs;
    uint32_t type = ((uint32_t)lsmash_bs_show_byte( bs, 4 ) << 24)
                  | ((uint32_t)lsmash_bs_show_byte( bs, 5 ) << 16)
                  | ((uint32_t)lsmash_bs_show_byte( bs, 6 ) <<  8)
                  |  (uint32_t)lsmash_bs_show_byte( bs, 7 );
    switch( type )
    {
        case LSMASH_4CC( 'f', 't', 'y', 'p' ) :
        case LSMASH_4CC( 's', 't', 'y', 'p' ) :
        case LSMASH_4CC( 'm', 'o', 'o', 'v' ) :
        case LSMASH_4CC( 'm', 'o', 'o', 'f' ) :
        case LSMASH_4CC( 's', 'i', 'd', 'x' ) :
        case LSMASH_4CC( 'm', 'd', 'a', 't' ) :
        case LSMASH_4CC( 'w', 'i', 'd', 'e' ) :
        case LSMASH_4CC( 'f', 'r', 'e', 'e' ) :
        case LSMASH_4CC( 's', 'k', 'i', 'p' ) :
        case LSMASH_4CC( 'p', 'n', 'o', 't' ) :
            return 1;
        default :
            return 0;
    }
}

int lsm_importer_find( importer_t *importer, const char *format, int auto_detect )
{
    importer->log_level = LSMASH_LOG_QUIET; /* Any error log is confusing for the probe step. */
    const importer_functions *funcs;
    int err = LSMASH_ERR_NAMELESS;
    if( auto_detect )
    {
        size_t old_bs_max_size = importer->bs->buffer.max_size;
        /* A container is tried first when its signature is found so that opening a regular MP4/MOV file does not run
         * every raw stream probe beforehand, which also removes the risk of misdetecting the container as a raw stream. */
        const importer_functions *first = NULL;
        if( importer_has_isobmff_signature( importer ) )
        {
            first = &isobm_importer;
            importer->class = &first->class;
            if( (err = first->probe( importer )) == 0 )
                funcs = first;
            else if( lsmash_bs_read_seek( importer->bs, 0, SEEK_SET ) != 0 )
                funcs = first;
            else
            {
                funcs = NULL;
                err   = LSMASH_ERR_NAMELESS;
            }
        }
        else
            funcs = NULL;
        /* just rely on detector. */
        if( !funcs )
            for( int i = 0; (funcs = importer_func_table[i]) != NULL; i++ )
            {
                importer->bs->buffer.max_size = old_bs_max_size;
                importer->class = &funcs->class;
                if( funcs == first || !funcs->detectable )
                    continue;
                if( (err = funcs->probe( importer )) == 0
                 || lsmash_bs_read_seek( importer->bs, 0, SEEK_SET ) != 0 )
                    break;
            }
    }
    else
    {
        /* needs name matching. */
        for( int i = 0; (funcs = importer_func_table[i]) != NULL; i++ )
        {
            importer->class = &funcs->class;
            if( strcmp( importer->class->name, format ) )
                continue;
            if( (err = funcs->probe( importer )) < 0 )
                funcs = NULL;
            break;
        }
    }
    importer->log_level = LSMASH_LOG_INFO;
    if( !funcs )
    {
        importer->class = &lsmash_importer_class;
        lsmash_log( importer, LSMASH_LOG_ERROR, "failed to find the matched importer.\n" );
    }
    else
        importer->funcs = *funcs;
    return err;
}

importer_t *lsmash_importer_open( lsmash_root_t *root, const char *identifier, const char *format )
{
    if( identifier == NULL )
        return NULL;
    int auto_detect = (format == NULL || !strcmp( format, "auto" ));
    importer_t *importer = lsm_importer_alloc( root );
    if( !importer )
        return NULL;
    importer->is_adhoc_open = 1;
    /* Open an input 'stream'. */
    if( !strcmp( identifier, "-" ) )
    {
        /* special treatment for stdin */
        if( auto_detect )
        {
            lsmash_log( importer, LSMASH_LOG_ERROR, "auto importer detection on stdin is not supported.\n" );
            goto fail;
        }
    }
    if( lsmash_open_file( identifier, 1, &importer->file_param ) < 0 )
    {
        lsmash_log( importer, LSMASH_LOG_ERROR, "failed to open %s.\n", identifier );
        goto fail;
    }
    lsmash_file_t *file = lsmash_set_file( root, &importer->file_param );
    if( LSMASH_IS_NON_EXISTING_BOX( file ) )
    {
        lsmash_log( importer, LSMASH_LOG_ERROR, "failed to set opened file.\n" );
        goto fail;
    }
    if( lsm_importer_set_file( importer, file ) < 0 )
    {
        lsmash_log( importer, LSMASH_LOG_ERROR, "failed to activate opened file.\n" );
        goto fail;
    }
    if( lsm_importer_find( importer, format, auto_detect ) < 0 )
        goto fail;
    return importer;
fail:
    lsmash_importer_close( importer );
    return NULL;
}

/* 0 if success, positive if changed, negative if failed */
int lsmash_importer_get_access_unit( importer_t *importer, uint32_t track_number, lsmash_sample_t **p_sample )
{
    if( !importer || !p_sample )
        return LSMASH_ERR_FUNCTION_PARAM;
    if( !importer->funcs.get_accessunit )
        return LSMASH_ERR_NAMELESS;
    *p_sample = NULL;
    return importer->funcs.get_accessunit( importer, track_number, p_sample );
}

/* Return 0 if failed, otherwise succeeded. */
uint32_t lsmash_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    if( !importer || !importer->funcs.get_last_delta )
        return 0;
    return importer->funcs.get_last_delta( importer, track_number );
}

int lsmash_importer_construct_timeline( importer_t *importer, uint32_t track_number )
{
    if( !importer )
        return LSMASH_ERR_FUNCTION_PARAM;
    if( !importer->funcs.construct_timeline )
        return LSMASH_ERR_PATCH_WELCOME;
    return importer->funcs.construct_timeline( importer, track_number );
}

uint32_t lsmash_importer_get_track_count( importer_t *importer )
{
    if( !importer || !importer->summaries )
        return 0;
    return importer->summaries->entry_count;
}

lsmash_summary_t *lsmash_duplicate_summary( importer_t *importer, uint32_t track_number )
{
    if( !importer )
        return NULL;
    lsmash_summary_t *src_summary = lsmash_list_get_entry_data( importer->summaries, track_number );
    if( !src_summary )
        return NULL;
    lsmash_summary_t *summary = lsmash_create_summary( src_summary->summary_type );
    if( !summary )
        return NULL;
    lsmash_codec_specific_list_t *opaque = summary->opaque;
    switch( src_summary->summary_type )
    {
        case LSMASH_SUMMARY_TYPE_VIDEO :
            *(lsmash_video_summary_t *)summary = *(lsmash_video_summary_t *)src_summary;
            break;
        case LSMASH_SUMMARY_TYPE_AUDIO :
            *(lsmash_audio_summary_t *)summary = *(lsmash_audio_summary_t *)src_summary;
            break;
        case LSMASH_SUMMARY_TYPE_HINT:
            *(lsmash_hint_summary_t *)summary  = *(lsmash_hint_summary_t *)src_summary;
            break;
        default :
            lsmash_cleanup_summary( summary );
            return NULL;
    }
    summary->opaque = opaque;
    for( lsmash_entry_t *entry = src_summary->opaque->list.head; entry; entry = entry->next )
    {
        lsmash_codec_specific_t *src_specific = (lsmash_codec_specific_t *)entry->data;
        if( !src_specific )
            continue;
        lsmash_codec_specific_t *dup = lsm_description_duplicate_codec_specific_data( src_specific );
        if( lsmash_list_add_entry( &summary->opaque->list, dup ) < 0 )
        {
            lsmash_cleanup_summary( (lsmash_summary_t *)summary );
            lsmash_destroy_codec_specific_data( dup );
            return NULL;
        }
    }
    return summary;
}

int lsm_importer_make_fake_movie( importer_t *importer )
{
    if( !importer || (importer->file->flags & LSMASH_FILE_MODE_BOX) )
        return LSMASH_ERR_FUNCTION_PARAM;
    if( LSMASH_IS_BOX_ADDITION_FAILURE( lsm_movie_create( importer->file ) ) )
        return LSMASH_ERR_NAMELESS;
    return 0;
}

int lsm_importer_make_fake_track( importer_t *importer, lsmash_media_type media_type, uint32_t *track_ID )
{
    if( !importer || (importer->file->flags & LSMASH_FILE_MODE_BOX) || track_ID == 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    isom_trak_t *trak = lsm_track_create( importer->file, media_type );
    int err;
    if( LSMASH_IS_NON_EXISTING_BOX( trak->tkhd )
     || LSMASH_IS_NON_EXISTING_BOX( trak->mdia->minf )
     || trak->tkhd->track_ID == 0 )
    {
        err = LSMASH_ERR_NAMELESS;
        goto fail;
    }
    if( (err = lsm_track_complement_data_reference( trak->mdia->minf )) < 0 )
        goto fail;
    *track_ID = trak->tkhd->track_ID;
    return 0;
fail:
    lsm_box_remove_by_itself( trak );
    return err;
}

void lsm_importer_break_fake_movie( importer_t *importer )
{
    if( !importer )
        return;
    lsm_box_remove_by_itself( importer->file->moov );
}

/* Build a fake moov / trak / stsd for a raw stream so that it can be treated as if it were an ISOBMFF/QTFF file.
 * The movie and the media share the same timescale so that the timeline durations equal the media durations.
 * The summary is only read here: it stays owned by the caller, who registers it in importer->summaries.
 * On failure, everything built here is rolled back, therefore the caller must not break the fake movie again
 * on behalf of this function, though doing it is harmless. */
int lsm_importer_setup_fake_track
(
    importer_t       *importer,
    lsmash_media_type media_type,
    uint32_t          timescale,
    lsmash_summary_t *summary,
    uint32_t         *track_ID
)
{
    if( !importer || !summary || !track_ID || timescale == 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    int err;
    lsmash_movie_parameters_t movie_param = { 0 };
    lsmash_track_parameters_t track_param = { 0 };
    lsmash_media_parameters_t media_param = { 0 };
    if( (err = lsm_importer_make_fake_movie( importer )) < 0 )
        return err;
    if( (err = lsm_importer_make_fake_track( importer, media_type, track_ID )) < 0
     || (err = lsmash_get_movie_parameters( importer->root, &movie_param )) < 0
     || (err = lsmash_get_track_parameters( importer->root, *track_ID, &track_param )) < 0
     || (err = lsmash_get_media_parameters( importer->root, *track_ID, &media_param )) < 0 )
        goto fail;
    movie_param.timescale = timescale;
    media_param.timescale = timescale;
    if( summary->summary_type == LSMASH_SUMMARY_TYPE_VIDEO )
    {
        /* The display size is a 16.16 fixed-point number. */
        track_param.display_width  = ((lsmash_video_summary_t *)summary)->width  << 16;
        track_param.display_height = ((lsmash_video_summary_t *)summary)->height << 16;
    }
    if( (err = lsmash_set_movie_parameters( importer->root, &movie_param )) < 0
     || (err = lsmash_set_track_parameters( importer->root, *track_ID, &track_param )) < 0
     || (err = lsmash_set_media_parameters( importer->root, *track_ID, &media_param )) < 0 )
        goto fail;
    if( lsmash_add_sample_entry( importer->root, *track_ID, summary ) != 1 )
    {
        err = LSMASH_ERR_NAMELESS;
        goto fail;
    }
    return 0;
fail:
    lsm_importer_break_fake_movie( importer );
    return err;
}
