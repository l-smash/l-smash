/*****************************************************************************
 * importer.c:
 *****************************************************************************
 * Copyright (C) 2010 L-SMASH project
 *
 * Authors: Takashi Hirata <silverfilain@gmail.com>
 * Contributors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
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

#ifndef __MINGW32__
#define _FILE_OFFSET_BITS 64 /* FIXME: This is redundant. Should be concentrated in utils.h */
#endif

#define LSMASH_IMPORTER_INTERNAL
#include "importer.h"

#include "box.h"

#include <stdlib.h>
#include <string.h>

#ifdef __MINGW32__ /* FIXME: This is redundant. Should be concentrated in utils.h */
#define mp4sys_fseek fseeko64
#define mp4sys_ftell ftello64
#else
#define mp4sys_fseek fseek
#define mp4sys_ftell ftell
#endif

/***************************************************************************
    importer framework
***************************************************************************/
struct mp4sys_importer_tag;

typedef void ( *mp4sys_importer_cleanup )( struct mp4sys_importer_tag* importer );
typedef int ( *mp4sys_importer_get_accessunit )( struct mp4sys_importer_tag*, uint32_t track_number , void* buf, uint32_t* size );
typedef int ( *mp4sys_importer_probe )( struct mp4sys_importer_tag* importer );

typedef struct
{
    const char*                    name;
    int                            detectable;
    mp4sys_importer_probe          probe;
    mp4sys_importer_get_accessunit get_accessunit;
    mp4sys_importer_cleanup        cleanup;
} mp4sys_importer_functions;

typedef struct mp4sys_importer_tag
{
    FILE*                     stream;
    int                       is_stdin;
    void*                     info; /* importer internal status information. */
    mp4sys_importer_functions funcs;
    lsmash_entry_list_t*      summaries;
} mp4sys_importer_t;

typedef enum
{
    MP4SYS_IMPORTER_ERROR  = -1,
    MP4SYS_IMPORTER_OK     = 0,
    MP4SYS_IMPORTER_CHANGE = 1,
    MP4SYS_IMPORTER_EOF    = 2,
} mp4sys_importer_status;

/***************************************************************************
    ADTS importer
***************************************************************************/
#define MP4SYS_ADTS_FIXED_HEADER_LENGTH 4 /* this is partly a lie. actually 28 bits. */
#define MP4SYS_ADTS_BASIC_HEADER_LENGTH 7
#define MP4SYS_ADTS_MAX_FRAME_LENGTH ( ( 1 << 13 ) - 1 )
#define MP4SYS_ADTS_MAX_RAW_DATA_BLOCKS 4

typedef struct
{
    uint16_t syncword;                           /* 12; */
    uint8_t  ID;                                 /*  1; */
    uint8_t  layer;                              /*  2; */
    uint8_t  protection_absent;                  /*  1; */
    uint8_t  profile_ObjectType;                 /*  2; */
    uint8_t  sampling_frequency_index;           /*  4; */
//  uint8_t  private_bit;                        /*  1; we don't care. */
    uint8_t  channel_configuration;              /*  3; */
//  uint8_t  original_copy;                      /*  1; we don't care. */
//  uint8_t  home;                               /*  1; we don't care. */

} mp4sys_adts_fixed_header_t;

typedef struct
{
//  uint8_t  copyright_identification_bit;       /*  1; we don't care. */
//  uint8_t  copyright_identification_start;     /*  1; we don't care. */
    uint16_t frame_length;                       /* 13; */
//  uint16_t adts_buffer_fullness;               /* 11; we don't care. */
    uint8_t  number_of_raw_data_blocks_in_frame; /*  2; */
//  uint16_t adts_error_check;                                           /* we don't support */
//  uint16_t raw_data_block_position[MP4SYS_ADTS_MAX_RAW_DATA_BLOCKS-1]; /* we don't use this directly, and... */
    uint16_t raw_data_block_size[MP4SYS_ADTS_MAX_RAW_DATA_BLOCKS];       /* use this instead of above. */
//  uint16_t adts_header_error_check;                                    /* we don't support, actually crc_check within this */
//  uint16_t adts_raw_data_block_error_check[MP4SYS_ADTS_MAX_RAW_DATA_BLOCKS]; /* we don't support */
} mp4sys_adts_variable_header_t;

static void mp4sys_adts_parse_fixed_header( uint8_t* buf, mp4sys_adts_fixed_header_t* header )
{
    /* FIXME: should we rewrite these code using bitstream reader? */
    header->syncword                 = (buf[0] << 4) | (buf[1] >> 4);
    header->ID                       = (buf[1] >> 3) & 0x1;
    header->layer                    = (buf[1] >> 1) & 0x3;
    header->protection_absent        = buf[1] & 0x1;
    header->profile_ObjectType       = buf[2] >> 6;
    header->sampling_frequency_index = (buf[2] >> 2) & 0xF;
//  header->private_bit              = (buf[2] >> 1) & 0x1; /* we don't care currently. */
    header->channel_configuration    = ((buf[2] << 2) | (buf[3] >> 6)) & 0x07;
//  header->original_copy            = (buf[3] >> 5) & 0x1; /* we don't care currently. */
//  header->home                     = (buf[3] >> 4) & 0x1; /* we don't care currently. */
}

