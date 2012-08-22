/*****************************************************************************
 * mp4sys.c:
 *****************************************************************************
 * Copyright (C) 2010-2012 L-SMASH project
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
#include <inttypes.h>

#include "box.h"
#include "description.h"
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
    uint8_t upStream; /* bit(1), always 0 in this muxer, used for interactive contents. */
    uint8_t reserved; /* const bit(1), always 1. */
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
    uint8_t predefined;     /* default the values from a set of predefined parameter sets as detailed below.
                             *  0x00        : Custum
                             *  0x01        : null SL packet header
                             *  0x02        : Reserved for use in MP4 files
                             *  0x03 - 0xFF : Reserved for ISO use
                             * MP4 file that does not use URL_Flag shall have constant value 0x02. */
    /* Custom values
     * The following fields are placed if predefined == 0x00. */
    unsigned useAccessUnitStartFlag       : 1;
    unsigned useAccessUnitEndFlag         : 1;
    unsigned useRandomAccessPointFlag     : 1;
    unsigned hasRandomAccessUnitsOnlyFlag : 1;
    unsigned usePaddingFlag               : 1;
    unsigned useTimeStampsFlag            : 1;
    unsigned useIdleFlag                  : 1;
    unsigned durationFlag                 : 1;
    uint32_t timeStampResolution;
    uint32_t OCRResolution;
    uint8_t  timeStampLength;
    uint8_t  OCRLength;
    uint8_t  AU_Length;
    uint8_t  instantBitrateLength;
    unsigned degradationPriorityLength    : 4;
    unsigned AU_seqNumLength              : 5;
    unsigned packetSeqNumLength           : 5;
    unsigned reserved                     : 2;
    /* The following fields are placed if durationFlag is true. */
    uint32_t timeScale;
    uint16_t accessUnitDuration;
    uint16_t compositionUnitDuration;
    /* The following fields are placed if useTimeStampsFlag is false. */
    uint64_t startDecodingTimeStamp;
    uint64_t startCompositionTimeStamp;
} mp4sys_SLConfigDescriptor_t;

/* ES_Descriptor */
typedef struct mp4sys_ES_Descriptor_t
{
    mp4sys_descriptor_head_t header;
    uint16_t ES_ID;
    unsigned streamDependenceFlag : 1;  /* no stream depencies between streams in this muxer, ES_ID of another elementary stream */
    unsigned URL_Flag             : 1;  /* no external URL referencing stream in MP4 */
    unsigned OCRstreamFlag        : 1;  /* no Object Clock Reference stream in this muxer (shall be false in MP4, useful if we're importing from MPEG-2?) */
    unsigned streamPriority       : 5;  /* no priority among streams in this muxer, higher is important */
    uint16_t dependsOn_ES_ID;
    uint8_t URLlength;
    uint8_t URLstring[255];
    uint16_t OCR_ES_Id;
    mp4sys_DecoderConfigDescriptor_t *decConfigDescr; /* cannot be NULL. */
    mp4sys_SLConfigDescriptor_t *slConfigDescr;
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
        mp4sys_ODProfileLevelIndication       ODProfileLevelIndication;
        mp4sys_sceneProfileLevelIndication    sceneProfileLevelIndication;
        mp4a_audioProfileLevelIndication      audioProfileLevelIndication;
        mp4sys_visualProfileLevelIndication   visualProfileLevelIndication;
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
    mp4sys_DecoderSpecificInfo_t *dsi = (mp4sys_DecoderSpecificInfo_t *)lsmash_malloc_zero( sizeof(mp4sys_DecoderSpecificInfo_t) );
    if( !dsi )
        return -1;
    dsi->header.tag = MP4SYS_DESCRIPTOR_TAG_DecSpecificInfoTag;
    dsi->data = lsmash_memdup( dsi_payload, dsi_payload_length );
    if( !dsi->data )
    {
        free( dsi );
        return -1;
    }
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
    mp4sys_DecoderConfigDescriptor_t *dcd = (mp4sys_DecoderConfigDescriptor_t *)lsmash_malloc_zero( sizeof(mp4sys_DecoderConfigDescriptor_t) );
    if( !dcd )
        return -1;
    dcd->header.tag = MP4SYS_DESCRIPTOR_TAG_DecoderConfigDescrTag;
    dcd->objectTypeIndication = objectTypeIndication;
    dcd->streamType = streamType;
    dcd->reserved = 1;
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
    mp4sys_SLConfigDescriptor_t *slcd = (mp4sys_SLConfigDescriptor_t *)lsmash_malloc_zero( sizeof(mp4sys_SLConfigDescriptor_t) );
    if( !slcd )
        return -1;
    slcd->header.tag        = MP4SYS_DESCRIPTOR_TAG_SLConfigDescrTag;
    slcd->predefined        = 0x02;     /* MP4 file which does not use URL_Flag shall have constant value 0x02 */
    slcd->useTimeStampsFlag = 1;
    debug_if( mp4sys_remove_SLConfigDescriptor( esd ) )
    {
        free( slcd );
        return -1;
    }
    esd->slConfigDescr = slcd;
    return 0;
}

/* ES_ID of the ES Descriptor is stored as 0 when the ES Descriptor is built into sample descriptions in MP4 file format
 * since the lower 16 bits of the track_ID is used, instead of ES_ID, for the identifier of the elemental stream within the track. */
mp4sys_ES_Descriptor_t* mp4sys_create_ES_Descriptor( uint16_t ES_ID )
{
    mp4sys_ES_Descriptor_t *esd = (mp4sys_ES_Descriptor_t *)lsmash_malloc_zero( sizeof(mp4sys_ES_Descriptor_t) );
    if( !esd )
        return NULL;
    esd->header.tag = MP4SYS_DESCRIPTOR_TAG_ES_DescrTag;
    esd->ES_ID = ES_ID;
    return esd;
}

