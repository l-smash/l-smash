/*****************************************************************************
 * print.c:
 *****************************************************************************
 * Copyright (C) 2010 L-SMASH project
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

#ifdef LSMASH_DEMUXER_ENABLED

#include "internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h> /* for isom_iprintf */

#include "box.h"


typedef int (*isom_print_box_t)( lsmash_root_t *, isom_box_t *, int );

typedef struct
{
    int level;
    isom_box_t *box;
    isom_print_box_t func;
} isom_print_entry_t;

static void isom_iprintf( int indent, const char *format, ... )
{
    va_list args;
    va_start( args, format );
    for( int i = 0; i < indent; i++ )
        printf( "    " );
    vprintf( format, args );
    va_end( args );
}

static void isom_iprintf_duration( int indent, char *field_name, uint64_t duration, uint32_t timescale )
{
    if( !timescale )
    {
        isom_iprintf( indent, "duration = %"PRIu64"\n", duration );
        return;
    }
    int dur = duration / timescale;
    int hour = (dur / 3600) % 24;
    int min  = (dur /   60) % 60;
    int sec  =  dur         % 60;
    int ms   = ((double)duration / timescale - (hour * 3600 + min * 60 + sec)) * 1e3 + 0.5;
    static char str[32];
    sprintf( str, "%02d:%02d:%02d.%03d", hour, min, sec, ms );
    isom_iprintf( indent, "%s = %"PRIu64" (%s)\n", field_name, duration, str );
}

static char *isom_mp4time2utc( uint64_t mp4time )
{
    int year_offset = mp4time / 31536000;
    int leap_years = year_offset / 4 + ((mp4time / 86400) > 366);   /* 1904 itself is leap year */
    int day = (mp4time / 86400) - (year_offset * 365) - leap_years + 1;
    while( day < 1 )
    {
        --year_offset;
        leap_years = year_offset / 4 + ((mp4time / 86400) > 366);
        day = (mp4time / 86400) - (year_offset * 365) - leap_years + 1;
    }
    int year = 1904 + year_offset;
    int is_leap = (!(year % 4) && (year % 100)) || !(year % 400);
    static const int month_days[13] = { 29, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int month;
    for( month = 1; month <= 12; month++ )
    {
        int i = (month == 2 && is_leap) ? 0 : month;
        if( day <= month_days[i] )
            break;
        day -= month_days[i];
    }
    int hour = (mp4time / 3600) % 24;
    int min  = (mp4time /   60) % 60;
    int sec  =  mp4time         % 60;
    static char utc[64];
    sprintf( utc, "UTC %d/%02d/%02d, %02d:%02d:%02d\n", year, month, day, hour, min, sec );
    return utc;
}

static void isom_iprint_matrix( int indent, int32_t *matrix )
{
    isom_iprintf( indent, "| a, b, u |   | %f, %f, %f |\n", lsmash_fixed2double( matrix[0], 16 ),
                                                            lsmash_fixed2double( matrix[1], 16 ),
                                                            lsmash_fixed2double( matrix[2], 30 ) );
    isom_iprintf( indent, "| c, d, v | = | %f, %f, %f |\n", lsmash_fixed2double( matrix[3], 16 ),
                                                            lsmash_fixed2double( matrix[4], 16 ),
                                                            lsmash_fixed2double( matrix[5], 30 ) );
    isom_iprintf( indent, "| x, y, z |   | %f, %f, %f |\n", lsmash_fixed2double( matrix[6], 16 ),
                                                            lsmash_fixed2double( matrix[7], 16 ),
                                                            lsmash_fixed2double( matrix[8], 30 ) );
}

static void isom_iprint_rgb_color( int indent, uint16_t *color )
{
    isom_iprintf( indent, "{ R, G, B } = { %"PRIu16", %"PRIu16", %"PRIu16" }\n", color[0], color[1], color[2] );
}

static void isom_iprint_rgba_color( int indent, uint8_t *color )
{
    isom_iprintf( indent, "{ R, G, B, A } = { %"PRIu8", %"PRIu8", %"PRIu8", %"PRIu8" }\n", color[0], color[1], color[2], color[3] );
}

static char *isom_unpack_iso_language( uint16_t language )
{
    static char unpacked[4];
    unpacked[0] = ((language >> 10) & 0x1f) + 0x60;
    unpacked[1] = ((language >>  5) & 0x1f) + 0x60;
    unpacked[2] = ( language        & 0x1f) + 0x60;
    unpacked[3] = 0;
    return unpacked;
}

static void isom_iprint_sample_description_common_reserved( int indent, uint8_t *reserved )
{
    uint64_t temp = ((uint64_t)reserved[0] << 40)
                  | ((uint64_t)reserved[1] << 32)
                  | ((uint64_t)reserved[2] << 24)
                  | ((uint64_t)reserved[3] << 16)
                  | ((uint64_t)reserved[4] <<  8)
                  |  (uint64_t)reserved[5];
    isom_iprintf( indent, "reserved = 0x%012"PRIx64"\n", temp );
}

static void isom_iprint_sample_flags( int indent, char *field_name, isom_sample_flags_t *flags )
{
    uint32_t temp = (flags->reserved                  << 28)
                  | (flags->is_leading                << 26)
                  | (flags->sample_depends_on         << 24)
                  | (flags->sample_is_depended_on     << 22)
                  | (flags->sample_has_redundancy     << 20)
                  | (flags->sample_padding_value      << 17)
                  | (flags->sample_is_non_sync_sample << 16)
                  |  flags->sample_degradation_priority;
    isom_iprintf( indent++, "%s = 0x%08"PRIx32"\n", field_name, temp );
         if( flags->is_leading & ISOM_SAMPLE_IS_UNDECODABLE_LEADING       ) isom_iprintf( indent, "undecodable leading\n" );
    else if( flags->is_leading & ISOM_SAMPLE_IS_NOT_LEADING               ) isom_iprintf( indent, "non-leading\n" );
    else if( flags->is_leading & ISOM_SAMPLE_IS_DECODABLE_LEADING         ) isom_iprintf( indent, "decodable leading\n" );
         if( flags->sample_depends_on & ISOM_SAMPLE_IS_INDEPENDENT        ) isom_iprintf( indent, "independent\n" );
    else if( flags->sample_depends_on & ISOM_SAMPLE_IS_NOT_INDEPENDENT    ) isom_iprintf( indent, "dependent\n" );
         if( flags->sample_is_depended_on & ISOM_SAMPLE_IS_NOT_DISPOSABLE ) isom_iprintf( indent, "non-disposable\n" );
    else if( flags->sample_is_depended_on & ISOM_SAMPLE_IS_DISPOSABLE     ) isom_iprintf( indent, "disposable\n" );
         if( flags->sample_has_redundancy & ISOM_SAMPLE_HAS_REDUNDANCY    ) isom_iprintf( indent, "redundant\n" );
    else if( flags->sample_has_redundancy & ISOM_SAMPLE_HAS_NO_REDUNDANCY ) isom_iprintf( indent, "non-redundant\n" );
    if( flags->sample_padding_value )
        isom_iprintf( indent, "padding_bits = %"PRIu8"\n", flags->sample_padding_value );
    isom_iprintf( indent, flags->sample_is_non_sync_sample ? "non-sync sample\n" : "sync sample\n" );
    isom_iprintf( indent, "degradation_priority = %"PRIu16"\n", flags->sample_degradation_priority );
}

static inline int isom_print_simple( isom_box_t *box, int level, char *name )
{
    if( !box )
        return -1;
    int indent = level;
    isom_iprintf( indent++, "[%s: %s]\n", isom_4cc2str( box->type ), name );
    isom_iprintf( indent, "position = %"PRIu64"\n", box->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", box->size );
    return 0;
}

static void isom_print_basebox_common( int indent, isom_box_t *box, char *name )
{
    isom_print_simple( box, indent, name );
}

static void isom_print_fullbox_common( int indent, isom_box_t *box, char *name )
{
    isom_iprintf( indent++, "[%s: %s]\n", isom_4cc2str( box->type ), name );
    isom_iprintf( indent, "position = %"PRIu64"\n", box->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", box->size );
    isom_iprintf( indent, "version = %"PRIu8"\n", box->version );
    isom_iprintf( indent, "flags = 0x%06"PRIx32"\n", box->flags & 0x00ffffff );
}

static void isom_print_box_common( int indent, isom_box_t *box, char *name )
{
    isom_box_t *parent = box->parent;
    if( parent && parent->type == ISOM_BOX_TYPE_STSD )
    {
        isom_print_basebox_common( indent, box, name );
        return;
    }
    if( isom_is_fullbox( box ) )
        isom_print_fullbox_common( indent, box, name );
    else
        isom_print_basebox_common( indent, box, name );
}

static int isom_print_unknown( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    int indent = level;
    isom_iprintf( indent++, "[%s]\n", isom_4cc2str( box->type ) );
    isom_iprintf( indent, "position = %"PRIu64"\n", box->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", box->size );
    return 0;
}

static int isom_print_ftyp( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_ftyp_t *ftyp = (isom_ftyp_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "File Type Box" );
    isom_iprintf( indent, "major_brand = %s\n", isom_4cc2str( ftyp->major_brand ) );
    isom_iprintf( indent, "minor_version = %"PRIu32"\n", ftyp->minor_version );
    isom_iprintf( indent++, "compatible_brands\n" );
    for( uint32_t i = 0; i < ftyp->brand_count; i++ )
        isom_iprintf( indent, "brand[%"PRIu32"] = %s\n", i, isom_4cc2str( ftyp->compatible_brands[i] ) );
    return 0;
}

static int isom_print_moov( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Movie Box" );
}

static int isom_print_mvhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_mvhd_t *mvhd = (isom_mvhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Movie Header Box" );
    isom_iprintf( indent, "creation_time = %s", isom_mp4time2utc( mvhd->creation_time ) );
    isom_iprintf( indent, "modification_time = %s", isom_mp4time2utc( mvhd->modification_time ) );
    isom_iprintf( indent, "timescale = %"PRIu32"\n", mvhd->timescale );
    isom_iprintf_duration( indent, "duration", mvhd->duration, mvhd->timescale );
    isom_iprintf( indent, "rate = %f\n", lsmash_fixed2double( mvhd->rate, 16 ) );
    isom_iprintf( indent, "volume = %f\n", lsmash_fixed2double( mvhd->volume, 8 ) );
    isom_iprintf( indent, "reserved = 0x%04"PRIx16"\n", mvhd->reserved );
    if( root->qt_compatible )
    {
        isom_iprintf( indent, "preferredLong1 = 0x%08"PRIx32"\n", mvhd->preferredLong[0] );
        isom_iprintf( indent, "preferredLong2 = 0x%08"PRIx32"\n", mvhd->preferredLong[1] );
        isom_iprintf( indent, "transformation matrix\n" );
        isom_iprint_matrix( indent + 1, mvhd->matrix );
        isom_iprintf( indent, "previewTime = %"PRId32"\n", mvhd->previewTime );
        isom_iprintf( indent, "previewDuration = %"PRId32"\n", mvhd->previewDuration );
        isom_iprintf( indent, "posterTime = %"PRId32"\n", mvhd->posterTime );
        isom_iprintf( indent, "selectionTime = %"PRId32"\n", mvhd->selectionTime );
        isom_iprintf( indent, "selectionDuration = %"PRId32"\n", mvhd->selectionDuration );
        isom_iprintf( indent, "currentTime = %"PRId32"\n", mvhd->currentTime );
    }
    else
    {
        isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", mvhd->preferredLong[0] );
        isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", mvhd->preferredLong[1] );
        isom_iprintf( indent, "transformation matrix\n" );
        isom_iprint_matrix( indent + 1, mvhd->matrix );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", mvhd->previewTime );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", mvhd->previewDuration );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", mvhd->posterTime );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", mvhd->selectionTime );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", mvhd->selectionDuration );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", mvhd->currentTime );
    }
    isom_iprintf( indent, "next_track_ID = %"PRIu32"\n", mvhd->next_track_ID );
    return 0;
}

static int isom_print_iods( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Object Descriptor Box" );
}

static int isom_print_esds( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "ES Descriptor Box" );
}

