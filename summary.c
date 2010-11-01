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
    isom_bs_t* bs = isom_bs_create( NULL ); /* no file writing */
    if( !bs )
        return -1;
    mp4a_AudioSpecificConfig_t* asc = mp4a_create_AudioSpecificConfig( summary->aot, summary->frequency, summary->channels, summary->sbr_mode );
    if( !asc )
    {
        isom_bs_cleanup( bs );
        return -1;
    }

    mp4a_put_AudioSpecificConfig( bs, asc );
    void* new_asc;
    uint32_t new_length;
    new_asc = isom_bs_export_data( bs, &new_length );
    mp4a_remove_AudioSpecificConfig( asc );
    isom_bs_cleanup( bs );

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

/* NOTE: This function is not strictly preferable, but accurate.
   The spec of audioProfileLevelIndication is too much complicated. */
mp4sys_audioProfileLevelIndication mp4sys_get_audioProfileLevelIndication( mp4sys_audio_summary_t* summary )
{
    if( !summary || summary->stream_type != MP4SYS_STREAM_TYPE_AudioStream
        || summary->channels == 0 || summary->frequency == 0 )
        return MP4SYS_AUDIO_PLI_NONE_REQUIRED; /* means error. */
    if( summary->object_type_indication != MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3 )
        return MP4SYS_AUDIO_PLI_NOT_SPECIFIED; /* This is of audio stream, but not described in ISO/IEC 14496-3. */

    mp4sys_audioProfileLevelIndication pli = MP4SYS_AUDIO_PLI_NOT_SPECIFIED;
    switch( summary->aot )
    {
    case MP4A_AUDIO_OBJECT_TYPE_AAC_LC:
        if( summary->sbr_mode == MP4A_AAC_SBR_HIERARCHICAL )
        {
            /* NOTE: This is not strictly preferable, but accurate; just possibly over-estimated.
               We do not expect to use MP4A_AAC_SBR_HIERARCHICAL mode without SBR, nor downsampled mode with SBR. */
            if( summary->channels <= 2 && summary->frequency <= 24 )
                pli = MP4SYS_AUDIO_PLI_HE_AAC_L2;
            else if( summary->channels <= 5 && summary->frequency <= 48 )
                pli = MP4SYS_AUDIO_PLI_HE_AAC_L5;
            else
                pli = MP4SYS_AUDIO_PLI_NOT_SPECIFIED;
            break;
        }
        /* pretending plain AAC-LC, if actually HE-AAC. */
        static const uint32_t mp4sys_aac_pli_table[5][3] = {
            /* channels, frequency,    audioProfileLevelIndication */
            {         6,     96000,        MP4SYS_AUDIO_PLI_AAC_L5 }, /* FIXME: 6ch is not strictly correct, but works in many case. */
            {         6,     48000,        MP4SYS_AUDIO_PLI_AAC_L4 }, /* FIXME: 6ch is not strictly correct, but works in many case. */
            {         2,     48000,        MP4SYS_AUDIO_PLI_AAC_L2 },
            {         2,     24000,        MP4SYS_AUDIO_PLI_AAC_L1 },
            {         0,         0, MP4SYS_AUDIO_PLI_NOT_SPECIFIED }
        };
        for( int i = 0; summary->channels <= mp4sys_aac_pli_table[i][0] && summary->frequency <= mp4sys_aac_pli_table[i][1] ; i++ )
            pli = mp4sys_aac_pli_table[i][2];
        break;
    case MP4A_AUDIO_OBJECT_TYPE_Layer_1:
    case MP4A_AUDIO_OBJECT_TYPE_Layer_2:
    case MP4A_AUDIO_OBJECT_TYPE_Layer_3:
        pli = MP4SYS_AUDIO_PLI_NOT_SPECIFIED; /* 14496-3, 1.5.2 Audio profiles and levels, does not allow any pli. */
        break;
    default:
        pli = MP4SYS_AUDIO_PLI_NOT_SPECIFIED; /* something we don't know/support, or what the spec never covers. */
        break;
    }
    return pli;
}

