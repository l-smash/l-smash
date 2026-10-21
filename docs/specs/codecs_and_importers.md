# Codec Architecture & Container Binding Specification (`docs/specs/codecs_and_importers.md`)

## 1. Architecture of `codecs/` and `importer/`

L-SMASH organizes codec and stream parsing into two decoupled, modular subsystems:
- **`codecs/`**: Bitstream syntax parsing, elementary stream header extraction, and container-neutral to container-specific configuration serialization (`isom_box_t` extension boxes and sample entry descriptions).
- **`importer/`**: Stream file format detection, container encapsulation (IVF, ADTS, WAVE, raw Annex-B), access unit delimitation, timeline timestamp generation, and sample packaging.

```
+--------------------+        +-------------------------+        +----------------------+
|  Raw Stream File   | -----> |  importer/ (*_imp.c)    | -----> |  codecs/ (*.c, *.h)  |
|  (WAV/IVF/Annex-B) |        |  Access Unit Extraction |        |  Syntax Verification |
+--------------------+        +-------------------------+        +----------------------+
                                           |                                |
                                           v                                v
                              +-------------------------+        +----------------------+
                              |  lsmash_sample_t Pool   |        |  lsmash_summary_t &  |
                              |  Timeline / Interleave  |        |  Codec Specific Data |
                              +-------------------------+        +----------------------+
                                           \                                /
                                            v                              v
                                      +------------------------------------------+
                                      |   core/ Movie Multiplexer (moov/moof)    |
                                      +------------------------------------------+
```

### 1.1 Summary and Codec-Specific Data Abstraction
Stream properties and decoder configuration are decoupled using public and internal abstract types:
- **`lsmash_summary_t`** (`lsmash_video_summary_t`, `lsmash_audio_summary_t`): Standard container-level media parameters (dimensions, pixel aspect ratio, sample rate, channels, bit depth). The `opaque` pointer holds a linked list of `lsmash_codec_specific_t` configuration records.
- **`lsmash_codec_specific_t`**: Stores codec configuration data in either:
  1. `LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED`: A strongly typed C struct representing decoded configuration parameters (e.g., `lsmash_h264_specific_parameters_t`, `lsmash_opus_specific_parameters_t`, `isom_pcmC_t`).
  2. `LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED`: The raw serialized binary box payload (including the 8-byte box header) ready for direct container serialization.

L-SMASH provides bidirectional conversion via `lsmash_create_codec_specific_data()` and `lsm_description_get_codec_specific()`, ensuring configuration can be parsed from incoming containers, manipulated as C structs, and re-serialized for target files.

### 1.2 Compile-Time `static const` Dispatch Tables
To maintain high performance and prevent branching spaghetti, `codecs/description.c`, `core/read.c`, `core/write.c`, and `core/print.c` employ static const dispatch tables:
- **`description.c`**: Maps `sample_entry_type` FourCC to description builders (`description_set_isom_opus_audio_description`, `description_set_h264_video_description`, `description_set_av1_video_description`, etc.) and codec-specific parameter reconstructors.
- **`core/read.c` / `core/write.c`**: Maps FourCC identifiers to dedicated box parsing, sizing, and writing function pointers via macros like `ADD_DESCRIPTION_READER_TABLE_ELEMENT` and `ADD_DESCRIPTION_WRITER_TABLE_ELEMENT`.
- **`core/print.c`**: Dispatches box debug printing via `ADD_PRINT_DESCRIPTION_EXTENSION_TABLE_ELEMENT` without hardcoding monolithic conditional ladders.

### 1.3 Elementary Stream Importer Architecture (`importer/importer.{c,h}`)
Importers encapsulate heterogeneous stream containers and raw bitstreams through the uniform `importer_functions` interface:
```c
typedef struct
{
    lsmash_class_t              class;
    int                         detectable;
    importer_probe              probe;
    importer_get_accessunit     get_accessunit;
    importer_get_last_duration  get_last_delta;
    importer_cleanup            cleanup;
    importer_construct_timeline construct_timeline;
} importer_functions;
```
- **Probing Order (`importer_func_table`)**: Format detection scans available importers sequentially:
  1. MPEG-4 Systems ADTS (`mp4sys_adts_importer`)
  2. MPEG-1/2 Audio Layer III (`mp4sys_mp3_importer`)
  3. 3GPP AMR (`amr_importer`)
  4. Dolby AC-3 (`ac3_importer`) & Enhanced AC-3 (`eac3_importer`)
  5. MPEG-4 ALS (`mp4a_als_importer`)
  6. DTS / DTS-HD (`dts_importer`)
  7. Xiph FLAC (`flac_importer`)
  8. RIFF WAVE / RF64 (`wave_importer`)
  9. AOM AV1 in IVF (`ivf_importer`)
  10. ITU-T H.264 / AVC Annex-B (`h264_importer`)
  11. ITU-T H.265 / HEVC Annex-B (`hevc_importer`)
  12. SMPTE VC-1 Advanced Profile (`vc1_importer`)
  13. Raw ISOBMFF Elementary Tracks (`isobm_importer`)
