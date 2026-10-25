/*****************************************************************************
 * lshmuxer.c
 *****************************************************************************
 * Copyright (C) 2010-2026 L-SMASH project
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

#include "cli.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

#define MAX_NUM_OF_BRANDS 50
#define LSMASH_MAX( a, b ) ((a) > (b) ? (a) : (b))
#define LSMASH_MIN( a, b ) ((a) < (b) ? (a) : (b))

#define eprintf( ... ) fprintf( stderr, __VA_ARGS__ )
#define REFRESH_CONSOLE eprintf( "                                                                               \r" )

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
    FILE    *file;
    uint64_t *ts;
    uint32_t sample_count;
    int      auto_media_timescale;
    int      auto_media_timebase;
    uint64_t media_timescale;
    uint64_t media_timebase;
    uint64_t duration;
    uint64_t composition_delay;
    uint64_t empty_delay;
} timecode_t;

typedef struct
{
    char    *raw_track_option;
    int      remove;
    int      disable;
    int      sbr;
    int      user_fps;
    int      has_dv;
    int      has_cd;
    uint8_t  dv_profile;
    uint8_t  dv_bl_signal_compatibility_id;
    int      has_spatial_audio;
    int      ambisonic_order;
    int      has_stereo_mode;
    char    *stereo_mode;
    char    *projection;
    int      has_ypr;
    double   yaw;
    double   pitch;
    double   roll;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t encoder_delay;
    uint32_t constant_delay;
    int16_t  alternate_group;
    uint16_t ISO_language;
    uint16_t copyright_language;
    char    *copyright_notice;
    char    *handler_name;
    uint32_t par_h;
    uint32_t par_v;
    uint32_t seek;
    int      consider_rap;
    /* Per-track timeline editing options */
    char    *timecode_name;
    uint32_t media_timescale;
    uint32_t media_timebase;
    uint32_t skip_duration_num;
    uint32_t skip_duration_den;
    uint32_t empty_delay_num;
    uint32_t empty_delay_den;
    int      dts_compression;
    int      force_tc_edit;
} track_media_option_t;

typedef struct
{
    int               active;
    lsmash_summary_t *summary;
} input_summary_t;

typedef struct
{
    lsmash_file_t           *fh;
    lsmash_file_parameters_t param;
} input_data_ref_t;

typedef struct
{
    lsmash_media_parameters_t param;
    uint32_t                  num_data_refs;
    input_data_ref_t         *data_refs;
} input_media_t;

typedef struct
{
    int                       active;
    int                       lpcm;
    int                       timeline_edited;
    lsmash_sample_t          *sample;
    double                    dts;
    uint64_t                  composition_delay;
    uint64_t                  skip_duration;
    uint64_t                  empty_delay;
    uint64_t                  edited_duration;
    int                       reach_end_of_media_timeline;
    uint32_t                  track_ID;
    uint32_t                  orig_timescale;
    uint32_t                  timebase;
    uint32_t                  last_sample_delta;
    uint64_t                  duration;
    uint32_t                  current_sample_number;
    uint32_t                  current_sample_index;
    uint32_t                  num_summaries;
    input_summary_t          *summaries;
    lsmash_track_parameters_t track_param;
    input_media_t             media;
} input_track_t;

typedef struct
{
    input_track_t             *track;
    lsmash_itunes_metadata_t  *itunes_metadata;
    lsmash_movie_parameters_t  param;
    uint32_t                   movie_ID;
    uint32_t                   num_tracks;
    uint32_t                   num_itunes_metadata;
    uint32_t                   current_track_number;
} input_movie_t;

typedef struct
{
    lsmash_file_t           *fh;
    lsmash_file_parameters_t param;
    input_movie_t            movie;
    int                      is_raw;
} input_file_t;

typedef struct
{
    lsmash_root_t *root;
    input_file_t   file;
} input_t;

typedef struct
{
    uint32_t                  track_ID;
    uint32_t                  timebase;
    uint32_t                  last_sample_delta;
    uint32_t                  current_sample_number;
    uint32_t                 *summary_remap;
    uint64_t                  skip_dt_interval;
    uint64_t                  last_sample_dts;
    uint32_t                  ctd_shift;
    uint32_t                  cd_shift;
    uint32_t                  priming_samples;
    int64_t                   start_offset;
    int                       lpcm;
    lsmash_track_parameters_t track_param;
    lsmash_media_parameters_t media_param;
} output_track_t;

typedef struct
{
    output_track_t           *track;
    lsmash_movie_parameters_t param;
    uint32_t                  num_tracks;
    uint32_t                  current_track_number;
} output_movie_t;

typedef struct
{
    const char              *name;
    lsmash_file_t           *fh;
    lsmash_file_parameters_t param;
    lsmash_file_parameters_t seg_param;
    output_movie_t           movie;
    uint32_t                 current_subseg_number;
    int (*open)( const char *filename, int open_mode, lsmash_file_parameters_t * );
    int (*close)( lsmash_file_parameters_t * );
} output_file_t;

typedef struct
{
    lsmash_root_t *root;
    output_file_t  file;
    uint32_t       current_seg_number;
} output_t;

typedef struct
{
    output_t              *output;
    input_t               *input;
    track_media_option_t **track_option;
    int                    num_input;
    int                    help;
    int                    version;
    int                    isom;
    int                    isom_version;
    int                    itunes_movie;
    int                    qtff;
    int                    brand_3gx;
    int                    explicit_format;
    int                    optimize_pd;
    int                    timeline_shift;
    int                    compact_size_table;
    uint32_t               movie_timescale;
    uint32_t               num_of_brands;
    uint32_t               brands[MAX_NUM_OF_BRANDS];
    uint32_t               major_brand;
    uint32_t               minor_version;
    int                    user_major_brand;
    int                    user_minor_version;
    int                    add_bom_to_chpl;
    int                    ref_chap_available;
    uint32_t               chap_track;
    char                  *chap_file;
    char                  *copyright_notice;
    uint16_t               copyright_language;
    itunes_metadata_t      itunes_metadata;
    int                    has_user_itunes_metadata;
    uint16_t               default_language;
    uint64_t               max_chunk_size;
    uint32_t               max_chunk_duration_in_ms;
    uint32_t               frag_base_track;
    uint32_t               subseg_per_seg;
    int                    dash;
    double                 min_frag_duration;
    double                 last_frag_merge_thresh;
    int                    dry_run;
    /* Global timeline editing shortcut options */
    uint32_t               global_edit_track;
    char                  *global_timecode;
    uint32_t               global_media_timescale;
    uint32_t               global_media_timebase;
    uint32_t               global_skip_num;
    uint32_t               global_skip_den;
    uint32_t               global_delay_num;
    uint32_t               global_delay_den;
    int                    global_dts_compression;
    int                    positional_mode;
    int                    argc;
} lshmuxer_t;

typedef struct
{
    char *whole_track_option;
    int   num_track_delimiter;
} file_option_t;

static void cleanup_input_movie( input_t *input )
{
    if( !input )
        return;
    input_movie_t *in_movie = &input->file.movie;
    if( in_movie->itunes_metadata )
    {
        for( uint32_t i = 0; i < in_movie->num_itunes_metadata; i++ )
        {
            lsmash_itunes_metadata_t *metadata = &in_movie->itunes_metadata[i];
            if( metadata->type == ITUNES_METADATA_TYPE_STRING )
                lsmash_free( metadata->value.string );
            else if( metadata->type == ITUNES_METADATA_TYPE_BINARY )
                lsmash_free( metadata->value.binary.data );
            lsmash_free( metadata->meaning );
            lsmash_free( metadata->name );
        }
        lsmash_freep( &in_movie->itunes_metadata );
    }
    if( in_movie->track )
    {
        for( uint32_t i = 0; i < in_movie->num_tracks; i++ )
        {
            input_track_t *in_track = &in_movie->track[i];
            lsmash_delete_sample( in_track->sample );
            if( in_track->summaries )
            {
                for( uint32_t j = 0; j < in_track->num_summaries; j++ )
                    lsmash_cleanup_summary( in_track->summaries[j].summary );
                lsmash_free( in_track->summaries );
            }
            if( in_track->media.data_refs )
            {
                for( uint32_t j = 0; j < in_track->media.num_data_refs; j++ )
                    if( in_track->media.data_refs[j].fh != input->file.fh )
                        lsmash_close_file( &in_track->media.data_refs[j].param );
                lsmash_free( in_track->media.data_refs );
            }
        }
        lsmash_freep( &in_movie->track );
    }
    lsmash_close_file( &input->file.param );
    lsmash_destroy_root( input->root );
    input->root = NULL;
}

static void cleanup_output_movie( output_t *output )
{
    if( !output )
        return;
    output_movie_t *out_movie = &output->file.movie;
    if( out_movie->track )
    {
        for( uint32_t i = 0; i < out_movie->num_tracks; i++ )
            lsmash_freep( &out_movie->track[i].summary_remap );
        lsmash_freep( &out_movie->track );
    }
    if( output->file.close )
    {
        if( !(output->file.seg_param.mode & LSMASH_FILE_MODE_INITIALIZATION) )
            output->file.close( &output->file.seg_param );
        output->file.close( &output->file.param );
    }
    if( !output->file.param.brands )
        lsmash_freep( &output->file.seg_param.brands );
    else
    {
        if( output->file.param.brands != output->file.seg_param.brands )
            lsmash_freep( &output->file.seg_param.brands );
        lsmash_freep( &output->file.param.brands );
    }
    lsmash_destroy_root( output->root );
    output->root = NULL;
}

static void cleanup_lshmuxer( lshmuxer_t *muxer )
{
    if( !muxer )
        return;
    cleanup_output_movie( muxer->output );
    if( muxer->input )
    {
        for( int i = 0; i < muxer->num_input; i++ )
            cleanup_input_movie( &muxer->input[i] );
        lsmash_freep( &muxer->input );
    }
    if( muxer->track_option )
    {
        for( int i = 0; i < muxer->num_input; i++ )
            lsmash_freep( &muxer->track_option[i] );
        lsmash_freep( &muxer->track_option );
    }
}

static void cleanup_timecode( timecode_t *timecode )
{
    if( !timecode )
        return;
    if( timecode->file )
    {
        fclose( timecode->file );
        timecode->file = NULL;
    }
    lsmash_freep( &timecode->ts );
}

static int lshmuxer_error( lshmuxer_t *muxer, const char *message, ... )
{
    REFRESH_CONSOLE;
    eprintf( "[Error] " );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    cleanup_lshmuxer( muxer );
    return -1;
}

static int error_message( const char *message, ... )
{
    REFRESH_CONSOLE;
    eprintf( "[Error] " );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    return -1;
}

static int warning_message( const char *message, ... )
{
    REFRESH_CONSOLE;
    eprintf( "[Warning] " );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    return -1;
}

#define LSHMUXER_ERR( ... ) lshmuxer_error( &muxer, __VA_ARGS__ )
#define ERROR_MSG( ... )    error_message( __VA_ARGS__ )
#define WARNING_MSG( ... )  warning_message( __VA_ARGS__ )

static void display_version( void )
{
    eprintf( "\n"
             "L-SMASH unified isom/mov multiplexer (lshmuxer) rev%s  %s\n"
             "Built on %s %s\n"
             "Copyright (C) 2010-2026 L-SMASH project\n",
             LSMASH_REV, LSMASH_GIT_HASH, __DATE__, __TIME__ );
}

static void display_help( void )
{
    display_version();
    eprintf( "\n"
             "Usage: lshmuxer [global_options] -i input1 [-i input2 ...] -o output\n"
             "Global options:\n"
             "    --help                    Display help\n"
             "    --version                 Display version information\n"
             "    --file-format <string>    Specify output file format\n"
             "                                mp4, mov, 3gp, 3g2, m4a, m4v\n"
             "    --isom-version <integer>  Specify maximum compatible ISO Base Media version\n"
             "    --brands <4CC,4CC,...>    Specify compatible brands (--brands help lists brands)\n"
             "    --major-brand <4CC>       Specify major brand\n"
             "    --minor-version <integer> Specify minor version\n"
             "    --optimize-pd             Optimize for progressive download (moov before mdat)\n"
             "    --interleave <integer>    Specify maximum chunk duration in milliseconds\n"
             "    --max-chunk-duration <int>Same as --interleave (in milliseconds)\n"
             "    --max-chunk-size <int>    Specify maximum chunk size in bytes\n"
             "    --movie-timescale <int>   Specify movie timescale\n"
             "    --shift-timeline          Enable composition to decode timeline shift\n"
             "    --compact-size-table      Compress sample size tables when possible\n"
             "    --fragment < integer>      Specify base track number for movie fragments\n"
             "    --min-frag-duration <flt> Specify minimum fragment duration in seconds\n"
             "    --last-frag-merge-thresh <flt> Threshold in (0, 1) to merge the last fragment\n"
             "    --dash <integer>          Specify number of subsegments per Media Segment (0: self-initializing)\n"
             "    --chapter <string>        Set chapters from the file\n"
             "    --chpl-with-bom           Add UTF-8 BOM to chapter strings in the chpl box\n"
             "    --chapter-track <integer> Specify which track to apply reference chapter to [1]\n"
             "    --copyright-notice <arg>  Specify copyright notice (<language>/<string>)\n"
             "    --language <string>       Specify the default language for all output tracks\n"
             "    --dry-run                 Perform a trial run without writing output files\n"
             "  Timeline editing global shortcuts (applied to --track [1] of first input):\n"
             "    --track <integer>         Specify track number to edit [1]\n"
             "    --timecode <string>       Specify timecode v1/v2 file to edit timeline\n"
             "    --media-timescale <int>   Specify media timescale to convert\n"
             "    --media-timebase <int>    Specify media timebase to convert\n"
             "    --skip <rational>         Skip start of media presentation in arbitrary units\n"
             "    --delay <rational>        Insert blank clip before actual media presentation\n"
             "    --dts-compression         Eliminate composition delay with DTS compression\n"
             "  iTunes metadata options:\n"
             "    --album-name <string>     Album name\n"
             "    --artist <string>         Artist\n"
             "    --comment <string>        User comment\n"
             "    --release-date <string>   Release date (YYYY-MM-DD)\n"
             "    --encoder <string>        Person or company that encoded the recording\n"
             "    --genre <string>          Genre\n"
             "    --lyrics <string>         Lyrics\n"
             "    --title <string>          Title or song name\n"
             "    --composer <string>       Composer\n"
             "    --album-artist <string>   Artist for the whole album\n"
             "    --copyright <string>      Copyright\n"
             "    --description <string>    Description\n"
             "    --grouping <string>       Grouping\n"
             "    --tempo <integer>         Beats per minute\n" );
    eprintf( "Track options (-i input?[track_number:]option1=value1,option2=value2[?track_number:...]):\n"
             "    remove                    Exclude this track from the output movie\n"
             "    disable                   Disable this track by default\n"
             "    fps=<arg>                 Specify video framerate (<integer> or <integer>/<integer>)\n"
             "    language=<string>         Specify media language\n"
             "    alternate-group=<integer> Specify alternate group\n"
             "    encoder-delay=<integer>   Represent audio encoder delay (priming samples)\n"
             "    constant-delay=<integer>  Specify constant composition delay\n"
             "    copyright=<arg>           Specify track copyright (<string> or <string>/<language>)\n"
             "    handler=<string>          Specify media handler name\n"
             "    sbr                       Enable backward-compatible explicit SBR signaling\n"
             "    par=<integer>:<integer>   Specify pixel aspect ratio\n"
             "    dv-profile=<p>[.<compat>] Specify Dolby Vision profile and compatibility ID\n"
             "    stereo-mode=<string>      Specify stereoscopic 3D mode (monoscopic, top-bottom, left-right, custom, right-left)\n"
             "    projection=<string>       Specify 360 projection (equirectangular, cubemap)\n"
             "    ypr=<yaw>/<pitch>/<roll>  Specify projection pose in degrees\n"
             "    ambisonic-order=<integer> Specify spatial audio ambisonic order\n"
             "    seek=<integer>            Specify starting sample point\n"
             "    safe-seek=<integer>       Same as seek except for considering random accessible point\n"
             "    timecode=<string>         Specify timecode file for this track\n"
             "    media-timescale=<integer> Specify media timescale to convert for this track\n"
             "    media-timebase=<integer>  Specify media timebase to convert for this track\n"
             "    skip=<rational>           Skip start of media presentation (<int> or <int>/<int>)\n"
             "    delay=<rational>          Insert blank clip before media presentation (<int> or <int>/<int>)\n"
             "    dts-compression           Eliminate composition delay with DTS compression\n" );
}

static void display_codec_name( lsmash_codec_type_t codec_type, uint32_t track_number )
{
#define DISPLAY_CODEC_NAME( codec, codec_name ) \
    else if( lsmash_check_codec_type_identical( codec_type, codec ) ) \
        eprintf( "Track %"PRIu32": "#codec_name"\n", track_number )
    if( 0 );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_AVC1_VIDEO, H.264 Advanced Video Coding );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_AVC3_VIDEO, H.264 Advanced Video Coding );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_HVC1_VIDEO, H.265 High Efficiency Video Coding );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_HEV1_VIDEO, H.265 High Efficiency Video Coding );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_VC_1_VIDEO, SMPTE VC-1 Advanced Profile );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_AV01_VIDEO, AOM AV1 Video );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_MP4V_VIDEO, MPEG-4 Visual );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_MP4A_AUDIO, MPEG-4 Audio );
    DISPLAY_CODEC_NAME(   QT_CODEC_TYPE_MP4A_AUDIO, MPEG-4 Audio );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_AC_3_AUDIO, AC-3 );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_EC_3_AUDIO, Enhanced AC-3 );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_DTSC_AUDIO, DTS );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_DTSE_AUDIO, DTS LBR );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_DTSH_AUDIO, DTS-HD );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_DTSL_AUDIO, DTS-HD Lossless );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_DTSX_AUDIO, DTS:X );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_FLAC_AUDIO, Free Lossless Audio Codec );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_ALAC_AUDIO, Apple Lossless Audio Codec );
    DISPLAY_CODEC_NAME(   QT_CODEC_TYPE_ALAC_AUDIO, Apple Lossless Audio Codec );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_SAWB_AUDIO, Wideband AMR voice );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_SAMR_AUDIO, Narrowband AMR voice );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_OPUS_AUDIO, Opus );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_IPCM_AUDIO, Integer Linear PCM );
    DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_FPCM_AUDIO, Floating-Point Linear PCM );
    DISPLAY_CODEC_NAME(   QT_CODEC_TYPE_LPCM_AUDIO, Uncompressed Audio );
#undef DISPLAY_CODEC_NAME
}

static char *duplicate_string( char *src )
{
    if( !src )
        return NULL;
    int dst_size = strlen( src ) + 1;
    char *dst = lsmash_malloc( dst_size );
    if( !dst )
        return NULL;
    memcpy( dst, src, dst_size );
    return dst;
}

static int get_itunes_metadata( lsmash_root_t *root, uint32_t metadata_number, lsmash_itunes_metadata_t *metadata )
{
    memset( metadata, 0, sizeof(lsmash_itunes_metadata_t) );
    if( lsmash_get_itunes_metadata( root, metadata_number, metadata ) < 0 )
        return -1;
    lsmash_itunes_metadata_t shadow = *metadata;
    metadata->meaning = NULL;
    metadata->name    = NULL;
    memset( &metadata->value, 0, sizeof(lsmash_itunes_metadata_value_t) );
    if( shadow.meaning )
    {
        metadata->meaning = duplicate_string( shadow.meaning );
        if( !metadata->meaning )
            goto fail;
    }
    if( shadow.name )
    {
        metadata->name = duplicate_string( shadow.name );
        if( !metadata->name )
            goto fail;
    }
    if( shadow.type == ITUNES_METADATA_TYPE_STRING )
    {
        metadata->value.string = duplicate_string( shadow.value.string );
        if( !metadata->value.string )
            goto fail;
    }
    else if( shadow.type == ITUNES_METADATA_TYPE_BINARY )
    {
        metadata->value.binary.data = lsmash_malloc( shadow.value.binary.size );
        if( !metadata->value.binary.data )
            goto fail;
        memcpy( metadata->value.binary.data, shadow.value.binary.data, shadow.value.binary.size );
        metadata->value.binary.size    = shadow.value.binary.size;
        metadata->value.binary.subtype = shadow.value.binary.subtype;
    }
    else
        metadata->value = shadow.value;
    lsmash_cleanup_itunes_metadata( &shadow );
    return 0;
fail:
    lsmash_cleanup_itunes_metadata( &shadow );
    lsmash_cleanup_itunes_metadata( metadata );
    return -1;
}

