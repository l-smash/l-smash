/*****************************************************************************
 * lsmash.h:
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

#ifndef LSMASH_H
#define LSMASH_H

#ifndef __MINGW32__
#define _FILE_OFFSET_BITS 64
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#ifdef __MINGW32__
#define fseek fseeko64
#define ftell ftello64
#endif


/* public data types */
typedef struct
{
    uint32_t complete;      /* recovery point: the identifier necessary for the recovery from its starting point to be completed */
    uint32_t identifier;    /* the identifier for samples
                             * If this identifier equals a certain recovery_point, then this sample is the recovery point. */
    uint8_t start_point;
} isom_recovery_t;

typedef struct
{
    uint8_t sync_point;
    uint8_t partial_sync;
    uint8_t allow_earlier;
    uint8_t leading;
    uint8_t independent;
    uint8_t disposable;
    uint8_t redundant;
    isom_recovery_t recovery;
} isom_sample_property_t;

typedef struct
{
    uint32_t length;
    uint8_t *data;
    uint64_t dts;
    uint64_t cts;
    uint32_t index;
    isom_sample_property_t prop;
} isom_sample_t;

typedef int (*isom_adhoc_remux_callback)( void* param, uint64_t done, uint64_t total );
typedef struct {
    uint64_t buffer_size;
    isom_adhoc_remux_callback func;
    void* param;
} isom_adhoc_remux_t;

typedef struct isom_root_tag isom_root_t;


/* public defines */
#define ISOM_FILE_MODE_WRITE 0x00000001
#define ISOM_FILE_MODE_READ  0x00000002
#define ISOM_FILE_MODE_DUMP  0x00000004

#define ISOM_4CC( a, b, c, d ) (((a)<<24) | ((b)<<16) | ((c)<<8) | (d))


/* public constants */
enum isom_box_code
{
    ISOM_BOX_TYPE_ID32 = ISOM_4CC( 'I', 'D', '3', '2' ),
    ISOM_BOX_TYPE_ALBM = ISOM_4CC( 'a', 'l', 'b', 'm' ),
    ISOM_BOX_TYPE_AUTH = ISOM_4CC( 'a', 'u', 't', 'h' ),
    ISOM_BOX_TYPE_BPCC = ISOM_4CC( 'b', 'p', 'c', 'c' ),
    ISOM_BOX_TYPE_BUFF = ISOM_4CC( 'b', 'u', 'f', 'f' ),
    ISOM_BOX_TYPE_BXML = ISOM_4CC( 'b', 'x', 'm', 'l' ),
    ISOM_BOX_TYPE_CCID = ISOM_4CC( 'c', 'c', 'i', 'd' ),
    ISOM_BOX_TYPE_CDEF = ISOM_4CC( 'c', 'd', 'e', 'f' ),
    ISOM_BOX_TYPE_CLSF = ISOM_4CC( 'c', 'l', 's', 'f' ),
    ISOM_BOX_TYPE_CMAP = ISOM_4CC( 'c', 'm', 'a', 'p' ),
    ISOM_BOX_TYPE_CO64 = ISOM_4CC( 'c', 'o', '6', '4' ),
    ISOM_BOX_TYPE_COLR = ISOM_4CC( 'c', 'o', 'l', 'r' ),
    ISOM_BOX_TYPE_CPRT = ISOM_4CC( 'c', 'p', 'r', 't' ),
    ISOM_BOX_TYPE_CSLG = ISOM_4CC( 'c', 's', 'l', 'g' ),
    ISOM_BOX_TYPE_CTTS = ISOM_4CC( 'c', 't', 't', 's' ),
    ISOM_BOX_TYPE_CVRU = ISOM_4CC( 'c', 'v', 'r', 'u' ),
    ISOM_BOX_TYPE_DCFD = ISOM_4CC( 'd', 'c', 'f', 'D' ),
    ISOM_BOX_TYPE_DINF = ISOM_4CC( 'd', 'i', 'n', 'f' ),
    ISOM_BOX_TYPE_DREF = ISOM_4CC( 'd', 'r', 'e', 'f' ),
    ISOM_BOX_TYPE_DSCP = ISOM_4CC( 'd', 's', 'c', 'p' ),
    ISOM_BOX_TYPE_DSGD = ISOM_4CC( 'd', 's', 'g', 'd' ),
    ISOM_BOX_TYPE_DSTG = ISOM_4CC( 'd', 's', 't', 'g' ),
    ISOM_BOX_TYPE_EDTS = ISOM_4CC( 'e', 'd', 't', 's' ),
    ISOM_BOX_TYPE_ELST = ISOM_4CC( 'e', 'l', 's', 't' ),
    ISOM_BOX_TYPE_FECI = ISOM_4CC( 'f', 'e', 'c', 'i' ),
    ISOM_BOX_TYPE_FECR = ISOM_4CC( 'f', 'e', 'c', 'r' ),
    ISOM_BOX_TYPE_FIIN = ISOM_4CC( 'f', 'i', 'i', 'n' ),
    ISOM_BOX_TYPE_FIRE = ISOM_4CC( 'f', 'i', 'r', 'e' ),
    ISOM_BOX_TYPE_FPAR = ISOM_4CC( 'f', 'p', 'a', 'r' ),
    ISOM_BOX_TYPE_FREE = ISOM_4CC( 'f', 'r', 'e', 'e' ),
    ISOM_BOX_TYPE_FRMA = ISOM_4CC( 'f', 'r', 'm', 'a' ),
    ISOM_BOX_TYPE_FTYP = ISOM_4CC( 'f', 't', 'y', 'p' ),
    ISOM_BOX_TYPE_GITN = ISOM_4CC( 'g', 'i', 't', 'n' ),
    ISOM_BOX_TYPE_GNRE = ISOM_4CC( 'g', 'n', 'r', 'e' ),
    ISOM_BOX_TYPE_GRPI = ISOM_4CC( 'g', 'r', 'p', 'i' ),
    ISOM_BOX_TYPE_HDLR = ISOM_4CC( 'h', 'd', 'l', 'r' ),
    ISOM_BOX_TYPE_HMHD = ISOM_4CC( 'h', 'm', 'h', 'd' ),
    ISOM_BOX_TYPE_ICNU = ISOM_4CC( 'i', 'c', 'n', 'u' ),
    ISOM_BOX_TYPE_IDAT = ISOM_4CC( 'i', 'd', 'a', 't' ),
    ISOM_BOX_TYPE_IHDR = ISOM_4CC( 'i', 'h', 'd', 'r' ),
    ISOM_BOX_TYPE_IINF = ISOM_4CC( 'i', 'i', 'n', 'f' ),
    ISOM_BOX_TYPE_ILOC = ISOM_4CC( 'i', 'l', 'o', 'c' ),
    ISOM_BOX_TYPE_IMIF = ISOM_4CC( 'i', 'm', 'i', 'f' ),
    ISOM_BOX_TYPE_INFU = ISOM_4CC( 'i', 'n', 'f', 'u' ),
    ISOM_BOX_TYPE_IODS = ISOM_4CC( 'i', 'o', 'd', 's' ),
    ISOM_BOX_TYPE_IPHD = ISOM_4CC( 'i', 'p', 'h', 'd' ),
    ISOM_BOX_TYPE_IPMC = ISOM_4CC( 'i', 'p', 'm', 'c' ),
    ISOM_BOX_TYPE_IPRO = ISOM_4CC( 'i', 'p', 'r', 'o' ),
    ISOM_BOX_TYPE_IREF = ISOM_4CC( 'i', 'r', 'e', 'f' ),
    ISOM_BOX_TYPE_JP   = ISOM_4CC( 'j', 'p', ' ', ' ' ),
    ISOM_BOX_TYPE_JP2C = ISOM_4CC( 'j', 'p', '2', 'c' ),
    ISOM_BOX_TYPE_JP2H = ISOM_4CC( 'j', 'p', '2', 'h' ),
    ISOM_BOX_TYPE_JP2I = ISOM_4CC( 'j', 'p', '2', 'i' ),
    ISOM_BOX_TYPE_KYWD = ISOM_4CC( 'k', 'y', 'w', 'd' ),
    ISOM_BOX_TYPE_LOCI = ISOM_4CC( 'l', 'o', 'c', 'i' ),
    ISOM_BOX_TYPE_LRCU = ISOM_4CC( 'l', 'r', 'c', 'u' ),
    ISOM_BOX_TYPE_MDAT = ISOM_4CC( 'm', 'd', 'a', 't' ),
    ISOM_BOX_TYPE_MDHD = ISOM_4CC( 'm', 'd', 'h', 'd' ),
    ISOM_BOX_TYPE_MDIA = ISOM_4CC( 'm', 'd', 'i', 'a' ),
    ISOM_BOX_TYPE_MDRI = ISOM_4CC( 'm', 'd', 'r', 'i' ),
    ISOM_BOX_TYPE_MECO = ISOM_4CC( 'm', 'e', 'c', 'o' ),
    ISOM_BOX_TYPE_MEHD = ISOM_4CC( 'm', 'e', 'h', 'd' ),
    ISOM_BOX_TYPE_M7HD = ISOM_4CC( 'm', '7', 'h', 'd' ),
    ISOM_BOX_TYPE_MERE = ISOM_4CC( 'm', 'e', 'r', 'e' ),
    ISOM_BOX_TYPE_META = ISOM_4CC( 'm', 'e', 't', 'a' ),
    ISOM_BOX_TYPE_MFHD = ISOM_4CC( 'm', 'f', 'h', 'd' ),
    ISOM_BOX_TYPE_MFRA = ISOM_4CC( 'm', 'f', 'r', 'a' ),
    ISOM_BOX_TYPE_MFRO = ISOM_4CC( 'm', 'f', 'r', 'o' ),
    ISOM_BOX_TYPE_MINF = ISOM_4CC( 'm', 'i', 'n', 'f' ),
    ISOM_BOX_TYPE_MJHD = ISOM_4CC( 'm', 'j', 'h', 'd' ),
    ISOM_BOX_TYPE_MOOF = ISOM_4CC( 'm', 'o', 'o', 'f' ),
    ISOM_BOX_TYPE_MOOV = ISOM_4CC( 'm', 'o', 'o', 'v' ),
    ISOM_BOX_TYPE_MVCG = ISOM_4CC( 'm', 'v', 'c', 'g' ),
    ISOM_BOX_TYPE_MVCI = ISOM_4CC( 'm', 'v', 'c', 'i' ),
    ISOM_BOX_TYPE_MVEX = ISOM_4CC( 'm', 'v', 'e', 'x' ),
    ISOM_BOX_TYPE_MVHD = ISOM_4CC( 'm', 'v', 'h', 'd' ),
    ISOM_BOX_TYPE_MVRA = ISOM_4CC( 'm', 'v', 'r', 'a' ),
    ISOM_BOX_TYPE_NMHD = ISOM_4CC( 'n', 'm', 'h', 'd' ),
    ISOM_BOX_TYPE_OCHD = ISOM_4CC( 'o', 'c', 'h', 'd' ),
    ISOM_BOX_TYPE_ODAF = ISOM_4CC( 'o', 'd', 'a', 'f' ),
    ISOM_BOX_TYPE_ODDA = ISOM_4CC( 'o', 'd', 'd', 'a' ),
    ISOM_BOX_TYPE_ODHD = ISOM_4CC( 'o', 'd', 'h', 'd' ),
    ISOM_BOX_TYPE_ODHE = ISOM_4CC( 'o', 'd', 'h', 'e' ),
    ISOM_BOX_TYPE_ODRB = ISOM_4CC( 'o', 'd', 'r', 'b' ),
    ISOM_BOX_TYPE_ODRM = ISOM_4CC( 'o', 'd', 'r', 'm' ),
    ISOM_BOX_TYPE_ODTT = ISOM_4CC( 'o', 'd', 't', 't' ),
    ISOM_BOX_TYPE_OHDR = ISOM_4CC( 'o', 'h', 'd', 'r' ),
    ISOM_BOX_TYPE_PADB = ISOM_4CC( 'p', 'a', 'd', 'b' ),
    ISOM_BOX_TYPE_PAEN = ISOM_4CC( 'p', 'a', 'e', 'n' ),
    ISOM_BOX_TYPE_PCLR = ISOM_4CC( 'p', 'c', 'l', 'r' ),
    ISOM_BOX_TYPE_PDIN = ISOM_4CC( 'p', 'd', 'i', 'n' ),
    ISOM_BOX_TYPE_PERF = ISOM_4CC( 'p', 'e', 'r', 'f' ),
    ISOM_BOX_TYPE_PITM = ISOM_4CC( 'p', 'i', 't', 'm' ),
    ISOM_BOX_TYPE_RES  = ISOM_4CC( 'r', 'e', 's', ' ' ),
    ISOM_BOX_TYPE_RESC = ISOM_4CC( 'r', 'e', 's', 'c' ),
    ISOM_BOX_TYPE_RESD = ISOM_4CC( 'r', 'e', 's', 'd' ),
    ISOM_BOX_TYPE_RTNG = ISOM_4CC( 'r', 't', 'n', 'g' ),
    ISOM_BOX_TYPE_SBGP = ISOM_4CC( 's', 'b', 'g', 'p' ),
    ISOM_BOX_TYPE_SCHI = ISOM_4CC( 's', 'c', 'h', 'i' ),
    ISOM_BOX_TYPE_SCHM = ISOM_4CC( 's', 'c', 'h', 'm' ),
    ISOM_BOX_TYPE_SDEP = ISOM_4CC( 's', 'd', 'e', 'p' ),
    ISOM_BOX_TYPE_SDHD = ISOM_4CC( 's', 'd', 'h', 'd' ),
    ISOM_BOX_TYPE_SDTP = ISOM_4CC( 's', 'd', 't', 'p' ),
    ISOM_BOX_TYPE_SDVP = ISOM_4CC( 's', 'd', 'v', 'p' ),
    ISOM_BOX_TYPE_SEGR = ISOM_4CC( 's', 'e', 'g', 'r' ),
    ISOM_BOX_TYPE_SGPD = ISOM_4CC( 's', 'g', 'p', 'd' ),
    ISOM_BOX_TYPE_SINF = ISOM_4CC( 's', 'i', 'n', 'f' ),
    ISOM_BOX_TYPE_SKIP = ISOM_4CC( 's', 'k', 'i', 'p' ),
    ISOM_BOX_TYPE_SMHD = ISOM_4CC( 's', 'm', 'h', 'd' ),
    ISOM_BOX_TYPE_SRMB = ISOM_4CC( 's', 'r', 'm', 'b' ),
    ISOM_BOX_TYPE_SRMC = ISOM_4CC( 's', 'r', 'm', 'c' ),
    ISOM_BOX_TYPE_SRPP = ISOM_4CC( 's', 'r', 'p', 'p' ),
    ISOM_BOX_TYPE_STBL = ISOM_4CC( 's', 't', 'b', 'l' ),
    ISOM_BOX_TYPE_STCO = ISOM_4CC( 's', 't', 'c', 'o' ),
    ISOM_BOX_TYPE_STDP = ISOM_4CC( 's', 't', 'd', 'p' ),
    ISOM_BOX_TYPE_STSC = ISOM_4CC( 's', 't', 's', 'c' ),
    ISOM_BOX_TYPE_STSD = ISOM_4CC( 's', 't', 's', 'd' ),
    ISOM_BOX_TYPE_STSH = ISOM_4CC( 's', 't', 's', 'h' ),
    ISOM_BOX_TYPE_STSS = ISOM_4CC( 's', 't', 's', 's' ),
    ISOM_BOX_TYPE_STSZ = ISOM_4CC( 's', 't', 's', 'z' ),
    ISOM_BOX_TYPE_STTS = ISOM_4CC( 's', 't', 't', 's' ),
    ISOM_BOX_TYPE_STZ2 = ISOM_4CC( 's', 't', 'z', '2' ),
    ISOM_BOX_TYPE_SUBS = ISOM_4CC( 's', 'u', 'b', 's' ),
    ISOM_BOX_TYPE_SWTC = ISOM_4CC( 's', 'w', 't', 'c' ),
    ISOM_BOX_TYPE_TFHD = ISOM_4CC( 't', 'f', 'h', 'd' ),
    ISOM_BOX_TYPE_TFRA = ISOM_4CC( 't', 'f', 'r', 'a' ),
    ISOM_BOX_TYPE_TIBR = ISOM_4CC( 't', 'i', 'b', 'r' ),
    ISOM_BOX_TYPE_TIRI = ISOM_4CC( 't', 'i', 'r', 'i' ),
    ISOM_BOX_TYPE_TITL = ISOM_4CC( 't', 'i', 't', 'l' ),
    ISOM_BOX_TYPE_TKHD = ISOM_4CC( 't', 'k', 'h', 'd' ),
    ISOM_BOX_TYPE_TRAF = ISOM_4CC( 't', 'r', 'a', 'f' ),
    ISOM_BOX_TYPE_TRAK = ISOM_4CC( 't', 'r', 'a', 'k' ),
    ISOM_BOX_TYPE_TREF = ISOM_4CC( 't', 'r', 'e', 'f' ),
    ISOM_BOX_TYPE_TREX = ISOM_4CC( 't', 'r', 'e', 'x' ),
    ISOM_BOX_TYPE_TRGR = ISOM_4CC( 't', 'r', 'g', 'r' ),
    ISOM_BOX_TYPE_TRUN = ISOM_4CC( 't', 'r', 'u', 'n' ),
    ISOM_BOX_TYPE_TSEL = ISOM_4CC( 't', 's', 'e', 'l' ),
    ISOM_BOX_TYPE_UDTA = ISOM_4CC( 'u', 'd', 't', 'a' ),
    ISOM_BOX_TYPE_UINF = ISOM_4CC( 'u', 'i', 'n', 'f' ),
    ISOM_BOX_TYPE_ULST = ISOM_4CC( 'u', 'l', 's', 't' ),
    ISOM_BOX_TYPE_URL  = ISOM_4CC( 'u', 'r', 'l', ' ' ),
    ISOM_BOX_TYPE_URN  = ISOM_4CC( 'u', 'r', 'n', ' ' ),
    ISOM_BOX_TYPE_UUID = ISOM_4CC( 'u', 'u', 'i', 'd' ),
    ISOM_BOX_TYPE_VMHD = ISOM_4CC( 'v', 'm', 'h', 'd' ),
    ISOM_BOX_TYPE_VWDI = ISOM_4CC( 'v', 'w', 'd', 'i' ),
    ISOM_BOX_TYPE_XML  = ISOM_4CC( 'x', 'm', 'l', ' ' ),
    ISOM_BOX_TYPE_YRRC = ISOM_4CC( 'y', 'r', 'r', 'c' ),