- **AU Allocation**: Importers assemble individual access units into dynamically allocated `lsmash_sample_t` packets, resolving media timestamps, random access flags, and durations before passing them to the movie multiplexing engine.

---

## 2. NALU-Based Video (`codecs/h264`, `codecs/hevc`, `codecs/nalu`, `importer/nalu_imp`)

NALU-based video codecs encapsulate variable-length Network Abstraction Layer Units into ISOBMFF sample structures per ISO/IEC 14496-15.

### 2.1 Annex-B Detection & Length-Prefixed Conversion (`codecs/nalu.{c,h}`)
- **Start Code Detection**: Scans bitstreams for 3-byte short start codes (`0x000001`) and 4-byte long start codes (`0x00000001`).
- **EBSP to RBSP**: `lsm_nalu_import_rbsp_from_ebsp()` strips emulation prevention bytes (`0x000003 -> 0x0000`) on demand for syntax parsing while preserving the original Encapsulated Byte Sequence Payload (EBSP) for multiplexing.
- **Length-Prefixed AU Assembly**: ISOBMFF replaces start codes with a fixed length prefix (4 bytes in L-SMASH, `NALU_DEFAULT_NALU_LENGTH_SIZE`). Importers assemble multiple NALUs belonging to a single Access Unit into contiguous memory using the `lsmash_multiple_buffers_t` banking structure.

### 2.2 H.264 / AVC Parsing & Configuration (`codecs/h264.{c,h}`)
- **Parameter Sets**: Parses Sequence Parameter Sets (SPS), Picture Parameter Sets (PPS), and Supplemental Enhancement Information (SEI). Computes frame dimensions from `pic_width_in_mbs_minus1`, `pic_height_in_map_units_minus1`, `frame_mbs_only_flag`, and cropping parameters (`frame_crop_left_offset`, `frame_crop_right_offset`, `frame_crop_top_offset`, `frame_crop_bottom_offset`).
- **Picture Order Count (POC)**: Implements POC derivation for Type 0 (POC LSB via `pic_order_cnt_lsb`), Type 1 (expected POC from delta parameters), and Type 2 (derived strictly from frame number), correctly resolving Picture-to-AU mapping and composition time offsets (`ctts`).
- **`avcC` Box Serialization**: Generates `AVCConfigurationBox` containing `configurationVersion = 1`, AVC Profile Indication, Profile Compatibility, AVC Level Indication, `lengthSizeMinusOne = 3` (4-byte length prefix), and dedicated pointer arrays for SPS, PPS, and optional SPSE (SPS Extension).

### 2.3 H.265 / HEVC Parsing & Configuration (`codecs/hevc.{c,h}`)
- **Hierarchical Parameter Sets**: Parses Video Parameter Sets (VPS), SPS, PPS, and SEI. Evaluates Profile Tier Level (`general_profile_space`, `general_tier_flag`, `general_profile_idc`, `general_profile_compatibility_flags`), sub-layer configurations, and picture cropping.
- **`hvcC` Box Serialization**: Generates `HEVCConfigurationBox` with `configurationVersion = 1`, general profile/tier/level indicators, `min_spatial_segmentation_idc`, parallelism indicators, and an array of parameter set NALU arrays tagged by `completeness` (1 bit) and `nal_unit_type` (6 bits).

### 2.4 Advanced Sample Grouping & Professional Extensions
- **Open-GOP (`rap `)**: For video streams containing non-IDR clean random access points (H.264 CRA or HEVC BLA/CRA), L-SMASH tags samples with `ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP` and emits `VisualRandomAccessEntry` records in the `rap ` Sample Group (`sgpd`/`sbgp`).
- **Gradual Decoder Refresh (`roll`)**: For streams utilizing recovery point SEI messages, L-SMASH establishes a `'roll'` Sample Group (`AudioRollRecoveryEntry` / `VisualRollRecoveryEntry`) where `roll_distance > 0` indicates the number of frames required to achieve full visual reconstruction.
- **Dolby Vision (`dvcC` / `dvvC`)**: Parses Dolby Vision RPU headers and serializes `DOVIConfigurationBox` (`dvcC` for Profiles ≤ 7, `dvvC` for Profile 8+) with `dv_version_major/minor`, `dv_profile`, `dv_level`, `rpu_present_flag`, `el_present_flag`, `bl_present_flag`, and `dv_bl_signal_compatibility_id`. Adds the `dby1` compatible brand.
- **Layered HEVC (`lhvC`)**: Serializes `LHEVCConfigurationBox` containing multi-layer parameter set arrays for stereoscopic 3D and multiview video profiles.

