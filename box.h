/*****************************************************************************
 * box.h:
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

#ifndef LSMASH_BOX_H
#define LSMASH_BOX_H

/* For generating creation_time and modification_time.
 * According to ISO/IEC-14496-5-2001, the difference between Unix time and Mac OS time is 2082758400.
 * However this is wrong and 2082844800 is correct. */
#include <time.h>
#define ISOM_MAC_EPOCH_OFFSET 2082844800

#include "utils.h"

typedef struct isom_box_tag isom_box_t;

/* If size is 1, then largesize is actual size.
 * If size is 0, then this box is the last one in the file.
 * usertype is for uuid. */
#define ISOM_BASEBOX_COMMON \
        lsmash_root_t *root;    /* pointer of root */ \
        isom_box_t *parent;     /* pointer of the parent box of this box */ \
        uint8_t  manager;       /* flags for L-SMASH */ \
        uint64_t pos;           /* starting position of this box in the file */ \
    uint64_t size;              /* the number of bytes in this box */ \
    uint32_t type;              /* four characters codes that identify box type */ \
    uint8_t  *usertype

#define ISOM_FULLBOX_COMMON \
    ISOM_BASEBOX_COMMON; \
    uint8_t  version;   /* Basically, version is either 0 or 1 */ \
    uint32_t flags      /* In the actual structure of box, flags is 24 bits. */

#define ISOM_DEFAULT_BOX_HEADER_SIZE          8
#define ISOM_DEFAULT_FULLBOX_HEADER_SIZE      12
#define ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE 16

struct isom_box_tag
{
    ISOM_FULLBOX_COMMON;
};

/* File Type Box
 * This box identifies the specifications to which this file complies.
 * This box shall occur before any variable-length box.
 * In the absence of this box, the file is QuickTime file format or MP4 version 1 file format.
 * In MP4 version 1 file format, Object Descriptor Box is mandatory.
 * In QuickTime file format, Object Descriptor Box isn't defined.
 * Therefore, if this box and an Object Descriptor Box are absent in the file, the file shall be QuikcTime file format. */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    uint32_t major_brand;           /* brand identifier */
    uint32_t minor_version;         /* the minor version of the major brand */
    uint32_t *compatible_brands;    /* a list, to the end of the box, of brands */

        uint32_t brand_count;       /* the number of factors in compatible_brands array */
} isom_ftyp_t;

/* Track Header Box
 * This box specifies the characteristics of a single track. */
typedef struct
{
    /* version is either 0 or 1
     * flags
     *      0x000001: Indicates that the track is enabled.
     *                A disabled track is treated as if it were not present.
     *      0x000002: Indicates that the track is used in the presentation.
     *      0x000004: Indicates that the track is used when previewing the presentation.
     *      0x000008: Indicates that the track is used in the movie's poster. (only defined in QuickTime file format)
     * ISOM: If in a presentation all tracks have neither track_in_movie nor track_in_preview set,
     *       then all tracks shall be treated as if both flags were set on all tracks. */
    ISOM_FULLBOX_COMMON;
    /* version == 0: uint64_t -> uint32_t */
    uint64_t creation_time;         /* the creation time of this track (in seconds since midnight, Jan. 1, 1904, in UTC time) */
    uint64_t modification_time;     /* the most recent time the track was modified (in seconds since midnight, Jan. 1, 1904, in UTC time) */
    uint32_t track_ID;              /* an integer that uniquely identifies the track
                                     * Track IDs are never re-used and cannot be zero. */
    uint32_t reserved1;
    uint64_t duration;              /* the duration of this track expressed in the movie timescale units */
    /* The following fields are treated as reserved in MP4 version 1. */
    uint32_t reserved2[2];
    int16_t  layer;                 /* the front-to-back ordering of video tracks; tracks with lower numbers are closer to the viewer. */
    int16_t  alternate_group;       /* an integer that specifies a group or collection of tracks
                                     * If this field is not 0, it should be the same for tracks that contain alternate data for one another
                                     * and different for tracks belonging to different such groups.
                                     * Only one track within an alternate group should be played or streamed at any one time. */
    int16_t  volume;                /* fixed point 8.8 number. 0x0100 is full volume. */
    uint16_t reserved3;
    int32_t  matrix[9];             /* transformation matrix for the video */
    /* track's visual presentation size
     * All images in the sequence are scaled to this size, before any overall transformation of the track represented by the matrix.
     * Note: these fields are treated as reserved in MP4 version 1. */
    uint32_t width;                 /* fixed point 16.16 number */
    uint32_t height;                /* fixed point 16.16 number */
    /* */
} isom_tkhd_t;

/* Track Clean Aperture Dimensions Box
 * A presentation mode where clap and pasp are reflected. */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint32_t width;     /* fixed point 16.16 number */
    uint32_t height;    /* fixed point 16.16 number */
} isom_clef_t;

/* Track Production Aperture Dimensions Box
 * A presentation mode where pasp is reflected. */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint32_t width;     /* fixed point 16.16 number */
    uint32_t height;    /* fixed point 16.16 number */
} isom_prof_t;

/* Track Encoded Pixels Dimensions Box
 * A presentation mode where clap and pasp are not reflected. */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint32_t width;     /* fixed point 16.16 number */
    uint32_t height;    /* fixed point 16.16 number */
} isom_enof_t;

/* Track Aperture Mode Dimensions Box */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    isom_clef_t *clef;      /* Track Clean Aperture Dimensions Box */
    isom_prof_t *prof;      /* Track Production Aperture Dimensions Box */
    isom_enof_t *enof;      /* Track Encoded Pixels Dimensions Box */
} isom_tapt_t;

/* Edit List Box
 * This box contains an explicit timeline map.
 * Each entry defines part of the track timeline: by mapping part of the media timeline, or by indicating 'empty' time, 
 * or by defining a 'dwell', where a single time-point in the media is held for a period.
 * The last edit in a track shall never be an empty edit.
 * Any difference between the duration in the Movie Header Box, and the track's duration is expressed as an implicit empty edit at the end.
 * It is recommended that any edits, explicit or implied, not select any portion of the composition timeline that doesn't map to a sample.
 * Therefore, if the first sample in the track has non-zero CTS, then this track should have at least one edit and the start time in it should
 * correspond to the value of the CTS the first sample has or more not to exceed the largest CTS in this track. */
typedef struct
{
    /* version == 0: 64bits -> 32bits */
    uint64_t segment_duration;  /* the duration of this edit expressed in the movie timescale units */
    int64_t  media_time;        /* the starting composition time within the media of this edit segment
                                 * If this field is set to -1, it is an empty edit. */
    int32_t  media_rate;        /* the relative rate at which to play the media corresponding to this edit segment
                                 * If this value is 0, then the edit is specifying a 'dwell':
                                 * the media at media_time is presented for the segment_duration.
                                 * This field is expressed as 16.16 fixed-point number. */
} isom_elst_entry_t;

typedef struct
{
    ISOM_FULLBOX_COMMON;        /* version is either 0 or 1 */
    lsmash_entry_list_t *list;
} isom_elst_t;

/* Edit Box
 * This optional box maps the presentation time-line to the media time-line as it is stored in the file.
 * In the absence of this box, there is an implicit one-to-one mapping of these time-lines,
 * and the presentation of a track starts at the beginning of the presentation. */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    isom_elst_t *elst;     /* Edit List Box */
} isom_edts_t;

/* Track Reference Box
 * The Track Reference Box contains Track Reference Type Boxes.
 * Track Reference Type Boxes define relationships between tracks.
 * They allow one track to specify how it is related to other tracks. */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    uint32_t *track_ID;         /* track_IDs of reference tracks / Zero value must not be used */

        uint32_t ref_count;     /* number of reference tracks */
} isom_tref_type_t;

typedef struct
{
    ISOM_BASEBOX_COMMON;
    lsmash_entry_list_t *ref_list;      /* Track Reference Type Boxes */
} isom_tref_t;

/* Media Header Box
 * This box declares overall information that is media-independent, and relevant to characteristics of the media in a track.*/
