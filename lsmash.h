/*****************************************************************************
 * lsmash.h:
 *****************************************************************************
 * Copyright (C) 2010-2011 L-SMASH project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
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

#ifndef LSMASH_H
#define LSMASH_H

#include <stdint.h>

#define PRIVATE     /* If this declaration is placed at a variable, any user shouldn't use it. */

#define LSMASH_4CC( a, b, c, d ) (((a)<<24) | ((b)<<16) | ((c)<<8) | (d))
#define LSMASH_PACK_ISO_LANGUAGE( a, b, c ) ((((a-0x60)&0x1f)<<10) | (((b-0x60)&0x1f)<<5) | ((c-0x60)&0x1f))

/* public constants */
typedef enum
{
    LSMASH_FILE_MODE_WRITE             = 1,
    LSMASH_FILE_MODE_READ              = 1<<1,
    LSMASH_FILE_MODE_DUMP              = 1<<2,
    LSMASH_FILE_MODE_FRAGMENTED        = 1<<16,
    LSMASH_FILE_MODE_WRITE_FRAGMENTED  = LSMASH_FILE_MODE_WRITE | LSMASH_FILE_MODE_FRAGMENTED,
    LSMASH_FILE_MODE_READ_FRAGMENTED   = LSMASH_FILE_MODE_READ  | LSMASH_FILE_MODE_FRAGMENTED,
} lsmash_file_mode;

typedef enum
{
    ISOM_MEDIA_HANDLER_TYPE_3GPP_SCENE_DESCRIPTION              = LSMASH_4CC( '3', 'g', 's', 'd' ),
    ISOM_MEDIA_HANDLER_TYPE_ID3_VERSION2_METADATA               = LSMASH_4CC( 'I', 'D', '3', '2' ),
    ISOM_MEDIA_HANDLER_TYPE_AUXILIARY_VIDEO_TRACK               = LSMASH_4CC( 'a', 'u', 'x', 'v' ),
    ISOM_MEDIA_HANDLER_TYPE_CPCM_AUXILIARY_METADATA             = LSMASH_4CC( 'c', 'p', 'a', 'd' ),
    ISOM_MEDIA_HANDLER_TYPE_CLOCK_REFERENCE_STREAM              = LSMASH_4CC( 'c', 'r', 's', 'm' ),
    ISOM_MEDIA_HANDLER_TYPE_DVB_MANDATORY_BASIC_DESCRIPTION     = LSMASH_4CC( 'd', 'm', 'b', 'd' ),
    ISOM_MEDIA_HANDLER_TYPE_TV_ANYTIME                          = LSMASH_4CC( 'd', 't', 'v', 'a' ),
    ISOM_MEDIA_HANDLER_TYPE_BROADBAND_CONTENT_GUIDE             = LSMASH_4CC( 'd', 't', 'v', 'a' ),
    ISOM_MEDIA_HANDLER_TYPE_FONT_DATA_STREAM                    = LSMASH_4CC( 'f', 'd', 's', 'm' ),
    ISOM_MEDIA_HANDLER_TYPE_GENERAL_MPEG4_SYSTEM_STREAM         = LSMASH_4CC( 'g', 'e', 's', 'm' ),
    ISOM_MEDIA_HANDLER_TYPE_HINT_TRACK                          = LSMASH_4CC( 'h', 'i', 'n', 't' ),
    ISOM_MEDIA_HANDLER_TYPE_IPDC_ELECTRONIC_SERVICE_GUIDE       = LSMASH_4CC( 'i', 'p', 'd', 'c' ),
    ISOM_MEDIA_HANDLER_TYPE_IPMP_STREAM                         = LSMASH_4CC( 'i', 'p', 's', 'm' ),
    ISOM_MEDIA_HANDLER_TYPE_MPEG7_STREAM                        = LSMASH_4CC( 'm', '7', 's', 'm' ),
    ISOM_MEDIA_HANDLER_TYPE_TIMED_METADATA_TRACK                = LSMASH_4CC( 'm', 'e', 't', 'a' ),
    ISOM_MEDIA_HANDLER_TYPE_MPEGJ_STREAM                        = LSMASH_4CC( 'm', 'j', 's', 'm' ),
    ISOM_MEDIA_HANDLER_TYPE_MPEG21_DIGITAL_ITEM                 = LSMASH_4CC( 'm', 'p', '2', '1' ),
    ISOM_MEDIA_HANDLER_TYPE_OBJECT_CONTENT_INFO_STREAM          = LSMASH_4CC( 'o', 'c', 's', 'm' ),
    ISOM_MEDIA_HANDLER_TYPE_OBJECT_DESCRIPTOR_STREAM            = LSMASH_4CC( 'o', 'd', 's', 'm' ),
    ISOM_MEDIA_HANDLER_TYPE_SCENE_DESCRIPTION_STREAM            = LSMASH_4CC( 's', 'd', 's', 'm' ),
    ISOM_MEDIA_HANDLER_TYPE_KEY_MANAGEMENT_MESSAGES             = LSMASH_4CC( 's', 'k', 'm', 'm' ),
    ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK                         = LSMASH_4CC( 's', 'o', 'u', 'n' ),
    ISOM_MEDIA_HANDLER_TYPE_TEXT_TRACK                          = LSMASH_4CC( 't', 'e', 'x', 't' ),
    ISOM_MEDIA_HANDLER_TYPE_PROPRIETARY_DESCRIPTIVE_METADATA    = LSMASH_4CC( 'u', 'r', 'i', ' ' ),
    ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK                         = LSMASH_4CC( 'v', 'i', 'd', 'e' ),
} lsmash_media_type;

typedef enum
{
    ISOM_BRAND_TYPE_3G2A  = LSMASH_4CC( '3', 'g', '2', 'a' ),
    ISOM_BRAND_TYPE_3GE6  = LSMASH_4CC( '3', 'g', 'e', '6' ),
    ISOM_BRAND_TYPE_3GG6  = LSMASH_4CC( '3', 'g', 'g', '6' ),
    ISOM_BRAND_TYPE_3GP4  = LSMASH_4CC( '3', 'g', 'p', '4' ),
    ISOM_BRAND_TYPE_3GP5  = LSMASH_4CC( '3', 'g', 'p', '5' ),
    ISOM_BRAND_TYPE_3GP6  = LSMASH_4CC( '3', 'g', 'p', '6' ),
    ISOM_BRAND_TYPE_3GR6  = LSMASH_4CC( '3', 'g', 'r', '6' ),
    ISOM_BRAND_TYPE_3GS6  = LSMASH_4CC( '3', 'g', 's', '6' ),
    ISOM_BRAND_TYPE_CAEP  = LSMASH_4CC( 'C', 'A', 'E', 'P' ),
    ISOM_BRAND_TYPE_CDES  = LSMASH_4CC( 'C', 'D', 'e', 's' ),
    ISOM_BRAND_TYPE_M4A   = LSMASH_4CC( 'M', '4', 'A', ' ' ),
    ISOM_BRAND_TYPE_M4B   = LSMASH_4CC( 'M', '4', 'B', ' ' ),
    ISOM_BRAND_TYPE_M4P   = LSMASH_4CC( 'M', '4', 'P', ' ' ),
    ISOM_BRAND_TYPE_M4V   = LSMASH_4CC( 'M', '4', 'V', ' ' ),
    ISOM_BRAND_TYPE_MPPI  = LSMASH_4CC( 'M', 'P', 'P', 'I' ),
    ISOM_BRAND_TYPE_ROSS  = LSMASH_4CC( 'R', 'O', 'S', 'S' ),
    ISOM_BRAND_TYPE_AVC1  = LSMASH_4CC( 'a', 'v', 'c', '1' ),
    ISOM_BRAND_TYPE_CAQV  = LSMASH_4CC( 'c', 'a', 'q', 'v' ),
    ISOM_BRAND_TYPE_DA0A  = LSMASH_4CC( 'd', 'a', '0', 'a' ),
    ISOM_BRAND_TYPE_DA0B  = LSMASH_4CC( 'd', 'a', '0', 'b' ),
    ISOM_BRAND_TYPE_DA1A  = LSMASH_4CC( 'd', 'a', '1', 'a' ),
    ISOM_BRAND_TYPE_DA1B  = LSMASH_4CC( 'd', 'a', '1', 'b' ),
    ISOM_BRAND_TYPE_DA2A  = LSMASH_4CC( 'd', 'a', '2', 'a' ),
    ISOM_BRAND_TYPE_DA2B  = LSMASH_4CC( 'd', 'a', '2', 'b' ),
    ISOM_BRAND_TYPE_DA3A  = LSMASH_4CC( 'd', 'a', '3', 'a' ),
    ISOM_BRAND_TYPE_DA3B  = LSMASH_4CC( 'd', 'a', '3', 'b' ),
    ISOM_BRAND_TYPE_DMB1  = LSMASH_4CC( 'd', 'm', 'b', '1' ),
    ISOM_BRAND_TYPE_DV1A  = LSMASH_4CC( 'd', 'v', '1', 'a' ),
    ISOM_BRAND_TYPE_DV1B  = LSMASH_4CC( 'd', 'v', '1', 'b' ),
    ISOM_BRAND_TYPE_DV2A  = LSMASH_4CC( 'd', 'v', '2', 'a' ),
    ISOM_BRAND_TYPE_DV2B  = LSMASH_4CC( 'd', 'v', '2', 'b' ),
    ISOM_BRAND_TYPE_DV3A  = LSMASH_4CC( 'd', 'v', '3', 'a' ),
    ISOM_BRAND_TYPE_DV3B  = LSMASH_4CC( 'd', 'v', '3', 'b' ),
    ISOM_BRAND_TYPE_DVR1  = LSMASH_4CC( 'd', 'v', 'r', '1' ),
    ISOM_BRAND_TYPE_DVT1  = LSMASH_4CC( 'd', 'v', 't', '1' ),
    ISOM_BRAND_TYPE_ISC2  = LSMASH_4CC( 'i', 's', 'c', '2' ),
    ISOM_BRAND_TYPE_ISO2  = LSMASH_4CC( 'i', 's', 'o', '2' ),
    ISOM_BRAND_TYPE_ISO3  = LSMASH_4CC( 'i', 's', 'o', '3' ),
    ISOM_BRAND_TYPE_ISO4  = LSMASH_4CC( 'i', 's', 'o', '4' ),
    ISOM_BRAND_TYPE_ISO5  = LSMASH_4CC( 'i', 's', 'o', '5' ),
    ISOM_BRAND_TYPE_ISO6  = LSMASH_4CC( 'i', 's', 'o', '6' ),
    ISOM_BRAND_TYPE_ISOM  = LSMASH_4CC( 'i', 's', 'o', 'm' ),
    ISOM_BRAND_TYPE_JPSI  = LSMASH_4CC( 'j', 'p', 's', 'i' ),
    ISOM_BRAND_TYPE_MJ2S  = LSMASH_4CC( 'm', 'j', '2', 'j' ),
    ISOM_BRAND_TYPE_MJP2  = LSMASH_4CC( 'm', 'j', 'p', '2' ),
    ISOM_BRAND_TYPE_MP21  = LSMASH_4CC( 'm', 'p', '2', '1' ),
    ISOM_BRAND_TYPE_MP41  = LSMASH_4CC( 'm', 'p', '4', '1' ),
    ISOM_BRAND_TYPE_MP42  = LSMASH_4CC( 'm', 'p', '4', '2' ),
    ISOM_BRAND_TYPE_MP71  = LSMASH_4CC( 'm', 'p', '7', '1' ),
    ISOM_BRAND_TYPE_NIKO  = LSMASH_4CC( 'n', 'i', 'k', 'o' ),
    ISOM_BRAND_TYPE_ODCF  = LSMASH_4CC( 'o', 'd', 'c', 'f' ),
    ISOM_BRAND_TYPE_OPF2  = LSMASH_4CC( 'o', 'p', 'f', '2' ),
    ISOM_BRAND_TYPE_OPX2  = LSMASH_4CC( 'o', 'p', 'x', '2' ),
    ISOM_BRAND_TYPE_PANA  = LSMASH_4CC( 'p', 'a', 'n', 'a' ),
    ISOM_BRAND_TYPE_QT    = LSMASH_4CC( 'q', 't', ' ', ' ' ),
    ISOM_BRAND_TYPE_SDV   = LSMASH_4CC( 's', 'd', 'v', ' ' ),
} lsmash_brand_type;

