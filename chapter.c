/*****************************************************************************
 * chapter.c:
 *****************************************************************************
 * Copyright (C) 2010-2012 L-SMASH project
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

#include "internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>

#include "box.h"
#include "isom.h"

#define CHAPTER_BUFSIZE 512
#define UTF8_BOM "\xEF\xBB\xBF"
#define UTF8_BOM_LENGTH 3

static int isom_get_start_time( char *chap_time, isom_chapter_entry_t *data )
{
    uint64_t hh, mm;
    double ss;
    if( sscanf( chap_time, "%"SCNu64":%2"SCNu64":%lf", &hh, &mm, &ss ) != 3 )
        return -1;
    /* check overflow */
    if( hh >= 5124095 || mm >= 60 || ss >= 60 )
        return -1;
    /* 1ns timescale */
    data->start_time = (hh * 3600 + mm * 60 + ss) * 1e9;
    return 0;
}

static int isom_lumber_line( char *buff, int bufsize, FILE *chapter  )
{
    char *tail;
    /* remove newline codes and skip empty line */
    do{
        if( fgets( buff, bufsize, chapter ) == NULL )
            return -1;
        tail = &buff[ strlen( buff ) - 1 ];
        while( tail >= buff && ( *tail == '\n' || *tail == '\r' ) )
            *tail-- = '\0';
    }while( tail < buff );
    return 0;
}

static int isom_read_simple_chapter( FILE *chapter, isom_chapter_entry_t *data )
{
    char buff[CHAPTER_BUFSIZE];
    int len;

    /* get start_time */
    if( isom_lumber_line( buff, CHAPTER_BUFSIZE, chapter ) )
        return -1;
    char *chapter_time = strchr( buff, '=' );   /* find separator */
    if( !chapter_time++
        || isom_get_start_time( chapter_time, data )
        || isom_lumber_line( buff, CHAPTER_BUFSIZE, chapter ) ) /* get chapter_name */
        return -1;
    char *chapter_name = strchr( buff, '=' );   /* find separator */
    if( !chapter_name++ )
        return -1;
    len = LSMASH_MIN( 255, strlen( chapter_name ) );  /* We support length of chapter_name up to 255 */
    data->chapter_name = (char *)malloc( len + 1 );
    if( !data->chapter_name )
        return -1;
    memcpy( data->chapter_name, chapter_name, len );
    data->chapter_name[len] = '\0';
    return 0;
}

static int isom_read_minimum_chapter( FILE *chapter, isom_chapter_entry_t *data )
{
    char buff[CHAPTER_BUFSIZE];
    int len;

    if( isom_lumber_line( buff, CHAPTER_BUFSIZE, chapter ) ) /* read newline */
        return -1;
    char *p_buff = !memcmp( buff, UTF8_BOM, UTF8_BOM_LENGTH ) ? &buff[UTF8_BOM_LENGTH] : &buff[0]; /* BOM detection */
    if( isom_get_start_time( p_buff, data ) ) /* get start_time */
        return -1;
    /* get chapter_name */
    char *chapter_name = strchr( buff, ' ' );   /* find separator */
    if( !chapter_name++ )
        return -1;
    len = LSMASH_MIN( 255, strlen( chapter_name ) );  /* We support length of chapter_name up to 255 */
    data->chapter_name = (char *)malloc( len + 1 );
    if( !data->chapter_name )
        return -1;
    memcpy( data->chapter_name, chapter_name, len );
    data->chapter_name[len] = '\0';
    return 0;
}

typedef int (*fn_get_chapter_data)( FILE *, isom_chapter_entry_t * );

static fn_get_chapter_data isom_check_chap_line( char *file_name )
{
    char buff[CHAPTER_BUFSIZE];
    FILE *fp = fopen( file_name, "rb" );
    if( !fp )
    {
        lsmash_log( LSMASH_LOG_ERROR, "failed to open the chapter file \"%s\".\n", file_name );
        return NULL;
    }
    fn_get_chapter_data fnc = NULL;
    if( fgets( buff, CHAPTER_BUFSIZE, fp ) != NULL )
    {
        char *p_buff = !memcmp( buff, UTF8_BOM, UTF8_BOM_LENGTH ) ? &buff[UTF8_BOM_LENGTH] : &buff[0];   /* BOM detection */
        if( !strncmp( p_buff, "CHAPTER", 7 ) )
            fnc = isom_read_simple_chapter;
        else if( isdigit( p_buff[0] ) && isdigit( p_buff[1] ) && p_buff[2] == ':'
             && isdigit( p_buff[3] ) && isdigit( p_buff[4] ) && p_buff[5] == ':' )
            fnc = isom_read_minimum_chapter;
        else
            lsmash_log( LSMASH_LOG_ERROR, "the chapter file is malformed.\n" );
    }
    fclose( fp );
    return fnc;
}

