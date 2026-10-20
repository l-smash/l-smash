# L-SMASH Project History & Architectural Evolution (`docs/HISTORY.md`)

## 1. Repository Lineage & Branch Topology

L-SMASH was created in 2010 by Yusuke Nakamura (`muken.the.vfrmaniac@gmail.com`) as a clean, specification-loyal ISOBMFF and QTFF library and toolset under the ISC License.

### 1.1 Key Historical Branches Audited at Project Revival
| Branch / Ref | Tip Commit | Summary of State |
| :--- | :---: | :--- |
| `master` (`origin/master`) | `18a9ed2` | Last official upstream `master` commit (2016-12-23: *"osdep: add missing fallback definitions of fseeko and ftello"*). |
| `av1` (`origin/av1`) | `96065d0` | Official WIP branch by Yusuke Nakamura (2 commits ahead of `master`: `2bd228c` *"[WIP] av1: Add Support Alliance for Open Media AV1"* and `96065d0` *"[WIP] Add IVF importer for AV1"*), containing a 1,747-line native AV1 OBU parser in `codecs/av1.{c,h}` and IVF importer in `importer/ivf_imp.c`. |
| `origin/memcheck` | `d549d80` | Experimental branch by Yusuke Nakamura introducing `common/memint.h` (`lsmash_mem_alloc_B` checked allocation wrappers). |
| `vimeo` (`vimeo/master`) | `4a21f21` | Community fork (49 commits ahead of `master`) adding Opus (`dOps`), Spherical Video v1/v2 (`st3d`, `sv3d`), HDR metadata (`mdcv`, `clli`), ProRes, RTP Hint Track authoring, `srat` Box reader/printer, and an external `obuparse`-based AV1 implementation (`codecs/av1_obu.{c,h}`). |
| `dev` | Branched from `4a21f21` | Active development branch for the official L-SMASH revival by original author Yusuke Nakamura. |

---

## 2. Notable Historical Commits & Architectural Lessons

1. **Commit `04e39f1` (`2017-01-31`, `core/timeline.c`): *"Optimize DTS calculation to fix linear access slowdown"***
   - Demonstrated that `lsmash_entry_list_t`'s single `last_accessed_entry` cursor degrades to $O(N)$ linear pointer-chasing per sample ($O(N^2)$ overall) when sample queries arrive out of monotonic order (such as B-frame CTS-ordered lookups in `isom_get_dts_from_info_list()`).
   - Serves as direct empirical motivation for Subsystem's migration of high-volume sample and timeline lists (`info_list`, `bunch_list`, `chunk_list`, `stts`, `ctts`, `stsz`, `sdtp`, `trun->optional`) to $O(1)$ Paged Block Arrays.
2. **Commit `9bc5dc3` (`2017-02-23`, `core/box.c`): *"Update fullbox_type_table size"***
   - Expanded `static lsmash_box_type_t fullbox_type_table[50]` to `fullbox_type_table[62]` while adding 12 FullBoxes (`svhd`, `proj`, `prhd`, `cbmp`, `equi`, `mshp`, `st3d`, `GULL`, `SAND`, `stvi`, `alac`, `srat`), leaving **60 FullBoxes + 1 terminator = 61 used slots out of 62 (only 1 free slot)**.
   - Confirmed the latent buffer-overflow hazard when adding the 2 FullBoxes required for ISO/IEC 23003-5 LPCM (`pcmC` and `chnl`).
3. **Commits `2bd228c` & `96065d0` (`2017-04-28` / `2018-09-21`, `origin/av1`) vs. `ff5e5fe` (`2019-06-14`, `vimeo`)**
   - While `vimeo` introduced an external dependency on `obuparse` (`codecs/av1_obu.{c,h}`), the official `av1` branch already contained the original author's native OBU bitstream parser (`codecs/av1.{c,h}`) and IVF importer (`importer/ivf_imp.c`), requiring only targeted bitstream bug fixes (`av1_get_uvlc`, `av1_get_leb128`, `av1_uncompressed_header`, and `av1C` `twelve_bit` propagation) to achieve complete dependency-free AV1 support.

---

## 3. Revival Milestones

