/*****************************************************************************
 * mp4a.h:
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

#ifndef MP4A_H
#define MP4A_H

#include "utils.h"

/***************************************************************************
    MPEG-4 Systems for MPEG-4 Audio
***************************************************************************/

/* ISO/IEC 14496-3 1.6.2.2 Payloads, Table 1.15 Audio Object Types */
typedef enum {
    MP4A_AUDIO_OBJECT_TYPE_NULL                           = 0,
    MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN                       = 1, /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_AAC_LC                         = 2, /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_AAC_SSR                        = 3, /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_AAC_LTP                        = 4, /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_SBR                            = 5, /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_AAC_scalable                   = 6, /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_TwinVQ                         = 7, /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_CELP                           = 8, /* ISO/IEC 14496-3 subpart 3 */
    MP4A_AUDIO_OBJECT_TYPE_HVXC                           = 9, /* ISO/IEC 14496-3 subpart 2 */
    MP4A_AUDIO_OBJECT_TYPE_TTSI                           = 12, /* ISO/IEC 14496-3 subpart 6 */
    MP4A_AUDIO_OBJECT_TYPE_Main_synthetic                 = 13, /* ISO/IEC 14496-3 subpart 5 */
    MP4A_AUDIO_OBJECT_TYPE_Wavetable_synthesis            = 14, /* ISO/IEC 14496-3 subpart 5 */
    MP4A_AUDIO_OBJECT_TYPE_General_MIDI                   = 15, /* ISO/IEC 14496-3 subpart 5 */
    MP4A_AUDIO_OBJECT_TYPE_Algorithmic_Synthesis_Audio_FX = 16, /* ISO/IEC 14496-3 subpart 5 */
    MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LC                      = 17, /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LTP                     = 19, /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_ER_AAC_scalable                = 20, /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_ER_Twin_VQ                     = 21, /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_ER_BSAC                        = 22, /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_ER_AAC_LD                      = 23, /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_ER_CELP                        = 24, /* ISO/IEC 14496-3 subpart 3 */
    MP4A_AUDIO_OBJECT_TYPE_ER_HVXC                        = 25, /* ISO/IEC 14496-3 subpart 2 */
    MP4A_AUDIO_OBJECT_TYPE_ER_HILN                        = 26, /* ISO/IEC 14496-3 subpart 7 */
    MP4A_AUDIO_OBJECT_TYPE_ER_Parametric                  = 27, /* ISO/IEC 14496-3 subpart 2 and 7 */
    MP4A_AUDIO_OBJECT_TYPE_SSC                            = 28, /* ISO/IEC 14496-3 subpart 8 */
    MP4A_AUDIO_OBJECT_TYPE_ESCAPE                         = 31,
    MP4A_AUDIO_OBJECT_TYPE_Layer_1                        = 32, /* ISO/IEC 14496-3 subpart 9 */
    MP4A_AUDIO_OBJECT_TYPE_Layer_2                        = 33, /* ISO/IEC 14496-3 subpart 9 */
    MP4A_AUDIO_OBJECT_TYPE_Layer_3                        = 34, /* ISO/IEC 14496-3 subpart 9 */
    MP4A_AUDIO_OBJECT_TYPE_DST                            = 35, /* ISO/IEC 14496-3 subpart 10 */
} mp4a_AudioObjectType;

