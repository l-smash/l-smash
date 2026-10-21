/*****************************************************************************
 * box.c
 *****************************************************************************
 * Copyright (C) 2012-2017 L-SMASH project
 * Copyright (c) 2026 Yusuke Nakamura
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

#include <stdlib.h>
#include <string.h>

#include "box.h"
#include "box_default.h"
#include "write.h"
#include "read.h"
#include "print.h"
#include "timeline.h"
#include "sample.h"

#include "codecs/mp4a.h"
#include "codecs/mp4sys.h"
#include "codecs/hevc.h"

#include "importer/importer.h"

static const lsmash_class_t lsmash_box_class =
{
    "box"
};

const lsmash_box_type_t static_lsmash_box_type_unspecified = LSMASH_BOX_TYPE_INITIALIZER;

void lsm_box_init_common_orig
(
    void                       *_box,
    void                       *_parent,
    lsmash_box_type_t           box_type,
    uint64_t                    precedence,
    isom_extension_destructor_t destructor
)
{
    isom_box_t *box    = (isom_box_t *)_box;
    isom_box_t *parent = (isom_box_t *)_parent;
    assert( box && parent && parent->root );
    box->class      = &lsmash_box_class;
    box->root       = parent->root;
    box->file       = parent->file;
    box->parent     = parent;
    box->precedence = precedence;
    box->destruct   = destructor;
    box->size       = 0;
    box->type       = box_type;
    /* Chimera specification boundary (`stsd` vs. SampleEntry):
     * Although `stsd` (Sample Description Box) is a FullBox, its immediate children
     * (VisualSampleEntry, AudioSampleEntry, etc.) are BaseBoxes that start with 6 reserved
     * bytes + 16-bit `data_reference_index`, followed by a format-specific 16-bit `version`
     * field (used by QTFF Sound/Video Sample Description v0/v1/v2 and ISOBMFF AudioSampleEntryV1).
     * Moreover, certain SampleEntry 4CCs (such as `'mp4a'`, `'alac'`, or `'text'`) collide
     * with 4CCs of nested FullBoxes/atoms (`QT_BOX_TYPE_MP4A`, `ISOM_BOX_TYPE_ALAC`, etc.).
     * Checking `parent->type != ISOM_BOX_TYPE_STSD` ensures a SampleEntry is never
     * misclassified as a FullBox even when its 4CC appears in `fullbox_type_table`. */
    if( !lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STSD ) && lsm_box_is_fullbox( box ) )
    {
        box->version = 0;
        box->flags   = 0;
    }
    lsm_write_set_box_writer( box );
}

static void box_reorder_tail( isom_box_t *parent )
{
    /* Reorder the appended box by 'precedence'. */
    lsmash_entry_t *x = parent->extensions.tail;
    assert( x && x->data );
    uint64_t precedence = ((isom_box_t *)x->data)->precedence;
    for( lsmash_entry_t *y = x->prev; y; y = y->prev )
    {
        isom_box_t *box = (isom_box_t *)y->data;
        if( LSMASH_IS_NON_EXISTING_BOX( box ) || precedence > box->precedence )
        {
            /* Exchange the entity data of adjacent two entries. */
            y->data = x->data;
            x->data = box;
            x = y;
        }
        else
            break;
    }
}

static int box_add_to_extension_list( void *parent_box, void *child_box )
{
    isom_box_t *parent = (isom_box_t *)parent_box;
    isom_box_t *child  = (isom_box_t *)child_box;
    assert( LSMASH_IS_EXISTING_BOX( parent ) && LSMASH_IS_EXISTING_BOX( child ) );
    /* Append at the end of the list. */
    if( lsmash_list_add_entry( &parent->extensions, child ) < 0 )
        return LSMASH_ERR_MEMORY_ALLOC;
    /* Don't reorder the appended box when the file is opened for reading. */
    if( LSMASH_IS_NON_EXISTING_BOX( parent->file )
      || (parent->file->flags & LSMASH_FILE_MODE_READ)
      || parent->file->fake_file_mode )
        return 0;
    box_reorder_tail( parent );
    return 0;
}

void lsm_box_bs_put_basebox_common( lsmash_bs_t *bs, isom_box_t *box )
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

void lsm_box_bs_put_fullbox_common( lsmash_bs_t *bs, isom_box_t *box )
{
    lsm_box_bs_put_basebox_common( bs, box );
    lsmash_bs_put_byte( bs, box->version );
    lsmash_bs_put_be24( bs, box->flags );
}

void lsm_box_bs_put_common( lsmash_bs_t *bs, void *box )
{
    if( !box )
    {
        bs->error = 1;
        return;
    }
    /* SampleEntry boxes directly under `stsd` must always emit an 8-byte BaseBox header
     * (or 16-byte largesize header), never a 12-byte FullBox header (`version` + `flags`),
     * even if `box->version` is non-zero (e.g., QTFF Sound Sample Description v1/v2 or
     * ISOBMFF AudioSampleEntryV1) or if the SampleEntry 4CC matches a FullBox type.
     * The SampleEntry's own 16-bit `version` field is serialized later inside the
     * type-specific writer (`write_visual_description`, `write_audio_description`, etc.). */
    isom_box_t *parent = ((isom_box_t *)box)->parent;
    if( parent && lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STSD ) )
    {
        lsm_box_bs_put_basebox_common( bs, (isom_box_t *)box );
        return;
    }
    if( lsm_box_is_fullbox( box ) )
        lsm_box_bs_put_fullbox_common( bs, (isom_box_t *)box );
    else
        lsm_box_bs_put_basebox_common( bs, (isom_box_t *)box );
}

/* Return 1 if the box is fullbox, Otherwise return 0. */
int lsm_box_is_fullbox( const void *box )
{
    const isom_box_t *current = (const isom_box_t *)box;
    lsmash_box_type_t type = current->type;
    static const lsmash_box_type_t * const fullbox_type_table[] =
    {
        &ISOM_BOX_TYPE_SIDX,
        &ISOM_BOX_TYPE_MVHD,
        &ISOM_BOX_TYPE_TKHD,
        &ISOM_BOX_TYPE_IODS,
        &ISOM_BOX_TYPE_ESDS,
        &QT_BOX_TYPE_ESDS,
        &QT_BOX_TYPE_CLEF,
        &QT_BOX_TYPE_PROF,
        &QT_BOX_TYPE_ENOF,
        &ISOM_BOX_TYPE_ELST,
        &ISOM_BOX_TYPE_MDHD,
        &ISOM_BOX_TYPE_HDLR,
        &ISOM_BOX_TYPE_VMHD,
        &ISOM_BOX_TYPE_SMHD,
        &ISOM_BOX_TYPE_HMHD,
        &ISOM_BOX_TYPE_NMHD,
        &QT_BOX_TYPE_GMIN,
        &ISOM_BOX_TYPE_DREF,
        &ISOM_BOX_TYPE_STSD,
        &ISOM_BOX_TYPE_STSL,
        &QT_BOX_TYPE_CHAN,
        &ISOM_BOX_TYPE_SRAT,
        &ISOM_BOX_TYPE_STTS,
        &ISOM_BOX_TYPE_CTTS,
        &ISOM_BOX_TYPE_CSLG,
        &ISOM_BOX_TYPE_STSS,
        &QT_BOX_TYPE_STPS,
        &ISOM_BOX_TYPE_SDTP,
        &ISOM_BOX_TYPE_STSC,
        &ISOM_BOX_TYPE_STSZ,
        &ISOM_BOX_TYPE_STZ2,
        &ISOM_BOX_TYPE_STCO,
        &ISOM_BOX_TYPE_CO64,
        &ISOM_BOX_TYPE_SGPD,
        &ISOM_BOX_TYPE_SBGP,
        &ISOM_BOX_TYPE_CHPL,
        &ISOM_BOX_TYPE_META,
        &QT_BOX_TYPE_KEYS,
        &ISOM_BOX_TYPE_MEAN,
        &ISOM_BOX_TYPE_NAME,
        &ISOM_BOX_TYPE_MEHD,
        &ISOM_BOX_TYPE_TREX,
        &ISOM_BOX_TYPE_MFHD,
        &ISOM_BOX_TYPE_TFHD,
        &ISOM_BOX_TYPE_TFDT,
        &ISOM_BOX_TYPE_TRUN,
        &ISOM_BOX_TYPE_TFRA,
        &ISOM_BOX_TYPE_MFRO,
        &ISOM_BOX_TYPE_ST3D,
        &ISOM_BOX_TYPE_SVHD,
        &ISOM_BOX_TYPE_PRHD,
        &ISOM_BOX_TYPE_EQUI,
        &ISOM_BOX_TYPE_CBMP,
        &ISOM_BOX_TYPE_PRJI,
        &ISOM_BOX_TYPE_EMSG,
        &ISOM_BOX_TYPE_MUST,
        &ISOM_BOX_TYPE_STRI,
        &ISOM_BOX_TYPE_HERO,
        &ISOM_BOX_TYPE_BLIN,
        &ISOM_BOX_TYPE_DADJ,
        &ISOM_BOX_TYPE_PCMC,
        &ISOM_BOX_TYPE_CHNL,
        NULL
    };
    for( int i = 0; fullbox_type_table[i]; i++ )
        if( lsmash_check_box_type_identical( type, *fullbox_type_table[i] ) )
            return 1;
    if( current->parent )
    {
        if( lsmash_check_box_type_identical( current->parent->type, ISOM_BOX_TYPE_DREF )
         || (lsmash_check_box_type_identical(                  type, ISOM_BOX_TYPE_CPRT )
          && lsmash_check_box_type_identical( current->parent->type, ISOM_BOX_TYPE_UDTA )) )
            return 1;
    }
    return 0;
}

/* Return 1 if the sample type is ISO/IEC 23003-5 LPCM audio ('ipcm' or 'fpcm'), Otherwise return 0. */
int lsm_box_is_iso_lpcm_audio( const void *box )
{
    if( !box )
        return 0;
    const isom_box_t *current = (const isom_box_t *)box;
    lsmash_box_type_t type = current->type;
    return lsmash_check_codec_type_identical( type, ISOM_CODEC_TYPE_IPCM_AUDIO )
        || lsmash_check_codec_type_identical( type, ISOM_CODEC_TYPE_FPCM_AUDIO );
}

/* Return 1 if the sample type is LPCM audio, Otherwise return 0. */
int lsm_box_is_lpcm_audio( const void *box )
{
    if( !box )
        return 0;
    const isom_box_t *current = (const isom_box_t *)box;
    lsmash_box_type_t type = current->type;
    return lsm_box_is_iso_lpcm_audio( box )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_23NI_AUDIO )
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

int lsm_box_is_qt_audio( lsmash_codec_type_t type )
{
    return lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_23NI_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_MAC3_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_MAC6_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_NONE_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_QDM2_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_QDMC_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_QCLP_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_AC_3_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_AGSM_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_ALAC_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_ALAW_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_CDX2_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_CDX4_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_DVCA_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_DVI_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_FL32_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_FL64_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_IMA4_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_IN24_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_IN32_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_LPCM_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_MP4A_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_RAW_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_SOWT_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_TWOS_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_ULAW_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_VDVA_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_FULLMP3_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_MP3_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_ADPCM2_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_ADPCM17_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_GSM49_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_NOT_SPECIFIED );
}

/* Return 1 if the sample type is uncompressed Y'CbCr video, Otherwise return 0. */
int lsm_box_is_uncompressed_ycbcr( lsmash_codec_type_t type )
{
    return lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_2VUY_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V210_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V216_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V308_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V408_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_V410_VIDEO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_YUV2_VIDEO );
}

int lsm_box_is_waveform_audio( lsmash_box_type_t type )
{
    return lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_ADPCM2_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_ADPCM17_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_GSM49_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_FULLMP3_AUDIO )
        || lsmash_check_codec_type_identical( type, QT_CODEC_TYPE_MP3_AUDIO );
}

size_t lsm_box_skip_common( uint8_t **p_data )
{
    uint8_t *orig = *p_data;
    uint8_t *data = *p_data;
    uint64_t size = LSMASH_GET_BE32( data );
    data += ISOM_BASEBOX_COMMON_SIZE;
    if( size == 1 )
    {
        /* 'size = LSMASH_GET_BE64( data );' is a dead assignment here. */
        data += 8;
    }
    *p_data = data;
    return data - orig;
}

/* TODO: more secure handling */
static size_t box_read_size_and_type_from_binary_string( uint8_t **p_data, uint64_t *size, lsmash_box_type_t *type )
{
    uint8_t *orig = *p_data;
    uint8_t *data = *p_data;
    *size        = LSMASH_GET_BE32( &data[0] );
    type->fourcc = LSMASH_GET_BE32( &data[4] );
    data += ISOM_BASEBOX_COMMON_SIZE;
    if( *size == 1 )
    {
        *size = LSMASH_GET_BE64( data );
        data += 8;
    }
    *p_data = data;
    if( type->fourcc == ISOM_BOX_TYPE_UUID.fourcc )
    {
        type->user.fourcc = LSMASH_GET_BE32( &data[0] );
        memcpy( type->user.id, &data[4], 12 );
    }
    return data - orig;
}

