/*****************************************************************************
 * stbl.c
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
#include "sample.h"
#include "stbl.h"

#include "codecs/description.h"

int lsm_stbl_add_stts_entry( isom_stbl_t *stbl, uint32_t sample_delta )
{
    assert( LSMASH_IS_EXISTING_BOX( stbl->stts ) );
    if( !stbl->stts->list )
        return LSMASH_ERR_NAMELESS;
    isom_stts_entry_t *data = (isom_stts_entry_t *)lsm_list_append( stbl->stts->list );
    if( !data )
        return LSMASH_ERR_MEMORY_ALLOC;
    data->sample_count = 1;
    data->sample_delta = sample_delta;
    return 0;
}

static int stbl_add_ctts_entry( isom_stbl_t *stbl, uint32_t sample_count, uint32_t sample_offset )
{
    assert( LSMASH_IS_EXISTING_BOX( stbl->ctts ) );
    if( !stbl->ctts->list )
        return LSMASH_ERR_NAMELESS;
    isom_ctts_entry_t *data = (isom_ctts_entry_t *)lsm_list_append( stbl->ctts->list );
    if( !data )
        return LSMASH_ERR_MEMORY_ALLOC;
    data->sample_count  = sample_count;
    data->sample_offset = sample_offset;
    return 0;
}

int lsm_stbl_add_stsc_entry( isom_stbl_t *stbl, uint32_t first_chunk, uint32_t samples_per_chunk, uint32_t sample_description_index )
{
    assert( LSMASH_IS_EXISTING_BOX( stbl->stsc ) );
    if( !stbl->stsc->list )
        return LSMASH_ERR_NAMELESS;
    isom_stsc_entry_t *data = (isom_stsc_entry_t *)lsm_list_append( stbl->stsc->list );
    if( !data )
        return LSMASH_ERR_MEMORY_ALLOC;
    data->first_chunk              = first_chunk;
    data->samples_per_chunk        = samples_per_chunk;
    data->sample_description_index = sample_description_index;
    return 0;
}

static int stbl_add_stsz_entry( isom_stbl_t *stbl, uint32_t entry_size )
{
    assert( LSMASH_IS_EXISTING_BOX( stbl ) );
    if( LSMASH_IS_NON_EXISTING_BOX( stbl->stsz ) )
        return LSMASH_ERR_NAMELESS;
    isom_stsz_t *stsz = stbl->stsz;
    /* retrieve initial sample_size */
    if( stsz->sample_count == 0 )
        stsz->sample_size = entry_size;
    /* if it seems constant sample size at present, update sample_count only */
    if( !stsz->list && stsz->sample_size == entry_size )
    {
        ++ stsz->sample_count;
        return 0;
    }
    /* found sample_size varies, create sample_size list */
    if( !stsz->list )
    {
        stsz->list = lsm_list_create_simple( sizeof(isom_stsz_entry_t) );
        if( !stsz->list )
            return LSMASH_ERR_MEMORY_ALLOC;
        for( uint32_t i = 0; i < stsz->sample_count; i++ )
        {
            isom_stsz_entry_t *data = (isom_stsz_entry_t *)lsm_list_append( stsz->list );
            if( !data )
                return LSMASH_ERR_MEMORY_ALLOC;
            data->entry_size = stsz->sample_size;
        }
        stsz->sample_size = 0;
    }
    isom_stsz_entry_t *data = (isom_stsz_entry_t *)lsm_list_append( stsz->list );
    if( !data )
        return LSMASH_ERR_MEMORY_ALLOC;
    data->entry_size = entry_size;
    ++ stsz->sample_count;
    return 0;
}

static int stbl_add_stss_entry( isom_stbl_t *stbl, uint32_t sample_number )
{
    assert( LSMASH_IS_EXISTING_BOX( stbl->stss ) );
    if( !stbl->stss->list )
        return LSMASH_ERR_NAMELESS;
    isom_stss_entry_t *data = (isom_stss_entry_t *)lsm_list_append( stbl->stss->list );
    if( !data )
        return LSMASH_ERR_MEMORY_ALLOC;
    data->sample_number = sample_number;
    return 0;
}

static int stbl_add_stps_entry( isom_stbl_t *stbl, uint32_t sample_number )
{
    assert( LSMASH_IS_EXISTING_BOX( stbl->stps ) );
    if( !stbl->stps->list )
        return LSMASH_ERR_NAMELESS;
    isom_stps_entry_t *data = (isom_stps_entry_t *)lsm_list_append( stbl->stps->list );
    if( !data )
        return LSMASH_ERR_MEMORY_ALLOC;
    data->sample_number = sample_number;
    return 0;
}

/* Between ISOBMFF and QTFF, the most significant 2-bit has different meaning.
 * For the maximum compatibility, we set 0 to the most significant 2-bit when compatible with
 * both ISOBMFF with AVCFF extensions and QTFF.
 *   compatibility == 0 -> neither AVCFF extensions nor QTFF compatible
 *   compatibility == 1 -> AVCFF extensions compatible
 *   compatibility == 2 -> QTFF compatible
 *   compatibility == 3 -> both AVCFF extensions and QTFF compatible */
static int stbl_add_sdtp_entry( isom_box_t *parent, lsmash_sample_property_t *prop, int compatibility )
{
    if( !prop || LSMASH_IS_NON_EXISTING_BOX( parent ) )
        return LSMASH_ERR_NAMELESS;
    isom_sdtp_t *sdtp = lsm_box_default_non_existing_sdtp();
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STBL ) )
        sdtp = ((isom_stbl_t *)parent)->sdtp;
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAF ) )
        sdtp = ((isom_traf_t *)parent)->sdtp;
    else
        assert( 0 );
    if( LSMASH_IS_NON_EXISTING_BOX( sdtp )
      || !sdtp->list )
        return LSMASH_ERR_NAMELESS;
    isom_sdtp_entry_t *data = (isom_sdtp_entry_t *)lsm_list_append( sdtp->list );
    if( !data )
        return LSMASH_ERR_MEMORY_ALLOC;
    if( compatibility == 1 )
        data->is_leading = prop->leading & 0x03;
    else if( compatibility == 2 )
        data->is_leading = prop->allow_earlier & 0x03;
    else
    {
        data->is_leading = 0;
        assert( compatibility == 3 );
    }
    data->sample_depends_on     = prop->independent & 0x03;
    data->sample_is_depended_on = prop->disposable  & 0x03;
    data->sample_has_redundancy = prop->redundant   & 0x03;
    return 0;
}

static int stbl_add_co64_entry( isom_stbl_t *stbl, uint64_t chunk_offset )
{
    assert( LSMASH_IS_EXISTING_BOX( stbl->stco ) );
    if( !stbl->stco->list )
        return LSMASH_ERR_NAMELESS;
    isom_co64_entry_t *data = (isom_co64_entry_t *)lsm_list_append( stbl->stco->list );
    if( !data )
        return LSMASH_ERR_MEMORY_ALLOC;
    data->chunk_offset = chunk_offset;
    return 0;
}

int lsm_stbl_convert_stco_to_co64( isom_stbl_t *stbl )
{
    assert( LSMASH_IS_EXISTING_BOX( stbl->stco ) );
    /* backup stco */
    int err = 0;
    isom_stco_t *stco = stbl->stco;
    LSMASH_MAKE_BOX_NON_EXISTING( stbl->stco );
    if( LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_co64( stbl ) ) )
    {
        err = LSMASH_ERR_NAMELESS;
        goto fail;
    }
    /* move chunk_offset to co64 from stco */
    isom_stco_entry_t *data;
    LSM_LIST_FOR_EACH( data, stco->list )
    {
        if( (err = stbl_add_co64_entry( stbl, data->chunk_offset )) < 0 )
            goto fail;
    }
fail:
    lsm_box_remove_by_itself( stco );
    return err;
}

int lsm_stbl_add_stco_entry( isom_stbl_t *stbl, uint64_t chunk_offset )
{
    if( !stbl->stco->list )
        return LSMASH_ERR_NAMELESS;
    if( stbl->stco->large_presentation )
        return stbl_add_co64_entry( stbl, chunk_offset );
    if( chunk_offset > UINT32_MAX )
    {
        int err = lsm_stbl_convert_stco_to_co64( stbl );
        if( err < 0 )
            return err;
        return stbl_add_co64_entry( stbl, chunk_offset );
    }
    isom_stco_entry_t *data = (isom_stco_entry_t *)lsm_list_append( stbl->stco->list );
    if( !data )
        return LSMASH_ERR_MEMORY_ALLOC;
    data->chunk_offset = (uint32_t)chunk_offset;
    return 0;
}

static isom_sgpd_t *stbl_get_sample_group_description_common( lsmash_entry_list_t *list, uint32_t grouping_type )
{
    for( lsmash_entry_t *entry = list->head; entry; entry = entry->next )
    {
        isom_sgpd_t *sgpd = (isom_sgpd_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( sgpd )
         || !sgpd->list )
            return lsm_box_default_non_existing_sgpd();
        if( sgpd->grouping_type == grouping_type )
            return sgpd;
    }
    return lsm_box_default_non_existing_sgpd();
}