static int mp4sys_adts_check_fixed_header( mp4sys_adts_fixed_header_t* header )
{
    if( header->syncword != 0xFFF )              return -1;
//  if( header->ID != 0x0 )                      return -1; /* we don't care. */
    if( header->layer != 0x0 )                   return -1; /* must be 0b00 for any type of AAC */
//  if( header->protection_absent != 0x1 )       return -1; /* we don't care. */
    if( header->profile_ObjectType != 0x1 )      return -1; /* FIXME: 0b00=Main, 0b01=LC, 0b10=SSR, 0b11=LTP. */
    if( header->sampling_frequency_index > 0xB ) return -1; /* must not be > 0xB. */
    if( header->channel_configuration == 0x0 )   return -1; /* FIXME: we do not support 0b000 currently. */
    if( header->profile_ObjectType == 0x3 && header->ID != 0x0 ) return -1; /* LTP is valid only if ID==0. */
    return 0;
}

static int mp4sys_adts_parse_variable_header( FILE* stream, uint8_t* buf, unsigned int protection_absent, mp4sys_adts_variable_header_t* header )
{
    /* FIXME: should we rewrite these code using bitstream reader? */
//  header->copyright_identification_bit       = (buf[3] >> 3) & 0x1; /* we don't care. */
//  header->copyright_identification_start     = (buf[3] >> 2) & 0x1; /* we don't care. */
    header->frame_length                       = ((buf[3] << 11) | (buf[4] << 3) | (buf[5] >> 5)) & 0x1FFF ;
//  header->adts_buffer_fullness               = ((buf[5] << 6) | (buf[6] >> 2)) 0x7FF ;  /* we don't care. */
    header->number_of_raw_data_blocks_in_frame = buf[6] & 0x3;

    if( header->frame_length <= MP4SYS_ADTS_BASIC_HEADER_LENGTH + 2 * (protection_absent == 0) )
        return -1; /* easy error check */

    /* protection_absent and number_of_raw_data_blocks_in_frame relatives */

    uint8_t buf2[2];
    unsigned int number_of_blocks = header->number_of_raw_data_blocks_in_frame;
    if( number_of_blocks == 0 )
    {
        header->raw_data_block_size[0] = header->frame_length - MP4SYS_ADTS_BASIC_HEADER_LENGTH;
        /* skip adts_error_check() and subtract that from block_size */
        if( protection_absent == 0 )
        {
            header->raw_data_block_size[0] -= 2;
            if( fread( buf2, 1, 2, stream ) != 2 )
                return -1;
        }
        return 0;
    }

    /* now we have multiple raw_data_block()s, so evaluate adts_header_error_check() */

    uint16_t raw_data_block_position[MP4SYS_ADTS_MAX_RAW_DATA_BLOCKS];
    uint16_t first_offset = MP4SYS_ADTS_BASIC_HEADER_LENGTH;
    if( protection_absent == 0 )
    {
        /* process adts_header_error_check() */
        for( int i = 0 ; i < number_of_blocks ; i++ ) /* 1-based in the spec, but we use 0-based */
        {
            if( fread( buf2, 1, 2, stream ) != 2 )
                return -1;
            raw_data_block_position[i] = (buf2[0] << 8) | buf2[1];
        }
        /* skip crc_check in adts_header_error_check().
           Or might be sizeof( adts_error_check() ) if we share with the case number_of_raw_data_blocks_in_frame == 0 */
        if( fread( buf2, 1, 2, stream ) != 2 )
            return -1;
        first_offset += ( 2 * number_of_blocks ) + 2; /* according to above */
    }
    else
    {
        /*
         * NOTE: We never support the case where number_of_raw_data_blocks_in_frame != 0 && protection_absent != 0,
         * because we have to parse the raw AAC bitstream itself to find boundaries of raw_data_block()s in this case.
         * Which is to say, that braindamaged spec requires us (mp4 muxer) to decode AAC once to split frames.
         * L-SMASH is NOT AAC DECODER, so that we've just given up for this case.
         * This is ISO/IEC 13818-7's sin which defines ADTS format originally.
         */
        return -1;
    }

    /* convert raw_data_block_position --> raw_data_block_size */

    /* do conversion for first */
    header->raw_data_block_size[0] = raw_data_block_position[0] - first_offset;
    /* set dummy offset to tail for loop, do coversion for rest. */
    raw_data_block_position[number_of_blocks] = header->frame_length;
    for( int i = 1 ; i <= number_of_blocks ; i++ )
        header->raw_data_block_size[i] = raw_data_block_position[i] - raw_data_block_position[i-1];

    /* adjustment for adts_raw_data_block_error_check() */
    if( protection_absent == 0 && number_of_blocks != 0 )
        for( int i = 0 ; i <= number_of_blocks ; i++ )
            header->raw_data_block_size[i] -= 2;

    return 0;
}

static int mp4sys_adts_parse_headers( FILE* stream, uint8_t* buf, mp4sys_adts_fixed_header_t* header, mp4sys_adts_variable_header_t* variable_header )
{
    mp4sys_adts_parse_fixed_header( buf, header );
    if( mp4sys_adts_check_fixed_header( header ) )
        return -1;
    /* get payload length & skip extra(crc) header */
    return mp4sys_adts_parse_variable_header( stream, buf, header->protection_absent, variable_header );
}

