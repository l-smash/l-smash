/*****************************************************************************
 * mp4sys.h:
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

#ifndef MP4SYS_H
#define MP4SYS_H

#include <inttypes.h>

/***************************************************************************
    MPEG-4 Systems
***************************************************************************/

/* 8.6.6.2 Semantics Table 6 - objectTypeIndication Values */
typedef enum {
    MP4SYS_OBJECT_TYPE_Forbidden                          = 0x00, /* Forbidden */
    MP4SYS_OBJECT_TYPE_Systems_ISO_14496_1                = 0x01, /* Systems ISO/IEC 14496-1 */
    /* For all 14496-1 streams unless specifically indicated to the contrary.
       Scene Description scenes, which are identified with StreamType=0x03 (see Table 7), using
       this object type value shall use the BIFSConfig specified in section 9.3.5.2.2 of this
       specification. */
    MP4SYS_OBJECT_TYPE_Systems_ISO_14496_1_BIFSv2         = 0x02, /* Systems ISO/IEC 14496-1 */
    /* This object type shall be used, with StreamType=0x03 (see Table 7), for Scene
       Description streams that use the BIFSv2Config specified in section 9.3.5.3.2 of this
       specification. Its use with other StreamTypes is reserved. */

    MP4SYS_OBJECT_TYPE_Interaction_Stream                 = 0x03, /* Interaction Stream */
    MP4SYS_OBJECT_TYPE_Extended_BIFS                      = 0x04, /* Extended BIFS */
    /* Used, with StreamType=0x03, for Scene Description streams that use the BIFSConfigEx; its use with
       other StreamTypes is reserved. (Was previously reserved for MUCommandStream but not used for that purpose.) */
    MP4SYS_OBJECT_TYPE_AFX_Stream                         = 0x05, /* AFX Stream */
    /* Used, with StreamType=0x03, for Scene Description streams that use the AFXConfig; its use with other StreamTypes is reserved. */
    MP4SYS_OBJECT_TYPE_Font_Data_Stream                   = 0x06, /* Font Data Stream */
    MP4SYS_OBJECT_TYPE_Synthetised_Texture                = 0x07, /* Synthetised Texture */
    MP4SYS_OBJECT_TYPE_Text_Stream                        = 0x08, /* Text Stream */

    MP4SYS_OBJECT_TYPE_Visual_ISO_14496_2                 = 0x20, /* Visual ISO/IEC 14496-2 */
    MP4SYS_OBJECT_TYPE_Visual_H264_ISO_14496_10           = 0x21, /* Visual ITU-T Recommendation H.264 | ISO/IEC 14496-10 */
    /* The actual object types are within the DecoderSpecificInfo and defined in H.264 | 14496-10. */
    MP4SYS_OBJECT_TYPE_Parameter_Sets_H_264_ISO_14496_10  = 0x22, /* Parameter Sets for ITU-T Recommendation H.264 | ISO/IEC 14496-10 */
    /* The actual object types are within the DecoderSpecificInfo and defined in 14496-2, Annex K. */

    MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3                  = 0x40, /* Audio ISO/IEC 14496-3 (MPEG-4 Audio) */
    //MP4SYS_OBJECT_TYPE_MP4A_AUDIO = 0x40,
    /* The actual object types are defined in 14496-3 and are in the DecoderSpecificInfo as specified in 14496-3 subpart 1 subclause 6.2.1. */

    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_Simple_Profile  = 0x60, /* Visual ISO/IEC 13818-2 Simple Profile (MPEG-2 Video) */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_Main_Profile    = 0x61, /* Visual ISO/IEC 13818-2 Main Profile */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_SNR_Profile     = 0x62, /* Visual ISO/IEC 13818-2 SNR Profile */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_Spatial_Profile = 0x63, /* Visual ISO/IEC 13818-2 Spatial Profile */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_High_Profile    = 0x64, /* Visual ISO/IEC 13818-2 High Profile */
    MP4SYS_OBJECT_TYPE_Visual_ISO_13818_2_422_Profile     = 0x65, /* Visual ISO/IEC 13818-2 422 Profile */
    MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_Main_Profile     = 0x66, /* Audio ISO/IEC 13818-7 Main Profile (MPEG-2 Audio)(AAC) */
    MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_LC_Profile       = 0x67, /* Audio ISO/IEC 13818-7 LowComplexity Profile */
    MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_SSR_Profile      = 0x68, /* Audio ISO/IEC 13818-7 Scaleable Sampling Rate Profile */
    /* For streams kinda 13818-7 the decoder specific information consists of the ADIF header if present
       (or none if not present) and an access unit is a "raw_data_block()" as defined in 13818-7. */
    MP4SYS_OBJECT_TYPE_Audio_ISO_13818_3                  = 0x69, /* Audio ISO/IEC 13818-3 (MPEG-2 BC-Audio)(redefined MPEG-1 Audio in MPEG-2) */
    /* For streams kinda 13818-3 the decoder specific information is empty since all necessary data is in the bitstream frames itself.
       The access units in this case are the "frame()" bitstream element as is defined in 11172-3. */
    MP4SYS_OBJECT_TYPE_Visual_ISO_11172_2                 = 0x6A, /* Visual ISO/IEC 11172-2 (MPEG-1 Video) */
    MP4SYS_OBJECT_TYPE_Audio_ISO_11172_3                  = 0x6B, /* Audio ISO/IEC 11172-3 (MPEG-1 Audio) */
    MP4SYS_OBJECT_TYPE_Visual_ISO_10918_1                 = 0x6C, /* Visual ISO/IEC 10918-1 (JPEG) */
    MP4SYS_OBJECT_TYPE_PNG                                = 0x6D, /* Portable Network Graphics */
    MP4SYS_OBJECT_TYPE_Visual_ISO_15444_1_JPEG2000        = 0x6E, /* Visual ISO/IEC 15444-1 (JPEG 2000) */

    /* FIXME: rename these symbols to be explaining, rather than based on four cc */
    MP4SYS_OBJECT_TYPE_EVRC_AUDIO                         = 0xA0, /* EVRC Voice */
    MP4SYS_OBJECT_TYPE_SSMV_AUDIO                         = 0xA1, /* SMV Voice */
    MP4SYS_OBJECT_TYPE_3GPP2_CMF                          = 0xA2, /* 3GPP2 Compact Multimedia Format (CMF) */
    MP4SYS_OBJECT_TYPE_VC_1_VIDEO                         = 0xA3, /* SMPTE VC-1 Video */
    MP4SYS_OBJECT_TYPE_DRAC_VIDEO                         = 0xA4, /* Dirac Video Coder */
    MP4SYS_OBJECT_TYPE_AC_3_AUDIO                         = 0xA5, /* AC-3 Audio */
    MP4SYS_OBJECT_TYPE_EC_3_AUDIO                         = 0xA6, /* Enhanced AC-3 audio */
    MP4SYS_OBJECT_TYPE_DRA1_AUDIO                         = 0xA7, /* DRA Audio */
    MP4SYS_OBJECT_TYPE_G719_AUDIO                         = 0xA8, /* ITU G.719 Audio */
    MP4SYS_OBJECT_TYPE_DTSC_AUDIO                         = 0xA9, /* DTS Coherent Acoustics audio */
    MP4SYS_OBJECT_TYPE_DTSH_AUDIO                         = 0xAA, /* DTS-HD High Resolution Audio */
    MP4SYS_OBJECT_TYPE_DTSL_AUDIO                         = 0xAB, /* DTS-HD Master Audio */
    MP4SYS_OBJECT_TYPE_DTSE_AUDIO                         = 0xAC, /* DTS Express low bit rate audio, also known as DTS LBR */

    MP4SYS_OBJECT_TYPE_SQCP_AUDIO                         = 0xE1, /* 13K Voice */

    /* FIXME: These OTIs are L-SMASH specific and for internal use. Shall never be coded into output.
              This is a workaround for L-SMASH's structure issue and will be resolved in the future.
              See mp4sys_amrnb_probe(). */
    MP4SYS_OBJECT_TYPE_PRIV_SAMR_AUDIO                    = 0xFFE2, /* AMR-NB */
    MP4SYS_OBJECT_TYPE_PRIV_SAWB_AUDIO                    = 0xFFE3, /* AMR-WB */

    MP4SYS_OBJECT_TYPE_NONE                               = 0xFF, /* no object type specified */
    /* Streams with this value with a StreamType indicating a systems stream (values 1,2,3,6,7,8,9)
       shall be treated as if the ObjectTypeIndication had been set to 0x01. */
} mp4sys_object_type_indication;

