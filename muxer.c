/*****************************************************************************
 * muxer.c:
 *****************************************************************************
 * Copyright (C) 2011 L-SMASH project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *          Takashi Hirata <silverfilain@gmail.com>
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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "lsmash.h"
#include "importer.h"

#define LSMASH_MAX( a, b ) ((a) > (b) ? (a) : (b))

#define eprintf( ... ) fprintf( stderr, __VA_ARGS__ )

#define MAX_NUM_OF_BRANDS 50
#define MAX_NUM_OF_INPUTS 10
#define MAX_NUM_OF_TRACKS 1

typedef struct
{
    int      help;
    int      isom;
    int      isom_version;
    int      qtff;
    int      brand_3gx;
    int      optimize_pd;
    int      timeline_shift;
    uint32_t num_of_brands;
    uint32_t brands[MAX_NUM_OF_BRANDS];
    uint32_t major_brand;
    uint32_t minor_version;
    uint32_t num_of_inputs;
} option_t;

typedef struct
{
    char    *raws;
    int      sbr;
    int      user_fps;
    uint32_t fps_num;
    uint32_t fps_den;
    int16_t  alternate_group;
    uint16_t ISO_language;
} input_track_option_t;

typedef struct
{
    lsmash_summary_t    *summary;
    input_track_option_t opt;
    int                  active;
} input_track_t;

typedef struct
{
    char *whole_track_option;
    int   num_of_track_delimiters;
} input_option_t;

typedef struct
{
    input_option_t     opt;
    char              *file_name;
    mp4sys_importer_t *importer;
    input_track_t      track[MAX_NUM_OF_TRACKS];
    uint32_t           num_of_tracks;
    uint32_t           num_of_active_tracks;
    uint32_t           current_track_number;
} input_t;

typedef struct
{
    lsmash_summary_t *summary;
    lsmash_sample_t  *sample;
    int               active;
    uint32_t          track_ID;
    uint32_t          timescale;
    uint32_t          timebase;
    uint32_t          sample_entry;
    uint32_t          current_sample_number;
    uint32_t          ctd_shift;
    uint32_t          last_delta;
    uint64_t          prev_dts;
    int64_t           start_offset;
} output_track_t;

typedef struct
{
    char           *file_name;
    lsmash_root_t  *root;
    output_track_t *track;
    uint32_t       num_of_tracks;
    uint32_t       current_track_number;
} output_t;

typedef struct
{
    option_t opt;
    output_t output;
    input_t  input[MAX_NUM_OF_INPUTS];
    uint32_t num_of_inputs;
    uint32_t current_input_number;
} muxer_t;

static void cleanup_muxer( muxer_t *muxer )
{
    if( !muxer )
        return;
    output_t *output = &muxer->output;
    if( output->root )
        lsmash_destroy_root( output->root );
    for( uint32_t i = 0; i < output->num_of_tracks; i++ )
    {
        output_track_t *out_track = &output->track[i];
        lsmash_delete_sample( out_track->sample );
    }
    if( output->track )
        free( output->track );
    for( uint32_t i = 0; i < muxer->num_of_inputs; i++ )
    {
        input_t *input = &muxer->input[i];
        if( input->importer )
            mp4sys_importer_close( input->importer );
        for( uint32_t j = 0; j < input->num_of_tracks; j++ )
        {
            input_track_t *in_track = &input->track[j];
            if( in_track->summary )
                lsmash_cleanup_summary( in_track->summary );
        }
    }
}

static int muxer_error( muxer_t *muxer, const char *message, ... )
{
    cleanup_muxer( muxer );
    eprintf( "Error: " );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    return -1;
}

static int error_message( const char *message, ... )
{
    eprintf( "Error: " );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    return -1;
}
static void display_help( void )
{
    eprintf( "Usage: muxer [global_options] -i input1 [-i input2 -i input3 ...] -o output\n"
             "Global options:\n"
             "    --help                    Display help\n"
             "    --optimize-pd             Optimize for progressive download\n"
             "    --file-format <string>    Specify output file format\n"
             "                              Multiple file format can be specified by comma separators\n"
             "                              The first is applied as the best used one\n"
             "    --isom-version <integer>  Specify maximum compatible ISO Base Media version\n"
             "    --shift-timeline          Enable composition to decode timeline shift\n"
             "Output file formats:\n"
             "    mp4, mov, 3gp, 3g2, m4a, m4v\n"
             "Track options:\n"
             "    fps=<int/int>             Specify video framerate\n"
             "    language=<string>         Specify media language\n"
             "    alternate-group=<integer> Specify alternate group\n"
             "    sbr                       Enable backward-compatible SBR explicit signaling mode\n"
             "How to use track options:\n"
             "    -i input?[track_option1],[track_option2]...\n" );
}

static int muxer_usage_error( void )
{
    display_help();
    return -1;
}

#define MUXER_ERR( ... ) muxer_error( &muxer, __VA_ARGS__ )
#define ERROR_MSG( ... ) error_message( __VA_ARGS__ )
#define MUXER_USAGE_ERR() muxer_usage_error();
#define REFRESH_CONSOLE eprintf( "                                                                               \r" )

static int add_brand( option_t *opt, uint32_t brand )
{
    if( opt->num_of_brands > MAX_NUM_OF_BRANDS )
        return -1;
    /* Avoid duplication. */
    for( uint32_t i = 0; i < opt->num_of_brands; i++ )
        if( opt->brands[i] == brand )
            return -2;
    opt->brands[opt->num_of_brands ++] = brand;
    return 0;
}