static int is_relative_path( const char *path )
{
    return !(path[0] == '/' || path[0] == '\\' || (path[0] != '\0' && path[1] == ':'));
}

static int input_data_reference
(
    input_t                 *input,
    uint32_t                 track_ID,
    input_data_ref_t        *in_data_ref,
    lsmash_data_reference_t *data_ref
)
{
    if( lsmash_open_file( data_ref->location, 1, &in_data_ref->param ) < 0 )
    {
        WARNING_MSG( "failed to open an external media file.\n" );
        return -1;
    }
    in_data_ref->param.mode |= LSMASH_FILE_MODE_MEDIA;
    in_data_ref->fh = lsmash_set_file( input->root, &in_data_ref->param );
    if( !in_data_ref->fh )
    {
        WARNING_MSG( "failed to set an external media file as a data reference.\n" );
        return -1;
    }
    if( lsmash_assign_data_reference( input->root, track_ID, data_ref->index, in_data_ref->fh ) < 0 )
    {
        WARNING_MSG( "failed to assign an external media a data reference.\n" );
        return -1;
    }
    return 0;
}

static int get_movie( input_t *input, char *input_name )
{
    if( !strcmp( input_name, "-" ) )
        return ERROR_MSG( "standard input not supported.\n" );
    input->root = lsmash_create_root();
    if( !input->root )
        return ERROR_MSG( "failed to create a ROOT for an input file.\n" );
    input_file_t *in_file = &input->file;
    if( lsmash_open_file( input_name, 1, &in_file->param ) < 0 )
        return ERROR_MSG( "failed to open an input file.\n" );
    in_file->fh = lsmash_set_file( input->root, &in_file->param );
    if( !in_file->fh )
        return ERROR_MSG( "failed to add an input file into a ROOT.\n" );
    if( lsmash_read_file( in_file->fh, &in_file->param ) < 0 )
        return ERROR_MSG( "failed to read an input file.\n" );
    in_file->is_raw = (in_file->param.brand_count == 0 && in_file->param.brands == NULL);
    if( in_file->is_raw )
        in_file->param.major_brand = 0;
    /* Get iTunes metadata. */
    input_movie_t *in_movie = &in_file->movie;
    in_movie->num_itunes_metadata = lsmash_count_itunes_metadata( input->root );
    if( in_movie->num_itunes_metadata )
    {
        in_movie->itunes_metadata = lsmash_malloc( in_movie->num_itunes_metadata * sizeof(lsmash_itunes_metadata_t) );
        if( !in_movie->itunes_metadata )
            return ERROR_MSG( "failed to alloc iTunes metadata.\n" );
        uint32_t itunes_metadata_count = 0;
        for( uint32_t i = 1; i <= in_movie->num_itunes_metadata; i++ )
        {
            if( get_itunes_metadata( input->root, i, &in_movie->itunes_metadata[itunes_metadata_count] ) )
            {
                WARNING_MSG( "failed to get an iTunes metadata.\n" );
                continue;
            }
            ++itunes_metadata_count;
        }
        in_movie->num_itunes_metadata = itunes_metadata_count;
    }
    in_movie->current_track_number = 1;
    lsmash_initialize_movie_parameters( &in_movie->param );
    if( lsmash_get_movie_parameters( input->root, &in_movie->param ) )
        return ERROR_MSG( "failed to get movie parameters.\n" );
    uint32_t num_tracks = in_movie->num_tracks = in_movie->param.number_of_tracks;
    if( num_tracks == 0 )
        return ERROR_MSG( "there is no valid track in input file.\n" );
    input_track_t *in_track = in_movie->track = lsmash_malloc_zero( num_tracks * sizeof(input_track_t) );
    if( !in_track )
        return ERROR_MSG( "failed to alloc input tracks.\n" );
    for( uint32_t i = 0; i < num_tracks; i++ )
    {
        in_track[i].track_ID = lsmash_get_track_ID( input->root, i + 1 );
        if( !in_track[i].track_ID )
            return ERROR_MSG( "failed to get track_ID.\n" );
    }
    for( uint32_t i = 0; i < num_tracks; i++ )
    {
        lsmash_initialize_track_parameters( &in_track[i].track_param );
        if( lsmash_get_track_parameters( input->root, in_track[i].track_ID, &in_track[i].track_param ) )
        {
            WARNING_MSG( "failed to get track parameters.\n" );
            continue;
        }
        lsmash_initialize_media_parameters( &in_track[i].media.param );
        if( lsmash_get_media_parameters( input->root, in_track[i].track_ID, &in_track[i].media.param ) )
        {
            WARNING_MSG( "failed to get media parameters.\n" );
            continue;
        }
        in_track[i].orig_timescale = in_track[i].media.param.timescale;
        uint32_t data_ref_count = lsmash_count_data_reference( input->root, in_track[i].track_ID );
        if( data_ref_count == 0 )
        {
            WARNING_MSG( "failed to get the number of data references.\n" );
            continue;
        }
        in_track[i].media.data_refs = lsmash_malloc_zero( data_ref_count * sizeof(input_data_ref_t) );
        if( !in_track[i].media.data_refs )
        {
            WARNING_MSG( "failed to allocate handles of data reference.\n" );
            continue;
        }
        in_track[i].media.num_data_refs = data_ref_count;
        for( uint32_t j = 0; j < data_ref_count; j++ )
        {
            input_data_ref_t *in_data_ref = &in_track[i].media.data_refs[j];
            lsmash_data_reference_t data_ref = { .index = j + 1 };
            if( lsmash_get_data_reference( input->root, in_track[i].track_ID, &data_ref ) < 0 )
            {
                WARNING_MSG( "failed to get a data reference.\n" );
                continue;
            }
            if( data_ref.location )
            {
                if( is_relative_path( data_ref.location ) && !is_relative_path( input_name ) )
                {
                    int location_length   = strlen( data_ref.location );
                    int input_name_length = strlen( input_name );
                    char *p = input_name + input_name_length;
                    while( p != input_name && *p != '/' && *p != '\\' )
                        --p;
                    int relative_path_length = p == input_name ? 2 : p - input_name;
                    char *location = lsmash_malloc( relative_path_length + location_length + 2 );
                    if( location )
                    {
                        memcpy( location, input_name, relative_path_length );
                        memcpy( location + relative_path_length + 1, data_ref.location, location_length );
                        location[relative_path_length]                       = '/';
                        location[relative_path_length + location_length + 1] = '\0';
                        lsmash_cleanup_data_reference( &data_ref );
                        data_ref.location = location;
                    }
                }
                int ret = input_data_reference( input, in_track[i].track_ID, in_data_ref, &data_ref );
                lsmash_cleanup_data_reference( &data_ref );
                if( ret < 0 )
                    continue;
            }
            else
            {
                in_data_ref->fh    = in_file->fh;
                in_data_ref->param = in_file->param;
            }
        }
        if( lsmash_construct_timeline( input->root, in_track[i].track_ID ) )
        {
            WARNING_MSG( "failed to construct timeline.\n" );
            continue;
        }
        /* Why: Prefer the media header duration (mdhd->duration) when present in an existing
         * container, falling back to the constructed media timeline duration only when mdhd->duration
         * is zero (such as fragmented inputs or raw elementary stream Fake Tracks). */
        in_track[i].duration = lsmash_get_media_duration( input->root, in_track[i].track_ID );
        if( !in_track[i].duration )
            in_track[i].duration = lsmash_get_media_duration_from_media_timeline( input->root, in_track[i].track_ID );
        if( lsmash_get_last_sample_delta_from_media_timeline( input->root, in_track[i].track_ID, &in_track[i].last_sample_delta ) )
        {
            WARNING_MSG( "failed to get the last sample delta.\n" );
            continue;
        }
        in_track[i].num_summaries = lsmash_count_summary( input->root, in_track[i].track_ID );
        if( in_track[i].num_summaries == 0 )
        {
            WARNING_MSG( "failed to find valid summaries.\n" );
            continue;
        }
        in_track[i].summaries = lsmash_malloc_zero( in_track[i].num_summaries * sizeof(input_summary_t) );
        if( !in_track[i].summaries )
            return ERROR_MSG( "failed to alloc input summaries.\n" );
        for( uint32_t j = 0; j < in_track[i].num_summaries; j++ )
        {
            lsmash_summary_t *summary = lsmash_get_summary( input->root, in_track[i].track_ID, j + 1 );
            if( !summary )
            {
                WARNING_MSG( "failed to get a summary.\n" );
                continue;
            }
            if( !LSMASH_FLAGS_SATISFIED( lsmash_check_codec_support( summary->sample_type ), LSMASH_CODEC_SUPPORT_FLAG_REMUX ) )
            {
                lsmash_cleanup_summary( summary );
                WARNING_MSG( "no support to mux/remux this stream.\n" );
                continue;
            }
            if( lsmash_check_codec_type_identical( summary->sample_type,   QT_CODEC_TYPE_LPCM_AUDIO )
             || lsmash_check_codec_type_identical( summary->sample_type, ISOM_CODEC_TYPE_IPCM_AUDIO )
             || lsmash_check_codec_type_identical( summary->sample_type, ISOM_CODEC_TYPE_FPCM_AUDIO ) )
                in_track[i].lpcm = 1;
            in_track[i].summaries[j].summary = summary;
            in_track[i].summaries[j].active  = 1;
        }
        in_track[i].active                = 1;
        in_track[i].timebase              = 1;
        in_track[i].current_sample_number = 1;
        in_track[i].sample                = NULL;
        in_track[i].dts                   = 0;
        in_track[i].composition_delay     = 0;
        in_track[i].skip_duration         = 0;
    }
    return 0;
}

static int add_brand( lshmuxer_t *muxer, uint32_t brand )
{
    if( muxer->num_of_brands >= MAX_NUM_OF_BRANDS )
        return -1;
    for( uint32_t i = 0; i < muxer->num_of_brands; i++ )
        if( muxer->brands[i] == brand )
            return -2;
    muxer->brands[muxer->num_of_brands++] = brand;
    return 0;
}

