# ISO/IEC 23003-5 LPCM in Fragmented ISOBMFF Specification (`docs/specs/lpcm_isobmff_fragmented.md`)

## 1. Executive Summary & Standard Compliance Rationale

Historically, L-SMASH supported uncompressed Linear PCM (LPCM) audio only via QuickTime File Format (QTFF) Sound Sample Descriptions (`lpcm`, `raw `, `sowt`, `twos`, `fl32`, `fl64`, `in24`, `in32`, Sound Description v0/v1/v2, `wave`/`frma`/`enda`, and `chan`).

However:
1. **QTFF Has No Official Fragmented Specification**: Movie Fragments (`moof`/`mfhd`/`traf`/`tfhd`/`tfdt`/`trun`) and Segment Index (`sidx`) are defined exclusively in **ISO/IEC 14496-12 (ISOBMFF)**, not in QTFF. Emitting QTFF Sound Description v1/v2 or `wave`/`chan` boxes inside an ISOBMFF (`isom`/`iso6`) container violates **strict standard compliance (Strict International Standard Compliance)**.
2. **International Standard for Uncompressed Audio in ISOBMFF**:
   - **ISO/IEC 23003-5** (*Information technology — MPEG audio technologies — Part 5: Uncompressed audio in MPEG-4 file format*) defines standard Sample Entries **`ipcm`** (uncompressed integer PCM) and **`fpcm`** (uncompressed IEEE 754 floating-point PCM) along with the mandatory **`pcmC`** (`PCMConfigBox`).
   - **ISO/IEC 14496-12** (Section 12.2.3 & 12.2.4) defines **`AudioSampleEntry` / `AudioSampleEntryV1`**, **`chnl`** (`ChannelLayoutBox`), and **`srat`** (`SamplingRateBox`).

This specification defines the full-featured implementation of ISO/IEC 23003-5 (`ipcm`, `fpcm`, `pcmC`) + ISO/IEC 14496-12 (`chnl`, `srat`, `AudioSampleEntryV1`) across non-fragmented and **multi-track Fragmented ISOBMFF** workflows, while maintaining existing QTFF LPCM support strictly for `qt  ` containers.

---

## 2. Binary Box & Sample Entry Specifications

### 2.1 Sample Entry Types (`ipcm` and `fpcm`)
Defined in `lsmash.h` and `core/box_type.{c,h}`:
- `ISOM_CODEC_TYPE_IPCM_AUDIO` (`LSMASH_4CC('i', 'p', 'c', 'm')`): Integer Linear PCM Audio Sample Entry.
- `ISOM_CODEC_TYPE_FPCM_AUDIO` (`LSMASH_4CC('f', 'p', 'c', 'm')`): Floating-Point Linear PCM Audio Sample Entry.

#### `AudioSampleEntry` (`version = 0`) vs. `AudioSampleEntryV1` (`version = 1`)
Per ISO/IEC 14496-12 Section 12.2.3.2:
- **When `sampling_rate <= 65535` Hz and `stsd->version == 0`**:
  - `audio->version = 0`
  - `audio->channelcount = channels` (actual channel count, e.g., 1, 2, 6, 8)
  - `audio->samplesize = 16` (template field in ISOBMFF; actual bit depth is signaled in `pcmC->PCM_sample_size`)
  - `audio->compression_ID = 0`, `audio->packet_size = 0`
  - `audio->samplerate = sampling_rate << 16`
- **When `sampling_rate > 65535` Hz (e.g., 88.2 kHz, 96 kHz, 176.4 kHz, 192 kHz, 352.8 kHz, 384 kHz) or `AudioSampleEntryV1` is used**:
  - `stsd->version = 1` (requires `max_isom_version >= 6` / `isom` brand compatibility)
  - `audio->version = 1` (**Note**: In ISOBMFF `AudioSampleEntryV1`, `version == 1` has **zero** additional fields before child boxes — unlike QTFF Sound Description v1 which has 16 bytes of `samplesPerPacket`/`bytesPerPacket`/`bytesPerFrame`/`bytesPerSample`!)
  - `audio->samplerate = 1 << 16` (or `min(sampling_rate, 65535) << 16` for v0 fallback)
  - Child box **`srat`** (`SamplingRateBox`, FullBox `version = 0, flags = 0`) carries the full 32-bit `uint32_t sampling_rate`.

