/*****************************************************************************
 * a52.c:
 *****************************************************************************
 * Copyright (C) 2012 L-SMASH project
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

#include "internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>

#include "box.h"

/***************************************************************************
    AC-3 tools
    ETSI TS 102 366 V1.2.1 (2008-08)
***************************************************************************/
#include "a52.h"

uint8_t *lsmash_create_ac3_specific_info( lsmash_ac3_specific_parameters_t *param, uint32_t *data_length )
{
#define AC3_SPECIFIC_BOX_LENGTH 11
    lsmash_bits_t bits = { 0 };
    lsmash_bs_t   bs   = { 0 };
    lsmash_bits_init( &bits, &bs );
    uint8_t buffer[AC3_SPECIFIC_BOX_LENGTH] = { 0 };
    bs.data  = buffer;
    bs.alloc = AC3_SPECIFIC_BOX_LENGTH;
    lsmash_bits_put( &bits, 32, AC3_SPECIFIC_BOX_LENGTH );  /* box size */
    lsmash_bits_put( &bits, 32, ISOM_BOX_TYPE_DAC3 );       /* box type: 'dac3' */
    lsmash_bits_put( &bits, 2, param->fscod );
    lsmash_bits_put( &bits, 5, param->bsid );
    lsmash_bits_put( &bits, 3, param->bsmod );
    lsmash_bits_put( &bits, 3, param->acmod );
    lsmash_bits_put( &bits, 1, param->lfeon );
    lsmash_bits_put( &bits, 5, param->frmsizecod >> 1 );
    lsmash_bits_put( &bits, 5, 0 );
    uint8_t *data = lsmash_bits_export_data( &bits, data_length );
    lsmash_bits_empty( &bits );
    return data;
#undef AC3_SPECIFIC_BOX_LENGTH
}

int lsmash_setup_ac3_specific_parameters_from_syncframe( lsmash_ac3_specific_parameters_t *param, uint8_t *data, uint32_t data_length )
{
    if( !data || data_length < AC3_MIN_SYNCFRAME_LENGTH )
        return -1;
    IF_A52_SYNCWORD( data )
        return -1;
    lsmash_bits_t bits = { 0 };
    lsmash_bs_t   bs   = { 0 };
    uint8_t buffer[AC3_MAX_SYNCFRAME_LENGTH] = { 0 };
    bs.data  = buffer;
    bs.alloc = AC3_MAX_SYNCFRAME_LENGTH;
    ac3_info_t  handler = { { 0 } };
    ac3_info_t *info    = &handler;
    memcpy( info->buffer, data, LSMASH_MIN( data_length, AC3_MAX_SYNCFRAME_LENGTH ) );
    info->bits = &bits;
    lsmash_bits_init( &bits, &bs );
    if( ac3_parse_syncframe_header( info, info->buffer ) )
        return -1;
    *param = info->dac3_param;
    return 0;
}

static int ac3_check_syncframe_header( lsmash_ac3_specific_parameters_t *param )
{
    if( param->fscod == 0x3 )
        return -1;      /* unknown Sample Rate Code */
    if( param->frmsizecod > 0x25 )
        return -1;      /* unknown Frame Size Code */
    if( param->bsid >= 10 )
        return -1;      /* might be EAC-3 */
    return 0;
}

int ac3_parse_syncframe_header( ac3_info_t *info, uint8_t *data )
{
    lsmash_bits_t *bits = info->bits;
    if( lsmash_bits_import_data( bits, data, AC3_MIN_SYNCFRAME_LENGTH ) )
        return -1;
    lsmash_ac3_specific_parameters_t *param = &info->dac3_param;
    lsmash_bits_get( bits, 32 );        /* syncword + crc1 */
    param->fscod      = lsmash_bits_get( bits, 2 );
    param->frmsizecod = lsmash_bits_get( bits, 6 );
    param->bsid       = lsmash_bits_get( bits, 5 );
    param->bsmod      = lsmash_bits_get( bits, 3 );
    param->acmod      = lsmash_bits_get( bits, 3 );
    if( (param->acmod & 0x01) && (param->acmod != 0x01) )
        lsmash_bits_get( bits, 2 );     /* cmixlev */
    if( param->acmod & 0x04 )
        lsmash_bits_get( bits, 2 );     /* surmixlev */
    if( param->acmod == 0x02 )
        lsmash_bits_get( bits, 2 );     /* dsurmod */
    param->lfeon = lsmash_bits_get( bits, 1 );
    lsmash_bits_empty( bits );
    return ac3_check_syncframe_header( param );
}

