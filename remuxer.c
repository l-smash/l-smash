/*****************************************************************************
 * remuxer.c:
 *****************************************************************************
 * Copyright (C) 2011 L-SMASH project
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

#include "lsmash.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#define LSMASH_MAX( a, b ) ((a) > (b) ? (a) : (b))

#define eprintf( ... ) fprintf( stderr, __VA_ARGS__ )

typedef struct
{
    uint32_t track_ID;
    uint32_t last_sample_delta;
    uint32_t current_sample_number;
    int      reach_end_of_media_timeline;
    lsmash_track_parameters_t track_param;
    lsmash_media_parameters_t media_param;
} track_t;

typedef struct
{
    lsmash_root_t                 *root;
    lsmash_itunes_metadata_list_t *itunes_meta_list;
    track_t                       *track;
    lsmash_movie_parameters_t movie_param;
    uint32_t num_tracks;
    uint32_t current_track_number;
} movie_t;

typedef struct
{
    movie_t *output;
    movie_t *input;
    int num_input;
} movie_io_t;

static void cleanup_movie( movie_t *movie )
{
    if( !movie )
        return;
    if( movie->itunes_meta_list )
        lsmash_destroy_itunes_metadata( movie->itunes_meta_list );
    if( movie->track )
        free( movie->track );
    lsmash_destroy_root( movie->root );
    movie->root = NULL;
    movie->itunes_meta_list = NULL;
    movie->track = NULL;
}

static int remuxer_error( movie_io_t *io, char* message )
{
    for( int i = 0; i < io->num_input; i++ )
        cleanup_movie( &io->input[i] );
    cleanup_movie( io->output );
    eprintf( message );
    return -1;
}

static int error_message( char* message )
{
    eprintf( message );
    return -1;
}

#define REMUXER_ERR( message ) remuxer_error( &io, message )
#define ERROR_MSG( message ) error_message( message )

static int get_movie( movie_t *input, char *input_name )
{
    if( !strcmp( input_name, "-" ) )
        return ERROR_MSG( "Standard input not supported.\n" );
    input->root = lsmash_open_movie( input_name, LSMASH_FILE_MODE_READ );
    if( !input->root )
        return ERROR_MSG( "Failed to open input file.\n" );
    input->current_track_number = 1;
    lsmash_movie_parameters_t *movie_param = &input->movie_param;
    lsmash_initialize_movie_parameters( movie_param );
    lsmash_get_movie_parameters( input->root, movie_param );
    uint32_t num_tracks = input->num_tracks = movie_param->number_of_tracks;
    /* Create tracks. */
    track_t *track = input->track = malloc( num_tracks * sizeof(track_t) );
    if( !track )
        return ERROR_MSG( "Failed to alloc input tracks.\n" );
    memset( track, 0, num_tracks * sizeof(track_t) );
    for( uint32_t i = 0; i < num_tracks; i++ )
    {
        track[i].track_ID = lsmash_get_track_ID( input->root, i + 1 );
        if( !track[i].track_ID )
            return ERROR_MSG( "Failed to get track_ID.\n" );
    }
    for( uint32_t i = 0; i < num_tracks; i++ )
    {
        input->itunes_meta_list = lsmash_export_itunes_metadata( input->root );
        if( !input->itunes_meta_list )
            return ERROR_MSG( "Failed to get iTunes metadata.\n" );
        lsmash_initialize_track_parameters( &track[i].track_param );
        if( lsmash_get_track_parameters( input->root, track[i].track_ID, &track[i].track_param ) )
            return ERROR_MSG( "Failed to get track parameters.\n" );
        lsmash_initialize_media_parameters( &track[i].media_param );
        if( lsmash_get_media_parameters( input->root, track[i].track_ID, &track[i].media_param ) )
            return ERROR_MSG( "Failed to get media parameters.\n" );
        if( lsmash_construct_timeline( input->root, track[i].track_ID ) )
            return ERROR_MSG( "Failed to construct media_timeline.\n" );
        if( lsmash_get_last_sample_delta_from_media_timeline( input->root, track[i].track_ID, &track[i].last_sample_delta ) )
            return ERROR_MSG( "Failed to get the last sample delta.\n" );
        track[i].current_sample_number = 1;
    }
    lsmash_discard_boxes( input->root );
    return 0;
}

