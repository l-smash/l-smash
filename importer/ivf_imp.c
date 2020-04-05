/*****************************************************************************
 * ivf_imp.c
 *****************************************************************************
 * Copyright (C) 2018-2020 L-SMASH project
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
#include "codecs/obuparse.h"
#include "codecs/av1.h"
#include "codecs/av1_obu.h"

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
    uint32_t au_length;
    uint32_t au_number;
    uint64_t pts;
    uint64_t first_pts_delta;
    uint32_t max_render_width;
    uint32_t max_render_height;
    ivf_global_header_t global_header;
    obu_av1_sample_state_t sstate;
    int (*get_access_unit)( lsmash_bs_t *bs, lsmash_sample_property_t *prop, uint32_t au_length );
} ivf_importer_t;

static void remove_ivf_importer( ivf_importer_t *ivf_imp )
{
    if( !ivf_imp )
        return;
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

static int ivf_importer_get_access_unit( lsmash_bs_t *bs, importer_t *importer, ivf_importer_t *ivf_imp, lsmash_sample_property_t *prop )
{
    /* IVF frame header */
    ivf_imp->au_length = lsmash_bs_get_le32( bs );
    ivf_imp->pts       = lsmash_bs_get_le64( bs );
    if( bs->error )/* TODO: eof handling */
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
    if ( lsmash_bs_is_end( importer->bs, 12 ) )
    {
        importer->status = IMPORTER_EOF;
        return IMPORTER_EOF;
    }
    lsmash_sample_property_t prop = {0};
    int err = ivf_importer_get_access_unit( importer->bs, importer, ivf_imp, &prop );
    if( err < 0 )
    {
        importer->status = IMPORTER_ERROR;
        return err;
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
    uint32_t samplesize;
    int issync;
    uint32_t max_render_width  = ivf_imp->max_render_width;
    uint32_t max_render_height = ivf_imp->max_render_height;
    uint8_t *samplebuf = obu_av1_assemble_sample( packetbuf, ivf_imp->au_length, &samplesize,
                                                  &ivf_imp->sstate, &max_render_width, &max_render_height, &issync );
    lsmash_free( packetbuf );
    if( !samplebuf )
    {
        importer->status = IMPORTER_ERROR;
        return LSMASH_ERR_INVALID_DATA;
    }
    if( issync )
        prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    /*
     * If, for some reason, we encounter a RenderWidth or RenderHeight larer than our current
     * MaxRenderWidth or MaxRenderHeight, we need to create a new sample entry in order to
     * keep the file legal, as per '2.2.4 Semantics' of the AV1-ISOBMFF spec.
     *
     * Ideally we would only have one sample entry that represents the whole file, but that
     * would require scanning the entire file up front before we create our original sample
     * entry.
     */
    if ( max_render_width > ivf_imp->max_render_width || max_render_height > ivf_imp->max_render_height ) {
        lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_list_get_entry_data( importer->summaries, track_number );
        if( !summary ) {
            return LSMASH_ERR_NAMELESS;
        }
        uint64_t num = ((uint64_t) max_render_width) * ((uint64_t) summary->height);
        uint64_t den = ((uint64_t) summary->width) * ((uint64_t) max_render_height);
        lsmash_reduce_fraction( &num, &den );

        summary->par_h = num;
        summary->par_v = den;

        ivf_imp->max_render_width  = max_render_width;
        ivf_imp->max_render_height = max_render_height;

        current_status = IMPORTER_CHANGE;
    }

    lsmash_sample_t *sample = lsmash_create_sample( samplesize );
    if( !sample ) {
        lsmash_free( samplebuf );
        return LSMASH_ERR_MEMORY_ALLOC;
    }
    *p_sample = sample;
    memcpy( sample->data, samplebuf, samplesize );
    lsmash_free( samplebuf );
    if( !ivf_imp->first_pts_delta )
        ivf_imp->first_pts_delta = ivf_imp->pts;
    sample->length = samplesize;
    sample->dts    = ivf_imp->pts;
    sample->cts    = ivf_imp->pts;
    sample->prop   = prop;
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

static lsmash_video_summary_t *ivf_create_summary( ivf_global_header_t *gh, lsmash_av1_specific_parameters_t *params, obu_av1_pixel_properties_t *props )
{
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

    lsmash_av1_specific_parameters_t *src_param = (lsmash_av1_specific_parameters_t *) specific->data.structured;
    *src_param = *params; /* from our probe */
    params->configOBUs.sz = 0;
    params->configOBUs.data = NULL; /* horrible hack. so horrible. but we own this pointer now. */

    lsmash_codec_specific_t *dst_cs = lsmash_convert_codec_specific_format( specific, LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
    lsmash_destroy_codec_specific_data( specific );
    if( !dst_cs )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( dst_cs );
        return NULL;
    }
    if ( lsmash_list_add_entry( &summary->opaque->list, dst_cs ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( dst_cs );
        return NULL;
    }

    uint64_t num = ((uint64_t) props->render_width) * ((uint64_t) props->seq_height);
    uint64_t den = ((uint64_t) props->seq_width) * ((uint64_t) props->render_height);
    lsmash_reduce_fraction( &num, &den );

    summary->sample_type           = codec_type;
    summary->timescale             = gh->frame_rate;
    summary->timebase              = gh->time_scale;
    summary->vfr                   = 0; /* TODO: VFR IVF? Does this actually exist in practice? :( */
    summary->sample_per_field      = 0;
    summary->width                 = props->seq_width;
    summary->height                = props->seq_height;
    summary->par_h                 = num;
    summary->par_v                 = den;
    summary->color.primaries_index = props->primaries_index;
    summary->color.transfer_index  = props->transfer_index;
    summary->color.matrix_index    = props->matrix_index;
    summary->color.full_range      = props->full_range;
    summary->max_au_length         = UINT32_MAX; /* unused */

    return summary;
}

static int ivf_importer_probe( importer_t *importer )
{
    ivf_importer_t *ivf_imp = create_ivf_importer( importer );
    if( !ivf_imp )
        return LSMASH_ERR_MEMORY_ALLOC;
    int err = 0;
    lsmash_bs_t *bs = importer->bs;
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
    gh->number_of_frames = lsmash_bs_show_le64( bs, 24 );
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
    /* Parse the first packet to get pixel aspect ratio and color information. */
    uint32_t au_length = lsmash_bs_show_le32( bs, 0 );
    obu_av1_pixel_properties_t props = { 0 };
    /*
     * This is probably not OK, since we're calling av1_obu.c functions directly...
     * We should probably make it go through proper channels.
     */
    lsmash_av1_specific_parameters_t *params = obu_av1_parse_first_tu( bs, au_length, 12, &props );
    if ( !params )
    {
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }

    /* Stash for use later while reading samples. */
    ivf_imp->max_render_width  = props.render_width;
    ivf_imp->max_render_height = props.render_height;

    lsmash_video_summary_t *summary = ivf_create_summary( &ivf_imp->global_header, params, &props );
    av1_destruct_specific_data( params );
    if( !summary )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        err = LSMASH_ERR_NAMELESS;
        goto fail;
    }
    if( (err = lsmash_list_add_entry( importer->summaries, summary )) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        err = LSMASH_ERR_NAMELESS;
        goto fail;
    }
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
    /* only works for CFR... */
    return ivf_imp->first_pts_delta;
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
