/*****************************************************************************
 * amr_imp.c
 *****************************************************************************
 * Copyright (C) 2010-2014 L-SMASH project
 *
 * Authors: Takashi Hirata <silverfilain@gmail.com>
 * Contributors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
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
    AMR-NB/WB storage format importer
    3GPP TS 26.101 V11.0.0 (2012-9)
    3GPP TS 26.201 V11.0.0 (2012-9)
    3GPP TS 26.244 V12.3.0 (2014-03)
    http://www.ietf.org/rfc/rfc3267.txt (Obsoleted)
    http://www.ietf.org/rfc/rfc4867.txt
***************************************************************************/
typedef struct
{
    importer_status status;
    lsmash_bs_t    *bs;
    int             wb; /* 0: AMR-NB, 1: AMR-WB */
    uint32_t        samples_in_frame;
    uint32_t        au_number;
} amr_importer_t;

static void remove_amr_importer
(
    amr_importer_t *amr_imp
)
{
    lsmash_bs_cleanup( amr_imp->bs );
    lsmash_free( amr_imp );
}

static amr_importer_t *create_amr_importer
(
    importer_t *importer
)
{
    amr_importer_t *amr_imp = (amr_importer_t *)lsmash_malloc_zero( sizeof(amr_importer_t) );
    if( !amr_imp )
        return NULL;
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
    {
        lsmash_free( amr_imp );
        return NULL;
    }
    amr_imp->bs = bs;
    bs->stream          = importer->stream;
    bs->read            = lsmash_fread_wrapper;
    bs->seek            = lsmash_fseek_wrapper;
    bs->unseekable      = importer->is_stdin;
    bs->buffer.max_size = BS_MAX_DEFAULT_READ_SIZE;
    return amr_imp;
}

static void amr_cleanup
(
    importer_t *importer
)
{
    debug_if( importer && importer->info )
        remove_amr_importer( importer->info );
}

static int amr_get_accessunit
(
    importer_t      *importer,
    uint32_t         track_number,
    lsmash_sample_t *buffered_sample
)
{
    debug_if( !importer || !importer->info || !buffered_sample->data || !buffered_sample->length )
        return -1;
    if( track_number != 1 )
        return -1;
    amr_importer_t *amr_imp = (amr_importer_t *)importer->info;
    lsmash_bs_t    *bs      = amr_imp->bs;
    if( amr_imp->status == IMPORTER_EOF || lsmash_bs_is_end( bs, 0 ) )
    {
        /* EOF */
        amr_imp->status = IMPORTER_EOF;
        buffered_sample->length = 0;
        return 0;
    }
    /* Each speech frame consists of one speech frame header and one speech data.
     * At the end of each speech data, octet alignment if needed.
     *   Speech frame header
     *      0 1 2 3 4 5 6 7
     *     +-+-------+-+-+-+
     *     |P|  FT   |Q|P|P|
     *     +-+-------+-+-+-+
     *    FT: Frame type index
     *    Q : Frame quality indicator
     *    P : Must be set to 0
     * FT= 9, 10 and 11 for AMR-NB shall not be used in the file format.
     * FT=12, 13 and 14 for AMR-NB are not defined yet in the file format.
     * FT=10, 11, 12 and 13 for AMR-WB are not defined yet in the file format.
     * FT determines the size of the speech frame starting with it.
     */
    uint8_t FT = (lsmash_bs_show_byte( bs, 0 ) >> 3) & 0x0F;
    const int frame_size[2][16] =
    {
        { 13, 14, 16, 18, 20, 21, 27, 32,  6, -1, -1, -1, 0, 0, 0, 1 },
        { 18, 24, 33, 37, 41, 47, 51, 59, 61,  6,  0,  0, 0, 0, 1, 1 }
    };
    int read_size = frame_size[ amr_imp->wb ][FT];
    if( read_size <= 0 )
    {
        lsmash_log( importer, LSMASH_LOG_ERROR, "an %s speech frame is detected.\n", read_size < 0 ? "invalid" : "unknown" );
        amr_imp->status = IMPORTER_ERROR;
        return -1;
    }
    if( buffered_sample->length < read_size )
        return -1;
    if( lsmash_bs_get_bytes_ex( bs, read_size, buffered_sample->data ) != read_size )
    {
        lsmash_log( importer, LSMASH_LOG_WARNING, "the stream is truncated at the end.\n" );
        amr_imp->status = IMPORTER_EOF;
        return -1;
    }
    buffered_sample->length        = read_size;
    buffered_sample->dts           = amr_imp->au_number ++ * amr_imp->samples_in_frame;
    buffered_sample->cts           = buffered_sample->dts;
    buffered_sample->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
    return 0;
}

