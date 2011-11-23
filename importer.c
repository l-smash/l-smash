/*****************************************************************************
 * importer.c:
 *****************************************************************************
 * Copyright (C) 2010-2011 L-SMASH project
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

#include "internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

#define LSMASH_IMPORTER_INTERNAL
#include "importer.h"

#include "mp4a.h"
#include "box.h"

/***************************************************************************
    importer framework
***************************************************************************/
struct mp4sys_importer_tag;

typedef void     ( *mp4sys_importer_cleanup )          ( struct mp4sys_importer_tag * );
typedef int      ( *mp4sys_importer_get_accessunit )   ( struct mp4sys_importer_tag *, uint32_t, lsmash_sample_t * );
typedef int      ( *mp4sys_importer_probe )            ( struct mp4sys_importer_tag * );
typedef uint32_t ( *mp4sys_importer_get_last_duration )( struct mp4sys_importer_tag *, uint32_t );

typedef struct
{
    const char*                       name;
    int                               detectable;
    mp4sys_importer_probe             probe;
    mp4sys_importer_get_accessunit    get_accessunit;
    mp4sys_importer_get_last_duration get_last_delta;
    mp4sys_importer_cleanup           cleanup;
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

static lsmash_audio_summary_t *mp4sys_adts_create_summary( mp4sys_adts_fixed_header_t *header )
{
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( MP4SYS_STREAM_TYPE_AudioStream );
    if( !summary )
        return NULL;
    summary->sample_type            = ISOM_CODEC_TYPE_MP4A_AUDIO;
    summary->object_type_indication = MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3;
    summary->max_au_length          = MP4SYS_ADTS_MAX_FRAME_LENGTH;
    summary->frequency              = mp4a_sampling_frequency_table[header->sampling_frequency_index][1];
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
         * see ISO/IEC 14496-1, DecoderSpecificInfo and 14496-3 Subpart 9: MPEG-1/2 Audio in MPEG-4.
         */
        summary->object_type_indication = header->profile_ObjectType + MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_Main_Profile;
        summary->aot                    = MP4A_AUDIO_OBJECT_TYPE_NULL;
        summary->asc                    = NULL;
        summary->asc_length             = 0;
        // summary->sbr_mode            = MP4A_AAC_SBR_NONE; /* MPEG-2 AAC should not be HE-AAC, but we forgive them. */
        return summary;
    }
#endif
    if( lsmash_setup_AudioSpecificConfig( summary ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
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
    uint32_t samples_in_frame;
    uint32_t au_number;
} mp4sys_adts_info_t;

static int mp4sys_adts_get_accessunit( mp4sys_importer_t* importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    mp4sys_adts_info_t* info = (mp4sys_adts_info_t*)importer->info;
    mp4sys_importer_status current_status = info->status;
    uint16_t raw_data_block_size = info->variable_header.raw_data_block_size[info->raw_data_block_idx];
    if( current_status == MP4SYS_IMPORTER_ERROR || buffered_sample->length < raw_data_block_size )
        return -1;
    if( current_status == MP4SYS_IMPORTER_EOF )
    {
        buffered_sample->length = 0;
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
        lsmash_cleanup_summary( entry->data );
        entry->data = summary;
        info->samples_in_frame = summary->samples_in_frame;
    }

    /* read a raw_data_block(), typically == payload of a ADTS frame */
    if( fread( buffered_sample->data, 1, raw_data_block_size, importer->stream ) != raw_data_block_size )
    {
        info->status = MP4SYS_IMPORTER_ERROR;
        return -1;
    }
    buffered_sample->length = raw_data_block_size;
    buffered_sample->dts = info->au_number++ * info->samples_in_frame;
    buffered_sample->cts = buffered_sample->dts;
    buffered_sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
    buffered_sample->prop.pre_roll.distance = 1;    /* MDCT */

    /* now we succeeded to read current frame, so "return" takes 0 always below. */

    /* skip adts_raw_data_block_error_check() */
    if( info->header.protection_absent == 0
        && info->variable_header.number_of_raw_data_blocks_in_frame != 0
        && fread( buffered_sample->data, 1, 2, importer->stream ) != 2 )
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
    mp4sys_adts_info_t* info = lsmash_malloc_zero( sizeof(mp4sys_adts_info_t) );
    if( !info )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    info->status = MP4SYS_IMPORTER_OK;
    info->raw_data_block_idx = 0;
    info->header = header;
    info->variable_header = variable_header;
    info->samples_in_frame = summary->samples_in_frame;

    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        free( info );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    importer->info = info;

    return 0;
}

static uint32_t mp4sys_adts_get_last_delta( mp4sys_importer_t* importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    mp4sys_adts_info_t *info = (mp4sys_adts_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != MP4SYS_IMPORTER_EOF )
        return 0;
    return info->samples_in_frame;
}

const static mp4sys_importer_functions mp4sys_adts_importer =
{
    "adts",
    1,
    mp4sys_adts_probe,
    mp4sys_adts_get_accessunit,
    mp4sys_adts_get_last_delta,
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
#define MP4SYS_LAYER_III            0x1
#define MP4SYS_LAYER_I              0x3

static const uint32_t mp4sys_mp3_frequency_tbl[2][3] = {
    { 22050, 24000, 16000 }, /* MPEG-2 BC audio */
    { 44100, 48000, 32000 }  /* MPEG-1 audio */
};

static lsmash_audio_summary_t *mp4sys_mp3_create_summary( mp4sys_mp3_header_t *header, int legacy_mode )
{
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( MP4SYS_STREAM_TYPE_AudioStream );
    if( !summary )
        return NULL;
    summary->sample_type            = ISOM_CODEC_TYPE_MP4A_AUDIO;
    summary->object_type_indication = header->ID ? MP4SYS_OBJECT_TYPE_Audio_ISO_11172_3 : MP4SYS_OBJECT_TYPE_Audio_ISO_13818_3;
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
        if( lsmash_setup_AudioSpecificConfig( summary ) )
        {
            lsmash_cleanup_summary( summary );
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
    uint32_t               samples_in_frame;
    uint32_t               au_number;
} mp4sys_mp3_info_t;

static int mp4sys_mp3_get_accessunit( mp4sys_importer_t* importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
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
        /* mp1's 'slot' is 4 bytes unit. see 11172-3, Audio Sequence General. */
        frame_size = ( 12 * 1000 * bitrate / frequency + header->padding_bit ) * 4;
    }
    else
    {
        /* mp2/3's 'slot' is 1 bytes unit. */
        frame_size = 144 * 1000 * bitrate / frequency + header->padding_bit;
    }

    if( current_status == MP4SYS_IMPORTER_ERROR || frame_size <= 4 || buffered_sample->length < frame_size )
        return -1;
    if( current_status == MP4SYS_IMPORTER_EOF )
    {
        buffered_sample->length = 0;
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
        lsmash_cleanup_summary( entry->data );
        entry->data = summary;
        info->samples_in_frame = summary->samples_in_frame;
    }
    /* read a frame's data. */
    memcpy( buffered_sample->data, info->raw_header, MP4SYS_MP3_HEADER_LENGTH );
    frame_size -= MP4SYS_MP3_HEADER_LENGTH;
    if( fread( ((uint8_t*)buffered_sample->data)+MP4SYS_MP3_HEADER_LENGTH, 1, frame_size, importer->stream ) != frame_size )
    {
        info->status = MP4SYS_IMPORTER_ERROR;
        return -1;
    }
    buffered_sample->length = MP4SYS_MP3_HEADER_LENGTH + frame_size;
    buffered_sample->dts = info->au_number++ * info->samples_in_frame;
    buffered_sample->cts = buffered_sample->dts;
    buffered_sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
    buffered_sample->prop.pre_roll.distance = header->layer == MP4SYS_LAYER_III ? 1 : 0;    /* Layer III uses MDCT */

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
    mp4sys_mp3_info_t* info = lsmash_malloc_zero( sizeof(mp4sys_mp3_info_t) );
    if( !info )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    info->status = MP4SYS_IMPORTER_OK;
    info->header = header;
    info->samples_in_frame = summary->samples_in_frame;
    memcpy( info->raw_header, buf, MP4SYS_MP3_HEADER_LENGTH );

    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        free( info );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    importer->info = info;

    return 0;
}

static uint32_t mp4sys_mp3_get_last_delta( mp4sys_importer_t* importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    mp4sys_mp3_info_t *info = (mp4sys_mp3_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != MP4SYS_IMPORTER_EOF )
        return 0;
    return info->samples_in_frame;
}

const static mp4sys_importer_functions mp4sys_mp3_importer =
{
    "MPEG-1/2BC_Audio_Legacy",
    1,
    mp4sys_mp3_probe,
    mp4sys_mp3_get_accessunit,
    mp4sys_mp3_get_last_delta,
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

typedef struct
{
    uint8_t  wb;
    uint32_t samples_in_frame;
    uint32_t au_number;
} mp4sys_amr_info_t;

static int mp4sys_amr_get_accessunit( mp4sys_importer_t* importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( track_number != 1 )
        return -1;
    mp4sys_amr_info_t *info = (mp4sys_amr_info_t *)importer->info;

    uint8_t* buf = buffered_sample->data;
    if( fread( buf, 1, 1, importer->stream ) == 0 )
    {
        /* EOF */
        buffered_sample->length = 0;
        return 0;
    }
    uint8_t FT = (*buf >> 3) & 0x0F;

    /* AMR-NB has varieties of frame-size table like this. so I'm not sure yet. */
    const int frame_size[2][16] = {
        { 13, 14, 16, 18, 20, 21, 27, 32,  5, 5, 5, 5, 0, 0, 0, 1 },
        { 18, 24, 33, 37, 41, 47, 51, 59, 61, 6, 6, 0, 0, 0, 1, 1 }
    };
    int read_size = frame_size[info->wb][FT];
    if( read_size == 0 || buffered_sample->length < read_size-- )
        return -1;
    if( read_size == 0 )
        buffered_sample->length = 1;
    else
    {
        if( fread( buf+1, 1, read_size, importer->stream ) != read_size )
            return -1;
        buffered_sample->length = read_size + 1;
    }
    buffered_sample->dts = info->au_number++ * info->samples_in_frame;
    buffered_sample->cts = buffered_sample->dts;
    buffered_sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
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
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( MP4SYS_STREAM_TYPE_AudioStream );
    if( !summary )
        return -1;
    summary->sample_type            = wb ? ISOM_CODEC_TYPE_SAWB_AUDIO : ISOM_CODEC_TYPE_SAMR_AUDIO;
    summary->object_type_indication = MP4SYS_OBJECT_TYPE_NONE; /* AMR is not defined in ISO/IEC 14496-3 */
    summary->exdata                 = NULL; /* to be set in mp4sys_amrnb_create_damr() */
    summary->exdata_length          = 0;    /* to be set in mp4sys_amrnb_create_damr() */
    summary->max_au_length          = wb ? 61 : 32;
    summary->aot                    = MP4A_AUDIO_OBJECT_TYPE_NULL; /* no effect */
    summary->frequency              = (8000 << wb);
    summary->channels               = 1;
    summary->bit_depth              = 16;
    summary->samples_in_frame       = (160 << wb);
    summary->sbr_mode               = MP4A_AAC_SBR_NOT_SPECIFIED; /* no effect */
    mp4sys_amr_info_t *info = malloc( sizeof(mp4sys_amr_info_t) );
    if( !info )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    info->wb = wb;
    info->samples_in_frame = summary->samples_in_frame;
    info->au_number = 0;
    importer->info = info;
    if( mp4sys_amr_create_damr( summary ) || lsmash_add_entry( importer->summaries, summary ) )
    {
        free( importer->info );
        importer->info = NULL;
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    return 0;
}

static uint32_t mp4sys_amr_get_last_delta( mp4sys_importer_t* importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    mp4sys_amr_info_t *info = (mp4sys_amr_info_t *)importer->info;
    if( !info || track_number != 1 )
        return 0;
    return info->samples_in_frame;
}

const static mp4sys_importer_functions mp4sys_amr_importer =
{
    "amr",
    1,
    mp4sys_amr_probe,
    mp4sys_amr_get_accessunit,
    mp4sys_amr_get_last_delta,
    mp4sys_amr_cleanup
};

/***************************************************************************
    AC-3 importer
***************************************************************************/
#define AC3_MAX_AU_LENGTH   3840
#define AC3_MIN_AU_LENGTH   128
#define AC3_SAMPLE_DURATION 1536    /* 256 (samples per audio block) * 6 (audio blocks) */

typedef struct
{
    uint8_t fscod;
    uint8_t bsid;
    uint8_t bsmod;
    uint8_t acmod;
    uint8_t lfeon;
    uint8_t frmsizecod;
} ac3_dac3_element_t;

typedef struct
{
    mp4sys_importer_status status;
    ac3_dac3_element_t dac3_element;
    lsmash_bits_t *bits;
    uint8_t buffer[AC3_MAX_AU_LENGTH];
    uint8_t *next_dac3;
    uint32_t au_number;
} mp4sys_ac3_info_t;

static void mp4sys_remove_ac3_info( mp4sys_ac3_info_t *info )
{
    if( !info )
        return;
    lsmash_bits_adhoc_cleanup( info->bits );
    free( info );
}

static mp4sys_ac3_info_t *mp4sys_create_ac3_info( void )
{
    mp4sys_ac3_info_t *info = (mp4sys_ac3_info_t *)lsmash_malloc_zero( sizeof(mp4sys_ac3_info_t) );
    if( !info )
        return NULL;
    info->bits = lsmash_bits_adhoc_create();
    if( !info->bits )
    {
        free( info );
        return NULL;
    }
    return info;
}

static void mp4sys_ac3_cleanup( mp4sys_importer_t *importer )
{
    debug_if( importer && importer->info )
        mp4sys_remove_ac3_info( importer->info );
}

static int ac3_check_syncframe_header( ac3_dac3_element_t *element )
{
    if( element->fscod == 0x3 )
        return -1;      /* unknown Sample Rate Code */
    if( element->frmsizecod > 0x25 )
        return -1;      /* unknown Frame Size Code */
    if( element->bsid >= 10 )
        return -1;      /* might be EAC-3 */
    return 0;
}

static int ac3_parse_syncframe_header( mp4sys_ac3_info_t *info, uint8_t *data )
{
    lsmash_bits_t *bits = info->bits;
    if( lsmash_bits_import_data( bits, data, AC3_MIN_AU_LENGTH ) )
        return -1;
    ac3_dac3_element_t *element = &info->dac3_element;
    lsmash_bits_get( bits, 32 );    /* syncword + crc1 */
    element->fscod      = lsmash_bits_get( bits, 2 );
    element->frmsizecod = lsmash_bits_get( bits, 6 );
    element->bsid       = lsmash_bits_get( bits, 5 );
    element->bsmod      = lsmash_bits_get( bits, 3 );
    element->acmod      = lsmash_bits_get( bits, 3 );
    if( (element->acmod & 0x01) && (element->acmod != 0x01) )
        lsmash_bits_get( bits, 2 );     /* cmixlev */
    if( element->acmod & 0x04 )
        lsmash_bits_get( bits, 2 );     /* surmixlev */
    if( element->acmod == 0x02 )
        lsmash_bits_get( bits, 2 );     /* dsurmod */
    element->lfeon = lsmash_bits_get( bits, 1 );
    lsmash_bits_empty( bits );
    return ac3_check_syncframe_header( element );
}

#define AC3_DAC3_BOX_LENGTH 11

static uint8_t *ac3_create_dac3( mp4sys_ac3_info_t *info )
{
    lsmash_bits_t *bits = info->bits;
    ac3_dac3_element_t *element = &info->dac3_element;
    lsmash_bits_put( bits, AC3_DAC3_BOX_LENGTH, 32 );
    lsmash_bits_put( bits, ISOM_BOX_TYPE_DAC3, 32 );
    lsmash_bits_put( bits, element->fscod, 2 );
    lsmash_bits_put( bits, element->bsid, 5 );
    lsmash_bits_put( bits, element->bsmod, 3 );
    lsmash_bits_put( bits, element->acmod, 3 );
    lsmash_bits_put( bits, element->lfeon, 1 );
    lsmash_bits_put( bits, element->frmsizecod >> 1, 5 );
    lsmash_bits_put( bits, 0, 5 );
    uint8_t *dac3 = lsmash_bits_export_data( bits, NULL );
    lsmash_bits_empty( bits );
    return dac3;
}

#define IF_AC3_SYNCWORD( x ) if( (x)[0] != 0x0b || (x)[1] != 0x77 )

/* data_length must be size of data that is available. */
int mp4sys_create_dac3_from_syncframe( lsmash_audio_summary_t *summary, uint8_t *data, uint32_t data_length )
{
    if( data_length < AC3_MIN_AU_LENGTH )
        return -1;
    IF_AC3_SYNCWORD( data )
        return -1;
    mp4sys_ac3_info_t info;
    info.bits = lsmash_bits_adhoc_create();
    if( !info.bits )
        return -1;
    if( ac3_parse_syncframe_header( &info, data ) )
    {
        lsmash_bits_adhoc_cleanup( info.bits );
        return -1;
    }
    uint8_t *dac3 = ac3_create_dac3( &info );
    lsmash_bits_adhoc_cleanup( info.bits );
    if( !dac3 )
        return -1;
    if( summary->exdata )
        free( summary->exdata );
    summary->exdata = dac3;
    summary->exdata_length = AC3_DAC3_BOX_LENGTH;
    return 0;
}

static const uint32_t ac3_sample_rate_table[4] = { 48000, 44100, 32000, 0 };

static const uint32_t ac3_frame_size_table[19][3] =
{
    /*  48,  44.1,    32 */
    {  128,   138,   192 },
    {  160,   174,   240 },
    {  192,   208,   288 },
    {  224,   242,   336 },
    {  256,   278,   384 },
    {  320,   348,   480 },
    {  384,   416,   576 },
    {  448,   486,   672 },
    {  512,   556,   768 },
    {  640,   696,   960 },
    {  768,   834,  1152 },
    {  896,   974,  1344 },
    { 1024,  1114,  1536 },
    { 1280,  1392,  1920 },
    { 1536,  1670,  2304 },
    { 1792,  1950,  2688 },
    { 2048,  2228,  3072 },
    { 2304,  2506,  3456 },
    { 2560,  2786,  3840 }
};

static const uint32_t ac3_channel_count_table[8] = { 2, 1, 2, 3, 3, 4, 4, 5 };

static const lsmash_channel_layout_tag ac3_channel_layout_table[8][2] =
{
    /*        LFE: off                      LFE: on             */
    { QT_CHANNEL_LAYOUT_UNKNOWN,    QT_CHANNEL_LAYOUT_UNKNOWN    },     /* FIXME: dual mono */
    { QT_CHANNEL_LAYOUT_MONO,       QT_CHANNEL_LAYOUT_AC3_1_0_1  },
    { QT_CHANNEL_LAYOUT_STEREO,     QT_CHANNEL_LAYOUT_DVD_4      },
    { QT_CHANNEL_LAYOUT_AC3_3_0,    QT_CHANNEL_LAYOUT_AC3_3_0_1  },
    { QT_CHANNEL_LAYOUT_DVD_2,      QT_CHANNEL_LAYOUT_AC3_2_1_1  },
    { QT_CHANNEL_LAYOUT_AC3_3_1,    QT_CHANNEL_LAYOUT_AC3_3_1_1  },
    { QT_CHANNEL_LAYOUT_DVD_3,      QT_CHANNEL_LAYOUT_DVD_18     },
    { QT_CHANNEL_LAYOUT_MPEG_5_0_C, QT_CHANNEL_LAYOUT_MPEG_5_1_C }
};

static lsmash_audio_summary_t *ac3_create_summary( mp4sys_ac3_info_t *info )
{
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( MP4SYS_STREAM_TYPE_AudioStream );
    if( !summary )
        return NULL;
    uint8_t *dac3 = ac3_create_dac3( info );
    if( !dac3 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
    ac3_dac3_element_t *element = &info->dac3_element;
    summary->exdata                 = dac3;
    summary->exdata_length          = AC3_DAC3_BOX_LENGTH;
    summary->sample_type            = ISOM_CODEC_TYPE_AC_3_AUDIO;
    summary->object_type_indication = MP4SYS_OBJECT_TYPE_AC_3_AUDIO;        /* forbidden to use for ISO Base Media */
    summary->max_au_length          = AC3_MAX_AU_LENGTH;
    summary->aot                    = MP4A_AUDIO_OBJECT_TYPE_NULL; /* no effect */
    summary->frequency              = ac3_sample_rate_table[ element->fscod ];
    summary->channels               = ac3_channel_count_table[ element->acmod ] + element->lfeon;
    summary->bit_depth              = 16;       /* no effect */
    summary->samples_in_frame       = AC3_SAMPLE_DURATION;
    summary->sbr_mode               = MP4A_AAC_SBR_NOT_SPECIFIED; /* no effect */
    summary->layout_tag             = ac3_channel_layout_table[ element->acmod ][ element->lfeon ];
    return summary;
}

static int ac3_compare_dac3_elements( ac3_dac3_element_t *a, ac3_dac3_element_t *b )
{
    return (a->fscod             != b->fscod)
        || (a->bsid              != b->bsid)
        || (a->bsmod             != b->bsmod)
        || (a->acmod             != b->acmod)
        || (a->lfeon             != b->lfeon)
        || ((a->frmsizecod >> 1) != (b->frmsizecod >> 1));
}

static int mp4sys_ac3_get_accessunit( mp4sys_importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_get_entry_data( importer->summaries, track_number );
    if( !summary )
        return -1;
    mp4sys_ac3_info_t *info = (mp4sys_ac3_info_t *)importer->info;
    mp4sys_importer_status current_status = info->status;
    if( current_status == MP4SYS_IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    ac3_dac3_element_t *element = &info->dac3_element;
    if( current_status == MP4SYS_IMPORTER_CHANGE )
    {
        free( summary->exdata );
        summary->exdata     = info->next_dac3;
        summary->frequency  = ac3_sample_rate_table[ element->fscod ];
        summary->channels   = ac3_channel_count_table[ element->acmod ] + element->lfeon;
        summary->layout_tag = ac3_channel_layout_table[ element->acmod ][ element->lfeon ];
    }
    uint32_t frame_size = ac3_frame_size_table[ element->frmsizecod >> 1 ][ element->fscod ];
    if( element->fscod == 0x1 && element->frmsizecod & 0x1 )
        frame_size += 2;
    if( frame_size > AC3_MIN_AU_LENGTH )
    {
        uint32_t read_size = frame_size - AC3_MIN_AU_LENGTH;
        if( fread( info->buffer + AC3_MIN_AU_LENGTH, 1, read_size, importer->stream ) != read_size )
            return -1;
    }
    memcpy( buffered_sample->data, info->buffer, frame_size );
    buffered_sample->length = frame_size;
    buffered_sample->dts = info->au_number++ * summary->samples_in_frame;
    buffered_sample->cts = buffered_sample->dts;
    buffered_sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
    buffered_sample->prop.pre_roll.distance = 1;    /* MDCT */
    if( fread( info->buffer, 1, AC3_MIN_AU_LENGTH, importer->stream ) != AC3_MIN_AU_LENGTH )
        info->status = MP4SYS_IMPORTER_EOF;
    else
    {
        /* Parse the next syncframe header. */
        IF_AC3_SYNCWORD( info->buffer )
            return -1;
        ac3_dac3_element_t current_element = info->dac3_element;
        ac3_parse_syncframe_header( info, info->buffer );
        if( ac3_compare_dac3_elements( &current_element, &info->dac3_element ) )
        {
            uint8_t *dac3 = ac3_create_dac3( info );
            if( !dac3 )
                return -1;
            info->status = MP4SYS_IMPORTER_CHANGE;
            info->next_dac3 = dac3;
        }
        else
            info->status = MP4SYS_IMPORTER_OK;
    }
    return current_status;
}

static int mp4sys_ac3_probe( mp4sys_importer_t* importer )
{
    uint8_t buf[AC3_MIN_AU_LENGTH];
    if( fread( buf, 1, AC3_MIN_AU_LENGTH, importer->stream ) != AC3_MIN_AU_LENGTH )
        return -1;
    IF_AC3_SYNCWORD( buf )
        return -1;
    mp4sys_ac3_info_t *info = mp4sys_create_ac3_info();
    if( !info )
        return -1;
    if( ac3_parse_syncframe_header( info, buf ) )
    {
        mp4sys_remove_ac3_info( info );
        return -1;
    }
    lsmash_audio_summary_t *summary = ac3_create_summary( info );
    if( !summary )
    {
        mp4sys_remove_ac3_info( info );
        return -1;
    }
    info->status = MP4SYS_IMPORTER_OK;
    info->au_number = 0;
    memcpy( info->buffer, buf, AC3_MIN_AU_LENGTH );
    importer->info = info;
    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        mp4sys_remove_ac3_info( importer->info );
        importer->info = NULL;
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    return 0;
}

static uint32_t mp4sys_ac3_get_last_delta( mp4sys_importer_t* importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    mp4sys_ac3_info_t *info = (mp4sys_ac3_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != MP4SYS_IMPORTER_EOF )
        return 0;
    return AC3_SAMPLE_DURATION;
}

const static mp4sys_importer_functions mp4sys_ac3_importer =
{
    "ac3",
    1,
    mp4sys_ac3_probe,
    mp4sys_ac3_get_accessunit,
    mp4sys_ac3_get_last_delta,
    mp4sys_ac3_cleanup
};

/***************************************************************************
    Enhanced AC-3 importer
***************************************************************************/
#define EAC3_MAX_SYNCFRAME_LENGTH 4096
#define EAC3_FIRST_FIVE_BYTES     5
#define EAC3_MIN_SAMPLE_DURATION  256

typedef struct
{
    uint8_t fscod;
    uint8_t fscod2;
    uint8_t bsid;
    uint8_t bsmod;
    uint8_t acmod;
    uint8_t lfeon;
    uint8_t num_dep_sub;
    uint16_t chan_loc;
} eac3_substream_info_t;

typedef struct
{
    mp4sys_importer_status status;
    uint8_t strmtyp;
    uint8_t substreamid;
    uint8_t current_independent_substream_id;
    eac3_substream_info_t independent_info_0;   /* mirror for creating summary */
    eac3_substream_info_t independent_info[8];
    eac3_substream_info_t dependent_info;
    uint8_t numblkscod;
    uint8_t number_of_audio_blocks;
    uint8_t frmsizecod;
    uint8_t number_of_independent_substreams;
    lsmash_bits_t *bits;
    uint8_t buffer[EAC3_MAX_SYNCFRAME_LENGTH];
    uint8_t *next_dec3;
    uint32_t next_dec3_length;
    uint32_t syncframe_count;
    uint32_t syncframe_count_in_au;
    uint32_t frame_size;
    lsmash_multiple_buffers_t *au_buffers;
    uint8_t *au;
    uint32_t au_length;
    uint8_t *incomplete_au;
    uint32_t incomplete_au_length;
    uint32_t au_number;
} mp4sys_eac3_info_t;

static void mp4sys_remove_eac3_info( mp4sys_eac3_info_t *info )
{
    if( !info )
        return;
    lsmash_destroy_multiple_buffers( info->au_buffers );
    lsmash_bits_adhoc_cleanup( info->bits );
    free( info );
}

static mp4sys_eac3_info_t *mp4sys_create_eac3_info( void )
{
    mp4sys_eac3_info_t *info = (mp4sys_eac3_info_t *)lsmash_malloc_zero( sizeof(mp4sys_eac3_info_t) );
    if( !info )
        return NULL;
    info->bits = lsmash_bits_adhoc_create();
    if( !info->bits )
    {
        free( info );
        return NULL;
    }
    info->au_buffers = lsmash_create_multiple_buffers( 2, EAC3_MAX_SYNCFRAME_LENGTH );
    if( !info->au_buffers )
    {
        lsmash_bits_adhoc_cleanup( info->bits );
        free( info );
        return NULL;
    }
    info->au            = lsmash_withdraw_buffer( info->au_buffers, 1 );
    info->incomplete_au = lsmash_withdraw_buffer( info->au_buffers, 2 );
    return info;
}

static void mp4sys_eac3_cleanup( mp4sys_importer_t *importer )
{
    debug_if( importer && importer->info )
        mp4sys_remove_eac3_info( importer->info );
}

static int eac3_check_syncframe_header( mp4sys_eac3_info_t *info )
{
    if( info->strmtyp == 0x3 )
        return -1;      /* unknown Stream type */
    eac3_substream_info_t *independent_info;
    if( info->strmtyp != 0x1 )
        independent_info = &info->independent_info[ info->current_independent_substream_id ];
    else
        independent_info = &info->dependent_info;
    if( independent_info->fscod == 0x3 && independent_info->fscod2 == 0x3 )
        return -1;      /* unknown Sample Rate Code */
    if( independent_info->bsid < 10 || independent_info->bsid > 16 )
        return -1;      /* not EAC-3 */
    return 0;
}

static int eac3_parse_syncframe_header( mp4sys_importer_t *importer )
{
    mp4sys_eac3_info_t *info = (mp4sys_eac3_info_t *)importer->info;
    lsmash_bits_t *bits = info->bits;
    if( lsmash_bits_import_data( bits, info->buffer, EAC3_FIRST_FIVE_BYTES ) )
        return -1;
    lsmash_bits_get( bits, 16 );    /* syncword */
    info->strmtyp     = lsmash_bits_get( bits, 2 );
    info->substreamid = lsmash_bits_get( bits, 3 );
    eac3_substream_info_t *substream_info;
    if( info->strmtyp != 0x1 )
    {
        info->current_independent_substream_id = info->substreamid;
        substream_info = &info->independent_info[ info->current_independent_substream_id ];
        if( info->substreamid == 0x0 )
            info->independent_info_0 = *substream_info;     /* backup */
        substream_info->chan_loc = 0;
    }
    else
        substream_info = &info->dependent_info;
    uint16_t frmsiz = lsmash_bits_get( bits, 11 );
    substream_info->fscod = lsmash_bits_get( bits, 2 );
    if( substream_info->fscod == 0x3 )
    {
        substream_info->fscod2 = lsmash_bits_get( bits, 2 );
        info->numblkscod = 0x3;
    }
    else
        info->numblkscod = lsmash_bits_get( bits, 2 );
    substream_info->acmod = lsmash_bits_get( bits, 3 );
    substream_info->lfeon = lsmash_bits_get( bits, 1 );
    lsmash_bits_empty( bits );
    /* Read up to the end of the current syncframe. */
    info->frame_size = 2 * (frmsiz + 1);
    uint32_t read_size = info->frame_size - EAC3_FIRST_FIVE_BYTES;
    if( fread( info->buffer + EAC3_FIRST_FIVE_BYTES, 1, read_size, importer->stream ) != read_size )
        return -1;
    if( lsmash_bits_import_data( bits, info->buffer + EAC3_FIRST_FIVE_BYTES, read_size ) )
        return -1;
    /* Continue to parse header. */
    substream_info->bsid = lsmash_bits_get( bits, 5 );
    lsmash_bits_get( bits, 5 );         /* dialnorm */
    if( lsmash_bits_get( bits, 1 ) )    /* compre */
        lsmash_bits_get( bits, 8 );     /* compr */
    if( substream_info->acmod == 0x0 )
    {
        lsmash_bits_get( bits, 5 );         /* dialnorm2 */
        if( lsmash_bits_get( bits, 1 ) )    /* compre2 */
            lsmash_bits_get( bits, 8 );     /* compr2 */
    }
    if( info->strmtyp == 0x1 && lsmash_bits_get( bits, 1 ) )    /* chanmape */
    {
        uint16_t chanmap = lsmash_bits_get( bits, 16 );
        uint16_t chan_loc = chanmap >> 5;
        chan_loc = (chan_loc & 0xff ) | ((chan_loc & 0x200) >> 1);
        info->independent_info[ info->current_independent_substream_id ].chan_loc |= chan_loc;
    }
    if( lsmash_bits_get( bits, 1 ) )    /* mixmdate */
    {
        if( substream_info->acmod > 0x2 )
            lsmash_bits_get( bits, 2 );     /* dmixmod */
        if( ((substream_info->acmod & 0x1) && (substream_info->acmod > 0x2)) || (substream_info->acmod & 0x4) )
            lsmash_bits_get( bits, 6 );     /* ltrt[c/sur]mixlev + loro[c/sur]mixlev */
        if( substream_info->lfeon && lsmash_bits_get( bits, 1 ) )   /* lfemixlevcode */
            lsmash_bits_get( bits, 5 );     /* lfemixlevcod */
        if( info->strmtyp == 0x0 )
        {
            if( lsmash_bits_get( bits, 1 ) )    /* pgmscle*/
                lsmash_bits_get( bits, 6 );         /* pgmscl */
            if( substream_info->acmod == 0x0 && lsmash_bits_get( bits, 1 ) )    /* pgmscle2 */
                lsmash_bits_get( bits, 6 );         /* pgmscl2 */
            if( lsmash_bits_get( bits, 1 ) )    /* extpgmscle */
                lsmash_bits_get( bits, 6 );         /* extpgmscl */
            uint8_t mixdef = lsmash_bits_get( bits, 2 );
            if( mixdef == 0x1 )
                lsmash_bits_get( bits, 5 );     /* premixcmpsel + drcsrc + premixcmpscl */
            else if( mixdef == 0x2 )
                lsmash_bits_get( bits, 12 );    /* mixdata */
            else if( mixdef == 0x3 )
            {
                uint8_t mixdeflen = lsmash_bits_get( bits, 5 );
                lsmash_bits_get( bits, 8 * (mixdeflen + 2) );     /* mixdata */
            }
            if( substream_info->acmod < 0x2 )
            {
                if( lsmash_bits_get( bits, 1 ) )    /* paninfoe */
                    lsmash_bits_get( bits, 14 );        /* paninfo */
                if( substream_info->acmod == 0x0 && lsmash_bits_get( bits, 1 ) )    /* paninfo2e */
                    lsmash_bits_get( bits, 14 );        /* paninfo2 */
            }
            if( lsmash_bits_get( bits, 1 ) )        /* frmmixcfginfoe */
            {
                if( info->numblkscod == 0x0 )
                    lsmash_bits_get( bits, 5 );         /* blkmixcfginfo[0] */
                else
                {
                    int number_of_blocks_per_syncframe = ((int []){ 1, 2, 3, 6 })[ info->numblkscod ];
                    for( int blk = 0; blk < number_of_blocks_per_syncframe; blk++ )
                        if( lsmash_bits_get( bits, 1 ) )    /* blkmixcfginfoe */
                            lsmash_bits_get( bits, 5 );         /* blkmixcfginfo[blk] */
                }
            }
        }
    }
    if( lsmash_bits_get( bits, 1 ) )    /* infomdate */
    {
        substream_info->bsmod = lsmash_bits_get( bits, 3 );
        lsmash_bits_get( bits, 1 );         /* copyrightb */
        lsmash_bits_get( bits, 1 );         /* origbs */
        if( substream_info->acmod == 0x2 )
            lsmash_bits_get( bits, 4 );         /* dsurmod + dheadphonmod */
        else if( substream_info->acmod >= 0x6 )
            lsmash_bits_get( bits, 2 );         /* dsurexmod */
        if( lsmash_bits_get( bits, 1 ) )    /* audprodie */
            lsmash_bits_get( bits, 8 );         /* mixlevel + roomtyp + adconvtyp */
        if( substream_info->acmod == 0x0 && lsmash_bits_get( bits, 1 ) )    /* audprodie2 */
            lsmash_bits_get( bits, 8 );         /* mixlevel2 + roomtyp2 + adconvtyp2 */
        if( substream_info->fscod < 0x3 )
            lsmash_bits_get( bits, 1 );     /* sourcefscod */
    }
    else
        substream_info->bsmod = 0;
    if( info->strmtyp == 0x0 && info->numblkscod != 0x3 )
        lsmash_bits_get( bits, 1 );     /* convsync */
    if( info->strmtyp == 0x2 )
    {
        int blkid;
        if( info->numblkscod == 0x3 )
            blkid = 1;
        else
            blkid = lsmash_bits_get( bits, 1 );
        if( blkid )
            lsmash_bits_get( bits, 6 );     /* frmsizecod */
    }
    if( lsmash_bits_get( bits, 1 ) )    /* addbsie */
    {
        uint8_t addbsil = lsmash_bits_get( bits, 6 );
        lsmash_bits_get( bits, (addbsil + 1) * 8 );     /* addbsi */
    }
    lsmash_bits_empty( bits );
    return eac3_check_syncframe_header( info );
}

static uint8_t *eac3_create_dec3( mp4sys_eac3_info_t *info, uint32_t *dec3_length )
{
    if( info->number_of_independent_substreams > 8 )
        return NULL;
    lsmash_bits_t *bits = info->bits;
    lsmash_bits_put( bits, 0, 32 );     /* box size */
    lsmash_bits_put( bits, ISOM_BOX_TYPE_DEC3, 32 );
    lsmash_bits_put( bits, 0, 13 );     /* data_rate will be calculated by isom_update_bitrate_info */
    lsmash_bits_put( bits, info->number_of_independent_substreams - 1, 3 );     /* num_ind_sub */
    /* Apparently, the condition of this loop defined in ETSI TS 102 366 V1.2.1 (2008-08) is wrong. */
    for( int i = 0; i < info->number_of_independent_substreams; i++ )
    {
        eac3_substream_info_t *independent_info = i ? &info->independent_info[i] : &info->independent_info_0;
        lsmash_bits_put( bits, independent_info->fscod, 2 );
        lsmash_bits_put( bits, independent_info->bsid, 5 );
        lsmash_bits_put( bits, independent_info->bsmod, 5 );
        lsmash_bits_put( bits, independent_info->acmod, 3 );
        lsmash_bits_put( bits, independent_info->lfeon, 1 );
        lsmash_bits_put( bits, 0, 3 );      /* reserved */
        lsmash_bits_put( bits, independent_info->num_dep_sub, 4 );
        if( independent_info->num_dep_sub > 0 )
            lsmash_bits_put( bits, independent_info->chan_loc, 9 );
        else
            lsmash_bits_put( bits, 0, 1 );      /* reserved */
    }
    uint8_t *dec3 = lsmash_bits_export_data( bits, dec3_length );
    lsmash_bits_empty( bits );
    /* Update box size. */
    dec3[0] = ((*dec3_length) >> 24) & 0xff;
    dec3[1] = ((*dec3_length) >> 16) & 0xff;
    dec3[2] = ((*dec3_length) >>  8) & 0xff;
    dec3[3] =  (*dec3_length)        & 0xff;
    return dec3;
}

#define IF_EAC3_SYNCWORD( x ) IF_AC3_SYNCWORD( x )

static void eac3_update_sample_rate( lsmash_audio_summary_t *summary, mp4sys_eac3_info_t *info )
{
    /* Additional independent substreams 1 to 7 must be encoded at the same sample rate as independent substream 0. */
    summary->frequency = ac3_sample_rate_table[ info->independent_info_0.fscod ];
    if( summary->frequency == 0 )
    {
        static const uint32_t eac3_reduced_sample_rate_table[4] = { 24000, 22050, 16000, 0 };
        summary->frequency = eac3_reduced_sample_rate_table[ info->independent_info_0.fscod2 ];
    }
}

static void eac3_update_channel_layout( lsmash_audio_summary_t *summary, eac3_substream_info_t *independent_info )
{
    if( independent_info->chan_loc == 0 )
    {
        summary->layout_tag = ac3_channel_layout_table[ independent_info->acmod ][ independent_info->lfeon ];
        return;
    }
    else if( independent_info->acmod != 0x7 )
    {
        summary->layout_tag = QT_CHANNEL_LAYOUT_UNKNOWN;
        return;
    }
    /* OK. All L, C, R, Ls and Rs exsist. */
    if( !independent_info->lfeon )
    {
        if( independent_info->chan_loc == 0x2 )
            summary->layout_tag = QT_CHANNEL_LAYOUT_EAC_7_0_A;
        else if( independent_info->chan_loc == 0x4 )
            summary->layout_tag = QT_CHANNEL_LAYOUT_EAC_6_0_A;
        else
            summary->layout_tag = QT_CHANNEL_LAYOUT_UNKNOWN;
        return;
    }
    /* Also LFE exsists. */
    static const struct
    {
        uint16_t chan_loc;
        lsmash_channel_layout_tag tag;
    } eac3_channel_layout_table[]
        = {
            { 0x1,   QT_CHANNEL_LAYOUT_EAC3_7_1_B },
            { 0x2,   QT_CHANNEL_LAYOUT_EAC3_7_1_A },
            { 0x4,   QT_CHANNEL_LAYOUT_EAC3_6_1_A },
            { 0x8,   QT_CHANNEL_LAYOUT_EAC3_6_1_B },
            { 0x10,  QT_CHANNEL_LAYOUT_EAC3_7_1_C },
            { 0x10,  QT_CHANNEL_LAYOUT_EAC3_7_1_D },
            { 0x40,  QT_CHANNEL_LAYOUT_EAC3_7_1_E },
            { 0x80,  QT_CHANNEL_LAYOUT_EAC3_6_1_C },
            { 0xc,   QT_CHANNEL_LAYOUT_EAC3_7_1_F },
            { 0x84,  QT_CHANNEL_LAYOUT_EAC3_7_1_G },
            { 0x88,  QT_CHANNEL_LAYOUT_EAC3_7_1_H },
            { 0 }
          };
    for( int i = 0; eac3_channel_layout_table[i].chan_loc; i++ )
        if( independent_info->chan_loc == eac3_channel_layout_table[i].chan_loc )
        {
            summary->layout_tag = eac3_channel_layout_table[i].tag;
            return;
        }
    summary->layout_tag = QT_CHANNEL_LAYOUT_UNKNOWN;
}

static void eac3_update_channel_info( lsmash_audio_summary_t *summary, mp4sys_eac3_info_t *info )
{
    summary->channels = 0;
    for( int i = 0; i < info->number_of_independent_substreams; i++ )
    {
        int channel_count = 0;
        eac3_substream_info_t *independent_info = i ? &info->independent_info[i] : &info->independent_info_0;
        channel_count = ac3_channel_count_table[ independent_info->acmod ]  /* L/C/R/Ls/Rs combination */
                      + 2 * !!(independent_info->chan_loc & 0x1)            /* Lc/Rc pair */
                      + 2 * !!(independent_info->chan_loc & 0x2)            /* Lrs/Rrs pair */
                      +     !!(independent_info->chan_loc & 0x4)            /* Cs */
                      +     !!(independent_info->chan_loc & 0x8)            /* Ts */
                      + 2 * !!(independent_info->chan_loc & 0x10)           /* Lsd/Rsd pair */
                      + 2 * !!(independent_info->chan_loc & 0x20)           /* Lw/Rw pair */
                      + 2 * !!(independent_info->chan_loc & 0x40)           /* Lvh/Rvh pair */
                      +     !!(independent_info->chan_loc & 0x80)           /* Cvh */
                      +     !!(independent_info->chan_loc & 0x100)          /* LFE2 */
                      + independent_info->lfeon;                            /* LFE */
        if( channel_count > summary->channels )
        {
            /* Pick the maximum number of channels. */
            summary->channels = channel_count;
            eac3_update_channel_layout( summary, independent_info );
        }
    }
}

static lsmash_audio_summary_t *eac3_create_summary( mp4sys_eac3_info_t *info )
{
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( MP4SYS_STREAM_TYPE_AudioStream );
    if( !summary )
        return NULL;
    summary->exdata = eac3_create_dec3( info, &summary->exdata_length );
    if( !summary->exdata )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
    summary->sample_type            = ISOM_CODEC_TYPE_EC_3_AUDIO;
    summary->object_type_indication = MP4SYS_OBJECT_TYPE_EC_3_AUDIO;        /* forbidden to use for ISO Base Media */
    summary->max_au_length          = info->syncframe_count_in_au * EAC3_MAX_SYNCFRAME_LENGTH;
    summary->aot                    = MP4A_AUDIO_OBJECT_TYPE_NULL; /* no effect */
    summary->bit_depth              = 16;       /* no effect */
    summary->samples_in_frame       = EAC3_MIN_SAMPLE_DURATION * 6;     /* 256 (samples per audio block) * 6 (audio blocks) */
    summary->sbr_mode               = MP4A_AAC_SBR_NOT_SPECIFIED; /* no effect */
    eac3_update_sample_rate( summary, info );
    eac3_update_channel_info( summary, info );
    return summary;
}

static int eac3_read_syncframe( mp4sys_importer_t *importer )
{
    mp4sys_eac3_info_t *info = (mp4sys_eac3_info_t *)importer->info;
    uint32_t read_size = fread( info->buffer, 1, EAC3_FIRST_FIVE_BYTES, importer->stream );
    if( read_size == 0 )
        return 1;       /* EOF */
    else if( read_size != EAC3_FIRST_FIVE_BYTES )
        return -1;
    IF_EAC3_SYNCWORD( info->buffer )
        return -1;
    if( eac3_parse_syncframe_header( importer ) )
        return -1;
    return 0;
}

static int eac3_get_next_accessunit_internal( mp4sys_importer_t *importer )
{
    static const uint8_t audio_block_table[4] = { 1, 2, 3, 6 };
    int complete_au = 0;
    mp4sys_eac3_info_t *info = (mp4sys_eac3_info_t *)importer->info;
    while( 1 )
    {
        int ret = eac3_read_syncframe( importer );
        if( ret == -1 )
            return -1;
        else if( ret == 1 )
        {
            /* According to ETSI TS 102 366 V1.2.1 (2008-08),
             * one access unit consists of 6 audio blocks and begins with independent substream 0.
             * The specification doesn't mention the case where a enhanced AC-3 stream ends at non-mod6 audio blocks.
             * At the end of the stream, therefore, we might make an access unit which has less than 6 audio blocks anyway. */
            info->status = MP4SYS_IMPORTER_EOF;
            complete_au = 1;
        }
        else
        {
            int independent = info->strmtyp != 0x1;
            if( independent && info->substreamid == 0x0 )
            {
                if( info->number_of_audio_blocks == 6 )
                {
                    /* Encountered the first syncframe of the next access unit. */
                    info->number_of_audio_blocks = 0;
                    complete_au = 1;
                }
                else if( info->number_of_audio_blocks > 6 )
                    return -1;
                info->number_of_independent_substreams = 0;
                info->number_of_audio_blocks += audio_block_table[ info->numblkscod ];
            }
            else if( info->syncframe_count == 0 )
                /* The first syncframe in an AU must be independent and assigned substream ID 0. */
                return -1;
            if( independent )
                info->independent_info[info->number_of_independent_substreams ++].num_dep_sub = 0;
            else
                ++ info->independent_info[info->number_of_independent_substreams - 1].num_dep_sub;
        }
        if( complete_au )
        {
            memcpy( info->au, info->incomplete_au, info->incomplete_au_length );
            info->au_length = info->incomplete_au_length;
            info->incomplete_au_length = 0;
            info->syncframe_count_in_au = info->syncframe_count;
            info->syncframe_count = 0;
            if( info->status == MP4SYS_IMPORTER_EOF )
                break;
        }
        if( info->incomplete_au_length + info->frame_size > info->au_buffers->buffer_size )
        {
            /* Increase buffer size to store AU. */
            lsmash_multiple_buffers_t *temp = lsmash_resize_multiple_buffers( info->au_buffers, info->au_buffers->buffer_size + EAC3_MAX_SYNCFRAME_LENGTH );
            if( !temp )
                return -1;
            info->au_buffers    = temp;
            info->au            = lsmash_withdraw_buffer( info->au_buffers, 1 );
            info->incomplete_au = lsmash_withdraw_buffer( info->au_buffers, 2 );
        }
        memcpy( info->incomplete_au + info->incomplete_au_length, info->buffer, info->frame_size );
        info->incomplete_au_length += info->frame_size;
        ++info->syncframe_count;
        if( complete_au )
            break;
    }
    return 0;
}

static int mp4sys_eac3_get_accessunit( mp4sys_importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_get_entry_data( importer->summaries, track_number );
    if( !summary )
        return -1;
    mp4sys_eac3_info_t *info = (mp4sys_eac3_info_t *)importer->info;
    mp4sys_importer_status current_status = info->status;
    if( current_status == MP4SYS_IMPORTER_EOF && info->au_length == 0 )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( current_status == MP4SYS_IMPORTER_CHANGE )
    {
        free( summary->exdata );
        summary->exdata = info->next_dec3;
        summary->exdata_length = info->next_dec3_length;
        summary->max_au_length = info->syncframe_count_in_au * EAC3_MAX_SYNCFRAME_LENGTH;
        eac3_update_sample_rate( summary, info );
        eac3_update_channel_info( summary, info );
    }
    memcpy( buffered_sample->data, info->au, info->au_length );
    buffered_sample->length = info->au_length;
    buffered_sample->dts = info->au_number++ * summary->samples_in_frame;
    buffered_sample->cts = buffered_sample->dts;
    buffered_sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
    buffered_sample->prop.pre_roll.distance = 1;    /* MDCT */
    if( info->status == MP4SYS_IMPORTER_EOF )
    {
        info->au_length = 0;
        return 0;
    }
    uint32_t old_syncframe_count_in_au = info->syncframe_count_in_au;
    if( eac3_get_next_accessunit_internal( importer ) )
        return -1;
    if( info->syncframe_count_in_au )
    {
        uint32_t new_length;
        uint8_t *dec3 = eac3_create_dec3( info, &new_length );
        if( !dec3 )
            return -1;
        if( (info->syncframe_count_in_au > old_syncframe_count_in_au)
         || (new_length != summary->exdata_length || memcmp( dec3, summary->exdata, summary->exdata_length )) )
        {
            info->status = MP4SYS_IMPORTER_CHANGE;
            info->next_dec3 = dec3;
            info->next_dec3_length = new_length;
        }
        else
        {
            info->status = MP4SYS_IMPORTER_OK;
            free( dec3 );
        }
    }
    return current_status;
}

static int mp4sys_eac3_probe( mp4sys_importer_t* importer )
{
    mp4sys_eac3_info_t *info = mp4sys_create_eac3_info();
    if( !info )
        return -1;
    importer->info = info;
    if( eac3_get_next_accessunit_internal( importer ) )
    {
        mp4sys_remove_eac3_info( importer->info );
        importer->info = NULL;
        return -1;
    }
    lsmash_audio_summary_t *summary = eac3_create_summary( info );
    if( !summary )
    {
        mp4sys_remove_eac3_info( importer->info );
        importer->info = NULL;
        return -1;
    }
    if( info->status != MP4SYS_IMPORTER_EOF )
        info->status = MP4SYS_IMPORTER_OK;
    info->au_number = 0;
    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        mp4sys_remove_eac3_info( importer->info );
        importer->info = NULL;
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    return 0;
}

static uint32_t mp4sys_eac3_get_last_delta( mp4sys_importer_t* importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    mp4sys_eac3_info_t *info = (mp4sys_eac3_info_t *)importer->info;
    if( !info || track_number != 1
     || info->status != MP4SYS_IMPORTER_EOF || info->au_length != 0 )
        return 0;
    return EAC3_MIN_SAMPLE_DURATION * info->number_of_audio_blocks;
}

const static mp4sys_importer_functions mp4sys_eac3_importer =
{
    "eac3",
    1,
    mp4sys_eac3_probe,
    mp4sys_eac3_get_accessunit,
    mp4sys_eac3_get_last_delta,
    mp4sys_eac3_cleanup
};

/***************************************************************************
    MPEG-4 ALS importer
***************************************************************************/
#define ALSSC_TWELVE_LENGTH 22

typedef struct
{
    uint32_t size;
    uint32_t samp_freq;
    uint32_t samples;
    uint32_t channels;
    uint16_t frame_length;
    uint8_t  resolution;
    uint8_t  random_access;
    uint8_t  ra_flag;
    uint32_t access_unit_size;
    uint32_t number_of_ra_units;
    uint32_t *ra_unit_size;
    uint8_t  *sc_data;
} als_specific_config_t;

typedef struct
{
    mp4sys_importer_status status;
    als_specific_config_t alssc;
    uint32_t samples_in_frame;
    uint32_t au_number;
} mp4sys_als_info_t;

typedef struct
{
    FILE    *stream;
    uint32_t pos;
    uint32_t buffer_size;
    uint8_t *buffer;
    uint8_t *end;
} als_stream_manager;

static void mp4sys_remove_als_info( mp4sys_als_info_t *info )
{
    if( info->alssc.ra_unit_size )
        free( info->alssc.ra_unit_size );
    if( info->alssc.sc_data )
        free( info->alssc.sc_data );
    free( info );
}

static void mp4sys_als_cleanup( mp4sys_importer_t *importer )
{
    debug_if( importer && importer->info )
        mp4sys_remove_als_info( importer->info );
}

static int als_stream_read( als_stream_manager *manager, uint32_t read_size )
{
    if( manager->buffer + manager->buffer_size >= manager->end )
    {
        uint8_t *temp = realloc( manager->buffer, manager->buffer_size + read_size );
        if( !temp )
            return -1;
        manager->buffer = temp;
        manager->buffer_size += read_size;
    }
    uint32_t actual_read_size = fread( manager->buffer + manager->pos, 1, read_size, manager->stream );
    if( actual_read_size == 0 )
        return -1;
    manager->end = manager->buffer + manager->pos + actual_read_size;
    return 0;
}

static int als_cleanup_stream_manager( als_stream_manager *manager )
{
    free( manager->buffer );
    return -1;
}

static uint32_t als_get_be32( als_stream_manager *manager )
{
    uint32_t value = (manager->buffer[ manager->pos     ] << 24)
                   | (manager->buffer[ manager->pos + 1 ] << 16)
                   | (manager->buffer[ manager->pos + 2 ] <<  8)
                   |  manager->buffer[ manager->pos + 3 ];
    manager->pos += 4;
    return value;
}

static int als_parse_specific_config( mp4sys_importer_t *importer, uint8_t *buf, als_specific_config_t *alssc )
{
    alssc->samp_freq     = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
    alssc->samples       = (buf[8] << 24) | (buf[9] << 16) | (buf[10] << 8) | buf[11];
    if( alssc->samples == 0xffffffff )
        return -1;      /* We don't support this case. */
    alssc->channels      = (buf[12] << 8) | buf[13];
    alssc->resolution    = (buf[14] & 0x1c) >> 2;
    if( alssc->resolution > 3 )
        return -1;      /* reserved */
    alssc->frame_length  = (buf[15] << 8) | buf[16];
    alssc->random_access = buf[17];
    alssc->ra_flag       = (buf[18] & 0xc0) >> 6;
    if( alssc->ra_flag == 0 )
        return -1;      /* We don't support this case. */
    buf[18] &= 0x3f;    /* Set 0 to ra_flag. We will remove ra_unit_size in each access unit. */
#if 0
    if( alssc->samples == 0xffffffff && alssc->ra_flag == 2 )
        return -1;
#endif
    int chan_sort = !!(buf[20] & 0x1);
    if( alssc->channels == 0 )
    {
        if( buf[20] & 0x8 )
            return -1;      /* If channels = 0 (mono), joint_stereo = 0. */
        else if( buf[20] & 0x4 )
            return -1;      /* If channels = 0 (mono), mc_coding = 0. */
        else if( chan_sort )
            return -1;      /* If channels = 0 (mono), chan_sort = 0. */
    }
    int chan_config      = !!(buf[20] & 0x2);
    int crc_enabled      = !!(buf[21] & 0x80);
    int aux_data_enabled = !!(buf[21] & 0x1);
    uint32_t read_size = 0;
    if( chan_config )
        read_size += 2;     /* chan_config_info */
    if( chan_sort )
    {
        uint32_t ChBits;
        for( ChBits = 1; alssc->channels >> ChBits; ChBits++ );
        uint32_t chan_pos_length = (alssc->channels + 1) * ChBits;
        read_size += chan_pos_length / 8 + !!(chan_pos_length % 8);
    }
    /* Set up stream manager. */
    als_stream_manager manager;
    manager.stream = importer->stream;
    manager.buffer_size = ALSSC_TWELVE_LENGTH;
    manager.buffer = malloc( manager.buffer_size );
    if( !manager.buffer )
        return -1;
    manager.pos = ALSSC_TWELVE_LENGTH + read_size;
    manager.end = manager.buffer + manager.buffer_size;
    memcpy( manager.buffer, buf, ALSSC_TWELVE_LENGTH );
    /* Continue to read and parse. */
    read_size += 8;     /* header_size and trailer_size */
    if( als_stream_read( &manager, read_size ) )
        return als_cleanup_stream_manager( &manager );
    uint32_t header_size  = als_get_be32( &manager );
    uint32_t trailer_size = als_get_be32( &manager );
    read_size = header_size * (header_size != 0xffffffff) + trailer_size * (trailer_size != 0xffffffff) + 4 * crc_enabled;
    if( als_stream_read( &manager, read_size ) )
        return -1;
    manager.pos += read_size;   /* Skip orig_header, orig_trailer and crc. */
    /* Random access unit */
    uint32_t number_of_frames = (alssc->samples / (alssc->frame_length + 1)) + !!(alssc->samples % (alssc->frame_length + 1));
    if( alssc->random_access != 0 )
        alssc->number_of_ra_units = number_of_frames / alssc->random_access + !!(number_of_frames % alssc->random_access);
    else
        alssc->number_of_ra_units = 0;
    if( alssc->ra_flag == 2 && alssc->random_access != 0 )
    {
        uint32_t pos = manager.pos;
        read_size = alssc->number_of_ra_units * 4;
        if( als_stream_read( &manager, read_size ) )
            return als_cleanup_stream_manager( &manager );
        alssc->ra_unit_size = malloc( alssc->number_of_ra_units * sizeof(uint32_t) );
        if( !alssc->ra_unit_size )
            return als_cleanup_stream_manager( &manager );
        for( uint32_t i = 0; i < alssc->number_of_ra_units; i++ )
            alssc->ra_unit_size[i] = als_get_be32( &manager );
        manager.pos = pos;      /* Remove ra_unit_size. */
    }
    else
        alssc->ra_unit_size = NULL;
    /* auxiliary data */
    if( aux_data_enabled )
    {
        if( als_stream_read( &manager, 4 ) )
            return als_cleanup_stream_manager( &manager );
        uint32_t aux_size = als_get_be32( &manager );
        read_size = aux_size * (aux_size != 0xffffffff);
        if( als_stream_read( &manager, read_size ) )
            return als_cleanup_stream_manager( &manager );
        manager.pos += read_size;
    }
    /* Copy ALSSpecificConfig. */
    alssc->size = manager.pos;
    alssc->sc_data = malloc( alssc->size );
    if( !alssc->sc_data )
        return als_cleanup_stream_manager( &manager );
    memcpy( alssc->sc_data, manager.buffer, alssc->size );
    als_cleanup_stream_manager( &manager );
    return 0;
}

static int mp4sys_als_get_accessunit( mp4sys_importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_get_entry_data( importer->summaries, track_number );
    if( !summary )
        return -1;
    mp4sys_als_info_t *info = (mp4sys_als_info_t *)importer->info;
    mp4sys_importer_status current_status = info->status;
    if( current_status == MP4SYS_IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    als_specific_config_t *alssc = &info->alssc;
    if( alssc->number_of_ra_units == 0 )
    {
        if( fread( buffered_sample->data, 1, alssc->access_unit_size, importer->stream ) != alssc->access_unit_size )
            return -1;
        buffered_sample->length = alssc->access_unit_size;
        buffered_sample->cts = buffered_sample->dts = 0;
        buffered_sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
        info->status = MP4SYS_IMPORTER_EOF;
        return 0;
    }
    uint32_t au_length;
    if( alssc->ra_flag == 2 )
    {
        au_length = alssc->ra_unit_size[info->au_number];
        if( fread( buffered_sample->data, 1, au_length, importer->stream ) != au_length )
            return -1;
    }
    else /* if( alssc->ra_flag == 1 ) */
    {
        uint8_t temp[4];
        if( fread( temp, 1, 4, importer->stream ) != 4 )
            return -1;
        au_length = (temp[0] << 24) | (temp[1] << 16) | (temp[2] << 8) | temp[3];     /* We remove ra_unit_size. */
        if( fread( buffered_sample->data, 1, au_length, importer->stream ) != au_length )
            return -1;
    }
    buffered_sample->length = au_length;
    buffered_sample->dts = info->au_number++ * info->samples_in_frame;
    buffered_sample->cts = buffered_sample->dts;
    buffered_sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
    if( info->au_number == alssc->number_of_ra_units )
        info->status = MP4SYS_IMPORTER_EOF;
    return 0;
}

static lsmash_audio_summary_t *als_create_summary( mp4sys_importer_t *importer, als_specific_config_t *alssc )
{
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( MP4SYS_STREAM_TYPE_AudioStream );
    if( !summary )
        return NULL;
    summary->exdata = lsmash_memdup( alssc->sc_data, alssc->size );
    if( !summary->exdata )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
    summary->exdata_length          = alssc->size;
    summary->sample_type            = ISOM_CODEC_TYPE_MP4A_AUDIO;
    summary->object_type_indication = MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3;
    summary->aot                    = MP4A_AUDIO_OBJECT_TYPE_ALS;
    summary->frequency              = alssc->samp_freq;
    summary->channels               = alssc->channels + 1;
    summary->bit_depth              = (alssc->resolution + 1) * 8;
    summary->sbr_mode               = MP4A_AAC_SBR_NOT_SPECIFIED; /* no effect */
    if( alssc->random_access != 0 )
    {
        summary->samples_in_frame = (alssc->frame_length + 1) * alssc->random_access;
        summary->max_au_length    = summary->channels * (summary->bit_depth / 8) * summary->samples_in_frame;
    }
    else
    {
        summary->samples_in_frame = 0;      /* hack for mp4sys_als_get_last_delta */
        uint64_t pos = lsmash_ftell( importer->stream );
        lsmash_fseek( importer->stream, 0, SEEK_END );
        summary->max_au_length = alssc->access_unit_size = lsmash_ftell( importer->stream ) - pos;
        lsmash_fseek( importer->stream, pos, SEEK_SET );
    }
    if( lsmash_setup_AudioSpecificConfig( summary ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
    return summary;
}

static int mp4sys_als_probe( mp4sys_importer_t *importer )
{
    uint8_t buf[ALSSC_TWELVE_LENGTH];
    if( fread( buf, 1, ALSSC_TWELVE_LENGTH, importer->stream ) != ALSSC_TWELVE_LENGTH )
        return -1;
    /* Check ALS identifier( = 0x414C5300). */
    if( buf[0] != 0x41 || buf[1] != 0x4C || buf[2] != 0x53 || buf[3] != 0x00 )
        return -1;
    als_specific_config_t alssc;
    if( als_parse_specific_config( importer, buf, &alssc ) )
        return -1;
    lsmash_audio_summary_t *summary = als_create_summary( importer, &alssc );
    if( !summary )
        return -1;
    /* importer status */
    mp4sys_als_info_t *info = lsmash_malloc_zero( sizeof(mp4sys_als_info_t) );
    if( !info )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    info->status = MP4SYS_IMPORTER_OK;
    info->alssc = alssc;
    info->samples_in_frame = summary->samples_in_frame;
    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        free( info );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    importer->info = info;
    return 0;
}

static uint32_t mp4sys_als_get_last_delta( mp4sys_importer_t* importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    mp4sys_als_info_t *info = (mp4sys_als_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != MP4SYS_IMPORTER_EOF )
        return 0;
    als_specific_config_t *alssc = &info->alssc;
    /* If alssc->number_of_ra_units == 0, then the last sample duration is just alssc->samples
     * since als_create_summary sets 0 to summary->samples_in_frame i.e. info->samples_in_frame. */
    return alssc->samples - (alssc->number_of_ra_units - 1) * info->samples_in_frame;
}

const static mp4sys_importer_functions mp4sys_als_importer =
{
    "als",
    1,
    mp4sys_als_probe,
    mp4sys_als_get_accessunit,
    mp4sys_als_get_last_delta,
    mp4sys_als_cleanup
};

/***************************************************************************
    H.264 importer
***************************************************************************/
typedef struct
{
    uint8_t nal_ref_idc;
    uint8_t nal_unit_type;
    uint8_t length;
} h264_nalu_header_t;

typedef struct
{
    uint16_t sar_width;
    uint16_t sar_height;
    uint8_t  video_full_range_flag;
    uint8_t  colour_primaries;
    uint8_t  transfer_characteristics;
    uint8_t  matrix_coefficients;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    uint8_t  fixed_frame_rate_flag;
} h264_vui_t;

typedef struct
{
    uint8_t  present;
    uint8_t  profile_idc;
    uint8_t  constraint_set_flags;
    uint8_t  level_idc;
    uint8_t  seq_parameter_set_id;
    uint8_t  chroma_format_idc;
    uint8_t  separate_colour_plane_flag;
    uint8_t  ChromaArrayType;
    uint8_t  bit_depth_luma_minus8;
    uint8_t  bit_depth_chroma_minus8;
    uint8_t  pic_order_cnt_type;
    uint8_t  delta_pic_order_always_zero_flag;
    uint8_t  num_ref_frames_in_pic_order_cnt_cycle;
    uint8_t  frame_mbs_only_flag;
    uint8_t  hrd_present;
    int32_t  offset_for_non_ref_pic;
    int32_t  offset_for_top_to_bottom_field;
    int32_t  offset_for_ref_frame[255];
    int64_t  ExpectedDeltaPerPicOrderCntCycle;
    uint32_t max_num_ref_frames;
    uint32_t log2_max_frame_num;
    uint32_t MaxFrameNum;
    uint32_t log2_max_pic_order_cnt_lsb;
    uint32_t MaxPicOrderCntLsb;
    uint32_t PicSizeInMapUnits;
    uint32_t cropped_width;
    uint32_t cropped_height;
    h264_vui_t vui;
} h264_sps_t;

typedef struct
{
    uint8_t  present;
    uint8_t  pic_parameter_set_id;
    uint8_t  seq_parameter_set_id;
    uint8_t  entropy_coding_mode_flag;
    uint8_t  bottom_field_pic_order_in_frame_present_flag;
    uint8_t  weighted_pred_flag;
    uint8_t  weighted_bipred_idc;
    uint8_t  deblocking_filter_control_present_flag;
    uint8_t  redundant_pic_cnt_present_flag;
    uint32_t SliceGroupChangeRate;
} h264_pps_t;

typedef struct
{
    uint8_t  present;
    uint8_t  random_accessible;
    uint32_t recovery_frame_cnt;
} h264_sei_t;

typedef struct
{
    uint8_t  present;
    uint8_t  type;
    uint8_t  pic_order_cnt_type;
    uint8_t  nal_ref_idc;
    uint8_t  IdrPicFlag;
    uint8_t  pic_parameter_set_id;
    uint8_t  field_pic_flag;
    uint8_t  bottom_field_flag;
    uint8_t  has_mmco5;
    uint8_t  has_redundancy;
    uint16_t idr_pic_id;
    uint32_t frame_num;
    int32_t  pic_order_cnt_lsb;
    int32_t  delta_pic_order_cnt_bottom;
    int32_t  delta_pic_order_cnt[2];
} h264_slice_info_t;

typedef struct
{
    uint8_t  type;
    uint8_t  idr;
    uint8_t  random_accessible;
    uint8_t  non_bipredictive;
    uint8_t  independent;
    uint8_t  disposable;        /* 0: nal_ref_idc != 0, 1: otherwise */
    uint8_t  has_redundancy;
    uint8_t  incomplete_au_has_primary;
    uint8_t  pic_parameter_set_id;
    uint8_t  field_pic_flag;
    uint8_t  bottom_field_flag;
    /* POC */
    uint8_t  has_mmco5;
    uint8_t  ref_pic_has_mmco5;
    uint8_t  ref_pic_bottom_field_flag;
    int32_t  ref_pic_TopFieldOrderCnt;
    int32_t  ref_pic_PicOrderCntMsb;
    int32_t  ref_pic_PicOrderCntLsb;
    int32_t  pic_order_cnt_lsb;
    int32_t  delta_pic_order_cnt_bottom;
    int32_t  delta_pic_order_cnt[2];
    int32_t  PicOrderCnt;
    /* */
    uint32_t recovery_frame_cnt;
    uint32_t frame_num;
    uint32_t FrameNumOffset;
    uint8_t *au;
    uint32_t au_length;
    uint8_t *incomplete_au;
    uint32_t incomplete_au_length;
    uint32_t au_number;
} h264_picture_info_t;

typedef struct
{
    mp4sys_importer_status status;
    lsmash_video_summary_t *summary;
    h264_nalu_header_t nalu_header;
    uint8_t prev_nalu_type;
    uint8_t composition_reordering_present;
    uint8_t no_more_read;
    uint8_t first_summary;
    h264_sps_t sps;
    h264_pps_t pps;
    h264_sei_t sei;
    isom_avcC_t avcC;
    h264_slice_info_t slice;
    h264_picture_info_t picture;
    lsmash_bits_t *bits;
    lsmash_multiple_buffers_t *buffers;
    uint8_t *rbsp_buffer;
    uint8_t *stream_buffer;
    uint8_t *stream_buffer_pos;
    uint8_t *stream_buffer_end;
    uint64_t ebsp_head_pos;
    uint32_t max_au_length;
    uint32_t num_undecodable;
    uint64_t last_intra_cts;
    lsmash_media_ts_list_t ts_list;
} mp4sys_h264_info_t;

enum
{
    H264_SLICE_TYPE_P    = 0,
    H264_SLICE_TYPE_B    = 1,
    H264_SLICE_TYPE_I    = 2,
    H264_SLICE_TYPE_SP   = 3,
    H264_SLICE_TYPE_SI   = 4
} h264_slice_type;

enum
{
    H264_PICTURE_TYPE_I           = 0,
    H264_PICTURE_TYPE_I_P         = 1,
    H264_PICTURE_TYPE_I_P_B       = 2,
    H264_PICTURE_TYPE_SI          = 3,
    H264_PICTURE_TYPE_SI_SP       = 4,
    H264_PICTURE_TYPE_I_SI        = 5,
    H264_PICTURE_TYPE_I_SI_P_SP   = 6,
    H264_PICTURE_TYPE_I_SI_P_SP_B = 7,
    H264_PICTURE_TYPE_NONE        = 8,
} h264_picture_type;

static void mp4sys_remove_h264_info( mp4sys_h264_info_t *info )
{
    if( !info )
        return;
    lsmash_remove_list( info->avcC.sequenceParameterSets,   isom_remove_avcC_ps );
    lsmash_remove_list( info->avcC.pictureParameterSets,    isom_remove_avcC_ps );
    lsmash_remove_list( info->avcC.sequenceParameterSetExt, isom_remove_avcC_ps );
    lsmash_bits_adhoc_cleanup( info->bits );
    lsmash_destroy_multiple_buffers( info->buffers );
    if( info->ts_list.timestamp )
        free( info->ts_list.timestamp );
    free( info );
}

static mp4sys_h264_info_t *mp4sys_create_h264_info( void )
{
#define H264_DEFAULT_BUFFER_SIZE (1<<16)
    mp4sys_h264_info_t *info = lsmash_malloc_zero( sizeof(mp4sys_h264_info_t) );
    if( !info )
        return NULL;
    info->bits = lsmash_bits_adhoc_create();
    if( !info->bits )
    {
        mp4sys_remove_h264_info( info );
        return NULL;
    }
    isom_avcC_t *avcC = &info->avcC;
    avcC->type = ISOM_BOX_TYPE_AVCC;
    avcC->sequenceParameterSets = lsmash_create_entry_list();
    if( !avcC->sequenceParameterSets )
    {
        mp4sys_remove_h264_info( info );
        return NULL;
    }
    avcC->pictureParameterSets = lsmash_create_entry_list();
    if( !avcC->pictureParameterSets )
    {
        mp4sys_remove_h264_info( info );
        return NULL;
    }
    info->buffers = lsmash_create_multiple_buffers( 4, H264_DEFAULT_BUFFER_SIZE );
    if( !info->buffers )
    {
        mp4sys_remove_h264_info( info );
        return NULL;
    }
    info->stream_buffer         = lsmash_withdraw_buffer( info->buffers, 1 );
    info->rbsp_buffer           = lsmash_withdraw_buffer( info->buffers, 2 );
    info->picture.au            = lsmash_withdraw_buffer( info->buffers, 3 );
    info->picture.incomplete_au = lsmash_withdraw_buffer( info->buffers, 4 );
    return info;
#undef H264_DEFAULT_BUFFER_SIZE
}

static void mp4sys_h264_cleanup( mp4sys_importer_t *importer )
{
    debug_if( importer && importer->info )
        mp4sys_remove_h264_info( importer->info );
}

static inline uint64_t h264_get_codeNum( lsmash_bits_t *bits )
{
    uint32_t leadingZeroBits = 0;
    for( int b = 0; !b; leadingZeroBits++ )
        b = lsmash_bits_get( bits, 1 );
    --leadingZeroBits;
    return ((uint64_t)1 << leadingZeroBits) - 1 + lsmash_bits_get( bits, leadingZeroBits );
}

static inline uint64_t h264_decode_exp_golomb_ue( uint64_t codeNum )
{
    return codeNum;
}

static inline int64_t h264_decode_exp_golomb_se( uint64_t codeNum )
{
    if( codeNum & 1 )
        return (int64_t)((codeNum >> 1) + 1);
    return -1 * (int64_t)(codeNum >> 1);
}

static uint64_t h264_get_exp_golomb_ue( lsmash_bits_t *bits )
{
    uint64_t codeNum = h264_get_codeNum( bits );
    return h264_decode_exp_golomb_ue( codeNum );
}

static uint64_t h264_get_exp_golomb_se( lsmash_bits_t *bits )
{
    uint64_t codeNum = h264_get_codeNum( bits );
    return h264_decode_exp_golomb_se( codeNum );
}

/* Convert EBSP (Encapsulated Byte Sequence Packets) to RBSP (Raw Byte Sequence Packets). */
static void h264_remove_emulation_prevention( uint8_t *src, uint64_t src_length, uint8_t **p_dst )
{
    uint8_t *src_end = src + src_length;
    uint8_t *dst = *p_dst;
    while( src < src_end )
        if( ((src + 2) < src_end) && !src[0] && !src[1] && (src[2] == 0x03) )
        {
            *dst++ = *src++;
            *dst++ = *src++;
            src++;  /* Skip emulation_prevention_three_byte (0x03). */
        }
        else
            *dst++ = *src++;
    *p_dst = dst;
}


#define IF_INVALID_VALUE( x ) if( x )
#define IF_EXCEED_INT32( x ) if( (x) < INT32_MIN || (x) > INT32_MAX )

static int h264_check_more_rbsp_data( lsmash_bits_t *bits )
{
    lsmash_bs_t *bs = bits->bs;
    if( bs->pos < bs->store && !(bits->store == 0 && (bs->store == bs->pos + 1)) )
        return 1;       /* rbsp_trailing_bits will be placed at the next or later byte.
                         * Note: bs->pos points at the next byte if bits->store isn't empty. */
    if( bits->store == 0 )
    {
        if( bs->store == bs->pos + 1 )
            return bs->data[ bs->pos ] != 0x80;
        /* No rbsp_trailing_bits is present in RBSP data. */
        bs->error = 1;
        return 0;
    }
    /* Check whether remainder of bits is identical to rbsp_trailing_bits. */
    uint8_t remainder_bits = bits->cache & ~(~0U << bits->store);
    uint8_t rbsp_trailing_bits = 1U << (bits->store - 1);
    return remainder_bits != rbsp_trailing_bits;
}

static int h264_check_nalu_header( h264_nalu_header_t *nalu_header, uint8_t **p_buf_pos, int use_long_start_code )
{
    uint8_t *buf_pos = *p_buf_pos;
    uint8_t forbidden_zero_bit =                              (*buf_pos >> 7) & 0x01;
    uint8_t nal_ref_idc        = nalu_header->nal_ref_idc   = (*buf_pos >> 5) & 0x03;
    uint8_t nal_unit_type      = nalu_header->nal_unit_type =  *buf_pos       & 0x1f;
    nalu_header->length = 1;
    *p_buf_pos = buf_pos + nalu_header->length;
    if( nal_unit_type == 14 || nal_unit_type == 20 )
        return -1;      /* We don't support yet. */
    IF_INVALID_VALUE( forbidden_zero_bit )
        return -1;
    /* SPS and PPS require long start code (0x00000001).
     * Also AU delimiter requires it too because this type of NALU shall be the first NALU of any AU if present. */
    IF_INVALID_VALUE( !use_long_start_code && (nal_unit_type == 7 || nal_unit_type == 8 || nal_unit_type == 9) )
        return -1;
    if( nal_ref_idc )
    {
        /* nal_ref_idc shall be equal to 0 for all NALUs having nal_unit_type equal to 6, 9, 10, 11, or 12. */
        IF_INVALID_VALUE( nal_unit_type == 6 || nal_unit_type == 9 || nal_unit_type == 10 || nal_unit_type == 11 || nal_unit_type == 12 )
            return -1;
    }
    else
        /* nal_ref_idc shall not be equal to 0 for NALUs with nal_unit_type equal to 5. */
        IF_INVALID_VALUE( nal_unit_type == 5 )
            return -1;
    return 0;
}

static int h264_parse_scaling_list( lsmash_bits_t *bits, int sizeOfScalingList )
{
    /* scaling_list( scalingList, sizeOfScalingList, useDefaultScalingMatrixFlag ) */
    int nextScale = 8;
    for( int i = 0; i < sizeOfScalingList; i++ )
    {
        int64_t delta_scale = h264_get_exp_golomb_se( bits );
        IF_INVALID_VALUE( delta_scale < -128 || delta_scale > 127 )
            return -1;
        nextScale = (nextScale + delta_scale + 256) % 256;
        if( nextScale == 0 )
            break;
    }
    return 0;
}

static int h264_parse_hrd_parameters( lsmash_bits_t *bits )
{
    /* hrd_parameters() */
    uint64_t cpb_cnt_minus1 = h264_get_exp_golomb_ue( bits );
    IF_INVALID_VALUE( cpb_cnt_minus1 > 31 )
        return -1;
    lsmash_bits_get( bits, 4 );     /* bit_rate_scale */
    lsmash_bits_get( bits, 4 );     /* cpb_size_scale */
    for( uint64_t SchedSelIdx = 0; SchedSelIdx <= cpb_cnt_minus1; SchedSelIdx++ )
    {
        h264_get_exp_golomb_ue( bits );     /* bit_rate_value_minus1[ SchedSelIdx ] */
        h264_get_exp_golomb_ue( bits );     /* cpb_size_value_minus1[ SchedSelIdx ] */
        lsmash_bits_get( bits, 1 );         /* cbr_flag             [ SchedSelIdx ] */
    }
    lsmash_bits_get( bits, 5 );     /* initial_cpb_removal_delay_length_minus1 */
    lsmash_bits_get( bits, 5 );     /* cpb_removal_delay_length_minus1 */
    lsmash_bits_get( bits, 5 );     /* dpb_output_delay_length_minus1 */
    lsmash_bits_get( bits, 5 );     /* time_offset_length */
    return 0;
}

static int h264_parse_sps_nalu( lsmash_bits_t *bits, h264_sps_t *sps, h264_nalu_header_t *nalu_header,
                                uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size )
{
    uint8_t *rbsp_start = rbsp_buffer;
    h264_remove_emulation_prevention( ebsp, ebsp_size, &rbsp_buffer );
    uint64_t rbsp_length = rbsp_buffer - rbsp_start;
    if( lsmash_bits_import_data( bits, rbsp_start, rbsp_length ) )
        return -1;
    memset( sps, 0, sizeof(h264_sps_t) );
    /* seq_parameter_set_data() */
    sps->profile_idc = lsmash_bits_get( bits, 8 );
    sps->constraint_set_flags = lsmash_bits_get( bits, 8 );
    sps->level_idc = lsmash_bits_get( bits, 8 );
    uint64_t seq_parameter_set_id = h264_get_exp_golomb_ue( bits );
    IF_INVALID_VALUE( seq_parameter_set_id > 31 )
        return -1;
    sps->seq_parameter_set_id = seq_parameter_set_id;
    if( sps->profile_idc == 100 || sps->profile_idc == 110 || sps->profile_idc == 122
     || sps->profile_idc == 244 || sps->profile_idc == 44  || sps->profile_idc == 83
     || sps->profile_idc == 86  || sps->profile_idc == 118 || sps->profile_idc == 128 )
    {
        sps->chroma_format_idc = h264_get_exp_golomb_ue( bits );
        if( sps->chroma_format_idc == 3 )
            sps->separate_colour_plane_flag = lsmash_bits_get( bits, 1 );
        uint64_t bit_depth_luma_minus8 = h264_get_exp_golomb_ue( bits );
        IF_INVALID_VALUE( bit_depth_luma_minus8 > 6 )
            return -1;
        uint64_t bit_depth_chroma_minus8 = h264_get_exp_golomb_ue( bits );
        IF_INVALID_VALUE( bit_depth_chroma_minus8 > 6 )
            return -1;
        sps->bit_depth_luma_minus8   = bit_depth_luma_minus8;
        sps->bit_depth_chroma_minus8 = bit_depth_chroma_minus8;
        lsmash_bits_get( bits, 1 );         /* qpprime_y_zero_transform_bypass_flag */
        if( lsmash_bits_get( bits, 1 ) )    /* seq_scaling_matrix_present_flag */
        {
            int num_loops = sps->chroma_format_idc != 3 ? 8 : 12;
            for( int i = 0; i < num_loops; i++ )
                if( lsmash_bits_get( bits, 1 )          /* seq_scaling_list_present_flag[i] */
                 && h264_parse_scaling_list( bits, i < 6 ? 16 : 64 ) )
                        return -1;
        }
    }
    else
    {
        sps->chroma_format_idc          = 1;
        sps->separate_colour_plane_flag = 0;
        sps->bit_depth_luma_minus8      = 0;
        sps->bit_depth_chroma_minus8    = 0;
    }
    sps->ChromaArrayType = sps->separate_colour_plane_flag ? 0 : sps->chroma_format_idc;
    uint64_t log2_max_frame_num_minus4 = h264_get_exp_golomb_ue( bits );
    IF_INVALID_VALUE( log2_max_frame_num_minus4 > 12 )
        return -1;
    sps->log2_max_frame_num = log2_max_frame_num_minus4 + 4;
    sps->MaxFrameNum = 1 << sps->log2_max_frame_num;
    uint64_t pic_order_cnt_type = h264_get_exp_golomb_ue( bits );
    IF_INVALID_VALUE( pic_order_cnt_type > 2 )
        return -1;
    sps->pic_order_cnt_type = pic_order_cnt_type;
    if( sps->pic_order_cnt_type == 0 )
    {
        uint64_t log2_max_pic_order_cnt_lsb_minus4 = h264_get_exp_golomb_ue( bits );
        IF_INVALID_VALUE( log2_max_pic_order_cnt_lsb_minus4 > 12 )
            return -1;
        sps->log2_max_pic_order_cnt_lsb = log2_max_pic_order_cnt_lsb_minus4 + 4;
        sps->MaxPicOrderCntLsb = 1 << sps->log2_max_pic_order_cnt_lsb;
    }
    else if( sps->pic_order_cnt_type == 1 )
    {
        sps->delta_pic_order_always_zero_flag = lsmash_bits_get( bits, 1 );
        int64_t max_value =  ((uint64_t)1 << 31) - 1;
        int64_t min_value = -((uint64_t)1 << 31) + 1;
        int64_t offset_for_non_ref_pic = h264_get_exp_golomb_se( bits );
        if( offset_for_non_ref_pic < min_value || offset_for_non_ref_pic > max_value )
            return -1;
        sps->offset_for_non_ref_pic = offset_for_non_ref_pic;
        int64_t offset_for_top_to_bottom_field = h264_get_exp_golomb_se( bits );
        if( offset_for_top_to_bottom_field < min_value || offset_for_top_to_bottom_field > max_value )
            return -1;
        sps->offset_for_top_to_bottom_field = offset_for_top_to_bottom_field;
        uint64_t num_ref_frames_in_pic_order_cnt_cycle = h264_get_exp_golomb_ue( bits );
        IF_INVALID_VALUE( num_ref_frames_in_pic_order_cnt_cycle > 255 )
            return -1;
        sps->num_ref_frames_in_pic_order_cnt_cycle = num_ref_frames_in_pic_order_cnt_cycle;
        sps->ExpectedDeltaPerPicOrderCntCycle = 0;
        for( int i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++ )
        {
            int64_t offset_for_ref_frame = h264_get_exp_golomb_se( bits );
            if( offset_for_ref_frame < min_value || offset_for_ref_frame > max_value )
                return -1;
            sps->offset_for_ref_frame[i] = offset_for_ref_frame;
            sps->ExpectedDeltaPerPicOrderCntCycle += offset_for_ref_frame;
        }
    }
    sps->max_num_ref_frames = h264_get_exp_golomb_ue( bits );
    lsmash_bits_get( bits, 1 );         /* gaps_in_frame_num_value_allowed_flag */
    uint64_t pic_width_in_mbs_minus1        = h264_get_exp_golomb_ue( bits );
    uint64_t pic_height_in_map_units_minus1 = h264_get_exp_golomb_ue( bits );
    sps->frame_mbs_only_flag = lsmash_bits_get( bits, 1 );
    if( !sps->frame_mbs_only_flag )
        lsmash_bits_get( bits, 1 );     /* mb_adaptive_frame_field_flag */
    lsmash_bits_get( bits, 1 );         /* direct_8x8_inference_flag */
    uint64_t PicWidthInMbs       = pic_width_in_mbs_minus1        + 1;
    uint64_t PicHeightInMapUnits = pic_height_in_map_units_minus1 + 1;
    sps->PicSizeInMapUnits = PicWidthInMbs * PicHeightInMapUnits;
    sps->cropped_width  = PicWidthInMbs * 16;
    sps->cropped_height = (2 - sps->frame_mbs_only_flag) * PicHeightInMapUnits * 16;
    if( lsmash_bits_get( bits, 1 ) )    /* frame_cropping_flag */
    {
        uint8_t CropUnitX;
        uint8_t CropUnitY;
        if( sps->ChromaArrayType == 0 )
        {
            CropUnitX = 1;
            CropUnitY = 2 - sps->frame_mbs_only_flag;
        }
        else
        {
            static const int SubWidthC [] = { 0, 2, 2, 1 };
            static const int SubHeightC[] = { 0, 2, 1, 1 };
            CropUnitX = SubWidthC [ sps->chroma_format_idc ];
            CropUnitY = SubHeightC[ sps->chroma_format_idc ] * (2 - sps->frame_mbs_only_flag);
        }
        uint64_t frame_crop_left_offset   = h264_get_exp_golomb_ue( bits );
        uint64_t frame_crop_right_offset  = h264_get_exp_golomb_ue( bits );
        uint64_t frame_crop_top_offset    = h264_get_exp_golomb_ue( bits );
        uint64_t frame_crop_bottom_offset = h264_get_exp_golomb_ue( bits );
        sps->cropped_width  -= (frame_crop_left_offset + frame_crop_right_offset)  * CropUnitX;
        sps->cropped_height -= (frame_crop_top_offset  + frame_crop_bottom_offset) * CropUnitY;
    }
    if( lsmash_bits_get( bits, 1 ) )    /* vui_parameters_present_flag */
    {
        /* vui_parameters() */
        if( lsmash_bits_get( bits, 1 ) )        /* aspect_ratio_info_present_flag */
        {
            uint8_t aspect_ratio_idc = lsmash_bits_get( bits, 8 );
            if( aspect_ratio_idc == 255 )
            {
                /* Extended_SAR */
                sps->vui.sar_width  = lsmash_bits_get( bits, 16 );
                sps->vui.sar_height = lsmash_bits_get( bits, 16 );
            }
            else
            {
                static const struct
                {
                    uint16_t sar_width;
                    uint16_t sar_height;
                } pre_defined_sar[]
                    = {
                        {  0,  0 }, {  1,  1 }, { 12, 11 }, {  10, 11 }, { 16, 11 },
                        { 40, 33 }, { 24, 11 }, { 20, 11 }, {  32, 11 }, { 80, 33 },
                        { 18, 11 }, { 15, 11 }, { 64, 33 }, { 160, 99 }, {  4,  3 },
                        {  3,  2 }, {  2,  1 }
                      };
                if( aspect_ratio_idc < (sizeof(pre_defined_sar) / sizeof(pre_defined_sar[0])) )
                {
                    sps->vui.sar_width  = pre_defined_sar[ aspect_ratio_idc ].sar_width;
                    sps->vui.sar_height = pre_defined_sar[ aspect_ratio_idc ].sar_height;
                }
                else
                {
                    /* Behavior when unknown aspect_ratio_idc is detected is not specified in the specification. */
                    sps->vui.sar_width  = 0;
                    sps->vui.sar_height = 0;
                }
            }
        }
        if( lsmash_bits_get( bits, 1 ) )        /* overscan_info_present_flag */
            lsmash_bits_get( bits, 1 );         /* overscan_appropriate_flag */
        if( lsmash_bits_get( bits, 1 ) )        /* video_signal_type_present_flag */
        {
            lsmash_bits_get( bits, 3 );         /* video_format */
            sps->vui.video_full_range_flag = lsmash_bits_get( bits, 1 );
            if( lsmash_bits_get( bits, 1 ) )    /* colour_description_present_flag */
            {
                sps->vui.colour_primaries         = lsmash_bits_get( bits, 8 );
                sps->vui.transfer_characteristics = lsmash_bits_get( bits, 8 );
                sps->vui.matrix_coefficients      = lsmash_bits_get( bits, 8 );
            }
        }
        if( lsmash_bits_get( bits, 1 ) )        /* chroma_loc_info_present_flag */
        {
            h264_get_exp_golomb_ue( bits );     /* chroma_sample_loc_type_top_field */
            h264_get_exp_golomb_ue( bits );     /* chroma_sample_loc_type_bottom_field */
        }
        if( lsmash_bits_get( bits, 1 ) )        /* timing_info_present_flag */
        {
            sps->vui.num_units_in_tick     = lsmash_bits_get( bits, 32 );
            sps->vui.time_scale            = lsmash_bits_get( bits, 32 );
            sps->vui.fixed_frame_rate_flag = lsmash_bits_get( bits, 1 );
        }
        int nal_hrd_parameters_present_flag = lsmash_bits_get( bits, 1 );
        if( nal_hrd_parameters_present_flag
         && h264_parse_hrd_parameters( bits ) )
            return -1;
        int vcl_hrd_parameters_present_flag = lsmash_bits_get( bits, 1 );
        if( vcl_hrd_parameters_present_flag
         && h264_parse_hrd_parameters( bits ) )
            return -1;
        if( nal_hrd_parameters_present_flag || vcl_hrd_parameters_present_flag )
        {
            sps->hrd_present = 1;
            lsmash_bits_get( bits, 1 );         /* low_delay_hrd_flag */
        }
        lsmash_bits_get( bits, 1 );             /* pic_struct_present_flag */
        if( lsmash_bits_get( bits, 1 ) )        /* bitstream_restriction_flag */
        {
            lsmash_bits_get( bits, 1 );         /* motion_vectors_over_pic_boundaries_flag */
            h264_get_exp_golomb_ue( bits );     /* max_bytes_per_pic_denom */
            h264_get_exp_golomb_ue( bits );     /* max_bits_per_mb_denom */
            h264_get_exp_golomb_ue( bits );     /* log2_max_mv_length_horizontal */
            h264_get_exp_golomb_ue( bits );     /* log2_max_mv_length_vertical */
            h264_get_exp_golomb_ue( bits );     /* num_reorder_frames */
            h264_get_exp_golomb_ue( bits );     /* max_dec_frame_buffering */
        }
    }
    else
    {
        sps->vui.video_full_range_flag = 0;
        sps->vui.num_units_in_tick     = 1;
        sps->vui.time_scale            = 50;
        sps->vui.fixed_frame_rate_flag = 0;
    }
    /* rbsp_trailing_bits() */
    IF_INVALID_VALUE( !lsmash_bits_get( bits, 1 ) )     /* rbsp_stop_one_bit */
        return -1;
    lsmash_bits_empty( bits );
    return bits->bs->error ? -1 : 0;
}

static int h264_parse_pps_nalu( lsmash_bits_t *bits, h264_sps_t *sps, h264_pps_t *pps, h264_nalu_header_t *nalu_header,
                                uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size )
{
    if( !sps )
        return -1;
    uint8_t *rbsp_start = rbsp_buffer;
    h264_remove_emulation_prevention( ebsp, ebsp_size, &rbsp_buffer );
    uint64_t rbsp_length = rbsp_buffer - rbsp_start;
    if( lsmash_bits_import_data( bits, rbsp_start, rbsp_length ) )
        return -1;
    memset( pps, 0, sizeof(h264_pps_t) );
    /* pic_parameter_set_rbsp */
    uint64_t pic_parameter_set_id = h264_get_exp_golomb_ue( bits );
    IF_INVALID_VALUE( pic_parameter_set_id > 255 )
        return -1;
    pps->pic_parameter_set_id = pic_parameter_set_id;
    uint64_t seq_parameter_set_id = h264_get_exp_golomb_ue( bits );
    IF_INVALID_VALUE( seq_parameter_set_id > 31 )
        return -1;
    pps->seq_parameter_set_id = seq_parameter_set_id;
    pps->entropy_coding_mode_flag = lsmash_bits_get( bits, 1 );
    pps->bottom_field_pic_order_in_frame_present_flag = lsmash_bits_get( bits, 1 );
    uint64_t num_slice_groups_minus1 = h264_get_exp_golomb_ue( bits );
    if( num_slice_groups_minus1 )        /* num_slice_groups_minus1 */
    {
        uint64_t slice_group_map_type = h264_get_exp_golomb_ue( bits );
        IF_INVALID_VALUE( slice_group_map_type > 6 )
            return -1;
        if( slice_group_map_type == 0 )
            for( uint64_t iGroup = 0; iGroup <= num_slice_groups_minus1; iGroup++ )
                h264_get_exp_golomb_ue( bits );     /* run_length_minus1[ iGroup ] */
        else if( slice_group_map_type == 2 )
            for( uint64_t iGroup = 0; iGroup < num_slice_groups_minus1; iGroup++ )
            {
                h264_get_exp_golomb_ue( bits );     /* top_left    [ iGroup ] */
                h264_get_exp_golomb_ue( bits );     /* bottom_right[ iGroup ] */
            }
        else if( slice_group_map_type == 3
              || slice_group_map_type == 4
              || slice_group_map_type == 5 )
        {
            lsmash_bits_get( bits, 1 );         /* slice_group_change_direction_flag */
            uint64_t slice_group_change_rate_minus1 = h264_get_exp_golomb_ue( bits );
            IF_INVALID_VALUE( slice_group_change_rate_minus1 > (sps->PicSizeInMapUnits - 1) )
                return -1;
            pps->SliceGroupChangeRate = slice_group_change_rate_minus1 + 1;
        }
        else if( slice_group_map_type == 6 )
        {
            uint64_t pic_size_in_map_units_minus1 = h264_get_exp_golomb_ue( bits );
            /* slice_group_id_length = ceil( log2( num_slice_groups_minus1 + 1 ) ); */
            uint64_t slice_group_id_length;
            for( slice_group_id_length = 1; num_slice_groups_minus1 >> slice_group_id_length; slice_group_id_length++ );
            for( uint64_t i = 0; i <= pic_size_in_map_units_minus1; i++ )
                /* slice_group_id */
                IF_INVALID_VALUE( lsmash_bits_get( bits, slice_group_id_length ) > num_slice_groups_minus1 )
                    return -1;
        }
    }
    h264_get_exp_golomb_ue( bits );     /* num_ref_idx_l0_default_active_minus1 */
    h264_get_exp_golomb_ue( bits );     /* num_ref_idx_l1_default_active_minus1 */
    pps->weighted_pred_flag  = lsmash_bits_get( bits, 1 );
    pps->weighted_bipred_idc = lsmash_bits_get( bits, 2 );
    h264_get_exp_golomb_se( bits );     /* pic_init_qp_minus26 */
    h264_get_exp_golomb_se( bits );     /* pic_init_qs_minus26 */
    h264_get_exp_golomb_se( bits );     /* chroma_qp_index_offset */
    pps->deblocking_filter_control_present_flag = lsmash_bits_get( bits, 1 );
    lsmash_bits_get( bits, 1 );         /* constrained_intra_pred_flag */
    pps->redundant_pic_cnt_present_flag = lsmash_bits_get( bits, 1 );
    if( h264_check_more_rbsp_data( bits ) )
    {
        int transform_8x8_mode_flag = lsmash_bits_get( bits, 1 );
        if( lsmash_bits_get( bits, 1 ) )        /* pic_scaling_matrix_present_flag */
        {
            int num_loops = 6 + (sps->chroma_format_idc != 3 ? 2 : 6) * transform_8x8_mode_flag;
            for( int i = 0; i < num_loops; i++ )
                if( lsmash_bits_get( bits, 1 )          /* pic_scaling_list_present_flag[i] */
                 && h264_parse_scaling_list( bits, i < 6 ? 16 : 64 ) )
                        return -1;
        }
        h264_get_exp_golomb_se( bits );         /* second_chroma_qp_index_offset */
    }
    /* rbsp_trailing_bits() */
    IF_INVALID_VALUE( !lsmash_bits_get( bits, 1 ) )     /* rbsp_stop_one_bit */
        return -1;
    lsmash_bits_empty( bits );
    return bits->bs->error ? -1 : 0;
}

static int h264_parse_sei_nalu( lsmash_bits_t *bits, h264_sei_t *sei, h264_nalu_header_t *nalu_header,
                                uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size )
{
    uint8_t *rbsp_start = rbsp_buffer;
    h264_remove_emulation_prevention( ebsp, ebsp_size, &rbsp_buffer );
    uint64_t rbsp_length = rbsp_buffer - rbsp_start;
    if( lsmash_bits_import_data( bits, rbsp_start, rbsp_length ) )
        return -1;
    uint64_t rbsp_pos = 0;
    do
    {
        /* sei_message() */
        uint32_t payloadType = 0;
        for( uint8_t temp = lsmash_bits_get( bits, 8 ); ; temp = lsmash_bits_get( bits, 8 ) )
        {
            /* 0xff     : ff_byte
             * otherwise: last_payload_type_byte */
            payloadType += temp;
            ++rbsp_pos;
            if( temp != 0xff )
                break;
        }
        uint32_t payloadSize = 0;
        for( uint8_t temp = lsmash_bits_get( bits, 8 ); ; temp = lsmash_bits_get( bits, 8 ) )
        {
            /* 0xff     : ff_byte
             * otherwise: last_payload_size_byte */
            payloadSize += temp;
            ++rbsp_pos;
            if( temp != 0xff )
                break;
        }
        if( payloadType == 3 )
        {
            /* filler_payload
             * AVC file format is forbidden to contain this. */
            return -1;
        }
        else if( payloadType == 6 )
        {
            /* recovery_point */
            sei->present            = 1;
            sei->random_accessible  = 1;
            sei->recovery_frame_cnt = h264_get_exp_golomb_ue( bits );
            lsmash_bits_get( bits, 1 );     /* exact_match_flag */
            lsmash_bits_get( bits, 1 );     /* broken_link_flag */
            lsmash_bits_get( bits, 2 );     /* changing_slice_group_idc */
        }
        else
            lsmash_bits_get( bits, payloadSize * 8 );
        lsmash_bits_get_align( bits );
        rbsp_pos += payloadSize;
    } while( *(rbsp_start + rbsp_pos) != 0x80 );        /* All SEI messages are byte aligned at their end.
                                                         * Therefore, 0x80 shall be rbsp_trailing_bits(). */
    lsmash_bits_empty( bits );
    return bits->bs->error ? -1 : 0;
}

static int h264_parse_slice_header( lsmash_bits_t *bits, h264_sps_t *sps, h264_pps_t *pps,
                                    h264_slice_info_t *slice, h264_nalu_header_t *nalu_header )
{
    memset( slice, 0, sizeof(h264_slice_info_t) );
    slice->pic_order_cnt_type = sps->pic_order_cnt_type;
    slice->nal_ref_idc = nalu_header->nal_ref_idc;
    slice->IdrPicFlag = (nalu_header->nal_unit_type == 5);
    /* slice_header() */
    h264_get_exp_golomb_ue( bits );     /* first_mb_in_slice */
    uint8_t slice_type = slice->type = h264_get_exp_golomb_ue( bits );
    IF_INVALID_VALUE( (uint64_t)slice->type > 9 )
        return -1;
    if( slice_type > 4 )
        slice_type = slice->type -= 5;
    IF_INVALID_VALUE( (slice->IdrPicFlag || sps->max_num_ref_frames == 0) && slice_type != 2 && slice_type != 4 )
        return -1;
    slice->pic_parameter_set_id = h264_get_exp_golomb_ue( bits );
    if( sps->separate_colour_plane_flag )
        lsmash_bits_get( bits, 2 );     /* colour_plane_id */
    uint64_t frame_num = lsmash_bits_get( bits, sps->log2_max_frame_num );
    IF_INVALID_VALUE( frame_num >= (1 << sps->log2_max_frame_num) || (slice->IdrPicFlag && frame_num) )
        return -1;
    slice->frame_num = frame_num;
    if( !sps->frame_mbs_only_flag )
    {
        slice->field_pic_flag = lsmash_bits_get( bits, 1 );
        if( slice->field_pic_flag )
            slice->bottom_field_flag = lsmash_bits_get( bits, 1 );
    }
    if( slice->IdrPicFlag )
    {
        uint64_t idr_pic_id = h264_get_exp_golomb_ue( bits );
        IF_INVALID_VALUE( idr_pic_id > 65535 )
            return -1;
        slice->idr_pic_id = idr_pic_id;
    }
    if( sps->pic_order_cnt_type == 0 )
    {
        uint64_t pic_order_cnt_lsb = lsmash_bits_get( bits, sps->log2_max_pic_order_cnt_lsb );
        IF_INVALID_VALUE( pic_order_cnt_lsb >= sps->MaxPicOrderCntLsb )
            return -1;
        slice->pic_order_cnt_lsb = pic_order_cnt_lsb;
        if( pps->bottom_field_pic_order_in_frame_present_flag && !slice->field_pic_flag )
            slice->delta_pic_order_cnt_bottom = h264_get_exp_golomb_se( bits );
    }
    else if( sps->pic_order_cnt_type == 1 && !sps->delta_pic_order_always_zero_flag )
    {
        slice->delta_pic_order_cnt[0] = h264_get_exp_golomb_se( bits );
        if( pps->bottom_field_pic_order_in_frame_present_flag && !slice->field_pic_flag )
            slice->delta_pic_order_cnt[1] = h264_get_exp_golomb_se( bits );
    }
    if( pps->redundant_pic_cnt_present_flag )
    {
        uint64_t redundant_pic_cnt = h264_get_exp_golomb_ue( bits );
        IF_INVALID_VALUE( redundant_pic_cnt > 127 )
            return -1;
        slice->has_redundancy = !!redundant_pic_cnt;
    }
    if( slice_type == H264_SLICE_TYPE_B )
        lsmash_bits_get( bits, 1 );
    uint64_t num_ref_idx_l0_active_minus1 = 0;
    uint64_t num_ref_idx_l1_active_minus1 = 0;
    if( slice_type == H264_SLICE_TYPE_P || slice_type == H264_SLICE_TYPE_SP || slice_type == H264_SLICE_TYPE_B )
    {
        if( lsmash_bits_get( bits, 1 ) )            /* num_ref_idx_active_override_flag */
        {
            num_ref_idx_l0_active_minus1 = h264_get_exp_golomb_ue( bits );
            IF_INVALID_VALUE( num_ref_idx_l0_active_minus1 > 31 )
                return -1;
            if( slice_type == H264_SLICE_TYPE_B )
            {
                num_ref_idx_l1_active_minus1 = h264_get_exp_golomb_ue( bits );
                IF_INVALID_VALUE( num_ref_idx_l1_active_minus1 > 31 )
                    return -1;
            }
        }
    }
    if( nalu_header->nal_unit_type == 20 )
    {
        return -1;      /* No support of MVC yet */
#if 0
        /* ref_pic_list_mvc_modification() */
        if( slice_type == H264_SLICE_TYPE_P || slice_type == H264_SLICE_TYPE_B || slice_type == H264_SLICE_TYPE_SP )
        {
            if( lsmash_bits_get( bits, 1 ) )        /* (S)P: ref_pic_list_modification_flag_l0
                                                     *    B: ref_pic_list_modification_flag_l1 */
            {
                uint64_t modification_of_pic_nums_idc;
                do
                {
                    modification_of_pic_nums_idc = h264_get_exp_golomb_ue( bits );
#if 0
                    if( modification_of_pic_nums_idc == 0 || modification_of_pic_nums_idc == 1 )
                        h264_get_exp_golomb_ue( bits );     /* abs_diff_pic_num_minus1 */
                    else if( modification_of_pic_nums_idc == 2 )
                        h264_get_exp_golomb_ue( bits );     /* long_term_pic_num */
                    else if( modification_of_pic_nums_idc == 4 || modification_of_pic_nums_idc == 5 )
                        h264_get_exp_golomb_ue( bits );     /* abs_diff_view_idx_minus1 */
#else
                    if( modification_of_pic_nums_idc != 3 )
                        h264_get_exp_golomb_ue( bits );     /* abs_diff_pic_num_minus1, long_term_pic_num or abs_diff_view_idx_minus1 */
#endif
                } while( modification_of_pic_nums_idc != 3 );
        }
#endif
    }
    else
    {
        /* ref_pic_list_modification() */
        if( slice_type == H264_SLICE_TYPE_P || slice_type == H264_SLICE_TYPE_B || slice_type == H264_SLICE_TYPE_SP )
        {
            if( lsmash_bits_get( bits, 1 ) )        /* (S)P: ref_pic_list_modification_flag_l0
                                                     *    B: ref_pic_list_modification_flag_l1 */
            {
                uint64_t modification_of_pic_nums_idc;
                do
                {
                    modification_of_pic_nums_idc = h264_get_exp_golomb_ue( bits );
#if 0
                    if( modification_of_pic_nums_idc == 0 || modification_of_pic_nums_idc == 1 )
                        h264_get_exp_golomb_ue( bits );     /* abs_diff_pic_num_minus1 */
                    else if( modification_of_pic_nums_idc == 2 )
                        h264_get_exp_golomb_ue( bits );     /* long_term_pic_num */
#else
                    if( modification_of_pic_nums_idc != 3 )
                        h264_get_exp_golomb_ue( bits );     /* abs_diff_pic_num_minus1 or long_term_pic_num */
#endif
                } while( modification_of_pic_nums_idc != 3 );
            }
        }
    }
    if( (pps->weighted_pred_flag && (slice_type == H264_SLICE_TYPE_P || slice_type == H264_SLICE_TYPE_SP))
     || (pps->weighted_bipred_idc == 1 && slice_type == H264_SLICE_TYPE_B) )
    {
        /* pred_weight_table() */
        h264_get_exp_golomb_ue( bits );         /* luma_log2_weight_denom */
        if( sps->ChromaArrayType )
            h264_get_exp_golomb_ue( bits );     /* chroma_log2_weight_denom */
        for( uint8_t i = 0; i <= num_ref_idx_l0_active_minus1; i++ )
        {
            if( lsmash_bits_get( bits, 1 ) )    /* luma_weight_l0_flag */
            {
                h264_get_exp_golomb_se( bits );     /* luma_weight_l0[i] */
                h264_get_exp_golomb_se( bits );     /* luma_offset_l0[i] */
            }
            if( sps->ChromaArrayType
             && lsmash_bits_get( bits, 1 )      /* chroma_weight_l0_flag */ )
                for( int j = 0; j < 2; j++ )
                {
                    h264_get_exp_golomb_se( bits );     /* chroma_weight_l0[i][j]*/
                    h264_get_exp_golomb_se( bits );     /* chroma_offset_l0[i][j] */
                }
        }
        if( slice_type == H264_SLICE_TYPE_B )
            for( uint8_t i = 0; i <= num_ref_idx_l1_active_minus1; i++ )
            {
                if( lsmash_bits_get( bits, 1 ) )    /* luma_weight_l1_flag */
                {
                    h264_get_exp_golomb_se( bits );     /* luma_weight_l1[i] */
                    h264_get_exp_golomb_se( bits );     /* luma_offset_l1[i] */
                }
                if( sps->ChromaArrayType
                 && lsmash_bits_get( bits, 1 )      /* chroma_weight_l1_flag */ )
                    for( int j = 0; j < 2; j++ )
                    {
                        h264_get_exp_golomb_se( bits );     /* chroma_weight_l1[i][j]*/
                        h264_get_exp_golomb_se( bits );     /* chroma_offset_l1[i][j] */
                    }
            }
    }
    if( !nalu_header->nal_ref_idc )
    {
        /* dec_ref_pic_marking() */
        if( slice->IdrPicFlag )
        {
            lsmash_bits_get( bits, 1 );     /* no_output_of_prior_pics_flag */
            lsmash_bits_get( bits, 1 );     /* long_term_reference_flag */
        }
        else if( lsmash_bits_get( bits, 1 ) )       /* adaptive_ref_pic_marking_mode_flag */
        {
            uint64_t memory_management_control_operation;
            do
            {
                memory_management_control_operation = h264_get_exp_golomb_ue( bits );
                if( memory_management_control_operation )
                {
                    if( memory_management_control_operation == 5 )
                        slice->has_mmco5 = 1;
                    h264_get_exp_golomb_ue( bits );
                }
            } while( memory_management_control_operation );
        }
    }
#if 0   /* We needn't read more.
         * Skip slice_id (only in slice_data_partition_a_layer_rbsp( )), slice_data() and rbsp_slice_trailing_bits(). */
    if( pps->entropy_coding_mode_flag && slice_type != H264_SLICE_TYPE_I && slice_type != H264_SLICE_TYPE_SI )
        h264_get_exp_golomb_ue( bits );     /* cabac_init_idc */
    h264_get_exp_golomb_se( bits );         /* slice_qp_delta */
    if( slice_type == H264_SLICE_TYPE_SP || slice_type == H264_SLICE_TYPE_SI )
    {
        if( slice_type == H264_SLICE_TYPE_SP )
            lsmash_bits_get( bits, 1 );     /* sp_for_switch_flag */
        h264_get_exp_golomb_se( bits );     /* slice_qs_delta */
    }
    if( pps->deblocking_filter_control_present_flag
     && h264_get_exp_golomb_ue( bits ) != 1 /* disable_deblocking_filter_idc */ )
    {
        int64_t slice_alpha_c0_offset_div2 = h264_get_exp_golomb_se( bits );
        IF_INVALID_VALUE( slice_alpha_c0_offset_div2 < -6 || slice_alpha_c0_offset_div2 > 6 )
            return -1;
        int64_t slice_beta_offset_div2     = h264_get_exp_golomb_se( bits );
        IF_INVALID_VALUE( slice_beta_offset_div2     < -6 || slice_beta_offset_div2     > 6 )
            return -1;
    }
    if( pps->num_slice_groups_minus1
    && (slice_group_map_type == 3 || slice_group_map_type == 4 || slice_group_map_type == 5) )
    {
        uint64_t slice_group_change_cycle_length = ceil( log( sps->PicSizeInMapUnits / pps->SliceGroupChangeRate + 1 ) / 0.693147180559945 );
        uint64_t slice_group_change_cycle = lsmash_bits_get( bits, slice_group_change_cycle_length );
        IF_INVALID_VALUE( slice_group_change_cycle > (uint64_t)ceil( sps->PicSizeInMapUnits / pps->SliceGroupChangeRate ) )
            return -1;
    }
#endif
    lsmash_bits_empty( bits );
    return bits->bs->error ? -1 : 0;
}

static int h264_parse_slice( lsmash_bits_t *bits, h264_sps_t *sps, h264_pps_t *pps,
                             h264_slice_info_t *slice, h264_nalu_header_t *nalu_header,
                             uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size )
{
    if( !sps || !pps )
        return -1;      /* This would occur when the stream starts from non-IDR picture. */
    uint8_t *rbsp_start = rbsp_buffer;
    h264_remove_emulation_prevention( ebsp, ebsp_size, &rbsp_buffer );
    uint64_t rbsp_length = rbsp_buffer - rbsp_start;
    if( lsmash_bits_import_data( bits, rbsp_start, rbsp_length ) )
        return -1;
    if( nalu_header->nal_unit_type != 3 && nalu_header->nal_unit_type != 4 )
        return h264_parse_slice_header( bits, sps, pps, slice, nalu_header );
    /* slice_data_partition_b_layer_rbsp() or slice_data_partition_c_layer_rbsp() */
    h264_get_exp_golomb_ue( bits );     /* slice_id */
    if( sps->separate_colour_plane_flag )
        lsmash_bits_get( bits, 2 );     /* colour_plane_id */
    if( pps->redundant_pic_cnt_present_flag )
    {
        uint64_t redundant_pic_cnt = h264_get_exp_golomb_ue( bits );
        IF_INVALID_VALUE( redundant_pic_cnt > 127 )
            return -1;
        slice->has_redundancy = !!redundant_pic_cnt;
    }
    /* Skip slice_data() and rbsp_slice_trailing_bits(). */
    lsmash_bits_empty( bits );
    return bits->bs->error ? -1 : 0;
}

static int h264_calculate_poc( h264_sps_t *sps, h264_picture_info_t *picture, h264_picture_info_t *prev_picture )
{
    int64_t TopFieldOrderCnt    = 0;
    int64_t BottomFieldOrderCnt = 0;
    if( sps->pic_order_cnt_type == 0 )
    {
        int32_t prevPicOrderCntMsb;
        int32_t prevPicOrderCntLsb;
        if( picture->idr )
        {
            prevPicOrderCntMsb = 0;
            prevPicOrderCntLsb = 0;
        }
        else if( prev_picture->ref_pic_has_mmco5 )
        {
            prevPicOrderCntMsb = 0;
            prevPicOrderCntLsb = prev_picture->ref_pic_bottom_field_flag ? 0 : prev_picture->ref_pic_TopFieldOrderCnt;
        }
        else
        {
            prevPicOrderCntMsb = prev_picture->ref_pic_PicOrderCntMsb;
            prevPicOrderCntLsb = prev_picture->ref_pic_PicOrderCntLsb;
        }
        int64_t PicOrderCntMsb;
        int32_t pic_order_cnt_lsb = picture->pic_order_cnt_lsb;
        uint64_t MaxPicOrderCntLsb = sps->MaxPicOrderCntLsb;
        if( (pic_order_cnt_lsb < prevPicOrderCntLsb)
         && ((prevPicOrderCntLsb - pic_order_cnt_lsb) >= (MaxPicOrderCntLsb / 2)) )
            PicOrderCntMsb = prevPicOrderCntMsb + MaxPicOrderCntLsb;
        else if( (pic_order_cnt_lsb > prevPicOrderCntLsb)
         && ((pic_order_cnt_lsb - prevPicOrderCntLsb) > (MaxPicOrderCntLsb / 2)) )
            PicOrderCntMsb = prevPicOrderCntMsb - MaxPicOrderCntLsb;
        else
            PicOrderCntMsb = prevPicOrderCntMsb;
        IF_EXCEED_INT32( PicOrderCntMsb )
            return -1;
        if( !picture->field_pic_flag )
        {
            TopFieldOrderCnt    = PicOrderCntMsb + pic_order_cnt_lsb;
            BottomFieldOrderCnt = TopFieldOrderCnt + picture->delta_pic_order_cnt_bottom;
        }
        else if( picture->bottom_field_flag )
            BottomFieldOrderCnt = PicOrderCntMsb + pic_order_cnt_lsb;
        else
            TopFieldOrderCnt    = PicOrderCntMsb + pic_order_cnt_lsb;
        IF_EXCEED_INT32( TopFieldOrderCnt )
            return -1;
        IF_EXCEED_INT32( BottomFieldOrderCnt )
            return -1;
#if 0
        fprintf( stderr, "PictureOrderCount\n" );
        fprintf( stderr, "    prevPicOrderCntMsb: %"PRId32"\n", prevPicOrderCntMsb );
        fprintf( stderr, "    prevPicOrderCntLsb: %"PRId32"\n", prevPicOrderCntLsb );
        fprintf( stderr, "    PicOrderCntMsb: %"PRId64"\n", PicOrderCntMsb );
        fprintf( stderr, "    pic_order_cnt_lsb: %"PRId32"\n", pic_order_cnt_lsb );
        fprintf( stderr, "    MaxPicOrderCntLsb: %"PRIu64"\n", MaxPicOrderCntLsb );
        fprintf( stderr, "    TopFieldOrderCnt: %"PRId64"\n", TopFieldOrderCnt );
        fprintf( stderr, "    BottomFieldOrderCnt: %"PRId64"\n", BottomFieldOrderCnt );
#endif
        if( !picture->disposable )
        {
            picture->ref_pic_has_mmco5         = picture->has_mmco5;
            picture->ref_pic_bottom_field_flag = picture->bottom_field_flag;
            picture->ref_pic_TopFieldOrderCnt  = TopFieldOrderCnt;
            picture->ref_pic_PicOrderCntMsb    = PicOrderCntMsb;
            picture->ref_pic_PicOrderCntLsb    = pic_order_cnt_lsb;
        }
    }
    else if( sps->pic_order_cnt_type == 1 )
    {
        uint32_t frame_num = picture->frame_num;
        uint32_t prevFrameNum = prev_picture->frame_num;
        uint32_t prevFrameNumOffset = prev_picture->has_mmco5 ? 0 : prev_picture->FrameNumOffset;
        uint64_t FrameNumOffset = picture->idr ? 0 : prevFrameNumOffset + (prevFrameNum > frame_num ? sps->MaxFrameNum : 0);
        IF_INVALID_VALUE( FrameNumOffset > INT32_MAX )
            return -1;
        uint64_t absFrameNum;
        int64_t expectedPicOrderCnt;
        if( sps->num_ref_frames_in_pic_order_cnt_cycle )
        {
            absFrameNum  = FrameNumOffset + frame_num;
            absFrameNum -= picture->disposable && absFrameNum > 0;
            if( absFrameNum )
            {
                uint64_t picOrderCntCycleCnt       = (absFrameNum - 1) / sps->num_ref_frames_in_pic_order_cnt_cycle;
                uint8_t frameNumInPicOrderCntCycle = (absFrameNum - 1) % sps->num_ref_frames_in_pic_order_cnt_cycle;
                expectedPicOrderCnt = picOrderCntCycleCnt * sps->ExpectedDeltaPerPicOrderCntCycle;
                for( uint8_t i = 0; i <= frameNumInPicOrderCntCycle; i++ )
                    expectedPicOrderCnt += sps->offset_for_ref_frame[i];
            }
            else
                expectedPicOrderCnt = 0;
        }
        else
        {
            absFrameNum = 0;
            expectedPicOrderCnt = 0;
        }
        if( picture->disposable )
            expectedPicOrderCnt += sps->offset_for_non_ref_pic;
        if( !picture->field_pic_flag )
        {
            TopFieldOrderCnt    = expectedPicOrderCnt + picture->delta_pic_order_cnt[0];
            BottomFieldOrderCnt = TopFieldOrderCnt    + sps->offset_for_top_to_bottom_field + picture->delta_pic_order_cnt[1];
        }
        else if( picture->bottom_field_flag )
            BottomFieldOrderCnt = expectedPicOrderCnt + sps->offset_for_top_to_bottom_field + picture->delta_pic_order_cnt[0];
        else
            TopFieldOrderCnt    = expectedPicOrderCnt + picture->delta_pic_order_cnt[0];
        IF_EXCEED_INT32( TopFieldOrderCnt )
            return -1;
        IF_EXCEED_INT32( BottomFieldOrderCnt )
            return -1;
    }
    else if( sps->pic_order_cnt_type == 2 )
    {
        uint32_t frame_num = picture->frame_num;
        uint32_t prevFrameNum = prev_picture->frame_num;
        int32_t prevFrameNumOffset = prev_picture->has_mmco5 ? 0 : prev_picture->FrameNumOffset;
        int64_t FrameNumOffset;
        int64_t tempPicOrderCnt;
        if( picture->idr )
        {
            FrameNumOffset  = 0;
            tempPicOrderCnt = 0;
        }
        else
        {
            FrameNumOffset  = prevFrameNumOffset + (prevFrameNum > frame_num ? sps->MaxFrameNum : 0);
            tempPicOrderCnt = 2 * (FrameNumOffset + frame_num) - picture->disposable;
        }
        IF_EXCEED_INT32( FrameNumOffset )
            return -1;
        if( !picture->field_pic_flag )
        {
            TopFieldOrderCnt    = tempPicOrderCnt;
            BottomFieldOrderCnt = tempPicOrderCnt;
        }
        else if( picture->bottom_field_flag )
            BottomFieldOrderCnt = tempPicOrderCnt;
        else
            TopFieldOrderCnt    = tempPicOrderCnt;
        IF_EXCEED_INT32( TopFieldOrderCnt )
            return -1;
        IF_EXCEED_INT32( BottomFieldOrderCnt )
            return -1;
        picture->FrameNumOffset = FrameNumOffset;
    }
    if( !picture->field_pic_flag )
        picture->PicOrderCnt = LSMASH_MIN( TopFieldOrderCnt, BottomFieldOrderCnt );
    else
        picture->PicOrderCnt = picture->bottom_field_flag ? BottomFieldOrderCnt : TopFieldOrderCnt;
#if 0
    fprintf( stderr, "    POC: %"PRId32"\n", picture->PicOrderCnt );
#endif
    return 0;
}

static inline void h264_compare_parameter_set( lsmash_entry_list_t *parameter_sets, uint8_t *ps_nalu, uint16_t ps_nalu_length, int *same_ps )
{
    if( !parameter_sets->head )
        return;
    isom_avcC_ps_entry_t *ps = (isom_avcC_ps_entry_t *)parameter_sets->head->data;
    if( ps && (ps->parameterSetLength == ps_nalu_length) )
        *same_ps = !memcmp( ps->parameterSetNALUnit, ps_nalu, ps_nalu_length );
}

#define H264_NALU_LENGTH_SIZE 4     /* We always use 4 bytes length. */

static lsmash_video_summary_t *h264_create_summary( mp4sys_h264_info_t *info, uint8_t *rbsp_buffer, int probe,
                                                    h264_nalu_header_t *sps_nalu_header,
                                                    uint8_t *sps_nalu, uint16_t sps_nalu_length,
                                                    h264_nalu_header_t *pps_nalu_header,
                                                    uint8_t *pps_nalu, uint16_t pps_nalu_length )
{
    assert( info );
    isom_avcC_t *avcC = &info->avcC;
    int same_sps = 0;
    int same_pps = 0;
    if( sps_nalu )
    {
        h264_compare_parameter_set( avcC->sequenceParameterSets, sps_nalu, sps_nalu_length, &same_sps );
        if( !same_sps )
        {
            if( h264_parse_sps_nalu( info->bits, &info->sps, sps_nalu_header, rbsp_buffer,
                                     sps_nalu + sps_nalu_header->length, sps_nalu_length - sps_nalu_header->length ) )
                return NULL;
            if( !probe || !info->sps.present )
            {
                h264_sps_t *sps = &info->sps;
                avcC->configurationVersion       = 1;
                avcC->AVCProfileIndication       = sps->profile_idc;
                avcC->profile_compatibility      = sps->constraint_set_flags;
                avcC->AVCLevelIndication         = sps->level_idc;
                avcC->lengthSizeMinusOne         = H264_NALU_LENGTH_SIZE - 1;
                avcC->numOfSequenceParameterSets = 1;
                avcC->chroma_format              = sps->chroma_format_idc;
                avcC->bit_depth_luma_minus8      = sps->bit_depth_luma_minus8;
                avcC->bit_depth_chroma_minus8    = sps->bit_depth_chroma_minus8;
                lsmash_remove_entries( avcC->sequenceParameterSets, isom_remove_avcC_ps );
                isom_avcC_ps_entry_t *ps = malloc( sizeof(isom_avcC_ps_entry_t) );
                if( !ps )
                    return NULL;
                ps->parameterSetNALUnit = lsmash_memdup( sps_nalu, sps_nalu_length );
                if( !ps->parameterSetNALUnit )
                {
                    free( ps );
                    return NULL;
                }
                ps->parameterSetLength = sps_nalu_length;
                if( lsmash_add_entry( avcC->sequenceParameterSets, ps ) )
                {
                    free( ps->parameterSetNALUnit );
                    free( ps );
                    return NULL;
                }
                info->sps.present = 1;
            }
        }
    }
    if( pps_nalu )
    {
        h264_compare_parameter_set( avcC->pictureParameterSets, pps_nalu, pps_nalu_length, &same_pps );
        if( !same_pps )
        {
            if( h264_parse_pps_nalu( info->bits, &info->sps, &info->pps, pps_nalu_header, rbsp_buffer,
                                     pps_nalu + pps_nalu_header->length, pps_nalu_length - pps_nalu_header->length ) )
                return NULL;
            if( !probe || !info->pps.present )
            {
                avcC->numOfPictureParameterSets = 1;
                lsmash_remove_entries( avcC->pictureParameterSets, isom_remove_avcC_ps );
                isom_avcC_ps_entry_t *ps = malloc( sizeof(isom_avcC_ps_entry_t) );
                if( !ps )
                    return NULL;
                ps->parameterSetNALUnit = lsmash_memdup( pps_nalu, pps_nalu_length );
                if( !ps->parameterSetNALUnit )
                {
                    free( ps );
                    return NULL;
                }
                ps->parameterSetLength = pps_nalu_length;
                if( lsmash_add_entry( avcC->pictureParameterSets, ps ) )
                {
                    free( ps->parameterSetNALUnit );
                    free( ps );
                    return NULL;
                }
                info->pps.present = 1;
            }
        }
    }
    /* Create summary when SPS, PPS and no summary are present even if probe is true.
     * Skip to create a new summary when detecting the first summary if probe is true, i.e. we hold the first summary. */
    if( !info->sps.present || !info->pps.present || (probe && info->summary) )
        return info->summary;
    if( !probe && ((!sps_nalu && pps_nalu && same_pps) || (!pps_nalu && sps_nalu && same_sps)) )
        return info->summary;
    h264_sps_t *sps = &info->sps;
    h264_pps_t *pps = &info->pps;
    if( sps->seq_parameter_set_id != pps->seq_parameter_set_id )
        return info->summary;       /* No support yet */
    lsmash_video_summary_t *summary;
    if( info->summary )
    {
        summary = info->summary;
        if( summary->exdata )
            free( summary->exdata );
        summary->exdata = NULL;
        info->first_summary = 0;
    }
    else
    {
        summary = (lsmash_video_summary_t *)lsmash_create_summary( MP4SYS_STREAM_TYPE_VisualStream );
        if( !summary )
            return NULL;
        info->first_summary = 1;
    }
    /* Update summary here.
     * max_au_length is set at the last of mp4sys_h264_probe function. */
    summary->sample_type            = ISOM_CODEC_TYPE_AVC1_VIDEO;
    summary->object_type_indication = MP4SYS_OBJECT_TYPE_Visual_H264_ISO_14496_10;
    summary->timescale              = sps->vui.time_scale;
    summary->timebase               = sps->vui.num_units_in_tick;
    summary->full_range             = sps->vui.video_full_range_flag;
    summary->vfr                    = !sps->vui.fixed_frame_rate_flag;
    summary->width                  = sps->cropped_width;
    summary->height                 = sps->cropped_height;
    summary->par_h                  = sps->vui.sar_width;
    summary->par_v                  = sps->vui.sar_height;
    summary->primaries              = sps->vui.colour_primaries;
    summary->transfer               = sps->vui.transfer_characteristics;
    summary->matrix                 = sps->vui.matrix_coefficients;
    /* Export 'avcC' box into exdata. */
    lsmash_bs_t *bs = lsmash_bs_create( NULL );
    if( !bs )
        goto fail;
    lsmash_bs_put_be32( bs, avcC->size );
    lsmash_bs_put_be32( bs, avcC->type );
    lsmash_bs_put_byte( bs, avcC->configurationVersion );
    lsmash_bs_put_byte( bs, avcC->AVCProfileIndication );
    lsmash_bs_put_byte( bs, avcC->profile_compatibility );
    lsmash_bs_put_byte( bs, avcC->AVCLevelIndication );
    lsmash_bs_put_byte( bs, avcC->lengthSizeMinusOne | 0xfc );
    lsmash_bs_put_byte( bs, avcC->numOfSequenceParameterSets | 0xe0 );
    isom_avcC_ps_entry_t *ps = (isom_avcC_ps_entry_t *)avcC->sequenceParameterSets->head->data;
    if( !ps )
        goto fail;
    lsmash_bs_put_be16( bs, ps->parameterSetLength );
    lsmash_bs_put_bytes( bs, ps->parameterSetNALUnit, ps->parameterSetLength );
    lsmash_bs_put_byte( bs, avcC->numOfPictureParameterSets );
    ps = (isom_avcC_ps_entry_t *)avcC->pictureParameterSets->head->data;
    if( !ps )
        goto fail;
    lsmash_bs_put_be16( bs, ps->parameterSetLength );
    lsmash_bs_put_bytes( bs, ps->parameterSetNALUnit, ps->parameterSetLength );
    if( ISOM_REQUIRES_AVCC_EXTENSION( avcC->AVCProfileIndication ) )
    {
        lsmash_bs_put_byte( bs, avcC->chroma_format | 0xfc );
        lsmash_bs_put_byte( bs, avcC->bit_depth_luma_minus8 | 0xf8 );
        lsmash_bs_put_byte( bs, avcC->bit_depth_chroma_minus8 | 0xf8 );
        lsmash_bs_put_byte( bs, avcC->numOfSequenceParameterSetExt );
        /* No SequenceParameterSetExt */
    }
    summary->exdata = lsmash_bs_export_data( bs, &summary->exdata_length );
    lsmash_bs_cleanup( bs );
    /* Update box size. */
    uint8_t *exdata = (uint8_t *)summary->exdata;
    exdata[0] = (summary->exdata_length >> 24) & 0xff;
    exdata[1] = (summary->exdata_length >> 16) & 0xff;
    exdata[2] = (summary->exdata_length >>  8) & 0xff;
    exdata[3] =  summary->exdata_length        & 0xff;
    return summary;
fail:
    lsmash_remove_list( avcC->sequenceParameterSets,   isom_remove_avcC_ps );
    lsmash_remove_list( avcC->pictureParameterSets,    isom_remove_avcC_ps );
    lsmash_remove_list( avcC->sequenceParameterSetExt, isom_remove_avcC_ps );
    lsmash_cleanup_summary( (lsmash_summary_t *)summary );
    lsmash_bs_cleanup( bs );
    return NULL;
}

static inline void h264_update_picture_type( h264_picture_info_t *picture, h264_slice_info_t *slice )
{
    if( picture->type == H264_PICTURE_TYPE_I_P )
    {
        if( slice->type == H264_SLICE_TYPE_B )
            picture->type = H264_PICTURE_TYPE_I_P_B;
        else if( slice->type == H264_SLICE_TYPE_SI || slice->type == H264_SLICE_TYPE_SP )
            picture->type = H264_PICTURE_TYPE_I_SI_P_SP;
    }
    else if( picture->type == H264_PICTURE_TYPE_I_P_B )
    {
        if( slice->type != H264_SLICE_TYPE_P && slice->type != H264_SLICE_TYPE_B && slice->type != H264_SLICE_TYPE_I )
            picture->type = H264_PICTURE_TYPE_I_SI_P_SP_B;
    }
    else if( picture->type == H264_PICTURE_TYPE_I )
    {
        if( slice->type == H264_SLICE_TYPE_P )
            picture->type = H264_PICTURE_TYPE_I_P;
        else if( slice->type == H264_SLICE_TYPE_B )
            picture->type = H264_PICTURE_TYPE_I_P_B;
        else if( slice->type == H264_SLICE_TYPE_SI )
            picture->type = H264_PICTURE_TYPE_I_SI;
        else if( slice->type == H264_SLICE_TYPE_SP )
            picture->type = H264_PICTURE_TYPE_I_SI_P_SP;
    }
    else if( picture->type == H264_PICTURE_TYPE_SI_SP )
    {
        if( slice->type == H264_SLICE_TYPE_P || slice->type == H264_SLICE_TYPE_I )
            picture->type = H264_PICTURE_TYPE_I_SI_P_SP;
        else if( slice->type == H264_SLICE_TYPE_B )
            picture->type = H264_PICTURE_TYPE_I_SI_P_SP_B;
    }
    else if( picture->type == H264_PICTURE_TYPE_SI )
    {
        if( slice->type == H264_SLICE_TYPE_P )
            picture->type = H264_PICTURE_TYPE_I_SI_P_SP;
        else if( slice->type == H264_SLICE_TYPE_B )
            picture->type = H264_PICTURE_TYPE_I_SI_P_SP_B;
        else if( slice->type != H264_SLICE_TYPE_I )
            picture->type = H264_PICTURE_TYPE_I_SI;
        else if( slice->type == H264_SLICE_TYPE_SP )
            picture->type = H264_PICTURE_TYPE_SI_SP;
    }
    else if( picture->type == H264_PICTURE_TYPE_I_SI )
    {
        if( slice->type == H264_SLICE_TYPE_P || slice->type == H264_SLICE_TYPE_SP )
            picture->type = H264_PICTURE_TYPE_I_SI_P_SP;
        else if( slice->type == H264_SLICE_TYPE_B )
            picture->type = H264_PICTURE_TYPE_I_SI_P_SP_B;
    }
    else if( picture->type == H264_PICTURE_TYPE_I_SI_P_SP )
    {
        if( slice->type == H264_SLICE_TYPE_B )
            picture->type = H264_PICTURE_TYPE_I_SI_P_SP_B;
    }
    else if( picture->type == H264_PICTURE_TYPE_NONE )
    {
        if( slice->type == H264_SLICE_TYPE_P )
            picture->type = H264_PICTURE_TYPE_I_P;
        else if( slice->type == H264_SLICE_TYPE_B )
            picture->type = H264_PICTURE_TYPE_I_P_B;
        else if( slice->type == H264_SLICE_TYPE_I )
            picture->type = H264_PICTURE_TYPE_I;
        else if( slice->type == H264_SLICE_TYPE_SI )
            picture->type = H264_PICTURE_TYPE_SI;
        else if( slice->type == H264_SLICE_TYPE_SP )
            picture->type = H264_PICTURE_TYPE_SI_SP;
    }
#if 0
    fprintf( stderr, "Picture type = %s\n", picture->type == H264_PICTURE_TYPE_I_P        ? "P"
                                          : picture->type == H264_PICTURE_TYPE_I_P_B      ? "B"
                                          : picture->type == H264_PICTURE_TYPE_I          ? "I"
                                          : picture->type == H264_PICTURE_TYPE_SI         ? "SI"
                                          : picture->type == H264_PICTURE_TYPE_I_SI       ? "SI"
                                          :                                                 "SP" );
#endif
}

/* Shall be called at least once per picture. */
static void h264_update_picture_info_for_slice( h264_picture_info_t *picture, h264_slice_info_t *slice )
{
    picture->has_mmco5                 |= slice->has_mmco5;
    picture->has_redundancy            |= slice->has_redundancy;
    picture->incomplete_au_has_primary |= !slice->has_redundancy;
    h264_update_picture_type( picture, slice );
    slice->present = 0;     /* Discard this slice info. */
}

/* Shall be called exactly once per picture. */
static void h264_update_picture_info( h264_picture_info_t *picture, h264_slice_info_t *slice, h264_sei_t *sei )
{
    picture->frame_num                  = slice->frame_num;
    picture->pic_order_cnt_lsb          = slice->pic_order_cnt_lsb;
    picture->delta_pic_order_cnt_bottom = slice->delta_pic_order_cnt_bottom;
    picture->delta_pic_order_cnt[0]     = slice->delta_pic_order_cnt[0];
    picture->delta_pic_order_cnt[1]     = slice->delta_pic_order_cnt[1];
    picture->field_pic_flag             = slice->field_pic_flag;
    picture->bottom_field_flag          = slice->bottom_field_flag;
    picture->idr                        = slice->IdrPicFlag;
    picture->pic_parameter_set_id       = slice->pic_parameter_set_id;
    picture->disposable                 = (slice->nal_ref_idc == 0);
    picture->random_accessible          = slice->IdrPicFlag;
    h264_update_picture_info_for_slice( picture, slice );
    picture->independent      = picture->type == H264_PICTURE_TYPE_I || picture->type == H264_PICTURE_TYPE_I_SI;
    picture->non_bipredictive = picture->type != H264_PICTURE_TYPE_I_P_B && picture->type != H264_PICTURE_TYPE_I_SI_P_SP_B;
    if( sei->present )
    {
        picture->random_accessible |= sei->random_accessible;
        picture->recovery_frame_cnt = sei->recovery_frame_cnt;
        sei->present = 0;
    }
}

static inline int h264_find_au_delimit_by_slice_info( h264_slice_info_t *slice, h264_slice_info_t *prev_slice )
{
    if( slice->frame_num                    != prev_slice->frame_num
     || ((slice->pic_order_cnt_type == 0    && prev_slice->pic_order_cnt_type == 0)
      && (slice->pic_order_cnt_lsb          != prev_slice->pic_order_cnt_lsb
      ||  slice->delta_pic_order_cnt_bottom != prev_slice->delta_pic_order_cnt_bottom))
     || ((slice->pic_order_cnt_type == 1    && prev_slice->pic_order_cnt_type == 1)
      && (slice->delta_pic_order_cnt[0]     != prev_slice->delta_pic_order_cnt[0]
      ||  slice->delta_pic_order_cnt[1]     != prev_slice->delta_pic_order_cnt[1]))
     || slice->field_pic_flag               != prev_slice->field_pic_flag
     || slice->bottom_field_flag            != prev_slice->bottom_field_flag
     || slice->IdrPicFlag                   != prev_slice->IdrPicFlag
     || slice->pic_parameter_set_id         != prev_slice->pic_parameter_set_id
     || ((slice->nal_ref_idc == 0           || prev_slice->nal_ref_idc == 0)
      && (slice->nal_ref_idc                != prev_slice->nal_ref_idc))
     || (slice->IdrPicFlag == 1             && prev_slice->IdrPicFlag == 1
      && slice->idr_pic_id                  != prev_slice->idr_pic_id) )
        return 1;
    return 0;
}

static inline int h264_find_au_delimit_by_nalu_type( uint8_t nalu_type, uint8_t prev_nalu_type )
{
    return ((nalu_type >= 6 && nalu_type <= 9) || (nalu_type >= 14 && nalu_type <= 18))
        && ((prev_nalu_type >= 1 && prev_nalu_type <= 5) || prev_nalu_type == 12 || prev_nalu_type == 19);
}

static int h264_supplement_buffer( mp4sys_h264_info_t *info, uint32_t size )
{
    uint32_t buffer_pos_offset   = info->stream_buffer_pos - info->stream_buffer;
    uint32_t buffer_valid_length = info->stream_buffer_end - info->stream_buffer;
    lsmash_multiple_buffers_t *temp = lsmash_resize_multiple_buffers( info->buffers, size );
    if( !temp )
        return -1;
    info->buffers               = temp;
    info->stream_buffer         = lsmash_withdraw_buffer( info->buffers, 1 );
    info->rbsp_buffer           = lsmash_withdraw_buffer( info->buffers, 2 );
    info->picture.au            = lsmash_withdraw_buffer( info->buffers, 3 );
    info->picture.incomplete_au = lsmash_withdraw_buffer( info->buffers, 4 );
    info->stream_buffer_pos = info->stream_buffer + buffer_pos_offset;
    info->stream_buffer_end = info->stream_buffer + buffer_valid_length;
    return 0;
}

static inline int h264_check_next_short_start_code( uint8_t *buf_pos, uint8_t *buf_end )
{
    return ((buf_pos + 2) < buf_end) && !buf_pos[0] && !buf_pos[1] && (buf_pos[2] == 0x01);
}

static void h264_check_buffer_shortage( mp4sys_importer_t *importer, uint32_t anticipation_bytes )
{
    mp4sys_h264_info_t *info = (mp4sys_h264_info_t *)importer->info;
    assert( anticipation_bytes < info->buffers->buffer_size );
    if( info->no_more_read )
        return;
    uint32_t remainder_bytes = info->stream_buffer_end - info->stream_buffer_pos;
    if( remainder_bytes <= anticipation_bytes )
    {
        /* Move unused data to the head of buffer. */
        for( uint32_t i = 0; i < remainder_bytes; i++ )
            *(info->stream_buffer + i) = *(info->stream_buffer_pos + i);
        /* Read and store the next data into the buffer.
         * Move the position of buffer on the head. */
        uint32_t read_size = fread( info->stream_buffer + remainder_bytes, 1, info->buffers->buffer_size - remainder_bytes, importer->stream );
        info->stream_buffer_pos = info->stream_buffer;
        info->stream_buffer_end = info->stream_buffer + remainder_bytes + read_size;
        info->no_more_read = read_size == 0 ? feof( importer->stream ) : 0;
    }
}

static inline int h264_complete_au( h264_picture_info_t *picture, int probe )
{
    if( !picture->incomplete_au_has_primary || picture->incomplete_au_length == 0 )
        return 0;
    if( !probe )
        memcpy( picture->au, picture->incomplete_au, picture->incomplete_au_length );
    picture->au_length                 = picture->incomplete_au_length;
    picture->incomplete_au_length      = 0;
    picture->incomplete_au_has_primary = 0;
    return 1;
}

static void h264_append_nalu_to_au( h264_picture_info_t *picture, uint8_t *src_nalu, uint32_t nalu_length, int probe )
{
    if( !probe )
    {
        uint8_t *dst_nalu = picture->incomplete_au + picture->incomplete_au_length + H264_NALU_LENGTH_SIZE;
        for( int i = H264_NALU_LENGTH_SIZE; i; i-- )
            *(dst_nalu - i) = (nalu_length >> ((i - 1) * 8)) & 0xff;
        memcpy( dst_nalu, src_nalu, nalu_length );
    }
    /* Note: picture->incomplete_au_length shall be 0 immediately after AU has completed.
     * Therefore, possible_au_length in h264_get_access_unit_internal() can't be used here
     * to avoid increasing AU length monotonously through the entire stream. */
    picture->incomplete_au_length += H264_NALU_LENGTH_SIZE + nalu_length;
}

static inline void h264_get_au_internal_end( mp4sys_h264_info_t *info, h264_picture_info_t *picture, h264_nalu_header_t *nalu_header, int no_more_buf )
{
    info->status = info->no_more_read && no_more_buf && (picture->incomplete_au_length == 0)
                 ? MP4SYS_IMPORTER_EOF
                 : MP4SYS_IMPORTER_OK;
    info->nalu_header = *nalu_header;
}

static int h264_get_au_internal_succeeded( mp4sys_h264_info_t *info, h264_picture_info_t *picture, h264_nalu_header_t *nalu_header, int no_more_buf )
{
    h264_get_au_internal_end( info, picture, nalu_header, no_more_buf );
    picture->au_number += 1;
    return 0;
}

static int h264_get_au_internal_failed( mp4sys_h264_info_t *info, h264_picture_info_t *picture, h264_nalu_header_t *nalu_header, int no_more_buf, int complete_au )
{
    h264_get_au_internal_end( info, picture, nalu_header, no_more_buf );
    if( complete_au )
        picture->au_number += 1;
    return -1;
}


/* If probe equals 0, don't get the actual data (EBPS) of an access unit and only parse NALU.
 * Currently, you can get AU of AVC video elemental stream only, not AVC parameter set elemental stream defined in 14496-15. */
static int h264_get_access_unit_internal( mp4sys_importer_t *importer, mp4sys_h264_info_t *info, uint32_t track_number, int probe )
{
#define H264_SHORT_START_CODE_LENGTH 3
    h264_slice_info_t *slice       = &info->slice;
    h264_picture_info_t *picture   = &info->picture;
    h264_nalu_header_t nalu_header = info->nalu_header;
    uint64_t consecutive_zero_byte_count = 0;
    uint64_t ebsp_length = 0;
    int      no_more_buf = 0;
    int      complete_au = 0;
    picture->au_length          = 0;
    picture->type               = H264_PICTURE_TYPE_NONE;
    picture->random_accessible  = 0;
    picture->recovery_frame_cnt = 0;
    picture->has_mmco5          = 0;
    picture->has_redundancy     = 0;
    while( 1 )
    {
        h264_check_buffer_shortage( importer, 2 );
        no_more_buf = info->stream_buffer_pos >= info->stream_buffer_end;
        int no_more = info->no_more_read && no_more_buf;
        if( h264_check_next_short_start_code( info->stream_buffer_pos, info->stream_buffer_end ) || no_more )
        {
            if( no_more && ebsp_length == 0 )
            {
                /* For the last NALU.
                 * This NALU already has been appended into the latest access unit and parsed. */
                ebsp_length = picture->incomplete_au_length - (H264_NALU_LENGTH_SIZE + nalu_header.length);
                consecutive_zero_byte_count = 0;
                h264_update_picture_info( picture, slice, &info->sei );
                h264_complete_au( picture, probe );
                return h264_get_au_internal_succeeded( info, picture, &nalu_header, no_more_buf );
            }
            uint64_t next_nalu_head_pos = info->ebsp_head_pos + ebsp_length + !no_more * H264_SHORT_START_CODE_LENGTH;
            uint8_t *next_short_start_code_pos = info->stream_buffer_pos;   /* Memorize position of short start code of the next NALU in buffer.
                                                                             * This is used when backward reading of stream doesn't occur. */
            uint8_t nalu_type = nalu_header.nal_unit_type;
            int read_back = 0;
#if 0
            if( probe )
            {
                fprintf( stderr, "NALU type: %"PRIu8"\n", nalu_type );
                fprintf( stderr, "    NALU header position: %"PRIx64"\n", info->ebsp_head_pos - nalu_header.length );
                fprintf( stderr, "    EBSP position: %"PRIx64"\n", info->ebsp_head_pos );
                fprintf( stderr, "    EBSP length: %"PRIx64" (%"PRIu64")\n", ebsp_length - consecutive_zero_byte_count,
                                                                             ebsp_length - consecutive_zero_byte_count );
                fprintf( stderr, "    consecutive_zero_byte_count: %"PRIx64"\n", consecutive_zero_byte_count );
                fprintf( stderr, "    Next NALU header position: %"PRIx64"\n", next_nalu_head_pos );
            }
#endif
            if( nalu_type == 12 )
            {
                /* We don't support streams with both filler and HRD yet.
                 * Otherwise, just skip filler because elemental streams defined in 14496-15 are forbidden to use filler. */
                if( info->sps.hrd_present )
                    return h264_get_au_internal_failed( info, picture, &nalu_header, no_more_buf, complete_au );
            }
            else if( (nalu_type >= 1 && nalu_type <= 13) || nalu_type == 19 )
            {
                /* Get the EBSP of the current NALU here.
                 * AVC elemental stream defined in 14496-15 can recognizes from 0 to 13, and 19 of nal_unit_type.
                 * We don't support SVC and MVC elemental stream defined in 14496-15 yet. */
                ebsp_length -= consecutive_zero_byte_count;     /* Any EBSP doesn't have zero bytes at the end. */
                uint64_t nalu_length = nalu_header.length + ebsp_length;
                uint64_t possible_au_length = picture->incomplete_au_length + H264_NALU_LENGTH_SIZE + nalu_length;
                if( info->buffers->buffer_size < possible_au_length )
                {
                    if( h264_supplement_buffer( info, 2 * possible_au_length ) )
                        h264_get_au_internal_failed( info, picture, &nalu_header, no_more_buf, complete_au );
                    next_short_start_code_pos = info->stream_buffer_pos;
                }
                /* Move to the first byte of the current NALU. */
                read_back = (info->stream_buffer_pos - info->stream_buffer) < (nalu_length + consecutive_zero_byte_count);
                if( read_back )
                {
                    lsmash_fseek( importer->stream, info->ebsp_head_pos - nalu_header.length, SEEK_SET );
                    int read_fail = fread( info->stream_buffer, 1, nalu_length, importer->stream ) != nalu_length;
                    info->stream_buffer_pos = info->stream_buffer;
                    info->stream_buffer_end = info->stream_buffer + nalu_length;
                    if( read_fail )
                        h264_get_au_internal_failed( info, picture, &nalu_header, no_more_buf, complete_au );
#if 0
                    if( probe )
                        fprintf( stderr, "    ----Read Back\n" );
#endif
                }
                else
                    info->stream_buffer_pos -= nalu_length + consecutive_zero_byte_count;
                if( nalu_type >= 1 && nalu_type <= 5 )
                {
                    /* VCL NALU (slice) */
                    h264_slice_info_t prev_slice = *slice;
                    if( h264_parse_slice( info->bits, &info->sps, &info->pps,
                                          slice, &nalu_header, info->rbsp_buffer,
                                          info->stream_buffer_pos + nalu_header.length, ebsp_length ) )
                        return h264_get_au_internal_failed( info, picture, &nalu_header, no_more_buf, complete_au );
                    if( prev_slice.present )
                    {
                        /* Check whether the AU that contains the previous VCL NALU completed or not. */
                        if( h264_find_au_delimit_by_slice_info( slice, &prev_slice ) )
                        {
                            /* The current NALU is the first VCL NALU of the primary coded picture of an new AU.
                             * Therefore, the previous slice belongs to the AU you want at this time. */
                            h264_update_picture_info( picture, &prev_slice, &info->sei );
                            complete_au = h264_complete_au( picture, probe );
                        }
                        else
                            h264_update_picture_info_for_slice( picture, &prev_slice );
                    }
                    h264_append_nalu_to_au( picture, info->stream_buffer_pos, nalu_length, probe );
                    slice->present = 1;
                }
                else
                {
                    if( h264_find_au_delimit_by_nalu_type( nalu_type, info->prev_nalu_type ) )
                    {
                        /* The last slice belongs to the AU you want at this time. */
                        h264_update_picture_info( picture, slice, &info->sei );
                        complete_au = h264_complete_au( picture, probe );
                    }
                    else if( no_more )
                        complete_au = h264_complete_au( picture, probe );
                    switch( nalu_type )
                    {
                        case 7 :    /* SPS */
                            info->summary = h264_create_summary( info, info->rbsp_buffer, probe,
                                                                 &nalu_header, info->stream_buffer_pos, nalu_length,
                                                                 NULL, NULL, 0 );
                            break;
                        case 8 :    /* PPS */
                            info->summary = h264_create_summary( info, info->rbsp_buffer, probe,
                                                                 NULL, NULL, 0,
                                                                 &nalu_header, info->stream_buffer_pos, nalu_length );
                            break;
                        case 9 :    /* We drop access unit delimiters. */
                            break;
                        case 13 :   /* We don't support sequence parameter set extension yet. */
                            return h264_get_au_internal_failed( info, picture, &nalu_header, no_more_buf, complete_au );
                        case 6 :    /* SEI */
                            if( h264_parse_sei_nalu( info->bits, &info->sei, &nalu_header,
                                                     info->rbsp_buffer, info->stream_buffer_pos + nalu_header.length, ebsp_length ) )
                                return h264_get_au_internal_failed( info, picture, &nalu_header, no_more_buf, complete_au );
                            /* Don't break here.
                             * Append this SEI NALU to access unit. */
                        default :
                            h264_append_nalu_to_au( picture, info->stream_buffer_pos, nalu_length, probe );
                            break;
                    }
                }
            }
            /* Move to the first byte of the next NALU. */
            if( read_back )
            {
                lsmash_fseek( importer->stream, next_nalu_head_pos, SEEK_SET );
                info->stream_buffer_pos = info->stream_buffer;
                info->stream_buffer_end = info->stream_buffer + fread( info->stream_buffer, 1, info->buffers->buffer_size, importer->stream );
            }
            else
                info->stream_buffer_pos = next_short_start_code_pos + H264_SHORT_START_CODE_LENGTH;
            info->prev_nalu_type = nalu_type;
            h264_check_buffer_shortage( importer, 0 );
            no_more_buf = info->stream_buffer_pos >= info->stream_buffer_end;
            ebsp_length = 0;
            no_more = info->no_more_read && no_more_buf;
            if( !no_more )
            {
                /* Check the next NALU header. */
                if( h264_check_nalu_header( &nalu_header, &info->stream_buffer_pos, !!consecutive_zero_byte_count ) )
                    return h264_get_au_internal_failed( info, picture, &nalu_header, no_more_buf, complete_au );
                info->ebsp_head_pos = next_nalu_head_pos + nalu_header.length;
            }
            /* If there is no more data in the stream, and flushed chunk of NALUs, flush it as complete AU here. */
            else if( picture->incomplete_au_length && picture->au_length == 0 )
            {
                h264_update_picture_info( picture, slice, &info->sei );
                h264_complete_au( picture, probe );
                return h264_get_au_internal_succeeded( info, picture, &nalu_header, no_more_buf );
            }
            if( complete_au )
                return h264_get_au_internal_succeeded( info, picture, &nalu_header, no_more_buf );
            consecutive_zero_byte_count = 0;
            continue;       /* Avoid increment of ebsp_length. */
        }
        else if( !no_more )
        {
            if( *info->stream_buffer_pos ++ )
                consecutive_zero_byte_count = 0;
            else
                ++consecutive_zero_byte_count;
        }
        ++ebsp_length;
    }
#undef H264_SHORT_START_CODE_LENGTH
}

#undef H264_NALU_LENGTH_SIZE

static int mp4sys_h264_get_accessunit( mp4sys_importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    mp4sys_h264_info_t *info = (mp4sys_h264_info_t *)importer->info;
    mp4sys_importer_status current_status = info->status;
    if( current_status == MP4SYS_IMPORTER_ERROR || buffered_sample->length < info->max_au_length )
        return -1;
    if( current_status == MP4SYS_IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( info->summary && !info->first_summary )
    {
        current_status = MP4SYS_IMPORTER_CHANGE;
        /* Summaries may be not active immediately because we can't get any corresponding AU at once.
         * The first summary will be an only exception of this. */
        lsmash_entry_t* entry = lsmash_get_entry( importer->summaries, track_number );
        if( !entry || !entry->data )
            return -1;
        lsmash_cleanup_summary( entry->data );
        entry->data = info->summary;
        info->summary = NULL;
    }
    if( h264_get_access_unit_internal( importer, info, track_number, 0 ) )
    {
        info->status = MP4SYS_IMPORTER_ERROR;
        return -1;
    }
    h264_sps_t *sps = &info->sps;
    h264_picture_info_t *picture = &info->picture;
    buffered_sample->dts = info->ts_list.timestamp[picture->au_number - 1].dts;
    buffered_sample->cts = info->ts_list.timestamp[picture->au_number - 1].cts;
    if( picture->au_number < info->num_undecodable )
        buffered_sample->prop.leading = ISOM_SAMPLE_IS_UNDECODABLE_LEADING;
    else
        buffered_sample->prop.leading = picture->non_bipredictive || buffered_sample->cts >= info->last_intra_cts
                                      ? ISOM_SAMPLE_IS_NOT_LEADING : ISOM_SAMPLE_IS_UNDECODABLE_LEADING;
    if( picture->independent )
        info->last_intra_cts = buffered_sample->cts;
    if( info->composition_reordering_present && !picture->disposable && !picture->idr )
        buffered_sample->prop.allow_earlier = QT_SAMPLE_EARLIER_PTS_ALLOWED;
    buffered_sample->prop.independent = picture->independent    ? ISOM_SAMPLE_IS_INDEPENDENT : ISOM_SAMPLE_IS_NOT_INDEPENDENT;
    buffered_sample->prop.disposable  = picture->disposable     ? ISOM_SAMPLE_IS_DISPOSABLE  : ISOM_SAMPLE_IS_NOT_DISPOSABLE;
    buffered_sample->prop.redundant   = picture->has_redundancy ? ISOM_SAMPLE_HAS_REDUNDANCY : ISOM_SAMPLE_HAS_NO_REDUNDANCY;
    buffered_sample->prop.post_roll.identifier = picture->frame_num;
    if( picture->random_accessible )
    {
        if( picture->idr )
            buffered_sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
        else if( picture->recovery_frame_cnt )
        {
            buffered_sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_RECOVERY;
            buffered_sample->prop.post_roll.complete = (picture->frame_num + picture->recovery_frame_cnt) % sps->MaxFrameNum;
        }
        else
            buffered_sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_OPEN_RAP;
    }
    buffered_sample->length = picture->au_length;
    memcpy( buffered_sample->data, picture->au, picture->au_length );
    /* Return 1 if a new summary is detected. */
    return current_status == MP4SYS_IMPORTER_CHANGE;
}

static int mp4sys_h264_probe( mp4sys_importer_t *importer )
{
#define H264_LONG_START_CODE_LENGTH  4
#define H264_CHECK_NEXT_LONG_START_CODE( x ) (!(x)[0] && !(x)[1] && !(x)[2] && ((x)[3] == 0x01))
    /* Find the first start code. */
    mp4sys_h264_info_t *info = mp4sys_create_h264_info();
    if( !info )
        return -1;
    info->stream_buffer_pos = info->stream_buffer;
    info->stream_buffer_end = info->stream_buffer + fread( info->stream_buffer, 1, info->buffers->buffer_size, importer->stream );
    info->no_more_read = info->stream_buffer >= info->stream_buffer_end ? feof( importer->stream ) : 0;
    while( 1 )
    {
        /* Invalid if encountered any value of non-zero before the first start code. */
        IF_INVALID_VALUE( *info->stream_buffer_pos )
            goto fail;
        /* The first NALU of an AU in decoding order shall have long start code (0x00000001). */
        if( H264_CHECK_NEXT_LONG_START_CODE( info->stream_buffer_pos ) )
            break;
        /* If the first trial of finding long start code failed, we assume this stream is not byte stream format of H.264. */
        if( (info->stream_buffer_pos + H264_LONG_START_CODE_LENGTH) == info->stream_buffer_end )
            goto fail;
        ++info->stream_buffer_pos;
    }
    /* OK. It seems the stream has a long start code of H.264. */
    importer->info = info;
    info->stream_buffer_pos += H264_LONG_START_CODE_LENGTH;
    h264_check_buffer_shortage( importer, 0 );
    h264_nalu_header_t first_nalu_header;
    if( h264_check_nalu_header( &first_nalu_header, &info->stream_buffer_pos, 1 ) )
        goto fail;
    uint64_t first_ebsp_head_pos = info->stream_buffer_pos - info->stream_buffer;   /* EBSP doesn't include NALU header. */
    info->status        = info->no_more_read ? MP4SYS_IMPORTER_EOF : MP4SYS_IMPORTER_OK;
    info->nalu_header   = first_nalu_header;
    info->ebsp_head_pos = first_ebsp_head_pos;
    /* Parse all NALU in the stream for preparation of calculating timestamps. */
    uint32_t poc_alloc = (1 << 12) * sizeof(uint64_t);
    uint64_t *poc = malloc( poc_alloc );
    if( !poc )
        goto fail;
    uint32_t num_access_units = 0;
    fprintf( stderr, "Analyzing stream as H.264\r" );
    while( info->status != MP4SYS_IMPORTER_EOF )
    {
#if 0
        fprintf( stderr, "Analyzing stream as H.264: %"PRIu32"\n", num_access_units + 1 );
#endif
        h264_picture_info_t prev_picture = info->picture;
        if( h264_get_access_unit_internal( importer, info, 0, 1 ) )
            goto fail;
        if( h264_calculate_poc( &info->sps, &info->picture, &prev_picture ) )
            goto fail;
        if( poc_alloc <= num_access_units * sizeof(uint64_t) )
        {
            uint32_t alloc = 2 * num_access_units * sizeof(uint64_t);
            uint64_t *temp = realloc( poc, alloc );
            if( !temp )
            {
                free( poc );
                goto fail;
            }
            poc = temp;
            poc_alloc = alloc;
        }
        poc[num_access_units++] = info->picture.PicOrderCnt;
        info->max_au_length = LSMASH_MAX( info->picture.au_length, info->max_au_length );
    }
    fprintf( stderr, "                                                                               \r" );
    if( !info->summary || lsmash_add_entry( importer->summaries, info->summary ) )
        goto fail;
    lsmash_media_ts_t *timestamp = malloc( num_access_units * sizeof(lsmash_media_ts_t) );
    if( !timestamp )
    {
        free( poc );
        goto fail;
    }
    /* Make zero-origin. */
    int32_t min_poc = poc[0];
    for( uint32_t i = 1; i < num_access_units; i++ )
        min_poc = LSMASH_MIN( (int32_t)poc[i], min_poc );
    if( min_poc )
        for( uint32_t i = 0; i < num_access_units; i++ )
            poc[i] -= min_poc;
    /* Deduplicate POCs. */
    uint64_t poc_offset = 0;
    uint64_t poc_max = 0;
    for( uint32_t i = 1; i < num_access_units; i++ )
    {
        if( poc[i] == 0 )
        {
            poc_offset += poc_max + 1;
            poc_max = 0;
        }
        else
            poc_max = LSMASH_MAX( poc[i], poc_max );
        poc[i] += poc_offset;
    }
    /* Count leading samples that are undecodable. */
    for( uint32_t i = 0; i < num_access_units; i++ )
    {
        if( poc[i] == 0 )
            break;
        ++info->num_undecodable;
    }
    /* Get max composition delay. */
    uint32_t composition_delay = 0;
    uint32_t max_composition_delay = 0;
    for( uint32_t i = 1; i < num_access_units; i++ )
        if( poc[i] < poc[i - 1] )
        {
            ++composition_delay;
            max_composition_delay = LSMASH_MAX( max_composition_delay, composition_delay );
        }
        else
            composition_delay = 0;
    /* Generate timestamps. */
    if( max_composition_delay )
    {
        for( uint32_t i = 0; i < num_access_units; i++ )
        {
            timestamp[i].cts = poc[i];
            timestamp[i].dts = i;
        }
        qsort( timestamp, num_access_units, sizeof(lsmash_media_ts_t), (int(*)( const void *, const void * ))lsmash_compare_cts );
        for( uint32_t i = 0; i < num_access_units; i++ )
            timestamp[i].cts = i + max_composition_delay;
        qsort( timestamp, num_access_units, sizeof(lsmash_media_ts_t), (int(*)( const void *, const void * ))lsmash_compare_dts );
    }
    else
        for( uint32_t i = 0; i < num_access_units; i++ )
            timestamp[i].cts = timestamp[i].dts = i;
    free( poc );
#if 0
    for( uint32_t i = 0; i < num_access_units; i++ )
        fprintf( stderr, "Timestamp[%"PRIu32"]: DTS=%"PRIu64", CTS=%"PRIu64"\n", i, timestamp[i].dts, timestamp[i].cts );
#endif
    info->ts_list.sample_count           = num_access_units;
    info->ts_list.timestamp              = timestamp;
    info->composition_reordering_present = !!max_composition_delay;
    /* Go back to EBSP of the first NALU. */
    lsmash_fseek( importer->stream, first_ebsp_head_pos, SEEK_SET );
    info->status                 = MP4SYS_IMPORTER_OK;
    info->nalu_header            = first_nalu_header;
    info->prev_nalu_type         = 0;
    info->no_more_read           = 0;
    info->first_summary          = 0;
    info->summary->max_au_length = info->max_au_length;
    info->summary                = NULL;
    info->stream_buffer_pos      = info->stream_buffer;
    info->stream_buffer_end      = info->stream_buffer + fread( info->stream_buffer, 1, info->buffers->buffer_size, importer->stream );
    info->ebsp_head_pos          = first_ebsp_head_pos;
    uint8_t *temp_au             = info->picture.au;
    uint8_t *temp_incomplete_au  = info->picture.incomplete_au;
    memset( &info->picture, 0, sizeof(h264_picture_info_t) );
    info->picture.au            = temp_au;
    info->picture.incomplete_au = temp_incomplete_au;
    memset( &info->slice, 0, sizeof(h264_slice_info_t) );
    memset( &info->sps, 0, sizeof(h264_sps_t) );
    memset( &info->pps, 0, sizeof(h264_pps_t) );
    lsmash_remove_entries( info->avcC.sequenceParameterSets,   isom_remove_avcC_ps );
    lsmash_remove_entries( info->avcC.pictureParameterSets,    isom_remove_avcC_ps );
    lsmash_remove_entries( info->avcC.sequenceParameterSetExt, isom_remove_avcC_ps );
    importer->info = info;
    return 0;
fail:
    mp4sys_remove_h264_info( info );
    lsmash_remove_entries( importer->summaries, lsmash_cleanup_summary );
    return -1;
#undef H264_LONG_START_CODE_LENGTH
#undef H264_CHECK_NEXT_LONG_START_CODE
}

static uint32_t mp4sys_h264_get_last_delta( mp4sys_importer_t* importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    mp4sys_h264_info_t *info = (mp4sys_h264_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != MP4SYS_IMPORTER_EOF )
        return 0;
    return info->ts_list.sample_count > 1
         ? 1
         : UINT32_MAX;    /* arbitrary */
}

const static mp4sys_importer_functions mp4sys_h264_importer =
{
    "H.264",
    1,
    mp4sys_h264_probe,
    mp4sys_h264_get_accessunit,
    mp4sys_h264_get_last_delta,
    mp4sys_h264_cleanup
};

/***************************************************************************
    SMPTE VC-1 importer (only for Advanced Profile)
***************************************************************************/

typedef struct
{
    uint8_t hrd_num_leaky_buckets;
} vc1_hrd_param_t;

typedef struct
{
    uint8_t  present;
    uint8_t  profile;
    uint8_t  level;
    uint8_t  colordiff_format;      /* currently 4:2:0 only */
    uint8_t  interlace;
    uint8_t  color_prim;
    uint8_t  transfer_char;
    uint8_t  matrix_coef;
    uint8_t  hrd_param_flag;
    uint8_t  aspect_width;
    uint8_t  aspect_height;
    uint8_t  framerate_flag;
    uint32_t framerate_numerator;
    uint32_t framerate_denominator;
    uint16_t max_coded_width;
    uint16_t max_coded_height;
    uint16_t disp_horiz_size;
    uint16_t disp_vert_size;
    vc1_hrd_param_t hrd_param;
    uint8_t *ebdu;
    uint32_t length;
} vc1_sequence_header_t;

typedef struct
{
    uint8_t  present;
    uint8_t  closed_entry_point;
    uint8_t *ebdu;
    uint32_t length;
} vc1_entry_point_t;

typedef struct
{
    uint8_t present;
    uint8_t frame_coding_mode;
    uint8_t type;
    uint8_t closed_gop;
    uint8_t start_of_sequence;
    uint8_t random_accessible;
} vc1_picture_info_t;

typedef struct
{
    uint8_t  random_accessible;
    uint8_t  closed_gop;
    uint8_t  independent;
    uint8_t  non_bipredictive;
    uint8_t  disposable;
    uint8_t *data;
    uint32_t data_length;
    uint8_t *incomplete_data;
    uint32_t incomplete_data_length;
    uint32_t number;
} vc1_access_unit_t;

typedef struct
{
    mp4sys_importer_status status;
    lsmash_video_summary_t *summary;
    uint8_t bdu_type;
    uint8_t prev_bdu_type;
    uint8_t no_more_read;
    uint8_t composition_reordering_present;
    uint8_t slice_present;
    uint8_t multiple_sequence;
    uint8_t multiple_entry_point;
    vc1_sequence_header_t first_sequence;
    vc1_sequence_header_t sequence;
    vc1_entry_point_t first_entry_point;
    vc1_entry_point_t entry_point;
    vc1_picture_info_t next_picture;
    vc1_access_unit_t access_unit;
    lsmash_bits_t *bits;
    lsmash_multiple_buffers_t *buffers;
    uint8_t *rbdu_buffer;
    uint8_t *stream_buffer;
    uint8_t *stream_buffer_pos;
    uint8_t *stream_buffer_end;
    uint64_t ebdu_head_pos;
    uint32_t max_au_length;
    uint32_t num_undecodable;
    uint64_t last_ref_intra_cts;
    lsmash_media_ts_list_t ts_list;
} mp4sys_vc1_info_t;

#define VC1_START_CODE_PREFIX_LENGTH 3      /* 0x000001 */
#define VC1_START_CODE_SUFFIX_LENGTH 1      /* BDU type */
#define VC1_START_CODE_LENGTH (VC1_START_CODE_PREFIX_LENGTH + VC1_START_CODE_SUFFIX_LENGTH)     /* = 4 */

enum
{
    VC1_ADVANCED_PICTURE_TYPE_I       = 0x6,        /* 0b110 */
    VC1_ADVANCED_PICTURE_TYPE_P       = 0x0,        /* 0b0 */
    VC1_ADVANCED_PICTURE_TYPE_B       = 0x2,        /* 0b10 */
    VC1_ADVANCED_PICTURE_TYPE_BI      = 0xE,        /* 0b1110 */
    VC1_ADVANCED_PICTURE_TYPE_SKIPPED = 0xF,        /* 0b1111 */
} vc1_picture_type;

enum
{
    VC1_ADVANCED_FIELD_PICTURE_TYPE_II   = 0x0,     /* 0b000 */
    VC1_ADVANCED_FIELD_PICTURE_TYPE_IP   = 0x1,     /* 0b001 */
    VC1_ADVANCED_FIELD_PICTURE_TYPE_PI   = 0x2,     /* 0b010 */
    VC1_ADVANCED_FIELD_PICTURE_TYPE_PP   = 0x3,     /* 0b011 */
    VC1_ADVANCED_FIELD_PICTURE_TYPE_BB   = 0x4,     /* 0b100 */
    VC1_ADVANCED_FIELD_PICTURE_TYPE_BBI  = 0x5,     /* 0b101 */
    VC1_ADVANCED_FIELD_PICTURE_TYPE_BIB  = 0x6,     /* 0b110 */
    VC1_ADVANCED_FIELD_PICTURE_TYPE_BIBI = 0x7,     /* 0b111 */
} vc1_field_picture_type;

enum
{
    VC1_FRAME_CODING_MODE_PROGRESSIVE     = 0x0,    /* 0b0 */
    VC1_FRAME_CODING_MODE_FRAME_INTERLACE = 0x2,    /* 0b10 */
    VC1_FRAME_CODING_MODE_FIELD_INTERLACE = 0x3,    /* 0b11 */
} vc1_frame_coding_mode;

#define MP4SYS_VC1_DEFAULT_BUFFER_SIZE (1<<16)

static void mp4sys_remove_vc1_info( mp4sys_vc1_info_t *info )
{
    if( !info )
        return;
    lsmash_bits_adhoc_cleanup( info->bits );
    lsmash_destroy_multiple_buffers( info->buffers );
    if( info->ts_list.timestamp )
        free( info->ts_list.timestamp );
    if( info->sequence.ebdu )
        free( info->sequence.ebdu );
    if( info->entry_point.ebdu )
        free( info->entry_point.ebdu );
    free( info );
}

static mp4sys_vc1_info_t *mp4sys_create_vc1_info( void )
{
    mp4sys_vc1_info_t *info = lsmash_malloc_zero( sizeof(mp4sys_vc1_info_t) );
    if( !info )
        return NULL;
    info->bits = lsmash_bits_adhoc_create();
    if( !info->bits )
    {
        mp4sys_remove_vc1_info( info );
        return NULL;
    }
    info->buffers = lsmash_create_multiple_buffers( 4, MP4SYS_VC1_DEFAULT_BUFFER_SIZE );
    if( !info->buffers )
    {
        mp4sys_remove_vc1_info( info );
        return NULL;
    }
    info->stream_buffer               = lsmash_withdraw_buffer( info->buffers, 1 );
    info->rbdu_buffer                 = lsmash_withdraw_buffer( info->buffers, 2 );
    info->access_unit.data            = lsmash_withdraw_buffer( info->buffers, 3 );
    info->access_unit.incomplete_data = lsmash_withdraw_buffer( info->buffers, 4 );
    return info;
}

static void mp4sys_vc1_cleanup( mp4sys_importer_t *importer )
{
    debug_if( importer && importer->info )
        mp4sys_remove_vc1_info( importer->info );
}

/* Convert EBDU (Encapsulated Byte Data Unit) to RBDU (Raw Byte Data Unit). */
static void vc1_remove_emulation_prevention( uint8_t *src, uint64_t src_length, uint8_t **p_dst )
{
    uint8_t *src_end = src + src_length;
    uint8_t *dst = *p_dst;
    while( src < src_end )
        if( ((src + 2) < src_end) && !src[0] && !src[1] && (src[2] == 0x03) )
        {
            *dst++ = *src++;
            *dst++ = *src++;
            src++;  /* Skip emulation_prevention_three_byte (0x03). */
        }
        else
            *dst++ = *src++;
    *p_dst = dst;
}

static int vc1_bits_import_rbdu_from_ebdu( lsmash_bits_t *bits, uint8_t *rbdu_buffer, uint8_t *ebdu, uint64_t ebdu_length )
{
    uint8_t *rbdu_start = rbdu_buffer;
    vc1_remove_emulation_prevention( ebdu + VC1_START_CODE_LENGTH, ebdu_length - VC1_START_CODE_LENGTH, &rbdu_buffer );
    uint64_t rbdu_length = rbdu_buffer - rbdu_start;
    return lsmash_bits_import_data( bits, rbdu_start, rbdu_length );
}

static void vc1_parse_hrd_param( lsmash_bits_t *bits, vc1_hrd_param_t *hrd_param )
{
    hrd_param->hrd_num_leaky_buckets = lsmash_bits_get( bits, 5 );
    lsmash_bits_get( bits, 4 );     /* bitrate_exponent */
    lsmash_bits_get( bits, 4 );     /* buffer_size_exponent */
    for( uint8_t i = 0; i < hrd_param->hrd_num_leaky_buckets; i++ )
    {
        lsmash_bits_get( bits, 16 );    /* hrd_rate */
        lsmash_bits_get( bits, 16 );    /* hrd_buffer */
    }
}

static int vc1_parse_sequence_header( mp4sys_vc1_info_t *info, uint8_t *ebdu, uint64_t ebdu_length, int probe )
{
    lsmash_bits_t *bits = info->bits;
    vc1_sequence_header_t *sequence = &info->sequence;
    if( vc1_bits_import_rbdu_from_ebdu( bits, info->rbdu_buffer, ebdu, ebdu_length ) )
        return -1;
    memset( sequence, 0, sizeof(vc1_sequence_header_t) );
    sequence->profile          = lsmash_bits_get( bits, 2 );
    if( sequence->profile != 3 )
        return -1;      /* SMPTE Reserved */
    sequence->level            = lsmash_bits_get( bits, 3 );
    if( sequence->level > 4 )
        return -1;      /* SMPTE Reserved */
    sequence->colordiff_format = lsmash_bits_get( bits, 2 );
    if( sequence->colordiff_format != 1 )
        return -1;      /* SMPTE Reserved */
    lsmash_bits_get( bits, 9 );     /* frmrtq_postproc (3)
                                     * bitrtq_postproc (5)
                                     * postproc_flag   (1) */
    sequence->max_coded_width  = lsmash_bits_get( bits, 12 );
    sequence->max_coded_height = lsmash_bits_get( bits, 12 );
    lsmash_bits_get( bits, 1 );     /* pulldown */
    sequence->interlace        = lsmash_bits_get( bits, 1 );
    lsmash_bits_get( bits, 4 );     /* tfcntrflag  (1)
                                     * finterpflag (1)
                                     * reserved    (1)
                                     * psf         (1) */
    if( lsmash_bits_get( bits, 1 ) )    /* display_ext */
    {
        sequence->disp_horiz_size = lsmash_bits_get( bits, 14 ) + 1;
        sequence->disp_vert_size  = lsmash_bits_get( bits, 14 ) + 1;
        if( lsmash_bits_get( bits, 1 ) )    /* aspect_ratio_flag */
        {
            uint8_t aspect_ratio = lsmash_bits_get( bits, 4 );
            if( aspect_ratio == 15 )
            {
                sequence->aspect_width  = lsmash_bits_get( bits, 8 ) + 1;   /* aspect_horiz_size */
                sequence->aspect_height = lsmash_bits_get( bits, 8 ) + 1;   /* aspect_vert_size */
            }
            else
            {
                static const struct
                {
                    uint32_t aspect_width;
                    uint32_t aspect_height;
                } vc1_aspect_ratio[15] =
                    {
                        {  0,  0 }, {  1,  1 }, { 12, 11 }, { 10, 11 }, { 16, 11 }, { 40, 33 }, {  24, 11 },
                        { 20, 11 }, { 32, 11 }, { 80, 33 }, { 18, 11 }, { 15, 11 }, { 64, 33 }, { 160, 99 },
                        {  0,  0 }  /* SMPTE Reserved */
                    };
                sequence->aspect_width  = vc1_aspect_ratio[ aspect_ratio ].aspect_width;
                sequence->aspect_height = vc1_aspect_ratio[ aspect_ratio ].aspect_height;
            }
        }
        sequence->framerate_flag = lsmash_bits_get( bits, 1 );
        if( sequence->framerate_flag )
        {
            if( lsmash_bits_get( bits, 1 ) )    /* framerateind */
            {
                sequence->framerate_numerator   = lsmash_bits_get( bits, 16 ) + 1;
                sequence->framerate_denominator = 32;
            }
            else
            {
                static const uint32_t vc1_frameratenr_table[8] = { 0, 24, 25, 30, 50, 60, 48, 72 };
                uint8_t frameratenr = lsmash_bits_get( bits, 8 );
                if( frameratenr == 0 || frameratenr > 7 )
                    return -1;
                uint8_t frameratedr = lsmash_bits_get( bits, 4 );
                if( frameratedr != 1 && frameratedr != 2 )
                    return -1;
                if( frameratedr == 1 )
                {
                    sequence->framerate_numerator = vc1_frameratenr_table[ frameratenr ];
                    sequence->framerate_denominator = 1;
                }
                else
                {
                    sequence->framerate_numerator = vc1_frameratenr_table[ frameratenr ] * 1000;
                    sequence->framerate_denominator = 1001;
                }
            }
        }
        if( lsmash_bits_get( bits, 1 ) )    /* color_format_flag */
        {
            sequence->color_prim    = lsmash_bits_get( bits, 8 );
            sequence->transfer_char = lsmash_bits_get( bits, 8 );
            sequence->matrix_coef   = lsmash_bits_get( bits, 8 );
        }
        sequence->hrd_param_flag = lsmash_bits_get( bits, 1 );
        if( sequence->hrd_param_flag )
            vc1_parse_hrd_param( bits, &sequence->hrd_param );
    }
    /* '1' and stuffing bits ('0's) */
    IF_INVALID_VALUE( !lsmash_bits_get( bits, 1 ) )
        return -1;
    lsmash_bits_empty( bits );
    /* Preparation for creating VC1SpecificBox */
    if( probe )
    {
        vc1_sequence_header_t *first_sequence = &info->first_sequence;
        if( !first_sequence->present )
        {
            sequence->ebdu = malloc( ebdu_length );
            if( !sequence->ebdu )
                return -1;
            memcpy( sequence->ebdu, ebdu, ebdu_length );
            sequence->length = ebdu_length;
            sequence->present = 1;
            *first_sequence = *sequence;
        }
        else if( first_sequence->ebdu && (first_sequence->length == ebdu_length) )
            info->multiple_sequence |= !!memcmp( ebdu, first_sequence->ebdu, ebdu_length );
    }
    return bits->bs->error ? -1 : 0;
}

static int vc1_parse_entry_point_header( mp4sys_vc1_info_t *info, uint8_t *ebdu, uint64_t ebdu_length, int probe )
{
    lsmash_bits_t *bits = info->bits;
    vc1_sequence_header_t *sequence = &info->sequence;
    vc1_entry_point_t *entry_point = &info->entry_point;
    if( vc1_bits_import_rbdu_from_ebdu( bits, info->rbdu_buffer, ebdu, ebdu_length ) )
        return -1;
    memset( entry_point, 0, sizeof(vc1_entry_point_t) );
    uint8_t broken_link_flag = lsmash_bits_get( bits, 1 );          /* 0: no concatenation between the current and the previous entry points
                                                                     * 1: concatenated and needed to discard B-pictures */
    entry_point->closed_entry_point = lsmash_bits_get( bits, 1 );   /* 0: Open RAP, 1: Closed RAP */
    if( !broken_link_flag && entry_point->closed_entry_point )
        return -1;  /* invalid combination */
    lsmash_bits_get( bits, 4 );         /* panscan_flag (1)
                                         * refdist_flag (1)
                                         * loopfilter   (1)
                                         * fastuvmc     (1) */
    uint8_t extended_mv = lsmash_bits_get( bits, 1 );
    lsmash_bits_get( bits, 6 );         /* dquant       (2)
                                         * vstransform  (1)
                                         * overlap      (1)
                                         * quantizer    (2) */
    if( sequence->hrd_param_flag )
        for( uint8_t i = 0; i < sequence->hrd_param.hrd_num_leaky_buckets; i++ )
            lsmash_bits_get( bits, 8 ); /* hrd_full */
    /* Decide coded size here.
     * The correct formula is defined in Amendment 2:2011 to SMPTE ST 421M:2006.
     * Don't use the formula specified in SMPTE 421M-2006. */
    uint16_t coded_width;
    uint16_t coded_height;
    if( lsmash_bits_get( bits, 1 ) )    /* coded_size_flag */
    {
        coded_width  = lsmash_bits_get( bits, 12 );
        coded_height = lsmash_bits_get( bits, 12 );
    }
    else
    {
        coded_width  = sequence->max_coded_width;
        coded_height = sequence->max_coded_height;
    }
    coded_width  = 2 * (coded_width  + 1);  /* corrected */
    coded_height = 2 * (coded_height + 1);  /* corrected */
    if( sequence->disp_horiz_size == 0 || sequence->disp_vert_size == 0 )
    {
        sequence->disp_horiz_size = coded_width;
        sequence->disp_vert_size  = coded_height;
    }
    /* */
    if( extended_mv )
        lsmash_bits_get( bits, 1 );     /* extended_dmv */
    if( lsmash_bits_get( bits, 1 ) )    /* range_mapy_flag */
        lsmash_bits_get( bits, 3 );     /* range_mapy */
    if( lsmash_bits_get( bits, 1 ) )    /* range_mapuv_flag */
        lsmash_bits_get( bits, 3 );     /* range_mapuv */
    /* '1' and stuffing bits ('0's) */
    IF_INVALID_VALUE( !lsmash_bits_get( bits, 1 ) )
        return -1;
    lsmash_bits_empty( bits );
    /* Preparation for creating VC1SpecificBox */
    if( probe )
    {
        vc1_entry_point_t *first_entry_point = &info->first_entry_point;
        if( !first_entry_point->present )
        {
            entry_point->ebdu = malloc( ebdu_length );
            if( !entry_point->ebdu )
                return -1;
            memcpy( entry_point->ebdu, ebdu, ebdu_length );
            entry_point->length = ebdu_length;
            entry_point->present = 1;
            *first_entry_point = *entry_point;
        }
        else if( first_entry_point->ebdu && (first_entry_point->length == ebdu_length) )
            info->multiple_entry_point |= !!memcmp( ebdu, first_entry_point->ebdu, ebdu_length );
    }
    return bits->bs->error ? -1 : 0;
}

static inline uint8_t vc1_get_vlc( lsmash_bits_t *bits, int length )
{
    uint8_t value = 0;
    for( int i = 0; i < length; i++ )
    {
        if( lsmash_bits_get( bits, 1 ) )
            value = (value << 1) | 1;
        else
        {
            value = value << 1;
            break;
        }
    }
    return value;
}

static int vc1_parse_advanced_picture( lsmash_bits_t *bits,
                                       vc1_sequence_header_t *sequence, vc1_picture_info_t *picture,
                                       uint8_t *rbdu_buffer, uint8_t *ebdu, uint64_t ebdu_length )
{
    if( vc1_bits_import_rbdu_from_ebdu( bits, rbdu_buffer, ebdu, ebdu_length ) )
        return -1;
    if( sequence->interlace )
        picture->frame_coding_mode = vc1_get_vlc( bits, 2 );
    else
        picture->frame_coding_mode = 0;
    if( picture->frame_coding_mode != 0x3 )
        picture->type = vc1_get_vlc( bits, 4 );         /* ptype (variable length) */
    else
        picture->type = lsmash_bits_get( bits, 3 );     /* fptype (3) */
    picture->present = 1;
    lsmash_bits_empty( bits );
    return bits->bs->error ? -1 : 0;
}

static int vc1_supplement_buffer( mp4sys_vc1_info_t *info, uint32_t size )
{
    uint32_t buffer_pos_offset   = info->stream_buffer_pos - info->stream_buffer;
    uint32_t buffer_valid_length = info->stream_buffer_end - info->stream_buffer;
    lsmash_multiple_buffers_t *temp = lsmash_resize_multiple_buffers( info->buffers, size );
    if( !temp )
        return -1;
    info->buffers                     = temp;
    info->stream_buffer               = lsmash_withdraw_buffer( info->buffers, 1 );
    info->rbdu_buffer                 = lsmash_withdraw_buffer( info->buffers, 2 );
    info->access_unit.data            = lsmash_withdraw_buffer( info->buffers, 3 );
    info->access_unit.incomplete_data = lsmash_withdraw_buffer( info->buffers, 4 );
    info->stream_buffer_pos = info->stream_buffer + buffer_pos_offset;
    info->stream_buffer_end = info->stream_buffer + buffer_valid_length;
    return 0;
}

static inline int vc1_check_next_start_code_prefix( uint8_t *buf_pos, uint8_t *buf_end )
{
    return ((buf_pos + 2) < buf_end) && !buf_pos[0] && !buf_pos[1] && (buf_pos[2] == 0x01);
}

static inline int vc1_check_next_start_code_suffix( uint8_t *p_bdu_type, uint8_t **p_start_code_suffix )
{
    uint8_t bdu_type = **p_start_code_suffix;
    if( (bdu_type >= 0x00 && bdu_type <= 0x09) || (bdu_type >= 0x20 && bdu_type <= 0xFF) )
        return -1;      /* SMPTE reserved or forbidden value */
    *p_bdu_type = bdu_type;
    ++ *p_start_code_suffix;
    return 0;
}

static void vc1_check_buffer_shortage( mp4sys_importer_t *importer, uint32_t anticipation_bytes )
{
    mp4sys_vc1_info_t *info = (mp4sys_vc1_info_t *)importer->info;
    assert( anticipation_bytes < info->buffers->buffer_size );
    if( info->no_more_read )
        return;
    uint32_t remainder_bytes = info->stream_buffer_end - info->stream_buffer_pos;
    if( remainder_bytes <= anticipation_bytes )
    {
        /* Move unused data to the head of buffer. */
        for( uint32_t i = 0; i < remainder_bytes; i++ )
            *(info->stream_buffer + i) = *(info->stream_buffer_pos + i);
        /* Read and store the next data into the buffer.
         * Move the position of buffer on the head. */
        uint32_t read_size = fread( info->stream_buffer + remainder_bytes, 1, info->buffers->buffer_size - remainder_bytes, importer->stream );
        info->stream_buffer_pos = info->stream_buffer;
        info->stream_buffer_end = info->stream_buffer + remainder_bytes + read_size;
        info->no_more_read = read_size == 0 ? feof( importer->stream ) : 0;
    }
}

static inline int vc1_find_au_delimit_by_bdu_type( uint8_t bdu_type, uint8_t prev_bdu_type )
{
    /* In any access unit, EBDU with smaller least significant 8-bits of BDU type doesn't precede EBDU with larger one.
     * Therefore, the condition: (bdu_type 0xF) > (prev_bdu_type & 0xF) is more precisely.
     * No two or more frame start codes shall be in the same access unit. */
    return bdu_type > prev_bdu_type || (bdu_type == 0x0D && prev_bdu_type == 0x0D);
}

static inline void vc1_update_au_property( vc1_access_unit_t *access_unit, vc1_picture_info_t *picture )
{
    access_unit->random_accessible = picture->random_accessible;
    access_unit->closed_gop        = picture->closed_gop;
    /* I-picture
     *      Be coded using information only from itself. (independent)
     *      All the macroblocks in an I-picture are intra-coded.
     * P-picture
     *      Be coded using motion compensated prediction from past reference fields or frame.
     *      Can contain macroblocks that are inter-coded (i.e. coded using prediction) and macroblocks that are intra-coded.
     * B-picture
     *      Be coded using motion compensated prediction from past and/or future reference fields or frames. (bi-predictive)
     *      Cannot be used for predicting any other picture. (disposable)
     * BI-picture
     *      All the macroblocks in BI-picture are intra-coded. (independent)
     *      Cannot be used for predicting any other picture. (disposable) */
    if( picture->frame_coding_mode == 0x3 )
    {
        /* field interlace */
        access_unit->independent      = picture->type == VC1_ADVANCED_FIELD_PICTURE_TYPE_II || picture->type == VC1_ADVANCED_FIELD_PICTURE_TYPE_BIBI;
        access_unit->non_bipredictive = picture->type <  VC1_ADVANCED_FIELD_PICTURE_TYPE_BB || picture->type == VC1_ADVANCED_FIELD_PICTURE_TYPE_BIBI;
        access_unit->disposable       = picture->type >= VC1_ADVANCED_FIELD_PICTURE_TYPE_BB;
    }
    else
    {
        /* frame progressive/interlace */
        access_unit->independent      = picture->type == VC1_ADVANCED_PICTURE_TYPE_I || picture->type == VC1_ADVANCED_PICTURE_TYPE_BI;
        access_unit->non_bipredictive = picture->type != VC1_ADVANCED_PICTURE_TYPE_B;
        access_unit->disposable       = picture->type == VC1_ADVANCED_PICTURE_TYPE_B || picture->type == VC1_ADVANCED_PICTURE_TYPE_BI;
    }
    picture->present           = 0;
    picture->type              = 0;
    picture->closed_gop        = 0;
    picture->start_of_sequence = 0;
    picture->random_accessible = 0;
}

static inline int vc1_complete_au( vc1_access_unit_t *access_unit, vc1_picture_info_t *next_picture, int probe )
{
    if( !next_picture->present )
        return 0;
    if( !probe )
        memcpy( access_unit->data, access_unit->incomplete_data, access_unit->incomplete_data_length );
    access_unit->data_length = access_unit->incomplete_data_length;
    access_unit->incomplete_data_length = 0;
    vc1_update_au_property( access_unit, next_picture );
    return 1;
}

static inline void vc1_append_ebdu_to_au( vc1_access_unit_t *access_unit, uint8_t *ebdu, uint32_t ebdu_length, int probe )
{
    if( !probe )
        memcpy( access_unit->incomplete_data + access_unit->incomplete_data_length, ebdu, ebdu_length );
    /* Note: access_unit->incomplete_data_length shall be 0 immediately after AU has completed.
     * Therefore, possible_au_length in vc1_get_access_unit_internal() can't be used here
     * to avoid increasing AU length monotonously through the entire stream. */
    access_unit->incomplete_data_length += ebdu_length;
}

static inline void vc1_get_au_internal_end( mp4sys_vc1_info_t *info, vc1_access_unit_t *access_unit, uint8_t bdu_type, int no_more_buf )
{
    info->status = info->no_more_read && no_more_buf && (access_unit->incomplete_data_length == 0)
                 ? MP4SYS_IMPORTER_EOF
                 : MP4SYS_IMPORTER_OK;
    info->bdu_type = bdu_type;
}

static int vc1_get_au_internal_succeeded( mp4sys_vc1_info_t *info, vc1_access_unit_t *access_unit, uint8_t bdu_type, int no_more_buf )
{
    vc1_get_au_internal_end( info, access_unit, bdu_type, no_more_buf );
    access_unit->number += 1;
    return 0;
}

static int vc1_get_au_internal_failed( mp4sys_vc1_info_t *info, vc1_access_unit_t *access_unit, uint8_t bdu_type, int no_more_buf, int complete_au )
{
    vc1_get_au_internal_end( info, access_unit, bdu_type, no_more_buf );
    if( complete_au )
        access_unit->number += 1;
    return -1;
}

static int vc1_get_access_unit_internal( mp4sys_importer_t *importer, mp4sys_vc1_info_t *info, uint32_t track_number, int probe )
{
    vc1_access_unit_t *access_unit  = &info->access_unit;
    uint8_t  bdu_type = info->bdu_type;
    uint64_t consecutive_zero_byte_count = 0;
    uint64_t ebdu_length = 0;
    int      no_more_buf = 0;
    int      complete_au = 0;
    access_unit->data_length = 0;
    while( 1 )
    {
        vc1_check_buffer_shortage( importer, 2 );
        no_more_buf = info->stream_buffer_pos >= info->stream_buffer_end;
        int no_more = info->no_more_read && no_more_buf;
        if( vc1_check_next_start_code_prefix( info->stream_buffer_pos, info->stream_buffer_end ) || no_more )
        {
            if( no_more && ebdu_length == 0 )
            {
                /* For the last EBDU.
                 * This EBDU already has been appended into the latest access unit and parsed. */
                ebdu_length = access_unit->incomplete_data_length;
                vc1_complete_au( access_unit, &info->next_picture, probe );
                return vc1_get_au_internal_succeeded( info, access_unit, bdu_type, no_more_buf );
            }
            ebdu_length += VC1_START_CODE_LENGTH;
            uint64_t next_scs_file_offset = info->ebdu_head_pos + ebdu_length + !no_more * VC1_START_CODE_PREFIX_LENGTH;
            uint8_t *next_ebdu_pos = info->stream_buffer_pos;   /* Memorize position of beginning of the next EBDU in buffer.
                                                                 * This is used when backward reading of stream doesn't occur. */
            int read_back = 0;
#if 0
            if( probe )
            {
                fprintf( stderr, "BDU type: %"PRIu8"                            \n", bdu_type );
                fprintf( stderr, "    EBDU position: %"PRIx64"                  \n", info->ebdu_head_pos );
                fprintf( stderr, "    EBDU length: %"PRIx64" (%"PRIu64")        \n", ebdu_length - consecutive_zero_byte_count,
                                                                                     ebdu_length - consecutive_zero_byte_count );
                fprintf( stderr, "    consecutive_zero_byte_count: %"PRIx64"    \n", consecutive_zero_byte_count );
                fprintf( stderr, "    Next start code suffix position: %"PRIx64"\n", next_scs_file_offset );
            }
#endif
            if( bdu_type >= 0x0A && bdu_type <= 0x0F )
            {
                /* Get the current EBDU here. */
                ebdu_length -= consecutive_zero_byte_count;     /* Any EBDU doesn't have zero bytes at the end. */
                uint64_t possible_au_length = access_unit->incomplete_data_length + ebdu_length;
                if( info->buffers->buffer_size < possible_au_length )
                {
                    if( vc1_supplement_buffer( info, 2 * possible_au_length ) )
                        vc1_get_au_internal_failed( info, access_unit, bdu_type, no_more_buf, complete_au );
                    next_ebdu_pos = info->stream_buffer_pos;
                }
                /* Move to the first byte of the current EBDU. */
                read_back = (info->stream_buffer_pos - info->stream_buffer) < (ebdu_length + consecutive_zero_byte_count);
                if( read_back )
                {
                    lsmash_fseek( importer->stream, info->ebdu_head_pos, SEEK_SET );
                    int read_fail = fread( info->stream_buffer, 1, ebdu_length, importer->stream ) != ebdu_length;
                    info->stream_buffer_pos = info->stream_buffer;
                    info->stream_buffer_end = info->stream_buffer + ebdu_length;
                    if( read_fail )
                        vc1_get_au_internal_failed( info, access_unit, bdu_type, no_more_buf, complete_au );
#if 0
                    if( probe )
                        fprintf( stderr, "    ----Read Back\n" );
#endif
                }
                else
                    info->stream_buffer_pos -= ebdu_length + consecutive_zero_byte_count;
                /* Complete the current access unit if encountered delimiter of current access unit. */
                if( vc1_find_au_delimit_by_bdu_type( bdu_type, info->prev_bdu_type ) )
                    /* The last video coded EBDU belongs to the access unit you want at this time. */
                    complete_au = vc1_complete_au( access_unit, &info->next_picture, probe );
                /* Process EBDU by its BDU type and append it to access unit. */
                switch( bdu_type )
                {
                    /* FRM_SC: Frame start code
                     * FLD_SC: Field start code
                     * SLC_SC: Slice start code
                     * SEQ_SC: Sequence header start code
                     * EP_SC:  Entry-point start code
                     * PIC_L:  Picture layer
                     * SLC_L:  Slice layer
                     * SEQ_L:  Sequence layer
                     * EP_L:   Entry-point layer */
                    case 0x0D : /* Frame
                                 * For the Progressive or Frame Interlace mode, shall signal the beginning of a new video frame.
                                 * For the Field Interlace mode, shall signal the beginning of a sequence of two independently coded video fields.
                                 * [FRM_SC][PIC_L][[FLD_SC][PIC_L] (optional)][[SLC_SC][SLC_L] (optional)] ...  */
                        if( vc1_parse_advanced_picture( info->bits, &info->sequence, &info->next_picture, info->rbdu_buffer,
                                                        info->stream_buffer_pos, ebdu_length ) )
                            return vc1_get_au_internal_failed( info, access_unit, bdu_type, no_more_buf, complete_au );
                    case 0x0C : /* Field
                                 * Shall only be used for Field Interlaced frames
                                 * and shall only be used to signal the beginning of the second field of the frame.
                                 * [FRM_SC][PIC_L][FLD_SC][PIC_L][[SLC_SC][SLC_L] (optional)] ...
                                 * Field start code is followed by INTERLACE_FIELD_PICTURE_FIELD2() which doesn't have info of its field picture type.*/
                        break;
                    case 0x0B : /* Slice
                                 * Shall not be used for start code of the first slice of a frame.
                                 * Shall not be used for start code of the first slice of an interlace field coded picture.
                                 * [FRM_SC][PIC_L][[FLD_SC][PIC_L] (optional)][SLC_SC][SLC_L][[SLC_SC][SLC_L] (optional)] ...
                                 * Slice layer may repeat frame header. We just ignore it. */
                        info->slice_present = 1;
                        break;
                    case 0x0E : /* Entry-point header
                                 * Entry-point indicates the direct followed frame is a start of group of frames.
                                 * Entry-point doesn't indicates the frame is a random access point when multiple sequence headers are present,
                                 * since it is necessary to decode sequence header which subsequent frames belong to for decoding them.
                                 * Entry point shall be followed by
                                 *   1. I-picture - progressive or frame interlace
                                 *   2. I/I-picture, I/P-picture, or P/I-picture - field interlace
                                 * [[SEQ_SC][SEQ_L] (optional)][EP_SC][EP_L][FRM_SC][PIC_L] ... */
                        if( vc1_parse_entry_point_header( info, info->stream_buffer_pos, ebdu_length, probe ) )
                            return vc1_get_au_internal_failed( info, access_unit, bdu_type, no_more_buf, complete_au );
                        info->next_picture.closed_gop        = info->entry_point.closed_entry_point;
                        info->next_picture.random_accessible = info->multiple_sequence ? info->next_picture.start_of_sequence : 1;
                        break;
                    case 0x0F : /* Sequence header
                                 * [SEQ_SC][SEQ_L][EP_SC][EP_L][FRM_SC][PIC_L] ... */
                        if( vc1_parse_sequence_header( info, info->stream_buffer_pos, ebdu_length, probe ) )
                            return vc1_get_au_internal_failed( info, access_unit, bdu_type, no_more_buf, complete_au );
                        info->next_picture.start_of_sequence = 1;
                        break;
                    default :   /* End-of-sequence (0x0A) */
                        break;
                }
                vc1_append_ebdu_to_au( access_unit, info->stream_buffer_pos, ebdu_length, probe );
            }
            else    /* We don't support other BDU types such as user data yet. */
                return vc1_get_au_internal_failed( info, access_unit, bdu_type, no_more_buf, complete_au );
            /* Move to the first byte of the next start code suffix. */
            if( read_back )
            {
                lsmash_fseek( importer->stream, next_scs_file_offset, SEEK_SET );
                info->stream_buffer_pos = info->stream_buffer;
                info->stream_buffer_end = info->stream_buffer + fread( info->stream_buffer, 1, info->buffers->buffer_size, importer->stream );
            }
            else
                info->stream_buffer_pos = next_ebdu_pos + VC1_START_CODE_PREFIX_LENGTH;
            info->prev_bdu_type = bdu_type;
            vc1_check_buffer_shortage( importer, 0 );
            no_more_buf = info->stream_buffer_pos >= info->stream_buffer_end;
            ebdu_length = 0;
            no_more = info->no_more_read && no_more_buf;
            if( !no_more )
            {
                /* Check the next BDU type. */
                if( vc1_check_next_start_code_suffix( &bdu_type, &info->stream_buffer_pos ) )
                    return vc1_get_au_internal_failed( info, access_unit, bdu_type, no_more_buf, complete_au );
                info->ebdu_head_pos = next_scs_file_offset - VC1_START_CODE_PREFIX_LENGTH;
            }
            /* If there is no more data in the stream, and flushed chunk of EBDUs, flush it as complete AU here. */
            else if( access_unit->incomplete_data_length && access_unit->data_length == 0 )
            {
                vc1_complete_au( access_unit, &info->next_picture, probe );
                return vc1_get_au_internal_succeeded( info, access_unit, bdu_type, no_more_buf );
            }
            if( complete_au )
                return vc1_get_au_internal_succeeded( info, access_unit, bdu_type, no_more_buf );
            consecutive_zero_byte_count = 0;
            continue;       /* Avoid increment of ebdu_length. */
        }
        else if( !no_more )
        {
            if( *info->stream_buffer_pos ++ )
                consecutive_zero_byte_count = 0;
            else
                ++consecutive_zero_byte_count;
        }
        ++ebdu_length;
    }
}

static int mp4sys_vc1_get_accessunit( mp4sys_importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    mp4sys_vc1_info_t *info = (mp4sys_vc1_info_t *)importer->info;
    mp4sys_importer_status current_status = info->status;
    if( current_status == MP4SYS_IMPORTER_ERROR || buffered_sample->length < info->max_au_length )
        return -1;
    if( current_status == MP4SYS_IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( vc1_get_access_unit_internal( importer, info, track_number, 0 ) )
    {
        info->status = MP4SYS_IMPORTER_ERROR;
        return -1;
    }
    vc1_access_unit_t *access_unit = &info->access_unit;
    buffered_sample->dts = info->ts_list.timestamp[access_unit->number - 1].dts;
    buffered_sample->cts = info->ts_list.timestamp[access_unit->number - 1].cts;
    buffered_sample->prop.leading = access_unit->non_bipredictive || buffered_sample->cts >= info->last_ref_intra_cts
                                  ? ISOM_SAMPLE_IS_NOT_LEADING : access_unit->independent
                                                               ? ISOM_SAMPLE_IS_DECODABLE_LEADING
                                                               : ISOM_SAMPLE_IS_UNDECODABLE_LEADING;
    if( access_unit->independent && !access_unit->disposable )
        info->last_ref_intra_cts = buffered_sample->cts;
    if( info->composition_reordering_present && !access_unit->disposable && !access_unit->closed_gop )
        buffered_sample->prop.allow_earlier = QT_SAMPLE_EARLIER_PTS_ALLOWED;
    buffered_sample->prop.independent = access_unit->independent ? ISOM_SAMPLE_IS_INDEPENDENT : ISOM_SAMPLE_IS_NOT_INDEPENDENT;
    buffered_sample->prop.disposable  = access_unit->disposable  ? ISOM_SAMPLE_IS_DISPOSABLE  : ISOM_SAMPLE_IS_NOT_DISPOSABLE;
    buffered_sample->prop.redundant   = ISOM_SAMPLE_HAS_NO_REDUNDANCY;
    if( access_unit->random_accessible )
        buffered_sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
    buffered_sample->length = access_unit->data_length;
    memcpy( buffered_sample->data, access_unit->data, access_unit->data_length );
    return current_status;
}

static uint8_t *vc1_create_dvc1( mp4sys_vc1_info_t *info, uint32_t *dvc1_length )
{
    lsmash_bits_t *bits = info->bits;
    vc1_sequence_header_t *sequence = &info->first_sequence;
    lsmash_bits_put( bits, 0, 32 );                                     /* box size */
    lsmash_bits_put( bits, ISOM_BOX_TYPE_DVC1, 32 );                    /* box type = 'dvc1' */
    lsmash_bits_put( bits, sequence->profile << 2, 4 );                 /* profile */
    lsmash_bits_put( bits, sequence->level, 3 );                        /* level */
    lsmash_bits_put( bits, 0, 1 );                                      /* reserved */
    /* VC1AdvDecSpecStruc (for Advanced Profile) */
    lsmash_bits_put( bits, sequence->level, 3 );                        /* level (identical to the previous level field) */
    lsmash_bits_put( bits, 0, 1 );                                      /* cbr */
    lsmash_bits_put( bits, 0, 6 );                                      /* reserved */
    lsmash_bits_put( bits, !sequence->interlace, 1 );                   /* no_interlace */
    lsmash_bits_put( bits, !info->multiple_sequence, 1 );               /* no_multiple_seq */
    lsmash_bits_put( bits, !info->multiple_entry_point, 1 );            /* no_multiple_entry */
    lsmash_bits_put( bits, !info->slice_present, 1 );                   /* no_slice_code */
    lsmash_bits_put( bits, !info->composition_reordering_present, 1 );  /* no_bframe */
    lsmash_bits_put( bits, 0, 1 );                                      /* reserved */
    uint32_t framerate = sequence->framerate_flag
                       ? ((double)sequence->framerate_numerator / sequence->framerate_denominator) + 0.5
                       : 0xffffffff;    /* 0xffffffff means framerate is unknown or unspecified. */
    lsmash_bits_put( bits, framerate, 32 );                             /* framerate */
    /* seqhdr_ephdr[] */
    uint8_t *ebdu = sequence->ebdu;
    for( uint32_t i = 0; i < sequence->length; i++ )
        lsmash_bits_put( bits, *ebdu++, 8 );
    ebdu = info->first_entry_point.ebdu;
    for( uint32_t i = 0; i < info->first_entry_point.length; i++ )
        lsmash_bits_put( bits, *ebdu++, 8 );
    /* */
    uint8_t *dvc1 = lsmash_bits_export_data( bits, dvc1_length );
    lsmash_bits_empty( bits );
    /* Update box size. */
    dvc1[0] = ((*dvc1_length) >> 24) & 0xff;
    dvc1[1] = ((*dvc1_length) >> 16) & 0xff;
    dvc1[2] = ((*dvc1_length) >>  8) & 0xff;
    dvc1[3] =  (*dvc1_length)        & 0xff;
    return dvc1;
}

static lsmash_video_summary_t *vc1_create_summary( mp4sys_vc1_info_t *info )
{
    if( !info->first_sequence.present || !info->first_entry_point.present )
        return NULL;
    lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_create_summary( MP4SYS_STREAM_TYPE_VisualStream );
    if( !summary )
        return NULL;
    summary->exdata = vc1_create_dvc1( info, &summary->exdata_length );
    if( !summary->exdata )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary);
        return NULL;
    }
    vc1_sequence_header_t *sequence = &info->first_sequence;
    summary->sample_type            = ISOM_CODEC_TYPE_VC_1_VIDEO;
    summary->object_type_indication = MP4SYS_OBJECT_TYPE_VC_1_VIDEO;
    summary->timescale              = sequence->framerate_numerator;
    summary->timebase               = sequence->framerate_denominator;
    summary->vfr                    = !sequence->framerate_flag;
    summary->width                  = sequence->disp_horiz_size;
    summary->height                 = sequence->disp_vert_size;
    summary->par_h                  = sequence->aspect_width;
    summary->par_v                  = sequence->aspect_height;
    summary->primaries              = sequence->color_prim;
    summary->transfer               = sequence->transfer_char;
    summary->matrix                 = sequence->matrix_coef;
    return summary;
}

static int mp4sys_vc1_probe( mp4sys_importer_t *importer )
{
#define VC1_CHECK_FIRST_START_CODE( x ) (!(x)[0] && !(x)[1] && ((x)[2] == 0x01))
    /* Find the first start code. */
    mp4sys_vc1_info_t *info = mp4sys_create_vc1_info();
    if( !info )
        return -1;
    info->stream_buffer_pos = info->stream_buffer;
    info->stream_buffer_end = info->stream_buffer + fread( info->stream_buffer, 1, info->buffers->buffer_size, importer->stream );
    info->no_more_read = info->stream_buffer >= info->stream_buffer_end ? feof( importer->stream ) : 0;
    while( 1 )
    {
        /* Invalid if encountered any value of non-zero before the first start code. */
        IF_INVALID_VALUE( *info->stream_buffer_pos )
            goto fail;
        /* The first EBDU in decoding order of the stream shall have start code (0x000001). */
        if( VC1_CHECK_FIRST_START_CODE( info->stream_buffer_pos ) )
            break;
        /* If the first trial of finding start code of sequence header failed, we assume this stream is not byte stream format of VC-1. */
        if( (info->stream_buffer_pos + VC1_START_CODE_LENGTH) == info->stream_buffer_end )
            goto fail;
        ++info->stream_buffer_pos;
    }
    /* OK. It seems the stream has a sequence header of VC-1. */
    importer->info = info;
    uint64_t first_ebdu_head_pos = info->stream_buffer_pos - info->stream_buffer;
    info->stream_buffer_pos += VC1_START_CODE_PREFIX_LENGTH;
    vc1_check_buffer_shortage( importer, 0 );
    uint8_t first_bdu_type = *info->stream_buffer_pos ++;
    info->status        = info->no_more_read ? MP4SYS_IMPORTER_EOF : MP4SYS_IMPORTER_OK;
    info->bdu_type      = first_bdu_type;
    info->ebdu_head_pos = first_ebdu_head_pos;
    /* Parse all EBDU in the stream for preparation of calculating timestamps. */
    uint32_t cts_alloc = (1 << 12) * sizeof(uint64_t);
    uint64_t *cts = malloc( cts_alloc );
    if( !cts )
        goto fail;
    uint32_t num_access_units = 0;
    uint32_t num_consecutive_b = 0;
    fprintf( stderr, "Analyzing stream as VC-1\r" );
    while( info->status != MP4SYS_IMPORTER_EOF )
    {
#if 0
        fprintf( stderr, "Analyzing stream as VC-1: %"PRIu32"\n", num_access_units + 1 );
#endif
        if( vc1_get_access_unit_internal( importer, info, 0, 1 ) )
            goto fail;
        /* In the case where B-pictures exist
         * Decode order
         *      I[0]P[1]P[2]B[3]B[4]P[5]...
         * DTS
         *        0   1   2   3   4   5 ...
         * Composition order
         *      I[0]P[1]B[3]B[4]P[2]P[5]...
         * CTS
         *        1   2   3   4   5   6 ...
         * We assumes B or BI-pictures always be present in the stream here. */
        if( !info->access_unit.disposable )
        {
            /* Apply CTS of the last B-picture plus 1 to the last non-B-picture. */
            if( num_access_units > num_consecutive_b )
                cts[ num_access_units - num_consecutive_b - 1 ] = num_access_units;
            num_consecutive_b = 0;
        }
        else    /* B or BI-picture */
        {
            /* B and BI-pictures shall be output or displayed in the same order as they are encoded. */
            cts[ num_access_units ] = num_access_units;
            ++num_consecutive_b;
        }
        if( cts_alloc <= num_access_units * sizeof(uint64_t) )
        {
            uint32_t alloc = 2 * num_access_units * sizeof(uint64_t);
            uint64_t *temp = realloc( cts, alloc );
            if( !temp )
            {
                free( cts );
                goto fail;
            }
            cts = temp;
            cts_alloc = alloc;
        }
        info->max_au_length = LSMASH_MAX( info->access_unit.data_length, info->max_au_length );
        ++num_access_units;
    }
    if( num_access_units > num_consecutive_b )
        cts[ num_access_units - num_consecutive_b - 1 ] = num_access_units;
    else
    {
        free( cts );
        goto fail;
    }
    fprintf( stderr, "                                                                               \r" );
    /* Construct timestamps. */
    lsmash_media_ts_t *timestamp = malloc( num_access_units * sizeof(lsmash_media_ts_t) );
    if( !timestamp )
    {
        free( cts );
        goto fail;
    }
    for( uint32_t i = 1; i < num_access_units; i++ )
        if( cts[i] < cts[i - 1] )
        {
            info->composition_reordering_present = 1;
            break;
        }
    if( info->composition_reordering_present )
        for( uint32_t i = 0; i < num_access_units; i++ )
        {
            timestamp[i].cts = cts[i];
            timestamp[i].dts = i;
        }
    else
        for( uint32_t i = 0; i < num_access_units; i++ )
            timestamp[i].cts = timestamp[i].dts = i;
    free( cts );
#if 0
    for( uint32_t i = 0; i < num_access_units; i++ )
        fprintf( stderr, "Timestamp[%"PRIu32"]: DTS=%"PRIu64", CTS=%"PRIu64"\n", i, timestamp[i].dts, timestamp[i].cts );
#endif
    info->summary = vc1_create_summary( info );
    if( !info->summary || lsmash_add_entry( importer->summaries, info->summary ) )
        goto fail;
    info->ts_list.sample_count           = num_access_units;
    info->ts_list.timestamp              = timestamp;
    /* Go back to layer of the first EBDU. */
    lsmash_fseek( importer->stream, first_ebdu_head_pos, SEEK_SET );
    info->status                         = MP4SYS_IMPORTER_OK;
    info->bdu_type                       = first_bdu_type;
    info->prev_bdu_type                  = 0;
    info->no_more_read                   = 0;
    info->summary->max_au_length         = info->max_au_length;
    info->summary                        = NULL;
    info->stream_buffer_pos              = info->stream_buffer + VC1_START_CODE_LENGTH;
    info->stream_buffer_end              = info->stream_buffer + fread( info->stream_buffer, 1, info->buffers->buffer_size, importer->stream );
    info->ebdu_head_pos                  = first_ebdu_head_pos;
    uint8_t *temp_access_unit            = info->access_unit.data;
    uint8_t *temp_incomplete_access_unit = info->access_unit.incomplete_data;
    memset( &info->access_unit, 0, sizeof(vc1_access_unit_t) );
    info->access_unit.data               = temp_access_unit;
    info->access_unit.incomplete_data    = temp_incomplete_access_unit;
    memset( &info->next_picture, 0, sizeof(vc1_picture_info_t) );
    importer->info = info;
    return 0;
fail:
    mp4sys_remove_vc1_info( info );
    lsmash_remove_entries( importer->summaries, lsmash_cleanup_summary );
    return -1;
#undef VC1_CHECK_FIRST_START_CODE
}

static uint32_t mp4sys_vc1_get_last_delta( mp4sys_importer_t* importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    mp4sys_vc1_info_t *info = (mp4sys_vc1_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != MP4SYS_IMPORTER_EOF )
        return 0;
    return info->ts_list.sample_count > 1
         ? 1
         : UINT32_MAX;    /* arbitrary */
}

#undef VC1_START_CODE_PREFIX_LENGTH
#undef VC1_START_CODE_SUFFIX_LENGTH
#undef VC1_START_CODE_LENGTH

const static mp4sys_importer_functions mp4sys_vc1_importer =
{
    "VC-1",
    1,
    mp4sys_vc1_probe,
    mp4sys_vc1_get_accessunit,
    mp4sys_vc1_get_last_delta,
    mp4sys_vc1_cleanup
};

/***************************************************************************
    importer public interfaces
***************************************************************************/


/******** importer listing table ********/
const static mp4sys_importer_functions* mp4sys_importer_tbl[] = {
    &mp4sys_adts_importer,
    &mp4sys_mp3_importer,
    &mp4sys_amr_importer,
    &mp4sys_ac3_importer,
    &mp4sys_eac3_importer,
    &mp4sys_als_importer,
    &mp4sys_h264_importer,
    &mp4sys_vc1_importer,
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
    lsmash_remove_list( importer->summaries, lsmash_cleanup_summary );
    free( importer );
}

mp4sys_importer_t *mp4sys_importer_open( const char *identifier, const char *format )
{
    if( identifier == NULL )
        return NULL;

    int auto_detect = ( format == NULL || !strcmp( format, "auto" ) );
    mp4sys_importer_t *importer = (mp4sys_importer_t *)lsmash_malloc_zero( sizeof(mp4sys_importer_t) );
    if( !importer )
        return NULL;

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
    const mp4sys_importer_functions *funcs;
    if( auto_detect )
    {
        /* just rely on detector. */
        for( int i = 0; (funcs = mp4sys_importer_tbl[i]) != NULL; i++ )
        {
            if( !funcs->detectable )
                continue;
            if( !funcs->probe( importer ) || lsmash_fseek( importer->stream, 0, SEEK_SET ) )
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
int mp4sys_importer_get_access_unit( mp4sys_importer_t* importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    if( !importer || !importer->funcs.get_accessunit || !buffered_sample->data || buffered_sample->length == 0 )
        return -1;
    return importer->funcs.get_accessunit( importer, track_number, buffered_sample );
}

/* Return 0 if failed, otherwise succeeded. */
uint32_t mp4sys_importer_get_last_delta( mp4sys_importer_t *importer, uint32_t track_number )
{
    if( !importer || !importer->funcs.get_last_delta )
        return -1;
    return importer->funcs.get_last_delta( importer, track_number );
}

uint32_t mp4sys_importer_get_track_count( mp4sys_importer_t *importer )
{
    if( !importer || !importer->summaries )
        return 0;
    return importer->summaries->entry_count;
}

lsmash_summary_t *mp4sys_duplicate_summary( mp4sys_importer_t *importer, uint32_t track_number )
{
    if( !importer )
        return NULL;
    lsmash_summary_t *src_summary = lsmash_get_entry_data( importer->summaries, track_number );
    if( !src_summary )
        return NULL;
    lsmash_summary_t *summary = lsmash_create_summary( src_summary->stream_type );
    if( !summary )
        return NULL;
    switch( src_summary->stream_type )
    {
        case MP4SYS_STREAM_TYPE_VisualStream :
            memcpy( summary, src_summary, sizeof(lsmash_video_summary_t) );
            break;
        case MP4SYS_STREAM_TYPE_AudioStream :
            memcpy( summary, src_summary, sizeof(lsmash_audio_summary_t) );
            break;
        default :
            lsmash_cleanup_summary( summary );
            return NULL;
    }
    summary->exdata = NULL;
    summary->exdata_length = 0;
    if( lsmash_summary_add_exdata( summary, src_summary->exdata, src_summary->exdata_length ) )
    {
        lsmash_cleanup_summary( summary );
        return NULL;
    }
    return summary;
}
