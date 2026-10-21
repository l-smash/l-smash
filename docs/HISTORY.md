# L-SMASH Project History & Architectural Evolution (`docs/HISTORY.md`)

## 1. Upstream & Fork Lineage (`master`, `av1`, `vimeo`, `dev`)

### 1.1 Original L-SMASH (`master`: `r1` – `r1485`, 2010–2017)
- Created and maintained primarily by **Yusuke Nakamura (`muken.the.vfrmaniac@gmail.com`)** and contributors (**Takashi Hirata**, **astrataro**, ** Yusuke** et al.).
- Established a pure C99 library (`liblsmash`) and CLI toolset (`muxer`, `remuxer`, `boxdumper`, `timelineeditor`) for reading, writing, and remuxing ISO Base Media File Format (ISOBMFF, ISO/IEC 14496-12) and QuickTime File Format (QTFF).
- Introduced the `box_default` Sentinel Box Architecture (`core/box_default.{c,h}`), dual-list Box ownership (`extensions` + type-specific lists), and self-contained elementary stream importers (`H.264/AVC`, `H.265/HEVC`, `VC-1`, `AAC/ADTS`, `ALS`, `AC-3/E-AC-3`, `DTS`, `AMR`, `MP3`, `WAVE`).
- Upstream `master` reached `v2.14.5` (`59030f5`, `r1485`).

### 1.2 Experimental `av1` Branch (`0715bf6`)
- Added initial experimental AV1 (`av01` / `av1C`) support and an IVF importer (`importer/ivf_imp.c`), originally relying on an external `obuparse` library dependency.

### 1.3 Vimeo Fork (`vimeo`: `r1486` – `r1526`, `4a21f21`, 2017–2020)
- Maintained by **Derek Buitenhuis** and contributors at Vimeo (`vimeo/l-smash`).
- Added HDR / Dolby Vision (`dvcC`/`dvvC`, `dby1`), Mastering Display Color Volume (`mdcv`), Content Light Level Info (`clli`), Spherical Video v1/v2 (`sv3d`), Stereoscopic 3D (`st3d`), Google Spatial Audio (`SA3D`), and initial AV1 (`av01`/`av1C`) support.
- Reached `v2.16.1` (`4a21f21`, `r1526`).

---

## 2. Historical Defect Analysis (Pre-`v2.17.0` Baseline)

Prior to the `v2.21.0`–`v2.23.1` series on `dev`, the `vimeo` (`4a21f21`) and `av1` (`0715bf6`) branches exhibited several technical limitations and defects:

1. **64-Bit Little-Endian Shift Bug in `common/bytes.c` (`lsmash_bs_get_le64`)**:
   - `lsmash_bs_get_le64()` shifted the upper 32-bit word by `<< 16` instead of `<< 32` (`((uint64_t)lsmash_bs_get_le32( bs ) << 16) | lsmash_bs_get_le32( bs )`), corrupting 64-bit little-endian reads.
2. **External `obuparse` Dependency & Incomplete AV1 Bitstream Compliance**:
   - The `vimeo` branch required an external `obuparse` library for AV1 importing, breaking L-SMASH's self-contained build design.
   - The experimental `av1` branch (`0715bf6`) only parsed `sequence_header_obu()` and the top of `uncompressed_header()`, stopping before `tile_info()` and failing to parse multi-tile `OBU_FRAME` / `OBU_TILE_GROUP` bitstreams or update reference slots on `show_existing_frame == 1`.
