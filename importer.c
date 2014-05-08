/*****************************************************************************
 * importer.c:
 *****************************************************************************
 * Copyright (C) 2010-2014 L-SMASH project
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
#include "description.h"

/***************************************************************************
    importer framework
***************************************************************************/
struct importer_tag;

static const lsmash_class_t lsmash_importer_class =
{
    "importer"
};

typedef void     ( *importer_cleanup )          ( struct importer_tag * );
typedef int      ( *importer_get_accessunit )   ( struct importer_tag *, uint32_t, lsmash_sample_t * );
typedef int      ( *importer_probe )            ( struct importer_tag * );
typedef uint32_t ( *importer_get_last_duration )( struct importer_tag *, uint32_t );

typedef struct
{
    lsmash_class_t             class;
    int                        detectable;
    importer_probe             probe;
    importer_get_accessunit    get_accessunit;
    importer_get_last_duration get_last_delta;
    importer_cleanup           cleanup;
} importer_functions;

typedef struct importer_tag
{
    const lsmash_class_t   *class;
    lsmash_stream_buffers_t sb;
    FILE                   *stream;    /* will be deprecated */
    int                     is_stdin;
    void                   *info;      /* importer internal status information. */
    importer_functions      funcs;
    lsmash_entry_list_t    *summaries;
} importer_t;

typedef enum
{
    IMPORTER_ERROR  = -1,
    IMPORTER_OK     = 0,
    IMPORTER_CHANGE = 1,
    IMPORTER_EOF    = 2,
} importer_status;

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
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
    if( !summary )
        return NULL;
    summary->sample_type            = ISOM_CODEC_TYPE_MP4A_AUDIO;
    summary->max_au_length          = MP4SYS_ADTS_MAX_FRAME_LENGTH;
    summary->frequency              = mp4a_sampling_frequency_table[header->sampling_frequency_index][1];
    summary->channels               = header->channel_configuration + ( header->channel_configuration == 0x07 ); /* 0x07 means 7.1ch */
    summary->sample_size            = 16;
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
    uint32_t data_length;
    uint8_t *data = mp4a_export_AudioSpecificConfig( header->profile_ObjectType + MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN,
                                                     summary->frequency, summary->channels, summary->sbr_mode,
                                                     NULL, 0, &data_length );
    if( !data )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
    lsmash_codec_specific_t *specific = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_MP4SYS_DECODER_CONFIG,
                                                                           LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
    if( !specific )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_free( data );
        return NULL;
    }
    lsmash_mp4sys_decoder_parameters_t *param = (lsmash_mp4sys_decoder_parameters_t *)specific->data.structured;
    param->objectTypeIndication = MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3;
    param->streamType           = MP4SYS_STREAM_TYPE_AudioStream;
    if( lsmash_set_mp4sys_decoder_specific_info( param, data, data_length ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        lsmash_free( data );
        return NULL;
    }
    lsmash_free( data );
    if( lsmash_add_entry( &summary->opaque->list, specific ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        return NULL;
    }
    return summary;
}

typedef struct
{
    importer_status               status;
    unsigned int                  raw_data_block_idx;
    mp4sys_adts_fixed_header_t    header;
    mp4sys_adts_variable_header_t variable_header;
    uint32_t                      samples_in_frame;
    uint32_t                      au_number;
} mp4sys_adts_info_t;

static int mp4sys_adts_get_accessunit( importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    mp4sys_adts_info_t* info = (mp4sys_adts_info_t*)importer->info;
    importer_status current_status = info->status;
    uint16_t raw_data_block_size = info->variable_header.raw_data_block_size[info->raw_data_block_idx];
    if( current_status == IMPORTER_ERROR || buffered_sample->length < raw_data_block_size )
        return -1;
    if( current_status == IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( current_status == IMPORTER_CHANGE )
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
        info->status = IMPORTER_ERROR;
        return -1;
    }
    buffered_sample->length = raw_data_block_size;
    buffered_sample->dts = info->au_number++ * info->samples_in_frame;
    buffered_sample->cts = buffered_sample->dts;
    buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    buffered_sample->prop.pre_roll.distance = 1;    /* MDCT */

    /* now we succeeded to read current frame, so "return" takes 0 always below. */

    /* skip adts_raw_data_block_error_check() */
    if( info->header.protection_absent == 0
        && info->variable_header.number_of_raw_data_blocks_in_frame != 0
        && fread( buffered_sample->data, 1, 2, importer->stream ) != 2 )
    {
        info->status = IMPORTER_ERROR;
        return 0;
    }
    /* current adts_frame() has any more raw_data_block()? */
    if( info->raw_data_block_idx < info->variable_header.number_of_raw_data_blocks_in_frame )
    {
        info->raw_data_block_idx++;
        info->status = IMPORTER_OK;
        return 0;
    }
    info->raw_data_block_idx = 0;

    /* preparation for next frame */

    uint8_t buf[MP4SYS_ADTS_MAX_FRAME_LENGTH];
    size_t ret = fread( buf, 1, MP4SYS_ADTS_BASIC_HEADER_LENGTH, importer->stream );
    if( ret == 0 )
    {
        info->status = IMPORTER_EOF;
        return 0;
    }
    if( ret != MP4SYS_ADTS_BASIC_HEADER_LENGTH )
    {
        info->status = IMPORTER_ERROR;
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
        info->status = IMPORTER_ERROR;
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
        info->status = IMPORTER_ERROR;
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
        info->status = IMPORTER_CHANGE;
        return 0;
    }
    /* no change which matters to mp4 muxing was found */
    info->status = IMPORTER_OK;
    return 0;
}

static void mp4sys_adts_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        lsmash_free( importer->info );
}

/* returns 0 if it seems adts. */
static int mp4sys_adts_probe( importer_t *importer )
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
    info->status = IMPORTER_OK;
    info->raw_data_block_idx = 0;
    info->header = header;
    info->variable_header = variable_header;
    info->samples_in_frame = summary->samples_in_frame;

    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        lsmash_free( info );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    importer->info = info;

    return 0;
}

static uint32_t mp4sys_adts_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    mp4sys_adts_info_t *info = (mp4sys_adts_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != IMPORTER_EOF )
        return 0;
    return info->samples_in_frame;
}

const static importer_functions mp4sys_adts_importer =
{
    { "adts" },
    1,
    mp4sys_adts_probe,
    mp4sys_adts_get_accessunit,
    mp4sys_adts_get_last_delta,
    mp4sys_adts_cleanup
};

/***************************************************************************
    mp3 (Legacy Interface) importer
***************************************************************************/

#define USE_MP4SYS_LEGACY_INTERFACE 1

static void mp4sys_mp3_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        lsmash_free( importer->info );
}

typedef struct
{
    uint16_t syncword;           /* <12> */
    uint8_t  ID;                 /* <1> */
    uint8_t  layer;              /* <2> */
    uint8_t  protection_bit;     /* <1> */
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
    header->protection_bit     = (data >> 16) & 0x1;
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
#define MP4SYS_MODE_IS_2CH( mode )  ((mode)!=3)
#define MP4SYS_LAYER_III            0x1
#define MP4SYS_LAYER_II             0x2
#define MP4SYS_LAYER_I              0x3

static const uint32_t mp4sys_mp3_frequency_tbl[2][3] = {
    { 22050, 24000, 16000 }, /* MPEG-2 BC audio */
    { 44100, 48000, 32000 }  /* MPEG-1 audio */
};

static int mp4sys_mp3_samples_in_frame( mp4sys_mp3_header_t *header )
{
    if( header->layer == MP4SYS_LAYER_I )
        return 384;
    else if( header->ID == 1 || header->layer == MP4SYS_LAYER_II )
        return 1152;
    else
        return 576;
}

static lsmash_audio_summary_t *mp4sys_mp3_create_summary( mp4sys_mp3_header_t *header, int legacy_mode )
{
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
    if( !summary )
        return NULL;
    summary->sample_type            = ISOM_CODEC_TYPE_MP4A_AUDIO;
    summary->max_au_length          = MP4SYS_MP3_MAX_FRAME_LENGTH;
    summary->frequency              = mp4sys_mp3_frequency_tbl[header->ID][header->sampling_frequency];
    summary->channels               = MP4SYS_MODE_IS_2CH( header->mode ) + 1;
    summary->sample_size            = 16;
    summary->samples_in_frame       = mp4sys_mp3_samples_in_frame( header );
    summary->aot                    = MP4A_AUDIO_OBJECT_TYPE_Layer_1 + (MP4SYS_LAYER_I - header->layer); /* no effect with Legacy Interface. */
    summary->sbr_mode               = MP4A_AAC_SBR_NOT_SPECIFIED; /* no effect */
#if !USE_MP4SYS_LEGACY_INTERFACE /* FIXME: This is very unstable. Many players crash with this. */
    if( !legacy_mode )
    {
        summary->object_type_indication = MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3;
        if( lsmash_setup_AudioSpecificConfig( summary ) )
        {
            lsmash_cleanup_summary( summary );
            return NULL;
        }
    }
    uint32_t data_length;
    uint8_t *data = mp4a_export_AudioSpecificConfig( MP4A_AUDIO_OBJECT_TYPE_Layer_1 + (MP4SYS_LAYER_I - header->layer),
                                                     summary->frequency, summary->channels, summary->sbr_mode,
                                                     NULL, 0, &data_length );
    if( !data )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
#endif
    lsmash_codec_specific_t *specific = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_MP4SYS_DECODER_CONFIG,
                                                                           LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
    if( !specific )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
#if !USE_MP4SYS_LEGACY_INTERFACE
        lsmash_free( data );
#endif
        return NULL;
    }
    lsmash_mp4sys_decoder_parameters_t *param = (lsmash_mp4sys_decoder_parameters_t *)specific->data.structured;
    param->objectTypeIndication = header->ID ? MP4SYS_OBJECT_TYPE_Audio_ISO_11172_3 : MP4SYS_OBJECT_TYPE_Audio_ISO_13818_3;
    param->streamType           = MP4SYS_STREAM_TYPE_AudioStream;
#if !USE_MP4SYS_LEGACY_INTERFACE
    if( lsmash_set_mp4sys_decoder_specific_info( param, data, data_length ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        lsmash_free( data );
        return NULL;
    }
    lsmash_free( data );
#endif
    if( lsmash_add_entry( &summary->opaque->list, specific ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        return NULL;
    }
    return summary;
}

typedef struct
{
    importer_status     status;
    mp4sys_mp3_header_t header;
    uint8_t             raw_header[MP4SYS_MP3_HEADER_LENGTH];
    uint32_t            samples_in_frame;
    uint32_t            au_number;
    uint16_t            main_data_size[32]; /* size of main_data of the last 32 frames, FIFO */
    uint16_t            prev_preroll_count; /* number of dependent frames of *previous* frame */
    uint16_t            enc_delay;
    uint16_t            padding;
    uint64_t            valid_samples;
} mp4sys_mp3_info_t;

static int parse_xing_info_header( mp4sys_mp3_info_t *info, mp4sys_mp3_header_t *header, uint8_t *frame )
{
    unsigned int sip = header->protection_bit ? 4 : 6;
    unsigned int side_info_size;
    if( header->ID == 1 )
        side_info_size = MP4SYS_MODE_IS_2CH( header->mode ) ? 32 : 17;
    else
        side_info_size = MP4SYS_MODE_IS_2CH( header->mode ) ? 17 : 9;

    uint8_t *mdp = frame + sip + side_info_size;
    if( memcmp( mdp, "Info", 4 ) && memcmp( mdp, "Xing", 4 ) )
        return 0;
    uint32_t flags = LSMASH_4CC( mdp[4], mdp[5], mdp[6], mdp[7] );
    uint32_t off = 8;
    uint32_t frame_count = 0;
    if( flags & 1 )
    {
        frame_count = LSMASH_4CC( mdp[8], mdp[9], mdp[10], mdp[11] );
        info->valid_samples = (uint64_t)frame_count * mp4sys_mp3_samples_in_frame( header );
        off += 4;
    }
    if( flags & 2 ) off +=   4; /* file size    */
    if( flags & 4 ) off += 100; /* TOC          */
    if( flags & 8 ) off +=   4; /* VBR quality  */

    if( mdp[off] == 'L' )
    {   /* LAME header present */
        unsigned v = (mdp[off + 21] << 16) | (mdp[off + 22] << 8) | mdp[off + 23];
        info->enc_delay     = v >> 12;
        info->padding       = v & 0xfff;
        if( frame_count )
            info->valid_samples -= info->enc_delay + info->padding;
    }
    return 1;
}

static int parse_vbri_header( mp4sys_mp3_info_t *info, mp4sys_mp3_header_t *header, uint8_t *frame )
{
    return memcmp( frame + 36, "VBRI", 4 ) == 0;
}

static int mp4sys_mp3_get_accessunit( importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    mp4sys_mp3_info_t   *info           = (mp4sys_mp3_info_t *)importer->info;
    mp4sys_mp3_header_t *header         = (mp4sys_mp3_header_t *)&info->header;
    importer_status      current_status = info->status;

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
        uint32_t div = frequency;
        if( header->layer == MP4SYS_LAYER_III && header->ID == 0 )
            div <<= 1;
        frame_size = 144 * 1000 * bitrate / div + header->padding_bit;
    }

    if( current_status == IMPORTER_ERROR || frame_size <= 4 || buffered_sample->length < frame_size )
        return -1;
    if( current_status == IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( current_status == IMPORTER_CHANGE )
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
        info->status = IMPORTER_ERROR;
        return -1;
    }
    buffered_sample->length = MP4SYS_MP3_HEADER_LENGTH + frame_size;
    buffered_sample->dts = info->au_number++ * info->samples_in_frame;
    buffered_sample->cts = buffered_sample->dts;
    buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    buffered_sample->prop.pre_roll.distance = header->layer == MP4SYS_LAYER_III ? 1 : 0;    /* Layer III uses MDCT */

    int vbr_header_present = 0;
    if( info->au_number == 1
     && (parse_xing_info_header( info, header, buffered_sample->data )
      || parse_vbri_header( info, header, buffered_sample->data )) )
    {
        vbr_header_present = 1;
        info->au_number--;
    }

    /* handle additional inter-frame dependency due to bit reservoir */
    if( !vbr_header_present && header->layer == MP4SYS_LAYER_III )
    {
        /* position of side_info */
        unsigned int sip = header->protection_bit ? 4 : 6;
        unsigned int main_data_begin = buffered_sample->data[sip];
        if( header->ID == 1 )
        {
            main_data_begin <<= 1;
            main_data_begin |= (buffered_sample->data[sip + 1] >> 7);
        }
        if( main_data_begin > 0 )
        {
            /* main_data_begin is a backpointer to the start of
             * bit reservoir data for this frame.
             * it contains total amount of bytes required from
             * preceding frames.
             * we just add up main_data size from history until it reaches
             * the required amount.
             */
            unsigned int reservoir_data = 0;
            unsigned int i;
            for( i = 0; i < 32 && reservoir_data < main_data_begin; ++i )
            {
                reservoir_data += info->main_data_size[i];
                if( info->main_data_size[i] == 0 )
                    break;
            }
            buffered_sample->prop.pre_roll.distance += info->prev_preroll_count;
            info->prev_preroll_count = i;
        }
        uint16_t side_info_size;
        if( header->ID == 1 )
            side_info_size = MP4SYS_MODE_IS_2CH( header->mode ) ? 32 : 17;
        else
            side_info_size = MP4SYS_MODE_IS_2CH( header->mode ) ? 17 : 9;

        /* pop back main_data_size[] and push main_data size of this frame
         * to the front */
        memmove( info->main_data_size + 1, info->main_data_size, sizeof(info->main_data_size) - sizeof( info->main_data_size[0] ) );
        info->main_data_size[0] = frame_size - sip - side_info_size;
    }
    /* now we succeeded to read current frame, so "return" takes 0 always below. */
    /* preparation for next frame */

    uint8_t buf[MP4SYS_MP3_HEADER_LENGTH];
    size_t ret = fread( buf, 1, MP4SYS_MP3_HEADER_LENGTH, importer->stream );
    if( ret == 0 )
    {
        info->status = IMPORTER_EOF;
        return 0;
    }
    if( ret >= 2 && (!memcmp( buf, "TA", 2 ) || !memcmp( buf, "AP", 2 )) )
    {
        /* ID3v1 or APE tag */
        info->status = IMPORTER_EOF;
        return 0;
    }
    if( ret == 1 && *buf == 0x00 )
    {
        /* NOTE: ugly hack for mp1 stream created with SCMPX. */
        info->status = IMPORTER_EOF;
        return 0;
    }
    if( ret != MP4SYS_MP3_HEADER_LENGTH )
    {
        info->status = IMPORTER_ERROR;
        return 0;
    }

    mp4sys_mp3_header_t new_header = {0};
    if( mp4sys_mp3_parse_header( buf, &new_header ) )
    {
        info->status = IMPORTER_ERROR;
        return 0;
    }
    memcpy( info->raw_header, buf, MP4SYS_MP3_HEADER_LENGTH );

    /* currently UNsupported "change(s)". */
    if( header->layer != new_header.layer /* This means change of object_type_indication with Legacy Interface. */
        || header->sampling_frequency != new_header.sampling_frequency ) /* This may change timescale. */
    {
        info->status = IMPORTER_ERROR;
        return 0;
    }

    /* currently supported "change(s)". */
    if( MP4SYS_MODE_IS_2CH( header->mode ) != MP4SYS_MODE_IS_2CH( new_header.mode ) )
        info->status = IMPORTER_CHANGE;
    else
        info->status = IMPORTER_OK; /* no change which matters to mp4 muxing was found */
    info->header = new_header;

    if( vbr_header_present )
        return mp4sys_mp3_get_accessunit( importer, track_number, buffered_sample );
    return 0;
}

static int mp4sys_mp3_probe( importer_t *importer )
{
    int c;
    if( (c = getc( importer->stream )) == 'I'
     && (c = getc( importer->stream )) == 'D'
     && (c = getc( importer->stream )) == '3' )
    {
        lsmash_fseek( importer->stream, 3, SEEK_CUR );
        uint32_t size = 0;
        for( int i = 0 ; i < 4; i++ )
        {
            size <<= 7;
            size |= getc( importer->stream );
        }
        lsmash_fseek( importer->stream, size, SEEK_CUR );
    }
    else
        ungetc( c, importer->stream );

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
    info->status = IMPORTER_OK;
    info->header = header;
    info->samples_in_frame = summary->samples_in_frame;
    memcpy( info->raw_header, buf, MP4SYS_MP3_HEADER_LENGTH );

    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        lsmash_free( info );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    importer->info = info;

    return 0;
}

static uint32_t mp4sys_mp3_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    mp4sys_mp3_info_t *info = (mp4sys_mp3_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != IMPORTER_EOF )
        return 0;
    return info->samples_in_frame;
}

const static importer_functions mp4sys_mp3_importer =
{
    { "MPEG-1/2BC_Audio_Legacy" },
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
static void amr_cleanup( importer_t* importer )
{
    debug_if( importer && importer->info )
        lsmash_free( importer->info );
}

typedef struct
{
    uint8_t  wb;
    uint32_t samples_in_frame;
    uint32_t au_number;
} amr_importer_info_t;

static int amr_get_accessunit( importer_t* importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( track_number != 1 )
        return -1;
    amr_importer_info_t *info = (amr_importer_info_t *)importer->info;

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
    buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    return 0;
}

#define MP4SYS_DAMR_LENGTH 17

int mp4sys_amr_create_damr( lsmash_audio_summary_t *summary )
{
    lsmash_bs_t* bs = lsmash_bs_create();
    if( !bs )
        return -1;
    lsmash_bs_put_be32( bs, MP4SYS_DAMR_LENGTH );
    lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_DAMR.fourcc );
    /* NOTE: These are specific to each codec vendor, but we're surely not a vendor.
              Using dummy data. */
    lsmash_bs_put_be32( bs, 0x20202020 ); /* vendor */
    lsmash_bs_put_byte( bs, 0 );          /* decoder_version */
    /* NOTE: Using safe value for these settings, maybe sub-optimal. */
    lsmash_bs_put_be16( bs, 0x83FF );     /* mode_set, represents for possibly existing frame-type (0x83FF == all). */
    lsmash_bs_put_byte( bs, 1 );          /* mode_change_period */
    lsmash_bs_put_byte( bs, 1 );          /* frames_per_sample */
    lsmash_codec_specific_t *specific = lsmash_malloc_zero( sizeof(lsmash_codec_specific_t) );
    if( !specific )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_bs_cleanup( bs );
        return -1;
    }
    specific->type              = LSMASH_CODEC_SPECIFIC_DATA_TYPE_UNKNOWN;
    specific->format            = LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED;
    specific->destruct          = (lsmash_codec_specific_destructor_t)lsmash_free;
    specific->data.unstructured = lsmash_bs_export_data( bs, &specific->size );
    specific->size              = MP4SYS_DAMR_LENGTH;
    lsmash_bs_cleanup( bs );
    if( !specific->data.unstructured
     || lsmash_add_entry( &summary->opaque->list, specific ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        return -1;
    }
    return 0;
}