typedef struct
{
    ISOM_FULLBOX_COMMON;            /* version is either 0 or 1 */
    /* version == 0: uint64_t -> uint32_t */
    uint64_t creation_time;         /* the creation time of the media in this track (in seconds since midnight, Jan. 1, 1904, in UTC time) */
    uint64_t modification_time;     /* the most recent time the media in this track was modified (in seconds since midnight, Jan. 1, 1904, in UTC time) */
    uint32_t timescale;             /* media timescale: timescale for this media */
    uint64_t duration;              /* the duration of this media expressed in the timescale indicated in this box */
    /* */
#define ISOM_LANG( lang ) ((((lang[0]-0x60)&0x1f)<<10) | (((lang[1]-0x60)&0x1f)<<5) | ((lang[2]-0x60)&0x1f))
    uint16_t language;              /* ISOM: ISO-639-2/T language codes. The first bit is 0.
                                     *       Each character is packed as the difference between its ASCII value and 0x60.
                                     * QTFF: Macintosh language codes is usually used.
                                     *       Mac's value is less than 0x800 while ISO's value is 0x800 or greater. */
    int16_t quality;                /* ISOM: pre_defined / QTFF: the media's playback quality */
} isom_mdhd_t;

/* Handler Reference Box
 * In Media Box, this box is mandatory and (ISOM: should/QTFF: must) come before Media Information Box.
 * ISOM: this box might be also in Meta Box.
 * QTFF: this box might be also in Media Information Box. If this box is present there, it must come before Data Information Box. */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint32_t componentType;             /* ISOM: pre_difined = 0
                                         * QTFF: 'mhlr' for Media Handler Reference Box and 'dhlr' for Data Handler Reference Box  */
    uint32_t componentSubtype;          /* ISOM and QT: when present in Media Handler Reference Box, this field defines the type of media data
                                         * QTFF: when present in Data Handler Reference Box, this field defines the data reference type */
    /* The following fields are defined in QTFF however these fields aren't mentioned in QuickTime SDK and are reserved in the specification.
     * In ISOM, these fields are still defined as reserved. */
    uint32_t componentManufacturer;     /* vendor indentification / A value of 0 matches any manufacturer. */
    uint32_t componentFlags;            /* flags describing required component capabilities
                                         * The high-order 8 bits should be set to 0.
                                         * The low-order 24 bits are specific to each component type. */
    uint32_t componentFlagsMask;        /* This field indicates which flags in the componentFlags field are relevant to this operation. */
    /* */
    uint8_t *componentName;             /* ISOM: a null-terminated string in UTF-8 characters
                                         * QTFF: Pascal string */

        uint32_t componentName_length;
} isom_hdlr_t;


/** Media Information Header Boxes
 ** There is a different media information header for each track type
 ** (corresponding to the media handler-type); the matching header shall be present. **/
/* Video Media Header Box
 * This box contains general presentation information, independent of the coding, for video media. */
typedef struct
{
    ISOM_FULLBOX_COMMON;        /* flags is 1 */
    uint16_t graphicsmode;      /* template: graphicsmode = 0 */
    uint16_t opcolor[3];        /* template: opcolor = { 0, 0, 0 } */
} isom_vmhd_t;

/* Sound Media Header Box
 * This box contains general presentation information, independent of the coding, for audio media. */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    int16_t balance;        /* a fixed-point 8.8 number that places mono audio tracks in a stereo space. template: balance = 0 */
    uint16_t reserved;
} isom_smhd_t;

/* Hint Media Header Box
 * This box contains general information, independent of the protocol, for hint tracks. (A PDU is a Protocol Data Unit.) */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint16_t maxPDUsize;        /* the size in bytes of the largest PDU in this (hint) stream */
    uint16_t avgPDUsize;        /* the average size of a PDU over the entire presentation */
    uint32_t maxbitrate;        /* the maximum rate in bits/second over any window of one second */
    uint32_t avgbitrate;        /* the average rate in bits/second over the entire presentation */
    uint32_t reserved;
} isom_hmhd_t;

/* Null Media Header Box
 * This box may be used for streams other than visual and audio (e.g., timed metadata streams). */
typedef struct
{
    /* Streams other than visual and audio may use a Null Media Header Box */
    ISOM_FULLBOX_COMMON;    /* flags is currently all zero */
} isom_nmhd_t;

/* Generic Media Information Box */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint16_t graphicsmode;
    uint16_t opcolor[3];
    int16_t balance;        /* This field is nomally set to 0. */
    uint16_t reserved;      /* Reserved for use by Apple. Set this field to 0. */
} isom_gmin_t;

/* Text Media Information Box */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    int32_t matrix[9];      /* Unkown fields. Default values are probably:
                             * { 0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000 } */
} isom_text_t;

/* Generic Media Information Header Box */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    isom_gmin_t *gmin;      /* Generic Media Information Box */
    isom_text_t *text;      /* Text Media Information Box */
} isom_gmhd_t;
/** **/

/* Data Reference Box
 * name and location fields are expressed in null-terminated string using UTF-8 characters. */
typedef struct
{
    /* This box is DataEntryUrlBox or DataEntryUrnBox */
    ISOM_FULLBOX_COMMON;    /* flags == 0x000001 means that the media data is in the same file
                             * as the Movie Box containing this data reference. */
    char *name;             /* only for DataEntryUrnBox */
    char *location;         /* a location to find the resource with the given name */

    uint32_t name_length;
    uint32_t location_length;
} isom_dref_entry_t;

typedef struct
{
    ISOM_FULLBOX_COMMON;
    lsmash_entry_list_t *list;
} isom_dref_t;

/* Data Information Box */
typedef struct
{
    /* This box is in Media Information Box or Meta Box */
    ISOM_BASEBOX_COMMON;
    isom_dref_t *dref;      /* Data Reference Box */
} isom_dinf_t;

/** Sample Description **/
/* ES Descriptor Box */
struct mp4sys_ES_Descriptor_t; /* FIXME: I think these structs using mp4sys should be placed in isom.c */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    struct mp4sys_ES_Descriptor_t *ES;
} isom_esds_t;

/* AVCDecoderConfigurationRecord */
typedef struct
{
#define ISOM_REQUIRES_AVCC_EXTENSION( x ) ((x) == 100 || (x) == 110 || (x) == 122 || (x) == 144)
    ISOM_BASEBOX_COMMON;
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

/* Parameter Set Entry */
typedef struct
{
    uint16_t parameterSetLength;
    uint8_t *parameterSetNALUnit;
} isom_avcC_ps_entry_t;

/* MPEG-4 Bit Rate Box
 * This box signals the bit rate information of the AVC video stream. */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    uint32_t bufferSizeDB;  /* the size of the decoding buffer for the elementary stream in bytes */
    uint32_t maxBitrate;    /* the maximum rate in bits/second over any window of one second */
    uint32_t avgBitrate;    /* the average rate in bits/second over the entire presentation */
} isom_btrt_t;

/* Clean Aperture Box
 * There are notionally four values in this box and these parameters are represented as a fraction N/D.
 * Here, we refer to the pair of parameters fooN and fooD as foo.
 * Considering the pixel dimensions as defined by the VisualSampleEntry width and height.
 * If picture centre of the image is at pcX and pcY, then horizOff and vertOff are defined as follows:
 *  pcX = horizOff + (width - 1)/2;
 *  pcY = vertOff + (height - 1)/2;
 * The leftmost/rightmost pixel and the topmost/bottommost line of the clean aperture fall at:
 *  pcX +/- (cleanApertureWidth - 1)/2;
 *  pcY +/- (cleanApertureHeight - 1)/2; */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    uint32_t cleanApertureWidthN;
    uint32_t cleanApertureWidthD;
    uint32_t cleanApertureHeightN;
    uint32_t cleanApertureHeightD;
    int32_t  horizOffN;
    uint32_t horizOffD;
    int32_t  vertOffN;
    uint32_t vertOffD;
} isom_clap_t;

/* Pixel Aspect Ratio Box
 * This box specifies the aspect ratio of a pixel, in arbitrary units.
 * If a pixel appears H wide and V tall, then hSpacing/vSpacing is equal to H/V.
 * When adjusting pixel aspect ratio, normally, the horizontal dimension of the video is scaled, if needed. */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    uint32_t hSpacing;      /* horizontal spacing */
    uint32_t vSpacing;      /* vertical spacing */
} isom_pasp_t;

/* Color Parameter Box
 * This box is used to map the numerical values of pixels in the file to a common representation of color
 * in which images can be correctly compared, combined, and displayed.
 * This box is defined in QuickTime file format. */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    uint32_t color_parameter_type;          /* 'nclc' or 'prof' */
    /* for 'nclc' */
    uint16_t primaries_index;               /* CIE 1931 xy chromaticity coordinates */
    uint16_t transfer_function_index;       /* nonlinear transfer function from RGB to ErEgEb */
    uint16_t matrix_index;                  /* matrix from ErEgEb to EyEcbEcr */
} isom_colr_t;