uint8_t *lsm_box_get_child_position( uint8_t *parent_data, uint32_t parent_size, lsmash_box_type_t child_type, uint32_t *child_size )
{
    if( !parent_data || !child_size || parent_size < ISOM_BASEBOX_COMMON_SIZE )
        return NULL;
    uint8_t *data = parent_data;
    uint64_t          size;
    lsmash_box_type_t type;
    (void)box_read_size_and_type_from_binary_string( &data, &size, &type );
    if( size != parent_size )
        return NULL;
    uint8_t *end = parent_data + parent_size;
    for( uint8_t *pos = data; pos + ISOM_BASEBOX_COMMON_SIZE <= end; )
    {
        uint32_t offset = box_read_size_and_type_from_binary_string( &pos, &size, &type );
        if( lsmash_check_box_type_identical( type, child_type ) )
        {
            *child_size = size;
            return pos - offset;
        }
        pos += size - offset;   /* Move to the next box. */
    }
    return NULL;
}

static void box_destruct_extension_binary( void *ext )
{
    if( !ext )
        return;
    isom_box_t *box = (isom_box_t *)ext;
    lsmash_free( box->binary );
}

int lsm_box_add_extension_binary
(
    void             *parent_box,
    lsmash_box_type_t box_type,
    uint64_t          precedence,
    uint8_t          *box_data,
    uint32_t          box_size
)
{
    if( !parent_box || !box_data || box_size < ISOM_BASEBOX_COMMON_SIZE
     || !lsmash_check_box_type_specified( &box_type ) )
        return LSMASH_ERR_FUNCTION_PARAM;
    isom_box_t *ext = lsmash_malloc_zero( sizeof(isom_box_t) );
    if( !ext )
        return LSMASH_ERR_MEMORY_ALLOC;
    isom_box_t *parent = (isom_box_t *)parent_box;
    ext->class      = &lsmash_box_class;
    ext->root       = parent->root;
    ext->file       = parent->file;
    ext->parent     = parent;
    ext->manager    = LSMASH_BINARY_CODED_BOX;
    ext->precedence = precedence;
    ext->size       = box_size;
    ext->type       = box_type;
    ext->binary     = box_data;
    ext->destruct   = box_destruct_extension_binary;
    if( box_add_to_extension_list( parent, ext ) < 0 )
    {
        lsmash_free( ext );
        return LSMASH_ERR_MEMORY_ALLOC;
    }
    lsm_write_set_box_writer( ext );
    return 0;
}

void lsm_box_remove_extension_box( isom_box_t *ext )
{
    if( LSMASH_IS_NON_EXISTING_BOX( ext ) )
        return;
    if( ext->destruct )
        ext->destruct( ext );
    lsm_box_remove_all_extension_boxes( &ext->extensions );
    lsmash_free( ext );
}

void lsm_box_remove_all_extension_boxes( lsmash_entry_list_t *extensions )
{
    lsmash_list_remove_entries( extensions );
}

isom_box_t *lsm_box_get_extension_box( lsmash_entry_list_t *extensions, lsmash_box_type_t box_type )
{
    for( lsmash_entry_t *entry = extensions->head; entry; entry = entry->next )
    {
        isom_box_t *ext = (isom_box_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( ext ) )
            continue;
        if( lsmash_check_box_type_identical( ext->type, box_type ) )
            return ext;
    }
    return (isom_box_t *)lsm_box_default_non_existing_unknown();
}

void *lsm_box_get_extension_box_format( lsmash_entry_list_t *extensions, lsmash_box_type_t box_type )
{
    for( lsmash_entry_t *entry = extensions->head; entry; entry = entry->next )
    {
        isom_box_t *ext = (isom_box_t *)entry->data;
        if( LSMASH_IS_NON_EXISTING_BOX( ext )
         || (ext->manager & LSMASH_BINARY_CODED_BOX)
         || !lsmash_check_box_type_identical( ext->type, box_type ) )
            continue;
        return ext;
    }
    return lsm_box_default_non_existing_unknown();
}

static lsmash_entry_t *box_get_entry
(
    lsmash_box_t           *parent,
    const lsmash_box_path_t box_path[]
)
{
    if( LSMASH_IS_NON_EXISTING_BOX( parent ) )
        return NULL;
    lsmash_entry_t *entry = NULL;
    const lsmash_box_path_t *path = &box_path[0];
    while( lsmash_check_box_type_specified( &path->type ) )
    {
        entry = parent->extensions.head;
        if( !entry )
            return NULL;
        parent = NULL;
        uint32_t i      = 1;
        uint32_t number = path->number ? path->number : 1;
        while( entry )
        {
            isom_box_t *box = entry->data;
            if( box && lsmash_check_box_type_identical( path->type, box->type ) )
            {
                if( i == number )
                {
                    /* Found a box. Move to a child box. */
                    parent = box;
                    ++path;
                    break;
                }
                ++i;
            }
            entry = entry->next;
        }
        if( LSMASH_IS_NON_EXISTING_BOX( parent ) )
            return NULL;
    }
    return entry;
}

/* box destructors
 * TODO: To eliminate REMOVE_LIST_BOX_2(), an eliminator should be determined when initializing
 *       the list to which the eliminator belongs. */
#define REMOVE_BOX( box_name ) \
        box_remove_predefined( box_name )

#define REMOVE_BOX_IN_LIST( box_name ) \
        box_remove_in_predefined_list( box_name )

#define REMOVE_LIST_BOX_TEMPLATE( REMOVER, box_name ) \
    do                                                \
    {                                                 \
        lsmash_list_destroy( box_name->list );        \
        REMOVER( box_name );                          \
    } while( 0 )

#define REMOVE_LIST_BOX( box_name ) \
        REMOVE_LIST_BOX_TEMPLATE( REMOVE_BOX, box_name )

#define REMOVE_LIST_BOX_IN_LIST( box_name ) \
        REMOVE_LIST_BOX_TEMPLATE( REMOVE_BOX_IN_LIST, box_name )

#define REMOVE_PAGED_LIST_BOX_TEMPLATE( REMOVER, box_name ) \
    do                                                      \
    {                                                       \
        lsm_list_destroy( box_name->list );                 \
        REMOVER( box_name );                                \
    } while( 0 )

#define REMOVE_PAGED_LIST_BOX( box_name ) \
        REMOVE_PAGED_LIST_BOX_TEMPLATE( REMOVE_BOX, box_name )

#define REMOVE_PAGED_LIST_BOX_IN_LIST( box_name ) \
        REMOVE_PAGED_LIST_BOX_TEMPLATE( REMOVE_BOX_IN_LIST, box_name )

#define DEFINE_SIMPLE_BOX_REMOVER_TEMPLATE( ... ) \
        CALL_FUNC_DEFAULT_ARGS( DEFINE_SIMPLE_BOX_REMOVER_TEMPLATE, __VA_ARGS__ )
#define DEFINE_SIMPLE_BOX_REMOVER_TEMPLATE_3( REMOVER, box_name, ... ) \
    static void box_remove_##box_name( isom_##box_name##_t *box_name ) \
    {                                                                  \
        REMOVER( box_name, __VA_ARGS__ );                              \
    }
#define DEFINE_SIMPLE_BOX_REMOVER_TEMPLATE_2( REMOVER, box_name )      \
    static void box_remove_##box_name( isom_##box_name##_t *box_name ) \
    {                                                                  \
        REMOVER( box_name );                                           \
    }

#define DEFINE_SIMPLE_BOX_REMOVER( func_name, box_name )   \
        DEFINE_SIMPLE_BOX_REMOVER_TEMPLATE( REMOVE_BOX, box_name )

#define DEFINE_SIMPLE_BOX_IN_LIST_REMOVER( func_name, box_name ) \
        DEFINE_SIMPLE_BOX_REMOVER_TEMPLATE( REMOVE_BOX_IN_LIST, box_name )

#define DEFINE_SIMPLE_LIST_BOX_REMOVER( func_name, box_name ) \
        DEFINE_SIMPLE_BOX_REMOVER_TEMPLATE( REMOVE_LIST_BOX, box_name )

#define DEFINE_SIMPLE_LIST_BOX_IN_LIST_REMOVER( func_name, box_name ) \
        DEFINE_SIMPLE_BOX_REMOVER_TEMPLATE( REMOVE_LIST_BOX_IN_LIST, box_name )

#define DEFINE_SIMPLE_PAGED_LIST_BOX_REMOVER( func_name, box_name ) \
        DEFINE_SIMPLE_BOX_REMOVER_TEMPLATE( REMOVE_PAGED_LIST_BOX, box_name )

#define DEFINE_SIMPLE_PAGED_LIST_BOX_IN_LIST_REMOVER( func_name, box_name ) \
        DEFINE_SIMPLE_BOX_REMOVER_TEMPLATE( REMOVE_PAGED_LIST_BOX_IN_LIST, box_name )

/*---------------------------------------------------------------------------
 * Dual-Registration & Single-Ownership Box Teardown Architecture
 *
 * Every child box in L-SMASH is registered in `parent->extensions` (which owns
 * the box allocation and frees the box struct via `lsm_box_remove_extension_box`),
 * and optionally referenced by either:
 *   (a) a singleton pointer member in `parent` (e.g. `moov->mvhd`, `stbl->stsd`), or
 *   (b) a typed predefined linked list in `parent` (e.g. `moov->trak_list`, `stsd->list`).
 *
 * When a box is destroyed:
 * 1. For singleton predefined children (`REMOVE_BOX` -> `box_remove_predefined`):
 *    `box->offset_in_parent` holds `offsetof(parent_type, child_field)`. Using byte-offset
 *    pointer arithmetic on `box->parent`, `box_remove_predefined()` locates the parent's
 *    member pointer and restores it to `box->nonexist_ptr` (the static sentinel constant),
 *    preserving the Null-Object Sentinel invariant without knowing the concrete parent type.
 * 2. For predefined list children (`REMOVE_BOX_IN_LIST` -> `box_remove_in_predefined_list`):
 *    `box->offset_in_parent` holds `offsetof(parent_type, child_list)`. The function locates
 *    the `lsmash_entry_list_t` inside `box->parent`, zeroes `entry->data = NULL` so the
 *    list's eliminator does not double-free the box, and frees only the `lsmash_entry_t`
 *    link node. The actual box memory is freed once by `parent->extensions`.
 *---------------------------------------------------------------------------*/
static void box_remove_predefined( void *opaque_box )
{
    isom_box_t *box = (isom_box_t *)opaque_box;
    if( LSMASH_IS_EXISTING_BOX( box )
     && LSMASH_IS_EXISTING_BOX( box->parent ) )
    {
        isom_box_t **p = (isom_box_t **)(((int8_t *)box->parent) + box->offset_in_parent);
        if( *p == box )
            *p = box->nonexist_ptr;
    }
}

/* We always free boxes through the extension list of the parent box.
 * Therefore, don't free boxes through any list other than the extension list. */
static void box_remove_in_predefined_list( void *opaque_box )
{
    isom_box_t *box = (isom_box_t *)opaque_box;
    if( LSMASH_IS_EXISTING_BOX( box )
     && LSMASH_IS_EXISTING_BOX( box->parent ) )
    {
        lsmash_entry_list_t *list = (lsmash_entry_list_t *)(((int8_t *)box->parent) + box->offset_in_parent);
        if( list )
            for( lsmash_entry_t *entry = list->head; entry; entry = entry->next )
                if( box == entry->data )
                {
                    /* We don't free this box here.
                     * Because of freeing an entry of the list here, don't pass the list to free this box.
                     * Or double free. */
                    entry->data = NULL;
                    lsmash_list_remove_entry_direct( list, entry );
                    break;
                }
    }
}

/* Remove a box by the pointer containing its address.
 * In addition, remove from the extension list of the parent box if possible.
 * Don't call this function within a function freeing one or more entries of any extension list because of double free.
 * Basically, don't use this function as a callback function. */
void lsm_box_remove_by_itself( void *opaque_box )
{
    isom_box_t *box = (isom_box_t *)opaque_box;
    if( LSMASH_IS_NON_EXISTING_BOX( box ) )
        return;
    if( LSMASH_IS_EXISTING_BOX( box->parent ) )
    {
        isom_box_t *parent = box->parent;
        for( lsmash_entry_t *entry = parent->extensions.head; entry; entry = entry->next )
            if( box == entry->data )
            {
                /* Free the corresponding entry here, therefore don't call this function as a callback function
                 * if a function frees the same entry later and calls this function. */
                lsmash_list_remove_entry_direct( &parent->extensions, entry );
                return;
            }
    }
    lsm_box_remove_extension_box( box );
}