---

## 3. OBU-Based Video (`codecs/av1`, `importer/ivf_imp`)

L-SMASH provides a self-contained, zero-dependency C11 implementation of the **Alliance for Open Media (AOM) AV1** bitstream parser (`codecs/av1.{c,h}`) and IVF elementary stream importer (`importer/ivf_imp.c`), compliant with:
- **AV1 Bitstream & Decoding Process Specification** (Version 1.0.0 with Errata 1)
- **AV1 Codec ISO Media File Format Binding Specification** (v1.2.0)

All Open Bitstream Unit (OBU) parsing, Temporal Unit (TU) reconstruction, reference frame slot tracking, and `av1C` (`AV1CodecConfigurationBox`) generation are performed natively in `codecs/av1.c`.

### 3.1 Bitstream Syntax Primitives
All AV1 variable-length and non-symmetric integer primitives are implemented directly over `lsmash_bits_t` and `lsmash_bs_t`:
1. **`lsm_av1_get_uvlc( lsmash_bits_t *bits )`** *(AV1 Spec Section 4.10.3)*:
   - Counts `leadingZeros` until a `1` bit is encountered.
   - If `leadingZeros >= 32`, consumes remaining zero bits up to the terminating `1` bit (or EOB/error) and returns `UINT32_MAX` (the maximum valid `uvlc()` value when `leadingZeros == 31` is `0xFFFFFFFE`).
   - If `bits->bs->eob` or `bits->bs->error` is reached before a `1` bit or during the suffix read, returns `UINT32_MAX` without hanging.
2. **`lsm_av1_get_leb128( lsmash_bs_t *bs, uint8_t *Leb128Bytes )` & `lsm_av1_show_leb128(...)`** *(AV1 Spec Section 4.10.5)*:
   - Reads up to 8 bytes, shifting each 7-bit group as `(uint64_t)(leb128_byte & 0x7F) << (i * 7)`.
   - Enforces the AV1 constraint that `value <= (1ULL << 32) - 1` (`UINT32_MAX`) and `Leb128Bytes <= 8`; on violation or unexpected EOF, sets `*Leb128Bytes = 0` and returns `UINT64_MAX`.
3. **`lsm_av1_get_su( lsmash_bits_t *bits, unsigned int n )`** *(AV1 Spec Section 4.10.6)*: Reads an n-bit signed two's-complement integer using `uint64_t signMask = 1ULL << (n - 1)`.
4. **`lsm_av1_get_ns( lsmash_bits_t *bits, unsigned int n )`** *(AV1 Spec Section 4.10.7)*: Reads a non-symmetric unsigned integer in `[0, n - 1]` using `w = floor(log2(n)) + 1` and `m = 2^w - n`.

### 3.2 Sequence Header & `av1C` Configuration
`lsm_av1_parse_sequence_header()` parses `sequence_header_obu()` into `av1_sequence_header_t`:
- Validates `seq_profile <= 2` and rejects `!still_picture && reduced_still_picture_header`.
- Parses `timing_info()`, `decoder_model_info()`, and up to 32 operating points (`operating_point_idc`, `seq_level_idx`, `seq_tier`, `operating_parameters_info()`, `initial_display_delay_minus_1`).
- Evaluates `color_config()`: derives `BitDepth` (8, 10, or 12) and `NumPlanes` (1 when `mono_chrome`, else 3). The sRGB 4:4:4 evaluation uses logical AND (`cc->color_primaries == CP_BT_709 && cc->transfer_characteristics == TC_SRGB && cc->matrix_coefficients == MC_IDENTITY`), ensuring standard BT.709 4:2:0 streams are never misclassified as 4:4:4 RGB.