/* Sample Scale Box
 * If this box is present and can be interpreted by the decoder,
 * all samples shall be displayed according to the scaling behaviour that is specified in this box.
 * Otherwise, all samples are scaled to the size that is indicated by the width and height field in the Track Header Box. */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint8_t constraint_flag;    /* Upper 7-bits are reserved.
                                 * If this flag is set, all samples described by this sample entry shall be scaled
                                 * according to the method specified by the field 'scale_method'. */
    uint8_t scale_method;       /* The semantics of the values for scale_method are as specified for the 'fit' attribute of regions in SMIL 1.0. */
    int16_t display_center_x;
    int16_t display_center_y;
} isom_stsl_t;

/* Sample Entry */
#define ISOM_SAMPLE_ENTRY \
    ISOM_BASEBOX_COMMON; \
    uint8_t reserved[6]; \
    uint16_t data_reference_index;

typedef struct
{
    ISOM_SAMPLE_ENTRY;
} isom_sample_entry_t;

/* Mpeg Sample Entry */
typedef struct
{
    ISOM_SAMPLE_ENTRY;
    isom_esds_t *esds;      /* ES Descriptor Box */
} isom_mp4s_entry_t;

/* ISOM: Visual Sample Entry / QTFF: Image Description */
typedef struct
{
    ISOM_SAMPLE_ENTRY;
    int16_t  version;           /* ISOM: pre_defined / QTFF: sample description version */
    int16_t  revision_level;    /* ISOM: reserved / QTFF: version of the CODEC */
    int32_t  vendor;            /* ISOM: pre_defined / QTFF: whose CODEC */
    uint32_t temporalQuality;   /* ISOM: pre_defined / QTFF: the temporal quality factor */
    uint32_t spatialQuality;    /* ISOM: pre_defined / QTFF: the spatial quality factor */
    /* The width and height are the maximum pixel counts that the codec will deliver.
     * Since these are counts they do not take into account pixel aspect ratio. */
    uint16_t width;
    uint16_t height;
    /* */
    uint32_t horizresolution;   /* 16.16 fixed-point / template: horizresolution = 0x00480000 / 72 dpi */
    uint32_t vertresolution;    /* 16.16 fixed-point / template: vertresolution = 0x00480000 / 72 dpi */
    uint32_t dataSize;          /* ISOM: reserved / QTFF: if known, the size of data for this descriptor */
    uint16_t frame_count;       /* frame per sample / template: frame_count = 1 */
    char compressorname[33];    /* a fixed 32-byte field, with the first byte set to the number of bytes to be displayed */
    uint16_t depth;             /* ISOM: template: depth = 0x0018
                                 * AVC : 0x0018: colour with no alpha
                                 *       0x0028: grayscale with no alpha
                                 *       0x0020: gray or colour with alpha
                                 * QTFF: depth of this data (1-32) or (33-40 grayscale) */
    int16_t color_table_ID;     /* ISOM: template: pre_defined = -1
                                 * QTFF: color table ID
                                 *       If this field is set to 0, the default color table should be used for the specified depth
                                 *       If the color table ID is set to 0, a color table is contained within the sample description itself.
                                 *       The color table immediately follows the color table ID field. */
    /* common extensions */
    isom_clap_t *clap;          /* Clean Aperture Box @ optional */
    isom_pasp_t *pasp;          /* Pixel Aspect Ratio Box @ optional */
    isom_colr_t *colr;          /* ISOM: null / QTFF: Color Parameter Box @ optional */
    isom_stsl_t *stsl;          /* ISOM: Sample Scale Box @ optional / QTFF: null */
    /* MP4 specific extension */
    isom_esds_t *esds;          /* ES Descriptor Box */
    /* AVC specific extensions */
    isom_avcC_t *avcC;          /* AVCDecoderConfigurationRecord */
    isom_btrt_t *btrt;          /* MPEG-4 Bit Rate Box @ optional */
    // isom_m4ds_t *m4ds;       /* MPEG4ExtensionDescriptorsBox @ optional */
} isom_visual_entry_t;

/* Format Box
 * This box shows the data format of the stored sound media.
 * ISO base media file format also defines the same four-character-code for the type field,
 * however, that is used to indicate original sample description of the media when a protected sample entry is used. */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    uint32_t data_format;       /* copy of sample description type */
} isom_frma_t;

/* Audio Endian Box */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    int16_t littleEndian;
} isom_enda_t;

/* MPEG-4 Audio Box */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    uint32_t unknown;           /* always 0? */
} isom_mp4a_t;

/* Terminator Box
 * This box is present to indicate the end of the sound description. It contains no data. */
typedef struct
{
    ISOM_BASEBOX_COMMON;    /* size = 8, type = 0x00000000 */
} isom_terminator_t;

/* Sound Information Decompression Parameters Box
 * This box provides the ability to store data specific to a given audio decompressor in the sound description.
 * The contents of this box are dependent on the audio decompressor. */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    isom_frma_t       *frma;            /* Format Box */
    isom_enda_t       *enda;            /* Audio Endian Box */
    isom_mp4a_t       *mp4a;            /* MPEG-4 Audio Box */
    isom_esds_t       *esds;            /* ES Descriptor Box */
    isom_terminator_t *terminator;      /* Terminator Box */
} isom_wave_t;

/* Channel Compositor Box */
typedef struct
{
    uint32_t channelLabel;          /* the channelLabel that describes the channel */
    uint32_t channelFlags;          /* flags that control the interpretation of coordinates */
    uint32_t coordinates[3];        /* an ordered triple that specifies a precise speaker location / 32-bit floating point */
} isom_channel_description_t;

typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint32_t channelLayoutTag;              /* the channelLayoutTag indicates the layout */
    uint32_t channelBitmap;                 /* If channelLayoutTag is set to 0x00010000, this field is the channel usage bitmap. */
    uint32_t numberChannelDescriptions;     /* the number of items in the Channel Descriptions array */
    /* Channel Descriptions array */
    isom_channel_description_t *channelDescriptions;
} isom_chan_t;

/* ISOM: Audio Sample Entry / QTFF: Sound Description */
typedef struct
{
    ISOM_SAMPLE_ENTRY;
    int16_t  version;           /* ISOM: reserved
                                 * QTFF: sample description version
                                 *       version = 0 supports only 'raw ' or 'twos' audio format.
                                 *       version = 1 is used to support out-of-band configuration settings for decompression.
                                 *       version = 2 is used to support high samplerate or 3 or more multichannel audio. */
    int16_t  revision_level;    /* ISOM: reserved / QTFF: version of the CODEC */
    int32_t  vendor;            /* ISOM: reserved / QTFF: whose CODEC */
    uint16_t channelcount;      /* ISOM: template: channelcount = 2
                                 * QTFF: the number of audio channels
                                 *       Allowable values are 1 (mono) or 2 (stereo).
                                 *       For more than 2, set this field to 3 and use numAudioChannels instead of this field. */
    uint16_t samplesize;        /* ISOM: template: samplesize = 16
                                 * QTFF: the number of bits in each uncompressed sample for a single channel
                                 *       Allowable values are 8 or 16.
                                 *       For non-mod8, set this field to 16 and use constBitsPerChannel instead of this field.
                                 *       For more than 16, set this field to 16 and use bytesPerPacket instead of this field. */
    int16_t  compression_ID;    /* ISOM: pre_defined
                                 * QTFF: version = 0 -> must be set to 0.
                                 *       version = 2 -> must be set to -2. */
    uint16_t packet_size;       /* ISOM: reserved / QTFF: must be set to 0. */
    uint32_t samplerate;        /* the sampling rate expressed as a 16.16 fixed-point number
                                 * ISOM: template: samplerate = {default samplerate of media}<<16
                                 * QTFF: the integer portion should match the media's timescale.
                                 *       If this field is invalid because of higher samplerate,
                                 *       then set this field to 0x00010000 and use audioSampleRate instead of this field. */
    /* version 1 fields
     * These fields are for description of the compression ratio of fixed ratio audio compression algorithms.
     * If these fields are not used, they are set to 0. */
    uint32_t samplesPerPacket;      /* For compressed audio, be set to the number of uncompressed frames generated by a compressed frame.
                                     * For uncompressed audio, shall be set to 1. */
    uint32_t bytesPerPacket;        /* the number of bytes in a sample for a single channel */
    uint32_t bytesPerFrame;         /* the number of bytes in a frame */
    uint32_t bytesPerSample;        /* 8-bit audio: 1, other audio: 2 */
    /* version 2 fields
     * LPCMFrame: one sample from each channel.
     * AudioPacket: For uncompressed audio, an AudioPacket is simply one LPCMFrame.
     *              For compressed audio, an AudioPacket is the natural compressed access unit of that format. */
    uint32_t sizeOfStructOnly;                  /* offset to extensions */
    uint64_t audioSampleRate;                   /* 64-bit floating point */
    uint32_t numAudioChannels;                  /* any channel assignment info will be in Channel Compositor Box. */
    int32_t  always7F000000;                    /* always 0x7F000000 */
    uint32_t constBitsPerChannel;               /* only set if constant (and only for uncompressed audio) */
    uint32_t formatSpecificFlags;
    uint32_t constBytesPerAudioPacket;          /* only set if constant */
    uint32_t constLPCMFramesPerAudioPacket;     /* only set if constant */
    /* extensions */
    isom_esds_t *esds;      /* ISOM: ES Descriptor Box / QTFF: null */
    isom_wave_t *wave;      /* ISOM: null / QTFF: Sound Information Decompression Parameters Box */
    isom_chan_t *chan;      /* ISOM: null / QTFF: Channel Compositor Box @ optional */

        uint32_t exdata_length;
        void *exdata;
        lsmash_audio_summary_t summary;
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

/* QuickTime Text Sample Description */
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

/* FontRecord */
typedef struct
{
    uint16_t font_ID;
    /* Pascal string */
    uint8_t font_name_length;
    char *font_name;
} isom_font_record_t;

/* Font Table Box */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    /* FontRecord
     * entry_count is uint16_t. */
    lsmash_entry_list_t *list;
} isom_ftab_t;

