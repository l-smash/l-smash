/*****************************************************************************
 * file.c
 *****************************************************************************
 * Copyright (C) 2010-2014 L-SMASH project
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

#include "box.h"
#include "read.h"
#include "fragment.h"

int isom_check_compatibility
(
    lsmash_file_t *file
)
{
    if( !file )
        return -1;
    /* Clear flags for compatibility. */
    ptrdiff_t compat_offset = offsetof( lsmash_file_t, qt_compatible );
    memset( (int8_t *)file + compat_offset, 0, sizeof(lsmash_file_t) - compat_offset );
    file->min_isom_version = UINT8_MAX; /* undefined value */
    /* Get the brand container. */
    isom_ftyp_t *ftyp = file->ftyp ? file->ftyp : (isom_ftyp_t *)lsmash_get_entry_data( &file->styp_list, 1 );
    /* Check brand to decide mandatory boxes. */
    if( !ftyp )
    {
        /* No brand declaration means this file is a MP4 version 1 or QuickTime file format. */
        if( file->moov
         && file->moov->iods )
        {
            file->mp4_version1    = 1;
            file->isom_compatible = 1;
        }
        else
        {
            file->qt_compatible    = 1;
            file->undefined_64_ver = 1;
        }
        return 0;
    }
    for( uint32_t i = 0; i <= ftyp->brand_count; i++ )
    {
        uint32_t brand = (i == ftyp->brand_count ? ftyp->major_brand : ftyp->compatible_brands[i]);
        switch( brand )
        {
            case ISOM_BRAND_TYPE_QT :
                file->qt_compatible = 1;
                break;
            case ISOM_BRAND_TYPE_MP41 :
                file->mp4_version1 = 1;
                break;
            case ISOM_BRAND_TYPE_MP42 :
                file->mp4_version2 = 1;
                break;
            case ISOM_BRAND_TYPE_AVC1 :
            case ISOM_BRAND_TYPE_ISOM :
                file->max_isom_version = LSMASH_MAX( file->max_isom_version, 1 );
                file->min_isom_version = LSMASH_MIN( file->min_isom_version, 1 );
                break;
            case ISOM_BRAND_TYPE_ISO2 :
                file->max_isom_version = LSMASH_MAX( file->max_isom_version, 2 );
                file->min_isom_version = LSMASH_MIN( file->min_isom_version, 2 );
                break;
            case ISOM_BRAND_TYPE_ISO3 :
                file->max_isom_version = LSMASH_MAX( file->max_isom_version, 3 );
                file->min_isom_version = LSMASH_MIN( file->min_isom_version, 3 );
                break;
            case ISOM_BRAND_TYPE_ISO4 :
                file->max_isom_version = LSMASH_MAX( file->max_isom_version, 4 );
                file->min_isom_version = LSMASH_MIN( file->min_isom_version, 4 );
                break;
            case ISOM_BRAND_TYPE_ISO5 :
                file->max_isom_version = LSMASH_MAX( file->max_isom_version, 5 );
                file->min_isom_version = LSMASH_MIN( file->min_isom_version, 5 );
                break;
            case ISOM_BRAND_TYPE_ISO6 :
                file->max_isom_version = LSMASH_MAX( file->max_isom_version, 6 );
                file->min_isom_version = LSMASH_MIN( file->min_isom_version, 6 );
                break;
            case ISOM_BRAND_TYPE_ISO7 :
                file->max_isom_version = LSMASH_MAX( file->max_isom_version, 7 );
                file->min_isom_version = LSMASH_MIN( file->min_isom_version, 7 );
                break;
            case ISOM_BRAND_TYPE_M4A :
            case ISOM_BRAND_TYPE_M4B :
            case ISOM_BRAND_TYPE_M4P :
            case ISOM_BRAND_TYPE_M4V :
                file->itunes_movie = 1;
                break;
            case ISOM_BRAND_TYPE_3GP4 :
                file->max_3gpp_version = LSMASH_MAX( file->max_3gpp_version, 4 );
                break;
            case ISOM_BRAND_TYPE_3GP5 :
                file->max_3gpp_version = LSMASH_MAX( file->max_3gpp_version, 5 );
                break;
            case ISOM_BRAND_TYPE_3GE6 :
            case ISOM_BRAND_TYPE_3GG6 :
            case ISOM_BRAND_TYPE_3GP6 :
            case ISOM_BRAND_TYPE_3GR6 :
            case ISOM_BRAND_TYPE_3GS6 :
                file->max_3gpp_version = LSMASH_MAX( file->max_3gpp_version, 6 );
                break;
            case ISOM_BRAND_TYPE_3GP7 :
                file->max_3gpp_version = LSMASH_MAX( file->max_3gpp_version, 7 );
                break;
            case ISOM_BRAND_TYPE_3GP8 :
                file->max_3gpp_version = LSMASH_MAX( file->max_3gpp_version, 8 );
                break;
            case ISOM_BRAND_TYPE_3GE9 :
            case ISOM_BRAND_TYPE_3GF9 :
            case ISOM_BRAND_TYPE_3GG9 :
            case ISOM_BRAND_TYPE_3GH9 :
            case ISOM_BRAND_TYPE_3GM9 :
            case ISOM_BRAND_TYPE_3GP9 :
            case ISOM_BRAND_TYPE_3GR9 :
            case ISOM_BRAND_TYPE_3GS9 :
            case ISOM_BRAND_TYPE_3GT9 :
                file->max_3gpp_version = LSMASH_MAX( file->max_3gpp_version, 9 );
                break;
            default :
                break;
        }
        switch( brand )
        {
            case ISOM_BRAND_TYPE_AVC1 :
            case ISOM_BRAND_TYPE_ISO2 :
            case ISOM_BRAND_TYPE_ISO3 :
            case ISOM_BRAND_TYPE_ISO4 :
            case ISOM_BRAND_TYPE_ISO5 :
            case ISOM_BRAND_TYPE_ISO6 :
                file->avc_extensions = 1;
                break;
            case ISOM_BRAND_TYPE_3GP4 :
            case ISOM_BRAND_TYPE_3GP5 :
            case ISOM_BRAND_TYPE_3GP6 :
            case ISOM_BRAND_TYPE_3GP7 :
            case ISOM_BRAND_TYPE_3GP8 :
            case ISOM_BRAND_TYPE_3GP9 :
                file->forbid_tref = 1;
                break;
            case ISOM_BRAND_TYPE_3GH9 :
            case ISOM_BRAND_TYPE_3GM9 :
            case ISOM_BRAND_TYPE_DASH :
            case ISOM_BRAND_TYPE_DSMS :
            case ISOM_BRAND_TYPE_LMSG :
            case ISOM_BRAND_TYPE_MSDH :
            case ISOM_BRAND_TYPE_MSIX :
            case ISOM_BRAND_TYPE_SIMS :
                file->media_segment = 1;
                break;
            default :
                break;
        }
    }
    file->isom_compatible = !file->qt_compatible
                          || file->mp4_version1
                          || file->mp4_version2
                          || file->itunes_movie
                          || file->max_3gpp_version;
    file->undefined_64_ver = file->qt_compatible || file->itunes_movie;
    if( file->flags & LSMASH_FILE_MODE_WRITE )
    {
        /* Media Segment is incompatible with ISO Base Media File Format version 4 or former must be compatible with
         * version 6 or later since it requires default-base-is-moof and Track Fragment Base Media Decode Time Box. */
        if( file->media_segment && (file->min_isom_version < 5 || (file->max_isom_version && file->max_isom_version < 6)) )
            return -1;
        file->allow_moof_base = (file->max_isom_version >= 5 && file->min_isom_version >= 5)
                             || (file->max_isom_version == 0 && file->min_isom_version == UINT8_MAX && file->media_segment);
    }
    return 0;
}

