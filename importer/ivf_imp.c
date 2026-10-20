/*****************************************************************************
 * ivf_imp.c
 *****************************************************************************
 * Copyright (C) 2018-2020 L-SMASH project
 * Copyright (c) 2026 Yusuke Nakamura
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *          Derek Buitenhuis <derek.buitenhuis@gmail.com>
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

/*********************************************************************************
    Indeo Video Format (IVF) importer
**********************************************************************************/
#include "common/utils.h"
#include "codecs/av1.h"

#define IVF_LE_4CC( a, b, c, d ) (((d)<<24) | ((c)<<16) | ((b)<<8) | (a))

typedef struct
{
    /* stored as little endian in the bitstream */
    uint32_t signature;     /* = 'DKIF' */
    uint16_t version;       /* = 0 */
    uint16_t header_length;
    uint32_t codec_fourcc;
    uint16_t width;
    uint16_t height;
    uint32_t frame_rate;
    uint32_t time_scale;
    uint32_t number_of_frames;
    uint32_t unused;
} ivf_global_header_t;

typedef struct
{
    uint32_t                         au_length;
    uint32_t                         au_number;
    uint64_t                         pts;
    uint64_t                         first_pts_delta;
    uint32_t                         max_render_width;
    uint32_t                         max_render_height;
    ivf_global_header_t              global_header;
    av1_parser_t                     parser;
    lsmash_av1_specific_parameters_t av1c_param;
} ivf_importer_t;

static void remove_ivf_importer( ivf_importer_t *ivf_imp )
{
    if( !ivf_imp )
        return;
    av1_cleanup_parser( &ivf_imp->parser );
    lsmash_destroy_av1_headers( &ivf_imp->av1c_param );
    lsmash_free( ivf_imp );
}

static ivf_importer_t *create_ivf_importer( importer_t *importer )
{
    return (ivf_importer_t *)lsmash_malloc_zero( sizeof(ivf_importer_t) );
}

static void ivf_importer_cleanup( importer_t *importer )
{
    debug_if( importer && importer->info )
        remove_ivf_importer( importer->info );
}

static int ivf_importer_get_access_unit( lsmash_bs_t *bs, importer_t *importer, ivf_importer_t *ivf_imp )
{
    /* IVF frame header */
    ivf_imp->au_length = lsmash_bs_get_le32( bs );
    ivf_imp->pts       = lsmash_bs_get_le64( bs );
    if( bs->error )
    {
        importer->status = IMPORTER_EOF;
        if( ivf_imp->au_length == 0 )
            return IMPORTER_EOF;
    }
    return 0;
}