#define MP4SYS_AMR_STORAGE_MAGIC_LENGTH 6
#define MP4SYS_AMRWB_EX_MAGIC_LENGTH 3

static int amr_probe( importer_t* importer )
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
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
    if( !summary )
        return -1;
    summary->sample_type            = wb ? ISOM_CODEC_TYPE_SAWB_AUDIO : ISOM_CODEC_TYPE_SAMR_AUDIO;
    summary->max_au_length          = wb ? 61 : 32;
    summary->aot                    = MP4A_AUDIO_OBJECT_TYPE_NULL; /* no effect */
    summary->frequency              = (8000 << wb);
    summary->channels               = 1;
    summary->sample_size            = 16;
    summary->samples_in_frame       = (160 << wb);
    summary->sbr_mode               = MP4A_AAC_SBR_NOT_SPECIFIED; /* no effect */
    amr_importer_info_t *info = lsmash_malloc( sizeof(amr_importer_info_t) );
    if( !info )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    info->wb               = wb;
    info->samples_in_frame = summary->samples_in_frame;
    info->au_number        = 0;
    importer->info = info;
    if( mp4sys_amr_create_damr( summary ) || lsmash_add_entry( importer->summaries, summary ) )
    {
        lsmash_free( importer->info );
        importer->info = NULL;
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    return 0;
}

static uint32_t amr_get_last_delta( importer_t* importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    amr_importer_info_t *info = (amr_importer_info_t *)importer->info;
    if( !info || track_number != 1 )
        return 0;
    return info->samples_in_frame;
}

const static importer_functions amr_importer =
{
    { "amr" },
    1,
    amr_probe,
    amr_get_accessunit,
    amr_get_last_delta,
    amr_cleanup
};

/***************************************************************************
    AC-3 importer
    ETSI TS 102 366 V1.2.1 (2008-08)
***************************************************************************/
#include "a52.h"

#define AC3_SAMPLE_DURATION 1536    /* 256 (samples per audio block) * 6 (audio blocks) */

typedef struct
{
    importer_status status;
    ac3_info_t      info;
} ac3_importer_info_t;

static void remove_ac3_importer_info( ac3_importer_info_t *info )
{
    if( !info )
        return;
    lsmash_bits_adhoc_cleanup( info->info.bits );
    lsmash_free( info );
}

static ac3_importer_info_t *create_ac3_importer_info( void )
{
    ac3_importer_info_t *info = (ac3_importer_info_t *)lsmash_malloc_zero( sizeof(ac3_importer_info_t) );
    if( !info )
        return NULL;
    info->info.bits = lsmash_bits_adhoc_create();
    if( !info->info.bits )
    {
        lsmash_free( info );
        return NULL;
    }
    return info;
}

static void ac3_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        remove_ac3_importer_info( importer->info );
}

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

#if 0
/* FIXME: though this table is for AC-3 in QTFF, we don't support it yet since the structure of CODEC specific info is unknown.
 *        ChannelLayout is given by ac3_channel_layout_table[ acmod ][ lfeon ]. */
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
#endif

static lsmash_audio_summary_t *ac3_create_summary( ac3_info_t *info )
{
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
    if( !summary )
        return NULL;
    lsmash_ac3_specific_parameters_t *param = &info->dac3_param;
    lsmash_codec_specific_t *specific = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_AC_3,
                                                                           LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
    specific->data.unstructured = lsmash_create_ac3_specific_info( &info->dac3_param, &specific->size );
    if( !specific->data.unstructured
     || lsmash_add_entry( &summary->opaque->list, specific ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        return NULL;
    }
    summary->sample_type            = ISOM_CODEC_TYPE_AC_3_AUDIO;
    summary->max_au_length          = AC3_MAX_SYNCFRAME_LENGTH;
    summary->aot                    = MP4A_AUDIO_OBJECT_TYPE_NULL;          /* no effect */
    summary->frequency              = ac3_sample_rate_table[ param->fscod ];
    summary->channels               = ac3_channel_count_table[ param->acmod ] + param->lfeon;
    summary->sample_size            = 16;                                   /* no effect */
    summary->samples_in_frame       = AC3_SAMPLE_DURATION;
    summary->sbr_mode               = MP4A_AAC_SBR_NOT_SPECIFIED;           /* no effect */
    return summary;
}

static int ac3_compare_specific_param( lsmash_ac3_specific_parameters_t *a, lsmash_ac3_specific_parameters_t *b )
{
    return (a->fscod             != b->fscod)
        || (a->bsid              != b->bsid)
        || (a->bsmod             != b->bsmod)
        || (a->acmod             != b->acmod)
        || (a->lfeon             != b->lfeon)
        || ((a->frmsizecod >> 1) != (b->frmsizecod >> 1));
}

static int ac3_importer_get_accessunit( importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_get_entry_data( importer->summaries, track_number );
    if( !summary )
        return -1;
    ac3_importer_info_t *importer_info = (ac3_importer_info_t *)importer->info;
    ac3_info_t *info = &importer_info->info;
    importer_status current_status = importer_info->status;
    if( current_status == IMPORTER_ERROR )
        return -1;
    if( current_status == IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    lsmash_ac3_specific_parameters_t *param = &info->dac3_param;
    uint32_t frame_size = ac3_frame_size_table[ param->frmsizecod >> 1 ][ param->fscod ];
    if( param->fscod == 0x1 && param->frmsizecod & 0x1 )
        frame_size += 2;
    if( buffered_sample->length < frame_size )
        return -1;
    if( current_status == IMPORTER_CHANGE )
    {
        lsmash_codec_specific_t *specific = isom_get_codec_specific( summary->opaque, LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_AC_3 );
        if( specific )
        {
            specific->destruct( specific->data.unstructured );
            specific->data.unstructured = info->next_dac3;
        }
        summary->frequency  = ac3_sample_rate_table[ param->fscod ];
        summary->channels   = ac3_channel_count_table[ param->acmod ] + param->lfeon;
        //summary->layout_tag = ac3_channel_layout_table[ param->acmod ][ param->lfeon ];
    }
    if( frame_size > AC3_MIN_SYNCFRAME_LENGTH )
    {
        uint32_t read_size = frame_size - AC3_MIN_SYNCFRAME_LENGTH;
        if( fread( info->buffer + AC3_MIN_SYNCFRAME_LENGTH, 1, read_size, importer->stream ) != read_size )
            return -1;
    }
    memcpy( buffered_sample->data, info->buffer, frame_size );
    buffered_sample->length = frame_size;
    buffered_sample->dts = info->au_number++ * summary->samples_in_frame;
    buffered_sample->cts = buffered_sample->dts;
    buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    buffered_sample->prop.pre_roll.distance = 1;    /* MDCT */
    if( fread( info->buffer, 1, AC3_MIN_SYNCFRAME_LENGTH, importer->stream ) != AC3_MIN_SYNCFRAME_LENGTH )
        importer_info->status = IMPORTER_EOF;
    else
    {
        /* Parse the next syncframe header. */
        IF_A52_SYNCWORD( info->buffer )
        {
            importer_info->status = IMPORTER_ERROR;
            return current_status;
        }
        lsmash_ac3_specific_parameters_t current_param = info->dac3_param;
        ac3_parse_syncframe_header( info, info->buffer );
        if( ac3_compare_specific_param( &current_param, &info->dac3_param ) )
        {
            uint32_t dummy;
            uint8_t *dac3 = lsmash_create_ac3_specific_info( &info->dac3_param, &dummy );
            if( !dac3 )
            {
                importer_info->status = IMPORTER_ERROR;
                return current_status;
            }
            importer_info->status = IMPORTER_CHANGE;
            info->next_dac3 = dac3;
        }
        else
            importer_info->status = IMPORTER_OK;
    }
    return current_status;
}

static int ac3_importer_probe( importer_t* importer )
{
    uint8_t buf[AC3_MIN_SYNCFRAME_LENGTH];
    if( fread( buf, 1, AC3_MIN_SYNCFRAME_LENGTH, importer->stream ) != AC3_MIN_SYNCFRAME_LENGTH )
        return -1;
    IF_A52_SYNCWORD( buf )
        return -1;
    ac3_importer_info_t *info = create_ac3_importer_info();
    if( !info )
        return -1;
    if( ac3_parse_syncframe_header( &info->info, buf ) )
    {
        remove_ac3_importer_info( info );
        return -1;
    }
    lsmash_audio_summary_t *summary = ac3_create_summary( &info->info );
    if( !summary )
    {
        remove_ac3_importer_info( info );
        return -1;
    }
    info->status = IMPORTER_OK;
    info->info.au_number = 0;
    memcpy( info->info.buffer, buf, AC3_MIN_SYNCFRAME_LENGTH );
    importer->info = info;
    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        remove_ac3_importer_info( importer->info );
        importer->info = NULL;
        return -1;
    }
    return 0;
}

static uint32_t ac3_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    ac3_importer_info_t *info = (ac3_importer_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != IMPORTER_EOF )
        return 0;
    return AC3_SAMPLE_DURATION;
}

const static importer_functions ac3_importer =
{
    { "AC-3" },
    1,
    ac3_importer_probe,
    ac3_importer_get_accessunit,
    ac3_importer_get_last_delta,
    ac3_importer_cleanup
};

/***************************************************************************
    Enhanced AC-3 importer
    ETSI TS 102 366 V1.2.1 (2008-08)
***************************************************************************/
#define EAC3_MIN_SAMPLE_DURATION 256

typedef struct
{
    importer_status status;
    eac3_info_t     info;
} eac3_importer_info_t;

static void remove_eac3_importer_info( eac3_importer_info_t *info )
{
    if( !info )
        return;
    lsmash_destroy_multiple_buffers( info->info.au_buffers );
    lsmash_bits_adhoc_cleanup( info->info.bits );
    lsmash_free( info );
}

static eac3_importer_info_t *create_eac3_importer_info( void )
{
    eac3_importer_info_t *info = (eac3_importer_info_t *)lsmash_malloc_zero( sizeof(eac3_importer_info_t) );
    if( !info )
        return NULL;
    eac3_info_t *eac3_info = &info->info;
    eac3_info->buffer_pos = eac3_info->buffer;
    eac3_info->buffer_end = eac3_info->buffer;
    eac3_info->bits = lsmash_bits_adhoc_create();
    if( !eac3_info->bits )
    {
        lsmash_free( info );
        return NULL;
    }
    eac3_info->au_buffers = lsmash_create_multiple_buffers( 2, EAC3_MAX_SYNCFRAME_LENGTH );
    if( !eac3_info->au_buffers )
    {
        lsmash_bits_adhoc_cleanup( eac3_info->bits );
        lsmash_free( info );
        return NULL;
    }
    eac3_info->au            = lsmash_withdraw_buffer( eac3_info->au_buffers, 1 );
    eac3_info->incomplete_au = lsmash_withdraw_buffer( eac3_info->au_buffers, 2 );
    return info;
}

static void eac3_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        remove_eac3_importer_info( importer->info );
}

static void eac3_update_sample_rate( lsmash_audio_summary_t *summary, lsmash_eac3_specific_parameters_t *dec3_param )
{
    /* Additional independent substreams 1 to 7 must be encoded at the same sample rate as independent substream 0. */
    summary->frequency = ac3_sample_rate_table[ dec3_param->independent_info[0].fscod ];
    if( summary->frequency == 0 )
    {
        static const uint32_t eac3_reduced_sample_rate_table[4] = { 24000, 22050, 16000, 0 };
        summary->frequency = eac3_reduced_sample_rate_table[ dec3_param->independent_info[0].fscod2 ];
    }
}

#if 0
/* FIXME: though this table is for EAC-3 in QTFF, we don't support it yet since the structure of CODEC specific info is unknown. */
static void eac3_update_channel_layout( lsmash_audio_summary_t *summary, lsmash_eac3_substream_info_t *independent_info )
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
        if( independent_info->chan_loc == 0x80 )
            summary->layout_tag = QT_CHANNEL_LAYOUT_EAC_7_0_A;
        else if( independent_info->chan_loc == 0x40 )
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
            { 0x100, QT_CHANNEL_LAYOUT_EAC3_7_1_B },
            { 0x80,  QT_CHANNEL_LAYOUT_EAC3_7_1_A },
            { 0x40,  QT_CHANNEL_LAYOUT_EAC3_6_1_A },
            { 0x20,  QT_CHANNEL_LAYOUT_EAC3_6_1_B },
            { 0x10,  QT_CHANNEL_LAYOUT_EAC3_7_1_C },
            { 0x10,  QT_CHANNEL_LAYOUT_EAC3_7_1_D },
            { 0x4,   QT_CHANNEL_LAYOUT_EAC3_7_1_E },
            { 0x2,   QT_CHANNEL_LAYOUT_EAC3_6_1_C },
            { 0x60,  QT_CHANNEL_LAYOUT_EAC3_7_1_F },
            { 0x42,  QT_CHANNEL_LAYOUT_EAC3_7_1_G },
            { 0x22,  QT_CHANNEL_LAYOUT_EAC3_7_1_H },
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
#endif

static void eac3_update_channel_info( lsmash_audio_summary_t *summary, lsmash_eac3_specific_parameters_t *dec3_param )
{
    summary->channels = 0;
    for( int i = 0; i <= dec3_param->num_ind_sub; i++ )
    {
        int channel_count = 0;
        lsmash_eac3_substream_info_t *independent_info = &dec3_param->independent_info[i];
        channel_count = ac3_channel_count_table[ independent_info->acmod ]  /* L/C/R/Ls/Rs combination */
                      + 2 * !!(independent_info->chan_loc & 0x100)          /* Lc/Rc pair */
                      + 2 * !!(independent_info->chan_loc & 0x80)           /* Lrs/Rrs pair */
                      +     !!(independent_info->chan_loc & 0x40)           /* Cs */
                      +     !!(independent_info->chan_loc & 0x20)           /* Ts */
                      + 2 * !!(independent_info->chan_loc & 0x10)           /* Lsd/Rsd pair */
                      + 2 * !!(independent_info->chan_loc & 0x8)            /* Lw/Rw pair */
                      + 2 * !!(independent_info->chan_loc & 0x4)            /* Lvh/Rvh pair */
                      +     !!(independent_info->chan_loc & 0x2)            /* Cvh */
                      +     !!(independent_info->chan_loc & 0x1)            /* LFE2 */
                      + independent_info->lfeon;                            /* LFE */
        if( channel_count > summary->channels )
        {
            /* Pick the maximum number of channels. */
            summary->channels = channel_count;
            //eac3_update_channel_layout( summary, independent_info );
        }
    }
}

static int eac3_importer_get_next_accessunit_internal( importer_t *importer )
{
    int complete_au = 0;
    eac3_importer_info_t *importer_info = (eac3_importer_info_t *)importer->info;
    eac3_info_t *info = &importer_info->info;
    while( !complete_au )
    {
        /* Read data from the stream if needed. */
        uint32_t remainder_length = info->buffer_end - info->buffer_pos;
        if( !info->no_more_read && remainder_length < EAC3_MAX_SYNCFRAME_LENGTH )
        {
            if( remainder_length )
                memmove( info->buffer, info->buffer_pos, remainder_length );
            uint32_t read_size = fread( info->buffer + remainder_length, 1, EAC3_MAX_SYNCFRAME_LENGTH, importer->stream );
            remainder_length += read_size;
            info->buffer_pos = info->buffer;
            info->buffer_end = info->buffer + remainder_length;
            info->no_more_read = read_size == 0 ? feof( importer->stream ) : 0;
        }
        /* Check the remainder length of the buffer.
         * If there is enough length, then parse the syncframe in it.
         * The length 5 is the required byte length to get frame size. */
        if( remainder_length < 5 )
        {
            /* Reached the end of stream.
             * According to ETSI TS 102 366 V1.2.1 (2008-08),
             * one access unit consists of 6 audio blocks and begins with independent substream 0.
             * The specification doesn't mention the case where a enhanced AC-3 stream ends at non-mod6 audio blocks.
             * At the end of the stream, therefore, we might make an access unit which has less than 6 audio blocks anyway. */
            importer_info->status = IMPORTER_EOF;
            complete_au = !!info->incomplete_au_length;
            if( !complete_au )
                return remainder_length ? -1 : 0;   /* No more access units in the stream. */
            if( !info->dec3_param_initialized )
                eac3_update_specific_param( info );
        }
        else
        {
            /* Parse syncframe. */
            IF_A52_SYNCWORD( info->buffer_pos )
                return -1;
            info->frame_size = 0;
            if( eac3_parse_syncframe( info, info->buffer_pos, LSMASH_MIN( remainder_length, EAC3_MAX_SYNCFRAME_LENGTH ) ) )
                return -1;
            if( remainder_length < info->frame_size )
                return -1;
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
                info->number_of_audio_blocks += eac3_audio_block_table[ info->numblkscod ];
                info->number_of_independent_substreams = 0;
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
            if( importer_info->status == IMPORTER_EOF )
                break;
        }
        /* Increase buffer size to store AU if short. */
        if( info->incomplete_au_length + info->frame_size > info->au_buffers->buffer_size )
        {
            lsmash_multiple_buffers_t *temp = lsmash_resize_multiple_buffers( info->au_buffers, info->au_buffers->buffer_size + EAC3_MAX_SYNCFRAME_LENGTH );
            if( !temp )
                return -1;
            info->au_buffers    = temp;
            info->au            = lsmash_withdraw_buffer( info->au_buffers, 1 );
            info->incomplete_au = lsmash_withdraw_buffer( info->au_buffers, 2 );
        }
        /* Append syncframe data. */
        memcpy( info->incomplete_au + info->incomplete_au_length, info->buffer_pos, info->frame_size );
        info->incomplete_au_length += info->frame_size;
        info->buffer_pos           += info->frame_size;
        ++ info->syncframe_count;
    }
    return info->bits->bs->error ? -1 : 0;
}

static int eac3_importer_get_accessunit( importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_get_entry_data( importer->summaries, track_number );
    if( !summary )
        return -1;
    eac3_importer_info_t *importer_info = (eac3_importer_info_t *)importer->info;
    eac3_info_t *info = &importer_info->info;
    importer_status current_status = importer_info->status;
    if( current_status == IMPORTER_ERROR || buffered_sample->length < info->au_length )
        return -1;
    if( current_status == IMPORTER_EOF && info->au_length == 0 )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( current_status == IMPORTER_CHANGE )
    {
        lsmash_codec_specific_t *specific = isom_get_codec_specific( summary->opaque, LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_EC_3 );
        if( specific )
        {
            specific->destruct( specific->data.unstructured );
            specific->data.unstructured = info->next_dec3;
            specific->size              = info->next_dec3_length;
        }
        summary->max_au_length = info->syncframe_count_in_au * EAC3_MAX_SYNCFRAME_LENGTH;
        eac3_update_sample_rate( summary, &info->dec3_param );
        eac3_update_channel_info( summary, &info->dec3_param );
    }
    memcpy( buffered_sample->data, info->au, info->au_length );
    buffered_sample->length = info->au_length;
    buffered_sample->dts = info->au_number++ * summary->samples_in_frame;
    buffered_sample->cts = buffered_sample->dts;
    buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    buffered_sample->prop.pre_roll.distance = 1;    /* MDCT */
    if( importer_info->status == IMPORTER_EOF )
    {
        info->au_length = 0;
        return 0;
    }
    uint32_t old_syncframe_count_in_au = info->syncframe_count_in_au;
    if( eac3_importer_get_next_accessunit_internal( importer ) )
    {
        importer_info->status = IMPORTER_ERROR;
        return current_status;
    }
    if( info->syncframe_count_in_au )
    {
        /* Check sample description change. */
        uint32_t new_length;
        uint8_t *dec3 = lsmash_create_eac3_specific_info( &info->dec3_param, &new_length );
        if( !dec3 )
        {
            importer_info->status = IMPORTER_ERROR;
            return current_status;
        }
        lsmash_codec_specific_t *specific = isom_get_codec_specific( summary->opaque, LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_EC_3 );
        if( (info->syncframe_count_in_au > old_syncframe_count_in_au)
         || (specific && (new_length != specific->size || memcmp( dec3, specific->data.unstructured, specific->size ))) )
        {
            importer_info->status = IMPORTER_CHANGE;
            info->next_dec3 = dec3;
            info->next_dec3_length = new_length;
        }
        else
        {
            if( importer_info->status != IMPORTER_EOF )
                importer_info->status = IMPORTER_OK;
            lsmash_free( dec3 );
        }
    }
    return current_status;
}