static lsmash_audio_summary_t* mp4sys_adts_create_summary( mp4sys_adts_fixed_header_t* header )
{
    lsmash_audio_summary_t* summary = (lsmash_audio_summary_t*)malloc( sizeof(lsmash_audio_summary_t) );
    if( !summary )
        return NULL;
    memset( summary, 0, sizeof(lsmash_audio_summary_t) );
    summary->sample_type            = ISOM_CODEC_TYPE_MP4A_AUDIO;
    summary->object_type_indication = MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3;
    summary->stream_type            = MP4SYS_STREAM_TYPE_AudioStream;
    summary->max_au_length          = MP4SYS_ADTS_MAX_FRAME_LENGTH;
    summary->frequency              = mp4a_AAC_frequency_table[header->sampling_frequency_index][1];
    summary->channels               = header->channel_configuration + ( header->channel_configuration == 0x07 ); /* 0x07 means 7.1ch */
    summary->bit_depth              = 16;
    summary->samples_in_frame       = 1024;
    summary->aot                    = header->profile_ObjectType + MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN;
    summary->sbr_mode               = MP4A_AAC_SBR_NOT_SPECIFIED;
#if 0 /* FIXME: This is very unstable. Many players crash with this. */
    if( header->ID != 0 )
    {
        /*
         * NOTE: This ADTS seems of ISO/IEC 13818-7 (MPEG-2 AAC).
         * It has special object_type_indications, depending on it's profile (Legacy Interface).
         * If ADIF header is not available, it should not have decoder specific information, so AudioObjectType neither.
         * see ISO/IEC 14496-1, 8.6.7 DecoderSpecificInfo and 14496-3 Subpart 9: MPEG-1/2 Audio in MPEG-4.
         */
        summary->object_type_indication = header->profile_ObjectType + MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_Main_Profile;
        summary->aot                    = MP4A_AUDIO_OBJECT_TYPE_NULL;
        summary->asc                    = NULL;
        summary->asc_length             = 0;
        // summary->sbr_mode            = MP4A_AAC_SBR_NONE; /* MPEG-2 AAC should not be HE-AAC, but we forgive them. */
        return summary;
    }
#endif
    if( mp4sys_setup_AudioSpecificConfig( summary ) )
    {
        mp4sys_cleanup_audio_summary( summary );
        return NULL;
    }
    return summary;
}

typedef struct
{
    mp4sys_importer_status status;
    unsigned int raw_data_block_idx;
    mp4sys_adts_fixed_header_t header;
    mp4sys_adts_variable_header_t variable_header;
} mp4sys_adts_info_t;

static int mp4sys_adts_get_accessunit( mp4sys_importer_t* importer, uint32_t track_number , void* userbuf, uint32_t *size )
{
    debug_if( !importer || !importer->info || !userbuf || !size )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    mp4sys_adts_info_t* info = (mp4sys_adts_info_t*)importer->info;
    mp4sys_importer_status current_status = info->status;
    uint16_t raw_data_block_size = info->variable_header.raw_data_block_size[info->raw_data_block_idx];
    if( current_status == MP4SYS_IMPORTER_ERROR || *size < raw_data_block_size )
        return -1;
    if( current_status == MP4SYS_IMPORTER_EOF )
    {
        *size = 0;
        return 0;
    }
    if( current_status == MP4SYS_IMPORTER_CHANGE )
    {
        lsmash_audio_summary_t* summary = mp4sys_adts_create_summary( &info->header );
        if( !summary )
            return -1;
        lsmash_entry_t* entry = lsmash_get_entry( importer->summaries, track_number );
        if( !entry || !entry->data )
            return -1;
        mp4sys_cleanup_audio_summary( entry->data );
        entry->data = summary;
    }

    /* read a raw_data_block(), typically == payload of a ADTS frame */
    if( fread( userbuf, 1, raw_data_block_size, importer->stream ) != raw_data_block_size )
    {
        info->status = MP4SYS_IMPORTER_ERROR;
        return -1;
    }
    *size = raw_data_block_size;

    /* now we succeeded to read current frame, so "return" takes 0 always below. */

    /* skip adts_raw_data_block_error_check() */
    if( info->header.protection_absent == 0
        && info->variable_header.number_of_raw_data_blocks_in_frame != 0
        && fread( userbuf, 1, 2, importer->stream ) != 2 )
    {
        info->status = MP4SYS_IMPORTER_ERROR;
        return 0;
    }
    /* current adts_frame() has any more raw_data_block()? */
    if( info->raw_data_block_idx < info->variable_header.number_of_raw_data_blocks_in_frame )
    {
        info->raw_data_block_idx++;
        info->status = MP4SYS_IMPORTER_OK;
        return 0;
    }
    info->raw_data_block_idx = 0;

    /* preparation for next frame */

    uint8_t buf[MP4SYS_ADTS_MAX_FRAME_LENGTH];
    size_t ret = fread( buf, 1, MP4SYS_ADTS_BASIC_HEADER_LENGTH, importer->stream );
    if( ret == 0 )
    {
        info->status = MP4SYS_IMPORTER_EOF;
        return 0;
    }
    if( ret != MP4SYS_ADTS_BASIC_HEADER_LENGTH )
    {
        info->status = MP4SYS_IMPORTER_ERROR;
        return 0;
    }
    /*
     * NOTE: About the spec of ADTS headers.
     * By the spec definition, ADTS's fixed header cannot change in the middle of stream.
     * But spec of MP4 allows that a stream(track) changes its properties in the middle of it.
     */
    /*
     * NOTE: About detailed check for ADTS headers.
     * We do not ommit detailed check for fixed header by simply testing bits' identification,
     * because there're some flags which does not matter to audio_summary (so AudioSpecificConfig neither)
     * so that we can take them as no change and never make new ObjectDescriptor.
     * I know that can be done with/by bitmask also and that should be fast, but L-SMASH project prefers
     * even foolishly straightforward way.
     */
    /*
     * NOTE: About our reading algorithm for ADTS.
     * It's rather simple if we retrieve payload of ADTS (i.e. raw AAC frame) at the same time to
     * retrieve headers.
     * But then we have to cache and memcpy every frame so that it requires more clocks and memory.
     * To avoid them, I adopted this separate retrieving method.
     */
    mp4sys_adts_fixed_header_t header = {0};
    mp4sys_adts_variable_header_t variable_header = {0};
    if( mp4sys_adts_parse_headers( importer->stream, buf, &header, &variable_header ) )
    {
        info->status = MP4SYS_IMPORTER_ERROR;
        return 0;
    }
    info->variable_header = variable_header;

    /*
     * NOTE: About our support for change(s) of properties within an ADTS stream.
     * We have to modify these conditions depending on the features we support.
     * For example, if we support copyright_identification_* in any way within any feature
     * defined by/in any specs, such as ISO/IEC 14496-1 (MPEG-4 Systems), like...
     * "8.3 Intellectual Property Management and Protection (IPMP)", or something similar,
     * we have to check copyright_identification_* and treat them in audio_summary.
     * "Change(s)" may result in MP4SYS_IMPORTER_ERROR or MP4SYS_IMPORTER_CHANGE
     * depending on the features we support, and what the spec allows.
     * Sometimes the "change(s)" can be allowed, while sometimes they're forbidden.
     */
    /* currently UNsupported "change(s)". */
    if( info->header.profile_ObjectType != header.profile_ObjectType /* currently unsupported. */
        || info->header.ID != header.ID /* In strict, this means change of object_type_indication. */
        || info->header.sampling_frequency_index != header.sampling_frequency_index ) /* This may change timebase. */
    {
        info->status = MP4SYS_IMPORTER_ERROR;
        return 0;
    }
    /* currently supported "change(s)". */
    if( info->header.channel_configuration != header.channel_configuration )
    {
        /*
         * FIXME: About conditions of VALID "change(s)".
         * we have to check whether any "change(s)" affect to audioProfileLevelIndication
         * in InitialObjectDescriptor (MP4_IOD) or not.
         * If another type or upper level is required by the change(s), that is forbidden.
         * Because ObjectDescriptor does not have audioProfileLevelIndication,
         * so that it seems impossible to change audioProfileLevelIndication in the middle of the stream.
         * Note also any other properties, such as AudioObjectType, object_type_indication.
         */
        /*
         * NOTE: updating summary must be done on next call,
         * because user may retrieve summary right after this function call of this time,
         * and that should be of current, before change, one.
         */
        info->header = header;
        info->status = MP4SYS_IMPORTER_CHANGE;
        return 0;
    }
    /* no change which matters to mp4 muxing was found */
    info->status = MP4SYS_IMPORTER_OK;
    return 0;
}

