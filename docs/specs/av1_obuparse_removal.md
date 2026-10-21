# Built-In AV1 OBU Parser & AV1-ISOBMFF Specification (`docs/specs/av1_obuparse_removal.md`)

## 1. Overview & Motivation

L-SMASH provides a self-contained, zero-dependency C11 implementation of the **Alliance for Open Media (AOM) AV1** bitstream parser (`codecs/av1.{c,h}`) and IVF elementary stream importer (`importer/ivf_imp.c`), compliant with:
- **AV1 Bitstream & Decoding Process Specification** (Version 1.0.0 with Errata 1)
- **AV1 Codec ISO Media File Format Binding Specification** (v1.2.0, 12 December 2019)

No external `obuparse` library is required or linked; all Open Bitstream Unit (OBU) parsing, Temporal Unit (TU) reconstruction, reference frame slot tracking, and `av1C` (`AV1CodecConfigurationBox`) generation are performed natively in `codecs/av1.c`.

---

## 2. Bitstream Syntax Primitives (`codecs/av1.{c,h}`)

All AV1 variable-length and non-symmetric integer primitives are implemented directly over `lsmash_bits_t` and `lsmash_bs_t`:

1. **`lsm_av1_get_uvlc( lsmash_bits_t *bits )`** *(AV1 Spec Section 4.10.3)*:
   - Counts `leadingZeros` until a `1` bit is encountered.
   - If `leadingZeros >= 32`, consumes remaining zero bits up to the terminating `1` bit (or EOB/error) and returns `UINT32_MAX` (the maximum valid `uvlc()` value when `leadingZeros == 31` is `0xFFFFFFFE`).
   - If `bits->bs->eob` or `bits->bs->error` is reached before a `1` bit or during the `leadingZeros`-bit suffix read, returns `UINT32_MAX` without hanging.
2. **`lsm_av1_get_leb128( lsmash_bs_t *bs, uint8_t *Leb128Bytes )` & `lsm_av1_show_leb128(...)`** *(AV1 Spec Section 4.10.5)*:
   - Reads up to 8 bytes, shifting each 7-bit group as `(uint64_t)(leb128_byte & 0x7F) << (i * 7)`.
   - Enforces the AV1 specification constraint that `value <= (1ULL << 32) - 1` (`UINT32_MAX`) and `Leb128Bytes <= 8`; on violation or unexpected EOF, sets `*Leb128Bytes = 0` and returns `UINT64_MAX`.
3. **`lsm_av1_get_su( lsmash_bits_t *bits, unsigned int n )`** *(AV1 Spec Section 4.10.6)*:
   - Reads an $n$-bit signed two's-complement integer using `uint64_t signMask = 1ULL << (n - 1)`.
4. **`lsm_av1_get_ns( lsmash_bits_t *bits, unsigned int n )`** *(AV1 Spec Section 4.10.7)*:
   - Reads a non-symmetric unsigned integer in $[0, n - 1]$ using $w = \lfloor\log_2(n)\rfloor + 1$ and $m = 2^w - n$.

---

## 3. Sequence Header & `av1C` Configuration (`lsm_av1_parse_sequence_header`)

`lsm_av1_parse_sequence_header()` parses `sequence_header_obu()` (AV1 Spec Section 5.5) into `av1_sequence_header_t`:
- Validates `seq_profile <= 2` and rejects `!still_picture && reduced_still_picture_header`.
- Parses `timing_info()`, `decoder_model_info()`, and up to 32 `operating_point` entries (`operating_point_idc`, `seq_level_idx`, `seq_tier`, `operating_parameters_info()`, `initial_display_delay_minus_1`).
- Parses `frame_width_bits_minus_1`, `frame_height_bits_minus_1`, `max_frame_width_minus_1`, `max_frame_height_minus_1`, and `frame_id_numbers_present_flag` (validating `additional_frame_id_length_minus_1 + delta_frame_id_length_minus_2 + 3 <= 16`).
- Parses `enable_order_hint` and `OrderHintBits = enable_order_hint ? order_hint_bits_minus_1 + 1 : 0`.
- Parses `color_config()` (AV1 Spec Section 5.5.2):
  - Correctly derives `BitDepth` (`8`, `10`, or `12` when `seq_profile == 2 && high_bitdepth && twelve_bit`) and `NumPlanes` (`1` when `mono_chrome`, else `3`).
  - Evaluates the sRGB 4:4:4 shortcut using logical AND (`cc->color_primaries == CP_BT_709 && cc->transfer_characteristics == TC_SRGB && cc->matrix_coefficients == MC_IDENTITY`), ensuring standard BT.709 4:2:0 streams are never misclassified as 4:4:4 RGB.

### `AV1CodecConfigurationBox` (`av1C`) Serialization
Per AV1-ISOBMFF v1.2.0 Section 2.3:
- `marker = 1` (1 bit), `version = 1` (7 bits)
- `seq_profile` (3 bits), `seq_level_idx_0` (5 bits)
- `seq_tier_0` (1 bit), `high_bitdepth` (1 bit), `twelve_bit` (1 bit), `monochrome` (1 bit), `chroma_subsampling_x` (1 bit), `chroma_subsampling_y` (1 bit), `chroma_sample_position` (2 bits)
- `reserved = 0` (3 bits), `initial_presentation_delay_present` (1 bit), and `initial_presentation_delay_minus_one` / `0` (4 bits)
- `configOBUs`: contains the initial `OBU_SEQUENCE_HEADER` and any accompanying global `OBU_METADATA` OBUs in the first Temporal Unit.

