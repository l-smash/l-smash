/*****************************************************************************
 * isom.h:
 *****************************************************************************
 * Copyright (C) 2010 L-SMASH project
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

#ifndef ISOM_H
#define ISOM_H

#ifndef __MINGW32__
#define _FILE_OFFSET_BITS 64
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#ifdef __MINGW32__
#define fseek fseeko64
#define ftell ftello64
#endif

#include "utils.h"

#define ISOM_MAX( a, b ) ((a) > (b) ? (a) : (b))
#define ISOM_MIN( a, b ) ((a) < (b) ? (a) : (b))

/* If size is 1, then largesize is actual size.
 * If size is 0, then this box is the last one in the file. This is useful for pipe I/O.
 * usertype is for uuid. */
#define ISOM_BASE \
    uint64_t size; \
    uint32_t type; \
    uint8_t  *usertype

#define ISOM_DEFAULT_BOX_HEADER_SIZE 8
#define ISOM_DEFAULT_FULLBOX_HEADER_SIZE 12
#define ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE 16

/* Box header */
typedef struct
{
    ISOM_BASE;
} isom_base_header_t;

/* FullBox header */
typedef struct
{
    ISOM_BASE;
    uint8_t  version;   /* basically, version is either 0 or 1 */
    uint32_t flags;     /* flags is 24 bits */
} isom_full_header_t;

/* File Type Box */
typedef struct
{
    isom_base_header_t base_header;
    uint32_t major_brand;           /* brand identifier */
    uint32_t minor_version;         /* the minor version of the major brand */
    uint32_t *compatible_brands;    /* a list, to the end of the box, of brands */

    uint32_t brand_count;
} isom_ftyp_t;

/* Track Header Box */
typedef struct
{
    /* version is either 0 or 1
     * flags
     *      0x000001: Track_enabled
     *      0x000002: Track_in_movie
     *      0x000004: Track_in_preview */
#define ISOM_TRACK_ENABLED 0x000001
#define ISOM_TRACK_IN_MOVIE 0x000002
#define ISOM_TRACK_IN_PREVIEW 0x000004
    isom_full_header_t full_header;
    /* version == 0: uint64_t -> uint32_t */
    uint64_t creation_time;
    uint64_t modification_time;
    uint32_t track_ID;
    uint32_t reserved1;
    uint64_t duration;  /* the duration of this track expressed in the time-scale indicated in the mvhd */
    /* */
    uint32_t reserved2[2];
    int16_t  layer;
    int16_t  alternate_group;
    int16_t  volume;            /* fixed point 8.8 number. 0x0100 is full volume. */
    uint16_t reserved3;
    int32_t  matrix[9];         /* transformation matrix for the video */
    /* track's visual presentation size */
    uint32_t width;             /* fixed point 16.16 number */
    uint32_t height;            /* fixed point 16.16 number */
    /* */
} isom_tkhd_t;

/* Track Clean Aperture Dimensions Box
 * A presentation mode where clap and pasp are reflected. */
typedef struct
{
    isom_full_header_t full_header;
    uint32_t width;     /* fixed point 16.16 number */
    uint32_t height;    /* fixed point 16.16 number */
} isom_clef_t;

/* Track Production Aperture Dimensions Box
 * A presentation mode where pasp is reflected. */
typedef struct
{
    isom_full_header_t full_header;
    uint32_t width;     /* fixed point 16.16 number */
    uint32_t height;    /* fixed point 16.16 number */
} isom_prof_t;

/* Track Encoded Pixels Dimensions Box
 * A presentation mode where clap and pasp are not reflected. */
typedef struct
{
    isom_full_header_t full_header;
    uint32_t width;     /* fixed point 16.16 number */
    uint32_t height;    /* fixed point 16.16 number */
} isom_enof_t;

/* Track Aperture Mode Dimensions Box */
typedef struct
{
    isom_base_header_t base_header;
    isom_clef_t *clef;      /* Track Clean Aperture Dimensions Box */
    isom_prof_t *prof;      /* Track Production Aperture Dimensions Box */
    isom_enof_t *enof;      /* Track Encoded Pixels Dimensions Box */
} isom_tapt_t;

/* Edit List Box */
typedef struct
{
#define ISOM_NORMAL_EDIT (1<<16)
#define ISOM_DWELL_EDIT 0
#define ISOM_EMPTY_EDIT -1
    /* version == 0: 64bits -> 32bits */
    uint64_t segment_duration;  /* the duration of this edit expressed in the time-scale indicated in the mvhd */
    int64_t  media_time;        /* the starting composition time within the media of this edit segment
                                 * If this field is set to -1, it is an empty edit. */
    int32_t  media_rate;        /* 16.16 fixed-point number */
} isom_elst_entry_t;

typedef struct
{
    isom_full_header_t full_header;     /* version is either 0 or 1 */
    lsmash_entry_list_t *list;
} isom_elst_t;

/* Edit Box */
typedef struct
{
    isom_base_header_t base_header;
    isom_elst_t *elst;     /* Edit List Box */
} isom_edts_t;

/* Track Reference Box */
typedef struct
{
    isom_base_header_t base_header;
    uint32_t *track_ID;         /* track_IDs of reference tracks / Zero value must not be used */

        uint32_t ref_count;     /* number of reference tracks */
} isom_tref_type_t;

typedef struct
{
    isom_base_header_t base_header;
    isom_tref_type_t *type;     /* Track Reference Type Box */

        uint32_t type_count;    /* number of reference types */
} isom_tref_t;

/* Media Header Box */
typedef struct
{
    isom_full_header_t full_header;     /* version is either 0 or 1 */
    /* version == 0: uint64_t -> uint32_t */
    uint64_t creation_time;
    uint64_t modification_time;
    uint32_t timescale;     /* time-scale for this media */
    uint64_t duration;      /* the duration of this media expressed in the time-scale indicated in this box */
    /* */
#define ISOM_LANG( lang ) ((((lang[0]-0x60)&0x1f)<<10) | (((lang[1]-0x60)&0x1f)<<5) | ((lang[2]-0x60)&0x1f))
    uint16_t language;      /* ISOM: ISO-639-2/T language codes. The first bit is 0.
                             *       Each character is packed as the difference between its ASCII value and 0x60.
                             * QTFF: Macintosh language codes is usually used.
                             *       Mac's value is less than 0x800 while ISO's value is 0x800 or greater. */
    uint16_t pre_defined;
} isom_mdhd_t;

/* Handler Reference Box */
typedef struct
{
    /* This box is in Media Box or Meta Box */
    isom_full_header_t full_header;
    uint32_t type;      /* ISOM: pre_difined = 0
                         * QT: 'mhlr' for Media Handler Reference Box and 'dhlr' for Data Handler Reference Box  */
    uint32_t subtype;   /* ISOM and QT: when present in Media Handler Reference Box, this field defines the type of media data
                         * QT: when present in Data Handler Reference Box, this field defines the data reference type */
    uint32_t reserved[3];
    uint8_t *name;      /* ISOM: a null-terminated string in UTF-8 characters
                         * QT: Pascal string */

    uint32_t name_length;
} isom_hdlr_t;

/** Media Information Header Boxes **/
/* Video Media Header Box */
typedef struct
{
    isom_full_header_t full_header;     /* flags is 1 */
    uint16_t graphicsmode;              /* template: graphicsmode = 0 */
    uint16_t opcolor[3];                /* template: opcolor = { 0, 0, 0 } */
} isom_vmhd_t;

/* Sound Media Header Box */
typedef struct
{
    isom_full_header_t full_header;
    int16_t balance;    /* a fixed-point 8.8 number that places mono audio tracks in a stereo space. template: balance = 0 */
    uint16_t reserved;
} isom_smhd_t;

/* Hint Media Header Box */
typedef struct
{
    isom_full_header_t full_header;
    uint16_t maxPDUsize;
    uint16_t avgPDUsize;
    uint32_t maxbitrate;
    uint32_t avgbitrate;
    uint32_t reserved;
} isom_hmhd_t;

/* Null Media Header Box */
typedef struct
{
    /* Streams other than visual and audio may use a Null Media Header Box */
    isom_full_header_t full_header;     /* flags is currently all zero */
} isom_nmhd_t;

/* Generic Media Information Box */
typedef struct
{
    isom_full_header_t full_header;
    uint16_t graphicsmode;
    uint16_t opcolor[3];
    int16_t balance;        /* This field is nomally set to 0. */
    uint16_t reserved;      /* Reserved for use by Apple. Set this field to 0. */
} isom_gmin_t;

typedef struct
{
    isom_base_header_t base_header;
    int32_t matrix[9];      /* Unkown fields. Default values are probably:
                             * { 0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000 } */
} isom_text_t;

/* Generic Media Information Header Box */
typedef struct
{
    isom_base_header_t base_header;
    isom_gmin_t *gmin;      /* Generic Media Information Box */
    isom_text_t *text;
} isom_gmhd_t;
/** **/

/* Data Reference Box */
typedef struct
{
    /* This box is DataEntryUrlBox or DataEntryUrnBox */
    isom_full_header_t full_header;     /* flags == 0x000001 means that the media data is in the same file
                                         * as the Movie Box containing this data reference. */
    char *name;     /* only for DataEntryUrnBox */
    char *location;

    uint32_t name_length;
    uint32_t location_length;
} isom_dref_entry_t;

typedef struct
{
    isom_full_header_t full_header;
    lsmash_entry_list_t *list;
} isom_dref_t;

/* Data Information Box */
typedef struct
{
    /* This box is in Media Information Box or Meta Box */
    isom_base_header_t base_header;
    isom_dref_t *dref;      /* Data Reference Box */
} isom_dinf_t;

/** Sample Description **/
/* Bit Rate Box */
typedef struct
{
    isom_base_header_t base_header;
    uint32_t bufferSizeDB;  /* the size of the decoding buffer for the elementary stream in bytes */
    uint32_t maxBitrate;    /* the maximum rate in bits/second over any window of one second */
    uint32_t avgBitrate;    /* the average rate in bits/second over the entire presentation */
} isom_btrt_t;

/* Pixel Aspect Ratio Box */
typedef struct
{
    isom_base_header_t base_header;
    uint32_t hSpacing;
    uint32_t vSpacing;
} isom_pasp_t;

/* Clean Aperture Box */
typedef struct
{
    isom_base_header_t base_header;
    uint32_t cleanApertureWidthN;
    uint32_t cleanApertureWidthD;
    uint32_t cleanApertureHeightN;
    uint32_t cleanApertureHeightD;
    int32_t  horizOffN;
    uint32_t horizOffD;
    int32_t  vertOffN;
    uint32_t vertOffD;
} isom_clap_t;

