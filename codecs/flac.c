/*****************************************************************************
 * flac.c
 *****************************************************************************
 * Copyright (C) 2026 L-SMASH project
 * 
 * Authors: XLWZ <xlwzforever@outlook.com>
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

#include "core/box.h"

#include <stdlib.h>
#include <string.h>

/***************************************************************************
    FLAC specific info (dfLa box) creation
***************************************************************************/

uint8_t *lsmash_create_flac_specific_info
(
    uint8_t *metadata_blocks,
    uint32_t metadata_blocks_size,
    uint32_t *data_length
)
{
    if ( !metadata_blocks || !metadata_blocks_size || !data_length )
        return NULL;
    /* dfLa is a FullBox: size(4) + type(4) + version(1) + flags(3) + payload */
    uint32_t box_size = 12 + metadata_blocks_size;
    uint8_t *data = lsmash_malloc( box_size );
    if ( !data )
        return NULL;
    LSMASH_SET_BE32( &data[0], box_size );
    LSMASH_SET_BE32( &data[4], ISOM_BOX_TYPE_DFLA.fourcc );
    /* version = 0, flags = 0 */
    data[8] = 0;
    data[9] = 0;
    data[10] = 0;
    data[11] = 0;
    memcpy( &data[12], metadata_blocks, metadata_blocks_size );
    *data_length = box_size;
    return data;
}

/***************************************************************************
    FLAC bitstream parser helpers
***************************************************************************/

/* Parse STREAMINFO from a block.
 * STREAMINFO is always 34 bytes.
 * Returns 0 on success, negative on error. */
int flac_parse_streaminfo
(
    uint8_t *streaminfo,
    uint32_t size,
    uint32_t *sample_rate,
    uint8_t *channels,
    uint8_t *bits_per_sample,
    uint32_t *min_block_size,
    uint32_t *max_block_size,
    uint64_t *total_samples
)
{
    if ( !streaminfo || size < 34 )
        return -1;
    lsmash_bs_t bs = { 0 };
    bs.buffer.data = streaminfo;
    bs.buffer.store = size;
    bs.buffer.alloc = size;
    *min_block_size = lsmash_bs_get_be16( &bs );
    *max_block_size = lsmash_bs_get_be16( &bs );
    lsmash_bs_skip_bytes( &bs, 3 ); /* min frame size */
    lsmash_bs_skip_bytes( &bs, 3 ); /* max frame size */
    uint8_t *buf24 = lsmash_bs_get_bytes( &bs, 3 );
    if ( !buf24 )
        return -1;
    *sample_rate = (buf24[0] << 12) | (buf24[1] << 4) | (buf24[2] >> 4);
    uint8_t next_byte = lsmash_bs_get_byte ( &bs );
    uint8_t ch_bps = ((buf24[2] & 0x0F) << 4) | (next_byte >> 4);
    *channels = (ch_bps >> 5) + 1;
    *bits_per_sample = (ch_bps & 0x1F) + 1;
    buf24 = lsmash_bs_get_bytes( &bs, 5 ); /* total_sample (36 bits) + first 4 bits of MD5 */
    if ( !buf24 )
        return -1;
    *total_samples = ((uint64_t)buf24[0] << 28) | ((uint64_t)buf24[1] << 20) 
                   | ((uint64_t)buf24[2] << 12) | ((uint64_t)buf24[3] << 4)
                   | (buf24[4] >> 4);
    return 0;
}

/* Check if the bytes at 'pos' contain a viald FLAC frame sysn code. 
 * Return -1 if not found, 0 if found with fixed block size, 1 if variable. */
int flac_check_sync
(
    uint8_t *data,
    uint32_t size,
    uint32_t offset
)
{
    if( (offset + 2 > size) || (data[offset] != 0xFF) || ((data[offset + 1] & 0xFE) != 0xF8) )
        return -1;
    /* blocking strategy: bit 0 of byte 2 */
    return data[offset + 1] & 1;
}

/* Parse a FLAC frame header to get the block size (in samples). 
 * frame_data points to the sync code (0xFF - 0xF8/0xF9). 
 * Returns the number of samples in this frame, or 0 on error */
uint32_t flac_parse_frame_header
(
    uint8_t *frame_data,
    uint32_t size
)
{
    if( !frame_data || size < 4)
        return 0;
    uint32_t bs_code = (frame_data[2] >> 4) & 0xF;
    uint32_t block_size;
    switch( bs_code )
    {
        case 0:
            /* reserved, should not happen */
            return 0;
            break;
        case 1:
            block_size = 192;
            break;
        case 2:
        case 3:
        case 4:
        case 5:
            block_size = 576 << (bs_code - 2);
            break;
        case 6:
            if( size < 5 )
                return 0;
            block_size = frame_data[4] + 1;
            break;
        case 7:
            if( size < 6 )
                return 0;
            block_size = ((frame_data[4] << 8) | frame_data[5]) + 1;
            break;
        default:
            block_size = 256 << (bs_code - 8);
            break;
    }

    if( block_size < 16 || block_size > 65535 )
        return 0;
    return block_size;
}