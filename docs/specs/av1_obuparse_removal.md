# Native AV1 Parser Salvage & Complete `obuparse` Removal Specification (`docs/specs/av1_obuparse_removal.md`)

## 1. Executive Summary & Mandate

In the `vimeo` fork (`dev` branch head `4a21f21`), AV1 support was added via commits `ff5e5fe`, `9f53585`, `590352d`, and `d98636c` by linking an external third-party library (`-lobuparse`, `#include <obuparse.h>`) inside `codecs/av1_obu.{c,h}` and `importer/ivf_imp.c`.

Meanwhile, on the official L-SMASH repository's **`av1` branch** (`origin/av1` / local `av1`, commits `2bd228c` and `96065d0` branched from `master` `18a9ed2`), original author Yusuke Nakamura implemented a , ISC-licensed native AV1 OBU parser in `codecs/av1.{c,h}` (1,747 lines) and a native IVF importer in `importer/ivf_imp.c` (413 lines).

### 1.1 Strict Policy
- **Zero Code Reuse from `obuparse`**: `codecs/av1_obu.c` and `codecs/av1_obu.h` must be completely deleted. No logic, data structures, or code from `obuparse` or `codecs/av1_obu.{c,h}` may be copied into the native implementation.
- **100% Native L-SMASH Bitstream Engine**: All AV1 OBU header, Sequence Header OBU, Metadata OBU, Frame Header OBU, Tile Group OBU, and Temporal Unit (TU) assembly logic must derive exclusively from the official `av1` branch (`2bd228c`, `96065d0`) and the official **AOMedia AV1 Bitstream & Decoding Process Specification** and **AV1 Codec ISO Media File Format Binding Specification**.

---

## 2. Audit of Current `obuparse` Dependencies on `dev`

| File | Lines | `obuparse` Dependency Details | Action |
| :--- | :--- | :--- | :--- |
| `configure` | `119`, `318` | Hardcodes `LIBS="$LIBS -lm -lobuparse"` and compiles `codecs/av1_obu.c` into `SRCS`. | Remove `-lobuparse` and `codecs/av1_obu.c`. |
| `codecs/av1_obu.h` | `1–46` | `#include <obuparse.h>`, defines `ObuAv1SampleState` embedding `OBPState` and `OBPSequenceHeader`. | **Delete file (`git rm`)**. |
| `codecs/av1_obu.c` | `1–315` | `#include <obuparse.h>`, calls `obp_get_next_obu()`, `obp_parse_sequence_header()`, `obp_parse_frame_header()`, `obp_parse_frame()`, `obp_parse_tile_group()`, `obp_parse_metadata()`. | **Delete file (`git rm`)**. |
| `importer/ivf_imp.c`| `34`, `172`, `362` | `#include <obuparse.h>`, `#include "codecs/av1_obu.h"`, calls `obu_av1_parse_first_tu()` and `obu_av1_assemble_sample()`. | Replace with native `av1` branch importer (`96065d0`) + multi-frame TU assembly. |

### 2.1 Bugs Identified in `dev`'s `codecs/av1_obu.c` (For Historical Record)
1. **12-bit AV1 Corruption (`codecs/av1_obu.c:81-91`)**: `obu_av1_parse_first_tu()` populated `param->high_bitdepth = seq.color_config.high_bitdepth` and `param->monochrome = seq.color_config.mono_chrome`, but **completely forgot to copy `param->twelve_bit = seq.color_config.twelve_bit;`**, leaving `twelve_bit` as `0` in `av1C` for all 12-bit AV1 streams.
2. **Memory Leak on Missing Sequence Header (`codecs/av1_obu.c:290`)**: `obu_av1_assemble_sample()` allocated `uint8_t *samplebuf = lsmash_malloc(tu_buf_len)` at line 142 and returned `LSMASH_ERR_INVALID_DATA` at line 290 without freeing `samplebuf` if `!sstate->seen_seq`.

---

## 3. Comparative Audit of Official `av1` Branch (`2bd228c`, `96065d0`) vs. `dev`

