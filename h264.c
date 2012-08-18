/*****************************************************************************
 * h264.c:
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
#include <inttypes.h>
#include <math.h>

#include "box.h"

/***************************************************************************
    ITU-T Recommendation H.264 (03/10)
    ISO/IEC 14496-15:2010
***************************************************************************/
#include "h264.h"

#define IF_INVALID_VALUE( x ) if( x )
#define IF_EXCEED_INT32( x ) if( (x) < INT32_MIN || (x) > INT32_MAX )
#define H264_POC_DEBUG_PRINT 0

typedef struct
{
    uint8_t *data;
    uint32_t remainder_length;
    uint32_t overall_wasted_length;
} h264_data_stream_handler_t;

typedef enum
{
    H264_SLICE_TYPE_P    = 0,
    H264_SLICE_TYPE_B    = 1,
    H264_SLICE_TYPE_I    = 2,
    H264_SLICE_TYPE_SP   = 3,
    H264_SLICE_TYPE_SI   = 4
} h264_slice_type;

void lsmash_destroy_h264_parameter_sets( lsmash_h264_specific_parameters_t *param )
{
    if( !param || !param->parameter_sets )
        return;
    lsmash_remove_entries( param->parameter_sets->sps_list,    isom_remove_avcC_ps );
    lsmash_remove_entries( param->parameter_sets->pps_list,    isom_remove_avcC_ps );
    lsmash_remove_entries( param->parameter_sets->spsext_list, isom_remove_avcC_ps );
    free( param->parameter_sets );
    param->parameter_sets = NULL;
}

void h264_destruct_specific_data( void *data )
{
    if( !data )
        return;
    lsmash_destroy_h264_parameter_sets( data );
    free( data );
}

void h264_cleanup_parser( h264_info_t *info )
{
    if( !info )
        return;
    lsmash_remove_entries( info->sps_list,   NULL );
    lsmash_remove_entries( info->pps_list,   NULL );
    lsmash_remove_entries( info->slice_list, NULL );
    lsmash_destroy_h264_parameter_sets( &info->avcC_param );
    lsmash_destroy_multiple_buffers( info->buffer.bank );
    lsmash_bits_adhoc_cleanup( info->bits );
    info->buffer.bank = NULL;
    info->bits = NULL;
}

int h264_setup_parser( h264_info_t *info, int parse_only, uint32_t (*update)( h264_info_t *, void *, uint32_t ) )
{
    if( !info )
        return -1;
    memset( info, 0, sizeof(h264_info_t) );
    info->avcC_param.lengthSizeMinusOne = H264_DEFAULT_NALU_LENGTH_SIZE - 1;
    h264_stream_buffer_t *buffer = &info->buffer;
    buffer->bank = lsmash_create_multiple_buffers( parse_only ? 2 : 4, H264_DEFAULT_BUFFER_SIZE );
    if( !buffer->bank )
        return -1;
    buffer->start  = lsmash_withdraw_buffer( buffer->bank, 1 );
    buffer->rbsp   = lsmash_withdraw_buffer( buffer->bank, 2 );
    buffer->pos    = buffer->start;
    buffer->end    = buffer->start;
    buffer->update = update;
    if( !parse_only )
    {
        info->picture.au            = lsmash_withdraw_buffer( buffer->bank, 3 );
        info->picture.incomplete_au = lsmash_withdraw_buffer( buffer->bank, 4 );
    }
    info->bits = lsmash_bits_adhoc_create();
    if( !info->bits )
    {
        lsmash_destroy_multiple_buffers( info->buffer.bank );
        info->buffer.bank = NULL;
        return -1;
    }
    lsmash_init_entry_list( info->sps_list );
    lsmash_init_entry_list( info->pps_list );
    lsmash_init_entry_list( info->slice_list );
    return 0;
}

static h264_sps_t *h264_get_sps( lsmash_entry_list_t *sps_list, uint8_t sps_id )
{
    if( !sps_list || sps_id > 31 )
        return NULL;
    for( lsmash_entry_t *entry = sps_list->head; entry; entry = entry->next )
    {
        h264_sps_t *sps = (h264_sps_t *)entry->data;
        if( !sps )
            return NULL;
        if( sps->seq_parameter_set_id == sps_id )
            return sps;
    }
    h264_sps_t *sps = lsmash_malloc_zero( sizeof(h264_sps_t) );
    if( !sps )
        return NULL;
    sps->seq_parameter_set_id = sps_id;
    if( lsmash_add_entry( sps_list, sps ) )
    {
        free( sps );
        return NULL;
    }
    return sps;
}

static h264_pps_t *h264_get_pps( lsmash_entry_list_t *pps_list, uint8_t pps_id )
{
    if( !pps_list )
        return NULL;
    for( lsmash_entry_t *entry = pps_list->head; entry; entry = entry->next )
    {
        h264_pps_t *pps = (h264_pps_t *)entry->data;
        if( !pps )
            return NULL;
        if( pps->pic_parameter_set_id == pps_id )
            return pps;
    }
    h264_pps_t *pps = lsmash_malloc_zero( sizeof(h264_pps_t) );
    if( !pps )
        return NULL;
    pps->pic_parameter_set_id = pps_id;
    if( lsmash_add_entry( pps_list, pps ) )
    {
        free( pps );
        return NULL;
    }
    return pps;
}

static h264_slice_info_t *h264_get_slice_info( lsmash_entry_list_t *slice_list, uint8_t slice_id )
{
    if( !slice_list )
        return NULL;
    for( lsmash_entry_t *entry = slice_list->head; entry; entry = entry->next )
    {
        h264_slice_info_t *slice = (h264_slice_info_t *)entry->data;
        if( !slice )
            return NULL;
        if( slice->slice_id == slice_id )
            return slice;
    }
    h264_slice_info_t *slice = lsmash_malloc_zero( sizeof(h264_slice_info_t) );
    if( !slice )
        return NULL;
    slice->slice_id = slice_id;
    if( lsmash_add_entry( slice_list, slice ) )
    {
        free( slice );
        return NULL;
    }
    return slice;
}

int h264_calculate_poc( h264_info_t *info, h264_picture_info_t *picture, h264_picture_info_t *prev_picture )
{
#if H264_POC_DEBUG_PRINT
    fprintf( stderr, "PictureOrderCount\n" );
#endif
    h264_pps_t *pps = h264_get_pps( info->pps_list, picture->pic_parameter_set_id );
    if( !pps )
        return -1;
    h264_sps_t *sps = h264_get_sps( info->sps_list, pps->seq_parameter_set_id );
    if( !sps )
        return -1;
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
        BottomFieldOrderCnt = TopFieldOrderCnt = PicOrderCntMsb + pic_order_cnt_lsb;
        if( !picture->field_pic_flag )
            BottomFieldOrderCnt += picture->delta_pic_order_cnt_bottom;
        IF_EXCEED_INT32( TopFieldOrderCnt )
            return -1;
        IF_EXCEED_INT32( BottomFieldOrderCnt )
            return -1;
        if( !picture->disposable )
        {
            picture->ref_pic_has_mmco5         = picture->has_mmco5;
            picture->ref_pic_bottom_field_flag = picture->bottom_field_flag;
            picture->ref_pic_TopFieldOrderCnt  = TopFieldOrderCnt;
            picture->ref_pic_PicOrderCntMsb    = PicOrderCntMsb;
            picture->ref_pic_PicOrderCntLsb    = pic_order_cnt_lsb;
        }
#if H264_POC_DEBUG_PRINT
        fprintf( stderr, "    prevPicOrderCntMsb: %"PRId32"\n", prevPicOrderCntMsb );
        fprintf( stderr, "    prevPicOrderCntLsb: %"PRId32"\n", prevPicOrderCntLsb );
        fprintf( stderr, "    PicOrderCntMsb: %"PRId64"\n", PicOrderCntMsb );
        fprintf( stderr, "    pic_order_cnt_lsb: %"PRId32"\n", pic_order_cnt_lsb );
        fprintf( stderr, "    MaxPicOrderCntLsb: %"PRIu64"\n", MaxPicOrderCntLsb );
#endif
    }
    else if( sps->pic_order_cnt_type == 1 )
    {
        uint32_t frame_num = picture->frame_num;
        uint32_t prevFrameNum = prev_picture->frame_num;
        uint32_t prevFrameNumOffset = prev_picture->has_mmco5 ? 0 : prev_picture->FrameNumOffset;
        uint64_t FrameNumOffset = picture->idr ? 0 : prevFrameNumOffset + (prevFrameNum > frame_num ? sps->MaxFrameNum : 0);
        IF_INVALID_VALUE( FrameNumOffset > INT32_MAX )
            return -1;
        int64_t expectedPicOrderCnt;
        if( sps->num_ref_frames_in_pic_order_cnt_cycle )
        {
            uint64_t absFrameNum = FrameNumOffset + frame_num;
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
            expectedPicOrderCnt = 0;
        if( picture->disposable )
            expectedPicOrderCnt += sps->offset_for_non_ref_pic;
        TopFieldOrderCnt    = expectedPicOrderCnt + picture->delta_pic_order_cnt[0];
        BottomFieldOrderCnt = TopFieldOrderCnt + sps->offset_for_top_to_bottom_field;
        if( !picture->field_pic_flag )
            BottomFieldOrderCnt += picture->delta_pic_order_cnt[1];
        IF_EXCEED_INT32( TopFieldOrderCnt )
            return -1;
        IF_EXCEED_INT32( BottomFieldOrderCnt )
            return -1;
        picture->FrameNumOffset = FrameNumOffset;
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
            IF_EXCEED_INT32( FrameNumOffset )
                return -1;
            IF_EXCEED_INT32( tempPicOrderCnt )
                return -1;
        }
        BottomFieldOrderCnt = TopFieldOrderCnt = tempPicOrderCnt;
        picture->FrameNumOffset = FrameNumOffset;
    }
    if( !picture->field_pic_flag )
        picture->PicOrderCnt = LSMASH_MIN( TopFieldOrderCnt, BottomFieldOrderCnt );
    else
        picture->PicOrderCnt = picture->bottom_field_flag ? BottomFieldOrderCnt : TopFieldOrderCnt;
