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

#endif

#endif