    ISOM_BOX_TYPE_AVCC = ISOM_4CC( 'a', 'v', 'c', 'C' ),
    ISOM_BOX_TYPE_BTRT = ISOM_4CC( 'b', 't', 'r', 't' ),
    ISOM_BOX_TYPE_CLAP = ISOM_4CC( 'c', 'l', 'a', 'p' ),
    ISOM_BOX_TYPE_ESDS = ISOM_4CC( 'e', 's', 'd', 's' ),
    ISOM_BOX_TYPE_PASP = ISOM_4CC( 'p', 'a', 's', 'p' ),
    ISOM_BOX_TYPE_STSL = ISOM_4CC( 's', 't', 's', 'l' ),

    ISOM_BOX_TYPE_CHPL = ISOM_4CC( 'c', 'h', 'p', 'l' ),

    ISOM_BOX_TYPE_DAC3 = ISOM_4CC( 'd', 'a', 'c', '3' ),
    ISOM_BOX_TYPE_DAMR = ISOM_4CC( 'd', 'a', 'm', 'r' ),

    ISOM_BOX_TYPE_FTAB = ISOM_4CC( 'f', 't', 'a', 'b' ),
};

enum qt_box_code
{
    QT_BOX_TYPE_CHAN = ISOM_4CC( 'c', 'h', 'a', 'n' ),
    QT_BOX_TYPE_CLEF = ISOM_4CC( 'c', 'l', 'e', 'f' ),
    QT_BOX_TYPE_CLIP = ISOM_4CC( 'c', 'l', 'i', 'p' ),
    QT_BOX_TYPE_COLR = ISOM_4CC( 'c', 'o', 'l', 'r' ),
    QT_BOX_TYPE_CRGN = ISOM_4CC( 'c', 'r', 'g', 'n' ),
    QT_BOX_TYPE_CTAB = ISOM_4CC( 'c', 't', 'a', 'b' ),
    QT_BOX_TYPE_ENOF = ISOM_4CC( 'e', 'n', 'o', 'f' ),
    QT_BOX_TYPE_FRMA = ISOM_4CC( 'f', 'r', 'm', 'a' ),
    QT_BOX_TYPE_GMHD = ISOM_4CC( 'g', 'm', 'h', 'd' ),
    QT_BOX_TYPE_GMIN = ISOM_4CC( 'g', 'm', 'i', 'n' ),
    QT_BOX_TYPE_IMAP = ISOM_4CC( 'i', 'm', 'a', 'p' ),
    QT_BOX_TYPE_KMAT = ISOM_4CC( 'k', 'm', 'a', 't' ),
    QT_BOX_TYPE_LOAD = ISOM_4CC( 'l', 'o', 'a', 'd' ),
    QT_BOX_TYPE_MATT = ISOM_4CC( 'm', 'a', 't', 't' ),
    QT_BOX_TYPE_MP4A = ISOM_4CC( 'm', 'p', '4', 'a' ),
    QT_BOX_TYPE_PNOT = ISOM_4CC( 'p', 'n', 'o', 't' ),
    QT_BOX_TYPE_PROF = ISOM_4CC( 'p', 'r', 'o', 'f' ),
    QT_BOX_TYPE_STPS = ISOM_4CC( 's', 't', 'p', 's' ),
    QT_BOX_TYPE_TAPT = ISOM_4CC( 't', 'a', 'p', 't' ),
    QT_BOX_TYPE_TEXT = ISOM_4CC( 't', 'e', 'x', 't' ),
    QT_BOX_TYPE_WAVE = ISOM_4CC( 'w', 'a', 'v', 'e' ),

