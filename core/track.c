/*****************************************************************************
 * track.c
 *****************************************************************************
 * Copyright (C) 2010-2017 L-SMASH project
 * Copyright (c) 2026 Yusuke Nakamura
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 * Contributors: Takashi Hirata <silverfilain@gmail.com>
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

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "box.h"
#include "box_default.h"
#include "timeline.h"
#include "movie.h"
#include "stbl.h"
#include "track.h"

#include "codecs/description.h"

/* Language codes */
typedef struct
{
    uint16_t mac_value;
    uint16_t iso_name;
} track_language_t;

static const track_language_t track_languages[] =
{
    {   0, ISOM_LANGUAGE_CODE_ENGLISH },
    {   1, ISOM_LANGUAGE_CODE_FRENCH },
    {   2, ISOM_LANGUAGE_CODE_GERMAN },
    {   3, ISOM_LANGUAGE_CODE_ITALIAN },
    {   4, ISOM_LANGUAGE_CODE_DUTCH_M },
    {   5, ISOM_LANGUAGE_CODE_SWEDISH },
    {   6, ISOM_LANGUAGE_CODE_SPANISH },
    {   7, ISOM_LANGUAGE_CODE_DANISH },
    {   8, ISOM_LANGUAGE_CODE_PORTUGUESE },
    {   9, ISOM_LANGUAGE_CODE_NORWEGIAN },
    {  10, ISOM_LANGUAGE_CODE_HEBREW },
    {  11, ISOM_LANGUAGE_CODE_JAPANESE },
    {  12, ISOM_LANGUAGE_CODE_ARABIC },
    {  13, ISOM_LANGUAGE_CODE_FINNISH },
    {  14, ISOM_LANGUAGE_CODE_GREEK },
    {  15, ISOM_LANGUAGE_CODE_ICELANDIC },
    {  16, ISOM_LANGUAGE_CODE_MALTESE },
    {  17, ISOM_LANGUAGE_CODE_TURKISH },
    {  18, ISOM_LANGUAGE_CODE_CROATIAN },
    {  19, ISOM_LANGUAGE_CODE_CHINESE },
    {  20, ISOM_LANGUAGE_CODE_URDU },
    {  21, ISOM_LANGUAGE_CODE_HINDI },
    {  22, ISOM_LANGUAGE_CODE_THAI },
    {  23, ISOM_LANGUAGE_CODE_KOREAN },
    {  24, ISOM_LANGUAGE_CODE_LITHUANIAN },
    {  25, ISOM_LANGUAGE_CODE_POLISH },
    {  26, ISOM_LANGUAGE_CODE_HUNGARIAN },
    {  27, ISOM_LANGUAGE_CODE_ESTONIAN },
    {  28, ISOM_LANGUAGE_CODE_LATVIAN },
    {  29, ISOM_LANGUAGE_CODE_SAMI },
    {  30, ISOM_LANGUAGE_CODE_FAROESE },
    {  32, ISOM_LANGUAGE_CODE_RUSSIAN },
    {  33, ISOM_LANGUAGE_CODE_CHINESE },
    {  34, ISOM_LANGUAGE_CODE_DUTCH },
    {  35, ISOM_LANGUAGE_CODE_IRISH },
    {  36, ISOM_LANGUAGE_CODE_ALBANIAN },
    {  37, ISOM_LANGUAGE_CODE_ROMANIAN },
    {  38, ISOM_LANGUAGE_CODE_CZECH },
    {  39, ISOM_LANGUAGE_CODE_SLOVAK },
    {  40, ISOM_LANGUAGE_CODE_SLOVENIA },
    {  41, ISOM_LANGUAGE_CODE_YIDDISH },
    {  42, ISOM_LANGUAGE_CODE_SERBIAN },
    {  43, ISOM_LANGUAGE_CODE_MACEDONIAN },
    {  44, ISOM_LANGUAGE_CODE_BULGARIAN },
    {  45, ISOM_LANGUAGE_CODE_UKRAINIAN },
    {  46, ISOM_LANGUAGE_CODE_BELARUSIAN },
    {  47, ISOM_LANGUAGE_CODE_UZBEK },
    {  48, ISOM_LANGUAGE_CODE_KAZAKH },
    {  49, ISOM_LANGUAGE_CODE_AZERBAIJANI },
    {  51, ISOM_LANGUAGE_CODE_ARMENIAN },
    {  52, ISOM_LANGUAGE_CODE_GEORGIAN },
    {  53, ISOM_LANGUAGE_CODE_MOLDAVIAN },
    {  54, ISOM_LANGUAGE_CODE_KIRGHIZ },
    {  55, ISOM_LANGUAGE_CODE_TAJIK },
    {  56, ISOM_LANGUAGE_CODE_TURKMEN },
    {  57, ISOM_LANGUAGE_CODE_MONGOLIAN },
    {  59, ISOM_LANGUAGE_CODE_PASHTO },
    {  60, ISOM_LANGUAGE_CODE_KURDISH },
    {  61, ISOM_LANGUAGE_CODE_KASHMIRI },
    {  62, ISOM_LANGUAGE_CODE_SINDHI },
    {  63, ISOM_LANGUAGE_CODE_TIBETAN },
    {  64, ISOM_LANGUAGE_CODE_NEPALI },
    {  65, ISOM_LANGUAGE_CODE_SANSKRIT },
    {  66, ISOM_LANGUAGE_CODE_MARATHI },
    {  67, ISOM_LANGUAGE_CODE_BENGALI },
    {  68, ISOM_LANGUAGE_CODE_ASSAMESE },
    {  69, ISOM_LANGUAGE_CODE_GUJARATI },
    {  70, ISOM_LANGUAGE_CODE_PUNJABI },
    {  71, ISOM_LANGUAGE_CODE_ORIYA },
    {  72, ISOM_LANGUAGE_CODE_MALAYALAM },
    {  73, ISOM_LANGUAGE_CODE_KANNADA },
    {  74, ISOM_LANGUAGE_CODE_TAMIL },
    {  75, ISOM_LANGUAGE_CODE_TELUGU },
    {  76, ISOM_LANGUAGE_CODE_SINHALESE },
    {  77, ISOM_LANGUAGE_CODE_BURMESE },
    {  78, ISOM_LANGUAGE_CODE_KHMER },
    {  79, ISOM_LANGUAGE_CODE_LAO },
    {  80, ISOM_LANGUAGE_CODE_VIETNAMESE },
    {  81, ISOM_LANGUAGE_CODE_INDONESIAN },
    {  82, ISOM_LANGUAGE_CODE_TAGALOG },
    {  83, ISOM_LANGUAGE_CODE_MALAY_ROMAN },
    {  84, ISOM_LANGUAGE_CODE_MAYAY_ARABIC },
    {  85, ISOM_LANGUAGE_CODE_AMHARIC },
    {  87, ISOM_LANGUAGE_CODE_OROMO },
    {  88, ISOM_LANGUAGE_CODE_SOMALI },
    {  89, ISOM_LANGUAGE_CODE_SWAHILI },
    {  90, ISOM_LANGUAGE_CODE_KINYARWANDA },
    {  91, ISOM_LANGUAGE_CODE_RUNDI },
    {  92, ISOM_LANGUAGE_CODE_CHEWA },
    {  93, ISOM_LANGUAGE_CODE_MALAGASY },
    {  94, ISOM_LANGUAGE_CODE_ESPERANTO },
    { 128, ISOM_LANGUAGE_CODE_WELSH },
    { 129, ISOM_LANGUAGE_CODE_BASQUE },
    { 130, ISOM_LANGUAGE_CODE_CATALAN },
    { 131, ISOM_LANGUAGE_CODE_LATIN },
    { 132, ISOM_LANGUAGE_CODE_QUECHUA },
    { 133, ISOM_LANGUAGE_CODE_GUARANI },
    { 134, ISOM_LANGUAGE_CODE_AYMARA },
    { 135, ISOM_LANGUAGE_CODE_TATAR },
    { 136, ISOM_LANGUAGE_CODE_UIGHUR },
    { 137, ISOM_LANGUAGE_CODE_DZONGKHA },
    { 138, ISOM_LANGUAGE_CODE_JAVANESE },
    { UINT16_MAX, 0 }
};

