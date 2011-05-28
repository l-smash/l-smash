/*****************************************************************************
 * mp4sys.c:
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

#include "internal.h" /* must be placed first */

#include "utils.h"

#include <stdlib.h>
#include <string.h>

#include "mp4a.h"
#define MP4SYS_INTERNAL
#include "mp4sys.h"

/***************************************************************************
    MPEG-4 Systems
***************************************************************************/

#define ALWAYS_28BITS_LENGTH_CODING 1 // for some weird (but originator's) devices

/* List of Class Tags for Descriptors */
typedef enum {
    MP4SYS_DESCRIPTOR_TAG_Forbidden                           = 0x00, /* Forbidden */
    MP4SYS_DESCRIPTOR_TAG_ObjectDescrTag                      = 0x01, /* ObjectDescrTag */
    MP4SYS_DESCRIPTOR_TAG_InitialObjectDescrTag               = 0x02, /* InitialObjectDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ES_DescrTag                         = 0x03, /* ES_DescrTag */
    MP4SYS_DESCRIPTOR_TAG_DecoderConfigDescrTag               = 0x04, /* DecoderConfigDescrTag */
    MP4SYS_DESCRIPTOR_TAG_DecSpecificInfoTag                  = 0x05, /* DecSpecificInfoTag */
    MP4SYS_DESCRIPTOR_TAG_SLConfigDescrTag                    = 0x06, /* SLConfigDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ContentIdentDescrTag                = 0x07, /* ContentIdentDescrTag */
    MP4SYS_DESCRIPTOR_TAG_SupplContentIdentDescrTag           = 0x08, /* SupplContentIdentDescrTag */
    MP4SYS_DESCRIPTOR_TAG_IPI_DescrPointerTag                 = 0x09, /* IPI_DescrPointerTag */
    MP4SYS_DESCRIPTOR_TAG_IPMP_DescrPointerTag                = 0x0A, /* IPMP_DescrPointerTag */
    MP4SYS_DESCRIPTOR_TAG_IPMP_DescrTag                       = 0x0B, /* IPMP_DescrTag */
    MP4SYS_DESCRIPTOR_TAG_QoS_DescrTag                        = 0x0C, /* QoS_DescrTag */
    MP4SYS_DESCRIPTOR_TAG_RegistrationDescrTag                = 0x0D, /* RegistrationDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ES_ID_IncTag                        = 0x0E, /* ES_ID_IncTag */
    MP4SYS_DESCRIPTOR_TAG_ES_ID_RefTag                        = 0x0F, /* ES_ID_RefTag */
    MP4SYS_DESCRIPTOR_TAG_MP4_IOD_Tag                         = 0x10, /* MP4_IOD_Tag, InitialObjectDescriptor for MP4 */
    MP4SYS_DESCRIPTOR_TAG_MP4_OD_Tag                          = 0x11, /* MP4_OD_Tag, ObjectDescriptor for MP4 */
    MP4SYS_DESCRIPTOR_TAG_IPI_DescrPointerRefTag              = 0x12, /* IPI_DescrPointerRefTag */
    MP4SYS_DESCRIPTOR_TAG_ExtendedProfileLevelDescrTag        = 0x13, /* ExtendedProfileLevelDescrTag */
    MP4SYS_DESCRIPTOR_TAG_profileLevelIndicationIndexDescrTag = 0x14, /* profileLevelIndicationIndexDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ContentClassificationDescrTag       = 0x40, /* ContentClassificationDescrTag */
    MP4SYS_DESCRIPTOR_TAG_KeyWordDescrTag                     = 0x41, /* KeyWordDescrTag */
    MP4SYS_DESCRIPTOR_TAG_RatingDescrTag                      = 0x42, /* RatingDescrTag */
    MP4SYS_DESCRIPTOR_TAG_LanguageDescrTag                    = 0x43, /* LanguageDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ShortTextualDescrTag                = 0x44, /* ShortTextualDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ExpandedTextualDescrTag             = 0x45, /* ExpandedTextualDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ContentCreatorNameDescrTag          = 0x46, /* ContentCreatorNameDescrTag */
    MP4SYS_DESCRIPTOR_TAG_ContentCreationDateDescrTag         = 0x47, /* ContentCreationDateDescrTag */
    MP4SYS_DESCRIPTOR_TAG_OCICreatorNameDescrTag              = 0x48, /* OCICreatorNameDescrTag */
    MP4SYS_DESCRIPTOR_TAG_OCICreationDateDescrTag             = 0x49, /* OCICreationDateDescrTag */
    MP4SYS_DESCRIPTOR_TAG_SmpteCameraPositionDescrTag         = 0x4A, /* SmpteCameraPositionDescrTag */
    MP4SYS_DESCRIPTOR_TAG_Forbidden1                          = 0xFF, /* Forbidden */
} mp4sys_descriptor_tag;
//    MP4SYS_DESCRIPTOR_TAG_ES_DescrRemoveRefTag                = 0x07, /* FIXME: (command tag), see 14496-14 Object Descriptors */

