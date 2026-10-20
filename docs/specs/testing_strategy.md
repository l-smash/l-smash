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

### 2.1 Build System & Toolchain Integration (`configure` & `Makefile`)
- **Verified Toolchain**: Read-Only MSYS2 MinGW64 toolchain (`mingw64/bin` and `usr/bin`: `gcc 14.2.0`, `GNU Make 4.4.1`, `sh`, `ar`, `ranlib`, `strip`). Package manager operations (`pacman`, `pacboy`) and writes outside the workspace remain strictly prohibited.
- **Test Target**: Add a `test` / `check` target in `Makefile` that compiles `tests/*.c` against `liblsmash.a` (or internal object files `$(OBJS)` when testing non-exported internal functions such as `lsmash_table_*`, `isom_is_fullbox`, or `av1_parse_*`) and executes the test binary.
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

### 3.2 Suite 2: Native AV1 Parser & `obuparse` Removal (`tests/test_av1_obu.c`)

| Test ID | Target Component | Verification Description |
| :--- | :--- | :--- |
| `` | `av1_get_uvlc()` Bitstream Parser | Synthesize bitstreams encoding `uvlc()` values `0`, `1`, `2`, `7`, `65535`, `UINT32_MAX - 1`, and $\ge 32$ leading zeros; verify exact decoded values and bit consumption. |
| `` | `av1_get_leb128()` & `av1_show_leb128()` | Test 1-byte to 8-byte LEB128 encodings (including non-minimal padded LEB128 and values with bit 28–31 set to catch 32-bit signed shift UB), plus rejection of $> 8$ bytes or values $> \text{UINT32\_MAX}$. |
| `` | Sequence Header OBU (8/10/12-bit & Chroma) | Synthesize Sequence Header OBUs for Main Profile (8-bit, 10-bit 4:2:0), High Profile (4:4:4), and Professional Profile (**12-bit** `high_bitdepth=1, twelve_bit=1` 4:2:2 and Monochrome); verify `av1C` fields (`high_bitdepth`, `twelve_bit`, `monochrome`, `chroma_subsampling_x/y`, `chroma_sample_position`). |
| `` | Frame Header OBU (`KEY`, `INTER`, `INTRA_ONLY`, `SWITCH`) | Synthesize Frame Headers with `enable_order_hint = 0` and `1`; verify `INTRA_ONLY_FRAME` and `INTER_FRAME` read 8-bit `refresh_frame_flags` (`f(8)`), whereas `SWITCH_FRAME` and `KEY_FRAME && show_frame` set `0xff` without consuming 8 bits. |
| `` | `show_existing_frame == 1` & Reference Updates | Synthesize a multi-frame GOP containing a forward `KEY_FRAME` (`show_frame = 0`) followed by inter frames and a `show_existing_frame = 1` frame referencing the forward keyframe; verify reference slot updates, POC (`order_hint`) calculation, and `rap ` sample group assignment. |
| `` | Sample Assembly & Forbidden OBU Filtering | Synthesize a Temporal Unit containing `OBU_TEMPORAL_DELIMITER`, `OBU_SEQUENCE_HEADER`, `OBU_METADATA`, `OBU_PADDING`, and `OBU_FRAME` (both with `obu_has_size_field = 1` and `0`); verify `OBU_TEMPORAL_DELIMITER` and `OBU_PADDING` are stripped and all output OBUs have `obu_has_size_field = 1`. |
| `` | Synthetic IVF Importer & ISOBMFF Round-Trip | Generate an in-memory/temp IVF stream with a 10-frame AV1 sequence, import via `ivf_imp`, mux into ISOBMFF (`mp42`/`isom`/`av01`), read back `av1C` and timeline samples, and verify bit-exact sample payloads and timestamps. |

---

### 3.3 Suite 3: ISO/IEC 23003-5 LPCM & Multi-Track Fragmented ISOBMFF (`tests/test_lpcm_isobmff.c`)

