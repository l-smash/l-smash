# Container & Codec Specification Technical Notes (`docs/TECHNICAL_NOTES.md`)

This document collects in-depth technical notes on subtle specification boundaries between ISO/IEC & ITU-T elementary streams and ISOBMFF / QuickTime File Format (QTFF) containers.

---

## 1. Open-GOP Random Access & Edit List Coordination: HEVC vs. AVC

An **Open-GOP** random access point (RAP) is an intra-coded picture (`I` / `CRA` / `BLA`) followed in decoding order (`DTS`) by **leading pictures** -- B-frames whose `DTS` is *after* the intra picture, but whose composition time (`CTS`) is *before* the intra picture and which predict from reference pictures in the preceding GOP. When random access starts directly at an Open-GOP RAP, those leading pictures lack their backward reference pictures and cannot be decoded properly:

    Decoding order (DTS):      ... [P_prev]  [I_open / CRA]  [B_lead1]  [B_lead2]  [P_next] ...
                                                   |            (broken on random access)
    Composition order (CTS):   ... [P_prev]  [B_lead1]  [B_lead2]  [I_open / CRA]  [P_next] ...
                                             |<-- prior to RAP -->| |<-- Edit List start_time

1. **HEVC (`hvc1` / `hev1` -- Decoder-Level Suppression)**:
   - ITU-T H.265 | ISO/IEC 23008-2 distinguishes undecodable **RASL** (`RASL_N`, `RASL_R`) pictures from decodable **RADL** (`RADL_N`, `RADL_R`) pictures in the NAL unit header.
   - When decoding starts at a **CRA** or **BLA** picture, the HEVC specification sets `NoRaslOutputFlag = 1`, mandating that the **decoder itself** discard and suppress output of all associated `RASL` pictures automatically.
   - Because the HEVC decoder guarantees clean output without container intervention, ISO/IEC 14496-15 classifies HEVC `IDR`, `CRA`, and `BLA` pictures alike as **Sync Samples** (`stss`).

2. **H.264 / AVC (`avc1`-`avc4` -- Container-Level Rescue via Edit List `elst`)**:
   - ITU-T H.264 | ISO/IEC 14496-10 provides no `NoRaslOutputFlag` equivalent for non-IDR Open-GOP `I` pictures. Seeking directly to an Open-GOP `I` picture without presentation filtering causes the decoder to output macroblock-corrupted leading B-frames.
   - Consequently, in AVC only true **IDR** pictures belong in `stss`, while Open-GOP `I` pictures are signaled via `'rap '` or `'roll'` sample groups (`ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP`).
   - **Container-Level Rescue (`elst`)**: Because the corrupted leading B-frames satisfy `CTS < CTS(I_open)`, setting the Track Edit List (`elst`) `start_time` (`media_time`) to `CTS(I_open)` (as `lshmuxer` does with `safe-seek`) instructs the player to decode from `I_open` in `DTS` order while suppressing presentation of all frames prior to `start_time`, hiding the corrupted leading B-frames.

---

## 2. Media Description (`stsd`) vs. Presentation Control (`trak`) Hierarchy

ISOBMFF and QTFF strictly separate **Media Description** (inside `moov.trak.mdia.minf.stbl.stsd`) from **Presentation Control** (inside `moov.trak.tkhd` and `moov.trak.tapt`):

    +-------------------------------------------------------------------------+
    | Media Layer (VisualSampleEntry in stsd)                                 |
    |   - Coded pixel dimensions: VisualSampleEntry.width / height            |
    |   - pasp (PixelAspectRatioBox): physical shape of one decoded pixel     |
    |   - clap (CleanApertureBox):    active picture region free of artifacts |
    |   - colr (ColourInformationBox): primaries, transfer curve, matrix      |
    |   - fiel (FieldInfoBox, QTFF):  interlaced field count and ordering     |
    +-------------------------------------------------------------------------+
                                        |
            (Decoded pixels handed to the container compositor)
                                        v
    +-------------------------------------------------------------------------+
    | Presentation Layer (Track Header tkhd & QTFF Track Aperture tapt)       |
    |   - tkhd.width / height (16.16 fixed-point): actual canvas display size |
    |   - tkhd.matrix: 3x3 affine transformation (rotation, scaling, flip)    |
    |   - tapt (QTFF only): clef (clean), prof (production), enof (encoded)   |
    +-------------------------------------------------------------------------+

