# L-SMASH Master Specification & Architecture Index (`SPEC.md`)

## 1. Core Philosophy & Architectural Mandates

### 1.1 strict standard compliance: Zero Tolerance for Non-Conformant Bitstreams & Containers
L-SMASH ("Loyal to Spec of Mpeg4 and Ad-hoc Simple Hackwork") is engineered as a reference-grade implementation of ISO Base Media File Format (ISOBMFF: ISO/IEC 14496-12) and QuickTime File Format (QTFF). All code and specifications in this repository operate under **strict standard compliance (Strict International Standard Compliance)**:
- **No Spec-Violating Hacks**: Multiplexed outputs must strictly conform to the target specification brands (`ftyp` / `styp` major and compatible brands). Never emit proprietary or out-of-spec boxes into a container whose negotiated brands forbid them.
- **Strict Separation of ISOBMFF and QTFF**:
  - ISOBMFF (ISO/IEC 14496-12, ISO/IEC 14496-14, ISO/IEC 14496-15, ISO/IEC 23003-5, AOM AV1-ISOBMFF) and QTFF share historical roots in `stsd`, `tkhd`, `hdlr`, `ctts`, `sdtp`, and `udta`, but diverge in critical binary layouts and semantics.
  - Runtime dispatch and box serialization must strictly distinguish between ISOBMFF and QTFF via `file->qt_compatible`, `file->isom_compatible`, `file->max_isom_version`, and box-level `box->manager & LSMASH_QTFF_BASE`.
  - Uncompressed PCM audio in Fragmented ISOBMFF must never rely on QTFF Sound Description v1/v2 (`lpcm`, `sowt`, `twos`, `in24`, `fl32`, `chan`, `wave`), because QTFF has no official Movie Fragment (`moof`/`traf`/`trun`) specification. Fragmented PCM audio in ISOBMFF must strictly implement **ISO/IEC 23003-5 (`ipcm` / `fpcm` Sample Entry + `pcmC` Box)** and **ISO/IEC 14496-12 (`chnl` ChannelLayout Box + `srat` SamplingRate Box + `AudioSampleEntryV1`)**.
- **ISC License Integrity & Engineering**:
  - All core library, codec parser, and importer code in L-SMASH is distributed under the **ISC License**.
  - External third-party dependencies with incompatible licenses or unnecessary coupling (specifically the external `obuparse` library introduced in the `vimeo` fork) must be eliminated in favor of L-SMASH's native implementations authored under the ISC License.

---

## 2. System Architecture Overview

L-SMASH is structured into four primary subsystems with strict directional dependencies (`cli/` $\rightarrow$ `importer/` $\rightarrow$ `codecs/` $\leftrightarrow$ `core/` $\rightarrow$ `common/`):

```
+-----------------------------------------------------------------------+
|                           Public API (lsmash.h)                       |
+-----------------------------------------------------------------------+
        |                           |                           |
        v                           v                           v
+---------------+           +---------------+           +---------------+
|     cli/      | --------> |   importer/   | --------> |    codecs/    |
|  muxer.c      |           |  isobm_imp.c  |           |  description  |
|  remuxer.c    |           |  wave_imp.c   |           |  h264 / hevc  |
|  boxdumper.c  |           |  ivf_imp.c    |           |  av1 (native) |
|  timeline...  |           |  nalu/adts... |           |  mp4a/ac3/dts |
+---------------+           +---------------+           +---------------+
        |                           |                           |
        +---------------------------+---------------------------+
                                    |
                                    v
+-----------------------------------------------------------------------+
|                              core/                                    |
|  box.{c,h} | box_default.{c,h} | box_type.{c,h} | file.{c,h}          |
|  isom.{c,h}| read.{c,h}        | write.{c,h}    | fragment.{c,h}      |
|  timeline.{c,h}                | print.{c,h}    | summary.{c,h}       |
+-----------------------------------------------------------------------+
                                    |
                                    v
+-----------------------------------------------------------------------+
|                             common/                                   |
|  internal.h | alloc.h | bytes.{c,h} | bits.{c,h} | list.{c,h}         |
|  math.h     | multichar.h           | osdep.{c,h}| utils.{c,h}        |
+-----------------------------------------------------------------------+
```