void lsm_box_remove_unknown_box( isom_unknown_box_t *unknown_box )
{
    lsmash_free( unknown_box->unknown_field );
}

static void box_remove_file_abstract( isom_file_abstract_t *file_abstract )
{
    if( LSMASH_IS_NON_EXISTING_BOX( file_abstract ) )
        return;
    lsm_print_destroy_list( file_abstract );
    lsm_timeline_remove_all( file_abstract );
    lsmash_free( file_abstract->compatible_brands );
    lsmash_bs_cleanup( file_abstract->bs );
    lsm_importer_destroy( file_abstract->importer );
    if( file_abstract->fragment )
    {
        lsm_list_destroy( file_abstract->fragment->pool );
        lsmash_free( file_abstract->fragment );
    }
    REMOVE_BOX_IN_LIST( file_abstract );
}

static void box_remove_ftyp( isom_ftyp_t *ftyp )
{
    lsmash_free( ftyp->compatible_brands );
    REMOVE_BOX( ftyp );
}

static void box_remove_iods( isom_iods_t *iods )
{
    if( LSMASH_IS_NON_EXISTING_BOX( iods ) )
        return;
    lsm_mp4sys_remove_descriptor( iods->OD );
    REMOVE_BOX( iods );
}

static void box_remove_trak( isom_trak_t *trak )
{
    if( trak->cache )
    {
        lsm_sample_remove_pool( trak->cache->chunk.pool );
        lsm_list_destroy( trak->cache->roll.pool );
        lsmash_free( trak->cache->rap );
        lsmash_free( trak->cache->fragment );
        lsmash_free( trak->cache );
    }
    REMOVE_BOX_IN_LIST( trak );
}

DEFINE_SIMPLE_BOX_REMOVER( box_remove_tkhd, tkhd )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_clef, clef )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_prof, prof )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_enof, enof )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_tapt, tapt )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_edts, edts )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_tref, tref )
DEFINE_SIMPLE_PAGED_LIST_BOX_REMOVER( box_remove_elst, elst )

static void box_remove_track_reference_type( isom_tref_type_t *ref )
{
    lsmash_free( ref->track_ID );
    box_remove_in_predefined_list( ref );
}

DEFINE_SIMPLE_BOX_REMOVER( box_remove_mdhd, mdhd )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_vmhd, vmhd )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_smhd, smhd )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_hmhd, hmhd )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_nmhd, nmhd )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_gmhd, gmhd )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_gmin, gmin )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_text, text )

static void box_remove_hdlr( isom_hdlr_t *hdlr )
{
    lsmash_free( hdlr->componentName );
    REMOVE_BOX( hdlr );
}

static void box_remove_glbl( isom_glbl_t *glbl )
{
    lsmash_free( glbl->header_data );
}

static void box_remove_esds( isom_esds_t *esds )
{
    if( LSMASH_IS_NON_EXISTING_BOX( esds ) )
        return;
    lsm_mp4sys_remove_descriptor( esds->ES );
}

static void box_remove_lhvC( isom_lhvC_t *lhvC )
{
    if( LSMASH_IS_NON_EXISTING_BOX( lhvC ) )
        return;
    lsm_hevc_free_lhevc_arrays( lhvC->array, lhvC->numOfArrays );
}

DEFINE_SIMPLE_LIST_BOX_REMOVER( box_remove_ftab, ftab )

DEFINE_SIMPLE_BOX_REMOVER( box_remove_frma, frma )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_enda, enda )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_mp4a, mp4a )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_terminator, terminator )

static void box_remove_chan( isom_chan_t *chan )
{
    lsmash_free( chan->channelDescriptions );
}

static void box_remove_chnl( isom_chnl_t *chnl )
{
    lsmash_free( chnl->positions );
}

static void box_remove_must( isom_must_t *must )
{
    lsmash_free( must->required_box_types );
}

static void box_remove_svhd( isom_svhd_t *svhd )
{
    lsmash_free( svhd->metadata_source );
}

static void box_remove_SA3D( isom_SA3D_t *SA3D )
{
    lsmash_list_remove_entries( &SA3D->channel_map );
}

DEFINE_SIMPLE_BOX_REMOVER( box_remove_stsd, stsd )

static void box_remove_visual_description( isom_sample_entry_t *description )
{
    isom_visual_entry_t *visual = (isom_visual_entry_t *)description;
    lsmash_free( visual->color_table.array );
    box_remove_in_predefined_list( visual );
}

static void box_remove_audio_description( isom_sample_entry_t *description )
{
    box_remove_in_predefined_list( description );
}

static void box_remove_hint_description( isom_sample_entry_t *description )
{
    isom_hint_entry_t *hint = (isom_hint_entry_t *)description;
    box_remove_in_predefined_list( hint );
}

static void box_remove_metadata_description( isom_sample_entry_t *description )
{
    box_remove_in_predefined_list( description );
}

static void box_remove_tx3g_description( isom_sample_entry_t *description )
{
    box_remove_in_predefined_list( description );
}

static void box_remove_qt_text_description( isom_sample_entry_t *description )
{
    isom_qt_text_entry_t *text = (isom_qt_text_entry_t *)description;
    lsmash_free( text->font_name );
    box_remove_in_predefined_list( text );
}

static void box_remove_mp4s_description( isom_sample_entry_t *description )
{
    box_remove_in_predefined_list( description );
}

void lsm_box_remove_sample_description( isom_sample_entry_t *sample )
{
    if( LSMASH_IS_NON_EXISTING_BOX( sample ) )
        return;
    lsmash_codec_type_t sample_type = sample->type;
    if( lsmash_check_box_type_identical( sample_type, LSMASH_CODEC_TYPE_RAW ) )
    {
        if( sample->manager & LSMASH_VIDEO_DESCRIPTION )
        {
            box_remove_visual_description( sample );
            return;
        }
        else if( sample->manager & LSMASH_AUDIO_DESCRIPTION )
        {
            box_remove_audio_description( sample );
            return;
        }
    }
    static const struct description_remover_table_tag
    {
        const lsmash_codec_type_t *type;
        void (*func)( isom_sample_entry_t * );
    } description_remover_table[] =
    {
#define ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( type, func ) { &(type), func }
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_AV01_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVC1_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVC2_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVC3_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVC4_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_AVCP_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_HVC1_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_HEV1_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SVC1_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MVC1_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MVC2_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MP4V_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DRAC_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_ENCV_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MJP2_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_S263_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_VC_1_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_2VUY_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_CFHD_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DV10_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVOO_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVOR_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVTV_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVVT_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_HD10_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_M105_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_PNTG_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SVQ1_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SVQ3_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SHR0_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SHR1_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SHR2_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SHR3_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SHR4_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_WRLE_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_APCH_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_APCN_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_APCS_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_APCO_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_AP4H_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_AP4X_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_CIVD_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DRAC_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVC_VIDEO,  box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVCP_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVPP_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DV5N_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DV5P_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVH2_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVH3_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVH5_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVH6_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVHP_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_DVHQ_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_FLIC_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_GIF_VIDEO,  box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_H261_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_H263_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_JPEG_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_MJPA_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_MJPB_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_PNG_VIDEO,  box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_RLE_VIDEO,  box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_RPZA_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_TGA_VIDEO,  box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_TIFF_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_ULRA_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_ULRG_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_ULY2_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_ULY0_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_ULH2_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_ULH0_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_UQY2_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_V210_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_V216_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_V308_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_V408_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_V410_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_YUV2_VIDEO, box_remove_visual_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MP4A_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_AC_3_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_ALAC_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSEL_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSDL_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSC_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSE_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSH_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSL_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DTSX_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_EC_3_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_FPCM_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_IPCM_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_OPUS_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SAMR_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SAWB_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_MP4A_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_23NI_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_NONE_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_LPCM_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_SOWT_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_TWOS_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_FL32_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_FL64_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_IN24_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_IN32_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_NOT_SPECIFIED, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_DRA1_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_ENCA_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_G719_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_G726_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_M4AE_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MLPA_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SAWP_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SEVC_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SQCP_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SSMV_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_TWOS_AUDIO, box_remove_audio_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_FDP_HINT,  box_remove_hint_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_M2TS_HINT, box_remove_hint_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_PM2T_HINT, box_remove_hint_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_PRTP_HINT, box_remove_hint_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_RM2T_HINT, box_remove_hint_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_RRTP_HINT, box_remove_hint_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_RSRP_HINT, box_remove_hint_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_RTP_HINT , box_remove_hint_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SM2T_HINT, box_remove_hint_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SRTP_HINT, box_remove_hint_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_IXSE_META, box_remove_metadata_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_METT_META, box_remove_metadata_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_METX_META, box_remove_metadata_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MLIX_META, box_remove_metadata_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_OKSD_META, box_remove_metadata_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_SVCM_META, box_remove_metadata_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_TEXT_META, box_remove_metadata_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_URIM_META, box_remove_metadata_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_XML_META,  box_remove_metadata_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_TX3G_TEXT, box_remove_tx3g_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( QT_CODEC_TYPE_TEXT_TEXT, box_remove_qt_text_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( ISOM_CODEC_TYPE_MP4S_SYSTEM, box_remove_mp4s_description ),
        ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT( LSMASH_CODEC_TYPE_UNSPECIFIED, NULL )
#undef ADD_DESCRIPTION_REMOVER_TABLE_ELEMENT
    };
    for( int i = 0; description_remover_table[i].func; i++ )
        if( lsmash_check_codec_type_identical( sample_type, *description_remover_table[i].type ) )
        {
            description_remover_table[i].func( sample );
            return;
        }
}

DEFINE_SIMPLE_PAGED_LIST_BOX_REMOVER( box_remove_stts, stts )
DEFINE_SIMPLE_PAGED_LIST_BOX_REMOVER( box_remove_ctts, ctts )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_cslg, cslg )
DEFINE_SIMPLE_PAGED_LIST_BOX_REMOVER( box_remove_stsc, stsc )
DEFINE_SIMPLE_PAGED_LIST_BOX_REMOVER( box_remove_stsz, stsz )
DEFINE_SIMPLE_PAGED_LIST_BOX_REMOVER( box_remove_stz2, stz2 )
DEFINE_SIMPLE_PAGED_LIST_BOX_REMOVER( box_remove_stss, stss )
DEFINE_SIMPLE_PAGED_LIST_BOX_REMOVER( box_remove_stps, stps )
DEFINE_SIMPLE_PAGED_LIST_BOX_REMOVER( box_remove_stco, stco )

static void box_remove_sdtp( isom_sdtp_t *sdtp )
{
    if( LSMASH_IS_NON_EXISTING_BOX( sdtp ) )
        return;
    lsm_list_destroy( sdtp->list );
    REMOVE_BOX( sdtp );
}

static void box_remove_sgpd( isom_sgpd_t *sgpd )
{
    if( LSMASH_IS_NON_EXISTING_BOX( sgpd ) )
        return;
    lsm_list_destroy( sgpd->list );
    REMOVE_BOX_IN_LIST( sgpd );
}

static void box_remove_sbgp( isom_sbgp_t *sbgp )
{
    if( LSMASH_IS_NON_EXISTING_BOX( sbgp ) )
        return;
    lsm_list_destroy( sbgp->list );
    REMOVE_BOX_IN_LIST( sbgp );
}

DEFINE_SIMPLE_BOX_REMOVER( box_remove_stbl, stbl )

static void box_remove_dref_entry( isom_dref_entry_t *data_entry )
{
    lsmash_free( data_entry->name );
    lsmash_free( data_entry->location );
    box_remove_in_predefined_list( data_entry );
}

DEFINE_SIMPLE_BOX_REMOVER( box_remove_dref, dref )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_dinf, dinf )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_minf, minf )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_mdia, mdia )
DEFINE_SIMPLE_LIST_BOX_REMOVER( box_remove_chpl, chpl )
DEFINE_SIMPLE_LIST_BOX_REMOVER( box_remove_keys, keys )

static void box_remove_mean( isom_mean_t *mean )
{
    lsmash_free( mean->meaning_string );
    REMOVE_BOX( mean );
}

static void box_remove_name( isom_name_t *name )
{
    lsmash_free( name->name );
    REMOVE_BOX( name );
}

static void box_remove_data( isom_data_t *data )
{
    lsmash_free( data->value );
    REMOVE_BOX( data );
}

DEFINE_SIMPLE_BOX_IN_LIST_REMOVER( box_remove_metaitem, metaitem )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_ilst, ilst )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_meta, meta )

