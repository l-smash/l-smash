/*****************************************************************************
 * ivf_imp.c
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

#include "common/internal.h" /* must be placed first */

#include <string.h>

#define LSMASH_IMPORTER_INTERNAL
#include "importer.h"

/*********************************************************************************
    Indeo Video Format (IVF) importer
**********************************************************************************/
#include "codec/av1.h"

#define IVF_LE_4CC( a, b, c, d ) (((d)<<24) | ((c)<<16) | ((b)<<8) | (a))

typedef struct
{
    /* stored as little endian in the bitstream */
    uint32_t signature;     /* = 'DKIF' */
    uint16_t version;       /* = 0 */
    uint16_t header_length;
    uint32_t codec_fourcc;
    uint16_t width;         /* unused for importing */
    uint16_t height;        /* unused for importing */
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
    ivf_global_header_t global_header;
    int (*get_access_unit)( importer_t * );
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

static int ivf_importer_get_access_unit( lsmash_bs_t *bs, ivf_importer_t *ivf_imp, lsmash_sample_property_t *prop )
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
    return importer->get_access_unit( bs, prop, ivf_imp->au_length );
}

/* TODO: EOF handling, parse AV1 temporal unit to set sample prop */
static int ivf_importer_get_accessunit( importer_t *importer, uint32_t track_number, lsmash_sample_t **p_sample )
{
    if( !importer->info )
        return LSMASH_ERR_NAMELESS;
    if( track_number != 1 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_list_get_entry_data( importer->summaries, track_number );
    if( !summary )
        return LSMASH_ERR_NAMELESS;
    ivf_importer_t *ivf_imp = (ivf_importer_t *)importer->info;
    importer_status current_status = importer->status;
    if( current_status == IMPORTER_ERROR )
        return LSMASH_ERR_NAMELESS;
    if( current_status == IMPORTER_EOF )
        return IMPORTER_EOF;
    lsmash_sample_property_t prop;
    int err = ivf_importer_get_access_unit( importer->bs, ivf_imp, &prop );
    if( err < 0 )
    {
        importer->status = IMPORTER_ERROR;
        return err;
    }
    lsmash_sample_t *sample = lsmash_create_sample( ivf_imp->au_length );
    if( !sample )
        return LSMASH_ERR_MEMORY_ALLOC;
    *p_sample = sample;
    if( lsmash_bs_get_bytes_ex( importer->bs, ivf_imp->au_length, sample->data ) != ivf_imp->au_length )
    {
        importer->status = IMPORTER_ERROR;
        return LSMASH_ERR_INVALID_DATA;
    }
    sample->length = ivf_imp->au_length;
    sample->dts    = pts;
    sample->cts    = pts;
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

static lsmash_video_summary_t *ivf_create_summary( ivf_global_header_t *gh )
{
    lsmash_video_summary_t *summary = (lsmash_video_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_VIDEO );
    if( !summary )
        return NULL;
    lsmash_codec_type_t codec_type = ivf_get_codec_type( gh );
    lsmash_codec_specific_t *specific = NULL;
    lsmash_codec_specific_data_type required_data_type = LSMASH_CODEC_SPECIFIC_DATA_TYPE_UNSPECIFIED;
    if( lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_AV01_VIDEO ) )
        specific = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_AV1,
                                                      LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED );
    else
        assert( 0 );
    if( !specific )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
    specific->data.unstructured = NULL;
    if( !specific->data.unstructured
     || lsmash_list_add_entry( &summary->opaque->list, specific ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        lsmash_destroy_codec_specific_data( specific );
        return NULL;
    }
    summary->sample_type      = codec_type;
    summary->timescale        = gh->frame_rate;
    summary->timebase         = gh->time_scale;
    summary->vfr              = 1; /* TODO */
    summary->sample_per_field = 0;
    summary->width            = gh->width;  /* TODO */
    summary->height           = gh->height; /* TODO */
#if 0
    summary->par_h                 = 1;
    summary->par_v                 = 1;
    summary->color.primaries_index = sequence->color_prim;
    summary->color.transfer_index  = sequence->transfer_char;
    summary->color.matrix_index    = sequence->matrix_coef;
#endif
    summary->max_au_length    = UINT32_MAX; /* unused */
    /* TODO: pasp and colr */
    return summary;
fail:
    lsmash_cleanup_summary( (lsmash_summary_t *)summary );
    return NULL;
}

static int ivf_importer_probe( importer_t *importer )
{
    ivf_importer_t *ivf_imp = create_ivf_importer( importer );
    if( !ivf_imp )
        return LSMASH_ERR_MEMORY_ALLOC;
    int err = 0;
    lsmash_bs_t *bs = importer->bs;
    ivf_global_header_t *gh = &ivf_imp->global_header;
    gh->signature     = lsmash_bs_show_le32( bs );
    gh->version       = lsmash_bs_show_le16( bs );
    gh->header_length = lsmash_bs_show_le16( bs );
    if( gh->signature     != IVF_LE_4CC( 'D', 'K', 'I', 'F' )
     || gh->version       != 0
     || gh->header_length != 32 )
    {
        err = LSMASH_ERR_INVALID_DATA;
        goto fail;
    }
    gh->codec_fourcc     = lsmash_bs_show_le32( bs );
    gh->width            = lsmash_bs_show_le16( bs );
    gh->height           = lsmash_bs_show_le16( bs );
    gh->frame_rate       = lsmash_bs_show_le32( bs );
    gh->time_scale       = lsmash_bs_show_le32( bs );
    gh->number_of_frames = lsmash_bs_show_le64( bs );
    /* Set up the access unit parser. */
    lsmash_codec_type_t codec_type = ivf_get_codec_type( gh );
    if( lsmash_check_codec_type_identical( codec_type, LSMASH_CODEC_TYPE_UNSPECIFIED ) )
    {
        err = LSMASH_ERR_PATCH_WELCOME;
        goto fail;
    }
    else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_AV01_VIDEO ) )
        ivf_imp->get_access_unit = av1_get_access_unit;
    lsmash_bs_skip_bytes( bs, gh->header_length );
    /* Parse the first packet to get pixel aspect ratio and color information. */
    if( (err = ivf_importer_get_access_unit( bs, ivf_imp )) < 0 )
        goto fail;
    lsmash_video_summary_t *summary = ivf_create_summary( &ivf_imp->global_header );
    if( !summary )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        err = LSMASH_ERR_NAMELESS;
        goto fail;
    }
    if( (err = lsmash_list_add_entry( importer->summaries, summary )) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
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
    return 1;   /* arbitrary */
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
