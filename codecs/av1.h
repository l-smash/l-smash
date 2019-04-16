/*****************************************************************************
 * av1.h
 *****************************************************************************
 * Copyright (C) 2018 L-SMASH project
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

struct lsmash_av1_config_obus_tag
{
    lsmash_entry_list_t sequence_header_list[1];    /* at most one entry though */
    lsmash_entry_list_t metadata_list       [1];
};

enum
{
    AV1_OBU_TYPE_SEQUENCE_HEADER        = 1,
    AV1_OBU_TYPE_TEMPORAL_DELIMITER     = 2,
    AV1_OBU_TYPE_FRAME_HEADER           = 3,
    AV1_OBU_TYPE_TILE_GROUP             = 4,
    AV1_OBU_TYPE_METADATA               = 5,
    AV1_OBU_TYPE_FRAME                  = 6,
    AV1_OBU_TYPE_REDUNDANT_FRAME_HEADER = 7,
    AV1_OBU_TYPE_TILE_LIST              = 8,
    AV1_OBU_TYPE_PADDING                = 15
};

typedef struct
{
    uint32_t sz;
    uint8_t *obu;
    /* */
    int      unused;
} av1_config_obus_entry_t;

int av1_get_access_unit
(
    lsmash_bs_t               *bs,
    lsmash_sample_property_t  *prop,
    lsmash_video_summary_t   **summary,
    uint8_t                   *au_data,
    uint32_t                   au_length,
    av1_parser_t              *parser
);

uint8_t *lsmash_create_av1_specific_info(
    lsmash_av1_specific_parameters_t *param,
    uint32_t *data_length
);
