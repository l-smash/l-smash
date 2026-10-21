/*****************************************************************************
 * flac.c
 *****************************************************************************
 * Copyright (C) 2026 L-SMASH project
 * Copyright (c) 2026 Yusuke Nakamura
 *
 * Authors: XLWZ <xlwzforever@outlook.com>
 *          Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
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
#include <inttypes.h>

#define FLAC_DFLA_HEADER_LENGTH       12
#define FLAC_METADATA_HEADER_LENGTH   4
#define FLAC_STREAMINFO_LENGTH        34
#define FLAC_DFLA_MIN_LENGTH          (FLAC_DFLA_HEADER_LENGTH + FLAC_METADATA_HEADER_LENGTH + FLAC_STREAMINFO_LENGTH)

/***************************************************************************
    FLAC specific info (dfLa box) creation
***************************************************************************/

uint8_t *lsmash_create_flac_specific_info
(
    uint8_t  *metadata_blocks,
    uint32_t  metadata_blocks_size,
    uint32_t *data_length
)
{
    if( !metadata_blocks || !data_length
     || metadata_blocks_size < FLAC_METADATA_HEADER_LENGTH + FLAC_STREAMINFO_LENGTH
     || metadata_blocks_size > UINT32_MAX - FLAC_DFLA_HEADER_LENGTH )
        return NULL;
    /* Per the FLAC-in-ISOBMFF specification, the first metadata block in dfLa
     * SHALL be the STREAMINFO block (BlockType == 0, Length == 34). */
    uint8_t  first_type = metadata_blocks[0] & 0x7F;
    uint32_t first_len  = ((uint32_t)metadata_blocks[1] << 16)
                        | ((uint32_t)metadata_blocks[2] << 8)
                        |  (uint32_t)metadata_blocks[3];
    if( first_type != 0 || first_len != FLAC_STREAMINFO_LENGTH )
        return NULL;
    /* dfLa is a FullBox: size(4) + type(4) + version(1) + flags(3) + payload */
    uint32_t box_size = FLAC_DFLA_HEADER_LENGTH + metadata_blocks_size;
    uint8_t *data = lsmash_malloc( box_size );
    if( !data )
        return NULL;
    LSMASH_SET_BE32( &data[0], box_size );
    LSMASH_SET_BE32( &data[4], ISOM_BOX_TYPE_DFLA.fourcc );
    /* version = 0, flags = 0 */
    data[8]  = 0;
    data[9]  = 0;
    data[10] = 0;
    data[11] = 0;
    memcpy( &data[FLAC_DFLA_HEADER_LENGTH], metadata_blocks, metadata_blocks_size );
    *data_length = box_size;
    return data;
}

/***************************************************************************
    FLAC bitstream parser helpers
***************************************************************************/

/* FLAC CRC-8 lookup table: polynomial x^8 + x^2 + x^1 + x^0 (0x07). */
static const uint8_t flac_crc8_table[256] =
{
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};

uint8_t lsm_flac_crc8( const uint8_t *data, uint32_t len )
{
    uint8_t crc = 0;
    for( uint32_t i = 0; i < len; i++ )
        crc = flac_crc8_table[crc ^ data[i]];
    return crc;
}

static int flac_decode_utf8
(
    const uint8_t *data,
    uint32_t       size,
    uint64_t      *number
)
{
    if( !data || size == 0 )
        return 0;
    uint8_t  first = data[0];
    uint64_t val   = 0;
    int      bytes = 0;
    if( (first & 0x80) == 0x00 )
    {
        bytes = 1;
        val   = first;
    }
    else if( (first & 0xE0) == 0xC0 )
    {
        bytes = 2;
        val   = first & 0x1F;
    }
    else if( (first & 0xF0) == 0xE0 )
    {
        bytes = 3;
        val   = first & 0x0F;
    }
    else if( (first & 0xF8) == 0xF0 )
    {
        bytes = 4;
        val   = first & 0x07;
    }
    else if( (first & 0xFC) == 0xF8 )
    {
        bytes = 5;
        val   = first & 0x03;
    }
    else if( (first & 0xFE) == 0xFC )
    {
        bytes = 6;
        val   = first & 0x01;
    }
    else if( (first & 0xFF) == 0xFE )
    {
        bytes = 7;
        val   = 0;
    }
    else
        return 0;
    if( (uint32_t)bytes > size )
        return 0;
    for( int i = 1; i < bytes; i++ )
    {
        if( (data[i] & 0xC0) != 0x80 )
            return 0;
        val = (val << 6) | (data[i] & 0x3F);
    }
    if( number )
        *number = val;
    return bytes;
}

/* Parse STREAMINFO from a 34-byte metadata block payload.
 * Returns 0 on success, negative on error. */
