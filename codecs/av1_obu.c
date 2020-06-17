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

/* This file is available under an ISC license. */

#include "common/internal.h" /* must be placed first */

#include "codecs/obuparse.h"
#include "codecs/av1.h"
#include "codecs/av1_obu.h"

#include <string.h>

lsmash_av1_specific_parameters_t *obu_av1_parse_first_tu
(
    lsmash_bs_t *bs,
    uint32_t length,
    uint32_t offset,
    obu_av1_pixel_properties_t *props
)
{
    lsmash_av1_specific_parameters_t *param = lsmash_malloc_zero( sizeof(lsmash_av1_specific_parameters_t) );
    if ( !param )
        return NULL;

    int seen_seq          = 0;
    int seen_frame        = 0;
    uint32_t off          = 0;
    uint8_t *data         = lsmash_bs_get_buffer_data( bs );
    uint64_t data_size    = lsmash_bs_get_remaining_buffer_size( bs );
    OBPSequenceHeader seq = { 0 };
    OBPState state        = { 0 };
    while( off < length )
    {
        int temporal_id, spatial_id;
        int seen_frame_header = 0;
        size_t obusize;
        ptrdiff_t pos;
        char errbuf[1024];
        OBPOBUType obutype;
        OBPError err = { &errbuf[0], 1024 };

        int oret = obp_get_next_obu( data + off + offset, data_size - off - offset,
                                     &obutype, &pos, &obusize, &temporal_id, &spatial_id, &err );
        if( oret < 0 )
        {
            av1_destruct_specific_data( param );
            return NULL;
        }
        off += pos;

        switch( obutype )
        {
            case OBP_OBU_SEQUENCE_HEADER:
            {
                int ret = obp_parse_sequence_header( data + off + offset, obusize, &seq, &err );
                if( ret < 0 )
                {
                    av1_destruct_specific_data( param );
                    return NULL;
                }

                param->seq_profile                          = seq.seq_profile;
                param->seq_level_idx_0                      = seq.seq_level_idx[0];
                param->seq_tier_0                           = seq.seq_tier[0];
                param->high_bitdepth                        = seq.color_config.high_bitdepth;
                param->monochrome                           = seq.color_config.mono_chrome;
                param->chroma_subsampling_x                 = seq.color_config.subsampling_x;
                param->chroma_subsampling_y                 = seq.color_config.subsampling_y;
                param->chroma_sample_position               = seq.color_config.chroma_sample_position;
                param->initial_presentation_delay_present   = seq.initial_display_delay_present_flag;
                param->initial_presentation_delay_minus_one = seq.initial_display_delay_minus_1[0]; /* is this right? */

                /* The values from the sequence header match ISO/IEC 23091-4. */
                props->primaries_index = seq.color_config.color_primaries;
                props->transfer_index  = seq.color_config.transfer_characteristics;
                props->matrix_index    = seq.color_config.matrix_coefficients;
                props->full_range      = !!seq.color_config.color_range;
                props->seq_width       = seq.max_frame_width_minus_1 + 1;
                props->seq_height      = seq.max_frame_height_minus_1 + 1;

                /* Only one sequence header OBU may be present in configOBUs. */
                if( !seen_seq )
                {
                    uint32_t oldpos       = param->configOBUs.sz;
                    param->configOBUs.sz += obusize + pos;
                    uint8_t *newdata      = lsmash_realloc( param->configOBUs.data, param->configOBUs.sz );
                    if( !newdata ) {
                        av1_destruct_specific_data( param );
                        return NULL;
                    }
                    param->configOBUs.data = newdata;
                    memcpy( param->configOBUs.data + oldpos, data + off + offset - pos, obusize + pos );
                }

                seen_seq = 1;
                off += obusize;

                break;
            }
            case OBP_OBU_FRAME:
            case OBP_OBU_FRAME_HEADER:
            {
                /* We've already seen one. */
                if ( seen_frame )
                    break;

                /* We must have seen a sequence header first. */
                if( !seen_seq )
                {
                    av1_destruct_specific_data( param );
                    return NULL;
                }

                OBPFrameHeader fh = { 0 };
                int ret;
                if( obutype == OBP_OBU_FRAME_HEADER )
                {
                    ret = obp_parse_frame_header( data + off + offset, obusize, &seq, &state,
                                                  temporal_id, spatial_id, &fh, &seen_frame_header, &err );
                }
                else
                {
                    OBPTileGroup tg = { 0 };
                    ret = obp_parse_frame( data + off + offset, obusize, &seq, &state,
                                           temporal_id, spatial_id, &fh, &tg, &seen_frame_header, &err );
                }
                if( ret < 0 )
                {
                    av1_destruct_specific_data( param );
                    return NULL;
                }

                props->render_width  = fh.RenderWidth;
                props->render_height = fh.RenderHeight;

                seen_frame = 1;

                off += obusize;

                break;
            }
            case OBP_OBU_METADATA:
            {
                /* Config OBUs are in a weird order. */
                if( !seen_seq )
                {
                    /*
                     * For the time being, we'll just give up. In theory, the proper thing to do is
                     * to always prepend the sequence header.
                     */
                    av1_destruct_specific_data( param );
                    return NULL;
                }

                uint32_t oldpos       = param->configOBUs.sz;
                param->configOBUs.sz += obusize + pos;
                uint8_t *newdata      = lsmash_realloc( param->configOBUs.data, param->configOBUs.sz );
                if( !newdata ) {
                    av1_destruct_specific_data( param );
                    return NULL;
                }
                param->configOBUs.data = newdata;
                memcpy( param->configOBUs.data + oldpos, data + off + offset - pos, obusize + pos );
                off += obusize;

                break;
            }
            case OBP_OBU_TEMPORAL_DELIMITER:
            {
                seen_frame_header = 0;

                off += obusize;
                break;
            }
            default:
                off += obusize;
                break;
        }
    }

    if( !seen_seq || !seen_frame )
    {
        av1_destruct_specific_data( param );
        return NULL;
    }

    return param;
}

