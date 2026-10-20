# L-SMASH

**L-SMASH** — *Loyal to Spec of Mpeg4 and Ad-hoc Simple Hackwork*

L-SMASH is a lightweight, high-performance, zero-external-dependency **Pure C99** media container library (`liblsmash`) and command-line toolset for reading, writing, remultiplexing, and inspecting **ISO Base Media File Format (ISOBMFF)** and **QuickTime File Format (QTFF)** files.

Designed for strict compliance with international standards, L-SMASH maintains a clean separation between ISOBMFF and QTFF binary layouts and refuses to emit non-conformant, cross-polluted container structures.

---

## Key Features & Supported Formats

### Container Standards
- **ISO/IEC 14496-12** — ISO Base Media File Format (ISOBMFF `iso1`–`iso6`, Standard & Fragmented Movies `moof`/`traf`/`trun`, Segment Index `sidx`, Random Access `mfra`/`tfra`, Sample Groups `sgpd`/`sbgp` for `rap `, `roll`, `prol`)
- **ISO/IEC 14496-14** — MP4 File Format (`mp41`, `mp42`, MPEG-4 Systems `iods`/`esds`)
- **ISO/IEC 23009-1** — MPEG-DASH ISOBMFF Media Segments (`dash`, `msdh`, `msix`, Event Message `emsg`)
- **Apple QuickTime File Format (QTFF)** — QuickTime Movie (`qt  `) with native Sound Description v0/v1/v2, reference chapter tracks, and Clean Aperture (`clap`) / Pixel Aspect Ratio (`pasp`) extensions
- **3GPP / 3GPP2** — `3gp4`–`3gp6`, `3g2a`
- **Apple iTunes Metadata** — `M4A `, `M4V ` (`meta`/`ilst` tagging and Nero `chpl` chapters)

### Video Codecs
- **ITU-T H.264 / ISO/IEC 14496-10 AVC (`avc1`, `avc3`)** — Built-in Annex-B NAL unit importer, SPS/PPS/SEI parser, `avcC` configuration, Recovery Point (`roll`) and Random Access Point (`rap `) sample grouping
- **ITU-T H.265 / ISO/IEC 23008-2 HEVC (`hvc1`, `hev1`)** — Built-in Annex-B NAL unit importer, VPS/SPS/PPS/SEI parser, `hvcC` configuration, CRA/BLA open-GOP (`rap `) and Gradual Decoding Refresh (`roll`) sample grouping
- **AOM AV1 (`av01`)** — Built-in OBU parser (**AV1 Bitstream & Decoding Process v1.0.0 Errata 1** and **AV1-ISOBMFF v1.2.0**), `av1C` configuration, IVF elementary stream importer, multi-tile `OBU_FRAME` / `OBU_TILE_GROUP` reconstruction, `stss` Sync Sample vs. `OPEN_RAP` classification, ITU-T T.35 HDR10+ detection, and Fragmented MP4 support
- **SMPTE VC-1 (`vc-1`)** — Advanced Profile elementary stream importer and `dvc1` configuration
- **ISO/IEC 14496-2 MPEG-4 Visual (`mp4v`)**
- **Apple ProRes (`apch`, `apcn`, `apcs`, `apco`, `ap4h`, `ap4x`)** & **QTFF Uncompressed Y'CbCr / RGB (`raw `, `2vuy`, `v210`, `v216`, `v308`, `v408`, `v410`, `r408`)**
- **HDR, Dolby Vision & Spatial Video Signaling** — Mastering Display Color Volume (`mdcv`), Content Light Level Info (`clli`), Dolby Vision (`dvcC`/`dvvC`, `dby1`), Stereoscopic 3D (`st3d`), and Spherical Video v1/v2 (`sv3d`/`svhd`/`proj`/`prhd`/`equi`/`cbmp`)

### Audio Codecs
- **ISO/IEC 14496-3 MPEG-4 Audio (`mp4a`)** — AAC-LC, HE-AAC v1/v2 (explicit/implicit SBR & PS signaling, ADTS elementary stream importer, `esds` descriptor configuration), and MPEG-4 ALS (`als `)
- **ATSC A/52 AC-3 (`ac-3` / `dac3`) & Enhanced AC-3 (`ec-3` / `dec3`)** — Built-in elementary stream importers and specific configuration boxes
- **IETF Opus (`Opus` / `dOps`)** — With pre-roll (`prol`) sample grouping and encoder delay Edit List mapping
- **Apple Lossless (`alac`)**
- **DTS Coherent Acoustics & DTS-HD (`dtsc`, `dtsh`, `dtse`, `dtsl` / `ddts`)** — Built-in elementary stream importer and `ddts` configuration
- **3GPP AMR-NB (`samr`) & AMR-WB (`sawb`)**, **MPEG-1/2 Layer III (`mp4a` / `.mp3`)**, **Windows Media Audio (`owma` / `wfex`)**, **Google Spatial Audio Ambisonics (`SA3D`)**
- **Uncompressed LPCM (ISO/IEC 23003-5 & QTFF)** — Native ISOBMFF integer (`ipcm`: 16/24/32-bit big/little-endian) and IEEE 754 floating-point (`fpcm`: 32/64-bit) audio with `pcmC`, `chnl` (ISO/IEC 14496-12 CICP / explicit speaker positions / object counts), `srat`, and `AudioSampleEntryV1` (`> 65535 Hz`) across standard and Fragmented ISOBMFF; QTFF Sound Description v0/v1/v2 (`raw `, `sowt`, `twos`, `in24`, `in32`, `fl32`, `fl64`, `lpcm`, `chan`, `wave`); and RIFF WAVE (`.wav`) importer supporting `WAVE_FORMAT_PCM`, `WAVE_FORMAT_IEEE_FLOAT`, and `WAVE_FORMAT_EXTENSIBLE`

