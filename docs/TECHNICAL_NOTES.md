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
   - **Container-Level Rescue (`elst`)**: Because the corrupted leading B-frames satisfy `CTS < CTS(I_open)`, setting the Track Edit List (`elst`) `start_time` to `CTS(I_open)` (as `remuxer` does with `safe-seek`) instructs the player to decode from `I_open` in `DTS` order while suppressing presentation of all frames prior to `start_time`, hiding the corrupted leading B-frames.

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
In QuickTime File Format (`.mov`), `SoundDescriptionV1` (`version == 1`) and `SoundDescriptionV2` (`version == 2`) in `codecs/description.c` follow strict structural invariants verified against the Apple QuickTime File Format Specification (`qtff.pdf`) and Apple Core Media Audio / QuickTime hardware ground-truth captures across 5 canonical configurations (`ALAC` 16-bit 1ch/2ch, `AAC` `mp4a` 1ch/2ch, and `ALAC` 24-bit 2ch):

1. **Fixed `bytesPerFrame = 2` Law in Variable-Compression `SoundDescriptionV1` (`version == 1`, `compression_ID == -2`)**:
   - In variable-length compressed audio (`compression_ID == QT_AUDIO_COMPRESSION_ID_VARIABLE_COMPRESSION`, `-2`, where `summary->bytes_per_frame == 0`), the packet byte sizes vary per sample and are stored in the `stsz` sample size table (`packet_size = 0`).
   - Apple Core Media Audio / QuickTime populates the 16-byte `SoundDescriptionV1` extension header using an implicit fixed template value **`bytesPerFrame = 2`** and derives **`bytesPerPacket = bytesPerFrame / channelcount`** (`= 2 / channelcount`):
     - **ALAC 16-bit 1ch (`48 kHz`)**: `version = 1`, `channelcount = 1`, `samplesize = 16`, `compression_ID = -2`, `samplesPerPacket = 4096`, `bytesPerPacket = 2`, `bytesPerFrame = 2`, `bytesPerSample = 2`.
     - **ALAC 16-bit 2ch (`48 kHz`)**: `version = 1`, `channelcount = 2`, `samplesize = 16`, `compression_ID = -2`, `samplesPerPacket = 4096`, `bytesPerPacket = 1`, `bytesPerFrame = 2`, `bytesPerSample = 2`.
     - **AAC `mp4a` 1ch (`48 kHz`)**: `version = 1`, `channelcount = 1`, `samplesize = 16`, `compression_ID = -2`, `samplesPerPacket = 1024`, `bytesPerPacket = 2`, `bytesPerFrame = 2`, `bytesPerSample = 2`.
     - **AAC `mp4a` 2ch (`48 kHz`)**: `version = 1`, `channelcount = 2`, `samplesize = 16`, `compression_ID = -2`, `samplesPerPacket = 1024`, `bytesPerPacket = 1`, `bytesPerFrame = 2`, `bytesPerSample = 2`.
   - Accordingly, across all three variable-compression `SoundDescriptionV1` setup paths (`description_set_qtff_mp4a_description()`, the `bytes_per_frame == 0` branch of `description_set_qtff_template_audio_description()`, and the `QT_AUDIO_COMPRESSION_ID_VARIABLE_COMPRESSION` branch of `description_setup_audio_description()`), L-SMASH sets `audio->bytesPerFrame = 2; audio->bytesPerPacket = audio->channelcount ? audio->bytesPerFrame / audio->channelcount : 0; audio->bytesPerSample = 2;`. On the read side, `lsm_description_create_audio_summary()` treats `bytesPerFrame` (`2`) and `bytesPerPacket` (`2 / channelcount`) as template placeholders when `compression_ID == -2`, preserving `summary->channels = audio->channelcount`, `summary->sample_size = audio->samplesize`, `summary->samples_in_frame = audio->samplesPerPacket`, and `summary->bytes_per_frame = 0` so remuxing preserves variable compression.

