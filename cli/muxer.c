/*****************************************************************************
 * muxer.c:
 *****************************************************************************
 * Copyright (C) 2010-2015 L-SMASH project
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

#include "cli.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "importer/importer.h"

#define MAX_NUM_OF_BRANDS 50
#define MAX_NUM_OF_INPUTS 10
#define MAX_NUM_OF_TRACKS 1

typedef struct
{
    char    *album_name;
    char    *artist;
    char    *comment;
    char    *release_date;
    char    *encoder;
    char    *genre;
    char    *lyrics;
    char    *title;
    char    *composer;
    char    *album_artist;
    char    *copyright;
    char    *description;
    char    *grouping;
    uint32_t beats_per_minute;
} itunes_metadata_t;

typedef struct
{
    int      help;
    int      version;
    int      isom;
    int      isom_version;
    int      itunes_movie;
    int      qtff;
    int      brand_3gx;
    int      optimize_pd;
    int      timeline_shift;
    uint32_t interleave;
    uint32_t num_of_brands;
    uint32_t brands[MAX_NUM_OF_BRANDS];
    uint32_t major_brand;
    uint32_t minor_version;
    uint32_t num_of_inputs;
    uint32_t chap_track;
    char    *chap_file;
    int      add_bom_to_chpl;
    char    *copyright_notice;
    uint16_t copyright_language;
    itunes_metadata_t itunes_metadata;
    uint16_t default_language;
} option_t;

typedef struct
{
    char    *raws;
    int      disable;
    int      sbr;
    int      user_fps;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t encoder_delay;
    int16_t  alternate_group;
    uint16_t ISO_language;
    uint16_t copyright_language;
    char    *copyright_notice;
    char    *handler_name;
} input_track_option_t;

typedef struct
{
    lsmash_summary_t    *summary;
    input_track_option_t opt;
    int                  active;
    int                  lpcm;
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
    importer_t        *importer;
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
    uint32_t          priming_samples;
    uint32_t          last_delta;
    uint64_t          prev_dts;
    int64_t           start_offset;
    double            dts;
    int               lpcm;
} output_track_t;

typedef struct
{
    output_track_t *track;
    uint32_t        num_of_tracks;
    uint32_t        current_track_number;
} output_movie_t;

typedef struct
{
    char                    *name;
    lsmash_file_t           *fh;
    lsmash_file_parameters_t param;
    output_movie_t           movie;
} output_file_t;

typedef struct
{
    lsmash_root_t  *root;
    output_file_t   file;
} output_t;

typedef struct
{
    option_t opt;
    output_t output;
    input_t  input[MAX_NUM_OF_INPUTS];
    uint32_t num_of_inputs;
} muxer_t;

static void cleanup_muxer( muxer_t *muxer )
{
    if( !muxer )
        return;
    output_t *output = &muxer->output;
    lsmash_close_file( &output->file.param );
    lsmash_destroy_root( output->root );
    if( output->file.movie.track )
    {
        for( uint32_t i = 0; i < output->file.movie.num_of_tracks; i++ )
        {
            output_track_t *out_track = &output->file.movie.track[i];
            lsmash_delete_sample( out_track->sample );
        }
        lsmash_free( output->file.movie.track );
    }
    for( uint32_t i = 0; i < muxer->num_of_inputs; i++ )
    {
        input_t *input = &muxer->input[i];
        lsmash_importer_close( input->importer );
        for( uint32_t j = 0; j < input->num_of_tracks; j++ )
            lsmash_cleanup_summary( input->track[j].summary );
    }
}

#define eprintf( ... ) fprintf( stderr, __VA_ARGS__ )
#define REFRESH_CONSOLE eprintf( "                                                                               \r" )

static int muxer_error( muxer_t *muxer, const char *message, ... )
{
    cleanup_muxer( muxer );
    REFRESH_CONSOLE;
    eprintf( "Error: " );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    return -1;
}

static int error_message( const char *message, ... )
{
    REFRESH_CONSOLE;
    eprintf( "Error: " );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    return -1;
}

static void display_version( void )
{
    eprintf( "\n"
             "L-SMASH isom/mov multiplexer rev%s  %s\n"
             "Built on %s %s\n"
             "Copyright (C) 2010-2014 L-SMASH project\n",
             LSMASH_REV, LSMASH_GIT_HASH, __DATE__, __TIME__ );
}

static void display_help( void )
{
    display_version();
    eprintf( "\n"
             "Usage: muxer [global_options] -i input1 [-i input2 -i input3 ...] -o output\n"
             "Global options:\n"
             "    --help                    Display help\n"
             "    --version                 Display version information\n"
             "    --optimize-pd             Optimize for progressive download\n"
             "    --interleave <integer>    Specify time interval for media interleaving in milliseconds\n"
             "    --file-format <string>    Specify output file format\n"
             "                              Multiple file format can be specified by comma separators\n"
             "                              The first is applied as the best used one\n"
             "    --isom-version <integer>  Specify maximum compatible ISO Base Media version\n"
             "    --shift-timeline          Enable composition to decode timeline shift\n"
             "    --chapter <string>        Set chapters from the file.\n"
             "    --chpl-with-bom           Add UTF-8 BOM to the chapter strings\n"
             "                              in the chapter list. (experimental)\n"
             "    --chapter-track <integer> Set which track the chapter applies to.\n"
             "                              This option takes effect only when reference\n"
             "                              chapter is available.\n"
             "                              If this option is not used, it defaults to 1.\n"
             "    --copyright-notice <arg>  Specify copyright notice with or without language (latter string)\n"
             "                                  <arg> is <string> or <string>/<string>\n"
             "    --language <string>       Specify the default language for all the output tracks.\n"
             "                              This option is overridden by the track options.\n"
             "Output file formats:\n"
             "    mp4, mov, 3gp, 3g2, m4a, m4v\n"
             "\n"
             "Track options:\n"
             "    disable                   Disable this track\n"
             "    fps=<arg>                 Specify video framerate\n"
             "                                  <arg> is <integer> or <integer>/<integer>\n"
             "    language=<string>         Specify media language\n"
             "    alternate-group=<integer> Specify alternate group\n"
             "    encoder-delay=<integer>   Represent audio encoder delay (priming samples) explicitly\n"
             "    copyright=<arg>           Specify copyright notice with or without language (latter string)\n"
             "                                  <arg> is <string> or <string>/<string>\n"
             "    handler=<string>          Set media handler name\n"
             "    sbr                       Enable backward-compatible SBR explicit signaling mode\n"
             "How to use track options:\n"
             "    -i input?[track_option1],[track_option2]...\n"
             "\n"
             "iTunes Metadata:\n"
             "    --album-name <string>     Album name\n"
             "    --artist <string>         Artist\n"
             "    --comment <string>        User comment\n"
             "    --release-date <string>   Release date (YYYY-MM-DD)\n"
             "    --encoder <string>        Person or company that encoded the recording\n"
             "    --genre <string>          Genre\n"
             "    --lyrics <string>         Lyrics\n"
             "    --title <string>          Title or song name\n"
             "    --composer <string>       Composer\n"
             "    --album-artist <string>   Artist for the whole album (if different than the individual tracks)\n"
             "    --copyright <string>      Copyright\n"
             "    --description <string>    Description\n"
             "    --grouping <string>       Grouping\n"
             "    --tempo <integer>         Beats per minute\n" );
}

static int muxer_usage_error( void )
{
    display_help();
    return -1;
}

#define MUXER_ERR( ... ) muxer_error( &muxer, __VA_ARGS__ )
#define ERROR_MSG( ... ) error_message( __VA_ARGS__ )
#define MUXER_USAGE_ERR() muxer_usage_error();

static int add_brand( option_t *opt, uint32_t brand )
{
    if( opt->num_of_brands >= MAX_NUM_OF_BRANDS )
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
        opt->isom = 1;
        eprintf( "MP4 muxing mode\n" );
        return setup_isom_version( opt );
    }
    opt->major_brand = opt->brands[0];      /* Pick the first brand as major brand. */
    for( uint32_t i = 0; i < opt->num_of_brands; i++ )
    {
        switch( opt->brands[i] )
        {
            case ISOM_BRAND_TYPE_3GP6 :
                /* When being compatible with 3gp6, also compatible with 3g2a. */
                add_brand( opt, ISOM_BRAND_TYPE_3G2A );
                opt->brand_3gx = 1;
                break;
            case ISOM_BRAND_TYPE_3G2A :
                opt->brand_3gx = 2;
                break;
            case ISOM_BRAND_TYPE_QT :
                opt->qtff = 1;
                break;
            case ISOM_BRAND_TYPE_M4A :
            case ISOM_BRAND_TYPE_M4V :
                opt->itunes_movie = 1;
                /* fall-through */
            case ISOM_BRAND_TYPE_MP42 :
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
            eprintf( "iTunes MP4 muxing mode\n" );
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
    if ( argc < 2 )
        return -1;
    else if( !strcasecmp( argv[1], "-h" ) || !strcasecmp( argv[1], "--help" ) )
    {
        muxer->opt.help = 1;
        return 0;
    }
    else if( !strcasecmp( argv[1], "-v" ) || !strcasecmp( argv[1], "--version" ) )
    {
        muxer->opt.version = 1;
        return 0;
    }
    else if( argc < 5 )
        return -1;
    uint32_t i = 1;
    option_t *opt = &muxer->opt;
    opt->chap_track = 1;
    opt->add_bom_to_chpl = 0;
    while( argc > i && *argv[i] == '-' )
    {
#define CHECK_NEXT_ARG if( argc == ++i ) return -1
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
            muxer->output.file.name = argv[i];
        }
        else if( !strcasecmp( argv[i], "--optimize-pd" ) )
            opt->optimize_pd = 1;
        else if( !strcasecmp( argv[i], "--interleave" ) )
        {
            CHECK_NEXT_ARG;
            if( opt->interleave )
                return ERROR_MSG( "you specified --interleave twice.\n" );
            opt->interleave = atoi( argv[i] );
        }
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
                return ERROR_MSG( "you specified --isom-version twice.\n" );
            opt->isom_version = atoi( argv[i] );
        }
        else if( !strcasecmp( argv[i], "--shift-timeline" ) )
            opt->timeline_shift = 1;
        else if( !strcasecmp( argv[i], "--chapter" ) )
        {
            CHECK_NEXT_ARG;
            opt->chap_file = argv[i];
        }
        else if( !strcasecmp( argv[i], "--chapter-track" ) )
        {
            CHECK_NEXT_ARG;
            opt->chap_track = atoi( argv[i] );
            if( !opt->chap_track )
                return ERROR_MSG( "%s is an invalid track number.\n", argv[i] );
        }
        else if( !strcasecmp( argv[i], "--chpl-with-bom" ) )
            opt->add_bom_to_chpl = 1;
        else if( !strcasecmp( argv[i], "--copyright-notice" ) )
        {
            CHECK_NEXT_ARG;
            if( opt->copyright_notice )
                return ERROR_MSG( "you specified --copyright-notice twice.\n" );
            opt->copyright_notice = argv[i];
            char *language = opt->copyright_notice;
            while( *language )
            {
                if( *language == '/' )
                {
                    *language++ = '\0';
                    break;
                }
                ++language;
            }
            opt->copyright_language = language ? lsmash_pack_iso_language( language ) : ISOM_LANGUAGE_CODE_UNDEFINED;
        }
        /* iTunes metadata */
#define CHECK_ITUNES_METADATA_ARG_STRING( argument, value ) \
        else if( !strcasecmp( argv[i], "--"#argument ) ) \
        { \
            CHECK_NEXT_ARG; \
            if( opt->itunes_metadata.value ) \
                return ERROR_MSG( "you specified --"#argument" twice.\n" ); \
            opt->itunes_metadata.value = argv[i]; \
        }
        CHECK_ITUNES_METADATA_ARG_STRING( album-name,   album_name )
        CHECK_ITUNES_METADATA_ARG_STRING( artist,       artist )
        CHECK_ITUNES_METADATA_ARG_STRING( comment,      comment )
        CHECK_ITUNES_METADATA_ARG_STRING( release-date, release_date )
        CHECK_ITUNES_METADATA_ARG_STRING( encoder,      encoder )
        CHECK_ITUNES_METADATA_ARG_STRING( genre,        genre )
        CHECK_ITUNES_METADATA_ARG_STRING( lyrics,       lyrics )
        CHECK_ITUNES_METADATA_ARG_STRING( title,        title )
        CHECK_ITUNES_METADATA_ARG_STRING( composer,     composer )
        CHECK_ITUNES_METADATA_ARG_STRING( album-artist, album_artist )
        CHECK_ITUNES_METADATA_ARG_STRING( copyright,    copyright )
        CHECK_ITUNES_METADATA_ARG_STRING( description,  description )
        CHECK_ITUNES_METADATA_ARG_STRING( grouping,     grouping )
#undef CHECK_ITUNES_METADATA_ARG_STRING
        else if( !strcasecmp( argv[i], "--tempo" ) )
        {
            CHECK_NEXT_ARG;
            if( opt->itunes_metadata.beats_per_minute )
                return ERROR_MSG( "you specified --tempo twice.\n" );
            opt->itunes_metadata.beats_per_minute = atoi( argv[i] );
        }
        else if( !strcasecmp( argv[i], "--language" ) )
        {
            CHECK_NEXT_ARG;
            opt->default_language = lsmash_pack_iso_language( argv[i] );
        }
#undef CHECK_NEXT_ARG
        else
            return ERROR_MSG( "you specified invalid option: %s.\n", argv[i] );
        ++i;
    }
    if( !muxer->output.file.name )
        return ERROR_MSG( "output file name is not specified.\n" );
    if( decide_brands( opt ) )
        return ERROR_MSG( "failed to set up output file format.\n" );
    if( opt->timeline_shift && !opt->qtff && opt->isom_version < 4 )
        return ERROR_MSG( "timeline shift requires --file-format mov, or --isom-version 4 or later.\n" );
    muxer->num_of_inputs = opt->num_of_inputs;
    return 0;
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
            if( strstr( track_option, "disable" ) )
                track_opt->disable = 1;
            else if( strstr( track_option, "alternate-group=" ) )
            {
                char *track_parameter = strchr( track_option, '=' ) + 1;
                track_opt->alternate_group = atoi( track_parameter );
            }
            else if( strstr( track_option, "encoder-delay=" ) )
            {
                char *track_parameter = strchr( track_option, '=' ) + 1;
                track_opt->encoder_delay = atoi( track_parameter );
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
            else if( strstr( track_option, "copyright=" ) )
            {
                char *track_parameter = strchr( track_option, '=' ) + 1;
                track_opt->copyright_notice = track_parameter;
                while( *track_parameter )
                {
                    if( *track_parameter == '/' )
                    {
                        *track_parameter++ = '\0';
                        break;
                    }
                    ++track_parameter;
                }
                track_opt->copyright_language = lsmash_pack_iso_language( track_parameter );
            }
            else if( strstr( track_option, "handler=" ) )
            {
                char *track_parameter = strchr( track_option, '=' ) + 1;
                track_opt->handler_name = track_parameter;
            }
            else if( strstr( track_option, "sbr" ) )
                track_opt->sbr = 1;
            else
                return ERROR_MSG( "unknown track option %s\n", track_option );
        }
    }
    return 0;
}

static void display_codec_name( lsmash_codec_type_t codec_type, uint32_t track_number )
{
#define DISPLAY_CODEC_NAME( codec, codec_name ) \
    else if( lsmash_check_codec_type_identical( codec_type, codec ) ) \
        eprintf( "Track %"PRIu32": "#codec_name"\n", track_number )
    if( 0 );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_AVC1_VIDEO, H.264 Advanced Video Coding );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_HVC1_VIDEO, H.265 High Efficiency Video Coding );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_VC_1_VIDEO, SMPTE VC-1 Advanced Profile );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_MP4A_AUDIO, MPEG-4 Audio );
    DISPLAY_CODEC_NAME(   QT_CODEC_TYPE_MP4A_AUDIO, MPEG-4 Audio );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_AC_3_AUDIO, AC-3 );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_EC_3_AUDIO, Enhanced AC-3 );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_DTSC_AUDIO, DTS );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_DTSE_AUDIO, DTS LBR );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_DTSH_AUDIO, DTS-HD );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_DTSL_AUDIO, DTS-HD Lossless );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_SAWB_AUDIO, Wideband AMR voice );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_SAMR_AUDIO, Narrowband AMR voice );
    DISPLAY_CODEC_NAME(   QT_CODEC_TYPE_LPCM_AUDIO, Uncompressed Audio );
