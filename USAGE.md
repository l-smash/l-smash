# L-SMASH User & C API Manual (`USAGE.md`)

This manual provides a comprehensive guide to the **L-SMASH** command-line utilities (`muxer`, `remuxer`, `boxdumper`, `timelineeditor`) and the **`liblsmash` C API**.

---

## Table of Contents

1. [Core Design Principles](#1-core-design-principles)
2. [Command-Line Tools (`cli/`)](#2-command-line-tools-cli)
   - [2.1 `muxer` — Elementary Stream Multiplexer](#21-muxer--elementary-stream-multiplexer)
   - [2.2 `remuxer` — Container Remultiplexer & Fragmenter](#22-remuxer--container-remultiplexer--fragmenter)
   - [2.3 `boxdumper` — Box Hierarchy, Timestamp & Chapter Inspector](#23-boxdumper--box-hierarchy-timestamp--chapter-inspector)
   - [2.4 `timelineeditor` — VFR Timecode & Edit List Editor](#24-timelineeditor--vfr-timecode--edit-list-editor)
3. [`liblsmash` C API Programming Guide](#3-liblsmash-c-api-programming-guide)
   - [3.1 Core Lifecycle: Root, File Parameters & Movie Setup](#31-core-lifecycle-root-file-parameters--movie-setup)
   - [3.2 Track & Media Creation (`trak`, `tkhd`, `mdia`, `mdhd`)](#32-track--media-creation-trak-tkhd-mdia-mdhd)
   - [3.3 Video Sample Entry Setup (H.264/AVC, HEVC, AV1)](#33-video-sample-entry-setup-h264avc-hevc-av1)
   - [3.4 Audio Sample Entry Setup (AAC & ISO/IEC 23003-5 LPCM)](#34-audio-sample-entry-setup-aac--isoiec-23003-5-lpcm)
   - [3.5 Standard, Fragmented & Segmented Multiplexing](#35-standard-fragmented--segmented-multiplexing)
   - [3.6 Edit Lists, Chapters & Container Metadata](#36-edit-lists-chapters--container-metadata)
   - [3.7 Demultiplexing & Media Timeline Reconstruction](#37-demultiplexing--media-timeline-reconstruction)
   - [3.8 Complete End-to-End C API Example](#38-complete-end-to-end-c-api-example)

---

## 1. Core Design Principles

### 1.1 Strict Compliance with International Standards
L-SMASH enforces strict compliance with the specifications governing each container family:
- **ISO/IEC 14496-12 (ISOBMFF)** and **Apple QuickTime File Format (QTFF)** share common historical roots (`ftyp`, `moov`, `trak`, `mdia`, `minf`, `stbl`), yet diverge significantly in their Sample Description (`stsd`), Channel Layout, Pixel Aspect Ratio / Clean Aperture, and Audio Sample Entry binary layouts.
- L-SMASH **never mixes QTFF-only atoms or fields into ISOBMFF files**, nor ISOBMFF-only boxes into QuickTime `.mov` files.

### 1.2 Architectural Separation of ISOBMFF and QTFF
The two standards are never mixed, on either the video side or the audio side. Each file is written entirely in the vocabulary of one family.

#### Video
| Aspect | ISOBMFF (`.mp4`, Fragmented MP4) | QTFF (`.mov`) |
| :--- | :--- | :--- |
| Colour space (`colr`) | `nclx` (explicit primaries / transfer / matrix / full-range flag) | `nclc` (primaries / transfer / matrix only) |
| Clean aperture | `clap` in the sample entry | `clap` in the sample entry, plus track-level `tapt` containing `clef` / `prof` / `enof` |
| Pixel aspect ratio | `pasp` | `pasp` |

- `tapt` / `clef` / `prof` / `enof` and `nclc` are QTFF-only and are not emitted into ISOBMFF files. `nclx` is not emitted into QuickTime files.

#### Audio
Uncompressed Linear PCM (LPCM) audio and high-sampling-rate signaling show the same boundary:
- **In ISOBMFF (`.mp4`, `.m4a`, Fragmented MP4)**:
  - Uncompressed LPCM uses **ISO/IEC 23003-5** (`ipcm` for 16/24/32-bit integer PCM, `fpcm` for 32/64-bit IEEE 754 floating-point PCM) paired with a mandatory **`pcmC`** (`PCMConfigBox`) specifying endianness and bit depth.
  - Multichannel speaker arrangements use **ISO/IEC 14496-12 `chnl`** (`ChannelLayout`), and sampling rates exceeding 65535 Hz (such as 88.2 kHz, 96 kHz, or 192 kHz) use ISOBMFF **`AudioSampleEntryV1`** (`stsd` version 1) with an **`srat`** (`SamplingRateBox`).
- **In QuickTime (`.mov`)**:
  - Uncompressed LPCM uses Apple QuickTime Sound Description v0/v1/v2 (`lpcm`, `sowt`, `twos`, `in24`, `in32`, `fl32`, `fl64`, `raw `) and QuickTime `chan` / `wave` atoms.

### 1.3 Supported Codecs, Timed Text, Sample Entry Extensions & Metadata

#### Video Codecs
- **AOM AV1 (`av01` + `av1C`)** — AOM AV1 Bitstream v1.0.0 Errata 1 & AV1-ISOBMFF v1.2.0 (*Summary Construct / Raw `.ivf` Importer / Media Demux*)
- **ITU-T H.264 / MPEG-4 AVC (`avc1`–`avc4`, `avcp` + `avcC`, `btrt`)** — ISO/IEC 14496-10 & ISO/IEC 14496-15 (*Summary Construct / Raw Annex-B Importer / Media Demux*)
- **ITU-T H.265 / MPEG-H HEVC (`hvc1`, `hev1` + `hvcC`, `btrt`)** — ISO/IEC 23008-2 & ISO/IEC 14496-15 (*Summary Construct / Raw Annex-B Importer / Media Demux*)
- **SMPTE VC-1 (`vc-1` + `dvc1`, `btrt`)** — SMPTE 421M & RP 2025 (*Summary Construct / Raw Advanced Profile Importer / Media Demux*)
- **ISO/IEC 14496-2 MPEG-4 Visual (`mp4v` + `esds`)**, **Apple ProRes (`apch`, `apcn`, `apcs`, `apco`, `ap4h`, `ap4x`)**, **QTFF Uncompressed (`raw `, `2vuy`, `v210`, `v216`, `v308`, `v408`, `v410`, `yuv2`)**, **Dirac (`drac`)**, **Motion JPEG 2000 (`mjp2`)**, **Ut Video (`ULRA`, `ULRG`, `ULY0`, `ULY2`, `ULH0`, `ULH2`, `UQY2`)**, **CineForm (`CFHD`)** (*Summary Construct / Media Demux*)

#### Audio Codecs
- **Xiph FLAC (`fLaC` + `dfLa`)** — IETF RFC 9639 & FLAC-in-ISOBMFF (*Summary Construct / Raw `.flac` Importer / Media Demux*)
- **MPEG-4 Audio (`mp4a` + `esds` / `wave`, `als `)** — ISO/IEC 14496-3 AAC-LC, HE-AAC v1/v2 (SBR/PS), MPEG-4 ALS (*Summary Construct / Raw ADTS `.aac` & `.als` Importers / Media Demux*)
- **IETF Opus (`Opus` + `dOps`)** — IETF RFC 6716 & Opus-in-ISOBMFF with `'roll'` (`AudioRollRecoveryEntry`) pre-roll sample grouping and encoder-delay Edit List mapping (*Summary Construct / Media Demux*)
- **Dolby AC-3 (`ac-3` + `dac3`) & Enhanced AC-3 (`ec-3` + `dec3`)** — ATSC A/52 & ETSI TS 102 366 Annex F (*Summary Construct / Raw `.ac3` & `.eac3` Importers / Media Demux*)
- **Apple Lossless (`alac` + `alac` / `wave`)** — ALAC in ISOBMFF & QTFF (*Summary Construct / Media Demux*)
- **DTS & DTS-HD (`dtsc`, `dtsh`, `dtse`, `dtsl`, `dtsx` + `ddts`)** — ETSI TS 102 114 (*Summary Construct / Raw `.dts` & `.dtshd` Importers / Media Demux*)
- **ISOBMFF Uncompressed LPCM (`ipcm`, `fpcm` + `pcmC`)** — ISO/IEC 23003-5 & ISO/IEC 14496-12 (*Summary Construct / Raw `.wav` Importer / Media Demux*)
- **QTFF Uncompressed LPCM (`lpcm`, `sowt`, `twos`, `raw `, `in24`, `in32`, `fl32`, `fl64`)** — Apple QuickTime Sound Description v0/v1/v2 (*Summary Construct / Raw `.wav` Importer / Media Demux*)
- **3GPP AMR-NB / AMR-WB (`samr`, `sawb` + `damr`)**, **MPEG-1/2 Layer III (`mp4a` / `.mp3`)**, **Windows Media Audio (`owma` + `wfex`)**

#### Timed Text & Subtitles
- **3GPP Timed Text (`tx3g` + `ftab`)** — 3GPP TS 26.245 / ISO/IEC 14496-12 Timed Text sample entry and Font Table Box (*Summary Construct / Reference Chapter Track / Media Demux*)
- **QuickTime Text Media (`text`)** — Apple QuickTime Text sample entry under `gmhd`/`gmin`/`text` (*Summary Construct / Reference Chapter Track / Media Demux*)

#### Sample Entry Extensions, HDR, Spatial Media & Metadata
- **HDR, Color & Mastering**: Color Parameter Box `colr` (`nclx` in ISOBMFF, `nclc` in QTFF), Mastering Display Color Volume (`mdcv`), Content Light Level Information (`clli`), Dolby Vision Configuration (`dvcC`, `dvvC`), Layered HEVC Configuration (`lhvC`), and QTFF `gama`, `fiel`, `cspc`, `sgbt`.
- **Aperture & Pixel Aspect Ratio**: Clean Aperture (`clap`), Pixel Aspect Ratio (`pasp`), and QuickTime Track Aperture Mode Dimensions (`tapt` / `clef` / `prof` / `enof`).
- **Spatial Video & Stereoscopic 3D**: Google Spherical Video v2 (`st3d`, `sv3d` / `svhd` / `proj` / `prhd` / `equi` / `cbmp` / `prji`) and Apple Spatial Video (`vexu` / `eyes` / `must` / `stri` / `hero` / `cams` / `blin` / `cmfy` / `dadj`, `hfov`).
- **Spatial Audio & Channel Layout**: Google Spatial Audio Ambisonics (`SA3D`), QuickTime Audio Channel Layout (`chan`), ISO/IEC 14496-12 `chnl` (`ChannelLayout`) and `srat` (`SamplingRateBox`), and ISO/IEC 23003-5 `pcmC` (`PCMConfigBox`).
- **Container Metadata & Chapters**:
  1. **Apple iTunes Metadata (`moov.udta.meta.ilst`) & QuickTime Metadata (`meta` / `keys` / `ilst`)**: Container-level key-value metadata tags (title, artist, album, encoder, custom `----` `mean`/`name`/`data` items).
  2. **QuickTime / iTunes Reference Chapter Tracks (`tref.chap`)**: A dedicated timed text track (`text` or `tx3g`) referenced from the media track via a `'chap'` Track Reference Box (`tref`), enabled via `--chapter` + `--chapter-track`.
  3. **Nero Chapter List (`moov.udta.chpl`)**: A table-based chapter list box stored inside `moov.udta` for `.mp4`/`.m4v`/`.m4a` files, enabled via `--chapter` (and optional `--chpl-with-bom`).

---

## 2. Command-Line Tools (`cli/`)

### 2.1 `muxer` — Elementary Stream Multiplexer

`muxer` imports raw elementary streams (or single-track WAVE/FLAC containers) and multiplexes them into `.mp4`, `.mov`, `.m4a`, `.m4v`, `.3gp`, or `.3g2`.

#### Synopsis
```sh
muxer [global_options] -i input1[?track_options] [-i input2[?track_options] ...] -o output.[mp4|mov|m4a|m4v|3gp|3g2]
```

#### Supported Input Formats
- **Video**:
  - Raw ITU-T H.264 / AVC Annex-B byte stream (`.h264`, `.264`, `.avc`)
  - Raw ITU-T H.265 / HEVC Annex-B byte stream (`.h265`, `.265`, `.hevc`)
  - AOM AV1 in IVF container (`.ivf`)
  - SMPTE VC-1 Advanced Profile elementary stream (`.vc1`)
- **Audio**:
  - Xiph FLAC stream (`.flac`, RFC 9639 / FLAC-in-ISOBMFF `fLaC`/`dfLa`)
  - Raw AAC ADTS stream (`.aac`, `.adts`)
  - Raw MPEG-4 ALS stream (`.als`)
  - Raw Dolby AC-3 (`.ac3`) and Enhanced AC-3 (`.eac3`, `.ec3`)
  - Raw DTS / DTS-HD stream (`.dts`, `.dtshd`)
  - RIFF WAVE / RF64 Uncompressed LPCM (`.wav`: `WAVE_FORMAT_PCM`, `WAVE_FORMAT_IEEE_FLOAT`, `WAVE_FORMAT_EXTENSIBLE`)
  - 3GPP AMR-NB / AMR-WB (`.amr`, `.awb`)
  - MPEG-1/2 Layer III (`.mp3`)

#### Global Options (`muxer`)

| Option | Argument | Description |
| :--- | :--- | :--- |
| `--help` | *(none)* | Display help message and exit. |
| `--version` | *(none)* | Display L-SMASH version and exit. |
| `-i` | `<path>[?<opts>]` | Specify an input file and optional per-input track options. |
| `-o` | `<path>` | Specify the output file path (extension determines brand defaults). |
| `--file-format` | `mp4\|mov\|m4a\|m4v\|3gp\|3g2` | Explicitly override the output file format. |
| `--optimize-pd` | *(none)* | Optimize for progressive download (places `moov` before `mdat`). |
| `--interleave` | `<integer>` | Specify maximum interleave chunk interval in milliseconds. |
| `--chapter` | `<path>` | Set chapters from a Nero/Ogg-style chapter text file. |
| `--chpl-with-bom` | *(none)* | Add UTF-8 BOM to chapter strings in the Nero `chpl` box (`mp4`/`m4v`/`m4a`). |
| `--chapter-track` | `<integer>` | Specify which track number the reference chapter track applies to (default: `1`). |
| `--copyright-notice` | `<arg>` | Specify copyright notice as `<language>/<string>` (`mp4`/`m4v`/`m4a`/`3gp`/`3g2`). |
| `--language` | `<string>` | Specify the default ISO-639-2/T language code for all tracks (`mp4`/`m4v`/`m4a`). |
| `--compact-size-table` | *(none)* | Compress Sample Size boxes (`stsz` → `stz2`) when all sample sizes fit in 4, 8, or 16 bits. |
| `--isom-version` | `<1..6>` | Specify maximum compatible ISO Base Media version (`iso1`–`iso6`). |
| `--shift-timeline` | *(none)* | Enable composition-to-decode timeline shift (uses negative `ctts` v1 offsets or Edit List). |
| `--brands` | `<4CC,4CC,...>` | Specify comma-separated compatible brand FourCCs (`--brands help` lists known brands). |
| `--major-brand` | `<4CC>` | Specify major brand FourCC. |
| `--minor-version` | `<integer>` | Specify minor version integer for `ftyp`. |

#### iTunes Metadata Options (`muxer`, valid for `mp4`, `m4a`, `m4v`)
`--album-name`, `--artist`, `--comment`, `--release-date` (`YYYY-MM-DD`), `--encoder`, `--genre`, `--lyrics`, `--title`, `--composer`, `--album-artist`, `--copyright`, `--description`, `--grouping`, `--beats-per-minute <int>`.

#### Per-Input Track Options (`-i input?opt1=val1,opt2=val2`)

| Track Option | Value Format | Description |
| :--- | :--- | :--- |
| `fps=<arg>` | `<num>` or `<num>/<den>` | Video framerate (e.g., `fps=24000/1001`, `fps=60`). |
| `language=<arg>` | 3-letter code | Media language code (e.g., `language=eng`, `language=jpn`). |
| `alternate-group=<int>` | `<integer>` | Alternate group ID (`tkhd.alternate_group`). |
| `encoder-delay=<int>` | `<integer>` | Audio encoder delay (priming samples) to strip via Edit List. |
| `sbr` | *(flag)* | Explicitly signal HE-AAC v1 Spectral Band Replication (SBR). |
| `ps` | *(flag)* | Explicitly signal HE-AAC v2 Parametric Stereo (PS; implies `sbr`). |
| `par=<w>:<h>` | `<int>:<int>` | Pixel Aspect Ratio (`pasp` box, e.g., `par=16:15`). |
| `clap=<args>` | `wN:wD,hN:hD,hON:hOD,vON:vOD` | Clean Aperture (`clap` box) rational parameters. |
| `handler=<string>` | `<string>` | Set the media handler name (`hdlr.name`). |

#### `muxer` Practical Examples
```sh
# Multiplex H.264 Annex-B video + AAC ADTS audio with encoder delay compensation into MP4
./cli/muxer -i video.h264?fps=30000/1001 -i audio.aac?language=eng,encoder-delay=2112 \
            --optimize-pd --title "Demo Movie" -o movie.mp4

# Multiplex AV1 (.ivf) + 5.1ch 24-bit 48 kHz WAVE into ISOBMFF MP4 (creates av01/av1C + ipcm/pcmC/chnl)
./cli/muxer -i stream.ivf?fps=24 -i surround_51ch_24bit.wav?language=eng --optimize-pd -o av1_51ch.mp4

# Multiplex WAVE into QuickTime .mov (creates QTFF lpcm/sowt/in24 Sound Description)
./cli/muxer -i surround_51ch_24bit.wav -o audio.mov
```

---

### 2.2 `remuxer` — Container Remultiplexer & Fragmenter

`remuxer` reads one or more existing container files (`.mp4`, `.mov`, `.m4a`, `.m4v`, `.3gp`, `.3g2`), optionally filters or re-configures tracks, and writes a new standard or **Fragmented ISOBMFF** container.

#### Synopsis
```sh
remuxer [global_options] -i input1[?track_opts] [-i input2 ...] -o output.[mp4|mov|m4a|m4v|3gp|3g2]
```

#### Global Options (`remuxer`)

| Option | Argument | Description |
| :--- | :--- | :--- |
| `-i` | `<path>[?<opts>]` | Input container file and optional per-track options (`?[track_num:]opt=val`). |
| `-o` | `<path>` | Output container file path. |
| `--file-format` | `<format>` | Specify output file format (`mp4`, `mov`, `m4a`, `m4v`, `3gp`, `3g2`). |
| `--optimize-pd` | *(none)* | Optimize for progressive download (`moov` before `mdat`). |
| `--interleave` | `<integer>` | Specify maximum interleave chunk interval in milliseconds. |
| `--fragment` | `<track_num>` | Enable **Fragmented Movie (`moof`/`traf`/`trun`)** output using `<track_num>` as the reference track for fragment boundaries (cannot be combined with `--optimize-pd`). |
| `--min-frag-duration` | `<float>` | Specify minimum fragment duration in seconds (default: `1.0`). |
| `--dash` | *(none)* | Enable **MPEG-DASH ISOBMFF Media Segment** output (`sidx` + `dash`/`msdh`/`msix` brands; requires `--fragment`). |
| `--chapter` | `<path>` | Set chapters from a chapter file. |
| `--chpl-with-bom` | *(none)* | Add UTF-8 BOM to chapter strings in the `chpl` box. |
| `--chapter-track` | `<integer>` | Specify reference chapter track number (default: `1`). |
| `--language` | `<string>` | Specify default ISO-639-2/T language code across tracks. |

#### Per-Track Options (`-i input?1:opt=val/2:opt=val`)
Multiple track specifiers on a single input are separated by `/` (e.g., `-i movie.mp4?1:language=eng/2:remove`):
- `language=<code3>` — Override track language.
- `alternate-group=<int>` — Set `tkhd.alternate_group`.
- `sbr` / `ps` — Explicitly signal HE-AAC SBR / PS.
- `handler=<string>` — Set media handler name.
- `remove` — Exclude this track from the output movie.
- `seek=<int>` — Start remultiplexing from the `<int>`-th Random Access Point (RAP).
- `safe-seek=<int>` — Same as `seek`, plus prepends an Edit List entry to skip any undecodable leading samples prior to the RAP.

#### `remuxer` Practical Examples
```sh
# Convert a standard MP4 into a Fragmented MP4 with 2.0-second fragments aligned to video track 1
./cli/remuxer -i movie.mp4 --fragment 1 --min-frag-duration 2.0 -o movie_frag.mp4

# Convert a multi-track MP4 into an MPEG-DASH self-initializing media segment (sidx + moof/mdat)
./cli/remuxer -i movie.mp4 --fragment 1 --dash --min-frag-duration 4.0 -o segment_dash.mp4

# Remux only track 1 (removing track 2) starting from the 3rd keyframe with safe Edit List trimming
./cli/remuxer -i movie.mp4?1:safe-seek=3/2:remove --optimize-pd -o trimmed.mp4
```

---

### 2.3 `boxdumper` — Box Hierarchy, Timestamp & Chapter Inspector

`boxdumper` inspects the internal structure of ISOBMFF and QTFF files.

#### Synopsis
```sh
boxdumper [options] input
```

#### Options (`boxdumper`)

| Option | Description |
| :--- | :--- |
| `--box` | Dump the complete Box/Atom hierarchy, headers, sample description extensions (`avcC`, `hvcC`, `av1C`, `pcmC`, `chnl`, `srat`, `esds`, etc.), and sample table entries (default mode). |
| `--chapter` | Extract and print the chapter list (`chpl` or reference chapter track). |
| `--timestamp [track_ID]` | Dump per-sample Decode Timestamps (`DTS`) and Composition Timestamps (`CTS`) for `track_ID` (default: track `1`). |

#### `boxdumper` Practical Examples
```sh
# Dump full Box hierarchy to stdout
./cli/boxdumper --box movie.mp4

# Dump DTS/CTS timestamps of track 1
./cli/boxdumper --timestamp 1 movie.mp4
```

---

### 2.4 `timelineeditor` — VFR Timecode & Edit List Editor

`timelineeditor` modifies the timing and Edit List (`edts`/`elst`) of a single track in a container and writes a progressive-download-optimized output file.

#### Synopsis
```sh
timelineeditor [options] input output
```

#### Options (`timelineeditor`)

| Option | Argument | Description |
| :--- | :--- | :--- |
| `--track` | `<integer>` | Specify target track number to edit (default: `1`). |
| `--timecode` | `<path>` | Edit timeline using a **Matroska timecode format v2** file (`# timecode format v2`). |
| `--media-timescale` | `<integer>` | Specify new media timescale (`mdhd.timescale`) before applying edits. |
| `--Media-timescale` | `<integer>` | Specify new media timescale (`mdhd.timescale`) and rescale existing timestamps after applying edits. |
| `--movie-timescale` | `<integer>` | Specify new movie timescale (`mvhd.timescale`). |
| `--skip` | `<integer>` | Skip `<integer>` ticks from the start of the media presentation via Edit List. |
| `--delay` | `<integer>` | Insert `<integer>` ticks of empty delay before the media presentation via Edit List. |
| `--dts-compression` | *(none)* | Eliminate initial composition-to-decode delay (`ctts` offset) by shifting DTS and compensating via Edit List. |

---

## 3. `liblsmash` C API Programming Guide

Include the public header `<lsmash.h>` and link with `-llsmash` (or `pkg-config --cflags --libs liblsmash`).

### 3.1 Core Lifecycle: Root, File Parameters & Movie Setup

Every `liblsmash` session begins by allocating an `lsmash_root_t` context with `lsmash_create_root()` and associating a file via `lsmash_open_file()` and `lsmash_set_file()`.

```c
#include <lsmash.h>

lsmash_root_t *root = lsmash_create_root();
lsmash_file_parameters_t file_param = { 0 };

/* mode = 0 for writing, 1 for reading */
if( lsmash_open_file( "output.mp4", 0, &file_param ) < 0 )
    return -1;

/* Configure ISOBMFF brands (ftyp) */
lsmash_brand_type brands[4] = {
    ISOM_BRAND_TYPE_ISOM,
    ISOM_BRAND_TYPE_ISO6,
    ISOM_BRAND_TYPE_AVC1,
    ISOM_BRAND_TYPE_MP42
};
file_param.major_brand   = ISOM_BRAND_TYPE_MP42;
file_param.minor_version = 0;
file_param.brands        = brands;
file_param.brand_count   = 4;

/* For Fragmented MP4 output, OR in LSMASH_FILE_MODE_FRAGMENTED:
 * file_param.mode |= LSMASH_FILE_MODE_FRAGMENTED; */

lsmash_file_t *file = lsmash_set_file( root, &file_param );

lsmash_movie_parameters_t movie_param;
lsmash_initialize_movie_parameters( &movie_param );
movie_param.timescale = 1000;
lsmash_set_movie_parameters( root, &movie_param );
```

---

### 3.2 Track & Media Creation (`trak`, `tkhd`, `mdia`, `mdhd`)

Create video or audio tracks with `lsmash_create_track()`, then configure track header (`tkhd`) and media header (`mdhd`) parameters:

```c
uint32_t video_track_id = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );

lsmash_track_parameters_t track_param;
lsmash_initialize_track_parameters( &track_param );
track_param.mode           = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
track_param.display_width  = 1920 << 16; /* 16.16 fixed-point */
track_param.display_height = 1080 << 16;
lsmash_set_track_parameters( root, video_track_id, &track_param );

lsmash_media_parameters_t media_param;
lsmash_initialize_media_parameters( &media_param );
media_param.timescale     = 24000;
media_param.ISO_language  = ISOM_LANGUAGE_CODE_ENGLISH;
media_param.rap_grouping  = 1; /* Enable 'rap ' sample grouping for Open-GOP / non-sync RAPs */
media_param.roll_grouping = 1; /* Enable 'roll' sample grouping for recovery points */
lsmash_set_media_parameters( root, video_track_id, &media_param );
```

---

### 3.3 Video Sample Entry Setup (H.264/AVC, HEVC, AV1)

Sample Descriptions (`stsd` entries) are configured through `lsmash_video_summary_t` and codec-specific extensions attached via `lsmash_add_codec_specific_data()`.

#### Example A: H.264 / MPEG-4 AVC (`avc1` + `avcC`)
```c
lsmash_video_summary_t *vsum =
    (lsmash_video_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_VIDEO );
vsum->sample_type = ISOM_CODEC_TYPE_AVC1_VIDEO;
vsum->width       = 1920;
vsum->height      = 1080;
vsum->depth       = 0x0018;

lsmash_codec_specific_t *avcc_cs = lsmash_create_codec_specific_data(
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_H264,
    LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
lsmash_h264_specific_parameters_t *avcc =
    (lsmash_h264_specific_parameters_t *)avcc_cs->data.structured;
avcc->lengthSizeMinusOne = 3; /* 4-byte NAL unit length prefix */

/* Append raw SPS and PPS NAL units (without Annex-B start codes) */
lsmash_append_h264_parameter_set( avcc, H264_PARAMETER_SET_TYPE_SPS, sps_nalu, sps_len );
lsmash_append_h264_parameter_set( avcc, H264_PARAMETER_SET_TYPE_PPS, pps_nalu, pps_len );

lsmash_add_codec_specific_data( (lsmash_summary_t *)vsum, avcc_cs );
lsmash_destroy_codec_specific_data( avcc_cs );

uint32_t v_sample_entry = lsmash_add_sample_entry( root, video_track_id, vsum );
lsmash_cleanup_summary( (lsmash_summary_t *)vsum );
```

#### Example B: AOM AV1 (`av01` + `av1C`)
To populate `lsmash_av1_specific_parameters_t` directly from a Temporal Unit containing a Sequence Header OBU, call `lsmash_setup_av1_specific_parameters_from_access_unit()`:

```c
lsmash_video_summary_t *vsum =
    (lsmash_video_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_VIDEO );
vsum->sample_type = ISOM_CODEC_TYPE_AV01_VIDEO;
vsum->width       = 1920;
vsum->height      = 1080;

lsmash_codec_specific_t *av1c_cs = lsmash_create_codec_specific_data(
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_AV1,
    LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
lsmash_av1_specific_parameters_t *av1c =
    (lsmash_av1_specific_parameters_t *)av1c_cs->data.structured;

/* Parse Sequence Header OBU from the first access unit into av1c */
lsmash_setup_av1_specific_parameters_from_access_unit( av1c, first_tu_data, first_tu_len );

lsmash_add_codec_specific_data( (lsmash_summary_t *)vsum, av1c_cs );
lsmash_destroy_codec_specific_data( av1c_cs );

uint32_t v_sample_entry = lsmash_add_sample_entry( root, video_track_id, vsum );
lsmash_cleanup_summary( (lsmash_summary_t *)vsum );
```

---

### 3.4 Audio Sample Entry Setup (AAC & ISO/IEC 23003-5 LPCM)

#### Example A: MPEG-4 AAC (`mp4a` + `esds`)
```c
lsmash_audio_summary_t *asum =
    (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
asum->sample_type      = ISOM_CODEC_TYPE_MP4A_AUDIO;
asum->frequency        = 48000;
asum->channels         = 2;
asum->sample_size      = 16;
asum->samples_in_frame = 1024;
asum->aot              = MP4A_AUDIO_OBJECT_TYPE_AAC_LC;
asum->sbr_mode         = MP4A_AAC_SBR_NOT_SPECIFIED;

lsmash_codec_specific_t *esds_cs = lsmash_create_codec_specific_data(
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_MP4SYS_DECODER_CONFIG,
    LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
lsmash_mp4sys_decoder_parameters_t *esds =
    (lsmash_mp4sys_decoder_parameters_t *)esds_cs->data.structured;
esds->objectTypeIndication = MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3;
esds->streamType           = MP4SYS_STREAM_TYPE_AudioStream;
lsmash_set_mp4sys_decoder_specific_info( esds, asc_bytes, asc_len );

lsmash_add_codec_specific_data( (lsmash_summary_t *)asum, esds_cs );
lsmash_destroy_codec_specific_data( esds_cs );

uint32_t a_sample_entry = lsmash_add_sample_entry( root, audio_track_id, asum );
lsmash_cleanup_summary( (lsmash_summary_t *)asum );
```

#### Example B: ISO/IEC 23003-5 Uncompressed LPCM (`ipcm` / `fpcm`, `pcmC`, `chnl`)
For uncompressed LPCM in ISOBMFF:
- Use `ISOM_CODEC_TYPE_IPCM_AUDIO` for integer PCM (16, 24, or 32 bits) or `ISOM_CODEC_TYPE_FPCM_AUDIO` for IEEE 754 floating-point PCM (32 or 64 bits).
- Attach a mandatory `LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_PCM_CONFIG` (`pcmC` box) specifying `format_flags` (`LSMASH_PCM_FORMAT_FLAG_BIG_ENDIAN` or `LSMASH_PCM_FORMAT_FLAG_LITTLE_ENDIAN`) and `PCM_sample_size`.
- Optionally attach `LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_CHANNEL_LAYOUT` (`chnl` box) to signal CICP `definedLayout`, explicit `speaker_position` arrays, `omittedChannelsMap`, or `object_count`.
- When `asum->frequency > 65535` (e.g., 96000 Hz or 192000 Hz), L-SMASH automatically emits an ISOBMFF **`AudioSampleEntryV1`** (`version = 1`) and child **`srat`** (`SamplingRateBox`).

```c
uint32_t audio_track_id = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );

lsmash_audio_summary_t *asum =
    (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
asum->sample_type      = ISOM_CODEC_TYPE_IPCM_AUDIO;
asum->frequency        = 96000; /* > 65535 Hz -> automatically writes AudioSampleEntryV1 + srat */
asum->channels         = 6;     /* 5.1ch */
asum->sample_size      = 24;
asum->samples_in_frame = 1;
asum->bytes_per_frame  = 6 * 3; /* 18 bytes per 5.1ch 24-bit frame */

/* 1. Mandatory pcmC (PCMConfigBox) */
lsmash_codec_specific_t *pcmc_cs = lsmash_create_codec_specific_data(
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_PCM_CONFIG,
    LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
lsmash_isom_pcm_config_t *pcmc = (lsmash_isom_pcm_config_t *)pcmc_cs->data.structured;
pcmc->format_flags    = LSMASH_PCM_FORMAT_FLAG_LITTLE_ENDIAN;
pcmc->PCM_sample_size = 24;
lsmash_add_codec_specific_data( (lsmash_summary_t *)asum, pcmc_cs );
lsmash_destroy_codec_specific_data( pcmc_cs );

/* 2. Optional chnl (ChannelLayout Box) */
lsmash_codec_specific_t *chnl_cs = lsmash_create_codec_specific_data(
    LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_CHANNEL_LAYOUT,
    LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
lsmash_isom_channel_layout_t *chnl = (lsmash_isom_channel_layout_t *)chnl_cs->data.structured;
chnl->stream_structure   = LSMASH_CHANNEL_STREAM_STRUCTURE_CHANNEL;
chnl->definedLayout      = 6; /* CICP Layout 6 = 5.1ch (L, R, C, LFE, Ls, Rs) */
chnl->omittedChannelsMap = 0;
chnl->channel_count      = 6;
lsmash_add_codec_specific_data( (lsmash_summary_t *)asum, chnl_cs );
lsmash_destroy_codec_specific_data( chnl_cs );

uint32_t a_sample_entry = lsmash_add_sample_entry( root, audio_track_id, asum );
lsmash_cleanup_summary( (lsmash_summary_t *)asum );
```

---

### 3.5 Standard, Fragmented & Segmented Multiplexing

#### Appending Samples (`lsmash_append_sample`)
Allocate each sample with `lsmash_create_sample( size )`, populate `data`, `dts`, `cts`, `index` (1-based sample description index), and `prop.ra_flags`, and pass it to `lsmash_append_sample()`. Note that `lsmash_append_sample()` takes ownership of the `lsmash_sample_t *` pointer.

```c
lsmash_sample_t *sample = lsmash_create_sample( payload_size );
memcpy( sample->data, payload_bytes, payload_size );
sample->dts           = dts;
sample->cts           = cts;
sample->index         = sample_entry_index; /* 1-based */
sample->prop.ra_flags = is_sync ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC
                                : ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
lsmash_append_sample( root, track_id, sample );
```

#### Standard Movie Finalization
After appending all samples to each track, flush the internal sample pool and finalize the movie:

```c
lsmash_flush_pooled_samples( root, video_track_id, last_video_sample_delta );
lsmash_update_track_duration( root, video_track_id, 0 );

lsmash_flush_pooled_samples( root, audio_track_id, last_audio_sample_delta );
lsmash_update_track_duration( root, audio_track_id, 0 );

/* NULL: the moov box stays after mdat (default placement) */
lsmash_finish_movie( root, NULL );
lsmash_close_file( &file_param );
lsmash_destroy_root( root );
```

#### Progressive Download (`moov` at the Front)
With `lsmash_finish_movie( root, NULL )`, `moov` is written after `mdat`. To place `moov` before `mdat` (FastStart), pass an `lsmash_adhoc_remux_t` instead. L-SMASH then rearranges the already-written file in place, moving the data through a working buffer of `buffer_size` bytes (internally raised to at least twice the `moov` size). This requires a seekable output. The optional `func` callback receives the bytes processed so far and the total; its return value is ignored, so it cannot abort the operation. Set `func` to `NULL` if no progress report is needed.

```c
static int on_progress( void *param, uint64_t done, uint64_t total )
{
    (void)param;
    fprintf( stderr, "\rmoving moov: %" PRIu64 " / %" PRIu64, done, total );
    return 0; /* ignored by the library */
}

lsmash_adhoc_remux_t remux = { 0 };
remux.buffer_size = 4 * 1024 * 1024; /* 4 MiB working buffer */
remux.func        = on_progress;
remux.param       = NULL;
lsmash_finish_movie( root, &remux );
lsmash_close_file( &file_param );
lsmash_destroy_root( root );
```

#### Fragmented Movie (`LSMASH_FILE_MODE_FRAGMENTED`) Workflow
When `file_param.mode` includes `LSMASH_FILE_MODE_FRAGMENTED`:
1. Any samples appended before the first `lsmash_create_fragment_movie( root )` call are placed in the initial `moov` box (or leave the initial `moov` empty for a pure initialization segment).
2. Before starting each new Movie Fragment (`moof` + `mdat`), flush pooled samples from the current fragment on all active tracks, then call `lsmash_create_fragment_movie( root )`:

```c
/* Start a new moof fragment */
lsmash_flush_pooled_samples( root, video_track_id, video_delta );
lsmash_flush_pooled_samples( root, audio_track_id, audio_delta );
lsmash_create_fragment_movie( root );

/* Append samples belonging to this fragment ... */
```

For constant-size, constant-duration audio tracks such as ISO/IEC 23003-5 LPCM, L-SMASH automatically hoists the common sample duration and byte size into `tfhd->default_sample_duration` and `tfhd->default_sample_size`, omitting redundant per-sample `trun` array rows.

#### Segmented ISOBMFF (`LSMASH_FILE_MODE_SEGMENT`)
`lsmash_open_file( path, 0, &param )` (write mode) pre-sets `param.mode` to `LSMASH_FILE_MODE_WRITE | LSMASH_FILE_MODE_BOX | LSMASH_FILE_MODE_INITIALIZATION | LSMASH_FILE_MODE_MEDIA`. Segment output is obtained by adjusting the flags before `lsmash_set_file()`:

| Output | `param.mode` (starting from the write-mode default) | Resulting layout |
| :--- | :--- | :--- |
| Fragmented MP4 (self-contained) | `+ FRAGMENTED` | `ftyp` `moov` (`moof` `mdat`)... `mfra` (the trailing `mfra` is written only when seekable) |
| Self-Initializing Media Segment (`remuxer --fragment --dash`) | `+ FRAGMENTED + INDEX + SEGMENT` | `ftyp` `moov` `sidx` (`moof` `mdat`)... (no `mfra`) |
| Initialization Segment (separate file) | `- MEDIA + FRAGMENTED + SEGMENT` | `ftyp` `moov` (no samples can be appended) |
| Media Segment (separate file) | `- INITIALIZATION + FRAGMENTED + INDEX + SEGMENT` (`MEDIA` kept) | `styp` `sidx` (`moof` `mdat`)... |

The remaining abbreviations drop the `LSMASH_FILE_MODE_` prefix. These are the combinations that `cli/remuxer.c` builds, and they are the ones to use.

**Self-Initializing Media Segment:**

```c
lsmash_file_parameters_t param = { 0 };
lsmash_open_file( "seg.mp4", 0, &param );
param.mode |= LSMASH_FILE_MODE_FRAGMENTED | LSMASH_FILE_MODE_INDEX | LSMASH_FILE_MODE_SEGMENT;

/* sidx is generated only when the maximum brand is iso6 or later. */
lsmash_brand_type brands[] = { ISOM_BRAND_TYPE_ISO6, ISOM_BRAND_TYPE_DASH };
param.major_brand = ISOM_BRAND_TYPE_ISO6;
param.brands      = brands;
param.brand_count = 2;

lsmash_file_t *file = lsmash_set_file( root, &param );
/* ... create tracks, add sample entries; then per fragment:
 *     lsmash_create_fragment_movie( root ); lsmash_append_sample( ... ); ...
 * Flush the pooled samples of every track at the end. */

lsmash_adhoc_remux_t remux = { 0 };
remux.buffer_size = 4 * 1024 * 1024;
lsmash_finish_movie( root, &remux ); /* writes the sidx; remux must not be NULL here */
lsmash_close_file( &param );
```

**Separate Initialization Segment and Media Segments** with `lsmash_switch_media_segment()`:

```c
/* 1. Initialization Segment: no MEDIA flag, so it carries moov only. */
lsmash_file_parameters_t init_param = { 0 };
lsmash_open_file( "a.mp4", 0, &init_param );
init_param.mode &= ~LSMASH_FILE_MODE_MEDIA;
init_param.mode |= LSMASH_FILE_MODE_FRAGMENTED | LSMASH_FILE_MODE_SEGMENT;
init_param.major_brand = ISOM_BRAND_TYPE_ISO6;
init_param.brands      = brands;
init_param.brand_count = 2;
lsmash_file_t *init_file = lsmash_set_file( root, &init_param ); /* becomes the active file */
/* ... create tracks and sample entries here (moov lives in init_file) ... */

/* 2. Media Segment: it must not have INITIALIZATION, and must have MEDIA. */
lsmash_file_parameters_t seg_param = { 0 };
lsmash_open_file( "a_1.mp4", 0, &seg_param );
seg_param.mode &= ~LSMASH_FILE_MODE_INITIALIZATION;
seg_param.mode |= LSMASH_FILE_MODE_FRAGMENTED | LSMASH_FILE_MODE_INDEX | LSMASH_FILE_MODE_SEGMENT;
seg_param.major_brand = ISOM_BRAND_TYPE_MSDH;
lsmash_brand_type seg_brands[] = { ISOM_BRAND_TYPE_MSDH, ISOM_BRAND_TYPE_MSIX, ISOM_BRAND_TYPE_ISO6 };
seg_param.brands      = seg_brands;
seg_param.brand_count = 3;
lsmash_file_t *seg1 = lsmash_set_file( root, &seg_param ); /* must be called with the same root */

lsmash_adhoc_remux_t remux = { 0 };
remux.buffer_size = 4 * 1024 * 1024;
/* Finishes the current file (the initialization segment is completed as a moov-only file),
 * links seg1 to it as the initializer, and makes seg1 the active file. */
lsmash_switch_media_segment( root, seg1, &remux );
/* ... lsmash_create_fragment_movie(), lsmash_append_sample() into seg1 ... */
/* For the next segment, open and lsmash_set_file() another file with the same flags
 * and call lsmash_switch_media_segment() again. The previous segment is finished at that point. */
/* Finally: lsmash_finish_movie( root, &remux ), then close every file. */
```

`lsmash_switch_media_segment()` behaviour:
- It finishes the currently active file by writing its final fragment (and, for a media segment, its `sidx`), then activates `successor`. `styp` is written lazily just before the first `moof` of a media segment; a default `styp` is generated from the brands if none was set.
- The active file and the successor must both have `WRITE`, `BOX`, `FRAGMENTED` and `SEGMENT`. The successor must have `MEDIA` and must not have `INITIALIZATION`. The current file must have `MEDIA` or `INITIALIZATION`.
- Both files must belong to `root`, they must differ, and `remux` must not be NULL. Otherwise `LSMASH_ERR_FUNCTION_PARAM` is returned.
- Switching is one-way (previous to next). The caller closes each finished segment file.

#### Supported Modes & Current Limitations
- **Write requirements.** `LSMASH_FILE_MODE_INDEX` requires a seekable output. `lsmash_set_file()` returns NULL otherwise. Unseekable output (for example stdout) is possible only with `LSMASH_FILE_MODE_FRAGMENTED` and without `INDEX`, and no `sidx` or trailing `mfra` is produced.
- **`sidx` needs iso6.** A `sidx` entry is generated only when `INDEX` is set and the highest ISO brand is `iso6` or later. With an older brand the flag has no visible effect and no `sidx` is written.
- **Combinations outside the table are not supported.** Flag sets that do not satisfy the preconditions above (for example `SEGMENT` or `INDEX` without `FRAGMENTED`, or an `INITIALIZATION` file as the successor) are not covered by the library. They are rejected with `LSMASH_ERR_FUNCTION_PARAM` by `lsmash_switch_media_segment()`, and elsewhere their behaviour is not defined. The library does not return `LSMASH_ERR_PATCH_WELCOME` for any file-mode combination.
- **Segment index scope.** The `sidx` is written for the single level of a segment (one entry per movie fragment). A daisy-chained or hierarchical index for live streaming is not implemented, and the segment index is generated only for seekable output.
- **Initialization Segment cannot hold samples.** Without `MEDIA`, `lsmash_append_sample()` is rejected. Put samples only into media segments.
- **Reading.** `styp` and `sidx` are parsed and shown by `boxdumper`, and a `moov` establishes the initializer of the file. The `sidx` is not used to build the media timeline. The timeline is constructed from `moov` plus the `moof` boxes found in the same `lsmash_file_t`. A Media Segment file that does not contain its own `moov` is not linked to a separate Initialization Segment file at read time, so read a Self-Initializing Media Segment (or a concatenation of the initialization segment and the media segments) instead.
- **`LSMASH_ERR_PATCH_WELCOME` in the timeline API.** It is returned when a track mixes LPCM and non-LPCM sample descriptions during timeline construction, and when the timestamps of an LPCM track are changed by the timestamp-replacement API.

---

### 3.6 Edit Lists, Chapters & Container Metadata

#### Edit List (`edts` / `elst`)
`lsmash_create_explicit_timeline_map()` appends one edit to a track. Call it after the track has been set up and before the movie is finished. Durations are in the movie timescale and `start_time` is in the media timescale.

```c
/* Skip 1024 samples of encoder delay (media timescale = 48000) and present the rest. */
lsmash_edit_t edit;
edit.duration   = presentation_duration_in_movie_timescale;
edit.start_time = 1024;                 /* first composition time within the media */
edit.rate       = ISOM_EDIT_MODE_NORMAL; /* 1.0x */
lsmash_create_explicit_timeline_map( root, track_ID, edit );

/* An empty edit (initial silence / delayed start) */
lsmash_edit_t gap;
gap.duration   = delay_in_movie_timescale;
gap.start_time = ISOM_EDIT_MODE_EMPTY;
gap.rate       = ISOM_EDIT_MODE_NORMAL;
lsmash_create_explicit_timeline_map( root, track_ID, gap ); /* an empty edit must be appended before the edit that follows it */
```

`lsmash_count_explicit_timeline_map()`, `lsmash_get_explicit_timeline_map()` and `lsmash_modify_explicit_timeline_map()` inspect and change edits by 1-based edit number. `lsmash_delete_explicit_timeline_map()` removes all edits of a track.

#### Chapters and Container Metadata
```c
/* Nero-style chapter list in moov/udta/chpl, from a Nero/Ogg-style chapter text file (same format as muxer --chapter) */
lsmash_set_tyrant_chapter( root, "chapters.txt", 0 /* add_bom */ );
uint32_t n = lsmash_count_tyrant_chapter( root );

/* QuickTime-style chapter track referenced by a track */
lsmash_create_reference_chapter_track( root, video_track_ID, "chapters.txt" );
```

Further container-level metadata is attached through `lsmash_set_itunes_metadata( root, metadata )` (iTunes-style `meta`/`ilst` items, given as an `lsmash_itunes_metadata_t` value) and `lsmash_set_copyright( root, track_ID, ISO_language, notice )` (`cprt`; `track_ID == 0` applies it to the whole presentation). Call these before `lsmash_finish_movie()`.

---

### 3.7 Demultiplexing & Media Timeline Reconstruction

To read an existing standard or fragmented container and access samples in presentation/decode timeline order:

```c
lsmash_root_t *root = lsmash_create_root();
lsmash_file_parameters_t file_param = { 0 };
lsmash_open_file( "input.mp4", 1 /* read mode */, &file_param );
lsmash_file_t *file = lsmash_set_file( root, &file_param );
lsmash_read_file( file, &file_param );

/* Inspect track 1 summary (codec parameters, dimensions / audio channels) */
lsmash_summary_t *summary = lsmash_get_summary( root, 1, 1 );
/* ... inspect summary fields ... */
lsmash_cleanup_summary( summary );

/* Construct the unified media timeline (seamlessly spans moov->stbl and all moof->traf->trun fragments) */
lsmash_construct_timeline( root, 1 );
uint32_t sample_count = lsmash_get_sample_count_in_media_timeline( root, 1 );

for( uint32_t i = 1; i <= sample_count; i++ )
{
    lsmash_sample_t *s = lsmash_get_sample_from_media_timeline( root, 1, i );
    if( s )
    {
        /* Process s->data (s->length bytes), s->dts, s->cts, s->prop.ra_flags */
        lsmash_delete_sample( s );
    }
}

lsmash_destruct_timeline( root, 1 );
lsmash_close_file( &file_param );
lsmash_destroy_root( root );
```

---

### 3.8 Complete End-to-End C API Example

The following self-contained C program (compatible with C99, C11 and C++) muxes a codec-neutral workflow: an AAC-LC track (`mp4a` + `esds`, with a fixed AudioSpecificConfig) with an encoder-delay edit, finalizes it for progressive download (`moov` at the front), and reads it back through the media timeline API. Replace the dummy payloads and the sample entry setup of 3.3 / 3.4 to use any other codec.

```c
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <lsmash.h>

#define FRAME_COUNT 100
#define FRAME_SIZE  64
#define TIMESCALE   48000

int main( void )
{
    const char *path = "example_faststart.mp4";

    /* 1. Mux */
    lsmash_root_t *root = lsmash_create_root();
    lsmash_file_parameters_t param = { 0 };
    if( !root || lsmash_open_file( path, 0, &param ) < 0 )
        return 1;
    lsmash_brand_type brands[] = { ISOM_BRAND_TYPE_MP42, ISOM_BRAND_TYPE_ISOM };
    param.major_brand = ISOM_BRAND_TYPE_MP42;
    param.brands      = brands;
    param.brand_count = 2;
    if( !lsmash_set_file( root, &param ) )
        return 1;

    lsmash_movie_parameters_t movie_param;
    lsmash_initialize_movie_parameters( &movie_param );
    lsmash_set_movie_parameters( root, &movie_param );
    uint32_t movie_timescale = lsmash_get_movie_timescale( root );

    uint32_t tid = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
    lsmash_track_parameters_t track_param;
    lsmash_initialize_track_parameters( &track_param );
    track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
    lsmash_set_track_parameters( root, tid, &track_param );

    lsmash_media_parameters_t media_param;
    lsmash_initialize_media_parameters( &media_param );
    media_param.timescale = TIMESCALE;
    lsmash_set_media_parameters( root, tid, &media_param );

    /* AAC-LC, 48 kHz, stereo: AudioSpecificConfig = 0x11 0x90 */
    static uint8_t asc[2] = { 0x11, 0x90 };
    lsmash_audio_summary_t *asum =
        (lsmash_audio_summary_t *)lsmash_create_summary( LSMASH_SUMMARY_TYPE_AUDIO );
    asum->sample_type      = ISOM_CODEC_TYPE_MP4A_AUDIO;
    asum->frequency        = TIMESCALE;
    asum->channels         = 2;
    asum->sample_size      = 16;
    asum->samples_in_frame = 1024;
    asum->aot              = MP4A_AUDIO_OBJECT_TYPE_AAC_LC;
    asum->sbr_mode         = MP4A_AAC_SBR_NOT_SPECIFIED;
    lsmash_codec_specific_t *esds_cs = lsmash_create_codec_specific_data(
        LSMASH_CODEC_SPECIFIC_DATA_TYPE_MP4SYS_DECODER_CONFIG,
        LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
    lsmash_mp4sys_decoder_parameters_t *esds =
        (lsmash_mp4sys_decoder_parameters_t *)esds_cs->data.structured;
    esds->objectTypeIndication = MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3;
    esds->streamType           = MP4SYS_STREAM_TYPE_AudioStream;
    lsmash_set_mp4sys_decoder_specific_info( esds, asc, sizeof(asc) );
    lsmash_add_codec_specific_data( (lsmash_summary_t *)asum, esds_cs );
    lsmash_destroy_codec_specific_data( esds_cs );
    uint32_t entry = lsmash_add_sample_entry( root, tid, asum );
    lsmash_cleanup_summary( (lsmash_summary_t *)asum );
    if( entry == 0 )
        return 1;

    for( uint32_t i = 0; i < FRAME_COUNT; i++ )
    {
        lsmash_sample_t *s = lsmash_create_sample( FRAME_SIZE );
        memset( s->data, (int)(i & 0xFF), FRAME_SIZE ); /* dummy payload */
        s->dts           = (uint64_t)i * 1024;
        s->cts           = s->dts;
        s->index         = entry;
        s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
        if( lsmash_append_sample( root, tid, s ) < 0 )
        {
            lsmash_delete_sample( s );
            return 1;
        }
    }
    lsmash_flush_pooled_samples( root, tid, 1024 );

    /* Edit list: skip 1024 samples of encoder delay. */
    lsmash_edit_t edit;
    edit.duration   = (uint64_t)(FRAME_COUNT * 1024 - 1024) * movie_timescale / TIMESCALE;
    edit.start_time = 1024;
    edit.rate       = ISOM_EDIT_MODE_NORMAL;
    lsmash_create_explicit_timeline_map( root, tid, edit );
    lsmash_update_track_duration( root, tid, 0 );

    /* Progressive download: move moov in front of mdat. */
    lsmash_adhoc_remux_t remux = { 0 };
    remux.buffer_size = 4 * 1024 * 1024;
    if( lsmash_finish_movie( root, &remux ) < 0 )
        return 1;
    lsmash_close_file( &param );
    lsmash_destroy_root( root );

    /* 2. Demux and read back through the media timeline */
    lsmash_root_t *rroot = lsmash_create_root();
    lsmash_file_parameters_t rparam = { 0 };
    if( !rroot || lsmash_open_file( path, 1, &rparam ) < 0 )
        return 1;
    lsmash_file_t *file = lsmash_set_file( rroot, &rparam );
    if( !file || lsmash_read_file( file, &rparam ) < 0 )
        return 1;
    if( lsmash_construct_timeline( rroot, 1 ) < 0 )
        return 1;
    uint32_t count = lsmash_get_sample_count_in_media_timeline( rroot, 1 );
    printf( "Read %" PRIu32 " samples.\n", count );
    for( uint32_t i = 1; i <= count; i++ )
    {
        lsmash_sample_t *s = lsmash_get_sample_from_media_timeline( rroot, 1, i );
        if( s )
            lsmash_delete_sample( s );
    }
    lsmash_destruct_timeline( rroot, 1 );
    lsmash_close_file( &rparam );
    lsmash_destroy_root( rroot );
    return 0;
}
```

For fragmented or segmented output, apply the flag combinations of 3.5 to `param.mode` and replace the sample loop by one `lsmash_create_fragment_movie()` call per fragment.