### 2.1 Why `pasp`, `clap`, and `colr` Do Not Drive Layout by Themselves
- `pasp`, `clap`, and `colr` are child boxes of a Visual Sample Entry in `stsd`. They describe the intrinsic properties of the decoded pixel buffer produced by that sample description, not how the track is sized on the movie canvas.
- Presentation geometry is controlled at the **Track (`trak`)** level by `tkhd.width`, `tkhd.height`, and `tkhd.matrix` (plus `tapt` in QuickTime). When configuring a video track, `tkhd.width` must be scaled by `pasp` (`hSpacing / vSpacing`) -- and in QTFF mode `tapt` (`clef`, `prof`, `enof`) must be populated -- for the compositor to render the intended aspect ratio and aperture.

### 2.2 Presentation-Layer Asymmetry Between ISOBMFF and QTFF
- **QTFF (`.mov`)** defines the Track Aperture Mode Dimensions atom (`moov.trak.tapt`), exposing Clean Aperture (`clef`), Production Aperture (`prof`), and Encoded Pixels (`enof`) at the Track level.
- **ISOBMFF (`.mp4`)** has no `tapt` box and provides **no Track-level Box to control or override Clean Aperture (`clap`) cropping or Colour Information (`colr`)** at the Presentation layer.

### 2.3 QuickTime `'fiel'` (`FieldInfoBox`) and Ice Floe Dispatch 19's Magic Numbers (`0, 1, 6, 9, 14`)
The QuickTime `'fiel'` extension consists of two 8-bit fields: `fields` (`1` = progressive, `2` = interlaced) and `detail` (field ordering). Defined in **Apple QuickTime Ice Floe Dispatch 19**, `detail` uses the non-contiguous constants **`0, 1, 6, 9, 14`** (`QT_FIELD_ORDERINGS_*` in `lsmash.h`):
- **`0` (`UNKNOWN`)**: Unknown / progressive (`fields == 1`).
- **`1` (`TEMPORAL_TOP_FIRST`)**: Temporal top-field-first (top field coded and displayed first).
- **`6` (`TEMPORAL_BOTTOM_FIRST`)**: Temporal bottom-field-first (bottom field coded and displayed first).
- **`9` (`SPATIAL_FIRST_LINE_EARLY` = `1 + 8`)**: Interleaved single-frame where the top scanline belongs to the temporally earlier field (Top Field First, TFF).
- **`14` (`SPATIAL_FIRST_LINE_LATE` = `6 + 8`)**: Interleaved single-frame where the top scanline belongs to the temporally later field (Bottom Field First, BFF).

### 2.4 QuickTime Sound Sample Description v1/v2 (`stsd`): Variable-Compression `bytesPerFrame = 2` Law, `bytesPerSample`, and `version = 2` Promotion for `> 16`-bit Audio
In QuickTime File Format (`.mov`), `SoundDescriptionV1` (`version == 1`) and `SoundDescriptionV2` (`version == 2`) in `stsd` (`codecs/description.c`) follow strict structural invariants verified against the Apple QuickTime File Format Specification (`qtff.pdf`) and Apple Core Media Audio / QuickTime hardware ground-truth captures across 5 canonical configurations (`ALAC` 16-bit 1ch/2ch, `AAC` `mp4a` 1ch/2ch, and `ALAC` 24-bit 2ch):