/* Sample Scale Box */
typedef struct
{
#define ISOM_SCALING_METHOD_FILL    1
#define ISOM_SCALING_METHOD_HIDDEN  2
#define ISOM_SCALING_METHOD_MEET    3
#define ISOM_SCALING_METHOD_SLICE_X 4
#define ISOM_SCALING_METHOD_SLICE_Y 5
    isom_full_header_t full_header;
    uint8_t constraint_flag;    /* Upper 7-bits are reserved.
                                 * If this flag is set, all samples described by this sample entry shall be scaled
                                 * according to the method specified by the field 'scale_method'. */
    uint8_t scale_method;       /* The semantics of the values for scale_method are as specified for the 'fit' attribute of regions in SMIL 1.0. */
    int16_t display_center_x;
    int16_t display_center_y;
} isom_stsl_t;

/* Sample Entry */
#define ISOM_SAMPLE_ENTRY \
    isom_base_header_t base_header; \
    uint8_t reserved[6]; \
    uint16_t data_reference_index;

typedef struct
{
    ISOM_SAMPLE_ENTRY;
} isom_sample_entry_t;

/* Visual Sample Entry */
#define ISOM_VISUAL_SAMPLE_ENTRY \
    ISOM_SAMPLE_ENTRY; \
    uint16_t pre_defined1; \
    uint16_t reserved1; \
    uint32_t pre_defined2[3]; \
    /* pixel counts that the codec will deliver */ \
    uint16_t width; \
    uint16_t height; \
    /* */ \
    uint32_t horizresolution;   /* 16.16 fixed-point / template: horizresolution = 0x00480000 / 72 dpi */ \
    uint32_t vertresolution;    /* 16.16 fixed-point / template: vertresolution = 0x00480000 / 72 dpi */ \
    uint32_t reserved2; \
    uint16_t frame_count;       /* frame per sample / template: frame_count = 1 */ \
    char compressorname[33];    /* a fixed 32-byte field, with the first byte set to the number of bytes to be displayed */ \
    uint16_t depth;             /* template: depth = 0x0018 \
                                 * According to 14496-15:2010, \
                                 *  0x0018: colour with no alpha \
                                 *  0x0028: grayscale with no alpha \
                                 *  0x0020: gray or colour with alpha */ \
    int16_t pre_defined3;       /* template: pre_defined = -1 */ \
    isom_clap_t *clap;          /* Clean Aperture Box / optional */ \
    isom_pasp_t *pasp;          /* Pixel Aspect Ratio Box / optional */ \
    isom_stsl_t *stsl;          /* Sample Scale Box / optional */

typedef struct
{
    ISOM_VISUAL_SAMPLE_ENTRY;
} isom_visual_entry_t;

/* Audio Sample Entry */
#define ISOM_AUDIO_SAMPLE_ENTRY \
    ISOM_SAMPLE_ENTRY; \
    uint32_t reserved1[2]; \
    uint16_t channelcount;  /* template: channelcount = 2 */ \
    uint16_t samplesize;    /* template: samplesize = 16 */ \
    uint16_t pre_defined; \
    uint16_t reserved2; \
    uint32_t samplerate;    /* 16.16 fixed-point number */

typedef struct
{
    ISOM_AUDIO_SAMPLE_ENTRY;
    uint32_t exdata_length;
    void *exdata;
} isom_audio_entry_t;

/* Hint Sample Entry */
#define ISOM_HINT_SAMPLE_ENTRY \
    ISOM_SAMPLE_ENTRY; \
    uint8_t *data;

typedef struct
{
    ISOM_HINT_SAMPLE_ENTRY;
    uint32_t data_length;
} isom_hint_entry_t;

/* Metadata Sample Entry */
#define ISOM_METADATA_SAMPLE_ENTRY \
    ISOM_SAMPLE_ENTRY;

typedef struct
{
    ISOM_METADATA_SAMPLE_ENTRY;
} isom_metadata_entry_t;

/* Sample Description Box */
typedef struct
{
    isom_full_header_t full_header;
    lsmash_entry_list_t *list;
} isom_stsd_t;
/** **/

/* Decoding Time to Sample Box */
typedef struct
{
    uint32_t sample_count;
    uint32_t sample_delta;      /* DTS[n+1] = DTS[n] + sample_delta[n] */
} isom_stts_entry_t;

typedef struct
{
    isom_full_header_t full_header;
    lsmash_entry_list_t *list;
} isom_stts_t;

/* Composition Time to Sample Box */
typedef struct
{
    uint32_t sample_count;
    uint32_t sample_offset;     /* CTS[n] = DTS[n] + sample_offset[n] */
} isom_ctts_entry_t;

typedef struct
{
    /* ISOM: if version is 1, sample_offset is signed 32bit integer.
     * QT: sample_offset is always signed 32bit integer. */
    isom_full_header_t full_header;
    lsmash_entry_list_t *list;
} isom_ctts_t;

/* Composition to Decode Box (Composition Shift Least Greatest Box) */
typedef struct
{
    isom_full_header_t full_header;
    int32_t compositionToDTSShift;
    int32_t leastDecodeToDisplayDelta;      /* the smallest sample_offset */
    int32_t greatestDecodeToDisplayDelta;   /* the largest sample_offset */
    int32_t compositionStartTime;           /* the smallest CTS for any sample */
    int32_t compositionEndTime;             /* the CTS plus the composition duration, of the sample with the largest CTS */
} isom_cslg_t;

/* Sample Size Box */
typedef struct
{
    uint32_t entry_size;
} isom_stsz_entry_t;

typedef struct
{
    isom_full_header_t full_header;
    uint32_t sample_size;       /* If this field is set to 0, then the samples have different sizes. */
    uint32_t sample_count;
    lsmash_entry_list_t *list;  /* available if sample_size == 0 */
} isom_stsz_t;

/* Sync Sample Box */
typedef struct
{
    uint32_t sample_number;
} isom_stss_entry_t;

typedef struct
{
    isom_full_header_t full_header;
    lsmash_entry_list_t *list;
} isom_stss_t;

/* Partial Sync Sample Box */
typedef struct
{
    uint32_t sample_number;
} isom_stps_entry_t;

typedef struct
{
    isom_full_header_t full_header;
    lsmash_entry_list_t *list;
} isom_stps_t;

/* Independent and Disposable Samples Box */
typedef struct
{
#define ISOM_SAMPLE_LEADING_UNKOWN         0
#define ISOM_SAMPLE_IS_UNDECODABLE_LEADING 1
#define ISOM_SAMPLE_IS_NOT_LEADING         2
#define ISOM_SAMPLE_IS_DECODABLE_LEADING   3
#define ISOM_SAMPLE_INDEPENDENCY_UNKOWN    0
#define ISOM_SAMPLE_IS_INDEPENDENT         1
#define ISOM_SAMPLE_IS_NOT_INDEPENDENT     2
#define ISOM_SAMPLE_DISPOSABLE_UNKOWN      0
#define ISOM_SAMPLE_IS_NOT_DISPOSABLE      1
#define ISOM_SAMPLE_IS_DISPOSABLE          2
#define ISOM_SAMPLE_REDUNDANCY_UNKOWN      0
#define ISOM_SAMPLE_HAS_REDUNDANCY         1
#define ISOM_SAMPLE_HAS_NO_REDUNDANCY      2
    unsigned is_leading            : 2;     /* leading */
    unsigned sample_depends_on     : 2;     /* independency */
    unsigned sample_is_depended_on : 2;     /* disposable */
    unsigned sample_has_redundancy : 2;     /* redundancy */
} isom_sdtp_entry_t;

typedef struct
{
    isom_full_header_t full_header;
    /* According to the specification, the size of the table, sample_count, doesn't exist in this box.
     * Instead of this, it is taken from the sample_count in the stsz or the stz2 box. */
    lsmash_entry_list_t *list;
} isom_sdtp_t;

/* Sample To Chunk Box */
typedef struct
{
    uint32_t first_chunk;
    uint32_t samples_per_chunk;
    uint32_t sample_description_index;
} isom_stsc_entry_t;

typedef struct
{
    isom_full_header_t full_header;
    lsmash_entry_list_t *list;
} isom_stsc_t;

/* Chunk Offset Box */
typedef struct
{
    uint32_t chunk_offset;
} isom_stco_entry_t;

typedef struct
{
    /* for large presentations */
    uint64_t chunk_offset;
} isom_co64_entry_t;

typedef struct
{
    isom_full_header_t full_header;
    lsmash_entry_list_t *list;

    uint8_t large_presentation;
} isom_stco_t; /* share with co64 box */

/* Sample to Group Box */
typedef struct
{
    uint32_t sample_count;              /* the number of consecutive samples with the same sample group descriptor */
    uint32_t group_description_index;   /* the index of the sample group entry which describes the samples in this group */
} isom_sbgp_entry_t;

typedef struct
{
    isom_full_header_t full_header;
    uint32_t grouping_type;     /* Links it to its sample group description table with the same value for grouping type. */
    lsmash_entry_list_t *list;
} isom_sbgp_t;

/* Sample Group Description Box */
/* description_length are available only if version == 1 and default_length == 0. */
typedef struct
{
    /* grouping_type is 'roll' */
    // uint32_t description_length;
    int16_t roll_distance;  /* the number of samples that must be decoded in order for a sample to be decoded correctly */
} isom_roll_entry_t;  /* Roll Recovery Entry */

typedef struct
{
    isom_full_header_t full_header;
    uint32_t grouping_type;     /* an integer that identifies the sbgp that is associated with this sample group description */
    uint32_t default_length;    /* the length of every group entry (if the length is constant), or zero (if it is variable)
                                 * This field is available only if version == 1. */
    lsmash_entry_list_t *list;
} isom_sgpd_t;

/* Sample Table Box */
typedef struct
{
    isom_base_header_t base_header;
    isom_stsd_t *stsd;      /* Sample Description Box */
    isom_stts_t *stts;      /* Decoding Time to Sample Box */
    isom_ctts_t *ctts;      /* Composition Time to Sample Box */
    isom_cslg_t *cslg;      /* Composition to Decode Box (Composition Shift Least Greatest Box) / optional */
    isom_stss_t *stss;      /* Sync Sample Box */
    isom_stps_t *stps;      /* Partial Sync Sample Box / This box is defined by QuickTime file format */
    isom_sdtp_t *sdtp;      /* Independent and Disposable Samples Box / optional */
    isom_stsc_t *stsc;      /* Sample To Chunk Box */
    isom_stsz_t *stsz;      /* Sample Size Box */
    isom_stco_t *stco;      /* Chunk Offset Box */
    isom_sbgp_t *sbgp;      /* Sample To Group Box / optional */
    isom_sgpd_t *sgpd;      /* Sample Group Description Box / optional */

    uint32_t grouping_count;
} isom_stbl_t;