3. **Lack of ISO/IEC 23003-5 LPCM (`ipcm`/`fpcm`/`pcmC`) & ISO/IEC 14496-12 `chnl` Support**:
   - L-SMASH only supported uncompressed PCM inside QuickTime `.mov` (or legacy `mp4a`), lacking ISO/IEC 23003-5 `ipcm`/`fpcm`/`pcmC` and ISO/IEC 14496-12 `chnl` / `srat` / `AudioSampleEntryV1` for standard and Fragmented ISOBMFF.
   - In addition, `core/box.c` (`fullbox_type_table` fixed at 60 slots), `core/write.c` (`isom_write_audio_description` writing QTFF fields into ISOBMFF `version == 1`, and `isom_write_srat` writing a 16.16 fixed-point shift), and `core/fragment.c` (`isom_append_fragment_track_run` checking `traf->trun_list.tail` instead of `moof->traf_list.tail`) had latent bugs affecting multi-track fragmented audio.
4. **Per-Sample Doubly-Linked List Overhead (`lsmash_entry_list_t`)**:
   - Storing high-volume Sample Tables (`stts`, `ctts`, `stsz`, `stsc`, `stco`, `sdtp`), Movie Fragment tables (`trun`, `tfra`, `sidx`), and Media Timelines (`info_list`, `bunch_list`, `chunk_list`) as heap-allocated doubly-linked nodes incurred two `malloc` calls and 24 bytes of pointer overhead per sample.
5. **Runtime-Initialized Fixed-Size `static` Dispatch Tables & `emsg` Reader Bug**:
   - Dispatch tables in `core/` and `codecs/` used lazy runtime initialization into fixed-size `static` arrays (e.g., `static ... table[128]`), creating data-race hazards in multi-threaded callers and silent bounds-overflow risks.
   - `isom_read_emsg()` in `core/read.c:735` checked `if( !emsg->scheme_id_uri )` twice instead of checking `if( !emsg->value )` after the second allocation.
6. **`lsmash_resize_multiple_buffers` Memory Corruption & `cli/muxer.c` Option Bug**:
   - `lsmash_resize_multiple_buffers()` in `common/multibuf.c` called `lsmash_realloc()` without re-striding buffers $1 \dots N-1$ from the old `buffer_size` stride to the new `buffer_size` stride.
   - `cli/muxer.c:549` checked `else if( !strcasecmp( argv[i], "--compact-size-table" ) )` inside the `if( !strcasecmp( argv[i], "--language" ) )` block, making `--compact-size-table` unreachable on the command line.

---

## 3. Release Evolution (`v2.21.0` – `v2.23.1`)

### 3.1 `v2.21.0`: Built-In AV1 OBU Parser, `le64` Fix & Synthetic Test Suite
- **`common/bytes.c`**: Fixed `lsmash_bs_get_le64()` to shift the high 32-bit word by `<< 32` rather than `<< 16`.
- **`codecs/av1.{c,h}` & `importer/ivf_imp.c`**: Replaced the external `obuparse` dependency with a built-in C99 AV1 OBU parser compliant with the AV1 Bitstream & Decoding Process Specification (v1.0.0 with Errata 1) and AV1 Codec ISO Media File Format Binding (v1.2.0):
  - Implemented `av1_get_uvlc()`, `av1_get_leb128()`, `av1_show_leb128()`, `av1_get_su()`, and `av1_get_ns()`.
  - Implemented full `sequence_header_obu()` and `uncompressed_header()` parsing through `tile_info()`, `quantization_params()`, `segmentation_params()`, `loop_filter_params()`, `cdef_params()`, `lr_params()`, `read_tx_mode()`, `skip_mode_params()`, and `global_motion_params()`.
  - Implemented multi-tile `OBU_FRAME` and `OBU_TILE_GROUP` tracking (`TileNum`, `tg_start`, `tg_end`), `show_existing_frame` keyframe reference slot refresh, `stss` Sync Sample vs. `OPEN_RAP` classification, sample OBU whitelisting, and ITU-T T.35 HDR10+ metadata detection.
- **`configure`, `Makefile`, `tests/test_main.c`**: Removed `--enable-obuparse` and added the self-contained synthetic test runner (`make test`).