**`AV1CodecConfigurationBox` (`av1C`) Serialization**:
- `marker = 1` (1 bit), `version = 1` (7 bits)
- `seq_profile` (3 bits), `seq_level_idx_0` (5 bits), `seq_tier_0` (1 bit), `high_bitdepth` (1 bit), `twelve_bit` (1 bit), `monochrome` (1 bit), `chroma_subsampling_x` (1 bit), `chroma_subsampling_y` (1 bit), `chroma_sample_position` (2 bits)
- `reserved = 0` (3 bits), `initial_presentation_delay_present` (1 bit), `initial_presentation_delay_minus_one` / `0` (4 bits)
- `configOBUs`: contains the initial `OBU_SEQUENCE_HEADER` and any accompanying global `OBU_METADATA` OBUs in the first Temporal Unit.

### 3.3 Full Frame Header, Multi-Tile & Reference Slot State Machine
`codecs/av1.c` implements the complete `uncompressed_header()` syntax and reference frame update process:
1. **`show_existing_frame == 1` Reference Slot Validation & Keyframe Refresh**:
   - Verifies `parser->RefValid[frame_to_show_map_idx] != 0`.
   - When `RefFrameType[frame_to_show_map_idx] == AV1_KEY_FRAME`, refreshes all 8 reference slots (`RefValid`, `RefFrameId`, `RefUpscaledWidth`, `RefFrameWidth`, `RefFrameHeight`, `RefRenderWidth`, `RefRenderHeight`, `RefOrderHint`, `RefFrameType`, `RefFeatureEnabled`, `RefFeatureData`) from slot `frame_to_show_map_idx`.
2. **Complete `uncompressed_header()` Parsing**:
   - Frame and render sizing (`av1_parse_frame_size`, `av1_parse_render_size`, `av1_parse_superres_params`).
   - Reference frame derivation (`av1_set_frame_refs`).
   - Tile info parsing (`TileColsLog2`, `TileCols`, `TileRowsLog2`, `TileRows`, `NumTiles`, `TileSizeBytes`).
   - Quantization, segmentation, delta-q, loop filter, CDEF, loop restoration, TX mode, frame reference mode, skip mode, global motion, and film grain parameters.
3. **Multi-Tile `OBU_FRAME` & `OBU_TILE_GROUP` Tracking (`av1_parse_tile_group_obu`)**:
   - Validates `tg_start == frame->TileNum` and `tg_start <= tg_end < frame->NumTiles`.
   - Advances `frame->TileNum = tg_end + 1`. Only when `tg_end == frame->NumTiles - 1` (final tile) does the parser reset `frame->SeenFrameHeader = 0` and update reference slots via `refresh_frame_flags`.
   - Incomplete multi-tile frames at TU boundaries trigger `LSMASH_ERR_INVALID_DATA`.

### 3.4 AV1-ISOBMFF Container Binding & Sync Sample Classification
- **Sample OBU Whitelist (Section 2.4)**:
  - *Retained in sample payload*: `OBU_SEQUENCE_HEADER`, `OBU_FRAME_HEADER`, `OBU_TILE_GROUP`, `OBU_METADATA`, `OBU_FRAME`.
  - *Stripped*: `OBU_TEMPORAL_DELIMITER`, `OBU_REDUNDANT_FRAME_HEADER`, `OBU_PADDING`, reserved OBU types.
  - *Rejected*: `OBU_TILE_LIST` (`LSMASH_ERR_INVALID_DATA`).
- **Sync Sample (`stss`) vs. Random Access Point (`rap ` / `OPEN_RAP`)**:
  - A Temporal Unit is a **Sync Sample** (`ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC`) if and only if:
    1. Its first frame is a Key Frame (`first_frame_type == AV1_KEY_FRAME && first_show_frame && !first_show_existing`), **and**
    2. An `OBU_SEQUENCE_HEADER` appears in the **same Temporal Unit prior to that first frame header** (`tu.with_sequence_header == 1` while `!tu.seen_frame_header`).
  - Key Frames without an in-band preceding `OBU_SEQUENCE_HEADER`, delayed Key Frames (`show_frame == 0`), Intra-Only frames, and Switch frames are classified as `ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP` (placed in the `rap ` Sample Group).

### 3.5 IVF Importer (`importer/ivf_imp.c`)
Parses raw AV1 streams encapsulated in the IVF container:
- 32-byte IVF file header: validates signature `DKIF`, fourcc `AV01`, dimensions, timebase/timescale, and frame count.
- 12-byte IVF frame header: extracts frame payload size and 64-bit presentation timestamp (`pts`).
- Delimits and passes complete Temporal Units to `lsm_av1_parse_temporal_unit()`.

---

## 4. SMPTE, Legacy & Uncompressed Video (`codecs/vc1`, `codecs/mp4v`, `codecs/description`, `importer/vc1_imp`)

