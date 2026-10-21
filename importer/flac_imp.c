/*****************************************************************************
 * flac_imp.c
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

#include <string.h>

#define LSMASH_IMPORTER_INTERNAL
#include "importer.h"

/***************************************************************************
    FLAC importer
    Encapsulation of FLAC in ISO Base Media File Format
    https://github.com/xiph/flac/blob/master/doc/isoflac.txt
***************************************************************************/

#define FLAC_MAX_FRAME_SIZE         65536
#define FLAC_MAX_METADATA_BLOCKS    128
#define FLAC_MAX_METADATA_TOTAL     (16 * 1024 * 1024)
#define FLAC_SYNC_BYTE1             0xFF
#define FLAC_SYNC_MASK_BYTE2        0xFE
#define FLAC_SYNC_MIN_BYTE2         0xF8

extern int lsm_flac_parse_streaminfo
(
    const uint8_t *streaminfo,
    uint32_t       size,
    uint32_t      *sample_rate,
    uint8_t       *channels,
    uint8_t       *bits_per_sample,
    uint32_t      *min_block_size,
    uint32_t      *max_block_size,
    uint64_t      *total_samples
);

extern int lsm_flac_parse_frame_header
(
    const uint8_t *frame_data,
    uint32_t       size,
    uint32_t      *block_size,
    uint64_t      *coded_number,
    int           *blocking_strategy
);

typedef struct
{
    uint8_t  last;
    uint8_t  type;
    uint32_t length;
    uint8_t *data;
} flac_metadata_block_t;

typedef struct
{
    lsm_list_t  metadata_blocks;
    uint32_t    sample_rate;
    uint8_t     channels;
    uint8_t     bits_per_sample;
    uint32_t    min_block_size;
    uint32_t    max_block_size;
    uint64_t    total_samples;

    uint64_t    next_dts;       /* cumulative DTS in samples, starts at 0 */
    uint32_t    last_block_size;
} flac_importer_t;

static void flac_imp_cleanup_metadata_block( flac_metadata_block_t *block )
{
    if( !block )
        return;
    lsmash_free( block->data );
}

static void flac_imp_remove( flac_importer_t *imp )
{
    if( !imp )
        return;
    lsm_list_clear( &imp->metadata_blocks );
    lsmash_free( imp );
}

static void flac_imp_cleanup( importer_t *imp )
{
    debug_if( imp && imp->info )
        flac_imp_remove( imp->info );
}

static flac_importer_t *flac_imp_create( void )
{
    flac_importer_t *imp = (flac_importer_t *)lsmash_malloc_zero( sizeof(flac_importer_t) );
    if( !imp )
        return NULL;
    lsm_list_init( &imp->metadata_blocks, sizeof(flac_metadata_block_t), flac_imp_cleanup_metadata_block );
    return imp;
}

/* Scan for a FLAC frame sync code in the buffer starting at 'start_offset'.
 * Return the offset of the sync code, or -1 if not found. */
static int flac_imp_find_sync( const uint8_t *data, uint32_t size, uint32_t start_offset )
{
    for( uint32_t offset = start_offset; offset + 1 < size; offset++ )
    {
        if( data[offset] == FLAC_SYNC_BYTE1
         && (data[offset + 1] & FLAC_SYNC_MASK_BYTE2) == FLAC_SYNC_MIN_BYTE2 )
            return (int)offset;
    }
    return -1;
}