#undef DISPLAY_CODEC_NAME
}

static int open_input_files( muxer_t *muxer )
{
    output_movie_t *out_movie = &muxer->output.file.movie;
    option_t       *opt       = &muxer->opt;
    for( uint32_t current_input_number = 1; current_input_number <= muxer->num_of_inputs; current_input_number++ )
    {
        input_t *input = &muxer->input[current_input_number - 1];
        /* Initialize importer framework. */
        input->importer = lsmash_importer_open( input->file_name, "auto" );
        if( !input->importer )
            return ERROR_MSG( "failed to open input file.\n" );
        input->num_of_tracks = lsmash_importer_get_track_count( input->importer );
        if( input->num_of_tracks == 0 )
            return ERROR_MSG( "there is no valid track in input file.\n" );
        if( opt->default_language )
             for( int i = 0; i < input->num_of_tracks; i ++ )
                 input->track[i].opt.ISO_language = opt->default_language;
        /* Parse track options */
        if( parse_track_options( input ) )
            return ERROR_MSG( "failed to parse track options.\n" );
        /* Activate tracks by CODEC type. */
        for( input->current_track_number = 1;
             input->current_track_number <= input->num_of_tracks;
             input->current_track_number ++ )
        {
            input_track_t *in_track = &input->track[input->current_track_number - 1];
            int err = lsmash_importer_construct_timeline( input->importer, input->current_track_number );
            if( err < 0 && err != LSMASH_ERR_PATCH_WELCOME )
            {
                in_track->active = 0;
                continue;
            }
            in_track->summary = lsmash_duplicate_summary( input->importer, input->current_track_number );
            if( !in_track->summary )
                return ERROR_MSG( "failed to get input summary.\n" );
            /* Check codec type. */
            lsmash_codec_type_t codec_type = in_track->summary->sample_type;
            in_track->active = 1;
            if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_AVC1_VIDEO ) )
            {
                if( opt->isom )
                    add_brand( opt, ISOM_BRAND_TYPE_AVC1 );
            }
            else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_HVC1_VIDEO ) )
            {
                if( !opt->isom && opt->qtff )
                    return ERROR_MSG( "the input seems HEVC, at present available only for ISO Base Media file format.\n" );
            }
            else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_VC_1_VIDEO ) )
            {
                if( !opt->isom && opt->qtff )
                    return ERROR_MSG( "the input seems VC-1, at present available only for ISO Base Media file format.\n" );
            }
            else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_MP4A_AUDIO )
                  || lsmash_check_codec_type_identical( codec_type,   QT_CODEC_TYPE_MP4A_AUDIO ) )
                /* Do nothing. */;
            else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_AC_3_AUDIO )
                  || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_EC_3_AUDIO ) )
            {
                if( !opt->isom && opt->qtff )
                    return ERROR_MSG( "the input seems (Enhanced) AC-3, at present available only for ISO Base Media file format.\n" );
                add_brand( opt, ISOM_BRAND_TYPE_DBY1 );
            }
            else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_DTSC_AUDIO )
                  || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_DTSE_AUDIO )
                  || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_DTSH_AUDIO )
                  || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_DTSL_AUDIO ) )
            {
                if( !opt->isom && opt->qtff )
                    return ERROR_MSG( "the input seems DTS(-HD) Audio, at present available only for ISO Base Media file format.\n" );
            }
            else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_SAWB_AUDIO )
                  || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_SAMR_AUDIO ) )
            {
                if( !opt->brand_3gx )
                    return ERROR_MSG( "the input seems AMR-NB/WB, available for 3GPP(2) file format.\n" );
            }
            else if( lsmash_check_codec_type_identical( codec_type, QT_CODEC_TYPE_LPCM_AUDIO ) )
            {
                if( opt->isom && !opt->qtff )
                    return ERROR_MSG( "the input seems Uncompressed Audio, at present available only for QuickTime file format.\n" );
                in_track->lpcm = 1;
            }
            else
            {
                lsmash_cleanup_summary( in_track->summary );
                in_track->summary = NULL;
                in_track->active = 0;
            }
            if( in_track->active )
            {
                ++ input->num_of_active_tracks;
                display_codec_name( codec_type, out_movie->num_of_tracks + input->num_of_active_tracks );
            }
        }
        out_movie->num_of_tracks += input->num_of_active_tracks;
    }
    if( out_movie->num_of_tracks == 0 )
        return ERROR_MSG( "there is no media that can be stored in output movie.\n" );
    return 0;
}