typedef struct {
    uint32_t size; // 2^28 at most
    mp4sys_descriptor_tag tag;
} mp4sys_descriptor_head_t;

/* DecoderSpecificInfo */
/* contents varies depends on ObjectTypeIndication and StreamType. */
typedef struct {
    mp4sys_descriptor_head_t header;
    uint8_t* data;
} mp4sys_DecoderSpecificInfo_t;

/* DecoderConfigDescriptor */
typedef struct {
    mp4sys_descriptor_head_t header;
    lsmash_mp4sys_object_type_indication objectTypeIndication;
    lsmash_mp4sys_stream_type streamType;
    // uint8_t upStream; /* bit(1), always 0 in this muxer, used for interactive contents. */
    // uint8_t reserved=1; /* const bit(1) */
    uint32_t bufferSizeDB; /* maybe CPB size in bytes, NOT bits. */
    uint32_t maxBitrate;
    uint32_t avgBitrate; /* 0 if VBR */
    mp4sys_DecoderSpecificInfo_t* decSpecificInfo;  /* can be NULL. */
    /* 14496-1 seems to say if we are in IOD(InitialObjectDescriptor), we might use this.
       See ExtensionProfileLevelDescr, The Initial Object Descriptor.
       But I don't think this is mandatory despite 14496-1, because 14496-14 says, in OD or IOD,
       we have to use ES_ID_Inc instead of ES_Descriptor, which does not have DecoderConfigDescriptor. */
    // profileLevelIndicationIndexDescriptor profileLevelIndicationIndexDescr [0..255];
} mp4sys_DecoderConfigDescriptor_t;

/* SLConfigDescriptor */
typedef struct {
    mp4sys_descriptor_head_t header;
    uint8_t predefined; /* MP4 file that does not use URL_Flag shall have constant value 0x02 */
    /* custom values may be placed here if predefined == 0, but we need not care in MP4 files
       without external references */
} mp4sys_SLConfigDescriptor_t;