int lsmash_set_tyrant_chapter( lsmash_root_t *root, char *file_name, int add_bom )
{
    /* This function should be called after updating of the latest movie duration. */
    if( !root || !root->moov || !root->moov->mvhd || !root->moov->mvhd->timescale || !root->moov->mvhd->duration )
        goto error_message;
    /* check each line format */
    fn_get_chapter_data fnc = isom_check_chap_line( file_name );
    if( !fnc )
        goto error_message;
    FILE *chapter = fopen( file_name, "rb" );
    if( !chapter )
    {
        lsmash_log( LSMASH_LOG_ERROR, "failed to open the chapter file \"%s\".\n", file_name );
        goto error_message;
    }
    if( isom_add_udta( root, 0 ) || isom_add_chpl( root->moov ) )
        goto fail;
    isom_chapter_entry_t data = {0};
    while( !fnc( chapter, &data ) )
    {
        if( add_bom )
        {
            char *chapter_name_with_bom = (char *)malloc( strlen( data.chapter_name ) + 1 + UTF8_BOM_LENGTH );
            if( !chapter_name_with_bom )
                goto fail2;
            sprintf( chapter_name_with_bom, "%s%s", UTF8_BOM, data.chapter_name );
            free( data.chapter_name );
            data.chapter_name = chapter_name_with_bom;
        }
        data.start_time = (data.start_time + 50) / 100;    /* convert to 100ns unit */
        if( data.start_time / 1e7 > (double)root->moov->mvhd->duration / root->moov->mvhd->timescale )
        {
            lsmash_log( LSMASH_LOG_WARNING, "a chapter point exceeding the actual duration detected. This chapter point and the following ones (if any) will be cut off.\n" );
            free( data.chapter_name );
            break;
        }
        if( isom_add_chpl_entry( root->moov->udta->chpl, &data ) )
            goto fail2;
        free( data.chapter_name );
        data.chapter_name = NULL;
    }
    fclose( chapter );
    return 0;
fail2:
    if( data.chapter_name )
        free( data.chapter_name );
fail:
    fclose( chapter );
error_message:
    lsmash_log( LSMASH_LOG_ERROR, "failed to set chapter list.\n" );
    return -1;
}

int lsmash_create_reference_chapter_track( lsmash_root_t *root, uint32_t track_ID, char *file_name )
{
    if( !root || !root->moov || !root->moov->mvhd || !root->moov->trak_list )
        goto error_message;
    if( !root->qt_compatible && !root->itunes_movie )
    {
        lsmash_log( LSMASH_LOG_ERROR, "reference chapter is not available for this file.\n" );
        goto error_message;
    }
    FILE *chapter = NULL;       /* shut up 'uninitialized' warning */
    /* Create a Track Reference Box. */
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak )
    {
        lsmash_log( LSMASH_LOG_ERROR, "the specified track ID to apply the chapter doesn't exist.\n" );
        goto error_message;
    }
    if( isom_add_tref( trak ) )
        goto error_message;
    /* Create a track_ID for a new chapter track. */
    uint32_t *id = (uint32_t *)malloc( sizeof(uint32_t) );
    if( !id )
        goto error_message;
    uint32_t chapter_track_ID = *id = root->moov->mvhd->next_track_ID;
    /* Create a Track Reference Type Box. */
    isom_tref_type_t *chap = isom_add_track_reference_type( trak->tref, QT_TREF_TYPE_CHAP, 1, id );
    if( !chap )
        goto error_message;      /* no need to free id */
    /* Create a reference chapter track. */
    if( chapter_track_ID != lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_TEXT_TRACK ) )
        goto error_message;
    /* Set track parameters. */
    lsmash_track_parameters_t track_param;
    lsmash_initialize_track_parameters( &track_param );
    track_param.mode = ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
    if( lsmash_set_track_parameters( root, chapter_track_ID, &track_param ) )
        goto fail;
    /* Set media parameters. */
    uint64_t media_timescale = lsmash_get_media_timescale( root, track_ID );
    if( !media_timescale )
        goto fail;
    lsmash_media_parameters_t media_param;
    lsmash_initialize_media_parameters( &media_param );
    media_param.timescale = media_timescale;
    media_param.ISO_language = root->max_3gpp_version >= 6 || root->itunes_movie ? ISOM_LANGUAGE_CODE_UNDEFINED : 0;
    media_param.MAC_language = 0;
    if( lsmash_set_media_parameters( root, chapter_track_ID, &media_param ) )
        goto fail;
    /* Create a sample description. */
    uint32_t sample_type = root->max_3gpp_version >= 6 || root->itunes_movie ? ISOM_CODEC_TYPE_TX3G_TEXT : QT_CODEC_TYPE_TEXT_TEXT;
    uint32_t sample_entry = lsmash_add_sample_entry( root, chapter_track_ID, sample_type, NULL );
    if( !sample_entry )
        goto fail;
    /* Check each line format. */
    fn_get_chapter_data fnc = isom_check_chap_line( file_name );
    if( !fnc )
        goto fail;
    /* Open chapter format file. */
    chapter = fopen( file_name, "rb" );
    if( !chapter )
    {
        lsmash_log( LSMASH_LOG_ERROR, "failed to open the chapter file \"%s\".\n", file_name );
        goto fail;
    }
    /* Parse the file and write text samples. */
    isom_chapter_entry_t data;
    while( !fnc( chapter, &data ) )
    {
        /* set start_time */
        data.start_time = data.start_time * 1e-9 * media_timescale + 0.5;
        /* write a text sample here */
        uint16_t name_length = strlen( data.chapter_name );
        lsmash_sample_t *sample = lsmash_create_sample( 2 + name_length + 12 * (sample_type == QT_CODEC_TYPE_TEXT_TEXT) );
        if( !sample )
        {
            free( data.chapter_name );
            goto fail;
        }
        sample->data[0] = (name_length >> 8) & 0xff;
        sample->data[1] =  name_length       & 0xff;
        memcpy( sample->data + 2, data.chapter_name, name_length );
        if( sample_type == QT_CODEC_TYPE_TEXT_TEXT )
        {
            /* QuickTime Player requires Text Encoding Attribute Box ('encd') if media language is ISO language codes : undefined.
             * Also this box can avoid garbling if the QuickTime text sample is encoded by Unicode characters.
             * Note: 3GPP Timed Text supports only UTF-8 or UTF-16, so this box isn't needed. */
            static const uint8_t encd[12] =
                {
                    0x00, 0x00, 0x00, 0x0C,     /* size: 12 */
                    0x65, 0x6E, 0x63, 0x64,     /* type: 'encd' */
                    0x00, 0x00, 0x01, 0x00      /* Unicode Encoding */
                };
            memcpy( sample->data + 2 + name_length, encd, 12 );
        }
        sample->dts = sample->cts = data.start_time;
        sample->prop.random_access_type = ISOM_SAMPLE_RANDOM_ACCESS_TYPE_SYNC;
        sample->index = sample_entry;
        if( lsmash_append_sample( root, chapter_track_ID, sample ) )
        {
            free( data.chapter_name );
            goto fail;
        }
        free( data.chapter_name );
        data.chapter_name = NULL;
    }
    if( lsmash_flush_pooled_samples( root, chapter_track_ID, 0 ) )
        goto fail;
    isom_trak_entry_t *chapter_trak = isom_get_trak( root, chapter_track_ID );
    if( !chapter_trak )
        goto fail;
    fclose( chapter );
    chapter_trak->is_chapter = 1;
    chapter_trak->related_track_ID = track_ID;
    return 0;
