/*****************************************************************************
 * lsmash.h:
 *****************************************************************************
 * Copyright (C) 2010-2012 L-SMASH project
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

/****************************************************************************
 * ROOT
 *   This is the top level abstract layer for file handling.
 ****************************************************************************/
typedef struct lsmash_root_tag lsmash_root_t;

typedef enum
{
    LSMASH_FILE_MODE_WRITE             = 1,
    LSMASH_FILE_MODE_READ              = 1<<1,
    LSMASH_FILE_MODE_DUMP              = 1<<2,
    LSMASH_FILE_MODE_FRAGMENTED        = 1<<16,
    LSMASH_FILE_MODE_WRITE_FRAGMENTED  = LSMASH_FILE_MODE_WRITE | LSMASH_FILE_MODE_FRAGMENTED,
    //LSMASH_FILE_MODE_READ_FRAGMENTED   = LSMASH_FILE_MODE_READ  | LSMASH_FILE_MODE_FRAGMENTED,
} lsmash_file_mode;

typedef int (*lsmash_adhoc_remux_callback)( void* param, uint64_t done, uint64_t total );
typedef struct {
    uint64_t buffer_size;
    lsmash_adhoc_remux_callback func;
    void* param;
} lsmash_adhoc_remux_t;

/****************************************************************************
 * Basic Types
 ****************************************************************************/
/* rational types */
typedef struct
{
    uint32_t n;     /* numerator */
    uint32_t d;     /* denominator */
} lsmash_rational_u32_t;

typedef struct
{
    int32_t  n;     /* numerator */
    uint32_t d;     /* denominator */
} lsmash_rational_s32_t;

typedef enum
{
    LSMASH_BOOLEAN_FALSE = 0,
    LSMASH_BOOLEAN_TRUE  = 1
} lsmash_boolean_t;

/****************************************************************************
 * Summary of Stream Configuration
 *   This is L-SMASH's original structure.
 ****************************************************************************/
typedef enum
{
    LSMASH_SUMMARY_TYPE_UNKOWN = 0,
    LSMASH_SUMMARY_TYPE_VIDEO,
    LSMASH_SUMMARY_TYPE_AUDIO,
} lsmash_summary_type;

/* CODEC identifiers */
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
    QT_CODEC_TYPE_DVC_VIDEO     = LSMASH_4CC( 'd', 'v', 'c', ' ' ),   /* DV NTSC format */
    QT_CODEC_TYPE_DVCP_VIDEO    = LSMASH_4CC( 'd', 'v', 'c', 'p' ),   /* DV PAL format */
    QT_CODEC_TYPE_DVPP_VIDEO    = LSMASH_4CC( 'd', 'v', 'p', 'p' ),   /* Panasonic DVCPro PAL format */
    QT_CODEC_TYPE_DV5N_VIDEO    = LSMASH_4CC( 'd', 'v', '5', 'n' ),   /* Panasonic DVCPro-50 NTSC format */
    QT_CODEC_TYPE_DV5P_VIDEO    = LSMASH_4CC( 'd', 'v', '5', 'p' ),   /* Panasonic DVCPro-50 PAL format */
    QT_CODEC_TYPE_DVH2_VIDEO    = LSMASH_4CC( 'd', 'v', 'h', '2' ),   /* Panasonic DVCPro-HD 1080p25 format */
    QT_CODEC_TYPE_DVH3_VIDEO    = LSMASH_4CC( 'd', 'v', 'h', '3' ),   /* Panasonic DVCPro-HD 1080p30 format */
    QT_CODEC_TYPE_DVH5_VIDEO    = LSMASH_4CC( 'd', 'v', 'h', '5' ),   /* Panasonic DVCPro-HD 1080i50 format */
    QT_CODEC_TYPE_DVH6_VIDEO    = LSMASH_4CC( 'd', 'v', 'h', '6' ),   /* Panasonic DVCPro-HD 1080i60 format */
    QT_CODEC_TYPE_DVHP_VIDEO    = LSMASH_4CC( 'd', 'v', 'h', 'p' ),   /* Panasonic DVCPro-HD 720p60 format */
    QT_CODEC_TYPE_DVHQ_VIDEO    = LSMASH_4CC( 'd', 'v', 'h', 'q' ),   /* Panasonic DVCPro-HD 720p50 format */
    QT_CODEC_TYPE_FLIC_VIDEO    = LSMASH_4CC( 'f', 'l', 'i', 'c' ),   /* Autodesk FLIC animation format */
    QT_CODEC_TYPE_GIF_VIDEO     = LSMASH_4CC( 'g', 'i', 'f', ' ' ),   /* GIF image format */
    QT_CODEC_TYPE_H261_VIDEO    = LSMASH_4CC( 'h', '2', '6', '1' ),   /* ITU H.261 video */
    QT_CODEC_TYPE_H263_VIDEO    = LSMASH_4CC( 'h', '2', '6', '3' ),   /* ITU H.263 video */
    QT_CODEC_TYPE_JPEG_VIDEO    = LSMASH_4CC( 'j', 'p', 'e', 'g' ),   /* JPEG image format */
    QT_CODEC_TYPE_MJPA_VIDEO    = LSMASH_4CC( 'm', 'j', 'p', 'a' ),   /* Motion-JPEG (format A) */
    QT_CODEC_TYPE_MJPB_VIDEO    = LSMASH_4CC( 'm', 'j', 'p', 'b' ),   /* Motion-JPEG (format B) */
    QT_CODEC_TYPE_PNG_VIDEO     = LSMASH_4CC( 'p', 'n', 'g', ' ' ),   /* W3C Portable Network Graphics (PNG) */
    QT_CODEC_TYPE_RAW_VIDEO     = LSMASH_4CC( 'r', 'a', 'w', ' ' ),   /* Uncompressed RGB */
    QT_CODEC_TYPE_RLE_VIDEO     = LSMASH_4CC( 'r', 'l', 'e', ' ' ),   /* Apple animation codec */
    QT_CODEC_TYPE_RPZA_VIDEO    = LSMASH_4CC( 'r', 'p', 'z', 'a' ),   /* Apple simple video 'road pizza' compression */
    QT_CODEC_TYPE_TGA_VIDEO     = LSMASH_4CC( 't', 'g', 'a', ' ' ),   /* Truvision Targa video format */
    QT_CODEC_TYPE_TIFF_VIDEO    = LSMASH_4CC( 't', 'i', 'f', 'f' ),   /* Tagged Image File Format (Adobe) */
    QT_CODEC_TYPE_ULRA_VIDEO    = LSMASH_4CC( 'U', 'L', 'R', 'A' ),   /* Ut Video RGBA 4:4:4:4 8bit full-range */
    QT_CODEC_TYPE_ULRG_VIDEO    = LSMASH_4CC( 'U', 'L', 'R', 'G' ),   /* Ut Video RGB 4:4:4 8bit full-range */
    QT_CODEC_TYPE_ULY0_VIDEO    = LSMASH_4CC( 'U', 'L', 'Y', '0' ),   /* Ut Video YCbCr 4:2:0 8bit limited */
    QT_CODEC_TYPE_ULY2_VIDEO    = LSMASH_4CC( 'U', 'L', 'Y', '2' ),   /* Ut Video YCbCr 4:2:2 8bit limited */
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

    LSMASH_CODEC_TYPE_RAW       = LSMASH_4CC( 'r', 'a', 'w', ' ' ),   /* Either video or audio */
} lsmash_codec_type;

typedef enum
{
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_UNKNOWN = 0,                        /* must be LSMASH_CODEC_SPECIFIC_FORM_UNSTRUCTURED */

    LSMASH_CODEC_SPECIFIC_DATA_TYPE_MP4SYS_DECODER_CONFIG,

    LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_H264,
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_VC_1,
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_AC_3,
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_EC_3,
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_DTS,
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_ALAC,

    LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_SAMPLE_SCALE,
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_H264_BITRATE,

    LSMASH_CODEC_SPECIFIC_DATA_TYPE_QT_VIDEO_COMMON,                    /* must be LSMASH_CODEC_SPECIFIC_FORM_STRUCTURED */
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_QT_AUDIO_COMMON,                    /* must be LSMASH_CODEC_SPECIFIC_FORM_STRUCTURED */
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_QT_AUDIO_FORMAT_SPECIFIC_FLAGS,     /* must be LSMASH_CODEC_SPECIFIC_FORM_STRUCTURED */
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_QT_AUDIO_DECOMPRESSION_PARAMETERS,  /* must be LSMASH_CODEC_SPECIFIC_FORM_UNSTRUCTURED */

    LSMASH_CODEC_SPECIFIC_DATA_TYPE_QT_VIDEO_FIELD_INFO,
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_QT_VIDEO_PIXEL_FORMAT,
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_QT_VIDEO_SIGNIFICANT_BITS,
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_QT_VIDEO_GAMMA_LEVEL,
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_QT_AUDIO_CHANNEL_LAYOUT,

    LSMASH_CODEC_SPECIFIC_DATA_TYPE_CODEC_GLOBAL_HEADER,
} lsmash_codec_specific_data_type;

typedef enum
{
    LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED   = 0,
    LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED = 1
} lsmash_codec_specific_format;

typedef union
{
    void    *structured;        /* LSMASH_CODEC_SPECIFIC_FORM_STRUCTURED */
    uint8_t *unstructured;      /* LSMASH_CODEC_SPECIFIC_FORM_UNSTRUCTURED */
} lsmash_codec_specific_data_t;

typedef void (*lsmash_codec_specific_destructor_t)( void * );
typedef struct
{
    lsmash_codec_specific_data_type    type;
    lsmash_codec_specific_format       format;
    lsmash_codec_specific_data_t       data;
    uint32_t                           size;
    lsmash_codec_specific_destructor_t destruct;
} lsmash_codec_specific_t;

typedef struct lsmash_codec_specific_list_tag lsmash_codec_specific_list_t;

#define LSMASH_BASE_SUMMARY \
    lsmash_summary_type summary_type; \
    lsmash_codec_type sample_type; \
    lsmash_codec_specific_list_t *opaque; \
    uint32_t max_au_length;     /* buffer length for 1 access unit, typically max size of 1 audio/video frame */

typedef struct
{
    LSMASH_BASE_SUMMARY
} lsmash_summary_t;

/****************************************************************************
 * Audio Description Layer
 *   NOTE: Currently assuming AAC-LC.
 ****************************************************************************/
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

/* See ISO/IEC 14496-3 Signaling of SBR, SBR Signaling and Corresponding Decoder Behavior */
typedef enum {
    MP4A_AAC_SBR_NOT_SPECIFIED = 0x0,   /* not mention to SBR presence. Implicit signaling. */
    MP4A_AAC_SBR_NONE,                  /* explicitly signals SBR does not present. Useless in general. */
    MP4A_AAC_SBR_BACKWARD_COMPATIBLE,   /* explicitly signals SBR present. Recommended method to signal SBR. */
    MP4A_AAC_SBR_HIERARCHICAL           /* SBR exists. SBR dedicated method. */
} lsmash_mp4a_aac_sbr_mode;

typedef struct
{
    LSMASH_BASE_SUMMARY
    // mp4a_audioProfileLevelIndication pli;   /* I wonder we should have this or not. */
    lsmash_mp4a_AudioObjectType aot;        /* Detailed codec type. If not mp4a, just ignored. */
    uint32_t frequency;                     /* the audio sampling rate (Hz)
                                             * For some audio, this field is used as a nominal value.
                                             * For instance, even if the stream is HE-AAC v1/SBR, this is base AAC's one. */
    uint32_t channels;                      /* the number of audio channels
                                             * Even if the stream is HE-AAC v2/SBR+PS, this is base AAC's one. */
    uint32_t sample_size;                   /* For uncompressed audio, the number of bits in each uncompressed sample for a single channel
                                             * For some compressed audio, such as audio that uses MDCT, this field shall be nonsense and then set to 16. */
    uint32_t samples_in_frame;              /* the number of decoded PCM samples in an audio frame at 'frequency'.
                                             * Even if the stream is HE-AAC/aacPlus/SBR(+PS), this is base AAC's one, so 1024. */
    lsmash_mp4a_aac_sbr_mode sbr_mode;      /* SBR treatment. Currently we always set this as mp4a_AAC_SBR_NOT_SPECIFIED(Implicit signaling).
                                             * User can set this for treatment in other way. */
} lsmash_audio_summary_t;