static int amr_check_magic_number
(
    lsmash_bs_t *bs
)
{
#define AMR_STORAGE_MAGIC_LENGTH  6
#define AMR_AMRWB_EX_MAGIC_LENGTH 3
    /* Check the magic number for single-channel AMR-NB/AMR-WB files.
     *   For AMR-NB, "#!AMR\n" (or 0x2321414d520a in hexadecimal).
     *   For AMR-WB, "#!AMR-WB\n" (or 0x2321414d522d57420a in hexadecimal).
     * Note that AMR-NB and AMR-WB data is stored in the 3GPP/3GPP2 file format according to
     * the AMR-NB and AMR-WB storage format for single channel header without the AMR magic numbers. */
    uint8_t buf[AMR_STORAGE_MAGIC_LENGTH];
    if( lsmash_bs_get_bytes_ex( bs, AMR_STORAGE_MAGIC_LENGTH, buf ) != AMR_STORAGE_MAGIC_LENGTH
     || memcmp( buf, "#!AMR", AMR_STORAGE_MAGIC_LENGTH - 1 ) )
        return -1;
    if( buf[AMR_STORAGE_MAGIC_LENGTH - 1] == '\n' )
        /* single-channel AMR-NB file */
        return 0;
    if( buf[AMR_STORAGE_MAGIC_LENGTH - 1] != '-'
     || lsmash_bs_get_bytes_ex( bs, AMR_AMRWB_EX_MAGIC_LENGTH, buf ) != AMR_AMRWB_EX_MAGIC_LENGTH
     || memcmp( buf, "WB\n", AMR_AMRWB_EX_MAGIC_LENGTH ) )
        return -1;
    /* single-channel AMR-WB file */
    return 1;
#undef AMR_STORAGE_MAGIC_LENGTH
#undef AMR_AMRWB_EX_MAGIC_LENGTH
}

static int amr_create_damr
(
    lsmash_audio_summary_t *summary,
    int                     wb
)
{
#define AMR_DAMR_LENGTH 17
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
        return -1;
    lsmash_bs_put_be32( bs, AMR_DAMR_LENGTH );
    lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_DAMR.fourcc );
    /* NOTE: These are specific to each codec vendor, but we're surely not a vendor.
     *       Using dummy data. */
    lsmash_bs_put_be32( bs, 0x20202020 );           /* vendor */
    lsmash_bs_put_byte( bs, 0 );                    /* decoder_version */
    /* NOTE: Using safe value for these settings, maybe sub-optimal. */
    lsmash_bs_put_be16( bs, wb ? 0xC3FF : 0x81FF ); /* mode_set, represents for all possibly existing and supported frame-types. */
    lsmash_bs_put_byte( bs, 1 );                    /* mode_change_period */
    lsmash_bs_put_byte( bs, 1 );                    /* frames_per_sample */
    lsmash_codec_specific_t *cs = lsmash_malloc_zero( sizeof(lsmash_codec_specific_t) );
    if( !cs )
    {
        lsmash_bs_cleanup( bs );
        return -1;
    }
    cs->type              = LSMASH_CODEC_SPECIFIC_DATA_TYPE_UNKNOWN;
    cs->format            = LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED;
    cs->destruct          = (lsmash_codec_specific_destructor_t)lsmash_free;
    cs->data.unstructured = lsmash_bs_export_data( bs, &cs->size );
    cs->size              = AMR_DAMR_LENGTH;
    lsmash_bs_cleanup( bs );
    if( !cs->data.unstructured
     || lsmash_add_entry( &summary->opaque->list, cs ) < 0 )
    {
        lsmash_destroy_codec_specific_data( cs );
        return -1;
    }
    return 0;
#undef AMR_DAMR_LENGTH
}

static lsmash_audio_summary_t *amr_create_summary
(
    importer_t *importer,
    int         wb
)
{
    /* Establish an audio summary for AMR-NB or AMR-WB stream. */
    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
    if( !summary )
        return NULL;
    summary->sample_type      = wb ? ISOM_CODEC_TYPE_SAWB_AUDIO : ISOM_CODEC_TYPE_SAMR_AUDIO;
    summary->max_au_length    = wb ? 61 : 32;
    summary->aot              = MP4A_AUDIO_OBJECT_TYPE_NULL;    /* no effect */
    summary->frequency        = (8000 << wb);
    summary->channels         = 1;                              /* always single channel */
    summary->sample_size      = 16;
    summary->samples_in_frame = (160 << wb);
    summary->sbr_mode         = MP4A_AAC_SBR_NOT_SPECIFIED;     /* no effect */
    if( amr_create_damr( summary, wb ) < 0
     || lsmash_add_entry( importer->summaries, summary ) < 0 )
    {
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );
        return NULL;
    }
    return summary;
}

static int amr_probe
(
    importer_t *importer
)
{

    amr_importer_t *amr_imp = create_amr_importer( importer );
    if( !amr_imp )
        return -1;
    int wb = amr_check_magic_number( amr_imp->bs );
    if( wb < 0 )
        goto fail;
    lsmash_audio_summary_t *summary = amr_create_summary( importer, wb );
    if( !summary )
        goto fail;
    amr_imp->status           = IMPORTER_OK;
    amr_imp->wb               = wb;
    amr_imp->samples_in_frame = summary->samples_in_frame;
    amr_imp->au_number        = 0;
    importer->info = amr_imp;
    return 0;
fail:
    remove_amr_importer( amr_imp );
    return -1;
}

static uint32_t amr_get_last_delta
(
    importer_t *importer,
    uint32_t    track_number
)
{
    debug_if( !importer || !importer->info )
        return 0;
    amr_importer_t *amr_imp = (amr_importer_t *)importer->info;
    if( !amr_imp || track_number != 1 )
        return 0;
    return amr_imp->samples_in_frame;
}

const importer_functions amr_importer =
{
    { "AMR", offsetof( importer_t, log_level ) },
    1,
    amr_probe,
    amr_get_accessunit,
    amr_get_last_delta,
    amr_cleanup
};
