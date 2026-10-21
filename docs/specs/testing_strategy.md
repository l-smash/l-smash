# Self-Contained Synthetic Testing Strategy (`docs/specs/testing_strategy.md`)

## 1. Overview & Principles

L-SMASH provides a self-contained C11 unit and integration test suite (`tests/test_main.c`) executed via `make test`:
1. **Zero External Test Media Dependencies**:
   - All test bitstreams—including AV1 Open Bitstream Units (OBUs), IVF containers, RIFF WAVE (`WAVE_FORMAT_PCM`, `WAVE_FORMAT_IEEE_FLOAT`, `WAVE_FORMAT_EXTENSIBLE`) files, standard and Fragmented MP4 movies, and binary-patched malformed containers—are synthesized programmatically in memory or temporary files and cleaned up upon completion.
2. **Active C11 Memory Audit (`-DLSMASH_DEBUG_ALLOC`)**:
   - The `tests/test_main` binary is compiled with `-DLSMASH_DEBUG_ALLOC` and linked against `liblsmash.a`.
   - Every test suite is wrapped in the `RUN_TEST( test_fn )` macro, which executes `lsm_alloc_verify_no_leaks()` and asserts `lsm_alloc_get_active_count() == 0 && lsm_alloc_get_active_bytes() == 0` immediately after each suite completes.
3. **Positive & Negative Coverage**:
   - Tests verify both valid specification-compliant multiplexing/demultiplexing round-trips and negative error-handling paths (truncated boxes, invalid bit depths, out-of-bounds table indices, and malformed bitstreams), confirming 100% memory rollback on error.

---

## 2. Test Suite Architecture (`tests/test_main.c`)

Running `make test` executes **15 test suites** (147,264 assertions):