/* ES_Descriptor */
typedef struct
{
    mp4sys_descriptor_head_t header;
    uint16_t ES_ID;
    // uint8_t ESD_flags; /* this 8bit value is always 0 in this muxer, see below for detail */
    /*
    typedef struct TAG_ESD_flags{
        unsigned streamDependenceFlag : 1; // no stream depencies between streams in this muxer, ES_ID of another elementary stream
        unsigned URL_Flag             : 1; // no external URL referencing stream in MP4
        unsigned OCRstreamFlag        : 1; // no Object Clock Reference stream in this muxer (shall be false in MP4, useful if we're importing from MPEG-2?)
        unsigned streamPriority       : 5; // no priority among streams in this muxer, higher is important
    } ESD_flags;
    // These are omitted according to ESD_flags == 0
    //if(streamDependenceFlag)
        uint16_t dependsOn_ES_ID; // bit(16)
    //if(URL_Flag)
        uint8_t URLlength; // bit(8)
        char URLstring[256]; // bit(8)[], UTF-8 encoded URL
    //if(OCR_ES_Flag)
        uint16_t OCR_ES_Id;
    */
    mp4sys_DecoderConfigDescriptor_t* decConfigDescr; /* cannot be NULL. */
    mp4sys_SLConfigDescriptor_t* slConfigDescr;
    /* descriptors below are not mandatory, I think Language Descriptor may somewhat useful */
    /*
    IPI_DescrPointer ipiPtr[0 .. 1];               // used to indicate using other ES's IP_IdentificationDataSet
    IP_IdentificationDataSet ipIDS[0 .. 255];      // abstract class, actually ContentIdentificationDescriptor(for commercial contents management),
                                                   // or SupplementaryContentIdentificationDescriptor(for embedding titles)
    IPMP_DescriptorPointer ipmpDescrPtr[0 .. 255]; // used to intellectual property / protection management
    LanguageDescriptor langDescr[0 .. 255];        // used to identify the language of the audio/speech or text object
    QoS_Descriptor qosDescr[0 .. 1];               // used to achieve QoS
    RegistrationDescriptor regDescr[0 .. 1];       // used to carry elementary streams with data whose format is not recognized by ISO/IEC 14496-1
    ExtensionDescriptor extDescr[0 .. 255];        // abstract class, actually defined no subclass, maybe useless
    */
} mp4sys_ES_Descriptor_t;

/* 14496-14 Object Descriptors (ES_ID_Inc) */
typedef struct {
    mp4sys_descriptor_head_t header;
    uint32_t Track_ID;
} mp4sys_ES_ID_Inc_t;

/* 14496-1 ObjectDescriptor / InitialObjectDescriptor */
typedef struct {
    mp4sys_descriptor_head_t header;
    uint16_t ObjectDescriptorID;
    // uint8_t URL_Flag; /* bit(1) */
    uint8_t includeInlineProfileLevelFlag; /* bit(1) */
    //const uint8_t reserved=0x0F(0b1111) or 0x1F(0b1.1111); /* bit(4 or 5), width is 4 for IOD, 5 for OD */
    /* if (URL_Flag) {
        uint8_t URLlength; // bit(8)
        char URLstring[256]; // bit(8)[]
    }*/
    /* else { */
        mp4sys_ODProfileLevelIndication ODProfileLevelIndication;
        mp4sys_sceneProfileLevelIndication sceneProfileLevelIndication;
        mp4a_audioProfileLevelIndication audioProfileLevelIndication;
        mp4sys_visualProfileLevelIndication visualProfileLevelIndication;
        mp4sys_graphicsProfileLevelIndication graphicsProfileLevelIndication;
        lsmash_entry_list_t* esDescr; /* List of ES_ID_Inc, not ES_Descriptor defined in 14496-1. 14496-14 overrides. */
        // OCI_Descriptor ociDescr[0 .. 255];
        // IPMP_DescriptorPointer ipmpDescrPtr[0 .. 255];
    /* } */
    // ExtensionDescriptor extDescr[0 .. 255];
} mp4sys_ObjectDescriptor_t;

int mp4sys_remove_DecoderSpecificInfo( mp4sys_ES_Descriptor_t* esd )
{
    if( !esd || !esd->decConfigDescr )
        return -1;
    if( !esd->decConfigDescr->decSpecificInfo )
        return 0;
    if( esd->decConfigDescr->decSpecificInfo->data )
        free( esd->decConfigDescr->decSpecificInfo->data );
    free( esd->decConfigDescr->decSpecificInfo );
    esd->decConfigDescr->decSpecificInfo = NULL;
    return 0;
}

int mp4sys_remove_DecoderConfigDescriptor( mp4sys_ES_Descriptor_t* esd )
{
    if( !esd )
        return -1;
    if( !esd->decConfigDescr )
        return 0;
    mp4sys_remove_DecoderSpecificInfo( esd );
    free( esd->decConfigDescr );
    esd->decConfigDescr = NULL;
    return 0;
}

int mp4sys_remove_SLConfigDescriptor( mp4sys_ES_Descriptor_t* esd )
{
    if( !esd )
        return -1;
    if( !esd->slConfigDescr )
        return 0;
    free( esd->slConfigDescr );
    esd->slConfigDescr = NULL;
    return 0;
}