static int set_movie_parameters( movie_io_t *io )
{
    uint32_t num_input = io->num_input;
    movie_t *input     = io->input;
    movie_t *output    = io->output;
    lsmash_initialize_movie_parameters( &output->movie_param );
    /* Pick the most used major_brands. */
    lsmash_brand_type major_brand      [num_input];
    uint32_t          minor_version    [num_input];
    uint32_t          major_brand_count[num_input];
    uint32_t          num_major_brand = 0;
    for( uint32_t i = 0; i < num_input; i++ )
    {
        major_brand      [num_major_brand] = input[i].movie_param.major_brand;
        minor_version    [num_major_brand] = input[i].movie_param.minor_version;
        major_brand_count[num_major_brand] = 0;
        for( uint32_t j = 0; j < num_input; j++ )
            if( (major_brand  [num_major_brand] == input[j].movie_param.major_brand)
             && (minor_version[num_major_brand] == input[j].movie_param.minor_version) )
            {
                if( i <= j )
                    ++major_brand_count[num_major_brand];
                else
                {
                    /* This major_brand already exists. Skip this. */
                    major_brand_count[num_major_brand] = 0;
                    --num_major_brand;
                    break;
                }
            }
        ++num_major_brand;
    }
    uint32_t most_used_count = 0;
    for( uint32_t i = 0; i < num_major_brand; i++ )
        if( major_brand_count[i] > most_used_count )
        {
            most_used_count = major_brand_count[i];
            output->movie_param.major_brand   = major_brand  [i];
            output->movie_param.minor_version = minor_version[i];
        }
    /* Deduplicate compatible brands. */
    uint32_t num_input_brands = 0;
    for( uint32_t i = 0; i < num_input; i++ )
        num_input_brands += input[i].movie_param.number_of_brands;
    lsmash_brand_type input_brands[num_input_brands];
    num_input_brands = 0;
    for( uint32_t i = 0; i < num_input; i++ )
        for( uint32_t j = 0; j < input[i].movie_param.number_of_brands; j++ )
            input_brands[num_input_brands++] = input[i].movie_param.brands[j];
    lsmash_brand_type output_brands[num_input_brands];
    uint32_t num_output_brands = 0;
    for( uint32_t i = 0; i < num_input_brands; i++ )
    {
        if( !input_brands[i] )
            continue;
        output_brands[num_output_brands] = input_brands[i];
        for( uint32_t j = 0; j < num_output_brands; j++ )
            if( output_brands[num_output_brands] == output_brands[j] )
            {
                /* This brand already exists. Skip this. */
                --num_output_brands;
                break;
            }
        ++num_output_brands;
    }
    output->movie_param.number_of_brands = num_output_brands;
    output->movie_param.brands           = output_brands;
    return lsmash_set_movie_parameters( output->root, &output->movie_param );
}

static int moov_to_front_callback( void *param, uint64_t written_movie_size, uint64_t total_movie_size )
{
    eprintf( "Finalizing: [%5.2lf%%]\r", ((double)written_movie_size / total_movie_size) * 100.0 );
    return 0;
}