1. **Fixed `bytesPerFrame = 2` Law in Variable-Compression `SoundDescriptionV1` (`version == 1`, `compression_ID == -2`)**:
   - In variable-length compressed audio (`compression_ID == -2`, `QT_AUDIO_COMPRESSION_ID_VARIABLE_COMPRESSION`, where uncompressed frame byte size is variable and reported as `0` in `lsmash_audio_summary_t::bytes_per_frame`), the compressed packet byte sizes vary per sample and are stored in the `stsz` sample size table (`packet_size = 0`).
   - Apple Core Media Audio / QuickTime populates the 16-byte `SoundDescriptionV1` extension header using an implicit fixed template value **`bytesPerFrame = 2`** and derives **`bytesPerPacket = bytesPerFrame / channelcount`** (`= 2 / channelcount`):
     - **ALAC 16-bit 1ch (`48 kHz`)**: `version = 1`, `channelcount = 1`, `samplesize = 16`, `compression_ID = -2`, `samplesPerPacket = 4096`, `bytesPerPacket = 2`, `bytesPerFrame = 2`, `bytesPerSample = 2`.
     - **ALAC 16-bit 2ch (`48 kHz`)**: `version = 1`, `channelcount = 2`, `samplesize = 16`, `compression_ID = -2`, `samplesPerPacket = 4096`, `bytesPerPacket = 1`, `bytesPerFrame = 2`, `bytesPerSample = 2`.
     - **AAC `mp4a` 1ch (`48 kHz`)**: `version = 1`, `channelcount = 1`, `samplesize = 16`, `compression_ID = -2`, `samplesPerPacket = 1024`, `bytesPerPacket = 2`, `bytesPerFrame = 2`, `bytesPerSample = 2`.
     - **AAC `mp4a` 2ch (`48 kHz`)**: `version = 1`, `channelcount = 2`, `samplesize = 16`, `compression_ID = -2`, `samplesPerPacket = 1024`, `bytesPerPacket = 1`, `bytesPerFrame = 2`, `bytesPerSample = 2`.
   - Accordingly, when constructing a variable-compression `SoundDescriptionV1` (`version == 1`, `compression_ID == -2`), L-SMASH (`codecs/description.c`) populates `bytesPerFrame = 2`, `bytesPerPacket = 2 / channelcount` (when `channelcount > 0`), and `bytesPerSample = 2`. On the read side (`lsmash_get_summary()`), when `compression_ID == -2`, L-SMASH treats `bytesPerFrame` (`2`) and `bytesPerPacket` (`2 / channelcount`) as template placeholders rather than fixed PCM frame sizes, preserving `channels = channelcount`, `sample_size = samplesize`, `samples_in_frame = samplesPerPacket`, and `bytes_per_frame = 0` in `lsmash_audio_summary_t` so that remultiplexing retains variable compression.

2. **`bytesPerSample` in Fixed vs. Variable Compression and Why 8-bit Variable-Compression `version = 1` Codecs Do Not Exist**:
   - `qtff.pdf` defines `bytesPerSample` in `SoundDescriptionV1` as: *"This is set to 1 for 8-bit audio, 2 for all other cases, even if the sample size is greater than 2 bytes."*
   - In the QuickTime / Core Media Audio ecosystem, no variable-compression (`compression_ID == -2`) codec uses `8`-bit `SoundDescriptionV1`: transform/subband lossy codecs (`AAC`, `MP3`, `AC-3`, etc.) have no intrinsic PCM bit depth and conventionally set `samplesize = 16` (`bytesPerSample = 2`), `ALAC` only defines `16`/`20`/`24`/`32`-bit bit depths (no `8`-bit mode exists in the ALAC specification, and `> 16`-bit uses `version = 2`), `FLAC` (`dfLa`) uses ISOBMFF `AudioSampleEntry` `version = 0`, and legacy `8`-bit compressed formats (`MACE 3:1`/`6:1`, `a-Law`, `u-Law`) are fixed-ratio codecs (`compression_ID = -1`).
   - Consequently, fixed-compression (`bytes_per_frame != 0`) `SoundDescriptionV1` entries set `bytesPerSample = 1` for 8-bit audio and `bytesPerSample = 2` for all other bit depths, while all variable-compression (`compression_ID == -2`) `SoundDescriptionV1` entries unconditionally set `bytesPerSample = 2`.

3. **Promotion to `SoundDescriptionV2` (`version = 2`) for Bit Depths Exceeding 16 Bits (`sample_size > 16`)**:
   - Because `SoundDescriptionV1` caps `samplesize` at `16` and `bytesPerSample` at `2`, audio formats with bit depths exceeding 16 bits (`sample_size > 16`, such as `20`/`24`/`32`-bit `ALAC`) cannot represent their source bit depth in `SoundDescriptionV1` header fields.
   - Apple Core Media Audio / QuickTime therefore promotes `> 16`-bit `ALAC` tracks (even when `channels <= 2` and `frequency <= 65535`) to **`SoundDescriptionV2` (`version = 2`)**, setting `channelcount = 3`, `samplesize = 16`, `compression_ID = -2`, `packet_size = 0`, `samplerate = 0x00010000` (`65536`), `sizeOfStructOnly = 72`, `audioSampleRate = 48000.0` (IEEE 754 64-bit float), `numAudioChannels = channels`, `always7F000000 = 0x7F000000`, `constBitsPerChannel = 0`, `formatSpecificFlags = QT_ALAC_FORMAT_FLAG_24BIT_SOURCE_DATA` (`0x00000003` for 24-bit; `0x01`/`0x02`/`0x04` for `16`/`20`/`32`-bit), `constBytesPerAudioPacket = 0`, and `constLPCMFramesPerAudioPacket = 4096`.
   - L-SMASH (`codecs/description.c`) promotes the QuickTime Sound Sample Description to `SoundDescriptionV2` (`version = 2`) whenever `sample_size > 16` (in addition to `channels > 2` or `frequency > 65535`), ensuring both newly multiplexed and remultiplexed `20`/`24`/`32`-bit `ALAC` tracks emit `SoundDescriptionV2` with the exact `QT_ALAC_FORMAT_FLAG_*BIT_SOURCE_DATA` flag.