fail:
    if( chapter )
        fclose( chapter );
    /* Remove chapter track reference. */
    lsmash_remove_entry_direct( trak->tref->ref_list, trak->tref->ref_list->tail, isom_remove_track_reference_type );
    if( trak->tref->ref_list->entry_count == 0 )
        isom_remove_tref( trak->tref );
    /* Remove the reference chapter track attached at tail of the list. */
    lsmash_remove_entry_direct( root->moov->trak_list, root->moov->trak_list->tail, isom_remove_trak );
error_message:
    lsmash_log( LSMASH_LOG_ERROR, "failed to set reference chapter.\n" );
    return -1;
}

int lsmash_print_chapter_list( lsmash_root_t *root )
{
    if( !root || !(root->flags & LSMASH_FILE_MODE_READ) )
        return -1;
    if( root->moov && root->moov->udta && root->moov->udta->chpl )
    {
        isom_chpl_t *chpl = root->moov->udta->chpl;
        uint32_t timescale;
        if( !chpl->version )
        {
            if( !root->moov && !root->moov->mvhd )
                return -1;
            timescale = root->moov->mvhd->timescale;
        }
        else
            timescale = 10000000;
        uint32_t i = 1;
        for( lsmash_entry_t *entry = chpl->list->head; entry; entry = entry->next )
        {
            isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
            int64_t start_time = data->start_time / timescale;
            int hh =  start_time / 3600;
            int mm = (start_time /   60) % 60;
            int ss =  start_time         % 60;
            int ms = ((data->start_time / (double)timescale) - hh * 3600 - mm * 60 - ss) * 1e3 + 0.5;
            if( !memcmp( data->chapter_name, UTF8_BOM, UTF8_BOM_LENGTH ) )    /* detect BOM */
            {
                data->chapter_name += UTF8_BOM_LENGTH;
#ifdef _WIN32
                if( i == 1 )
                    printf( UTF8_BOM );    /* add BOM on Windows */
#endif
            }
            printf( "CHAPTER%02"PRIu32"=%02d:%02d:%02d.%03d\n", i, hh, mm, ss, ms );
            printf( "CHAPTER%02"PRIu32"NAME=%s\n", i++, data->chapter_name );
        }
        return 0;
    }
    else
        lsmash_log( LSMASH_LOG_ERROR, "this file doesn't have a chapter list.\n" );
    return -1;
}