int lsm_flac_parse_streaminfo
(
    const uint8_t *streaminfo,
    uint32_t       size,
    uint32_t      *sample_rate,
    uint8_t       *channels,
    uint8_t       *bits_per_sample,
    uint32_t      *min_block_size,
    uint32_t      *max_block_size,
    uint64_t      *total_samples
)
{
    if( !streaminfo || size < FLAC_STREAMINFO_LENGTH )
        return LSMASH_ERR_FUNCTION_PARAM;
    uint32_t min_bs = ((uint32_t)streaminfo[0] << 8) | streaminfo[1];
    uint32_t max_bs = ((uint32_t)streaminfo[2] << 8) | streaminfo[3];
    uint32_t sr     = ((uint32_t)streaminfo[10] << 12)
                    | ((uint32_t)streaminfo[11] << 4)
                    | ((uint32_t)streaminfo[12] >> 4);
    uint8_t  ch     = ((streaminfo[12] >> 1) & 0x07) + 1;
    uint8_t  bps    = (((streaminfo[12] & 0x01) << 4) | (streaminfo[13] >> 4)) + 1;
    uint64_t total  = ((uint64_t)(streaminfo[13] & 0x0F) << 32)
                    | ((uint64_t)streaminfo[14] << 24)
                    | ((uint64_t)streaminfo[15] << 16)
                    | ((uint64_t)streaminfo[16] << 8)
                    |  (uint64_t)streaminfo[17];
    if( min_bs < 16 || max_bs < min_bs || sr == 0 || sr > 655350 || bps < 4 || bps > 32 )
        return LSMASH_ERR_INVALID_DATA;
    if( min_block_size )  *min_block_size  = min_bs;
    if( max_block_size )  *max_block_size  = max_bs;
    if( sample_rate )     *sample_rate     = sr;
    if( channels )        *channels        = ch;
    if( bits_per_sample ) *bits_per_sample = bps;
    if( total_samples )   *total_samples   = total;
    return 0;
}

/* Check if the bytes at 'offset' contain a valid FLAC frame sync code.
 * Return -1 if not found, 0 if found with fixed block size, 1 if variable. */
int lsm_flac_check_sync
(
    const uint8_t *data,
    uint32_t       size,
    uint32_t       offset
)
{
    if( !data || offset + 2 > size || data[offset] != 0xFF || (data[offset + 1] & 0xFE) != 0xF8 )
        return -1;
    /* blocking strategy: bit 0 of byte 1 */
    return data[offset + 1] & 1;
}

/* Parse and CRC-8-validate a FLAC frame header per RFC 9639 Section 9.1.
 * frame_data points to the sync code (0xFF 0xF8/0xF9).
 * Returns 0 on success, negative on invalid header or CRC-8 mismatch. */
int lsm_flac_parse_frame_header
(
    const uint8_t *frame_data,
    uint32_t       size,
    uint32_t      *block_size,
    uint64_t      *coded_number,
    int           *blocking_strategy
)
{
    int strategy = lsm_flac_check_sync( frame_data, size, 0 );
    if( strategy < 0 || size < 6 )
        return LSMASH_ERR_INVALID_DATA;
    uint32_t bs_code   = (frame_data[2] >> 4) & 0x0F;
    uint32_t sr_code   =  frame_data[2]       & 0x0F;
    uint32_t ch_assign = (frame_data[3] >> 4) & 0x0F;
    uint32_t bps_code  = (frame_data[3] >> 1) & 0x07;
    uint32_t reserved  =  frame_data[3]       & 0x01;
    if( bs_code == 0 || sr_code == 0x0F || ch_assign >= 11 || bps_code == 3 || bps_code == 7 || reserved != 0 )
        return LSMASH_ERR_INVALID_DATA;
    uint64_t num = 0;
    int utf8_len = flac_decode_utf8( &frame_data[4], size - 4, &num );
    if( utf8_len <= 0 )
        return LSMASH_ERR_INVALID_DATA;
    uint32_t pos = 4 + (uint32_t)utf8_len;
    uint32_t bs  = 0;
    switch( bs_code )
    {
        case 1 :
            bs = 192;
            break;
        case 2 :
        case 3 :
        case 4 :
        case 5 :
            bs = 576U << (bs_code - 2);
            break;
        case 6 :
            if( pos + 1 >= size )
                return LSMASH_ERR_INVALID_DATA;
            bs = (uint32_t)frame_data[pos++] + 1;
            break;
        case 7 :
            if( pos + 2 >= size )
                return LSMASH_ERR_INVALID_DATA;
            bs = (((uint32_t)frame_data[pos] << 8) | frame_data[pos + 1]) + 1;
            pos += 2;
            break;
        default :
            bs = 256U << (bs_code - 8);
            break;
    }
    if( bs < 16 || bs > 65535 )
        return LSMASH_ERR_INVALID_DATA;
    if( sr_code == 12 )
        pos += 1;
    else if( sr_code == 13 || sr_code == 14 )
        pos += 2;
    if( pos >= size )
        return LSMASH_ERR_INVALID_DATA;
    if( lsm_flac_crc8( frame_data, pos ) != frame_data[pos] )
        return LSMASH_ERR_INVALID_DATA;
    if( block_size )        *block_size        = bs;
    if( coded_number )      *coded_number      = num;
    if( blocking_strategy ) *blocking_strategy = strategy;
    return 0;
}