/* 8.6.6.2 Semantics Table 7 - streamType Values */
typedef enum {
    MP4SYS_STREAM_TYPE_Forbidden               = 0x00, /* Forbidden */
    MP4SYS_STREAM_TYPE_ObjectDescriptorStream  = 0x01, /* ObjectDescriptorStream (see 8.5) */
    MP4SYS_STREAM_TYPE_ClockReferenceStream    = 0x02, /* ClockReferenceStream (see 10.2.5) */
    MP4SYS_STREAM_TYPE_SceneDescriptionStream  = 0x03, /* SceneDescriptionStream (see 9.2.1) */
    MP4SYS_STREAM_TYPE_VisualStream            = 0x04, /* VisualStream */
    MP4SYS_STREAM_TYPE_AudioStream             = 0x05, /* AudioStream */
    MP4SYS_STREAM_TYPE_MPEG7Stream             = 0x06, /* MPEG7Stream */
    MP4SYS_STREAM_TYPE_IPMPStream              = 0x07, /* IPMPStream (see 8.3.2) */
    MP4SYS_STREAM_TYPE_ObjectContentInfoStream = 0x08, /* ObjectContentInfoStream (see 8.4.2) */
    MP4SYS_STREAM_TYPE_MPEGJStream             = 0x09, /* MPEGJStream */
    MP4SYS_STREAM_TYPE_InteractionStream       = 0x0A, /* Interaction Stream */
    MP4SYS_STREAM_TYPE_IPMPToolStream          = 0x0B, /* IPMPToolStream */
    MP4SYS_STREAM_TYPE_FontDataStream          = 0x0C, /* FontDataStream */
    MP4SYS_STREAM_TYPE_StreamingText           = 0x0D, /* StreamingText */
} mp4sys_stream_type;