/****************************************************************************
 * Video Description Layer
 ****************************************************************************/
/* Clean Aperture */
typedef struct
{
    lsmash_rational_u32_t width;
    lsmash_rational_u32_t height;
    lsmash_rational_s32_t horizontal_offset;
    lsmash_rational_s32_t vertical_offset;
} lsmash_clap_t;

typedef struct
{
    lsmash_rational_u32_t top;
    lsmash_rational_u32_t left;
    lsmash_rational_u32_t bottom;
    lsmash_rational_u32_t right;
} lsmash_crop_t;

/* Video depth */
typedef enum
{
    ISOM_DEPTH_TEMPLATE = 0x0018,

    /* H.264/AVC */
    AVC_DEPTH_COLOR_WITH_NO_ALPHA     = 0x0018,     /* color with no alpha */
    AVC_DEPTH_GRAYSCALE_WITH_NO_ALPHA = 0x0028,     /* grayscale with no alpha */
    AVC_DEPTH_WITH_ALPHA              = 0x0020,     /* gray or color with alpha */

    /* QuickTime Video
     * (1-32) or (33-40 grayscale) */
    QT_VIDEO_DEPTH_COLOR_1      = 0x0001,
    QT_VIDEO_DEPTH_COLOR_2      = 0x0002,
    QT_VIDEO_DEPTH_COLOR_4      = 0x0004,
    QT_VIDEO_DEPTH_COLOR_8      = 0x0008,
    QT_VIDEO_DEPTH_COLOR_16     = 0x0010,
    QT_VIDEO_DEPTH_COLOR_24     = 0x0018,
    QT_VIDEO_DEPTH_COLOR_32     = 0x0020,
    QT_VIDEO_DEPTH_GRAYSCALE_1  = 0x0021,
    QT_VIDEO_DEPTH_GRAYSCALE_2  = 0x0022,
    QT_VIDEO_DEPTH_GRAYSCALE_4  = 0x0024,
    QT_VIDEO_DEPTH_GRAYSCALE_8  = 0x0028,
} lsmash_video_depth;

/* Index for the chromaticity coordinates of the color primaries */
enum
{
    /* for ISO Base Media file format */
    ISOM_PRIMARIES_INDEX_ITU_R709_5      = 1,   /* ITU-R BT.709-2/5, ITU-R BT.1361,
                                                 * SMPTE 274M-1995, SMPTE 296M-1997,
                                                 * IEC 61966-2-1 (sRGB or sYCC), IEC 61966-2-4 (xvYCC),
                                                 * SMPTE RP 177M-1993 Annex B
                                                 *   green x = 0.300  y = 0.600
                                                 *   blue  x = 0.150  y = 0.060
                                                 *   red   x = 0.640  y = 0.330
                                                 *   white x = 0.3127 y = 0.3290 (CIE III. D65) */
    ISOM_PRIMARIES_INDEX_UNSPECIFIED     = 2,   /* Unspecified */
    ISOM_PRIMARIES_INDEX_ITU_R470M       = 4,   /* ITU-R BT.470-6 System M
                                                 *   green x = 0.21  y = 0.71
                                                 *   blue  x = 0.14  y = 0.08
                                                 *   red   x = 0.67  y = 0.33
                                                 *   white x = 0.310 y = 0.316 */
    ISOM_PRIMARIES_INDEX_ITU_R470BG      = 5,   /* EBU Tech. 3213 (1981), ITU-R BT.470-6 System B, G,
                                                 * ITU-R BT.601-6 625, ITU-R BT.1358 625,
                                                 * ITU-R BT.1700 625 PAL and 625 SECAM
                                                 *   green x = 0.29   y = 0.60
                                                 *   blue  x = 0.15   y = 0.06
                                                 *   red   x = 0.64   y = 0.33
                                                 *   white x = 0.3127 y = 0.3290 (CIE III. D65) */
    ISOM_PRIMARIES_INDEX_SMPTE_170M_2004 = 6,   /* SMPTE C Primaries from SMPTE RP 145-1993, SMPTE 170M-2004,
                                                 * ITU-R BT.601-6 525, ITU-R BT.1358 525,
                                                 * ITU-R BT.1700 NTSC, SMPTE 170M-2004
                                                 *   green x = 0.310  y = 0.595
                                                 *   blue  x = 0.155  y = 0.070
                                                 *   red   x = 0.630  y = 0.340
                                                 *   white x = 0.3127 y = 0.3290 (CIE III. D65) */
    ISOM_PRIMARIES_INDEX_SMPTE_240M_1999 = 7,   /* SMPTE 240M-1999
                                                 * functionally the same as the value ISOM_PRIMARIES_INDEX_SMPTE_170M_2004 */

    /* for QuickTime file format */
    QT_PRIMARIES_INDEX_ITU_R709_2        = 1,   /* the same as the value ISOM_PRIMARIES_INDEX_ITU_R709_5 */
    QT_PRIMARIES_INDEX_UNSPECIFIED       = 2,   /* Unspecified */
    QT_PRIMARIES_INDEX_EBU_3213          = 5,   /* the same as the value ISOM_PRIMARIES_INDEX_ITU_R470BG */
    QT_PRIMARIES_INDEX_SMPTE_C           = 6,   /* the same as the value ISOM_PRIMARIES_INDEX_SMPTE_170M_2004 */
};

/* Index for the opto-electronic transfer characteristic of the image color components */
enum
{
    /* for ISO Base Media file format */
    ISOM_TRANSFER_INDEX_ITU_R709_5      = 1,    /* ITU-R BT.709-2/5, ITU-R BT.1361
                                                 * SMPTE 274M-1995, SMPTE 296M-1997,
                                                 * SMPTE 293M-1996, SMPTE 170M-1994
                                                 *   vV = 1.099 * vLc^0.45 - 0.099 for 1 >= vLc >= 0.018
                                                 *   vV = 4.500 * vLc              for 0.018 > vLc >= 0 */
    ISOM_TRANSFER_INDEX_UNSPECIFIED     = 2,    /* Unspecified */
    ISOM_TRANSFER_INDEX_ITU_R470M       = 4,    /* ITU-R BT.470-6 System M, ITU-R BT.1700 625 PAL and 625 SECAM
                                                 *   Assumed display gamma 2.2 */
    ISOM_TRANSFER_INDEX_ITU_R470BG      = 5,    /* ITU-R BT.470-6 System B, G
                                                 *   Assumed display gamma 2.8 */
    ISOM_TRANSFER_INDEX_SMPTE_170M_2004 = 6,    /* ITU-R BT.601-6 525 or 625, ITU-R BT.1358 525 or 625,
                                                 * ITU-R BT.1700 NTSC, SMPTE 170M-2004
                                                 * functionally the same as the value ISOM_TRANSFER_INDEX_ITU_R709_5
                                                 *   vV = 1.099 * vLc^0.45 - 0.099 for 1 >= vLc >= 0.018
                                                 *   vV = 4.500 * vLc              for 0.018 > vLc >= 0 */
    ISOM_TRANSFER_INDEX_SMPTE_240M_1999 = 7,    /* SMPTE 240M-1995/1999, interim color implementation of SMPTE 274M-1995
                                                 *   vV = 1.1115 * vLc^0.45 - 0.1115 for 1 >= vLc >= 0.0228
                                                 *   vV = 4.0 * vLc                  for 0.0228 > vLc >= 0 */
    ISOM_TRANSFER_INDEX_LINEAR          = 8,    /* Linear transfer characteristics */
    ISOM_TRANSFER_INDEX_XVYCC           = 11,   /* IEC 61966-2-4 (xvYCC)
                                                 *   vV = 1.099 * vLc^0.45 - 0.099     for vLc >= 0.018
                                                 *   vV = 4.500 * vLc                  for 0.018 > vLc > -0.018
                                                 *   vV = -1.099 * (-vLc)^0.45 + 0.099 for -0.018 >= vLc */
    ISOM_TRANSFER_INDEX_ITU_R1361       = 12,   /* ITU-R BT.1361
                                                 *   vV = 1.099 * vLc^0.45 - 0.099               for 1.33 > vLc >= 0.018
                                                 *   vV = 4.500 * vLc                            for 0.018 > vLc >= -0.0045
                                                 *   vV = -(1.099 * (-4 * vLc)^0.45 + 0.099) / 4 for -0.0045 > vLc >= -0.25 */
    ISOM_TRANSFER_INDEX_SRGB            = 13,   /* IEC 61966-2-1 (sRGB or sYCC)
                                                 *   vV = 1.055 * vLc^(1/2.4) - 0.055 for 1 > vLc >= 0.0031308
                                                 *   vV = 12.92 * vLc                 for 0.0031308 > vLc >= 0 */

    /* for QuickTime file format */
    QT_TRANSFER_INDEX_ITU_R709_2        = 1,    /* the same as the value ISOM_TRANSFER_INDEX_ITU_R709_5 */
    QT_TRANSFER_INDEX_UNSPECIFIED       = 2,    /* Unspecified */
    QT_TRANSFER_INDEX_SMPTE_240M_1995   = 7,    /* the same as the value ISOM_TRANSFER_INDEX_SMPTE_240M_1999 */
};

/* Index for the matrix coefficients associated with derivation of luma and chroma signals from the green, blue, and red primaries */
enum
{
    /* for ISO Base Media file format */
    ISOM_MATRIX_INDEX_NO_MATRIX       = 0,  /* No matrix transformation
                                             * IEC 61966-2-1 (sRGB) */
    ISOM_MATRIX_INDEX_ITU_R_709_5     = 1,  /* ITU-R BT.709-2/5, ITU-R BT.1361,
                                             * SMPTE 274M-1995, SMPTE 296M-1997
                                             * IEC 61966-2-1 (sYCC), IEC 61966-2-4 xvYCC_709,
                                             * SMPTE RP 177M-1993 Annex B
                                             *   vKr = 0.2126; vKb = 0.0722 */
    ISOM_MATRIX_INDEX_UNSPECIFIED     = 2,  /* Unspecified */
    ISOM_MATRIX_INDEX_USFCCT_47_CFR   = 4,  /* United States Federal Communications Commission Title 47 Code of Federal Regulations
                                             *   vKr = 0.30; vKb = 0.11 */
    ISOM_MATRIX_INDEX_ITU_R470BG      = 5,  /* ITU-R BT.470-6 System B, G,
                                             * ITU-R BT.601-4/6 625, ITU-R BT.1358 625,
                                             * ITU-R BT.1700 625 PAL and 625 SECAM, IEC 61966-2-4 xvYCC601
                                             *   vKr = 0.299; vKb = 0.114 */
    ISOM_MATRIX_INDEX_SMPTE_170M_2004 = 6,  /* ITU-R BT.601-4/6 525, ITU-R BT.1358 525,
                                             * ITU-R BT.1700 NTSC,
                                             * SMPTE 170M-1994, SMPTE 293M-1996
                                             * functionally the same as the value ISOM_MATRIX_INDEX_ITU_R470BG
                                             *   vKr = 0.299; vKb = 0.114 */
    ISOM_MATRIX_INDEX_SMPTE_240M_1999 = 7,  /* SMPTE 240M-1995, interim color implementation of SMPTE 274M-1995
                                             *   vKr = 0.212; vKb = 0.087 */
    ISOM_MATRIX_INDEX_YCGCO           = 8,  /* YCoCg */