static int ivf_importer_get_accessunit( importer_t *importer, uint32_t track_number, lsmash_sample_t **p_sample )
{
    if( !importer->info )
        return LSMASH_ERR_NAMELESS;
    if( track_number != 1 )
        return LSMASH_ERR_FUNCTION_PARAM;
    ivf_importer_t *ivf_imp = (ivf_importer_t *)importer->info;
    importer_status current_status = importer->status;
    if( current_status == IMPORTER_ERROR )
        return LSMASH_ERR_NAMELESS;
    if( current_status == IMPORTER_EOF )
        return IMPORTER_EOF;
    if( lsmash_bs_is_end( importer->bs, 12 ) )
    {
        importer->status = IMPORTER_EOF;
        return IMPORTER_EOF;
    }
    int err = ivf_importer_get_access_unit( importer->bs, importer, ivf_imp );
    if( err < 0 )
    {
        importer->status = IMPORTER_ERROR;
        return err;
    }
    if( ivf_imp->au_length == 0 )
    {
        importer->status = IMPORTER_ERROR;
        return LSMASH_ERR_INVALID_DATA;
    }
    uint8_t *packetbuf = lsmash_malloc( ivf_imp->au_length );
    if( !packetbuf )
    {
        importer->status = IMPORTER_ERROR;
        return LSMASH_ERR_MEMORY_ALLOC;
    }
    if( lsmash_bs_get_bytes_ex( importer->bs, ivf_imp->au_length, packetbuf ) != ivf_imp->au_length )
    {
        lsmash_free( packetbuf );
        importer->status = IMPORTER_ERROR;
        return LSMASH_ERR_INVALID_DATA;
    }
    uint8_t *samplebuf = lsmash_malloc( ivf_imp->au_length );
    if( !samplebuf )
    {
        lsmash_free( packetbuf );
        importer->status = IMPORTER_ERROR;
        return LSMASH_ERR_MEMORY_ALLOC;
    }
    uint32_t samplesize = 0;
    lsmash_sample_property_t prop = { 0 };
    err = av1_parse_temporal_unit( &ivf_imp->parser,
                                   packetbuf,
                                   ivf_imp->au_length,
                                   samplebuf,
                                   &samplesize,
                                   &prop,
                                   &ivf_imp->av1c_param );
    lsmash_free( packetbuf );
    if( err < 0 )
    {
        lsmash_free( samplebuf );
        importer->status = IMPORTER_ERROR;
        return err;
    }
    uint32_t max_render_width  = ivf_imp->parser.MaxRenderWidth;
    uint32_t max_render_height = ivf_imp->parser.MaxRenderHeight;
    if( max_render_width > ivf_imp->max_render_width || max_render_height > ivf_imp->max_render_height )
    {
        lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_list_get_entry_data( importer->summaries, track_number );
        if( !summary )
        {
            lsmash_free( samplebuf );
            return LSMASH_ERR_NAMELESS;
        }
        uint64_t num = ((uint64_t)max_render_width)  * ((uint64_t)summary->height);
        uint64_t den = ((uint64_t)summary->width)    * ((uint64_t)max_render_height);
        lsmash_reduce_fraction( &num, &den );

        summary->par_h = (uint32_t)num;
        summary->par_v = (uint32_t)den;

        ivf_imp->max_render_width  = max_render_width;
        ivf_imp->max_render_height = max_render_height;

        current_status = IMPORTER_CHANGE;
    }

    lsmash_sample_t *sample = lsmash_create_sample( samplesize );
    if( !sample )
    {
        lsmash_free( samplebuf );
        return LSMASH_ERR_MEMORY_ALLOC;
    }
    *p_sample = sample;
    if( samplesize > 0 )
        memcpy( sample->data, samplebuf, samplesize );
    lsmash_free( samplebuf );
    if( !ivf_imp->first_pts_delta && ivf_imp->pts > 0 )
        ivf_imp->first_pts_delta = ivf_imp->pts;
    sample->length = samplesize;
    sample->dts    = ivf_imp->pts;
    sample->cts    = ivf_imp->pts;
    sample->prop   = prop;
    ivf_imp->au_number++;
    return current_status;
}

static lsmash_codec_type_t ivf_get_codec_type
(
    ivf_global_header_t *gh
)
{
    switch( gh->codec_fourcc )
    {
        case IVF_LE_4CC( 'A', 'V', '0', '1' ) :
            return ISOM_CODEC_TYPE_AV01_VIDEO;
        case IVF_LE_4CC( 'V', 'P', '0', '8' ) :
            return ISOM_CODEC_TYPE_VP08_VIDEO;
        case IVF_LE_4CC( 'V', 'P', '0', '9' ) :
            return ISOM_CODEC_TYPE_VP09_VIDEO;
        default :
            return LSMASH_CODEC_TYPE_UNSPECIFIED;
    }
}

