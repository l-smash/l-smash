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

typedef void ( *mp4sys_importer_cleanup )( struct mp4sys_importer_tag* importer );
typedef int ( *mp4sys_importer_get_accessunit )( struct mp4sys_importer_tag*, uint32_t track_number, lsmash_sample_t *buffered_sample );
typedef int ( *mp4sys_importer_probe )( struct mp4sys_importer_tag* importer );
typedef void ( *mp4sys_importer_remove_summary )( void *summary );

typedef struct
{
    const char*                    name;
    int                            detectable;
    mp4sys_importer_probe          probe;
    mp4sys_importer_get_accessunit get_accessunit;
    mp4sys_importer_cleanup        cleanup;
    mp4sys_importer_remove_summary remove_summary;
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
    lsmash_audio_summary_t* summary = lsmash_create_audio_summary();
    if( !summary )
        return NULL;
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
        lsmash_cleanup_audio_summary( summary );
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
        lsmash_cleanup_audio_summary( entry->data );
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
    mp4sys_adts_info_t* info = malloc( sizeof(mp4sys_adts_info_t) );
    if( !info )
    {
        lsmash_cleanup_audio_summary( summary );
        return -1;
    }
    memset( info, 0, sizeof(mp4sys_adts_info_t) );
    info->status = MP4SYS_IMPORTER_OK;
    info->raw_data_block_idx = 0;
    info->header = header;
    info->variable_header = variable_header;
    info->samples_in_frame = summary->samples_in_frame;

    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        free( info );
        lsmash_cleanup_audio_summary( summary );
        return -1;
    }
    importer->info = info;

    return 0;
}

static void mp4sys_adts_remove_sumary( void *summary )
{
    lsmash_cleanup_audio_summary( (lsmash_audio_summary_t *)summary );
}

const static mp4sys_importer_functions mp4sys_adts_importer = {
    "adts",
    1,
    mp4sys_adts_probe,
    mp4sys_adts_get_accessunit,
    mp4sys_adts_cleanup,
    mp4sys_adts_remove_sumary
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
    lsmash_audio_summary_t* summary = lsmash_create_audio_summary();
    if( !summary )
        return NULL;
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
        if( lsmash_setup_AudioSpecificConfig( summary ) )
        {
            lsmash_cleanup_audio_summary( summary );
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
        lsmash_cleanup_audio_summary( entry->data );
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
        lsmash_cleanup_audio_summary( summary );
        return -1;
    }
    memset( info, 0, sizeof(mp4sys_mp3_info_t) );
    info->status = MP4SYS_IMPORTER_OK;
    info->header = header;
    info->samples_in_frame = summary->samples_in_frame;
    memcpy( info->raw_header, buf, MP4SYS_MP3_HEADER_LENGTH );

    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        free( info );
        lsmash_cleanup_audio_summary( summary );
        return -1;
    }
    importer->info = info;

    return 0;
}

static void mp4sys_mp3_remove_sumary( void *summary )
{
    lsmash_cleanup_audio_summary( (lsmash_audio_summary_t *)summary );
}

const static mp4sys_importer_functions mp4sys_mp3_importer = {
    "MPEG-1/2BC_Audio_Legacy",
    1,
    mp4sys_mp3_probe,
    mp4sys_mp3_get_accessunit,
    mp4sys_mp3_cleanup,
    mp4sys_mp3_remove_sumary
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
    lsmash_audio_summary_t* summary = lsmash_create_audio_summary();
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
    mp4sys_amr_info_t *info = malloc( sizeof(mp4sys_amr_info_t) );
    if( !importer->info )
    {
        lsmash_cleanup_audio_summary( summary );
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
        lsmash_cleanup_audio_summary( summary );
        return -1;
    }
    return 0;
}

static void mp4sys_amr_remove_sumary( void *summary )
{
    lsmash_cleanup_audio_summary( (lsmash_audio_summary_t *)summary );
}

