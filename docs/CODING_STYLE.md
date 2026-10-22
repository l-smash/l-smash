# L-SMASH Official Coding Style & Contribution Guidelines

This document specifies the official coding style, formatting standards, symbol naming conventions, and Git contribution policies for the L-SMASH codebase. All contributions must adhere strictly to these conventions.

---

## 1. Language Standard & Header Policy

### 1.1 C11 Core Implementation & C99/C++ Public API
- **Internal Implementation**: All library internals (`common/`, `core/`, `codecs/`, `importer/`), CLI frontends (`cli/`), and tests (`tests/`) are written in standard **C11** (`-std=c11 -pedantic`). Modern C11 features including `_Static_assert`, anonymous structs/unions, and `<stdalign.h>` / `max_align_t` are permitted and encouraged for structural validation and safety.
- **Public Header (`lsmash.h`)**: The public header `lsmash.h` is consumed by diverse external applications and wrappers. It must remain **strictly 100% C99 (`-std=c99 -pedantic`) and C++ compatible**, with complete `extern "C"` wrappers and zero C11-only syntax.

### 1.2 Translation Unit Header Inclusions
- Every `.c` file across the library and tools must place the internal foundation header as its very first include immediately following the top license header:
  ```c
  #include "common/internal.h" /* must be placed first */
  ```
  (or `#include "internal.h"` for files inside `common/`).

### 1.3 Internal Header Guard Policy
- **No Include Guards for Internal Headers**: To maintain a clean, strictly acyclic, and predictable dependency graph without hidden circular includes, **internal header files (`common/*.h`, `core/*.h`, `codecs/*.h`, `importer/*.h`, `cli/*.h`) do not use `#ifndef ... #define ... #endif` include guards**.
- Only the standalone public header `lsmash.h` maintains `#ifndef LSMASH_H ... #endif`.
- Types, structs, and declarations must be organized so that headers are included in a well-defined topological order with zero duplicate inclusions.

---

## 2. Formatting & Layout Conventions

### 2.1 Indentation
- Indentation is strictly **4 spaces** per level. Tab characters (`\t`) are forbidden.

### 2.2 Braces (Allman Style)
- Braces `{` and `}` for functions, structures, unions, enumerations, and control flow statements (`if`, `else`, `for`, `while`, `do`, `switch`) must be placed on their own dedicated lines:
  ```c
  if( condition )
  {
      do_something();
  }
  else
  {
      do_other();
  }
  ```
- For single-statement control flow bodies, braces may be omitted if clarity is preserved:
  ```c
  if( !ptr )
      return LSMASH_ERR_INVALID_DATA;
  ```

### 2.3 Spaces Inside Parentheses
- In control statements (`if`, `for`, `while`, `switch`) and function/macro invocations with arguments, a single space must follow the opening parenthesis and precede the closing parenthesis:
  ```c
  if( cond )
  for( int i = 0; i < count; i++ )
  func( arg1, arg2 );
  ```
- **Exceptions (no interior spaces)**:
  - Empty argument lists: `func()`
  - Type casts: `(uint8_t *)ptr`
  - `sizeof` expressions: `sizeof(isom_box_t)`

### 2.4 Pointer Asterisk Placement & Vertical Alignment
- The pointer asterisk `*` binds to the identifier/variable name, not to the type:
  ```c
  isom_box_t *box;
  ```