static isom_sbgp_t *stbl_get_sample_to_group_common( lsmash_entry_list_t *list, uint32_t grouping_type )
{
    for( lsmash_entry_t *entry = list->head; entry; entry = entry->next )
    {
        isom_sbgp_t *sbgp = (isom_sbgp_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( sbgp )
         || !sbgp->list )
            return lsm_box_default_non_existing_sbgp();
        if( sbgp->grouping_type == grouping_type )
            return sbgp;
    }
    return lsm_box_default_non_existing_sbgp();
}

isom_sgpd_t *lsm_stbl_get_sample_group_description( isom_stbl_t *stbl, uint32_t grouping_type )
{
    return stbl_get_sample_group_description_common( &stbl->sgpd_list, grouping_type );
}

isom_sbgp_t *lsm_stbl_get_sample_to_group( isom_stbl_t *stbl, uint32_t grouping_type )
{
    return stbl_get_sample_to_group_common( &stbl->sbgp_list, grouping_type );
}

isom_sgpd_t *lsm_stbl_get_roll_recovery_sample_group_description( lsmash_entry_list_t *list )
{
    isom_sgpd_t *sgpd;
    if( ((sgpd = stbl_get_sample_group_description_common( list, ISOM_GROUP_TYPE_ROLL )), LSMASH_IS_EXISTING_BOX( sgpd ))
     || ((sgpd = stbl_get_sample_group_description_common( list, ISOM_GROUP_TYPE_PROL )), LSMASH_IS_EXISTING_BOX( sgpd )) )
        return sgpd;
    return lsm_box_default_non_existing_sgpd();
}

isom_sbgp_t *lsm_stbl_get_roll_recovery_sample_to_group( lsmash_entry_list_t *list )
{
    isom_sbgp_t *sbgp;
    if( ((sbgp = stbl_get_sample_to_group_common( list, ISOM_GROUP_TYPE_ROLL )), LSMASH_IS_EXISTING_BOX( sbgp ))
     || ((sbgp = stbl_get_sample_to_group_common( list, ISOM_GROUP_TYPE_PROL )), LSMASH_IS_EXISTING_BOX( sbgp )) )
        return sbgp;
    return lsm_box_default_non_existing_sbgp();
}

isom_sgpd_t *lsm_stbl_get_fragment_sample_group_description( isom_traf_t *traf, uint32_t grouping_type )
{
    return stbl_get_sample_group_description_common( &traf->sgpd_list, grouping_type );
}

isom_sbgp_t *lsm_stbl_get_fragment_sample_to_group( isom_traf_t *traf, uint32_t grouping_type )
{
    return stbl_get_sample_to_group_common( &traf->sbgp_list, grouping_type );
}

static isom_rap_entry_t *stbl_add_rap_group_entry( isom_sgpd_t *sgpd )
{
    if( LSMASH_IS_NON_EXISTING_BOX( sgpd ) )
        return NULL;
    isom_rap_entry_t *data = (isom_rap_entry_t *)lsm_list_append( sgpd->list );
    if( !data )
        return NULL;
    data->description_length        = 0;
    data->num_leading_samples_known = 0;
    data->num_leading_samples       = 0;
    return data;
}

static isom_roll_entry_t *stbl_add_roll_group_entry( isom_sgpd_t *sgpd, int16_t roll_distance )
{
    if( LSMASH_IS_NON_EXISTING_BOX( sgpd ) )
        return NULL;
    isom_roll_entry_t *data = (isom_roll_entry_t *)lsm_list_append( sgpd->list );
    if( !data )
        return NULL;
    data->description_length = 0;
    data->roll_distance      = roll_distance;
    return data;
}

static isom_group_assignment_entry_t *stbl_add_group_assignment_entry( isom_sbgp_t *sbgp, uint32_t sample_count, uint32_t group_description_index )
{
    if( LSMASH_IS_NON_EXISTING_BOX( sbgp ) )
        return NULL;
    isom_group_assignment_entry_t *data = (isom_group_assignment_entry_t *)lsm_list_append( sbgp->list );
    if( !data )
        return NULL;
    data->sample_count            = sample_count;
    data->group_description_index = group_description_index;
    return data;
}

static uint32_t stbl_get_sample_count_from_sample_table( isom_stbl_t *stbl )
{
    if( LSMASH_IS_EXISTING_BOX( stbl->stsz ) )
        return stbl->stsz->sample_count;
    else if( LSMASH_IS_EXISTING_BOX( stbl->stz2 ) )
        return stbl->stz2->sample_count;
    else
        return 0;
}

uint32_t lsm_stbl_get_sample_count( isom_trak_t *trak )
{
    return stbl_get_sample_count_from_sample_table( trak->mdia->minf->stbl );
}

uint64_t lsm_stbl_get_dts( isom_stts_t *stts, uint32_t sample_number )
{
    if( !stts->list )
        return 0;
    uint64_t dts = 0;
    uint32_t i   = 1;
    int      found = 0;
    isom_stts_entry_t *data = NULL;
    LSM_LIST_FOR_EACH( data, stts->list )
    {
        if( !data )
            return 0;
        if( i + data->sample_count > sample_number )
        {
            found = 1;
            break;
        }
        dts += (uint64_t)data->sample_delta * data->sample_count;
        i   += data->sample_count;
    }
    if( !found )
        return 0;
    dts += (uint64_t)data->sample_delta * (sample_number - i);
    return dts;
}

#if 0
static uint64_t stbl_get_cts( isom_stts_t *stts, isom_ctts_t *ctts, uint32_t sample_number )
{
    if( !stts->list )
        return 0;
    if( LSMASH_IS_NON_EXISTING_BOX( ctts ) )
        return lsm_stbl_get_dts( stts, sample_number );
    uint32_t i = 1;     /* This can be 0 (and then condition below shall be changed) but I dare use same algorithm with lsm_stbl_get_dts. */
    int      found = 0;
    isom_ctts_entry_t *data = NULL;
    if( sample_number == 0 )
        return 0;
    LSM_LIST_FOR_EACH( data, ctts->list )
    {
        if( !data )
            return 0;
        if( i + data->sample_count > sample_number )
        {
            found = 1;
            break;
        }
        i += data->sample_count;
    }
    if( !found )
        return 0;
    return lsm_stbl_get_dts( stts, sample_number ) + data->sample_offset;
}
#endif

int lsm_stbl_replace_last_sample_delta( isom_stbl_t *stbl, uint32_t sample_delta )
{
    assert( LSMASH_IS_EXISTING_BOX( stbl->stts ) );
    isom_stts_entry_t *last_stts_data = (isom_stts_entry_t *)lsm_list_tail( stbl->stts->list );
    if( !last_stts_data )
        return LSMASH_ERR_NAMELESS;
    if( sample_delta != last_stts_data->sample_delta )
    {
        if( last_stts_data->sample_count > 1 )
        {
            last_stts_data->sample_count -= 1;
            int err = lsm_stbl_add_stts_entry( stbl, sample_delta );
            if( err < 0 )
                return err;
        }
        else
            last_stts_data->sample_delta = sample_delta;
    }
    return 0;
}

static int stbl_increment_sample_number_in_entry
(
    uint32_t          *sample_number_in_entry,
    uint32_t           sample_count_in_entry,
    lsm_list_cursor_t *cursor
)
{
    if( *sample_number_in_entry != sample_count_in_entry )
    {
        *sample_number_in_entry += 1;
        return 0;
    }
    /* Precede the next entry. */
    *sample_number_in_entry = 1;
    lsm_list_cursor_next( cursor );
    return 0;
}

/*---------------------------------------------------------------------------
 * Technical & Historical Background: Bitrate Calculation (btrt / esds)
 *
 * In ISO/IEC 14496-1 (`esds` DecoderConfigDescriptor) and ISO/IEC 14496-12
 * (`btrt` MPEG4BitRateBox), the fields `bufferSizeDB`, `maxBitrate`, and
 * `avgBitrate` were formally conceived within the hypothetical decoder
 * system model (HRD / VBV / CPB) to define the scheduled delivery rate
 * into the *input* of the decoding buffer (Delivery Rate).
 *
 * However, the encoder's delivery schedule and transmission rate constraints
 * cannot always be deduced from standalone elementary streams or demuxed
 * container samples alone. Furthermore, general media inspection utilities
 * and end users historically expect bitrate metrics to reflect the decoding
 * consumption rate at the *output* of the decoding buffer (the sum of sample
 * sizes decoded and consumed within a 1-second sliding window).
 *
 * To reconcile formal delivery requirements with practical inspection needs,
 * L-SMASH calculates `bufferSizeDB`, `maxBitrate`, and `avgBitrate` as a
 * 1-second sliding window decoding consumption rate and buffer simulation.
 *---------------------------------------------------------------------------*/