static int include_obu(OBPOBUType obutype)
{
    return obutype == OBP_OBU_SEQUENCE_HEADER ||
           obutype == OBP_OBU_FRAME_HEADER ||
           obutype == OBP_OBU_TILE_GROUP ||
           obutype == OBP_OBU_METADATA ||
           obutype == OBP_OBU_FRAME;
}

uint8_t *obu_av1_assemble_sample
(
    uint8_t *packetbuf,
    uint32_t length,
    uint32_t *samplelength,
    obu_av1_sample_state_t *sstate,
    uint32_t *max_render_width,
    uint32_t *max_render_height,
    int *issync
)
{
    uint8_t *samplebuf   = NULL;
    uint32_t offset      = 0;
    int first_fh         = 1;
    int seen_seq_this_tu = 0;
    *samplelength        = 0;
    *issync              = 0;

    while( offset < length )
    {
        int temporal_id, spatial_id;
        size_t obusize;
        ptrdiff_t pos;
        char errbuf[1024];
        OBPOBUType obutype;
        OBPError err = { &errbuf[0], 1024 };

        int ret = obp_get_next_obu( packetbuf + offset, length - offset, &obutype,
                                    &pos, &obusize, &temporal_id, &spatial_id, &err );
        if( ret < 0 ) {
            lsmash_free( samplebuf );
            return NULL;
        }

        offset += pos;

        if( obutype == OBP_OBU_TEMPORAL_DELIMITER )
            sstate->seen_frame_header = 0;

        if( !include_obu(obutype) )
        {
            offset += obusize;
            continue;
        }

        switch( obutype ) {
            case OBP_OBU_SEQUENCE_HEADER:
            {
                sstate->seen_seq = 1;
                seen_seq_this_tu = 1;

                ret = obp_parse_sequence_header( packetbuf + offset, obusize, &sstate->seq, &err );
                if( ret < 0 ) {
                    lsmash_free( samplebuf );
                    return NULL;
                }

                break;
            }
            case OBP_OBU_FRAME_HEADER:
            case OBP_OBU_FRAME:
            {
                /* spec requires sync samples to have the seq header first */
                if( !sstate->seen_seq )
                    return NULL;

                OBPFrameHeader fh = { 0 };
                if ( obutype == OBP_OBU_FRAME_HEADER )
                {
                    ret = obp_parse_frame_header( packetbuf + offset, obusize, &sstate->seq, &sstate->state,
                                                  temporal_id, spatial_id, &fh, &sstate->seen_frame_header, &err );
                }
                else
                {
                    OBPTileGroup tg = { 0 };
                    ret = obp_parse_frame( packetbuf + offset, obusize, &sstate->seq, &sstate->state,
                                           temporal_id, spatial_id, &fh, &tg, &sstate->seen_frame_header, &err );
                }
                if( ret < 0 ) {
                    lsmash_free( samplebuf );
                    return NULL;
                }

                /* Track MaxRenderWidth and MaxRenderHeight */
                if( *max_render_width < fh.RenderWidth )
                    *max_render_width = fh.RenderWidth;
                if( *max_render_height < fh.RenderHeight )
                    *max_render_height = fh.RenderHeight;


                /*
                 * To be sync:
                 *     - Its first frame is a Key Frame that has show_frame flag set to 1,
                 *     - It contains a Sequence Header OBU before the first Frame Header OBU.
                 */
                if( seen_seq_this_tu && first_fh )
                    *issync = fh.show_frame && fh.frame_type == OBP_KEY_FRAME;

                first_fh = 0;

                break;
            }
            default:
                break;
        }

        uint32_t total_size = obusize + pos;
        uint8_t *newbuf     = lsmash_realloc( samplebuf, (*samplelength) + total_size );
        if( !newbuf )
        {
            lsmash_free( samplebuf );
            return NULL;
        }
        samplebuf = newbuf;

        memcpy(samplebuf + (*samplelength), &packetbuf[offset - pos], total_size);

        offset          += obusize;
        (*samplelength) += total_size;
    }

    return samplebuf;
}