| Suite # | Function Name | Subsystem & Verification Scope |
| :--- | :--- | :--- |
| **1** | `test_memory_audit_allocator` | Verifies `-DLSMASH_DEBUG_ALLOC` intrusive headers, 16-byte user pointer alignment, `0xCD` uninitialized memory poisoning, `lsmash_malloc_zero`, `lsmash_memdup`, `lsmash_realloc`, `lsmash_free`, `lsmash_freep`, and `lsm_alloc_verify_no_leaks()`. |
| **2** | `test_paged_block_array` | Verifies `lsm_list_t` (`common/list.{c,h}`): zero-initialized `{ 0 }` and `NULL` container safety, 10,500-element append across 42 pages of 256 elements, $O(1)$ random access (`lsm_list_at` / `lsm_list_get`), 100% element pointer stability across page allocations, `LSM_LIST_FOR_EACH` with `continue`/`break`, `lsm_list_cursor_t` forward/backward traversal across page boundaries, tail removal, cross-page mid-list removal (`lsm_list_remove`), `lsm_list_move`, and custom eliminator cleanup. |
| **3** | `test_large_scale_paged_tables_and_roll_dedup` | End-to-end stress test of `lsm_list_t` inside `core/`:<br>- **10,500-sample VFR movie** (42 pages of `stts`, `ctts`, `stsz`, `sdtp`) with 35 cycles of 30 consecutive post-roll recovery samples (`roll_distance = +2`), verifying `stbl_deduplicate_roll_group()` (`core/stbl.c`) mid-table removal without pointer invalidation.<br>- **600-sample `stz2` compact sample size table** (`field_size = 4` bits) exercising `lsm_stbl_compress_sample_size_table()` (`core/stbl.c`) and `lsm_list_move()`.<br>- **900-sample Fragmented VFR & DASH Segment (`sidx` / `tfra`) movies** (3 fragments $\times$ 300 samples/fragment) crossing page boundaries in `fragment->pool` and `trun->optional`. |
| **4** | `test_bitstream_primitives` | Verifies `lsmash_bs_get_le64` 64-bit shift (`<< 32`), `lsm_av1_get_uvlc` (standard values, max valid `0xFFFFFFFE`, `leadingZeros >= 32` rejection, and truncated all-zero EOF non-hang), and `lsm_av1_get_leb128` / `lsm_av1_show_leb128` (1-byte, 5-byte `UINT32_MAX`, 8-byte padded `UINT32_MAX`, `> UINT32_MAX` overflow rejection, `> 8` bytes rejection, and truncated EOF rejection). |
| **5** | `test_sequence_header_and_av1c` | Verifies `lsm_av1_parse_sequence_header` and `av1C` box creation/reconstruction across 8-bit Main Profile (4:2:0 BT.709, confirming `color_config` does not misclassify BT.709 as sRGB 4:4:4) and 12-bit Professional Profile (Profile 2, `high_bitdepth = 1, twelve_bit = 1`, 4:2:2). |
| **6** | `test_frame_headers_and_tu_assembly` | Verifies AV1 Temporal Unit assembly (`lsm_av1_parse_temporal_unit`): `OBU_TEMPORAL_DELIMITER` and `OBU_PADDING` stripping, ITU-T T.35 HDR10+ `OBU_METADATA` detection and `configOBUs` inclusion, `KEY_FRAME` / `INTER_FRAME` / `INTRA_ONLY_FRAME` / `SWITCH_FRAME` parsing, 8-bit `refresh_frame_flags` slot updates, `show_existing_frame = 1` pointing to a `KEY_FRAME` refreshing all 8 reference slots, and `enable_order_hint = 0` streams. |
| **7** | `test_av1_multitile_and_sync_sample_compliance` | Verifies AV1-ISOBMFF v1.2.0 and multi-tile compliance:<br>- `SYNC` (`stss`) when `OBU_SEQUENCE_HEADER` precedes `KEY_FRAME (show_frame=1)` in the same TU vs. `OPEN_RAP` when `OBU_SEQUENCE_HEADER` is absent or appears after the first frame.<br>- Multi-tile $2 \times 2$ (`NumTiles = 4`) `OBU_FRAME` and split `OBU_FRAME` (tiles $0..1$) + `OBU_REDUNDANT_FRAME_HEADER` (stripped) + `OBU_TILE_GROUP` (tiles $2..3$).<br>- Rejection of incomplete multi-tile frames and discontinuous `tg_start`, stripping of reserved OBU types, and rejection of `OBU_TILE_LIST`. |
| **8** | `test_ivf_mux_demux_roundtrip` | Synthesizes a 3-frame AV1 IVF file, imports via `importer/ivf_imp.c`, muxes to MP4, verifies `[av01]` and `[av1C]` box dump formatting (`lsmash_print_movie`), and verifies bit-exact sample payload and sync-flag reconstruction via the media timeline. |
| **9** | `test_av1_fragmented_isobmff_roundtrip` | Synthesizes a 6-TU AV1 IVF stream combining `SYNC` keyframes, split multi-tile inter frames, non-displayed ARF + `show_existing_frame = 1` compound TUs, `KEY_FRAME` without in-band `OBU_SEQUENCE_HEADER` (`OPEN_RAP`), and `INTRA_ONLY_FRAME` (`OPEN_RAP`), muxes across an initial `moov` and 2 `moof` fragments, and verifies `stss`, `trun` sample flags, `traf` `rap ` sample groups, and timeline reconstruction. |
| **10** | `test_lpcm_pcmc_chnl_srat_roundtrip` | Verifies ISO/IEC 23003-5 `ipcm`/`fpcm` and `pcmC` (`16`/`24`/`32`-bit BE/LE integer and `32`/`64`-bit BE/LE IEEE 754 float), ISO/IEC 14496-12 `chnl` (`definedLayout` CICP, explicit `speaker_position = 126` with `azimuth`/`elevation`, and `object_count`), and `AudioSampleEntryV1` + `srat` for $96\text{ kHz}$ and $192\text{ kHz}$ tracks, plus rejection of missing `pcmC` or invalid sample sizes. |
| **11** | `test_multitrack_fragmented_lpcm_roundtrip` | Muxes two 5.1ch LPCM tracks (`ipcm` 24-bit @ $48\text{ kHz}$ and `fpcm` 32-bit @ $96\text{ kHz}$) across 2 movie fragments, verifying multi-track `trun` `data_offset` placement, `tfhd` default duration/size hoisting, `trun->optional == NULL` compaction, and bit-exact timeline demultiplexing. |
| **12** | `test_wave_importer_isobmff_lpcm` | Synthesizes a 6-channel 24-bit $48\text{ kHz}$ `WAVE_FORMAT_EXTENSIBLE` (`dwChannelMask = 0x3F`) WAV file and a 2-channel 32-bit float $96\text{ kHz}$ `WAVE_FORMAT_IEEE_FLOAT` WAV file, imports both via `importer/wave_imp.c`, muxes into ISOBMFF MP4, and verifies automatic conversion to `ipcm`/`fpcm` with `pcmC`, `chnl`, and `srat`. |
| **13** | `test_malformed_boxes_and_sample_tables` | Negative testing of malformed boxes and sample tables with zero-leak rollback verification:<br>- `stsc` with invalid 1-based `first_chunk = 0`<br>- Truncated `stsz` claiming `sample_count = 500` in a 4-entry box<br>- `sbgp` with out-of-bounds `group_description_index = 99`<br>- `pcmC` with invalid `PCM_sample_size = 13`<br>- Truncated `emsg` box testing `pos > box->size` underflow guard and `scheme_id_uri`/`value` cleanup<br>- Truncated `sbgp` box ending mid-table<br>- Excessive box nesting depth ($> 64$ levels) rejected with `LSMASH_ERR_INVALID_DATA`<br>- Child box size exceeding parent box boundary rejected with `LSMASH_ERR_INVALID_DATA`<br>- Truncated `chan` box claiming `0x10000000` channel descriptions rejected with `LSMASH_ERR_INVALID_DATA` |
| **14** | `test_multibuf_resize_and_codec_regressions` | Regression suite for `lsmash_multiple_buffers_t` and codec/metadata fixes:<br>- `lsmash_resize_multiple_buffers` expansion ($32 \rightarrow 96$ bytes), shrinkage ($96 \rightarrow 20$ bytes), data preservation across all 4 buffers, zero-initialized expansion tail, and overflow rejection.<br>- `lsm_nalu_get_codeNum` Exp-Golomb decoding, EOF termination, and $\ge 64$ leading zero bits non-hang.<br>- MP3 `nBlockSize` calculation ($144 \times 128000 / 48000 = 384$) and `cbSize < 12` rejection in `lsm_qt_wfex_update_bitrate`.<br>- WMA 16-bit LE `cbSize` reading and truncated box rejection in `lsm_wma_print_codec_specific`.<br>- Opus 8-channel `channel_order[OutputChannelCount - 1]` lookup and `"front center"` output in `lsm_opus_print_codec_specific`.<br>- `lsmash_count_tyrant_chapter` and custom iTunes metadata (`ITUNES_METADATA_ITEM_CUSTOM` string & integer) retrieval round-trip. |
| **15** | `test_flac_isobmff_roundtrip` | Synthesizes a valid RFC 9639 FLAC stream (`fLaC` magic, `STREAMINFO` + `VORBIS_COMMENT` metadata blocks, and CRC-8-valid fixed and uncommon block-size audio frames), tests `lsm_flac_verify_crc8`, `lsm_flac_parse_streaminfo`, `lsm_flac_parse_frame_header` (plus negative CRC-8 corruption and reserved block-size rejection), imports via `importer/flac_imp.c`, muxes into ISOBMFF (`fLaC` + `dfLa`), verifies `[fLaC]` and `[dfLa]` boxdumper output, and verifies exact `STREAMINFO` and sample duration/payload recovery on demux. |

---

## 3. Running the Test Suite

```sh
./configure
make -j8 all test
```

Expected output:
```text
=== L-SMASH Synthetic Test Suite ===
[PASS] test_memory_audit_allocator
[PASS] test_paged_block_array
[PASS] test_large_scale_paged_tables_and_roll_dedup
[PASS] test_bitstream_primitives
[PASS] test_sequence_header_and_av1c
[PASS] test_frame_headers_and_tu_assembly
[PASS] test_av1_multitile_and_sync_sample_compliance
[PASS] test_ivf_mux_demux_roundtrip
[PASS] test_av1_fragmented_isobmff_roundtrip
[PASS] test_lpcm_pcmc_chnl_srat_roundtrip
[PASS] test_multitrack_fragmented_lpcm_roundtrip
[PASS] test_wave_importer_isobmff_lpcm
[PASS] test_malformed_boxes_and_sample_tables
[PASS] test_multibuf_resize_and_codec_regressions
[PASS] test_flac_isobmff_roundtrip
=== Assertions run: 147264, Failures: 0 ===
```