int main( int argc, char *argv[] )
{
    if( argc < 3 || !strcasecmp( argv[1], "-h" ) || !strcasecmp( argv[1], "--help" ) )
    {
        fprintf( stderr, "Usage: remuxer input1 [input2 input3 ...] output\n" );
        return -1;
    }
    int num_input = argc - 2;
    movie_t output = { 0 };
    movie_t input[ num_input ];
    movie_io_t io = { &output, input, num_input };
    memset( input, 0, num_input * sizeof(movie_t) );
    /* Get input movies. */
    for( int i = 0; i < num_input; i++ )
        if( get_movie( &input[i], argv[i + 1] ) )
            return REMUXER_ERR( "Failed to get input movie.\n" );
    /* Create output movie. */
    output.root = lsmash_open_movie( argv[argc - 1], LSMASH_FILE_MODE_WRITE );
    if( !output.root )
        return REMUXER_ERR( "Failed to open output movie.\n" );
    if( set_movie_parameters( &io ) )
        return REMUXER_ERR( "Failed to set output movie parameters.\n" );
    /* Set iTunes metadata. */
    for( int i = 0; i < num_input; i++ )
        if( lsmash_import_itunes_metadata( output.root, input[i].itunes_meta_list ) )
            return REMUXER_ERR( "Failed to set iTunes metadata.\n" );
    /* Create tracks of the output movie. */
    for( int i = 0; i < num_input; i++ )
        output.num_tracks += input[i].num_tracks;
    output.track = malloc( output.num_tracks * sizeof(track_t) );
    if( !output.track )
        return REMUXER_ERR( "Failed to alloc output tracks.\n" );
    output.current_track_number = 1;
    for( int i = 0; i < num_input; i++ )
        for( uint32_t j = 0; j < input[i].num_tracks; j++ )
        {
            track_t *in_track = &input[i].track[j];
            track_t *out_track = &output.track[output.current_track_number - 1];
            uint32_t handler_type = in_track->media_param.handler_type;
            out_track->track_ID = lsmash_create_track( output.root, handler_type );
            if( !out_track->track_ID )
                return REMUXER_ERR( "Failed to create a track.\n" );
            /* Copy track and media parameters except for track_ID. */
            out_track->track_param = in_track->track_param;
            out_track->media_param = in_track->media_param;
            out_track->track_param.track_ID = out_track->track_ID;
            if( lsmash_set_track_parameters( output.root, out_track->track_ID, &out_track->track_param ) )
                return REMUXER_ERR( "Failed to set track parameters.\n" );
            if( lsmash_set_media_parameters( output.root, out_track->track_ID, &out_track->media_param ) )
                return REMUXER_ERR( "Failed to set media parameters.\n" );
            if( lsmash_copy_timeline_map( output.root, out_track->track_ID, input[i].root, in_track->track_ID ) )
                return REMUXER_ERR( "Failed to copy a timeline map.\n" );
            if( lsmash_copy_decoder_specific_info( output.root, out_track->track_ID, input[i].root, in_track->track_ID ) )
                return REMUXER_ERR( "Failed to copy a Decoder Specific Info.\n" );
            out_track->last_sample_delta = in_track->last_sample_delta;
            out_track->current_sample_number = 1;
            out_track->reach_end_of_media_timeline = 0;
            ++ output.current_track_number;
        }
    output.current_track_number = 1;
    /* Start muxing. */
    double   largest_dts = 0;
    uint32_t input_movie_number = 1;
    uint32_t num_consecutive_sample_skip = 0;
    uint32_t num_active_input_tracks = output.num_tracks;
    uint64_t total_media_size = 0;
    while( 1 )
    {
        movie_t *movie = &input[input_movie_number - 1];
        track_t *in_track = &movie->track[movie->current_track_number - 1];
        /* Try append a sample in an input track where we didn't reach the end of media timeline. */
        if( !in_track->reach_end_of_media_timeline )
        {
            track_t *out_track = &output.track[output.current_track_number - 1];
            uint32_t in_track_ID = in_track->track_ID;
            uint32_t out_track_ID = out_track->track_ID;
            uint32_t input_media_timescale = in_track->media_param.timescale;
            /* Get a DTS from a track in an input movie. */
            uint64_t dts;
            if( lsmash_get_dts_from_media_timeline( movie->root, in_track_ID, in_track->current_sample_number, &dts ) )
            {
                if( lsmash_check_sample_existence_in_media_timeline( movie->root, in_track_ID, in_track->current_sample_number ) )
                    return REMUXER_ERR( "Failed to get the DTS.\n" );
                else
                {
                    in_track->reach_end_of_media_timeline = 1;
                    if( --num_active_input_tracks == 0 )
                        break;      /* end of muxing */
                }
            }
            /* Get and append a sample if it's good time. */
            else if( ((double)dts / input_media_timescale) <= largest_dts
             || num_consecutive_sample_skip == num_active_input_tracks )
            {
                /* Get an actual sample data from a track in an input movie. */
                lsmash_sample_t *sample = lsmash_get_sample_from_media_timeline( movie->root, in_track_ID, in_track->current_sample_number );
                if( !sample )
                    return REMUXER_ERR( "Failed to get sample.\n" );
                /* Append sample into output movie. */
                uint64_t sample_size = sample->length;      /* sample might be deleted internally after appending. */
                if( lsmash_append_sample( output.root, out_track_ID, sample ) )
                {
                    lsmash_delete_sample( sample );
                    return REMUXER_ERR( "Failed to append a sample.\n" );
                }
                largest_dts = LSMASH_MAX( largest_dts, (double)dts / input_media_timescale );
                total_media_size += sample_size;
                eprintf( "Importing: %"PRIu64" bytes\r", total_media_size );
                ++ in_track->current_sample_number;
                ++ out_track->current_sample_number;
                num_consecutive_sample_skip = 0;
            }
            else
                ++num_consecutive_sample_skip;      /* Skip appendig sample. */
        }
        /* Move the next track. */
        ++ movie->current_track_number;
        ++ output.current_track_number;
        if( movie->current_track_number > movie->num_tracks )
        {
            /* Move the next input movie. */
            movie->current_track_number = 1;
            ++input_movie_number;
        }
        if( input_movie_number > num_input )
            input_movie_number = 1;             /* Back the first input movie. */
        if( output.current_track_number > output.num_tracks )
            output.current_track_number = 1;    /* Back the first track in the output movie. */
    }
    for( uint32_t i = 0; i < output.num_tracks; i++ )
        if( lsmash_flush_pooled_samples( output.root, output.track[i].track_ID, output.track[i].last_sample_delta ) )
            return REMUXER_ERR( "Failed to flush samples.\n" );
    lsmash_adhoc_remux_t moov_to_front;
    moov_to_front.func = moov_to_front_callback;
    moov_to_front.buffer_size = 4*1024*1024;
    moov_to_front.param = NULL;
    eprintf( "                                                                               \r" );
    if( lsmash_finish_movie( output.root, &moov_to_front ) )
        return REMUXER_ERR( "Failed to finish output movie.\n" );
    for( int i = 0; i < io.num_input; i++ )
        cleanup_movie( &io.input[i] );
    cleanup_movie( io.output );
    eprintf( "Remuxing completed!                                                            \n" );
    return 0;
}
