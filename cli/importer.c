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

#include "common/internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

#define LSMASH_IMPORTER_INTERNAL
#include "importer.h"

#include "core/box.h"

#include "codecs/mp4a.h"
#include "codecs/description.h"

/***************************************************************************
    importer framework
***************************************************************************/
struct importer_tag;

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
    lsmash_log_level        log_level;
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

static const lsmash_class_t lsmash_importer_class =
{
    "importer",
    offsetof( importer_t, log_level )
};

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
            raw_data_block_position[i] = LSMASH_GET_BE16( buf2 );
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

static const importer_functions mp4sys_adts_importer =
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
    uint32_t data = LSMASH_GET_BE32( buf );
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
    uint32_t flags = LSMASH_GET_BE32( &mdp[4] );
    uint32_t off = 8;
    uint32_t frame_count = 0;
    if( flags & 1 )
    {
        frame_count = LSMASH_GET_BE32( &mdp[8] );
        info->valid_samples = (uint64_t)frame_count * mp4sys_mp3_samples_in_frame( header );
        off += 4;
    }
    if( flags & 2 ) off +=   4; /* file size    */
    if( flags & 4 ) off += 100; /* TOC          */
    if( flags & 8 ) off +=   4; /* VBR quality  */

    if( mdp[off] == 'L' )
    {   /* LAME header present */
        unsigned v = LSMASH_GET_BE24( &mdp[off + 21] );
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

static const importer_functions mp4sys_mp3_importer =
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
    3GPP TS 26.101 V11.0.0 (2012-9)
    3GPP TS 26.201 V11.0.0 (2012-9)
    3GPP TS 26.244 V12.3.0 (2014-03)
    http://www.ietf.org/rfc/rfc3267.txt (Obsoleted)
    http://www.ietf.org/rfc/rfc4867.txt
***************************************************************************/
typedef struct
{
    importer_status status;
    lsmash_bs_t    *bs;
    int             wb; /* 0: AMR-NB, 1: AMR-WB */
    uint32_t        samples_in_frame;
    uint32_t        au_number;
} amr_importer_t;

static void remove_amr_importer
(
    amr_importer_t *amr_imp
)
{
    lsmash_bs_cleanup( amr_imp->bs );
    lsmash_free( amr_imp );
}

static amr_importer_t *create_amr_importer
(
    importer_t *importer
)
{
    amr_importer_t *amr_imp = (amr_importer_t *)lsmash_malloc_zero( sizeof(amr_importer_t) );
    if( !amr_imp )
        return NULL;
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
    {
        lsmash_free( amr_imp );
        return NULL;
    }
    amr_imp->bs = bs;
    bs->stream          = importer->stream;
    bs->read            = lsmash_fread_wrapper;
    bs->seek            = lsmash_fseek_wrapper;
    bs->unseekable      = importer->is_stdin;
    bs->buffer.max_size = BS_MAX_DEFAULT_READ_SIZE;
    return amr_imp;
}

static void amr_cleanup
(
    importer_t *importer
)
{
    debug_if( importer && importer->info )
        remove_amr_importer( importer->info );
}

static int amr_get_accessunit
(
    importer_t      *importer,
    uint32_t         track_number,
    lsmash_sample_t *buffered_sample
)
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( track_number != 1 )
        return -1;
    amr_importer_t *amr_imp = (amr_importer_t *)importer->info;
    lsmash_bs_t    *bs      = amr_imp->bs;
    if( amr_imp->status == IMPORTER_EOF || lsmash_bs_is_end( bs, 0 ) )
    {
        /* EOF */
        amr_imp->status = IMPORTER_EOF;
        buffered_sample->length = 0;
        return 0;
    }
    /* Each speech frame consists of one speech frame header and one speech data.
     * At the end of each speech data, octet alignment if needed.
     *   Speech frame header
     *      0 1 2 3 4 5 6 7
     *     +-+-------+-+-+-+
     *     |P|  FT   |Q|P|P|
     *     +-+-------+-+-+-+
     *    FT: Frame type index
     *    Q : Frame quality indicator
     *    P : Must be set to 0
     * FT= 9, 10 and 11 for AMR-NB shall not be used in the file format.
     * FT=12, 13 and 14 for AMR-NB are not defined yet in the file format.
     * FT=10, 11, 12 and 13 for AMR-WB are not defined yet in the file format.
     * FT determines the size of the speech frame starting with it.
     */
    uint8_t FT = (lsmash_bs_show_byte( bs, 0 ) >> 3) & 0x0F;
    const int frame_size[2][16] =
    {
        { 13, 14, 16, 18, 20, 21, 27, 32,  6, -1, -1, -1, 0, 0, 0, 1 },
        { 18, 24, 33, 37, 41, 47, 51, 59, 61,  6,  0,  0, 0, 0, 1, 1 }
    };
    int read_size = frame_size[ amr_imp->wb ][FT];
    if( read_size <= 0 )
    {
        lsmash_log( importer, LSMASH_LOG_ERROR, "an %s speech frame is detected.\n", read_size < 0 ? "invalid" : "unknown" );
        amr_imp->status = IMPORTER_ERROR;
        return -1;
    }
    if( buffered_sample->length < read_size )
        return -1;
    if( lsmash_bs_get_bytes_ex( bs, read_size, buffered_sample->data ) != read_size )
    {
        lsmash_log( importer, LSMASH_LOG_WARNING, "the stream is truncated at the end.\n" );
        amr_imp->status = IMPORTER_EOF;
        return -1;
    }
    buffered_sample->length        = read_size;
    buffered_sample->dts           = amr_imp->au_number ++ * amr_imp->samples_in_frame;
    buffered_sample->cts           = buffered_sample->dts;
    buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    return 0;
}

static int amr_check_magic_number
(
    lsmash_bs_t *bs
)
{
#define AMR_STORAGE_MAGIC_LENGTH  6
#define AMR_AMRWB_EX_MAGIC_LENGTH 3
    /* Check the magic number for single-channel AMR-NB/AMR-WB files.
     *   For AMR-NB, "#!AMR\n" (or 0x2321414d520a in hexadecimal).
     *   For AMR-WB, "#!AMR-WB\n" (or 0x2321414d522d57420a in hexadecimal).
     * Note that AMR-NB and AMR-WB data is stored in the 3GPP/3GPP2 file format according to
     * the AMR-NB and AMR-WB storage format for single channel header without the AMR magic numbers. */
    uint8_t buf[AMR_STORAGE_MAGIC_LENGTH];
    if( lsmash_bs_get_bytes_ex( bs, AMR_STORAGE_MAGIC_LENGTH, buf ) != AMR_STORAGE_MAGIC_LENGTH
     || memcmp( buf, "#!AMR", AMR_STORAGE_MAGIC_LENGTH - 1 ) )
        return -1;
    if( buf[AMR_STORAGE_MAGIC_LENGTH - 1] == '\n' )
        /* single-channel AMR-NB file */
        return 0;
    if( buf[AMR_STORAGE_MAGIC_LENGTH - 1] != '-'
     || lsmash_bs_get_bytes_ex( bs, AMR_AMRWB_EX_MAGIC_LENGTH, buf ) != AMR_AMRWB_EX_MAGIC_LENGTH
     || memcmp( buf, "WB\n", AMR_AMRWB_EX_MAGIC_LENGTH ) )
        return -1;
    /* single-channel AMR-WB file */
    return 1;
#undef AMR_STORAGE_MAGIC_LENGTH
#undef AMR_AMRWB_EX_MAGIC_LENGTH
}

static int amr_create_damr
(
    lsmash_audio_summary_t *summary,
    int                     wb
)
{
#define AMR_DAMR_LENGTH 17
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
        return -1;
    lsmash_bs_put_be32( bs, AMR_DAMR_LENGTH );
    lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_DAMR.fourcc );
    /* NOTE: These are specific to each codec vendor, but we're surely not a vendor.
     *       Using dummy data. */
    lsmash_bs_put_be32( bs, 0x20202020 );           /* vendor */
    lsmash_bs_put_byte( bs, 0 );                    /* decoder_version */
    /* NOTE: Using safe value for these settings, maybe sub-optimal. */
    lsmash_bs_put_be16( bs, wb ? 0xC3FF : 0x81FF ); /* mode_set, represents for all possibly existing and supported frame-types. */
    lsmash_bs_put_byte( bs, 1 );                    /* mode_change_period */
    lsmash_bs_put_byte( bs, 1 );                    /* frames_per_sample */
    lsmash_codec_specific_t *cs = lsmash_malloc_zero( sizeof(lsmash_codec_specific_t) );
    if( !cs )
    {
        lsmash_bs_cleanup( bs );
        return -1;
    }
    cs->type              = LSMASH_CODEC_SPECIFIC_DATA_TYPE_UNKNOWN;
    cs->format            = LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED;
    cs->destruct          = (lsmash_codec_specific_destructor_t)lsmash_free;
    cs->data.unstructured = lsmash_bs_export_data( bs, &cs->size );
    cs->size              = AMR_DAMR_LENGTH;
    lsmash_bs_cleanup( bs );
    if( !cs->data.unstructured
     || lsmash_add_entry( &summary->opaque->list, cs ) < 0 )
    {
        lsmash_destroy_codec_specific_data( cs );
        return -1;
    }
    return 0;
#undef AMR_DAMR_LENGTH
}

static lsmash_audio_summary_t *amr_create_summary
(
    importer_t *importer,
    int         wb
)
{
    /* Establish an audio summary for AMR-NB or AMR-WB stream. */
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
    if( !summary )
        return NULL;
    summary->sample_type      = wb ? ISOM_CODEC_TYPE_SAWB_AUDIO : ISOM_CODEC_TYPE_SAMR_AUDIO;
    summary->max_au_length    = wb ? 61 : 32;
    summary->aot              = MP4A_AUDIO_OBJECT_TYPE_NULL;    /* no effect */
    summary->frequency        = (8000 << wb);
    summary->channels         = 1;                              /* always single channel */
    summary->sample_size      = 16;
    summary->samples_in_frame = (160 << wb);
    summary->sbr_mode         = MP4A_AAC_SBR_NOT_SPECIFIED;     /* no effect */
    if( amr_create_damr( summary, wb ) < 0
     || lsmash_add_entry( importer->summaries, summary ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
    return summary;
}

static int amr_probe
(
    importer_t *importer
)
{

    amr_importer_t *amr_imp = create_amr_importer( importer );
    if( !amr_imp )
        return -1;
    int wb = amr_check_magic_number( amr_imp->bs );
    if( wb < 0 )
        goto fail;
    lsmash_audio_summary_t *summary = amr_create_summary( importer, wb );
    if( !summary )
        goto fail;
    amr_imp->status           = IMPORTER_OK;
    amr_imp->wb               = wb;
    amr_imp->samples_in_frame = summary->samples_in_frame;
    amr_imp->au_number        = 0;
    importer->info = amr_imp;
    return 0;
fail:
    remove_amr_importer( amr_imp );
    return -1;
}

static uint32_t amr_get_last_delta
(
    importer_t *importer,
    uint32_t    track_number
)
{
    debug_if( !importer || !importer->info )
        return 0;
    amr_importer_t *amr_imp = (amr_importer_t *)importer->info;
    if( !amr_imp || track_number != 1 )
        return 0;
    return amr_imp->samples_in_frame;
}

static const importer_functions amr_importer =
{
    { "AMR", offsetof( importer_t, log_level ) },
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
#include "codecs/a52.h"

#define AC3_SAMPLE_DURATION 1536    /* 256 (samples per audio block) * 6 (audio blocks) */

typedef struct
{
    importer_status status;
    ac3_info_t      info;
    uint64_t        next_frame_pos;
    uint8_t        *next_dac3;
    uint8_t         buffer[AC3_MAX_SYNCFRAME_LENGTH];
    uint32_t        au_number;
} ac3_importer_t;

static void remove_ac3_importer( ac3_importer_t *ac3_imp )
{
    if( !ac3_imp )
        return;
    lsmash_bits_adhoc_cleanup( ac3_imp->info.bits );
    lsmash_free( ac3_imp );
}

static ac3_importer_t *create_ac3_importer( void )
{
    ac3_importer_t *ac3_imp = (ac3_importer_t *)lsmash_malloc_zero( sizeof(ac3_importer_t) );
    if( !ac3_imp )
        return NULL;
    ac3_imp->info.bits = lsmash_bits_adhoc_create();
    if( !ac3_imp->info.bits )
    {
        lsmash_free( ac3_imp );
        return NULL;
    }
    return ac3_imp;
}

static void ac3_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        remove_ac3_importer( importer->info );
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

static lsmash_audio_summary_t *ac3_create_summary( ac3_info_t *info )
{
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
    if( !summary )
        return NULL;
    lsmash_ac3_specific_parameters_t *param = &info->dac3_param;
    lsmash_codec_specific_t *cs = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_AC_3,
                                                                     LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
    cs->data.unstructured = lsmash_create_ac3_specific_info( &info->dac3_param, &cs->size );
    if( !cs->data.unstructured
     || lsmash_add_entry( &summary->opaque->list, cs ) )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( cs );
        return NULL;
    }
    summary->sample_type      = ISOM_CODEC_TYPE_AC_3_AUDIO;
    summary->max_au_length    = AC3_MAX_SYNCFRAME_LENGTH;
    summary->aot              = MP4A_AUDIO_OBJECT_TYPE_NULL;    /* no effect */
    summary->frequency        = ac3_get_sample_rate( param );
    summary->channels         = ac3_get_channel_count( param );
    summary->sample_size      = 16;                             /* no effect */
    summary->samples_in_frame = AC3_SAMPLE_DURATION;
    summary->sbr_mode         = MP4A_AAC_SBR_NOT_SPECIFIED;     /* no effect */
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

static int ac3_buffer_frame( uint8_t *buffer, lsmash_bs_t *bs )
{
    uint64_t remain_size = lsmash_bs_get_remaining_buffer_size( bs );
    if( remain_size < AC3_MAX_SYNCFRAME_LENGTH )
    {
        if( lsmash_bs_read( bs, bs->buffer.max_size ) < 0 )
            return -1;
        remain_size = lsmash_bs_get_remaining_buffer_size( bs );
    }
    uint64_t copy_size = LSMASH_MIN( remain_size, AC3_MAX_SYNCFRAME_LENGTH );
    memcpy( buffer, lsmash_bs_get_buffer_data( bs ), copy_size );
    return 0;
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
    ac3_importer_t *ac3_imp = (ac3_importer_t *)importer->info;
    ac3_info_t     *info    = &ac3_imp->info;
    importer_status current_status = ac3_imp->status;
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
        lsmash_codec_specific_t *cs = isom_get_codec_specific( summary->opaque, LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_AC_3 );
        if( cs )
        {
            cs->destruct( cs->data.unstructured );
            cs->data.unstructured = ac3_imp->next_dac3;
        }
        summary->frequency  = ac3_get_sample_rate( param );
        summary->channels   = ac3_get_channel_count( param );
        //summary->layout_tag = ac3_channel_layout_table[ param->acmod ][ param->lfeon ];
    }
    memcpy( buffered_sample->data, ac3_imp->buffer, frame_size );
    buffered_sample->length                 = frame_size;
    buffered_sample->dts                    = ac3_imp->au_number++ * summary->samples_in_frame;
    buffered_sample->cts                    = buffered_sample->dts;
    buffered_sample->prop.ra_flags          = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    buffered_sample->prop.pre_roll.distance = 1;    /* MDCT */
    lsmash_bs_t *bs = info->bits->bs;
    ac3_imp->next_frame_pos += frame_size;
    lsmash_bs_read_seek( bs, ac3_imp->next_frame_pos, SEEK_SET );
    uint8_t syncword[2] =
    {
        lsmash_bs_show_byte( bs, 0 ),
        lsmash_bs_show_byte( bs, 1 )
    };
    if( bs->eob || (bs->eof && 0 == lsmash_bs_get_remaining_buffer_size( bs )) )
        ac3_imp->status = IMPORTER_EOF;
    else
    {
        /* Parse the next syncframe header. */
        if( syncword[0] != 0x0b
         || syncword[1] != 0x77
         || ac3_buffer_frame( ac3_imp->buffer, bs ) < 0 )
        {
            ac3_imp->status = IMPORTER_ERROR;
            return current_status;
        }
        lsmash_ac3_specific_parameters_t current_param = info->dac3_param;
        ac3_parse_syncframe_header( info );
        if( ac3_compare_specific_param( &current_param, &info->dac3_param ) )
        {
            uint32_t dummy;
            uint8_t *dac3 = lsmash_create_ac3_specific_info( &info->dac3_param, &dummy );
            if( !dac3 )
            {
                ac3_imp->status = IMPORTER_ERROR;
                return current_status;
            }
            ac3_imp->status    = IMPORTER_CHANGE;
            ac3_imp->next_dac3 = dac3;
        }
        else
            ac3_imp->status = IMPORTER_OK;
    }
    return current_status;
}

static int ac3_importer_probe( importer_t *importer )
{
    ac3_importer_t *ac3_imp = create_ac3_importer();
    if( !ac3_imp )
        return -1;
    lsmash_bits_t *bits = ac3_imp->info.bits;
    lsmash_bs_t   *bs   = bits->bs;
    bs->stream          = importer->stream;
    bs->read            = lsmash_fread_wrapper;
    bs->seek            = lsmash_fseek_wrapper;
    bs->unseekable      = importer->is_stdin;
    bs->buffer.max_size = AC3_MAX_SYNCFRAME_LENGTH;
    /* Check the syncword and parse the syncframe header */
    if( lsmash_bs_show_byte( bs, 0 ) != 0x0b
     || lsmash_bs_show_byte( bs, 1 ) != 0x77
     || ac3_buffer_frame( ac3_imp->buffer, bs ) < 0
     || ac3_parse_syncframe_header( &ac3_imp->info ) < 0 )
        goto fail;
    lsmash_audio_summary_t *summary = ac3_create_summary( &ac3_imp->info );
    if( !summary )
        goto fail;
    if( lsmash_add_entry( importer->summaries, summary ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        goto fail;
    }
    ac3_imp->status    = IMPORTER_OK;
    ac3_imp->au_number = 0;
    importer->info = ac3_imp;
    return 0;
fail:
    remove_ac3_importer( ac3_imp );
    return -1;
}

static uint32_t ac3_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    ac3_importer_t *ac3_imp = (ac3_importer_t *)importer->info;
    if( !ac3_imp || track_number != 1 || ac3_imp->status != IMPORTER_EOF )
        return 0;
    return AC3_SAMPLE_DURATION;
}

static const importer_functions ac3_importer =
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
    uint64_t next_frame_pos;
    uint32_t next_dec3_length;
    uint8_t *next_dec3;
    uint8_t  current_fscod2;
    uint8_t  buffer[EAC3_MAX_SYNCFRAME_LENGTH];
    lsmash_multiple_buffers_t *au_buffers;
    uint8_t *au;
    uint8_t *incomplete_au;
    uint32_t au_length;
    uint32_t incomplete_au_length;
    uint32_t au_number;
    uint32_t syncframe_count_in_au;
} eac3_importer_t;

static void remove_eac3_importer( eac3_importer_t *eac3_imp )
{
    if( !eac3_imp )
        return;
    lsmash_destroy_multiple_buffers( eac3_imp->au_buffers );
    lsmash_bits_adhoc_cleanup( eac3_imp->info.bits );
    lsmash_free( eac3_imp );
}

static eac3_importer_t *create_eac3_importer( void )
{
    eac3_importer_t *eac3_imp = (eac3_importer_t *)lsmash_malloc_zero( sizeof(eac3_importer_t) );
    if( !eac3_imp )
        return NULL;
    eac3_info_t *info = &eac3_imp->info;
    info->bits = lsmash_bits_adhoc_create();
    if( !info->bits )
    {
        lsmash_free( eac3_imp );
        return NULL;
    }
    eac3_imp->au_buffers = lsmash_create_multiple_buffers( 2, EAC3_MAX_SYNCFRAME_LENGTH );
    if( !eac3_imp->au_buffers )
    {
        lsmash_bits_adhoc_cleanup( info->bits );
        lsmash_free( eac3_imp );
        return NULL;
    }
    eac3_imp->au            = lsmash_withdraw_buffer( eac3_imp->au_buffers, 1 );
    eac3_imp->incomplete_au = lsmash_withdraw_buffer( eac3_imp->au_buffers, 2 );
    return eac3_imp;
}

static void eac3_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        remove_eac3_importer( importer->info );
}

