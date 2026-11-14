# Memory Architecture & Container Specification (`docs/MEMORY_ARCHITECTURE.md`)

## 1. Overview & Motivation

Media container multiplexing and demultiplexing workloads exhibit two fundamentally different data-structure access patterns:
1. **Polymorphic Hierarchical Box Trees (`moov`, `trak`, `mdia`, `minf`, `stbl`, `moof`, `traf`, `extensions`)**:
   - Contain heterogeneous Box structs of varying byte sizes linked dynamically during file parsing, construction, and destruction.
   - Require stable struct addresses (`isom_box_t *`) that are cross-referenced by both parent singleton pointers and `parent->extensions` lists.
2. **High-Volume Homogeneous Sample & Timeline Tables (`stts`, `ctts`, `stsz`, `stz2`, `stss`, `stps`, `sdtp`, `stsc`, `stco`, `co64`, `sgpd`, `sbgp`, `elst`, `trun->optional`, `tfra`, `sidx`, `fragment->pool`, and `timeline` tables)**:
   - Store hundreds of thousands to millions of fixed-size, homogeneous value records (4 to 32 bytes each).
   - Previously, storing these tables in doubly-linked node lists (`lsmash_entry_list_t`) required **two separate heap allocations per entry** (`sizeof(lsmash_entry_t) == 24` bytes for the node plus `malloc(sizeof(entry))` for the payload, plus allocator metadata overhead) and degraded random-access lookup to `O(N)` pointer chasing whenever access patterns deviated from strictly sequential traversal.

To address both workloads cleanly in standard C11, L-SMASH employs a **Hybrid Container Architecture** paired with a built-in **C11 Debug Memory Auditor (`LSMASH_DEBUG_ALLOC`)**.

---

## 2. Hybrid Container Classification

### 2.1 Box-Tree Pointer Lists (`lsmash_entry_list_t`)
Polymorphic Box lists and dual-ownership Box registries continue to use `lsmash_entry_list_t` (`common/list.{c,h}`):
- `box->extensions` (all child/extension boxes attached to any `isom_box_t`)
- `root->file_list`, `file->moof_list`, `moov->trak_list`, `mvex->trex_list`, `moof->traf_list`, `traf->trun_list`, `mfra->tfra_list`
- `stsd->list` (polymorphic sample description entries), `stbl->sgpd_list`, `stbl->sbgp_list`, `traf->sgpd_list`, `traf->sbgp_list`
- `dref->list`, `udta->meta->ilst->item_list`, `WMA`/`QuickTime`/`ISOBMFF` extension box lists

These lists contain a small number of polymorphic heap-allocated Box structs where each node holds an owning or non-owning `void *data` pointer to a variable-sized `isom_box_t` derivative.

### 2.2 High-Volume Paged Tables (`lsm_list_t`)
All high-volume homogeneous value tables use the **C11 Paged Block Array (`lsm_list_t`)**:

| Subsystem | Parent Struct / Box | Field Name | Element Type | `sizeof(Element)` |
| :--- | :--- | :--- | :--- | :--- |
| **Sample Tables (`stbl`)** | `isom_stts_t` | `list` | `isom_stts_entry_t` | 8 bytes |
| | `isom_ctts_t` | `list` | `isom_ctts_entry_t` | 8 bytes |
| | `isom_stsz_t` | `list` | `isom_stsz_entry_t` | 4 bytes |
| | `isom_stz2_t` | `list` | `isom_stsz_entry_t` | 4 bytes |
| | `isom_stss_t` | `list` | `isom_stss_entry_t` | 4 bytes |
| | `isom_stps_t` | `list` | `isom_stps_entry_t` | 4 bytes |
| | `isom_sdtp_t` | `list` | `isom_sdtp_entry_t` | 1 byte |
| | `isom_stsc_t` | `list` | `isom_stsc_entry_t` | 12 bytes |
| | `isom_stco_t` (`stco`/`co64`) | `list` | `isom_stco_entry_t` / `isom_co64_entry_t` | 4 / 8 bytes |
| | `isom_sgpd_t` (`rap `, `roll`, `prol`) | `list` | `isom_rap_entry_t` / `isom_roll_entry_t` (`roll_distance` for `roll`/`prol`) | 2 bytes |
| | `isom_sbgp_t` | `list` | `isom_group_assignment_entry_t` | 8 bytes |
| **Edit List & Cache** | `isom_elst_t` | `list` | `isom_elst_entry_t` | 20 bytes |
| | `isom_cache_t` (`roll.pool`) | `pool` | `isom_roll_group_t` | 48 bytes |
| | `isom_fragment_manager_t` (`fragment->pool`) | `pool` | `lsmash_sample_t *` | 8 bytes |
| **Movie Fragments (`moof`/`sidx`/`mfra`)** | `isom_trun_t` | `optional` | `isom_trun_optional_row_t` | 16 bytes |
| | `isom_tfra_t` | `list` | `isom_tfra_location_time_entry_t` | 28 bytes |
| | `isom_sidx_t` | `list` | `isom_sidx_referenced_item_t` | 16 bytes |
| **Media Timeline (`timeline`)** | `isom_timeline_t` | `info_list` | `isom_sample_info_t` | 32 bytes |
| | `isom_timeline_t` | `bunch_list` | `isom_lpcm_bunch_t` | 56 bytes |
| | `isom_timeline_t` | `chunk_list` | `isom_portable_chunk_t` | 16 bytes |