isom_trak_t *lsm_track_get_trak( lsmash_file_t *file, uint32_t track_ID )
{
    if( track_ID == 0
     || LSMASH_IS_NON_EXISTING_BOX( file->moov )
     || file != file->initializer )
        return isom_non_existing_trak();
    for( lsmash_entry_t *entry = file->moov->trak_list.head; entry; entry = entry->next )
    {
        isom_trak_t *trak = (isom_trak_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( trak )
         || LSMASH_IS_NON_EXISTING_BOX( trak->tkhd ) )
            return isom_non_existing_trak();
        if( trak->tkhd->track_ID == track_ID )
            return trak;
    }
    return isom_non_existing_trak();
}

isom_trex_t *lsm_track_get_trex( isom_mvex_t *mvex, uint32_t track_ID )
{
    if( track_ID == 0 || LSMASH_IS_NON_EXISTING_BOX( mvex ) )
        return isom_non_existing_trex();
    for( lsmash_entry_t *entry = mvex->trex_list.head; entry; entry = entry->next )
    {
        isom_trex_t *trex = (isom_trex_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( trex ) )
            return isom_non_existing_trex();
        if( trex->track_ID == track_ID )
            return trex;
    }
    return isom_non_existing_trex();
}

isom_traf_t *lsm_track_get_traf( isom_moof_t *moof, uint32_t track_ID )
{
    if( track_ID == 0 || LSMASH_IS_NON_EXISTING_BOX( moof ) )
        return isom_non_existing_traf();
    for( lsmash_entry_t *entry = moof->traf_list.head; entry; entry = entry->next )
    {
        isom_traf_t *traf = (isom_traf_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( traf )
         || LSMASH_IS_NON_EXISTING_BOX( traf->tfhd ) )
            return isom_non_existing_traf();
        if( traf->tfhd->track_ID == track_ID )
            return traf;
    }
    return isom_non_existing_traf();
}

isom_tfra_t *lsm_track_get_tfra( isom_mfra_t *mfra, uint32_t track_ID )
{
    if( track_ID == 0 || LSMASH_IS_NON_EXISTING_BOX( mfra ) )
        return isom_non_existing_tfra();
    for( lsmash_entry_t *entry = mfra->tfra_list.head; entry; entry = entry->next )
    {
        isom_tfra_t *tfra = (isom_tfra_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( tfra ) )
            return isom_non_existing_tfra();
        if( tfra->track_ID == track_ID )
            return tfra;
    }
    return isom_non_existing_tfra();
}

static int track_add_elst_entry( isom_elst_t *elst, uint64_t segment_duration, int64_t media_time, int32_t media_rate )
{
    assert( LSMASH_IS_EXISTING_BOX( elst->file ) );
    isom_elst_entry_t *data = (isom_elst_entry_t *)lsm_list_append( elst->list );
    if( !data )
        return LSMASH_ERR_MEMORY_ALLOC;
    data->segment_duration = segment_duration;
    data->media_time       = media_time;
    data->media_rate       = media_rate;
    if( !elst->file->undefined_64_ver
     && (data->segment_duration > UINT32_MAX
      || data->media_time       >  INT32_MAX
      || data->media_time       <  INT32_MIN) )
        elst->version = 1;
    return 0;
}

/* This function returns 0 if failed, sample_entry_number if succeeded. */
int lsmash_add_sample_entry( lsmash_root_t *root, uint32_t track_ID, void *summary )
{
    if( LSMASH_IS_NON_EXISTING_BOX( root ) || !summary
     || ((lsmash_summary_t *)summary)->data_ref_index == 0
     || ((lsmash_summary_t *)summary)->data_ref_index > UINT16_MAX )
        return 0;
    isom_trak_t *trak = lsm_track_get_trak( root->file, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak )
     || LSMASH_IS_NON_EXISTING_BOX( trak->file )
     || LSMASH_IS_NON_EXISTING_BOX( trak->mdia->hdlr )
     || LSMASH_IS_NON_EXISTING_BOX( trak->mdia->minf->stbl->stsd ) )
        return 0;
    isom_stsd_t      *stsd       = trak->mdia->minf->stbl->stsd;
    lsmash_media_type media_type = trak->mdia->hdlr->componentSubtype;
    if( isom_setup_sample_description( stsd, media_type, (lsmash_summary_t *)summary ) < 0 )
        return 0;
    else
        return stsd->list.entry_count;
}