2. **`bytesPerSample` in Fixed vs. Variable Compression and Why 8-bit Variable-Compression `version = 1` Codecs Do Not Exist**:
   - `qtff.pdf` defines `bytesPerSample` in `SoundDescriptionV1` as: *"This is set to 1 for 8-bit audio, 2 for all other cases, even if the sample size is greater than 2 bytes."*
   - In the QuickTime / Core Media Audio ecosystem, no variable-compression (`compression_ID == -2`) codec uses `8`-bit `SoundDescriptionV1`: transform/subband lossy codecs (`AAC`, `MP3`, `AC-3`, etc.) have no intrinsic PCM bit depth and conventionally set `samplesize = 16` (`bytesPerSample = 2`), `ALAC` only defines `16`/`20`/`24`/`32`-bit bit depths (no `8`-bit mode exists in the ALAC specification, and `> 16`-bit uses `version = 2`), `FLAC` (`dfLa`) uses ISOBMFF `AudioSampleEntry` `version = 0`, and legacy `8`-bit compressed formats (`MACE 3:1`/`6:1`, `a-Law`, `u-Law`) are fixed-ratio codecs (`compression_ID = -1`).
   - Consequently, fixed-compression (`bytes_per_frame != 0`) `SoundDescriptionV1` uses `audio->bytesPerSample = 1 + (sample_size != 8);`, while all variable-compression (`compression_ID == -2`) `SoundDescriptionV1` paths uniformly set `audio->bytesPerSample = 2;` (also ensuring `bytesPerSample == 2` even if a track was tentatively initialized with an 8-bit sample size prior to applying `LSMASH_CODEC_SPECIFIC_DATA_TYPE_QT_AUDIO_COMMON`).

3. **Promotion to `SoundDescriptionV2` (`version = 2`) for Bit Depths Exceeding 16 Bits (`sample_size > 16`)**:
   - Because `SoundDescriptionV1` caps `samplesize` at `16` and `bytesPerSample` at `2`, audio formats with bit depths exceeding 16 bits (`sample_size > 16`, such as `20`/`24`/`32`-bit `ALAC`) cannot represent their source bit depth in `SoundDescriptionV1` header fields.
   - Apple Core Media Audio / QuickTime therefore promotes `> 16`-bit `ALAC` tracks (even when `channels <= 2` and `frequency <= 65535`) to **`SoundDescriptionV2` (`version = 2`)**, setting `channelcount = 3`, `samplesize = 16`, `compression_ID = -2`, `packet_size = 0`, `samplerate = 0x00010000` (`65536`), `sizeOfStructOnly = 72`, `audioSampleRate = 48000.0` (IEEE 754 64-bit float), `numAudioChannels = channels`, `always7F000000 = 0x7F000000`, `constBitsPerChannel = 0`, `formatSpecificFlags = QT_ALAC_FORMAT_FLAG_24BIT_SOURCE_DATA` (`0x00000003` for 24-bit; `0x01`/`0x02`/`0x04` for `16`/`20`/`32`-bit), `constBytesPerAudioPacket = 0`, and `constLPCMFramesPerAudioPacket = 4096`.
   - `description_set_qtff_template_audio_description()` promotes `audio->version = 2` whenever `sample_size > 16` (in addition to `summary->channels > 2 || summary->frequency > UINT16_MAX`), ensuring both newly multiplexed and remuxed `20`/`24`/`32`-bit `ALAC` tracks emit `SoundDescriptionV2` with the exact `QT_ALAC_FORMAT_FLAG_*BIT_SOURCE_DATA` flag.

---

## 3. `mdhd.duration`, Decoding Tail Stretching (`stts`) & 3-Stage Hybrid VFR Final Frame Duration

### 3.1 Presentation Span vs. Decoding Timeline Tail Stretching (`core/track.c`, `core/timeline.c`)
1. **Presentation Span (`mdhd.duration`)**:
   - The track media duration `mdhd.duration` represents the total presentation span on the Composition timeline:
     ```text
     mdhd.duration = (max_cts - min_cts) + D_comp
     ```
     where `D_comp` is the Composition duration of the last displayed frame. This ensures the exact presentation duration even under Implicit timeline mapping (when no Edit List `elst` is present).
2. **Decoding Tail Stretching (`stts` final `sample_delta`)**:
   - ISO/IEC 14496-12 requires both `sum(stts) == mdhd.duration` (i.e., `last_dts + final_stts_delta == mdhd.duration`) and `DTS <= CTS` (when `cslg` is absent).
   - In B-frame reordered streams where `(max_cts - min_cts) > last_dts` (such as VFR or `--dts-compression` streams), `track_update_mdhd_duration()` (`core/track.c`) stretches the final `sample_delta` of the Decoding timeline (`stts`) to:
     ```text
     final_stts_delta = mdhd.duration - last_dts = D_comp + (max_cts - min_cts - last_dts)
     ```
     Regardless of how long the final Decoding timeline duration becomes, it merely represents the tail interval during which no subsequent frame is pulled from the decode buffer and has zero adverse impact on presentation or decoding.