### 2.1 Core Architectural Pillars
1. **Hierarchical Box Tree & `box_default` (Null-Object Sentinel Pattern Pattern)**:
   - Every ISOBMFF/QTFF Box struct begins with `ISOM_BASE_BOX` (`core/box.h`), embedding common metadata (`class`, `root`, `file`, `parent`, `destruct`, `extensions`, `offset_in_parent`, `nonexist_ptr`, `manager`, `precedence`, `size`, `type`, `pos`).
   - Singleton child box pointers on parent structs (e.g., `moov->mvhd`, `trak->tkhd`, `stbl->stts`) never hold `NULL`. When absent, they point to immutable static sentinel instances allocated in `core/box_default.c` carrying `manager = LSMASH_NON_EXISTING_BOX`.
   - This eliminates null-pointer dereferences across deep Box hierarchies (`trak->mdia->minf->stbl->stsd->...` evaluates safely to a non-existing box sentinel if any intermediate box is absent).
2. **Dual-List Box Ownership & Lifetime Management**:
   - Every Box owns a value-embedded `lsmash_entry_list_t extensions` list (`core/box.h`).
   - When a child box is instantiated via `isom_add_*`, it is always registered in `parent->extensions` with `eliminator = isom_remove_extension_box`, which is the **sole owner** responsible for calling `box->destruct(box)` and `lsmash_free(box)`.
   - When a child box is also indexed in a typed parent list (e.g., `moov->trak_list`, `stsd->list`, `moof->traf_list`, `traf->trun_list`), that typed list uses `eliminator = isom_do_nothing`. Upon destruction (`isom_remove_box_by_itself`), the node in the typed list is unlinked via `parent + box->offset_in_parent` without freeing the box payload twice, while singleton child pointers are automatically restored to `box->nonexist_ptr`.
3. **Sample Tables, Fragmented Multiplexing, and Media Timelines**:
   - **Non-Fragmented Mode (`core/isom.c`)**: Accumulates per-sample metadata in `stbl` child boxes (`stts`, `ctts`, `stsz`, `stsc`, `stco`/`co64`, `stss`, `stps`, `sdtp`, `sgpd`, `sbgp`) and flushes media chunks to `mdat`.
   - **Fragmented Mode (`core/fragment.c`)**: Emits an initialization movie (`ftyp` + `moov` with `mvex`/`trex`) followed by self-contained or indexed Movie Fragments (`sidx` + `moof`/`mfhd`/`traf`/`tfhd`/`tfdt`/`trun` + `mdat`) and an optional Movie Fragment Random Access (`mfra`/`tfra`/`mfro`) trailer.
   - **Timeline Abstraction (`core/timeline.c`)**: Flattens non-fragmented `stbl` tables and fragmented `moof`/`traf`/`trun` sequences into an in-memory random-accessible timeline (`info_list` for general samples, `bunch_list` for run-length-compressed LPCM frame bunches, `chunk_list` for portable media chunks) to support remuxing, edit list (`elst`) reconstruction, and random access lookup.

---

## 3. Core Subsystems