/* Probe function: check if the stream is a valid FLAC stream. */
static int flac_imp_probe( importer_t *imp )
{
    flac_importer_t *flac_imp = flac_imp_create();
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

    /* Read all metadata blocks after the fLaC marker into the paged block array.
     * We compute the exact byte offset where audio frames start:
     * audio_start = 4 (fLaC) + sum_of(4 + data_length) for each metadata block. */
    uint64_t metadata_total   = 0;
    int      found_streaminfo = 0;

    while( 1 )
    {
        while( lsmash_bs_get_remaining_buffer_size( bs ) < 4 )
        {
            if( bs->eof )
                break;
            if( lsmash_bs_read( bs, 4096 ) < 0 )
            {
                err = LSMASH_ERR_INVALID_DATA;
                goto fail;
            }
        }
        if( bs->eof && lsmash_bs_get_remaining_buffer_size( bs ) < 4 )
            break;

        uint8_t header[4];
        if( lsmash_bs_get_bytes_ex( bs, 4, header ) < 0 )
        {
            err = LSMASH_ERR_INVALID_DATA;
            goto fail;
        }
        uint8_t  last = (header[0] >> 7) & 0x01;
        uint8_t  type =  header[0]       & 0x7F;
        uint32_t len  = ((uint32_t)header[1] << 16) | ((uint32_t)header[2] << 8) | header[3];

        /* Per RFC 9639, BlockType 127 is invalid, and the first metadata block MUST be STREAMINFO (type 0, length 34). */
        if( type == 127
         || (lsm_list_count( &flac_imp->metadata_blocks ) == 0 && (type != 0 || len != 34))
         || lsm_list_count( &flac_imp->metadata_blocks ) >= FLAC_MAX_METADATA_BLOCKS
         || metadata_total + 4 + len > FLAC_MAX_METADATA_TOTAL )
        {
            err = LSMASH_ERR_INVALID_DATA;
            goto fail;
        }

        while( lsmash_bs_get_remaining_buffer_size( bs ) < len )
        {
            if( bs->eof )
            {
                err = LSMASH_ERR_INVALID_DATA;
                goto fail;
            }
            if( lsmash_bs_read( bs, LSMASH_MAX( len, 4096U ) ) < 0 )
            {
                err = LSMASH_ERR_INVALID_DATA;
                goto fail;
            }
        }

        uint8_t *block_payload = NULL;
        if( len > 0 )
        {
            block_payload = lsmash_malloc( len );
            if( !block_payload )
            {
                err = LSMASH_ERR_MEMORY_ALLOC;
                goto fail;
            }
            if( lsmash_bs_get_bytes_ex( bs, len, block_payload ) < 0 )
            {
                lsmash_free( block_payload );
                err = LSMASH_ERR_INVALID_DATA;
                goto fail;
            }
        }

        flac_metadata_block_t *block = lsm_list_append( &flac_imp->metadata_blocks );
        if( !block )
        {
            lsmash_free( block_payload );
            err = LSMASH_ERR_MEMORY_ALLOC;
            goto fail;
        }
        block->last   = last;
        block->type   = type;
        block->length = len;
        block->data   = block_payload;

        metadata_total += 4 + (uint64_t)len;

        if( type == 0 && !found_streaminfo )
        {
            if( lsm_flac_parse_streaminfo( block_payload, len,
                                           &flac_imp->sample_rate,
                                           &flac_imp->channels,
                                           &flac_imp->bits_per_sample,
                                           &flac_imp->min_block_size,
                                           &flac_imp->max_block_size,
                                           &flac_imp->total_samples ) < 0 )
            {
                err = LSMASH_ERR_INVALID_DATA;
                goto fail;
            }
            found_streaminfo = 1;
        }

        if( last )
            break;
    }

    if( !found_streaminfo || metadata_total == 0 || metadata_total > FLAC_MAX_METADATA_TOTAL )
    {
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }

    uint32_t meta_size = (uint32_t)metadata_total;
    uint8_t *meta_data = lsmash_malloc( meta_size );
    if( !meta_data )
    {
        err = LSMASH_ERR_MEMORY_ALLOC;
        goto fail;
    }
    uint32_t offset      = 0;
    uint32_t block_count = lsm_list_count( &flac_imp->metadata_blocks );
    uint32_t block_idx   = 0;
    flac_metadata_block_t *block;
    LSM_LIST_FOR_EACH( block, &flac_imp->metadata_blocks )
    {
        uint8_t is_last = (++block_idx == block_count) ? 0x80 : 0x00;
        meta_data[offset + 0] = is_last | (block->type & 0x7F);
        meta_data[offset + 1] = (uint8_t)((block->length >> 16) & 0xFF);
        meta_data[offset + 2] = (uint8_t)((block->length >>  8) & 0xFF);
        meta_data[offset + 3] = (uint8_t)( block->length        & 0xFF);
        offset += 4;
        if( block->length > 0 )
        {
            memcpy( &meta_data[offset], block->data, block->length );
            offset += block->length;
        }
    }

    /* Seek to the start of audio data (after fLaC + all metadata blocks).
     * This ensures the buffer is positioned correctly for frame reading. */
    lsmash_bs_read_seek( bs, (int64_t)(4 + metadata_total), SEEK_SET );

    /* Create summary with codec specific data */
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
    if( !summary )
    {
        lsmash_free( meta_data );
        err = LSMASH_ERR_NAMELESS;
        goto fail;
    }
    lsmash_codec_specific_t *cs = lsmash_create_codec_specific_data(
        LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_FLAC,
        LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
    if( !cs )
    {
        lsmash_free( meta_data );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        err = LSMASH_ERR_MEMORY_ALLOC;
        goto fail;
    }
    cs->data.unstructured = lsmash_create_flac_specific_info( meta_data, meta_size, &cs->size );
    lsmash_free( meta_data );
    if( !cs->data.unstructured
     || lsmash_list_add_entry( &summary->opaque->list, cs ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( cs );
        err = LSMASH_ERR_NAMELESS;
        goto fail;
    }

    summary->sample_type      = ISOM_CODEC_TYPE_FLAC_AUDIO;
    summary->max_au_length    = FLAC_MAX_FRAME_SIZE;
    /* FLAC doesn't use MP4A AOT, so we set it to NULL. */
    summary->aot              = MP4A_AUDIO_OBJECT_TYPE_NULL;
    summary->frequency        = flac_imp->sample_rate;
    summary->channels         = flac_imp->channels;
    summary->sample_size      = flac_imp->bits_per_sample;
    /* Use max block size as frame size hint. */
    summary->samples_in_frame = flac_imp->max_block_size;
    summary->sbr_mode         = MP4A_AAC_SBR_NOT_SPECIFIED;

    if( lsmash_list_add_entry( imp->summaries, summary ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        err = LSMASH_ERR_MEMORY_ALLOC;
        goto fail;
    }

    imp->info   = flac_imp;
    imp->status = IMPORTER_OK;
    return 0;

fail:
    flac_imp_remove( flac_imp );
    return err;
}

static int flac_imp_get_accessunit
(
    importer_t      *imp,
    uint32_t         track_number,
    lsmash_sample_t **p_sample
)
{
    if( !imp->info )
        return LSMASH_ERR_NAMELESS;
    if( track_number != 1 )
        return LSMASH_ERR_FUNCTION_PARAM;

    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_list_get_entry_data( imp->summaries, track_number );
    if( !summary )
        return LSMASH_ERR_NAMELESS;

    flac_importer_t *flac_imp = (flac_importer_t *)imp->info;
    importer_status current_status = imp->status;
    if( current_status == IMPORTER_ERROR )
        return LSMASH_ERR_NAMELESS;
    if( current_status == IMPORTER_EOF )
        return IMPORTER_EOF;

    lsmash_bs_t *bs     = imp->bs;
    uint64_t     remain = lsmash_bs_get_remaining_buffer_size( bs );

    if( remain < FLAC_MAX_FRAME_SIZE && !bs->eof )
    {
        if( lsmash_bs_read( bs, FLAC_MAX_FRAME_SIZE ) < 0 )
        {
            imp->status = IMPORTER_ERROR;
            return LSMASH_ERR_NAMELESS;
        }
        remain = lsmash_bs_get_remaining_buffer_size( bs );
    }

    /* Check for EOF */
    if( (bs->eof || bs->eob) && remain < 6 )
    {
        imp->status = IMPORTER_EOF;
        return IMPORTER_EOF;
    }

    uint8_t *buf = lsmash_bs_get_buffer_data( bs );

    /* Find first valid sync code */
    int      sync_offset       = -1;
    uint32_t block_size        = 0;
    uint64_t frame_num         = UINT64_MAX;
    int      blocking_strategy = 0;
    uint32_t search_from       = 0;

    while( 1 )
    {
        uint32_t search_limit = (remain > UINT32_MAX) ? UINT32_MAX : (uint32_t)remain;
        sync_offset = flac_imp_find_sync( buf, search_limit, search_from );
        if( sync_offset < 0 )
        {
            if( bs->eof || bs->eob )
            {
                imp->status = IMPORTER_EOF;
                return IMPORTER_EOF;
            }
            imp->status = IMPORTER_ERROR;
            return LSMASH_ERR_NAMELESS;
        }

        if( lsm_flac_parse_frame_header( buf + sync_offset,
                                         search_limit - (uint32_t)sync_offset,
                                         &block_size,
                                         &frame_num,
                                         &blocking_strategy ) == 0
         && block_size >= flac_imp->min_block_size
         && block_size <= flac_imp->max_block_size )
        {
            /* Found a valid FLAC frame */
            break;
        }

        /* Invalid frame header or block size - skip this false positive */
        search_from = (uint32_t)sync_offset + 2;
        if( search_from + 6 > search_limit )
        {
            imp->status = IMPORTER_ERROR;
            return LSMASH_ERR_NAMELESS;
        }
    }

    /* Find next frame boundary by searching for the next valid sync word */
    uint32_t frame_end = (remain > UINT32_MAX) ? UINT32_MAX : (uint32_t)remain;
    search_from = (uint32_t)sync_offset + 6;

    while( 1 )
    {
        uint32_t search_limit = (remain > UINT32_MAX) ? UINT32_MAX : (uint32_t)remain;
        int next_sync = flac_imp_find_sync( buf, search_limit, search_from );
        if( next_sync > sync_offset )
        {
            uint32_t next_block_size = 0;
            uint64_t next_frame_num  = UINT64_MAX;
            int      next_strategy   = 0;
            if( lsm_flac_parse_frame_header( buf + next_sync,
                                             search_limit - (uint32_t)next_sync,
                                             &next_block_size,
                                             &next_frame_num,
                                             &next_strategy ) == 0
             && next_strategy == blocking_strategy )
            {
                /* Further validate using frame/sample number:
                 * For fixed block size, frame number increments by 1.
                 * For variable block size, sample number increments by block_size. */
                uint64_t expected_next = (blocking_strategy == 0)
                                       ? frame_num + 1
                                       : frame_num + block_size;
                if( next_frame_num == expected_next )
                {
                    frame_end = (uint32_t)next_sync;
                    break;
                }
            }
            search_from = (uint32_t)next_sync + 2;
            continue;
        }

        /* No next sync found - need more data or EOF */
        if( bs->eof )
        {
            frame_end = search_limit;
            break;
        }
        /* Read more data */
        if( bs->buffer.pos > FLAC_MAX_FRAME_SIZE * 1000 )
            lsmash_bs_dispose_past_data( bs );
        if( lsmash_bs_read( bs, FLAC_MAX_FRAME_SIZE ) < 0 )
        {
            imp->status = IMPORTER_ERROR;
            return LSMASH_ERR_NAMELESS;
        }
        remain = lsmash_bs_get_remaining_buffer_size( bs );
        search_limit = (remain > UINT32_MAX) ? UINT32_MAX : (uint32_t)remain;
        if( search_limit < 6 )
        {
            frame_end = search_limit;
            break;
        }
        buf = lsmash_bs_get_buffer_data( bs );
        search_from = (uint32_t)sync_offset + 6;
    }

    uint32_t frame_size = frame_end - (uint32_t)sync_offset;
    if( frame_size < 6 || frame_size > FLAC_MAX_FRAME_SIZE )
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
    sample->length        = frame_size;
    sample->dts           = flac_imp->next_dts;
    sample->cts           = sample->dts;
    sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;

    flac_imp->last_block_size = block_size;
    flac_imp->next_dts       += block_size;
    summary->samples_in_frame = block_size;

    /* Advance the buffer past this frame so the next call starts after it. */
    lsmash_bs_skip_bytes( bs, (uint32_t)sync_offset + frame_size );

    remain = lsmash_bs_get_remaining_buffer_size( bs );
    if( (bs->eof || bs->eob) && remain < 6 )
        imp->status = IMPORTER_EOF;
    else
        imp->status = IMPORTER_OK;

    return current_status;
}

static uint32_t flac_imp_get_last_delta
(
    importer_t *imp,
    uint32_t    track_number
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
    flac_imp_probe,
    flac_imp_get_accessunit,
    flac_imp_get_last_delta,
    flac_imp_cleanup,
};