### the implementation: Project Revival, Branch Establishment & Full Read-Only Audit
- Established `dev` branch from `vimeo` (`4a21f21`) and installed local workspace/session protection hooks (`commit-msg`, `pre-commit`, `.gitignore`, `.ignore`, remote push lock).
- Executed a comprehensive Read-Only audit across `common/`, `core/`, `codecs/`, `importer/`, `cli/`, and build scripts (`configure`, `Makefile`).
- Authored the initial hierarchical specification suite:
  - `SPEC.md`
  - `docs/specs/memory_architecture.md`
  - `docs/specs/av1_obuparse_removal.md`
  - `docs/specs/lpcm_isobmff_fragmented.md`
  - `docs/specs/testing_strategy.md`
  - `docs/HISTORY.md`

### the implementation: Complete Removal of `obuparse`, Native AV1 OBU Parser Integration, and Synthetic Test Suite Establishment
- Fixed `lsmash_bs_get_le64()` (`<< 16` $\rightarrow$ `<< 32`) and `lsmash_bs_get_le32()` (`(uint32_t)` cast before `<< 16`) in `common/bytes.c`.
- Removed external `-lobuparse` dependency from `configure` and deleted `codecs/av1_obu.c` and `codecs/av1_obu.h`.
- Salvaged the original author's native AV1 OBU parser from `origin/av1` (`2bd228c`, `96065d0`) into `codecs/av1.{c,h}` and `importer/ivf_imp.c`, fixing all identified bitstream bugs (`av1_get_uvlc`, `av1_get_leb128`/`av1_show_leb128`, `av1_parser_color_config`, `OrderHintBits`, `av1_uncompressed_header`, `av1_decode_frame_wrapup`, and 12-bit `twelve_bit` propagation into `av1C`).
- Added `DISPLAY_CODEC_NAME( ISOM_CODEC_TYPE_AV01_VIDEO, AOM AV1 Video );` in `cli/muxer.c` and exported `lsmash_setup_av1_specific_parameters_from_access_unit` and `lsmash_destroy_av1_headers` in `lsmash.h`.
- Created the self-contained, copyright-free synthetic test suite (`tests/test_main.c`, `make test` / `make check`) verifying bitstream primitives, Sequence Header OBUs (8/10/12-bit, BT.709 vs. sRGB), Frame Header OBUs, ITU-T T.35 HDR10+ metadata detection, `av1C` box round-trip, and synthetic IVF-to-ISOBMFF mux/demux round-trip (134 assertions passing with 0 warnings).

### the implementation: ISO/IEC 23003-5 LPCM-in-ISOBMFF, Multi-Track Fragmented Audio, C99 Memory Audit Mechanism, and structured Naming Convention
- Established the **Symbol Naming Conventions** in `SPEC.md` (`lsmash_*`/`LSMASH_*` for Public API Public API, `lsm_{filename}_*` for Internal Shared Library-Internal Shared API, and `static {filename}_*` for File-Local Static File-Local Private API).
- Implemented the self-contained C99 Memory Audit Mechanism in `common/alloc.{c,h}` (enabled via `-DLSMASH_DEBUG_ALLOC` in `make test`), wrapping all 6 allocation functions (`lsmash_malloc`, `lsmash_malloc_zero`, `lsmash_realloc`, `lsmash_memdup`, `lsmash_free`, `lsmash_freep`) with `#ptr`/`#size` stringified expression capture, 64-byte aligned intrusive doubly-linked list headers, front/rear canaries, `0xA5`/`0xDD` memory poisoning, and zero-leak assertion enforcement (`lsm_alloc_verify_no_leaks()`).
- Resolved 6 critical pre-existing container/fragment bugs:
  1. Replaced the fixed-size `fullbox_type_table[62]` in `core/box.c` with a compile-time `static const` unsized array and registered `ISOM_BOX_TYPE_PCMC` (`pcmC`) and `ISOM_BOX_TYPE_CHNL` (`chnl`).
  2. Fixed `isom_write_audio_description()` (`core/write.c`) and `isom_add_audio_description()` (`core/box.c`) to check `audio->manager & LSMASH_QTFF_BASE` before emitting 16-byte QTFF v1 fields, and fixed `isom_box_common_copy()` (`core/read.c:135`) so `stsd->version = 1` is preserved when reading `AudioSampleEntryV1`.
  3. Implemented `isom_write_srat()` and registered `ISOM_BOX_TYPE_SRAT` in `box_writer_table[]` (`core/write.c`).
  4. Replaced `if( trun->flags )` with `if( trun->flags & ISOM_TR_FLAGS_OPTIONAL_ROWS_PRESENT_MASK )` (`0x000f00`) in `core/fragment.c`, preventing tens of thousands of redundant `isom_trun_optional_row_t` allocations on secondary tracks in multi-track Fragmented MP4.
  5. Fixed `useful_first_sample_flags` (`core/fragment.c:948`) and the `rap->trun_number` shift typo (`core/fragment.c:1598`).
