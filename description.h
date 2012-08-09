/*****************************************************************************
 * description.h:
 *****************************************************************************
 * Copyright (C) 2012 L-SMASH project
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

struct lsmash_codec_specific_list_tag
{
    lsmash_entry_list_t list;
};

lsmash_codec_specific_t *isom_duplicate_codec_specific_data( lsmash_codec_specific_t *specific );
isom_extension_box_t *isom_get_sample_description_extension( lsmash_entry_list_t *extensions, uint32_t box_type );
lsmash_codec_specific_t *isom_get_codec_specific( lsmash_codec_specific_list_t *opaque, uint32_t type );
uint8_t *isom_get_child_box_position( uint8_t *parent_data, uint32_t parent_size, uint32_t child_type, uint32_t *child_size );
void *isom_get_extension_box( lsmash_entry_list_t *exetnsion, uint32_t box_type );
int isom_add_extension_box( lsmash_entry_list_t *extensions, void *box, void *eliminator );
void isom_remove_sample_description_extensions( lsmash_entry_list_t *extensions );
void isom_remove_sample_description_extension( isom_extension_box_t *ext );
int isom_setup_visual_description( isom_stsd_t *stsd, uint32_t sample_type, lsmash_video_summary_t *summary );
int isom_setup_audio_description( isom_stsd_t *stsd, uint32_t sample_type, lsmash_audio_summary_t *summary );
lsmash_video_summary_t *isom_create_video_summary_from_description( isom_visual_entry_t *visual );
lsmash_audio_summary_t *isom_create_audio_summary_from_description( isom_audio_entry_t *audio );