| Pillar | Specification Document | Target Scope & Objective |
| :--- | :--- | :--- |
| **Subsystem: C99 Memory & Container Modernization** | [`docs/specs/memory_architecture.md`](docs/specs/memory_architecture.md) | Replace high-volume doubly-linked lists (`lsmash_entry_list_t` / `lsmash_entry_t` with 24-byte node overhead and double `malloc` per sample) in Sample Tables, Fragments, and Timelines with cache-friendly C99 Paged Block Arrays / Flat Vectors while strictly preserving `box_default` (Null-Object Sentinel Pattern), `extensions` single-ownership invariants, and pointer stability in `sbgp`/`chunk_list`. |
| **Subsystem: Native AV1 & Complete `obuparse` Removal (Completed in the implementation)** | [`docs/specs/av1_obuparse_removal.md`](docs/specs/av1_obuparse_removal.md) | Salvaged and completed the original author's native AV1 OBU parser from the official `av1` branch (`2bd228c`, `96065d0`), fixed all identified bitstream parsing bugs (`uvlc`, `leb128`, `show_existing_frame`, `order_hint`, `OrderHintBits`, `color_config`, `refresh_frame_flags`, `twelve_bit`, `lsmash_bs_get_le64`), and completely removed `obuparse` from `codecs/`, `importer/`, and `configure`. |
| **Subsystem: ISO/IEC 23003-5 LPCM in Fragmented ISOBMFF** | [`docs/specs/lpcm_isobmff_fragmented.md`](docs/specs/lpcm_isobmff_fragmented.md) | Implement ISO/IEC 23003-5 (`ipcm`, `fpcm`, `pcmC`) and ISO/IEC 14496-12 (`chnl`, `srat`, `AudioSampleEntryV1`) with full multi-track Fragmented ISOBMFF support (`moof`/`traf`/`trun`), remove the hardcoded `fullbox_type_table[62]` limit in `core/box.c`, fix the `AudioSampleEntryV1` writer bug in `core/write.c`, and fix multi-track `trun->optional` allocation bugs in `core/fragment.c`. |
| **Subsystem: Self-Contained Synthetic Test Suite (Foundation Established in the implementation)** | [`docs/specs/testing_strategy.md`](docs/specs/testing_strategy.md) | Established zero-external-media-dependency C99 unit and integration test runner (`tests/test_main.c`, `make test`) with programmatically synthesized AV1 OBU bitstreams, `av1C` boxes, and IVF-to-ISOBMFF round-trip verification; to be expanded with LPCM and Paged Block Array suites in subsequent phases. |

---

## 4. Directory & Module Index

### 4.1 Root & Configuration
| Path | Responsibility |
| :--- | :--- |
| `lsmash.h` | Public C API header: box types, codec FourCCs, file parameters, summary structs, muxing/demuxing/timeline APIs, and codec-specific configuration structures. |
| `configure` | POSIX shell build configurator: detects compiler/toolchain (supports POSIX/MSYS2 `sh`, `gcc`, `ar`, `ranlib`, `strip`), generates `config.h`, `config.mak`, `liblsmash.pc`, and `liblsmash.ver` (symbol version script parsed from `common/*.h` and `importer/*.h`). |
| `Makefile` | Build rules for static/shared `liblsmash`, CLI tools (`cli/*`), installation, and test targets (verified with GNU Make 4.4.1 & GCC 14.2.0 via Read-Only MSYS2 MinGW64 toolchain). |
| `SPEC.md` | Master architectural specification and documentation index (this file). |
| `docs/HISTORY.md` | Historical record of repository branches (`master`, `av1`, `vimeo`, `dev`), key architectural commits, and project revival milestones. |
| `docs/specs/*.md` | Detailed technical specifications for each of the four modernization missions. |

### 4.2 `common/` — Low-Level Primitives & Containers
| Path | Responsibility |
| :--- | :--- |
| `common/internal.h` | Internal umbrella header included first by all translation units (`config.h`, `lsmash.h`, `alloc.h`, `math.h`, `utils.h`, `osdep.h`, `bytes.h`, `bits.h`, `list.h`, `multichar.h`). |
| `common/alloc.h` | Memory allocation wrappers (`lsmash_malloc`, `lsmash_malloc_zero`, `lsmash_calloc`, `lsmash_realloc`, `lsmash_free`, `lsmash_freep`, `lsmash_memdup`). |
| `common/bytes.c`, `common/bytes.h` | Byte-stream I/O (`lsmash_bs_t`): buffered file/memory reading and writing, big/little-endian integer serialization, and byte counter tracking. |
| `common/bits.c`, `common/bits.h` | Bit-level reader/writer (`lsmash_bits_t`) built on `lsmash_bs_t` and in-memory bitbuffers (`lsmash_multiple_buffers_t`). |
| `common/list.c`, `common/list.h` | Doubly-linked list container (`lsmash_entry_list_t`, `lsmash_entry_t`) with 1-based indexing and last-accessed cursor caching; target of Subsystem container modernization. |
| `common/math.h` | GCD/LCM, rational reduction, min/max/clip macros, and integer overflow helpers. |
| `common/multichar.h` | FourCC and ISO language packing macros (`LSMASH_4CC`, `LSMASH_PACK_ISO_LANGUAGE`). |
| `common/osdep.c`, `common/osdep.h` | OS-specific large-file (`fseeko`/`ftello`) and Windows UTF-8/wide-char file/console wrappers. |
| `common/utils.c`, `common/utils.h` | Logging (`lsmash_log`), buffer bank management (`lsmash_multiple_buffers_t`), and general helpers. |