/* Timed Text Sample Entry */
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

/* Sample Description Box */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    lsmash_entry_list_t *list;
} isom_stsd_t;
/** **/

/* Decoding Time to Sample Box
 * This box contains a compact version of a table that allows indexing from decoding time to sample number.
 * Each entry in the table gives the number of consecutive samples with the same time delta, and the delta of those samples.
 * By adding the deltas a complete time-to-sample map may be built.
 * All samples must have non-zero durations except for the last one.
 * The sum of all deltas gives the media duration in the track (not mapped to the movie timescale, and not considering any edit list).
 * DTS is an abbreviation of 'decoding time stamp'. */
typedef struct
{
    uint32_t sample_count;      /* number of consecutive samples that have the given sample_delta */
    uint32_t sample_delta;      /* DTS[0] = 0; DTS[n+1] = DTS[n] + sample_delta[n]; */
} isom_stts_entry_t;

typedef struct
{
    ISOM_FULLBOX_COMMON;
    lsmash_entry_list_t *list;
} isom_stts_t;

/* Composition Time to Sample Box
 * This box provides the offset between decoding time and composition time.
 * CTS is an abbreviation of 'composition time stamp'.
 * This box is optional and must only be present if DTS and CTS differ for any samples.
 * ISOM: if version is set to 1, sample_offset is signed 32-bit integer.
 * QTFF: sample_offset is always signed 32-bit integer. */
typedef struct
{
    uint32_t sample_count;      /* number of consecutive samples that have the given sample_offset */
    uint32_t sample_offset;     /* CTS[n] = DTS[n] + sample_offset[n]; */
} isom_ctts_entry_t;

typedef struct
{
    ISOM_FULLBOX_COMMON;
    lsmash_entry_list_t *list;
} isom_ctts_t;

/* Composition to Decode Box (Composition Shift Least Greatest Box)
 * This box may be used to relate the composition and decoding timelines,
 * and deal with some of the ambiguities that signed composition offsets introduce. */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    int32_t compositionToDTSShift;          /* If this value is added to the composition times (as calculated by the CTS offsets from the DTS),
                                             * then for all samples, their CTS is guaranteed to be greater than or equal to their DTS,
                                             * and the buffer model implied by the indicated profile/level will be honoured;
                                             * if leastDecodeToDisplayDelta is positive or zero, this field can be 0;
                                             * otherwise it should be at least (- leastDecodeToDisplayDelta). */
    int32_t leastDecodeToDisplayDelta;      /* the smallest sample_offset in this track */
    int32_t greatestDecodeToDisplayDelta;   /* the largest sample_offset in this track */
    int32_t compositionStartTime;           /* the smallest CTS for any sample */
    int32_t compositionEndTime;             /* the CTS plus the composition duration, of the sample with the largest CTS in this track */
} isom_cslg_t;

/* Sample Size Box
 * This box contains the sample count and a table giving the size in bytes of each sample.
 * The total number of samples in the media is always indicated in the sample_count.
 * Note: a sample size of zero is not prohibited in general, but it must be valid and defined for the coding system,
 *       as defined by the sample entry, that the sample belongs to. */
typedef struct
{
    uint32_t entry_size;        /* the size of a sample */
} isom_stsz_entry_t;

typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint32_t sample_size;           /* If this field is set to 0, then the samples have different sizes. */
    uint32_t sample_count;          /* the number of samples in the track */
    lsmash_entry_list_t *list;      /* available if sample_size == 0 */
} isom_stsz_t;

/* Sync Sample Box
 * If this box is not present, every sample is a random access point.
 * In AVC streams, this box cannot point non-IDR samples.
 * The table is arranged in strictly increasing order of sample number. */
typedef struct
{
    uint32_t sample_number;     /* the numbers of the samples that are random access points in the stream. */
} isom_stss_entry_t;

typedef struct
{
    ISOM_FULLBOX_COMMON;
    lsmash_entry_list_t *list;
} isom_stss_t;

/* Partial Sync Sample Box
 * Tip from QT engineering - Open-GOP intra frames need to be marked as "partial sync samples".
 * Partial sync frames perform a partial reset of inter-frame dependencies;
 * decoding two partial sync frames and the non-droppable difference frames between them is
 * sufficient to prepare a decompressor for correctly decoding the difference frames that follow. */
typedef struct
{
    uint32_t sample_number;     /* the numbers of the samples that are partial sync samples in the stream. */
} isom_stps_entry_t;

typedef struct
{
    ISOM_FULLBOX_COMMON;
    lsmash_entry_list_t *list;
} isom_stps_t;

/* Independent and Disposable Samples Box */
typedef struct
{
    unsigned is_leading            : 2;     /* ISOM: leading / QTFF: samples later in decode order may have earlier display times */
    unsigned sample_depends_on     : 2;     /* independency */
    unsigned sample_is_depended_on : 2;     /* disposable */
    unsigned sample_has_redundancy : 2;     /* redundancy */
} isom_sdtp_entry_t;

typedef struct
{
    ISOM_FULLBOX_COMMON;
    /* According to the specification, the size of the table, sample_count, doesn't exist in this box.
     * Instead of this, it is taken from the sample_count in the stsz or the stz2 box. */
    lsmash_entry_list_t *list;
} isom_sdtp_t;

/* Sample To Chunk Box
 * This box can be used to find the chunk that contains a sample, its position, and the associated sample description.
 * The table is compactly coded. Each entry gives the index of the first chunk of a run of chunks with the same characteristics.
 * By subtracting one entry here from the previous one, you can compute how many chunks are in this run.
 * You can convert this to a sample count by multiplying by the appropriate samples_per_chunk. */
typedef struct
{
    uint32_t first_chunk;                   /* the index of the first chunk in this run of chunks that share the same samples_per_chunk and sample_description_index */
    uint32_t samples_per_chunk;             /* the number of samples in each of these chunks */
    uint32_t sample_description_index;      /* the index of the sample entry that describes the samples in this chunk */
} isom_stsc_entry_t;

typedef struct
{
    ISOM_FULLBOX_COMMON;
    lsmash_entry_list_t *list;
} isom_stsc_t;

/* Chunk Offset Box
 * chunk_offset is the offset of the start of a chunk into its containing media file.
 * Offsets are file offsets, not the offset into any box within the file. */
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
    ISOM_FULLBOX_COMMON;        /* type = 'stco': 32-bit chunk offsets / type = 'co64': 64-bit chunk offsets */
    lsmash_entry_list_t *list;

        uint8_t large_presentation;     /* Set 1 to this if 64-bit chunk-offset are needed. */
} isom_stco_t;      /* share with co64 box */