typedef enum
{
    /* Audio Type */
    ISOM_CODEC_TYPE_AC_3_AUDIO  = LSMASH_4CC( 'a', 'c', '-', '3' ),   /* AC-3 audio */
    ISOM_CODEC_TYPE_ALAC_AUDIO  = LSMASH_4CC( 'a', 'l', 'a', 'c' ),   /* Apple lossless audio codec */
    ISOM_CODEC_TYPE_DRA1_AUDIO  = LSMASH_4CC( 'd', 'r', 'a', '1' ),   /* DRA Audio */
    ISOM_CODEC_TYPE_DTSC_AUDIO  = LSMASH_4CC( 'd', 't', 's', 'c' ),   /* DTS Coherent Acoustics audio */
    ISOM_CODEC_TYPE_DTSH_AUDIO  = LSMASH_4CC( 'd', 't', 's', 'h' ),   /* DTS-HD High Resolution Audio */
    ISOM_CODEC_TYPE_DTSL_AUDIO  = LSMASH_4CC( 'd', 't', 's', 'l' ),   /* DTS-HD Master Audio */
    ISOM_CODEC_TYPE_DTSE_AUDIO  = LSMASH_4CC( 'd', 't', 's', 'e' ),   /* DTS Express low bit rate audio, also known as DTS LBR */
    ISOM_CODEC_TYPE_EC_3_AUDIO  = LSMASH_4CC( 'e', 'c', '-', '3' ),   /* Enhanced AC-3 audio */
    ISOM_CODEC_TYPE_ENCA_AUDIO  = LSMASH_4CC( 'e', 'n', 'c', 'a' ),   /* Encrypted/Protected audio */
    ISOM_CODEC_TYPE_G719_AUDIO  = LSMASH_4CC( 'g', '7', '1', '9' ),   /* ITU-T Recommendation G.719 (2008) */
    ISOM_CODEC_TYPE_G726_AUDIO  = LSMASH_4CC( 'g', '7', '2', '6' ),   /* ITU-T Recommendation G.726 (1990) */
    ISOM_CODEC_TYPE_M4AE_AUDIO  = LSMASH_4CC( 'm', '4', 'a', 'e' ),   /* MPEG-4 Audio Enhancement */
    ISOM_CODEC_TYPE_MLPA_AUDIO  = LSMASH_4CC( 'm', 'l', 'p', 'a' ),   /* MLP Audio */
    ISOM_CODEC_TYPE_MP4A_AUDIO  = LSMASH_4CC( 'm', 'p', '4', 'a' ),   /* MPEG-4 Audio */
    ISOM_CODEC_TYPE_RAW_AUDIO   = LSMASH_4CC( 'r', 'a', 'w', ' ' ),   /* Uncompressed audio */
    ISOM_CODEC_TYPE_SAMR_AUDIO  = LSMASH_4CC( 's', 'a', 'm', 'r' ),   /* Narrowband AMR voice */
    ISOM_CODEC_TYPE_SAWB_AUDIO  = LSMASH_4CC( 's', 'a', 'w', 'b' ),   /* Wideband AMR voice */
    ISOM_CODEC_TYPE_SAWP_AUDIO  = LSMASH_4CC( 's', 'a', 'w', 'p' ),   /* Extended AMR-WB (AMR-WB+) */
    ISOM_CODEC_TYPE_SEVC_AUDIO  = LSMASH_4CC( 's', 'e', 'v', 'c' ),   /* EVRC Voice */
    ISOM_CODEC_TYPE_SQCP_AUDIO  = LSMASH_4CC( 's', 'q', 'c', 'p' ),   /* 13K Voice */
    ISOM_CODEC_TYPE_SSMV_AUDIO  = LSMASH_4CC( 's', 's', 'm', 'v' ),   /* SMV Voice */
    ISOM_CODEC_TYPE_TWOS_AUDIO  = LSMASH_4CC( 't', 'w', 'o', 's' ),   /* Uncompressed 16-bit audio */

    QT_CODEC_TYPE_23NI_AUDIO    = LSMASH_4CC( '2', '3', 'n', 'i' ),   /* 32-bit little endian integer uncompressed */
    QT_CODEC_TYPE_MAC3_AUDIO    = LSMASH_4CC( 'M', 'A', 'C', '3' ),   /* MACE 3:1 */
    QT_CODEC_TYPE_MAC6_AUDIO    = LSMASH_4CC( 'M', 'A', 'C', '6' ),   /* MACE 6:1 */
    QT_CODEC_TYPE_NONE_AUDIO    = LSMASH_4CC( 'N', 'O', 'N', 'E' ),   /* either 'raw ' or 'twos' */
    QT_CODEC_TYPE_QDM2_AUDIO    = LSMASH_4CC( 'Q', 'D', 'M', '2' ),   /* Qdesign music 2 */
    QT_CODEC_TYPE_QDMC_AUDIO    = LSMASH_4CC( 'Q', 'D', 'M', 'C' ),   /* Qdesign music 1 */
    QT_CODEC_TYPE_QCLP_AUDIO    = LSMASH_4CC( 'Q', 'c', 'l', 'p' ),   /* Qualcomm PureVoice */
    QT_CODEC_TYPE_AC_3_AUDIO    = LSMASH_4CC( 'a', 'c', '-', '3' ),   /* Digital Audio Compression Standard (AC-3, Enhanced AC-3) */
    QT_CODEC_TYPE_AGSM_AUDIO    = LSMASH_4CC( 'a', 'g', 's', 'm' ),   /* GSM */
    QT_CODEC_TYPE_ALAC_AUDIO    = LSMASH_4CC( 'a', 'l', 'a', 'c' ),   /* Apple lossless audio codec */
    QT_CODEC_TYPE_ALAW_AUDIO    = LSMASH_4CC( 'a', 'l', 'a', 'w' ),   /* a-Law 2:1 */
    QT_CODEC_TYPE_CDX2_AUDIO    = LSMASH_4CC( 'c', 'd', 'x', '2' ),   /* CD/XA 2:1 */
    QT_CODEC_TYPE_CDX4_AUDIO    = LSMASH_4CC( 'c', 'd', 'x', '4' ),   /* CD/XA 4:1 */
    QT_CODEC_TYPE_DVCA_AUDIO    = LSMASH_4CC( 'd', 'v', 'c', 'a' ),   /* DV Audio */
    QT_CODEC_TYPE_DVI_AUDIO     = LSMASH_4CC( 'd', 'v', 'i', ' ' ),   /* DVI (as used in RTP, 4:1 compression) */
    QT_CODEC_TYPE_FL32_AUDIO    = LSMASH_4CC( 'f', 'l', '3', '2' ),   /* 32-bit float */
    QT_CODEC_TYPE_FL64_AUDIO    = LSMASH_4CC( 'f', 'l', '6', '4' ),   /* 64-bit float */
    QT_CODEC_TYPE_IMA4_AUDIO    = LSMASH_4CC( 'i', 'm', 'a', '4' ),   /* IMA (International Multimedia Assocation, defunct, 4:1) */
    QT_CODEC_TYPE_IN24_AUDIO    = LSMASH_4CC( 'i', 'n', '2', '4' ),   /* 24-bit integer uncompressed */
    QT_CODEC_TYPE_IN32_AUDIO    = LSMASH_4CC( 'i', 'n', '3', '2' ),   /* 32-bit integer uncompressed */
    QT_CODEC_TYPE_LPCM_AUDIO    = LSMASH_4CC( 'l', 'p', 'c', 'm' ),   /* Uncompressed audio (various integer and float formats) */
    QT_CODEC_TYPE_MP4A_AUDIO    = LSMASH_4CC( 'm', 'p', '4', 'a' ),   /* MPEG-4 Audio */
    QT_CODEC_TYPE_RAW_AUDIO     = LSMASH_4CC( 'r', 'a', 'w', ' ' ),   /* 8-bit offset-binary uncompressed */
    QT_CODEC_TYPE_SOWT_AUDIO    = LSMASH_4CC( 's', 'o', 'w', 't' ),   /* 16-bit little endian uncompressed */
    QT_CODEC_TYPE_TWOS_AUDIO    = LSMASH_4CC( 't', 'w', 'o', 's' ),   /* 8-bit or 16-bit big endian uncompressed */
    QT_CODEC_TYPE_ULAW_AUDIO    = LSMASH_4CC( 'u', 'l', 'a', 'w' ),   /* uLaw 2:1 */
    QT_CODEC_TYPE_VDVA_AUDIO    = LSMASH_4CC( 'v', 'd', 'v', 'a' ),   /* DV audio (variable duration per video frame) */
    QT_CODEC_TYPE_FULLMP3_AUDIO = LSMASH_4CC( '.', 'm', 'p', '3' ),   /* MPEG-1 layer 3, CBR & VBR (QT4.1 and later) */
    QT_CODEC_TYPE_MP3_AUDIO     = 0x6D730055,                         /* MPEG-1 layer 3, CBR only (pre-QT4.1) */
    QT_CODEC_TYPE_ADPCM2_AUDIO  = 0x6D730002,                         /* Microsoft ADPCM - ACM code 2 */
    QT_CODEC_TYPE_ADPCM17_AUDIO = 0x6D730011,                         /* DVI/Intel IMA ADPCM - ACM code 17 */
    QT_CODEC_TYPE_GSM49_AUDIO   = 0x6D730031,                         /* Microsoft GSM 6.10 - ACM code 49 */
    QT_CODEC_TYPE_NOT_SPECIFIED = 0x00000000,                         /* either 'raw ' or 'twos' */

    /* Video Type */
    ISOM_CODEC_TYPE_AVC1_VIDEO  = LSMASH_4CC( 'a', 'v', 'c', '1' ),   /* Advanced Video Coding */
    ISOM_CODEC_TYPE_AVC2_VIDEO  = LSMASH_4CC( 'a', 'v', 'c', '2' ),   /* Advanced Video Coding */
    ISOM_CODEC_TYPE_AVCP_VIDEO  = LSMASH_4CC( 'a', 'v', 'c', 'p' ),   /* Advanced Video Coding Parameters */
    ISOM_CODEC_TYPE_DRAC_VIDEO  = LSMASH_4CC( 'd', 'r', 'a', 'c' ),   /* Dirac Video Coder */
    ISOM_CODEC_TYPE_ENCV_VIDEO  = LSMASH_4CC( 'e', 'n', 'c', 'v' ),   /* Encrypted/protected video */
    ISOM_CODEC_TYPE_MJP2_VIDEO  = LSMASH_4CC( 'm', 'j', 'p', '2' ),   /* Motion JPEG 2000 */
    ISOM_CODEC_TYPE_MP4V_VIDEO  = LSMASH_4CC( 'm', 'p', '4', 'v' ),   /* MPEG-4 Visual */
    ISOM_CODEC_TYPE_MVC1_VIDEO  = LSMASH_4CC( 'm', 'v', 'c', '1' ),   /* Multiview coding */
    ISOM_CODEC_TYPE_MVC2_VIDEO  = LSMASH_4CC( 'm', 'v', 'c', '2' ),   /* Multiview coding */
    ISOM_CODEC_TYPE_S263_VIDEO  = LSMASH_4CC( 's', '2', '6', '3' ),   /* ITU H.263 video (3GPP format) */
    ISOM_CODEC_TYPE_SVC1_VIDEO  = LSMASH_4CC( 's', 'v', 'c', '1' ),   /* Scalable Video Coding */
    ISOM_CODEC_TYPE_VC_1_VIDEO  = LSMASH_4CC( 'v', 'c', '-', '1' ),   /* SMPTE VC-1 */

    QT_CODEC_TYPE_CFHD_VIDEO    = LSMASH_4CC( 'C', 'F', 'H', 'D' ),   /* CineForm High-Definition (HD) wavelet codec */
    QT_CODEC_TYPE_DV10_VIDEO    = LSMASH_4CC( 'D', 'V', '1', '0' ),   /* Digital Voodoo 10 bit Uncompressed 4:2:2 codec */
    QT_CODEC_TYPE_DVOO_VIDEO    = LSMASH_4CC( 'D', 'V', 'O', 'O' ),   /* Digital Voodoo 8 bit Uncompressed 4:2:2 codec */
    QT_CODEC_TYPE_DVOR_VIDEO    = LSMASH_4CC( 'D', 'V', 'O', 'R' ),   /* Digital Voodoo intermediate raw */
    QT_CODEC_TYPE_DVTV_VIDEO    = LSMASH_4CC( 'D', 'V', 'T', 'V' ),   /* Digital Voodoo intermediate 2vuy */
    QT_CODEC_TYPE_DVVT_VIDEO    = LSMASH_4CC( 'D', 'V', 'V', 'T' ),   /* Digital Voodoo intermediate v210 */
    QT_CODEC_TYPE_HD10_VIDEO    = LSMASH_4CC( 'H', 'D', '1', '0' ),   /* Digital Voodoo 10 bit Uncompressed 4:2:2 HD codec */
    QT_CODEC_TYPE_M105_VIDEO    = LSMASH_4CC( 'M', '1', '0', '5' ),   /* Internal format of video data supported by Matrox hardware; pixel organization is proprietary*/
    QT_CODEC_TYPE_PNTG_VIDEO    = LSMASH_4CC( 'P', 'N', 'T', 'G' ),   /* Apple MacPaint image format */
    QT_CODEC_TYPE_SVQ1_VIDEO    = LSMASH_4CC( 'S', 'V', 'Q', '1' ),   /* Sorenson Video 1 video */
    QT_CODEC_TYPE_SVQ3_VIDEO    = LSMASH_4CC( 'S', 'V', 'Q', '3' ),   /* Sorenson Video 3 video */
    QT_CODEC_TYPE_SHR0_VIDEO    = LSMASH_4CC( 'S', 'h', 'r', '0' ),   /* Generic SheerVideo codec */
    QT_CODEC_TYPE_SHR1_VIDEO    = LSMASH_4CC( 'S', 'h', 'r', '1' ),   /* SheerVideo RGB[A] 8b - at 8 bits/channel */
    QT_CODEC_TYPE_SHR2_VIDEO    = LSMASH_4CC( 'S', 'h', 'r', '2' ),   /* SheerVideo Y'CbCr[A] 8bv 4:4:4[:4] - at 8 bits/channel, in ITU-R BT.601-4 video range */
    QT_CODEC_TYPE_SHR3_VIDEO    = LSMASH_4CC( 'S', 'h', 'r', '3' ),   /* SheerVideo Y'CbCr 8bv 4:2:2 - 2:1 chroma subsampling, at 8 bits/channel, in ITU-R BT.601-4 video range */
    QT_CODEC_TYPE_SHR4_VIDEO    = LSMASH_4CC( 'S', 'h', 'r', '4' ),   /* SheerVideo Y'CbCr 8bw 4:2:2 - 2:1 chroma subsampling, at 8 bits/channel, with full-range luma and wide-range two's-complement chroma */
    QT_CODEC_TYPE_WRLE_VIDEO    = LSMASH_4CC( 'W', 'R', 'L', 'E' ),   /* Windows BMP image format */
    QT_CODEC_TYPE_APCH_VIDEO    = LSMASH_4CC( 'a', 'p', 'c', 'h' ),   /* Apple ProRes 422 High Quality */
    QT_CODEC_TYPE_APCN_VIDEO    = LSMASH_4CC( 'a', 'p', 'c', 'n' ),   /* Apple ProRes 422 Standard Definition */
    QT_CODEC_TYPE_APCS_VIDEO    = LSMASH_4CC( 'a', 'p', 'c', 's' ),   /* Apple ProRes 422 LT */
    QT_CODEC_TYPE_APCO_VIDEO    = LSMASH_4CC( 'a', 'p', 'c', 'o' ),   /* Apple ProRes 422 Proxy */
    QT_CODEC_TYPE_AP4H_VIDEO    = LSMASH_4CC( 'a', 'p', '4', 'h' ),   /* Apple ProRes 4444 */
    QT_CODEC_TYPE_CIVD_VIDEO    = LSMASH_4CC( 'c', 'i', 'v', 'd' ),   /* Cinepak Video */
    QT_CODEC_TYPE_DRAC_VIDEO    = LSMASH_4CC( 'd', 'r', 'a', 'c' ),   /* Dirac Video Coder */
    QT_CODEC_TYPE_DVH5_VIDEO    = LSMASH_4CC( 'd', 'v', 'h', '5' ),   /* DVCPRO-HD 1080/50i */
    QT_CODEC_TYPE_DVH6_VIDEO    = LSMASH_4CC( 'd', 'v', 'h', '6' ),   /* DVCPRO-HD 1080/60i */
    QT_CODEC_TYPE_DVHP_VIDEO    = LSMASH_4CC( 'd', 'v', 'h', 'p' ),   /* DVCPRO-HD 720/60p */
    QT_CODEC_TYPE_FLIC_VIDEO    = LSMASH_4CC( 'f', 'l', 'i', 'c' ),   /* Autodesk FLIC animation format */
    QT_CODEC_TYPE_GIF_VIDEO     = LSMASH_4CC( 'g', 'i', 'f', ' ' ),   /* GIF image format */
    QT_CODEC_TYPE_H261_VIDEO    = LSMASH_4CC( 'h', '2', '6', '1' ),   /* ITU H.261 video */
    QT_CODEC_TYPE_H263_VIDEO    = LSMASH_4CC( 'h', '2', '6', '3' ),   /* ITU H.263 video */
    QT_CODEC_TYPE_JPEG_VIDEO    = LSMASH_4CC( 'j', 'p', 'e', 'g' ),   /* JPEG image format */
    QT_CODEC_TYPE_MJPA_VIDEO    = LSMASH_4CC( 'm', 'j', 'p', 'a' ),   /* Motion-JPEG (format A) */
    QT_CODEC_TYPE_MJPB_VIDEO    = LSMASH_4CC( 'm', 'j', 'p', 'b' ),   /* Motion-JPEG (format B) */
    QT_CODEC_TYPE_PNG_VIDEO     = LSMASH_4CC( 'p', 'n', 'g', ' ' ),   /* W3C Portable Network Graphics (PNG) */
    QT_CODEC_TYPE_RLE_VIDEO     = LSMASH_4CC( 'r', 'l', 'e', ' ' ),   /* Apple animation codec */
    QT_CODEC_TYPE_RPZA_VIDEO    = LSMASH_4CC( 'r', 'p', 'z', 'a' ),   /* Apple simple video 'road pizza' compression */
    QT_CODEC_TYPE_TGA_VIDEO     = LSMASH_4CC( 't', 'g', 'a', ' ' ),   /* Truvision Targa video format */
    QT_CODEC_TYPE_TIFF_VIDEO    = LSMASH_4CC( 't', 'i', 'f', 'f' ),   /* Tagged Image File Format (Adobe) */
    QT_CODEC_TYPE_V210_VIDEO    = LSMASH_4CC( 'v', '2', '1', '0' ),   /* Uncompressed Y'CbCr, 10-bit-per-component 4:2:2 */
    QT_CODEC_TYPE_V216_VIDEO    = LSMASH_4CC( 'v', '2', '1', '6' ),   /* Uncompressed Y'CbCr, 10, 12, 14, or 16-bit-per-component 4:2:2 */
    QT_CODEC_TYPE_V308_VIDEO    = LSMASH_4CC( 'v', '3', '0', '8' ),   /* Uncompressed Y'CbCr, 8-bit-per-component 4:4:4 */
    QT_CODEC_TYPE_V408_VIDEO    = LSMASH_4CC( 'v', '4', '0', '8' ),   /* Uncompressed Y'CbCr, 8-bit-per-component 4:4:4:4 */
    QT_CODEC_TYPE_V410_VIDEO    = LSMASH_4CC( 'v', '4', '1', '0' ),   /* Uncompressed Y'CbCr, 10-bit-per-component 4:4:4 */
    QT_CODEC_TYPE_YUV2_VIDEO    = LSMASH_4CC( 'y', 'u', 'v', '2' ),   /* Uncompressed Y'CbCr, 8-bit-per-component 4:2:2 */

    /* Text Type */
    ISOM_CODEC_TYPE_ENCT_TEXT   = LSMASH_4CC( 'e', 'n', 'c', 't' ),   /* Encrypted Text */
    ISOM_CODEC_TYPE_TX3G_TEXT   = LSMASH_4CC( 't', 'x', '3', 'g' ),   /* Timed Text stream */

    QT_CODEC_TYPE_TEXT_TEXT     = LSMASH_4CC( 't', 'e', 'x', 't' ),   /* QuickTime Text Media */

    /* Hint Type */
    ISOM_CODEC_TYPE_FDP_HINT    = LSMASH_4CC( 'f', 'd', 'p', ' ' ),   /* File delivery hints */
    ISOM_CODEC_TYPE_M2TS_HINT   = LSMASH_4CC( 'm', '2', 't', 's' ),   /* MPEG-2 transport stream for DMB */
    ISOM_CODEC_TYPE_PM2T_HINT   = LSMASH_4CC( 'p', 'm', '2', 't' ),   /* Protected MPEG-2 Transport */
    ISOM_CODEC_TYPE_PRTP_HINT   = LSMASH_4CC( 'p', 'r', 't', 'p' ),   /* Protected RTP Reception */
    ISOM_CODEC_TYPE_RM2T_HINT   = LSMASH_4CC( 'r', 'm', '2', 't' ),   /* MPEG-2 Transport Reception */
    ISOM_CODEC_TYPE_RRTP_HINT   = LSMASH_4CC( 'r', 'r', 't', 'p' ),   /* RTP reception */
    ISOM_CODEC_TYPE_RSRP_HINT   = LSMASH_4CC( 'r', 's', 'r', 'p' ),   /* SRTP Reception */
    ISOM_CODEC_TYPE_RTP_HINT    = LSMASH_4CC( 'r', 't', 'p', ' ' ),   /* RTP Hints */
    ISOM_CODEC_TYPE_SM2T_HINT   = LSMASH_4CC( 's', 'm', '2', 't' ),   /* MPEG-2 Transport Server */
    ISOM_CODEC_TYPE_SRTP_HINT   = LSMASH_4CC( 's', 'r', 't', 'p' ),   /* SRTP Hints */

    /* Metadata Type */
    ISOM_CODEC_TYPE_IXSE_META   = LSMASH_4CC( 'i', 'x', 's', 'e' ),   /* DVB Track Level Index Track */
    ISOM_CODEC_TYPE_METT_META   = LSMASH_4CC( 'm', 'e', 't', 't' ),   /* Text timed metadata */
    ISOM_CODEC_TYPE_METX_META   = LSMASH_4CC( 'm', 'e', 't', 'x' ),   /* XML timed metadata */
    ISOM_CODEC_TYPE_MLIX_META   = LSMASH_4CC( 'm', 'l', 'i', 'x' ),   /* DVB Movie level index track */
    ISOM_CODEC_TYPE_OKSD_META   = LSMASH_4CC( 'o', 'k', 's', 'd' ),   /* OMA Keys */
    ISOM_CODEC_TYPE_SVCM_META   = LSMASH_4CC( 's', 'v', 'c', 'M' ),   /* SVC metadata */
    ISOM_CODEC_TYPE_TEXT_META   = LSMASH_4CC( 't', 'e', 'x', 't' ),   /* Textual meta-data with MIME type */
    ISOM_CODEC_TYPE_URIM_META   = LSMASH_4CC( 'u', 'r', 'i', 'm' ),   /* URI identified timed metadata */
    ISOM_CODEC_TYPE_XML_META    = LSMASH_4CC( 'x', 'm', 'l', ' ' ),   /* XML-formatted meta-data */

    /* Other Type */
    ISOM_CODEC_TYPE_ENCS_SYSTEM = LSMASH_4CC( 'e', 'n', 'c', 's' ),   /* Encrypted Systems stream */
    ISOM_CODEC_TYPE_MP4S_SYSTEM = LSMASH_4CC( 'm', 'p', '4', 's' ),   /* MPEG-4 Systems */
} lsmash_codec_type;