const static mp4sys_importer_functions mp4sys_amr_importer = {
    "amr",
    1,
    mp4sys_amr_probe,
    mp4sys_amr_get_accessunit,
    mp4sys_amr_cleanup,
    mp4sys_amr_remove_sumary
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
    H.264 importer
***************************************************************************/
typedef struct
{
    uint8_t forbidden_zero_bit;
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
    uint32_t recovery_frame_cnt;
    uint8_t  random_accessible;
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
    uint8_t *stream_buffer;
    uint8_t *stream_buffer_pos;
    uint32_t stream_buffer_size;
    uint32_t stream_read_size;
    uint64_t ebsp_head_pos;
    uint8_t *rbsp_buffer;
    uint32_t rbsp_buffer_size;
    uint32_t au_buffer_size;
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

#define MP4SYS_H264_DEFAULT_BUFFER_SIZE (1<<16)

static void mp4sys_remove_h264_info( mp4sys_h264_info_t *info )
{
    if( !info )
        return;
    lsmash_remove_list( info->avcC.sequenceParameterSets,   isom_remove_avcC_ps );
    lsmash_remove_list( info->avcC.pictureParameterSets,    isom_remove_avcC_ps );
    lsmash_remove_list( info->avcC.sequenceParameterSetExt, isom_remove_avcC_ps );
    lsmash_bits_adhoc_cleanup( info->bits );
    if( info->stream_buffer )
        free( info->stream_buffer );
    if( info->picture.au )
        free( info->picture.au );
    if( info->picture.incomplete_au )
        free( info->picture.incomplete_au );
    if( info->rbsp_buffer )
        free( info->rbsp_buffer );
    if( info->ts_list.timestamp )
        free( info->ts_list.timestamp );
    free( info );
}

static mp4sys_h264_info_t *mp4sys_create_h264_info( void )
{
    mp4sys_h264_info_t *info = malloc( sizeof(mp4sys_h264_info_t) );
    if( !info )
        return NULL;
    memset( info, 0, sizeof(mp4sys_h264_info_t) );
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
    info->stream_buffer = malloc( MP4SYS_H264_DEFAULT_BUFFER_SIZE );
    if( !info->stream_buffer )
    {
        mp4sys_remove_h264_info( info );
        return NULL;
    }
    info->stream_buffer_size = MP4SYS_H264_DEFAULT_BUFFER_SIZE;
    return info;
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
    if( bs->pos + 1 < bs->store )
        return 1;       /* rbsp_trailing_bits is placed at next or later byte. */
    if( bs->pos + 1 > bs->store )
    {
        bs->error = 1;
        return 0;
    }
    return (uint8_t)(bits->cache & ~(~0U << bits->store)) != (uint8_t)(1U << bits->store);
}

static int h264_check_nalu_header( h264_nalu_header_t *nalu_header, uint8_t **p_buf_pos, int use_long_start_code )
{
    uint8_t *buf_pos = *p_buf_pos;
    uint8_t forbidden_zero_bit = nalu_header->forbidden_zero_bit = (*buf_pos >> 7) & 0x01;
    uint8_t nal_ref_idc        = nalu_header->nal_ref_idc        = (*buf_pos >> 5) & 0x03;
    uint8_t nal_unit_type      = nalu_header->nal_unit_type      =  *buf_pos       & 0x1f;
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
            uint64_t slice_group_id_length = ceil( log( num_slice_groups_minus1 + 1 ) / 0.693147180559945 );
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
            sei->recovery_frame_cnt = h264_get_exp_golomb_ue( bits );
            sei->random_accessible = 1;
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
                uint64_t modification_of_pic_nums_idc
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
#if 0   /* We needn't read more. */
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

static lsmash_video_summary_t *h264_create_summary( mp4sys_h264_info_t *info, uint8_t *rbsp_buffer,
                                                    h264_nalu_header_t *sps_nalu_header,
                                                    uint8_t *sps_nalu, uint16_t sps_nalu_length,
                                                    h264_nalu_header_t *pps_nalu_header,
                                                    uint8_t *pps_nalu, uint16_t pps_nalu_length,
                                                    int probe )
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
                avcC->lengthSizeMinusOne         = 3;       /* We always use 4 bytes length. */
                avcC->numOfSequenceParameterSets = 1;
                avcC->chroma_format              = sps->chroma_format_idc;
                avcC->bit_depth_luma_minus8      = sps->bit_depth_luma_minus8;
                avcC->bit_depth_chroma_minus8    = sps->bit_depth_chroma_minus8;
                lsmash_remove_entries( avcC->sequenceParameterSets, isom_remove_avcC_ps );
                isom_avcC_ps_entry_t *ps = malloc( sizeof(isom_avcC_ps_entry_t) );
                if( !ps )
                    return NULL;
                ps->parameterSetLength = sps_nalu_length;
                ps->parameterSetNALUnit = malloc( sps_nalu_length );
                if( !ps->parameterSetNALUnit )
                {
                    free( ps );
                    return NULL;
                }
                memcpy( ps->parameterSetNALUnit, sps_nalu, sps_nalu_length );
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
                ps->parameterSetLength = pps_nalu_length;
                ps->parameterSetNALUnit = malloc( pps_nalu_length );
                if( !ps->parameterSetNALUnit )
                {
                    free( ps );
                    return NULL;
                }
                memcpy( ps->parameterSetNALUnit, pps_nalu, pps_nalu_length );
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
        summary = lsmash_create_video_summary();
        if( !summary )
            return NULL;
        memset( summary, 0, sizeof(lsmash_video_summary_t) );
        info->first_summary = 1;
    }
    /* Update summary here.
     * max_au_length is set at the last of mp4sys_h264_probe function. */
    summary->sample_type            = ISOM_CODEC_TYPE_AVC1_VIDEO;
    summary->object_type_indication = MP4SYS_OBJECT_TYPE_Visual_H264_ISO_14496_10;
    summary->stream_type            = MP4SYS_STREAM_TYPE_VisualStream;
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
    lsmash_cleanup_video_summary( summary );
    lsmash_bs_cleanup( bs );
    return NULL;
}

static inline int h264_update_picture_type( h264_picture_info_t *picture, h264_slice_info_t *slice )
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
    return 0;
}