/***************************************************************************
    Enhanced AC-3 tools
    ETSI TS 102 366 V1.2.1 (2008-08)
***************************************************************************/

uint8_t *lsmash_create_eac3_specific_info( lsmash_eac3_specific_parameters_t *param, uint32_t *data_length )
{
#define EAC3_SPECIFIC_BOX_MAX_LENGTH 42
    if( param->num_ind_sub > 7 )
        return NULL;
    lsmash_bits_t bits = { 0 };
    lsmash_bs_t   bs   = { 0 };
    lsmash_bits_init( &bits, &bs );
    uint8_t buffer[EAC3_SPECIFIC_BOX_MAX_LENGTH] = { 0 };
    bs.data  = buffer;
    bs.alloc = EAC3_SPECIFIC_BOX_MAX_LENGTH;
    lsmash_bits_put( &bits, 32, 0 );                    /* box size */
    lsmash_bits_put( &bits, 32, ISOM_BOX_TYPE_DEC3 );   /* box type: 'dec3' */
    lsmash_bits_put( &bits, 13, param->data_rate );     /* data_rate; setup by isom_update_bitrate_description */
    lsmash_bits_put( &bits, 3, param->num_ind_sub );
    /* Apparently, the condition of this loop defined in ETSI TS 102 366 V1.2.1 (2008-08) is wrong. */
    for( int i = 0; i <= param->num_ind_sub; i++ )
    {
        lsmash_eac3_substream_info_t *independent_info = &param->independent_info[i];
        lsmash_bits_put( &bits, 2, independent_info->fscod );
        lsmash_bits_put( &bits, 5, independent_info->bsid );
        lsmash_bits_put( &bits, 5, independent_info->bsmod );
        lsmash_bits_put( &bits, 3, independent_info->acmod );
        lsmash_bits_put( &bits, 1, independent_info->lfeon );
        lsmash_bits_put( &bits, 3, 0 );          /* reserved */
        lsmash_bits_put( &bits, 4, independent_info->num_dep_sub );
        if( independent_info->num_dep_sub > 0 )
            lsmash_bits_put( &bits, 9, independent_info->chan_loc );
        else
            lsmash_bits_put( &bits, 1, 0 );      /* reserved */
    }
    uint8_t *data = lsmash_bits_export_data( &bits, data_length );
    lsmash_bits_empty( &bits );
    /* Update box size. */
    data[0] = ((*data_length) >> 24) & 0xff;
    data[1] = ((*data_length) >> 16) & 0xff;
    data[2] = ((*data_length) >>  8) & 0xff;
    data[3] =  (*data_length)        & 0xff;
    return data;
#undef EAC3_SPECIFIC_BOX_MAX_LENGTH
}