---

## 3. `mdhd.duration`, Decoding Tail Stretching (`stts`) & 3-Stage Hybrid VFR Final Frame Duration

### 3.1 Presentation Span vs. Decoding Timeline Tail Stretching (`core/track.c`, `core/timeline.c`)
1. **Presentation Span (`mdhd.duration`)**:
   - The track media duration `mdhd.duration` in the Media Header Box (`mdhd`) represents the total presentation span on the Composition timeline—from the minimum composition timestamp (`min_cts`, the earliest displayed sample's `CTS`) to the end of the maximum composition timestamp (`max_cts`, the last displayed sample's `CTS`), plus the **composition duration of the final displayed frame**:
     ```text
     mdhd.duration = (max_cts - min_cts) + final_frame_composition_duration
     ```
     This guarantees the exact presentation duration even under Implicit timeline mapping (when no Edit List `elst` is present).
2. **Decoding Tail Stretching (`stts` final `sample_delta`)**:
   - ISO/IEC 14496-12 requires the sum of all `stts` `sample_delta` entries (`sum(stts)`) to equal `mdhd.duration`, and requires `DTS <= CTS` when `cslg` is absent.
   - Let `last_dts` denote the decoding timestamp of the final sample in decoding order (the cumulative sum of all preceding `stts` `sample_delta` entries). In B-frame reordered streams where `(max_cts - min_cts) > last_dts` (such as VFR streams or streams with compressed initial `DTS`), L-SMASH (`core/track.c` during `lsmash_update_track_duration()`) stretches the final `sample_delta` of the Decoding timeline (`stts`) by the reorder tail difference `(max_cts - min_cts - last_dts)`:
     ```text
     final_stts_sample_delta = mdhd.duration - last_dts
                             = final_frame_composition_duration + (max_cts - min_cts - last_dts)
     ```
     Regardless of how long the final `stts` `sample_delta` becomes, it merely represents the tail interval during which no subsequent frame is pulled from the decode buffer and has zero adverse impact on presentation or decoding.
3. **Remux Idempotency (`core/timeline.c`)**:
   - When constructing a Media Timeline (`lsmash_construct_timeline()`) from an existing non-fragmented container with `ctts`, L-SMASH keeps each sample's raw `stts` `sample_delta` for per-sample timestamp inspection (`lsmash_get_media_timestamps()` and `boxdumper --timestamp`) while recovering the unstretched final-frame composition duration `sum(stts) - (max_cts - min_cts)` as the Media Timeline's default final sample delta (`lsmash_get_last_sample_delta_from_media_timeline()`).
   - Passing `lsmash_get_last_sample_delta_from_media_timeline()` to `lsmash_flush_pooled_samples()` during remultiplexing is therefore 100% idempotent and never adds the reorder tail `(max_cts - min_cts - last_dts)` a second time.

### 3.2 3-Stage Hybrid Determination of the Final Frame's Composition Duration
To prevent a localized long-duration still frame at composition index `N - 2` from inflating the final displayed frame `N - 1` in an `N`-frame VFR stream (composition indices `0` through `N - 1`), L-SMASH determines the final frame's composition duration using a 3-stage hybrid strategy (`cli/lshmuxer.c`, `cli/timelineeditor.c`, and `core/track.c`):
1. **Explicit End Timestamp in Matroska Timecode Format v2**: If a Timecode v2 file for an `N`-sample track provides an optional `(N + 1)`-th timestamp line strictly greater than the `N`-th timestamp line, that `(N + 1)`-th line specifies the explicit presentation end timestamp of frame `N - 1`, and the final frame's composition duration is the exact difference between the `(N + 1)`-th and `N`-th timestamps.
2. **Sequence Frame Rate Evaluation in Matroska Timecode Format v1**: In Timecode v1, frame intervals are evaluated through the final frame `N - 1` using either the active `start,end,<FPS>` range covering frame `N - 1` or the baseline `Assume <FPS>`, ensuring frame `N - 1` receives its own sequence frame rate duration rather than copying frame `N - 2`'s duration.
3. **Statistical Still-Frame Outlier Guard Fallback**: When no explicit end timestamp is available (an `N`-line Timecode v2 file, or automatic duration calculation when `last_sample_delta == 0` is passed to `lsmash_update_track_duration()`), L-SMASH compares the last composition interval (between the final two displayed frames, `N - 1` and `N - 2`) against the **lower median** of all `N - 1` adjacent composition intervals in the track (for `N >= 3`). If the last composition interval exceeds `4` times the lower median (and the lower median is positive), the final frame's composition duration falls back to the lower median interval.

