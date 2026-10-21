# L-SMASH Master Architecture & Specification (`SPEC.md`)

## 1. Project Identity & Design Philosophy

**L-SMASH** (*Loyal to Spec of Mpeg4 and Ad-hoc Simple Hackwork*) is a pure C99 library (`liblsmash`) and command-line toolset (`muxer`, `remuxer`, `boxdumper`, `timelineeditor`) for reading, writing, remuxing, and inspecting **ISO Base Media File Format (ISOBMFF, ISO/IEC 14496-12)** and **QuickTime File Format (QTFF)** media containers.

### 1.1 Strict Compliance with International Standards
L-SMASH is designed around strict compliance with the international standards governing each container and codec format:
- **Strict Separation of ISOBMFF and QTFF**: Even though ISOBMFF (`mp41`, `mp42`, `isom`, `iso2`–`iso6`) and QuickTime (`qt  `) share the same ancestral box/atom framing, their semantics, sample entry layouts, channel layout descriptors, and parameter boxes differ significantly. L-SMASH never conflates QTFF-only constructs with ISOBMFF constructs, and refuses to generate non-conformant, cross-polluted container structures.
- **No Unstandardized Extensions in Pure ISOBMFF**: Proprietary QuickTime Sound Description v1/v2 fields (`lpcm` flags, `chan` atoms, `wave` decompression atoms) are restricted to QuickTime compatibility modes (`file->qt_compatible`). Pure ISOBMFF files use standardized ISO/IEC 14496-12 and ISO/IEC 23003-5 constructs (`ipcm`, `fpcm`, `pcmC`, `chnl`, `srat`, `AudioSampleEntryV1`).
- **Zero External Runtime Dependencies**: All container and elementary stream parsers (including H.264/AVC, H.265/HEVC, AOM AV1 OBU, SMPTE VC-1, AAC/ADTS, AC-3/E-AC-3, DTS, and WAVE/LPCM) are self-contained in standard C99 (`common/`, `core/`, `codecs/`, `importer/`).

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
   - When allocating a box via `isom_add_<box>( parent )`, the parent's pointer is replaced with a heap-allocated struct. When destroying a box via `isom_remove_box_by_itself( box_ptr )`, the parent's pointer is automatically restored to `isom_non_existing_<box>()`.

### 2.2 Dual-List Box Ownership (`extensions` vs. Type-Specific Lists)
Every Box contains `lsmash_entry_list_t extensions` (part of `ISOM_BASEBOX_COMMON`):
- When a child box is created and attached to a parent, it is registered in `parent->extensions` (which governs serialization order in `isom_write_children` / `isom_print_children` and destruction in `isom_remove_all_extension_boxes`).
- Multi-instance boxes (such as `trak_list`, `sgpd_list`, `sbgp_list`, `moof_list`, `traf_list`, `trun_list`) are **also** referenced by a type-specific `lsmash_entry_list_t` on the parent whose `eliminator` is `isom_remove_dangling_box` (a no-op eliminator when the box is already owned by `extensions`), while `extensions` owns the actual destructor (`isom_remove_box_by_itself`).
- Removing a box via `isom_remove_box_by_itself()` automatically unlinks it from both `parent->extensions` and any type-specific list, and resets the parent's singleton pointer to the `box_default` sentinel.