typedef enum
{
    ISOM_LANGUAGE_CODE_ENGLISH          = LSMASH_PACK_ISO_LANGUAGE( 'e', 'n', 'g' ),
    ISOM_LANGUAGE_CODE_FRENCH           = LSMASH_PACK_ISO_LANGUAGE( 'f', 'r', 'a' ),
    ISOM_LANGUAGE_CODE_GERMAN           = LSMASH_PACK_ISO_LANGUAGE( 'd', 'e', 'u' ),
    ISOM_LANGUAGE_CODE_ITALIAN          = LSMASH_PACK_ISO_LANGUAGE( 'i', 't', 'a' ),
    ISOM_LANGUAGE_CODE_DUTCH_M          = LSMASH_PACK_ISO_LANGUAGE( 'd', 'u', 'm' ),
    ISOM_LANGUAGE_CODE_SWEDISH          = LSMASH_PACK_ISO_LANGUAGE( 's', 'w', 'e' ),
    ISOM_LANGUAGE_CODE_SPANISH          = LSMASH_PACK_ISO_LANGUAGE( 's', 'p', 'a' ),
    ISOM_LANGUAGE_CODE_DANISH           = LSMASH_PACK_ISO_LANGUAGE( 'd', 'a', 'n' ),
    ISOM_LANGUAGE_CODE_PORTUGUESE       = LSMASH_PACK_ISO_LANGUAGE( 'p', 'o', 'r' ),
    ISOM_LANGUAGE_CODE_NORWEGIAN        = LSMASH_PACK_ISO_LANGUAGE( 'n', 'o', 'r' ),
    ISOM_LANGUAGE_CODE_HEBREW           = LSMASH_PACK_ISO_LANGUAGE( 'h', 'e', 'b' ),
    ISOM_LANGUAGE_CODE_JAPANESE         = LSMASH_PACK_ISO_LANGUAGE( 'j', 'p', 'n' ),
    ISOM_LANGUAGE_CODE_ARABIC           = LSMASH_PACK_ISO_LANGUAGE( 'a', 'r', 'a' ),
    ISOM_LANGUAGE_CODE_FINNISH          = LSMASH_PACK_ISO_LANGUAGE( 'f', 'i', 'n' ),
    ISOM_LANGUAGE_CODE_GREEK            = LSMASH_PACK_ISO_LANGUAGE( 'e', 'l', 'l' ),
    ISOM_LANGUAGE_CODE_ICELANDIC        = LSMASH_PACK_ISO_LANGUAGE( 'i', 's', 'l' ),
    ISOM_LANGUAGE_CODE_MALTESE          = LSMASH_PACK_ISO_LANGUAGE( 'm', 'l', 't' ),
    ISOM_LANGUAGE_CODE_TURKISH          = LSMASH_PACK_ISO_LANGUAGE( 't', 'u', 'r' ),
    ISOM_LANGUAGE_CODE_CROATIAN         = LSMASH_PACK_ISO_LANGUAGE( 'h', 'r', 'v' ),
    ISOM_LANGUAGE_CODE_CHINESE          = LSMASH_PACK_ISO_LANGUAGE( 'z', 'h', 'o' ),
    ISOM_LANGUAGE_CODE_URDU             = LSMASH_PACK_ISO_LANGUAGE( 'u', 'r', 'd' ),
    ISOM_LANGUAGE_CODE_HINDI            = LSMASH_PACK_ISO_LANGUAGE( 'h', 'i', 'n' ),
    ISOM_LANGUAGE_CODE_THAI             = LSMASH_PACK_ISO_LANGUAGE( 't', 'h', 'a' ),
    ISOM_LANGUAGE_CODE_KOREAN           = LSMASH_PACK_ISO_LANGUAGE( 'k', 'o', 'r' ),
    ISOM_LANGUAGE_CODE_LITHUANIAN       = LSMASH_PACK_ISO_LANGUAGE( 'l', 'i', 't' ),
    ISOM_LANGUAGE_CODE_POLISH           = LSMASH_PACK_ISO_LANGUAGE( 'p', 'o', 'l' ),
    ISOM_LANGUAGE_CODE_HUNGARIAN        = LSMASH_PACK_ISO_LANGUAGE( 'h', 'u', 'n' ),
    ISOM_LANGUAGE_CODE_ESTONIAN         = LSMASH_PACK_ISO_LANGUAGE( 'e', 's', 't' ),
    ISOM_LANGUAGE_CODE_LATVIAN          = LSMASH_PACK_ISO_LANGUAGE( 'l', 'a', 'v' ),
    ISOM_LANGUAGE_CODE_SAMI             = LSMASH_PACK_ISO_LANGUAGE( 's', 'm', 'i' ),
    ISOM_LANGUAGE_CODE_FAROESE          = LSMASH_PACK_ISO_LANGUAGE( 'f', 'a', 'o' ),
    ISOM_LANGUAGE_CODE_RUSSIAN          = LSMASH_PACK_ISO_LANGUAGE( 'r', 'u', 's' ),
    ISOM_LANGUAGE_CODE_DUTCH            = LSMASH_PACK_ISO_LANGUAGE( 'n', 'l', 'd' ),
    ISOM_LANGUAGE_CODE_IRISH            = LSMASH_PACK_ISO_LANGUAGE( 'g', 'l', 'e' ),
    ISOM_LANGUAGE_CODE_ALBANIAN         = LSMASH_PACK_ISO_LANGUAGE( 's', 'q', 'i' ),
    ISOM_LANGUAGE_CODE_ROMANIAN         = LSMASH_PACK_ISO_LANGUAGE( 'r', 'o', 'n' ),
    ISOM_LANGUAGE_CODE_CZECH            = LSMASH_PACK_ISO_LANGUAGE( 'c', 'e', 's' ),
    ISOM_LANGUAGE_CODE_SLOVAK           = LSMASH_PACK_ISO_LANGUAGE( 's', 'l', 'k' ),
    ISOM_LANGUAGE_CODE_SLOVENIA         = LSMASH_PACK_ISO_LANGUAGE( 's', 'l', 'v' ),
    ISOM_LANGUAGE_CODE_YIDDISH          = LSMASH_PACK_ISO_LANGUAGE( 'y', 'i', 'd' ),
    ISOM_LANGUAGE_CODE_SERBIAN          = LSMASH_PACK_ISO_LANGUAGE( 's', 'r', 'p' ),
    ISOM_LANGUAGE_CODE_MACEDONIAN       = LSMASH_PACK_ISO_LANGUAGE( 'm', 'k', 'd' ),
    ISOM_LANGUAGE_CODE_BULGARIAN        = LSMASH_PACK_ISO_LANGUAGE( 'b', 'u', 'l' ),
    ISOM_LANGUAGE_CODE_UKRAINIAN        = LSMASH_PACK_ISO_LANGUAGE( 'u', 'k', 'r' ),
    ISOM_LANGUAGE_CODE_BELARUSIAN       = LSMASH_PACK_ISO_LANGUAGE( 'b', 'e', 'l' ),
    ISOM_LANGUAGE_CODE_UZBEK            = LSMASH_PACK_ISO_LANGUAGE( 'u', 'z', 'b' ),
    ISOM_LANGUAGE_CODE_KAZAKH           = LSMASH_PACK_ISO_LANGUAGE( 'k', 'a', 'z' ),
    ISOM_LANGUAGE_CODE_AZERBAIJANI      = LSMASH_PACK_ISO_LANGUAGE( 'a', 'z', 'e' ),
    ISOM_LANGUAGE_CODE_ARMENIAN         = LSMASH_PACK_ISO_LANGUAGE( 'h', 'y', 'e' ),
    ISOM_LANGUAGE_CODE_GEORGIAN         = LSMASH_PACK_ISO_LANGUAGE( 'k', 'a', 't' ),
    ISOM_LANGUAGE_CODE_MOLDAVIAN        = LSMASH_PACK_ISO_LANGUAGE( 'r', 'o', 'n' ),
    ISOM_LANGUAGE_CODE_KIRGHIZ          = LSMASH_PACK_ISO_LANGUAGE( 'k', 'i', 'r' ),
    ISOM_LANGUAGE_CODE_TAJIK            = LSMASH_PACK_ISO_LANGUAGE( 't', 'g', 'k' ),
    ISOM_LANGUAGE_CODE_TURKMEN          = LSMASH_PACK_ISO_LANGUAGE( 't', 'u', 'k' ),
    ISOM_LANGUAGE_CODE_MONGOLIAN        = LSMASH_PACK_ISO_LANGUAGE( 'm', 'o', 'n' ),
    ISOM_LANGUAGE_CODE_PASHTO           = LSMASH_PACK_ISO_LANGUAGE( 'p', 'u', 's' ),
    ISOM_LANGUAGE_CODE_KURDISH          = LSMASH_PACK_ISO_LANGUAGE( 'k', 'u', 'r' ),
    ISOM_LANGUAGE_CODE_KASHMIRI         = LSMASH_PACK_ISO_LANGUAGE( 'k', 'a', 's' ),
    ISOM_LANGUAGE_CODE_SINDHI           = LSMASH_PACK_ISO_LANGUAGE( 's', 'n', 'd' ),
    ISOM_LANGUAGE_CODE_TIBETAN          = LSMASH_PACK_ISO_LANGUAGE( 'b', 'o', 'd' ),
    ISOM_LANGUAGE_CODE_NEPALI           = LSMASH_PACK_ISO_LANGUAGE( 'n', 'e', 'p' ),
    ISOM_LANGUAGE_CODE_SANSKRIT         = LSMASH_PACK_ISO_LANGUAGE( 's', 'a', 'n' ),
    ISOM_LANGUAGE_CODE_MARATHI          = LSMASH_PACK_ISO_LANGUAGE( 'm', 'a', 'r' ),
    ISOM_LANGUAGE_CODE_BENGALI          = LSMASH_PACK_ISO_LANGUAGE( 'b', 'e', 'n' ),
    ISOM_LANGUAGE_CODE_ASSAMESE         = LSMASH_PACK_ISO_LANGUAGE( 'a', 's', 'm' ),
    ISOM_LANGUAGE_CODE_GUJARATI         = LSMASH_PACK_ISO_LANGUAGE( 'g', 'u', 'j' ),
    ISOM_LANGUAGE_CODE_PUNJABI          = LSMASH_PACK_ISO_LANGUAGE( 'p', 'a', 'n' ),
    ISOM_LANGUAGE_CODE_ORIYA            = LSMASH_PACK_ISO_LANGUAGE( 'o', 'r', 'i' ),
    ISOM_LANGUAGE_CODE_MALAYALAM        = LSMASH_PACK_ISO_LANGUAGE( 'm', 'a', 'l' ),
    ISOM_LANGUAGE_CODE_KANNADA          = LSMASH_PACK_ISO_LANGUAGE( 'k', 'a', 'n' ),
    ISOM_LANGUAGE_CODE_TAMIL            = LSMASH_PACK_ISO_LANGUAGE( 't', 'a', 'm' ),
    ISOM_LANGUAGE_CODE_TELUGU           = LSMASH_PACK_ISO_LANGUAGE( 't', 'e', 'l' ),
    ISOM_LANGUAGE_CODE_SINHALESE        = LSMASH_PACK_ISO_LANGUAGE( 's', 'i', 'n' ),
    ISOM_LANGUAGE_CODE_BURMESE          = LSMASH_PACK_ISO_LANGUAGE( 'm', 'y', 'a' ),
    ISOM_LANGUAGE_CODE_KHMER            = LSMASH_PACK_ISO_LANGUAGE( 'k', 'h', 'm' ),
    ISOM_LANGUAGE_CODE_LAO              = LSMASH_PACK_ISO_LANGUAGE( 'l', 'a', 'o' ),
    ISOM_LANGUAGE_CODE_VIETNAMESE       = LSMASH_PACK_ISO_LANGUAGE( 'v', 'i', 'e' ),
    ISOM_LANGUAGE_CODE_INDONESIAN       = LSMASH_PACK_ISO_LANGUAGE( 'i', 'n', 'd' ),
    ISOM_LANGUAGE_CODE_TAGALOG          = LSMASH_PACK_ISO_LANGUAGE( 't', 'g', 'l' ),
    ISOM_LANGUAGE_CODE_MALAY_ROMAN      = LSMASH_PACK_ISO_LANGUAGE( 'm', 's', 'a' ),
    ISOM_LANGUAGE_CODE_MAYAY_ARABIC     = LSMASH_PACK_ISO_LANGUAGE( 'm', 's', 'a' ),
    ISOM_LANGUAGE_CODE_AMHARIC          = LSMASH_PACK_ISO_LANGUAGE( 'a', 'm', 'h' ),
    ISOM_LANGUAGE_CODE_OROMO            = LSMASH_PACK_ISO_LANGUAGE( 'o', 'r', 'm' ),
    ISOM_LANGUAGE_CODE_SOMALI           = LSMASH_PACK_ISO_LANGUAGE( 's', 'o', 'm' ),
    ISOM_LANGUAGE_CODE_SWAHILI          = LSMASH_PACK_ISO_LANGUAGE( 's', 'w', 'a' ),
    ISOM_LANGUAGE_CODE_KINYARWANDA      = LSMASH_PACK_ISO_LANGUAGE( 'k', 'i', 'n' ),
    ISOM_LANGUAGE_CODE_RUNDI            = LSMASH_PACK_ISO_LANGUAGE( 'r', 'u', 'n' ),
    ISOM_LANGUAGE_CODE_CHEWA            = LSMASH_PACK_ISO_LANGUAGE( 'n', 'y', 'a' ),
    ISOM_LANGUAGE_CODE_MALAGASY         = LSMASH_PACK_ISO_LANGUAGE( 'm', 'l', 'g' ),
    ISOM_LANGUAGE_CODE_ESPERANTO        = LSMASH_PACK_ISO_LANGUAGE( 'e', 'p', 'o' ),
    ISOM_LANGUAGE_CODE_WELSH            = LSMASH_PACK_ISO_LANGUAGE( 'c', 'y', 'm' ),
    ISOM_LANGUAGE_CODE_BASQUE           = LSMASH_PACK_ISO_LANGUAGE( 'e', 'u', 's' ),
    ISOM_LANGUAGE_CODE_CATALAN          = LSMASH_PACK_ISO_LANGUAGE( 'c', 'a', 't' ),
    ISOM_LANGUAGE_CODE_LATIN            = LSMASH_PACK_ISO_LANGUAGE( 'l', 'a', 't' ),
    ISOM_LANGUAGE_CODE_QUECHUA          = LSMASH_PACK_ISO_LANGUAGE( 'q', 'u', 'e' ),
    ISOM_LANGUAGE_CODE_GUARANI          = LSMASH_PACK_ISO_LANGUAGE( 'g', 'r', 'n' ),
    ISOM_LANGUAGE_CODE_AYMARA           = LSMASH_PACK_ISO_LANGUAGE( 'a', 'y', 'm' ),
    ISOM_LANGUAGE_CODE_TATAR            = LSMASH_PACK_ISO_LANGUAGE( 'c', 'r', 'h' ),
    ISOM_LANGUAGE_CODE_UIGHUR           = LSMASH_PACK_ISO_LANGUAGE( 'u', 'i', 'g' ),
    ISOM_LANGUAGE_CODE_DZONGKHA         = LSMASH_PACK_ISO_LANGUAGE( 'd', 'z', 'o' ),
    ISOM_LANGUAGE_CODE_JAVANESE         = LSMASH_PACK_ISO_LANGUAGE( 'j', 'a', 'v' ),
    ISOM_LANGUAGE_CODE_UNDEFINED        = LSMASH_PACK_ISO_LANGUAGE( 'u', 'n', 'd' ),
} lsmash_iso_language_code;