static int set_itunes_metadata( output_t *output, option_t *opt )
{
    if( !opt->itunes_movie )
        return 0;
    itunes_metadata_t *metadata = &opt->itunes_metadata;
#define SET_ITUNES_METADATA( item, type, value ) \
    if( value \
     && lsmash_set_itunes_metadata( output->root, (lsmash_itunes_metadata_t){ item, ITUNES_METADATA_TYPE_NONE, { .type = value }, NULL, NULL } ) ) \
        return -1
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_ENCODING_TOOL,    string,  "L-SMASH" );
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_ALBUM_NAME,       string,  metadata->album_name );
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_ARTIST,           string,  metadata->artist );
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_USER_COMMENT,     string,  metadata->comment );
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_RELEASE_DATE,     string,  metadata->release_date );
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_ENCODED_BY,       string,  metadata->encoder );
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_USER_GENRE,       string,  metadata->genre );
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_LYRICS,           string,  metadata->lyrics );
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_TITLE,            string,  metadata->title );
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_COMPOSER,         string,  metadata->composer );
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_ALBUM_ARTIST,     string,  metadata->album_artist );
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_COPYRIGHT,        string,  metadata->copyright );
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_DESCRIPTION,      string,  metadata->description );
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_GROUPING,         string,  metadata->grouping );
    SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_BEATS_PER_MINUTE, integer, metadata->beats_per_minute );
