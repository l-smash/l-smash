# Memory Architecture & C99 Container Modernization Specification (`docs/specs/memory_architecture.md`)

## 1. Executive Summary & Historical Context

`lsmash_entry_list_t` / `lsmash_entry_t` (`common/list.{c,h}`) was originally introduced as a generic doubly-linked list to support edit-list (`elst`) splicing and arbitrary Box tree manipulation. Over time, it was reused for **all** collections in L-SMASH, including high-volume Sample Table boxes (`stts`, `ctts`, `stsz`, `stz2`, `stss`, `stps`, `sdtp`, `stsc`, `stco`/`co64`, `sgpd`, `sbgp`), Movie Fragment Track Runs (`trun->optional`, `tfra->list`, `sidx->list`), and Media Timelines (`timeline->info_list`, `timeline->bunch_list`, `timeline->chunk_list`).

### 1.1 The Cost of Doubly-Linked Lists at Sample Scale
On 64-bit targets (`LP64` / `LLP64`), each `lsmash_entry_t` node occupies **24 bytes**:
```c
struct lsmash_entry_tag
{
    lsmash_entry_t *next; /* 8 bytes */
    lsmash_entry_t *prev; /* 8 bytes */
    void           *data; /* 8 bytes */
};
```
Every sample table entry addition performs **two separate heap allocations**:
1. `lsmash_malloc(sizeof(entry_struct))` (typically 4 to 16 bytes for `stsz`, `stts`, `ctts`, `sdtp`, `stco`, `trun_optional_row`, etc.).
2. `lsmash_malloc(sizeof(lsmash_entry_t))` inside `lsmash_list_add_entry()` (24 bytes).

When accounting for standard C heap allocator metadata (16 bytes of header/alignment per `malloc` chunk on 64-bit glibc/jemalloc/Windows UCRT):
- A single 4-byte `isom_stsz_entry_t` or 1-byte `isom_sdtp_entry_t` consumes **$32\text{ B (data chunk)} + 40\text{ B (node chunk)} = 72\text{ bytes}$** of physical RAM — an **18x to 72x memory overhead**.
- For a 2-hour 60fps video track ($432,000$ samples), `stsz->list` + `sdtp->list` + `ctts->list` + `timeline->info_list` alone execute **$>3.4\text{ million}$ `malloc`/`free` calls** and consume **$>150\text{ MB}$** of fragmented heap nodes.
- **CPU Cache & Algorithmic Penalty**: Although `lsmash_entry_list_t` caches `last_accessed_entry` and `last_accessed_number` (`common/list.c:75-120`), any non-monotonic index lookup with distance $> \text{entry\_count}/2$ or alternating lookups forces $O(N)$ pointer chasing across non-contiguous heap lines.
  - **Historical Proof (`04e39f1`)**: Commit `04e39f1` (*"Optimize DTS calculation to fix linear access slowdown"*, `core/timeline.c:412-436`) was an emergency workaround because `isom_get_dts_from_info_list()` was called in composition order (non-monotonic sample numbers when B-frames are present), defeating `last_accessed_entry` when distance checks misfired and causing catastrophic $O(N^2)$ pointer-chasing stalls.

---

## 2. Sacred Layer Invariants (MUST NEVER BE BROKEN)

Before modifying any list structure in `core/`, two foundational architectural mechanisms in `core/box.{c,h}` and `core/box_default.{c,h}` must be preserved with 100% fidelity.

### 2.1 Invariant A: The `box_default` Null-Object Sentinel Pattern Pattern
- Every Box struct starts with `ISOM_BASE_BOX` (`core/box.h:65-80`).
- Every singleton child Box pointer inside a parent Box struct (e.g., `file->moov`, `moov->mvhd`, `trak->tkhd`, `trak->mdia`, `mdia->minf`, `minf->stbl`, `stbl->stsd`, `stbl->stts`, `stbl->ctts`, `stbl->stsz`, `stbl->stsc`, `stbl->stco`, `traf->tfhd`, `traf->tfdt`) is initialized via `isom_init_box_fields()` (`core/box_default.c`) to point to a static immutable sentinel struct (`def_*`) whose `manager` field has `LSMASH_NON_EXISTING_BOX` set.
- `LSMASH_IS_NON_EXISTING_BOX(box)` checks `((box)->manager & LSMASH_NON_EXISTING_BOX)` without requiring NULL checks on intermediate parent pointers:
  ```c
  /* Safe even if trak, mdia, minf, or stbl is a non-existing sentinel! */
  if( LSMASH_IS_NON_EXISTING_BOX( trak->mdia->minf->stbl->stsd ) )
  ```