    QT_BOX_TYPE_TERMINATOR = 0x00000000,
};

enum isom_handler_type_code
{
    ISOM_HANDLER_TYPE_DATA  = ISOM_4CC( 'd', 'h', 'l', 'r' ),
    ISOM_HANDLER_TYPE_MEDIA = ISOM_4CC( 'm', 'h', 'l', 'r' ),
};

enum isom_media_type_code
{
    ISOM_MEDIA_HANDLER_TYPE_AUDIO = ISOM_4CC( 's', 'o', 'u', 'n' ),
    ISOM_MEDIA_HANDLER_TYPE_VIDEO = ISOM_4CC( 'v', 'i', 'd', 'e' ),
    ISOM_MEDIA_HANDLER_TYPE_HINT  = ISOM_4CC( 'h', 'i', 'n', 't' ),
    ISOM_MEDIA_HANDLER_TYPE_META  = ISOM_4CC( 'm', 'e', 't', 'a' ),
    ISOM_MEDIA_HANDLER_TYPE_TEXT  = ISOM_4CC( 't', 'e', 'x', 't' ),
};

enum isom_data_reference_type_code
{
    ISOM_REFERENCE_HANDLER_TYPE_ALIAS    = ISOM_4CC( 'a', 'l', 'i', 's' ),
    ISOM_REFERENCE_HANDLER_TYPE_RESOURCE = ISOM_4CC( 'r', 's', 'r', 'c' ),
    ISOM_REFERENCE_HANDLER_TYPE_URL      = ISOM_4CC( 'u', 'r', 'l', ' ' ),
};

enum isom_brand_code
{
    ISOM_BRAND_TYPE_3G2A  = ISOM_4CC( '3', 'g', '2', 'a' ),
    ISOM_BRAND_TYPE_3GE6  = ISOM_4CC( '3', 'g', 'e', '6' ),
    ISOM_BRAND_TYPE_3GG6  = ISOM_4CC( '3', 'g', 'g', '6' ),
    ISOM_BRAND_TYPE_3GP4  = ISOM_4CC( '3', 'g', 'p', '4' ),
    ISOM_BRAND_TYPE_3GP5  = ISOM_4CC( '3', 'g', 'p', '5' ),
    ISOM_BRAND_TYPE_3GP6  = ISOM_4CC( '3', 'g', 'p', '6' ),
    ISOM_BRAND_TYPE_3GR6  = ISOM_4CC( '3', 'g', 'r', '6' ),
    ISOM_BRAND_TYPE_3GS6  = ISOM_4CC( '3', 'g', 's', '6' ),
    ISOM_BRAND_TYPE_CAEP  = ISOM_4CC( 'C', 'A', 'E', 'P' ),
    ISOM_BRAND_TYPE_CDES  = ISOM_4CC( 'C', 'D', 'e', 's' ),
    ISOM_BRAND_TYPE_M4A   = ISOM_4CC( 'M', '4', 'A', ' ' ),
    ISOM_BRAND_TYPE_M4B   = ISOM_4CC( 'M', '4', 'B', ' ' ),
    ISOM_BRAND_TYPE_M4P   = ISOM_4CC( 'M', '4', 'P', ' ' ),
    ISOM_BRAND_TYPE_M4V   = ISOM_4CC( 'M', '4', 'V', ' ' ),
    ISOM_BRAND_TYPE_MPPI  = ISOM_4CC( 'M', 'P', 'P', 'I' ),
    ISOM_BRAND_TYPE_ROSS  = ISOM_4CC( 'R', 'O', 'S', 'S' ),
    ISOM_BRAND_TYPE_AVC1  = ISOM_4CC( 'a', 'v', 'c', '1' ),
    ISOM_BRAND_TYPE_CAQV  = ISOM_4CC( 'c', 'a', 'q', 'v' ),
    ISOM_BRAND_TYPE_DA0A  = ISOM_4CC( 'd', 'a', '0', 'a' ),
    ISOM_BRAND_TYPE_DA0B  = ISOM_4CC( 'd', 'a', '0', 'b' ),
    ISOM_BRAND_TYPE_DA1A  = ISOM_4CC( 'd', 'a', '1', 'a' ),
    ISOM_BRAND_TYPE_DA1B  = ISOM_4CC( 'd', 'a', '1', 'b' ),
    ISOM_BRAND_TYPE_DA2A  = ISOM_4CC( 'd', 'a', '2', 'a' ),
    ISOM_BRAND_TYPE_DA2B  = ISOM_4CC( 'd', 'a', '2', 'b' ),
    ISOM_BRAND_TYPE_DA3A  = ISOM_4CC( 'd', 'a', '3', 'a' ),
    ISOM_BRAND_TYPE_DA3B  = ISOM_4CC( 'd', 'a', '3', 'b' ),
    ISOM_BRAND_TYPE_DMB1  = ISOM_4CC( 'd', 'm', 'b', '1' ),
    ISOM_BRAND_TYPE_DV1A  = ISOM_4CC( 'd', 'v', '1', 'a' ),
    ISOM_BRAND_TYPE_DV1B  = ISOM_4CC( 'd', 'v', '1', 'b' ),
    ISOM_BRAND_TYPE_DV2A  = ISOM_4CC( 'd', 'v', '2', 'a' ),
    ISOM_BRAND_TYPE_DV2B  = ISOM_4CC( 'd', 'v', '2', 'b' ),
    ISOM_BRAND_TYPE_DV3A  = ISOM_4CC( 'd', 'v', '3', 'a' ),
    ISOM_BRAND_TYPE_DV3B  = ISOM_4CC( 'd', 'v', '3', 'b' ),
    ISOM_BRAND_TYPE_DVR1  = ISOM_4CC( 'd', 'v', 'r', '1' ),
    ISOM_BRAND_TYPE_DVT1  = ISOM_4CC( 'd', 'v', 't', '1' ),
    ISOM_BRAND_TYPE_ISC2  = ISOM_4CC( 'i', 's', 'c', '2' ),
    ISOM_BRAND_TYPE_ISO2  = ISOM_4CC( 'i', 's', 'o', '2' ),
    ISOM_BRAND_TYPE_ISO3  = ISOM_4CC( 'i', 's', 'o', '3' ),
    ISOM_BRAND_TYPE_ISO4  = ISOM_4CC( 'i', 's', 'o', '4' ),
    ISOM_BRAND_TYPE_ISOM  = ISOM_4CC( 'i', 's', 'o', 'm' ),
    ISOM_BRAND_TYPE_JPSI  = ISOM_4CC( 'j', 'p', 's', 'i' ),
    ISOM_BRAND_TYPE_MJ2S  = ISOM_4CC( 'm', 'j', '2', 'j' ),
    ISOM_BRAND_TYPE_MJP2  = ISOM_4CC( 'm', 'j', 'p', '2' ),
    ISOM_BRAND_TYPE_MP21  = ISOM_4CC( 'm', 'p', '2', '1' ),
    ISOM_BRAND_TYPE_MP41  = ISOM_4CC( 'm', 'p', '4', '1' ),
    ISOM_BRAND_TYPE_MP42  = ISOM_4CC( 'm', 'p', '4', '2' ),
    ISOM_BRAND_TYPE_MP71  = ISOM_4CC( 'm', 'p', '7', '1' ),
    ISOM_BRAND_TYPE_NIKO  = ISOM_4CC( 'n', 'i', 'k', 'o' ),
    ISOM_BRAND_TYPE_ODCF  = ISOM_4CC( 'o', 'd', 'c', 'f' ),
    ISOM_BRAND_TYPE_OPF2  = ISOM_4CC( 'o', 'p', 'f', '2' ),
    ISOM_BRAND_TYPE_OPX2  = ISOM_4CC( 'o', 'p', 'x', '2' ),
    ISOM_BRAND_TYPE_PANA  = ISOM_4CC( 'p', 'a', 'n', 'a' ),
    ISOM_BRAND_TYPE_QT    = ISOM_4CC( 'q', 't', ' ', ' ' ),
    ISOM_BRAND_TYPE_SDV   = ISOM_4CC( 's', 'd', 'v', ' ' ),
};

enum isom_codec_code
{
    /* Audio Type */
    ISOM_CODEC_TYPE_AC_3_AUDIO = ISOM_4CC( 'a', 'c', '-', '3' ),    /* AC-3 audio */
    ISOM_CODEC_TYPE_ALAC_AUDIO = ISOM_4CC( 'a', 'l', 'a', 'c' ),    /* Apple lossless audio codec */
    ISOM_CODEC_TYPE_DRA1_AUDIO = ISOM_4CC( 'd', 'r', 'a', '1' ),    /* DRA Audio */
    ISOM_CODEC_TYPE_DTSC_AUDIO = ISOM_4CC( 'd', 't', 's', 'c' ),    /* DTS Coherent Acoustics audio */
    ISOM_CODEC_TYPE_DTSH_AUDIO = ISOM_4CC( 'd', 't', 's', 'h' ),    /* DTS-HD High Resolution Audio */
    ISOM_CODEC_TYPE_DTSL_AUDIO = ISOM_4CC( 'd', 't', 's', 'l' ),    /* DTS-HD Master Audio */
    ISOM_CODEC_TYPE_DTSE_AUDIO = ISOM_4CC( 'd', 't', 's', 'e' ),    /* DTS Express low bit rate audio, also known as DTS LBR */
    ISOM_CODEC_TYPE_EC_3_AUDIO = ISOM_4CC( 'e', 'c', '-', '3' ),    /* Enhanced AC-3 audio */
    ISOM_CODEC_TYPE_ENCA_AUDIO = ISOM_4CC( 'e', 'n', 'c', 'a' ),    /* Encrypted/Protected audio */
    ISOM_CODEC_TYPE_G719_AUDIO = ISOM_4CC( 'g', '7', '1', '9' ),    /* ITU-T Recommendation G.719 (2008) */
    ISOM_CODEC_TYPE_G726_AUDIO = ISOM_4CC( 'g', '7', '2', '6' ),    /* ITU-T Recommendation G.726 (1990) */
    ISOM_CODEC_TYPE_M4AE_AUDIO = ISOM_4CC( 'm', '4', 'a', 'e' ),    /* MPEG-4 Audio Enhancement */
    ISOM_CODEC_TYPE_MLPA_AUDIO = ISOM_4CC( 'm', 'l', 'p', 'a' ),    /* MLP Audio */
    ISOM_CODEC_TYPE_MP4A_AUDIO = ISOM_4CC( 'm', 'p', '4', 'a' ),    /* MPEG-4 Audio */
    ISOM_CODEC_TYPE_RAW_AUDIO  = ISOM_4CC( 'r', 'a', 'w', ' ' ),    /* Uncompressed audio */
    ISOM_CODEC_TYPE_SAMR_AUDIO = ISOM_4CC( 's', 'a', 'm', 'r' ),    /* Narrowband AMR voice */
    ISOM_CODEC_TYPE_SAWB_AUDIO = ISOM_4CC( 's', 'a', 'w', 'b' ),    /* Wideband AMR voice */
    ISOM_CODEC_TYPE_SAWP_AUDIO = ISOM_4CC( 's', 'a', 'w', 'p' ),    /* Extended AMR-WB (AMR-WB+) */
    ISOM_CODEC_TYPE_SEVC_AUDIO = ISOM_4CC( 's', 'e', 'v', 'c' ),    /* EVRC Voice */
    ISOM_CODEC_TYPE_SQCP_AUDIO = ISOM_4CC( 's', 'q', 'c', 'p' ),    /* 13K Voice */
    ISOM_CODEC_TYPE_SSMV_AUDIO = ISOM_4CC( 's', 's', 'm', 'v' ),    /* SMV Voice */
    ISOM_CODEC_TYPE_TWOS_AUDIO = ISOM_4CC( 't', 'w', 'o', 's' ),    /* Uncompressed 16-bit audio */