static int track_update_mdhd_duration( isom_trak_t *trak, uint32_t last_sample_delta )
{
    assert( LSMASH_IS_EXISTING_BOX( trak ) );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->file )
     || LSMASH_IS_NON_EXISTING_BOX( trak->mdia->mdhd )
     || !trak->cache
     || !trak->mdia->minf->stbl->stts->list )
        return LSMASH_ERR_INVALID_DATA;
    lsmash_file_t *file = trak->file;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_stts_t *stts = stbl->stts;
    isom_ctts_t *ctts = stbl->ctts;
    isom_cslg_t *cslg = stbl->cslg;
    mdhd->duration = 0;
    uint32_t sample_count = lsm_stbl_get_sample_count( trak );
    if( sample_count == 0 )
    {
        /* Return error if non-fragmented movie has no samples. */
        if( !file->fragment && !lsm_list_count( stts->list ) )
            return LSMASH_ERR_INVALID_DATA;
        return 0;
    }
    /* Now we have at least 1 sample, so do stts_entry. */
    isom_stts_entry_t *last_stts_data = (isom_stts_entry_t *)lsm_list_tail( stts->list );
    if( !last_stts_data )
        return LSMASH_ERR_INVALID_DATA;
    if( sample_count == 1 )
        mdhd->duration = last_stts_data->sample_delta;
    /* Now we have at least 2 samples,
     * but dunno whether 1 stts_entry which has 2 samples or 2 stts_entry which has 1 samle each. */
    else if( LSMASH_IS_NON_EXISTING_BOX( ctts ) )
    {
        /* use dts instead of cts */
        mdhd->duration = lsm_stbl_get_dts( stts, sample_count );
        int err;
        if( last_sample_delta )
        {
            mdhd->duration += last_sample_delta;
            if( (err = lsm_stbl_replace_last_sample_delta( stbl, last_sample_delta )) < 0 )
                return err;
        }
        else if( last_stts_data->sample_count > 1 )
            mdhd->duration += last_stts_data->sample_delta; /* no need to update last_stts_data->sample_delta */
        else
        {
            /* Remove the last entry. */
            if( (err = lsm_list_remove_tail( stts->list )) < 0 )
                return err;
            /* copy the previous sample_delta. */
            last_stts_data = (isom_stts_entry_t *)lsm_list_tail( stts->list );
            if( !last_stts_data )
                return LSMASH_ERR_INVALID_DATA;
            ++ last_stts_data->sample_count;
            mdhd->duration += last_stts_data->sample_delta;
        }
    }
    else
    {
        if( !lsm_list_count( ctts->list ) )
            return LSMASH_ERR_INVALID_DATA;
        uint64_t dts        = 0;
        uint64_t max_cts    = 0;
        uint64_t max2_cts   = 0;
        uint64_t min_cts    = LSMASH_TIMESTAMP_UNDEFINED;
        int64_t  max_offset = 0;
        int64_t  min_offset = UINT32_MAX;
        int32_t  ctd_shift  = trak->cache->timestamp.ctd_shift;
        uint32_t j = 0;
        uint32_t k = 0;
        uint32_t stts_idx = 0;
        uint32_t ctts_idx = 0;
        for( uint32_t i = 0; i < sample_count; i++ )
        {
            isom_stts_entry_t *stts_data = (isom_stts_entry_t *)lsm_list_at( stts->list, stts_idx );
            isom_ctts_entry_t *ctts_data = (isom_ctts_entry_t *)lsm_list_at( ctts->list, ctts_idx );
            if( !stts_data || !ctts_data )
                return LSMASH_ERR_INVALID_DATA;
            if( ctts_data->sample_offset != ISOM_NON_OUTPUT_SAMPLE_OFFSET )
            {
                uint64_t cts;
                if( ctd_shift )
                {
                    /* Anyway, add composition to decode timeline shift for calculating maximum and minimum CTS correctly. */
                    int64_t sample_offset = (int32_t)ctts_data->sample_offset;
                    cts = dts + sample_offset + ctd_shift;
                    max_offset = LSMASH_MAX( max_offset, sample_offset );
                    min_offset = LSMASH_MIN( min_offset, sample_offset );
                }
                else
                {
                    cts = dts + ctts_data->sample_offset;
                    max_offset = LSMASH_MAX( max_offset, ctts_data->sample_offset );
                    min_offset = LSMASH_MIN( min_offset, ctts_data->sample_offset );
                }
                min_cts = LSMASH_MIN( min_cts, cts );
                if( max_cts < cts )
                {
                    max2_cts = max_cts;
                    max_cts  = cts;
                }
                else if( max2_cts < cts )
                    max2_cts = cts;
            }
            dts += stts_data->sample_delta;
            /* If finished sample_count of current entry, move to next. */
            if( ++j == ctts_data->sample_count )
            {
                ++ctts_idx;
                j = 0;
            }
            if( ++k == stts_data->sample_count )
            {
                ++stts_idx;
                k = 0;
            }
        }
        dts -= last_stts_data->sample_delta;
        if( file->fragment )
            /* Overall presentation is extended exceeding this initial movie.
             * So, any players shall display the movie exceeding the durations
             * indicated in Movie Header Box, Track Header Boxes and Media Header Boxes.
             * Samples up to the duration indicated in Movie Extends Header Box shall be displayed.
             * In the absence of Movie Extends Header Box, all samples shall be displayed. */
            mdhd->duration += dts + last_sample_delta;
        else
        {
            if( !last_sample_delta )
            {
                /* The spec allows an arbitrary value for the duration of the last sample. So, we pick last-1 sample's. */
                last_sample_delta = max_cts - max2_cts;
            }
            if( min_cts != LSMASH_TIMESTAMP_UNDEFINED )
                mdhd->duration = max_cts - min_cts + last_sample_delta;
            /* To match dts and media duration, update stts and mdhd relatively. */
            if( mdhd->duration > dts )
                last_sample_delta = mdhd->duration - dts;
            else
                mdhd->duration = dts + last_sample_delta;   /* media duration must not less than last dts. */
        }
        int err = lsm_stbl_replace_last_sample_delta( stbl, last_sample_delta );
        if( err < 0 )
            return err;
        /* Explicit composition information and timeline shifting  */
        if( LSMASH_IS_EXISTING_BOX( cslg ) || file->qt_compatible || file->max_isom_version >= 4 )
        {
            if( ctd_shift )
            {
                /* Remove composition to decode timeline shift. */
                max_cts  -= ctd_shift;
                max2_cts -= ctd_shift;
                min_cts  -= ctd_shift;
            }
            int64_t composition_end_time = max_cts + (max_cts - max2_cts);
            if( !file->fragment
             && min_cts != LSMASH_TIMESTAMP_UNDEFINED
             && (min_offset <= INT32_MAX) && (min_offset >= INT32_MIN)
             && (max_offset <= INT32_MAX) && (max_offset >= INT32_MIN)
             && ((int64_t)min_cts <= INT32_MAX) && (composition_end_time <= INT32_MAX) )
            {
                if( LSMASH_IS_NON_EXISTING_BOX( cslg ) )
                {
                    if( LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_cslg( trak->mdia->minf->stbl ) ) )
                        return LSMASH_ERR_NAMELESS;
                    cslg = stbl->cslg;
                }
                cslg->compositionToDTSShift        = ctd_shift;
                cslg->leastDecodeToDisplayDelta    = min_offset;
                cslg->greatestDecodeToDisplayDelta = max_offset;
                cslg->compositionStartTime         = min_cts;
                cslg->compositionEndTime           = composition_end_time;
            }
            else
                isom_remove_box_by_itself( cslg );
        }
    }
    if( mdhd->duration > UINT32_MAX && !file->undefined_64_ver )
        mdhd->version = 1;
    return 0;
}

int lsm_track_update_tkhd_duration( isom_trak_t *trak )
{
    assert( LSMASH_IS_EXISTING_BOX( trak ) );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->tkhd )
     || LSMASH_IS_NON_EXISTING_BOX( trak->file->moov->mvhd ) )
        return LSMASH_ERR_INVALID_DATA;
    lsmash_file_t *file = trak->file;
    isom_tkhd_t   *tkhd = trak->tkhd;
    tkhd->duration = 0;
    if( file->fragment
     || LSMASH_IS_NON_EXISTING_BOX( trak->edts->elst ) )
    {
        /* If this presentation might be extended or this track doesn't have edit list, calculate track duration from media duration. */
        if( LSMASH_IS_NON_EXISTING_BOX( trak->mdia->mdhd )
         || trak->mdia->mdhd->timescale == 0 )
            return LSMASH_ERR_INVALID_DATA;
        if( trak->mdia->mdhd->duration == 0 )
        {
            int err = track_update_mdhd_duration( trak, 0 );
            if( err < 0 )
                return err;
        }
        tkhd->duration = trak->mdia->mdhd->duration * ((double)file->moov->mvhd->timescale / trak->mdia->mdhd->timescale);
    }
    else
    {
        /* If the presentation won't be extended and this track has any edit, then track duration is just the sum of the segment_duartions. */
        isom_elst_entry_t *data;
        LSM_LIST_FOR_EACH( data, trak->edts->elst->list )
        {
            if( !data )
                return LSMASH_ERR_INVALID_DATA;
            tkhd->duration += data->segment_duration;
        }
    }
    if( tkhd->duration > UINT32_MAX && !file->undefined_64_ver )
        tkhd->version = 1;
    if( !file->fragment && tkhd->duration == 0 )
        tkhd->duration = tkhd->version == 1 ? 0xffffffffffffffff : 0xffffffff;
    return lsm_movie_update_mvhd_duration( file->moov );
}

int lsmash_update_track_duration( lsmash_root_t *root, uint32_t track_ID, uint32_t last_sample_delta )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file;
    isom_trak_t   *trak = lsm_track_get_trak( file, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak ) )
        return LSMASH_ERR_NAMELESS;
    int err = track_update_mdhd_duration( trak, last_sample_delta );
    if( err < 0 )
        return err;
    /* If the presentation won't be extended and this track has any edit, we don't change or update duration in tkhd. */
    if( !file->fragment
     && LSMASH_IS_EXISTING_BOX( trak->edts )
     && LSMASH_IS_EXISTING_BOX( trak->edts->elst ) )
        err = lsm_movie_update_mvhd_duration( file->moov );  /* Only update movie duration. */
    else
        err = lsm_track_update_tkhd_duration( trak );        /* Also update movie duration internally. */
    return err;
}

static inline uint64_t track_get_current_mp4time( void )
{
    return (uint64_t)time( NULL ) + ISOM_MAC_EPOCH_OFFSET;
}

static int track_set_media_creation_time( isom_trak_t *trak, uint64_t current_mp4time )
{
    if( LSMASH_IS_NON_EXISTING_BOX( trak->mdia->mdhd ) )
        return LSMASH_ERR_NAMELESS;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    if( mdhd->creation_time == 0 )
        mdhd->creation_time = mdhd->modification_time = current_mp4time;
    return 0;
}

int lsm_track_set_creation_time( isom_trak_t *trak, uint64_t current_mp4time )
{
    assert( LSMASH_IS_EXISTING_BOX( trak ) );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->tkhd ) )
        return LSMASH_ERR_NAMELESS;
    isom_tkhd_t *tkhd = trak->tkhd;
    if( tkhd->creation_time == 0 )
        tkhd->creation_time = tkhd->modification_time = current_mp4time;
    return track_set_media_creation_time( trak, current_mp4time );
}

