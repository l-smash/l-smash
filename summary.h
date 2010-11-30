/*****************************************************************************
 * summary.h:
 *****************************************************************************
 * Copyright (C) 2010 L-SMASH project
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

#ifndef LSMASH_SUMMARY_H
#define LSMASH_SUMMARY_H

#include "mp4a.h"
#include "mp4sys.h"

/* L-SMASH's original structure, summary of audio/video stream configuration */
/* FIXME: I wonder whether this struct should blong to namespace of "isom" or not. */
/* NOTE: For audio, currently assuming AAC-LC. For video, currently not used. */

#define MP4SYS_BASE_SUMMARY \
    mp4sys_object_type_indication object_type_indication;\
    mp4sys_stream_type stream_type;\
    void* exdata;                /* typically payload of DecoderSpecificInfo (that's called AudioSpecificConfig in mp4a) */\
    uint32_t exdata_length;      /* length of exdata */\
    uint32_t max_au_length;      /* buffer length for 1 access unit, typically max size of 1 audio/video frame */

typedef struct {
    MP4SYS_BASE_SUMMARY
} mp4sys_summary_t;

typedef struct {
    MP4SYS_BASE_SUMMARY
    // mp4a_audioProfileLevelIndication pli ; /* I wonder we should have this or not. */
    mp4a_AudioObjectType aot;    /* Detailed codec type. If not mp4a, just ignored. */
    uint32_t frequency;          /* Even if the stream is HE-AAC v1/SBR, this is base AAC's one. */
    uint32_t channels;           /* Even if the stream is HE-AAC v2/SBR+PS, this is base AAC's one. */
    uint32_t bit_depth;          /* If AAC, AAC stream itself does not mention to accuracy (bit_depth of decoded PCM data), we assume 16bit. */
    uint32_t samples_in_frame;   /* Even if the stream is HE-AAC/aacPlus/SBR(+PS), this is base AAC's one, so 1024. */
    mp4a_aac_sbr_mode sbr_mode;  /* SBR treatment. Currently we always set this as mp4a_AAC_SBR_NOT_SPECIFIED(Implicit signaling).
                                    User can set this for treatment in other way. */
} mp4sys_audio_summary_t;

typedef struct {
    MP4SYS_BASE_SUMMARY
    // mp4sys_visualProfileLevelIndication pli ; /* I wonder we should have this or not. */
    // mp4v_VideoObjectType vot;    /* Detailed codec type. If not mp4v, just ignored. */
    uint32_t width;
    uint32_t height;
    uint32_t display_width;
    uint32_t display_height;
    uint32_t bit_depth;          /* If AAC, AAC stream itself does not mention to accuracy (bit_depth of decoded PCM data), we assume 16bit. */

} mp4sys_video_summary_t;

int mp4sys_summary_add_exdata( mp4sys_audio_summary_t* summary, void* exdata, uint32_t exdata_length );

/* to facilitate to make exdata (typically DecoderSpecificInfo or AudioSpecificConfig). */
int mp4sys_setup_AudioSpecificConfig( mp4sys_audio_summary_t* summary );
int mp4sys_summary_add_exdata( mp4sys_audio_summary_t* summary, void* exdata, uint32_t exdata_length );

/* FIXME: these functions may change in the future.
   I wonder these functions should be for generic (not limited to audio) summary. */
void mp4sys_cleanup_audio_summary( mp4sys_audio_summary_t* summary );

mp4a_audioProfileLevelIndication mp4sys_get_audioProfileLevelIndication( mp4sys_audio_summary_t* summary );

#endif /* #ifndef LSMASH_SUMMARY_H */