### Architecture Highlights
- **C99 Paged Block Array (`lsm_list_t`)**: High-volume Sample Tables (`stts`, `ctts`, `stsz`, `stz2`, `stss`, `stps`, `sdtp`, `stsc`, `stco`/`co64`, `sgpd`, `sbgp`, `elst`), Movie Fragment runs (`trun->optional`, `tfra`, `sidx`, `fragment->pool`), and Media Timelines (`info_list`, `bunch_list`, `chunk_list`) store value elements contiguously in $2^8 = 256$-element pages, providing $O(1)$ bit-shift random access and pointer stability on append without per-entry linked-list node allocations.
- **Sentinel Box Architecture (`box_default`)**: Singleton child box pointers on parent structs follow the Null-Object Sentinel Pattern and never hold `NULL`; when a box is absent, pointers reference static `LSMASH_NON_EXISTING_BOX` sentinels, enabling deep hierarchical traversal (`trak->mdia->minf->stbl->stsd`) without null-pointer branches.
- **Compile-Time `static const` Dispatch Tables**: All Box readers, writers, destructors, printers, and codec summary dispatchers are immutable, thread-safe, ROM-placed (`static const`) tables.
- **Self-Contained Synthetic Test Suite & Memory Auditor**: Includes a built-in C99 memory auditor (`-DLSMASH_DEBUG_ALLOC`) with `#ptr`/`#size` expression capture, 64-byte aligned headers, front/rear canaries, `0xA5`/`0xDD` memory poisoning, and zero-leak verification across 14 synthetic test suites requiring zero external media files.

---

## Build Instructions & Self-Contained Test Suite

### Prerequisites
- A standard **C99 compiler** (`gcc` or `clang`)
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

L-SMASH provides a self-contained C99 unit and integration test suite (`tests/test_main.c`) compiled with `-DLSMASH_DEBUG_ALLOC` (active memory canary, use-after-free poisoning, and zero-leak audit). It synthesizes all bitstreams in memory and requires **no external test files**:

```sh
make test
```

---

## CLI Tools Quick Start

Building `all` (the default target) produces four command-line utilities in `cli/`:

| Tool | Purpose |
| :--- | :--- |
| **`muxer`** | Multiplexes raw elementary streams (H.264, HEVC, AV1 `.ivf`, AAC ADTS, AC-3/E-AC-3, DTS, `.wav` LPCM, AMR, MP3, VC-1) or single-track containers into ISOBMFF (`.mp4`, `.m4a`, `.m4v`), QTFF (`.mov`), or 3GPP (`.3gp`, `.3g2`). |
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

For the complete CLI option reference and the `liblsmash` C API programming manual, see **[`USAGE.md`](USAGE.md)**.

---

## Documentation & Architecture Specifications

| Document | Contents |
| :--- | :--- |
| **[`USAGE.md`](USAGE.md)** | Complete user guide for `muxer`, `remuxer`, `boxdumper`, and `timelineeditor`, plus the **`liblsmash` C API Programming Manual** with end-to-end C99 code examples. |
| **[`SPEC.md`](SPEC.md)** | Master Architecture & Specification Index (strict standard compliance principles, `box_default` Sentinel Box Architecture, Dual-List Box ownership, symbol naming conventions, and specification syntax identifier preservation). |
| **[`docs/HISTORY.md`](docs/HISTORY.md)** | Project history, branch lineage (`master`, `av1`, `vimeo`, `dev`), and architectural evolution. |
| **[`docs/specs/memory_architecture.md`](docs/specs/memory_architecture.md)** | C99 Paged Block Array (`lsm_list_t`) architecture, hybrid container design, and `-DLSMASH_DEBUG_ALLOC` memory audit specification. |
| **[`docs/specs/av1_obuparse_removal.md`](docs/specs/av1_obuparse_removal.md)** | Built-in AV1 OBU parser and AV1-ISOBMFF v1.2.0 specification. |
| **[`docs/specs/lpcm_isobmff_fragmented.md`](docs/specs/lpcm_isobmff_fragmented.md)** | ISO/IEC 23003-5 LPCM (`ipcm`/`fpcm`/`pcmC`) and ISO/IEC 14496-12 (`chnl`, `srat`, `AudioSampleEntryV1`) in standard and Fragmented ISOBMFF specification. |
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