int mp4sys_remove_ES_Descriptor( mp4sys_ES_Descriptor_t* esd )
{
    if( !esd )
        return 0;
    mp4sys_remove_DecoderConfigDescriptor( esd );
    mp4sys_remove_SLConfigDescriptor( esd );
    free( esd );
    return 0;
}

int mp4sys_remove_ES_ID_Incs( mp4sys_ObjectDescriptor_t* od )
{
    if( !od )
        return -1;
    if( od->esDescr )
    {
        lsmash_remove_list( od->esDescr, NULL );
        od->esDescr = NULL;
    }
    return 0;
}

int mp4sys_remove_ObjectDescriptor( mp4sys_ObjectDescriptor_t* od )
{
    if( !od )
        return 0;
    mp4sys_remove_ES_ID_Incs( od );
    free( od );
    return 0;
}

int mp4sys_add_DecoderSpecificInfo( mp4sys_ES_Descriptor_t* esd, void* dsi_payload, uint32_t dsi_payload_length )
{
    if( !esd || !esd->decConfigDescr || dsi_payload == NULL || dsi_payload_length == 0 )
        return -1;
    mp4sys_DecoderSpecificInfo_t* dsi = (mp4sys_DecoderSpecificInfo_t*)malloc( sizeof(mp4sys_DecoderSpecificInfo_t) );
    if( !dsi )
        return -1;
    memset( dsi, 0, sizeof(mp4sys_DecoderSpecificInfo_t) );
    dsi->header.tag = MP4SYS_DESCRIPTOR_TAG_DecSpecificInfoTag;
    dsi->data = malloc( dsi_payload_length );
    if( !dsi->data )
    {
        free( dsi );
        return -1;
    }
    memcpy( dsi->data, dsi_payload, dsi_payload_length );
    dsi->header.size = dsi_payload_length;
    debug_if( mp4sys_remove_DecoderSpecificInfo( esd ) )
    {
        free( dsi->data );
        free( dsi );
        return -1;
    }
    esd->decConfigDescr->decSpecificInfo = dsi;
    return 0;
}

/*
    bufferSizeDB is byte unit, NOT bit unit.
    avgBitrate is 0 if VBR
*/
int mp4sys_add_DecoderConfigDescriptor(
    mp4sys_ES_Descriptor_t* esd,
    lsmash_mp4sys_object_type_indication objectTypeIndication,
    lsmash_mp4sys_stream_type streamType,
    uint32_t bufferSizeDB,
    uint32_t maxBitrate,
    uint32_t avgBitrate
){
    if( !esd )
        return -1;
    mp4sys_DecoderConfigDescriptor_t* dcd = (mp4sys_DecoderConfigDescriptor_t*)malloc( sizeof(mp4sys_DecoderConfigDescriptor_t) );
    if( !dcd )
        return -1;
    memset( dcd, 0, sizeof(mp4sys_DecoderConfigDescriptor_t) );
    dcd->header.tag = MP4SYS_DESCRIPTOR_TAG_DecoderConfigDescrTag;
    dcd->objectTypeIndication = objectTypeIndication;
    dcd->streamType = streamType;
    dcd->bufferSizeDB = bufferSizeDB;
    dcd->maxBitrate = maxBitrate;
    dcd->avgBitrate = avgBitrate;
    debug_if( mp4sys_remove_DecoderConfigDescriptor( esd ) )
    {
        free( dcd );
        return -1;
    }
    esd->decConfigDescr = dcd;
    return 0;
}

/*
    bufferSizeDB is byte unit, NOT bit unit.
    avgBitrate is 0 if VBR
*/
int mp4sys_update_DecoderConfigDescriptor( mp4sys_ES_Descriptor_t* esd, uint32_t bufferSizeDB, uint32_t maxBitrate, uint32_t avgBitrate )
{
    if( !esd || !esd->decConfigDescr )
        return -1;
    mp4sys_DecoderConfigDescriptor_t* dcd = esd->decConfigDescr;
    dcd->bufferSizeDB = bufferSizeDB;
    dcd->maxBitrate = maxBitrate;
    dcd->avgBitrate = avgBitrate;
    return 0;
}