static lsmash_video_summary_t *ivf_create_summary( ivf_importer_t *ivf_imp )
{
    ivf_global_header_t              *gh     = &ivf_imp->global_header;
    av1_sequence_header_t            *sh     = &ivf_imp->parser.sequence_header;
    lsmash_av1_specific_parameters_t *params = &ivf_imp->av1c_param;

    lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_VIDEO );
    if( !summary )
        return NULL;
    lsmash_codec_type_t codec_type = ivf_get_codec_type( gh );
    lsmash_codec_specific_t *specific = NULL;
    if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_AV01_VIDEO ) )
        specific = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_AV1,
                                                      LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
    else
        assert( 0 );
    if( !specific )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }

    lsmash_av1_specific_parameters_t *dst_param = (lsmash_av1_specific_parameters_t *)specific->data.structured;
    *dst_param = *params;
    dst_param->configOBUs.data = NULL;
    dst_param->configOBUs.sz   = 0;
    if( params->configOBUs.sz > 0 && params->configOBUs.data )
    {
        dst_param->configOBUs.data = lsmash_memdup( params->configOBUs.data, params->configOBUs.sz );
        if( !dst_param->configOBUs.data )
        {
            lsmash_cleanup_summary( (lsmash_summary_t *)summary );
            lsmash_destroy_codec_specific_data( specific );
            return NULL;
        }
        dst_param->configOBUs.sz = params->configOBUs.sz;
    }

    if( lsmash_list_add_entry( &summary->opaque->list, specific ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        return NULL;
    }

    uint32_t seq_width     = sh->max_frame_width_minus_1  + 1;
    uint32_t seq_height    = sh->max_frame_height_minus_1 + 1;
    uint32_t render_width  = ivf_imp->max_render_width  ? ivf_imp->max_render_width  : seq_width;
    uint32_t render_height = ivf_imp->max_render_height ? ivf_imp->max_render_height : seq_height;
    uint64_t num = ((uint64_t)render_width) * ((uint64_t)seq_height);
    uint64_t den = ((uint64_t)seq_width)    * ((uint64_t)render_height);
    lsmash_reduce_fraction( &num, &den );

    summary->sample_type           = codec_type;
    summary->timescale             = gh->frame_rate;
    summary->timebase              = gh->time_scale;
    summary->vfr                   = 0;
    summary->sample_per_field      = 0;
    summary->width                 = seq_width;
    summary->height                = seq_height;
    summary->par_h                 = (uint32_t)num;
    summary->par_v                 = (uint32_t)den;
    summary->color.primaries_index = sh->color_config.color_primaries;
    summary->color.transfer_index  = sh->color_config.transfer_characteristics;
    summary->color.matrix_index    = sh->color_config.matrix_coefficients;
    summary->color.full_range      = sh->color_config.color_range;
    summary->max_au_length         = UINT32_MAX; /* unused */

    return summary;
}

