# L-SMASH

**L-SMASH** — *Loyal to Spec of Mpeg4 and Ad-hoc Simple Hackwork*

L-SMASH is a lightweight, high-performance, zero-external-dependency **C11** media container library (`liblsmash`, with public header `lsmash.h` maintaining strict C99 and C++ compatibility) and command-line toolset for reading, writing, remultiplexing, and inspecting **ISO Base Media File Format (ISOBMFF)** and **QuickTime File Format (QTFF)** files.

Designed for strict compliance with international standards, L-SMASH maintains a clean separation between ISOBMFF and QTFF binary layouts and refuses to emit non-conformant, cross-polluted container structures.

---

## Key Features & Supported Formats

### Container Standards
- **ISO/IEC 14496-12** — ISO Base Media File Format (ISOBMFF `iso1`–`iso6`, Standard & Fragmented Movies `moof`/`traf`/`trun`, Segment Index `sidx`, Random Access `mfra`/`tfra`, Sample Groups `sgpd`/`sbgp` for `rap `, `'roll'` [negative `roll_distance` pre-roll / positive post-roll], `'prol'` [audio pre-roll for non-sync streams])
- **ISO/IEC 14496-14** — MP4 File Format (`mp41`, `mp42`, MPEG-4 Systems `iods`/`esds`)
- **ISO/IEC 23009-1** — MPEG-DASH ISOBMFF Media Segments (`dash`, `msdh`, `msix`, Event Message `emsg`)
- **Apple QuickTime File Format (QTFF)** — QuickTime Movie (`qt  `) with native Sound Description v0/v1/v2, reference chapter tracks, and Track Aperture (`tapt`) / Clean Aperture (`clap`) / Pixel Aspect Ratio (`pasp`) extensions
- **3GPP / 3GPP2** — `3gp4`–`3gp6`, `3g2a`
- **Apple iTunes M4A / M4V** — `M4A `, `M4V `

### Video Codecs
| Codec Family | Sample Entry / Config FourCC | Specification | Support Scope |
| :--- | :--- | :--- | :--- |
| **AOM AV1** | `av01` + `av1C` | AOM AV1 Bitstream v1.0.0 Errata 1 & AV1-ISOBMFF v1.2.0 | Summary Construct / Raw Importer (`.ivf`) / Media Demux (Standard & Fragmented) |
| **ITU-T H.264 / MPEG-4 AVC** | `avc1`–`avc4`, `avcp` + `avcC`, `btrt` | ITU-T H.264 / ISO/IEC 14496-10 & ISO/IEC 14496-15 | Summary Construct / Raw Importer (Annex-B) / Media Demux |
| **ITU-T H.265 / MPEG-H HEVC** | `hvc1`, `hev1` + `hvcC`, `btrt` | ITU-T H.265 / ISO/IEC 23008-2 & ISO/IEC 14496-15 | Summary Construct / Raw Importer (Annex-B) / Media Demux |
| **SMPTE VC-1** | `vc-1` + `dvc1`, `btrt` | SMPTE 421M & RP 2025 | Summary Construct / Raw Importer (Advanced Profile) / Media Demux |
| **MPEG-4 Visual** | `mp4v` + `esds` | ISO/IEC 14496-2 & ISO/IEC 14496-14 | Summary Construct / Media Demux |
| **Apple ProRes** | `apch`, `apcn`, `apcs`, `apco`, `ap4h`, `ap4x` | Apple QuickTime File Format (QTFF) | Summary Construct / Media Demux |
| **QTFF Uncompressed Y'CbCr / RGB** | `raw `, `2vuy`, `v210`, `v216`, `v308`, `v408`, `v410`, `yuv2` | Apple QuickTime File Format (QTFF) | Summary Construct / Media Demux |
| **Dirac / Motion JPEG / Ut Video / Legacy QT** | `drac`, `mjp2`, `jpeg`, `mjpa`, `mjpb`, `png `, `ULRA`, `ULRG`, `ULY0`, `ULY2`, `ULH0`, `ULH2`, `UQY2`, `CFHD`, `dv*` | SMPTE ST 2042 / ISO/IEC 15444-3 / QTFF | Summary Construct / Media Demux |