### 4.3 `core/` — ISOBMFF & QTFF Container Engine
| Path | Responsibility |
| :--- | :--- |
| `core/box.c`, `core/box.h` | In-memory Box definitions (`isom_*_t`), `ISOM_BASE_BOX` hierarchy, box creation (`isom_add_*`), destruction (`isom_remove_box_by_itself`), precedence ordering, and `isom_is_fullbox()` registry. |
| `core/box_default.c`, `core/box_default.h` | Static default sentinel instances (`isom_non_existing_*`) implementing the Null-Object Sentinel Pattern pattern (`LSMASH_NON_EXISTING_BOX`) and automatic pointer restoration on child destruction. |
| `core/box_type.c`, `core/box_type.h` | Box and Codec FourCC/UUID type constants (`ISOM_BOX_TYPE_*`, `QT_BOX_TYPE_*`, `ISOM_CODEC_TYPE_*`, `QT_CODEC_TYPE_*`) and type matching helpers. |
| `core/file.c`, `core/file.h` | File lifecycle, brand compatibility flags (`isom_compatible`, `qt_compatible`, `max_isom_version`, `avc_extensions`, etc.), and read/write mode management. |
| `core/isom.c`, `core/isom.h` | Track/movie manipulation, sample table population (`stts`, `ctts`, `stsz`, `stsc`, `stco`/`co64`, `sdtp`, `sgpd`/`sbgp` for `rap ` and `roll`), chunk caching/interleaving, and movie finalization. |
| `core/fragment.c`, `core/fragment.h` | Movie Fragment (`moof`/`traf`/`trun`) and Segment Index (`sidx`) creation, default sample flag optimization, `tfra`/`mfra` random access tables, and multi-track fragment pooling. |
| `core/read.c`, `core/read.h` | Binary Box parser (`isom_read_*`) deserializing ISOBMFF/QTFF streams into the in-memory Box hierarchy. |
| `core/write.c`, `core/write.h` | Binary Box serializer (`isom_write_*`) computing box sizes (`isom_update_box_size`) and writing boxes to `lsmash_bs_t`. |
| `core/timeline.c`, `core/timeline.h` | Timeline construction (`lsmash_construct_timeline`) from non-fragmented or fragmented movies, DTS/CTS lookup, LPCM bunch aggregation, and sample extraction. |
| `core/print.c`, `core/print.h` | Human-readable Box tree dumper (`lsmash_print_movie`, `isom_print_*`). |
| `core/summary.c`, `core/summary.h` | Media stream summary lifecycle (`lsmash_summary_t`, `lsmash_audio_summary_t`, `lsmash_video_summary_t`) and codec-specific opaque data list management. |

