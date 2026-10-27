# L-SMASH User & C API Manual (`USAGE.md`)

This manual provides a comprehensive guide to the **L-SMASH** command-line utilities (**`lshmuxer`** and **`boxdumper`**, plus migration guidance for the legacy `muxer`, `remuxer`, and `timelineeditor` binaries) and the **`liblsmash` C API**.

---

## Table of Contents

1. [Quick Start & Tool Overview](#1-quick-start--tool-overview)
   - [1.1 Which Tool Do I Use?](#11-which-tool-do-i-use)
   - [1.2 One-Minute Recipes](#12-one-minute-recipes)
2. [Command-Line Tools (`cli/`)](#2-command-line-tools-cli)
   - [2.1 `lshmuxer` — Unified Multiplexer, Remultiplexer, Fragmenter & Timeline Editor](#21-lshmuxer--unified-multiplexer-remultiplexer-fragmenter--timeline-editor)
   - [2.2 `boxdumper` — Box Hierarchy, Timestamp & Chapter Inspector](#22-boxdumper--box-hierarchy-timestamp--chapter-inspector)
   - [2.3 CLI Concepts & Input Text-File Formats (Chapters & Timecodes)](#23-cli-concepts--input-text-file-formats-chapters--timecodes)
   - [2.4 Legacy CLI Tools (`muxer`, `remuxer`, `timelineeditor`) & Migration to `lshmuxer`](#24-legacy-cli-tools-muxer-remuxer-timelineeditor--migration-to-lshmuxer)
3. [`liblsmash` C API Programming Guide](#3-liblsmash-c-api-programming-guide)
   - [Mental Model: Workflow and Object Hierarchy](#mental-model-workflow-and-object-hierarchy)
   - [3.1 Core Lifecycle: Root, File Parameters & Movie Setup](#31-core-lifecycle-root-file-parameters--movie-setup)
   - [3.2 Track & Media Creation (`trak`, `tkhd`, `mdia`, `mdhd`)](#32-track--media-creation-trak-tkhd-mdia-mdhd)
   - [3.3 Video Sample Entry Setup (H.264/AVC, HEVC, AV1)](#33-video-sample-entry-setup-h264avc-hevc-av1)
   - [3.4 Audio Sample Entry Setup (AAC & ISO/IEC 23003-5 LPCM)](#34-audio-sample-entry-setup-aac--isoiec-23003-5-lpcm)
   - [3.5 Standard, Fragmented & Segmented Multiplexing](#35-standard-fragmented--segmented-multiplexing)
   - [3.6 Edit Lists, Chapters & Container Metadata](#36-edit-lists-chapters--container-metadata)
   - [3.7 Demultiplexing & Media Timeline Reconstruction](#37-demultiplexing--media-timeline-reconstruction)
   - [3.8 External Data References (`dref`)](#38-external-data-references-dref)
   - [3.9 Complete End-to-End C API Example](#39-complete-end-to-end-c-api-example)
4. [Container Architecture & Supported Formats Reference](#4-container-architecture--supported-formats-reference)
   - [4.1 Strict Compliance with International Standards](#41-strict-compliance-with-international-standards)
   - [4.2 Architectural Separation of ISOBMFF and QTFF](#42-architectural-separation-of-isobmff-and-qtff)
   - [4.3 Supported Codecs, Timed Text, Sample Entry Extensions & Metadata](#43-supported-codecs-timed-text-sample-entry-extensions--metadata)

---

## 1. Quick Start & Tool Overview

### 1.1 Which Tool Do I Use?

All command-line creation, remultiplexing, fragmentation, and timeline editing workflows are consolidated into **`lshmuxer`**, paired with **`boxdumper`** for container inspection:

| I want to... | Primary Tool | Section |
| :--- | :--- | :--- |
| Multiplex raw streams (H.264, HEVC, AV1, VC-1, AAC, FLAC, AC-3/E-AC-3, DTS, WAVE, AMR, MP3, ALS) into `.mp4` / `.mov` / `.m4a` / `.m4v` / `.3gp` / `.3g2` | **`lshmuxer`** | [2.1](#21-lshmuxer--unified-multiplexer-remultiplexer-fragmenter--timeline-editor) |
| Remultiplex existing containers, filter/remove tracks, trim from a keyframe, or generate **Fragmented MP4** (`moof`/`trun`) and **MPEG-DASH segments** (`sidx`) | **`lshmuxer`** | [2.1](#21-lshmuxer--unified-multiplexer-remultiplexer-fragmenter--timeline-editor) |
| Apply a VFR Matroska timecode v1/v2 file or edit skip / delay / timescales on raw or container tracks (in 1 pass or standalone) | **`lshmuxer`** | [2.1](#21-lshmuxer--unified-multiplexer-remultiplexer-fragmenter--timeline-editor) |
| Inspect a file's Box/Atom hierarchy (`--box`), per-sample DTS/CTS timestamps (`--timestamp`), or chapters (`--chapter`) | **`boxdumper`** | [2.2](#22-boxdumper--box-hierarchy-timestamp--chapter-inspector) |
| Migrate existing scripts from the legacy `muxer`, `remuxer`, or `timelineeditor` tools | **`lshmuxer`** | [2.4](#24-legacy-cli-tools-muxer-remuxer-timelineeditor--migration-to-lshmuxer) |
| Read or write MP4 / MOV from your own C/C++ program | **`liblsmash`** | [3](#3-liblsmash-c-api-programming-guide) |

### 1.2 One-Minute Recipes
```sh
# 1. Multiplex H.264 + AAC into a web-friendly MP4 (moov placed first so playback starts while downloading)
./cli/lshmuxer -i video.h264?fps=30000/1001 -i audio.aac?language=eng,encoder-delay=2112 --optimize-pd -o movie.mp4

# 2. Convert an existing MP4 into a Fragmented MP4 (2-second fragments aligned to track 1)
./cli/lshmuxer -i movie.mp4 --fragment 1 --min-frag-duration 2.0 -o movie_frag.mp4

# 3. Inspect a file: dump complete Box tree to stdout
./cli/boxdumper --box movie.mp4

# 4. Apply VFR timestamps from a Matroska timecode v2 file to track 1 of an existing MP4
./cli/lshmuxer -i movie.mp4?1:timecode=timecodes.txt -o movie_vfr.mp4

# 5. 1-Pass Multiplex + VFR Timecode + Chapter + Audio in a single command
./cli/lshmuxer -i video.h264?timecode=timecodes.txt -i audio.aac?language=jpn,encoder-delay=2112 \
               --chapter chapters.txt --optimize-pd -o movie_vfr_1pass.mp4
```
See [2.3](#23-cli-concepts--input-text-file-formats-chapters--timecodes) for explanations of `--optimize-pd`, `encoder-delay`, and `--shift-timeline`, as well as the text file formats for `--chapter` and `timecode=`.

---

## 2. Command-Line Tools (`cli/`)

### 2.1 `lshmuxer` — Unified Multiplexer, Remultiplexer, Fragmenter & Timeline Editor

`lshmuxer` is the unified 1-pass command-line multiplexer, container remultiplexer, MP4 fragmenter / MPEG-DASH segmenter, and VFR timeline editor. It accepts any combination of raw elementary streams and existing ISOBMFF/QTFF containers in a single invocation, applies optional per-track timecode v1/v2 or Edit List modifications in memory, and writes a standard, progressive-download-optimized, fragmented, or DASH-segmented output container.

#### Synopsis
```sh
lshmuxer [global_options] -i input1[?track_options] [-i input2[?track_options] ...] -o output.[mp4|mov|m4a|m4v|3gp|3g2]
```

#### Supported Input Formats (`lshmuxer`)
- **Containers (Standard & Fragmented)**:
  - ISOBMFF / MP4 (`.mp4`, `.m4a`, `.m4v`), Apple QuickTime (`.mov`), and 3GPP / 3GPP2 (`.3gp`, `.3g2`), including multi-track containers and files with external `dref` media references.
- **Raw Video Elementary Streams**:
  - ITU-T H.264 / MPEG-4 AVC Annex-B byte stream (`.h264`, `.264`, `.avc`)
  - ITU-T H.265 / MPEG-H HEVC Annex-B byte stream (`.h265`, `.265`, `.hevc`)
  - AOM AV1 in IVF container (`.ivf`)
  - SMPTE VC-1 Advanced Profile elementary stream (`.vc1`)
- **Raw & Single-Track Audio Streams**:
  - Xiph FLAC stream (`.flac`, RFC 9639 / FLAC-in-ISOBMFF `fLaC` + `dfLa`)
  - MPEG-4 AAC ADTS stream (`.aac`, `.adts`)
  - MPEG-4 Audio Lossless Coding stream (`.als`)
  - Dolby AC-3 (`.ac3`) and Enhanced AC-3 (`.eac3`, `.ec3`)
  - DTS and DTS-HD stream (`.dts`, `.dtshd`)
  - RIFF WAVE / RF64 Uncompressed LPCM (`.wav`: `WAVE_FORMAT_PCM`, `WAVE_FORMAT_IEEE_FLOAT`, `WAVE_FORMAT_EXTENSIBLE`; mapped to ISO/IEC 23003-5 `ipcm`/`fpcm` + `pcmC`/`chnl`/`srat` in ISOBMFF or `lpcm`/`sowt`/`in24`/`fl32` Sound Descriptions in QTFF `.mov`)
  - 3GPP AMR-NB and AMR-WB (`.amr`, `.awb`)
  - MPEG-1/2 Audio Layer III (`.mp3`)

#### Global Options (`lshmuxer`)

| Option | Argument | Description |
| :--- | :--- | :--- |
| `--help` | *(none)* | Display help message and exit. |
| `--version` | *(none)* | Display L-SMASH version and exit. |
| `-i` | `<path>[?<opts>]` | Specify an input file (raw stream or container) and optional per-track options. |
| `-o` | `<path>` | Specify the output file path (the file extension selects default container brands). |
| `--file-format` | `mp4\|mov\|3gp\|3g2\|m4a\|m4v` | Explicitly specify the output container format regardless of file extension. |
| `--isom-version` | `<1..6>` | Specify maximum compatible ISO Base Media version (`iso1`–`iso6`). |
| `--brands` | `<4CC,4CC,...>` | Specify comma-separated compatible brand FourCCs (`--brands help` lists known brands). |
| `--major-brand` | `<4CC>` | Specify major brand FourCC in `ftyp`. |
| `--minor-version` | `<integer>` | Specify minor version integer in `ftyp`. |
| `--optimize-pd` | *(none)* | Optimize for progressive download (places `moov` before `mdat`; FastStart). |
| `--interleave` | `<integer>` | Specify maximum interleave chunk duration in milliseconds (default: `1000`). |
| `--max-chunk-duration` | `<integer>` | Alias for `--interleave` (maximum chunk duration in milliseconds). |
| `--max-chunk-size` | `<integer>` | Specify maximum chunk size in bytes (default: `4194304` = 4 MiB). |
| `--movie-timescale` | `<integer>` | Specify movie timescale (`mvhd.timescale`). |
| `--shift-timeline` | *(none)* | Enable composition-to-decode timeline shift (`ctts` v1 / QTFF negative composition offsets + `cslg`, or Edit List). |
| `--compact-size-table` | *(none)* | Compress Sample Size boxes (`stsz` → `stz2`) when all sample sizes fit in 4, 8, or 16 bits. |
| `--fragment` | `<track_num>` | Enable **Fragmented Movie (`moof`/`traf`/`trun`)** output using 1-based `<track_num>` as the reference track for fragment boundaries (cannot be combined with `--optimize-pd`). |
| `--min-frag-duration` | `<float>` | Specify minimum fragment duration in seconds when `--fragment` is active. |
| `--last-frag-merge-thresh` | `<float>` | Threshold in `(0.0, 1.0)` below which the trailing fragment is merged into its predecessor (requires `--fragment` and `--min-frag-duration`). |
| `--dash` | `[integer]` | Enable **MPEG-DASH ISOBMFF** output (`sidx` + `dash`/`msdh`/`msix`/`iso6` brands; requires `--fragment`). Pass `0` (or omit the integer) for a single self-initializing segment; pass `N >= 1` to write a separate Initialization Segment (`output.mp4`) and Media Segments (`output_1.mp4`, `output_2.mp4`, ...) containing `N` subsegments each. |
| `--chapter` | `<path>` | Set chapters from a timestamp-based or Ogg-style chapter text file (writes a `moov.udta.chpl` User Data chapter list in `.mp4`/`.m4v`/`.m4a` and/or a `tref.chap` reference chapter track in `.mov`/`.mp4`/`.m4v`/`.m4a`). |
| `--chpl-with-bom` | *(none)* | Prepend a 3-byte UTF-8 BOM (`0xEF 0xBB 0xBF`) to chapter title strings in the `moov.udta.chpl` chapter list box (`mp4`/`m4v`/`m4a`). |
| `--chapter-track` | `<integer>` | Specify which 1-based output track number references the `tref.chap` chapter track (default: `1`). |
| `--copyright-notice` | `<arg>` | Set movie-level copyright notice (`cprt` in `moov.udta`) as `<language>/<string>` or `<string>` (`mp4`/`m4v`/`m4a`/`3gp`/`3g2`). |
| `--language` | `<string>` | Specify the default 3-letter ISO-639-2/T language code for all output tracks. |
| `--dry-run` | *(none)* | Perform full input probing, option validation, and timeline calculation without writing output files. |

#### iTunes Metadata Options (`lshmuxer`, valid for `mp4`, `m4a`, `m4v`)
| Option | Argument | Description |
| :--- | :--- | :--- |
| `--album-name` | `<string>` | Album title (`©alb`). |
| `--artist` | `<string>` | Track artist (`©ART`). |
| `--comment` | `<string>` | User comment (`©cmt`). |
| `--release-date` | `<YYYY-MM-DD>` | Release date string (`©day`). |
| `--encoder` | `<string>` | Person or organization that encoded the recording (`©enc`). |
| `--genre` | `<string>` | Genre name (`©gen`). |
| `--lyrics` | `<string>` | Song lyrics (`©lyr`). |
| `--title` | `<string>` | Track or movie title (`©nam`). |
| `--composer` | `<string>` | Composer (`©wrt`). |
| `--album-artist` | `<string>` | Artist for the whole album (`aART`). |
| `--copyright` | `<string>` | iTunes copyright tag (`cprt` in `ilst`). |
| `--description` | `<string>` | Description string (`desc`). |
| `--grouping` | `<string>` | Content grouping (`©grp`). |
| `--tempo` / `--beats-per-minute` | `<integer>` | Tempo in beats per minute (`tmpo`). |

#### Per-Input & Per-Track Options (`-i input?[track_num:]opt1=val1,opt2=val2[?track_num:...]`)
Per-track options are appended to an input path after `?`.
- **Single-track inputs (or Track 1 shorthand)**: The `<track_num>:` prefix may be omitted (e.g., `-i video.h264?fps=24000/1001,par=1:1`).
- **Multi-track container inputs**: Prefix each track's comma-separated option list with its 1-based input track number and a colon, separating multiple tracks with `?` (e.g., `-i movie.mp4?1:safe-seek=3,language=eng?2:remove`).

| Track Option | Value Format | Scope | Description |
| :--- | :--- | :--- | :--- |
| `remove` | *(flag)* | Container | Exclude this input track from the output movie. |
| `disable` | *(flag)* | Raw / Container | Clear `ISOM_TRACK_ENABLED` in `tkhd.flags` so the track is disabled by default. |
| `language=<arg>` | 3-letter code | Raw / Container | Set ISO-639-2/T media language code (e.g., `language=eng`, `language=jpn`). |
| `alternate-group=<int>` | `<integer>` | Raw / Container | Set track alternate group ID (`tkhd.alternate_group`). |
| `handler=<string>` | `<string>` | Raw / Container | Set media handler name (`hdlr.name`). |
| `copyright=<arg>` | `<str>[/<lang>]` | Raw / Container | Set track-level copyright notice (`trak.udta.cprt`). |
| `fps=<arg>` | `<int>` or `<num>/<den>` | Raw Video | Specify video frame rate (e.g., `fps=24000/1001`, `fps=60`). |
| `constant-delay=<int>` | `<integer>` | Raw Video | Add a constant composition delay (`CTS = DTS + delay * timebase`) to every sample. |
| `par=<w>:<h>` | `<int>:<int>` | Raw Video | Set Pixel Aspect Ratio (`pasp` box and scaled `tkhd.width`, e.g., `par=16:15`). |
| `dv-profile=<arg>` | `<profile>[.<compat>]` | Raw Video | Signal Dolby Vision configuration (`dvcC`/`dvvC` and `dby1` brand, e.g., `dv-profile=8.1`). |
| `stereo-mode=<arg>` | `<mode>` | Video | Set Stereoscopic 3D (`st3d`) mode: `monoscopic`, `top-bottom`, `left-right`, `custom`, `right-left`. |
| `projection=<arg>` | `<type>` | Video | Set Google Spherical Video v2 (`sv3d`) projection: `equirectangular` or `cubemap`. |
| `ypr=<y>/<p>/<r>` | `<flt>/<flt>/<flt>` | Video | Set Spherical Video v2 pose header (`prhd`) yaw, pitch, and roll in degrees (requires `projection=`). |
| `encoder-delay=<int>` | `<integer>` | Raw / Container | Represent audio priming delay (in media timescale samples) to skip via Edit List (`elst`) and pre-roll (`'roll'`). |
| `sbr` | *(flag)* | AAC Audio | Enable explicit backward-compatible HE-AAC Spectral Band Replication (SBR) signaling in `esds`. |
| `ambisonic-order=<int>` | `<integer>` | Audio | Emit Google Spatial Audio (`SA3D`) box with the specified ambisonic order (`>= 0`, where `(order + 1)^2 == channels`). |
| `seek=<int>` | `<integer>` | Container | Start remultiplexing from 1-based sample number `<int>`. |
| `safe-seek=<int>` | `<integer>` | Container | Seek to the Random Access Point (RAP) at or preceding sample `<int>` and prepend an Edit List to skip leading frames prior to `<int>`. |
| `timecode=<path>` | `<path>` | Raw / Container | Rewrite this track's timeline in 1 pass using a Matroska timecode v1 or v2 file. |
| `media-timescale=<int>` | `<integer>` | Raw / Container | Convert this track's media timescale (`mdhd.timescale`) to `<int>` (`timescale=<int>` is also accepted as an alias). |
| `media-timebase=<int>` | `<integer>` | Raw / Container | Convert this track's timestamp unit step (timebase) to `<int>` (`timebase=<int>` is also accepted as an alias). |
| `skip=<arg>` | `<int>` or `<num>/<den>` | Raw / Container | Skip duration at the start of this track's presentation via Edit List. |
| `delay=<arg>` | `<int>` or `<num>/<den>` | Raw / Container | Insert an Empty Edit delay before this track's presentation via Edit List. |
| `dts-compression` | *(flag)* | Raw / Container | Eliminate initial composition-to-decode delay on this track via DTS compression. |

> [!WARNING]
> **`dts-compression` can break HRD / VBV conformance.** This option squeezes the DTS spacing of the initial frames and compensates with an Edit List. A video encoder's rate control (VBV / HRD) assumes frames leave the CPB / VBV buffer at the original, regular DTS spacing. After DTS compression, the decoder must remove data from its buffer *earlier* than the encoder planned, so the buffer model collapses and a **buffer underflow** can occur. This is especially serious for hardware decoders and strict broadcast or streaming profiles. For streams that must stay HRD / VBV conformant, do not use `dts-compression`, and keep `skip=` / `delay=` edits minimal. See [3.6.1](#361-what-an-edit-list-really-is) for the underlying timeline model.

#### `lshmuxer` Practical Examples
```sh
# 1. Multiplex H.264 Annex-B video + AAC ADTS audio with priming compensation, chapters, and iTunes metadata
./cli/lshmuxer -i video.h264?fps=30000/1001 -i audio.aac?language=eng,encoder-delay=2112 \
               --chapter chapters.txt --title "Demo Movie" --optimize-pd -o movie.mp4

# 2. Multiplex AV1 (.ivf) + 5.1ch 24-bit 48 kHz WAVE into ISOBMFF MP4 (emits av01/av1C + ipcm/pcmC/chnl)
./cli/lshmuxer -i stream.ivf?fps=24 -i surround_51ch_24bit.wav?language=eng --optimize-pd -o av1_51ch.mp4

# 3. Multiplex WAVE into QuickTime .mov (emits QTFF lpcm/sowt/in24 Sound Description)
./cli/lshmuxer -i surround_51ch_24bit.wav -o audio.mov

# 4. Remux only track 1 (removing track 2) starting at sample 30 with RAP-aligned safe Edit List trimming
./cli/lshmuxer -i movie.mp4?1:safe-seek=30?2:remove --optimize-pd -o trimmed.mp4

# 5. Convert a standard MP4 into a Fragmented MP4 with 2.0-second fragments aligned to track 1
./cli/lshmuxer -i movie.mp4 --fragment 1 --min-frag-duration 2.0 -o movie_frag.mp4

# 6. Convert an MP4 into an MPEG-DASH self-initializing media segment (--dash 0) or separate segments (--dash 1)
./cli/lshmuxer -i movie.mp4 --fragment 1 --dash 0 --min-frag-duration 4.0 -o segment_dash.mp4
./cli/lshmuxer -i movie.mp4 --fragment 1 --dash 1 --min-frag-duration 4.0 -o seg_init.mp4

# 7. Apply a Matroska timecode v2 file and Edit List delay to track 1 in 1 pass from raw streams
./cli/lshmuxer -i video.h264?timecode=tc_v2.txt,delay=100/1000 -i audio.aac?language=jpn --optimize-pd -o vfr.mp4
```

---

### 2.2 `boxdumper` — Box Hierarchy, Timestamp & Chapter Inspector

`boxdumper` inspects the internal Box/Atom hierarchy, codec configuration records, sample tables, per-sample timestamps, and chapter lists of ISOBMFF and QTFF files.

#### Synopsis
```sh
boxdumper [options] input
```

#### Options (`boxdumper`)

| Option | Description |
| :--- | :--- |
| `--box` | Dump the complete Box/Atom hierarchy, headers, sample description extensions (`avcC`, `hvcC`, `av1C`, `dvc1`, `dfLa`, `pcmC`, `chnl`, `srat`, `esds`, `dac3`, `dec3`, `ddts`, `dOps`, `alac`, etc.), and sample table entries (default mode when no flag is given). |
| `--chapter` | Extract and print the chapter list (`moov.udta.chpl` or `tref.chap` reference chapter track) in chapter text format. |
| `--timestamp [track_ID]` | Dump the media timescale (`mdhd.timescale`) and per-sample Decode Timestamps (`DTS`) and Composition Timestamps (`CTS`) for `track_ID` (default: track `1`). |

#### `boxdumper` Practical Examples
```sh
# Dump full Box hierarchy to stdout
./cli/boxdumper --box movie.mp4

# Dump DTS/CTS timestamps of track 1
./cli/boxdumper --timestamp 1 movie.mp4

# Extract chapter list to stdout
./cli/boxdumper --chapter movie.mp4
```

---

### 2.3 CLI Concepts & Input Text-File Formats (Chapters & Timecodes)

- **`--optimize-pd` (FastStart / progressive download)**: By default, a single-pass MP4 writes the Movie Box (`moov`) after the Media Data Box (`mdat`), requiring a player to seek to the end of the file before starting playback. `--optimize-pd` relocates `moov` in front of `mdat` and adjusts all chunk offsets (`stco`/`co64`) so web and streaming playback can begin immediately while downloading.
- **`encoder-delay=<n>`**: Lossy audio encoders (such as AAC, Opus, and MP3) prepend `n` priming samples of silence at stream startup. Specifying `encoder-delay=<n>` writes an Edit List (`elst` `media_time = n`) and an audio pre-roll sample group (`'roll'` with negative `roll_distance`) so the player discards the priming silence and maintains exact A/V synchronization (see [3.6.1](#361-what-an-edit-list-really-is)).
- **`--shift-timeline`**: When video contains B-frames, a frame's composition timestamp can precede its decoding timestamp. `--shift-timeline` shifts composition offsets using signed negative offsets (`ctts` version 1 in ISOBMFF `iso4`+ or signed `ctts` version 0 in QuickTime, accompanied by a `cslg` Composition-to-Decode Box) or an Edit List so the earliest presentation starts at `0` without delaying decode times (see [3.6.2](#362-dual-timelines-and-cslg)).
- **Delay precision (`delay=`)**: An Empty Edit delay is expressed in the **Movie Timescale (`mvhd.timescale`)**. See [3.6.5](#365-empty-edit-and-movie-timescale-precision-the-sample-accuracy-trap) when sample-accurate audio delay is required.

#### Chapter Text File Formats (`--chapter`)
L-SMASH accepts two plain-text chapter formats (auto-detected from the first line, with optional leading UTF-8 BOM):

1. **Ogg / OGM Style (`CHAPTERxx=` / `CHAPTERxxNAME=` pairs)**:
   ```text
   CHAPTER01=00:00:00.000
   CHAPTER01NAME=Opening
   CHAPTER02=00:03:25.500
   CHAPTER02NAME=Main Story
   ```
2. **Timestamp-Based Minimum Style (`hh:mm:ss.mmm <title>` per line)**:
   ```text
   00:00:00.000 Opening
   00:03:25.500 Main Story
   ```

Depending on the target container format, `--chapter chapters.txt` populates:
- **Reference Chapter Track (`tref.chap`)**: Available in QuickTime (`.mov`) and iTunes/MP4 (`.mp4`, `.m4v`, `.m4a`). Creates a dedicated timed text track (`text` in `.mov`, `tx3g` in `.mp4`/`.m4v`/`.m4a`) linked from `--chapter-track` (default: track `1`) via a `'chap'` Track Reference Box (`tref`).
- **User Data Chapter List (`moov.udta.chpl`)**: Available in `.mp4`, `.m4v`, and `.m4a` (and 3GPP family). Writes a table-based `chpl` FullBox inside `moov.udta` storing chapter start timestamps in 100 ns units and UTF-8 title strings (pass `--chpl-with-bom` to prefix each title string with a UTF-8 BOM).

#### Matroska Timecode Format v1 / v2 Text Files (`timecode=`)
- **Format v2 (Per-Frame Presentation Timestamps in Milliseconds)**:
  ```text
  # timecode format v2
  0
  41.708
  83.417
  125.125
  166.833
  ```
  The first line must be `# timecode format v2`. Each subsequent non-comment line gives the presentation timestamp in milliseconds of one frame in composition order (`N` lines for `N` frames). An optional `(N + 1)`-th timestamp line (strictly greater than the `N`-th line) may be provided to specify the explicit presentation end timestamp of the final frame (so the final frame's composition duration is the difference between the `(N + 1)`-th and `N`-th timestamps); if omitted, the final frame's duration is estimated from the preceding frame interval with a statistical outlier guard against long still frames.
- **Format v1 (Baseline Frame Rate + Override Frame Ranges)**:
  ```text
  # timecode format v1
  Assume 23.976
  100,249,29.970
  ```
  The first line must be `# timecode format v1`, followed by `Assume <FPS>` (baseline frame rate) and zero or more `start_frame,end_frame,<FPS>` lines overriding the frame rate for inclusive 0-based composition frame ranges (through the final frame `N - 1`).

---

### 2.4 Legacy CLI Tools (`muxer`, `remuxer`, `timelineeditor`) & Migration to `lshmuxer`

The standalone binaries `muxer`, `remuxer`, and `timelineeditor` are **legacy compatibility tools** retained in `cli/` for existing scripts and differential regression testing (`tests/test_cli_equivalence.c`). All of their capabilities are unified in **`lshmuxer`**, which is the recommended replacement for all new and migrated workflows.

#### Quick Migration Reference (`muxer` / `remuxer` / `timelineeditor` → `lshmuxer`)

| Legacy Command | Equivalent `lshmuxer` Command | Migration Notes |
| :--- | :--- | :--- |
| `muxer [opts] -i raw1?opt=val -i raw2 -o out.mp4` | `lshmuxer [opts] -i raw1?opt=val -i raw2 -o out.mp4` | **100% drop-in compatible.** All `muxer` global options, iTunes metadata flags, and per-input `?opt=val` flags work identically in `lshmuxer`. |
| `remuxer [opts] -i in.mp4?1:opt=val?2:remove -o out.mp4` | `lshmuxer [opts] -i in.mp4?1:opt=val?2:remove -o out.mp4` | **100% drop-in compatible.** Per-track options for multiple tracks on a single input are separated by `?` (`?1:...?2:...`) in both tools. |
| `timelineeditor [opts] in.mp4 out.mp4` | `lshmuxer -i in.mp4?[track:]opt=val -o out.mp4` | Map `timelineeditor` options (`--track <n>`, `--timecode <f>`, `--skip <s>`, `--delay <d>`, `--dts-compression`, `--media-timescale <ts>`, `--media-timebase <tb>` / `--Media-timescale <tb>`) to `lshmuxer` per-track options (`-i in.mp4?[n:]timecode=<f>,skip=<s>,delay=<d>,dts-compression,media-timescale=<ts>,media-timebase=<tb>`). |
| **3-step pipeline**: `muxer` → `timelineeditor` → `remuxer` | **Single 1-pass command**: `lshmuxer -i raw.h264?timecode=tc.txt -i audio.aac -o out.mp4` | Eliminates intermediate temporary MP4 files by applying VFR timecode editing, multiplexing, and optional fragmentation in a single pass. |

---

## 3. `liblsmash` C API Programming Guide

Include the public header `<lsmash.h>` and link with `-llsmash` (or `pkg-config --cflags --libs liblsmash`).

### Mental Model: Workflow and Object Hierarchy

#### Workflow at a Glance

**Muxing (writing)**
1. Create the Root and File (`lsmash_create_root`, `lsmash_open_file`, `lsmash_set_file`).
2. Create the Track and Media (`lsmash_create_track`, `lsmash_set_media_parameters`).
3. Register Summaries, i.e. Sample Entries with Codec Configuration (`lsmash_add_sample_entry`).
4. Append Samples in a loop (`lsmash_append_sample`).
5. Flush, Finish the Movie and Destroy (`lsmash_flush_pooled_samples`, `lsmash_finish_movie`, `lsmash_destroy_root`).

**Demuxing (reading)**
1. Open the File and read the headers (`lsmash_open_file`, `lsmash_read_file`).
2. Get Summaries, i.e. codec information (`lsmash_get_summary`).
3. Construct the Media Timeline and get Samples (`lsmash_construct_timeline`, `lsmash_get_sample_from_media_timeline`).
4. Destroy the Timeline and close (`lsmash_destroy_root`).

#### Why Are There Several Nested Structures?
Think of the file as a small hierarchy of clocks and catalogs:

| Level | Box | What it is for a programmer |
| :--- | :--- | :--- |
| Movie | `mvhd` | The common clock of the whole file (Movie Timescale = Presentation Timeline). |
| Track | `tkhd` | The display frame on screen (width / height, enabled flag) plus the Edit List that maps the media onto the Presentation Timeline. |
| Media | `mdhd` | The clock of one stream (Media Timescale) and its language code. |
| Summary | `stsd` Sample Entry | A **catalog for initializing the decoder**: resolution, sample rate, and Codec Specific Data such as SPS/PPS or AudioSpecificConfig. |

The catalog is registered first, separately from the frame data. Every frame (`lsmash_sample_t`) then only carries the catalog number, for example `sample->index = 1`.

#### Codec Specific Data: Structured vs. Unstructured
- `LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED`: a C structure (for example `lsmash_h264_specific_parameters_t`) whose fields you can read and write directly.
- `LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED`: the raw bytes of the Box as stored in the file.

When demuxing, `lsmash_convert_codec_specific_format()` converts a codec-specific entry into the format you prefer. See the ownership and index cheat sheet in [3.5](#35-standard-fragmented--segmented-multiplexing) for pointer ownership, 1-based indexes and `lsmash_flush_pooled_samples`.

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

> [!IMPORTANT]
> **Ownership and index cheat sheet**
> - `lsmash_append_sample()`: on **success** ownership of the sample moves to `liblsmash` (never free it). On **failure** the caller must call `lsmash_delete_sample()`.
> - Indexes (`sample->index`, sample description, edit number, `dref` index, track number from CLI) are **1-based**; `0` is invalid.
> - `lsmash_flush_pooled_samples( root, track_id, last_sample_delta )` is required at the end because `stts` stores the difference between consecutive DTS values; the last sample's duration is unknown until you provide it.
> - Pick `timescale` so that every frame / audio-frame duration is an integer (audio: sampling rate; video: e.g. 30000 for 29.97 fps with delta 1001).
> - Raw stream vs. MP4 sample: strip Annex-B start codes and prefix each NAL unit with its length; strip ADTS headers; codec-specific data goes in `STRUCTURED` form (via `lsmash_set_codec_specific_data` with parsed structures) or `UNSTRUCTURED` (raw `avcC`/`esds` payload).

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
With `lsmash_finish_movie( root, NULL )`, `moov` is written after `mdat`. To place `moov` before `mdat` (FastStart), pass an `lsmash_adhoc_remux_t` instead. L-SMASH then rearranges the already-written file in place, moving the data through a working buffer of `buffer_size` bytes (internally raised to at least twice the `moov` size). This requires a seekable output. The optional `func` callback receives the bytes processed so far and the total; if it returns non-zero, the operation is aborted and `lsmash_finish_movie()` returns a negative error code. Since the file is being rearranged in place, an aborted output must be discarded. Set `func` to `NULL` if no progress report is needed.

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

For constant-size, constant-duration audio tracks such as ISO/IEC 23003-5 LPCM, L-SMASH automatically hoists the common sample duration and byte size into the Track Fragment Header Box (`tfhd` `default_sample_duration` and `default_sample_size`), omitting redundant per-sample `trun` array rows.

#### Segmented ISOBMFF (`LSMASH_FILE_MODE_SEGMENT`)
`lsmash_open_file( path, 0, &param )` (write mode) pre-sets `param.mode` to `LSMASH_FILE_MODE_WRITE | LSMASH_FILE_MODE_BOX | LSMASH_FILE_MODE_INITIALIZATION | LSMASH_FILE_MODE_MEDIA`. Segment output is obtained by adjusting the flags before `lsmash_set_file()`:

| Output | `param.mode` (starting from the write-mode default) | Resulting layout |
| :--- | :--- | :--- |
| Fragmented MP4 (self-contained) | `+ FRAGMENTED` | `ftyp` `moov` (`moof` `mdat`)... `mfra` (the trailing `mfra` is written only when seekable) |
| Self-Initializing Media Segment (`lshmuxer --fragment 1 --dash 0`) | `+ FRAGMENTED + INDEX + SEGMENT` | `ftyp` `moov` `sidx` (`moof` `mdat`)... (no `mfra`) |
| Initialization Segment (separate file) | `- MEDIA + FRAGMENTED + SEGMENT` | `ftyp` `moov` (no samples can be appended) |
| Media Segment (separate file) | `- INITIALIZATION + FRAGMENTED + INDEX + SEGMENT` (`MEDIA` kept) | `styp` `sidx` (`moof` `mdat`)... |

The remaining abbreviations drop the `LSMASH_FILE_MODE_` prefix. These are the combinations that `cli/lshmuxer.c` builds, and they are the ones to use.

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
- **`sidx` needs iso6.** A `sidx` entry is generated only when `INDEX` is set and the highest ISO brand is `iso6` or later. If `lsmash_set_file()` is given `INDEX` for an initializing file whose brands do not reach `iso6`, it returns NULL instead of silently dropping the `sidx`.
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

#### 3.6.1 What an Edit List Really Is
The name is literal: an **Edit List** is a list of *edits* in the editing sense. It was designed in QuickTime / ISOBMFF as a **non-destructive cut-editing mechanism**: without re-encoding or rewriting any media data on the Media Timeline, it picks arbitrary ranges (`start_time`, `duration`), optionally at another speed (`media_rate`) or as a gap (Empty Edit, `start_time = -1`), and lines them up on the Presentation Timeline.

Many developers regard it as a charm for fixing A/V sync. Common uses are only special cases of the same cut-and-paste model: correcting the initial CTS offset of video, or skipping audio encoder delay (priming samples), both mean "cut the Media Timeline from a later point and paste it at Presentation time `0`".

```
Media Timeline (mdhd.timescale)         Presentation Timeline (mvhd.timescale)
 0    1024                     N
 |-----|=======================|          Edit #1: empty, duration = D
 priming   kept range                      Edit #2: start_time = 1024
 (skipped)    \                            0     D
               \  elst                     |-----|=======================|
                +------------------------>  gap   kept range (rate 1.0)
```

#### 3.6.2 Dual Timelines and `cslg`
```
Presentation Timeline  (mvhd.timescale)   what the user sees; edits (elst) map into it
        ^
        | elst: Media Timeline -> Presentation Timeline
        |
Media Timeline         (mdhd.timescale)   per-track clock
   +-- Decoding Timeline     DT  (stts)   when each sample is decoded
   +-- Composition Timeline  CT  (ctts)   when each sample is shown;  CT = DT + offset
```
With B-frames, CT can become smaller than DT. To avoid negative offsets the Composition Timeline is shifted later, and the shift is declared by `cslg` (CompositionToDecodeBox: `compositionToDTSShift`, `leastDecodeToDisplayDelta`, and so on).

```
 DT : 0   1   2   3        without shift: CT(0) = -1  (negative, not allowed in version 0 ctts)
 CT :-1   2   0   1
 CT': 0   3   1   2        shifted by +1, declared by cslg  -> first CT' = 1 > 0 after shift
```

#### 3.6.3 No Edit List: the Implicit 1:1 Mapping and the Undefined Origin
Without an Edit List, the two timelines map 1:1, but the standard does **not define which Media Timeline point corresponds to Presentation `0`**. If the first sample has `CT > 0` and no Edit List exists, players legitimately differ:

```
Media Timeline:      0     CT0=2002(first sample)
                     |......|======== video ========
Player A (QuickTime Player etc.): Media 0 -> Presentation 0
Presentation:        0......|======== video ========   initial delay of CT0 (A/V offset)
Player B (others):   first CT -> Presentation 0
Presentation:        0======== video ========         no delay
```
Both behaviours are valid per the specification. To make every player present at the same timing, write an explicit Edit List (`start_time = CT0`, which maps the first sample to Presentation `0`).

#### 3.6.4 Warning: Edit Lists and the Decoder Buffer (HRD / VBV)
An Edit List only defines *which interval is shown*. It knows nothing about when the decoder reads the bitstream or removes frames from the CPB / VBV buffer. Skipping leading frames or splicing several ranges with careless edits forces the decoder to decode all preceding reference frames at once before the Presentation start time, which can cause **buffer underflow** and stutter. For the same reason, `dts-compression` (see [2.1](#21-lshmuxer--unified-multiplexer-remultiplexer-fragmenter--timeline-editor)) can invalidate the encoder's HRD / VBV model. Keep edits minimal and start on a random access point for streams that must stay buffer-conformant.


#### 3.6.5 Empty Edit and Movie Timescale Precision: the Sample-Accuracy Trap
In ISOBMFF / QTFF the **Presentation Timeline** and the **Movie Timeline** are the same time axis. Its clock is defined by the Movie Header Box (`mvhd.timescale`, the Movie Timescale).

Each `elst` entry mixes two clocks:

| Field | Meaning | Unit |
| :--- | :--- | :--- |
| `start_time` (`media_time`) | where to start cutting inside the media | **`mdhd.timescale`** (Media Timescale of the track) |
| `duration` (`segment_duration`) | length on the Presentation Timeline | **`mvhd.timescale`** (Movie Timescale) |

Skipping the head of a track (for example AAC `encoder-delay`) uses `start_time`, so it is always **Sample-Accurate**. An **Empty Edit** (`start_time = -1`, `LSMASH_TIMESTAMP_UNDEFINED`) has no media at all: it is a pure Presentation Timeline concept whose only parameter is `duration`. A delay inserted at the head can therefore only be specified in **Movie Timescale** granularity.

If the Movie Timescale is coarser than the audio Media Timescale (or not an integer multiple of it), an Empty Edit cannot delay audio by an exact number of samples, and rounding causes A/V sync drift. Typical cases:

| `mvhd.timescale` | 48 kHz audio | 44.1 kHz audio |
| :--- | :--- | :--- |
| `600` (QuickTime tradition, common multiple of 24/25/30/50/60 fps) | 1 tick = 80 samples; only multiples of 80 samples are possible | not divisible; always rounded |
| `1000` | not divisible (48 samples per tick); always rounded | not divisible |
| `9000` / `90000` | `90000` is not divisible by 48000; `9000` neither | not divisible |

**Countermeasure:** when you need a Sample-Accurate Empty Edit on audio, set the Movie Timescale equal to the audio Media Timescale (the sampling rate, such as `48000` or `44100`). For audio-only movies (`.m4a` etc.) this is always the right choice, and L-SMASH `lshmuxer` applies it when a single track is output (it resets the movie timescale to that track's media timescale). When audio and video share one file, either choose a Movie Timescale that is an integer multiple of the audio sampling rate, or avoid Empty Edit for the audio and express the offset with `start_time` (skip) instead. With `lshmuxer`, set `--movie-timescale` alongside `?delay=`.

**Why the Movie Timescale is not set to the LCM of the track timescales:** with several tracks (for example video `30000` and audio `44100`), the least common multiple (`4,410,000` here) would make every Movie Timescale tick extremely fine. Long durations and large sample durations expressed in such ticks can easily overflow `mvhd` / `tkhd` / `elst` `duration` fields and intermediate calculations, especially in Version 0 boxes where these fields are 32-bit. For multi-track output L-SMASH therefore deliberately keeps a safe, modest fixed value (such as `600` or `1000`) and accepts the coarser Empty Edit granularity as a design trade-off; use the countermeasures above when sample-accurate audio delay matters.

#### Chapters and Container Metadata
```c
/* User Data chapter list in moov.udta.chpl, from a timestamp-based or Ogg-style chapter text file (same format as lshmuxer --chapter) */
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

/* Construct the unified media timeline (seamlessly spans moov.trak.mdia.minf.stbl and all moof.traf.trun fragments) */
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

### 3.8 External Data References (`dref`)

#### Mechanism

Every track owns a Data Reference Box (`dref`) inside `minf` / `dinf`. Each entry (a `'url '` Box) tells where the media data of the sample descriptions that point to it (`data_reference_index` in the sample entry, `lsmash_summary_t::data_ref_index` in the API) is stored:

- **Self-contained** entry: the `'url '` flags have bit 0 (`0x000001`) set and carry no location. The samples are in the same file as the movie boxes. L-SMASH adds this entry automatically when a track has none.
- **External** entry: the flags are `0` and the `'url '` carries the location (a URL or file name) of another file that holds the samples. The movie file then contains only `moov` and the sample tables (`stsc`, `stco`, `stsz`, ...), whose chunk offsets address the external file.

The public type is `lsmash_data_reference_t`: `index` is the 1-based position in the `dref`, and `location` is the string of the referenced file (`NULL` for a self-contained entry).

#### Writing: movie file plus external media file

The referenced file is an ordinary output file opened with `lsmash_open_file()` and registered with `lsmash_set_file()`. It carries samples only, so replace its mode with `LSMASH_FILE_MODE_WRITE | LSMASH_FILE_MODE_MEDIA` (no `BOX`, no `INITIALIZATION`) after opening it.

```c
lsmash_root_t *root = lsmash_create_root();

/* The movie file keeps the boxes (moov). */
lsmash_file_parameters_t movie_param = { 0 };
lsmash_open_file( "movie.mp4", 0, &movie_param );
lsmash_file_t *movie_file = lsmash_set_file( root, &movie_param );

/* The external file keeps only the raw sample data. */
lsmash_file_parameters_t media_param = { 0 };
lsmash_open_file( "media.bin", 0, &media_param );
media_param.mode = LSMASH_FILE_MODE_WRITE | LSMASH_FILE_MODE_MEDIA;
lsmash_file_t *media_file = lsmash_set_file( root, &media_param );

/* ... lsmash_set_movie_parameters(), lsmash_create_track(), lsmash_set_track_parameters(),
 *     lsmash_set_media_parameters() ... */

/* Entries are numbered in creation order.
 * Optional: pass the movie file with location == NULL to create the self-contained entry as index 1. */
lsmash_data_reference_t ref = { .index = 0, .location = "media.bin" };  /* location as it will be resolved by a reader */
lsmash_create_data_reference( root, track_ID, &ref, media_file );       /* ref.index is set to the new index */

/* Point the sample descriptions of the track to the entry before adding them. */
summary->data_ref_index = ref.index;
lsmash_add_sample_entry( root, track_ID, summary );

/* Append samples as usual. They are written to the external file. */
lsmash_append_sample( root, track_ID, sample );
lsmash_flush_pooled_samples( root, track_ID, last_sample_delta );
lsmash_finish_movie( root, NULL );

lsmash_close_file( &media_param );
lsmash_close_file( &movie_param );
lsmash_destroy_root( root );
```

`lsmash_create_data_reference()` returns a negative value when the file does not belong to `root`, is not writable, lacks both `MEDIA` and `INITIALIZATION`, or when a fragmented file is involved (see the limitations below). Passing the movie file itself or a `NULL` location creates a self-contained entry.

#### Reading: resolving the external file

The library never opens a referenced file by itself. The application reads the location, opens the file, and assigns it to the entry before constructing the timeline.

```c
lsmash_root_t *root = lsmash_create_root();
lsmash_file_parameters_t movie_param = { 0 };
lsmash_open_file( "movie.mp4", 1 /* read mode */, &movie_param );
lsmash_file_t *movie_file = lsmash_set_file( root, &movie_param );
lsmash_read_file( movie_file, &movie_param );

uint32_t count = lsmash_count_data_reference( root, track_ID );
for( uint32_t i = 1; i <= count; i++ )
{
    lsmash_data_reference_t ref = { .index = i, .location = NULL };
    if( lsmash_get_data_reference( root, track_ID, &ref ) < 0 )
        continue;
    if( ref.location )      /* external entry (NULL means self-contained) */
    {
        /* Resolve ref.location relative to the movie file yourself. */
        lsmash_file_parameters_t media_param = { 0 };
        lsmash_open_file( resolved_path, 1, &media_param );
        media_param.mode |= LSMASH_FILE_MODE_MEDIA;
        lsmash_file_t *media_file = lsmash_set_file( root, &media_param );
        lsmash_assign_data_reference( root, track_ID, ref.index, media_file );
        /* Keep media_param open until the timeline is destructed. */
    }
    lsmash_cleanup_data_reference( &ref );
}

lsmash_construct_timeline( root, track_ID );
/* lsmash_get_sample_from_media_timeline() now reads each sample from the file that its chunk references. */
```

`lsmash_assign_data_reference()` only affects external entries; calling it for a self-contained entry succeeds and changes nothing. `cli/lshmuxer.c` follows exactly this procedure when it opens an input movie that has external references.

#### Current Limitations

- **Movie Fragments are not supported with external data references.** A normal movie (`moov` / `stbl`) can reference external files, but a Movie Fragment (`moof` / `traf`) layout with a non-self-contained `'url '` is not implemented. Fragments address their samples relative to the Movie Fragment Box and write them into the movie file itself, so `lsmash_create_data_reference()` returns `LSMASH_ERR_FUNCTION_PARAM` when either the movie file or the referenced file is fragmented. A self-contained entry in a fragmented movie is still accepted.
- **Only `'url '` entries are written.** `'urn '` entries are not generated.
- **The referenced file must be a plain media container.** A box-structured file (for example another MP4) cannot be used as the referenced data yet.
- **Reading never follows the location automatically.** The application is responsible for opening and assigning every external file. Samples of an external entry that was not assigned are not read from the external file, so assign every external entry before calling lsmash_construct_timeline().

#### Security Warning (Arbitrary File Disclosure / Path Traversal via dref)

> **Security Warning (Arbitrary File Disclosure / Path Traversal via dref):** An attacker can craft a container whose `dref` contains a non-self-contained `'url '` Box with a `location` such as `../../keys/private.key` or a local absolute path. If a media server or application passes the `location` obtained by `lsmash_get_data_reference()` unchecked to `lsmash_open_file()` / `fopen()`, binds it with `lsmash_assign_data_reference()`, and remuxes the movie into a file for delivery, the binary contents of the private key or confidential file are copied as media sample data into the `mdat` Box of the output. The attacker then obtains the file by playing or downloading the output.

Application developers must follow these rules:

1. When processing or remuxing media uploaded by untrusted users, reject external data references (non-self-contained `dref` entries) by default and do not call `lsmash_assign_data_reference()`.
2. Even in a trusted workflow that allows external references, never pass `dref->location` directly to `fopen()` / `lsmash_open_file()`. Reject path traversal (`..`), absolute paths, scheme specifications (`file://`, `http://`, etc.) and symbolic links, and confine the resolved (canonicalized) path strictly inside a dedicated media directory.

---

### 3.9 Complete End-to-End C API Example

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

---

## 4. Container Architecture & Supported Formats Reference

### 4.1 Strict Compliance with International Standards
L-SMASH enforces strict compliance with the specifications governing each container family:
- **ISO/IEC 14496-12 (ISOBMFF)** and **Apple QuickTime File Format (QTFF)** share common historical roots (`ftyp`, `moov`, `trak`, `mdia`, `minf`, `stbl`), yet diverge significantly in their Sample Description (`stsd`), Channel Layout, Pixel Aspect Ratio / Clean Aperture, and Audio Sample Entry binary layouts.
- L-SMASH **never mixes QTFF-only atoms or fields into ISOBMFF files**, nor ISOBMFF-only boxes into QuickTime `.mov` files.

### 4.2 Architectural Separation of ISOBMFF and QTFF
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

### 4.3 Supported Codecs, Timed Text, Sample Entry Extensions & Metadata

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
  3. **User Data Chapter List (`moov.udta.chpl`)**: A table-based chapter list box stored inside `moov.udta` for `.mp4`/`.m4v`/`.m4a` files, enabled via `--chapter` (and optional `--chpl-with-bom`).