static void box_remove_cprt( isom_cprt_t *cprt )
{
    lsmash_free( cprt->notice );
    REMOVE_BOX_IN_LIST( cprt );
}

static void box_remove_rtp( isom_rtp_t *rtp )
{
    lsmash_free( rtp->sdptext );
    REMOVE_BOX( rtp );
}

static void box_remove_sdp( isom_sdp_t *sdp )
{
    lsmash_free( sdp->sdptext );
    REMOVE_BOX( sdp );
}

DEFINE_SIMPLE_BOX_REMOVER( box_remove_udta, udta )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_WLOC, WLOC )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_LOOP, LOOP )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_SelO, SelO )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_AllF, AllF )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_hnti, hnti )

static void box_remove_ctab( isom_ctab_t *ctab )
{
    lsmash_free( ctab->color_table.array );
    REMOVE_BOX( ctab );
}

DEFINE_SIMPLE_BOX_REMOVER( box_remove_mvex, mvex )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_mvhd, mvhd )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_mehd, mehd )
DEFINE_SIMPLE_BOX_IN_LIST_REMOVER( box_remove_trex, trex )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_moov, moov )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_mdat, mdat )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_mfhd, mfhd )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_tfhd, tfhd )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_tfdt, tfdt )

static void box_remove_trun( isom_trun_t *trun )
{
    lsm_list_destroy( trun->optional );
    REMOVE_BOX_IN_LIST( trun );
}

DEFINE_SIMPLE_BOX_IN_LIST_REMOVER( box_remove_traf, traf )
DEFINE_SIMPLE_BOX_IN_LIST_REMOVER( box_remove_moof, moof )

static void box_remove_free( isom_free_t *skip )
{
    lsmash_free( skip->data );
}
#define box_remove_skip box_remove_free

DEFINE_SIMPLE_BOX_REMOVER( box_remove_mfra, mfra )
DEFINE_SIMPLE_BOX_REMOVER( box_remove_mfro, mfro )
DEFINE_SIMPLE_PAGED_LIST_BOX_IN_LIST_REMOVER( box_remove_tfra, tfra )
DEFINE_SIMPLE_PAGED_LIST_BOX_IN_LIST_REMOVER( box_remove_sidx, sidx )

static void box_remove_styp( isom_styp_t *styp )
{
    lsmash_free( styp->compatible_brands );
    REMOVE_BOX_IN_LIST( styp );
}

#define box_elem_size_elst sizeof(isom_elst_entry_t)
#define box_elem_size_stts sizeof(isom_stts_entry_t)
#define box_elem_size_ctts sizeof(isom_ctts_entry_t)
#define box_elem_size_stsz sizeof(isom_stsz_entry_t)
#define box_elem_size_stz2 sizeof(isom_stsz_entry_t)
#define box_elem_size_stss sizeof(isom_stss_entry_t)
#define box_elem_size_stps sizeof(isom_stps_entry_t)
#define box_elem_size_sdtp sizeof(isom_sdtp_entry_t)
#define box_elem_size_stsc sizeof(isom_stsc_entry_t)
#define box_elem_size_stco sizeof(isom_co64_entry_t)
#define box_elem_size_sgpd LSMASH_MAX( sizeof(isom_rap_entry_t), sizeof(isom_roll_entry_t) )
#define box_elem_size_sbgp sizeof(isom_group_assignment_entry_t)
#define box_elem_size_sidx sizeof(isom_sidx_referenced_item_t)

static void box_remove_ftab_entry( isom_font_record_t *font_record )
{
    if( !font_record )
        return;
    lsmash_free( font_record->font_name );
    lsmash_free( font_record );
}

static void box_remove_chpl_entry( isom_chpl_entry_t *data )
{
    if( !data )
        return;
    lsmash_free( data->chapter_name );
    lsmash_free( data );
}

static void box_remove_keys_entry( isom_keys_entry_t *data )
{
    if( !data )
        return;
    lsmash_free( data->key_value );
    lsmash_free( data );
}

static void box_remove_emsg( isom_emsg_t *emsg )
{
    lsmash_free( emsg->scheme_id_uri );
    lsmash_free( emsg->value );
    lsmash_free( emsg->message_data );
    REMOVE_BOX_IN_LIST( emsg );
}

/* box size updater */
uint64_t lsm_box_update_size( void *opaque_box )
{
    isom_box_t *box = (isom_box_t *)opaque_box;
    assert( LSMASH_IS_EXISTING_BOX( box ) );
    if( box->manager & LSMASH_WRITTEN_BOX )
        /* No need to calculate the size of this box since the size is already decided and fixed. */
        return box->size;
    uint64_t size = 0;
    if( box->write )
    {
        /* Calculate the size of this box excluding its children with a fake bytestream writer. */
        {
            lsmash_bs_t fake_bs = { NULL };
            if( box->write( &fake_bs, box ) == 0 )
                size = lsmash_bs_get_valid_data_size( &fake_bs );
        }
        /* Calculate the size of the children if no error. */
        if( size >= ISOM_BASEBOX_COMMON_SIZE )
        {
            for( lsmash_entry_t *entry = box->extensions.head; entry; entry = entry->next )
                if( entry->data )
                    size += lsm_box_update_size( entry->data );
            /* Check large size. */
            if( size > UINT32_MAX )
                size += 8;
        }
        else
            /* TODO: add error handling. */
            size = 0;
    }
    box->size = size;
    return size;
}

/* box adding functions */
#define ATTACH_EXACTLY_ONE_BOX_TO_PARENT( box_name, parent_type )     \
    do                                                                \
    {                                                                 \
        size_t offset_in_parent = offsetof( parent_type, box_name );  \
        isom_box_t **p = (isom_box_t **)(((int8_t *)box_name->parent) \
                       + offset_in_parent);                           \
        assert( *p );                                                 \
        if( LSMASH_IS_NON_EXISTING_BOX( *p ) )                        \
        {                                                             \
            *p = (isom_box_t *)box_name;                              \
            (*p)->offset_in_parent = offset_in_parent;                \
        }                                                             \
    } while( 0 )

#define ADD_BOX_TO_PREDEFINED_LIST( box_name, parent_name )                    \
    if( lsmash_list_add_entry( &parent_name->box_name##_list, box_name ) < 0 ) \
    {                                                                          \
        lsmash_list_remove_entry_tail( &parent_name->extensions );             \
        return lsm_box_default_non_existing_##box_name();                      \
    }                                                                          \
    box_name->offset_in_parent = offsetof( isom_##parent_name##_t, box_name##_list )

#define INIT_BOX_COMMON0( box_name, parent_name, box_type, precedence )  \
        const isom_extension_destructor_t box_remove_##box_name = NULL;  \
        lsm_box_init_common( box_name, parent_name, box_type, precedence, box_remove_##box_name )
#define INIT_BOX_COMMON1( box_name, parent_name, box_type, precedence ) \
        lsm_box_init_common( box_name, parent_name, box_type, precedence, box_remove_##box_name )

#define CREATE_BOX( box_name, parent_name, box_type, precedence, has_destructor )      \
    if( LSMASH_IS_NON_EXISTING_BOX( (isom_box_t *)parent_name ) )                      \
        return lsm_box_default_non_existing_##box_name();                              \
    isom_##box_name##_t *box_name = ALLOCATE_BOX( box_name );                          \
    if( LSMASH_IS_NON_EXISTING_BOX( box_name ) )                                       \
        return box_name;                                                               \
    INIT_BOX_COMMON ## has_destructor( box_name, parent_name, box_type, precedence );  \
    if( box_add_to_extension_list( parent_name, box_name ) < 0 )                       \
    {                                                                                  \
        lsmash_free( box_name );                                                       \
        return lsm_box_default_non_existing_##box_name();                              \
    }
#define CREATE_LIST_BOX( box_name, parent_name, box_type, precedence, has_destructor )  \
    CREATE_BOX( box_name, parent_name, box_type, precedence, has_destructor );          \
    box_name->list = lsmash_list_create( box_remove_##box_name##_entry );               \
    if( !box_name->list )                                                               \
    {                                                                                   \
        lsmash_list_remove_entry_tail( &parent_name->extensions );                      \
        return lsm_box_default_non_existing_##box_name();                               \
    }
#define CREATE_PAGED_LIST_BOX( box_name, parent_name, box_type, precedence, has_destructor ) \
    CREATE_BOX( box_name, parent_name, box_type, precedence, has_destructor );               \
    box_name->list = lsm_list_create_simple( box_elem_size_##box_name );                     \
    if( !box_name->list )                                                                    \
    {                                                                                        \
        lsmash_list_remove_entry_tail( &parent_name->extensions );                           \
        return lsm_box_default_non_existing_##box_name();                                    \
    }

#define ADD_BOX_TEMPLATE( box_name, parent_name, box_type, precedence, BOX_CREATOR ) \
    BOX_CREATOR( box_name, parent_name, box_type, precedence, 1 );                   \
    if( LSMASH_IS_NON_EXISTING_BOX( parent_name->box_name ) )                        \
    {                                                                                \
        parent_name->box_name = box_name;                                            \
        box_name->offset_in_parent = offsetof( isom_##parent_name##_t, box_name );   \
    } do {} while( 0 )
#define ADD_BOX_IN_LIST_TEMPLATE( box_name, parent_name, box_type, precedence, BOX_CREATOR ) \
    BOX_CREATOR( box_name, parent_name, box_type, precedence, 1 );                           \
    ADD_BOX_TO_PREDEFINED_LIST( box_name, parent_name )

#define ADD_BOX( box_name, parent_name, box_type, precedence ) \
        ADD_BOX_TEMPLATE( box_name, parent_name, box_type, precedence, CREATE_BOX )
#define ADD_BOX_IN_LIST( box_name, parent_name, box_type, precedence ) \
        ADD_BOX_IN_LIST_TEMPLATE( box_name, parent_name, box_type, precedence, CREATE_BOX )
#define ADD_LIST_BOX( box_name, parent_name, box_type, precedence ) \
        ADD_BOX_TEMPLATE( box_name, parent_name, box_type, precedence, CREATE_LIST_BOX )
#define ADD_LIST_BOX_IN_LIST( box_name, parent_name, box_type, precedence ) \
        ADD_BOX_IN_LIST_TEMPLATE( box_name, parent_name, box_type, precedence, CREATE_LIST_BOX )
#define ADD_PAGED_LIST_BOX( box_name, parent_name, box_type, precedence ) \
        ADD_BOX_TEMPLATE( box_name, parent_name, box_type, precedence, CREATE_PAGED_LIST_BOX )
#define ADD_PAGED_LIST_BOX_IN_LIST( box_name, parent_name, box_type, precedence ) \
        ADD_BOX_IN_LIST_TEMPLATE( box_name, parent_name, box_type, precedence, CREATE_PAGED_LIST_BOX )

#define DEFINE_SIMPLE_BOX_ADDER_TEMPLATE( ... ) CALL_FUNC_DEFAULT_ARGS( DEFINE_SIMPLE_BOX_ADDER_TEMPLATE, __VA_ARGS__ )
#define DEFINE_SIMPLE_BOX_ADDER_TEMPLATE_6( ADDER, box_name, parent_name, box_type, precedence, postprocess ) \
    isom_##box_name##_t *lsm_box_add_##box_name( isom_##parent_name##_t *parent_name )                        \
    {                                                                                                         \
        ADDER( box_name, parent_name, box_type, precedence );                                                 \
        do { postprocess } while( 0 );                                                                        \
        return box_name;                                                                                      \
    }
#define DEFINE_SIMPLE_BOX_ADDER_TEMPLATE_5( ADDER, box_name, parent_name, box_type, precedence ) \
    DEFINE_SIMPLE_BOX_ADDER_TEMPLATE_6( ADDER, box_name, parent_name, box_type, precedence, )

#define DEFINE_SIMPLE_BOX_ADDER( func_name, ... ) \
        DEFINE_SIMPLE_BOX_ADDER_TEMPLATE( ADD_BOX, __VA_ARGS__ )
#define DEFINE_SIMPLE_BOX_IN_LIST_ADDER( func_name, ... ) \
        DEFINE_SIMPLE_BOX_ADDER_TEMPLATE( ADD_BOX_IN_LIST, __VA_ARGS__ )
#define DEFINE_SIMPLE_LIST_BOX_ADDER( func_name, ... ) \
        DEFINE_SIMPLE_BOX_ADDER_TEMPLATE( ADD_LIST_BOX, __VA_ARGS__ )
#define DEFINE_SIMPLE_PAGED_LIST_BOX_ADDER( func_name, ... ) \
        DEFINE_SIMPLE_BOX_ADDER_TEMPLATE( ADD_PAGED_LIST_BOX, __VA_ARGS__ )

#define DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( func_name, box_name, parent_name, box_type, precedence, has_destructor, parent_type ) \
    isom_##box_name##_t *lsm_box_add_##box_name( parent_type *parent_name )                                                         \
    {                                                                                                                               \
        CREATE_BOX( box_name, parent_name, box_type, precedence, has_destructor );                                                  \
        return box_name;                                                                                                            \
    }

DEFINE_SIMPLE_BOX_IN_LIST_ADDER( lsm_box_add_file_abstract, file_abstract, root_abstract, LSMASH_BOX_TYPE_UNSPECIFIED, 0,
                                 file_abstract->file = file_abstract; )

isom_tref_type_t *lsm_box_add_track_reference_type( isom_tref_t *tref, isom_track_reference_type type )
{
    if( LSMASH_IS_NON_EXISTING_BOX( tref ) )
        return lsm_box_default_non_existing_tref_type();
    isom_tref_type_t *tref_type = ALLOCATE_BOX( tref_type );
    if( LSMASH_IS_NON_EXISTING_BOX( tref_type ) )
        return tref_type;
    /* Initialize common fields. */
    tref_type->class      = &lsmash_box_class;
    tref_type->root       = tref->root;
    tref_type->file       = tref->file;
    tref_type->parent     = (isom_box_t *)tref;
    tref_type->precedence = LSMASH_BOX_PRECEDENCE_ISOM_TREF_TYPE;
    tref_type->destruct   = (isom_extension_destructor_t)box_remove_track_reference_type;
    tref_type->size       = 0;
    tref_type->type       = lsmash_form_iso_box_type( type );
    lsm_write_set_box_writer( (isom_box_t *)tref_type );
    if( box_add_to_extension_list( tref, tref_type ) < 0 )
    {
        lsmash_free( tref_type );
        return lsm_box_default_non_existing_tref_type();
    }
    if( lsmash_list_add_entry( &tref->ref_list, tref_type ) < 0 )
    {
        lsmash_list_remove_entry_tail( &tref->extensions );
        return lsm_box_default_non_existing_tref_type();
    }
    tref_type->offset_in_parent = offsetof( isom_tref_t, ref_list );
    return tref_type;
}

DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_terminator, terminator, wave, QT_BOX_TYPE_TERMINATOR, LSMASH_BOX_PRECEDENCE_QTFF_TERMINATOR )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_frma, frma, wave,   QT_BOX_TYPE_FRMA, LSMASH_BOX_PRECEDENCE_QTFF_FRMA )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_enda, enda, wave,   QT_BOX_TYPE_ENDA, LSMASH_BOX_PRECEDENCE_QTFF_ENDA )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_mp4a, mp4a, wave,   QT_BOX_TYPE_MP4A, LSMASH_BOX_PRECEDENCE_QTFF_MP4A )
DEFINE_SIMPLE_LIST_BOX_ADDER( lsm_box_add_ftab, ftab, tx3g_entry, ISOM_BOX_TYPE_FTAB, LSMASH_BOX_PRECEDENCE_ISOM_FTAB )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_ftyp, ftyp, file_abstract, ISOM_BOX_TYPE_FTYP, LSMASH_BOX_PRECEDENCE_ISOM_FTYP )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_moov, moov, file_abstract, ISOM_BOX_TYPE_MOOV, LSMASH_BOX_PRECEDENCE_ISOM_MOOV )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_mvhd, mvhd, moov,          ISOM_BOX_TYPE_MVHD, LSMASH_BOX_PRECEDENCE_ISOM_MVHD )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_iods, iods, moov,          ISOM_BOX_TYPE_IODS, LSMASH_BOX_PRECEDENCE_ISOM_IODS )