int mp4sys_add_SLConfigDescriptor( mp4sys_ES_Descriptor_t* esd )
{
    if( !esd )
        return -1;
    mp4sys_SLConfigDescriptor_t* slcd = (mp4sys_SLConfigDescriptor_t*)malloc( sizeof(mp4sys_SLConfigDescriptor_t) );
    if( !slcd )
        return -1;
    memset( slcd, 0, sizeof(mp4sys_SLConfigDescriptor_t) );
    slcd->header.tag = MP4SYS_DESCRIPTOR_TAG_SLConfigDescrTag;
    slcd->predefined = 0x02; /* MP4 file which does not use URL_Flag shall have constant value 0x02 */
    debug_if( mp4sys_remove_SLConfigDescriptor( esd ) )
    {
        free( slcd );
        return -1;
    }
    esd->slConfigDescr = slcd;
    return 0;
}

/* ES_ID might be usually 0 or lower 16 bits of the TrackID
   14496-14 says, "set to 0 as stored; when built into a stream, the lower 16 bits of the TrackID are used."
   I'm not sure about actual meaning of "stored" and "built into a stream", but maybe 0 will do in stsd(esds). */
mp4sys_ES_Descriptor_t* mp4sys_create_ES_Descriptor( uint16_t ES_ID )
{
    mp4sys_ES_Descriptor_t* esd = (mp4sys_ES_Descriptor_t*)malloc( sizeof(mp4sys_ES_Descriptor_t) );
    if( !esd )
        return NULL;
    memset( esd, 0, sizeof(mp4sys_ES_Descriptor_t) );
    esd->header.tag = MP4SYS_DESCRIPTOR_TAG_ES_DescrTag;
    esd->ES_ID = ES_ID;
    return esd;
}

/* NOTE: This is only for MP4_IOD and MP4_OD, not for Iso Base Media's ObjectDescriptor and InitialObjectDescriptor */
int mp4sys_add_ES_ID_Inc( mp4sys_ObjectDescriptor_t* od, uint32_t Track_ID )
{
    if( !od )
        return -1;
    if( !od->esDescr && ( od->esDescr = lsmash_create_entry_list() ) == NULL )
        return -1;
    mp4sys_ES_ID_Inc_t* es_id_inc = (mp4sys_ES_ID_Inc_t*)malloc( sizeof(mp4sys_ES_ID_Inc_t) );
    if( !es_id_inc )
        return -1;
    memset( es_id_inc, 0, sizeof(mp4sys_ES_ID_Inc_t) );
    es_id_inc->header.tag = MP4SYS_DESCRIPTOR_TAG_ES_ID_IncTag;
    es_id_inc->Track_ID = Track_ID;
    if( lsmash_add_entry( od->esDescr, es_id_inc ) )
    {
        free( es_id_inc );
        return -1;
    }
    return 0;
}

/* NOTE: This is only for MP4_OD, not for Iso Base Media's ObjectDescriptor */
mp4sys_ObjectDescriptor_t* mp4sys_create_ObjectDescriptor( uint16_t ObjectDescriptorID )
{
    mp4sys_ObjectDescriptor_t* od = (mp4sys_ObjectDescriptor_t*)malloc( sizeof(mp4sys_ObjectDescriptor_t) );
    if( !od )
        return NULL;
    memset( od, 0, sizeof(mp4sys_ObjectDescriptor_t) );
    od->header.tag = MP4SYS_DESCRIPTOR_TAG_MP4_OD_Tag;
    od->ObjectDescriptorID = ObjectDescriptorID;
    od->includeInlineProfileLevelFlag = 1; /* 1 as part of reserved flag. */
    od->ODProfileLevelIndication = MP4SYS_OD_PLI_NONE_REQUIRED;
    od->sceneProfileLevelIndication = MP4SYS_SCENE_PLI_NONE_REQUIRED;
    od->audioProfileLevelIndication = MP4A_AUDIO_PLI_NONE_REQUIRED;
    od->visualProfileLevelIndication = MP4SYS_VISUAL_PLI_NONE_REQUIRED;
    od->graphicsProfileLevelIndication = MP4SYS_GRAPHICS_PLI_NONE_REQUIRED;
    return od;
}