int isom_check_mandatory_boxes
(
    lsmash_file_t *file
)
{
    if( !file
     || !file->moov
     || !file->moov->mvhd )
        return -1;
    /* A movie requires at least one track. */
    if( !file->moov->trak_list.head )
        return -1;
    for( lsmash_entry_t *entry = file->moov->trak_list.head; entry; entry = entry->next )
    {
        isom_trak_t *trak = (isom_trak_t *)entry->data;
        if( !trak
         || !trak->tkhd
         || !trak->mdia
         || !trak->mdia->mdhd
         || !trak->mdia->hdlr
         || !trak->mdia->minf
         || !trak->mdia->minf->dinf
         || !trak->mdia->minf->dinf->dref
         || !trak->mdia->minf->stbl
         || !trak->mdia->minf->stbl->stsd
         || !trak->mdia->minf->stbl->stsz
         || !trak->mdia->minf->stbl->stts
         || !trak->mdia->minf->stbl->stsc
         || !trak->mdia->minf->stbl->stco )
            return -1;
        if( file->qt_compatible && !trak->mdia->minf->hdlr )
            return -1;
        isom_stbl_t *stbl = trak->mdia->minf->stbl;
        if( !stbl->stsd->list.head )
            return -1;
        if( !file->fragment
         && (!stbl->stsd->list.head
          || !stbl->stts->list || !stbl->stts->list->head
          || !stbl->stsc->list || !stbl->stsc->list->head
          || !stbl->stco->list || !stbl->stco->list->head) )
            return -1;
    }
    if( !file->fragment )
        return 0;
    if( !file->moov->mvex )
        return -1;
    for( lsmash_entry_t *entry = file->moov->mvex->trex_list.head; entry; entry = entry->next )
        if( !entry->data )  /* trex */
            return -1;
    return 0;
}