typedef enum
{
#define UINT16_MAX_PLUS_ONE 0x10000
    QT_COLOR_PARAMETER_NOT_SPECIFIED = UINT16_MAX_PLUS_ONE,
    QT_COLOR_PARAMETER_ITU_R_BT470_M,
    QT_COLOR_PARAMETER_ITU_R_BT470_BG,
    QT_COLOR_PARAMETER_ITU_R_BT709,
    QT_COLOR_PARAMETER_SMPTE_170M,
    QT_COLOR_PARAMETER_SMPTE_240M,
    QT_COLOR_PARAMETER_SMPTE_274M,
    QT_COLOR_PARAMETER_SMPTE_293M,
    QT_COLOR_PARAMETER_SMPTE_296M,
    QT_COLOR_PARAMETER_END,
} lsmash_color_parameter;

typedef enum
{
    QT_CHANNEL_LABEL_UNKNOWN                = 0xffffffff,   /* unknown or unspecified other use */
    QT_CHANNEL_LABEL_UNUSED                 = 0,            /* channel is present, but has no intended use or destination */
    QT_CHANNEL_LABEL_USE_COORDINATES        = 100,          /* channel is described by the coordinates fields. */

    QT_CHANNEL_LABEL_LEFT                   = 1,
    QT_CHANNEL_LABEL_RIGHT                  = 2,
    QT_CHANNEL_LABEL_CENTER                 = 3,
    QT_CHANNEL_LABEL_LFE_SCREEN             = 4,
    QT_CHANNEL_LABEL_LEFT_SURROUND          = 5,            /* WAVE: "Back Left" */
    QT_CHANNEL_LABEL_RIGHT_SUROUND          = 6,            /* WAVE: "Back Right" */
    QT_CHANNEL_LABEL_LEFT_CENTER            = 7,
    QT_CHANNEL_LABEL_RIGHT_CENTER           = 8,
    QT_CHANNEL_LABEL_CENTER_SURROUND        = 9,            /* WAVE: "Back Center" or plain "Rear Surround" */
    QT_CHANNEL_LABEL_LEFT_SURROUND_DIRECT   = 10,           /* WAVE: "Side Left" */
    QT_CHANNEL_LABEL_RIGHT_SURROUND_DIRECT  = 11,           /* WAVE: "Side Right" */
    QT_CHANNEL_LABEL_TOP_CENTER_SURROUND    = 12,
    QT_CHANNEL_LABEL_VERTICAL_HEIGHT_LEFT   = 13,           /* WAVE: "Top Front Left" */
    QT_CHANNEL_LABEL_VERTICAL_HEIGHT_CENTER = 14,           /* WAVE: "Top Front Center" */
    QT_CHANNEL_LABEL_VERTICAL_HEIGHT_RIGHT  = 15,           /* WAVE: "Top Front Right" */

    QT_CHANNEL_LABEL_TOP_BACK_LEFT          = 16,
    QT_CHANNEL_LABEL_TOP_BACK_CENTER        = 17,
    QT_CHANNEL_LABEL_TOP_BACK_RIGHT         = 18,

    QT_CHANNEL_LABEL_REAR_SURROUND_LEFT     = 33,
    QT_CHANNEL_LABEL_REAR_SURROUND_RIGHT    = 34,
    QT_CHANNEL_LABEL_LEFT_WIDE              = 35,
    QT_CHANNEL_LABEL_RIGHT_WIDE             = 36,
    QT_CHANNEL_LABEL_LFE2                   = 37,
    QT_CHANNEL_LABEL_LEFT_TOTAL             = 38,           /* matrix encoded 4 channels */
    QT_CHANNEL_LABEL_RIGHT_TOTAL            = 39,           /* matrix encoded 4 channels */
    QT_CHANNEL_LABEL_HEARING_IMPAIRED       = 40,
    QT_CHANNEL_LABEL_NARRATION              = 41,
    QT_CHANNEL_LABEL_MONO                   = 42,
    QT_CHANNEL_LABEL_DIALOG_CENTRIC_MIX     = 43,

    QT_CHANNEL_LABEL_CENTER_SURROUND_DIRECT = 44,           /* back center, non diffuse */

    QT_CHANNEL_LABEL_HAPTIC                 = 45,

    /* first order ambisonic channels */
    QT_CHANNEL_LABEL_AMBISONIC_W            = 200,
    QT_CHANNEL_LABEL_AMBISONIC_X            = 201,
    QT_CHANNEL_LABEL_AMBISONIC_Y            = 202,
    QT_CHANNEL_LABEL_AMBISONIC_Z            = 203,

    /* Mid/Side Recording */
    QT_CHANNEL_LABEL_MS_MID                 = 204,
    QT_CHANNEL_LABEL_MS_SIDE                = 205,

    /* X-Y Recording */
    QT_CHANNEL_LABEL_XY_X                   = 206,
    QT_CHANNEL_LABEL_XY_Y                   = 207,

    /* other */
    QT_CHANNEL_LABEL_HEADPHONES_LEFT        = 301,
    QT_CHANNEL_LABEL_HEADPHONES_RIGHT       = 302,
    QT_CHANNEL_LABEL_CLICK_TRACK            = 304,
    QT_CHANNEL_LABEL_FOREIGN_LANGUAGE       = 305,

    /* generic discrete channel */
    QT_CHANNEL_LABEL_DISCRETE               = 400,

    /* numbered discrete channel */
    QT_CHANNEL_LABEL_DISCRETE_0             = (1<<16),
    QT_CHANNEL_LABEL_DISCRETE_1             = (1<<16) | 1,
    QT_CHANNEL_LABEL_DISCRETE_2             = (1<<16) | 2,
    QT_CHANNEL_LABEL_DISCRETE_3             = (1<<16) | 3,
    QT_CHANNEL_LABEL_DISCRETE_4             = (1<<16) | 4,
    QT_CHANNEL_LABEL_DISCRETE_5             = (1<<16) | 5,
    QT_CHANNEL_LABEL_DISCRETE_6             = (1<<16) | 6,
    QT_CHANNEL_LABEL_DISCRETE_7             = (1<<16) | 7,
    QT_CHANNEL_LABEL_DISCRETE_8             = (1<<16) | 8,
    QT_CHANNEL_LABEL_DISCRETE_9             = (1<<16) | 9,
    QT_CHANNEL_LABEL_DISCRETE_10            = (1<<16) | 10,
    QT_CHANNEL_LABEL_DISCRETE_11            = (1<<16) | 11,
    QT_CHANNEL_LABEL_DISCRETE_12            = (1<<16) | 12,
    QT_CHANNEL_LABEL_DISCRETE_13            = (1<<16) | 13,
    QT_CHANNEL_LABEL_DISCRETE_14            = (1<<16) | 14,
    QT_CHANNEL_LABEL_DISCRETE_15            = (1<<16) | 15,
    QT_CHANNEL_LABEL_DISCRETE_65535         = (1<<16) | 65535,
} lsmash_channel_label;

typedef enum
{
    QT_CHANNEL_BIT_LEFT                   = 1,
    QT_CHANNEL_BIT_RIGHT                  = 1<<1,
    QT_CHANNEL_BIT_CENTER                 = 1<<2,
    QT_CHANNEL_BIT_LFE_SCREEN             = 1<<3,
    QT_CHANNEL_BIT_LEFT_SURROUND          = 1<<4,       /* WAVE: "Back Left" */
    QT_CHANNEL_BIT_RIGHT_SURROUND         = 1<<5,       /* WAVE: "Back Right" */
    QT_CHANNEL_BIT_LEFT_CENTER            = 1<<6,
    QT_CHANNEL_BIT_RIGHT_CENTER           = 1<<7,
    QT_CHANNEL_BIT_CENTER_SURROUND        = 1<<8,       /* WAVE: "Back Center" */
    QT_CHANNEL_BIT_LEFT_SURROUND_DIRECT   = 1<<9,       /* WAVE: "Side Left" */
    QT_CHANNEL_BIT_RIGHT_SURROUND_DIRECT  = 1<<10,      /* WAVE: "Side Right" */
    QT_CHANNEL_BIT_TOP_CENTER_SURROUND    = 1<<11,
    QT_CHANNEL_BIT_VERTICAL_HEIGHT_LEFT   = 1<<12,      /* WAVE: "Top Front Left" */
    QT_CHANNEL_BIT_VERTICAL_HEIGHT_CENTER = 1<<13,      /* WAVE: "Top Front Center" */
    QT_CHANNEL_BIT_VERTICAL_HEIGHT_RIGHT  = 1<<14,      /* WAVE: "Top Front Right" */
    QT_CHANNEL_BIT_TOP_BACK_LEFT          = 1<<15,
    QT_CHANNEL_BIT_TOP_BACK_CENTER        = 1<<16,
    QT_CHANNEL_BIT_TOP_BACK_RIGHT         = 1<<17,
    QT_CHANNEL_BIT_FULL                   = 0x3ffff,
} lsmash_channel_bitmap;

typedef enum
{
    QT_CHANNEL_FLAGS_ALL_OFF                 = 0,
    QT_CHANNEL_FLAGS_RECTANGULAR_COORDINATES = 1,
    QT_CHANNEL_FLAGS_SPHERICAL_COORDINATES   = 1<<1,
    QT_CHANNEL_FLAGS_METERS                  = 1<<2,
} lsmash_channel_flags;

typedef enum
{
    /* indices for accessing the coordinates array in Channel Descriptions */
    /* for rectangulare coordinates */
    QT_CHANNEL_COORDINATES_LEFT_RIGHT = 0,      /* Negative is left and positive is right. */
    QT_CHANNEL_COORDINATES_BACK_FRONT = 1,      /* Negative is back and positive is front. */
    QT_CHANNEL_COORDINATES_DOWN_UP    = 2,      /* Negative is below ground level, 0 is ground level, and positive is above ground level. */
    /* for spherical coordinates */
    QT_CHANNEL_COORDINATES_AZIMUTH    = 0,      /* 0 is front center, positive is right, negative is left. This is measured in degrees. */
    QT_CHANNEL_COORDINATES_ELEVATION  = 1,      /* +90 is zenith, 0 is horizontal, -90 is nadir. This is measured in degrees. */
    QT_CHANNEL_COORDINATES_DISTANCE   = 2,      /* The units are described by flags. */
} lsmash_channel_coordinates_index;