/* Sample Group Description Box
 * This box gives information about the characteristics of sample groups. */
typedef struct
{
    ISOM_FULLBOX_COMMON;            /* Use of version 0 entries is deprecated. */
    uint32_t grouping_type;         /* an integer that identifies the sbgp that is associated with this sample group description */
    uint32_t default_length;        /* the length of every group entry (if the length is constant), or zero (if it is variable)
                                     * This field is available only if version == 1. */
    lsmash_entry_list_t *list;
} isom_sgpd_entry_t;

/* Random Access Entry
 * Samples marked by this group must be random access points, and may also be sync points. */
typedef struct
{
    /* grouping_type is 'rap ' */
    uint32_t description_length;                /* This field is available only if version == 1 and default_length == 0. */
    unsigned num_leading_samples_known : 1;     /* the value of one indicates that the number of leading samples is known for each sample in this group,
                                                 * and the number is specified by num_leading_samples. */
    unsigned num_leading_samples       : 7;     /* the number of leading samples for each sample in this group
                                                 * Note: when num_leading_samples_known is equal to 0, this field should be ignored. */
} isom_rap_entry_t;

/* Roll Recovery Entry
 * This grouping type is defined as that group of samples having the same roll distance. */
typedef struct
{
    /* grouping_type is 'roll' */
    uint32_t description_length;        /* This field is available only if version == 1 and default_length == 0. */
    int16_t roll_distance;              /* the number of samples that must be decoded in order for a sample to be decoded correctly
                                         * The value zero must not be used. */
} isom_roll_entry_t;

/* Sample to Group Box
 * This box is used to find the group that a sample belongs to and the associated description of that sample group. */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint32_t grouping_type;             /* Links it to its sample group description table with the same value for grouping type. */
    uint32_t grouping_type_parameter;   /* an indication of the sub-type of the grouping
                                         * This field is available only if version == 1. */
    lsmash_entry_list_t *list;
} isom_sbgp_entry_t;

typedef struct
{
    uint32_t sample_count;                  /* the number of consecutive samples with the same sample group descriptor */
    uint32_t group_description_index;       /* the index of the sample group entry which describes the samples in this group
                                             * The index ranges from 1 to the number of sample group entries in the Sample Group Description Box,
                                             * or takes the value 0 to indicate that this sample is a member of no group of this type. */
} isom_group_assignment_entry_t;

/* Sample Table Box */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    isom_stsd_t *stsd;      /* Sample Description Box */
    isom_stts_t *stts;      /* Decoding Time to Sample Box */
    isom_ctts_t *ctts;      /* Composition Time to Sample Box */
    isom_cslg_t *cslg;      /* ISOM: Composition to Decode Box / QTFF: Composition Shift Least Greatest Box */
    isom_stss_t *stss;      /* Sync Sample Box */
    isom_stps_t *stps;      /* ISOM: null / QTFF: Partial Sync Sample Box */
    isom_sdtp_t *sdtp;      /* Independent and Disposable Samples Box */
    isom_stsc_t *stsc;      /* Sample To Chunk Box */
    isom_stsz_t *stsz;      /* Sample Size Box */
    isom_stco_t *stco;      /* Chunk Offset Box */
    lsmash_entry_list_t *sgpd_list;      /* ISOM: Sample Group Description Boxes / QTFF: null */
    lsmash_entry_list_t *sbgp_list;      /* ISOM: Sample To Group Boxes / QTFF: null */
} isom_stbl_t;

/* Media Information Box */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    /* Media Information Header Boxes */
    isom_vmhd_t *vmhd;      /* Video Media Header Box */
    isom_smhd_t *smhd;      /* Sound Media Header Box */
    isom_hmhd_t *hmhd;      /* ISOM: Hint Media Header Box / QTFF: null */
    isom_nmhd_t *nmhd;      /* ISOM: Null Media Header Box / QTFF: null */
    isom_gmhd_t *gmhd;      /* ISOM: null / QTFF: Generic Media Information Header Box */
    /* */
    isom_hdlr_t *hdlr;      /* ISOM: null / QTFF: Data Handler Reference Box
                             * Note: this box must come before Data Information Box. */
    isom_dinf_t *dinf;      /* Data Information Box */
    isom_stbl_t *stbl;      /* Sample Table Box */
} isom_minf_t;

/* Media Box */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    isom_mdhd_t *mdhd;      /* Media Header Box */
    isom_hdlr_t *hdlr;      /* ISOM: Handler Reference Box / QTFF: Media Handler Reference Box
                             * Note: this box must come before Media Information Box. */
    isom_minf_t *minf;      /* Media Information Box */
} isom_mdia_t;

/* Movie Header Box
 * This box defines overall information which is media-independent, and relevant to the entire presentation considered as a whole. */
typedef struct
{
    ISOM_FULLBOX_COMMON;            /* version is either 0 or 1 */
    /* version == 0: uint64_t -> uint32_t */
    uint64_t creation_time;         /* the creation time of the presentation (in seconds since midnight, Jan. 1, 1904, in UTC time) */
    uint64_t modification_time;     /* the most recent time the presentation was modified (in seconds since midnight, Jan. 1, 1904, in UTC time) */
    uint32_t timescale;             /* movie timescale: timescale for the entire presentation */
    uint64_t duration;              /* the duration, expressed in movie timescale, of the longest track */
    /* The following fields are treated as reserved in MP4 version 1. */
    int32_t  rate;                  /* fixed point 16.16 number. 0x00010000 is normal forward playback. */
    int16_t  volume;                /* fixed point 8.8 number. 0x0100 is full volume. */
    int16_t  reserved;
    int32_t  preferredLong[2];      /* ISOM: reserved / QTFF: unknown */
    int32_t  matrix[9];             /* transformation matrix for the video */
    /* The following fields are defined in QuickTime file format.
     * In ISO Base Media file format, these fields are treated as pre_defined. */
    int32_t  previewTime;           /* the time value in the movie at which the preview begins */
    int32_t  previewDuration;       /* the duration of the movie preview in movie timescale units */
    int32_t  posterTime;            /* the time value of the time of the movie poster */
    int32_t  selectionTime;         /* the time value for the start time of the current selection */
    int32_t  selectionDuration;     /* the duration of the current selection in movie timescale units */
    int32_t  currentTime;           /* the time value for current time position within the movie */
    /* */
    uint32_t next_track_ID;         /* larger than the largest track-ID in use */
} isom_mvhd_t;

/* Object Descriptor Box
 * Note that this box is mandatory under 14496-1:2001 (mp41) while not mandatory under 14496-14:2003 (mp42). */
struct mp4sys_ObjectDescriptor_t; /* FIXME: I think these structs using mp4sys should be placed in isom.c */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    struct mp4sys_ObjectDescriptor_t *OD;
} isom_iods_t;

/* Media Data Box
 * This box contains the media data.
 * A presentation may contain zero or more Media Data Boxes.*/
typedef struct
{
    ISOM_BASEBOX_COMMON;    /* If size is 0, then this box is the last box. */

        uint64_t placeholder_pos;       /* placeholder position for largesize */
} isom_mdat_t;

/* Free Space Box
 * The contents of a free-space box are irrelevant and may be ignored without affecting the presentation. */
typedef struct
{
    ISOM_BASEBOX_COMMON;    /* type is 'free' or 'skip' */
    uint32_t length;
    uint8_t *data;
} isom_free_t;

typedef isom_free_t isom_skip_t;

/* Chapter List Box
 * This box is NOT defined in the ISO/MPEG-4 specs. */
typedef struct
{
    uint64_t start_time;    /* version = 0: expressed in movie timescale units
                             * version = 1: expressed in 100 nanoseconds */
    /* Chapter name is Pascal string */
    uint8_t chapter_name_length;
    char *chapter_name;
} isom_chpl_entry_t;

typedef struct
{
    ISOM_FULLBOX_COMMON;            /* version = 0 is defined in F4V file format. */
    uint8_t unknown;                /* only available under version = 1 */
    lsmash_entry_list_t *list;      /* if version is set to 0, entry_count is uint8_t. */
} isom_chpl_t;

typedef struct
{
    char *chapter_name;
    uint64_t start_time;
} isom_chapter_entry_t;

/* User Data Box
 * This box is a container box for informative user-data.
 * This user data is formatted as a set of boxes with more specific box types, which declare more precisely their content.
 * QTFF: for historical reasons, this box is optionally terminated by a 32-bit integer set to 0. */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    isom_chpl_t *chpl;      /* Chapter List Box */
} isom_udta_t;