static void mp4sys_adts_cleanup( mp4sys_importer_t* importer )
{
    debug_if( importer && importer->info )
        free( importer->info );
}

/* returns 0 if it seems adts. */
static int mp4sys_adts_probe( mp4sys_importer_t* importer )
{
    uint8_t buf[MP4SYS_ADTS_MAX_FRAME_LENGTH];
    if( fread( buf, 1, MP4SYS_ADTS_BASIC_HEADER_LENGTH, importer->stream ) != MP4SYS_ADTS_BASIC_HEADER_LENGTH )
        return -1;

    mp4sys_adts_fixed_header_t header = {0};
    mp4sys_adts_variable_header_t variable_header = {0};
    if( mp4sys_adts_parse_headers( importer->stream, buf, &header, &variable_header ) )
        return -1;

    /* now the stream seems valid ADTS */

    lsmash_audio_summary_t* summary = mp4sys_adts_create_summary( &header );
    if( !summary )
        return -1;

    /* importer status */
    mp4sys_adts_info_t* info = malloc( sizeof(mp4sys_adts_info_t) );
    if( !info )
    {
        mp4sys_cleanup_audio_summary( summary );
        return -1;
    }
    memset( info, 0, sizeof(mp4sys_adts_info_t) );
    info->status = MP4SYS_IMPORTER_OK;
    info->raw_data_block_idx = 0;
    info->header = header;
    info->variable_header = variable_header;

    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        free( info );
        mp4sys_cleanup_audio_summary( summary );
        return -1;
    }
    importer->info = info;

    return 0;
}

const static mp4sys_importer_functions mp4sys_adts_importer = {
    "adts",
    1,
    mp4sys_adts_probe,
    mp4sys_adts_get_accessunit,
    mp4sys_adts_cleanup
};

/***************************************************************************
    mp3 (Legacy Interface) importer
***************************************************************************/

static void mp4sys_mp3_cleanup( mp4sys_importer_t* importer )
{
    debug_if( importer && importer->info )
        free( importer->info );
}

typedef struct
{
    uint16_t syncword;           /* <12> */
    uint8_t  ID;                 /* <1> */
    uint8_t  layer;              /* <2> */
//  uint8_t  protection_bit;     /* <1> don't care. */
    uint8_t  bitrate_index;      /* <4> */
    uint8_t  sampling_frequency; /* <2> */
    uint8_t  padding_bit;        /* <1> */
//  uint8_t  private_bit;        /* <1> don't care. */
    uint8_t  mode;               /* <2> */
//  uint8_t  mode_extension;     /* <2> don't care. */
//  uint8_t  copyright;          /* <1> don't care. */
//  uint8_t  original_copy;      /* <1> don't care. */
    uint8_t  emphasis;           /* <2> for error check only. */

} mp4sys_mp3_header_t;