isom_ctab_t *lsm_box_add_ctab( void *parent_box )
{
    /* According to QuickTime File Format Specification, this box is placed inside Movie Box if present.
     * However, sometimes this box occurs inside an image description entry or the end of Sample Description Box. */
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( LSMASH_IS_NON_EXISTING_BOX( parent ) )
        return lsm_box_default_non_existing_ctab();
    CREATE_BOX( ctab, parent, QT_BOX_TYPE_CTAB, LSMASH_BOX_PRECEDENCE_QTFF_CTAB, 1 );
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MOOV ) )
        ATTACH_EXACTLY_ONE_BOX_TO_PARENT( ctab, isom_moov_t );
    return ctab;
}

isom_trak_t *lsm_box_add_trak( isom_moov_t *moov )
{
    if( LSMASH_IS_NON_EXISTING_BOX( moov )
     || LSMASH_IS_NON_EXISTING_BOX( moov->file ) )
        return lsm_box_default_non_existing_trak();
    CREATE_BOX( trak, moov, ISOM_BOX_TYPE_TRAK, LSMASH_BOX_PRECEDENCE_ISOM_TRAK, 1 );
    isom_fragment_t *fragment = NULL;
    isom_cache_t    *cache    = lsmash_malloc_zero( sizeof(isom_cache_t) );
    if( !cache )
        goto fail;
    if( moov->file->fragment )
    {
        fragment = lsmash_malloc_zero( sizeof(isom_fragment_t) );
        if( !fragment )
            goto fail;
        cache->fragment = fragment;
        fragment->largest_cts                 = LSMASH_TIMESTAMP_UNDEFINED;
        fragment->subsegment.largest_cts      = LSMASH_TIMESTAMP_UNDEFINED;
        fragment->subsegment.smallest_cts     = LSMASH_TIMESTAMP_UNDEFINED;
        fragment->subsegment.first_sample_cts = LSMASH_TIMESTAMP_UNDEFINED;
        fragment->subsegment.first_ed_cts     = LSMASH_TIMESTAMP_UNDEFINED;
        fragment->subsegment.first_rp_cts     = LSMASH_TIMESTAMP_UNDEFINED;
    }
    if( lsmash_list_add_entry( &moov->trak_list, trak ) < 0 )
        goto fail;
    trak->offset_in_parent = offsetof( isom_moov_t, trak_list );
    trak->cache            = cache;
    return trak;
fail:
    lsmash_free( fragment );
    lsmash_free( cache );
    lsmash_list_remove_entry_tail( &moov->extensions );
    return lsm_box_default_non_existing_trak();
}

DEFINE_SIMPLE_BOX_ADDER     ( lsm_box_add_tkhd, tkhd, trak, ISOM_BOX_TYPE_TKHD, LSMASH_BOX_PRECEDENCE_ISOM_TKHD )
DEFINE_SIMPLE_BOX_ADDER     ( lsm_box_add_tapt, tapt, trak,   QT_BOX_TYPE_TAPT, LSMASH_BOX_PRECEDENCE_QTFF_TAPT )
DEFINE_SIMPLE_BOX_ADDER     ( lsm_box_add_clef, clef, tapt,   QT_BOX_TYPE_CLEF, LSMASH_BOX_PRECEDENCE_QTFF_CLEF )
DEFINE_SIMPLE_BOX_ADDER     ( lsm_box_add_prof, prof, tapt,   QT_BOX_TYPE_PROF, LSMASH_BOX_PRECEDENCE_QTFF_PROF )
DEFINE_SIMPLE_BOX_ADDER     ( lsm_box_add_enof, enof, tapt,   QT_BOX_TYPE_ENOF, LSMASH_BOX_PRECEDENCE_QTFF_ENOF )
DEFINE_SIMPLE_BOX_ADDER           ( lsm_box_add_edts, edts, trak, ISOM_BOX_TYPE_EDTS, LSMASH_BOX_PRECEDENCE_ISOM_EDTS )
DEFINE_SIMPLE_PAGED_LIST_BOX_ADDER( lsm_box_add_elst, elst, edts, ISOM_BOX_TYPE_ELST, LSMASH_BOX_PRECEDENCE_ISOM_ELST )
DEFINE_SIMPLE_BOX_ADDER           ( lsm_box_add_tref, tref, trak, ISOM_BOX_TYPE_TREF, LSMASH_BOX_PRECEDENCE_ISOM_TREF )
DEFINE_SIMPLE_BOX_ADDER     ( lsm_box_add_mdia, mdia, trak, ISOM_BOX_TYPE_MDIA, LSMASH_BOX_PRECEDENCE_ISOM_MDIA )
DEFINE_SIMPLE_BOX_ADDER     ( lsm_box_add_mdhd, mdhd, mdia, ISOM_BOX_TYPE_MDHD, LSMASH_BOX_PRECEDENCE_ISOM_MDHD )

isom_hdlr_t *lsm_box_add_hdlr( void *parent_box )
{
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( LSMASH_IS_NON_EXISTING_BOX( parent ) )
        return lsm_box_default_non_existing_hdlr();
    CREATE_BOX( hdlr, parent, ISOM_BOX_TYPE_HDLR, LSMASH_BOX_PRECEDENCE_ISOM_HDLR, 1 );
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MDIA ) )
        ATTACH_EXACTLY_ONE_BOX_TO_PARENT( hdlr, isom_mdia_t );
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_META )
          || lsmash_check_box_type_identical( parent->type,   QT_BOX_TYPE_META ) )
        ATTACH_EXACTLY_ONE_BOX_TO_PARENT( hdlr, isom_meta_t );
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MINF ) )
        ATTACH_EXACTLY_ONE_BOX_TO_PARENT( hdlr, isom_minf_t );
    else
        assert( 0 );
    return hdlr;
}

DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_minf, minf, mdia, ISOM_BOX_TYPE_MINF, LSMASH_BOX_PRECEDENCE_ISOM_MINF )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_vmhd, vmhd, minf, ISOM_BOX_TYPE_VMHD, LSMASH_BOX_PRECEDENCE_ISOM_VMHD )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_smhd, smhd, minf, ISOM_BOX_TYPE_SMHD, LSMASH_BOX_PRECEDENCE_ISOM_SMHD )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_hmhd, hmhd, minf, ISOM_BOX_TYPE_HMHD, LSMASH_BOX_PRECEDENCE_ISOM_HMHD )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_nmhd, nmhd, minf, ISOM_BOX_TYPE_NMHD, LSMASH_BOX_PRECEDENCE_ISOM_NMHD )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_gmhd, gmhd, minf,   QT_BOX_TYPE_GMHD, LSMASH_BOX_PRECEDENCE_QTFF_GMHD )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_gmin, gmin, gmhd,   QT_BOX_TYPE_GMIN, LSMASH_BOX_PRECEDENCE_QTFF_GMIN )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_text, text, gmhd,   QT_BOX_TYPE_TEXT, LSMASH_BOX_PRECEDENCE_QTFF_TEXT )

isom_dinf_t *lsm_box_add_dinf( void *parent_box )
{
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( LSMASH_IS_NON_EXISTING_BOX( parent ) )
        return lsm_box_default_non_existing_dinf();
    CREATE_BOX( dinf, parent, ISOM_BOX_TYPE_DINF, LSMASH_BOX_PRECEDENCE_ISOM_DINF, 1 );
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MINF ) )
        ATTACH_EXACTLY_ONE_BOX_TO_PARENT( dinf, isom_minf_t );
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_META )
          || lsmash_check_box_type_identical( parent->type,   QT_BOX_TYPE_META ) )
        ATTACH_EXACTLY_ONE_BOX_TO_PARENT( dinf, isom_meta_t );
    else
        assert( 0 );
    return dinf;
}

isom_dref_entry_t *lsm_box_add_dref_entry( isom_dref_t *dref, lsmash_box_type_t type )
{
    if( LSMASH_IS_NON_EXISTING_BOX( dref ) )
        return lsm_box_default_non_existing_dref_entry();
    isom_dref_entry_t *dref_entry = ALLOCATE_BOX( dref_entry );
    if( LSMASH_IS_NON_EXISTING_BOX( dref_entry ) )
        return dref_entry;
    lsm_box_init_common( dref_entry, dref, type, LSMASH_BOX_PRECEDENCE_ISOM_DREF_ENTRY, box_remove_dref_entry );
    if( box_add_to_extension_list( dref, dref_entry ) < 0 )
    {
        lsmash_free( dref_entry );
        return lsm_box_default_non_existing_dref_entry();
    }
    if( lsmash_list_add_entry( &dref->list, dref_entry ) < 0 )
    {
        lsmash_list_remove_entry_tail( &dref->extensions );
        return lsm_box_default_non_existing_dref_entry();
    }
    dref_entry->offset_in_parent = offsetof( isom_dref_t, list );
    return dref_entry;
}

DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_dref, dref, dinf, ISOM_BOX_TYPE_DREF, LSMASH_BOX_PRECEDENCE_ISOM_DREF )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_stbl, stbl, minf, ISOM_BOX_TYPE_STBL, LSMASH_BOX_PRECEDENCE_ISOM_STBL )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_stsd, stsd, stbl, ISOM_BOX_TYPE_STSD, LSMASH_BOX_PRECEDENCE_ISOM_STSD )