#if H264_POC_DEBUG_PRINT
    if( picture->field_pic_flag )
    {
        if( !picture->bottom_field_flag )
            fprintf( stderr, "    TopFieldOrderCnt: %"PRId64"\n", TopFieldOrderCnt );
        else
            fprintf( stderr, "    BottomFieldOrderCnt: %"PRId64"\n", BottomFieldOrderCnt );
    }
    fprintf( stderr, "    POC: %"PRId32"\n", picture->PicOrderCnt );
#endif
    return 0;
}

int h264_check_nalu_header( h264_nalu_header_t *nalu_header, uint8_t **p_buf_pos, int use_long_start_code )
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
static uint8_t *h264_remove_emulation_prevention( uint8_t *src, uint64_t src_length, uint8_t *dst )
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

static int h264_import_rbsp_from_ebsp( lsmash_bits_t *bits, uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size )
{
    uint8_t *rbsp_start  = rbsp_buffer;
    uint8_t *rbsp_end    = h264_remove_emulation_prevention( ebsp, ebsp_size, rbsp_buffer );
    uint64_t rbsp_length = rbsp_end - rbsp_start;
    return lsmash_bits_import_data( bits, rbsp_start, rbsp_length );
}

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

static int h264_parse_sps_easy( lsmash_bits_t *bits, h264_sps_t *sps, uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size )
{
    if( h264_import_rbsp_from_ebsp( bits, rbsp_buffer, ebsp, ebsp_size ) )
        return -1;
    memset( sps, 0, sizeof(h264_sps_t) );
    sps->profile_idc          = lsmash_bits_get( bits, 8 );
    sps->constraint_set_flags = lsmash_bits_get( bits, 8 );
    sps->level_idc            = lsmash_bits_get( bits, 8 );
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
    return bits->bs->error ? -1 : 0;
}

int h264_parse_sps( h264_info_t *info, uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size )
{
    lsmash_bits_t *bits = info->bits;
    /* seq_parameter_set_data() */
    h264_sps_t temp_sps;
    if( h264_parse_sps_easy( bits, &temp_sps, rbsp_buffer, ebsp, ebsp_size ) )
        return -1;
    h264_sps_t *sps = h264_get_sps( info->sps_list, temp_sps.seq_parameter_set_id );
    if( !sps )
        return -1;
    memset( sps, 0, sizeof(h264_sps_t) );
    sps->profile_idc                = temp_sps.profile_idc;
    sps->constraint_set_flags       = temp_sps.constraint_set_flags;
    sps->level_idc                  = temp_sps.level_idc;
    sps->seq_parameter_set_id       = temp_sps.seq_parameter_set_id;
    sps->chroma_format_idc          = temp_sps.chroma_format_idc;
    sps->separate_colour_plane_flag = temp_sps.separate_colour_plane_flag;
    sps->bit_depth_luma_minus8      = temp_sps.bit_depth_luma_minus8;
    sps->bit_depth_chroma_minus8    = temp_sps.bit_depth_chroma_minus8;
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
            h264_get_exp_golomb_ue( bits );     /* max_num_reorder_frames */
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
    if( bits->bs->error )
        return -1;
    sps->present = 1;
    info->sps = *sps;
    return 0;
}

static int h264_parse_pps_easy( lsmash_bits_t *bits, h264_pps_t *pps, uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size )
{
    if( h264_import_rbsp_from_ebsp( bits, rbsp_buffer, ebsp, ebsp_size ) )
        return -1;
    memset( pps, 0, sizeof(h264_pps_t) );
    uint64_t pic_parameter_set_id = h264_get_exp_golomb_ue( bits );
    IF_INVALID_VALUE( pic_parameter_set_id > 255 )
        return -1;
    pps->pic_parameter_set_id = pic_parameter_set_id;
    return bits->bs->error ? -1 : 0;
}