static int mp4sys_mp3_parse_header( uint8_t* buf, mp4sys_mp3_header_t* header )
{
    /* FIXME: should we rewrite these code using bitstream reader? */
    uint32_t data = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    header->syncword           = (data >> 20) & 0xFFF; /* NOTE: don't consider what is called MPEG2.5, which last bit is 0. */
    header->ID                 = (data >> 19) & 0x1;
    header->layer              = (data >> 17) & 0x3;
//  header->protection_bit     = (data >> 16) & 0x1; /* don't care. */
    header->bitrate_index      = (data >> 12) & 0xF;
    header->sampling_frequency = (data >> 10) & 0x3;
    header->padding_bit        = (data >>  9) & 0x1;
//  header->private_bit        = (data >>  8) & 0x1; /* don't care. */
    header->mode               = (data >>  6) & 0x3;
//  header->mode_extension     = (data >>  4) & 0x3;
//  header->copyright          = (data >>  3) & 0x1; /* don't care. */
//  header->original_copy      = (data >>  2) & 0x1; /* don't care. */
    header->emphasis           = data         & 0x3; /* for error check only. */

    if( header->syncword != 0xFFF )                                    return -1;
    if( header->layer == 0x0 )                                         return -1;
    if( header->bitrate_index == 0x0 || header->bitrate_index == 0xF ) return -1; /* FIXME: "free" bitrate is unsupported currently. */
    if( header->sampling_frequency == 0x3)                             return -1;
    if( header->emphasis == 0x2)                                       return -1;
    return 0;
}

#define MP4SYS_MP3_MAX_FRAME_LENGTH (1152*(16/8)*2)
#define MP4SYS_MP3_HEADER_LENGTH    4
#define MP4SYS_MODE_IS_2CH( mode )  (!!~(mode))
#define MP4SYS_LAYER_I              0x3

static const uint32_t mp4sys_mp3_frequency_tbl[2][3] = {
    { 22050, 24000, 16000 }, /* MPEG-2 BC audio */
    { 44100, 48000, 32000 }  /* MPEG-1 audio */
};

static lsmash_audio_summary_t* mp4sys_mp3_create_summary( mp4sys_mp3_header_t* header, int legacy_mode )
{
    lsmash_audio_summary_t* summary = (lsmash_audio_summary_t*)malloc( sizeof(lsmash_audio_summary_t) );
    if( !summary )
        return NULL;
    memset( summary, 0, sizeof(lsmash_audio_summary_t) );
    summary->sample_type            = ISOM_CODEC_TYPE_MP4A_AUDIO;
    summary->object_type_indication = header->ID ? MP4SYS_OBJECT_TYPE_Audio_ISO_11172_3 : MP4SYS_OBJECT_TYPE_Audio_ISO_13818_3;
    summary->stream_type            = MP4SYS_STREAM_TYPE_AudioStream;
    summary->max_au_length          = MP4SYS_MP3_MAX_FRAME_LENGTH;
    summary->frequency              = mp4sys_mp3_frequency_tbl[header->ID][header->sampling_frequency];
    summary->channels               = MP4SYS_MODE_IS_2CH( header->mode ) + 1;
    summary->bit_depth              = 16;
    summary->samples_in_frame       = header->layer == MP4SYS_LAYER_I ? 384 : 1152;
    summary->aot                    = MP4A_AUDIO_OBJECT_TYPE_Layer_1 + (MP4SYS_LAYER_I - header->layer); /* no effect with Legacy Interface. */
    summary->sbr_mode               = MP4A_AAC_SBR_NOT_SPECIFIED; /* no effect */
    summary->exdata                 = NULL;
    summary->exdata_length          = 0;
#if 0 /* FIXME: This is very unstable. Many players crash with this. */
    if( !legacy_mode )
    {
        summary->object_type_indication = MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3;
        if( mp4sys_setup_AudioSpecificConfig( summary ) )
        {
            mp4sys_cleanup_audio_summary( summary );
            return NULL;
        }
    }
#endif
    return summary;
}

typedef struct
{
    mp4sys_importer_status status;
    mp4sys_mp3_header_t    header;
    uint8_t                raw_header[MP4SYS_MP3_HEADER_LENGTH];
} mp4sys_mp3_info_t;