---

## 4. Multi-Stage Filterbank Delays in HE-AAC v1/v2 & Edit List Limits

Overlapped transform and subband audio codecs introduce **priming delay** at the start of the stream. In **HE-AAC v1** (AAC-LC + Spectral Band Replication, SBR) and **HE-AAC v2** (AAC-LC + SBR + Parametric Stereo, PS), delays accumulate across three cascaded filterbanks:

    [PCM Input]
        |
        v
    +-------------------------------------------------------------------+
    | Stage 1: AAC-LC Core (MDCT 50% overlap + encoder lookahead)       |
    |   Delay = D_core (e.g., 1024 MDCT + 576/1088 lookahead samples)   |
    +-------------------------------------------------------------------+
        |  (Core decoded at base sampling rate fs_core, e.g., 24 kHz)
        v
    +-------------------------------------------------------------------+
    | Stage 2: SBR Decoder (64-band Dual-Rate QMF Analysis + Synthesis) |
    |   Delay = 2 * D_core + D_sbr_qmf (e.g., +962 samples at 2*fs_core)|
    +-------------------------------------------------------------------+
        |  (Upsampled to 2 * fs_core, e.g., 48 kHz)
        v
    +-------------------------------------------------------------------+
    | Stage 3: PS Decoder (Hybrid Subband Filterbank on low QMF bands)  |
    |   Delay = 2 * D_core + D_sbr_qmf + D_ps_hybrid                    |
    +-------------------------------------------------------------------+

1. **Decoder-Dependent Total Delay**:
   - Because HE-AAC is backwards-compatible, a legacy AAC-LC-only decoder executes only Stage 1 (experiencing delay `D_core` at `fs_core`), an HE-AAC v1 decoder executes Stages 1 + 2 (experiencing `2 * D_core + D_sbr_qmf` at `2 * fs_core`), and an HE-AAC v2 decoder executes Stages 1 + 2 + 3 (adding `D_ps_hybrid`).

2. **Structural Limit of ISOBMFF / QTFF Edit Lists (`elst`)**:
   - As documented in the Apple QuickTime File Format Specification (*Audio Priming - Handling Encoder Delay in AAC*, `qtff.pdf`) and ISO/IEC 14496-24, an Edit List entry (`elst`) contains only **one** scalar `start_time` (`media_time`) field per track, and the `'roll'` sample group contains only one `roll_distance`.
   - Consequently, an ISOBMFF or QTFF file **cannot simultaneously express distinct priming trim offsets** for an AAC-LC fallback decoder and a full SBR/PS decoder. When multiplexing HE-AAC (`?sbr`), `elst.start_time` (`?encoder-delay=<n>`) must be configured for the target full HE-AAC decoder's total delay.

---

## 5. Negative POC / Leading Pictures & Composition-to-Decode Timeline Shift (`cslg` / `ctts` v1)

In ITU-T H.264/AVC and later codecs (HEVC, AV1), presentation order is governed by Picture Order Count (`POC`). Even in a Closed GOP, pictures decoded *after* the initial IDR picture at decoding index `i = 0` (`POC = 0`) can have a negative POC (`POC < 0`) and therefore be presented *before* decoding-order sample `0` (the same holds for leading B-pictures following an Open-GOP CRA/BLA/I picture):

    Decoding order (i):       i = 0 (IDR, POC = 0)     i = 1 (P/B, POC = -2)    i = 2 (P, POC = +4) ...
                              DTS = 0, CTS = ts[1]     DTS = 40, CTS = ts[0]=0  DTS = 80, CTS = ts[3]
    Composition order (m):    m = 0 (decoded at i=1)   m = 1 (decoded at i=0)   m = 2 (decoded at i=3) ...