static inline int h264_update_picture_info( h264_picture_info_t *picture, h264_slice_info_t *slice, h264_sei_t *sei )
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
    picture->random_accessible         |= (picture->idr || sei->random_accessible);
    picture->has_mmco5                 |= slice->has_mmco5;
    picture->has_redundancy            |= slice->has_redundancy;
    picture->recovery_frame_cnt         = sei->recovery_frame_cnt;
    if( h264_update_picture_type( picture, slice ) )
        return -1;
    picture->independent      = picture->type == H264_PICTURE_TYPE_I || picture->type == H264_PICTURE_TYPE_I_SI;
    picture->non_bipredictive = picture->type != H264_PICTURE_TYPE_I_P_B && picture->type != H264_PICTURE_TYPE_I_SI_P_SP_B;
    sei->random_accessible  = 0;
    sei->recovery_frame_cnt = 0;
    return 0;
}

static inline int h264_find_au_delimit( h264_slice_info_t *slice, h264_slice_info_t *prev_slice )
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

#define H264_SHORT_START_CODE_LENGTH 3
#define H264_LONG_START_CODE_LENGTH  4
#define CHECK_NEXT_SHORT_START_CODE( x ) (!(x)[0] && !(x)[1] && ((x)[2] == 0x01))
#define CHECK_NEXT_LONG_START_CODE( x ) (!(x)[0] && !(x)[1] && !(x)[2] && ((x)[3] == 0x01))
#define IF_BUFFER_SHORTAGE( anticipation_bytes, buffer_size ) \
    if( ((buf_pos + anticipation_bytes) >= buf_end) && !no_more_read ) \
    { \
        read_size = fread( buf + anticipation_bytes, 1, buffer_size - anticipation_bytes, importer->stream ); \
        buf_pos = buf; \
        buf_end = buf + anticipation_bytes + read_size; \
        no_more_read = (buf_pos + anticipation_bytes) >= buf_end ? feof( importer->stream ) : 0; \
    }
#define COMPLETE_AU \
    { \
        if( !probe ) \
            memcpy( au, incomplete_au, incomplete_au_length ); \
        au_length = incomplete_au_length; \
        incomplete_au_length = 0; \
        incomplete_au_has_primary = 0; \
    }