/** Caches for handling tracks **/
typedef struct
{
    uint32_t chunk_number;                  /* chunk number */
    uint32_t sample_description_index;      /* sample description index */
    uint64_t first_dts;                     /* the first DTS in chunk */
    uint64_t pool_size;                     /* the sum of the size of samples in the pool */
    lsmash_entry_list_t *pool;              /* samples pooled to interleave */
} isom_chunk_t;

typedef struct
{
    uint64_t dts;
    uint64_t cts;
} isom_timestamp_t;

typedef struct
{
    isom_group_assignment_entry_t *assignment;      /* the address corresponding to the entry in Sample to Group Box */
    isom_rap_entry_t  *random_access;               /* the address corresponding to the random access entry in Sample Group Description Box */
    uint8_t            is_prev_rap;                 /* whether the previous sample is a random access point or not */
} isom_rap_group_t;

typedef struct
{
    isom_group_assignment_entry_t *assignment;      /* the address corresponding to the entry in Sample to Group Box */
    uint32_t first_sample;                          /* the number of the first sample of the group */
    uint32_t recovery_point;                        /* the identifier necessary for the recovery from its starting point to be completed */
    uint8_t delimited;                              /* the flag if the sample_count is determined */
    uint8_t described;                              /* the flag if the group description is determined */
} isom_roll_group_t;

typedef struct
{
    lsmash_entry_list_t *pool;        /* grouping pooled to delimit and describe */
} isom_grouping_t;

typedef struct
{
    uint8_t has_samples;
    uint32_t traf_number;
    uint32_t last_duration;     /* the last sample duration in this track fragment */
    uint64_t largest_cts;       /* the largest CTS in this track fragments */
} isom_fragment_t;

typedef struct
{
    uint8_t all_sync;       /* if all samples are sync sample */
    isom_chunk_t chunk;
    isom_timestamp_t timestamp;
    isom_grouping_t roll;
    isom_rap_group_t *rap;
    isom_fragment_t *fragment;
} isom_cache_t;

/** Movie Fragments Boxes **/
/* Track Fragments Flags ('tf_flags') */
typedef enum
{
    ISOM_TF_FLAGS_BASE_DATA_OFFSET_PRESENT               = 0x000001,    /* base_data_offset field exists. */
    ISOM_TF_FLAGS_SAMPLE_DESCRIPTION_INDEX_PRESENT       = 0x000002,    /* sample_description_index field exists. */
    ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT        = 0x000008,    /* default_sample_duration field exists. */
    ISOM_TF_FLAGS_DEFAULT_SAMPLE_SIZE_PRESENT            = 0x000010,    /* default_sample_size field exists. */
    ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT           = 0x000020,    /* default_sample_flags field exists. */
    ISOM_TF_FLAGS_DURATION_IS_EMPTY                      = 0x010000,    /* There are no samples for this time interval. */
} isom_tf_flags_code;

/* Track Run Flags ('tr_flags') */
typedef enum
{
    ISOM_TR_FLAGS_DATA_OFFSET_PRESENT                    = 0x000001,    /* data_offset field exists. */
    ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT             = 0x000004,    /* first_sample_flags field exists. */
    ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT                = 0x000100,    /* sample_duration field exists. */
    ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT                    = 0x000200,    /* sample_size field exists. */
    ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT                   = 0x000400,    /* sample_flags field exists. */
    ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT = 0x000800,    /* sample_composition_time_offset field exists. */
} isom_tr_flags_code;

/* Sample Flags */
typedef struct
{
    unsigned reserved                  : 4;
    /* The definition of the following fields is quite the same as Independent and Disposable Samples Box. */
    unsigned is_leading                : 2;
    unsigned sample_depends_on         : 2;
    unsigned sample_is_depended_on     : 2;
    unsigned sample_has_redundancy     : 2;
    /* */
    unsigned sample_padding_value      : 3;     /* the number of bits at the end of this sample */
    unsigned sample_is_non_sync_sample : 1;     /* 0 value means this sample is sync sample. */
    uint16_t sample_degradation_priority;
} isom_sample_flags_t;

/* Movie Extends Header Box
 * This box is omitted when used in live streaming.
 * If this box is not present, the overall duration must be computed by examining each fragment. */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    /* version == 0: uint64_t -> uint32_t */
    uint64_t fragment_duration;     /* the duration of the longest track, in the timescale indicated in the Movie Header Box, including movie fragments. */
} isom_mehd_t;

/* Track Extends Box
 * This box sets up default values used by the movie fragments. */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint32_t            track_ID;       /* identifier of the track; this shall be the track ID of a track in the Movie Box */
    uint32_t            default_sample_description_index;
    uint32_t            default_sample_duration;
    uint32_t            default_sample_size;
    isom_sample_flags_t default_sample_flags;
} isom_trex_entry_t;

/* Movie Extends Box
 * This box warns readers that there might be Movie Fragment Boxes in this file. */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    isom_mehd_t         *mehd;          /* Movie Extends Header Box / omitted when used in live streaming */
    lsmash_entry_list_t *trex_list;     /* Track Extends Box */

        uint64_t placeholder_pos;       /* placeholder position for Movie Extends Header Box */ 
} isom_mvex_t;

/* Movie Fragment Header Box
 * This box contains a sequence number, as a safety check.
 * The sequence number 'usually' starts at 1 and must increase for each movie fragment in the file, in the order in which they occur. */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint32_t sequence_number;       /* the ordinal number of this fragment, in increasing order */
} isom_mfhd_t;

/* Track Fragment Header Box
 * Each movie fragment can contain zero or more fragments for each track;
 * and a track fragment can contain zero or more contiguous runs of samples.
 * This box sets up information and defaults used for those runs of samples. */
typedef struct
{
    ISOM_FULLBOX_COMMON;                            /* flags field is used for 'tf_flags'. */
    uint32_t            track_ID;
    /* all the following are optional fields */
    uint64_t            base_data_offset;           /* an explicit anchor for the data offsets in each track run
                                                     * Offsets are file offsets as like as chunk_offset in Chunk Offset Box.
                                                     * If not provided, the base_data_offset for the first track in the movie fragment is the position 
                                                     * of the first byte of the enclosing Movie Fragment Box, and for second and subsequent track fragments,
                                                     * the default is the end of the data defined by the preceding fragment.
                                                     * To avoid the case this field might overflow, e.g. semi-permanent live streaming and broadcasting,
                                                     * you shall not use this optional field. */
    uint32_t            sample_description_index;   /* override default_sample_description_index in Track Extends Box */
    uint32_t            default_sample_duration;    /* override default_sample_duration in Track Extends Box */
    uint32_t            default_sample_size;        /* override default_sample_size in Track Extends Box */
    isom_sample_flags_t default_sample_flags;       /* override default_sample_flags in Track Extends Box */
} isom_tfhd_t;

/* Track Fragment Run Box
 * Within the Track Fragment Box, there are zero or more Track Fragment Run Boxes.
 * If the duration-is-empty flag is set in the tf_flags, there are no track runs.
 * A track run documents a contiguous set of samples for a track. */
typedef struct
{
    ISOM_FULLBOX_COMMON;                        /* flags field is used for 'tr_flags'. */
    uint32_t            sample_count;           /* the number of samples being added in this run; also the number of rows in the following table */
    /* The following are optional fields. */
    int32_t             data_offset;            /* This value is added to the implicit or explicit data_offset established in the Track Fragment Header Box.
                                                 * If this field is not present, then the data for this run starts immediately after the data of the previous run,
                                                 * or at the base_data_offset defined by the Track Fragment Header Box if this is the first run in a track fragment. */
    isom_sample_flags_t first_sample_flags;     /* a set of flags for the first sample only of this run */
    lsmash_entry_list_t *optional;              /* all fields in this array are optional. */
} isom_trun_entry_t;

typedef struct
{
    /* If the following fields is present, each field overrides default value described in Track Fragment Header Box or Track Extends Box. */
    uint32_t            sample_duration;                    /* override default_sample_duration */
    uint32_t            sample_size;                        /* override default_sample_size */
    isom_sample_flags_t sample_flags;                       /* override default_sample_flags */
    /* */
    uint32_t            sample_composition_time_offset;     /* composition time offset */
} isom_trun_optional_row_t;