/* 8.6.4.2 Semantics Table 3 - ODProfileLevelIndication Values */
typedef enum {
    MP4SYS_OD_PLI_Forbidden     = 0x00, /* Forbidden */
    MP4SYS_OD_PLI_NOT_SPECIFIED = 0xFE, /* no OD profile specified */
    MP4SYS_OD_PLI_NONE_REQUIRED = 0xFF, /* no OD capability required */
} mp4sys_ODProfileLevelIndication;

/* 8.6.4.2 Semantics Table 4 - sceneProfileLevelIndication Values */
typedef enum {
    MP4SYS_SCENE_PLI_RESERVED      = 0x00, /* Reserved for ISO use */
    MP4SYS_SCENE_PLI_Simple2D_L1   = 0x01, /* Simple 2D L1 */
    MP4SYS_SCENE_PLI_Simple2D_L2   = 0x02, /* Simple 2D L2 */
    MP4SYS_SCENE_PLI_Audio_L1      = 0x03, /* Audio L1 */
    MP4SYS_SCENE_PLI_Audio_L2      = 0x04, /* Audio L2 */
    MP4SYS_SCENE_PLI_Audio_L3      = 0x05, /* Audio L3 */
    MP4SYS_SCENE_PLI_Audio_L4      = 0x06, /* Audio L4 */
    MP4SYS_SCENE_PLI_3D_Audio_L1   = 0x07, /* 3D Audio L1 */
    MP4SYS_SCENE_PLI_3D_Audio_L2   = 0x08, /* 3D Audio L2 */
    MP4SYS_SCENE_PLI_3D_Audio_L3   = 0x09, /* 3D Audio L3 */
    MP4SYS_SCENE_PLI_3D_Audio_L4   = 0x0A, /* 3D Audio L4 */
    MP4SYS_SCENE_PLI_Basic2D_L1    = 0x0B, /* Basic 2D L1 */
    MP4SYS_SCENE_PLI_Core2D_L1     = 0x0C, /* Core 2D L1 */
    MP4SYS_SCENE_PLI_Core2D_L2     = 0x0D, /* Core 2D L2 */
    MP4SYS_SCENE_PLI_Advanced2D_L1 = 0x0E, /* Advanced 2D L1 */
    MP4SYS_SCENE_PLI_Advanced2D_L2 = 0x0F, /* Advanced 2D L2 */
    MP4SYS_SCENE_PLI_Advanced2D_L3 = 0x10, /* Advanced 2D L3 */
    MP4SYS_SCENE_PLI_Main2D_L1     = 0x11, /* Main 2D L1 */
    MP4SYS_SCENE_PLI_Main2D_L2     = 0x12, /* Main 2D L2 */
    MP4SYS_SCENE_PLI_Main2D_L3     = 0x13, /* Main 2D L3 */
    MP4SYS_SCENE_PLI_NOT_SPECIFIED = 0xFE, /* no scene profile specified */
    MP4SYS_SCENE_PLI_NONE_REQUIRED = 0xFF, /* no scene capability required */
} mp4sys_sceneProfileLevelIndication;

