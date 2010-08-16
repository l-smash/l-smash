/*****************************************************************************
 * isom.c:
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

#include "isom.h"

static int isom_add_avcC( isom_entry_list_t *list );
static int isom_add_co64( isom_root_t *root, uint32_t trak_number );

static void isom_remove_avcC( isom_avcC_t *avcC );
static void isom_remove_avcC_ps( isom_avcC_ps_entry_t *ps );

static int isom_write_mdat_header( isom_root_t *root );

static uint64_t isom_update_mvhd_size( isom_root_t *root );
static uint64_t isom_update_tkhd_size( isom_trak_entry_t *trak );
static uint64_t isom_update_mdhd_size( isom_trak_entry_t *trak );
static int isom_update_moov_size( isom_root_t *root );

/*---- creator ----*/
isom_root_t *isom_create_root( char *filename )
{
    isom_root_t *root = malloc( sizeof(isom_root_t) );
    if( !root )
        return NULL;
    memset( root, 0, sizeof(isom_root_t) );
    root->bs = malloc( sizeof(isom_bs_t) );
    if( !root->bs )
        return NULL;
    memset( root->bs, 0, sizeof(isom_bs_t) );
    root->bs->stream = fopen( filename, "wb" );
    if( !root->bs->stream )
        return NULL;
    return root;
}

isom_sample_t *isom_create_sample( void )
{
    isom_sample_t *sample = malloc( sizeof(isom_sample_t) );
    if( !sample )
        return NULL;
    memset( sample, 0, sizeof(isom_sample_t) );
    return sample;
}

void isom_remove_sample( isom_sample_t *sample )
{
    if( !sample )
        return;
    if( sample->data )
        free( sample->data );
    free( sample );
}

#define isom_create_basebox( box_name, box_4cc ) \
    isom_##box_name##_t *(box_name) = malloc( sizeof(isom_##box_name##_t) ); \
    if( !(box_name) ) \
        return -1; \
    memset( box_name, 0, sizeof(isom_##box_name##_t) ); \
    isom_init_base_header( &((box_name)->base_header), box_4cc )

#define isom_create_fullbox( box_name, box_4cc ) \
    isom_##box_name##_t *(box_name) = malloc( sizeof(isom_##box_name##_t) ); \
    if( !(box_name) ) \
        return -1; \
    memset( box_name, 0, sizeof(isom_##box_name##_t) ); \
    isom_init_full_header( &((box_name)->full_header), box_4cc )

#define isom_create_list_fullbox( box_name, box_4cc ) \
    isom_create_fullbox( box_name, box_4cc ); \
    (box_name)->list = isom_create_entry_list(); \
    if( !((box_name)->list) ) \
    { \
        free( box_name ); \
        return -1; \
    }
/*---- ----*/

static inline void isom_init_base_header( isom_base_header_t *bh, uint32_t type )
{
    bh->size     = 0;
    bh->type     = type;
    bh->usertype = NULL;
}

static inline void isom_init_full_header( isom_full_header_t *fbh, uint32_t type )
{
    fbh->size     = 0;
    fbh->type     = type;
    fbh->usertype = NULL;
    fbh->version  = 0;
    fbh->flags    = 0;
}

static void isom_bs_put_base_header( isom_bs_t *bs, isom_base_header_t *bh )
{
    if( bh->size > UINT32_MAX )
    {
        isom_bs_put_be32( bs, 1 );
        isom_bs_put_be32( bs, bh->type );
        isom_bs_put_be64( bs, bh->size );     /* largesize */
    }
    else
    {
        isom_bs_put_be32( bs, (uint32_t)bh->size );
        isom_bs_put_be32( bs, bh->type );
    }
    if( bh->type == ISOM_BOX_TYPE_UUID )
        isom_bs_put_bytes( bs, bh->usertype, 16 );
}

static void isom_bs_put_full_header( isom_bs_t *bs, isom_full_header_t *fbh )
{
    isom_base_header_t bh;
    memcpy( &bh, fbh, sizeof(isom_base_header_t) );
    isom_bs_put_base_header( bs, &bh );
    isom_bs_put_byte( bs, fbh->version );
    isom_bs_put_be24( bs, fbh->flags );
}

static isom_trak_entry_t *isom_get_trak( isom_root_t *root, uint32_t trak_number )
{
    if( !root || !root->moov || !root->moov->trak_list )
        return NULL;
    uint32_t i = 0;
    for( isom_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
        if( ++i == trak_number )
            return (isom_trak_entry_t *)entry->data;
    return NULL;
}

static uint32_t isom_get_track_ID( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->tkhd )
        return 0;
    return trak->tkhd->track_ID;
}

static int isom_add_elst_entry( isom_elst_t *elst, uint64_t segment_duration, int64_t media_time, int32_t media_rate )
{
    isom_elst_entry_t *data = malloc( sizeof(isom_elst_entry_t) );
    if( !data )
        return -1;
    data->segment_duration = segment_duration;
    data->media_time = media_time;
    data->media_rate = media_rate;
    if( isom_add_entry( elst->list, data ) )
    {
        free( data );
        return -1;
    }
    if( data->segment_duration > UINT32_MAX || data->media_time > UINT32_MAX )
        elst->full_header.version = 1;
    return 0;
}

int isom_add_dref_entry( isom_root_t *root, uint32_t trak_number, uint32_t flags, char *name, char *location )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->dinf || !trak->mdia->minf->dinf->dref || !trak->mdia->minf->dinf->dref->list )
        return -1;
    isom_dref_entry_t *data = malloc( sizeof(isom_dref_entry_t) );
    if( !data )
        return -1;
    memset( data, 0, sizeof(isom_dref_entry_t) );
    isom_init_full_header( &data->full_header, name ? ISOM_BOX_TYPE_URN : ISOM_BOX_TYPE_URL );
    data->full_header.flags = flags;
    if( location )
    {
        data->location_length = strlen( location ) + 1;
        data->location = malloc( data->location_length );
        if( !data->location )
        {
            free( data );
            return -1;
        }
        memcpy( data->location, location, data->location_length );
    }
    if( name )
    {
        data->name_length = strlen( name ) + 1;
        data->name = malloc( data->name_length );
        if( !data->name )
        {
            if( data->location )
                free( data->location );
            free( data );
            return -1;
        }
        memcpy( data->name, name, data->name_length );
    }
    if( isom_add_entry( trak->mdia->minf->dinf->dref->list, data ) )
    {
        if( data->location )
            free( data->location );
        if( data->name )
            free( data->name );
        free( data );
        return -1;
    }
    return 0;
}

static isom_avcC_ps_entry_t *isom_create_ps_entry( uint8_t *ps, uint32_t ps_size )
{
    isom_avcC_ps_entry_t *entry = malloc( sizeof(isom_avcC_ps_entry_t) );
    if( !entry )
        return NULL;
    entry->parameterSetLength = ps_size;
    entry->parameterSetNALUnit = malloc( ps_size );
    if( !entry->parameterSetNALUnit )
    {
        free( entry );
        return NULL;
    }
    memcpy( entry->parameterSetNALUnit, ps, ps_size );
    return entry;
}

int isom_add_sps_entry( isom_root_t *root, uint32_t trak_number, uint32_t entry_number, uint8_t *sps, uint32_t sps_size )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_avc_entry_t *data = (isom_avc_entry_t *)isom_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_avcC_t *avcC = (isom_avcC_t *)data->avcC;
    if( !avcC )
        return -1;
    isom_avcC_ps_entry_t *ps = isom_create_ps_entry( sps, sps_size );
    if( !ps )
        return -1;
    if( isom_add_entry( avcC->sequenceParameterSets, ps ) )
    {
        isom_remove_avcC_ps( ps );
        return -1;
    }
    avcC->numOfSequenceParameterSets = avcC->sequenceParameterSets->entry_count;
    return 0;
}

int isom_add_pps_entry( isom_root_t *root, uint32_t trak_number, uint32_t entry_number, uint8_t *pps, uint32_t pps_size )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_avc_entry_t *data = (isom_avc_entry_t *)isom_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_avcC_t *avcC = (isom_avcC_t *)data->avcC;
    if( !avcC )
        return -1;
    isom_avcC_ps_entry_t *ps = isom_create_ps_entry( pps, pps_size );
    if( !ps )
        return -1;
    if( isom_add_entry( avcC->pictureParameterSets, ps ) )
    {
        isom_remove_avcC_ps( ps );
        return -1;
    }
    avcC->numOfPictureParameterSets = avcC->pictureParameterSets->entry_count;
    return 0;
}

int isom_add_spsext_entry( isom_root_t *root, uint32_t trak_number, uint32_t entry_number, uint8_t *spsext, uint32_t spsext_size )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_avc_entry_t *data = (isom_avc_entry_t *)isom_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_avcC_t *avcC = (isom_avcC_t *)data->avcC;
    if( !avcC )
        return -1;
    isom_avcC_ps_entry_t *ps = isom_create_ps_entry( spsext, spsext_size );
    if( !ps )
        return -1;
    if( isom_add_entry( avcC->sequenceParameterSetExt, ps ) )
    {
        isom_remove_avcC_ps( ps );
        return -1;
    }
    avcC->numOfSequenceParameterSetExt = avcC->sequenceParameterSetExt->entry_count;
    return 0;
}

static int isom_add_avc_entry( isom_entry_list_t *list, uint32_t sample_type )
{
    if( !list )
        return -1;
    isom_avc_entry_t *avc = malloc( sizeof(isom_avc_entry_t) );
    if( !avc )
        return -1;
    memset( avc, 0, sizeof(isom_avc_entry_t) );
    isom_init_base_header( &avc->base_header, sample_type );
    avc->data_reference_index = 1;
    avc->horizresolution = avc->vertresolution = 0x00480000;
    avc->frame_count = 1;
    switch( sample_type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
        case ISOM_CODEC_TYPE_AVC2_VIDEO :
            strcpy( avc->compressorname, "\012AVC Coding" );
            break;
        case ISOM_CODEC_TYPE_AVCP_VIDEO :
            strcpy( avc->compressorname, "\016AVC Parameters" );
            break;
        default :
            return -1;
    }
    avc->depth = 0x0018;
    avc->pre_defined3 = -1;
    if( isom_add_entry( list, avc ) || isom_add_avcC( list ) )
    {
        free( avc );
        return -1;
    }
    return 0;
}

static int isom_add_mp4v_entry( isom_entry_list_t *list )
{
    if( !list )
        return -1;
    isom_mp4v_entry_t *mp4v = malloc( sizeof(isom_visual_entry_t) );
    if( !mp4v )
        return -1;
    memset( mp4v, 0, sizeof(isom_mp4v_entry_t) );
    isom_init_base_header( &mp4v->base_header, ISOM_CODEC_TYPE_MP4V_VIDEO );
    mp4v->data_reference_index = 1;
    mp4v->horizresolution = mp4v->vertresolution = 0x00480000;
    mp4v->frame_count = 1;
    mp4v->compressorname[32] = '\0';
    mp4v->depth = 0x0018;
    mp4v->pre_defined3 = -1;
    if( isom_add_entry( list, mp4v ) )
    {
        free( mp4v );
        return -1;
    }
    return 0;
}

static int isom_add_mp4a_entry( isom_entry_list_t *list, mp4sys_audio_summary_t* summary )
{
    if( !list || !summary
        || summary->stream_type != MP4SYS_STREAM_TYPE_AudioStream )
        return -1;
    switch( summary->object_type_indication )
    {
    case MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3:
    case MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_Main_Profile:
    case MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_LC_Profile:
    case MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_SSR_Profile:
        break;
    default:
        return -1;
    }

    isom_create_fullbox( esds, ISOM_BOX_TYPE_ESDS );
    mp4sys_ES_Descriptor_params_t esd_param;
    esd_param.ES_ID = 0;              /* This is esds internal, so 0 is allowed. */
    esd_param.objectTypeIndication = summary->object_type_indication;
    esd_param.streamType = summary->stream_type;
    esd_param.bufferSizeDB = 0;       /* NOTE: ISO/IEC 14496-3 does not mention this, so we use 0. */
    esd_param.maxBitrate = 0;         /* This will be updated later if needed. or... I think this can be arbitrary value. */
    esd_param.avgBitrate = 0;         /* FIXME: 0 if VBR. */
    esd_param.dsi_payload = summary->exdata;
    esd_param.dsi_payload_length = summary->exdata_length;
    esds->ES = mp4sys_setup_ES_Descriptor( &esd_param );
    if( !esds->ES )
    {
        free( esds );
        return -1;
    }
    isom_mp4a_entry_t *mp4a = malloc( sizeof(isom_mp4a_entry_t) );
    if( !mp4a )
    {
        mp4sys_remove_ES_Descriptor( esds->ES );
        free( esds );
        return -1;
    }
    memset( mp4a, 0, sizeof(isom_mp4a_entry_t) );
    isom_init_base_header( &mp4a->base_header, ISOM_CODEC_TYPE_MP4A_AUDIO );
    mp4a->data_reference_index = 1;
    /* In pure mp4 file, these "template" fields shall be default values according to the spec.
       But not pure - hybrid with other spec - mp4 file can take other values.
       Which is to say, these template values shall be ignored in terms of mp4, except some object_type_indications.
       see 14496-14, "6 Template fields used". */
    mp4a->channelcount = summary->channels;
    mp4a->samplesize = summary->bit_depth;
    /* WARNING: This field cannot retain frequency above 65535Hz.
       This is not "FIXME", I just honestly implemented what the spec says.
       BTW, who ever expects sampling frequency takes fixed-point decimal??? */
    mp4a->samplerate = summary->frequency << 16;
    mp4a->esds = esds;
    mp4a->pli = mp4sys_get_audioProfileLevelIndication( summary );
    if( isom_add_entry( list, mp4a ) )
    {
        mp4sys_remove_ES_Descriptor( esds->ES );
        free( esds );
        free( mp4a );
        return -1;
    }
    return 0;
}

static int isom_add_mp4s_entry( isom_entry_list_t *list )
{
    if( !list )
        return -1;
    isom_mp4s_entry_t *mp4s = malloc( sizeof(isom_mp4s_entry_t) );
    if( !mp4s )
        return -1;
    memset( mp4s, 0, sizeof(isom_mp4s_entry_t) );
    isom_init_base_header( &mp4s->base_header, ISOM_CODEC_TYPE_MP4S_SYSTEM );
    mp4s->data_reference_index = 1;
    if( isom_add_entry( list, mp4s ) )
    {
        free( mp4s );
        return -1;
    }
    return 0;
}

static int isom_add_visual_entry( isom_entry_list_t *list, uint32_t sample_type )
{
    if( !list )
        return -1;
    isom_visual_entry_t *visual = malloc( sizeof(isom_visual_entry_t) );
    if( !visual )
        return -1;
    memset( visual, 0, sizeof(isom_visual_entry_t) );
    isom_init_base_header( &visual->base_header, sample_type );
    visual->data_reference_index = 1;
    visual->horizresolution = visual->vertresolution = 0x00480000;
    visual->frame_count = 1;
    visual->compressorname[32] = '\0';
    visual->depth = 0x0018;
    visual->pre_defined3 = -1;
    if( isom_add_entry( list, visual ) )
    {
        free( visual );
        return -1;
    }
    return 0;
}

static int isom_add_audio_entry( isom_entry_list_t *list, uint32_t sample_type )
{
    if( !list )
        return -1;
    isom_audio_entry_t *audio = malloc( sizeof(isom_audio_entry_t) );
    if( !audio )
        return -1;
    memset( audio, 0, sizeof(isom_audio_entry_t) );
    isom_init_base_header( &audio->base_header, sample_type );
    audio->data_reference_index = 1;
    audio->channelcount = 2;
    audio->samplesize = 16;
    audio->samplerate = 48000U<<16;
    if( isom_add_entry( list, audio ) )
    {
        free( audio );
        return -1;
    }
    return 0;
}

/* This function returns 0 if failed, sample_entry_number if succeeded. */
int isom_add_sample_entry( isom_root_t *root, uint32_t trak_number, uint32_t sample_type, void* summary )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return 0;
    isom_entry_list_t *list = trak->mdia->minf->stbl->stsd->list;
    int ret = -1;
    switch( sample_type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
        case ISOM_CODEC_TYPE_AVC2_VIDEO :
        case ISOM_CODEC_TYPE_AVCP_VIDEO :
            ret = isom_add_avc_entry( list, sample_type );
            break;
        case ISOM_CODEC_TYPE_MP4V_VIDEO :
            ret = isom_add_mp4v_entry( list );
            break;
        case ISOM_CODEC_TYPE_MP4A_AUDIO :
            ret = isom_add_mp4a_entry( list, (mp4sys_audio_summary_t*)summary );
            break;
        case ISOM_CODEC_TYPE_MP4S_SYSTEM :
            ret = isom_add_mp4s_entry( list );
            break;
        case ISOM_CODEC_TYPE_DRAC_VIDEO :
        case ISOM_CODEC_TYPE_ENCV_VIDEO :
        case ISOM_CODEC_TYPE_MJP2_VIDEO :
        case ISOM_CODEC_TYPE_MVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC2_VIDEO :
        case ISOM_CODEC_TYPE_S263_VIDEO :
        case ISOM_CODEC_TYPE_SVC1_VIDEO :
        case ISOM_CODEC_TYPE_VC_1_VIDEO :
            ret = isom_add_visual_entry( list, sample_type );
            break;
        case ISOM_CODEC_TYPE_AC_3_AUDIO :
        case ISOM_CODEC_TYPE_ALAC_AUDIO :
        case ISOM_CODEC_TYPE_DRA1_AUDIO :
        case ISOM_CODEC_TYPE_DTSC_AUDIO :
        case ISOM_CODEC_TYPE_DTSH_AUDIO :
        case ISOM_CODEC_TYPE_DTSL_AUDIO :
        case ISOM_CODEC_TYPE_EC_3_AUDIO :
        case ISOM_CODEC_TYPE_ENCA_AUDIO :
        case ISOM_CODEC_TYPE_G719_AUDIO :
        case ISOM_CODEC_TYPE_G726_AUDIO :
        case ISOM_CODEC_TYPE_M4AE_AUDIO :
        case ISOM_CODEC_TYPE_MLPA_AUDIO :
        case ISOM_CODEC_TYPE_RAW_AUDIO  :
        case ISOM_CODEC_TYPE_SAMR_AUDIO :
        case ISOM_CODEC_TYPE_SAWB_AUDIO :
        case ISOM_CODEC_TYPE_SAWP_AUDIO :
        case ISOM_CODEC_TYPE_SEVC_AUDIO :
        case ISOM_CODEC_TYPE_SQCP_AUDIO :
        case ISOM_CODEC_TYPE_SSMV_AUDIO :
        case ISOM_CODEC_TYPE_TWOS_AUDIO :
            ret = isom_add_audio_entry( list, sample_type );
            break;
        /* Under Construction */
        default :
            return 0;
    }
    return ret ? 0 : list->entry_count;
}