static int setup_isom_version( option_t *opt )
{
    add_brand( opt, ISOM_BRAND_TYPE_ISOM );
    if( opt->isom_version > 6 )
        return ERROR_MSG( "unknown ISO Base Media version.\n" );
#define SET_ISOM_VERSION( version ) \
    if( opt->isom_version >= version ) \
        add_brand( opt, ISOM_BRAND_TYPE_ISO##version )
    SET_ISOM_VERSION( 2 );
    SET_ISOM_VERSION( 3 );
    SET_ISOM_VERSION( 4 );
    SET_ISOM_VERSION( 5 );
    SET_ISOM_VERSION( 6 );
#undef SET_ISOM_VERSION
    return 0;
}

static int decide_brands( option_t *opt )
{
    if( opt->num_of_brands == 0 )
    {
        /* default file format */
        opt->major_brand = ISOM_BRAND_TYPE_MP42;
        opt->minor_version = 0x00000000;
        add_brand( opt, ISOM_BRAND_TYPE_MP42 );
        add_brand( opt, ISOM_BRAND_TYPE_MP41 );
        add_brand( opt, ISOM_BRAND_TYPE_ISOM );
        eprintf( "MP4 muxing mode\n" );
        return setup_isom_version( opt );
    }
    opt->major_brand = opt->brands[0];      /* Pick the first brand as major brand. */
    for( uint32_t i = 0; i < opt->num_of_brands; i++ )
    {
        switch( opt->brands[i] )
        {
            case ISOM_BRAND_TYPE_3GP6 :
                opt->brand_3gx = 1;
                break;
            case ISOM_BRAND_TYPE_3G2A :
                /* When being compatible with 3g2a, also compatible with 3gp6. */
                add_brand( opt, ISOM_BRAND_TYPE_3GP6 );
                opt->brand_3gx = 2;
                break;
            case ISOM_BRAND_TYPE_QT :
                opt->qtff = 1;
                break;
            case ISOM_BRAND_TYPE_MP42 :
            case ISOM_BRAND_TYPE_M4A :
            case ISOM_BRAND_TYPE_M4V :
                add_brand( opt, ISOM_BRAND_TYPE_MP42 );
                add_brand( opt, ISOM_BRAND_TYPE_MP41 );
                break;
            default :
                break;
        }
        if( opt->brands[i] != ISOM_BRAND_TYPE_QT )
            opt->isom = 1;
    }
    switch( opt->major_brand )
    {
        case ISOM_BRAND_TYPE_MP42 :
            opt->minor_version = 0x00000000;
            eprintf( "MP4 muxing mode\n" );
            break;
        case ISOM_BRAND_TYPE_M4A :
        case ISOM_BRAND_TYPE_M4V :
            opt->minor_version = 0x00000000;
            eprintf( "Apple MP4 muxing mode\n" );
            break;
        case ISOM_BRAND_TYPE_3GP6 :
            opt->minor_version = 0x00000000;    /* means, 3gp(3gp6) 6.0.0 : "6" is not included in minor_version. */
            eprintf( "3GPP muxing mode\n" );
            break;
        case ISOM_BRAND_TYPE_3G2A :
            opt->minor_version = 0x00010000;    /* means, 3g2(3g2a) 1.0.0 : a == 1 */
            eprintf( "3GPP2 muxing mode\n" );
            break;
        case ISOM_BRAND_TYPE_QT :
            opt->minor_version = 0x00000000;    /* We don't know exact version of the spec to use QTFF features. */
            eprintf( "QuickTime file format muxing mode\n" );
            break;
        default :
            break;
    }
    /* Set up ISO Base Media version. */
    if( opt->isom )
        setup_isom_version( opt );
    if( opt->num_of_brands > MAX_NUM_OF_BRANDS )
        return ERROR_MSG( "exceed the maximum number of brands we can deal with.\n" );
    return 0;
}

static int parse_global_options( int argc, char **argv, muxer_t *muxer )
{
#define CHECK_NEXT_ARG if( argc == ++i ) return -1
    if( argc < 5 )
        return -1;
    uint32_t i = 1;
    option_t *opt = &muxer->opt;
    while( argc > i && *argv[i] == '-' )
    {
        if( !strcasecmp( argv[i], "-i" ) || !strcasecmp( argv[i], "--input" ) )
        {
            CHECK_NEXT_ARG;
            if( opt->num_of_inputs + 1 > MAX_NUM_OF_INPUTS )
                return ERROR_MSG( "exceed the maximum number of input files.\n" );
            input_t *input = &muxer->input[opt->num_of_inputs];
            input_option_t *input_movie_opt = &input->opt;
            char *p = argv[i];
            while( *p )
                input_movie_opt->num_of_track_delimiters += (*p++ == '?');
            if( input_movie_opt->num_of_track_delimiters > MAX_NUM_OF_TRACKS )
                return ERROR_MSG( "you specified options to exceed the maximum number of tracks per input files.\n" );
            input->file_name = strtok( argv[i], "?" );
            input_movie_opt->whole_track_option = strtok( NULL, "" );
            if( input_movie_opt->num_of_track_delimiters )
            {
                input_track_option_t *track_opt = &input->track[0].opt;
                track_opt->raws = strtok( input_movie_opt->whole_track_option, "?" );
#if (MAX_NUM_OF_TRACKS - 1)
                for( uint32_t j = 1; j < input_movie_opt->num_of_track_delimiters; j++ )
                {
                    track_opt = &input->track[j].opt;
                    track_opt->raws = strtok( NULL, "?" );
                }
#endif
            }
            ++ opt->num_of_inputs;
        }
        else if( !strcasecmp( argv[i], "-o" ) || !strcasecmp( argv[i], "--output" ) )
        {
            CHECK_NEXT_ARG;
            muxer->output.file_name = argv[i];
        }
        else if( !strcasecmp( argv[i], "-h" ) || !strcasecmp( argv[i], "--help" ) )
        {
            opt->help = 1;
            return 0;
        }
        else if( !strcasecmp( argv[i], "--optimize-pd" ) )
            opt->optimize_pd = 1;
        else if( !strcasecmp( argv[i], "--file-format" ) )
        {
            CHECK_NEXT_ARG;
            static const struct
            {
                uint32_t brand_4cc;
                char    *file_format;
            } file_format_list[]
                = {
                    { ISOM_BRAND_TYPE_MP42, "mp4" },
                    { ISOM_BRAND_TYPE_QT,   "mov" },
                    { ISOM_BRAND_TYPE_3GP6, "3gp" },
                    { ISOM_BRAND_TYPE_3G2A, "3g2" },
                    { ISOM_BRAND_TYPE_M4A,  "m4a" },
                    { ISOM_BRAND_TYPE_M4V,  "m4v" },
                    { 0, NULL }
                  };
            char *file_format = NULL;
            while( (file_format = strtok( file_format ? NULL : argv[i], "," )) != NULL )
            {
                int j;
                for( j = 0; file_format_list[j].file_format; j++ )
                    if( !strcmp( file_format, file_format_list[j].file_format ) )
                    {
                        int ret = add_brand( opt, file_format_list[j].brand_4cc );
                        if( ret == -2 )
                            return ERROR_MSG( "you specified same output file format twice.\n" );
                        else if( ret == -1 )
                            return ERROR_MSG( "exceed the maximum number of brands we can deal with.\n" );
                        break;
                    }
                if( !file_format_list[j].file_format )
                    return MUXER_USAGE_ERR();
            }
        }
        else if( !strcasecmp( argv[i], "--isom-version" ) )
        {
            CHECK_NEXT_ARG;
            if( opt->isom_version )
                return ERROR_MSG( "you specified ISO Base Media version twice.\n" );
            opt->isom_version = atoi( argv[i] );
        }
        else if( !strcasecmp( argv[i], "--shift-timeline" ) )
            opt->timeline_shift = 1;
        else
            return ERROR_MSG( "you specified invalid option: %s.\n", argv[i] );
        ++i;
    }
    if( decide_brands( opt ) )
        return ERROR_MSG( "failed to set up output file format.\n" );
    if( opt->timeline_shift && !opt->qtff && opt->isom_version < 4 )
        return ERROR_MSG( "timeline shift requires --file-format mov, or --isom-version 4 or later.\n" );
    return 0;
#undef CHECK_NEXT_ARG
}

static int parse_track_options( input_t *input )
{
    for( input->current_track_number = 1;
         input->current_track_number <= input->num_of_tracks;
         input->current_track_number ++ )
    {
        input_track_t *in_track = &input->track[input->current_track_number - 1];
        input_track_option_t *track_opt = &in_track->opt;
        if( track_opt->raws == NULL )
            break;
#if 0
        if( !strchr( track_opt->raws, ':' )
         || strchr( track_opt->raws, ':' ) == track_opt->raws )
            return ERROR_MSG( "track number is not specified in %s\n", track_opt->raws );
        if( strchr( track_opt->raws, ':' ) != strrchr( track_opt->raws, ':' ) )
            return ERROR_MSG( "multiple colons inside one track option in %s.\n", track_opt->raws );
        uint32_t track_number = atoi( strtok( track_opt->raws, ":" ) );
        if( track_number == 0 || track_number > MAX_NUM_OF_TRACKS )
            return ERROR_MSG( "%s is an invalid track number %"PRIu32".\n", strtok( track_opt->raws, ":" ), track_number );
        in_track = &input->track[track_number - 1];
        track_opt = &in_track->opt;
        char *track_option;
        while( (track_option = strtok( NULL, "," )) != NULL )
#else
        char *track_option = NULL;
        while( (track_option = strtok( track_option ? NULL : track_opt->raws, "," )) != NULL )
#endif
        {
            if( strchr( track_option, '=' ) != strrchr( track_option, '=' ) )
                return ERROR_MSG( "multiple equal signs inside one track option in %s\n", track_option );
            if( strstr( track_option, "alternate_group=" ) )
            {
                char *track_parameter = strchr( track_option, '=' ) + 1;
                track_opt->alternate_group = atoi( track_parameter );
            }
            else if( strstr( track_option, "language=" ) )
            {
                char *track_parameter = strchr( track_option, '=' ) + 1;
                track_opt->ISO_language = lsmash_pack_iso_language( track_parameter );
            }
            else if( strstr( track_option, "fps=" ) )
            {
                char *track_parameter = strchr( track_option, '=' ) + 1;
                if( sscanf( track_parameter, "%"SCNu32"/%"SCNu32, &track_opt->fps_num, &track_opt->fps_den ) == 1 )
                {
                    track_opt->fps_num = atoi( track_parameter );
                    track_opt->fps_den = 1;
                }
                track_opt->user_fps = 1;
            }
            else if( strstr( track_option, "sbr" ) )
                track_opt->sbr = 1;
            else
                return ERROR_MSG( "unknown track option %s\n", track_option );
        }
    }
    return 0;
}

static int moov_to_front_callback( void *param, uint64_t written_movie_size, uint64_t total_movie_size )
{
    eprintf( "Finalizing: [%5.2lf%%]\r", ((double)written_movie_size / total_movie_size) * 100.0 );
    return 0;
}

int main( int argc, char *argv[] )
{
    if( argc < 3 )
        return MUXER_USAGE_ERR();
    /* Parse options */
    muxer_t muxer = { { 0 } };
    option_t *opt = &muxer.opt;
    if( parse_global_options( argc, argv, &muxer ) )
        return MUXER_USAGE_ERR();
    if( opt->help )
    {
        display_help();
        cleanup_muxer( &muxer );
        return 0;
    }
    muxer.num_of_inputs = opt->num_of_inputs;
    output_t *output = &muxer.output;
    for( muxer.current_input_number = 1;
         muxer.current_input_number <= muxer.num_of_inputs;
         muxer.current_input_number ++ )
    {
        input_t *input = &muxer.input[muxer.current_input_number - 1];
        /* Initialize importer framework */
        input->importer = mp4sys_importer_open( input->file_name, "auto" );
        if( !input->importer )
            return MUXER_ERR( "failed to open input file.\n" );
        input->num_of_tracks = mp4sys_importer_get_track_count( input->importer );
        if( input->num_of_tracks == 0 )
            return MUXER_ERR( "there is no valid track in input file.\n" );
        /* Parse track options */
        if( parse_track_options( input ) )
            return ERROR_MSG( "failed to parse track options.\n" );
        /* Activate tracks by CODEC type. */
        for( input->current_track_number = 1;
             input->current_track_number <= input->num_of_tracks;
             input->current_track_number ++ )
        {
            input_track_t *in_track = &input->track[input->current_track_number - 1];
            in_track->summary = mp4sys_duplicate_summary( input->importer, input->current_track_number );
            if( !in_track->summary )
                return MUXER_ERR( "failed to get input summary.\n" );
            /* check codec type. */
            lsmash_codec_type codec_type = in_track->summary->sample_type;
            in_track->active = 1;
            switch( codec_type )
            {
                case ISOM_CODEC_TYPE_AVC1_VIDEO :
                    if( opt->isom )
                        add_brand( opt, ISOM_BRAND_TYPE_AVC1 );
                    break;
                case ISOM_CODEC_TYPE_MP4A_AUDIO :
                case ISOM_CODEC_TYPE_AC_3_AUDIO :
                case ISOM_CODEC_TYPE_EC_3_AUDIO :
                    break;
                case ISOM_CODEC_TYPE_SAWB_AUDIO :
                case ISOM_CODEC_TYPE_SAMR_AUDIO :
                    if( !opt->brand_3gx )
#if 0
                    {
                        eprintf( "Warning: the input seems AMR-NB/WB but it is available for 3GPP(2) file format.\n"
                                 "Just skip this input stream.\n" );
                        in_track->active = 0;
                    }
#else
                        return MUXER_ERR( "the input seems AMR-NB/WB but it is available for 3GPP(2) file format.\n" );
#endif
                    break;
                default :
                    lsmash_cleanup_summary( in_track->summary );
                    in_track->summary = NULL;
                    in_track->active = 0;
                    break;
            }
            input->num_of_active_tracks += in_track->active;
        }
        output->num_of_tracks += input->num_of_active_tracks;
    }
    if( output->num_of_tracks == 0 )
        return MUXER_ERR( "there is no media that can be stored in output movie.\n" );
    /* Allocate output tracks. */
    output->track = malloc( output->num_of_tracks * sizeof(output_track_t) );
    if( !output->track )
        return MUXER_ERR( "failed to allocate output tracks.\n" );
    memset( output->track, 0, output->num_of_tracks * sizeof(output_track_t) );
    /* Initialize L-SMASH muxer */
    output->root = lsmash_open_movie( output->file_name, LSMASH_FILE_MODE_WRITE );
    if( !output->root )
        return MUXER_ERR( "failed to create root.\n" );
    /* Initialize movie */
    lsmash_movie_parameters_t movie_param;
    lsmash_initialize_movie_parameters( &movie_param );
    movie_param.major_brand      = opt->major_brand;
    movie_param.brands           = opt->brands;
    movie_param.number_of_brands = opt->num_of_brands;
    movie_param.minor_version    = opt->minor_version;
    if( lsmash_set_movie_parameters( output->root, &movie_param ) )
        return MUXER_ERR( "failed to set movie parameters.\n" );
    output->current_track_number = 1;
    for( muxer.current_input_number = 1;
         muxer.current_input_number <= muxer.num_of_inputs;
         muxer.current_input_number ++ )
    {
        input_t *input = &muxer.input[muxer.current_input_number - 1];
        for( input->current_track_number = 1;
             input->current_track_number <= input->num_of_tracks;
             input->current_track_number ++ )
        {
            input_track_t *in_track = &input->track[input->current_track_number - 1];
            if( !in_track->active )
                continue;
            input_track_option_t *track_opt = &in_track->opt;
            output_track_t *out_track = &output->track[output->current_track_number - 1];
            /* Set up track parameters. */
            lsmash_track_parameters_t track_param;
            lsmash_initialize_track_parameters( &track_param );
            track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
            if( opt->qtff )
                track_param.mode |= QT_TRACK_IN_POSTER;
            track_param.alternate_group = track_opt->alternate_group;
            lsmash_media_parameters_t media_param;
            lsmash_initialize_media_parameters( &media_param );
            media_param.ISO_language = track_opt->ISO_language;
            switch( in_track->summary->stream_type )
            {
                case MP4SYS_STREAM_TYPE_VisualStream :
                {
                    out_track->track_ID = lsmash_create_track( output->root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
                    if( !out_track->track_ID )
                        return MUXER_ERR( "failed to create a track.\n" );
                    lsmash_video_summary_t *summary = (lsmash_video_summary_t *)in_track->summary;
                    uint64_t display_width  = summary->width  << 16;
                    uint64_t display_height = summary->height << 16;
                    if( summary->par_h && summary->par_v )
                    {
                        double sar = (double)summary->par_h / summary->par_h;
                        if( sar > 1.0 )
                            display_width *= sar;
                        else
                            display_height /= sar;
                    }
                    track_param.display_width  = display_width;
                    track_param.display_height = display_height;
                    /* Initialize media */
                    uint32_t timescale = 25;    /* default value */
                    uint32_t timebase  = 1;     /* default value */
                    if( track_opt->user_fps )
                    {
                        timescale = track_opt->fps_num;
                        timebase  = track_opt->fps_den;
                    }
                    else if( !summary->vfr )
                    {
                        /* H.264 maximum_fps = ceil(timescale / (2 * timebase) */
                        uint32_t compare_timescale = (summary->timescale >> 1) + (summary->timescale & 1);
                        uint32_t compare_timebase  = summary->timebase;
                        static const struct
                        {
                            uint32_t timescale;
                            uint32_t timebase;
                        } well_known_fps[]
                            = {
                                { 24000, 1001 }, { 30000, 1001 }, { 60000, 1001 }, { 120000, 1001 }, { 72000, 1001 },
                                { 25, 1 }, { 50, 1 }, { 24, 1 }, { 30, 1 }, { 60, 1 }, { 120, 1 }, { 72, 1 }, { 0, 0 }
                              };
                        for( int i = 0; well_known_fps[i].timescale; i++ )
                            if( well_known_fps[i].timescale == compare_timescale
                             && well_known_fps[i].timebase  == compare_timebase )
                            {
                                timescale = well_known_fps[i].timescale;
                                timebase  = well_known_fps[i].timebase;
                                break;
                            }
                    }
                    media_param.timescale          = timescale;
                    media_param.media_handler_name = "L-SMASH Video Handler";
                    media_param.roll_grouping      = 1;
                    media_param.rap_grouping       = opt->isom_version >= 6;
                    out_track->timescale = timescale;
                    out_track->timebase  = timebase;
                    break;
                }
                case MP4SYS_STREAM_TYPE_AudioStream :
                {
                    out_track->track_ID = lsmash_create_track( output->root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
                    if( !out_track->track_ID )
                        return MUXER_ERR( "failed to create a track.\n" );
                    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)in_track->summary;
                    if( track_opt->sbr )
                    {
                        if( summary->object_type_indication != MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3 )
                            return MUXER_ERR( "--sbr is only valid with MPEG-4 Audio.\n" );
                        summary->sbr_mode = MP4A_AAC_SBR_BACKWARD_COMPATIBLE;
                        if( lsmash_setup_AudioSpecificConfig( summary ) )
                            return MUXER_ERR( "failed to set SBR mode.\n" );
                    }
                    media_param.timescale          = summary->frequency;
                    media_param.media_handler_name = "L-SMASH Audio Handler";
                    out_track->timescale = summary->frequency;
                    out_track->timebase  = 1;
                    break;
                }
                default :
                    return MUXER_ERR( "not supported stream type.\n" );
            }
            if( lsmash_set_track_parameters( output->root, out_track->track_ID, &track_param ) )
                return MUXER_ERR( "failed to set track parameters.\n" );
            if( lsmash_set_media_parameters( output->root, out_track->track_ID, &media_param ) )
                return MUXER_ERR( "failed to set media parameters.\n" );
            out_track->summary = in_track->summary;
            out_track->sample_entry = lsmash_add_sample_entry( output->root, out_track->track_ID, out_track->summary->sample_type, out_track->summary );
            if( !out_track->sample_entry )
                return MUXER_ERR( "failed to add sample_entry.\n" );
            out_track->active = 1;
            ++ output->current_track_number;
        }
        input->current_track_number = 1;
    }
    output->current_track_number = 1;
    muxer.current_input_number = 1;
    /* Start muxing. */
    double   largest_dts = 0;
    uint32_t num_consecutive_sample_skip = 0;
    uint32_t num_active_input_tracks = output->num_of_tracks;
    uint64_t total_media_size = 0;
    uint8_t  sample_count = 0;
    while( 1 )
    {
        input_t *input = &muxer.input[muxer.current_input_number - 1];
        output_track_t *out_track = &output->track[output->current_track_number - 1];
        if( out_track->active )
        {
            lsmash_sample_t *sample = out_track->sample;
            /* Get a new sample data if the track doesn't hold any one. */
            if( !sample )
            {
                /* Allocate sample buffer. */
                sample = lsmash_create_sample( out_track->summary->max_au_length );
                if( !sample )
                    return MUXER_ERR( "failed to alloc memory for buffer.                                             \n" );
                /* FIXME: mp4sys_importer_get_access_unit() returns 1 if there're any changes in stream's properties.
                 * If you want to support them, you have to retrieve summary again, and make some operation accordingly. */
                if( mp4sys_importer_get_access_unit( input->importer, input->current_track_number, sample ) )
                {
                    lsmash_delete_sample( sample );
                    ERROR_MSG( "failed to get a frame from input file. Maybe corrupted.                        \n"
                               "Aborting muxing operation and trying to let output be valid file.\n" );
                    break;
                }
                if( sample->length == 0 )
                {
                    /* No more appendable samples in this track. */
                    lsmash_delete_sample( sample );
                    sample = NULL;
                    out_track->active = 0;
                    if( --num_active_input_tracks == 0 )
                        break;      /* Reached the end of whole tracks. */
                }
                else
                {
                    sample->index = out_track->sample_entry;
                    sample->dts *= out_track->timebase;
                    sample->cts *= out_track->timebase;
                    if( opt->timeline_shift )
                    {
                        if( out_track->current_sample_number == 0 )
                            out_track->ctd_shift = sample->cts;
                        sample->cts -= out_track->ctd_shift;
                    }
                    out_track->sample = sample;
                }
            }
            if( sample )
            {
                /* Append a sample if meeting a condition. */
                if( ((double)sample->dts / out_track->timescale) <= largest_dts
                 || num_consecutive_sample_skip == num_active_input_tracks )
                {
                    uint64_t sample_size = sample->length;      /* sample might be deleted internally after appending. */
                    if( lsmash_append_sample( output->root, out_track->track_ID, sample ) )
                        return MUXER_ERR( "failed to append a sample.                                                     \n" );
                    if( out_track->current_sample_number == 0 )
                        out_track->start_offset = sample->cts;
                    else
                        out_track->last_delta = sample->dts - out_track->prev_dts;
                    largest_dts = LSMASH_MAX( largest_dts, (double)sample->dts / out_track->timescale );
                    out_track->prev_dts = sample->dts;
                    out_track->sample = NULL;
                    total_media_size += sample_size;
                    ++ out_track->current_sample_number;
                    num_consecutive_sample_skip = 0;
                    /* Print, per 256 samples, total size of imported media. */
                    if( ++sample_count == 0 )
                        eprintf( "Importing: %"PRIu64" bytes\r", total_media_size );
                }
                else
                    ++num_consecutive_sample_skip;      /* Skip appendig sample. */
            }
        }
        if( ++ output->current_track_number > output->num_of_tracks )
            output->current_track_number = 1;       /* Back the first output track. */
        /* Move the next track. */
        if( ++ input->current_track_number > input->num_of_tracks )
        {
            /* Move the next input movie. */
            input->current_track_number = 1;
            if( ++ muxer.current_input_number > muxer.num_of_inputs )
                muxer.current_input_number = 1;        /* Back the first input movie. */
        }
    }
    for( output->current_track_number = 1;
         output->current_track_number <= output->num_of_tracks;
         output->current_track_number ++ )
    {
        /* Close track. */
        output_track_t *out_track = &output->track[output->current_track_number - 1];
        if( lsmash_flush_pooled_samples( output->root, out_track->track_ID, out_track->last_delta ) )
            ERROR_MSG( "failed to flush the rest of samples.                                           \n" );
        /* Create edit list.
         * segment_duration == 0 means an appropriate one will be applied. */
        if( lsmash_create_explicit_timeline_map( output->root, out_track->track_ID, 0, out_track->start_offset, ISOM_EDIT_MODE_NORMAL ) )
            ERROR_MSG( "failed to set timeline map                                                    .\n" );
    }
    /* Close movie. */
    lsmash_adhoc_remux_t *finalize;
    if( opt->optimize_pd )
    {
        lsmash_adhoc_remux_t moov_to_front;
        moov_to_front.func = moov_to_front_callback;
        moov_to_front.buffer_size = 4*1024*1024;
        moov_to_front.param = NULL;
        finalize = &moov_to_front;
    }
    else
        finalize = NULL;
    REFRESH_CONSOLE;
    if( lsmash_finish_movie( output->root, finalize ) )
        ERROR_MSG( "failed to finish movie.                                                        \n" );
    cleanup_muxer( &muxer );        /* including lsmash_destroy_root() */
    eprintf( "Muxing completed!                                                              \n" );
    return 0;
}