static int ivf_importer_probe( importer_t *importer )
{
    ivf_importer_t *ivf_imp = create_ivf_importer( importer );
    if( !ivf_imp )
        return LSMASH_ERR_MEMORY_ALLOC;
    int err = av1_setup_parser( &ivf_imp->parser, NULL );
    if( err < 0 )
        goto fail;
    lsmash_bs_t *bs = importer->bs;
    if( lsmash_bs_is_end( bs, 31 ) )
    {
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }
    ivf_global_header_t *gh = &ivf_imp->global_header;
    gh->signature     = lsmash_bs_show_le32( bs, 0 );
    gh->version       = lsmash_bs_show_le16( bs, 4 );
    gh->header_length = lsmash_bs_show_le16( bs, 6 );
    if( gh->signature     != IVF_LE_4CC( 'D', 'K', 'I', 'F' )
     || gh->version       != 0
     || gh->header_length != 32 )
    {
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }
    gh->codec_fourcc     = lsmash_bs_show_le32( bs, 8 );
    gh->width            = lsmash_bs_show_le16( bs, 12 );
    gh->height           = lsmash_bs_show_le16( bs, 14 );
    gh->frame_rate       = lsmash_bs_show_le32( bs, 16 );
    gh->time_scale       = lsmash_bs_show_le32( bs, 20 );
    gh->number_of_frames = lsmash_bs_show_le32( bs, 24 );
    if( gh->frame_rate == 0 || gh->time_scale == 0 )
    {
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }
    /* Set up the access unit parser. */
    lsmash_codec_type_t codec_type = ivf_get_codec_type( gh );
    if( lsmash_check_codec_type_identical( codec_type, LSMASH_CODEC_TYPE_UNSPECIFIED ) )
    {
        err = LSMASH_ERR_PATCH_WELCOME;
        goto fail;
    }
    else if( !lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_AV01_VIDEO ) )
    {
        /* We only support AV1 for now... */
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }
    lsmash_bs_skip_bytes( bs, gh->header_length );
    if( lsmash_bs_is_end( bs, 11 ) )
    {
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }
    /* Parse the first packet to get pixel aspect ratio and color information. */
    uint32_t au_length = lsmash_bs_show_le32( bs, 0 );
    if( au_length == 0 || lsmash_bs_is_end( bs, 12 + au_length - 1 ) )
    {
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }
    uint8_t *probe_buf = lsmash_malloc( au_length );
    if( !probe_buf )
    {
        err = LSMASH_ERR_MEMORY_ALLOC;
        goto fail;
    }
    for( uint32_t i = 0; i < au_length; i++ )
        probe_buf[i] = lsmash_bs_show_byte( bs, 12 + i );
    err = av1_parse_temporal_unit( &ivf_imp->parser,
                                   probe_buf,
                                   au_length,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &ivf_imp->av1c_param );
    lsmash_free( probe_buf );
    if( err < 0 )
        goto fail;
    if( !ivf_imp->parser.seen_seq_header || !ivf_imp->parser.MaxRenderWidth )
    {
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }

    /* Stash for use later while reading samples. */
    ivf_imp->max_render_width  = ivf_imp->parser.MaxRenderWidth
                               ? ivf_imp->parser.MaxRenderWidth
                               : (ivf_imp->parser.sequence_header.max_frame_width_minus_1 + 1);
    ivf_imp->max_render_height = ivf_imp->parser.MaxRenderHeight
                               ? ivf_imp->parser.MaxRenderHeight
                               : (ivf_imp->parser.sequence_header.max_frame_height_minus_1 + 1);

    lsmash_video_summary_t *summary = ivf_create_summary( ivf_imp );
    if( !summary )
    {
        err = LSMASH_ERR_NAMELESS;
        goto fail;
    }
    if( (err = lsmash_list_add_entry( importer->summaries, summary )) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        err = LSMASH_ERR_NAMELESS;
        goto fail;
    }

    /* Reset parser frame/reference state so Frame 0 is parsed cleanly in get_accessunit. */
    av1_sequence_header_t saved_sh = ivf_imp->parser.sequence_header;
    uint8_t saved_hdr10p           = ivf_imp->parser.has_hdr10p;
    av1_reset_parser( &ivf_imp->parser );
    ivf_imp->parser.sequence_header = saved_sh;
    ivf_imp->parser.seen_seq_header = 1;
    ivf_imp->parser.has_hdr10p      = saved_hdr10p;
    ivf_imp->parser.MaxRenderWidth  = ivf_imp->max_render_width;
    ivf_imp->parser.MaxRenderHeight = ivf_imp->max_render_height;

    importer->info   = ivf_imp;
    importer->status = IMPORTER_OK;
    return 0;
fail:
    remove_ivf_importer( ivf_imp );
    importer->info = NULL;
    return err;
}

static uint32_t ivf_importer_get_last_delta( importer_t *importer, uint32_t track_number )
{
    debug_if( !importer || !importer->info )
        return 0;
    ivf_importer_t *ivf_imp = (ivf_importer_t *)importer->info;
    if( !ivf_imp || track_number != 1 || importer->status != IMPORTER_EOF )
        return 0;
    lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_list_get_entry_data( importer->summaries, track_number );
    if( !summary )
        return 0;
    return ivf_imp->first_pts_delta ? (uint32_t)ivf_imp->first_pts_delta : 1;
}

const importer_functions ivf_importer =
{
    { "Indeo Video Format", offsetof( importer_t, log_level ) },
    1,
    ivf_importer_probe,
    ivf_importer_get_accessunit,
    ivf_importer_get_last_delta,
    ivf_importer_cleanup,
};
