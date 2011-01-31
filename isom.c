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
#include "summary.h" /* FIXME: to be replaced with lsmash.h or whatnot */

/* MP4 Audio Sample Entry */
typedef struct
{
    ISOM_AUDIO_SAMPLE_ENTRY;
    isom_esds_t *esds;
    mp4a_audioProfileLevelIndication pli; /* This is not used in mp4a box itself, but the value is specific for that. */
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
    (box_name)->list = lsmash_create_entry_list(); \
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

static void isom_bs_put_base_header( lsmash_bs_t *bs, isom_base_header_t *bh )
{
    if( bh->size > UINT32_MAX )
    {
        lsmash_bs_put_be32( bs, 1 );
        lsmash_bs_put_be32( bs, bh->type );
        lsmash_bs_put_be64( bs, bh->size );     /* largesize */
    }
    else
    {
        lsmash_bs_put_be32( bs, (uint32_t)bh->size );
        lsmash_bs_put_be32( bs, bh->type );
    }
    if( bh->type == ISOM_BOX_TYPE_UUID )
        lsmash_bs_put_bytes( bs, bh->usertype, 16 );
}

static void isom_bs_put_full_header( lsmash_bs_t *bs, isom_full_header_t *fbh )
{
    isom_base_header_t bh;
    memcpy( &bh, fbh, sizeof(isom_base_header_t) );
    isom_bs_put_base_header( bs, &bh );
    lsmash_bs_put_byte( bs, fbh->version );
    lsmash_bs_put_be24( bs, fbh->flags );
}

static isom_trak_entry_t *isom_get_trak( isom_root_t *root, uint32_t track_ID )
{
    if( !track_ID || !root || !root->moov || !root->moov->trak_list )
        return NULL;
    for( lsmash_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
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
    if( lsmash_add_entry( elst->list, data ) )
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
    if( lsmash_add_entry( dref->list, data ) )
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
    isom_avc_entry_t *data = (isom_avc_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_avcC_t *avcC = (isom_avcC_t *)data->avcC;
    if( !avcC )
        return -1;
    isom_avcC_ps_entry_t *ps = isom_create_ps_entry( sps, sps_size );
    if( !ps )
        return -1;
    if( lsmash_add_entry( avcC->sequenceParameterSets, ps ) )
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
    isom_avc_entry_t *data = (isom_avc_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_avcC_t *avcC = (isom_avcC_t *)data->avcC;
    if( !avcC )
        return -1;
    isom_avcC_ps_entry_t *ps = isom_create_ps_entry( pps, pps_size );
    if( !ps )
        return -1;
    if( lsmash_add_entry( avcC->pictureParameterSets, ps ) )
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
    isom_avc_entry_t *data = (isom_avc_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_avcC_t *avcC = (isom_avcC_t *)data->avcC;
    if( !avcC )
        return -1;
    isom_avcC_ps_entry_t *ps = isom_create_ps_entry( spsext, spsext_size );
    if( !ps )
        return -1;
    if( lsmash_add_entry( avcC->sequenceParameterSetExt, ps ) )
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
    lsmash_remove_list( avcC->sequenceParameterSets,   isom_remove_avcC_ps );
    lsmash_remove_list( avcC->pictureParameterSets,    isom_remove_avcC_ps );
    lsmash_remove_list( avcC->sequenceParameterSetExt, isom_remove_avcC_ps );
    free( avcC );
}

static int isom_add_avcC( lsmash_entry_list_t *list )
{
    if( !list )
        return -1;
    isom_avc_entry_t *data = (isom_avc_entry_t *)lsmash_get_entry_data( list, list->entry_count );
    if( !data )
        return -1;
    isom_create_basebox( avcC, ISOM_BOX_TYPE_AVCC );
    avcC->sequenceParameterSets = lsmash_create_entry_list();
    if( !avcC->sequenceParameterSets )
    {
        free( avcC );
        return -1;
    }
    avcC->pictureParameterSets = lsmash_create_entry_list();
    if( !avcC->pictureParameterSets )
    {
        isom_remove_avcC( avcC );
        return -1;
    }
    avcC->sequenceParameterSetExt = lsmash_create_entry_list();
    if( !avcC->sequenceParameterSetExt )
    {
        isom_remove_avcC( avcC );
        return -1;
    }
    data->avcC = avcC;
    return 0;
}

static int isom_add_avc_entry( lsmash_entry_list_t *list, uint32_t sample_type )
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
    if( lsmash_add_entry( list, avc ) || isom_add_avcC( list ) )
    {
        free( avc );
        return -1;
    }
    return 0;
}

static int isom_add_mp4a_entry( lsmash_entry_list_t *list, mp4sys_audio_summary_t* summary )
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
    if( lsmash_add_entry( list, mp4a ) )
    {
        mp4sys_remove_ES_Descriptor( esds->ES );
        free( esds );
        free( mp4a );
        return -1;
    }
    return 0;
}

#if 0
static int isom_add_mp4v_entry( lsmash_entry_list_t *list )
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
    if( lsmash_add_entry( list, mp4v ) )
    {
        free( mp4v );
        return -1;
    }
    return 0;
}

static int isom_add_mp4s_entry( lsmash_entry_list_t *list )
{
    if( !list )
        return -1;
    isom_mp4s_entry_t *mp4s = malloc( sizeof(isom_mp4s_entry_t) );
    if( !mp4s )
        return -1;
    memset( mp4s, 0, sizeof(isom_mp4s_entry_t) );
    isom_init_base_header( &mp4s->base_header, ISOM_CODEC_TYPE_MP4S_SYSTEM );
    mp4s->data_reference_index = 1;
    if( lsmash_add_entry( list, mp4s ) )
    {
        free( mp4s );
        return -1;
    }
    return 0;
}

static int isom_add_visual_entry( lsmash_entry_list_t *list, uint32_t sample_type )
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
    if( lsmash_add_entry( list, visual ) )
    {
        free( visual );
        return -1;
    }
    return 0;
}
#endif

static void isom_remove_wave( isom_wave_t *wave );

static int isom_add_wave( isom_audio_entry_t *audio )
{
    if( !audio || audio->wave )
        return -1;
    isom_create_basebox( wave, QT_BOX_TYPE_WAVE );
    audio->wave = wave;
    return 0;
}

static int isom_add_frma( isom_wave_t *wave )
{
    if( !wave || wave->frma )
        return -1;
    isom_create_basebox( frma, QT_BOX_TYPE_FRMA );
    wave->frma = frma;
    return 0;
}

static int isom_add_mp4a( isom_wave_t *wave )
{
    if( !wave || wave->mp4a )
        return -1;
    isom_create_basebox( mp4a, QT_BOX_TYPE_MP4A );
    wave->mp4a = mp4a;
    return 0;
}

static int isom_add_terminator( isom_wave_t *wave )
{
    if( !wave || wave->terminator )
        return -1;
    isom_create_basebox( terminator, QT_BOX_TYPE_TERMINATOR );
    wave->terminator = terminator;
    return 0;
}

static int isom_add_audio_entry( lsmash_entry_list_t *list, uint32_t sample_type, mp4sys_audio_summary_t *summary )
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
    if( audio->base_header.type == ISOM_CODEC_TYPE_MP4A_AUDIO )
    {
        audio->version = 1;
        audio->channelcount = ISOM_MIN( summary->channels, 2 );
        audio->compression_ID = -2;     /* assume VBR */
        audio->samplesPerPacket = summary->samples_in_frame;
        audio->bytesPerPacket = summary->bit_depth / 8;
        audio->bytesPerFrame = audio->bytesPerPacket * summary->channels;
        audio->bytesPerSample = 1 + (summary->bit_depth != 8);
        if( isom_add_wave( audio ) ||
            isom_add_frma( audio->wave ) ||
            isom_add_mp4a( audio->wave ) ||
            isom_add_terminator( audio->wave ) )
            goto fail;
        audio->wave->frma->data_format = sample_type;
        /* create ES Descriptor */
        isom_esds_t *esds = malloc( sizeof(isom_esds_t) );
        if( !esds )
            goto fail;
        memset( esds, 0, sizeof(isom_esds_t) );
        isom_init_full_header( &esds->full_header, ISOM_BOX_TYPE_ESDS );
        mp4sys_ES_Descriptor_params_t esd_param;
        memset( &esd_param, 0, sizeof(mp4sys_ES_Descriptor_params_t) );
        esd_param.objectTypeIndication = summary->object_type_indication;
        esd_param.streamType = summary->stream_type;
        esd_param.dsi_payload = summary->exdata;
        esd_param.dsi_payload_length = summary->exdata_length;
        esds->ES = mp4sys_setup_ES_Descriptor( &esd_param );
        if( !esds->ES )
        {
            free( esds );
            goto fail;
        }
        audio->wave->esds = esds;
    }
    else
    {
        if( summary->exdata )
        {
            audio->exdata_length = summary->exdata_length;
            audio->exdata = malloc( audio->exdata_length );
            if( !audio->exdata )
                goto fail;
            memcpy( audio->exdata, summary->exdata, audio->exdata_length );
        }
        else
            audio->exdata = NULL;
    }
    if( lsmash_add_entry( list, audio ) )
        goto fail;
    return 0;
fail:
    isom_remove_wave( audio->wave );
    if( audio->exdata )
        free( audio->exdata );
    free( audio );
    return -1;
}

static int isom_add_text_entry( lsmash_entry_list_t *list )
{
    if( !list )
        return -1;
    isom_text_entry_t *text = malloc( sizeof(isom_text_entry_t) );
    if( !text )
        return -1;
    memset( text, 0, sizeof(isom_text_entry_t) );
    isom_init_base_header( &text->base_header, QT_CODEC_TYPE_TEXT_TEXT );
    text->data_reference_index = 1;
    if( lsmash_add_entry( list, text ) )
    {
        free( text );
        return -1;
    }
    return 0;
}

static int isom_add_ftab( isom_tx3g_entry_t *tx3g )
{
    if( !tx3g )
        return -1;
    isom_ftab_t *ftab = malloc( sizeof(isom_ftab_t) );
    if( !ftab )
        return -1;
    memset( ftab, 0, sizeof(isom_ftab_t) );
    isom_init_base_header( &ftab->base_header, ISOM_BOX_TYPE_FTAB );
    ftab->list = lsmash_create_entry_list();
    if( !ftab->list )
    {
        free( ftab );
        return -1;
    }
    tx3g->ftab = ftab;
    return 0;
}

static int isom_add_tx3g_entry( lsmash_entry_list_t *list )
{
    if( !list )
        return -1;
    isom_tx3g_entry_t *tx3g = malloc( sizeof(isom_tx3g_entry_t) );
    if( !tx3g )
        return -1;
    memset( tx3g, 0, sizeof(isom_tx3g_entry_t) );
    isom_init_base_header( &tx3g->base_header, ISOM_CODEC_TYPE_TX3G_TEXT );
    tx3g->data_reference_index = 1;
    if( isom_add_ftab( tx3g ) ||
        lsmash_add_entry( list, tx3g ) )
    {
        free( tx3g );
        return -1;
    }
    return 0;
}