    /* Video Type */
    ISOM_CODEC_TYPE_AVC1_VIDEO = ISOM_4CC( 'a', 'v', 'c', '1' ),    /* Advanced Video Coding */
    ISOM_CODEC_TYPE_AVC2_VIDEO = ISOM_4CC( 'a', 'v', 'c', '2' ),    /* Advanced Video Coding */
    ISOM_CODEC_TYPE_AVCP_VIDEO = ISOM_4CC( 'a', 'v', 'c', 'p' ),    /* Advanced Video Coding Parameters */
    ISOM_CODEC_TYPE_DRAC_VIDEO = ISOM_4CC( 'd', 'r', 'a', 'c' ),    /* Dirac Video Coder */
    ISOM_CODEC_TYPE_ENCV_VIDEO = ISOM_4CC( 'e', 'n', 'c', 'v' ),    /* Encrypted/protected video */
    ISOM_CODEC_TYPE_MJP2_VIDEO = ISOM_4CC( 'm', 'j', 'p', '2' ),    /* Motion JPEG 2000 */
    ISOM_CODEC_TYPE_MP4V_VIDEO = ISOM_4CC( 'm', 'p', '4', 'v' ),    /* MPEG-4 Visual */
    ISOM_CODEC_TYPE_MVC1_VIDEO = ISOM_4CC( 'm', 'v', 'c', '1' ),    /* Multiview coding */
    ISOM_CODEC_TYPE_MVC2_VIDEO = ISOM_4CC( 'm', 'v', 'c', '2' ),    /* Multiview coding */
    ISOM_CODEC_TYPE_S263_VIDEO = ISOM_4CC( 's', '2', '6', '3' ),    /* ITU H.263 video (3GPP format) */
    ISOM_CODEC_TYPE_SVC1_VIDEO = ISOM_4CC( 's', 'v', 'c', '1' ),    /* Scalable Video Coding */
    ISOM_CODEC_TYPE_VC_1_VIDEO = ISOM_4CC( 'v', 'c', '-', '1' ),    /* SMPTE VC-1 */

    /* Hint Type */
    ISOM_CODEC_TYPE_FDP_HINT  = ISOM_4CC( 'f', 'd', 'p', ' ' ),     /* File delivery hints */
    ISOM_CODEC_TYPE_M2TS_HINT = ISOM_4CC( 'm', '2', 't', 's' ),     /* MPEG-2 transport stream for DMB */
    ISOM_CODEC_TYPE_PM2T_HINT = ISOM_4CC( 'p', 'm', '2', 't' ),     /* Protected MPEG-2 Transport */
    ISOM_CODEC_TYPE_PRTP_HINT = ISOM_4CC( 'p', 'r', 't', 'p' ),     /* Protected RTP Reception */
    ISOM_CODEC_TYPE_RM2T_HINT = ISOM_4CC( 'r', 'm', '2', 't' ),     /* MPEG-2 Transport Reception */
    ISOM_CODEC_TYPE_RRTP_HINT = ISOM_4CC( 'r', 'r', 't', 'p' ),     /* RTP reception */
    ISOM_CODEC_TYPE_RSRP_HINT = ISOM_4CC( 'r', 's', 'r', 'p' ),     /* SRTP Reception */
    ISOM_CODEC_TYPE_RTP_HINT  = ISOM_4CC( 'r', 't', 'p', ' ' ),     /* RTP Hints */
    ISOM_CODEC_TYPE_SM2T_HINT = ISOM_4CC( 's', 'm', '2', 't' ),     /* MPEG-2 Transport Server */
    ISOM_CODEC_TYPE_SRTP_HINT = ISOM_4CC( 's', 'r', 't', 'p' ),     /* SRTP Hints */

    /* Metadata Type */
    ISOM_CODEC_TYPE_IXSE_META = ISOM_4CC( 'i', 'x', 's', 'e' ),     /* DVB Track Level Index Track */
    ISOM_CODEC_TYPE_METT_META = ISOM_4CC( 'm', 'e', 't', 't' ),     /* Text timed metadata */
    ISOM_CODEC_TYPE_METX_META = ISOM_4CC( 'm', 'e', 't', 'x' ),     /* XML timed metadata */
    ISOM_CODEC_TYPE_MLIX_META = ISOM_4CC( 'm', 'l', 'i', 'x' ),     /* DVB Movie level index track */
    ISOM_CODEC_TYPE_OKSD_META = ISOM_4CC( 'o', 'k', 's', 'd' ),     /* OMA Keys */
    ISOM_CODEC_TYPE_SVCM_META = ISOM_4CC( 's', 'v', 'c', 'M' ),     /* SVC metadata */
    ISOM_CODEC_TYPE_TEXT_META = ISOM_4CC( 't', 'e', 'x', 't' ),     /* Textual meta-data with MIME type */
    ISOM_CODEC_TYPE_URIM_META = ISOM_4CC( 'u', 'r', 'i', 'm' ),     /* URI identified timed metadata */
    ISOM_CODEC_TYPE_XML_META  = ISOM_4CC( 'x', 'm', 'l', ' ' ),     /* XML-formatted meta-data */

    /* Other Type */
    ISOM_CODEC_TYPE_ENCS_SYSTEM = ISOM_4CC( 'e', 'n', 'c', 's' ),   /* Encrypted Systems stream */
    ISOM_CODEC_TYPE_MP4S_SYSTEM = ISOM_4CC( 'm', 'p', '4', 's' ),   /* MPEG-4 Systems */
    ISOM_CODEC_TYPE_ENCT_TEXT   = ISOM_4CC( 'e', 'n', 'c', 't' ),   /* Encrypted Text */
    ISOM_CODEC_TYPE_TX3G_TEXT   = ISOM_4CC( 't', 'x', '3', 'g' ),   /* Timed Text stream */
};

enum qt_codec_code
{
    /* Audio Type */
    QT_CODEC_TYPE_QDM2_AUDIO = ISOM_4CC( 'Q', 'D', 'M', '2' ),
    QT_CODEC_TYPE_QDMC_AUDIO = ISOM_4CC( 'Q', 'D', 'M', 'C' ),
    QT_CODEC_TYPE_QCLP_AUDIO = ISOM_4CC( 'Q', 'c', 'l', 'p' ),
    QT_CODEC_TYPE_AGSM_AUDIO = ISOM_4CC( 'a', 'g', 's', 'm' ),
    QT_CODEC_TYPE_ALAW_AUDIO = ISOM_4CC( 'a', 'l', 'a', 'w' ),
    QT_CODEC_TYPE_DVI_AUDIO  = ISOM_4CC( 'd', 'v', 'i', ' ' ),
    QT_CODEC_TYPE_FL32_AUDIO = ISOM_4CC( 'f', 'l', '3', '2' ),
    QT_CODEC_TYPE_FL64_AUDIO = ISOM_4CC( 'f', 'l', '6', '4' ),
    QT_CODEC_TYPE_IMA4_AUDIO = ISOM_4CC( 'i', 'm', 'a', '4' ),
    QT_CODEC_TYPE_IN24_AUDIO = ISOM_4CC( 'i', 'n', '2', '4' ),
    QT_CODEC_TYPE_IN32_AUDIO = ISOM_4CC( 'i', 'n', '3', '2' ),
    QT_CODEC_TYPE_LPCM_AUDIO = ISOM_4CC( 'l', 'p', 'c', 'm' ),
    QT_CODEC_TYPE_ULAW_AUDIO = ISOM_4CC( 'u', 'l', 'a', 'w' ),
    QT_CODEC_TYPE_VDVA_AUDIO = ISOM_4CC( 'v', 'd', 'v', 'a' ),

    /* Video Type */
    QT_CODEC_TYPE_CFHD_VIDEO = ISOM_4CC( 'C', 'F', 'H', 'D' ),
    QT_CODEC_TYPE_DV10_VIDEO = ISOM_4CC( 'D', 'V', '1', '0' ),
    QT_CODEC_TYPE_DVOO_VIDEO = ISOM_4CC( 'D', 'V', 'O', 'O' ),
    QT_CODEC_TYPE_DVOR_VIDEO = ISOM_4CC( 'D', 'V', 'O', 'R' ),
    QT_CODEC_TYPE_DVTV_VIDEO = ISOM_4CC( 'D', 'V', 'T', 'V' ),
    QT_CODEC_TYPE_DVVT_VIDEO = ISOM_4CC( 'D', 'V', 'V', 'T' ),
    QT_CODEC_TYPE_HD10_VIDEO = ISOM_4CC( 'H', 'D', '1', '0' ),
    QT_CODEC_TYPE_M105_VIDEO = ISOM_4CC( 'M', '1', '0', '5' ),
    QT_CODEC_TYPE_PNTG_VIDEO = ISOM_4CC( 'P', 'N', 'T', 'G' ),
    QT_CODEC_TYPE_SVQ1_VIDEO = ISOM_4CC( 'S', 'V', 'Q', '1' ),
    QT_CODEC_TYPE_SVQ3_VIDEO = ISOM_4CC( 'S', 'V', 'Q', '3' ),
    QT_CODEC_TYPE_SHR0_VIDEO = ISOM_4CC( 'S', 'h', 'r', '0' ),
    QT_CODEC_TYPE_SHR1_VIDEO = ISOM_4CC( 'S', 'h', 'r', '1' ),
    QT_CODEC_TYPE_SHR2_VIDEO = ISOM_4CC( 'S', 'h', 'r', '2' ),
    QT_CODEC_TYPE_SHR3_VIDEO = ISOM_4CC( 'S', 'h', 'r', '3' ),
    QT_CODEC_TYPE_SHR4_VIDEO = ISOM_4CC( 'S', 'h', 'r', '4' ),
    QT_CODEC_TYPE_WRLE_VIDEO = ISOM_4CC( 'W', 'R', 'L', 'E' ),
    QT_CODEC_TYPE_CIVD_VIDEO = ISOM_4CC( 'c', 'i', 'v', 'd' ),
    QT_CODEC_TYPE_DRAC_VIDEO = ISOM_4CC( 'd', 'r', 'a', 'c' ),
    QT_CODEC_TYPE_DVH5_VIDEO = ISOM_4CC( 'd', 'v', 'h', '5' ),
    QT_CODEC_TYPE_DVH6_VIDEO = ISOM_4CC( 'd', 'v', 'h', '6' ),
    QT_CODEC_TYPE_DVHP_VIDEO = ISOM_4CC( 'd', 'v', 'h', 'p' ),
    QT_CODEC_TYPE_FLIC_VIDEO = ISOM_4CC( 'f', 'l', 'i', 'c' ),
    QT_CODEC_TYPE_GIF_VIDEO  = ISOM_4CC( 'g', 'i', 'f', ' ' ),
    QT_CODEC_TYPE_H261_VIDEO = ISOM_4CC( 'h', '2', '6', '1' ),
    QT_CODEC_TYPE_H263_VIDEO = ISOM_4CC( 'h', '2', '6', '3' ),
    QT_CODEC_TYPE_JPEG_VIDEO = ISOM_4CC( 'j', 'p', 'e', 'g' ),
    QT_CODEC_TYPE_MJPA_VIDEO = ISOM_4CC( 'm', 'j', 'p', 'a' ),
    QT_CODEC_TYPE_MJPB_VIDEO = ISOM_4CC( 'm', 'j', 'p', 'b' ),
    QT_CODEC_TYPE_PNG_VIDEO  = ISOM_4CC( 'p', 'n', 'g', ' ' ),
    QT_CODEC_TYPE_RLE_VIDEO  = ISOM_4CC( 'r', 'l', 'e', ' ' ),
    QT_CODEC_TYPE_RPZA_VIDEO = ISOM_4CC( 'r', 'p', 'z', 'a' ),
    QT_CODEC_TYPE_TGA_VIDEO  = ISOM_4CC( 't', 'g', 'a', ' ' ),
    QT_CODEC_TYPE_TIFF_VIDEO = ISOM_4CC( 't', 'i', 'f', 'f' ),