typedef enum
{
    /* channel abbreviations:
     * L - left
     * R - right
     * C - center
     * Ls - left surround
     * Rs - right surround
     * Cs - center surround
     * Rls - rear left surround
     * Rrs - rear right surround
     * Lw - left wide
     * Rw - right wide
     * Lsd - left surround direct
     * Rsd - right surround direct
     * Lc - left center
     * Rc - right center
     * Ts - top surround
     * Vhl - vertical height left
     * Vhc - vertical height center
     * Vhr - vertical height right
     * Lt - left matrix total. for matrix encoded stereo.
     * Rt - right matrix total. for matrix encoded stereo. */

    /*  General layouts */
    QT_CHANNEL_LAYOUT_USE_CHANNEL_DESCRIPTIONS = 0,                 /* use the array of Channel Descriptions to define the mapping. */
    QT_CHANNEL_LAYOUT_USE_CHANNEL_BITMAP       = 1<<16,             /* use the bitmap to define the mapping. */

    QT_CHANNEL_LAYOUT_MONO                     = (100<<16) | 1,     /* a standard mono stream */
    QT_CHANNEL_LAYOUT_STEREO                   = (101<<16) | 2,     /* a standard stereo stream (L R) - implied playback */
    QT_CHANNEL_LAYOUT_STEREO_HEADPHONES        = (102<<16) | 2,     /* a standard stereo stream (L R) - implied headphone playback */
    QT_CHANNEL_LAYOUT_MATRIX_STEREO            = (103<<16) | 2,     /* a matrix encoded stereo stream (Lt, Rt) */
    QT_CHANNEL_LAYOUT_MID_SIDE                 = (104<<16) | 2,     /* mid/side recording */
    QT_CHANNEL_LAYOUT_XY                       = (105<<16) | 2,     /* coincident mic pair (often 2 figure 8's) */
    QT_CHANNEL_LAYOUT_BINAURAL                 = (106<<16) | 2,     /* binaural stereo (left, right) */
    QT_CHANNEL_LAYOUT_AMBISONIC_B_FORMAT       = (107<<16) | 4,     /* W, X, Y, Z */

    QT_CHANNEL_LAYOUT_QUADRAPHONIC             = (108<<16) | 4,     /* front left, front right, back left, back right */

    QT_CHANNEL_LAYOUT_PENTAGONAL               = (109<<16) | 5,     /* left, right, rear left, rear right, center */

    QT_CHANNEL_LAYOUT_HEXAGONAL                = (110<<16) | 6,     /* left, right, rear left, rear right, center, rear */

    QT_CHANNEL_LAYOUT_OCTAGONAL                = (111<<16) | 8,     /* front left, front right, rear left, rear right,
                                                                     * front center, rear center, side left, side right */

    QT_CHANNEL_LAYOUT_CUBE                     = (112<<16) | 8,     /* left, right, rear left, rear right,
                                                                     * top left, top right, top rear left, top rear right */

    /*  MPEG defined layouts */
    QT_CHANNEL_LAYOUT_MPEG_1_0                 = QT_CHANNEL_LAYOUT_MONO,            /* C */
    QT_CHANNEL_LAYOUT_MPEG_2_0                 = QT_CHANNEL_LAYOUT_STEREO,          /* L R */
    QT_CHANNEL_LAYOUT_MPEG_3_0_A               = (113<<16) | 3,                     /* L R C */
    QT_CHANNEL_LAYOUT_MPEG_3_0_B               = (114<<16) | 3,                     /* C L R */
    QT_CHANNEL_LAYOUT_MPEG_4_0_A               = (115<<16) | 4,                     /* L R C Cs */
    QT_CHANNEL_LAYOUT_MPEG_4_0_B               = (116<<16) | 4,                     /* C L R Cs */
    QT_CHANNEL_LAYOUT_MPEG_5_0_A               = (117<<16) | 5,                     /* L R C Ls Rs */
    QT_CHANNEL_LAYOUT_MPEG_5_0_B               = (118<<16) | 5,                     /* L R Ls Rs C */
    QT_CHANNEL_LAYOUT_MPEG_5_0_C               = (119<<16) | 5,                     /* L C R Ls Rs */
    QT_CHANNEL_LAYOUT_MPEG_5_0_D               = (120<<16) | 5,                     /* C L R Ls Rs */
    QT_CHANNEL_LAYOUT_MPEG_5_1_A               = (121<<16) | 6,                     /* L R C LFE Ls Rs */
    QT_CHANNEL_LAYOUT_MPEG_5_1_B               = (122<<16) | 6,                     /* L R Ls Rs C LFE */
    QT_CHANNEL_LAYOUT_MPEG_5_1_C               = (123<<16) | 6,                     /* L C R Ls Rs LFE */
    QT_CHANNEL_LAYOUT_MPEG_5_1_D               = (124<<16) | 6,                     /* C L R Ls Rs LFE */
    QT_CHANNEL_LAYOUT_MPEG_6_1_A               = (125<<16) | 7,                     /* L R C LFE Ls Rs Cs */
    QT_CHANNEL_LAYOUT_MPEG_7_1_A               = (126<<16) | 8,                     /* L R C LFE Ls Rs Lc Rc */
    QT_CHANNEL_LAYOUT_MPEG_7_1_B               = (127<<16) | 8,                     /* C Lc Rc L R Ls Rs LFE (doc: IS-13818-7 MPEG2-AAC Table 3.1) */
    QT_CHANNEL_LAYOUT_MPEG_7_1_C               = (128<<16) | 8,                     /* L R C LFE Ls Rs Rls Rrs */
    QT_CHANNEL_LAYOUT_EMAGIC_DEFAULT_7_1       = (129<<16) | 8,                     /* L R Ls Rs C LFE Lc Rc */
    QT_CHANNEL_LAYOUT_SMPTE_DTV                = (130<<16) | 8,                     /* L R C LFE Ls Rs Lt Rt */

    /*  ITU defined layouts */
    QT_CHANNEL_LAYOUT_ITU_1_0                  = QT_CHANNEL_LAYOUT_MONO,            /* C */
    QT_CHANNEL_LAYOUT_ITU_2_0                  = QT_CHANNEL_LAYOUT_STEREO,          /* L R */

    QT_CHANNEL_LAYOUT_ITU_2_1                  = (131<<16) | 3,                     /* L R Cs */
    QT_CHANNEL_LAYOUT_ITU_2_2                  = (132<<16) | 4,                     /* L R Ls Rs */
    QT_CHANNEL_LAYOUT_ITU_3_0                  = QT_CHANNEL_LAYOUT_MPEG_3_0_A,      /* L R C */
    QT_CHANNEL_LAYOUT_ITU_3_1                  = QT_CHANNEL_LAYOUT_MPEG_4_0_A,      /* L R C Cs */

    QT_CHANNEL_LAYOUT_ITU_3_2                  = QT_CHANNEL_LAYOUT_MPEG_5_0_A,      /* L R C Ls Rs */
    QT_CHANNEL_LAYOUT_ITU_3_2_1                = QT_CHANNEL_LAYOUT_MPEG_5_1_A,      /* L R C LFE Ls Rs */
    QT_CHANNEL_LAYOUT_ITU_3_4_1                = QT_CHANNEL_LAYOUT_MPEG_7_1_C,      /* L R C LFE Ls Rs Rls Rrs */

    /* DVD defined layouts */
    QT_CHANNEL_LAYOUT_DVD_0                    = QT_CHANNEL_LAYOUT_MONO,            /* C (mono) */
    QT_CHANNEL_LAYOUT_DVD_1                    = QT_CHANNEL_LAYOUT_STEREO,          /* L R */
    QT_CHANNEL_LAYOUT_DVD_2                    = QT_CHANNEL_LAYOUT_ITU_2_1,         /* L R Cs */
    QT_CHANNEL_LAYOUT_DVD_3                    = QT_CHANNEL_LAYOUT_ITU_2_2,         /* L R Ls Rs */
    QT_CHANNEL_LAYOUT_DVD_4                    = (133<<16) | 3,                     /* L R LFE */
    QT_CHANNEL_LAYOUT_DVD_5                    = (134<<16) | 4,                     /* L R LFE Cs */
    QT_CHANNEL_LAYOUT_DVD_6                    = (135<<16) | 5,                     /* L R LFE Ls Rs */
    QT_CHANNEL_LAYOUT_DVD_7                    = QT_CHANNEL_LAYOUT_MPEG_3_0_A,      /* L R C */
    QT_CHANNEL_LAYOUT_DVD_8                    = QT_CHANNEL_LAYOUT_MPEG_4_0_A,      /* L R C Cs */
    QT_CHANNEL_LAYOUT_DVD_9                    = QT_CHANNEL_LAYOUT_MPEG_5_0_A,      /* L R C Ls Rs */
    QT_CHANNEL_LAYOUT_DVD_10                   = (136<<16) | 4,                     /* L R C LFE */
    QT_CHANNEL_LAYOUT_DVD_11                   = (137<<16) | 5,                     /* L R C LFE Cs */
    QT_CHANNEL_LAYOUT_DVD_12                   = QT_CHANNEL_LAYOUT_MPEG_5_1_A,      /* L R C LFE Ls Rs */
    /* 13 through 17 are duplicates of 8 through 12. */
    QT_CHANNEL_LAYOUT_DVD_13                   = QT_CHANNEL_LAYOUT_DVD_8,           /* L R C Cs */
    QT_CHANNEL_LAYOUT_DVD_14                   = QT_CHANNEL_LAYOUT_DVD_9,           /* L R C Ls Rs */
    QT_CHANNEL_LAYOUT_DVD_15                   = QT_CHANNEL_LAYOUT_DVD_10,          /* L R C LFE */
    QT_CHANNEL_LAYOUT_DVD_16                   = QT_CHANNEL_LAYOUT_DVD_11,          /* L R C LFE Cs */
    QT_CHANNEL_LAYOUT_DVD_17                   = QT_CHANNEL_LAYOUT_DVD_12,          /* L R C LFE Ls Rs */
    QT_CHANNEL_LAYOUT_DVD_18                   = (138<<16) | 5,                     /* L R Ls Rs LFE */
    QT_CHANNEL_LAYOUT_DVD_19                   = QT_CHANNEL_LAYOUT_MPEG_5_0_B,      /* L R Ls Rs C */
    QT_CHANNEL_LAYOUT_DVD_20                   = QT_CHANNEL_LAYOUT_MPEG_5_1_B,      /* L R Ls Rs C LFE */

    /* These are the symmetrical layouts. */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_4             = QT_CHANNEL_LAYOUT_QUADRAPHONIC,
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_5             = QT_CHANNEL_LAYOUT_PENTAGONAL,
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_6             = QT_CHANNEL_LAYOUT_HEXAGONAL,
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_8             = QT_CHANNEL_LAYOUT_OCTAGONAL,
    /* These are the surround-based layouts. */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_5_0           = QT_CHANNEL_LAYOUT_MPEG_5_0_B,      /* L R Ls Rs C */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_6_0           = (139<<16) | 6,                     /* L R Ls Rs C Cs */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_7_0           = (140<<16) | 7,                     /* L R Ls Rs C Rls Rrs */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_7_0_FRONT     = (148<<16) | 7,                     /* L R Ls Rs C Lc Rc */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_5_1           = QT_CHANNEL_LAYOUT_MPEG_5_1_A,      /* L R C LFE Ls Rs */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_6_1           = QT_CHANNEL_LAYOUT_MPEG_6_1_A,      /* L R C LFE Ls Rs Cs */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_7_1           = QT_CHANNEL_LAYOUT_MPEG_7_1_C,      /* L R C LFE Ls Rs Rls Rrs */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_7_1_FRONT     = QT_CHANNEL_LAYOUT_MPEG_7_1_A,      /* L R C LFE Ls Rs Lc Rc */

    QT_CHANNEL_LAYOUT_AAC_3_0                  = QT_CHANNEL_LAYOUT_MPEG_3_0_B,      /* C L R */
    QT_CHANNEL_LAYOUT_AAC_QUADRAPHONIC         = QT_CHANNEL_LAYOUT_QUADRAPHONIC,    /* L R Ls Rs */
    QT_CHANNEL_LAYOUT_AAC_4_0                  = QT_CHANNEL_LAYOUT_MPEG_4_0_B,      /* C L R Cs */
    QT_CHANNEL_LAYOUT_AAC_5_0                  = QT_CHANNEL_LAYOUT_MPEG_5_0_D,      /* C L R Ls Rs */
    QT_CHANNEL_LAYOUT_AAC_5_1                  = QT_CHANNEL_LAYOUT_MPEG_5_1_D,      /* C L R Ls Rs LFE */
    QT_CHANNEL_LAYOUT_AAC_6_0                  = (141<<16) | 6,                     /* C L R Ls Rs Cs */
    QT_CHANNEL_LAYOUT_AAC_6_1                  = (142<<16) | 7,                     /* C L R Ls Rs Cs LFE */
    QT_CHANNEL_LAYOUT_AAC_7_0                  = (143<<16) | 7,                     /* C L R Ls Rs Rls Rrs */
    QT_CHANNEL_LAYOUT_AAC_7_1                  = QT_CHANNEL_LAYOUT_MPEG_7_1_B,      /* C Lc Rc L R Ls Rs LFE */
    QT_CHANNEL_LAYOUT_AAC_OCTAGONAL            = (144<<16) | 8,                     /* C L R Ls Rs Rls Rrs Cs */

    QT_CHANNEL_LAYOUT_TMH_10_2_STD             = (145<<16) | 16,                    /* L R C Vhc Lsd Rsd Ls Rs Vhl Vhr Lw Rw Csd Cs LFE1 LFE2 */
    QT_CHANNEL_LAYOUT_TMH_10_2_FULL            = (146<<16) | 21,                    /* TMH_10_2_std plus: Lc Rc HI VI Haptic */

    QT_CHANNEL_LAYOUT_AC3_1_0_1                = (149<<16) | 2,                     /* C LFE */
    QT_CHANNEL_LAYOUT_AC3_3_0                  = (150<<16) | 3,                     /* L C R */
    QT_CHANNEL_LAYOUT_AC3_3_1                  = (151<<16) | 4,                     /* L C R Cs */
    QT_CHANNEL_LAYOUT_AC3_3_0_1                = (152<<16) | 4,                     /* L C R LFE */
    QT_CHANNEL_LAYOUT_AC3_2_1_1                = (153<<16) | 4,                     /* L R Cs LFE */
    QT_CHANNEL_LAYOUT_AC3_3_1_1                = (154<<16) | 5,                     /* L C R Cs LFE */

    QT_CHANNEL_LAYOUT_EAC_6_0_A                = (155<<16) | 6,                     /* L C R Ls Rs Cs */
    QT_CHANNEL_LAYOUT_EAC_7_0_A                = (156<<16) | 7,                     /* L C R Ls Rs Rls Rrs */

    QT_CHANNEL_LAYOUT_EAC3_6_1_A               = (157<<16) | 7,                     /* L C R Ls Rs LFE Cs */
    QT_CHANNEL_LAYOUT_EAC3_6_1_B               = (158<<16) | 7,                     /* L C R Ls Rs LFE Ts */
    QT_CHANNEL_LAYOUT_EAC3_6_1_C               = (159<<16) | 7,                     /* L C R Ls Rs LFE Vhc */
    QT_CHANNEL_LAYOUT_EAC3_7_1_A               = (160<<16) | 8,                     /* L C R Ls Rs LFE Rls Rrs */
    QT_CHANNEL_LAYOUT_EAC3_7_1_B               = (161<<16) | 8,                     /* L C R Ls Rs LFE Lc Rc */
    QT_CHANNEL_LAYOUT_EAC3_7_1_C               = (162<<16) | 8,                     /* L C R Ls Rs LFE Lsd Rsd */
    QT_CHANNEL_LAYOUT_EAC3_7_1_D               = (163<<16) | 8,                     /* L C R Ls Rs LFE Lw Rw */
    QT_CHANNEL_LAYOUT_EAC3_7_1_E               = (164<<16) | 8,                     /* L C R Ls Rs LFE Vhl Vhr */

    QT_CHANNEL_LAYOUT_EAC3_7_1_F               = (165<<16) | 8,                     /* L C R Ls Rs LFE Cs Ts */
    QT_CHANNEL_LAYOUT_EAC3_7_1_G               = (166<<16) | 8,                     /* L C R Ls Rs LFE Cs Vhc */
    QT_CHANNEL_LAYOUT_EAC3_7_1_H               = (167<<16) | 8,                     /* L C R Ls Rs LFE Ts Vhc */

    QT_CHANNEL_LAYOUT_DTS_3_1                  = (168<<16) | 4,                     /* C L R LFE */
    QT_CHANNEL_LAYOUT_DTS_4_1                  = (169<<16) | 5,                     /* C L R Cs LFE */
    QT_CHANNEL_LAYOUT_DTS_6_0_A                = (170<<16) | 6,                     /* Lc Rc L R Ls Rs */
    QT_CHANNEL_LAYOUT_DTS_6_0_B                = (171<<16) | 6,                     /* C L R Rls Rrs Ts */
    QT_CHANNEL_LAYOUT_DTS_6_0_C                = (172<<16) | 6,                     /* C Cs L R Rls Rrs */
    QT_CHANNEL_LAYOUT_DTS_6_1_A                = (173<<16) | 7,                     /* Lc Rc L R Ls Rs LFE */
    QT_CHANNEL_LAYOUT_DTS_6_1_B                = (174<<16) | 7,                     /* C L R Rls Rrs Ts LFE */
    QT_CHANNEL_LAYOUT_DTS_6_1_C                = (175<<16) | 7,                     /* C Cs L R Rls Rrs LFE */
    QT_CHANNEL_LAYOUT_DTS_7_0                  = (176<<16) | 7,                     /* Lc C Rc L R Ls Rs */
    QT_CHANNEL_LAYOUT_DTS_7_1                  = (177<<16) | 8,                     /* Lc C Rc L R Ls Rs LFE */
    QT_CHANNEL_LAYOUT_DTS_8_0_A                = (178<<16) | 8,                     /* Lc Rc L R Ls Rs Rls Rrs */
    QT_CHANNEL_LAYOUT_DTS_8_0_B                = (179<<16) | 8,                     /* Lc C Rc L R Ls Cs Rs */
    QT_CHANNEL_LAYOUT_DTS_8_1_A                = (180<<16) | 9,                     /* Lc Rc L R Ls Rs Rls Rrs LFE */
    QT_CHANNEL_LAYOUT_DTS_8_1_B                = (181<<16) | 9,                     /* Lc C Rc L R Ls Cs Rs LFE */
    QT_CHANNEL_LAYOUT_DTS_6_1_D                = (182<<16) | 7,                     /* C L R Ls Rs LFE Cs */

    QT_CHANNEL_LAYOUT_ALAC_MONO                = QT_CHANNEL_LAYOUT_MONO,            /* C */
    QT_CHANNEL_LAYOUT_ALAC_STEREO              = QT_CHANNEL_LAYOUT_STEREO,          /* L R */
    QT_CHANNEL_LAYOUT_ALAC_3_0                 = QT_CHANNEL_LAYOUT_MPEG_3_0_B,      /* C L R */
    QT_CHANNEL_LAYOUT_ALAC_4_0                 = QT_CHANNEL_LAYOUT_MPEG_4_0_B,      /* C L R Cs */
    QT_CHANNEL_LAYOUT_ALAC_5_0                 = QT_CHANNEL_LAYOUT_MPEG_5_0_D,      /* C L R Ls Rs */
    QT_CHANNEL_LAYOUT_ALAC_5_1                 = QT_CHANNEL_LAYOUT_MPEG_5_1_D,      /* C L R Ls Rs LFE */
    QT_CHANNEL_LAYOUT_ALAC_6_1                 = QT_CHANNEL_LAYOUT_AAC_6_1,         /* C L R Ls Rs Cs LFE */
    QT_CHANNEL_LAYOUT_ALAC_7_1                 = QT_CHANNEL_LAYOUT_MPEG_7_1_B,      /* C Lc Rc L R Ls Rs LFE */

    QT_CHANNEL_LAYOUT_DISCRETE_IN_ORDER        = 147<<16,                           /* needs to be ORed with the actual number of channels */  
    QT_CHANNEL_LAYOUT_UNKNOWN                  = 0xffff0000,                        /* needs to be ORed with the actual number of channels */
} lsmash_channel_layout_tag;