static void *box_add_sample_description_entry
(
    isom_stsd_t *stsd,
    void        *description
)
{
    assert( description );
    if( box_add_to_extension_list( stsd, description ) < 0 )
    {
        lsm_box_remove_by_itself( description );
        return description;
    }
    if( lsmash_list_add_entry( &stsd->list, description ) < 0 )
    {
        lsmash_list_remove_entry_tail( &stsd->extensions );
        return description;
    }
    ((isom_box_t *)description)->offset_in_parent = offsetof( isom_stsd_t, list );
    return description;
}

isom_visual_entry_t *lsm_box_add_visual_description( isom_stsd_t *stsd, lsmash_codec_type_t sample_type )
{
    assert( LSMASH_IS_EXISTING_BOX( stsd ) );
    isom_visual_entry_t *visual = ALLOCATE_BOX( visual_entry );
    if( LSMASH_IS_NON_EXISTING_BOX( visual ) )
        return visual;
    lsm_box_init_common( visual, stsd, sample_type, LSMASH_BOX_PRECEDENCE_HM, box_remove_visual_description );
    visual->manager |= LSMASH_VIDEO_DESCRIPTION;
    return box_add_sample_description_entry( stsd, visual );
}

isom_audio_entry_t *lsm_box_add_audio_description( isom_stsd_t *stsd, lsmash_codec_type_t sample_type )
{
    assert( LSMASH_IS_EXISTING_BOX( stsd ) );
    isom_audio_entry_t *audio = ALLOCATE_BOX( audio_entry );
    if( LSMASH_IS_NON_EXISTING_BOX( audio ) )
        return audio;
    lsm_box_init_common( audio, stsd, sample_type, LSMASH_BOX_PRECEDENCE_HM, box_remove_audio_description );
    audio->manager |= LSMASH_AUDIO_DESCRIPTION;
    return box_add_sample_description_entry( stsd, audio );
}

isom_hint_entry_t *lsm_box_add_hint_description( isom_stsd_t *stsd, lsmash_codec_type_t sample_type )
{
    assert( stsd );
    isom_hint_entry_t *hint = ALLOCATE_BOX( hint_entry );
    if ( LSMASH_IS_NON_EXISTING_BOX( hint ) )
        return hint;
    lsm_box_init_common( hint, stsd, sample_type, LSMASH_BOX_PRECEDENCE_HM, box_remove_hint_description );
    return box_add_sample_description_entry( stsd, hint );
}

isom_qt_text_entry_t *lsm_box_add_qt_text_description( isom_stsd_t *stsd )
{
    assert( LSMASH_IS_EXISTING_BOX( stsd ) );
    isom_qt_text_entry_t *text = ALLOCATE_BOX( qt_text_entry );
    if( LSMASH_IS_NON_EXISTING_BOX( text ) )
        return text;
    lsm_box_init_common( text, stsd, QT_CODEC_TYPE_TEXT_TEXT, LSMASH_BOX_PRECEDENCE_HM, box_remove_qt_text_description );
    return box_add_sample_description_entry( stsd, text );
}

isom_tx3g_entry_t *lsm_box_add_tx3g_description( isom_stsd_t *stsd )
{
    assert( LSMASH_IS_EXISTING_BOX( stsd ) );
    isom_tx3g_entry_t *tx3g = ALLOCATE_BOX( tx3g_entry );
    if( LSMASH_IS_NON_EXISTING_BOX( tx3g ) )
        return tx3g;
    lsm_box_init_common( tx3g, stsd, ISOM_CODEC_TYPE_TX3G_TEXT, LSMASH_BOX_PRECEDENCE_HM, box_remove_tx3g_description );
    return box_add_sample_description_entry( stsd, tx3g );
}

isom_esds_t *lsm_box_add_esds( void *parent_box )
{
    isom_box_t *parent = (isom_box_t *)parent_box;
    int is_qt = lsmash_check_box_type_identical( parent->type, QT_BOX_TYPE_WAVE );
    lsmash_box_type_t box_type   = is_qt ? QT_BOX_TYPE_ESDS : ISOM_BOX_TYPE_ESDS;
    uint64_t          precedence = is_qt ? LSMASH_BOX_PRECEDENCE_QTFF_ESDS : LSMASH_BOX_PRECEDENCE_ISOM_ESDS;
    CREATE_BOX( esds, parent, box_type, precedence, 1 );
    return esds;
}

DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_glbl, glbl, parent_box, QT_BOX_TYPE_GLBL, LSMASH_BOX_PRECEDENCE_QTFF_GLBL, 1, void )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_clap, clap, visual, ISOM_BOX_TYPE_CLAP, LSMASH_BOX_PRECEDENCE_ISOM_CLAP, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_pasp, pasp, visual, ISOM_BOX_TYPE_PASP, LSMASH_BOX_PRECEDENCE_ISOM_PASP, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_colr, colr, visual, ISOM_BOX_TYPE_COLR, LSMASH_BOX_PRECEDENCE_ISOM_COLR, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_gama, gama, visual,   QT_BOX_TYPE_GAMA, LSMASH_BOX_PRECEDENCE_QTFF_GAMA, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_fiel, fiel, visual,   QT_BOX_TYPE_FIEL, LSMASH_BOX_PRECEDENCE_QTFF_FIEL, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_clli, clli, visual,   QT_BOX_TYPE_CLLI, LSMASH_BOX_PRECEDENCE_QTFF_CLLI, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_mdcv, mdcv, visual,   QT_BOX_TYPE_MDCV, LSMASH_BOX_PRECEDENCE_QTFF_MDCV, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_cspc, cspc, visual,   QT_BOX_TYPE_CSPC, LSMASH_BOX_PRECEDENCE_QTFF_CSPC, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_sgbt, sgbt, visual,   QT_BOX_TYPE_SGBT, LSMASH_BOX_PRECEDENCE_QTFF_SGBT, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_stsl, stsl, visual, ISOM_BOX_TYPE_STSL, LSMASH_BOX_PRECEDENCE_ISOM_STSL, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_btrt, btrt, visual, ISOM_BOX_TYPE_BTRT, LSMASH_BOX_PRECEDENCE_ISOM_BTRT, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_dovi, dovi, visual, ISOM_BOX_TYPE_DVVC, LSMASH_BOX_PRECEDENCE_ISOM_DOVI, 0, isom_visual_entry_t ) // DVVC is just a placeholder
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_lhvC, lhvC, visual, ISOM_BOX_TYPE_LHVC, LSMASH_BOX_PRECEDENCE_ISOM_LHVC, 1, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_st3d, st3d, visual, ISOM_BOX_TYPE_ST3D, LSMASH_BOX_PRECEDENCE_ISOM_ST3D, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_sv3d, sv3d, visual, ISOM_BOX_TYPE_SV3D, LSMASH_BOX_PRECEDENCE_ISOM_SV3D, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_svhd, svhd, sv3d,   ISOM_BOX_TYPE_SVHD, LSMASH_BOX_PRECEDENCE_ISOM_SVHD, 1, isom_sv3d_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_proj, proj, sv3d,   ISOM_BOX_TYPE_PROJ, LSMASH_BOX_PRECEDENCE_ISOM_PROJ, 0, isom_sv3d_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_prhd, prhd, proj,   ISOM_BOX_TYPE_PRHD, LSMASH_BOX_PRECEDENCE_ISOM_PRHD, 0, isom_proj_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_equi, equi, proj,   ISOM_BOX_TYPE_EQUI, LSMASH_BOX_PRECEDENCE_ISOM_EQUI, 0, isom_proj_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_cbmp, cbmp, proj,   ISOM_BOX_TYPE_CBMP, LSMASH_BOX_PRECEDENCE_ISOM_CBMP, 0, isom_proj_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_prji, prji, proj,   ISOM_BOX_TYPE_PRJI, LSMASH_BOX_PRECEDENCE_ISOM_PRJI, 0, isom_proj_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_vexu, vexu, visual, ISOM_BOX_TYPE_VEXU, LSMASH_BOX_PRECEDENCE_ISOM_VEXU, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_eyes, eyes, vexu,   ISOM_BOX_TYPE_EYES, LSMASH_BOX_PRECEDENCE_ISOM_EYES, 0, isom_vexu_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_must, must, eyes,   ISOM_BOX_TYPE_MUST, LSMASH_BOX_PRECEDENCE_ISOM_MUST, 1, isom_eyes_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_stri, stri, eyes,   ISOM_BOX_TYPE_STRI, LSMASH_BOX_PRECEDENCE_ISOM_STRI, 0, isom_eyes_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_hero, hero, eyes,   ISOM_BOX_TYPE_HERO, LSMASH_BOX_PRECEDENCE_ISOM_HERO, 0, isom_eyes_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_cams, cams, eyes,   ISOM_BOX_TYPE_CAMS, LSMASH_BOX_PRECEDENCE_ISOM_CAMS, 0, isom_eyes_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_blin, blin, cams,   ISOM_BOX_TYPE_BLIN, LSMASH_BOX_PRECEDENCE_ISOM_BLIN, 0, isom_cams_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_cmfy, cmfy, eyes,   ISOM_BOX_TYPE_CMFY, LSMASH_BOX_PRECEDENCE_ISOM_CMFY, 0, isom_eyes_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_dadj, dadj, cmfy,   ISOM_BOX_TYPE_DADJ, LSMASH_BOX_PRECEDENCE_ISOM_DADJ, 0, isom_cmfy_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_hfov, hfov, visual, ISOM_BOX_TYPE_HFOV, LSMASH_BOX_PRECEDENCE_ISOM_HFOV, 0, isom_visual_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_wave, wave, audio,    QT_BOX_TYPE_WAVE, LSMASH_BOX_PRECEDENCE_QTFF_WAVE, 0, isom_audio_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_chan, chan, audio,    QT_BOX_TYPE_CHAN, LSMASH_BOX_PRECEDENCE_QTFF_CHAN, 1, isom_audio_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_srat, srat, audio,  ISOM_BOX_TYPE_SRAT, LSMASH_BOX_PRECEDENCE_ISOM_SRAT, 0, isom_audio_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_pcmC, pcmC, audio,  ISOM_BOX_TYPE_PCMC, LSMASH_BOX_PRECEDENCE_ISOM_PCMC, 0, isom_audio_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_chnl, chnl, audio,  ISOM_BOX_TYPE_CHNL, LSMASH_BOX_PRECEDENCE_ISOM_CHNL, 1, isom_audio_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_SA3D, SA3D, audio,  ISOM_BOX_TYPE_SA3D, LSMASH_BOX_PRECEDENCE_ISOM_SA3D, 1, isom_audio_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_tims, tims, hint,   ISOM_BOX_TYPE_TIMS, LSMASH_BOX_PRECEDENCE_ISOM_TIMS, 0, isom_hint_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_tsro, tsro, hint,   ISOM_BOX_TYPE_TSRO, LSMASH_BOX_PRECEDENCE_ISOM_TSRO, 0, isom_hint_entry_t )
DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER( lsm_box_add_tssy, tssy, hint,   ISOM_BOX_TYPE_TSSY, LSMASH_BOX_PRECEDENCE_ISOM_TSSY, 0, isom_hint_entry_t )

DEFINE_SIMPLE_PAGED_LIST_BOX_ADDER( lsm_box_add_stts, stts, stbl, ISOM_BOX_TYPE_STTS, LSMASH_BOX_PRECEDENCE_ISOM_STTS )
DEFINE_SIMPLE_PAGED_LIST_BOX_ADDER( lsm_box_add_ctts, ctts, stbl, ISOM_BOX_TYPE_CTTS, LSMASH_BOX_PRECEDENCE_ISOM_CTTS )
DEFINE_SIMPLE_BOX_ADDER           ( lsm_box_add_cslg, cslg, stbl, ISOM_BOX_TYPE_CSLG, LSMASH_BOX_PRECEDENCE_ISOM_CSLG )
DEFINE_SIMPLE_PAGED_LIST_BOX_ADDER( lsm_box_add_stsc, stsc, stbl, ISOM_BOX_TYPE_STSC, LSMASH_BOX_PRECEDENCE_ISOM_STSC )
DEFINE_SIMPLE_BOX_ADDER           ( lsm_box_add_stsz, stsz, stbl, ISOM_BOX_TYPE_STSZ, LSMASH_BOX_PRECEDENCE_ISOM_STSZ )  /* We don't create a list here. */
DEFINE_SIMPLE_PAGED_LIST_BOX_ADDER( lsm_box_add_stz2, stz2, stbl, ISOM_BOX_TYPE_STZ2, LSMASH_BOX_PRECEDENCE_ISOM_STZ2 )
DEFINE_SIMPLE_PAGED_LIST_BOX_ADDER( lsm_box_add_stss, stss, stbl, ISOM_BOX_TYPE_STSS, LSMASH_BOX_PRECEDENCE_ISOM_STSS )
DEFINE_SIMPLE_PAGED_LIST_BOX_ADDER( lsm_box_add_stps, stps, stbl,   QT_BOX_TYPE_STPS, LSMASH_BOX_PRECEDENCE_QTFF_STPS )