/* If probe equals 0, don't get the actual data (EBPS) of an access unit and only parse NALU.
 * Currently, you can get AU of AVC video elemental stream only, not AVC parameter set elemental stream defined in 14496-15. */
static int h264_get_access_unit_internal( mp4sys_importer_t *importer, mp4sys_h264_info_t *info, uint32_t track_number, int probe )
{
    h264_slice_info_t *slice           = &info->slice;
    h264_picture_info_t *picture       = &info->picture;
    lsmash_video_summary_t *summary    = info->summary;
    h264_nalu_header_t nalu_header     = info->nalu_header;
    uint8_t prev_nalu_type             = info->prev_nalu_type;
    uint8_t no_more_read               = info->no_more_read;
    uint8_t *buf                       = info->stream_buffer;
    uint8_t *buf_pos                   = info->stream_buffer_pos;
    uint8_t *buf_end                   = info->stream_buffer + info->stream_read_size;
    uint32_t buf_size                  = info->stream_buffer_size;
    uint64_t ebsp_head_pos             = info->ebsp_head_pos;
    uint8_t *rbsp_buffer               = info->rbsp_buffer;
    uint32_t rbsp_buffer_size          = info->rbsp_buffer_size;
    uint8_t *au                        = info->picture.au;
    uint32_t au_length                 = 0;
    uint8_t *incomplete_au             = info->picture.incomplete_au;
    uint32_t incomplete_au_length      = info->picture.incomplete_au_length;
    uint8_t  incomplete_au_has_primary = info->picture.incomplete_au_has_primary;
    uint32_t au_buffer_size            = info->au_buffer_size;
    uint32_t read_size = buf_end - buf;
    uint64_t consecutive_zero_byte_count = 0;
    uint64_t ebsp_length = 0;
    int      no_more_buf = 0;
    int      complete_au = 0;
    int      success = 0;
    picture->type              = H264_PICTURE_TYPE_NONE;
    picture->random_accessible = 0;
    picture->has_mmco5         = 0;
    picture->has_redundancy    = 0;
    while( 1 )
    {
        IF_BUFFER_SHORTAGE( 2, buf_size );
        no_more_buf = buf_pos >= buf_end;
        int no_more = no_more_read && no_more_buf;
        if( (((buf_pos + 2) < buf_end) && CHECK_NEXT_SHORT_START_CODE( buf_pos )) || no_more )
        {
            uint64_t next_nalu_head_pos = ebsp_head_pos + ebsp_length + !no_more * H264_SHORT_START_CODE_LENGTH;
            uint8_t *backup_pos = buf_pos;
            uint8_t nalu_type = nalu_header.nal_unit_type;
            int is_vcl_nalu = nalu_type >= 1 && nalu_type <= 5;
            int read_back = 0;
            int last_nalu = 0;
            if( no_more && ebsp_length == 0 )
            {
                /* For the last NALU. */
                ebsp_length = incomplete_au_length - nalu_header.length - 4;
                consecutive_zero_byte_count = 0;
                last_nalu = 1;
            }
#if 0
            if( probe )
            {
                fprintf( stderr, "NALU type: %"PRIu8"\n", nalu_type );
                fprintf( stderr, "    NALU header position: %"PRIx64"\n", ebsp_head_pos - nalu_header.length );
                fprintf( stderr, "    EBSP position: %"PRIx64"\n", ebsp_head_pos );
                fprintf( stderr, "    EBSP length: %"PRIx64" (%"PRIu64")\n", ebsp_length - consecutive_zero_byte_count,
                                                                             ebsp_length - consecutive_zero_byte_count );
                fprintf( stderr, "    consecutive_zero_byte_count: %"PRIx64"\n", consecutive_zero_byte_count );
                fprintf( stderr, "    Next NALU header position: %"PRIx64"\n", next_nalu_head_pos );
            }
#endif
            if( nalu_type == 12 )
            {
                /* We don't support streams with both filler and HRD yet.
                 * Otherwise, just skip filler because elemental streams defined in 14496-15 is forbidden to use filler. */
                if( info->sps.hrd_present )
                    return -1;
            }
            else if( (nalu_type >= 1 && nalu_type <= 13) || nalu_type == 19 )
            {
                /* Get the EBSP of the current NALU here.
                 * AVC elemental stream defined in 14496-15 can recognizes from 0 to 13, and 19 of nal_unit_type.
                 * We don't support SVC and MVC elemental stream defined in 14496-15 yet. */
                ebsp_length -= consecutive_zero_byte_count;     /* Any EBSP doesn't have zero bytes at the end. */
                uint64_t nalu_length = nalu_header.length + ebsp_length;
                uint32_t buf_distance = buf_pos - buf;
                if( buf_size < nalu_length )
                {
                    uint32_t alloc = nalu_length + (1 << 16);
                    uint8_t *temp = realloc( buf, alloc );
                    if( !temp )
                        break;
                    buf = temp;
                    buf_size = alloc;
                    buf_pos = buf + buf_distance;
                    buf_end = buf + read_size;
                    backup_pos = buf_pos;
                }
                /* Move to the first byte of the current NALU. */
                read_back = buf_distance < (nalu_length + consecutive_zero_byte_count);
                if( read_back )
                {
                    lsmash_fseek( importer->stream, ebsp_head_pos - nalu_header.length, SEEK_SET );
                    int read_fail = fread( buf, 1, nalu_length, importer->stream ) != nalu_length;
                    read_size = nalu_length;
                    buf_pos = buf;
                    buf_end = buf + read_size;
                    if( read_fail )
                        break;
#if 0
                    if( probe )
                        fprintf( stderr, "    ----Read Back\n" );
#endif
                }
                else
                    buf_pos -= nalu_length + consecutive_zero_byte_count;
                if( is_vcl_nalu || nalu_type == 6 || nalu_type == 7 || nalu_type == 8 )
                {
                    /* Alloc buffer for EBSP to RBSP. */
                    if( rbsp_buffer_size < ebsp_length )
                    {
                        uint32_t alloc = ebsp_length + (1 << 16);
                        uint8_t *temp = rbsp_buffer ? realloc( rbsp_buffer, alloc ) : malloc( alloc );
                        if( !temp )
                            break;
                        rbsp_buffer = temp;
                        rbsp_buffer_size = alloc;
                    }
                }
                if( is_vcl_nalu )
                {
                    /* VCL NALU (slice) */
                    h264_slice_info_t prev_slice = *slice;
                    memset( slice, 0, sizeof(h264_slice_info_t) );
                    if( h264_parse_slice_header( info->bits, &info->sps, &info->pps, slice, &nalu_header, rbsp_buffer, buf_pos + nalu_header.length, ebsp_length ) )
                        break;
                    if( prev_slice.present || last_nalu )
                    {
                        /* Check whether the AU that contains the previous VCL NALU completed or not. */
                        if( (incomplete_au_has_primary && h264_find_au_delimit( slice, &prev_slice )) || last_nalu )
                        {
                            /* The current NALU is the first VCL NALU of the primary coded picture of an new AU.
                             * Therefore, the previous slice belongs to the AU you want at this time. */
                            if( h264_update_picture_info( picture, &prev_slice, &info->sei ) )
                                break;
                            complete_au = 1;
                        }
                        else
                        {
                            picture->random_accessible |= (picture->idr || info->sei.random_accessible);
                            picture->has_mmco5         |= prev_slice.has_mmco5;
                            picture->has_redundancy    |= prev_slice.has_redundancy;
                            if( h264_update_picture_type( picture, &prev_slice ) )
                                break;
                            picture->independent      = picture->type == H264_PICTURE_TYPE_I || picture->type == H264_PICTURE_TYPE_I_SI;
                            picture->non_bipredictive = picture->type != H264_PICTURE_TYPE_I_P_B && picture->type != H264_PICTURE_TYPE_I_SI_P_SP_B;
                            info->sei.random_accessible  = 0;
                            info->sei.recovery_frame_cnt = 0;
                        }
                        prev_slice.present = 0;     /* Discard the previous slice info. */
                    }
                    slice->present = 1;
                }
                else if( ((nalu_type >= 6 && nalu_type <= 9) || (nalu_type >= 14 && nalu_type <= 18))
                 && ((prev_nalu_type >= 1 && prev_nalu_type <= 5) || prev_nalu_type == 12 || prev_nalu_type == 19) )
                {
                    /* The last slice belongs to the AU you want at this time. */
                    if( h264_update_picture_info( picture, slice, &info->sei ) )
                        break;
                    slice->present = 0;     /* Discard the current slice info. */
                    complete_au = 1;
                }
                else if( no_more )
                    complete_au = 1;
                if( nalu_type == 6 )
                {
                    if( h264_parse_sei_nalu( info->bits, &info->sei, &nalu_header, rbsp_buffer, buf_pos + nalu_header.length, ebsp_length ) )
                        break;
                }
                else if( nalu_type == 7 )
                    summary = h264_create_summary( info, rbsp_buffer, &nalu_header, buf_pos, nalu_length, NULL, NULL, 0, probe );
                else if( nalu_type == 8 )
                    summary = h264_create_summary( info, rbsp_buffer, NULL, NULL, 0, &nalu_header, buf_pos, nalu_length, probe );
                else if( nalu_type == 13 )
                    return -1;      /* We don't support sequence parameter set extension yet. */
                if( complete_au && incomplete_au_has_primary )
                {
                    COMPLETE_AU;
                    if( last_nalu )
                    {
                        success = 1;
                        break;
                    }
                }
                if( is_vcl_nalu || nalu_type == 6
                 || (nalu_type >= 9 && nalu_type <= 11) || nalu_type == 19 )
                {
                    /* Append this NALU into access unit. */
                    uint64_t needed_au_length = incomplete_au_length + 4 + nalu_length;
                    if( au_buffer_size < needed_au_length )
                    {
                        uint32_t alloc = needed_au_length + (1 << 16);
                        uint8_t *temp = incomplete_au ? realloc( incomplete_au, alloc ) : malloc( alloc );
                        if( !temp )
                            break;
                        incomplete_au = temp;
                        temp = au ? realloc( au, alloc ) : malloc( alloc );
                        if( !temp )
                            break;
                        au = temp;
                        au_buffer_size = alloc;
                    }
                    if( !probe )
                    {
                        uint8_t four_bytes_nalu_length[4];
                        four_bytes_nalu_length[0] = (nalu_length >> 24) & 0xff;
                        four_bytes_nalu_length[1] = (nalu_length >> 16) & 0xff;
                        four_bytes_nalu_length[2] = (nalu_length >>  8) & 0xff;
                        four_bytes_nalu_length[3] =  nalu_length        & 0xff;
                        memcpy( incomplete_au + incomplete_au_length, four_bytes_nalu_length, 4 );
                        memcpy( incomplete_au + incomplete_au_length + 4, buf_pos, nalu_length );
                    }
                    incomplete_au_length = needed_au_length;
                    if( is_vcl_nalu )
                        incomplete_au_has_primary |= !slice->has_redundancy;
                }
            }
            /* Move to the first byte of the next EBSP. */
            if( read_back )
            {
                lsmash_fseek( importer->stream, next_nalu_head_pos, SEEK_SET );
                read_size = fread( buf, 1, buf_size, importer->stream );
                buf_pos = buf;
                buf_end = buf + read_size;
            }
            else
                buf_pos = backup_pos + H264_SHORT_START_CODE_LENGTH;
            prev_nalu_type = nalu_type;
            IF_BUFFER_SHORTAGE( 0, buf_size );
            no_more_buf = buf_pos >= buf_end;
            ebsp_length = 0;
            no_more = no_more_read && no_more_buf;
            if( !no_more )
            {
                /* Check the next NALU header. */
                if( h264_check_nalu_header( &nalu_header, &buf_pos, !!consecutive_zero_byte_count ) )
                    break;
                ebsp_head_pos = next_nalu_head_pos + nalu_header.length;
            }
            if( complete_au || no_more )
            {
                /* If there is no data in the stream, and flushed chunk of NALUs, flush it as complete AU here. */
                if( no_more && incomplete_au_length && au_length == 0 )
                {
                    if( h264_update_picture_info( picture, slice, &info->sei ) )
                        break;
                    slice->present = 0;     /* redundant */
                    COMPLETE_AU;
                }
                success = 1;
                break;
            }
            consecutive_zero_byte_count = 0;
            continue;       /* Avoid increment of ebsp_length. */
        }
        else if( !no_more )
        {
            if( *buf_pos++ )
                consecutive_zero_byte_count = 0;
            else
                ++consecutive_zero_byte_count;
        }
        ++ebsp_length;
    }
    info->status                            = no_more_read && no_more_buf && incomplete_au_length == 0 ? MP4SYS_IMPORTER_EOF : MP4SYS_IMPORTER_OK;
    info->summary                           = summary;
    info->nalu_header                       = nalu_header;
    info->prev_nalu_type                    = prev_nalu_type;
    info->no_more_read                      = no_more_read;
    info->stream_buffer                     = buf;
    info->stream_buffer_pos                 = buf_pos;
    info->stream_read_size                  = buf_end - buf;
    info->stream_buffer_size                = buf_size;
    info->ebsp_head_pos                     = ebsp_head_pos;
    info->rbsp_buffer                       = rbsp_buffer;
    info->rbsp_buffer_size                  = rbsp_buffer_size;
    info->au_buffer_size                    = au_buffer_size;
    info->picture.au                        = au;
    info->picture.au_length                 = au_length;
    info->picture.incomplete_au             = incomplete_au;
    info->picture.incomplete_au_length      = incomplete_au_length;
    info->picture.incomplete_au_has_primary = incomplete_au_has_primary;
    info->picture.au_number                += 1;
    return (!success || !au_length) ? -1 : 0;
}

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
        lsmash_cleanup_video_summary( entry->data );
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
    buffered_sample->prop.recovery.identifier = picture->frame_num;
    if( picture->random_accessible )
    {
        if( picture->idr )
            buffered_sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
        else if( picture->recovery_frame_cnt )
        {
            buffered_sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_RECOVERY;
            buffered_sample->prop.recovery.complete = (picture->frame_num + picture->recovery_frame_cnt) % sps->MaxFrameNum;
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
    /* Find the first start code. */
    uint8_t buf[MP4SYS_H264_DEFAULT_BUFFER_SIZE];
    int found_start_code = 0;
    uint32_t read_size = fread( buf, 1, MP4SYS_H264_DEFAULT_BUFFER_SIZE, importer->stream );
    uint8_t *buf_pos = buf;
    uint8_t *buf_end = buf + read_size;
    int no_more_read = buf >= buf_end ? feof( importer->stream ) : 0;
    while( 1 )
    {
        /* Invalid if encountered any value of non-zero before the first start code. */
        IF_INVALID_VALUE( *buf_pos )
            return -1;
        /* The first NALU of an AU in decoding order shall have long start code (0x00000001). */
        if( CHECK_NEXT_LONG_START_CODE( buf_pos ) )
        {
            found_start_code = 1;
            break;
        }
        if( (buf_pos + H264_LONG_START_CODE_LENGTH) == buf_end )
            break;
        ++buf_pos;
    }
    /* If the first trial of finding long start code failed, we assume this stream is not byte stream format of H.264. */
    if( !found_start_code )
        return -1;
    buf_pos += H264_LONG_START_CODE_LENGTH;
    IF_BUFFER_SHORTAGE( 0, MP4SYS_H264_DEFAULT_BUFFER_SIZE );
    h264_nalu_header_t first_nalu_header;
    if( h264_check_nalu_header( &first_nalu_header, &buf_pos, 1 ) )
        return -1;
    uint64_t first_ebsp_head_pos = buf_pos - buf;       /* EBSP doesn't include NALU header. */
    /* Check existence of a complete AU. */
    mp4sys_h264_info_t *info = mp4sys_create_h264_info();
    if( !info )
        return -1;
    info->status        = no_more_read ? MP4SYS_IMPORTER_EOF : MP4SYS_IMPORTER_OK;
    info->nalu_header   = first_nalu_header;
    info->ebsp_head_pos = first_ebsp_head_pos;
    memcpy( info->stream_buffer, buf, read_size );
    info->stream_buffer_pos = &info->stream_buffer[buf_pos - buf];
    info->stream_read_size  = read_size;
    /* Parse all NALU in the stream for preparation of calculating timestamps. */
    uint32_t num_access_units = 0;
    uint64_t *poc = NULL;
    uint32_t poc_alloc = 0;
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
        if( poc_alloc <= num_access_units )
        {
            uint32_t alloc = (num_access_units + (1 << 12)) * sizeof(uint64_t);
            uint64_t *temp = poc ? realloc( poc, alloc ) : malloc( alloc );
            if( !temp )
            {
                if( poc )
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
    read_size = fread( info->stream_buffer, 1, info->stream_buffer_size, importer->stream );
    info->status                            = MP4SYS_IMPORTER_OK;
    info->nalu_header                       = first_nalu_header;
    info->prev_nalu_type                    = 0;
    info->no_more_read                      = 0;
    info->first_summary                     = 0;
    info->summary->max_au_length            = info->max_au_length;
    info->summary                           = NULL;
    info->stream_buffer_pos                 = info->stream_buffer;
    info->stream_read_size                  = read_size;
    info->ebsp_head_pos                     = first_ebsp_head_pos;
    uint8_t *temp_au                        = info->picture.au;
    uint8_t *temp_incomplete_au             = info->picture.incomplete_au;
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
    lsmash_remove_entries( importer->summaries, lsmash_cleanup_video_summary );
    return -1;
}

static void mp4sys_h264_remove_sumary( void *summary )
{
    lsmash_cleanup_video_summary( (lsmash_video_summary_t *)summary );
}

const static mp4sys_importer_functions mp4sys_h264_importer = {
    "H.264",
    1,
    mp4sys_h264_probe,
    mp4sys_h264_get_accessunit,
    mp4sys_h264_cleanup,
    mp4sys_h264_remove_sumary
};

/***************************************************************************
    importer public interfaces
***************************************************************************/


/******** importer listing table ********/
const static mp4sys_importer_functions* mp4sys_importer_tbl[] = {
    &mp4sys_adts_importer,
    &mp4sys_mp3_importer,
    &mp4sys_amr_importer,
    &mp4sys_h264_importer,
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
    lsmash_remove_list( importer->summaries, importer->funcs.remove_summary );
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

lsmash_video_summary_t *mp4sys_duplicate_video_summary( mp4sys_importer_t *importer, uint32_t track_number )
{
    if( !importer )
        return NULL;
    lsmash_video_summary_t *summary = lsmash_create_video_summary();
    if( !summary )
        return NULL;
    lsmash_video_summary_t *src_summary = lsmash_get_entry_data( importer->summaries, track_number );
    if( !src_summary )
        return NULL;
    memcpy( summary, src_summary, sizeof(lsmash_video_summary_t) );
    summary->exdata = NULL;
    summary->exdata_length = 0;
    if( lsmash_summary_add_exdata( (lsmash_summary_t *)summary, src_summary->exdata, src_summary->exdata_length ) )
    {
        free( summary );
        return NULL;
    }
    return summary;
}

lsmash_audio_summary_t* mp4sys_duplicate_audio_summary( mp4sys_importer_t* importer, uint32_t track_number )
{
    if( !importer )
        return NULL;
    lsmash_audio_summary_t* summary = lsmash_create_audio_summary();
    if( !summary )
        return NULL;
    lsmash_audio_summary_t* src_summary = lsmash_get_entry_data( importer->summaries, track_number );
    if( !src_summary )
        return NULL;
    memcpy( summary, src_summary, sizeof(lsmash_audio_summary_t) );
    summary->exdata = NULL;
    summary->exdata_length = 0;
    if( lsmash_summary_add_exdata( (lsmash_summary_t *)summary, src_summary->exdata, src_summary->exdata_length ) )
    {
        free( summary );
        return NULL;
    }
    return summary;
}