/* This function returns 0 if failed, sample_entry_number if succeeded. */
int isom_add_sample_entry( isom_root_t *root, uint32_t track_ID, uint32_t sample_type, void *summary )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->root || !trak->root->ftyp || !trak->mdia || !trak->mdia->minf ||
        !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return 0;
    lsmash_entry_list_t *list = trak->mdia->minf->stbl->stsd->list;
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
            if( trak->root->ftyp->major_brand != ISOM_BRAND_TYPE_QT )
                ret = isom_add_mp4a_entry( list, (mp4sys_audio_summary_t *)summary );
            else
                ret = isom_add_audio_entry( list, sample_type, (mp4sys_audio_summary_t *)summary );
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
        case ISOM_CODEC_TYPE_TX3G_TEXT :
            ret = isom_add_tx3g_entry( list );
            break;
        case QT_CODEC_TYPE_TEXT_TEXT :
            ret = isom_add_text_entry( list );
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
    if( lsmash_add_entry( stbl->stts->list, data ) )
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
    if( lsmash_add_entry( stbl->ctts->list, data ) )
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
    if( lsmash_add_entry( stbl->stsc->list, data ) )
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
        stsz->list = lsmash_create_entry_list();
        if( !stsz->list )
            return -1;
        for( uint32_t i = 0; i < stsz->sample_count; i++ )
        {
            isom_stsz_entry_t *data = malloc( sizeof(isom_stsz_entry_t) );
            if( !data )
                return -1;
            data->entry_size = stsz->sample_size;
            if( lsmash_add_entry( stsz->list, data ) )
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
    if( lsmash_add_entry( stsz->list, data ) )
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
    if( lsmash_add_entry( stbl->stss->list, data ) )
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
    if( lsmash_add_entry( stbl->stps->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_add_sdtp_entry( isom_stbl_t *stbl, isom_sample_property_t *prop, uint8_t avc_extensions )
{
    if( !prop )
        return -1;
    if( !stbl || !stbl->sdtp || !stbl->sdtp->list )
        return -1;
    isom_sdtp_entry_t *data = malloc( sizeof(isom_sdtp_entry_t) );
    if( !data )
        return -1;
    /* isom_sdtp_entry_t is smaller than isom_sample_property_t. */
    data->is_leading = (avc_extensions ? prop->leading : prop->allow_earlier) & 0x03;
    data->sample_depends_on = prop->independent & 0x03;
    data->sample_is_depended_on = prop->disposable & 0x03;
    data->sample_has_redundancy = prop->redundant & 0x03;
    if( lsmash_add_entry( stbl->sdtp->list, data ) )
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
    if( lsmash_add_entry( stbl->stco->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static int isom_convert_stco_to_co64( isom_stbl_t* stbl )
{
    /* backup stco */
    isom_stco_t *stco = stbl->stco;
    stbl->stco = NULL;
    if( isom_add_co64( stbl ) )
        return -1;
    /* move chunk_offset to co64 from stco */
    for( lsmash_entry_t *entry = stco->list->head; entry; entry = entry->next )
    {
        isom_stco_entry_t *data = (isom_stco_entry_t*)entry->data;
        if( isom_add_co64_entry( stbl, data->chunk_offset ) )
            return -1;
    }
    lsmash_remove_list( stco->list, NULL );
    free( stco );
    return 0;
}

static int isom_add_stco_entry( isom_stbl_t *stbl, uint64_t chunk_offset )
{
    if( !stbl || !stbl->stco || !stbl->stco->list )
        return -1;
    if( stbl->stco->large_presentation )
        return isom_add_co64_entry( stbl, chunk_offset );
    if( chunk_offset > UINT32_MAX )
    {
        if( isom_convert_stco_to_co64( stbl ) )
            return -1;
        return isom_add_co64_entry( stbl, chunk_offset );
    }
    isom_stco_entry_t *data = malloc( sizeof(isom_stco_entry_t) );
    if( !data )
        return -1;
    data->chunk_offset = (uint32_t)chunk_offset;
    if( lsmash_add_entry( stbl->stco->list, data ) )
    {
        free( data );
        return -1;
    }
    return 0;
}

static isom_sbgp_t *isom_get_sample_to_group( isom_stbl_t *stbl, uint32_t grouping_type )
{
    isom_sbgp_t *sbgp = NULL;
    for( uint32_t i = 0; i < stbl->grouping_count; i++ )
    {
        sbgp = stbl->sbgp + i;
        if( !sbgp || !sbgp->list )
            return NULL;
        if( sbgp->grouping_type == grouping_type )
            break;
    }
    return sbgp;
}

static isom_sgpd_t *isom_get_sample_group_description( isom_stbl_t *stbl, uint32_t grouping_type )
{
    isom_sgpd_t *sgpd = NULL;
    for( uint32_t i = 0; i < stbl->grouping_count; i++ )
    {
        sgpd = stbl->sgpd + i;
        if( !sgpd || !sgpd->list )
            return NULL;
        if( sgpd->grouping_type == grouping_type )
            break;
    }
    return sgpd;
}

static isom_sbgp_entry_t *isom_add_sbgp_entry( isom_sbgp_t *sbgp, uint32_t sample_count, uint32_t group_description_index )
{
    if( !sbgp )
        return NULL;
    isom_sbgp_entry_t *data = malloc( sizeof(isom_sbgp_entry_t) );
    if( !data )
        return NULL;
    data->sample_count = sample_count;
    data->group_description_index = group_description_index;
    if( lsmash_add_entry( sbgp->list, data ) )
    {
        free( data );
        return NULL;
    }
    return data;
}

static isom_roll_entry_t *isom_add_roll_group_entry( isom_sgpd_t *sgpd, int16_t roll_distance )
{
    if( !sgpd )
        return NULL;
    isom_roll_entry_t *data = malloc( sizeof(isom_roll_entry_t) );
     if( !data )
        return NULL;
    data->roll_distance = roll_distance;
    if( lsmash_add_entry( sgpd->list, data ) )
    {
        free( data );
        return NULL;
    }
    return data;
}

static int isom_add_chpl_entry( isom_chpl_t *chpl, uint64_t start_time, char *chapter_name )
{
    if( !chapter_name || !chpl || !chpl->list )
        return -1;
    isom_chpl_entry_t *data = malloc( sizeof(isom_chpl_entry_t) );
    if( !data )
        return -1;
    data->start_time = start_time;
    data->chapter_name_length = ISOM_MIN( strlen( chapter_name ), 255 );
    data->chapter_name = malloc( data->chapter_name_length + 1 );
    if( !data->chapter_name )
    {
        free( data );
        return -1;
    }
    memcpy( data->chapter_name, chapter_name, data->chapter_name_length );
    if( lsmash_add_entry( chpl->list, data ) )
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

static int isom_scan_trak_profileLevelIndication( isom_trak_entry_t* trak, mp4a_audioProfileLevelIndication* audio_pli, mp4sys_visualProfileLevelIndication* visual_pli )
{
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    isom_stsd_t* stsd = trak->mdia->minf->stbl->stsd;
    if( !stsd || !stsd->list || !stsd->list->head )
        return -1;
    for( lsmash_entry_t *entry = stsd->list->head; entry; entry = entry->next )
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
                *audio_pli = mp4a_max_audioProfileLevelIndication( *audio_pli, ((isom_mp4a_entry_t*)sample_entry)->pli );
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
                *audio_pli = MP4A_AUDIO_PLI_NOT_SPECIFIED;
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
    mp4a_audioProfileLevelIndication audio_pli = MP4A_AUDIO_PLI_NONE_REQUIRED;
    mp4sys_visualProfileLevelIndication visual_pli = MP4SYS_VISUAL_PLI_NONE_REQUIRED;
    for( lsmash_entry_t *entry = moov->trak_list->head; entry; entry = entry->next )
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
            case ISOM_MEDIA_HANDLER_TYPE_VIDEO :
                tkhd->matrix[0] = 0x00010000;
                tkhd->matrix[4] = 0x00010000;
                tkhd->matrix[8] = 0x40000000;
                break;
            case ISOM_MEDIA_HANDLER_TYPE_AUDIO :
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

static int isom_add_clef( isom_tapt_t *tapt )
{
    if( tapt->clef )
        return 0;
    isom_create_fullbox( clef, QT_BOX_TYPE_CLEF );
    tapt->clef = clef;
    return 0;
}

static int isom_add_prof( isom_tapt_t *tapt )
{
    if( tapt->prof )
        return 0;
    isom_create_fullbox( prof, QT_BOX_TYPE_PROF );
    tapt->prof = prof;
    return 0;
}

static int isom_add_enof( isom_tapt_t *tapt )
{
    if( tapt->enof )
        return 0;
    isom_create_fullbox( enof, QT_BOX_TYPE_ENOF );
    tapt->enof = enof;
    return 0;
}

static int isom_add_tapt( isom_trak_entry_t *trak )
{
    if( trak->tapt )
        return 0;
    isom_create_basebox( tapt, QT_BOX_TYPE_TAPT );
    trak->tapt = tapt;
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

static int isom_add_tref( isom_trak_entry_t *trak )
{
    if( trak->tref )
        return 0;
    isom_create_basebox( tref, ISOM_BOX_TYPE_TREF );
    trak->tref = tref;
    return 0;
}

static int isom_add_mdhd( isom_mdia_t *mdia, uint16_t default_language )
{
    if( !mdia || mdia->mdhd )
        return -1;
    isom_create_fullbox( mdhd, ISOM_BOX_TYPE_MDHD );
    mdhd->language = default_language;
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

static int isom_add_hdlr( isom_mdia_t *mdia, isom_minf_t *minf, uint32_t media_type, isom_root_t *root )
{
    if( (!mdia && !minf) || (mdia && minf) )
        return -1;    /* Either one must be given. */
    if( (mdia && mdia->hdlr) || (minf && minf->hdlr) )
        return -1;    /* Selected one must not have hdlr yet. */
    isom_create_fullbox( hdlr, ISOM_BOX_TYPE_HDLR );
    uint32_t type = mdia ? (root->qt_compatible ? ISOM_HANDLER_TYPE_MEDIA : 0) : ISOM_HANDLER_TYPE_DATA;
    uint32_t subtype = media_type;
    hdlr->type = type;
    hdlr->subtype = subtype;
    char *type_name = NULL;
    char *subtype_name = NULL;
    uint8_t type_name_length = 0;
    uint8_t subtype_name_length = 0;
    switch( type )
    {
        case ISOM_HANDLER_TYPE_DATA :
            type_name = "Data ";
            type_name_length = 5;
            break;
        default :
            type_name = "Media ";
            type_name_length = 6;
            break;
    }
    switch( subtype )
    {
        case ISOM_MEDIA_HANDLER_TYPE_AUDIO :
            subtype_name = "Sound ";
            subtype_name_length = 6;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_VIDEO :
            subtype_name = "Video ";
            subtype_name_length = 6;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_HINT :
            subtype_name = "Hint ";
            subtype_name_length = 5;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_META :
            subtype_name = "Meta ";
            subtype_name_length = 5;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_TEXT :
            subtype_name = "Text ";
            subtype_name_length = 5;
            break;
        case ISOM_REFERENCE_HANDLER_TYPE_ALIAS :
            subtype_name = "Alias ";
            subtype_name_length = 6;
            break;
        case ISOM_REFERENCE_HANDLER_TYPE_RESOURCE :
            subtype_name = "Resource ";
            subtype_name_length = 9;
            break;
        case ISOM_REFERENCE_HANDLER_TYPE_URL :
            subtype_name = "URL ";
            subtype_name_length = 4;
            break;
        default :
            subtype_name = "Unknown ";
            subtype_name_length = 8;
            break;
    }
    uint32_t name_length = 15 + subtype_name_length + type_name_length + root->isom_compatible + root->qt_compatible;
    uint8_t *name = malloc( name_length );
    if( !name )
        return -1;
    if( root->qt_compatible )
        name[0] = name_length & 0xff;
    memcpy( name + root->qt_compatible, "L-SMASH ", 8 );
    memcpy( name + root->qt_compatible + 8, subtype_name, subtype_name_length );
    memcpy( name + root->qt_compatible + 8 + subtype_name_length, type_name, type_name_length );
    memcpy( name + root->qt_compatible + 8 + subtype_name_length + type_name_length, "HANDLER", 7 );
    if( root->isom_compatible )
        name[name_length - 1] = 0;
    hdlr->name = name;
    hdlr->name_length = name_length;
    if( mdia )
        mdia->hdlr = hdlr;
    else
        minf->hdlr = hdlr;
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

static int isom_add_gmin( isom_gmhd_t *gmhd )
{
    if( !gmhd || gmhd->gmin )
        return -1;
    isom_create_fullbox( gmin, QT_BOX_TYPE_GMIN );
    gmhd->gmin = gmin;
    return 0;
}

static int isom_add_text( isom_gmhd_t *gmhd )
{
    if( !gmhd || gmhd->text )
        return -1;
    isom_create_basebox( text, QT_BOX_TYPE_TEXT );
    text->matrix[0] = 0x00010000;
    text->matrix[4] = 0x00010000;
    text->matrix[8] = 0x40000000;
    gmhd->text = text;
    return 0;
}

static int isom_add_gmhd( isom_minf_t *minf )
{
    if( !minf || minf->gmhd )
        return -1;
    isom_create_basebox( gmhd, QT_BOX_TYPE_GMHD );
    minf->gmhd = gmhd;
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
    pasp->hSpacing = 1;
    pasp->vSpacing = 1;
    visual->pasp = pasp;
    return 0;
}

static int isom_add_clap( isom_visual_entry_t *visual )
{
    if( !visual || visual->clap )
        return -1;
    isom_create_basebox( clap, ISOM_BOX_TYPE_CLAP );
    clap->cleanApertureWidthN = 1;
    clap->cleanApertureWidthD = 1;
    clap->cleanApertureHeightN = 1;
    clap->cleanApertureHeightD = 1;
    clap->horizOffN = 0;
    clap->horizOffD = 1;
    clap->vertOffN = 0;
    clap->vertOffD = 1;
    visual->clap = clap;
    return 0;
}

static int isom_add_stsl( isom_visual_entry_t *visual )
{
    if( !visual || visual->stsl )
        return -1;
    isom_create_fullbox( stsl, ISOM_BOX_TYPE_STSL );
    stsl->scale_method = ISOM_SCALING_METHOD_HIDDEN;
    visual->stsl = stsl;
    return 0;
}

int isom_add_btrt( isom_root_t *root, uint32_t track_ID, uint32_t entry_number )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_avc_entry_t *data = lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
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

static int isom_add_cslg( isom_stbl_t *stbl )
{
    if( !stbl || stbl->cslg )
        return -1;
    isom_create_fullbox( cslg, ISOM_BOX_TYPE_CSLG );
    stbl->cslg = cslg;
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
    sgpd->list = lsmash_create_entry_list();
    if( !sgpd->list )
    {
        stbl->sgpd = NULL;
        free( sgpd_array );
        return -1;
    }
    stbl->sgpd = sgpd_array;
    sgpd->grouping_type = grouping_type;
    sgpd->full_header.version = 1;  /* We use version 1 because it is recommended in the spec. */
    switch( grouping_type )
    {
        case ISOM_GROUP_TYPE_ROLL :
            sgpd->default_length = 2;
            break;
        default :
            /* We don't consider other grouping types currently. */
            break;
    }
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
    sbgp->list = lsmash_create_entry_list();
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
        moov->trak_list = lsmash_create_entry_list();
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
    if( lsmash_add_entry( moov->trak_list, trak ) )
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
        lsmash_remove_list( (box_type)->list, NULL ); \
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

static void isom_remove_tapt( isom_tapt_t *tapt )
{
    if( !tapt )
        return;
    isom_remove_fullbox( clef, tapt );
    isom_remove_fullbox( prof, tapt );
    isom_remove_fullbox( enof, tapt );
    free( tapt );
}

static void isom_remove_edts( isom_edts_t *edts )
{
    if( !edts )
        return;
    isom_remove_list_fullbox( elst, edts );
    free( edts );
}

static void isom_remove_tref( isom_tref_t *tref )
{
    if( !tref )
        return;
    for( uint32_t i = 0; i < tref->type_count; i++ )
    {
        isom_tref_type_t *type = &tref->type[i];
        if( type && type->track_ID )
            free( type->track_ID );
    }
    free( tref->type );
    free( tref );
}

static void isom_remove_esds( isom_esds_t *esds )
{
    if( !esds )
        return;
    mp4sys_remove_ES_Descriptor( esds->ES );
    free( esds );
}

static void isom_remove_font_record( isom_font_record_t *font_record )
{
    if( !font_record )
        return;
    if( font_record->font_name )
        free( font_record->font_name );
    free( font_record );
}

static void isom_remove_ftab( isom_ftab_t *ftab )
{
    if( !ftab )
        return;
    lsmash_remove_list( ftab->list, isom_remove_font_record );
    free( ftab );
}

static void isom_remove_wave( isom_wave_t *wave )
{
    if( !wave )
        return;
    free( wave->frma );
    free( wave->mp4a );
    isom_remove_esds( wave->esds );
    free( wave->terminator );
    free( wave );
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
    for( lsmash_entry_t *entry = stsd->list->head; entry; )
    {
        isom_sample_entry_t *sample = (isom_sample_entry_t *)entry->data;
        if( !sample )
        {
            lsmash_entry_t *next = entry->next;
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
                if( avc->stsl )
                    free( avc->stsl );
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
                if( mp4v->stsl )
                    free( mp4v->stsl );
                isom_remove_esds( mp4v->esds );
                free( mp4v );
                break;
            }
#endif
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
            {
                if( !((isom_audio_entry_t *)sample)->version )
                {
                    isom_mp4a_entry_t *mp4a = (isom_mp4a_entry_t *)entry->data;
                    isom_remove_esds( mp4a->esds );
                    free( mp4a );
                }
                else
                {
                    /* MPEG-4 Audio in QTFF */
                    isom_audio_entry_t *audio = (isom_audio_entry_t *)entry->data;
                    isom_remove_wave( audio->wave );
                    if( audio->exdata )
                        free( audio->exdata );
                    free( audio );
                }
                break;
            }
#if 0
            case ISOM_CODEC_TYPE_MP4S_SYSTEM :
            {
                isom_mp4s_entry_t *mp4s = (isom_mp4s_entry_t *)entry->data;
                isom_remove_esds( mp4s->esds );
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
                if( visual->stsl )
                    free( visual->stsl );
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
            case ISOM_CODEC_TYPE_TX3G_TEXT :
            {
                isom_tx3g_entry_t *tx3g = (isom_tx3g_entry_t *)entry->data;
                if( tx3g->ftab )
                    isom_remove_ftab( tx3g->ftab );
                free( tx3g );
                break;
            }
            case QT_CODEC_TYPE_TEXT_TEXT :
            {
                isom_text_entry_t *text = (isom_text_entry_t *)entry->data;
                if( text->font_name )
                    free( text->font_name );
                free( text );
                break;
            }
            default :
                break;
        }
        lsmash_entry_t *next = entry->next;
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
    isom_remove_fullbox( cslg, stbl );
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
        lsmash_remove_list( (stbl->sbgp + i - 1)->list, NULL );
        lsmash_remove_list( (stbl->sgpd + i - 1)->list, NULL );
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
    for( lsmash_entry_t *entry = dref->list->head; entry; )
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
        lsmash_entry_t *next = entry->next;
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

static void isom_remove_hdlr( isom_hdlr_t *hdlr )
{
    if( !hdlr )
        return;
    if( hdlr->name )
        free( hdlr->name );
    free( hdlr );
}

static void isom_remove_gmhd( isom_gmhd_t *gmhd )
{
    if( !gmhd )
        return;
    isom_remove_fullbox( gmin, gmhd );
    if( gmhd->text )
        free( gmhd->text );
    free( gmhd );
}

static void isom_remove_minf( isom_minf_t *minf )
{
    if( !minf )
        return;
    isom_remove_fullbox( vmhd, minf );
    isom_remove_fullbox( smhd, minf );
    isom_remove_fullbox( hmhd, minf );
    isom_remove_fullbox( nmhd, minf );
    isom_remove_gmhd( minf->gmhd );
    isom_remove_hdlr( minf->hdlr );
    isom_remove_dinf( minf->dinf );
    isom_remove_stbl( minf->stbl );
    free( minf );
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
    for( lsmash_entry_t *entry = chpl->list->head; entry; )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        if( data )
        {
            if( data->chapter_name )
                free( data->chapter_name );
            free( data );
        }
        lsmash_entry_t *next = entry->next;
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
    isom_remove_tapt( trak->tapt );
    isom_remove_edts( trak->edts );
    isom_remove_tref( trak->tref );
    isom_remove_mdia( trak->mdia );
    isom_remove_udta( trak->udta );
    if( trak->cache )
    {
        lsmash_remove_list( trak->cache->chunk.pool, isom_delete_sample );
        lsmash_remove_list( trak->cache->roll.pool, NULL );
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
        lsmash_remove_list( moov->trak_list, isom_remove_trak );
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
static int isom_write_tkhd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_tkhd_t *tkhd = trak->tkhd;
    if( !tkhd )
        return -1;
    isom_bs_put_full_header( bs, &tkhd->full_header );
    if( tkhd->full_header.version )
    {
        lsmash_bs_put_be64( bs, tkhd->creation_time );
        lsmash_bs_put_be64( bs, tkhd->modification_time );
        lsmash_bs_put_be32( bs, tkhd->track_ID );
        lsmash_bs_put_be32( bs, tkhd->reserved1 );
        lsmash_bs_put_be64( bs, tkhd->duration );
    }
    else
    {
        lsmash_bs_put_be32( bs, (uint32_t)tkhd->creation_time );
        lsmash_bs_put_be32( bs, (uint32_t)tkhd->modification_time );
        lsmash_bs_put_be32( bs, tkhd->track_ID );
        lsmash_bs_put_be32( bs, tkhd->reserved1 );
        lsmash_bs_put_be32( bs, (uint32_t)tkhd->duration );
    }
    lsmash_bs_put_be32( bs, tkhd->reserved2[0] );
    lsmash_bs_put_be32( bs, tkhd->reserved2[1] );
    lsmash_bs_put_be16( bs, tkhd->layer );
    lsmash_bs_put_be16( bs, tkhd->alternate_group );
    lsmash_bs_put_be16( bs, tkhd->volume );
    lsmash_bs_put_be16( bs, tkhd->reserved3 );
    for( uint32_t i = 0; i < 9; i++ )
        lsmash_bs_put_be32( bs, tkhd->matrix[i] );
    lsmash_bs_put_be32( bs, tkhd->width );
    lsmash_bs_put_be32( bs, tkhd->height );
    return lsmash_bs_write_data( bs );
}

static int isom_write_clef( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_clef_t *clef = trak->tapt->clef;
    if( !clef )
        return 0;
    isom_bs_put_full_header( bs, &clef->full_header );
    lsmash_bs_put_be32( bs, clef->width );
    lsmash_bs_put_be32( bs, clef->height );
    return lsmash_bs_write_data( bs );
}

static int isom_write_prof( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_prof_t *prof = trak->tapt->prof;
    if( !prof )
        return 0;
    isom_bs_put_full_header( bs, &prof->full_header );
    lsmash_bs_put_be32( bs, prof->width );
    lsmash_bs_put_be32( bs, prof->height );
    return lsmash_bs_write_data( bs );
}

static int isom_write_enof( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_enof_t *enof = trak->tapt->enof;
    if( !enof )
        return 0;
    isom_bs_put_full_header( bs, &enof->full_header );
    lsmash_bs_put_be32( bs, enof->width );
    lsmash_bs_put_be32( bs, enof->height );
    return lsmash_bs_write_data( bs );
}

static int isom_write_tapt( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_tapt_t *tapt = trak->tapt;
    if( !tapt )
        return 0;
    isom_bs_put_base_header( bs, &tapt->base_header );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_clef( bs, trak ) ||
        isom_write_prof( bs, trak ) ||
        isom_write_enof( bs, trak ) )
        return -1;
    return 0;
}

static int isom_write_elst( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_elst_t *elst = trak->edts->elst;
    if( !elst )
        return -1;
    if( !elst->list->entry_count )
        return 0;
    isom_bs_put_full_header( bs, &elst->full_header );
    lsmash_bs_put_be32( bs, elst->list->entry_count );
    for( lsmash_entry_t *entry = elst->list->head; entry; entry = entry->next )
    {
        isom_elst_entry_t *data = (isom_elst_entry_t *)entry->data;
        if( !data )
            return -1;
        if( elst->full_header.version )
        {
            lsmash_bs_put_be64( bs, data->segment_duration );
            lsmash_bs_put_be64( bs, data->media_time );
        }
        else
        {
            lsmash_bs_put_be32( bs, (uint32_t)data->segment_duration );
            lsmash_bs_put_be32( bs, (uint32_t)data->media_time );
        }
        lsmash_bs_put_be32( bs, data->media_rate );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_edts( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_edts_t *edts = trak->edts;
    if( !edts )
        return 0;
    isom_bs_put_base_header( bs, &edts->base_header );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    return isom_write_elst( bs, trak );
}

static int isom_write_tref( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_tref_t *tref = trak->tref;
    if( !tref )
        return 0;
    isom_bs_put_base_header( bs, &tref->base_header );
    for( uint32_t i = 0; i < tref->type_count; i++ )
    {
        isom_tref_type_t *type = &tref->type[i];
        isom_bs_put_base_header( bs, &type->base_header );
        for( uint32_t j = 0; j < type->ref_count; j++ )
            lsmash_bs_put_be32( bs, type->track_ID[j] );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_mdhd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    if( !mdhd )
        return -1;
    isom_bs_put_full_header( bs, &mdhd->full_header );
    if( mdhd->full_header.version )
    {
        lsmash_bs_put_be64( bs, mdhd->creation_time );
        lsmash_bs_put_be64( bs, mdhd->modification_time );
        lsmash_bs_put_be32( bs, mdhd->timescale );
        lsmash_bs_put_be64( bs, mdhd->duration );
    }
    else
    {
        lsmash_bs_put_be32( bs, (uint32_t)mdhd->creation_time );
        lsmash_bs_put_be32( bs, (uint32_t)mdhd->modification_time );
        lsmash_bs_put_be32( bs, mdhd->timescale );
        lsmash_bs_put_be32( bs, (uint32_t)mdhd->duration );
    }
    lsmash_bs_put_be16( bs, mdhd->language );
    lsmash_bs_put_be16( bs, mdhd->pre_defined );
    return lsmash_bs_write_data( bs );
}

static int isom_write_hdlr( lsmash_bs_t *bs, isom_trak_entry_t *trak, uint8_t is_media_handler )
{
    isom_hdlr_t *hdlr = is_media_handler ? trak->mdia->hdlr : trak->mdia->minf->hdlr;
    if( !hdlr )
        return 0;
    isom_bs_put_full_header( bs, &hdlr->full_header );
    lsmash_bs_put_be32( bs, hdlr->type );
    lsmash_bs_put_be32( bs, hdlr->subtype );
    for( uint32_t i = 0; i < 3; i++ )
        lsmash_bs_put_be32( bs, hdlr->reserved[i] );
    lsmash_bs_put_bytes( bs, hdlr->name, hdlr->name_length );
    return lsmash_bs_write_data( bs );
}

static int isom_write_vmhd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_vmhd_t *vmhd = trak->mdia->minf->vmhd;
    if( !vmhd )
        return -1;
    isom_bs_put_full_header( bs, &vmhd->full_header );
    lsmash_bs_put_be16( bs, vmhd->graphicsmode );
    for( uint32_t i = 0; i < 3; i++ )
        lsmash_bs_put_be16( bs, vmhd->opcolor[i] );
    return lsmash_bs_write_data( bs );
}

static int isom_write_smhd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_smhd_t *smhd = trak->mdia->minf->smhd;
    if( !smhd )
        return -1;
    isom_bs_put_full_header( bs, &smhd->full_header );
    lsmash_bs_put_be16( bs, smhd->balance );
    lsmash_bs_put_be16( bs, smhd->reserved );
    return lsmash_bs_write_data( bs );
}

static int isom_write_hmhd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_hmhd_t *hmhd = trak->mdia->minf->hmhd;
    if( !hmhd )
        return -1;
    isom_bs_put_full_header( bs, &hmhd->full_header );
    lsmash_bs_put_be16( bs, hmhd->maxPDUsize );
    lsmash_bs_put_be16( bs, hmhd->avgPDUsize );
    lsmash_bs_put_be32( bs, hmhd->maxbitrate );
    lsmash_bs_put_be32( bs, hmhd->avgbitrate );
    lsmash_bs_put_be32( bs, hmhd->reserved );
    return lsmash_bs_write_data( bs );
}

static int isom_write_nmhd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_nmhd_t *nmhd = trak->mdia->minf->nmhd;
    if( !nmhd )
        return -1;
    isom_bs_put_full_header( bs, &nmhd->full_header );
    return lsmash_bs_write_data( bs );
}

static int isom_write_gmin( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_gmin_t *gmin = trak->mdia->minf->gmhd->gmin;
    if( !gmin )
        return -1;
    isom_bs_put_full_header( bs, &gmin->full_header );
    lsmash_bs_put_be16( bs, gmin->graphicsmode );
    for( uint32_t i = 0; i < 3; i++ )
        lsmash_bs_put_be16( bs, gmin->opcolor[i] );
    lsmash_bs_put_be16( bs, gmin->balance );
    lsmash_bs_put_be16( bs, gmin->reserved );
    return lsmash_bs_write_data( bs );
}

static int isom_write_text( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_text_t *text = trak->mdia->minf->gmhd->text;
    if( !text )
        return -1;
    isom_bs_put_base_header( bs, &text->base_header );
    for( uint32_t i = 0; i < 9; i++ )
        lsmash_bs_put_be32( bs, text->matrix[i] );
    return lsmash_bs_write_data( bs );
}

static int isom_write_gmhd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_gmhd_t *gmhd = trak->mdia->minf->gmhd;
    if( !gmhd )
        return -1;
    isom_bs_put_base_header( bs, &gmhd->base_header );
    if( isom_write_gmin( bs, trak ) ||
        isom_write_text( bs, trak ) )
        return -1;
    return 0;
}

static int isom_write_dref( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_dref_t *dref = trak->mdia->minf->dinf->dref;
    if( !dref || !dref->list )
        return -1;
    isom_bs_put_full_header( bs, &dref->full_header );
    lsmash_bs_put_be32( bs, dref->list->entry_count );
    for( lsmash_entry_t *entry = dref->list->head; entry; entry = entry->next )
    {
        isom_dref_entry_t *data = (isom_dref_entry_t *)entry->data;
        if( !data )
            return -1;
        isom_bs_put_full_header( bs, &data->full_header );
        if( data->full_header.type == ISOM_BOX_TYPE_URN )
            lsmash_bs_put_bytes( bs, data->name, data->name_length );
        lsmash_bs_put_bytes( bs, data->location, data->location_length );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_dinf( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_dinf_t *dinf = trak->mdia->minf->dinf;
    if( !dinf )
        return -1;
    isom_bs_put_base_header( bs, &dinf->base_header );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    return isom_write_dref( bs, trak );
}

static void isom_put_pasp( lsmash_bs_t *bs, isom_pasp_t *pasp )
{
    if( !pasp )
        return;
    isom_bs_put_base_header( bs, &pasp->base_header );
    lsmash_bs_put_be32( bs, pasp->hSpacing );
    lsmash_bs_put_be32( bs, pasp->vSpacing );
}

static void isom_put_clap( lsmash_bs_t *bs, isom_clap_t *clap )
{
    if( !clap )
        return;
    isom_bs_put_base_header( bs, &clap->base_header );
    lsmash_bs_put_be32( bs, clap->cleanApertureWidthN );
    lsmash_bs_put_be32( bs, clap->cleanApertureWidthD );
    lsmash_bs_put_be32( bs, clap->cleanApertureHeightN );
    lsmash_bs_put_be32( bs, clap->cleanApertureHeightD );
    lsmash_bs_put_be32( bs, clap->horizOffN );
    lsmash_bs_put_be32( bs, clap->horizOffD );
    lsmash_bs_put_be32( bs, clap->vertOffN );
    lsmash_bs_put_be32( bs, clap->vertOffD );
}

static void isom_put_stsl( lsmash_bs_t *bs, isom_stsl_t *stsl )
{
    if( !stsl )
        return;
    isom_bs_put_full_header( bs, &stsl->full_header );
    lsmash_bs_put_byte( bs, stsl->constraint_flag );
    lsmash_bs_put_byte( bs, stsl->scale_method );
    lsmash_bs_put_be16( bs, stsl->display_center_x );
    lsmash_bs_put_be16( bs, stsl->display_center_y );
}

static int isom_put_ps_entries( lsmash_bs_t *bs, lsmash_entry_list_t *list )
{
    for( lsmash_entry_t *entry = list->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be16( bs, data->parameterSetLength );
        lsmash_bs_put_bytes( bs, data->parameterSetNALUnit, data->parameterSetLength );
    }
    return 0;
}

static int isom_put_avcC( lsmash_bs_t *bs, isom_avcC_t *avcC )
{
    if( !bs || !avcC || !avcC->sequenceParameterSets || !avcC->pictureParameterSets )
        return -1;
    isom_bs_put_base_header( bs, &avcC->base_header );
    lsmash_bs_put_byte( bs, avcC->configurationVersion );
    lsmash_bs_put_byte( bs, avcC->AVCProfileIndication );
    lsmash_bs_put_byte( bs, avcC->profile_compatibility );
    lsmash_bs_put_byte( bs, avcC->AVCLevelIndication );
    lsmash_bs_put_byte( bs, avcC->lengthSizeMinusOne | 0xfc );            /* upper 6-bits are reserved as 111111b */
    lsmash_bs_put_byte( bs, avcC->numOfSequenceParameterSets | 0xe0 );    /* upper 3-bits are reserved as 111b */
    if( isom_put_ps_entries( bs, avcC->sequenceParameterSets ) )
        return -1;
    lsmash_bs_put_byte( bs, avcC->numOfPictureParameterSets );
    if( isom_put_ps_entries( bs, avcC->pictureParameterSets ) )
        return -1;
    if( ISOM_REQUIRES_AVCC_EXTENSION( avcC->AVCProfileIndication ) )
    {
        lsmash_bs_put_byte( bs, avcC->chroma_format | 0xfc );             /* upper 6-bits are reserved as 111111b */
        lsmash_bs_put_byte( bs, avcC->bit_depth_luma_minus8 | 0xf8 );     /* upper 5-bits are reserved as 11111b */
        lsmash_bs_put_byte( bs, avcC->bit_depth_chroma_minus8 | 0xf8 );   /* upper 5-bits are reserved as 11111b */
        lsmash_bs_put_byte( bs, avcC->numOfSequenceParameterSetExt );
        if( isom_put_ps_entries( bs, avcC->sequenceParameterSetExt ) )
            return -1;
    }
    return 0;
}

static void isom_put_btrt( lsmash_bs_t *bs, isom_btrt_t *btrt )
{
    if( !bs || !btrt )
        return;
    isom_bs_put_base_header( bs, &btrt->base_header );
    lsmash_bs_put_be32( bs, btrt->bufferSizeDB );
    lsmash_bs_put_be32( bs, btrt->maxBitrate );
    lsmash_bs_put_be32( bs, btrt->avgBitrate );
}

static int isom_write_esds( lsmash_bs_t *bs, isom_esds_t *esds )
{
    if( !bs || !esds )
        return -1;
    isom_bs_put_full_header( bs, &esds->full_header );
    return mp4sys_write_ES_Descriptor( bs, esds->ES );
}

static int isom_write_frma( lsmash_bs_t *bs, isom_frma_t *frma )
{
    if( !frma )
        return -1;
    isom_bs_put_base_header( bs, &frma->base_header );
    lsmash_bs_put_be32( bs, frma->data_format );
    return lsmash_bs_write_data( bs );
}

static int isom_write_mp4a( lsmash_bs_t *bs, isom_mp4a_t *mp4a )
{
    if( !mp4a )
        return 0;
    isom_bs_put_base_header( bs, &mp4a->base_header );
    lsmash_bs_put_be32( bs, mp4a->unknown );
    return lsmash_bs_write_data( bs );
}

static int isom_write_terminator( lsmash_bs_t *bs, isom_terminator_t *terminator )
{
    if( !terminator )
        return -1;
    isom_bs_put_base_header( bs, &terminator->base_header );
    return lsmash_bs_write_data( bs );
}

static int isom_write_wave( lsmash_bs_t *bs, isom_wave_t *wave )
{
    if( !wave )
        return 0;
    isom_bs_put_base_header( bs, &wave->base_header );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_frma( bs, wave->frma ) ||
        isom_write_mp4a( bs, wave->mp4a ) ||
        isom_write_esds( bs, wave->esds ) )
        return -1;
    return isom_write_terminator( bs, wave->terminator );
}

static int isom_write_avc_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_avc_entry_t *data = (isom_avc_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_base_header( bs, &data->base_header );
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    lsmash_bs_put_be16( bs, data->pre_defined1 );
    lsmash_bs_put_be16( bs, data->reserved1 );
    for( uint32_t j = 0; j < 3; j++ )
        lsmash_bs_put_be32( bs, data->pre_defined2[j] );
    lsmash_bs_put_be16( bs, data->width );
    lsmash_bs_put_be16( bs, data->height );
    lsmash_bs_put_be32( bs, data->horizresolution );
    lsmash_bs_put_be32( bs, data->vertresolution );
    lsmash_bs_put_be32( bs, data->reserved2 );
    lsmash_bs_put_be16( bs, data->frame_count );
    lsmash_bs_put_bytes( bs, data->compressorname, 32 );
    lsmash_bs_put_be16( bs, data->depth );
    lsmash_bs_put_be16( bs, data->pre_defined3 );
    isom_put_clap( bs, data->clap );
    isom_put_pasp( bs, data->pasp );
    isom_put_stsl( bs, data->stsl );
    if( !data->avcC )
        return -1;
    isom_put_avcC( bs, data->avcC );
    if( data->btrt )
        isom_put_btrt( bs, data->btrt );
    return lsmash_bs_write_data( bs );
}

static int isom_write_mp4a_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_mp4a_entry_t *data = (isom_mp4a_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_base_header( bs, &data->base_header );
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    lsmash_bs_put_be16( bs, data->version );
    lsmash_bs_put_be16( bs, data->revision_level );
    lsmash_bs_put_be32( bs, data->vendor );
    lsmash_bs_put_be16( bs, data->channelcount );
    lsmash_bs_put_be16( bs, data->samplesize );
    lsmash_bs_put_be16( bs, data->compression_ID );
    lsmash_bs_put_be16( bs, data->packet_size );
    lsmash_bs_put_be32( bs, data->samplerate );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    return isom_write_esds( bs, data->esds );
}

static int isom_write_audio_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_audio_entry_t *data = (isom_audio_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_base_header( bs, &data->base_header );
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    lsmash_bs_put_be16( bs, data->version );
    lsmash_bs_put_be16( bs, data->revision_level );
    lsmash_bs_put_be32( bs, data->vendor );
    lsmash_bs_put_be16( bs, data->channelcount );
    lsmash_bs_put_be16( bs, data->samplesize );
    lsmash_bs_put_be16( bs, data->compression_ID );
    lsmash_bs_put_be16( bs, data->packet_size );
    lsmash_bs_put_be32( bs, data->samplerate );
    lsmash_bs_put_bytes( bs, data->exdata, data->exdata_length );
    if( data->version == 1 )
    {
        lsmash_bs_put_be32( bs, data->samplesPerPacket );
        lsmash_bs_put_be32( bs, data->bytesPerPacket );
        lsmash_bs_put_be32( bs, data->bytesPerFrame );
        lsmash_bs_put_be32( bs, data->bytesPerSample );
        if( isom_write_wave( bs, data->wave ) )
            return -1;
    }
    return lsmash_bs_write_data( bs );
}

#if 0
static int isom_write_visual_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_visual_entry_t *data = (isom_visual_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_base_header( bs, &data->base_header );
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    lsmash_bs_put_be16( bs, data->pre_defined1 );
    lsmash_bs_put_be16( bs, data->reserved1 );
    for( uint32_t j = 0; j < 3; j++ )
        lsmash_bs_put_be32( bs, data->pre_defined2[j] );
    lsmash_bs_put_be16( bs, data->width );
    lsmash_bs_put_be16( bs, data->height );
    lsmash_bs_put_be32( bs, data->horizresolution );
    lsmash_bs_put_be32( bs, data->vertresolution );
    lsmash_bs_put_be32( bs, data->reserved2 );
    lsmash_bs_put_be16( bs, data->frame_count );
    lsmash_bs_put_bytes( bs, data->compressorname, 32 );
    lsmash_bs_put_be16( bs, data->depth );
    lsmash_bs_put_be16( bs, data->pre_defined3 );
    isom_put_clap( bs, data->clap );
    isom_put_pasp( bs, data->pasp );
    isom_put_stsl( bs, data->stsl );
    if( data->base_header.type == ISOM_CODEC_TYPE_AVC1_VIDEO )
    {
        isom_avc_entry_t *avc = (isom_avc_entry_t *)data;
        if( !avc || !avc->avcC )
            return -1;
        isom_put_avcC( bs, avc->avcC );
        if( avc->btrt )
            isom_put_btrt( bs, avc->btrt );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_hint_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_hint_entry_t *data = (isom_hint_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_base_header( bs, &data->base_header );
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    if( data->data && data->data_length )
        lsmash_bs_put_bytes( bs, data->data, data->data_length );
    return lsmash_bs_write_data( bs );
}

static int isom_write_metadata_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_metadata_entry_t *data = (isom_metadata_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_base_header( bs, &data->base_header );
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    return lsmash_bs_write_data( bs );
}
#endif

static int isom_write_text_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_text_entry_t *data = (isom_text_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_base_header( bs, &data->base_header );
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    lsmash_bs_put_be32( bs, data->displayFlags );
    lsmash_bs_put_be32( bs, data->textJustification );
    for( uint32_t i = 0; i < 3; i++ )
        lsmash_bs_put_be16( bs, data->bgColor[i] );
    lsmash_bs_put_be16( bs, data->top );
    lsmash_bs_put_be16( bs, data->left );
    lsmash_bs_put_be16( bs, data->bottom );
    lsmash_bs_put_be16( bs, data->right );
    lsmash_bs_put_be32( bs, data->scrpStartChar );
    lsmash_bs_put_be16( bs, data->scrpHeight );
    lsmash_bs_put_be16( bs, data->scrpAscent );
    lsmash_bs_put_be16( bs, data->scrpFont );
    lsmash_bs_put_be16( bs, data->scrpFace );
    lsmash_bs_put_be16( bs, data->scrpSize );
    for( uint32_t i = 0; i < 3; i++ )
        lsmash_bs_put_be16( bs, data->scrpColor[i] );
    lsmash_bs_put_byte( bs, data->font_name_length );
    if( data->font_name && data->font_name_length )
        lsmash_bs_put_bytes( bs, data->font_name, data->font_name_length );
    return lsmash_bs_write_data( bs );
}

static int isom_put_ftab( lsmash_bs_t *bs, isom_ftab_t *ftab )
{
    if( !ftab || !ftab->list )
        return -1;
    isom_bs_put_base_header( bs, &ftab->base_header );
    lsmash_bs_put_be16( bs, ftab->list->entry_count );
    for( lsmash_entry_t *entry = ftab->list->head; entry; entry = entry->next )
    {
        isom_font_record_t *data = (isom_font_record_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be16( bs, data->font_ID );
        lsmash_bs_put_byte( bs, data->font_name_length );
        if( data->font_name && data->font_name_length )
            lsmash_bs_put_bytes( bs, data->font_name, data->font_name_length );
    }
    return 0;
}

static int isom_write_tx3g_entry( lsmash_bs_t *bs, lsmash_entry_t *entry )
{
    isom_tx3g_entry_t *data = (isom_tx3g_entry_t *)entry->data;
    if( !data )
        return -1;
    isom_bs_put_base_header( bs, &data->base_header );
    lsmash_bs_put_bytes( bs, data->reserved, 6 );
    lsmash_bs_put_be16( bs, data->data_reference_index );
    lsmash_bs_put_be32( bs, data->displayFlags );
    lsmash_bs_put_byte( bs, data->horizontal_justification );
    lsmash_bs_put_byte( bs, data->vertical_justification );
    for( uint32_t i = 0; i < 4; i++ )
        lsmash_bs_put_byte( bs, data->background_color_rgba[i] );
    lsmash_bs_put_be16( bs, data->top );
    lsmash_bs_put_be16( bs, data->left );
    lsmash_bs_put_be16( bs, data->bottom );
    lsmash_bs_put_be16( bs, data->right );
    lsmash_bs_put_be16( bs, data->startChar );
    lsmash_bs_put_be16( bs, data->endChar );
    lsmash_bs_put_be16( bs, data->font_ID );
    lsmash_bs_put_byte( bs, data->face_style_flags );
    lsmash_bs_put_byte( bs, data->font_size );
    for( uint32_t i = 0; i < 4; i++ )
        lsmash_bs_put_byte( bs, data->text_color_rgba[i] );
    isom_put_ftab( bs, data->ftab );
    return lsmash_bs_write_data( bs );
}

static int isom_write_stsd( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stsd_t *stsd = trak->mdia->minf->stbl->stsd;
    if( !stsd || !stsd->list || !stsd->list->head )
        return -1;
    isom_bs_put_full_header( bs, &stsd->full_header );
    lsmash_bs_put_be32( bs, stsd->list->entry_count );
    int ret = -1;
    for( lsmash_entry_t *entry = stsd->list->head; entry; entry = entry->next )
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
                ret = isom_write_avc_entry( bs, entry );
                break;
            case ISOM_CODEC_TYPE_MP4A_AUDIO :
                if( !((isom_audio_entry_t *)sample)->version )
                    ret = isom_write_mp4a_entry( bs, entry );
                else
                    ret = isom_write_audio_entry( bs, entry );
                break;
#if 0
            case ISOM_CODEC_TYPE_DRAC_VIDEO :
            case ISOM_CODEC_TYPE_ENCV_VIDEO :
            case ISOM_CODEC_TYPE_MJP2_VIDEO :
            case ISOM_CODEC_TYPE_S263_VIDEO :
            case ISOM_CODEC_TYPE_VC_1_VIDEO :
                ret = isom_write_visual_entry( bs, entry );
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
                ret = isom_write_audio_entry( bs, entry );
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
                ret = isom_write_hint_entry( bs, entry );
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
                ret = isom_write_metadata_entry( bs, entry );
                break;
#endif
            case ISOM_CODEC_TYPE_TX3G_TEXT :
                ret = isom_write_tx3g_entry( bs, entry );
                break;
            case QT_CODEC_TYPE_TEXT_TEXT :
                ret = isom_write_text_entry( bs, entry );
                break;
            default :
                break;
        }
        if( ret )
            break;
    }
    return ret;
}

static int isom_write_stts( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stts_t *stts = trak->mdia->minf->stbl->stts;
    if( !stts || !stts->list )
        return -1;
    isom_bs_put_full_header( bs, &stts->full_header );
    lsmash_bs_put_be32( bs, stts->list->entry_count );
    for( lsmash_entry_t *entry = stts->list->head; entry; entry = entry->next )
    {
        isom_stts_entry_t *data = (isom_stts_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be32( bs, data->sample_count );
        lsmash_bs_put_be32( bs, data->sample_delta );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_ctts( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_ctts_t *ctts = trak->mdia->minf->stbl->ctts;
    if( !ctts )
        return 0;
    if( !ctts->list )
        return -1;
    isom_bs_put_full_header( bs, &ctts->full_header );
    lsmash_bs_put_be32( bs, ctts->list->entry_count );
    for( lsmash_entry_t *entry = ctts->list->head; entry; entry = entry->next )
    {
        isom_ctts_entry_t *data = (isom_ctts_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be32( bs, data->sample_count );
        lsmash_bs_put_be32( bs, data->sample_offset );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_cslg( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_cslg_t *cslg = trak->mdia->minf->stbl->cslg;
    if( !cslg )
        return 0;
    isom_bs_put_full_header( bs, &cslg->full_header );
    lsmash_bs_put_be32( bs, cslg->compositionToDTSShift );
    lsmash_bs_put_be32( bs, cslg->leastDecodeToDisplayDelta );
    lsmash_bs_put_be32( bs, cslg->greatestDecodeToDisplayDelta );
    lsmash_bs_put_be32( bs, cslg->compositionStartTime );
    lsmash_bs_put_be32( bs, cslg->compositionEndTime );
    return lsmash_bs_write_data( bs );
}

static int isom_write_stsz( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stsz_t *stsz = trak->mdia->minf->stbl->stsz;
    if( !stsz )
        return -1;
    isom_bs_put_full_header( bs, &stsz->full_header );
    lsmash_bs_put_be32( bs, stsz->sample_size );
    lsmash_bs_put_be32( bs, stsz->sample_count );
    if( stsz->sample_size == 0 && stsz->list )
        for( lsmash_entry_t *entry = stsz->list->head; entry; entry = entry->next )
        {
            isom_stsz_entry_t *data = (isom_stsz_entry_t *)entry->data;
            if( !data )
                return -1;
            lsmash_bs_put_be32( bs, data->entry_size );
        }
    return lsmash_bs_write_data( bs );
}

static int isom_write_stss( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stss_t *stss = trak->mdia->minf->stbl->stss;
    if( !stss )
        return 0;   /* If the sync sample box is not present, every sample is a random access point. */
    if( !stss->list )
        return -1;
    isom_bs_put_full_header( bs, &stss->full_header );
    lsmash_bs_put_be32( bs, stss->list->entry_count );
    for( lsmash_entry_t *entry = stss->list->head; entry; entry = entry->next )
    {
        isom_stss_entry_t *data = (isom_stss_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be32( bs, data->sample_number );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_stps( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stps_t *stps = trak->mdia->minf->stbl->stps;
    if( !stps )
        return 0;
    if( !stps->list )
        return -1;
    isom_bs_put_full_header( bs, &stps->full_header );
    lsmash_bs_put_be32( bs, stps->list->entry_count );
    for( lsmash_entry_t *entry = stps->list->head; entry; entry = entry->next )
    {
        isom_stps_entry_t *data = (isom_stps_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be32( bs, data->sample_number );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_sdtp( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_sdtp_t *sdtp = trak->mdia->minf->stbl->sdtp;
    if( !sdtp )
        return 0;
    if( !sdtp->list )
        return -1;
    isom_bs_put_full_header( bs, &sdtp->full_header );
    for( lsmash_entry_t *entry = sdtp->list->head; entry; entry = entry->next )
    {
        isom_sdtp_entry_t *data = (isom_sdtp_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_byte( bs, (data->is_leading<<6) |
                              (data->sample_depends_on<<4) |
                              (data->sample_is_depended_on<<2) |
                               data->sample_has_redundancy );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_stsc( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stsc_t *stsc = trak->mdia->minf->stbl->stsc;
    if( !stsc || !stsc->list )
        return -1;
    isom_bs_put_full_header( bs, &stsc->full_header );
    lsmash_bs_put_be32( bs, stsc->list->entry_count );
    for( lsmash_entry_t *entry = stsc->list->head; entry; entry = entry->next )
    {
        isom_stsc_entry_t *data = (isom_stsc_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be32( bs, data->first_chunk );
        lsmash_bs_put_be32( bs, data->samples_per_chunk );
        lsmash_bs_put_be32( bs, data->sample_description_index );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_co64( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stco_t *co64 = trak->mdia->minf->stbl->stco;
    if( !co64 || !co64->list )
        return -1;
    isom_bs_put_full_header( bs, &co64->full_header );
    lsmash_bs_put_be32( bs, co64->list->entry_count );
    for( lsmash_entry_t *entry = co64->list->head; entry; entry = entry->next )
    {
        isom_co64_entry_t *data = (isom_co64_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be64( bs, data->chunk_offset );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_stco( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stco_t *stco = trak->mdia->minf->stbl->stco;
    if( !stco || !stco->list )
        return -1;
    if( stco->large_presentation )
        return isom_write_co64( bs, trak );
    isom_bs_put_full_header( bs, &stco->full_header );
    lsmash_bs_put_be32( bs, stco->list->entry_count );
    for( lsmash_entry_t *entry = stco->list->head; entry; entry = entry->next )
    {
        isom_stco_entry_t *data = (isom_stco_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be32( bs, data->chunk_offset );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_sbgp( lsmash_bs_t *bs, isom_trak_entry_t *trak, uint32_t grouping_number )
{
    isom_sbgp_t *sbgp = trak->mdia->minf->stbl->sbgp + grouping_number - 1;
    if( !sbgp || !sbgp->list )
        return -1;
    isom_bs_put_full_header( bs, &sbgp->full_header );
    lsmash_bs_put_be32( bs, sbgp->grouping_type );
    lsmash_bs_put_be32( bs, sbgp->list->entry_count );
    for( lsmash_entry_t *entry = sbgp->list->head; entry; entry = entry->next )
    {
        isom_sbgp_entry_t *data = (isom_sbgp_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be32( bs, data->sample_count );
        lsmash_bs_put_be32( bs, data->group_description_index );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_sgpd( lsmash_bs_t *bs, isom_trak_entry_t *trak, uint32_t grouping_number )
{
    isom_sgpd_t *sgpd = trak->mdia->minf->stbl->sgpd + grouping_number - 1;
    if( !sgpd || !sgpd->list )
        return -1;
    isom_bs_put_full_header( bs, &sgpd->full_header );
    lsmash_bs_put_be32( bs, sgpd->grouping_type );
    if( sgpd->full_header.version == 1 )
        lsmash_bs_put_be32( bs, sgpd->default_length );
    lsmash_bs_put_be32( bs, sgpd->list->entry_count );
    for( lsmash_entry_t *entry = sgpd->list->head; entry; entry = entry->next )
    {
        if( !entry->data )
            return -1;
        switch( sgpd->grouping_type )
        {
            case ISOM_GROUP_TYPE_ROLL :
            {
                lsmash_bs_put_be16( bs, ((isom_roll_entry_t *)entry->data)->roll_distance );
                break;
            }
            default :
                /* We don't consider other grouping types currently. */
                // if( sgpd->full_header.version == 1 && !sgpd->default_length )
                //     lsmash_bs_put_be32( bs, ((isom_sgpd_entry_t *)entry->data)->description_length );
                break;
        }
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_stbl( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    if( !stbl )
        return -1;
    isom_bs_put_base_header( bs, &stbl->base_header );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_stsd( bs, trak ) ||
        isom_write_stts( bs, trak ) ||
        isom_write_ctts( bs, trak ) ||
        isom_write_cslg( bs, trak ) ||
        isom_write_stss( bs, trak ) ||
        isom_write_stps( bs, trak ) ||
        isom_write_sdtp( bs, trak ) ||
        isom_write_stsc( bs, trak ) ||
        isom_write_stsz( bs, trak ) ||
        isom_write_stco( bs, trak ) )
        return -1;
    for( uint32_t i = 1; i <= trak->mdia->minf->stbl->grouping_count; i++ )
    {
        if( isom_write_sgpd( bs, trak, i ) ||
            isom_write_sbgp( bs, trak, i ) )
            return -1;
    }
    return 0;
}

static int isom_write_minf( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_minf_t *minf = trak->mdia->minf;
    if( !minf )
        return -1;
    isom_bs_put_base_header( bs, &minf->base_header );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( (minf->vmhd && isom_write_vmhd( bs, trak )) ||
        (minf->smhd && isom_write_smhd( bs, trak )) ||
        (minf->hmhd && isom_write_hmhd( bs, trak )) ||
        (minf->nmhd && isom_write_nmhd( bs, trak )) ||
        (minf->gmhd && isom_write_gmhd( bs, trak )) )
        return -1;
    if( isom_write_hdlr( bs, trak, 0 ) ||
        isom_write_dinf( bs, trak ) ||
        isom_write_stbl( bs, trak ) )
        return -1;
    return 0;
}

static int isom_write_mdia( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    isom_mdia_t *mdia = trak->mdia;
    if( !mdia )
        return -1;
    isom_bs_put_base_header( bs, &mdia->base_header );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_mdhd( bs, trak ) ||
        isom_write_hdlr( bs, trak, 1 ) ||
        isom_write_minf( bs, trak ) )
        return -1;
    return 0;
}

static int isom_write_chpl( lsmash_bs_t *bs, isom_chpl_t *chpl )
{
    if( !chpl )
        return 0;
    if( !chpl->list )
        return -1;
    isom_bs_put_full_header( bs, &chpl->full_header );
    lsmash_bs_put_byte( bs, chpl->reserved );
    lsmash_bs_put_be32( bs, chpl->list->entry_count );
    for( lsmash_entry_t *entry = chpl->list->head; entry; entry = entry->next )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        if( !data )
            return -1;
        lsmash_bs_put_be64( bs, data->start_time );
        lsmash_bs_put_byte( bs, data->chapter_name_length );
        lsmash_bs_put_bytes( bs, data->chapter_name, data->chapter_name_length );
    }
    return lsmash_bs_write_data( bs );
}

static int isom_write_udta( lsmash_bs_t *bs, isom_moov_t *moov, isom_trak_entry_t *trak )
{
    /* Setting non-NULL pointer to trak means trak->udta data will be written in stream.
     * If trak is set by NULL while moov is set by non-NULL pointer, moov->udta data will be written in stream. */
    isom_udta_t *udta = trak ? trak->udta : moov ? moov->udta : NULL;
    if( !udta )
        return 0;
    isom_bs_put_base_header( bs, &udta->base_header );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( moov && isom_write_chpl( bs, udta->chpl ) )
        return -1;
    return 0;
}

static int isom_write_trak( lsmash_bs_t *bs, isom_trak_entry_t *trak )
{
    if( !trak )
        return -1;
    isom_bs_put_base_header( bs, &trak->base_header );
    if( lsmash_bs_write_data( bs ) )
        return -1;
    if( isom_write_tkhd( bs, trak ) ||
        isom_write_tapt( bs, trak ) ||
        isom_write_edts( bs, trak ) ||
        isom_write_tref( bs, trak ) ||
        isom_write_mdia( bs, trak ) ||
        isom_write_udta( bs, NULL, trak ) )
        return -1;
    return 0;
}

static int isom_write_iods( isom_root_t *root )
{
    if( !root || !root->moov )
        return -1;
    if( !root->moov->iods )
        return 0;
    isom_iods_t *iods = root->moov->iods;
    lsmash_bs_t *bs = root->bs;
    isom_bs_put_full_header( bs, &iods->full_header );
    return mp4sys_write_ObjectDescriptor( bs, iods->OD );
}

static int isom_write_mvhd( isom_root_t *root )
{
    if( !root || !root->moov || !root->moov->mvhd )
        return -1;
    isom_mvhd_t *mvhd = root->moov->mvhd;
    lsmash_bs_t *bs = root->bs;
    isom_bs_put_full_header( bs, &mvhd->full_header );
    if( mvhd->full_header.version )
    {
        lsmash_bs_put_be64( bs, mvhd->creation_time );
        lsmash_bs_put_be64( bs, mvhd->modification_time );
        lsmash_bs_put_be32( bs, mvhd->timescale );
        lsmash_bs_put_be64( bs, mvhd->duration );
    }
    else
    {
        lsmash_bs_put_be32( bs, (uint32_t)mvhd->creation_time );
        lsmash_bs_put_be32( bs, (uint32_t)mvhd->modification_time );
        lsmash_bs_put_be32( bs, mvhd->timescale );
        lsmash_bs_put_be32( bs, (uint32_t)mvhd->duration );
    }
    lsmash_bs_put_be32( bs, mvhd->rate );
    lsmash_bs_put_be16( bs, mvhd->volume );
    lsmash_bs_put_bytes( bs, mvhd->reserved, 10 );
    for( uint32_t i = 0; i < 9; i++ )
        lsmash_bs_put_be32( bs, mvhd->matrix[i] );
    for( uint32_t i = 0; i < 6; i++ )
        lsmash_bs_put_be32( bs, mvhd->pre_defined[i] );
    lsmash_bs_put_be32( bs, mvhd->next_track_ID );
    return lsmash_bs_write_data( bs );
}

static int isom_bs_write_largesize_placeholder( lsmash_bs_t *bs )
{
    lsmash_bs_put_be32( bs, ISOM_DEFAULT_BOX_HEADER_SIZE );
    lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_FREE );
    return lsmash_bs_write_data( bs );
}

static int isom_write_mdat_header( isom_root_t *root )
{
    if( !root || !root->bs || !root->mdat )
        return -1;
    isom_mdat_t *mdat = root->mdat;
    lsmash_bs_t *bs = root->bs;
    mdat->placeholder_pos = ftell( bs->stream );
    if( isom_bs_write_largesize_placeholder( bs ) )
        return -1;
    mdat->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE;
    isom_bs_put_base_header( bs, &mdat->base_header );
    return lsmash_bs_write_data( bs );
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
    lsmash_entry_t *entry;
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
    lsmash_entry_t *entry;
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
    if( !trak || !trak->root || !trak->mdia || !trak->mdia->mdhd || !trak->mdia->minf || !trak->mdia->minf->stbl ||
        !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list || trak->mdia->minf->stbl->stts->list->entry_count == 0 )
        return -1;
    isom_mdhd_t *mdhd = trak->mdia->mdhd;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_stts_t *stts = stbl->stts;
    isom_ctts_t *ctts = stbl->ctts;
    isom_cslg_t *cslg = stbl->cslg;
    mdhd->duration = 0;
    uint32_t sample_count = isom_get_sample_count( trak );
    if( sample_count == 0 )
        return -1;
    /* Now we have at least 1 sample, so do stts_entry. */
    lsmash_entry_t *last_stts = stts->list->tail;
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
            if( lsmash_remove_entry( stts->list, stts->list->entry_count ) )
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
        uint32_t max_offset = 0, min_offset = UINT32_MAX;
        uint32_t j, k;
        lsmash_entry_t *stts_entry = stts->list->head;
        lsmash_entry_t *ctts_entry = ctts->list->head;
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
            max_offset = ISOM_MAX( max_offset, ctts_data->sample_offset );
            min_offset = ISOM_MIN( min_offset, ctts_data->sample_offset );
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
        /* Explicit composition information and DTS shifting  */
        if( cslg || trak->root->qt_compatible )
        {
            if( (min_offset <= UINT32_MAX) && (max_offset <= UINT32_MAX) &&
                (min_cts <= UINT32_MAX) && (2 * max_cts - max2_cts <= UINT32_MAX) )
            {
                if( !cslg )
                {
                    if( isom_add_cslg( trak->mdia->minf->stbl ) )
                        return -1;
                    cslg = stbl->cslg;
                }
                cslg->compositionToDTSShift = 0;    /* We don't consider DTS shifting at present. */
                cslg->leastDecodeToDisplayDelta = min_offset;
                cslg->greatestDecodeToDisplayDelta = max_offset;
                cslg->compositionStartTime = min_cts;
                cslg->compositionEndTime = 2 * max_cts - max2_cts;
            }
            else
            {
                if( cslg )
                    free( cslg );
                stbl->cslg = NULL;
            }
        }
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
    for( lsmash_entry_t *entry = moov->trak_list->head; entry; entry = entry->next )
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
        for( lsmash_entry_t *entry = trak->edts->elst->list->head; entry; entry = entry->next )
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

static int isom_add_size( isom_trak_entry_t *trak, uint32_t sample_size )
{
    if( !sample_size )
        return -1;
    return isom_add_stsz_entry( trak->mdia->minf->stbl, sample_size );
}

static int isom_add_sync_point( isom_trak_entry_t *trak, uint32_t sample_number, isom_sample_property_t *prop )
{
    if( !prop->sync_point ) /* no null check for prop */
        return 0;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    if( !stbl->stss && isom_add_stss( stbl ) )
        return -1;
    return isom_add_stss_entry( stbl, sample_number );
}

static int isom_add_partial_sync( isom_trak_entry_t *trak, uint32_t sample_number, isom_sample_property_t *prop )
{
    if( !trak->root->qt_compatible )
        return 0;
    if( !prop->partial_sync ) /* no null check for prop */
        return 0;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    if( !stbl->stps && isom_add_stps( stbl ) )
        return -1;
    return isom_add_stps_entry( stbl, sample_number );
}

static int isom_add_dependency_type( isom_trak_entry_t *trak, isom_sample_property_t *prop )
{
    if( !trak->root->qt_compatible && !trak->root->avc_extensions )
        return 0;
    uint8_t avc_extensions = trak->root->avc_extensions;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    if( stbl->sdtp )
        return isom_add_sdtp_entry( stbl, prop, avc_extensions );
    if( !prop->allow_earlier && !prop->leading && !prop->independent && !prop->disposable && !prop->redundant )  /* no null check for prop */
        return 0;
    if( isom_add_sdtp( stbl ) )
        return -1;
    uint32_t count = isom_get_sample_count( trak );
    /* fill past samples with ISOM_SAMPLE_*_UNKNOWN */
    isom_sample_property_t null_prop = { 0 };
    for( uint32_t i = 1; i < count; i++ )
        if( isom_add_sdtp_entry( stbl, &null_prop, avc_extensions ) )
            return -1;
    return isom_add_sdtp_entry( stbl, prop, avc_extensions );
}

static int isom_group_roll_recovery( isom_trak_entry_t *trak, isom_sample_property_t *prop )
{
    if( !trak->root->avc_extensions )
        return 0;
    isom_stbl_t *stbl = trak->mdia->minf->stbl;
    isom_sbgp_t *sbgp = isom_get_sample_to_group( stbl, ISOM_GROUP_TYPE_ROLL );
    isom_sgpd_t *sgpd = isom_get_sample_group_description( stbl, ISOM_GROUP_TYPE_ROLL );
    if( !sbgp || !sgpd )
        return 0;
    lsmash_entry_list_t *pool = trak->cache->roll.pool;
    if( !pool )
    {
        pool = lsmash_create_entry_list();
        if( !pool )
            return -1;
        trak->cache->roll.pool = pool;
    }
    isom_roll_group_t *group = (isom_roll_group_t *)lsmash_get_entry_data( pool, pool->entry_count );
    uint32_t sample_count = isom_get_sample_count( trak );
    if( !pool->entry_count || prop->recovery.start_point )
    {
        if( pool->entry_count )
            group->delimited = 1;
        /* Create a new group. This group is not 'roll' yet, so we set 0 on its group_description_index. */
        group = malloc( sizeof(isom_roll_group_t) );
        if( !group )
            return -1;
        memset( group, 0, sizeof(isom_roll_group_t) );
        group->first_sample = sample_count;
        group->recovery_point = prop->recovery.complete;
        group->sample_to_group = isom_add_sbgp_entry( sbgp, pool->entry_count ? 1 : sample_count, 0 );
        if( !group->sample_to_group || lsmash_add_entry( pool, group ) )
        {
            free( group );
            return -1;
        }
    }
    else
        ++ group->sample_to_group->sample_count;
    if( prop->sync_point )
    {
        /* All recoveries are completed if encountered a sync sample. */
        for( lsmash_entry_t *entry = pool->head; entry; entry = entry->next )
        {
            group = (isom_roll_group_t *)entry->data;
            if( !group )
                return -1;
            group->described = 1;
        }
        return 0;
    }
    for( lsmash_entry_t *entry = pool->head; entry; entry = entry->next )
    {
        group = (isom_roll_group_t *)entry->data;
        if( !group )
            return -1;
        if( group->described )
            continue;
        /* Be careful of consecutive undecodable leading samples after the partial sync sample (i.e. Open-GOP I-picture).
         * These samples are not able to decode correctly from the recovery point specified in display order.
         * In this case, therefore, roll_distance will be number of consecutive undecodable leading samples after the partial sync sample plus one. */
        if( group->roll_recovery )
        {
            ++ group->roll_recovery->roll_distance;
            if( prop->leading != ISOM_SAMPLE_IS_UNDECODABLE_LEADING )
            {
                /* The intra picture that doesn't lead any pictures that depend on former groups is substantially an instantaneous decoding picture.
                 * Therefore, roll_distance of the group that includes this picture as the first sample is zero.
                 * However, this means this group cannot have random access point, since roll_distance = 0 must not be used. This is not preferable.
                 * So we treat this group as 'roll' and pick roll_distance = 1 though this treatment is over-estimation. */
                group->sample_to_group->group_description_index = sgpd->list->entry_count;
                group->described = 1;
            }
        }
        else if( prop->recovery.identifier == group->recovery_point )
        {
            int16_t distance = sample_count - group->first_sample;
            group->roll_recovery = isom_add_roll_group_entry( sgpd, distance );
            if( !group->roll_recovery )
                return -1;
            group->described = distance || !(prop->independent || (prop->leading == ISOM_SAMPLE_IS_UNDECODABLE_LEADING));
            if( distance )
            {
                group->sample_to_group->group_description_index = sgpd->list->entry_count;
                /* All groups before the current group are described. */
                lsmash_entry_t *current = entry;
                for( entry = pool->head; entry != current; entry = entry->next )
                {
                    group = (isom_roll_group_t *)entry->data;
                    if( !group )
                        return -1;
                    group->described = 1;
                }
            }
            break;      /* Avoid evaluating groups, in the pool, having the same identifier for recovery point again. */
        }
    }
    for( lsmash_entry_t *entry = pool->head; entry; entry = pool->head )
    {
        group = (isom_roll_group_t *)entry->data;
        if( !group )
            return -1;
        if( !group->delimited || !group->described )
            break;
        if( lsmash_remove_entry_direct( pool, entry ) )
            return -1;
    }
    return 0;
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
        current->pool = lsmash_create_entry_list();
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
    lsmash_bs_put_bytes( root->bs, sample->data, sample->length );
    if( lsmash_bs_write_data( root->bs ) )
        return -1;
    root->mdat->base_header.size += sample->length;
    return 0;
}

static int isom_write_pooled_samples( isom_trak_entry_t *trak, lsmash_entry_list_t *pool )
{
    if( !trak->root )
        return -1;
    for( lsmash_entry_t *entry = pool->head; entry; entry = entry->next )
    {
        isom_sample_t *data = (isom_sample_t *)entry->data;
        if( !data || !data->data )
            return -1;
        /* Add a sample_size and increment sample_count. */
        if( isom_add_size( trak, data->length ) )
            return -1;
        /* Add a decoding timestamp and a composition timestamp. */
        if( isom_add_timestamp( trak, data->dts, data->cts ) )
            return -1;
        /* Add a sync point if needed. */
        if( isom_add_sync_point( trak, isom_get_sample_count( trak ), &data->prop ) )
            return -1;
        /* Add a partial sync point if needed. */
        if( isom_add_partial_sync( trak, isom_get_sample_count( trak ), &data->prop ) )
            return -1;
        /* Add leading, independent, disposable and redundant information if needed. */
        if( isom_add_dependency_type( trak, &data->prop ) )
            return -1;
        /* Group samples into roll recovery type if needed. */
        if( isom_group_roll_recovery( trak, &data->prop ) )
            return -1;
        if( isom_write_sample_data( trak->root, data ) )
            return -1;
    }
    lsmash_remove_entries( pool, isom_delete_sample );
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
    isom_sgpd_t *sgpd = isom_get_sample_group_description( stbl, ISOM_GROUP_TYPE_ROLL );
    if( !sgpd )
        return 0;
    for( lsmash_entry_t *entry = trak->cache->roll.pool->head; entry; entry = entry->next )
    {
        isom_roll_group_t *group = (isom_roll_group_t *)entry->data;
        if( !group )
            return -1;
        if( group->described || !group->roll_recovery )
            continue;
        /* roll_distance == 0 must not be used. */
        if( !group->roll_recovery->roll_distance )
            lsmash_remove_entry( sgpd->list, sgpd->list->entry_count );
        else
            group->sample_to_group->group_description_index = sgpd->list->entry_count;
        group->described = 1;
    }
    return 0;
}

int isom_set_avc_config( isom_root_t *root, uint32_t track_ID, uint32_t entry_number,
    uint8_t configurationVersion, uint8_t AVCProfileIndication, uint8_t profile_compatibility, uint8_t AVCLevelIndication, uint8_t lengthSizeMinusOne,
    uint8_t chroma_format, uint8_t bit_depth_luma_minus8, uint8_t bit_depth_chroma_minus8 )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_avc_entry_t *data = (isom_avc_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
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
    isom_sample_entry_t *sample_entry = (isom_sample_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
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
    lsmash_entry_t *stts_entry = trak->mdia->minf->stbl->stts->list->head;
    lsmash_entry_t *stsz_entry = trak->mdia->minf->stbl->stsz->list ? trak->mdia->minf->stbl->stsz->list->head : NULL;
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
            if( ((isom_audio_entry_t *)sample_entry)->version )
                break;
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

static int isom_check_compatibility( isom_root_t *root )
{
    if( !root )
        return -1;
    /* Check brand to decide mandatory boxes. */
    if( !root->ftyp || !root->ftyp->brand_count )
    {
        /* We assume this file is not a QuickTime but MP4 version 1 format file. */
        root->mp4_version1 = 1;
        return 0;
    }
    for( uint32_t i = 0; i < root->ftyp->brand_count; i++ )
    {
        switch( root->ftyp->compatible_brands[i] )
        {
            case ISOM_BRAND_TYPE_QT :
                root->qt_compatible = 1;
                break;
            case ISOM_BRAND_TYPE_MP41 :
                root->mp4_version1 = 1;
                break;
            case ISOM_BRAND_TYPE_MP42 :
                root->mp4_version2 = 1;
                break;
            case ISOM_BRAND_TYPE_AVC1 :
            case ISOM_BRAND_TYPE_ISO2 :
            case ISOM_BRAND_TYPE_ISO3 :
            case ISOM_BRAND_TYPE_ISO4 :
                root->avc_extensions = 1;
                break;
            case ISOM_BRAND_TYPE_3GP4 :
                root->max_3gpp_version = ISOM_MAX( root->max_3gpp_version, 4 );
                break;
            case ISOM_BRAND_TYPE_3GP5 :
                root->max_3gpp_version = ISOM_MAX( root->max_3gpp_version, 5 );
                break;
            case ISOM_BRAND_TYPE_3GE6 :
            case ISOM_BRAND_TYPE_3GG6 :
            case ISOM_BRAND_TYPE_3GP6 :
            case ISOM_BRAND_TYPE_3GR6 :
            case ISOM_BRAND_TYPE_3GS6 :
                root->max_3gpp_version = ISOM_MAX( root->max_3gpp_version, 6 );
                break;
        }
    }
    root->isom_compatible = !root->qt_compatible || root->mp4_version1 || root->mp4_version2 || root->max_3gpp_version;
    return 0;
}

static int isom_check_mandatory_boxes( isom_root_t *root )
{
    if( !root )
        return -1;
    if( !root->moov || !root->moov->mvhd )
        return -1;
    if( root->moov->trak_list )
        for( lsmash_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
        {
            isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
            if( !trak->tkhd || !trak->mdia )
                return -1;
            if( !trak->mdia->mdhd || !trak->mdia->hdlr || !trak->mdia->minf )
                return -1;
            if( (root->qt_compatible && !trak->mdia->minf->hdlr) || !trak->mdia->minf->dinf || !trak->mdia->minf->dinf->dref )
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

static uint64_t isom_update_clef_size( isom_clef_t *clef )
{
    if( !clef )
        return 0;
    clef->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 8;
    CHECK_LARGESIZE( clef->full_header.size );
    return clef->full_header.size;
}

static uint64_t isom_update_prof_size( isom_prof_t *prof )
{
    if( !prof )
        return 0;
    prof->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 8;
    CHECK_LARGESIZE( prof->full_header.size );
    return prof->full_header.size;
}

static uint64_t isom_update_enof_size( isom_enof_t *enof )
{
    if( !enof )
        return 0;
    enof->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 8;
    CHECK_LARGESIZE( enof->full_header.size );
    return enof->full_header.size;
}

static uint64_t isom_update_tapt_size( isom_tapt_t *tapt )
{
    if( !tapt )
        return 0;
    tapt->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_clef_size( tapt->clef )
        + isom_update_prof_size( tapt->prof )
        + isom_update_enof_size( tapt->enof );
    CHECK_LARGESIZE( tapt->base_header.size );
    return tapt->base_header.size;
}

static uint64_t isom_update_elst_size( isom_elst_t *elst )
{
    if( !elst || !elst->list )
        return 0;
    uint32_t i = 0;
    elst->full_header.version = 0;
    for( lsmash_entry_t *entry = elst->list->head; entry; entry = entry->next, i++ )
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

static uint64_t isom_update_tref_size( isom_tref_t *tref )
{
    if( !tref )
        return 0;
    tref->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE;
    for( uint32_t i = 0; i < tref->type_count; i++ )
    {
        isom_tref_type_t *type = &tref->type[i];
        type->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + (uint64_t)type->ref_count * 4;
        CHECK_LARGESIZE( type->base_header.size );
        tref->base_header.size += type->base_header.size;
    }
    CHECK_LARGESIZE( tref->base_header.size );
    return tref->base_header.size;
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
        for( lsmash_entry_t *entry = dref->list->head; entry; entry = entry->next )
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

static uint64_t isom_update_gmin_size( isom_gmin_t *gmin )
{
    if( !gmin )
        return 0;
    gmin->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 12;
    CHECK_LARGESIZE( gmin->full_header.size );
    return gmin->full_header.size;
}

static uint64_t isom_update_text_size( isom_text_t *text )
{
    if( !text )
        return 0;
    text->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 36;
    CHECK_LARGESIZE( text->base_header.size );
    return text->base_header.size;
}

static uint64_t isom_update_gmhd_size( isom_gmhd_t *gmhd )
{
    if( !gmhd )
        return 0;
    gmhd->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_gmin_size( gmhd->gmin )
        + isom_update_text_size( gmhd->text );
    CHECK_LARGESIZE( gmhd->base_header.size );
    return gmhd->base_header.size;
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

static uint64_t isom_update_stsl_size( isom_stsl_t *stsl )
{
    if( !stsl )
        return 0;
    stsl->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 6;
    CHECK_LARGESIZE( stsl->full_header.size );
    return stsl->full_header.size;
}

static uint64_t isom_update_avcC_size( isom_avcC_t *avcC )
{
    if( !avcC || !avcC->sequenceParameterSets || !avcC->pictureParameterSets )
        return 0;
    uint64_t size = ISOM_DEFAULT_BOX_HEADER_SIZE + 7;
    for( lsmash_entry_t *entry = avcC->sequenceParameterSets->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
        size += 2 + data->parameterSetLength;
    }
    for( lsmash_entry_t *entry = avcC->pictureParameterSets->head; entry; entry = entry->next )
    {
        isom_avcC_ps_entry_t *data = (isom_avcC_ps_entry_t *)entry->data;
        size += 2 + data->parameterSetLength;
    }
    if( ISOM_REQUIRES_AVCC_EXTENSION( avcC->AVCProfileIndication ) )
    {
        size += 4;
        for( lsmash_entry_t *entry = avcC->sequenceParameterSetExt->head; entry; entry = entry->next )
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
        + isom_update_stsl_size( avc->stsl )
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
        + isom_update_clap_size( mp4v->clap )
        + isom_update_stsl_size( mp4v->stsl );
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

static uint64_t isom_update_frma_size( isom_frma_t *frma )
{
    if( !frma )
        return 0;
    frma->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 4;
    CHECK_LARGESIZE( frma->base_header.size );
    return frma->base_header.size;
}

static uint64_t isom_update_mp4a_size( isom_mp4a_t *mp4a )
{
    if( !mp4a )
        return 0;
    mp4a->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 4;
    CHECK_LARGESIZE( mp4a->base_header.size );
    return mp4a->base_header.size;
}

static uint64_t isom_update_terminator_size( isom_terminator_t *terminator )
{
    if( !terminator )
        return 0;
    terminator->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE;
    CHECK_LARGESIZE( terminator->base_header.size );
    return terminator->base_header.size;
}

static uint64_t isom_update_wave_size( isom_wave_t *wave )
{
    if( !wave )
        return 0;
    wave->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE
        + isom_update_frma_size( wave->frma )
        + isom_update_mp4a_size( wave->mp4a )
        + isom_update_esds_size( wave->esds )
        + isom_update_terminator_size( wave->terminator );
    CHECK_LARGESIZE( wave->base_header.size );
    return wave->base_header.size;
}

static uint64_t isom_update_audio_entry_size( isom_audio_entry_t *audio )
{
    if( !audio )
        return 0;
    audio->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 28 + (uint64_t)audio->exdata_length;
    if( audio->version == 1 )
        audio->base_header.size += 16 + isom_update_wave_size( audio->wave );
    CHECK_LARGESIZE( audio->base_header.size );
    return audio->base_header.size;
}

static uint64_t isom_update_text_entry_size( isom_text_entry_t *text )
{
    if( !text )
        return 0;
    text->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 51 + (uint64_t)text->font_name_length;
    CHECK_LARGESIZE( text->base_header.size );
    return text->base_header.size;
}

static uint64_t isom_update_ftab_size( isom_ftab_t *ftab )
{
    if( !ftab || !ftab->list )
        return 0;
    ftab->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 2;
    for( lsmash_entry_t *entry = ftab->list->head; entry; entry = entry->next )
    {
        isom_font_record_t *data = (isom_font_record_t *)entry->data;
        ftab->base_header.size += 3 + data->font_name_length;
    }
    CHECK_LARGESIZE( ftab->base_header.size );
    return ftab->base_header.size;
}

static uint64_t isom_update_tx3g_entry_size( isom_tx3g_entry_t *tx3g )
{
    if( !tx3g )
        return 0;
    tx3g->base_header.size = ISOM_DEFAULT_BOX_HEADER_SIZE + 38 + isom_update_ftab_size( tx3g->ftab );
    CHECK_LARGESIZE( tx3g->base_header.size );
    return tx3g->base_header.size;
}

static uint64_t isom_update_stsd_size( isom_stsd_t *stsd )
{
    if( !stsd || !stsd->list )
        return 0;
    uint64_t size = ISOM_DEFAULT_LIST_FULLBOX_HEADER_SIZE;
    for( lsmash_entry_t *entry = stsd->list->head; entry; entry = entry->next )
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
                if( !((isom_audio_entry_t *)data)->version )
                    size += isom_update_mp4a_entry_size( (isom_mp4a_entry_t *)data );
                else
                    size += isom_update_audio_entry_size( (isom_audio_entry_t *)data );
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
            case ISOM_CODEC_TYPE_TX3G_TEXT :
                size += isom_update_tx3g_entry_size( (isom_tx3g_entry_t *)data );
                break;
            case QT_CODEC_TYPE_TEXT_TEXT :
                size += isom_update_text_entry_size( (isom_text_entry_t *)data );
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

static uint64_t isom_update_cslg_size( isom_cslg_t *cslg )
{
    if( !cslg )
        return 0;
    cslg->full_header.size = ISOM_DEFAULT_FULLBOX_HEADER_SIZE + 20;
    CHECK_LARGESIZE( cslg->full_header.size );
    return cslg->full_header.size;
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
            /* We don't consider other grouping types currently. */
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
        + isom_update_cslg_size( stbl->cslg )
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
        + isom_update_gmhd_size( minf->gmhd )
        + isom_update_hdlr_size( minf->hdlr )
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
    for( lsmash_entry_t *entry = chpl->list->head; entry; entry = entry->next )
    {
        isom_chpl_entry_t *data = (isom_chpl_entry_t *)entry->data;
        chpl->full_header.size += 9 + data->chapter_name_length;
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
        + isom_update_tapt_size( trak->tapt )
        + isom_update_edts_size( trak->edts )
        + isom_update_tref_size( trak->tref )
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
        for( lsmash_entry_t *entry = moov->trak_list->head; entry; entry = entry->next )
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
    for( lsmash_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
        if( !trak || !trak->tkhd )
            return;
        if( trak->tkhd->track_ID == track_ID )
        {
            lsmash_entry_t *next = entry->next;
            lsmash_entry_t *prev = entry->prev;
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

uint32_t isom_create_track( isom_root_t *root, uint32_t media_type )
{
    isom_trak_entry_t *trak = isom_add_trak( root );
    if( !trak )
        return 0;
    if( isom_add_tkhd( trak, media_type ) ||
        isom_add_mdia( trak ) ||
        isom_add_mdhd( trak->mdia, root->qt_compatible ? 0 : ISOM_LANG( "und" ) ) ||
        isom_add_minf( trak->mdia ) ||
        isom_add_stbl( trak->mdia->minf ) ||
        isom_add_dinf( trak->mdia->minf ) ||
        isom_add_dref( trak->mdia->minf->dinf ) ||
        isom_add_stsd( trak->mdia->minf->stbl ) ||
        isom_add_stts( trak->mdia->minf->stbl ) ||
        isom_add_stsc( trak->mdia->minf->stbl ) ||
        isom_add_stco( trak->mdia->minf->stbl ) ||
        isom_add_stsz( trak->mdia->minf->stbl ) )
        return 0;
    if( isom_add_hdlr( trak->mdia, NULL, media_type, root ) )
        return 0;
    if( root->qt_compatible && isom_add_hdlr( NULL, trak->mdia->minf, ISOM_REFERENCE_HANDLER_TYPE_URL, root ) )
        return 0;
    switch( media_type )
    {
        case ISOM_MEDIA_HANDLER_TYPE_VIDEO :
            if( isom_add_vmhd( trak->mdia->minf ) )
                return 0;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_AUDIO :
            if( isom_add_smhd( trak->mdia->minf ) )
                return 0;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_HINT :
            if( isom_add_hmhd( trak->mdia->minf ) )
                return 0;
            break;
        case ISOM_MEDIA_HANDLER_TYPE_TEXT :
            if( root->qt_compatible )
            {
                if( isom_add_gmhd( trak->mdia->minf ) ||
                    isom_add_gmin( trak->mdia->minf->gmhd ) ||
                    isom_add_text( trak->mdia->minf->gmhd ) )
                    return 0;
            }
            else
                return 0;   /* We support only reference text media track for chapter yet. */
            break;
        default :
            if( isom_add_nmhd( trak->mdia->minf ) )
                return 0;
            break;
    }
    return trak->tkhd->track_ID;
}

int isom_set_media_handler( isom_root_t *root, uint32_t track_ID, uint32_t media_type, char *handler_name )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia )
        return -1;
    if( !trak->mdia->hdlr && isom_add_hdlr( trak->mdia, NULL, 0, 0 ) )
        return -1;
    isom_hdlr_t *hdlr = trak->mdia->hdlr;
    hdlr->type = ISOM_HANDLER_TYPE_MEDIA;
    hdlr->subtype = media_type;
    if( handler_name )
    {
        if( hdlr->name )
            free( hdlr->name );
        uint32_t name_length = strlen( handler_name ) + root->isom_compatible + root->qt_compatible;
        uint8_t *name = NULL;
        if( root->qt_compatible )
        {
            name_length = ISOM_MIN( name_length, 255 );
            name = malloc( name_length );
            name[0] = name_length & 0xff;
        }
        else
            name = malloc( name_length );
        memcpy( name + root->qt_compatible, name, strlen( handler_name ) );
        if( root->isom_compatible )
            name[name_length - 1] = 0;
        hdlr->name = name;
        hdlr->name_length = name_length;
    }
    return 0;
}

int isom_set_media_handler_name( isom_root_t *root, uint32_t track_ID, char *handler_name )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->hdlr )
        return -1;
    isom_hdlr_t *hdlr = trak->mdia->hdlr;
    uint8_t *name = NULL;
    uint32_t name_length = strlen( handler_name ) + root->isom_compatible + root->qt_compatible;
    if( root->qt_compatible )
        name_length = ISOM_MIN( name_length, 255 );
    if( name_length > hdlr->name_length && hdlr->name )
        name = realloc( hdlr->name, name_length );
    else if( !hdlr->name )
        name = malloc( name_length );
    else
        name = hdlr->name;
    if( !name )
        return -1;
    if( root->qt_compatible )
        name[0] = name_length & 0xff;
    memcpy( name + root->qt_compatible, handler_name, strlen( handler_name ) );
    if( root->isom_compatible )
        name[name_length - 1] = 0;
    hdlr->name = name;
    hdlr->name_length = name_length;
    return 0;
}

int isom_set_data_handler( isom_root_t *root, uint32_t track_ID, uint32_t reference_type, char *handler_name )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf )
        return -1;
    if( !trak->mdia->minf->hdlr && isom_add_hdlr( NULL, trak->mdia->minf, 0, 0 ) )
        return -1;
    isom_hdlr_t *hdlr = trak->mdia->minf->hdlr;
    hdlr->type = ISOM_HANDLER_TYPE_DATA;
    hdlr->subtype = reference_type;
    if( handler_name )
    {
        if( hdlr->name )
            free( hdlr->name );
        uint32_t name_length = strlen( handler_name ) + root->isom_compatible + root->qt_compatible;
        uint8_t *name = NULL;
        if( root->qt_compatible )
        {
            name_length = ISOM_MIN( name_length, 255 );
            name = malloc( name_length );
            name[0] = name_length & 0xff;
        }
        else
            name = malloc( name_length );
        memcpy( name + root->qt_compatible, name, strlen( handler_name ) );
        if( root->isom_compatible )
            name[name_length - 1] = 0;
        hdlr->name = name;
        hdlr->name_length = name_length;
    }
    return 0;
}

int isom_set_data_handler_name( isom_root_t *root, uint32_t track_ID, char *handler_name )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->hdlr )
        return -1;
    isom_hdlr_t *hdlr = trak->mdia->minf->hdlr;
    uint8_t *name = NULL;
    uint32_t name_length = strlen( handler_name ) + root->isom_compatible + root->qt_compatible;
    if( root->qt_compatible )
        name_length = ISOM_MIN( name_length, 255 );
    if( name_length > hdlr->name_length && hdlr->name )
        name = realloc( hdlr->name, name_length );
    else if( !hdlr->name )
        name = malloc( name_length );
    else
        name = hdlr->name;
    if( !name )
        return -1;
    if( root->qt_compatible )
        name[0] = name_length & 0xff;
    memcpy( name + root->qt_compatible, handler_name, strlen( handler_name ) );
    if( root->isom_compatible )
        name[name_length - 1] = 0;
    hdlr->name = name;
    hdlr->name_length = name_length;
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

uint64_t isom_get_media_duration( isom_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return 0;
    return trak->mdia->mdhd->duration;
}

uint64_t isom_get_track_duration( isom_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->tkhd )
        return 0;
    return trak->tkhd->duration;
}

uint32_t isom_get_last_sample_delta( isom_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl ||
        !trak->mdia->minf->stbl->stts || !trak->mdia->minf->stbl->stts->list ||
        !trak->mdia->minf->stbl->stts->list->head || !trak->mdia->minf->stbl->stts->list->head->data )
        return 0;
    return ((isom_stts_entry_t *)trak->mdia->minf->stbl->stts->list->head->data)->sample_delta;
}

uint32_t isom_get_start_time_offset( isom_root_t *root, uint32_t track_ID )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl ||
        !trak->mdia->minf->stbl->ctts || !trak->mdia->minf->stbl->ctts->list ||
        !trak->mdia->minf->stbl->ctts->list->head || !trak->mdia->minf->stbl->ctts->list->head->data )
        return 0;
    return ((isom_ctts_entry_t *)trak->mdia->minf->stbl->ctts->list->head->data)->sample_offset;
}

int isom_set_track_mode( isom_root_t *root, uint32_t track_ID, uint32_t mode )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->tkhd )
        return -1;
    trak->tkhd->full_header.flags = mode;
    return 0;
}

int isom_set_media_language( isom_root_t *root, uint32_t track_ID, char *ISO_language, uint16_t Mac_language )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->mdhd )
        return -1;
    trak->mdia->mdhd->language = ISO_language ? ISOM_LANG( ISO_language ) : !root->qt_compatible ? ISOM_LANG( "und" ): Mac_language;
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
    isom_visual_entry_t *data = (isom_visual_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    switch( data->base_header.type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
#if 0
        case ISOM_CODEC_TYPE_AVC2_VIDEO :
        case ISOM_CODEC_TYPE_AVCP_VIDEO :
        case ISOM_CODEC_TYPE_SVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC2_VIDEO :
        case ISOM_CODEC_TYPE_MP4V_VIDEO :
        case ISOM_CODEC_TYPE_DRAC_VIDEO :
        case ISOM_CODEC_TYPE_ENCV_VIDEO :
        case ISOM_CODEC_TYPE_MJP2_VIDEO :
        case ISOM_CODEC_TYPE_S263_VIDEO :
        case ISOM_CODEC_TYPE_VC_1_VIDEO :
#endif
            break;
        default :
            return -1;
    }
    data->width = width;
    data->height = height;
    return 0;
}

int isom_set_sample_aspect_ratio( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint32_t hSpacing, uint32_t vSpacing )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_visual_entry_t *data = (isom_visual_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    switch( data->base_header.type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
#if 0
        case ISOM_CODEC_TYPE_AVC2_VIDEO :
        case ISOM_CODEC_TYPE_AVCP_VIDEO :
        case ISOM_CODEC_TYPE_SVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC2_VIDEO :
        case ISOM_CODEC_TYPE_MP4V_VIDEO :
        case ISOM_CODEC_TYPE_DRAC_VIDEO :
        case ISOM_CODEC_TYPE_ENCV_VIDEO :
        case ISOM_CODEC_TYPE_MJP2_VIDEO :
        case ISOM_CODEC_TYPE_S263_VIDEO :
        case ISOM_CODEC_TYPE_VC_1_VIDEO :
#endif
            break;
        default :
            return -1;
    }
    if( !data->pasp && isom_add_pasp( data ) )
        return -1;
    isom_pasp_t *pasp = (isom_pasp_t *)data->pasp;
    pasp->hSpacing = hSpacing;
    pasp->vSpacing = vSpacing;
    return 0;
}

int isom_set_track_aperture_modes( isom_root_t *root, uint32_t track_ID, uint32_t entry_number )
{
    if( !root->qt_compatible )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_visual_entry_t *data = (isom_visual_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    switch( data->base_header.type )
    {
        case ISOM_CODEC_TYPE_AVC1_VIDEO :
#if 0
        case ISOM_CODEC_TYPE_AVC2_VIDEO :
        case ISOM_CODEC_TYPE_AVCP_VIDEO :
        case ISOM_CODEC_TYPE_SVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC1_VIDEO :
        case ISOM_CODEC_TYPE_MVC2_VIDEO :
        case ISOM_CODEC_TYPE_MP4V_VIDEO :
        case ISOM_CODEC_TYPE_DRAC_VIDEO :
        case ISOM_CODEC_TYPE_ENCV_VIDEO :
        case ISOM_CODEC_TYPE_MJP2_VIDEO :
        case ISOM_CODEC_TYPE_S263_VIDEO :
        case ISOM_CODEC_TYPE_VC_1_VIDEO :
#endif
            break;
        default :
            return -1;
    }
    uint32_t width = data->width << 16;
    uint32_t height = data->height << 16;
    if( !trak->tapt && isom_add_tapt( trak ) )
        return -1;
    isom_tapt_t *tapt = trak->tapt;
    if( (!tapt->clef && isom_add_clef( tapt )) ||
        (!tapt->prof && isom_add_prof( tapt )) ||
        (!tapt->enof && isom_add_enof( tapt )) )
        return -1;
    isom_pasp_t *pasp = data->pasp;
    if( !pasp )
    {
        if( isom_add_pasp( data ) )
            return -1;
        pasp = data->pasp;
    }
    isom_clap_t *clap = data->clap;
    if( !clap )
    {
        if( isom_add_clap( data ) )
            return -1;
        clap = data->clap;
        clap->cleanApertureWidthN = data->width;
        clap->cleanApertureHeightN = data->height;
    }
    if( !pasp->hSpacing || !pasp->vSpacing ||
        !clap->cleanApertureWidthN || !clap->cleanApertureWidthD ||
        !clap->cleanApertureHeightN || !clap->cleanApertureHeightD ||
        !clap->horizOffD || !clap->vertOffD )
        return -1;
    double par = (double)pasp->hSpacing / pasp->vSpacing;
    double clap_width = ((double)clap->cleanApertureWidthN / clap->cleanApertureWidthD) * (1<<16);
    double clap_height = ((double)clap->cleanApertureHeightN / clap->cleanApertureHeightD) * (1<<16);
    if( par >= 1.0 )
    {
        tapt->clef->width = clap_width * par;
        tapt->clef->height = clap_height;
        tapt->prof->width = width * par;
        tapt->prof->height = height;
    }
    else
    {
        tapt->clef->width = clap_width;
        tapt->clef->height = clap_height / par;
        tapt->prof->width = width;
        tapt->prof->height = height / par;
    }
    tapt->enof->width = width;
    tapt->enof->height = height;
    return 0;
}

int isom_set_scaling_method( isom_root_t *root, uint32_t track_ID, uint32_t entry_number,
                             uint8_t scale_method, int16_t display_center_x, int16_t display_center_y )
{
    if( !root || !track_ID || !entry_number || !scale_method )
        return -1;
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_visual_entry_t *data = (isom_visual_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    isom_stsl_t *stsl = data->stsl;
    if( !stsl )
    {
        if( isom_add_stsl( data ) )
            return -1;
        stsl = data->stsl;
    }
    stsl->constraint_flag = 1;
    stsl->scale_method = scale_method;
    stsl->display_center_x = display_center_x;
    stsl->display_center_y = display_center_y;
    return 0;
}

int isom_set_sample_type( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint32_t sample_type )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl || !trak->mdia->minf->stbl->stsd || !trak->mdia->minf->stbl->stsd->list )
        return -1;
    isom_sample_entry_t *data = (isom_sample_entry_t *)lsmash_get_entry_data( trak->mdia->minf->stbl->stsd->list, entry_number );
    if( !data )
        return -1;
    data->base_header.type = sample_type;
    return 0;
}

int isom_create_grouping( isom_root_t *root, uint32_t track_ID, uint32_t grouping_type )
{
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
        return -1;
    return isom_add_sbgp( trak->mdia->minf->stbl, grouping_type );
}

/*---- movie manipulators ----*/

isom_root_t *isom_create_movie( char *filename )
{
    isom_root_t *root = malloc( sizeof(isom_root_t) );
    if( !root )
        return NULL;
    memset( root, 0, sizeof(isom_root_t) );
    root->bs = malloc( sizeof(lsmash_bs_t) );
    if( !root->bs )
        return NULL;
    memset( root->bs, 0, sizeof(lsmash_bs_t) );
    root->bs->stream = fopen( filename, "w+b" );
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
    if( !root )
        return -1;
    if( !brand_count )
    {
        /* Absence of ftyp box means this file is a QuickTime or MP4 version 1 format file. */
        if( root->ftyp )
        {
            if( root->ftyp->compatible_brands )
                free( root->ftyp->compatible_brands );
            free( root->ftyp );
            root->ftyp = NULL;
        }
        return 0;
    }
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
    return isom_check_compatibility( root );
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
    if( !root )
        return -1;
    isom_ftyp_t *ftyp = root->ftyp;
    if( !ftyp || !ftyp->brand_count )
        return 0;
    lsmash_bs_t *bs = root->bs;
    isom_bs_put_base_header( bs, &ftyp->base_header );
    lsmash_bs_put_be32( bs, ftyp->major_brand );
    lsmash_bs_put_be32( bs, ftyp->minor_version );
    for( uint32_t i = 0; i < ftyp->brand_count; i++ )
        lsmash_bs_put_be32( bs, ftyp->compatible_brands[i] );
    return lsmash_bs_write_data( bs );
}

int isom_write_moov( isom_root_t *root )
{
    if( !root || !root->moov )
        return -1;
    isom_bs_put_base_header( root->bs, &root->moov->base_header );
    if( lsmash_bs_write_data( root->bs ) )
        return -1;
    if( isom_write_mvhd( root ) ||
        isom_write_iods( root ) )
        return -1;
    if( root->moov->trak_list )
        for( lsmash_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
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
    uint8_t large_flag = mdat->base_header.size > UINT32_MAX;
    lsmash_bs_t *bs = root->bs;
    FILE *stream = bs->stream;
    uint64_t current_pos = ftell( stream );
    if( large_flag )
    {
        fseek( stream, mdat->placeholder_pos, SEEK_SET );
        lsmash_bs_put_be32( bs, 1 );
        lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_MDAT );
        lsmash_bs_put_be64( bs, mdat->base_header.size + ISOM_DEFAULT_BOX_HEADER_SIZE );
    }
    else
    {
        fseek( stream, mdat->placeholder_pos + ISOM_DEFAULT_BOX_HEADER_SIZE, SEEK_SET );
        lsmash_bs_put_be32( bs, mdat->base_header.size );
        lsmash_bs_put_be32( bs, ISOM_BOX_TYPE_MDAT );
    }
    if( lsmash_bs_write_data( bs ) )
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
    lsmash_bs_t *bs = root->bs;
    skip->base_header.size = 8 + skip->length;
    isom_bs_put_base_header( bs, &skip->base_header );
    if( skip->data && skip->length )
        lsmash_bs_put_bytes( bs, skip->data, skip->length );
    return lsmash_bs_write_data( bs );
}

int isom_create_object_descriptor( isom_root_t *root )
{
    if( !root )
        return -1;
    /* Return error if this file is not compatible with MP4 file format. */
    if( !root->mp4_version1 && !root->mp4_version2 )
        return -1;
    return isom_add_iods( root->moov );
}

/*---- finishing functions ----*/

int isom_finish_movie( isom_root_t *root, isom_adhoc_remux_t* remux )
{
    if( !root || !root->moov || !root->moov->trak_list )
        return -1;
    for( lsmash_entry_t *entry = root->moov->trak_list->head; entry; entry = entry->next )
    {
        isom_trak_entry_t *trak = (isom_trak_entry_t *)entry->data;
        if( !trak || !trak->tkhd )
            return -1;
        uint32_t track_ID = trak->tkhd->track_ID;
        uint32_t related_track_ID = trak->related_track_ID;
        uint32_t track_mode = trak->is_chapter ? 0 : ISOM_TRACK_ENABLED;
        track_mode |= ISOM_TRACK_IN_MOVIE;  /* presentation for QuickTime Player */
        if( isom_set_track_mode( root, track_ID, track_mode ) )
            return -1;
        if( trak->is_chapter && related_track_ID )
        {
            /* In order that the track duration of the chapter track doesn't exceed that of the related track. */
            uint64_t track_duration = ISOM_MIN( trak->tkhd->duration, isom_get_track_duration( root, related_track_ID ) );
            if( isom_create_explicit_timeline_map( root, track_ID, track_duration, 0, ISOM_NORMAL_EDIT ) )
                return -1;
        }
    }
    if( root->mp4_version1 == 1 && isom_add_iods( root->moov ) )
        return -1;
    if( isom_check_mandatory_boxes( root ) ||
        isom_set_movie_creation_time( root ) ||
        isom_update_moov_size( root->moov ) )
        return -1;

    if( !remux )
        return isom_write_moov( root );

    /* stco->co64 conversion, depending on last chunk's offset */
    for( lsmash_entry_t* entry = root->moov->trak_list->head; entry; )
    {
        isom_trak_entry_t* trak = (isom_trak_entry_t*)entry->data;
        if( !trak || !trak->mdia || !trak->mdia->minf || !trak->mdia->minf->stbl )
            return -1;
        isom_stco_t* stco = trak->mdia->minf->stbl->stco;
        if( !stco || !stco->list || !stco->list->tail )
            return -1;
        if( stco->large_presentation
            || ((isom_stco_entry_t*)stco->list->tail->data)->chunk_offset + root->moov->base_header.size <= UINT32_MAX )
        {
            entry = entry->next;
            continue; /* no need to remux */
        }
        /* stco->co64 conversion */
        if( isom_convert_stco_to_co64( trak->mdia->minf->stbl )
            || isom_update_moov_size( root->moov ) )
            return -1;
        entry = root->moov->trak_list->head; /* whenever any conversion, re-check all traks */
    }

    /* now the amount of offset is fixed. */

    /* buffer size must be at least sizeof(moov)*2 */
    remux->buffer_size = ISOM_MAX( remux->buffer_size, root->moov->base_header.size * 2 );

    uint8_t* buf[2];
    if( (buf[0] = (uint8_t*)malloc( remux->buffer_size )) == NULL )
        return -1; /* NOTE: i think we still can fallback to "return isom_write_moov( root );" here. */
    uint64_t size = remux->buffer_size / 2;
    buf[1] = buf[0] + size; /* split to 2 buffers */

    /* now the amount of offset is fixed. apply that to stco/co64 */
    for( lsmash_entry_t* entry = root->moov->trak_list->head; entry; entry = entry->next )
    {
        isom_stco_t* stco = ((isom_trak_entry_t*)entry->data)->mdia->minf->stbl->stco;
        if( stco->large_presentation )
            for( lsmash_entry_t* co64_entry = stco->list->head ; co64_entry ; co64_entry = co64_entry->next )
                ((isom_co64_entry_t*)co64_entry->data)->chunk_offset += root->moov->base_header.size;
        else
            for( lsmash_entry_t* stco_entry = stco->list->head ; stco_entry ; stco_entry = stco_entry->next )
                ((isom_stco_entry_t*)stco_entry->data)->chunk_offset += root->moov->base_header.size;
    }

    FILE *stream = root->bs->stream;
    isom_mdat_t *mdat = root->mdat;
    uint64_t total = ftell( stream ) + root->moov->base_header.size; // FIXME:
    uint64_t readnum;
    /* backup starting area of mdat and write moov there instead */
    if( fseek( stream, mdat->placeholder_pos, SEEK_SET ) )
        goto fail;
    readnum = fread( buf[0], 1, size, stream );
    uint64_t read_pos = ftell( stream );

    /* write moov there instead */
    if( fseek( stream, mdat->placeholder_pos, SEEK_SET )
        || isom_write_moov( root ) )
        goto fail;
    uint64_t write_pos = ftell( stream );

    mdat->placeholder_pos += root->moov->base_header.size; /* update placeholder */

    /* copy-pastan */
    int buf_switch = 1;
    while( readnum == size )
    {
        if( fseek( stream, read_pos, SEEK_SET ) )
            goto fail;
        readnum = fread( buf[buf_switch], 1, size, stream );
        read_pos = ftell( stream );

        buf_switch ^= 0x1;

        if( fseek( stream, write_pos, SEEK_SET )
            || fwrite( buf[buf_switch], 1, size, stream ) != size )
            goto fail;
        write_pos = ftell( stream );
        if( remux->func ) remux->func( remux->param, write_pos, total ); // FIXME:
    }
    if( fwrite( buf[buf_switch^0x1], 1, readnum, stream ) != readnum )
        goto fail;
    if( remux->func ) remux->func( remux->param, total, total ); // FIXME:

    free( buf[0] );
    return 0;

fail:
    free( buf[0] );
    return -1;
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
    for( lsmash_entry_t *entry = stts->list->head; entry; entry = entry->next )
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
    isom_elst_entry_t *data = (isom_elst_entry_t *)lsmash_get_entry_data( trak->edts->elst->list, entry_number );
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
    return lsmash_add_entry( current->pool, sample );
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

int isom_create_reference_chapter_track( isom_root_t *root, uint32_t track_ID, char *file_name )
{
    if( !root || !root->qt_compatible || !root->moov || !root->moov->mvhd )
        return -1;
    /* Create Track Reference Type Box. */
    isom_trak_entry_t *trak = isom_get_trak( root, track_ID );
    if( !trak || isom_add_tref( trak ) )
        return -1;
    isom_tref_t *tref = trak->tref;
    isom_tref_type_t *type = NULL, *chap = NULL;
    if( !tref->type )
        type = (isom_tref_type_t *)malloc( sizeof(isom_tref_type_t) );
    else
        type = (isom_tref_type_t *)realloc( tref->type, (tref->type_count + 1) * sizeof(isom_tref_type_t) );
    if( !type )
        return -1;
    chap = &type[tref->type_count];
    tref->type = type;
    tref->type_count += 1;
    isom_init_base_header( &chap->base_header, QT_TREF_TYPE_CHAP );
    chap->track_ID = (uint32_t *)malloc( sizeof(uint32_t) );
    if( !chap->track_ID )
        return -1;
    uint32_t chapter_track_ID = chap->track_ID[0] = root->moov->mvhd->next_track_ID;
    chap->ref_count = 1;
    /* Create reference chapter track. */
    if( chapter_track_ID != isom_create_track( root, ISOM_MEDIA_HANDLER_TYPE_TEXT ) )
        return -1;
    /* Copy media timescale. */
    uint64_t media_timescale = isom_get_media_timescale( root, track_ID );
    if( !media_timescale || isom_set_media_timescale( root, chapter_track_ID, media_timescale ) )
        goto fail;
    /* Set media language field. ISOM: undefined / QTFF: English */
    if( isom_set_media_language( root, chapter_track_ID, root->max_3gpp_version < 6 ? NULL : "und", 0 ) )
        goto fail;
    /* Create sample description. */
    uint32_t sample_type = root->max_3gpp_version < 6 ? QT_CODEC_TYPE_TEXT_TEXT : ISOM_CODEC_TYPE_TX3G_TEXT;
    uint32_t sample_entry = isom_add_sample_entry( root, chapter_track_ID, sample_type, NULL );
    if( !sample_entry )
        goto fail;
    /* Open chapter format file. */
    FILE *chapter = fopen( file_name, "rb" );
    if( !chapter )
        goto fail;
    /* Parse the file and write text samples. */
    char buff[512];
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
        uint64_t start_time = (ms * 1e-3 + (ss + mm * 60 + hh * 3600)) * media_timescale + 0.5;
        /* write a text sample here */
        uint16_t name_length = strlen( chapter_name );
        isom_sample_t *sample = isom_create_sample( name_length + 2 );
        if( !sample )
            goto fail;
        sample->data[0] = (name_length >> 8) & 0xff;
        sample->data[1] =  name_length       & 0xff;
        memcpy( sample->data + 2, chapter_name, name_length );
        /* QuickTime Player requires encd atom if media language is ISO language codes : undefined. */
        //uint8_t extradata[12] = { 0x00, 0x00, 0x00, 0x0C,   /* size: 12 */
        //                          0x65, 0x6E, 0x63, 0x64,   /* type: 'encd' */
        //                          0x00, 0x00, 0x01, 0x00 };
        //memcpy( sample->data + 2 + name_length, extradata, 12 );
        sample->dts = sample->cts = start_time;
        sample->index = sample_entry;
        if( isom_write_sample( root, chapter_track_ID, sample ) )
            goto fail;
    }
    fclose( chapter );
    if( isom_flush_pooled_samples( root, chapter_track_ID, 0 ) )
        goto fail;
    trak = isom_get_trak( root, chapter_track_ID );
    if( !trak )
        goto fail;
    trak->is_chapter = 1;
    trak->related_track_ID = track_ID;
    return 0;
fail:
    if( chapter )
        fclose( chapter );
    free( chap->track_ID );
    chap->track_ID = NULL;
    isom_remove_trak( isom_get_trak( root, chapter_track_ID ) );
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