/* NOTE: This is only for MP4_IOD, not for Iso Base Media's InitialObjectDescriptor */
int mp4sys_to_InitialObjectDescriptor(
    mp4sys_ObjectDescriptor_t* od,
    uint8_t include_inline_pli,
    mp4sys_ODProfileLevelIndication od_pli,
    mp4sys_sceneProfileLevelIndication scene_pli,
    mp4a_audioProfileLevelIndication audio_pli,
    mp4sys_visualProfileLevelIndication visual_pli,
    mp4sys_graphicsProfileLevelIndication graph_pli
){
    if( !od )
        return -1;
    od->header.tag = MP4SYS_DESCRIPTOR_TAG_MP4_IOD_Tag;
    od->includeInlineProfileLevelFlag = include_inline_pli;
    od->ODProfileLevelIndication = od_pli;
    od->sceneProfileLevelIndication = scene_pli;
    od->audioProfileLevelIndication = audio_pli;
    od->visualProfileLevelIndication = visual_pli;
    od->graphicsProfileLevelIndication = graph_pli;
    return 0;
}

/* returns total size of descriptor, including header, 2 at least */
static inline uint32_t mp4sys_get_descriptor_size( uint32_t payload_size_in_byte )
{
#if ALWAYS_28BITS_LENGTH_CODING
    return payload_size_in_byte + 4 + 1; /* +4 means 28bits length coding, +1 means tag's space */
#else
    /* descriptor length will be split into 7bits
       see 14496-1 Expandable classes and Length encoding of descriptors and commands */
    uint32_t i;
    for( i = 1; payload_size_in_byte >> ( 7 * i ) ; i++);
    return payload_size_in_byte + i + 1; /* +1 means tag's space */
#endif
}

static uint32_t mp4sys_update_DecoderSpecificInfo_size( mp4sys_ES_Descriptor_t* esd )
{
    debug_if( !esd || !esd->decConfigDescr )
        return 0;
    if( !esd->decConfigDescr->decSpecificInfo )
        return 0;
    /* no need to update, header.size is already set */
    return mp4sys_get_descriptor_size( esd->decConfigDescr->decSpecificInfo->header.size );
}

static uint32_t mp4sys_update_DecoderConfigDescriptor_size( mp4sys_ES_Descriptor_t* esd )
{
    debug_if( !esd )
        return 0;
    if( !esd->decConfigDescr )
        return 0;
    uint32_t size = 13;
    size += mp4sys_update_DecoderSpecificInfo_size( esd );
    esd->decConfigDescr->header.size = size;
    return mp4sys_get_descriptor_size( size );
}

static uint32_t mp4sys_update_SLConfigDescriptor_size( mp4sys_ES_Descriptor_t* esd )
{
    debug_if( !esd )
        return 0;
    if( !esd->slConfigDescr )
        return 0;
    esd->slConfigDescr->header.size = 1;
    return mp4sys_get_descriptor_size( esd->slConfigDescr->header.size );
}

uint32_t mp4sys_update_ES_Descriptor_size( mp4sys_ES_Descriptor_t* esd )
{
    if( !esd )
        return 0;
    uint32_t size = 3;
    size += mp4sys_update_DecoderConfigDescriptor_size( esd );
    size += mp4sys_update_SLConfigDescriptor_size( esd );
    esd->header.size = size;
    return mp4sys_get_descriptor_size( size );
}

static uint32_t mp4sys_update_ES_ID_Inc_size( mp4sys_ES_ID_Inc_t* es_id_inc )
{
    debug_if( !es_id_inc )
        return 0;
    es_id_inc->header.size = 4;
    return mp4sys_get_descriptor_size( es_id_inc->header.size );
}