static int isom_print_trak( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Track Box" );
}

static int isom_print_tkhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_tkhd_t *tkhd = (isom_tkhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Header Box" );
    ++indent;
    if( tkhd->flags & ISOM_TRACK_ENABLED )
        isom_iprintf( indent, "Track enabled\n" );
    else
        isom_iprintf( indent, "Track disabled\n" );
    if( tkhd->flags & ISOM_TRACK_IN_MOVIE )
        isom_iprintf( indent, "Track in movie\n" );
    if( tkhd->flags & ISOM_TRACK_IN_PREVIEW )
        isom_iprintf( indent, "Track in preview\n" );
    if( root->qt_compatible && (tkhd->flags & QT_TRACK_IN_POSTER) )
        isom_iprintf( indent, "Track in poster\n" );
    isom_iprintf( --indent, "creation_time = %s", isom_mp4time2utc( tkhd->creation_time ) );
    isom_iprintf( indent, "modification_time = %s", isom_mp4time2utc( tkhd->modification_time ) );
    isom_iprintf( indent, "track_ID = %"PRIu32"\n", tkhd->track_ID );
    isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", tkhd->reserved1 );
    if( root && root->moov && root->moov->mvhd )
        isom_iprintf_duration( indent, "duration", tkhd->duration, root->moov->mvhd->timescale );
    else
        isom_iprintf_duration( indent, "duration", tkhd->duration, 0 );
    isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", tkhd->reserved2[0] );
    isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", tkhd->reserved2[1] );
    isom_iprintf( indent, "layer = %"PRId16"\n", tkhd->layer );
    isom_iprintf( indent, "alternate_group = %"PRId16"\n", tkhd->alternate_group );
    isom_iprintf( indent, "volume = %f\n", lsmash_fixed2double( tkhd->volume, 8 ) );
    isom_iprintf( indent, "reserved = 0x%04"PRIx16"\n", tkhd->reserved3 );
    isom_iprintf( indent, "transformation matrix\n" );
    isom_iprint_matrix( indent + 1, tkhd->matrix );
    isom_iprintf( indent, "width = %f\n", lsmash_fixed2double( tkhd->width, 16 ) );
    isom_iprintf( indent, "height = %f\n", lsmash_fixed2double( tkhd->height, 16 ) );
    return 0;
}

static int isom_print_tapt( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Track Aperture Mode Dimensions Box" );
}