static lsmash_audio_summary_t *eac3_create_summary( eac3_info_t *info )
{
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
    if( !summary )
        return NULL;
    lsmash_codec_specific_t *specific = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_EC_3,
                                                                           LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
    specific->data.unstructured = lsmash_create_eac3_specific_info( &info->dec3_param, &specific->size );
    if( !specific->data.unstructured
     || lsmash_add_entry( &summary->opaque->list, specific ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        return NULL;
    }
    summary->sample_type            = ISOM_CODEC_TYPE_EC_3_AUDIO;
    summary->max_au_length          = info->syncframe_count_in_au * EAC3_MAX_SYNCFRAME_LENGTH;
    summary->aot                    = MP4A_AUDIO_OBJECT_TYPE_NULL;      /* no effect */
    summary->sample_size            = 16;                               /* no effect */
    summary->samples_in_frame       = EAC3_MIN_SAMPLE_DURATION * 6;     /* 256 (samples per audio block) * 6 (audio blocks) */
    summary->sbr_mode               = MP4A_AAC_SBR_NOT_SPECIFIED;       /* no effect */
    eac3_update_sample_rate( summary, &info->dec3_param );
    eac3_update_channel_info( summary, &info->dec3_param );
    return summary;
}

static int eac3_importer_probe( importer_t *importer )
{
    eac3_importer_info_t *info = create_eac3_importer_info();
    if( !info )
        return -1;
    importer->info = info;
    if( eac3_importer_get_next_accessunit_internal( importer ) )
    {
        remove_eac3_importer_info( importer->info );
        importer->info = NULL;
        return -1;
    }
    lsmash_audio_summary_t *summary = eac3_create_summary( &info->info );
    if( !summary )
    {
        remove_eac3_importer_info( importer->info );
        importer->info = NULL;
        return -1;
    }
    if( info->status != IMPORTER_EOF )
        info->status = IMPORTER_OK;
    info->info.au_number = 0;
    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        remove_eac3_importer_info( importer->info );
        importer->info = NULL;
        return -1;
    }
    return 0;
}

static uint32_t eac3_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    eac3_importer_info_t *info = (eac3_importer_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != IMPORTER_EOF || info->info.au_length )
        return 0;
    return EAC3_MIN_SAMPLE_DURATION * info->info.number_of_audio_blocks;
}

const static importer_functions eac3_importer =
{
    { "Enhanced AC-3" },
    1,
    eac3_importer_probe,
    eac3_importer_get_accessunit,
    eac3_importer_get_last_delta,
    eac3_importer_cleanup
};

/***************************************************************************
    MPEG-4 ALS importer
    ISO/IEC 14496-3 2009 Fourth edition
***************************************************************************/
#define ALSSC_TWELVE_LENGTH 22

typedef struct
{
    uint32_t  size;
    uint32_t  samp_freq;
    uint32_t  samples;
    uint32_t  channels;
    uint16_t  frame_length;
    uint8_t   resolution;
    uint8_t   random_access;
    uint8_t   ra_flag;
    uint32_t  access_unit_size;
    uint32_t  number_of_ra_units;
    uint32_t *ra_unit_size;
    uint8_t  *sc_data;
    size_t    alloc;
} als_specific_config_t;

typedef struct
{
    importer_status          status;
    lsmash_stream_buffers_t *sb;
    als_specific_config_t    alssc;
    uint32_t                 samples_in_frame;
    uint32_t                 au_number;
} mp4sys_als_info_t;

static void mp4sys_remove_als_info( mp4sys_als_info_t *info )
{
    if( info->alssc.ra_unit_size )
        lsmash_free( info->alssc.ra_unit_size );
    if( info->alssc.sc_data )
        lsmash_free( info->alssc.sc_data );
    lsmash_stream_buffers_cleanup( info->sb );
    lsmash_free( info );
}

static void mp4sys_als_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        mp4sys_remove_als_info( importer->info );
}

static mp4sys_als_info_t *create_mp4sys_als_importer_info( importer_t *importer )
{
    mp4sys_als_info_t *info = lsmash_malloc_zero( sizeof(mp4sys_als_info_t) );
    if( !info )
        return NULL;
    info->sb = &importer->sb;
    lsmash_stream_buffers_t *sb = info->sb;
    lsmash_stream_buffers_setup( sb, LSMASH_STREAM_BUFFERS_TYPE_FILE, importer->stream );
    sb->bank = lsmash_create_multiple_buffers( 1, 1 << 16 );
    if( !sb->bank )
    {
        lsmash_free( info );
        return NULL;
    }
    sb->start = lsmash_withdraw_buffer( sb->bank, 1 );
    sb->pos   = sb->start;
    sb->end   = sb->start;
    return info;
}

static int als_prepare_buffer_read( lsmash_stream_buffers_t *sb, size_t buffer_size )
{
    if( buffer_size <= sb->bank->buffer_size )
        return 0;
    uintptr_t buffer_pos_offset   = sb->pos - sb->start;
    uintptr_t buffer_valid_length = sb->end - sb->start;
    lsmash_multiple_buffers_t *bank = lsmash_resize_multiple_buffers( sb->bank, buffer_size );
    if( !bank )
        return -1;
    sb->bank  = bank;
    sb->start = lsmash_withdraw_buffer( bank, 1 );
    sb->pos   = sb->start + buffer_pos_offset;
    sb->end   = sb->start + buffer_valid_length;
    return 0;
}

static void als_copy_from_buffer( als_specific_config_t *alssc, lsmash_stream_buffers_t *sb, size_t size )
{
    if( alssc->alloc < size )
    {
        size_t   alloc = alssc->alloc ? (alssc->alloc << 1) : (1 << 16);
        uint8_t *temp  = lsmash_realloc( alssc->sc_data, alloc );
        if( !temp )
            return;
        alssc->sc_data = temp;
        alssc->alloc   = alloc;
    }
    lsmash_stream_buffers_memcpy( alssc->sc_data + alssc->size, sb, size );
    alssc->size += size;
}

#define CHECK_UPDATE( SIZE ) \
    if( sb->update( sb, (SIZE) - 1 ) < (SIZE) ) return -1

static int als_parse_specific_config( lsmash_stream_buffers_t *sb, als_specific_config_t *alssc )
{
    /* Check ALS identifier( = 0x414C5300). */
    lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
    lsmash_stream_buffers_read( sb, 0 );
    uint8_t *buf = sb->pos;
    if( sb->end < sb->start + ALSSC_TWELVE_LENGTH
     || buf[0] != 0x41 || buf[1] != 0x4C || buf[2] != 0x53 || buf[3] != 0x00 )
        return -1;
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
    als_copy_from_buffer( alssc, sb, ALSSC_TWELVE_LENGTH );
    if( chan_config )
    {
        /* chan_config_info */
        CHECK_UPDATE( 2 );
        als_copy_from_buffer( alssc, sb, 2 );
    }
    if( chan_sort )
    {
        uint32_t ChBits;    /* ceil[log2(channels+1)] = 1...16 */
        for( ChBits = 1; alssc->channels >> ChBits; ChBits++ );
        uint32_t chan_pos_length = (alssc->channels + 1) * ChBits;
        chan_pos_length = chan_pos_length / 8 + !!(chan_pos_length % 8);
        if( als_prepare_buffer_read( sb, chan_pos_length ) < 0 )
            return -1;
        CHECK_UPDATE( chan_pos_length );
        als_copy_from_buffer( alssc, sb, chan_pos_length );
    }
    /* header_size and trailer_size */
    CHECK_UPDATE( 8 );
    uint32_t header_size  = (sb->pos[0] << 24) | (sb->pos[1] << 16) | (sb->pos[2] << 8) | sb->pos[3];
    uint32_t trailer_size = (sb->pos[4] << 24) | (sb->pos[5] << 16) | (sb->pos[6] << 8) | sb->pos[7];
    als_copy_from_buffer( alssc, sb, 8 );
    /* orig_header, orig_trailer and crc. */
    uint32_t read_size = header_size * (header_size != 0xffffffff) + trailer_size * (trailer_size != 0xffffffff) + 4 * crc_enabled;
    if( als_prepare_buffer_read( sb, read_size ) < 0 )
        return -1;
    CHECK_UPDATE( read_size );
    als_copy_from_buffer( alssc, sb, read_size );
    /* Random access unit */
    uint32_t number_of_frames = (alssc->samples / (alssc->frame_length + 1)) + !!(alssc->samples % (alssc->frame_length + 1));
    if( alssc->random_access != 0 )
        alssc->number_of_ra_units = number_of_frames / alssc->random_access + !!(number_of_frames % alssc->random_access);
    else
        alssc->number_of_ra_units = 0;
    if( alssc->ra_flag == 2 && alssc->random_access != 0 )
    {
        /* We don't copy all ra_unit_size. */
        read_size = alssc->number_of_ra_units * 4;
        if( als_prepare_buffer_read( sb, read_size ) < 0 )
            return -1;
        CHECK_UPDATE( read_size );
        alssc->ra_unit_size = lsmash_malloc( read_size );
        if( !alssc->ra_unit_size )
            return -1;
        uint32_t max_ra_unit_size = 0;
        for( uint32_t i = 0; i < alssc->number_of_ra_units; i++ )
        {
            if( sb->pos + 4 > sb->end )
                return -1;
            alssc->ra_unit_size[i] = (sb->pos[0] << 24) | (sb->pos[1] << 16) | (sb->pos[2] << 8) | sb->pos[3];
            lsmash_stream_buffers_seek( sb, 4, SEEK_CUR );
            max_ra_unit_size = LSMASH_MAX( max_ra_unit_size, alssc->ra_unit_size[i] );
        }
        if( max_ra_unit_size > sb->bank->buffer_size
         && als_prepare_buffer_read( sb, max_ra_unit_size ) < 0 )
            return -1;
    }
    else
        alssc->ra_unit_size = NULL;
    /* auxiliary data */
    if( aux_data_enabled )
    {
        CHECK_UPDATE( 4 );
        uint32_t aux_size = (sb->pos[0] << 24) | (sb->pos[1] << 16) | (sb->pos[2] << 8) | sb->pos[3];
        als_copy_from_buffer( alssc, sb, 4 );
        if( aux_size && aux_size != 0xffffffff )
        {
            if( als_prepare_buffer_read( sb, aux_size ) < 0 )
                return -1;
            CHECK_UPDATE( aux_size );
            als_copy_from_buffer( alssc, sb, aux_size );
        }
    }
    return 0;
}

static int mp4sys_als_importer_get_accessunit( importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_get_entry_data( importer->summaries, track_number );
    if( !summary )
        return -1;
    mp4sys_als_info_t *info = (mp4sys_als_info_t *)importer->info;
    importer_status current_status = info->status;
    if( current_status == IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    lsmash_stream_buffers_t *sb    = info->sb;
    als_specific_config_t   *alssc = &info->alssc;
    if( alssc->number_of_ra_units == 0 )
    {
        lsmash_stream_buffers_memcpy( buffered_sample->data, sb, alssc->access_unit_size );
        buffered_sample->length        = alssc->access_unit_size;
        buffered_sample->cts           = 0;
        buffered_sample->dts           = 0;
        buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
        info->status = IMPORTER_EOF;
        return 0;
    }
    uint32_t au_length;
    if( alssc->ra_flag == 2 )
        au_length = alssc->ra_unit_size[info->au_number];
    else /* if( alssc->ra_flag == 1 ) */
    {
        CHECK_UPDATE( 4 );
        /* We remove ra_unit_size. */
        au_length = (sb->pos[0] << 24) | (sb->pos[1] << 16) | (sb->pos[2] << 8) | sb->pos[3];
        lsmash_stream_buffers_seek( sb, 4, SEEK_CUR );
    }
    if( buffered_sample->length < au_length
     || als_prepare_buffer_read( sb, au_length ) < 0 )
        return -1;
    CHECK_UPDATE( au_length );
    lsmash_stream_buffers_memcpy( buffered_sample->data, sb, au_length );
    buffered_sample->length        = au_length;
    buffered_sample->dts           = info->au_number++ * info->samples_in_frame;
    buffered_sample->cts           = buffered_sample->dts;
    buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    if( info->au_number == alssc->number_of_ra_units )
        info->status = IMPORTER_EOF;
    return 0;
}

#undef CHECK_UPDATE

static lsmash_audio_summary_t *als_create_summary( lsmash_stream_buffers_t *sb, als_specific_config_t *alssc )
{
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
    if( !summary )
        return NULL;
    summary->sample_type            = ISOM_CODEC_TYPE_MP4A_AUDIO;
    summary->aot                    = MP4A_AUDIO_OBJECT_TYPE_ALS;
    summary->frequency              = alssc->samp_freq;
    summary->channels               = alssc->channels + 1;
    summary->sample_size            = (alssc->resolution + 1) * 8;
    summary->sbr_mode               = MP4A_AAC_SBR_NOT_SPECIFIED; /* no effect */
    if( alssc->random_access != 0 )
    {
        summary->samples_in_frame = (alssc->frame_length + 1) * alssc->random_access;
        summary->max_au_length    = summary->channels * (summary->sample_size / 8) * summary->samples_in_frame;
    }
    else
    {
        /* Read the remainder of overall stream as an access unit. */
        alssc->access_unit_size = lsmash_stream_buffers_get_remainder( sb );
        for( uint32_t buffer_size = sb->bank->buffer_size; !sb->no_more_read; buffer_size <<= 1 )
        {
            if( als_prepare_buffer_read( sb, buffer_size ) < 0 )
                return NULL;
            alssc->access_unit_size = sb->update( sb, buffer_size - 1 );
        }
        summary->max_au_length    = alssc->access_unit_size;
        summary->samples_in_frame = 0;      /* hack for mp4sys_als_importer_get_last_delta() */
    }
    uint32_t data_length;
    uint8_t *data = mp4a_export_AudioSpecificConfig( MP4A_AUDIO_OBJECT_TYPE_ALS,
                                                     summary->frequency, summary->channels, summary->sbr_mode,
                                                     alssc->sc_data, alssc->size, &data_length );
    if( !data )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
    lsmash_codec_specific_t *specific = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_MP4SYS_DECODER_CONFIG,
                                                                           LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
    if( !specific )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_free( data );
        return NULL;
    }
    lsmash_mp4sys_decoder_parameters_t *param = (lsmash_mp4sys_decoder_parameters_t *)specific->data.structured;
    param->objectTypeIndication = MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3;
    param->streamType           = MP4SYS_STREAM_TYPE_AudioStream;
    if( lsmash_set_mp4sys_decoder_specific_info( param, data, data_length ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        lsmash_free( data );
        return NULL;
    }
    lsmash_free( data );
    if( lsmash_add_entry( &summary->opaque->list, specific ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        return NULL;
    }
    return summary;
}

static int mp4sys_als_importer_probe( importer_t *importer )
{
    mp4sys_als_info_t *info = create_mp4sys_als_importer_info( importer );
    if( !info )
        return -1;
    /* Parse ALS specific configuration. */
    if( als_parse_specific_config( info->sb, &info->alssc ) )
    {
        mp4sys_remove_als_info( info );
        return -1;
    }
    lsmash_audio_summary_t *summary = als_create_summary( info->sb, &info->alssc );
    if( !summary )
    {
        mp4sys_remove_als_info( info );
        return -1;
    }
    /* importer status */
    info->status           = IMPORTER_OK;
    info->samples_in_frame = summary->samples_in_frame;
    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        mp4sys_remove_als_info( info );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return -1;
    }
    importer->info = info;
    return 0;
}

static uint32_t mp4sys_als_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    mp4sys_als_info_t *info = (mp4sys_als_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != IMPORTER_EOF )
        return 0;
    als_specific_config_t *alssc = &info->alssc;
    /* If alssc->number_of_ra_units == 0, then the last sample duration is just alssc->samples
     * since als_create_summary sets 0 to summary->samples_in_frame i.e. info->samples_in_frame. */
    return alssc->samples - (alssc->number_of_ra_units - 1) * info->samples_in_frame;
}

const static importer_functions mp4sys_als_importer =
{
    { "MPEG-4 ALS" },
    1,
    mp4sys_als_importer_probe,
    mp4sys_als_importer_get_accessunit,
    mp4sys_als_importer_get_last_delta,
    mp4sys_als_importer_cleanup
};

/***************************************************************************
    DTS importer
    ETSI TS 102 114 V1.2.1 (2002-12)
    ETSI TS 102 114 V1.3.1 (2011-08)
    ETSI TS 102 114 V1.4.1 (2012-09)
***************************************************************************/
#include "dts.h"

typedef struct
{
    importer_status status;
    dts_info_t      info;
} dts_importer_info_t;

static void remove_dts_importer_info( dts_importer_info_t *info )
{
    if( !info )
        return;
    lsmash_destroy_multiple_buffers( info->info.au_buffers );
    lsmash_bits_adhoc_cleanup( info->info.bits );
    lsmash_free( info );
}

static dts_importer_info_t *create_dts_importer_info( void )
{
    dts_importer_info_t *info = (dts_importer_info_t *)lsmash_malloc_zero( sizeof(dts_importer_info_t) );
    if( !info )
        return NULL;
    dts_info_t *dts_info = &info->info;
    dts_info->buffer_pos = dts_info->buffer;
    dts_info->buffer_end = dts_info->buffer;
    dts_info->bits = lsmash_bits_adhoc_create();
    if( !dts_info->bits )
    {
        lsmash_free( info );
        return NULL;
    }
    dts_info->au_buffers = lsmash_create_multiple_buffers( 2, DTS_MAX_EXSS_SIZE );
    if( !dts_info->au_buffers )
    {
        lsmash_bits_adhoc_cleanup( dts_info->bits );
        lsmash_free( info );
        return NULL;
    }
    dts_info->au            = lsmash_withdraw_buffer( dts_info->au_buffers, 1 );
    dts_info->incomplete_au = lsmash_withdraw_buffer( dts_info->au_buffers, 2 );
    dts_setup_parser( dts_info );
    return info;
}

static void dts_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        remove_dts_importer_info( importer->info );
}

