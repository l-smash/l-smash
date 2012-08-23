/*****************************************************************************
 * alac.c:
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

#include "internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "box.h"

#define ALAC_SPECIFIC_BOX_LENGTH 36

uint8_t *lsmash_create_alac_specific_info( lsmash_alac_specific_parameters_t *param, uint32_t *data_length )
{
    uint8_t buffer[ALAC_SPECIFIC_BOX_LENGTH];
    lsmash_bs_t bs = { 0 };
    bs.data  = buffer;
    bs.alloc = ALAC_SPECIFIC_BOX_LENGTH;
    lsmash_bs_put_be32( &bs, ALAC_SPECIFIC_BOX_LENGTH );    /* box size */
    lsmash_bs_put_be32( &bs, ISOM_BOX_TYPE_ALAC );          /* box type: 'alac' */
    lsmash_bs_put_be32( &bs, 0 );                           /* version + flags */
    lsmash_bs_put_be32( &bs, param->frameLength );
    lsmash_bs_put_byte( &bs, 0 );                           /* compatibleVersion */
    lsmash_bs_put_byte( &bs, param->bitDepth );
    lsmash_bs_put_byte( &bs, 40 );                          /* pb */
    lsmash_bs_put_byte( &bs, 14 );                          /* mb */
    lsmash_bs_put_byte( &bs, 10 );                          /* kb */
    lsmash_bs_put_byte( &bs, param->numChannels );
    lsmash_bs_put_be16( &bs, 255 );                         /* maxRun */
    lsmash_bs_put_be32( &bs, param->maxFrameBytes );
    lsmash_bs_put_be32( &bs, param->avgBitrate );
    lsmash_bs_put_be32( &bs, param->sampleRate );
    return lsmash_bs_export_data( &bs, data_length );
}

int alac_construct_specific_parameters( lsmash_codec_specific_t *dst, lsmash_codec_specific_t *src )
{
    assert( dst && dst->data.structured && src && src->data.unstructured );
    if( src->size < ALAC_SPECIFIC_BOX_LENGTH )
        return -1;
    lsmash_alac_specific_parameters_t *param = (lsmash_alac_specific_parameters_t *)dst->data.structured;
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
    data += 4;  /* Skip version and flags. */
    param->frameLength   = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    param->bitDepth      = data[5];
    param->numChannels   = data[9];
    param->maxFrameBytes = (data[12] << 24) | (data[13] << 16) | (data[14] << 8) | data[15];
    param->avgBitrate    = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
    param->sampleRate    = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
    return 0;
}

int alac_print_codec_specific( FILE *fp, lsmash_root_t *root, isom_box_t *box, int level )
{
    assert( fp && root && box );
    int indent = level;
    lsmash_ifprintf( fp, indent++, "[%s: ALAC Specific Box]\n", isom_4cc2str( box->type ) );
    lsmash_ifprintf( fp, indent, "position = %"PRIu64"\n", box->pos );
    lsmash_ifprintf( fp, indent, "size = %"PRIu64"\n", box->size );
    if( box->size < ALAC_SPECIFIC_BOX_LENGTH )
        return -1;
    isom_extension_box_t *ext = (isom_extension_box_t *)box;
    assert( ext->format == EXTENSION_FORMAT_BINARY );
    uint8_t *data = ext->form.binary;
    isom_skip_box_common( &data );
    lsmash_ifprintf( fp, indent, "version = %"PRIu8"\n", data[0] );
    lsmash_ifprintf( fp, indent, "flags = 0x%06"PRIx32"\n", (data[1] << 16) | (data[2] << 8) | data[3] );
    data += 4;
    lsmash_ifprintf( fp, indent, "frameLength = %"PRIu32"\n", (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3] );
    lsmash_ifprintf( fp, indent, "compatibleVersion = %"PRIu8"\n", data[4] );
    lsmash_ifprintf( fp, indent, "bitDepth = %"PRIu8"\n", data[5] );
    lsmash_ifprintf( fp, indent, "pb = %"PRIu8"\n", data[6] );
    lsmash_ifprintf( fp, indent, "mb = %"PRIu8"\n", data[7] );
    lsmash_ifprintf( fp, indent, "kb = %"PRIu8"\n", data[8] );
    lsmash_ifprintf( fp, indent, "numChannels = %"PRIu8"\n", data[9] );
    lsmash_ifprintf( fp, indent, "maxRun = %"PRIu16"\n", (data[10] << 8) | data[11] );
    lsmash_ifprintf( fp, indent, "maxFrameBytes = %"PRIu32"\n", (data[12] << 24) | (data[13] << 16) | (data[14] << 8) | data[15] );
    lsmash_ifprintf( fp, indent, "avgBitrate = %"PRIu32"\n", (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19] );
    lsmash_ifprintf( fp, indent, "sampleRate = %"PRIu32"\n", (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23] );
    return 0;
}

#undef ALAC_SPECIFIC_BOX_LENGTH