int isom_rearrange_data
(
    lsmash_file_t        *file,
    lsmash_adhoc_remux_t *remux,
    uint8_t              *buf[2],
    size_t                read_num,
    size_t                size,
    uint64_t              read_pos,
    uint64_t              write_pos,
    uint64_t              file_size
)
{
    assert( remux );
    /* Copy-pastan */
    int buf_switch = 1;
    lsmash_bs_t *bs = file->bs;
    while( read_num == size )
    {
        if( lsmash_bs_write_seek( bs, read_pos, SEEK_SET ) < 0 )
            return -1;
        lsmash_bs_read_data( bs, buf[buf_switch], &read_num );
        read_pos    = bs->offset;
        buf_switch ^= 0x1;
        if( lsmash_bs_write_seek( bs, write_pos, SEEK_SET ) < 0 )
            return -1;
        if( lsmash_bs_write_data( bs, buf[buf_switch], size ) < 0 )
            return -1;
        write_pos = bs->offset;
        if( remux->func )
            remux->func( remux->param, write_pos, file_size ); // FIXME:
    }
    if( lsmash_bs_write_data( bs, buf[buf_switch ^ 0x1], read_num ) < 0 )
        return -1;
    if( remux->func )
        remux->func( remux->param, file_size, file_size ); // FIXME:
    return 0;
}