- Every allocated Box stores:
  - `box->nonexist_ptr`: pointer to the static default sentinel (`def_*`) for its concrete type.
  - `box->offset_in_parent`: `offsetof(parent_type, child_member)` recording the byte offset within its `parent` struct where the child pointer (or child list) resides.

### 2.2 Invariant B: Dual-List Ownership (`extensions` vs. Typed Child Lists) & `offset_in_parent` Restoration
Every Box instance has an embedded `lsmash_entry_list_t extensions` field (`ISOM_BASE_BOX`). Child boxes are managed through a strict single-ownership protocol (`core/box.c:520-625`):

```
Parent Box (e.g., isom_moov_t)
  |
  +-- extensions (lsmash_entry_list_t)  [OWNER: eliminator = isom_remove_extension_box]
  |     |
  |     +--> [entry] ---> isom_mvhd_t (Singleton Child)
  |     +--> [entry] ---> isom_trak_t #1 (List Child)
  |     +--> [entry] ---> isom_trak_t #2 (List Child)
  |
  +-- mvhd (isom_mvhd_t *) -------------> points to isom_mvhd_t
  |     (offset_in_parent = offsetof(isom_moov_t, mvhd))
  |
  +-- trak_list (lsmash_entry_list_t)   [NON-OWNER VIEW: eliminator = isom_do_nothing]
        (offset_in_parent = offsetof(isom_moov_t, trak_list))
        |
        +--> [entry] ---> points to isom_trak_t #1
        +--> [entry] ---> points to isom_trak_t #2
```

When `isom_remove_box_by_itself(box)` (`core/box.c:592-624`) destroys a Box:
1. If `box->manager & LSMASH_IN_PREDEFINED_LIST` is set:
   - It locates the typed list on the parent using `(lsmash_entry_list_t *)((int8_t *)parent + box->offset_in_parent)`.
   - It iterates that `lsmash_entry_list_t` and calls `lsmash_list_remove_entry_direct(list, entry)` (`core/box.c:606`), which frees **only the `lsmash_entry_t` node** (`eliminator == isom_do_nothing`).
2. Otherwise (Singleton Predefined Box):
   - `isom_Predefined_Box_Restore(parent, box)` (`core/box.c:563-570`) writes `box->nonexist_ptr` back into `*(void **)((int8_t *)parent + box->offset_in_parent)` if it still points to `box`.
3. Finally, `isom_remove_box_in_extension_list(parent, box)` (`core/box.c:576-589`) finds `box` in `parent->extensions` and calls `lsmash_list_remove_entry_direct(&parent->extensions, entry)`, invoking `isom_remove_extension_box(box)` $\rightarrow$ `box->destruct(box)` $\rightarrow$ `isom_remove_all_extension_boxes(&box->extensions)` $\rightarrow$ `lsmash_free(box)`.

> [!IMPORTANT]
> **Hard Architectural Rule**:
> `box->extensions` and all predefined Box lists that use `LSMASH_IN_PREDEFINED_LIST` / `offset_in_parent` (`moov->trak_list`, `stsd->list`, `dref->list`, `stbl->sgpd_list`, `stbl->sbgp_list`, `moof->traf_list`, `traf->trun_list`, `traf->sgpd_list`, `traf->sbgp_list`, `tref->ref_list`, `udta->cprt_list`, `ilst->metaitem_list`, `mvex->trex_list`, `mfra->tfra_list`, `file->styp_list`, `file->sidx_list`, `file->moof_list`, `file->emsg_list`) have small cardinality ($1 \sim 10^2$) and are tightly coupled to `isom_remove_box_by_itself()` via `(lsmash_entry_list_t *)((int8_t *)parent + box->offset_in_parent)`. These Box-tree lists **must remain `lsmash_entry_list_t`**.

---

## 3. Complete Quantitative Audit of All `lsmash_entry_list_t` Instances

Every occurrence of `lsmash_entry_list_t` across `core/`, `codecs/`, and `importer/` has been audited and classified below by:
- **Storage Form**: Value-embedded (`lsmash_entry_list_t`) vs. Heap-allocated pointer (`lsmash_entry_list_t *`).
- **Element Type & Size**: Concrete C struct stored in `entry->data` and its `sizeof`.
- **Scale ($N$)**: Typical cardinality in real-world media files:
  - **Small ($S$)**: $1 \sim 10^2$ elements (Box trees, tracks, parameter sets).
  - **Medium ($M$)**: $10^2 \sim 10^4$ elements (Chunks, fragments, edit segments, roll groups).
  - **Large ($L$)**: $10^5 \sim 10^7$ elements (Per-sample tables, timelines, LPCM bunches).