int lsm_track_setup_handler_reference( isom_hdlr_t *hdlr, uint32_t media_type )
{
    assert( LSMASH_IS_EXISTING_BOX( hdlr ) );
    isom_box_t    *parent = hdlr->parent;
    lsmash_file_t *file   = hdlr->file;
    if( LSMASH_IS_NON_EXISTING_BOX( parent )
     || LSMASH_IS_NON_EXISTING_BOX( file ) )
        return LSMASH_ERR_NAMELESS;
    isom_mdia_t *mdia = lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MDIA ) ? (isom_mdia_t *)parent : isom_non_existing_mdia();
    isom_meta_t *meta = lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_META ) ? (isom_meta_t *)parent
                      : lsmash_check_box_type_identical( parent->type,   QT_BOX_TYPE_META ) ? (isom_meta_t *)parent : isom_non_existing_meta();
    uint32_t type    = LSMASH_IS_EXISTING_BOX( mdia ) ? (file->qt_compatible ? QT_HANDLER_TYPE_MEDIA : 0)
                                                      : (LSMASH_IS_EXISTING_BOX( meta ) ? 0 : QT_HANDLER_TYPE_DATA);
    uint32_t subtype = media_type;
    hdlr->componentType    = type;
    hdlr->componentSubtype = subtype;
    char *type_name    = NULL;
    char *subtype_name = NULL;
    uint8_t type_name_length    = 0;
    uint8_t subtype_name_length = 0;
    if( mdia )
        type_name = "Media ";
    else if( meta )
        type_name = "Metadata ";
    else    /* if( minf ) */
        type_name = "Data ";
    type_name_length = strlen( type_name );
    struct
    {
        uint32_t subtype;
        char    *subtype_name;
        uint8_t  subtype_name_length;
    } subtype_table[] =
        {
            { ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK,          "Sound ",    6 },
            { ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK,          "Video ",    6 },
            { ISOM_MEDIA_HANDLER_TYPE_HINT_TRACK,           "Hint ",     5 },
            { ISOM_MEDIA_HANDLER_TYPE_TIMED_METADATA_TRACK, "Metadata ", 9 },
            { ISOM_MEDIA_HANDLER_TYPE_TEXT_TRACK,           "Text ",     5 },
            { ISOM_META_HANDLER_TYPE_ITUNES_METADATA,       "iTunes ",   7 },
            { QT_REFERENCE_HANDLER_TYPE_ALIAS,              "Alias ",    6 },
            { QT_REFERENCE_HANDLER_TYPE_RESOURCE,           "Resource ", 9 },
            { QT_REFERENCE_HANDLER_TYPE_URL,                "URL ",      4 },
            { subtype,                                      "Unknown ",  8 }
        };
    for( int i = 0; subtype_table[i].subtype; i++ )
        if( subtype == subtype_table[i].subtype )
        {
            subtype_name        = subtype_table[i].subtype_name;
            subtype_name_length = subtype_table[i].subtype_name_length;
            break;
        }
    uint32_t name_length = 15 + subtype_name_length + type_name_length + file->isom_compatible + file->qt_compatible;
    uint8_t *name = lsmash_malloc( name_length );
    if( !name )
        return LSMASH_ERR_MEMORY_ALLOC;
    if( file->qt_compatible )
        name[0] = name_length & 0xff;
    memcpy( name + file->qt_compatible, "L-SMASH ", 8 );
    memcpy( name + file->qt_compatible + 8, subtype_name, subtype_name_length );
    memcpy( name + file->qt_compatible + 8 + subtype_name_length, type_name, type_name_length );
    memcpy( name + file->qt_compatible + 8 + subtype_name_length + type_name_length, "Handler", 7 );
    if( file->isom_compatible )
        name[name_length - 1] = 0;
    hdlr->componentName        = name;
    hdlr->componentName_length = name_length;
    return 0;
}

isom_trak_t *lsm_track_create( lsmash_file_t *file, lsmash_media_type media_type )
{
    /* Don't allow to create a new track if the initial movie is already written. */
    if( (file->fragment && file->fragment->movie)
     || (LSMASH_IS_EXISTING_BOX( file->moov ) && (file->moov->manager & LSMASH_WRITTEN_BOX)) )
        return isom_non_existing_trak();
    isom_trak_t *trak = isom_add_trak( file->moov );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->file->moov->mvhd ) )
        goto fail;
    if( LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_tkhd( trak ) )
     || LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_mdia( trak ) )
     || LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_mdhd( trak->mdia ) )
     || LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_minf( trak->mdia ) )
     || LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_dinf( trak->mdia->minf ) )
     || LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_dref( trak->mdia->minf->dinf ) )
     || LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_stbl( trak->mdia->minf ) )
     || LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_stsd( trak->mdia->minf->stbl ) )
     || LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_stts( trak->mdia->minf->stbl ) )
     || LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_stsc( trak->mdia->minf->stbl ) )
     || LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_stco( trak->mdia->minf->stbl ) )
     || LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_stsz( trak->mdia->minf->stbl ) ) )
        goto fail;
    if( LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_hdlr( trak->mdia ) )
     || lsm_track_setup_handler_reference( trak->mdia->hdlr, media_type ) < 0 )
        goto fail;
    if( file->qt_compatible )
    {
        if( LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_hdlr( trak->mdia->minf ) )
         || lsm_track_setup_handler_reference( trak->mdia->minf->hdlr, QT_REFERENCE_HANDLER_TYPE_URL ) < 0 )
            goto fail;
    }
    switch( media_type )
    {
        case ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK :
            if( LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_vmhd( trak->mdia->minf ) ) )
                goto fail;
            trak->mdia->minf->vmhd->flags = 0x000001;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK :
            if( LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_smhd( trak->mdia->minf ) ) )
                goto fail;
            trak->cache->is_audio = 1;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_HINT_TRACK :
            if( LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_hmhd( trak->mdia->minf ) ) )
                goto fail;
            trak->mdia->minf->hmhd->combinedPDUsize = 0;
            trak->mdia->minf->hmhd->PDUcount        = 0;
            trak->mdia->minf->hmhd->maxPDUsize      = 0;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_TEXT_TRACK :
            if( file->qt_compatible || file->itunes_movie )
            {
                if( LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_gmhd( trak->mdia->minf ) )
                 || LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_gmin( trak->mdia->minf->gmhd ) )
                 || LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_text( trak->mdia->minf->gmhd ) ) )
                    return 0;
                /* Default Text Media Information Box. */
                {
                    isom_text_t *text = trak->mdia->minf->gmhd->text;
                    text->matrix[0] = 0x00010000;
                    text->matrix[4] = 0x00010000;
                    text->matrix[8] = 0x40000000;
                }
            }
            else
                goto fail;  /* We support only reference text media track for chapter yet. */
            break;
        default :
            if( LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_nmhd( trak->mdia->minf ) ) )
                goto fail;
            break;
    }
    /* Default Track Header Box. */
    {
        isom_tkhd_t *tkhd = trak->tkhd;
        if( media_type == ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK )
            tkhd->volume = 0x0100;
        tkhd->matrix[0] = 0x00010000;
        tkhd->matrix[4] = 0x00010000;
        tkhd->matrix[8] = 0x40000000;
        tkhd->duration  = 0xffffffff;
        tkhd->track_ID  = trak->file->moov->mvhd->next_track_ID ++;
    }
    trak->mdia->mdhd->language = file->qt_compatible ? 0 : ISOM_LANGUAGE_CODE_UNDEFINED;
    return trak;
fail:
    isom_remove_box_by_itself( trak );
    return isom_non_existing_trak();
}

/*******************************
    public interfaces
*******************************/

/*---- track manipulators ----*/

void lsmash_delete_track( lsmash_root_t *root, uint32_t track_ID )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return;
    for( lsmash_entry_t *entry = root->file->initializer->moov->trak_list.head; entry; entry = entry->next )
    {
        isom_trak_t *trak = (isom_trak_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( trak )
         || LSMASH_IS_NON_EXISTING_BOX( trak->tkhd ) )
            return;
        if( trak->tkhd->track_ID == track_ID )
        {
            isom_remove_box_by_itself( trak );
            return;
        }
    }
}

uint32_t lsmash_create_track( lsmash_root_t *root, lsmash_media_type media_type )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return 0;
    isom_trak_t *trak = lsm_track_create( root->file, media_type );
    if( LSMASH_IS_NON_EXISTING_BOX( trak )
     || LSMASH_IS_NON_EXISTING_BOX( trak->tkhd ) )
        return 0;
    return trak->tkhd->track_ID;
}