### 4.1 SMPTE VC-1 Advanced Profile (`codecs/vc1.{c,h}`, `importer/vc1_imp.c`)
- **Start Codes**: Scans for 4-byte SMPTE start codes (`0x000001` + identifier byte): Sequence Header (`0x0F`), Entry Point (`0x0E`), Frame (`0x0D`), Field (`0x0C`), Slice (`0x0B`).
- **`dvc1` Box Generation**: Generates `VC1SpecificBox` per SMPTE RP 2025 containing `profile = 3` (Advanced Profile), `level`, and the raw Sequence Header / Entry Point parameters.
- **Timeline & POC Calculation**: Computes picture order count for progressive frames, field pairs, and interlaced frames, converting raw timestamps into constant or variable sample durations.

### 4.2 MPEG-4 Visual (`codecs/mp4v.c`, `codecs/mp4sys.c`)
- **Visual Object Layer (VOL)**: Parses video object start codes, VOL headers, video plane with short header, aspect ratio information, and timing parameters (`vop_time_increment_resolution`).
- **`esds` Encapsulation**: Embeds `DecoderSpecificInfo` inside MPEG-4 Systems `esds` under object type indication `0x20` (MPEG-4 Visual) for `mp4v` sample entries.

### 4.3 Apple ProRes (`codecs/description.c`)
Supports full Apple ProRes FourCC profiles in QuickTime containers (`.mov`):
- `apch` (ProRes 422 HQ), `apcn` (ProRes 422 Standard), `apcs` (ProRes 422 LT), `apco` (ProRes 422 Proxy)
- `ap4h` (ProRes 4444), `ap4x` (ProRes 4444 XQ, supporting up to 12-bit sample depth with alpha channels)

### 4.4 QuickTime Uncompressed Y'CbCr / RGB Video Extensions
For uncompressed formats (`2vuy`, `v210`, `v216`, `v308`, `v408`, `v410`, `raw `, `rle `), L-SMASH constructs and parses essential QTFF color and geometry extension atoms:
- **`colr`**: Color parameter atom specifying NCLC (primaries, transfer, matrix) or ICC profiles.
- **`clap`**: Clean aperture atom specifying rational clean aperture width, height, and offsets.
- **`pasp`**: Pixel aspect ratio atom (`hSpacing` / `vSpacing`).
- **`fiel`**: Field/frame information atom (fields per sample, display order / temporal scan order).
- **`gama`**: Gamma level atom (rational gamma value).
- **`cspc`**: Color space FourCC atom.
- **`sgbt`**: Significant bits atom for non-power-of-two bit depths.

---

## 5. MPEG-4 Systems & Audio (`codecs/mp4sys`, `codecs/mp4a`, `importer/adts_imp`, `importer/als_imp`, `importer/mp3_imp`)

### 5.1 ISO/IEC 14496-1 Systems Descriptor Engine (`codecs/mp4sys.{c,h}`)
L-SMASH implements the hierarchical descriptor tree required for MPEG-4 Systems `esds` (`ElementaryStreamDescriptorBox`):
```
ES_Descriptor (Tag 0x03)
  +-- DecoderConfigDescriptor (Tag 0x04)
        |-- objectTypeIndication (0x40 = AAC, 0x6B = MP3, 0x20 = MPEG-4 Video)
        |-- streamType (Audio, Video, Systems)
        |-- bufferSizeDB, maxBitrate, avgBitrate
        +-- DecoderSpecificInfo (Tag 0x05)
              +-- AudioSpecificConfig / VOL Data
  +-- SLConfigDescriptor (Tag 0x06) (predefined = 0x02 MP4)
```
Variable-length descriptor length fields are encoded using 7-bit continuation bytes per ISO/IEC 14496-1.

### 5.2 MPEG-4 Audio / AAC (`codecs/mp4a.{c,h}`, `importer/adts_imp.c`)
- **ADTS Importer (`importer/adts_imp.c`)**: Synchronizes to ADTS frame headers (`0xFFF`), extracts profile, sampling frequency index, channel configuration, and frame length. Supports CRC protection presence/absence.
- **`AudioSpecificConfig` (ASC)**:
  - Generates explicit Audio Object Types (AOT 2 = AAC-LC, 5 = SBR, 29 = PS, 23 = AAC-LD, 39 = AAC-ELD).
  - Handles explicit vs. implicit SBR/PS signaling: hierarchically serializes `extensionSamplingFrequencyIndex` and `extensionAudioObjectType` when explicit backward-compatible or hierarchical signaling is requested.