static int mp4sys_mp3_get_accessunit( mp4sys_importer_t* importer, uint32_t track_number , void* userbuf, uint32_t *size )
{
    debug_if( !importer || !importer->info || !userbuf || !size )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    mp4sys_mp3_info_t* info = (mp4sys_mp3_info_t*)importer->info;
    mp4sys_mp3_header_t* header = (mp4sys_mp3_header_t*)&info->header;
    mp4sys_importer_status current_status = info->status;

    const uint32_t bitrate_tbl[2][3][16] = {
        {   /* MPEG-2 BC audio */
            { 0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }, /* Layer III */
            { 0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }, /* Layer II  */
            { 0, 32, 48, 56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, 0 }  /* Layer I   */
        },
        {   /* MPEG-1 audio */
            { 0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 0 }, /* Layer III */
            { 0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, 0 }, /* Layer II  */
            { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0 }  /* Layer I   */
        }
    };
    uint32_t bitrate = bitrate_tbl[header->ID][header->layer-1][header->bitrate_index];
    uint32_t frequency = mp4sys_mp3_frequency_tbl[header->ID][header->sampling_frequency];
    debug_if( bitrate == 0 || frequency == 0 )
        return -1;
    uint32_t frame_size;
    if( header->layer == MP4SYS_LAYER_I )
    {
        /* mp1's 'slot' is 4 bytes unit. see 11172-3, 2.4.2.1 Audio Sequence General. */
        frame_size = ( 12 * 1000 * bitrate / frequency + header->padding_bit ) * 4;
    }
    else
    {
        /* mp2/3's 'slot' is 1 bytes unit. */
        frame_size = 144 * 1000 * bitrate / frequency + header->padding_bit;
    }

    if( current_status == MP4SYS_IMPORTER_ERROR || frame_size <= 4 || *size < frame_size )
        return -1;
    if( current_status == MP4SYS_IMPORTER_EOF )
    {
        *size = 0;
        return 0;
    }
    if( current_status == MP4SYS_IMPORTER_CHANGE )
    {
        lsmash_audio_summary_t* summary = mp4sys_mp3_create_summary( header, 1 ); /* FIXME: use legacy mode. */
        if( !summary )
            return -1;
        lsmash_entry_t* entry = lsmash_get_entry( importer->summaries, track_number );
        if( !entry || !entry->data )
            return -1;
        mp4sys_cleanup_audio_summary( entry->data );
        entry->data = summary;
    }
    /* read a frame's data. */
    memcpy( userbuf, info->raw_header, MP4SYS_MP3_HEADER_LENGTH );
    frame_size -= MP4SYS_MP3_HEADER_LENGTH;
    if( fread( ((uint8_t*)userbuf)+MP4SYS_MP3_HEADER_LENGTH, 1, frame_size, importer->stream ) != frame_size )
    {
        info->status = MP4SYS_IMPORTER_ERROR;
        return -1;
    }
    *size = MP4SYS_MP3_HEADER_LENGTH + frame_size;

    /* now we succeeded to read current frame, so "return" takes 0 always below. */
    /* preparation for next frame */

    uint8_t buf[MP4SYS_MP3_HEADER_LENGTH];
    size_t ret = fread( buf, 1, MP4SYS_MP3_HEADER_LENGTH, importer->stream );
    if( ret == 0 )
    {
        info->status = MP4SYS_IMPORTER_EOF;
        return 0;
    }
    if( ret == 1 && *buf == 0x00 )
    {
        /* NOTE: ugly hack for mp1 stream created with SCMPX. */
        info->status = MP4SYS_IMPORTER_EOF;
        return 0;
    }
    if( ret != MP4SYS_MP3_HEADER_LENGTH )
    {
        info->status = MP4SYS_IMPORTER_ERROR;
        return 0;
    }

    mp4sys_mp3_header_t new_header = {0};
    if( mp4sys_mp3_parse_header( buf, &new_header ) )
    {
        info->status = MP4SYS_IMPORTER_ERROR;
        return 0;
    }
    memcpy( info->raw_header, buf, MP4SYS_MP3_HEADER_LENGTH );

    /* currently UNsupported "change(s)". */
    if( header->layer != new_header.layer /* This means change of object_type_indication with Legacy Interface. */
        || header->sampling_frequency != new_header.sampling_frequency ) /* This may change timescale. */
    {
        info->status = MP4SYS_IMPORTER_ERROR;
        return 0;
    }

    /* currently supported "change(s)". */
    if( MP4SYS_MODE_IS_2CH( header->mode ) != MP4SYS_MODE_IS_2CH( new_header.mode ) )
        info->status = MP4SYS_IMPORTER_CHANGE;
    else
        info->status = MP4SYS_IMPORTER_OK; /* no change which matters to mp4 muxing was found */
    info->header = new_header;
    return 0;
}

static int mp4sys_mp3_probe( mp4sys_importer_t* importer )
{
    uint8_t buf[MP4SYS_MP3_HEADER_LENGTH];
    if( fread( buf, 1, MP4SYS_MP3_HEADER_LENGTH, importer->stream ) != MP4SYS_MP3_HEADER_LENGTH )
        return -1;

    mp4sys_mp3_header_t header = {0};
    if( mp4sys_mp3_parse_header( buf, &header ) )
        return -1;

    /* now the stream seems valid mp3 */

    lsmash_audio_summary_t* summary = mp4sys_mp3_create_summary( &header, 1 ); /* FIXME: use legacy mode. */
    if( !summary )
        return -1;

    /* importer status */
    mp4sys_mp3_info_t* info = malloc( sizeof(mp4sys_mp3_info_t) );
    if( !info )
    {
        mp4sys_cleanup_audio_summary( summary );
        return -1;
    }
    memset( info, 0, sizeof(mp4sys_mp3_info_t) );
    info->status = MP4SYS_IMPORTER_OK;
    info->header = header;
    memcpy( info->raw_header, buf, MP4SYS_MP3_HEADER_LENGTH );

    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        free( info );
        mp4sys_cleanup_audio_summary( summary );
        return -1;
    }
    importer->info = info;

    return 0;
}

const static mp4sys_importer_functions mp4sys_mp3_importer = {
    "MPEG-1/2BC_Audio_Legacy",
    1,
    mp4sys_mp3_probe,
    mp4sys_mp3_get_accessunit,
    mp4sys_mp3_cleanup
};

/***************************************************************************
    AMR-NB/WB storage format importer
    http://www.ietf.org/rfc/rfc3267.txt (Obsoleted)
    http://www.ietf.org/rfc/rfc4867.txt
***************************************************************************/
static void mp4sys_amr_cleanup( mp4sys_importer_t* importer )
{
    debug_if( importer && importer->info )
        free( importer->info );
}

static int mp4sys_amr_get_accessunit( mp4sys_importer_t* importer, uint32_t track_number , void* userbuf, uint32_t *size )
{
    debug_if( !importer || !importer->info || !userbuf || !size )
        return -1;
    if( track_number != 1 )
        return -1;
    uint8_t wb = *(uint8_t*)importer->info;

    uint8_t* buf = userbuf;
    if( fread( buf, 1, 1, importer->stream ) == 0 )
    {
        /* EOF */
        *size = 0;
        return 0;
    }
    uint8_t FT = (*buf >> 3) & 0x0F;

    /* AMR-NB has varieties of frame-size table like this. so I'm not sure yet. */
    const int frame_size[2][16] = {
        { 13, 14, 16, 18, 20, 21, 27, 32,  5, 5, 5, 5, 0, 0, 0, 1 },
        { 18, 24, 33, 37, 41, 47, 51, 59, 61, 6, 6, 0, 0, 0, 1, 1 }
    };
    int read_size = frame_size[wb][FT];
    if( read_size == 0 || *size < read_size-- )
        return -1;
    if( read_size == 0 )
    {
        *size = 1;
        return 0;
    }
    if( fread( buf+1, 1, read_size, importer->stream ) != read_size )
        return -1;
    *size = read_size + 1;
    return 0;
}