static int isom_print_clef( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_clef_t *clef = (isom_clef_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Clean Aperture Dimensions Box" );
    isom_iprintf( indent, "width = %f\n", lsmash_fixed2double( clef->width, 16 ) );
    isom_iprintf( indent, "height = %f\n", lsmash_fixed2double( clef->height, 16 ) );
    return 0;
}

static int isom_print_prof( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_prof_t *prof = (isom_prof_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Production Aperture Dimensions Box" );
    isom_iprintf( indent, "width = %f\n", lsmash_fixed2double( prof->width, 16 ) );
    isom_iprintf( indent, "height = %f\n", lsmash_fixed2double( prof->height, 16 ) );
    return 0;
}

static int isom_print_enof( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_enof_t *enof = (isom_enof_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Encoded Pixels Dimensions Box" );
    isom_iprintf( indent, "width = %f\n", lsmash_fixed2double( enof->width, 16 ) );
    isom_iprintf( indent, "height = %f\n", lsmash_fixed2double( enof->height, 16 ) );
    return 0;
}

static int isom_print_edts( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Edit Box" );
}

static int isom_print_elst( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_elst_t *elst = (isom_elst_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Edit List Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", elst->list->entry_count );
    for( lsmash_entry_t *entry = elst->list->head; entry; entry = entry->next )
    {
        isom_elst_entry_t *data = (isom_elst_entry_t *)entry->data;
        isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
        isom_iprintf( indent, "segment_duration = %"PRIu64"\n", data->segment_duration );
        isom_iprintf( indent, "media_time = %"PRId64"\n", data->media_time );
        isom_iprintf( indent--, "media_rate = %f\n", lsmash_fixed2double( data->media_rate, 16 ) );
    }
    return 0;
}

static int isom_print_tref( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Track Reference Box" );
}

static int isom_print_track_reference_type( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_tref_type_t *ref = (isom_tref_type_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Reference Type Box" );
    for( uint32_t i = 0; i < ref->ref_count; i++ )
        isom_iprintf( indent, "track_ID[%"PRIu32"] = %"PRIu32"\n", i, ref->track_ID[i] );
    return 0;


    return isom_print_simple( box, level, "Track Reference Type Box" );
}

static int isom_print_mdia( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Media Box" );
}

static int isom_print_mdhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_mdhd_t *mdhd = (isom_mdhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Media Header Box" );
    isom_iprintf( indent, "creation_time = %s", isom_mp4time2utc( mdhd->creation_time ) );
    isom_iprintf( indent, "modification_time = %s", isom_mp4time2utc( mdhd->modification_time ) );
    isom_iprintf( indent, "timescale = %"PRIu32"\n", mdhd->timescale );
    isom_iprintf_duration( indent, "duration", mdhd->duration, mdhd->timescale );
    if( mdhd->language >= 0x800 )
        isom_iprintf( indent, "language = %s\n", isom_unpack_iso_language( mdhd->language ) );
    else
        isom_iprintf( indent, "language = %"PRIu16"\n", mdhd->language );
    if( root->qt_compatible )
        isom_iprintf( indent, "quality = %"PRId16"\n", mdhd->quality );
    else
        isom_iprintf( indent, "pre_defined = 0x%04"PRIx16"\n", mdhd->quality );
    return 0;
}

static int isom_print_hdlr( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_hdlr_t *hdlr = (isom_hdlr_t *)box;
    int indent = level;
    char str[hdlr->componentName_length + 1];
    memcpy( str, hdlr->componentName, hdlr->componentName_length );
    str[hdlr->componentName_length] = 0;
    isom_print_box_common( indent++, box, "Handler Reference Box" );
    if( root->qt_compatible )
    {
        isom_iprintf( indent, "componentType = %s\n", isom_4cc2str( hdlr->componentType ) );
        isom_iprintf( indent, "componentSubtype = %s\n", isom_4cc2str( hdlr->componentSubtype ) );
        isom_iprintf( indent, "componentManufacturer = %s\n", isom_4cc2str( hdlr->componentManufacturer ) );
        isom_iprintf( indent, "componentFlags = 0x%08"PRIx32"\n", hdlr->componentFlags );
        isom_iprintf( indent, "componentFlagsMask = 0x%08"PRIx32"\n", hdlr->componentFlagsMask );
        if( hdlr->componentName_length )
            isom_iprintf( indent, "componentName = %s\n", &str[1] );
        else
            isom_iprintf( indent, "componentName = \n" );
    }
    else
    {
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", hdlr->componentType );
        isom_iprintf( indent, "handler_type = %s\n", isom_4cc2str( hdlr->componentSubtype ) );
        isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", hdlr->componentManufacturer );
        isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", hdlr->componentFlags );
        isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", hdlr->componentFlagsMask );
        isom_iprintf( indent, "name = %s\n", str );
    }
    return 0;
}

static int isom_print_minf( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Media Information Box" );
}

static int isom_print_vmhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_vmhd_t *vmhd = (isom_vmhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Video Media Header Box" );
    isom_iprintf( indent, "graphicsmode = %"PRIu16"\n", vmhd->graphicsmode );
    isom_iprintf( indent, "opcolor\n" );
    isom_iprint_rgb_color( indent + 1, vmhd->opcolor );
    return 0;
}

static int isom_print_smhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_smhd_t *smhd = (isom_smhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Sound Media Header Box" );
    isom_iprintf( indent, "balance = %f\n", lsmash_fixed2double( smhd->balance, 8 ) );
    isom_iprintf( indent, "reserved = 0x%04"PRIx16"\n", smhd->reserved );
    return 0;
}

static int isom_print_hmhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_hmhd_t *hmhd = (isom_hmhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Hint Media Header Box" );
    isom_iprintf( indent, "maxPDUsize = %"PRIu16"\n", hmhd->maxPDUsize );
    isom_iprintf( indent, "avgPDUsize = %"PRIu16"\n", hmhd->avgPDUsize );
    isom_iprintf( indent, "maxbitrate = %"PRIu32"\n", hmhd->maxbitrate );
    isom_iprintf( indent, "avgbitrate = %"PRIu32"\n", hmhd->avgbitrate );
    isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", hmhd->reserved );
    return 0;
}

static int isom_print_nmhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Null Media Header Box" );
}

static int isom_print_gmhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Generic Media Information Header Box" );
}

static int isom_print_gmin( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_gmin_t *gmin = (isom_gmin_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Generic Media Information Box" );
    isom_iprintf( indent, "graphicsmode = %"PRIu16"\n", gmin->graphicsmode );
    isom_iprintf( indent, "opcolor\n" );
    isom_iprint_rgb_color( indent + 1, gmin->opcolor );
    isom_iprintf( indent, "balance = %f\n", lsmash_fixed2double( gmin->balance, 8 ) );
    isom_iprintf( indent, "reserved = 0x%04"PRIx16"\n", gmin->reserved );
    return 0;
}

static int isom_print_text( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_text_t *text = (isom_text_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Text Media Information Box" );
    isom_iprintf( indent, "Unknown matrix\n" );
    isom_iprint_matrix( indent + 1, text->matrix );
    return 0;
}

static int isom_print_dinf( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Data Information Box" );
}

static int isom_print_dref( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_dref_t *dref = (isom_dref_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Data Reference Box" );
    isom_iprintf( indent, "entry_count = %"PRIu16"\n", dref->list->entry_count );
    return 0;
}

static int isom_print_url( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_dref_entry_t *url = (isom_dref_entry_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Data Entry Url Box" );
    if( url->flags & 0x000001 )
        isom_iprintf( indent, "location = in the same file\n" );
    else
        isom_iprintf( indent, "location = %s\n", url->location );
    return 0;
}

static int isom_print_stbl( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Sample Table Box" );
}

static int isom_print_stsd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_stsd_t *)box)->list )
        return -1;
    isom_stsd_t *stsd = (isom_stsd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Sample Description Box" );
    isom_iprintf( indent, "entry_count = %"PRIu16"\n", stsd->list->entry_count );
    return 0;
}

static int isom_print_visual_description( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_visual_entry_t *visual = (isom_visual_entry_t *)box;
    int indent = level;
    isom_iprintf( indent++, "[%s: Visual Description]\n", isom_4cc2str( visual->type ) );
    isom_iprintf( indent, "position = %"PRIu64"\n", visual->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", visual->size );
    isom_iprint_sample_description_common_reserved( indent, visual->reserved );
    isom_iprintf( indent, "data_reference_index = %"PRIu16"\n", visual->data_reference_index );
    if( root->qt_compatible )
    {
        isom_iprintf( indent, "version = %"PRId16"\n", visual->version );
        isom_iprintf( indent, "revision_level = %"PRId16"\n", visual->revision_level );
        isom_iprintf( indent, "vendor = %s\n", isom_4cc2str( visual->vendor ) );
        isom_iprintf( indent, "temporalQuality = %"PRIu32"\n", visual->temporalQuality );
        isom_iprintf( indent, "spatialQuality = %"PRIu32"\n", visual->spatialQuality );
        isom_iprintf( indent, "width = %"PRIu16"\n", visual->width );
        isom_iprintf( indent, "height = %"PRIu16"\n", visual->height );
        isom_iprintf( indent, "horizresolution = %f\n", lsmash_fixed2double( visual->horizresolution, 16 ) );
        isom_iprintf( indent, "vertresolution = %f\n", lsmash_fixed2double( visual->vertresolution, 16 ) );
        isom_iprintf( indent, "dataSize = %"PRIu32"\n", visual->dataSize );
        isom_iprintf( indent, "frame_count = %"PRIu16"\n", visual->frame_count );
        isom_iprintf( indent, "compressorname_length = %"PRIu8"\n", visual->compressorname[0] );
        isom_iprintf( indent, "compressorname = %s\n", visual->compressorname + 1 );
        isom_iprintf( indent, "depth = 0x%04"PRIx16, visual->depth );
        if( visual->depth == 32 )
            printf( " (colour with alpha)\n" );
        else if( visual->depth >= 33 && visual->depth <= 40 )
            printf( " (grayscale with no alpha)\n" );
        else
            printf( "\n" );
        isom_iprintf( indent, "color_table_ID = %"PRId16"\n", visual->color_table_ID );
    }
    else
    {
        isom_iprintf( indent, "pre_defined = 0x%04"PRIx16"\n", visual->version );
        isom_iprintf( indent, "reserved = 0x%04"PRIx16"\n", visual->revision_level );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", visual->vendor );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", visual->temporalQuality );
        isom_iprintf( indent, "pre_defined = 0x%08"PRIx32"\n", visual->spatialQuality );
        isom_iprintf( indent, "width = %"PRIu16"\n", visual->width );
        isom_iprintf( indent, "height = %"PRIu16"\n", visual->height );
        isom_iprintf( indent, "horizresolution = %f\n", lsmash_fixed2double( visual->horizresolution, 16 ) );
        isom_iprintf( indent, "vertresolution = %f\n", lsmash_fixed2double( visual->vertresolution, 16 ) );
        isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", visual->dataSize );
        isom_iprintf( indent, "frame_count = %"PRIu16"\n", visual->frame_count );
        isom_iprintf( indent, "compressorname_length = %"PRIu8"\n", visual->compressorname[0] );
        isom_iprintf( indent, "compressorname = %s\n", visual->compressorname + 1 );
        isom_iprintf( indent, "depth = 0x%04"PRIx16, visual->depth );
        if( visual->depth == 0x0018 )
            printf( " (colour with no alpha)\n" );
        else if( visual->depth == 0x0028 )
            printf( " (grayscale with no alpha)\n" );
        else if( visual->depth == 0x0020 )
            printf( " (gray or colour with alpha)\n" );
        else
            printf( "\n" );
        isom_iprintf( indent, "pre_defined = 0x%04"PRIx16"\n", visual->color_table_ID );
    }
    return 0;
}

static int isom_print_btrt( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_btrt_t *btrt = (isom_btrt_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Bit Rate Box" );
    isom_iprintf( indent, "bufferSizeDB = %"PRIu32"\n", btrt->bufferSizeDB );
    isom_iprintf( indent, "maxBitrate = %"PRIu32"\n", btrt->maxBitrate );
    isom_iprintf( indent, "avgBitrate = %"PRIu32"\n", btrt->avgBitrate );
    return 0;
}

static int isom_print_clap( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_clap_t *clap = (isom_clap_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Clean Aperture Box" );
    isom_iprintf( indent, "cleanApertureWidthN = %"PRIu32"\n", clap->cleanApertureWidthN );
    isom_iprintf( indent, "cleanApertureWidthD = %"PRIu32"\n", clap->cleanApertureWidthD );
    isom_iprintf( indent, "cleanApertureHeightN = %"PRIu32"\n", clap->cleanApertureHeightN );
    isom_iprintf( indent, "cleanApertureHeightD = %"PRIu32"\n", clap->cleanApertureHeightD );
    isom_iprintf( indent, "horizOffN = %"PRId32"\n", clap->horizOffN );
    isom_iprintf( indent, "horizOffD = %"PRIu32"\n", clap->horizOffD );
    isom_iprintf( indent, "vertOffN = %"PRId32"\n", clap->vertOffN );
    isom_iprintf( indent, "vertOffD = %"PRIu32"\n", clap->vertOffD );
    return 0;
}

static int isom_print_pasp( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_pasp_t *pasp = (isom_pasp_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Pixel Aspect Ratio Box" );
    isom_iprintf( indent, "hSpacing = %"PRIu32"\n", pasp->hSpacing );
    isom_iprintf( indent, "vSpacing = %"PRIu32"\n", pasp->vSpacing );
    return 0;
}

static int isom_print_colr( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_colr_t *colr = (isom_colr_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Color Parameter Box" );
    isom_iprintf( indent, "color_parameter_type = %s\n", isom_4cc2str( colr->color_parameter_type ) );
    if( colr->color_parameter_type == QT_COLOR_PARAMETER_TYPE_NCLC )
    {
        isom_iprintf( indent, "primaries_index = %"PRIu16"\n", colr->primaries_index );
        isom_iprintf( indent, "transfer_function_index = %"PRIu16"\n", colr->transfer_function_index );
        isom_iprintf( indent, "matrix_index = %"PRIu16"\n", colr->matrix_index );
    }
    return 0;
}

static int isom_print_stsl( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_stsl_t *stsl = (isom_stsl_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Sample Scale Box" );
    isom_iprintf( indent, "constraint_flag = %s\n", (stsl->constraint_flag & 0x01) ? "on" : "off" );
    isom_iprintf( indent, "scale_method = " );
    if( stsl->scale_method == ISOM_SCALING_METHOD_FILL )
        printf( "'fill'\n" );
    else if( stsl->scale_method == ISOM_SCALING_METHOD_HIDDEN )
        printf( "'hidden'\n" );
    else if( stsl->scale_method == ISOM_SCALING_METHOD_MEET )
        printf( "'meet'\n" );
    else if( stsl->scale_method == ISOM_SCALING_METHOD_SLICE_X )
        printf( "'slice' in the x-coodinate\n" );
    else if( stsl->scale_method == ISOM_SCALING_METHOD_SLICE_Y )
        printf( "'slice' in the y-coodinate\n" );
    isom_iprintf( indent, "display_center_x = %"PRIu16"\n", stsl->display_center_x );
    isom_iprintf( indent, "display_center_y = %"PRIu16"\n", stsl->display_center_y );
    return 0;
}

static int isom_print_avcC( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_avcC_t *avcC = (isom_avcC_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "AVC Configuration Box" );
    isom_iprintf( indent, "configurationVersion = %"PRIu8"\n", avcC->configurationVersion );
    isom_iprintf( indent, "AVCProfileIndication = %"PRIu8"\n", avcC->AVCProfileIndication );
    isom_iprintf( indent, "profile_compatibility = 0x%02"PRIu8"\n", avcC->profile_compatibility );
    isom_iprintf( indent, "AVCLevelIndication = %"PRIu8"\n", avcC->AVCLevelIndication );
    isom_iprintf( indent, "lengthSizeMinusOne = %"PRIu8"\n", avcC->lengthSizeMinusOne & 0x03 );
    isom_iprintf( indent, "numOfSequenceParameterSets = %"PRIu8"\n", avcC->numOfSequenceParameterSets & 0x1f );
    isom_iprintf( indent, "numOfPictureParameterSets = %"PRIu8"\n", avcC->numOfPictureParameterSets );
    if( ISOM_REQUIRES_AVCC_EXTENSION( avcC->AVCProfileIndication ) )
    {
        isom_iprintf( indent, "chroma_format = %"PRIu8"\n", avcC->chroma_format & 0x03 );
        isom_iprintf( indent, "bit_depth_luma_minus8 = %"PRIu8"\n", avcC->bit_depth_luma_minus8 & 0x7 );
        isom_iprintf( indent, "bit_depth_chroma_minus8 = %"PRIu8"\n", avcC->bit_depth_chroma_minus8 & 0x7 );
        isom_iprintf( indent, "numOfSequenceParameterSetExt = %"PRIu8"\n", avcC->numOfSequenceParameterSetExt );
    }
    return 0;
}

static int isom_print_audio_description( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_audio_entry_t *audio = (isom_audio_entry_t *)box;
    int indent = level;
    isom_iprintf( indent++, "[%s: Audio Description]\n", isom_4cc2str( audio->type ) );
    isom_iprintf( indent, "position = %"PRIu64"\n", audio->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", audio->size );
    isom_iprint_sample_description_common_reserved( indent, audio->reserved );
    isom_iprintf( indent, "data_reference_index = %"PRIu16"\n", audio->data_reference_index );
    if( root->qt_compatible )
    {
        isom_iprintf( indent, "version = %"PRId16"\n", audio->version );
        isom_iprintf( indent, "revision_level = %"PRId16"\n", audio->revision_level );
        isom_iprintf( indent, "vendor = %s\n", isom_4cc2str( audio->vendor ) );
        isom_iprintf( indent, "channelcount = %"PRIu16"\n", audio->channelcount );
        isom_iprintf( indent, "samplesize = %"PRIu16"\n", audio->samplesize );
        isom_iprintf( indent, "compression_ID = %"PRId16"\n", audio->compression_ID );
        isom_iprintf( indent, "packet_size = %"PRIu16"\n", audio->packet_size );
    }
    else
    {
        isom_iprintf( indent, "reserved = 0x%04"PRIx16"\n", audio->version );
        isom_iprintf( indent, "reserved = 0x%04"PRIx16"\n", audio->revision_level );
        isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", audio->vendor );
        isom_iprintf( indent, "channelcount = %"PRIu16"\n", audio->channelcount );
        isom_iprintf( indent, "samplesize = %"PRIu16"\n", audio->samplesize );
        isom_iprintf( indent, "pre_defined = %"PRId16"\n", audio->compression_ID );
        isom_iprintf( indent, "reserved = %"PRIu16"\n", audio->packet_size );
    }
    isom_iprintf( indent, "samplerate = %f\n", lsmash_fixed2double( audio->samplerate, 16 ) );
    if( audio->version == 1 )
    {
        isom_iprintf( indent, "samplesPerPacket = %"PRIu32"\n", audio->samplesPerPacket );
        isom_iprintf( indent, "bytesPerPacket = %"PRIu32"\n", audio->bytesPerPacket );
        isom_iprintf( indent, "bytesPerFrame = %"PRIu32"\n", audio->bytesPerFrame );
        isom_iprintf( indent, "bytesPerSample = %"PRIu32"\n", audio->bytesPerSample );
    }
    else if( audio->version == 2 )
    {
        isom_iprintf( indent, "sizeOfStructOnly = %"PRIu32"\n", audio->sizeOfStructOnly );
        isom_iprintf( indent, "audioSampleRate = %lf\n", lsmash_int2float64( audio->audioSampleRate ) );
        isom_iprintf( indent, "numAudioChannels = %"PRIu32"\n", audio->numAudioChannels );
        isom_iprintf( indent, "always7F000000 = 0x%08"PRIx32"\n", audio->always7F000000 );
        isom_iprintf( indent, "constBitsPerChannel = %"PRIu32"\n", audio->constBitsPerChannel );
        isom_iprintf( indent++, "formatSpecificFlags = 0x%08"PRIx32"\n", audio->formatSpecificFlags );
        if( isom_is_lpcm_audio( audio->type ) )
        {
            isom_iprintf( indent, "sample format: " );
            if( audio->formatSpecificFlags & QT_LPCM_FORMAT_FLAG_FLOAT )
                printf( "floating point\n" );
            else
            {
                printf( "integer\n" );
                isom_iprintf( indent, "signedness: " );
                printf( audio->formatSpecificFlags & QT_LPCM_FORMAT_FLAG_SIGNED_INTEGER ? "signed\n" : "unsigned\n" );
            }
            if( audio->constBytesPerAudioPacket != 1 )
            {
                isom_iprintf( indent, "endianness: " );
                printf( audio->formatSpecificFlags & QT_LPCM_FORMAT_FLAG_BIG_ENDIAN ? "big\n" : "little\n" );
            }
            isom_iprintf( indent, "packed: " );
            if( audio->formatSpecificFlags & QT_LPCM_FORMAT_FLAG_PACKED )
                printf( "yes\n" );
            else
            {
                printf( "no\n" );
                isom_iprintf( indent, "alignment: " );
                printf( audio->formatSpecificFlags & QT_LPCM_FORMAT_FLAG_ALIGNED_HIGH ? "high\n" : "low\n" );
            }
            if( audio->numAudioChannels > 1 )
            {
                isom_iprintf( indent, "interleved: " );
                printf( audio->formatSpecificFlags & QT_LPCM_FORMAT_FLAG_NON_INTERLEAVED ? "no\n" : "yes\n" );
            }
        }
        isom_iprintf( --indent, "constBytesPerAudioPacket = %"PRIu32"\n", audio->constBytesPerAudioPacket );
        isom_iprintf( indent, "constLPCMFramesPerAudioPacket = %"PRIu32"\n", audio->constLPCMFramesPerAudioPacket );
    }
    return 0;
}

static int isom_print_wave( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Sound Information Decompression Parameters Box" );
}

static int isom_print_frma( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_frma_t *frma = (isom_frma_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Format Box" );
    isom_iprintf( indent, "data_format = %s\n", isom_4cc2str( frma->data_format ) );
    return 0;
}

static int isom_print_enda( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_enda_t *enda = (isom_enda_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Audio Endian Box" );
    isom_iprintf( indent, "littleEndian = %s\n", enda->littleEndian ? "yes" : "no" );
    return 0;
}

static int isom_print_terminator( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_terminator_t *terminator = (isom_terminator_t *)box;
    int indent = level;
    isom_iprintf( indent++, "[0x00000000: Terminator Box]\n" );
    isom_iprintf( indent, "position = %"PRIu64"\n", terminator->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", terminator->size );
    return 0;
}

static int isom_print_chan( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_chan_t *chan = (isom_chan_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Channel Compositor Box" );
    isom_iprintf( indent, "channelLayoutTag = 0x%08"PRIx32"\n", chan->channelLayoutTag );
    isom_iprintf( indent, "channelBitmap = 0x%08"PRIx32"\n", chan->channelBitmap );
    isom_iprintf( indent, "numberChannelDescriptions = %"PRIu32"\n", chan->numberChannelDescriptions );
    if( chan->numberChannelDescriptions )
    {
        isom_channel_description_t *desc = chan->channelDescriptions;
        for( uint32_t i = 0; i < chan->numberChannelDescriptions; i++ )
        {
            isom_iprintf( indent++, "ChannelDescriptions[%"PRIu32"]\n", i );
            isom_iprintf( indent, "channelLabel = 0x%08"PRIx32"\n", desc->channelLabel );
            isom_iprintf( indent, "channelFlags = 0x%08"PRIx32"\n", desc->channelFlags );
            for( int j = 0; j < 3; j++ )
                isom_iprintf( indent, "coordinates[%d] = %f\n", j, lsmash_int2float32( desc->coordinates[j] ) );
            --indent;
        }
    }
    return 0;
}

static int isom_print_text_description( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_text_entry_t *text = (isom_text_entry_t *)box;
    int indent = level;
    isom_iprintf( indent++, "[text: QuickTime Text Description]\n" );
    isom_iprintf( indent, "position = %"PRIu64"\n", text->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", text->size );
    isom_iprint_sample_description_common_reserved( indent, text->reserved );
    isom_iprintf( indent, "data_reference_index = %"PRIu16"\n", text->data_reference_index );
    isom_iprintf( indent, "displayFlags = 0x%08"PRId32"\n", text->displayFlags );
    isom_iprintf( indent, "textJustification = %"PRId32"\n", text->textJustification );
    isom_iprintf( indent, "bgColor\n" );
    isom_iprint_rgb_color( indent + 1, text->bgColor );
    isom_iprintf( indent, "top = %"PRId16"\n", text->top );
    isom_iprintf( indent, "left = %"PRId16"\n", text->left );
    isom_iprintf( indent, "bottom = %"PRId16"\n", text->bottom );
    isom_iprintf( indent, "right = %"PRId16"\n", text->right );
    isom_iprintf( indent, "scrpStartChar = %"PRId32"\n", text->scrpStartChar );
    isom_iprintf( indent, "scrpHeight = %"PRId16"\n", text->scrpHeight );
    isom_iprintf( indent, "scrpAscent = %"PRId16"\n", text->scrpAscent );
    isom_iprintf( indent, "scrpFont = %"PRId16"\n", text->scrpFont );
    isom_iprintf( indent, "scrpFace = %"PRIu16"\n", text->scrpFace );
    isom_iprintf( indent, "scrpSize = %"PRId16"\n", text->scrpSize );
    isom_iprintf( indent, "scrpColor\n" );
    isom_iprint_rgb_color( indent + 1, text->scrpColor );
    if( text->font_name_length )
        isom_iprintf( indent, "font_name = %s\n", text->font_name );
    return 0;
}

static int isom_print_tx3g_description( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_tx3g_entry_t *tx3g = (isom_tx3g_entry_t *)box;
    int indent = level;
    isom_iprintf( indent++, "[tx3g: Timed Text Description]\n" );
    isom_iprintf( indent, "position = %"PRIu64"\n", tx3g->pos );
    isom_iprintf( indent, "size = %"PRIu64"\n", tx3g->size );
    isom_iprint_sample_description_common_reserved( indent, tx3g->reserved );
    isom_iprintf( indent, "data_reference_index = %"PRIu16"\n", tx3g->data_reference_index );
    isom_iprintf( indent, "displayFlags = 0x%08"PRId32"\n", tx3g->displayFlags );
    isom_iprintf( indent, "horizontal_justification = %"PRId8"\n", tx3g->horizontal_justification );
    isom_iprintf( indent, "vertical_justification = %"PRId8"\n", tx3g->vertical_justification );
    isom_iprintf( indent, "background_color_rgba\n" );
    isom_iprint_rgba_color( indent + 1, tx3g->background_color_rgba );
    isom_iprintf( indent, "top = %"PRId16"\n", tx3g->top );
    isom_iprintf( indent, "left = %"PRId16"\n", tx3g->left );
    isom_iprintf( indent, "bottom = %"PRId16"\n", tx3g->bottom );
    isom_iprintf( indent, "right = %"PRId16"\n", tx3g->right );
    isom_iprintf( indent, "startChar = %"PRIu16"\n", tx3g->startChar );
    isom_iprintf( indent, "endChar = %"PRIu16"\n", tx3g->endChar );
    isom_iprintf( indent, "font_ID = %"PRIu16"\n", tx3g->font_ID );
    isom_iprintf( indent, "face_style_flags = %"PRIu8"\n", tx3g->face_style_flags );
    isom_iprintf( indent, "font_size = %"PRIu8"\n", tx3g->font_size );
    isom_iprintf( indent, "text_color_rgba\n" );
    isom_iprint_rgba_color( indent + 1, tx3g->text_color_rgba );
    return 0;
}

static int isom_print_ftab( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_ftab_t *)box)->list )
        return -1;
    isom_ftab_t *ftab = (isom_ftab_t *)box;
    int indent = level;
    uint16_t i = 0;
    isom_print_box_common( indent++, box, "Font Table Box" );
    isom_iprintf( indent, "entry_count = %"PRIu16"\n", ftab->list->entry_count );
    for( lsmash_entry_t *entry = ftab->list->head; entry; entry = entry->next )
    {
        isom_font_record_t *data = (isom_font_record_t *)entry->data;
        isom_iprintf( indent++, "entry[%"PRIu16"]\n", i++ );
        isom_iprintf( indent, "font_ID = %"PRIu16"\n", data->font_ID );
        if( data->font_name_length )
            isom_iprintf( indent, "font_name = %s\n", data->font_name );
        --indent;
    }
    return 0;
}

static int isom_print_stts( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_stts_t *)box)->list )
        return -1;
    isom_stts_t *stts = (isom_stts_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Decoding Time to Sample Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", stts->list->entry_count );
    for( lsmash_entry_t *entry = stts->list->head; entry; entry = entry->next )
    {
        isom_stts_entry_t *data = (isom_stts_entry_t *)entry->data;
        isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
        isom_iprintf( indent, "sample_count = %"PRIu32"\n", data->sample_count );
        isom_iprintf( indent--, "sample_delta = %"PRIu32"\n", data->sample_delta );
    }
    return 0;
}

static int isom_print_ctts( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_ctts_t *)box)->list )
        return -1;
    isom_ctts_t *ctts = (isom_ctts_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Composition Time to Sample Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", ctts->list->entry_count );
    if( root->qt_compatible || ctts->version == 1 )
        for( lsmash_entry_t *entry = ctts->list->head; entry; entry = entry->next )
        {
            isom_ctts_entry_t *data = (isom_ctts_entry_t *)entry->data;
            isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
            isom_iprintf( indent, "sample_count = %"PRIu32"\n", data->sample_count );
            isom_iprintf( indent--, "sample_offset = %"PRId32"\n", (union {uint32_t ui; int32_t si;}){data->sample_offset}.si );
        }
    else
        for( lsmash_entry_t *entry = ctts->list->head; entry; entry = entry->next )
        {
            isom_ctts_entry_t *data = (isom_ctts_entry_t *)entry->data;
            isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
            isom_iprintf( indent, "sample_count = %"PRIu32"\n", data->sample_count );
            isom_iprintf( indent--, "sample_offset = %"PRIu32"\n", data->sample_offset );
        }
    return 0;
}

static int isom_print_cslg( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_cslg_t *cslg = (isom_cslg_t *)box;
    int indent = level;
    if( root->qt_compatible )
    {
        isom_print_box_common( indent++, box, "Composition Shift Least Greatest Box" );
        isom_iprintf( indent, "compositionOffsetToDTDDeltaShift = %"PRId32"\n", cslg->compositionToDTSShift );
        isom_iprintf( indent, "leastDecodeToDisplayDelta = %"PRId32"\n", cslg->leastDecodeToDisplayDelta );
        isom_iprintf( indent, "greatestDecodeToDisplayDelta = %"PRId32"\n", cslg->greatestDecodeToDisplayDelta );
        isom_iprintf( indent, "displayStartTime = %"PRId32"\n", cslg->compositionStartTime );
        isom_iprintf( indent, "displayEndTime = %"PRId32"\n", cslg->compositionEndTime );
    }
    else
    {
        isom_print_box_common( indent++, box, "Composition to Decode Box" );
        isom_iprintf( indent, "compositionToDTSShift = %"PRId32"\n", cslg->compositionToDTSShift );
        isom_iprintf( indent, "leastDecodeToDisplayDelta = %"PRId32"\n", cslg->leastDecodeToDisplayDelta );
        isom_iprintf( indent, "greatestDecodeToDisplayDelta = %"PRId32"\n", cslg->greatestDecodeToDisplayDelta );
        isom_iprintf( indent, "compositionStartTime = %"PRId32"\n", cslg->compositionStartTime );
        isom_iprintf( indent, "compositionEndTime = %"PRId32"\n", cslg->compositionEndTime );
    }
    return 0;
}

static int isom_print_stss( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_stss_t *)box)->list )
        return -1;
    isom_stss_t *stss = (isom_stss_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Sync Sample Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", stss->list->entry_count );
    for( lsmash_entry_t *entry = stss->list->head; entry; entry = entry->next )
        isom_iprintf( indent, "sample_number[%"PRIu32"] = %"PRIu32"\n", i++, ((isom_stss_entry_t *)entry->data)->sample_number );
    return 0;
}

static int isom_print_stps( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_stps_t *)box)->list )
        return -1;
    isom_stps_t *stps = (isom_stps_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Partial Sync Sample Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", stps->list->entry_count );
    for( lsmash_entry_t *entry = stps->list->head; entry; entry = entry->next )
        isom_iprintf( indent, "sample_number[%"PRIu32"] = %"PRIu32"\n", i++, ((isom_stps_entry_t *)entry->data)->sample_number );
    return 0;
}

static int isom_print_sdtp( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_sdtp_t *)box)->list )
        return -1;
    isom_sdtp_t *sdtp = (isom_sdtp_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Independent and Disposable Samples Box" );
    for( lsmash_entry_t *entry = sdtp->list->head; entry; entry = entry->next )
    {
        isom_sdtp_entry_t *data = (isom_sdtp_entry_t *)entry->data;
        isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
        if( data->is_leading || data->sample_depends_on || data->sample_is_depended_on || data->sample_has_redundancy )
        {
            if( root->avc_extensions )
            {
                if( data->is_leading & ISOM_SAMPLE_IS_UNDECODABLE_LEADING )
                    isom_iprintf( indent, "undecodable leading\n" );
                else if( data->is_leading & ISOM_SAMPLE_IS_NOT_LEADING )
                    isom_iprintf( indent, "non-leading\n" );
                else if( data->is_leading & ISOM_SAMPLE_IS_DECODABLE_LEADING )
                    isom_iprintf( indent, "decodable leading\n" );
            }
            else if( data->is_leading & QT_SAMPLE_EARLIER_PTS_ALLOWED )
                isom_iprintf( indent, "early display times allowed\n" );
            if( data->sample_depends_on & ISOM_SAMPLE_IS_INDEPENDENT )
                isom_iprintf( indent, "independent\n" );
            else if( data->sample_depends_on & ISOM_SAMPLE_IS_NOT_INDEPENDENT )
                isom_iprintf( indent, "dependent\n" );
            if( data->sample_is_depended_on & ISOM_SAMPLE_IS_NOT_DISPOSABLE )
                isom_iprintf( indent, "non-disposable\n" );
            else if( data->sample_is_depended_on & ISOM_SAMPLE_IS_DISPOSABLE )
                isom_iprintf( indent, "disposable\n" );
            if( data->sample_has_redundancy & ISOM_SAMPLE_HAS_REDUNDANCY )
                isom_iprintf( indent, "redundant\n" );
            else if( data->sample_has_redundancy & ISOM_SAMPLE_HAS_NO_REDUNDANCY )
                isom_iprintf( indent, "non-redundant\n" );
        }
        else
            isom_iprintf( indent, "no description\n" );
        --indent;
    }
    return 0;
}

static int isom_print_stsc( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_stsc_t *)box)->list )
        return -1;
    isom_stsc_t *stsc = (isom_stsc_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Sample To Chunk Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", stsc->list->entry_count );
    for( lsmash_entry_t *entry = stsc->list->head; entry; entry = entry->next )
    {
        isom_stsc_entry_t *data = (isom_stsc_entry_t *)entry->data;
        isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
        isom_iprintf( indent, "first_chunk = %"PRIu32"\n", data->first_chunk );
        isom_iprintf( indent, "samples_per_chunk = %"PRIu32"\n", data->samples_per_chunk );
        isom_iprintf( indent--, "sample_description_index = %"PRIu32"\n", data->sample_description_index );
    }
    return 0;
}