int lsm_stbl_calculate_bitrate_description
(
    isom_stbl_t *stbl,
    isom_mdhd_t *mdhd,
    uint32_t    *bufferSizeDB,
    uint32_t    *maxBitrate,
    uint32_t    *avgBitrate,
    uint32_t     sample_description_index
)
{
    isom_stsz_t *stsz = stbl->stsz;
    lsm_list_t *stsz_list = LSMASH_IS_EXISTING_BOX( stsz ) ? stsz->list : stbl->stz2->list;
    lsm_list_cursor_t stsz_cursor;
    lsm_list_cursor_t stts_cursor;
    lsm_list_cursor_t next_stsc_cursor;
    lsm_list_cursor_init( &stsz_cursor, stsz_list );
    lsm_list_cursor_init( &stts_cursor, stbl->stts->list );
    lsm_list_cursor_init( &next_stsc_cursor, stbl->stsc->list );
    isom_stts_entry_t *stts_data    = NULL;
    isom_stsc_entry_t *stsc_data    = NULL;
    uint32_t rate                   = 0;
    uint64_t dts                    = 0;
    uint32_t time_wnd               = 0;
    uint32_t chunk_number           = 0;
    uint32_t sample_number_in_stts  = 1;
    uint32_t sample_number_in_chunk = 1;
    uint32_t constant_sample_size   = LSMASH_IS_EXISTING_BOX( stsz ) ? stsz->sample_size : 0;
    *bufferSizeDB = 0;
    *maxBitrate   = 0;
    *avgBitrate   = 0;
    while( stts_cursor.data )
    {
        int err;
        if( !stsc_data || sample_number_in_chunk == stsc_data->samples_per_chunk )
        {
            /* Move the next chunk. */
            sample_number_in_chunk = 1;
            ++chunk_number;
            /* Check if the next entry is broken. */
            while( next_stsc_cursor.data && ((isom_stsc_entry_t *)next_stsc_cursor.data)->first_chunk < chunk_number )
            {
                /* Just skip broken next entry. */
                lsm_list_cursor_next( &next_stsc_cursor );
            }
            /* Check if the next chunk belongs to the next sequence of chunks. */
            if( next_stsc_cursor.data && ((isom_stsc_entry_t *)next_stsc_cursor.data)->first_chunk == chunk_number )
            {
                stsc_data = (isom_stsc_entry_t *)next_stsc_cursor.data;
                lsm_list_cursor_next( &next_stsc_cursor );
                /* Check if the next contiguous chunks belong to given sample description. */
                if( stsc_data->sample_description_index != sample_description_index )
                {
                    /* Skip chunks which don't belong to given sample description. */
                    uint32_t number_of_skips   = 0;
                    uint32_t first_chunk       = stsc_data->first_chunk;
                    uint32_t samples_per_chunk = stsc_data->samples_per_chunk;
                    while( next_stsc_cursor.data )
                    {
                        isom_stsc_entry_t *next_stsc_data = (isom_stsc_entry_t *)next_stsc_cursor.data;
                        if( next_stsc_data->sample_description_index != sample_description_index )
                        {
                            stsc_data         = next_stsc_data;
                            number_of_skips  += (stsc_data->first_chunk - first_chunk) * samples_per_chunk;
                            first_chunk       = stsc_data->first_chunk;
                            samples_per_chunk = stsc_data->samples_per_chunk;
                        }
                        else if( next_stsc_data->first_chunk <= first_chunk )
                            ;   /* broken entry */
                        else
                            break;
                        /* Just skip the next entry. */
                        lsm_list_cursor_next( &next_stsc_cursor );
                    }
                    if( !next_stsc_cursor.data )
                        break;      /* There is no more chunks which don't belong to given sample description. */
                    number_of_skips += (((isom_stsc_entry_t *)next_stsc_cursor.data)->first_chunk - first_chunk) * samples_per_chunk;
                    for( uint32_t i = 0; i < number_of_skips; i++ )
                    {
                        if( stsz_list )
                        {
                            if( !stsz_cursor.data )
                                break;
                            lsm_list_cursor_next( &stsz_cursor );
                        }
                        if( !stts_cursor.data )
                            break;
                        if( (err = stbl_increment_sample_number_in_entry( &sample_number_in_stts,
                                                                          ((isom_stts_entry_t *)stts_cursor.data)->sample_count,
                                                                          &stts_cursor )) < 0 )
                            return err;
                    }
                    if( (stsz_list && !stsz_cursor.data) || !stts_cursor.data )
                        break;
                    chunk_number = stsc_data->first_chunk;
                }
            }
        }
        else
            ++sample_number_in_chunk;
        /* Get current sample's size. */
        uint32_t size;
        if( stsz_list )
        {
            if( !stsz_cursor.data )
                break;
            isom_stsz_entry_t *stsz_data = (isom_stsz_entry_t *)stsz_cursor.data;
            size = stsz_data->entry_size;
            lsm_list_cursor_next( &stsz_cursor );
        }
        else
            size = constant_sample_size;
        /* Get current sample's DTS. */
        if( stts_data )
            dts += stts_data->sample_delta;
        stts_data = (isom_stts_entry_t *)stts_cursor.data;
        if( !stts_data )
            return LSMASH_ERR_INVALID_DATA;
        if( (err = stbl_increment_sample_number_in_entry( &sample_number_in_stts, stts_data->sample_count, &stts_cursor )) < 0 )
            return err;
        /* Calculate bitrate description. */
        if( *bufferSizeDB < size )
            *bufferSizeDB = size;
        *avgBitrate += size;
        rate += size;
        if( dts > time_wnd + mdhd->timescale )
        {
            if( rate > *maxBitrate )
                *maxBitrate = rate;
            time_wnd = dts;
            rate = 0;
        }
    }
    double duration = (double)mdhd->duration / mdhd->timescale;
    *avgBitrate = (uint32_t)(*avgBitrate / duration);
    if( *maxBitrate == 0 )
        *maxBitrate = *avgBitrate;
    /* Convert to bits per second. */
    *maxBitrate *= 8;
    *avgBitrate *= 8;
    return 0;
}

int lsm_stbl_is_variable_size( isom_stbl_t *stbl )
{
    if( (LSMASH_IS_EXISTING_BOX( stbl->stz2 ) && stbl->stz2->sample_count > 1)
     || (LSMASH_IS_EXISTING_BOX( stbl->stsz ) && stbl->stsz->sample_count > 1 && stbl->stsz->sample_size == 0) )
        return 1;
    else
        return 0;
}

uint32_t lsm_stbl_get_first_sample_size( isom_stbl_t *stbl )
{
    if( LSMASH_IS_EXISTING_BOX( stbl->stsz ) )
    {
        /* 'stsz' */
        if( stbl->stsz->sample_size )
            return stbl->stsz->sample_size;
        isom_stsz_entry_t *first_stsz = (isom_stsz_entry_t *)lsm_list_head( stbl->stsz->list );
        return first_stsz ? first_stsz->entry_size : 0;
    }
    else if( LSMASH_IS_EXISTING_BOX( stbl->stz2 ) )
    {
        /* stz2 */
        isom_stsz_entry_t *first_stz2 = (isom_stsz_entry_t *)lsm_list_head( stbl->stz2->list );
        return first_stz2 ? first_stz2->entry_size : 0;
    }
    else
        return 0;
}

int lsm_stbl_update_bitrate_description( isom_mdia_t *mdia )
{
    if( LSMASH_IS_NON_EXISTING_BOX( mdia->mdhd ) )
        return LSMASH_ERR_INVALID_DATA;
    isom_stbl_t *stbl = mdia->minf->stbl;
    if( LSMASH_IS_NON_EXISTING_BOX( stbl->stsd )
     || (LSMASH_IS_NON_EXISTING_BOX( stbl->stsz ) && LSMASH_IS_NON_EXISTING_BOX( stbl->stz2 ))
     || !stbl->stsc->list
     || !stbl->stts->list )
        return LSMASH_ERR_INVALID_DATA;
    uint32_t sample_description_index = 0;
    for( lsmash_entry_t *entry = stbl->stsd->list.head; entry; entry = entry->next )
    {
        isom_sample_entry_t *sample_entry = (isom_sample_entry_t *)entry->data;
        if( !sample_entry )
            return LSMASH_ERR_INVALID_DATA;
        ++sample_description_index;
        isom_bitrate_updater_t func_update_bitrate = lsm_description_get_bitrate_updater( sample_entry );
        if( func_update_bitrate )
        {
            int err = func_update_bitrate( stbl, mdia->mdhd, sample_description_index );
            if( err < 0 )
                return err;
        }
    }
    return sample_description_index ? 0 : LSMASH_ERR_INVALID_DATA;
}