### 2.2 `PCMConfigBox` (`pcmC` — ISO/IEC 23003-5)
- **Box Type**: `ISOM_BOX_TYPE_PCMC` (`LSMASH_4CC('p', 'c', 'm', 'C')`)
- **Box Header**: FullBox (`version = 0`, `flags = 0`, 12 bytes header + 2 bytes payload = **14 bytes total**)
- **Syntax**:
  ```c
  aligned(8) class PCMConfigBox extends FullBox('pcmC', version = 0, 0) {
      unsigned int(8) format_flags;     // bit(1) pcm_sample_endian (0=BE, 1=LE), bit(7) reserved=0
      unsigned int(8) PCM_sample_size;  // bits per sample: 16, 24, 32 (for 'ipcm'); 32, 64 (for 'fpcm')
  }
  ```
- **Bit Field Semantics (`format_flags`)**:
  - `0x00`: Big-endian PCM samples.
  - `0x01`: Little-endian PCM samples (`LSMASH_PCM_ENDIAN_LITTLE = 0x01`).
  - Bits `0xFE` (`reserved`) **must be `0`** under strict standard compliance validation.
- **Valid `PCM_sample_size` Values**:
  - For `ipcm`: `16`, `24`, or `32` bits (signed two's complement integer).
  - For `fpcm`: `32` or `64` bits (IEEE 754 floating-point).

### 2.3 `ChannelLayoutBox` (`chnl` — ISO/IEC 14496-12 Section 12.2.4.1)
- **Box Type**: `ISOM_BOX_TYPE_CHNL` (`LSMASH_4CC('c', 'h', 'n', 'l')`)
- **Box Header**: FullBox (`version = 0`, `flags = 0`)
- **Syntax (`version == 0`)**:
  ```c
  aligned(8) class ChannelLayoutBox extends FullBox('chnl', version = 0, 0) {
      unsigned int(8) stream_structure; // bit(1) channelStructured (0x01), bit(1) objectStructured (0x02)
      if (stream_structure & channelStructured) {
          unsigned int(8) definedLayout; // ISO/IEC 23091-3 (CICP) ChannelConfiguration
          if (definedLayout == 0) {
              for (i = 1; i <= layout_channel_count; i++) {
                  // layout_channel_count comes from AudioSampleEntry.channelcount
                  unsigned int(7) reserved = 0; // or in ed.5/6: unsigned int(8) speaker_position
                  unsigned int(8) speaker_position;
                  if (speaker_position == 126) { // explicit position
                      signed int(16) azimuth;
                      signed int(8)  elevation;
                  }
              }
          } else {
              unsigned int(64) omittedChannelsMap;
          }
      }
      if (stream_structure & objectStructured) {
          unsigned int(8) object_count;
      }
  }
  ```
- ** Box Precedence**:
  - In `core/box.h`, define `LSMASH_BOX_PRECEDENCE_ISOM_CHNL`, `LSMASH_BOX_PRECEDENCE_ISOM_PCMC`, and `LSMASH_BOX_PRECEDENCE_ISOM_SRAT` so child boxes in `AudioSampleEntry` (`ipcm`/`fpcm`) are ordered canonically (`srat` $\rightarrow$ `chnl` $\rightarrow$ `pcmC`).

### 2.4 Sample Framing & Byte Layout in `mdat` (`stbl` and `traf`/`trun`)
Per ISO/IEC 23003-5 and ISO/IEC 14496-12:
- One **LPCM frame** consists of one sample value per channel interleaved in channel layout order:
  $$\text{frame\_size} = \frac{\text{PCM\_sample\_size}}{8} \times \text{channelcount}\quad\text{(bytes)}$$
- Each LPCM frame has:
  - Media timescale = `sampling_rate` (Hz)
  - Sample duration (`sample_delta` / `default_sample_duration`) = `1`
  - Sample size (`sample_size` / `default_sample_size`) = `frame_size`
  - Sync sample flag = `ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC` (every LPCM frame is an independent sync sample).
- **Internal Frame Splitting (`isom_append_sample_by_type` in `core/isom.c:3902`)**:
  - L-SMASH's `wave_imp.c` (and user applications) may pass a bunch of $N$ consecutive LPCM frames (e.g., 1,000 frames) in a single `lsmash_sample_t` buffer (`sample->length = N * frame_size`) or individual frames (`sample->length = frame_size`).
  - `isom_set_isom_lpcm_description()` **must** set `audio->constBytesPerAudioPacket = frame_size` (and `isom_read_pcmC()` must also populate `audio->constBytesPerAudioPacket = (pcmC->PCM_sample_size * audio->channelcount) / 8` when reading an `ipcm`/`fpcm` track) so that `isom_is_lpcm_audio()` and `isom_append_sample_by_type()` work transparently across both non-fragmented and fragmented muxing/remuxing!

---

## 3. Critical Pre-Existing Bugs Fixed in the implementation

Our Read-Only codebase audit and the implementation verification uncovered and resolved **six critical bugs** in `core/box.c`, `core/read.c`, `core/write.c`, and `core/fragment.c`:

### 3.1 Bug 1: `fullbox_type_table` Fixed-Size Array Overflow (`core/box.c:165`)
In `core/box.c:163-228`:
```c
int isom_is_fullbox( void *box )
{
    if( !box )
        return 0;
    isom_box_t *box_orig = (isom_box_t *)box;
    static lsmash_box_type_t fullbox_type_table[62] = { LSMASH_BOX_TYPE_INITIALIZER };
    if( !lsmash_check_box_type_specified( &fullbox_type_table[0] ) )
    {
        /* Initialize the table. */
        int i = 0;
        fullbox_type_table[i++] = ISOM_BOX_TYPE_MVHD;
        ...
```
- **Root Cause**:
  - On `master`, `fullbox_type_table[50]` had 48 FullBox entries + 1 `LSMASH_BOX_TYPE_INITIALIZER` terminator = **49 elements (only 1 slot left)**.
  - On `dev` (`vimeo` commit `9bc5dc3`), 12 FullBoxes were added and the array size was bumped by 12 to `fullbox_type_table[62]`, resulting in 60 FullBox entries + 1 terminator = **61 elements (still only 1 slot left!)**.
  - Adding `ISOM_BOX_TYPE_PCMC` (`pcmC`) and `ISOM_BOX_TYPE_CHNL` (`chnl`) requires **2 new FullBox entries** (62 entries + 1 terminator = 63 elements), which **overflows `fullbox_type_table[62]`** (out-of-bounds write on the static array)!
- **Permanent Fix**:
  - Replace the runtime-initialized fixed-size `static lsmash_box_type_t fullbox_type_table[62]` with a compile-time `static const` unsized array (or eliminate the magic number `62` completely) so adding FullBoxes can never overflow the table again.

### 3.2 Bug 2: `isom_write_audio_description()` Corrupts ISOBMFF `AudioSampleEntryV1` (`core/write.c:662`)
Compare how `core/read.c`, `core/print.c`, and `core/write.c` handle `audio->version == 1`:
- **`core/read.c:1584-1597` (Correct)**:
  ```c
  else if( audio->version == 1 )
  {
      if( ((isom_stsd_t *)parent)->version == 0 )
      {
          audio->samplesPerPacket = lsmash_bs_get_be32( bs );
          audio->bytesPerPacket   = lsmash_bs_get_be32( bs );
          audio->bytesPerFrame    = lsmash_bs_get_be32( bs );
          audio->bytesPerSample   = lsmash_bs_get_be32( bs );
          box->manager |= LSMASH_QTFF_BASE;
      }
      else
          /* AudioSampleEntryV1 has no additional fields. */
          box->manager &= ~LSMASH_QTFF_BASE;
  }
  ```
- **`core/print.c:1309` (Correct)**:
  ```c
  if( audio->version == 1 && (audio->manager & LSMASH_QTFF_BASE) )
  ```
- **`core/write.c:662-668` (BUG!)**:
  ```c
  else if( data->version == 1 )
  {
      lsmash_bs_put_be32( bs, data->samplesPerPacket );
      lsmash_bs_put_be32( bs, data->bytesPerPacket );
      lsmash_bs_put_be32( bs, data->bytesPerFrame );
      lsmash_bs_put_be32( bs, data->bytesPerSample );
  }
  ```
  And in **`core/box.c:1349` (BUG!)**:
  ```c
  audio->size = ISOM_BASEBOX_COMMON_SIZE + 28 + (16 * (audio->version == 1)) + (36 * (audio->version == 2));
  ```
- **Impact**: Whenever an ISOBMFF `AudioSampleEntryV1` (`stsd->version == 1`, `audio->version == 1`, `!(audio->manager & LSMASH_QTFF_BASE)`) is written (required for `srat` when `sampling_rate > 65535`), `isom_write_audio_description()` unconditionally writes 16 bytes of QTFF fields (`0x00000000 x 4`) before child boxes, producing a completely invalid ISOBMFF `stsd` box!
- **Fix**:
  - In `core/write.c:662`: change condition to `else if( data->version == 1 && (data->manager & LSMASH_QTFF_BASE) )`.
  - In `core/box.c:1349` (and `isom_update_box_size` calculations): only add `16` when `audio->version == 1 && (audio->manager & LSMASH_QTFF_BASE)`.

### 3.3 Bug 3: `isom_write_srat()` Missing in `core/write.c`
- `srat` (`SamplingRateBox`) is defined in `core/box.h:269`, `core/box_default.c:93`, `core/box.c:1032` (`isom_add_srat`), `core/read.c:1661` (`isom_read_srat`), and `core/print.c:1357` (`isom_print_srat`).
- However, **`isom_write_srat()` does not exist in `core/write.c`**, and `ISOM_BOX_TYPE_SRAT` is absent from `box_writer_table[]` in `core/write.c`!
- **Fix**: Implement `isom_write_srat()` in `core/write.c`:
  ```c
  static int isom_write_srat( lsmash_bs_t *bs, isom_box_t *box )
  {
      isom_srat_t *srat = (isom_srat_t *)box;
      isom_bs_put_box_common( bs, srat );
      lsmash_bs_put_be32( bs, srat->sampling_rate );
      return lsmash_bs_flush_buffer( bs );
  }
  ```
  and register `{ ISOM_BOX_TYPE_SRAT, isom_write_srat }` in `box_writer_table[]`.

### 3.4 Bug 4: Multi-Track Fragmented `trun->optional` Explosion (`core/fragment.c:1225, 1367, 1640`)
In `core/fragment.c`:
- A Track Run Box (`trun`) has two categories of `tr_flags`:
  - **Header-level flags**: `ISOM_TR_FLAGS_DATA_OFFSET_PRESENT` (`0x000001`) and `ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT` (`0x000004`).
  - **Per-sample row flags (`0x000f00`)**:
    - `ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT` (`0x000100`)
    - `ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT` (`0x000200`)
    - `ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT` (`0x000400`)
    - `ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT` (`0x000800`)
- For LPCM audio in `traf`/`trun`, every sample has constant duration (`1` == `tfhd->default_sample_duration`), constant size (`frame_size` == `tfhd->default_sample_size`), constant sync flags (`tfhd->default_sample_flags`), and zero composition offset (`0`). Thus **zero per-sample optional rows (`trun->optional`) are needed**, allowing 48,000 LPCM samples in a 1-second fragment to be represented by a single 20-byte `trun` box!
- **The Bug**:
  - In `isom_set_fragment_last_duration()` (`core/fragment.c:1225`), `isom_update_fragment_previous_sample_duration()` (`core/fragment.c:1367`), and `isom_fragment_update_sample_tables()` (`core/fragment.c:1640`), the code checks:
    ```c
    if( trun->flags )
    {
        isom_trun_optional_row_t *row = isom_request_trun_optional_row( trun, tfhd, trun->sample_count );
        ...
    ```
  - In a **multi-track fragmented movie** (or a `traf` with multiple `trun`s), when Track 1 flushes its pooled samples into `file->fragment->pool`, `file->fragment->pool_size` becomes non-zero.
  - When Track 2 (e.g., LPCM audio) is flushed via `isom_flush_fragment_pooled_samples()` (`core/fragment.c:1327-1334`), line 1328 sets `trun->flags |= ISOM_TR_FLAGS_DATA_OFFSET_PRESENT;` (`0x000001`) **before** calling `isom_set_fragment_last_duration()` at line 1334!
  - Inside `isom_set_fragment_last_duration()`, `if( trun->flags )` evaluates to **TRUE** solely because `ISOM_TR_FLAGS_DATA_OFFSET_PRESENT` (`0x000001`) is set! It then calls `isom_request_trun_optional_row(trun, tfhd, trun->sample_count)`, which allocates `trun->sample_count` (e.g., 48,000) `isom_trun_optional_row_t` entries!
  - Furthermore, once `trun->optional` is non-NULL, `isom_finish_fragment_movie()` (`core/fragment.c:927-947`) inspects `trun->optional->head->next`. And if a `traf` has multiple `trun`s (`delimit == 1` at `core/fragment.c:1533`), `trun->flags |= ISOM_TR_FLAGS_DATA_OFFSET_PRESENT` is set on the previous `trun` before `isom_update_fragment_previous_sample_duration()` (`line 1367`) checks `if( trun->flags )`, again allocating `trun->sample_count` optional rows unnecessarily!
- **Fix**:
  - Define:
    ```c
    #define ISOM_TR_FLAGS_OPTIONAL_ROWS_PRESENT_MASK               \
        ( ISOM_TR_FLAGS_SAMPLE_DURATION_PRESENT                    \
        | ISOM_TR_FLAGS_SAMPLE_SIZE_PRESENT                        \
        | ISOM_TR_FLAGS_SAMPLE_FLAGS_PRESENT                       \
        | ISOM_TR_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT )
    ```
  - Replace `if( trun->flags )` at `core/fragment.c:1225`, `1367`, and `1640` with `if( trun->flags & ISOM_TR_FLAGS_OPTIONAL_ROWS_PRESENT_MASK )`.

### 3.5 Bug 5: `useful_first_sample_flags` & `tfra->length_size_of_trun_num` Bugs in `core/fragment.c`
1. **Unnecessary `first_sample_flags` in `trun` (`core/fragment.c:919-948`)**:
   - In `isom_finish_fragment_movie()`, `useful_first_sample_flags` is initialized to `1`. When `trun->sample_count > 1` and `trun->optional == NULL` (all samples in the run used default flags), neither `if( trun->sample_count == 1 )` nor `else if( trun->optional && trun->optional->head )` executes, leaving `useful_first_sample_flags = 1` and setting `ISOM_TR_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT` even when `trun->first_sample_flags` is identical to `tfhd->default_sample_flags`!
   - **Fix**: Add `else if( !isom_compare_sample_flags( &trun->first_sample_flags, &tfhd->default_sample_flags ) ) useful_first_sample_flags = 0;`.
2. **Copy-Paste Typo in `tfra->length_size_of_trun_num` (`core/fragment.c:1598`)**:
   - Line 1598 computes `tfra->length_size_of_trun_num` using `for( length = 1; rap->traf_number >> (length * 8); length++ );` (`rap->traf_number` instead of `rap->trun_number`).
   - **Fix**: Change `rap->traf_number` on line 1598 to `rap->trun_number`.

### 3.6 Bug 6: `isom_box_common_copy()` `stsd->version` Drop in `core/read.c:135`
- In `core/read.c:135`, `isom_box_common_copy()` checked `if( lsmash_check_box_type_identical( ((isom_box_t *)src)->type, ISOM_BOX_TYPE_STSD ) )` (instead of checking `parent->type == ISOM_BOX_TYPE_STSD` as `core/box.c:149` does).
- Because `stsd` is a FullBox (`stsd->version == 1` when containing `AudioSampleEntryV1`) while its child Sample Entries are BaseBoxes, checking `src->type == ISOM_BOX_TYPE_STSD` caused `isom_read_stsd()` to call `isom_basebox_common_copy()` and **discard `stsd->version = 1`**, leaving `stsd->version == 0`!
- Consequently, `isom_read_audio_description()` (`core/read.c:1596`) saw `((isom_stsd_t *)parent)->version == 0` when reading an ISOBMFF `AudioSampleEntryV1` (`audio->version == 1`), misidentifying it as a QTFF Sound Sample Description v1 and consuming 16 bytes of child boxes (`srat`, `chnl`, `pcmC`) as bogus `samplesPerPacket` fields!
- **Fix**: Changed `core/read.c:135` to check `parent && lsmash_check_box_type_identical( parent->type, ISOM_BOX_TYPE_STSD )`, matching `core/box.c:149`.

---

## 4. Completed Module Modification Matrix (the implementation)

| Module | File Path | Implemented Changes |
| :--- | :--- | :--- |
| **Public API** | `lsmash.h` | Added `ISOM_CODEC_TYPE_IPCM_AUDIO` (`ipcm`), `ISOM_CODEC_TYPE_FPCM_AUDIO` (`fpcm`), `LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_PCM_CONFIG`, `LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_CHANNEL_LAYOUT`, `lsmash_isom_pcm_config_t`, and `lsmash_isom_channel_layout_t`. |
| **Box Types** | `core/box_type.{c,h}` | Defined `ISOM_BOX_TYPE_PCMC` (`pcmC`), `ISOM_BOX_TYPE_CHNL` (`chnl`), `ISOM_CODEC_TYPE_IPCM_AUDIO`, `ISOM_CODEC_TYPE_FPCM_AUDIO`. |
| **Box Definitions** | `core/box.h`, `core/box_default.{c,h}`, `core/box.c` | Defined `isom_pcmC_t` and `isom_chnl_t` (`isom_chnl_explicit_position_t`); added `isom_add_pcmC()`, `isom_add_chnl()`, `box_destruct_chnl()`; replaced fixed `fullbox_type_table[62]` with compile-time `static const` unsized array and registered `ISOM_BOX_TYPE_PCMC` & `ISOM_BOX_TYPE_CHNL`; fixed `audio->version == 1` size check in `isom_add_audio_description()`. |
| **Box Reader** | `core/read.c` | Fixed `isom_box_common_copy()` `stsd->version` copy bug; implemented `read_pcmC()` and `read_chnl()` (File-Local Static naming); registered `ISOM_CODEC_TYPE_IPCM_AUDIO` and `ISOM_CODEC_TYPE_FPCM_AUDIO` in `description_reader_table[]`; computed `audio->constBytesPerAudioPacket` when reading `pcmC`. |
| **Box Writer** | `core/write.c` | Fixed `isom_write_audio_description()` `data->version == 1 && (data->manager & LSMASH_QTFF_BASE)`; implemented `isom_write_srat()`, `write_pcmC()`, and `write_chnl()`; registered in `box_writer_table[]`. |
| **Box Printer** | `core/print.c` | Implemented `print_pcmC()` and `print_chnl()` and registered in `print_func_table[]`. |
| **Codec Description** | `codecs/description.{c,h}` | Implemented `description_set_isom_lpcm()` (`ipcm`/`fpcm` + mandatory `pcmC` + optional `chnl` + `srat` with `stsd->version = 1` / `AudioSampleEntryV1` when `frequency > UINT16_MAX`, plus automatic fallback conversion from QTFF LPCM summary); added structured/unstructured converters, `isom_compare_opaque_extensions()`, and `ipcm`/`fpcm` summary extraction in `isom_get_audio_summary()`. |
| **ISOM Core & Timeline**| `core/isom.c`, `core/timeline.c`, `core/summary.c` | Updated `isom_is_lpcm_audio()` and `isom_is_uncompressed_audio()` to recognize `ISOM_CODEC_TYPE_IPCM_AUDIO` and `ISOM_CODEC_TYPE_FPCM_AUDIO`; verified `timeline->bunch_list` aggregation across both non-fragmented `stbl` and multi-track fragmented `moof`/`traf`/`trun`. |
| **Fragment Engine** | `core/fragment.c` | Fixed `ISOM_TR_FLAGS_OPTIONAL_ROWS_PRESENT_MASK` check at lines 1225, 1367, 1640; fixed `useful_first_sample_flags` at line 948; fixed `rap->trun_number` at line 1598. |
| **WAVE Importer & CLI** | `importer/wave_imp.c`, `cli/muxer.c` | Added support for `WAVE_FORMAT_PCM` (`0x0001`), `WAVE_FORMAT_IEEE_FLOAT` (`0x0003`), and `WAVE_FORMAT_EXTENSIBLE` (`0xFFFE`) with integer/float subformats and `dwChannelMask` to CICP `definedLayout` mapping (`wave_imp_channel_mask_to_defined_layout`); added `ipcm`/`fpcm` display names in `cli/muxer.c`. |