static int isom_print_stsz( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_stsz_t *stsz = (isom_stsz_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Sample Size Box" );
    if( !stsz->sample_size )
        isom_iprintf( indent, "sample_size = 0 (variable)\n" );
    else
        isom_iprintf( indent, "sample_size = %"PRIu32" (constant)\n", stsz->sample_size );
    isom_iprintf( indent, "sample_count = %"PRIu32"\n", stsz->sample_count );
    if( !stsz->sample_size && stsz->list )
        for( lsmash_entry_t *entry = stsz->list->head; entry; entry = entry->next )
        {
            isom_stsz_entry_t *data = (isom_stsz_entry_t *)entry->data;
            isom_iprintf( indent, "entry_size[%"PRIu32"] = %"PRIu32"\n", i++, data->entry_size );
        }
    return 0;
}

static int isom_print_stco( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_stco_t *)box)->list )
        return -1;
    isom_stco_t *stco = (isom_stco_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Chunk Offset Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", stco->list->entry_count );
    if( stco->type == ISOM_BOX_TYPE_STCO )
    {
        for( lsmash_entry_t *entry = stco->list->head; entry; entry = entry->next )
            isom_iprintf( indent, "chunk_offset[%"PRIu32"] = %"PRIu32"\n", i++, ((isom_stco_entry_t *)entry->data)->chunk_offset );
    }
    else
    {
        for( lsmash_entry_t *entry = stco->list->head; entry; entry = entry->next )
            isom_iprintf( indent, "chunk_offset[%"PRIu32"] = %"PRIu64"\n", i++, ((isom_co64_entry_t *)entry->data)->chunk_offset );
    }
    return 0;
}