static int dts_importer_get_next_accessunit_internal( importer_t *importer )
{
    int complete_au = 0;
    dts_importer_info_t *importer_info = (dts_importer_info_t *)importer->info;
    dts_info_t *info = &importer_info->info;
    while( !complete_au )
    {
        /* Read data from the stream if needed. */
        uint32_t remainder_length = info->buffer_end - info->buffer_pos;
        if( !info->no_more_read && remainder_length < DTS_MAX_EXSS_SIZE )
        {
            if( remainder_length )
                memmove( info->buffer, info->buffer_pos, remainder_length );
            uint32_t read_size = fread( info->buffer + remainder_length, 1, DTS_MAX_EXSS_SIZE, importer->stream );
            remainder_length += read_size;
            info->buffer_pos   = info->buffer;
            info->buffer_end   = info->buffer + remainder_length;
            info->no_more_read = read_size == 0 ? feof( importer->stream ) : 0;
        }
        /* Check the remainder length of the buffer.
         * If there is enough length, then parse the frame in it.
         * The length 10 is the required byte length to get frame size. */
        if( remainder_length < 10 )
        {
            /* Reached the end of stream. */
            importer_info->status = IMPORTER_EOF;
            complete_au = !!info->incomplete_au_length;
            if( !complete_au )
                return remainder_length ? -1 : 0;   /* No more access units in the stream. */
            if( !info->ddts_param_initialized )
                dts_update_specific_param( info );
        }
        else
        {
            /* Parse substream frame. */
            dts_substream_type prev_substream_type = info->substream_type;
            info->substream_type = dts_get_substream_type( info );
            int (*dts_parse_frame)( dts_info_t *, uint8_t *, uint32_t ) = NULL;
            switch( info->substream_type )
            {
                /* Decide substream frame parser and check if this frame and the previous frame belong to the same AU. */
                case DTS_SUBSTREAM_TYPE_CORE :
                    if( prev_substream_type != DTS_SUBSTREAM_TYPE_NONE )
                        complete_au = 1;
                    dts_parse_frame = dts_parse_core_substream;
                    break;
                case DTS_SUBSTREAM_TYPE_EXTENSION :
                {
                    uint8_t prev_exss_index = info->exss_index;
                    if( dts_get_exss_index( info, &info->exss_index ) )
                        return -1;
                    if( prev_substream_type == DTS_SUBSTREAM_TYPE_EXTENSION
                     && info->exss_index <= prev_exss_index )
                        complete_au = 1;
                    dts_parse_frame = dts_parse_extension_substream;
                    break;
                }
                default :
                    return -1;
            }
            if( !info->ddts_param_initialized && complete_au )
                dts_update_specific_param( info );
            info->frame_size = 0;
            if( dts_parse_frame( info, info->buffer_pos, LSMASH_MIN( remainder_length, DTS_MAX_EXSS_SIZE ) ) )
                return -1;  /* Failed to parse. */
        }
        if( complete_au )
        {
            memcpy( info->au, info->incomplete_au, info->incomplete_au_length );
            info->au_length            = info->incomplete_au_length;
            info->incomplete_au_length = 0;
            info->exss_count           = (info->substream_type == DTS_SUBSTREAM_TYPE_EXTENSION);
            if( importer_info->status == IMPORTER_EOF )
                break;
        }
        /* Increase buffer size to store AU if short. */
        if( info->incomplete_au_length + info->frame_size > info->au_buffers->buffer_size )
        {
            lsmash_multiple_buffers_t *temp = lsmash_resize_multiple_buffers( info->au_buffers, info->au_buffers->buffer_size + DTS_MAX_EXSS_SIZE );
            if( !temp )
                return -1;
            info->au_buffers    = temp;
            info->au            = lsmash_withdraw_buffer( info->au_buffers, 1 );
            info->incomplete_au = lsmash_withdraw_buffer( info->au_buffers, 2 );
        }
        /* Append frame data. */
        memcpy( info->incomplete_au + info->incomplete_au_length, info->buffer_pos, info->frame_size );
        info->incomplete_au_length += info->frame_size;
        info->buffer_pos           += info->frame_size;
    }
    return info->bits->bs->error ? -1 : 0;
}

static int dts_importer_get_accessunit( importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_get_entry_data( importer->summaries, track_number );
    if( !summary )
        return -1;
    dts_importer_info_t *importer_info = (dts_importer_info_t *)importer->info;
    dts_info_t *info = &importer_info->info;
    importer_status current_status = importer_info->status;
    if( current_status == IMPORTER_ERROR || buffered_sample->length < info->au_length )
        return -1;
    if( current_status == IMPORTER_EOF && info->au_length == 0 )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( current_status == IMPORTER_CHANGE )
        summary->max_au_length = 0;
    memcpy( buffered_sample->data, info->au, info->au_length );
    buffered_sample->length                 = info->au_length;
    buffered_sample->dts                    = info->au_number++ * summary->samples_in_frame;
    buffered_sample->cts                    = buffered_sample->dts;
    buffered_sample->prop.ra_flags          = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    buffered_sample->prop.pre_roll.distance = !!(info->flags & DTS_EXT_SUBSTREAM_LBR_FLAG);     /* MDCT */
    if( importer_info->status == IMPORTER_EOF )
    {
        info->au_length = 0;
        return 0;
    }
    if( dts_importer_get_next_accessunit_internal( importer ) )
        importer_info->status = IMPORTER_ERROR;
    return current_status;
}

static lsmash_audio_summary_t *dts_create_summary( dts_info_t *info )
{
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
    if( !summary )
        return NULL;
    lsmash_dts_specific_parameters_t *param = &info->ddts_param;
    lsmash_codec_specific_t *specific = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_DTS,
                                                                           LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
    specific->data.unstructured = lsmash_create_dts_specific_info( param, &specific->size );
    if( !specific->data.unstructured
     || lsmash_add_entry( &summary->opaque->list, specific ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        return NULL;
    }
    summary->aot         = MP4A_AUDIO_OBJECT_TYPE_NULL;     /* no effect */
    summary->sbr_mode    = MP4A_AAC_SBR_NOT_SPECIFIED;      /* no effect */
    summary->sample_type = lsmash_dts_get_codingname( param );
    switch( param->DTSSamplingFrequency )
    {
        case 12000 :    /* Invalid? (No reference in the spec) */
        case 24000 :
        case 48000 :
        case 96000 :
        case 192000 :
        case 384000 :   /* Invalid? (No reference in the spec) */
            summary->frequency = 48000;
            break;
        case 22050 :
        case 44100 :
        case 88200 :
        case 176400 :
        case 352800 :   /* Invalid? (No reference in the spec) */
            summary->frequency = 44100;
            break;
        case 8000 :     /* Invalid? (No reference in the spec) */
        case 16000 :
        case 32000 :
        case 64000 :
        case 128000 :
            summary->frequency = 32000;
            break;
        default :
            summary->frequency = 0;
            break;
    }
    summary->samples_in_frame = (summary->frequency * info->frame_duration) / param->DTSSamplingFrequency;
    summary->max_au_length    = DTS_MAX_CORE_SIZE + DTS_MAX_NUM_EXSS * DTS_MAX_EXSS_SIZE;
    summary->sample_size      = param->pcmSampleDepth;
    summary->channels         = dts_get_max_channel_count( info );
    return summary;
}

static int dts_importer_probe( importer_t *importer )
{
    dts_importer_info_t *info = create_dts_importer_info();
    if( !info )
        return -1;
    importer->info = info;
    if( dts_importer_get_next_accessunit_internal( importer ) )
    {
        remove_dts_importer_info( importer->info );
        importer->info = NULL;
        return -1;
    }
    lsmash_audio_summary_t *summary = dts_create_summary( &info->info );
    if( !summary )
    {
        remove_dts_importer_info( importer->info );
        importer->info = NULL;
        return -1;
    }
    if( info->status != IMPORTER_EOF )
        info->status = IMPORTER_OK;
    info->info.au_number = 0;
    if( lsmash_add_entry( importer->summaries, summary ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        remove_dts_importer_info( importer->info );
        importer->info = NULL;
        return -1;
    }
    return 0;
}

static uint32_t dts_importer_get_last_delta( importer_t* importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    dts_importer_info_t *info = (dts_importer_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != IMPORTER_EOF || info->info.au_length )
        return 0;
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_get_entry_data( importer->summaries, track_number );
    if( !summary )
        return 0;
    return (summary->frequency * info->info.frame_duration) / info->info.ddts_param.DTSSamplingFrequency;
}

const static importer_functions dts_importer =
{
    { "DTS Coherent Acoustics" },
    1,
    dts_importer_probe,
    dts_importer_get_accessunit,
    dts_importer_get_last_delta,
    dts_importer_cleanup
};

/***************************************************************************
    H.264 importer
    ITU-T Recommendation H.264 (04/13)
    ISO/IEC 14496-15:2010
***************************************************************************/
#include "h264.h"
#include "nalu.h"

typedef struct
{
    importer_status        status;
    h264_info_t            info;
    lsmash_entry_list_t    avcC_list[1];    /* stored as lsmash_codec_specific_t */
    lsmash_media_ts_list_t ts_list;
    uint32_t max_au_length;
    uint32_t num_undecodable;
    uint32_t avcC_number;
    uint32_t last_delta;
    uint64_t last_intra_cts;
    uint8_t  composition_reordering_present;
    uint8_t  field_pic_present;
} h264_importer_info_t;

typedef struct
{
    int64_t  poc;
    uint32_t delta;
    uint16_t poc_delta;
    uint16_t reset;
} nal_pic_timing_t;

static void remove_h264_importer_info( h264_importer_info_t *info )
{
    if( !info )
        return;
    lsmash_remove_entries( info->avcC_list, lsmash_destroy_codec_specific_data );
    h264_cleanup_parser( &info->info );
    if( info->ts_list.timestamp )
        lsmash_free( info->ts_list.timestamp );
    lsmash_free( info );
}

static void h264_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        remove_h264_importer_info( importer->info );
}

static h264_importer_info_t *create_h264_importer_info( importer_t *importer )
{
    h264_importer_info_t *info = lsmash_malloc_zero( sizeof(h264_importer_info_t) );
    if( !info )
        return NULL;
    if( h264_setup_parser( &info->info, &importer->sb, 0, LSMASH_STREAM_BUFFERS_TYPE_FILE, importer->stream ) )
    {
        remove_h264_importer_info( info );
        return NULL;
    }
    lsmash_init_entry_list( info->avcC_list );
    return info;
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
        uint8_t *dst_nalu = picture->incomplete_au + picture->incomplete_au_length + H264_DEFAULT_NALU_LENGTH_SIZE;
        for( int i = H264_DEFAULT_NALU_LENGTH_SIZE; i; i-- )
            *(dst_nalu - i) = (nalu_length >> ((i - 1) * 8)) & 0xff;
        memcpy( dst_nalu, src_nalu, nalu_length );
    }
    /* Note: picture->incomplete_au_length shall be 0 immediately after AU has completed.
     * Therefore, possible_au_length in h264_get_access_unit_internal() can't be used here
     * to avoid increasing AU length monotonously through the entire stream. */
    picture->incomplete_au_length += H264_DEFAULT_NALU_LENGTH_SIZE + nalu_length;
}

static inline void h264_get_au_internal_end( h264_importer_info_t *info, h264_picture_info_t *picture, h264_nalu_header_t *nalu_header, int no_more_buf )
{
    if( info->info.buffer.sb->no_more_read && no_more_buf && (picture->incomplete_au_length == 0) )
        info->status = IMPORTER_EOF;
    else if( info->status != IMPORTER_CHANGE )
        info->status = IMPORTER_OK;
    info->info.nalu_header = *nalu_header;
}

static int h264_get_au_internal_succeeded( h264_importer_info_t *info, h264_picture_info_t *picture, h264_nalu_header_t *nalu_header, int no_more_buf )
{
    h264_get_au_internal_end( info, picture, nalu_header, no_more_buf );
    picture->au_number += 1;
    return 0;
}

static int h264_get_au_internal_failed( h264_importer_info_t *info, h264_picture_info_t *picture, h264_nalu_header_t *nalu_header, int no_more_buf, int complete_au )
{
    h264_get_au_internal_end( info, picture, nalu_header, no_more_buf );
    if( complete_au )
        picture->au_number += 1;
    return -1;
}

static lsmash_video_summary_t *h264_create_summary
(
    lsmash_h264_specific_parameters_t *param,
    h264_sps_t                        *sps,
    uint32_t                           max_au_length
)
{
    lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_VIDEO );
    if( !summary )
        return NULL;
    /* Update summary here.
     * max_au_length is set at the last of mp4sys_h264_probe function. */
    lsmash_codec_specific_t *specific = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_H264,
                                                                           LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
    specific->data.unstructured = lsmash_create_h264_specific_info( param, &specific->size );
    if( !specific->data.unstructured
     || lsmash_add_entry( &summary->opaque->list, specific ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        return NULL;
    }
    summary->sample_type            = ISOM_CODEC_TYPE_AVC1_VIDEO;
    summary->max_au_length          = max_au_length;
    summary->timescale              = sps->vui.time_scale;
    summary->timebase               = sps->vui.num_units_in_tick;
    summary->vfr                    = !sps->vui.fixed_frame_rate_flag;
    summary->sample_per_field       = 0;
    summary->width                  = sps->cropped_width;
    summary->height                 = sps->cropped_height;
    summary->par_h                  = sps->vui.sar_width;
    summary->par_v                  = sps->vui.sar_height;
    summary->color.primaries_index  = sps->vui.colour_primaries;
    summary->color.transfer_index   = sps->vui.transfer_characteristics;
    summary->color.matrix_index     = sps->vui.matrix_coefficients;
    summary->color.full_range       = sps->vui.video_full_range_flag;
    return summary;
}