int isom_add_stts_entry( isom_root_t *root, uint32_t trak_number, uint32_t sample_delta )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list )
        return -1;
    isom_stts_entry_t *data = malloc( sizeof(isom_stts_entry_t) );
    if( !data )
        return -1;
    data->sample_count = 1;
    data->sample_delta = sample_delta;
    if( isom_add_entry( trak->mdia->minf->stbl->stts->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

int isom_add_ctts_entry( isom_root_t *root, uint32_t trak_number, uint32_t sample_offset )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->ctts || !trak->mdia->minf->stbl->ctts->list )
        return -1;
    isom_ctts_entry_t *data = malloc( sizeof(isom_ctts_entry_t) );
    if( !data )
        return -1;
    data->sample_count = 1;
    data->sample_offset = sample_offset;
    if( isom_add_entry( trak->mdia->minf->stbl->ctts->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

int isom_add_stsc_entry( isom_root_t *root, uint32_t trak_number, uint32_t first_chunk, uint32_t samples_per_chunk, uint32_t sample_description_index )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsc || !trak->mdia->minf->stbl->stsc->list )
        return -1;
    isom_stsc_entry_t *data = malloc( sizeof(isom_stsc_entry_t) );
    if( !data )
        return -1;
    data->first_chunk = first_chunk;
    data->samples_per_chunk = samples_per_chunk;
    data->sample_description_index = sample_description_index;
    if( isom_add_entry( trak->mdia->minf->stbl->stsc->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

int isom_add_stsz_entry( isom_root_t *root, uint32_t trak_number, uint32_t entry_size )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsz )
        return -1;
    isom_stsz_t *stsz = trak->mdia->minf->stbl->stsz;
    /* retrieve initial sample_size */
    if( !stsz->sample_count )
        stsz->sample_size = entry_size;
    /* if it seems constant access_unit size at present, update sample_count only */
    if( !stsz->list && stsz->sample_size == entry_size )
    {
        ++ stsz->sample_count;
        return 0;
    }
    /* found sample_size varies, create sample_size list */
    if( !stsz->list )
    {
        stsz->list = isom_create_entry_list();
        if( !stsz->list )
            return -1;
        for( uint32_t i = 0; i < stsz->sample_count; i++ )
        {
            isom_stsz_entry_t *data = malloc( sizeof(isom_stsz_entry_t) );
            if( !data )
                return -1;
            data->entry_size = stsz->sample_size;
            if( isom_add_entry( stsz->list, data ) )
            {
                free( data );
                return -1;
            }
        }
        stsz->sample_size = 0;
    }
    isom_stsz_entry_t *data = malloc( sizeof(isom_stsz_entry_t) );
    if( !data )
        return -1;
    data->entry_size = entry_size;
    if( isom_add_entry( stsz->list, data ) )
    {
        free( data );
        return -1;
    }
    ++ stsz->sample_count;
    return 0;
}

int isom_add_stss_entry( isom_root_t *root, uint32_t trak_number, uint32_t sample_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stss || !trak->mdia->minf->stbl->stss->list )
        return -1;
    isom_stss_entry_t *data = malloc( sizeof(isom_stss_entry_t) );
    if( !data )
        return -1;
    data->sample_number = sample_number;
    if( isom_add_entry( trak->mdia->minf->stbl->stss->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

int isom_add_sdtp_entry( isom_root_t *root, uint32_t trak_number, isom_sample_property_t *prop )
{
    if( !prop )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->sdtp || !trak->mdia->minf->stbl->sdtp->list )
        return -1;
    isom_sdtp_entry_t *data = malloc( sizeof(isom_sdtp_entry_t) );
    if( !data )
        return -1;
    /* isom_sdtp_entry_t is smaller than isom_sample_property_t. */
    data->is_leading = prop->leading & 0x03;
    data->sample_depends_on = prop->independent & 0x03;
    data->sample_is_depended_on = prop->disposable & 0x03;
    data->sample_has_redundancy = prop->redundant & 0x03;
    if( isom_add_entry( trak->mdia->minf->stbl->sdtp->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

int isom_add_co64_entry( isom_root_t *root, uint32_t trak_number, uint64_t chunk_offset )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stco || !trak->mdia->minf->stbl->stco->list )
        return -1;
    isom_co64_entry_t *data = malloc( sizeof(isom_co64_entry_t) );
    if( !data )
        return -1;
    data->chunk_offset = chunk_offset;
    if( isom_add_entry( trak->mdia->minf->stbl->stco->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

int isom_add_stco_entry( isom_root_t *root, uint32_t trak_number, uint64_t chunk_offset )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stco || !trak->mdia->minf->stbl->stco->list )
        return -1;
    if( trak->mdia->minf->stbl->stco->large_presentation )
    {
        if( isom_add_co64_entry( root, trak_number, chunk_offset ) )
            return -1;
        return 0;
    }
    if( chunk_offset > UINT32_MAX )
    {
        /* backup stco */
        isom_stco_t *stco = trak->mdia->minf->stbl->stco;
        trak->mdia->minf->stbl->stco = NULL;
        int e = 0;
        if( isom_add_co64( root, trak_number ) )
            e = 1;
        /* move chunk_offset to co64 from stco */
        for( isom_entry_t *entry = stco->list->head; !e && entry; )
        {
            isom_stco_entry_t *data = (isom_stco_entry_t *)entry->data;
            if( isom_add_co64_entry( root, trak_number, data->chunk_offset ) )
                e = 1;
            isom_entry_t *next = entry->next;
            if( entry )
            {
                if( entry->data )
                    free( entry->data );
                free( entry );
            }
            entry = next;
        }
        if( e )
        {
            isom_remove_list( trak->mdia->minf->stbl->stco->list, NULL );
            trak->mdia->minf->stbl->stco = stco;
            return -1;
        }
        else
        {
            free( stco->list );
            free( stco );
        }
        if( isom_add_co64_entry( root, trak_number, chunk_offset ) )
            return -1;
        return 0;
    }
    isom_stco_entry_t *data = malloc( sizeof(isom_stco_entry_t) );
    if( !data )
        return -1;
    data->chunk_offset = (uint32_t)chunk_offset;
    if( isom_add_entry( trak->mdia->minf->stbl->stco->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

int isom_add_sbgp_entry( isom_root_t *root, uint32_t trak_number, uint32_t grouping_number, uint32_t sample_count, uint32_t group_description_index )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->grouping_count ||
        !grouping_number || trak->mdia->minf->stbl->grouping_count < grouping_number || !sample_count )
        return -1;
    isom_sbgp_t *sbgp = trak->mdia->minf->stbl->sbgp + grouping_number - 1;
    if( !sbgp || !sbgp->list )
        return -1;
    isom_sbgp_entry_t *data = malloc( sizeof(isom_sbgp_entry_t) );
    if( !data )
        return -1;
    data->sample_count = sample_count;
    data->group_description_index = group_description_index;
    if( isom_add_entry( sbgp->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

int isom_add_roll_group_entry( isom_root_t *root, uint32_t trak_number, uint32_t grouping_number, uint32_t description_length, int16_t roll_distance )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->grouping_count ||
        !grouping_number || trak->mdia->minf->stbl->grouping_count < grouping_number )
        return -1;
    isom_sgpd_t *sgpd = trak->mdia->minf->stbl->sgpd + grouping_number - 1;
    if( !sgpd || !sgpd->list || sgpd->grouping_type != ISOM_GROUP_TYPE_ROLL )
        return -1;
    isom_roll_group_entry_t *data = malloc( sizeof(isom_roll_group_entry_t) );
     if( !data )
        return -1;
    data->description_length = description_length;
    data->roll_distance = roll_distance;
    if( isom_add_entry( sgpd->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

int isom_add_chpl_entry( isom_root_t *root, uint64_t start_time, char *chapter_name )
{
    if( !chapter_name || !root || !root->moov || !root->moov->udta || !root->moov->udta->chpl || !root->moov->udta->chpl->list )
        return -1;
    isom_chpl_entry_t *data = malloc( sizeof(isom_chpl_entry_t) );
    if( !data )
        return -1;
    data->start_time = start_time;
    data->name_length = ISOM_MIN( strlen( chapter_name ), 255 );
    data->chapter_name = malloc( data->name_length + 1 );
    if( !data->chapter_name )
    {
        free( data );
        return -1;
    }
    memcpy( data->chapter_name, chapter_name, data->name_length );
    if( isom_add_entry( root->moov->udta->chpl->list, data ) )
    {
        free( data->chapter_name );
        free( data );
        return -1;
    }
    return 0;
}

static int isom_add_ftyp( isom_root_t *root )
{
    if( root->ftyp )
        return -1;
    isom_create_basebox( ftyp, ISOM_BOX_TYPE_FTYP );
    ftyp->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 8;
    root->ftyp = ftyp;
    return 0;
}

static int isom_add_moov( isom_root_t *root )
{
    if( root->moov )
        return -1;
    isom_create_basebox( moov, ISOM_BOX_TYPE_MOOV );
    root->moov = moov;
    return 0;
}

static int isom_add_mvhd( isom_root_t *root )
{
    if( !root || !root->moov )
        return -1;
    if( !root->moov->mvhd )
    {
        isom_create_fullbox( mvhd, ISOM_BOX_TYPE_MVHD );
        mvhd->rate = 0x00010000;
        mvhd->volume = 0x0100;
        mvhd->matrix[0] = 0x00010000;
        mvhd->matrix[4] = 0x00010000;
        mvhd->matrix[8] = 0x40000000;
        mvhd->next_track_ID = 1;
        root->moov->mvhd = mvhd;
    }
    return 0;
}

static int isom_scan_trak_profileLevelIndication( isom_trak_entry_t* trak, mp4sys_audioProfileLevelIndication* audio_pli, mp4sys_visualProfileLevelIndication* visual_pli )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    isom_stsd_t* stsd = trak->mdia->minf->stbl->stsd;
    if( !stsd || !stsd->list || !stsd->list->head )
        return -1;
    for( isom_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_sample_entry_t* sample_entry = (isom_sample_entry_t*)entry->data;
        if( !sample_entry )
            return -1;
        switch( sample_entry->base_header.type )
        {
            case ISOM_CODEC_TYPE_AVC1_VIDEO :
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
                /* FIXME: Do we have to arbitrate like audio? */
                if( *visual_pli == MP4SYS_VISUAL_PLI_NONE_REQUIRED )
                    *visual_pli = MP4SYS_VISUAL_PLI_H264_AVC;
                break;
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
                *audio_pli = mp4sys_max_audioProfileLevelIndication( *audio_pli, ((isom_mp4a_entry_t*)sample_entry)->pli );
                break;
            case ISOM_CODEC_TYPE_DRAC_VIDEO :
            case ISOM_CODEC_TYPE_ENCV_VIDEO :
            case ISOM_CODEC_TYPE_MJP2_VIDEO :
            case ISOM_CODEC_TYPE_MVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC2_VIDEO :
            case ISOM_CODEC_TYPE_S263_VIDEO :
            case ISOM_CODEC_TYPE_SVC1_VIDEO :
            case ISOM_CODEC_TYPE_VC_1_VIDEO :
                /* FIXME: Do we have to arbitrate like audio? */
                if( *visual_pli == MP4SYS_VISUAL_PLI_NONE_REQUIRED )
                    *visual_pli = MP4SYS_VISUAL_PLI_NOT_SPECIFIED;
                break;
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_DRA1_AUDIO :
            case ISOM_CODEC_TYPE_DTSC_AUDIO :
            case ISOM_CODEC_TYPE_DTSH_AUDIO :
            case ISOM_CODEC_TYPE_DTSL_AUDIO :
            case ISOM_CODEC_TYPE_EC_3_AUDIO :
            case ISOM_CODEC_TYPE_ENCA_AUDIO :
            case ISOM_CODEC_TYPE_G719_AUDIO :
            case ISOM_CODEC_TYPE_G726_AUDIO :
            case ISOM_CODEC_TYPE_M4AE_AUDIO :
            case ISOM_CODEC_TYPE_MLPA_AUDIO :
            case ISOM_CODEC_TYPE_RAW_AUDIO :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
            case ISOM_CODEC_TYPE_SAWP_AUDIO :
            case ISOM_CODEC_TYPE_SEVC_AUDIO :
            case ISOM_CODEC_TYPE_SQCP_AUDIO :
            case ISOM_CODEC_TYPE_SSMV_AUDIO :
            case ISOM_CODEC_TYPE_TWOS_AUDIO :
                /* NOTE: These audio codecs other than mp4a does not have appropriate pli. */
                *visual_pli = MP4SYS_VISUAL_PLI_NOT_SPECIFIED;
                break;
            case ISOM_CODEC_TYPE_FDP_HINT :
            case ISOM_CODEC_TYPE_M2TS_HINT :
            case ISOM_CODEC_TYPE_PM2T_HINT :
            case ISOM_CODEC_TYPE_PRTP_HINT :
            case ISOM_CODEC_TYPE_RM2T_HINT :
            case ISOM_CODEC_TYPE_RRTP_HINT :
            case ISOM_CODEC_TYPE_RSRP_HINT :
            case ISOM_CODEC_TYPE_RTP_HINT  :
            case ISOM_CODEC_TYPE_SM2T_HINT :
            case ISOM_CODEC_TYPE_SRTP_HINT :
                /* FIXME: Do we have to set OD_profileLevelIndication? */
                break;
            case ISOM_CODEC_TYPE_IXSE_META :
            case ISOM_CODEC_TYPE_METT_META :
            case ISOM_CODEC_TYPE_METX_META :
            case ISOM_CODEC_TYPE_MLIX_META :
            case ISOM_CODEC_TYPE_OKSD_META :
            case ISOM_CODEC_TYPE_SVCM_META :
            case ISOM_CODEC_TYPE_TEXT_META :
            case ISOM_CODEC_TYPE_URIM_META :
            case ISOM_CODEC_TYPE_XML_META  :
                /* FIXME: Do we have to set OD_profileLevelIndication? */
                break;
        }
    }
    return 0;
}

static int isom_add_iods( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->trak_list )
        return -1;
    if( root->moov->iods )
        return 0;
    isom_create_fullbox( iods, ISOM_BOX_TYPE_IODS );
    iods->OD = mp4sys_create_ObjectDescriptor( 1 ); /* NOTE: Use 1 for ObjectDescriptorID of IOD. */
    if( !iods->OD )
    {
        free( iods );
        return -1;
    }

    mp4sys_audioProfileLevelIndication audio_pli = MP4SYS_AUDIO_PLI_NONE_REQUIRED;
    mp4sys_visualProfileLevelIndication visual_pli = MP4SYS_VISUAL_PLI_NONE_REQUIRED;
    for( isom_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_entry_t* trak = (isom_trak_entry_t*)entry->data;
        if( !trak || !trak->tkhd )
            return -1;
        if( isom_scan_trak_profileLevelIndication( trak, &audio_pli, &visual_pli ) )
            return -1;
        if( mp4sys_add_ES_ID_Inc( iods->OD, trak->tkhd->track_ID ) )
            return -1;
    }
    if( mp4sys_to_InitialObjectDescriptor( iods->OD,
                                           0, /* FIXME: I'm not quite sure what the spec says. */
                                           MP4SYS_OD_PLI_NONE_REQUIRED, MP4SYS_SCENE_PLI_NONE_REQUIRED,
                                           audio_pli, visual_pli,
                                           MP4SYS_GRAPHICS_PLI_NONE_REQUIRED ) )
    {
        free( iods );
        return -1;
    }
    root->moov->iods = iods;
    return 0;
}

static int isom_add_tkhd( isom_root_t *root, uint32_t trak_number, uint32_t hdlr_type )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->root || !trak->root->moov || !trak->root->moov->mvhd || !trak->root->moov->trak_list )
        return -1;
    if( !trak->tkhd )
    {
        isom_create_fullbox( tkhd, ISOM_BOX_TYPE_TKHD );
        switch( hdlr_type )
        {
            case ISOM_HDLR_TYPE_VISUAL :
                tkhd->matrix[0] = 0x00010000;
                tkhd->matrix[4] = 0x00010000;
                tkhd->matrix[8] = 0x40000000;
                break;
            case ISOM_HDLR_TYPE_AUDIO :
                tkhd->volume = 0x0100;
                break;
            default :
                break;
        }
        tkhd->duration = 0xffff;
        tkhd->track_ID = trak->root->moov->mvhd->next_track_ID;
        ++ trak->root->moov->mvhd->next_track_ID;
        trak->tkhd = tkhd;
    }
    return 0;
}

static int isom_add_elst( isom_edts_t *edts )
{
    if( edts->elst )
        return 0;
    isom_create_list_fullbox( elst, ISOM_BOX_TYPE_ELST );
    edts->elst = elst;
    return 0;
}

static int isom_add_edts( isom_trak_entry_t *trak )
{
    if( trak->edts )
        return 0;
    isom_create_basebox( edts, ISOM_BOX_TYPE_EDTS );
    trak->edts = edts;
    return 0;
}

static int isom_add_mdia( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak )
        return -1;
    if( !trak->mdia )
    {
        isom_create_basebox( mdia, ISOM_BOX_TYPE_MDIA );
        trak->mdia = mdia;
    }
    return 0;
}

static int isom_add_mdhd( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia )
        return -1;
    if( !trak->mdia->mdhd )
    {
        isom_create_fullbox( mdhd, ISOM_BOX_TYPE_MDHD );
        mdhd->language = ISOM_LANG( "und" );
        trak->mdia->mdhd = mdhd;
    }
    return 0;
}

static int isom_add_minf( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia )
        return -1;
    if( !trak->mdia->minf )
    {
        isom_create_basebox( minf, ISOM_BOX_TYPE_MINF );
        trak->mdia->minf = minf;
    }
    return 0;
}

static int isom_add_hdlr( isom_root_t *root, uint32_t trak_number, uint32_t handler_type )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia )
        return -1;
    if( !trak->mdia->hdlr )
    {
        isom_create_fullbox( hdlr, ISOM_BOX_TYPE_HDLR );
        hdlr->handler_type = handler_type;
        hdlr->name = malloc( 1 );
        if( !hdlr->name )
            return -1;
        hdlr->name[0] = '\0';
        hdlr->name_length = 1;
        trak->mdia->hdlr = hdlr;
    }
    return 0;
}

static int isom_add_vmhd( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf )
        return -1;
    if( !trak->mdia->minf->vmhd )
    {
        isom_create_fullbox( vmhd, ISOM_BOX_TYPE_VMHD );
        vmhd->full_header.flags = 0x000001;
        trak->mdia->minf->vmhd = vmhd;
    }
    return 0;
}

static int isom_add_smhd( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf )
        return -1;
    if( !trak->mdia->minf->smhd )
    {
        isom_create_fullbox( smhd, ISOM_BOX_TYPE_SMHD );
        trak->mdia->minf->smhd = smhd;
    }
    return 0;
}

static int isom_add_hmhd( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf )
        return -1;
    if( !trak->mdia->minf->hmhd )
    {
        isom_create_fullbox( hmhd, ISOM_BOX_TYPE_HMHD );
        trak->mdia->minf->hmhd = hmhd;
    }
    return 0;
}

static int isom_add_nmhd( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf )
        return -1;
    if( !trak->mdia->minf->nmhd )
    {
        isom_create_fullbox( nmhd, ISOM_BOX_TYPE_NMHD );
        trak->mdia->minf->nmhd = nmhd;
    }
    return 0;
}

static int isom_add_dinf( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf )
        return -1;
    if( !trak->mdia->minf->dinf )
    {
        isom_create_basebox( dinf, ISOM_BOX_TYPE_DINF );
        trak->mdia->minf->dinf = dinf;
    }
    return 0;
}