### 3.2 `v2.22.0`: C99 Memory Auditor & ISO/IEC 23003-5 LPCM (`ipcm`/`fpcm`/`pcmC`/`chnl`/`srat`)
- **`common/alloc.{c,h}` & `common/internal.h`**: Implemented the built-in C99 memory audit mechanism (`--enable-debug-alloc` / `-DLSMASH_DEBUG_ALLOC`) featuring `#ptr`/`#size` expression capture, 64-byte aligned intrusive headers, front/rear canaries, `0xCD`/`0xDD` memory poisoning, and `lsm_alloc_verify_no_leaks()`.
- **`core/box.{c,h}`, `core/read.c`, `core/write.c`, `core/print.c`, `codecs/description.c`, `lsmash.h`**:
  - Converted `fullbox_type_table` in `core/box.c` to an unbounded `static const` array.
  - Fixed `isom_write_audio_description()` and `isom_read_audio_description()` to support ISOBMFF `AudioSampleEntryV1` (`version == 1`) without emitting QTFF-only Sound Description v1 fields when `!file->qt_compatible`.
  - Fixed `isom_write_srat()` to write a plain 32-bit integer `sampling_rate` (`lsmash_bs_put_be32( bs, srat->sampling_rate )`) per ISO/IEC 14496-12 Section 12.2.3.2.
  - Implemented ISO/IEC 23003-5 `ipcm`/`fpcm` and `pcmC` (`PCMConfigBox`: `format_flags`, `PCM_sample_size`) and ISO/IEC 14496-12 `chnl` (`ChannelLayout`: `stream_structure`, `definedLayout`, `speaker_position`, `azimuth`, `elevation`, `omittedChannelsMap`, `object_count`).
- **`core/fragment.c`**:
  - Fixed `isom_append_fragment_track_run()` to check whether `traf` is the last `traf` in `moof->traf_list.tail` before omitting `data_offset` across multi-track fragments.
  - Added automatic `trun` compaction when all samples in a track run match `tfhd->default_sample_duration`, `default_sample_size`, and `default_sample_flags`.
- **`importer/wave_imp.c`**: Added support for `WAVE_FORMAT_IEEE_FLOAT` (`0x0003`), `WAVE_FORMAT_EXTENSIBLE` (`0xFFFE`), 24/32/64-bit PCM/Float, sampling rates $> 65535\text{ Hz}$, and Microsoft `dwChannelMask` to ISO/IEC 14496-12 `chnl` mapping.

### 3.3 `v2.22.0` (Development): C99 Paged Block Array (`lsm_list_t`) & Sample Table Migration
- **`common/list.{c,h}`**: Implemented `lsm_list_t`, a C99 Paged Block Array storing elements contiguously in $2^8 = 256$-element pages with $O(1)$ bit-shift lookup (`lsm_list_at`, `lsm_list_get`), 100% element pointer stability on append, lockstep multi-table cursors (`lsm_list_cursor_t`), and single-loop C99 iteration macros (`LSM_LIST_FOR_EACH`, `LSM_ENTRY_LIST_FOR_EACH`).
- **`core/timeline.c`, `core/fragment.{c,h}`, `core/isom.c`, `core/read.c`, `core/write.c`, `core/print.c`, `codecs/*`**:
  - Migrated all high-volume sample tables (`stts`, `ctts`, `stsz`, `stz2`, `stss`, `stps`, `sdtp`, `stsc`, `stco`, `co64`, `sgpd`, `sbgp`, `elst`), fragment tables (`trun->optional`, `tfra`, `sidx`, `fragment->pool`), and media timeline tables (`info_list`, `bunch_list`, `chunk_list`) from `lsmash_entry_list_t` to `lsm_list_t`.
  - Fixed a latent pointer-invalidation hazard in `isom_add_roll_group_entry()` / `isom_deduplicate_roll_group()` by storing 1-based group indices (`roll_group_number`, `prev_group_number`, `first_sample_defined`) in `isom_roll_pool_entry_t` instead of raw pointers into `sbgp->list`.

