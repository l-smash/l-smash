/*****************************************************************************
 * opus.c
 *****************************************************************************
 * Copyright (C) 2014 L-SMASH project
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

#include "common/internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "core/box.h"

#define OPUS_SPECIFIC_BOX_MIN_LENGTH 14

uint8_t *lsmash_create_opus_specific_info( lsmash_opus_specific_parameters_t *param, uint32_t *data_length )
{
    if( !param || !data_length )
        return NULL;
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
        return NULL;
    lsmash_bs_put_be32( bs, 0 );                            /* box size */
    lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_DOPS.fourcc );    /* box type: 'dOps' */
    lsmash_bs_put_byte( bs, param->Version );
    lsmash_bs_put_byte( bs, param->OutputChannelCount );
    lsmash_bs_put_be16( bs, param->PreSkip );
    lsmash_bs_put_be32( bs, param->InputSampleRate );
    lsmash_bs_put_be16( bs, param->OutputGain );
    lsmash_bs_put_byte( bs, param->ChannelMappingFamily );
    if( param->ChannelMappingFamily != 0 )
    {
        lsmash_bs_put_byte( bs, param->StreamCount );
        lsmash_bs_put_byte( bs, param->CoupledCount );
        lsmash_bs_put_bytes( bs, param->OutputChannelCount, param->ChannelMapping );
    }
    uint8_t *data = lsmash_bs_export_data( bs, data_length );
    lsmash_bs_cleanup( bs );
    /* Update box size. */
    LSMASH_SET_BE32( data, *data_length );
    return data;
}

int opus_construct_specific_parameters( lsmash_codec_specific_t *dst, lsmash_codec_specific_t *src )
{
    assert( dst && dst->data.structured && src && src->data.unstructured );
    if( src->size < OPUS_SPECIFIC_BOX_MIN_LENGTH )
        return LSMASH_ERR_INVALID_DATA;
    lsmash_opus_specific_parameters_t *param = (lsmash_opus_specific_parameters_t *)dst->data.structured;
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
    int err = lsmash_bs_import_data( bs, data, src->size - (data - src->data.unstructured) );
    if( err < 0 )
        goto fail;
    if( lsmash_bs_get_byte( bs ) != 0 )
    {
        /* We don't support version other than 0. */
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }
    param->Version              = 0;
    param->OutputChannelCount   = lsmash_bs_get_byte( bs );
    param->PreSkip              = lsmash_bs_get_be16( bs );
    param->InputSampleRate      = lsmash_bs_get_be32( bs );
    param->OutputGain           = lsmash_bs_get_be16( bs );
    param->ChannelMappingFamily = lsmash_bs_get_byte( bs );
    memset( param->ChannelMapping, 0, sizeof(param->ChannelMapping) );
    if( param->ChannelMappingFamily != 0 )
    {
        param->StreamCount  = lsmash_bs_get_byte( bs );
        param->CoupledCount = lsmash_bs_get_byte( bs );
        if( lsmash_bs_get_bytes_ex( bs, param->OutputChannelCount, param->ChannelMapping ) != param->OutputChannelCount )
        {
            err = LSMASH_ERR_NAMELESS;
            goto fail;
        }
    }
    else
    {
        if( param->OutputChannelCount > 2 )
        {
            err = LSMASH_ERR_INVALID_DATA;
            goto fail;
        }
        param->StreamCount       = 1;
        param->CoupledCount      = param->OutputChannelCount - 1;
        param->ChannelMapping[0] = 0;
        param->ChannelMapping[1] = param->CoupledCount;
    }
    lsmash_bs_cleanup( bs );
    return 0;
fail:
    lsmash_bs_cleanup( bs );
    return err;
}

int opus_print_codec_specific( FILE *fp, lsmash_file_t *file, isom_box_t *box, int level )
{
    assert( fp && file && box && (box->manager & LSMASH_BINARY_CODED_BOX) );
    int indent = level;
    lsmash_ifprintf( fp, indent++, "[%s: Opus Specific Box]\n", isom_4cc2str( box->type.fourcc ) );
    lsmash_ifprintf( fp, indent, "position = %"PRIu64"\n", box->pos );
    lsmash_ifprintf( fp, indent, "size = %"PRIu64"\n", box->size );
    if( box->size < OPUS_SPECIFIC_BOX_MIN_LENGTH )
        return LSMASH_ERR_INVALID_DATA;
    uint8_t     *data   = box->binary;
    uint32_t     offset = isom_skip_box_common( &data );
    lsmash_bs_t *bs     = lsmash_bs_create();
    if( !bs )
        return LSMASH_ERR_MEMORY_ALLOC;
    int err = lsmash_bs_import_data( bs, data, box->size - offset );
    if( err < 0 )
        goto abort;
    uint8_t Version = lsmash_bs_get_byte( bs );
    lsmash_ifprintf( fp, indent, "Version = %"PRIu8"\n", Version );
    if( Version != 0 )
        goto abort;
    uint8_t OutputChannelCount = lsmash_bs_get_byte( bs );
    lsmash_ifprintf( fp, indent, "OutputChannelCount = %"PRIu8"\n", OutputChannelCount );
    lsmash_ifprintf( fp, indent, "PreSkip = %"PRIu16"\n", lsmash_bs_get_be16( bs ) );
    lsmash_ifprintf( fp, indent, "InputSampleRate = %"PRIu32"\n", lsmash_bs_get_be32( bs ) );
    lsmash_ifprintf( fp, indent, "OutputGain = %"PRId16"\n", lsmash_bs_get_be16( bs ) );
    uint8_t ChannelMappingFamily = lsmash_bs_get_byte( bs );
    lsmash_ifprintf( fp, indent, "ChannelMappingFamily = %"PRIu8"\n", ChannelMappingFamily );
    if( ChannelMappingFamily != 0 )
    {
        lsmash_ifprintf( fp, indent, "StreamCount = %"PRIu8"\n", lsmash_bs_get_byte( bs ) );
        lsmash_ifprintf( fp, indent, "CoupledCount = %"PRIu8"\n", lsmash_bs_get_byte( bs ) );
        lsmash_ifprintf( fp, indent++, "ChannelMapping\n" );
        for( uint8_t i = 0; i < OutputChannelCount; i++ )
        {
            uint8_t channel = lsmash_bs_get_byte( bs );
            if( channel != 255 )
            {
                if( ChannelMappingFamily == 1 )
                {
                    static const char *channel_order[8][8] =
                    {
                        { "mono" },
                        { "left", "stereo" },
                        { "left", "center", "right" },
                        { "front left", "front right", "rear left", "rear right" },
                        { "front left", "fron center", "front right", "rear left", "rear right" },
                        { "front left", "fron center", "front right", "rear left", "rear right", "LFE" },
                        { "front left", "fron center", "front right", "side left", "side right", "rear center", "LFE" },
                        { "front left", "fron center", "front right", "side left", "side right", "rear left", "rear right", "LFE" }
                    };
                    lsmash_ifprintf( fp, indent, "%"PRIu8" -> %"PRIu8": %s\n", i, channel, channel_order[OutputChannelCount][i] );
                }
                else
                    lsmash_ifprintf( fp, indent, "%"PRIu8" -> %"PRIu8": unknown\n", i, channel );
            }
            else
                lsmash_ifprintf( fp, indent, "%"PRIu8": silence\n", i );
        }
    }
abort:
    lsmash_bs_cleanup( bs );
    return err;
}

#undef OPUS_SPECIFIC_BOX_MIN_LENGTH
