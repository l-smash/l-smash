/*****************************************************************************
 * a52.h:
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

#define AC3_MIN_SYNCFRAME_LENGTH  128
#define AC3_MAX_SYNCFRAME_LENGTH  3840
#define EAC3_MAX_SYNCFRAME_LENGTH 4096

#define IF_A52_SYNCWORD( x ) if( (x)[0] != 0x0b || (x)[1] != 0x77 )


typedef struct
{
    lsmash_ac3_specific_parameters_t dac3_param;
    lsmash_bits_t *bits;
    uint8_t  buffer[AC3_MAX_SYNCFRAME_LENGTH];
    uint8_t *next_dac3;
    uint32_t au_number;
} ac3_info_t;

typedef struct
{
    lsmash_eac3_specific_parameters_t dec3_param;
    lsmash_eac3_substream_info_t independent_info[8];
    lsmash_eac3_substream_info_t dependent_info;
    uint8_t dec3_param_initialized;
    uint8_t strmtyp;
    uint8_t substreamid;
    uint8_t current_independent_substream_id;
    uint8_t numblkscod;
    uint8_t number_of_audio_blocks;
    uint8_t frmsizecod;
    uint8_t number_of_independent_substreams;
    uint8_t no_more_read;
    uint8_t *next_dec3;
    uint32_t next_dec3_length;
    uint32_t syncframe_count;
    uint32_t syncframe_count_in_au;
    uint32_t frame_size;
    uint8_t  buffer[2 * EAC3_MAX_SYNCFRAME_LENGTH];
    uint8_t *buffer_pos;
    uint8_t *buffer_end;
    lsmash_bits_t *bits;
    lsmash_multiple_buffers_t *au_buffers;
    uint8_t *au;
    uint32_t au_length;
    uint8_t *incomplete_au;
    uint32_t incomplete_au_length;
    uint32_t au_number;
} eac3_info_t;

static const uint8_t eac3_audio_block_table[4] = { 1, 2, 3, 6 };

int ac3_parse_syncframe_header( ac3_info_t *info, uint8_t *data );
int eac3_parse_syncframe( eac3_info_t *info, uint8_t *data, uint32_t data_length );
void eac3_update_specific_param( eac3_info_t *info );