typedef enum
{
    /* In MP4 and/or ISO base media file format, if in a presentation all tracks have neither track_in_movie nor track_in_preview set,
     * then all tracks shall be treated as if both flags were set on all tracks. */
    ISOM_TRACK_ENABLED      = 0x000001,     /* Track_enabled: Indicates that the track is enabled.
                                             * A disabled track is treated as if it were not present. */
    ISOM_TRACK_IN_MOVIE     = 0x000002,     /* Track_in_movie: Indicates that the track is used in the presentation. */
    ISOM_TRACK_IN_PREVIEW   = 0x000004,     /* Track_in_preview: Indicates that the track is used when previewing the presentation. */

    QT_TRACK_IN_POSTER      = 0x000008,     /* Track_in_poster: Indicates that the track is used in the movie's poster. (only defined in QuickTime file format) */
} lsmash_track_mode;

typedef enum
{
    ISOM_SCALING_METHOD_FILL    = 1,
    ISOM_SCALING_METHOD_HIDDEN  = 2,
    ISOM_SCALING_METHOD_MEET    = 3,
    ISOM_SCALING_METHOD_SLICE_X = 4,
    ISOM_SCALING_METHOD_SLICE_Y = 5,
} lsmash_scaling_method;

typedef enum
{
    ISOM_EDIT_MODE_NORMAL   = 1<<16,
    ISOM_EDIT_MODE_DWELL    = 0,
    ISOM_EDIT_MODE_EMPTY    = -1,
} lsmash_edit_mode;

typedef enum
{
    /* allow_ealier */
    QT_SAMPLE_EARLIER_PTS_ALLOWED       = 1,
    /* leading */
    ISOM_SAMPLE_LEADING_UNKNOWN         = 0,
    ISOM_SAMPLE_IS_UNDECODABLE_LEADING  = 1,
    ISOM_SAMPLE_IS_NOT_LEADING          = 2,
    ISOM_SAMPLE_IS_DECODABLE_LEADING    = 3,
    /* independent */
    ISOM_SAMPLE_INDEPENDENCY_UNKNOWN    = 0,
    ISOM_SAMPLE_IS_NOT_INDEPENDENT      = 1,
    ISOM_SAMPLE_IS_INDEPENDENT          = 2,
    /* disposable */
    ISOM_SAMPLE_DISPOSABLE_UNKNOWN      = 0,
    ISOM_SAMPLE_IS_NOT_DISPOSABLE       = 1,
    ISOM_SAMPLE_IS_DISPOSABLE           = 2,
    /* redundant */
    ISOM_SAMPLE_REDUNDANCY_UNKNOWN      = 0,
    ISOM_SAMPLE_HAS_REDUNDANCY          = 1,
    ISOM_SAMPLE_HAS_NO_REDUNDANCY       = 2,
} lsmash_sample_dependency;

/* objectTypeIndication */
typedef enum {
    MP4SYS_OBJECT_TYPE_Forbidden                          = 0x00,   /* Forbidden */
    MP4SYS_OBJECT_TYPE_Systems_ISO_14496_1                = 0x01,   /* Systems ISO/IEC 14496-1 */
    /* For all 14496-1 streams unless specifically indicated to the contrary.
       Scene Description scenes, which are identified with StreamType=0x03, using
       this object type value shall use the BIFSConfig. */
    MP4SYS_OBJECT_TYPE_Systems_ISO_14496_1_BIFSv2         = 0x02,   /* Systems ISO/IEC 14496-1 */
    /* This object type shall be used, with StreamType=0x03, for Scene
       Description streams that use the BIFSv2Config.
       Its use with other StreamTypes is reserved. */

    MP4SYS_OBJECT_TYPE_Interaction_Stream                 = 0x03,   /* Interaction Stream */
    MP4SYS_OBJECT_TYPE_Extended_BIFS                      = 0x04,   /* Extended BIFS */
    /* Used, with StreamType=0x03, for Scene Description streams that use the BIFSConfigEx; its use with
       other StreamTypes is reserved. (Was previously reserved for MUCommandStream but not used for that purpose.) */
    MP4SYS_OBJECT_TYPE_AFX_Stream                         = 0x05,   /* AFX Stream */
    /* Used, with StreamType=0x03, for Scene Description streams that use the AFXConfig;
       its use with other StreamTypes is reserved. */
    MP4SYS_OBJECT_TYPE_Font_Data_Stream                   = 0x06,   /* Font Data Stream */
    MP4SYS_OBJECT_TYPE_Synthetised_Texture                = 0x07,   /* Synthetised Texture */
    MP4SYS_OBJECT_TYPE_Text_Stream                        = 0x08,   /* Text Stream */

    MP4SYS_OBJECT_TYPE_Visual_ISO_14496_2                 = 0x20,   /* Visual ISO/IEC 14496-2 */
    MP4SYS_OBJECT_TYPE_Visual_H264_ISO_14496_10           = 0x21,   /* Visual ITU-T Recommendation H.264 | ISO/IEC 14496-10 */
    /* The actual object types are within the DecoderSpecificInfo and defined in H.264 | 14496-10. */
    MP4SYS_OBJECT_TYPE_Parameter_Sets_H_264_ISO_14496_10  = 0x22,   /* Parameter Sets for ITU-T Recommendation H.264 | ISO/IEC 14496-10 */
    /* The actual object types are within the DecoderSpecificInfo and defined in 14496-2. */

    MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3                  = 0x40,   /* Audio ISO/IEC 14496-3 (MPEG-4 Audio) */
    //MP4SYS_OBJECT_TYPE_MP4A_AUDIO = 0x40,
    /* The actual object types are defined in 14496-3 and are in the DecoderSpecificInfo as specified in 14496-3. */

    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_Simple_Profile  = 0x60,   /* Visual ISO/IEC 13818-2 Simple Profile (MPEG-2 Video) */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_Main_Profile    = 0x61,   /* Visual ISO/IEC 13818-2 Main Profile */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_SNR_Profile     = 0x62,   /* Visual ISO/IEC 13818-2 SNR Profile */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_Spatial_Profile = 0x63,   /* Visual ISO/IEC 13818-2 Spatial Profile */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_High_Profile    = 0x64,   /* Visual ISO/IEC 13818-2 High Profile */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_422_Profile     = 0x65,   /* Visual ISO/IEC 13818-2 422 Profile */
    MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_Main_Profile     = 0x66,   /* Audio ISO/IEC 13818-7 Main Profile (MPEG-2 Audio)(AAC) */
    MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_LC_Profile       = 0x67,   /* Audio ISO/IEC 13818-7 LowComplexity Profile */
    MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_SSR_Profile      = 0x68,   /* Audio ISO/IEC 13818-7 Scaleable Sampling Rate Profile */
    /* For streams kinda 13818-7 the decoder specific information consists of the ADIF header if present
       (or none if not present) and an access unit is a "raw_data_block()" as defined in 13818-7. */
    MP4SYS_OBJECT_TYPE_Audio_ISO_13818_3                  = 0x69,   /* Audio ISO/IEC 13818-3 (MPEG-2 BC-Audio)(redefined MPEG-1 Audio in MPEG-2) */
    /* For streams kinda 13818-3 the decoder specific information is empty since all necessary data is in the bitstream frames itself.
       The access units in this case are the "frame()" bitstream element as is defined in 11172-3. */
    MP4SYS_OBJECT_TYPE_Visual_ISO_11172_2                 = 0x6A,   /* Visual ISO/IEC 11172-2 (MPEG-1 Video) */
    MP4SYS_OBJECT_TYPE_Audio_ISO_11172_3                  = 0x6B,   /* Audio ISO/IEC 11172-3 (MPEG-1 Audio) */
    MP4SYS_OBJECT_TYPE_Visual_ISO_10918_1                 = 0x6C,   /* Visual ISO/IEC 10918-1 (JPEG) */
    MP4SYS_OBJECT_TYPE_PNG                                = 0x6D,   /* Portable Network Graphics */
    MP4SYS_OBJECT_TYPE_Visual_ISO_15444_1_JPEG2000        = 0x6E,   /* Visual ISO/IEC 15444-1 (JPEG 2000) */

    /* FIXME: rename these symbols to be explaining, rather than based on four cc */
    MP4SYS_OBJECT_TYPE_EVRC_AUDIO                         = 0xA0,   /* EVRC Voice */
    MP4SYS_OBJECT_TYPE_SSMV_AUDIO                         = 0xA1,   /* SMV Voice */
    MP4SYS_OBJECT_TYPE_3GPP2_CMF                          = 0xA2,   /* 3GPP2 Compact Multimedia Format (CMF) */
    MP4SYS_OBJECT_TYPE_VC_1_VIDEO                         = 0xA3,   /* SMPTE VC-1 Video */
    MP4SYS_OBJECT_TYPE_DRAC_VIDEO                         = 0xA4,   /* Dirac Video Coder */
    MP4SYS_OBJECT_TYPE_AC_3_AUDIO                         = 0xA5,   /* AC-3 Audio */
    MP4SYS_OBJECT_TYPE_EC_3_AUDIO                         = 0xA6,   /* Enhanced AC-3 audio */
    MP4SYS_OBJECT_TYPE_DRA1_AUDIO                         = 0xA7,   /* DRA Audio */
    MP4SYS_OBJECT_TYPE_G719_AUDIO                         = 0xA8,   /* ITU G.719 Audio */
    MP4SYS_OBJECT_TYPE_DTSC_AUDIO                         = 0xA9,   /* DTS Coherent Acoustics audio */
    MP4SYS_OBJECT_TYPE_DTSH_AUDIO                         = 0xAA,   /* DTS-HD High Resolution Audio */
    MP4SYS_OBJECT_TYPE_DTSL_AUDIO                         = 0xAB,   /* DTS-HD Master Audio */
    MP4SYS_OBJECT_TYPE_DTSE_AUDIO                         = 0xAC,   /* DTS Express low bit rate audio, also known as DTS LBR */

    MP4SYS_OBJECT_TYPE_SQCP_AUDIO                         = 0xE1,   /* 13K Voice */

    MP4SYS_OBJECT_TYPE_NONE                               = 0xFF,   /* no object type specified */
    /* Streams with this value with a StreamType indicating a systems stream (values 1,2,3,6,7,8,9)
       shall be treated as if the ObjectTypeIndication had been set to 0x01. */
} lsmash_mp4sys_object_type_indication;

/* streamType */
typedef enum {
    MP4SYS_STREAM_TYPE_Forbidden               = 0x00,  /* Forbidden */
    MP4SYS_STREAM_TYPE_ObjectDescriptorStream  = 0x01,  /* ObjectDescriptorStream */
    MP4SYS_STREAM_TYPE_ClockReferenceStream    = 0x02,  /* ClockReferenceStream */
    MP4SYS_STREAM_TYPE_SceneDescriptionStream  = 0x03,  /* SceneDescriptionStream */
    MP4SYS_STREAM_TYPE_VisualStream            = 0x04,  /* VisualStream */
    MP4SYS_STREAM_TYPE_AudioStream             = 0x05,  /* AudioStream */
    MP4SYS_STREAM_TYPE_MPEG7Stream             = 0x06,  /* MPEG7Stream */
    MP4SYS_STREAM_TYPE_IPMPStream              = 0x07,  /* IPMPStream */
    MP4SYS_STREAM_TYPE_ObjectContentInfoStream = 0x08,  /* ObjectContentInfoStream */
    MP4SYS_STREAM_TYPE_MPEGJStream             = 0x09,  /* MPEGJStream */
    MP4SYS_STREAM_TYPE_InteractionStream       = 0x0A,  /* Interaction Stream */
    MP4SYS_STREAM_TYPE_IPMPToolStream          = 0x0B,  /* IPMPToolStream */
    MP4SYS_STREAM_TYPE_FontDataStream          = 0x0C,  /* FontDataStream */
    MP4SYS_STREAM_TYPE_StreamingText           = 0x0D,  /* StreamingText */
} lsmash_mp4sys_stream_type;

/* Audio Object Types */
typedef enum {
    MP4A_AUDIO_OBJECT_TYPE_NULL                           = 0,
    MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN                       = 1,  /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_AAC_LC                         = 2,  /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_AAC_SSR                        = 3,  /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_AAC_LTP                        = 4,  /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_SBR                            = 5,  /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_AAC_scalable                   = 6,  /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_TwinVQ                         = 7,  /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_CELP                           = 8,  /* ISO/IEC 14496-3 subpart 3 */
    MP4A_AUDIO_OBJECT_TYPE_HVXC                           = 9,  /* ISO/IEC 14496-3 subpart 2 */
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
    MP4A_AUDIO_OBJECT_TYPE_PS                             = 29, /* ISO/IEC 14496-3 subpart 8 */
    MP4A_AUDIO_OBJECT_TYPE_MPEG_Surround                  = 30, /* ISO/IEC 23003-1 */
    MP4A_AUDIO_OBJECT_TYPE_ESCAPE                         = 31,
    MP4A_AUDIO_OBJECT_TYPE_Layer_1                        = 32, /* ISO/IEC 14496-3 subpart 9 */
    MP4A_AUDIO_OBJECT_TYPE_Layer_2                        = 33, /* ISO/IEC 14496-3 subpart 9 */
    MP4A_AUDIO_OBJECT_TYPE_Layer_3                        = 34, /* ISO/IEC 14496-3 subpart 9 */
    MP4A_AUDIO_OBJECT_TYPE_DST                            = 35, /* ISO/IEC 14496-3 subpart 10 */
    MP4A_AUDIO_OBJECT_TYPE_ALS                            = 36, /* ISO/IEC 14496-3 subpart 11 */
    MP4A_AUDIO_OBJECT_TYPE_SLS                            = 37, /* ISO/IEC 14496-3 subpart 12 */
    MP4A_AUDIO_OBJECT_TYPE_SLS_non_core                   = 38, /* ISO/IEC 14496-3 subpart 12 */
    MP4A_AUDIO_OBJECT_TYPE_ER_AAC_ELD                     = 39, /* ISO/IEC 14496-3 subpart 4 */
    MP4A_AUDIO_OBJECT_TYPE_SMR_Simple                     = 40, /* ISO/IEC 14496-23 */
    MP4A_AUDIO_OBJECT_TYPE_SMR_Main                       = 41, /* ISO/IEC 14496-23 */
    MP4A_AUDIO_OBJECT_TYPE_SAOC                           = 43, /* ISO/IEC 23003-2 */
} lsmash_mp4a_AudioObjectType;