#define MP4SYS_DAMR_LENGTH 17

int mp4sys_amr_create_damr( lsmash_audio_summary_t *summary )
{
    lsmash_bs_t* bs = lsmash_bs_create( NULL ); /* no file writing */
    if( !bs )
        return -1;
    lsmash_bs_put_be32( bs, MP4SYS_DAMR_LENGTH );
    lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_DAMR );
    /* NOTE: These are specific to each codec vendor, but we're surely not a vendor.
              Using dummy data. */
    lsmash_bs_put_be32( bs, 0x20202020 ); /* vendor */
    lsmash_bs_put_byte( bs, 0 );          /* decoder_version */

    /* NOTE: Using safe value for these settings, maybe sub-optimal. */
    lsmash_bs_put_be16( bs, 0x83FF );     /* mode_set, represents for possibly existing frame-type (0x83FF == all). */
    lsmash_bs_put_byte( bs, 1 );          /* mode_change_period */
    lsmash_bs_put_byte( bs, 1 );          /* frames_per_sample */

    if( summary->exdata )
        free( summary->exdata );
    summary->exdata = lsmash_bs_export_data( bs, &summary->exdata_length );
    lsmash_bs_cleanup( bs );
    if( !summary->exdata )
        return -1;
    summary->exdata_length = MP4SYS_DAMR_LENGTH;
    return 0;
}

#define MP4SYS_AMR_STORAGE_MAGIC_LENGTH 6
#define MP4SYS_AMRWB_EX_MAGIC_LENGTH 3

static int mp4sys_amr_probe( mp4sys_importer_t* importer )
{
    uint8_t buf[MP4SYS_AMR_STORAGE_MAGIC_LENGTH];
    uint8_t wb = 0;
    if( fread( buf, 1, MP4SYS_AMR_STORAGE_MAGIC_LENGTH, importer->stream ) != MP4SYS_AMR_STORAGE_MAGIC_LENGTH )
        return -1;
    if( memcmp( buf, "#!AMR", MP4SYS_AMR_STORAGE_MAGIC_LENGTH-1 ) )
        return -1;
    if( buf[MP4SYS_AMR_STORAGE_MAGIC_LENGTH-1] != '\n' )
    {
        if( buf[MP4SYS_AMR_STORAGE_MAGIC_LENGTH-1] != '-' )
            return -1;
        if( fread( buf, 1, MP4SYS_AMRWB_EX_MAGIC_LENGTH, importer->stream ) != MP4SYS_AMRWB_EX_MAGIC_LENGTH )
            return -1;
        if( memcmp( buf, "WB\n", MP4SYS_AMRWB_EX_MAGIC_LENGTH ) )
            return -1;
        wb = 1;
    }
    lsmash_audio_summary_t* summary = malloc( sizeof(lsmash_audio_summary_t) );
    memset( summary, 0, sizeof(lsmash_audio_summary_t) );
    if( !summary )
        return -1;
    summary->sample_type            = wb ? ISOM_CODEC_TYPE_SAWB_AUDIO : ISOM_CODEC_TYPE_SAMR_AUDIO;
    summary->object_type_indication = MP4SYS_OBJECT_TYPE_NONE; /* AMR is not defined in ISO/IEC 14496-3 */
    summary->stream_type            = MP4SYS_STREAM_TYPE_AudioStream;
    summary->exdata                 = NULL; /* to be set in mp4sys_amrnb_create_damr() */
    summary->exdata_length          = 0;    /* to be set in mp4sys_amrnb_create_damr() */
    summary->max_au_length          = wb ? 61 : 32;
    summary->aot                    = MP4A_AUDIO_OBJECT_TYPE_NULL; /* no effect */
    summary->frequency              = (8000 << wb);
    summary->channels               = 1;
    summary->bit_depth              = 16;
    summary->samples_in_frame       = (160 << wb);
    summary->sbr_mode               = MP4A_AAC_SBR_NOT_SPECIFIED; /* no effect */
    importer->info = malloc( sizeof(uint8_t) );
    if( !importer->info )
    {
        mp4sys_cleanup_audio_summary( summary );
        return -1;
    }
    *(uint8_t*)importer->info = wb;
    if( mp4sys_amr_create_damr( summary ) || lsmash_add_entry( importer->summaries, summary ) )
    {
        free( importer->info );
        importer->info = NULL;
        mp4sys_cleanup_audio_summary( summary );
        return -1;
    }
    return 0;
}

const static mp4sys_importer_functions mp4sys_amr_importer = {
    "amr",
    1,
    mp4sys_amr_probe,
    mp4sys_amr_get_accessunit,
    mp4sys_amr_cleanup
};