    /* Text Type */
    QT_CODEC_TYPE_TEXT_TEXT = ISOM_4CC( 't', 'e', 'x', 't' ),
};

enum isom_track_reference_code
{
    ISOM_TREF_TYPE_AVCP = ISOM_4CC( 'a', 'v', 'c', 'p' ),
    ISOM_TREF_TYPE_CDSC = ISOM_4CC( 'c', 'd', 's', 'c' ),
    ISOM_TREF_TYPE_DPND = ISOM_4CC( 'd', 'p', 'n', 'd' ),
    ISOM_TREF_TYPE_HIND = ISOM_4CC( 'h', 'i', 'n', 'd' ),
    ISOM_TREF_TYPE_HINT = ISOM_4CC( 'h', 'i', 'n', 't' ),
    ISOM_TREF_TYPE_IPIR = ISOM_4CC( 'i', 'p', 'i', 'r' ),
    ISOM_TREF_TYPE_MPOD = ISOM_4CC( 'm', 'p', 'o', 'd' ),
    ISOM_TREF_TYPE_SBAS = ISOM_4CC( 's', 'b', 'a', 's' ),
    ISOM_TREF_TYPE_SCAL = ISOM_4CC( 's', 'c', 'a', 'l' ),
    ISOM_TREF_TYPE_SWFR = ISOM_4CC( 's', 'w', 'f', 'r' ),
    ISOM_TREF_TYPE_SYNC = ISOM_4CC( 's', 'y', 'n', 'c' ),
    ISOM_TREF_TYPE_VDEP = ISOM_4CC( 'v', 'd', 'e', 'p' ),
    ISOM_TREF_TYPE_VPLX = ISOM_4CC( 'v', 'p', 'l', 'x' ),
};

enum qt_track_reference_code
{
    QT_TREF_TYPE_CHAP = ISOM_4CC( 'c', 'h', 'a', 'p' ),
    QT_TREF_TYPE_SCPT = ISOM_4CC( 's', 'c', 'p', 't' ),
    QT_TREF_TYPE_SSRC = ISOM_4CC( 's', 's', 'r', 'c' ),
    QT_TREF_TYPE_TMCD = ISOM_4CC( 't', 'm', 'c', 'd' ),
};

enum isom_grouping_code
{
    ISOM_GROUP_TYPE_AVLL = ISOM_4CC( 'a', 'v', 'l', 'l' ),      /* AVC Layer */
    ISOM_GROUP_TYPE_AVSS = ISOM_4CC( 'a', 'v', 's', 's' ),      /* AVC Sub Sequence */
    ISOM_GROUP_TYPE_RASH = ISOM_4CC( 'r', 'a', 's', 'h' ),      /* Rate Share */
    ISOM_GROUP_TYPE_ROLL = ISOM_4CC( 'r', 'o', 'l', 'l' ),      /* Roll Recovery */
};

#define ISOM_LANG_T( a, b, c ) ((((a-0x60)&0x1f)<<10) | (((b-0x60)&0x1f)<<5) | ((c-0x60)&0x1f))

enum isom_language_code
{
    ISOM_LANGUAGE_CODE_ENGLISH    = ISOM_LANG_T( 'e', 'n', 'g' ),
    ISOM_LANGUAGE_CODE_FRENCH     = ISOM_LANG_T( 'f', 'r', 'a' ),
    ISOM_LANGUAGE_CODE_GERMAN     = ISOM_LANG_T( 'd', 'e', 'u' ),
    ISOM_LANGUAGE_CODE_ITALIAN    = ISOM_LANG_T( 'i', 't', 'a' ),
    ISOM_LANGUAGE_CODE_DUTCH_M    = ISOM_LANG_T( 'd', 'u', 'm' ),
    ISOM_LANGUAGE_CODE_SWEDISH    = ISOM_LANG_T( 's', 'w', 'e' ),
    ISOM_LANGUAGE_CODE_SPANISH    = ISOM_LANG_T( 's', 'p', 'a' ),
    ISOM_LANGUAGE_CODE_DANISH     = ISOM_LANG_T( 'd', 'a', 'n' ),
    ISOM_LANGUAGE_CODE_PORTUGUESE = ISOM_LANG_T( 'p', 'o', 'r' ),
    ISOM_LANGUAGE_CODE_NORWEGIAN  = ISOM_LANG_T( 'n', 'o', 'r' ),
    ISOM_LANGUAGE_CODE_HEBREW     = ISOM_LANG_T( 'h', 'e', 'b' ),
    ISOM_LANGUAGE_CODE_JAPANESE   = ISOM_LANG_T( 'j', 'p', 'n' ),
    ISOM_LANGUAGE_CODE_ARABIC     = ISOM_LANG_T( 'a', 'r', 'a' ),
    ISOM_LANGUAGE_CODE_FINNISH    = ISOM_LANG_T( 'f', 'i', 'n' ),
    ISOM_LANGUAGE_CODE_GREEK      = ISOM_LANG_T( 'e', 'l', 'l' ),
    ISOM_LANGUAGE_CODE_ICELANDIC  = ISOM_LANG_T( 'i', 's', 'l' ),
    ISOM_LANGUAGE_CODE_MALTESE    = ISOM_LANG_T( 'm', 'l', 't' ),
    ISOM_LANGUAGE_CODE_TURKISH    = ISOM_LANG_T( 't', 'u', 'r' ),
    ISOM_LANGUAGE_CODE_CROATIAN   = ISOM_LANG_T( 'h', 'r', 'v' ),
    ISOM_LANGUAGE_CODE_CHINESE    = ISOM_LANG_T( 'z', 'h', 'o' ),
    ISOM_LANGUAGE_CODE_URDU       = ISOM_LANG_T( 'u', 'r', 'd' ),
    ISOM_LANGUAGE_CODE_HINDI      = ISOM_LANG_T( 'h', 'i', 'n' ),
    ISOM_LANGUAGE_CODE_THAI       = ISOM_LANG_T( 't', 'h', 'a' ),
    ISOM_LANGUAGE_CODE_KOREAN     = ISOM_LANG_T( 'k', 'o', 'r' ),
    ISOM_LANGUAGE_CODE_LITHUANIAN = ISOM_LANG_T( 'l', 'i', 't' ),
    ISOM_LANGUAGE_CODE_POLISH     = ISOM_LANG_T( 'p', 'o', 'l' ),
    ISOM_LANGUAGE_CODE_HUNGARIAN  = ISOM_LANG_T( 'h', 'u', 'n' ),
    ISOM_LANGUAGE_CODE_ESTONIAN   = ISOM_LANG_T( 'e', 's', 't' ),
    ISOM_LANGUAGE_CODE_LATVIAN    = ISOM_LANG_T( 'l', 'a', 'v' ),
    ISOM_LANGUAGE_CODE_SAMI       = ISOM_LANG_T( 's', 'm', 'i' ),
    ISOM_LANGUAGE_CODE_FAROESE    = ISOM_LANG_T( 'f', 'a', 'o' ),
    ISOM_LANGUAGE_CODE_RUSSIAN    = ISOM_LANG_T( 'r', 'u', 's' ),
    ISOM_LANGUAGE_CODE_DUTCH      = ISOM_LANG_T( 'n', 'l', 'd' ),
    ISOM_LANGUAGE_CODE_IRISH      = ISOM_LANG_T( 'g', 'l', 'e' ),
    ISOM_LANGUAGE_CODE_ALBANIAN   = ISOM_LANG_T( 's', 'q', 'i' ),
    ISOM_LANGUAGE_CODE_ROMANIAN   = ISOM_LANG_T( 'r', 'o', 'n' ),
    ISOM_LANGUAGE_CODE_CZECH      = ISOM_LANG_T( 'c', 'e', 's' ),
    ISOM_LANGUAGE_CODE_SLOVAK     = ISOM_LANG_T( 's', 'l', 'k' ),
    ISOM_LANGUAGE_CODE_SLOVENIA   = ISOM_LANG_T( 's', 'l', 'v' ),
    ISOM_LANGUAGE_CODE_UNDEFINED  = ISOM_LANG_T( 'u', 'n', 'd' ),
};

#undef ISOM_LANG_T

enum qt_color_patameter_type_code
{
    QT_COLOR_PARAMETER_TYPE_NCLC = ISOM_4CC( 'n', 'c', 'l', 'c' ),      /* nonconstant luminance coding */
    QT_COLOR_PARAMETER_TYPE_PROF = ISOM_4CC( 'p', 'r', 'o', 'f' ),      /* ICC profile */
};

enum qt_color_parameter_table
{
#define UINT16_MAX_PLUS_ONE 0x10000
    QT_COLOR_PARAMETER_NOT_SPECIFIED = UINT16_MAX_PLUS_ONE,
    QT_COLOR_PARAMETER_ITU_R_BT470_M,
    QT_COLOR_PARAMETER_ITU_R_BT470_BG,
    QT_COLOR_PARAMETER_ITU_R_BT709,
    QT_COLOR_PARAMETER_SMPTE_170M,
    QT_COLOR_PARAMETER_SMPTE_240M,
    QT_COLOR_PARAMETER_SMPTE_274M,
    QT_COLOR_PARAMETER_SMPTE_293M,
    QT_COLOR_PARAMETER_SMPTE_296M,
    QT_COLOR_PARAMETER_END,
};

