/*****************************************************************************
 * box.c:
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

#include "box.h"

lsmash_extended_box_type_t lsmash_form_extended_box_type( uint32_t fourcc, const uint8_t id[12] )
{
    return (lsmash_extended_box_type_t){ fourcc, { id[0], id[1], id[2], id[3], id[4],  id[5],
                                                   id[6], id[7], id[8], id[9], id[10], id[11] } };
}

lsmash_box_type_t lsmash_form_iso_box_type( uint32_t fourcc )
{
    return (lsmash_box_type_t){ fourcc, lsmash_form_extended_box_type( fourcc, LSMASH_ISO_12_BYTES ) };
}

lsmash_box_type_t lsmash_form_qtff_box_type( uint32_t fourcc )
{
    return (lsmash_box_type_t){ fourcc, lsmash_form_extended_box_type( fourcc, LSMASH_QTFF_12_BYTES ) };
}

#define CHECK_BOX_TYPE_IDENTICAL( a, b ) \
       a.fourcc      == b.fourcc         \
    && a.user.fourcc == b.user.fourcc    \
    && a.user.id[0]  == b.user.id[0]     \
    && a.user.id[1]  == b.user.id[1]     \
    && a.user.id[2]  == b.user.id[2]     \
    && a.user.id[3]  == b.user.id[3]     \
    && a.user.id[4]  == b.user.id[4]     \
    && a.user.id[5]  == b.user.id[5]     \
    && a.user.id[6]  == b.user.id[6]     \
    && a.user.id[7]  == b.user.id[7]     \
    && a.user.id[8]  == b.user.id[8]     \
    && a.user.id[9]  == b.user.id[9]     \
    && a.user.id[10] == b.user.id[10]    \
    && a.user.id[11] == b.user.id[11]

int lsmash_check_box_type_identical( lsmash_box_type_t a, lsmash_box_type_t b )
{
    return CHECK_BOX_TYPE_IDENTICAL( a, b );
}

int lsmash_check_codec_type_identical( lsmash_codec_type_t a, lsmash_codec_type_t b )
{
    return CHECK_BOX_TYPE_IDENTICAL( a, b );
}

int lsmash_check_box_type_specified( lsmash_box_type_t *box_type )
{
    assert( box_type );
    if( !box_type )
        return 0;
    return !!(box_type->fourcc
           |  box_type->user.fourcc
           |  box_type->user.id[0] | box_type->user.id[1] | box_type->user.id[2]  | box_type->user.id[3]
           |  box_type->user.id[4] | box_type->user.id[5] | box_type->user.id[6]  | box_type->user.id[7]
           |  box_type->user.id[8] | box_type->user.id[9] | box_type->user.id[10] | box_type->user.id[11]);
}

void isom_init_box_common( void *_box, void *_parent, lsmash_box_type_t box_type )
{
    isom_box_t *box    = (isom_box_t *)_box;
    isom_box_t *parent = (isom_box_t *)_parent;
    assert( box && parent && parent->root );
    box->root   = parent->root;
    box->parent = parent;
    box->size   = 0;
    box->type   = box_type;
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STSD ) || !isom_is_fullbox( box ) )
        return;
    box->version = 0;
    box->flags   = 0;
}

void isom_bs_put_basebox_common( lsmash_bs_t *bs, isom_box_t *box )
{
    if( box->size > UINT32_MAX )
    {
        lsmash_bs_put_be32( bs, 1 );
        lsmash_bs_put_be32( bs, box->type.fourcc );
        lsmash_bs_put_be64( bs, box->size );    /* largesize */
    }
    else
    {
        lsmash_bs_put_be32( bs, (uint32_t)box->size );
        lsmash_bs_put_be32( bs, box->type.fourcc );
    }
    if( box->type.fourcc == ISOM_BOX_TYPE_UUID.fourcc )
    {
        lsmash_bs_put_be32( bs, box->type.user.fourcc );
        lsmash_bs_put_bytes( bs, 12, box->type.user.id );
    }
}

void isom_bs_put_fullbox_common( lsmash_bs_t *bs, isom_box_t *box )
{
    isom_bs_put_basebox_common( bs, box );
    lsmash_bs_put_byte( bs, box->version );
    lsmash_bs_put_be24( bs, box->flags );
}

