/*****************************************************************************
 * av1.c
 *****************************************************************************
 * Copyright (C) 2018-2020 L-SMASH project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *          Derek Buitenhuis <derek.buitenhuis@gmail.com>
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

#include <inttypes.h>
#include <string.h>

#include "core/box.h"

/*********************************************************************************
    Alliance for Open Media AV1
    References:
        AV1 Bitstream & Decoding Process Specification
            Version 1.0.0
        AV1 Codec ISO Media File Format Binding
            v1.2.0, 12 December 2019
**********************************************************************************/

#define AV1_CODEC_CONFIGURATION_RECORD_MARKER (1)
#define AV1_CODEC_CONFIGURATION_RECORD_VERSION_1 (1)

#define AV1_SPECIFIC_BOX_MIN_LENGTH (4)

void av1_destruct_specific_data
(
    void *data
)
{
    if( !data )
        return;
    lsmash_av1_specific_parameters_t *param = data;
    lsmash_free( param->configOBUs.data );
    lsmash_free( data );
}

int av1_construct_specific_parameters
(
    lsmash_codec_specific_t *dst,
    lsmash_codec_specific_t *src
)
{
    assert( dst && dst->data.structured && src && src->data.unstructured );
    if( src->size < ISOM_BASEBOX_COMMON_SIZE + 4 )
        return LSMASH_ERR_INVALID_DATA;
    lsmash_av1_specific_parameters_t *param = (lsmash_av1_specific_parameters_t *)dst->data.structured;
    uint8_t *data = src->data.unstructured;
    uint64_t size = LSMASH_GET_BE32( data );
    data += ISOM_BASEBOX_COMMON_SIZE;
    if( size == 1 )
    {
        size = LSMASH_GET_BE64( data );
        data += 8;
    }
    if( size != src->size )
        return LSMASH_ERR_INVALID_DATA;
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
        return LSMASH_ERR_MEMORY_ALLOC;
    uint32_t cr_size = src->size - (data - src->data.unstructured);
    int err = lsmash_bs_import_data( bs, data, cr_size );
    if( err < 0 )
        goto fail;
    uint8_t temp8 = lsmash_bs_get_byte( bs );
    if( (temp8 >> 7) != AV1_CODEC_CONFIGURATION_RECORD_MARKER )
    {
        /* The marker bit shall be set to 1. */
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }
    if( (temp8 & 0x7F) != AV1_CODEC_CONFIGURATION_RECORD_VERSION_1 )
    {
        /* We don't support 'version' other than 1. */
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }
    temp8 = lsmash_bs_get_byte( bs );
    param->seq_profile     = temp8 >> 5;
    param->seq_level_idx_0 = temp8 & 0x1F;
    temp8 = lsmash_bs_get_byte( bs );
    param->seq_tier_0             =  temp8 >> 7;
    param->high_bitdepth          = (temp8 >> 6) & 0x01;
    param->twelve_bit             = (temp8 >> 5) & 0x01;
    param->monochrome             = (temp8 >> 4) & 0x01;
    param->chroma_subsampling_x   = (temp8 >> 3) & 0x01;
    param->chroma_subsampling_y   = (temp8 >> 2) & 0x01;
    param->chroma_sample_position =  temp8       & 0x03;
    temp8 = lsmash_bs_get_byte( bs );
    param->initial_presentation_delay_present = (temp8 >> 4) & 0x01;
    if( param->initial_presentation_delay_present )
        param->initial_presentation_delay_minus_one = temp8 & 0x0F;
    param->configOBUs.sz = cr_size - 4;
    if ( param->configOBUs.sz > 0 )
    {
        param->configOBUs.data = lsmash_malloc( param->configOBUs.sz );
        if( !param->configOBUs.data )
            return LSMASH_ERR_MEMORY_ALLOC;
        err = lsmash_bs_get_bytes_ex( bs, param->configOBUs.sz,  param->configOBUs.data );
        if ( err < 0 )
            goto fail;
    }
    else
        param->configOBUs.data = NULL;

    lsmash_bs_cleanup( bs );
    return 0;

fail:
    lsmash_bs_cleanup( bs );
    return err;
}

uint8_t *lsmash_create_av1_specific_info
(
    lsmash_av1_specific_parameters_t *param,
    uint32_t                         *data_length
)
{
    if( !param || !data_length )
        return NULL;
    /* Create an AV1CodecConfigurationBox */
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
        return NULL;
    lsmash_bs_put_be32( bs, 0 );                            /* box size */
    lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_AV1C.fourcc );    /* box type: 'av1C' */
    uint8_t temp8;
    temp8 = (AV1_CODEC_CONFIGURATION_RECORD_MARKER << 7)
          |  AV1_CODEC_CONFIGURATION_RECORD_VERSION_1;
    lsmash_bs_put_byte( bs, temp8 );
    temp8 = (param->seq_profile << 5)
          | (param->seq_level_idx_0 & 0x1F);
    lsmash_bs_put_byte( bs, temp8 );
    temp8 = ((param->seq_tier_0           << 7) & (1 << 7))
          | ((param->high_bitdepth        << 6) & (1 << 6))
          | ((param->twelve_bit           << 5) & (1 << 5))
          | ((param->monochrome           << 4) & (1 << 4))
          | ((param->chroma_subsampling_x << 3) & (1 << 3))
          | ((param->chroma_subsampling_y << 2) & (1 << 2))
          | ( param->chroma_sample_position     & 0x03);
    lsmash_bs_put_byte( bs, temp8 );
    if( param->initial_presentation_delay_present )
        lsmash_bs_put_byte( bs, 0x10 | (param->initial_presentation_delay_minus_one & 0x0F) );
    else
        lsmash_bs_put_byte( bs, 0 );
    /* configOBUs */
    if ( param->configOBUs.sz && param->configOBUs.data )
        lsmash_bs_put_bytes( bs, param->configOBUs.sz, param->configOBUs.data );
    uint8_t *data = lsmash_bs_export_data( bs, data_length );
    lsmash_bs_cleanup( bs );
    /* Update box size. */
    LSMASH_SET_BE32( data, *data_length );
    return data;
}