static int mp4sys_is_same_profile( mp4sys_audioProfileLevelIndication a, mp4sys_audioProfileLevelIndication b )
{
    switch( a )
    {
    case MP4SYS_AUDIO_PLI_Main_L1:
    case MP4SYS_AUDIO_PLI_Main_L2:
    case MP4SYS_AUDIO_PLI_Main_L3:
    case MP4SYS_AUDIO_PLI_Main_L4:
        if( MP4SYS_AUDIO_PLI_Main_L1 <= b && b <= MP4SYS_AUDIO_PLI_Main_L4 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_Scalable_L1:
    case MP4SYS_AUDIO_PLI_Scalable_L2:
    case MP4SYS_AUDIO_PLI_Scalable_L3:
    case MP4SYS_AUDIO_PLI_Scalable_L4:
        if( MP4SYS_AUDIO_PLI_Scalable_L1 <= b && b <= MP4SYS_AUDIO_PLI_Scalable_L4 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_Speech_L1:
    case MP4SYS_AUDIO_PLI_Speech_L2:
        if( MP4SYS_AUDIO_PLI_Speech_L1 <= b && b <= MP4SYS_AUDIO_PLI_Speech_L2 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_Synthetic_L1:
    case MP4SYS_AUDIO_PLI_Synthetic_L2:
    case MP4SYS_AUDIO_PLI_Synthetic_L3:
        if( MP4SYS_AUDIO_PLI_Synthetic_L1 <= b && b <= MP4SYS_AUDIO_PLI_Synthetic_L3 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_HighQuality_L1:
    case MP4SYS_AUDIO_PLI_HighQuality_L2:
    case MP4SYS_AUDIO_PLI_HighQuality_L3:
    case MP4SYS_AUDIO_PLI_HighQuality_L4:
    case MP4SYS_AUDIO_PLI_HighQuality_L5:
    case MP4SYS_AUDIO_PLI_HighQuality_L6:
    case MP4SYS_AUDIO_PLI_HighQuality_L7:
    case MP4SYS_AUDIO_PLI_HighQuality_L8:
        if( MP4SYS_AUDIO_PLI_HighQuality_L1 <= b && b <= MP4SYS_AUDIO_PLI_HighQuality_L8 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_LowDelay_L1:
    case MP4SYS_AUDIO_PLI_LowDelay_L2:
    case MP4SYS_AUDIO_PLI_LowDelay_L3:
    case MP4SYS_AUDIO_PLI_LowDelay_L4:
    case MP4SYS_AUDIO_PLI_LowDelay_L5:
    case MP4SYS_AUDIO_PLI_LowDelay_L6:
    case MP4SYS_AUDIO_PLI_LowDelay_L7:
    case MP4SYS_AUDIO_PLI_LowDelay_L8:
        if( MP4SYS_AUDIO_PLI_LowDelay_L1 <= b && b <= MP4SYS_AUDIO_PLI_LowDelay_L8 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_Natural_L1:
    case MP4SYS_AUDIO_PLI_Natural_L2:
    case MP4SYS_AUDIO_PLI_Natural_L3:
    case MP4SYS_AUDIO_PLI_Natural_L4:
        if( MP4SYS_AUDIO_PLI_Natural_L1 <= b && b <= MP4SYS_AUDIO_PLI_Natural_L4 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_MobileInternetworking_L1:
    case MP4SYS_AUDIO_PLI_MobileInternetworking_L2:
    case MP4SYS_AUDIO_PLI_MobileInternetworking_L3:
    case MP4SYS_AUDIO_PLI_MobileInternetworking_L4:
    case MP4SYS_AUDIO_PLI_MobileInternetworking_L5:
    case MP4SYS_AUDIO_PLI_MobileInternetworking_L6:
        if( MP4SYS_AUDIO_PLI_MobileInternetworking_L1 <= b && b <= MP4SYS_AUDIO_PLI_MobileInternetworking_L6 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_AAC_L1:
    case MP4SYS_AUDIO_PLI_AAC_L2:
    case MP4SYS_AUDIO_PLI_AAC_L4:
    case MP4SYS_AUDIO_PLI_AAC_L5:
        if( MP4SYS_AUDIO_PLI_AAC_L1 <= b && b <= MP4SYS_AUDIO_PLI_AAC_L5 )
            return 1;
        return 0;
        break;
    case MP4SYS_AUDIO_PLI_HE_AAC_L2:
    case MP4SYS_AUDIO_PLI_HE_AAC_L3:
    case MP4SYS_AUDIO_PLI_HE_AAC_L4:
    case MP4SYS_AUDIO_PLI_HE_AAC_L5:
        if( MP4SYS_AUDIO_PLI_HE_AAC_L2 <= b && b <= MP4SYS_AUDIO_PLI_HE_AAC_L5 )
            return 1;
        return 0;
        break;
    default:
        break;
    }
    return 0;
}

/* NOTE: This function is not strictly preferable, but accurate.
   The spec of audioProfileLevelIndication is too much complicated. */
mp4sys_audioProfileLevelIndication mp4sys_max_audioProfileLevelIndication( mp4sys_audioProfileLevelIndication a, mp4sys_audioProfileLevelIndication b )
{
    /* NONE_REQUIRED is minimal priotity, and NOT_SPECIFIED is max priority. */
    if( a == MP4SYS_AUDIO_PLI_NOT_SPECIFIED || b == MP4SYS_AUDIO_PLI_NONE_REQUIRED )
        return a;
    if( a == MP4SYS_AUDIO_PLI_NONE_REQUIRED || b == MP4SYS_AUDIO_PLI_NOT_SPECIFIED )
        return b;
    mp4sys_audioProfileLevelIndication c, d;
    if( a < b )
    {
        c = a;
        d = b;
    }
    else
    {
        c = b;
        d = a;
    }
    /* AAC-LC and SBR specific; If mixtured there, use correspond HE_AAC profile. */
    if( MP4SYS_AUDIO_PLI_AAC_L1 <= c && c <= MP4SYS_AUDIO_PLI_AAC_L5
        && MP4SYS_AUDIO_PLI_HE_AAC_L2 <= d && d <= MP4SYS_AUDIO_PLI_HE_AAC_L5 )
    {
        if( c <= MP4SYS_AUDIO_PLI_AAC_L2 )
            return d;
        c += 4; /* upgrade to HE-AAC */
        return c > d ? c : d;
    }
    /* General */
    if( mp4sys_is_same_profile( c, d ) )
        return d;
    return MP4SYS_AUDIO_PLI_NOT_SPECIFIED;
}
