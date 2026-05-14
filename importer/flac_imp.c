/*****************************************************************************
 * flac_imp.c
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

#include <string.h>

#define LSMASH_IMPORTER_INTERNAL
#include "importer.h"

/***************************************************************************
    FLAC importer
    Encapsulation of FLAC in ISO Base Media File Format
    https://github.com/xiph/flac/blob/master/doc/isoflac.txt
***************************************************************************/

#define FLAC_MAX_FRAME_SIZE     65536
#define FLAC_SYNC_BYTE1         0xFF
#define FLAC_SYNC_MASK_BYTE2    0xFE
#define FLAC_SYNC_MIN_BYTE2     0xF8

typedef struct
{
    uint8_t    *metadata_blocks;
    uint32_t    metadata_blocks_size;
    uint32_t    sample_rate;
    uint8_t     channels;
    uint8_t     bits_per_sample;
    uint32_t    min_block_size;
    uint32_t    max_block_size;
    uint64_t    total_samples;

    uint64_t    next_dts;       /* cumulative DTS in samples, starts at 0 */
    uint32_t    last_block_size;
} flac_importer_t;

static void remove_flac_importer( flac_importer_t *imp )
{
    if( !imp )
        return;
    lsmash_free( imp->metadata_blocks );
    lsmash_free( imp );
}

static void flac_importer_cleanup( importer_t *imp )
{
    debug_if( imp && imp->info )
        remove_flac_importer( imp->info );
}

static flac_importer_t *create_flac_importer( void )
{
    return (flac_importer_t *)lsmash_malloc_zero( sizeof(flac_importer_t) );
}

/* FLAC CRC-8: polynomial x^8 + x^2 + x^1 + x^0 (0x07), LSB-first 
 * Used for frame header validation. */