- In consecutive variable declarations, struct members, assignment statements (`=`), and multi-line macro continuation backslashes (`\`), align columns vertically:
  ```c
  uint32_t     sample_count   = 0;
  uint64_t     media_duration = 0;
  isom_stts_t *stts           = NULL;
  ```

### 2.5 Function Declarations and Definitions
- For major functions and functions with multiple parameters, place the opening parenthesis `(` on the line immediately below the function name, and format each parameter on its own indented line, vertically aligning parameter types and parameter names:
  ```c
  int lsm_stbl_calculate_bitrate_description
  (
      isom_stbl_t *stbl,
      isom_mdhd_t *mdhd,
      uint32_t    *bufferSizeDB,
      uint32_t    *maxBitrate,
      uint32_t    *avgBitrate,
      uint32_t     sample_description_index
  )
  {
      ...
  }
  ```

---

## 3. Naming Conventions

### 3.1 Symbol Visibility and Prefix Rules
L-SMASH enforces a strict three-tier symbol visibility hierarchy across the entire codebase:

1. **Public API (`lsmash.h` only)**:
   - Function names: `lsmash_<name>` (e.g., `lsmash_open_file`, `lsmash_create_movie`)
   - Types and structs: `lsmash_<name>_t`
   - Macros and constants: `LSMASH_<NAME>`, `ISOM_<NAME>`, `QT_<NAME>`
   - Public symbols are exported through `liblsmash.ver` and must maintain permanent API/ABI compatibility.

2. **Internal Shared Functions (`common/`, `core/`, `codecs/`, `importer/`)**:
   - Functions shared across compilation units but not exported publicly must use the **`lsm_<filename>_*`** prefix matching the defining module basename:
     - `core/stbl.{c,h}`: `lsm_stbl_*`
     - `core/sample.{c,h}`: `lsm_sample_*`
     - `core/track.{c,h}`: `lsm_track_*`
     - `core/movie.{c,h}`: `lsm_movie_*`
     - `core/box.{c,h}`: `lsm_box_*`
     - `codecs/av1.{c,h}`: `lsm_av1_*`
     - `codecs/mp4a.{c,h}`: `lsm_mp4a_*`
     - `codecs/description.{c,h}`: `lsm_description_*`
     - `common/alloc.{c,h}`: `lsm_alloc_*`
     - `common/list.{c,h}`: `lsm_list_*`

3. **File-Local Static Functions**:
   - `static` helper functions private to a single `.c` compilation unit must use the **`<filename>_*`** prefix (without `lsm_`):
     - `stbl_*` in `core/stbl.c`
     - `sample_*` in `core/sample.c`
     - `track_*` in `core/track.c`
     - `movie_*` in `core/movie.c`
     - `box_*` in `core/box.c`
     - `av1_*` in `codecs/av1.c`

### 3.2 Preservation of Specification Syntax Identifiers
When implementing bitstream syntax parsers, container box definitions, or codec descriptors specified by formal international standards (ISO/IEC 14496-1, ISO/IEC 14496-12, ISO/IEC 23003-5, ISO/IEC 23091-3, AOM AV1, RFC 9639 FLAC, Apple QTFF):
- **Preserve exact spelling, capitalization, CamelCase, and underscores**: Syntax element names and derived mathematical variables must never be altered or adapted into arbitrary casing styles.
- **Why**: Exact preservation ensures bidirectional `grep` and searchability between the C source code, debug print dump outputs, and the official specification documents.
- Examples:
  - ISO/IEC 23003-5 (`pcmC`): `format_flags`, `PCM_sample_size`
  - ISO/IEC 14496-12 (`chnl`): `stream_structure`, `definedLayout`, `speaker_position`, `omittedChannelsMap`
  - ISO/IEC 14496-12 (`btrt`): `bufferSizeDB`, `maxBitrate`, `avgBitrate`
  - AOM AV1: `seq_profile`, `seq_level_idx`, `seq_tier`, `high_bitdepth`, `SeenFrameHeader`, `OrderHintBits`, `TileColsLog2`

---

## 4. Commenting Policy

### 4.1 Explanatory Comments (`/* ... */`)
- All explanatory comments, architectural notes, and algorithm rationale must use C-style block comments: `/* ... */`.
- Comments should document the **Why & How** rather than echoing trivial C statements:
  - Document complex ISOBMFF vs. QTFF specification boundaries and hybrid container compatibility rules.
  - Document specification errata, real-world muxer deviations, and defensive parsing rationales.
  - Document performance hacks, bit-manipulation invariants, and memory layout constraints.

### 4.2 Code Deactivation (`//`)
- C++-style single-line comments (`//`) are reserved exclusively for temporary deactivation or commenting-out of code (e.g., prototype branches or experimental features undergoing evaluation).

---

## 5. Git History & Contribution Policy

### 5.1 Linear History Only (No Merge Commits)
- L-SMASH enforces a strictly **linear Git history**. Merge commits are completely forbidden.
- Never use the GitHub Web UI "Merge pull request" button or `git merge --no-ff`.
- External contributions and branch updates must be integrated via `git cherry-pick` (preserving original author attribution) followed by fast-forward merges or rebases (`git merge --ff-only`).

### 5.2 No Hosting-Specific Issue or PR Numbers
- To prevent repository lock-in to specific hosting providers (e.g., GitHub, GitLab) and avoid erroneous cross-linking between forks, **commit messages, code comments, and documentation must never contain hosting-specific Issue or Pull Request references** (such as `#123`).
- Commit messages and documentation must describe the technical root cause, standard references, and concrete behavior changes directly.

### 5.3 Commit Message Format
- All commit subjects (first line) must follow the conventional commit structure:
  ```text
  <type>(<scope>): <concise summary in imperative mood>
  ```
- Allowed types: `feat`, `fix`, `refactor`, `docs`, `test`, `build`, `chore`.
- Provide a detailed explanation of technical motivation and changes in the commit body.
- When an AI assistant participates in generating or modifying code, append the designated trailer identifying the model and thinking level:
  ```text
  Assisted-By: <Model Name> (<Thinking Level>)
  ```

---

## 6. Versioning Policy

L-SMASH follows strict semantic-like C library versioning defined by macros in the public header `lsmash.h`: `LSMASH_VERSION_MAJOR`, `LSMASH_VERSION_MINOR`, and `LSMASH_VERSION_MICRO`.

### 6.1 Version Macro Increment Rules
1. **MAJOR (`LSMASH_VERSION_MAJOR`)**:
   - Incremented only on breaking ABI/API changes or complete generational overhauls.
   - Resets `MINOR` and `MICRO` to `0`.
2. **MINOR (`LSMASH_VERSION_MINOR`)**:
   - Incremented when new public APIs, public structures, public enumerations, or new public Codec IDs/FourCCs are added to `lsmash.h`.
   - Resets `MICRO` to `0`.
3. **MICRO (`LSMASH_VERSION_MICRO`)**:
   - Incremented for backward-compatible bug fixes, internal architecture refactoring, performance improvements, defensive security hardening, or toolchain/build standard updates (e.g., C11 standard migration) that do not alter the public API signatures in `lsmash.h`.

### 6.2 Strict Commit Discipline for Version Bumps
- **Code Change Commits Only**: Macro version updates in `lsmash.h` must take place directly within the code change commit (`feat`, `fix`, `refactor`, or `build`) that introduces the relevant feature, bug fix, or architecture change.
- **Prohibition on `docs` and `test` Commits**: Version macros must **never** be modified in pure documentation (`docs(...)`) or test (`test(...)`) commits. Commits labeled `docs` or `test` must strictly contain only documentation or test files.


---

## 7. Markdown Documentation Rules

These invariants apply to `README.md`, `USAGE.md`, `ARCHITECTURE.md`, and `docs/**/*.md`.

### 7.1 No LaTeX Math Blocks
- Do not use LaTeX math notation (`$...$` or `$$...$$`) in Markdown documents.
- Renderers such as GitHub and KaTeX interpret escapes differently (for example, `_` inside `\text{...}` causing syntax errors, or `&`, `->`, `<<`, `>>` being interfered with by HTML entity conversion), and the raw text becomes hard to read in plain-text viewers.

### 7.2 Standard Alternative Notation
- Write address calculations, bit operations, conditional expressions (formerly `cases` blocks), and structure layouts in C code blocks (` ```c `) or inline code (`` `...` ``).
- Write complexity orders (`O(1)`, `O(N)`), mathematical functions (`floor(log2(n))`), units, and arrows as inline code or standard plain text.