void isom_bs_put_box_common( lsmash_bs_t *bs, void *box )
{
    if( !box )
    {
        bs->error = 1;
        return;
    }
    isom_box_t *parent = ((isom_box_t *)box)->parent;
    if( parent && lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STSD ) )
    {
        isom_bs_put_basebox_common( bs, (isom_box_t *)box );
        return;
    }
    if( isom_is_fullbox( box ) )
        isom_bs_put_fullbox_common( bs, (isom_box_t *)box );
    else
        isom_bs_put_basebox_common( bs, (isom_box_t *)box );
}

/* Return 1 if the box is fullbox, Otherwise return 0. */
int isom_is_fullbox( void *box )
{
    isom_box_t *current = (isom_box_t *)box;
    lsmash_box_type_t type = current->type;
    static lsmash_box_type_t fullbox_type_table[50] = { LSMASH_BOX_TYPE_INITIALIZER };
    if( !lsmash_check_box_type_specified( &fullbox_type_table[0] ) )
    {
        /* Initialize the table. */
        int i = 0;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_MVHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_TKHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_IODS;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_ESDS;
        fullbox_type_table[i++] = QT_BOX_TYPE_ESDS;
        fullbox_type_table[i++] = QT_BOX_TYPE_CLEF;
        fullbox_type_table[i++] = QT_BOX_TYPE_PROF;
        fullbox_type_table[i++] = QT_BOX_TYPE_ENOF;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_ELST;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_MDHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_HDLR;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_VMHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_SMHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_HMHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_NMHD;
        fullbox_type_table[i++] = QT_BOX_TYPE_GMIN;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_DREF;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_URL;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_STSD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_STSL;
        fullbox_type_table[i++] = QT_BOX_TYPE_CHAN;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_STTS;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_CTTS;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_CSLG;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_STSS;
        fullbox_type_table[i++] = QT_BOX_TYPE_STPS;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_SDTP;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_STSC;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_STSZ;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_STCO;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_CO64;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_SGPD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_SBGP;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_CHPL;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_META;
        fullbox_type_table[i++] = QT_BOX_TYPE_KEYS;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_MEAN;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_NAME;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_MEHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_TREX;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_MFHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_TFHD;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_TFDT;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_TRUN;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_TFRA;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_MFRO;
        fullbox_type_table[i]   = LSMASH_BOX_TYPE_UNSPECIFIED;
    }
    for( int i = 0; lsmash_check_box_type_specified( &fullbox_type_table[i] ); i++ )
        if( lsmash_check_box_type_identical( type, fullbox_type_table[i] ) )
            return 1;
    return lsmash_check_box_type_identical( type, ISOM_BOX_TYPE_CPRT )
        && current->parent && lsmash_check_box_type_identical( current->parent->type, ISOM_BOX_TYPE_UDTA );
}

/* Return 1 if the sample type is LPCM audio, Otherwise return 0. */
int isom_is_lpcm_audio( void *box )
{
    isom_box_t *current = (isom_box_t *)box;
    lsmash_box_type_t type = current->type;
    return lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_23NI_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_NONE_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_LPCM_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_SOWT_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_TWOS_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_FL32_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_FL64_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_IN24_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_IN32_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_NOT_SPECIFIED )
        || (lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_RAW_AUDIO ) && (current->manager & LSMASH_AUDIO_DESCRIPTION));
}

/* Return 1 if the sample type is uncompressed Y'CbCr video, Otherwise return 0. */
int isom_is_uncompressed_ycbcr( lsmash_box_type_t type )
{
    return lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V210_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V216_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V308_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V408_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V410_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_YUV2_VIDEO );
}

size_t isom_skip_box_common( uint8_t **p_data )
{
    uint8_t *orig = *p_data;
    uint8_t *data = *p_data;
    uint64_t size = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    data += ISOM_BASEBOX_COMMON_SIZE;
    if( size == 1 )
    {
        size = ((uint64_t)data[0] << 56) | ((uint64_t)data[1] << 48) | ((uint64_t)data[2] << 40) | ((uint64_t)data[3] << 32)
             | ((uint64_t)data[4] << 24) | ((uint64_t)data[5] << 16) | ((uint64_t)data[6] <<  8) |  (uint64_t)data[7];
        data += 8;
    }
    *p_data = data;
    return data - orig;
}
