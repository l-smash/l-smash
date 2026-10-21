# ISO/IEC 23003-5 LPCM & ISO/IEC 14496-12 Audio Signaling Specification (`docs/specs/lpcm_isobmff_fragmented.md`)

## 1. Overview & Scope

This specification defines L-SMASH's implementation of uncompressed Linear PCM (LPCM) audio and channel/sampling-rate signaling in **ISO Base Media File Format (ISOBMFF)** across both standard (`moov`/`stbl`) and **Fragmented (`moof`/`traf`/`trun`)** movies, in accordance with:
- **ISO/IEC 23003-5** — *Information technology — MPEG audio technologies — Part 5: Uncompressed audio in MPEG-4 File Format* (`ipcm`, `fpcm`, `pcmC`)
- **ISO/IEC 14496-12** — *ISO Base Media File Format* Section 12.2 (`AudioSampleEntry`, `AudioSampleEntryV1`, `chnl`, `srat`)
- **ISO/IEC 23091-3 (CICP)** — *Coding-independent code points — Part 3: Audio* (`definedLayout`, `speaker_position`)

### Strict Container Separation: ISOBMFF vs. QuickTime (QTFF)
L-SMASH strictly separates QuickTime (`.mov`) uncompressed audio from ISOBMFF (`.mp4`, `.m4a`, Fragmented MP4) uncompressed audio:
- **QuickTime (`file->qt_compatible != 0`)**: Uses Apple Sound Description v0/v1/v2 (`lpcm`, `sowt`, `twos`, `in24`, `in32`, `fl32`, `fl64`, `raw `), QuickTime `chan` atoms, and `wave` decompression atoms.
- **ISOBMFF (`!file->qt_compatible`)**: Uses ISO/IEC 23003-5 `ipcm` (integer PCM) and `fpcm` (IEEE 754 floating-point PCM) sample entries with mandatory `pcmC` (`PCMConfigBox`), optional ISO/IEC 14496-12 `chnl` (`ChannelLayout`), and ISOBMFF `AudioSampleEntryV1` (`version = 1`) + `srat` (`SamplingRateBox`) when the sampling rate exceeds $65535\text{ Hz}$.

---

## 2. Box Syntax & Binary Layout

### 2.1 `ipcm` / `fpcm` Sample Entries & `AudioSampleEntryV1` (`core/read.c`, `core/write.c`)
Per ISO/IEC 14496-12 Section 12.2.3:
- When `samplerate <= 65535 Hz` (`isom_audio_entry_t.version == 0`):
  - `channelcount` is set to the actual channel count (`channels`), `samplesize = 16` (template field), and `samplerate = frequency << 16`.
- When `samplerate > 65535 Hz` (e.g., $88.2\text{ kHz}$, $96\text{ kHz}$, $192\text{ kHz}$) in ISOBMFF (`!file->qt_compatible`):
  - `stsd` is promoted to `version = 1` (`AudioSampleEntryV1`), `entry->version = 1`, `entry->samplerate = 1 << 16`, and a child **`srat`** (`SamplingRateBox`) is appended containing the full 32-bit `sampling_rate`.
  - Unlike QuickTime Sound Description v1, ISOBMFF `AudioSampleEntryV1` does **not** include the 16-byte QuickTime fields (`samplesPerPacket`, `bytesPerPacket`, `bytesPerFrame`, `bytesPerSample`); child extension boxes (`pcmC`, `chnl`, `srat`) follow immediately after the 28-byte `AudioSampleEntry` header.

### 2.2 `pcmC` (`PCMConfigBox`, ISO/IEC 23003-5 Section 5.2)
`isom_pcmC_t` (`core/box.h`) is a FullBox (`version = 0, flags = 0`, 14 bytes total):
```c
typedef struct
{
    ISOM_FULLBOX_COMMON;
    uint8_t format_flags;    /* bit(0): 0 = big-endian, 1 = little-endian */
    uint8_t PCM_sample_size; /* bits per sample: 16, 24, or 32 for ipcm; 32 or 64 for fpcm */
} isom_pcmC_t;
```
**Validation Rules**:
- Mandatory for both `ipcm` and `fpcm`; `lsmash_add_sample_entry()` and `isom_read_pcmC()` reject missing or malformed `pcmC` boxes.
- `(format_flags & ~0x01) != 0` is rejected (`LSMASH_ERR_INVALID_DATA`).
- For `ipcm`, `PCM_sample_size` must be `16`, `24`, or `32`.
- For `fpcm`, `PCM_sample_size` must be `32` or `64`.