static int h264_store_codec_specific
(
    h264_importer_info_t              *info,
    lsmash_h264_specific_parameters_t *avcC_param
)
{
    lsmash_codec_specific_t *src_cs = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_H264,
                                                                         LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
    if( !src_cs )
        return -1;
    lsmash_h264_specific_parameters_t *src_param = (lsmash_h264_specific_parameters_t *)src_cs->data.structured;
    *src_param = *avcC_param;
    lsmash_codec_specific_t *dst_cs = lsmash_convert_codec_specific_format( src_cs, LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
    src_param->parameter_sets = NULL;   /* Avoid freeing parameter sets within avcC_param. */
    lsmash_destroy_codec_specific_data( src_cs );
    if( !dst_cs || lsmash_add_entry( info->avcC_list, dst_cs ) )
    {
        lsmash_destroy_codec_specific_data( dst_cs );
        return -1;
    }
    return 0;
}

/* If probe equals 0, don't get the actual data (EBPS) of an access unit and only parse NALU.
 * Currently, you can get AU of AVC video elemental stream only, not AVC parameter set elemental stream defined in 14496-15. */
static int h264_get_access_unit_internal
(
    h264_importer_info_t *importer_info,
    int                   probe
)
{
    h264_info_t             *info     = &importer_info->info;
    h264_slice_info_t       *slice    = &info->slice;
    h264_picture_info_t     *picture  = &info->picture;
    h264_stream_buffer_t    *hb       = &info->buffer;
    lsmash_stream_buffers_t *sb       = hb->sb;
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
    picture->broken_link_flag   = 0;
    while( 1 )
    {
        lsmash_stream_buffers_update( sb, 2 );
        no_more_buf = (lsmash_stream_buffers_get_remainder( sb ) == 0);
        int no_more = lsmash_stream_buffers_is_eos( sb ) && no_more_buf;
        if( !nalu_check_next_short_start_code( sb->pos, sb->end ) && !no_more )
        {
            if( lsmash_stream_buffers_get_byte( sb ) )
                consecutive_zero_byte_count = 0;
            else
                ++consecutive_zero_byte_count;
            ++ebsp_length;
            continue;
        }
        if( no_more && ebsp_length == 0 )
        {
            /* For the last NALU.
             * This NALU already has been appended into the latest access unit and parsed. */
            h264_update_picture_info( info, picture, slice, &info->sei );
            h264_complete_au( picture, probe );
            return h264_get_au_internal_succeeded( importer_info, picture, &nalu_header, no_more_buf );
        }
        uint64_t next_nalu_head_pos = info->ebsp_head_pos + ebsp_length + !no_more * H264_SHORT_START_CODE_LENGTH;
       /* Memorize position of short start code of the next NALU in buffer.
        * This is used when backward reading of stream doesn't occur. */
        uint8_t *next_short_start_code_pos = lsmash_stream_buffers_get_pos( sb );
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
        if( nalu_type == H264_NALU_TYPE_FD )
        {
            /* We don't support streams with both filler and HRD yet.
             * Otherwise, just skip filler because elemental streams defined in 14496-15 are forbidden to use filler. */
            if( info->sps.vui.hrd.present )
                return h264_get_au_internal_failed( importer_info, picture, &nalu_header, no_more_buf, complete_au );
        }
        else if( (nalu_type >= H264_NALU_TYPE_SLICE_N_IDR && nalu_type <= H264_NALU_TYPE_SPS_EXT)
              || nalu_type == H264_NALU_TYPE_SLICE_AUX )
        {
            /* Get the EBSP of the current NALU here.
             * AVC elemental stream defined in 14496-15 can recognizes from 0 to 13, and 19 of nal_unit_type.
             * We don't support SVC and MVC elemental stream defined in 14496-15 yet. */
            ebsp_length -= consecutive_zero_byte_count;     /* Any EBSP doesn't have zero bytes at the end. */
            uint64_t nalu_length = nalu_header.length + ebsp_length;
            uint64_t possible_au_length = picture->incomplete_au_length + H264_DEFAULT_NALU_LENGTH_SIZE + nalu_length;
            if( lsmash_stream_buffers_get_buffer_size( sb ) < possible_au_length )
            {
                if( h264_supplement_buffer( hb, picture, 2 * possible_au_length ) )
                    return h264_get_au_internal_failed( importer_info, picture, &nalu_header, no_more_buf, complete_au );
                next_short_start_code_pos = lsmash_stream_buffers_get_pos( sb );
            }
            /* Move to the first byte of the current NALU. */
            read_back = (lsmash_stream_buffers_get_offset( sb ) < (nalu_length + consecutive_zero_byte_count));
            if( read_back )
            {
                lsmash_fseek( sb->stream, info->ebsp_head_pos - nalu_header.length, SEEK_SET );
                lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
                lsmash_stream_buffers_read( sb, nalu_length );
                if( lsmash_stream_buffers_get_valid_size( sb ) != nalu_length )
                    return h264_get_au_internal_failed( importer_info, picture, &nalu_header, no_more_buf, complete_au );
#if 0
                if( probe )
                    fprintf( stderr, "    ----Read Back\n" );
#endif
            }
            else
                lsmash_stream_buffers_seek( sb, -(nalu_length + consecutive_zero_byte_count), SEEK_CUR );
            if( nalu_type >= H264_NALU_TYPE_SLICE_N_IDR && nalu_type <= H264_NALU_TYPE_SLICE_IDR )
            {
                /* VCL NALU (slice) */
                h264_slice_info_t prev_slice = *slice;
                if( h264_parse_slice( info, &nalu_header, hb->rbsp,
                                      lsmash_stream_buffers_get_pos( sb ) + nalu_header.length, ebsp_length ) )
                    return h264_get_au_internal_failed( importer_info, picture, &nalu_header, no_more_buf, complete_au );
                if( probe && info->avcC_pending )
                {
                    /* Copy and append a Codec Specific info. */
                    if( h264_store_codec_specific( importer_info, &info->avcC_param ) < 0 )
                        return -1;
                }
                if( h264_move_pending_avcC_param( info ) < 0 )
                    return -1;
                if( prev_slice.present )
                {
                    /* Check whether the AU that contains the previous VCL NALU completed or not. */
                    if( h264_find_au_delimit_by_slice_info( slice, &prev_slice ) )
                    {
                        /* The current NALU is the first VCL NALU of the primary coded picture of an new AU.
                         * Therefore, the previous slice belongs to the AU you want at this time. */
                        h264_update_picture_info( info, picture, &prev_slice, &info->sei );
                        complete_au = h264_complete_au( picture, probe );
                    }
                    else
                        h264_update_picture_info_for_slice( info, picture, &prev_slice );
                }
                h264_append_nalu_to_au( picture, lsmash_stream_buffers_get_pos( sb ), nalu_length, probe );
                slice->present = 1;
            }
            else
            {
                if( h264_find_au_delimit_by_nalu_type( nalu_type, info->prev_nalu_type ) )
                {
                    /* The last slice belongs to the AU you want at this time. */
                    h264_update_picture_info( info, picture, slice, &info->sei );
                    complete_au = h264_complete_au( picture, probe );
                }
                else if( no_more )
                    complete_au = h264_complete_au( picture, probe );
                switch( nalu_type )
                {
                    case H264_NALU_TYPE_SEI :
                    {
                        uint8_t *sei_pos = lsmash_stream_buffers_get_pos( sb );
                        if( h264_parse_sei( info->bits, &info->sps, &info->sei, hb->rbsp, sei_pos + nalu_header.length, ebsp_length ) < 0 )
                            return h264_get_au_internal_failed( importer_info, picture, &nalu_header, no_more_buf, complete_au );
                        h264_append_nalu_to_au( picture, sei_pos, nalu_length, probe );
                        break;
                    }
                    case H264_NALU_TYPE_SPS :
                        if( h264_try_to_append_parameter_set( info, H264_PARAMETER_SET_TYPE_SPS, sb->pos, nalu_header.length + ebsp_length ) < 0 )
                            return h264_get_au_internal_failed( importer_info, picture, &nalu_header, no_more_buf, complete_au );
                        break;
                    case H264_NALU_TYPE_PPS :
                        if( h264_try_to_append_parameter_set( info, H264_PARAMETER_SET_TYPE_PPS, sb->pos, nalu_header.length + ebsp_length ) < 0 )
                            return h264_get_au_internal_failed( importer_info, picture, &nalu_header, no_more_buf, complete_au );
                        break;
                    case H264_NALU_TYPE_AUD :   /* We drop access unit delimiters. */
                        break;
                    case H264_NALU_TYPE_SPS_EXT :
                        if( h264_try_to_append_parameter_set( info, H264_PARAMETER_SET_TYPE_SPSEXT, sb->pos, nalu_header.length + ebsp_length ) < 0 )
                            return h264_get_au_internal_failed( importer_info, picture, &nalu_header, no_more_buf, complete_au );
                        break;
                    default :
                        h264_append_nalu_to_au( picture, lsmash_stream_buffers_get_pos( sb ), nalu_length, probe );
                        break;
                }
                if( info->avcC_pending )
                    importer_info->status = IMPORTER_CHANGE;
            }
        }
        /* Move to the first byte of the next NALU. */
        if( read_back )
        {
            lsmash_fseek( sb->stream, next_nalu_head_pos, SEEK_SET );
            lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
            lsmash_stream_buffers_read( sb, 0 );
        }
        else
            lsmash_stream_buffers_set_pos( sb, next_short_start_code_pos + H264_SHORT_START_CODE_LENGTH );
        info->prev_nalu_type = nalu_type;
        lsmash_stream_buffers_update( sb, 0 );
        no_more_buf = (lsmash_stream_buffers_get_remainder( sb ) == 0);
        ebsp_length = 0;
        no_more = lsmash_stream_buffers_is_eos( sb ) && no_more_buf;
        if( !no_more )
        {
            /* Check the next NALU header. */
            if( h264_check_nalu_header( &nalu_header, sb, !!consecutive_zero_byte_count ) )
                return h264_get_au_internal_failed( importer_info, picture, &nalu_header, no_more_buf, complete_au );
            info->ebsp_head_pos = next_nalu_head_pos + nalu_header.length;
        }
        /* If there is no more data in the stream, and flushed chunk of NALUs, flush it as complete AU here. */
        else if( picture->incomplete_au_length && picture->au_length == 0 )
        {
            h264_update_picture_info( info, picture, slice, &info->sei );
            h264_complete_au( picture, probe );
            return h264_get_au_internal_succeeded( importer_info, picture, &nalu_header, no_more_buf );
        }
        if( complete_au )
            return h264_get_au_internal_succeeded( importer_info, picture, &nalu_header, no_more_buf );
        consecutive_zero_byte_count = 0;
    }
}

static int h264_importer_get_accessunit
(
    importer_t      *importer,
    uint32_t         track_number,
    lsmash_sample_t *buffered_sample
)
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    h264_importer_info_t *importer_info = (h264_importer_info_t *)importer->info;
    h264_info_t *info = &importer_info->info;
    importer_status current_status = importer_info->status;
    if( current_status == IMPORTER_ERROR || buffered_sample->length < importer_info->max_au_length )
        return -1;
    if( current_status == IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( h264_get_access_unit_internal( importer_info, 0 ) )
    {
        importer_info->status = IMPORTER_ERROR;
        return -1;
    }
    if( importer_info->status == IMPORTER_CHANGE && !info->avcC_pending )
        current_status = IMPORTER_CHANGE;
    if( current_status == IMPORTER_CHANGE )
    {
        /* Update the active summary. */
        lsmash_codec_specific_t *cs = (lsmash_codec_specific_t *)lsmash_get_entry_data( importer_info->avcC_list, ++ importer_info->avcC_number );
        if( !cs )
            return -1;
        lsmash_h264_specific_parameters_t *avcC_param = (lsmash_h264_specific_parameters_t *)cs->data.structured;
        lsmash_video_summary_t *summary = h264_create_summary( avcC_param, &info->sps, importer_info->max_au_length );
        if( !summary )
            return -1;
        lsmash_remove_entry( importer->summaries, track_number, lsmash_cleanup_summary );
        if( lsmash_add_entry( importer->summaries, summary ) )
        {
            lsmash_cleanup_summary( (lsmash_summary_t *)summary );
            return -1;
        }
        importer_info->status = IMPORTER_OK;
    }
    h264_sps_t *sps = &info->sps;
    h264_picture_info_t *picture = &info->picture;
    buffered_sample->dts = importer_info->ts_list.timestamp[picture->au_number - 1].dts;
    buffered_sample->cts = importer_info->ts_list.timestamp[picture->au_number - 1].cts;
    if( picture->au_number < importer_info->num_undecodable )
        buffered_sample->prop.leading = ISOM_SAMPLE_IS_UNDECODABLE_LEADING;
    else
        buffered_sample->prop.leading = picture->independent || buffered_sample->cts >= importer_info->last_intra_cts
                                      ? ISOM_SAMPLE_IS_NOT_LEADING : ISOM_SAMPLE_IS_UNDECODABLE_LEADING;
    if( picture->independent )
        importer_info->last_intra_cts = buffered_sample->cts;
    if( importer_info->composition_reordering_present && !picture->disposable && !picture->idr )
        buffered_sample->prop.allow_earlier = QT_SAMPLE_EARLIER_PTS_ALLOWED;
    buffered_sample->prop.independent = picture->independent    ? ISOM_SAMPLE_IS_INDEPENDENT : ISOM_SAMPLE_IS_NOT_INDEPENDENT;
    buffered_sample->prop.disposable  = picture->disposable     ? ISOM_SAMPLE_IS_DISPOSABLE  : ISOM_SAMPLE_IS_NOT_DISPOSABLE;
    buffered_sample->prop.redundant   = picture->has_redundancy ? ISOM_SAMPLE_HAS_REDUNDANCY : ISOM_SAMPLE_HAS_NO_REDUNDANCY;
    buffered_sample->prop.post_roll.identifier = picture->frame_num;
    if( picture->random_accessible )
    {
        if( picture->idr )
            buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
        else if( picture->recovery_frame_cnt )
        {
            buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_POST_ROLL_START;
            buffered_sample->prop.post_roll.complete = (picture->frame_num + picture->recovery_frame_cnt) % sps->MaxFrameNum;
        }
        else
        {
            buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_RAP;
            if( !picture->broken_link_flag )
                buffered_sample->prop.ra_flags |= QT_SAMPLE_RANDOM_ACCESS_FLAG_PARTIAL_SYNC;
        }
    }
    buffered_sample->length = picture->au_length;
    memcpy( buffered_sample->data, picture->au, picture->au_length );
    return current_status;
}

static void nalu_deduplicate_poc
(
    nal_pic_timing_t *npt,
    uint32_t         *max_composition_delay,
    uint32_t          num_access_units,
    uint32_t          max_num_reorder_pics
)
{
    /* Deduplicate POCs. */
    int64_t  poc_offset            = 0;
    int64_t  poc_min               = 0;
    int64_t  invalid_poc_min       = 0;
    uint32_t last_poc_reset        = UINT32_MAX;
    uint32_t invalid_poc_start     = 0;
    int      invalid_poc_present   = 0;
    for( uint32_t i = 0; ; i++ )
    {
        if( i < num_access_units && npt[i].poc != 0 && !npt[i].reset )
        {
            /* poc_offset is not added to each POC here.
             * It is done when we encounter the next coded video sequence. */
            if( npt[i].poc < 0 )
            {
                /* Pictures with negative POC shall precede IDR-picture in composition order.
                 * The minimum POC is added to poc_offset when we encounter the next coded video sequence. */
                if( last_poc_reset == UINT32_MAX || i > last_poc_reset + max_num_reorder_pics )
                {
                    if( !invalid_poc_present )
                    {
                        invalid_poc_present = 1;
                        invalid_poc_start   = i;
                    }
                    if( invalid_poc_min > npt[i].poc )
                        invalid_poc_min = npt[i].poc;
                }
                else if( poc_min > npt[i].poc )
                {
                    poc_min = npt[i].poc;
                    *max_composition_delay = LSMASH_MAX( *max_composition_delay, i - last_poc_reset );
                }
            }
            continue;
        }
        /* Encountered a new coded video sequence or no more POCs.
         * Add poc_offset to each POC of the previous coded video sequence. */
        poc_offset -= poc_min;
        int64_t poc_max = 0;
        for( uint32_t j = last_poc_reset; j < i + !!npt[i].reset; j++ )
            if( npt[j].poc >= 0 || (j <= last_poc_reset + max_num_reorder_pics) )
            {
                npt[j].poc += poc_offset;
                if( poc_max < npt[j].poc )
                    poc_max = npt[j].poc;
            }
        poc_offset = poc_max + 1;
        if( invalid_poc_present )
        {
            /* Pictures with invalid negative POC is probably supposed to be composited
             * both before the next coded video sequence and after the current one. */
            poc_offset -= invalid_poc_min;
            for( uint32_t j = invalid_poc_start; j < i + !!npt[i].reset; j++ )
                if( npt[j].poc < 0 )
                {
                    npt[j].poc += poc_offset;
                    if( poc_max < npt[j].poc )
                        poc_max = npt[j].poc;
                }
            invalid_poc_present = 0;
            invalid_poc_start   = 0;
            invalid_poc_min     = 0;
            poc_offset = poc_max + 1;
        }
        if( i < num_access_units )
        {
            if( npt[i].reset )
                npt[i].poc = 0;
            poc_min        = 0;
            last_poc_reset = i;
        }
        else
            break;      /* no more POCs */
    }
}

static void nalu_generate_timestamps_from_poc
(
    importer_t        *importer,
    lsmash_media_ts_t *timestamp,
    nal_pic_timing_t  *npt,
    uint8_t           *composition_reordering_present,
    uint32_t          *last_delta,
    uint32_t           max_composition_delay,
    uint32_t           num_access_units
)
{
    /* Check if composition delay derived from reordering is present. */
    if( max_composition_delay == 0 )
    {
        for( uint32_t i = 1; i < num_access_units; i++ )
            if( npt[i].poc < npt[i - 1].poc )
            {
                *composition_reordering_present = 1;
                break;
            }
    }
    else
        *composition_reordering_present = 1;
    /* Generate timestamps. */
    if( *composition_reordering_present )
    {
        /* Generate timestamps.
         * Here, DTSs and CTSs are temporary values for sort. */
        for( uint32_t i = 0; i < num_access_units; i++ )
        {
            timestamp[i].cts = (uint64_t)npt[i].poc;
            timestamp[i].dts = (uint64_t)i;
        }
        qsort( timestamp, num_access_units, sizeof(lsmash_media_ts_t), (int(*)( const void *, const void * ))lsmash_compare_cts );
        /* Check POC gap in output order. */
        for( uint32_t i = 1; i < num_access_units; i++ )
            if( timestamp[i].cts > timestamp[i - 1].cts + npt[i - 1].poc_delta )
                lsmash_log( importer, LSMASH_LOG_WARNING,
                            "POC gap is detected at picture %"PRIu64". Maybe some pictures are lost.\n", timestamp[i].dts );
        /* Get the maximum composition delay derived from reordering. */
        for( uint32_t i = 0; i < num_access_units; i++ )
            if( i < timestamp[i].dts )
            {
                uint32_t composition_delay = timestamp[i].dts - i;
                max_composition_delay = LSMASH_MAX( max_composition_delay, composition_delay );
            }
        uint64_t *ts_buffer = (uint64_t *)lsmash_malloc( (num_access_units + max_composition_delay) * sizeof(uint64_t) );
        if( !ts_buffer )
        {
            /* It seems that there is no enough memory to generate more appropriate timestamps.
             * Anyway, generate CTSs and DTSs. */
            for( uint32_t i = 0; i < num_access_units; i++ )
                timestamp[i].cts = i + max_composition_delay;
            qsort( timestamp, num_access_units, sizeof(lsmash_media_ts_t), (int(*)( const void *, const void * ))lsmash_compare_dts );
            *last_delta = 1;
            return;
        }
        uint64_t *reorder_cts      = ts_buffer;
        uint64_t *prev_reorder_cts = ts_buffer + num_access_units;
        *last_delta = npt[num_access_units - 1].delta;
        /* Generate CTSs. */
        timestamp[0].cts = 0;
        for( uint32_t i = 1; i < num_access_units; i++ )
            timestamp[i].cts = timestamp[i - 1].cts + npt[i - 1].delta;
        int64_t composition_delay_time = timestamp[max_composition_delay].cts;
        for( uint32_t i = 0; i < num_access_units; i++ )
        {
            timestamp[i].cts += composition_delay_time;
            reorder_cts[i] = timestamp[i].cts;
        }
        /* Generate DTSs. */
        qsort( timestamp, num_access_units, sizeof(lsmash_media_ts_t), (int(*)( const void *, const void * ))lsmash_compare_dts );
        for( uint32_t i = 0; i < num_access_units; i++ )
        {
            timestamp[i].dts = i <= max_composition_delay
                             ? reorder_cts[i] - composition_delay_time
                             : prev_reorder_cts[(i - max_composition_delay) % max_composition_delay];
            prev_reorder_cts[i % max_composition_delay] = reorder_cts[i];
        }
        lsmash_free( ts_buffer );
#if 0
        fprintf( stderr, "max_composition_delay=%"PRIu32", composition_delay_time=%"PRIu64"\n",
                          max_composition_delay, composition_delay_time );
#endif
    }
    else
    {
        timestamp[0].dts = 0;
        timestamp[0].cts = 0;
        for( uint32_t i = 1; i < num_access_units; i++ )
        {
            timestamp[i].dts = timestamp[i - 1].dts + npt[i - 1].delta;
            timestamp[i].cts = timestamp[i - 1].cts + npt[i - 1].delta;
        }
        *last_delta = npt[num_access_units - 1].delta;
    }
}

static void nalu_reduce_timescale
(
    lsmash_media_ts_t *timestamp,
    nal_pic_timing_t  *npt,
    uint32_t          *last_delta,
    uint32_t          *timescale,
    uint32_t           num_access_units
)
{
    uint64_t gcd_delta = *timescale;
    for( uint32_t i = 0; i < num_access_units && gcd_delta > 1; i++ )
        gcd_delta = lsmash_get_gcd( gcd_delta, npt[i].delta );
    if( gcd_delta > 1 )
    {
        for( uint32_t i = 0; i < num_access_units; i++ )
        {
            timestamp[i].dts /= gcd_delta;
            timestamp[i].cts /= gcd_delta;
        }
        *last_delta /= gcd_delta;
        *timescale  /= gcd_delta;
    }
#if 0
    for( uint32_t i = 0; i < num_access_units; i++ )
        fprintf( stderr, "Timestamp[%"PRIu32"]: POC=%"PRId64", DTS=%"PRIu64", CTS=%"PRIu64"\n",
                 i, npt[i].poc, timestamp[i].dts, timestamp[i].cts );
#endif
}

static int h264_importer_probe( importer_t *importer )
{
#define H264_LONG_START_CODE_LENGTH 4
#define H264_CHECK_NEXT_LONG_START_CODE( x ) (!(x)[0] && !(x)[1] && !(x)[2] && ((x)[3] == 0x01))
    h264_importer_info_t *importer_info = create_h264_importer_info( importer );
    if( !importer_info )
        return -1;
    h264_info_t             *info = &importer_info->info;
    h264_stream_buffer_t    *hb   = &info->buffer;
    lsmash_stream_buffers_t *sb   = hb->sb;     /* shall be equal to &importer->sb */
    /* Find the first start code. */
    lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
    lsmash_stream_buffers_read( sb, 0 );
    while( 1 )
    {
        /* The first NALU of an AU in decoding order shall have long start code (0x00000001). */
        if( H264_CHECK_NEXT_LONG_START_CODE( sb->pos ) )
            break;
        /* If the first trial of finding long start code failed, we assume this stream is not byte stream format of H.264. */
        if( lsmash_stream_buffers_get_remainder( sb ) == H264_LONG_START_CODE_LENGTH )
            goto fail;
        /* Invalid if encountered any value of non-zero before the first start code. */
        if( lsmash_stream_buffers_get_byte( sb ) )
            goto fail;
    }
    /* OK. It seems the stream has a long start code of H.264. */
    importer->info = importer_info;
    lsmash_stream_buffers_seek( sb, H264_LONG_START_CODE_LENGTH, SEEK_CUR );
    uint64_t first_ebsp_head_pos = lsmash_stream_buffers_get_offset( sb );
    lsmash_stream_buffers_update( sb, 0 );
    h264_nalu_header_t first_nalu_header;
    if( h264_check_nalu_header( &first_nalu_header, sb, 1 ) )
        goto fail;
    if( lsmash_stream_buffers_get_remainder( sb ) == 0 )
        goto fail;  /* It seems the stream ends at the first incomplete access unit. */
    first_ebsp_head_pos += first_nalu_header.length;    /* EBSP doesn't include NALU header. */
    importer_info->status = IMPORTER_OK;
    info->nalu_header   = first_nalu_header;
    info->ebsp_head_pos = first_ebsp_head_pos;
    /* Parse all NALU in the stream for preparation of calculating timestamps. */
    uint32_t npt_alloc = (1 << 12) * sizeof(nal_pic_timing_t);
    nal_pic_timing_t *npt = lsmash_malloc( npt_alloc );
    if( !npt )
        goto fail;
    uint32_t picture_stats[H264_PICTURE_TYPE_NONE + 1] = { 0 };
    uint32_t num_access_units = 0;
    lsmash_log( importer, LSMASH_LOG_INFO, "Analyzing stream as H.264\r" );
    while( importer_info->status != IMPORTER_EOF )
    {
#if 0
        lsmash_log( importer, LSMASH_LOG_INFO, "Analyzing stream as H.264: %"PRIu32"\n", num_access_units + 1 );
#endif
        h264_picture_info_t     *picture = &info->picture;
        h264_picture_info_t prev_picture = *picture;
        if( h264_get_access_unit_internal( importer_info, 1 )
         || h264_calculate_poc( info, picture, &prev_picture ) )
        {
            lsmash_free( npt );
            goto fail;
        }
        if( npt_alloc <= num_access_units * sizeof(nal_pic_timing_t) )
        {
            uint32_t alloc = 2 * num_access_units * sizeof(nal_pic_timing_t);
            nal_pic_timing_t *temp = (nal_pic_timing_t *)lsmash_realloc( npt, alloc );
            if( !temp )
            {
                lsmash_free( npt );
                goto fail;
            }
            npt = temp;
            npt_alloc = alloc;
        }
        importer_info->field_pic_present |= info->picture.field_pic_flag;
        npt[num_access_units].poc       = picture->PicOrderCnt;
        npt[num_access_units].delta     = picture->delta;
        npt[num_access_units].poc_delta = picture->field_pic_flag ? 1 : 2;
        npt[num_access_units].reset     = picture->has_mmco5;
        ++num_access_units;
        importer_info->max_au_length = LSMASH_MAX( info->picture.au_length, importer_info->max_au_length );
        if( picture->idr )
            ++picture_stats[H264_PICTURE_TYPE_IDR];
        else if( picture->type >= H264_PICTURE_TYPE_NONE )
            ++picture_stats[H264_PICTURE_TYPE_NONE];
        else
            ++picture_stats[ picture->type ];
    }
    fprintf( stderr, "                                                                               \r" );
    lsmash_log( importer, LSMASH_LOG_INFO,
                "IDR: %"PRIu32", I: %"PRIu32", P: %"PRIu32", B: %"PRIu32", "
                "SI: %"PRIu32", SP: %"PRIu32", Unknown: %"PRIu32"\n",
                picture_stats[H264_PICTURE_TYPE_IDR        ],
                picture_stats[H264_PICTURE_TYPE_I          ],
                picture_stats[H264_PICTURE_TYPE_I_P        ],
                picture_stats[H264_PICTURE_TYPE_I_P_B      ],
                picture_stats[H264_PICTURE_TYPE_SI         ]
              + picture_stats[H264_PICTURE_TYPE_I_SI       ],
                picture_stats[H264_PICTURE_TYPE_SI_SP      ]
              + picture_stats[H264_PICTURE_TYPE_I_SI_P_SP  ]
              + picture_stats[H264_PICTURE_TYPE_I_SI_P_SP_B],
                picture_stats[H264_PICTURE_TYPE_NONE       ] );
    /* Copy and append the last Codec Specific info. */
    if( h264_store_codec_specific( importer_info, &info->avcC_param ) < 0 )
        return -1;
    /* Set up the first summary. */
    lsmash_codec_specific_t *cs = (lsmash_codec_specific_t *)lsmash_get_entry_data( importer_info->avcC_list, ++ importer_info->avcC_number );
    if( !cs || !cs->data.structured )
    {
        lsmash_free( npt );
        goto fail;
    }
    lsmash_h264_specific_parameters_t *avcC_param = (lsmash_h264_specific_parameters_t *)cs->data.structured;
    lsmash_video_summary_t *summary = h264_create_summary( avcC_param, &info->sps, importer_info->max_au_length );
    if( !summary || lsmash_add_entry( importer->summaries, summary ) )
    {
        if( summary )
            lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_free( npt );
        goto fail;
    }
    summary->sample_per_field = importer_info->field_pic_present;
    /* */
    lsmash_media_ts_t *timestamp = lsmash_malloc( num_access_units * sizeof(lsmash_media_ts_t) );
    if( !timestamp )
    {
        lsmash_free( npt );
        goto fail;
    }
    /* Count leading samples that are undecodable. */
    for( uint32_t i = 0; i < num_access_units; i++ )
    {
        if( npt[i].poc == 0 )
            break;
        ++ importer_info->num_undecodable;
    }
    /* Deduplicate POCs. */
    uint32_t max_composition_delay = 0;
    nalu_deduplicate_poc( npt, &max_composition_delay, num_access_units, 32 );
    /* Generate timestamps. */
    nalu_generate_timestamps_from_poc( importer, timestamp, npt,
                                       &importer_info->composition_reordering_present,
                                       &importer_info->last_delta,
                                       max_composition_delay, num_access_units );
    nalu_reduce_timescale( timestamp, npt, &importer_info->last_delta, &summary->timescale, num_access_units );
    lsmash_free( npt );
    importer_info->ts_list.sample_count = num_access_units;
    importer_info->ts_list.timestamp    = timestamp;
    /* Go back to EBSP of the first NALU. */
    lsmash_fseek( sb->stream, first_ebsp_head_pos, SEEK_SET );
    importer_info->status       = IMPORTER_OK;
    info->nalu_header           = first_nalu_header;
    info->prev_nalu_type        = 0;
    sb->no_more_read            = 0;
    lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
    lsmash_stream_buffers_read( sb, 0 );
    info->ebsp_head_pos         = first_ebsp_head_pos;
    uint8_t *temp_au            = info->picture.au;
    uint8_t *temp_incomplete_au = info->picture.incomplete_au;
    memset( &info->picture, 0, sizeof(h264_picture_info_t) );
    info->picture.au            = temp_au;
    info->picture.incomplete_au = temp_incomplete_au;
    memset( &info->slice, 0, sizeof(h264_slice_info_t) );
    memset( &info->sps, 0, sizeof(h264_sps_t) );
    memset( &info->pps, 0, sizeof(h264_pps_t) );
    lsmash_remove_entries( info->avcC_param.parameter_sets->sps_list,    isom_remove_dcr_ps );
    lsmash_remove_entries( info->avcC_param.parameter_sets->pps_list,    isom_remove_dcr_ps );
    lsmash_remove_entries( info->avcC_param.parameter_sets->spsext_list, isom_remove_dcr_ps );
    lsmash_destroy_h264_parameter_sets( &info->avcC_param_next );
    return 0;
fail:
    remove_h264_importer_info( importer_info );
    importer->info = NULL;
    lsmash_remove_entries( importer->summaries, lsmash_cleanup_summary );
    return -1;
#undef H264_LONG_START_CODE_LENGTH
#undef H264_CHECK_NEXT_LONG_START_CODE
}

static uint32_t h264_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    h264_importer_info_t *info = (h264_importer_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != IMPORTER_EOF )
        return 0;
    return info->ts_list.sample_count
         ? info->last_delta
         : UINT32_MAX;    /* arbitrary */
}