/* Media Information Box */
typedef struct
{
    isom_base_header_t base_header;
    /* Media Information Header Boxes */
    isom_vmhd_t *vmhd;      /* Video Media Header Box */
    isom_smhd_t *smhd;      /* Sound Media Header Box */
    isom_hmhd_t *hmhd;      /* Hint Media Header Box */
    isom_nmhd_t *nmhd;      /* Null Media Header Box */
    isom_gmhd_t *gmhd;      /* Generic Media Information Header Box / This box is defined by QuickTime file format */
    /* */
    isom_hdlr_t *hdlr;      /* Data Handler Reference Box / This box is defined by QuickTime file format
                             * Note: this box must come before Data Information Box. */
    isom_dinf_t *dinf;      /* Data Information Box */
    isom_stbl_t *stbl;      /* Sample Table Box */
} isom_minf_t;

/* Media Box */
typedef struct
{
    isom_base_header_t base_header;
    isom_mdhd_t *mdhd;      /* Media Header Box */
    isom_hdlr_t *hdlr;      /* ISOM: Handler Reference Box / QT: Media Handler Reference Box
                             * Note: this box must come before Media Information Box. */
    isom_minf_t *minf;      /* Media Information Box */
} isom_mdia_t;

/* Movie Header Box */
typedef struct
{
    isom_full_header_t full_header;     /* version is either 0 or 1 */
    /* version == 0: uint64_t -> uint32_t */
    uint64_t creation_time;
    uint64_t modification_time;
    uint32_t timescale;         /* time-scale for the entire presentation */
    uint64_t duration;          /* the duration of the longest track */
    /* */
    int32_t  rate;              /* fixed point 16.16 number. 0x00010000 is normal forward playback. */
    int16_t  volume;            /* fixed point 8.8 number. 0x0100 is full volume. */
    char     reserved[10];
    int32_t  matrix[9];         /* transformation matrix for the video */
    uint32_t pre_defined[6];
    uint32_t next_track_ID;     /* larger than the largest track-ID in use */
} isom_mvhd_t;

/* Media Data Box */
typedef struct
{
    isom_base_header_t base_header;     /* If size is 0, then this box is the last box. */

        uint64_t placeholder_pos;       /* placeholder position for largesize */
} isom_mdat_t;

/* Free Space Box */
typedef struct
{
    isom_base_header_t base_header;     /* type is 'free' or 'skip' */
    uint32_t length;
    uint8_t *data;
} isom_free_t;

typedef isom_free_t isom_skip_t;

/*** extended by ISO IEC 14496-14 (MP4 file format) ***/
/* Object Descriptor Box
 * Note that this box is mandatory under 14496-1:2001 (mp41) while not mandatory under 14496-14:2003 (mp42). */
struct mp4sys_ObjectDescriptor_t; /* FIXME: I think these structs using mp4sys should be placed in isom.c */
typedef struct
{
    isom_full_header_t full_header;
    struct mp4sys_ObjectDescriptor_t *OD;
} isom_iods_t;

/** Sample Description Boxes **/
/* ES Descriptor Box */
struct mp4sys_ES_Descriptor_t; /* FIXME: I think these structs using mp4sys should be placed in isom.c */
typedef struct
{
    isom_full_header_t full_header;
    struct mp4sys_ES_Descriptor_t *ES;
} isom_esds_t;

/* MP4 Visual Sample Entry */
typedef struct
{
    ISOM_VISUAL_SAMPLE_ENTRY;
    isom_esds_t *esds;
} isom_mp4v_entry_t;

/* Mpeg Sample Entry */
typedef struct
{
    ISOM_SAMPLE_ENTRY;
    isom_esds_t *esds;
} isom_mp4s_entry_t;
/** **/
/*** ***/

/*** extended by ISO IEC 14496-15 (AVC file format) ***/
/* Parameter Set Entry */
typedef struct
{
    uint8_t parameterSetLength;
    uint8_t *parameterSetNALUnit;
} isom_avcC_ps_entry_t;

typedef struct
{
#define ISOM_REQUIRES_AVCC_EXTENSION( x ) ((x) == 100 || (x) == 110 || (x) == 122 || (x) == 144)
    isom_base_header_t base_header;
    uint8_t configurationVersion;                   /* 1 */
    uint8_t AVCProfileIndication;                   /* profile_idc in SPS */
    uint8_t profile_compatibility;
    uint8_t AVCLevelIndication;                     /* level_idc in SPS */
    uint8_t lengthSizeMinusOne;                     /* in bytes of the NALUnitLength field. upper 6-bits are reserved as 111111b */
    uint8_t numOfSequenceParameterSets;             /* upper 3-bits are reserved as 111b */
    lsmash_entry_list_t *sequenceParameterSets;     /* SPSs */
    uint8_t numOfPictureParameterSets;
    lsmash_entry_list_t *pictureParameterSets;      /* PPSs */
    /* if( ISOM_REQUIRES_AVCC_EXTENSION( AVCProfileIndication ) ) */
    uint8_t chroma_format;                          /* chroma_format_idc in SPS / upper 6-bits are reserved as 111111b */
    uint8_t bit_depth_luma_minus8;                  /* shall be in the range of 0 to 4 / upper 5-bits are reserved as 11111b */
    uint8_t bit_depth_chroma_minus8;                /* shall be in the range of 0 to 4 / upper 5-bits are reserved as 11111b */
    uint8_t numOfSequenceParameterSetExt;
    lsmash_entry_list_t *sequenceParameterSetExt;   /* SPSExts */
    /* */
} isom_avcC_t;

/* AVC Sample Entry */
typedef struct
{
    ISOM_VISUAL_SAMPLE_ENTRY;
    isom_avcC_t *avcC;         /* AVCDecoderConfigurationRecord */
    isom_btrt_t *btrt;         /* MPEG4BitRateBox / optional */
    // isom_m4ds_t *m4ds;        /* MPEG4ExtensionDescriptorsBox / optional */
} isom_avc_entry_t;
/*** ***/

/* Text Sample Entry */
typedef struct
{
    ISOM_SAMPLE_ENTRY;
    int32_t displayFlags;
    int32_t textJustification;
    uint16_t bgColor[3];            /* background RGB color */
    /* defaultTextBox */
    int16_t top;
    int16_t left;
    int16_t bottom;
    int16_t right;
    /* defaultStyle */
    int32_t scrpStartChar;          /* starting character position */
    int16_t scrpHeight;
    int16_t scrpAscent;
    int16_t scrpFont;
    uint16_t scrpFace;              /* only first 8-bits are used */
    int16_t scrpSize;
    uint16_t scrpColor[3];          /* foreground RGB color */
    /* defaultFontName is Pascal string */
    uint8_t font_name_length;
    char *font_name;
} isom_text_entry_t;

typedef struct
{
    uint16_t font_ID;
    /* Pascal string */
    uint8_t font_name_length;
    char *font_name;
} isom_font_record_t;

typedef struct
{
    isom_base_header_t base_header;
    /* FontRecord 
     * entry_count is uint16_t. */
    lsmash_entry_list_t *list;
} isom_ftab_t;

typedef struct
{
    ISOM_SAMPLE_ENTRY;
    uint32_t displayFlags;
    int8_t horizontal_justification;
    int8_t vertical_justification;
    uint8_t background_color_rgba[4];
    /* BoxRecord default_text_box */
    int16_t top;
    int16_t left;
    int16_t bottom;
    int16_t right;
    /* StyleRecord default_style */
    uint16_t startChar;     /* always 0 */
    uint16_t endChar;       /* always 0 */
    uint16_t font_ID;
    uint8_t face_style_flags;
    uint8_t font_size;
    uint8_t text_color_rgba[4];
    /* Font Table Box font_table */
    isom_ftab_t *ftab;
} isom_tx3g_entry_t;

/* Chapter List Box
 * This box is NOT defined in the ISO/MPEG-4 specs. */
typedef struct
{
    uint64_t start_time;    /* expressed in 100 nanoseconds */
    /* Chapter name is Pascal string */
    uint8_t chapter_name_length;
    char *chapter_name;
} isom_chpl_entry_t;

typedef struct
{
    isom_full_header_t full_header;     /* version is 1 */
    uint8_t reserved;
    lsmash_entry_list_t *list;
} isom_chpl_t;

/* User Data Box */
typedef struct
{
    isom_base_header_t base_header;
    isom_chpl_t *chpl;      /* Chapter List Box */
} isom_udta_t;

/* Movie Box */
typedef struct
{
    isom_base_header_t base_header;
    isom_mvhd_t       *mvhd;        /* Movie Header Box */
    isom_iods_t       *iods;
    lsmash_entry_list_t *trak_list; /* Track Box List */
    isom_udta_t       *udta;        /* User Data Box */
} isom_moov_t;

/* ROOT */
typedef struct
{
    isom_ftyp_t *ftyp;      /* File Type Box */
    isom_moov_t *moov;      /* Movie Box */
    isom_mdat_t *mdat;      /* Media Data Box */
    isom_free_t *free;      /* Free Space Box */

        lsmash_bs_t *bs;                /* bytestream manager */
        double max_chunk_duration;      /* max duration per chunk in seconds */
        uint8_t qt_compatible;          /* compatibility with QuickTime file format */
        uint8_t isom_compatible;        /* compatibility with ISO Base Media file format */
        uint8_t mp4_version1;           /* compatibility with MP4 ver.1 file format */
        uint8_t mp4_version2;           /* compatibility with MP4 ver.2 file format */
        uint8_t max_3gpp_version;       /* maximum 3GPP version */
} isom_root_t;

/** Track Box **/
typedef struct
{
    uint32_t chunk_number;              /* chunk number */
    uint32_t sample_description_index;  /* sample description index */
    uint64_t first_dts;                 /* the first DTS in chunk */
    lsmash_entry_list_t *pool;          /* samples pooled to interleave */
} isom_chunk_t;

typedef struct
{
    uint64_t dts;
    uint64_t cts;
} isom_timestamp_t;

typedef struct
{
    isom_sbgp_entry_t *sample_to_group;     /* the address corresponding to the entry in Sample to Group Box */
    isom_roll_entry_t *roll_recovery;       /* the address corresponding to the roll recovery entry in Sample Group Description Box */
    uint32_t first_sample;                  /* number of the first sample of the group */
    uint32_t recovery_point;
    uint8_t delimited;                      /* the flag if the sample_count is determined */
    uint8_t described;                      /* the flag if the group description is determined */
} isom_roll_group_t;

typedef struct
{
    // uint32_t grouping_type;
    lsmash_entry_list_t *pool;        /* grouping pooled to delimit and describe */
} isom_grouping_t;

typedef struct
{
    isom_chunk_t chunk;
    isom_timestamp_t timestamp;
    isom_grouping_t roll;
} isom_cache_t;