int lsm_stbl_add_sample_grouping( isom_box_t *parent, isom_grouping_type grouping_type )
{
    isom_sgpd_t *sgpd;
    isom_sbgp_t *sbgp;
    if( ((sgpd = lsm_box_add_sgpd( parent )), LSMASH_IS_NON_EXISTING_BOX( sgpd ))
     || ((sbgp = lsm_box_add_sbgp( parent )), LSMASH_IS_NON_EXISTING_BOX( sbgp )) )
        return LSMASH_ERR_NAMELESS;
    sbgp->grouping_type = grouping_type;
    sgpd->grouping_type = grouping_type;
    sgpd->version       = 1;    /* We use version 1 for Sample Group Description Box because it is recommended in the spec. */
    switch( grouping_type )
    {
        case ISOM_GROUP_TYPE_RAP :
            sgpd->default_length = 1;
            break;
        case ISOM_GROUP_TYPE_ROLL :
        case ISOM_GROUP_TYPE_PROL :
            sgpd->default_length = 2;
            break;
        default :
            /* We don't consider other grouping types currently. */
            break;
    }
    return 0;
}

int lsm_stbl_create_sample_grouping( isom_trak_t *trak, isom_grouping_type grouping_type )
{
    assert( LSMASH_IS_EXISTING_BOX( trak ) );
    lsmash_file_t *file = trak->file;
    switch( grouping_type )
    {
        case ISOM_GROUP_TYPE_RAP :
            assert( file->max_isom_version >= 6 );
            break;
        case ISOM_GROUP_TYPE_ROLL :
        case ISOM_GROUP_TYPE_PROL :
            assert( file->avc_extensions || file->qt_compatible );
            break;
        default :
            assert( 0 );
            break;
    }
    int err = lsm_stbl_add_sample_grouping( (isom_box_t *)trak->mdia->minf->stbl, grouping_type );
    if( err < 0 )
        return err;
    if( trak->cache->fragment && file->max_isom_version >= 6 )
        switch( grouping_type )
        {
            case ISOM_GROUP_TYPE_RAP :
                trak->cache->fragment->rap_grouping = 1;
                break;
            case ISOM_GROUP_TYPE_ROLL :
            case ISOM_GROUP_TYPE_PROL :
                trak->cache->fragment->roll_grouping = 1;
                break;
            default :
                /* We don't consider other grouping types currently. */
                break;
        }
    return 0;
}

int lsm_stbl_compress_sample_size_table( isom_stbl_t *stbl )
{
    if( stbl->file->max_3gpp_version )
        /* 3GPP: Limitations to the ISO base media file format
         *  - compact sample sizes ('stz2') shall not be used for tracks containing H.263, MPEG-4 video, AMR, AMR-WB, AAC or Timed text.
         * Note that 'mp4a' check is incomplete here since this restriction is not applied to Enhanced aacPlus audio (HE-AAC v2). */
        for( lsmash_entry_t *entry = stbl->stsd->list.head; entry; entry = entry->next )
        {
            isom_sample_entry_t *sample_entry = (isom_sample_entry_t *)entry->data;
            if( LSMASH_IS_NON_EXISTING_BOX( sample_entry ) )
                return LSMASH_ERR_INVALID_DATA;
            lsmash_codec_type_t sample_type = sample_entry->type;
            if( lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_S263_VIDEO )
             || lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_MP4V_VIDEO )
             || lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_MP4A_AUDIO )
             || lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_SAMR_AUDIO )
             || lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_SAWB_AUDIO )
             || lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_TX3G_TEXT ) )
                return 0;
        }
    if( LSMASH_IS_EXISTING_BOX( stbl->stsz ) && lsm_stbl_is_variable_size( stbl ) )
    {
        int max_num_bits = 0;
        isom_stsz_entry_t *data;
        LSM_LIST_FOR_EACH( data, stbl->stsz->list )
        {
            if( !data )
                return LSMASH_ERR_INVALID_DATA;
            int num_bits;
            for( num_bits = 1; data->entry_size >> num_bits; num_bits++ );
            if( max_num_bits < num_bits )
            {
                max_num_bits = num_bits;
                if( max_num_bits > 16 )
                    return 0;   /* not compressible */
            }
        }
        if( max_num_bits <= 16 && LSMASH_IS_BOX_ADDITION_SUCCESS( lsm_box_add_stz2( stbl ) ) )
        {
            /* The sample size table can be compressed by using 'stz2'. */
            isom_stsz_t *stsz = stbl->stsz;
            isom_stz2_t *stz2 = stbl->stz2;
            stz2->sample_count = stsz->sample_count;
            if( max_num_bits <= 4 )
                stz2->field_size = 4;
            else if( max_num_bits <= 8 )
                stz2->field_size = 8;
            else
                stz2->field_size = 16;
            lsm_list_move( stz2->list, stsz->list );
            lsm_box_remove_by_itself( stsz );
        }
    }
    return 0;
}

int lsm_stbl_add_dependency_type( isom_stbl_t *stbl, lsmash_file_t *file, lsmash_sample_property_t *prop )
{
    if( !file->qt_compatible && !file->avc_extensions )
        return 0;
    int compatibility = file->avc_extensions && file->qt_compatible ? 3
                      : file->qt_compatible                         ? 2
                      : file->avc_extensions                        ? 1
                      :                                               0;
    if( LSMASH_IS_EXISTING_BOX( stbl->sdtp ) )
        return stbl_add_sdtp_entry( (isom_box_t *)stbl, prop, compatibility );
    /* no null check for prop */
    if( !prop->allow_earlier
     && !prop->leading
     && !prop->independent
     && !prop->disposable
     && !prop->redundant )
        return 0;
    if( LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_sdtp( (isom_box_t *)stbl ) ) )
        return LSMASH_ERR_NAMELESS;
    uint32_t count = stbl_get_sample_count_from_sample_table( stbl );
    /* fill past samples with ISOM_SAMPLE_*_UNKNOWN */
    lsmash_sample_property_t null_prop = { 0 };
    for( uint32_t i = 1; i < count; i++ )
    {
        int err = stbl_add_sdtp_entry( (isom_box_t *)stbl, &null_prop, compatibility );
        if( err < 0 )
            return err;
    }
    return stbl_add_sdtp_entry( (isom_box_t *)stbl, prop, compatibility );
}

static uint32_t stbl_add_size( isom_stbl_t *stbl, uint32_t sample_size )
{
    if( stbl_add_stsz_entry( stbl, sample_size ) < 0 )
        return 0;
    return stbl_get_sample_count_from_sample_table( stbl );
}

static uint32_t stbl_add_dts( isom_stbl_t *stbl, uint64_t dts, uint64_t prev_dts )
{
    isom_stts_t *stts = stbl->stts;
    if( lsm_list_count( stts->list ) == 0 )
        return lsm_stbl_add_stts_entry( stbl, dts ) < 0 ? 0 : dts;
    if( dts <= prev_dts )
        return 0;
    uint32_t sample_delta = dts - prev_dts;
    isom_stts_entry_t *data = (isom_stts_entry_t *)lsm_list_tail( stts->list );
    if( data->sample_delta == sample_delta )
        ++ data->sample_count;
    else if( lsm_stbl_add_stts_entry( stbl, sample_delta ) < 0 )
        return 0;
    return sample_delta;
}

/* Add ctts box and the first ctts entry. */
static int stbl_add_initial_sample_offset( isom_stbl_t *stbl, uint32_t sample_offset )
{
    if( LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_ctts( stbl ) ) )
        return LSMASH_ERR_NAMELESS;
    if( sample_offset == ISOM_NON_OUTPUT_SAMPLE_OFFSET )
        stbl->ctts->version = 1;
    uint32_t sample_count = stbl_get_sample_count_from_sample_table( stbl );
    if( sample_count > 1 )
    {
        /* Set all prior samples' sample_offset to 0. */
        int err = stbl_add_ctts_entry( stbl, sample_count - 1, 0 );
        if( err < 0 )
            return err;
    }
    return stbl_add_ctts_entry( stbl, 1, sample_offset );
}

static int stbl_add_sample_offset( isom_stbl_t *stbl, uint32_t sample_offset )
{
    if( !stbl->ctts->list )
        return LSMASH_ERR_INVALID_DATA;
    isom_ctts_entry_t *data = (isom_ctts_entry_t *)lsm_list_tail( stbl->ctts->list );
    if( data->sample_offset == sample_offset )
        ++ data->sample_count;
    else
    {
        int err = stbl_add_ctts_entry( stbl, 1, sample_offset );
        if( err < 0 )
            return err;
    }
    return 0;
}

static int stbl_add_cts( isom_stbl_t *stbl, uint64_t dts, uint64_t cts, int non_output_sample )
{
    uint32_t sample_offset = !non_output_sample ? cts - dts : ISOM_NON_OUTPUT_SAMPLE_OFFSET;
    if( LSMASH_IS_EXISTING_BOX( stbl->ctts ) )
        return stbl_add_sample_offset( stbl, sample_offset );
    return sample_offset != 0 ? stbl_add_initial_sample_offset( stbl, sample_offset ) : 0;
}