static uint8_t flac_crc8( const uint8_t *data, uint32_t len )
{
    uint8_t crc = 0;
    for( uint32_t i = 0; i < len; i++ )
    {
        crc ^= data[i];
        for( int j = 0; j < 8; j++ )
            crc = (crc & 0x80) ? (uint8_t)(crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}

/* Scan for a FLAC frame sync code in the buffer starting at 'offset'.
 * Return the offset of the sync code, or -1 if not found. */
static int flac_find_sync( uint8_t *data, uint32_t size, uint32_t start_offset )
{
    for( uint32_t offset = start_offset; offset + 1 < size; offset++ )
    {
        if( data[offset] == FLAC_SYNC_BYTE1
         && (data[offset + 1] & FLAC_SYNC_MASK_BYTE2) == FLAC_SYNC_MIN_BYTE2 )
            return (int)offset;
    }
    return -1;
}

/* Get block size in sample from a FLAC frame header at 'frame_data'. 
 * 'frame_data' points to the sync code (2 bytes: 0xFF 0xF8/0xF9). */
static uint32_t flac_get_frame_block_size
(
    uint8_t *frame_data,
    uint32_t buf_size,
    uint32_t offset
)
{
    if( offset + 4 > buf_size )
        return 0;
    uint8_t byte2 = frame_data[offset + 2];
    uint32_t bs_code = (byte2 >> 4)  & 0xF;
    switch( bs_code )
    {
        case 0:
            /* reserved, should not happen */
            return 0;
        case 1:
            return 192;
        case 2:
        case 3:
        case 4:
        case 5:
            return 576 << (bs_code - 2);
        case 6:
            if( offset + 5 > buf_size )
                return 0;
            return frame_data[offset + 4] + 1;
        case 7:
            if( offset + 6 > buf_size )
                return 0;
            return (((uint32_t)frame_data[offset + 4] << 8) | frame_data[offset + 5]) + 1;
        default:
            return 256 << (bs_code - 8);
    }
}

/* Get the frame/sample number from a FLAC frame header. 
 * 'frame_date' + offset point to the sync code.
 * Returns the frame number (fixed block) or sample number (variable block) */
static uint64_t flac_get_frame_number
(
    uint8_t *frame_data,
    uint32_t buf_size,
    uint32_t offset
)
{
    /* First 4 bytes: sync(2) + block_size/sr(1) + ch/bps(1).
     * After that: UTF-8 encoded frame/sample number (1-7 bytes).*/
    uint32_t pos = offset + 4;
    uint64_t number = 0;

    if( pos >= buf_size )
        return UINT64_MAX;
    
    uint8_t first = frame_data[pos];
    int bytes;

    if( (first & 0x80) == 0 )
    {
        /* 1-byte: 0xxxxxxx */
        bytes = 1;
        number = first;
    }
    else if( (first & 0xE0) == 0xC0 )
    {
        /* 2-byte: 110xxxxx 10xxxxxx*/
        bytes = 2;
        number = first & 0x1F;
    }
    else if( (first & 0xF0) == 0xE0 )
    {
        /* 3-byte: 1110xxxx 10xxxxxx 10xxxxxx */
        bytes = 3;
        number = first & 0x0F;
    }
    else if( (first & 0xF8) == 0xF0 )
    {
        /* 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
        bytes = 4;
        number = first & 0x07;
    }
    else if( (first & 0xFC) == 0xF8 )
    {
        /* 5-byte: 111110xx 10xxxxxx ... */
        bytes = 5;
        number = first & 0x03;
    }
    else if( (first & 0xFE) == 0xFC )
    {
        /* 6-byte: 1111110x ... */
        bytes = 6;
        number = first & 0x01;
    }
    else if( (first & 0xFF) == 0xFE )
    {
        /* 7-byte: 11111110 ... */
        bytes = 7;
        number = 0;
    }
    else
    {
        /* Invalid UTF-8 */
        return UINT64_MAX;
    }

    if( pos + bytes > buf_size )
        return UINT64_MAX;
    
    for( int i = 1; i < bytes; i++ )
    {
        uint8_t b = frame_data[pos + i];
        if( (b & 0xC0) != 0x80 )
            return UINT64_MAX; /* Invalid UTF-8 continuation byte */
        number = (number << 6) | (b & 0x3F);
    }

    return number;
}

static int flac_validate_sync_candidate
(
    uint8_t *data,
    uint32_t size,
    uint32_t candidate_offset
)
{
    if( candidate_offset + 4 > size )
        return 0;
    uint8_t nbyte2 = data[candidate_offset + 2];
    uint8_t nbyte3 = data[candidate_offset + 3];

    uint32_t n_bs_code = (nbyte2 >> 4) & 0xF;
    uint8_t  n_bps_idx = (nbyte3 >> 1) & 0x7;

    if( n_bs_code == 0 || n_bps_idx == 7 )
        return 0;
    
    /* Validate CRC-8 of the frame header. 
     * CRC covers: sync byte 2 (byte[1]) + header bytes + frame/sample number. 
     * The CRC-8 byte follow immediately after the coded number. */
    uint32_t pos = candidate_offset + 4;
    uint64_t number = 0;
    int num_bytes;

    if( pos >= size )
        return 0;
    
    uint8_t first = data[pos];
    if( (first & 0x80) == 0 )
    {
        /* 1-byte: 0xxxxxxx */
        num_bytes = 1;
        number = first;
    }
    else if( (first & 0xE0) == 0xC0 )
    {
        /* 2-byte: 110xxxxx 10xxxxxx*/
        num_bytes = 2;
        number = first & 0x1F;
    }
    else if( (first & 0xF0) == 0xE0 )
    {
        /* 3-byte: 1110xxxx 10xxxxxx 10xxxxxx */
        num_bytes = 3;
        number = first & 0x0F;
    }
    else if( (first & 0xF8) == 0xF0 )
    {
        /* 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
        num_bytes = 4;
        number = first & 0x07;
    }
    else if( (first & 0xFC) == 0xF8 )
    {
        /* 5-byte: 111110xx 10xxxxxx ... */
        num_bytes = 5;
        number = first & 0x03;
    }
    else if( (first & 0xFE) == 0xFC )
    {
        /* 6-byte: 1111110x ... */
        num_bytes = 6;
        number = first & 0x01;
    }
    else if( (first & 0xFF) == 0xFE )
    {
        /* 7-byte: 11111110 ... */
        num_bytes = 7;
        number = 0;
    }
    else
    {
        /* Invalid UTF-8 */
        return 0;
    }

    if( pos + num_bytes + 1 > size )
        return 0;
    
    for( int i = 1; i < num_bytes; i++ )
    {
        if( (data[pos + i] & 0xC0) != 0x80 )
            return 0; /* Invalid UTF-8 continuation byte */
        number = (number << 6) | (data[pos + i] & 0x3F);
    }

    /* CRC-8 covers all frame header bytes from candidate_ offset
     * (including sync byte) through the last byte of the coded number */
    uint8_t crc_computed = flac_crc8( &data[candidate_offset],
                                      (pos + num_bytes) - candidate_offset );
    if( crc_computed != data[pos + num_bytes] )
        return 0; /* CRC mismatch */
    
    return 1; /* Valid sync candidate */
}

/* Probe function: check if the stream is a valid FLAC stream. */
static int flac_importer_probe( importer_t *imp )
{
    flac_importer_t *flac_imp = create_flac_importer();
    if( !flac_imp )
        return LSMASH_ERR_MEMORY_ALLOC;
    
    lsmash_bs_t *bs = imp->bs;
    int err;

    /* Read and check fLaC magic */
    if( lsmash_bs_show_byte( bs, 0 ) != 'f' 
     || lsmash_bs_show_byte( bs, 1 ) != 'L'
     || lsmash_bs_show_byte( bs, 2 ) != 'a'
     || lsmash_bs_show_byte( bs, 3 ) != 'C' )
    {
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }
    lsmash_bs_skip_bytes( bs, 4 );

    /* Read all metadata blocks after the fLaC marker.
     * We compute the exact byte offset where audio frames start:
     * audio_start = 4 (fLaC) + sum_of(4 + data_length) for each metadata block */
    uint64_t metadata_total = 0;
    uint32_t meta_alloc = 4096;
    uint8_t *meta_data = lsmash_malloc( meta_alloc );
    if( !meta_data )
    {
        err = LSMASH_ERR_MEMORY_ALLOC;
        goto fail;
    }
    uint32_t meta_size = 0;
    int found_streaminfo = 0;

    while ( 1 )
    {
        while ( lsmash_bs_get_remaining_buffer_size( bs ) < 4 )
        {
            if ( bs->eof )
                break;
            int ret = lsmash_bs_read( bs, meta_alloc);
            if ( ret < 0 )
            {
                lsmash_free( meta_data );
                err = LSMASH_ERR_INVALID_DATA;
                goto fail;
            }
        }
        if ( bs->eof && lsmash_bs_get_remaining_buffer_size( bs ) < 4 )
            break;
        
        uint8_t header[4];
        if( lsmash_bs_get_bytes_ex( bs, 4, header ) < 0 )
        {
            lsmash_free( meta_data );
            err = LSMASH_ERR_INVALID_DATA;
            goto fail;
        }
        uint8_t last = (header[0] >> 7) & 1;
        uint8_t type = header[0] & 0x7F;
        uint32_t len = ((uint32_t)header[1] << 16) | ((uint32_t)header[2] << 8) | header[3];

        while ( lsmash_bs_get_remaining_buffer_size( bs ) < len)
        {
            if( bs->eof )
            {
                lsmash_free( meta_data );
                err = LSMASH_ERR_INVALID_DATA;
                goto fail;
            }
            int ret = lsmash_bs_read( bs, meta_alloc );
            if( ret < 0 )
            {
                lsmash_free( meta_data );
                err = LSMASH_ERR_INVALID_DATA;
                goto fail;
            }
        }
        
        if( meta_size + 4 + len > meta_alloc )
        {
            meta_alloc = meta_size + 4 + len + 4096;
            uint8_t *new_meta = lsmash_realloc( meta_data, meta_alloc );
            if( !new_meta )
            {
                lsmash_free( meta_data );
                err = LSMASH_ERR_MEMORY_ALLOC;
                goto fail;
            }
            meta_data = new_meta;
        }

        meta_data[meta_size + 0] = header[0];
        meta_data[meta_size + 1] = header[1];
        meta_data[meta_size + 2] = header[2];
        meta_data[meta_size + 3] = header[3];
        meta_size += 4;
        if( lsmash_bs_get_bytes_ex( bs, len, &meta_data[meta_size] ) < 0 )
        {
            lsmash_free( meta_data );
            err = LSMASH_ERR_INVALID_DATA;
            goto fail;
        }
        meta_size += len;
        metadata_total += 4 + len;

        if( type == 0 && ! found_streaminfo && len >= 34 )
        {
            uint8_t *si = &meta_data[meta_size - len];
            lsmash_bs_t sibs = { 0 };
            sibs.buffer.data = si;
            sibs.buffer.store = len;
            sibs.buffer.alloc = len;
            flac_imp->min_block_size = lsmash_bs_get_be16( &sibs );
            flac_imp->max_block_size = lsmash_bs_get_be16( &sibs );
            lsmash_bs_skip_bytes( &sibs, 3 );
            lsmash_bs_skip_bytes( &sibs, 3 );
            uint8_t *buf24 = lsmash_bs_get_bytes( &sibs, 3 );
            flac_imp->sample_rate = (buf24[0] << 12) | (buf24[1] << 4) | (buf24[2] >> 4);
            /* channels (3 bits) and bps (5 bits) span the lower 4 bits
             * of the last sample_rate byte and the upper 4 bits of the next byte */
            uint8_t next_byte = lsmash_bs_get_byte( &sibs );
            uint8_t ch_bps = ((buf24[2] & 0x0F) << 4) | (next_byte >> 4);
            flac_imp->channels = (ch_bps >> 5) + 1;
            flac_imp->bits_per_sample = (ch_bps & 0x1F) + 1;
            buf24 = lsmash_bs_get_bytes( &sibs, 5);
            flac_imp->total_samples = ((uint64_t)buf24[0] << 28) | ((uint64_t)buf24[1] << 20) 
                                    | ((uint64_t)buf24[2] << 12) | ((uint64_t)buf24[3] << 4)
                                    | (buf24[4] >> 4);
            found_streaminfo = 1;
        }

        if( last )
            break;
    }

    if( !found_streaminfo )
    {
        lsmash_free( meta_data );
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }

    flac_imp->metadata_blocks       = meta_data;
    flac_imp->metadata_blocks_size  = meta_size;
    
    /* Seek to the start of audio data (after fLaC + all metadata blocks). 
     * This ensures the buffer is positioned correctly for frame reading. */
    lsmash_bs_read_seek( bs, (int64_t)(4 + metadata_total), SEEK_SET );

    /* Create summary with codec specific data */
    lsmash_audio_summary_t *summary = 
        (lsmash_audio_summary_t *)lsmash_create_summary
        ( LSMASH_SUMMARY_TYPE_AUDIO );
    if( !summary )
    {
        err = LSMASH_ERR_NAMELESS;
        goto fail;
    }
    lsmash_codec_specific_t *cs = lsmash_create_codec_specific_data(
        LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_FLAC,
        LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
    if( !cs )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *) summary );
        err = LSMASH_ERR_MEMORY_ALLOC;
        goto fail;
    }
    cs->data.unstructured = lsmash_create_flac_specific_info(
         meta_data, 
         meta_size, 
         &cs->size );
    if( !cs->data.unstructured 
     || lsmash_list_add_entry( &summary->opaque->list, cs ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *) summary );
        lsmash_destroy_codec_specific_data( cs );
        err = LSMASH_ERR_NAMELESS;
        goto fail;
    }

    summary->sample_type        = ISOM_CODEC_TYPE_FLAC_AUDIO;
    summary->max_au_length      = FLAC_MAX_FRAME_SIZE;
    /* FLAC doesn't use MP4A AOT, so we set it to NULL. */
    summary->aot                = MP4A_AUDIO_OBJECT_TYPE_NULL; 
    summary->frequency          = flac_imp->sample_rate;
    summary->channels           = flac_imp->channels;
    summary->sample_size        = flac_imp->bits_per_sample;
    /* Use max block size as frame size hint. */
    summary->samples_in_frame   = flac_imp->max_block_size; 
    summary->sbr_mode           = MP4A_AAC_SBR_NOT_SPECIFIED;

    if( lsmash_list_add_entry( imp->summaries, summary ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *) summary );
        err = LSMASH_ERR_MEMORY_ALLOC;
        goto fail;
    }

    imp->info = flac_imp;
    imp->status = IMPORTER_OK;
    return 0;