/* NOTE: This is only for MP4_IOD and MP4_OD, not for ISO Base Media's ObjectDescriptor and InitialObjectDescriptor */
int mp4sys_add_ES_ID_Inc( mp4sys_ObjectDescriptor_t* od, uint32_t Track_ID )
{
    if( !od )
        return -1;
    if( !od->esDescr && ( od->esDescr = lsmash_create_entry_list() ) == NULL )
        return -1;
    mp4sys_ES_ID_Inc_t *es_id_inc = (mp4sys_ES_ID_Inc_t *)lsmash_malloc_zero( sizeof(mp4sys_ES_ID_Inc_t) );
    if( !es_id_inc )
        return -1;
    es_id_inc->header.tag = MP4SYS_DESCRIPTOR_TAG_ES_ID_IncTag;
    es_id_inc->Track_ID = Track_ID;
    if( lsmash_add_entry( od->esDescr, es_id_inc ) )
    {
        free( es_id_inc );
        return -1;
    }
    return 0;
}

/* NOTE: This is only for MP4_OD, not for ISO Base Media's ObjectDescriptor */
mp4sys_ObjectDescriptor_t* mp4sys_create_ObjectDescriptor( uint16_t ObjectDescriptorID )
{
    mp4sys_ObjectDescriptor_t *od = (mp4sys_ObjectDescriptor_t *)lsmash_malloc_zero( sizeof(mp4sys_ObjectDescriptor_t) );
    if( !od )
        return NULL;
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
    for( i = 1; payload_size_in_byte >> ( 7 * i ); i++ );
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
    mp4sys_SLConfigDescriptor_t *slcd = esd->slConfigDescr;
    uint32_t size = 1;
    if( slcd->predefined == 0x00 )
        size += 15;
    if( slcd->durationFlag )
        size += 8;
    if( !slcd->useTimeStampsFlag )
        size += (2 * slcd->timeStampLength + 7) / 8;
    esd->slConfigDescr->header.size = size;
    return mp4sys_get_descriptor_size( size );
}

