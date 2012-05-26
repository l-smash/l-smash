/*****************************************************************************
 * vc1.c:
 *****************************************************************************
 * Copyright (C) 2012 L-SMASH project
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

#include "internal.h"

#include <string.h>
#include <stdlib.h>

#include "box.h"

/***************************************************************************
    SMPTE 421M-2006
    SMPTE RP 2025-2007
***************************************************************************/
#include "vc1.h"

#define IF_INVALID_VALUE( x ) if( x )

typedef struct
{
    uint8_t *data;
    uint32_t remainder_length;
    uint32_t overall_wasted_length;
} vc1_data_stream_handler_t;

typedef struct
{
    uint8_t *ebdu;
    uint32_t ebdu_size;
} vc1_header_t;

typedef enum
{
    VC1_ADVANCED_PICTURE_TYPE_P       = 0x0,        /* 0b0 */
    VC1_ADVANCED_PICTURE_TYPE_B       = 0x2,        /* 0b10 */
    VC1_ADVANCED_PICTURE_TYPE_I       = 0x6,        /* 0b110 */
    VC1_ADVANCED_PICTURE_TYPE_BI      = 0xE,        /* 0b1110 */
    VC1_ADVANCED_PICTURE_TYPE_SKIPPED = 0xF,        /* 0b1111 */
} vc1_picture_type;

typedef enum
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

typedef enum
{
    VC1_FRAME_CODING_MODE_PROGRESSIVE     = 0x0,    /* 0b0 */
    VC1_FRAME_CODING_MODE_FRAME_INTERLACE = 0x2,    /* 0b10 */
    VC1_FRAME_CODING_MODE_FIELD_INTERLACE = 0x3,    /* 0b11 */
} vc1_frame_coding_mode;

static void vc1_destroy_header( vc1_header_t *hdr )
{
    if( !hdr )
        return;
    if( hdr->ebdu )
        free( hdr->ebdu );
    free( hdr );
}

void lsmash_destroy_vc1_headers( lsmash_vc1_specific_parameters_t *param )
{
    if( !param )
        return;
    vc1_destroy_header( param->seqhdr );
    vc1_destroy_header( param->ephdr );
}

void vc1_cleanup_parser( vc1_info_t *info )
{
    if( !info )
        return;
    lsmash_destroy_vc1_headers( &info->dvc1_param );
    lsmash_destroy_multiple_buffers( info->buffer.bank );
    lsmash_bits_adhoc_cleanup( info->bits );
}

int vc1_setup_parser( vc1_info_t *info, int parse_only, uint32_t (*update)( vc1_info_t *, void *, uint32_t ) )
{
    if( !info )
        return -1;
    memset( info, 0, sizeof(vc1_info_t) );
    vc1_stream_buffer_t *buffer = &info->buffer;
    buffer->bank = lsmash_create_multiple_buffers( parse_only ? 2 : 4, VC1_DEFAULT_BUFFER_SIZE );
    if( !buffer->bank )
        return -1;
    buffer->start  = lsmash_withdraw_buffer( buffer->bank, 1 );
    buffer->rbdu   = lsmash_withdraw_buffer( buffer->bank, 2 );
    buffer->pos    = buffer->start;
    buffer->end    = buffer->start;
    buffer->update = update;
    if( !parse_only )
    {
        info->access_unit.data            = lsmash_withdraw_buffer( buffer->bank, 3 );
        info->access_unit.incomplete_data = lsmash_withdraw_buffer( buffer->bank, 4 );
    }
    info->bits = lsmash_bits_adhoc_create();
    if( !info->bits )
    {
        lsmash_destroy_multiple_buffers( info->buffer.bank );
        info->buffer.bank = NULL;
        return -1;
    }
    return 0;
}

static inline uint8_t vc1_get_vlc( lsmash_bits_t *bits, int length )
{
    uint8_t value = 0;
    for( int i = 0; i < length; i++ )
        if( lsmash_bits_get( bits, 1 ) )
            value = (value << 1) | 1;
        else
        {
            value = value << 1;
            break;
        }
    return value;
}