const static importer_functions h264_importer =
{
    { "H.264" },
    1,
    h264_importer_probe,
    h264_importer_get_accessunit,
    h264_importer_get_last_delta,
    h264_importer_cleanup
};

/***************************************************************************
    HEVC importer
    ITU-T Recommendation H.265 (04/13)
    ISO/IEC 14496-15:2013 FDIS
***************************************************************************/
#include "hevc.h"

typedef struct
{
    const lsmash_class_t  *class;
    importer_status        status;
    hevc_info_t            info;
    lsmash_entry_list_t    hvcC_list[1];    /* stored as lsmash_codec_specific_t */
    lsmash_media_ts_list_t ts_list;
    uint32_t max_au_length;
    uint32_t num_undecodable;
    uint32_t hvcC_number;
    uint32_t last_delta;
    uint64_t last_intra_cts;
    uint8_t  composition_reordering_present;
    uint8_t  field_pic_present;
    uint8_t  max_TemporalId;
} hevc_importer_info_t;

static void remove_hevc_importer_info( hevc_importer_info_t *info )
{
    if( !info )
        return;
    lsmash_remove_entries( info->hvcC_list, lsmash_destroy_codec_specific_data );
    hevc_cleanup_parser( &info->info );
    if( info->ts_list.timestamp )
        lsmash_free( info->ts_list.timestamp );
    lsmash_free( info );
}

static void hevc_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        remove_hevc_importer_info( importer->info );
}

static hevc_importer_info_t *create_hevc_importer_info( importer_t *importer )
{
    hevc_importer_info_t *info = lsmash_malloc_zero( sizeof(hevc_importer_info_t) );
    if( !info )
        return NULL;
    if( hevc_setup_parser( &info->info, &importer->sb, 0, LSMASH_STREAM_BUFFERS_TYPE_FILE, importer->stream ) )
    {
        remove_hevc_importer_info( info );
        return NULL;
    }
    lsmash_init_entry_list( info->hvcC_list );
    return info;
}

static inline int hevc_complete_au( hevc_access_unit_t *au, int probe )
{
    if( !au->picture.has_primary || au->incomplete_length == 0 )
        return 0;
    if( !probe )
        memcpy( au->data, au->incomplete_data, au->incomplete_length );
    au->TemporalId          = au->picture.TemporalId;
    au->length              = au->incomplete_length;
    au->incomplete_length   = 0;
    au->picture.has_primary = 0;
    return 1;
}

static void hevc_append_nalu_to_au( hevc_access_unit_t *au, uint8_t *src_nalu, uint32_t nalu_length, int probe )
{
    if( !probe )
    {
        uint8_t *dst_nalu = au->incomplete_data + au->incomplete_length + HEVC_DEFAULT_NALU_LENGTH_SIZE;
        for( int i = HEVC_DEFAULT_NALU_LENGTH_SIZE; i; i-- )
            *(dst_nalu - i) = (nalu_length >> ((i - 1) * 8)) & 0xff;
        memcpy( dst_nalu, src_nalu, nalu_length );
    }
    /* Note: picture->incomplete_au_length shall be 0 immediately after AU has completed.
     * Therefore, possible_au_length in hevc_get_access_unit_internal() can't be used here
     * to avoid increasing AU length monotonously through the entire stream. */
    au->incomplete_length += HEVC_DEFAULT_NALU_LENGTH_SIZE + nalu_length;
}

static inline void hevc_get_au_internal_end( hevc_importer_info_t *info, hevc_access_unit_t *au, hevc_nalu_header_t *nalu_header, int no_more_buf )
{
    if( info->info.buffer.sb->no_more_read && no_more_buf && (au->incomplete_length == 0) )
        info->status = IMPORTER_EOF;
    else if( info->status != IMPORTER_CHANGE )
        info->status = IMPORTER_OK;
    info->info.nalu_header = *nalu_header;
}

static int hevc_get_au_internal_succeeded( hevc_importer_info_t *info, hevc_access_unit_t *au, hevc_nalu_header_t *nalu_header, int no_more_buf )
{
    hevc_get_au_internal_end( info, au, nalu_header, no_more_buf );
    au->number += 1;
    return 0;
}

static int hevc_get_au_internal_failed( hevc_importer_info_t *info, hevc_access_unit_t *au, hevc_nalu_header_t *nalu_header, int no_more_buf, int complete_au )
{
    hevc_get_au_internal_end( info, au, nalu_header, no_more_buf );
    if( complete_au )
        au->number += 1;
    return -1;
}

static lsmash_video_summary_t *hevc_create_summary
(
    lsmash_hevc_specific_parameters_t *param,
    hevc_sps_t                        *sps,
    uint32_t                           max_au_length
)
{
    lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_VIDEO );
    if( !summary )
        return NULL;
    /* Update summary here.
     * max_au_length is set at the last of hevc_importer_probe function. */
    lsmash_codec_specific_t *specific = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_HEVC,
                                                                           LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
    specific->data.unstructured = lsmash_create_hevc_specific_info( param, &specific->size );
    if( !specific->data.unstructured
     || lsmash_add_entry( &summary->opaque->list, specific ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        return NULL;
    }
    summary->sample_type            = ISOM_CODEC_TYPE_HVC1_VIDEO;
    summary->max_au_length          = max_au_length;
    summary->timescale              = sps->vui.time_scale;
    summary->timebase               = sps->vui.num_units_in_tick;
    summary->vfr                    = (param->constantFrameRate == 0);
    summary->sample_per_field       = 0;
    summary->width                  = sps->cropped_width;
    summary->height                 = sps->cropped_height;
    summary->par_h                  = sps->vui.sar_width;
    summary->par_v                  = sps->vui.sar_height;
    summary->color.primaries_index  = sps->vui.colour_primaries         != 2 ? sps->vui.colour_primaries         : 0;
    summary->color.transfer_index   = sps->vui.transfer_characteristics != 2 ? sps->vui.transfer_characteristics : 0;
    summary->color.matrix_index     = sps->vui.matrix_coeffs            != 2 ? sps->vui.matrix_coeffs            : 0;
    summary->color.full_range       = sps->vui.video_full_range_flag;
    lsmash_convert_crop_into_clap( sps->vui.def_disp_win_offset, summary->width, summary->height, &summary->clap );
    return summary;
}