### 2.3 `chnl` (`ChannelLayout`, ISO/IEC 14496-12 Section 12.2.4.2)
`isom_chnl_t` (`core/box.h`) and public `lsmash_isom_channel_layout_t` (`lsmash.h`) preserve the exact syntax identifier names from ISO/IEC 14496-12:
```c
typedef struct
{
    uint8_t speaker_position;
    int16_t azimuth;
    int8_t  elevation;
} lsmash_isom_channel_position_t;

typedef struct
{
    uint8_t                        stream_structure;   /* bit 0 (0x01): channelStructured, bit 1 (0x02): objectStructured */
    uint8_t                        definedLayout;      /* ISO/IEC 23091-3 CICP ChannelConfiguration (0 = explicit positions) */
    uint64_t                       omittedChannelsMap; /* present when definedLayout != 0 */
    uint8_t                        object_count;       /* present when (stream_structure & 0x02) != 0 */
    uint16_t                       channel_count;      /* number of explicit positions when definedLayout == 0 */
    lsmash_isom_channel_position_t positions[64];
} lsmash_isom_channel_layout_t;
```
- When `stream_structure & LSMASH_CHANNEL_STREAM_STRUCTURE_CHANNEL` (`0x01`):
  - Reads/writes `uint8_t definedLayout`.
  - If `definedLayout == 0`: iterates `channel_count` times (derived from the enclosing `AudioSampleEntry.channelcount`), reading/writing `uint8_t speaker_position`, followed by `int16_t azimuth` and `int8_t elevation` whenever `speaker_position == 126` (`LSMASH_SPEAKER_POSITION_EXPLICIT`).
  - If `definedLayout != 0`: reads/writes `uint64_t omittedChannelsMap` (64-bit big-endian bitmask).
- When `stream_structure & LSMASH_CHANNEL_STREAM_STRUCTURE_OBJECT` (`0x02`):
  - Reads/writes `uint8_t object_count`.

### 2.4 `srat` (`SamplingRateBox`, ISO/IEC 14496-12 Section 12.2.3.2)
`isom_srat_t` (`core/box.h`) is a FullBox (`version = 0, flags = 0`, 16 bytes total) containing a 32-bit unsigned integer `uint32_t sampling_rate` serialized in big-endian (`lsmash_bs_put_be32( bs, srat->sampling_rate )`).

---

## 3. Standard & Multi-Track Fragmented ISOBMFF Multiplexing

### 3.1 Sample Frame Sizing
In ISO/IEC 23003-5, each ISOBMFF sample in an `ipcm` or `fpcm` track represents one uncompressed multi-channel PCM frame of duration $1$ tick at the media timescale ($f_s$), with constant byte size:
$$\text{bytes\_per\_frame} = \text{channels} \times \frac{\text{PCM\_sample\_size}}{8}$$

### 3.2 Multi-Track Fragmented MP4 (`moof`/`traf`/`trun`) Compaction (`core/fragment.c`)
When multiplexing one or more LPCM tracks into a Fragmented ISOBMFF movie (`LSMASH_FILE_MODE_FRAGMENTED`):
1. **Multi-Track `data_offset` Correctness (`isom_append_fragment_track_run`)**:
   - A `trun` box can only omit `ISOM_TR_FLAGS_DATA_OFFSET_PRESENT` if it is the first run of the track fragment AND its `traf` is currently the **last `traf` in `moof->traf_list.tail`**. Whenever samples are interleaved across multiple `traf` boxes in the same `moof`, `ISOM_TR_FLAGS_DATA_OFFSET_PRESENT` is explicitly set so each track fragment points to its exact byte offset in `mdat`.
2. **Automatic `tfhd` Default Hoisting & `trun->optional` Omission (`isom_flush_fragment_pooled_samples`)**:
   - When flushing a fragment's sample pool, L-SMASH inspects whether all samples in the `trun` share the same duration, byte size, and sample flags.
   - For constant-size, constant-duration audio such as LPCM, the common `duration` ($1$) and `size` ($\text{bytes\_per\_frame}$) are hoisted into `tfhd->default_sample_duration` and `tfhd->default_sample_size`, clearing the per-sample flags (`0x000f00`) on `trun->flags` and freeing `trun->optional` (`trun->optional = NULL`).
   - During demultiplexing (`core/timeline.c`), when `trun->optional` is empty or omits per-sample fields, timeline reconstruction falls back to `tfhd->default_sample_duration` / `trex->default_sample_duration` and `tfhd->default_sample_size` / `trex->default_sample_size`.

---

## 4. RIFF WAVE Importer (`importer/wave_imp.c`)

The WAVE importer (`importer/wave_imp.c`) supports:
- **`WAVE_FORMAT_PCM` (`0x0001`)**: 8-bit (QTFF), 16-bit, 24-bit, and 32-bit little-endian integer PCM.
- **`WAVE_FORMAT_IEEE_FLOAT` (`0x0003`)**: 32-bit and 64-bit little-endian IEEE 754 floating-point PCM.
- **`WAVE_FORMAT_EXTENSIBLE` (`0xFFFE`)**:
  - Parses `wValidBitsPerSample`, `dwChannelMask`, and the 16-byte `SubFormat` GUID (`KSDATAFORMAT_SUBTYPE_PCM` or `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT`).
  - Maps Microsoft `dwChannelMask` speaker bitmasks to ISO/IEC 14496-12 `chnl` (`definedLayout` CICP configurations for Mono `1`, Stereo `2`, 5.1ch `6`, 7.1ch `12`, or explicit `speaker_position` arrays for custom masks).
- **Automatic Target Container Adaptation**:
  - When multiplexing into an ISOBMFF container (`!file->qt_compatible`), `lsmash_add_sample_entry()` automatically maps little-endian integer/float LPCM summaries with `pcmC` / `chnl` into `ipcm` or `fpcm` sample entries (and promotes `frequency > 65535` to `AudioSampleEntryV1` + `srat`), while preserving `lpcm`/`sowt`/`in24`/`in32`/`fl32`/`fl64` when multiplexing into QuickTime `.mov`.
