# Self-Contained Synthetic Test Suite Specification (`docs/specs/testing_strategy.md`)

## 1. Philosophy & Design Principles

Historically, L-SMASH lacked an automated, self-contained unit and integration test suite in the repository tree, relying instead on manual CLI testing (`muxer`, `remuxer`, `boxdumper`, `timelineeditor`) against external media files.

To guarantee **strict standard compliance (Strict International Standard Compliance)** without copyright, licensing, or binary-blob bloat in Git:
1. **100% Copyright-Free & Zero External Media Files**:
   - No `.mp4`, `.mov`, `.wav`, `.ivf`, or `.h264` binary asset files shall ever be committed to the repository or downloaded over the network.
   - Every test case programmatically synthesizes its own bitstreams (RIFF WAVE headers + LPCM waveforms, IVF headers + AV1 OBU bitstreams, and raw ISOBMFF/QTFF Box byte sequences) directly in C99 memory buffers or temporary files created and cleaned up during the test run.
2. **Deterministic, Bit-Exact Round-Trip Verification**:
   - Tests verify both **structural Box layout** (exact byte-for-byte serialized headers for `av1C`, `pcmC`, `chnl`, `srat`, `AudioSampleEntryV1`, `moof`/`traf`/`trun`) and **sample-level round-trip integrity** (muxing $\rightarrow$ reading $\rightarrow$ `lsmash_construct_timeline()` $\rightarrow$ `lsmash_get_sample_from_media_timeline()` $\rightarrow$ bit-exact payload, DTS, CTS, and sync-flag comparison).
3. **Negative & Boundary Testing (Malformed Input Resilience)**:
   - Synthetic byte generators intentionally construct malformed or out-of-spec boxes and bitstreams (e.g., `leb128 > UINT32_MAX`, `uvlc` with $\ge 32$ leading zeros, `pcmC` with invalid bit depths, truncated `chnl` or `av1C` boxes) to assert that L-SMASH rejects invalid data cleanly without buffer overflows, undefined behavior, or memory leaks.

---

## 2. Test Suite Architecture & Directory Layout

```
tests/
  test_main.c              # Lightweight C99 test runner & assertion harness
  test_util.h              # Bitstream/byte-buffer builders & temporary file helpers
  test_containers.c        # Subsystem: Paged Block Array, box_default, sbgp/chunk_list stability
  test_av1_obu.c           # Subsystem: Native AV1 OBU parser, uvlc/leb128, av1C, IVF round-trip
  test_lpcm_isobmff.c      # Subsystem: ISO/IEC 23003-5 ipcm/fpcm, pcmC, chnl, srat, Multi-Track Fragmented
  test_malformed_boxes.c   # Subsystem: Negative tests, boundary conditions, FullBox table verification
```

### 2.1 Build System, Toolchain & C99 Memory Audit Integration (`configure`, `Makefile`, `common/alloc.{c,h}`)
- **Verified Toolchain**: Read-Only MSYS2 MinGW64 toolchain (`mingw64/bin` and `usr/bin`: `gcc 14.2.0`, `GNU Make 4.4.1`, `sh`, `ar`, `ranlib`, `strip`). Package manager operations (`pacman`, `pacboy`) and writes outside the workspace remain strictly prohibited.
- **Test Target**: The `test` / `check` target in `Makefile` compiles all library and test translation units into `*.test.o` with **`-DLSMASH_DEBUG_ALLOC`** enabled, links `tests/test_main.exe`, and executes the test binary.
- **C99 Self-Contained Memory Audit Mechanism (`common/alloc.{c,h}`)**:
  - When compiled with `-DLSMASH_DEBUG_ALLOC`, all 6 allocation APIs (`lsmash_malloc`, `lsmash_malloc_zero`, `lsmash_realloc`, `lsmash_memdup`, `lsmash_free`, `lsmash_freep`) are wrapped by preprocessor macros capturing `__FILE__`, `__LINE__`, and the stringified call expression (`#size`, `#ptr`, `#ptrptr`).
  - Each allocation prepends a 64-byte aligned `lsm_alloc_header_t` (recording `size`, active intrusive doubly-linked list pointers `prev`/`next`, `file`, `line`, `arg`, and `front_canary = 0xCAFEBABE5A5A5A5AULL`) and appends an 8-byte unaligned `rear_canary = 0xDEADC0DE3C3C3C3CULL` immediately after the user payload.
  - Uninitialized memory from `lsmash_malloc` / `lsmash_realloc` growth is poisoned with `0xA5`, and freed blocks are poisoned with `0xDD` (`front_canary = 0xFEEEFEEEDEADBEEFULL`) to immediately expose Use-After-Free, Double-Free, Invalid-Free, and Heap Buffer Overrun bugs with `file(line)` + `arg` diagnostics and `abort()`.
  - `RUN_TEST` in `tests/test_main.c` invokes `lsm_alloc_verify_no_leaks()` after every individual test suite and at program exit, enforcing `active_count == 0 && active_bytes == 0`.
- All tests return exit code `0` on 100% pass and non-zero with file/line diagnostics on failure.

---

## 3. Detailed Test Cases by Mission