static int hevc_store_codec_specific
(
    hevc_importer_info_t              *info,
    lsmash_hevc_specific_parameters_t *hvcC_param
)
{
    lsmash_codec_specific_t *src_cs = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_HEVC,
                                                                         LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
    if( !src_cs )
        return -1;
    lsmash_hevc_specific_parameters_t *src_param = (lsmash_hevc_specific_parameters_t *)src_cs->data.structured;
    *src_param = *hvcC_param;
    lsmash_codec_specific_t *dst_cs = lsmash_convert_codec_specific_format( src_cs, LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
    src_param->parameter_arrays = NULL;     /* Avoid freeing parameter arrays within hvcC_param. */
    lsmash_destroy_codec_specific_data( src_cs );
    if( !dst_cs || lsmash_add_entry( info->hvcC_list, dst_cs ) )
    {
        lsmash_destroy_codec_specific_data( dst_cs );
        return -1;
    }
    return 0;
}

/* If probe equals 0, don't get the actual data (EBPS) of an access unit and only parse NALU. */
static int hevc_get_access_unit_internal
(
    hevc_importer_info_t *importer_info,
    int                   probe
)
{
    hevc_info_t             *info     = &importer_info->info;
    hevc_slice_info_t       *slice    = &info->slice;
    hevc_access_unit_t      *au       = &info->au;
    hevc_picture_info_t     *picture  = &au->picture;
    hevc_stream_buffer_t    *hb       = &info->buffer;
    lsmash_stream_buffers_t *sb       = hb->sb;
    hevc_nalu_header_t nalu_header = info->nalu_header;
    uint64_t consecutive_zero_byte_count = 0;
    uint64_t ebsp_length = 0;
    int      no_more_buf = 0;
    int      complete_au = 0;
    picture->type              = HEVC_PICTURE_TYPE_NONE;
    picture->random_accessible = 0;
    picture->recovery_poc_cnt  = 0;
    au->length = 0;
    while( 1 )
    {
        lsmash_stream_buffers_update( sb, 2 );
        no_more_buf = (lsmash_stream_buffers_get_remainder( sb ) == 0);
        int no_more = lsmash_stream_buffers_is_eos( sb ) && no_more_buf;
        if( !nalu_check_next_short_start_code( sb->pos, sb->end ) && !no_more )
        {
            if( lsmash_stream_buffers_get_byte( sb ) )
                consecutive_zero_byte_count = 0;
            else
                ++consecutive_zero_byte_count;
            ++ebsp_length;
            continue;
        }
        if( no_more && ebsp_length == 0 )
        {
            /* For the last NALU.
             * This NALU already has been appended into the latest access unit and parsed. */
            hevc_update_picture_info( info, picture, slice, &info->sps, &info->sei );
            hevc_complete_au( au, probe );
            return hevc_get_au_internal_succeeded( importer_info, au, &nalu_header, no_more_buf );
        }
        uint64_t next_nalu_head_pos = info->ebsp_head_pos + ebsp_length + !no_more * HEVC_SHORT_START_CODE_LENGTH;
       /* Memorize position of short start code of the next NALU in buffer.
        * This is used when backward reading of stream doesn't occur. */
        uint8_t *next_short_start_code_pos = lsmash_stream_buffers_get_pos( sb );
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
        if( nalu_type == HEVC_NALU_TYPE_FD )
        {
            /* We don't support streams with both filler and HRD yet.
             * Otherwise, just skip filler because elemental streams defined in 14496-15 are forbidden to use filler. */
            if( info->sps.vui.hrd.present )
                return hevc_get_au_internal_failed( importer_info, au, &nalu_header, no_more_buf, complete_au );
        }
        else if( nalu_type <= HEVC_NALU_TYPE_RASL_R
             || (nalu_type >= HEVC_NALU_TYPE_BLA_W_LP && nalu_type <= HEVC_NALU_TYPE_CRA)
             || (nalu_type >= HEVC_NALU_TYPE_VPS      && nalu_type <= HEVC_NALU_TYPE_SUFFIX_SEI)  )
        {
            /* Get the EBSP of the current NALU here. */
            ebsp_length -= consecutive_zero_byte_count;     /* Any EBSP doesn't have zero bytes at the end. */
            uint64_t nalu_length = nalu_header.length + ebsp_length;
            uint64_t possible_au_length = au->incomplete_length + HEVC_DEFAULT_NALU_LENGTH_SIZE + nalu_length;
            if( lsmash_stream_buffers_get_buffer_size( sb ) < possible_au_length )
            {
                if( hevc_supplement_buffer( hb, au, 2 * possible_au_length ) )
                    return hevc_get_au_internal_failed( importer_info, au, &nalu_header, no_more_buf, complete_au );
                next_short_start_code_pos = lsmash_stream_buffers_get_pos( sb );
            }
            /* Move to the first byte of the current NALU. */
            read_back = (lsmash_stream_buffers_get_offset( sb ) < (nalu_length + consecutive_zero_byte_count));
            if( read_back )
            {
                lsmash_fseek( sb->stream, info->ebsp_head_pos - nalu_header.length, SEEK_SET );
                lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
                lsmash_stream_buffers_read( sb, nalu_length );
                if( lsmash_stream_buffers_get_valid_size( sb ) != nalu_length )
                    return hevc_get_au_internal_failed( importer_info, au, &nalu_header, no_more_buf, complete_au );
#if 0
                if( probe )
                    fprintf( stderr, "    ----Read Back\n" );
#endif
            }
            else
                lsmash_stream_buffers_seek( sb, -(nalu_length + consecutive_zero_byte_count), SEEK_CUR );
            if( nalu_type <= HEVC_NALU_TYPE_RSV_VCL31 )
            {
                /* VCL NALU (slice) */
                hevc_slice_info_t prev_slice = *slice;
                if( hevc_parse_slice_segment_header( info, &nalu_header, hb->rbsp,
                                                     lsmash_stream_buffers_get_pos( sb ) + nalu_header.length, ebsp_length ) )
                    return hevc_get_au_internal_failed( importer_info, au, &nalu_header, no_more_buf, complete_au );
                if( probe && info->hvcC_pending )
                {
                    /* Copy and append a Codec Specific info. */
                    if( hevc_store_codec_specific( importer_info, &info->hvcC_param ) < 0 )
                        return -1;
                }
                if( hevc_move_pending_hvcC_param( info ) < 0 )
                    return -1;
                if( prev_slice.present )
                {
                    /* Check whether the AU that contains the previous VCL NALU completed or not. */
                    if( hevc_find_au_delimit_by_slice_info( info, slice, &prev_slice ) )
                    {
                        /* The current NALU is the first VCL NALU of the primary coded picture of a new AU.
                         * Therefore, the previous slice belongs to the AU you want at this time. */
                        hevc_update_picture_info( info, picture, &prev_slice, &info->sps, &info->sei );
                        complete_au = hevc_complete_au( au, probe );
                    }
                    else
                        hevc_update_picture_info_for_slice( info, picture, &prev_slice );
                }
                hevc_append_nalu_to_au( au, lsmash_stream_buffers_get_pos( sb ), nalu_length, probe );
                slice->present = 1;
            }
            else
            {
                if( hevc_find_au_delimit_by_nalu_type( nalu_type, info->prev_nalu_type ) )
                {
                    /* The last slice belongs to the AU you want at this time. */
                    hevc_update_picture_info( info, picture, slice, &info->sps, &info->sei );
                    complete_au = hevc_complete_au( au, probe );
                }
                else if( no_more )
                    complete_au = hevc_complete_au( au, probe );
                switch( nalu_type )
                {
                    case HEVC_NALU_TYPE_PREFIX_SEI :
                    case HEVC_NALU_TYPE_SUFFIX_SEI :
                    {
                        uint8_t *sei_pos = lsmash_stream_buffers_get_pos( sb );
                        if( hevc_parse_sei( info->bits, &info->vps, &info->sps, &info->sei, &nalu_header,
                                            hb->rbsp, sei_pos + nalu_header.length, ebsp_length ) )
                            return hevc_get_au_internal_failed( importer_info, au, &nalu_header, no_more_buf, complete_au );
                        hevc_append_nalu_to_au( au, sei_pos, nalu_length, probe );
                        break;
                    }
                    case HEVC_NALU_TYPE_VPS :
                        if( hevc_try_to_append_dcr_nalu( info, HEVC_DCR_NALU_TYPE_VPS, sb->pos, nalu_header.length + ebsp_length ) < 0 )
                            return hevc_get_au_internal_failed( importer_info, au, &nalu_header, no_more_buf, complete_au );
                        break;
                    case HEVC_NALU_TYPE_SPS :
                        if( hevc_try_to_append_dcr_nalu( info, HEVC_DCR_NALU_TYPE_SPS, sb->pos, nalu_header.length + ebsp_length ) < 0 )
                            return hevc_get_au_internal_failed( importer_info, au, &nalu_header, no_more_buf, complete_au );
                        break;
                    case HEVC_NALU_TYPE_PPS :
                        if( hevc_try_to_append_dcr_nalu( info, HEVC_DCR_NALU_TYPE_PPS, sb->pos, nalu_header.length + ebsp_length ) < 0 )
                            return hevc_get_au_internal_failed( importer_info, au, &nalu_header, no_more_buf, complete_au );
                        break;
                    case HEVC_NALU_TYPE_AUD :   /* We drop access unit delimiters. */
                        break;
                    default :
                        hevc_append_nalu_to_au( au, lsmash_stream_buffers_get_pos( sb ), nalu_length, probe );
                        break;
                }
                if( info->hvcC_pending )
                    importer_info->status = IMPORTER_CHANGE;
            }
        }
        /* Move to the first byte of the next NALU. */
        if( read_back )
        {
            lsmash_fseek( sb->stream, next_nalu_head_pos, SEEK_SET );
            lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
            lsmash_stream_buffers_read( sb, 0 );
        }
        else
            lsmash_stream_buffers_set_pos( sb, next_short_start_code_pos + HEVC_SHORT_START_CODE_LENGTH );
        info->prev_nalu_type = nalu_type;
        lsmash_stream_buffers_update( sb, 1 );
        no_more_buf = (lsmash_stream_buffers_get_remainder( sb ) == 0);
        ebsp_length = 0;
        no_more = lsmash_stream_buffers_is_eos( sb ) && no_more_buf;
        if( !no_more )
        {
            /* Check the next NALU header. */
            if( hevc_check_nalu_header( &nalu_header, sb, !!consecutive_zero_byte_count ) )
                return hevc_get_au_internal_failed( importer_info, au, &nalu_header, no_more_buf, complete_au );
            info->ebsp_head_pos = next_nalu_head_pos + nalu_header.length;
        }
        /* If there is no more data in the stream, and flushed chunk of NALUs, flush it as complete AU here. */
        else if( au->incomplete_length && au->length == 0 )
        {
            hevc_update_picture_info( info, picture, slice, &info->sps, &info->sei );
            hevc_complete_au( au, probe );
            return hevc_get_au_internal_succeeded( importer_info, au, &nalu_header, no_more_buf );
        }
        if( complete_au )
            return hevc_get_au_internal_succeeded( importer_info, au, &nalu_header, no_more_buf );
        consecutive_zero_byte_count = 0;
    }
}

static int hevc_importer_get_accessunit( importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    hevc_importer_info_t *importer_info = (hevc_importer_info_t *)importer->info;
    hevc_info_t *info = &importer_info->info;
    importer_status current_status = importer_info->status;
    if( current_status == IMPORTER_ERROR || buffered_sample->length < importer_info->max_au_length )
        return -1;
    if( current_status == IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( hevc_get_access_unit_internal( importer_info, 0 ) )
    {
        importer_info->status = IMPORTER_ERROR;
        return -1;
    }
    if( importer_info->status == IMPORTER_CHANGE && !info->hvcC_pending )
        current_status = IMPORTER_CHANGE;
    if( current_status == IMPORTER_CHANGE )
    {
        /* Update the active summary. */
        lsmash_codec_specific_t *cs = (lsmash_codec_specific_t *)lsmash_get_entry_data( importer_info->hvcC_list, ++ importer_info->hvcC_number );
        if( !cs )
            return -1;
        lsmash_hevc_specific_parameters_t *hvcC_param = (lsmash_hevc_specific_parameters_t *)cs->data.structured;
        lsmash_video_summary_t *summary = hevc_create_summary( hvcC_param, &info->sps, importer_info->max_au_length );
        if( !summary )
            return -1;
        lsmash_remove_entry( importer->summaries, track_number, lsmash_cleanup_summary );
        if( lsmash_add_entry( importer->summaries, summary ) )
        {
            lsmash_cleanup_summary( (lsmash_summary_t *)summary );
            return -1;
        }
        importer_info->status = IMPORTER_OK;
    }
    //hevc_sps_t *sps = &info->sps;
    hevc_access_unit_t  *au      = &info->au;
    hevc_picture_info_t *picture = &au->picture;
    buffered_sample->dts = importer_info->ts_list.timestamp[ au->number - 1 ].dts;
    buffered_sample->cts = importer_info->ts_list.timestamp[ au->number - 1 ].cts;
    /* Set property of disposability. */
    if( picture->sublayer_nonref && au->TemporalId == importer_info->max_TemporalId )
        /* Sub-layer non-reference pictures are not referenced by subsequent pictures of
         * the same sub-layer in decoding order. */
        buffered_sample->prop.disposable = ISOM_SAMPLE_IS_DISPOSABLE;
    else
        buffered_sample->prop.disposable = ISOM_SAMPLE_IS_NOT_DISPOSABLE;
    /* Set property of leading. */
    if( picture->radl || picture->rasl )
        buffered_sample->prop.leading = picture->radl ? ISOM_SAMPLE_IS_DECODABLE_LEADING : ISOM_SAMPLE_IS_UNDECODABLE_LEADING;
    else
    {
        if( au->number < importer_info->num_undecodable )
            buffered_sample->prop.leading = ISOM_SAMPLE_IS_UNDECODABLE_LEADING;
        else
        {
            if( picture->independent || buffered_sample->cts >= importer_info->last_intra_cts )
                buffered_sample->prop.leading = ISOM_SAMPLE_IS_NOT_LEADING;
            else
                buffered_sample->prop.leading = ISOM_SAMPLE_IS_UNDECODABLE_LEADING;
        }
    }
    if( picture->independent )
        importer_info->last_intra_cts = buffered_sample->cts;
    /* Set property of independence. */
    buffered_sample->prop.independent = picture->independent ? ISOM_SAMPLE_IS_INDEPENDENT : ISOM_SAMPLE_IS_NOT_INDEPENDENT;
    buffered_sample->prop.redundant   = ISOM_SAMPLE_HAS_NO_REDUNDANCY;
    buffered_sample->prop.post_roll.identifier = picture->poc;
    if( picture->random_accessible )
    {
        if( picture->irap )
        {
            buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            if( picture->closed_rap )
                buffered_sample->prop.ra_flags |= ISOM_SAMPLE_RANDOM_ACCESS_FLAG_CLOSED_RAP;
            else
                buffered_sample->prop.ra_flags |= ISOM_SAMPLE_RANDOM_ACCESS_FLAG_RAP;
        }
        else if( picture->recovery_poc_cnt )
        {
            buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_POST_ROLL_START;
            buffered_sample->prop.post_roll.complete = picture->poc + picture->recovery_poc_cnt;
        }
        else
            buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_RAP;
    }
    buffered_sample->length = au->length;
    memcpy( buffered_sample->data, au->data, au->length );
    return current_status;
}

static int hevc_importer_probe( importer_t *importer )
{
#define HEVC_LONG_START_CODE_LENGTH 4
#define HEVC_CHECK_NEXT_LONG_START_CODE( x ) (!(x)[0] && !(x)[1] && !(x)[2] && ((x)[3] == 0x01))
    hevc_importer_info_t *importer_info = create_hevc_importer_info( importer );
    if( !importer_info )
        return -1;
    hevc_info_t             *info = &importer_info->info;
    hevc_stream_buffer_t    *hb   = &info->buffer;
    lsmash_stream_buffers_t *sb   = hb->sb;     /* shall be equal to &importer->sb */
    /* Find the first start code. */
    lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
    lsmash_stream_buffers_read( sb, 0 );
    while( 1 )
    {
        /* The first NALU of an AU in decoding order shall have long start code (0x00000001). */
        if( HEVC_CHECK_NEXT_LONG_START_CODE( sb->pos ) )
            break;
        /* If the first trial of finding long start code failed, we assume this stream is not byte stream format of H.264. */
        if( lsmash_stream_buffers_get_remainder( sb ) == HEVC_LONG_START_CODE_LENGTH )
            goto fail;
        /* Invalid if encountered any value of non-zero before the first start code. */
        if( lsmash_stream_buffers_get_byte( sb ) )
            goto fail;
    }
    /* OK. It seems the stream has a long start code of H.264. */
    importer->info = importer_info;
    lsmash_stream_buffers_seek( sb, HEVC_LONG_START_CODE_LENGTH, SEEK_CUR );
    uint64_t first_ebsp_head_pos = lsmash_stream_buffers_get_offset( sb );
    lsmash_stream_buffers_update( sb, 0 );
    hevc_nalu_header_t first_nalu_header;
    if( hevc_check_nalu_header( &first_nalu_header, sb, 1 ) )
        goto fail;
    if( lsmash_stream_buffers_get_remainder( sb ) == 0 )
        goto fail;  /* It seems the stream ends at the first incomplete access unit. */
    first_ebsp_head_pos += first_nalu_header.length;    /* EBSP doesn't include NALU header. */
    importer_info->status = IMPORTER_OK;
    info->nalu_header    = first_nalu_header;
    info->ebsp_head_pos  = first_ebsp_head_pos;
    info->prev_nalu_type = HEVC_NALU_TYPE_UNKNOWN;
    /* Parse all NALU in the stream for preparation of calculating timestamps. */
    uint32_t npt_alloc = (1 << 12) * sizeof(nal_pic_timing_t);
    nal_pic_timing_t *npt = (nal_pic_timing_t *)lsmash_malloc( npt_alloc );
    if( !npt )
        goto fail;
    uint32_t picture_stats[HEVC_PICTURE_TYPE_NONE + 1] = { 0 };
    uint32_t num_access_units = 0;
    lsmash_log( importer, LSMASH_LOG_INFO, "Analyzing stream as HEVC\r" );
    while( importer_info->status != IMPORTER_EOF )
    {
#if 0
        lsmash_log( importer, LSMASH_LOG_INFO, "Analyzing stream as HEVC: %"PRIu32"\n", num_access_units + 1 );
#endif
        hevc_picture_info_t     *picture = &info->au.picture;
        hevc_picture_info_t prev_picture = *picture;
        picture->first = (num_access_units == 0);
        if( hevc_get_access_unit_internal( importer_info, 1 )
         || hevc_calculate_poc( info, &info->au.picture, &prev_picture ) )
        {
            lsmash_free( npt );
            goto fail;
        }
        if( npt_alloc <= num_access_units * sizeof(nal_pic_timing_t) )
        {
            uint32_t alloc = 2 * num_access_units * sizeof(nal_pic_timing_t);
            nal_pic_timing_t *temp = (nal_pic_timing_t *)lsmash_realloc( npt, alloc );
            if( !temp )
            {
                lsmash_free( npt );
                goto fail;
            }
            npt = temp;
            npt_alloc = alloc;
        }
        importer_info->field_pic_present |= picture->field_coded;
        npt[num_access_units].poc       = picture->poc;
        npt[num_access_units].delta     = picture->delta;
        npt[num_access_units].poc_delta = 1;
        npt[num_access_units].reset     = 0;
        ++num_access_units;
        importer_info->max_au_length  = LSMASH_MAX( importer_info->max_au_length,  info->au.length );
        importer_info->max_TemporalId = LSMASH_MAX( importer_info->max_TemporalId, info->au.TemporalId );
        if( picture->idr )
            ++picture_stats[HEVC_PICTURE_TYPE_IDR];
        else if( picture->type >= HEVC_PICTURE_TYPE_NONE )
            ++picture_stats[HEVC_PICTURE_TYPE_NONE];
        else
            ++picture_stats[ picture->type ];
    }
    fprintf( stderr, "                                                                               \r" );
    lsmash_log( importer, LSMASH_LOG_INFO,
                "IDR: %"PRIu32", I: %"PRIu32", P: %"PRIu32", B: %"PRIu32", Unknown: %"PRIu32"\n",
                picture_stats[HEVC_PICTURE_TYPE_IDR], picture_stats[HEVC_PICTURE_TYPE_I],
                picture_stats[HEVC_PICTURE_TYPE_I_P], picture_stats[HEVC_PICTURE_TYPE_I_P_B],
                picture_stats[HEVC_PICTURE_TYPE_NONE]);
    /* Copy and append the last Codec Specific info. */
    if( hevc_store_codec_specific( importer_info, &info->hvcC_param ) < 0 )
        return -1;
    /* Set up the first summary. */
    lsmash_codec_specific_t *cs = (lsmash_codec_specific_t *)lsmash_get_entry_data( importer_info->hvcC_list, ++ importer_info->hvcC_number );
    if( !cs || !cs->data.structured )
    {
        lsmash_free( npt );
        goto fail;
    }
    lsmash_hevc_specific_parameters_t *hvcC_param = (lsmash_hevc_specific_parameters_t *)cs->data.structured;
    lsmash_video_summary_t *summary = hevc_create_summary( hvcC_param, &info->sps, importer_info->max_au_length );
    if( !summary || lsmash_add_entry( importer->summaries, summary ) )
    {
        if( summary )
            lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_free( npt );
        goto fail;
    }
    summary->sample_per_field = importer_info->field_pic_present;
    /* */
    lsmash_media_ts_t *timestamp = lsmash_malloc( num_access_units * sizeof(lsmash_media_ts_t) );
    if( !timestamp )
    {
        lsmash_free( npt );
        goto fail;
    }
    /* Count leading samples that are undecodable. */
    for( uint32_t i = 0; i < num_access_units; i++ )
    {
        if( npt[i].poc == 0 )
            break;
        ++ importer_info->num_undecodable;
    }
    /* Deduplicate POCs. */
    uint32_t max_composition_delay = 0;
    nalu_deduplicate_poc( npt, &max_composition_delay, num_access_units, 15 );
    /* Generate timestamps. */
    nalu_generate_timestamps_from_poc( importer, timestamp, npt,
                                       &importer_info->composition_reordering_present,
                                       &importer_info->last_delta,
                                       max_composition_delay, num_access_units );
    summary->timescale *= 2;    /* We assume that picture timing is in field level.
                                 * For HEVC, it seems time_scale is set in frame level basically.
                                 * So multiply by 2 for reducing timebase and timescale. */
    nalu_reduce_timescale( timestamp, npt, &importer_info->last_delta, &summary->timescale, num_access_units );
    lsmash_free( npt );
    importer_info->ts_list.sample_count = num_access_units;
    importer_info->ts_list.timestamp    = timestamp;
    /* Go back to EBSP of the first NALU. */
    lsmash_fseek( sb->stream, first_ebsp_head_pos, SEEK_SET );
    importer_info->status       = IMPORTER_OK;
    info->nalu_header           = first_nalu_header;
    info->ebsp_head_pos         = first_ebsp_head_pos;
    info->prev_nalu_type        = HEVC_NALU_TYPE_UNKNOWN;
    sb->no_more_read            = 0;
    lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
    lsmash_stream_buffers_read( sb, 0 );
    uint8_t *temp_au            = info->au.data;
    uint8_t *temp_incomplete_au = info->au.incomplete_data;
    memset( &info->au, 0, sizeof(hevc_access_unit_t) );
    info->au.data            = temp_au;
    info->au.incomplete_data = temp_incomplete_au;
    memset( &info->slice, 0, sizeof(hevc_slice_info_t) );
    memset( &info->vps,   0, sizeof(hevc_vps_t) );
    memset( &info->sps,   0, sizeof(hevc_sps_t) );
    memset( &info->pps,   0, sizeof(hevc_pps_t) );
    for( int i = 0; i < HEVC_DCR_NALU_TYPE_NUM; i++ )
        lsmash_remove_entries( info->hvcC_param.parameter_arrays->ps_array[i].list, isom_remove_dcr_ps );
    lsmash_destroy_hevc_parameter_arrays( &info->hvcC_param_next );
    return 0;
fail:
    remove_hevc_importer_info( importer_info );
    importer->info = NULL;
    lsmash_remove_entries( importer->summaries, lsmash_cleanup_summary );
    return -1;
#undef HEVC_LONG_START_CODE_LENGTH
#undef HEVC_CHECK_NEXT_LONG_START_CODE
}

static uint32_t hevc_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    hevc_importer_info_t *info = (hevc_importer_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != IMPORTER_EOF )
        return 0;
    return info->ts_list.sample_count
         ? info->last_delta
         : UINT32_MAX;    /* arbitrary */
}

const static importer_functions hevc_importer =
{
    { "HEVC" },
    1,
    hevc_importer_probe,
    hevc_importer_get_accessunit,
    hevc_importer_get_last_delta,
    hevc_importer_cleanup
};

/***************************************************************************
    SMPTE VC-1 importer (only for Advanced Profile)
    SMPTE 421M-2006
    SMPTE RP 2025-2007
***************************************************************************/
#include "vc1.h"

typedef struct
{
    importer_status        status;
    vc1_info_t             info;
    vc1_sequence_header_t  first_sequence;
    lsmash_media_ts_list_t ts_list;
    uint8_t  composition_reordering_present;
    uint32_t max_au_length;
    uint32_t num_undecodable;
    uint64_t last_ref_intra_cts;
} vc1_importer_info_t;

static void remove_vc1_importer_info( vc1_importer_info_t *info )
{
    if( !info )
        return;
    vc1_cleanup_parser( &info->info );
    if( info->ts_list.timestamp )
        lsmash_free( info->ts_list.timestamp );
    lsmash_free( info );
}

static void vc1_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        remove_vc1_importer_info( importer->info );
}

static vc1_importer_info_t *create_vc1_importer_info( importer_t *importer )
{
    vc1_importer_info_t *info = lsmash_malloc_zero( sizeof(vc1_importer_info_t) );
    if( !info )
        return NULL;
    if( vc1_setup_parser( &info->info, &importer->sb, 0, LSMASH_STREAM_BUFFERS_TYPE_FILE, importer->stream ) )
    {
        remove_vc1_importer_info( info );
        return NULL;
    }
    return info;
}

