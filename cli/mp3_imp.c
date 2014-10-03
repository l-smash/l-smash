/*****************************************************************************
 * mp3_imp.c
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

#include <string.h>

#define LSMASH_IMPORTER_INTERNAL
#include "importer.h"

/***************************************************************************
    mp3 (Legacy Interface) importer
***************************************************************************/
#include "codecs/mp4a.h"

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

const importer_functions mp4sys_mp3_importer =
{
    { "MPEG-1/2BC_Audio_Legacy" },
    1,
    mp4sys_mp3_probe,
    mp4sys_mp3_get_accessunit,
    mp4sys_mp3_get_last_delta,
    mp4sys_mp3_cleanup
};