typedef struct
{
    isom_base_header_t base_header;
    isom_tkhd_t *tkhd;          /* Track Header Box */
    isom_tapt_t *tapt;          /* Track Aperture Mode Dimensions Box / This box is defined in QuickTime file format */
    isom_edts_t *edts;          /* Edit Box */
    isom_tref_t *tref;          /* Track Reference Box */
    isom_mdia_t *mdia;          /* Media Box */
    isom_udta_t *udta;          /* User Data Box */

    isom_root_t *root;          /* go to root */
    isom_mdat_t *mdat;          /* go to referenced mdat box */
    isom_cache_t *cache;
    uint32_t related_track_ID;
    uint8_t is_chapter;
} isom_trak_entry_t;
/** **/


#define ISOM_4CC( a, b, c, d ) (((a)<<24) | ((b)<<16) | ((c)<<8) | (d))

enum isom_box_code
{
    ISOM_BOX_TYPE_ID32 = ISOM_4CC( 'I', 'D', '3', '2' ),
    ISOM_BOX_TYPE_ALBM = ISOM_4CC( 'a', 'l', 'b', 'm' ),
    ISOM_BOX_TYPE_AUTH = ISOM_4CC( 'a', 'u', 't', 'h' ),
    ISOM_BOX_TYPE_BPCC = ISOM_4CC( 'b', 'p', 'c', 'c' ),
    ISOM_BOX_TYPE_BUFF = ISOM_4CC( 'b', 'u', 'f', 'f' ),
    ISOM_BOX_TYPE_BXML = ISOM_4CC( 'b', 'x', 'm', 'l' ),
    ISOM_BOX_TYPE_CCID = ISOM_4CC( 'c', 'c', 'i', 'd' ),
    ISOM_BOX_TYPE_CDEF = ISOM_4CC( 'c', 'd', 'e', 'f' ),
    ISOM_BOX_TYPE_CLSF = ISOM_4CC( 'c', 'l', 's', 'f' ),
    ISOM_BOX_TYPE_CMAP = ISOM_4CC( 'c', 'm', 'a', 'p' ),
    ISOM_BOX_TYPE_CO64 = ISOM_4CC( 'c', 'o', '6', '4' ),
    ISOM_BOX_TYPE_COLR = ISOM_4CC( 'c', 'o', 'l', 'r' ),
    ISOM_BOX_TYPE_CPRT = ISOM_4CC( 'c', 'p', 'r', 't' ),
    ISOM_BOX_TYPE_CSLG = ISOM_4CC( 'c', 's', 'l', 'g' ),
    ISOM_BOX_TYPE_CTTS = ISOM_4CC( 'c', 't', 't', 's' ),
    ISOM_BOX_TYPE_CVRU = ISOM_4CC( 'c', 'v', 'r', 'u' ),
    ISOM_BOX_TYPE_DCFD = ISOM_4CC( 'd', 'c', 'f', 'D' ),
    ISOM_BOX_TYPE_DINF = ISOM_4CC( 'd', 'i', 'n', 'f' ),
    ISOM_BOX_TYPE_DREF = ISOM_4CC( 'd', 'r', 'e', 'f' ),
    ISOM_BOX_TYPE_DSCP = ISOM_4CC( 'd', 's', 'c', 'p' ),
    ISOM_BOX_TYPE_DSGD = ISOM_4CC( 'd', 's', 'g', 'd' ),
    ISOM_BOX_TYPE_DSTG = ISOM_4CC( 'd', 's', 't', 'g' ),
    ISOM_BOX_TYPE_EDTS = ISOM_4CC( 'e', 'd', 't', 's' ),
    ISOM_BOX_TYPE_ELST = ISOM_4CC( 'e', 'l', 's', 't' ),
    ISOM_BOX_TYPE_FECI = ISOM_4CC( 'f', 'e', 'c', 'i' ),
    ISOM_BOX_TYPE_FECR = ISOM_4CC( 'f', 'e', 'c', 'r' ),
    ISOM_BOX_TYPE_FIIN = ISOM_4CC( 'f', 'i', 'i', 'n' ),
    ISOM_BOX_TYPE_FIRE = ISOM_4CC( 'f', 'i', 'r', 'e' ),
    ISOM_BOX_TYPE_FPAR = ISOM_4CC( 'f', 'p', 'a', 'r' ),
    ISOM_BOX_TYPE_FREE = ISOM_4CC( 'f', 'r', 'e', 'e' ),
    ISOM_BOX_TYPE_FRMA = ISOM_4CC( 'f', 'r', 'm', 'a' ),
    ISOM_BOX_TYPE_FTYP = ISOM_4CC( 'f', 't', 'y', 'p' ),
    ISOM_BOX_TYPE_GITN = ISOM_4CC( 'g', 'i', 't', 'n' ),
    ISOM_BOX_TYPE_GNRE = ISOM_4CC( 'g', 'n', 'r', 'e' ),
    ISOM_BOX_TYPE_GRPI = ISOM_4CC( 'g', 'r', 'p', 'i' ),
    ISOM_BOX_TYPE_HDLR = ISOM_4CC( 'h', 'd', 'l', 'r' ),
    ISOM_BOX_TYPE_HMHD = ISOM_4CC( 'h', 'm', 'h', 'd' ),
    ISOM_BOX_TYPE_ICNU = ISOM_4CC( 'i', 'c', 'n', 'u' ),
    ISOM_BOX_TYPE_IDAT = ISOM_4CC( 'i', 'd', 'a', 't' ),
    ISOM_BOX_TYPE_IHDR = ISOM_4CC( 'i', 'h', 'd', 'r' ),
    ISOM_BOX_TYPE_IINF = ISOM_4CC( 'i', 'i', 'n', 'f' ),
    ISOM_BOX_TYPE_ILOC = ISOM_4CC( 'i', 'l', 'o', 'c' ),
    ISOM_BOX_TYPE_IMIF = ISOM_4CC( 'i', 'm', 'i', 'f' ),
    ISOM_BOX_TYPE_INFU = ISOM_4CC( 'i', 'n', 'f', 'u' ),
    ISOM_BOX_TYPE_IODS = ISOM_4CC( 'i', 'o', 'd', 's' ),
    ISOM_BOX_TYPE_IPHD = ISOM_4CC( 'i', 'p', 'h', 'd' ),
    ISOM_BOX_TYPE_IPMC = ISOM_4CC( 'i', 'p', 'm', 'c' ),
    ISOM_BOX_TYPE_IPRO = ISOM_4CC( 'i', 'p', 'r', 'o' ),
    ISOM_BOX_TYPE_IREF = ISOM_4CC( 'i', 'r', 'e', 'f' ),
    ISOM_BOX_TYPE_JP   = ISOM_4CC( 'j', 'p', ' ', ' ' ),
    ISOM_BOX_TYPE_JP2C = ISOM_4CC( 'j', 'p', '2', 'c' ),
    ISOM_BOX_TYPE_JP2H = ISOM_4CC( 'j', 'p', '2', 'h' ),
    ISOM_BOX_TYPE_JP2I = ISOM_4CC( 'j', 'p', '2', 'i' ),
    ISOM_BOX_TYPE_KYWD = ISOM_4CC( 'k', 'y', 'w', 'd' ),
    ISOM_BOX_TYPE_LOCI = ISOM_4CC( 'l', 'o', 'c', 'i' ),
    ISOM_BOX_TYPE_LRCU = ISOM_4CC( 'l', 'r', 'c', 'u' ),
    ISOM_BOX_TYPE_MDAT = ISOM_4CC( 'm', 'd', 'a', 't' ),
    ISOM_BOX_TYPE_MDHD = ISOM_4CC( 'm', 'd', 'h', 'd' ),
    ISOM_BOX_TYPE_MDIA = ISOM_4CC( 'm', 'd', 'i', 'a' ),
    ISOM_BOX_TYPE_MDRI = ISOM_4CC( 'm', 'd', 'r', 'i' ),
    ISOM_BOX_TYPE_MECO = ISOM_4CC( 'm', 'e', 'c', 'o' ),
    ISOM_BOX_TYPE_MEHD = ISOM_4CC( 'm', 'e', 'h', 'd' ),
    ISOM_BOX_TYPE_M7HD = ISOM_4CC( 'm', '7', 'h', 'd' ),
    ISOM_BOX_TYPE_MERE = ISOM_4CC( 'm', 'e', 'r', 'e' ),
    ISOM_BOX_TYPE_META = ISOM_4CC( 'm', 'e', 't', 'a' ),
    ISOM_BOX_TYPE_MFHD = ISOM_4CC( 'm', 'f', 'h', 'd' ),
    ISOM_BOX_TYPE_MFRA = ISOM_4CC( 'm', 'f', 'r', 'a' ),
    ISOM_BOX_TYPE_MFRO = ISOM_4CC( 'm', 'f', 'r', 'o' ),
    ISOM_BOX_TYPE_MINF = ISOM_4CC( 'm', 'i', 'n', 'f' ),
    ISOM_BOX_TYPE_MJHD = ISOM_4CC( 'm', 'j', 'h', 'd' ),
    ISOM_BOX_TYPE_MOOF = ISOM_4CC( 'm', 'o', 'o', 'f' ),
    ISOM_BOX_TYPE_MOOV = ISOM_4CC( 'm', 'o', 'o', 'v' ),
    ISOM_BOX_TYPE_MVCG = ISOM_4CC( 'm', 'v', 'c', 'g' ),
    ISOM_BOX_TYPE_MVCI = ISOM_4CC( 'm', 'v', 'c', 'i' ),
    ISOM_BOX_TYPE_MVEX = ISOM_4CC( 'm', 'v', 'e', 'x' ),
    ISOM_BOX_TYPE_MVHD = ISOM_4CC( 'm', 'v', 'h', 'd' ),
    ISOM_BOX_TYPE_MVRA = ISOM_4CC( 'm', 'v', 'r', 'a' ),
    ISOM_BOX_TYPE_NMHD = ISOM_4CC( 'n', 'm', 'h', 'd' ),
    ISOM_BOX_TYPE_OCHD = ISOM_4CC( 'o', 'c', 'h', 'd' ),
    ISOM_BOX_TYPE_ODAF = ISOM_4CC( 'o', 'd', 'a', 'f' ),
    ISOM_BOX_TYPE_ODDA = ISOM_4CC( 'o', 'd', 'd', 'a' ),
    ISOM_BOX_TYPE_ODHD = ISOM_4CC( 'o', 'd', 'h', 'd' ),
    ISOM_BOX_TYPE_ODHE = ISOM_4CC( 'o', 'd', 'h', 'e' ),
    ISOM_BOX_TYPE_ODRB = ISOM_4CC( 'o', 'd', 'r', 'b' ),
    ISOM_BOX_TYPE_ODRM = ISOM_4CC( 'o', 'd', 'r', 'm' ),
    ISOM_BOX_TYPE_ODTT = ISOM_4CC( 'o', 'd', 't', 't' ),
    ISOM_BOX_TYPE_OHDR = ISOM_4CC( 'o', 'h', 'd', 'r' ),
    ISOM_BOX_TYPE_PADB = ISOM_4CC( 'p', 'a', 'd', 'b' ),
    ISOM_BOX_TYPE_PAEN = ISOM_4CC( 'p', 'a', 'e', 'n' ),
    ISOM_BOX_TYPE_PCLR = ISOM_4CC( 'p', 'c', 'l', 'r' ),
    ISOM_BOX_TYPE_PDIN = ISOM_4CC( 'p', 'd', 'i', 'n' ),
    ISOM_BOX_TYPE_PERF = ISOM_4CC( 'p', 'e', 'r', 'f' ),
    ISOM_BOX_TYPE_PITM = ISOM_4CC( 'p', 'i', 't', 'm' ),
    ISOM_BOX_TYPE_RES  = ISOM_4CC( 'r', 'e', 's', ' ' ),
    ISOM_BOX_TYPE_RESC = ISOM_4CC( 'r', 'e', 's', 'c' ),
    ISOM_BOX_TYPE_RESD = ISOM_4CC( 'r', 'e', 's', 'd' ),
    ISOM_BOX_TYPE_RTNG = ISOM_4CC( 'r', 't', 'n', 'g' ),
    ISOM_BOX_TYPE_SBGP = ISOM_4CC( 's', 'b', 'g', 'p' ),
    ISOM_BOX_TYPE_SCHI = ISOM_4CC( 's', 'c', 'h', 'i' ),
    ISOM_BOX_TYPE_SCHM = ISOM_4CC( 's', 'c', 'h', 'm' ),
    ISOM_BOX_TYPE_SDEP = ISOM_4CC( 's', 'd', 'e', 'p' ),
    ISOM_BOX_TYPE_SDHD = ISOM_4CC( 's', 'd', 'h', 'd' ),
    ISOM_BOX_TYPE_SDTP = ISOM_4CC( 's', 'd', 't', 'p' ),
    ISOM_BOX_TYPE_SDVP = ISOM_4CC( 's', 'd', 'v', 'p' ),
    ISOM_BOX_TYPE_SEGR = ISOM_4CC( 's', 'e', 'g', 'r' ),
    ISOM_BOX_TYPE_SGPD = ISOM_4CC( 's', 'g', 'p', 'd' ),
    ISOM_BOX_TYPE_SINF = ISOM_4CC( 's', 'i', 'n', 'f' ),
    ISOM_BOX_TYPE_SKIP = ISOM_4CC( 's', 'k', 'i', 'p' ),
    ISOM_BOX_TYPE_SMHD = ISOM_4CC( 's', 'm', 'h', 'd' ),
    ISOM_BOX_TYPE_SRMB = ISOM_4CC( 's', 'r', 'm', 'b' ),
    ISOM_BOX_TYPE_SRMC = ISOM_4CC( 's', 'r', 'm', 'c' ),
    ISOM_BOX_TYPE_SRPP = ISOM_4CC( 's', 'r', 'p', 'p' ),
    ISOM_BOX_TYPE_STBL = ISOM_4CC( 's', 't', 'b', 'l' ),
    ISOM_BOX_TYPE_STCO = ISOM_4CC( 's', 't', 'c', 'o' ),
    ISOM_BOX_TYPE_STDP = ISOM_4CC( 's', 't', 'd', 'p' ),
    ISOM_BOX_TYPE_STSC = ISOM_4CC( 's', 't', 's', 'c' ),
    ISOM_BOX_TYPE_STSD = ISOM_4CC( 's', 't', 's', 'd' ),
    ISOM_BOX_TYPE_STSH = ISOM_4CC( 's', 't', 's', 'h' ),
    ISOM_BOX_TYPE_STSS = ISOM_4CC( 's', 't', 's', 's' ),
    ISOM_BOX_TYPE_STSZ = ISOM_4CC( 's', 't', 's', 'z' ),
    ISOM_BOX_TYPE_STTS = ISOM_4CC( 's', 't', 't', 's' ),
    ISOM_BOX_TYPE_STZ2 = ISOM_4CC( 's', 't', 'z', '2' ),
    ISOM_BOX_TYPE_SUBS = ISOM_4CC( 's', 'u', 'b', 's' ),
    ISOM_BOX_TYPE_SWTC = ISOM_4CC( 's', 'w', 't', 'c' ),
    ISOM_BOX_TYPE_TFHD = ISOM_4CC( 't', 'f', 'h', 'd' ),
    ISOM_BOX_TYPE_TFRA = ISOM_4CC( 't', 'f', 'r', 'a' ),
    ISOM_BOX_TYPE_TIBR = ISOM_4CC( 't', 'i', 'b', 'r' ),
    ISOM_BOX_TYPE_TIRI = ISOM_4CC( 't', 'i', 'r', 'i' ),
    ISOM_BOX_TYPE_TITL = ISOM_4CC( 't', 'i', 't', 'l' ),
    ISOM_BOX_TYPE_TKHD = ISOM_4CC( 't', 'k', 'h', 'd' ),
    ISOM_BOX_TYPE_TRAF = ISOM_4CC( 't', 'r', 'a', 'f' ),
    ISOM_BOX_TYPE_TRAK = ISOM_4CC( 't', 'r', 'a', 'k' ),
    ISOM_BOX_TYPE_TREF = ISOM_4CC( 't', 'r', 'e', 'f' ),
    ISOM_BOX_TYPE_TREX = ISOM_4CC( 't', 'r', 'e', 'x' ),
    ISOM_BOX_TYPE_TRGR = ISOM_4CC( 't', 'r', 'g', 'r' ),
    ISOM_BOX_TYPE_TRUN = ISOM_4CC( 't', 'r', 'u', 'n' ),
    ISOM_BOX_TYPE_TSEL = ISOM_4CC( 't', 's', 'e', 'l' ),
    ISOM_BOX_TYPE_UDTA = ISOM_4CC( 'u', 'd', 't', 'a' ),
    ISOM_BOX_TYPE_UINF = ISOM_4CC( 'u', 'i', 'n', 'f' ),
    ISOM_BOX_TYPE_ULST = ISOM_4CC( 'u', 'l', 's', 't' ),
    ISOM_BOX_TYPE_URL  = ISOM_4CC( 'u', 'r', 'l', ' ' ),
    ISOM_BOX_TYPE_URN  = ISOM_4CC( 'u', 'r', 'n', ' ' ),
    ISOM_BOX_TYPE_UUID = ISOM_4CC( 'u', 'u', 'i', 'd' ),
    ISOM_BOX_TYPE_VMHD = ISOM_4CC( 'v', 'm', 'h', 'd' ),
    ISOM_BOX_TYPE_VWDI = ISOM_4CC( 'v', 'w', 'd', 'i' ),
    ISOM_BOX_TYPE_XML  = ISOM_4CC( 'x', 'm', 'l', ' ' ),
    ISOM_BOX_TYPE_YRRC = ISOM_4CC( 'y', 'r', 'r', 'c' ),