uint32_t lsmash_get_track_ID( lsmash_root_t *root, uint32_t track_number )
{
    if( lsm_movie_check_initializer_present( root ) < 0
     || LSMASH_IS_NON_EXISTING_BOX( root->file->initializer->moov ) )
        return 0;
    isom_trak_t *trak = (isom_trak_t *)lsmash_list_get_entry_data( &root->file->initializer->moov->trak_list, track_number );
    if( LSMASH_IS_NON_EXISTING_BOX( trak )
     || LSMASH_IS_NON_EXISTING_BOX( trak->tkhd ) )
        return 0;
    return trak->tkhd->track_ID;
}

void lsmash_initialize_track_parameters( lsmash_track_parameters_t *param )
{
    memset( param, 0, sizeof(lsmash_track_parameters_t) );
    param->audio_volume = 0x0100;
    param->matrix[0]    = 0x00010000;
    param->matrix[4]    = 0x00010000;
    param->matrix[8]    = 0x40000000;
}

int lsmash_set_track_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_track_parameters_t *param )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file;
    isom_trak_t   *trak = lsm_track_get_trak( file, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->tkhd )
     || LSMASH_IS_NON_EXISTING_BOX( trak->mdia->hdlr )
     || LSMASH_IS_NON_EXISTING_BOX( file->moov->mvhd ) )
        return LSMASH_ERR_NAMELESS;
    /* Prepare Track Aperture Modes if required. */
    if( file->qt_compatible && param->aperture_modes )
    {
        if( LSMASH_IS_NON_EXISTING_BOX( trak->tapt ) && LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_tapt( trak ) ) )
            return LSMASH_ERR_NAMELESS;
        isom_tapt_t *tapt = trak->tapt;
        if( (LSMASH_IS_NON_EXISTING_BOX( tapt->clef ) && LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_clef( tapt ) ))
         || (LSMASH_IS_NON_EXISTING_BOX( tapt->prof ) && LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_prof( tapt ) ))
         || (LSMASH_IS_NON_EXISTING_BOX( tapt->enof ) && LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_enof( tapt ) )) )
            return LSMASH_ERR_NAMELESS;
    }
    else
        isom_remove_box_by_itself( trak->tapt );
    /* Set up Track Header. */
    uint32_t media_type = trak->mdia->hdlr->componentSubtype;
    isom_tkhd_t *tkhd = trak->tkhd;
    tkhd->flags    = param->mode;
    tkhd->track_ID = param->track_ID ? param->track_ID : tkhd->track_ID;
    tkhd->duration = LSMASH_IS_NON_EXISTING_BOX( trak->edts->elst ) ? param->duration : tkhd->duration;
    /* Template fields
     *   alternate_group, layer, volume and matrix
     * According to 14496-14, these value are all set to defaut values in 14496-12.
     * And when a file is read as an MPEG-4 file, these values shall be ignored.
     * If a file complies with other specifications, then those fields may have non-default values
     * as required by those other specifications. */
    if( param->alternate_group )
    {
        if( file->qt_compatible || file->itunes_movie || file->max_3gpp_version >= 4 )
            tkhd->alternate_group = param->alternate_group;
        else
        {
            tkhd->alternate_group = 0;
            lsmash_log( NULL, LSMASH_LOG_WARNING,
                        "alternate_group is specified but not compatible with any of the brands. It won't be set.\n" );
        }
    }
    else
        tkhd->alternate_group = 0;
    if( file->qt_compatible || file->itunes_movie )
    {
        tkhd->layer  = media_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK ? param->video_layer  : 0;
        tkhd->volume = media_type == ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK ? param->audio_volume : 0;
        if( media_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK )
            for( int i = 0; i < 9; i++ )
                tkhd->matrix[i] = param->matrix[i];
        else
            for( int i = 0; i < 9; i++ )
                tkhd->matrix[i] = 0;
    }
    else
    {
        tkhd->layer     = 0;
        tkhd->volume    = media_type == ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK ? 0x0100 : 0;
        tkhd->matrix[0] = 0x00010000;
        tkhd->matrix[1] = 0;
        tkhd->matrix[2] = 0;
        tkhd->matrix[3] = 0;
        tkhd->matrix[4] = 0x00010000;
        tkhd->matrix[5] = 0;
        tkhd->matrix[6] = 0;
        tkhd->matrix[7] = 0;
        tkhd->matrix[8] = 0x40000000;
    }
    /* visual presentation size */
    tkhd->width  = media_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK ? param->display_width  : 0;
    tkhd->height = media_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK ? param->display_height : 0;
    /* Update next_track_ID if needed. */
    if( file->moov->mvhd->next_track_ID <= tkhd->track_ID )
        file->moov->mvhd->next_track_ID = tkhd->track_ID + 1;
    return 0;
}

int lsmash_get_track_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_track_parameters_t *param )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    isom_trak_t *trak = lsm_track_get_trak( root->file->initializer, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->tkhd ) )
        return LSMASH_ERR_NAMELESS;
    isom_tkhd_t *tkhd = trak->tkhd;
    param->mode            = tkhd->flags;
    param->track_ID        = tkhd->track_ID;
    param->duration        = tkhd->duration;
    param->video_layer     = tkhd->layer;
    param->alternate_group = tkhd->alternate_group;
    param->audio_volume    = tkhd->volume;
    for( int i = 0; i < 9; i++ )
        param->matrix[i]   = tkhd->matrix[i];
    param->display_width   = tkhd->width;
    param->display_height  = tkhd->height;
    param->aperture_modes  = LSMASH_IS_EXISTING_BOX( trak->tapt );
    return 0;
}

static inline int track_check_dref_presence( isom_trak_t *trak )
{
    if( LSMASH_IS_NON_EXISTING_BOX( trak->mdia->minf->dinf->dref ) )
        return LSMASH_ERR_NAMELESS;
    return 0;
}

uint32_t lsmash_count_data_reference
(
    lsmash_root_t *root,
    uint32_t       track_ID
)
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return 0;
    isom_trak_t *trak = lsm_track_get_trak( root->file->initializer, track_ID );
    if( track_check_dref_presence( trak ) < 0 )
        return 0;
    return trak->mdia->minf->dinf->dref->list.entry_count;
}

int lsmash_get_data_reference
(
    lsmash_root_t           *root,
    uint32_t                 track_ID,
    lsmash_data_reference_t *data_ref
)
{
    if( lsm_movie_check_initializer_present( root ) < 0 || !data_ref )
        return LSMASH_ERR_FUNCTION_PARAM;
    isom_trak_t *trak = lsm_track_get_trak( root->file->initializer, track_ID );
    if( track_check_dref_presence( trak ) < 0 )
        return LSMASH_ERR_NAMELESS;
    isom_dref_entry_t *url = lsmash_list_get_entry_data( &trak->mdia->minf->dinf->dref->list, data_ref->index );
    if( LSMASH_IS_NON_EXISTING_BOX( url ) )
        return LSMASH_ERR_NAMELESS;
    if( !(url->flags & 0x000001) && url->location )
    {
        int length = strlen( url->location );
        char *location = lsmash_malloc( length + 1 );
        if( !location )
            return LSMASH_ERR_MEMORY_ALLOC;
        memcpy( location, url->location, length );
        location[length] = '\0';
        data_ref->location = location;
    }
    else
        data_ref->location = NULL;
    return 0;
}

void lsmash_cleanup_data_reference
(
    lsmash_data_reference_t *data_ref
)
{
    if( !data_ref )
        return;
    lsmash_freep( &data_ref->location );
}

