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

#include "lsmash.h"
#include "mp4a.h"
#include "mp4sys.h"

/* to facilitate to make exdata (typically DecoderSpecificInfo or AudioSpecificConfig). */
int mp4sys_setup_AudioSpecificConfig( lsmash_audio_summary_t* summary );
int mp4sys_summary_add_exdata( lsmash_audio_summary_t* summary, void* exdata, uint32_t exdata_length );

/* FIXME: these functions may change in the future.
   I wonder these functions should be for generic (not limited to audio) summary. */
void mp4sys_cleanup_audio_summary( lsmash_audio_summary_t* summary );

mp4a_audioProfileLevelIndication mp4sys_get_audioProfileLevelIndication( lsmash_audio_summary_t* summary );

#endif /* #ifndef LSMASH_SUMMARY_H */