int h264_parse_pps( h264_info_t *info, uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size )
{
    lsmash_bits_t *bits = info->bits;
    /* pic_parameter_set_rbsp */
    h264_pps_t temp_pps;
    if( h264_parse_pps_easy( bits, &temp_pps, rbsp_buffer, ebsp, ebsp_size ) )
        return -1;
    h264_pps_t *pps = h264_get_pps( info->pps_list, temp_pps.pic_parameter_set_id );
    if( !pps )
        return -1;
    memset( pps, 0, sizeof(h264_pps_t) );
    pps->pic_parameter_set_id = temp_pps.pic_parameter_set_id;
    uint64_t seq_parameter_set_id = h264_get_exp_golomb_ue( bits );
    IF_INVALID_VALUE( seq_parameter_set_id > 31 )
        return -1;
    h264_sps_t *sps = h264_get_sps( info->sps_list, seq_parameter_set_id );
    if( !sps )
        return -1;
    pps->seq_parameter_set_id = seq_parameter_set_id;
    pps->entropy_coding_mode_flag = lsmash_bits_get( bits, 1 );
    pps->bottom_field_pic_order_in_frame_present_flag = lsmash_bits_get( bits, 1 );
    uint64_t num_slice_groups_minus1 = h264_get_exp_golomb_ue( bits );
    IF_INVALID_VALUE( num_slice_groups_minus1 > 7 )
        return -1;
    pps->num_slice_groups_minus1 = num_slice_groups_minus1;
    if( num_slice_groups_minus1 )        /* num_slice_groups_minus1 */
    {
        uint64_t slice_group_map_type = h264_get_exp_golomb_ue( bits );
        IF_INVALID_VALUE( slice_group_map_type > 6 )
            return -1;
        pps->slice_group_map_type = slice_group_map_type;
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
    if( bits->bs->error )
        return -1;
    pps->present = 1;
    info->sps = *sps;
    info->pps = *pps;
    return 0;
}

int h264_parse_sei( lsmash_bits_t *bits, h264_sei_t *sei,
                    uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size )
{
    if( h264_import_rbsp_from_ebsp( bits, rbsp_buffer, ebsp, ebsp_size ) )
        return -1;
    uint8_t *rbsp_start = rbsp_buffer;
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

static int h264_parse_slice_header( h264_info_t *info, h264_nalu_header_t *nalu_header )
{
    h264_slice_info_t *slice = &info->slice;
    memset( slice, 0, sizeof(h264_slice_info_t) );
    /* slice_header() */
    lsmash_bits_t *bits = info->bits;
    h264_get_exp_golomb_ue( bits );     /* first_mb_in_slice */
    uint8_t slice_type = slice->type = h264_get_exp_golomb_ue( bits );
    IF_INVALID_VALUE( (uint64_t)slice->type > 9 )
        return -1;
    if( slice_type > 4 )
        slice_type = slice->type -= 5;
    uint64_t pic_parameter_set_id = h264_get_exp_golomb_ue( bits );
    IF_INVALID_VALUE( pic_parameter_set_id > 255 )
        return -1;
    slice->pic_parameter_set_id = pic_parameter_set_id;
    h264_pps_t *pps = h264_get_pps( info->pps_list, pic_parameter_set_id );
    if( !pps )
        return -1;
    h264_sps_t *sps = h264_get_sps( info->sps_list, pps->seq_parameter_set_id );
    if( !sps )
        return -1;
    slice->nal_ref_idc = nalu_header->nal_ref_idc;
    slice->IdrPicFlag = (nalu_header->nal_unit_type == 5);
    slice->pic_order_cnt_type = sps->pic_order_cnt_type;
    IF_INVALID_VALUE( (slice->IdrPicFlag || sps->max_num_ref_frames == 0) && slice_type != 2 && slice_type != 4 )
        return -1;
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
    /* We needn't read more if not slice data partition A.
     * Skip slice_data() and rbsp_slice_trailing_bits(). */
    if( nalu_header->nal_unit_type == 2 )
    {
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
        && (pps->slice_group_map_type == 3 || pps->slice_group_map_type == 4 || pps->slice_group_map_type == 5) )
        {
            double temp = (double)sps->PicSizeInMapUnits / pps->SliceGroupChangeRate;
            uint64_t slice_group_change_cycle_length = ceil( log( temp + 1 ) / 0.693147180559945 );
            uint64_t slice_group_change_cycle = lsmash_bits_get( bits, slice_group_change_cycle_length );
            IF_INVALID_VALUE( slice_group_change_cycle > (uint64_t)ceil( temp ) )
                return -1;
        }
        /* end of slice_header() */
        slice->slice_id = h264_get_exp_golomb_ue( bits );
        h264_slice_info_t *slice_part = h264_get_slice_info( info->slice_list, slice->slice_id );
        if( !slice_part )
            return -1;
        *slice_part = *slice;
    }
    lsmash_bits_empty( bits );
    if( bits->bs->error )
        return -1;
    info->sps = *sps;
    info->pps = *pps;
    return 0;
}

int h264_parse_slice( h264_info_t *info, h264_nalu_header_t *nalu_header,
                      uint8_t *rbsp_buffer, uint8_t *ebsp, uint64_t ebsp_size )
{
    lsmash_bits_t *bits = info->bits;
    if( h264_import_rbsp_from_ebsp( bits, rbsp_buffer, ebsp, ebsp_size ) )
        return -1;
    if( nalu_header->nal_unit_type != 3 && nalu_header->nal_unit_type != 4 )
        return h264_parse_slice_header( info, nalu_header );
    /* slice_data_partition_b_layer_rbsp() or slice_data_partition_c_layer_rbsp() */
    uint64_t slice_id = h264_get_exp_golomb_ue( bits );
    h264_slice_info_t *slice = h264_get_slice_info( info->slice_list, slice_id );
    if( !slice )
        return -1;
    h264_pps_t *pps = h264_get_pps( info->pps_list, slice->pic_parameter_set_id );
    if( !pps )
        return -1;
    h264_sps_t *sps = h264_get_sps( info->sps_list, pps->seq_parameter_set_id );
    if( !sps )
        return -1;
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
    if( bits->bs->error )
        return -1;
    info->sps = *sps;
    info->pps = *pps;
    return 0;
}

static inline void  h264_update_picture_type( h264_picture_info_t *picture, h264_slice_info_t *slice )
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
    fprintf( stderr, "Picture type = %s\n", picture->type == H264_PICTURE_TYPE_I_P   ? "P"
                                          : picture->type == H264_PICTURE_TYPE_I_P_B ? "B"
                                          : picture->type == H264_PICTURE_TYPE_I     ? "I"
                                          : picture->type == H264_PICTURE_TYPE_SI    ? "SI"
                                          : picture->type == H264_PICTURE_TYPE_I_SI  ? "SI"
                                          :                                            "SP" );
#endif
}

/* Shall be called at least once per picture. */
void h264_update_picture_info_for_slice( h264_picture_info_t *picture, h264_slice_info_t *slice )
{
    picture->has_mmco5                 |= slice->has_mmco5;
    picture->has_redundancy            |= slice->has_redundancy;
    picture->incomplete_au_has_primary |= !slice->has_redundancy;
    h264_update_picture_type( picture, slice );
    slice->present = 0;     /* Discard this slice info. */
}

/* Shall be called exactly once per picture. */
void h264_update_picture_info( h264_picture_info_t *picture, h264_slice_info_t *slice, h264_sei_t *sei )
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
    if( sei->present )
    {
        picture->random_accessible |= sei->random_accessible;
        picture->recovery_frame_cnt = sei->recovery_frame_cnt;
        sei->present = 0;
    }
}

int h264_find_au_delimit_by_slice_info( h264_slice_info_t *slice, h264_slice_info_t *prev_slice )
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

int h264_find_au_delimit_by_nalu_type( uint8_t nalu_type, uint8_t prev_nalu_type )
{
    return ((nalu_type >= 6 && nalu_type <= 9) || (nalu_type >= 14 && nalu_type <= 18))
        && ((prev_nalu_type >= 1 && prev_nalu_type <= 5) || prev_nalu_type == 12 || prev_nalu_type == 19);
}

int h264_supplement_buffer( h264_stream_buffer_t *buffer, h264_picture_info_t *picture, uint32_t size )
{
    uint32_t buffer_pos_offset   = buffer->pos - buffer->start;
    uint32_t buffer_valid_length = buffer->end - buffer->start;
    lsmash_multiple_buffers_t *bank = lsmash_resize_multiple_buffers( buffer->bank, size );
    if( !bank )
        return -1;
    buffer->bank  = bank;
    buffer->start = lsmash_withdraw_buffer( bank, 1 );
    buffer->rbsp  = lsmash_withdraw_buffer( bank, 2 );
    buffer->pos = buffer->start + buffer_pos_offset;
    buffer->end = buffer->start + buffer_valid_length;
    if( picture && bank->number_of_buffers == 4 )
    {
        picture->au            = lsmash_withdraw_buffer( bank, 3 );
        picture->incomplete_au = lsmash_withdraw_buffer( bank, 4 );
    }
    return 0;
}