### Audio Codecs
| Codec Family | Sample Entry / Config FourCC | Specification | Support Scope |
| :--- | :--- | :--- | :--- |
| **Xiph FLAC** | `fLaC` + `dfLa` | IETF RFC 9639 & FLAC-in-ISOBMFF | Summary Construct / Raw Importer (`.flac`) / Media Demux (Standard & Fragmented) |
| **MPEG-4 Audio (AAC / HE-AAC / ALS)** | `mp4a` + `esds` (or QT `wave`/`frma`/`esds`) | ISO/IEC 14496-3 & ISO/IEC 14496-14 | Summary Construct / Raw Importer (ADTS `.aac`, `.als`) / Media Demux |
| **IETF Opus** | `Opus` + `dOps` | IETF RFC 6716 & Opus-in-ISOBMFF with `'roll'` (`AudioRollRecoveryEntry`) pre-roll sample grouping and encoder-delay Edit List mapping | Summary Construct / Media Demux |
| **Dolby AC-3 & Enhanced AC-3** | `ac-3` + `dac3`, `ec-3` + `dec3` | ATSC A/52 & ETSI TS 102 366 Annex F | Summary Construct / Raw Importer (`.ac3`, `.eac3`) / Media Demux |
| **Apple Lossless (ALAC)** | `alac` + `alac` (or QT `wave`/`frma`/`alac`) | Apple Lossless Audio Codec / QTFF / ISOBMFF | Summary Construct / Media Demux |
| **DTS & DTS-HD** | `dtsc`, `dtsh`, `dtse`, `dtsl`, `dtsx` + `ddts` | ETSI TS 102 114 & DTS ISO/IEC 14496-12 Annex | Summary Construct / Raw Importer (`.dts`, `.dtshd`) / Media Demux |
| **ISOBMFF Uncompressed LPCM** | `ipcm`, `fpcm` + `pcmC` (+ `chnl`, `srat`) | ISO/IEC 23003-5 & ISO/IEC 14496-12 (`AudioSampleEntryV1`) | Summary Construct / Raw Importer (`.wav`) / Media Demux (Standard & Fragmented) |
| **QTFF Uncompressed LPCM** | `lpcm`, `sowt`, `twos`, `raw `, `in24`, `in32`, `fl32`, `fl64` (+ `wave`, `chan`) | Apple QuickTime File Format (QTFF Sound Desc v0/v1/v2) | Summary Construct / Raw Importer (`.wav`) / Media Demux |
| **3GPP AMR-NB / AMR-WB** | `samr`, `sawb` + `damr` | 3GPP TS 26.244 | Summary Construct / Raw Importer (`.amr`, `.awb`) / Media Demux |
| **MPEG-1/2 Audio Layer III (MP3)** | `mp4a` + `esds` / `.mp3` + `wave` | ISO/IEC 11172-3 / ISO/IEC 13818-3 / QTFF | Summary Construct / Raw Importer (`.mp3`) / Media Demux |
| **Windows Media Audio / Waveform** | `owma` + `wfex` | Microsoft `WAVEFORMATEX` / QTFF | Summary Construct / Media Demux |

### Timed Text & Subtitles
| Format | Sample Entry / Extension FourCC | Specification | Support Scope |
| :--- | :--- | :--- | :--- |
| **3GPP Timed Text** | `tx3g` + `ftab` (`FontTableBox`) | 3GPP TS 26.245 / ISO/IEC 14496-12 | Summary Construct / Reference Chapter Track / Media Demux |
| **QuickTime Text Media** | `text` (inside `gmhd`/`gmin`/`text`) | Apple QuickTime File Format (QTFF) | Summary Construct / Reference Chapter Track / Media Demux |