/* 14496-3 1.5.2.4 audioProfileLevelIndication Table 1.12 audioProfileLevelIndication values */
typedef enum {
    MP4A_AUDIO_PLI_Reserved                 = 0x00, /* Reserved for ISO use */
    MP4A_AUDIO_PLI_Main_L1                  = 0x01, /* Main Audio Profile L1 */
    MP4A_AUDIO_PLI_Main_L2                  = 0x02, /* Main Audio Profile L2 */
    MP4A_AUDIO_PLI_Main_L3                  = 0x03, /* Main Audio Profile L3 */
    MP4A_AUDIO_PLI_Main_L4                  = 0x04, /* Main Audio Profile L4 */
    MP4A_AUDIO_PLI_Scalable_L1              = 0x05, /* Scalable Audio Profile L1 */
    MP4A_AUDIO_PLI_Scalable_L2              = 0x06, /* Scalable Audio Profile L2 */
    MP4A_AUDIO_PLI_Scalable_L3              = 0x07, /* Scalable Audio Profile L3 */
    MP4A_AUDIO_PLI_Scalable_L4              = 0x08, /* Scalable Audio Profile L4 */
    MP4A_AUDIO_PLI_Speech_L1                = 0x09, /* Speech Audio Profile L1 */
    MP4A_AUDIO_PLI_Speech_L2                = 0x0A, /* Speech Audio Profile L2 */
    MP4A_AUDIO_PLI_Synthetic_L1             = 0x0B, /* Synthetic Audio Profile L1 */
    MP4A_AUDIO_PLI_Synthetic_L2             = 0x0C, /* Synthetic Audio Profile L2 */
    MP4A_AUDIO_PLI_Synthetic_L3             = 0x0D, /* Synthetic Audio Profile L3 */
    MP4A_AUDIO_PLI_HighQuality_L1           = 0x0E, /* High Quality Audio Profile L1 */
    MP4A_AUDIO_PLI_HighQuality_L2           = 0x0F, /* High Quality Audio Profile L2 */
    MP4A_AUDIO_PLI_HighQuality_L3           = 0x10, /* High Quality Audio Profile L3 */
    MP4A_AUDIO_PLI_HighQuality_L4           = 0x11, /* High Quality Audio Profile L4 */
    MP4A_AUDIO_PLI_HighQuality_L5           = 0x12, /* High Quality Audio Profile L5 */
    MP4A_AUDIO_PLI_HighQuality_L6           = 0x13, /* High Quality Audio Profile L6 */
    MP4A_AUDIO_PLI_HighQuality_L7           = 0x14, /* High Quality Audio Profile L7 */
    MP4A_AUDIO_PLI_HighQuality_L8           = 0x15, /* High Quality Audio Profile L8 */
    MP4A_AUDIO_PLI_LowDelay_L1              = 0x16, /* Low Delay Audio Profile L1 */
    MP4A_AUDIO_PLI_LowDelay_L2              = 0x17, /* Low Delay Audio Profile L2 */
    MP4A_AUDIO_PLI_LowDelay_L3              = 0x18, /* Low Delay Audio Profile L3 */
    MP4A_AUDIO_PLI_LowDelay_L4              = 0x19, /* Low Delay Audio Profile L4 */
    MP4A_AUDIO_PLI_LowDelay_L5              = 0x1A, /* Low Delay Audio Profile L5 */
    MP4A_AUDIO_PLI_LowDelay_L6              = 0x1B, /* Low Delay Audio Profile L6 */
    MP4A_AUDIO_PLI_LowDelay_L7              = 0x1C, /* Low Delay Audio Profile L7 */
    MP4A_AUDIO_PLI_LowDelay_L8              = 0x1D, /* Low Delay Audio Profile L8 */
    MP4A_AUDIO_PLI_Natural_L1               = 0x1E, /* Natural Audio Profile L1 */
    MP4A_AUDIO_PLI_Natural_L2               = 0x1F, /* Natural Audio Profile L2 */
    MP4A_AUDIO_PLI_Natural_L3               = 0x20, /* Natural Audio Profile L3 */
    MP4A_AUDIO_PLI_Natural_L4               = 0x21, /* Natural Audio Profile L4 */
    MP4A_AUDIO_PLI_MobileInternetworking_L1 = 0x22, /* Mobile Audio Internetworking Profile L1 */
    MP4A_AUDIO_PLI_MobileInternetworking_L2 = 0x23, /* Mobile Audio Internetworking Profile L2 */
    MP4A_AUDIO_PLI_MobileInternetworking_L3 = 0x24, /* Mobile Audio Internetworking Profile L3 */
    MP4A_AUDIO_PLI_MobileInternetworking_L4 = 0x25, /* Mobile Audio Internetworking Profile L4 */
    MP4A_AUDIO_PLI_MobileInternetworking_L5 = 0x26, /* Mobile Audio Internetworking Profile L5 */
    MP4A_AUDIO_PLI_MobileInternetworking_L6 = 0x27, /* Mobile Audio Internetworking Profile L6 */
    MP4A_AUDIO_PLI_AAC_L1                   = 0x28, /* AAC Profile L1 */
    MP4A_AUDIO_PLI_AAC_L2                   = 0x29, /* AAC Profile L2 */
    MP4A_AUDIO_PLI_AAC_L4                   = 0x2A, /* AAC Profile L4 */
    MP4A_AUDIO_PLI_AAC_L5                   = 0x2B, /* AAC Profile L5 */
    MP4A_AUDIO_PLI_HE_AAC_L2                = 0x2C, /* High Efficiency AAC Profile L2 */
    MP4A_AUDIO_PLI_HE_AAC_L3                = 0x2D, /* High Efficiency AAC Profile L3 */
    MP4A_AUDIO_PLI_HE_AAC_L4                = 0x2E, /* High Efficiency AAC Profile L4 */
    MP4A_AUDIO_PLI_HE_AAC_L5                = 0x2F, /* High Efficiency AAC Profile L5 */
    MP4A_AUDIO_PLI_NOT_SPECIFIED            = 0xFE, /* no audio profile specified */
    MP4A_AUDIO_PLI_NONE_REQUIRED            = 0xFF, /* no audio capability required */
} mp4a_audioProfileLevelIndication;

/* see ISO/IEC 14496-3 1.6.5 Signaling of SBR, Table 1.22 SBR Signaling and Corresponding Decoder Behavior */
typedef enum {
    MP4A_AAC_SBR_NOT_SPECIFIED = 0x0, /* not mention to SBR presence. Implicit signaling. */
    MP4A_AAC_SBR_NONE,                /* explicitly signals SBR does not present. Useless in general. */
    MP4A_AAC_SBR_BACKWARD_COMPATIBLE, /* explicitly signals SBR present. Recommended method to signal SBR. */
    MP4A_AAC_SBR_HIERARCHICAL         /* SBR exists. SBR dedicated method. */
} mp4a_aac_sbr_mode;

#ifndef MP4A_INTERNAL

typedef void mp4a_AudioSpecificConfig_t;

/* export for mp4sys / importer */
mp4a_AudioSpecificConfig_t* mp4a_create_AudioSpecificConfig( mp4a_AudioObjectType aot, uint32_t frequency, uint32_t channels, mp4a_aac_sbr_mode sbr_mode );
void mp4a_put_AudioSpecificConfig( lsmash_bs_t* bs, mp4a_AudioSpecificConfig_t* asc );
void mp4a_remove_AudioSpecificConfig( mp4a_AudioSpecificConfig_t* asc );

/* export for importer */
extern const uint32_t mp4a_AAC_frequency_table[13][4];

/* profileLevelIndication relative functions. */
mp4a_audioProfileLevelIndication mp4a_get_audioProfileLevelIndication( mp4a_AudioObjectType aot, uint32_t frequency, uint32_t channels, mp4a_aac_sbr_mode sbr_mode );
mp4a_audioProfileLevelIndication mp4a_max_audioProfileLevelIndication(
    mp4a_audioProfileLevelIndication a,
    mp4a_audioProfileLevelIndication b
);

#endif

#endif