static int isom_add_dref( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->dinf )
        return -1;
    if( !trak->mdia->minf->dinf->dref )
    {
        isom_create_list_fullbox( dref, ISOM_BOX_TYPE_DREF );
        trak->mdia->minf->dinf->dref = dref;
    }
    if( isom_add_dref_entry( root, trak_number, 0x000001, NULL, NULL ) )
        return -1;
    return 0;
}

static int isom_add_stbl( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf )
        return -1;
    if( !trak->mdia->minf->stbl )
    {
        isom_create_basebox( stbl, ISOM_BOX_TYPE_STBL );
        trak->mdia->minf->stbl = stbl;
    }
    return 0;
}

static int isom_add_stsd( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    if( !trak->mdia->minf->stbl->stsd )
    {
        isom_create_list_fullbox( stsd, ISOM_BOX_TYPE_STSD );
        trak->mdia->minf->stbl->stsd = stsd;
    }
    return 0;
}

int isom_add_pasp( isom_root_t *root, uint32_t trak_number, uint32_t entry_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_visual_entry_t *visual = (isom_visual_entry_t *)isom_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !visual )
        return -1;
    isom_create_basebox( pasp, ISOM_BOX_TYPE_PASP );
    visual->pasp = pasp;
    return 0;
}

static int isom_add_avcC( isom_entry_list_t *list )
{
    if( !list )
        return -1;
    isom_avc_entry_t *data = (isom_avc_entry_t *)isom_get_entry_data( list, list->entry_count );
    if( !data )
        return -1;
    isom_create_basebox( avcC, ISOM_BOX_TYPE_AVCC );
    avcC->sequenceParameterSets = isom_create_entry_list();
    if( !avcC->sequenceParameterSets )
    {
        free( avcC );
        return -1;
    }
    avcC->pictureParameterSets = isom_create_entry_list();
    if( !avcC->pictureParameterSets )
    {
        isom_remove_avcC( avcC );
        return -1;
    }
    avcC->sequenceParameterSetExt = isom_create_entry_list();
    if( !avcC->sequenceParameterSetExt )
    {
        isom_remove_avcC( avcC );
        return -1;
    }
    data->avcC = avcC;
    return 0;
}

int isom_add_btrt( isom_root_t *root, uint32_t trak_number, uint32_t entry_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_avc_entry_t *data = isom_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_create_basebox( btrt, ISOM_BOX_TYPE_BTRT );
    data->btrt = btrt;
    return 0;
}

static int isom_add_stts( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    if( !trak->mdia->minf->stbl->stts )
    {
        isom_create_list_fullbox( stts, ISOM_BOX_TYPE_STTS );
        trak->mdia->minf->stbl->stts = stts;
    }
    return 0;
}

int isom_add_ctts( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    if( !trak->mdia->minf->stbl->ctts )
    {
        isom_create_list_fullbox( ctts, ISOM_BOX_TYPE_CTTS );
        trak->mdia->minf->stbl->ctts = ctts;
    }
    return 0;
}

static int isom_add_stsc( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    if( !trak->mdia->minf->stbl->stsc )
    {
        isom_create_list_fullbox( stsc, ISOM_BOX_TYPE_STSC );
        trak->mdia->minf->stbl->stsc = stsc;
    }
    return 0;
}

static int isom_add_stsz( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    if( !trak->mdia->minf->stbl->stsz )
    {
        isom_create_fullbox( stsz, ISOM_BOX_TYPE_STSZ );  /* We don't create a list here. */
        trak->mdia->minf->stbl->stsz = stsz;
    }
    return 0;
}

int isom_add_stss( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    if( !trak->mdia->minf->stbl->stss )
    {
        isom_create_list_fullbox( stss, ISOM_BOX_TYPE_STSS );
        trak->mdia->minf->stbl->stss = stss;
    }
    return 0;
}

int isom_add_sdtp( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    if( !trak->mdia->minf->stbl->sdtp )
    {
        isom_create_list_fullbox( sdtp, ISOM_BOX_TYPE_SDTP );
        trak->mdia->minf->stbl->sdtp = sdtp;
    }
    return 0;
}

static int isom_add_co64( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    if( !trak->mdia->minf->stbl->stco )
    {
        isom_create_list_fullbox( stco, ISOM_BOX_TYPE_CO64 );
        stco->large_presentation = 1;
        trak->mdia->minf->stbl->stco = stco;
    }
    return 0;
}

static int isom_add_stco( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    if( !trak->mdia->minf->stbl->stco )
    {
        isom_create_list_fullbox( stco, ISOM_BOX_TYPE_STCO );
        stco->large_presentation = 0;
        trak->mdia->minf->stbl->stco = stco;
    }
    return 0;
}

static int isom_add_sgpd( isom_root_t *root, uint32_t trak_number, uint32_t grouping_type )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    uint64_t grouping_count = 1;
    isom_sgpd_t *sgpd_array;
    if( !trak->mdia->minf->stbl->sgpd )
        sgpd_array = malloc( sizeof(isom_sgpd_t) );
    else
    {
        grouping_count += trak->mdia->minf->stbl->grouping_count;
        sgpd_array = realloc( trak->mdia->minf->stbl->sgpd, grouping_count * sizeof(isom_sgpd_t) );
    }
    if( !sgpd_array )
        return -1;
    isom_sgpd_t *sgpd = sgpd_array + grouping_count - 1;
    memset( sgpd, 0, sizeof(isom_sgpd_t) );
    isom_init_full_header( &sgpd->full_header, ISOM_BOX_TYPE_SGPD );
    sgpd->list = isom_create_entry_list();
    if( !sgpd->list )
    {
        trak->mdia->minf->stbl->sgpd = NULL;
        free( sgpd_array );
        return -1;
    }
    trak->mdia->minf->stbl->sgpd = sgpd_array;
    sgpd->grouping_type = grouping_type;
    return 0;
}

int isom_add_sbgp( isom_root_t *root, uint32_t trak_number, uint32_t grouping_type )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    uint64_t grouping_count = 1;
    isom_sbgp_t *sbgp_array;
    if( !trak->mdia->minf->stbl->sbgp )
        sbgp_array = malloc( sizeof(isom_sbgp_t) );
    else
    {
        grouping_count += trak->mdia->minf->stbl->grouping_count;
        sbgp_array = realloc( trak->mdia->minf->stbl->sbgp, grouping_count * sizeof(isom_sbgp_t) );
    }
    if( !sbgp_array )
        return -1;
    isom_sbgp_t *sbgp = sbgp_array + grouping_count - 1;
    memset( sbgp, 0, sizeof(isom_sbgp_t) );
    isom_init_full_header( &sbgp->full_header, ISOM_BOX_TYPE_SBGP );
    sbgp->list = isom_create_entry_list();
    if( !sbgp->list )
    {
        trak->mdia->minf->stbl->sbgp = NULL;
        free( sbgp_array );
        return -1;
    }
    trak->mdia->minf->stbl->sbgp = sbgp_array;
    sbgp->grouping_type = grouping_type;
    if( isom_add_sgpd( root, trak_number, grouping_type ) )
        return -1;
    trak->mdia->minf->stbl->grouping_count = grouping_count;
    return 0;
}

int isom_add_chpl( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->udta )
        return -1;
    if( !root->moov->udta->chpl )
    {
        isom_create_list_fullbox( chpl, ISOM_BOX_TYPE_CHPL );
        chpl->full_header.version = 1;
        root->moov->udta->chpl = chpl;
    }
    return 0;
}

int isom_add_udta( isom_root_t *root, uint32_t trak_number )
{
    /* trak_number == 0 means the direct addition to moov box */
    if( !trak_number )
    {
        if( !root || !root->moov )
            return -1;
        if( root->moov->udta )
            return 0;
        isom_create_basebox( udta, ISOM_BOX_TYPE_UDTA );
        root->moov->udta = udta;
        return 0;
    }
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak )
        return -1;
    if( trak->udta )
        return 0;
    isom_create_basebox( udta, ISOM_BOX_TYPE_UDTA );
    trak->udta = udta;
    return 0;
}

int isom_add_trak( isom_root_t *root, uint32_t hdlr_type )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return -1;
    isom_moov_t *moov = root->moov;
    if( !moov->trak_list )
    {
        moov->trak_list = isom_create_entry_list();
        if( !moov->trak_list )
            return -1;
    }
    isom_create_basebox( trak_entry, ISOM_BOX_TYPE_TRAK );
    isom_cache_t *cache = malloc( sizeof(isom_cache_t) );
    if( !cache )
        return -1;
    memset( cache, 0, sizeof(isom_cache_t) );
    if( isom_add_entry( moov->trak_list, trak_entry ) )
        return -1;
    trak_entry->root = root;
    trak_entry->cache = cache;
    uint32_t trak_number = moov->trak_list->entry_count;
    isom_add_tkhd( root, trak_number, hdlr_type );
    isom_add_mdia( root, trak_number );
    isom_add_mdhd( root, trak_number );
    isom_add_minf( root, trak_number );
    isom_add_dinf( root, trak_number );
    isom_add_dref( root, trak_number );
    isom_add_stbl( root, trak_number );
    isom_add_stsd( root, trak_number );
    isom_add_stts( root, trak_number );
    isom_add_stsc( root, trak_number );
    isom_add_stco( root, trak_number );
    isom_add_stsz( root, trak_number );
    isom_add_hdlr( root, trak_number, hdlr_type );
    switch( hdlr_type )
    {
        case ISOM_HDLR_TYPE_VISUAL :
            isom_add_vmhd( root, trak_number );
            break;
        case ISOM_HDLR_TYPE_AUDIO :
            isom_add_smhd( root, trak_number );
            break;
        case ISOM_HDLR_TYPE_HINT :
            isom_add_hmhd( root, trak_number );
            break;
        default :
            isom_add_nmhd( root, trak_number );
            break;
    }
    return 0;
}

int isom_add_free( isom_root_t *root, uint8_t *data, uint64_t data_length )
{
    if( !root )
        return -1;
    if( !root->free )
    {
        isom_create_basebox( skip, ISOM_BOX_TYPE_FREE );
        root->free = skip;
    }
    if( data && data_length )
        return isom_set_free( root, data, data_length );
    return 0;
}

/* If a mdat box already exists, flush a current one and start a new one. */
int isom_add_mdat( isom_root_t *root )
{
    if( !root )
        return 0;
    if( root->mdat )
        isom_write_mdat_size( root );   /* flush a current mdat */
    else
    {
        isom_create_basebox( mdat, ISOM_BOX_TYPE_MDAT );
        root->mdat = mdat;
    }
    isom_write_mdat_header( root );     /* start a new mdat */
    return 0;
}

#define isom_remove_fullbox( box_type, container ) \
    isom_##box_type##_t *(box_type) = (container)->box_type; \
    if( box_type ) \
        free( box_type )

#define isom_remove_list_fullbox( box_type, container ) \
    isom_##box_type##_t *(box_type) = (container)->box_type; \
    if( box_type ) \
    { \
        isom_remove_list( (box_type)->list, NULL ); \
        free( box_type ); \
    }

static void isom_remove_ftyp( isom_ftyp_t *ftyp )
{
    if( !ftyp )
        return;
    if( ftyp->compatible_brands )
        free( ftyp->compatible_brands );
    free( ftyp );
}

void isom_remove_edts( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->edts )
        return;
    isom_remove_list_fullbox( elst, trak->edts );
    free( trak->edts );
}

static void isom_remove_avcC_ps( isom_avcC_ps_entry_t *ps )
{
    if( !ps )
        return;
    if( ps->parameterSetNALUnit )
        free( ps->parameterSetNALUnit  );
    free( ps );
}

static void isom_remove_avcC( isom_avcC_t *avcC )
{
    if( !avcC )
        return;
    isom_remove_list( avcC->sequenceParameterSets,   isom_remove_avcC_ps );
    isom_remove_list( avcC->pictureParameterSets,    isom_remove_avcC_ps );
    isom_remove_list( avcC->sequenceParameterSetExt, isom_remove_avcC_ps );
    free( avcC );
}

static void isom_remove_stsd( isom_stsd_t *stsd )
{
    if( !stsd )
        return;
    if( !stsd->list )
    {
        free( stsd );
        return;
    }
    for( isom_entry_t *entry = stsd->list->head; entry; )
    {
        isom_sample_entry_t *sample = (isom_sample_entry_t *)entry->data;
        if( !sample )
        {
            isom_entry_t *next = entry->next;
            free( entry );
            entry = next;
            continue;
        }
        switch( sample->base_header.type )
        {
            case ISOM_CODEC_TYPE_AVC1_VIDEO :
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
            {
                isom_avc_entry_t *avc = (isom_avc_entry_t *)entry->data;
                if( avc->pasp )
                    free( avc->pasp );
                if( avc->clap )
                    free( avc->clap );
                if( avc->avcC )
                    isom_remove_avcC( avc->avcC );
                if( avc->btrt )
                    free( avc->btrt );
                free( avc );
                break;
            }
            case ISOM_CODEC_TYPE_MP4V_VIDEO :
            {
                isom_mp4v_entry_t *mp4v = (isom_mp4v_entry_t *)entry->data;
                if( mp4v->pasp )
                    free( mp4v->pasp );
                if( mp4v->clap )
                    free( mp4v->clap );
                if( mp4v->esds )
                    free( mp4v->esds );
                free( mp4v );
                break;
            }
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
            {
                isom_mp4a_entry_t *mp4a = (isom_mp4a_entry_t *)entry->data;
                if( mp4a->esds )
                {
                    mp4sys_remove_ES_Descriptor( mp4a->esds->ES );
                    free( mp4a->esds );
                }
                free( mp4a );
                break;
            }
            case ISOM_CODEC_TYPE_MP4S_SYSTEM :
            {
                isom_mp4s_entry_t *mp4s = (isom_mp4s_entry_t *)entry->data;
                if( mp4s->esds )
                    free( mp4s->esds );
                free( mp4s );
                break;
            }
            case ISOM_CODEC_TYPE_DRAC_VIDEO :
            case ISOM_CODEC_TYPE_ENCV_VIDEO :
            case ISOM_CODEC_TYPE_MJP2_VIDEO :
            case ISOM_CODEC_TYPE_MVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC2_VIDEO :
            case ISOM_CODEC_TYPE_S263_VIDEO :
            case ISOM_CODEC_TYPE_SVC1_VIDEO :
            case ISOM_CODEC_TYPE_VC_1_VIDEO :
            {
                isom_visual_entry_t *visual = (isom_visual_entry_t *)entry->data;
                if( visual->pasp )
                    free( visual->pasp );
                if( visual->clap )
                    free( visual->clap );
                free( visual );
                break;
            }
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_DRA1_AUDIO :
            case ISOM_CODEC_TYPE_DTSC_AUDIO :
            case ISOM_CODEC_TYPE_DTSH_AUDIO :
            case ISOM_CODEC_TYPE_DTSL_AUDIO :
            case ISOM_CODEC_TYPE_EC_3_AUDIO :
            case ISOM_CODEC_TYPE_ENCA_AUDIO :
            case ISOM_CODEC_TYPE_G719_AUDIO :
            case ISOM_CODEC_TYPE_G726_AUDIO :
            case ISOM_CODEC_TYPE_M4AE_AUDIO :
            case ISOM_CODEC_TYPE_MLPA_AUDIO :
            case ISOM_CODEC_TYPE_RAW_AUDIO :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
            case ISOM_CODEC_TYPE_SAWP_AUDIO :
            case ISOM_CODEC_TYPE_SEVC_AUDIO :
            case ISOM_CODEC_TYPE_SQCP_AUDIO :
            case ISOM_CODEC_TYPE_SSMV_AUDIO :
            case ISOM_CODEC_TYPE_TWOS_AUDIO :
            {
                isom_audio_entry_t *audio = (isom_audio_entry_t *)entry->data;
                free( audio );
                break;
            }
            case ISOM_CODEC_TYPE_FDP_HINT :
            case ISOM_CODEC_TYPE_M2TS_HINT :
            case ISOM_CODEC_TYPE_PM2T_HINT :
            case ISOM_CODEC_TYPE_PRTP_HINT :
            case ISOM_CODEC_TYPE_RM2T_HINT :
            case ISOM_CODEC_TYPE_RRTP_HINT :
            case ISOM_CODEC_TYPE_RSRP_HINT :
            case ISOM_CODEC_TYPE_RTP_HINT  :
            case ISOM_CODEC_TYPE_SM2T_HINT :
            case ISOM_CODEC_TYPE_SRTP_HINT :
            {
                isom_hint_entry_t *hint = (isom_hint_entry_t *)entry->data;
                if( hint->data )
                    free( hint->data );
                free( hint );
                break;
            }
            case ISOM_CODEC_TYPE_IXSE_META :
            case ISOM_CODEC_TYPE_METT_META :
            case ISOM_CODEC_TYPE_METX_META :
            case ISOM_CODEC_TYPE_MLIX_META :
            case ISOM_CODEC_TYPE_OKSD_META :
            case ISOM_CODEC_TYPE_SVCM_META :
            case ISOM_CODEC_TYPE_TEXT_META :
            case ISOM_CODEC_TYPE_URIM_META :
            case ISOM_CODEC_TYPE_XML_META  :
            {
                isom_metadata_entry_t *metadata = (isom_metadata_entry_t *)entry->data;
                free( metadata );
                break;
            }
            default :
                break;
        }
        isom_entry_t *next = entry->next;
        free( entry );
        entry = next;
    }
    free( stsd->list );
    free( stsd );
}

static void isom_remove_stbl( isom_stbl_t *stbl )
{
    if( !stbl )
        return;
    isom_remove_stsd( stbl->stsd );
    isom_remove_list_fullbox( stts, stbl );
    isom_remove_list_fullbox( ctts, stbl );
    isom_remove_list_fullbox( stsc, stbl );
    isom_remove_list_fullbox( stsz, stbl );
    isom_remove_list_fullbox( stss, stbl );
    isom_remove_list_fullbox( sdtp, stbl );
    isom_remove_list_fullbox( stco, stbl );
    for( uint32_t i = stbl->grouping_count; i ; i-- )
    {
        isom_remove_list( (stbl->sbgp + i - 1)->list, NULL );
        isom_remove_list( (stbl->sgpd + i - 1)->list, NULL );
    }
    if( stbl->sbgp )
        free( stbl->sbgp );
    if( stbl->sgpd )
        free( stbl->sgpd );
    free( stbl );
}

static void isom_remove_dinf( isom_dinf_t *dinf )
{
    if( !dinf )
        return;
    isom_dref_t *dref = dinf->dref;
    if( dref )
    {
        if( !dref->list )
        {
            free( dref );
            free( dinf );
            return;
        }
        for( isom_entry_t *entry = dref->list->head; entry; )
        {
            isom_dref_entry_t *data = (isom_dref_entry_t *)entry->data;
            if( data )
            {
                if( data->name )
                    free( data->name );
                if( data->location )
                    free( data->location );
                free( data );
            }
            isom_entry_t *next = entry->next;
            free( entry );
            entry = next;
        }
        free( dref->list );
        free( dref );
    }
    free( dinf );
}

