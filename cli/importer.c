/*****************************************************************************
 * importer.c
 *****************************************************************************
 * Copyright (C) 2010-2014 L-SMASH project
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
extern const importer_functions h264_importer;
extern const importer_functions hevc_importer;
extern const importer_functions vc1_importer;

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
    &h264_importer,
    &hevc_importer,
    &vc1_importer,
    NULL,
};

/***************************************************************************
    importer public interfaces
***************************************************************************/

/******** importer public functions ********/

void lsmash_importer_close( importer_t *importer )
{
    if( !importer )
        return;
    if( !importer->is_stdin && importer->stream )
        fclose( importer->stream );
    if( importer->funcs.cleanup )
        importer->funcs.cleanup( importer );
    lsmash_remove_list( importer->summaries, lsmash_cleanup_summary );
    lsmash_free( importer );
}

importer_t *lsmash_importer_open( const char *identifier, const char *format )
{
    if( identifier == NULL )
        return NULL;
    int auto_detect = ( format == NULL || !strcmp( format, "auto" ) );
    importer_t *importer = (importer_t *)lsmash_malloc_zero( sizeof(importer_t) );
    if( !importer )
        return NULL;
    importer->class = &lsmash_importer_class;
    if( !strcmp( identifier, "-" ) )
    {
        /* special treatment for stdin */
        if( auto_detect )
        {
            lsmash_log( importer, LSMASH_LOG_ERROR, "auto importer detection on stdin is not supported.\n" );
            goto fail;
        }
        importer->stream = stdin;
        importer->is_stdin = 1;
    }
    else if( (importer->stream = lsmash_fopen( identifier, "rb" )) == NULL )
    {
        lsmash_log( importer, LSMASH_LOG_ERROR, "failed to open %s.\n", identifier );
        goto fail;
    }
    importer->summaries = lsmash_create_entry_list();
    if( !importer->summaries )
    {
        lsmash_log( importer, LSMASH_LOG_ERROR, "failed to set up the importer.\n" );
        goto fail;
    }
    /* find importer */
    importer->log_level = LSMASH_LOG_QUIET; /* Any error log is confusing for the probe step. */
    const importer_functions *funcs;
    if( auto_detect )
    {
        /* just rely on detector. */
        for( int i = 0; (funcs = importer_func_table[i]) != NULL; i++ )
        {
            importer->class = &funcs->class;
            if( !funcs->detectable )
                continue;
            if( !funcs->probe( importer ) || lsmash_fseek( importer->stream, 0, SEEK_SET ) != 0 )
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
            if( funcs->probe( importer ) < 0 )
                funcs = NULL;
            break;
        }
    }
    importer->log_level = LSMASH_LOG_INFO;
    if( !funcs )
    {
        importer->class = &lsmash_importer_class;
        lsmash_log( importer, LSMASH_LOG_ERROR, "failed to find the matched importer.\n" );
        goto fail;
    }
    importer->funcs = *funcs;
    return importer;
fail:
    lsmash_importer_close( importer );
    return NULL;
}

/* 0 if success, positive if changed, negative if failed */
int lsmash_importer_get_access_unit( importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    if( !importer || !buffered_sample->data || buffered_sample->length == 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    if( !importer->funcs.get_accessunit )
        return LSMASH_ERR_NAMELESS;
    return importer->funcs.get_accessunit( importer, track_number, buffered_sample );
}

/* Return 0 if failed, otherwise succeeded. */
uint32_t lsmash_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    if( !importer || !importer->funcs.get_last_delta )
        return 0;
    return importer->funcs.get_last_delta( importer, track_number );
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
    lsmash_summary_t *src_summary = lsmash_get_entry_data( importer->summaries, track_number );
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
        lsmash_codec_specific_t *dup = isom_duplicate_codec_specific_data( src_specific );
        if( lsmash_add_entry( &summary->opaque->list, dup ) < 0 )
        {
            lsmash_cleanup_summary( (lsmash_summary_t *)summary );
            lsmash_destroy_codec_specific_data( dup );
            return NULL;
        }
    }
    return summary;
}