/* Chimera specification boundary for Composition Time to Sample (`ctts`):
 *
 * `ctts` `sample_offset` has three distinct specification regimes:
 * 1. ISOBMFF `ctts` version 0 (ISO/IEC 14496-12 prior to `iso4`):
 *    `sample_offset` is an unsigned 32-bit integer (`uint32_t`). Negative composition
 *    offsets (`cts < dts`) are forbidden at the `ctts` level and must instead be
 *    compensated via an Edit List (`elst`).
 * 2. ISOBMFF `ctts` version 1 (`iso4` and later brands):
 *    `sample_offset` is a signed 32-bit two's-complement integer (`int32_t`), with
 *    `INT32_MIN` (`0x80000000`, `ISOM_NON_OUTPUT_SAMPLE_OFFSET`) reserved to signal
 *    non-output samples.
 * 3. QuickTime File Format (QTFF):
 *    QTFF only defines `ctts` version 0, yet defines `sample_offset` as a SIGNED
 *    32-bit two's-complement integer (`int32_t`) and pairs it with the `cslg`
 *    (Composition Shift Least Greatest) atom to signal the decode-to-composition
 *    timeline shift (`ctd_shift`).
 *
 * Therefore, if `cts < dts` is encountered in a hybrid ISOBMFF+QTFF file where
 * `max_isom_version >= 4` and `qt_compatible == 1`, we cannot use `ctts` v1
 * (undefined in QTFF) nor negative offsets in `ctts` v0 (illegal in ISOBMFF v0),
 * so `stbl_check_sample_offset_compatibility` rejects the incompatible combination. */
static int stbl_check_sample_offset_compatibility( lsmash_file_t *file, uint64_t dts, uint64_t cts, int non_output_sample )
{
    if( non_output_sample )
    {
        if( file->min_isom_version < 4 )
            return LSMASH_ERR_INVALID_DATA; /* Non-output sample can be indicated under 'iso4' or later brands. */
    }
    else
    {
        if( file->isom_compatible && file->qt_compatible && (((cts >= dts) ? (cts - dts) : (dts - cts)) > INT32_MAX) )
            return LSMASH_ERR_INVALID_DATA; /* sample_offset is not compatible with both ISOBMFF and QTFF. */
    }
    if( non_output_sample || cts < dts )
    {
        /* Negative sample offset is required. */
        if( file->max_isom_version <  4 && !file->qt_compatible )
            return LSMASH_ERR_INVALID_DATA; /* Negative sample offset is not supported in both ISOBMFF and QTFF. */
        if( file->max_isom_version >= 4 &&  file->qt_compatible )
            return LSMASH_ERR_INVALID_DATA; /* ctts version 1 is not defined in QTFF. */
    }
    return 0;
}

void lsm_stbl_update_cache_timestamp
(
    isom_cache_t *cache,
    uint64_t      dts,
    uint64_t      cts,
    int32_t       ctd_shift,
    uint32_t      sample_duration,
    int           non_output_sample
)
{
    cache->timestamp.dts       = dts;
    cache->timestamp.cts       = non_output_sample ? cache->timestamp.cts : cts;
    cache->timestamp.ctd_shift = ctd_shift;
    if( cache->fragment )
    {
        cache->fragment->last_duration = sample_duration;
        if( !non_output_sample )
            cache->fragment->largest_cts
                = cache->fragment->largest_cts != LSMASH_TIMESTAMP_UNDEFINED
                ? LSMASH_MAX( cache->timestamp.cts, cache->fragment->largest_cts )
                : cache->timestamp.cts;
    }
}

static int stbl_add_timestamp( isom_stbl_t *stbl, isom_cache_t *cache, lsmash_file_t *file, uint64_t dts, uint64_t cts )
{
    if( !cache || !stbl->stts->list )
        return LSMASH_ERR_INVALID_DATA;
    int non_output_sample = (cts == LSMASH_TIMESTAMP_UNDEFINED);
    int err = stbl_check_sample_offset_compatibility( file, dts, cts, non_output_sample );
    if( err < 0 )
        return err;
    uint32_t sample_count = stbl_get_sample_count_from_sample_table( stbl );
    uint32_t sample_delta = sample_count > 1 ? stbl_add_dts( stbl, dts, cache->timestamp.dts ) : 0;
    if( sample_count > 1 && sample_delta == 0 )
        return LSMASH_ERR_INVALID_DATA;
    if( (err = stbl_add_cts( stbl, dts, cts, non_output_sample )) < 0 )
        return err;
    int32_t ctd_shift = cache->timestamp.ctd_shift;
    if( !non_output_sample && ((cts + ctd_shift) < dts) )
    {
        /* Check overflow of composition to decode timeline shift. */
        if( (dts - cts) > INT32_MAX )
            return LSMASH_ERR_INVALID_DATA;
        assert( LSMASH_IS_EXISTING_BOX( stbl->ctts ) );
        if( stbl->ctts->version == 0 && !file->qt_compatible )
            stbl->ctts->version = 1;
        ctd_shift = dts - cts;
    }
    lsm_stbl_update_cache_timestamp( cache, dts, cts, ctd_shift, sample_delta, non_output_sample );
    return 0;
}

static int stbl_add_sync_point( isom_stbl_t *stbl, isom_cache_t *cache, uint32_t sample_number, lsmash_sample_property_t *prop )
{
    if( !(prop->ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC) )   /* no null check for prop */
    {
        if( !cache->all_sync )
            return 0;
        if( LSMASH_IS_NON_EXISTING_BOX( stbl->stss )
         && LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_stss( stbl ) ) )
            return LSMASH_ERR_NAMELESS;
        int err = stbl_add_stss_entry( stbl, 1 );
        if( err < 0 )   /* Declare here the first sample is a sync sample. */
            return err;
        cache->all_sync = 0;
        return 0;
    }
    if( cache->all_sync )     /* We don't need stss box if all samples are sync sample. */
        return 0;
    if( LSMASH_IS_NON_EXISTING_BOX( stbl->stss ) )
    {
        if( stbl_get_sample_count_from_sample_table( stbl ) == 1 )
        {
            cache->all_sync = 1;    /* Also the first sample is a sync sample. */
            return 0;
        }
        if( LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_stss( stbl ) ) )
            return LSMASH_ERR_NAMELESS;
    }
    return stbl_add_stss_entry( stbl, sample_number );
}

static int stbl_add_partial_sync( isom_stbl_t *stbl, lsmash_file_t *file, uint32_t sample_number, lsmash_sample_property_t *prop )
{
    if( !file->qt_compatible )
        return 0;
    if( !(prop->ra_flags & QT_SAMPLE_RANDOM_ACCESS_FLAG_PARTIAL_SYNC) )
        return 0;
    /* This sample is a partial sync sample. */
    if( LSMASH_IS_NON_EXISTING_BOX( stbl->stps )
     && LSMASH_IS_BOX_ADDITION_FAILURE( lsm_box_add_stps( stbl ) ) )
        return LSMASH_ERR_NAMELESS;
    return stbl_add_stps_entry( stbl, sample_number );
}

int lsm_stbl_rap_grouping_established( isom_rap_group_t *group, int num_leading_samples_known, isom_sgpd_t *sgpd, int is_fragment )
{
    isom_rap_entry_t *rap = group->random_access;
    if( !rap )
        return 0;
    assert( rap == (isom_rap_entry_t *)lsm_list_tail( sgpd->list ) );
    rap->num_leading_samples_known = num_leading_samples_known;
    /* Avoid duplication of sample group descriptions. */
    uint32_t group_description_index = is_fragment ? 0x10001 : 1;
    uint32_t sgpd_count = lsm_list_count( sgpd->list );
    for( uint32_t i = 0; i + 1 < sgpd_count; i++ )
    {
        isom_rap_entry_t *data = (isom_rap_entry_t *)lsm_list_at( sgpd->list, i );
        if( !data )
            return LSMASH_ERR_INVALID_DATA;
        if( rap->num_leading_samples_known == data->num_leading_samples_known
         && rap->num_leading_samples       == data->num_leading_samples )
        {
            /* The same description already exists.
             * Remove the latest random access entry. */
            lsm_list_remove_tail( sgpd->list );
            /* Replace assigned group_description_index with the one corresponding the same description. */
            if( group->assignment->group_description_index == 0 )
            {
                /* We don't create consecutive sample groups not assigned to 'rap '.
                 * So the previous sample group shall be a group of 'rap ' if any. */
                if( group->prev_assignment )
                {
                    assert( group->prev_assignment->group_description_index );
                    group->prev_assignment->group_description_index = group_description_index;
                }
            }
            else
                group->assignment->group_description_index = group_description_index;
            break;
        }
        ++group_description_index;
    }
    group->random_access = NULL;
    return 0;
}