### 4.4 `codecs/` — Elementary Stream Parsers & Codec Descriptions
| Path | Responsibility |
| :--- | :--- |
| `codecs/description.c`, `codecs/description.h` | Codec sample entry (`stsd` child) setup, validation, and summary extraction for video, audio (including QTFF LPCM and target ISO/IEC 23003-5 LPCM), and hint tracks. |
| `codecs/av1.c`, `codecs/av1.h` | Native AV1 OBU bitstream parser (`av1_get_uvlc`, `av1_get_leb128`, `av1_parse_sequence_header`, `av1_parse_temporal_unit`), ITU-T T.35 HDR10+ metadata detection, sample assembly, and AV1 Codec Configuration Box (`av1C`) serialization/deserialization/printing. |
| `codecs/h264.c`, `codecs/h264.h` | H.264/AVC NAL unit parser,SPS/PPS/SEI parsing, and `avcC` configuration handling. |
| `codecs/hevc.c`, `codecs/hevc.h` | H.265/HEVC NAL unit parser, VPS/SPS/PPS/SEI parsing, and `hvcC` configuration handling. |
| `codecs/nalu.c`, `codecs/nalu.h` | Shared NAL unit start-code scanner, emulation prevention byte removal, and length-prefixed AU conversion. |
| `codecs/mp4sys.c`, `codecs/mp4sys.h` | MPEG-4 Systems (`esds` ObjectDescriptor, ES_Descriptor, DecoderConfigDescriptor, DecoderSpecificInfo) implementation. |
| `codecs/mp4a.c`, `codecs/mp4a.h` | MPEG-4 Audio (`AudioSpecificConfig`, SBR/PS signaling, profile/level indication) support. |
| `codecs/ac3.c`, `codecs/ac3.h` | AC-3 (`dac3`) and Enhanced AC-3 (`dec3`) bitstream parser and specific box support. |
| `codecs/dts.c`, `codecs/dts.h` | DTS (`ddts`) coherent acoustics and HD substream parser and specific box support. |
| `codecs/alac.c`, `codecs/opus.c`, `codecs/vc1.c`, `codecs/wma.c`, `codecs/a52.c`, `codecs/id3.c`, `codecs/qt_wfex.c` | Codec-specific box and header parsers for ALAC (`alac`), Opus (`dOps`), VC-1 (`dvc1`), WMA, A/52, ID3v2, and WAVEFORMATEX. |

### 4.5 `importer/` — Raw & Container Stream Importers
| Path | Responsibility |
| :--- | :--- |
| `importer/importer.c`, `importer/importer.h` | Importer plugin registry, probe/open/get_access_unit/close dispatch table, and public importer APIs. |
| `importer/isobm_imp.c` | ISOBMFF/QTFF container importer leveraging `core/timeline.c`. |
| `importer/wave_imp.c` | RIFF WAVE (`.wav`) LPCM audio importer (currently emits QTFF LPCM flags; scheduled for ISO/IEC 23003-5 & `WAVEFORMATEXTENSIBLE` enhancement in Subsystem). |
| `importer/ivf_imp.c` | IVF container importer for AV1 streams powered by L-SMASH's native AV1 OBU parser (`codecs/av1.{c,h}`). |
| `importer/nalu_imp.c` | Raw H.264/AVC and H.265/HEVC Annex-B byte stream importer. |
| `importer/adts_imp.c`, `importer/ac3_imp.c`, `importer/dts_imp.c`, `importer/amr_imp.c`, `importer/mp3_imp.c`, `importer/vc1_imp.c` | Elementary stream importers for ADTS AAC, AC-3/E-AC-3, DTS, AMR-NB/WB, MP3, and SMPTE VC-1. |

### 4.6 `cli/` — Command-Line Applications
| Path | Responsibility |
| :--- | :--- |
| `cli/cli.c`, `cli/cli.h` | Shared CLI initialization and argument/version helpers. |
| `cli/muxer.c` | Elementary stream to ISOBMFF/QTFF/3GPP multiplexer (supports `--fragment` mode). |
| `cli/remuxer.c` | Container-to-container remultiplexer and track/chapter editor. |
| `cli/boxdumper.c` | Container Box hierarchy and sample table inspection utility. |
| `cli/timelineeditor.c` | Edit list (`elst`) and timecode manipulation utility. |

### 4.7 `tests/` — Self-Contained Synthetic Test Suite
| Path | Responsibility |
| :--- | :--- |
| `tests/test_main.c` | Self-contained, copyright-free C99 synthetic unit and integration test suite (`make test` / `make check`). |