static void isom_remove_minf( isom_minf_t *minf )
{
    if( !minf )
        return;
    isom_remove_fullbox( vmhd, minf );
    isom_remove_fullbox( smhd, minf );
    isom_remove_fullbox( hmhd, minf );
    isom_remove_fullbox( nmhd, minf );
    isom_remove_dinf( minf->dinf );
    isom_remove_stbl( minf->stbl );
    free( minf );
}

static void isom_remove_hdlr( isom_hdlr_t *hdlr )
{
    if( !hdlr )
        return;
    if( hdlr->name )
        free( hdlr->name );
    free( hdlr );
}

static void isom_remove_mdia( isom_mdia_t *mdia )
{
    if( !mdia )
        return;
    isom_remove_fullbox( mdhd, mdia );
    isom_remove_minf( mdia->minf );
    isom_remove_hdlr( mdia->hdlr );
    free( mdia );
}

static void isom_remove_chpl( isom_chpl_t *chpl )
{
    if( !chpl )
        return;
    if( !chpl->list )
    {
        free( chpl );
        return;
    }
    for( isom_entry_t *entry = chpl->list->head; entry; )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        if( data )
        {
            if( data->chapter_name )
                free( data->chapter_name );
            free( data );
        }
        isom_entry_t *next = entry->next;
        free( entry );
        entry = next;
    }
    free( chpl->list );
    free( chpl );
}

static void isom_remove_udta( isom_udta_t *udta )
{
    if( !udta )
        return;
    isom_remove_chpl( udta->chpl );
    free( udta );
}

void isom_remove_trak( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak )
        return;
    isom_remove_fullbox( tkhd, trak );
    isom_remove_edts( root, trak_number );
    isom_remove_mdia( trak->mdia );
    isom_remove_udta( trak->udta );
    if( trak->cache )
    {
        isom_remove_list( trak->cache->chunk.pool, isom_remove_sample );
        free( trak->cache );
    }
    free( trak );
}

void isom_remove_iods( isom_iods_t *iods )
{
    if( !iods )
        return;
    mp4sys_remove_ObjectDescriptor( iods->OD );
    free( iods );
    return;
}

static void isom_remove_moov( isom_root_t *root )
{
    if( !root || !root->moov )
        return;
    isom_moov_t *moov = root->moov;
    if( moov->mvhd )
        free( moov->mvhd );
    isom_remove_iods( moov->iods );
    isom_remove_udta( moov->udta );
    if( moov->trak_list )
    {
        /* FIXME: These are kinda workarounds.
           isom_remove_trak, and furthermore, trak_number system itself needs overhaul. */
        uint32_t i = 1;
        for( isom_entry_t *entry = moov->trak_list->head; entry; i++, entry = entry->next )
        {
            isom_remove_trak( root, i );
            entry->data = NULL;
        }
        isom_remove_list( moov->trak_list, NULL );
    }
    free( moov );
}

void isom_remove_mdat( isom_root_t *root )
{
    if( root && root->mdat )
        free( root->mdat );
}

void isom_remove_free( isom_root_t *root )
{
    if( root && root->free )
    {
        if( root->free->data )
            free( root->free->data );
        free( root->free );
    }
}

void isom_destroy_root( isom_root_t *root )
{
    if( !root )
        return;
    isom_remove_ftyp( root->ftyp );
    isom_remove_moov( root );
    isom_remove_mdat( root );
    isom_remove_free( root );
    if( root->bs )
    {
        if( root->bs->stream )
            fclose( root->bs->stream );
        if( root->bs->data )
            free( root->bs->data );
        free( root->bs );
    }
    free( root );
}

