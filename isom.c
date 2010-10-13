/*****************************************************************************
 * isom.c:
 *****************************************************************************
 * Copyright (C) 2010 L-SMASH project
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

#include "isom.h"
#include "mp4sys.h"

/* MP4 Audio Sample Entry */
typedef struct
{
    ISOM_AUDIO_SAMPLE_ENTRY;
    isom_esds_t *esds;
    mp4sys_audioProfileLevelIndication pli; /* This is not used in mp4a box itself, but the value is specific for that. */
} isom_mp4a_entry_t;


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

static isom_trak_entry_t *isom_get_trak( isom_root_t *root, uint32_t track_ID )
{
    if( !root || !root->moov || !root->moov->trak_list )
        return NULL;
    for( isom_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
        if( !trak || !trak->tkhd )
            return NULL;
        if( trak->tkhd->track_ID == track_ID )
            return trak;
    }
    return NULL;
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

static int isom_add_dref_entry( isom_dref_t *dref, uint32_t flags, char *name, char *location )
{
    if( !dref || !dref->list )
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
    if( isom_add_entry( dref->list, data ) )
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

static void isom_remove_avcC_ps( isom_avcC_ps_entry_t *ps )
{
    if( !ps )
        return;
    if( ps->parameterSetNALUnit )
        free( ps->parameterSetNALUnit );
    free( ps );
}

int isom_add_sps_entry( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint8_t *sps, uint32_t sps_size )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
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

int isom_add_pps_entry( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint8_t *pps, uint32_t pps_size )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
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

int isom_add_spsext_entry( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint8_t *spsext, uint32_t spsext_size )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
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

static void isom_remove_avcC( isom_avcC_t *avcC )
{
    if( !avcC )
        return;
    isom_remove_list( avcC->sequenceParameterSets,   isom_remove_avcC_ps );
    isom_remove_list( avcC->pictureParameterSets,    isom_remove_avcC_ps );
    isom_remove_list( avcC->sequenceParameterSetExt, isom_remove_avcC_ps );
    free( avcC );
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
    case MP4SYS_OBJECT_TYPE_Audio_ISO_13818_3: /* Legacy Interface */
    case MP4SYS_OBJECT_TYPE_Audio_ISO_11172_3: /* Legacy Interface */
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
    mp4a->samplerate = summary->frequency <= UINT16_MAX ? summary->frequency << 16 : 0;
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

#if 0
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
#endif

static int isom_add_audio_entry( isom_entry_list_t *list, uint32_t sample_type, mp4sys_audio_summary_t *summary )
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
    audio->samplerate = summary->frequency <= UINT16_MAX ? summary->frequency << 16 : 0;
    if( summary->exdata )
    {
        audio->exdata_length = summary->exdata_length;
        audio->exdata = malloc( audio->exdata_length );
        if( !audio->exdata )
        {
            free( audio );
            return -1;
        }
        memcpy( audio->exdata, summary->exdata, audio->exdata_length );
    }
    else
        audio->exdata = NULL;
    if( isom_add_entry( list, audio ) )
    {
        if( audio->exdata )
            free( audio->exdata );
        free( audio );
        return -1;
    }
    return 0;
}

/* This function returns 0 if failed, sample_entry_number if succeeded. */
int isom_add_sample_entry( isom_root_t *root, uint32_t track_ID, uint32_t sample_type, void* summary )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return 0;
    isom_entry_list_t *list = trak->mdia->minf->stbl->stsd->list;
    int ret = -1;
    switch( sample_type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
#if 0
        case ISOM_CODEC_TYPE_AVC2_VIDEO :
        case ISOM_CODEC_TYPE_AVCP_VIDEO :
        case ISOM_CODEC_TYPE_SVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC2_VIDEO :
#endif
            ret = isom_add_avc_entry( list, sample_type );
            break;
#if 0
        case ISOM_CODEC_TYPE_MP4V_VIDEO :
            ret = isom_add_mp4v_entry( list );
            break;
#endif
        case ISOM_CODEC_TYPE_MP4A_AUDIO :
            ret = isom_add_mp4a_entry( list, (mp4sys_audio_summary_t*)summary );
            break;
#if 0
        case ISOM_CODEC_TYPE_MP4S_SYSTEM :
            ret = isom_add_mp4s_entry( list );
            break;
        case ISOM_CODEC_TYPE_DRAC_VIDEO :
        case ISOM_CODEC_TYPE_ENCV_VIDEO :
        case ISOM_CODEC_TYPE_MJP2_VIDEO :
        case ISOM_CODEC_TYPE_S263_VIDEO :
        case ISOM_CODEC_TYPE_VC_1_VIDEO :
            ret = isom_add_visual_entry( list, sample_type );
            break;
#endif
        case ISOM_CODEC_TYPE_AC_3_AUDIO :
        case ISOM_CODEC_TYPE_ALAC_AUDIO :
        case ISOM_CODEC_TYPE_SAMR_AUDIO :
        case ISOM_CODEC_TYPE_SAWB_AUDIO :
#if 0
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
        case ISOM_CODEC_TYPE_SAWP_AUDIO :
        case ISOM_CODEC_TYPE_SEVC_AUDIO :
        case ISOM_CODEC_TYPE_SQCP_AUDIO :
        case ISOM_CODEC_TYPE_SSMV_AUDIO :
        case ISOM_CODEC_TYPE_TWOS_AUDIO :
#endif
            ret = isom_add_audio_entry( list, sample_type, (mp4sys_audio_summary_t *)summary );
            break;
        default :
            return 0;
    }
    return ret ? 0 : list->entry_count;
}

static int isom_add_stts_entry( isom_stbl_t *stbl, uint32_t sample_delta )
{
    if( !stbl || !stbl->stts || !stbl->stts->list )
        return -1;
    isom_stts_entry_t *data = malloc( sizeof(isom_stts_entry_t) );
    if( !data )
        return -1;
    data->sample_count = 1;
    data->sample_delta = sample_delta;
    if( isom_add_entry( stbl->stts->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_add_ctts_entry( isom_stbl_t *stbl, uint32_t sample_offset )
{
    if( !stbl || !stbl->ctts || !stbl->ctts->list )
        return -1;
    isom_ctts_entry_t *data = malloc( sizeof(isom_ctts_entry_t) );
    if( !data )
        return -1;
    data->sample_count = 1;
    data->sample_offset = sample_offset;
    if( isom_add_entry( stbl->ctts->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_add_stsc_entry( isom_stbl_t *stbl, uint32_t first_chunk, uint32_t samples_per_chunk, uint32_t sample_description_index )
{
    if( !stbl || !stbl->stsc || !stbl->stsc->list )
        return -1;
    isom_stsc_entry_t *data = malloc( sizeof(isom_stsc_entry_t) );
    if( !data )
        return -1;
    data->first_chunk = first_chunk;
    data->samples_per_chunk = samples_per_chunk;
    data->sample_description_index = sample_description_index;
    if( isom_add_entry( stbl->stsc->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_add_stsz_entry( isom_stbl_t *stbl, uint32_t entry_size )
{
    if( !stbl || !stbl->stsz )
        return -1;
    isom_stsz_t *stsz = stbl->stsz;
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

static int isom_add_stss_entry( isom_stbl_t *stbl, uint32_t sample_number )
{
    if( !stbl || !stbl->stss || !stbl->stss->list )
        return -1;
    isom_stss_entry_t *data = malloc( sizeof(isom_stss_entry_t) );
    if( !data )
        return -1;
    data->sample_number = sample_number;
    if( isom_add_entry( stbl->stss->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

int isom_add_stps_entry( isom_stbl_t *stbl, uint32_t sample_number )
{
    if( !stbl || !stbl->stps || !stbl->stps->list )
        return -1;
    isom_stps_entry_t *data = malloc( sizeof(isom_stps_entry_t) );
    if( !data )
        return -1;
    data->sample_number = sample_number;
    if( isom_add_entry( stbl->stps->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_add_sdtp_entry( isom_stbl_t *stbl, isom_sample_property_t *prop )
{
    if( !prop )
        return -1;
    if( !stbl || !stbl->sdtp || !stbl->sdtp->list )
        return -1;
    isom_sdtp_entry_t *data = malloc( sizeof(isom_sdtp_entry_t) );
    if( !data )
        return -1;
    /* isom_sdtp_entry_t is smaller than isom_sample_property_t. */
    data->is_leading = prop->leading & 0x03;
    data->sample_depends_on = prop->independent & 0x03;
    data->sample_is_depended_on = prop->disposable & 0x03;
    data->sample_has_redundancy = prop->redundant & 0x03;
    if( isom_add_entry( stbl->sdtp->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_add_co64( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stco )
        return -1;
    isom_create_list_fullbox( stco, ISOM_BOX_TYPE_CO64 );
    stco->large_presentation = 1;
    stbl->stco = stco;
    return 0;
}

static int isom_add_stco( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stco )
        return -1;
    isom_create_list_fullbox( stco, ISOM_BOX_TYPE_STCO );
    stco->large_presentation = 0;
    stbl->stco = stco;
    return 0;
}

static int isom_add_co64_entry( isom_stbl_t *stbl, uint64_t chunk_offset )
{
    if( !stbl || !stbl->stco || !stbl->stco->list )
        return -1;
    isom_co64_entry_t *data = malloc( sizeof(isom_co64_entry_t) );
    if( !data )
        return -1;
    data->chunk_offset = chunk_offset;
    if( isom_add_entry( stbl->stco->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_add_stco_entry( isom_stbl_t *stbl, uint64_t chunk_offset )
{
    if( !stbl || !stbl->stco || !stbl->stco->list )
        return -1;
    if( stbl->stco->large_presentation )
    {
        if( isom_add_co64_entry( stbl, chunk_offset ) )
            return -1;
        return 0;
    }
    if( chunk_offset > UINT32_MAX )
    {
        /* backup stco */
        isom_stco_t *stco = stbl->stco;
        stbl->stco = NULL;
        int e = 0;
        if( isom_add_co64( stbl ) )
            e = 1;
        /* move chunk_offset to co64 from stco */
        for( isom_entry_t *entry = stco->list->head; !e && entry; )
        {
            isom_stco_entry_t *data = (isom_stco_entry_t *)entry->data;
            if( isom_add_co64_entry( stbl, data->chunk_offset ) )
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
            isom_remove_list( stbl->stco->list, NULL );
            stbl->stco = stco;
            return -1;
        }
        else
        {
            free( stco->list );
            free( stco );
        }
        if( isom_add_co64_entry( stbl, chunk_offset ) )
            return -1;
        return 0;
    }
    isom_stco_entry_t *data = malloc( sizeof(isom_stco_entry_t) );
    if( !data )
        return -1;
    data->chunk_offset = (uint32_t)chunk_offset;
    if( isom_add_entry( stbl->stco->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

#if 0
static int isom_add_sbgp_entry( isom_stbl_t *stbl, uint32_t grouping_number, uint32_t sample_count, uint32_t group_description_index )
{
    if( !stbl || !stbl->grouping_count || !grouping_number || stbl->grouping_count < grouping_number || !sample_count )
        return -1;
    isom_sbgp_t *sbgp = stbl->sbgp + grouping_number - 1;
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

static int isom_add_roll_group_entry( isom_stbl_t *stbl, uint32_t grouping_number, uint32_t description_length, int16_t roll_distance )
{
    if( !stbl || !stbl->grouping_count || !grouping_number || stbl->grouping_count < grouping_number )
        return -1;
    isom_sgpd_t *sgpd = stbl->sgpd + grouping_number - 1;
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
#endif

static int isom_add_chpl_entry( isom_chpl_t *chpl, uint64_t start_time, char *chapter_name )
{
    if( !chapter_name || !chpl || !chpl->list )
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
    if( isom_add_entry( chpl->list, data ) )
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

static int isom_add_mvhd( isom_moov_t *moov )
{
    if( !moov || moov->mvhd )
        return -1;
    isom_create_fullbox( mvhd, ISOM_BOX_TYPE_MVHD );
    mvhd->rate = 0x00010000;
    mvhd->volume = 0x0100;
    mvhd->matrix[0] = 0x00010000;
    mvhd->matrix[4] = 0x00010000;
    mvhd->matrix[8] = 0x40000000;
    mvhd->next_track_ID = 1;
    moov->mvhd = mvhd;
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
#if 0
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
            case ISOM_CODEC_TYPE_SVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC2_VIDEO :
#endif
                /* FIXME: Do we have to arbitrate like audio? */
                if( *visual_pli == MP4SYS_VISUAL_PLI_NONE_REQUIRED )
                    *visual_pli = MP4SYS_VISUAL_PLI_H264_AVC;
                break;
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
                *audio_pli = mp4sys_max_audioProfileLevelIndication( *audio_pli, ((isom_mp4a_entry_t*)sample_entry)->pli );
                break;
#if 0
            case ISOM_CODEC_TYPE_DRAC_VIDEO :
            case ISOM_CODEC_TYPE_ENCV_VIDEO :
            case ISOM_CODEC_TYPE_MJP2_VIDEO :
            case ISOM_CODEC_TYPE_S263_VIDEO :
            case ISOM_CODEC_TYPE_VC_1_VIDEO :
                /* FIXME: Do we have to arbitrate like audio? */
                if( *visual_pli == MP4SYS_VISUAL_PLI_NONE_REQUIRED )
                    *visual_pli = MP4SYS_VISUAL_PLI_NOT_SPECIFIED;
                break;
#endif
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
#if 0
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
            case ISOM_CODEC_TYPE_SAWP_AUDIO :
            case ISOM_CODEC_TYPE_SEVC_AUDIO :
            case ISOM_CODEC_TYPE_SQCP_AUDIO :
            case ISOM_CODEC_TYPE_SSMV_AUDIO :
            case ISOM_CODEC_TYPE_TWOS_AUDIO :
#endif
                /* NOTE: These audio codecs other than mp4a does not have appropriate pli. */
                *audio_pli = MP4SYS_AUDIO_PLI_NOT_SPECIFIED;
                break;
#if 0
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
#endif
        }
    }
    return 0;
}

static int isom_add_iods( isom_moov_t *moov )
{
    if( !moov || !moov->trak_list || moov->iods )
        return -1;
    isom_create_fullbox( iods, ISOM_BOX_TYPE_IODS );
    iods->OD = mp4sys_create_ObjectDescriptor( 1 ); /* NOTE: Use 1 for ObjectDescriptorID of IOD. */
    if( !iods->OD )
    {
        free( iods );
        return -1;
    }
    mp4sys_audioProfileLevelIndication audio_pli = MP4SYS_AUDIO_PLI_NONE_REQUIRED;
    mp4sys_visualProfileLevelIndication visual_pli = MP4SYS_VISUAL_PLI_NONE_REQUIRED;
    for( isom_entry_t *entry = moov->trak_list->head; entry; entry = entry->next )
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
    moov->iods = iods;
    return 0;
}

static int isom_add_tkhd( isom_trak_entry_t *trak, uint32_t handler_type )
{
    if( !trak || !trak->root || !trak->root->moov || !trak->root->moov->mvhd || !trak->root->moov->trak_list )
        return -1;
    if( !trak->tkhd )
    {
        isom_create_fullbox( tkhd, ISOM_BOX_TYPE_TKHD );
        switch( handler_type )
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
        tkhd->duration = 0xffffffff;
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

static int isom_add_mdhd( isom_mdia_t *mdia )
{
    if( !mdia || mdia->mdhd )
        return -1;
    isom_create_fullbox( mdhd, ISOM_BOX_TYPE_MDHD );
    mdhd->language = ISOM_LANG( "und" );
    mdia->mdhd = mdhd;
    return 0;
}

static int isom_add_mdia( isom_trak_entry_t *trak )
{
    if( !trak || trak->mdia )
        return -1;
    isom_create_basebox( mdia, ISOM_BOX_TYPE_MDIA );
    trak->mdia = mdia;
    return 0;
}

static int isom_add_hdlr( isom_mdia_t *mdia, uint32_t handler_type )
{
    if( !mdia || mdia->hdlr )
        return -1;
    isom_create_fullbox( hdlr, ISOM_BOX_TYPE_HDLR );
    hdlr->handler_type = handler_type;
    hdlr->name = malloc( 1 );
    if( !hdlr->name )
        return -1;
    hdlr->name[0] = '\0';
    hdlr->name_length = 1;
    mdia->hdlr = hdlr;
    return 0;
}

static int isom_add_minf( isom_mdia_t *mdia )
{
    if( !mdia || mdia->minf )
        return -1;
    isom_create_basebox( minf, ISOM_BOX_TYPE_MINF );
    mdia->minf = minf;
    return 0;
}

static int isom_add_vmhd( isom_minf_t *minf )
{
    if( !minf || minf->vmhd )
        return -1;
    isom_create_fullbox( vmhd, ISOM_BOX_TYPE_VMHD );
    vmhd->full_header.flags = 0x000001;
    minf->vmhd = vmhd;
    return 0;
}

static int isom_add_smhd( isom_minf_t *minf )
{
    if( !minf || minf->smhd )
        return -1;
    isom_create_fullbox( smhd, ISOM_BOX_TYPE_SMHD );
    minf->smhd = smhd;
    return 0;
}

static int isom_add_hmhd( isom_minf_t *minf )
{
    if( !minf || minf->hmhd )
        return -1;
    isom_create_fullbox( hmhd, ISOM_BOX_TYPE_HMHD );
    minf->hmhd = hmhd;
    return 0;
}

static int isom_add_nmhd( isom_minf_t *minf )
{
    if( !minf || minf->nmhd )
        return -1;
    isom_create_fullbox( nmhd, ISOM_BOX_TYPE_NMHD );
    minf->nmhd = nmhd;
    return 0;
}

static int isom_add_dinf( isom_minf_t *minf )
{
    if( !minf || minf->dinf )
        return -1;
    isom_create_basebox( dinf, ISOM_BOX_TYPE_DINF );
    minf->dinf = dinf;
    return 0;
}

static int isom_add_dref( isom_dinf_t *dinf )
{
    if( !dinf || dinf->dref )
        return -1;
    isom_create_list_fullbox( dref, ISOM_BOX_TYPE_DREF );
    dinf->dref = dref;
    if( isom_add_dref_entry( dref, 0x000001, NULL, NULL ) )
        return -1;
    return 0;
}

static int isom_add_stsd( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stsd )
        return -1;
    isom_create_list_fullbox( stsd, ISOM_BOX_TYPE_STSD );
    stbl->stsd = stsd;
    return 0;
}

static int isom_add_pasp( isom_visual_entry_t *visual )
{
    if( !visual || visual->pasp )
        return -1;
    isom_create_basebox( pasp, ISOM_BOX_TYPE_PASP );
    visual->pasp = pasp;
    return 0;
}

int isom_add_btrt( isom_root_t *root, uint32_t track_ID, uint32_t entry_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_avc_entry_t *data = isom_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_create_basebox( btrt, ISOM_BOX_TYPE_BTRT );
    data->btrt = btrt;
    return 0;
}

static int isom_add_stts( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stts )
        return -1;
    isom_create_list_fullbox( stts, ISOM_BOX_TYPE_STTS );
    stbl->stts = stts;
    return 0;
}

static int isom_add_ctts( isom_stbl_t *stbl )
{
    if( !stbl || stbl->ctts )
        return -1;
    isom_create_list_fullbox( ctts, ISOM_BOX_TYPE_CTTS );
    stbl->ctts = ctts;
    return 0;
}

static int isom_add_stsc( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stsc )
        return -1;
    isom_create_list_fullbox( stsc, ISOM_BOX_TYPE_STSC );
    stbl->stsc = stsc;
    return 0;
}

static int isom_add_stsz( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stsz )
        return -1;
    isom_create_fullbox( stsz, ISOM_BOX_TYPE_STSZ );  /* We don't create a list here. */
    stbl->stsz = stsz;
    return 0;
}

static int isom_add_stss( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stss )
        return -1;
    isom_create_list_fullbox( stss, ISOM_BOX_TYPE_STSS );
    stbl->stss = stss;
    return 0;
}

int isom_add_stps( isom_stbl_t *stbl )
{
    if( !stbl || stbl->stps )
        return -1;
    isom_create_list_fullbox( stps, QT_BOX_TYPE_STPS );
    stbl->stps = stps;
    return 0;
}

static int isom_add_sdtp( isom_stbl_t *stbl )
{
    if( !stbl || stbl->sdtp )
        return -1;
    isom_create_list_fullbox( sdtp, ISOM_BOX_TYPE_SDTP );
    stbl->sdtp = sdtp;
    return 0;
}

#if 0
static int isom_add_sgpd( isom_stbl_t *stbl, uint32_t grouping_type )
{
    if( !stbl )
        return -1;
    uint64_t grouping_count = 1;
    isom_sgpd_t *sgpd_array;
    if( !stbl->sgpd )
        sgpd_array = malloc( sizeof(isom_sgpd_t) );
    else
    {
        grouping_count += stbl->grouping_count;
        sgpd_array = realloc( stbl->sgpd, grouping_count * sizeof(isom_sgpd_t) );
    }
    if( !sgpd_array )
        return -1;
    isom_sgpd_t *sgpd = sgpd_array + grouping_count - 1;
    memset( sgpd, 0, sizeof(isom_sgpd_t) );
    isom_init_full_header( &sgpd->full_header, ISOM_BOX_TYPE_SGPD );
    sgpd->list = isom_create_entry_list();
    if( !sgpd->list )
    {
        stbl->sgpd = NULL;
        free( sgpd_array );
        return -1;
    }
    stbl->sgpd = sgpd_array;
    sgpd->grouping_type = grouping_type;
    return 0;
}

static int isom_add_sbgp( isom_stbl_t *stbl, uint32_t grouping_type )
{
    if( !stbl )
        return -1;
    uint64_t grouping_count = 1;
    isom_sbgp_t *sbgp_array;
    if( !stbl->sbgp )
        sbgp_array = malloc( sizeof(isom_sbgp_t) );
    else
    {
        grouping_count += stbl->grouping_count;
        sbgp_array = realloc( stbl->sbgp, grouping_count * sizeof(isom_sbgp_t) );
    }
    if( !sbgp_array )
        return -1;
    isom_sbgp_t *sbgp = sbgp_array + grouping_count - 1;
    memset( sbgp, 0, sizeof(isom_sbgp_t) );
    isom_init_full_header( &sbgp->full_header, ISOM_BOX_TYPE_SBGP );
    sbgp->list = isom_create_entry_list();
    if( !sbgp->list )
    {
        stbl->sbgp = NULL;
        free( sbgp_array );
        return -1;
    }
    stbl->sbgp = sbgp_array;
    sbgp->grouping_type = grouping_type;
    if( isom_add_sgpd( stbl, grouping_type ) )
        return -1;
    stbl->grouping_count = grouping_count;
    return 0;
}
#endif

static int isom_add_stbl( isom_minf_t *minf )
{
    if( !minf || minf->stbl )
        return -1;
    isom_create_basebox( stbl, ISOM_BOX_TYPE_STBL );
    minf->stbl = stbl;
    return 0;
}

static int isom_add_chpl( isom_moov_t *moov )
{
    if( !moov || !moov->udta || moov->udta->chpl )
        return -1;
    isom_create_list_fullbox( chpl, ISOM_BOX_TYPE_CHPL );
    chpl->full_header.version = 1;
    moov->udta->chpl = chpl;
    return 0;
}

static int isom_add_udta( isom_root_t *root, uint32_t track_ID )
{
    /* track_ID == 0 means the direct addition to moov box */
    if( !track_ID )
    {
        if( !root || !root->moov )
            return -1;
        if( root->moov->udta )
            return 0;
        isom_create_basebox( udta, ISOM_BOX_TYPE_UDTA );
        root->moov->udta = udta;
        return 0;
    }
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak )
        return -1;
    if( trak->udta )
        return 0;
    isom_create_basebox( udta, ISOM_BOX_TYPE_UDTA );
    trak->udta = udta;
    return 0;
}

static isom_trak_entry_t *isom_add_trak( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return NULL;
    isom_moov_t *moov = root->moov;
    if( !moov->trak_list )
    {
        moov->trak_list = isom_create_entry_list();
        if( !moov->trak_list )
            return NULL;
    }
    isom_trak_entry_t *trak = malloc( sizeof(isom_trak_entry_t) );
    if( !trak )
        return NULL;
    memset( trak, 0, sizeof(isom_trak_entry_t) );
    isom_init_base_header( &trak->base_header, ISOM_BOX_TYPE_TRAK );
    isom_cache_t *cache = malloc( sizeof(isom_cache_t) );
    if( !cache )
        return NULL;
    memset( cache, 0, sizeof(isom_cache_t) );
    if( isom_add_entry( moov->trak_list, trak ) )
        return NULL;
    trak->root = root;
    trak->cache = cache;
    return trak;
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

static void isom_remove_edts( isom_edts_t *edts )
{
    if( !edts )
        return;
    isom_remove_list_fullbox( elst, edts );
    free( edts );
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
#if 0
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
            case ISOM_CODEC_TYPE_SVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC2_VIDEO :
#endif
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
#if 0
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
#endif
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
#if 0
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
            case ISOM_CODEC_TYPE_S263_VIDEO :
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
#endif
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
#if 0
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
            case ISOM_CODEC_TYPE_SAWP_AUDIO :
            case ISOM_CODEC_TYPE_SEVC_AUDIO :
            case ISOM_CODEC_TYPE_SQCP_AUDIO :
            case ISOM_CODEC_TYPE_SSMV_AUDIO :
            case ISOM_CODEC_TYPE_TWOS_AUDIO :
#endif
            {
                isom_audio_entry_t *audio = (isom_audio_entry_t *)entry->data;
                if( audio->exdata )
                    free( audio->exdata );
                free( audio );
                break;
            }
#if 0
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
#endif
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
    isom_remove_list_fullbox( stps, stbl );
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

static void isom_remove_dref( isom_dref_t *dref )
{
    if( !dref )
        return;
    if( !dref->list )
    {
        free( dref );
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

static void isom_remove_dinf( isom_dinf_t *dinf )
{
    if( !dinf )
        return;
    isom_remove_dref( dinf->dref );
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

static void isom_remove_trak( isom_trak_entry_t *trak )
{
    if( !trak )
        return;
    isom_remove_fullbox( tkhd, trak );
    isom_remove_edts( trak->edts );
    isom_remove_mdia( trak->mdia );
    isom_remove_udta( trak->udta );
    if( trak->cache )
    {
        isom_remove_list( trak->cache->chunk.pool, isom_delete_sample );
        free( trak->cache );
    }
    free( trak );
}

static void isom_remove_iods( isom_iods_t *iods )
{
    if( !iods )
        return;
    mp4sys_remove_ObjectDescriptor( iods->OD );
    free( iods );
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
        isom_remove_list( moov->trak_list, isom_remove_trak );
    free( moov );
    root->moov = NULL;
}

static void isom_remove_mdat( isom_mdat_t *mdat )
{
    if( mdat )
        free( mdat );
}

static void isom_remove_free( isom_free_t *skip )
{
    if( skip )
    {
        if( skip->data )
            free( skip->data );
        free( skip );
    }
}

/* Box writers */
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
    return isom_bs_write_data( bs );
}

static int isom_write_elst( isom_bs_t *bs, isom_trak_entry_t *trak )
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
    return isom_bs_write_data( bs );
}

static int isom_write_edts( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_edts_t *edts = trak->edts;
    if( !edts )
        return 0;
    isom_bs_put_base_header( bs, &edts->base_header );
    if( isom_bs_write_data( bs ) )
        return -1;
    return isom_write_elst( bs, trak );
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
    return isom_bs_write_data( bs );
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
    return isom_bs_write_data( bs );
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
    return isom_bs_write_data( bs );
}

static int isom_write_smhd( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_smhd_t *smhd = trak->mdia->minf->smhd;
    if( !smhd )
        return -1;
    isom_bs_put_full_header( bs, &smhd->full_header );
    isom_bs_put_be16( bs, smhd->balance );
    isom_bs_put_be16( bs, smhd->reserved );
    return isom_bs_write_data( bs );
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
    return isom_bs_write_data( bs );
}

static int isom_write_nmhd( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_nmhd_t *nmhd = trak->mdia->minf->nmhd;
    if( !nmhd )
        return -1;
    isom_bs_put_full_header( bs, &nmhd->full_header );
    return isom_bs_write_data( bs );
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
    return isom_bs_write_data( bs );
}

static int isom_write_dinf( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_dinf_t *dinf = trak->mdia->minf->dinf;
    if( !dinf )
        return -1;
    isom_bs_put_base_header( bs, &dinf->base_header );
    if( isom_bs_write_data( bs ) )
        return -1;
    return isom_write_dref( bs, trak );
}

static void isom_put_pasp( isom_bs_t *bs, isom_pasp_t *pasp )
{
    if( !pasp )
        return;
    isom_bs_put_base_header( bs, &pasp->base_header );
    isom_bs_put_be32( bs, pasp->hSpacing );
    isom_bs_put_be32( bs, pasp->vSpacing );
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
}

static int isom_write_esds( isom_bs_t *bs, isom_esds_t *esds )
{
    if( !bs || !esds )
        return -1;
    isom_bs_put_full_header( bs, &esds->full_header );
    return mp4sys_write_ES_Descriptor( bs, esds->ES );
}

static int isom_write_avc_entry( isom_bs_t *bs, isom_entry_t *entry )
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
    return isom_bs_write_data( bs );
}

static int isom_write_mp4a_entry( isom_bs_t *bs, isom_entry_t *entry )
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
    return isom_write_esds( bs, data->esds );
}

static int isom_write_audio_entry( isom_bs_t *bs, isom_entry_t *entry )
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
    isom_bs_put_bytes( bs, data->exdata, data->exdata_length );
    return isom_bs_write_data( bs );
}

#if 0
static int isom_write_visual_entry( isom_bs_t *bs, isom_entry_t *entry )
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
    return isom_bs_write_data( bs );
}

static int isom_write_hint_entry( isom_bs_t *bs, isom_entry_t *entry )
{
    isom_hint_entry_t *data = (isom_hint_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_base_header( bs, &data->base_header );
    isom_bs_put_bytes( bs, data->reserved, 6 );
    isom_bs_put_be16( bs, data->data_reference_index );
    if( data->data && data->data_length )
        isom_bs_put_bytes( bs, data->data, data->data_length );
    return isom_bs_write_data( bs );
}

static int isom_write_metadata_entry( isom_bs_t *bs, isom_entry_t *entry )
{
    isom_metadata_entry_t *data = (isom_metadata_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_base_header( bs, &data->base_header );
    isom_bs_put_bytes( bs, data->reserved, 6 );
    isom_bs_put_be16( bs, data->data_reference_index );
    return isom_bs_write_data( bs );
}
#endif

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
#if 0
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
            case ISOM_CODEC_TYPE_SVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC2_VIDEO :
#endif
                isom_write_avc_entry( bs, entry );
                break;
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
                isom_write_mp4a_entry( bs, entry );
                break;
#if 0
            case ISOM_CODEC_TYPE_DRAC_VIDEO :
            case ISOM_CODEC_TYPE_ENCV_VIDEO :
            case ISOM_CODEC_TYPE_MJP2_VIDEO :
            case ISOM_CODEC_TYPE_S263_VIDEO :
            case ISOM_CODEC_TYPE_VC_1_VIDEO :
                isom_write_visual_entry( bs, entry );
                break;
#endif
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
#if 0
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
            case ISOM_CODEC_TYPE_SAWP_AUDIO :
            case ISOM_CODEC_TYPE_SEVC_AUDIO :
            case ISOM_CODEC_TYPE_SQCP_AUDIO :
            case ISOM_CODEC_TYPE_SSMV_AUDIO :
            case ISOM_CODEC_TYPE_TWOS_AUDIO :
#endif
                isom_write_audio_entry( bs, entry );
                break;
#if 0
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
                isom_write_hint_entry( bs, entry );
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
                isom_write_metadata_entry( bs, entry );
                break;
#endif
            default :
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
    return isom_bs_write_data( bs );
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
    return isom_bs_write_data( bs );
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
    return isom_bs_write_data( bs );
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
    return isom_bs_write_data( bs );
}

static int isom_write_stps( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stps_t *stps = trak->mdia->minf->stbl->stps;
    if( !stps )
        return 0;
    if( !stps->list )
        return -1;
    isom_bs_put_full_header( bs, &stps->full_header );
    isom_bs_put_be32( bs, stps->list->entry_count );
    for( isom_entry_t *entry = stps->list->head; entry; entry = entry->next )
    {
        isom_stps_entry_t *data = (isom_stps_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_be32( bs, data->sample_number );
    }
    return isom_bs_write_data( bs );
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
    return isom_bs_write_data( bs );
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
    return isom_bs_write_data( bs );
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
    return isom_bs_write_data( bs );
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
    return isom_bs_write_data( bs );
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
    return isom_bs_write_data( bs );
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
    return isom_bs_write_data( bs );
}

static int isom_write_stbl( isom_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    if( !stbl )
        return -1;
    isom_bs_put_base_header( bs, &stbl->base_header );
    if( isom_bs_write_data( bs ) )
        return -1;
    if( isom_write_stsd( bs, trak ) ||
        isom_write_stts( bs, trak ) ||
        isom_write_ctts( bs, trak ) ||
        isom_write_stss( bs, trak ) ||
        isom_write_stps( bs, trak ) ||
        isom_write_sdtp( bs, trak ) ||
        isom_write_stsc( bs, trak ) ||
        isom_write_stsz( bs, trak ) ||
        isom_write_stco( bs, trak ) )
        return -1;
    for( uint32_t i = 1; i <= trak->mdia->minf->stbl->grouping_count; i++ )
    {
        if( isom_write_sbgp( bs, trak, i ) ||
            isom_write_sgpd( bs, trak, i ) )
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
    if( (minf->vmhd && isom_write_vmhd( bs, trak )) ||
        (minf->smhd && isom_write_smhd( bs, trak )) ||
        (minf->hmhd && isom_write_hmhd( bs, trak )) ||
        (minf->nmhd && isom_write_nmhd( bs, trak )) )
        return -1;
    if( isom_write_dinf( bs, trak ) ||
        isom_write_stbl( bs, trak ) )
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
    if( isom_write_mdhd( bs, trak ) ||
        isom_write_hdlr( bs, trak ) ||
        isom_write_minf( bs, trak ) )
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
    return isom_bs_write_data( bs );
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
    if( isom_write_tkhd( bs, trak ) ||
        isom_write_edts( bs, trak ) ||
        isom_write_mdia( bs, trak ) ||
        isom_write_udta( bs, NULL, trak ) )
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
    return isom_bs_write_data( bs );
}

static int isom_bs_write_largesize_reserver( isom_bs_t *bs )
{
    isom_bs_put_be32( bs, ISOM_DEFAULT_BOX_HEADER_SIZE );
    isom_bs_put_be32( bs, ISOM_BOX_TYPE_FREE );
    return isom_bs_write_data( bs );
}

static int isom_write_mdat_header( isom_root_t *root )
{
    if( !root || !root->bs || !root->mdat )
        return -1;
    isom_mdat_t *mdat = root->mdat;
    isom_bs_t *bs = root->bs;
    mdat->base_header.size = 2 * ISOM_DEFAULT_BOX_HEADER_SIZE;
    mdat->header_pos = ftell( bs->stream );
    mdat->large_flag = 0;
    isom_bs_put_base_header( bs, &mdat->base_header );
    return isom_bs_write_largesize_reserver( bs );
}

static uint32_t isom_get_sample_count( isom_trak_entry_t *trak )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsz )
        return 0;
    return trak->mdia->minf->stbl->stsz->sample_count;
}

static uint64_t isom_get_dts( isom_stts_t *stts, uint32_t sample_number )
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

#if 0
static uint64_t isom_get_cts( isom_stts_t *stts, isom_ctts_t *ctts, uint32_t sample_number )
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
    return isom_get_dts( stts, sample_number ) + data->sample_offset;
}
#endif

static int isom_replace_last_sample_delta( isom_stbl_t *stbl, uint32_t sample_delta )
{
    if( !stbl || !stbl->stts || !stbl->stts->list || !stbl->stts->list->tail || !stbl->stts->list->tail->data )
        return -1;
    isom_stts_entry_t *last_stts_data = (isom_stts_entry_t *)stbl->stts->list->tail->data;
    if( sample_delta != last_stts_data->sample_delta )
    {
        if( last_stts_data->sample_count > 1 )
        {
            last_stts_data->sample_count -= 1;
            if( isom_add_stts_entry( stbl, sample_delta ) )
                return -1;
        }
        else
            last_stts_data->sample_delta = sample_delta;
    }
    return 0;
}

static int isom_update_mdhd_duration( isom_trak_entry_t *trak, uint32_t last_sample_delta )
{
    if( !trak || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->minf || !trak->mdia->minf->stbl ||
        !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list || trak->mdia->minf->stbl->stts->list->entry_count == 0 )
        return -1;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_stts_t *stts = stbl->stts;
    isom_ctts_t *ctts = stbl->ctts;
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
        if( last_sample_delta )
        {
            mdhd->duration += last_sample_delta;
            if( isom_replace_last_sample_delta( stbl, last_sample_delta ) )
                return -1;
        }
        else if( last_stts_data->sample_count > 1 )
            mdhd->duration += last_stts_data->sample_delta; /* no need to update last_stts_data->sample_delta */
        else
        {
            /* Remove the last entry. */
            if( isom_remove_entry( stts->list, stts->list->entry_count ) )
                return -1;
            /* copy the previous sample_delta. */
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
        if( !last_sample_delta )
        {
            /* The spec allows an arbitrary value for the duration of the last sample. So, we pick last-1 sample's. */
            last_sample_delta = max_cts - max2_cts;
        }
        mdhd->duration = max_cts - min_cts + last_sample_delta;
        /* To match dts and mdhd duration, update stts and mdhd relatively. */
        if( mdhd->duration > dts )
            last_sample_delta = mdhd->duration - dts;
        else
            mdhd->duration = dts + last_sample_delta; /* mdhd duration must not less than last dts. */
        if( isom_replace_last_sample_delta( stbl, last_sample_delta ) )
            return -1;
    }
    if( mdhd->duration > UINT32_MAX )
        mdhd->full_header.version = 1;
    return 0;
}

static int isom_update_mvhd_duration( isom_moov_t *moov )
{
    if( !moov || !moov->mvhd )
        return -1;
    isom_mvhd_t *mvhd = moov->mvhd;
    mvhd->duration = 0;
    for( isom_entry_t *entry = moov->trak_list->head; entry; entry = entry->next )
    {
        /* We pick maximum track duration as movie duration. */
        isom_trak_entry_t *data = (isom_trak_entry_t *)entry->data;
        if( !data || !data->tkhd )
            return -1;
        mvhd->duration = entry != moov->trak_list->head ? ISOM_MAX( mvhd->duration, data->tkhd->duration ) : data->tkhd->duration;
    }
    if( mvhd->duration > UINT32_MAX )
        mvhd->full_header.version = 1;
    return 0;
}

static int isom_update_tkhd_duration( isom_trak_entry_t *trak )
{
    if( !trak || !trak->tkhd || !trak->root || !trak->root->moov )
        return -1;
    isom_tkhd_t *tkhd = trak->tkhd;
    tkhd->duration = 0;
    if( !trak->edts || !trak->edts->elst )
    {
        if( !trak->mdia || !trak->mdia->mdhd || !trak->root->moov->mvhd || !trak->mdia->mdhd->timescale )
            return -1;
        if( !trak->mdia->mdhd->duration && isom_update_mdhd_duration( trak, 0 ) )
            return -1;
        tkhd->duration = trak->mdia->mdhd->duration * ((double)trak->root->moov->mvhd->timescale / trak->mdia->mdhd->timescale);
    }
    else
    {
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
        tkhd->duration = tkhd->full_header.version == 1 ? 0xffffffffffffffff : 0xffffffff;
    return isom_update_mvhd_duration( trak->root->moov );
}

int isom_update_track_duration( isom_root_t *root, uint32_t track_ID, uint32_t last_sample_delta )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak )
        return -1;
    if( isom_update_mdhd_duration( trak, last_sample_delta ) )
        return -1;
    /* If the track already has a edit list, we don't change or update duration in tkhd and mvhd. */
    return trak->edts && trak->edts->elst ? 0 : isom_update_tkhd_duration( trak );
}

static int isom_add_size( isom_stbl_t *stbl, uint32_t sample_size )
{
    if( !sample_size )
        return -1;
    return isom_add_stsz_entry( stbl, sample_size );
}

static int isom_add_sync_point( isom_stbl_t *stbl, uint32_t sample_number, isom_sample_property_t *prop )
{
    if( !prop->sync_point ) /* no null check for prop */
        return 0;
    if( !stbl->stss && isom_add_stss( stbl ) )
        return -1;
    return isom_add_stss_entry( stbl, sample_number );
}

static int isom_add_partial_sync( isom_stbl_t *stbl, uint32_t sample_number, isom_sample_property_t *prop )
{
    if( !prop->partial_sync ) /* no null check for prop */
        return 0;
    if( !stbl->stps && isom_add_stps( stbl ) )
        return -1;
    return isom_add_stps_entry( stbl, sample_number );
}

static int isom_add_dependency_type( isom_trak_entry_t *trak, isom_sample_property_t *prop )
{
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    if( stbl->sdtp )
        return isom_add_sdtp_entry( stbl, prop );
    if( !prop->leading && !prop->independent && !prop->disposable && !prop->redundant )  /* no null check for prop */
        return 0;
    if( isom_add_sdtp( stbl ) )
        return -1;
    uint32_t count = isom_get_sample_count( trak );
    /* fill past samples with ISOM_SAMPLE_*_UNKNOWN */
    isom_sample_property_t null_prop = { 0 };
    for( uint32_t i = 1; i < count; i++ )
        if( isom_add_sdtp_entry( stbl, &null_prop ) )
            return -1;
    return isom_add_sdtp_entry( stbl, prop );
}

/* returns 1 if pooled samples must be flushed. */
/* FIXME: I wonder if this function should have a extra argument which indicates force_to_flush_cached_chunk.
   see isom_write_sample for detail. */
static int isom_add_chunk( isom_trak_entry_t *trak, isom_sample_t *sample )
{
    if( !trak->root || !trak->cache || !trak->mdia->mdhd || !trak->mdia->mdhd->timescale ||
        !trak->mdia->minf->stbl->stsc || !trak->mdia->minf->stbl->stsc->list )
        return -1;
    isom_chunk_t *current = &trak->cache->chunk;
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
    if( trak->root->max_chunk_duration >= chunk_duration )
        return 0; /* no need to flush current cached chunk, the current sample must be put into that. */

    /* NOTE: chunk relative stuff must be pushed into root after a chunk is fully determined with its contents. */
    /* now current cached chunk is fixed, actually add chunk relative properties to root accordingly. */

    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_stsc_t *stsc = stbl->stsc;
    /* Add a new chunk sequence in this track if needed. */
    if( !stsc->list->tail || current->pool->entry_count != ((isom_stsc_entry_t *)stsc->list->tail->data)->samples_per_chunk )
    {
        if( isom_add_stsc_entry( stbl, current->chunk_number, current->pool->entry_count, current->sample_description_index ) )
            return -1;
    }
    /* Add a new chunk offset in this track here. */
    if( isom_add_stco_entry( stbl, trak->root->bs->written ) )
        return -1;
    /* update cache information */
    ++ current->chunk_number;
    /* re-initialize cache, using the current sample */
    current->sample_description_index = sample->index;
    current->first_dts = sample->dts;
    /* current->pool must be flushed in isom_write_sample() */
    return 1;
}

static int isom_add_dts( isom_trak_entry_t *trak, uint64_t dts )
{
    if( !trak->cache || !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list )
        return -1;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_stts_t *stts = stbl->stts;
    if( !stts->list->entry_count )
    {
        if( isom_add_stts_entry( stbl, dts ) )
            return -1;
        trak->cache->timestamp.dts = dts;
        return 0;
    }
    if( dts <= trak->cache->timestamp.dts )
        return -1;
    uint32_t sample_delta = dts - trak->cache->timestamp.dts;
    isom_stts_entry_t *data = (isom_stts_entry_t *)stts->list->tail->data;
    if( data->sample_delta == sample_delta )
        ++ data->sample_count;
    else if( isom_add_stts_entry( stbl, sample_delta ) )
        return -1;
    trak->cache->timestamp.dts = dts;
    return 0;
}

static int isom_add_cts( isom_trak_entry_t *trak, uint64_t cts )
{
    if( !trak->cache )
        return -1;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_ctts_t *ctts = stbl->ctts;
    if( !ctts )
    {
        if( cts == trak->cache->timestamp.dts )
        {
            trak->cache->timestamp.cts = cts;
            return 0;
        }
        /* Add ctts box and the first ctts entry. */
        if( isom_add_ctts( stbl ) || isom_add_ctts_entry( stbl, 0 ) )
            return -1;
        uint32_t sample_count = isom_get_sample_count( trak );
        ctts = stbl->ctts;
        isom_ctts_entry_t *data = (isom_ctts_entry_t *)ctts->list->head->data;
        if( sample_count != 1 )
        {
            data->sample_count = isom_get_sample_count( trak ) - 1;
            if( isom_add_ctts_entry( stbl, cts - trak->cache->timestamp.dts ) )
                return -1;
        }
        else
            data->sample_offset = cts;
        trak->cache->timestamp.cts = cts;
        return 0;
    }
    if( !ctts->list )
        return -1;
    isom_ctts_entry_t *data = (isom_ctts_entry_t *)ctts->list->tail->data;
    uint32_t sample_offset = cts - trak->cache->timestamp.dts;
    if( data->sample_offset == sample_offset )
        ++ data->sample_count;
    else if( isom_add_ctts_entry( stbl, sample_offset ) )
        return -1;
    trak->cache->timestamp.cts = cts;
    return 0;
}

static int isom_add_timestamp( isom_trak_entry_t *trak, uint64_t dts, uint64_t cts )
{
    if( cts < dts )
        return -1;
    if( isom_get_sample_count( trak ) > 1 && isom_add_dts( trak, dts ) )
        return -1;
    return isom_add_cts( trak, cts );
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

static int isom_write_pooled_samples( isom_trak_entry_t *trak, isom_entry_list_t *pool )
{
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    for( isom_entry_t *entry = pool->head; entry; entry = entry->next )
    {
        isom_sample_t *data = (isom_sample_t *)entry->data;
        if( !data || !data->data )
            return -1;
        /* Add a sample_size and increment sample_count. */
        if( isom_add_size( stbl, data->length ) )
            return -1;
        /* Add a decoding timestamp and a composition timestamp. */
        if( isom_add_timestamp( trak, data->dts, data->cts ) )
            return -1;
        /* Add a sync point if needed. */
        if( isom_add_sync_point( stbl, isom_get_sample_count( trak ), &data->prop ) )
            return -1;
        /* Add a partial sync point if needed. */
        if( isom_add_partial_sync( stbl, isom_get_sample_count( trak ), &data->prop ) )
            return -1;
        /* Add leading, independent, disposable and redundant information if needed. */
        if( isom_add_dependency_type( trak, &data->prop ) )
            return -1;
        if( isom_write_sample_data( trak->root, data ) )
            return -1;
    }
    isom_remove_entries( pool, isom_delete_sample );
    return 0;
}

static int isom_output_cache( isom_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->cache || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl ||
        !trak->mdia->minf->stbl->stsc || !trak->mdia->minf->stbl->stsc->list )
        return -1;
    isom_chunk_t *current = &trak->cache->chunk;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    if( !stbl->stsc->list->tail ||
        current->pool->entry_count != ((isom_stsc_entry_t *)stbl->stsc->list->tail->data)->samples_per_chunk )
        if( isom_add_stsc_entry( stbl, current->chunk_number, current->pool->entry_count, current->sample_description_index ) )
            return -1;
    if( isom_add_stco_entry( stbl, root->bs->written ) ||
        isom_write_pooled_samples( trak, current->pool ) )
        return -1;
    return 0;
}

int isom_set_avc_config( isom_root_t *root, uint32_t track_ID, uint32_t entry_number,
    uint8_t configurationVersion, uint8_t AVCProfileIndication, uint8_t profile_compatibility, uint8_t AVCLevelIndication, uint8_t lengthSizeMinusOne,
    uint8_t chroma_format, uint8_t bit_depth_luma_minus8, uint8_t bit_depth_chroma_minus8 )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
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

int isom_update_bitrate_info( isom_root_t *root, uint32_t track_ID, uint32_t entry_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
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
            break;
        }
        case ISOM_CODEC_TYPE_ALAC_AUDIO :
        {
            isom_audio_entry_t *alac = (isom_audio_entry_t *)sample_entry;
            if( !alac || alac->exdata_length < 36 || !alac->exdata )
                return -1;
            uint8_t *exdata = (uint8_t *)alac->exdata + 28;
            exdata[0] = (info.avgBitrate >> 24) & 0xff;
            exdata[1] = (info.avgBitrate >> 16) & 0xff;
            exdata[2] = (info.avgBitrate >>  8) & 0xff;
            exdata[3] =  info.avgBitrate        & 0xff;
            break;
        }
        default :
            break;
    }
    return 0;
}

static int isom_check_mandatory_boxes( isom_root_t *root )
{
    if( !root )
        return 0;
    if( !root->moov || !root->moov->mvhd )
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

#define CHECK_LARGESIZE( size ) if( (size) > UINT32_MAX ) (size) += 8

static uint64_t isom_update_mvhd_size( isom_mvhd_t *mvhd )
{
    if( !mvhd )
        return 0;
    mvhd->full_header.version = 0;
    if( mvhd->creation_time > UINT32_MAX || mvhd->modification_time > UINT32_MAX || mvhd->duration > UINT32_MAX )
        mvhd->full_header.version = 1;
    mvhd->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 96 + (uint64_t)mvhd->full_header.version * 12;
    CHECK_LARGESIZE( mvhd->full_header.size );
    return mvhd->full_header.size;
}

static uint64_t isom_update_iods_size( isom_iods_t *iods )
{
    if( !iods || !iods->OD )
        return 0;
    iods->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + mp4sys_update_ObjectDescriptor_size( iods->OD );
    CHECK_LARGESIZE( iods->full_header.size );
    return iods->full_header.size;
}

static uint64_t isom_update_tkhd_size( isom_tkhd_t *tkhd )
{
    if( !tkhd )
        return 0;
    tkhd->full_header.version = 0;
    if( tkhd->creation_time > UINT32_MAX || tkhd->modification_time > UINT32_MAX || tkhd->duration > UINT32_MAX )
        tkhd->full_header.version = 1;
    tkhd->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 80 + (uint64_t)tkhd->full_header.version * 12;
    CHECK_LARGESIZE( tkhd->full_header.size );
    return tkhd->full_header.size;
}

static uint64_t isom_update_elst_size( isom_elst_t *elst )
{
    if( !elst || !elst->list )
        return 0;
    uint32_t i = 0;
    elst->full_header.version = 0;
    for( isom_entry_t *entry = elst->list->head; entry; entry = entry->next, i++ )
    {
        isom_elst_entry_t *data = (isom_elst_entry_t *)entry->data;
        if( data->segment_duration > UINT32_MAX || data->media_time > UINT32_MAX )
            elst->full_header.version = 1;
    }
    elst->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (uint64_t)i * ( elst->full_header.version ? 20 : 12 );
    CHECK_LARGESIZE( elst->full_header.size );
    return elst->full_header.size;
}

static uint64_t isom_update_edts_size( isom_edts_t *edts )
{
    if( !edts )
        return 0;
    edts->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + isom_update_elst_size( edts->elst );
    CHECK_LARGESIZE( edts->base_header.size );
    return edts->base_header.size;
}

static uint64_t isom_update_mdhd_size( isom_mdhd_t *mdhd )
{
    if( !mdhd )
        return 0;
    mdhd->full_header.version = 0;
    if( mdhd->creation_time > UINT32_MAX || mdhd->modification_time > UINT32_MAX || mdhd->duration > UINT32_MAX )
        mdhd->full_header.version = 1;
    mdhd->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 20 + (uint64_t)mdhd->full_header.version * 12;
    CHECK_LARGESIZE( mdhd->full_header.size );
    return mdhd->full_header.size;
}

static uint64_t isom_update_hdlr_size( isom_hdlr_t *hdlr )
{
    if( !hdlr )
        return 0;
    hdlr->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 20 + (uint64_t)hdlr->name_length;
    CHECK_LARGESIZE( hdlr->full_header.size );
    return hdlr->full_header.size;
}

static uint64_t isom_update_dref_entry_size( isom_dref_entry_t *urln )
{
    if( !urln )
        return 0;
    urln->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + (uint64_t)urln->name_length + urln->location_length;
    CHECK_LARGESIZE( urln->full_header.size );
    return urln->full_header.size;
}

static uint64_t isom_update_dref_size( isom_dref_t *dref )
{
    if( !dref || !dref->list )
        return 0;
    dref->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE;
    if( dref->list )
        for( isom_entry_t *entry = dref->list->head; entry; entry = entry->next )
        {
            isom_dref_entry_t *data = (isom_dref_entry_t *)entry->data;
            dref->full_header.size += isom_update_dref_entry_size( data );
        }
    CHECK_LARGESIZE( dref->full_header.size );
    return dref->full_header.size;
}

static uint64_t isom_update_dinf_size( isom_dinf_t *dinf )
{
    if( !dinf )
        return 0;
    dinf->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + isom_update_dref_size( dinf->dref );
    CHECK_LARGESIZE( dinf->base_header.size );
    return dinf->base_header.size;
}

static uint64_t isom_update_vmhd_size( isom_vmhd_t *vmhd )
{
    if( !vmhd )
        return 0;
    vmhd->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 8;
    CHECK_LARGESIZE( vmhd->full_header.size );
    return vmhd->full_header.size;
}

static uint64_t isom_update_smhd_size( isom_smhd_t *smhd )
{
    if( !smhd )
        return 0;
    smhd->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 4;
    CHECK_LARGESIZE( smhd->full_header.size );
    return smhd->full_header.size;
}

static uint64_t isom_update_hmhd_size( isom_hmhd_t *hmhd )
{
    if( !hmhd )
        return 0;
    hmhd->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 16;
    CHECK_LARGESIZE( hmhd->full_header.size );
    return hmhd->full_header.size;
}

static uint64_t isom_update_nmhd_size( isom_nmhd_t *nmhd )
{
    if( !nmhd )
        return 0;
    nmhd->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE;
    CHECK_LARGESIZE( nmhd->full_header.size );
    return nmhd->full_header.size;
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
    uint64_t size = ISOM_DEFAULT_BOX_HEADER_SIZE + 7;
    for( isom_entry_t *entry = avcC->sequenceParameterSets->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
        size += 2 + data->parameterSetLength;
    }
    for( isom_entry_t *entry = avcC->pictureParameterSets->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
        size += 2 + data->parameterSetLength;
    }
    if( ISOM_REQUIRES_AVCC_EXTENSION( avcC->AVCProfileIndication ) )
    {
        size += 4;
        for( isom_entry_t *entry = avcC->sequenceParameterSetExt->head; entry; entry = entry->next )
        {
            isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
            size += 2 + data->parameterSetLength;
        }
    }
    avcC->base_header.size = size;
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
    avc->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 78
        + isom_update_pasp_size( avc->pasp )
        + isom_update_clap_size( avc->clap )
        + isom_update_avcC_size( avc->avcC )
        + isom_update_btrt_size( avc->btrt );
    CHECK_LARGESIZE( avc->base_header.size );
    return avc->base_header.size;
}

static uint64_t isom_update_esds_size( isom_esds_t *esds )
{
    if( !esds )
        return 0;
    esds->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + mp4sys_update_ES_Descriptor_size( esds->ES );
    CHECK_LARGESIZE( esds->full_header.size );
    return esds->full_header.size;
}

static uint64_t isom_update_mp4a_entry_size( isom_mp4a_entry_t *mp4a )
{
    if( !mp4a || mp4a->base_header.type != ISOM_CODEC_TYPE_MP4A_AUDIO )
        return 0;
    mp4a->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 28 + isom_update_esds_size( mp4a->esds );
    CHECK_LARGESIZE( mp4a->base_header.size );
    return mp4a->base_header.size;
}

#if 0
static uint64_t isom_update_mp4v_entry_size( isom_mp4v_entry_t *mp4v )
{
    if( !mp4v || mp4v->base_header.type != ISOM_CODEC_TYPE_MP4V_VIDEO )
        return 0;
    mp4v->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 78
        + isom_update_esds_size( mp4v->esds )
        + isom_update_pasp_size( mp4v->pasp )
        + isom_update_clap_size( mp4v->clap );
    CHECK_LARGESIZE( mp4v->base_header.size );
    return mp4v->base_header.size;
}

static uint64_t isom_update_mp4s_entry_size( isom_mp4s_entry_t *mp4s )
{
    if( !mp4s || mp4s->base_header.type != ISOM_CODEC_TYPE_MP4S_SYSTEM )
        return 0;
    mp4s->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 8 + isom_update_esds_size( mp4s->esds );
    CHECK_LARGESIZE( mp4s->base_header.size );
    return mp4s->base_header.size;
}
#endif

static uint64_t isom_update_audio_entry_size( isom_audio_entry_t *audio )
{
    if( !audio )
        return 0;
    audio->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 28 + (uint64_t)audio->exdata_length;
    CHECK_LARGESIZE( audio->base_header.size );
    return audio->base_header.size;
}

static uint64_t isom_update_stsd_size( isom_stsd_t *stsd )
{
    if( !stsd || !stsd->list )
        return 0;
    uint64_t size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE;
    for( isom_entry_t *entry = stsd->list->head; entry; entry = entry->next )
    {
        isom_sample_entry_t *data = (isom_sample_entry_t *)entry->data;
        switch( data->base_header.type )
        {
            case ISOM_CODEC_TYPE_AVC1_VIDEO :
#if 0
            case ISOM_CODEC_TYPE_AVC2_VIDEO :
            case ISOM_CODEC_TYPE_AVCP_VIDEO :
            case ISOM_CODEC_TYPE_SVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC1_VIDEO :
            case ISOM_CODEC_TYPE_MVC2_VIDEO :
#endif
                size += isom_update_avc_entry_size( (isom_avc_entry_t *)data );
                break;
#if 0
            case ISOM_CODEC_TYPE_MP4V_VIDEO :
                size += isom_update_mp4v_entry_size( (isom_mp4v_entry_t *)data );
                break;
#endif
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
                size += isom_update_mp4a_entry_size( (isom_mp4a_entry_t *)data );
                break;
#if 0
            case ISOM_CODEC_TYPE_MP4S_SYSTEM :
                size += isom_update_mp4s_entry_size( (isom_mp4s_entry_t *)data );
                break;
            case ISOM_CODEC_TYPE_DRAC_VIDEO :
            case ISOM_CODEC_TYPE_ENCV_VIDEO :
            case ISOM_CODEC_TYPE_MJP2_VIDEO :
            case ISOM_CODEC_TYPE_S263_VIDEO :
            case ISOM_CODEC_TYPE_VC_1_VIDEO :
                size += isom_update_visual_entry_size( (isom_visual_entry_t *)data );
                break;
#endif
            case ISOM_CODEC_TYPE_AC_3_AUDIO :
            case ISOM_CODEC_TYPE_ALAC_AUDIO :
            case ISOM_CODEC_TYPE_SAMR_AUDIO :
            case ISOM_CODEC_TYPE_SAWB_AUDIO :
#if 0
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
            case ISOM_CODEC_TYPE_SAWP_AUDIO :
            case ISOM_CODEC_TYPE_SEVC_AUDIO :
            case ISOM_CODEC_TYPE_SQCP_AUDIO :
            case ISOM_CODEC_TYPE_SSMV_AUDIO :
            case ISOM_CODEC_TYPE_TWOS_AUDIO :
#endif
                size += isom_update_audio_entry_size( (isom_audio_entry_t *)data );
                break;
            default :
                break;
        }
    }
    stsd->full_header.size = size;
    CHECK_LARGESIZE( stsd->full_header.size );
    return stsd->full_header.size;
}

static uint64_t isom_update_stts_size( isom_stts_t *stts )
{
    if( !stts || !stts->list )
        return 0;
    stts->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (uint64_t)stts->list->entry_count * 8;
    CHECK_LARGESIZE( stts->full_header.size );
    return stts->full_header.size;
}

static uint64_t isom_update_ctts_size( isom_ctts_t *ctts )
{
    if( !ctts || !ctts->list )
        return 0;
    ctts->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (uint64_t)ctts->list->entry_count * 8;
    CHECK_LARGESIZE( ctts->full_header.size );
    return ctts->full_header.size;
}

static uint64_t isom_update_stsz_size( isom_stsz_t *stsz )
{
    if( !stsz )
        return 0;
    stsz->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 8 + ( stsz->list ? (uint64_t)stsz->list->entry_count * 4 : 0 );
    CHECK_LARGESIZE( stsz->full_header.size );
    return stsz->full_header.size;
}

static uint64_t isom_update_stss_size( isom_stss_t *stss )
{
    if( !stss || !stss->list )
        return 0;
    stss->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (uint64_t)stss->list->entry_count * 4;
    CHECK_LARGESIZE( stss->full_header.size );
    return stss->full_header.size;
}

static uint64_t isom_update_stps_size( isom_stps_t *stps )
{
    if( !stps || !stps->list )
        return 0;
    stps->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (uint64_t)stps->list->entry_count * 4;
    CHECK_LARGESIZE( stps->full_header.size );
    return stps->full_header.size;
}

static uint64_t isom_update_sdtp_size( isom_sdtp_t *sdtp )
{
    if( !sdtp || !sdtp->list )
        return 0;
    sdtp->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + (uint64_t)sdtp->list->entry_count;
    CHECK_LARGESIZE( sdtp->full_header.size );
    return sdtp->full_header.size;
}

static uint64_t isom_update_stsc_size( isom_stsc_t *stsc )
{
    if( !stsc || !stsc->list )
        return 0;
    stsc->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (uint64_t)stsc->list->entry_count * 12;
    CHECK_LARGESIZE( stsc->full_header.size );
    return stsc->full_header.size;
}

static uint64_t isom_update_stco_size( isom_stco_t *stco )
{
    if( !stco || !stco->list )
        return 0;
    stco->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (uint64_t)stco->list->entry_count * (stco->large_presentation ? 8 : 4);
    CHECK_LARGESIZE( stco->full_header.size );
    return stco->full_header.size;
}

static uint64_t isom_update_sbgp_size( isom_sbgp_t *sbgp )
{
    if( !sbgp || !sbgp->list )
        return 0;
    sbgp->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + 4 + (uint64_t)sbgp->list->entry_count * 8;
    CHECK_LARGESIZE( sbgp->full_header.size );
    return sbgp->full_header.size;
}

static uint64_t isom_update_sgpd_size( isom_sgpd_t *sgpd )
{
    if( !sgpd || !sgpd->list )
        return 0;
    uint64_t size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + (1 + (sgpd->full_header.version == 1)) * 4;
    size += (uint64_t)sgpd->list->entry_count * ((sgpd->full_header.version == 1) && !sgpd->default_length) * 4;
    switch( sgpd->grouping_type )
    {
        case ISOM_GROUP_TYPE_ROLL :
            size += (uint64_t)sgpd->list->entry_count * 2;
            break;
        default :
            break;
    }
    sgpd->full_header.size = size;
    CHECK_LARGESIZE( sgpd->full_header.size );
    return sgpd->full_header.size;
}

static uint64_t isom_update_stbl_size( isom_stbl_t *stbl )
{
    if( !stbl )
        return 0;
    stbl->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_stsd_size( stbl->stsd )
        + isom_update_stts_size( stbl->stts )
        + isom_update_ctts_size( stbl->ctts )
        + isom_update_stsz_size( stbl->stsz )
        + isom_update_stss_size( stbl->stss )
        + isom_update_stps_size( stbl->stps )
        + isom_update_sdtp_size( stbl->sdtp )
        + isom_update_stsc_size( stbl->stsc )
        + isom_update_stco_size( stbl->stco );
    for( uint32_t i = 0; i < stbl->grouping_count; i++ )
    {
        stbl->base_header.size += isom_update_sbgp_size( stbl->sbgp + i );
        stbl->base_header.size += isom_update_sgpd_size( stbl->sgpd + i );
    }
    CHECK_LARGESIZE( stbl->base_header.size );
    return stbl->base_header.size;
}

static uint64_t isom_update_minf_size( isom_minf_t *minf )
{
    if( !minf )
        return 0;
    minf->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_vmhd_size( minf->vmhd )
        + isom_update_smhd_size( minf->smhd )
        + isom_update_hmhd_size( minf->hmhd )
        + isom_update_nmhd_size( minf->nmhd )
        + isom_update_dinf_size( minf->dinf )
        + isom_update_stbl_size( minf->stbl );
    CHECK_LARGESIZE( minf->base_header.size );
    return minf->base_header.size;
}

static uint64_t isom_update_mdia_size( isom_mdia_t *mdia )
{
    if( !mdia )
        return 0;
    mdia->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_mdhd_size( mdia->mdhd )
        + isom_update_hdlr_size( mdia->hdlr )
        + isom_update_minf_size( mdia->minf );
    CHECK_LARGESIZE( mdia->base_header.size );
    return mdia->base_header.size;
}

static uint64_t isom_update_chpl_size( isom_chpl_t *chpl )
{
    if( !chpl )
        return 0;
    chpl->full_header.size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE + 1;
    for( isom_entry_t *entry = chpl->list->head; entry; entry = entry->next )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        chpl->full_header.size += 9 + data->name_length;
    }
    CHECK_LARGESIZE( chpl->full_header.size );
    return chpl->full_header.size;
}

static uint64_t isom_update_udta_size( isom_udta_t *udta_moov, isom_udta_t *udta_trak )
{
    isom_udta_t *udta = udta_trak ? udta_trak : udta_moov ? udta_moov : NULL;
    if( !udta )
        return 0;
    udta->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + ( udta_moov ? isom_update_chpl_size( udta->chpl ) : 0 );
    CHECK_LARGESIZE( udta->base_header.size );
    return udta->base_header.size;
}

static uint64_t isom_update_trak_entry_size( isom_trak_entry_t *trak )
{
    if( !trak )
        return 0;
    trak->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_tkhd_size( trak->tkhd )
        + isom_update_edts_size( trak->edts )
        + isom_update_mdia_size( trak->mdia )
        + isom_update_udta_size( NULL, trak->udta );
    CHECK_LARGESIZE( trak->base_header.size );
    return trak->base_header.size;
}

static int isom_update_moov_size( isom_moov_t *moov )
{
    if( !moov )
        return -1;
    moov->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_mvhd_size( moov->mvhd )
        + isom_update_iods_size( moov->iods )
        + isom_update_udta_size( moov->udta, NULL );
    if( moov->trak_list )
        for( isom_entry_t *entry = moov->trak_list->head; entry; entry = entry->next )
        {
            isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
            moov->base_header.size += isom_update_trak_entry_size( trak );
        }
    CHECK_LARGESIZE( moov->base_header.size );
    return 0;
}

/*******************************
    public interfaces
*******************************/

/*---- track manipulators ----*/

void isom_delete_track( isom_root_t *root, uint32_t track_ID )
{
    if( !root || !root->moov || !root->moov->trak_list )
        return;
    for( isom_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
        if( !trak || !trak->tkhd )
            return;
        if( trak->tkhd->track_ID == track_ID )
        {
            isom_entry_t *next = entry->next;
            isom_entry_t *prev = entry->prev;
            isom_remove_trak( trak );
            free( entry );
            entry = next;
            if( entry )
            {
                if( prev )
                    prev->next = entry;
                entry->prev = prev;
            }
            return;
        }
    }
}

uint32_t isom_create_track( isom_root_t *root, uint32_t handler_type )
{
    isom_trak_entry_t *trak = isom_add_trak( root );
    if( !trak )
        return 0;
    if( isom_add_tkhd( trak, handler_type ) ||
        isom_add_mdia( trak ) ||
        isom_add_mdhd( trak->mdia ) ||
        isom_add_minf( trak->mdia ) ||
        isom_add_stbl( trak->mdia->minf ) ||
        isom_add_dinf( trak->mdia->minf ) ||
        isom_add_dref( trak->mdia->minf->dinf ) ||
        isom_add_hdlr( trak->mdia, handler_type ) ||
        isom_add_stsd( trak->mdia->minf->stbl ) ||
        isom_add_stts( trak->mdia->minf->stbl ) ||
        isom_add_stsc( trak->mdia->minf->stbl ) ||
        isom_add_stco( trak->mdia->minf->stbl ) ||
        isom_add_stsz( trak->mdia->minf->stbl ) )
        return 0;
    switch( handler_type )
    {
        case ISOM_HDLR_TYPE_VISUAL :
            if( isom_add_vmhd( trak->mdia->minf ) )
                return 0;
            break;
        case ISOM_HDLR_TYPE_AUDIO :
            if( isom_add_smhd( trak->mdia->minf ) )
                return 0;
            break;
        case ISOM_HDLR_TYPE_HINT :
            if( isom_add_hmhd( trak->mdia->minf ) )
                return 0;
            break;
        default :
            if( isom_add_nmhd( trak->mdia->minf ) )
                return 0;
            break;
    }
    return trak->tkhd->track_ID;
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

int isom_set_media_timescale( isom_root_t *root, uint32_t track_ID, uint32_t timescale )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return -1;
    trak->mdia->mdhd->timescale = timescale;
    return 0;
}

uint32_t isom_get_media_timescale( isom_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return 0;
    return trak->mdia->mdhd->timescale;
}

int isom_set_track_mode( isom_root_t *root, uint32_t track_ID, uint32_t mode )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->tkhd )
        return -1;
    trak->tkhd->full_header.flags = mode;
    return 0;
}

int isom_set_handler_name( isom_root_t *root, uint32_t track_ID, char *handler_name )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
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

int isom_set_language( isom_root_t *root, uint32_t track_ID, char *language )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd || !language || strlen( language ) != 3 )
        return -1;
    trak->mdia->mdhd->language = ISOM_LANG( language );
    return 0;
}

int isom_set_track_ID( isom_root_t *root, uint32_t track_ID, uint32_t new_track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->tkhd || !root->moov->mvhd )
        return -1;
    trak->tkhd->track_ID = new_track_ID;
    /* Update next_track_ID if needed. */
    if( root->moov->mvhd->next_track_ID <= new_track_ID )
        root->moov->mvhd->next_track_ID = new_track_ID + 1;
    return 0;
}

int isom_set_track_volume( isom_root_t *root, uint32_t track_ID, int16_t volume )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->tkhd )
        return -1;
    trak->tkhd->volume = volume;
    return 0;
}

int isom_set_track_presentation_size( isom_root_t *root, uint32_t track_ID, uint32_t width, uint32_t height )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->tkhd )
        return -1;
    trak->tkhd->width = width;
    trak->tkhd->height = height;
    return 0;
}

int isom_set_sample_resolution( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint16_t width, uint16_t height )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_visual_entry_t *data = (isom_visual_entry_t *)isom_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    data->width = width;
    data->height = height;
    return 0;
}

int isom_set_sample_aspect_ratio( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint32_t hSpacing, uint32_t vSpacing )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_visual_entry_t *data = (isom_visual_entry_t *)isom_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    if( !data->pasp && isom_add_pasp( data ) )
        return -1;
    isom_pasp_t *pasp = (isom_pasp_t *)data->pasp;
    pasp->hSpacing = hSpacing;
    pasp->vSpacing = vSpacing;
    return 0;
}

int isom_set_sample_type( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint32_t sample_type )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_sample_entry_t *data = (isom_sample_entry_t *)isom_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    data->base_header.type = sample_type;
    return 0;
}

/*---- movie manipulators ----*/

isom_root_t *isom_create_movie( char *filename )
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
    if( isom_add_moov( root ) || isom_add_mvhd( root->moov ) )
        return NULL;
    return root;
}

int isom_set_movie_timescale( isom_root_t *root, uint32_t timescale )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return -1;
    root->moov->mvhd->timescale = timescale;
    return 0;
}

uint32_t isom_get_movie_timescale( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return 0;
    return root->moov->mvhd->timescale;
}

int isom_set_brands( isom_root_t *root, uint32_t major_brand, uint32_t minor_version, uint32_t *brands, uint32_t brand_count )
{
    if( !root || !brand_count )
        return -1;
    if( !root->ftyp && isom_add_ftyp( root ) )
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

int isom_set_max_chunk_duration( isom_root_t *root, double max_chunk_duration )
{
    if( !root )
        return -1;
    root->max_chunk_duration = max_chunk_duration;
    return 0;
}

int isom_write_ftyp( isom_root_t *root )
{
    isom_bs_t *bs = root->bs;
    isom_ftyp_t *ftyp = root->ftyp;
    isom_bs_put_base_header( bs, &ftyp->base_header );
    isom_bs_put_be32( bs, ftyp->major_brand );
    isom_bs_put_be32( bs, ftyp->minor_version );
    for( uint32_t i = 0; i < ftyp->brand_count; i++ )
        isom_bs_put_be32( bs, ftyp->compatible_brands[i] );
    return isom_bs_write_data( bs );
}

int isom_write_moov( isom_root_t *root )
{
    if( !root || !root->moov )
        return -1;
    isom_bs_put_base_header( root->bs, &root->moov->base_header );
    if( isom_bs_write_data( root->bs ) )
        return -1;
    if( isom_write_mvhd( root ) ||
        isom_write_iods( root ) )
        return -1;
    if( root->moov->trak_list )
        for( isom_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
            if( isom_write_trak( root->bs, (isom_trak_entry_t *)entry->data ) )
                return -1;
    return isom_write_udta( root->bs, root->moov, NULL );
}

/* If a mdat box already exists, flush a current one and start a new one. */
int isom_add_mdat( isom_root_t *root )
{
    if( !root )
        return 0;
    if( root->mdat && isom_write_mdat_size( root ) )    /* flush a current mdat */
        return -1;
    else
    {
        isom_create_basebox( mdat, ISOM_BOX_TYPE_MDAT );
        root->mdat = mdat;
    }
    return isom_write_mdat_header( root );     /* start a new mdat */
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
    return isom_bs_write_data( bs );
}

/*---- finishing functions ----*/

int isom_finish_movie( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->trak_list )
        return -1;
    for( isom_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
        if( !trak || !trak->tkhd )
            return -1;
        if( isom_set_track_mode( root, trak->tkhd->track_ID, ISOM_TRACK_ENABLED ) )
            return -1;
    }
    if( isom_add_iods( root->moov ) )
        return -1;
    if( isom_check_mandatory_boxes( root ) ||
        isom_set_movie_creation_time( root ) ||
        isom_update_moov_size( root->moov ) ||
        isom_write_moov( root ) )
        return -1;
    return 0;
}

int isom_set_last_sample_delta( isom_root_t *root, uint32_t track_ID, uint32_t sample_delta )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->minf || !trak->mdia->minf->stbl ||
        !trak->mdia->minf->stbl->stsz || !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list )
        return -1;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_stts_t *stts = stbl->stts;
    uint32_t sample_count = isom_get_sample_count( trak );
    if( !stts->list->tail )
    {
        if( sample_count != 1 )
            return -1;
        if( isom_add_stts_entry( stbl, sample_delta ) )
            return -1;
        return isom_update_track_duration( root, track_ID, 0 );
    }
    uint32_t i = 0;
    for( isom_entry_t *entry = stts->list->head; entry; entry = entry->next )
        i += ((isom_stts_entry_t *)entry->data)->sample_count;
    if( sample_count < i )
        return -1;
    isom_stts_entry_t *last_stts_data = (isom_stts_entry_t *)stts->list->tail->data;
    if( !last_stts_data )
        return -1;
    if( sample_count > i )
    {
        if( sample_count - i > 1 )
            return -1;
        /* Add a sample_delta. */
        if( sample_delta == last_stts_data->sample_delta )
            ++ last_stts_data->sample_count;
        else if( isom_add_stts_entry( stbl, sample_delta ) )
            return -1;
    }
    else if( sample_count == i && isom_replace_last_sample_delta( stbl, sample_delta ) )
        return -1;
    return isom_update_track_duration( root, track_ID, sample_delta );
}

int isom_flush_pooled_samples( isom_root_t *root, uint32_t track_ID, uint32_t last_sample_delta )
{
    if( isom_output_cache( root, track_ID ) )
        return -1;
    return isom_set_last_sample_delta( root, track_ID, last_sample_delta );
}

void isom_destroy_root( isom_root_t *root )
{
    if( !root )
        return;
    isom_remove_ftyp( root->ftyp );
    isom_remove_moov( root );
    isom_remove_mdat( root->mdat );
    isom_remove_free( root->free );
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

/*---- timeline manipulator ----*/

int isom_modify_timeline_map( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint64_t segment_duration, int64_t media_time, int32_t media_rate )
{
    if( !segment_duration || media_time < -1 )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->edts || !trak->edts->elst || !trak->edts->elst->list )
        return -1;
    isom_elst_entry_t *data = (isom_elst_entry_t *)isom_get_entry_data( trak->edts->elst->list, entry_number );
    if( !data )
        return -1;
    data->segment_duration = segment_duration;
    data->media_time = media_time;
    data->media_rate = media_rate;
    return isom_update_tkhd_duration( trak ) ? -1 : isom_update_mvhd_duration( root->moov );
}

int isom_create_explicit_timeline_map( isom_root_t *root, uint32_t track_ID, uint64_t segment_duration, int64_t media_time, int32_t media_rate )
{
    if( media_time < -1 )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->tkhd )
        return -1;
    segment_duration = segment_duration ? segment_duration :
                       trak->tkhd->duration ? trak->tkhd->duration :
                       isom_update_tkhd_duration( trak ) ? 0 : trak->tkhd->duration;
    if( isom_add_edts( trak ) )
        return -1;
    if( isom_add_elst( trak->edts ) )
        return -1;
    if( isom_add_elst_entry( trak->edts->elst, segment_duration, media_time, media_rate ) )
        return -1;
    return isom_update_tkhd_duration( trak );
}

/*---- create / modification time fields manipulators ----*/

int isom_update_media_modification_time( isom_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return -1;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    mdhd->modification_time = isom_get_current_mp4time();
    /* overwrite strange creation_time */
    if( mdhd->creation_time > mdhd->modification_time )
        mdhd->creation_time = mdhd->modification_time;
    return 0;
}

int isom_update_track_modification_time( isom_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->tkhd )
        return -1;
    isom_tkhd_t *tkhd = trak->tkhd;
    tkhd->modification_time = isom_get_current_mp4time();
    /* overwrite strange creation_time */
    if( tkhd->creation_time > tkhd->modification_time )
        tkhd->creation_time = tkhd->modification_time;
    return 0;
}