enum qt_channel_label_code
{
    QT_CHANNEL_LABEL_UNKNOWN                = 0xffffffff,   /* unknown or unspecified other use */
    QT_CHANNEL_LABEL_UNUSED                 = 0,            /* channel is present, but has no intended use or destination */
    QT_CHANNEL_LABEL_USE_COORDINATES        = 100,          /* channel is described by the coordinates fields. */

    QT_CHANNEL_LABEL_LEFT                   = 1,
    QT_CHANNEL_LABEL_RIGHT                  = 2,
    QT_CHANNEL_LABEL_CENTER                 = 3,
    QT_CHANNEL_LABEL_LFE_SCREEN             = 4,
    QT_CHANNEL_LABEL_LEFT_SURROUND          = 5,            /* WAVE: "Back Left" */
    QT_CHANNEL_LABEL_RIGHT_SUROUND          = 6,            /* WAVE: "Back Right" */
    QT_CHANNEL_LABEL_LEFT_CENTER            = 7,
    QT_CHANNEL_LABEL_RIGHT_CENTER           = 8,
    QT_CHANNEL_LABEL_CENTER_SURROUND        = 9,            /* WAVE: "Back Center" or plain "Rear Surround" */
    QT_CHANNEL_LABEL_LEFT_SURROUND_DIRECT   = 10,           /* WAVE: "Side Left" */
    QT_CHANNEL_LABEL_RIGHT_SURROUND_DIRECT  = 11,           /* WAVE: "Side Right" */
    QT_CHANNEL_LABEL_TOP_CENTER_SURROUND    = 12,
    QT_CHANNEL_LABEL_VERTICAL_HEIGHT_LEFT   = 13,           /* WAVE: "Top Front Left" */
    QT_CHANNEL_LABEL_VERTICAL_HEIGHT_CENTER = 14,           /* WAVE: "Top Front Center" */
    QT_CHANNEL_LABEL_VERTICAL_HEIGHT_RIGHT  = 15,           /* WAVE: "Top Front Right" */

    QT_CHANNEL_LABEL_TOP_BACK_LEFT          = 16,
    QT_CHANNEL_LABEL_TOP_BACK_CENTER        = 17,
    QT_CHANNEL_LABEL_TOP_BACK_RIGHT         = 18,

    QT_CHANNEL_LABEL_REAR_SURROUND_LEFT     = 33,
    QT_CHANNEL_LABEL_REAR_SURROUND_RIGHT    = 34,
    QT_CHANNEL_LABEL_LEFT_WIDE              = 35,
    QT_CHANNEL_LABEL_RIGHT_WIDE             = 36,
    QT_CHANNEL_LABEL_LFE2                   = 37,
    QT_CHANNEL_LABEL_LEFT_TOTAL             = 38,           /* matrix encoded 4 channels */
    QT_CHANNEL_LABEL_RIGHT_TOTAL            = 39,           /* matrix encoded 4 channels */
    QT_CHANNEL_LABEL_HEARING_IMPAIRED       = 40,
    QT_CHANNEL_LABEL_NARRATION              = 41,
    QT_CHANNEL_LABEL_MONO                   = 42,
    QT_CHANNEL_LABEL_DIALOG_CENTRIC_MIX     = 43,

    QT_CHANNEL_LABEL_CENTER_SURROUND_DIRECT = 44,           /* back center, non diffuse */

    QT_CHANNEL_LABEL_HAPTIC                 = 45,

    /* first order ambisonic channels */
    QT_CHANNEL_LABEL_AMBISONIC_W            = 200,
    QT_CHANNEL_LABEL_AMBISONIC_X            = 201,
    QT_CHANNEL_LABEL_AMBISONIC_Y            = 202,
    QT_CHANNEL_LABEL_AMBISONIC_Z            = 203,

    /* Mid/Side Recording */
    QT_CHANNEL_LABEL_MS_MID                 = 204,
    QT_CHANNEL_LABEL_MS_SIDE                = 205,

    /* X-Y Recording */
    QT_CHANNEL_LABEL_XY_X                   = 206,
    QT_CHANNEL_LABEL_XY_Y                   = 207,

    /* other */
    QT_CHANNEL_LABEL_HEADPHONES_LEFT        = 301,
    QT_CHANNEL_LABEL_HEADPHONES_RIGHT       = 302,
    QT_CHANNEL_LABEL_CLICK_TRACK            = 304,
    QT_CHANNEL_LABEL_FOREIGN_LANGUAGE       = 305,

    /* generic discrete channel */
    QT_CHANNEL_LABEL_DISCRETE               = 400,

    /* numbered discrete channel */
    QT_CHANNEL_LABEL_DISCRETE_0             = (1<<16),
    QT_CHANNEL_LABEL_DISCRETE_1             = (1<<16) | 1,
    QT_CHANNEL_LABEL_DISCRETE_2             = (1<<16) | 2,
    QT_CHANNEL_LABEL_DISCRETE_3             = (1<<16) | 3,
    QT_CHANNEL_LABEL_DISCRETE_4             = (1<<16) | 4,
    QT_CHANNEL_LABEL_DISCRETE_5             = (1<<16) | 5,
    QT_CHANNEL_LABEL_DISCRETE_6             = (1<<16) | 6,
    QT_CHANNEL_LABEL_DISCRETE_7             = (1<<16) | 7,
    QT_CHANNEL_LABEL_DISCRETE_8             = (1<<16) | 8,
    QT_CHANNEL_LABEL_DISCRETE_9             = (1<<16) | 9,
    QT_CHANNEL_LABEL_DISCRETE_10            = (1<<16) | 10,
    QT_CHANNEL_LABEL_DISCRETE_11            = (1<<16) | 11,
    QT_CHANNEL_LABEL_DISCRETE_12            = (1<<16) | 12,
    QT_CHANNEL_LABEL_DISCRETE_13            = (1<<16) | 13,
    QT_CHANNEL_LABEL_DISCRETE_14            = (1<<16) | 14,
    QT_CHANNEL_LABEL_DISCRETE_15            = (1<<16) | 15,
    QT_CHANNEL_LABEL_DISCRETE_65535         = (1<<16) | 65535,
};

enum qt_channel_bitmap_code
{
    QT_CHANNEL_BIT_LEFT                   = 1,
    QT_CHANNEL_BIT_RIGHT                  = 1<<1,
    QT_CHANNEL_BIT_CENTER                 = 1<<2,
    QT_CHANNEL_BIT_LFE_SCREEN             = 1<<3,
    QT_CHANNEL_BIT_LEFT_SURROUND          = 1<<4,       /* WAVE: "Back Left" */
    QT_CHANNEL_BIT_RIGHT_SURROUND         = 1<<5,       /* WAVE: "Back Right" */
    QT_CHANNEL_BIT_LEFT_CENTER            = 1<<6,
    QT_CHANNEL_BIT_RIGHT_CENTER           = 1<<7,
    QT_CHANNEL_BIT_CENTER_SURROUND        = 1<<8,       /* WAVE: "Back Center" */
    QT_CHANNEL_BIT_LEFT_SURROUND_DIRECT   = 1<<9,       /* WAVE: "Side Left" */
    QT_CHANNEL_BIT_RIGHT_SURROUND_DIRECT  = 1<<10,      /* WAVE: "Side Right" */
    QT_CHANNEL_BIT_TOP_CENTER_SURROUND    = 1<<11,
    QT_CHANNEL_BIT_VERTICAL_HEIGHT_LEFT   = 1<<12,      /* WAVE: "Top Front Left" */
    QT_CHANNEL_BIT_VERTICAL_HEIGHT_CENTER = 1<<13,      /* WAVE: "Top Front Center" */
    QT_CHANNEL_BIT_VERTICAL_HEIGHT_RIGHT  = 1<<14,      /* WAVE: "Top Front Right" */
    QT_CHANNEL_BIT_TOP_BACK_LEFT          = 1<<15,
    QT_CHANNEL_BIT_TOP_BACK_CENTER        = 1<<16,
    QT_CHANNEL_BIT_TOP_BACK_RIGHT         = 1<<17,
    QT_CHANNEL_BIT_FULL                   = 0x3ffff,
};

enum qt_channel_flags_code
{
    QT_CHANNEL_FLAGS_ALL_OFF                 = 0,
    QT_CHANNEL_FLAGS_RECTANGULAR_COORDINATES = 1,
    QT_CHANNEL_FLAGS_SPHERICAL_COORDINATES   = 1<<1,
    QT_CHANNEL_FLAGS_METERS                  = 1<<2,
};

enum qt_channel_coordinates_index_code
{
    /* indices for accessing the coordinates array in Channel Descriptions */
    /* for rectangulare coordinates */
    QT_CHANNEL_COORDINATES_LEFT_RIGHT = 0,      /* Negative is left and positive is right. */
    QT_CHANNEL_COORDINATES_BACK_FRONT = 1,      /* Negative is back and positive is front. */
    QT_CHANNEL_COORDINATES_DOWN_UP    = 2,      /* Negative is below ground level, 0 is ground level, and positive is above ground level. */
    /* for spherical coordinates */
    QT_CHANNEL_COORDINATES_AZIMUTH    = 0,      /* 0 is front center, positive is right, negative is left. This is measured in degrees. */
    QT_CHANNEL_COORDINATES_ELEVATION  = 1,      /* +90 is zenith, 0 is horizontal, -90 is nadir. This is measured in degrees. */
    QT_CHANNEL_COORDINATES_DISTANCE   = 2,      /* The units are described by flags. */
};

enum qt_channel_layout_tag_code
{
    /* channel abbreviations:
     * L - left
     * R - right
     * C - center
     * Ls - left surround
     * Rs - right surround
     * Cs - center surround
     * Rls - rear left surround
     * Rrs - rear right surround
     * Lw - left wide
     * Rw - right wide
     * Lsd - left surround direct
     * Rsd - right surround direct
     * Lc - left center
     * Rc - right center
     * Ts - top surround
     * Vhl - vertical height left
     * Vhc - vertical height center
     * Vhr - vertical height right
     * Lt - left matrix total. for matrix encoded stereo.
     * Rt - right matrix total. for matrix encoded stereo. */

    /*  General layouts */
    QT_CHANNEL_LAYOUT_USE_CHANNEL_DESCRIPTIONS = 0,                 /* use the array of Channel Descriptions to define the mapping. */
    QT_CHANNEL_LAYOUT_USE_CHANNEL_BITMAP       = 1<<16,             /* use the bitmap to define the mapping. */