/* Box writers */
int isom_write_ftyp( isom_root_t *root )
{
    isom_bs_t *bs = root->bs;
    isom_ftyp_t *ftyp = root->ftyp;
    isom_bs_put_base_header( bs, &ftyp->base_header );
    isom_bs_put_be32( bs, ftyp->major_brand );
    isom_bs_put_be32( bs, ftyp->minor_version );
    for( uint32_t i = 0; i < ftyp->brand_count; i++ )
        isom_bs_put_be32( bs, ftyp->compatible_brands[i] );
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_tkhd( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_tkhd_t *tkhd = trak->tkhd;
    if( !tkhd )
        return -1;
    isom_bs_put_full_header( bs, &tkhd->full_header );
    if( tkhd->full_header.version )
    {
        isom_bs_put_be64( bs, tkhd->creation_time );
        isom_bs_put_be64( bs, tkhd->modification_time );
        isom_bs_put_be32( bs, tkhd->track_ID );
        isom_bs_put_be32( bs, tkhd->reserved1 );
        isom_bs_put_be64( bs, tkhd->duration );
    }
    else
    {
        isom_bs_put_be32( bs, (uint32_t)tkhd->creation_time );
        isom_bs_put_be32( bs, (uint32_t)tkhd->modification_time );
        isom_bs_put_be32( bs, tkhd->track_ID );
        isom_bs_put_be32( bs, tkhd->reserved1 );
        isom_bs_put_be32( bs, (uint32_t)tkhd->duration );
    }
    isom_bs_put_be32( bs, tkhd->reserved2[0] );
    isom_bs_put_be32( bs, tkhd->reserved2[1] );
    isom_bs_put_be16( bs, tkhd->layer );
    isom_bs_put_be16( bs, tkhd->alternate_group );
    isom_bs_put_be16( bs, tkhd->volume );
    isom_bs_put_be16( bs, tkhd->reserved3 );
    for( uint32_t i = 0; i < 9; i++ )
        isom_bs_put_be32( bs, tkhd->matrix[i] );
    isom_bs_put_be32( bs, tkhd->width );
    isom_bs_put_be32( bs, tkhd->height );
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}
int isom_write_elst( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_elst_t *elst = trak->edts->elst;
    if( !elst )
        return -1;
    if( !elst->list->entry_count )
        return 0;
    isom_bs_put_full_header( bs, &elst->full_header );
    isom_bs_put_be32( bs, elst->list->entry_count );
    for( isom_entry_t *entry = elst->list->head; entry; entry = entry->next )
    {
        isom_elst_entry_t *data = (isom_elst_entry_t *)entry->data;
        if( !data )
            return -1;
        if( elst->full_header.version )
        {
            isom_bs_put_be64( bs, data->segment_duration );
            isom_bs_put_be64( bs, data->media_time );
        }
        else
        {
            isom_bs_put_be32( bs, (uint32_t)data->segment_duration );
            isom_bs_put_be32( bs, (uint32_t)data->media_time );
        }
        isom_bs_put_be32( bs, data->media_rate );
    }
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

int isom_write_edts( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_edts_t *edts = trak->edts;
    if( !edts )
        return 0;
    isom_bs_put_base_header( bs, &edts->base_header );
    if( isom_bs_write_data( bs ) )
        return -1;
    if( isom_write_elst( bs, trak ) )
        return -1;
    return 0;
}

static int isom_write_mdhd( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    if( !mdhd )
        return -1;
    isom_bs_put_full_header( bs, &mdhd->full_header );
    if( mdhd->full_header.version )
    {
        isom_bs_put_be64( bs, mdhd->creation_time );
        isom_bs_put_be64( bs, mdhd->modification_time );
        isom_bs_put_be32( bs, mdhd->timescale );
        isom_bs_put_be64( bs, mdhd->duration );
    }
    else
    {
        isom_bs_put_be32( bs, (uint32_t)mdhd->creation_time );
        isom_bs_put_be32( bs, (uint32_t)mdhd->modification_time );
        isom_bs_put_be32( bs, mdhd->timescale );
        isom_bs_put_be32( bs, (uint32_t)mdhd->duration );
    }
    isom_bs_put_be16( bs, mdhd->language );
    isom_bs_put_be16( bs, mdhd->pre_defined );
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_hdlr( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_hdlr_t *hdlr = trak->mdia->hdlr;
    if( !hdlr )
        return -1;
    isom_bs_put_full_header( bs, &hdlr->full_header );
    isom_bs_put_be32( bs, hdlr->pre_defined );
    isom_bs_put_be32( bs, hdlr->handler_type );
    for( uint32_t i = 0; i < 3; i++ )
        isom_bs_put_be32( bs, hdlr->reserved[i] );
    isom_bs_put_bytes( bs, hdlr->name, hdlr->name_length );
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_vmhd( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_vmhd_t *vmhd = trak->mdia->minf->vmhd;
    if( !vmhd )
        return -1;
    isom_bs_put_full_header( bs, &vmhd->full_header );
    isom_bs_put_be16( bs, vmhd->graphicsmode );
    for( uint32_t i = 0; i < 3; i++ )
        isom_bs_put_be16( bs, vmhd->opcolor[i] );
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_smhd( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_smhd_t *smhd = trak->mdia->minf->smhd;
    if( !smhd )
        return -1;
    isom_bs_put_full_header( bs, &smhd->full_header );
    isom_bs_put_be16( bs, smhd->balance );
    isom_bs_put_be16( bs, smhd->reserved );
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_hmhd( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_hmhd_t *hmhd = trak->mdia->minf->hmhd;
    if( !hmhd )
        return -1;
    isom_bs_put_full_header( bs, &hmhd->full_header );
    isom_bs_put_be16( bs, hmhd->maxPDUsize );
    isom_bs_put_be16( bs, hmhd->avgPDUsize );
    isom_bs_put_be32( bs, hmhd->maxbitrate );
    isom_bs_put_be32( bs, hmhd->avgbitrate );
    isom_bs_put_be32( bs, hmhd->reserved );
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_nmhd( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_nmhd_t *nmhd = trak->mdia->minf->nmhd;
    if( !nmhd )
        return -1;
    isom_bs_put_full_header( bs, &nmhd->full_header );
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_dref( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_dref_t *dref = trak->mdia->minf->dinf->dref;
    if( !dref || !dref->list )
        return -1;
    isom_bs_put_full_header( bs, &dref->full_header );
    isom_bs_put_be32( bs, dref->list->entry_count );
    for( isom_entry_t *entry = dref->list->head; entry; entry = entry->next )
    {
        isom_dref_entry_t *data = (isom_dref_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_full_header( bs, &data->full_header );
        if( data->full_header.type == ISOM_BOX_TYPE_URN )
            isom_bs_put_bytes( bs, data->name, data->name_length );
        isom_bs_put_bytes( bs, data->location, data->location_length );
    }
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_dinf( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_dinf_t *dinf = trak->mdia->minf->dinf;
    if( !dinf )
        return -1;
    isom_bs_put_base_header( bs, &dinf->base_header );
    if( isom_bs_write_data( bs ) )
        return -1;
    if( isom_write_dref( bs, trak ) )
        return -1;
    return 0;
}

static void isom_put_pasp( isom_bs_t *bs, isom_pasp_t *pasp )
{
    if( !pasp )
        return;
    isom_bs_put_base_header( bs, &pasp->base_header );
    isom_bs_put_be32( bs, pasp->hSpacing );
    isom_bs_put_be32( bs, pasp->vSpacing );
    return;
}

static void isom_put_clap( isom_bs_t *bs, isom_clap_t *clap )
{
    if( !clap )
        return;
    isom_bs_put_base_header( bs, &clap->base_header );
    isom_bs_put_be32( bs, clap->cleanApertureWidthN );
    isom_bs_put_be32( bs, clap->cleanApertureWidthD );
    isom_bs_put_be32( bs, clap->cleanApertureHeightN );
    isom_bs_put_be32( bs, clap->cleanApertureHeightD );
    isom_bs_put_be32( bs, clap->horizOffN );
    isom_bs_put_be32( bs, clap->horizOffD );
    isom_bs_put_be32( bs, clap->vertOffN );
    isom_bs_put_be32( bs, clap->vertOffD );
    return;
}

static int isom_put_ps_entries( isom_bs_t *bs, isom_entry_list_t *list )
{
    for( isom_entry_t *entry = list->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_be16( bs, data->parameterSetLength );
        isom_bs_put_bytes( bs, data->parameterSetNALUnit, data->parameterSetLength );
    }
    return 0;
}

static int isom_put_avcC( isom_bs_t *bs, isom_avcC_t *avcC )
{
    if( !bs || !avcC || !avcC->sequenceParameterSets || !avcC->pictureParameterSets )
        return -1;
    isom_bs_put_base_header( bs, &avcC->base_header );
    isom_bs_put_byte( bs, avcC->configurationVersion );
    isom_bs_put_byte( bs, avcC->AVCProfileIndication );
    isom_bs_put_byte( bs, avcC->profile_compatibility );
    isom_bs_put_byte( bs, avcC->AVCLevelIndication );
    isom_bs_put_byte( bs, avcC->lengthSizeMinusOne | 0xfc );            /* upper 6-bits are reserved as 111111b */
    isom_bs_put_byte( bs, avcC->numOfSequenceParameterSets | 0xe0 );    /* upper 3-bits are reserved as 111b */
    if( isom_put_ps_entries( bs, avcC->sequenceParameterSets ) )
        return -1;
    isom_bs_put_byte( bs, avcC->numOfPictureParameterSets );
    if( isom_put_ps_entries( bs, avcC->pictureParameterSets ) )
        return -1;
    if( ISOM_REQUIRES_AVCC_EXTENSION( avcC->AVCProfileIndication ) )
    {
        isom_bs_put_byte( bs, avcC->chroma_format | 0xfc );             /* upper 6-bits are reserved as 111111b */
        isom_bs_put_byte( bs, avcC->bit_depth_luma_minus8 | 0xf8 );     /* upper 5-bits are reserved as 11111b */
        isom_bs_put_byte( bs, avcC->bit_depth_chroma_minus8 | 0xf8 );   /* upper 5-bits are reserved as 11111b */
        isom_bs_put_byte( bs, avcC->numOfSequenceParameterSetExt );
        if( isom_put_ps_entries( bs, avcC->sequenceParameterSetExt ) )
            return -1;
    }
    return 0;
}

static void isom_put_btrt( isom_bs_t *bs, isom_btrt_t *btrt )
{
    if( !bs || !btrt )
        return;
    isom_bs_put_base_header( bs, &btrt->base_header );
    isom_bs_put_be32( bs, btrt->bufferSizeDB );
    isom_bs_put_be32( bs, btrt->maxBitrate );
    isom_bs_put_be32( bs, btrt->avgBitrate );
    return;
}

static int isom_write_esds( isom_bs_t *bs, isom_esds_t *esds )
{
    if( !bs || !esds )
        return -1;
    isom_bs_put_full_header( bs, &esds->full_header );
    return mp4sys_write_ES_Descriptor( bs, esds->ES );
}

static int isom_write_avc_entry( isom_bs_t *bs, isom_stsd_t *stsd )
{
    for( isom_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_avc_entry_t *data = (isom_avc_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_base_header( bs, &data->base_header );
        isom_bs_put_bytes( bs, data->reserved, 6 );
        isom_bs_put_be16( bs, data->data_reference_index );
        isom_bs_put_be16( bs, data->pre_defined1 );
        isom_bs_put_be16( bs, data->reserved1 );
        for( uint32_t j = 0; j < 3; j++ )
            isom_bs_put_be32( bs, data->pre_defined2[j] );
        isom_bs_put_be16( bs, data->width );
        isom_bs_put_be16( bs, data->height );
        isom_bs_put_be32( bs, data->horizresolution );
        isom_bs_put_be32( bs, data->vertresolution );
        isom_bs_put_be32( bs, data->reserved2 );
        isom_bs_put_be16( bs, data->frame_count );
        isom_bs_put_bytes( bs, data->compressorname, 32 );
        isom_bs_put_be16( bs, data->depth );
        isom_bs_put_be16( bs, data->pre_defined3 );
        isom_put_clap( bs, data->clap );
        isom_put_pasp( bs, data->pasp );
        if( !data->avcC )
            return -1;
        isom_put_avcC( bs, data->avcC );
        if( data->btrt )
            isom_put_btrt( bs, data->btrt );
        if( isom_bs_write_data( bs ) )
            return -1;
    }
    return 0;
}

static int isom_write_mp4a_entry( isom_bs_t *bs, isom_stsd_t *stsd )
{
    for( isom_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_mp4a_entry_t *data = (isom_mp4a_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_base_header( bs, &data->base_header );
        isom_bs_put_bytes( bs, data->reserved, 6 );
        isom_bs_put_be16( bs, data->data_reference_index );
        isom_bs_put_be32( bs, data->reserved1[0] );
        isom_bs_put_be32( bs, data->reserved1[1] );
        isom_bs_put_be16( bs, data->channelcount );
        isom_bs_put_be16( bs, data->samplesize );
        isom_bs_put_be16( bs, data->pre_defined );
        isom_bs_put_be16( bs, data->reserved2 );
        isom_bs_put_be32( bs, data->samplerate );
        if( isom_bs_write_data( bs ) )
            return -1;
        if( isom_write_esds( bs, data->esds ) );
            return -1;
    }
    return 0;
}

static int isom_write_visual_entry( isom_bs_t *bs, isom_stsd_t *stsd )
{
    for( isom_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_visual_entry_t *data = (isom_visual_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_base_header( bs, &data->base_header );
        isom_bs_put_bytes( bs, data->reserved, 6 );
        isom_bs_put_be16( bs, data->data_reference_index );
        isom_bs_put_be16( bs, data->pre_defined1 );
        isom_bs_put_be16( bs, data->reserved1 );
        for( uint32_t j = 0; j < 3; j++ )
            isom_bs_put_be32( bs, data->pre_defined2[j] );
        isom_bs_put_be16( bs, data->width );
        isom_bs_put_be16( bs, data->height );
        isom_bs_put_be32( bs, data->horizresolution );
        isom_bs_put_be32( bs, data->vertresolution );
        isom_bs_put_be32( bs, data->reserved2 );
        isom_bs_put_be16( bs, data->frame_count );
        isom_bs_put_bytes( bs, data->compressorname, 32 );
        isom_bs_put_be16( bs, data->depth );
        isom_bs_put_be16( bs, data->pre_defined3 );
        isom_put_clap( bs, data->clap );
        isom_put_pasp( bs, data->pasp );
        if( data->base_header.type == ISOM_CODEC_TYPE_AVC1_VIDEO )
        {
            isom_avc_entry_t *avc = (isom_avc_entry_t *)data;
            if( !avc || !avc->avcC )
                return -1;
            isom_put_avcC( bs, avc->avcC );
            if( avc->btrt )
                isom_put_btrt( bs, avc->btrt );
        }
        if( isom_bs_write_data( bs ) )
            return -1;
    }
    return 0;
}

static int isom_write_audio_entry( isom_bs_t *bs, isom_stsd_t *stsd )
{
    for( isom_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_audio_entry_t *data = (isom_audio_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_base_header( bs, &data->base_header );
        isom_bs_put_bytes( bs, data->reserved, 6 );
        isom_bs_put_be16( bs, data->data_reference_index );
        isom_bs_put_be32( bs, data->reserved1[0] );
        isom_bs_put_be32( bs, data->reserved1[1] );
        isom_bs_put_be16( bs, data->channelcount );
        isom_bs_put_be16( bs, data->samplesize );
        isom_bs_put_be16( bs, data->pre_defined );
        isom_bs_put_be16( bs, data->reserved2 );
        isom_bs_put_be32( bs, data->samplerate );
        if( isom_bs_write_data( bs ) )
            return -1;
    }
    return 0;
}

static int isom_write_hint_entry( isom_bs_t *bs, isom_stsd_t *stsd )
{
    for( isom_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_hint_entry_t *data = (isom_hint_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_base_header( bs, &data->base_header );
        isom_bs_put_bytes( bs, data->reserved, 6 );
        isom_bs_put_be16( bs, data->data_reference_index );
        if( data->data && data->data_length )
            isom_bs_put_bytes( bs, data->data, data->data_length );
        if( isom_bs_write_data( bs ) )
            return -1;
    }
    return 0;
}

static int isom_write_metadata_entry( isom_bs_t *bs, isom_stsd_t *stsd )
{
    for( isom_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_metadata_entry_t *data = (isom_metadata_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_base_header( bs, &data->base_header );
        isom_bs_put_bytes( bs, data->reserved, 6 );
        isom_bs_put_be16( bs, data->data_reference_index );
        if( isom_bs_write_data( bs ) )
            return -1;
    }
    return 0;
}

static int isom_write_stsd( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stsd_t *stsd = trak->mdia->minf->stbl->stsd;
    if( !stsd || !stsd->list || !stsd->list->head )
        return -1;
    isom_bs_put_full_header( bs, &stsd->full_header );
    isom_bs_put_be32( bs, stsd->list->entry_count );
    for( isom_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_sample_entry_t *sample = (isom_sample_entry_t *)entry->data;
        if( !sample )
            return -1;
        switch( sample->base_header.type )
        {
            case ISOM_CODEC_TYPE_AVC1_VIDEO :
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
                isom_write_avc_entry( bs, stsd );
                break;
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
                isom_write_mp4a_entry( bs, stsd );
                break;
            case ISOM_CODEC_TYPE_DRAC_VIDEO :
            case ISOM_CODEC_TYPE_ENCV_VIDEO :
            case ISOM_CODEC_TYPE_MJP2_VIDEO :
            case ISOM_CODEC_TYPE_MVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC2_VIDEO :
            case ISOM_CODEC_TYPE_S263_VIDEO :
            case ISOM_CODEC_TYPE_SVC1_VIDEO :
            case ISOM_CODEC_TYPE_VC_1_VIDEO :
                isom_write_visual_entry( bs, stsd );
                break;
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_DRA1_AUDIO :
            case ISOM_CODEC_TYPE_DTSC_AUDIO :
            case ISOM_CODEC_TYPE_DTSH_AUDIO :
            case ISOM_CODEC_TYPE_DTSL_AUDIO :
            case ISOM_CODEC_TYPE_EC_3_AUDIO :
            case ISOM_CODEC_TYPE_ENCA_AUDIO :
            case ISOM_CODEC_TYPE_G719_AUDIO :
            case ISOM_CODEC_TYPE_G726_AUDIO :
            case ISOM_CODEC_TYPE_M4AE_AUDIO :
            case ISOM_CODEC_TYPE_MLPA_AUDIO :
            case ISOM_CODEC_TYPE_RAW_AUDIO :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
            case ISOM_CODEC_TYPE_SAWP_AUDIO :
            case ISOM_CODEC_TYPE_SEVC_AUDIO :
            case ISOM_CODEC_TYPE_SQCP_AUDIO :
            case ISOM_CODEC_TYPE_SSMV_AUDIO :
            case ISOM_CODEC_TYPE_TWOS_AUDIO :
                isom_write_audio_entry( bs, stsd );
                break;
            case ISOM_CODEC_TYPE_FDP_HINT :
            case ISOM_CODEC_TYPE_M2TS_HINT :
            case ISOM_CODEC_TYPE_PM2T_HINT :
            case ISOM_CODEC_TYPE_PRTP_HINT :
            case ISOM_CODEC_TYPE_RM2T_HINT :
            case ISOM_CODEC_TYPE_RRTP_HINT :
            case ISOM_CODEC_TYPE_RSRP_HINT :
            case ISOM_CODEC_TYPE_RTP_HINT  :
            case ISOM_CODEC_TYPE_SM2T_HINT :
            case ISOM_CODEC_TYPE_SRTP_HINT :
                isom_write_hint_entry( bs, stsd );
                break;
            case ISOM_CODEC_TYPE_IXSE_META :
            case ISOM_CODEC_TYPE_METT_META :
            case ISOM_CODEC_TYPE_METX_META :
            case ISOM_CODEC_TYPE_MLIX_META :
            case ISOM_CODEC_TYPE_OKSD_META :
            case ISOM_CODEC_TYPE_SVCM_META :
            case ISOM_CODEC_TYPE_TEXT_META :
            case ISOM_CODEC_TYPE_URIM_META :
            case ISOM_CODEC_TYPE_XML_META  :
                isom_write_metadata_entry( bs, stsd );
                break;
        }
    }
    return 0;
}

static int isom_write_stts( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stts_t *stts = trak->mdia->minf->stbl->stts;
    if( !stts || !stts->list )
        return -1;
    isom_bs_put_full_header( bs, &stts->full_header );
    isom_bs_put_be32( bs, stts->list->entry_count );
    for( isom_entry_t *entry = stts->list->head; entry; entry = entry->next )
    {
        isom_stts_entry_t *data = (isom_stts_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_be32( bs, data->sample_count );
        isom_bs_put_be32( bs, data->sample_delta );
    }
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_ctts( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_ctts_t *ctts = trak->mdia->minf->stbl->ctts;
    if( !ctts )
        return 0;
    if( !ctts->list )
        return -1;
    isom_bs_put_full_header( bs, &ctts->full_header );
    isom_bs_put_be32( bs, ctts->list->entry_count );
    for( isom_entry_t *entry = ctts->list->head; entry; entry = entry->next )
    {
        isom_ctts_entry_t *data = (isom_ctts_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_be32( bs, data->sample_count );
        isom_bs_put_be32( bs, data->sample_offset );
    }
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_stsz( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stsz_t *stsz = trak->mdia->minf->stbl->stsz;
    if( !stsz )
        return -1;
    isom_bs_put_full_header( bs, &stsz->full_header );
    isom_bs_put_be32( bs, stsz->sample_size );
    isom_bs_put_be32( bs, stsz->sample_count );
    if( stsz->sample_size == 0 && stsz->list )
        for( isom_entry_t *entry = stsz->list->head; entry; entry = entry->next )
        {
            isom_stsz_entry_t *data = (isom_stsz_entry_t *)entry->data;
            if( !data )
                return -1;
            isom_bs_put_be32( bs, data->entry_size );
        }
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_stss( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stss_t *stss = trak->mdia->minf->stbl->stss;
    if( !stss )
        return 0;   /* If the sync sample box is not present, every sample is a random access point. */
    if( !stss->list )
        return -1;
    isom_bs_put_full_header( bs, &stss->full_header );
    isom_bs_put_be32( bs, stss->list->entry_count );
    for( isom_entry_t *entry = stss->list->head; entry; entry = entry->next )
    {
        isom_stss_entry_t *data = (isom_stss_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_be32( bs, data->sample_number );
    }
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_sdtp( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_sdtp_t *sdtp = trak->mdia->minf->stbl->sdtp;
    if( !sdtp )
        return 0;
    if( !sdtp->list )
        return -1;
    isom_bs_put_full_header( bs, &sdtp->full_header );
    for( isom_entry_t *entry = sdtp->list->head; entry; entry = entry->next )
    {
        isom_sdtp_entry_t *data = (isom_sdtp_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_byte( bs, (data->is_leading<<6) |
                              (data->sample_depends_on<<4) |
                              (data->sample_is_depended_on<<2) |
                               data->sample_has_redundancy );
    }
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_stsc( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stsc_t *stsc = trak->mdia->minf->stbl->stsc;
    if( !stsc || !stsc->list )
        return -1;
    isom_bs_put_full_header( bs, &stsc->full_header );
    isom_bs_put_be32( bs, stsc->list->entry_count );
    for( isom_entry_t *entry = stsc->list->head; entry; entry = entry->next )
    {
        isom_stsc_entry_t *data = (isom_stsc_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_be32( bs, data->first_chunk );
        isom_bs_put_be32( bs, data->samples_per_chunk );
        isom_bs_put_be32( bs, data->sample_description_index );
    }
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_co64( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stco_t *co64 = trak->mdia->minf->stbl->stco;
    if( !co64 || !co64->list )
        return -1;
    isom_bs_put_full_header( bs, &co64->full_header );
    isom_bs_put_be32( bs, co64->list->entry_count );
    for( isom_entry_t *entry = co64->list->head; entry; entry = entry->next )
    {
        isom_co64_entry_t *data = (isom_co64_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_be64( bs, data->chunk_offset );
    }
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_stco( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stco_t *stco = trak->mdia->minf->stbl->stco;
    if( !stco || !stco->list )
        return -1;
    if( stco->large_presentation )
        return isom_write_co64( bs, trak );
    isom_bs_put_full_header( bs, &stco->full_header );
    isom_bs_put_be32( bs, stco->list->entry_count );
    for( isom_entry_t *entry = stco->list->head; entry; entry = entry->next )
    {
        isom_stco_entry_t *data = (isom_stco_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_be32( bs, data->chunk_offset );
    }
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_sbgp( isom_bs_t *bs, isom_trak_entry_t *trak, uint32_t grouping_number )
{
    isom_sbgp_t *sbgp = trak->mdia->minf->stbl->sbgp + grouping_number - 1;
    if( !sbgp || !sbgp->list )
        return -1;
    isom_bs_put_full_header( bs, &sbgp->full_header );
    isom_bs_put_be32( bs, sbgp->grouping_type );
    isom_bs_put_be32( bs, sbgp->list->entry_count );
    for( isom_entry_t *entry = sbgp->list->head; entry; entry = entry->next )
    {
        isom_sbgp_entry_t *data = (isom_sbgp_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_be32( bs, data->sample_count );
        isom_bs_put_be32( bs, data->group_description_index );
    }
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_sgpd( isom_bs_t *bs, isom_trak_entry_t *trak, uint32_t grouping_number )
{
    isom_sgpd_t *sgpd = trak->mdia->minf->stbl->sgpd + grouping_number - 1;
    if( !sgpd || !sgpd->list )
        return -1;
    isom_bs_put_full_header( bs, &sgpd->full_header );
    isom_bs_put_be32( bs, sgpd->grouping_type );
    if( sgpd->full_header.version == 1 )
        isom_bs_put_be32( bs, sgpd->default_length );
    isom_bs_put_be32( bs, sgpd->list->entry_count );
    for( isom_entry_t *entry = sgpd->list->head; entry; entry = entry->next )
    {
        if( !entry->data )
            return -1;
        if( sgpd->full_header.version == 1 && !sgpd->default_length )
            isom_bs_put_be32( bs, ((isom_sample_group_entry_t *)entry->data)->description_length );
        switch( sgpd->grouping_type )
        {
            case ISOM_GROUP_TYPE_ROLL :
            {
                isom_roll_group_entry_t *data = (isom_roll_group_entry_t *)entry->data;
                isom_bs_put_be16( bs, data->roll_distance );
                break;
            }
            default :
                break;
        }
    }
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_stbl( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    if( !stbl )
        return -1;
    isom_bs_put_base_header( bs, &stbl->base_header );
    if( isom_bs_write_data( bs ) )
        return -1;
    if( isom_write_stsd( bs, trak ) )
        return -1;
    if( isom_write_stts( bs, trak ) )
        return -1;
    if( isom_write_ctts( bs, trak ) )
        return -1;
    if( isom_write_stss( bs, trak ) )
        return -1;
    if( isom_write_sdtp( bs, trak ) )
        return -1;
    if( isom_write_stsc( bs, trak ) )
        return -1;
    if( isom_write_stsz( bs, trak ) )
        return -1;
    if( isom_write_stco( bs, trak ) )
        return -1;
    for( uint32_t i = 1; i <= trak->mdia->minf->stbl->grouping_count; i++ )
    {
        if( isom_write_sbgp( bs, trak, i ) )
            return -1;
        if( isom_write_sgpd( bs, trak, i ) )
            return -1;
    }
    return 0;
}

static int isom_write_minf( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_minf_t *minf = trak->mdia->minf;
    if( !minf )
        return -1;
    isom_bs_put_base_header( bs, &minf->base_header );
    if( isom_bs_write_data( bs ) )
        return -1;
    if( minf->vmhd && isom_write_vmhd( bs, trak ) )
        return -1;
    if( minf->smhd && isom_write_smhd( bs, trak ) )
        return -1;
    if( minf->hmhd && isom_write_hmhd( bs, trak ) )
        return -1;
    if( minf->nmhd && isom_write_nmhd( bs, trak ) )
        return -1;
    if( isom_write_dinf( bs, trak ) )
        return -1;
    if( isom_write_stbl( bs, trak ) )
        return -1;
    return 0;
}

static int isom_write_mdia( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_mdia_t *mdia = trak->mdia;
    if( !mdia )
        return -1;
    isom_bs_put_base_header( bs, &mdia->base_header );
    if( isom_bs_write_data( bs ) )
        return -1;
    if( isom_write_mdhd( bs, trak ) )
        return -1;
    if( isom_write_hdlr( bs, trak ) )
        return -1;
    if( isom_write_minf( bs, trak ) )
        return -1;
    return 0;
}

static int isom_write_chpl( isom_bs_t *bs, isom_chpl_t *chpl )
{
    if( !chpl )
        return 0;
    if( !chpl->list )
        return -1;
    isom_bs_put_full_header( bs, &chpl->full_header );
    isom_bs_put_byte( bs, chpl->reserved );
    isom_bs_put_be32( bs, chpl->list->entry_count );
    for( isom_entry_t *entry = chpl->list->head; entry; entry = entry->next )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_be64( bs, data->start_time );
        isom_bs_put_byte( bs, data->name_length );
        isom_bs_put_bytes( bs, data->chapter_name, data->name_length );
    }
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_udta( isom_bs_t *bs, isom_moov_t *moov, isom_trak_entry_t *trak )
{
    /* Setting non-NULL pointer to trak means trak->udta data will be written in stream.
     * If trak is set by NULL while moov is set by non-NULL pointer, moov->udta data will be written in stream. */
    isom_udta_t *udta = trak ? trak->udta : moov ? moov->udta : NULL;
    if( !udta )
        return 0;
    isom_bs_put_base_header( bs, &udta->base_header );
    if( isom_bs_write_data( bs ) )
        return -1;
    if( moov && isom_write_chpl( bs, udta->chpl ) )
        return -1;
    return 0;
}

static int isom_write_trak( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    if( !trak )
        return -1;
    isom_bs_put_base_header( bs, &trak->base_header );
    if( isom_bs_write_data( bs ) )
        return -1;
    if( isom_write_tkhd( bs, trak ) )
        return -1;
    if( isom_write_edts( bs, trak ) )
        return -1;
    if( isom_write_mdia( bs, trak ) )
        return -1;
    if( isom_write_udta( bs, NULL, trak ) )
        return -1;
    return 0;
}

static int isom_write_iods( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->iods )
        return -1;
    isom_iods_t *iods = root->moov->iods;
    isom_bs_t *bs = root->bs;
    isom_bs_put_full_header( bs, &iods->full_header );
    return mp4sys_write_ObjectDescriptor( bs, iods->OD );
}

static int isom_write_mvhd( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return -1;
    isom_mvhd_t *mvhd = root->moov->mvhd;
    isom_bs_t *bs = root->bs;
    isom_bs_put_full_header( bs, &mvhd->full_header );
    if( mvhd->full_header.version )
    {
        isom_bs_put_be64( bs, mvhd->creation_time );
        isom_bs_put_be64( bs, mvhd->modification_time );
        isom_bs_put_be32( bs, mvhd->timescale );
        isom_bs_put_be64( bs, mvhd->duration );
    }
    else
    {
        isom_bs_put_be32( bs, (uint32_t)mvhd->creation_time );
        isom_bs_put_be32( bs, (uint32_t)mvhd->modification_time );
        isom_bs_put_be32( bs, mvhd->timescale );
        isom_bs_put_be32( bs, (uint32_t)mvhd->duration );
    }
    isom_bs_put_be32( bs, mvhd->rate );
    isom_bs_put_be16( bs, mvhd->volume );
    isom_bs_put_bytes( bs, mvhd->reserved, 10 );
    for( uint32_t i = 0; i < 9; i++ )
        isom_bs_put_be32( bs, mvhd->matrix[i] );
    for( uint32_t i = 0; i < 6; i++ )
        isom_bs_put_be32( bs, mvhd->pre_defined[i] );
    isom_bs_put_be32( bs, mvhd->next_track_ID );
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

int isom_write_moov( isom_root_t *root )
{
    if( !root || !root->moov )
        return -1;
    isom_bs_put_base_header( root->bs, &root->moov->base_header );
    if( isom_bs_write_data( root->bs ) )
        return -1;
    if( isom_write_mvhd( root ) )
        return -1;
    if( isom_write_iods( root ) )
        return -1;
    if( root->moov->trak_list )
        for( isom_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
            if( isom_write_trak( root->bs, (isom_trak_entry_t *)entry->data ) )
                return -1;
    if( isom_write_udta( root->bs, root->moov, NULL ) )
        return -1;
    return 0;
}

int isom_write_free( isom_root_t *root )
{
    if( !root || !root->bs || !root->free )
        return -1;
    isom_free_t *skip = root->free;
    isom_bs_t *bs = root->bs;
    skip->base_header.size = 8 + skip->length;
    isom_bs_put_base_header( bs, &skip->base_header );
    if( skip->data && skip->length )
        isom_bs_put_bytes( bs, skip->data, skip->length );
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

static int isom_write_mdat_header( isom_root_t *root )
{
    if( !root || !root->bs || !root->mdat )
        return -1;
    isom_mdat_t *mdat = root->mdat;
    isom_bs_t *bs = root->bs;
    mdat->base_header.size = 16;
    mdat->large_flag = 0;
    isom_bs_put_base_header( bs, &mdat->base_header );
    isom_bs_put_be64( bs, 0 );     /* reserved for largesize */
    mdat->header_pos = ftell( bs->stream );
    if( isom_bs_write_data( bs ) )
        return -1;
    return 0;
}

uint32_t isom_get_trak_number( isom_trak_entry_t *trak )
{
    if( !trak || !trak->root || !trak->root->moov || !trak->root->moov->trak_list )
        return 0;
    uint32_t i = 1;
    for( isom_entry_t *entry = trak->root->moov->trak_list->head; entry; entry = entry->next )
    {
        if( trak == (isom_trak_entry_t *)entry->data )
            return i;
        ++i;
    }
    return 0;
}

uint32_t isom_get_sample_count( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsz )
        return 0;
    return trak->mdia->minf->stbl->stsz->sample_count;
}

uint64_t isom_get_dts( isom_stts_t *stts, uint32_t sample_number )
{
    if( !stts || !stts->list )
        return 0;
    uint64_t dts = 0;
    uint32_t i = 1;
    isom_entry_t *entry;
    isom_stts_entry_t *data;
    for( entry = stts->list->head; entry; entry = entry->next )
    {
        data = (isom_stts_entry_t *)entry->data;
        if( !data )
            return 0;
        if( i + data->sample_count > sample_number )
            break;
        for( uint32_t j = 0; j < data->sample_count; j++ )
            dts += data->sample_delta;
        i += data->sample_count;
    }
    if( !entry )
        return 0;
    while( i++ < sample_number )
        dts += data->sample_delta;
    return dts;
}

uint64_t isom_get_cts( isom_stts_t *stts, isom_ctts_t *ctts, uint32_t sample_number )
{
    if( !stts || !stts->list )
        return 0;
    if( !ctts )
        return isom_get_dts( stts, sample_number );
    uint32_t i = 1;     /* This can be 0 (and then condition below shall be changed) but I dare use same algorithm with isom_get_dts. */
    isom_entry_t *entry;
    isom_ctts_entry_t *data;
    if( sample_number == 0 )
        return 0;
    for( entry = ctts->list->head; entry; entry = entry->next )
    {
        data = (isom_ctts_entry_t *)entry->data;
        if( !data )
            return 0;
        if( i + data->sample_count > sample_number )
            break;
        i += data->sample_count;
    }
    if( !entry )
        return 0;
    return isom_get_dts(stts, sample_number) + data->sample_offset;
}

uint32_t isom_get_media_timescale( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return 0;
    return trak->mdia->mdhd->timescale;
}

uint32_t isom_get_movie_timescale( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return 0;
    return root->moov->mvhd->timescale;
}

static int isom_update_mdhd_duration( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->minf || !trak->mdia->minf->stbl ||
        !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list || trak->mdia->minf->stbl->stts->list->entry_count == 0 )
        return -1;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    isom_stts_t *stts = trak->mdia->minf->stbl->stts;
    isom_ctts_t *ctts = trak->mdia->minf->stbl->ctts;
    mdhd->duration = 0;
    uint32_t sample_count = isom_get_sample_count( trak );
    if( sample_count == 0 )
        return -1;
    /* Now we have at least 1 sample, so do stts_entry. */
    isom_entry_t *last_stts = stts->list->tail;
    isom_stts_entry_t *last_stts_data = (isom_stts_entry_t *)last_stts->data;
    if( sample_count == 1 )
        mdhd->duration = last_stts_data->sample_delta;
    /* Now we have at least 2 samples,
     * but dunno whether 1 stts_entry which has 2 samples or 2 stts_entry which has 1 samle each. */
    else if( !ctts )
    {
        /* use dts instead of cts */
        mdhd->duration = isom_get_dts( stts, sample_count );
        if( last_stts_data->sample_count > 1 )
            mdhd->duration += last_stts_data->sample_delta; /* no need to update last_stts_data->sample_delta */
        else
        {
            uint32_t i = 0;
            for( isom_entry_t *entry = stts->list->head; entry; entry = entry->next )
                ++i;
            isom_remove_entry( stts->list, i );     /* Remove the last entry. */
            ++ ((isom_stts_entry_t *)stts->list->tail->data)->sample_count;
            mdhd->duration += ((isom_stts_entry_t *)stts->list->tail->data)->sample_delta;
        }
    }
    else
    {
        if( !ctts->list || ctts->list->entry_count == 0 )
            return -1;
        uint64_t dts = 0;
        uint64_t max_cts = 0, max2_cts = 0, min_cts = UINT64_MAX;
        uint32_t j, k;
        isom_entry_t *stts_entry = stts->list->head;
        isom_entry_t *ctts_entry = ctts->list->head;
        j = k = 0;
        for( uint32_t i = 0; i < sample_count; i++ )
        {
            if( !ctts_entry || !stts_entry )
                return -1;
            isom_stts_entry_t *stts_data = (isom_stts_entry_t *)stts_entry->data;
            isom_ctts_entry_t *ctts_data = (isom_ctts_entry_t *)ctts_entry->data;
            if( !stts_data || !ctts_data )
                return -1;
            uint64_t cts = dts + ctts_data->sample_offset;
            min_cts = ISOM_MIN( min_cts, cts );
            if( max_cts < cts )
            {
                max2_cts = max_cts;
                max_cts = cts;
            }else if( max2_cts < cts )
                max2_cts = cts;
            dts += stts_data->sample_delta;
            /* If finished sample_count of current entry, move to next. */
            if( ++j == ctts_data->sample_count )
            {
                ctts_entry = ctts_entry->next;
                j = 0;
            }
            if( ++k == stts_data->sample_count )
            {
                stts_entry = stts_entry->next;
                k = 0;
            }
        }
        dts -= last_stts_data->sample_delta;
        /* The spec allows an arbitrary value for the duration of the last sample. So, we pick last-1 sample's. */
        uint32_t last_delta = max_cts - max2_cts;
        mdhd->duration = max_cts - min_cts + last_delta;
        /* To match dts and mdhd duration, update stts and mdhd relatively. */
        if( mdhd->duration > dts )
            last_delta = mdhd->duration - dts;
        else
            mdhd->duration = dts + last_delta; /* mdhd duration must not less than last dts. */
        if( last_stts_data->sample_count > 1 && last_delta != last_stts_data->sample_delta )
        {
            last_stts_data->sample_count -= 1;
            if( isom_add_stts_entry( root, trak_number, last_delta ) )
                return -1;
        }
        else
            last_stts_data->sample_delta = last_delta;
    }
    if( mdhd->duration > UINT32_MAX )
        mdhd->full_header.version = 1;
    return 0;
}

static int isom_update_tkhd_duration( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->tkhd )
        return -1;
    isom_tkhd_t *tkhd = trak->tkhd;
    tkhd->duration = 0;
    if( !trak->edts || !trak->edts->elst )
    {
        if( !trak->mdia || !trak->mdia->mdhd || !trak->root || !trak->root->moov || !trak->root->moov->mvhd || !trak->mdia->mdhd->timescale )
            return -1;
        if( !trak->mdia->mdhd->duration && isom_update_mdhd_duration( root, trak_number ) )
            return -1;
        tkhd->duration = trak->mdia->mdhd->duration * ((double)trak->root->moov->mvhd->timescale / trak->mdia->mdhd->timescale);
    }
    else
    {
        tkhd->duration = 0;
        for( isom_entry_t *entry = trak->edts->elst->list->head; entry; entry = entry->next )
        {
            isom_elst_entry_t *data = (isom_elst_entry_t *)entry->data;
            if( !data )
                return -1;
            tkhd->duration += data->segment_duration;
        }
    }
    if( tkhd->duration > UINT32_MAX )
        tkhd->full_header.version = 1;
    if( !tkhd->duration )
        tkhd->duration = tkhd->full_header.version == 1 ? 0xffffffff : 0xffff;
    return 0;
}

static int isom_update_mvhd_duration( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return -1;
    isom_mvhd_t *mvhd = root->moov->mvhd;
    mvhd->duration = 0;
    for( isom_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
    {
        /* We pick maximum track duration as movie duration. */
        isom_trak_entry_t *data = (isom_trak_entry_t *)entry->data;
        if( !data || !data->tkhd )
            return -1;
        mvhd->duration = entry != root->moov->trak_list->head ? ISOM_MAX( mvhd->duration, data->tkhd->duration ) : data->tkhd->duration;
    }
    if( mvhd->duration > UINT32_MAX )
        mvhd->full_header.version = 1;
    return 0;
}

int isom_update_track_duration( isom_root_t *root, uint32_t trak_number )
{
    if( isom_update_mdhd_duration( root, trak_number ) ||
        isom_update_tkhd_duration( root, trak_number ) ||
        isom_update_mvhd_duration( root ) )
        return -1;
    return 0;
}

int isom_add_mandatory_boxes( isom_root_t *root, uint32_t hdlr_type )
{
    isom_add_ftyp( root );
    if( !root->moov )
    {
        isom_add_moov( root );
        isom_add_mvhd( root );
        isom_add_trak( root, hdlr_type );
    }
    return 0;
}

#define isom_add_size isom_add_stsz_entry

static int isom_add_sync_point( isom_root_t *root, uint32_t trak_number, isom_trak_entry_t *trak, uint32_t sample_number, isom_sample_property_t *prop )
{
    if( !prop->sync_point ) /* no null check for prop */
        return 0;
    if( !trak_number )
        trak_number = isom_get_trak_number( trak );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    if( !trak->mdia->minf->stbl->stss && isom_add_stss( root, trak_number ) )
        return -1;
    return isom_add_stss_entry( root, trak_number, sample_number );
}

static int isom_add_dependency_type( isom_root_t *root, uint32_t trak_number, isom_trak_entry_t *trak, isom_sample_property_t* prop )
{
    if( !trak_number )
        trak_number = isom_get_trak_number( trak );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl ) /* no null check for prop */
        return -1;
    if( trak->mdia->minf->stbl->sdtp )
        return isom_add_sdtp_entry( root, trak_number, prop );
    if( !prop->leading && !prop->independent && !prop->disposable && !prop->redundant )
        return 0;
    if( isom_add_sdtp( root, trak_number ) )
        return -1;
    uint32_t count = isom_get_sample_count( trak );
    /* fill past samples with ISOM_SAMPLE_*_UNKNOWN */
    isom_sample_property_t null_prop = { 0 };
    for( uint32_t i = 1; i < count; i++ )
        if( isom_add_sdtp_entry( root, trak_number, &null_prop ) )
            return -1;
    return isom_add_sdtp_entry( root, trak_number, prop );
}

/* returns 1 if pooled samples must be flushed. */
/* FIXME: I wonder if this function should have a extra argument which indicates force_to_flush_cached_chunk.
   see isom_write_sample for detail. */
static int isom_add_chunk( isom_root_t *root, uint32_t trak_number, isom_trak_entry_t *trak, isom_sample_t *sample, double max_chunk_duration )
{
    if( !trak_number )
        trak_number = isom_get_trak_number( trak );
    if( !trak || !trak->cache || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->mdhd->timescale || !trak->mdia->minf || !trak->mdia->minf->stbl ||
        !trak->mdia->minf->stbl->stsc || !trak->mdia->minf->stbl->stsc->list )
        return -1;
    isom_chunk_cache_t *current = &trak->cache->chunk;
    if( current->chunk_number == 0 )
    {
        /* Very initial settings, just once per trak */
        current->pool = isom_create_entry_list();
        if( !current->pool )
            return -1;
        current->chunk_number = 1;
        current->sample_description_index = sample->index;
        current->first_dts = 0;
    }
    if( sample->dts < current->first_dts )
        return -1; /* easy error check. */
    double chunk_duration = (double)(sample->dts - current->first_dts) / trak->mdia->mdhd->timescale;
    if( max_chunk_duration >= chunk_duration )
        return 0; /* no need to flush current cached chunk, the current sample must be put into that. */

    /* NOTE: chunk relative stuff must be pushed into root after a chunk is fully determined with its contents. */
    /* now current cached chunk is fixed, actually add chunk relative properties to root accordingly. */

    isom_stsc_t *stsc = trak->mdia->minf->stbl->stsc;
    /* Add a new chunk sequence in this track if needed. */
    if( !stsc->list->tail || current->pool->entry_count != ((isom_stsc_entry_t *)stsc->list->tail->data)->samples_per_chunk )
    {
        if( isom_add_stsc_entry( root, trak_number, current->chunk_number, current->pool->entry_count, current->sample_description_index ) )
            return -1;
    }
    /* Add a new chunk offset in this track here. */
    if( isom_add_stco_entry( root, trak_number, root->bs->written ) )
        return -1;
    /* update cache information */
    ++ current->chunk_number;
    /* re-initialize cache, using the current sample */
    current->sample_description_index = sample->index;
    current->first_dts = sample->dts;
    /* current->pool must be flushed in isom_write_sample() */
    return 1;
}

static int isom_add_dts( isom_root_t *root, uint32_t trak_number, isom_trak_entry_t *trak, uint64_t dts )
{
    if( !trak_number )
        trak_number = isom_get_trak_number( trak );
    if( !trak_number || !trak->cache || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl ||
        !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list )
        return -1;
    isom_stts_t *stts = trak->mdia->minf->stbl->stts;
    if( !stts->list->entry_count )
    {
        if( isom_add_stts_entry( root, trak_number, dts ) )
            return -1;
        trak->cache->ts.dts = dts;
        return 0;
    }
    if( dts <= trak->cache->ts.dts )
        return -1;
    uint32_t sample_delta = dts - trak->cache->ts.dts;
    isom_stts_entry_t *data = (isom_stts_entry_t *)stts->list->tail->data;
    if( data->sample_delta == sample_delta )
        ++ data->sample_count;
    else if( isom_add_stts_entry( root, trak_number, sample_delta ) )
        return -1;
    trak->cache->ts.dts = dts;
    return 0;
}

static int isom_add_cts( isom_root_t *root, uint32_t trak_number, isom_trak_entry_t *trak, uint64_t cts )
{
    if( !trak_number )
        trak_number = isom_get_trak_number( trak );
    if( !trak_number || !trak->cache || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    isom_ctts_t *ctts = trak->mdia->minf->stbl->ctts;
    if( !ctts )
    {
        if( cts == trak->cache->ts.dts )
        {
            trak->cache->ts.cts = cts;
            return 0;
        }
        /* Add ctts box and the first ctts entry. */
        if( isom_add_ctts( root, trak_number ) || isom_add_ctts_entry( root, trak_number, 0 ) )
            return -1;
        uint32_t sample_count = isom_get_sample_count( trak );
        ctts = trak->mdia->minf->stbl->ctts;
        isom_ctts_entry_t *data = (isom_ctts_entry_t *)ctts->list->head->data;
        if( sample_count != 1 )
        {
            data->sample_count = isom_get_sample_count( trak ) - 1;
            if( isom_add_ctts_entry( root, trak_number, cts - trak->cache->ts.dts ) )
                return -1;
        }
        else
            data->sample_offset = cts;
        trak->cache->ts.cts = cts;
        return 0;
    }
    if( !ctts->list )
        return -1;
    isom_ctts_entry_t *data = (isom_ctts_entry_t *)ctts->list->tail->data;
    uint32_t sample_offset = cts - trak->cache->ts.dts;
    if( data->sample_offset == sample_offset )
        ++ data->sample_count;
    else if( isom_add_ctts_entry( root, trak_number, sample_offset ) )
        return -1;
    trak->cache->ts.cts = cts;
    return 0;
}

static inline int isom_add_timestamp( isom_root_t *root, uint32_t trak_number, isom_trak_entry_t *trak, uint64_t dts, uint64_t cts )
{
    if( cts < dts )
        return -1;
    if( !trak_number )
        trak_number = isom_get_trak_number( trak );
    if( isom_get_sample_count( trak ) > 1 && isom_add_dts( root, trak_number, trak, dts ) )
        return -1;
    return isom_add_cts( root, trak_number, trak, cts );
}

static int isom_write_sample_data( isom_root_t *root, isom_sample_t *sample )
{
    if( !root || !root->mdat || !root->bs || !root->bs->stream )
        return -1;
    isom_bs_put_bytes( root->bs, sample->data, sample->length );
    if( isom_bs_write_data( root->bs ) )
        return -1;
    root->mdat->base_header.size += sample->length;
    return 0;
}

static int isom_write_pooled_samples( isom_root_t *root, uint32_t trak_number, isom_trak_entry_t *trak, isom_entry_list_t *pool )
{
    for( isom_entry_t *entry = pool->head; entry; entry = entry->next )
    {
        isom_sample_t *data = (isom_sample_t *)entry->data;
        if( !data || !data->data )
            return -1;
        /* Add a sample_size and increment sample_count. */
        if( isom_add_size( root, trak_number, data->length ) )
            return -1;
        /* Add a decoding timestamp and a composition timestamp. */
        if( isom_add_timestamp( root, trak_number, trak, data->dts, data->cts ) )
            return -1;
        /* Add a sync point if needed. */
        if( isom_add_sync_point( root, trak_number, trak, isom_get_sample_count( trak ), &data->prop ) )
            return -1;
        /* Add leading, independent, disposable and redundant information if needed. */
        if( isom_add_dependency_type( root, trak_number, trak, &data->prop ) )
            return -1;
        if( isom_write_sample_data( root, data ) )
            return -1;
    }
    isom_remove_entries( pool, isom_remove_sample );
    return 0;
}

/* FIXME: This function should be removed after we implement "sample buffer pool system". */
static isom_sample_t *isom_duplicate_sample( isom_sample_t *sample )
{
    if( !sample || !sample->data ) /* This function may belong to public some time, so do not remove this. */
        return NULL;
    isom_sample_t *new_sample = (isom_sample_t *)malloc( sizeof(isom_sample_t) );
    if( !new_sample )
        return NULL;
    *new_sample = *sample;
    /* FIXME: Currently type of data is "char", but I'd prefer "uint8_t" or "void". */
    new_sample->data = (char *)malloc( sample->length );
    if( !new_sample->data )
    {
        free( new_sample );
        return NULL;
    }
    memcpy( new_sample->data, sample->data, sample->length );
    return new_sample;
}

int isom_write_sample( isom_root_t *root, uint32_t trak_number, isom_sample_t *sample, double max_chunk_duration )
{
    /* I myself think max_chunk_duration == 0, whici means all samples will be cached on memory, should be prevented.
       This means removal of a feature that we used to have, but anyway very alone chunk does not make sense. */
    if( !root || !sample || !sample->data || max_chunk_duration == 0 )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak )
        return -1;

    /* Add a chunk if needed. */
    /*
     * FIXME: I think we have to implement "arbitrate chunk handling between tracks" system.
     * Which means, even if a chunk of a trak has not exceeded max_chunk_duration yet,
     * the chunk should be forced to be fixed and determined so that it shall be written into the file.
     * Without that, for example, a video sample with frame rate of 0.01fps would not be written
     * near the corresponding audio sample.
     * As a result, players(demuxers) have to use fseek to playback that kind of mp4 in A/V sync.
     * Note that even though we cannot help the case with random access (i.e. seek) even with this system,
     * we should do it.
     */
    int ret = isom_add_chunk( root, trak_number, trak, sample, max_chunk_duration );
    if( ret < 0 )
        return -1;

    /* ret == 1 means cached samples must be flushed. */
    isom_chunk_cache_t *current = &trak->cache->chunk;
    if( ret == 1 && isom_write_pooled_samples( root, trak_number, trak, current->pool ) )
        return -1;

    /* anyway the current sample must be pooled. */
    /* FIXME: Duplicate sample. This is not effective way.
       We have to implement sample buffer pool in the top level, and calling application should use that
       in order to reduce memcpy(). */
    isom_sample_t *dup_sample = isom_duplicate_sample( sample );
    if( !dup_sample )
        return -1;
    if( isom_add_entry( current->pool, dup_sample ) )
        return -1;
    return 0;
}

static int isom_output_cache( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->cache || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl ||
        !trak->mdia->minf->stbl->stsc || !trak->mdia->minf->stbl->stsc->list )
        return -1;
    isom_chunk_cache_t *current = &trak->cache->chunk;
    if( !trak->mdia->minf->stbl->stsc->list->tail ||
        current->pool->entry_count != ((isom_stsc_entry_t *)trak->mdia->minf->stbl->stsc->list->tail->data)->samples_per_chunk )
        if( isom_add_stsc_entry( root, trak_number, current->chunk_number, current->pool->entry_count, current->sample_description_index ) )
            return -1;
    if( isom_add_stco_entry( root, trak_number, root->bs->written ) ||
        isom_write_pooled_samples( root, trak_number, trak, current->pool ) )
        return -1;
    return 0;
}

int isom_write_mdat_size( isom_root_t *root )
{
    if( !root || !root->bs || !root->bs->stream || !root->mdat )
        return -1;
    isom_mdat_t *mdat = root->mdat;
    if( mdat->base_header.size > UINT32_MAX )
        mdat->large_flag = 1;
    isom_bs_t *bs = root->bs;
    FILE *stream = bs->stream;
    uint64_t current_pos = ftell( stream );
    fseek( stream, mdat->header_pos, SEEK_SET );
    if( mdat->large_flag )
    {
        isom_bs_put_be32( bs, 1 );
        isom_bs_put_be32( bs, ISOM_BOX_TYPE_MDAT );
        isom_bs_put_be64( bs, mdat->base_header.size );
    }
    else
    {
        isom_bs_put_be32( bs, mdat->base_header.size );
        isom_bs_put_be32( bs, ISOM_BOX_TYPE_MDAT );
    }
    if( isom_bs_write_data( bs ) )
        return -1;
    fseek( stream, current_pos, SEEK_SET );
    return 0;
}

int isom_set_brands( isom_root_t *root, uint32_t major_brand, uint32_t minor_version, uint32_t *brands, uint32_t brand_count )
{
    if( !root->ftyp || !brand_count )
        return -1;
    isom_ftyp_t *ftyp = root->ftyp;
    ftyp->major_brand = major_brand;
    ftyp->minor_version = minor_version;
    ftyp->compatible_brands = malloc( brand_count * sizeof(uint32_t) );
    if( !ftyp->compatible_brands )
        return -1;
    for( uint32_t i = 0; i < brand_count; i++ )
    {
        ftyp->compatible_brands[i] = brands[i];
        ftyp->base_header.size += 4;
    }
    ftyp->brand_count = brand_count;
    return 0;
}

int isom_set_handler( isom_trak_entry_t *trak, uint32_t handler_type, char *name )
{
    if( !trak || !trak->mdia || !trak->mdia->hdlr )
        return -1;
    isom_hdlr_t *hdlr = trak->mdia->hdlr;
    hdlr->handler_type = handler_type;
    if( name )
    {
        hdlr->name = name;
        hdlr->name_length = strlen( name );
    }
    return 0;
}

int isom_set_movie_timescale( isom_root_t *root, uint32_t timescale )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return -1;
    root->moov->mvhd->timescale = timescale;
    return 0;
}

int isom_set_media_timescale( isom_root_t *root, uint32_t trak_number, uint32_t timescale )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return -1;
    trak->mdia->mdhd->timescale = timescale;
    return 0;
}

int isom_set_track_mode( isom_root_t *root, uint32_t trak_number, uint32_t mode )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->tkhd )
        return -1;
    trak->tkhd->full_header.flags = mode;
    return 0;
}

int isom_set_track_presentation_size( isom_root_t *root, uint32_t trak_number, uint32_t width, uint32_t height )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->tkhd )
        return -1;
    trak->tkhd->width = width;
    trak->tkhd->height = height;
    return 0;
}

int isom_set_track_volume( isom_root_t *root, uint32_t trak_number, int16_t volume )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->tkhd )
        return -1;
    trak->tkhd->volume = volume;
    return 0;
}

int isom_set_sample_resolution( isom_root_t *root, uint32_t trak_number, uint32_t entry_number, uint16_t width, uint16_t height )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_visual_entry_t *data = (isom_visual_entry_t *)isom_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    data->width = width;
    data->height = height;
    return 0;
}

int isom_set_sample_type( isom_root_t *root, uint32_t trak_number, uint32_t entry_number, uint32_t sample_type )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_sample_entry_t *data = (isom_sample_entry_t *)isom_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    data->base_header.type = sample_type;
    return 0;
}

int isom_set_sample_aspect_ratio( isom_root_t *root, uint32_t trak_number, uint32_t entry_number, uint32_t hSpacing, uint32_t vSpacing )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_visual_entry_t *data = (isom_visual_entry_t *)isom_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_pasp_t *pasp = (isom_pasp_t *)data->pasp;
    if( !pasp )
        return -1;
    pasp->hSpacing = hSpacing;
    pasp->vSpacing = vSpacing;
    return 0;
}

int isom_modify_timeline_map( isom_root_t *root, uint32_t trak_number, uint32_t entry_number, uint64_t segment_duration, int64_t media_time, int32_t media_rate )
{
    if( !segment_duration || media_time < -1 )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->edts || !trak->edts->elst || !trak->edts->elst->list )
        return -1;
    isom_elst_entry_t *data = (isom_elst_entry_t *)isom_get_entry_data( trak->edts->elst->list, entry_number );
    if( !data )
        return -1;
    data->segment_duration = segment_duration;
    data->media_time = media_time;
    data->media_rate = media_rate;
    return isom_update_tkhd_duration( root, trak_number ) ? -1 : isom_update_mvhd_duration( root );
}

int isom_set_avc_config( isom_root_t *root, uint32_t trak_number, uint32_t entry_number,
    uint8_t configurationVersion, uint8_t AVCProfileIndication, uint8_t profile_compatibility, uint8_t AVCLevelIndication, uint8_t lengthSizeMinusOne,
    uint8_t chroma_format, uint8_t bit_depth_luma_minus8, uint8_t bit_depth_chroma_minus8 )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_avc_entry_t *data = (isom_avc_entry_t *)isom_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_avcC_t *avcC = (isom_avcC_t *)data->avcC;
    if( !avcC )
        return -1;
    avcC->configurationVersion = configurationVersion;
    avcC->AVCProfileIndication = AVCProfileIndication;
    avcC->profile_compatibility = profile_compatibility;
    avcC->AVCLevelIndication = AVCLevelIndication;
    avcC->lengthSizeMinusOne = lengthSizeMinusOne;
    if( ISOM_REQUIRES_AVCC_EXTENSION( AVCProfileIndication ) )
    {
        avcC->chroma_format = chroma_format;
        avcC->bit_depth_luma_minus8 = bit_depth_luma_minus8;
        avcC->bit_depth_chroma_minus8 = bit_depth_chroma_minus8;
    }
    return 0;
}

int isom_update_bitrate_info( isom_root_t *root, uint32_t trak_number, uint32_t entry_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->minf || !trak->mdia->minf->stbl ||
        !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list || !trak->mdia->minf->stbl->stsz ||
        !trak->mdia->minf->stbl->stts->list || !trak->mdia->minf->stbl->stts->list )
        return -1;
    isom_sample_entry_t *sample_entry = (isom_sample_entry_t *)isom_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !sample_entry )
        return -1;
    struct bitrate_info_t
    {
        uint32_t bufferSizeDB;
        uint32_t maxBitrate;
        uint32_t avgBitrate;
    } info = { 0, 0, 0 };
    uint32_t i = 0;
    uint32_t rate = 0;
    uint32_t time_wnd = 0;
    uint32_t timescale = trak->mdia->mdhd->timescale;
    uint64_t dts = 0;
    isom_entry_t *stts_entry = trak->mdia->minf->stbl->stts->list->head;
    isom_entry_t *stsz_entry = trak->mdia->minf->stbl->stsz->list ? trak->mdia->minf->stbl->stsz->list->head : NULL;
    isom_stts_entry_t *stts_data = NULL;
    while( stts_entry )
    {
        uint32_t size;
        if( trak->mdia->minf->stbl->stsz->list )
        {
            if( !stsz_entry )
                break;
            isom_stsz_entry_t *stsz_data = (isom_stsz_entry_t *)stsz_entry->data;
            if( !stsz_data )
                return -1;
            size = stsz_data->entry_size;
            stsz_entry = stsz_entry->next;
        }
        else
            size = trak->mdia->minf->stbl->stsz->sample_size;
        if( stts_data )
            dts += stts_data->sample_delta;
        stts_data = (isom_stts_entry_t *)stts_entry->data;
        if( ++i == stts_data->sample_count )
        {
            stts_entry = stts_entry->next;
            i = 0;
        }
        if( info.bufferSizeDB < size )
            info.bufferSizeDB = size;
        info.avgBitrate += size;
        rate += size;
        if( dts > time_wnd + timescale )
        {
            if( rate > info.maxBitrate )
                info.maxBitrate = rate;
            time_wnd = dts;
            rate = 0;
        }
    }
    double duration = (double)trak->mdia->mdhd->duration / timescale;
    info.avgBitrate = (uint32_t)(info.avgBitrate / duration);
    if( !info.maxBitrate )
        info.maxBitrate = info.avgBitrate;
    /* move to bps */
    info.maxBitrate *= 8;
    info.avgBitrate *= 8;
    /* set bitrate info */
    switch( sample_entry->base_header.type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
        case ISOM_CODEC_TYPE_AVC2_VIDEO :
        case ISOM_CODEC_TYPE_AVCP_VIDEO :
        {
            isom_avc_entry_t *stsd_data = (isom_avc_entry_t *)sample_entry;
            if( !stsd_data )
                return -1;
            //isom_btrt_t *btrt = (isom_btrt_t *)stsd_data->btrt;
            isom_btrt_t *btrt = stsd_data->btrt;
            if( btrt )
            {
                btrt->bufferSizeDB = info.bufferSizeDB;
                btrt->maxBitrate   = info.maxBitrate;
                btrt->avgBitrate   = info.avgBitrate;
            }
            break;
        }
        case ISOM_CODEC_TYPE_MP4A_AUDIO :
        {
            isom_mp4a_entry_t *stsd_data = (isom_mp4a_entry_t *)sample_entry;
            if( !stsd_data || !stsd_data->esds || !stsd_data->esds->ES )
                return -1;
            /* FIXME: avgBitrate is 0 only if VBR in proper. */
            if( mp4sys_update_DecoderConfigDescriptor( stsd_data->esds->ES, info.bufferSizeDB, info.maxBitrate, 0 ) )
                return -1;
        }
    }
    return 0;
}

int isom_set_handler_name( isom_root_t *root, uint32_t trak_number, char *handler_name )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->hdlr )
        return -1;
    isom_hdlr_t *hdlr = trak->mdia->hdlr;
    char *name = NULL;
    uint32_t length = strlen( handler_name ) + 1;
    if( length > hdlr->name_length && hdlr->name )
        name = realloc( hdlr->name, length );
    else if( !hdlr->name )
        name = malloc( length );
    if( !name )
        return -1;
    hdlr->name = name;
    memcpy( hdlr->name, handler_name, length );
    hdlr->name_length = length;
    return 0;
}

int isom_set_last_sample_delta( isom_root_t *root, uint32_t trak_number, uint32_t sample_delta )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->minf || !trak->mdia->minf->stbl ||
        !trak->mdia->minf->stbl->stsz || !trak->mdia->minf->stbl->stts->list || !trak->mdia->minf->stbl->stts->list )
        return -1;
    /* Ensure that stts is complete. */
    if( isom_output_cache( root, trak_number ) )
        return -1;
    isom_stts_t *stts = trak->mdia->minf->stbl->stts;
    uint32_t sample_count = isom_get_sample_count( trak );
    if( !stts->list->tail )
    {
        if( sample_count != 1 )
            return -1;
        if( isom_add_stts_entry( root, trak_number, sample_delta ) )
            return -1;
        return isom_update_track_duration( root, trak_number );
    }
    uint32_t i = 0;
    for( isom_entry_t *entry = stts->list->head; entry; entry = entry->next )
        i += ((isom_stts_entry_t *)entry->data)->sample_count;
    isom_stts_entry_t *last_stts_data = (isom_stts_entry_t *)stts->list->tail->data;
    if( sample_count > i )
    {
        if( sample_count - i > 1 )
            return -1;
        /* Add a sample_delta. */
        uint32_t prev_delta = last_stts_data->sample_delta;
        if( sample_delta == prev_delta )
            ++ last_stts_data->sample_count;
        else if( isom_add_stts_entry( root, trak_number, sample_delta ) )
            return -1;
    }
    else if( sample_count == i )
    {
        /* Reset the last sample_delta */
        if( last_stts_data->sample_count > 1 )
        {
            last_stts_data->sample_count -= 1;
            if( isom_add_stts_entry( root, trak_number, sample_delta ) )
                return -1;
        }
        else
            last_stts_data->sample_delta = sample_delta;
    }
    else
        return -1;
    return isom_update_track_duration( root, trak_number );
}

int isom_set_language( isom_root_t *root, uint32_t trak_number, char *language )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->mdhd || !language || strlen( language ) != 3 )
        return -1;
    trak->mdia->mdhd->language = ISOM_LANG( language );
    return 0;
}

int isom_set_track_ID( isom_root_t *root, uint32_t trak_number, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->tkhd || !root->moov->mvhd )
        return -1;
    trak->tkhd->track_ID = track_ID;
    /* Update next_track_ID if needed. */
    if( root->moov->mvhd->next_track_ID <= track_ID )
        root->moov->mvhd->next_track_ID = track_ID + 1;
    return 0;
}

int isom_set_free( isom_root_t *root, uint8_t *data, uint64_t data_length )
{
    if( !root || !root->free || !data || !data_length )
        return -1;
    isom_free_t *skip = root->free;
    uint8_t *tmp = NULL;
    if( !skip->data )
        tmp = malloc( data_length );
    else if( skip->length < data_length )
        tmp = realloc( skip->data, data_length );
    if( !tmp )
        return -1;
    memcpy( tmp, data, data_length );
    skip->data = tmp;
    skip->length = data_length;
    return 0;
}

int isom_set_tyrant_chapter( isom_root_t *root, char *file_name )
{
    /* This function should be called after updating of the latest movie duration. */
    if( !root || !root->moov || !root->moov->mvhd || !root->moov->mvhd->timescale || !root->moov->mvhd->duration )
        return -1;
    char buff[512];
    FILE *chapter = fopen( file_name, "rb" );
    if( !chapter )
        return -1;
    if( isom_add_udta( root, 0 ) || isom_add_chpl( root ) )
        goto fail;
    while( fgets( buff, sizeof(buff), chapter ) != NULL )
    {
        /* skip empty line */
        if( buff[0] == '\n' || buff[0] == '\r' )
            continue;
        /* remove newline codes */
        char *tail = &buff[ strlen( buff ) - 1 ];
        if( *tail == '\n' || *tail == '\r' )
        {
            if( tail > buff && *tail == '\n' && *(tail-1) == '\r' )
            {
                *tail = '\0';
                *(tail-1) = '\0';
            }
            else
                *tail = '\0';
        }
        /* get chapter_name */
        char *chapter_name = strchr( buff, ' ' );   /* find separator */
        if( !chapter_name || strlen( buff ) <= ++chapter_name - buff )
            goto fail;
        /* get start_time */
        uint64_t hh, mm, ss, ms;
        if( sscanf( buff, "%"SCNu64":%"SCNu64":%"SCNu64".%"SCNu64, &hh, &mm, &ss, &ms ) != 4 )
            goto fail;
        /* start_time will overflow at 512409557:36:10.956 */
        if( hh > 512409556 || mm > 59 || ss > 59 || ms > 999 )
            goto fail;
        uint64_t start_time = ms * 10000 + (ss + mm * 60 + hh * 3600) * 10000000;
        if( start_time / 1e7 > (double)root->moov->mvhd->duration / root->moov->mvhd->timescale )
            break;
        if( isom_add_chpl_entry( root, start_time, chapter_name ) )
            goto fail;
    }
    fclose( chapter );
    return 0;
fail:
    fclose( chapter );
    return -1;
}

int isom_create_explicit_timeline_map( isom_root_t *root, uint32_t trak_number, uint64_t segment_duration, int64_t media_time, int32_t media_rate )
{
    if( media_time < -1 )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->tkhd )
        return -1;
    segment_duration = segment_duration ? segment_duration :
                       trak->tkhd->duration ? trak->tkhd->duration :
                       isom_update_tkhd_duration( root, trak_number ) ? 0 : trak->tkhd->duration;
    if( isom_add_edts( trak ) )
        return -1;
    if( isom_add_elst( trak->edts ) )
        return -1;
    if( isom_add_elst_entry( trak->edts->elst, segment_duration, media_time, media_rate ) )
        return -1;
    return isom_update_tkhd_duration( root, trak_number ) ? -1 : isom_update_mvhd_duration( root );
}

static int isom_check_mandatory_boxes( isom_root_t *root )
{
    if( !root )
        return 0;
    if( !root->ftyp || !root->moov || !root->moov->mvhd )
        return -1;
    if( root->moov->trak_list )
        for( isom_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
        {
            isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
            if( !trak->tkhd || !trak->mdia )
                return -1;
            if( !trak->mdia->mdhd || !trak->mdia->hdlr || !trak->mdia->minf )
                return -1;
            if( !trak->mdia->minf->dinf || !trak->mdia->minf->dinf->dref )
                return -1;
            if( !trak->mdia->minf->stbl )
                return -1;
            if( !trak->mdia->minf->stbl->stsz )
                return -1;
            if( !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list->head )
                return -1;
            if( !trak->mdia->minf->stbl->stsc || !trak->mdia->minf->stbl->stsc->list->head )
                return -1;
            if( !trak->mdia->minf->stbl->stco || !trak->mdia->minf->stbl->stco->list->head )
                return -1;
        }
    if( !root->moov->trak_list->head )
        return -1;
    return 0;
}

/* For generating creation_time and modification_time.
 * According to ISO/IEC-14496-5-2001, the difference between Unix time and Mac OS time is 2082758400.
 * However this is wrong and 2082844800 is correct. */
#include <time.h>
#define MAC_EPOCH_OFFSET 2082844800

static inline uint64_t isom_get_current_mp4time( void )
{
    return (uint64_t)time( NULL ) + MAC_EPOCH_OFFSET;
}

static int isom_set_media_creation_time( isom_trak_entry_t *trak, uint64_t current_mp4time )
{
    if( !trak->mdia || !trak->mdia->mdhd )
        return -1;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    if( !mdhd->creation_time )
        mdhd->creation_time = mdhd->modification_time = current_mp4time;
    return 0;
}

static int isom_set_track_creation_time( isom_trak_entry_t *trak, uint64_t current_mp4time )
{
    if( !trak || !trak->tkhd )
        return -1;
    isom_tkhd_t *tkhd = trak->tkhd;
    if( !tkhd->creation_time )
        tkhd->creation_time = tkhd->modification_time = current_mp4time;
    if( isom_set_media_creation_time( trak, current_mp4time ) )
        return -1;
    return 0;
}

static int isom_set_movie_creation_time( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd || !root->moov->trak_list )
        return -1;
    uint64_t current_mp4time = isom_get_current_mp4time();
    for( uint32_t i = 1; i <= root->moov->trak_list->entry_count; i++ )
        if( isom_set_track_creation_time( isom_get_trak( root, i ), current_mp4time ) )
            return -1;
    isom_mvhd_t *mvhd = root->moov->mvhd;
    if( !mvhd->creation_time )
        mvhd->creation_time = mvhd->modification_time = current_mp4time;
    return 0;
}

int isom_update_media_modification_time( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return -1;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    mdhd->modification_time = isom_get_current_mp4time();
    if( mdhd->creation_time < mdhd->modification_time )
        mdhd->creation_time = mdhd->modification_time;
    return 0;
}

int isom_update_track_modification_time( isom_root_t *root, uint32_t trak_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, trak_number );
    if( !trak || !trak->tkhd )
        return -1;
    isom_tkhd_t *tkhd = trak->tkhd;
    tkhd->modification_time = isom_get_current_mp4time();
    if( tkhd->creation_time < tkhd->modification_time )
        tkhd->creation_time = tkhd->modification_time;
    return 0;
}

int isom_update_movie_modification_time( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return -1;
    isom_mvhd_t *mvhd = root->moov->mvhd;
    mvhd->modification_time = isom_get_current_mp4time();
    if( mvhd->creation_time < mvhd->modification_time )
        mvhd->creation_time = mvhd->modification_time;
    return 0;
}

#define CHECK_LARGESIZE( size ) if( (size) > UINT32_MAX ) (size) += 8

static uint64_t isom_update_mvhd_size( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return 0;
    isom_mvhd_t *mvhd = root->moov->mvhd;
    mvhd->full_header.version = 0;
    if( mvhd->creation_time > UINT32_MAX || mvhd->modification_time > UINT32_MAX || mvhd->duration > UINT32_MAX )
        mvhd->full_header.version = 1;
    uint64_t size = 96 + (uint64_t)mvhd->full_header.version * 12;
    mvhd->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( mvhd->full_header.size );
    return mvhd->full_header.size;
}

static uint64_t isom_update_iods_size( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->iods || !root->moov->iods->OD )
        return 0;
    isom_iods_t *iods = root->moov->iods;
    uint64_t size = mp4sys_update_ObjectDescriptor_size( root->moov->iods->OD );
    iods->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( iods->full_header.size );
    return iods->full_header.size;
}

static uint64_t isom_update_tkhd_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->tkhd )
        return 0;
    isom_tkhd_t *tkhd = trak->tkhd;
    tkhd->full_header.version = 0;
    if( tkhd->creation_time > UINT32_MAX || tkhd->modification_time > UINT32_MAX || tkhd->duration > UINT32_MAX )
        tkhd->full_header.version = 1;
    uint64_t size = 80 + (uint64_t)tkhd->full_header.version * 12;
    trak->tkhd->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->tkhd->full_header.size );
    return trak->tkhd->full_header.size;
}

static uint64_t isom_update_elst_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->edts || !trak->edts->elst || !trak->edts->elst->list )
        return 0;
    uint32_t i = 0;
    trak->edts->elst->full_header.version = 0;
    for( isom_entry_t *entry = trak->edts->elst->list->head; entry; entry = entry->next, i++ )
    {
        isom_elst_entry_t *data = (isom_elst_entry_t *)entry->data;
        if( data->segment_duration > UINT32_MAX || data->media_time > UINT32_MAX )
            trak->edts->elst->full_header.version = 1;
    }
    uint64_t size = (uint64_t)i * ( trak->edts->elst->full_header.version ? 20 : 12 );
    trak->edts->elst->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->edts->elst->full_header.size );
    return trak->edts->elst->full_header.size;
}

static uint64_t isom_update_edts_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->edts )
        return 0;
    trak->edts->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + isom_update_elst_size( trak );
    CHECK_LARGESIZE( trak->edts->base_header.size );
    return trak->edts->base_header.size;
}

