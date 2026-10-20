# L-SMASH Project History & Architectural Evolution (`docs/HISTORY.md`)

## 1. Repository Lineage & Branch Topology

L-SMASH was created in 2010 by Yusuke Nakamura (`muken.the.vfrmaniac@gmail.com`) as a clean, specification-loyal ISOBMFF and QTFF library and toolset under the ISC License.

### 1.1 Key Historical Branches Audited at Project Revival
| Branch / Ref | Tip Commit | Summary of State |
| :--- | :---: | :--- |
| `master` (`origin/master`) | `18a9ed2` | Last official upstream `master` commit (2016-12-23: *"osdep: add missing fallback definitions of fseeko and ftello"*). |
| `av1` (`origin/av1`) | `96065d0` | Official WIP branch by Yusuke Nakamura (2 commits ahead of `master`: `2bd228c` *"[WIP] av1: Add Support Alliance for Open Media AV1"* and `96065d0` *"[WIP] Add IVF importer for AV1"*), containing a 1,747-line native AV1 OBU parser in `codecs/av1.{c,h}` and IVF importer in `importer/ivf_imp.c`. |
| `origin/memcheck` | `d549d80` | Experimental branch by Yusuke Nakamura introducing `common/memint.h` (`lsmash_mem_alloc_B` checked allocation wrappers). |
| `vimeo` (`vimeo/master`) | `4a21f21` | Community fork (49 commits ahead of `master`) adding Opus (`dOps`), Spherical Video v1/v2 (`st3d`, `sv3d`), HDR metadata (`mdcv`, `clli`), ProRes, RTP Hint Track authoring, `srat` Box reader/printer, and an external `obuparse`-based AV1 implementation (`codecs/av1_obu.{c,h}`). |
| `dev` | Branched from `4a21f21` | Active development branch for the official L-SMASH revival by original author Yusuke Nakamura. |

---

## 2. Notable Historical Commits & Architectural Lessons

1. **Commit `04e39f1` (`2017-01-31`, `core/timeline.c`): *"Optimize DTS calculation to fix linear access slowdown"***
   - Demonstrated that `lsmash_entry_list_t`'s single `last_accessed_entry` cursor degrades to $O(N)$ linear pointer-chasing per sample ($O(N^2)$ overall) when sample queries arrive out of monotonic order (such as B-frame CTS-ordered lookups in `isom_get_dts_from_info_list()`).
   - Serves as direct empirical motivation for Subsystem's migration of high-volume sample and timeline lists (`info_list`, `bunch_list`, `chunk_list`, `stts`, `ctts`, `stsz`, `sdtp`, `trun->optional`) to $O(1)$ Paged Block Arrays.
2. **Commit `9bc5dc3` (`2017-02-23`, `core/box.c`): *"Update fullbox_type_table size"***
   - Expanded `static lsmash_box_type_t fullbox_type_table[50]` to `fullbox_type_table[62]` while adding 12 FullBoxes (`svhd`, `proj`, `prhd`, `cbmp`, `equi`, `mshp`, `st3d`, `GULL`, `SAND`, `stvi`, `alac`, `srat`), leaving **60 FullBoxes + 1 terminator = 61 used slots out of 62 (only 1 free slot)**.
   - Confirmed the latent buffer-overflow hazard when adding the 2 FullBoxes required for ISO/IEC 23003-5 LPCM (`pcmC` and `chnl`).
3. **Commits `2bd228c` & `96065d0` (`2017-04-28` / `2018-09-21`, `origin/av1`) vs. `ff5e5fe` (`2019-06-14`, `vimeo`)**
   - While `vimeo` introduced an external dependency on `obuparse` (`codecs/av1_obu.{c,h}`), the official `av1` branch already contained the original author's native OBU bitstream parser (`codecs/av1.{c,h}`) and IVF importer (`importer/ivf_imp.c`), requiring only targeted bitstream bug fixes (`av1_get_uvlc`, `av1_get_leb128`, `av1_uncompressed_header`, and `av1C` `twelve_bit` propagation) to achieve complete dependency-free AV1 support.

---

## 3. Revival Milestones

### the implementation: Project Revival, Branch Establishment & Full Read-Only Audit
- Established `dev` branch from `vimeo` (`4a21f21`) and installed local workspace/session protection hooks (`commit-msg`, `pre-commit`, `.gitignore`, `.ignore`, remote push lock).
- Executed a comprehensive Read-Only audit across `common/`, `core/`, `codecs/`, `importer/`, `cli/`, and build scripts (`configure`, `Makefile`).
- Authored the initial hierarchical specification suite:
  - `SPEC.md`
  - `docs/specs/memory_architecture.md`
  - `docs/specs/av1_obuparse_removal.md`
  - `docs/specs/lpcm_isobmff_fragmented.md`
  - `docs/specs/testing_strategy.md`
  - `docs/HISTORY.md`