### 3.1 Architectural Strengths of the Official `av1` Branch (`2bd228c` & `96065d0`)
The native implementation in `2bd228c` (`codecs/av1.c`, `codecs/av1.h`) and `96065d0` (`importer/ivf_imp.c`) follows the exact same state-machine and `lsmash_multiple_buffers_t` architecture as `codecs/h264.c` and `codecs/hevc.c`:
- **Complete Native OBU Parser (`codecs/av1.c`)**:
  - `av1_check_obu_header()`: Parses `obu_forbidden_bit`, `obu_type`, `obu_extension_flag`, `obu_has_size_field`, `obu_reserved_1bit`, `temporal_id`, `spatial_id`, and `obu_size` (via `leb128()`).
  - `av1_parse_sequence_header()`: Full parser for `sequence_header_obu()`, including `operating_points_cnt_minus_1`, `timing_info()`, `decoder_model_info()`, `OperatingPoint` parameters, `max_frame_width_minus_1`, `max_frame_height_minus_1`, `frame_id_numbers_present_flag`, `enable_superres`, `enable_order_hint`, `seq_force_screen_content_tools`, `OrderHintBits`, `color_config()` (including `high_bitdepth`, `twelve_bit`, `mono_chrome`, `subsampling_x`, `subsampling_y`, `chroma_sample_position`), and `film_grain_params_present`.
  - `av1_parse_frame_header()` / `av1_uncompressed_header()`: Parses `frame_header_obu()` and updates reference frame state (`RefValid`, `RefFrameId`, `RefUpscaledWidth`, `RefFrameWidth`, `RefFrameHeight`, `RefRenderWidth`, `RefRenderHeight`, `RefFrameType`) via `av1_update_reference_frames()`.
  - `av1_calculate_poc()`: Computes relative display order (`OrderHint` / POC) using `av1_get_relative_dist()`.
- **Clean `av1C` Box Implementation on `dev` (`codecs/av1.c`)**:
  - On `dev`, `codecs/av1.c` (266 lines) contains the completed `av1C` Box reader (`isom_read_av1C`), writer (`isom_write_av1C`), printer (`isom_print_av1C`), `lsmash_create_av1_specific_info()`, and `isom_setup_av1_specific_parameters_from_box()`.

### 3.2 Bitstream & Logic Bugs Discovered in Official `av1` Branch (`2bd228c`)
During our line-by-line audit of `2bd228c:codecs/av1.c` against the AOMedia AV1 Bitstream Specification, we identified **four critical bugs** in the WIP code that must be fixed when integrating it into `dev`:

#### Bug 1: `av1_get_uvlc()` Reads `leadingZeros` Bits Instead of `1` Bit Per Iteration (`2bd228c:codecs/av1.c:248-260`)
In `2bd228c:codecs/av1.c`:
```c
static uint32_t av1_get_uvlc( lsmash_bits_t *bits )
{
    uint32_t leadingZeros = 0;
    while( !lsmash_bits_get( bits, leadingZeros ) )
        ++leadingZeros;
    if( leadingZeros >= 32 )
        return UINT32_MAX;
    uint32_t value = lsmash_bits_get( bits, leadingZeros );
    return value + (1UL << leadingZeros) - 1;
}
```
- **Defect A**: On the first iteration, `leadingZeros == 0`, so `lsmash_bits_get(bits, 0)` reads **0 bits** and returns `0`! On subsequent iterations, it reads 1 bit, then 2 bits, then 3 bits instead of 1 bit per iteration!
- **Defect B**: Per AV1 Spec Section 4.10.3 (`uvlc()`):
  ```c
  leadingZeros = 0
  while ( 1 ) {
      done = read_bit()
      if ( done )
          break
      leadingZeros++
  }
  if ( leadingZeros >= 32 ) {
      return ( 1 << 32 ) - 1
  }
  value = f( leadingZeros )
  return value + ( 1 << leadingZeros ) - 1
  ```
- **Required Fix**:
  ```c
  static uint32_t av1_get_uvlc( lsmash_bits_t *bits )
  {
      uint32_t leadingZeros = 0;
      while( !lsmash_bits_get( bits, 1 ) )
          ++leadingZeros;
      if( leadingZeros >= 32 )
          return UINT32_MAX;
      uint32_t value = lsmash_bits_get( bits, leadingZeros );
      return value + (1U << leadingZeros) - 1U;
  }
  ```

#### Bug 2: `av1_get_leb128()` and `av1_show_leb128()` 32-Bit Signed Shift Undefined Behavior (`2bd228c:codecs/av1.c:276, 301`)
In `2bd228c:codecs/av1.c`:
```c
uint64_t value = 0;
for( int i = 0; i < 8; i++ )
{
    uint8_t leb128_byte = lsmash_bs_get_byte( bs );
    value |= ((leb128_byte & 0x7f) << (i * 7));
    if( !(leb128_byte & 0x80) )
        break;
}
```
- **Defect**: `(leb128_byte & 0x7f)` undergoes C99 integer promotion to 32-bit signed `int`. When `i >= 4`, `i * 7 >= 28`, shifting into or past the sign bit of 32-bit `int` (Undefined Behavior in C99). Furthermore, AV1 Spec Section 4.10.5 mandates that `value <= (1ULL << 32) - 1` (`UINT32_MAX`) and the 8th byte (`i == 7`) must not have its MSB (`0x80`) set.
- **Required Fix**: Cast `(uint64_t)(leb128_byte & 0x7f) << (i * 7)` and validate `value <= UINT32_MAX`.