- Implemented full strict standard compliance support for ISO/IEC 23003-5 (`ipcm`, `fpcm`, `pcmC`) and ISO/IEC 14496-12 (`chnl`, `srat`, `AudioSampleEntryV1`) across `lsmash.h`, `core/`, `codecs/description.c`, `importer/wave_imp.c` (including `WAVE_FORMAT_IEEE_FLOAT` and `WAVE_FORMAT_EXTENSIBLE` 5.1ch/7.1ch CICP channel layout mapping), and `cli/muxer.c`.
- Expanded `tests/test_main.c` to 8 test suites and 6,236 assertions (100% passing with 0 memory leaks).

### the implementation: Preservation of Specification Syntax Identifiers & the implementation/3 AV1 v1.0.0 Errata 1 / AV1-ISOBMFF v1.2.0 Compliance Audit
- Codified the **Preservation of Specification Syntax Identifiers** in `SPEC.md` (Section 2.4): all bitstream syntax element names (`mono_chrome`, `subsampling_x`, `leadingZeros`, `PCM_sample_size`, `definedLayout`, `omittedChannelsMap`, `speaker_position`, `azimuth`, `elevation`, etc.) and derived variable names (`SeenSeqHeader`, `SeenFrameHeader`, `FrameIsIntra`, `OrderHintBits`, `Leb128Bytes`, `UpscaledWidth`, etc.) must match the governing specifications character-for-character without case conversion or abbreviation.
- Audited and aligned all the implementation (`codecs/av1.{c,h}`, `importer/ivf_imp.c`) and the implementation (`core/read.c`, `core/write.c`, `codecs/description.c`) struct member and local variable names to the Preservation of Specification Syntax Identifiers.
- Resolved all 6 regressions in `codecs/av1.{c,h}` from the `origin/av1` (`2bd228c`) integration:
  1. Restored `Copyright (C) 2017-2020 L-SMASH project` (+ `Copyright (c) 2026 Yusuke Nakamura`) in `codecs/av1.{c,h}` and updated specification references to `AV1 Bitstream & Decoding Process Specification Version 1.0.0 with Errata 1` and `AV1 Codec ISO Media File Format Binding v1.2.0, 12 December 2019`.
  2. Fixed Sync Sample (`stss`) determination in `av1_parse_temporal_unit()` (`codecs/av1.c`) to require an `OBU_SEQUENCE_HEADER` within the same Temporal Unit prior to the first Frame Header / Frame OBU (`tu.with_sequence_header`), marking Key Frame Temporal Units without an in-TU Sequence Header as `ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP` per AV1-ISOBMFF v1.2.0 Section 2.4.
  3. Implemented the full `uncompressed_header()` through `film_grain_params()` in `codecs/av1.c`, removed the erroneous `&& is_frame_obu` check in `av1_parse_tile_group()`, updated `av1_parse_frame()` to align and parse `tile_group_obu()`, and deferred `av1_decode_frame_wrapup()` + `SeenFrameHeader = 0` until `tg_end == frame->NumTiles - 1` (supporting multi-tile split `OBU_FRAME` + `OBU_TILE_GROUP` streams).
  4. Restored `av1_print_codec_specific()` (`"[%s: AV1CodecConfigurationBox]\n"`, human-readable `chroma_sample_position` strings, and trailing `configOBUs` / `size = %"PRIu32"\n`).
  5. Restored appending initial `OBU_METADATA` OBUs into `param->configOBUs` in the initial configuration Temporal Unit (`tu.recording_config_obus`) per AV1-ISOBMFF v1.2.0 Section 2.3.3.
  6. Replaced the OBU sample filtering blacklist with a strict AV1-ISOBMFF v1.2.0 Section 2.2 whitelist (`av1_is_sample_obu()`).