### 3.4 `v2.22.0` (Development): Compile-Time `static const` Dispatch Tables, `emsg` Reader Fix & User Documentation
- **`core/read.c`, `core/write.c`, `core/print.c`, `core/summary.c`, `codecs/description.c`, `codecs/dts.c`**: Converted all remaining lazy runtime-initialized `static` dispatch tables to compile-time `static const` arrays.
- **`core/read.c`, `core/box.c`, `common/bits.{c,h}`**:
  - Fixed `isom_read_emsg()` (`core/read.c:735`) to check `if( !emsg->value )`, added an explicit `pos > box->size` underflow guard before computing `emsg->message_data_length`, and updated `isom_remove_emsg()` (`core/box.c`) to free `scheme_id_uri`, `value`, and `message_data`.
  - Documented the non-owning borrow contract of `lsmash_bits_t` in `common/bits.{c,h}`.
- **`README.md` & `USAGE.md`**: Added English project overview (`README.md`) and user/C API manual (`USAGE.md`), and documented the `core/isom.{c,h}` 4-module decomposition design in `SPEC.md`.

### 3.5 `v2.22.1`: Codec, Metadata, Build, `multibuf` Resize & `--compact-size-table` Fixes
- **Integrated Upstream Contributor Fixes (Preserving Original Git Author Attributions)**:
  1. **NALU Exp-Golomb EOF & Overflow Handling** (`Xinyue Lu <i@7086.in>`): Fixed infinite loop in `nalu_get_codeNum()` (`codecs/nalu.c`) on EOF or $\ge 64$ leading zero bits.
  2. **MP3 `nBlockSize` Calculation** (`Xinyue Lu <i@7086.in>`): Fixed MP3 `nBlockSize` integer division truncation in `waveform_audio_update_bitrate()` (`codecs/qt_wfex.c`), plus added a `cbSize < 12` bounds check before writing `MPEGLAYER3WAVEFORMAT` fields.
  3. **WMA `cbSize` 16-Bit LE Reading** (`Xinyue Lu <i@7086.in>`): Fixed `cbSize` reading in `wma_print_codec_specific()` (`codecs/wma.c`) to use `LSMASH_GET_LE16` instead of `LSMASH_GET_BYTE`, plus added a box size bounds check (`box->size < 8 + (uint64_t)WAVEFORMATEX_MINIMUM_SIZE + cbSize`).
  4. **Opus 8-Channel Mapping Table Lookup** (`Xinyue Lu <i@7086.in>`): Fixed `channel_order` table lookup in `opus_print_codec_specific()` (`codecs/opus.c`) to index by `OutputChannelCount - 1` instead of `StreamCount + CoupledCount - 1`, and fixed the `"front center"` typo.
  5. **Custom iTunes Metadata Type Retrieval** (`Xinyue Lu <i@7086.in>`): Fixed custom iTunes metadata retrieval in `lsmash_get_itunes_metadata()` (`core/meta.c`) to inspect `mean`/`name`/`data` boxes for `ITUNES_METADATA_ITEM_CUSTOM` instead of hardcoding `ITUNES_METADATA_TYPE_NONE`.
  6. **Tyrant Chapter Presence Check** (`Xinyue Lu <i@7086.in>`): Fixed `lsmash_count_tyrant_chapter()` (`core/chapter.c`) to check `LSMASH_IS_NON_EXISTING_BOX( root->file->moov->udta->chpl )`.
  7. **macOS Shared Library Linker Flag Detection** (`Anton Mitrofanov <BugMaster@narod.ru>`): Fixed macOS shared library linker flags in `configure` by testing `-Wl,-version_script` before enabling it (with a follow-up cleanup removing a stray `cat liblsmash.ver`).
- **`common/multibuf.c`**: Rewrote `lsmash_resize_multiple_buffers()` to check for integer multiplication overflow, compact buffers in forward order before `lsmash_realloc` when shrinking, and move buffers in reverse order after `lsmash_realloc` (zero-filling newly exposed tail bytes) when expanding.
- **`cli/muxer.c`**: Moved `else if( !strcasecmp( argv[i], "--compact-size-table" ) )` out of the `--language` conditional block into the top-level option loop in `parse_global_options()`.

