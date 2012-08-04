/*****************************************************************************
 * description.c:
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

#include "internal.h"   /* must be placed first */

#include "box.h"

int lsmash_convert_crop_into_clap( lsmash_crop_t crop, uint32_t width, uint32_t height, lsmash_clap_t *clap )
{
    if( !clap || crop.top.d == 0 || crop.bottom.d == 0 || crop.left.d == 0 ||  crop.right.d == 0 )
        return -1;
    uint64_t vertical_crop_lcm   = lsmash_get_lcm( crop.top.d,  crop.bottom.d );
    uint64_t horizontal_crop_lcm = lsmash_get_lcm( crop.left.d, crop.right.d  );
    lsmash_rational_u64_t clap_height;
    lsmash_rational_u64_t clap_width;
    lsmash_rational_s64_t clap_horizontal_offset;
    lsmash_rational_s64_t clap_vertical_offset;
    clap_height.d            = vertical_crop_lcm;
    clap_width.d             = horizontal_crop_lcm;
    clap_horizontal_offset.d = 2 * vertical_crop_lcm;
    clap_vertical_offset.d   = 2 * horizontal_crop_lcm;
    clap_height.n = height * vertical_crop_lcm
                  - (crop.top.n * (vertical_crop_lcm / crop.top.d) + crop.bottom.n * (vertical_crop_lcm / crop.bottom.d));
    clap_width.n  = width * horizontal_crop_lcm
                  - (crop.left.n * (horizontal_crop_lcm / crop.left.d) + crop.right.n * (horizontal_crop_lcm / crop.right.d));
    clap_horizontal_offset.n = (int64_t)(crop.left.n * (horizontal_crop_lcm / crop.left.d))
                             - crop.right.n * (horizontal_crop_lcm / crop.right.d);
    clap_vertical_offset.n   = (int64_t)(crop.top.n * (vertical_crop_lcm / crop.top.d))
                             - crop.bottom.n * (vertical_crop_lcm / crop.bottom.d);
    lsmash_reduce_fraction( &clap_height.n, &clap_height.d );
    lsmash_reduce_fraction( &clap_width.n,  &clap_width.d  );
    lsmash_reduce_fraction_su( &clap_vertical_offset.n,   &clap_vertical_offset.d   );
    lsmash_reduce_fraction_su( &clap_horizontal_offset.n, &clap_horizontal_offset.d );
    clap->height            = (lsmash_rational_u32_t){ clap_height.n,            clap_height.d            };
    clap->width             = (lsmash_rational_u32_t){ clap_width.n,             clap_width.d             };
    clap->vertical_offset   = (lsmash_rational_s32_t){ clap_vertical_offset.n,   clap_vertical_offset.d   };
    clap->horizontal_offset = (lsmash_rational_s32_t){ clap_horizontal_offset.n, clap_horizontal_offset.d };
    return 0;
}

int lsmash_convert_clap_into_crop( lsmash_clap_t clap, uint32_t width, uint32_t height, lsmash_crop_t *crop )
{
    if( !crop || clap.height.d == 0 || clap.vertical_offset.d == 0 || clap.width.d == 0 || clap.horizontal_offset.d == 0 )
        return -1;
    uint64_t vertical_lcm   = lsmash_get_lcm( clap.height.d, clap.vertical_offset.d   );
    uint64_t horizontal_lcm = lsmash_get_lcm( clap.width.d,  clap.horizontal_offset.d );
    lsmash_rational_u64_t crop_top;
    lsmash_rational_u64_t crop_bottom;
    lsmash_rational_u64_t crop_left;
    lsmash_rational_u64_t crop_right;
    crop_top.d    = 2 * vertical_lcm;
    crop_bottom.d = 2 * vertical_lcm;
    crop_left.d   = 2 * horizontal_lcm;
    crop_right.d  = 2 * horizontal_lcm;
    crop_top.n    = height * vertical_lcm
                  - clap.height.n * (vertical_lcm / clap.height.d)
                  + 2 * clap.vertical_offset.n * (vertical_lcm / clap.vertical_offset.d);
    crop_bottom.n = height * vertical_lcm
                  - clap.height.n * (vertical_lcm / clap.height.d)
                  - 2 * clap.vertical_offset.n * (vertical_lcm / clap.vertical_offset.d);
    crop_left.n   = width * horizontal_lcm
                  - clap.width.n * (horizontal_lcm / clap.width.d)
                  + 2 * clap.horizontal_offset.n * (horizontal_lcm / clap.horizontal_offset.d);
    crop_right.n  = width * horizontal_lcm
                  - clap.width.n * (horizontal_lcm / clap.width.d)
                  - 2 * clap.horizontal_offset.n * (horizontal_lcm / clap.horizontal_offset.d);
    lsmash_reduce_fraction( &crop_top.n,    &crop_top.d    );
    lsmash_reduce_fraction( &crop_bottom.n, &crop_bottom.d );
    lsmash_reduce_fraction( &crop_left.n,   &crop_left.d   );
    lsmash_reduce_fraction( &crop_right.n,  &crop_right.d  );
    crop->top    = (lsmash_rational_u32_t){ crop_top.n,    crop_top.d    };
    crop->bottom = (lsmash_rational_u32_t){ crop_bottom.n, crop_bottom.d };
    crop->left   = (lsmash_rational_u32_t){ crop_left.n,   crop_left.d   };
    crop->right  = (lsmash_rational_u32_t){ crop_right.n,  crop_right.d  };
    return 0;
}

#if 0
static lsmash_video_summary_t *isom_create_video_summary_from_description( isom_visual_entry_t *visual )
{
    isom_visual_entry_t *visual;
    lsmash_video_summary_t *summary = lsmash_malloc_zero( sizeof(lsmash_video_summary_t) );
    if( !summary )
        return NULL;
    summary->width  = visual->width;
    summary->height = visual->height;
    if( visual->clap )
    {
        summary->clap.width.n             = clap->cleanApertureWidthN;
        summary->clap.width.d             = clap->cleanApertureWidthD;
        summary->clap.height.n            = clap->cleanApertureHeightN;
        summary->clap.height.d            = clap->cleanApertureHeightD;
        summary->clap.horizontal_offset.n = clap->horizOffN;
        summary->clap.horizontal_offset.d = clap->horizOffD;
        summary->clap.vertical_offset.n   = clap->vertOffN;
        summary->clap.vertical_offset.d   = clap->vertOffD;
    }
    if( visual->pasp )
    {
        summary->par_h = visual->pasp->hSpacing;
        summary->par_v = visual->pasp->vSpacing;
    }
    if( visual->stsl )
        summary->scaling_method = visual->stsl->scale_method;
    if( visual->colr )
    {
        summary->primaries = visual->colr->primaries_index;
        summary->transfer  = visual->colr->transfer_function_index;
        summary->matrix    = visual->colr->matrix_index;
    }
    return summary;
}
#endif