static int eac3_importer_get_next_accessunit_internal( importer_t *importer )
{
    int au_completed = 0;
    eac3_importer_t *eac3_imp = (eac3_importer_t *)importer->info;
    eac3_info_t     *info     = &eac3_imp->info;
    lsmash_bs_t     *bs       = info->bits->bs;
    while( !au_completed )
    {
        /* Read data from the stream if needed. */
        eac3_imp->next_frame_pos += info->frame_size;
        lsmash_bs_read_seek( bs, eac3_imp->next_frame_pos, SEEK_SET );
        uint64_t remain_size = lsmash_bs_get_remaining_buffer_size( bs );
        if( remain_size < EAC3_MAX_SYNCFRAME_LENGTH )
        {
            if( lsmash_bs_read( bs, bs->buffer.max_size ) < 0 )
            {
                lsmash_log( importer, LSMASH_LOG_ERROR, "failed to read data from the stream.\n" );
                return -1;
            }
            remain_size = lsmash_bs_get_remaining_buffer_size( bs );
        }
        uint64_t copy_size = LSMASH_MIN( remain_size, EAC3_MAX_SYNCFRAME_LENGTH );
        memcpy( eac3_imp->buffer, lsmash_bs_get_buffer_data( bs ), copy_size );
        /* Check the remainder length of the buffer.
         * If there is enough length, then parse the syncframe in it.
         * The length 5 is the required byte length to get frame size. */
        if( bs->eob || (bs->eof && remain_size < 5) )
        {
            /* Reached the end of stream.
             * According to ETSI TS 102 366 V1.2.1 (2008-08),
             * one access unit consists of 6 audio blocks and begins with independent substream 0.
             * The specification doesn't mention the case where a enhanced AC-3 stream ends at non-mod6 audio blocks.
             * At the end of the stream, therefore, we might make an access unit which has less than 6 audio blocks anyway. */
            eac3_imp->status = IMPORTER_EOF;
            au_completed = !!eac3_imp->incomplete_au_length;
            if( !au_completed )
            {
                /* No more access units in the stream. */
                if( lsmash_bs_get_remaining_buffer_size( bs ) )
                {
                    lsmash_log( importer, LSMASH_LOG_WARNING, "the stream is truncated at the end.\n" );
                    return -1;
                }
                return 0;
            }
            if( !info->dec3_param_initialized )
                eac3_update_specific_param( info );
        }
        else
        {
            /* Check the syncword. */
            if( lsmash_bs_show_byte( bs, 0 ) != 0x0b
             || lsmash_bs_show_byte( bs, 1 ) != 0x77 )
            {
                lsmash_log( importer, LSMASH_LOG_ERROR, "a syncword is not found.\n" );
                return -1;
            }
            /* Parse syncframe. */
            info->frame_size = 0;
            if( eac3_parse_syncframe( info ) < 0 )
            {
                lsmash_log( importer, LSMASH_LOG_ERROR, "failed to parse syncframe.\n" );
                return -1;
            }
            if( remain_size < info->frame_size )
            {
                lsmash_log( importer, LSMASH_LOG_ERROR, "a frame is truncated.\n" );
                return -1;
            }
            int independent = info->strmtyp != 0x1;
            if( independent && info->substreamid == 0x0 )
            {
                if( info->number_of_audio_blocks == 6 )
                {
                    /* Encountered the first syncframe of the next access unit. */
                    info->number_of_audio_blocks = 0;
                    au_completed = 1;
                }
                else if( info->number_of_audio_blocks > 6 )
                {
                    lsmash_log( importer, LSMASH_LOG_ERROR, "greater than 6 consecutive independent substreams.\n" );
                    return -1;
                }
                info->number_of_audio_blocks += eac3_audio_block_table[ info->numblkscod ];
                info->number_of_independent_substreams = 0;
                eac3_imp->current_fscod2 = info->fscod2;
            }
            else if( info->syncframe_count == 0 )
            {
                /* The first syncframe in an AU must be independent and assigned substream ID 0. */
                lsmash_log( importer, LSMASH_LOG_ERROR, "the first syncframe is NOT an independent substream.\n" );
                return -1;
            }
            if( independent )
                info->independent_info[info->number_of_independent_substreams ++].num_dep_sub = 0;
            else
                ++ info->independent_info[info->number_of_independent_substreams - 1].num_dep_sub;
        }
        if( au_completed )
        {
            memcpy( eac3_imp->au, eac3_imp->incomplete_au, eac3_imp->incomplete_au_length );
            eac3_imp->au_length             = eac3_imp->incomplete_au_length;
            eac3_imp->incomplete_au_length  = 0;
            eac3_imp->syncframe_count_in_au = info->syncframe_count;
            info->syncframe_count = 0;
            if( eac3_imp->status == IMPORTER_EOF )
                break;
        }
        /* Increase buffer size to store AU if short. */
        if( eac3_imp->incomplete_au_length + info->frame_size > eac3_imp->au_buffers->buffer_size )
        {
            lsmash_multiple_buffers_t *temp = lsmash_resize_multiple_buffers( eac3_imp->au_buffers,
                                                                              eac3_imp->au_buffers->buffer_size + EAC3_MAX_SYNCFRAME_LENGTH );
            if( !temp )
                return -1;
            eac3_imp->au_buffers    = temp;
            eac3_imp->au            = lsmash_withdraw_buffer( eac3_imp->au_buffers, 1 );
            eac3_imp->incomplete_au = lsmash_withdraw_buffer( eac3_imp->au_buffers, 2 );
        }
        /* Append syncframe data. */
        memcpy( eac3_imp->incomplete_au + eac3_imp->incomplete_au_length, eac3_imp->buffer, info->frame_size );
        eac3_imp->incomplete_au_length += info->frame_size;
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
    eac3_importer_t *eac3_imp = (eac3_importer_t *)importer->info;
    eac3_info_t     *info     = &eac3_imp->info;
    importer_status current_status = eac3_imp->status;
    if( current_status == IMPORTER_ERROR || buffered_sample->length < eac3_imp->au_length )
        return -1;
    if( current_status == IMPORTER_EOF && eac3_imp->au_length == 0 )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( current_status == IMPORTER_CHANGE )
    {
        lsmash_codec_specific_t *cs = isom_get_codec_specific( summary->opaque, LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_EC_3 );
        if( cs )
        {
            cs->destruct( cs->data.unstructured );
            cs->data.unstructured = eac3_imp->next_dec3;
            cs->size              = eac3_imp->next_dec3_length;
        }
        summary->max_au_length = eac3_imp->syncframe_count_in_au * EAC3_MAX_SYNCFRAME_LENGTH;
        eac3_update_sample_rate( &summary->frequency, &info->dec3_param, &eac3_imp->current_fscod2 );
        eac3_update_channel_count( &summary->channels, &info->dec3_param );
    }
    memcpy( buffered_sample->data, eac3_imp->au, eac3_imp->au_length );
    buffered_sample->length                 = eac3_imp->au_length;
    buffered_sample->dts                    = eac3_imp->au_number++ * summary->samples_in_frame;
    buffered_sample->cts                    = buffered_sample->dts;
    buffered_sample->prop.ra_flags          = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    buffered_sample->prop.pre_roll.distance = 1;    /* MDCT */
    if( eac3_imp->status == IMPORTER_EOF )
    {
        eac3_imp->au_length = 0;
        return 0;
    }
    uint32_t old_syncframe_count_in_au = eac3_imp->syncframe_count_in_au;
    if( eac3_importer_get_next_accessunit_internal( importer ) < 0 )
    {
        eac3_imp->status = IMPORTER_ERROR;
        return current_status;
    }
    if( eac3_imp->syncframe_count_in_au )
    {
        /* Check sample description change. */
        uint32_t new_length;
        uint8_t *dec3 = lsmash_create_eac3_specific_info( &info->dec3_param, &new_length );
        if( !dec3 )
        {
            eac3_imp->status = IMPORTER_ERROR;
            return current_status;
        }
        lsmash_codec_specific_t *cs = isom_get_codec_specific( summary->opaque, LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_EC_3 );
        if( (eac3_imp->syncframe_count_in_au > old_syncframe_count_in_au)
         || (cs && (new_length != cs->size || memcmp( dec3, cs->data.unstructured, cs->size ))) )
        {
            eac3_imp->status = IMPORTER_CHANGE;
            eac3_imp->next_dec3        = dec3;
            eac3_imp->next_dec3_length = new_length;
        }
        else
        {
            if( eac3_imp->status != IMPORTER_EOF )
                eac3_imp->status = IMPORTER_OK;
            lsmash_free( dec3 );
        }
    }
    return current_status;
}

static lsmash_audio_summary_t *eac3_create_summary( eac3_importer_t *eac3_imp )
{
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
    if( !summary )
        return NULL;
    eac3_info_t *info = &eac3_imp->info;
    lsmash_codec_specific_t *cs = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_EC_3,
                                                                     LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
    cs->data.unstructured = lsmash_create_eac3_specific_info( &info->dec3_param, &cs->size );
    if( !cs->data.unstructured
     || lsmash_add_entry( &summary->opaque->list, cs ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( cs );
        return NULL;
    }
    summary->sample_type      = ISOM_CODEC_TYPE_EC_3_AUDIO;
    summary->max_au_length    = eac3_imp->syncframe_count_in_au * EAC3_MAX_SYNCFRAME_LENGTH;
    summary->aot              = MP4A_AUDIO_OBJECT_TYPE_NULL;    /* no effect */
    summary->sample_size      = 16;                             /* no effect */
    summary->samples_in_frame = EAC3_MIN_SAMPLE_DURATION * 6;   /* 256 (samples per audio block) * 6 (audio blocks) */
    summary->sbr_mode         = MP4A_AAC_SBR_NOT_SPECIFIED;     /* no effect */
    eac3_update_sample_rate( &summary->frequency, &info->dec3_param, &eac3_imp->current_fscod2 );
    eac3_update_channel_count( &summary->channels, &info->dec3_param );
    return summary;
}

static int eac3_importer_probe( importer_t *importer )
{
    eac3_importer_t *eac3_imp = create_eac3_importer();
    if( !eac3_imp )
        return -1;
    lsmash_bits_t *bits = eac3_imp->info.bits;
    lsmash_bs_t   *bs   = bits->bs;
    bs->stream          = importer->stream;
    bs->read            = lsmash_fread_wrapper;
    bs->seek            = lsmash_fseek_wrapper;
    bs->unseekable      = importer->is_stdin;
    bs->buffer.max_size = EAC3_MAX_SYNCFRAME_LENGTH;
    importer->info = eac3_imp;
    if( eac3_importer_get_next_accessunit_internal( importer ) < 0 )
        goto fail;
    lsmash_audio_summary_t *summary = eac3_create_summary( eac3_imp );
    if( !summary )
        goto fail;
    if( eac3_imp->status != IMPORTER_EOF )
        eac3_imp->status = IMPORTER_OK;
    eac3_imp->au_number = 0;
    if( lsmash_add_entry( importer->summaries, summary ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        goto fail;
    }
    return 0;
fail:
    remove_eac3_importer( eac3_imp );
    importer->info      = NULL;
    return -1;
}

static uint32_t eac3_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    eac3_importer_t *eac3_imp = (eac3_importer_t *)importer->info;
    if( !eac3_imp || track_number != 1 || eac3_imp->status != IMPORTER_EOF || eac3_imp->au_length )
        return 0;
    return EAC3_MIN_SAMPLE_DURATION * eac3_imp->info.number_of_audio_blocks;
}

static const importer_functions eac3_importer =
{
    { "Enhanced AC-3", offsetof( importer_t, log_level ) },
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
    importer_status        status;
    lsmash_bs_t           *bs;
    als_specific_config_t  alssc;
    uint32_t               samples_in_frame;
    uint32_t               au_number;
} mp4a_als_importer_t;

static void remove_mp4a_als_importer( mp4a_als_importer_t *als_imp )
{
    lsmash_bs_cleanup( als_imp->bs );
    lsmash_free( als_imp->alssc.ra_unit_size );
    lsmash_free( als_imp->alssc.sc_data );
    lsmash_free( als_imp );
}

static void mp4a_als_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        remove_mp4a_als_importer( importer->info );
}

static mp4a_als_importer_t *create_mp4a_als_importer( importer_t *importer )
{
    mp4a_als_importer_t *als_imp = lsmash_malloc_zero( sizeof(mp4a_als_importer_t) );
    if( !als_imp )
        return NULL;
    als_imp->bs = lsmash_bs_create();
    if( !als_imp->bs )
    {
        lsmash_free( als_imp );
        return NULL;
    }
    return als_imp;
}

static void als_copy_from_buffer( als_specific_config_t *alssc, lsmash_bs_t *bs, uint64_t size )
{
    if( alssc->alloc < size )
    {
        size_t alloc = alssc->alloc ? (alssc->alloc << 1) : (1 << 10);
        uint8_t *temp = lsmash_realloc( alssc->sc_data, alloc );
        if( !temp )
            return;
        alssc->sc_data = temp;
        alssc->alloc   = alloc;
    }
    memcpy( alssc->sc_data + alssc->size, lsmash_bs_get_buffer_data( bs ), size );
    alssc->size += size;
    lsmash_bs_read_seek( bs, size, SEEK_CUR );
}

static int als_parse_specific_config( mp4a_als_importer_t *als_imp )
{
    lsmash_bs_t *bs = als_imp->bs;
    /* Check ALS identifier( = 0x414C5300). */
    if( 0x414C5300 != lsmash_bs_show_be32( bs, 0 ) )
        return -1;
    als_specific_config_t *alssc = &als_imp->alssc;
    alssc->samp_freq     = lsmash_bs_show_be32( bs, 4 );
    alssc->samples       = lsmash_bs_show_be32( bs, 8 );
    if( alssc->samples == 0xffffffff )
        return -1;      /* We don't support this case. */
    alssc->channels      = lsmash_bs_show_be16( bs, 12 );
    alssc->resolution    = (lsmash_bs_show_byte( bs, 14 ) & 0x1c) >> 2;
    if( alssc->resolution > 3 )
        return -1;      /* reserved */
    alssc->frame_length  = lsmash_bs_show_be16( bs, 15 );
    alssc->random_access = lsmash_bs_show_byte( bs, 17 );
    alssc->ra_flag       = (lsmash_bs_show_byte( bs, 18 ) & 0xc0) >> 6;
    if( alssc->ra_flag == 0 )
        return -1;      /* We don't support this case. */
#if 0
    if( alssc->samples == 0xffffffff && alssc->ra_flag == 2 )
        return -1;
#endif
    uint8_t temp8 = lsmash_bs_show_byte( bs, 20 );
    int chan_sort = !!(temp8 & 0x1);
    if( alssc->channels == 0 )
    {
        if( temp8 & 0x8 )
            return -1;      /* If channels = 0 (mono), joint_stereo = 0. */
        else if( temp8 & 0x4 )
            return -1;      /* If channels = 0 (mono), mc_coding = 0. */
        else if( chan_sort )
            return -1;      /* If channels = 0 (mono), chan_sort = 0. */
    }
    int chan_config      = !!(temp8 & 0x2);
    temp8 = lsmash_bs_show_byte( bs, 21 );
    int crc_enabled      = !!(temp8 & 0x80);
    int aux_data_enabled = !!(temp8 & 0x1);
    als_copy_from_buffer( alssc, bs, ALSSC_TWELVE_LENGTH );
    if( chan_config )
    {
        /* chan_config_info */
        lsmash_bs_read( bs, 2 );
        als_copy_from_buffer( alssc, bs, 2 );
    }
    if( chan_sort )
    {
        uint32_t ChBits = lsmash_ceil_log2( alssc->channels + 1 );
        uint32_t chan_pos_length = (alssc->channels + 1) * ChBits;
        chan_pos_length = ((uint64_t)chan_pos_length + 7) / 8;    /* byte_align */
        lsmash_bs_read( bs, chan_pos_length );
        als_copy_from_buffer( alssc, bs, chan_pos_length );
    }
    /* orig_header, orig_trailer and crc. */
    {
        uint32_t header_size  = lsmash_bs_show_be32( bs, 0 );
        uint32_t trailer_size = lsmash_bs_show_be32( bs, 4 );
        als_copy_from_buffer( alssc, bs, 8 );
        if( header_size != 0xffffffff )
        {
            lsmash_bs_read( bs, header_size );
            als_copy_from_buffer( alssc, bs, header_size );
        }
        if( trailer_size != 0xffffffff )
        {
            lsmash_bs_read( bs, trailer_size );
            als_copy_from_buffer( alssc, bs, trailer_size );
        }
        if( crc_enabled )
        {
            lsmash_bs_read( bs, 4 );
            als_copy_from_buffer( alssc, bs, 4 );
        }
    }
    /* Random access units */
    {
        uint32_t number_of_frames = ((alssc->samples + alssc->frame_length) / (alssc->frame_length + 1));
        if( alssc->random_access != 0 )
            alssc->number_of_ra_units = ((uint64_t)number_of_frames + alssc->random_access - 1) / alssc->random_access;
        else
            alssc->number_of_ra_units = 0;
        if( alssc->ra_flag == 2 && alssc->random_access != 0 )
        {
            /* We don't copy all ra_unit_size into alssc->sc_data. */
            int64_t read_size  = (int64_t)alssc->number_of_ra_units * 4;
            int64_t end_offset = lsmash_bs_get_stream_pos( bs ) + read_size;
            alssc->ra_unit_size = lsmash_malloc( read_size );
            if( !alssc->ra_unit_size )
                return -1;
            uint32_t max_ra_unit_size = 0;
            for( uint32_t i = 0; i < alssc->number_of_ra_units; i++ )
            {
                alssc->ra_unit_size[i] = lsmash_bs_get_be32( bs );
                max_ra_unit_size = LSMASH_MAX( max_ra_unit_size, alssc->ra_unit_size[i] );
            }
            lsmash_bs_read_seek( bs, end_offset, SEEK_SET );
        }
        else
            alssc->ra_unit_size = NULL;
    }
    /* auxiliary data */
    if( aux_data_enabled )
    {
        uint32_t aux_size = lsmash_bs_show_be32( bs, 0 );
        als_copy_from_buffer( alssc, bs, 4 );
        if( aux_size && aux_size != 0xffffffff )
        {
            lsmash_bs_read( bs, aux_size );
            als_copy_from_buffer( alssc, bs, aux_size );
        }
    }
    /* Set 0 to ra_flag. We will remove ra_unit_size in each access unit. */
    alssc->sc_data[18] &= 0x3f;
    return 0;
}

static int mp4a_als_importer_get_accessunit( importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_get_entry_data( importer->summaries, track_number );
    if( !summary )
        return -1;
    mp4a_als_importer_t *als_imp = (mp4a_als_importer_t *)importer->info;
    importer_status current_status = als_imp->status;
    if( current_status == IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    lsmash_bs_t *bs = als_imp->bs;
    als_specific_config_t *alssc = &als_imp->alssc;
    if( alssc->number_of_ra_units == 0 )
    {
        memcpy( buffered_sample->data, lsmash_bs_get_buffer_data( bs ), alssc->access_unit_size );
        buffered_sample->length        = alssc->access_unit_size;
        buffered_sample->cts           = 0;
        buffered_sample->dts           = 0;
        buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
        als_imp->status = IMPORTER_EOF;
        return 0;
    }
    uint32_t au_length;
    if( alssc->ra_flag == 2 )
        au_length = alssc->ra_unit_size[ als_imp->au_number ];
    else /* if( alssc->ra_flag == 1 ) */
        /* We don't export ra_unit_size into a sample. */
        au_length = lsmash_bs_get_be32( bs );
    if( buffered_sample->length < au_length )
    {
        lsmash_log( importer, LSMASH_LOG_WARNING, "the buffer has not enough size.\n" );
        return -1;
    }
    if( lsmash_bs_get_bytes_ex( bs, au_length, buffered_sample->data ) != au_length )
    {
        lsmash_log( importer, LSMASH_LOG_WARNING, "failed to read an access unit.\n" );
        als_imp->status = IMPORTER_ERROR;
        return -1;
    }
    buffered_sample->length        = au_length;
    buffered_sample->dts           = als_imp->au_number ++ * als_imp->samples_in_frame;
    buffered_sample->cts           = buffered_sample->dts;
    buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    if( als_imp->au_number == alssc->number_of_ra_units )
        als_imp->status = IMPORTER_EOF;
    return 0;
}

#undef CHECK_UPDATE

static lsmash_audio_summary_t *als_create_summary( lsmash_bs_t *bs, als_specific_config_t *alssc )
{
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
    if( !summary )
        return NULL;
    summary->sample_type = ISOM_CODEC_TYPE_MP4A_AUDIO;
    summary->aot         = MP4A_AUDIO_OBJECT_TYPE_ALS;
    summary->frequency   = alssc->samp_freq;
    summary->channels    = alssc->channels + 1;
    summary->sample_size = (alssc->resolution + 1) * 8;
    summary->sbr_mode    = MP4A_AAC_SBR_NOT_SPECIFIED; /* no effect */
    if( alssc->random_access != 0 )
    {
        summary->samples_in_frame = (alssc->frame_length + 1) * alssc->random_access;
        summary->max_au_length    = summary->channels * (summary->sample_size / 8) * summary->samples_in_frame;
    }
    else
    {
        /* Read the remainder of overall stream as an access unit. */
        alssc->access_unit_size = lsmash_bs_get_remaining_buffer_size( bs );
        while( !bs->eof )
        {
            if( lsmash_bs_read( bs, bs->buffer.max_size ) < 0 )
                return NULL;
            alssc->access_unit_size = lsmash_bs_get_remaining_buffer_size( bs );
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
    if( lsmash_set_mp4sys_decoder_specific_info( param, data, data_length ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        lsmash_free( data );
        return NULL;
    }
    lsmash_free( data );
    if( lsmash_add_entry( &summary->opaque->list, specific ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        return NULL;
    }
    return summary;
}

static int mp4a_als_importer_probe( importer_t *importer )
{
    mp4a_als_importer_t *als_imp = create_mp4a_als_importer( importer );
    if( !als_imp )
        return -1;
    lsmash_bs_t *bs = als_imp->bs;
    bs->stream          = importer->stream;
    bs->read            = lsmash_fread_wrapper;
    bs->seek            = lsmash_fseek_wrapper;
    bs->unseekable      = importer->is_stdin;
    bs->buffer.max_size = BS_MAX_DEFAULT_READ_SIZE;
    /* Parse ALS specific configuration. */
    if( als_parse_specific_config( als_imp ) < 0 )
        goto fail;
    lsmash_audio_summary_t *summary = als_create_summary( bs, &als_imp->alssc );
    if( !summary )
        goto fail;
    /* importer status */
    als_imp->status           = IMPORTER_OK;
    als_imp->samples_in_frame = summary->samples_in_frame;
    if( lsmash_add_entry( importer->summaries, summary ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        goto fail;
    }
    importer->info = als_imp;
    return 0;
fail:
    remove_mp4a_als_importer( als_imp );
    return -1;
}

static uint32_t mp4a_als_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    mp4a_als_importer_t *als_imp = (mp4a_als_importer_t *)importer->info;
    if( !als_imp || track_number != 1 || als_imp->status != IMPORTER_EOF )
        return 0;
    als_specific_config_t *alssc = &als_imp->alssc;
    /* If alssc->number_of_ra_units == 0, then the last sample duration is just alssc->samples
     * since als_create_summary sets 0 to summary->samples_in_frame i.e. als_imp->samples_in_frame. */
    return alssc->samples - (alssc->number_of_ra_units - 1) * als_imp->samples_in_frame;
}

static const importer_functions mp4a_als_importer =
{
    { "MPEG-4 ALS", offsetof( importer_t, log_level ) },
    1,
    mp4a_als_importer_probe,
    mp4a_als_importer_get_accessunit,
    mp4a_als_importer_get_last_delta,
    mp4a_als_importer_cleanup
};

/***************************************************************************
    DTS importer
    ETSI TS 102 114 V1.2.1 (2002-12)
    ETSI TS 102 114 V1.3.1 (2011-08)
    ETSI TS 102 114 V1.4.1 (2012-09)
***************************************************************************/
#include "codecs/dts.h"

typedef struct
{
    importer_status status;
    dts_info_t      info;
    uint64_t next_frame_pos;
    uint8_t buffer[DTS_MAX_EXSS_SIZE];
    lsmash_multiple_buffers_t *au_buffers;
    uint8_t *au;
    uint32_t au_length;
    uint8_t *incomplete_au;
    uint32_t incomplete_au_length;
    uint32_t au_number;
} dts_importer_t;

static void remove_dts_importer( dts_importer_t *dts_imp )
{
    if( !dts_imp )
        return;
    lsmash_destroy_multiple_buffers( dts_imp->au_buffers );
    lsmash_bits_adhoc_cleanup( dts_imp->info.bits );
    lsmash_free( dts_imp );
}

static dts_importer_t *create_dts_importer( void )
{
    dts_importer_t *dts_imp = (dts_importer_t *)lsmash_malloc_zero( sizeof(dts_importer_t) );
    if( !dts_imp )
        return NULL;
    dts_info_t *dts_info = &dts_imp->info;
    dts_info->bits = lsmash_bits_adhoc_create();
    if( !dts_info->bits )
    {
        lsmash_free( dts_imp );
        return NULL;
    }
    dts_imp->au_buffers = lsmash_create_multiple_buffers( 2, DTS_MAX_EXSS_SIZE );
    if( !dts_imp->au_buffers )
    {
        lsmash_bits_adhoc_cleanup( dts_info->bits );
        lsmash_free( dts_imp );
        return NULL;
    }
    dts_imp->au            = lsmash_withdraw_buffer( dts_imp->au_buffers, 1 );
    dts_imp->incomplete_au = lsmash_withdraw_buffer( dts_imp->au_buffers, 2 );
    dts_setup_parser( dts_info );
    return dts_imp;
}

static void dts_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        remove_dts_importer( importer->info );
}

static int dts_importer_get_next_accessunit_internal( importer_t *importer )
{
    int au_completed = 0;
    dts_importer_t *dts_imp = (dts_importer_t *)importer->info;
    dts_info_t     *info    = &dts_imp->info;
    lsmash_bs_t    *bs      = info->bits->bs;
    while( !au_completed )
    {
        /* Read data from the stream if needed. */
        dts_imp->next_frame_pos += info->frame_size;
        lsmash_bs_read_seek( bs, dts_imp->next_frame_pos, SEEK_SET );
        uint64_t remain_size = lsmash_bs_get_remaining_buffer_size( bs );
        if( remain_size < DTS_MAX_EXSS_SIZE )
        {
            if( lsmash_bs_read( bs, bs->buffer.max_size ) < 0 )
            {
                lsmash_log( importer, LSMASH_LOG_ERROR, "failed to read data from the stream.\n" );
                return -1;
            }
            remain_size = lsmash_bs_get_remaining_buffer_size( bs );
        }
        memcpy( dts_imp->buffer, lsmash_bs_get_buffer_data( bs ), LSMASH_MIN( remain_size, DTS_MAX_EXSS_SIZE ) );
        /* Check the remainder length of the buffer.
         * If there is enough length, then parse the frame in it.
         * The length 10 is the required byte length to get frame size. */
        if( bs->eob || (bs->eof && remain_size < 10) )
        {
            /* Reached the end of stream. */
            dts_imp->status = IMPORTER_EOF;
            au_completed = !!dts_imp->incomplete_au_length;
            if( !au_completed )
            {
                /* No more access units in the stream. */
                if( lsmash_bs_get_remaining_buffer_size( bs ) )
                {
                    lsmash_log( importer, LSMASH_LOG_WARNING, "the stream is truncated at the end.\n" );
                    return -1;
                }
                return 0;
            }
            if( !info->ddts_param_initialized )
                dts_update_specific_param( info );
        }
        else
        {
            /* Parse substream frame. */
            dts_substream_type prev_substream_type = info->substream_type;
            info->substream_type = dts_get_substream_type( info );
            int (*dts_parse_frame)( dts_info_t * ) = NULL;
            switch( info->substream_type )
            {
                /* Decide substream frame parser and check if this frame and the previous frame belong to the same AU. */
                case DTS_SUBSTREAM_TYPE_CORE :
                    if( prev_substream_type != DTS_SUBSTREAM_TYPE_NONE )
                        au_completed = 1;
                    dts_parse_frame = dts_parse_core_substream;
                    break;
                case DTS_SUBSTREAM_TYPE_EXTENSION :
                {
                    uint8_t prev_exss_index = info->exss_index;
                    if( dts_get_exss_index( info, &info->exss_index ) < 0 )
                    {
                        lsmash_log( importer, LSMASH_LOG_ERROR, "failed to get the index of an extension substream.\n" );
                        return -1;
                    }
                    if( prev_substream_type == DTS_SUBSTREAM_TYPE_EXTENSION
                     && info->exss_index <= prev_exss_index )
                        au_completed = 1;
                    dts_parse_frame = dts_parse_extension_substream;
                    break;
                }
                default :
                    lsmash_log( importer, LSMASH_LOG_ERROR, "unknown substream type is detected.\n" );
                    return -1;
            }
            if( !info->ddts_param_initialized && au_completed )
                dts_update_specific_param( info );
            info->frame_size = 0;
            if( dts_parse_frame( info ) < 0 )
            {
                lsmash_log( importer, LSMASH_LOG_ERROR, "failed to parse a frame.\n" );
                return -1;
            }
        }
        if( au_completed )
        {
            memcpy( dts_imp->au, dts_imp->incomplete_au, dts_imp->incomplete_au_length );
            dts_imp->au_length            = dts_imp->incomplete_au_length;
            dts_imp->incomplete_au_length = 0;
            info->exss_count = (info->substream_type == DTS_SUBSTREAM_TYPE_EXTENSION);
            if( dts_imp->status == IMPORTER_EOF )
                break;
        }
        /* Increase buffer size to store AU if short. */
        if( dts_imp->incomplete_au_length + info->frame_size > dts_imp->au_buffers->buffer_size )
        {
            lsmash_multiple_buffers_t *temp = lsmash_resize_multiple_buffers( dts_imp->au_buffers,
                                                                              dts_imp->au_buffers->buffer_size + DTS_MAX_EXSS_SIZE );
            if( !temp )
                return -1;
            dts_imp->au_buffers    = temp;
            dts_imp->au            = lsmash_withdraw_buffer( dts_imp->au_buffers, 1 );
            dts_imp->incomplete_au = lsmash_withdraw_buffer( dts_imp->au_buffers, 2 );
        }
        /* Append frame data. */
        memcpy( dts_imp->incomplete_au + dts_imp->incomplete_au_length, dts_imp->buffer, info->frame_size );
        dts_imp->incomplete_au_length += info->frame_size;
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
    dts_importer_t *dts_imp = (dts_importer_t *)importer->info;
    dts_info_t     *info    = &dts_imp->info;
    importer_status current_status = dts_imp->status;
    if( current_status == IMPORTER_ERROR || buffered_sample->length < dts_imp->au_length )
        return -1;
    if( current_status == IMPORTER_EOF && dts_imp->au_length == 0 )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( current_status == IMPORTER_CHANGE )
        summary->max_au_length = 0;
    memcpy( buffered_sample->data, dts_imp->au, dts_imp->au_length );
    buffered_sample->length                 = dts_imp->au_length;
    buffered_sample->dts                    = dts_imp->au_number++ * summary->samples_in_frame;
    buffered_sample->cts                    = buffered_sample->dts;
    buffered_sample->prop.ra_flags          = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    buffered_sample->prop.pre_roll.distance = !!(info->flags & DTS_EXT_SUBSTREAM_LBR_FLAG);     /* MDCT */
    if( dts_imp->status == IMPORTER_EOF )
    {
        dts_imp->au_length = 0;
        return 0;
    }
    if( dts_importer_get_next_accessunit_internal( importer ) )
        dts_imp->status = IMPORTER_ERROR;
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
    /* The CODEC identifiers probably should not be the combination of 'mp4a' and
     * the objectTypeIndications for DTS audio since there is no public specification
     * which defines the encapsulation of the stream as the MPEG-4 Audio context yet.
     * In the world, there are muxers which is using such doubtful implementation.
     * The objectTypeIndications are registered at MP4RA, but this does not always
     * mean we can mux by using those objectTypeIndications.
     * If available, there shall be the specification which defines the existence of
     * DecoderSpecificInfo and its semantics, and what access unit consists of. */
    summary->sample_type = lsmash_dts_get_codingname( param );
    summary->aot         = MP4A_AUDIO_OBJECT_TYPE_NULL;     /* make no sense */
    summary->sbr_mode    = MP4A_AAC_SBR_NOT_SPECIFIED;      /* make no sense */
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
    dts_importer_t *dts_imp = create_dts_importer();
    if( !dts_imp )
        return -1;
    lsmash_bits_t *bits = dts_imp->info.bits;
    lsmash_bs_t   *bs   = bits->bs;
    bs->stream          = importer->stream;
    bs->read            = lsmash_fread_wrapper;
    bs->seek            = lsmash_fseek_wrapper;
    bs->unseekable      = importer->is_stdin;
    bs->buffer.max_size = DTS_MAX_EXSS_SIZE;
    importer->info = dts_imp;
    if( dts_importer_get_next_accessunit_internal( importer ) < 0 )
        goto fail;
    lsmash_audio_summary_t *summary = dts_create_summary( &dts_imp->info );
    if( !summary )
        goto fail;
    if( dts_imp->status != IMPORTER_EOF )
        dts_imp->status = IMPORTER_OK;
    dts_imp->au_number = 0;
    if( lsmash_add_entry( importer->summaries, summary ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        goto fail;
    }
    return 0;
fail:
    remove_dts_importer( dts_imp );
    importer->info = NULL;
    return -1;
}

static uint32_t dts_importer_get_last_delta( importer_t* importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    dts_importer_t *dts_imp = (dts_importer_t *)importer->info;
    if( !dts_imp || track_number != 1 || dts_imp->status != IMPORTER_EOF || dts_imp->au_length )
        return 0;
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_get_entry_data( importer->summaries, track_number );
    if( !summary )
        return 0;
    return (summary->frequency * dts_imp->info.frame_duration) / dts_imp->info.ddts_param.DTSSamplingFrequency;
}

static const importer_functions dts_importer =
{
    { "DTS Coherent Acoustics", offsetof( importer_t, log_level ) },
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
#include "codecs/h264.h"
#include "codecs/nalu.h"

typedef struct
{
    importer_status        status;
    h264_info_t            info;
    lsmash_entry_list_t    avcC_list[1];    /* stored as lsmash_codec_specific_t */
    lsmash_media_ts_list_t ts_list;
    lsmash_bs_t           *bs;
    uint32_t max_au_length;
    uint32_t num_undecodable;
    uint32_t avcC_number;
    uint32_t last_delta;
    uint64_t last_intra_cts;
    uint64_t sc_head_pos;
    uint8_t  composition_reordering_present;
    uint8_t  field_pic_present;
} h264_importer_t;

typedef struct
{
    int64_t  poc;
    uint32_t delta;
    uint16_t poc_delta;
    uint16_t reset;
} nal_pic_timing_t;

static void remove_h264_importer( h264_importer_t *h264_imp )
{
    if( !h264_imp )
        return;
    lsmash_remove_entries( h264_imp->avcC_list, lsmash_destroy_codec_specific_data );
    h264_cleanup_parser( &h264_imp->info );
    lsmash_bs_cleanup( h264_imp->bs );
    lsmash_free( h264_imp->ts_list.timestamp );
    lsmash_free( h264_imp );
}

static void h264_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        remove_h264_importer( importer->info );
}

static h264_importer_t *create_h264_importer( importer_t *importer )
{
    h264_importer_t *h264_imp = lsmash_malloc_zero( sizeof(h264_importer_t) );
    if( !h264_imp )
        return NULL;
    if( h264_setup_parser( &h264_imp->info, 0 ) < 0 )
    {
        remove_h264_importer( h264_imp );
        return NULL;
    }
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
    {
        remove_h264_importer( h264_imp );
        return NULL;
    }
    bs->stream          = importer->stream;
    bs->read            = lsmash_fread_wrapper;
    bs->seek            = lsmash_fseek_wrapper;
    bs->unseekable      = importer->is_stdin;
    bs->buffer.max_size = BS_MAX_DEFAULT_READ_SIZE;
    lsmash_init_entry_list( h264_imp->avcC_list );
    h264_imp->bs = bs;
    return h264_imp;
}

static inline int h264_complete_au( h264_access_unit_t *au, int probe )
{
    if( !au->picture.has_primary || au->incomplete_length == 0 )
        return 0;
    if( !probe )
        memcpy( au->data, au->incomplete_data, au->incomplete_length );
    au->length              = au->incomplete_length;
    au->incomplete_length   = 0;
    au->picture.has_primary = 0;
    return 1;
}

static void h264_append_nalu_to_au( h264_access_unit_t *au, uint8_t *src_nalu, uint32_t nalu_length, int probe )
{
    if( !probe )
    {
        uint8_t *dst_nalu = au->incomplete_data + au->incomplete_length + NALU_DEFAULT_NALU_LENGTH_SIZE;
        for( int i = NALU_DEFAULT_NALU_LENGTH_SIZE; i; i-- )
            *(dst_nalu - i) = (nalu_length >> ((i - 1) * 8)) & 0xff;
        memcpy( dst_nalu, src_nalu, nalu_length );
    }
    /* Note: au->incomplete_length shall be 0 immediately after AU has completed.
     * Therefore, possible_au_length in h264_get_access_unit_internal() can't be used here
     * to avoid increasing AU length monotonously through the entire stream. */
    au->incomplete_length += NALU_DEFAULT_NALU_LENGTH_SIZE + nalu_length;
}

static inline void h264_get_au_internal_end( h264_importer_t *h264_imp, h264_access_unit_t *au )
{
    if( lsmash_bs_is_end( h264_imp->bs, 0 ) && (au->incomplete_length == 0) )
        h264_imp->status = IMPORTER_EOF;
    else if( h264_imp->status != IMPORTER_CHANGE )
        h264_imp->status = IMPORTER_OK;
}

static int h264_get_au_internal_succeeded( h264_importer_t *h264_imp, h264_access_unit_t *au )
{
    h264_get_au_internal_end( h264_imp, au );
    au->number += 1;
    return 0;
}

static int h264_get_au_internal_failed( h264_importer_t *h264_imp, h264_access_unit_t *au, int complete_au )
{
    h264_get_au_internal_end( h264_imp, au );
    if( complete_au )
        au->number += 1;
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
    lsmash_codec_specific_t *cs = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_H264,
                                                                     LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
    cs->data.unstructured = lsmash_create_h264_specific_info( param, &cs->size );
    if( !cs->data.unstructured
     || lsmash_add_entry( &summary->opaque->list, cs ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( cs );
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
    h264_importer_t                   *h264_imp,
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
    if( !dst_cs || lsmash_add_entry( h264_imp->avcC_list, dst_cs ) < 0 )
    {
        lsmash_destroy_codec_specific_data( dst_cs );
        return -1;
    }
    return 0;
}

static inline void h264_new_access_unit
(
    h264_access_unit_t *au
)
{
    au->length                     = 0;
    au->picture.type               = H264_PICTURE_TYPE_NONE;
    au->picture.random_accessible  = 0;
    au->picture.recovery_frame_cnt = 0;
    au->picture.has_mmco5          = 0;
    au->picture.has_redundancy     = 0;
    au->picture.broken_link_flag   = 0;
}

/* If probe equals 0, don't get the actual data (EBPS) of an access unit and only parse NALU.
 * Currently, you can get AU of AVC video elemental stream only, not AVC parameter set elemental stream defined in 14496-15. */
static int h264_get_access_unit_internal
(
    importer_t *importer,
    int         probe
)
{
    h264_importer_t      *h264_imp = (h264_importer_t *)importer->info;
    h264_info_t          *info     = &h264_imp->info;
    h264_slice_info_t    *slice    = &info->slice;
    h264_access_unit_t   *au       = &info->au;
    h264_picture_info_t  *picture  = &au->picture;
    h264_stream_buffer_t *sb       = &info->buffer;
    lsmash_bs_t          *bs       = h264_imp->bs;
    int complete_au = 0;
    h264_new_access_unit( au );
    while( 1 )
    {
        h264_nalu_header_t nuh;
        uint64_t start_code_length;
        uint64_t trailing_zero_bytes;
        uint64_t nalu_length = h264_find_next_start_code( bs, &nuh, &start_code_length, &trailing_zero_bytes );
        if( start_code_length <= NALU_SHORT_START_CODE_LENGTH && lsmash_bs_is_end( bs, nalu_length ) )
        {
            /* For the last NALU.
             * This NALU already has been appended into the latest access unit and parsed. */
            h264_update_picture_info( info, picture, slice, &info->sei );
            complete_au = h264_complete_au( au, probe );
            if( complete_au )
                return h264_get_au_internal_succeeded( h264_imp, au );
            else
                return h264_get_au_internal_failed( h264_imp, au, complete_au );
        }
        uint8_t  nalu_type        = nuh.nal_unit_type;
        uint64_t next_sc_head_pos = h264_imp->sc_head_pos
                                  + start_code_length
                                  + nalu_length
                                  + trailing_zero_bytes;
#if 0
        if( probe )
        {
            fprintf( stderr, "NALU type: %"PRIu8"                    \n", nalu_type );
            fprintf( stderr, "    NALU header position: %"PRIx64"    \n", h264_imp->sc_head_pos + start_code_length );
            fprintf( stderr, "    EBSP position: %"PRIx64"           \n", h264_imp->sc_head_pos + start_code_length + nuh.length );
            fprintf( stderr, "    EBSP length: %"PRIx64" (%"PRIu64") \n", nalu_length - nuh.length, nalu_length - nuh.length );
            fprintf( stderr, "    trailing_zero_bytes: %"PRIx64"     \n", trailing_zero_bytes );
            fprintf( stderr, "    Next start code position: %"PRIx64"\n", next_sc_head_pos );
        }
#endif
        if( nalu_type == H264_NALU_TYPE_FD )
        {
            /* We don't support streams with both filler and HRD yet.
             * Otherwise, just skip filler because elemental streams defined in 14496-15 are forbidden to use filler. */
            if( info->sps.vui.hrd.present )
                return h264_get_au_internal_failed( h264_imp, au, complete_au );
        }
        else if( (nalu_type >= H264_NALU_TYPE_SLICE_N_IDR && nalu_type <= H264_NALU_TYPE_SPS_EXT)
              || nalu_type == H264_NALU_TYPE_SLICE_AUX )
        {
            /* Increase the buffer if needed. */
            uint64_t possible_au_length = au->incomplete_length + NALU_DEFAULT_NALU_LENGTH_SIZE + nalu_length;
            if( sb->bank->buffer_size < possible_au_length
             && h264_supplement_buffer( sb, au, 2 * possible_au_length ) < 0 )
            {
                lsmash_log( importer, LSMASH_LOG_ERROR, "failed to increase the buffer size.\n" );
                return h264_get_au_internal_failed( h264_imp, au, complete_au );
            }
            /* Get the EBSP of the current NALU here.
             * AVC elemental stream defined in 14496-15 can recognizes from 0 to 13, and 19 of nal_unit_type.
             * We don't support SVC and MVC elemental stream defined in 14496-15 yet. */
            uint8_t *nalu = lsmash_bs_get_buffer_data( bs ) + start_code_length;
            if( nalu_type >= H264_NALU_TYPE_SLICE_N_IDR && nalu_type <= H264_NALU_TYPE_SLICE_IDR )
            {
                /* VCL NALU (slice) */
                h264_slice_info_t prev_slice = *slice;
                if( h264_parse_slice( info, &nuh, sb->rbsp, nalu + nuh.length, nalu_length - nuh.length ) )
                    return h264_get_au_internal_failed( h264_imp, au, complete_au );
                if( probe && info->avcC_pending )
                {
                    /* Copy and append a Codec Specific info. */
                    if( h264_store_codec_specific( h264_imp, &info->avcC_param ) < 0 )
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
                        complete_au = h264_complete_au( au, probe );
                    }
                    else
                        h264_update_picture_info_for_slice( info, picture, &prev_slice );
                }
                h264_append_nalu_to_au( au, nalu, nalu_length, probe );
                slice->present = 1;
            }
            else
            {
                if( h264_find_au_delimit_by_nalu_type( nalu_type, info->prev_nalu_type ) )
                {
                    /* The last slice belongs to the AU you want at this time. */
                    h264_update_picture_info( info, picture, slice, &info->sei );
                    complete_au = h264_complete_au( au, probe );
                }
                switch( nalu_type )
                {
                    case H264_NALU_TYPE_SEI :
                    {
                        if( h264_parse_sei( info->bits, &info->sps, &info->sei, sb->rbsp, nalu + nuh.length, nalu_length - nuh.length ) < 0 )
                            return h264_get_au_internal_failed( h264_imp, au, complete_au );
                        h264_append_nalu_to_au( au, nalu, nalu_length, probe );
                        break;
                    }
                    case H264_NALU_TYPE_SPS :
                        if( h264_try_to_append_parameter_set( info, H264_PARAMETER_SET_TYPE_SPS, nalu, nalu_length ) < 0 )
                            return h264_get_au_internal_failed( h264_imp, au, complete_au );
                        break;
                    case H264_NALU_TYPE_PPS :
                        if( h264_try_to_append_parameter_set( info, H264_PARAMETER_SET_TYPE_PPS, nalu, nalu_length ) < 0 )
                            return h264_get_au_internal_failed( h264_imp, au, complete_au );
                        break;
                    case H264_NALU_TYPE_AUD :   /* We drop access unit delimiters. */
                        break;
                    case H264_NALU_TYPE_SPS_EXT :
                        if( h264_try_to_append_parameter_set( info, H264_PARAMETER_SET_TYPE_SPSEXT, nalu, nalu_length ) < 0 )
                            return h264_get_au_internal_failed( h264_imp, au, complete_au );
                        break;
                    default :
                        h264_append_nalu_to_au( au, nalu, nalu_length, probe );
                        break;
                }
                if( info->avcC_pending )
                    h264_imp->status = IMPORTER_CHANGE;
            }
        }
        /* Move to the first byte of the next start code. */
        info->prev_nalu_type = nalu_type;
        if( lsmash_bs_read_seek( bs, next_sc_head_pos, SEEK_SET ) != next_sc_head_pos )
        {
            lsmash_log( importer, LSMASH_LOG_ERROR, "failed to seek the next start code.\n" );
            return h264_get_au_internal_failed( h264_imp, au, complete_au );
        }
        /* Check if no more data to read from the stream. */
        if( !lsmash_bs_is_end( bs, NALU_SHORT_START_CODE_LENGTH ) )
            h264_imp->sc_head_pos = next_sc_head_pos;
        /* If there is no more data in the stream, and flushed chunk of NALUs, flush it as complete AU here. */
        else if( au->incomplete_length && au->length == 0 )
        {
            h264_update_picture_info( info, picture, slice, &info->sei );
            h264_complete_au( au, probe );
            return h264_get_au_internal_succeeded( h264_imp, au );
        }
        if( complete_au )
            return h264_get_au_internal_succeeded( h264_imp, au );
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
    h264_importer_t *h264_imp = (h264_importer_t *)importer->info;
    h264_info_t     *info     = &h264_imp->info;
    importer_status current_status = h264_imp->status;
    if( current_status == IMPORTER_ERROR || buffered_sample->length < h264_imp->max_au_length )
        return -1;
    if( current_status == IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( h264_get_access_unit_internal( importer, 0 ) < 0 )
    {
        h264_imp->status = IMPORTER_ERROR;
        return -1;
    }
    if( h264_imp->status == IMPORTER_CHANGE && !info->avcC_pending )
        current_status = IMPORTER_CHANGE;
    if( current_status == IMPORTER_CHANGE )
    {
        /* Update the active summary. */
        lsmash_codec_specific_t *cs = (lsmash_codec_specific_t *)lsmash_get_entry_data( h264_imp->avcC_list, ++ h264_imp->avcC_number );
        if( !cs )
            return -1;
        lsmash_h264_specific_parameters_t *avcC_param = (lsmash_h264_specific_parameters_t *)cs->data.structured;
        lsmash_video_summary_t *summary = h264_create_summary( avcC_param, &info->sps, h264_imp->max_au_length );
        if( !summary )
            return -1;
        lsmash_remove_entry( importer->summaries, track_number, lsmash_cleanup_summary );
        if( lsmash_add_entry( importer->summaries, summary ) < 0 )
        {
            lsmash_cleanup_summary( (lsmash_summary_t *)summary );
            return -1;
        }
        h264_imp->status = IMPORTER_OK;
    }
    h264_access_unit_t  *au      = &info->au;
    h264_picture_info_t *picture = &au->picture;
    buffered_sample->dts = h264_imp->ts_list.timestamp[ au->number - 1 ].dts;
    buffered_sample->cts = h264_imp->ts_list.timestamp[ au->number - 1 ].cts;
    if( au->number < h264_imp->num_undecodable )
        buffered_sample->prop.leading = ISOM_SAMPLE_IS_UNDECODABLE_LEADING;
    else
        buffered_sample->prop.leading = picture->independent || buffered_sample->cts >= h264_imp->last_intra_cts
                                      ? ISOM_SAMPLE_IS_NOT_LEADING : ISOM_SAMPLE_IS_UNDECODABLE_LEADING;
    if( picture->independent )
        h264_imp->last_intra_cts = buffered_sample->cts;
    if( h264_imp->composition_reordering_present && !picture->disposable && !picture->idr )
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
            buffered_sample->prop.post_roll.complete = (picture->frame_num + picture->recovery_frame_cnt) % info->sps.MaxFrameNum;
        }
        else
        {
            buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_RAP;
            if( !picture->broken_link_flag )
                buffered_sample->prop.ra_flags |= QT_SAMPLE_RANDOM_ACCESS_FLAG_PARTIAL_SYNC;
        }
    }
    buffered_sample->length = au->length;
    memcpy( buffered_sample->data, au->data, au->length );
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
        lsmash_class_t *logger = &(lsmash_class_t){ .name = importer->class->name };
        for( uint32_t i = 1; i < num_access_units; i++ )
            if( timestamp[i].cts > timestamp[i - 1].cts + npt[i - 1].poc_delta )
                lsmash_log( &logger, LSMASH_LOG_WARNING,
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

static lsmash_video_summary_t *h264_setup_first_summary
(
    importer_t *importer
)
{
    h264_importer_t *h264_imp = (h264_importer_t *)importer->info;
    lsmash_codec_specific_t *cs = (lsmash_codec_specific_t *)lsmash_get_entry_data( h264_imp->avcC_list, ++ h264_imp->avcC_number );
    if( !cs || !cs->data.structured )
    {
        lsmash_destroy_codec_specific_data( cs );
        return NULL;
    }
    lsmash_video_summary_t *summary = h264_create_summary( (lsmash_h264_specific_parameters_t *)cs->data.structured,
                                                           &h264_imp->info.sps, h264_imp->max_au_length );
    if( !summary )
    {
        lsmash_destroy_codec_specific_data( cs );
        return NULL;
    }
    if( lsmash_add_entry( importer->summaries, summary ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
    summary->sample_per_field = h264_imp->field_pic_present;
    return summary;
}

static int h264_analyze_whole_stream
(
    importer_t *importer
)
{
    /* Parse all NALU in the stream for preparation of calculating timestamps. */
    uint32_t npt_alloc = (1 << 12) * sizeof(nal_pic_timing_t);
    nal_pic_timing_t *npt = lsmash_malloc( npt_alloc );
    if( !npt )
        goto fail;
    uint32_t picture_stats[H264_PICTURE_TYPE_NONE + 1] = { 0 };
    uint32_t num_access_units = 0;
    lsmash_class_t *logger = &(lsmash_class_t){ "H.264" };
    lsmash_log( &logger, LSMASH_LOG_INFO, "Analyzing stream as H.264\r" );
    h264_importer_t *h264_imp = (h264_importer_t *)importer->info;
    h264_info_t     *info     = &h264_imp->info;
    h264_imp->status = IMPORTER_OK;
    while( h264_imp->status != IMPORTER_EOF )
    {
#if 0
        lsmash_log( &logger, LSMASH_LOG_INFO, "Analyzing stream as H.264: %"PRIu32"\n", num_access_units + 1 );
#endif
        h264_picture_info_t     *picture = &info->au.picture;
        h264_picture_info_t prev_picture = *picture;
        if( h264_get_access_unit_internal( importer, 1 )       < 0
         || h264_calculate_poc( info, picture, &prev_picture ) < 0 )
            goto fail;
        if( npt_alloc <= num_access_units * sizeof(nal_pic_timing_t) )
        {
            uint32_t alloc = 2 * num_access_units * sizeof(nal_pic_timing_t);
            nal_pic_timing_t *temp = (nal_pic_timing_t *)lsmash_realloc( npt, alloc );
            if( !temp )
                goto fail;
            npt       = temp;
            npt_alloc = alloc;
        }
        h264_imp->field_pic_present |= picture->field_pic_flag;
        npt[num_access_units].poc       = picture->PicOrderCnt;
        npt[num_access_units].delta     = picture->delta;
        npt[num_access_units].poc_delta = picture->field_pic_flag ? 1 : 2;
        npt[num_access_units].reset     = picture->has_mmco5;
        ++num_access_units;
        h264_imp->max_au_length = LSMASH_MAX( info->au.length, h264_imp->max_au_length );
        if( picture->idr )
            ++picture_stats[H264_PICTURE_TYPE_IDR];
        else if( picture->type >= H264_PICTURE_TYPE_NONE )
            ++picture_stats[H264_PICTURE_TYPE_NONE];
        else
            ++picture_stats[ picture->type ];
    }
    lsmash_log_refresh_line( &logger );
    lsmash_log( &logger, LSMASH_LOG_INFO,
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
    if( h264_store_codec_specific( h264_imp, &info->avcC_param ) < 0 )
        goto fail;
    /* Set up the first summary. */
    lsmash_video_summary_t *summary = h264_setup_first_summary( importer );
    if( !summary )
        goto fail;
    /* Allocate timestamps. */
    lsmash_media_ts_t *timestamp = lsmash_malloc( num_access_units * sizeof(lsmash_media_ts_t) );
    if( !timestamp )
        goto fail;
    /* Count leading samples that are undecodable. */
    for( uint32_t i = 0; i < num_access_units; i++ )
    {
        if( npt[i].poc == 0 )
            break;
        ++ h264_imp->num_undecodable;
    }
    /* Deduplicate POCs. */
    uint32_t max_composition_delay = 0;
    nalu_deduplicate_poc( npt, &max_composition_delay, num_access_units, 32 );
    /* Generate timestamps. */
    nalu_generate_timestamps_from_poc( importer, timestamp, npt,
                                       &h264_imp->composition_reordering_present,
                                       &h264_imp->last_delta,
                                       max_composition_delay, num_access_units );
    nalu_reduce_timescale( timestamp, npt, &h264_imp->last_delta, &summary->timescale, num_access_units );
    lsmash_free( npt );
    h264_imp->ts_list.sample_count = num_access_units;
    h264_imp->ts_list.timestamp    = timestamp;
    return 0;
fail:
    lsmash_log_refresh_line( &logger );
    lsmash_free( npt );
    return -1;
}

static int h264_importer_probe( importer_t *importer )
{
    /* Find the first start code. */
    h264_importer_t *h264_imp = create_h264_importer( importer );
    if( !h264_imp )
        return -1;
    lsmash_bs_t *bs = h264_imp->bs;
    uint64_t first_sc_head_pos = nalu_find_first_start_code( bs );
    if( first_sc_head_pos == NALU_NO_START_CODE_FOUND )
        goto fail;
    /* OK. It seems the stream has a long start code of H.264. */
    importer->info = h264_imp;
    h264_info_t *info = &h264_imp->info;
    lsmash_bs_read_seek( bs, first_sc_head_pos, SEEK_SET );
    h264_imp->sc_head_pos = first_sc_head_pos;
    if( h264_analyze_whole_stream( importer ) < 0 )
        goto fail;
    /* Go back to the start code of the first NALU. */
    h264_imp->status = IMPORTER_OK;
    lsmash_bs_read_seek( bs, first_sc_head_pos, SEEK_SET );
    h264_imp->sc_head_pos       = first_sc_head_pos;
    info->prev_nalu_type        = H264_NALU_TYPE_UNSPECIFIED0;
    uint8_t *temp_au            = info->au.data;
    uint8_t *temp_incomplete_au = info->au.incomplete_data;
    memset( &info->au, 0, sizeof(h264_access_unit_t) );
    info->au.data               = temp_au;
    info->au.incomplete_data    = temp_incomplete_au;
    memset( &info->slice, 0, sizeof(h264_slice_info_t) );
    memset( &info->sps, 0, sizeof(h264_sps_t) );
    memset( &info->pps, 0, sizeof(h264_pps_t) );
    lsmash_remove_entries( info->avcC_param.parameter_sets->sps_list,    isom_remove_dcr_ps );
    lsmash_remove_entries( info->avcC_param.parameter_sets->pps_list,    isom_remove_dcr_ps );
    lsmash_remove_entries( info->avcC_param.parameter_sets->spsext_list, isom_remove_dcr_ps );
    lsmash_destroy_h264_parameter_sets( &info->avcC_param_next );
    return 0;
fail:
    remove_h264_importer( h264_imp );
    importer->info = NULL;
    lsmash_remove_entries( importer->summaries, lsmash_cleanup_summary );
    return -1;
}

static uint32_t h264_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    h264_importer_t *h264_imp = (h264_importer_t *)importer->info;
    if( !h264_imp || track_number != 1 || h264_imp->status != IMPORTER_EOF )
        return 0;
    return h264_imp->ts_list.sample_count
         ? h264_imp->last_delta
         : UINT32_MAX;    /* arbitrary */
}

static const importer_functions h264_importer =
{
    { "H.264", offsetof( importer_t, log_level ) },
    1,
    h264_importer_probe,
    h264_importer_get_accessunit,
    h264_importer_get_last_delta,
    h264_importer_cleanup
};

/***************************************************************************
    HEVC importer
    ITU-T Recommendation H.265 (04/13)
    ISO/IEC 14496-15:2014
***************************************************************************/
#include "codecs/hevc.h"

typedef struct
{
    importer_status        status;
    hevc_info_t            info;
    lsmash_entry_list_t    hvcC_list[1];    /* stored as lsmash_codec_specific_t */
    lsmash_media_ts_list_t ts_list;
    lsmash_bs_t           *bs;
    uint32_t max_au_length;
    uint32_t num_undecodable;
    uint32_t hvcC_number;
    uint32_t last_delta;
    uint64_t last_intra_cts;
    uint64_t sc_head_pos;
    uint8_t  composition_reordering_present;
    uint8_t  field_pic_present;
    uint8_t  max_TemporalId;
} hevc_importer_t;

static void remove_hevc_importer( hevc_importer_t *hevc_imp )
{
    if( !hevc_imp )
        return;
    lsmash_remove_entries( hevc_imp->hvcC_list, lsmash_destroy_codec_specific_data );
    hevc_cleanup_parser( &hevc_imp->info );
    lsmash_bs_cleanup( hevc_imp->bs );
    lsmash_free( hevc_imp->ts_list.timestamp );
    lsmash_free( hevc_imp );
}

static void hevc_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        remove_hevc_importer( importer->info );
}

static hevc_importer_t *create_hevc_importer( importer_t *importer )
{
    hevc_importer_t *hevc_imp = lsmash_malloc_zero( sizeof(hevc_importer_t) );
    if( !hevc_imp )
        return NULL;
    if( hevc_setup_parser( &hevc_imp->info, 0 ) < 0 )
    {
        remove_hevc_importer( hevc_imp );
        return NULL;
    }
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
    {
        remove_hevc_importer( hevc_imp );
        return NULL;
    }
    bs->stream          = importer->stream;
    bs->read            = lsmash_fread_wrapper;
    bs->seek            = lsmash_fseek_wrapper;
    bs->unseekable      = importer->is_stdin;
    bs->buffer.max_size = BS_MAX_DEFAULT_READ_SIZE;
    lsmash_init_entry_list( hevc_imp->hvcC_list );
    hevc_imp->bs = bs;
    hevc_imp->info.eos = 1;
    return hevc_imp;
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
        uint8_t *dst_nalu = au->incomplete_data + au->incomplete_length + NALU_DEFAULT_NALU_LENGTH_SIZE;
        for( int i = NALU_DEFAULT_NALU_LENGTH_SIZE; i; i-- )
            *(dst_nalu - i) = (nalu_length >> ((i - 1) * 8)) & 0xff;
        memcpy( dst_nalu, src_nalu, nalu_length );
    }
    /* Note: picture->incomplete_au_length shall be 0 immediately after AU has completed.
     * Therefore, possible_au_length in hevc_get_access_unit_internal() can't be used here
     * to avoid increasing AU length monotonously through the entire stream. */
    au->incomplete_length += NALU_DEFAULT_NALU_LENGTH_SIZE + nalu_length;
}

static inline void hevc_get_au_internal_end( hevc_importer_t *hevc_imp, hevc_access_unit_t *au )
{
    if( lsmash_bs_is_end( hevc_imp->bs, 0 ) && (au->incomplete_length == 0) )
        hevc_imp->status = IMPORTER_EOF;
    else if( hevc_imp->status != IMPORTER_CHANGE )
        hevc_imp->status = IMPORTER_OK;
}

static int hevc_get_au_internal_succeeded( hevc_importer_t *hevc_imp, hevc_access_unit_t *au )
{
    hevc_get_au_internal_end( hevc_imp, au );
    au->number += 1;
    return 0;
}

static int hevc_get_au_internal_failed( hevc_importer_t *hevc_imp, hevc_access_unit_t *au, int complete_au )
{
    hevc_get_au_internal_end( hevc_imp, au );
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
     || lsmash_add_entry( &summary->opaque->list, specific ) < 0 )
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
    hevc_importer_t                   *hevc_imp,
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
    if( !dst_cs || lsmash_add_entry( hevc_imp->hvcC_list, dst_cs ) < 0 )
    {
        lsmash_destroy_codec_specific_data( dst_cs );
        return -1;
    }
    return 0;
}

static inline void hevc_new_access_unit( hevc_access_unit_t *au )
{
    au->length                    = 0;
    au->picture.type              = HEVC_PICTURE_TYPE_NONE;
    au->picture.random_accessible = 0;
    au->picture.recovery_poc_cnt  = 0;
}

/* If probe equals 0, don't get the actual data (EBPS) of an access unit and only parse NALU. */
static int hevc_get_access_unit_internal
(
    importer_t *importer,
    int         probe
)
{
    hevc_importer_t      *hevc_imp = (hevc_importer_t *)importer->info;
    hevc_info_t          *info     = &hevc_imp->info;
    hevc_slice_info_t    *slice    = &info->slice;
    hevc_access_unit_t   *au       = &info->au;
    hevc_picture_info_t  *picture  = &au->picture;
    hevc_stream_buffer_t *sb       = &info->buffer;
    lsmash_bs_t          *bs       = hevc_imp->bs;
    int complete_au = 0;
    hevc_new_access_unit( au );
    while( 1 )
    {
        hevc_nalu_header_t nuh;
        uint64_t start_code_length;
        uint64_t trailing_zero_bytes;
        uint64_t nalu_length = hevc_find_next_start_code( bs, &nuh, &start_code_length, &trailing_zero_bytes );
        if( start_code_length <= NALU_SHORT_START_CODE_LENGTH && lsmash_bs_is_end( bs, nalu_length ) )
        {
            /* For the last NALU.
             * This NALU already has been appended into the latest access unit and parsed. */
            hevc_update_picture_info( info, picture, slice, &info->sps, &info->sei );
            complete_au = hevc_complete_au( au, probe );
            if( complete_au )
                return hevc_get_au_internal_succeeded( hevc_imp, au );
            else
                return hevc_get_au_internal_failed( hevc_imp, au, complete_au );
        }
        uint8_t  nalu_type        = nuh.nal_unit_type;
        uint64_t next_sc_head_pos = hevc_imp->sc_head_pos
                                  + start_code_length
                                  + nalu_length
                                  + trailing_zero_bytes;
#if 0
        if( probe )
        {
            fprintf( stderr, "NALU type: %"PRIu8"                    \n", nalu_type );
            fprintf( stderr, "    NALU header position: %"PRIx64"    \n", hevc_imp->sc_head_pos + start_code_length );
            fprintf( stderr, "    EBSP position: %"PRIx64"           \n", hevc_imp->sc_head_pos + start_code_length + nuh.length );
            fprintf( stderr, "    EBSP length: %"PRIx64" (%"PRIu64") \n", nalu_length - nuh.length, nalu_length - nuh.length );
            fprintf( stderr, "    trailing_zero_bytes: %"PRIx64"     \n", trailing_zero_bytes );
            fprintf( stderr, "    Next start code position: %"PRIx64"\n", next_sc_head_pos );
        }
#endif
        /* Check if the end of sequence. Used for POC calculation. */
        info->eos |= info->prev_nalu_type == HEVC_NALU_TYPE_EOS
                  || info->prev_nalu_type == HEVC_NALU_TYPE_EOB;
        /* Process the current NALU by its type. */
        if( nalu_type == HEVC_NALU_TYPE_FD )
        {
            /* We don't support streams with both filler and HRD yet.
             * Otherwise, just skip filler because elemental streams defined in 14496-15 are forbidden to use filler. */
            if( info->sps.vui.hrd.present )
                return hevc_get_au_internal_failed( hevc_imp, au, complete_au );
        }
        else if( nalu_type <= HEVC_NALU_TYPE_RASL_R
             || (nalu_type >= HEVC_NALU_TYPE_BLA_W_LP && nalu_type <= HEVC_NALU_TYPE_CRA)
             || (nalu_type >= HEVC_NALU_TYPE_VPS      && nalu_type <= HEVC_NALU_TYPE_SUFFIX_SEI)  )
        {
            /* Increase the buffer if needed. */
            uint64_t possible_au_length = au->incomplete_length + NALU_DEFAULT_NALU_LENGTH_SIZE + nalu_length;
            if( sb->bank->buffer_size < possible_au_length
             && hevc_supplement_buffer( sb, au, 2 * possible_au_length ) < 0 )
            {
                lsmash_log( importer, LSMASH_LOG_ERROR, "failed to increase the buffer size.\n" );
                return hevc_get_au_internal_failed( hevc_imp, au, complete_au );
            }
            /* Get the EBSP of the current NALU here. */
            uint8_t *nalu = lsmash_bs_get_buffer_data( bs ) + start_code_length;
            if( nalu_type <= HEVC_NALU_TYPE_RSV_VCL31 )
            {
                /* VCL NALU (slice) */
                hevc_slice_info_t prev_slice = *slice;
                if( hevc_parse_slice_segment_header( info, &nuh, sb->rbsp,
                                                     nalu + nuh.length, nalu_length - nuh.length ) )
                    return hevc_get_au_internal_failed( hevc_imp, au, complete_au );
                if( probe && info->hvcC_pending )
                {
                    /* Copy and append a Codec Specific info. */
                    if( hevc_store_codec_specific( hevc_imp, &info->hvcC_param ) < 0 )
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
                hevc_append_nalu_to_au( au, nalu, nalu_length, probe );
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
                switch( nalu_type )
                {
                    case HEVC_NALU_TYPE_PREFIX_SEI :
                    case HEVC_NALU_TYPE_SUFFIX_SEI :
                    {
                        if( hevc_parse_sei( info->bits, &info->vps, &info->sps, &info->sei, &nuh,
                                            sb->rbsp, nalu + nuh.length, nalu_length - nuh.length ) < 0 )
                            return hevc_get_au_internal_failed( hevc_imp, au, complete_au );
                        hevc_append_nalu_to_au( au, nalu, nalu_length, probe );
                        break;
                    }
                    case HEVC_NALU_TYPE_VPS :
                        if( hevc_try_to_append_dcr_nalu( info, HEVC_DCR_NALU_TYPE_VPS, nalu, nalu_length ) < 0 )
                            return hevc_get_au_internal_failed( hevc_imp, au, complete_au );
                        break;
                    case HEVC_NALU_TYPE_SPS :
                        if( hevc_try_to_append_dcr_nalu( info, HEVC_DCR_NALU_TYPE_SPS, nalu, nalu_length ) < 0 )
                            return hevc_get_au_internal_failed( hevc_imp, au, complete_au );
                        break;
                    case HEVC_NALU_TYPE_PPS :
                        if( hevc_try_to_append_dcr_nalu( info, HEVC_DCR_NALU_TYPE_PPS, nalu, nalu_length ) < 0 )
                            return hevc_get_au_internal_failed( hevc_imp, au, complete_au );
                        break;
                    case HEVC_NALU_TYPE_AUD :   /* We drop access unit delimiters. */
                        break;
                    default :
                        hevc_append_nalu_to_au( au, nalu, nalu_length, probe );
                        break;
                }
                if( info->hvcC_pending )
                    hevc_imp->status = IMPORTER_CHANGE;
            }
        }
        /* Move to the first byte of the next start code. */
        info->prev_nalu_type = nalu_type;
        if( lsmash_bs_read_seek( bs, next_sc_head_pos, SEEK_SET ) != next_sc_head_pos )
        {
            lsmash_log( importer, LSMASH_LOG_ERROR, "failed to seek the next start code.\n" );
            return hevc_get_au_internal_failed( hevc_imp, au, complete_au );
        }
        if( !lsmash_bs_is_end( bs, NALU_SHORT_START_CODE_LENGTH ) )
            hevc_imp->sc_head_pos = next_sc_head_pos;
        /* If there is no more data in the stream, and flushed chunk of NALUs, flush it as complete AU here. */
        else if( au->incomplete_length && au->length == 0 )
        {
            hevc_update_picture_info( info, picture, slice, &info->sps, &info->sei );
            hevc_complete_au( au, probe );
            return hevc_get_au_internal_succeeded( hevc_imp, au );
        }
        if( complete_au )
            return hevc_get_au_internal_succeeded( hevc_imp, au );
    }
}

static int hevc_importer_get_accessunit( importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    hevc_importer_t *hevc_imp = (hevc_importer_t *)importer->info;
    hevc_info_t     *info     = &hevc_imp->info;
    importer_status current_status = hevc_imp->status;
    if( current_status == IMPORTER_ERROR || buffered_sample->length < hevc_imp->max_au_length )
        return -1;
    if( current_status == IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( hevc_get_access_unit_internal( importer, 0 ) < 0 )
    {
        hevc_imp->status = IMPORTER_ERROR;
        return -1;
    }
    if( hevc_imp->status == IMPORTER_CHANGE && !info->hvcC_pending )
        current_status = IMPORTER_CHANGE;
    if( current_status == IMPORTER_CHANGE )
    {
        /* Update the active summary. */
        lsmash_codec_specific_t *cs = (lsmash_codec_specific_t *)lsmash_get_entry_data( hevc_imp->hvcC_list, ++ hevc_imp->hvcC_number );
        if( !cs )
            return -1;
        lsmash_hevc_specific_parameters_t *hvcC_param = (lsmash_hevc_specific_parameters_t *)cs->data.structured;
        lsmash_video_summary_t *summary = hevc_create_summary( hvcC_param, &info->sps, hevc_imp->max_au_length );
        if( !summary )
            return -1;
        lsmash_remove_entry( importer->summaries, track_number, lsmash_cleanup_summary );
        if( lsmash_add_entry( importer->summaries, summary ) )
        {
            lsmash_cleanup_summary( (lsmash_summary_t *)summary );
            return -1;
        }
        hevc_imp->status = IMPORTER_OK;
    }
    //hevc_sps_t *sps = &info->sps;
    hevc_access_unit_t  *au      = &info->au;
    hevc_picture_info_t *picture = &au->picture;
    buffered_sample->dts = hevc_imp->ts_list.timestamp[ au->number - 1 ].dts;
    buffered_sample->cts = hevc_imp->ts_list.timestamp[ au->number - 1 ].cts;
    /* Set property of disposability. */
    if( picture->sublayer_nonref && au->TemporalId == hevc_imp->max_TemporalId )
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
        if( au->number < hevc_imp->num_undecodable )
            buffered_sample->prop.leading = ISOM_SAMPLE_IS_UNDECODABLE_LEADING;
        else
        {
            if( picture->independent || buffered_sample->cts >= hevc_imp->last_intra_cts )
                buffered_sample->prop.leading = ISOM_SAMPLE_IS_NOT_LEADING;
            else
                buffered_sample->prop.leading = ISOM_SAMPLE_IS_UNDECODABLE_LEADING;
        }
    }
    if( picture->independent )
        hevc_imp->last_intra_cts = buffered_sample->cts;
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

static lsmash_video_summary_t *hevc_setup_first_summary
(
    importer_t *importer
)
{
    hevc_importer_t *hevc_imp = (hevc_importer_t *)importer->info;
    lsmash_codec_specific_t *cs = (lsmash_codec_specific_t *)lsmash_get_entry_data( hevc_imp->hvcC_list, ++ hevc_imp->hvcC_number );
    if( !cs || !cs->data.structured )
    {
        lsmash_destroy_codec_specific_data( cs );
        return NULL;
    }
    lsmash_video_summary_t *summary = hevc_create_summary( (lsmash_hevc_specific_parameters_t *)cs->data.structured,
                                                           &hevc_imp->info.sps, hevc_imp->max_au_length );
    if( !summary )
    {
        lsmash_destroy_codec_specific_data( cs );
        return NULL;
    }
    if( lsmash_add_entry( importer->summaries, summary ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
    summary->sample_per_field = hevc_imp->field_pic_present;
    return summary;
}

static int hevc_analyze_whole_stream
(
    importer_t *importer
)
{
    /* Parse all NALU in the stream for preparation of calculating timestamps. */
    uint32_t npt_alloc = (1 << 12) * sizeof(nal_pic_timing_t);
    nal_pic_timing_t *npt = (nal_pic_timing_t *)lsmash_malloc( npt_alloc );
    if( !npt )
        return -1;
    uint32_t picture_stats[HEVC_PICTURE_TYPE_NONE + 1] = { 0 };
    uint32_t num_access_units = 0;
    lsmash_class_t *logger = &(lsmash_class_t){ "HEVC" };
    lsmash_log( &logger, LSMASH_LOG_INFO, "Analyzing stream as HEVC\r" );
    hevc_importer_t *hevc_imp = (hevc_importer_t *)importer->info;
    hevc_info_t     *info     = &hevc_imp->info;
    hevc_imp->status = IMPORTER_OK;
    while( hevc_imp->status != IMPORTER_EOF )
    {
#if 0
        lsmash_log( &logger, LSMASH_LOG_INFO, "Analyzing stream as HEVC: %"PRIu32"\n", num_access_units + 1 );
#endif
        hevc_picture_info_t     *picture = &info->au.picture;
        hevc_picture_info_t prev_picture = *picture;
        if( hevc_get_access_unit_internal( importer, 1 )                 < 0
         || hevc_calculate_poc( info, &info->au.picture, &prev_picture ) < 0 )
            goto fail;
        if( npt_alloc <= num_access_units * sizeof(nal_pic_timing_t) )
        {
            uint32_t alloc = 2 * num_access_units * sizeof(nal_pic_timing_t);
            nal_pic_timing_t *temp = (nal_pic_timing_t *)lsmash_realloc( npt, alloc );
            if( !temp )
                goto fail;
            npt = temp;
            npt_alloc = alloc;
        }
        hevc_imp->field_pic_present |= picture->field_coded;
        npt[num_access_units].poc       = picture->poc;
        npt[num_access_units].delta     = picture->delta;
        npt[num_access_units].poc_delta = 1;
        npt[num_access_units].reset     = 0;
        ++num_access_units;
        hevc_imp->max_au_length  = LSMASH_MAX( hevc_imp->max_au_length,  info->au.length );
        hevc_imp->max_TemporalId = LSMASH_MAX( hevc_imp->max_TemporalId, info->au.TemporalId );
        if( picture->idr )
            ++picture_stats[HEVC_PICTURE_TYPE_IDR];
        else if( picture->irap )
            ++picture_stats[ picture->broken_link ? HEVC_PICTURE_TYPE_BLA : HEVC_PICTURE_TYPE_CRA ];
        else if( picture->type >= HEVC_PICTURE_TYPE_NONE )
            ++picture_stats[HEVC_PICTURE_TYPE_NONE];
        else
            ++picture_stats[ picture->type ];
    }
    lsmash_log_refresh_line( &logger );
    lsmash_log( &logger, LSMASH_LOG_INFO,
                "IDR: %"PRIu32", CRA: %"PRIu32", BLA: %"PRIu32", I: %"PRIu32", P: %"PRIu32", B: %"PRIu32", Unknown: %"PRIu32"\n",
                picture_stats[HEVC_PICTURE_TYPE_IDR], picture_stats[HEVC_PICTURE_TYPE_CRA],
                picture_stats[HEVC_PICTURE_TYPE_BLA], picture_stats[HEVC_PICTURE_TYPE_I],
                picture_stats[HEVC_PICTURE_TYPE_I_P], picture_stats[HEVC_PICTURE_TYPE_I_P_B],
                picture_stats[HEVC_PICTURE_TYPE_NONE]);
    /* Copy and append the last Codec Specific info. */
    if( hevc_store_codec_specific( hevc_imp, &info->hvcC_param ) < 0 )
        goto fail;
    /* Set up the first summary. */
    lsmash_video_summary_t *summary = hevc_setup_first_summary( importer );
    if( !summary )
        goto fail;
    /* */
    lsmash_media_ts_t *timestamp = lsmash_malloc( num_access_units * sizeof(lsmash_media_ts_t) );
    if( !timestamp )
        goto fail;
    /* Count leading samples that are undecodable. */
    for( uint32_t i = 0; i < num_access_units; i++ )
    {
        if( npt[i].poc == 0 )
            break;
        ++ hevc_imp->num_undecodable;
    }
    /* Deduplicate POCs. */
    uint32_t max_composition_delay = 0;
    nalu_deduplicate_poc( npt, &max_composition_delay, num_access_units, 15 );
    /* Generate timestamps. */
    nalu_generate_timestamps_from_poc( importer, timestamp, npt,
                                       &hevc_imp->composition_reordering_present,
                                       &hevc_imp->last_delta,
                                       max_composition_delay, num_access_units );
    summary->timescale *= 2;    /* We assume that picture timing is in field level.
                                 * For HEVC, it seems time_scale is set in frame level basically.
                                 * So multiply by 2 for reducing timebase and timescale. */
    nalu_reduce_timescale( timestamp, npt, &hevc_imp->last_delta, &summary->timescale, num_access_units );
    lsmash_free( npt );
    hevc_imp->ts_list.sample_count = num_access_units;
    hevc_imp->ts_list.timestamp    = timestamp;
    return 0;
fail:
    lsmash_log_refresh_line( &logger );
    lsmash_free( npt );
    return -1;
}

static int hevc_importer_probe( importer_t *importer )
{
    /* Find the first start code. */
    hevc_importer_t *hevc_imp = create_hevc_importer( importer );
    if( !hevc_imp )
        return -1;
    lsmash_bs_t *bs = hevc_imp->bs;
    uint64_t first_sc_head_pos = nalu_find_first_start_code( bs );
    if( first_sc_head_pos == NALU_NO_START_CODE_FOUND )
        goto fail;
    /* OK. It seems the stream has a long start code of HEVC. */
    importer->info = hevc_imp;
    hevc_info_t *info = &hevc_imp->info;
    lsmash_bs_read_seek( bs, first_sc_head_pos, SEEK_SET );
    hevc_imp->sc_head_pos = first_sc_head_pos;
    if( hevc_analyze_whole_stream( importer ) < 0 )
        goto fail;
    /* Go back to the start code of the first NALU. */
    hevc_imp->status = IMPORTER_OK;
    lsmash_bs_read_seek( bs, first_sc_head_pos, SEEK_SET );
    hevc_imp->sc_head_pos       = first_sc_head_pos;
    info->prev_nalu_type        = HEVC_NALU_TYPE_UNKNOWN;
    uint8_t *temp_au            = info->au.data;
    uint8_t *temp_incomplete_au = info->au.incomplete_data;
    memset( &info->au, 0, sizeof(hevc_access_unit_t) );
    info->au.data            = temp_au;
    info->au.incomplete_data = temp_incomplete_au;
    memset( &info->slice, 0, sizeof(hevc_slice_info_t) );
    memset( &info->vps,   0, sizeof(hevc_vps_t) );
    memset( &info->sps,   0, sizeof(hevc_sps_t) );
    memset( &info->pps,   0, SIZEOF_PPS_EXCLUDING_HEAP );
    for( int i = 0; i < HEVC_DCR_NALU_TYPE_NUM; i++ )
        lsmash_remove_entries( info->hvcC_param.parameter_arrays->ps_array[i].list, isom_remove_dcr_ps );
    lsmash_destroy_hevc_parameter_arrays( &info->hvcC_param_next );
    return 0;
fail:
    remove_hevc_importer( hevc_imp );
    importer->info = NULL;
    lsmash_remove_entries( importer->summaries, lsmash_cleanup_summary );
    return -1;
}

static uint32_t hevc_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    hevc_importer_t *hevc_imp = (hevc_importer_t *)importer->info;
    if( !hevc_imp || track_number != 1 || hevc_imp->status != IMPORTER_EOF )
        return 0;
    return hevc_imp->ts_list.sample_count
         ? hevc_imp->last_delta
         : UINT32_MAX;    /* arbitrary */
}

static const importer_functions hevc_importer =
{
    { "HEVC", offsetof( importer_t, log_level ) },
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
#include "codecs/vc1.h"

typedef struct
{
    importer_status        status;
    vc1_info_t             info;
    vc1_sequence_header_t  first_sequence;
    lsmash_media_ts_list_t ts_list;
    lsmash_bs_t           *bs;
    uint8_t  composition_reordering_present;
    uint32_t max_au_length;
    uint32_t num_undecodable;
    uint64_t last_ref_intra_cts;
} vc1_importer_t;

static void remove_vc1_importer( vc1_importer_t *vc1_imp )
{
    if( !vc1_imp )
        return;
    vc1_cleanup_parser( &vc1_imp->info );
    lsmash_bs_cleanup( vc1_imp->bs );
    lsmash_free( vc1_imp->ts_list.timestamp );
    lsmash_free( vc1_imp );
}

static void vc1_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        remove_vc1_importer( importer->info );
}

static vc1_importer_t *create_vc1_importer( importer_t *importer )
{
    vc1_importer_t *vc1_imp = lsmash_malloc_zero( sizeof(vc1_importer_t) );
    if( !vc1_imp )
        return NULL;
    if( vc1_setup_parser( &vc1_imp->info, 0 ) < 0 )
    {
        remove_vc1_importer( vc1_imp );
        return NULL;
    }
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
    {
        remove_vc1_importer( vc1_imp );
        return NULL;
    }
    bs->stream          = importer->stream;
    bs->read            = lsmash_fread_wrapper;
    bs->seek            = lsmash_fseek_wrapper;
    bs->unseekable      = importer->is_stdin;
    bs->buffer.max_size = BS_MAX_DEFAULT_READ_SIZE;
    vc1_imp->bs = bs;
    return vc1_imp;
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

static inline void vc1_get_au_internal_end( vc1_importer_t *vc1_imp, vc1_access_unit_t *access_unit )
{
    vc1_imp->status = lsmash_bs_is_end( vc1_imp->bs, 0 ) && (access_unit->incomplete_data_length == 0)
                    ? IMPORTER_EOF
                    : IMPORTER_OK;
}

static int vc1_get_au_internal_succeeded( vc1_importer_t *vc1_imp )
{
    vc1_access_unit_t *access_unit = &vc1_imp->info.access_unit;
    vc1_get_au_internal_end( vc1_imp, access_unit );
    access_unit->number += 1;
    return 0;
}

static int vc1_get_au_internal_failed( vc1_importer_t *vc1_imp, int complete_au )
{
    vc1_access_unit_t *access_unit = &vc1_imp->info.access_unit;
    vc1_get_au_internal_end( vc1_imp, access_unit );
    if( complete_au )
        access_unit->number += 1;
    return -1;
}

static int vc1_importer_get_access_unit_internal( importer_t *importer, int probe )
{
    vc1_importer_t      *vc1_imp     = (vc1_importer_t *)importer->info;
    vc1_info_t          *info        = &vc1_imp->info;
    vc1_stream_buffer_t *sb          = &info->buffer;
    vc1_access_unit_t   *access_unit = &info->access_unit;
    lsmash_bs_t         *bs          = vc1_imp->bs;
    int                  complete_au = 0;
    access_unit->data_length = 0;
    while( 1 )
    {
        uint8_t  bdu_type;
        uint64_t trailing_zero_bytes;
        uint64_t ebdu_length = vc1_find_next_start_code_prefix( bs, &bdu_type, &trailing_zero_bytes );
        if( ebdu_length <= VC1_START_CODE_LENGTH && lsmash_bs_is_end( bs, ebdu_length ) )
        {
            /* For the last EBDU.
             * This EBDU already has been appended into the latest access unit and parsed. */
            vc1_complete_au( access_unit, &info->picture, probe );
            return vc1_get_au_internal_succeeded( vc1_imp );
        }
        else if( bdu_type == 0xFF )
        {
            lsmash_log( importer, LSMASH_LOG_ERROR, "a forbidden BDU type is detected.\n" );
            return vc1_get_au_internal_failed( vc1_imp, complete_au );
        }
        uint64_t next_ebdu_head_pos = info->ebdu_head_pos
                                    + ebdu_length
                                    + trailing_zero_bytes;
#if 0
        if( probe )
        {
            fprintf( stderr, "BDU type: %"PRIu8"                    \n", bdu_type );
            fprintf( stderr, "    EBDU position: %"PRIx64"          \n", info->ebdu_head_pos );
            fprintf( stderr, "    EBDU length: %"PRIx64" (%"PRIu64")\n", ebdu_length, ebdu_length );
            fprintf( stderr, "    trailing_zero_bytes: %"PRIx64"    \n", trailing_zero_bytes );
            fprintf( stderr, "    Next EBDU position: %"PRIx64"     \n", next_ebdu_head_pos );
        }
#endif
        if( bdu_type >= 0x0A && bdu_type <= 0x0F )
        {
            /* Complete the current access unit if encountered delimiter of current access unit. */
            if( vc1_find_au_delimit_by_bdu_type( bdu_type, info->prev_bdu_type ) )
                /* The last video coded EBDU belongs to the access unit you want at this time. */
                complete_au = vc1_complete_au( access_unit, &info->picture, probe );
            /* Increase the buffer if needed. */
            uint64_t possible_au_length = access_unit->incomplete_data_length + ebdu_length;
            if( sb->bank->buffer_size < possible_au_length
             && vc1_supplement_buffer( sb, access_unit, 2 * possible_au_length ) < 0 )
            {
                lsmash_log( importer, LSMASH_LOG_ERROR, "failed to increase the buffer size.\n" );
                return vc1_get_au_internal_failed( vc1_imp, complete_au );
            }
            /* Process EBDU by its BDU type and append it to access unit. */
            uint8_t *ebdu = lsmash_bs_get_buffer_data( bs );
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
                    if( vc1_parse_advanced_picture( info->bits, &info->sequence, &info->picture, sb->rbdu, ebdu, ebdu_length ) < 0 )
                    {
                        lsmash_log( importer, LSMASH_LOG_ERROR, "failed to parse a frame.\n" );
                        return vc1_get_au_internal_failed( vc1_imp, complete_au );
                    }
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
                    if( vc1_parse_entry_point_header( info, ebdu, ebdu_length, probe ) < 0 )
                    {
                        lsmash_log( importer, LSMASH_LOG_ERROR, "failed to parse an entry point.\n" );
                        return vc1_get_au_internal_failed( vc1_imp, complete_au );
                    }
                    /* Signal random access type of the frame that follows this entry-point header. */
                    info->picture.closed_gop        = info->entry_point.closed_entry_point;
                    info->picture.random_accessible = info->dvc1_param.multiple_sequence ? info->picture.start_of_sequence : 1;
                    break;
                case 0x0F : /* Sequence header
                             * [SEQ_SC][SEQ_L][EP_SC][EP_L][FRM_SC][PIC_L] ... */
                    if( vc1_parse_sequence_header( info, ebdu, ebdu_length, probe ) < 0 )
                    {
                        lsmash_log( importer, LSMASH_LOG_ERROR, "failed to parse a sequence header.\n" );
                        return vc1_get_au_internal_failed( vc1_imp, complete_au );
                    }
                    /* The frame that is the first frame after this sequence header shall be a random accessible point. */
                    info->picture.start_of_sequence = 1;
                    if( probe && !vc1_imp->first_sequence.present )
                        vc1_imp->first_sequence = info->sequence;
                    break;
                default :   /* End-of-sequence (0x0A) */
                    break;
            }
            /* Append the current EBDU into the end of an incomplete access unit. */
            vc1_append_ebdu_to_au( access_unit, ebdu, ebdu_length, probe );
        }
        else    /* We don't support other BDU types such as user data yet. */
            return vc1_get_au_internal_failed( vc1_imp, complete_au );
        /* Move to the first byte of the next EBDU. */
        info->prev_bdu_type = bdu_type;
        if( lsmash_bs_read_seek( bs, next_ebdu_head_pos, SEEK_SET ) != next_ebdu_head_pos )
        {
            lsmash_log( importer, LSMASH_LOG_ERROR, "failed to seek the next start code suffix.\n" );
            return vc1_get_au_internal_failed( vc1_imp, complete_au );
        }
        /* Check if no more data to read from the stream. */
        if( !lsmash_bs_is_end( bs, VC1_START_CODE_PREFIX_LENGTH ) )
            info->ebdu_head_pos = next_ebdu_head_pos;
        /* If there is no more data in the stream, and flushed chunk of EBDUs, flush it as complete AU here. */
        else if( access_unit->incomplete_data_length && access_unit->data_length == 0 )
        {
            vc1_complete_au( access_unit, &info->picture, probe );
            return vc1_get_au_internal_succeeded( vc1_imp );
        }
        if( complete_au )
            return vc1_get_au_internal_succeeded( vc1_imp );
    }
}

static int vc1_importer_get_accessunit( importer_t *importer, uint32_t track_number, lsmash_sample_t *buffered_sample )
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( !importer->info || track_number != 1 )
        return -1;
    vc1_importer_t *vc1_imp = (vc1_importer_t *)importer->info;
    vc1_info_t     *info    = &vc1_imp->info;
    importer_status current_status = vc1_imp->status;
    if( current_status == IMPORTER_ERROR || buffered_sample->length < vc1_imp->max_au_length )
        return -1;
    if( current_status == IMPORTER_EOF )
    {
        buffered_sample->length = 0;
        return 0;
    }
    if( vc1_importer_get_access_unit_internal( importer, 0 ) < 0 )
    {
        vc1_imp->status = IMPORTER_ERROR;
        return -1;
    }
    vc1_access_unit_t *access_unit = &info->access_unit;
    buffered_sample->dts = vc1_imp->ts_list.timestamp[ access_unit->number - 1 ].dts;
    buffered_sample->cts = vc1_imp->ts_list.timestamp[ access_unit->number - 1 ].cts;
    buffered_sample->prop.leading = access_unit->independent
                                 || access_unit->non_bipredictive
                                 || buffered_sample->cts >= vc1_imp->last_ref_intra_cts
                                  ? ISOM_SAMPLE_IS_NOT_LEADING : ISOM_SAMPLE_IS_UNDECODABLE_LEADING;
    if( access_unit->independent && !access_unit->disposable )
        vc1_imp->last_ref_intra_cts = buffered_sample->cts;
    if( vc1_imp->composition_reordering_present && !access_unit->disposable && !access_unit->closed_gop )
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
     || lsmash_add_entry( &summary->opaque->list, specific ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        return NULL;
    }
    summary->sample_type           = ISOM_CODEC_TYPE_VC_1_VIDEO;
    summary->max_au_length         = max_au_length;
    summary->timescale             = sequence->framerate_numerator;
    summary->timebase              = sequence->framerate_denominator;
    summary->vfr                   = !sequence->framerate_flag;
    summary->sample_per_field      = 0;
    summary->width                 = sequence->disp_horiz_size;
    summary->height                = sequence->disp_vert_size;
    summary->par_h                 = sequence->aspect_width;
    summary->par_v                 = sequence->aspect_height;
    summary->color.primaries_index = sequence->color_prim;
    summary->color.transfer_index  = sequence->transfer_char;
    summary->color.matrix_index    = sequence->matrix_coef;
    return summary;
}

static int vc1_analyze_whole_stream
(
    importer_t *importer
)
{
    /* Parse all EBDU in the stream for preparation of calculating timestamps. */
    uint32_t cts_alloc = (1 << 12) * sizeof(uint64_t);
    uint64_t *cts = lsmash_malloc( cts_alloc );
    if( !cts )
        return -1;  /* Failed to allocate CTS list */
    uint32_t num_access_units  = 0;
    uint32_t num_consecutive_b = 0;
    lsmash_class_t *logger = &(lsmash_class_t){ "VC-1" };
    lsmash_log( &logger, LSMASH_LOG_INFO, "Analyzing stream as VC-1\r" );
    vc1_importer_t *vc1_imp = (vc1_importer_t *)importer->info;
    vc1_info_t     *info    = &vc1_imp->info;
    vc1_imp->status = IMPORTER_OK;
    while( vc1_imp->status != IMPORTER_EOF )
    {
#if 0
        lsmash_log( &logger, LSMASH_LOG_INFO, "Analyzing stream as VC-1: %"PRIu32"\n", num_access_units + 1 );
#endif
        if( vc1_importer_get_access_unit_internal( importer, 1 ) < 0 )
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
            info->dvc1_param.bframe_present = 1;
        }
        if( cts_alloc <= num_access_units * sizeof(uint64_t) )
        {
            uint32_t alloc = 2 * num_access_units * sizeof(uint64_t);
            uint64_t *temp = lsmash_realloc( cts, alloc );
            if( !temp )
                goto fail;  /* Failed to re-allocate CTS list */
            cts = temp;
            cts_alloc = alloc;
        }
        vc1_imp->max_au_length = LSMASH_MAX( info->access_unit.data_length, vc1_imp->max_au_length );
        ++num_access_units;
    }
    if( num_access_units > num_consecutive_b )
        cts[ num_access_units - num_consecutive_b - 1 ] = num_access_units;
    else
        goto fail;
    /* Construct timestamps. */
    lsmash_media_ts_t *timestamp = lsmash_malloc( num_access_units * sizeof(lsmash_media_ts_t) );
    if( !timestamp )
        goto fail;  /* Failed to allocate timestamp list */
    for( uint32_t i = 1; i < num_access_units; i++ )
        if( cts[i] < cts[i - 1] )
        {
            vc1_imp->composition_reordering_present = 1;
            break;
        }
    if( vc1_imp->composition_reordering_present )
        for( uint32_t i = 0; i < num_access_units; i++ )
        {
            timestamp[i].cts = cts[i];
            timestamp[i].dts = i;
        }
    else
        for( uint32_t i = 0; i < num_access_units; i++ )
            timestamp[i].cts = timestamp[i].dts = i;
    lsmash_free( cts );
    lsmash_log_refresh_line( &logger );
#if 0
    for( uint32_t i = 0; i < num_access_units; i++ )
        fprintf( stderr, "Timestamp[%"PRIu32"]: DTS=%"PRIu64", CTS=%"PRIu64"\n", i, timestamp[i].dts, timestamp[i].cts );
#endif
    vc1_imp->ts_list.sample_count = num_access_units;
    vc1_imp->ts_list.timestamp    = timestamp;
    return 0;
fail:
    lsmash_log_refresh_line( &logger );
    lsmash_free( cts );
    return -1;
}

static int vc1_importer_probe( importer_t *importer )
{
    /* Find the first start code. */
    vc1_importer_t *vc1_imp = create_vc1_importer( importer );
    if( !vc1_imp )
        return -1;
    lsmash_bs_t *bs = vc1_imp->bs;
    uint64_t first_ebdu_head_pos = 0;
    while( 1 )
    {
        /* The first EBDU in decoding order of the stream shall have start code (0x000001). */
        if( 0x000001 == lsmash_bs_show_be24( bs, first_ebdu_head_pos ) )
            break;
        /* Invalid if encountered any value of non-zero before the first start code. */
        if( lsmash_bs_show_byte( bs, first_ebdu_head_pos ) )
            goto fail;
        ++first_ebdu_head_pos;
    }
    /* OK. It seems the stream has a sequence header of VC-1. */
    importer->info = vc1_imp;
    vc1_info_t *info = &vc1_imp->info;
    lsmash_bs_read_seek( bs, first_ebdu_head_pos, SEEK_SET );
    info->ebdu_head_pos = first_ebdu_head_pos;
    if( vc1_analyze_whole_stream( importer ) < 0 )
        goto fail;
    lsmash_video_summary_t *summary = vc1_create_summary( info, &vc1_imp->first_sequence, vc1_imp->max_au_length );
    if( !summary )
        goto fail;
    if( lsmash_add_entry( importer->summaries, summary ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        goto fail;
    }
    /* Go back to layer of the first EBDU. */
    vc1_imp->status = IMPORTER_OK;
    lsmash_bs_read_seek( bs, first_ebdu_head_pos, SEEK_SET );
    info->prev_bdu_type                  = 0xFF;    /* 0xFF is a forbidden value. */
    info->ebdu_head_pos                  = first_ebdu_head_pos;
    uint8_t *temp_access_unit            = info->access_unit.data;
    uint8_t *temp_incomplete_access_unit = info->access_unit.incomplete_data;
    memset( &info->access_unit, 0, sizeof(vc1_access_unit_t) );
    info->access_unit.data               = temp_access_unit;
    info->access_unit.incomplete_data    = temp_incomplete_access_unit;
    memset( &info->picture, 0, sizeof(vc1_picture_info_t) );
    return 0;
fail:
    remove_vc1_importer( vc1_imp );
    importer->info = NULL;
    lsmash_remove_entries( importer->summaries, lsmash_cleanup_summary );
    return -1;
}

static uint32_t vc1_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    vc1_importer_t *vc1_imp = (vc1_importer_t *)importer->info;
    if( !vc1_imp || track_number != 1 || vc1_imp->status != IMPORTER_EOF )
        return 0;
    return vc1_imp->ts_list.sample_count
         ? 1
         : UINT32_MAX;    /* arbitrary */
}

static const importer_functions vc1_importer =
{
    { "VC-1", offsetof( importer_t, log_level ) },
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
static const importer_functions *importer_func_table[] =
{
    &mp4sys_adts_importer,
    &mp4sys_mp3_importer,
    &amr_importer,
    &ac3_importer,
    &eac3_importer,
    &mp4a_als_importer,
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
    importer->class = &lsmash_importer_class;
    if( !strcmp( identifier, "-" ) )
    {
        /* special treatment for stdin */
        if( auto_detect )
        {
            lsmash_log( importer, LSMASH_LOG_ERROR, "auto importer detection on stdin is not supported.\n" );
            goto fail;
        }
        importer->stream = stdin;
        importer->is_stdin = 1;
    }
    else if( (importer->stream = lsmash_fopen( identifier, "rb" )) == NULL )
    {
        lsmash_log( importer, LSMASH_LOG_ERROR, "failed to open %s.\n", identifier );
        goto fail;
    }
    importer->summaries = lsmash_create_entry_list();
    if( !importer->summaries )
    {
        lsmash_log( importer, LSMASH_LOG_ERROR, "failed to set up the importer.\n" );
        goto fail;
    }
    /* find importer */
    importer->log_level = LSMASH_LOG_QUIET; /* Any error log is confusing for the probe step. */
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
    importer->log_level = LSMASH_LOG_INFO;
    if( !funcs )
    {
        importer->class = &lsmash_importer_class;
        lsmash_log( importer, LSMASH_LOG_ERROR, "failed to find the matched importer.\n" );
        goto fail;
    }
    importer->funcs = *funcs;
    return importer;
fail:
    lsmash_importer_close( importer );
    return NULL;
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