int av1_copy_codec_specific
(
    lsmash_codec_specific_t *dst,
    lsmash_codec_specific_t *src
)
{
    assert( src && src->format == LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED && src->data.structured );
    assert( dst && dst->format == LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED && dst->data.structured );
    lsmash_av1_specific_parameters_t *src_data = (lsmash_av1_specific_parameters_t *)src->data.structured;
    lsmash_av1_specific_parameters_t *dst_data = (lsmash_av1_specific_parameters_t *)dst->data.structured;
    lsmash_free( dst_data->configOBUs.data );
    *dst_data = *src_data;
    dst_data->configOBUs.sz = src_data->configOBUs.sz;
    dst_data->configOBUs.data = lsmash_malloc( src_data->configOBUs.sz );
    if( !dst_data->configOBUs.data )
        return LSMASH_ERR_MEMORY_ALLOC;
    memcpy( dst_data->configOBUs.data, src_data->configOBUs.data, src_data->configOBUs.sz );
    return 0;
}

int av1_print_codec_specific( FILE *fp, lsmash_file_t *file, isom_box_t *box, int level )
{
    assert( fp && file && box && (box->manager & LSMASH_BINARY_CODED_BOX) );
    int indent = level;
    lsmash_ifprintf( fp, indent++, "[%s: AV1CodecConfigurationBox]\n", isom_4cc2str( box->type.fourcc ) );
    lsmash_ifprintf( fp, indent, "position = %"PRIu64"\n", box->pos );
    lsmash_ifprintf( fp, indent, "size = %"PRIu64"\n", box->size );
    if( box->size < AV1_SPECIFIC_BOX_MIN_LENGTH )
        return LSMASH_ERR_INVALID_DATA;
    uint8_t     *data   = box->binary;
    uint32_t     offset = isom_skip_box_common( &data );
    lsmash_bs_t *bs     = lsmash_bs_create();
    if( !bs )
        return LSMASH_ERR_MEMORY_ALLOC;
    int err = lsmash_bs_import_data( bs, data, box->size - offset );
    if( err < 0 )
        goto abort;
    uint8_t temp8 = lsmash_bs_get_byte( bs );
    lsmash_ifprintf( fp, indent, "marker = %"PRIu8"\n", (temp8 >> 7) );
    lsmash_ifprintf( fp, indent, "version = %"PRIu8"\n", (temp8 & 0x7F) );
    temp8 = lsmash_bs_get_byte( bs );
    lsmash_ifprintf( fp, indent, "seq_profile = %"PRIu8"\n", (temp8 >> 5) );
    lsmash_ifprintf( fp, indent, "seq_level_idx_0 = %"PRIu8"\n", (temp8 & 0x1F) );
    temp8 = lsmash_bs_get_byte( bs );
    lsmash_ifprintf( fp, indent, "seq_tier_0 = %"PRIu8"\n", (temp8 >> 7) );
    lsmash_ifprintf( fp, indent, "high_bitdepth = %"PRIu8"\n", (temp8 >> 6) & 0x01 );
    lsmash_ifprintf( fp, indent, "twelve_bit = %"PRIu8"\n", (temp8 >> 5) & 0x01 );
    lsmash_ifprintf( fp, indent, "monochrome = %"PRIu8"\n", (temp8 >> 4) & 0x01 );
    lsmash_ifprintf( fp, indent, "chroma_subsampling_x = %"PRIu8"\n", (temp8 >> 3) & 0x01 );
    lsmash_ifprintf( fp, indent, "chroma_subsampling_y = %"PRIu8"\n", (temp8 >> 2) & 0x01 );
    char *chroma_position;
    switch( temp8 & 0x03 )
    {
        case LSMASH_AV1_CSP_VERTICAL:
            chroma_position = "vertical";
            break;
        case LSMASH_AV1_CSP_COLOCATED:
            chroma_position = "colocated";
            break;
        case LSMASH_AV1_CSP_RESERVED:
            chroma_position = "reserved (invalid)";
            break;
        case LSMASH_AV1_CSP_UNKNOWN:
        default:
            chroma_position = "unknown";
            break;
    }
    lsmash_ifprintf( fp, indent, "chroma_sample_position = %s\n", chroma_position);
    temp8 = lsmash_bs_get_byte( bs );
    uint8_t initial_presentation_delay_present = (temp8 >> 4) & 0x01;
    if( initial_presentation_delay_present )
        lsmash_ifprintf( fp, indent, "initial_presentation_delay_minus_one = %"PRIu8"\n", (temp8 & 0x0F) );
    if( box->size - offset - 4 > 0 )
    {
        lsmash_ifprintf( fp, indent++, "configOBUs\n" );
        lsmash_ifprintf( fp, indent, "size = %"PRIu32"\n", (box->size - offset - 4) );
    }

    lsmash_bs_cleanup( bs );
    return 0;
abort:
    lsmash_bs_cleanup( bs );
    return err;
}