---

## 4. Full Frame Header, Multi-Tile & Reference Slot State Machine

To support multi-tile `OBU_FRAME` and split `OBU_FRAME_HEADER` + `OBU_TILE_GROUP` streams, `codecs/av1.c` implements the complete `uncompressed_header()` syntax (AV1 Spec Section 5.9) and reference frame update process (Section 7.20):

1. **`show_existing_frame == 1` Reference Slot Validation & Keyframe Refresh**:
   - Verifies `parser->RefValid[frame_to_show_map_idx] != 0`.
   - When `RefFrameType[frame_to_show_map_idx] == AV1_KEY_FRAME`, refreshes all 8 reference slots (`RefValid[i]`, `RefFrameId[i]`, `RefUpscaledWidth[i]`, `RefFrameWidth[i]`, `RefFrameHeight[i]`, `RefRenderWidth[i]`, `RefRenderHeight[i]`, `RefOrderHint[i]`, `RefFrameType[i]`, `RefFeatureEnabled[i]`, `RefFeatureData[i]`) from slot `frame_to_show_map_idx` via `av1_load_reference_frame_info()` and `av1_update_reference_list()`.
2. **Complete `uncompressed_header()` Sub-Tables**:
   - `av1_parse_frame_size()`, `av1_parse_render_size()`, `av1_parse_superres_params()`, `av1_compute_image_size()`
   - `av1_set_frame_refs()` (short-signaling reference frame derivation per Section 7.8)
   - `av1_parse_tile_info()` (`TileColsLog2`, `TileCols`, `TileRowsLog2`, `TileRows`, `NumTiles`, `TileSizeBytes`)
   - `av1_parse_quantization_params()` & `av1_parse_segmentation_params()` (deriving `LosslessArray[s]`, `CodedLossless`, and `AllLossless`)
   - `av1_parse_delta_q_params()` & `av1_parse_delta_lf_params()`
   - `av1_parse_loop_filter_params()`, `av1_parse_cdef_params()`, `av1_parse_lr_params()`
   - `av1_read_tx_mode()`, `av1_parse_frame_reference_mode()`, `av1_parse_skip_mode_params()`, `av1_parse_global_motion_params()`, and `av1_parse_film_grain_params()`
3. **Multi-Tile `OBU_FRAME` & `OBU_TILE_GROUP` Tracking (`av1_parse_tile_group_obu`)**:
   - Reads `tile_start_and_end_present_flag`, `tg_start`, and `tg_end` (when `NumTiles > 1`).
   - Validates `tg_start == frame->TileNum` and `tg_start <= tg_end < frame->NumTiles`.
   - Advances `frame->TileNum = tg_end + 1`. Only when `tg_end == frame->NumTiles - 1` (the final tile of the frame) does the parser reset `frame->SeenFrameHeader = 0` and execute `av1_update_reference_list()` using the full 8-bit `refresh_frame_flags` mask (`(frame->refresh_frame_flags >> i) & 1`).
   - At the end of a Temporal Unit, if `parser->frame.SeenFrameHeader != 0` (an incomplete multi-tile frame), `lsm_av1_parse_temporal_unit()` returns `LSMASH_ERR_INVALID_DATA`.

---

## 5. AV1-ISOBMFF v1.2.0 Sample Assembly & Sync Sample Classification

`lsm_av1_parse_temporal_unit()` enforces the container binding rules of AV1-ISOBMFF v1.2.0:
1. **Sample OBU Whitelist (Section 2.4)**:
   - Retained in sample data: `OBU_SEQUENCE_HEADER`, `OBU_FRAME_HEADER`, `OBU_TILE_GROUP`, `OBU_METADATA`, `OBU_FRAME`.
   - Stripped from sample data: `OBU_TEMPORAL_DELIMITER`, `OBU_REDUNDANT_FRAME_HEADER`, `OBU_PADDING`, and reserved OBU types.
   - Rejected as invalid (`LSMASH_ERR_INVALID_DATA`): `OBU_TILE_LIST`.
2. **Sync Sample (`stss` / `SYNC`) vs. Random Access Point (`rap ` / `OPEN_RAP`) Classification**:
   - Per AV1-ISOBMFF v1.2.0 Section 2.4, a Temporal Unit is a **Sync Sample** (`ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC`) if and only if:
     1. Its first frame is a Key Frame (`first_frame_type == AV1_KEY_FRAME && first_show_frame && !first_show_existing`), **and**
     2. An `OBU_SEQUENCE_HEADER` appears in the **same Temporal Unit prior to that first frame header** (`tu.with_sequence_header == 1` recorded while `!tu.seen_frame_header`).
   - Key Frames without an in-band preceding `OBU_SEQUENCE_HEADER` in the same TU, delayed Key Frames (`show_frame == 0`), Intra-Only frames (`AV1_INTRA_ONLY_FRAME`), and Switch frames (`AV1_SWITCH_FRAME`) are classified as `ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP` (recorded in the `rap ` Sample Group rather than `stss`).