/* Return -1 if incomplete Enhanced AC-3 sample is given. */
int lsmash_setup_eac3_specific_parameters_from_frame( lsmash_eac3_specific_parameters_t *param, uint8_t *data, uint32_t data_length )
{
    if( !data || data_length < 5 )
        return -1;
    lsmash_bits_t bits = { 0 };
    lsmash_bs_t   bs   = { 0 };
    uint8_t buffer[EAC3_MAX_SYNCFRAME_LENGTH] = { 0 };
    bs.data  = buffer;
    bs.alloc = EAC3_MAX_SYNCFRAME_LENGTH;
    eac3_info_t  handler = { { 0 } };
    eac3_info_t *info    = &handler;
    uint32_t overall_wasted_data_length = 0;
    info->buffer_pos = info->buffer;
    info->buffer_end = info->buffer;
    info->bits = &bits;
    lsmash_bits_init( &bits, &bs );
    while( 1 )
    {
        /* Check the remainder length of the input data.
         * If there is enough length, then parse the syncframe in it.
         * The length 5 is the required byte length to get frame size. */
        uint32_t remainder_length = info->buffer_end - info->buffer_pos;
        if( !info->no_more_read && remainder_length < EAC3_MAX_SYNCFRAME_LENGTH )
        {
            if( remainder_length )
                memmove( info->buffer, info->buffer_pos, remainder_length );
            uint32_t wasted_data_length = LSMASH_MIN( data_length, EAC3_MAX_SYNCFRAME_LENGTH );
            data_length -= wasted_data_length;
            memcpy( info->buffer + remainder_length, data + overall_wasted_data_length, wasted_data_length );
            overall_wasted_data_length += wasted_data_length;
            remainder_length           += wasted_data_length;
            info->buffer_pos = info->buffer;
            info->buffer_end = info->buffer + remainder_length;
            info->no_more_read = (data_length < 5);
        }
        if( remainder_length < 5 && info->no_more_read )
            goto setup_param;   /* No more valid data. */
        /* Parse syncframe. */
        IF_A52_SYNCWORD( info->buffer_pos )
            goto setup_param;
        info->frame_size = 0;
        if( eac3_parse_syncframe( info, info->buffer_pos, LSMASH_MIN( remainder_length, EAC3_MAX_SYNCFRAME_LENGTH ) ) )
            goto setup_param;
        if( remainder_length < info->frame_size )
            goto setup_param;
        int independent = info->strmtyp != 0x1;
        if( independent && info->substreamid == 0x0 )
        {
            if( info->number_of_audio_blocks == 6 )
            {
                /* Encountered the first syncframe of the next access unit. */
                info->number_of_audio_blocks = 0;
                goto setup_param;
            }
            else if( info->number_of_audio_blocks > 6 )
                goto setup_param;
            info->number_of_audio_blocks += eac3_audio_block_table[ info->numblkscod ];
            info->number_of_independent_substreams = 0;
        }
        else if( info->syncframe_count == 0 )
            /* The first syncframe in an AU must be independent and assigned substream ID 0. */
            return -2;
        if( independent )
            info->independent_info[info->number_of_independent_substreams ++].num_dep_sub = 0;
        else
            ++ info->independent_info[info->number_of_independent_substreams - 1].num_dep_sub;
        info->buffer_pos += info->frame_size;
        ++ info->syncframe_count;
    }
setup_param:
    if( info->number_of_independent_substreams == 0 || info->number_of_independent_substreams > 8 )
        return -1;
    if( !info->dec3_param_initialized )
        eac3_update_specific_param( info );
    *param = info->dec3_param;
    return info->number_of_audio_blocks == 6 ? 0 : -1;
}

uint16_t lsmash_eac3_get_chan_loc_from_chanmap( uint16_t chanmap )
{
    return ((chanmap & 0x7f8) >> 2) | ((chanmap & 0x2) >> 1);
}

static int eac3_check_syncframe_header( eac3_info_t *info )
{
    if( info->strmtyp == 0x3 )
        return -1;      /* unknown Stream type */
    lsmash_eac3_substream_info_t *substream_info;
    if( info->strmtyp != 0x1 )
        substream_info = &info->independent_info[ info->current_independent_substream_id ];
    else
        substream_info = &info->dependent_info;
    if( substream_info->fscod == 0x3 && substream_info->fscod2 == 0x3 )
        return -1;      /* unknown Sample Rate Code */
    if( substream_info->bsid < 10 || substream_info->bsid > 16 )
        return -1;      /* not EAC-3 */
    return 0;
}

