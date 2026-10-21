# L-SMASH Master Architecture & Specification (`SPEC.md`)

## 1. Project Identity & Design Philosophy

**L-SMASH** (*Loyal to Spec of Mpeg4 and Ad-hoc Simple Hackwork*) is a C11 library (`liblsmash`, with public header `lsmash.h` maintaining strict C99 and C++ compatibility) and command-line toolset (`muxer`, `remuxer`, `boxdumper`, `timelineeditor`) for reading, writing, remuxing, and inspecting **ISO Base Media File Format (ISOBMFF, ISO/IEC 14496-12)** and **QuickTime File Format (QTFF)** media containers.

### 1.1 Strict Compliance with International Standards
L-SMASH is designed around strict compliance with the international standards governing each container and codec format:
- **Strict Separation of ISOBMFF and QTFF**: Even though ISOBMFF (`mp41`, `mp42`, `isom`, `iso2`–`iso6`) and QuickTime (`qt  `) share the same ancestral box/atom framing, their semantics, sample entry layouts, channel layout descriptors, and parameter boxes differ significantly. L-SMASH never conflates QTFF-only constructs with ISOBMFF constructs, and refuses to generate non-conformant, cross-polluted container structures.
- **No Unstandardized Extensions in Pure ISOBMFF**: Proprietary QuickTime Sound Description v1/v2 fields (`lpcm` flags, `chan` atoms, `wave` decompression atoms) are restricted to QuickTime compatibility modes (`file->qt_compatible`). Pure ISOBMFF files use standardized ISO/IEC 14496-12 and ISO/IEC 23003-5 constructs (`ipcm`, `fpcm`, `pcmC`, `chnl`, `srat`, `AudioSampleEntryV1`).
- **Zero External Runtime Dependencies**: All container and elementary stream parsers (including H.264/AVC, H.265/HEVC, AOM AV1 OBU, SMPTE VC-1, AAC/ADTS, Xiph FLAC, AC-3/E-AC-3, DTS, and WAVE/LPCM) are self-contained in standard C11 (`common/`, `core/`, `codecs/`, `importer/`).

---

## 2. Core Architectural Invariants (`core/` & `common/`)

### 2.1 The `box_default` Sentinel Box Architecture (`core/box_default.{c,h}`)
L-SMASH models the hierarchical ISOBMFF/QTFF Box tree using a **Null-Object Sentinel Pattern** (`box_default`):
1. Every concrete Box struct (`isom_moov_t`, `isom_trak_t`, `isom_mdia_t`, `isom_minf_t`, `isom_stbl_t`, `isom_stts_t`, etc.) begins with the common header macro `ISOM_BASEBOX_COMMON` or `ISOM_FULLBOX_COMMON`.
2. **Non-Null Singleton Child Pointers**: Singleton child box pointers on a parent struct (such as `file->moov`, `trak->tkhd`, `trak->mdia`, `mdia->minf`, `minf->stbl`, `stbl->stsd`, `stbl->stts`) **never hold `NULL`**.
3. When a child box does not exist, its pointer points to a static, immutable sentinel instance allocated in `core/box_default.c` whose `manager` field has the `LSMASH_NON_EXISTING_BOX` bit set (`0x02`).
4. Because every sentinel's own child pointers recursively point to the corresponding child sentinels, deep pointer chains such as:
   ```c
   if( LSMASH_IS_EXISTING_BOX( trak->mdia->minf->stbl->stsd ) )
   ```
   can be dereferenced safely without intermediate `NULL` checks.
5. **Lifecycle Rules**:
   - Always test box existence with `LSMASH_IS_EXISTING_BOX( box_ptr )` or `LSMASH_IS_NON_EXISTING_BOX( box_ptr )`—**never** `if( box_ptr != NULL )` on singleton box pointers.
   - When allocating a box via `lsm_box_add_<box>( parent )`, the parent's pointer is replaced with a heap-allocated struct. When destroying a box via `lsm_box_remove_by_itself( box_ptr )`, the parent's pointer is automatically restored to `lsm_box_default_non_existing_<box>()`.

### 2.2 Dual-List Box Ownership (`extensions` vs. Type-Specific Lists)
Every Box contains `lsmash_entry_list_t extensions` (part of `ISOM_BASEBOX_COMMON`):
- When a child box is created and attached to a parent, it is registered in `parent->extensions` (which governs serialization order in `write_children` / `print_children` and destruction in `lsm_box_remove_all_extension_boxes`).
- Multi-instance boxes (such as `trak_list`, `sgpd_list`, `sbgp_list`, `moof_list`, `traf_list`, `trun_list`) are **also** referenced by a type-specific `lsmash_entry_list_t` on the parent, while `extensions` owns the actual destructor (`lsm_box_remove_extension_box` / `lsm_box_remove_by_itself`).
- Removing a box via `lsm_box_remove_by_itself()` automatically unlinks it from both `parent->extensions` and any type-specific list, and resets the parent's singleton pointer to the `box_default` sentinel.