- Fixed a bitstream writer bug in `lsmash_bits_put_align()` (`common/bits.c`) where `bits->store` and `bits->cache` were not reset to `0` after flushing the aligned byte.
- Enforced `TileNum` continuity (`tg_start == frame->TileNum`), end-of-TU `SeenFrameHeader == 0` validation, and `ivf_importer_probe()` frame presence validation.
- Expanded `tests/test_main.c` to 9 test suites and 6,257 assertions (100% passing with 0 memory leaks).

### the implementation: C99 Memory Architecture & Container Modernization (`lsm_list_t` Paged Block Array Migration)
- Implemented the C99 Paged Block Array container (`lsm_list_t`, $2^8 = 256$ fixed-capacity element pages) and unified C99 iterator/accessor primitives (`LSM_LIST_FOR_EACH`, `LSM_ENTRY_LIST_FOR_EACH`, `lsm_list_count`, `lsm_list_at`, `lsm_list_get`, `lsm_list_head`, `lsm_list_tail`, `lsm_list_append`, `lsm_list_add`, `lsm_list_remove_tail`, `lsm_list_remove`, `lsm_list_move`) in `common/list.{c,h}`.
- Eliminated per-entry doubly-linked list node (`lsmash_entry_t`) and double-`malloc` overhead across all High-volume paged tables high-volume tables while preserving 100% compatibility with Box-tree pointer lists Box-tree ownership (`box_default` Null-Object Sentinel Pattern, `parent->extensions`, `LSMASH_IN_PREDEFINED_LIST`, `offset_in_parent`):
  1. **Media Timeline (`core/timeline.c`)**: Migrated `timeline->edit_list`, `timeline->chunk_list`, `timeline->info_list`, and `timeline->bunch_list` to `lsm_list_t`, guaranteeing 100% pointer stability for `info.chunk`/`bunch.chunk` references into `chunk_list` and simplifying `isom_get_dts_from_info_list()`, `isom_get_cts_from_info_list()`, and `isom_get_bunch()` to direct $O(1)$ indexed lookups.
  2. **Movie Fragments (`core/fragment.c`, `core/box.{c,h}`, `core/file.c`, `core/isom.c`, `core/read.c`, `core/write.c`, `core/print.c`)**: Migrated `trun->optional`, `tfra->list`, `sidx->list`, and `fragment->pool` to `lsm_list_t`, replacing the $O(N)$ linear scan in `isom_request_trun_optional_row()` with $O(1)$ `lsm_list_get()`.
  3. **Sample Tables & Edit Lists (`core/`)**: Migrated `elst->list`, `stts->list`, `ctts->list`, `stsz->list`, `stz2->list`, `stss->list`, `stps->list`, `sdtp->list`, `stsc->list`, `stco->list`/`co64->list`, `sgpd->list`, `sbgp->list`, and `cache->roll.pool` to `lsm_list_t`.
  4. **Index-Safe Roll Group Deduplication (`core/isom.{c,h}`)**: Replaced raw `sbgp->list` pointer caching in `isom_roll_group_t` (`cache->roll.pool`) with `isom_sbgp_t *sbgp` and 1-based `uint32_t assignment_number` (decremented for subsequent pending groups upon mid-table merge in `isom_deduplicate_roll_group()`), eliminating dangling pointer hazards during mid-table deletion.
- Added `test_paged_block_array()` (``..``) and `test_large_scale_paged_tables_and_roll_dedup()` (``..``) to `tests/test_main.c`, expanding the synthetic test suite to 11 test suites and 146,276 assertions (100% passing with 0 warnings, 0 memory leaks, and 0 canary corruptions under `-DLSMASH_DEBUG_ALLOC`).