/* Track Fragment Box */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    isom_tfhd_t         *tfhd;          /* Track Fragment Header Box */
    lsmash_entry_list_t *trun_list;     /* Track Fragment Run Box List
                                         * If the duration-is-empty flag is set in the tf_flags, there are no track runs. */

        isom_cache_t *cache;
} isom_traf_entry_t;

/* Movie Fragment Box */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    isom_mfhd_t         *mfhd;          /* Movie Fragment Header Box */
    lsmash_entry_list_t *traf_list;     /* Track Fragment Box List */
} isom_moof_entry_t;

/* Track Fragment Random Access Box
 * Each entry in this box contains the location and the presentation time of the random accessible sample.
 * Note that not every random accessible sample in the track needs to be listed in the table.
 * The absence of this box does not mean that all the samples are sync samples. */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint32_t track_ID;
    unsigned int reserved                  : 26;
    unsigned int length_size_of_traf_num   : 2;     /* the length in byte of the traf_number field minus one */
    unsigned int length_size_of_trun_num   : 2;     /* the length in byte of the trun_number field minus one */
    unsigned int length_size_of_sample_num : 2;     /* the length in byte of the sample_number field minus one */
    uint32_t number_of_entry;                       /* the number of the entries for this track
                                                     * Value zero indicates that every sample is a random access point and no table entry follows. */
    lsmash_entry_list_t *list;                      /* entry_count corresponds to number_of_entry. */
} isom_tfra_entry_t;

typedef struct
{
    /* version == 0: 64bits -> 32bits */
    uint64_t time;              /* the presentation time of the random access sample in units defined in the Media Header Box of the associated track
                                 * According to 14496-12:2008/FPDAM 3, presentation times are composition times. */
    uint64_t moof_offset;       /* the offset of the Movie Fragment Box used in this entry
                                 * Offset is the byte-offset between the beginning of the file and the beginning of the Movie Fragment Box. */
    /* */
    uint32_t traf_number;       /* the Track Fragment Box ('traf') number that contains the random accessible sample
                                 * The number ranges from 1 in each Movie Fragment Box ('moof'). */
    uint32_t trun_number;       /* the Track Fragment Run Box ('trun') number that contains the random accessible sample
                                 * The number ranges from 1 in each Track Fragment Box ('traf'). */
    uint32_t sample_number;     /* the sample number that contains the random accessible sample
                                 * The number ranges from 1 in each Track Fragment Run Box ('trun'). */
} isom_tfra_location_time_entry_t;

/* Movie Fragment Random Access Offset Box
 * This box provides a copy of the length field from the enclosing Movie Fragment Random Access Box. */
typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint32_t length;        /* an integer gives the number of bytes of the enclosing Movie Fragment Random Access Box
                             * This field is placed at the last of the enclosing box to assist readers scanning
                             * from the end of the file in finding the Movie Fragment Random Access Box. */
} isom_mfro_t;

/* Movie Fragment Random Access Box
 * This box provides a table which may assist readers in finding random access points in a file using movie fragments,
 * and is usually placed at or near the end of the file.
 * The last box within the Movie Fragment Random Access Box, which is called Movie Fragment Random Access Offset Box,
 * provides a copy of the length field from the Movie Fragment Random Access Box. */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    lsmash_entry_list_t *tfra_list;     /* Track Fragment Random Access Box */
    isom_mfro_t         *mfro;          /* Movie Fragment Random Access Offset Box */
} isom_mfra_t;

/* Movie fragment manager 
 * The presence of this means we use the structure of movie fragments. */
typedef struct
{
    isom_moof_entry_t *movie;       /* the address corresponding to the current Movie Fragment Box */
    uint64_t fragment_count;        /* the number of movie fragments we created */
    uint64_t pool_size;
    lsmash_entry_list_t *pool;      /* samples pooled to interleave for the current movie fragment */
} isom_fragment_manager_t;

/** **/

/* Movie Box */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    isom_mvhd_t         *mvhd;          /* Movie Header Box */
    isom_iods_t         *iods;          /* ISOM: Object Descriptor Box / QTFF: null */
    lsmash_entry_list_t *trak_list;     /* Track Box List */
    isom_udta_t         *udta;          /* User Data Box */
    isom_mvex_t         *mvex;          /* Movie Extends Box */
} isom_moov_t;

/* ROOT */
struct lsmash_root_tag
{
    ISOM_FULLBOX_COMMON;                /* the size field expresses total file size 
                                         * the flags field expresses file mode */
    isom_ftyp_t         *ftyp;          /* File Type Box */
    isom_moov_t         *moov;          /* Movie Box */
    lsmash_entry_list_t *moof_list;     /* Movie Fragment Box List */
    isom_mdat_t         *mdat;          /* Media Data Box */
    isom_free_t         *free;          /* Free Space Box */
    isom_mfra_t         *mfra;          /* Movie Fragment Random Access Box */

        lsmash_bs_t *bs;                    /* bytestream manager */
        isom_fragment_manager_t *fragment;  /* movie fragment manager */
        double max_chunk_duration;          /* max duration per chunk in seconds */
        double max_async_tolerance;         /* max tolerance, in seconds, for amount of interleaving asynchronization between tracks */
        uint8_t qt_compatible;              /* compatibility with QuickTime file format */
        uint8_t isom_compatible;            /* compatibility with ISO Base Media file format */
        uint8_t avc_extensions;             /* compatibility with AVC extensions */
        uint8_t mp4_version1;               /* compatibility with MP4 ver.1 file format */
        uint8_t mp4_version2;               /* compatibility with MP4 ver.2 file format */
        uint8_t itunes_audio;               /* compatibility with iTunes Audio */
        uint8_t max_3gpp_version;           /* maximum 3GPP version */
        uint8_t max_isom_version;           /* maximum ISO Base Media file format version */
        lsmash_entry_list_t *print;
};

/* Track Box */
typedef struct
{
    ISOM_BASEBOX_COMMON;
    isom_tkhd_t *tkhd;          /* Track Header Box */
    isom_tapt_t *tapt;          /* ISOM: null / QTFF: Track Aperture Mode Dimensions Box */
    isom_edts_t *edts;          /* Edit Box */
    isom_tref_t *tref;          /* Track Reference Box */
    isom_mdia_t *mdia;          /* Media Box */
    isom_udta_t *udta;          /* User Data Box */

        isom_cache_t *cache;
        uint32_t related_track_ID;
        uint8_t is_chapter;
} isom_trak_entry_t;
/** **/

typedef struct
{
    uint16_t mac_value;
    uint16_t iso_name;
} isom_language_t;