1. **Why Subtracting the First Decoded Sample's `CTS` Underflows Unsigned `uint64_t` in Decoding Order**:
   - When rewriting a track's timeline from a timecode sequence (`cli/lshmuxer.c`, `cli/timelineeditor.c`), samples are first sorted into composition order (`m = 0 .. N-1`) and assigned new composition timestamps shifted by the initial reorder delay offset (`CTS_m = ts[m] + initial_reorder_delay`).
   - Once the samples are sorted back into decoding order (`lsmash_sort_timestamps_decoding_order()`), decoding-order sample `0` (`i = 0`, `POC = 0`) is **not** the earliest displayed sample whenever a later decoding-order sample `k > 0` has a negative POC (`m = 0`), meaning sample `k` has a smaller `CTS` than sample `0` (`CTS_k < CTS_0`).
   - When removing the initial reorder delay offset on a track that uses composition-to-decode timeline shifting (`cslg` / signed negative `ctts` v1 offsets, without `--dts-compression`), subtracting decoding-order sample `0`'s `CTS` (`CTS_0`) from every sample would therefore underflow unsigned `uint64_t` on sample `k`. Subtracting the known initial reorder delay offset (`minimum_CTS` across all samples) directly ensures every sample's adjusted `CTS` equals `ts[m] >= 0` (`min CTS == 0`) and allows `lsmash_set_media_timestamps()` to record the exact `cslg.compositionToDTSShift`.

2. **Minimum-CTS Scan for `--shift-timeline` (`cli/lshmuxer.c`)**:
   - For the same reason, when `lshmuxer --shift-timeline` shifts a track's composition timestamps so that the earliest displayed frame starts at `CTS = 0`, the composition-to-decode shift (`cslg.compositionToDTSShift`) must be computed by scanning the entire media timeline for the **minimum `CTS`** across all samples, rather than taking the `CTS` of the first sample in decoding order.

3. **Fragmented MP4 (`moof` / `trun` Version 1) Per-Run Version Promotion & Pre-Mux Edit List Invariants**:
   - **Per-Run `trun` Version 1 Promotion (`core/fragment.c`)**: Unlike a non-fragmented track's single `stbl.ctts` box, a Fragmented MP4 writes a separate Track Run Box (`trun`, initialized at `version = 0`) inside each Movie Fragment (`moof`), while tracking the maximum composition-to-decode shift across the movie. If `trun` were promoted to `version = 1` (signed 32-bit `sample_composition_time_offset`) only when a sample's negative offset exceeded the running movie-wide maximum shift, any subsequent fragment whose negative composition offsets (`CTS < DTS`) were smaller than or equal to an earlier fragment's maximum shift would remain at `trun` `version = 0`, serializing negative two's-complement offsets as unsigned 32-bit integers. L-SMASH therefore promotes each individual `trun` box to `version = 1` whenever **any** sample in that run satisfies `CTS < DTS` (under ISO Base Media version 6 / `iso6`), while independently updating the movie-wide maximum composition-to-decode shift whenever `(DTS - CTS)` reaches a new high-water mark.
   - **Underflow-Safe Composition-to-Decode Shift Derivation & Fragmented Edit List Copy (`core/timeline.c`)**: When constructing a Media Timeline (`lsmash_construct_timeline()`), L-SMASH derives the track's composition-to-decode shift directly from the magnitude of signed negative composition offsets (`(int32_t)sample_offset < 0` in `ctts` v1/QTFF or `trun` v1) before computing unsigned `CTS` values. Furthermore, when `lsmash_copy_timeline_map()` copies an Edit List (`elst`) into a fragmented output movie whose initial `moov` contains zero samples (because all samples are written in subsequent `moof` fragments), `moov.trak.mdia.minf.stbl.cslg` does not exist yet; L-SMASH therefore preserves the source track's composition-to-decode shift as the baseline when copying `elst` `media_time` values so no spurious shift difference is added.
   - **Pre-Mux Edit List Alignment in Fragmented Mode (`cli/lshmuxer.c`)**: Because the initial `moov` (and its `edts.elst` Edit List) of a Fragmented MP4 must be written before the first `moof` fragment is multiplexed, `lshmuxer` pre-scans the input media timeline before multiplexing to determine the earliest composition timestamp and composition-to-decode shift, and adjusts copied non-empty `elst` `media_time` entries accordingly when fragmenting an unshifted container input with `--shift-timeline`.