    QT_CHANNEL_LAYOUT_MONO                     = (100<<16) | 1,     /* a standard mono stream */
    QT_CHANNEL_LAYOUT_STEREO                   = (101<<16) | 2,     /* a standard stereo stream (L R) - implied playback */
    QT_CHANNEL_LAYOUT_STEREO_HEADPHONES        = (102<<16) | 2,     /* a standard stereo stream (L R) - implied headphone playback */
    QT_CHANNEL_LAYOUT_MATRIX_STEREO            = (103<<16) | 2,     /* a matrix encoded stereo stream (Lt, Rt) */
    QT_CHANNEL_LAYOUT_MID_SIDE                 = (104<<16) | 2,     /* mid/side recording */
    QT_CHANNEL_LAYOUT_XY                       = (105<<16) | 2,     /* coincident mic pair (often 2 figure 8's) */
    QT_CHANNEL_LAYOUT_BINAURAL                 = (106<<16) | 2,     /* binaural stereo (left, right) */
    QT_CHANNEL_LAYOUT_AMBISONIC_B_FORMAT       = (107<<16) | 4,     /* W, X, Y, Z */

    QT_CHANNEL_LAYOUT_QUADRAPHONIC             = (108<<16) | 4,     /* front left, front right, back left, back right */

    QT_CHANNEL_LAYOUT_PENTAGONAL               = (109<<16) | 5,     /* left, right, rear left, rear right, center */

    QT_CHANNEL_LAYOUT_HEXAGONAL                = (110<<16) | 6,     /* left, right, rear left, rear right, center, rear */

    QT_CHANNEL_LAYOUT_OCTAGONAL                = (111<<16) | 8,     /* front left, front right, rear left, rear right,
                                                                     * front center, rear center, side left, side right */

    QT_CHANNEL_LAYOUT_CUBE                     = (112<<16) | 8,     /* left, right, rear left, rear right,
                                                                     * top left, top right, top rear left, top rear right */

    /*  MPEG defined layouts */
    QT_CHANNEL_LAYOUT_MPEG_1_0                 = QT_CHANNEL_LAYOUT_MONO,            /* C */
    QT_CHANNEL_LAYOUT_MPEG_2_0                 = QT_CHANNEL_LAYOUT_STEREO,          /* L R */
    QT_CHANNEL_LAYOUT_MPEG_3_0_A               = (113<<16) | 3,                     /* L R C */
    QT_CHANNEL_LAYOUT_MPEG_3_0_B               = (114<<16) | 3,                     /* C L R */
    QT_CHANNEL_LAYOUT_MPEG_4_0_A               = (115<<16) | 4,                     /* L R C Cs */
    QT_CHANNEL_LAYOUT_MPEG_4_0_B               = (116<<16) | 4,                     /* C L R Cs */
    QT_CHANNEL_LAYOUT_MPEG_5_0_A               = (117<<16) | 5,                     /* L R C Ls Rs */
    QT_CHANNEL_LAYOUT_MPEG_5_0_B               = (118<<16) | 5,                     /* L R Ls Rs C */
    QT_CHANNEL_LAYOUT_MPEG_5_0_C               = (119<<16) | 5,                     /* L C R Ls Rs */
    QT_CHANNEL_LAYOUT_MPEG_5_0_D               = (120<<16) | 5,                     /* C L R Ls Rs */
    QT_CHANNEL_LAYOUT_MPEG_5_1_A               = (121<<16) | 6,                     /* L R C LFE Ls Rs */
    QT_CHANNEL_LAYOUT_MPEG_5_1_B               = (122<<16) | 6,                     /* L R Ls Rs C LFE */
    QT_CHANNEL_LAYOUT_MPEG_5_1_C               = (123<<16) | 6,                     /* L C R Ls Rs LFE */
    QT_CHANNEL_LAYOUT_MPEG_5_1_D               = (124<<16) | 6,                     /* C L R Ls Rs LFE */
    QT_CHANNEL_LAYOUT_MPEG_6_1_A               = (125<<16) | 7,                     /* L R C LFE Ls Rs Cs */
    QT_CHANNEL_LAYOUT_MPEG_7_1_A               = (126<<16) | 8,                     /* L R C LFE Ls Rs Lc Rc */
    QT_CHANNEL_LAYOUT_MPEG_7_1_B               = (127<<16) | 8,                     /* C Lc Rc L R Ls Rs LFE (doc: IS-13818-7 MPEG2-AAC Table 3.1) */
    QT_CHANNEL_LAYOUT_MPEG_7_1_C               = (128<<16) | 8,                     /* L R C LFE Ls Rs Rls Rrs */
    QT_CHANNEL_LAYOUT_EMAGIC_DEFAULT_7_1       = (129<<16) | 8,                     /* L R Ls Rs C LFE Lc Rc */
    QT_CHANNEL_LAYOUT_SMPTE_DTV                = (130<<16) | 8,                     /* L R C LFE Ls Rs Lt Rt */

    /*  ITU defined layouts */
    QT_CHANNEL_LAYOUT_ITU_1_0                  = QT_CHANNEL_LAYOUT_MONO,            /* C */
    QT_CHANNEL_LAYOUT_ITU_2_0                  = QT_CHANNEL_LAYOUT_STEREO,          /* L R */

    QT_CHANNEL_LAYOUT_ITU_2_1                  = (131<<16) | 3,                     /* L R Cs */
    QT_CHANNEL_LAYOUT_ITU_2_2                  = (132<<16) | 4,                     /* L R Ls Rs */
    QT_CHANNEL_LAYOUT_ITU_3_0                  = QT_CHANNEL_LAYOUT_MPEG_3_0_A,      /* L R C */
    QT_CHANNEL_LAYOUT_ITU_3_1                  = QT_CHANNEL_LAYOUT_MPEG_4_0_A,      /* L R C Cs */

    QT_CHANNEL_LAYOUT_ITU_3_2                  = QT_CHANNEL_LAYOUT_MPEG_5_0_A,      /* L R C Ls Rs */
    QT_CHANNEL_LAYOUT_ITU_3_2_1                = QT_CHANNEL_LAYOUT_MPEG_5_1_A,      /* L R C LFE Ls Rs */
    QT_CHANNEL_LAYOUT_ITU_3_4_1                = QT_CHANNEL_LAYOUT_MPEG_7_1_C,      /* L R C LFE Ls Rs Rls Rrs */

    /* DVD defined layouts */
    QT_CHANNEL_LAYOUT_DVD_0                    = QT_CHANNEL_LAYOUT_MONO,            /* C (mono) */
    QT_CHANNEL_LAYOUT_DVD_1                    = QT_CHANNEL_LAYOUT_STEREO,          /* L R */
    QT_CHANNEL_LAYOUT_DVD_2                    = QT_CHANNEL_LAYOUT_ITU_2_1,         /* L R Cs */
    QT_CHANNEL_LAYOUT_DVD_3                    = QT_CHANNEL_LAYOUT_ITU_2_2,         /* L R Ls Rs */
    QT_CHANNEL_LAYOUT_DVD_4                    = (133<<16) | 3,                     /* L R LFE */
    QT_CHANNEL_LAYOUT_DVD_5                    = (134<<16) | 4,                     /* L R LFE Cs */
    QT_CHANNEL_LAYOUT_DVD_6                    = (135<<16) | 5,                     /* L R LFE Ls Rs */
    QT_CHANNEL_LAYOUT_DVD_7                    = QT_CHANNEL_LAYOUT_MPEG_3_0_A,      /* L R C */
    QT_CHANNEL_LAYOUT_DVD_8                    = QT_CHANNEL_LAYOUT_MPEG_4_0_A,      /* L R C Cs */
    QT_CHANNEL_LAYOUT_DVD_9                    = QT_CHANNEL_LAYOUT_MPEG_5_0_A,      /* L R C Ls Rs */
    QT_CHANNEL_LAYOUT_DVD_10                   = (136<<16) | 4,                     /* L R C LFE */
    QT_CHANNEL_LAYOUT_DVD_11                   = (137<<16) | 5,                     /* L R C LFE Cs */
    QT_CHANNEL_LAYOUT_DVD_12                   = QT_CHANNEL_LAYOUT_MPEG_5_1_A,      /* L R C LFE Ls Rs */
    /* 13 through 17 are duplicates of 8 through 12. */
    QT_CHANNEL_LAYOUT_DVD_13                   = QT_CHANNEL_LAYOUT_DVD_8,           /* L R C Cs */
    QT_CHANNEL_LAYOUT_DVD_14                   = QT_CHANNEL_LAYOUT_DVD_9,           /* L R C Ls Rs */
    QT_CHANNEL_LAYOUT_DVD_15                   = QT_CHANNEL_LAYOUT_DVD_10,          /* L R C LFE */
    QT_CHANNEL_LAYOUT_DVD_16                   = QT_CHANNEL_LAYOUT_DVD_11,          /* L R C LFE Cs */
    QT_CHANNEL_LAYOUT_DVD_17                   = QT_CHANNEL_LAYOUT_DVD_12,          /* L R C LFE Ls Rs */
    QT_CHANNEL_LAYOUT_DVD_18                   = (138<<16) | 5,                     /* L R Ls Rs LFE */
    QT_CHANNEL_LAYOUT_DVD_19                   = QT_CHANNEL_LAYOUT_MPEG_5_0_B,      /* L R Ls Rs C */
    QT_CHANNEL_LAYOUT_DVD_20                   = QT_CHANNEL_LAYOUT_MPEG_5_1_B,      /* L R Ls Rs C LFE */

    /* These are the symmetrical layouts. */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_4             = QT_CHANNEL_LAYOUT_QUADRAPHONIC,
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_5             = QT_CHANNEL_LAYOUT_PENTAGONAL,
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_6             = QT_CHANNEL_LAYOUT_HEXAGONAL,
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_8             = QT_CHANNEL_LAYOUT_OCTAGONAL,
    /* These are the surround-based layouts. */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_5_0           = QT_CHANNEL_LAYOUT_MPEG_5_0_B,      /* L R Ls Rs C */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_6_0           = (139<<16) | 6,                     /* L R Ls Rs C Cs */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_7_0           = (140<<16) | 7,                     /* L R Ls Rs C Rls Rrs */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_7_0_FRONT     = (148<<16) | 7,                     /* L R Ls Rs C Lc Rc */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_5_1           = QT_CHANNEL_LAYOUT_MPEG_5_1_A,      /* L R C LFE Ls Rs */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_6_1           = QT_CHANNEL_LAYOUT_MPEG_6_1_A,      /* L R C LFE Ls Rs Cs */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_7_1           = QT_CHANNEL_LAYOUT_MPEG_7_1_C,      /* L R C LFE Ls Rs Rls Rrs */
    QT_CHANNEL_LAYOUT_AUDIO_UNIT_7_1_FRONT     = QT_CHANNEL_LAYOUT_MPEG_7_1_A,      /* L R C LFE Ls Rs Lc Rc */