### Sample Entry Extensions, HDR, Spatial Media & Metadata
- **HDR, Color & Layered Video Extensions**:
  - **Color Parameter Box (`colr`)**: ISO/IEC 14496-12 `nclx` (with `full_range_flag`) and QuickTime `nclc` color primaries, transfer characteristics, and matrix coefficients, plus QTFF `gama`, `fiel`, `cspc`, and `sgbt`.
  - **HDR10 Static Metadata (`mdcv`, `clli`)**: SMPTE ST 2086 Mastering Display Color Volume (`mdcv`) and CTA-861.3 / ISO/IEC 14496-12 Content Light Level Information (`clli`).
  - **Dolby Vision (`dvcC`, `dvvC`)**: Dolby Vision Configuration Boxes (`dv_version_major/minor`, `dv_profile`, `dv_level`, `rpu/el/bl_present_flag`, `dv_bl_signal_compatibility_id`) and `dby1` compatible brand.
  - **Layered HEVC (`lhvC`)**: ISO/IEC 14496-15 L-HEVC Configuration Box parameter arrays.
- **Aperture & Pixel Aspect Ratio**:
  - **Clean Aperture (`clap`) & Pixel Aspect Ratio (`pasp`)**: Rational aperture dimensions/offsets and horizontal/vertical pixel spacing on visual sample entries.
  - **QuickTime Track Aperture Mode Dimensions (`tapt`)**: Track-level `clef` (Clean Aperture), `prof` (Production Aperture), and `enof` (Encoded Pixels) boxes.
- **Spatial Video & Stereoscopic 3D**:
  - **Google Spherical Video v2 & Stereoscopic 3D (`st3d`, `sv3d`)**: `st3d` stereoscopic mode signaling and `sv3d` (`svhd` metadata header + `proj` projection container with `prhd` pose header and `equi` equirectangular / `cbmp` cubemap / `prji` projection boxes).
  - **Apple Spatial Video (`vexu`, `hfov`)**: QuickTime / MV-HEVC Video Extended Usage (`vexu`) container with Stereo View (`eyes`), Required Box Types (`must`), Stereo View Information (`stri`), Hero Eye (`hero`), Camera System (`cams` + `blin` baseline in micrometers), Comfort Adjustment (`cmfy` + `dadj` disparity adjustment), and Horizontal Field of View (`hfov`).
- **Spatial Audio, Channel Layout & PCM Signaling**:
  - **Google Spatial Audio (`SA3D`)**: Ambisonic type, order, channel ordering, normalization, and channel map.
  - **QuickTime Audio Channel Layout (`chan`)**: CoreAudio channel layout tags, bitmaps, and per-channel `channelDescriptions` (`channelLabel`, `channelFlags`, `coordinates[3]`).
  - **ISO/IEC 14496-12 Audio Signaling (`chnl`, `srat`) & ISO/IEC 23003-5 (`pcmC`)**: CICP `definedLayout` / explicit `speaker_position` (`azimuth`, `elevation`) / `omittedChannelsMap` / `object_count` in `chnl`, 32-bit `sampling_rate` in `srat` for `AudioSampleEntryV1`, and endianness/bit-depth in `pcmC`.
- **Container Metadata & Chapters (Three Distinct Mechanisms)**:
  1. **Apple iTunes Metadata (`moov.udta.meta.ilst`) & QuickTime Metadata (`meta` / `keys` / `ilst`)**: Key-value tagging (album, artist, title, encoder, BPM, cover art, and custom `----` `mean`/`name`/`data` items) stored inside the Item List Box (`ilst`) under an ISO FullBox `meta` (`hdlr` subtype `'mdir'`) or QTFF BaseBox `meta` (`hdlr` subtype `'mdta'` with `keys`).
  2. **QuickTime / iTunes Reference Chapter Tracks (`tref.chap`)**: A dedicated timed text track (`text` in QTFF `.mov` or `tx3g` in `.mp4`/`.m4a`/`.m4v`) whose samples hold chapter titles at their exact presentation timestamps, linked from the main media track via a Track Reference Box (`tref`) of type `'chap'`.
  3. **Nero Chapter List (`moov.udta.chpl`)**: A single table-based FullBox (`chpl`) stored directly inside `moov.udta` listing chapter start timestamps (100 ns units) and UTF-8 chapter title strings (supported in `.mp4`/`.m4v`/`.m4a` alongside or independently of reference chapter tracks).