| Test ID | Target Component | Verification Description |
| :--- | :--- | :--- |
| `` | `fullbox_type_table` Capacity & Registration | Verify `isom_is_fullbox()` returns `1` for `pcmC`, `chnl`, `srat`, and all 60+ existing FullBoxes, and `0` for non-FullBoxes (`wave`, `frma`, `enda`, `mdat`, `av1C`). |
| `` | `ipcm` & `pcmC` Box Round-Trip (16/24/32-bit, LE/BE) | Mux and demux `ipcm` tracks across all 6 combinations of bit depth (`16`, `24`, `32`) and endianness (`pcm_sample_endian = 0` BE and `1` LE) at 48 kHz; verify 14-byte `pcmC` binary layout and bit-exact PCM sample data. |
| `` | `fpcm` & `pcmC` Box Round-Trip (32/64-bit IEEE 754 Float) | Mux and demux `fpcm` tracks with 32-bit and 64-bit IEEE 754 floating-point samples (LE and BE); verify `pcmC` and bit-exact sample reconstruction. |
| `` | High Sample Rate (`> 65535` Hz) `AudioSampleEntryV1` + `srat` | Mux 96 kHz and 192 kHz `ipcm`/`fpcm` tracks; verify `stsd->version == 1`, `audio->version == 1` (**without** spurious 16-byte QTFF v1 fields!), and `srat->sampling_rate == 96000` / `192000`. |
| `` | `chnl` (`ChannelLayoutBox`) Defined & Explicit Layouts | Test `chnl` serialization/deserialization for stereo (`definedLayout = 2`), 5.1 surround (`definedLayout = 6`), `omittedChannelsMap`, and custom speaker positions (`definedLayout = 0` including `speaker_position == 126` with explicit `azimuth` and `elevation`). |
| `` | Multi-Track Fragmented ISOBMFF (`AV1` + `ipcm` / Dual `ipcm` + `fpcm`) | Mux a multi-track Fragmented ISOBMFF movie (`ftyp` + `moov`/`mvex` + multiple `sidx`/`moof`/`traf`/`trun`/`mdat` segments + `mfra`/`tfra`) combining video and `ipcm`/`fpcm` audio; verify: <br>1. **Zero `trun->optional` rows** are allocated/written for constant-frame-size LPCM `trun` boxes (`trun->flags == 0x000001`).<br>2. `lsmash_construct_timeline()` reconstructs both tracks across fragments (`timeline->info_list` and `timeline->bunch_list`) and extracts bit-exact samples. |
| `` | Synthetic WAVE Importer (`wave_imp`) to Fragmented ISOBMFF | Generate synthetic `.wav` streams (`WAVE_FORMAT_PCM`, `WAVE_FORMAT_IEEE_FLOAT`, `WAVE_FORMAT_EXTENSIBLE` 24-bit 5.1), import via `wave_imp`, and mux into Fragmented ISOBMFF (`ipcm`/`fpcm`). |

---

### 3.4 Suite 4: Malformed Box & strict standard compliance Strict Validation (`tests/test_malformed_boxes.c`)

| Test ID | Target Component | Verification Description |
| :--- | :--- | :--- |
| `` | strict standard compliance Brand Separation | Verify that attempting to mux QTFF-only LPCM (`lpcm`/`sowt`/`twos`) into a pure ISOBMFF Fragmented container without QT compatibility is rejected, and `ipcm`/`fpcm` is rejected in legacy QTFF-only mode. |
| `` | Malformed `pcmC` & `chnl` | Feed truncated `pcmC` (`size < 14`), non-zero reserved bits in `pcmC->format_flags`, or truncated `chnl` speaker arrays to `isom_read_box()`; verify graceful error handling without memory leaks. |
| `` | Malformed `av1C` & OBU Streams | Feed truncated `av1C` (`size < 12`) and corrupted OBU size fields (`obu_size` exceeding Temporal Unit length); verify clean error return and zero leaks. |