/* see ISO/IEC 14496-3 Signaling of SBR, SBR Signaling and Corresponding Decoder Behavior */
typedef enum {
    MP4A_AAC_SBR_NOT_SPECIFIED = 0x0,   /* not mention to SBR presence. Implicit signaling. */
    MP4A_AAC_SBR_NONE,                  /* explicitly signals SBR does not present. Useless in general. */
    MP4A_AAC_SBR_BACKWARD_COMPATIBLE,   /* explicitly signals SBR present. Recommended method to signal SBR. */
    MP4A_AAC_SBR_HIERARCHICAL           /* SBR exists. SBR dedicated method. */
} lsmash_mp4a_aac_sbr_mode;

typedef enum
{
    ISOM_SAMPLE_RANDOM_ACCESS_TYPE_NONE         = 0,        /* not random access point */
    ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC         = 1,        /* sync sample */
    ISOM_SAMPLE_RANDOM_ACCESS_TYPE_CLOSED_RAP   = 1,        /* the first sample of a closed GOP */
    ISOM_SAMPLE_RANDOM_ACCESS_TYPE_OPEN_RAP     = 2,        /* the first sample of an open GOP */
    ISOM_SAMPLE_RANDOM_ACCESS_TYPE_UNKNOWN_RAP  = 3,        /* the first sample of an open or closed GOP */
    ISOM_SAMPLE_RANDOM_ACCESS_TYPE_RECOVERY     = 4,        /* starting point of gradual decoder refresh */

    QT_SAMPLE_RANDOM_ACCESS_TYPE_NONE           = 0,        /* not random access point */
    QT_SAMPLE_RANDOM_ACCESS_TYPE_SYNC           = 1,        /* sync sample */
    QT_SAMPLE_RANDOM_ACCESS_TYPE_PARTIAL_SYNC   = 2,        /* partial sync sample */
    QT_SAMPLE_RANDOM_ACCESS_TYPE_CLOSED_RAP     = 1,        /* the first sample of a closed GOP */
    QT_SAMPLE_RANDOM_ACCESS_TYPE_OPEN_RAP       = 2,        /* the first sample of an open GOP */
} lsmash_random_access_type;

typedef enum
{
    /* UTF String type */
    ITUNES_METADATA_TYPE_ALBUM_NAME                 = LSMASH_4CC( 0xA9, 'a', 'l', 'b' ),    /* Album Name */
    ITUNES_METADATA_TYPE_ARTIST                     = LSMASH_4CC( 0xA9, 'A', 'R', 'T' ),    /* Artist */
    ITUNES_METADATA_TYPE_USER_COMMENT               = LSMASH_4CC( 0xA9, 'c', 'm', 't' ),    /* User Comment */
    ITUNES_METADATA_TYPE_RELEASE_DATE               = LSMASH_4CC( 0xA9, 'd', 'a', 'y' ),    /* YYYY-MM-DD format string (may be incomplete, i.e. only year) */
    ITUNES_METADATA_TYPE_ENCODED_BY                 = LSMASH_4CC( 0xA9, 'e', 'n', 'c' ),    /* Person or company that encoded the recording */
    ITUNES_METADATA_TYPE_USER_GENRE                 = LSMASH_4CC( 0xA9, 'g', 'e', 'n' ),    /* User Genre user-specified string */
    ITUNES_METADATA_TYPE_0XA9_GROUPING              = LSMASH_4CC( 0xA9, 'g', 'r', 'p' ),    /* Grouping */
    ITUNES_METADATA_TYPE_LYRICS                     = LSMASH_4CC( 0xA9, 'l', 'y', 'r' ),    /* Lyrics */
    ITUNES_METADATA_TYPE_TITLE                      = LSMASH_4CC( 0xA9, 'n', 'a', 'm' ),    /* Title / Song Name */
    ITUNES_METADATA_TYPE_TRACK_SUBTITLE             = LSMASH_4CC( 0xA9, 's', 't', '3' ),    /* Track Sub-Title */
    ITUNES_METADATA_TYPE_ENCODING_TOOL              = LSMASH_4CC( 0xA9, 't', 'o', 'o' ),    /* Software which encoded the recording */
    ITUNES_METADATA_TYPE_COMPOSER                   = LSMASH_4CC( 0xA9, 'w', 'r', 't' ),    /* Composer */
    ITUNES_METADATA_TYPE_ALBUM_ARTIST               = LSMASH_4CC( 'a', 'A', 'R', 'T' ),     /* Artist for the whole album (if different than the individual tracks) */
    ITUNES_METADATA_TYPE_PODCAST_CATEGORY           = LSMASH_4CC( 'c', 'a', 't', 'g' ),     /* Podcast Category */
    ITUNES_METADATA_TYPE_COPYRIGHT                  = LSMASH_4CC( 'c', 'p', 'r', 't' ),     /* Copyright */
    ITUNES_METADATA_TYPE_DESCRIPTION                = LSMASH_4CC( 'd', 'e', 's', 'c' ),     /* Description (limited to 255 bytes) */
    ITUNES_METADATA_TYPE_GROUPING                   = LSMASH_4CC( 'g', 'r', 'u', 'p' ),     /* Grouping */
    ITUNES_METADATA_TYPE_PODCAST_KEYWORD            = LSMASH_4CC( 'k', 'e', 'y', 'w' ),     /* Podcast Keywords */
    ITUNES_METADATA_TYPE_LONG_DESCRIPTION           = LSMASH_4CC( 'l', 'd', 'e', 's' ),     /* Long Description */
    ITUNES_METADATA_TYPE_PURCHASE_DATE              = LSMASH_4CC( 'p', 'u', 'r', 'd' ),     /* Purchase Date */
    ITUNES_METADATA_TYPE_TV_EPISODE_ID              = LSMASH_4CC( 't', 'v', 'e', 'n' ),     /* TV Episode ID */
    ITUNES_METADATA_TYPE_TV_NETWORK                 = LSMASH_4CC( 't', 'v', 'n', 'n' ),     /* TV Network Name */
    ITUNES_METADATA_TYPE_TV_SHOW_NAME               = LSMASH_4CC( 't', 'v', 's', 'h' ),     /* TV Show Name */
    ITUNES_METADATA_TYPE_ITUNES_PURCHASE_ACCOUNT_ID = LSMASH_4CC( 'a', 'p', 'I', 'D' ),     /* iTunes Account Used for Purchase */

    /* Integer type
     * (X): X means length of bytes */
    ITUNES_METADATA_TYPE_EPISODE_GLOBAL_ID          = LSMASH_4CC( 'e', 'g', 'i', 'd' ),     /* (1) Episode Global Unique ID */
    ITUNES_METADATA_TYPE_PREDEFINED_GENRE           = LSMASH_4CC( 'g', 'n', 'r', 'e' ),     /* (4) Pre-defined Genre / Enumerated value from ID3 tag set, plus 1 */
    ITUNES_METADATA_TYPE_PODCAST_URL                = LSMASH_4CC( 'p', 'u', 'r', 'l' ),     /* (?) Podcast URL */
    ITUNES_METADATA_TYPE_CONTENT_RATING             = LSMASH_4CC( 'r', 't', 'n', 'g' ),     /* (1) Content Rating / Does song have explicit content? 0: none, 2: clean, 4: explicit */
    ITUNES_METADATA_TYPE_MEDIA_TYPE                 = LSMASH_4CC( 's', 't', 'i', 'k' ),     /* (1) Media Type */
    ITUNES_METADATA_TYPE_BEATS_PER_MINUTE           = LSMASH_4CC( 't', 'm', 'p', 'o' ),     /* (2) Beats Per Minute */
    ITUNES_METADATA_TYPE_TV_EPISODE                 = LSMASH_4CC( 't', 'v', 'e', 's' ),     /* (4) TV Episode */
    ITUNES_METADATA_TYPE_TV_SEASON                  = LSMASH_4CC( 't', 'v', 's', 'n' ),     /* (4) TV Season */
    ITUNES_METADATA_TYPE_ITUNES_ACCOUNT_TYPE        = LSMASH_4CC( 'a', 'k', 'I', 'D' ),     /* (1) iTunes Account Type / 0: iTunes, 1: AOL */
    ITUNES_METADATA_TYPE_ITUNES_ARTIST_ID           = LSMASH_4CC( 'a', 't', 'I', 'D' ),     /* (4) iTunes Artist ID */
    ITUNES_METADATA_TYPE_ITUNES_COMPOSER_ID         = LSMASH_4CC( 'c', 'm', 'I', 'D' ),     /* (4) iTunes Composer ID */
    ITUNES_METADATA_TYPE_ITUNES_CATALOG_ID          = LSMASH_4CC( 'c', 'n', 'I', 'D' ),     /* (4) iTunes Catalog ID */
    ITUNES_METADATA_TYPE_ITUNES_TV_GENRE_ID         = LSMASH_4CC( 'g', 'e', 'I', 'D' ),     /* (4) iTunes TV Genre ID */
    ITUNES_METADATA_TYPE_ITUNES_PLAYLIST_ID         = LSMASH_4CC( 'p', 'l', 'I', 'D' ),     /* (8) iTunes Playlist ID */
    ITUNES_METADATA_TYPE_ITUNES_COUNTRY_CODE        = LSMASH_4CC( 's', 'f', 'I', 'D' ),     /* (4) iTunes Country Code */

    /* Boolean type */
    ITUNES_METADATA_TYPE_DISC_COMPILATION           = LSMASH_4CC( 'c', 'p', 'i', 'l' ),     /* Disc Compilation / Is disc part of a compilation? 0: No, 1: Yes */
    ITUNES_METADATA_TYPE_HIGH_DEFINITION_VIDEO      = LSMASH_4CC( 'h', 'd', 'v', 'd' ),     /* High Definition Video / 0: No, 1: Yes */
    ITUNES_METADATA_TYPE_PODCAST                    = LSMASH_4CC( 'p', 'c', 's', 't' ),     /* Podcast / 0: No, 1: Yes */
    ITUNES_METADATA_TYPE_GAPLESS_PLAYBACK           = LSMASH_4CC( 'p', 'g', 'a', 'p' ),     /* Gapless Playback / 0: insert gap, 1: no gap */

    /* Binary type */
    ITUNES_METADATA_TYPE_COVER_ART                  = LSMASH_4CC( 'c', 'o', 'v', 'r' ),     /* One or more cover art images */
    ITUNES_METADATA_TYPE_DISC_NUMBER                = LSMASH_4CC( 'd', 'i', 's', 'k' ),     /* Disc Number */
    ITUNES_METADATA_TYPE_TRACH_NUMBER               = LSMASH_4CC( 't', 'r', 'k', 'n' ),     /* Track Number */

    /* Custom type */
    ITUNES_METADATA_TYPE_CUSTOM                     = LSMASH_4CC( '-', '-', '-', '-' ),     /* Custom */
} lsmash_itunes_metadata_type;

/* public data types */
typedef struct
{
    uint32_t complete;      /* recovery point: the identifier necessary for the recovery from its starting point to be completed */
    uint32_t identifier;    /* the identifier for samples
                             * If this identifier equals a certain identifier of recovery point,
                             * then this sample is the recovery point of the earliest group in the pool. */
} lsmash_post_roll_t;

typedef struct
{
    uint16_t distance;      /* pre-roll distance for representing audio decoder delay derived from composition
                             * For example, typical AAC encoding uses a transform over consecutive sets of 2048 audio samples,
                             * applied every 1024 audio samples (MDCTs are overlapped).
                             * For correct audio to be decoded, both transforms for any period of 1024 audio samples are needed.
                             * For this AAC stream, therefore, shall be set to 1 (one AAC access unit).
                             * Note: the number of priming audio sample i.e. encoder delay shall be represented by media_time in edit. */
} lsmash_pre_roll_t;

typedef struct
{
    uint8_t allow_earlier;
    uint8_t leading;
    uint8_t independent;
    uint8_t disposable;
    uint8_t redundant;
    lsmash_random_access_type random_access_type;
    lsmash_post_roll_t        post_roll;
    lsmash_pre_roll_t         pre_roll;
} lsmash_sample_property_t;

typedef struct
{
    uint32_t length;
    uint8_t *data;
    uint64_t dts;
    uint64_t cts;
    uint32_t index;
    lsmash_sample_property_t prop;
} lsmash_sample_t;

typedef struct
{
    uint64_t dts;
    uint64_t cts;
} lsmash_media_ts_t;

typedef struct
{
    uint32_t sample_count;
    lsmash_media_ts_t *timestamp;
} lsmash_media_ts_list_t;

/* */
typedef int (*lsmash_adhoc_remux_callback)( void* param, uint64_t done, uint64_t total );
typedef struct {
    uint64_t buffer_size;
    lsmash_adhoc_remux_callback func;
    void* param;
} lsmash_adhoc_remux_t;

/* L-SMASH's original structure, summary of audio/video stream configuration */
/* NOTE: For audio, currently assuming AAC-LC. */

#define LSMASH_BASE_SUMMARY \
    lsmash_codec_type sample_type;      /* Codec type */ \
    lsmash_mp4sys_object_type_indication object_type_indication; \
    lsmash_mp4sys_stream_type stream_type; \
    void *exdata;               /* typically payload of DecoderSpecificInfo (that's called AudioSpecificConfig in mp4a) */ \
    uint32_t exdata_length;     /* length of exdata */ \
    uint32_t max_au_length;     /* buffer length for 1 access unit, typically max size of 1 audio/video frame */

typedef struct
{
    LSMASH_BASE_SUMMARY
} lsmash_summary_t;

typedef struct
{
    LSMASH_BASE_SUMMARY
    // mp4a_audioProfileLevelIndication pli;   /* I wonder we should have this or not. */
    lsmash_mp4a_AudioObjectType aot;        /* Detailed codec type. If not mp4a, just ignored. */
    uint32_t frequency;                     /* Even if the stream is HE-AAC v1/SBR, this is base AAC's one. */
    uint32_t channels;                      /* Even if the stream is HE-AAC v2/SBR+PS, this is base AAC's one. */
    uint32_t bit_depth;                     /* If AAC, AAC stream itself does not mention to accuracy (bit_depth of decoded PCM data), we assume 16bit. */
    uint32_t samples_in_frame;              /* Even if the stream is HE-AAC/aacPlus/SBR(+PS), this is base AAC's one, so 1024. */
    lsmash_mp4a_aac_sbr_mode sbr_mode;      /* SBR treatment. Currently we always set this as mp4a_AAC_SBR_NOT_SPECIFIED(Implicit signaling).
                                             * User can set this for treatment in other way. */
    lsmash_channel_layout_tag layout_tag;   /* channel layout */
    lsmash_channel_bitmap bitmap;           /* Only available when layout_tag is set to QT_CHANNEL_LAYOUT_USE_CHANNEL_BITMAP. */
    /* LPCM descriptions */
    uint8_t sample_format;      /* 0: integer, 1: floating point */
    uint8_t endianness;         /* 0: big endian, 1: little endian */
    uint8_t signedness;         /* 0: unsigned, 1: signed / This is only valid when sample format is integer. */
    uint8_t packed;             /* 0: unpacked, 1: packed i.e. the sample bits occupy the entire available bits for the channel */
    uint8_t alignment;          /* 0: low bit placement, 1: high bit placement / This is only valid when unpacked. */
    uint8_t interleaved;        /* 0: non-interleaved, 1: interleaved i.e. the samples for each channels are stored in one stream */
} lsmash_audio_summary_t;