#undef SET_ITUNES_METADATA
    return 0;
}

static int prepare_output( muxer_t *muxer )
{
    option_t       *opt       = &muxer->opt;
    output_t       *output    = &muxer->output;
    output_file_t  *out_file  = &output->file;
    output_movie_t *out_movie = &out_file->movie;
    /* Allocate output tracks. */
    out_movie->track = lsmash_malloc( out_movie->num_of_tracks * sizeof(output_track_t) );
    if( !out_movie->track )
        return ERROR_MSG( "failed to allocate output tracks.\n" );
    memset( out_movie->track, 0, out_movie->num_of_tracks * sizeof(output_track_t) );
    /* Initialize L-SMASH muxer */
    output->root = lsmash_create_root();
    if( !output->root )
        return ERROR_MSG( "failed to create a ROOT.\n" );
    lsmash_file_parameters_t *file_param = &out_file->param;
    if( lsmash_open_file( out_file->name, 0, file_param ) < 0 )
        return ERROR_MSG( "failed to open an output file.\n" );
    file_param->major_brand   = opt->major_brand;
    file_param->brands        = opt->brands;
    file_param->brand_count   = opt->num_of_brands;
    file_param->minor_version = opt->minor_version;
    if( opt->interleave )
        file_param->max_chunk_duration = opt->interleave * 1e-3;
    out_file->fh = lsmash_set_file( output->root, file_param );
    if( !out_file->fh )
        return ERROR_MSG( "failed to add an output file into a ROOT.\n" );
    /* Initialize movie */
    lsmash_movie_parameters_t movie_param;
    lsmash_initialize_movie_parameters( &movie_param );
    if( lsmash_set_movie_parameters( output->root, &movie_param ) )
        return ERROR_MSG( "failed to set movie parameters.\n" );
    if( opt->copyright_notice
     && lsmash_set_copyright( output->root, 0, opt->copyright_language, opt->copyright_notice ) )
        return ERROR_MSG( "failed to set a copyright notice for the entire movie.\n" );
    if( set_itunes_metadata( output, opt ) )
        return ERROR_MSG( "failed to set iTunes metadata.\n" );
    out_movie->current_track_number = 1;
    for( uint32_t current_input_number = 1; current_input_number <= muxer->num_of_inputs; current_input_number++ )
    {
        input_t *input = &muxer->input[current_input_number - 1];
        for( input->current_track_number = 1;
             input->current_track_number <= input->num_of_tracks;
             input->current_track_number ++ )
        {
            input_track_t *in_track = &input->track[ input->current_track_number - 1 ];
            if( !in_track->active )
                continue;
            input_track_option_t *track_opt = &in_track->opt;
            output_track_t *out_track = &out_movie->track[ out_movie->current_track_number - 1 ];
            /* Set up track parameters. */
            lsmash_track_parameters_t track_param;
            lsmash_initialize_track_parameters( &track_param );
            track_param.mode = ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
            if( !track_opt->disable )
                track_param.mode |= ISOM_TRACK_ENABLED;
            if( opt->qtff )
                track_param.mode |= QT_TRACK_IN_POSTER;
            track_param.alternate_group = track_opt->alternate_group;
            lsmash_media_parameters_t media_param;
            lsmash_initialize_media_parameters( &media_param );
            media_param.ISO_language = track_opt->ISO_language;
            switch( in_track->summary->summary_type )
            {
                case LSMASH_SUMMARY_TYPE_VIDEO :
                {
                    out_track->track_ID = lsmash_create_track( output->root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
                    if( !out_track->track_ID )
                        return ERROR_MSG( "failed to create a track.\n" );
                    lsmash_video_summary_t *summary = (lsmash_video_summary_t *)in_track->summary;
                    uint64_t display_width  = (uint64_t)summary->width  << 16;
                    uint64_t display_height = (uint64_t)summary->height << 16;
                    if( summary->par_h && summary->par_v )
                    {
                        double sar = (double)summary->par_h / summary->par_v;
                        if( sar > 1.0 )
                            display_width *= sar;
                        else
                            display_height /= sar;
                    }
                    track_param.display_width  = display_width  <= UINT32_MAX ? display_width  : UINT32_MAX;
                    track_param.display_height = display_height <= UINT32_MAX ? display_height : UINT32_MAX;
                    /* Initialize media */
                    uint32_t timescale = 25;    /* default value */
                    uint32_t timebase  = 1;     /* default value */
                    if( track_opt->user_fps )
                    {
                        timescale = track_opt->fps_num << (!!summary->sample_per_field);
                        timebase  = track_opt->fps_den;
                    }
                    else if( !summary->vfr )
                    {
                        if( lsmash_check_codec_type_identical( summary->sample_type, ISOM_CODEC_TYPE_AVC1_VIDEO )
                         || lsmash_check_codec_type_identical( summary->sample_type, ISOM_CODEC_TYPE_HVC1_VIDEO ) )
                        {
                            uint32_t compare_timebase  = summary->timebase;
                            uint32_t compare_timescale = summary->timescale;
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
                            lsmash_codec_specific_t *bitrate = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_H264_BITRATE,
                                                                                                  LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
                            if( bitrate )
                                lsmash_add_codec_specific_data( in_track->summary, bitrate );
                            lsmash_destroy_codec_specific_data( bitrate );
                        }
                        else
                        {
                            timescale = summary->timescale;
                            timebase  = summary->timebase;
                        }
                    }
                    media_param.timescale          = timescale;
                    media_param.media_handler_name = track_opt->handler_name ? track_opt->handler_name : "L-SMASH Video Handler";
                    media_param.roll_grouping      = 1;
                    media_param.rap_grouping       = opt->isom_version >= 6;
                    out_track->timescale = timescale;
                    out_track->timebase  = timebase;
                    break;
                }
                case LSMASH_SUMMARY_TYPE_AUDIO :
                {
                    out_track->track_ID = lsmash_create_track( output->root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
                    if( !out_track->track_ID )
                        return ERROR_MSG( "failed to create a track.\n" );
                    lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)in_track->summary;
                    if( track_opt->sbr )
                    {
                        /* Check if explicit SBR is valid or not. */
                        if( lsmash_mp4sys_get_object_type_indication( (lsmash_summary_t *)summary ) != MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3 )
                            return ERROR_MSG( "--sbr is only valid with MPEG-4 Audio.\n" );
                        summary->sbr_mode = MP4A_AAC_SBR_BACKWARD_COMPATIBLE;
                        if( lsmash_setup_AudioSpecificConfig( summary ) )
                            return ERROR_MSG( "failed to set SBR mode.\n" );
                    }
                    media_param.timescale          = summary->frequency;
                    media_param.media_handler_name = track_opt->handler_name ? track_opt->handler_name : "L-SMASH Audio Handler";
                    media_param.roll_grouping      = (opt->isom_version >= 2 || (opt->qtff && !in_track->lpcm));
                    out_track->priming_samples = track_opt->encoder_delay;
                    out_track->timescale       = summary->frequency;
                    out_track->timebase        = 1;
                    out_track->lpcm            = in_track->lpcm;
                    break;
                }
                default :
                    return ERROR_MSG( "not supported stream type.\n" );
            }
            /* Reset the movie timescale in order to match the media timescale if only one track is there. */
            if( muxer->num_of_inputs        == 1
             && current_input_number        == 1
             && input->current_track_number == 1 )
            {
                movie_param.timescale = media_param.timescale;
                if( lsmash_set_movie_parameters( output->root, &movie_param ) )
                    return ERROR_MSG( "failed to set movie parameters.\n" );
            }
            /* Set copyright information. */
            if( track_opt->copyright_notice
             && lsmash_set_copyright( output->root, out_track->track_ID, track_opt->copyright_language, track_opt->copyright_notice ) )
                return ERROR_MSG( "failed to set a copyright notice.\n" );
            /* Set track parameters. */
            if( lsmash_set_track_parameters( output->root, out_track->track_ID, &track_param ) )
                return ERROR_MSG( "failed to set track parameters.\n" );
            /* Set media parameters. */
            if( lsmash_set_media_parameters( output->root, out_track->track_ID, &media_param ) )
                return ERROR_MSG( "failed to set media parameters.\n" );
            out_track->summary      = in_track->summary;
            out_track->sample_entry = lsmash_add_sample_entry( output->root, out_track->track_ID, out_track->summary );
            if( !out_track->sample_entry )
                return ERROR_MSG( "failed to add sample description entry.\n" );
            out_track->active = 1;
            ++ out_movie->current_track_number;
        }
        input->current_track_number = 1;
    }
    out_movie->current_track_number = 1;
    return 0;
}

static void set_reference_chapter_track( output_t *output, option_t *opt )
{
    if( !opt->chap_file || (!opt->qtff && !opt->itunes_movie) || (opt->brand_3gx == 1) )
        return;
    lsmash_create_reference_chapter_track( output->root, opt->chap_track, opt->chap_file );
}

static int do_mux( muxer_t *muxer )
{
#define LSMASH_MAX( a, b ) ((a) > (b) ? (a) : (b))
    option_t       *opt       = &muxer->opt;
    output_t       *output    = &muxer->output;
    output_movie_t *out_movie = &output->file.movie;
    set_reference_chapter_track( output, opt );
    double   largest_dts = 0;
    uint32_t current_input_number = 1;
    uint32_t num_consecutive_sample_skip = 0;
    uint32_t num_active_input_tracks = out_movie->num_of_tracks;
    uint64_t total_media_size = 0;
    uint8_t  sample_count = 0;
    while( 1 )
    {
        input_t *input = &muxer->input[current_input_number - 1];
        output_track_t *out_track = &out_movie->track[ out_movie->current_track_number - 1 ];
        if( out_track->active )
        {
            lsmash_sample_t *sample = out_track->sample;
            /* Get a new sample data if the track doesn't hold any one. */
            if( !sample )
            {
                /* lsmash_importer_get_access_unit() returns 1 if there're any changes in stream's properties. */
                int ret = lsmash_importer_get_access_unit( input->importer, input->current_track_number, &sample );
                if( ret == LSMASH_ERR_MEMORY_ALLOC )
                    return ERROR_MSG( "failed to alloc memory for buffer.\n" );
                else if( ret <= -1 )
                {
                    lsmash_delete_sample( sample );
                    ERROR_MSG( "failed to get a frame from input file. Maybe corrupted.\n"
                               "Aborting muxing operation and trying to let output be valid file.\n" );
                    break;
                }
                else if( ret == 1 ) /* a change of stream's properties */
                {
                    input_track_t *in_track = &input->track[input->current_track_number - 1];
                    lsmash_cleanup_summary( in_track->summary );
                    in_track->summary = lsmash_duplicate_summary( input->importer, input->current_track_number );
                    out_track->summary      = in_track->summary;
                    out_track->sample_entry = lsmash_add_sample_entry( output->root, out_track->track_ID, out_track->summary );
                    if( !out_track->sample_entry )
                    {
                        ERROR_MSG( "failed to add sample description entry.\n" );
                        break;
                    }
                }
                else if( ret == 2 ) /* EOF */
                {
                    /* No more appendable samples in this track. */
                    lsmash_delete_sample( sample );
                    sample = NULL;
                    out_track->active = 0;
                    out_track->last_delta = lsmash_importer_get_last_delta( input->importer, input->current_track_number );
                    if( out_track->last_delta == 0 )
                        ERROR_MSG( "failed to get the last sample delta.\n" );
                    out_track->last_delta *= out_track->timebase;
                    if( --num_active_input_tracks == 0 )
                        break;      /* Reached the end of whole tracks. */
                }
                if( sample )
                {
                    sample->index = out_track->sample_entry;
                    sample->dts  *= out_track->timebase;
                    sample->cts  *= out_track->timebase;
                    if( opt->timeline_shift )
                    {
                        if( out_track->current_sample_number == 0 )
                            out_track->ctd_shift = sample->cts;
                        sample->cts -= out_track->ctd_shift;
                    }
                    out_track->dts = (double)sample->dts / out_track->timescale;
                    out_track->sample = sample;
                }
            }
            if( sample )
            {
                /* Append a sample if meeting a condition. */
                if( out_track->dts <= largest_dts || num_consecutive_sample_skip == num_active_input_tracks )
                {
                    uint64_t sample_size = sample->length;      /* sample might be deleted internally after appending. */
                    uint64_t sample_dts  = sample->dts;         /* same as above */
                    uint64_t sample_cts  = sample->cts;         /* same as above */
                    if( lsmash_append_sample( output->root, out_track->track_ID, sample ) )
                        return ERROR_MSG( "failed to append a sample.\n" );
                    if( out_track->current_sample_number == 0 )
                        out_track->start_offset = sample_cts;
                    else
                        out_track->last_delta = sample_dts - out_track->prev_dts;       /* for any changes in stream's properties */
                    out_track->prev_dts = sample_dts;
                    out_track->sample = NULL;
                    largest_dts = LSMASH_MAX( largest_dts, out_track->dts );
                    total_media_size += sample_size;
                    ++ out_track->current_sample_number;
                    num_consecutive_sample_skip = 0;
                    /* Print, per 256 samples, total size of imported media. */
                    if( ++sample_count == 0 )
                    {
                        REFRESH_CONSOLE;
                        eprintf( "Importing: %"PRIu64" bytes\r", total_media_size );
                    }
                }
                else
                    ++num_consecutive_sample_skip;      /* Skip appendig sample. */
            }
        }
        if( ++ out_movie->current_track_number > out_movie->num_of_tracks )
            out_movie->current_track_number = 1;    /* Back the first output track. */
        /* Move the next track. */
        if( ++ input->current_track_number > input->num_of_tracks )
        {
            /* Move the next input movie. */
            input->current_track_number = 1;
            if( ++ current_input_number > muxer->num_of_inputs )
                current_input_number = 1;       /* Back the first input movie. */
        }
    }
    for( out_movie->current_track_number = 1;
         out_movie->current_track_number <= out_movie->num_of_tracks;
         out_movie->current_track_number ++ )
    {
        /* Close track. */
        output_track_t *out_track = &out_movie->track[ out_movie->current_track_number - 1 ];
        uint32_t last_sample_delta = out_track->lpcm ? 1 : out_track->last_delta;
        if( lsmash_flush_pooled_samples( output->root, out_track->track_ID, last_sample_delta ) )
            ERROR_MSG( "failed to flush the rest of samples.\n" );
        /* Create edit list.
         * Don't trust media duration basically. It's just duration of media, not duration of track presentation. */
        uint64_t actual_duration = out_track->lpcm
                                 ? lsmash_get_media_duration( output->root, out_track->track_ID )
                                 : out_track->prev_dts + last_sample_delta;
        actual_duration -= out_track->priming_samples;
        lsmash_edit_t edit;
        edit.duration   = actual_duration * ((double)lsmash_get_movie_timescale( output->root ) / out_track->timescale);
        edit.start_time = out_track->priming_samples + out_track->start_offset;
        edit.rate       = ISOM_EDIT_MODE_NORMAL;
        if( lsmash_create_explicit_timeline_map( output->root, out_track->track_ID, edit ) )
            ERROR_MSG( "failed to set timeline map.\n" );
    }
    return 0;
#undef LSMASH_MAX
}

static int moov_to_front_callback( void *param, uint64_t written_movie_size, uint64_t total_movie_size )
{
    REFRESH_CONSOLE;
    eprintf( "Finalizing: [%5.2lf%%]\r", total_movie_size ? ((double)written_movie_size / total_movie_size) * 100.0 : 0 );
    return 0;
}

static int finish_movie( output_t *output, option_t *opt )
{
    /* Set chapter list. */
    if( opt->chap_file )
        lsmash_set_tyrant_chapter( output->root, opt->chap_file, opt->add_bom_to_chpl );
    /* Close movie. */
    REFRESH_CONSOLE;
    if( opt->optimize_pd )
    {
        lsmash_adhoc_remux_t moov_to_front;
        moov_to_front.func        = moov_to_front_callback;
        moov_to_front.buffer_size = 4*1024*1024;    /* 4MiB */
        moov_to_front.param       = NULL;
        return lsmash_finish_movie( output->root, &moov_to_front );
    }
    if( lsmash_finish_movie( output->root, NULL ) )
        return -1;
    return lsmash_write_lsmash_indicator( output->root );
}

int main( int argc, char *argv[] )
{
    muxer_t muxer = { { 0 } };
    lsmash_get_mainargs( &argc, &argv );
    if( parse_global_options( argc, argv, &muxer ) )
        return MUXER_USAGE_ERR();
    if( muxer.opt.help )
    {
        display_help();
        cleanup_muxer( &muxer );
        return 0;
    }
    else if( muxer.opt.version )
    {
        display_version();
        cleanup_muxer( &muxer );
        return 0;
    }
    if( open_input_files( &muxer ) )
        return MUXER_ERR( "failed to open input files.\n" );
    if( prepare_output( &muxer ) )
        return MUXER_ERR( "failed to set up preparation for output.\n" );
    if( do_mux( &muxer ) )
        return MUXER_ERR( "failed to do muxing.\n" );
    if( finish_movie( &muxer.output, &muxer.opt ) )
        return MUXER_ERR( "failed to finish movie.\n" );
    REFRESH_CONSOLE;
    eprintf( "Muxing completed!\n" );
    cleanup_muxer( &muxer );        /* including lsmash_destroy_root() */
    return 0;
}