    QT_CHANNEL_LAYOUT_AAC_3_0                  = QT_CHANNEL_LAYOUT_MPEG_3_0_B,      /* C L R */
    QT_CHANNEL_LAYOUT_AAC_QUADRAPHONIC         = QT_CHANNEL_LAYOUT_QUADRAPHONIC,    /* L R Ls Rs */
    QT_CHANNEL_LAYOUT_AAC_4_0                  = QT_CHANNEL_LAYOUT_MPEG_4_0_B,      /* C L R Cs */
    QT_CHANNEL_LAYOUT_AAC_5_0                  = QT_CHANNEL_LAYOUT_MPEG_5_0_D,      /* C L R Ls Rs */
    QT_CHANNEL_LAYOUT_AAC_5_1                  = QT_CHANNEL_LAYOUT_MPEG_5_1_D,      /* C L R Ls Rs Lfe */
    QT_CHANNEL_LAYOUT_AAC_6_0                  = (141<<16) | 6,                     /* C L R Ls Rs Cs */
    QT_CHANNEL_LAYOUT_AAC_6_1                  = (142<<16) | 7,                     /* C L R Ls Rs Cs Lfe */
    QT_CHANNEL_LAYOUT_AAC_7_0                  = (143<<16) | 7,                     /* C L R Ls Rs Rls Rrs */
    QT_CHANNEL_LAYOUT_AAC_7_1                  = QT_CHANNEL_LAYOUT_MPEG_7_1_B,      /* C Lc Rc L R Ls Rs Lfe */
    QT_CHANNEL_LAYOUT_AAC_OCTAGONAL            = (144<<16) | 8,                     /* C L R Ls Rs Rls Rrs Cs */

    QT_CHANNEL_LAYOUT_TMH_10_2_STD             = (145<<16) | 16,                    /* L R C Vhc Lsd Rsd Ls Rs Vhl Vhr Lw Rw Csd Cs LFE1 LFE2 */
    QT_CHANNEL_LAYOUT_TMH_10_2_FULL            = (146<<16) | 21,                    /* TMH_10_2_std plus: Lc Rc HI VI Haptic */

    QT_CHANNEL_LAYOUT_AC3_1_0_1                = (149<<16) | 2,                     /* C LFE */
    QT_CHANNEL_LAYOUT_AC3_3_0                  = (150<<16) | 3,                     /* L C R */
    QT_CHANNEL_LAYOUT_AC3_3_1                  = (151<<16) | 4,                     /* L C R Cs */
    QT_CHANNEL_LAYOUT_AC3_3_0_1                = (152<<16) | 4,                     /* L C R LFE */
    QT_CHANNEL_LAYOUT_AC3_2_1_1                = (153<<16) | 4,                     /* L R Cs LFE */
    QT_CHANNEL_LAYOUT_AC3_3_1_1                = (154<<16) | 5,                     /* L C R Cs LFE */

    QT_CHANNEL_LAYOUT_DISCRETE_IN_ORDER        = 147<<16,                           /* needs to be ORed with the actual number of channels */  
    QT_CHANNEL_LAYOUT_UNKNOWN                  = 0xffff0000,                        /* needs to be ORed with the actual number of channels */
};

enum isom_track_mode_code
{
    ISOM_TRACK_ENABLED      = 0x000001,
    ISOM_TRACK_IN_MOVIE     = 0x000002,
    ISOM_TRACK_IN_PREVIEW   = 0x000004,
    QT_TRACK_IN_POSTER      = 0x000008,
};

enum isom_scaling_method_code
{
    ISOM_SCALING_METHOD_FILL    = 1,
    ISOM_SCALING_METHOD_HIDDEN  = 2,
    ISOM_SCALING_METHOD_MEET    = 3,
    ISOM_SCALING_METHOD_SLICE_X = 4,
    ISOM_SCALING_METHOD_SLICE_Y = 5,
};

enum isom_edit_mode_code
{
    ISOM_EDIT_MODE_NORMAL   = 1<<16,
    ISOM_EDIT_MODE_DWELL    = 0,
    ISOM_EDIT_MODE_EMPTY    = -1,
};

enum isom_sample_property_code
{
    /* allow_ealier */
    QT_SAMPLE_EARLIER_PTS_ALLOWED       = 1,
    /* leading */
    ISOM_SAMPLE_LEADING_UNKOWN          = 0,
    ISOM_SAMPLE_IS_UNDECODABLE_LEADING  = 1,
    ISOM_SAMPLE_IS_NOT_LEADING          = 2,
    ISOM_SAMPLE_IS_DECODABLE_LEADING    = 3,
    /* independent */
    ISOM_SAMPLE_INDEPENDENCY_UNKOWN     = 0,
    ISOM_SAMPLE_IS_NOT_INDEPENDENT      = 1,
    ISOM_SAMPLE_IS_INDEPENDENT          = 2,
    /* disposable */
    ISOM_SAMPLE_DISPOSABLE_UNKOWN       = 0,
    ISOM_SAMPLE_IS_NOT_DISPOSABLE       = 1,
    ISOM_SAMPLE_IS_DISPOSABLE           = 2,
    /* redundant */
    ISOM_SAMPLE_REDUNDANCY_UNKOWN       = 0,
    ISOM_SAMPLE_HAS_REDUNDANCY          = 1,
    ISOM_SAMPLE_HAS_NO_REDUNDANCY       = 2,
};


/* public functions */
int isom_add_sps_entry( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint8_t *sps, uint32_t sps_size );
int isom_add_pps_entry( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint8_t *pps, uint32_t pps_size );
int isom_add_sample_entry( isom_root_t *root, uint32_t track_ID, uint32_t sample_type, void* summary );

int isom_add_btrt( isom_root_t *root, uint32_t track_ID, uint32_t entry_number );
int isom_add_mdat( isom_root_t *root );
int isom_add_free( isom_root_t *root, uint8_t *data, uint64_t data_length );

int isom_write_ftyp( isom_root_t *root );
int isom_write_moov( isom_root_t *root );
int isom_write_free( isom_root_t *root );

uint32_t isom_get_media_timescale( isom_root_t *root, uint32_t track_ID );
uint64_t isom_get_media_duration( isom_root_t *root, uint32_t track_ID );
uint64_t isom_get_track_duration( isom_root_t *root, uint32_t track_ID );
uint32_t isom_get_last_sample_delta( isom_root_t *root, uint32_t track_ID );
uint32_t isom_get_start_time_offset( isom_root_t *root, uint32_t track_ID );
uint32_t isom_get_movie_timescale( isom_root_t *root );

int isom_set_brands( isom_root_t *root, uint32_t major_brand, uint32_t minor_version, uint32_t *brands, uint32_t brand_count );
int isom_set_max_chunk_duration( isom_root_t *root, double max_chunk_duration );
int isom_set_media_handler( isom_root_t *root, uint32_t track_ID, uint32_t media_type, char *name );
int isom_set_media_handler_name( isom_root_t *root, uint32_t track_ID, char *handler_name );
int isom_set_data_handler( isom_root_t *root, uint32_t track_ID, uint32_t reference_type, char *name );
int isom_set_data_handler_name( isom_root_t *root, uint32_t track_ID, char *handler_name );
int isom_set_movie_timescale( isom_root_t *root, uint32_t timescale );
int isom_set_media_timescale( isom_root_t *root, uint32_t track_ID, uint32_t timescale );
int isom_set_track_mode( isom_root_t *root, uint32_t track_ID, uint32_t mode );
int isom_set_track_presentation_size( isom_root_t *root, uint32_t track_ID, uint32_t width, uint32_t height );
int isom_set_track_volume( isom_root_t *root, uint32_t track_ID, int16_t volume );
int isom_set_track_aperture_modes( isom_root_t *root, uint32_t track_ID, uint32_t entry_number );
int isom_set_sample_resolution( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint16_t width, uint16_t height );
int isom_set_sample_type( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint32_t sample_type );
int isom_set_sample_aspect_ratio( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint32_t hSpacing, uint32_t vSpacing );
int isom_set_color_parameter( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, int primaries, int transfer, int matrix );
int isom_set_scaling_method( isom_root_t *root, uint32_t track_ID, uint32_t entry_number,
                             uint8_t scale_method, int16_t display_center_x, int16_t display_center_y );
int isom_set_channel_layout( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint32_t layout_tag, uint32_t bitmap );
int isom_set_avc_config( isom_root_t *root, uint32_t track_ID, uint32_t entry_number,
                         uint8_t configurationVersion, uint8_t AVCProfileIndication, uint8_t profile_compatibility,
                         uint8_t AVCLevelIndication, uint8_t lengthSizeMinusOne,
                         uint8_t chroma_format, uint8_t bit_depth_luma_minus8, uint8_t bit_depth_chroma_minus8 );
int isom_set_handler_name( isom_root_t *root, uint32_t track_ID, char *handler_name );
int isom_set_last_sample_delta( isom_root_t *root, uint32_t track_ID, uint32_t sample_delta );
int isom_set_media_language( isom_root_t *root, uint32_t track_ID, char *ISO_language, uint16_t Mac_language );
int isom_set_track_ID( isom_root_t *root, uint32_t track_ID, uint32_t new_track_ID );
int isom_set_free( isom_root_t *root, uint8_t *data, uint64_t data_length );
int isom_set_tyrant_chapter( isom_root_t *root, char *file_name );

int isom_create_explicit_timeline_map( isom_root_t *root, uint32_t track_ID, uint64_t segment_duration, int64_t media_time, int32_t media_rate );
int isom_create_reference_chapter_track( isom_root_t *root, uint32_t track_ID, char *file_name );
int isom_create_grouping( isom_root_t *root, uint32_t track_ID, uint32_t grouping_type );
int isom_create_object_descriptor( isom_root_t *root );

int isom_modify_timeline_map( isom_root_t *root, uint32_t track_ID, uint32_t entry_number, uint64_t segment_duration, int64_t media_time, int32_t media_rate );

int isom_update_media_modification_time( isom_root_t *root, uint32_t track_ID );
int isom_update_track_modification_time( isom_root_t *root, uint32_t track_ID );
int isom_update_movie_modification_time( isom_root_t *root );
int isom_update_track_duration( isom_root_t *root, uint32_t track_ID, uint32_t last_sample_delta );
int isom_update_bitrate_info( isom_root_t *root, uint32_t track_ID, uint32_t entry_number );


isom_root_t *isom_open_movie( const char *filename, uint32_t mode );
uint32_t isom_create_track( isom_root_t *root, uint32_t handler_type );
isom_sample_t *isom_create_sample( uint32_t size );
void isom_delete_sample( isom_sample_t *sample );
int isom_write_sample( isom_root_t *root, uint32_t track_ID, isom_sample_t *sample );
int isom_write_mdat_size( isom_root_t *root );
int isom_flush_pooled_samples( isom_root_t *root, uint32_t track_ID, uint32_t last_sample_delta );
int isom_finish_movie( isom_root_t *root, isom_adhoc_remux_t* remux );
void isom_destroy_root( isom_root_t *root );

void isom_delete_track( isom_root_t *root, uint32_t track_ID );
void isom_delete_explicit_timeline_map( isom_root_t *root, uint32_t track_ID );
void isom_delete_tyrant_chapter( isom_root_t *root );

isom_root_t *isom_parse_movie( char *filename );
int isom_print_movie( isom_root_t *root );

#endif