fail:
    remove_flac_importer( flac_imp );
    return err;
}

static int flac_importer_get_accessunit
(
    importer_t *imp,
    uint32_t track_number,
    lsmash_sample_t **p_sample
)
{
    if( !imp->info )
        return LSMASH_ERR_NAMELESS;
    if( track_number != 1 )
        return LSMASH_ERR_FUNCTION_PARAM;
    
    lsmash_audio_summary_t *summary = 
        (lsmash_audio_summary_t *)lsmash_list_get_entry_data
        ( imp->summaries, track_number );
    if( !summary )
        return LSMASH_ERR_NAMELESS;
    
    flac_importer_t *flac_imp = (flac_importer_t *)imp->info;
    importer_status current_status = imp->status;
    if( current_status == IMPORTER_ERROR )
        return LSMASH_ERR_NAMELESS;
    if( current_status == IMPORTER_EOF )
        return IMPORTER_EOF;
    
    lsmash_bs_t *bs = imp->bs;
    uint64_t remain;
    uint8_t *buf;

    remain = lsmash_bs_get_remaining_buffer_size( bs );

    if( remain < FLAC_MAX_FRAME_SIZE && !bs->eof )
    {
        int err = lsmash_bs_read( bs, FLAC_MAX_FRAME_SIZE );
        if( err < 0 )
        {
            imp->status = IMPORTER_ERROR;
            return LSMASH_ERR_NAMELESS;
        }
        remain = lsmash_bs_get_remaining_buffer_size( bs );
    }

    /* Check for EOF */
    if( (bs->eof || bs->eob) && remain < 4)
    {
        imp->status = IMPORTER_EOF;
        return IMPORTER_EOF;
    }

    buf = lsmash_bs_get_buffer_data( bs );

    /* Find first valid sync code */
    int sync_offset;
    uint32_t block_size = 0;
    uint32_t search_from = 0;

    while( 1 )
    {
        sync_offset = flac_find_sync
            ( buf, (uint32_t)remain, search_from );
        if( sync_offset < 0 )
        {
            if( bs->eof | bs->eob )
            {
                imp->status = IMPORTER_EOF;
                return IMPORTER_EOF;
            }
            imp->status = IMPORTER_ERROR;
            return LSMASH_ERR_NAMELESS;
        }

        block_size = flac_get_frame_block_size
            ( buf, (uint32_t)remain, (uint32_t)sync_offset );
        if( block_size >= flac_imp->min_block_size
         && block_size <= flac_imp->max_block_size 
         && flac_validate_sync_candidate
            ( buf, (uint32_t)remain, (uint32_t)sync_offset ) )
        {
            /* Found a valid FLAC frame */
            break;
        }
        
        /* Invalid block size - skip this false positive */
        search_from = (uint32_t)(sync_offset + 2);
        if( search_from + 4 > (uint32_t)remain )
        {
            imp->status = IMPORTER_ERROR;
            return LSMASH_ERR_NAMELESS;
        }
    }

    /* Get frame number for validating the next-frame boundary */
    uint64_t frame_num = flac_get_frame_number
        ( buf, (uint32_t)remain, (uint32_t)sync_offset );

    /* Find next frame boundary by searching for the next valid sync word */
    uint32_t frame_end = (uint32_t)remain;
    search_from = (uint32_t)sync_offset + 4;

    while( 1 )
    {
        int next_sync = flac_find_sync
            ( buf, (uint32_t)remain, search_from );
        if( next_sync > sync_offset )
        {
            if( flac_validate_sync_candidate
                ( buf, (uint32_t)remain, (uint32_t)next_sync ) )
            {
                /* Further validate using frame number. 
                 * For fixed block size, frame number increments by 1.
                 * For variable block, sample number increaments by block_size. */
                uint64_t next_frame_num = flac_get_frame_number
                    ( buf, (uint32_t)remain, (uint32_t)next_sync );
                if( next_frame_num != UINT64_MAX )
                {
                    int blocking_strategy = buf[sync_offset + 1] & 1;
                    if( blocking_strategy == 0 )
                    {
                        /* Fixed block: frame number should be current + 1 */
                        if( next_frame_num == frame_num + 1 )
                        {
                            frame_end = (uint32_t)next_sync;
                            break;
                        }
                    }
                    else
                    {
                        /* Variable block: sample number should be current + block_size */
                        if( next_frame_num == frame_num + block_size )
                        {
                            frame_end = (uint32_t)next_sync;
                            break;
                        }
                    }
                }
            }
            search_from = (uint32_t)(next_sync + 2);
            continue;
        }

        /* No next sync found - need more data or EOF */
        if( bs->eof )
        {
            frame_end = (uint32_t)remain;
            break;
        }
        /* Read more data */
        if( bs->buffer.pos > FLAC_MAX_FRAME_SIZE * 1000)
            lsmash_bs_dispose_past_data( bs );
        int rerr = lsmash_bs_read( bs, FLAC_MAX_FRAME_SIZE );
        if (rerr < 0 )
        {
            imp->status = IMPORTER_ERROR;
            return LSMASH_ERR_NAMELESS;
        }
        remain = lsmash_bs_get_remaining_buffer_size( bs );
        if( remain < 4 )
        {
            frame_end = (uint32_t)remain;
            break;
        }
        buf = lsmash_bs_get_buffer_data( bs );
        search_from = (uint32_t)sync_offset + 4;
    }

    uint32_t frame_size = frame_end - (uint32_t)sync_offset;
    if( frame_size < 4 || frame_size > FLAC_MAX_FRAME_SIZE )
    {
        imp->status = IMPORTER_ERROR;
        return LSMASH_ERR_NAMELESS;
    }

    lsmash_sample_t *sample = lsmash_create_sample( frame_size );
    if( !sample )
    {
        imp->status = IMPORTER_ERROR;
        return LSMASH_ERR_MEMORY_ALLOC;
    }
    *p_sample = sample;
    memcpy( sample->data, &buf[sync_offset], frame_size );
    sample->length          = frame_size;
    sample->dts             = flac_imp->next_dts;
    sample->cts             = sample->dts;
    sample->prop.ra_flags   = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;

    flac_imp->last_block_size   = block_size;
    flac_imp->next_dts         += block_size;
    summary->samples_in_frame   = block_size;

    /* Advance the buffer past this frame so the next call starts after it. */
    lsmash_bs_skip_bytes( bs, (uint32_t)sync_offset + frame_size );

    remain = lsmash_bs_get_remaining_buffer_size( bs );
    if( (bs->eof || bs->eob) && remain < 4 )
        imp->status = IMPORTER_EOF;
    else
        imp->status = IMPORTER_OK;
    
    return current_status;
}

static uint32_t flac_importer_get_last_delta
(
    importer_t *imp,
    uint32_t track_number
)
{
    debug_if( !imp || !imp->info )
        return 0;
    flac_importer_t *flac_imp = (flac_importer_t *)imp->info;
    if( !flac_imp || track_number != 1 || imp->status != IMPORTER_EOF )
        return 0;
    return flac_imp->last_block_size;
}

const importer_functions flac_importer =
{
    { "FLAC" },
    1, /* detectable */
    flac_importer_probe,
    flac_importer_get_accessunit,
    flac_importer_get_last_delta,
    flac_importer_cleanup,
};