uint32_t h264_update_buffer_from_access_unit( h264_info_t *info, void *src, uint32_t anticipation_bytes )
{
    h264_stream_buffer_t *buffer = &info->buffer;
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
        h264_data_stream_handler_t *stream = (h264_data_stream_handler_t *)src;
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

static void h264_bs_put_parameter_sets( lsmash_bs_t *bs, lsmash_entry_list_t *ps_list, uint32_t max_ps_count )
{
    uint32_t ps_count = 0;
    for( lsmash_entry_t *entry = ps_list->head; entry && ps_count < max_ps_count; entry = entry->next )
    {
        isom_avcC_ps_entry_t *ps = (isom_avcC_ps_entry_t *)entry->data;
        if( ps )
        {
            lsmash_bs_put_be16( bs, ps->parameterSetLength );
            lsmash_bs_put_bytes( bs, ps->parameterSetLength, ps->parameterSetNALUnit );
        }
        else
            lsmash_bs_put_be16( bs, 0 );
        ++ps_count;
    }
}

uint8_t *lsmash_create_h264_specific_info( lsmash_h264_specific_parameters_t *param, uint32_t *data_length )
{
    if( !param || !param->parameter_sets || !data_length )
        return NULL;
    if( param->lengthSizeMinusOne != 0 && param->lengthSizeMinusOne != 1 && param->lengthSizeMinusOne != 3 )
        return NULL;
    static const uint32_t max_ps_count[3] = { 31, 255, 255 };
    lsmash_entry_list_t *ps_list[3] =
        {
            param->parameter_sets->sps_list,        /* SPS */
            param->parameter_sets->pps_list,        /* PPS */
            param->parameter_sets->spsext_list      /* SPSExt */
        };
    /* SPS and PPS are mandatory. */
    if( !ps_list[0] || !ps_list[0]->head || ps_list[0]->entry_count == 0
     || !ps_list[1] || !ps_list[1]->head || ps_list[1]->entry_count == 0 )
        return NULL;
    /* Calculate enough buffer size. */
    uint32_t buffer_size = ISOM_BASEBOX_COMMON_SIZE + 11;
    for( int i = 0; i < 3; i++ )
        if( ps_list[i] )
        {
            uint32_t ps_count = 0;
            for( lsmash_entry_t *entry = ps_list[i]->head; entry && ps_count < max_ps_count[i]; entry = entry->next )
            {
                isom_avcC_ps_entry_t *ps = (isom_avcC_ps_entry_t *)entry->data;
                if( !ps )
                    return NULL;
                buffer_size += 2 + ps->parameterSetLength;
                ++ps_count;
            }
            if( ps_list[i]->entry_count <= max_ps_count[i] && ps_list[i]->entry_count != ps_count )
                return NULL;    /* Created specific info will be broken. */
        }
    /* Set up bytestream writer. */
    uint8_t buffer[buffer_size];
    lsmash_bs_t bs = { 0 };
    bs.data  = buffer;
    bs.alloc = buffer_size;
    /* Create an AVCConfigurationBox */
    lsmash_bs_put_be32( &bs, 0 );                                                               /* box size */
    lsmash_bs_put_be32( &bs, ISOM_BOX_TYPE_AVCC );                                              /* box type: 'avcC' */
    lsmash_bs_put_byte( &bs, 1 );                                                               /* configurationVersion */
    lsmash_bs_put_byte( &bs, param->AVCProfileIndication );                                     /* AVCProfileIndication */
    lsmash_bs_put_byte( &bs, param->profile_compatibility );                                    /* profile_compatibility */
    lsmash_bs_put_byte( &bs, param->AVCLevelIndication );                                       /* AVCLevelIndication */
    lsmash_bs_put_byte( &bs, param->lengthSizeMinusOne | 0xfc );                                /* lengthSizeMinusOne */
    lsmash_bs_put_byte( &bs, LSMASH_MIN( ps_list[0]->entry_count, max_ps_count[0] ) | 0xe0 );   /* numOfSequenceParameterSets */
    h264_bs_put_parameter_sets( &bs, ps_list[0], max_ps_count[0] );                             /* sequenceParameterSetLength
                                                                                                 * sequenceParameterSetNALUnit */
    lsmash_bs_put_byte( &bs, LSMASH_MIN( ps_list[1]->entry_count, max_ps_count[1] ) );          /* numOfPictureParameterSets */
    h264_bs_put_parameter_sets( &bs, ps_list[1], max_ps_count[1] );                             /* pictureParameterSetLength
                                                                                                 * pictureParameterSetNALUnit */
    if( ISOM_REQUIRES_AVCC_EXTENSION( param->AVCProfileIndication ) )
    {
        lsmash_bs_put_byte( &bs, param->chroma_format           | 0xfc );                       /* chroma_format */
        lsmash_bs_put_byte( &bs, param->bit_depth_luma_minus8   | 0xf8 );                       /* bit_depth_luma_minus8 */
        lsmash_bs_put_byte( &bs, param->bit_depth_chroma_minus8 | 0xf8 );                       /* bit_depth_chroma_minus8 */
        if( ps_list[2] )
        {
            lsmash_bs_put_byte( &bs, LSMASH_MIN( ps_list[2]->entry_count, max_ps_count[2] ) );  /* numOfSequenceParameterSetExt */
            h264_bs_put_parameter_sets( &bs, ps_list[2], max_ps_count[2] );                     /* sequenceParameterSetExtLength
                                                                                                 * sequenceParameterSetExtNALUnit */
        }
        else    /* no sequence parameter set extensions */
            lsmash_bs_put_byte( &bs, 0 );                                                       /* numOfSequenceParameterSetExt */
    }
    uint8_t *data = lsmash_bs_export_data( &bs, data_length );
    /* Update box size. */
    data[0] = ((*data_length) >> 24) & 0xff;
    data[1] = ((*data_length) >> 16) & 0xff;
    data[2] = ((*data_length) >>  8) & 0xff;
    data[3] =  (*data_length)        & 0xff;
    return data;
}

static int h264_get_sps_id( uint8_t *ps_ebsp, uint32_t ps_ebsp_length, uint8_t *ps_id )
{
    /* max number of bits of sps_id = 11: 0b000001XXXXX
     * (24 + 11 - 1) / 8 + 1 = 5 bytes
     * Why +1? Because there might be an emulation_prevention_three_byte. */
    lsmash_bits_t bits = { 0 };
    lsmash_bs_t   bs   = { 0 };
    uint8_t rbsp_buffer[6];
    uint8_t buffer     [6];
    bs.data  = buffer;
    bs.alloc = 6;
    lsmash_bits_init( &bits, &bs );
    if( h264_import_rbsp_from_ebsp( &bits, rbsp_buffer, ps_ebsp, LSMASH_MIN( ps_ebsp_length, 6 ) ) )
        return -1;
    lsmash_bits_get( &bits, 24 );   /* profile_idc, constraint_set_flags and level_idc */
    uint64_t sec_parameter_set_id = h264_get_exp_golomb_ue( &bits );
    IF_INVALID_VALUE( sec_parameter_set_id > 31 )
        return -1;
    *ps_id = sec_parameter_set_id;
    return bs.error ? -1 : 0;
}

static int h264_get_pps_id( uint8_t *ps_ebsp, uint32_t ps_ebsp_length, uint8_t *ps_id )
{
    /* max number of bits of pps_id = 17: 0b000000001XXXXXXXX
     * (17 - 1) / 8 + 1 = 3 bytes
     * Why +1? Because there might be an emulation_prevention_three_byte. */
    lsmash_bits_t bits = { 0 };
    lsmash_bs_t   bs   = { 0 };
    uint8_t rbsp_buffer[4];
    uint8_t buffer     [4];
    bs.data  = buffer;
    bs.alloc = 4;
    lsmash_bits_init( &bits, &bs );
    if( h264_import_rbsp_from_ebsp( &bits, rbsp_buffer, ps_ebsp, LSMASH_MIN( ps_ebsp_length, 4 ) ) )
        return -1;
    uint64_t pic_parameter_set_id = h264_get_exp_golomb_ue( &bits );
    IF_INVALID_VALUE( pic_parameter_set_id > 255 )
        return -1;
    *ps_id = pic_parameter_set_id;
    return bs.error ? -1 : 0;
}

static inline int h264_get_ps_id( uint8_t *ps_ebsp, uint32_t ps_ebsp_length,
                                  uint8_t *ps_id, lsmash_h264_parameter_set_type ps_type )
{
    int (*get_ps_id)( uint8_t *ps_ebsp, uint32_t ps_ebsp_length, uint8_t *ps_id )
        = ps_type == H264_PARAMETER_SET_TYPE_SPS ? h264_get_sps_id
        : ps_type == H264_PARAMETER_SET_TYPE_PPS ? h264_get_pps_id
        :                                          NULL;
    return get_ps_id ? get_ps_id( ps_ebsp, ps_ebsp_length, ps_id ) : -1;
}

static inline lsmash_entry_list_t *h264_get_parameter_set_list( lsmash_h264_specific_parameters_t *param,
                                                                lsmash_h264_parameter_set_type ps_type )
{
    if( !param->parameter_sets )
        return NULL;
    return ps_type == H264_PARAMETER_SET_TYPE_SPS    ? param->parameter_sets->sps_list
         : ps_type == H264_PARAMETER_SET_TYPE_PPS    ? param->parameter_sets->pps_list
         : ps_type == H264_PARAMETER_SET_TYPE_SPSEXT ? param->parameter_sets->spsext_list
         : NULL;
}

static lsmash_entry_t *h264_get_ps_entry_from_param( lsmash_h264_specific_parameters_t *param,
                                                     lsmash_h264_parameter_set_type ps_type,
                                                     uint8_t ps_id )
{
    int (*get_ps_id)( uint8_t *ps_ebsp, uint32_t ps_ebsp_length, uint8_t *ps_id )
        = ps_type == H264_PARAMETER_SET_TYPE_SPS ? h264_get_sps_id
        : ps_type == H264_PARAMETER_SET_TYPE_PPS ? h264_get_pps_id
        :                                          NULL;
    if( !get_ps_id )
        return NULL;
    lsmash_entry_list_t *ps_list = h264_get_parameter_set_list( param, ps_type );
    if( !ps_list )
        return NULL;
    for( lsmash_entry_t *entry = ps_list->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *ps = (isom_avcC_ps_entry_t *)entry->data;
        if( !ps )
            return NULL;
        uint8_t param_ps_id;
        if( get_ps_id( ps->parameterSetNALUnit + 1, ps->parameterSetLength - 1, &param_ps_id ) )
            return NULL;
        if( ps_id == param_ps_id )
            return entry;
    }
    return NULL;
}

static inline int h264_get_max_ps_length( lsmash_entry_list_t *ps_list, uint32_t *max_ps_length )
{
    *max_ps_length = 0;
    for( lsmash_entry_t *entry = ps_list->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *ps = (isom_avcC_ps_entry_t *)entry->data;
        if( !ps )
            return -1;
        *max_ps_length = LSMASH_MAX( *max_ps_length, ps->parameterSetLength );
    }
    return 0;
}

static inline int h264_get_ps_count( lsmash_entry_list_t *ps_list, uint32_t *ps_count )
{
    *ps_count = 0;
    for( lsmash_entry_t *entry = ps_list->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *ps = (isom_avcC_ps_entry_t *)entry->data;
        if( !ps )
            return -1;
        ++(*ps_count);
    }
    return 0;
}

static inline int h264_check_same_ps_existence( lsmash_entry_list_t *ps_list, void *ps_data, uint32_t ps_length )
{
    for( lsmash_entry_t *entry = ps_list->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *ps = (isom_avcC_ps_entry_t *)entry->data;
        if( !ps )
            return -1;
        if( ps->parameterSetLength == ps_length && !memcmp( ps->parameterSetNALUnit, ps_data, ps_length ) )
            return 1;   /* The same parameter set already exists. */
    }
    return 0;
}

static inline int h264_validate_ps_type( lsmash_h264_parameter_set_type ps_type, void *ps_data, uint32_t ps_length )
{
    if( !ps_data || ps_length < 2 )
        return -1;
    if( ps_type != H264_PARAMETER_SET_TYPE_SPS
     && ps_type != H264_PARAMETER_SET_TYPE_PPS
     && ps_type != H264_PARAMETER_SET_TYPE_SPSEXT )
        return -1;
    uint8_t nalu_type = *((uint8_t *)ps_data) & 0x1f;
    if( nalu_type != 7 && nalu_type != 8 && nalu_type != 13 )
        return -1;
    if( (ps_type == H264_PARAMETER_SET_TYPE_SPS    && nalu_type != 7)
     || (ps_type == H264_PARAMETER_SET_TYPE_PPS    && nalu_type != 8)
     || (ps_type == H264_PARAMETER_SET_TYPE_SPSEXT && nalu_type != 13) )
        return -1;
    return 0;
}

/* Return 1 if a new parameter set is appendable.
 * Return 0 if no need to append a new parameter set.
 * Return -1 if there is error.
 * Return -2 if a new specific info is needed. */
int lsmash_check_h264_parameter_set_appendable( lsmash_h264_specific_parameters_t *param,
                                                lsmash_h264_parameter_set_type ps_type,
                                                void *ps_data, uint32_t ps_length )
{
    if( !param )
        return -1;
    if( h264_validate_ps_type( ps_type, ps_data, ps_length ) )
        return -1;
    if( ps_type == H264_PARAMETER_SET_TYPE_SPSEXT
     && !ISOM_REQUIRES_AVCC_EXTENSION( param->AVCProfileIndication ) )
        return 0;
    /* Check whether the same parameter set already exsits or not. */
    lsmash_entry_list_t *ps_list = h264_get_parameter_set_list( param, ps_type );
    if( !ps_list || !ps_list->head )
        return 1;   /* No parameter set */
    switch( h264_check_same_ps_existence( ps_list, ps_data, ps_length ) )
    {
        case 0  : break;
        case 1  : return 0;     /* The same parameter set already exists. */
        default : return -1;    /* An error occured. */
    }
    uint32_t max_ps_length;
    if( h264_get_max_ps_length( ps_list, &max_ps_length ) )
        return -1;
    max_ps_length = LSMASH_MAX( max_ps_length, ps_length );
    uint32_t ps_count;
    if( h264_get_ps_count( ps_list, &ps_count ) )
        return -1;
    if( (ps_type == H264_PARAMETER_SET_TYPE_SPS    && ps_count >= 31)
     || (ps_type == H264_PARAMETER_SET_TYPE_PPS    && ps_count >= 255)
     || (ps_type == H264_PARAMETER_SET_TYPE_SPSEXT && ps_count >= 255) )
        return -2;  /* No more appendable parameter sets. */
    if( ps_type == H264_PARAMETER_SET_TYPE_SPSEXT )
        return 1;
    /* Check whether a new specific info is needed or not. */
    lsmash_bits_t bits = { 0 };
    lsmash_bs_t   bs   = { 0 };
    uint8_t rbsp_buffer[max_ps_length];
    uint8_t buffer     [max_ps_length];
    bs.data  = buffer;
    bs.alloc = max_ps_length;
    lsmash_bits_init( &bits, &bs );
    if( ps_type == H264_PARAMETER_SET_TYPE_PPS )
    {
        /* PPS */
        uint8_t pps_id;
        if( h264_get_pps_id( ps_data + 1, ps_length - 1, &pps_id ) )
            return -1;
        for( lsmash_entry_t *entry = ps_list->head; entry; entry = entry->next )
        {
            isom_avcC_ps_entry_t *ps = (isom_avcC_ps_entry_t *)entry->data;
            if( !ps )
                return -1;
            uint8_t param_pps_id;
            if( h264_get_pps_id( ps->parameterSetNALUnit + 1, ps->parameterSetLength - 1, &param_pps_id ) )
                return -1;
            if( pps_id == param_pps_id )
                return -2;  /* PPS that has the same pic_parameter_set_id already exists with different form. */
        }
        return 0;
    }
    /* SPS */
    h264_sps_t sps;
    if( h264_parse_sps_easy( &bits, &sps, rbsp_buffer, ps_data + 1, ps_length - 1 ) )
        return -1;
    lsmash_bits_empty( &bits );
    /* FIXME; If the sequence parameter sets are marked with different profiles,
     * and the relevant profile compatibility flags are all zero,
     * then the stream may need examination to determine which profile, if any, the stream conforms to.
     * If the stream is not examined, or the examination reveals that there is no profile to which the stream conforms,
     * then the stream must be split into two or more sub-streams with separate configuration records in which these rules can be met. */
#if 0
    if( sps.profile_idc != param->AVCProfileIndication && (sps->constraint_set_flags & param->profile_compatibility) )
#else
    if( sps.profile_idc != param->AVCProfileIndication )
#endif
        return -2;
    /* The values of chroma_format_idc, bit_depth_luma_minus8 and bit_depth_chroma_minus8
     * must be identical in all SPSs in a single AVC configuration record. */
    if( ISOM_REQUIRES_AVCC_EXTENSION( param->AVCProfileIndication )
     && (sps.chroma_format_idc       != param->chroma_format
     ||  sps.bit_depth_luma_minus8   != param->bit_depth_luma_minus8
     ||  sps.bit_depth_chroma_minus8 != param->bit_depth_chroma_minus8) )
        return -2;
    /* Forbidden to duplicate SPS that has the same seq_parameter_set_id with different form within the same configuration record. */
    uint8_t sps_id = sps.seq_parameter_set_id;
    for( lsmash_entry_t *entry = ps_list->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *ps = (isom_avcC_ps_entry_t *)entry->data;
        if( !ps )
            return -1;
        uint8_t param_sps_id;
        if( h264_get_sps_id( ps->parameterSetNALUnit + 1, ps->parameterSetLength - 1, &param_sps_id ) )
            return -1;
        if( sps_id == param_sps_id )
            return -2;  /* SPS that has the same seq_parameter_set_id already exists with different form. */
    }
    return 0;
}

int lsmash_append_h264_parameter_set( lsmash_h264_specific_parameters_t *param,
                                      lsmash_h264_parameter_set_type ps_type,
                                      void *ps_data, uint32_t ps_length )
{
    if( !param || !ps_data || ps_length < 2 )
        return -1;
    if( !param->parameter_sets )
    {
        param->parameter_sets = lsmash_malloc_zero( sizeof(lsmash_h264_parameter_sets_t) );
        if( !param->parameter_sets )
            return -1;
    }
    lsmash_entry_list_t *ps_list = h264_get_parameter_set_list( param, ps_type );
    if( !ps_list )
        return -1;
    if( ps_type != H264_PARAMETER_SET_TYPE_SPS
     && ps_type != H264_PARAMETER_SET_TYPE_PPS
     && ps_type != H264_PARAMETER_SET_TYPE_SPSEXT )
        return -1;
    if( ps_type == H264_PARAMETER_SET_TYPE_SPSEXT )
    {
        if( !ISOM_REQUIRES_AVCC_EXTENSION( param->AVCProfileIndication ) )
            return 0;
        isom_avcC_ps_entry_t *ps = isom_create_ps_entry( ps_data, ps_length );
        if( !ps )
            return -1;
        if( lsmash_add_entry( ps_list, ps ) )
        {
            isom_remove_avcC_ps( ps );
            return -1;
        }
        return 0;
    }
    /* Check if the same parameter set identifier already exists. */
    uint8_t ps_id;
    if( h264_get_ps_id( ps_data + 1, ps_length - 1, &ps_id, ps_type ) )
        return -1;
    lsmash_entry_t *entry = h264_get_ps_entry_from_param( param, ps_type, ps_id );
    if( entry )
        return -1;  /* The same parameter set identifier already exists. */
    isom_avcC_ps_entry_t *ps = isom_create_ps_entry( ps_data, ps_length );
    if( !ps )
        return -1;
    if( lsmash_add_entry( ps_list, ps ) )
    {
        isom_remove_avcC_ps( ps );
        return -1;
    }
    if( ps_type == H264_PARAMETER_SET_TYPE_SPS )
    {
        /* Update specific info with SPS. */
        lsmash_bits_t bits = { 0 };
        lsmash_bs_t   bs   = { 0 };
        uint8_t rbsp_buffer[ps_length];
        uint8_t buffer     [ps_length];
        bs.data  = buffer;
        bs.alloc = ps_length;
        lsmash_bits_init( &bits, &bs );
        h264_sps_t sps;
        if( h264_parse_sps_easy( &bits, &sps, rbsp_buffer, ps_data + 1, ps_length - 1 ) )
        {
            lsmash_remove_entry_direct( ps_list, ps_list->tail, isom_remove_avcC_ps );
            return -1;
        }
        if( ps_list->entry_count == 1 )
            param->profile_compatibility = 0xff;
        param->AVCProfileIndication    = sps.profile_idc;
        param->profile_compatibility  &= sps.constraint_set_flags;
        param->AVCLevelIndication      = LSMASH_MAX( param->AVCLevelIndication, sps.level_idc );
        param->chroma_format           = sps.chroma_format_idc;
        param->bit_depth_luma_minus8   = sps.bit_depth_luma_minus8;
        param->bit_depth_chroma_minus8 = sps.bit_depth_chroma_minus8;
    }
    /* Add a new parameter set in order of ascending parameter set identifier. */
    int append_head = 0;
    if( ps_id )
        for( int i = ps_id - 1; i; i-- )
        {
            entry = h264_get_ps_entry_from_param( param, ps_type, i );
            if( entry )
                break;
        }
    if( ps_id == 0 || !entry )
    {
        /* Couldn't find parameter set with lower identifier.
         * Next, find parameter set with upper identifier. */
        int max_ps_id = ps_type == H264_PARAMETER_SET_TYPE_SPS ? 31 : 255;
        for( int i = ps_id + 1; i <= max_ps_id; i++ )
        {
            entry = h264_get_ps_entry_from_param( param, ps_type, i );
            if( entry )
                break;
        }
        if( entry )
            append_head = 1;
    }
    if( !entry )
        return 0;   /* The new entry was appended to tail. */
    lsmash_entry_t *new_entry = ps_list->tail;
    if( append_head )
    {
        /* before: entry[i > ps_id] ... -> prev_entry -> new_entry[ps_id]
         * after:  new_entry[ps_id] -> entry[i > ps_id] -> ... -> prev_entry */
        if( new_entry->prev )
            new_entry->prev->next = NULL;
        new_entry->prev = NULL;
        entry->prev = new_entry;
        new_entry->next = entry;
        return 0;
    }
    /* before: entry[i < ps_id] -> next_entry -> ... -> prev_entry -> new_entry[ps_id]
     * after:  entry[i < ps_id] -> new_entry[ps_id] -> next_entry -> ... -> prev_entry */
    if( new_entry->prev )
        new_entry->prev->next = NULL;
    new_entry->prev = entry;
    new_entry->next = entry->next;
    if( entry->next )
        entry->next->prev = new_entry;
    entry->next = new_entry;
    return 0;
}

int h264_try_to_append_parameter_set( h264_info_t *info, lsmash_h264_parameter_set_type ps_type, void *ps_data, uint32_t ps_length )
{
    lsmash_h264_specific_parameters_t *param = &info->avcC_param;
    int ret = lsmash_check_h264_parameter_set_appendable( param, ps_type, ps_data, ps_length );
    switch( ret )
    {
        case -1 :   /* Error */
        case -2 :   /* Mulitiple sample description is needed. */
            return ret;
        case 1 :    /* Appendable */
            switch( ps_type )
            {
                case H264_PARAMETER_SET_TYPE_SPS :
                    if( h264_parse_sps( info, info->buffer.rbsp, ps_data + 1, ps_length - 1 ) )
                        return -1;
                    break;
                case H264_PARAMETER_SET_TYPE_PPS :
                    if( h264_parse_pps( info, info->buffer.rbsp, ps_data + 1, ps_length - 1 ) )
                        return -1;
                    break;
                default :
                    break;
            }
            return lsmash_append_h264_parameter_set( param, ps_type, ps_data, ps_length );
        default :   /* No need to append */
            return 0;
    }
}

static int h264_parse_succeeded( h264_info_t *info, lsmash_h264_specific_parameters_t *param )
{
    int ret;
    if( info->sps.present && info->pps.present )
    {
        *param = info->avcC_param;
        /* Avoid freeing parameter sets. */
        info->avcC_param.parameter_sets = NULL;
        ret = 0;
    }
    else
        ret = -1;
    h264_cleanup_parser( info );
    return ret;
}

static inline int h264_parse_failed( h264_info_t *info )
{
    h264_cleanup_parser( info );
    return -1;
}

int lsmash_setup_h264_specific_parameters_from_access_unit( lsmash_h264_specific_parameters_t *param, uint8_t *data, uint32_t data_length )
{
    if( !param || !data || data_length == 0 )
        return -1;
    h264_info_t  handler = { { 0 } };
    h264_info_t *info    = &handler;
    if( h264_setup_parser( info, 1, h264_update_buffer_from_access_unit ) )
        return h264_parse_failed( info );
    h264_stream_buffer_t *buffer = &info->buffer;
    h264_slice_info_t    *slice  = &info->slice;
    h264_data_stream_handler_t stream = { 0 };
    stream.data             = data;
    stream.remainder_length = data_length;
    h264_nalu_header_t nalu_header = { 0 };
    uint64_t consecutive_zero_byte_count = 0;
    uint64_t ebsp_length = 0;
    int      no_more_buf = 0;
    int      complete_au = 0;
    while( 1 )
    {
        buffer->update( info, &stream, 2 );
        no_more_buf = buffer->pos >= buffer->end;
        int no_more = info->no_more_read && no_more_buf;
        if( !h264_check_next_short_start_code( buffer->pos, buffer->end ) && !no_more )
        {
            if( *(buffer->pos ++) )
                consecutive_zero_byte_count = 0;
            else
                ++consecutive_zero_byte_count;
            ++ebsp_length;
            continue;
        }
        if( no_more && ebsp_length == 0 )
            /* For the last NALU. This NALU already has been parsed. */
            return h264_parse_succeeded( info, param );
        uint64_t next_nalu_head_pos = info->ebsp_head_pos + ebsp_length + !no_more * H264_SHORT_START_CODE_LENGTH;
        uint8_t *next_short_start_code_pos = buffer->pos;       /* Memorize position of short start code of the next NALU in buffer.
                                                                 * This is used when backward reading of stream doesn't occur. */
        uint8_t nalu_type = nalu_header.nal_unit_type;
        int read_back = 0;
        if( nalu_type == 12 )
        {
            /* We don't support streams with both filler and HRD yet.
             * Otherwise, just skip filler because elemental streams defined in 14496-15 are forbidden to use filler. */
            if( info->sps.hrd_present )
                return h264_parse_failed( info );
        }
        else if( (nalu_type >= 1 && nalu_type <= 13) || nalu_type == 19 )
        {
            /* Get the EBSP of the current NALU here.
             * AVC elemental stream defined in 14496-15 can recognize from 0 to 13, and 19 of nal_unit_type.
             * We don't support SVC and MVC elemental stream defined in 14496-15 yet. */
            ebsp_length -= consecutive_zero_byte_count;     /* Any EBSP doesn't have zero bytes at the end. */
            uint64_t nalu_length = nalu_header.length + ebsp_length;
            if( buffer->bank->buffer_size < (H264_DEFAULT_NALU_LENGTH_SIZE + nalu_length) )
            {
                if( h264_supplement_buffer( buffer, NULL, 2 * (H264_DEFAULT_NALU_LENGTH_SIZE + nalu_length) ) )
                    return h264_parse_failed( info );
                next_short_start_code_pos = buffer->pos;
            }
            /* Move to the first byte of the current NALU. */
            read_back = (buffer->pos - buffer->start) < (nalu_length + consecutive_zero_byte_count);
            if( read_back )
            {
                memcpy( buffer->start, stream.data + info->ebsp_head_pos - nalu_header.length, nalu_length );
                buffer->pos = buffer->start;
                buffer->end = buffer->start + nalu_length;
            }
            else
                buffer->pos -= nalu_length + consecutive_zero_byte_count;
            if( nalu_type >= 1 && nalu_type <= 5 )
            {
                /* VCL NALU (slice) */
                h264_slice_info_t prev_slice = *slice;
                if( h264_parse_slice( info, &nalu_header, buffer->rbsp,
                                      buffer->pos + nalu_header.length, ebsp_length ) )
                    return h264_parse_failed( info );
                if( prev_slice.present )
                {
                    /* Check whether the AU that contains the previous VCL NALU completed or not. */
                    if( h264_find_au_delimit_by_slice_info( slice, &prev_slice ) )
                        /* The current NALU is the first VCL NALU of the primary coded picture of an new AU.
                         * Therefore, the previous slice belongs to that new AU. */
                        complete_au = 1;
                }
                slice->present = 1;
            }
            else
            {
                if( h264_find_au_delimit_by_nalu_type( nalu_type, info->prev_nalu_type ) )
                {
                    /* The last slice belongs to the AU you want at this time. */
                    slice->present = 0;
                    complete_au = 1;
                }
                else if( no_more )
                    complete_au = 1;
                switch( nalu_type )
                {
                    case 7 :    /* Sequence Parameter Set */
                        if( h264_try_to_append_parameter_set( info, H264_PARAMETER_SET_TYPE_SPS, buffer->pos, nalu_length ) )
                            return h264_parse_failed( info );
                        break;
                    case 8 :    /* Picture Parameter Set */
                        if( h264_try_to_append_parameter_set( info, H264_PARAMETER_SET_TYPE_PPS, buffer->pos, nalu_length ) )
                            return h264_parse_failed( info );
                        break;
                    case 13 :   /* Sequence Parameter Set Extension */
                        if( h264_try_to_append_parameter_set( info, H264_PARAMETER_SET_TYPE_SPSEXT, buffer->pos, nalu_length ) )
                            return h264_parse_failed( info );
                        break;
                    default :
                        break;
                }
            }
        }
        /* Move to the first byte of the next NALU. */
        if( read_back )
        {
            uint64_t wasted_data_length = LSMASH_MIN( stream.remainder_length, buffer->bank->buffer_size );
            memcpy( buffer->start, stream.data + next_nalu_head_pos, wasted_data_length );
            stream.overall_wasted_length = next_nalu_head_pos + wasted_data_length;
            stream.remainder_length      = data_length - stream.overall_wasted_length;
            buffer->pos = buffer->start;
            buffer->end = buffer->start + wasted_data_length;
        }
        else
            buffer->pos = next_short_start_code_pos + H264_SHORT_START_CODE_LENGTH;
        info->prev_nalu_type = nalu_type;
        buffer->update( info, &stream, 0 );
        no_more_buf = buffer->pos >= buffer->end;
        ebsp_length = 0;
        no_more = info->no_more_read && no_more_buf;
        if( !no_more && !complete_au )
        {
            /* Check the next NALU header. */
            if( h264_check_nalu_header( &nalu_header, &buffer->pos, !!consecutive_zero_byte_count ) )
                return h264_parse_failed( info );
            info->ebsp_head_pos = next_nalu_head_pos + nalu_header.length;
        }
        else
            return h264_parse_succeeded( info, param );
        consecutive_zero_byte_count = 0;
    }
}

static int isom_get_avcC_ps( lsmash_bs_t *bs, lsmash_entry_list_t *list, uint8_t entry_count )
{
    for( uint8_t i = 0; i < entry_count; i++ )
    {
        isom_avcC_ps_entry_t *data = malloc( sizeof(isom_avcC_ps_entry_t) );
        if( !data )
            return -1;
        if( lsmash_add_entry( list, data ) )
        {
            free( data );
            return -1;
        }
        data->parameterSetLength  = lsmash_bs_get_be16( bs );
        data->parameterSetNALUnit = lsmash_bs_get_bytes( bs, data->parameterSetLength );
        if( !data->parameterSetNALUnit )
        {
            lsmash_remove_entries( list, isom_remove_avcC_ps );
            return -1;
        }
    }
    return 0;
}

int h264_construct_specific_parameters( lsmash_codec_specific_t *dst, lsmash_codec_specific_t *src )
{
    assert( dst && dst->data.structured && src && src->data.unstructured );
    if( src->size < ISOM_BASEBOX_COMMON_SIZE + 7 )
        return -1;
    lsmash_h264_specific_parameters_t *param = (lsmash_h264_specific_parameters_t *)dst->data.structured;
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
    if( !param->parameter_sets )
    {
        param->parameter_sets = lsmash_malloc_zero( sizeof(lsmash_h264_parameter_sets_t) );
        if( !param->parameter_sets )
            return -1;
    }
    lsmash_bs_t *bs = lsmash_bs_create( NULL );
    if( !bs )
        return -1;
    if( lsmash_bs_import_data( bs, data, src->size - (src->data.unstructured - data) ) )
        goto fail;
    if( lsmash_bs_get_byte( bs ) != 1 )
        goto fail;  /* We don't support configurationVersion other than 1. */
    param->AVCProfileIndication  = lsmash_bs_get_byte( bs );
    param->profile_compatibility = lsmash_bs_get_byte( bs );
    param->AVCLevelIndication    = lsmash_bs_get_byte( bs );
    param->lengthSizeMinusOne    = lsmash_bs_get_byte( bs ) & 0x03;
    uint8_t numOfSequenceParameterSets = lsmash_bs_get_byte( bs ) & 0x1F;
    if( numOfSequenceParameterSets
     && isom_get_avcC_ps( bs, param->parameter_sets->sps_list, numOfSequenceParameterSets ) )
        goto fail;
    uint8_t numOfPictureParameterSets = lsmash_bs_get_byte( bs );
    if( numOfPictureParameterSets
     && isom_get_avcC_ps( bs, param->parameter_sets->pps_list, numOfPictureParameterSets ) )
        goto fail;
    if( ISOM_REQUIRES_AVCC_EXTENSION( param->AVCProfileIndication ) )
    {
        param->chroma_format           = lsmash_bs_get_byte( bs ) & 0x03;
        param->bit_depth_luma_minus8   = lsmash_bs_get_byte( bs ) & 0x07;
        param->bit_depth_chroma_minus8 = lsmash_bs_get_byte( bs ) & 0x07;
        uint8_t numOfSequenceParameterSetExt = lsmash_bs_get_byte( bs );
        if( numOfSequenceParameterSetExt
         && isom_get_avcC_ps( bs, param->parameter_sets->spsext_list, numOfSequenceParameterSetExt ) )
            goto fail;
    }
    lsmash_bs_cleanup( bs );
    return 0;
fail:
    lsmash_bs_cleanup( bs );
    return -1;
}

int h264_print_codec_specific( FILE *fp, lsmash_root_t *root, isom_box_t *box, int level )
{
    assert( fp && root && box );
    int indent = level;
    lsmash_ifprintf( fp, indent++, "[%s: AVC Configuration Box]\n", isom_4cc2str( box->type ) );
    lsmash_ifprintf( fp, indent, "position = %"PRIu64"\n", box->pos );
    lsmash_ifprintf( fp, indent, "size = %"PRIu64"\n", box->size );
    isom_extension_box_t *ext = (isom_extension_box_t *)box;
    assert( ext->format == EXTENSION_FORMAT_BINARY );
    uint8_t *data = ext->form.binary;
    uint32_t offset = isom_skip_box_common( &data );
    lsmash_bs_t *bs = lsmash_bs_create( NULL );
    if( !bs )
        return -1;
    if( lsmash_bs_import_data( bs, data, ext->size - offset ) )
    {
        lsmash_bs_cleanup( bs );
        return -1;
    }
    lsmash_ifprintf( fp, indent, "configurationVersion = %"PRIu8"\n", lsmash_bs_get_byte( bs ) );
    uint8_t AVCProfileIndication = lsmash_bs_get_byte( bs );
    lsmash_ifprintf( fp, indent, "AVCProfileIndication = %"PRIu8"\n", AVCProfileIndication );
    lsmash_ifprintf( fp, indent, "profile_compatibility = 0x%02"PRIx8"\n", lsmash_bs_get_byte( bs ) );
    lsmash_ifprintf( fp, indent, "AVCLevelIndication = %"PRIu8"\n", lsmash_bs_get_byte( bs ) );
    uint8_t temp8 = lsmash_bs_get_byte( bs );
    lsmash_ifprintf( fp, indent, "reserved = 0x%02"PRIx8"\n", (temp8 >> 2) & 0x3F );
    lsmash_ifprintf( fp, indent, "lengthSizeMinusOne = %"PRIu8"\n", temp8 & 0x03 );
    temp8 = lsmash_bs_get_byte( bs );
    lsmash_ifprintf( fp, indent, "reserved = 0x%02"PRIx8"\n", (temp8 >> 5) & 0x07 );
    uint8_t numOfSequenceParameterSets = temp8 & 0x1f;
    lsmash_ifprintf( fp, indent, "numOfSequenceParameterSets = %"PRIu8"\n", numOfSequenceParameterSets );
    for( uint8_t i = 0; i < numOfSequenceParameterSets; i++ )
    {
        uint16_t parameterSetLength = lsmash_bs_get_be16( bs );
        lsmash_bs_get_bytes( bs, parameterSetLength );
    }
    uint8_t numOfPictureParameterSets = lsmash_bs_get_byte( bs );
    lsmash_ifprintf( fp, indent, "numOfPictureParameterSets = %"PRIu8"\n", numOfPictureParameterSets );
    for( uint8_t i = 0; i < numOfPictureParameterSets; i++ )
    {
        uint16_t parameterSetLength = lsmash_bs_get_be16( bs );
        lsmash_bs_get_bytes( bs, parameterSetLength );
    }
    /* Note: there are too many files, in the world, that don't contain the following fields. */
    if( ISOM_REQUIRES_AVCC_EXTENSION( AVCProfileIndication )
     && (lsmash_bs_get_pos( bs ) < (ext->size - offset)) )
    {
        temp8 = lsmash_bs_get_byte( bs );
        lsmash_ifprintf( fp, indent, "reserved = 0x%02"PRIx8"\n", (temp8 >> 2) & 0x3F );
        lsmash_ifprintf( fp, indent, "chroma_format = %"PRIu8"\n", temp8 & 0x03 );
        temp8 = lsmash_bs_get_byte( bs );
        lsmash_ifprintf( fp, indent, "reserved = 0x%02"PRIx8"\n", (temp8 >> 3) & 0x1F );
        lsmash_ifprintf( fp, indent, "bit_depth_luma_minus8 = %"PRIu8"\n", temp8 & 0x7 );
        temp8 = lsmash_bs_get_byte( bs );
        lsmash_ifprintf( fp, indent, "reserved = 0x%02"PRIx8"\n", (temp8 >> 3) & 0x1F );
        lsmash_ifprintf( fp, indent, "bit_depth_chroma_minus8 = %"PRIu8"\n", temp8 & 0x7 );
        lsmash_ifprintf( fp, indent, "numOfSequenceParameterSetExt = %"PRIu8"\n", lsmash_bs_get_byte( bs ) );
    }
    lsmash_bs_cleanup( bs );
    return 0;
}

int h264_copy_codec_specific( lsmash_codec_specific_t *dst, lsmash_codec_specific_t *src )
{
    assert( src && src->format == LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED && src->data.structured );
    assert( dst && dst->format == LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED && dst->data.structured );
    lsmash_h264_specific_parameters_t *src_data = (lsmash_h264_specific_parameters_t *)src->data.structured;
    lsmash_h264_specific_parameters_t *dst_data = (lsmash_h264_specific_parameters_t *)dst->data.structured;
    lsmash_destroy_h264_parameter_sets( dst_data );
    *dst_data = *src_data;
    if( !src_data->parameter_sets )
        return 0;
    dst_data->parameter_sets = lsmash_malloc_zero( sizeof(lsmash_h264_parameter_sets_t) );
    if( !dst_data->parameter_sets )
        return -1;
    for( int i = 0; i < 3; i++ )
    {
        lsmash_entry_list_t *src_ps_list = h264_get_parameter_set_list( src_data, i );
        lsmash_entry_list_t *dst_ps_list = h264_get_parameter_set_list( dst_data, i );
        assert( src_ps_list && dst_ps_list );
        for( lsmash_entry_t *entry = src_ps_list->head; entry; entry = entry->next )
        {
            isom_avcC_ps_entry_t *src_ps = (isom_avcC_ps_entry_t *)entry->data;
            if( !src_ps )
                continue;
            isom_avcC_ps_entry_t *dst_ps = isom_create_ps_entry( src_ps->parameterSetNALUnit, src_ps->parameterSetLength );
            if( !dst_ps )
            {
                lsmash_destroy_h264_parameter_sets( dst_data );
                return -1;
            }
            if( lsmash_add_entry( dst_ps_list, dst_ps ) )
            {
                lsmash_destroy_h264_parameter_sets( dst_data );
                isom_remove_avcC_ps( dst_ps );
                return -1;
            }
        }
    }
    return 0;
}

int h264_print_bitrate( FILE *fp, lsmash_root_t *root, isom_box_t *box, int level )
{
    assert( fp && root && box );
    int indent = level;
    lsmash_ifprintf( fp, indent++, "[%s: MPEG-4 Bit Rate Box]\n", isom_4cc2str( box->type ) );
    lsmash_ifprintf( fp, indent, "position = %"PRIu64"\n", box->pos );
    lsmash_ifprintf( fp, indent, "size = %"PRIu64"\n", box->size );
    isom_btrt_t *btrt = (isom_btrt_t *)box;
    lsmash_ifprintf( fp, indent, "bufferSizeDB = %"PRIu32"\n", btrt->bufferSizeDB );
    lsmash_ifprintf( fp, indent, "maxBitrate = %"PRIu32"\n", btrt->maxBitrate );
    lsmash_ifprintf( fp, indent, "avgBitrate = %"PRIu32"\n", btrt->avgBitrate );
    return 0;
}