### 2.3 Small Codec and Importer Lists (`lsmash_entry_list_t`)
Small bounded parameter lists in `codecs/` and `importer/` (such as H.264 SPS/PPS lists, HEVC parameter set arrays, ADTS/AC-3/DTS importer track lists, and chapter entries, typically ≤ 16 elements) remain on `lsmash_entry_list_t` and use the `LSM_ENTRY_LIST_FOR_EACH` macro for traversal.

---

## 3. C11 Paged Block Array (`lsm_list_t`) Specification

### 3.1 Data Structure (`common/list.h`)
```c
#define LSM_LIST_PAGE_SHIFT 8U
#define LSM_LIST_PAGE_SIZE  (1U << LSM_LIST_PAGE_SHIFT) /* 256 elements per page */
#define LSM_LIST_PAGE_MASK  (LSM_LIST_PAGE_SIZE - 1U)   /* 0xFF */

typedef struct
{
    uint8_t                    **pages;
    uint32_t                     entry_count;
    uint32_t                     page_count;
    uint32_t                     page_capacity;
    uint32_t                     elem_size;
    lsmash_entry_data_eliminator eliminator;
} lsm_list_t;
```

### 3.2 Structural Guarantees & Complexity
1. **True `O(1)` Indexed Addressing**:
   Element at 0-based `index` resides at:
   ```c
   page_idx = index >> 8;
   slot_idx = index & 0xFF;
   address = list->pages[page_idx] + slot_idx * list->elem_size;
   ```
2. **100% Pointer Stability on Append (`lsm_list_append` / `lsm_list_add`)**:
   - Once a 256-element page is allocated, its base address (`pages[p]`) is **never reallocated or moved** when subsequent elements or pages are appended. Only the outer `uint8_t **pages` directory array grows via `lsmash_realloc` when `page_count` reaches `page_capacity`.
   - Therefore, any pointer to an existing element (such as `stts` or `stsc` last-entry pointers, or `timeline` sample/chunk pointers) remains valid across arbitrary future appends.
3. **Zero-Initialized & `NULL` Container Safety**:
   - All read accessors (`lsm_list_count`, `lsm_list_at`, `lsm_list_get`, `lsm_list_head`, `lsm_list_tail`, `lsm_list_cursor_init`, `LSM_LIST_FOR_EACH`) safely handle both `list == NULL` and a zero-initialized `lsm_list_t list = { 0 }` struct without dereferencing `NULL`.
4. **In-Place Zero-Initialized Slot Allocation (`lsm_list_append`)**:
   - `void *lsm_list_append( lsm_list_t *list )` allocates the next slot in the current page (allocating a new 256-element page on page boundaries), zeroes the `elem_size` bytes with `memset`, increments `entry_count`, and returns a direct pointer to the slot—requiring **zero per-element `malloc` calls**.
5. **Cross-Page Mid-List Removal (`lsm_list_remove_at` / `lsm_list_remove`)**:
   - When an element at `index < entry_count - 1` is removed (such as in `stbl_deduplicate_roll_group()` in `core/stbl.c`), `lsm_list_remove_at()` invokes `list->eliminator` (if non-`NULL`) on the target slot, shifts subsequent slots within each page via `memmove`, and copies the first element of `pages[p + 1]` into the last slot (`255`) of `pages[p]` across `[start_page .. end_page]`.
   - Because mid-list removal shifts elements after `index`, `isom_roll_group_t` in `core/stbl.h` stores the 1-based group index (`assignment_number`) rather than raw `isom_group_assignment_entry_t *` pointers into `sbgp->list`.

### 3.3 Unified Accessor, Cursor & Iteration API (`common/list.h`)
- **0-Based & 1-Based Accessors**:
  - `uint32_t lsm_list_count( const lsm_list_t *list )`
  - `void *lsm_list_at( const lsm_list_t *list, uint32_t index )` *(0-based)*
  - `void *lsm_list_get( const lsm_list_t *list, uint32_t entry_number )` *(1-based ISOBMFF index)*
  - `void *lsm_list_head( const lsm_list_t *list )`
  - `void *lsm_list_tail( const lsm_list_t *list )`