isom_stco_t *lsm_box_add_stco( isom_stbl_t *stbl )
{
    ADD_PAGED_LIST_BOX( stco, stbl, ISOM_BOX_TYPE_STCO, LSMASH_BOX_PRECEDENCE_ISOM_STCO );
    stco->large_presentation = 0;
    return stco;
}

isom_stco_t *lsm_box_add_co64( isom_stbl_t *stbl )
{
    ADD_PAGED_LIST_BOX( stco, stbl, ISOM_BOX_TYPE_CO64, LSMASH_BOX_PRECEDENCE_ISOM_CO64 );
    stco->large_presentation = 1;
    return stco;
}

isom_sdtp_t *lsm_box_add_sdtp( isom_box_t *parent )
{
    if( LSMASH_IS_NON_EXISTING_BOX( parent ) )
        return lsm_box_default_non_existing_sdtp();
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STBL ) )
    {
        isom_stbl_t *stbl = (isom_stbl_t *)parent;
        ADD_PAGED_LIST_BOX( sdtp, stbl, ISOM_BOX_TYPE_SDTP, LSMASH_BOX_PRECEDENCE_ISOM_SDTP );
        return sdtp;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAF ) )
    {
        isom_traf_t *traf = (isom_traf_t *)parent;
        ADD_PAGED_LIST_BOX( sdtp, traf, ISOM_BOX_TYPE_SDTP, LSMASH_BOX_PRECEDENCE_ISOM_SDTP );
        return sdtp;
    }
    assert( 0 );
    return lsm_box_default_non_existing_sdtp();
}

isom_sgpd_t *lsm_box_add_sgpd( void *parent_box )
{
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( LSMASH_IS_NON_EXISTING_BOX( parent ) )
        return lsm_box_default_non_existing_sgpd();
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STBL ) )
    {
        isom_stbl_t *stbl = (isom_stbl_t *)parent;
        ADD_PAGED_LIST_BOX_IN_LIST( sgpd, stbl, ISOM_BOX_TYPE_SGPD, LSMASH_BOX_PRECEDENCE_ISOM_SGPD );
        return sgpd;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAF ) )
    {
        isom_traf_t *traf = (isom_traf_t *)parent;
        ADD_PAGED_LIST_BOX_IN_LIST( sgpd, traf, ISOM_BOX_TYPE_SGPD, LSMASH_BOX_PRECEDENCE_ISOM_SGPD );
        return sgpd;
    }
    assert( 0 );
    return lsm_box_default_non_existing_sgpd();
}

isom_sbgp_t *lsm_box_add_sbgp( void *parent_box )
{
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( LSMASH_IS_NON_EXISTING_BOX( parent ) )
        return lsm_box_default_non_existing_sbgp();
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STBL ) )
    {
        isom_stbl_t *stbl = (isom_stbl_t *)parent;
        ADD_PAGED_LIST_BOX_IN_LIST( sbgp, stbl, ISOM_BOX_TYPE_SBGP, LSMASH_BOX_PRECEDENCE_ISOM_SBGP );
        return sbgp;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAF ) )
    {
        isom_traf_t *traf = (isom_traf_t *)parent;
        ADD_PAGED_LIST_BOX_IN_LIST( sbgp, traf, ISOM_BOX_TYPE_SBGP, LSMASH_BOX_PRECEDENCE_ISOM_SBGP );
        return sbgp;
    }
    assert( 0 );
    return lsm_box_default_non_existing_sbgp();
}

DEFINE_SIMPLE_LIST_BOX_ADDER( lsm_box_add_chpl, chpl, udta, ISOM_BOX_TYPE_CHPL, LSMASH_BOX_PRECEDENCE_ISOM_CHPL )

isom_metaitem_t *lsm_box_add_metaitem( isom_ilst_t *ilst, lsmash_itunes_metadata_item item )
{
    if( LSMASH_IS_NON_EXISTING_BOX( ilst ) )
        return lsm_box_default_non_existing_metaitem();
    lsmash_box_type_t type = lsmash_form_iso_box_type( item );
    ADD_BOX_IN_LIST( metaitem, ilst, type, LSMASH_BOX_PRECEDENCE_ISOM_METAITEM );
    return metaitem;
}

DEFINE_SIMPLE_BOX_ADDER     ( lsm_box_add_mean, mean, metaitem, ISOM_BOX_TYPE_MEAN, LSMASH_BOX_PRECEDENCE_ISOM_MEAN )
DEFINE_SIMPLE_BOX_ADDER     ( lsm_box_add_name, name, metaitem, ISOM_BOX_TYPE_NAME, LSMASH_BOX_PRECEDENCE_ISOM_NAME )
DEFINE_SIMPLE_BOX_ADDER     ( lsm_box_add_data, data, metaitem, ISOM_BOX_TYPE_DATA, LSMASH_BOX_PRECEDENCE_ISOM_DATA )
DEFINE_SIMPLE_BOX_ADDER     ( lsm_box_add_ilst, ilst, meta,     ISOM_BOX_TYPE_ILST, LSMASH_BOX_PRECEDENCE_ISOM_ILST )
DEFINE_SIMPLE_LIST_BOX_ADDER( lsm_box_add_keys, keys, meta,       QT_BOX_TYPE_KEYS, LSMASH_BOX_PRECEDENCE_QTFF_KEYS )

isom_meta_t *lsm_box_add_meta( void *parent_box )
{
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( LSMASH_IS_NON_EXISTING_BOX( parent ) )
        return lsm_box_default_non_existing_meta();
    CREATE_BOX( meta, parent, ISOM_BOX_TYPE_META, LSMASH_BOX_PRECEDENCE_ISOM_META, 1 );
    if( parent->file == (lsmash_file_t *)parent )
        ATTACH_EXACTLY_ONE_BOX_TO_PARENT( meta, lsmash_file_t );
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MOOV ) )
        ATTACH_EXACTLY_ONE_BOX_TO_PARENT( meta, isom_moov_t );
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAK ) )
        ATTACH_EXACTLY_ONE_BOX_TO_PARENT( meta, isom_trak_t );
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_UDTA ) )
        ATTACH_EXACTLY_ONE_BOX_TO_PARENT( meta, isom_udta_t );
    else
        assert( 0 );
    return meta;
}

DEFINE_SIMPLE_BOX_IN_LIST_ADDER( lsm_box_add_cprt, cprt, udta, ISOM_BOX_TYPE_CPRT, LSMASH_BOX_PRECEDENCE_ISOM_CPRT )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_hnti, hnti, udta, ISOM_BOX_TYPE_HNTI, LSMASH_BOX_PRECEDENCE_ISOM_HNTI )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_rtp, rtp, hnti, ISOM_BOX_TYPE_RTP, LSMASH_BOX_PRECEDENCE_ISOM_RTP )
DEFINE_SIMPLE_BOX_ADDER( lsm_box_add_sdp, sdp, hnti, ISOM_BOX_TYPE_SDP, LSMASH_BOX_PRECEDENCE_ISOM_SDP )

isom_udta_t *lsm_box_add_udta( void *parent_box )
{
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( LSMASH_IS_NON_EXISTING_BOX( parent ) )
        return lsm_box_default_non_existing_udta();
    if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_MOOV ) )
    {
        isom_moov_t *moov = (isom_moov_t *)parent;
        ADD_BOX( udta, moov, ISOM_BOX_TYPE_UDTA, LSMASH_BOX_PRECEDENCE_ISOM_UDTA );
        return udta;
    }
    else if( lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_TRAK ) )
    {
        isom_trak_t *trak = (isom_trak_t *)parent;
        ADD_BOX( udta, trak, ISOM_BOX_TYPE_UDTA, LSMASH_BOX_PRECEDENCE_ISOM_UDTA );
        return udta;
    }
    assert( 0 );
    return lsm_box_default_non_existing_udta();
}

DEFINE_SIMPLE_BOX_ADDER        ( lsm_box_add_WLOC, WLOC, udta,   QT_BOX_TYPE_WLOC, LSMASH_BOX_PRECEDENCE_QTFF_WLOC )
DEFINE_SIMPLE_BOX_ADDER        ( lsm_box_add_LOOP, LOOP, udta,   QT_BOX_TYPE_LOOP, LSMASH_BOX_PRECEDENCE_QTFF_LOOP )
DEFINE_SIMPLE_BOX_ADDER        ( lsm_box_add_SelO, SelO, udta,   QT_BOX_TYPE_SELO, LSMASH_BOX_PRECEDENCE_QTFF_SELO )
DEFINE_SIMPLE_BOX_ADDER        ( lsm_box_add_AllF, AllF, udta,   QT_BOX_TYPE_ALLF, LSMASH_BOX_PRECEDENCE_QTFF_ALLF )
DEFINE_SIMPLE_BOX_ADDER        ( lsm_box_add_mvex, mvex, moov, ISOM_BOX_TYPE_MVEX, LSMASH_BOX_PRECEDENCE_ISOM_MVEX )
DEFINE_SIMPLE_BOX_ADDER        ( lsm_box_add_mehd, mehd, mvex, ISOM_BOX_TYPE_MEHD, LSMASH_BOX_PRECEDENCE_ISOM_MEHD )
DEFINE_SIMPLE_BOX_IN_LIST_ADDER( lsm_box_add_trex, trex, mvex, ISOM_BOX_TYPE_TREX, LSMASH_BOX_PRECEDENCE_ISOM_TREX )
DEFINE_SIMPLE_BOX_IN_LIST_ADDER( lsm_box_add_moof, moof, file_abstract, ISOM_BOX_TYPE_MOOF, LSMASH_BOX_PRECEDENCE_ISOM_MOOF )
DEFINE_SIMPLE_BOX_ADDER        ( lsm_box_add_mfhd, mfhd, moof, ISOM_BOX_TYPE_MFHD, LSMASH_BOX_PRECEDENCE_ISOM_MFHD )
DEFINE_SIMPLE_BOX_IN_LIST_ADDER( lsm_box_add_traf, traf, moof, ISOM_BOX_TYPE_TRAF, LSMASH_BOX_PRECEDENCE_ISOM_TRAF )
DEFINE_SIMPLE_BOX_ADDER        ( lsm_box_add_tfhd, tfhd, traf, ISOM_BOX_TYPE_TFHD, LSMASH_BOX_PRECEDENCE_ISOM_TFHD )
DEFINE_SIMPLE_BOX_ADDER        ( lsm_box_add_tfdt, tfdt, traf, ISOM_BOX_TYPE_TFDT, LSMASH_BOX_PRECEDENCE_ISOM_TFDT )
DEFINE_SIMPLE_BOX_IN_LIST_ADDER( lsm_box_add_trun, trun, traf, ISOM_BOX_TYPE_TRUN, LSMASH_BOX_PRECEDENCE_ISOM_TRUN )
DEFINE_SIMPLE_BOX_ADDER        ( lsm_box_add_mfra, mfra, file_abstract, ISOM_BOX_TYPE_MFRA, LSMASH_BOX_PRECEDENCE_ISOM_MFRA )
DEFINE_SIMPLE_BOX_IN_LIST_ADDER( lsm_box_add_tfra, tfra, mfra, ISOM_BOX_TYPE_TFRA, LSMASH_BOX_PRECEDENCE_ISOM_TFRA )
DEFINE_SIMPLE_BOX_ADDER        ( lsm_box_add_mfro, mfro, mfra, ISOM_BOX_TYPE_MFRO, LSMASH_BOX_PRECEDENCE_ISOM_MFRO )
DEFINE_SIMPLE_BOX_IN_LIST_ADDER( lsm_box_add_emsg, emsg, file_abstract, ISOM_BOX_TYPE_EMSG, LSMASH_BOX_PRECEDENCE_ISOM_EMSG )

isom_mdat_t *lsm_box_add_mdat( isom_file_abstract_t *file )
{
    assert( LSMASH_IS_NON_EXISTING_BOX( file->mdat ) );
    CREATE_BOX( mdat, file, ISOM_BOX_TYPE_MDAT, LSMASH_BOX_PRECEDENCE_ISOM_MDAT, 1 );
    file->mdat = mdat;
    return mdat;
}

isom_free_t *lsm_box_add_free( void *parent_box )
{
    isom_box_t *parent = (isom_box_t *)parent_box;
    if( LSMASH_IS_NON_EXISTING_BOX( parent ) )
        return lsm_box_default_non_existing_skip();
    CREATE_BOX( skip, parent, ISOM_BOX_TYPE_FREE, LSMASH_BOX_PRECEDENCE_ISOM_FREE, 1 );
    return skip;
}

