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