static const isom_language_t isom_languages[] =
{
    {   0, ISOM_LANGUAGE_CODE_ENGLISH },
    {   1, ISOM_LANGUAGE_CODE_FRENCH },
    {   2, ISOM_LANGUAGE_CODE_GERMAN },
    {   3, ISOM_LANGUAGE_CODE_ITALIAN },
    {   4, ISOM_LANGUAGE_CODE_DUTCH_M },
    {   5, ISOM_LANGUAGE_CODE_SWEDISH },
    {   6, ISOM_LANGUAGE_CODE_SPANISH },
    {   7, ISOM_LANGUAGE_CODE_DANISH },
    {   8, ISOM_LANGUAGE_CODE_PORTUGUESE },
    {   9, ISOM_LANGUAGE_CODE_NORWEGIAN },
    {  10, ISOM_LANGUAGE_CODE_HEBREW },
    {  11, ISOM_LANGUAGE_CODE_JAPANESE },
    {  12, ISOM_LANGUAGE_CODE_ARABIC },
    {  13, ISOM_LANGUAGE_CODE_FINNISH },
    {  14, ISOM_LANGUAGE_CODE_GREEK },
    {  15, ISOM_LANGUAGE_CODE_ICELANDIC },
    {  16, ISOM_LANGUAGE_CODE_MALTESE },
    {  17, ISOM_LANGUAGE_CODE_TURKISH },
    {  18, ISOM_LANGUAGE_CODE_CROATIAN },
    {  19, ISOM_LANGUAGE_CODE_CHINESE },
    {  20, ISOM_LANGUAGE_CODE_URDU },
    {  21, ISOM_LANGUAGE_CODE_HINDI },
    {  22, ISOM_LANGUAGE_CODE_THAI },
    {  23, ISOM_LANGUAGE_CODE_KOREAN },
    {  24, ISOM_LANGUAGE_CODE_LITHUANIAN },
    {  25, ISOM_LANGUAGE_CODE_POLISH },
    {  26, ISOM_LANGUAGE_CODE_HUNGARIAN },
    {  27, ISOM_LANGUAGE_CODE_ESTONIAN },
    {  28, ISOM_LANGUAGE_CODE_LATVIAN },
    {  29, ISOM_LANGUAGE_CODE_SAMI },
    {  30, ISOM_LANGUAGE_CODE_FAROESE },
    {  32, ISOM_LANGUAGE_CODE_RUSSIAN },
    {  33, ISOM_LANGUAGE_CODE_CHINESE },
    {  34, ISOM_LANGUAGE_CODE_DUTCH },
    {  35, ISOM_LANGUAGE_CODE_IRISH },
    {  36, ISOM_LANGUAGE_CODE_ALBANIAN },
    {  37, ISOM_LANGUAGE_CODE_ROMANIAN },
    {  38, ISOM_LANGUAGE_CODE_CZECH },
    {  39, ISOM_LANGUAGE_CODE_SLOVAK },
    {  40, ISOM_LANGUAGE_CODE_SLOVENIA },
    {  41, ISOM_LANGUAGE_CODE_YIDDISH },
    {  42, ISOM_LANGUAGE_CODE_SERBIAN },
    {  43, ISOM_LANGUAGE_CODE_MACEDONIAN },
    {  44, ISOM_LANGUAGE_CODE_BULGARIAN },
    {  45, ISOM_LANGUAGE_CODE_UKRAINIAN },
    {  46, ISOM_LANGUAGE_CODE_BELARUSIAN },
    {  47, ISOM_LANGUAGE_CODE_UZBEK },
    {  48, ISOM_LANGUAGE_CODE_KAZAKH },
    {  49, ISOM_LANGUAGE_CODE_AZERBAIJANI },
    {  51, ISOM_LANGUAGE_CODE_ARMENIAN },
    {  52, ISOM_LANGUAGE_CODE_GEORGIAN },
    {  53, ISOM_LANGUAGE_CODE_MOLDAVIAN },
    {  54, ISOM_LANGUAGE_CODE_KIRGHIZ },
    {  55, ISOM_LANGUAGE_CODE_TAJIK },
    {  56, ISOM_LANGUAGE_CODE_TURKMEN },
    {  57, ISOM_LANGUAGE_CODE_MONGOLIAN },
    {  59, ISOM_LANGUAGE_CODE_PASHTO },
    {  60, ISOM_LANGUAGE_CODE_KURDISH },
    {  61, ISOM_LANGUAGE_CODE_KASHMIRI },
    {  62, ISOM_LANGUAGE_CODE_SINDHI },
    {  63, ISOM_LANGUAGE_CODE_TIBETAN },
    {  64, ISOM_LANGUAGE_CODE_NEPALI },
    {  65, ISOM_LANGUAGE_CODE_SANSKRIT },
    {  66, ISOM_LANGUAGE_CODE_MARATHI },
    {  67, ISOM_LANGUAGE_CODE_BENGALI },
    {  68, ISOM_LANGUAGE_CODE_ASSAMESE },
    {  69, ISOM_LANGUAGE_CODE_GUJARATI },
    {  70, ISOM_LANGUAGE_CODE_PUNJABI },
    {  71, ISOM_LANGUAGE_CODE_ORIYA },
    {  72, ISOM_LANGUAGE_CODE_MALAYALAM },
    {  73, ISOM_LANGUAGE_CODE_KANNADA },
    {  74, ISOM_LANGUAGE_CODE_TAMIL },
    {  75, ISOM_LANGUAGE_CODE_TELUGU },
    {  76, ISOM_LANGUAGE_CODE_SINHALESE },
    {  77, ISOM_LANGUAGE_CODE_BURMESE },
    {  78, ISOM_LANGUAGE_CODE_KHMER },
    {  79, ISOM_LANGUAGE_CODE_LAO },
    {  80, ISOM_LANGUAGE_CODE_VIETNAMESE },
    {  81, ISOM_LANGUAGE_CODE_INDONESIAN },
    {  82, ISOM_LANGUAGE_CODE_TAGALOG },
    {  83, ISOM_LANGUAGE_CODE_MALAY_ROMAN },
    {  84, ISOM_LANGUAGE_CODE_MAYAY_ARABIC },
    {  85, ISOM_LANGUAGE_CODE_AMHARIC },
    {  87, ISOM_LANGUAGE_CODE_OROMO },
    {  88, ISOM_LANGUAGE_CODE_SOMALI },
    {  89, ISOM_LANGUAGE_CODE_SWAHILI },
    {  90, ISOM_LANGUAGE_CODE_KINYARWANDA },
    {  91, ISOM_LANGUAGE_CODE_RUNDI },
    {  92, ISOM_LANGUAGE_CODE_CHEWA },
    {  93, ISOM_LANGUAGE_CODE_MALAGASY },
    {  94, ISOM_LANGUAGE_CODE_ESPERANTO },
    { 128, ISOM_LANGUAGE_CODE_WELSH },
    { 129, ISOM_LANGUAGE_CODE_BASQUE },
    { 130, ISOM_LANGUAGE_CODE_CATALAN },
    { 131, ISOM_LANGUAGE_CODE_LATIN },
    { 132, ISOM_LANGUAGE_CODE_QUECHUA },
    { 133, ISOM_LANGUAGE_CODE_GUARANI },
    { 134, ISOM_LANGUAGE_CODE_AYMARA },
    { 135, ISOM_LANGUAGE_CODE_TATAR },
    { 136, ISOM_LANGUAGE_CODE_UIGHUR },
    { 137, ISOM_LANGUAGE_CODE_DZONGKHA },
    { 138, ISOM_LANGUAGE_CODE_JAVANESE },
    { UINT16_MAX, 0 }
};

typedef struct
{
    uint16_t primaries;
    uint16_t transfer;
    uint16_t matrix;
} isom_color_parameter_t;

static const isom_color_parameter_t isom_color_parameter_tbl[] =
{
    { 2, 2, 2 },        /* Not specified */
    { 2, 2, 2 },        /* ITU-R BT.470 System M */
    { 5, 2, 6 },        /* ITU-R BT.470 System B, G */
    { 1, 1, 1 },        /* ITU-R BT.709 */
    { 6, 1, 6 },        /* SMPTE 170M */
    { 6, 7, 7 },        /* SMPTE 240M */
    { 1, 1, 1 },        /* SMPTE 274M */
    { 5, 1, 6 },        /* SMPTE 293M */
    { 1, 1, 1 },        /* SMPTE 296M */
};

enum qt_compression_id_code
{
    QT_COMPRESSION_ID_NOT_COMPRESSED            = 0,
    QT_COMPRESSION_ID_FIXED_COMPRESSION         = -1,
    QT_COMPRESSION_ID_VARIABLE_COMPRESSION      = -2,
    QT_COMPRESSION_ID_TWO_TO_ONE                = 1,
    QT_COMPRESSION_ID_EIGHT_TO_THREE            = 2,
    QT_COMPRESSION_ID_THREE_TO_ONE              = 3,
    QT_COMPRESSION_ID_SIX_TO_ONE                = 4,
    QT_COMPRESSION_ID_SIX_TO_ONE_PACKET_SIZE    = 8,
    QT_COMPRESSION_ID_THREE_TO_ONE_PACKET_SIZE  = 16,
};

enum qt_audio_format_flags_code
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
};

int isom_is_fullbox( void *box );
int isom_is_lpcm_audio( uint32_t type );
void isom_init_box_common( void *box, void *parent, uint32_t type );
void isom_remove_avcC_ps( isom_avcC_ps_entry_t *ps );
int isom_check_compatibility( lsmash_root_t *root );
char *isom_4cc2str( uint32_t fourcc );
char *isom_unpack_iso_language( uint16_t language );

#define isom_create_box( box_name, parent_name, box_4cc ) \
    isom_##box_name##_t *(box_name) = malloc( sizeof(isom_##box_name##_t) ); \
    if( !box_name ) \
        return -1; \
    memset( box_name, 0, sizeof(isom_##box_name##_t) ); \
    isom_init_box_common( box_name, parent_name, box_4cc )

#define isom_create_list_box( box_name, parent_name, box_4cc ) \
    isom_create_box( box_name, parent_name, box_4cc ); \
    box_name->list = lsmash_create_entry_list(); \
    if( !box_name->list ) \
    { \
        free( box_name ); \
        return -1; \
    }

#endif