### 3.1 Suite 1: Memory Architecture & Container Invariants (`tests/test_containers.c`)

| Test ID | Target Component | Verification Description |
| :--- | :--- | :--- |
| `` | Paged Block Array (`common/`) | Append $1,000,000$ 4-byte (`isom_stsz_entry_t`) and 48-byte (`isom_sample_info_t`) elements across $\sim 1,000$ page boundaries; verify $O(1)$ random access `get(i)` for all $i \in [1, 10^6]$. |
| `` | Pointer Stability on Append | Record raw pointers to elements at index $1$, $1024$ (page boundary), and $1025$; append $500,000$ additional elements; assert recorded pointers remain valid and point to unmodified values. |
| `` | Tail Pop & Reuse (`PopTail`) | Push $5,000$ elements, pop $3,000$ elements from tail (`sgpd->list` / `chunk_list` pattern), push $4,000$ new elements; verify counts and element integrity. |
| `` | $O(1)$ Table Move (`stsz` $\rightarrow$ `stz2`) | Populate source table with $100,000$ entries, move to destination table (`isom_compress_sample_size_table` pattern); verify source is empty and destination retains all entries in $O(1)$ time. |
| `` | `box_default` Null-Object Sentinel Pattern & `extensions` | Create a Box hierarchy (`root` $\rightarrow$ `file` $\rightarrow$ `moov` $\rightarrow$ `trak` $\rightarrow$ `mdia` $\rightarrow$ `minf` $\rightarrow$ `stbl`), destroy child boxes via `isom_remove_box_by_itself()`, and verify singleton pointers automatically restore to `isom_non_existing_*()` sentinels (`LSMASH_IS_NON_EXISTING_BOX`) with zero double-frees. |
| `` | `sbgp` Roll Group Deduplication Stability | Simulate `isom_group_roll_recovery()` with multiple pending roll groups in `cache->roll.pool` where group $G_1$ is deduplicated via `isom_deduplicate_roll_group()` while group $G_2$ remains undetermined; verify $G_2$'s assignment in `sbgp->list` is not corrupted. |

---

### 3.2 Suite 2: Native AV1 Parser & `obuparse` Removal (`tests/test_main.c` — Implemented & Passing in the implementation)

| Test ID | Target Component | Verification Description |
| :--- | :--- | :--- |
| `` | `lsmash_bs_get_le64()` & `lsmash_bs_get_le32()` | Verify 32-bit and 64-bit little-endian reads across upper-word bit boundaries (regression test for `<< 32` shift fix in `common/bytes.c`). |
| `` | `av1_get_uvlc()` Bitstream Parser | Synthesize bitstreams encoding `uvlc()` values `0`, `1`, `2`, `7`, `65535`, `UINT32_MAX - 1`, and $\ge 32$ leading zeros; verify exact decoded values and bit consumption. |
| `` | `av1_get_leb128()` & `av1_show_leb128()` | Test 1-byte to 8-byte LEB128 encodings (including non-minimal padded LEB128 and values with bit 28–31 set to catch 32-bit signed shift UB), plus rejection of $> 8$ bytes or values $> \text{UINT32\_MAX}$. |
| `` | Sequence Header OBU (8/10/12-bit, BT.709 vs. sRGB) | Synthesize Sequence Header OBUs for Main Profile (8-bit BT.709 4:2:0, sRGB 4:4:4, 10-bit 4:2:0) and Professional Profile (**12-bit** `high_bitdepth=1, twelve_bit=1` 4:2:2); verify `av1C` fields (`high_bitdepth`, `twelve_bit`, `monochrome`, `chroma_subsampling_x/y`, `chroma_sample_position`) and `av1C` box creation/deserialization/copy round-trip. |
| `` | Frame Header OBU (`KEY`, `INTER`, `INTRA_ONLY`, `SWITCH`) | Synthesize Frame Headers with `enable_order_hint = 1`; verify `INTRA_ONLY_FRAME` and `INTER_FRAME` read 8-bit `refresh_frame_flags` (`f(8)`), whereas `SWITCH_FRAME` and `KEY_FRAME && show_frame` set `0xff` without consuming 8 bits. |
| `` | `show_existing_frame == 1` & Reference Updates | Synthesize a multi-frame GOP containing a `KEY_FRAME`, an `INTER_FRAME` (`show_frame = 0`, refreshing slot 2), and a `show_existing_frame = 1` frame referencing slot 2; verify reference slot updates, `OrderHint` propagation, and `ra_flags` assignment. |
| `` | Sample Assembly, Forbidden OBU Filtering & HDR10+ | Synthesize a Temporal Unit containing `OBU_TEMPORAL_DELIMITER`, `OBU_SEQUENCE_HEADER`, `OBU_METADATA` (ITU-T T.35 HDR10+), `OBU_PADDING`, and `OBU_FRAME`; verify `OBU_TEMPORAL_DELIMITER` and `OBU_PADDING` are stripped and `has_hdr10p == 1` is detected. |
| `` | Synthetic IVF Importer & ISOBMFF Round-Trip | Generate a synthetic 3-frame AV1 IVF stream, import via `ivf_imp`, mux into ISOBMFF (`mp41`/`isom`/`av01`), verify `[av01: Visual Description]` and `[av1C: AV1 Configuration Box]` via `lsmash_print_movie()`, and verify bit-exact sample payloads and sync flags from `lsmash_construct_timeline()`. |