    /* for QuickTime file format */
    QT_MATRIX_INDEX_ITU_R_709_2       = 1,  /* the same as the value ISOM_MATRIX_INDEX_ITU_R_709_5 */
    QT_MATRIX_INDEX_UNSPECIFIED       = 2,  /* Unspecified */
    QT_MATRIX_INDEX_ITU_R_601_4       = 6,  /* the same as the value ISOM_MATRIX_INDEX_SMPTE_170M_2004 */
    QT_MATRIX_INDEX_SMPTE_240M_1995   = 7   /* the same as the value ISOM_MATRIX_INDEX_SMPTE_240M_1999 */
};

typedef struct
{
    LSMASH_BASE_SUMMARY
    // mp4sys_visualProfileLevelIndication pli;    /* I wonder we should have this or not. */
    // lsmash_mp4v_VideoObjectType vot;            /* Detailed codec type. If not mp4v, just ignored. */
    uint32_t           timescale;           /* media timescale
                                             * User can't set this parameter manually. */
    uint32_t           timebase;            /* increment unit of timestamp
                                             * User can't set this parameter manually. */
    uint8_t            vfr;                 /* whether a stream is assumed as variable frame rate
                                             * User can't set this parameter manually. */
    uint32_t           width;               /* pixel counts of width samples have */
    uint32_t           height;              /* pixel counts of height samples have */
    char               compressorname[33];  /* a 32-byte Pascal string containing the name of the compressor that created the image */
    lsmash_video_depth depth;               /* data size of a pixel */
    lsmash_clap_t      clap;                /* clean aperture */
    uint32_t           par_h;               /* horizontal factor of pixel aspect ratio */
    uint32_t           par_v;               /* vertical factor of pixel aspect ratio */
    struct
    {
        /* To omit to write these field, set zero value to all them. */
        uint16_t primaries_index;   /* the chromaticity coordinates of the color primaries */
        uint16_t transfer_index;    /* the opto-electronic transfer characteristic of the image color components */
        uint16_t matrix_index;      /* the matrix coefficients associated with derivation of luma and chroma signals from the green, blue, and red primaries */
        uint8_t  full_range;
    } color;
} lsmash_video_summary_t;

/****************************************************************************
 * Media Sample
 ****************************************************************************/
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

typedef enum
{
    ISOM_SAMPLE_RANDOM_ACCESS_TYPE_NONE         = 0,        /* not random access point */
    ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC         = 1,        /* sync sample */
    ISOM_SAMPLE_RANDOM_ACCESS_TYPE_CLOSED_RAP   = 1,        /* the first sample of a closed GOP */
    ISOM_SAMPLE_RANDOM_ACCESS_TYPE_OPEN_RAP     = 2,        /* the first sample of an open GOP */
    ISOM_SAMPLE_RANDOM_ACCESS_TYPE_UNKNOWN_RAP  = 3,        /* the first sample of an open or closed GOP */
    ISOM_SAMPLE_RANDOM_ACCESS_TYPE_POST_ROLL    = 4,        /* the post-roll starting point of random access recovery */
    ISOM_SAMPLE_RANDOM_ACCESS_TYPE_PRE_ROLL     = 5,        /* the pre-roll ending point of random access recovery */

    QT_SAMPLE_RANDOM_ACCESS_TYPE_NONE           = 0,        /* alias of ISOM_SAMPLE_RANDOM_ACCESS_TYPE_NONE */
    QT_SAMPLE_RANDOM_ACCESS_TYPE_SYNC           = 1,        /* alias of ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC */
    QT_SAMPLE_RANDOM_ACCESS_TYPE_PARTIAL_SYNC   = 2,        /* partial sync sample */
    QT_SAMPLE_RANDOM_ACCESS_TYPE_CLOSED_RAP     = 1,        /* alias of ISOM_SAMPLE_RANDOM_ACCESS_TYPE_CLOSED_RAP */
    QT_SAMPLE_RANDOM_ACCESS_TYPE_OPEN_RAP       = 2,        /* alias of ISOM_SAMPLE_RANDOM_ACCESS_TYPE_OPEN_RAP */
} lsmash_random_access_type;

typedef struct
{
    uint32_t identifier;    /* the identifier of sample
                             * If this identifier equals a certain identifier of random access recovery point,
                             * then this sample is the random access recovery point of the earliest unestablished post-roll group. */
    uint32_t complete;      /* the identifier of future random access recovery point, which is necessary for the recovery from its starting point to be completed
                             * For muxing, this value is used only if random_access_type is set to ISOM_SAMPLE_RANDOM_ACCESS_TYPE_POST_ROLL.
                             * The following is an example of use for gradual decoder refresh of H.264/AVC.
                             *   For each sample, set 'frame_num' to the 'identifier'.
                             *   For samples with recovery point SEI message, set ISOM_SAMPLE_RANDOM_ACCESS_TYPE_POST_ROLL to random_access_type
                             *   and set '(frame_num + recovery_frame_cnt) % MaxFrameNum' to the 'complete'.
                             *   The above-mentioned values are set appropriately, then L-SMASH will establish appropriate post-roll grouping. */
} lsmash_post_roll_t;

typedef struct
{
    uint32_t distance;      /* the distance from the previous random access point or pre-roll starting point
                             * of the random access recovery point to this sample.
                             * For muxing, this value is used only if random_access_type is not set to ISOM_SAMPLE_RANDOM_ACCESS_TYPE_NONE
                             * or ISOM_SAMPLE_RANDOM_ACCESS_TYPE_POST_ROLL.
                             * Some derived specifications forbid using pre-roll settings and use post-roll settings instead (e.g. AVC uses only post-roll).
                             * The following is an example of pre-roll distance for representing audio decoder delay derived from composition.
                             *   Typical AAC encoding uses a transform over consecutive sets of 2048 audio samples,
                             *   applied every 1024 audio samples (MDCTs are overlapped).
                             *   For correct audio to be decoded, both transforms for any period of 1024 audio samples are needed.
                             *   For this AAC stream, therefore, 'distance' of each sample shall be set to 1 (one AAC access unit).
                             *   Note: the number of priming audio sample i.e. encoder delay shall be represented by 'start_time' in an edit. */
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

/****************************************************************************
 * Media Layer
 ****************************************************************************/
/* Media handler types */
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

/* ISO language codes */
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

/****************************************************************************
 * Track Layer
 ****************************************************************************/
/* Track mode */
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

/* Explicit Timeline Map (Edit)
 * There are two types of timeline; one is the media timeline, the other is the presentation timeline (or the movie timeline).
 * An edit maps the presentation timeline to the media timeline.
 * Therefore, an edit can select any portion within the media and specify its playback speed.
 * The media within the track is played through the presentation timeline, so you can construct any complex presentation from a media by edits.
 * In the absence of any edit, there is an implicit one-to-one mapping of these timelines, and the presentation of a track starts at the beginning of the presentation.
 * Note: any edit doesn't restrict decoding and composition. So, if a sample in an edit need to decode from a sample in outside of that edit,
 *       the decoder shall start to decode from there but player shall not display any sample in outside of that edit. */
#define ISOM_EDIT_MODE_NORMAL        (1<<16)
#define ISOM_EDIT_MODE_DWELL         0
#define ISOM_EDIT_MODE_EMPTY         -1
#define ISOM_EDIT_DURATION_UNKNOWN32 0xffffffff
#define ISOM_EDIT_DURATION_UNKNOWN64 0xffffffffffffffff

typedef struct
{
    uint64_t duration;      /* the duration of this edit expressed in the movie timescale units
                             * An edit can be used to the media within fragmented tracks.
                             * The duration is unknown at the time of creating the initial movie because of real-time creation such as live streaming,
                             * it is recomended the duration is set to ISOM_EDIT_DURATION_UNKNOWN32 (the maximum 32-bit unsigned integer)
                             * or ISOM_EDIT_DURATION_UNKNOWN64 (the maximum 64-bit unsigned integer). */
    int64_t  start_time;    /* the starting composition time within the media of this edit
                             * If set to ISOM_EDIT_MODE_EMPTY (-1), it construct an empty edit, which doesn't select any portion within the media. */
    int32_t  rate;          /* the relative rate at which to play the media corresponding to this edit, expressed as 16.16 fixed-point number
                             * If set to ISOM_EDIT_MODE_NORMAL (0x00010000), there is no rate change for timeline mapping.
                             * If set to ISOM_EDIT_MODE_DWELL (0), the media at start_time is presented for the duration. */
} lsmash_edit_t;

/****************************************************************************
 * Movie Layer
 ****************************************************************************/
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

/****************************************************************************
 * Basic Public Functions
 ****************************************************************************/
int lsmash_add_free( lsmash_root_t *root, uint8_t *data, uint64_t data_length );

int lsmash_write_free( lsmash_root_t *root );

uint64_t lsmash_get_media_duration( lsmash_root_t *root, uint32_t track_ID );
uint64_t lsmash_get_track_duration( lsmash_root_t *root, uint32_t track_ID );
uint32_t lsmash_get_last_sample_delta( lsmash_root_t *root, uint32_t track_ID );
uint32_t lsmash_get_start_time_offset( lsmash_root_t *root, uint32_t track_ID );
uint32_t lsmash_get_composition_to_decode_shift( lsmash_root_t *root, uint32_t track_ID );
uint32_t lsmash_get_media_timescale( lsmash_root_t *root, uint32_t track_ID );
uint32_t lsmash_get_movie_timescale( lsmash_root_t *root );

int lsmash_set_last_sample_delta( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_delta );
int lsmash_set_free( lsmash_root_t *root, uint8_t *data, uint64_t data_length );
int lsmash_set_tyrant_chapter( lsmash_root_t *root, char *file_name, int add_bom );

int lsmash_create_reference_chapter_track( lsmash_root_t *root, uint32_t track_ID, char *file_name );
int lsmash_create_object_descriptor( lsmash_root_t *root );

/* explicit timeline map (edit) */
int lsmash_create_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID, lsmash_edit_t edit );
int lsmash_modify_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID, uint32_t edit_number, lsmash_edit_t edit );
int lsmash_get_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID, uint32_t edit_number, lsmash_edit_t *edit );
int lsmash_delete_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID );
uint32_t lsmash_count_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID );

/* modification time */
int lsmash_update_media_modification_time( lsmash_root_t *root, uint32_t track_ID );
int lsmash_update_track_modification_time( lsmash_root_t *root, uint32_t track_ID );
int lsmash_update_movie_modification_time( lsmash_root_t *root );

uint16_t lsmash_pack_iso_language( char *iso_language );

/* fundamental functions to create and/or read movie */
lsmash_root_t *lsmash_open_movie( const char *filename, lsmash_file_mode mode );
void lsmash_initialize_movie_parameters( lsmash_movie_parameters_t *param );
int lsmash_set_movie_parameters( lsmash_root_t *root, lsmash_movie_parameters_t *param );
uint32_t lsmash_create_track( lsmash_root_t *root, lsmash_media_type media_type );
void lsmash_initialize_track_parameters( lsmash_track_parameters_t *param );
int lsmash_set_track_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_track_parameters_t *param );
void lsmash_initialize_media_parameters( lsmash_media_parameters_t *param );
int lsmash_set_media_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_media_parameters_t *param );
int lsmash_add_sample_entry( lsmash_root_t *root, uint32_t track_ID, void *summary );
lsmash_sample_t *lsmash_create_sample( uint32_t size );
int lsmash_sample_alloc( lsmash_sample_t *sample, uint32_t size );
void lsmash_delete_sample( lsmash_sample_t *sample );
int lsmash_append_sample( lsmash_root_t *root, uint32_t track_ID, lsmash_sample_t *sample );
int lsmash_flush_pooled_samples( lsmash_root_t *root, uint32_t track_ID, uint32_t last_sample_delta );
int lsmash_update_track_duration( lsmash_root_t *root, uint32_t track_ID, uint32_t last_sample_delta );
int lsmash_finish_movie( lsmash_root_t *root, lsmash_adhoc_remux_t* remux );
void lsmash_destroy_root( lsmash_root_t *root );
int lsmash_get_movie_parameters( lsmash_root_t *root, lsmash_movie_parameters_t *param );
uint32_t lsmash_get_track_ID( lsmash_root_t *root, uint32_t track_number );
int lsmash_get_track_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_track_parameters_t *param );
int lsmash_get_media_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_media_parameters_t *param );