typedef struct
{
    LSMASH_BASE_SUMMARY
    // mp4sys_visualProfileLevelIndication pli;    /* I wonder we should have this or not. */
    // lsmash_mp4v_VideoObjectType vot;            /* Detailed codec type. If not mp4v, just ignored. */
    uint32_t timescale;                         /* media timescale
                                                 * User can't set this parameter manually. */
    uint32_t timebase;                          /* increment unit of timestamp
                                                 * User can't set this parameter manually. */
    uint8_t vfr;                                /* whether a stream is assumed as variable frame rate
                                                 * User can't set this parameter manually. */
    uint8_t full_range;
    uint32_t width;                             /* pixel counts of width samples have */
    uint32_t height;                            /* pixel counts of height samples have */
    uint32_t crop_top;
    uint32_t crop_left;
    uint32_t crop_bottom;
    uint32_t crop_right;
    uint32_t par_h;                             /* horizontal factor of pixel aspect ratio */
    uint32_t par_v;                             /* vertical factor of pixel aspect ratio */
    lsmash_scaling_method scaling_method;       /* If not set, video samples are scaled into the visual presentation region to fill it. */
    lsmash_color_parameter primaries;
    lsmash_color_parameter transfer;
    lsmash_color_parameter matrix;
} lsmash_video_summary_t;

typedef struct
{
    lsmash_media_type handler_type;     /* the nature of the media
                                         * You can't change handler_type through this parameter manually. */
    uint32_t timescale;                 /* media timescale: timescale for this media */
    uint64_t duration;                  /* the duration of this media, expressed in the media timescale
                                         * You can't set this parameter manually. */
    uint8_t  roll_grouping;             /* roll recovery grouping present
                                         * Require 'avc1' brand, or ISO Base Media File Format version 2 or later. */
    uint8_t  rap_grouping;              /* random access point grouping present
                                         * Require ISO Base Media File Format version 6 or later. */
    /* Use either type of language code. */
    uint16_t MAC_language;              /* Macintosh language code for this media */
    uint16_t ISO_language;              /* ISO 639-2/T language code for this media */
    /* human-readable name for the track type (for debugging and inspection purposes) */
    char *media_handler_name;
    char *data_handler_name;
    /* Any user shouldn't use the following parameters. */
    PRIVATE char media_handler_name_shadow[256];
    PRIVATE char data_handler_name_shadow[256];
} lsmash_media_parameters_t;

typedef struct
{
    lsmash_track_mode mode;
    uint32_t track_ID;              /* an integer that uniquely identifies the track
                                     * Don't set to value already used except for zero value.
                                     * Zero value don't override established track_ID. */
    uint64_t duration;              /* the duration of this track expressed in the movie timescale units
                                     * If there is any edit, your setting is ignored. */
    int16_t  alternate_group;       /* an integer that specifies a group or collection of tracks
                                     * If this field is not 0, it should be the same for tracks that contain alternate data for one another
                                     * and different for tracks belonging to different such groups.
                                     * Only one track within an alternate group should be played or streamed at any one time.
                                     * Note: alternate_group is ignored when a file is read as an MPEG-4. */
    /* The following parameters are ignored when a file is read as an MPEG-4 or 3GPP file format. */
    int16_t  video_layer;           /* the front-to-back ordering of video tracks; tracks with lower numbers are closer to the viewer. */
    int16_t  audio_volume;          /* fixed point 8.8 number. 0x0100 is full volume. */
    int32_t  matrix[9];             /* transformation matrix for the video
                                     * Each value represents, in order, a, b, u, c, d, v, x, y and w.
                                     * All the values in a matrix are stored as 16.16 fixed-point values,
                                     * except for u, v and w, which are stored as 2.30 fixed-point values.
                                     * Not all derived specifications use matrices.
                                     * If a matrix is used, the point (p, q) is transformed into (p', q') using the matrix as follows:
                                     *             | a b u |
                                     * (p, q, 1) * | c d v | = z * (p', q', 1)
                                     *             | x y w |
                                     * p' = (a * p + c * q + x) / z; q' = (b * p + d * q + y) / z; z = u * p + v * q + w
                                     * Note: transformation matrix is applied after scaling to display size up to display_width and display_height. */
    /* visual presentation region size */
    uint32_t display_width;         /* visual presentation region size of horizontal direction as fixed point 16.16 number. */
    uint32_t display_height;        /* visual presentation region size of vertical direction as fixed point 16.16 number. */
    /* */
    uint8_t  aperture_modes;        /* track aperture modes present
                                     * This feature is only available under QuickTime file format.
                                     * Automatically disabled if multiple sample description is present or scaling method is specified. */
} lsmash_track_parameters_t;

typedef struct
{
    lsmash_brand_type  major_brand;         /* the best used brand */
    lsmash_brand_type *brands;              /* the list of compatible brands */
    uint32_t number_of_brands;              /* the number of compatible brands used in the movie */
    uint32_t minor_version;                 /* minor version of best used brand */
    double   max_chunk_duration;            /* max duration per chunk in seconds. 0.5 is default value. */
    double   max_async_tolerance;           /* max tolerance, in seconds, for amount of interleaving asynchronization between tracks.
                                             * 2.0 is default value. At least twice of max_chunk_duration is used. */
    uint64_t max_chunk_size;                /* max size per chunk in bytes. 4*1024*1024 (4MiB) is default value. */
    uint64_t max_read_size;                 /* max size of reading from a chunk at a time. 4*1024*1024 (4MiB) is default value. */
    uint32_t timescale;                     /* movie timescale: timescale for the entire presentation */
    uint64_t duration;                      /* the duration, expressed in movie timescale, of the longest track
                                             * You can't set this parameter manually. */
    uint32_t number_of_tracks;              /* the number of tracks in the movie
                                             * You can't set this parameter manually. */
    /* The following parameters are recognized only when a file is read as an Apple MPEG-4 or QuickTime file fromat. */
    int32_t  playback_rate;                 /* fixed point 16.16 number. 0x00010000 is normal forward playback and default value. */
    int32_t  playback_volume;               /* fixed point 8.8 number. 0x0100 is full volume and default value. */
    int32_t  preview_time;                  /* the time value in the movie at which the preview begins */
    int32_t  preview_duration;              /* the duration of the movie preview in movie timescale units */
    int32_t  poster_time;                   /* the time value of the time of the movie poster */
    /* Any user shouldn't use the following parameter. */
    PRIVATE lsmash_brand_type brands_shadow[50];
} lsmash_movie_parameters_t;

typedef enum
{
    LSMASH_BOOLEAN_FALSE = 0,
    LSMASH_BOOLEAN_TRUE  = 1
} lsmash_boolean_t;

typedef struct lsmash_root_tag lsmash_root_t;
typedef void lsmash_itunes_metadata_list_t;

/* public functions */
int lsmash_add_sps_entry( lsmash_root_t *root, uint32_t track_ID, uint32_t entry_number, uint8_t *sps, uint32_t sps_size );
int lsmash_add_pps_entry( lsmash_root_t *root, uint32_t track_ID, uint32_t entry_number, uint8_t *pps, uint32_t pps_size );
int lsmash_add_spsext_entry( lsmash_root_t *root, uint32_t track_ID, uint32_t entry_number, uint8_t *spsext, uint32_t spsext_size );
int lsmash_add_sample_entry( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_type, void* summary );

int lsmash_add_btrt( lsmash_root_t *root, uint32_t track_ID, uint32_t entry_number );
int lsmash_add_free( lsmash_root_t *root, uint8_t *data, uint64_t data_length );

int lsmash_write_free( lsmash_root_t *root );

uint64_t lsmash_get_media_duration( lsmash_root_t *root, uint32_t track_ID );
uint64_t lsmash_get_track_duration( lsmash_root_t *root, uint32_t track_ID );
uint32_t lsmash_get_last_sample_delta( lsmash_root_t *root, uint32_t track_ID );
uint32_t lsmash_get_start_time_offset( lsmash_root_t *root, uint32_t track_ID );
uint32_t lsmash_get_composition_to_decode_shift( lsmash_root_t *root, uint32_t track_ID );
uint32_t lsmash_get_media_timescale( lsmash_root_t *root, uint32_t track_ID );
uint32_t lsmash_get_movie_timescale( lsmash_root_t *root );

int lsmash_set_avc_config( lsmash_root_t *root, uint32_t track_ID, uint32_t entry_number,
                           uint8_t configurationVersion, uint8_t AVCProfileIndication, uint8_t profile_compatibility,
                           uint8_t AVCLevelIndication, uint8_t lengthSizeMinusOne,
                           uint8_t chroma_format, uint8_t bit_depth_luma_minus8, uint8_t bit_depth_chroma_minus8 );
int lsmash_set_last_sample_delta( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_delta );
int lsmash_set_free( lsmash_root_t *root, uint8_t *data, uint64_t data_length );
int lsmash_set_tyrant_chapter( lsmash_root_t *root, char *file_name, int add_bom );

int lsmash_create_reference_chapter_track( lsmash_root_t *root, uint32_t track_ID, char *file_name );
int lsmash_create_object_descriptor( lsmash_root_t *root );

int lsmash_create_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID, uint64_t segment_duration, int64_t media_time, int32_t media_rate );
int lsmash_modify_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID, uint32_t entry_number, uint64_t segment_duration, int64_t media_time, int32_t media_rate );
int lsmash_delete_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID );

int lsmash_update_media_modification_time( lsmash_root_t *root, uint32_t track_ID );
int lsmash_update_track_modification_time( lsmash_root_t *root, uint32_t track_ID );
int lsmash_update_movie_modification_time( lsmash_root_t *root );
int lsmash_update_track_duration( lsmash_root_t *root, uint32_t track_ID, uint32_t last_sample_delta );

uint16_t lsmash_pack_iso_language( char *iso_language );

lsmash_root_t *lsmash_open_movie( const char *filename, lsmash_file_mode mode );
void lsmash_initialize_movie_parameters( lsmash_movie_parameters_t *param );
int lsmash_set_movie_parameters( lsmash_root_t *root, lsmash_movie_parameters_t *param );
int lsmash_get_movie_parameters( lsmash_root_t *root, lsmash_movie_parameters_t *param );
uint32_t lsmash_create_track( lsmash_root_t *root, lsmash_media_type media_type );
uint32_t lsmash_get_track_ID( lsmash_root_t *root, uint32_t track_number );
void lsmash_initialize_track_parameters( lsmash_track_parameters_t *param );
int lsmash_set_track_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_track_parameters_t *param );
int lsmash_get_track_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_track_parameters_t *param );
void lsmash_initialize_media_parameters( lsmash_media_parameters_t *param );
int lsmash_set_media_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_media_parameters_t *param );
int lsmash_get_media_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_media_parameters_t *param );
lsmash_sample_t *lsmash_create_sample( uint32_t size );
int lsmash_sample_alloc( lsmash_sample_t *sample, uint32_t size );
void lsmash_delete_sample( lsmash_sample_t *sample );
int lsmash_append_sample( lsmash_root_t *root, uint32_t track_ID, lsmash_sample_t *sample );
int lsmash_flush_pooled_samples( lsmash_root_t *root, uint32_t track_ID, uint32_t last_sample_delta );
int lsmash_finish_movie( lsmash_root_t *root, lsmash_adhoc_remux_t* remux );
void lsmash_discard_boxes( lsmash_root_t *root );
void lsmash_destroy_root( lsmash_root_t *root );

int lsmash_create_fragment_movie( lsmash_root_t *root );
int lsmash_create_fragment_empty_duration( lsmash_root_t *root, uint32_t track_ID, uint32_t duration );

void lsmash_delete_track( lsmash_root_t *root, uint32_t track_ID );
void lsmash_delete_tyrant_chapter( lsmash_root_t *root );

/* track_ID == 0 means copyright declaration applies to the entire presentation, not an entire track. */
int lsmash_set_copyright( lsmash_root_t *root, uint32_t track_ID, uint16_t ISO_language, char *notice );

/* When type is specified as ITUNES_METADATA_TYPE_CUSTOM, meaning is mandatory while name is optionally valid.
 * Otherwise, meaning and name are just ignored. */
int lsmash_set_itunes_metadata_string( lsmash_root_t *root, lsmash_itunes_metadata_type type, char *value, char *meaning, char *name );
int lsmash_set_itunes_metadata_integer( lsmash_root_t *root, lsmash_itunes_metadata_type type, uint64_t value, char *meaning, char *name );
int lsmash_set_itunes_metadata_boolean( lsmash_root_t *root, lsmash_itunes_metadata_type type, lsmash_boolean_t value, char *meaning, char *name );

#ifdef LSMASH_DEMUXER_ENABLED
int lsmash_print_movie( lsmash_root_t *root );

/* This function might output BOM on Windows. Make sure that this is the first function that outputs something to stdout. */
int lsmash_print_chapter_list( lsmash_root_t *root );

int lsmash_copy_timeline_map( lsmash_root_t *dst, uint32_t dst_track_ID, lsmash_root_t *src, uint32_t src_track_ID );
int lsmash_copy_decoder_specific_info( lsmash_root_t *dst, uint32_t dst_track_ID, lsmash_root_t *src, uint32_t src_track_ID );
int lsmash_construct_timeline( lsmash_root_t *root, uint32_t track_ID );
void lsmash_destruct_timeline( lsmash_root_t *root, uint32_t track_ID );
int lsmash_get_last_sample_delta_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t *last_sample_delta );
int lsmash_get_sample_delta_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number, uint32_t *sample_delta );
int lsmash_get_dts_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number, uint64_t *dts );
int lsmash_get_cts_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number, uint64_t *cts );
int lsmash_get_composition_to_decode_shift_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t *ctd_shift );
int lsmash_get_closest_random_accessible_point_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number, uint32_t *rap_number );
int lsmash_get_closest_random_accessible_point_detail_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number,
                                                                           uint32_t *rap_number, lsmash_random_access_type *type, uint32_t *leading, uint32_t *distance );
uint32_t lsmash_get_sample_count_in_media_timeline( lsmash_root_t *root, uint32_t track_ID );
uint32_t lsmash_get_max_sample_size_in_media_timeline( lsmash_root_t *root, uint32_t track_ID );
lsmash_sample_t *lsmash_get_sample_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number );
int lsmash_check_sample_existence_in_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number );

lsmash_itunes_metadata_list_t *lsmash_export_itunes_metadata( lsmash_root_t *root );
int lsmash_import_itunes_metadata( lsmash_root_t *root, lsmash_itunes_metadata_list_t *list );
void lsmash_destroy_itunes_metadata( lsmash_itunes_metadata_list_t *list );

int lsmash_set_media_timestamps( lsmash_root_t *root, uint32_t track_ID, lsmash_media_ts_list_t *ts_list );
int lsmash_get_media_timestamps( lsmash_root_t *root, uint32_t track_ID, lsmash_media_ts_list_t *ts_list );
void lsmash_delete_media_timestamps( lsmash_media_ts_list_t *ts_list );
int lsmash_get_max_sample_delay( lsmash_media_ts_list_t *ts_list, uint32_t *max_sample_delay );
void lsmash_sort_timestamps_decoding_order( lsmash_media_ts_list_t *ts_list );
void lsmash_sort_timestamps_composition_order( lsmash_media_ts_list_t *ts_list );
#endif

/* to facilitate to make exdata (typically DecoderSpecificInfo or AudioSpecificConfig). */
int lsmash_setup_AudioSpecificConfig( lsmash_audio_summary_t* summary );
int lsmash_summary_add_exdata( lsmash_summary_t *summary, void* exdata, uint32_t exdata_length );

lsmash_summary_t *lsmash_create_summary( lsmash_mp4sys_stream_type stream_type );
void lsmash_cleanup_summary( lsmash_summary_t *summary );

#undef PRIVATE

#endif