### 3.6 `v2.22.2`: Decomposition of `core/isom.c` into `stbl`, `sample`, `track`, and `movie` Modules & `core/` Symbol Naming Unification
- **Elimination of `core/isom.c` and 4-Module Logical Decomposition**:
  - Decomposed the monolithic `core/isom.c` (~4,150 lines) along ISOBMFF logical hierarchy boundaries into four dedicated modules and removed `core/isom.c`:
    1. **`core/stbl.{c,h}`** (*Sample Table & Sample Grouping Engine*): `stts`, `ctts`, `cslg`, `stsz`/`stz2` compaction, `stsc`, `stco`/`co64` 64-bit promotion, `stss`, `stps`, `sdtp`, `btrt`/`esds` bitrate calculation, and `sgpd`/`sbgp` (`rap `, `roll`, `prol`) state machines and roll-group deduplication (`lsm_stbl_*`, `static stbl_*`).
    2. **`core/sample.{c,h}`** (*Sample Lifecycle, Pooling & Chunk Interleaving Engine*): `lsmash_create_sample`, `lsmash_sample_alloc`, `lsmash_delete_sample`, `isom_sample_pool_t` management (`lsm_sample_*`), LPCM frame-sliced accumulation, RTP hint sample aggregation, chunk flushing, `lsmash_append_sample`, `lsmash_flush_pooled_samples`, and `lsmash_set_last_sample_delta` (`static sample_*`).
    3. **`core/track.{c,h}`** (*Track, Media, Data Reference & Edit List Management*): `trak`/`tkhd`/`trex`/`traf`/`tfra` lookup and lifecycle (`lsm_track_*`), media handler and ISO language setup, `lsmash_add_sample_entry`, `dref` (`url `) data references, and `edts`/`elst` explicit timeline maps (`static track_*`).
    4. **`core/movie.{c,h}`** (*Movie Context, Progressive Download & User Data*): `moov`/`mvhd`/`iods` lifecycle (`lsm_movie_*`), `lsmash_finish_movie`, progressive download `moov` rearrangement and offset recalculation (`static movie_*`), `lsmash_reserve_media_data_size`, `lsmash_create_object_descriptor`, `lsmash_set_copyright` (`cprt`), and `lsmash_set_sdp` (`hnti`/`sdp `/`rtp `).
  - **`core/chapter.c`**: Relocated `lsmash_delete_tyrant_chapter()` from `core/isom.c` to `core/chapter.c` alongside the other chapter manipulation APIs.
  - **`core/box.h` & `configure`**: Extracted internal prototypes and sample/grouping structs from `core/box.h` into `core/stbl.h`, `core/sample.h`, `core/track.h`, and `core/movie.h`, and updated `SRC_CORE` in `configure` and all callers across `core/`, `codecs/`, and `importer/`.
- **Full `core/` Symbol Naming Convention Unification (`lsm_<filename>_*` / `static <filename>_*`)**:
  - Standardized all internal shared functions (`lsm_<filename>_*`) and file-local static functions (`static <filename>_*`) across all remaining `core/` modules (`core/file.{c,h}`, `core/fragment.{c,h}`, `core/timeline.{c,h}`, `core/read.{c,h}`, `core/write.{c,h}`, `core/print.{c,h}`, `core/summary.{c,h}`, `core/meta.{c,h}`, `core/chapter.{c,h}`, `core/box.{c,h}`, and `core/box_default.{c,h}`), while preserving all public `lsmash_*` API symbols in `lsmash.h`.
  - Unified token-concatenation macros (`DEFINE_SIMPLE_BOX_ADDER_TEMPLATE_6`, `DEFINE_SIMPLE_SAMPLE_EXTENSION_ADDER`, `DEFINE_SIMPLE_BOX_REMOVER_TEMPLATE` in `core/box.c`, `ADD_BOX` in `core/read.c`, and `DEFINE_BOX_DEFAULT_CONSTANT` in `core/box_default.h`) to emit `lsm_box_add_<box>`, `static box_remove_<box>`, and `lsm_box_default_non_existing_<box>` directly without preprocessor shims.
  - Corrected historical identifier typos (`isom_timelime_*` $\rightarrow$ `lsm_timeline_*`, `isom_printer_destory_list` $\rightarrow$ `lsm_print_destroy_list`, `isom_pring_qt_color_table` $\rightarrow$ `print_qt_color_table`, `isom_print_sample_description_extesion` $\rightarrow$ `print_sample_description_extension`) and restricted `box_add_to_extension_list` and `box_get_entry` in `core/box.c` from undeclared external linkage to `static`.