    ISOM_BOX_TYPE_AVCC = ISOM_4CC( 'a', 'v', 'c', 'C' ),
    ISOM_BOX_TYPE_BTRT = ISOM_4CC( 'b', 't', 'r', 't' ),
    ISOM_BOX_TYPE_CLAP = ISOM_4CC( 'c', 'l', 'a', 'p' ),
    ISOM_BOX_TYPE_ESDS = ISOM_4CC( 'e', 's', 'd', 's' ),
    ISOM_BOX_TYPE_PASP = ISOM_4CC( 'p', 'a', 's', 'p' ),
    ISOM_BOX_TYPE_STSL = ISOM_4CC( 's', 't', 's', 'l' ),

    ISOM_BOX_TYPE_CHPL = ISOM_4CC( 'c', 'h', 'p', 'l' ),

    ISOM_BOX_TYPE_DAC3 = ISOM_4CC( 'd', 'a', 'c', '3' ),
    ISOM_BOX_TYPE_DAMR = ISOM_4CC( 'd', 'a', 'm', 'r' ),

    ISOM_BOX_TYPE_FTAB = ISOM_4CC( 'f', 't', 'a', 'b' ),
};

enum qt_box_code
{
    QT_BOX_TYPE_CLEF = ISOM_4CC( 'c', 'l', 'e', 'f' ),
    QT_BOX_TYPE_CLIP = ISOM_4CC( 'c', 'l', 'i', 'p' ),
    QT_BOX_TYPE_CRGN = ISOM_4CC( 'c', 'r', 'g', 'n' ),
    QT_BOX_TYPE_CTAB = ISOM_4CC( 'c', 't', 'a', 'b' ),
    QT_BOX_TYPE_ENOF = ISOM_4CC( 'e', 'n', 'o', 'f' ),
    QT_BOX_TYPE_GMHD = ISOM_4CC( 'g', 'm', 'h', 'd' ),
    QT_BOX_TYPE_GMIN = ISOM_4CC( 'g', 'm', 'i', 'n' ),
    QT_BOX_TYPE_IMAP = ISOM_4CC( 'i', 'm', 'a', 'p' ),
    QT_BOX_TYPE_KMAT = ISOM_4CC( 'k', 'm', 'a', 't' ),
    QT_BOX_TYPE_LOAD = ISOM_4CC( 'l', 'o', 'a', 'd' ),
    QT_BOX_TYPE_MATT = ISOM_4CC( 'm', 'a', 't', 't' ),
    QT_BOX_TYPE_PNOT = ISOM_4CC( 'p', 'n', 'o', 't' ),
    QT_BOX_TYPE_PROF = ISOM_4CC( 'p', 'r', 'o', 'f' ),
    QT_BOX_TYPE_STPS = ISOM_4CC( 's', 't', 'p', 's' ),
    QT_BOX_TYPE_TAPT = ISOM_4CC( 't', 'a', 'p', 't' ),
    QT_BOX_TYPE_TEXT = ISOM_4CC( 't', 'e', 'x', 't' ),
};

enum isom_handler_type_code
{
    ISOM_HANDLER_TYPE_DATA  = ISOM_4CC( 'd', 'h', 'l', 'r' ),
    ISOM_HANDLER_TYPE_MEDIA = ISOM_4CC( 'm', 'h', 'l', 'r' ),
};

enum isom_media_type_code
{
    ISOM_MEDIA_HANDLER_TYPE_AUDIO = ISOM_4CC( 's', 'o', 'u', 'n' ),
    ISOM_MEDIA_HANDLER_TYPE_VIDEO = ISOM_4CC( 'v', 'i', 'd', 'e' ),
    ISOM_MEDIA_HANDLER_TYPE_HINT  = ISOM_4CC( 'h', 'i', 'n', 't' ),
    ISOM_MEDIA_HANDLER_TYPE_META  = ISOM_4CC( 'm', 'e', 't', 'a' ),
    ISOM_MEDIA_HANDLER_TYPE_TEXT  = ISOM_4CC( 't', 'e', 'x', 't' ),
};