- **Dual Encapsulation**: Emits `mp4a` + `esds` for ISOBMFF; emits QuickTime Sound Description v0/v1 with child `wave` -> `frma` (`mp4a`) -> `esds` -> `terminator` for legacy QuickTime compatibility.

### 5.3 MPEG-4 ALS (`importer/als_imp.c`)
Imports raw MPEG-4 Audio Lossless Coding (ALS) streams, verifies ALS frame headers, extracts channel assignments and uncompressed sample resolution, and packages ALS configuration data into `esds` (`objectTypeIndication = 0x40`, AOT 36).

### 5.4 MPEG-1/2 Audio Layer III / MP3 (`importer/mp3_imp.c`)
- **Frame Sync**: Locks onto sync words (`0xFFE` / `0xFFF`), decoding MPEG audio version (MPEG-1, MPEG-2, MPEG-2.5), layer, bitrate index, sample rate index, and padding bit.
- **Frame Sizing**:
  ```c
  /* MPEG-1 (1152 samples/frame) */
  frame_size = (144 * bitrate) / samplerate + padding;
  /* MPEG-2 / 2.5 (576 samples/frame) */
  frame_size = (72 * bitrate) / samplerate + padding;
  ```
- **Container Placement**: Encapsulates into ISOBMFF `mp4a` + `esds` (`objectTypeIndication = 0x6B`) or QuickTime `.mp3` sample entries.

---

## 6. Digital Cinema & Multichannel Audio (`codecs/a52`, `codecs/dts`, `importer/a52_imp`, `importer/dts_imp`)

### 6.1 Dolby AC-3 & Enhanced AC-3 (`codecs/a52.{c,h}`, `importer/a52_imp.c`)
- **AC-3 (Dolby Digital)**:
  - Detects sync word `0x0B77`, parsing `fscod` (sampling frequency: 48 kHz, 44.1 kHz, 32 kHz), `frmsizecod` (frame size and bitrate), `bsid` (bitstream identification), `bsmod` (bitstream mode), `acmod` (audio coding mode: 1/0 to 3/2), and `lfeon`.
  - Serializes `dac3` (`AC3SpecificBox`, 3 bytes payload) containing `fscod`, `bsid`, `bsmod`, `acmod`, `lfeon`, and `bit_rate_code`.
- **Enhanced AC-3 (E-AC-3 / Dolby Digital Plus)**:
  - Parses stream types: Type 0 (independent), Type 1 (dependent, channel extension), Type 2 (transcoded).
  - Evaluates `numblkscod` (blocks per frame: 1, 2, 3, or 6; allowing 256, 512, 768, 1536 audio samples/frame).
  - Serializes `dec3` (`EC3SpecificBox`) containing total `data_rate`, number of independent substreams (`num_ind_sub`), and individual substream descriptor entries (`fscod`, `bsid`, `asvc`, `bsmod`, `acmod`, `lfeon`, `num_dep_sub`, `chan_loc`).

### 6.2 DTS & DTS-HD (`codecs/dts.{c,h}`, `importer/dts_imp.c`)
- **Core Substream**: Parses 14-bit and 16-bit core sync words (`0x7FFE8001` big-endian, `0x1FFFE800` 14-bit, `0xFE7F0180` little-endian), extracting core sample rate, bit rate, channel arrangement (`AMODE`), and LFE flags.
- **Extension Substreams**: Identifies DTS-HD extension headers:
  - `X96k`: 96 kHz sampling rate extension.
  - `XCh`: 6.1 channel extension (discrete back center).
  - `XXCH`: 7.1 channel extension.
  - `XLL`: DTS-HD Master Audio (lossless extension).
  - `LBR`: DTS Express (low bitrate audio).
- **`ddts` Box Serialization**: Generates `DTSSpecificBox` per ETSI TS 102 114:
  - `DTSSamplingFrequency`, `maxBitrate`, `avgBitrate`, `pcmSampleDepth`, `FrameDuration` code (512, 1024, 2048, 4096).
  - `StreamConstruction` bitmask indicating core and extension presence.
  - `CoreLFEPresent` and `CoreChannelMask`.

---

## 7. Modern & Lossless Audio (`codecs/opus`, `codecs/flac`, `codecs/alac`, `importer/flac_imp`)

### 7.1 IETF Opus (`codecs/opus.{c,h}`)
Complies with **IETF RFC 6716** and the **Opus-in-ISOBMFF Specification**:
- **`dOps` (`OpusSpecificBox`)**: Serializes mandatory configuration box:
  - `Version = 0` (8 bits)
  - `OutputChannelCount` (8 bits)
  - `PreSkip` (16 bits): encoder delay samples to discard at stream onset.
  - `InputSampleRate` (32 bits, typically 48000 Hz).
  - `OutputGain` (16 bits, `0 dB = 0`).
  - `ChannelMappingFamily` (8 bits: 0 for mono/stereo; 1 for surround with `StreamCount`, `CoupledCount`, and channel mapping matrix).