/* 14496-2 Annex G Profile and level indication and restrictions */
typedef enum {
    MP4SYS_VISUAL_PLI_Reserved                       = 0x00, /* 0b00000000, Reserved */
    MP4SYS_VISUAL_PLI_Simple_PL1                     = 0x01, /* 0b00000001, Simple Profile/Level 1 */
    MP4SYS_VISUAL_PLI_Simple_PL2                     = 0x02, /* 0b00000010, Simple Profile/Level 2 */
    MP4SYS_VISUAL_PLI_Simple_PL3                     = 0x03, /* 0b00000011, Simple Profile/Level 3 */
    MP4SYS_VISUAL_PLI_Simple_Scalable_PL1            = 0x11, /* 0b00010001, Simple Scalable Profile/Level 1 */
    MP4SYS_VISUAL_PLI_Simple_Scalable_PL2            = 0x12, /* 0b00010010, Simple Scalable Profile/Level 2 */
    MP4SYS_VISUAL_PLI_Core_PL1                       = 0x21, /* 0b00100001, Core Profile/Level 1 */
    MP4SYS_VISUAL_PLI_Core_PL2                       = 0x22, /* 0b00100010, Core Profile/Level 2 */
    MP4SYS_VISUAL_PLI_Main_PL2                       = 0x32, /* 0b00110010, Main Profile/Level 2 */
    MP4SYS_VISUAL_PLI_Main_PL3                       = 0x33, /* 0b00110011, Main Profile/Level 3 */
    MP4SYS_VISUAL_PLI_Main_PL4                       = 0x34, /* 0b00110100, Main Profile/Level 4 */
    MP4SYS_VISUAL_PLI_N_bit_PL2                      = 0x42, /* 0b01000010, N-bit Profile/Level 2 */
    MP4SYS_VISUAL_PLI_Scalable_Texture_PL1           = 0x51, /* 0b01010001, Scalable Texture Profile/Level 1 */
    MP4SYS_VISUAL_PLI_Simple_Face_Animation_PL1      = 0x61, /* 0b01100001, Simple Face Animation Profile/Level 1 */
    MP4SYS_VISUAL_PLI_Simple_Face_Animation_PL2      = 0x62, /* 0b01100010, Simple Face Animation Profile/Level 2 */
    MP4SYS_VISUAL_PLI_Simple_FBA_PL1                 = 0x63, /* 0b01100011, Simple FBA Profile/Level 1 */
    MP4SYS_VISUAL_PLI_Simple_FBA_PL2                 = 0x64, /* 0b01100100, Simple FBA Profile/Level 2 */
    MP4SYS_VISUAL_PLI_Basic_Animated_Texture_PL1     = 0x71, /* 0b01110001, Basic Animated Texture Profile/Level 1 */
    MP4SYS_VISUAL_PLI_Basic_Animated_Texture_PL2     = 0x72, /* 0b01110010, Basic Animated Texture Profile/Level 2 */
    MP4SYS_VISUAL_PLI_H264_AVC                       = 0x7F, /* ISO/IEC 14496-10 Advanced Video Codec / H.264, defined in ISO/IEC 14496-1:2001/Amd.7:2004 */
                                                             /* NOTE: Some other implementations seeem to use 0x15(0b00010101) for AVC, but I think that's wrong. */
    MP4SYS_VISUAL_PLI_Hybrid_PL1                     = 0x81, /* 0b10000001, Hybrid Profile/Level 1 */
    MP4SYS_VISUAL_PLI_Hybrid_PL2                     = 0x82, /* 0b10000010, Hybrid Profile/Level 2 */
    MP4SYS_VISUAL_PLI_Advanced_Real_Time_Simple_PL1  = 0x91, /* 0b10010001, Advanced Real Time Simple Profile/Level 1 */
    MP4SYS_VISUAL_PLI_Advanced_Real_Time_Simple_PL2  = 0x92, /* 0b10010010, Advanced Real Time Simple Profile/Level 2 */
    MP4SYS_VISUAL_PLI_Advanced_Real_Time_Simple_PL3  = 0x93, /* 0b10010011, Advanced Real Time Simple Profile/Level 3 */
    MP4SYS_VISUAL_PLI_Advanced_Real_Time_Simple_PL4  = 0x94, /* 0b10010100, Advanced Real Time Simple Profile/Level 4 */
    MP4SYS_VISUAL_PLI_Core_Scalable_PL1              = 0xA1, /* 0b10100001, Core Scalable Profile/Level1 */
    MP4SYS_VISUAL_PLI_Core_Scalable_PL2              = 0xA2, /* 0b10100010, Core Scalable Profile/Level2 */
    MP4SYS_VISUAL_PLI_Core_Scalable_PL3              = 0xA3, /* 0b10100011, Core Scalable Profile/Level3 */
    MP4SYS_VISUAL_PLI_Advanced_Coding_Efficiency_PL1 = 0xB1, /* 0b10110001, Advanced Coding Efficiency Profile/Level 1 */
    MP4SYS_VISUAL_PLI_Advanced_Coding_Efficiency_PL2 = 0xB2, /* 0b10110010, Advanced Coding Efficiency Profile/Level 2 */
    MP4SYS_VISUAL_PLI_Advanced_Coding_Efficiency_PL3 = 0xB3, /* 0b10110011, Advanced Coding Efficiency Profile/Level 3 */
    MP4SYS_VISUAL_PLI_Advanced_Coding_Efficiency_PL4 = 0xB4, /* 0b10110100, Advanced Coding Efficiency Profile/Level 4 */
    MP4SYS_VISUAL_PLI_Advanced_Core_PL1              = 0xC1, /* 0b11000001, Advanced Core Profile/Level 1 */
    MP4SYS_VISUAL_PLI_Advanced_Core_PL2              = 0xC2, /* 0b11000010, Advanced Core Profile/Level 2 */
    MP4SYS_VISUAL_PLI_Advanced_Scalable_Texture_L1   = 0xD1, /* 0b11010001, Advanced Scalable Texture/Level1 */
    MP4SYS_VISUAL_PLI_Advanced_Scalable_Texture_L2   = 0xD2, /* 0b11010010, Advanced Scalable Texture/Level2 */
    MP4SYS_VISUAL_PLI_Advanced_Scalable_Texture_L3   = 0xD3, /* 0b11010011, Advanced Scalable Texture/Level3 */
    MP4SYS_VISUAL_PLI_NOT_SPECIFIED                  = 0xFE, /* no visual profile specified */
    MP4SYS_VISUAL_PLI_NONE_REQUIRED                  = 0xFF, /* no visual capability required */
} mp4sys_visualProfileLevelIndication;

