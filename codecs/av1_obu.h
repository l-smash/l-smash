/*****************************************************************************
 * av1_obu.c
 *****************************************************************************
 * Copyright (C) 2020 L-SMASH Project
 *
 * Authors: Derek Buitenhuis <derek.buitenhuis@gmail.com>
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

typedef struct {
    uint32_t seq_width;
    uint32_t seq_height;
    uint32_t render_width;
    uint32_t render_height;
    uint16_t primaries_index;
    uint16_t transfer_index;
    uint16_t matrix_index;
    uint8_t  full_range;
} obu_av1_pixel_properties_t;

typedef struct {
    OBPState state;
    OBPSequenceHeader seq;
    int seen_seq;
    int seen_frame_header;
} obu_av1_sample_state_t;

lsmash_av1_specific_parameters_t *obu_av1_parse_first_tu
(
    lsmash_bs_t *bs,
    uint32_t length,
    uint32_t offset,
    obu_av1_pixel_properties_t *props
);

uint8_t *obu_av1_assemble_sample
(
    uint8_t *packetbuf,
    uint32_t length,
    uint32_t *samplelength,
    obu_av1_sample_state_t *sstate,
    uint32_t *max_render_width,
    uint32_t *max_render_height,
    int *issync
);