static uint64_t isom_update_mdhd_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return 0;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    mdhd->full_header.version = 0;
    if( mdhd->creation_time > UINT32_MAX || mdhd->modification_time > UINT32_MAX || mdhd->duration > UINT32_MAX )
        mdhd->full_header.version = 1;
    uint64_t size = 20 + (uint64_t)mdhd->full_header.version * 12;
    trak->mdia->mdhd->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->mdia->mdhd->full_header.size );
    return trak->mdia->mdhd->full_header.size;
}

static uint64_t isom_update_hdlr_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->hdlr )
        return 0;
    isom_hdlr_t *hdlr = trak->mdia->hdlr;
    uint64_t size = 20 + (uint64_t)hdlr->name_length;
    trak->mdia->hdlr->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->mdia->hdlr->full_header.size );
    return trak->mdia->hdlr->full_header.size;
}

static uint64_t isom_update_dref_entry_size( isom_dref_entry_t *urln )
{
    if( !urln )
        return 0;
    uint64_t size = (uint64_t)urln->name_length + urln->location_length;
    urln->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( urln->full_header.size );
    return urln->full_header.size;
}

static uint64_t isom_update_dref_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->dinf || !trak->mdia->minf->dinf->dref || !trak->mdia->minf->dinf->dref->list )
        return 0;
    uint64_t size = 0;
    if( trak->mdia->minf->dinf->dref->list )
        for( isom_entry_t *entry = trak->mdia->minf->dinf->dref->list->head; entry; entry = entry->next )
        {
            isom_dref_entry_t *data = (isom_dref_entry_t *)entry->data;
            size += isom_update_dref_entry_size( data );
        }
    trak->mdia->minf->dinf->dref->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->mdia->minf->dinf->dref->full_header.size );
    return trak->mdia->minf->dinf->dref->full_header.size;
}

