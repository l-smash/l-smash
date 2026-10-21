/*****************************************************************************
 * stbl.h
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

#ifndef LSMASH_STBL_H
#define LSMASH_STBL_H

struct isom_rap_group_tag
{
    isom_group_assignment_entry_t *assignment;          /* the address corresponding to the entry in Sample to Group Box */
    isom_group_assignment_entry_t *prev_assignment;     /* the address of the previous assignment */
    isom_rap_entry_t              *random_access;       /* the address corresponding to the random access entry in Sample Group Description Box */
    uint8_t                        is_prev_rap;         /* whether the previous sample is a random access point or not */
};

typedef struct
{
    isom_sbgp_t *sbgp;                              /* the address to the active Sample to Group Box */
    isom_sgpd_t *sgpd;                              /* the address to the active Sample Group Description Box */
    uint32_t     assignment_number;                 /* 1-based index of the entry in Sample to Group Box */
    uint32_t first_sample;                          /* the number of the first sample of the group */
    uint32_t recovery_point;                        /* the identifier necessary for the recovery from its starting point to be completed */
    uint64_t rp_cts;                                /* the CTS of the recovery point */
    int16_t  roll_distance;                         /* the current roll_distance
                                                     * The value may be updated when 'described' is set to ROLL_DISTANCE_INITIALIZED. */
#define MAX_ROLL_WAIT_AND_SEE_COUNT 64
    uint8_t  wait_and_see_count;                    /* Wait-and-see after initialization of roll_distance until reaching MAX_ROLL_WAIT_AND_SEE. */
    uint8_t  is_fragment;                           /* the flag if the current group is in fragment */
    uint8_t  prev_is_recovery_start;                /* whether the previous sample is a starting point of recovery or not */
    uint8_t  delimited;                             /* the flag if the sample_count is determined */
#define ROLL_DISTANCE_INITIALIZED 1
#define ROLL_DISTANCE_DETERMINED  2
    uint8_t  described;                             /* the status of the group description */
} isom_roll_group_t;

int lsm_stbl_add_stts_entry( isom_stbl_t *stbl, uint32_t sample_delta );
int lsm_stbl_add_stsc_entry( isom_stbl_t *stbl, uint32_t first_chunk, uint32_t samples_per_chunk, uint32_t sample_description_index );
int lsm_stbl_convert_stco_to_co64( isom_stbl_t *stbl );
int lsm_stbl_add_stco_entry( isom_stbl_t *stbl, uint64_t chunk_offset );

isom_sgpd_t *lsm_stbl_get_sample_group_description( isom_stbl_t *stbl, uint32_t grouping_type );
isom_sbgp_t *lsm_stbl_get_sample_to_group( isom_stbl_t *stbl, uint32_t grouping_type );
isom_sgpd_t *lsm_stbl_get_roll_recovery_sample_group_description( lsmash_entry_list_t *list );
isom_sbgp_t *lsm_stbl_get_roll_recovery_sample_to_group( lsmash_entry_list_t *list );
isom_sgpd_t *lsm_stbl_get_fragment_sample_group_description( isom_traf_t *traf, uint32_t grouping_type );
isom_sbgp_t *lsm_stbl_get_fragment_sample_to_group( isom_traf_t *traf, uint32_t grouping_type );

uint32_t lsm_stbl_get_sample_count( isom_trak_t *trak );
uint64_t lsm_stbl_get_dts( isom_stts_t *stts, uint32_t sample_number );
int lsm_stbl_replace_last_sample_delta( isom_stbl_t *stbl, uint32_t sample_delta );

int lsm_stbl_calculate_bitrate_description
(
    isom_stbl_t *stbl,
    isom_mdhd_t *mdhd,
    uint32_t    *bufferSizeDB,
    uint32_t    *maxBitrate,
    uint32_t    *avgBitrate,
    uint32_t     sample_description_index
);

int lsm_stbl_is_variable_size( isom_stbl_t *stbl );
uint32_t lsm_stbl_get_first_sample_size( isom_stbl_t *stbl );
int lsm_stbl_update_bitrate_description( isom_mdia_t *mdia );

int lsm_stbl_add_sample_grouping( isom_box_t *parent, isom_grouping_type grouping_type );
int lsm_stbl_create_sample_grouping( isom_trak_t *trak, isom_grouping_type grouping_type );
int lsm_stbl_compress_sample_size_table( isom_stbl_t *stbl );
int lsm_stbl_add_dependency_type( isom_stbl_t *stbl, lsmash_file_t *file, lsmash_sample_property_t *prop );

void lsm_stbl_update_cache_timestamp
(
    isom_cache_t *cache,
    uint64_t      dts,
    uint64_t      cts,
    int32_t       ctd_shift,
    uint32_t      sample_duration,
    int           non_output_sample
);

int lsm_stbl_rap_grouping_established( isom_rap_group_t *group, int num_leading_samples_known, isom_sgpd_t *sgpd, int is_fragment );
int lsm_stbl_group_random_access( isom_box_t *parent, isom_cache_t *cache, lsmash_sample_t *sample );
int lsm_stbl_all_recovery_completed( isom_sbgp_t *sbgp, lsm_list_t *pool );
int lsm_stbl_group_roll_recovery( isom_box_t *parent, isom_cache_t *cache, lsmash_sample_t *sample );

int lsm_stbl_update_chunk_tables
(
    isom_stbl_t   *stbl,
    lsmash_file_t *media_file,
    isom_chunk_t  *current
);

int lsm_stbl_update_sample_tables
(
    isom_trak_t         *trak,
    lsmash_sample_t     *sample,
    uint32_t            *samples_per_packet,
    isom_sample_entry_t *sample_entry
);

#endif