int lsm_stbl_group_random_access( isom_box_t *parent, isom_cache_t *cache, lsmash_sample_t *sample )
{
    if( parent->file->max_isom_version < 6 )
        return 0;
    isom_sbgp_t  *sbgp;
    isom_sgpd_t  *sgpd;
    uint32_t      sample_count;
    int           is_fragment;
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STBL ) )
    {
        isom_stbl_t *stbl = (isom_stbl_t *)parent;
        sbgp  = lsm_stbl_get_sample_to_group         ( stbl, ISOM_GROUP_TYPE_RAP );
        sgpd  = lsm_stbl_get_sample_group_description( stbl, ISOM_GROUP_TYPE_RAP );
        sample_count = stbl_get_sample_count_from_sample_table( stbl );
        is_fragment  = 0;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAF ) )
    {
        isom_traf_t *traf = (isom_traf_t *)parent;
        sbgp  = lsm_stbl_get_fragment_sample_to_group         ( traf, ISOM_GROUP_TYPE_RAP );
        sgpd  = lsm_stbl_get_fragment_sample_group_description( traf, ISOM_GROUP_TYPE_RAP );
        sample_count = cache->fragment->sample_count + 1;   /* Cached sample_count is incremented later in lsm_fragment_update_cache(). */
        is_fragment  = 1;
    }
    else
    {
        assert( 0 );
        sbgp  = lsm_box_default_non_existing_sbgp();
        sgpd  = lsm_box_default_non_existing_sgpd();
        /* redundant initializations to suppress warnings from unclever compilers */
        sample_count = 0;
        is_fragment  = 0;
    }
    if( LSMASH_IS_NON_EXISTING_BOX( sbgp )
     || LSMASH_IS_NON_EXISTING_BOX( sgpd ) )
        return 0;
    lsmash_sample_property_t *prop = &sample->prop;
    uint8_t is_rap = (prop->ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC)
                  || (prop->ra_flags & QT_SAMPLE_RANDOM_ACCESS_FLAG_PARTIAL_SYNC)
                  || (prop->ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_RAP)
                  || (LSMASH_IS_POST_ROLL_START( prop->ra_flags ) && prop->post_roll.identifier == prop->post_roll.complete);
    isom_rap_group_t *group = cache->rap;
    if( !group )
    {
        /* This sample is the first sample, create a grouping cache. */
        assert( sample_count == 1 );
        group = lsmash_malloc( sizeof(isom_rap_group_t) );
        if( !group )
            return LSMASH_ERR_MEMORY_ALLOC;
        if( is_rap )
        {
            group->random_access = stbl_add_rap_group_entry( sgpd );
            group->assignment    = stbl_add_group_assignment_entry( sbgp, 1, lsm_list_count( sgpd->list ) + (is_fragment ? 0x10000 : 0) );
        }
        else
        {
            /* The first sample is not always a random access point. */
            group->random_access = NULL;
            group->assignment    = stbl_add_group_assignment_entry( sbgp, 1, 0 );
        }
        if( !group->assignment )
        {
            lsmash_free( group );
            return LSMASH_ERR_MEMORY_ALLOC;
        }
        group->prev_assignment = NULL;
        group->is_prev_rap     = is_rap;
        cache->rap             = group;
        return 0;
    }
    int err;
    if( group->is_prev_rap )
    {
        /* OK. here, the previous sample is a menber of 'rap '. */
        if( !is_rap )
        {
            /* This sample isn't a member of 'rap ' and the previous sample is.
             * So we create a new group and set 0 on its group_description_index. */
            group->prev_assignment = group->assignment;
            group->assignment      = stbl_add_group_assignment_entry( sbgp, 1, 0 );
            if( !group->assignment )
            {
                lsmash_free( group );
                return LSMASH_ERR_MEMORY_ALLOC;
            }
        }
        else if( !LSMASH_IS_CLOSED_RAP( prop->ra_flags ) )
        {
            /* Create a new group since there is the possibility the next sample is a leading sample.
             * This sample is a member of 'rap ', so we set appropriate value on its group_description_index. */
            if( (err = lsm_stbl_rap_grouping_established( group, 1, sgpd, is_fragment )) < 0 )
                return err;
            group->random_access   = stbl_add_rap_group_entry( sgpd );
            group->prev_assignment = group->assignment;
            group->assignment      = stbl_add_group_assignment_entry( sbgp, 1, lsm_list_count( sgpd->list ) + (is_fragment ? 0x10000 : 0) );
            if( !group->assignment )
            {
                lsmash_free( group );
                return LSMASH_ERR_MEMORY_ALLOC;
            }
        }
        else    /* The previous and current sample are a member of 'rap ', and the next sample must not be a leading sample. */
            ++ group->assignment->sample_count;
    }
    else if( is_rap )
    {
        /* This sample is a member of 'rap ' and the previous sample isn't.
         * So we create a new group and set appropriate value on its group_description_index. */
        if( (err = lsm_stbl_rap_grouping_established( group, 1, sgpd, is_fragment )) < 0 )
            return err;
        group->random_access   = stbl_add_rap_group_entry( sgpd );
        group->prev_assignment = group->assignment;
        group->assignment      = stbl_add_group_assignment_entry( sbgp, 1, lsm_list_count( sgpd->list ) + (is_fragment ? 0x10000 : 0) );
        if( !group->assignment )
        {
            lsmash_free( group );
            return LSMASH_ERR_MEMORY_ALLOC;
        }
    }
    else    /* The previous and current sample aren't a member of 'rap '. */
        ++ group->assignment->sample_count;
    /* Obtain the property of the latest random access point group. */
    if( !is_rap && group->random_access )
    {
        if( prop->leading == ISOM_SAMPLE_LEADING_UNKNOWN )
        {
            /* We can no longer know num_leading_samples in this group. */
            if( (err = lsm_stbl_rap_grouping_established( group, 0, sgpd, is_fragment )) < 0 )
                return err;
        }
        else
        {
            if( prop->leading == ISOM_SAMPLE_IS_UNDECODABLE_LEADING
             || prop->leading == ISOM_SAMPLE_IS_DECODABLE_LEADING )
                ++ group->random_access->num_leading_samples;
            /* no more consecutive leading samples in this group */
            else if( (err = lsm_stbl_rap_grouping_established( group, 1, sgpd, is_fragment )) < 0 )
                return err;
        }
    }
    group->is_prev_rap = is_rap;
    return 0;
}

static isom_group_assignment_entry_t *stbl_get_roll_assignment( isom_roll_group_t *group )
{
    return group && group->sbgp
         ? (isom_group_assignment_entry_t *)lsm_list_get( group->sbgp->list, group->assignment_number )
         : NULL;
}

static int stbl_roll_grouping_established( isom_roll_group_t *group )
{
    isom_group_assignment_entry_t *assignment = stbl_get_roll_assignment( group );
    if( !assignment )
        return LSMASH_ERR_INVALID_DATA;
    /* Avoid duplication of sample group descriptions. */
    isom_sgpd_t *sgpd = group->sgpd;
    uint32_t group_description_index = group->is_fragment ? 0x10001 : 1;
    isom_roll_entry_t *data;
    LSM_LIST_FOR_EACH( data, sgpd->list )
    {
        if( !data )
            return LSMASH_ERR_INVALID_DATA;
        if( group->roll_distance == data->roll_distance )
        {
            /* The same description already exists.
             * Set the group_description_index corresponding the same description. */
            assignment->group_description_index = group_description_index;
            return 0;
        }
        ++group_description_index;
    }
    /* Add a new roll recovery description. */
    if( !stbl_add_roll_group_entry( sgpd, group->roll_distance ) )
        return LSMASH_ERR_MEMORY_ALLOC;
    assignment->group_description_index = lsm_list_count( sgpd->list ) + (group->is_fragment ? 0x10000 : 0);
    return 0;
}

/* Roll Recovery Sample Group (`sbgp` / `sgpd`) Deferred Deduplication:
 *
 * Why can't adjacent `sbgp` group assignment entries be merged immediately as samples
 * arrive?
 * For post-roll recovery (e.g., H.264/HEVC GDR recovery points), the exact recovery
 * distance (`roll_distance`) or completeness of a group is not known when the first
 * sample of the group is appended; it is only determined once the target recovery sample
 * arrives later in the stream (`group->described == ROLL_DISTANCE_DETERMINED`) and the
 * group boundary is closed (`group->delimited == 1`).
 *
 * Consequently, in-flight groups are buffered in `cache->roll.pool` while placeholder
 * assignment entries are appended to `sbgp->list`. Each pooled `isom_roll_group_t`
 * references its corresponding `sbgp->list` slot by 1-based index (`assignment_number`)
 * rather than a raw pointer, because `lsm_list_remove()` shifts subsequent slots.
 *
 * `current_group_number` starts at `lsm_list_count(sbgp->list) - lsm_list_count(pool) + 1`
 * (the 1-based index of the oldest still-pooled group in `sbgp->list`), and `prev_assignment`
 * points to the already-finalized entry immediately preceding the pool (`current_group_number - 1`).
 * Whenever a newly determined group has the same `group_description_index` as `prev_assignment`,
 * its `sample_count` is folded into `prev_assignment`, the redundant `sbgp->list` entry is
 * removed, and `next_group->assignment_number` is decremented for all remaining pooled groups
 * to keep their 1-based indices synchronized with the compacted `sbgp->list`. */