### 2.3 Bitstream & I/O Layer (`common/bytes.{c,h}`, `common/bits.{c,h}`, `common/multibuf.{c,h}`)
- `lsmash_bs_t` (`common/bytes.h`) provides buffered byte-stream reading/writing over either a `FILE *` stream or an in-memory buffer (`lsmash_bs_set_empty_stream()`), tracking `eob` (end of buffer), `eof` (end of file), `error`, and `written` byte counters.
- `lsmash_bits_t` (`common/bits.h`) wraps `lsmash_bs_t` to provide bit-level reading/writing (`lsmash_bits_get`, `lsmash_bits_put`, `lsmash_bits_get_align`, `lsmash_bits_put_align`).
- **Bitstream Ownership & Borrowing Contract (`lsmash_bits_t`)**:
  - `lsmash_bits_init( &bits, bs )` and `lsmash_bits_create( bs )` attach `bits` to an existing `lsmash_bs_t` as a **non-owning borrow**.
  - **Never** call `lsmash_bits_empty( &bits )` when `bits` borrows an active parent `lsmash_bs_t` (such as `file->bs` during Box reading or writing), because `lsmash_bits_empty()` calls `lsmash_bs_empty( bits->bs )`, wiping the parent stream's internal read/write buffer. Use `lsmash_bits_get_align( &bits )` on read and `lsmash_bits_put_align( &bits )` on write.
  - `lsmash_bits_empty()` is only valid on a dedicated scratch `lsmash_bs_t` owned exclusively by that bit reader (such as one created via `lsmash_bits_adhoc_create()` or a codec parser's private scratch buffer).
- `lsmash_multiple_buffers_t` (`common/multibuf.{c,h}`) manages a contiguous bank of `number_of_buffers` fixed-size buffers (`buffer_size` bytes each). When resizing via `lsmash_resize_multiple_buffers()`, buffers are re-strided cleanly: on expansion, buffers are moved in reverse index order (`number_of_buffers - 1` down to `1`) after `lsmash_realloc` and newly exposed tail bytes are zero-initialized; on shrinkage, buffers are compacted in forward index order (`1` up to `number_of_buffers - 1`) before `lsmash_realloc`.
- All integer serialization in `common/bytes.c` uses explicit `uint64_t` casts before bit shifts ≥ 32 bits (`lsmash_bs_get_be64`, `lsmash_bs_get_le64`) to prevent 32-bit signed integer promotion undefined behavior.

### 2.4 Coding Style, Header Policy & Symbol Naming Conventions
The official coding style, formatting standards, translation unit header rules, and Git contribution policies are defined in **[`docs/CODING_STYLE.md`](docs/CODING_STYLE.md)**. Key invariants:
- **Language Standard**: Standard C11 (`-std=c11 -pedantic`) for library internals, CLI tools, and tests; public header `lsmash.h` maintains strict C99 and C++ compatibility.
- **Header Order & Guard Policy**: Every `.c` starts with `#include "common/internal.h" /* must be placed first */`. Internal headers do not use `#ifndef ... #define ... #endif` include guards.
- **Formatting**: 4 spaces indentation (no tabs), Allman-style braces on separate lines, interior spaces inside parentheses `if( cond )`, and pointer asterisk on the variable name `isom_box_t *box`.
- **Symbol Visibility**:
  - Public API (`lsmash_*`): Declared in `lsmash.h` and exported via `liblsmash.ver`.
  - Internal Shared (`lsm_<module>_*`): Non-`static` functions shared across compilation units (`lsm_stbl_*`, `lsm_sample_*`, `lsm_track_*`, `lsm_movie_*`, `lsm_box_*`, etc.).
  - File-Local (`<module>_*`): `static` functions private to a single `.c` file (`stbl_*`, `sample_*`, `track_*`, etc.).
- **Specification Syntax Identifiers**: Exact spelling, casing, CamelCase, and underscores from international standards (ISO/IEC, QTFF, AV1) are strictly preserved (e.g. `pcmC` `format_flags`, `chnl` `stream_structure`, `btrt` `bufferSizeDB`) for bidirectional `grep` searchability against official specification texts.
- **Git Policy**: Strictly linear history (zero merge commits; no GitHub Web UI merges; author-preserving cherry-picks and fast-forwards only) and no hosting-specific issue/PR numbers in commit messages or code.

### 2.5 Compile-Time `static const` Dispatch Tables
All Box and Codec dispatch tables across `core/` and `codecs/` are defined as **compile-time `static const` arrays** sized automatically by the compiler (`sizeof(table) / sizeof(table[0])`), with zero runtime initialization state:
- `core/box.c`: `fullbox_type_table[]` in `lsm_box_init_common()`
- `core/read.c`: `box_reader_table[]` in `lsm_read_unknown_box()`
- `core/write.c`: `box_writer_table[]` in `lsm_write_set_box_writer()`
- `core/print.c`: `box_printer_table[]` in `lsm_print_set_box_printer()`
- `core/summary.c`: `codec_support_table[]` in `lsmash_check_codec_support()`
- `codecs/description.c`: `codec_Construct_table[]`, `codec_Copy_table[]`, `codec_Print_table[]`, and `codec_pecific_conversion_table[]`
- `codecs/dts.c`: `dts_extension_Substream_lbr_table[]`, `dts_channel_layout_table[]`, and `dts_audio_asset_descriptor_channel_table[]`

This guarantees thread safety across concurrent `lsmash_root_t` instances, places dispatch tables in read-only memory (`.rodata`), and eliminates fixed-size array bounds hazards.

---

## 3. Core Subsystems

Detailed specifications for each major subsystem reside in `docs/specs/` and are indexed in Section 4:

1. **Comprehensive Codecs & Stream Importers Architecture (`codecs/`, `importer/`, `docs/specs/codecs_and_importers.md`)**:
   - Self-contained C11 implementation of all video and audio codecs (H.264/AVC, H.265/HEVC, AOM AV1, SMPTE VC-1, MPEG-4 Visual/Audio, ProRes, QTFF uncompressed video, Xiph FLAC, IETF Opus, Dolby AC-3/E-AC-3, DTS/DTS-HD, Apple Lossless, ISO/IEC 23003-5 LPCM, 3GPP AMR, MP3, WMA) with zero external dependencies.
   - Decoupled `lsmash_summary_t` and `lsmash_codec_specific_t` structured/unstructured conversion model, compile-time `static const` dispatch tables, and uniform `importer_functions` streaming interface.
   - Comprehensive container binding specifications for ISOBMFF and QuickTime (QTFF), including NALU start-code conversion, complete AV1 `uncompressed_header()` and multi-tile tracking, ISO/IEC 23003-5 `ipcm`/`fpcm` with `pcmC`/`chnl`/`srat`, and Opus `'roll'` pre-roll sample grouping with Edit List mapping.
2. **ISO/IEC 23003-5 LPCM & ISO/IEC 14496-12 Audio Signaling (`core/`, `codecs/description.c`, `importer/wave_imp.c`)**:
   - Support for uncompressed integer (`ipcm`: 16/24/32-bit big/little-endian) and floating-point (`fpcm`: 32/64-bit IEEE 754) audio in standard and multi-track Fragmented ISOBMFF (`moof`/`traf`/`trun`), including `pcmC`, `chnl`, `srat`, and ISOBMFF `AudioSampleEntryV1`.
   - `trun` default sample duration/size compaction across multiple tracks in `core/fragment.c`, plus `WAVE_FORMAT_EXTENSIBLE` and `WAVE_FORMAT_IEEE_FLOAT` support in `importer/wave_imp.c`.
3. **Hybrid Container Architecture & Paged Block Arrays (`common/alloc.{c,h}`, `common/list.{c,h}`, `core/`)**:
   - Built-in C11 memory auditor (`-DLSMASH_DEBUG_ALLOC`) with `#ptr`/`#size` expression capture, 64-byte aligned headers, front/rear canaries, `0xCD`/`0xDD` memory poisoning, and leak verification.
   - Hybrid container model: `lsmash_entry_list_t` doubly-linked pointer lists for polymorphic Box trees (`extensions`, `trak_list`, `moof_list`) paired with **`lsm_list_t` C11 Paged Block Arrays** (`2^8 = 256` elements per page) for high-volume sample tables (`stts`, `ctts`, `stsz`, `stz2`, `stss`, `stps`, `sdtp`, `stsc`, `stco`/`co64`, `sgpd`, `sbgp`, `elst`), fragment tables (`trun->optional`, `tfra`, `sidx`, `fragment->pool`), and media timelines (`info_list`, `bunch_list`, `chunk_list`).
4. **Core Container Engine Module Architecture (`core/stbl.{c,h}`, `core/sample.{c,h}`, `core/track.{c,h}`, `core/movie.{c,h}`)**:
   - Section 6 below specifies the four-module architecture of the core container engine organized by ISOBMFF logical hierarchy (`stbl`, `sample`, `track`, `movie`), replacing the monolithic `core/isom.c` compilation unit, together with the unified symbol naming across `core/`, `codecs/`, and `importer/`.
5. **Xiph FLAC (`fLaC` / `dfLa`) & Box Reader Hardening (`codecs/flac.c`, `importer/flac_imp.c`, `core/read.c`)**:
   - Full RFC 9639 and FLAC-in-ISOBMFF encapsulation (`fLaC` Sample Entry and `dfLa` `FLACSpecificBox` containing `STREAMINFO` and metadata blocks), table-driven CRC-8 frame header verification, variable/fixed block size timestamp accumulation, and raw `.flac` stream reconstruction on demultiplexing.
   - Box reader hardening in `core/read.c` enforcing `ISOM_MAX_BOX_NESTING_LEVEL` (64 levels), child box boundary checks against remaining parent box bytes, and integer overflow guards.

---

## 4. Detailed Specification Index (`docs/specs/` & `docs/`)

| Document | Scope & Responsibility | Status |
| :--- | :--- | :--- |
| [`docs/CODING_STYLE.md`](docs/CODING_STYLE.md) | Official C11 coding style, Allman formatting, internal header policy, symbol naming, and Git contribution guidelines | Active (`v2.23.1`) |
| [`docs/HISTORY.md`](docs/HISTORY.md) | Historical lineage (`master`, `av1`, `vimeo`, `dev`), forensic bug analysis, and release changelog | Synchronized (`v2.23.2`) |
| [`docs/specs/memory_architecture.md`](docs/specs/memory_architecture.md) | C11 memory audit (`LSMASH_DEBUG_ALLOC`), `lsmash_multiple_buffers_t` resize invariants, and `lsm_list_t` Paged Block Array architecture | Synchronized (`v2.23.2`) |
| [`docs/specs/codecs_and_importers.md`](docs/specs/codecs_and_importers.md) | Comprehensive architecture and container binding specification for all video and audio codecs and importers (`codecs/*`, `importer/*`) | Synchronized (`v2.23.2`) |
| [`docs/specs/testing_strategy.md`](docs/specs/testing_strategy.md) | Self-contained synthetic test suite (`make test`), bitstream generators, malformed box negative tests, and regression suites | Synchronized (`v2.23.2`) |

---

## 5. Build, Toolchain & Verification

- **Toolchain**: POSIX `sh` + GNU `make` + C11 compiler (`gcc` or `clang`). On Windows, verified with the MSYS2 MinGW64 toolchain (`x86_64-w64-mingw32-gcc`).
- **Build & Test Command**:
  ```sh
  ./configure
  make -j8 all test
  ```
- **Quality Requirements**:
  1. Zero compiler warnings under `-Wall -Wshadow -std=c11 -pedantic` across `liblsmash.a`, CLI tools, and `tests/test_main.c`.
  2. Public header `lsmash.h` strictly passes standalone verification under `gcc -std=c99 -pedantic` and `g++ -std=c++11 -pedantic`.
  3. All 16 synthetic test suites in `make test` must pass with **0 leaks** and **0 active blocks/bytes** reported by `lsm_alloc_verify_no_leaks()`.

---

## 6. Core Container Engine Architecture (`core/stbl.{c,h}`, `core/sample.{c,h}`, `core/track.{c,h}`, `core/movie.{c,h}`)

The core ISOBMFF/QTFF container manipulation engine in `core/` is decomposed along the container's logical hierarchy into four focused modules (plus `core/chapter.c` for chapter list operations), completely replacing the legacy monolithic `core/isom.c`:
1. **`core/stbl.{c,h}` — Sample Table (`stbl`) & Sample Grouping (`sgpd`/`sbgp`) Engine**
2. **`core/sample.{c,h}` — Sample Lifecycle, Pooling & Chunk Interleaving Engine**
3. **`core/track.{c,h}` — Track (`trak`/`tkhd`), Media (`mdia`/`mdhd`/`hdlr`), Data Reference (`dref`) & Edit List (`elst`) Management**
4. **`core/movie.{c,h}` — Movie (`moov`/`mvhd`/`iods`), Progressive Download Rearrangement & User Data (`cprt`/`hnti`)**

### 6.1 4-Module Hierarchy (`core/`)

```
                  +---------------------------------------+
                  |             core/box.h                |
                  |  (Box structs, sentinels, prototypes) |
                  +-------------------+-------------------+
                                      |
        +--------------------+--------+--------+--------------------+
        |                    |                 |                    |
        v                    v                 v                    v
+---------------+    +---------------+ +---------------+    +---------------+
|  core/stbl.c  |    | core/sample.c | | core/track.c  |    | core/movie.c  |
|  Sample Table |    | Sample Alloc, | | Track, Media, |    | Movie, mvhd,  |
|  stts/ctts/   |    | Sample Pool,  | | Handler, tref,|    | iods, Finish, |
|  stsz/stz2/   |    | Chunk Flush,  | | dref, edts/   |    | Progressive   |
|  stsc/stco/   |    | LPCM/RTP      | | elst, Track   |    | Download,     |
|  sgpd/sbgp &  |    | Interleaving  | | Duration      |    | User Data     |
|  Bitrates     |    |               | |               |    | (cprt/hnti)   |
+---------------+    +---------------+ +---------------+    +---------------+
```

### 6.2 Module Responsibilities & Symbol Mapping

In accordance with the symbol naming conventions (Section 2.4):
- Public API functions declared in `lsmash.h` preserve their `lsmash_*` names.
- Internal functions shared across compilation units in `core/`, `codecs/`, or `importer/` use the **`lsm_<filename>_*`** prefix (`lsm_stbl_*`, `lsm_sample_*`, `lsm_track_*`, `lsm_movie_*`) and are declared in `core/<filename>.h`.
- File-local helpers are `static` with **`<filename>_*`** (`stbl_*`, `sample_*`, `track_*`, `movie_*`) names.

#### Module 1: `core/stbl.{c,h}` — Sample Table & Sample Grouping Engine
Owns Sample Table (`stts`, `ctts`, `cslg`, `stsz`/`stz2`, `stsc`, `stco`/`co64`, `stss`, `stps`, `sdtp`) entry manipulation, `stco` → `co64` 64-bit promotion, `stsz` → `stz2` compaction, bitrate calculation (`btrt`/`esds`), and Sample Grouping (`sgpd`/`sbgp`: `rap `, `'roll'` [negative `roll_distance` pre-roll / positive post-roll], `'prol'` [audio pre-roll for non-sync streams]) state machines and roll-group deduplication:

| Symbol | Scope | Signature / Role |
| :--- | :--- | :--- |
| `lsm_stbl_add_stts_entry` / `lsm_stbl_add_stsc_entry` / `lsm_stbl_add_stco_entry` | Internal Shared (`core/stbl.h`) | Append entries to `stts`, `stsc`, and `stco`/`co64` tables |
| `lsm_stbl_convert_stco_to_co64` | Internal Shared (`core/stbl.h`) | Promote 32-bit `stco` chunk offsets to 64-bit `co64` (`core/stbl.c`, `core/movie.c`, `core/fragment.c`) |
| `lsm_stbl_compress_sample_size_table` | Internal Shared (`core/stbl.h`) | Compact `stsz` into 1/2/4/8/16-bit `stz2` when beneficial (`core/movie.c`, `core/fragment.c`) |
| `lsm_stbl_get_sample_count` / `lsm_stbl_get_dts` / `lsm_stbl_replace_last_sample_delta` | Internal Shared (`core/stbl.h`) | Query total samples, compute cumulative DTS, and update the final sample delta |
| `lsm_stbl_calculate_bitrate_description` / `lsm_stbl_is_variable_size` / `lsm_stbl_get_first_sample_size` / `lsm_stbl_update_bitrate_description` | Internal Shared (`core/stbl.h`) | Compute `bufferSizeDB`, `maxBitrate`, and `avgBitrate` across sample tables and movie fragments (`core/`, `codecs/`) |
| `lsm_stbl_get_sample_group_description` / `lsm_stbl_get_sample_to_group` / `lsm_stbl_get_roll_recovery_*` / `lsm_stbl_get_fragment_*` | Internal Shared (`core/stbl.h`) | Retrieve `sgpd` and `sbgp` boxes for standard tracks and track fragments |
| `lsm_stbl_add_sample_grouping` / `lsm_stbl_create_sample_grouping` / `lsm_stbl_add_dependency_type` | Internal Shared (`core/stbl.h`) | Initialize `sgpd`/`sbgp` pairings and append `sdtp` entries |
| `lsm_stbl_update_cache_timestamp` / `lsm_stbl_rap_grouping_established` / `lsm_stbl_group_random_access` / `lsm_stbl_all_recovery_completed` / `lsm_stbl_group_roll_recovery` | Internal Shared (`core/stbl.h`) | Random access (`rap `) and roll recovery (`roll` pre/post-roll, `prol` non-sync audio pre-roll) sample grouping state machines |
| `lsm_stbl_update_chunk_tables` / `lsm_stbl_update_sample_tables` | Internal Shared (`core/stbl.h`) | Commit pooled chunks and per-sample timing/size/sync/grouping entries into `stbl` |
| `static stbl_*` | File-Local (`core/stbl.c`) | `stbl_add_ctts_entry`, `stbl_add_stsz_entry`, `stbl_add_stss_entry`, `stbl_add_stps_entry`, `stbl_deduplicate_roll_group`, `stbl_update_roll_group`, `stbl_update_rap_group`, `stbl_create_bitrate_Au_info`, etc. |

#### Module 2: `core/sample.{c,h}` — Sample Lifecycle, Pooling & Chunk Interleaving Engine
Owns `lsmash_sample_t` allocation/destruction, `isom_sample_pool_t` management, chunk interleaving across tracks, LPCM frame splitting, RTP hint sample aggregation, and `mdat` flushing:

| Symbol | Scope | Signature / Role |
| :--- | :--- | :--- |
| `lsmash_create_sample` / `lsmash_sample_alloc` / `lsmash_delete_sample` | Public API (`lsmash.h`) | Public sample allocation, buffer sizing, and deallocation |
| `lsmash_append_sample` / `lsmash_flush_pooled_samples` / `lsmash_set_last_sample_delta` | Public API (`lsmash.h`) | Public sample multiplexing, pool flushing, and final duration assignment |
| `lsm_sample_create_pool` / `lsm_sample_clear_pool` / `lsm_sample_remove_pool` | Internal Shared (`core/sample.h`) | Lifecycle management for `isom_sample_pool_t` (`core/box.c`, `core/track.c`, `core/fragment.c`) |
| `lsm_sample_update_tables` | Internal Shared (`core/sample.h`) | Update sample tables and chunk state for a new sample (`core/sample.c`, `core/fragment.c`) |
| `lsm_sample_pool_push` | Internal Shared (`core/sample.h`) | Append sample payload bytes into a track or fragment sample pool |
| `lsm_sample_append_by_type` | Internal Shared (`core/sample.h`) | Dispatch media vs. hint vs. LPCM frame-sliced sample appending (`core/sample.c`, `core/fragment.c`) |
| `static sample_*` | File-Local (`core/sample.c`) | `sample_check_chunk_Alive`, `sample_flush_pool`, `sample_append_raw_lpcm`, `sample_append_media`, `sample_append_hint`, `sample_append`, `sample_set_last_delta` |

#### Module 3: `core/track.{c,h}` — Track, Media, Data Reference & Edit List Management
Owns `trak`, `tkhd`, `tapt`, `trex`, `traf`, `tfra`, `tref`, `mdia`, `mdhd`, `hdlr`, `minf`, `dref`, `stsd` sample entry registration (`lsmash_add_sample_entry`), and `edts`/`elst` explicit timeline maps:

| Symbol | Scope | Signature / Role |
| :--- | :--- | :--- |
| `lsmash_create_track` / `lsmash_delete_track` / `lsmash_get_track_ID` / `lsmash_get_track_parameters` / `lsmash_set_track_parameters` / `lsmash_update_track_duration` | Public API (`lsmash.h`) | Public track lifecycle, parameter configuration, and duration calculation |
| `lsmash_get_media_parameters` / `lsmash_set_media_parameters` / `lsmash_pack_iso_language` / `lsmash_add_sample_entry` | Public API (`lsmash.h`) | Public media handler, ISO-639-2/T language packing, and codec sample description registration |
| `lsmash_create_data_reference` / `lsmash_get_data_reference` / `lsmash_count_data_reference` | Public API (`lsmash.h`) | Public `dref` (`url `) data reference management |
| `lsmash_create_explicit_timeline_map` / `lsmash_modify_explicit_timeline_map` / `lsmash_get_explicit_timeline_map` / `lsmash_delete_explicit_timeline_map` / `lsmash_count_explicit_timeline_map` | Public API (`lsmash.h`) | Public `edts`/`elst` edit list manipulation |
| `lsm_track_get_trak` / `lsm_track_get_trex` / `lsm_track_get_traf` / `lsm_track_get_tfra` | Internal Shared (`core/track.h`) | Track-ID lookup across `moov`, `mvex`, `moof`, and `mfra` |
| `lsm_track_create` / `lsm_track_update_tkhd_duration` / `lsm_track_set_creation_time` / `lsm_track_setup_handler_reference` | Internal Shared (`core/track.h`) | Internal track construction, `tkhd` duration scaling, and `hdlr` initialization |
| `lsm_track_complement_data_reference` / `lsm_track_get_written_media_file` | Internal Shared (`core/track.h`) | Default `dref` completion and target media file resolution |
| `static track_*` | File-Local (`core/track.c`) | `track_set_aperature_modes`, `track_prepare_sample_Groups`, `track_add_sample_description_entry`, `track_get_edts` |

#### Module 4: `core/movie.{c,h}` — Movie Context, Progressive Download & User Data
Owns `moov`, `mvhd`, `iods`, movie parameter management, `lsmash_finish_movie`, progressive download (`moov`-before-`mdat` rearrangement and chunk offset shifting), and movie/track user data (`cprt`, `hnti`/`sdp `/`rtp `):

| Symbol | Scope | Signature / Role |
| :--- | :--- | :--- |
| `lsmash_initialize_movie_parameters` / `lsmash_set_movie_parameters` / `lsmash_get_movie_parameters` | Public API (`lsmash.h`) | Public movie header (`mvhd`) configuration and inspection |
| `lsmash_finish_movie` / `lsmash_reserve_media_data_size` / `lsmash_create_object_descriptor` | Public API (`lsmash.h`) | Public movie finalization, progressive download remuxing, `mdat` reservation, and `iods` creation |
| `lsmash_set_copyright` / `lsmash_set_sdp` | Public API (`lsmash.h`) | Public `udta` copyright (`cprt`) and RTP Session Description Protocol (`hnti`/`sdp `) APIs |
| `lsm_movie_check_initializer_present` / `lsm_movie_create` / `lsm_movie_establish` | Internal Shared (`core/movie.h`) | Initializer validation, `moov` box creation, and post-read movie establishment |
| `lsm_movie_update_mvhd_duration` / `lsm_movie_setup_iods` / `lsm_movie_check_large_offset_requirement` / `lsm_movie_add_preceding_box_size` | Internal Shared (`core/movie.h`) | Movie duration aggregation, `iods` synthesis, and `stco`/`co64` offset adjustment |
| `static movie_*` | File-Local (`core/movie.c`) | `movie_update_chunk_offset`, `movie_validate_mandatory_boxes`, `movie_rearrange_for_pd` |

*(Note: `lsmash_delete_tyrant_chapter()` resides in `core/chapter.c` alongside all other chapter manipulation APIs.)*

### 6.3 Core Infrastructure Modules Symbol Mapping (`core/`)
All remaining `core/` compilation units likewise follow the `lsmash_*` (Public API), `lsm_<filename>_*` (Internal Shared), and `static <filename>_*` (File-Local) naming hierarchy:

| Module | Internal Shared (`lsm_<filename>_*`) | File-Local (`static <filename>_*`) |
| :--- | :--- | :--- |
| `core/box.{c,h}` | `lsm_box_init_common`, `lsm_box_is_fullbox`, `lsm_box_add_*`, `lsm_box_remove_by_itself`, `lsm_box_remove_extension_box`, `lsm_box_remove_all_extension_boxes`, `lsm_box_remove_sample_description`, `lsm_box_remove_unknown_box`, `lsm_box_update_size`, `lsm_box_add_extension_binary`, `lsm_box_get_extension_box`, `lsm_box_get_extension_box_format`, `lsm_box_bs_put_*`, `lsm_box_4cc2str`, `lsm_box_make_cts`, `lsm_box_check_media_hdlr_from_stsd`, `lsm_box_get_media_type_from_stsd` | `box_reorder_tail`, `box_add_to_extension_list`, `box_get_entry`, `box_remove_predefined`, `box_remove_in_predefined_list`, `box_remove_*`, `box_add_sample_description_entry`, `box_fake_file_read`, `box_fake_file_seek` |
| `core/box_default.{c,h}` | `lsm_box_default_allocate`, `lsm_box_default_non_existing_*` | — |
| `core/file.{c,h}` | `lsm_file_check_compat_flags`, `lsm_file_check_brand_version`, `lsm_file_set_brands` | `file_clear_compat_flags`, `file_check_mandatory_boxes`, `file_close`, `file_set_brands` |
| `core/fragment.{c,h}` | `lsm_fragment_create_empty_duration`, `lsm_fragment_update_track_duration`, `lsm_fragment_flush_pooled_samples`, `lsm_fragment_append_sample`, `lsm_fragment_finish_movie`, `lsm_fragment_set_last_sample_delta` | `fragment_write_mfra`, `fragment_update_sidx`, `fragment_flush`, `fragment_prepare_chunk_run`, `fragment_output_sample_flags`, `fragment_input_sample_flags`, `fragment_create_track_run`, `fragment_append_track_run`, `fragment_update_cache`, etc. |
| `core/timeline.{c,h}` | `lsm_timeline_remove_all`, `lsm_timeline_destroy_track`, `lsm_timeline_get_dts`, `lsm_timeline_get_cts`, `lsm_timeline_get_sample_duration`, `lsm_timeline_check_sample_existence`, `lsm_timeline_get_sample`, `lsm_timeline_get_sample_info`, `lsm_timeline_get_sample_property`, `lsm_timeline_get_explicit_timeline_map`, `lsm_timeline_count_explicit_timeline_map` | `timeline_get_track`, `timeline_add_dref_entry`, `timeline_add_sample_description`, `timeline_get_portable_chunk`, `timeline_add_sample_info`, `timeline_construct`, etc. |
| `core/read.{c,h}` | `lsm_read_box`, `lsm_read_unknown_box`, `lsm_read_children`, `lsm_read_leaf_box_common_last_process`, `lsm_read_check_box_size`, `lsm_read_visual_description`, `lsm_read_audio_description` | `read_file`, `read_unknown_box`, `read_children`, `read_ftyp`, `read_moov`, `read_trak`, `read_stbl`, `read_pcmC`, `read_chnl`, `read_srat`, `read_emsg`, etc. |
| `core/write.{c,h}` | `lsm_write_box`, `lsm_write_extension_boxes`, `lsm_write_set_box_writer`, `lsm_write_moov`, `lsm_write_mdat_header`, `lsm_write_mdat_size`, `lsm_write_ftyp`, `lsm_write_meta`, `lsm_write_free` | `write_children`, `write_unknown_box`, `write_ftyp`, `write_moov`, `write_trak`, `write_stbl`, `write_pcmC`, `write_chnl`, `write_srat`, `write_emsg`, etc. |
| `core/print.{c,h}` | `lsm_print_indent`, `lsm_print_box_common`, `lsm_print_unknown`, `lsm_print_destroy_list`, `lsm_print_set_box_printer` | `print_iprintf`, `print_ifprintf`, `print_basebox_common`, `print_fullbox_common`, `print_box_common`, `print_children`, `print_ftyp`, `print_moov`, `print_trak`, `print_pcmC`, `print_chnl`, `print_srat`, `print_qt_color_table`, `print_sample_description_extension`, etc. |
| `core/summary.{c,h}` | *(Public `lsmash_*` APIs only)* | `summary_allocate_opaque`, `summary_deallocate_opaque` |
| `core/meta.{c,h}` | *(Public `lsmash_*` APIs only)* | `meta_add_metadata`, `meta_set_itunes_metadata_string`, `meta_set_itunes_metadata_integer`, `meta_set_itunes_metadata_boolean`, `meta_set_itunes_metadata_binary`, `meta_get_itunes_metadata_type` |
| `core/chapter.{c,h}` | *(Public `lsmash_*` APIs only)* | `chapter_get_start_time`, `chapter_lumber_line`, `chapter_read_simple_chapter`, `chapter_read_minimum_chapter`, `chapter_check_chap_line`, `chapter_add_chpl_entry` |

### 6.4 Codecs & Importer Modules Symbol Mapping (`codecs/` & `importer/`)
All modules in `codecs/` and `importer/` enforce the identical symbol naming hierarchy:

| Module | Internal Shared (`lsm_<filename>_*`) | File-Local (`static <filename>_*`) |
| :--- | :--- | :--- |
| `codecs/description.{c,h}` | `lsm_description_setup_from_summary`, `lsm_description_get_codec_specific`, `lsm_description_copy_opaque_extension`, `lsm_description_print_codec_specific`, `lsm_description_append_Binary` | `description_initialize_*`, `description_setup_*`, `description_get_*` |
| `codecs/av1.{c,h}` | `lsm_av1_get_uvlc`, `lsm_av1_get_leb128`, `lsm_av1_show_leb128`, `lsm_av1_get_su`, `lsm_av1_get_ns`, `lsm_av1_parse_sequence_header`, `lsm_av1_parse_temporal_unit`, `lsm_av1_construct_specific_parameters`, `lsm_av1_copy_codec_specific`, `lsm_av1_print_codec_specific`, `lsm_av1_update_bitrate` | `av1_parse_*`, `av1_load_reference_frame_info`, `av1_update_reference_list`, etc. |
| `codecs/h264.{c,h}` / `codecs/hevc.{c,h}` / `codecs/nalu.{c,h}` | `lsm_h264_*`, `lsm_hevc_*`, `lsm_nalu_*` | `h264_*`, `hevc_*`, `nalu_*` |
| `codecs/flac.{c,h}` | `lsm_flac_verify_crc8`, `lsm_flac_parse_streaminfo`, `lsm_flac_parse_frame_header`, `lsm_flac_construct_specific_parameters`, `lsm_flac_copy_codec_specific`, `lsm_flac_print_codec_specific` | `flac_read_utf8_uint64`, `flac_put_specific_Metadata_blocks`, `flac_print_*` |
| `codecs/a52.{c,h}` / `codecs/dts.{c,h}` / `codecs/alac.{c,h}` / `codecs/opus.{c,h}` / `codecs/mp4a.{c,h}` / `codecs/mp4v.{c,h}` / `codecs/mp4sys.{c,h}` / `codecs/vc1.{c,h}` / `codecs/wma.{c,h}` / `codecs/qt_wfex.{c,h}` | `lsm_a52_*`, `lsm_dts_*`, `lsm_alac_*`, `lsm_opus_*`, `lsm_mp4a_*`, `lsm_mp4v_*`, `lsm_mp4sys_*`, `lsm_vc1_*`, `lsm_wma_*`, `lsm_qt_wfex_*` | `a52_*`, `dts_*`, `alac_*`, `opus_*`, `mp4a_*`, `mp4v_*`, `mp4sys_*`, `vc1_*`, `wma_*`, `qt_wfex_*` |
| `importer/importer.{c,h}` & `importer/*_imp.c` | *(Public `lsmash_importer_*` exported in `liblsmash.ver` for CLI tools)* | `importer_find`, `a52_imp_*`, `adts_imp_*`, `als_imp_*`, `amr_imp_*`, `dts_imp_*`, `flac_imp_*`, `isobm_imp_*`, `ivf_imp_*`, `mp3_imp_*`, `nalu_imp_*`, `vc1_imp_*`, `wave_imp_*` |

---

## 7. Future Roadmap / Pending Tasks

1. **Read support for separate Media Segments.** A Media Segment (`.m4s` × N) that has no `moov` is not yet linked to its Initialization Segment (`init.mp4`) at read time. The read path must associate each segment with the initializer (`file->initializer` and `lsmash_activate_file()`) so that a Media Timeline spanning several segment files can be built and demuxed directly, without concatenating the files first. Writing such segments is already supported (`lsmash_switch_media_segment()`).
2. **Data Reference (`dref`) external media files.** Resolving and reading media data that a `dref` entry places in an external file is not complete and remains to be finished.