DEFINE_SIMPLE_BOX_IN_LIST_ADDER( lsm_box_add_styp, styp, file_abstract, ISOM_BOX_TYPE_STYP, LSMASH_BOX_PRECEDENCE_ISOM_STYP )

isom_sidx_t *lsm_box_add_sidx( isom_file_abstract_t *file_abstract )
{
    ADD_PAGED_LIST_BOX_IN_LIST( sidx, file_abstract, ISOM_BOX_TYPE_SIDX, LSMASH_BOX_PRECEDENCE_ISOM_SIDX );
    return sidx;
}

#undef ATTACH_EXACTLY_ONE_BOX_TO_PARENT
#undef CREATE_BOX
#undef CREATE_LIST_BOX
#undef CREATE_PAGED_LIST_BOX
#undef ADD_BOX_TEMPLATE
#undef ADD_BOX_IN_LIST_TEMPLATE
#undef ADD_BOX
#undef ADD_BOX_IN_LIST
#undef ADD_LIST_BOX
#undef ADD_LIST_BOX_IN_LIST
#undef ADD_PAGED_LIST_BOX
#undef ADD_PAGED_LIST_BOX_IN_LIST
#undef DEFINE_SIMPLE_BOX_ADDER_TEMPLATE
#undef DEFINE_SIMPLE_BOX_ADDER_TEMPLATE_6
#undef DEFINE_SIMPLE_BOX_ADDER_TEMPLATE_5
#undef DEFINE_SIMPLE_BOX_ADDER
#undef DEFINE_SIMPLE_BOX_IN_LIST_ADDER
#undef DEFINE_SIMPLE_LIST_BOX_ADDER
#undef DEFINE_SIMPLE_PAGED_LIST_BOX_ADDER

static int box_fake_file_read
(
    void    *opaque,
    uint8_t *buf,
    int      size
)
{
    fake_file_stream_t *stream = (fake_file_stream_t *)opaque;
    int read_size;
    if( stream->pos + size > stream->size )
        read_size = stream->size - stream->pos;
    else
        read_size = size;
    memcpy( buf, stream->data + stream->pos, read_size );
    stream->pos += read_size;
    return read_size;
}

static int64_t box_fake_file_seek
(
    void   *opaque,
    int64_t offset,
    int     whence
)
{
    fake_file_stream_t *stream = (fake_file_stream_t *)opaque;
    if( whence == SEEK_SET )
        stream->pos = offset;
    else if( whence == SEEK_CUR )
        stream->pos += offset;
    else if( whence == SEEK_END )
        stream->pos = stream->size + offset;
    return stream->pos;
}

/* Public functions */
lsmash_root_t *lsmash_create_root( void )
{
    lsmash_root_t *root = ALLOCATE_BOX( root_abstract );
    if( LSMASH_IS_NON_EXISTING_BOX( root ) )
        return NULL;
    root->root = root;
    return root;
}

void lsmash_destroy_root( lsmash_root_t *root )
{
    lsm_box_remove_by_itself( root );
}

lsmash_extended_box_type_t lsmash_form_extended_box_type( uint32_t fourcc, const uint8_t id[12] )
{
    return (lsmash_extended_box_type_t){ fourcc, { id[0], id[1], id[2], id[3], id[4],  id[5],
                                                   id[6], id[7], id[8], id[9], id[10], id[11] } };
}

lsmash_box_type_t lsmash_form_box_type
(
    lsmash_compact_box_type_t  type,
    lsmash_extended_box_type_t user
)
{
    return (lsmash_box_type_t){ type, user };
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

int lsmash_check_box_type_specified( const lsmash_box_type_t *box_type )
{
    assert( box_type );
    if( !box_type )
        return 0;
    return !!(box_type->fourcc
            | box_type->user.fourcc
            | box_type->user.id[0] | box_type->user.id[1] | box_type->user.id[2]  | box_type->user.id[3]
            | box_type->user.id[4] | box_type->user.id[5] | box_type->user.id[6]  | box_type->user.id[7]
            | box_type->user.id[8] | box_type->user.id[9] | box_type->user.id[10] | box_type->user.id[11]);
}

lsmash_box_t *lsmash_get_box
(
    lsmash_box_t           *parent,
    const lsmash_box_path_t box_path[]
)
{
    lsmash_entry_t *entry = box_get_entry( parent, box_path );
    return (lsmash_box_t *)(entry ? entry->data : NULL);
}

lsmash_box_t *lsmash_create_box
(
    lsmash_box_type_t type,
    uint8_t          *data,
    uint32_t          size,
    uint64_t          precedence
)
{
    if( !lsmash_check_box_type_specified( &type ) )
        return NULL;
    isom_unknown_box_t *box = ALLOCATE_BOX( unknown );
    if( LSMASH_IS_NON_EXISTING_BOX( box ) )
        return NULL;
    if( size && data )
    {
        box->unknown_size  = size;
        box->unknown_field = lsmash_memdup( data, size );
        if( !box->unknown_field )
        {
            lsmash_free( box );
            return NULL;
        }
    }
    else
    {
        box->unknown_size  = 0;
        box->unknown_field = NULL;
        size = 0;
    }
    box->class      = &lsmash_box_class;
    box->root       = lsm_box_default_non_existing_root_abstract();
    box->file       = lsm_box_default_non_existing_file_abstract();
    box->parent     = (isom_box_t *)lsm_box_default_non_existing_unknown();
    box->destruct   = (isom_extension_destructor_t)lsm_box_remove_unknown_box;
    box->manager    = LSMASH_UNKNOWN_BOX;
    box->precedence = precedence;
    box->size       = ISOM_BASEBOX_COMMON_SIZE + size + (type.fourcc == ISOM_BOX_TYPE_UUID.fourcc ? 16 : 0);
    box->type       = type;
    lsm_write_set_box_writer( (isom_box_t *)box );
    return (lsmash_box_t *)box;
}

int lsmash_add_box
(
    lsmash_box_t *parent,
    lsmash_box_t *box
)
{
    if( LSMASH_IS_NON_EXISTING_BOX( parent ) )
        /* You cannot add any box without a box being its parent. */
        return LSMASH_ERR_FUNCTION_PARAM;
    if( LSMASH_IS_NON_EXISTING_BOX( box ) || box->size < ISOM_BASEBOX_COMMON_SIZE )
        return LSMASH_ERR_FUNCTION_PARAM;
    if( parent->root == (lsmash_root_t *)parent )
    {
        /* Only files can be added into any ROOT.
         * For backward compatibility, use the active file as the parent. */
        if( LSMASH_IS_EXISTING_BOX( parent->file ) )
            parent = (isom_box_t *)parent->file;
        else
            return LSMASH_ERR_FUNCTION_PARAM;
    }
    /* Add a box as a child box. */
    box->class  = &lsmash_box_class;
    box->root   = parent->root;
    box->file   = parent->file;
    box->parent = parent;
    return box_add_to_extension_list( parent, box );
}

int lsmash_add_box_ex
(
    lsmash_box_t  *parent,
    lsmash_box_t **p_box
)
{
    if( LSMASH_IS_NON_EXISTING_BOX( parent ) )
        /* You cannot add any box without a box being its parent. */
        return LSMASH_ERR_FUNCTION_PARAM;
    isom_unknown_box_t *box = (isom_unknown_box_t *)*p_box;
    if( LSMASH_IS_NON_EXISTING_BOX( box ) || box->size < ISOM_BASEBOX_COMMON_SIZE )
        return LSMASH_ERR_FUNCTION_PARAM;
    if( !(box->manager & LSMASH_UNKNOWN_BOX) )
        /* Simply add the box. */
        return lsmash_add_box( parent, *p_box );
    /* Check if the size of the box to be added is valid. */
    if( box->size != ISOM_BASEBOX_COMMON_SIZE + box->unknown_size + (box->type.fourcc == ISOM_BOX_TYPE_UUID.fourcc ? 16 : 0) )
        return LSMASH_ERR_FUNCTION_PARAM;
    if( LSMASH_IS_NON_EXISTING_BOX( parent->file ) || parent->file == (lsmash_file_t *)box )
        return LSMASH_ERR_FUNCTION_PARAM;
    if( parent->root == (lsmash_root_t *)parent )
        /* Only files can be added into any ROOT.
         * For backward compatibility, use the active file as the parent. */
        parent = (isom_box_t *)parent->file;
    /* Switch to the fake-file stream mode. */
    lsmash_file_t *file      = parent->file;
    lsmash_bs_t   *bs_backup = file->bs;
    lsmash_bs_t   *bs        = lsmash_bs_create();
    if( !bs )
        return LSMASH_ERR_MEMORY_ALLOC;
    uint8_t *buf = lsmash_malloc( box->size );
    if( !buf )
    {
        lsmash_bs_cleanup( bs );
        return LSMASH_ERR_MEMORY_ALLOC;
    }
    fake_file_stream_t fake_file =
        {
            .size = box->size,
            .data = buf,
            .pos  = 0
        };
    bs->stream = &fake_file;
    bs->read   = box_fake_file_read;
    bs->write  = NULL;
    bs->seek   = box_fake_file_seek;
    file->bs             = bs;
    file->fake_file_mode = 1;
    /* Make the byte string representing the given box. */
    LSMASH_SET_BE32( &buf[0], box->size );
    LSMASH_SET_BE32( &buf[4], box->type.fourcc );
    if( box->type.fourcc == ISOM_BOX_TYPE_UUID.fourcc )
    {
        LSMASH_SET_BE32( &buf[8], box->type.user.fourcc );
        memcpy( &buf[12], box->type.user.id, 12 );
    }
    memcpy( buf + (uintptr_t)(box->size - box->unknown_size), box->unknown_field, box->unknown_size );
    /* Add a box as a child box and try to expand into struct format. */
    lsmash_box_t dummy = { 0 };
    int ret = lsm_read_box( file, &dummy, parent, 0, 0 );
    lsmash_free( buf );
    lsmash_bs_cleanup( bs );
    file->bs             = bs_backup;   /* Switch back to the normal file stream mode. */
    file->fake_file_mode = 0;
    if( ret < 0 )
        return ret;
    /* Reorder the added box by 'precedence'. */
    *p_box = (lsmash_box_t *)parent->extensions.tail->data;
    (*p_box)->precedence = box->precedence;
    box_reorder_tail( parent );
    /* Do also its children by the same way. */
    lsmash_entry_list_t extensions = box->extensions;
    lsmash_list_init_simple( &box->extensions );    /* to avoid freeing the children */
    lsm_box_remove_by_itself( box );
    for( lsmash_entry_t *entry = extensions.head; entry; entry = entry->next )
    {
        if( !entry->data )
            continue;
        lsmash_box_t *child = (lsmash_box_t *)entry->data;
        if( lsmash_add_box_ex( *p_box, &child ) == 0 )
        {
            (*p_box)->size += child->size;
            /* Avoid freeing at the end of this function. */
            entry->data = NULL;
        }
    }
    lsm_box_remove_all_extension_boxes( &extensions );
    return 0;
}

void lsmash_destroy_box
(
    lsmash_box_t *box
)
{
    lsm_box_remove_by_itself( box );
}

void lsmash_destroy_children
(
    lsmash_box_t *box
)
{
    if( LSMASH_IS_EXISTING_BOX( box ) )
        lsm_box_remove_all_extension_boxes( &box->extensions );
}

int lsmash_get_box_precedence
(
    lsmash_box_t *box,
    uint64_t     *precedence
)
{
    if( !box || !precedence )
        return LSMASH_ERR_FUNCTION_PARAM;
    *precedence = box->precedence;
    return 0;
}

lsmash_box_t *lsmash_root_as_box
(
    lsmash_root_t *root
)
{
    return (lsmash_box_t *)root;
}

lsmash_box_t *lsmash_file_as_box
(
    lsmash_file_t *file
)
{
    return (lsmash_box_t *)file;
}

int lsmash_write_top_level_box
(
    lsmash_box_t *box
)
{
    if( !box || (isom_box_t *)box->file != box->parent )
        return LSMASH_ERR_FUNCTION_PARAM;
    int ret = lsm_write_box( box->file->bs, box );
    if( ret < 0 )
        return ret;
    box->file->size += box->size;
    return 0;
}

uint8_t *lsmash_export_box
(
    lsmash_box_t *box,
    uint32_t     *size
)
{
    if( !box || !size )
        return NULL;
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
        return NULL;
    if( lsm_write_box( bs, box ) < 0 )
    {
        lsmash_bs_cleanup( bs );
        return NULL;
    }
    *size = bs->buffer.store;
    uint8_t *data = bs->buffer.data;
    bs->buffer.data = NULL;
    lsmash_bs_cleanup( bs );
    return data;
}