/* 8.6.4.2 Semantics Table 5 - graphicsProfileLevelIndication Values */
typedef enum {
    MP4SYS_GRAPHICS_PLI_RESERVED         = 0x00, /* Reserved for ISO use */
    MP4SYS_GRAPHICS_PLI_Simple2D_L1      = 0x01, /* Simple2D profile L1 */
    MP4SYS_GRAPHICS_PLI_Simple2D_Text_L1 = 0x02, /* Simple 2D + Text profile L1 */
    MP4SYS_GRAPHICS_PLI_Simple2D_Text_L2 = 0x03, /* Simple 2D + Text profile L2 */
    MP4SYS_GRAPHICS_PLI_Core2D_L1        = 0x04, /* Core 2D profile L1 */
    MP4SYS_GRAPHICS_PLI_Core2D_L2        = 0x05, /* Core 2D profile L2 */
    MP4SYS_GRAPHICS_PLI_Advanced2D_L1    = 0x06, /* Advanced 2D profile L1 */
    MP4SYS_GRAPHICS_PLI_Advanced2D_L2    = 0x07, /* Advanced 2D profile L2 */
    MP4SYS_GRAPHICS_PLI_NOT_SPECIFIED    = 0xFE, /* no graphics profile specified */
    MP4SYS_GRAPHICS_PLI_NONE_REQUIRED    = 0xFF, /* no graphics capability required */
} mp4sys_graphicsProfileLevelIndication;