---

### 3.3 Suite 3: ISO/IEC 23003-5 LPCM & Multi-Track Fragmented ISOBMFF (`tests/test_main.c` — Implemented & Passing in the implementation)

| Test ID | Target Component | Verification Description |
| :--- | :--- | :--- |
| `` | `common/alloc.{c,h}` Memory Audit (`test_memory_audit_allocator`) | Verify `#ptr`/`#size` expression capture, `0xA5` uninitialized poisoning, zero-initialization, `lsmash_realloc` growth/shrinkage, `lsmash_memdup`, `lsmash_freep`, and active block/byte tracking across all 6 allocation macros. |
| `` | `fullbox_type_table` Capacity & Registration | Verify `isom_is_fullbox()` returns `1` for `pcmC`, `chnl`, `srat`, and all 60+ existing FullBoxes, and `0` for non-FullBoxes (`wave`, `frma`, `enda`, `mdat`, `av1C`). |
| `` | `ipcm` & `pcmC` Box Round-Trip (16/24/32-bit, LE/BE) | Mux and demux `ipcm` tracks across bit depths (`16`, `24`, `32`) and endianness (`pcm_sample_endian = 0` BE and `1` LE); verify 14-byte `pcmC` binary layout, structured/unstructured conversion, strict standard compliance invalid config rejection, and bit-exact PCM sample data. |
| `` | `fpcm` & `pcmC` Box Round-Trip (32/64-bit IEEE 754 Float) | Mux and demux `fpcm` tracks with 32-bit and 64-bit IEEE 754 floating-point samples (LE and BE); verify `pcmC` and bit-exact sample reconstruction. |
| `` | High Sample Rate (`> 65535` Hz) `AudioSampleEntryV1` + `srat` | Mux 96 kHz and 192 kHz `ipcm`/`fpcm` tracks; verify `stsd->version == 1`, `audio->version == 1` (**without** spurious 16-byte QTFF v1 fields!), and `srat->sampling_rate == 96000` / `192000`. |
| `` | `chnl` (`ChannelLayoutBox`) Defined & Explicit Layouts | Test `chnl` serialization/deserialization/printing for Mono (`definedLayout = 1`), Stereo (`definedLayout = 2`), 5.1 surround (`definedLayout = 6`), 7.1 surround (`definedLayout = 12`), `objectStructured` (`object_count = 2`), and custom speaker positions (`definedLayout = 0` including `speaker_position == 126` with explicit `azimuth = -135` and `elevation = 45`). |
| `` | Multi-Track Fragmented ISOBMFF (`test_multitrack_fragmented_lpcm_roundtrip`) | Mux a 2-track 5.1ch Fragmented ISOBMFF movie (`tid1`: 6ch 24-bit `ipcm` @ 48 kHz; `tid2`: 6ch 32-bit `fpcm` @ 96 kHz) across multiple fragments (`moof`/`traf`/`trun`/`mdat`); verify: <br>1. **Zero `trun->optional` rows** (`trun->optional == NULL`, `(trun->flags & 0x000f00) == 0`) on **both** Track 1 and Track 2 (`trun->flags == 0x000001`).<br>2. `lsmash_construct_timeline()` reconstructs all 400 + 800 frames via `timeline->bunch_list` with bit-exact payloads and timestamps. |
| `` | Synthetic WAVE Importer (`test_wave_importer_isobmff_lpcm`) | Synthesize `WAVE_FORMAT_EXTENSIBLE` (24-bit 5.1ch PCM, `dwChannelMask = 0x3F`) and `WAVE_FORMAT_IEEE_FLOAT` (32-bit 96 kHz stereo float) `.wav` files, import via `wave_imp`, mux into ISOBMFF (`ipcm` + `chnl` `definedLayout=6` and `fpcm` + `srat` `96000`), and verify bit-exact round-trip. |

---

### 3.4 Suite 4: Malformed Box & strict standard compliance Strict Validation (`tests/test_malformed_boxes.c`)

| Test ID | Target Component | Verification Description |
| :--- | :--- | :--- |
| `` | strict standard compliance Brand Separation | Verify that attempting to mux QTFF-only LPCM (`lpcm`/`sowt`/`twos`) into a pure ISOBMFF Fragmented container without QT compatibility is rejected, and `ipcm`/`fpcm` is rejected in legacy QTFF-only mode. |
| `` | Malformed `pcmC` & `chnl` | Feed truncated `pcmC` (`size < 14`), non-zero reserved bits in `pcmC->format_flags`, or truncated `chnl` speaker arrays to `isom_read_box()`; verify graceful error handling without memory leaks. |
| `` | Malformed `av1C` & OBU Streams | Feed truncated `av1C` (`size < 12`) and corrupted OBU size fields (`obu_size` exceeding Temporal Unit length); verify clean error return and zero leaks. |