static uint64_t isom_update_dinf_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->dinf )
        return 0;
    trak->mdia->minf->dinf->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + isom_update_dref_size( trak );
    CHECK_LARGESIZE( trak->mdia->minf->dinf->base_header.size );
    return trak->mdia->minf->dinf->base_header.size;
}

static uint64_t isom_update_vmhd_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->vmhd )
        return 0;
    trak->mdia->minf->vmhd->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 8;
    CHECK_LARGESIZE( trak->mdia->minf->vmhd->full_header.size );
    return trak->mdia->minf->vmhd->full_header.size;
}

static uint64_t isom_update_smhd_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->smhd )
        return 0;
    trak->mdia->minf->smhd->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 4;
    CHECK_LARGESIZE( trak->mdia->minf->smhd->full_header.size );
    return trak->mdia->minf->smhd->full_header.size;
}

static uint64_t isom_update_hmhd_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->hmhd )
        return 0;
    trak->mdia->minf->hmhd->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 16;
    CHECK_LARGESIZE( trak->mdia->minf->hmhd->full_header.size );
    return trak->mdia->minf->hmhd->full_header.size;
}

static uint64_t isom_update_nmhd_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->nmhd )
        return 0;
    trak->mdia->minf->nmhd->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE;
    CHECK_LARGESIZE( trak->mdia->minf->nmhd->full_header.size );
    return trak->mdia->minf->nmhd->full_header.size;
}