int eac3_parse_syncframe( eac3_info_t *info, uint8_t *data, uint32_t data_length )
{
    lsmash_bits_t *bits = info->bits;
    if( lsmash_bits_import_data( bits, data, data_length ) )
        return -1;
    lsmash_bits_get( bits, 16 );                                                    /* syncword           (16) */
    info->strmtyp     = lsmash_bits_get( bits, 2 );                                 /* strmtyp            (2) */
    info->substreamid = lsmash_bits_get( bits, 3 );                                 /* substreamid        (3) */
    lsmash_eac3_substream_info_t *substream_info;
    if( info->strmtyp != 0x1 )
    {
        if( info->substreamid == 0x0 && info->number_of_independent_substreams )
            eac3_update_specific_param( info );
        info->current_independent_substream_id = info->substreamid;
        substream_info = &info->independent_info[ info->current_independent_substream_id ];
        substream_info->chan_loc = 0;
    }
    else
        substream_info = &info->dependent_info;
    info->frame_size = 2 * (lsmash_bits_get( bits, 11 ) + 1);                       /* frmsiz             (11) */
    substream_info->fscod = lsmash_bits_get( bits, 2 );                             /* fscod              (2) */
    if( substream_info->fscod == 0x3 )
    {
        substream_info->fscod2 = lsmash_bits_get( bits, 2 );                        /* fscod2             (2) */
        info->numblkscod = 0x3;
    }
    else
        info->numblkscod = lsmash_bits_get( bits, 2 );                              /* numblkscod         (2) */
    substream_info->acmod = lsmash_bits_get( bits, 3 );                             /* acmod              (3) */
    substream_info->lfeon = lsmash_bits_get( bits, 1 );                             /* lfeon              (1) */
    substream_info->bsid = lsmash_bits_get( bits, 5 );                              /* bsid               (5) */
    lsmash_bits_get( bits, 5 );                                                     /* dialnorm           (5) */
    if( lsmash_bits_get( bits, 1 ) )                                                /* compre             (1) */
        lsmash_bits_get( bits, 8 );                                                 /* compr              (8) */
    if( substream_info->acmod == 0x0 )
    {
        lsmash_bits_get( bits, 5 );                                                 /* dialnorm2          (5) */
        if( lsmash_bits_get( bits, 1 ) )                                            /* compre2            (1) */
            lsmash_bits_get( bits, 8 );                                             /* compr2             (8) */
    }
    if( info->strmtyp == 0x1 && lsmash_bits_get( bits, 1 ) )                        /* chanmape           (1) */
    {
        uint16_t chanmap = lsmash_bits_get( bits, 16 );                             /* chanmap            (16) */
        info->independent_info[ info->current_independent_substream_id ].chan_loc |= lsmash_eac3_get_chan_loc_from_chanmap( chanmap );
    }
    if( lsmash_bits_get( bits, 1 ) )                                                /* mixmdate           (1) */
    {
        if( substream_info->acmod > 0x2 )
            lsmash_bits_get( bits, 2 );                                             /* dmixmod            (2) */
        if( ((substream_info->acmod & 0x1) && (substream_info->acmod > 0x2)) || (substream_info->acmod & 0x4) )
            lsmash_bits_get( bits, 6 );                                             /* ltrt[c/sur]mixlev  (3)
                                                                                     * loro[c/sur]mixlev  (3) */
        if( substream_info->lfeon && lsmash_bits_get( bits, 1 ) )                   /* lfemixlevcode      (1) */
            lsmash_bits_get( bits, 5 );                                             /* lfemixlevcod       (5) */
        if( info->strmtyp == 0x0 )
        {
            if( lsmash_bits_get( bits, 1 ) )                                        /* pgmscle            (1) */
                lsmash_bits_get( bits, 6 );                                         /* pgmscl             (6) */
            if( substream_info->acmod == 0x0 && lsmash_bits_get( bits, 1 ) )        /* pgmscle2           (1) */
                lsmash_bits_get( bits, 6 );                                         /* pgmscl2            (6) */
            if( lsmash_bits_get( bits, 1 ) )                                        /* extpgmscle         (1) */
                lsmash_bits_get( bits, 6 );                                         /* extpgmscl          (6) */
            uint8_t mixdef = lsmash_bits_get( bits, 2 );                            /* mixdef             (2) */
            if( mixdef == 0x1 )
                lsmash_bits_get( bits, 5 );                                         /* premixcmpsel       (1)
                                                                                     * drcsrc             (1)
                                                                                     * premixcmpscl       (3) */
            else if( mixdef == 0x2 )
                lsmash_bits_get( bits, 12 );                                        /* mixdata            (12) */
            else if( mixdef == 0x3 )
            {
                uint8_t mixdeflen = lsmash_bits_get( bits, 5 );                     /* mixdeflen          (5) */
                lsmash_bits_get( bits, 8 * (mixdeflen + 2) );                       /* mixdata            (8 * (mixdeflen + 2))
                                                                                     * mixdatafill        (0-7) */
            }
            if( substream_info->acmod < 0x2 )
            {
                if( lsmash_bits_get( bits, 1 ) )                                    /* paninfoe           (1) */
                    lsmash_bits_get( bits, 14 );                                    /* panmean            (8)
                                                                                     * paninfo            (6) */
                if( substream_info->acmod == 0x0 && lsmash_bits_get( bits, 1 ) )    /* paninfo2e          (1) */
                    lsmash_bits_get( bits, 14 );                                    /* panmean2           (8)
                                                                                     * paninfo2           (6) */
            }
            if( lsmash_bits_get( bits, 1 ) )                                        /* frmmixcfginfoe     (1) */
            {
                if( info->numblkscod == 0x0 )
                    lsmash_bits_get( bits, 5 );                                     /* blkmixcfginfo[0]   (5) */
                else
                {
                    int number_of_blocks_per_syncframe = ((int []){ 1, 2, 3, 6 })[ info->numblkscod ];
                    for( int blk = 0; blk < number_of_blocks_per_syncframe; blk++ )
                        if( lsmash_bits_get( bits, 1 ) )                            /* blkmixcfginfoe     (1)*/
                            lsmash_bits_get( bits, 5 );                             /* blkmixcfginfo[blk] (5) */
                }
            }
        }
    }
    if( lsmash_bits_get( bits, 1 ) )                                                /* infomdate          (1) */
    {
        substream_info->bsmod = lsmash_bits_get( bits, 3 );                         /* bsmod              (3) */
        lsmash_bits_get( bits, 1 );                                                 /* copyrightb         (1) */
        lsmash_bits_get( bits, 1 );                                                 /* origbs             (1) */
        if( substream_info->acmod == 0x2 )
            lsmash_bits_get( bits, 4 );                                             /* dsurmod            (2)
                                                                                     * dheadphonmod       (2) */
        else if( substream_info->acmod >= 0x6 )
            lsmash_bits_get( bits, 2 );                                             /* dsurexmod          (2) */
        if( lsmash_bits_get( bits, 1 ) )                                            /* audprodie          (1) */
            lsmash_bits_get( bits, 8 );                                             /* mixlevel           (5)
                                                                                     * roomtyp            (2)
                                                                                     * adconvtyp          (1) */
        if( substream_info->acmod == 0x0 && lsmash_bits_get( bits, 1 ) )            /* audprodie2         (1) */
            lsmash_bits_get( bits, 8 );                                             /* mixlevel2          (5)
                                                                                     * roomtyp2           (2)
                                                                                     * adconvtyp2         (1) */
        if( substream_info->fscod < 0x3 )
            lsmash_bits_get( bits, 1 );                                             /* sourcefscod        (1) */
    }
    else
        substream_info->bsmod = 0;
    if( info->strmtyp == 0x0 && info->numblkscod != 0x3 )
        lsmash_bits_get( bits, 1 );                                                 /* convsync           (1) */
    if( info->strmtyp == 0x2 )
    {
        int blkid;
        if( info->numblkscod == 0x3 )
            blkid = 1;
        else
            blkid = lsmash_bits_get( bits, 1 );                                     /* blkid              (1) */
        if( blkid )
            lsmash_bits_get( bits, 6 );                                             /* frmsizecod         (6) */
    }
    if( lsmash_bits_get( bits, 1 ) )                                                /* addbsie            (1) */
    {
        uint8_t addbsil = lsmash_bits_get( bits, 6 );                               /* addbsil            (6) */
        lsmash_bits_get( bits, (addbsil + 1) * 8 );                                 /* addbsi             ((addbsil + 1) * 8) */
    }
    lsmash_bits_empty( bits );
    return eac3_check_syncframe_header( info );
}

void eac3_update_specific_param( eac3_info_t *info )
{
    lsmash_eac3_specific_parameters_t *param = &info->dec3_param;
    param->data_rate = 0;
    param->num_ind_sub = info->number_of_independent_substreams - 1;
    for( uint8_t i = 0; i <= param->num_ind_sub; i++ )
        param->independent_info[i] = info->independent_info[i];
    info->dec3_param_initialized = 1;
}