enum isom_data_reference_type_code
{
    ISOM_REFERENCE_HANDLER_TYPE_ALIAS    = ISOM_4CC( 'a', 'l', 'i', 's' ),
    ISOM_REFERENCE_HANDLER_TYPE_RESOURCE = ISOM_4CC( 'r', 's', 'r', 'c' ),
    ISOM_REFERENCE_HANDLER_TYPE_URL      = ISOM_4CC( 'u', 'r', 'l', ' ' ),
};

enum isom_brand_code
{
    ISOM_BRAND_TYPE_3G2A  = ISOM_4CC( '3', 'g', '2', 'a' ),
    ISOM_BRAND_TYPE_3GE6  = ISOM_4CC( '3', 'g', 'e', '6' ),
    ISOM_BRAND_TYPE_3GG6  = ISOM_4CC( '3', 'g', 'g', '6' ),
    ISOM_BRAND_TYPE_3GP4  = ISOM_4CC( '3', 'g', 'p', '4' ),
    ISOM_BRAND_TYPE_3GP5  = ISOM_4CC( '3', 'g', 'p', '5' ),
    ISOM_BRAND_TYPE_3GP6  = ISOM_4CC( '3', 'g', 'p', '6' ),
    ISOM_BRAND_TYPE_3GR6  = ISOM_4CC( '3', 'g', 'r', '6' ),
    ISOM_BRAND_TYPE_3GS6  = ISOM_4CC( '3', 'g', 's', '6' ),
    ISOM_BRAND_TYPE_CAEP  = ISOM_4CC( 'C', 'A', 'E', 'P' ),
    ISOM_BRAND_TYPE_CDES  = ISOM_4CC( 'C', 'D', 'e', 's' ),
    ISOM_BRAND_TYPE_M4A   = ISOM_4CC( 'M', '4', 'A', ' ' ),
    ISOM_BRAND_TYPE_M4B   = ISOM_4CC( 'M', '4', 'B', ' ' ),
    ISOM_BRAND_TYPE_M4P   = ISOM_4CC( 'M', '4', 'P', ' ' ),
    ISOM_BRAND_TYPE_M4V   = ISOM_4CC( 'M', '4', 'V', ' ' ),
    ISOM_BRAND_TYPE_MPPI  = ISOM_4CC( 'M', 'P', 'P', 'I' ),
    ISOM_BRAND_TYPE_ROSS  = ISOM_4CC( 'R', 'O', 'S', 'S' ),
    ISOM_BRAND_TYPE_AVC1  = ISOM_4CC( 'a', 'v', 'c', '1' ),
    ISOM_BRAND_TYPE_CAQV  = ISOM_4CC( 'c', 'a', 'q', 'v' ),
    ISOM_BRAND_TYPE_DA0A  = ISOM_4CC( 'd', 'a', '0', 'a' ),
    ISOM_BRAND_TYPE_DA0B  = ISOM_4CC( 'd', 'a', '0', 'b' ),
    ISOM_BRAND_TYPE_DA1A  = ISOM_4CC( 'd', 'a', '1', 'a' ),
    ISOM_BRAND_TYPE_DA1B  = ISOM_4CC( 'd', 'a', '1', 'b' ),
    ISOM_BRAND_TYPE_DA2A  = ISOM_4CC( 'd', 'a', '2', 'a' ),
    ISOM_BRAND_TYPE_DA2B  = ISOM_4CC( 'd', 'a', '2', 'b' ),
    ISOM_BRAND_TYPE_DA3A  = ISOM_4CC( 'd', 'a', '3', 'a' ),
    ISOM_BRAND_TYPE_DA3B  = ISOM_4CC( 'd', 'a', '3', 'b' ),
    ISOM_BRAND_TYPE_DMB1  = ISOM_4CC( 'd', 'm', 'b', '1' ),
    ISOM_BRAND_TYPE_DV1A  = ISOM_4CC( 'd', 'v', '1', 'a' ),
    ISOM_BRAND_TYPE_DV1B  = ISOM_4CC( 'd', 'v', '1', 'b' ),
    ISOM_BRAND_TYPE_DV2A  = ISOM_4CC( 'd', 'v', '2', 'a' ),
    ISOM_BRAND_TYPE_DV2B  = ISOM_4CC( 'd', 'v', '2', 'b' ),
    ISOM_BRAND_TYPE_DV3A  = ISOM_4CC( 'd', 'v', '3', 'a' ),
    ISOM_BRAND_TYPE_DV3B  = ISOM_4CC( 'd', 'v', '3', 'b' ),
    ISOM_BRAND_TYPE_DVR1  = ISOM_4CC( 'd', 'v', 'r', '1' ),
    ISOM_BRAND_TYPE_DVT1  = ISOM_4CC( 'd', 'v', 't', '1' ),
    ISOM_BRAND_TYPE_ISC2  = ISOM_4CC( 'i', 's', 'c', '2' ),
    ISOM_BRAND_TYPE_ISO2  = ISOM_4CC( 'i', 's', 'o', '2' ),
    ISOM_BRAND_TYPE_ISO3  = ISOM_4CC( 'i', 's', 'o', '3' ),
    ISOM_BRAND_TYPE_ISO4  = ISOM_4CC( 'i', 's', 'o', '4' ),
    ISOM_BRAND_TYPE_ISOM  = ISOM_4CC( 'i', 's', 'o', 'm' ),
    ISOM_BRAND_TYPE_JPSI  = ISOM_4CC( 'j', 'p', 's', 'i' ),
    ISOM_BRAND_TYPE_MJ2S  = ISOM_4CC( 'm', 'j', '2', 'j' ),
    ISOM_BRAND_TYPE_MJP2  = ISOM_4CC( 'm', 'j', 'p', '2' ),
    ISOM_BRAND_TYPE_MP21  = ISOM_4CC( 'm', 'p', '2', '1' ),
    ISOM_BRAND_TYPE_MP41  = ISOM_4CC( 'm', 'p', '4', '1' ),
    ISOM_BRAND_TYPE_MP42  = ISOM_4CC( 'm', 'p', '4', '2' ),
    ISOM_BRAND_TYPE_MP71  = ISOM_4CC( 'm', 'p', '7', '1' ),
    ISOM_BRAND_TYPE_NIKO  = ISOM_4CC( 'n', 'i', 'k', 'o' ),
    ISOM_BRAND_TYPE_ODCF  = ISOM_4CC( 'o', 'd', 'c', 'f' ),
    ISOM_BRAND_TYPE_OPF2  = ISOM_4CC( 'o', 'p', 'f', '2' ),
    ISOM_BRAND_TYPE_OPX2  = ISOM_4CC( 'o', 'p', 'x', '2' ),
    ISOM_BRAND_TYPE_PANA  = ISOM_4CC( 'p', 'a', 'n', 'a' ),
    ISOM_BRAND_TYPE_QT    = ISOM_4CC( 'q', 't', ' ', ' ' ),
    ISOM_BRAND_TYPE_SDV   = ISOM_4CC( 's', 'd', 'v', ' ' ),
};

enum isom_codec_code
{
    /* Audio Type */
    ISOM_CODEC_TYPE_AC_3_AUDIO = ISOM_4CC( 'a', 'c', '-', '3' ),    /* AC-3 audio */
    ISOM_CODEC_TYPE_ALAC_AUDIO = ISOM_4CC( 'a', 'l', 'a', 'c' ),    /* Apple lossless audio codec */
    ISOM_CODEC_TYPE_DRA1_AUDIO = ISOM_4CC( 'd', 'r', 'a', '1' ),    /* DRA Audio */
    ISOM_CODEC_TYPE_DTSC_AUDIO = ISOM_4CC( 'd', 't', 's', 'c' ),    /* DTS Coherent Acoustics audio */
    ISOM_CODEC_TYPE_DTSH_AUDIO = ISOM_4CC( 'd', 't', 's', 'h' ),    /* DTS-HD High Resolution Audio */
    ISOM_CODEC_TYPE_DTSL_AUDIO = ISOM_4CC( 'd', 't', 's', 'l' ),    /* DTS-HD Master Audio */
    ISOM_CODEC_TYPE_DTSE_AUDIO = ISOM_4CC( 'd', 't', 's', 'e' ),    /* DTS Express low bit rate audio, also known as DTS LBR */
    ISOM_CODEC_TYPE_EC_3_AUDIO = ISOM_4CC( 'e', 'c', '-', '3' ),    /* Enhanced AC-3 audio */
    ISOM_CODEC_TYPE_ENCA_AUDIO = ISOM_4CC( 'e', 'n', 'c', 'a' ),    /* Encrypted/Protected audio */
    ISOM_CODEC_TYPE_G719_AUDIO = ISOM_4CC( 'g', '7', '1', '9' ),    /* ITU-T Recommendation G.719 (2008) */
    ISOM_CODEC_TYPE_G726_AUDIO = ISOM_4CC( 'g', '7', '2', '6' ),    /* ITU-T Recommendation G.726 (1990) */
    ISOM_CODEC_TYPE_M4AE_AUDIO = ISOM_4CC( 'm', '4', 'a', 'e' ),    /* MPEG-4 Audio Enhancement */
    ISOM_CODEC_TYPE_MLPA_AUDIO = ISOM_4CC( 'm', 'l', 'p', 'a' ),    /* MLP Audio */
    ISOM_CODEC_TYPE_MP4A_AUDIO = ISOM_4CC( 'm', 'p', '4', 'a' ),    /* MPEG-4 Audio */
    ISOM_CODEC_TYPE_RAW_AUDIO  = ISOM_4CC( 'r', 'a', 'w', ' ' ),    /* Uncompressed audio */
    ISOM_CODEC_TYPE_SAMR_AUDIO = ISOM_4CC( 's', 'a', 'm', 'r' ),    /* Narrowband AMR voice */
    ISOM_CODEC_TYPE_SAWB_AUDIO = ISOM_4CC( 's', 'a', 'w', 'b' ),    /* Wideband AMR voice */
    ISOM_CODEC_TYPE_SAWP_AUDIO = ISOM_4CC( 's', 'a', 'w', 'p' ),    /* Extended AMR-WB (AMR-WB+) */
    ISOM_CODEC_TYPE_SEVC_AUDIO = ISOM_4CC( 's', 'e', 'v', 'c' ),    /* EVRC Voice */
    ISOM_CODEC_TYPE_SQCP_AUDIO = ISOM_4CC( 's', 'q', 'c', 'p' ),    /* 13K Voice */
    ISOM_CODEC_TYPE_SSMV_AUDIO = ISOM_4CC( 's', 's', 'm', 'v' ),    /* SMV Voice */
    ISOM_CODEC_TYPE_TWOS_AUDIO = ISOM_4CC( 't', 'w', 'o', 's' ),    /* Uncompressed 16-bit audio */