static uint64_t isom_update_btrt_size( isom_btrt_t *btrt )
{
    if( !btrt )
        return 0;
    btrt->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 12;
    CHECK_LARGESIZE( btrt->base_header.size );
    return btrt->base_header.size;
}

static uint64_t isom_update_pasp_size( isom_pasp_t *pasp )
{
    if( !pasp )
        return 0;
    pasp->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 8;
    CHECK_LARGESIZE( pasp->base_header.size );
    return pasp->base_header.size;
}

static uint64_t isom_update_clap_size( isom_clap_t *clap )
{
    if( !clap )
        return 0;
    clap->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 32;
    CHECK_LARGESIZE( clap->base_header.size );
    return clap->base_header.size;
}

static uint64_t isom_update_avcC_size( isom_avcC_t *avcC )
{
    if( !avcC || !avcC->sequenceParameterSets || !avcC->pictureParameterSets )
        return 0;
    uint64_t size = 7;
    for( isom_entry_t *entry = avcC->sequenceParameterSets->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
        size += 2U + data->parameterSetLength;
    }
    for( isom_entry_t *entry = avcC->pictureParameterSets->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
        size += 2U + data->parameterSetLength;
    }
    if( ISOM_REQUIRES_AVCC_EXTENSION( avcC->AVCProfileIndication ) )
    {
        size += 4U;
        for( isom_entry_t *entry = avcC->sequenceParameterSetExt->head; entry; entry = entry->next )
        {
            isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
            size += 2U + data->parameterSetLength;
        }
    }
    avcC->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( avcC->base_header.size );
    return avcC->base_header.size;
}

static uint64_t isom_update_avc_entry_size( isom_avc_entry_t *avc )
{
    if( !avc ||
        ((avc->base_header.type != ISOM_CODEC_TYPE_AVC1_VIDEO) &&
        (avc->base_header.type != ISOM_CODEC_TYPE_AVC2_VIDEO) &&
        (avc->base_header.type != ISOM_CODEC_TYPE_AVCP_VIDEO)) )
        return 0;
    uint64_t size = 78;
    size += isom_update_pasp_size( avc->pasp );
    size += isom_update_clap_size( avc->clap );
    size += isom_update_avcC_size( avc->avcC );
    size += isom_update_btrt_size( avc->btrt );
    avc->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( avc->base_header.size );
    return avc->base_header.size;
}

static uint64_t isom_update_esds_size( isom_esds_t *esds )
{
    if( !esds )
        return 0;
    esds->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE;
    esds->full_header.size += mp4sys_update_ES_Descriptor_size( esds->ES );
    CHECK_LARGESIZE( esds->full_header.size );
    return esds->full_header.size;
}

static uint64_t isom_update_mp4v_entry_size( isom_mp4v_entry_t *mp4v )
{
    if( !mp4v || mp4v->base_header.type != ISOM_CODEC_TYPE_MP4V_VIDEO )
        return 0;
    uint64_t size = 78 + isom_update_esds_size( mp4v->esds );
    size += isom_update_pasp_size( mp4v->pasp );
    size += isom_update_clap_size( mp4v->clap );
    mp4v->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( mp4v->base_header.size );
    return mp4v->base_header.size;
}

static uint64_t isom_update_mp4a_entry_size( isom_mp4a_entry_t *mp4a )
{
    if( !mp4a || mp4a->base_header.type != ISOM_CODEC_TYPE_MP4A_AUDIO )
        return 0;
    uint64_t size = 28 + isom_update_esds_size( mp4a->esds );
    mp4a->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( mp4a->base_header.size );
    return mp4a->base_header.size;
}

static uint64_t isom_update_mp4s_entry_size( isom_mp4s_entry_t *mp4s )
{
    if( !mp4s || mp4s->base_header.type != ISOM_CODEC_TYPE_MP4S_SYSTEM )
        return 0;
    uint64_t size = 8 + isom_update_esds_size( mp4s->esds );
    mp4s->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( mp4s->base_header.size );
    return mp4s->base_header.size;
}

static uint64_t isom_update_stsd_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return 0;
    uint64_t size = 0;
    for( isom_entry_t *entry = trak->mdia->minf->stbl->stsd->list->head; entry; entry = entry->next )
    {
        isom_sample_entry_t *data = (isom_sample_entry_t *)entry->data;
        switch( data->base_header.type )
        {
            case ISOM_CODEC_TYPE_AVC1_VIDEO :
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
                size += isom_update_avc_entry_size( (isom_avc_entry_t *)data );
                break;
            case ISOM_CODEC_TYPE_MP4V_VIDEO :
                size += isom_update_mp4v_entry_size( (isom_mp4v_entry_t *)data );
                break;
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
                size += isom_update_mp4a_entry_size( (isom_mp4a_entry_t *)data );
                break;
            case ISOM_CODEC_TYPE_MP4S_SYSTEM :
                size += isom_update_mp4s_entry_size( (isom_mp4s_entry_t *)data );
                break;
            default :
                break;
        }
    }
    trak->mdia->minf->stbl->stsd->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->mdia->minf->stbl->stsd->full_header.size );
    return trak->mdia->minf->stbl->stsd->full_header.size;
}

static uint64_t isom_update_stts_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list )
        return 0;
    uint64_t size = 0;
    for( isom_entry_t *entry = trak->mdia->minf->stbl->stts->list->head; entry; entry = entry->next )
        size += 8;
    trak->mdia->minf->stbl->stts->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->mdia->minf->stbl->stts->full_header.size );
    return trak->mdia->minf->stbl->stts->full_header.size;
}

static uint64_t isom_update_ctts_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->ctts || !trak->mdia->minf->stbl->ctts->list )
        return 0;
    uint64_t size = 0;
    for( isom_entry_t *entry = trak->mdia->minf->stbl->ctts->list->head; entry; entry = entry->next )
        size += 8;
    trak->mdia->minf->stbl->ctts->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->mdia->minf->stbl->ctts->full_header.size );
    return trak->mdia->minf->stbl->ctts->full_header.size;
}

static uint64_t isom_update_stsz_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsz )
        return 0;
    uint64_t size = 0;
    if( trak->mdia->minf->stbl->stsz->list )
        for( isom_entry_t *entry = trak->mdia->minf->stbl->stsz->list->head; entry; entry = entry->next )
            size += 4;
    trak->mdia->minf->stbl->stsz->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 8 + size;
    CHECK_LARGESIZE( trak->mdia->minf->stbl->stsz->full_header.size );
    return trak->mdia->minf->stbl->stsz->full_header.size;
}

static uint64_t isom_update_stss_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stss || !trak->mdia->minf->stbl->stss->list )
        return 0;
    uint64_t size = 0;
    for( isom_entry_t *entry = trak->mdia->minf->stbl->stss->list->head; entry; entry = entry->next )
        size += 4;
    trak->mdia->minf->stbl->stss->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->mdia->minf->stbl->stss->full_header.size );
    return trak->mdia->minf->stbl->stss->full_header.size;
}

static uint64_t isom_update_sdtp_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->sdtp || !trak->mdia->minf->stbl->sdtp->list )
        return 0;
    trak->mdia->minf->stbl->sdtp->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + trak->mdia->minf->stbl->sdtp->list->entry_count;
    CHECK_LARGESIZE( trak->mdia->minf->stbl->sdtp->full_header.size );
    return trak->mdia->minf->stbl->sdtp->full_header.size;
}

static uint64_t isom_update_stsc_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsc || !trak->mdia->minf->stbl->stsc->list )
        return 0;
    uint64_t size = 0;
    for( isom_entry_t *entry = trak->mdia->minf->stbl->stsc->list->head; entry; entry = entry->next )
        size += 12;
    trak->mdia->minf->stbl->stsc->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->mdia->minf->stbl->stsc->full_header.size );
    return trak->mdia->minf->stbl->stsc->full_header.size;
}

static uint64_t isom_update_stco_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stco || !trak->mdia->minf->stbl->stco->list )
        return 0;
    uint32_t i = 0;
    for( isom_entry_t *entry = trak->mdia->minf->stbl->stco->list->head; entry; entry = entry->next )
        ++i;
    uint64_t size = (uint64_t)i * (trak->mdia->minf->stbl->stco->large_presentation ? 8 : 4);
    trak->mdia->minf->stbl->stco->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->mdia->minf->stbl->stco->full_header.size );
    return trak->mdia->minf->stbl->stco->full_header.size;
}

static uint64_t isom_update_sbgp_size( isom_sbgp_t *sbgp )
{
    if( !sbgp || !sbgp->list )
        return 0;
    uint32_t size = 0;
    for( isom_entry_t *entry = sbgp->list->head; entry; entry = entry->next )
        size += 8;
    sbgp->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + 4 + size;
    CHECK_LARGESIZE( sbgp->full_header.size );
    return sbgp->full_header.size;
}

static uint64_t isom_update_sgpd_size( isom_sgpd_t *sgpd )
{
    if( !sgpd || !sgpd->list )
        return 0;
    uint32_t size = 0;
    for( isom_entry_t *entry = sgpd->list->head; entry; entry = entry->next )
    {
        size += ((sgpd->full_header.version == 1) && !sgpd->default_length) * 4;
        switch( sgpd->grouping_type )
        {
            case ISOM_GROUP_TYPE_ROLL :
                size += 2;
                break;
            default :
                break;
        }
    }
    sgpd->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (1 + (sgpd->full_header.version == 1)) * 4 + size;
    CHECK_LARGESIZE( sgpd->full_header.size );
    return sgpd->full_header.size;
}

static uint64_t isom_update_stbl_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return 0;
    uint64_t size = isom_update_stsd_size( trak );
    size += isom_update_stts_size( trak );
    size += isom_update_ctts_size( trak );
    size += isom_update_stsz_size( trak );
    size += isom_update_stss_size( trak );
    size += isom_update_sdtp_size( trak );
    size += isom_update_stsc_size( trak );
    size += isom_update_stco_size( trak );
    for( uint32_t i = 0; i < trak->mdia->minf->stbl->grouping_count; i++ )
    {
        size += isom_update_sbgp_size( trak->mdia->minf->stbl->sbgp + i );
        size += isom_update_sgpd_size( trak->mdia->minf->stbl->sgpd + i );
    }
    trak->mdia->minf->stbl->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->mdia->minf->stbl->base_header.size );
    return trak->mdia->minf->stbl->base_header.size;
}

static uint64_t isom_update_minf_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf )
        return 0;
    uint64_t size = isom_update_vmhd_size( trak );
    size += isom_update_smhd_size( trak );
    size += isom_update_hmhd_size( trak );
    size += isom_update_nmhd_size( trak );
    size += isom_update_dinf_size( trak );
    size += isom_update_stbl_size( trak );
    trak->mdia->minf->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->mdia->minf->base_header.size );
    return trak->mdia->minf->base_header.size;
}

static uint64_t isom_update_mdia_size( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia )
        return 0;
    uint64_t size = isom_update_mdhd_size( trak );
    size += isom_update_hdlr_size( trak );
    size += isom_update_minf_size( trak );
    trak->mdia->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->mdia->base_header.size );
    return trak->mdia->base_header.size;
}

static uint64_t isom_update_chpl_size( isom_chpl_t *chpl )
{
    if( !chpl )
        return 0;
    uint64_t size = 1;
    for( isom_entry_t *entry = chpl->list->head; entry; entry = entry->next )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        size += 9U + data->name_length;
    }
    chpl->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( chpl->full_header.size );
    return chpl->full_header.size;
}

static uint64_t isom_update_udta_size( isom_moov_t *moov, isom_trak_entry_t *trak )
{
    isom_udta_t *udta = trak ? trak->udta : moov ? moov->udta : NULL;
    if( !udta )
        return 0;
    uint64_t size = 0;
    if( moov )
        size += isom_update_chpl_size( udta->chpl );
    udta->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( udta->base_header.size );
    return udta->base_header.size;
}

static uint64_t isom_update_trak_entry_size( isom_trak_entry_t *trak )
{
    if( !trak )
        return 0;
    uint64_t size = isom_update_tkhd_size( trak );
    size += isom_update_edts_size( trak );
    size += isom_update_mdia_size( trak );
    size += isom_update_udta_size( NULL, trak );
    trak->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( trak->base_header.size );
    return trak->base_header.size;
}

static int isom_update_moov_size( isom_root_t *root )
{
    if( !root || !root->moov )
        return -1;
    uint64_t size = isom_update_mvhd_size( root );
    size += isom_update_iods_size( root );
    size += isom_update_udta_size( root->moov, NULL );
    if( root->moov->trak_list )
        for( isom_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
        {
            isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
            size += isom_update_trak_entry_size( trak );
        }
    root->moov->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + size;
    CHECK_LARGESIZE( root->moov->base_header.size );
    return 0;
}

int isom_finish_movie( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->trak_list )
        return -1;
    for( isom_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
    {
        uint32_t trak_number = isom_get_trak_number( (isom_trak_entry_t *)entry->data );
        if( isom_set_track_mode( root, trak_number, ISOM_TRACK_ENABLED ) )
            return -1;
    }
    if( isom_add_iods( root ) )
        return -1;
    if( isom_check_mandatory_boxes( root ) ||
        isom_set_movie_creation_time( root ) ||
        isom_update_moov_size( root ) ||
        isom_write_moov( root ) )
        return -1;
    return 0;
}