- **Lockstep Multi-Table Cursor (`lsm_list_cursor_t`)**:
  - Used in `core/timeline.c` (`timeline_construct`, `lsmash_get_media_timestamps`, and `lsmash_set_media_timestamps`) where multiple sample tables (`stts`, `ctts`, `stss`, `stps`, `sdtp`, `stsz`, `sbgp`) are advanced simultaneously in lockstep:
    ```c
    lsm_list_cursor_t stts_cur;
    lsm_list_cursor_init( &stts_cur, stbl->stts->list );
    isom_stts_entry_t *stts_data = (isom_stts_entry_t *)stts_cur.data;
    /* Advance to next entry: */
    stts_data = (isom_stts_entry_t *)lsm_list_cursor_next( &stts_cur );
    ```
- **Single-Loop Iteration Macros**:
  ```c
  #define LSM_LIST_FOR_EACH( var, list_ptr ) \
      for( uint32_t lsm_idx_##var = 0; \
           ((var) = lsm_list_at( (list_ptr), lsm_idx_##var )) != NULL; \
           ++lsm_idx_##var )

  #define LSM_ENTRY_LIST_FOR_EACH( var, list_ptr ) \
      for( lsmash_entry_t *lsm_ent_##var = lsm_entry_list_head( (list_ptr) ); \
           ((var) = (lsm_ent_##var ? lsm_ent_##var->data : NULL)) != NULL; \
           lsm_ent_##var = lsm_ent_##var->next )
  ```
  Both expand to a single `for` loop, so `break` and `continue` inside the loop body behave naturally without hidden outer-loop traps.

---

## 4. Multi-Buffer Bank Resize Architecture (`common/multibuf.{c,h}`)

`lsmash_multiple_buffers_t` allocates a contiguous memory block of `number_of_buffers * buffer_size` bytes where buffer `b` in `[1, number_of_buffers]` resides at byte offset `(b - 1) * buffer_size`.

When `lsmash_resize_multiple_buffers( multiple_buffer, new_buffer_size )` is called:
1. **Validation & Overflow Protection**:
   - Rejects `!multiple_buffer`, `!multiple_buffer->buffers`, `!multiple_buffer->number_of_buffers`, or `new_buffer_size == 0`.
   - If `new_buffer_size == old_buffer_size`, returns `multiple_buffer` immediately.
   - Verifies `(size_t)number_of_buffers <= SIZE_MAX / new_buffer_size` and `(uint64_t)number_of_buffers * new_buffer_size <= UINT32_MAX` to prevent both `size_t` and 32-bit multiplication overflows (shared with `lsmash_create_multiple_buffers()`).
2. **Transactional Shrinking (`new_buffer_size < old_buffer_size`)**:
   - Allocates a fresh destination bank (`lsmash_malloc( total_alloc )`) first; if allocation fails, returns `NULL` immediately without modifying `multiple_buffer->buffers` or mutating the stored buffer layout.
   - Copies the leading `new_buffer_size` bytes of each buffer (`i = 0 .. number_of_buffers - 1`) from `src + (size_t)i * old_buffer_size` via `memcpy()`, then frees the previous allocation (`lsmash_free()`).
3. **Expanding (`new_buffer_size > old_buffer_size`)**:
   - Calls `lsmash_realloc( multiple_buffer->buffers, total_alloc )` first (leaving `multiple_buffer->buffers` intact on `NULL` failure).
   - Iterates in **reverse order** (`i = count - 1` down to `1`), moving `old_buffer_size` bytes from `temp + (size_t)i * old_buffer_size` to `temp + (size_t)i * new_buffer_size` via `memmove()`, and zero-initializing the newly exposed tail `[old_buffer_size .. new_buffer_size - 1]` of every buffer (including buffer `0`) via `memset()`.

---

## 5. Allocator Contracts & C11 Debug Memory Auditor (`common/alloc.{c,h}`)

All four core allocator entry points (`lsmash_malloc`, `lsmash_malloc_zero`, `lsmash_realloc`, `lsmash_memdup`) enforce deterministic zero-size and overflow contracts alongside optional audit instrumentation:

1. **Release Zero-Size & `NULL` Contracts (`common/alloc.c`)**:
   - In release builds, `lsmash_malloc`, `lsmash_malloc_zero`, `lsmash_realloc`, and `lsmash_memdup` guarantee a **non-`NULL` minimum 1-byte allocation on `size == 0`** (`size ? size : 1`) when memory is available, eliminating platform-dependent `malloc(0)` / `realloc(p, 0)` ambiguity; `lsmash_memdup` skips `memcpy()` when `size == 0` and rejects `ptr == NULL`.