    /* Video Type */
    ISOM_CODEC_TYPE_AVC1_VIDEO = ISOM_4CC( 'a', 'v', 'c', '1' ),    /* Advanced Video Coding */
    ISOM_CODEC_TYPE_AVC2_VIDEO = ISOM_4CC( 'a', 'v', 'c', '2' ),    /* Advanced Video Coding */
    ISOM_CODEC_TYPE_AVCP_VIDEO = ISOM_4CC( 'a', 'v', 'c', 'p' ),    /* Advanced Video Coding Parameters */
    ISOM_CODEC_TYPE_DRAC_VIDEO = ISOM_4CC( 'd', 'r', 'a', 'c' ),    /* Dirac Video Coder */
    ISOM_CODEC_TYPE_ENCV_VIDEO = ISOM_4CC( 'e', 'n', 'c', 'v' ),    /* Encrypted/protected video */
    ISOM_CODEC_TYPE_MJP2_VIDEO = ISOM_4CC( 'm', 'j', 'p', '2' ),    /* Motion JPEG 2000 */
    ISOM_CODEC_TYPE_MP4V_VIDEO = ISOM_4CC( 'm', 'p', '4', 'v' ),    /* MPEG-4 Visual */
    ISOM_CODEC_TYPE_MVC1_VIDEO = ISOM_4CC( 'm', 'v', 'c', '1' ),    /* Multiview coding */
    ISOM_CODEC_TYPE_MVC2_VIDEO = ISOM_4CC( 'm', 'v', 'c', '2' ),    /* Multiview coding */
    ISOM_CODEC_TYPE_S263_VIDEO = ISOM_4CC( 's', '2', '6', '3' ),    /* ITU H.263 video (3GPP format) */
    ISOM_CODEC_TYPE_SVC1_VIDEO = ISOM_4CC( 's', 'v', 'c', '1' ),    /* Scalable Video Coding */
    ISOM_CODEC_TYPE_VC_1_VIDEO = ISOM_4CC( 'v', 'c', '-', '1' ),    /* SMPTE VC-1 */

    /* Hint Type */
    ISOM_CODEC_TYPE_FDP_HINT  = ISOM_4CC( 'f', 'd', 'p', ' ' ),     /* File delivery hints */
    ISOM_CODEC_TYPE_M2TS_HINT = ISOM_4CC( 'm', '2', 't', 's' ),     /* MPEG-2 transport stream for DMB */
    ISOM_CODEC_TYPE_PM2T_HINT = ISOM_4CC( 'p', 'm', '2', 't' ),     /* Protected MPEG-2 Transport */
    ISOM_CODEC_TYPE_PRTP_HINT = ISOM_4CC( 'p', 'r', 't', 'p' ),     /* Protected RTP Reception */
    ISOM_CODEC_TYPE_RM2T_HINT = ISOM_4CC( 'r', 'm', '2', 't' ),     /* MPEG-2 Transport Reception */
    ISOM_CODEC_TYPE_RRTP_HINT = ISOM_4CC( 'r', 'r', 't', 'p' ),     /* RTP reception */
    ISOM_CODEC_TYPE_RSRP_HINT = ISOM_4CC( 'r', 's', 'r', 'p' ),     /* SRTP Reception */
    ISOM_CODEC_TYPE_RTP_HINT  = ISOM_4CC( 'r', 't', 'p', ' ' ),     /* RTP Hints */
    ISOM_CODEC_TYPE_SM2T_HINT = ISOM_4CC( 's', 'm', '2', 't' ),     /* MPEG-2 Transport Server */
    ISOM_CODEC_TYPE_SRTP_HINT = ISOM_4CC( 's', 'r', 't', 'p' ),     /* SRTP Hints */

    /* Metadata Type */
    ISOM_CODEC_TYPE_IXSE_META = ISOM_4CC( 'i', 'x', 's', 'e' ),     /* DVB Track Level Index Track */
    ISOM_CODEC_TYPE_METT_META = ISOM_4CC( 'm', 'e', 't', 't' ),     /* Text timed metadata */
    ISOM_CODEC_TYPE_METX_META = ISOM_4CC( 'm', 'e', 't', 'x' ),     /* XML timed metadata */
    ISOM_CODEC_TYPE_MLIX_META = ISOM_4CC( 'm', 'l', 'i', 'x' ),     /* DVB Movie level index track */
    ISOM_CODEC_TYPE_OKSD_META = ISOM_4CC( 'o', 'k', 's', 'd' ),     /* OMA Keys */
    ISOM_CODEC_TYPE_SVCM_META = ISOM_4CC( 's', 'v', 'c', 'M' ),     /* SVC metadata */
    ISOM_CODEC_TYPE_TEXT_META = ISOM_4CC( 't', 'e', 'x', 't' ),     /* Textual meta-data with MIME type */
    ISOM_CODEC_TYPE_URIM_META = ISOM_4CC( 'u', 'r', 'i', 'm' ),     /* URI identified timed metadata */
    ISOM_CODEC_TYPE_XML_META  = ISOM_4CC( 'x', 'm', 'l', ' ' ),     /* XML-formatted meta-data */

    /* Other Type */
    ISOM_CODEC_TYPE_ENCS_SYSTEM = ISOM_4CC( 'e', 'n', 'c', 's' ),   /* Encrypted Systems stream */
    ISOM_CODEC_TYPE_MP4S_SYSTEM = ISOM_4CC( 'm', 'p', '4', 's' ),   /* MPEG-4 Systems */
    ISOM_CODEC_TYPE_ENCT_TEXT   = ISOM_4CC( 'e', 'n', 'c', 't' ),   /* Encrypted Text */
    ISOM_CODEC_TYPE_TX3G_TEXT   = ISOM_4CC( 't', 'x', '3', 'g' ),   /* Timed Text stream */
};

enum qt_codec_code
{
    /* Audio Type */
    QT_CODEC_TYPE_QDM2_AUDIO = ISOM_4CC( 'Q', 'D', 'M', '2' ),
    QT_CODEC_TYPE_QDMC_AUDIO = ISOM_4CC( 'Q', 'D', 'M', 'C' ),
    QT_CODEC_TYPE_QCLP_AUDIO = ISOM_4CC( 'Q', 'c', 'l', 'p' ),
    QT_CODEC_TYPE_AGSM_AUDIO = ISOM_4CC( 'a', 'g', 's', 'm' ),
    QT_CODEC_TYPE_ALAW_AUDIO = ISOM_4CC( 'a', 'l', 'a', 'w' ),
    QT_CODEC_TYPE_DVI_AUDIO  = ISOM_4CC( 'd', 'v', 'i', ' ' ),
    QT_CODEC_TYPE_FL32_AUDIO = ISOM_4CC( 'f', 'l', '3', '2' ),
    QT_CODEC_TYPE_FL64_AUDIO = ISOM_4CC( 'f', 'l', '6', '4' ),
    QT_CODEC_TYPE_IMA4_AUDIO = ISOM_4CC( 'i', 'm', 'a', '4' ),
    QT_CODEC_TYPE_IN24_AUDIO = ISOM_4CC( 'i', 'n', '2', '4' ),
    QT_CODEC_TYPE_IN32_AUDIO = ISOM_4CC( 'i', 'n', '3', '2' ),
    QT_CODEC_TYPE_LPCM_AUDIO = ISOM_4CC( 'l', 'p', 'c', 'm' ),
    QT_CODEC_TYPE_ULAW_AUDIO = ISOM_4CC( 'u', 'l', 'a', 'w' ),
    QT_CODEC_TYPE_VDVA_AUDIO = ISOM_4CC( 'v', 'd', 'v', 'a' ),

    /* Video Type */
    QT_CODEC_TYPE_CFHD_VIDEO = ISOM_4CC( 'C', 'F', 'H', 'D' ),
    QT_CODEC_TYPE_DV10_VIDEO = ISOM_4CC( 'D', 'V', '1', '0' ),
    QT_CODEC_TYPE_DVOO_VIDEO = ISOM_4CC( 'D', 'V', 'O', 'O' ),
    QT_CODEC_TYPE_DVOR_VIDEO = ISOM_4CC( 'D', 'V', 'O', 'R' ),
    QT_CODEC_TYPE_DVTV_VIDEO = ISOM_4CC( 'D', 'V', 'T', 'V' ),
    QT_CODEC_TYPE_DVVT_VIDEO = ISOM_4CC( 'D', 'V', 'V', 'T' ),
    QT_CODEC_TYPE_HD10_VIDEO = ISOM_4CC( 'H', 'D', '1', '0' ),
    QT_CODEC_TYPE_M105_VIDEO = ISOM_4CC( 'M', '1', '0', '5' ),
    QT_CODEC_TYPE_PNTG_VIDEO = ISOM_4CC( 'P', 'N', 'T', 'G' ),
    QT_CODEC_TYPE_SVQ1_VIDEO = ISOM_4CC( 'S', 'V', 'Q', '1' ),
    QT_CODEC_TYPE_SVQ3_VIDEO = ISOM_4CC( 'S', 'V', 'Q', '3' ),
    QT_CODEC_TYPE_SHR0_VIDEO = ISOM_4CC( 'S', 'h', 'r', '0' ),
    QT_CODEC_TYPE_SHR1_VIDEO = ISOM_4CC( 'S', 'h', 'r', '1' ),
    QT_CODEC_TYPE_SHR2_VIDEO = ISOM_4CC( 'S', 'h', 'r', '2' ),
    QT_CODEC_TYPE_SHR3_VIDEO = ISOM_4CC( 'S', 'h', 'r', '3' ),
    QT_CODEC_TYPE_SHR4_VIDEO = ISOM_4CC( 'S', 'h', 'r', '4' ),
    QT_CODEC_TYPE_WRLE_VIDEO = ISOM_4CC( 'W', 'R', 'L', 'E' ),
    QT_CODEC_TYPE_CIVD_VIDEO = ISOM_4CC( 'c', 'i', 'v', 'd' ),
    QT_CODEC_TYPE_DRAC_VIDEO = ISOM_4CC( 'd', 'r', 'a', 'c' ),
    QT_CODEC_TYPE_DVH5_VIDEO = ISOM_4CC( 'd', 'v', 'h', '5' ),
    QT_CODEC_TYPE_DVH6_VIDEO = ISOM_4CC( 'd', 'v', 'h', '6' ),
    QT_CODEC_TYPE_DVHP_VIDEO = ISOM_4CC( 'd', 'v', 'h', 'p' ),
    QT_CODEC_TYPE_FLIC_VIDEO = ISOM_4CC( 'f', 'l', 'i', 'c' ),
    QT_CODEC_TYPE_GIF_VIDEO  = ISOM_4CC( 'g', 'i', 'f', ' ' ),
    QT_CODEC_TYPE_H261_VIDEO = ISOM_4CC( 'h', '2', '6', '1' ),
    QT_CODEC_TYPE_H263_VIDEO = ISOM_4CC( 'h', '2', '6', '3' ),
    QT_CODEC_TYPE_JPEG_VIDEO = ISOM_4CC( 'j', 'p', 'e', 'g' ),
    QT_CODEC_TYPE_MJPA_VIDEO = ISOM_4CC( 'm', 'j', 'p', 'a' ),
    QT_CODEC_TYPE_MJPB_VIDEO = ISOM_4CC( 'm', 'j', 'p', 'b' ),
    QT_CODEC_TYPE_PNG_VIDEO  = ISOM_4CC( 'p', 'n', 'g', ' ' ),
    QT_CODEC_TYPE_RLE_VIDEO  = ISOM_4CC( 'r', 'l', 'e', ' ' ),
    QT_CODEC_TYPE_RPZA_VIDEO = ISOM_4CC( 'r', 'p', 'z', 'a' ),
    QT_CODEC_TYPE_TGA_VIDEO  = ISOM_4CC( 't', 'g', 'a', ' ' ),
    QT_CODEC_TYPE_TIFF_VIDEO = ISOM_4CC( 't', 'i', 'f', 'f' ),
    /* Text Type */
    QT_CODEC_TYPE_TEXT_TEXT = ISOM_4CC( 't', 'e', 'x', 't' ),
};