static int isom_print_sgpd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_sgpd_entry_t *)box)->list )
        return -1;
    isom_sgpd_entry_t *sgpd = (isom_sgpd_entry_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Sample Group Description Box" );
    isom_iprintf( indent, "grouping_type = %s\n", isom_4cc2str( sgpd->grouping_type ) );
    if( sgpd->version == 1 )
    {
        isom_iprintf( indent, "default_length = %"PRIu32, sgpd->default_length );
        printf( " %s\n", sgpd->default_length ? "(constant)" : "(variable)" );
    }
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", sgpd->list->entry_count );
    switch( sgpd->grouping_type )
    {
        case ISOM_GROUP_TYPE_RAP :
            for( lsmash_entry_t *entry = sgpd->list->head; entry; entry = entry->next )
            {
                if( sgpd->version == 1 && !sgpd->default_length )
                    isom_iprintf( indent, "description_length[%"PRIu32"] = %"PRIu32"\n", i++, ((isom_rap_entry_t *)entry->data)->description_length );
                else
                {
                    isom_rap_entry_t *rap = (isom_rap_entry_t *)entry->data;
                    isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
                    isom_iprintf( indent, "num_leading_samples_known = %"PRIu8"\n", rap->num_leading_samples_known );
                    isom_iprintf( indent--, "num_leading_samples = %"PRIu8"\n", rap->num_leading_samples );
                }
            }
            break;
        case ISOM_GROUP_TYPE_ROLL :
            for( lsmash_entry_t *entry = sgpd->list->head; entry; entry = entry->next )
            {
                if( sgpd->version == 1 && !sgpd->default_length )
                    isom_iprintf( indent, "description_length[%"PRIu32"] = %"PRIu32"\n", i++, ((isom_roll_entry_t *)entry->data)->description_length );
                else
                    isom_iprintf( indent, "roll_distance[%"PRIu32"] = %"PRId16"\n", i++, ((isom_roll_entry_t *)entry->data)->roll_distance );
            }
            break;
        default :
            break;
    }
    return 0;
}

static int isom_print_sbgp( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_sbgp_entry_t *)box)->list )
        return -1;
    isom_sbgp_entry_t *sbgp = (isom_sbgp_entry_t *)box;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Sample to Group Box" );
    isom_iprintf( indent, "grouping_type = %s\n", isom_4cc2str( sbgp->grouping_type ) );
    if( sbgp->version == 1 )
        isom_iprintf( indent, "grouping_type_parameter = %s\n", isom_4cc2str( sbgp->grouping_type_parameter ) );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", sbgp->list->entry_count );
    for( lsmash_entry_t *entry = sbgp->list->head; entry; entry = entry->next )
    {
        isom_group_assignment_entry_t *data = (isom_group_assignment_entry_t *)entry->data;
        isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
        isom_iprintf( indent, "sample_count = %"PRIu32"\n", data->sample_count );
        isom_iprintf( indent--, "group_description_index = %"PRIu32, data->group_description_index );
        if( !data->group_description_index )
            printf( " (not in this grouping type)\n" );
        else
            printf( "\n" );
    }
    return 0;
}

static int isom_print_udta( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "User Data Box" );
}

static int isom_print_chpl( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_chpl_t *chpl = (isom_chpl_t *)box;
    uint32_t timescale;
    if( !chpl->version )
    {
        if( !root->moov && !root->moov->mvhd )
            return -1;
        timescale = root->moov->mvhd->timescale;
    }
    else
        timescale = 10000000;
    int indent = level;
    uint32_t i = 0;
    isom_print_box_common( indent++, box, "Chapter List Box" );
    if( chpl->version == 1 )
    {
        isom_iprintf( indent, "unknown = 0x%02"PRIx8"\n", chpl->unknown );
        isom_iprintf( indent, "entry_count = %"PRIu32"\n", chpl->list->entry_count );
    }
    else
        isom_iprintf( indent, "entry_count = %"PRIu8"\n", (uint8_t)chpl->list->entry_count );
    for( lsmash_entry_t *entry = chpl->list->head; entry; entry = entry->next )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        int64_t start_time = data->start_time / timescale;
        int hh =  start_time / 3600;
        int mm = (start_time /   60) % 60;
        int ss =  start_time         % 60;
        int ms = ((data->start_time / (double)timescale) - hh * 3600 - mm * 60 - ss) * 1e3 + 0.5;
        char str[256];
        memset( str, 0, sizeof(str) );
        memcpy( str, data->chapter_name, data->chapter_name_length );
        isom_iprintf( indent++, "chapter[%"PRIu32"]\n", i++ );
        isom_iprintf( indent, "start_time = %02d:%02d:%02d.%03d\n", hh, mm, ss, ms );
        isom_iprintf( indent--, "chapter_name = %s\n", str );
    }
    return 0;
}

static int isom_print_meta( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    int indent = level;
    if( !(box->manager & LSMASH_QTFF_BASE) )
    {
        isom_print_basebox_common( indent++, box, "Meta Box" );
        isom_iprintf( indent, "version = %"PRIu8"\n", box->version );
        isom_iprintf( indent, "flags = 0x%06"PRIx32"\n", box->flags & 0x00ffffff );
    }
    else
        isom_print_basebox_common( indent, box, "Metadata Box" );
    return 0;
}

static int isom_print_keys( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box || !((isom_keys_t *)box)->list )
        return -1;
    isom_keys_t *keys = (isom_keys_t *)box;
    int indent = level;
    uint32_t i = 1;
    isom_print_box_common( indent++, box, "Metadata Item Keys Box" );
    isom_iprintf( indent, "entry_count = %"PRIu32"\n", keys->list->entry_count );
    for( lsmash_entry_t *entry = keys->list->head; entry; entry = entry->next )
    {
        isom_keys_entry_t *data = (isom_keys_entry_t *)entry->data;
        isom_iprintf( indent++, "[key %"PRIu32"]\n", i++ );
        isom_iprintf( indent, "key_size = %"PRIu32"\n", data->key_size );
        isom_iprintf( indent, "key_namespace = %s\n", isom_4cc2str( data->key_namespace ) );
        uint32_t value_length = data->key_size - 8;
        char str[value_length + 1];
        memcpy( str, data->key_value, value_length );
        str[value_length] = 0;
        isom_iprintf( indent--, "key_value = %s\n", str );
    }
    return 0;
}

static int isom_print_ilst( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Metadata Item List Box" );
}