int lsmash_create_data_reference
(
    lsmash_root_t           *root,
    uint32_t                 track_ID,
    lsmash_data_reference_t *data_ref,
    lsmash_file_t           *file
)
{
    /* At present, we don't support external data references for movie fragments.
     * Note that, for external media data, default-base-is-moof is meaningless since relative
     * offsets from Movie Fragment Boxes make no sense.
     * In the future, the condition of !(file->flags & LSMASH_FILE_MODE_WRITE) may be removed
     * for the implementation which does not write actually and does reference read-only file. */
    if( LSMASH_IS_NON_EXISTING_BOX( root )
     || LSMASH_IS_NON_EXISTING_BOX( file )
     || file->root != root
     || (!(file->flags & LSMASH_FILE_MODE_MEDIA) && !(file->flags & LSMASH_FILE_MODE_INITIALIZATION))
     || !(file->flags & LSMASH_FILE_MODE_WRITE)
     || (root->file != file && ((file->flags & LSMASH_FILE_MODE_FRAGMENTED) || file->fragment))
     || !data_ref )
        return LSMASH_ERR_FUNCTION_PARAM;
    isom_trak_t *trak = lsm_track_get_trak( root->file, track_ID );
    if( track_check_dref_presence( trak ) < 0 )
        return LSMASH_ERR_NAMELESS;
    isom_dref_entry_t *url = isom_add_dref_entry( trak->mdia->minf->dinf->dref, ISOM_BOX_TYPE_URL );
    if( LSMASH_IS_NON_EXISTING_BOX( url ) )
        return LSMASH_ERR_NAMELESS;
    if( !data_ref->location || root->file == file )
    {
        /* Media data is in the same file. */
        url->flags    = 0x000001;
        url->ref_file = root->file;
    }
    else
    {
        /* Set the location of the file. */
        int length = strlen( data_ref->location );
        url->location = lsmash_malloc( length + 1 );
        if( !url->location )
        {
            isom_remove_box_by_itself( url );
            return LSMASH_ERR_MEMORY_ALLOC;
        }
        memcpy( url->location, data_ref->location, length );
        url->location[length] = '\0';
        url->location_length  = length + 1;
        url->ref_file         = file;
    }
    data_ref->index = trak->mdia->minf->dinf->dref->list.entry_count;
    return 0;
}

int lsmash_assign_data_reference
(
    lsmash_root_t *root,
    uint32_t       track_ID,
    uint32_t       data_ref_index,
    lsmash_file_t *file
)
{
    if( lsm_movie_check_initializer_present( root ) < 0
     || !file || file->root != root
     || !(file->flags & LSMASH_FILE_MODE_MEDIA)
     || !(file->flags & LSMASH_FILE_MODE_READ)
     || data_ref_index == 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    isom_trak_t *trak = lsm_track_get_trak( root->file->initializer, track_ID );
    if( track_check_dref_presence( trak ) < 0 )
        return LSMASH_ERR_NAMELESS;
    isom_dref_entry_t *url = (isom_dref_entry_t *)lsmash_list_get_entry_data( &trak->mdia->minf->dinf->dref->list, data_ref_index );
    if( LSMASH_IS_NON_EXISTING_BOX( url ) )
        return LSMASH_ERR_NAMELESS;
    if( !(url->flags & 0x000001) )
        /* Reference an external media data. */
        url->ref_file = file;
    return 0;
}

static int track_set_media_handler_name( lsmash_file_t *file, uint32_t track_ID, char *handler_name )
{
    isom_trak_t *trak = lsm_track_get_trak( file, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->mdia->hdlr ) )
        return LSMASH_ERR_NAMELESS;
    isom_hdlr_t *hdlr = trak->mdia->hdlr;
    uint8_t *name        = NULL;
    uint32_t name_length = strlen( handler_name ) + file->isom_compatible + file->qt_compatible;
    if( file->qt_compatible )
        name_length = LSMASH_MIN( name_length, 255 );
    if( name_length > hdlr->componentName_length && hdlr->componentName )
        name = lsmash_realloc( hdlr->componentName, name_length );
    else if( !hdlr->componentName )
        name = lsmash_malloc( name_length );
    else
        name = hdlr->componentName;
    if( !name )
        return LSMASH_ERR_MEMORY_ALLOC;
    if( file->qt_compatible )
        name[0] = name_length & 0xff;
    memcpy( name + file->qt_compatible, handler_name, strlen( handler_name ) );
    if( file->isom_compatible )
        name[name_length - 1] = 0;
    hdlr->componentName        = name;
    hdlr->componentName_length = name_length;
    return 0;
}

static int track_set_data_handler_name( lsmash_file_t *file, uint32_t track_ID, char *handler_name )
{
    isom_trak_t *trak = lsm_track_get_trak( file, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->mdia->minf->hdlr ) )
        return LSMASH_ERR_NAMELESS;
    isom_hdlr_t *hdlr = trak->mdia->minf->hdlr;
    uint8_t *name        = NULL;
    uint32_t name_length = strlen( handler_name ) + file->isom_compatible + file->qt_compatible;
    if( file->qt_compatible )
        name_length = LSMASH_MIN( name_length, 255 );
    if( name_length > hdlr->componentName_length && hdlr->componentName )
        name = lsmash_realloc( hdlr->componentName, name_length );
    else if( !hdlr->componentName )
        name = lsmash_malloc( name_length );
    else
        name = hdlr->componentName;
    if( !name )
        return LSMASH_ERR_MEMORY_ALLOC;
    if( file->qt_compatible )
        name[0] = name_length & 0xff;
    memcpy( name + file->qt_compatible, handler_name, strlen( handler_name ) );
    if( file->isom_compatible )
        name[name_length - 1] = 0;
    hdlr->componentName        = name;
    hdlr->componentName_length = name_length;
    return 0;
}

uint32_t lsmash_get_media_timescale( lsmash_root_t *root, uint32_t track_ID )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return 0;
    isom_trak_t *trak = lsm_track_get_trak( root->file->initializer, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->mdia->mdhd ) )
        return 0;
    return trak->mdia->mdhd->timescale;
}

uint64_t lsmash_get_media_duration( lsmash_root_t *root, uint32_t track_ID )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return 0;
    isom_trak_t *trak = lsm_track_get_trak( root->file->initializer, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->mdia->mdhd ) )
        return 0;
    return trak->mdia->mdhd->duration;
}

uint64_t lsmash_get_track_duration( lsmash_root_t *root, uint32_t track_ID )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return 0;
    isom_trak_t *trak = lsm_track_get_trak( root->file->initializer, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->tkhd ) )
        return 0;
    return trak->tkhd->duration;
}

uint32_t lsmash_get_last_sample_delta( lsmash_root_t *root, uint32_t track_ID )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return 0;
    isom_trak_t *trak = lsm_track_get_trak( root->file, track_ID );
    isom_stts_entry_t *last_stts = (isom_stts_entry_t *)lsm_list_tail( trak->mdia->minf->stbl->stts->list );
    return last_stts ? last_stts->sample_delta : 0;
}

uint32_t lsmash_get_start_time_offset( lsmash_root_t *root, uint32_t track_ID )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return 0;
    isom_trak_t *trak = lsm_track_get_trak( root->file, track_ID );
    isom_ctts_entry_t *first_ctts = (isom_ctts_entry_t *)lsm_list_head( trak->mdia->minf->stbl->ctts->list );
    return first_ctts ? first_ctts->sample_offset : 0;
}

uint32_t lsmash_get_composition_to_decode_shift( lsmash_root_t *root, uint32_t track_ID )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return 0;
    lsmash_file_t *file = root->file->initializer;
    isom_trak_t   *trak = lsm_track_get_trak( file, track_ID );
    uint32_t sample_count = lsm_stbl_get_sample_count( trak );
    if( sample_count == 0 )
        return 0;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    if( !lsm_list_count( stbl->stts->list )
     || !lsm_list_count( stbl->ctts->list ) )
        return 0;
    if( !(file->max_isom_version >= 4 && stbl->ctts->version == 1) && !file->qt_compatible )
        return 0;   /* This movie shall not have composition to decode timeline shift. */
    uint32_t stts_idx  = 0;
    uint32_t ctts_idx  = 0;
    uint64_t dts       = 0;
    uint64_t cts       = 0;
    uint32_t ctd_shift = 0;
    uint32_t i         = 0;
    uint32_t j         = 0;
    for( uint32_t k = 0; k < sample_count; k++ )
    {
        isom_stts_entry_t *stts_data = (isom_stts_entry_t *)lsm_list_at( stbl->stts->list, stts_idx );
        isom_ctts_entry_t *ctts_data = (isom_ctts_entry_t *)lsm_list_at( stbl->ctts->list, ctts_idx );
        if( !stts_data || !ctts_data )
            return 0;
        if( ctts_data->sample_offset != ISOM_NON_OUTPUT_SAMPLE_OFFSET )
        {
            cts = dts + (int32_t)ctts_data->sample_offset;
            if( dts > cts + ctd_shift )
                ctd_shift = dts - cts;
        }
        dts += stts_data->sample_delta;
        if( ++i == stts_data->sample_count )
        {
            ++stts_idx;
            i = 0;
        }
        if( ++j == ctts_data->sample_count )
        {
            ++ctts_idx;
            j = 0;
        }
    }
    return ctd_shift;
}