- **Pre-Roll Specification (`'roll'` vs. `'prol'`)**:
  - In ISO/IEC 14496-12, **`'roll'` (`AudioRollRecoveryEntry` / `VisualRollRecoveryEntry`)** carries a `signed int(16) roll_distance`:
    - Positive values (`> 0`) indicate *post-roll*.
    - Negative values (`< 0`) indicate **pre-roll**.
  - **Opus Binding**: Every Opus sample is random-access capable (no `stss` Sync Sample box is present). Opus-in-ISOBMFF Section 4.4 specifies that pre-roll shall be signaled using **`'roll'` with a negative `roll_distance`** (typically representing 80 ms, or -3840 samples @ 48 kHz), combined with an Edit List (`elst`) to skip the `PreSkip` / delay samples.
  - **Role of `'prol'` (`AudioPreRollEntry`)**: `'prol'` is designated exclusively for audio codecs where not every sample is a Sync Sample (e.g., Dolby AC-4, MPEG-H 3D Audio / USAC) to describe pre-roll for designated random access recovery points. Opus never uses `'prol'`.

### 7.2 Xiph FLAC (`codecs/flac.{c,h}`, `importer/flac_imp.c`)
Complies with **IETF RFC 9639** and **FLAC-in-ISOBMFF**:
- **`dfLa` (`FLACSpecificBox`)**: Contains 4-byte FullBox header (`version = 0, flags = 0`), followed by one or more FLAC metadata blocks. The first block is the mandatory 34-byte `STREAMINFO` block (`min_blocksize`, `max_blocksize`, `min_framesize`, `max_framesize`, `sample_rate`, `channels`, `bits_per_sample`, `total_samples`, MD5 signature).
- **Variable Block Frame Parsing**: `importer/flac_imp.c` validates sync code (`0xFFF8` / `0xFFF9`), decodes variable block size, sample rate, channel assignment, and sample size headers, performs UTF-8 frame/sample number decoding, checks header CRC-8, and validates the 16-bit frame footer CRC.
- **ISOBMFF Encapsulation**: Each FLAC frame is stored as a single ISOBMFF sample in an `fLaC` track.

### 7.3 Apple Lossless / ALAC (`codecs/alac.{c,h}`)
- **ALAC Magic Cookie (36 bytes total)**:
  - `frameLength` (32 bits), `compatibleVersion` (8 bits), `bitDepth` (8 bits).
  - `pb` (8 bits), `mb` (8 bits), `kb` (8 bits), `numChannels` (8 bits), `maxRun` (16 bits).
  - `maxFrameBytes` (32 bits), `avgBitRate` (32 bits), `sampleRate` (32 bits).
- **Dual Container Binding**:
  - *ISOBMFF*: Sample entry `alac` containing child box `alac` storing the raw 36-byte magic cookie.
  - *QuickTime (QTFF)*: Sound Description v0 `alac` containing child `wave` decompression atom -> `frma` (`alac`) -> `alac` magic cookie atom -> `terminator`.

---

## 8. Uncompressed LPCM Audio (`codecs/description`, `importer/wave_imp`)

L-SMASH enforces strict isolation between QuickTime (`.mov`) and ISOBMFF (`.mp4`, Fragmented MP4) uncompressed audio.

### 8.1 Strict Container Separation
- **QuickTime (`file->qt_compatible != 0`)**: Uses Apple Sound Description v0/v1/v2 (`lpcm`, `sowt`, `twos`, `in24`, `in32`, `fl32`, `fl64`, `raw `), QuickTime `chan` atoms, and `wave` decompression atoms.
- **ISOBMFF (`!file->qt_compatible`)**: Uses ISO/IEC 23003-5 `ipcm` (integer PCM) and `fpcm` (IEEE 754 floating-point PCM) sample entries with mandatory `pcmC`, optional ISO/IEC 14496-12 `chnl`, and `AudioSampleEntryV1` + `srat` when sampling rates exceed 65535 Hz.