/* data_length must be size of data that is available. */
int mp4sys_create_dac3_from_syncframe( lsmash_audio_summary_t *summary, uint8_t *data, uint32_t data_length )
{
    /* Requires the following 7 bytes.
     * syncword                                         : 16
     * crc1                                             : 16
     * fscod                                            : 2
     * frmsizecod                                       : 6
     * bsid                                             : 5
     * bsmod                                            : 3
     * acmod                                            : 3
     * if((acmod & 0x01) && (acmod != 0x01)) cmixlev    : 2
     * if(acmod & 0x04) surmixlev                       : 2
     * if(acmod == 0x02) dsurmod                        : 2
     * lfeon                                            : 1
     */
    if( data_length < 7 )
        return -1;
    /* check syncword */
    if( data[0] != 0x0b || data[1] != 0x77 )
        return -1;
    /* get necessary data for AC3SpecificBox */
    uint32_t fscod, bsid, bsmod, acmod, lfeon, frmsizecod;
    fscod      = data[4] >> 6;
    frmsizecod = data[4] & 0x3f;
    bsid       = data[5] >> 3;
    bsmod      = data[5] & 0x07;
    acmod      = data[6] >> 5;
    if( acmod == 0x02 )
        lfeon  = data[6] >> 2;      /* skip dsurmod */
    else
    {
        if( (acmod & 0x01) && acmod != 0x01 && (acmod & 0x04) )
            lfeon = data[6];        /* skip cmixlev and surmixlev */
        else if( ((acmod & 0x01) && acmod != 0x01) || (acmod & 0x04) )
            lfeon = data[6] >> 2;   /* skip cmixlev or surmixlev */
        else
            lfeon = data[6] >> 4;
    }
    lfeon &= 0x01;
    /* create AC3SpecificBox */
    lsmash_bits_t *bits = lsmash_bits_adhoc_create();
    lsmash_bits_put( bits, 11, 32 );
    lsmash_bits_put( bits, ISOM_BOX_TYPE_DAC3, 32 );
    lsmash_bits_put( bits, fscod, 2 );
    lsmash_bits_put( bits, bsid, 5 );
    lsmash_bits_put( bits, bsmod, 3 );
    lsmash_bits_put( bits, acmod, 3 );
    lsmash_bits_put( bits, lfeon, 1 );
    lsmash_bits_put( bits, frmsizecod >> 1, 5 );
    lsmash_bits_put( bits, 0, 5 );
    if( summary->exdata )
        free( summary->exdata );
    summary->exdata = lsmash_bits_export_data( bits, &summary->exdata_length );
    lsmash_bits_adhoc_cleanup( bits );
    return 0;
}

/***************************************************************************
    importer public interfaces
***************************************************************************/


/******** importer listing table ********/
const static mp4sys_importer_functions* mp4sys_importer_tbl[] = {
    &mp4sys_adts_importer,
    &mp4sys_mp3_importer,
    &mp4sys_amr_importer,
    NULL,
};

/******** importer public functions ********/

void mp4sys_importer_close( mp4sys_importer_t* importer )
{
    if( !importer )
        return;
    if( !importer->is_stdin && importer->stream )
        fclose( importer->stream );
    if( importer->funcs.cleanup )
        importer->funcs.cleanup( importer );
    /* FIXME: To be extended to support visual summary. */
    lsmash_remove_list( importer->summaries, mp4sys_cleanup_audio_summary );
    free( importer );
}

mp4sys_importer_t* mp4sys_importer_open( const char* identifier, const char* format )
{
    if( identifier == NULL )
        return NULL;

    int auto_detect = ( format == NULL || !strcmp( format, "auto" ) );
    mp4sys_importer_t* importer = (mp4sys_importer_t*)malloc( sizeof(mp4sys_importer_t) );
    if( !importer )
        return NULL;
    memset( importer, 0, sizeof(mp4sys_importer_t) );

    if( !strcmp( identifier, "-" ) )
    {
        /* special treatment for stdin */
        if( auto_detect )
        {
            free( importer );
            return NULL;
        }
        importer->stream = stdin;
        importer->is_stdin = 1;
    }
    else if( (importer->stream = fopen( identifier, "rb" )) == NULL )
    {
        mp4sys_importer_close( importer );
        return NULL;
    }
    importer->summaries = lsmash_create_entry_list();
    if( !importer->summaries )
    {
        mp4sys_importer_close( importer );
        return NULL;
    }
    /* find importer */
    const mp4sys_importer_functions* funcs;
    if( auto_detect )
    {
        /* just rely on detector. */
        for( int i = 0; (funcs = mp4sys_importer_tbl[i]) != NULL; i++ )
        {
            if( !funcs->detectable )
                continue;
            if( !funcs->probe( importer ) || mp4sys_fseek( importer->stream, 0, SEEK_SET ) )
                break;
        }
    }
    else
    {
        /* needs name matching. */
        for( int i = 0; (funcs = mp4sys_importer_tbl[i]) != NULL; i++ )
        {
            if( strcmp( funcs->name, format ) )
                continue;
            if( funcs->probe( importer ) )
                funcs = NULL;
            break;
        }
    }
    if( !funcs )
    {
        mp4sys_importer_close( importer );
        return NULL;
    }
    importer->funcs = *funcs;
    return importer;
}

/* 0 if success, positive if changed, negative if failed */
int mp4sys_importer_get_access_unit( mp4sys_importer_t* importer, uint32_t track_number, void* buf, uint32_t* size )
{
    if( !importer || !importer->funcs.get_accessunit || !buf || !size || *size == 0 )
        return -1;
    return importer->funcs.get_accessunit( importer, track_number, buf, size );
}

lsmash_audio_summary_t* mp4sys_duplicate_audio_summary( mp4sys_importer_t* importer, uint32_t track_number )
{
    if( !importer )
        return NULL;
    lsmash_audio_summary_t* summary = (lsmash_audio_summary_t*)malloc( sizeof(lsmash_audio_summary_t) );
    if( !summary )
        return NULL;
    lsmash_audio_summary_t* src_summary = lsmash_get_entry_data( importer->summaries, track_number );
    if( !src_summary )
        return NULL;
    memcpy( summary, src_summary, sizeof(lsmash_audio_summary_t) );
    summary->exdata = NULL;
    summary->exdata_length = 0;
    if( mp4sys_summary_add_exdata( summary, src_summary->exdata, src_summary->exdata_length ) )
    {
        free( summary );
        return NULL;
    }
    return summary;
}