enum isom_track_reference_code
{
    ISOM_TREF_TYPE_AVCP = ISOM_4CC( 'a', 'v', 'c', 'p' ),
    ISOM_TREF_TYPE_CDSC = ISOM_4CC( 'c', 'd', 's', 'c' ),
    ISOM_TREF_TYPE_DPND = ISOM_4CC( 'd', 'p', 'n', 'd' ),
    ISOM_TREF_TYPE_HIND = ISOM_4CC( 'h', 'i', 'n', 'd' ),
    ISOM_TREF_TYPE_HINT = ISOM_4CC( 'h', 'i', 'n', 't' ),
    ISOM_TREF_TYPE_IPIR = ISOM_4CC( 'i', 'p', 'i', 'r' ),
    ISOM_TREF_TYPE_MPOD = ISOM_4CC( 'm', 'p', 'o', 'd' ),
    ISOM_TREF_TYPE_SBAS = ISOM_4CC( 's', 'b', 'a', 's' ),
    ISOM_TREF_TYPE_SCAL = ISOM_4CC( 's', 'c', 'a', 'l' ),
    ISOM_TREF_TYPE_SWFR = ISOM_4CC( 's', 'w', 'f', 'r' ),
    ISOM_TREF_TYPE_SYNC = ISOM_4CC( 's', 'y', 'n', 'c' ),
    ISOM_TREF_TYPE_VDEP = ISOM_4CC( 'v', 'd', 'e', 'p' ),
    ISOM_TREF_TYPE_VPLX = ISOM_4CC( 'v', 'p', 'l', 'x' ),
};

enum qt_track_reference_code
{
    QT_TREF_TYPE_CHAP = ISOM_4CC( 'c', 'h', 'a', 'p' ),
    QT_TREF_TYPE_SCPT = ISOM_4CC( 's', 'c', 'p', 't' ),
    QT_TREF_TYPE_SSRC = ISOM_4CC( 's', 's', 'r', 'c' ),
    QT_TREF_TYPE_TMCD = ISOM_4CC( 't', 'm', 'c', 'd' ),
};

enum isom_grouping_code
{
    ISOM_GROUP_TYPE_AVLL = ISOM_4CC( 'a', 'v', 'l', 'l' ),      /* AVC Layer */
    ISOM_GROUP_TYPE_AVSS = ISOM_4CC( 'a', 'v', 's', 's' ),      /* AVC Sub Sequence */
    ISOM_GROUP_TYPE_RASH = ISOM_4CC( 'r', 'a', 's', 'h' ),      /* Rate Share */
    ISOM_GROUP_TYPE_ROLL = ISOM_4CC( 'r', 'o', 'l', 'l' ),      /* Roll Recovery */
};


typedef struct
{
    uint32_t complete;      /* recovery point: the identifier necessary for the recovery from its starting point to be completed */
    uint32_t identifier;    /* the identifier for samples
                             * If this identifier equals a certain recovery_point, then this sample is the recovery point. */
    uint8_t start_point;
} isom_recovery_t;

typedef struct
{
    uint8_t sync_point;
    uint8_t partial_sync;
    uint8_t leading;
    uint8_t independent;
    uint8_t disposable;
    uint8_t redundant;
    isom_recovery_t recovery;
} isom_sample_property_t;

typedef struct
{
    uint32_t length;
    uint8_t *data;
    uint64_t dts;
    uint64_t cts;
    uint32_t index;
    isom_sample_property_t prop;
} isom_sample_t;

typedef int (*isom_adhoc_remux_callback)( void* param, uint64_t done, uint64_t total );
typedef struct {
    uint64_t buffer_size;
    isom_adhoc_remux_callback func;
    void* param;
} isom_adhoc_remux_t;

int isom_add_sps_entry( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint8_t *sps, uint32_t sps_size );
int isom_add_pps_entry( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint8_t *pps, uint32_t pps_size );
int isom_add_sample_entry( isom_root_t *root, uint32_t track_ID, uint32_t sample_type, void* summary );

int isom_add_btrt( isom_root_t *root, uint32_t track_ID, uint32_t entry_number );
int isom_add_mdat( isom_root_t *root );
int isom_add_free( isom_root_t *root, uint8_t *data, uint64_t data_length );

int isom_write_ftyp( isom_root_t *root );
int isom_write_moov( isom_root_t *root );
int isom_write_free( isom_root_t *root );

uint32_t isom_get_media_timescale( isom_root_t *root, uint32_t track_ID );
uint64_t isom_get_media_duration( isom_root_t *root, uint32_t track_ID );
uint64_t isom_get_track_duration( isom_root_t *root, uint32_t track_ID );
uint32_t isom_get_last_sample_delta( isom_root_t *root, uint32_t track_ID );
uint32_t isom_get_start_time_offset( isom_root_t *root, uint32_t track_ID );
uint32_t isom_get_movie_timescale( isom_root_t *root );

int isom_set_brands( isom_root_t *root, uint32_t major_brand, uint32_t minor_version, uint32_t *brands, uint32_t brand_count );
int isom_set_max_chunk_duration( isom_root_t *root, double max_chunk_duration );
int isom_set_media_handler( isom_root_t *root, uint32_t track_ID, uint32_t media_type, char *name );
int isom_set_media_handler_name( isom_root_t *root, uint32_t track_ID, char *handler_name );
int isom_set_data_handler( isom_root_t *root, uint32_t track_ID, uint32_t reference_type, char *name );
int isom_set_data_handler_name( isom_root_t *root, uint32_t track_ID, char *handler_name );
int isom_set_movie_timescale( isom_root_t *root, uint32_t timescale );
int isom_set_media_timescale( isom_root_t *root, uint32_t track_ID, uint32_t timescale );
int isom_set_track_mode( isom_root_t *root, uint32_t track_ID, uint32_t mode );
int isom_set_track_presentation_size( isom_root_t *root, uint32_t track_ID, uint32_t width, uint32_t height );
int isom_set_track_volume( isom_root_t *root, uint32_t track_ID, int16_t volume );
int isom_set_track_aperture_modes( isom_root_t *root, uint32_t track_ID, uint32_t entry_number );
int isom_set_sample_resolution( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint16_t width, uint16_t height );
int isom_set_sample_type( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint32_t sample_type );
int isom_set_sample_aspect_ratio( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint32_t hSpacing, uint32_t vSpacing );
int isom_set_scaling_method( isom_root_t *root, uint32_t track_ID, uint32_t entry_number,
                             uint8_t scale_method, int16_t display_center_x, int16_t display_center_y );
int isom_set_avc_config( isom_root_t *root, uint32_t track_ID, uint32_t entry_number,
                         uint8_t configurationVersion, uint8_t AVCProfileIndication, uint8_t profile_compatibility,
                         uint8_t AVCLevelIndication, uint8_t lengthSizeMinusOne,
                         uint8_t chroma_format, uint8_t bit_depth_luma_minus8, uint8_t bit_depth_chroma_minus8 );
int isom_set_handler_name( isom_root_t *root, uint32_t track_ID, char *handler_name );
int isom_set_last_sample_delta( isom_root_t *root, uint32_t track_ID, uint32_t sample_delta );
int isom_set_media_language( isom_root_t *root, uint32_t track_ID, char *ISO_language, uint16_t Mac_language );
int isom_set_track_ID( isom_root_t *root, uint32_t track_ID, uint32_t new_track_ID );
int isom_set_free( isom_root_t *root, uint8_t *data, uint64_t data_length );
int isom_set_tyrant_chapter( isom_root_t *root, char *file_name );

int isom_create_explicit_timeline_map( isom_root_t *root, uint32_t track_ID, uint64_t segment_duration, int64_t media_time, int32_t media_rate );
int isom_create_reference_chapter_track( isom_root_t *root, uint32_t track_ID, char *file_name );
int isom_create_grouping( isom_root_t *root, uint32_t track_ID, uint32_t grouping_type );
int isom_create_object_descriptor( isom_root_t *root );

int isom_modify_timeline_map( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint64_t segment_duration, int64_t media_time, int32_t media_rate );

int isom_update_media_modification_time( isom_root_t *root, uint32_t track_ID );
int isom_update_track_modification_time( isom_root_t *root, uint32_t track_ID );
int isom_update_movie_modification_time( isom_root_t *root );
int isom_update_track_duration( isom_root_t *root, uint32_t track_ID, uint32_t last_sample_delta );
int isom_update_bitrate_info( isom_root_t *root, uint32_t track_ID, uint32_t entry_number );


isom_root_t *isom_create_movie( char *filename );
uint32_t isom_create_track( isom_root_t *root, uint32_t handler_type );
isom_sample_t *isom_create_sample( uint32_t size );
void isom_delete_sample( isom_sample_t *sample );
int isom_write_sample( isom_root_t *root, uint32_t track_ID, isom_sample_t *sample );
int isom_write_mdat_size( isom_root_t *root );
int isom_flush_pooled_samples( isom_root_t *root, uint32_t track_ID, uint32_t last_sample_delta );
int isom_finish_movie( isom_root_t *root, isom_adhoc_remux_t* remux );
void isom_destroy_root( isom_root_t *root );

void isom_delete_track( isom_root_t *root, uint32_t track_ID );
void isom_delete_explicit_timeline_map( isom_root_t *root, uint32_t track_ID );
void isom_delete_tyrant_chapter( isom_root_t *root );

#endif