### Architecture Highlights
- **C11 Paged Block Array (`lsm_list_t`)**: High-volume Sample Tables (`stts`, `ctts`, `stsz`, `stz2`, `stss`, `stps`, `sdtp`, `stsc`, `stco`/`co64`, `sgpd`, `sbgp`, `elst`), Movie Fragment runs (`trun->optional`, `tfra`, `sidx`, `fragment->pool`), and Media Timelines (`info_list`, `bunch_list`, `chunk_list`) store value elements contiguously in `2^8 = 256`-element pages, providing `O(1)` bit-shift random access and pointer stability on append without per-entry linked-list node allocations.
- **Sentinel Box Architecture (`box_default`)**: Singleton child box pointers on parent structs follow the Null-Object Sentinel Pattern and never hold `NULL`; when a box is absent, pointers reference static `LSMASH_NON_EXISTING_BOX` sentinels, enabling deep hierarchical traversal (`trak->mdia->minf->stbl->stsd`) without null-pointer branches.
- **Compile-Time `static const` Dispatch Tables**: All Box readers, writers, destructors, printers, and codec summary dispatchers are immutable, thread-safe, ROM-placed (`static const`) tables.
- **Self-Contained Synthetic Test Suite & Memory Auditor**: Includes a built-in C11 memory auditor (`-DLSMASH_DEBUG_ALLOC`) with `#ptr`/`#size` expression capture, 64-byte aligned headers, front/rear canaries, `0xA5`/`0xDD` memory poisoning, and zero-leak verification across 15 synthetic test suites requiring zero external media files.

---

## Build Instructions & Self-Contained Test Suite

### Prerequisites
- A standard **C11 compiler** (`gcc` or `clang`)
- POSIX `sh`, GNU `make`, `ar`, `ranlib`
- **Zero external library dependencies**

### Building on Linux, macOS, or Windows (MSYS2 MinGW64)

```sh
./configure
make -j8
```

To install `liblsmash`, `lsmash.h`, `liblsmash.pc`, and the CLI tools:

```sh
make install
```

To build only the library without the CLI applications:

```sh
make -j8 lib
```

### Primary `configure` Options

| Option | Description |
| :--- | :--- |
| `--prefix=PREFIX` | Installation prefix (default: `/usr/local`) |
| `--bindir=DIR` | Executable installation directory (default: `PREFIX/bin`) |
| `--libdir=DIR` | Library and `pkgconfig` installation directory (default: `PREFIX/lib`) |
| `--includedir=DIR` | Header installation directory (default: `PREFIX/include`) |
| `--enable-shared` | Build shared library (`liblsmash.so`, `liblsmash.dylib`, or `liblsmash-2.dll`) |
| `--disable-static` | Do not build static library (`liblsmash.a`; requires `--enable-shared`) |
| `--enable-debug` | Compile with `-g3 -O0` and disable symbol stripping |
| `--cc=CC` | Specify C compiler executable (default: `gcc`) |
| `--cross-prefix=PREFIX` | Specify toolchain prefix for cross-compilation (e.g., `x86_64-w64-mingw32-`) |
| `--target-os=OS` | Override target OS detection (`mingw`, `cygwin`, `darwin`, `linux`, etc.) |
| `--extra-cflags=XCFLAGS` | Append custom C compiler flags (e.g., `--extra-cflags="-O3 -march=native"`) |
| `--extra-ldflags=XLDFLAGS` | Append custom linker flags |
| `--extra-libs=XLIBS` | Append additional libraries to link |

### Running the Self-Contained Test Suite

L-SMASH provides a self-contained C11 unit and integration test suite (`tests/test_main.c`) compiled with `-DLSMASH_DEBUG_ALLOC` (active memory canary, use-after-free poisoning, and zero-leak audit). It synthesizes all bitstreams in memory and requires **no external test files**:

```sh
make test
```

---

## CLI Tools Quick Start

Building `all` (the default target) produces four command-line utilities in `cli/`:

| Tool | Purpose |
| :--- | :--- |
| **`muxer`** | Multiplexes raw elementary streams (H.264, HEVC, AV1 `.ivf`, AAC ADTS, FLAC `.flac`, AC-3/E-AC-3, DTS, `.wav` LPCM, AMR, MP3, VC-1) or single-track containers into ISOBMFF (`.mp4`, `.m4a`, `.m4v`), QTFF (`.mov`), or 3GPP (`.3gp`, `.3g2`). |
| **`remuxer`** | Remultiplexes existing ISOBMFF/QTFF containers, converts standard movies to **Fragmented MP4** (`--fragment`) or **MPEG-DASH segments** (`--dash`), filters tracks, and performs RAP-aligned seeking. |
| **`boxdumper`** | Dumps the complete hierarchical Box tree (`--box`), per-sample DTS/CTS timestamps (`--timestamp`), or chapter lists (`--chapter`). |
| **`timelineeditor`** | Edits track timescales, rewrites VFR timestamps from Matroska timecode v2 files (`--timecode`), and constructs explicit Edit Lists (`--skip`, `--delay`, `--dts-compression`). |

### Common CLI Examples

```sh
# 1. Multiplex H.264 (Annex B) and AAC (ADTS) into an ISOBMFF MP4 with progressive download optimization
./cli/muxer -i video.h264?fps=24000/1001 -i audio.aac?language=eng --optimize-pd -o movie.mp4

# 2. Multiplex AV1 (.ivf) and 24-bit 96 kHz Stereo WAVE (.wav -> ISO/IEC 23003-5 ipcm) into MP4
./cli/muxer -i video.ivf?fps=24000/1001 -i audio_96k_24b.wav?language=jpn --optimize-pd -o av1_lpcm.mp4

# 3. Convert a standard MP4 into Fragmented MP4
./cli/remuxer -i movie.mp4 --fragment 1 --min-frag-duration 2.0 -o movie_frag.mp4

# 4. Inspect the complete Box hierarchy and sample tables
./cli/boxdumper --box movie_frag.mp4

# 5. Apply a Matroska v2 VFR timecode file to track 1
./cli/timelineeditor --track 1 --timecode timecodes_v2.txt movie.mp4 movie_vfr.mp4
```

For the complete CLI option reference and the `liblsmash` C API programming manual, see **[`USAGE.md`](USAGE.md)**: start with its [Quick Start & Tool Overview](USAGE.md#1-quick-start--tool-overview), and see the [C API Mental Model](USAGE.md#mental-model-workflow-and-object-hierarchy) if you program against `liblsmash`.

---

## Documentation & Architecture Specifications

| Document | Contents |
| :--- | :--- |
| **[`USAGE.md`](USAGE.md)** | Quick Start, complete user guide for `muxer`, `remuxer`, `boxdumper`, and `timelineeditor`, plus the **`liblsmash` C API Programming Manual** with end-to-end code examples. |
| **[`SPEC.md`](SPEC.md)** | Master Architecture & Specification Index (strict standard compliance principles, `box_default` Sentinel Box Architecture, Dual-List Box ownership, symbol naming conventions, and specification syntax identifier preservation). |
| **[`docs/CODING_STYLE.md`](docs/CODING_STYLE.md)** | Official C11 coding style, Allman layout conventions, internal header policy, symbol prefixes, and Git contribution guidelines. |
| **[`docs/HISTORY.md`](docs/HISTORY.md)** | Project history, branch lineage (`master`, `av1`, `vimeo`, `dev`), and architectural evolution. |
| **[`docs/specs/memory_architecture.md`](docs/specs/memory_architecture.md)** | C11 Paged Block Array (`lsm_list_t`) architecture, hybrid container design, and `-DLSMASH_DEBUG_ALLOC` memory audit specification. |
| **[`docs/specs/codecs_and_importers.md`](docs/specs/codecs_and_importers.md)** | Comprehensive architecture and container binding specification for all video and audio codecs and stream importers (`codecs/*`, `importer/*`). |
| **[`docs/specs/testing_strategy.md`](docs/specs/testing_strategy.md)** | Self-contained synthetic bitstream generation and negative testing strategy. |

---

## License

L-SMASH is distributed under the **ISC License**:

```text
Copyright (C) 2010-2026 L-SMASH project
Copyright (c) 2011-2026 Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```