/* This function works as aggregate of ES_ID_Incs, so this function itself updates no size information */
static uint32_t mp4sys_update_ES_ID_Incs_size( mp4sys_ObjectDescriptor_t* od )
{
    debug_if( !od )
        return 0;
    if( !od->esDescr )
        return 0;
    uint32_t size = 0;
    for( lsmash_entry_t *entry = od->esDescr->head; entry; entry = entry->next )
        size += mp4sys_update_ES_ID_Inc_size( (mp4sys_ES_ID_Inc_t*)entry->data );
    return size;
}

uint32_t mp4sys_update_ObjectDescriptor_size( mp4sys_ObjectDescriptor_t* od )
{
    if( !od )
        return 0;
    uint32_t size = od->header.tag == MP4SYS_DESCRIPTOR_TAG_MP4_IOD_Tag ? 7 : 2;
    size += mp4sys_update_ES_ID_Incs_size( od );
    od->header.size = size;
    return mp4sys_get_descriptor_size( size );
}

static int mp4sys_put_descriptor_header( lsmash_bs_t *bs, mp4sys_descriptor_head_t* header )
{
    debug_if( !bs || !header )
        return -1;
    lsmash_bs_put_byte( bs, header->tag );
    /* descriptor length will be splitted into 7bits
       see 14496-1 Expandable classes and Length encoding of descriptors and commands */
#if ALWAYS_28BITS_LENGTH_CODING
    lsmash_bs_put_byte( bs, ( header->size >> 21 ) | 0x80 );
    lsmash_bs_put_byte( bs, ( header->size >> 14 ) | 0x80 );
    lsmash_bs_put_byte( bs, ( header->size >>  7 ) | 0x80 );
#else
    for( uint32_t i = mp4sys_get_descriptor_size( header->size ) - header->size - 2; i; i-- ){
        lsmash_bs_put_byte( bs, ( header->size >> ( 7 * i ) ) | 0x80 );
    }
#endif
    lsmash_bs_put_byte( bs, header->size & 0x7F );
    return 0;
}

static int mp4sys_write_DecoderSpecificInfo( lsmash_bs_t *bs, mp4sys_DecoderSpecificInfo_t* dsi )
{
    debug_if( !bs )
        return -1;
    if( !dsi )
        return 0; /* can be NULL */
    debug_if( mp4sys_put_descriptor_header( bs, &dsi->header ) )
        return -1;
    if( dsi->data && dsi->header.size != 0 )
        lsmash_bs_put_bytes( bs, dsi->data, dsi->header.size );
    return lsmash_bs_write_data( bs );
}

static int mp4sys_write_DecoderConfigDescriptor( lsmash_bs_t *bs, mp4sys_DecoderConfigDescriptor_t* dcd )
{
    debug_if( !bs )
        return -1;
    if( !dcd )
        return -1; /* cannot be NULL */
    debug_if( mp4sys_put_descriptor_header( bs, &dcd->header ) )
        return -1;
    lsmash_bs_put_byte( bs, dcd->objectTypeIndication );
    lsmash_bs_put_byte( bs, (dcd->streamType << 2) | 0x01 ); /* contains upStream<1> = 0, reserved<1> = 1 */
    lsmash_bs_put_be24( bs, dcd->bufferSizeDB );
    lsmash_bs_put_be32( bs, dcd->maxBitrate );
    lsmash_bs_put_be32( bs, dcd->avgBitrate );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    return mp4sys_write_DecoderSpecificInfo( bs, dcd->decSpecificInfo );
    /* here, profileLevelIndicationIndexDescriptor is omitted */
}

static int mp4sys_write_SLConfigDescriptor( lsmash_bs_t *bs, mp4sys_SLConfigDescriptor_t* slcd )
{
    debug_if( !bs )
        return -1;
    if( !slcd )
        return 0;
    debug_if( mp4sys_put_descriptor_header( bs, &slcd->header ) )
        return -1;
    lsmash_bs_put_byte( bs, slcd->predefined );
    return lsmash_bs_write_data( bs );
}