### 8.2 ISOBMFF Box Syntax
1. **`pcmC` (`PCMConfigBox`, ISO/IEC 23003-5 Section 5.2)**:
   - FullBox (`version = 0, flags = 0`, 14 bytes total).
   - `format_flags` (8 bits): bit 0 indicates endianness (0 = big-endian, 1 = little-endian). All other bits must be 0.
   - `PCM_sample_size` (8 bits): bits per sample. Validated strictly: 16, 24, or 32 for `ipcm`; 32 or 64 for `fpcm`. Missing or malformed `pcmC` causes `LSMASH_ERR_INVALID_DATA`.
2. **`chnl` (`ChannelLayout`, ISO/IEC 14496-12 Section 12.2.4.2)**:
   - Preserves exact ISO/IEC 14496-12 syntax identifiers in `lsmash_isom_channel_layout_t`:
     - `stream_structure`: bit 0 (channel structured), bit 1 (object structured).
     - `definedLayout`: ISO/IEC 23091-3 (CICP) ChannelConfiguration index (e.g., 1 = Mono, 2 = Stereo, 6 = 5.1ch, 12 = 7.1ch). When non-zero, followed by 64-bit `omittedChannelsMap`.
     - `positions`: When `definedLayout == 0`, provides an explicit array of `speaker_position`, including `azimuth` and `elevation` when `speaker_position == 126` (`LSMASH_SPEAKER_POSITION_EXPLICIT`).
3. **`srat` (`SamplingRateBox`, ISO/IEC 14496-12 Section 12.2.3.2)**:
   - When sampling rate exceeds 65535 Hz (e.g., 88.2 kHz, 96 kHz, 192 kHz), `AudioSampleEntry` is promoted to `version = 1` (`AudioSampleEntryV1`), setting `samplerate = 1 << 16` and appending `srat` containing the 32-bit `sampling_rate`.

### 8.3 Multi-Track Fragmented ISOBMFF Compaction (`core/fragment.c`)
- **Multi-Track Interleaving (`data_offset`)**: A `trun` box omits `ISOM_TR_FLAGS_DATA_OFFSET_PRESENT` only when it is the first run of the track fragment AND its `traf` is the last `traf` in `moof`. Interleaved tracks explicitly write `data_offset` to point precisely to sample offsets in `mdat`.
- **`tfhd` Default Hoisting**: Constant-size, constant-duration LPCM audio (1 tick/sample, constant byte size `channels * (sample_size / 8)`) hoists duration and size into `tfhd->default_sample_duration` and `tfhd->default_sample_size`, omitting per-sample rows (`trun->optional = NULL`).

### 8.4 RIFF WAVE Importer (`importer/wave_imp.c`)
- **Formats**: Supports `WAVE_FORMAT_PCM` (`0x0001`), `WAVE_FORMAT_IEEE_FLOAT` (`0x0003`), and `WAVE_FORMAT_EXTENSIBLE` (`0xFFFE`).
- **Channel Mask Mapping**: Maps Microsoft `dwChannelMask` speaker bits to ISO/IEC 14496-12 `chnl` CICP presets or explicit speaker position arrays.
- **Target Adaptation**: Automatically maps WAVE streams to `ipcm`/`fpcm` with `pcmC`/`chnl`/`srat` for ISOBMFF output, or legacy QTFF sound descriptions for MOV output.

---

## 9. Telephony & Waveform Audio (`importer/amr_imp`, `codecs/wma`, `codecs/qt_wfex`)

### 9.1 3GPP AMR-NB & AMR-WB (`importer/amr_imp.c`)
- **Magic Identifiers**: Identifies raw files via `#!AMR\n` (AMR-NB) and `#!AMR-WB\n` (AMR-WB).
- **Frame Header Decoding**: Decodes frame type index to determine octet-aligned speech payload sizes (AMR-NB modes 0–7: 4.75 kbps to 12.2 kbps; AMR-WB modes 0–8: 6.60 kbps to 23.85 kbps).
- **Configuration Boxes**: Generates `damr` (`AMRSpecificBox`) or `dawb` (`AMRWBSpecificBox`) with `vendor`, `decoder_version`, `mode_set`, `mode_change_period`, and `frames_per_sample`.

### 9.2 Waveform Audio in QuickTime (`codecs/qt_wfex.c`, `codecs/wma.c`)
- **`wfex` Atom**: Encapsulates Microsoft `WAVEFORMATEX` inside a QuickTime decompression atom (`wave` -> `wfex`).
- **Bitrate & Block Alignment**: `lsm_qt_wfex_update_bitrate()` updates `nAvgBytesPerSec` and `nBlockAlign`, rejecting invalid `cbSize < 12`.
- **WMA Parsing**: `lsm_wma_print_codec_specific()` parses little-endian `WAVEFORMATEX` data for Windows Media Audio streams stored in QuickTime containers.