static int setup_isom_version( lshmuxer_t *muxer )
{
    add_brand( muxer, ISOM_BRAND_TYPE_ISOM );
    if( muxer->isom_version > 6 )
        return ERROR_MSG( "unknown ISO Base Media version.\n" );
#define SET_ISOM_VERSION( version ) \
    if( muxer->isom_version >= version ) \
        add_brand( muxer, ISOM_BRAND_TYPE_ISO##version )
    SET_ISOM_VERSION( 2 );
    SET_ISOM_VERSION( 3 );
    SET_ISOM_VERSION( 4 );
    SET_ISOM_VERSION( 5 );
    SET_ISOM_VERSION( 6 );
#undef SET_ISOM_VERSION
    return 0;
}

static void display_brands( void )
{
    eprintf( "Supported brands:\n"
             "  mp41, mp42, isom, iso2, iso3, iso4, iso5, iso6\n"
             "  avc1, av01, cdm4, dby1, Opus\n"
             "  M4A , M4B , M4V , qt  \n"
             "  3gp4, 3gp5, 3gp6, 3g2a, 3gg6\n"
             "  dash, msdh, msix\n" );
}

static int decide_brands( lshmuxer_t *muxer )
{
    if( muxer->num_of_brands == 0 )
    {
        if( !muxer->user_major_brand )
            muxer->major_brand = ISOM_BRAND_TYPE_MP42;
        if( !muxer->user_minor_version )
            muxer->minor_version = 0x00000000;
        add_brand( muxer, ISOM_BRAND_TYPE_MP42 );
        add_brand( muxer, ISOM_BRAND_TYPE_MP41 );
        add_brand( muxer, ISOM_BRAND_TYPE_ISOM );
        muxer->isom = 1;
        eprintf( "MP4 muxing mode\n" );
        return setup_isom_version( muxer );
    }
    if( !muxer->user_major_brand )
        muxer->major_brand = muxer->brands[0];
    for( uint32_t i = 0; i < muxer->num_of_brands; i++ )
    {
        switch( muxer->brands[i] )
        {
            case ISOM_BRAND_TYPE_3GP6 :
                add_brand( muxer, ISOM_BRAND_TYPE_3G2A );
                muxer->brand_3gx = 1;
                break;
            case ISOM_BRAND_TYPE_3G2A :
                muxer->brand_3gx = 2;
                break;
            case ISOM_BRAND_TYPE_QT :
                muxer->qtff = 1;
                break;
            case ISOM_BRAND_TYPE_M4A :
            case ISOM_BRAND_TYPE_M4V :
                muxer->itunes_movie = 1;
                /* fall-through */
            case ISOM_BRAND_TYPE_MP42 :
                add_brand( muxer, ISOM_BRAND_TYPE_MP42 );
                add_brand( muxer, ISOM_BRAND_TYPE_MP41 );
                break;
            default :
                break;
        }
        if( muxer->brands[i] != ISOM_BRAND_TYPE_QT )
            muxer->isom = 1;
    }
    if( !muxer->user_minor_version )
    {
        switch( muxer->major_brand )
        {
            case ISOM_BRAND_TYPE_MP42 :
                muxer->minor_version = 0x00000000;
                eprintf( "MP4 muxing mode\n" );
                break;
            case ISOM_BRAND_TYPE_M4A :
            case ISOM_BRAND_TYPE_M4V :
                muxer->minor_version = 0x00000000;
                eprintf( "iTunes MP4 muxing mode\n" );
                break;
            case ISOM_BRAND_TYPE_3GP6 :
                muxer->minor_version = 0x00000000;
                eprintf( "3GPP muxing mode\n" );
                break;
            case ISOM_BRAND_TYPE_3G2A :
                muxer->minor_version = 0x00010000;
                eprintf( "3GPP2 muxing mode\n" );
                break;
            case ISOM_BRAND_TYPE_QT :
                muxer->minor_version = 0x00000000;
                eprintf( "QuickTime file format muxing mode\n" );
                break;
            default :
                break;
        }
    }
    if( muxer->isom )
    {
        if( setup_isom_version( muxer ) < 0 )
            return -1;
    }
    if( muxer->num_of_brands > MAX_NUM_OF_BRANDS )
        return ERROR_MSG( "exceed the maximum number of brands we can deal with.\n" );
    return 0;
}

static int parse_track_option( lshmuxer_t *muxer )
{
    track_media_option_t **track = muxer->track_option;
    for( int i = 0; i < muxer->num_input; i++ )
    {
        uint32_t num_tracks = muxer->input[i].file.movie.num_tracks;
        for( uint32_t j = 0; j < num_tracks; j++ )
        {
            char *raw_opt = track[i][j].raw_track_option;
            if( raw_opt == NULL )
                break;
            uint32_t track_number = 1;
            char *first_token = NULL;
            char *colon = strchr( raw_opt, ':' );
            char *comma = strchr( raw_opt, ',' );
            char *equal = strchr( raw_opt, '=' );
            /* A leading '<track_number>:' is present only if ':' appears before any ',' or '=' */
            if( colon && (!comma || colon < comma) && (!equal || colon < equal) )
            {
                if( colon == raw_opt )
                    return ERROR_MSG( "track number is not specified in %s\n", raw_opt );
                *colon = '\0';
                track_number = atoi( raw_opt );
                if( track_number == 0 || track_number > num_tracks )
                    return ERROR_MSG( "%s is an invalid track number.\n", raw_opt );
                first_token = colon + 1;
            }
            else
            {
                track_number = 1;
                if( track_number > num_tracks )
                    return ERROR_MSG( "invalid track number.\n" );
                first_token = raw_opt;
            }
            track_media_option_t *current_track_opt = &track[i][track_number - 1];
            char *track_option = NULL;
            while( (track_option = strtok( track_option ? NULL : first_token, "," )) != NULL )
            {
                if( strchr( track_option, '=' ) != strrchr( track_option, '=' ) )
                    return ERROR_MSG( "multiple equal signs inside one track option in %s\n", track_option );
                if( strstr( track_option, "remove" ) )
                {
                    current_track_opt->remove = 1;
                    break;
                }
                else if( strstr( track_option, "disable" ) )
                    current_track_opt->disable = 1;
                else if( strstr( track_option, "alternate-group=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->alternate_group = atoi( track_parameter );
                }
                else if( strstr( track_option, "encoder-delay=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->encoder_delay = atoi( track_parameter );
                }
                else if( strstr( track_option, "constant-delay=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->constant_delay = atoi( track_parameter );
                    current_track_opt->has_cd         = 1;
                }
                else if( strstr( track_option, "language=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->ISO_language = lsmash_pack_iso_language( track_parameter );
                }
                else if( strstr( track_option, "dv-profile=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    if( sscanf( track_parameter, "%"SCNu8".%"SCNu8, &current_track_opt->dv_profile, &current_track_opt->dv_bl_signal_compatibility_id ) == 1 )
                    {
                        current_track_opt->dv_profile = atoi( track_parameter );
                        current_track_opt->dv_bl_signal_compatibility_id = 0;
                    }
                    current_track_opt->has_dv = 1;
                }
                else if( strstr( track_option, "ambisonic-order=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->ambisonic_order   = atoi( track_parameter );
                    current_track_opt->has_spatial_audio = 1;
                }
                else if( strstr( track_option, "stereo-mode=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->stereo_mode     = track_parameter;
                    current_track_opt->has_stereo_mode = 1;
                }
                else if( strstr( track_option, "projection=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->projection = track_parameter;
                }
                else if( strstr( track_option, "ypr=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    if( sscanf( track_parameter, "%lf/%lf/%lf", &current_track_opt->yaw, &current_track_opt->pitch, &current_track_opt->roll ) != 3 )
                        return ERROR_MSG( "incorrect number of elements for %s\n", track_option );
                    current_track_opt->has_ypr = 1;
                }
                else if( strstr( track_option, "fps=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    if( sscanf( track_parameter, "%"SCNu32"/%"SCNu32, &current_track_opt->fps_num, &current_track_opt->fps_den ) == 1 )
                    {
                        current_track_opt->fps_num = atoi( track_parameter );
                        current_track_opt->fps_den = 1;
                    }
                    if( !current_track_opt->fps_num || !current_track_opt->fps_den )
                        return ERROR_MSG( "invalid framerate %s\n", track_option );
                    current_track_opt->user_fps = 1;
                }
                else if( strstr( track_option, "copyright=" ) )
                {
                    /* Why: Parse copyright=<notice>[/<language>] where the notice string comes first
                     * and the optional 3-letter ISO 639-2/T language code follows '/'. */
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->copyright_notice = track_parameter;
                    while( *track_parameter )
                    {
                        if( *track_parameter == '/' )
                        {
                            *track_parameter++ = '\0';
                            break;
                        }
                        ++track_parameter;
                    }
                    current_track_opt->copyright_language = lsmash_pack_iso_language( track_parameter );
                }
                else if( strstr( track_option, "handler=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->handler_name = track_parameter;
                }
                else if( !strcmp( track_option, "sbr" ) || !strcmp( track_option, "ps" ) )
                    current_track_opt->sbr = 1;
                else if( strstr( track_option, "par=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    if( sscanf( track_parameter, "%"SCNu32":%"SCNu32, &current_track_opt->par_h, &current_track_opt->par_v ) != 2 )
                    {
                        current_track_opt->par_h = 0;
                        current_track_opt->par_v = 0;
                    }
                }
                else if( strstr( track_option, "safe-seek=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->seek         = atoi( track_parameter );
                    current_track_opt->consider_rap = 1;
                }
                else if( strstr( track_option, "seek=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->seek = atoi( track_parameter );
                }
                else if( strstr( track_option, "timecode=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->timecode_name = track_parameter;
                }
                else if( strstr( track_option, "media-timescale=" ) || strstr( track_option, "timescale=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->media_timescale = atoi( track_parameter );
                    if( !current_track_opt->media_timescale )
                        return ERROR_MSG( "invalid media timescale in %s\n", track_option );
                }
                else if( strstr( track_option, "media-timebase=" ) || strstr( track_option, "timebase=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    current_track_opt->media_timebase = atoi( track_parameter );
                    if( !current_track_opt->media_timebase )
                        return ERROR_MSG( "invalid media timebase in %s\n", track_option );
                }
                else if( strstr( track_option, "skip=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    if( sscanf( track_parameter, "%"SCNu32"/%"SCNu32, &current_track_opt->skip_duration_num, &current_track_opt->skip_duration_den ) != 2 )
                    {
                        current_track_opt->skip_duration_num = atoi( track_parameter );
                        current_track_opt->skip_duration_den = 1;
                    }
                    if( !current_track_opt->skip_duration_num || !current_track_opt->skip_duration_den )
                        return ERROR_MSG( "invalid skip duration in %s\n", track_option );
                }
                else if( strstr( track_option, "delay=" ) )
                {
                    char *track_parameter = strchr( track_option, '=' ) + 1;
                    if( sscanf( track_parameter, "%"SCNu32"/%"SCNu32, &current_track_opt->empty_delay_num, &current_track_opt->empty_delay_den ) != 2 )
                    {
                        current_track_opt->empty_delay_num = atoi( track_parameter );
                        current_track_opt->empty_delay_den = 1;
                    }
                    if( !current_track_opt->empty_delay_num || !current_track_opt->empty_delay_den )
                        return ERROR_MSG( "invalid delay time in %s\n", track_option );
                }
                else if( !strcmp( track_option, "dts-compression" ) )
                    current_track_opt->dts_compression = 1;
                else
                    return ERROR_MSG( "unknown track option %s\n", track_option );
            }
        }
    }
    return 0;
}

static uint32_t string_to_4cc( const char *str )
{
    uint32_t fourcc = 0;
    for( int i = 0; i < 4; i++ )
    {
        unsigned char c = str[i] ? (unsigned char)str[i] : ' ';
        fourcc = (fourcc << 8) | c;
        if( !str[i] )
        {
            for( int j = i + 1; j < 4; j++ )
                fourcc = (fourcc << 8) | ' ';
            break;
        }
    }
    return fourcc;
}

static int parse_cli_option( int argc, char **argv, lshmuxer_t *muxer )
{
#define FAILED_PARSE_CLI_OPTION( ... ) \
    do                                         \
    {                                          \
        lsmash_free( input_file_option );      \
        return ERROR_MSG( __VA_ARGS__ );       \
    }                                          \
    while( 0 )
    input_t               *input             = muxer->input;
    track_media_option_t **track_option      = muxer->track_option;
    file_option_t         *input_file_option = lsmash_malloc_zero( muxer->num_input * sizeof(file_option_t) );
    if( !input_file_option )
        return ERROR_MSG( "failed to allocate per-file option.\n" );
    int input_movie_number = 0;
    for( int i = 1; i < argc; i++ )
    {
        if( !strcasecmp( argv[i], "-i" ) || !strcasecmp( argv[i], "--input" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "-i requires an argument.\n" );
            input_file_option[input_movie_number].num_track_delimiter = 0;
            char *p = argv[i];
            while( *p )
                input_file_option[input_movie_number].num_track_delimiter += (*p++ == '?');
            if( get_movie( &input[input_movie_number], strtok( argv[i], "?" ) ) )
                FAILED_PARSE_CLI_OPTION( "failed to get input movie.\n" );
            uint32_t num_tracks = input[input_movie_number].file.movie.num_tracks;
            track_option[input_movie_number] = lsmash_malloc_zero( num_tracks * sizeof(track_media_option_t) );
            if( !track_option[input_movie_number] )
                FAILED_PARSE_CLI_OPTION( "couldn't allocate memory.\n" );
            input_file_option[input_movie_number].whole_track_option = strtok( NULL, "" );
            input[input_movie_number].file.movie.movie_ID = input_movie_number + 1;
            ++input_movie_number;
        }
        else if( !strcasecmp( argv[i], "-o" ) || !strcasecmp( argv[i], "--output" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "-o requires an argument.\n" );
            output_t *output = muxer->output;
            output->root = lsmash_create_root();
            if( !output->root )
                FAILED_PARSE_CLI_OPTION( "failed to create a ROOT.\n" );
            output->file.name = argv[i];
        }
        else if( !strcasecmp( argv[i], "--optimize-pd" ) )
            muxer->optimize_pd = 1;
        else if( !strcasecmp( argv[i], "--interleave" ) || !strcasecmp( argv[i], "--max-chunk-duration" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "%s requires an argument.\n", argv[i - 1] );
            muxer->max_chunk_duration_in_ms = atoi( argv[i] );
            if( muxer->max_chunk_duration_in_ms == 0 )
                FAILED_PARSE_CLI_OPTION( "%s is an invalid value for %s.\n", argv[i], argv[i - 1] );
        }
        else if( !strcasecmp( argv[i], "--max-chunk-size" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--max-chunk-size requires an argument.\n" );
            muxer->max_chunk_size = atoi( argv[i] );
            if( muxer->max_chunk_size == 0 )
                FAILED_PARSE_CLI_OPTION( "%s is an invalid value for --max-chunk-size.\n", argv[i] );
        }
        else if( !strcasecmp( argv[i], "--file-format" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--file-format requires an argument.\n" );
            static const struct
            {
                uint32_t    brand_4cc;
                const char *file_format;
            } file_format_list[] =
            {
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
                        int ret = add_brand( muxer, file_format_list[j].brand_4cc );
                        if( ret == -2 )
                            FAILED_PARSE_CLI_OPTION( "you specified same output file format twice.\n" );
                        else if( ret == -1 )
                            FAILED_PARSE_CLI_OPTION( "exceed the maximum number of brands we can deal with.\n" );
                        muxer->explicit_format = 1;
                        break;
                    }
                if( !file_format_list[j].file_format )
                    FAILED_PARSE_CLI_OPTION( "unknown file format: %s\n", file_format );
            }
        }
        else if( !strcasecmp( argv[i], "--brands" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--brands requires an argument.\n" );
            if( !strcasecmp( argv[i], "help" ) )
            {
                display_brands();
                lsmash_free( input_file_option );
                muxer->help = 1;
                return 0;
            }
            char *brand_str = NULL;
            while( (brand_str = strtok( brand_str ? NULL : argv[i], "," )) != NULL )
            {
                if( add_brand( muxer, string_to_4cc( brand_str ) ) == -1 )
                    FAILED_PARSE_CLI_OPTION( "exceed the maximum number of brands we can deal with.\n" );
                muxer->explicit_format = 1;
            }
        }
        else if( !strcasecmp( argv[i], "--major-brand" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--major-brand requires an argument.\n" );
            muxer->major_brand      = string_to_4cc( argv[i] );
            muxer->user_major_brand = 1;
            muxer->explicit_format  = 1;
            add_brand( muxer, muxer->major_brand );
        }
        else if( !strcasecmp( argv[i], "--minor-version" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--minor-version requires an argument.\n" );
            muxer->minor_version      = atoi( argv[i] );
            muxer->user_minor_version = 1;
        }
        else if( !strcasecmp( argv[i], "--isom-version" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--isom-version requires an argument.\n" );
            if( muxer->isom_version )
                FAILED_PARSE_CLI_OPTION( "you specified --isom-version twice.\n" );
            muxer->isom_version    = atoi( argv[i] );
            muxer->explicit_format = 1;
        }
        else if( !strcasecmp( argv[i], "--shift-timeline" ) )
            muxer->timeline_shift = 1;
        else if( !strcasecmp( argv[i], "--compact-size-table" ) || !strcasecmp( argv[i], "compact-size-table" ) )
            muxer->compact_size_table = 1;
        else if( !strcasecmp( argv[i], "--chapter" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--chapter requires an argument.\n" );
            muxer->chap_file = argv[i];
        }
        else if( !strcasecmp( argv[i], "--chpl-with-bom" ) )
            muxer->add_bom_to_chpl = 1;
        else if( !strcasecmp( argv[i], "--chapter-track" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--chapter-track requires an argument.\n" );
            muxer->chap_track = atoi( argv[i] );
            if( !muxer->chap_track )
                FAILED_PARSE_CLI_OPTION( "%s is an invalid track number.\n", argv[i] );
        }
        else if( !strcasecmp( argv[i], "--copyright-notice" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--copyright-notice requires an argument.\n" );
            if( muxer->copyright_notice )
                FAILED_PARSE_CLI_OPTION( "you specified --copyright-notice twice.\n" );
            /* Why: Parse <notice>[/<language>] where '/' separates the copyright notice from the
             * optional 3-letter ISO 639-2/T language code. When '/' is omitted, language points to
             * the trailing NUL and lsmash_pack_iso_language() returns 0 (mapped to 'und' by the core). */
            muxer->copyright_notice = argv[i];
            char *language = muxer->copyright_notice;
            while( *language )
            {
                if( *language == '/' )
                {
                    *language++ = '\0';
                    break;
                }
                ++language;
            }
            muxer->copyright_language = language ? lsmash_pack_iso_language( language ) : ISOM_LANGUAGE_CODE_UNDEFINED;
        }
        else if( !strcasecmp( argv[i], "--movie-timescale" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--movie-timescale requires an argument.\n" );
            if( muxer->movie_timescale )
                FAILED_PARSE_CLI_OPTION( "you specified --movie-timescale twice.\n" );
            muxer->movie_timescale = atoi( argv[i] );
        }
        else if( !strcasecmp( argv[i], "--language" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--language requires an argument.\n" );
            muxer->default_language = lsmash_pack_iso_language( argv[i] );
        }
        else if( !strcasecmp( argv[i], "--fragment" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--fragment requires an argument.\n" );
            muxer->frag_base_track = atoi( argv[i] );
            if( muxer->frag_base_track == 0 )
                FAILED_PARSE_CLI_OPTION( "%s is an invalid track number.\n", argv[i] );
        }
        else if( !strcasecmp( argv[i], "--min-frag-duration" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--min-frag-duration requires an argument.\n" );
            muxer->min_frag_duration = atof( argv[i] );
            if( muxer->min_frag_duration <= 0.0 )
                FAILED_PARSE_CLI_OPTION( "%s is an invalid fragment duration.\n", argv[i] );
            else if( muxer->frag_base_track == 0 )
                FAILED_PARSE_CLI_OPTION( "--min-frag-duration requires --fragment also be set.\n" );
        }
        else if( !strcasecmp( argv[i], "--last-frag-merge-thresh" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--last-frag-merge-thresh requires an argument.\n" );
            muxer->last_frag_merge_thresh = atof( argv[i] );
            if( muxer->last_frag_merge_thresh <= 0.0 || muxer->last_frag_merge_thresh >= 1.0 )
                FAILED_PARSE_CLI_OPTION( "%s is an invalid fragment duration. Must be in (0, 1).\n", argv[i] );
            else if( muxer->frag_base_track == 0 )
                FAILED_PARSE_CLI_OPTION( "--last-frag-merge-thresh requires --fragment also be set.\n" );
            else if( muxer->min_frag_duration == 0.0 )
                FAILED_PARSE_CLI_OPTION( "--last-frag-merge-thresh requires --min-frag-duration also be set.\n" );
        }
        else if( !strcasecmp( argv[i], "--dash" ) )
        {
            if( ++i < argc && argv[i][0] != '-' )
                muxer->subseg_per_seg = atoi( argv[i] );
            else
            {
                --i;
                muxer->subseg_per_seg = 0;
            }
            muxer->dash = 1;
        }
        else if( !strcasecmp( argv[i], "--dry-run" ) )
            muxer->dry_run = 1;
        /* Global timeline editing shortcut options */
        else if( !strcasecmp( argv[i], "--track" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--track requires an argument.\n" );
            muxer->global_edit_track = atoi( argv[i] );
            if( !muxer->global_edit_track )
                FAILED_PARSE_CLI_OPTION( "Invalid track number.\n" );
        }
        else if( !strcasecmp( argv[i], "--timecode" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--timecode requires an argument.\n" );
            muxer->global_timecode = argv[i];
        }
        else if( !strcasecmp( argv[i], "--media-timescale" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--media-timescale requires an argument.\n" );
            muxer->global_media_timescale = atoi( argv[i] );
            if( !muxer->global_media_timescale )
                FAILED_PARSE_CLI_OPTION( "Invalid media timescale.\n" );
        }
        else if( !strcasecmp( argv[i], "--media-timebase" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--media-timebase requires an argument.\n" );
            muxer->global_media_timebase = atoi( argv[i] );
            if( !muxer->global_media_timebase )
                FAILED_PARSE_CLI_OPTION( "Invalid media timebase.\n" );
        }
        else if( !strcasecmp( argv[i], "--skip" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--skip requires an argument.\n" );
            if( sscanf( argv[i], "%"SCNu32"/%"SCNu32, &muxer->global_skip_num, &muxer->global_skip_den ) != 2 )
            {
                muxer->global_skip_num = atoi( argv[i] );
                muxer->global_skip_den = 1;
            }
            if( muxer->global_skip_num == 0 || muxer->global_skip_den == 0 )
                FAILED_PARSE_CLI_OPTION( "Invalid skip duration.\n" );
        }
        else if( !strcasecmp( argv[i], "--delay" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "--delay requires an argument.\n" );
            if( sscanf( argv[i], "%"SCNu32"/%"SCNu32, &muxer->global_delay_num, &muxer->global_delay_den ) != 2 )
            {
                muxer->global_delay_num = atoi( argv[i] );
                muxer->global_delay_den = 1;
            }
            if( muxer->global_delay_num == 0 || muxer->global_delay_den == 0 )
                FAILED_PARSE_CLI_OPTION( "Invalid delay time.\n" );
        }
        else if( !strcasecmp( argv[i], "--dts-compression" ) )
            muxer->global_dts_compression = 1;
#define CHECK_ITUNES_METADATA_ARG_STRING( argument, value ) \
        else if( !strcasecmp( argv[i], "--"#argument ) ) \
        { \
            if( ++i == argc ) \
                FAILED_PARSE_CLI_OPTION( "--"#argument" requires an argument.\n" ); \
            if( muxer->itunes_metadata.value ) \
                FAILED_PARSE_CLI_OPTION( "you specified --"#argument" twice.\n" ); \
            muxer->itunes_metadata.value     = argv[i]; \
            muxer->has_user_itunes_metadata = 1; \
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
        else if( !strcasecmp( argv[i], "--tempo" ) || !strcasecmp( argv[i], "--beats-per-minute" ) )
        {
            if( ++i == argc )
                FAILED_PARSE_CLI_OPTION( "%s requires an argument.\n", argv[i - 1] );
            if( muxer->itunes_metadata.beats_per_minute )
                FAILED_PARSE_CLI_OPTION( "you specified --tempo twice.\n" );
            muxer->itunes_metadata.beats_per_minute = atoi( argv[i] );
            muxer->has_user_itunes_metadata         = 1;
        }
        else if( i == argc - 2 && input_movie_number == 0 && !muxer->output->root && argv[i][0] != '-' )
        {
            /* Support positional 'lshmuxer [options] input output' invocation syntax */
            input_file_option[0].num_track_delimiter = 0;
            char *p = argv[i];
            while( *p )
                input_file_option[0].num_track_delimiter += (*p++ == '?');
            if( get_movie( &input[0], strtok( argv[i], "?" ) ) )
                FAILED_PARSE_CLI_OPTION( "failed to get input movie.\n" );
            uint32_t num_tracks = input[0].file.movie.num_tracks;
            track_option[0] = lsmash_malloc_zero( num_tracks * sizeof(track_media_option_t) );
            if( !track_option[0] )
                FAILED_PARSE_CLI_OPTION( "couldn't allocate memory.\n" );
            input_file_option[0].whole_track_option = strtok( NULL, "" );
            input[0].file.movie.movie_ID = 1;
            ++input_movie_number;
            ++i;
            output_t *output = muxer->output;
            output->root = lsmash_create_root();
            if( !output->root )
                FAILED_PARSE_CLI_OPTION( "failed to create a ROOT.\n" );
            output->file.name      = argv[i];
            muxer->optimize_pd     = 1;
            muxer->positional_mode = 1;
        }
        else
            FAILED_PARSE_CLI_OPTION( "unknown option found: %s\n", argv[i] );
    }
    muxer->argc = argc;
    if( !muxer->output->root )
        FAILED_PARSE_CLI_OPTION( "output file name is not specified.\n" );
    /* Initialize track options from input track/media parameters */
    for( int i = 0; i < muxer->num_input; i++ )
        for( uint32_t j = 0; j < input[i].file.movie.num_tracks; j++ )
        {
            input_track_t *in_track = &input[i].file.movie.track[j];
            if( !in_track->active )
                continue;
            track_option[i][j].alternate_group = in_track->track_param.alternate_group;
            if( input[i].file.is_raw )
            {
                track_option[i][j].ISO_language = 0;
                track_option[i][j].handler_name = NULL;
            }
            else
            {
                track_option[i][j].ISO_language = in_track->media.param.ISO_language;
                track_option[i][j].handler_name = in_track->media.param.media_handler_name;
            }
        }
    if( muxer->default_language )
        for( int i = 0; i < muxer->num_input; i++ )
            for( uint32_t j = 0; j < input[i].file.movie.num_tracks; j++ )
                track_option[i][j].ISO_language = muxer->default_language;
    for( int i = 0; i < muxer->num_input; i++ )
    {
        if( input_file_option[i].num_track_delimiter > (int)input[i].file.movie.num_tracks )
            FAILED_PARSE_CLI_OPTION( "more track options specified than the actual number of the tracks (%"PRIu32").\n",
                                     input[i].file.movie.num_tracks );
        if( input_file_option[i].num_track_delimiter )
        {
            track_option[i][0].raw_track_option = strtok( input_file_option[i].whole_track_option, "?" );
            for( int j = 1; j < input_file_option[i].num_track_delimiter; j++ )
                track_option[i][j].raw_track_option = strtok( NULL, "?" );
        }
    }
    if( parse_track_option( muxer ) )
        FAILED_PARSE_CLI_OPTION( "failed to parse track options.\n" );
    /* Apply global timeline editing shortcut options to the target track of the first input */
    if( muxer->positional_mode || muxer->global_edit_track
     || muxer->global_timecode || muxer->global_media_timescale || muxer->global_media_timebase
     || muxer->global_skip_num || muxer->global_delay_num || muxer->global_dts_compression )
    {
        uint32_t target_track = muxer->global_edit_track ? muxer->global_edit_track : 1;
        if( target_track > input[0].file.movie.num_tracks )
            FAILED_PARSE_CLI_OPTION( "Invalid track number %"PRIu32".\n", target_track );
        track_media_option_t *topt = &track_option[0][target_track - 1];
        topt->force_tc_edit = 1;
        if( muxer->global_timecode )
            topt->timecode_name = muxer->global_timecode;
        if( muxer->global_media_timescale )
            topt->media_timescale = muxer->global_media_timescale;
        if( muxer->global_media_timebase )
            topt->media_timebase = muxer->global_media_timebase;
        if( muxer->global_skip_num )
        {
            topt->skip_duration_num = muxer->global_skip_num;
            topt->skip_duration_den = muxer->global_skip_den;
        }
        if( muxer->global_delay_num )
        {
            topt->empty_delay_num = muxer->global_delay_num;
            topt->empty_delay_den = muxer->global_delay_den;
        }
        if( muxer->global_dts_compression )
            topt->dts_compression = 1;
    }
    lsmash_free( input_file_option );
    return 0;
#undef FAILED_PARSE_CLI_OPTION
}

/* --- Timecode and Timeline Editing --- */

static uint64_t get_gcd( uint64_t a, uint64_t b )
{
    if( !b )
        return a;
    while( 1 )
    {
        uint64_t c = a % b;
        if( !c )
            return b;
        a = b;
        b = c;
    }
}

static uint64_t get_lcm( uint64_t a, uint64_t b )
{
    if( !a )
        return 0;
    return (a / get_gcd( a, b )) * b;
}

static uint64_t get_media_timebase( lsmash_media_ts_list_t *ts_list )
{
    uint64_t timebase = ts_list->timestamp[0].cts;
    for( uint32_t i = 1; i < ts_list->sample_count; i++ )
        timebase = get_gcd( timebase, ts_list->timestamp[i].cts );
    for( uint32_t i = 0; i < ts_list->sample_count; i++ )
        timebase = get_gcd( timebase, ts_list->timestamp[i].dts );
    return timebase;
}

static double sigexp10( double value, double *exponent )
{
    *exponent = 1;
    while( value < 1 )
    {
        value     *= 10;
        *exponent /= 10;
    }
    while( value >= 10 )
    {
        value     /= 10;
        *exponent *= 10;
    }
    return value;
}

#define DOUBLE_EPSILON 5e-6
#define MATROSKA_TIMESCALE 1000000000
#define SKIP_LINE_CHARACTER( x ) ((x) == '#' || (x) == '\n' || (x) == '\r')

static double correct_fps( double fps, timecode_t *timecode )
{
    int i = 1;
    uint64_t fps_num, fps_den;
    double exponent;
    double fps_sig = sigexp10( fps, &exponent );
    while( 1 )
    {
        fps_den = i * timecode->media_timebase;
        fps_num = round( fps_den * fps_sig ) * exponent;
        if( fps_num > UINT32_MAX )
            return ERROR_MSG( "framerate correction failed.\n"
                              "Specify an appropriate timebase manually or remake timecode file.\n" );
        if( fabs( ((double)fps_num / fps_den) / exponent - fps_sig ) < DOUBLE_EPSILON )
            break;
        ++i;
    }
    if( timecode->auto_media_timescale )
    {
        timecode->media_timescale = timecode->media_timescale
                                  ? get_lcm( timecode->media_timescale, fps_num )
                                  : fps_num;
        if( timecode->media_timescale > UINT32_MAX )
            timecode->auto_media_timescale = 0;
    }
    return (double)fps_num / fps_den;
}

static int try_matroska_timescale( double *fps_array, timecode_t *timecode, uint32_t num_loops )
{
    timecode->media_timebase  = 0;
    timecode->media_timescale = MATROSKA_TIMESCALE;
    for( uint32_t i = 0; i < num_loops; i++ )
    {
        uint64_t fps_den;
        double exponent;
        double fps_sig = sigexp10( fps_array[i], &exponent );
        fps_den = round( MATROSKA_TIMESCALE / fps_sig ) / exponent;
        timecode->media_timebase = fps_den && timecode->media_timebase
                                 ? get_gcd( timecode->media_timebase, fps_den )
                                 : fps_den;
        if( timecode->media_timebase > UINT32_MAX || !timecode->media_timebase )
            return ERROR_MSG( "Automatic media timescale generation failed.\n"
                              "Specify media timescale manually.\n" );
    }
    return 0;
}

static int parse_timecode( timecode_t *timecode, uint32_t sample_count )
{
#define FAILED_PARSE_TIMECODE( ... )       \
    do                                     \
    {                                      \
        lsmash_free( fps_array );          \
        lsmash_free( timecode_array );     \
        return ERROR_MSG( __VA_ARGS__ );   \
    }                                      \
    while( 0 )
    int tcfv;
    int ret = fscanf( timecode->file, "# timecode format v%d", &tcfv );
    if( ret != 1 || (tcfv != 1 && tcfv != 2) )
        return ERROR_MSG( "Unsupported timecode format\n" );
    char buff[256];
    double *timecode_array = NULL;
    if( tcfv == 1 )
    {
        double assume_fps = 0;
        while( fgets( buff, sizeof(buff), timecode->file ) )
        {
            if( SKIP_LINE_CHARACTER( buff[0] ) )
                continue;
            if( sscanf( buff, "assume %lf", &assume_fps ) != 1
             && sscanf( buff, "Assume %lf", &assume_fps ) != 1 )
                return ERROR_MSG( "Assumed fps not found\n" );
            break;
        }
        if( assume_fps <= 0 )
            return ERROR_MSG( "Invalid assumed fps\n" );
        int64_t file_pos = lsmash_ftell( timecode->file );
        if( file_pos < 0 )
            return ERROR_MSG( "Failed to tell the position of input timecode file.\n" );
        uint32_t num_sequences = 0;
        int64_t  start, end;
        int64_t  prev_start = -1, prev_end = -1;
        double   sequence_fps;
        while( fgets( buff, sizeof(buff), timecode->file ) )
        {
            if( SKIP_LINE_CHARACTER( buff[0] ) )
                continue;
            ret = sscanf( buff, "%"SCNd64",%"SCNd64",%lf", &start, &end, &sequence_fps );
            if( ret != 3 && ret != EOF )
                return ERROR_MSG( "Invalid input timecode file\n" );
            if( start > end || start <= prev_start || end <= prev_end || sequence_fps <= 0 )
                return ERROR_MSG( "Invalid input timecode file\n" );
            prev_start = start;
            prev_end   = end;
            if( timecode->auto_media_timescale || timecode->auto_media_timebase )
                ++num_sequences;
        }
        if( lsmash_fseek( timecode->file, file_pos, SEEK_SET ) != 0 )
            return ERROR_MSG( "Failed to seek input timecode file.\n" );
        double *fps_array = lsmash_malloc( ((timecode->auto_media_timescale || timecode->auto_media_timebase) * num_sequences + 1) * sizeof(double) );
        if( !fps_array )
            return ERROR_MSG( "Failed to allocate fps array\n" );
        double corrected_assume_fps = correct_fps( assume_fps, timecode );
        if( corrected_assume_fps < 0 )
            FAILED_PARSE_TIMECODE( "Failed to correct the assumed framerate\n" );
        timecode_array = lsmash_malloc( sample_count * sizeof(double) );
        if( !timecode_array )
            FAILED_PARSE_TIMECODE( "Failed to alloc timecodes\n" );
        timecode_array[0] = 0;
        num_sequences = 0;
        uint32_t i = 0;
        while( i < sample_count - 1 && fgets( buff, sizeof(buff), timecode->file ) )
        {
            if( SKIP_LINE_CHARACTER( buff[0] ) )
                continue;
            ret = sscanf( buff, "%"SCNd64",%"SCNd64",%lf", &start, &end, &sequence_fps );
            if( ret != 3 )
                start = end = sample_count - 1;
            for( ; i < start && i < sample_count - 1; i++ )
                timecode_array[i + 1] = timecode_array[i] + 1 / corrected_assume_fps;
            if( i < sample_count - 1 )
            {
                if( timecode->auto_media_timescale || timecode->auto_media_timebase )
                    fps_array[num_sequences++] = sequence_fps;
                sequence_fps = correct_fps( sequence_fps, timecode );
                if( sequence_fps < 0 )
                    FAILED_PARSE_TIMECODE( "Failed to correct the framerate of a sequence.\n" );
                for( i = start; i <= end && i < sample_count - 1; i++ )
                    timecode_array[i + 1] = timecode_array[i] + 1 / sequence_fps;
            }
        }
        for( ; i < sample_count - 1; i++ )
            timecode_array[i + 1] = timecode_array[i] + 1 / corrected_assume_fps;
        if( timecode->auto_media_timescale || timecode->auto_media_timebase )
            fps_array[num_sequences] = assume_fps;
        if( timecode->auto_media_timebase && !timecode->auto_media_timescale )
        {
            double exponent;
            double assume_fps_sig, sequence_fps_sig;
            if( try_matroska_timescale( fps_array, timecode, num_sequences + 1 ) < 0 )
                FAILED_PARSE_TIMECODE( "Failed to try matroska timescale.\n" );
            if( lsmash_fseek( timecode->file, file_pos, SEEK_SET ) != 0 )
                FAILED_PARSE_TIMECODE( "Failed to seek input timecode file.\n" );
            assume_fps_sig = sigexp10( assume_fps, &exponent );
            corrected_assume_fps = MATROSKA_TIMESCALE / ( round( MATROSKA_TIMESCALE / assume_fps_sig ) / exponent );
            for( i = 0; i < sample_count - 1 && fgets( buff, sizeof(buff), timecode->file ); )
            {
                if( SKIP_LINE_CHARACTER( buff[0] ) )
                    continue;
                ret = sscanf( buff, "%"SCNd64",%"SCNd64",%lf", &start, &end, &sequence_fps );
                if( ret != 3 )
                    start = end = sample_count - 1;
                sequence_fps_sig = sigexp10( sequence_fps, &exponent );
                sequence_fps = MATROSKA_TIMESCALE / ( round( MATROSKA_TIMESCALE / sequence_fps_sig ) / exponent );
                for( ; i < start && i < sample_count - 1; i++ )
                    timecode_array[i + 1] = timecode_array[i] + 1 / corrected_assume_fps;
                for( i = start; i <= end && i < sample_count - 1; i++ )
                    timecode_array[i + 1] = timecode_array[i] + 1 / sequence_fps;
            }
            for( ; i < sample_count - 1; i++ )
                timecode_array[i + 1] = timecode_array[i] + 1 / corrected_assume_fps;
        }
        lsmash_free( fps_array );
    }
    else
    {
        uint32_t num_timecodes = 0;
        int64_t file_pos = lsmash_ftell( timecode->file );
        if( file_pos < 0 )
            return ERROR_MSG( "Failed to tell the position of input timecode file.\n" );
        while( fgets( buff, sizeof(buff), timecode->file ) )
        {
            if( SKIP_LINE_CHARACTER( buff[0] ) )
            {
                if( !num_timecodes )
                {
                    file_pos = lsmash_ftell( timecode->file );
                    if( file_pos < 0 )
                        return ERROR_MSG( "Failed to tell the position of input timecode file.\n" );
                }
                continue;
            }
            ++num_timecodes;
        }
        if( !num_timecodes )
            return ERROR_MSG( "No timecodes!\n" );
        if( sample_count > num_timecodes )
            return ERROR_MSG( "Lack number of timecodes.\n" );
        if( lsmash_fseek( timecode->file, file_pos, SEEK_SET ) != 0 )
            return ERROR_MSG( "Failed to seek input timecode file.\n" );
        timecode_array = lsmash_malloc( sample_count * sizeof(double) );
        if( !timecode_array )
            return ERROR_MSG( "Failed to alloc timecodes.\n" );
        uint32_t i = 0;
        if( fgets( buff, sizeof(buff), timecode->file ) )
        {
            ret = sscanf( buff, "%lf", &timecode_array[0] );
            if( ret != 1 )
            {
                lsmash_free( timecode_array );
                return ERROR_MSG( "Invalid timecode number: 0\n" );
            }
            timecode_array[i++] *= 1e-3;
            while( i < sample_count && fgets( buff, sizeof(buff), timecode->file ) )
            {
                if( SKIP_LINE_CHARACTER( buff[0] ) )
                    continue;
                ret = sscanf( buff, "%lf", &timecode_array[i] );
                timecode_array[i] *= 1e-3;
                if( ret != 1 || timecode_array[i] <= timecode_array[i - 1] )
                {
                    lsmash_free( timecode_array );
                    return ERROR_MSG( "Invalid input timecode.\n" );
                }
                ++i;
            }
        }
        if( i < sample_count )
        {
            lsmash_free( timecode_array );
            return ERROR_MSG( "Failed to get timecodes.\n" );
        }
        if( sample_count != 1 && timecode->auto_media_timescale )
        {
            double *fps_array = lsmash_malloc( (sample_count - 1) * sizeof(double) );
            if( !fps_array )
                FAILED_PARSE_TIMECODE( "Failed to allocate fps array\n" );
            for( i = 0; i < sample_count - 1; i++ )
            {
                fps_array[i] = 1 / (timecode_array[i + 1] - timecode_array[i]);
                if( timecode->auto_media_timescale )
                {
                    int j = 1;
                    uint64_t fps_num, fps_den;
                    double exponent;
                    double fps_sig = sigexp10( fps_array[i], &exponent );
                    while( 1 )
                    {
                        fps_den = j * timecode->media_timebase;
                        fps_num = round( fps_den * fps_sig ) * exponent;
                        if( fps_num > UINT32_MAX
                         || fabs( ((double)fps_num / fps_den) / exponent - fps_sig ) < DOUBLE_EPSILON )
                            break;
                        ++j;
                    }
                    timecode->media_timescale = fps_num && timecode->media_timescale
                                              ? get_lcm( timecode->media_timescale, fps_num )
                                              : fps_num;
                    if( timecode->media_timescale > UINT32_MAX )
                    {
                        timecode->auto_media_timescale = 0;
                        continue;
                    }
                }
            }
            if( timecode->auto_media_timebase && !timecode->auto_media_timescale
             && try_matroska_timescale( fps_array, timecode, sample_count - 1 ) < 0 )
                FAILED_PARSE_TIMECODE( "Failed to try matroska timescale.\n" );
            lsmash_free( fps_array );
        }
    }
    if( timecode->auto_media_timescale || timecode->auto_media_timebase )
    {
        uint64_t reduce = get_gcd( timecode->media_timebase, timecode->media_timescale );
        timecode->media_timebase  /= reduce;
        timecode->media_timescale /= reduce;
    }
    else if( timecode->media_timescale > UINT32_MAX || !timecode->media_timescale )
    {
        lsmash_free( timecode_array );
        return ERROR_MSG( "Failed to generate media timescale automatically.\n"
                          "Specify an appropriate media timescale manually.\n" );
    }
    uint32_t timescale = timecode->media_timescale;
    uint32_t timebase  = timecode->media_timebase;
    double delay_tc = timecode_array[0];
    timecode->empty_delay = ((uint64_t)(delay_tc * ((double)timescale / timebase) + 0.5)) * timebase;
    timecode->ts          = lsmash_malloc( sample_count * sizeof(uint64_t) );
    if( !timecode->ts )
    {
        lsmash_free( timecode_array );
        return ERROR_MSG( "Failed to allocate timestamps.\n" );
    }
    timecode->ts[0] = 0;
    for( uint32_t i = 1; i < sample_count; i++ )
    {
        timecode->ts[i] = ((uint64_t)((timecode_array[i] - delay_tc) * ((double)timescale / timebase) + 0.5)) * timebase;
        if( timecode->ts[i] <= timecode->ts[i - 1] )
        {
            lsmash_free( timecode_array );
            lsmash_free( timecode->ts );
            timecode->ts = NULL;
            return ERROR_MSG( "Invalid timecode.\n" );
        }
    }
    lsmash_free( timecode_array );
    return 0;
#undef FAILED_PARSE_TIMECODE
}

#undef DOUBLE_EPSILON
#undef MATROSKA_TIMESCALE
#undef SKIP_LINE_CHARACTER

static int edit_track_timeline
(
    input_t              *input,
    input_track_t        *in_track,
    track_media_option_t *track_opt
)
{
    int has_tc_edit = (track_opt->timecode_name != NULL
                    || track_opt->media_timescale != 0
                    || track_opt->media_timebase != 0
                    || track_opt->dts_compression != 0
                    || track_opt->skip_duration_num != 0
                    || track_opt->empty_delay_num != 0
                    || track_opt->force_tc_edit != 0);
    /* Why: For raw elementary streams, normalize the Fake Track's timescale and timestamps using
     * bitstream timing (well_known_fps[] / !summary->vfr) or user ?fps= override BEFORE running
     * any timecode/timeline editing pass. This ensures 1-pass raw stream timeline editing operates
     * on the exact same baseline media timeline as a container muxed from the raw stream. */
    if( input->file.is_raw )
    {
        if( !in_track->num_summaries || !in_track->summaries[0].summary
         || in_track->summaries[0].summary->summary_type != LSMASH_SUMMARY_TYPE_VIDEO )
        {
            in_track->timebase = 1;
            if( !has_tc_edit )
                return 0;
        }
        else
        {
            lsmash_video_summary_t *summary = (lsmash_video_summary_t *)in_track->summaries[0].summary;
            /* Why: For raw video streams without an explicit user framerate (?fps=), default to 25/1
             * and only adopt bitstream-signaled timing when !summary->vfr. For H.264/HEVC, additionally
             * require the rate to match well_known_fps[] so non-standard or uninitialized VUI tick
             * ratios fall back safely to 25fps CFR. */
            uint32_t new_timescale = 25;
            uint32_t new_timebase  = 1;
            if( track_opt->user_fps )
            {
                new_timescale = track_opt->fps_num << (!!summary->sample_per_field);
                new_timebase  = track_opt->fps_den;
            }
            else if( !summary->vfr )
            {
                if( lsmash_check_codec_type_identical( summary->sample_type, ISOM_CODEC_TYPE_AVC1_VIDEO )
                 || lsmash_check_codec_type_identical( summary->sample_type, ISOM_CODEC_TYPE_HVC1_VIDEO ) )
                {
                    static const struct
                    {
                        uint32_t timescale;
                        uint32_t timebase;
                    } well_known_fps[] =
                        {
                            { 24000, 1001 }, { 30000, 1001 }, { 60000, 1001 }, { 120000, 1001 }, { 72000, 1001 },
                            { 25, 1 }, { 50, 1 }, { 24, 1 }, { 30, 1 }, { 60, 1 }, { 120, 1 }, { 72, 1 }, { 0, 0 }
                        };
                    for( int i = 0; well_known_fps[i].timescale; i++ )
                        if( summary->timescale == well_known_fps[i].timescale
                         && summary->timebase  == well_known_fps[i].timebase )
                        {
                            new_timescale = summary->timescale;
                            new_timebase  = summary->timebase;
                            break;
                        }
                }
                else
                {
                    new_timescale = summary->timescale;
                    new_timebase  = summary->timebase;
                }
            }
            in_track->timebase = new_timebase;
            uint32_t orig_scale = summary->timebase ? summary->timebase : 1;
            if( new_timescale != in_track->media.param.timescale || new_timebase != orig_scale )
            {
                uint32_t track_ID = in_track->track_ID;
                lsmash_media_ts_list_t ts_list;
                if( lsmash_get_media_timestamps( input->root, track_ID, &ts_list ) )
                    return ERROR_MSG( "Failed to get media timestamps.\n" );
                uint32_t sample_count = ts_list.sample_count;
                if( sample_count == 0 )
                {
                    lsmash_delete_media_timestamps( &ts_list );
                    return 0;
                }
                lsmash_media_ts_t *timestamp = ts_list.timestamp;
                for( uint32_t i = 0; i < sample_count; i++ )
                {
                    timestamp[i].dts = (timestamp[i].dts / orig_scale) * new_timebase;
                    timestamp[i].cts = (timestamp[i].cts / orig_scale) * new_timebase;
                }
                if( in_track->last_sample_delta == (uint32_t)(UINT32_MAX * orig_scale) )
                    in_track->last_sample_delta = (uint32_t)(UINT32_MAX * new_timebase);
                else
                    in_track->last_sample_delta = (in_track->last_sample_delta / orig_scale) * new_timebase;
                if( !in_track->last_sample_delta )
                    in_track->last_sample_delta = new_timebase;
                in_track->media.param.timescale = new_timescale;
                in_track->orig_timescale        = new_timescale;
                in_track->duration = timestamp[sample_count - 1].dts + in_track->last_sample_delta;
                if( lsmash_set_media_timestamps( input->root, track_ID, &ts_list ) )
                {
                    lsmash_delete_media_timestamps( &ts_list );
                    return ERROR_MSG( "Failed to set media timestamps.\n" );
                }
                lsmash_delete_media_timestamps( &ts_list );
            }
            if( !has_tc_edit )
                return 0;
        }
    }
    if( !has_tc_edit && !track_opt->user_fps )
    {
        if( track_opt->has_cd )
        {
            lsmash_media_ts_list_t ts_list;
            if( !lsmash_get_media_timestamps( input->root, in_track->track_ID, &ts_list ) )
            {
                uint64_t tb = get_media_timebase( &ts_list );
                in_track->timebase = tb ? (uint32_t)tb : 1;
                lsmash_delete_media_timestamps( &ts_list );
            }
        }
        return 0;
    }
    uint32_t track_ID = in_track->track_ID;
    lsmash_media_ts_list_t ts_list;
    if( lsmash_get_media_timestamps( input->root, track_ID, &ts_list ) )
        return ERROR_MSG( "Failed to get media timestamps.\n" );
    uint32_t sample_count = ts_list.sample_count;
    if( sample_count == 0 )
    {
        lsmash_delete_media_timestamps( &ts_list );
        return 0;
    }
    lsmash_media_ts_t *timestamp = ts_list.timestamp;
    uint64_t timebase = get_media_timebase( &ts_list );
    if( !timebase )
        timebase = 1;
    uint32_t orig_timebase = timebase;

    /* Pure framerate override (fps=<num>[/<den>]) on a container input without timecode editing */
    if( !input->file.is_raw && track_opt->user_fps && !has_tc_edit )
    {
        uint32_t sample_per_field = 0;
        if( in_track->num_summaries && in_track->summaries[0].summary
         && in_track->summaries[0].summary->summary_type == LSMASH_SUMMARY_TYPE_VIDEO )
            sample_per_field = ((lsmash_video_summary_t *)in_track->summaries[0].summary)->sample_per_field;
        uint32_t new_timescale = track_opt->fps_num << (!!sample_per_field);
        uint32_t new_timebase  = track_opt->fps_den;
        in_track->timebase = new_timebase;
        for( uint32_t i = 0; i < sample_count; i++ )
        {
            timestamp[i].dts = (timestamp[i].dts / orig_timebase) * new_timebase;
            timestamp[i].cts = (timestamp[i].cts / orig_timebase) * new_timebase;
        }
        in_track->last_sample_delta = (in_track->last_sample_delta / orig_timebase) * new_timebase;
        if( !in_track->last_sample_delta )
            in_track->last_sample_delta = new_timebase;
        in_track->media.param.timescale = new_timescale;
        in_track->duration = timestamp[sample_count - 1].dts + in_track->last_sample_delta;
        if( lsmash_set_media_timestamps( input->root, track_ID, &ts_list ) )
        {
            lsmash_delete_media_timestamps( &ts_list );
            return ERROR_MSG( "Failed to set media timestamps.\n" );
        }
        lsmash_delete_media_timestamps( &ts_list );
        return 0;
    }

    timecode_t timecode = { 0 };
    if( track_opt->timecode_name )
    {
        timecode.file = lsmash_fopen( track_opt->timecode_name, "rb" );
        if( !timecode.file )
        {
            lsmash_delete_media_timestamps( &ts_list );
            return ERROR_MSG( "Failed to open timecode file %s.\n", track_opt->timecode_name );
        }
    }
    uint32_t timescale;
    double   timebase_convert_multiplier;
    if( track_opt->media_timescale || track_opt->media_timebase )
    {
        uint32_t orig_timescale = in_track->media.param.timescale;
        timescale = track_opt->media_timescale ? track_opt->media_timescale : orig_timescale;
        timebase  = track_opt->media_timebase  ? track_opt->media_timebase  : orig_timebase;
        if( !track_opt->media_timescale && track_opt->media_timebase && (timebase > orig_timebase) )
            timescale = timescale * ((double)timebase / orig_timebase) + 0.5;
        timebase_convert_multiplier = ((double)timescale / orig_timescale) * ((double)orig_timebase / timebase);
    }
    else
    {
        timescale = in_track->media.param.timescale;
        uint64_t reduce = get_gcd( timescale, timebase );
        timescale /= reduce;
        timebase  /= reduce;
        timebase_convert_multiplier = 1;
    }
    if( timecode.file )
    {
        timecode.auto_media_timescale = !track_opt->media_timescale;
        timecode.auto_media_timebase  = !track_opt->media_timebase;
        timecode.media_timescale      = timecode.auto_media_timescale ? 0 : timescale;
        timecode.media_timebase       = timebase;
        if( parse_timecode( &timecode, sample_count ) )
        {
            cleanup_timecode( &timecode );
            lsmash_delete_media_timestamps( &ts_list );
            return ERROR_MSG( "Failed to parse timecode file.\n" );
        }
        timescale = timecode.media_timescale;
        timebase  = timecode.media_timebase;
    }
    uint32_t sample_delay;
    if( lsmash_get_max_sample_delay( &ts_list, &sample_delay ) )
    {
        cleanup_timecode( &timecode );
        lsmash_delete_media_timestamps( &ts_list );
        return ERROR_MSG( "Failed to get maximum composition sample delay.\n" );
    }
    if( sample_delay )
        lsmash_sort_timestamps_composition_order( &ts_list );
    if( !timecode.file )
    {
        timecode.ts = lsmash_malloc( sample_count * sizeof(uint64_t) );
        if( !timecode.ts )
        {
            cleanup_timecode( &timecode );
            lsmash_delete_media_timestamps( &ts_list );
            return ERROR_MSG( "Failed to alloc timestamps\n" );
        }
        for( uint32_t i = 0; i < sample_count; i++ )
        {
            timecode.ts[i] = (timestamp[i].cts - timestamp[0].cts) / orig_timebase;
            timecode.ts[i] = ((uint64_t)(timecode.ts[i] * timebase_convert_multiplier + 0.5)) * timebase;
            if( i && (timecode.ts[i] <= timecode.ts[i - 1]) )
            {
                cleanup_timecode( &timecode );
                lsmash_delete_media_timestamps( &ts_list );
                return ERROR_MSG( "Invalid timescale conversion.\n" );
            }
        }
    }
    if( sample_delay )
    {
        uint32_t dts_compression_multiplier = track_opt->dts_compression * !track_opt->media_timescale * sample_delay + 1;
        uint64_t initial_delta = timecode.ts[1];
        timescale *= dts_compression_multiplier;
        if( dts_compression_multiplier > 1 )
            for( uint32_t i = 0; i < sample_count; i++ )
                timecode.ts[i] *= dts_compression_multiplier;
        uint64_t sample_delay_time = timecode.composition_delay = track_opt->dts_compression ? 0 : timecode.ts[sample_delay];
        for( uint32_t i = 0; i < sample_count; i++ )
            timestamp[i].cts = timecode.ts[i] + sample_delay_time;
        lsmash_sort_timestamps_decoding_order( &ts_list );
        uint64_t *prev_reordered_cts = lsmash_malloc( sample_delay * sizeof(uint64_t) );
        if( !prev_reordered_cts )
        {
            cleanup_timecode( &timecode );
            lsmash_delete_media_timestamps( &ts_list );
            return ERROR_MSG( "Failed to allocate the previous reordered CTS array.\n" );
        }
        for( uint32_t i = 0; i <= sample_delay; i++ )
        {
            if( !track_opt->dts_compression )
                timestamp[i].dts = timecode.ts[i];
            else
            {
                timestamp[i].dts = (i * initial_delta) / (!!track_opt->media_timescale * sample_delay + 1);
                if( i && (timestamp[i].dts <= timestamp[i - 1].dts) )
                {
                    lsmash_free( prev_reordered_cts );
                    cleanup_timecode( &timecode );
                    lsmash_delete_media_timestamps( &ts_list );
                    return ERROR_MSG( "Failed to do DTS compression.\n" );
                }
            }
            prev_reordered_cts[i % sample_delay] = timecode.ts[i] + sample_delay_time;
        }
        for( uint32_t i = sample_delay + 1; i < sample_count; i++ )
        {
            timestamp[i].dts = prev_reordered_cts[(i - sample_delay) % sample_delay];
            prev_reordered_cts[i % sample_delay] = timecode.ts[i] + sample_delay_time;
        }
        lsmash_free( prev_reordered_cts );
    }
    else
        for( uint32_t i = 0; i < sample_count; i++ )
            timestamp[i].cts = timestamp[i].dts = timecode.ts[i];
    if( sample_count > 1 )
    {
        in_track->last_sample_delta = timecode.ts[sample_count - 1] - timecode.ts[sample_count - 2];
        timecode.duration = timecode.ts[sample_count - 1] + in_track->last_sample_delta;
    }
    else
        timecode.duration = in_track->last_sample_delta = UINT32_MAX;
    in_track->media.param.timescale = timescale;
    in_track->timebase              = timebase;
    in_track->duration              = timecode.duration;
    in_track->edited_duration       = timecode.duration;
    in_track->composition_delay     = timecode.composition_delay;
    in_track->empty_delay           = timecode.empty_delay;
    in_track->timeline_edited       = 1;
    cleanup_timecode( &timecode );
    if( lsmash_set_media_timestamps( input->root, track_ID, &ts_list ) )
    {
        lsmash_delete_media_timestamps( &ts_list );
        return ERROR_MSG( "Failed to set media timestamps.\n" );
    }
    lsmash_delete_media_timestamps( &ts_list );
    return 0;
}

static void replace_with_valid_brand( lshmuxer_t *muxer )
{
    static const lsmash_brand_type brand_filter_list[] =
    {
        ISOM_BRAND_TYPE_3G2A,
        ISOM_BRAND_TYPE_3GG6,
        ISOM_BRAND_TYPE_3GG9,
        ISOM_BRAND_TYPE_3GP4,
        ISOM_BRAND_TYPE_3GP5,
        ISOM_BRAND_TYPE_3GP6,
        ISOM_BRAND_TYPE_3GP7,
        ISOM_BRAND_TYPE_3GP8,
        ISOM_BRAND_TYPE_3GP9,
        ISOM_BRAND_TYPE_3GR6,
        ISOM_BRAND_TYPE_3GR9,
        ISOM_BRAND_TYPE_M4A ,
        ISOM_BRAND_TYPE_M4B ,
        ISOM_BRAND_TYPE_M4V ,
        ISOM_BRAND_TYPE_AV01,
        ISOM_BRAND_TYPE_AVC1,
        ISOM_BRAND_TYPE_CDM4,
        ISOM_BRAND_TYPE_DBY1,
        ISOM_BRAND_TYPE_OPUS,
        ISOM_BRAND_TYPE_ISO2,
        ISOM_BRAND_TYPE_ISO3,
        ISOM_BRAND_TYPE_ISO4,
        ISOM_BRAND_TYPE_ISO5,
        ISOM_BRAND_TYPE_ISO6,
        ISOM_BRAND_TYPE_ISO7,
        ISOM_BRAND_TYPE_ISOM,
        ISOM_BRAND_TYPE_MP41,
        ISOM_BRAND_TYPE_MP42,
        ISOM_BRAND_TYPE_QT  ,
        0
    };
    input_t *input = muxer->input;
    uint32_t video_track_count   = 0;
    uint32_t audio_track_count   = 0;
    uint32_t video_num_summaries = 0;
    uint32_t audio_num_summaries = 0;
    for( int i = 0; i < muxer->num_input; i++ )
    {
        input_movie_t *movie = &input[i].file.movie;
        for( uint32_t j = 0; j < movie->num_tracks; j++ )
        {
            if( movie->track[j].media.param.handler_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK )
            {
                if( ++video_track_count == 1 )
                    video_num_summaries = movie->track[j].num_summaries;
            }
            else if( movie->track[j].media.param.handler_type == ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK )
            {
                if( ++audio_track_count == 1 )
                    audio_num_summaries = movie->track[j].num_summaries;
            }
        }
    }
    for( int i = 0; i < muxer->num_input; i++ )
    {
        if( input[i].file.is_raw )
            continue;
        for( uint32_t j = 0; j <= input[i].file.param.brand_count; j++ )
        {
            int       invalid = 1;
            uint32_t *brand   = j ? &input[i].file.param.brands[j - 1] : &input[i].file.param.major_brand;
            uint32_t *version = j ? NULL                               : &input[i].file.param.minor_version;
            for( int k = 0; brand_filter_list[k]; k++ )
            {
                if( *brand == brand_filter_list[k] )
                {
                    if( (muxer->frag_base_track || muxer->dash) && *brand == ISOM_BRAND_TYPE_QT )
                        break;
                    if( ((*brand >> 24) & 0xFF) == '3'
                     && ((*brand >> 16) & 0xFF) == 'g'
                     && (((*brand >>  8) & 0xFF) == 'p' || ((*brand >>  8) & 0xFF) == 'r') )
                    {
                        if( muxer->frag_base_track == 0
                         && video_track_count   <= 1 && audio_track_count   <= 1
                         && video_num_summaries <= 1 && audio_num_summaries <= 1 )
                            continue;
                        if( (*brand & 0xFF) < '6' )
                        {
                            *brand = ISOM_BRAND_TYPE_3GG6;
                            if( version )
                                *version = 0x00000700;
                        }
                        else
                            *brand = LSMASH_4CC( '3', 'g', 'g', *brand & 0xFF );
                    }
                    if( muxer->dash
                     && (*brand == ISOM_BRAND_TYPE_AVC1
                      || (((*brand >> 24) & 0xFF) == 'i'
                       && ((*brand >> 16) & 0xFF) == 's'
                       && ((*brand >>  8) & 0xFF) == 'o'
                       && ((*brand & 0xFF) == 'm' || (*brand & 0xFF) < '6'))) )
                        *brand = ISOM_BRAND_TYPE_ISO6;
                    invalid = 0;
                    break;
                }
            }
            if( invalid )
            {
                *brand = ISOM_BRAND_TYPE_MP42;
                if( version )
                    *version = 0;
            }
        }
    }
}

static void sanitize_dash_brands( uint32_t *brands, uint32_t *num_brands )
{
    uint32_t count = 0;
    for( uint32_t i = 0; i < *num_brands; i++ )
    {
        uint32_t b = brands[i];
        if( b == ISOM_BRAND_TYPE_AVC1
         || (((b >> 24) & 0xFF) == 'i'
          && ((b >> 16) & 0xFF) == 's'
          && ((b >>  8) & 0xFF) == 'o'
          && ((b & 0xFF) == 'm' || (b & 0xFF) < '6')) )
            b = ISOM_BRAND_TYPE_ISO6;
        int dup = 0;
        for( uint32_t j = 0; j < count; j++ )
            if( brands[j] == b )
            {
                dup = 1;
                break;
            }
        if( !dup )
            brands[count++] = b;
    }
    *num_brands = count;
}

static int pick_most_used_major_brand( input_t *input, output_file_t *out_file, int num_input )
{
    void *heap = lsmash_malloc( num_input * (sizeof(lsmash_brand_type) + 2 * sizeof(uint32_t)) );
    if( !heap )
        return ERROR_MSG( "failed to allocate memory.\n" );
    lsmash_brand_type *major_brand       = (lsmash_brand_type *)heap;
    uint32_t          *minor_version     = (uint32_t *)((lsmash_brand_type *)heap + num_input);
    uint32_t          *major_brand_count = minor_version + num_input;
    uint32_t           num_major_brand   = 0;
    for( int i = 0; i < num_input; i++ )
    {
        if( input[i].file.is_raw || !input[i].file.param.major_brand )
            continue;
        major_brand      [num_major_brand] = input[i].file.param.major_brand;
        minor_version    [num_major_brand] = input[i].file.param.minor_version;
        major_brand_count[num_major_brand] = 0;
        for( int j = 0; j < num_input; j++ )
        {
            if( input[j].file.is_raw || !input[j].file.param.major_brand )
                continue;
            if( (major_brand  [num_major_brand] == input[j].file.param.major_brand)
             && (minor_version[num_major_brand] == input[j].file.param.minor_version) )
            {
                if( i <= j )
                    ++major_brand_count[num_major_brand];
                else
                {
                    major_brand_count[num_major_brand] = 0;
                    --num_major_brand;
                    break;
                }
            }
        }
        ++num_major_brand;
    }
    uint32_t most_used_count = 0;
    for( uint32_t i = 0; i < num_major_brand; i++ )
        if( major_brand_count[i] > most_used_count )
        {
            most_used_count = major_brand_count[i];
            out_file->param.major_brand   = major_brand  [i];
            out_file->param.minor_version = minor_version[i];
        }
    if( most_used_count == 0 )
    {
        out_file->param.major_brand   = ISOM_BRAND_TYPE_MP42;
        out_file->param.minor_version = 0;
    }
    lsmash_free( heap );
    return 0;
}

static int check_raw_codec_brands( lshmuxer_t *muxer )
{
    uint32_t out_track_num = 0;
    for( int i = 0; i < muxer->num_input; i++ )
    {
        input_movie_t *in_movie = &muxer->input[i].file.movie;
        for( uint32_t j = 0; j < in_movie->num_tracks; j++ )
        {
            input_track_t *in_track = &in_movie->track[j];
            if( !in_track->active || in_track->num_summaries == 0 || !in_track->summaries[0].summary )
                continue;
            ++out_track_num;
            lsmash_summary_t    *summary    = in_track->summaries[0].summary;
            lsmash_codec_type_t  codec_type = summary->sample_type;
            track_media_option_t *topt      = &muxer->track_option[i][j];
            if( muxer->input[i].file.is_raw )
                display_codec_name( codec_type, out_track_num );
            /* Why: When remuxing existing container tracks without an explicit format override,
             * preserve the container's own brand list rather than injecting codec-specific brands
             * (such as 'avc1' into a pure QuickTime 'qt  ' movie), unless a track option (such as
             * Dolby Vision ?dv-profile=) explicitly requires adding 'dby1'. */
            if( !muxer->input[i].file.is_raw && !muxer->explicit_format )
            {
                if( topt->has_dv && muxer->isom )
                    add_brand( muxer, ISOM_BRAND_TYPE_DBY1 );
                continue;
            }
            if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_AVC1_VIDEO ) )
            {
                if( muxer->isom )
                    add_brand( muxer, ISOM_BRAND_TYPE_AVC1 );
            }
            else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_AV01_VIDEO ) )
            {
                if( !muxer->isom && muxer->qtff )
                    return ERROR_MSG( "the input seems AV1, at present available only for ISO Base Media file format.\n" );
                if( muxer->isom )
                {
                    add_brand( muxer, ISOM_BRAND_TYPE_AV01 );
                    for( uint32_t k = 1; k <= lsmash_count_codec_specific_data( summary ); k++ )
                    {
                        lsmash_codec_specific_t *av1 = lsmash_get_codec_specific_data( summary, k );
                        if( av1 && av1->type == LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_AV1
                         && ((lsmash_av1_specific_parameters_t *)av1->data.structured)->has_hdr10p )
                            add_brand( muxer, ISOM_BRAND_TYPE_CDM4 );
                    }
                }
            }
            else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_HVC1_VIDEO )
                  || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_HEV1_VIDEO ) )
            {
                if( !muxer->isom && muxer->qtff )
                    return ERROR_MSG( "the input seems HEVC, at present available only for ISO Base Media file format.\n" );
                if( muxer->isom )
                {
                    if( topt->has_dv )
                        add_brand( muxer, ISOM_BRAND_TYPE_DBY1 );
                    for( uint32_t k = 1; k <= lsmash_count_codec_specific_data( summary ); k++ )
                    {
                        lsmash_codec_specific_t *hevc = lsmash_get_codec_specific_data( summary, k );
                        if( hevc && hevc->type == LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_HEVC
                         && ((lsmash_hevc_specific_parameters_t *)hevc->data.structured)->has_hdr10p )
                            add_brand( muxer, ISOM_BRAND_TYPE_CDM4 );
                    }
                }
            }
            else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_VC_1_VIDEO ) )
            {
                if( !muxer->isom && muxer->qtff )
                    return ERROR_MSG( "the input seems VC-1, at present available only for ISO Base Media file format.\n" );
            }
            else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_AC_3_AUDIO )
                  || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_EC_3_AUDIO ) )
            {
                if( !muxer->isom && muxer->qtff )
                    return ERROR_MSG( "the input seems (Enhanced) AC-3, at present available only for ISO Base Media file format.\n" );
                if( muxer->isom )
                    add_brand( muxer, ISOM_BRAND_TYPE_DBY1 );
            }
            else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_DTSC_AUDIO )
                  || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_DTSE_AUDIO )
                  || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_DTSH_AUDIO )
                  || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_DTSL_AUDIO )
                  || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_DTSX_AUDIO ) )
            {
                if( !muxer->isom && muxer->qtff )
                    return ERROR_MSG( "the input seems DTS(-HD) Audio, at present available only for ISO Base Media file format.\n" );
            }
            else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_OPUS_AUDIO ) )
            {
                if( !muxer->isom && muxer->qtff )
                    return ERROR_MSG( "the input seems Opus, at present available only for ISO Base Media file format.\n" );
                if( muxer->isom )
                    add_brand( muxer, ISOM_BRAND_TYPE_OPUS );
            }
            else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_FLAC_AUDIO ) )
            {
                if( !muxer->isom && muxer->qtff )
                    return ERROR_MSG( "the input seems FLAC, at present available only for ISO Base Media file format.\n" );
            }
            else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_SAWB_AUDIO )
                  || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_SAMR_AUDIO ) )
            {
                /* Why: 3GPP AMR-NB/WB sample entries (samr/sawb) are only defined for 3GPP(2)
                 * file formats, so raw AMR inputs or explicit non-3GPP(2) output formats must be
                 * rejected when no 3GPP(2) brand is active. */
                if( (muxer->input[i].file.is_raw || muxer->explicit_format) && !muxer->brand_3gx )
                    return ERROR_MSG( "the input seems AMR-NB/WB, available for 3GPP(2) file format.\n" );
            }
        }
    }
    return 0;
}

static int set_movie_parameters( lshmuxer_t *muxer )
{
    int            num_input = muxer->num_input;
    input_t       *input     = muxer->input;
    output_t      *output    = muxer->output;
    output_file_t *out_file  = &output->file;
    if( muxer->frag_base_track )
        out_file->param.mode |= LSMASH_FILE_MODE_FRAGMENTED;
    int self_contained_segment = (muxer->dash && muxer->subseg_per_seg == 0);
    if( muxer->dash )
    {
        if( muxer->frag_base_track )
        {
            if( self_contained_segment )
                out_file->param.mode |= LSMASH_FILE_MODE_INDEX;
            else
                out_file->param.mode &= ~LSMASH_FILE_MODE_MEDIA;
            out_file->param.mode |= LSMASH_FILE_MODE_SEGMENT;
        }
        else
            WARNING_MSG( "--dash requires --fragment.\n" );
    }
    if( muxer->max_chunk_duration_in_ms )
        out_file->param.max_chunk_duration = muxer->max_chunk_duration_in_ms * 1e-3;
    if( muxer->max_chunk_size )
        out_file->param.max_chunk_size = muxer->max_chunk_size;

    int all_inputs_raw = 1;
    for( int i = 0; i < num_input; i++ )
        if( !input[i].file.is_raw )
            all_inputs_raw = 0;

    if( all_inputs_raw || muxer->explicit_format )
    {
        if( decide_brands( muxer ) < 0 )
            return ERROR_MSG( "failed to set up output file format.\n" );
        if( muxer->timeline_shift && !muxer->qtff && muxer->isom_version < 4 )
            return ERROR_MSG( "timeline shift requires --file-format mov, or --isom-version 4 or later.\n" );
        if( check_raw_codec_brands( muxer ) < 0 )
            return -1;
        /* Why: Only MPEG-DASH segmentation (--dash) requires ISO Base Media version 6 ('iso6') for
         * Segment Index Box ('sidx') and Track Fragment Base Media Decode Time Box ('tfdt');
         * standard Movie Fragments (--fragment without --dash) retain the base MP4/ISOBMFF brands. */
        if( self_contained_segment )
        {
            muxer->major_brand   = ISOM_BRAND_TYPE_DASH;
            muxer->minor_version = 0;
            if( muxer->num_of_brands < MAX_NUM_OF_BRANDS )
            {
                memmove( &muxer->brands[1], &muxer->brands[0], muxer->num_of_brands * sizeof(uint32_t) );
                muxer->brands[0] = ISOM_BRAND_TYPE_DASH;
                ++muxer->num_of_brands;
            }
            add_brand( muxer, ISOM_BRAND_TYPE_ISO6 );
        }
        else if( muxer->dash && muxer->isom )
            add_brand( muxer, ISOM_BRAND_TYPE_ISO6 );
        if( muxer->dash )
            sanitize_dash_brands( muxer->brands, &muxer->num_of_brands );
        lsmash_brand_type *output_brands = lsmash_malloc_zero( muxer->num_of_brands * sizeof(lsmash_brand_type) );
        if( !output_brands )
            return ERROR_MSG( "failed to allocate brands for an output file.\n" );
        memcpy( output_brands, muxer->brands, muxer->num_of_brands * sizeof(lsmash_brand_type) );
        out_file->param.major_brand   = muxer->major_brand;
        out_file->param.minor_version = muxer->minor_version;
        out_file->param.brand_count   = muxer->num_of_brands;
        out_file->param.brands        = output_brands;
    }
    else
    {
        muxer->isom = 1;
        if( check_raw_codec_brands( muxer ) < 0 )
            return -1;
        if( muxer->dash )
            add_brand( muxer, ISOM_BRAND_TYPE_ISO6 );
        replace_with_valid_brand( muxer );
        if( self_contained_segment )
        {
            out_file->param.major_brand   = ISOM_BRAND_TYPE_DASH;
            out_file->param.minor_version = 0;
        }
        else if( pick_most_used_major_brand( input, out_file, num_input ) < 0 )
            return ERROR_MSG( "failed to pick the most used major brand.\n" );
        uint32_t num_input_brands = num_input + muxer->num_of_brands + (self_contained_segment ? 1 : 0);
        for( int i = 0; i < num_input; i++ )
            num_input_brands += input[i].file.param.brand_count;
        lsmash_brand_type *input_brands = lsmash_malloc( num_input_brands * sizeof(lsmash_brand_type) );
        if( !input_brands )
            return ERROR_MSG( "failed to allocate brands for an input file.\n" );
        num_input_brands = 0;
        if( self_contained_segment )
            input_brands[num_input_brands++] = ISOM_BRAND_TYPE_DASH;
        for( int i = 0; i < num_input; i++ )
        {
            if( input[i].file.is_raw )
                continue;
            if( input[i].file.param.major_brand )
                input_brands[num_input_brands++] = input[i].file.param.major_brand;
            for( uint32_t j = 0; j < input[i].file.param.brand_count; j++ )
                if( input[i].file.param.brands[j] )
                    input_brands[num_input_brands++] = input[i].file.param.brands[j];
        }
        for( uint32_t i = 0; i < muxer->num_of_brands; i++ )
            input_brands[num_input_brands++] = muxer->brands[i];
        lsmash_brand_type *output_brands = lsmash_malloc_zero( num_input_brands * sizeof(lsmash_brand_type) );
        if( !output_brands )
        {
            lsmash_free( input_brands );
            return ERROR_MSG( "failed to allocate brands for an output file.\n" );
        }
        uint32_t num_output_brands = 0;
        for( uint32_t i = 0; i < num_input_brands; i++ )
        {
            output_brands[num_output_brands] = input_brands[i];
            for( uint32_t j = 0; j < num_output_brands; j++ )
                if( output_brands[num_output_brands] == output_brands[j] )
                {
                    --num_output_brands;
                    break;
                }
            ++num_output_brands;
        }
        lsmash_free( input_brands );
        if( muxer->dash )
            sanitize_dash_brands( output_brands, &num_output_brands );
        out_file->param.brand_count = num_output_brands;
        out_file->param.brands      = output_brands;
        for( uint32_t i = 0; i < num_output_brands; i++ )
        {
            if( output_brands[i] == ISOM_BRAND_TYPE_QT )
                muxer->qtff = 1;
            if( output_brands[i] == ISOM_BRAND_TYPE_M4A
             || output_brands[i] == ISOM_BRAND_TYPE_M4B
             || output_brands[i] == ISOM_BRAND_TYPE_M4P
             || output_brands[i] == ISOM_BRAND_TYPE_M4V )
                muxer->itunes_movie = 1;
        }
    }

    out_file->fh = lsmash_set_file( output->root, &out_file->param );
    if( !out_file->fh )
        return ERROR_MSG( "failed to add an output file into a ROOT.\n" );
    out_file->seg_param = out_file->param;
    if( muxer->chap_file )
    {
        if( all_inputs_raw || muxer->explicit_format )
        {
            /* Why: Reference chapter tracks ('chap' track reference) are supported only in QuickTime
             * and iTunes MP4 movie formats, and are disallowed when a 3GPP brand is active. */
            muxer->ref_chap_available = (muxer->qtff || muxer->itunes_movie) && (muxer->brand_3gx != 1);
        }
        else
        {
            for( uint32_t i = 0; i < out_file->param.brand_count; i++ )
            {
                uint32_t brand = out_file->param.brands[i];
                if( ((brand >> 24) & 0xFF) == '3'
                 && ((brand >> 16) & 0xFF) == 'g'
                 && ((brand >>  8) & 0xFF) == 'p' )
                    break;
                if( brand == ISOM_BRAND_TYPE_QT  || brand == ISOM_BRAND_TYPE_M4A
                 || brand == ISOM_BRAND_TYPE_M4B || brand == ISOM_BRAND_TYPE_M4P
                 || brand == ISOM_BRAND_TYPE_M4V )
                {
                    muxer->ref_chap_available = 1;
                    break;
                }
            }
        }
    }
    /* Why: When timeline editing is performed on a single container input, preserve the input
     * movie header parameters (mvhd) and, if the movie has a single track, set mvhd->timescale to
     * that track's original pre-edit media timescale (matching container timeline editing semantics
     * where mvhd parameters are established prior to media timeline modification). */
    int container_timeline_edited = 0;
    for( int i = 0; i < muxer->num_input; i++ )
        if( !input[i].file.is_raw )
            for( uint32_t j = 0; j < input[i].file.movie.num_tracks; j++ )
                if( input[i].file.movie.track[j].active && input[i].file.movie.track[j].timeline_edited )
                    container_timeline_edited = 1;
    if( container_timeline_edited && muxer->num_input == 1 )
        out_file->movie.param = input[0].file.movie.param;
    else
        lsmash_initialize_movie_parameters( &out_file->movie.param );
    if( muxer->movie_timescale )
        out_file->movie.param.timescale = muxer->movie_timescale;
    /* Why: For a single-track movie without a multi-track timescale requirement (or a single raw
     * elementary stream input), match the movie header timescale directly to the sole active track's
     * media timescale so edit list durations need no cross-timescale rounding. */
    if( out_file->movie.num_tracks == 1 && (!muxer->movie_timescale || (all_inputs_raw && muxer->num_input == 1)) )
    {
        for( int i = 0; i < muxer->num_input; i++ )
        {
            input_movie_t *in_movie = &input[i].file.movie;
            for( uint32_t j = 0; j < in_movie->num_tracks; j++ )
                if( in_movie->track[j].active )
                {
                    out_file->movie.param.timescale = (container_timeline_edited && !input[i].file.is_raw)
                                                    ? in_movie->track[j].orig_timescale
                                                    : in_movie->track[j].media.param.timescale;
                    break;
                }
        }
    }
    if( lsmash_set_movie_parameters( output->root, &out_file->movie.param ) )
        return ERROR_MSG( "failed to set movie parameters.\n" );
    if( muxer->copyright_notice
     && lsmash_set_copyright( output->root, 0, muxer->copyright_language, muxer->copyright_notice ) )
        return ERROR_MSG( "failed to set a copyright notice for the entire movie.\n" );
    return 0;
}

static int set_itunes_metadata( lshmuxer_t *muxer )
{
    output_t *output         = muxer->output;
    input_t  *input          = muxer->input;
    int       num_input      = muxer->num_input;
    int       all_inputs_raw = 1;
    for( int i = 0; i < num_input; i++ )
    {
        if( !input[i].file.is_raw )
            all_inputs_raw = 0;
        for( uint32_t j = 0; j < input[i].file.movie.num_itunes_metadata; j++ )
            if( lsmash_set_itunes_metadata( output->root, input[i].file.movie.itunes_metadata[j] ) )
            {
                WARNING_MSG( "failed to set an iTunes metadata.\n" );
                continue;
            }
    }
    if( !muxer->itunes_movie && !muxer->has_user_itunes_metadata )
        return 0;
    itunes_metadata_t *metadata = &muxer->itunes_metadata;
#define SET_ITUNES_METADATA( item, type, value ) \
    if( (value) \
     && lsmash_set_itunes_metadata( output->root, (lsmash_itunes_metadata_t){ item, ITUNES_METADATA_TYPE_NONE, { .type = (value) }, NULL, NULL } ) ) \
        return -1
    /* Why: Only synthesize a default ITUNES_METADATA_ITEM_ENCODING_TOOL ("L-SMASH") tag when
     * muxing raw inputs (or an explicit format override with no inherited container metadata);
     * container inputs already carry their own iTunes metadata list and must not get a duplicate tag. */
    if( muxer->itunes_movie && all_inputs_raw )
        SET_ITUNES_METADATA( ITUNES_METADATA_ITEM_ENCODING_TOOL, string, "L-SMASH" );
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

static int set_starting_point( input_t *input, input_track_t *in_track, uint32_t seek_point, int consider_rap )
{
    if( seek_point == 0 )
        return 0;
    uint32_t rap_number;
    if( lsmash_get_closest_random_accessible_point_from_media_timeline( input->root, in_track->track_ID, 1, &rap_number ) )
    {
        if( consider_rap )
            return ERROR_MSG( "failed to get the first random accessible point.\n" );
        else
        {
            WARNING_MSG( "no random access point!\n" );
            in_track->current_sample_number = seek_point;
            return 0;
        }
    }
    uint64_t rap_dts;
    uint64_t rap_cts;
    uint32_t ctd_shift;
    if( lsmash_get_dts_from_media_timeline( input->root, in_track->track_ID, rap_number, &rap_dts ) )
        return ERROR_MSG( "failed to get DTS of the first random accessible sample of seek point.\n" );
    if( lsmash_get_cts_from_media_timeline( input->root, in_track->track_ID, rap_number, &rap_cts ) )
        return ERROR_MSG( "failed to get CTS of the first random accessible sample of seek point.\n" );
    if( lsmash_get_composition_to_decode_shift_from_media_timeline( input->root, in_track->track_ID, &ctd_shift ) )
        return ERROR_MSG( "failed to get composition to decode timeline shift.\n" );
    in_track->composition_delay = rap_cts - rap_dts + ctd_shift;
    if( lsmash_get_closest_random_accessible_point_from_media_timeline( input->root, in_track->track_ID, seek_point, &rap_number ) )
        return ERROR_MSG( "failed to get a random accessible point.\n" );
    if( rap_number != seek_point )
    {
        WARNING_MSG( "starting point you specified is not a random accessible point.\n" );
        if( consider_rap )
        {
            if( lsmash_get_cts_from_media_timeline( input->root, in_track->track_ID, rap_number, &rap_cts ) )
                return ERROR_MSG( "failed to get CTS of the closest and past random accessible sample of starting point.\n" );
            uint64_t seek_cts;
            if( lsmash_get_cts_from_media_timeline( input->root, in_track->track_ID, seek_point, &seek_cts ) )
                return ERROR_MSG( "failed to get CTS of starting point.\n" );
            if( rap_cts < seek_cts )
                in_track->skip_duration = seek_cts - rap_cts;
        }
    }
    in_track->current_sample_number = consider_rap ? rap_number : seek_point;
    return 0;
}

static void exclude_invalid_output_track
(
    output_t      *output,
    output_track_t *out_track,
    input_movie_t *in_movie,
    input_track_t *in_track,
    const char    *message,
    ...
)
{
    REFRESH_CONSOLE;
    eprintf( "[Warning] in %"PRIu32"/%"PRIu32" -> out %"PRIu32": ", in_movie->movie_ID, in_track->track_ID, out_track->track_ID );
    va_list args;
    va_start( args, message );
    vfprintf( stderr, message, args );
    va_end( args );
    lsmash_delete_track( output->root, out_track->track_ID );
    --output->file.movie.num_tracks;
    in_track->active = 0;
}

static int apply_summary_track_options
(
    lshmuxer_t           *muxer,
    input_t              *in,
    input_track_t        *in_track,
    output_track_t       *out_track,
    track_media_option_t *track_opt
)
{
    if( in_track->num_summaries == 0 || !in_track->summaries[0].summary )
        return 0;
    /* Why: For raw elementary streams, user track options (par=, btrt, dovi, st3d, prhd, projection,
     * sbr, SA3D) apply only to the initial sample entry (first sequence), whereas any subsequent
     * sample entries generated by mid-stream sequence parameter changes retain their bitstream-derived
     * parameters unmodified. */
    uint32_t num_target_summaries = in->file.is_raw ? 1 : in_track->num_summaries;
    for( uint32_t k = 0; k < num_target_summaries; k++ )
    {
        lsmash_summary_t *base_summary = in_track->summaries[k].summary;
        if( !base_summary )
            continue;
        if( base_summary->summary_type == LSMASH_SUMMARY_TYPE_VIDEO )
        {
            lsmash_video_summary_t *summary = (lsmash_video_summary_t *)base_summary;
            if( track_opt->par_h && track_opt->par_v )
            {
                summary->par_h = track_opt->par_h;
                summary->par_v = track_opt->par_v;
            }
            if( k == 0 && (in->file.is_raw || (track_opt->par_h && track_opt->par_v)) )
            {
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
                out_track->track_param.display_width  = display_width  <= UINT32_MAX ? display_width  : UINT32_MAX;
                out_track->track_param.display_height = display_height <= UINT32_MAX ? display_height : UINT32_MAX;
            }
            /* Why: Append MPEG4BitRateBox (btrt) for raw AVC1/HVC1 streams only when fixed-rate
             * bitstream timing is used (neither ?fps= override nor VFR is active). */
            if( in->file.is_raw && !track_opt->user_fps && !summary->vfr
             && (lsmash_check_codec_type_identical( summary->sample_type, ISOM_CODEC_TYPE_AVC1_VIDEO )
              || lsmash_check_codec_type_identical( summary->sample_type, ISOM_CODEC_TYPE_HVC1_VIDEO )) )
            {
                lsmash_codec_specific_t *bitrate = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_H264_BITRATE,
                                                                                      LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
                if( bitrate )
                    lsmash_add_codec_specific_data( base_summary, bitrate );
                lsmash_destroy_codec_specific_data( bitrate );
            }
            if( track_opt->has_dv )
            {
                lsmash_codec_specific_t *dovi = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_HEVC_DOVI,
                                                                                   LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
                if( !dovi )
                    return ERROR_MSG( "Failed to allocate Dolby Vision configuration box.\n" );
                uint32_t tb = out_track->timebase ? out_track->timebase
                            : (track_opt->user_fps ? track_opt->fps_den : (summary->timebase ? summary->timebase : 1));
                lsmash_dovi_set_config( (lsmash_hevc_dovi_t *)dovi->data.structured, track_opt->dv_profile,
                                        track_opt->dv_bl_signal_compatibility_id, out_track->media_param.timescale, tb,
                                        summary->width, summary->height );
                lsmash_add_codec_specific_data( base_summary, dovi );
                lsmash_destroy_codec_specific_data( dovi );
            }
            if( track_opt->has_stereo_mode )
            {
                lsmash_codec_specific_t *st3d = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_ST3D,
                                                                                   LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
                if( !st3d )
                    return ERROR_MSG( "failed to allocate Stereoscopic 3D Video Box\n" );
                lsmash_st3d_t *data = (lsmash_st3d_t *)st3d->data.structured;
                if( !strcasecmp( track_opt->stereo_mode, "monoscopic" ) )
                    data->stereo_mode = 0;
                else if( !strcasecmp( track_opt->stereo_mode, "top-bottom" ) )
                    data->stereo_mode = 1;
                else if( !strcasecmp( track_opt->stereo_mode, "left-right" ) )
                    data->stereo_mode = 2;
                else if( !strcasecmp( track_opt->stereo_mode, "custom" ) )
                    data->stereo_mode = 3;
                else if( !strcasecmp( track_opt->stereo_mode, "right-left" ) )
                    data->stereo_mode = 4;
                lsmash_add_codec_specific_data( base_summary, st3d );
                lsmash_destroy_codec_specific_data( st3d );
            }
            if( track_opt->has_ypr )
            {
                lsmash_codec_specific_t *prhd = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_PRHD,
                                                                                   LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
                if( !prhd )
                    return ERROR_MSG( "failed to allocate Projection Header\n" );
                lsmash_prhd_t *data = (lsmash_prhd_t *)prhd->data.structured;
                data->yaw   = (int32_t)(track_opt->yaw   * (1 << 16));
                data->pitch = (int32_t)(track_opt->pitch * (1 << 16));
                data->roll  = (int32_t)(track_opt->roll  * (1 << 16));
                lsmash_add_codec_specific_data( base_summary, prhd );
                lsmash_destroy_codec_specific_data( prhd );
            }
            if( track_opt->projection )
            {
                if( strstr( track_opt->projection, "equirectangular" ) )
                {
                    lsmash_codec_specific_t *equi = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_EQUI,
                                                                                       LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
                    if( !equi )
                        return ERROR_MSG( "failed to allocate the Projection Box\n" );
                    lsmash_add_codec_specific_data( base_summary, equi );
                    lsmash_destroy_codec_specific_data( equi );
                }
                else if( strstr( track_opt->projection, "cubemap" ) )
                {
                    lsmash_codec_specific_t *cbmp = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_CBMP,
                                                                                       LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
                    if( !cbmp )
                        return ERROR_MSG( "failed to allocate the Projection Box\n" );
                    lsmash_add_codec_specific_data( base_summary, cbmp );
                    lsmash_destroy_codec_specific_data( cbmp );
                }
                else
                    return ERROR_MSG( "unsupported projection mode %s\n", track_opt->projection );
            }
            if( in->file.is_raw )
            {
                out_track->media_param.roll_grouping = 1;
                out_track->media_param.rap_grouping  = muxer->isom_version >= 6;
            }
        }
        else if( base_summary->summary_type == LSMASH_SUMMARY_TYPE_AUDIO )
        {
            lsmash_audio_summary_t *summary = (lsmash_audio_summary_t *)base_summary;
            if( track_opt->sbr )
            {
                if( lsmash_mp4sys_get_object_type_indication( base_summary ) != MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3 )
                    return ERROR_MSG( "--sbr is only valid with MPEG-4 Audio.\n" );
                summary->sbr_mode = MP4A_AAC_SBR_BACKWARD_COMPATIBLE;
                if( lsmash_setup_AudioSpecificConfig( summary ) )
                    return ERROR_MSG( "failed to set SBR mode.\n" );
            }
            if( track_opt->has_spatial_audio )
            {
                if( track_opt->ambisonic_order < 0 )
                    return ERROR_MSG( "ambisonic order must be a positive integer\n" );
                uint32_t ambisonic_channels = (track_opt->ambisonic_order + 1) * (track_opt->ambisonic_order + 1);
                lsmash_boolean_t head_locked_stereo = LSMASH_BOOLEAN_FALSE;
                if( ambisonic_channels != summary->channels )
                {
                    if( ambisonic_channels + 2 == summary->channels )
                        head_locked_stereo = LSMASH_BOOLEAN_TRUE;
                    else
                        return ERROR_MSG( "ambisonic order's expected channel count does not match actual channel count\n" );
                }
                lsmash_codec_specific_t *SA3D = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_SA3D,
                                                                                   LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
                if( !SA3D )
                    return ERROR_MSG( "failed to allocate Spatial Audio Box\n" );
                lsmash_SA3D_t *data = (lsmash_SA3D_t *)SA3D->data.structured;
                data->version                    = 0;
                data->head_locked_stereo         = head_locked_stereo;
                data->ambisonic_type             = SA3D_AMBISONIC_TYPE_PERIPHONIC;
                data->ambisonic_order            = (uint32_t)track_opt->ambisonic_order;
                data->ambisonic_channel_ordering = SA3D_AMBISONIC_CHANNEL_ORDERING_ACN;
                data->ambisonic_normalization    = SA3D_AMBISONIC_NORMALIZATION_SN3D;
                data->num_channels               = ambisonic_channels;
                for( uint32_t c = 0; c < ambisonic_channels; c++ )
                    data->channel_map[c] = c;
                lsmash_add_codec_specific_data( base_summary, SA3D );
                lsmash_destroy_codec_specific_data( SA3D );
            }
            if( in->file.is_raw )
                out_track->media_param.roll_grouping = !in_track->lpcm && (muxer->isom_version >= 2 || muxer->qtff);
            out_track->priming_samples = track_opt->encoder_delay;
        }
    }
    return 0;
}

static int prepare_output( lshmuxer_t *muxer )
{
    input_t        *input     = muxer->input;
    output_t       *output    = muxer->output;
    output_movie_t *out_movie = &output->file.movie;
    if( muxer->dry_run )
    {
        output->file.open  = dry_open_file;
        output->file.close = dry_close_file;
    }
    else
    {
        output->file.open  = lsmash_open_file;
        output->file.close = lsmash_close_file;
    }
    if( output->file.open( output->file.name, 0, &output->file.param ) < 0 )
        return ERROR_MSG( "failed to open an output file.\n" );
    for( int i = 0; i < muxer->num_input; i++ )
        out_movie->num_tracks += input[i].file.movie.num_tracks;
    for( int i = 0; i < muxer->num_input; i++ )
    {
        input_movie_t *in_movie = &input[i].file.movie;
        for( uint32_t j = 0; j < in_movie->num_tracks; j++ )
        {
            if( muxer->track_option[i][j].remove )
                in_movie->track[j].active = 0;
            if( !in_movie->track[j].active )
            {
                --out_movie->num_tracks;
                continue;
            }
            if( edit_track_timeline( &input[i], &in_movie->track[j], &muxer->track_option[i][j] ) < 0 )
                return ERROR_MSG( "failed to edit media timeline.\n" );
        }
    }
    if( out_movie->num_tracks == 0 )
        return ERROR_MSG( "there is no media that can be stored in output movie.\n" );
    if( set_movie_parameters( muxer ) < 0 )
        return ERROR_MSG( "failed to set output movie parameters.\n" );
    if( set_itunes_metadata( muxer ) < 0 )
        return ERROR_MSG( "failed to set iTunes metadata.\n" );
    out_movie->track = lsmash_malloc_zero( out_movie->num_tracks * sizeof(output_track_t) );
    if( !out_movie->track )
        return ERROR_MSG( "failed to alloc output tracks.\n" );
    out_movie->current_track_number = 1;
    for( int i = 0; i < muxer->num_input; i++ )
    {
        input_movie_t *in_movie = &input[i].file.movie;
        for( uint32_t j = 0; j < in_movie->num_tracks; j++ )
        {
            track_media_option_t *current_track_opt = &muxer->track_option[i][j];
            input_track_t *in_track = &in_movie->track[j];
            if( !in_track->active )
                continue;
            output_track_t *out_track = &out_movie->track[out_movie->current_track_number - 1];
            out_track->summary_remap = lsmash_malloc_zero( in_track->num_summaries * sizeof(uint32_t) );
            if( !out_track->summary_remap )
                return ERROR_MSG( "failed to create summary mapping for a track.\n" );
            out_track->track_ID = lsmash_create_track( output->root, in_track->media.param.handler_type );
            if( !out_track->track_ID )
                return ERROR_MSG( "failed to create a track.\n" );
            out_track->timebase = in_track->timebase;
            out_track->lpcm     = in_track->lpcm;
            if( input[i].file.is_raw )
            {
                /* Why: Initialize track_param and media_param from scratch for raw elementary streams
                 * instead of copying from the Fake Track. Copying the Fake Track's media parameters
                 * would inherit no_sample_dependency_table = 1 (because the Fake Track has no sdtp box),
                 * which suppresses SampleDependencyTypeBox (sdtp) generation in lsmash_set_media_parameters(). */
                lsmash_initialize_track_parameters( &out_track->track_param );
                out_track->track_param.mode = ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
                if( !current_track_opt->disable )
                    out_track->track_param.mode |= ISOM_TRACK_ENABLED;
                if( muxer->qtff )
                    out_track->track_param.mode |= QT_TRACK_IN_POSTER;
                lsmash_initialize_media_parameters( &out_track->media_param );
                out_track->media_param.handler_type = in_track->media.param.handler_type;
                out_track->media_param.timescale    = in_track->media.param.timescale;
                out_track->media_param.ISO_language = current_track_opt->ISO_language;
                if( current_track_opt->handler_name )
                    out_track->media_param.media_handler_name = current_track_opt->handler_name;
                else if( in_track->media.param.handler_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK )
                    out_track->media_param.media_handler_name = "L-SMASH Video Handler";
                else if( in_track->media.param.handler_type == ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK )
                    out_track->media_param.media_handler_name = "L-SMASH Audio Handler";
            }
            else
            {
                out_track->track_param = in_track->track_param;
                out_track->media_param = in_track->media.param;
                if( current_track_opt->disable )
                    out_track->track_param.mode &= ~ISOM_TRACK_ENABLED;
                out_track->media_param.ISO_language       = current_track_opt->ISO_language;
                out_track->media_param.media_handler_name = current_track_opt->handler_name;
            }
            out_track->track_param.alternate_group = current_track_opt->alternate_group;
            /* Why: When editing the timeline of an existing container track, preserve any existing
             * compact sample size table ('stz2') setting on that track in addition to honoring
             * an explicit --compact-size-table flag. */
            if( in_track->timeline_edited && !input[i].file.is_raw )
                out_track->media_param.compact_sample_size_table |= muxer->compact_size_table;
            else
                out_track->media_param.compact_sample_size_table = muxer->compact_size_table;
            out_track->track_param.track_ID = out_track->track_ID;
            if( apply_summary_track_options( muxer, &input[i], in_track, out_track, current_track_opt ) < 0 )
                return -1;
            if( current_track_opt->copyright_notice
             && lsmash_set_copyright( output->root, out_track->track_ID, current_track_opt->copyright_language, current_track_opt->copyright_notice ) )
                return ERROR_MSG( "failed to set a copyright notice.\n" );
            if( lsmash_set_track_parameters( output->root, out_track->track_ID, &out_track->track_param ) < 0 )
            {
                exclude_invalid_output_track( output, out_track, in_movie, in_track, "failed to set track parameters.\n" );
                continue;
            }
            if( lsmash_set_media_parameters( output->root, out_track->track_ID, &out_track->media_param ) < 0 )
            {
                exclude_invalid_output_track( output, out_track, in_movie, in_track, "failed to set media parameters.\n" );
                continue;
            }
            lsmash_data_reference_t data_ref = { .index = 1, .location = NULL };
            if( lsmash_create_data_reference( output->root, out_track->track_ID, &data_ref, output->file.fh ) < 0 )
                return ERROR_MSG( "failed to create a data reference for output movie.\n" );
            uint32_t valid_summary_count = 0;
            for( uint32_t k = 0; k < in_track->num_summaries; k++ )
            {
                if( !in_track->summaries[k].active )
                {
                    out_track->summary_remap[k] = 0;
                    continue;
                }
                lsmash_summary_t *summary = in_track->summaries[k].summary;
                summary->data_ref_index = 1;
                if( lsmash_add_sample_entry( output->root, out_track->track_ID, summary ) == 0 )
                {
                    WARNING_MSG( "failed to append a summary.\n" );
                    lsmash_cleanup_summary( summary );
                    in_track->summaries[k].summary = NULL;
                    in_track->summaries[k].active  = 0;
                    out_track->summary_remap[k]    = 0;
                    continue;
                }
                out_track->summary_remap[k] = ++valid_summary_count;
            }
            if( valid_summary_count == 0 )
            {
                exclude_invalid_output_track( output, out_track, in_movie, in_track, "failed to append all summaries.\n" );
                continue;
            }
            out_track->last_sample_delta = in_track->last_sample_delta;
            if( set_starting_point( &input[i], in_track, current_track_opt->seek, current_track_opt->consider_rap ) < 0 )
            {
                exclude_invalid_output_track( output, out_track, in_movie, in_track, "failed to set starting point.\n" );
                continue;
            }
            out_track->current_sample_number = 1;
            out_track->skip_dt_interval      = 0;
            out_track->last_sample_dts       = 0;
            ++out_movie->current_track_number;
        }
        lsmash_destroy_children( lsmash_file_as_box( input[i].file.fh ) );
    }
    if( out_movie->num_tracks == 0 )
        return ERROR_MSG( "failed to create the output movie.\n" );
    out_movie->current_track_number = 1;
    output->current_seg_number      = 1;
    return 0;
}

static void set_reference_chapter_track( lshmuxer_t *muxer )
{
    if( muxer->ref_chap_available )
        lsmash_create_reference_chapter_track( muxer->output->root, muxer->chap_track, muxer->chap_file );
}

static int flush_movie_fragment( lshmuxer_t *muxer )
{
    input_t        *inputs    = muxer->input;
    output_t       *output    = muxer->output;
    output_movie_t *out_movie = &output->file.movie;
    uint32_t out_current_track_number = 1;
    for( int i = 1; i <= muxer->num_input; i++ )
    {
        input_t       *in       = &inputs[i - 1];
        input_movie_t *in_movie = &in->file.movie;
        for( uint32_t j = 1; j <= in_movie->num_tracks; j++ )
        {
            input_track_t *in_track = &in_movie->track[j - 1];
            if( !in_track->active )
                continue;
            output_track_t *out_track = &out_movie->track[out_current_track_number - 1];
            if( !in_track->reach_end_of_media_timeline )
            {
                lsmash_sample_t sample;
                if( lsmash_get_sample_info_from_media_timeline( in->root, in_track->track_ID, in_track->current_sample_number, &sample ) < 0 )
                    return ERROR_MSG( "failed to get the information of the next sample.\n" );
                uint64_t sample_dts = sample.dts - out_track->skip_dt_interval;
                if( lsmash_flush_pooled_samples( output->root, out_track->track_ID, sample_dts - out_track->last_sample_dts ) < 0 )
                    return ERROR_MSG( "failed to flush the rest of samples in a fragment.\n" );
            }
            else if( lsmash_flush_pooled_samples( output->root, out_track->track_ID, out_track->last_sample_delta ) < 0 )
                return ERROR_MSG( "failed to flush the rest of samples in a fragment.\n" );
            if( ++out_current_track_number > out_movie->num_tracks )
                break;
        }
    }
    return 0;
}

static int moov_to_front_callback( void *param, uint64_t written_movie_size, uint64_t total_movie_size )
{
    static uint32_t progress_pos = 0;
    if( (written_movie_size >> 24) <= progress_pos )
        return 0;
    REFRESH_CONSOLE;
    eprintf( "Finalizing: [%5.2lf%%]\r", total_movie_size ? ((double)written_movie_size / total_movie_size) * 100.0 : 0 );
    progress_pos = written_movie_size >> 24;
    return 0;
}

static lsmash_adhoc_remux_t moov_to_front =
{
    .func        = moov_to_front_callback,
    .buffer_size = 4 * 1024 * 1024,
    .param       = NULL
};

static int open_media_segment( output_t *output, lsmash_file_parameters_t *seg_param )
{
    output_file_t *out_file = &output->file;
    int out_file_name_length = strlen( out_file->name );
    const char *end = &out_file->name[out_file_name_length];
    const char *p   = end;
    while( p >= out_file->name && *p != '.' && *p != '/' && *p != '\\' )
        --p;
    if( p < out_file->name )
        ++p;
    if( *p != '.' )
        p = end;
    int suffix_length = 1;
    for( uint32_t i = output->current_seg_number; i; i /= 10 )
        ++suffix_length;
    int seg_name_length   = out_file_name_length + suffix_length;
    int suffixless_length = p - out_file->name;
    char *seg_name = lsmash_malloc( (seg_name_length + 1) * sizeof(char) );
    if( !seg_name )
        return ERROR_MSG( "failed to allocate to store a segment filename.\n" );
    seg_name[seg_name_length] = '\0';
    memcpy( seg_name, out_file->name, suffixless_length );
    sprintf( seg_name + suffixless_length, "_%"PRIu32, output->current_seg_number );
    if( *p == '.' )
        memcpy( seg_name + suffixless_length + suffix_length, p, end - p );
    int ret = out_file->open( seg_name, 0, seg_param );
    if( ret == 0 )
        eprintf( "[Segment] out: %s\n", seg_name );
    lsmash_free( seg_name );
    return ret;
}

static int switch_segment( lshmuxer_t *muxer )
{
    output_t      *output   = muxer->output;
    output_file_t *out_file = &output->file;
    lsmash_file_parameters_t seg_param = { 0 };
    if( open_media_segment( output, &seg_param ) < 0 )
        return ERROR_MSG( "failed to open an output file for segmentation.\n" );
    if( out_file->seg_param.mode & LSMASH_FILE_MODE_INITIALIZATION )
    {
        uint32_t           brand_count = out_file->param.brand_count + 2;
        lsmash_brand_type *brands      = lsmash_malloc_zero( brand_count * sizeof(lsmash_brand_type) );
        if( !brands )
            return ERROR_MSG( "failed to allocate brands for an output segment file.\n" );
        brands[0] = ISOM_BRAND_TYPE_MSDH;
        brands[1] = ISOM_BRAND_TYPE_MSIX;
        for( uint32_t i = 0; i < out_file->param.brand_count; i++ )
            brands[i + 2] = out_file->param.brands[i];
        seg_param.major_brand = ISOM_BRAND_TYPE_MSDH;
        seg_param.brand_count = brand_count;
        seg_param.brands      = brands;
        seg_param.mode        = LSMASH_FILE_MODE_WRITE | LSMASH_FILE_MODE_FRAGMENTED
                              | LSMASH_FILE_MODE_BOX   | LSMASH_FILE_MODE_MEDIA
                              | LSMASH_FILE_MODE_INDEX | LSMASH_FILE_MODE_SEGMENT;
    }
    else
    {
        void *opaque     = seg_param.opaque;
        seg_param        = out_file->seg_param;
        seg_param.opaque = opaque;
    }
    lsmash_file_t *segment = lsmash_set_file( output->root, &seg_param );
    if( !segment )
        return ERROR_MSG( "failed to add an output segment file into a ROOT.\n" );
    if( lsmash_switch_media_segment( output->root, segment, &moov_to_front ) < 0 )
        return ERROR_MSG( "failed to switch to the next segment.\n" );
    if( !(out_file->seg_param.mode & LSMASH_FILE_MODE_INITIALIZATION) )
        return out_file->close( &out_file->seg_param );
    out_file->seg_param = seg_param;
    return 0;
}

static int handle_segmentation( lshmuxer_t *muxer )
{
    if( muxer->subseg_per_seg == 0 )
        return 0;
    output_t *output = muxer->output;
    if( muxer->subseg_per_seg == output->file.current_subseg_number
     || output->current_seg_number == 1 )
    {
        if( switch_segment( muxer ) < 0 )
        {
            ERROR_MSG( "failed to switch to a segment.\n" );
            return -1;
        }
        output->file.current_subseg_number = 1;
        ++output->current_seg_number;
    }
    else
        ++output->file.current_subseg_number;
    return 0;
}

static void adapt_description_index( output_track_t *out_track, input_track_t *in_track, lsmash_sample_t *sample )
{
    sample->index = sample->index > in_track->num_summaries ? in_track->num_summaries
                  : sample->index == 0 ? 1
                  : sample->index;
    sample->index = out_track->summary_remap[sample->index - 1];
    if( in_track->current_sample_index == 0 )
        in_track->current_sample_index = sample->index;
}

static int adjust_timestamp
(
    lshmuxer_t           *muxer,
    output_track_t       *out_track,
    track_media_option_t *track_opt,
    lsmash_sample_t      *sample
)
{
    if( out_track->current_sample_number == 1 )
        out_track->skip_dt_interval = sample->dts;
    if( out_track->skip_dt_interval )
    {
        sample->dts -= out_track->skip_dt_interval;
        sample->cts -= out_track->skip_dt_interval;
    }
    if( muxer->timeline_shift )
    {
        if( out_track->current_sample_number == 1 )
            out_track->ctd_shift = sample->cts;
        sample->cts -= out_track->ctd_shift;
    }
    if( track_opt->has_cd )
    {
        if( out_track->current_sample_number == 1 )
        {
            /* Why: ?constant-delay=<arg> is specified in media timebase units (frames/ticks), so
             * scale constant_delay by out_track->timebase before comparing against sample->cts. */
            uint64_t cd_in_timebase = (uint64_t)track_opt->constant_delay * out_track->timebase;
            if( cd_in_timebase < sample->cts )
                return ERROR_MSG( "constant delay must be greater than or equal to first CTS.\n" );
            out_track->cd_shift = cd_in_timebase - sample->cts;
        }
        sample->cts += out_track->cd_shift;
    }
    return 0;
}

static int do_mux( lshmuxer_t *muxer )
{
    input_t        *inputs    = muxer->input;
    output_t       *output    = muxer->output;
    output_movie_t *out_movie = &output->file.movie;
    set_reference_chapter_track( muxer );
    double   largest_dts                 = 0;
    double   frag_base_dts               = 0;
    int      input_movie_number          = 1;
    uint32_t num_consecutive_sample_skip = 0;
    uint32_t num_active_input_tracks     = out_movie->num_tracks;
    uint64_t total_media_size            = 0;
    uint32_t progress_pos                = 0;
    uint8_t  pending_flush_fragments     = (muxer->frag_base_track != 0);
    while( 1 )
    {
        input_t       *in       = &inputs[input_movie_number - 1];
        input_movie_t *in_movie = &in->file.movie;
        uint32_t       in_idx   = in_movie->current_track_number - 1;
        input_track_t *in_track = &in_movie->track[in_idx];
        if( !in_track->active )
        {
            if( ++in_movie->current_track_number > in_movie->num_tracks )
            {
                in_movie->current_track_number = 1;
                ++input_movie_number;
            }
            if( input_movie_number > muxer->num_input )
                input_movie_number = 1;
            continue;
        }
        if( !in_track->reach_end_of_media_timeline )
        {
            lsmash_sample_t *sample = in_track->sample;
            output_track_t  *out_track = &out_movie->track[out_movie->current_track_number - 1];
            track_media_option_t *track_opt = &muxer->track_option[input_movie_number - 1][in_idx];
            if( !sample )
            {
                sample = lsmash_get_sample_from_media_timeline( in->root, in_track->track_ID, in_track->current_sample_number );
                if( sample )
                {
                    adapt_description_index( out_track, in_track, sample );
                    if( adjust_timestamp( muxer, out_track, track_opt, sample ) < 0 )
                    {
                        lsmash_delete_sample( sample );
                        return -1;
                    }
                    in_track->sample = sample;
                    in_track->dts    = (double)sample->dts / in_track->media.param.timescale;
                }
                else
                {
                    if( lsmash_check_sample_existence_in_media_timeline( in->root, in_track->track_ID, in_track->current_sample_number ) )
                    {
                        ERROR_MSG( "failed to get a sample.\n" );
                        break;
                    }
                    lsmash_sample_t sample_info = { 0 };
                    if( lsmash_get_sample_info_from_media_timeline( in->root, in_track->track_ID, in_track->current_sample_number, &sample_info ) < 0 )
                    {
                        in_track->sample = NULL;
                        in_track->reach_end_of_media_timeline = 1;
                        if( --num_active_input_tracks == 0 )
                            break;
                    }
                    else
                    {
                        ERROR_MSG( "failed to get a sample.\n" );
                        break;
                    }
                }
            }
            if( sample )
            {
                if( muxer->frag_base_track )
                {
                    if( pending_flush_fragments == 0 )
                    {
                        int over_duration = 1;
                        if( muxer->min_frag_duration != 0.0 )
                        {
                            lsmash_sample_t info;
                            int newsamp = lsmash_get_sample_info_from_media_timeline( in->root, in_track->track_ID, in_track->current_sample_number + 1, &info );
                            double cur_dts  = (double)info.dts / in_track->media.param.timescale;
                            double dur_in_s = (double)in_track->duration / (double)in_track->media.param.timescale;
                            if( newsamp >= 0 )
                                over_duration = cur_dts - frag_base_dts >= muxer->min_frag_duration;
                            if( over_duration && dur_in_s != 0.0 && muxer->last_frag_merge_thresh != 0.0 )
                            {
                                double left = dur_in_s - cur_dts;
                                over_duration = left > (muxer->min_frag_duration * muxer->last_frag_merge_thresh);
                            }
                        }
                        if( muxer->frag_base_track == out_movie->current_track_number
                         && sample->prop.ra_flags != ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE
                         && over_duration )
                        {
                            pending_flush_fragments = 1;
                            frag_base_dts = in_track->dts;
                        }
                    }
                    else if( num_consecutive_sample_skip == num_active_input_tracks || total_media_size == 0 )
                    {
                        if( flush_movie_fragment( muxer ) < 0 )
                        {
                            ERROR_MSG( "failed to flush a movie fragment.\n" );
                            break;
                        }
                        if( handle_segmentation( muxer ) < 0 )
                            break;
                        if( lsmash_create_fragment_movie( output->root ) < 0 )
                        {
                            ERROR_MSG( "failed to create a movie fragment.\n" );
                            break;
                        }
                        pending_flush_fragments = 0;
                    }
                }
                int append = 0;
                int need_new_fragment = (muxer->frag_base_track && sample->index != in_track->current_sample_index);
                if( pending_flush_fragments == 0 )
                    append = (in_track->dts <= largest_dts || num_consecutive_sample_skip == num_active_input_tracks) && !need_new_fragment;
                else if( muxer->frag_base_track != out_movie->current_track_number && !need_new_fragment )
                {
                    if( sample->prop.ra_flags == ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE )
                        append = 1;
                    else
                    {
                        lsmash_sample_t info;
                        if( lsmash_get_sample_info_from_media_timeline( in->root, in_track->track_ID, in_track->current_sample_number + 1, &info ) < 0 )
                            append = 0;
                        else
                            append = (info.prop.ra_flags != ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE)
                                  && ((double)info.dts / in_track->media.param.timescale <= frag_base_dts);
                    }
                }
                if( append )
                {
                    if( sample->index )
                    {
                        uint64_t sample_size     = sample->length;
                        uint64_t last_sample_dts = sample->dts;
                        uint64_t sample_cts      = sample->cts;
                        uint32_t sample_index    = sample->index;
                        if( lsmash_append_sample( output->root, out_track->track_ID, sample ) < 0 )
                        {
                            lsmash_delete_sample( sample );
                            return ERROR_MSG( "failed to append a sample.\n" );
                        }
                        if( out_track->current_sample_number == 1 )
                            out_track->start_offset = sample_cts;
                        else
                            out_track->start_offset = LSMASH_MIN( (int64_t)sample_cts, out_track->start_offset );
                        largest_dts                       = LSMASH_MAX( largest_dts, in_track->dts );
                        in_track->sample                  = NULL;
                        in_track->current_sample_number  += 1;
                        in_track->current_sample_index    = sample_index;
                        out_track->current_sample_number += 1;
                        out_track->last_sample_dts        = last_sample_dts;
                        num_consecutive_sample_skip       = 0;
                        total_media_size                 += sample_size;
                        if( (total_media_size >> 22) > progress_pos )
                        {
                            progress_pos = total_media_size >> 22;
                            eprintf( "Importing: %"PRIu64" bytes\r", total_media_size );
                        }
                    }
                    else
                    {
                        lsmash_delete_sample( sample );
                        in_track->sample = NULL;
                        in_track->current_sample_number += 1;
                    }
                }
                else
                    ++num_consecutive_sample_skip;
            }
        }
        if( ++in_movie->current_track_number > in_movie->num_tracks )
        {
            in_movie->current_track_number = 1;
            ++input_movie_number;
        }
        if( input_movie_number > muxer->num_input )
            input_movie_number = 1;
        if( ++out_movie->current_track_number > out_movie->num_tracks )
            out_movie->current_track_number = 1;
    }
    for( uint32_t i = 0; i < out_movie->num_tracks; i++ )
        if( lsmash_flush_pooled_samples( output->root, out_movie->track[i].track_ID, out_movie->track[i].last_sample_delta ) )
            return ERROR_MSG( "failed to flush samples.\n" );
    return 0;
}

static int construct_timeline_maps( lshmuxer_t *muxer )
{
    input_t               *input        = muxer->input;
    output_t              *output       = muxer->output;
    output_movie_t        *out_movie    = &output->file.movie;
    track_media_option_t **track_option = muxer->track_option;
    out_movie->current_track_number = 1;
    for( int i = 0; i < muxer->num_input; i++ )
        for( uint32_t j = 0; j < input[i].file.movie.num_tracks; j++ )
        {
            input_track_t *in_track = &input[i].file.movie.track[j];
            if( !in_track->active )
                continue;
            output_track_t       *out_track = &out_movie->track[out_movie->current_track_number++ - 1];
            track_media_option_t *topt      = &track_option[i][j];
            uint32_t movie_timescale = lsmash_get_movie_timescale( output->root );
            uint32_t media_timescale = lsmash_get_media_timescale( output->root, out_track->track_ID );
            if( !media_timescale )
                return ERROR_MSG( "media timescale is broken.\n" );
            double timescale_convert_multiplier = (double)movie_timescale / media_timescale;

            uint64_t start_offset = out_track->start_offset;
            if( muxer->frag_base_track && input[i].file.is_raw && !in_track->timeline_edited )
            {
                uint64_t dts1 = 0, cts1 = 0;
                if( !lsmash_get_dts_from_media_timeline( input[i].root, in_track->track_ID, 1, &dts1 )
                 && !lsmash_get_cts_from_media_timeline( input[i].root, in_track->track_ID, 1, &cts1 )
                 && cts1 > dts1 )
                    start_offset = cts1 - dts1;
            }

            if( topt->seek )
            {
                if( lsmash_delete_explicit_timeline_map( output->root, out_track->track_ID ) )
                    return ERROR_MSG( "failed to delete explicit timeline maps.\n" );
                lsmash_edit_t edit;
                edit.start_time = in_track->composition_delay + in_track->skip_duration;
                if( edit.start_time )
                {
                    uint64_t empty_duration = edit.start_time + lsmash_get_composition_to_decode_shift( output->root, out_track->track_ID );
                    lsmash_edit_t empty_edit;
                    empty_edit.duration   = empty_duration * timescale_convert_multiplier + 0.5;
                    empty_edit.start_time = ISOM_EDIT_MODE_EMPTY;
                    empty_edit.rate       = ISOM_EDIT_MODE_NORMAL;
                    if( lsmash_create_explicit_timeline_map( output->root, out_track->track_ID, empty_edit ) )
                        return ERROR_MSG( "failed to create an empty duration.\n" );
                }
                if( muxer->frag_base_track == 0 )
                    edit.duration = (out_track->last_sample_dts + out_track->last_sample_delta - in_track->skip_duration) * timescale_convert_multiplier;
                else
                    edit.duration = ISOM_EDIT_DURATION_IMPLICIT;
                edit.rate = ISOM_EDIT_MODE_NORMAL;
                if( lsmash_create_explicit_timeline_map( output->root, out_track->track_ID, edit ) )
                    return ERROR_MSG( "failed to create an explicit timeline map.\n" );
            }
            /* Why: Reconstruct the explicit timeline map ('elst') from the edited media timeline
             * whenever timeline editing or skip/delay parameters are active, except in bare positional
             * invocation mode (argc == 3, '<prog> input output' with no options), which performs only
             * GCD media timescale/timebase reduction while copying the input Edit List verbatim. */
            else if( (in_track->timeline_edited || topt->skip_duration_num || topt->empty_delay_num)
                  && !(muxer->positional_mode && muxer->argc == 3) )
            {
                if( lsmash_delete_explicit_timeline_map( output->root, out_track->track_ID ) )
                    return ERROR_MSG( "failed to delete explicit timeline maps.\n" );
                uint64_t empty_delay = in_track->empty_delay
                                     + (topt->empty_delay_num ? (uint64_t)((double)((uint64_t)topt->empty_delay_num * media_timescale) / topt->empty_delay_den + 0.5) : 0);
                uint64_t base_dur    = in_track->timeline_edited
                                     ? in_track->edited_duration
                                     : (out_track->last_sample_dts + out_track->last_sample_delta);
                uint64_t duration    = base_dur + empty_delay;
                if( empty_delay )
                {
                    lsmash_edit_t empty_edit;
                    empty_edit.duration   = ((double)empty_delay / media_timescale) * movie_timescale;
                    empty_edit.start_time = ISOM_EDIT_MODE_EMPTY;
                    empty_edit.rate       = ISOM_EDIT_MODE_NORMAL;
                    if( lsmash_create_explicit_timeline_map( output->root, out_track->track_ID, empty_edit ) )
                        return ERROR_MSG( "failed to create an empty duration.\n" );
                    duration  = ((double)duration / media_timescale) * movie_timescale;
                    duration -= empty_edit.duration;
                }
                else
                    duration = ((double)duration / media_timescale) * movie_timescale;
                lsmash_edit_t edit;
                edit.duration   = muxer->frag_base_track ? ISOM_EDIT_DURATION_IMPLICIT : duration;
                edit.start_time = (in_track->timeline_edited ? in_track->composition_delay : (out_track->priming_samples + start_offset))
                                + (topt->skip_duration_num ? (uint64_t)((double)((uint64_t)topt->skip_duration_num * media_timescale) / topt->skip_duration_den + 0.5) : 0);
                edit.rate       = ISOM_EDIT_MODE_NORMAL;
                if( lsmash_create_explicit_timeline_map( output->root, out_track->track_ID, edit ) )
                    return ERROR_MSG( "failed to create an explicit timeline map.\n" );
            }
            else if( input[i].file.is_raw || topt->encoder_delay || topt->has_cd || muxer->timeline_shift || topt->user_fps )
            {
                if( lsmash_delete_explicit_timeline_map( output->root, out_track->track_ID ) )
                    return ERROR_MSG( "failed to delete explicit timeline maps.\n" );
                uint64_t actual_duration = out_track->lpcm
                                         ? lsmash_get_media_duration( output->root, out_track->track_ID )
                                         : out_track->last_sample_dts + out_track->last_sample_delta;
                actual_duration -= out_track->priming_samples;
                lsmash_edit_t edit;
                edit.duration   = muxer->frag_base_track ? ISOM_EDIT_DURATION_IMPLICIT : (uint64_t)(actual_duration * timescale_convert_multiplier);
                edit.start_time = out_track->priming_samples + start_offset;
                edit.rate       = ISOM_EDIT_MODE_NORMAL;
                if( lsmash_create_explicit_timeline_map( output->root, out_track->track_ID, edit ) )
                    return ERROR_MSG( "failed to set timeline map.\n" );
            }
            else if( lsmash_copy_timeline_map( output->root, out_track->track_ID, input[i].root, in_track->track_ID ) )
                return ERROR_MSG( "failed to copy timeline maps.\n" );
        }
    out_movie->current_track_number = 1;
    return 0;
}

static int finish_movie( lshmuxer_t *muxer )
{
    output_t *output = muxer->output;
    if( muxer->chap_file )
        lsmash_set_tyrant_chapter( output->root, muxer->chap_file, muxer->add_bom_to_chpl );
    REFRESH_CONSOLE;
    int all_raw             = 1;
    int any_timeline_edited = 0;
    for( int i = 0; i < muxer->num_input; i++ )
    {
        if( !muxer->input[i].file.is_raw )
            all_raw = 0;
        for( uint32_t j = 0; j < muxer->input[i].file.movie.num_tracks; j++ )
            if( muxer->input[i].file.movie.track[j].active && muxer->input[i].file.movie.track[j].timeline_edited )
                any_timeline_edited = 1;
    }
    int pure_raw = all_raw && !any_timeline_edited;
    lsmash_adhoc_remux_t *remux_cb = &moov_to_front;
    if( !muxer->frag_base_track && !muxer->optimize_pd && pure_raw )
        remux_cb = NULL;
    if( lsmash_finish_movie( output->root, remux_cb ) )
        return -1;
    /* Why: When progressive download optimization (--optimize-pd) is performed on pure raw stream
     * inputs, lsmash_finish_movie() relocates moov before mdat and finalizes the file layout directly
     * without appending a trailing 30-byte L-SMASH indicator 'free' box. */
    if( muxer->frag_base_track || (pure_raw && muxer->optimize_pd) )
        return 0;
    return lsmash_write_lsmash_indicator( output->root );
}

int main( int argc, char *argv[] )
{
    if( argc < 2 )
    {
        display_help();
        return -1;
    }
    else if( !strcasecmp( argv[1], "-h" ) || !strcasecmp( argv[1], "--help" ) )
    {
        display_help();
        return 0;
    }
    else if( !strcasecmp( argv[1], "-v" ) || !strcasecmp( argv[1], "--version" ) )
    {
        display_version();
        return 0;
    }
    else if( argc < 3 )
    {
        display_help();
        return -1;
    }

    output_t output = { 0 };
    lshmuxer_t muxer =
    {
        .output         = &output,
        .chap_track     = 1,
        .add_bom_to_chpl = 0
    };
    lsmash_get_mainargs( &argc, &argv );
    for( int i = 1; i < argc; i++ )
        if( !strcasecmp( argv[i], "-i" ) || !strcasecmp( argv[i], "--input" ) )
            ++muxer.num_input;
    /* Support positional 'lshmuxer [options] input output' invocation syntax */
    if( muxer.num_input == 0 && argc >= 3 )
        muxer.num_input = 1;

    input_t *input = lsmash_malloc_zero( muxer.num_input * sizeof(input_t) );
    if( !input )
        return LSHMUXER_ERR( "failed to alloc input_t.\n" );
    muxer.input = input;
    track_media_option_t **track_option = lsmash_malloc_zero( muxer.num_input * sizeof(track_media_option_t *) );
    if( !track_option )
        return LSHMUXER_ERR( "failed to alloc track_media_option_t *.\n" );
    muxer.track_option = track_option;

    int ret = parse_cli_option( argc, argv, &muxer );
    if( ret < 0 || muxer.help )
    {
        if( ret < 0 )
            LSHMUXER_ERR( "failed to parse command line options.\n" );
        else
            cleanup_lshmuxer( &muxer );
        return ret;
    }
    if( prepare_output( &muxer ) < 0 )
        return LSHMUXER_ERR( "failed to set up preparation for output.\n" );
    if( muxer.frag_base_track && construct_timeline_maps( &muxer ) < 0 )
        return LSHMUXER_ERR( "failed to construct timeline maps.\n" );
    if( do_mux( &muxer ) < 0 )
        return LSHMUXER_ERR( "failed to mux/remux movies.\n" );
    if( !muxer.frag_base_track && construct_timeline_maps( &muxer ) < 0 )
        return LSHMUXER_ERR( "failed to construct timeline maps.\n" );
    if( finish_movie( &muxer ) < 0 )
        return LSHMUXER_ERR( "failed to finish output movie.\n" );
    REFRESH_CONSOLE;
    eprintf( "Muxing completed!\n" );
    cleanup_lshmuxer( &muxer );
    return 0;
}