- **Pointer Stability Requirement**: Whether external structs hold raw C pointers to elements inside the list across subsequent insertions or deletions.
- **Access & Mutation Pattern**: Append-only + sequential scan (`Seq`), 1-based random index lookup (`Idx`), tail removal (`PopTail`), mid-list removal (`MidDel`), bulk move (`Move`).

### 3.1 Box-tree pointer lists: Box Tree & Predefined Box Lists (Retain `lsmash_entry_list_t`)
| Container Field | Defined In | Element Type | Scale | Ptr Stability | Operations | Notes / Invariant Coupling |
| :--- | :--- | :--- | :---: | :---: | :--- | :--- |
| `box->extensions` | `core/box.h:70` (`ISOM_BASE_BOX`) | `isom_box_t *` (polymorphic) | $S$ | Yes | `Seq`, `PopTail`, `MidDel` (`direct`) | Sole owner of all child boxes (`isom_remove_extension_box`). |
| `file->styp_list` | `core/file.h:122` | `isom_styp_t *` | $S$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `file->sidx_list` | `core/file.h:123` | `isom_sidx_t *` | $S$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `file->moof_list` | `core/file.h:124` | `isom_moof_t *` | $S\sim M$ | Yes | `Seq`, `MidDel` | Holds 1 active `moof` during write; all `moof`s during read/dump. |
| `file->emsg_list` | `core/file.h:125` | `isom_emsg_t *` | $S$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `moov->trak_list` | `core/box.h:616` | `isom_trak_t *` | $S$ | Yes | `Seq`, `Idx`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `tref->ref_list` | `core/box.h:146` | `isom_tref_type_t *` | $S$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `dref->list` | `core/box.h:221` | `isom_dref_entry_t *` | $S$ | Yes | `Seq`, `Idx`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `stsd->list` | `core/box.h:240` | `isom_sample_entry_t *` | $S$ | Yes | `Seq`, `Idx`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `stbl->sgpd_list` | `core/box.h:595` | `isom_sgpd_t *` | $S$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `stbl->sbgp_list` | `core/box.h:596` | `isom_sbgp_t *` | $S$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `udta->cprt_list` | `core/box.h:609` | `isom_cprt_t *` | $S$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `ilst->metaitem_list`| `core/box.h:642` | `isom_metaitem_t *` | $S$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `mvex->trex_list` | `core/box.h:667` | `isom_trex_t *` | $S$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `moof->traf_list` | `core/box.h:739` | `isom_traf_t *` | $S$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `traf->trun_list` | `core/box.h:731` | `isom_trun_t *` | $S\sim M$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`); uses `tail->prev` (`core/fragment.c:1348`). |
| `traf->sgpd_list` | `core/box.h:732` | `isom_sgpd_t *` | $S$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `traf->sbgp_list` | `core/box.h:733` | `isom_sbgp_t *` | $S$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `mfra->tfra_list` | `core/box.h:769` | `isom_tfra_t *` | $S$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `root->print` | `core/file.h:158` | `isom_print_entry_t *` | $S\sim M$ | No | `Seq` | Created only during `lsmash_print_movie()`. |
| `root->file_list` | `core/file.h:159` | `lsmash_file_t *` | $S$ | Yes | `Seq`, `MidDel` | Predefined Box list (`offset_in_parent`). |
| `file->timeline` | `core/file.h:126` | `isom_timeline_t *` | $S$ | Yes | `Seq`, `Idx`, `MidDel` | One `isom_timeline_t` per track (`core/timeline.c:148`). |

### 3.2 High-volume paged tables: High-Volume Sample Table, Fragment & Timeline Lists (Primary Modernization Targets)
| Container Field | Defined In | Element Struct | `sizeof` | Scale | Ptr Stability Required? | Operations | Hot-Path Bottlenecks & Special Constraints |
| :--- | :--- | :--- | :---: | :---: | :---: | :--- | :--- |
| `stts->list` | `core/box.h:428` | `isom_stts_entry_t` | 8 B | $M\sim L$ | No (only `tail->data` modified in place) | `Seq`, `Tail` | Run-length encoded (`sample_count`, `sample_delta`), but reaches $10^5+$ in VFR streams. |
| `ctts->list` | `core/box.h:440` | `isom_ctts_entry_t` | 8 B | $L$ | No (only `tail->data` modified in place) | `Seq`, `Head`, `Tail` | Reaches $10^5\sim 10^6$ in B-frame video streams (almost 1 entry per sample). |
| `stsz->list` | `core/box.h:468` | `isom_stsz_entry_t` | 4 B | $L$ | No | `Seq`, `Head`, `Move` (`stz2`) | **1 entry per sample** for all variable-size video/audio tracks ($10^5\sim 10^7$). Moved to `stz2->list` in `core/isom.c:1988`. |
| `stz2->list` | `core/box.h:481` | `isom_stsz_entry_t` | 4 B | $L$ | No | `Seq`, `Head`, `Move` | Same element struct as `stsz->list`. |
| `stss->list` | `core/box.h:449` | `isom_stss_entry_t` | 4 B | $M\sim L$ | No | `Seq` | 1 entry per sync sample (IDR/keyframe). |
| `stps->list` | `core/box.h:458` | `isom_stps_entry_t` | 4 B | $M$ | No | `Seq` | 1 entry per partial sync sample. |
| `sdtp->list` | `core/box.h:498` | `isom_sdtp_entry_t` | 4 B | $L$ | No | `Seq` | **1 entry per sample** whenever sample dependency box is active ($10^5\sim 10^7$). |
| `stsc->list` | `core/box.h:510` | `isom_stsc_entry_t` | 12 B | $M\sim L$ | No (reads `tail->data`) | `Seq`, `Tail` | 1 entry per chunk run. |
| `stco->list` | `core/box.h:524` | `isom_stco_entry_t` (4B) / `isom_co64_entry_t` (8B) | 4 / 8 B | $M\sim L$ | No (reads `tail->data`) | `Seq`, `Tail` | 1 entry per media chunk; converted 32$\rightarrow$64-bit in `isom_convert_stco_to_co64()`. |
| `trun->optional` | `core/box.h:715` | `isom_trun_optional_row_t` | 16 B | $M\sim L$ | No | `Seq`, `Idx` (linear scan in `fragment.c:1140`) | Reaches $10^3\sim 10^5$ per fragment; currently scanned linearly in `isom_request_trun_optional_row()`! |
| `tfra->list` | `core/box.h:754` | `isom_tfra_location_time_entry_t` | 28 B | $M$ | No | `Seq`, `MidDel` (`direct` in `fragment.c:187, 225`) | 1 entry per fragment RAP; removes non-RAP entries during `isom_setup_tfra()`. |
| `sidx->list` | `core/box.h:788` | `isom_sidx_referenced_item_t` | 20 B | $M$ | No | `Seq` | 1 entry per subsegment. |
| `elst->list` | `core/box.h:168` | `isom_elst_entry_t` | 24 B | $S\sim M$ | No | `Seq`, `Idx` | Edit list segments (`lsmash_get_explicit_timeline_map`). |
| `sgpd->list` | `core/box.h:557` | `isom_rap_entry_t` (6B) / `isom_roll_entry_t` (6B) / `isom_Unknown_group_entry_t` | 6~16 B | $S\sim M$ | **YES (`tail->data`)** | `Seq`, `Idx`, `PopTail` | `cache->rap->random_access` holds a raw pointer to `sgpd->list->tail->data` across samples until deduplicated via `lsmash_list_remove_entry_tail(sgpd->list)` (`core/isom.c:3106, 3120`). |
| `sbgp->list` | `core/box.h:572` | `isom_group_assignment_entry_t` | 8 B | $M\sim L$ | **YES (`assignment`, `prev_assignment`)** | `Seq`, `Idx`, **`MidDel` (by index!)** | **CRITICAL HAZARD**: `cache->rap->assignment` and `cache->roll.pool` (`isom_roll_group_t.assignment`) hold raw C pointers into `sbgp->list` while `isom_deduplicate_roll_group()` (`core/isom.c:3331`) deletes entries by index! (See Section 3.4). |
| `cache->roll.pool` | `core/isom.h:61` | `isom_roll_group_t` | 48 B | $S\sim M$ | No | `Seq`, `Idx` (`tail`), `MidDel` (`direct`) | Queue of pending roll recovery groups awaiting distance determination (`core/isom.c:3317-3405`). |
| `fragment->pool` | `core/fragment.h:83` | `isom_sample_pool_t` | 24 B | $S\sim M$ | No | `Seq`, `Clear` | Pools chunk buffers for the active Movie Fragment (`core/fragment.c:1247`). |
| `timeline->info_list` | `core/timeline.c:55` | `isom_sample_info_t` | 48 B | $L$ | No | `Seq`, **`Idx` (heavy random access!)** | **1 entry per non-LPCM sample ($10^5\sim 10^7$)**; heavily queried by 1-based `sample_number` in `lsmash_get_sample_from_media_timeline()`, `lsmash_get_dts_from_media_timeline()`, etc. |
| `timeline->bunch_list` | `core/timeline.c:56` | `isom_lpcm_bunch_t` | 56 B | $M\sim L$ | No | `Seq`, **`Idx` (random access in `isom_get_bunch`)** | Run-length-grouped LPCM frames; queried by `bunch_number` in `isom_get_bunch()` (`core/timeline.c:350-375`). |
| `timeline->chunk_list` | `core/timeline.c:57` | `isom_portable_chunk_t` | 32 B | $M\sim L$ | **YES (`info.chunk`, `bunch.chunk`)** | `Seq`, `PopTail` (`timeline.c:1149`) | **CRITICAL HAZARD**: Every `isom_sample_info_t.chunk` and `isom_lpcm_bunch_t.chunk` holds a raw `isom_portable_chunk_t *` pointing into `timeline->chunk_list` (`core/timeline.c:1053, 1268`)! |

### 3.3 Small codec and importer lists: Codec, Summary & Importer Small Lists (Low Volume: $1 \sim 10^2$)
| Container Field | Defined In | Element Type | Scale | Operations | Notes |
| :--- | :--- | :--- | :---: | :--- | :--- |
| `summary->opaque->list` | `lsmash.h:1049` | `lsmash_codec_specific_t *` | $S$ | `Seq`, `Idx`, `MidDel` (`direct`) | Part of `lsmash_codec_specific_list_t` (`core/summary.c:51`). |
| `avcC->sequenceParameterSets->list`, `pictureParameterSets->list`, `sequenceParameterSetExt->list` | `codecs/h264.h:41` (`isom_dcr_ps_list_t`) | `isom_dcr_ps_entry_t *` | $S$ | `Seq`, `PopTail` | H.264 parameter sets (typically $1\sim 4$ entries). |
| `hvcC->parameter_arrays.ps_array[i].list` | `codecs/hevc.c:150` | `isom_dcr_ps_entry_t *` | $S$ | `Seq`, `PopTail` | HEVC VPS/SPS/PPS/SEI arrays (typically $1\sim 8$ entries). |
| `esds->ES->...` descriptor lists | `codecs/mp4sys.c:76-135` | `mp4sys_*_t *` | $S$ | `Seq`, `PopTail`, `MidDel` (`direct`) | MPEG-4 Systems descriptor tree (`mp4sys.c:268, 309`). |
| `chpl->list`, `keys->list`, `WMA` / `dts` / `description` temp lists | `core/box.h:281, 623`, `codecs/*.c` | Small metadata structs | $S$ | `Seq`, `Idx` | Chapter list ($<100$), metadata keys ($<100$). |
| `importer->summaries` | `importer/importer.h:57` | `lsmash_summary_t *` | $S$ | `Seq`, `Idx`, `MidDel` | 1 entry per elementary stream/track ($1\sim 16$). |
| `adts_imp`, `ac3_imp`, `dts_imp`, `mp3_imp`, `amr_imp`, `vc1_imp` temporary `au_list` | `importer/*_imp.c` | Small AU buffers | $S$ | `Seq` | Small lookahead/frame lists. |

---

### 3.4 Deep-Dive Analysis of Pointer Stability & Mid-List Deletion Hazards

Our audit uncovered three critical pointer-stability hazards that would cause **Use-After-Free (UAF)** or **silent data corruption** if a naive 1D `realloc`-based Flat Vector were dropped in:

#### Hazard 1: `timeline->chunk_list` Raw Pointers in `info_list` and `bunch_list`
In `lsmash_construct_timeline()` (`core/timeline.c:1053, 1268`):
```c
info.chunk = (isom_portable_chunk_t *)timeline->chunk_list->tail->data;
```
Every single `isom_sample_info_t` (in `timeline->info_list`) and `isom_lpcm_bunch_t` (in `timeline->bunch_list`) stores a raw C pointer `isom_portable_chunk_t *chunk` pointing directly to an element inside `timeline->chunk_list` while new chunks continue to be appended to `timeline->chunk_list`!
- If `timeline->chunk_list` stored `isom_portable_chunk_t` values in a 1D flat array that grew via `realloc()`, any reallocation of `chunk_list` would immediately invalidate `info.chunk` and `bunch.chunk` in all previously added samples!
- By contrast, a **Paged Block Array** (which allocates fixed-size pages of `isom_portable_chunk_t` and only `realloc`s the top-level directory of page pointers) guarantees **100% address stability** of every `isom_portable_chunk_t` element for the entire lifetime of `timeline->chunk_list`.

#### Hazard 2: `sbgp->list` Raw Pointers + Mid-List Deletion in `isom_deduplicate_roll_group()`
In `core/isom.c:3194-3540`:
- `isom_rap_group_t` (`cache->rap`) holds raw C pointers `group->assignment` and `group->prev_assignment` (`isom_group_assignment_entry_t *`) pointing directly to elements inside `sbgp->list`.
- `isom_roll_group_t` (stored in `cache->roll.pool`) holds `group->assignment` (`isom_group_assignment_entry_t *`) pointing directly to its corresponding entry inside `sbgp->list`.
- Worse still, examine `isom_deduplicate_roll_group()` (`core/isom.c:3312-3344`):
  ```c
  uint32_t current_group_number = sbgp->list->entry_count - pool->entry_count + 1;
  isom_group_assignment_entry_t *prev_assignment =
      (isom_group_assignment_entry_t *)lsmash_list_get_entry_data( sbgp->list, current_group_number - 1 );
  for( lsmash_entry_t *entry = pool->head; entry; )
  {
      isom_roll_group_t *group = (isom_roll_group_t *)entry->data;
      if( !group || !group->assignment )
          return LSMASH_ERR_INVALID_DATA;
      if( !group->delimited || group->described != ROLL_DISTANCE_DETERMINED )
          return 0;
      if( prev_assignment && prev_assignment->group_description_index == group->assignment->group_description_index )
      {
          /* Merge the current group with the previous. */
          lsmash_entry_t *next_entry = entry->next;
          prev_assignment->sample_count += group->assignment->sample_count;
          int err;
          if( (err = lsmash_list_remove_entry( sbgp->list, current_group_number )) < 0
           || (err = lsmash_list_remove_entry_direct( pool, entry ))               < 0 )
              return err;
          entry = next_entry;
      }
      ...
  ```
  Consider what happens when `pool` contains two groups: $G_1$ (at `sbgp->list` index $N-1$, `delimited == 1 && described == ROLL_DISTANCE_DETERMINED`) and $G_2$ (at `sbgp->list` index $N$, still undetermined: `!delimited` or `described != ROLL_DISTANCE_DETERMINED`):
  1. $G_1$ matches `prev_assignment->group_description_index`, so `lsmash_list_remove_entry(sbgp->list, N - 1)` deletes index $N-1$ from `sbgp->list`.
  2. Next iteration inspects $G_2$, sees `!group->delimited || group->described != ROLL_DISTANCE_DETERMINED`, and returns `0` early (`core/isom.c:3324`).
  3. $G_2$ remains alive in `pool` with its raw pointer `G2->assignment`!
  - If `sbgp->list` were a contiguous array (or even a Paged Block Array that shifted elements left on mid-array deletion), deleting index $N-1$ would shift $G_2$'s entry from index $N$ to index $N-1$, leaving `G2->assignment` pointing to the vacated tail slot (one past the end of `sbgp->list`)!
  - **Resolution for `sbgp->list`**: When migrating `sbgp->list` to the Paged Block Array (`lsmash_Lw_table` / `lsmash_paged_array`), `isom_rap_group_t` and `isom_roll_group_t` must either:
    1. Store the 0-based entry index (`uint32_t assignment_index`) instead of a raw pointer `isom_group_assignment_entry_t *assignment`, decrementing `g->assignment_index` for remaining entries in `pool` after `entry` when `current_group_number` is merged; **or**
    2. Update `next->assignment` pointers in `pool` when compacting the tail of `sbgp->list` (note that `pool` only ever holds the last `pool->entry_count` entries at the tail of `sbgp->list`, usually 1–3 entries).

#### Hazard 3: `stsz->list` $\rightarrow$ `stz2->list` Ownership Transfer (`lsmash_list_move_entries`)
In `isom_compress_sample_size_table()` (`core/isom.c:1988`), `lsmash_list_move_entries(stz2->list, stsz->list)` transfers all `isom_stsz_entry_t` entries from `stsz->list` to `stz2->list` in $O(1)$ time by swapping list head/tail pointers. Since both `stsz->list` and `stz2->list` store identical `uint32_t entry_size` (`isom_stsz_entry_t`), any new table container used for `stsz->list` and `stz2->list` must support $O(1)$ move/swap semantics.

---

## 4. Architectural Comparison & Optimal C99 Container Design

We evaluate the three candidate architectures against L-SMASH's quantitative requirements:

| Evaluation Criterion | Legacy `lsmash_entry_list_t` | ① Flat Vector (1D `realloc`) | ② Paged Block Array ($2^K$ chunks) | ③ Hybrid: Paged Block Array (Tables) + `lsmash_entry_list_t` (Box Tree) |
| :--- | :--- | :--- | :--- | :--- |
| **Random Access Complexity** | $O(N)$ worst-case (B-frame / non-monotonic) | $O(1)$ (`data[i]`) | $O(1)$ (`pages[i >> K][i & mask]`) | **$O(1)$ for all Sample/Timeline tables** |
| **Memory Overhead ($10^6$ `stsz` entries)** | ~64–72 MB (2 `malloc`s/entry) | ~4–8 MB (1 buffer, up to 2x slack) | **~4.01 MB** (exact 64 KB pages, zero `realloc` copy) | **~4.01 MB (significant reduction)** |
| **Pointer Stability on Append** | Yes | **NO** (`realloc` invalidates all pointers!) | **YES** (existing pages never move in memory!) | **YES (100% stable on append)** |
| **Large Allocation Failure Risk** | Low (tiny allocations) | **High** (contiguous multi-MB `realloc` can fail or copy tens of MBs) | **Zero** (fixed 16 KB–64 KB page allocations; only tiny pointer directory grows) | **Zero** |
| **`box_default` & `offset_in_parent` Compatibility** | Native | Breaks `isom_remove_box_by_itself` if applied to Box lists | Breaks `isom_remove_box_by_itself` if applied to Box lists | **100% Compatible (Box tree keeps `lsmash_entry_list_t`)** |

### 4.1 Selected Architecture: Option ③ (Hybrid + Paged Block Array with C99 Unified Accessors)

1. **Retain `lsmash_entry_list_t` for Box-tree pointer lists & C (Box Trees, Codec Parameter Sets, Importer Summaries)**:
   - Zero risk to `box_default` (`LSMASH_NON_EXISTING_BOX`), `parent->extensions` ownership, and `offset_in_parent` pointer restoration in `core/box.c`.
2. **Introduce C99 Paged Block Array (`lsmash_block_list_t` / `lsmash_table_t`) for High-volume paged tables (Sample Tables, Fragment Tables, and Media Timelines)**:
   - Stores homogeneous elements of `elem_size` bytes in fixed-capacity pages of $2^K$ elements (e.g., $K = 10 \Rightarrow 1024$ elements per page, or $K = 12 \Rightarrow 4096$ elements per page).
   - **Data Layout**:
     ```c
     #define LSMASH_TABLE_PAGE_SHIFT 10U
     #define LSMASH_TABLE_PAGE_SIZE  (1U << LSMASH_TABLE_PAGE_SHIFT) /* 1024 elements/page */
     #define LSMASH_TABLE_PAGE_MASK  (LSMASH_TABLE_PAGE_SIZE - 1U)

     typedef struct
     {
         void   **pages;          /* Dynamic directory of page pointers */
         uint32_t entry_count;    /* Total number of active elements */
         uint32_t page_count;     /* Number of allocated pages */
         uint32_t page_capacity;  /* Capacity of the pages[] pointer directory */
         uint32_t elem_size;      /* sizeof(element) in bytes */
     } lsmash_Regular_table_t; /* (Concrete naming finalized in the implementation) */
     ```
   - **$O(1)$ Branchless Element Lookup (0-based or 1-based)**:
     ```c
     static inline void *lsmash_table_get( const lsmash_Regular_table_t *table, uint32_t index_1based )
     {
         if( !table || index_1based == 0 || index_1based > table->entry_count )
             return NULL;
         uint32_t idx = index_1based - 1U;
         return (uint8_t *)table->pages[idx >> LSMASH_TABLE_PAGE_SHIFT]
              + ((size_t)(idx & LSMASH_TABLE_PAGE_MASK) * table->elem_size);
     }
     ```
   - **Key Properties**:
     - **Zero Per-Entry `malloc`**: Only 1 `malloc` per 1,024 entries (a **99.9% reduction** in heap allocator calls).
     - **Zero `lsmash_entry_t` Node Overhead**: Saves 24 bytes + 32 bytes of heap headers per entry.
     - **100% Pointer Stability on Append**: Existing pages are never `realloc`ed or moved when new pages are appended. `timeline->chunk_list` pointers (`info.chunk`, `bunch.chunk`), `sgpd->list->tail` pointers (`cache->rap->random_access`), and `stts`/`ctts`/`stsc` tail pointers remain valid across arbitrary appends.
     - **$O(1)$ Tail Access & Tail Pop**: Getting or popping the tail element simply accesses `lsmash_table_get(table, table->entry_count)` and decrements `entry_count` without freeing the page (keeping capacity warm for subsequent appends).
     - **$O(1)$ Move (`stsz` $\rightarrow$ `stz2`)**: Swapping or moving the `lsmash_Regular_table_t` struct fields transfers all pages in $O(1)$ time.
     - **Cache-Line Sequential Iteration**: A C99 cursor/iterator macro walks contiguous elements within each page `0 .. LSMASH_TABLE_PAGE_SIZE - 1`, maximizing hardware L1/L2 prefetch efficiency during `core/write.c`, `core/print.c`, `core/isom.c` bitrate calculation, and `core/timeline.c` construction.

### 4.2 Build System Constraint (`configure` Symbol Version Script)
During our audit of `configure` (line 428), we identified how `liblsmash.ver` is generated for shared libraries:
```sh
sed -n 's/^lsmash_.*(/&/p; s/^struct lsmash_.* \*lsmash_.*(/&/p; s/^unsigned lsmash_.*(/&/p; s/^void \*lsmash_.*(/&/p; s/^void lsmash_.*(/&/p; s/^int lsmash_.*(/&/p; s/^int64_t lsmash_.*(/&/p; s/^uint8_t \*lsmash_.*(/&/p; s/^uint8_t lsmash_.*(/&/p; s/^uint16_t lsmash_.*(/&/p; s/^uint32_t lsmash_.*(/&/p; s/^uint64_t lsmash_.*(/&/p; s/^char \*lsmash_.*(/&/p; s/^double lsmash_.*(/&/p; s/^lsmash_entry_list_t \*lsmash_.*(/&/p; s/^lsmash_entry_t \*lsmash_.*(/&/p' $SRCDIR/common/*.h $SRCDIR/importer/*.h
```
- **Rule**: Any non-inline function declared in `common/*.h` whose name starts with `lsmash_` and whose return type matches the `sed` expression above is automatically added to `liblsmash.ver` as a global exported symbol! If the function is `static inline`, its line begins with `static inline`, so `sed` ignores it. If a non-inline internal helper in `common/*.h` uses a new return type (or is `static inline`), we must ensure `liblsmash.ver` never exports non-existent symbols or fails to link `--version-script,liblsmash.ver`.

---

## 5. Phased Migration Plan

To guarantee zero regressions and preserve bisectability, the container modernization will be executed in three self-contained phases:

### the implementation: Container Primitive & Synthetic Unit Tests (`common/`)
1. Implement the C99 Paged Block Array container and unified iterator/accessor macros in `common/` (with `static inline` hot-path accessors for $O(1)$ lookup, tail access, append-by-value/append-slot, tail pop, and $O(1)$ move).
2. Add comprehensive synthetic unit tests verifying $10^6$-element appends, pointer stability across page boundaries, random access, tail pop, and move semantics.

### the implementation: Timeline & Fragment Optional Row Migration (`core/timeline.c`, `core/fragment.c`)
1. Migrate `timeline->info_list` (`isom_sample_info_t`), `timeline->bunch_list` (`isom_lpcm_bunch_t`), and `timeline->chunk_list` (`isom_portable_chunk_t`) in `core/timeline.c`.
   - Simplifies `isom_get_dts_from_info_list()`, `isom_get_cts_from_info_list()`, and `isom_get_bunch()` to direct $O(1)$ array indexing (`lsmash_table_get`), eliminating the $O(N)$ linear fallback and `last_accessed_entry` cursor fragility (`04e39f1`).
2. Migrate `trun->optional` (`isom_trun_optional_row_t`), `tfra->list`, and `sidx->list` in `core/fragment.c` (`core/box.h`, `core/read.c`, `core/write.c`, `core/print.c`).
   - Replaces the $O(N)$ linear loop in `isom_request_trun_optional_row()` (`core/fragment.c:1140`) with $O(1)$ indexed lookup.

### the implementation: Sample Table (`stbl`) Migration (`core/box.h`, `core/isom.c`, `core/read.c`, `core/write.c`, `core/print.c`)
1. Migrate pure value-table boxes: `stts->list`, `ctts->list`, `stsz->list`, `stz2->list`, `stss->list`, `stps->list`, `sdtp->list`, `stsc->list`, `stco->list`/`co64->list`, and `elst->list`.
2. Migrate `sbgp->list` and `sgpd->list`, converting `isom_rap_group_t` and `isom_roll_group_t` in `core/isom.c` to index-safe `sbgp` references so `isom_deduplicate_roll_group()` is 100% immune to mid-list deletion pointer invalidation.