### 3.7 `v2.23.0`: `codecs/` & `importer/` Symbol Naming Unification, FLAC-in-ISOBMFF (`fLaC`/`dfLa`), Chimera Architecture Documentation & Box Reader Hardening
- **Full `codecs/` & `importer/` Symbol Naming Unification (`lsm_<filename>_*` / `static <filename>_*`) & `static inline` Audit**:
  - Unified internal shared functions across `codecs/` (`lsm_a52_*`, `lsm_alac_*`, `lsm_av1_*`, `lsm_description_*`, `lsm_dts_*`, `lsm_h264_*`, `lsm_hevc_*`, `lsm_mp4a_*`, `lsm_mp4sys_*`, `lsm_mp4v_*`, `lsm_nalu_*`, `lsm_opus_*`, `lsm_qt_wfex_*`, `lsm_vc1_*`, `lsm_wma_*`) and file-local static helpers across `codecs/` and `importer/` (`static <filename>_*`, e.g., `a52_imp_*`, `adts_imp_*`, `als_imp_*`, `amr_imp_*`, `dts_imp_*`, `isobm_imp_*`, `ivf_imp_*`, `mp3_imp_*`, `nalu_imp_*`, `vc1_imp_*`, `wave_imp_*`), while preserving all public `lsmash_*` APIs and `liblsmash.ver` shared-library exports.
  - Audited `static inline` usage across the repository: removed unnecessary `inline` specifiers from large `.c` functions (`h264_update_picture_info_for_slice`, `hevc_update_picture_info_for_slice`, `av1_parse_superres_params`, `av1_compute_image_size`, `av1_get_q_index`, `dts_parse_corr_ext_substream`), converted single-line hot bit/byte helpers (`nalu_check_more_rbsp_data`, `av1_get_bit`) to `static inline`, and standardized `common/osdep.h` and `common/utils.h` header helpers to `static inline`.
- **ISOBMFF vs. QTFF Chimera Boundary & Core Architecture Comments**:
  - Added detailed technical background documentation directly in `core/box_default.{c,h}`, `core/box.{c,h}`, `common/list.{c,h}`, `core/stbl.c`, `core/fragment.c`, `core/read.c`, `core/write.c`, `codecs/description.c`, and `codecs/av1.c` explaining the `box_default` Null-Object Sentinel invariant, `extensions` vs. type-specific dual-list ownership, `lsm_list_t` vs. `lsmash_entry_list_t` trade-offs, `roll_group_number` 1-based index stability, `AudioSampleEntry` v0/v1/v2 (`stsd.version == 0` QTFF vs. `stsd.version == 1` ISOBMFF `AudioSampleEntryV1`), `colr` (`nclc` vs. `nclx`), `hdlr` (`mhlr`/`dhlr` vs. ISO), `meta` (ISO FullBox vs. iTunes `udta.meta` vs. QTFF BaseBox), and `ctts` v0 unsigned vs. v1/QTFF signed composition offsets.