int mp4sys_write_ES_Descriptor( lsmash_bs_t *bs, mp4sys_ES_Descriptor_t* esd )
{
    if( !bs || !esd )
        return -1;
    debug_if( mp4sys_put_descriptor_header( bs, &esd->header ) )
        return -1;
    lsmash_bs_put_be16( bs, esd->ES_ID );
    lsmash_bs_put_byte( bs, 0 ); /* streamDependenceFlag<1>, URL_Flag<1>, OCRstreamFlag<1>, streamPriority<5> */
    /* here, some syntax elements are omitted due to previous flags (all 0) */
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( mp4sys_write_DecoderConfigDescriptor( bs, esd->decConfigDescr ) )
        return -1;
    return mp4sys_write_SLConfigDescriptor( bs, esd->slConfigDescr );
}

static int mp4sys_put_ES_ID_Inc( lsmash_bs_t *bs, mp4sys_ES_ID_Inc_t* es_id_inc )
{
    debug_if( !es_id_inc )
        return 0;
    debug_if( mp4sys_put_descriptor_header( bs, &es_id_inc->header ) )
        return -1;
    lsmash_bs_put_be32( bs, es_id_inc->Track_ID );
    return 0;
}

/* This function works as aggregate of ES_ID_Incs */
static int mp4sys_write_ES_ID_Incs( lsmash_bs_t *bs, mp4sys_ObjectDescriptor_t* od )
{
    debug_if( !od )
        return 0;
    if( !od->esDescr )
        return 0; /* This may violate the spec, but some muxer do this */
    for( lsmash_entry_t *entry = od->esDescr->head; entry; entry = entry->next )
        mp4sys_put_ES_ID_Inc( bs, (mp4sys_ES_ID_Inc_t*)entry->data );
    return lsmash_bs_write_data( bs );
}

int mp4sys_write_ObjectDescriptor( lsmash_bs_t *bs, mp4sys_ObjectDescriptor_t* od )
{
    if( !bs || !od )
        return -1;
    debug_if( mp4sys_put_descriptor_header( bs, &od->header ) )
        return -1;
    uint16_t temp = (od->ObjectDescriptorID << 6);
    // temp |= (0x0 << 5); /* URL_Flag */
    temp |= (od->includeInlineProfileLevelFlag << 4); /* if MP4_OD, includeInlineProfileLevelFlag is 0x1. */
    temp |= 0xF;  /* reserved */
    lsmash_bs_put_be16( bs, temp );
    /* here, since we don't support URL_Flag, we put ProfileLevelIndications */
    if( od->header.tag == MP4SYS_DESCRIPTOR_TAG_MP4_IOD_Tag )
    {
        lsmash_bs_put_byte( bs, od->ODProfileLevelIndication );
        lsmash_bs_put_byte( bs, od->sceneProfileLevelIndication );
        lsmash_bs_put_byte( bs, od->audioProfileLevelIndication );
        lsmash_bs_put_byte( bs, od->visualProfileLevelIndication );
        lsmash_bs_put_byte( bs, od->graphicsProfileLevelIndication );
    }
    if( lsmash_bs_write_data( bs ) )
        return -1;
    return mp4sys_write_ES_ID_Incs( bs, od );
}

/**** following functions are for facilitation purpose ****/

mp4sys_ES_Descriptor_t* mp4sys_setup_ES_Descriptor( mp4sys_ES_Descriptor_params_t* params )
{
    if( !params )
        return NULL;
    mp4sys_ES_Descriptor_t* esd = mp4sys_create_ES_Descriptor( params->ES_ID );
    if( !esd )
        return NULL;
    if( mp4sys_add_SLConfigDescriptor( esd )
        || mp4sys_add_DecoderConfigDescriptor( esd, params->objectTypeIndication, params->streamType, params->bufferSizeDB, params->maxBitrate, params->avgBitrate )
        || ( params->dsi_payload && params->dsi_payload_length != 0 && mp4sys_add_DecoderSpecificInfo( esd, params->dsi_payload, params->dsi_payload_length ) )
    ){
        mp4sys_remove_ES_Descriptor( esd );
        return NULL;
    }
    return esd;
}