static int isom_print_metaitem( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_metaitem_t *metaitem = (isom_metaitem_t *)box;
    if( box->parent && box->parent->parent && (box->parent->parent->manager & LSMASH_QTFF_BASE) )
    {
        int indent = level;
        isom_iprintf( indent++, "[key_index %"PRIu32": Metadata Item Box]\n", box->type );
        isom_iprintf( indent, "position = %"PRIu64"\n", box->pos );
        isom_iprintf( indent, "size = %"PRIu64"\n", box->size );
        return 0;
    }
    char *name;
    static const struct
    {
        uint32_t type;
        char    *name;
    } metaitem_table[] =
        {
            { LSMASH_4CC( 0xA9, 'a', 'l', 'b' ), "Album Name" },
            { LSMASH_4CC( 0xA9, 'a', 'r', 't' ), "Artist" },
            { LSMASH_4CC( 0xA9, 'A', 'R', 'T' ), "Artist" },
            { LSMASH_4CC( 0xA9, 'c', 'm', 't' ), "User Comment" },
            { LSMASH_4CC( 0xA9, 'd', 'a', 'y' ), "Release Date" },
            { LSMASH_4CC( 0xA9, 'e', 'n', 'c' ), "Encoded By" },
            { LSMASH_4CC( 0xA9, 'g', 'e', 'n' ), "User Genre" },
            { LSMASH_4CC( 0xA9, 'g', 'r', 'p' ), "Grouping" },
            { LSMASH_4CC( 0xA9, 'l', 'y', 'r' ), "Lyrics" },
            { LSMASH_4CC( 0xA9, 'n', 'a', 'm' ), "Title" },
            { LSMASH_4CC( 0xA9, 't', 'o', 'o' ), "Encoding Tool" },
            { LSMASH_4CC( 0xA9, 'w', 'r', 't' ), "Composer" },
            { LSMASH_4CC( 'a', 'A', 'R', 'T' ),  "Album Artist" },
            { LSMASH_4CC( 'c', 'a', 't', 'g' ),  "Category" },
            { LSMASH_4CC( 'c', 'o', 'v', 'r' ),  "Cover Art" },
            { LSMASH_4CC( 'c', 'p', 'i', 'l' ),  "Disc Compilation" },
            { LSMASH_4CC( 'c', 'p', 'r', 't' ),  "Copyright" },
            { LSMASH_4CC( 'd', 'e', 's', 'c' ),  "Description" },
            { LSMASH_4CC( 'd', 'i', 's', 'k' ),  "Disc Number" },
            { LSMASH_4CC( 'e', 'g', 'i', 'd' ),  "Episode Global Unique ID" },
            { LSMASH_4CC( 'g', 'n', 'r', 'e' ),  "Pre-defined Genre" },
            { LSMASH_4CC( 'g', 'r', 'u', 'p' ),  "Grouping" },
            { LSMASH_4CC( 'h', 'd', 'v', 'd' ),  "High Definition Video" },
            { LSMASH_4CC( 'k', 'e', 'y', 'w' ),  "Keyword" },
            { LSMASH_4CC( 'l', 'd', 'e', 's' ),  "Long Description" },
            { LSMASH_4CC( 'p', 'c', 's', 't' ),  "Podcast" },
            { LSMASH_4CC( 'p', 'g', 'a', 'p' ),  "Gapless Playback" },
            { LSMASH_4CC( 'p', 'u', 'r', 'd' ),  "Purcase Date" },
            { LSMASH_4CC( 'p', 'u', 'r', 'l' ),  "Podcast URL" },
            { LSMASH_4CC( 'r', 't', 'n', 'g' ),  "Content Rating" },
            { LSMASH_4CC( 's', 't', 'i', 'k' ),  "Media Type" },
            { LSMASH_4CC( 't', 'm', 'p', 'o' ),  "Beats Per Minute" },
            { LSMASH_4CC( 't', 'r', 'k', 'n' ),  "Track Number" },
            { LSMASH_4CC( 't', 'v', 'e', 'n' ),  "TV Episode Number" },
            { LSMASH_4CC( 't', 'v', 'e', 's' ),  "TV Episode" },
            { LSMASH_4CC( 't', 'v', 'n', 'n' ),  "TV Network" },
            { LSMASH_4CC( 't', 'v', 's', 'h' ),  "TV Show Name" },
            { LSMASH_4CC( 't', 'v', 's', 'n' ),  "TV Season" },
            { LSMASH_4CC( 'a', 'k', 'I', 'D' ),  "iTunes Account Used for Purchase" },
            { LSMASH_4CC( 'a', 'p', 'I', 'D' ),  "iTunes Acount Type" },
            { LSMASH_4CC( 'a', 't', 'I', 'D' ),  "iTunes Artist ID" },
            { LSMASH_4CC( 'c', 'm', 'I', 'D' ),  "iTunes Composer ID" },
            { LSMASH_4CC( 'c', 'n', 'I', 'D' ),  "iTunes Content ID" },
            { LSMASH_4CC( 'g', 'e', 'I', 'D' ),  "iTunes TV Genre ID" },
            { LSMASH_4CC( 'p', 'l', 'I', 'D' ),  "iTunes Playlist ID" },
            { LSMASH_4CC( 's', 'f', 'I', 'D' ),  "iTunes Country Code" },
            { LSMASH_4CC( '-', '-', '-', '-' ),  "Custom Metadata Item" },
            { 0 }
        };
    int i;
    for( i = 0; metaitem_table[i].type; i++ )
        if( metaitem->type == metaitem_table[i].type )
        {
            name = metaitem_table[i].name;
            break;
        }
    if( !metaitem_table[i].type )
        name = "Unknown";
    uint32_t name_length = strlen( name );
    uint32_t display_name_length = name_length + 20;
    char display_name[display_name_length + 1];
    memcpy( display_name, "Metadata Item Box (", 19 );
    memcpy( display_name + 19, name, name_length );
    display_name[display_name_length - 1] = ')';
    display_name[display_name_length] = 0;
    return isom_print_simple( box, level, display_name );
}

static int isom_print_name( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_name_t *name = (isom_name_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Name Box" );
    char str[name->name_length + 1];
    memcpy( str, name->name, name->name_length );
    str[name->name_length] = 0;
    isom_iprintf( indent, "name = %s\n", str );
    return 0;
}

static int isom_print_mean( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_mean_t *mean = (isom_mean_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Mean Box" );
    char str[mean->meaning_string_length + 1];
    memcpy( str, mean->meaning_string, mean->meaning_string_length );
    str[mean->meaning_string_length] = 0;
    isom_iprintf( indent, "meaning_string = %s\n", str );
    return 0;
}

static int isom_print_data( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_data_t *data = (isom_data_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Data Box" );
    if( box->parent && box->parent->parent && box->parent->parent->parent
     && (box->parent->parent->parent->manager & LSMASH_QTFF_BASE) )
    {
        uint32_t type_set_indicator = data->reserved >> 8;
        uint32_t well_known_type = ((data->reserved << 16) | (data->type_set_identifier << 8) | data->type_code) & 0xffffff;
        char *well_known_type_name;
        static const struct
        {
            uint32_t type;
            char    *name;
        } well_known_type_table[] =
            {
                { 0,  "reserved" },
                { 1,  "UTF-8" },
                { 2,  "UTF-16 BE" },
                { 3,  "S/JIS" },
                { 4,  "UTF-8 sort" },
                { 5,  "UTF-16 sort" },
                { 13,  "JPEG in a JFIF wrapper" },
                { 14,  "PNG in a PNG wrapper" },
                { 21,  "BE Signed Integer" },
                { 22,  "BE Unsigned Integer" },
                { 23,  "BE Float32" },
                { 24,  "BE Float64" },
                { 27,  "BMP (Windows bitmap format graphics)" },
                { 28,  "QuickTime Metadata box" },
                { UINT32_MAX }
            };
        int table_index;
        for( table_index = 0; well_known_type_table[table_index].type != UINT32_MAX; table_index++ )
            if( well_known_type == well_known_type_table[table_index].type )
            {
                well_known_type_name = well_known_type_table[table_index].name;
                break;
            }
        if( well_known_type_table[table_index].type == UINT32_MAX )
            well_known_type_name = "Unknown";
        isom_iprintf( indent, "type_set_indicator = %"PRIu8"\n", type_set_indicator );
        isom_iprintf( indent, "well_known_type = %"PRIu32" (%s)\n", well_known_type, well_known_type_name );
        isom_iprintf( indent, "locale_indicator = %"PRIu32"\n", data->the_locale );
        if( well_known_type == 1 )
        {
            /* UTF-8 without any count or null terminator */
            char str[data->value_length + 1];
            memcpy( str, data->value, data->value_length );
            str[data->value_length] = 0;
            isom_iprintf( indent, "value = %s\n", str );
        }
        else if( well_known_type == 13 || well_known_type == 14 || well_known_type == 27 )
            isom_iprintf( indent, "value = (binary data)\n" );
        else if( well_known_type == 21 && data->value_length && data->value_length <= 4 )
        {
            /* a big-endian signed integer in 1,2,3 or 4 bytes */
            uint32_t integer   = data->value[0];
            uint32_t max_value = 0xff;
            for( uint32_t i = 1; i < data->value_length; i++ )
            {
                integer   = (integer   << 8) | data->value[i];
                max_value = (max_value << 8) | 0xff;
            }
            isom_iprintf( indent, "value = %"PRId32"\n", (int32_t)(integer | (integer > (max_value >> 1) ? ~max_value : 0)) );
        }
        else if( well_known_type == 22 && data->value_length && data->value_length <= 4 )
        {
            /* a big-endian unsigned integer in 1,2,3 or 4 bytes */
            uint32_t integer = data->value[0];
            for( uint32_t i = 1; i < data->value_length; i++ )
                integer = (integer << 8) | data->value[i];
            isom_iprintf( indent, "value = %"PRIu32"\n", integer );
        }
        else if( well_known_type == 23 && data->value_length == 4 )
        {
            /* a big-endian 32-bit floating point value (IEEE754) */
            uint32_t float32 = (data->value[0] << 24) | (data->value[1] << 16) | (data->value[2] << 8) | data->value[3];
            isom_iprintf( indent, "value = %f\n", lsmash_int2float32( float32 ) );
        }
        else if( well_known_type == 24 && data->value_length == 8 )
        {
            /* a big-endian 64-bit floating point value (IEEE754) */
            uint64_t float64 = ((uint64_t)data->value[0] << 56) | ((uint64_t)data->value[1] << 48)
                             | ((uint64_t)data->value[2] << 40) | ((uint64_t)data->value[3] << 32)
                             | ((uint64_t)data->value[4] << 24) | ((uint64_t)data->value[5] << 16)
                             | ((uint64_t)data->value[6] <<  8) |  (uint64_t)data->value[7];
            isom_iprintf( indent, "value = %lf\n", lsmash_int2float64( float64 ) );
        }
        else
        {
            isom_iprintf( indent, "value = " );
            if( data->value_length )
            {
                printf( "0x" );
                for( uint32_t i = 0; i < data->value_length; i++ )
                    printf( "%02"PRIx8, data->value[i] );
            }
            printf( "\n" );
        }
    }
    else
    {
        isom_iprintf( indent, "reserved = %"PRIu16"\n", data->reserved );
        isom_iprintf( indent, "type_set_identifier = %"PRIu8"%s\n",
                      data->type_set_identifier,
                      data->type_set_identifier ? "" : " (basic type set)" );
        isom_iprintf( indent, "type_code = %"PRIu8"\n", data->type_code );
        isom_iprintf( indent, "the_locale = %"PRIu32"\n", data->the_locale );
        if( data->type_code == 21 )
        {
            /* integer type */
            isom_iprintf( indent, "value = " );
            if( data->value_length )
            {
                printf( "0x" );
                for( uint32_t i = 0; i < data->value_length; i++ )
                    printf( "%02"PRIx8, data->value[i] );
            }
            printf( "\n" );
        }
        else
        {
            char str[data->value_length + 1];
            memcpy( str, data->value, data->value_length );
            str[data->value_length] = 0;
            isom_iprintf( indent, "value = %s\n", str );
        }
    }
    return 0;
}

static int isom_print_mvex( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Movie Extends Box" );
}

static int isom_print_mehd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_mehd_t *mehd = (isom_mehd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Movie Extends Header Box" );
    if( root && root->moov && root->moov->mvhd )
        isom_iprintf_duration( indent, "fragment_duration", mehd->fragment_duration, root->moov->mvhd->timescale );
    else
        isom_iprintf_duration( indent, "fragment_duration", mehd->fragment_duration, 0 );
    return 0;
}

static int isom_print_trex( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_trex_entry_t *trex = (isom_trex_entry_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Extends Box" );
    isom_iprintf( indent, "track_ID = %"PRIu32"\n", trex->track_ID );
    isom_iprintf( indent, "default_sample_description_index = %"PRIu32"\n", trex->default_sample_description_index );
    isom_iprintf( indent, "default_sample_duration = %"PRIu32"\n", trex->default_sample_duration );
    isom_iprintf( indent, "default_sample_size = %"PRIu32"\n", trex->default_sample_size );
    isom_iprint_sample_flags( indent, "default_sample_flags", &trex->default_sample_flags );
    return 0;
}

static int isom_print_moof( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Movie Fragment Box" );
}

static int isom_print_mfhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_mfhd_t *mfhd = (isom_mfhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Movie Fragment Header Box" );
    isom_iprintf( indent, "sequence_number = %"PRIu32"\n", mfhd->sequence_number );
    return 0;
}

static int isom_print_traf( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Track Fragment Box" );
}

static int isom_print_tfhd( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_tfhd_t *tfhd = (isom_tfhd_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Fragment Header Box" );
    ++indent;
    if( tfhd->flags & ISOM_TF_FLAGS_BASE_DATA_OFFSET_PRESENT         ) isom_iprintf( indent, "base-data-offset-present\n" );
    if( tfhd->flags & ISOM_TF_FLAGS_SAMPLE_DESCRIPTION_INDEX_PRESENT ) isom_iprintf( indent, "sample-description-index-present\n" );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT  ) isom_iprintf( indent, "default-sample-duration-present\n" );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_SIZE_PRESENT      ) isom_iprintf( indent, "default-sample-size-present\n" );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT     ) isom_iprintf( indent, "default-sample-flags-present\n" );
    isom_iprintf( --indent, "track_ID = %"PRIu32"\n", tfhd->track_ID );
    if( tfhd->flags & ISOM_TF_FLAGS_BASE_DATA_OFFSET_PRESENT )
        isom_iprintf( indent, "base_data_offset = %"PRIu64"\n", tfhd->base_data_offset );
    if( tfhd->flags & ISOM_TF_FLAGS_SAMPLE_DESCRIPTION_INDEX_PRESENT )
        isom_iprintf( indent, "sample_description_index = %"PRIu32"\n", tfhd->sample_description_index );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT )
        isom_iprintf( indent, "default_sample_duration = %"PRIu32"\n", tfhd->default_sample_duration );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_SIZE_PRESENT )
        isom_iprintf( indent, "default_sample_size = %"PRIu32"\n", tfhd->default_sample_size );
    if( tfhd->flags & ISOM_TF_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT )
        isom_iprint_sample_flags( indent, "default_sample_flags", &tfhd->default_sample_flags );
    return 0;
}