3. **Remux Idempotency (`lsm_timeline_construct`)**:
   - When constructing a Media Timeline from an existing container (`core/timeline.c`), `lsm_timeline_construct()` keeps the raw `stts` entry duration in `last_info->duration` (so `lsmash_get_media_timestamps()` and `boxdumper --timestamp` reflect the exact `stts` table) while recovering the unstretched Composition duration `D_comp = sum(stts) - (max_cts - min_cts)` in `timeline->last_sample_delta` (returned by `lsmash_get_last_sample_delta_from_media_timeline()`).
   - Passing `lsmash_get_last_sample_delta_from_media_timeline()` to `lsmash_flush_pooled_samples()` during remultiplexing is therefore 100% idempotent and never double-adds `(max_cts - min_cts - last_dts)`.

### 3.2 3-Stage Hybrid Determination of Final Frame Composition Duration (`D_comp`)
To prevent a localized long-duration still frame at index `N - 2` from inflating the final frame `N - 1` in VFR streams, L-SMASH determines `D_comp` using a 3-stage hybrid strategy across `cli/timelineeditor.c`, `cli/lshmuxer.c`, and `core/track.c`:
1. **Stage 1 (Timecode v2 Explicit End Timestamp `t_N`)**: If a Timecode v2 file for an `N`-sample track provides an `(N + 1)`-th timestamp line `t_N > t_(N - 1)` (`ts[N] > ts[N - 1]`), `t_N` is treated as the explicit end timestamp of the final frame and `D_comp = t_N - t_(N - 1)` (`ts[N] - ts[N - 1]`).
2. **Stage 2 (Timecode v1 `Assume <FPS>` & Sequence Range Evaluation)**: In Timecode v1, `parse_timecode()` evaluates frame intervals through sample `N - 1` (`i < sample_count`) to populate `timecode_array[sample_count]`, ensuring frame `N - 1` uses its own sequence FPS or `Assume <FPS>` rather than copying frame `N - 2`'s duration.
3. **Stage 3 (Statistical Still-Frame Outlier Guard Fallback)**: When no explicit end timestamp is available (`N`-line Timecode v2 or automatic estimation with `last_sample_delta == 0` in `track_update_mdhd_duration()`), the preceding composition interval `delta_prev = max_cts - max2_cts` (or `ts[N - 1] - ts[N - 2]`) is compared against the lower median `D_med` of all composition intervals (`deltas[(num_deltas - 1) / 2]` for `N >= 3`). If `delta_prev > 4 * D_med` and `D_med > 0`, `D_comp` falls back to `D_med`.

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
   - Consequently, an ISOBMFF or QTFF file **cannot simultaneously express distinct priming trim offsets** for an AAC-LC fallback decoder and a full SBR/PS decoder. When multiplexing HE-AAC (`--sbr`), `elst.start_time` (`--encoder-delay`) must be configured for the target full HE-AAC decoder's total delay.

---

## 5. Negative POC / Leading Pictures & Composition-to-Decode Timeline Shift (`cslg` / `ctts` v1)

In ITU-T H.264/AVC and later codecs (HEVC, AV1), presentation order is governed by Picture Order Count (`POC`). Even in a Closed GOP, pictures decoded *after* the initial IDR picture at decoding index `i = 0` (`POC = 0`) can have a negative POC (`POC < 0`) and therefore be presented *before* `timestamp[0]` (the same holds for leading B-pictures following an Open-GOP CRA/BLA/I picture):

    Decoding order (i):       i = 0 (IDR, POC = 0)     i = 1 (P/B, POC = -2)    i = 2 (P, POC = +4) ...
                              DTS = 0, CTS = ts[1]     DTS = 40, CTS = ts[0]=0  DTS = 80, CTS = ts[3]
    Composition order (m):    m = 0 (decoded at i=1)   m = 1 (decoded at i=0)   m = 2 (decoded at i=3) ...