uint32_t mp4sys_update_ES_Descriptor_size( mp4sys_ES_Descriptor_t* esd )
{
    if( !esd )
        return 0;
    uint32_t size = 3;
    if( esd->streamDependenceFlag )
        size += 2;
    if( esd->URL_Flag )
        size += 1 + esd->URLlength;
    if( esd->OCRstreamFlag )
        size += 2;
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

static int mp4sys_put_DecoderSpecificInfo( lsmash_bs_t *bs, mp4sys_DecoderSpecificInfo_t* dsi )
{
    debug_if( !bs )
        return -1;
    if( !dsi )
        return 0; /* can be NULL */
    debug_if( mp4sys_put_descriptor_header( bs, &dsi->header ) )
        return -1;
    if( dsi->data && dsi->header.size != 0 )
        lsmash_bs_put_bytes( bs, dsi->header.size, dsi->data );
    return 0;
}

static int mp4sys_put_DecoderConfigDescriptor( lsmash_bs_t *bs, mp4sys_DecoderConfigDescriptor_t* dcd )
{
    debug_if( !bs )
        return -1;
    if( !dcd )
        return -1; /* cannot be NULL */
    debug_if( mp4sys_put_descriptor_header( bs, &dcd->header ) )
        return -1;
    lsmash_bs_put_byte( bs, dcd->objectTypeIndication );
    uint8_t temp;
    temp  = (dcd->streamType << 2) & 0x3F;
    temp |= (dcd->upStream   << 1) & 0x01;
    temp |=  dcd->reserved         & 0x01;
    lsmash_bs_put_byte( bs, temp );
    lsmash_bs_put_be24( bs, dcd->bufferSizeDB );
    lsmash_bs_put_be32( bs, dcd->maxBitrate );
    lsmash_bs_put_be32( bs, dcd->avgBitrate );
    return mp4sys_put_DecoderSpecificInfo( bs, dcd->decSpecificInfo );
    /* here, profileLevelIndicationIndexDescriptor is omitted */
}

static int mp4sys_put_SLConfigDescriptor( lsmash_bs_t *bs, mp4sys_SLConfigDescriptor_t* slcd )
{
    debug_if( !bs )
        return -1;
    if( !slcd )
        return 0;
    debug_if( mp4sys_put_descriptor_header( bs, &slcd->header ) )
        return -1;
    lsmash_bs_put_byte( bs, slcd->predefined );
    if( slcd->predefined == 0x00 )
    {
        uint8_t temp8;
        temp8  = slcd->useAccessUnitStartFlag       << 7;
        temp8 |= slcd->useAccessUnitEndFlag         << 6;
        temp8 |= slcd->useRandomAccessPointFlag     << 5;
        temp8 |= slcd->hasRandomAccessUnitsOnlyFlag << 4;
        temp8 |= slcd->usePaddingFlag               << 3;
        temp8 |= slcd->useTimeStampsFlag            << 2;
        temp8 |= slcd->useIdleFlag                  << 1;
        temp8 |= slcd->durationFlag;
        lsmash_bs_put_byte( bs, temp8 );
        lsmash_bs_put_be32( bs, slcd->timeStampResolution );
        lsmash_bs_put_be32( bs, slcd->OCRResolution );
        lsmash_bs_put_byte( bs, slcd->timeStampLength );
        lsmash_bs_put_byte( bs, slcd->OCRLength );
        lsmash_bs_put_byte( bs, slcd->AU_Length );
        lsmash_bs_put_byte( bs, slcd->instantBitrateLength );
        uint16_t temp16;
        temp16  = slcd->degradationPriorityLength << 12;
        temp16 |= slcd->AU_seqNumLength           << 7;
        temp16 |= slcd->packetSeqNumLength        << 2;
        temp16 |= slcd->reserved;
        lsmash_bs_put_be16( bs, temp16 );
    }
    if( slcd->durationFlag )
    {
        lsmash_bs_put_be32( bs, slcd->timeScale );
        lsmash_bs_put_be16( bs, slcd->accessUnitDuration );
        lsmash_bs_put_be16( bs, slcd->compositionUnitDuration );
    }
    if( !slcd->useTimeStampsFlag )
    {
        lsmash_bits_t *bits = lsmash_bits_create( bs );
        if( !bits )
            return -1;
        lsmash_bits_put( bits, slcd->timeStampLength, slcd->startDecodingTimeStamp );
        lsmash_bits_put( bits, slcd->timeStampLength, slcd->startCompositionTimeStamp );
        lsmash_bits_put_align( bits );
        lsmash_bits_cleanup( bits );
    }
    return 0;
}

int mp4sys_put_ES_Descriptor( lsmash_bs_t *bs, mp4sys_ES_Descriptor_t *esd )
{
    if( !bs || !esd )
        return -1;
    debug_if( mp4sys_put_descriptor_header( bs, &esd->header ) )
        return -1;
    lsmash_bs_put_be16( bs, esd->ES_ID );
    uint8_t temp;
    temp  = esd->streamDependenceFlag << 7;
    temp |= esd->URL_Flag             << 6;
    temp |= esd->OCRstreamFlag        << 5;
    temp |= esd->streamPriority;
    lsmash_bs_put_byte( bs, temp );
    if( esd->streamDependenceFlag )
        lsmash_bs_put_be16( bs, esd->dependsOn_ES_ID );
    if( esd->URL_Flag )
    {
        lsmash_bs_put_byte( bs, esd->URLlength );
        lsmash_bs_put_bytes( bs, esd->URLlength, esd->URLstring );
    }
    if( esd->OCRstreamFlag )
        lsmash_bs_put_be16( bs, esd->OCR_ES_Id );
    /* here, some syntax elements are omitted due to previous flags (all 0) */
    if( mp4sys_put_DecoderConfigDescriptor( bs, esd->decConfigDescr ) )
        return -1;
    return mp4sys_put_SLConfigDescriptor( bs, esd->slConfigDescr );
}

int mp4sys_write_ES_Descriptor( lsmash_bs_t *bs, mp4sys_ES_Descriptor_t *esd )
{
    if( mp4sys_put_ES_Descriptor( bs, esd ) )
        return -1;
    return lsmash_bs_write_data( bs );
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

#ifdef LSMASH_DEMUXER_ENABLED
static int mp4sys_copy_DecoderSpecificInfo( mp4sys_ES_Descriptor_t *dst, mp4sys_ES_Descriptor_t *src )
{
    if( !src || !src->decConfigDescr || !dst || !dst->decConfigDescr
     || mp4sys_remove_DecoderSpecificInfo( dst ) )
        return -1;
    mp4sys_DecoderSpecificInfo_t *dsi = src->decConfigDescr->decSpecificInfo;
    if( !dsi || !dsi->data || !dsi->header.size )
        return 0;
    return mp4sys_add_DecoderSpecificInfo( dst, dsi->data, dsi->header.size );
}

static int mp4sys_copy_DecoderConfigDescriptor( mp4sys_ES_Descriptor_t *dst, mp4sys_ES_Descriptor_t *src )
{
    if( !src || !dst
     || mp4sys_remove_DecoderConfigDescriptor( dst ) )
        return -1;
    if( !src->decConfigDescr )
        return 0;
    if( mp4sys_add_DecoderConfigDescriptor( dst, 0, 0, 0, 0, 0 ) )
        return -1;
    *dst->decConfigDescr = *src->decConfigDescr;
    dst->decConfigDescr->decSpecificInfo = NULL;
    return mp4sys_copy_DecoderSpecificInfo( dst, src );
}

static int mp4sys_copy_SLConfigDescriptor( mp4sys_ES_Descriptor_t *dst, mp4sys_ES_Descriptor_t *src )
{
    if( !src || !dst
     || mp4sys_remove_SLConfigDescriptor( dst ) )
        return -1;
    if( !src->slConfigDescr )
        return 0;
    if( mp4sys_add_SLConfigDescriptor( dst ) )
        return -1;
    *dst->slConfigDescr = *src->slConfigDescr;
    return 0;
}

mp4sys_ES_Descriptor_t *mp4sys_duplicate_ES_Descriptor( mp4sys_ES_Descriptor_t *src )
{
    if( !src )
        return NULL;
    mp4sys_ES_Descriptor_t *dst = mp4sys_create_ES_Descriptor( 0 );
    if( !dst )
        return NULL;
    *dst = *src;
    dst->decConfigDescr = NULL;
    dst->slConfigDescr = NULL;
    if( mp4sys_copy_DecoderConfigDescriptor( dst, src )
     || mp4sys_copy_SLConfigDescriptor( dst, src ) )
    {
        mp4sys_remove_ES_Descriptor( dst );
        return NULL;
    }
    return dst;
}

static void mp4sys_print_descriptor_header( FILE *fp, mp4sys_descriptor_head_t *header, int indent )
{
    static const char *descriptor_names_table[256] =
        {
            "Forbidden",
            "ObjectDescriptor",
            "InitialObjectDescriptor",
            "ES_Descriptor",
            "DecoderConfigDescriptor",
            "DecoderSpecificInfo",
            "SLConfigDescriptor",
            [0x0E] = "ES_ID_Inc",
            [0x0F] = "ES_ID_Ref",
            [0x10] = "MP4_IOD",
            [0x11] = "MP4_OD"
        };
    if( descriptor_names_table[ header->tag ] )
        lsmash_ifprintf( fp, indent, "[tag = 0x%02"PRIx8": %s]\n", header->tag, descriptor_names_table[ header->tag ] );
    else
        lsmash_ifprintf( fp, indent, "[tag = 0x%02"PRIx8"]\n", header->tag );
    lsmash_ifprintf( fp, ++indent, "expandableClassSize = %"PRIu32"\n", header->size );
}

static void mp4sys_print_DecoderConfigDescriptor( FILE *fp, mp4sys_DecoderConfigDescriptor_t *dcd, int indent )
{
    static const char *object_type_indication_descriptions_table[256] =
        {
            "Forbidden",
            "Systems ISO/IEC 14496-1 (a)",
            "Systems ISO/IEC 14496-1 (b)",
            "Interaction Stream",
            "Systems ISO/IEC 14496-1 Extended BIFS Configuration",
            "Systems ISO/IEC 14496-1 AFX",
            "Font Data Stream",
            "Synthesized Texture Stream",
            "Streaming Text Stream",
            "LASeR Stream",
            "Simple Aggregation Format (SAF) Stream",
            [0x20] = "Visual ISO/IEC 14496-2",
            [0x21] = "Visual ITU-T Recommendation H.264 | ISO/IEC 14496-10",
            [0x22] = "Parameter Sets for ITU-T Recommendation H.264 | ISO/IEC 14496-10",
            [0x40] = "Audio ISO/IEC 14496-3",
            [0x60] = "Visual ISO/IEC 13818-2 Simple Profile",
            [0x61] = "Visual ISO/IEC 13818-2 Main Profile",
            [0x62] = "Visual ISO/IEC 13818-2 SNR Profile",
            [0x63] = "Visual ISO/IEC 13818-2 Spatial Profile",
            [0x64] = "Visual ISO/IEC 13818-2 High Profile",
            [0x65] = "Visual ISO/IEC 13818-2 422 Profile",
            [0x66] = "Audio ISO/IEC 13818-7 Main Profile",
            [0x67] = "Audio ISO/IEC 13818-7 LowComplexity Profile",
            [0x68] = "Audio ISO/IEC 13818-7 Scaleable Sampling Rate Profile",
            [0x69] = "Audio ISO/IEC 13818-3",
            [0x6A] = "Visual ISO/IEC 11172-2",
            [0x6B] = "Audio ISO/IEC 11172-3",
            [0x6C] = "Visual ISO/IEC 10918-1",
            [0x6D] = "Portable Network Graphics",
            [0x6E] = "Visual ISO/IEC 15444-1 (JPEG 2000)",
            [0xA0] = "EVRC Voice",
            [0xA1] = "SMV Voice",
            [0xA2] = "3GPP2 Compact Multimedia Format (CMF)",
            [0xA3] = "SMPTE VC-1 Video",
            [0xA4] = "Dirac Video Coder",
            [0xA5] = "AC-3 Audio",
            [0xA6] = "Enhanced AC-3 audio",
            [0xA7] = "DRA Audio",
            [0xA8] = "ITU G.719 Audio",
            [0xA9] = "DTS Coherent Acoustics audio",
            [0xAA] = "DTS-HD High Resolution Audio",
            [0xAB] = "DTS-HD Master Audio",
            [0xAC] = "DTS Express low bit rate audio",
            [0xE1] = "13K Voice",
            [0xFF] = "no object type specified"
        };
    static const char *stream_type_descriptions_table[64] =
        {
            "Forbidden",
            "ObjectDescriptorStream",
            "ClockReferenceStream",
            "SceneDescriptionStream",
            "VisualStream",
            "AudioStream",
            "MPEG7Stream",
            "IPMPStream",
            "ObjectContentInfoStream",
            "MPEGJStream",
            "Interaction Stream",
            "IPMPToolStream",
            "FontDataStream",
            "StreamingText"
        };
    mp4sys_print_descriptor_header( fp, &dcd->header, indent++ );
    if( object_type_indication_descriptions_table[ dcd->objectTypeIndication ] )
        lsmash_ifprintf( fp, indent, "objectTypeIndication = 0x%02"PRIx8" (%s)\n", dcd->objectTypeIndication, object_type_indication_descriptions_table[ dcd->objectTypeIndication ] );
    else
        lsmash_ifprintf( fp, indent, "objectTypeIndication = 0x%02"PRIx8"\n", dcd->objectTypeIndication );
    if( stream_type_descriptions_table[ dcd->streamType ] )
        lsmash_ifprintf( fp, indent, "streamType = 0x%02"PRIx8" (%s)\n", dcd->streamType, stream_type_descriptions_table[ dcd->streamType ] );
    else
        lsmash_ifprintf( fp, indent, "streamType = 0x%02"PRIx8"\n", dcd->streamType );
    lsmash_ifprintf( fp, indent, "upStream = %"PRIu8"\n", dcd->upStream );
    lsmash_ifprintf( fp, indent, "reserved = %"PRIu8"\n", dcd->reserved );
    lsmash_ifprintf( fp, indent, "bufferSizeDB = %"PRIu32"\n", dcd->bufferSizeDB );
    lsmash_ifprintf( fp, indent, "maxBitrate = %"PRIu32"\n", dcd->maxBitrate );
    lsmash_ifprintf( fp, indent, "avgBitrate = %"PRIu32"%s\n", dcd->avgBitrate, dcd->avgBitrate ? "" : " (variable bitrate)" );
}

static void mp4sys_print_SLConfigDescriptor( FILE *fp, mp4sys_SLConfigDescriptor_t *slcd, int indent )
{
    mp4sys_print_descriptor_header( fp, &slcd->header, indent++ );
    lsmash_ifprintf( fp, indent, "predefined = %"PRIu8"\n", slcd->predefined );
    if( slcd->predefined == 0 )
    {
        lsmash_ifprintf( fp, indent, "useAccessUnitStartFlag = %"PRIu8"\n", slcd->useAccessUnitStartFlag );
        lsmash_ifprintf( fp, indent, "useAccessUnitEndFlag = %"PRIu8"\n", slcd->useAccessUnitEndFlag );
        lsmash_ifprintf( fp, indent, "useRandomAccessPointFlag = %"PRIu8"\n", slcd->useRandomAccessPointFlag );
        lsmash_ifprintf( fp, indent, "hasRandomAccessUnitsOnlyFlag = %"PRIu8"\n", slcd->hasRandomAccessUnitsOnlyFlag );
        lsmash_ifprintf( fp, indent, "usePaddingFlag = %"PRIu8"\n", slcd->usePaddingFlag );
        lsmash_ifprintf( fp, indent, "useTimeStampsFlag = %"PRIu8"\n", slcd->useTimeStampsFlag );
        lsmash_ifprintf( fp, indent, "useIdleFlag = %"PRIu8"\n", slcd->useIdleFlag );
        lsmash_ifprintf( fp, indent, "durationFlag = %"PRIu8"\n", slcd->durationFlag );
        lsmash_ifprintf( fp, indent, "timeStampResolution = %"PRIu32"\n", slcd->timeStampResolution );
        lsmash_ifprintf( fp, indent, "OCRResolution = %"PRIu32"\n", slcd->OCRResolution );
        lsmash_ifprintf( fp, indent, "timeStampLength = %"PRIu8"\n", slcd->timeStampLength );
        lsmash_ifprintf( fp, indent, "OCRLength = %"PRIu8"\n", slcd->OCRLength );
        lsmash_ifprintf( fp, indent, "AU_Length = %"PRIu8"\n", slcd->AU_Length );
        lsmash_ifprintf( fp, indent, "instantBitrateLength = %"PRIu8"\n", slcd->instantBitrateLength );
        lsmash_ifprintf( fp, indent, "degradationPriorityLength = %"PRIu8"\n", slcd->degradationPriorityLength );
        lsmash_ifprintf( fp, indent, "AU_seqNumLength = %"PRIu8"\n", slcd->AU_seqNumLength );
        lsmash_ifprintf( fp, indent, "packetSeqNumLength = %"PRIu8"\n", slcd->packetSeqNumLength );
        lsmash_ifprintf( fp, indent, "reserved = 0x%01"PRIx8"\n", slcd->reserved );
    }
    if( slcd->durationFlag )
    {
        lsmash_ifprintf( fp, indent, "timeScale = %"PRIu32"\n", slcd->timeScale );
        lsmash_ifprintf( fp, indent, "accessUnitDuration = %"PRIu16"\n", slcd->accessUnitDuration );
        lsmash_ifprintf( fp, indent, "compositionUnitDuration = %"PRIu16"\n", slcd->compositionUnitDuration );
    }
    if( !slcd->useTimeStampsFlag )
    {
        lsmash_ifprintf( fp, indent, "startDecodingTimeStamp = %"PRIu64"\n", slcd->startDecodingTimeStamp );
        lsmash_ifprintf( fp, indent, "startCompositionTimeStamp = %"PRIu64"\n", slcd->startCompositionTimeStamp );
    }
}

static void mp4sys_print_ES_Descriptor( FILE *fp, mp4sys_ES_Descriptor_t *esd, int indent )
{
    mp4sys_print_descriptor_header( fp, &esd->header, indent++ );
    lsmash_ifprintf( fp, indent, "ES_ID = %"PRIu16"\n", esd->ES_ID );
    lsmash_ifprintf( fp, indent, "streamDependenceFlag = %"PRIu8"\n", esd->streamDependenceFlag );
    lsmash_ifprintf( fp, indent, "URL_Flag = %"PRIu8"\n", esd->URL_Flag );
    lsmash_ifprintf( fp, indent, "OCRstreamFlag = %"PRIu8"\n", esd->OCRstreamFlag );
    lsmash_ifprintf( fp, indent, "streamPriority = %"PRIu8"\n", esd->streamPriority );
    if( esd->streamDependenceFlag )
        lsmash_ifprintf( fp, indent, "dependsOn_ES_ID = %"PRIu16"\n", esd->dependsOn_ES_ID );
    if( esd->URL_Flag )
    {
        lsmash_ifprintf( fp, indent, "URLlength = %"PRIu8"\n", esd->URLlength );
        char URLstring[256] = { 0 };
        memcpy( URLstring, esd->URLstring, esd->URLlength );
        lsmash_ifprintf( fp, indent, "URLlength = %s\n", URLstring );
    }
    if( esd->OCRstreamFlag )
        lsmash_ifprintf( fp, indent, "OCR_ES_Id = %"PRIu16"\n", esd->OCR_ES_Id );
    mp4sys_print_DecoderConfigDescriptor( fp, esd->decConfigDescr, indent );
    mp4sys_print_SLConfigDescriptor( fp, esd->slConfigDescr, indent );
}

int mp4sys_print_codec_specific( FILE *fp, lsmash_root_t *root, isom_box_t *box, int level )
{
    assert( fp && root && box );
    isom_extension_box_t *ext = (isom_extension_box_t *)box;
    assert( ext->format == EXTENSION_FORMAT_BOX && ext->form.box );
    isom_esds_t *esds = (isom_esds_t *)ext->form.box;
    int indent = level;
    lsmash_ifprintf( fp, indent++, "[%s: Elemental Stream Descriptor Box]\n", isom_4cc2str( esds->type ) );
    lsmash_ifprintf( fp, indent, "position = %"PRIu64"\n", esds->pos );
    lsmash_ifprintf( fp, indent, "size = %"PRIu64"\n", esds->size );
    lsmash_ifprintf( fp, indent, "version = %"PRIu8"\n", esds->version );
    lsmash_ifprintf( fp, indent, "flags = 0x%06"PRIx32"\n", esds->flags & 0x00ffffff );
    mp4sys_print_ES_Descriptor( fp, esds->ES, indent );
    return 0;
}
#endif /* LSMASH_DEMUXER_ENABLED */

static void mp4sys_get_descriptor_header( lsmash_bs_t *bs, mp4sys_descriptor_head_t* header )
{
    header->tag  = lsmash_bs_get_byte( bs );
    uint8_t temp = lsmash_bs_get_byte( bs );
    int nextByte = temp & 0x80;
    uint32_t sizeOfInstance = temp & 0x7F;
    while( nextByte )
    {
        temp = lsmash_bs_get_byte( bs );
        nextByte = temp & 0x80;
        sizeOfInstance = (sizeOfInstance << 7) | (temp & 0x7F);
    }
    header->size = sizeOfInstance;
}

static int mp4sys_get_DecoderSpecificInfo( lsmash_bs_t *bs, mp4sys_ES_Descriptor_t *esd )
{
    mp4sys_DecoderSpecificInfo_t *dsi = (mp4sys_DecoderSpecificInfo_t *)lsmash_malloc_zero( sizeof(mp4sys_DecoderSpecificInfo_t) );
    if( !dsi )
        return -1;
    esd->decConfigDescr->decSpecificInfo = dsi;
    mp4sys_get_descriptor_header( bs, &dsi->header );
    if( dsi->header.size )
    {
        dsi->data = lsmash_bs_get_bytes( bs, dsi->header.size );
        if( !dsi->data )
        {
            mp4sys_remove_DecoderSpecificInfo( esd );
            return -1;
        }
    }
    return 0;
}

static int mp4sys_get_DecoderConfigDescriptor( lsmash_bs_t *bs, mp4sys_ES_Descriptor_t *esd )
{
    mp4sys_DecoderConfigDescriptor_t *dcd = (mp4sys_DecoderConfigDescriptor_t *)lsmash_malloc_zero( sizeof(mp4sys_DecoderConfigDescriptor_t) );
    if( !dcd )
        return -1;
    esd->decConfigDescr = dcd;
    mp4sys_get_descriptor_header( bs, &dcd->header );
    dcd->objectTypeIndication = lsmash_bs_get_byte( bs );
    uint8_t temp              = lsmash_bs_get_byte( bs );
    dcd->streamType = (temp >> 2) & 0x3F;
    dcd->upStream   = (temp >> 1) & 0x01;
    dcd->reserved   =  temp       & 0x01;
    dcd->bufferSizeDB         = lsmash_bs_get_be24( bs );
    dcd->maxBitrate           = lsmash_bs_get_be32( bs );
    dcd->avgBitrate           = lsmash_bs_get_be32( bs );
    if( dcd->header.size > 13
     && mp4sys_get_DecoderSpecificInfo( bs, esd ) )
    {
        mp4sys_remove_DecoderConfigDescriptor( esd );
        return -1;
    }
    return 0;
}

static int mp4sys_get_SLConfigDescriptor( lsmash_bs_t *bs, mp4sys_ES_Descriptor_t *esd )
{
    mp4sys_SLConfigDescriptor_t *slcd = (mp4sys_SLConfigDescriptor_t *)lsmash_malloc_zero( sizeof(mp4sys_SLConfigDescriptor_t) );
    if( !slcd )
        return -1;
    esd->slConfigDescr = slcd;
    mp4sys_get_descriptor_header( bs, &slcd->header );
    slcd->predefined = lsmash_bs_get_byte( bs );
    if( slcd->predefined == 0x00 )
    {
        uint8_t temp8              = lsmash_bs_get_byte( bs );
        slcd->useAccessUnitStartFlag       = (temp8 >> 7) & 0x01;
        slcd->useAccessUnitEndFlag         = (temp8 >> 6) & 0x01;
        slcd->useRandomAccessPointFlag     = (temp8 >> 5) & 0x01;
        slcd->hasRandomAccessUnitsOnlyFlag = (temp8 >> 4) & 0x01;
        slcd->usePaddingFlag               = (temp8 >> 3) & 0x01;
        slcd->useTimeStampsFlag            = (temp8 >> 2) & 0x01;
        slcd->useIdleFlag                  = (temp8 >> 1) & 0x01;
        slcd->durationFlag                 =  temp8       & 0x01;
        slcd->timeStampResolution  = lsmash_bs_get_be32( bs );
        slcd->OCRResolution        = lsmash_bs_get_be32( bs );
        slcd->timeStampLength      = lsmash_bs_get_byte( bs );
        slcd->OCRLength            = lsmash_bs_get_byte( bs );
        slcd->AU_Length            = lsmash_bs_get_byte( bs );
        slcd->instantBitrateLength = lsmash_bs_get_byte( bs );
        uint16_t temp16            = lsmash_bs_get_be16( bs );
        slcd->degradationPriorityLength = (temp16 >> 12) & 0x0F;
        slcd->AU_seqNumLength           = (temp16 >>  7) & 0x1F;
        slcd->packetSeqNumLength        = (temp16 >>  2) & 0x1F;
        slcd->reserved                  =  temp16        & 0x03;
    }
    else if( slcd->predefined == 0x01 )
    {
        slcd->timeStampResolution  = 1000;
        slcd->timeStampLength      = 32;
    }
    else if( slcd->predefined == 0x02 )
        slcd->useTimeStampsFlag = 1;
    if( slcd->durationFlag )
    {
        slcd->timeScale               = lsmash_bs_get_be32( bs );
        slcd->accessUnitDuration      = lsmash_bs_get_be16( bs );
        slcd->compositionUnitDuration = lsmash_bs_get_be16( bs );
    }
    if( !slcd->useTimeStampsFlag )
    {
        lsmash_bits_t *bits = lsmash_bits_create( bs );
        if( !bits )
            return -1;
        slcd->startDecodingTimeStamp    = lsmash_bits_get( bits, slcd->timeStampLength );
        slcd->startCompositionTimeStamp = lsmash_bits_get( bits, slcd->timeStampLength );
        lsmash_bits_cleanup( bits );
    }
    return 0;
}

mp4sys_ES_Descriptor_t *mp4sys_get_ES_Descriptor( lsmash_bs_t *bs )
{
    mp4sys_ES_Descriptor_t *esd = (mp4sys_ES_Descriptor_t *)lsmash_malloc_zero( sizeof(mp4sys_ES_Descriptor_t) );
    if( !esd )
        return NULL;
    mp4sys_get_descriptor_header( bs, &esd->header );
    esd->ES_ID   = lsmash_bs_get_be16( bs );
    uint8_t temp = lsmash_bs_get_byte( bs );
    esd->streamDependenceFlag = (temp >> 7) & 0x01;
    esd->URL_Flag             = (temp >> 6) & 0x01;
    esd->OCRstreamFlag        = (temp >> 5) & 0x01;
    esd->streamPriority       =  temp       & 0x1F;
    if( esd->streamDependenceFlag )
        esd->dependsOn_ES_ID = lsmash_bs_get_be16( bs );
    if( esd->URL_Flag )
    {
        esd->URLlength = lsmash_bs_get_byte( bs );
        for( uint8_t i = 0; i < esd->URLlength; i++ )
            esd->URLstring[i] = lsmash_bs_get_byte( bs );
    }
    if( esd->OCRstreamFlag )
        esd->OCR_ES_Id = lsmash_bs_get_be16( bs );
    if( mp4sys_get_DecoderConfigDescriptor( bs, esd )
     || mp4sys_get_SLConfigDescriptor( bs, esd ) )
    {
        mp4sys_remove_ES_Descriptor( esd );
        return NULL;
    }
    return esd;
}

static uint8_t *mp4sys_export_DecoderSpecificInfo( mp4sys_ES_Descriptor_t *esd, uint32_t *dsi_payload_length )
{
    if( !esd || !esd->decConfigDescr || !esd->decConfigDescr->decSpecificInfo )
        return NULL;
    mp4sys_DecoderSpecificInfo_t *dsi = (mp4sys_DecoderSpecificInfo_t *)esd->decConfigDescr->decSpecificInfo;
    uint8_t *dsi_payload = NULL;
    /* DecoderSpecificInfo can be absent. */
    if( dsi->header.size )
    {
        dsi_payload = lsmash_memdup( dsi->data, dsi->header.size );
        if( !dsi_payload )
            return NULL;
    }
    if( dsi_payload_length )
        *dsi_payload_length = dsi->header.size;
    return dsi_payload;
}

/* Sumamry is needed to decide ProfileLevelIndication.
 * Currently, support audio's only. */
int mp4sys_setup_summary_from_DecoderSpecificInfo( lsmash_audio_summary_t *summary, mp4sys_ES_Descriptor_t *esd )
{
    uint32_t dsi_payload_length = UINT32_MAX;       /* arbitrary */
    uint8_t *dsi_payload = mp4sys_export_DecoderSpecificInfo( esd, &dsi_payload_length );
    if( !dsi_payload && dsi_payload_length )
        return -1;
    if( dsi_payload_length && mp4a_setup_summary_from_AudioSpecificConfig( summary, dsi_payload, dsi_payload_length ) )
    {
        free( dsi_payload );
        return -1;
    }
    return 0;
}

/**** following functions are for facilitation purpose ****/

mp4sys_ES_Descriptor_t *mp4sys_setup_ES_Descriptor( mp4sys_ES_Descriptor_params_t *params )
{
    if( !params )
        return NULL;
    mp4sys_ES_Descriptor_t *esd = mp4sys_create_ES_Descriptor( params->ES_ID );
    if( !esd )
        return NULL;
    if( mp4sys_add_SLConfigDescriptor( esd )
     || mp4sys_add_DecoderConfigDescriptor( esd, params->objectTypeIndication, params->streamType, params->bufferSizeDB, params->maxBitrate, params->avgBitrate )
     || (params->dsi_payload && params->dsi_payload_length != 0 && mp4sys_add_DecoderSpecificInfo( esd, params->dsi_payload, params->dsi_payload_length )) )
    {
        mp4sys_remove_ES_Descriptor( esd );
        return NULL;
    }
    return esd;
}

int lsmash_set_mp4sys_decoder_specific_info( lsmash_mp4sys_decoder_parameters_t *param, uint8_t *payload, uint32_t payload_length )
{
    if( !param || !payload || payload_length == 0 )
        return -1;
    if( !param->dsi )
    {
        param->dsi = lsmash_malloc_zero( sizeof(lsmash_mp4sys_decoder_specific_info_t) );
        if( !param->dsi )
            return -1;
    }
    else
    {
        if( param->dsi->payload )
        {
            free( param->dsi->payload );
            param->dsi->payload = NULL;
        }
        param->dsi->payload_length = 0;
    }
    param->dsi->payload = lsmash_memdup( payload, payload_length );
    if( !param->dsi->payload )
        return -1;
    param->dsi->payload_length = payload_length;
    return 0;
}

void lsmash_destroy_mp4sys_decoder_specific_info( lsmash_mp4sys_decoder_parameters_t *param )
{
    if( !param || !param->dsi )
        return;
    if( param->dsi->payload )
        free( param->dsi->payload );
    free( param->dsi );
    param->dsi = NULL;
}

void mp4sys_destruct_decoder_config( void *data )
{
    if( !data )
        return;
    lsmash_destroy_mp4sys_decoder_specific_info( data );
    free( data );
}

uint8_t *lsmash_create_mp4sys_decoder_config( lsmash_mp4sys_decoder_parameters_t *param, uint32_t *data_length )
{
    if( !param || !data_length )
        return NULL;
    mp4sys_ES_Descriptor_params_t esd_param = { 0 };
    esd_param.ES_ID                = 0; /* Within sample description, ES_ID is stored as 0. */
    esd_param.objectTypeIndication = param->objectTypeIndication;
    esd_param.streamType           = param->streamType;
    esd_param.bufferSizeDB         = param->bufferSizeDB;
    esd_param.maxBitrate           = param->maxBitrate;
    esd_param.avgBitrate           = param->avgBitrate;
    if( param->dsi && param->dsi->payload && param->dsi->payload_length )
    {
        esd_param.dsi_payload        = param->dsi->payload;
        esd_param.dsi_payload_length = param->dsi->payload_length;
    }
    mp4sys_ES_Descriptor_t *esd = mp4sys_setup_ES_Descriptor( &esd_param );
    if( !esd )
        return NULL;
    lsmash_bs_t *bs = lsmash_bs_create( NULL );
    if( !bs )
    {
        mp4sys_remove_ES_Descriptor( esd );
        return NULL;
    }
    lsmash_bs_put_be32( bs, ISOM_FULLBOX_COMMON_SIZE + mp4sys_update_ES_Descriptor_size( esd ) );
    lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_ESDS );
    lsmash_bs_put_be32( bs, 0 );
    mp4sys_put_ES_Descriptor( bs, esd );
    mp4sys_remove_ES_Descriptor( esd );
    uint8_t *data = lsmash_bs_export_data( bs, data_length );
    lsmash_bs_cleanup( bs );
    if( !data )
        return NULL;
    /* Update box size. */
    data[0] = ((*data_length) >> 24) & 0xff;
    data[1] = ((*data_length) >> 16) & 0xff;
    data[2] = ((*data_length) >>  8) & 0xff;
    data[3] =  (*data_length)        & 0xff;
    return data;
}

int mp4sys_construct_decoder_config( lsmash_codec_specific_t *dst, lsmash_codec_specific_t *src )
{
    assert( dst && dst->data.structured && src && src->data.unstructured );
    if( src->size < ISOM_FULLBOX_COMMON_SIZE + 23 )
        return -1;
    lsmash_mp4sys_decoder_parameters_t *param = (lsmash_mp4sys_decoder_parameters_t *)dst->data.structured;
    uint8_t *data = src->data.unstructured;
    uint64_t size = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    data += ISOM_BASEBOX_COMMON_SIZE;
    if( size == 1 )
    {
        size = ((uint64_t)data[0] << 56) | ((uint64_t)data[1] << 48) | ((uint64_t)data[2] << 40) | ((uint64_t)data[3] << 32)
             | ((uint64_t)data[4] << 24) | ((uint64_t)data[5] << 16) | ((uint64_t)data[6] <<  8) |  (uint64_t)data[7];
        data += 8;
    }
    if( size != src->size )
        return -1;
    data += 4;  /* Skip version and flags. */
    lsmash_bs_t *bs = lsmash_bs_create( NULL );
    if( !bs )
        return -1;
    if( lsmash_bs_import_data( bs, data, src->size - (src->data.unstructured - data) ) )
    {
        lsmash_bs_cleanup( bs );
        return -1;
    }
    mp4sys_ES_Descriptor_t *esd = mp4sys_get_ES_Descriptor( bs );
    lsmash_bs_cleanup( bs );
    if( !esd || !esd->decConfigDescr )
        return -1;
    mp4sys_DecoderConfigDescriptor_t *dcd = esd->decConfigDescr;
    param->objectTypeIndication = dcd->objectTypeIndication;
    param->streamType           = dcd->streamType;
    param->bufferSizeDB         = dcd->bufferSizeDB;
    param->maxBitrate           = dcd->maxBitrate;
    param->avgBitrate           = dcd->avgBitrate;
    mp4sys_DecoderSpecificInfo_t *dsi = dcd->decSpecificInfo;
    if( dsi && dsi->header.size && dsi->data
     && lsmash_set_mp4sys_decoder_specific_info( param, dsi->data, dsi->header.size ) )
    {
        mp4sys_remove_ES_Descriptor( esd );
        return -1;
    }
    mp4sys_remove_ES_Descriptor( esd );
    return 0;
}

int mp4sys_copy_decoder_config( lsmash_codec_specific_t *dst, lsmash_codec_specific_t *src )
{
    assert( src && src->format == LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED && src->data.structured );
    assert( dst && dst->format == LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED && dst->data.structured );
    lsmash_mp4sys_decoder_parameters_t *src_data = (lsmash_mp4sys_decoder_parameters_t *)src->data.structured;
    lsmash_mp4sys_decoder_parameters_t *dst_data = (lsmash_mp4sys_decoder_parameters_t *)dst->data.structured;
    lsmash_destroy_mp4sys_decoder_specific_info( dst_data );
    *dst_data = *src_data;
    dst_data->dsi = NULL;
    if( !src_data->dsi || !src_data->dsi->payload || src_data->dsi->payload_length == 0 )
        return 0;
    return lsmash_set_mp4sys_decoder_specific_info( dst_data, src_data->dsi->payload, src_data->dsi->payload_length );
}

lsmash_mp4sys_object_type_indication lsmash_mp4sys_get_object_type_indication( lsmash_summary_t *summary )
{
    if( !summary )
        return MP4SYS_OBJECT_TYPE_Forbidden;
    lsmash_codec_specific_t *orig = isom_get_codec_specific( summary->opaque, LSMASH_CODEC_SPECIFIC_DATA_TYPE_MP4SYS_DECODER_CONFIG );
    if( !orig )
        return MP4SYS_OBJECT_TYPE_Forbidden;
    /* Found decoder configuration.
     * Let's get objectTypeIndication. */
    lsmash_mp4sys_object_type_indication objectTypeIndication;
    if( orig->format == LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED )
        objectTypeIndication = ((lsmash_mp4sys_decoder_parameters_t *)orig->data.structured)->objectTypeIndication;
    else
    {
        lsmash_codec_specific_t *conv = lsmash_convert_codec_specific_format( orig, LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
        if( !conv )
            return MP4SYS_OBJECT_TYPE_Forbidden;
        objectTypeIndication = ((lsmash_mp4sys_decoder_parameters_t *)conv->data.structured)->objectTypeIndication;
        lsmash_destroy_codec_specific_data( conv );
    }
    return objectTypeIndication;
}

int lsmash_get_mp4sys_decoder_specific_info( lsmash_mp4sys_decoder_parameters_t *param, uint8_t **payload, uint32_t *payload_length )
{
    if( !param || !payload || !payload_length )
        return -1;
    if( !param->dsi || !param->dsi->payload || param->dsi->payload_length == 0 )
    {
        *payload = NULL;
        *payload_length = 0;
        return 0;
    }
    uint8_t *temp = lsmash_memdup( param->dsi->payload, param->dsi->payload_length );
    if( !temp )
        return -1;
    *payload = temp;
    *payload_length = param->dsi->payload_length;
    return 0;
}