static int isom_print_trun( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_trun_entry_t *trun = (isom_trun_entry_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Fragment Run Box" );
    ++indent;
    if( trun->flags & ISOM_TR_FLAGS_DATA_OFFSET_PRESENT                    ) isom_iprintf( indent, "data-offset-present\n" );
    if( trun->flags & ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT             ) isom_iprintf( indent, "first-sample-flags-present\n" );
    if( trun->flags & ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT                ) isom_iprintf( indent, "sample-duration-present\n" );
    if( trun->flags & ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT                    ) isom_iprintf( indent, "sample-size-present\n" );
    if( trun->flags & ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT                   ) isom_iprintf( indent, "sample-flags-present\n" );
    if( trun->flags & ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT ) isom_iprintf( indent, "sample-composition-time-offsets-present\n" );
    isom_iprintf( --indent, "sample_count = %"PRIu32"\n", trun->sample_count );
    if( trun->flags & ISOM_TR_FLAGS_DATA_OFFSET_PRESENT )
        isom_iprintf( indent, "data_offset = %"PRId32"\n", trun->data_offset );
    if( trun->flags & ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT )
        isom_iprint_sample_flags( indent, "first_sample_flags", &trun->first_sample_flags );
    if( trun->optional )
    {
        uint32_t i = 0;
        for( lsmash_entry_t *entry = trun->optional->head; entry; entry = entry->next )
        {
            isom_trun_optional_row_t *row = (isom_trun_optional_row_t *)entry->data;
            isom_iprintf( indent++, "sample[%"PRIu32"]\n", i++ );
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT )
                isom_iprintf( indent, "sample_duration = %"PRIu32"\n", row->sample_duration );
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT )
                isom_iprintf( indent, "sample_size = %"PRIu32"\n", row->sample_size );
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT )
                isom_iprint_sample_flags( indent, "sample_flags", &row->sample_flags );
            if( trun->flags & ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT )
                isom_iprintf( indent, "sample_composition_time_offset = %"PRIu32"\n", row->sample_composition_time_offset );
            --indent;
        }
    }
    return 0;
}

static int isom_print_free( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Free Space Box" );
}

static int isom_print_mdat( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Media Data Box" );
}

static int isom_print_mfra( lsmash_root_t *root, isom_box_t *box, int level )
{
    return isom_print_simple( box, level, "Movie Fragment Random Access Box" );
}