int lsm_flac_print_codec_specific( FILE *fp, lsmash_file_t *file, isom_box_t *box, int level )
{
    assert( fp && file && box && (box->manager & LSMASH_BINARY_CODED_BOX) );
    int indent = level;
    lsmash_ifprintf( fp, indent++, "[%s: FLAC Specific Box]\n", lsm_box_4cc2str( box->type.fourcc ) );
    lsmash_ifprintf( fp, indent, "position = %"PRIu64"\n", box->pos );
    lsmash_ifprintf( fp, indent, "size = %"PRIu64"\n", box->size );
    if( box->size < FLAC_DFLA_MIN_LENGTH )
        return LSMASH_ERR_INVALID_DATA;
    uint8_t *data   = box->binary;
    uint32_t offset = lsm_box_skip_common( &data );
    if( box->size - offset < 4 + FLAC_METADATA_HEADER_LENGTH + FLAC_STREAMINFO_LENGTH )
        return LSMASH_ERR_INVALID_DATA;
    uint8_t  version = LSMASH_GET_BYTE( &data[0] );
    uint32_t flags   = LSMASH_GET_BE24( &data[1] );
    lsmash_ifprintf( fp, indent, "version = %"PRIu8"\n", version );
    lsmash_ifprintf( fp, indent, "flags = 0x%06"PRIx32"\n", flags );
    if( version != 0 )
        return 0;
    uint64_t remain = (box->size - offset) - 4;
    const uint8_t *ptr = data + 4;
    static const char * const block_type_name_table[] =
    {
        "STREAMINFO",
        "PADDING",
        "APPLICATION",
        "SEEKTABLE",
        "VORBIS_COMMENT",
        "CUESHEET",
        "PICTURE"
    };
    uint32_t block_idx = 0;
    while( remain >= FLAC_METADATA_HEADER_LENGTH )
    {
        uint8_t  last = (ptr[0] >> 7) & 0x01;
        uint8_t  type =  ptr[0]       & 0x7F;
        uint32_t len  = ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | ptr[3];
        ptr    += FLAC_METADATA_HEADER_LENGTH;
        remain -= FLAC_METADATA_HEADER_LENGTH;
        if( remain < len )
            return LSMASH_ERR_INVALID_DATA;
        const char *type_name = (type < 7) ? block_type_name_table[type] : (type == 127 ? "invalid" : "reserved");
        lsmash_ifprintf( fp, indent, "FLACMetadataBlock[%"PRIu32"]: type = %"PRIu8" (%s), last = %"PRIu8", length = %"PRIu32"\n",
                         block_idx++, type, type_name, last, len );
        if( type == 0 && len >= FLAC_STREAMINFO_LENGTH )
        {
            uint32_t sample_rate     = 0;
            uint8_t  channels        = 0;
            uint8_t  bits_per_sample = 0;
            uint32_t min_block_size  = 0;
            uint32_t max_block_size  = 0;
            uint64_t total_samples   = 0;
            if( lsm_flac_parse_streaminfo( ptr, len, &sample_rate, &channels, &bits_per_sample,
                                           &min_block_size, &max_block_size, &total_samples ) == 0 )
            {
                uint32_t min_frame_size = ((uint32_t)ptr[4] << 16) | ((uint32_t)ptr[5] << 8) | ptr[6];
                uint32_t max_frame_size = ((uint32_t)ptr[7] << 16) | ((uint32_t)ptr[8] << 8) | ptr[9];
                lsmash_ifprintf( fp, indent + 1, "MinimumBlockSize = %"PRIu32"\n", min_block_size );
                lsmash_ifprintf( fp, indent + 1, "MaximumBlockSize = %"PRIu32"\n", max_block_size );
                lsmash_ifprintf( fp, indent + 1, "MinimumFrameSize = %"PRIu32"\n", min_frame_size );
                lsmash_ifprintf( fp, indent + 1, "MaximumFrameSize = %"PRIu32"\n", max_frame_size );
                lsmash_ifprintf( fp, indent + 1, "SampleRate = %"PRIu32"\n", sample_rate );
                lsmash_ifprintf( fp, indent + 1, "NumberOfChannelsMinusOne = %"PRIu8"\n", (uint8_t)(channels - 1) );
                lsmash_ifprintf( fp, indent + 1, "BitsPerSampleMinusOne = %"PRIu8"\n", (uint8_t)(bits_per_sample - 1) );
                lsmash_ifprintf( fp, indent + 1, "TotalInterchannelSamples = %"PRIu64"\n", total_samples );
            }
        }
        ptr    += len;
        remain -= len;
        if( last )
            break;
    }
    return 0;
}