static int stbl_deduplicate_roll_group( isom_sbgp_t *sbgp, lsm_list_t *pool )
{
    /* Deduplication */
    uint32_t current_group_number = lsm_list_count( sbgp->list ) - lsm_list_count( pool ) + 1;
    isom_group_assignment_entry_t *prev_assignment = (isom_group_assignment_entry_t *)lsm_list_get( sbgp->list, current_group_number - 1 );
    for( uint32_t i = 0; i < lsm_list_count( pool ); )
    {
        isom_roll_group_t *group = (isom_roll_group_t *)lsm_list_at( pool, i );
        isom_group_assignment_entry_t *assignment = stbl_get_roll_assignment( group );
        if( !group || !assignment )
            return LSMASH_ERR_INVALID_DATA;
        if( !group->delimited || group->described != ROLL_DISTANCE_DETERMINED )
            return 0;
        if( prev_assignment && prev_assignment->group_description_index == assignment->group_description_index )
        {
            /* Merge the current group with the previous. */
            prev_assignment->sample_count += assignment->sample_count;
            int err;
            if( (err = lsm_list_remove( sbgp->list, current_group_number )) < 0
             || (err = lsm_list_remove_at( pool, i ))                       < 0 )
                return err;
            for( uint32_t j = i; j < lsm_list_count( pool ); j++ )
            {
                isom_roll_group_t *next_group = (isom_roll_group_t *)lsm_list_at( pool, j );
                if( next_group && next_group->assignment_number > current_group_number )
                    --next_group->assignment_number;
            }
            prev_assignment = (isom_group_assignment_entry_t *)lsm_list_get( sbgp->list, current_group_number - 1 );
        }
        else
        {
            prev_assignment = assignment;
            ++current_group_number;
            ++i;
        }
    }
    return 0;
}

/* Remove pooled caches that has become unnecessary. */
static int stbl_clean_roll_pool( lsm_list_t *pool )
{
    while( lsm_list_count( pool ) > 0 )
    {
        isom_roll_group_t *group = (isom_roll_group_t *)lsm_list_head( pool );
        if( !group )
            return LSMASH_ERR_INVALID_DATA;
        if( !group->delimited || group->described != ROLL_DISTANCE_DETERMINED )
            return 0;
        int err = lsm_list_remove_at( pool, 0 );
        if( err < 0 )
            return err;
    }
    return 0;
}

static int stbl_flush_roll_pool( isom_sbgp_t *sbgp, lsm_list_t *pool )
{
    int err;
    isom_roll_group_t *group;
    LSM_LIST_FOR_EACH( group, pool )
    {
        if( !group )
            return LSMASH_ERR_INVALID_DATA;
        if( group->delimited
         && group->described == ROLL_DISTANCE_DETERMINED
         && group->roll_distance != 0
         && (err = stbl_roll_grouping_established( group )) < 0 )
            return err;
    }
    if( (err = stbl_deduplicate_roll_group( sbgp, pool )) < 0 )
        return err;
    return stbl_clean_roll_pool( pool );
}

static int stbl_all_recovery_described( isom_sbgp_t *sbgp, lsm_list_t *pool )
{
    isom_roll_group_t *group;
    LSM_LIST_FOR_EACH( group, pool )
    {
        if( !group )
            return LSMASH_ERR_INVALID_DATA;
        group->described = ROLL_DISTANCE_DETERMINED;
    }
    return stbl_flush_roll_pool( sbgp, pool );
}

int lsm_stbl_all_recovery_completed( isom_sbgp_t *sbgp, lsm_list_t *pool )
{
    isom_roll_group_t *group;
    LSM_LIST_FOR_EACH( group, pool )
    {
        if( !group )
            return LSMASH_ERR_INVALID_DATA;
        group->described = ROLL_DISTANCE_DETERMINED;
        group->delimited = 1;
    }
    return stbl_flush_roll_pool( sbgp, pool );
}

static isom_roll_entry_t *stbl_get_roll_description
(
    isom_roll_group_t *group
)
{
    isom_group_assignment_entry_t *assignment = stbl_get_roll_assignment( group );
    if( !assignment )
        return NULL;
    uint32_t group_description_index = assignment->group_description_index;
    if( group_description_index && group->is_fragment )
    {
        assert( group_description_index > 0x10000 );
        group_description_index -= 0x10000;
    }
    return (isom_roll_entry_t *)lsm_list_get( group->sgpd->list, group_description_index );
}