#### Bug 3: Three Bitstream Syntax Bugs in `av1_uncompressed_header()` (`2bd228c:codecs/av1.c:1122, 1175, 1203`)
1. **`show_existing_frame == 1` Incomplete Parsing (`line 1122`)**:
   - In `2bd228c`, when `fh->show_existing_frame == 1`, `av1_uncompressed_header()` immediately returned `0` without reading `frame_to_show_map_idx` (`f(3)`), `temporal_point_info()` (when `sh->decoder_model_info_present_flag && !sh->timing_info.equal_picture_interval`), or `display_frame_id` (`f(idLen)` when `sh->frame_id_numbers_present_flag`).
   - Crucially, per AV1 Spec Section 5.9.2, when `show_existing_frame == 1` and `RefFrameType[frame_to_show_map_idx] == AV1_FRAME_TYPE_KEY` (e.g., an forward keyframe shown via `show_existing_frame`), `refresh_frame_flags` becomes `allFrames` (`0xff`), `fh->order_hint = RefOrderHint[frame_to_show_map_idx]`, and reference frame state (`av1_update_reference_frames()`) must be updated!
2. **Unconditional `order_hint` Read (`line 1175`)**:
   - `2bd228c` executed `fh->order_hint = lsmash_bits_get( bits, sh->OrderHintBits );` unconditionally. When `sh->enable_order_hint == 0`, `OrderHintBits` is `0`, or if uninitialized, must not read bits. Guard with `fh->order_hint = sh->enable_order_hint ? lsmash_bits_get( bits, sh->OrderHintBits ) : 0;`.
3. **`refresh_frame_flags` Bit Width & `INTRA_ONLY_FRAME` Handling (`line 1203`)**:
   - In `2bd228c`:
     ```c
     if( fh->frame_type == AV1_FRAME_TYPE_SWITCH || fh->frame_type == AV1_FRAME_TYPE_INTRA_ONLY
      || (fh->frame_type == AV1_FRAME_TYPE_KEY && fh->show_frame) )
         fh->refresh_frame_flags = 0xff;
     else
         fh->refresh_frame_flags = lsmash_bits_get( bits, 1 );
     ```
   - **Defect**: Per AV1 Spec Section 5.9.2:
     - Only `SWITCH_FRAME` and (`KEY_FRAME && show_frame`) implicitly set `refresh_frame_flags = 0xff` (`allFrames`).
     - `INTRA_ONLY_FRAME` does **NOT** implicitly set `0xff`; it explicitly encodes `refresh_frame_flags` (`f(8)`) in the bitstream!
     - In the `else` branch, `refresh_frame_flags` is `f(8)` (**8 bits**), NOT `f(1)` (1 bit)!
   - **Required Fix**:
     ```c
     if( fh->frame_type == AV1_FRAME_TYPE_SWITCH
      || (fh->frame_type == AV1_FRAME_TYPE_KEY && fh->show_frame) )
         fh->refresh_frame_flags = 0xff;
     else
         fh->refresh_frame_flags = lsmash_bits_get( bits, 8 );
     ```

#### Bug 4: `lsmash_create_av1_specific_info()` Serialization Bug in `2bd228c` (`2bd228c:codecs/av1.c:1603-1615`)
In `2bd228c`, `lsmash_create_av1_specific_info()` accidentally called `lsmash_bs_put_be32()` instead of `lsmash_bs_put_byte()` for the 4 header bytes of `av1C` and wrote `((param->high_bitdepth << 6) & 0x01)` (which always evaluates to `0`).
- **Resolution**: Retain the already-corrected `lsmash_create_av1_specific_info()`, `isom_read_av1C()`, `isom_write_av1C()`, and `isom_print_av1C()` from `dev`'s `codecs/av1.c`, and merge the native OBU parser from `2bd228c:codecs/av1.{c,h}` into `codecs/av1.{c,h}`.

---

## 4. AV1-ISOBMFF Sample & Configuration Box (`av1C`) Compliance Rules

Per the **AV1 Codec ISO Media File Format Binding Specification (v1.2.0)**:

1. **Temporal Unit (TU) & Sample Reconstruction**:
   - Each ISOBMFF sample corresponds to one Temporal Unit (all OBUs associated with a single presentation time, starting with a `TEMPORAL_DELIMITER` OBU in the raw stream).
   - **Forbidden OBUs in Samples & `av1C`**:
     - `OBU_TEMPORAL_DELIMITER` (`obu_type == 2`), `OBU_TILE_LIST` (`obu_type == 8`), and `OBU_PADDING` (`obu_type == 15`) **MUST BE STRIPPED** when assembling an ISOBMFF sample (and must not appear in `av1C.configOBUs`).
   - **Mandatory `obu_has_size_field == 1`**:
     - Every OBU stored in an ISOBMFF sample or in `av1C.configOBUs` must have `obu_has_size_field = 1`. If an input stream contains an OBU with `obu_has_size_field == 0`, the sample assembler must synthesize the `obu_has_size_field = 1` header and encode its payload length using canonical LEB128.
2. **`AV1CodecConfigurationBox` (`av1C`) Population**:
   - `marker = 1` (1 bit), `version = 1` (7 bits).
   - `seq_profile` (3 bits), `seq_level_idx_0` (5 bits).
   - `seq_tier_0` (1 bit), `high_bitdepth` (1 bit), `twelve_bit` (1 bit), `monochrome` (1 bit), `chroma_subsampling_x` (1 bit), `chroma_subsampling_y` (1 bit), `chroma_sample_position` (2 bits).
   - `reserved = 0` (3 bits), `initial_presentation_delay_present` (1 bit), `initial_presentation_delay_minus_1` / `reserved` (4 bits).
   - `configOBUs`: Contains the `OBU_SEQUENCE_HEADER` (and any stream-level `OBU_METADATA` preceding the first frame).
3. **Sync Sample (`stss` / `trun` Sample Flags) & Delayed Random Access Point (`rap ` Sample Group) Signaling**:
   - **Sync Sample (`ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC`)**: Set when the sample's first frame is a `KEY_FRAME` with `show_frame == 1` (or an immediate `show_existing_frame == 1` pointing to a `KEY_FRAME` that refreshes all reference slots) and is preceded by an `OBU_SEQUENCE_HEADER` within the same sample.
   - **Delayed Random Access Point (`rap ` Sample Group)**: When a sample contains a forward `KEY_FRAME` with `show_frame == 0` (ARF / delayed keyframe), it is marked for the `rap ` sample group (`num_leading_samples_known = 1`, `num_leading_samples = 0`) per AV1-ISOBMFF Section 2.4.

---

## 5. Step-by-Step Salvage & Integration Plan

1. **Merge & Fix Native AV1 Parser in `codecs/av1.{c,h}`**:
   - Integrate the native OBU parser structures (`av1_info_t`, `av1_sequence_header_t`, `av1_frame_header_t`, `av1_stream_ buffer` state) and functions from `2bd228c:codecs/av1.{c,h}` into `dev`'s `codecs/av1.{c,h}`.
   - Apply all four bug fixes documented in Section 3.2 (`av1_get_uvlc`, `av1_get_leb128`/`av1_show_leb128`, `av1_uncompressed_header`, and `twelve_bit` propagation into `lsmash_av1_specific_parameters_t`).
   - Support both low-overhead OBU streams (`obu_has_size_field == 1`) and size-less OBU streams (`obu_has_size_field == 0`, re-synthesizing LEB128 size fields).
2. **Replace `importer/ivf_imp.c` with Native Implementation**:
   - Salvage `96065d0:importer/ivf_imp.c` and integrate with `codecs/av1.{c,h}` so IVF frames (including superframes / multi-frame Temporal Units with `show_frame == 0` followed by `show_frame == 1` or `show_existing_frame == 1`) are parsed and assembled using 100% native L-SMASH routines.
3. **Eliminate `obuparse` Completely**:
   - `git rm codecs/av1_obu.c codecs/av1_obu.h`
   - Update `configure` to remove `-lobuparse` from `LIBS` (line 119) and `codecs/av1_obu.c` from `SRCS` (line 318).
4. **Verify via Synthetic AV1 OBU Test Suite**:
   - Execute synthetic unit tests covering 8-bit, 10-bit, and 12-bit Sequence Headers, `uvlc`/`leb128` edge cases, `KEY_FRAME`, `INTER_FRAME`, `INTRA_ONLY_FRAME`, `SWITCH_FRAME`, `show_existing_frame`, and IVF muxing/remuxing without `obuparse`.