static inline int vc1_complete_au( vc1_access_unit_t *access_unit, vc1_picture_info_t *picture, int probe )
{
    if( !picture->present )
        return 0;
    if( !probe )
        memcpy( access_unit->data, access_unit->incomplete_data, access_unit->incomplete_data_length );
    access_unit->data_length = access_unit->incomplete_data_length;
    access_unit->incomplete_data_length = 0;
    vc1_update_au_property( access_unit, picture );
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

static inline void vc1_get_au_internal_end( vc1_importer_info_t *info, vc1_access_unit_t *access_unit, uint8_t bdu_type, int no_more_buf )
{
    info->status = info->info.buffer.sb->no_more_read && no_more_buf && (access_unit->incomplete_data_length == 0)
                 ? IMPORTER_EOF
                 : IMPORTER_OK;
    info->info.bdu_type = bdu_type;
}

static int vc1_get_au_internal_succeeded( vc1_importer_info_t *info, vc1_access_unit_t *access_unit, uint8_t bdu_type, int no_more_buf )
{
    vc1_get_au_internal_end( info, access_unit, bdu_type, no_more_buf );
    access_unit->number += 1;
    return 0;
}

static int vc1_get_au_internal_failed( vc1_importer_info_t *info, vc1_access_unit_t *access_unit, uint8_t bdu_type, int no_more_buf, int complete_au )
{
    vc1_get_au_internal_end( info, access_unit, bdu_type, no_more_buf );
    if( complete_au )
        access_unit->number += 1;
    return -1;
}

static int vc1_importer_get_access_unit_internal( vc1_importer_info_t *importer_info, int probe )
{
    vc1_info_t              *info        = &importer_info->info;
    vc1_access_unit_t       *access_unit = &info->access_unit;
    vc1_stream_buffer_t     *hb          = &info->buffer;
    lsmash_stream_buffers_t *sb          = hb->sb;
    uint8_t  bdu_type = info->bdu_type;
    uint64_t consecutive_zero_byte_count = 0;
    uint64_t ebdu_length = 0;
    int      no_more_buf = 0;
    int      complete_au = 0;
    access_unit->data_length = 0;
    while( 1 )
    {
        lsmash_stream_buffers_update( sb, 2 );
        no_more_buf = (lsmash_stream_buffers_get_remainder( sb ) == 0);
        int no_more = lsmash_stream_buffers_is_eos( sb ) && no_more_buf;
        if( !vc1_check_next_start_code_prefix( sb->pos, sb->end ) && !no_more )
        {
            if( lsmash_stream_buffers_get_byte( sb ) )
                consecutive_zero_byte_count = 0;
            else
                ++consecutive_zero_byte_count;
            ++ebdu_length;
            continue;
        }
        if( no_more && ebdu_length == 0 )
        {
            /* For the last EBDU.
             * This EBDU already has been appended into the latest access unit and parsed. */
            vc1_complete_au( access_unit, &info->picture, probe );
            return vc1_get_au_internal_succeeded( importer_info, access_unit, bdu_type, no_more_buf );
        }
        ebdu_length += VC1_START_CODE_LENGTH;
        uint64_t next_scs_file_offset = info->ebdu_head_pos + ebdu_length + !no_more * VC1_START_CODE_PREFIX_LENGTH;
        /* Memorize position of beginning of the next EBDU in buffer.
         * This is used when backward reading of stream doesn't occur. */
        uint8_t *next_ebdu_pos = lsmash_stream_buffers_get_pos( sb );
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
            if( lsmash_stream_buffers_get_buffer_size( sb ) < possible_au_length )
            {
                if( vc1_supplement_buffer( hb, access_unit, 2 * possible_au_length ) )
                    return vc1_get_au_internal_failed( importer_info, access_unit, bdu_type, no_more_buf, complete_au );
                next_ebdu_pos = lsmash_stream_buffers_get_pos( sb );
            }
            /* Move to the first byte of the current EBDU. */
            read_back = (lsmash_stream_buffers_get_offset( sb ) < (ebdu_length + consecutive_zero_byte_count));
            if( read_back )
            {
                lsmash_fseek( sb->stream, info->ebdu_head_pos, SEEK_SET );
                lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
                lsmash_stream_buffers_read( sb, ebdu_length );
                if( lsmash_stream_buffers_get_valid_size( sb ) != ebdu_length )
                    return vc1_get_au_internal_failed( importer_info, access_unit, bdu_type, no_more_buf, complete_au );
#if 0
                if( probe )
                    fprintf( stderr, "    ----Read Back\n" );
#endif
            }
            else
                lsmash_stream_buffers_seek( sb, -(ebdu_length + consecutive_zero_byte_count), SEEK_CUR );
            /* Complete the current access unit if encountered delimiter of current access unit. */
            if( vc1_find_au_delimit_by_bdu_type( bdu_type, info->prev_bdu_type ) )
                /* The last video coded EBDU belongs to the access unit you want at this time. */
                complete_au = vc1_complete_au( access_unit, &info->picture, probe );
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
                    if( vc1_parse_advanced_picture( info->bits, &info->sequence, &info->picture, hb->rbdu,
                                                    lsmash_stream_buffers_get_pos( sb ), ebdu_length ) )
                        return vc1_get_au_internal_failed( importer_info, access_unit, bdu_type, no_more_buf, complete_au );
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
                    info->dvc1_param.slice_present = 1;
                    break;
                case 0x0E : /* Entry-point header
                             * Entry-point indicates the direct followed frame is a start of group of frames.
                             * Entry-point doesn't indicates the frame is a random access point when multiple sequence headers are present,
                             * since it is necessary to decode sequence header which subsequent frames belong to for decoding them.
                             * Entry point shall be followed by
                             *   1. I-picture - progressive or frame interlace
                             *   2. I/I-picture, I/P-picture, or P/I-picture - field interlace
                             * [[SEQ_SC][SEQ_L] (optional)][EP_SC][EP_L][FRM_SC][PIC_L] ... */
                    if( vc1_parse_entry_point_header( info, lsmash_stream_buffers_get_pos( sb ), ebdu_length, probe ) )
                        return vc1_get_au_internal_failed( importer_info, access_unit, bdu_type, no_more_buf, complete_au );
                    /* Signal random access type of the frame that follows this entry-point header. */
                    info->picture.closed_gop        = info->entry_point.closed_entry_point;
                    info->picture.random_accessible = info->dvc1_param.multiple_sequence ? info->picture.start_of_sequence : 1;
                    break;
                case 0x0F : /* Sequence header
                             * [SEQ_SC][SEQ_L][EP_SC][EP_L][FRM_SC][PIC_L] ... */
                    if( vc1_parse_sequence_header( info, lsmash_stream_buffers_get_pos( sb ), ebdu_length, probe ) )
                        return vc1_get_au_internal_failed( importer_info, access_unit, bdu_type, no_more_buf, complete_au );
                    /* The frame that is the first frame after this sequence header shall be a random accessible point. */
                    info->picture.start_of_sequence = 1;
                    if( probe && !importer_info->first_sequence.present )
                        importer_info->first_sequence = info->sequence;
                    break;
                default :   /* End-of-sequence (0x0A) */
                    break;
            }
            vc1_append_ebdu_to_au( access_unit, lsmash_stream_buffers_get_pos( sb ), ebdu_length, probe );
        }
        else    /* We don't support other BDU types such as user data yet. */
            return vc1_get_au_internal_failed( importer_info, access_unit, bdu_type, no_more_buf, complete_au );
        /* Move to the first byte of the next start code suffix. */
        if( read_back )
        {
            lsmash_fseek( sb->stream, next_scs_file_offset, SEEK_SET );
            lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
            lsmash_stream_buffers_read( sb, 0 );
        }
        else
            lsmash_stream_buffers_set_pos( sb, next_ebdu_pos + VC1_START_CODE_PREFIX_LENGTH );
        info->prev_bdu_type = bdu_type;
        lsmash_stream_buffers_update( sb, 0 );
        no_more_buf = (lsmash_stream_buffers_get_remainder( sb ) == 0);
        ebdu_length = 0;
        no_more = lsmash_stream_buffers_is_eos( sb ) && no_more_buf;
        if( !no_more )
        {
            /* Check the next BDU type. */
            if( vc1_check_next_start_code_suffix( &bdu_type, &sb->pos ) )
                return vc1_get_au_internal_failed( importer_info, access_unit, bdu_type, no_more_buf, complete_au );
            info->ebdu_head_pos = next_scs_file_offset - VC1_START_CODE_PREFIX_LENGTH;
        }
        /* If there is no more data in the stream, and flushed chunk of EBDUs, flush it as complete AU here. */
        else if( access_unit->incomplete_data_length && access_unit->data_length == 0 )
        {
            vc1_complete_au( access_unit, &info->picture, probe );
            return vc1_get_au_internal_succeeded( importer_info, access_unit, bdu_type, no_more_buf );
        }
        if( complete_au )
            return vc1_get_au_internal_succeeded( importer_info, access_unit, bdu_type, no_more_buf );
        consecutive_zero_byte_count = 0;
    }
}

static int vc1_importer_get_accessunit( importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    vc1_importer_info_t *importer_info = (vc1_importer_info_t *)importer->info;
    vc1_info_t *info = &importer_info->info;
    importer_status current_status = importer_info->status;
    if( current_status == IMPORTER_ERROR || buffered_sample->length < importer_info->max_au_length )
        return -1;
    if( current_status == IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( vc1_importer_get_access_unit_internal( importer_info, 0 ) )
    {
        importer_info->status = IMPORTER_ERROR;
        return -1;
    }
    vc1_access_unit_t *access_unit = &info->access_unit;
    buffered_sample->dts = importer_info->ts_list.timestamp[access_unit->number - 1].dts;
    buffered_sample->cts = importer_info->ts_list.timestamp[access_unit->number - 1].cts;
    buffered_sample->prop.leading = access_unit->independent || access_unit->non_bipredictive || buffered_sample->cts >= importer_info->last_ref_intra_cts
                                  ? ISOM_SAMPLE_IS_NOT_LEADING : ISOM_SAMPLE_IS_UNDECODABLE_LEADING;
    if( access_unit->independent && !access_unit->disposable )
        importer_info->last_ref_intra_cts = buffered_sample->cts;
    if( importer_info->composition_reordering_present && !access_unit->disposable && !access_unit->closed_gop )
        buffered_sample->prop.allow_earlier = QT_SAMPLE_EARLIER_PTS_ALLOWED;
    buffered_sample->prop.independent = access_unit->independent ? ISOM_SAMPLE_IS_INDEPENDENT : ISOM_SAMPLE_IS_NOT_INDEPENDENT;
    buffered_sample->prop.disposable  = access_unit->disposable  ? ISOM_SAMPLE_IS_DISPOSABLE  : ISOM_SAMPLE_IS_NOT_DISPOSABLE;
    buffered_sample->prop.redundant   = ISOM_SAMPLE_HAS_NO_REDUNDANCY;
    if( access_unit->random_accessible )
        /* All random access point is a sync sample even if it's an open RAP. */
        buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    buffered_sample->length = access_unit->data_length;
    memcpy( buffered_sample->data, access_unit->data, access_unit->data_length );
    return current_status;
}

static lsmash_video_summary_t *vc1_create_summary( vc1_info_t *info, vc1_sequence_header_t *sequence, uint32_t max_au_length )
{
    if( !info->sequence.present || !info->entry_point.present )
        return NULL;
    lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_VIDEO );
    if( !summary )
        return NULL;
    lsmash_codec_specific_t *specific = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_VC_1,
                                                                           LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
    specific->data.unstructured = lsmash_create_vc1_specific_info( &info->dvc1_param, &specific->size );
    if( !specific->data.unstructured
     || lsmash_add_entry( &summary->opaque->list, specific ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        return NULL;
    }
    summary->sample_type            = ISOM_CODEC_TYPE_VC_1_VIDEO;
    summary->max_au_length          = max_au_length;
    summary->timescale              = sequence->framerate_numerator;
    summary->timebase               = sequence->framerate_denominator;
    summary->vfr                    = !sequence->framerate_flag;
    summary->sample_per_field       = 0;
    summary->width                  = sequence->disp_horiz_size;
    summary->height                 = sequence->disp_vert_size;
    summary->par_h                  = sequence->aspect_width;
    summary->par_v                  = sequence->aspect_height;
    summary->color.primaries_index  = sequence->color_prim;
    summary->color.transfer_index   = sequence->transfer_char;
    summary->color.matrix_index     = sequence->matrix_coef;
    return summary;
}

static int vc1_importer_probe( importer_t *importer )
{
#define VC1_CHECK_FIRST_START_CODE( x ) (!(x)[0] && !(x)[1] && ((x)[2] == 0x01))
    /* Find the first start code. */
    vc1_importer_info_t *importer_info = create_vc1_importer_info( importer );
    if( !importer_info )
        return -1;
    vc1_info_t              *info = &importer_info->info;
    vc1_stream_buffer_t     *hb   = &info->buffer;
    lsmash_stream_buffers_t *sb   = hb->sb;     /* shall be equal to &importer->sb */
    lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
    lsmash_stream_buffers_read( sb, 0 );
    while( 1 )
    {
        /* The first EBDU in decoding order of the stream shall have start code (0x000001). */
        if( VC1_CHECK_FIRST_START_CODE( sb->pos ) )
            break;
        /* If the first trial of finding start code of sequence header failed, we assume this stream is not byte stream format of VC-1. */
        if( lsmash_stream_buffers_get_remainder( sb ) == VC1_START_CODE_LENGTH )
            goto fail;
        /* Invalid if encountered any value of non-zero before the first start code. */
        if( lsmash_stream_buffers_get_byte( sb ) )
            goto fail;
    }
    /* OK. It seems the stream has a sequence header of VC-1. */
    importer->info = importer_info;
    uint64_t first_ebdu_head_pos = lsmash_stream_buffers_get_offset( sb );
    lsmash_stream_buffers_seek( sb, VC1_START_CODE_PREFIX_LENGTH, SEEK_CUR );
    lsmash_stream_buffers_update( sb, 0 );
    uint8_t first_bdu_type = lsmash_stream_buffers_get_byte( sb );
    if( lsmash_stream_buffers_get_remainder( sb ) == 0 )
        goto fail;  /* It seems the stream ends at the first incomplete access unit. */
    importer_info->status = IMPORTER_OK;
    info->bdu_type        = first_bdu_type;
    info->ebdu_head_pos   = first_ebdu_head_pos;
    /* Parse all EBDU in the stream for preparation of calculating timestamps. */
    uint32_t cts_alloc = (1 << 12) * sizeof(uint64_t);
    uint64_t *cts = lsmash_malloc( cts_alloc );
    if( !cts )
        goto fail;
    uint32_t num_access_units = 0;
    uint32_t num_consecutive_b = 0;
    lsmash_log( importer, LSMASH_LOG_INFO, "Analyzing stream as VC-1\r" );
    while( importer_info->status != IMPORTER_EOF )
    {
#if 0
        lsmash_log( importer, LSMASH_LOG_INFO, "Analyzing stream as VC-1: %"PRIu32"\n", num_access_units + 1 );
#endif
        if( vc1_importer_get_access_unit_internal( importer_info, 1 ) )
        {
            lsmash_free( cts );
            goto fail;
        }
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
            info->dvc1_param.bframe_present = 1;
        }
        if( cts_alloc <= num_access_units * sizeof(uint64_t) )
        {
            uint32_t alloc = 2 * num_access_units * sizeof(uint64_t);
            uint64_t *temp = lsmash_realloc( cts, alloc );
            if( !temp )
            {
                lsmash_free( cts );
                goto fail;
            }
            cts = temp;
            cts_alloc = alloc;
        }
        importer_info->max_au_length = LSMASH_MAX( info->access_unit.data_length, importer_info->max_au_length );
        ++num_access_units;
    }
    if( num_access_units > num_consecutive_b )
        cts[ num_access_units - num_consecutive_b - 1 ] = num_access_units;
    else
    {
        lsmash_free( cts );
        goto fail;
    }
    fprintf( stderr, "                                                                               \r" );
    /* Construct timestamps. */
    lsmash_media_ts_t *timestamp = lsmash_malloc( num_access_units * sizeof(lsmash_media_ts_t) );
    if( !timestamp )
    {
        lsmash_free( cts );
        goto fail;
    }
    for( uint32_t i = 1; i < num_access_units; i++ )
        if( cts[i] < cts[i - 1] )
        {
            importer_info->composition_reordering_present = 1;
            break;
        }
    if( importer_info->composition_reordering_present )
        for( uint32_t i = 0; i < num_access_units; i++ )
        {
            timestamp[i].cts = cts[i];
            timestamp[i].dts = i;
        }
    else
        for( uint32_t i = 0; i < num_access_units; i++ )
            timestamp[i].cts = timestamp[i].dts = i;
    lsmash_free( cts );
#if 0
    for( uint32_t i = 0; i < num_access_units; i++ )
        fprintf( stderr, "Timestamp[%"PRIu32"]: DTS=%"PRIu64", CTS=%"PRIu64"\n", i, timestamp[i].dts, timestamp[i].cts );
#endif
    lsmash_video_summary_t *summary = vc1_create_summary( info, &importer_info->first_sequence, importer_info->max_au_length );
    if( !summary || lsmash_add_entry( importer->summaries, summary ) )
    {
        lsmash_free( timestamp );
        goto fail;
    }
    importer_info->ts_list.sample_count = num_access_units;
    importer_info->ts_list.timestamp    = timestamp;
    /* Go back to layer of the first EBDU. */
    lsmash_fseek( sb->stream, first_ebdu_head_pos, SEEK_SET );
    importer_info->status                = IMPORTER_OK;
    info->bdu_type                       = first_bdu_type;
    info->prev_bdu_type                  = 0;
    sb->no_more_read                     = 0;
    lsmash_stream_buffers_seek( sb, 0, SEEK_SET );
    lsmash_stream_buffers_read( sb, 0 );
    lsmash_stream_buffers_seek( sb, VC1_START_CODE_LENGTH, SEEK_SET );
    info->ebdu_head_pos                  = first_ebdu_head_pos;
    uint8_t *temp_access_unit            = info->access_unit.data;
    uint8_t *temp_incomplete_access_unit = info->access_unit.incomplete_data;
    memset( &info->access_unit, 0, sizeof(vc1_access_unit_t) );
    info->access_unit.data               = temp_access_unit;
    info->access_unit.incomplete_data    = temp_incomplete_access_unit;
    memset( &info->picture, 0, sizeof(vc1_picture_info_t) );
    return 0;
fail:
    remove_vc1_importer_info( importer_info );
    importer->info = NULL;
    lsmash_remove_entries( importer->summaries, lsmash_cleanup_summary );
    return -1;
#undef VC1_CHECK_FIRST_START_CODE
}

static uint32_t vc1_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    vc1_importer_info_t *info = (vc1_importer_info_t *)importer->info;
    if( !info || track_number != 1 || info->status != IMPORTER_EOF )
        return 0;
    return info->ts_list.sample_count
         ? 1
         : UINT32_MAX;    /* arbitrary */
}

const static importer_functions vc1_importer =
{
    { "VC-1" },
    1,
    vc1_importer_probe,
    vc1_importer_get_accessunit,
    vc1_importer_get_last_delta,
    vc1_importer_cleanup
};

/***************************************************************************
    importer public interfaces
***************************************************************************/


/******** importer listing table ********/
const static importer_functions *importer_func_table[] = {
    &mp4sys_adts_importer,
    &mp4sys_mp3_importer,
    &amr_importer,
    &ac3_importer,
    &eac3_importer,
    &mp4sys_als_importer,
    &dts_importer,
    &h264_importer,
    &hevc_importer,
    &vc1_importer,
    NULL,
};

/******** importer public functions ********/

void lsmash_importer_close( importer_t *importer )
{
    if( !importer )
        return;
    if( !importer->is_stdin && importer->stream )
        fclose( importer->stream );
    if( importer->funcs.cleanup )
        importer->funcs.cleanup( importer );
    lsmash_remove_list( importer->summaries, lsmash_cleanup_summary );
    lsmash_free( importer );
}

importer_t *lsmash_importer_open( const char *identifier, const char *format )
{
    if( identifier == NULL )
        return NULL;
    int auto_detect = ( format == NULL || !strcmp( format, "auto" ) );
    importer_t *importer = (importer_t *)lsmash_malloc_zero( sizeof(importer_t) );
    if( !importer )
        return NULL;
    if( !strcmp( identifier, "-" ) )
    {
        /* special treatment for stdin */
        if( auto_detect )
        {
            lsmash_free( importer );
            return NULL;
        }
        importer->stream = stdin;
        importer->is_stdin = 1;
    }
    else if( (importer->stream = lsmash_fopen( identifier, "rb" )) == NULL )
    {
        lsmash_importer_close( importer );
        return NULL;
    }
    importer->summaries = lsmash_create_entry_list();
    if( !importer->summaries )
    {
        lsmash_importer_close( importer );
        return NULL;
    }
    /* find importer */
    const importer_functions *funcs;
    if( auto_detect )
    {
        /* just rely on detector. */
        for( int i = 0; (funcs = importer_func_table[i]) != NULL; i++ )
        {
            importer->class = &funcs->class;
            if( !funcs->detectable )
                continue;
            if( !funcs->probe( importer ) || lsmash_fseek( importer->stream, 0, SEEK_SET ) )
                break;
        }
    }
    else
    {
        /* needs name matching. */
        for( int i = 0; (funcs = importer_func_table[i]) != NULL; i++ )
        {
            importer->class = &funcs->class;
            if( strcmp( importer->class->name, format ) )
                continue;
            if( funcs->probe( importer ) )
                funcs = NULL;
            break;
        }
    }
    if( !funcs )
    {
        lsmash_importer_close( importer );
        return NULL;
    }
    importer->funcs = *funcs;
    return importer;
}

/* 0 if success, positive if changed, negative if failed */
int lsmash_importer_get_access_unit( importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    if( !importer || !importer->funcs.get_accessunit || !buffered_sample->data || buffered_sample->length == 0 )
        return -1;
    return importer->funcs.get_accessunit( importer, track_number, buffered_sample );
}

/* Return 0 if failed, otherwise succeeded. */
uint32_t lsmash_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    if( !importer || !importer->funcs.get_last_delta )
        return 0;
    return importer->funcs.get_last_delta( importer, track_number );
}

uint32_t lsmash_importer_get_track_count( importer_t *importer )
{
    if( !importer || !importer->summaries )
        return 0;
    return importer->summaries->entry_count;
}

lsmash_summary_t *lsmash_duplicate_summary( importer_t *importer, uint32_t track_number )
{
    if( !importer )
        return NULL;
    lsmash_summary_t *src_summary = lsmash_get_entry_data( importer->summaries, track_number );
    if( !src_summary )
        return NULL;
    lsmash_summary_t *summary = lsmash_create_summary( src_summary->summary_type );
    if( !summary )
        return NULL;
    lsmash_codec_specific_list_t *opaque = summary->opaque;
    switch( src_summary->summary_type )
    {
        case LSMASH_SUMMARY_TYPE_VIDEO :
            *(lsmash_video_summary_t *)summary = *(lsmash_video_summary_t *)src_summary;
            break;
        case LSMASH_SUMMARY_TYPE_AUDIO :
            *(lsmash_audio_summary_t *)summary = *(lsmash_audio_summary_t *)src_summary;
            break;
        default :
            lsmash_cleanup_summary( summary );
            return NULL;
    }
    summary->opaque = opaque;
    for( lsmash_entry_t *entry = src_summary->opaque->list.head; entry; entry = entry->next )
    {
        lsmash_codec_specific_t *src_specific = (lsmash_codec_specific_t *)entry->data;
        if( !src_specific )
            continue;
        lsmash_codec_specific_t *dup = isom_duplicate_codec_specific_data( src_specific );
        if( lsmash_add_entry( &summary->opaque->list, dup ) )
        {
            lsmash_cleanup_summary( (lsmash_summary_t *)summary );
            lsmash_destroy_codec_specific_data( dup );
            return NULL;
        }
    }
    return summary;
}