2. **Debug `LSM_ALLOC_MAX_DEBUG_SIZE` Overflow Guard**:
   - When configured with `--enable-debug-alloc` (or compiled with `-DLSMASH_DEBUG_ALLOC`, which `make test` enables automatically), every debug wrapper checks `size > LSM_ALLOC_MAX_DEBUG_SIZE` (`SIZE_MAX - sizeof(lsm_alloc_header_t) - LSM_ALLOC_REDZONE_SIZE`) up front in `lsmash_malloc_debug`, `lsmash_malloc_zero_debug`, `lsmash_realloc_debug`, and `lsmash_memdup_debug`, returning `NULL` cleanly without integer wraparound or mutating existing `realloc` source blocks.
3. **64-Byte Aligned Intrusive Header (`lsm_alloc_header_t`) & Redzone**:
   - Prepends a 64-byte header (`magic`, `size`, doubly-linked `prev`/`next`, `__FILE__`, `__LINE__`, `#size`/`#ptr` stringified expression, and 8-byte `front_canary = 0xC0DEC0DEC0DEC0DEULL`) and appends a 16-byte rear redzone filled with `0xA5`.
   - Because `sizeof(lsm_alloc_header_t) == 64`, the returned user pointer preserves 16-byte alignment on 64-bit platforms.
4. **Uninitialized & Freed Memory Poisoning**:
   - `lsmash_malloc` fills newly allocated user bytes with `0xCD` (`LSM_ALLOC_POISON_ALLOC`) to immediately expose uninitialized memory reads.
   - `lsmash_free` verifies front and rear canaries, marks `hdr->magic = 0xDEAD7EEDDEAD7EEDULL` to catch double-frees, and overwrites the payload with `0xDD` (`LSM_ALLOC_POISON_FREE`) to catch use-after-free bugs.
5. **Function-Pointer Compatibility**:
   - `common/alloc.c` defines `#define LSMASH_ALLOC_IMPLEMENTATION` before including `common/internal.h` and provides real linkable functions for `lsmash_malloc`, `lsmash_free`, etc. alongside the macro wrappers in `common/alloc.h`, so passing `lsmash_free` as an `lsmash_entry_data_eliminator` callback works seamlessly.
6. **Zero-Leak Verification API**:
   - `lsm_alloc_get_active_count()`, `lsm_alloc_get_active_bytes()`, `lsm_alloc_dump_leaks()`, and `lsm_alloc_verify_no_leaks()` allow every test suite in `tests/test_main.c` to assert that 0 blocks and 0 bytes remain allocated at test completion and after error-path rollbacks.

---

## 6. Container Allocation Bounds & OOM Rollback Contracts (`core/`, `common/osdep.c`)

1. **Box OOM Rollback (`core/movie.c`, `core/read.c`)**:
   - `lsmash_set_copyright()` and `lsmash_set_sdp()` validate `strlen() < UINT32_MAX`, check for existing `sdp`/`rtp` boxes, and immediately invoke `lsm_box_remove_by_itself()` on incomplete `cprt`, `sdp`, or `rtp` boxes if `lsmash_memdup()` fails, leaving no orphan box without a payload in the tree.
   - Duplicate single-child boxes (`read_iods`, `read_ctab`, and `metaitem` child boxes `read_mean`, `read_name`, `read_data`) fall back to `read_unknown_box()` when `LSMASH_IS_EXISTING_BOX(...)` already holds, preventing replacement leaks of existing child structs or string allocations.
2. **Metadata Sentinel & Allocation Wraparound Guards (`core/meta.c`)**:
   - `meta_add_metadata()` verifies `LSMASH_IS_EXISTING_BOX( file->moov )` at runtime (preventing static `moov` sentinel mutation when `lsmash_set_itunes_metadata()` is called on an active Media Segment) and enforces `UINT32_MAX` string length bounds.
   - `lsmash_get_itunes_metadata()` delegates string duplication for `mean`, `name`, and `data` payloads to `meta_alloc_cstr_copy()`, which explicitly enforces `len <= SIZE_MAX - 1` before evaluating `(size_t)len + 1`. Even under the non-`NULL` 1-byte `lsmash_malloc(0)` contract, a 32-bit `UINT32_MAX` length cannot wrap `len + 1` to `0` and trigger a heap buffer overflow in `memcpy()`.
3. **Platform Wide-Char Conversion Guards (`common/osdep.c`, `common/utils.c`)**:
   - `lsmash_string_to_wchar()`, `lsmash_string_from_wchar()`, and `lsmash_convert_ansi_to_utf8()` initialize output pointers to `NULL`, verify `SIZE_MAX / sizeof(wchar_t)` bounds before multiplying, allocate/free consistently via `lsmash_malloc`/`lsmash_free`, and roll back cleanly if `MultiByteToWideChar()` or `WideCharToMultiByte()` fails.