uint16_t lsmash_pack_iso_language( char *iso_language )
{
    if( !iso_language || strlen( iso_language ) != 3 )
        return 0;
    return (uint16_t)LSMASH_PACK_ISO_LANGUAGE( iso_language[0], iso_language[1], iso_language[2] );
}

static int track_iso2mac_language( uint16_t ISO_language, uint16_t *MAC_language )
{
    assert( MAC_language );
    int i = 0;
    for( ; track_languages[i].iso_name; i++ )
        if( ISO_language == track_languages[i].iso_name )
            break;
    if( !track_languages[i].iso_name )
        return LSMASH_ERR_NAMELESS;
    *MAC_language = track_languages[i].mac_value;
    return 0;
}

static int track_mac2iso_language( uint16_t MAC_language, uint16_t *ISO_language )
{
    assert( ISO_language );
    int i = 0;
    for( ; track_languages[i].iso_name; i++ )
        if( MAC_language == track_languages[i].mac_value )
            break;
    *ISO_language = track_languages[i].iso_name ? track_languages[i].iso_name : ISOM_LANGUAGE_CODE_UNDEFINED;
    return 0;
}

static int track_set_media_language( lsmash_file_t *file, uint32_t track_ID, uint16_t ISO_language, uint16_t MAC_language )
{
    isom_trak_t *trak = lsm_track_get_trak( file, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->mdia->mdhd ) )
        return LSMASH_ERR_NAMELESS;
    uint16_t language = 0;
    if( file->isom_compatible )
    {
        if( ISO_language )
            language = ISO_language;
        else if( MAC_language )
        {
            int err = track_mac2iso_language( MAC_language, &language );
            if( err )
                return err;
        }
        else
            language = ISOM_LANGUAGE_CODE_UNDEFINED;
    }
    else if( file->qt_compatible )
    {
        if( ISO_language )
        {
            int err = track_iso2mac_language( ISO_language, &language );
            if( err )
                return err;
        }
        else
            language = MAC_language;
    }
    else
        return LSMASH_ERR_INVALID_DATA;
    trak->mdia->mdhd->language = language;
    return 0;
}

void lsmash_initialize_media_parameters( lsmash_media_parameters_t *param )
{
    memset( param, 0, sizeof(lsmash_media_parameters_t) );
    param->timescale = 1;
}

int lsmash_set_media_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_media_parameters_t *param )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file;
    isom_trak_t   *trak = lsm_track_get_trak( file, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->mdia->mdhd )
     || LSMASH_IS_NON_EXISTING_BOX( trak->mdia->minf->stbl ) )
        return LSMASH_ERR_NAMELESS;
    trak->mdia->mdhd->timescale = param->timescale;
    int err = track_set_media_language( file, track_ID, param->ISO_language, param->MAC_language );
    if( err < 0 )
        return err;
    if( param->media_handler_name
     && (err = track_set_media_handler_name( file, track_ID, param->media_handler_name )) < 0 )
        return err;
    if( file->qt_compatible && param->data_handler_name
     && (err = track_set_data_handler_name( file, track_ID, param->data_handler_name )) < 0 )
        return err;
    if( (file->avc_extensions || file->qt_compatible) && param->roll_grouping
     && (err = lsm_stbl_create_sample_grouping( trak, ISOM_GROUP_TYPE_ROLL )) < 0 )
        return err;
    if( (file->max_isom_version >= 6) && param->rap_grouping
     && (err = lsm_stbl_create_sample_grouping( trak, ISOM_GROUP_TYPE_RAP )) < 0 )
        return err;
    if( !file->qt_compatible && param->compact_sample_size_table )
        trak->mdia->minf->stbl->compress_sample_size_table = lsm_stbl_compress_sample_size_table;
    if( !param->no_sample_dependency_table )
        trak->mdia->minf->stbl->add_dependency_type = lsm_stbl_add_dependency_type;
    return 0;
}

static uint32_t track_get_actual_handler_name_length( isom_hdlr_t *hdlr, lsmash_file_t *file )
{
    if( hdlr->componentName_length == 0 )
        return 0;
    uint32_t length;
    uint8_t *name;
    if( file->qt_compatible )
    {
        length = LSMASH_MIN( hdlr->componentName[0], hdlr->componentName_length - 1 );
        if( !file->isom_compatible )
            return length;
        name = &hdlr->componentName[1];
    }
    else
    {
        length = hdlr->componentName_length;
        name   = hdlr->componentName;
    }
    /* Considering fool-proof such as not terminated by '\0'. */
    uint32_t i = 0;
    while( i < length && name[i] )
        ++i;
    return i;
}

int lsmash_get_media_parameters( lsmash_root_t *root, uint32_t track_ID, lsmash_media_parameters_t *param )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file->initializer;
    isom_trak_t   *trak = lsm_track_get_trak( file, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->mdia->mdhd )
     || LSMASH_IS_NON_EXISTING_BOX( trak->mdia->hdlr )
     || LSMASH_IS_NON_EXISTING_BOX( trak->mdia->minf->stbl ) )
        return LSMASH_ERR_NAMELESS;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    param->timescale    = mdhd->timescale;
    param->handler_type = trak->mdia->hdlr->componentSubtype;
    param->duration     = mdhd->duration;
    /* Whether sample grouping present. */
    {
        isom_sbgp_t *sbgp;
        isom_sgpd_t *sgpd;
        sbgp = lsm_stbl_get_sample_to_group         ( stbl, ISOM_GROUP_TYPE_RAP );
        sgpd = lsm_stbl_get_sample_group_description( stbl, ISOM_GROUP_TYPE_RAP );
        param->rap_grouping = LSMASH_IS_EXISTING_BOX( sbgp ) && LSMASH_IS_EXISTING_BOX( sgpd );
        sbgp = lsm_stbl_get_roll_recovery_sample_to_group         ( &stbl->sbgp_list );
        sgpd = lsm_stbl_get_roll_recovery_sample_group_description( &stbl->sgpd_list );
        param->roll_grouping = LSMASH_IS_EXISTING_BOX( sbgp ) && LSMASH_IS_EXISTING_BOX( sgpd );
    }
    /* Get media language. */
    if( mdhd->language >= 0x800 )
    {
        param->MAC_language = 0;
        param->ISO_language = mdhd->language;
    }
    else
    {
        param->MAC_language = mdhd->language;
        param->ISO_language = 0;
    }
    /* Get handler name(s). */
    isom_hdlr_t *hdlr = trak->mdia->hdlr;
    uint32_t actual_length = track_get_actual_handler_name_length( hdlr, file );
    uint32_t length = LSMASH_MIN( 255, actual_length );
    if( length )
    {
        memcpy( param->media_handler_name_shadow, hdlr->componentName + file->qt_compatible, length );
        param->media_handler_name_shadow[length] = '\0';
        param->media_handler_name = param->media_handler_name_shadow;
    }
    else
    {
        param->media_handler_name = NULL;
        memset( param->media_handler_name_shadow, 0, sizeof(param->media_handler_name_shadow) );
    }
    if( LSMASH_IS_EXISTING_BOX( trak->mdia->minf->hdlr ) )
    {
        hdlr = trak->mdia->minf->hdlr;
        actual_length = track_get_actual_handler_name_length( hdlr, file );
        length = LSMASH_MIN( 255, actual_length );
        if( length )
        {
            memcpy( param->data_handler_name_shadow, hdlr->componentName + file->qt_compatible, length );
            param->data_handler_name_shadow[length] = '\0';
            param->data_handler_name = param->data_handler_name_shadow;
        }
        else
        {
            param->data_handler_name = NULL;
            memset( param->data_handler_name_shadow, 0, sizeof(param->data_handler_name_shadow) );
        }
    }
    else
    {
        param->data_handler_name = NULL;
        memset( param->data_handler_name_shadow, 0, sizeof(param->data_handler_name_shadow) );
    }
    param->compact_sample_size_table  = LSMASH_IS_EXISTING_BOX( stbl->stz2 );
    param->no_sample_dependency_table = LSMASH_IS_NON_EXISTING_BOX( stbl->sdtp );
    param->reserved[0] = param->reserved[1] = 0;
    return 0;
}