/* Just for mp4sys_setup_ES_Descriptor, to facilitate to make ES_Descriptor */
typedef struct {
    uint16_t ES_ID;                 /* Maybe 0 in stsd(esds), or alternatively, lower 16 bits of the TrackID */
    mp4sys_object_type_indication objectTypeIndication;
    mp4sys_stream_type streamType;
    uint32_t bufferSizeDB;          /* byte unit, NOT bit unit. */
    uint32_t maxBitrate;
    uint32_t avgBitrate;            /* 0 if VBR */
    void* dsi_payload;              /* AudioSpecificConfig or so */
    uint32_t dsi_payload_length ;   /* size of dsi_payload */
} mp4sys_ES_Descriptor_params_t;

#ifndef MP4SYS_INTERNAL

#include "utils.h"

typedef void mp4sys_ES_Descriptor_t;
typedef void mp4sys_ObjectDescriptor_t;

int mp4sys_remove_DecoderSpecificInfo( mp4sys_ES_Descriptor_t* esd );
int mp4sys_remove_DecoderConfigDescriptor( mp4sys_ES_Descriptor_t* esd );
int mp4sys_remove_SLConfigDescriptor( mp4sys_ES_Descriptor_t* esd );
int mp4sys_remove_ES_Descriptor( mp4sys_ES_Descriptor_t* esd );
int mp4sys_remove_ES_ID_Incs( mp4sys_ObjectDescriptor_t* od );
int mp4sys_remove_ObjectDescriptor( mp4sys_ObjectDescriptor_t* od );

int mp4sys_add_DecoderSpecificInfo( mp4sys_ES_Descriptor_t* esd, void* dsi_payload, uint32_t dsi_payload_length );
/*
    bufferSizeDB is byte unit, NOT bit unit.
    avgBitrate is 0 if VBR
*/
int mp4sys_add_DecoderConfigDescriptor(
    mp4sys_ES_Descriptor_t* esd,
    mp4sys_object_type_indication objectTypeIndication,
    mp4sys_stream_type streamType,
    uint32_t bufferSizeDB,
    uint32_t maxBitrate,
    uint32_t avgBitrate
);
int mp4sys_add_SLConfigDescriptor( mp4sys_ES_Descriptor_t* esd );
int mp4sys_add_ES_ID_Inc( mp4sys_ObjectDescriptor_t* od, uint32_t Track_ID);

/* ES_ID might be usually 0 or lower 16 bits of the TrackID
   14496-14 says, "set to 0 as stored; when built into a stream, the lower 16 bits of the TrackID are used."
   I'm not sure about actual meaning of "stored" and "built into a stream", but maybe 0 will do in stsd(esds). */
mp4sys_ES_Descriptor_t* mp4sys_create_ES_Descriptor( uint16_t ES_ID );
mp4sys_ObjectDescriptor_t* mp4sys_create_ObjectDescriptor( uint16_t ObjectDescriptorID );
int mp4sys_to_InitialObjectDescriptor(
    mp4sys_ObjectDescriptor_t* od,
    uint8_t include_inline_pli,
    mp4sys_ODProfileLevelIndication od_pli,
    mp4sys_sceneProfileLevelIndication scene_pli,
    mp4a_audioProfileLevelIndication audio_pli,
    mp4sys_visualProfileLevelIndication visual_pli,
    mp4sys_graphicsProfileLevelIndication graph_pli
);

uint32_t mp4sys_update_ES_Descriptor_size( mp4sys_ES_Descriptor_t* esd );
uint32_t mp4sys_update_ObjectDescriptor_size( mp4sys_ObjectDescriptor_t* od );

int mp4sys_write_ES_Descriptor( lsmash_bs_t *bs, mp4sys_ES_Descriptor_t* esd );
int mp4sys_write_ObjectDescriptor( lsmash_bs_t *bs, mp4sys_ObjectDescriptor_t* od );

int mp4sys_update_DecoderConfigDescriptor(
    mp4sys_ES_Descriptor_t* esd,
    uint32_t bufferSizeDB,
    uint32_t maxBitrate,
    uint32_t avgBitrate
);

/* to facilitate to make ES_Descriptor */
mp4sys_ES_Descriptor_t* mp4sys_setup_ES_Descriptor( mp4sys_ES_Descriptor_params_t* params );

#endif /* #ifndef MP4SYS_INTERNAL */

#endif /* #ifndef MP4SYS_H */