1. **Why Subtracting `timestamp[0].cts` Underflows `uint64_t` in Decoding Order**:
   - During timeline editing (`cli/timelineeditor.c:edit_media_timeline` and `cli/lshmuxer.c:edit_track_timeline`), timestamps are first sorted into composition order (`m = 0 .. N-1`) and shifted by the uniform offset `sample_delay_time = timecode->ts[sample_delay]` so that sample `i` (with composition index `c(i)`) receives `CTS = timecode->ts[c(i)] + sample_delay_time`.
   - After sorting back into decoding order (`lsmash_sort_timestamps_decoding_order`), `timestamp[0]` is decoding-order sample `0` with `c(0) > 0`, whereas a subsequent sample `k > 0` has `c(k) = 0` and `timestamp[k].cts = sample_delay_time < timestamp[0].cts`.
   - Subtracting `timestamp[0].cts` from every `timestamp[i].cts` when re-editing a track with `orig_ctd_shift > 0` (`!dts_compression`) would therefore underflow unsigned `uint64_t` on sample `k`. Subtracting `sample_delay_time` directly yields `timestamp[i].cts == timecode->ts[c(i)] >= 0` for all samples (`min_i CTS_i == 0`) and allows `lsmash_set_media_timestamps()` to derive the exact `timeline->ctd_shift = sample_delay_time`.

2. **Minimum-CTS Scan for `--shift-timeline` (`cli/lshmuxer.c:adjust_timestamp`)**:
   - For the same reason, when `lshmuxer --shift-timeline` (`muxer->timeline_shift`) shifts composition timestamps so that the earliest presentation starts at `CTS = 0`, `out_track->ctd_shift` must be initialized to the minimum adjusted CTS (`min_k (CTS_k - skip_dt_interval)`) across the input media timeline rather than decoding-order sample 1's `sample->cts`.

3. **Fragmented MP4 (`moof` / `trun` Version 1) Per-Run Version Promotion & Pre-Mux Edit List Invariants**:
   - **Per-Run `trun->version = 1` Promotion (`core/fragment.c:fragment_update_sample_tables`)**: Unlike a non-fragmented track's single `stbl->ctts` box, a Fragmented MP4 allocates a new `isom_trun_t` (initialized with `version = 0`) for every Movie Fragment (`moof`) and Track Run (`trun`), whereas `cache->timestamp.ctd_shift` is a persistent track-wide high-water mark. Promoting `trun->version = 1` only when `(sample->cts + ctd_shift) < sample->dts` would leave `trun->version == 0` in all subsequent fragments whose negative composition offsets (`sample->cts < sample->dts`) do not exceed an earlier fragment's `ctd_shift`, silently serializing negative two's-complement offsets as unsigned 32-bit integers. Each `trun` must therefore be promoted to `version = 1` whenever any sample in that run satisfies `sample->cts < sample->dts` (under ISO Base Media version 6 / `iso6`), while updating `ctd_shift` whenever `(sample->dts - sample->cts) > ctd_shift`.
   - **Underflow-Safe `timeline->ctd_shift` Derivation & Fragmented Edit List Copy (`core/timeline.c`)**: During `lsm_timeline_construct()`, `timeline->ctd_shift` is derived directly from the magnitude of signed negative offsets (`(int32_t)offset < 0` in `ctts` v1/QTFF or `trun` v1) rather than `uint64_t cts = dts + (int32_t)offset`. Furthermore, when `lsmash_copy_timeline_map()` copies an Edit List into a fragmented destination whose initial `moov` has 0 samples (`dst_file->fragment && !lsm_stbl_get_sample_count( dst_trak )`), `stbl->cslg` does not exist because the initial `moov` is written before any `moof` fragments are muxed; defaulting `dst_ctd_shift` to `src_ctd_shift` prevents `media_time_shift = src_ctd_shift - dst_ctd_shift` from spuriously adding `+src_ctd_shift` to the copied `elst` `media_time`.
   - **Pre-Mux `start_offset` & `elst` Shift in `cli/lshmuxer.c` (`construct_timeline_maps`)**: Because `construct_timeline_maps()` runs prior to `do_mux()` when `--fragment` is active, `lshmuxer` pre-scans the input media timeline from `in_track->current_sample_number` to compute `start_offset` and `frag_ctd_shift` using the exact same rules as `adjust_timestamp()`, and shifts copied non-empty `elst` `start_time` entries down by `frag_ctd_shift` when fragmenting an unshifted container input with `--shift-timeline`.