static int isom_set_brands
(
    lsmash_file_t     *file,
    lsmash_brand_type  major_brand,
    uint32_t           minor_version,
    lsmash_brand_type *brands,
    uint32_t           brand_count
)
{
    if( brand_count > 50 )
        return -1;      /* We support setting brands up to 50. */
    if( major_brand == 0 || brand_count == 0 )
    {
        /* Absence of File Type Box means this file is a QuickTime or MP4 version 1 format file. */
        isom_remove_box_by_itself( file->ftyp );
        /* Anyway we use QTFF as a default file format. */
        file->qt_compatible = 1;
        return 0;
    }
    isom_ftyp_t *ftyp;
    if( file->flags & LSMASH_FILE_MODE_INITIALIZATION )
    {
        /* Add File Type Box if absent yet. */
        if( !file->ftyp && !isom_add_ftyp( file ) )
            return -1;
        ftyp = file->ftyp;
    }
    else
    {
        /* Add Segment Type Box if absent yet. */
        ftyp = file->styp_list.head && file->styp_list.head->data
             ? (isom_styp_t *)file->styp_list.head->data
             : isom_add_styp( file );
        if( !ftyp )
            return -1;
    }
    /* Allocate an array of compatible brands.
     * ISO/IEC 14496-12 doesn't forbid the absence of brands in the compatible brand list.
     * For a reason of safety, however, we set at least one brand in the list. */
    size_t alloc_size = (brand_count ? brand_count : 1) * sizeof(uint32_t);
    lsmash_brand_type *compatible_brands;
    if( !file->compatible_brands )
        compatible_brands = lsmash_malloc( alloc_size );
    else
        compatible_brands = lsmash_realloc( file->compatible_brands, alloc_size );
    if( !compatible_brands )
        return -1;
    /* Set compatible brands. */
    if( brand_count )
        for( uint32_t i = 0; i < brand_count; i++ )
            compatible_brands[i] = brands[i];
    else
    {
        /* At least one compatible brand. */
        compatible_brands[0] = major_brand;
        brand_count = 1;
    }
    file->compatible_brands = compatible_brands;
    /* Duplicate an array of compatible brands. */
    lsmash_free( ftyp->compatible_brands );
    ftyp->compatible_brands = lsmash_memdup( compatible_brands, alloc_size );
    if( !ftyp->compatible_brands )
    {
        lsmash_freep( &file->compatible_brands );
        return -1;
    }
    ftyp->size          = ISOM_BASEBOX_COMMON_SIZE + 8 + brand_count * 4;
    ftyp->major_brand   = major_brand;
    ftyp->minor_version = minor_version;
    ftyp->brand_count   = brand_count;
    file->brand_count   = brand_count;
    return isom_check_compatibility( file );
}

/*******************************
    public interfaces
*******************************/

void lsmash_discard_boxes
(
    lsmash_root_t *root
)
{
    if( !root || !root->file )
        return;
    isom_remove_all_extension_boxes( &root->file->extensions );
}

int lsmash_open_file
(
    const char               *filename,
    int                       open_mode,
    lsmash_file_parameters_t *param
)
{
    if( !filename || !param )
        return -1;
    char mode[4] = { 0 };
    lsmash_file_mode file_mode = 0;
    if( open_mode == 0 )
    {
        memcpy( mode, "w+b", 4 );
        file_mode = LSMASH_FILE_MODE_WRITE
                  | LSMASH_FILE_MODE_BOX
                  | LSMASH_FILE_MODE_INITIALIZATION
                  | LSMASH_FILE_MODE_MEDIA;
    }
#ifdef LSMASH_DEMUXER_ENABLED
    else if( open_mode == 1 )
    {
        memcpy( mode, "rb", 3 );
        file_mode = LSMASH_FILE_MODE_READ;
    }
#endif
    if( file_mode == 0 )
        return -1;
    FILE *stream   = NULL;
    int   seekable = 1;
    if( !strcmp( filename, "-" ) )
    {
        if( file_mode & LSMASH_FILE_MODE_READ )
        {
            stream   = stdin;
            seekable = 0;
        }
        else if( file_mode & LSMASH_FILE_MODE_WRITE )
        {
            stream     = stdout;
            seekable   = 0;
            file_mode |= LSMASH_FILE_MODE_FRAGMENTED;
        }
    }
    else
        stream = lsmash_fopen( filename, mode );
    if( !stream )
        return -1;
    memset( param, 0, sizeof(lsmash_file_parameters_t) );
    param->mode                = file_mode;
    param->opaque              = (void *)stream;
    param->read                = lsmash_fread_wrapper;
    param->write               = lsmash_fwrite_wrapper;
    param->seek                = seekable ? lsmash_fseek_wrapper : NULL;
    param->major_brand         = 0;
    param->brands              = NULL;
    param->brand_count         = 0;
    param->minor_version       = 0;
    param->max_chunk_duration  = 0.5;
    param->max_async_tolerance = 2.0;
    param->max_chunk_size      = 4 * 1024 * 1024;
    param->max_read_size       = 4 * 1024 * 1024;
    return 0;
}

