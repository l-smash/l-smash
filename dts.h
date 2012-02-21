/*****************************************************************************
 * dts.h:
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

#define DTS_MAX_CORE_SIZE      16384
#define DTS_MAX_EXTENSION_SIZE 32768

typedef enum
{
    DTS_SUBSTREAM_TYPE_NONE      = 0,
    DTS_SUBSTREAM_TYPE_CORE      = 1,
    DTS_SUBSTREAM_TYPE_EXTENSION = 2,
} dts_substream_type;

typedef struct
{
    uint32_t sampling_frequency;
    uint32_t frame_duration;
    uint16_t frame_size;
    uint16_t channel_layout;
    uint8_t  channel_arrangement;
    uint8_t  xxch_lower_planes;
    uint8_t  extension_audio_descriptor;
    uint8_t  pcm_resolution;
} dts_core_info_t;

typedef struct
{
    uint32_t sampling_frequency;
    uint32_t frame_duration;
    uint16_t channel_layout;
    uint8_t  xxch_lower_planes;
    uint8_t  bStaticFieldsPresent;
    uint8_t  bMixMetadataEnbl;
    uint8_t  bOne2OneMapChannels2Speakers;
    uint8_t  nuNumMixOutConfigs;
    uint8_t  nNumMixOutCh[4];
    uint8_t  number_of_assets;
    uint8_t  stereo_downmix;
    uint8_t  representation_type;
    uint8_t  bit_resolution;
} dts_extension_info_t;

typedef struct
{
    uint32_t sampling_frequency;
    uint32_t frame_duration;
    uint16_t channel_layout;
    uint8_t  bit_width;
} dts_lossless_info_t;

typedef struct
{
    uint32_t sampling_frequency;
    uint32_t frame_duration;
    uint16_t channel_layout;
    uint8_t  stereo_downmix;
    uint8_t  lfe_present;
    uint8_t  duration_modifier;
    uint8_t  sample_size;
} dts_lbr_info_t;

typedef struct
{
    dts_substream_type               substream_type;
    lsmash_dts_construction_flag     flags;
    lsmash_dts_specific_parameters_t ddts_param;
    dts_core_info_t                  core;
    dts_extension_info_t             extension;
    dts_lossless_info_t              lossless;
    dts_lbr_info_t                   lbr;
    uint8_t  ddts_param_initialized;
    uint8_t  no_more_read;
    uint8_t  extension_index;
    uint8_t  extension_substream_count;
    uint32_t frame_duration;
    uint32_t frame_size;
    uint8_t  buffer[2 * DTS_MAX_EXTENSION_SIZE];
    uint8_t *buffer_pos;
    uint8_t *buffer_end;
    lsmash_bits_t *bits;
    lsmash_multiple_buffers_t *au_buffers;
    uint8_t *au;
    uint32_t au_length;
    uint8_t *incomplete_au;
    uint32_t incomplete_au_length;
    uint32_t au_number;
} dts_info_t;

int dts_parse_core_substream( dts_info_t *info, uint8_t *data, uint32_t data_length );
int dts_parse_extension_substream( dts_info_t *info, uint8_t *data, uint32_t data_length );
int dts_get_channel_count_from_channel_layout( uint16_t channel_layout );
dts_substream_type dts_get_substream_type( dts_info_t *info );
int dts_get_extension_index( dts_info_t *info, uint8_t *extension_index );
void dts_update_specific_param( dts_info_t *info );