int lsm_track_complement_data_reference( isom_minf_t *minf )
{
    if( LSMASH_IS_NON_EXISTING_BOX( minf->dinf->dref ) )
        return LSMASH_ERR_INVALID_DATA;
    /* Complement data referece if absent. */
    if( !minf->dinf->dref->list.head )
    {
        isom_dref_entry_t *url = isom_add_dref_entry( minf->dinf->dref, ISOM_BOX_TYPE_URL );
        if( LSMASH_IS_NON_EXISTING_BOX( url ) )
            return LSMASH_ERR_NAMELESS;
        url->flags = 0x000001;  /* Media data is in the same file. */
    }
    return 0;
}

lsmash_file_t *lsm_track_get_written_media_file
(
    isom_trak_t *trak,
    uint32_t     sample_description_index
)
{
    isom_minf_t         *minf        = trak->mdia->minf;
    isom_sample_entry_t *description = (isom_sample_entry_t *)lsmash_list_get_entry_data( &minf->stbl->stsd->list, sample_description_index );
    isom_dref_entry_t   *dref_entry  = (isom_dref_entry_t *)lsmash_list_get_entry_data( &minf->dinf->dref->list, description ? description->data_reference_index : 1 );
    lsmash_file_t       *file        = (!dref_entry || LSMASH_IS_NON_EXISTING_BOX( dref_entry->ref_file )) ? trak->file : dref_entry->ref_file;
    if( !(file->flags & LSMASH_FILE_MODE_MEDIA)
     || !(file->flags & LSMASH_FILE_MODE_WRITE) )
        return trak->file;
    return file;
}

/*---- timeline manipulator ----*/

int lsmash_modify_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID, uint32_t edit_number, lsmash_edit_t edit )
{
    if( lsm_movie_check_initializer_present( root ) < 0
     || edit.start_time < -1 )
        return LSMASH_ERR_FUNCTION_PARAM;
    lsmash_file_t *file = root->file->initializer;
    isom_trak_t   *trak = lsm_track_get_trak( file, track_ID );
    if( !trak->edts->elst->list )
        return LSMASH_ERR_NAMELESS;
    isom_elst_t       *elst = trak->edts->elst;
    isom_elst_entry_t *data = (isom_elst_entry_t *)lsm_list_get( elst->list, edit_number );
    if( !data )
        return LSMASH_ERR_NAMELESS;
    data->segment_duration = edit.duration;
    data->media_time       = edit.start_time;
    data->media_rate       = edit.rate;
    if( elst->pos == 0 || !file->fragment || file->bs->unseekable )
        return lsm_track_update_tkhd_duration( trak );
    /* Rewrite the specified entry.
     * Note: we don't update the version of the Edit List Box. */
    lsmash_bs_t *bs = file->bs;
    uint64_t current_pos = bs->offset;
    uint64_t entry_pos = elst->pos + ISOM_LIST_FULLBOX_COMMON_SIZE + ((uint64_t)edit_number - 1) * (elst->version == 1 ? 20 : 12);
    lsmash_bs_write_seek( bs, entry_pos, SEEK_SET );
    if( elst->version )
    {
        lsmash_bs_put_be64( bs, data->segment_duration );
        lsmash_bs_put_be64( bs, data->media_time );
    }
    else
    {
        lsmash_bs_put_be32( bs, (uint32_t)LSMASH_MIN( data->segment_duration, UINT32_MAX ) );
        lsmash_bs_put_be32( bs, (uint32_t)data->media_time );
    }
    lsmash_bs_put_be32( bs, data->media_rate );
    int ret = lsmash_bs_flush_buffer( bs );
    lsmash_bs_write_seek( bs, current_pos, SEEK_SET );
    return ret;
}

int lsmash_create_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID, lsmash_edit_t edit )
{
    if( lsm_movie_check_initializer_present( root ) < 0 || edit.start_time < -1 )
        return LSMASH_ERR_FUNCTION_PARAM;
    isom_trak_t *trak = lsm_track_get_trak( root->file, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->tkhd ) )
        return LSMASH_ERR_NAMELESS;
    edit.duration = (edit.duration || root->file->fragment) ? edit.duration
                  : trak->tkhd->duration ? trak->tkhd->duration
                  : lsm_track_update_tkhd_duration( trak ) < 0 ? 0
                  : trak->tkhd->duration;
    if( (LSMASH_IS_NON_EXISTING_BOX( trak->edts )       && LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_edts( trak ) ))
     || (LSMASH_IS_NON_EXISTING_BOX( trak->edts->elst ) && LSMASH_IS_BOX_ADDITION_FAILURE( isom_add_elst( trak->edts ) )) )
        return LSMASH_ERR_NAMELESS;
    int err = track_add_elst_entry( trak->edts->elst, edit.duration, edit.start_time, edit.rate );
    if( err < 0 )
        return err;
    return lsm_track_update_tkhd_duration( trak );
}

int lsmash_get_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID, uint32_t edit_number, lsmash_edit_t *edit )
{
    if( lsm_movie_check_initializer_present( root ) < 0 || !edit )
        return LSMASH_ERR_FUNCTION_PARAM;
    isom_elst_entry_t *data;
    isom_trak_t *trak = lsm_track_get_trak( root->file->initializer, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak ) )
        data = isom_timelime_get_explicit_timeline_map( root, track_ID, edit_number );
    else
    {
        if( LSMASH_IS_NON_EXISTING_BOX( trak->edts->elst ) )
        {
            /* no edits */
            edit->duration   = 0;
            edit->start_time = 0;
            edit->rate       = 0;
            return 0;
        }
        data = (isom_elst_entry_t *)lsm_list_get( trak->edts->elst->list, edit_number );
    }
    if( !data )
        return LSMASH_ERR_NAMELESS;
    edit->duration   = data->segment_duration;
    edit->start_time = data->media_time;
    edit->rate       = data->media_rate;
    return 0;
}

uint32_t lsmash_count_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    isom_trak_t *trak = lsm_track_get_trak( root->file->initializer, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak ) )
        return isom_timelime_count_explicit_timeline_map( root, track_ID );
    else
        return lsm_list_count( trak->edts->elst->list );
}

int lsmash_delete_explicit_timeline_map( lsmash_root_t *root, uint32_t track_ID )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    isom_trak_t *trak = lsm_track_get_trak( root->file->initializer, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak ) )
        return LSMASH_ERR_NAMELESS;
    isom_remove_box_by_itself( trak->edts );
    return lsm_track_update_tkhd_duration( trak );
}

/*---- create / modification time fields manipulators ----*/

int lsmash_update_media_modification_time( lsmash_root_t *root, uint32_t track_ID )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    isom_trak_t *trak = lsm_track_get_trak( root->file->initializer, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->mdia->mdhd ) )
        return LSMASH_ERR_NAMELESS;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    mdhd->modification_time = track_get_current_mp4time();
    /* overwrite strange creation_time */
    if( mdhd->creation_time > mdhd->modification_time )
        mdhd->creation_time = mdhd->modification_time;
    return 0;
}

int lsmash_update_track_modification_time( lsmash_root_t *root, uint32_t track_ID )
{
    if( lsm_movie_check_initializer_present( root ) < 0 )
        return LSMASH_ERR_FUNCTION_PARAM;
    isom_trak_t *trak = lsm_track_get_trak( root->file->initializer, track_ID );
    if( LSMASH_IS_NON_EXISTING_BOX( trak->tkhd ) )
        return LSMASH_ERR_NAMELESS;
    isom_tkhd_t *tkhd = trak->tkhd;
    tkhd->modification_time = track_get_current_mp4time();
    /* overwrite strange creation_time */
    if( tkhd->creation_time > tkhd->modification_time )
        tkhd->creation_time = tkhd->modification_time;
    return 0;
}