int lsmash_close_file
(
    lsmash_file_parameters_t *param
)
{
    if( !param )
        return -1;
    if( !param->opaque )
        return 0;
    int ret = fclose( (FILE *)param->opaque );
    param->opaque = NULL;
    return ret;
}

lsmash_file_t *lsmash_set_file
(
    lsmash_root_t            *root,
    lsmash_file_parameters_t *param
)
{
    if( !root || !param )
        return NULL;
    lsmash_file_t *file = isom_add_file( root );
    if( !file )
        return NULL;
    lsmash_bs_t *bs = lsmash_bs_create();
    if( !bs )
        goto fail;
    file->bs                  = bs;
    file->flags               = param->mode;
    file->bs->stream          = param->opaque;
    file->bs->read            = param->read;
    file->bs->write           = param->write;
    file->bs->seek            = param->seek;
    file->bs->unseekable      = (param->seek == NULL);
    file->bs->buffer.max_size = param->max_read_size;
    file->max_chunk_duration  = param->max_chunk_duration;
    file->max_async_tolerance = LSMASH_MAX( param->max_async_tolerance, 2 * param->max_chunk_duration );
    file->max_chunk_size      = param->max_chunk_size;
    if( (file->flags & LSMASH_FILE_MODE_WRITE)
     && (file->flags & LSMASH_FILE_MODE_BOX) )
    {
        /* Construction of Segment Index Box requires seekability at our current implementation.
         * If segment is not so large, data rearrangement can be avoided by buffering i.e. the
         * seekability is not essential, but at present we don't support buffering of all materials
         * within segment. */
        if( (file->flags & LSMASH_FILE_MODE_INDEX) && file->bs->unseekable )
            goto fail;
        /* Establish the fragment handler if required. */
        if( file->flags & LSMASH_FILE_MODE_FRAGMENTED )
        {
            file->fragment = lsmash_malloc_zero( sizeof(isom_fragment_manager_t) );
            if( !file->fragment )
                goto fail;
            file->fragment->pool = lsmash_create_entry_list();
            if( !file->fragment->pool )
                goto fail;
        }
        else if( file->bs->unseekable )
            /* For unseekable output operations, LSMASH_FILE_MODE_FRAGMENTED shall be set. */
            goto fail;
        /* Establish file types. */
        if( isom_set_brands( file, param->major_brand,
                                   param->minor_version,
                                   param->brands, param->brand_count ) < 0 )
            goto fail;
        /* Create the movie header if the initialization of the streams is required. */
        if( file->flags & LSMASH_FILE_MODE_INITIALIZATION )
        {
            if( !isom_add_moov( file )
             || !isom_add_mvhd( file->moov ) )
                goto fail;
            /* Default Movie Header Box. */
            isom_mvhd_t *mvhd = file->moov->mvhd;
            mvhd->rate          = 0x00010000;
            mvhd->volume        = 0x0100;
            mvhd->matrix[0]     = 0x00010000;
            mvhd->matrix[4]     = 0x00010000;
            mvhd->matrix[8]     = 0x40000000;
            mvhd->next_track_ID = 1;
            file->initializer = file;
        }
    }
    if( !root->file )
        root->file = file;
    return file;
fail:
    isom_remove_box_by_itself( file );
    return NULL;
}