int isom_update_movie_modification_time( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return -1;
    isom_mvhd_t *mvhd = root->moov->mvhd;
    mvhd->modification_time = isom_get_current_mp4time();
    /* overwrite strange creation_time */
    if( mvhd->creation_time > mvhd->modification_time )
        mvhd->creation_time = mvhd->modification_time;
    return 0;
}

/*---- sample manipulators ----*/

int isom_write_sample( isom_root_t *root, uint32_t track_ID, isom_sample_t *sample )
{
    /* I myself think max_chunk_duration == 0, which means all samples will be cached on memory, should be prevented.
       This means removal of a feature that we used to have, but anyway very alone chunk does not make sense. */
    if( !root || !sample || !sample->data || root->max_chunk_duration == 0 )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
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
    int ret = isom_add_chunk( trak, sample );
    if( ret < 0 )
        return -1;

    /* ret == 1 means cached samples must be flushed. */
    isom_chunk_t *current = &trak->cache->chunk;
    if( ret == 1 && isom_write_pooled_samples( trak, current->pool ) )
        return -1;

    /* anyway the current sample must be pooled. */
    return isom_add_entry( current->pool, sample );
}

isom_sample_t *isom_create_sample( uint32_t size )
{
    isom_sample_t *sample = malloc( sizeof(isom_sample_t) );
    if( !sample )
        return NULL;
    memset( sample, 0, sizeof(isom_sample_t) );
    sample->data = malloc( size );
    if( !sample->data )
    {
        free( sample );
        return NULL;
    }
    sample->length = size;
    return sample;
}

void isom_delete_sample( isom_sample_t *sample )
{
    if( !sample )
        return;
    if( sample->data )
        free( sample->data );
    free( sample );
}

/*---- misc functions ----*/

int isom_set_tyrant_chapter( isom_root_t *root, char *file_name )
{
    /* This function should be called after updating of the latest movie duration. */
    if( !root || !root->moov || !root->moov->mvhd || !root->moov->mvhd->timescale || !root->moov->mvhd->duration )
        return -1;
    char buff[512];
    FILE *chapter = fopen( file_name, "rb" );
    if( !chapter )
        return -1;
    if( isom_add_udta( root, 0 ) || isom_add_chpl( root->moov ) )
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
        if( isom_add_chpl_entry( root->moov->udta->chpl, start_time, chapter_name ) )
            goto fail;
    }
    fclose( chapter );
    return 0;
fail:
    fclose( chapter );
    return -1;
}

void isom_delete_explicit_timeline_map( isom_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak )
        return;
    isom_remove_edts( trak->edts );
    trak->edts = NULL;
}

void isom_delete_tyrant_chapter( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->udta )
        return;
    isom_remove_chpl( root->moov->udta->chpl );
    root->moov->udta->chpl = NULL;
}