int lsmash_create_fragment_movie( lsmash_root_t *root );
int lsmash_create_fragment_empty_duration( lsmash_root_t *root, uint32_t track_ID, uint32_t duration );

void lsmash_discard_boxes( lsmash_root_t *root );

void lsmash_delete_track( lsmash_root_t *root, uint32_t track_ID );
void lsmash_delete_tyrant_chapter( lsmash_root_t *root );

/* track_ID == 0 means copyright declaration applies to the entire presentation, not an entire track. */
int lsmash_set_copyright( lsmash_root_t *root, uint32_t track_ID, uint16_t ISO_language, char *notice );

int lsmash_convert_crop_into_clap( lsmash_crop_t crop, uint32_t width, uint32_t height, lsmash_clap_t *clap );
int lsmash_convert_clap_into_crop( lsmash_clap_t clap, uint32_t width, uint32_t height, lsmash_crop_t *crop );

/* functions to set up or get CODEC specific info */
lsmash_codec_specific_t *lsmash_create_codec_specific_data( lsmash_codec_specific_data_type type, lsmash_codec_specific_format format );
void lsmash_destroy_codec_specific_data( lsmash_codec_specific_t *specific );
int lsmash_add_codec_specific_data( lsmash_summary_t *summary, lsmash_codec_specific_t *specific );
lsmash_codec_specific_t *lsmash_convert_codec_specific_format( lsmash_codec_specific_t *specific, lsmash_codec_specific_format format );
lsmash_summary_t *lsmash_get_summary( lsmash_root_t *root, uint32_t track_ID, uint32_t description_number );
uint32_t lsmash_count_summary( lsmash_root_t *root, uint32_t track_ID );
lsmash_codec_specific_t *lsmash_get_codec_specific_data( lsmash_summary_t *summary, uint32_t extension_number );
uint32_t lsmash_count_codec_specific_data( lsmash_summary_t *summary );

#ifdef LSMASH_DEMUXER_ENABLED
int lsmash_print_movie( lsmash_root_t *root, const char *filename );

/* This function might output BOM on Windows. Make sure that this is the first function that outputs something to stdout. */
int lsmash_print_chapter_list( lsmash_root_t *root );

int lsmash_copy_timeline_map( lsmash_root_t *dst, uint32_t dst_track_ID, lsmash_root_t *src, uint32_t src_track_ID );
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
uint64_t lsmash_get_media_duration_from_media_timeline( lsmash_root_t *root, uint32_t track_ID );
lsmash_sample_t *lsmash_get_sample_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number );
int lsmash_get_sample_info_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number, lsmash_sample_t *sample );
int lsmash_get_sample_property_from_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number, lsmash_sample_property_t *prop );
int lsmash_check_sample_existence_in_media_timeline( lsmash_root_t *root, uint32_t track_ID, uint32_t sample_number );

int lsmash_set_media_timestamps( lsmash_root_t *root, uint32_t track_ID, lsmash_media_ts_list_t *ts_list );
int lsmash_get_media_timestamps( lsmash_root_t *root, uint32_t track_ID, lsmash_media_ts_list_t *ts_list );
void lsmash_delete_media_timestamps( lsmash_media_ts_list_t *ts_list );
int lsmash_get_max_sample_delay( lsmash_media_ts_list_t *ts_list, uint32_t *max_sample_delay );
void lsmash_sort_timestamps_decoding_order( lsmash_media_ts_list_t *ts_list );
void lsmash_sort_timestamps_composition_order( lsmash_media_ts_list_t *ts_list );
#endif

/* to facilitate to make exdata (typically DecoderSpecificInfo or AudioSpecificConfig). */
int lsmash_setup_AudioSpecificConfig( lsmash_audio_summary_t* summary );

lsmash_summary_t *lsmash_create_summary( lsmash_summary_type summary_type );
void lsmash_cleanup_summary( lsmash_summary_t *summary );

/****************************************************************************
 * Tools for creating CODEC Specific Information Extensions (Magic Cookies)
 ****************************************************************************/
/** MPEG-4 stream tools **/
/* objectTypeIndication */
typedef enum
{
    MP4SYS_OBJECT_TYPE_Forbidden                          = 0x00,   /* Forbidden */
    MP4SYS_OBJECT_TYPE_Systems_ISO_14496_1                = 0x01,   /* Systems ISO/IEC 14496-1
                                                                     * For all 14496-1 streams unless specifically indicated to the contrary.
                                                                     * Scene Description scenes, which are identified with StreamType=0x03, using
                                                                     * this object type value shall use the BIFSConfig. */
    MP4SYS_OBJECT_TYPE_Systems_ISO_14496_1_BIFSv2         = 0x02,   /* Systems ISO/IEC 14496-1
                                                                     * This object type shall be used, with StreamType=0x03, for Scene
                                                                     * Description streams that use the BIFSv2Config.
                                                                     * Its use with other StreamTypes is reserved. */
    MP4SYS_OBJECT_TYPE_Interaction_Stream                 = 0x03,   /* Interaction Stream */
    MP4SYS_OBJECT_TYPE_Extended_BIFS                      = 0x04,   /* Extended BIFS
                                                                     * Used, with StreamType=0x03, for Scene Description streams that use the BIFSConfigEx;
                                                                     * its use with other StreamTypes is reserved.
                                                                     * (Was previously reserved for MUCommandStream but not used for that purpose.) */
    MP4SYS_OBJECT_TYPE_AFX_Stream                         = 0x05,   /* AFX Stream
                                                                     * Used, with StreamType=0x03, for Scene Description streams that use the AFXConfig;
                                                                     * its use with other StreamTypes is reserved. */
    MP4SYS_OBJECT_TYPE_Font_Data_Stream                   = 0x06,   /* Font Data Stream */
    MP4SYS_OBJECT_TYPE_Synthetised_Texture                = 0x07,   /* Synthetised Texture */
    MP4SYS_OBJECT_TYPE_Text_Stream                        = 0x08,   /* Text Stream */
    MP4SYS_OBJECT_TYPE_Visual_ISO_14496_2                 = 0x20,   /* Visual ISO/IEC 14496-2 */
    MP4SYS_OBJECT_TYPE_Visual_H264_ISO_14496_10           = 0x21,   /* Visual ITU-T Recommendation H.264 | ISO/IEC 14496-10
                                                                     * The actual object types are within the DecoderSpecificInfo and defined in H.264 | 14496-10. */
    MP4SYS_OBJECT_TYPE_Parameter_Sets_H_264_ISO_14496_10  = 0x22,   /* Parameter Sets for ITU-T Recommendation H.264 | ISO/IEC 14496-10
                                                                     * The actual object types are within the DecoderSpecificInfo and defined in 14496-2. */
    MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3                  = 0x40,   /* Audio ISO/IEC 14496-3 (MPEG-4 Audio)
                                                                     * The actual object types are defined in 14496-3 and are in the DecoderSpecificInfo as specified in 14496-3. */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_Simple_Profile  = 0x60,   /* Visual ISO/IEC 13818-2 Simple Profile (MPEG-2 Video) */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_Main_Profile    = 0x61,   /* Visual ISO/IEC 13818-2 Main Profile */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_SNR_Profile     = 0x62,   /* Visual ISO/IEC 13818-2 SNR Profile */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_Spatial_Profile = 0x63,   /* Visual ISO/IEC 13818-2 Spatial Profile */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_High_Profile    = 0x64,   /* Visual ISO/IEC 13818-2 High Profile */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_422_Profile     = 0x65,   /* Visual ISO/IEC 13818-2 422 Profile */
    MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_Main_Profile     = 0x66,   /* Audio ISO/IEC 13818-7 Main Profile (MPEG-2 Audio)(AAC) */
    MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_LC_Profile       = 0x67,   /* Audio ISO/IEC 13818-7 LowComplexity Profile */
    MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_SSR_Profile      = 0x68,   /* Audio ISO/IEC 13818-7 Scaleable Sampling Rate Profile
                                                                     * For streams kinda 13818-7 the decoder specific information consists of the ADIF header if present
                                                                     * (or none if not present) and an access unit is a "raw_data_block()" as defined in 13818-7. */
    MP4SYS_OBJECT_TYPE_Audio_ISO_13818_3                  = 0x69,   /* Audio ISO/IEC 13818-3 (MPEG-2 BC-Audio)(redefined MPEG-1 Audio in MPEG-2)
                                                                     * For streams kinda 13818-3 the decoder specific information is empty since all necessary data is in the bitstream frames itself.
                                                                     * The access units in this case are the "frame()" bitstream element as is defined in 11172-3. */
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

    MP4SYS_OBJECT_TYPE_NONE                               = 0xFF,   /* no object type specified
                                                                     * Streams with this value with a StreamType indicating a systems stream (values 1,2,3,6,7,8,9)
                                                                     * shall be treated as if the ObjectTypeIndication had been set to 0x01. */
} lsmash_mp4sys_object_type_indication;