int lsm_stbl_group_roll_recovery( isom_box_t *parent, isom_cache_t *cache, lsmash_sample_t *sample )
{
    if( !parent->file->avc_extensions
     && !parent->file->qt_compatible )
        return 0;
    uint32_t sample_count;
    int      is_fragment;
    lsmash_entry_list_t *sbgp_list;
    lsmash_entry_list_t *sgpd_list;
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STBL ) )
    {
        isom_stbl_t *stbl = (isom_stbl_t *)parent;
        sbgp_list = &stbl->sbgp_list;
        sgpd_list = &stbl->sgpd_list;
        sample_count = stbl_get_sample_count_from_sample_table( stbl );
        is_fragment  = 0;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAF ) )
    {
        if( parent->file->max_isom_version < 6 )
            return 0;
        isom_traf_t *traf = (isom_traf_t *)parent;
        sbgp_list = &traf->sbgp_list;
        sgpd_list = &traf->sgpd_list;
        sample_count = cache->fragment->sample_count + 1;   /* Cached sample_count is incremented later in lsm_fragment_update_cache(). */
        is_fragment  = 1;
    }
    else
    {
        assert( 0 );
        return LSMASH_ERR_INVALID_DATA;
    }
    isom_sbgp_t *sbgp = lsm_stbl_get_roll_recovery_sample_to_group         ( sbgp_list );
    isom_sgpd_t *sgpd = lsm_stbl_get_roll_recovery_sample_group_description( sgpd_list );
    if( LSMASH_IS_NON_EXISTING_BOX( sbgp )
     || LSMASH_IS_NON_EXISTING_BOX( sgpd )
     || sbgp->grouping_type != sgpd->grouping_type )
        return 0;
    /* Check if 'roll' -> 'prol' conversion is needed. */
    if( cache->is_audio
     && sbgp->grouping_type == ISOM_GROUP_TYPE_ROLL
     && !(sample->prop.ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC) )
    {
        /* Since not every samples is a sync sample, change grouping_type into 'prol'. */
        sbgp->grouping_type = ISOM_GROUP_TYPE_PROL;
        sgpd->grouping_type = ISOM_GROUP_TYPE_PROL;
    }
    lsm_list_t *pool = cache->roll.pool;
    if( !pool )
    {
        pool = lsm_list_create_simple( sizeof(isom_roll_group_t) );
        if( !pool )
            return LSMASH_ERR_MEMORY_ALLOC;
        cache->roll.pool = pool;
    }
    lsmash_sample_property_t *prop  = &sample->prop;
    isom_roll_group_t        *group = (isom_roll_group_t *)lsm_list_tail( pool );
    int is_recovery_start = LSMASH_IS_POST_ROLL_START( prop->ra_flags );
    int valid_pre_roll = !is_recovery_start
                      && (prop->ra_flags != ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE)
                      && (prop->pre_roll.distance > 0)
                      && (prop->pre_roll.distance <= -INT16_MIN);
    int new_group = !group || is_recovery_start || (group->prev_is_recovery_start != is_recovery_start);
    if( !new_group )
    {
        /* Check pre-roll distance. */
        assert( stbl_get_roll_assignment( group ) && group->sgpd );
        isom_roll_entry_t *prev_roll = stbl_get_roll_description( group );
        if( !prev_roll )
            new_group = valid_pre_roll;
        else if( !valid_pre_roll || (prop->pre_roll.distance != -prev_roll->roll_distance) )
            /* Pre-roll distance is different from the previous. */
            new_group = 1;
    }
    if( new_group )
    {
        if( group )
            group->delimited = 1;
        else
            assert( sample_count == 1 );
        /* Create a new group. */
        if( !stbl_add_group_assignment_entry( sbgp, 1, 0 ) )
            return LSMASH_ERR_MEMORY_ALLOC;
        group = (isom_roll_group_t *)lsm_list_append( pool );
        if( !group )
        {
            lsm_list_remove_tail( sbgp->list );
            return LSMASH_ERR_MEMORY_ALLOC;
        }
        group->sbgp                   = sbgp;
        group->sgpd                   = sgpd;
        group->assignment_number      = lsm_list_count( sbgp->list );
        group->prev_is_recovery_start = is_recovery_start;
        group->is_fragment            = is_fragment;
        if( is_recovery_start )
        {
            /* a member of non-roll or post-roll group */
            group->first_sample   = sample_count;
            group->recovery_point = prop->post_roll.complete;
        }
        else
        {
            group->described = ROLL_DISTANCE_DETERMINED;
            if( valid_pre_roll )
            {
                /* a member of pre-roll group */
                group->roll_distance = -(signed)prop->pre_roll.distance;
                int err = stbl_roll_grouping_established( group );
                if( err < 0 )
                    return err;
            }
            else
                /* a member of non-roll group */
                group->roll_distance = 0;
        }
    }
    else
    {
        isom_group_assignment_entry_t *assignment = stbl_get_roll_assignment( group );
        if( !assignment )
            return LSMASH_ERR_INVALID_DATA;
        group->prev_is_recovery_start = is_recovery_start;
        assignment->sample_count     += 1;
    }
    /* If encountered a RAP, all recovery is completed here. */
    if( prop->ra_flags & (ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC
                        | ISOM_SAMPLE_RANDOM_ACCESS_FLAG_RAP
                        |   QT_SAMPLE_RANDOM_ACCESS_FLAG_PARTIAL_SYNC) )
        return stbl_all_recovery_described( sbgp, pool );
    /* Check whether this sample is a random access recovery point or not. */
    for( uint32_t i = 0; i < lsm_list_count( pool ); i++ )
    {
        group = (isom_roll_group_t *)lsm_list_at( pool, i );
        if( !group )
            return LSMASH_ERR_INVALID_DATA;
        if( group->described == ROLL_DISTANCE_DETERMINED )
            continue;
        if( group->described == ROLL_DISTANCE_INITIALIZED )
        {
            /* Let's consider the following picture sequence.
             *   coded order : P[0] P[1] P[2] P[3] P[4] P[5]
             *   DTS         :   0    1    2    3    4    5
             *   CTS         :   2    4    3    6    7    5
             * Here, P[0] conveys a recovery point SEI and P[3] is the recovery point.
             * Correctness of decoded pictures is specified by recovery point in output order for both AVC and HEVC.
             * Therefore, as follows,
             *   output order : P[0] P[2] P[1] P[5]|P[3] P[4]
             *                  ---(incorrect?)--->|
             * there is no guarantee that P[5] is decoded and output correctly.
             * From this, it can be said that the roll_distance of this sequence is equal to 5. */
            isom_roll_entry_t *post_roll = stbl_get_roll_description( group );
            if( post_roll && post_roll->roll_distance > 0 )
            {
                if( sample->cts   != LSMASH_TIMESTAMP_UNDEFINED
                 && group->rp_cts != LSMASH_TIMESTAMP_UNDEFINED
                 && group->rp_cts > sample->cts )
                    /* Updated roll_distance for composition reordering. */
                    post_roll->roll_distance = sample_count - group->first_sample;
                if( ++ group->wait_and_see_count >= MAX_ROLL_WAIT_AND_SEE_COUNT )
                    group->described = ROLL_DISTANCE_DETERMINED;
            }
        }
        else if( prop->post_roll.identifier == group->recovery_point )
        {
            int16_t distance = sample_count - group->first_sample;
            group->rp_cts        = sample->cts;
            group->roll_distance = distance;
            /* Add a roll recovery entry only when roll_distance isn't zero since roll_distance = 0 must not be used. */
            if( distance )
            {
                /* Now, this group is a 'roll'.
                 * The roll_distance may be updated later because of composition reordering. */
                group->described          = ROLL_DISTANCE_INITIALIZED;
                group->wait_and_see_count = 0;
                /* All groups with uninitialized roll_distance before the current group are described. */
                for( uint32_t j = 0; j < i; j++ )
                {
                    isom_roll_group_t *prev_group = (isom_roll_group_t *)lsm_list_at( pool, j );
                    if( !prev_group || prev_group->described != ROLL_DISTANCE_INITIALIZED )
                        continue;
                    prev_group->described = ROLL_DISTANCE_DETERMINED;
                }
                /* Cache the mark of the first recovery point in a subsegment. */
                if( cache->fragment
                 && cache->fragment->subsegment.first_rp_number == 0 )
                    cache->fragment->subsegment.is_first_recovery_point = 1;
            }
            else
                /* Random Accessible Point */
                return stbl_all_recovery_described( sbgp, pool );
        }
    }
    return stbl_flush_roll_pool( sbgp, pool );
}

int lsm_stbl_update_chunk_tables
(
    isom_stbl_t   *stbl,
    lsmash_file_t *media_file,
    isom_chunk_t  *current
)
{
    isom_stsc_entry_t *last_stsc_data = (isom_stsc_entry_t *)lsm_list_tail( stbl->stsc->list );
    /* Create a new chunk sequence in this track if needed. */
    int err;
    if( (!last_stsc_data
      || current->pool->sample_count       != last_stsc_data->samples_per_chunk
      || current->sample_description_index != last_stsc_data->sample_description_index)
     && (err = lsm_stbl_add_stsc_entry( stbl, current->chunk_number,
                                              current->pool->sample_count,
                                              current->sample_description_index )) < 0 )
        return err;
    /* Add a new chunk offset in this track. */
    uint64_t offset = media_file->size;
    if( media_file->fragment )
        offset += ISOM_BASEBOX_COMMON_SIZE + media_file->fragment->pool_size;
    return lsm_stbl_add_stco_entry( stbl, offset );
}

int lsm_stbl_update_sample_tables
(
    isom_trak_t         *trak,
    lsmash_sample_t     *sample,
    uint32_t            *samples_per_packet,
    isom_sample_entry_t *sample_entry
)
{
    int err;
    isom_audio_entry_t *audio = (isom_audio_entry_t *)sample_entry;
    if( (audio->manager & LSMASH_AUDIO_DESCRIPTION)
     && (audio->manager & LSMASH_QTFF_BASE)
     && (audio->version == 1)
     && (audio->compression_ID != QT_AUDIO_COMPRESSION_ID_VARIABLE_COMPRESSION) )
    {
        /* Add entries of the sample table for each uncompressed sample. */
        uint64_t sample_duration = trak->mdia->mdhd->timescale / (audio->samplerate >> 16);
        if( audio->samplesPerPacket == 0 || sample_duration == 0 || sample->cts == LSMASH_TIMESTAMP_UNDEFINED )
            return LSMASH_ERR_INVALID_DATA;
        uint64_t sample_dts = sample->dts;
        uint64_t sample_cts = sample->cts;
        isom_stbl_t *stbl = trak->mdia->minf->stbl;
        for( uint32_t i = 0; i < audio->samplesPerPacket; i++ )
        {
            /* Add a size of uncomressed audio and increment sample_count.
             * This points to individual uncompressed audio samples, each one byte in size, within the compressed frames. */
            uint32_t sample_count = stbl_add_size( stbl, 1 );
            if( sample_count == 0 )
                return LSMASH_ERR_NAMELESS;
            /* Add a decoding timestamp and a composition timestamp. */
            if( (err = stbl_add_timestamp( stbl, trak->cache, trak->file, sample_dts, sample_cts )) < 0 )
                return err;
            sample_dts += sample_duration;
            sample_cts += sample_duration;
        }
        *samples_per_packet = audio->samplesPerPacket;
    }
    else
    {
        isom_stbl_t *stbl = trak->mdia->minf->stbl;
        /* Add a sample_size and increment sample_count. */
        uint32_t sample_count = stbl_add_size( stbl, sample->length );
        if( sample_count == 0 )
            return LSMASH_ERR_NAMELESS;
        /* Add a decoding timestamp and a composition timestamp. */
        if( (err = stbl_add_timestamp( stbl, trak->cache, trak->file, sample->dts, sample->cts )) < 0 )
            return err;
        /* Add a sync point if needed. */
        if( (err = stbl_add_sync_point( stbl, trak->cache, sample_count, &sample->prop )) < 0 )
            return err;
        /* Add a partial sync point if needed. */
        if( (err = stbl_add_partial_sync( stbl, trak->file, sample_count, &sample->prop )) < 0 )
            return err;
        /* Add leading, independent, disposable and redundant information if needed. */
        if( stbl->add_dependency_type
         && (err = stbl->add_dependency_type( stbl, trak->file, &sample->prop )) < 0 )
            return err;
        /* Group samples into random access point type if needed. */
        if( (err = lsm_stbl_group_random_access( (isom_box_t *)stbl, trak->cache, sample )) < 0 )
            return err;
        /* Group samples into random access recovery point type if needed. */
        if( (err = lsm_stbl_group_roll_recovery( (isom_box_t *)stbl, trak->cache, sample )) < 0 )
            return err;
        *samples_per_packet = 1;
    }
    return 0;
}