int64_t lsmash_read_file
(
    lsmash_file_t            *file,
    lsmash_file_parameters_t *param
)
{
#ifdef LSMASH_DEMUXER_ENABLED
    if( !file || !file->bs )
        return -1LL;
    int64_t ret = -1;
    if( file->flags & (LSMASH_FILE_MODE_READ | LSMASH_FILE_MODE_DUMP) )
    {
        /* Get the file size if seekable when reading. */
        if( !file->bs->unseekable )
        {
            ret = lsmash_bs_read_seek( file->bs, 0, SEEK_END );
            if( ret < 0 )
                return ret;
            file->bs->written = ret;
            lsmash_bs_read_seek( file->bs, 0, SEEK_SET );
        }
        else
            ret = 0;
        /* Read whole boxes. */
        if( isom_read_file( file ) < 0 )
            return -1;
        if( param )
        {
            if( file->ftyp )
            {
                /* file types */
                isom_ftyp_t *ftyp = file->ftyp;
                param->major_brand   = ftyp->major_brand ? ftyp->major_brand : ISOM_BRAND_TYPE_QT;
                param->minor_version = ftyp->minor_version;
                param->brands        = file->compatible_brands;
                param->brand_count   = file->brand_count;
            }
            else if( file->styp_list.head && file->styp_list.head->data )
            {
                /* segment types */
                isom_styp_t *styp = (isom_styp_t *)file->styp_list.head->data;
                param->major_brand   = styp->major_brand ? styp->major_brand : ISOM_BRAND_TYPE_QT;
                param->minor_version = styp->minor_version;
                param->brands        = file->compatible_brands;
                param->brand_count   = file->brand_count;
            }
            else
            {
                param->major_brand   = file->mp4_version1 ? ISOM_BRAND_TYPE_MP41 : ISOM_BRAND_TYPE_QT;
                param->minor_version = 0;
                param->brands        = NULL;
                param->brand_count   = 0;
            }
        }
    }
    return ret;
#else
    return -1LL;
#endif
}

int lsmash_activate_file
(
    lsmash_root_t *root,
    lsmash_file_t *file
)
{
    if( !root || !file || file->root != root )
        return -1;
    root->file = file;
    return 0;
}

int lsmash_switch_media_segment
(
    lsmash_root_t        *root,
    lsmash_file_t        *successor,
    lsmash_adhoc_remux_t *remux
)
{
    if( !root || !remux )
        return -1;
    lsmash_file_t *predecessor = root->file;
    if( !predecessor || !successor
     || predecessor == successor
     || predecessor->root != successor->root
     || !predecessor->root || !successor->root
     || predecessor->root != root || successor->root != root
     ||  (successor->flags & LSMASH_FILE_MODE_INITIALIZATION)
     || !(successor->flags & LSMASH_FILE_MODE_MEDIA)
     || !(predecessor->flags & LSMASH_FILE_MODE_WRITE)      || !(successor->flags & LSMASH_FILE_MODE_WRITE)
     || !(predecessor->flags & LSMASH_FILE_MODE_BOX)        || !(successor->flags & LSMASH_FILE_MODE_BOX)
     || !(predecessor->flags & LSMASH_FILE_MODE_FRAGMENTED) || !(successor->flags & LSMASH_FILE_MODE_FRAGMENTED)
     || !(predecessor->flags & LSMASH_FILE_MODE_SEGMENT)    || !(successor->flags & LSMASH_FILE_MODE_SEGMENT)
     || (!(predecessor->flags & LSMASH_FILE_MODE_MEDIA) && !(predecessor->flags & LSMASH_FILE_MODE_INITIALIZATION))
     || isom_finish_final_fragment_movie( predecessor, remux ) < 0 )
        return -1;
    if( predecessor->flags & LSMASH_FILE_MODE_INITIALIZATION )
    {
        if( predecessor->initializer != predecessor )
            return -1;
        successor->initializer = predecessor;
    }
    else
        successor->initializer = predecessor->initializer;
    successor->fragment_count = predecessor->fragment_count;
    root->file = successor;
    return 0;
}