/* streamType */
typedef enum
{
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

/* Decoder Specific Information
 * an opaque container with information for a specific media decoder
 * The existence and semantics of decoder specific information depends on the values of streamType and objectTypeIndication. */
typedef struct lsmash_mp4sys_decoder_specific_info_tag lsmash_mp4sys_decoder_specific_info_t;

/* Note: bufferSizeDB, maxBitrate and avgBitrate are calculated internally when calling lsmash_finish_movie().
 *       You need not to set up them manually when muxing streams by L-SMASH. */
typedef struct
{
    lsmash_mp4sys_object_type_indication   objectTypeIndication;
    lsmash_mp4sys_stream_type              streamType;
    uint32_t                               bufferSizeDB;    /* the size of the decoding buffer for this elementary stream in byte */
    uint32_t                               maxBitrate;      /* the maximum bitrate in bits per second of the elementary stream in
                                                             * any time window of one second duration */
    uint32_t                               avgBitrate;      /* the average bitrate in bits per second of the elementary stream
                                                             * Set to 0 if the stream is encoded as variable bitrate. */
    lsmash_mp4sys_decoder_specific_info_t *dsi;             /* zero or one decoder specific information */
} lsmash_mp4sys_decoder_parameters_t;

int lsmash_set_mp4sys_decoder_specific_info( lsmash_mp4sys_decoder_parameters_t *param, uint8_t *payload, uint32_t payload_length );
void lsmash_destroy_mp4sys_decoder_specific_info( lsmash_mp4sys_decoder_parameters_t *param );
uint8_t *lsmash_create_mp4sys_decoder_config( lsmash_mp4sys_decoder_parameters_t *param, uint32_t *data_length );
/* Return MP4SYS_OBJECT_TYPE_Forbidden if objectTypeIndication is not found or there is an error to find it. */
lsmash_mp4sys_object_type_indication lsmash_mp4sys_get_object_type_indication( lsmash_summary_t *summary );
/* Return -1 if any error.
 * Even if the decoder specific information is not found, it is not an error since no decoder specific information is allowed for some stream formats. */
int lsmash_get_mp4sys_decoder_specific_info( lsmash_mp4sys_decoder_parameters_t *param, uint8_t **payload, uint32_t *payload_length );

/* AC-3 tools to make exdata (AC-3 specific info). */
typedef struct
{
    uint8_t fscod;          /* the same value as the fscod field in the AC-3 bitstream */
    uint8_t bsid;           /* the same value as the bsid field in the AC-3 bitstream */
    uint8_t bsmod;          /* the same value as the bsmod field in the AC-3 bitstream */
    uint8_t acmod;          /* the same value as the acmod field in the AC-3 bitstream */
    uint8_t lfeon;          /* the same value as the lfeon field in the AC-3 bitstream */
    uint8_t frmsizecod;     /* the same value as the frmsizecod field in the AC-3 bitstream */
} lsmash_ac3_specific_parameters_t;

int lsmash_setup_ac3_specific_parameters_from_syncframe( lsmash_ac3_specific_parameters_t *param, uint8_t *data, uint32_t data_length );
uint8_t *lsmash_create_ac3_specific_info( lsmash_ac3_specific_parameters_t *param, uint32_t *data_length );

/* Enhanced AC-3 tools to make exdata (Enhanced AC-3 specific info). */
typedef struct
{
    uint8_t  fscod;         /* the same value as the fscod field in the independent substream */
    uint8_t  fscod2;        /* Any user must not use this. */
    uint8_t  bsid;          /* the same value as the bsid field in the independent substream */
    uint8_t  bsmod;         /* the same value as the bsmod field in the independent substream
                             * If the bsmod field is not present in the independent substream, this field shall be set to 0. */
    uint8_t  acmod;         /* the same value as the acmod field in the independent substream */
    uint8_t  lfeon;         /* the same value as the lfeon field in the independent substream */
    uint8_t  num_dep_sub;   /* the number of dependent substreams that are associated with the independent substream */
    uint16_t chan_loc;      /* channel locations of dependent substreams associated with the independent substream
                             * This information is extracted from the chanmap field of each dependent substream. */
} lsmash_eac3_substream_info_t;

typedef struct
{
    uint16_t data_rate;     /* the data rate of the Enhanced AC-3 bitstream in kbit/s
                             * If the Enhanced AC-3 stream is variable bitrate, then this value indicates the maximum data rate of the stream. */
    uint8_t  num_ind_sub;   /* the number of independent substreams that are present in the Enhanced AC-3 bitstream
                             * The value of this field is one less than the number of independent substreams present
                             * and shall be in the range of 0 to 7, inclusive. */
    lsmash_eac3_substream_info_t independent_info[8];
} lsmash_eac3_specific_parameters_t;

int lsmash_setup_eac3_specific_parameters_from_frame( lsmash_eac3_specific_parameters_t *param, uint8_t *data, uint32_t data_length );
uint16_t lsmash_eac3_get_chan_loc_from_chanmap( uint16_t chanmap );
uint8_t *lsmash_create_eac3_specific_info( lsmash_eac3_specific_parameters_t *param, uint32_t *data_length );

/* DTS audio tools to make exdata (DTS specific info). */
typedef enum
{
    DTS_CORE_SUBSTREAM_CORE_FLAG = 0x00000001,
    DTS_CORE_SUBSTREAM_XXCH_FLAG = 0x00000002,
    DTS_CORE_SUBSTREAM_X96_FLAG  = 0x00000004,
    DTS_CORE_SUBSTREAM_XCH_FLAG  = 0x00000008,
    DTS_EXT_SUBSTREAM_CORE_FLAG  = 0x00000010,
    DTS_EXT_SUBSTREAM_XBR_FLAG   = 0x00000020,
    DTS_EXT_SUBSTREAM_XXCH_FLAG  = 0x00000040,
    DTS_EXT_SUBSTREAM_X96_FLAG   = 0x00000080,
    DTS_EXT_SUBSTREAM_LBR_FLAG   = 0x00000100,
    DTS_EXT_SUBSTREAM_XLL_FLAG   = 0x00000200,
} lsmash_dts_construction_flag;

typedef struct lsmash_dts_reserved_box_tag lsmash_dts_reserved_box_t;

typedef struct
{
    uint32_t DTSSamplingFrequency;  /* the maximum sampling frequency stored in the compressed audio stream
                                     * 'frequency', which is a member of lsmash_audio_summary_t, shall be set according to DTSSamplingFrequency of either:
                                     *   48000 for original sampling frequencies of 24000Hz, 48000Hz, 96000Hz or 192000Hz;
                                     *   44100 for original sampling frequencies of 22050Hz, 44100Hz, 88200Hz or 176400Hz;
                                     *   32000 for original sampling frequencies of 16000Hz, 32000Hz, 64000Hz or 128000Hz. */
    uint32_t maxBitrate;            /* the peak bit rate, in bits per second, of the audio elementary stream for the duration of the track,
                                     * including the core substream (if present) and all extension substreams.
                                     * If the stream is a constant bit rate, this parameter shall have the same value as avgBitrate.
                                     * If the maximum bit rate is unknown, this parameter shall be set to 0. */
    uint32_t avgBitrate;            /* the average bit rate, in bits per second, of the audio elementary stream for the duration of the track,
                                     * including the core substream and any extension substream that may be present. */
    uint8_t  pcmSampleDepth;        /* the bit depth of the rendered audio
                                     * The value is 16 or 24 bits. */
    uint8_t  FrameDuration;         /* the number of audio samples decoded in a complete audio access unit at DTSSamplingFrequency
                                     *   0: 512, 1: 1024, 2: 2048, 3: 4096 */
    uint8_t  StreamConstruction;    /* complete information on the existence and of location of extensions in any synchronized frame */
    uint8_t  CoreLFEPresent;        /* the presence of an LFE channel in the core
                                     *   0: none
                                     *   1: LFE exists */
    uint8_t  CoreLayout;            /* the channel layout of the core within the core substream
                                     * If no core substream exists, this parameter shall be ignored and ChannelLayout or
                                     * RepresentationType shall be used to determine channel configuration. */
    uint16_t CoreSize;              /* The size of a core substream AU in bytes.
                                     * If no core substream exists, CoreSize = 0. */
    uint8_t  StereoDownmix;         /* the presence of an embedded stereo downmix in the stream
                                     *   0: none
                                     *   1: embedded downmix present */
    uint8_t  RepresentationType;    /* This indicates special properties of the audio presentation.
                                     *   0: Audio asset designated for mixing with another audio asset
                                     *   2: Lt/Rt Encoded for matrix surround decoding
                                     *   3: Audio processed for headphone playback
                                     *   otherwise: Reserved
                                     * If ChannelLayout != 0, this value shall be ignored. */
    uint16_t ChannelLayout;         /* complete information on channels coded in the audio stream including core and extensions */
    uint8_t  MultiAssetFlag;        /* This flag shall set if the stream contains more than one asset.
                                     *   0: single asset
                                     *   1: multiple asset
                                     * When multiple assets exist, the remaining parameters only reflect the coding parameters of the first asset. */
    uint8_t  LBRDurationMod;        /* This flag indicates a special case of the LBR coding bandwidth, resulting in 1/3 or 2/3 band limiting.
                                     * If set to 1, LBR frame duration is 50 % larger than indicated in FrameDuration */
    lsmash_dts_reserved_box_t *box;
} lsmash_dts_specific_parameters_t;

int lsmash_setup_dts_specific_parameters_from_frame( lsmash_dts_specific_parameters_t *param, uint8_t *data, uint32_t data_length );
uint8_t lsmash_dts_get_stream_construction( lsmash_dts_construction_flag flags );
uint32_t lsmash_dts_get_codingname( lsmash_dts_specific_parameters_t *param );
uint8_t *lsmash_create_dts_specific_info( lsmash_dts_specific_parameters_t *param, uint32_t *data_length );
int lsmash_append_dts_reserved_box( lsmash_dts_specific_parameters_t *param, uint8_t *box_data, uint32_t box_size );
void lsmash_remove_dts_reserved_box( lsmash_dts_specific_parameters_t *param );

/* Apple Lossless Audio tools */
typedef struct
{
    uint32_t frameLength;       /* the frames per packet when no explicit frames per packet setting is present in the packet header
                                 * The encoder frames per packet can be explicitly set but for maximum compatibility,
                                 * the default encoder setting of 4096 should be used. */
    uint8_t  bitDepth;          /* the bit depth of the source PCM data (maximum value = 32) */
    uint8_t  numChannels;       /* the channel count (1 = mono, 2 = stereo, etc...)
                                 * When channel layout info is not provided in the Channel Layout extension,
                                 * a channel count > 2 describes a set of discreet channels with no specific ordering. */
    uint32_t maxFrameBytes;     /* the maximum size of an Apple Lossless packet within the encoded stream
                                 * Value of 0 indicates unknown. */
    uint32_t avgBitrate;        /* the average bit rate in bits per second of the Apple Lossless stream
                                 * Value of 0 indicates unknown. */
    uint32_t sampleRate;        /* sample rate of the encoded stream */
} lsmash_alac_specific_parameters_t;

uint8_t *lsmash_create_alac_specific_info( lsmash_alac_specific_parameters_t *param, uint32_t *data_length );

/* H.264 tools to make exdata (AVC specific info). */
typedef enum
{
    H264_PARAMETER_SET_TYPE_SPS    = 0,
    H264_PARAMETER_SET_TYPE_PPS    = 1,
    H264_PARAMETER_SET_TYPE_SPSEXT = 2,
} lsmash_h264_parameter_set_type;

typedef struct lsmash_h264_parameter_sets_tag lsmash_h264_parameter_sets_t;

typedef struct
{
    uint8_t                       AVCProfileIndication;     /* profile_idc in sequence parameter sets */
    uint8_t                       profile_compatibility;    /* constraint_set_flags in sequence parameter sets */
    uint8_t                       AVCLevelIndication;       /* maximum level_idc in sequence parameter sets */
    uint8_t                       lengthSizeMinusOne;       /* the length in bytes of the NALUnitLength field prior to NAL unit
                                                             * The value of this field shall be one of 0, 1, or 3
                                                             * corresponding to a length encoded with 1, 2, or 4 bytes, respectively.
                                                             * NALUnitLength indicates the size of a NAL unit measured in bytes,
                                                             * and includes the size of both the one byte NAL header and the EBSP payload
                                                             * but does not include the length field itself. */
    uint8_t                       chroma_format;            /* chroma_format_idc in sequence parameter sets */
    uint8_t                       bit_depth_luma_minus8;    /* bit_depth_luma_minus8 in sequence parameter sets */
    uint8_t                       bit_depth_chroma_minus8;  /* bit_depth_chroma_minus8 in sequence parameter sets */
    lsmash_h264_parameter_sets_t *parameter_sets;           /* sequence parameter sets */
} lsmash_h264_specific_parameters_t;

/* H.264 Bitrate information.
 * Though you need not to set these fields manually since lsmash_finish_movie() calls the function
 * that calculates these values internally, these fields are optional.
 * Therefore, if you want to add this info, append this as an extension via LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_H264_BITRATE at least. */
typedef struct
{
    uint32_t bufferSizeDB;  /* the size of the decoding buffer for the elementary stream in bytes */
    uint32_t maxBitrate;    /* the maximum rate in bits/second over any window of one second */
    uint32_t avgBitrate;    /* the average rate in bits/second over the entire presentation */
} lsmash_h264_bitrate_t;

int lsmash_setup_h264_specific_parameters_from_access_unit( lsmash_h264_specific_parameters_t *param, uint8_t *data, uint32_t data_length );
void lsmash_destroy_h264_parameter_sets( lsmash_h264_specific_parameters_t *param );
int lsmash_check_h264_parameter_set_appendable( lsmash_h264_specific_parameters_t *param, lsmash_h264_parameter_set_type ps_type, void *ps_data, uint32_t ps_length );
int lsmash_append_h264_parameter_set( lsmash_h264_specific_parameters_t *param, lsmash_h264_parameter_set_type ps_type, void *ps_data, uint32_t ps_length );
uint8_t *lsmash_create_h264_specific_info( lsmash_h264_specific_parameters_t *param, uint32_t *data_length );

/* VC-1 tools to make exdata (VC-1 specific info). */
typedef struct lsmash_vc1_header_tag lsmash_vc1_header_t;

typedef struct
{
    /* Note: multiple_sequence, multiple_entry, slice_present and bframe_present shall be decided through overall VC-1 bitstream. */
    uint8_t  profile;               /* the encoding profile used in the VC-1 bitstream
                                     *   0: simple profile (not supported)
                                     *   4: main profile   (not supported)
                                     *  12: advanced profile
                                     * Currently, only 12 for advanced profile is available. */
    uint8_t  level;                 /* the highest encoding level used in the VC-1 bitstream */
    uint8_t  cbr;                   /* 0: non-constant bitrate model
                                     * 1: constant bitrate model */
    uint8_t  interlaced;            /* 0: interlaced coding of frames is not used.
                                     * 1: frames may use interlaced coding. */
    uint8_t  multiple_sequence;     /* 0: the track contains no sequence headers (stored only in VC-1 specific info structure),
                                     *    or
                                     *    all sequence headers in the track are identical to the sequence header that is specified in the seqhdr field.
                                     *    In this case, random access points are samples that contain an entry-point header.
                                     * 1: the track may contain Sequence headers that are different from the sequence header specified in the seqhdr field.
                                     *    In this case, random access points are samples that contain both a sequence Header and an entry-point header. */
    uint8_t  multiple_entry;        /* 0: all entry-point headers in the track are identical to the entry-point header that is specified in the ephdr field.
                                     * 1: the track may contain entry-point headers that are different from the entry-point header specified in the ephdr field. */
    uint8_t  slice_present;         /* 0: frames are not coded as multiple slices.
                                     * 1: frames may be coded as multiple slices. */
    uint8_t  bframe_present;        /* 0: neither B-frames nor BI-frames are present in the track.
                                     * 1: B-frames or BI-frames may be present in the track. */
    uint32_t framerate;             /* the rounded frame rate (frames per second) of the track
                                     * Should be set to 0xffffffff if the frame rate is not known, unspecified, or non-constant. */
    lsmash_vc1_header_t *seqhdr;    /* a sequence header EBDU (mandatory) */
    lsmash_vc1_header_t *ephdr;     /* an entry-point header EBDU (mandatory) */
} lsmash_vc1_specific_parameters_t;

int lsmash_setup_vc1_specific_parameters_from_access_unit( lsmash_vc1_specific_parameters_t *param, uint8_t *data, uint32_t data_length );
void lsmash_destroy_vc1_headers( lsmash_vc1_specific_parameters_t *param );
int lsmash_put_vc1_header( lsmash_vc1_specific_parameters_t *param, void *hdr_data, uint32_t hdr_length );
uint8_t *lsmash_create_vc1_specific_info( lsmash_vc1_specific_parameters_t *param, uint32_t *data_length );

/* Sample scaling
 * Without this extension, video samples are scaled into the visual presentation region to fill it. */
typedef enum
{
    ISOM_SCALE_METHOD_FILL    = 1,
    ISOM_SCALE_METHOD_HIDDEN  = 2,
    ISOM_SCALE_METHOD_MEET    = 3,
    ISOM_SCALE_METHOD_SLICE_X = 4,
    ISOM_SCALE_METHOD_SLICE_Y = 5,
} lsmash_scale_method;

typedef struct
{
    uint8_t constraint_flag;            /* Upper 7-bits are reserved.
                                         * If this flag is set, all samples described by this sample entry shall be scaled
                                         * according to the method specified by the field 'scale_method'. */
    lsmash_scale_method scale_method;   /* The semantics of the values for scale_method are as specified for the 'fit' attribute of regions in SMIL 1.0. */
    int16_t display_center_x;
    int16_t display_center_y;
} lsmash_isom_sample_scale_t;

/* QuickTime Video CODEC tools */
typedef enum
{
    QT_COMPRESSION_QUALITY_LOSSLESS = 0x00000400,   /* only valid for spatial compression */
    QT_COMPRESSION_QUALITY_MAX      = 0x000003FF,
    QT_COMPRESSION_QUALITY_MIN      = 0x00000000,
    QT_COMPRESSION_QUALITY_LOW      = 0x00000100,
    QT_COMPRESSION_QUALITY_NORMAL   = 0x00000200,
    QT_COMPRESSION_QUALITY_HIGH     = 0x00000300
} lsmash_qt_compression_quality;

typedef struct
{
    int16_t                         revision_level;             /* version of the CODEC */
    int32_t                         vendor;                     /* whose CODEC */
    lsmash_qt_compression_quality   temporalQuality;            /* the temporal quality factor (0-1023) */
    lsmash_qt_compression_quality   spatialQuality;             /* the spatial quality factor (0-1024) */
    uint32_t                        horizontal_resolution;      /* a 16.16 fixed-point number containing the horizontal resolution of the image in pixels per inch. */
    uint32_t                        vertical_resolution;        /* a 16.16 fixed-point number containing the vertical resolution of the image in pixels per inch. */
    uint32_t                        dataSize;                   /* if known, the size of data for this descriptor */
    uint16_t                        frame_count;                /* frame per sample */
    int16_t                         color_table_ID;             /* color table ID
                                                                 * If this field is set to 0, the default color table should be used for the specified depth
                                                                 * If the color table ID is set to 0, a color table is contained within the sample description itself.
                                                                 * The color table immediately follows the color table ID field. */
} lsmash_qt_video_common_t;

typedef struct
{
    uint32_t level;     /* A fixed-point 16.16 number indicating the gamma level at which the image was captured. */
} lsmash_qt_gamma_t;

typedef enum
{
    QT_FIELEDS_SCAN_PROGRESSIVE = 1,    /* progressive scan */
    QT_FIELEDS_SCAN_INTERLACED  = 2,    /* 2:1 interlaced */
} lsmash_qt_number_of_fields;

/* field ordering for interlaced material */
typedef enum
{
    QT_FIELD_ORDERINGS_UNKNOWN                  = 0,
    QT_FIELD_ORDERINGS_TEMPORAL_TOP_FIRST       = 1,
    QT_FIELD_ORDERINGS_TEMPORAL_BOTTOM_FIRST    = 6,
    QT_FIELD_ORDERINGS_SPATIAL_FIRST_LINE_EARLY = 9,
    QT_FIELD_ORDERINGS_SPATIAL_FIRST_LINE_LATE  = 14
} lsmash_qt_field_orderings;

typedef struct
{
    lsmash_qt_number_of_fields fields;
    lsmash_qt_field_orderings  detail;
} lsmash_qt_field_info_t;

/* the native pixel format */
typedef enum
{
    QT_PIXEL_FORMAT_TYPE_1_MONOCHROME                       = 0x00000001,                           /* 1 bit indexed */
    QT_PIXEL_FORMAT_TYPE_2_INDEXED                          = 0x00000002,                           /* 2 bit indexed */
    QT_PIXEL_FORMAT_TYPE_4_INDEXED                          = 0x00000004,                           /* 4 bit indexed */
    QT_PIXEL_FORMAT_TYPE_8_INDEXED                          = 0x00000008,                           /* 8 bit indexed */
    QT_PIXEL_FORMAT_TYPE_1_INDEXED_GRAY_WHITE_IS_ZERO       = 0x00000021,                           /* 1 bit indexed gray, white is zero */
    QT_PIXEL_FORMAT_TYPE_2_INDEXED_GRAY_WHITE_IS_ZERO       = 0x00000022,                           /* 2 bit indexed gray, white is zero */
    QT_PIXEL_FORMAT_TYPE_4_INDEXED_GRAY_WHITE_IS_ZERO       = 0x00000024,                           /* 4 bit indexed gray, white is zero */
    QT_PIXEL_FORMAT_TYPE_8_INDEXED_GRAY_WHITE_IS_ZERO       = 0x00000028,                           /* 8 bit indexed gray, white is zero */
    QT_PIXEL_FORMAT_TYPE_16BE555                            = 0x00000010,                           /* 16 bit BE RGB 555 */
    QT_PIXEL_FORMAT_TYPE_16LE555                            = LSMASH_4CC( 'L', '5', '5', '5' ),     /* 16 bit LE RGB 555 */
    QT_PIXEL_FORMAT_TYPE_16LE5551                           = LSMASH_4CC( '5', '5', '5', '1' ),     /* 16 bit LE RGB 5551 */
    QT_PIXEL_FORMAT_TYPE_16BE565                            = LSMASH_4CC( 'B', '5', '6', '5' ),     /* 16 bit BE RGB 565 */
    QT_PIXEL_FORMAT_TYPE_16LE565                            = LSMASH_4CC( 'L', '5', '6', '5' ),     /* 16 bit LE RGB 565 */
    QT_PIXEL_FORMAT_TYPE_24RGB                              = 0x00000018,                           /* 24 bit RGB */
    QT_PIXEL_FORMAT_TYPE_24BGR                              = LSMASH_4CC( '2', '4', 'B', 'G' ),     /* 24 bit BGR */
    QT_PIXEL_FORMAT_TYPE_32ARGB                             = 0x00000020,                           /* 32 bit ARGB */
    QT_PIXEL_FORMAT_TYPE_32BGRA                             = LSMASH_4CC( 'B', 'G', 'R', 'A' ),     /* 32 bit BGRA */
    QT_PIXEL_FORMAT_TYPE_32ABGR                             = LSMASH_4CC( 'A', 'B', 'G', 'R' ),     /* 32 bit ABGR */
    QT_PIXEL_FORMAT_TYPE_32RGBA                             = LSMASH_4CC( 'R', 'G', 'B', 'A' ),     /* 32 bit RGBA */
    QT_PIXEL_FORMAT_TYPE_64ARGB                             = LSMASH_4CC( 'b', '6', '4', 'a' ),     /* 64 bit ARGB, 16-bit big-endian samples */
    QT_PIXEL_FORMAT_TYPE_48RGB                              = LSMASH_4CC( 'b', '4', '8', 'r' ),     /* 48 bit RGB, 16-bit big-endian samples */
    QT_PIXEL_FORMAT_TYPE_32_ALPHA_GRAY                      = LSMASH_4CC( 'b', '3', '2', 'a' ),     /* 32 bit AlphaGray, 16-bit big-endian samples, black is zero */
    QT_PIXEL_FORMAT_TYPE_16_GRAY                            = LSMASH_4CC( 'b', '1', '6', 'g' ),     /* 16 bit Grayscale, 16-bit big-endian samples, black is zero */
    QT_PIXEL_FORMAT_TYPE_30RGB                              = LSMASH_4CC( 'R', '1', '0', 'k' ),     /* 30 bit RGB, 10-bit big-endian samples, 2 unused padding bits (at least significant end) */
    QT_PIXEL_FORMAT_TYPE_422YpCbCr8                         = LSMASH_4CC( '2', 'v', 'u', 'y' ),     /* Component Y'CbCr 8-bit 4:2:2, ordered Cb Y'0 Cr Y'1 */
    QT_PIXEL_FORMAT_TYPE_4444YpCbCrA8                       = LSMASH_4CC( 'v', '4', '0', '8' ),     /* Component Y'CbCrA 8-bit 4:4:4:4, ordered Cb Y' Cr A */
    QT_PIXEL_FORMAT_TYPE_4444YpCbCrA8R                      = LSMASH_4CC( 'r', '4', '0', '8' ),     /* Component Y'CbCrA 8-bit 4:4:4:4, rendering format. full range alpha, zero biased YUV, ordered A Y' Cb Cr */
    QT_PIXEL_FORMAT_TYPE_4444AYpCbCr8                       = LSMASH_4CC( 'y', '4', '0', '8' ),     /* Component Y'CbCrA 8-bit 4:4:4:4, ordered A Y' Cb Cr, full range alpha, video range Y'CbCr */
    QT_PIXEL_FORMAT_TYPE_4444AYpCbCr16                      = LSMASH_4CC( 'y', '4', '1', '6' ),     /* Component Y'CbCrA 16-bit 4:4:4:4, ordered A Y' Cb Cr, full range alpha, video range Y'CbCr, 16-bit little-endian samples */
    QT_PIXEL_FORMAT_TYPE_444YpCbCr8                         = LSMASH_4CC( 'v', '3', '0', '8' ),     /* Component Y'CbCr 8-bit 4:4:4 */
    QT_PIXEL_FORMAT_TYPE_422YpCbCr16                        = LSMASH_4CC( 'v', '2', '1', '6' ),     /* Component Y'CbCr 10,12,14,16-bit 4:2:2 */
    QT_PIXEL_FORMAT_TYPE_422YpCbCr10                        = LSMASH_4CC( 'v', '2', '1', '0' ),     /* Component Y'CbCr 10-bit 4:2:2 */
    QT_PIXEL_FORMAT_TYPE_444YpCbCr10                        = LSMASH_4CC( 'v', '4', '1', '0' ),     /* Component Y'CbCr 10-bit 4:4:4 */
    QT_PIXEL_FORMAT_TYPE_420YpCbCr8_PLANAR                  = LSMASH_4CC( 'y', '4', '2', '0' ),     /* Planar Component Y'CbCr 8-bit 4:2:0 */
    QT_PIXEL_FORMAT_TYPE_420YpCbCr8_PLANAR_FULL_RANGE       = LSMASH_4CC( 'f', '4', '2', '0' ),     /* Planar Component Y'CbCr 8-bit 4:2:0, full range */
    QT_PIXEL_FORMAT_TYPE_422YpCbCr_4A_8_BIPLANAR            = LSMASH_4CC( 'a', '2', 'v', 'y' ),     /* First plane: Video-range Component Y'CbCr 8-bit 4:2:2, ordered Cb Y'0 Cr Y'1; second plane: alpha 8-bit 0-255 */
    QT_PIXEL_FORMAT_TYPE_420YpCbCr8_BIPLANAR_VIDEO_RANGE    = LSMASH_4CC( '4', '2', '0', 'v' ),     /* Bi-Planar Component Y'CbCr 8-bit 4:2:0, video-range (luma=[16,235] chroma=[16,240]) */
    QT_PIXEL_FORMAT_TYPE_420YpCbCr8_BIPLANAR_FULL_RANGE     = LSMASH_4CC( '4', '2', '0', 'f' ),     /* Bi-Planar Component Y'CbCr 8-bit 4:2:0, full-range (luma=[0,255] chroma=[1,255]) */
    QT_PIXEL_FORMAT_TYPE_422YpCbCr8_YUVS                    = LSMASH_4CC( 'y', 'u', 'v', 's' ),     /* Component Y'CbCr 8-bit 4:2:2, ordered Y'0 Cb Y'1 Cr */
    QT_PIXEL_FORMAT_TYPE_422YpCbCr8_FULL_RANGE              = LSMASH_4CC( 'y', 'u', 'v', 'f' ),     /* Component Y'CbCr 8-bit 4:2:2, full range, ordered Y'0 Cb Y'1 Cr */

    /* Developer specific FourCCs (from dispatch 20) */
    QT_PIXEL_FORMAT_TYPE_SOFTVOUT_SOFTCODEC                 = LSMASH_4CC( 's', 'o', 'f', 't' ),     /* Intermediary pixel format used by SoftVout and SoftCodec */
    QT_PIXEL_FORMAT_TYPE_VIEW_GRAPHICS                      = LSMASH_4CC( 'v', 'w', 'g', 'r' ),     /* Intermediary pixel format used by View Graphics */
    QT_PIXEL_FORMAT_TYPE_SGI                                = LSMASH_4CC( 'S', 'G', 'V', 'C' ),     /* Intermediary pixel format used by SGI */
} lsmash_qt_pixel_format;

typedef struct
{
    lsmash_qt_pixel_format pixel_format;    /* the native pixel format of an image */
} lsmash_qt_pixel_format_t;

/* Significant Bits Extension
 * mandatory extension for 'v216' (Uncompressed Y'CbCr, 10, 12, 14, or 16-bit-per-component 4:2:2) */
typedef struct
{
    uint8_t significantBits;    /* the number of significant bits per component */
} lsmash_qt_significant_bits_t;

/* QuickTime Audio CODEC tools */
typedef enum
{
    QT_AUDIO_COMPRESSION_ID_NOT_COMPRESSED            = 0,
    QT_AUDIO_COMPRESSION_ID_FIXED_COMPRESSION         = -1,
    QT_AUDIO_COMPRESSION_ID_VARIABLE_COMPRESSION      = -2,
    QT_AUDIO_COMPRESSION_ID_TWO_TO_ONE                = 1,
    QT_AUDIO_COMPRESSION_ID_EIGHT_TO_THREE            = 2,
    QT_AUDIO_COMPRESSION_ID_THREE_TO_ONE              = 3,
    QT_AUDIO_COMPRESSION_ID_SIX_TO_ONE                = 4,
    QT_AUDIO_COMPRESSION_ID_SIX_TO_ONE_PACKET_SIZE    = 8,
    QT_AUDIO_COMPRESSION_ID_THREE_TO_ONE_PACKET_SIZE  = 16,
} lsmash_qt_audio_compression_id;

typedef struct
{
    int16_t                        revision_level;      /* version of the CODEC */
    int32_t                        vendor;              /* whose CODEC */
    lsmash_qt_audio_compression_id compression_ID;
} lsmash_qt_audio_common_t;

/* Audio Channel Layout
 * This CODEC specific extension is for
 *   QuickTime Audio inside QuickTime file format
 *   and
 *   Apple Lossless Audio inside ISO Base Media file format.
 * When audio stream has 3 or more number of channels, this extension shall be present. */
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

typedef struct
{
    lsmash_channel_layout_tag channelLayoutTag;     /* channel layout */
    lsmash_channel_bitmap     channelBitmap;        /* Only available when layout_tag is set to QT_CHANNEL_LAYOUT_USE_CHANNEL_BITMAP. */
} lsmash_qt_audio_channel_layout_t;

/* QuickTime Audio Format Specific Flags
 * Some values are ignored i.e. as if treated as unspecified when you specify certain CODECs.
 * For instance, you specify QT_CODEC_TYPE_SOWT_AUDIO, then all these values are ignored.
 * These values are basically used for QT_CODEC_TYPE_LPCM_AUDIO.
 * The endiannes value can be used for QT_CODEC_TYPE_FL32_AUDIO, QT_CODEC_TYPE_FL64_AUDIO, QT_CODEC_TYPE_IN24_AUDIO and QT_CODEC_TYPE_IN32_AUDIO. */
typedef enum
{
    QT_AUDIO_FORMAT_FLAG_FLOAT            = 1,      /* Set for floating point, clear for integer. */
    QT_AUDIO_FORMAT_FLAG_BIG_ENDIAN       = 1<<1,   /* Set for big endian, clear for little endian. */
    QT_AUDIO_FORMAT_FLAG_SIGNED_INTEGER   = 1<<2,   /* Set for signed integer, clear for unsigned integer.
                                                     * This is only valid if QT_AUDIO_FORMAT_FLAG_FLOAT is clear. */
    QT_AUDIO_FORMAT_FLAG_PACKED           = 1<<3,   /* Set if the sample bits occupy the entire available bits for the channel,
                                                     * clear if they are high or low aligned within the channel. */
    QT_AUDIO_FORMAT_FLAG_ALIGNED_HIGH     = 1<<4,   /* Set if the sample bits are placed into the high bits of the channel, clear for low bit placement.
                                                     * This is only valid if QT_AUDIO_FORMAT_FLAG_PACKED is clear. */
    QT_AUDIO_FORMAT_FLAG_NON_INTERLEAVED  = 1<<5,   /* Set if the samples for each channel are located contiguously and the channels are layed out end to end,
                                                     * clear if the samples for each frame are layed out contiguously and the frames layed out end to end. */
    QT_AUDIO_FORMAT_FLAG_NON_MIXABLE      = 1<<6,   /* Set to indicate when a format is non-mixable.
                                                     * Note that this flag is only used when interacting with the HAL's stream format information.
                                                     * It is not a valid flag for any other uses. */
    QT_AUDIO_FORMAT_FLAG_ALL_CLEAR        = 1<<31,  /* Set if all the flags would be clear in order to preserve 0 as the wild card value. */

    QT_LPCM_FORMAT_FLAG_FLOAT             = QT_AUDIO_FORMAT_FLAG_FLOAT,
    QT_LPCM_FORMAT_FLAG_BIG_ENDIAN        = QT_AUDIO_FORMAT_FLAG_BIG_ENDIAN,
    QT_LPCM_FORMAT_FLAG_SIGNED_INTEGER    = QT_AUDIO_FORMAT_FLAG_SIGNED_INTEGER,
    QT_LPCM_FORMAT_FLAG_PACKED            = QT_AUDIO_FORMAT_FLAG_PACKED,
    QT_LPCM_FORMAT_FLAG_ALIGNED_HIGH      = QT_AUDIO_FORMAT_FLAG_ALIGNED_HIGH,
    QT_LPCM_FORMAT_FLAG_NON_INTERLEAVED   = QT_AUDIO_FORMAT_FLAG_NON_INTERLEAVED,
    QT_LPCM_FORMAT_FLAG_NON_MIXABLE       = QT_AUDIO_FORMAT_FLAG_NON_MIXABLE,
    QT_LPCM_FORMAT_FLAG_ALL_CLEAR         = QT_AUDIO_FORMAT_FLAG_ALL_CLEAR,

    /* These flags are set for Apple Lossless data that was sourced from N bit native endian signed integer data. */
    QT_ALAC_FORMAT_FLAG_16BIT_SOURCE_DATA = 1,
    QT_ALAC_FORMAT_FLAG_20BIT_SOURCE_DATA = 2,
    QT_ALAC_FORMAT_FLAG_24BIT_SOURCE_DATA = 3,
    QT_ALAC_FORMAT_FLAG_32BIT_SOURCE_DATA = 4,
} lsmash_qt_audio_format_specific_flag;

typedef struct
{
    lsmash_qt_audio_format_specific_flag format_flags;
} lsmash_qt_audio_format_specific_flags_t;

/* Global Header
 * Ut Video inside QuickTime file format requires this extension for storing CODEC specific information. */
typedef struct
{
    uint32_t header_size;
    uint8_t *header_data;
} lsmash_codec_global_header_t;

/****************************************************************************
 * iTunes Metadata
 ****************************************************************************/
typedef enum
{
    /* UTF String type */
    ITUNES_METADATA_ITEM_ALBUM_NAME                 = LSMASH_4CC( 0xA9, 'a', 'l', 'b' ),    /* Album Name */
    ITUNES_METADATA_ITEM_ARTIST                     = LSMASH_4CC( 0xA9, 'A', 'R', 'T' ),    /* Artist */
    ITUNES_METADATA_ITEM_USER_COMMENT               = LSMASH_4CC( 0xA9, 'c', 'm', 't' ),    /* User Comment */
    ITUNES_METADATA_ITEM_RELEASE_DATE               = LSMASH_4CC( 0xA9, 'd', 'a', 'y' ),    /* YYYY-MM-DD format string (may be incomplete, i.e. only year) */
    ITUNES_METADATA_ITEM_ENCODED_BY                 = LSMASH_4CC( 0xA9, 'e', 'n', 'c' ),    /* Person or company that encoded the recording */
    ITUNES_METADATA_ITEM_USER_GENRE                 = LSMASH_4CC( 0xA9, 'g', 'e', 'n' ),    /* User Genre user-specified string */
    ITUNES_METADATA_ITEM_0XA9_GROUPING              = LSMASH_4CC( 0xA9, 'g', 'r', 'p' ),    /* Grouping */
    ITUNES_METADATA_ITEM_LYRICS                     = LSMASH_4CC( 0xA9, 'l', 'y', 'r' ),    /* Lyrics */
    ITUNES_METADATA_ITEM_TITLE                      = LSMASH_4CC( 0xA9, 'n', 'a', 'm' ),    /* Title / Song Name */
    ITUNES_METADATA_ITEM_TRACK_SUBTITLE             = LSMASH_4CC( 0xA9, 's', 't', '3' ),    /* Track Sub-Title */
    ITUNES_METADATA_ITEM_ENCODING_TOOL              = LSMASH_4CC( 0xA9, 't', 'o', 'o' ),    /* Software which encoded the recording */
    ITUNES_METADATA_ITEM_COMPOSER                   = LSMASH_4CC( 0xA9, 'w', 'r', 't' ),    /* Composer */
    ITUNES_METADATA_ITEM_ALBUM_ARTIST               = LSMASH_4CC( 'a', 'A', 'R', 'T' ),     /* Artist for the whole album (if different than the individual tracks) */
    ITUNES_METADATA_ITEM_PODCAST_CATEGORY           = LSMASH_4CC( 'c', 'a', 't', 'g' ),     /* Podcast Category */
    ITUNES_METADATA_ITEM_COPYRIGHT                  = LSMASH_4CC( 'c', 'p', 'r', 't' ),     /* Copyright */
    ITUNES_METADATA_ITEM_DESCRIPTION                = LSMASH_4CC( 'd', 'e', 's', 'c' ),     /* Description (limited to 255 bytes) */
    ITUNES_METADATA_ITEM_GROUPING                   = LSMASH_4CC( 'g', 'r', 'u', 'p' ),     /* Grouping */
    ITUNES_METADATA_ITEM_PODCAST_KEYWORD            = LSMASH_4CC( 'k', 'e', 'y', 'w' ),     /* Podcast Keywords */
    ITUNES_METADATA_ITEM_LONG_DESCRIPTION           = LSMASH_4CC( 'l', 'd', 'e', 's' ),     /* Long Description */
    ITUNES_METADATA_ITEM_PURCHASE_DATE              = LSMASH_4CC( 'p', 'u', 'r', 'd' ),     /* Purchase Date */
    ITUNES_METADATA_ITEM_TV_EPISODE_ID              = LSMASH_4CC( 't', 'v', 'e', 'n' ),     /* TV Episode ID */
    ITUNES_METADATA_ITEM_TV_NETWORK                 = LSMASH_4CC( 't', 'v', 'n', 'n' ),     /* TV Network Name */
    ITUNES_METADATA_ITEM_TV_SHOW_NAME               = LSMASH_4CC( 't', 'v', 's', 'h' ),     /* TV Show Name */
    ITUNES_METADATA_ITEM_ITUNES_PURCHASE_ACCOUNT_ID = LSMASH_4CC( 'a', 'p', 'I', 'D' ),     /* iTunes Account Used for Purchase */
    ITUNES_METADATA_ITEM_ITUNES_SORT_ALBUM          = LSMASH_4CC( 's', 'o', 'a', 'l' ),     /* Sort Album */
    ITUNES_METADATA_ITEM_ITUNES_SORT_ARTIST         = LSMASH_4CC( 's', 'o', 'a', 'r' ),     /* Sort Artist */
    ITUNES_METADATA_ITEM_ITUNES_SORT_ALBUM_ARTIST   = LSMASH_4CC( 's', 'o', 'a', 'a' ),     /* Sort Album Artist */
    ITUNES_METADATA_ITEM_ITUNES_SORT_COMPOSER       = LSMASH_4CC( 's', 'o', 'c', 'o' ),     /* Sort Composer */
    ITUNES_METADATA_ITEM_ITUNES_SORT_NAME           = LSMASH_4CC( 's', 'o', 'n', 'm' ),     /* Sort Name */
    ITUNES_METADATA_ITEM_ITUNES_SORT_SHOW           = LSMASH_4CC( 's', 'o', 's', 'n' ),     /* Sort Show */

    /* Integer type
     * (X): X means length of bytes */
    ITUNES_METADATA_ITEM_EPISODE_GLOBAL_ID          = LSMASH_4CC( 'e', 'g', 'i', 'd' ),     /* (1) Episode Global Unique ID */
    ITUNES_METADATA_ITEM_PREDEFINED_GENRE           = LSMASH_4CC( 'g', 'n', 'r', 'e' ),     /* (4) Pre-defined Genre / Enumerated value from ID3 tag set, plus 1 */
    ITUNES_METADATA_ITEM_PODCAST_URL                = LSMASH_4CC( 'p', 'u', 'r', 'l' ),     /* (?) Podcast URL */
    ITUNES_METADATA_ITEM_CONTENT_RATING             = LSMASH_4CC( 'r', 't', 'n', 'g' ),     /* (1) Content Rating / Does song have explicit content? 0: none, 2: clean, 4: explicit */
    ITUNES_METADATA_ITEM_MEDIA_TYPE                 = LSMASH_4CC( 's', 't', 'i', 'k' ),     /* (1) Media Type */
    ITUNES_METADATA_ITEM_BEATS_PER_MINUTE           = LSMASH_4CC( 't', 'm', 'p', 'o' ),     /* (2) Beats Per Minute */
    ITUNES_METADATA_ITEM_TV_EPISODE                 = LSMASH_4CC( 't', 'v', 'e', 's' ),     /* (4) TV Episode */
    ITUNES_METADATA_ITEM_TV_SEASON                  = LSMASH_4CC( 't', 'v', 's', 'n' ),     /* (4) TV Season */
    ITUNES_METADATA_ITEM_ITUNES_ACCOUNT_TYPE        = LSMASH_4CC( 'a', 'k', 'I', 'D' ),     /* (1) iTunes Account Type / 0: iTunes, 1: AOL */
    ITUNES_METADATA_ITEM_ITUNES_ARTIST_ID           = LSMASH_4CC( 'a', 't', 'I', 'D' ),     /* (4) iTunes Artist ID */
    ITUNES_METADATA_ITEM_ITUNES_COMPOSER_ID         = LSMASH_4CC( 'c', 'm', 'I', 'D' ),     /* (4) iTunes Composer ID */
    ITUNES_METADATA_ITEM_ITUNES_CATALOG_ID          = LSMASH_4CC( 'c', 'n', 'I', 'D' ),     /* (4) iTunes Catalog ID */
    ITUNES_METADATA_ITEM_ITUNES_TV_GENRE_ID         = LSMASH_4CC( 'g', 'e', 'I', 'D' ),     /* (4) iTunes TV Genre ID */
    ITUNES_METADATA_ITEM_ITUNES_PLAYLIST_ID         = LSMASH_4CC( 'p', 'l', 'I', 'D' ),     /* (8) iTunes Playlist ID */
    ITUNES_METADATA_ITEM_ITUNES_COUNTRY_CODE        = LSMASH_4CC( 's', 'f', 'I', 'D' ),     /* (4) iTunes Country Code */

    /* Boolean type */
    ITUNES_METADATA_ITEM_DISC_COMPILATION           = LSMASH_4CC( 'c', 'p', 'i', 'l' ),     /* Disc Compilation / Is disc part of a compilation? 0: No, 1: Yes */
    ITUNES_METADATA_ITEM_HIGH_DEFINITION_VIDEO      = LSMASH_4CC( 'h', 'd', 'v', 'd' ),     /* High Definition Video / 0: No, 1: Yes */
    ITUNES_METADATA_ITEM_PODCAST                    = LSMASH_4CC( 'p', 'c', 's', 't' ),     /* Podcast / 0: No, 1: Yes */
    ITUNES_METADATA_ITEM_GAPLESS_PLAYBACK           = LSMASH_4CC( 'p', 'g', 'a', 'p' ),     /* Gapless Playback / 0: insert gap, 1: no gap */

    /* Binary type */
    ITUNES_METADATA_ITEM_COVER_ART                  = LSMASH_4CC( 'c', 'o', 'v', 'r' ),     /* One or more cover art images (JPEG/PNG/BMP data) */
    ITUNES_METADATA_ITEM_DISC_NUMBER                = LSMASH_4CC( 'd', 'i', 's', 'k' ),     /* Disc Number */
    ITUNES_METADATA_ITEM_TRACK_NUMBER               = LSMASH_4CC( 't', 'r', 'k', 'n' ),     /* Track Number */

    /* Custom type */
    ITUNES_METADATA_ITEM_CUSTOM                     = LSMASH_4CC( '-', '-', '-', '-' ),     /* Custom */
} lsmash_itunes_metadata_item;

typedef enum
{
    ITUNES_METADATA_TYPE_NONE    = 0,
    ITUNES_METADATA_TYPE_STRING  = 1,
    ITUNES_METADATA_TYPE_INTEGER = 2,
    ITUNES_METADATA_TYPE_BOOLEAN = 3,
    ITUNES_METADATA_TYPE_BINARY  = 4,
} lsmash_itunes_metadata_type;

typedef enum
{
    ITUNES_METADATA_SUBTYPE_IMPLICIT = 0,   /* for use with tags for which no type needs to be indicated because only one type is allowed */
    ITUNES_METADATA_SUBTYPE_UTF8     = 1,   /* without any count or null terminator */
    ITUNES_METADATA_SUBTYPE_UTF16    = 2,   /* also known as UTF-16BE */
    ITUNES_METADATA_SUBTYPE_SJIS     = 3,   /* deprecated unless it is needed for special Japanese characters */
    ITUNES_METADATA_SUBTYPE_HTML     = 6,   /* the HTML file header specifies which HTML version */
    ITUNES_METADATA_SUBTYPE_XML      = 7,   /* the XML header must identify the DTD or schemas */
    ITUNES_METADATA_SUBTYPE_UUID     = 8,   /* also known as GUID; stored as 16 bytes in binary (valid as an ID) */
    ITUNES_METADATA_SUBTYPE_ISRC     = 9,   /* stored as UTF-8 text (valid as an ID) */
    ITUNES_METADATA_SUBTYPE_MI3P     = 10,  /* stored as UTF-8 text (valid as an ID) */
    ITUNES_METADATA_SUBTYPE_GIF      = 12,  /* (deprecated) a GIF image */
    ITUNES_METADATA_SUBTYPE_JPEG     = 13,  /* in a JFIF wrapper */
    ITUNES_METADATA_SUBTYPE_PNG      = 14,  /* in a PNG wrapper */
    ITUNES_METADATA_SUBTYPE_URL      = 15,  /* absolute, in UTF-8 characters */
    ITUNES_METADATA_SUBTYPE_DURATION = 16,  /* in milliseconds, a 32-bit integer */
    ITUNES_METADATA_SUBTYPE_TIME     = 17,  /* in UTC, counting seconds since midnight on 1 January, 1904; 32 or 64 bits */
    ITUNES_METADATA_SUBTYPE_GENRES   = 18,  /* a list of values from the enumerated set */
    ITUNES_METADATA_SUBTYPE_INTEGER  = 21,  /* A signed big-endian integer in 1,2,3,4 or 8 bytes */
    ITUNES_METADATA_SUBTYPE_RIAAPA   = 24,  /* RIAA Parental advisory; -1=no, 1=yes, 0=unspecified. 8-bit integer */
    ITUNES_METADATA_SUBTYPE_UPC      = 25,  /* Universal Product Code, in text UTF-8 format (valid as an ID) */
    ITUNES_METADATA_SUBTYPE_BMP      = 27,  /* Windows bitmap format graphics */
} lsmash_itunes_metadata_subtype;

typedef union
{
    char            *string;    /* for ITUNES_METADATA_TYPE_STRING (UTF-8 string) */
    uint64_t         integer;   /* for ITUNES_METADATA_TYPE_INTEGER */
    lsmash_boolean_t boolean;   /* for ITUNES_METADATA_TYPE_BOOLEAN */
    /* for ITUNES_METADATA_TYPE_BINARY */
    struct
    {
        lsmash_itunes_metadata_subtype subtype;
        uint32_t                       size;
        uint8_t                       *data;
    } binary;
} lsmash_itunes_metadata_value_t;

typedef struct
{
    /* When 'item' is specified as ITUNES_METADATA_ITEM_CUSTOM, 'type' and 'meaning' is mandatory while 'name' is optionally valid.
     * Otherwise 'type', 'meaning' and 'name' are just ignored. 'value' is always mandatory. */
    lsmash_itunes_metadata_item    item;
    lsmash_itunes_metadata_type    type;
    lsmash_itunes_metadata_value_t value;
    char                          *meaning;
    char                          *name;
} lsmash_itunes_metadata_t;

int lsmash_set_itunes_metadata( lsmash_root_t *root, lsmash_itunes_metadata_t metadata );
int lsmash_get_itunes_metadata( lsmash_root_t *root, uint32_t metadata_number, lsmash_itunes_metadata_t *metadata );
uint32_t lsmash_count_itunes_metadata( lsmash_root_t *root );

#undef PRIVATE

#endif
