/*****************************************************************************
 * summary.c:
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

#include "summary.h"

#include <stdlib.h>
#include <string.h>

/***************************************************************************
    summary and AudioSpecificConfig relative tools
***************************************************************************/

/* create AudioSpecificConfig as memory block from summary, and set it into that summary itself */
int mp4sys_setup_AudioSpecificConfig( mp4sys_audio_summary_t* summary )
{
    if( !summary )
        return -1;
    lsmash_bs_t* bs = lsmash_bs_create( NULL ); /* no file writing */
    if( !bs )
        return -1;
    mp4a_AudioSpecificConfig_t* asc = mp4a_create_AudioSpecificConfig( summary->aot, summary->frequency, summary->channels, summary->sbr_mode );
    if( !asc )
    {
        lsmash_bs_cleanup( bs );
        return -1;
    }

    mp4a_put_AudioSpecificConfig( bs, asc );
    void* new_asc;
    uint32_t new_length;
    new_asc = lsmash_bs_export_data( bs, &new_length );
    mp4a_remove_AudioSpecificConfig( asc );
    lsmash_bs_cleanup( bs );

    if( !new_asc )
        return -1;
    if( summary->exdata )
        free( summary->exdata );
    summary->exdata = new_asc;
    summary->exdata_length = new_length;
    return 0 ;
}

/* Copy exdata into summary from memory block */
int mp4sys_summary_add_exdata( mp4sys_audio_summary_t* summary, void* exdata, uint32_t exdata_length )
{
    if( !summary )
        return -1;
    /* atomic operation */
    void* new_exdata = NULL;
    if( exdata && exdata_length != 0 )
    {
        new_exdata = malloc( exdata_length );
        if( !new_exdata )
            return -1;
        memcpy( new_exdata, exdata, exdata_length );
        summary->exdata_length = exdata_length;
    }
    else
        summary->exdata_length = 0;

    if( summary->exdata )
        free( summary->exdata );
    summary->exdata = new_exdata;
    return 0;
}

void mp4sys_cleanup_audio_summary( mp4sys_audio_summary_t* summary )
{
    if( !summary )
        return;
    if( summary->exdata )
        free( summary->exdata );
    free( summary );
}

mp4a_audioProfileLevelIndication mp4sys_get_audioProfileLevelIndication( mp4sys_audio_summary_t* summary )
{
    if( !summary || summary->stream_type != MP4SYS_STREAM_TYPE_AudioStream )
        return MP4A_AUDIO_PLI_NONE_REQUIRED; /* means error. */
    if( summary->object_type_indication != MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3 )
        return MP4A_AUDIO_PLI_NOT_SPECIFIED; /* This is of audio stream, but not described in ISO/IEC 14496-3. */
    return mp4a_get_audioProfileLevelIndication( summary->aot, summary->frequency, summary->channels, summary->sbr_mode );
}