- **Xiph FLAC-in-ISOBMFF (`fLaC` / `dfLa`) Support**:
  - Integrated FLAC-in-ISOBMFF codec and elementary stream importer support (`Author: XLWZ <xlwzforever@outlook.com>`) and adapted `codecs/flac.{c,h}`, `importer/flac_imp.c`, `codecs/description.c`, and `core/print.c` to current conventions (`lsm_flac_*`, `static flac_imp_*`, `lsm_list_t` paged block array, table-driven CRC-8, RFC 9639 uncommon 8/16-bit block size and sample rate frame header parsing, `dfLa` boxdumper printing, exact `STREAMINFO` recovery on demux, and `test_flac_isobmff_roundtrip`).
- **Upstream Fixes & Box Reader Hardening (`Makefile`, `core/file.c`, `core/read.c`, `core/print.c`, `codecs/hevc.c`, `tests/test_main.c`)**:
  - Fixed POSIX shared library installation permission in `Makefile` (`install -m 755 $(SHAREDLIB)`).
  - Integrated `lsmash_read_file()` file-size return value fix (`Author: Zhao Zhili <zhilizhao@tencent.com>`) and `chan` box `channelDescriptions` pointer advancement fix in `read_chan()` / `print_chan()` (`Author: Hiroyuki Hori <43978282+hi-hori@users.noreply.github.com>`).
  - Hardened `core/read.c` with a maximum box nesting depth guard (`ISOM_MAX_BOX_NESTING_LEVEL = 64`), child box size vs. parent boundary validation, FullBox header minimum size validation, `read_chan()` `numberChannelDescriptions` payload bounds check, `read_qt_color_table()` `uint32_t` loop index promotion, `read_trun()` / `read_tfra()` `box->size` loop guards, and `codecs/hevc.c` prefix SEI `nalUnitLength >= 3` check, accompanied by negative test cases in `tests/test_main.c`.
- **Documentation Categorization (`README.md`, `USAGE.md`, `SPEC.md`, `docs/specs/*.md`)**:
  - Reorganized `README.md` and `USAGE.md` to cleanly separate `Video Codecs`, `Audio Codecs` (including Xiph FLAC `fLaC`/`dfLa`), `Timed Text & Subtitles` (`tx3g`+`ftab` and QuickTime `text`), and `Sample Entry Extensions, HDR, Spatial Media & Metadata` (explicitly distinguishing Apple iTunes `meta`/`ilst`, QuickTime/iTunes reference chapter tracks `tref.chap`, and Nero `chpl` chapters).

### 3.8 `v2.23.1`: C11 Transition, Public Header C99/C++ Compatibility, Internal Header Cleanup & Coding Style Specification
- **C11 Transition, Public Header C99/C++ Compatibility & Internal Header Cleanup**:
  - Updated `configure` build standard to `-std=c11 -pedantic`, permitting C11 features (`_Static_assert`, anonymous structs/unions, `max_align_t`) across internal modules (`common/`, `core/`, `codecs/`, `importer/`, `cli/`, `tests/`), while strictly preserving C99 (`-std=c99 -pedantic`) and C++ compatibility for public header `lsmash.h`.
  - Added compile-time invariant validation using `_Static_assert` in `common/list.h` (`LSM_LIST_PAGE_SIZE` power-of-two check) and `core/box_default.c` (`ISOM_BASEBOX_COMMON` struct offset consistency check).
  - Audited all internal headers across `common/`, `core/`, `codecs/`, `importer/`, and `cli/`, removing redundant `#ifndef ... #define ... #endif` include guards in accordance with L-SMASH design conventions (reserving header guards strictly for public `lsmash.h`), and removed duplicate header inclusion in `importer/ivf_imp.c`.
  - Documented technical and historical background comments in `core/stbl.c` (`lsm_stbl_calculate_bitrate_description`) on `btrt`/`esds` 1-second sliding window consumption bitrate calculation.
  - Created standalone official coding guidelines in `docs/CODING_STYLE.md` and synchronized `SPEC.md`, `README.md`, and subsystem specifications.