static int isom_print_tfra( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_tfra_entry_t *tfra = (isom_tfra_entry_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Track Fragment Random Access Box" );
    isom_iprintf( indent, "track_ID = %"PRIu32"\n", tfra->track_ID );
    isom_iprintf( indent, "reserved = 0x%08"PRIx32"\n", tfra->reserved );
    isom_iprintf( indent, "length_size_of_traf_num = %"PRIu8"\n", tfra->length_size_of_traf_num );
    isom_iprintf( indent, "length_size_of_trun_num = %"PRIu8"\n", tfra->length_size_of_trun_num );
    isom_iprintf( indent, "length_size_of_sample_num = %"PRIu8"\n", tfra->length_size_of_sample_num );
    isom_iprintf( indent, "number_of_entry = %"PRIu32"\n", tfra->number_of_entry );
    if( tfra->list )
    {
        uint32_t i = 0;
        for( lsmash_entry_t *entry = tfra->list->head; entry; entry = entry->next )
        {
            isom_tfra_location_time_entry_t *data = (isom_tfra_location_time_entry_t *)entry->data;
            isom_iprintf( indent++, "entry[%"PRIu32"]\n", i++ );
            isom_iprintf( indent, "time = %"PRIu64"\n", data->time );
            isom_iprintf( indent, "moof_offset = %"PRIu64"\n", data->moof_offset );
            isom_iprintf( indent, "traf_number = %"PRIu32"\n", data->traf_number );
            isom_iprintf( indent, "trun_number = %"PRIu32"\n", data->trun_number );
            isom_iprintf( indent, "sample_number = %"PRIu32"\n", data->sample_number );
            --indent;
        }
    }
    return 0;
}

static int isom_print_mfro( lsmash_root_t *root, isom_box_t *box, int level )
{
    if( !box )
        return -1;
    isom_mfro_t *mfro = (isom_mfro_t *)box;
    int indent = level;
    isom_print_box_common( indent++, box, "Movie Fragment Random Access Offset Box" );
    isom_iprintf( indent, "size = %"PRIu32"\n", mfro->length );
    return 0;
}

int lsmash_print_movie( lsmash_root_t *root )
{
    if( !root || !root->print || !(root->flags & LSMASH_FILE_MODE_DUMP) )
        return -1;
    printf( "[ROOT]\n" );
    printf( "    size = %"PRIu64"\n", root->size );
    for( lsmash_entry_t *entry = root->print->head; entry; entry = entry->next )
    {
        isom_print_entry_t *data = (isom_print_entry_t *)entry->data;
        if( !data || data->func( root, data->box, data->level ) )
            return -1;
    }
    return 0;
}

static isom_print_box_t isom_select_print_func( isom_box_t *box )
{
    if( box->manager & LSMASH_UNKNOWN_BOX )
        return isom_print_unknown;
    if( box->parent )
    {
        isom_box_t *parent = box->parent;
        if( parent->type == ISOM_BOX_TYPE_STSD )
            switch( box->type )
            {
                case ISOM_CODEC_TYPE_AVC1_VIDEO :
                case ISOM_CODEC_TYPE_AVC2_VIDEO :
                case ISOM_CODEC_TYPE_AVCP_VIDEO :
                case ISOM_CODEC_TYPE_DRAC_VIDEO :
                case ISOM_CODEC_TYPE_ENCV_VIDEO :
                case ISOM_CODEC_TYPE_MJP2_VIDEO :
                case ISOM_CODEC_TYPE_MP4V_VIDEO :
                case ISOM_CODEC_TYPE_MVC1_VIDEO :
                case ISOM_CODEC_TYPE_MVC2_VIDEO :
                case ISOM_CODEC_TYPE_S263_VIDEO :
                case ISOM_CODEC_TYPE_SVC1_VIDEO :
                case ISOM_CODEC_TYPE_VC_1_VIDEO :
                case QT_CODEC_TYPE_CFHD_VIDEO :
                case QT_CODEC_TYPE_DV10_VIDEO :
                case QT_CODEC_TYPE_DVOO_VIDEO :
                case QT_CODEC_TYPE_DVOR_VIDEO :
                case QT_CODEC_TYPE_DVTV_VIDEO :
                case QT_CODEC_TYPE_DVVT_VIDEO :
                case QT_CODEC_TYPE_HD10_VIDEO :
                case QT_CODEC_TYPE_M105_VIDEO :
                case QT_CODEC_TYPE_PNTG_VIDEO :
                case QT_CODEC_TYPE_SVQ1_VIDEO :
                case QT_CODEC_TYPE_SVQ3_VIDEO :
                case QT_CODEC_TYPE_SHR0_VIDEO :
                case QT_CODEC_TYPE_SHR1_VIDEO :
                case QT_CODEC_TYPE_SHR2_VIDEO :
                case QT_CODEC_TYPE_SHR3_VIDEO :
                case QT_CODEC_TYPE_SHR4_VIDEO :
                case QT_CODEC_TYPE_WRLE_VIDEO :
                case QT_CODEC_TYPE_APCH_VIDEO :
                case QT_CODEC_TYPE_APCN_VIDEO :
                case QT_CODEC_TYPE_APCS_VIDEO :
                case QT_CODEC_TYPE_APCO_VIDEO :
                case QT_CODEC_TYPE_AP4H_VIDEO :
                case QT_CODEC_TYPE_CIVD_VIDEO :
                //case QT_CODEC_TYPE_DRAC_VIDEO :
                case QT_CODEC_TYPE_DVH5_VIDEO :
                case QT_CODEC_TYPE_DVH6_VIDEO :
                case QT_CODEC_TYPE_DVHP_VIDEO :
                case QT_CODEC_TYPE_FLIC_VIDEO :
                case QT_CODEC_TYPE_GIF_VIDEO :
                case QT_CODEC_TYPE_H261_VIDEO :
                case QT_CODEC_TYPE_H263_VIDEO :
                case QT_CODEC_TYPE_JPEG_VIDEO :
                case QT_CODEC_TYPE_MJPA_VIDEO :
                case QT_CODEC_TYPE_MJPB_VIDEO :
                case QT_CODEC_TYPE_PNG_VIDEO :
                case QT_CODEC_TYPE_RLE_VIDEO :
                case QT_CODEC_TYPE_RPZA_VIDEO :
                case QT_CODEC_TYPE_TGA_VIDEO :
                case QT_CODEC_TYPE_TIFF_VIDEO :
                    return isom_print_visual_description;
                case ISOM_CODEC_TYPE_AC_3_AUDIO :
                case ISOM_CODEC_TYPE_ALAC_AUDIO :
                case ISOM_CODEC_TYPE_DRA1_AUDIO :
                case ISOM_CODEC_TYPE_DTSC_AUDIO :
                case ISOM_CODEC_TYPE_DTSH_AUDIO :
                case ISOM_CODEC_TYPE_DTSL_AUDIO :
                case ISOM_CODEC_TYPE_DTSE_AUDIO :
                case ISOM_CODEC_TYPE_EC_3_AUDIO :
                case ISOM_CODEC_TYPE_ENCA_AUDIO :
                case ISOM_CODEC_TYPE_G719_AUDIO :
                case ISOM_CODEC_TYPE_G726_AUDIO :
                case ISOM_CODEC_TYPE_M4AE_AUDIO :
                case ISOM_CODEC_TYPE_MLPA_AUDIO :
                case ISOM_CODEC_TYPE_MP4A_AUDIO :
                //case ISOM_CODEC_TYPE_RAW_AUDIO  :
                case ISOM_CODEC_TYPE_SAMR_AUDIO :
                case ISOM_CODEC_TYPE_SAWB_AUDIO :
                case ISOM_CODEC_TYPE_SAWP_AUDIO :
                case ISOM_CODEC_TYPE_SEVC_AUDIO :
                case ISOM_CODEC_TYPE_SQCP_AUDIO :
                case ISOM_CODEC_TYPE_SSMV_AUDIO :
                //case ISOM_CODEC_TYPE_TWOS_AUDIO :
                case QT_CODEC_TYPE_23NI_AUDIO :
                case QT_CODEC_TYPE_MAC3_AUDIO :
                case QT_CODEC_TYPE_MAC6_AUDIO :
                case QT_CODEC_TYPE_NONE_AUDIO :
                case QT_CODEC_TYPE_QDM2_AUDIO :
                case QT_CODEC_TYPE_QDMC_AUDIO :
                case QT_CODEC_TYPE_QCLP_AUDIO :
                case QT_CODEC_TYPE_AGSM_AUDIO :
                case QT_CODEC_TYPE_ALAW_AUDIO :
                case QT_CODEC_TYPE_CDX2_AUDIO :
                case QT_CODEC_TYPE_CDX4_AUDIO :
                case QT_CODEC_TYPE_DVCA_AUDIO :
                case QT_CODEC_TYPE_DVI_AUDIO :
                case QT_CODEC_TYPE_FL32_AUDIO :
                case QT_CODEC_TYPE_FL64_AUDIO :
                case QT_CODEC_TYPE_IMA4_AUDIO :
                case QT_CODEC_TYPE_IN24_AUDIO :
                case QT_CODEC_TYPE_IN32_AUDIO :
                case QT_CODEC_TYPE_LPCM_AUDIO :
                case QT_CODEC_TYPE_RAW_AUDIO :
                case QT_CODEC_TYPE_SOWT_AUDIO :
                case QT_CODEC_TYPE_TWOS_AUDIO :
                case QT_CODEC_TYPE_ULAW_AUDIO :
                case QT_CODEC_TYPE_VDVA_AUDIO :
                case QT_CODEC_TYPE_FULLMP3_AUDIO :
                case QT_CODEC_TYPE_MP3_AUDIO :
                case QT_CODEC_TYPE_ADPCM2_AUDIO :
                case QT_CODEC_TYPE_ADPCM17_AUDIO :
                case QT_CODEC_TYPE_GSM49_AUDIO :
                case QT_CODEC_TYPE_NOT_SPECIFIED :
                    return isom_print_audio_description;
                case QT_CODEC_TYPE_TEXT_TEXT :
                    return isom_print_text_description;
                case ISOM_CODEC_TYPE_TX3G_TEXT :
                    return isom_print_tx3g_description;
                default :
                    return isom_print_unknown;
            }
        if( parent->type == QT_BOX_TYPE_WAVE )
            switch( box->type )
            {
                case QT_BOX_TYPE_FRMA :
                    return isom_print_frma;
                case QT_BOX_TYPE_ENDA :
                    return isom_print_enda;
                case ISOM_BOX_TYPE_ESDS :
                    return isom_print_esds;
                case QT_BOX_TYPE_TERMINATOR :
                    return isom_print_terminator;
                default :
                    return isom_print_unknown;
            }
        if( parent->type == ISOM_BOX_TYPE_TREF )
            return isom_print_track_reference_type;
        if( parent->parent && parent->parent->type == ISOM_BOX_TYPE_ILST )
        {
            if( parent->type == LSMASH_4CC( '-', '-', '-', '-' ) )
            {
                if( box->type == ISOM_BOX_TYPE_MEAN )
                    return isom_print_mean;
                if( box->type == ISOM_BOX_TYPE_NAME )
                    return isom_print_name;
            }
            if( box->type == ISOM_BOX_TYPE_DATA )
                return isom_print_data;
        }
        if( parent->type == ISOM_BOX_TYPE_ILST )
            return isom_print_metaitem;
    }
    switch( box->type )
    {
        case ISOM_BOX_TYPE_FTYP :
            return isom_print_ftyp;
        case ISOM_BOX_TYPE_MOOV :
            return isom_print_moov;
        case ISOM_BOX_TYPE_MVHD :
            return isom_print_mvhd;
        case ISOM_BOX_TYPE_IODS :
            return isom_print_iods;
        case ISOM_BOX_TYPE_ESDS :
            return isom_print_esds;
        case ISOM_BOX_TYPE_TRAK :
            return isom_print_trak;
        case ISOM_BOX_TYPE_TKHD :
            return isom_print_tkhd;
        case QT_BOX_TYPE_TAPT :
            return isom_print_tapt;
        case QT_BOX_TYPE_CLEF :
            return isom_print_clef;
        case QT_BOX_TYPE_PROF :
            return isom_print_prof;
        case QT_BOX_TYPE_ENOF :
            return isom_print_enof;
        case ISOM_BOX_TYPE_EDTS :
            return isom_print_edts;
        case ISOM_BOX_TYPE_ELST :
            return isom_print_elst;
        case ISOM_BOX_TYPE_TREF :
            return isom_print_tref;
        case ISOM_BOX_TYPE_MDIA :
            return isom_print_mdia;
        case ISOM_BOX_TYPE_MDHD :
            return isom_print_mdhd;
        case ISOM_BOX_TYPE_HDLR :
            return isom_print_hdlr;
        case ISOM_BOX_TYPE_MINF :
            return isom_print_minf;
        case ISOM_BOX_TYPE_VMHD :
            return isom_print_vmhd;
        case ISOM_BOX_TYPE_SMHD :
            return isom_print_smhd;
        case ISOM_BOX_TYPE_HMHD :
            return isom_print_hmhd;
        case ISOM_BOX_TYPE_NMHD :
            return isom_print_nmhd;
        case QT_BOX_TYPE_GMHD :
            return isom_print_gmhd;
        case QT_BOX_TYPE_GMIN :
            return isom_print_gmin;
        case QT_BOX_TYPE_TEXT :
            return isom_print_text;
        case ISOM_BOX_TYPE_DINF :
            return isom_print_dinf;
        case ISOM_BOX_TYPE_DREF :
            return isom_print_dref;
        case ISOM_BOX_TYPE_URL  :
            return isom_print_url;
        case ISOM_BOX_TYPE_STBL :
            return isom_print_stbl;
        case ISOM_BOX_TYPE_STSD :
            return isom_print_stsd;
        case ISOM_BOX_TYPE_BTRT :
            return isom_print_btrt;
        case ISOM_BOX_TYPE_CLAP :
            return isom_print_clap;
        case ISOM_BOX_TYPE_PASP :
            return isom_print_pasp;
        case QT_BOX_TYPE_COLR :
            return isom_print_colr;
        case ISOM_BOX_TYPE_STSL :
            return isom_print_stsl;
        case ISOM_BOX_TYPE_AVCC :
            return isom_print_avcC;
        case QT_BOX_TYPE_WAVE :
            return isom_print_wave;
        case QT_BOX_TYPE_CHAN :
            return isom_print_chan;
        case ISOM_BOX_TYPE_FTAB :
            return isom_print_ftab;
        case ISOM_BOX_TYPE_STTS :
            return isom_print_stts;
        case ISOM_BOX_TYPE_CTTS :
            return isom_print_ctts;
        case ISOM_BOX_TYPE_CSLG :
            return isom_print_cslg;
        case ISOM_BOX_TYPE_STSS :
            return isom_print_stss;
        case QT_BOX_TYPE_STPS :
            return isom_print_stps;
        case ISOM_BOX_TYPE_SDTP :
            return isom_print_sdtp;
        case ISOM_BOX_TYPE_STSC :
            return isom_print_stsc;
        case ISOM_BOX_TYPE_STSZ :
            return isom_print_stsz;
        case ISOM_BOX_TYPE_STCO :
        case ISOM_BOX_TYPE_CO64 :
            return isom_print_stco;
        case ISOM_BOX_TYPE_SGPD :
            return isom_print_sgpd;
        case ISOM_BOX_TYPE_SBGP :
            return isom_print_sbgp;
        case ISOM_BOX_TYPE_UDTA :
            return isom_print_udta;
        case ISOM_BOX_TYPE_CHPL :
            return isom_print_chpl;
        case ISOM_BOX_TYPE_MVEX :
            return isom_print_mvex;
        case ISOM_BOX_TYPE_MEHD :
            return isom_print_mehd;
        case ISOM_BOX_TYPE_TREX :
            return isom_print_trex;
        case ISOM_BOX_TYPE_MOOF :
            return isom_print_moof;
        case ISOM_BOX_TYPE_MFHD :
            return isom_print_mfhd;
        case ISOM_BOX_TYPE_TRAF :
            return isom_print_traf;
        case ISOM_BOX_TYPE_TFHD :
            return isom_print_tfhd;
        case ISOM_BOX_TYPE_TRUN :
            return isom_print_trun;
        case ISOM_BOX_TYPE_FREE :
        case ISOM_BOX_TYPE_SKIP :
            return isom_print_free;
        case ISOM_BOX_TYPE_MDAT :
            return isom_print_mdat;
        case QT_BOX_TYPE_KEYS :
            return isom_print_keys;
        case ISOM_BOX_TYPE_META :
            return isom_print_meta;
        case ISOM_BOX_TYPE_ILST :
            return isom_print_ilst;
        case ISOM_BOX_TYPE_MFRA :
            return isom_print_mfra;
        case ISOM_BOX_TYPE_TFRA :
            return isom_print_tfra;
        case ISOM_BOX_TYPE_MFRO :
            return isom_print_mfro;
        default :
            return isom_print_unknown;
    }
}

int isom_add_print_func( lsmash_root_t *root, void *box, int level )
{
    if( !(root->flags & LSMASH_FILE_MODE_DUMP) )
        return 0;
    isom_print_entry_t *data = malloc( sizeof(isom_print_entry_t) );
    if( !data )
        return -1;
    data->level = level;
    data->box = (isom_box_t *)box;
    data->func = isom_select_print_func( (isom_box_t *)box );
    if( !data->func || lsmash_add_entry( root->print, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static void isom_remove_print_func( isom_print_entry_t *data )
{
    if( !data || !data->box )
        return;
    if( data->box->manager & LSMASH_ABSENT_IN_ROOT )
        free( data->box );      /* free flagged box */
    free( data );
}

void isom_remove_print_funcs( lsmash_root_t *root )
{
    lsmash_remove_list( root->print, isom_remove_print_func );
    root->print = NULL;
}

#endif /* LSMASH_DEMUXER_ENABLED */