### 2.3 Bitstream & I/O Layer (`common/bytes.{c,h}`, `common/bits.{c,h}`, `common/multibuf.{c,h}`)
- `lsmash_bs_t` (`common/bytes.h`) provides buffered byte-stream reading/writing over either a `FILE *` stream or an in-memory buffer (`lsmash_bs_set_empty_stream()`), tracking `eob` (end of buffer), `eof` (end of file), `error`, and `written` byte counters.
- `lsmash_bits_t` (`common/bits.h`) wraps `lsmash_bs_t` to provide bit-level reading/writing (`lsmash_bits_get`, `lsmash_bits_put`, `lsmash_bits_get_align`, `lsmash_bits_put_align`).
- **Bitstream Ownership & Borrowing Contract (`lsmash_bits_t`)**:
  - `lsmash_bits_init( &bits, bs )` and `lsmash_bits_create( bs )` attach `bits` to an existing `lsmash_bs_t` as a **non-owning borrow**.
  - **Never** call `lsmash_bits_empty( &bits )` when `bits` borrows an active parent `lsmash_bs_t` (such as `file->bs` during Box reading or writing), because `lsmash_bits_empty()` calls `lsmash_bs_empty( bits->bs )`, wiping the parent stream's internal read/write buffer. Use `lsmash_bits_get_align( &bits )` on read and `lsmash_bits_put_align( &bits )` on write.
  - `lsmash_bits_empty()` is only valid on a dedicated scratch `lsmash_bs_t` owned exclusively by that bit reader (such as one created via `lsmash_bits_adhoc_create()` or a codec parser's private scratch buffer).
- `lsmash_multiple_buffers_t` (`common/multibuf.{c,h}`) manages a contiguous bank of `number_of_buffers` fixed-size buffers (`buffer_size` bytes each). When resizing via `lsmash_resize_multiple_buffers()`, buffers are re-strided cleanly: on expansion, buffers are moved in reverse index order (`number_of_buffers - 1` down to `1`) after `lsmash_realloc` and newly exposed tail bytes are zero-initialized; on shrinkage, buffers are compacted in forward index order (`1` up to `number_of_buffers - 1`) before `lsmash_realloc`.
- All integer serialization in `common/bytes.c` uses explicit `uint64_t` casts before bit shifts $\ge 32$ bits (`lsmash_bs_get_be64`, `lsmash_bs_get_le64`) to prevent 32-bit signed integer promotion undefined behavior.

### 2.4 Symbol Naming Conventions & Specification Syntax Identifiers

#### Symbol Naming Conventions
To maintain clear module boundaries without breaking external callers of `<lsmash.h>`, symbols follow a structured naming convention across three visibility scopes:
- **Public API (`lsmash_*`)**: Exported library functions, types, and macros declared in `lsmash.h` and exposed via `liblsmash.ver`. Existing public symbol names are strictly preserved for ABI/API compatibility.
- **Internal Shared Functions (`lsm_<module>_*`)**: Non-`static` functions shared across compilation units inside `common/`, `core/`, `codecs/`, and `importer/` (for example, `lsm_alloc_get_active_count()`, `lsm_list_init()`, `lsm_isom_get_trak()`).
- **File-Local Static Functions (`<module>_*`)**: `static` helper functions local to a single `.c` file (for example, `alloc_verify_block()`, `av1_parse_tile_info()`, `isom_write_pcmC()`).

#### Preservation of Specification Syntax Identifiers
When a struct field, constant, or helper represents a syntax element defined by an international specification (ISO/IEC 14496-12, ISO/IEC 23003-5,ISO/IEC 23091-3, or AOM AV1), **the exact identifier spelling and casing from the official specification are preserved** so that the C source code can be cross-referenced directly against the standard text:
- **ISO/IEC 23003-5 (`pcmC`)**: `format_flags`, `PCM_sample_size`
- **ISO/IEC 14496-12 (`chnl`)**: `stream_structure`, `definedLayout`, `speaker_position`, `azimuth`, `elevation`, `omittedChannelsMap`, `object_count`
- **ISO/IEC 14496-12 (`srat`)**: `sampling_rate`
- **AOM AV1 Specification**: `seq_profile`, `seq_level_idx`, `seq_tier`, `high_bitdepth`, `twelve_bit`, `mono_chrome`, `color_description_present_flag`, `subsampling_x`, `subsampling_y`, `chroma_sample_position`, `SeenFrameHeader`, `show_existing_frame`, `frame_to_show_map_idx`, `refresh_frame_flags`, `OrderHintBits`, `TileColsLog2`, `TileRowsLog2`, `NumTiles`, `TileNum`, `CodedLossless`, `AllLossless`, etc.

### 2.5 Compile-Time `static const` Dispatch Tables
All Box and Codec dispatch tables across `core/` and `codecs/` are defined as **compile-time `static const` arrays** sized automatically by the compiler (`sizeof(table) / sizeof(table[0])`), with zero runtime initialization state:
- `core/box.c`: `fullbox_type_table[]` in `isom_init_box_common()`
- `core/read.c`: `box_reader_table[]` in `isom_read_unknown_box()`
- `core/write.c`: `box_writer_table[]` in `isom_set_box_writer()`
- `core/print.c`: `box_printer_table[]` in `isom_Ip_set_box_printer()`
- `core/summary.c`: `summary_setup_table[]` in `lsmash_get_summary()`
- `codecs/description.c`: `codec_Construct_table[]`, `codec_Copy_table[]`, `codec_Print_table[]`, and `codec_pecific_conversion_table[]`
- `codecs/dts.c`: `dts_extension_Substream_lbr_table[]`, `dts_channel_layout_table[]`, and `dts_audio_asset_descriptor_channel_table[]`

This guarantees thread safety across concurrent `lsmash_root_t` instances, places dispatch tables in read-only memory (`.rodata`), and eliminates fixed-size array bounds hazards.

---

## 3. Core Subsystems

Detailed specifications for each major subsystem reside in `docs/specs/` and are indexed in Section 4:

1. **Built-In AOM AV1 OBU Parser (`codecs/av1.{c,h}`, `importer/ivf_imp.c`)**:
   - Self-contained C99 implementation of the AV1 Bitstream & Decoding Process Specification (v1.0.0 with Errata 1) and AV1 Codec ISO Media File Format Binding (v1.2.0), with no external `obuparse` dependency.
   - Full `uncompressed_header()` parsing through `tile_info()`, `quantization_params()`, `segmentation_params()`, `loop_filter_params()`, `cdef_params()`, `lr_params()`, `read_tx_mode()`, `skip_mode_params()`, and `global_motion_params()`, plus multi-tile `OBU_FRAME` / `OBU_TILE_GROUP` tracking, `stss` Sync Sample vs. `OPEN_RAP` classification, and sample OBU whitelisting.
2. **ISO/IEC 23003-5 LPCM & ISO/IEC 14496-12 Audio Signaling (`core/`, `codecs/description.c`, `importer/wave_imp.c`)**:
   - Support for uncompressed integer (`ipcm`: 16/24/32-bit big/little-endian) and floating-point (`fpcm`: 32/64-bit IEEE 754) audio in standard and multi-track Fragmented ISOBMFF (`moof`/`traf`/`trun`), including `pcmC`, `chnl`, `srat`, and ISOBMFF `AudioSampleEntryV1`.
   - `trun` default sample duration/size compaction across multiple tracks in `core/fragment.c`, plus `WAVE_FORMAT_EXTENSIBLE` and `WAVE_FORMAT_IEEE_FLOAT` support in `importer/wave_imp.c`.
3. **Hybrid Container Architecture & Paged Block Arrays (`common/alloc.{c,h}`, `common/list.{c,h}`, `core/`)**:
   - Built-in C99 memory auditor (`-DLSMASH_DEBUG_ALLOC`) with `#ptr`/`#size` expression capture, 64-byte aligned headers, front/rear canaries, `0xCD`/`0xDD` memory poisoning, and leak verification.
   - Hybrid container model: `lsmash_entry_list_t` doubly-linked pointer lists for polymorphic Box trees (`extensions`, `trak_list`, `moof_list`) paired with **`lsm_list_t` C99 Paged Block Arrays** ($2^8 = 256$ elements per page) for high-volume sample tables (`stts`, `ctts`, `stsz`, `stz2`, `stss`, `stps`, `sdtp`, `stsc`, `stco`/`co64`, `sgpd`, `sbgp`, `elst`), fragment tables (`trun->optional`, `tfra`, `sidx`, `fragment->pool`), and media timelines (`info_list`, `bunch_list`, `chunk_list`).
4. **Core Module Decomposition Design (`core/isom.{c,h}`)**:
   - Section 6 below specifies the planned decomposition of `core/isom.{c,h}` into focused compilation units (`core/isom.c`, `core/track.c`, `core/sample.c`, `core/stbl.c`).

---

## 4. Detailed Specification Index (`docs/specs/`)

| Document | Scope & Responsibility | Status |
| :--- | :--- | :--- |
| [`docs/HISTORY.md`](docs/HISTORY.md) | Historical lineage (`master`, `av1`, `vimeo`, `dev`), forensic bug analysis, and release changelog | Synchronized (`v2.21.1`) |
| [`docs/specs/memory_architecture.md`](docs/specs/memory_architecture.md) | C99 memory audit (`LSMASH_DEBUG_ALLOC`), `lsmash_multiple_buffers_t` resize invariants, and `lsm_list_t` Paged Block Array architecture | Synchronized (`v2.21.1`) |
| [`docs/specs/av1_obuparse_removal.md`](docs/specs/av1_obuparse_removal.md) | Built-in AV1 OBU parser (`codecs/av1.{c,h}`), Errata 1 & AV1-ISOBMFF v1.2.0 compliance, and IVF importer | Synchronized (`v2.21.1`) |
| [`docs/specs/lpcm_isobmff_fragmented.md`](docs/specs/lpcm_isobmff_fragmented.md) | ISO/IEC 23003-5 (`ipcm`, `fpcm`, `pcmC`), ISO/IEC 14496-12 (`chnl`, `srat`, `AudioSampleEntryV1`), and Fragmented LPCM | Synchronized (`v2.21.1`) |
| [`docs/specs/testing_strategy.md`](docs/specs/testing_strategy.md) | Self-contained synthetic test suite (`make test`), bitstream generators, malformed box negative tests, and regression suites | Synchronized (`v2.21.1`) |

---

## 5. Build, Toolchain & Verification

- **Toolchain**: POSIX `sh` + GNU `make` + C99 compiler (`gcc` or `clang`). On Windows, verified with the MSYS2 MinGW64 toolchain (`x86_64-w64-mingw32-gcc`).
- **Build & Test Command**:
  ```sh
  ./configure
  make -j8 all test
  ```
- **Quality Requirements**:
  1. Zero compiler warnings under `-Wall -Wextra -std=c99` across `liblsmash.a`, CLI tools, and `tests/test_main.c`.
  2. All 14 synthetic test suites in `make test` must pass with **0 leaks** and **0 active blocks/bytes** reported by `lsm_alloc_verify_no_leaks()`.

---

## 6. `core/isom.{c,h}` Decomposition Design (`core/isom.c`, `core/track.c`, `core/sample.c`, `core/stbl.c`)

Currently, `core/isom.c` (~3,490 lines) and `core/isom.h` (68 lines) combine four distinct responsibilities in a single compilation unit:
1. **Sample Table (`stbl`) & Sample Group (`sgpd`/`sbgp`) Entry Manipulation** (~1,420 lines)
2. **Track (`trak`), Edit List (`edts`/`elst`), Track Reference (`tref`), & Handler (`hdlr`) Management** (~940 lines)
3. **Sample Pooling, Non-Fragmented Sample Appending, & Bitrate/DTS/Duration Updates** (~750 lines)
4. **Movie (`moov`), Brand (`ftyp`), File Initialization, Progressive Download Remuxing, & Finish Lifecycle** (~380 lines)

### 6.1 Target 4-Module Decomposition (`core/`)

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
|  core/stbl.c  |    | core/track.c  | | core/sample.c |    |  core/isom.c  |
|  Sample Table |    | Track, Media, | | Sample Alloc, |    | Movie, Brands,|
|  stts/ctts/   |    | Handler, tref,| | Pool, Append, |    | File Finish,  |
|  stsz/stz2/   |    | edts/elst,    | | Flush, Bitrate|    | Progressive   |
|  stsc/stco/   |    | Track Dur.    | | & Timestamps  |    | Download      |
|  sgpd/sbgp    |    |               | |               |    |               |
+---------------+    +---------------+ +---------------+    +---------------+
```

### 6.2 Function-by-Function Assignment & `lsm_isom_*` Internal Renaming

In accordance with the symbol naming conventions (Section 2.4):
- Public API functions declared in `lsmash.h` keep their `lsmash_*` names.
- Module-local helpers remain `static` with `<module>_*` (`isom_*`) names.
- Internal functions called across `.c` files in `core/`, `codecs/`, or `importer/` use the **`lsm_isom_*`** prefix and are declared in `core/isom.h` (or `core/box.h`).

#### Module 1: `core/stbl.c` — Sample Table & Sample Group Operations
Owns `stsd` codec description lookup, `stts`, `ctts`, `cslg`, `stsz`, `stz2`, `stsc`, `stco`/`co64`, `stss`, `stps`, `sdtp`, and `sgpd`/`sbgp` (`rap `, `roll`, `prol`) entry manipulation:

| Current Symbol | Visibility | Target Symbol & Signature | Callers Outside `core/stbl.c` |
| :--- | :--- | :--- | :--- |
| `isom_get_sample_count` | Cross-file (`core/`, `codecs/`) | `uint32_t lsm_isom_get_sample_count( isom_trak_t *trak )` | `core/sample.c`, `core/timeline.c`, `codecs/*.c` |
| `isom_get_dts_elist_duration` | File-local `static` | *(inline / `core/track.c`)* | `core/track.c` |
| `isom_add_stts_entry` | File-local `static` | `static int isom_add_stts_entry( ... )` | Local to `core/stbl.c` |
| `isom_add_ctts_entry` | File-local `static` | `static int isom_add_ctts_entry( ... )` | Local to `core/stbl.c` |
| `isom_add_stsc_entry` | File-local `static` | `static int isom_add_stsc_entry( ... )` | Local to `core/stbl.c` |
| `isom_add_stco_entry` | File-local `static` | `static int isom_add_stco_entry( ... )` | Local to `core/stbl.c` |
| `isom_add_co64_entry` | File-local `static` | `static int isom_add_co64_entry( ... )` | Local to `core/stbl.c` |
| `isom_convert_stco_to_co64` | Cross-file (`core/`) | `int lsm_isom_convert_stco_to_co64( isom_stbl_t *stbl )` | `core/isom.c`, `core/stbl.c` |
| `isom_add_chunk_offset_entry` | File-local `static` | `static int isom_add_chunk_offset_entry( ... )` | Local to `core/stbl.c` |
| `isom_add_stsz_entry` | File-local `static` | `static int isom_add_stsz_entry( ... )` | Local to `core/stbl.c` |
| `isom_add_stz2_entry` | File-local `static` | `static int isom_add_stz2_entry( ... )` | Local to `core/stbl.c` |
| `isom_Paid_field_size` / `isom_compact_sample_size_table` | Cross-file (`core/`) | `int lsm_isom_compact_sample_size_table( isom_stbl_t *stbl )` | `core/isom.c` |
| `isom_add_SampleSize_entry` | Cross-file (`core/`) | `int lsm_isom_add_sample_size_entry( isom_stbl_t *stbl, uint32_t sample_size )` | `core/sample.c` |
| `isom_add_stss_entry` | Cross-file (`core/`) | `int lsm_isom_add_stss_entry( isom_stbl_t *stbl, uint32_t sample_number )` | `core/sample.c` |
| `isom_add_stps_entry` | Cross-file (`core/`) | `int lsm_isom_add_stps_entry( isom_stbl_t *stbl, uint32_t sample_number )` | `core/sample.c` |
| `isom_add_sdtp_entry` | Cross-file (`core/`) | `int lsm_isom_add_sdtp_entry( isom_box_t *parent, lsmash_sample_property_t *prop, uint8_t avc_extensions )` | `core/sample.c`, `core/fragment.c`, `core/timeline.c` |
| `isom_replace_last_sample_delta` | Cross-file (`core/`) | `int lsm_isom_replace_last_sample_delta( isom_stbl_t *stbl, uint32_t sample_delta )` | `core/sample.c`, `core/timeline.c` |
| `isom_update_time_tables` | Cross-file (`core/`) | `int lsm_isom_update_time_tables( isom_trak_t *trak, lsmash_sample_t *sample, uint32_t *samples_per_packet )` | `core/sample.c` |
| `isom_append_chunk` | Cross-file (`core/`) | `int lsm_isom_append_chunk( isom_trak_t *trak, lsmash_sample_t *sample, uint32_t samples_per_packet )` | `core/sample.c` |
| `isom_get_roll_group_entry` / `isom_add_roll_group_entry` / `isom_deduplicate_roll_group` / `isom_update_roll_group` / `isom_all_recovery_completed` | Cross-file (`core/`) | `lsm_isom_add_roll_group_entry`, `lsm_isom_update_roll_group`, `lsm_isom_all_recovery_completed` | `core/sample.c`, `core/fragment.c`, `core/timeline.c` |
| `isom_get_rap_group_entry` / `isom_add_rap_group_entry` / `isom_update_rap_group` | Cross-file (`core/`) | `lsm_isom_add_rap_group_entry`, `lsm_isom_update_rap_group` | `core/sample.c`, `core/fragment.c`, `core/timeline.c` |
| `isom_get_sample_group_description` / `isom_get_sample_to_group` | Cross-file (`core/`) | `lsm_isom_get_sample_group_description`, `lsm_isom_get_sample_to_group` | `core/fragment.c`, `core/timeline.c`, `core/stbl.c` |
| `isom_setup_sample_grouping` | Cross-file (`core/`) | `int lsm_isom_setup_sample_grouping( isom_box_t *parent, isom_grouping_type grouping_type )` | `core/track.c`, `core/fragment.c`, `core/timeline.c` |
| `isom_group_random_access` / `isom_group_roll_recovery` | Cross-file (`core/`) | `lsm_isom_group_random_access`, `lsm_isom_group_roll_recovery` | `core/sample.c`, `core/fragment.c`, `core/timeline.c` |

#### Module 2: `core/track.c` — Track, Media, Handler, Track Reference & Edit List Management
Owns `trak`, `tkhd`, `tapt`, `edts`/`elst`, `tref`, `mdia`, `mdhd`, `hdlr`, `minf`, and track duration/parameter APIs:

| Current Symbol | Visibility | Target Symbol & Signature | Callers Outside `core/track.c` |
| :--- | :--- | :--- | :--- |
| `isom_get_trak` | Cross-file (`core/`, `codecs/`) | `isom_trak_t *lsm_isom_get_trak( lsmash_file_t *file, uint32_t track_ID )` | `core/*.c`, `codecs/*.c` |
| `isom_set_media_handler_name` | File-local `static` | `static int isom_set_media_handler_name( ... )` | Local to `core/track.c` |
| `isom_check_mandatory_boxes` / `isom_check_minf_boxes` / `isom_check_stbl_boxes` | Cross-file (`core/`) | `int lsm_isom_check_mandatory_boxes( lsmash_file_t *file )` | `core/isom.c`, `core/fragment.c` |
| `lsmash_create_track` / `lsmash_delete_track` | Public API (`lsmash.h`) | `lsmash_create_track`, `lsmash_delete_track` | Public |
| `lsmash_get_track_ID` / `lsmash_get_track_parameters` / `lsmash_set_track_parameters` | Public API (`lsmash.h`) | `lsmash_*` | Public |
| `lsmash_get_media_parameters` / `lsmash_set_media_parameters` | Public API (`lsmash.h`) | `lsmash_*` | Public |
| `lsmash_update_track_duration` | Public API (`lsmash.h`) | `lsmash_update_track_duration` | Public, `core/isom.c`, `core/fragment.c` |
| `lsmash_create_explicit_timeline_map` / `lsmash_modify_explicit_timeline_map` / `lsmash_get_explicit_timeline_map` / `lsmash_delete_explicit_timeline_map` / `lsmash_count_explicit_timeline_map` | Public API (`lsmash.h`) | `lsmash_*` | Public |
| `lsmash_set_track_reference` | Public API (`lsmash.h`) | `lsmash_set_track_reference` | Public |

#### Module 3: `core/sample.c` — Sample Allocation, Sample Pool, Appending, & Bitrate/Timestamp Updates
Owns `lsmash_sample_t` lifecycle, `isom_sample_pool_t` management, `lsmash_append_sample`, `lsmash_flush_pooled_samples`, and bitrate/timestamp table extraction:

| Current Symbol | Visibility | Target Symbol & Signature | Callers Outside `core/sample.c` |
| :--- | :--- | :--- | :--- |
| `lsmash_create_sample` / `lsmash_Sample_alloc` / `lsmash_delete_sample` | Public API (`lsmash.h`) | `lsmash_*` | Public |
| `isom_create_sample_pool` / `isom_remove_sample_pool` | Cross-file (`core/`) | `lsm_isom_create_sample_pool`, `lsm_isom_remove_sample_pool` | `core/box.c`, `core/track.c`, `core/fragment.c` |
| `isom_pool_sample` | Cross-file (`core/`) | `int lsm_isom_pool_sample( isom_sample_pool_t *pool, lsmash_sample_t *sample, uint32_t samples_per_packet )` | `core/fragment.c`, `core/sample.c` |
| `isom_append_sample_by_type` / `isom_flush_pooled_samples` | File-local `static` | `static int isom_append_sample_by_type( ... )` | Local to `core/sample.c` |
| `lsmash_append_sample` / `lsmash_flush_pooled_samples` | Public API (`lsmash.h`) | `lsmash_*` | Public |
| `isom_update_bitrate_description` | Cross-file (`core/`) | `int lsm_isom_update_bitrate_description( isom_mdia_t *mdia )` | `core/isom.c`, `core/fragment.c` |
| `isom_check_Variablesize_stsz` / `isom_get_sample_info_from_stbl` | File-local `static` | `static ...` | Local to `core/sample.c` |
| `lsmash_get_media_timestamps` / `lsmash_set_media_timestamps` / `lsmash_delete_media_timestamps` / `lsmash_sort_dts` | Public API (`lsmash.h`) | `lsmash_*` | Public |

#### Module 4: `core/isom.c` — Movie Context, Compatibility Brands, File Finalization & Progressive Download
Retains movie-level setup, brand compatibility validation, `lsmash_finish_movie`, progressive download (`moov`-before-`mdat`) rewriting, and root/file helpers:

| Current Symbol | Visibility | Target Symbol & Signature | Callers Outside `core/isom.c` |
| :--- | :--- | :--- | :--- |
| `isom_check_initializer_present` | Cross-file (`core/`, `codecs/`) | `int lsm_isom_check_initializer_present( lsmash_root_t *root )` | `core/*.c`, `codecs/*.c` |
| `isom_check_compatibility` | Cross-file (`core/`) | `int lsm_isom_check_compatibility( lsmash_file_t *file )` | `core/read.c`, `core/file.c`, `core/track.c` |
| `isom_establish_movie` | Cross-file (`core/`) | `int lsm_isom_establish_movie( lsmash_file_t *file )` | `core/file.c` |
| `isom_complement_data_reference` | Cross-file (`core/`) | `int lsm_isom_complement_data_reference( isom_minf_t *minf )` | `core/track.c`, `core/isom.c` |
| `lsmash_initialize_movie_parameters` / `lsmash_set_movie_parameters` / `lsmash_get_movie_parameters` | Public API (`lsmash.h`) | `lsmash_*` | Public |
| `lsmash_set_copyright` / `lsmash_set_free_space` / `lsmash_write_free_space` | Public API (`lsmash.h`) | `lsmash_*` | Public |
| `lsmash_finish_movie` / `lsmash_write_lsmash_indicator` | Public API (`lsmash.h`) | `lsmash_*` | Public |