/* Convert EBDU (Encapsulated Byte Data Unit) to RBDU (Raw Byte Data Unit). */
static uint8_t *vc1_remove_emulation_prevention( uint8_t *src, uint64_t src_length, uint8_t *dst )
{
    uint8_t *src_end = src + src_length;
    while( src < src_end )
        if( ((src + 2) < src_end) && !src[0] && !src[1] && (src[2] == 0x03) )
        {
            /* 0x000003 -> 0x0000 */
            *dst++ = *src++;
            *dst++ = *src++;
            src++;  /* Skip emulation_prevention_three_byte (0x03). */
        }
        else
            *dst++ = *src++;
    return dst;
}

static int vc1_import_rbdu_from_ebdu( lsmash_bits_t *bits, uint8_t *rbdu_buffer, uint8_t *ebdu, uint64_t ebdu_size )
{
    uint8_t *rbdu_start  = rbdu_buffer;
    uint8_t *rbdu_end    = vc1_remove_emulation_prevention( ebdu, ebdu_size, rbdu_buffer );
    uint64_t rbdu_length = rbdu_end - rbdu_start;
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

int vc1_parse_sequence_header( vc1_info_t *info, uint8_t *ebdu, uint64_t ebdu_size, int try_append )
{
    lsmash_bits_t *bits = info->bits;
    vc1_sequence_header_t *sequence = &info->sequence;
    if( vc1_import_rbdu_from_ebdu( bits, info->buffer.rbdu, ebdu + VC1_START_CODE_LENGTH, ebdu_size ) )
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
                IF_INVALID_VALUE( frameratenr == 0 )
                    return -1;  /* Forbidden */
                if( frameratenr > 7 )
                    return -1;  /* SMPTE Reserved */
                uint8_t frameratedr = lsmash_bits_get( bits, 4 );
                if( frameratedr != 1 && frameratedr != 2 )
                    return -1;  /* 0: Forbidden, 3-15: SMPTE Reserved */
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
    if( try_append )
    {
        /* Update some specific parameters. */
        lsmash_vc1_specific_parameters_t *param = &info->dvc1_param;
        vc1_header_t *seqhdr = (vc1_header_t *)param->seqhdr;
        if( !seqhdr )
        {
            seqhdr = malloc( sizeof(vc1_header_t) );
            if( !seqhdr )
                return -1;
            seqhdr->ebdu = lsmash_memdup( ebdu, ebdu_size );
            if( !seqhdr->ebdu )
            {
                free( seqhdr );
                return -1;
            }
            seqhdr->ebdu_size = ebdu_size;
            param->seqhdr = seqhdr;
        }
        else if( seqhdr && seqhdr->ebdu && (seqhdr->ebdu_size == ebdu_size) )
            param->multiple_sequence |= !!memcmp( ebdu, seqhdr->ebdu, seqhdr->ebdu_size );
        param->profile     = sequence->profile << 2;
        param->level       = LSMASH_MAX( param->level, sequence->level );
        param->interlaced |= sequence->interlace;
        uint32_t framerate = sequence->framerate_flag
                           ? ((double)sequence->framerate_numerator / sequence->framerate_denominator) + 0.5
                           : 0xffffffff;    /* 0xffffffff means framerate is unknown or unspecified. */
        if( param->framerate == 0 )
            param->framerate = framerate;
        else if( param->framerate != framerate )
            param->framerate = 0xffffffff;
    }
    info->sequence.present = 1;
    return bits->bs->error ? -1 : 0;
}

int vc1_parse_entry_point_header( vc1_info_t *info, uint8_t *ebdu, uint64_t ebdu_size, int try_append )
{
    lsmash_bits_t *bits = info->bits;
    vc1_sequence_header_t *sequence = &info->sequence;
    vc1_entry_point_t *entry_point = &info->entry_point;
    if( vc1_import_rbdu_from_ebdu( bits, info->buffer.rbdu, ebdu + VC1_START_CODE_LENGTH, ebdu_size ) )
        return -1;
    memset( entry_point, 0, sizeof(vc1_entry_point_t) );
    uint8_t broken_link_flag = lsmash_bits_get( bits, 1 );          /* 0: no concatenation between the current and the previous entry points
                                                                     * 1: concatenated and needed to discard B-pictures */
    entry_point->closed_entry_point = lsmash_bits_get( bits, 1 );   /* 0: Open RAP, 1: Closed RAP */
    IF_INVALID_VALUE( broken_link_flag && entry_point->closed_entry_point )
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
    if( try_append )
    {
        lsmash_vc1_specific_parameters_t *param = &info->dvc1_param;
        vc1_header_t *ephdr = (vc1_header_t *)param->ephdr;
        if( !ephdr )
        {
            ephdr = malloc( sizeof(vc1_header_t) );
            if( !ephdr )
                return -1;
            ephdr->ebdu = lsmash_memdup( ebdu, ebdu_size );
            if( !ephdr->ebdu )
            {
                free( ephdr );
                return -1;
            }
            ephdr->ebdu_size = ebdu_size;
            param->ephdr = ephdr;
        }
        else if( ephdr && ephdr->ebdu && (ephdr->ebdu_size == ebdu_size) )
            param->multiple_entry |= !!memcmp( ebdu, ephdr->ebdu, ephdr->ebdu_size );
    }
    info->entry_point.present = 1;
    return bits->bs->error ? -1 : 0;
}

int vc1_parse_advanced_picture( lsmash_bits_t *bits,
                                vc1_sequence_header_t *sequence, vc1_picture_info_t *picture,
                                uint8_t *rbdu_buffer, uint8_t *ebdu, uint64_t ebdu_size )
{
    if( vc1_import_rbdu_from_ebdu( bits, rbdu_buffer, ebdu + VC1_START_CODE_LENGTH, ebdu_size ) )
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

void vc1_update_au_property( vc1_access_unit_t *access_unit, vc1_picture_info_t *picture )
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

int vc1_find_au_delimit_by_bdu_type( uint8_t bdu_type, uint8_t prev_bdu_type )
{
    /* In any access unit, EBDU with smaller least significant 8-bits of BDU type doesn't precede EBDU with larger one.
     * Therefore, the condition: (bdu_type 0xF) > (prev_bdu_type & 0xF) is more precisely.
     * No two or more frame start codes shall be in the same access unit. */
    return bdu_type > prev_bdu_type || (bdu_type == 0x0D && prev_bdu_type == 0x0D);
}

int vc1_supplement_buffer( vc1_stream_buffer_t *buffer, vc1_access_unit_t *access_unit, uint32_t size )
{
    uint32_t buffer_pos_offset   = buffer->pos - buffer->start;
    uint32_t buffer_valid_length = buffer->end - buffer->start;
    lsmash_multiple_buffers_t *bank = lsmash_resize_multiple_buffers( buffer->bank, size );
    if( !bank )
        return -1;
    buffer->bank  = bank;
    buffer->start = lsmash_withdraw_buffer( bank, 1 );
    buffer->rbdu  = lsmash_withdraw_buffer( bank, 2 );
    buffer->pos = buffer->start + buffer_pos_offset;
    buffer->end = buffer->start + buffer_valid_length;
    if( access_unit && bank->number_of_buffers == 4 )
    {
        access_unit->data            = lsmash_withdraw_buffer( bank, 3 );
        access_unit->incomplete_data = lsmash_withdraw_buffer( bank, 4 );
    }
    return 0;
}

static uint32_t vc1_update_buffer_from_access_unit( vc1_info_t *info, void *src, uint32_t anticipation_bytes )
{
    vc1_stream_buffer_t *buffer = &info->buffer;
    assert( anticipation_bytes < buffer->bank->buffer_size );
    uint32_t remainder_bytes = buffer->end - buffer->pos;
    if( info->no_more_read )
        return remainder_bytes;
    if( remainder_bytes <= anticipation_bytes )
    {
        /* Move unused data to the head of buffer. */
        for( uint32_t i = 0; i < remainder_bytes; i++ )
            *(buffer->start + i) = *(buffer->pos + i);
        /* Read and store the next data into the buffer.
         * Move the position of buffer on the head. */
        vc1_data_stream_handler_t *stream = (vc1_data_stream_handler_t *)src;
        uint32_t wasted_data_length = LSMASH_MIN( stream->remainder_length, buffer->bank->buffer_size - remainder_bytes );
        memcpy( buffer->start + remainder_bytes, stream->data + stream->overall_wasted_length, wasted_data_length );
        stream->remainder_length      -= wasted_data_length;
        stream->overall_wasted_length += wasted_data_length;
        remainder_bytes               += wasted_data_length;
        buffer->pos = buffer->start;
        buffer->end = buffer->start + remainder_bytes;
        info->no_more_read = (stream->remainder_length == 0);
    }
    return remainder_bytes;
}

uint8_t *lsmash_create_vc1_specific_info( lsmash_vc1_specific_parameters_t *param, uint32_t *data_length )
{
    if( !param || !data_length )
        return NULL;
    if( !param->seqhdr && !param->ephdr )
        return NULL;
    /* Calculate enough buffer size. */
    vc1_header_t *seqhdr = (vc1_header_t *)param->seqhdr;
    vc1_header_t *ephdr  = (vc1_header_t *)param->ephdr;
    uint32_t buffer_size = ISOM_BASEBOX_COMMON_SIZE + 7 + seqhdr->ebdu_size + ephdr->ebdu_size;
    /* Set up bitstream writer. */
    uint8_t buffer[buffer_size];
    lsmash_bits_t bits = { 0 };
    lsmash_bs_t   bs   = { 0 };
    bs.data  = buffer;
    bs.alloc = buffer_size;
    lsmash_bits_init( &bits, &bs );
    /* Create a VC1SpecificBox */
    lsmash_bits_put( &bits, 32, 0 );                                    /* box size */
    lsmash_bits_put( &bits, 32, ISOM_BOX_TYPE_DVC1 );                   /* box type: 'dvc1' */
    lsmash_bits_put( &bits, 4, param->profile );                        /* profile */
    lsmash_bits_put( &bits, 3, param->level );                          /* level */
    lsmash_bits_put( &bits, 1, 0 );                                     /* reserved */
    /* VC1AdvDecSpecStruc (for Advanced Profile) */
    lsmash_bits_put( &bits, 3, param->level );                          /* level (identical to the previous level field) */
    lsmash_bits_put( &bits, 1, param->cbr );                            /* cbr */
    lsmash_bits_put( &bits, 6, 0 );                                     /* reserved */
    lsmash_bits_put( &bits, 1, !param->interlaced );                    /* no_interlace */
    lsmash_bits_put( &bits, 1, !param->multiple_sequence );             /* no_multiple_seq */
    lsmash_bits_put( &bits, 1, !param->multiple_entry );                /* no_multiple_entry */
    lsmash_bits_put( &bits, 1, !param->slice_present );                 /* no_slice_code */
    lsmash_bits_put( &bits, 1, !param->bframe_present );                /* no_bframe */
    lsmash_bits_put( &bits, 1, 0 );                                     /* reserved */
    lsmash_bits_put( &bits, 32, param->framerate );                     /* framerate */
    /* seqhdr_ephdr[] */
    for( uint32_t i = 0; i < seqhdr->ebdu_size; i++ )
        lsmash_bits_put( &bits, 8, *(seqhdr->ebdu + i) );
    for( uint32_t i = 0; i < ephdr->ebdu_size; i++ )
        lsmash_bits_put( &bits, 8, *(ephdr->ebdu + i) );
    /* */
    uint8_t *data = lsmash_bits_export_data( &bits, data_length );
    /* Update box size. */
    data[0] = ((*data_length) >> 24) & 0xff;
    data[1] = ((*data_length) >> 16) & 0xff;
    data[2] = ((*data_length) >>  8) & 0xff;
    data[3] =  (*data_length)        & 0xff;
    return data;
}

static int vc1_try_to_put_header( vc1_header_t **p_hdr, uint8_t *multiple_hdr, void *hdr_data, uint32_t hdr_length )
{
    vc1_header_t *hdr = *p_hdr;
    if( !hdr )
    {
        hdr = lsmash_malloc_zero( sizeof(vc1_header_t) );
        if( !hdr )
            return -1;
    }
    else if( hdr->ebdu )
    {
        *multiple_hdr |= hdr->ebdu_size == hdr_length ? !!memcmp( hdr_data, hdr->ebdu, hdr->ebdu_size ) : 1;
        return 0;
    }
    hdr->ebdu = lsmash_memdup( hdr_data, hdr_length );
    hdr->ebdu_size = hdr->ebdu ? hdr_length : 0;
    *p_hdr = hdr;
    return hdr->ebdu ? 0 : -1;
}

int lsmash_put_vc1_header( lsmash_vc1_specific_parameters_t *param, void *hdr_data, uint32_t hdr_length )
{
    if( !param || !hdr_data || hdr_length < 5 )
        return -1;
    /* Check start code prefix (0x000001). */
    uint8_t *data = (uint8_t *)hdr_data;
    if( data[0] != 0x00 || data[1] != 0x00 || data[2] != 0x01 )
        return -1;
    if( data[3] == 0x0F )       /* sequence header */
        return vc1_try_to_put_header( (vc1_header_t **)&param->seqhdr, &param->multiple_sequence, hdr_data, hdr_length );
    else if( data[3] == 0x0E )  /* entry point header */
        return vc1_try_to_put_header( (vc1_header_t **)&param->ephdr, &param->multiple_entry, hdr_data, hdr_length );
    return -1;
}

static int vc1_parse_succeeded( vc1_info_t *info, lsmash_vc1_specific_parameters_t *param )
{
    int ret;
    if( info->sequence.present && info->entry_point.present )
    {
        *param = info->dvc1_param;
        /* Avoid freeing headers. */
        info->dvc1_param.seqhdr = NULL;
        info->dvc1_param.ephdr  = NULL;
        ret = 0;
    }
    else
        ret = -1;
    vc1_cleanup_parser( info );
    return ret;
}

static inline int vc1_parse_failed( vc1_info_t *info )
{
    vc1_cleanup_parser( info );
    return -1;
}

int lsmash_setup_vc1_specific_parameters_from_access_unit( lsmash_vc1_specific_parameters_t *param, uint8_t *data, uint32_t data_length )
{
    if( !param || !data || data_length == 0 )
        return -1;
    vc1_info_t  handler = { { 0 } };
    vc1_info_t *info    = &handler;
    if( vc1_setup_parser( info, 1, vc1_update_buffer_from_access_unit ) )
        return vc1_parse_failed( info );
    info->dvc1_param = *param;
    vc1_stream_buffer_t *buffer = &info->buffer;
    vc1_data_stream_handler_t stream = { 0 };
    stream.data             = data;
    stream.remainder_length = data_length;
    uint8_t  bdu_type = 0xFF;   /* 0xFF is a forbidden value. */
    uint64_t consecutive_zero_byte_count = 0;
    uint64_t ebdu_length = 0;
    int      no_more_buf = 0;
    while( 1 )
    {
        buffer->update( info, &stream, 2 );
        no_more_buf = buffer->pos >= buffer->end;
        int no_more = info->no_more_read && no_more_buf;
        if( !vc1_check_next_start_code_prefix( buffer->pos, buffer->end ) && !no_more )
        {
            if( *(buffer->pos ++) )
                consecutive_zero_byte_count = 0;
            else
                ++consecutive_zero_byte_count;
            ++ebdu_length;
            continue;
        }
        if( no_more && ebdu_length == 0 )
            /* For the last EBDU. This EBDU already has been parsed. */
            return vc1_parse_succeeded( info, param );
        ebdu_length += VC1_START_CODE_LENGTH;
        uint64_t next_scs_file_offset = info->ebdu_head_pos + ebdu_length + !no_more * VC1_START_CODE_PREFIX_LENGTH;
        uint8_t *next_ebdu_pos = buffer->pos;       /* Memorize position of beginning of the next EBDU in buffer.
                                                     * This is used when backward reading of stream doesn't occur. */
        int read_back = 0;
        if( bdu_type >= 0x0A && bdu_type <= 0x0F )
        {
            /* Get the current EBDU here. */
            ebdu_length -= consecutive_zero_byte_count;     /* Any EBDU doesn't have zero bytes at the end. */
            if( buffer->bank->buffer_size < ebdu_length )
            {
                if( vc1_supplement_buffer( buffer, NULL, 2 * ebdu_length ) )
                    return vc1_parse_failed( info );
                next_ebdu_pos = buffer->pos;
            }
            /* Move to the first byte of the current EBDU. */
            read_back = (buffer->pos - buffer->start) < (ebdu_length + consecutive_zero_byte_count);
            if( read_back )
            {
                memcpy( buffer->start, stream.data + info->ebdu_head_pos, ebdu_length );
                buffer->pos = buffer->start;
                buffer->end = buffer->start + ebdu_length;
            }
            else
                buffer->pos -= ebdu_length + consecutive_zero_byte_count;
            /* Complete the current access unit if encountered delimiter of current access unit. */
            if( vc1_find_au_delimit_by_bdu_type( bdu_type, info->prev_bdu_type ) )
                /* The last video coded EBDU belongs to the access unit you want at this time. */
                return vc1_parse_succeeded( info, param );
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
                {
                    vc1_picture_info_t *picture = &info->picture;
                    if( vc1_parse_advanced_picture( info->bits, &info->sequence, picture, buffer->rbdu,
                                                    buffer->pos, ebdu_length ) )
                        return vc1_parse_failed( info );
                    info->dvc1_param.bframe_present |= picture->frame_coding_mode == 0x3
                                                     ? picture->type >= VC1_ADVANCED_FIELD_PICTURE_TYPE_BB
                                                     : picture->type == VC1_ADVANCED_PICTURE_TYPE_B || picture->type == VC1_ADVANCED_PICTURE_TYPE_BI;
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
                    if( vc1_parse_entry_point_header( info, buffer->pos, ebdu_length, 1 ) )
                        return vc1_parse_failed( info );
                    break;
                case 0x0F : /* Sequence header
                             * [SEQ_SC][SEQ_L][EP_SC][EP_L][FRM_SC][PIC_L] ... */
                    if( vc1_parse_sequence_header( info, buffer->pos, ebdu_length, 1 ) )
                        return vc1_parse_failed( info );
                    break;
                default :   /* End-of-sequence (0x0A) */
                    break;
            }
        }
        /* Move to the first byte of the next start code suffix. */
        if( read_back )
        {
            uint64_t wasted_data_length = LSMASH_MIN( stream.remainder_length, buffer->bank->buffer_size );
            memcpy( buffer->start, stream.data + next_scs_file_offset, wasted_data_length );
            stream.overall_wasted_length = next_scs_file_offset + wasted_data_length;
            stream.remainder_length      = data_length - stream.overall_wasted_length;
            buffer->pos = buffer->start;
            buffer->end = buffer->start + wasted_data_length;
        }
        else
            buffer->pos = next_ebdu_pos + VC1_START_CODE_PREFIX_LENGTH;
        info->prev_bdu_type = bdu_type;
        buffer->update( info, &stream, 0 );
        no_more_buf = buffer->pos >= buffer->end;
        ebdu_length = 0;
        no_more = info->no_more_read && no_more_buf;
        if( !no_more )
        {
            /* Check the next BDU type. */
            if( vc1_check_next_start_code_suffix( &bdu_type, &buffer->pos ) )
                return vc1_parse_failed( info );
            info->ebdu_head_pos = next_scs_file_offset - VC1_START_CODE_PREFIX_LENGTH;
        }
        else
            return vc1_parse_succeeded( info, param );
        consecutive_zero_byte_count = 0;
    }
}
