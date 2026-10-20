---
name: project-string-no-std-string
description: "core::String rewritten to drop its std::string/std::string_view dependency; new custom StringView class in string_view.hpp."
metadata:
  node_type: memory
  type: project
  originSessionId: c267e765-cb44-4a48-873f-d4471de41b03
---

**2026-07-06 session.** `src/core/string.hpp`'s `String` class no longer
stores a `std::string` internally — it owns a hand-rolled heap buffer
(`char* data; size_t size, capacity;`, geometric growth, always
null-terminated, no SSO). Added `src/core/string_view.hpp`: a from-scratch
`StringView` (pointer+size, no allocation) replacing `std::string_view`
everywhere in this subsystem — `String::view()` now returns `StringView`,
and `string_unicode.hpp`'s byte-level functions (`is_valid_utf8`,
`codepoint_count`, `to_utf16`, `to_utf32`, `codepoint_at`, `byte_offset_of`,
`CodepointView`) take `StringView` instead of `std::string_view`.

**Why:** explicit user request to remove the std::string/std::string_view
dependency from `String` and give it its own view type.

**Scope boundary kept deliberately narrow** (avoided cascading into
UTF-16/32 handling, which was out of the ask):
- `unicode::to_utf16/to_utf32` still return `std::u16string`/`std::u32string`;
  `froutf16/froutf32` still take `std::u16string_view`/`std::u32string_view`
  and return `std::string` — these are UTF-16/32 interop helpers, not
  `String`'s own storage, so left untouched.
- `unicode::encode_utf8` was templatized (`template<Sink> encode_utf8(cp, Sink&)`,
  calls `out.push_back(char)`) instead of hardcoding `std::string&`, so it
  works for both `std::string` (existing froutf16/froutf32 callers) and
  `String` (new internal use in uToLower/uToUpper/map/filter/flatMap) without
  string_unicode.hpp needing to know about `String` (avoids a circular
  include: string_unicode.hpp is included *before* String is defined).
- `String` **keeps one interop constructor**, `String(const std::string&)`
  (copies bytes in immediately, no stored dependency) — required by real
  call sites: `src/sdl3/hidapi.hpp`'s `wideToString()` (wraps
  `unicode::froutf16/froutf32` results) and the class's own
  `froutf16`/`froutf32`/`uReverse` factories. Confirmed via grep before
  writing: nothing in the codebase called the old `.std_string()` /
  `.move_std_string()` accessors, so those were dropped outright rather than
  kept as dead API.
- Dropped a `String(std::string_view)` constructor entirely (no implicit
  conversion path StringView←std::string_view was added, by design) —
  `src/sdl3/hidapi.hpp:41,44` (`unicode::to_utf16/to_utf32(std::string_view(...))`)
  were the only real external callers passing a genuine `std::string_view`
  through this boundary; updated to `s.view()` instead.

**Correctness-critical part:** since `= default` copy/move ctor/assignment
(what the old std::string-backed version used) would double-free with a raw
owned pointer, all four had to be hand-written (Rule of 5) — default move
would just *copy* the pointer value, not null out the source.

**Blast radius was small** — checked before starting: only
`src/sdl3/hidapi.hpp` (2 call sites) needed edits outside `src/core/`; the
"String" hits in ~30 other files were either `Result<T, String>` error
types or an unrelated `NodeType::String` enum in the `data::` module, not
API surface touching `.view()`/`.std_string()`/`std::string_view` params.

**Validated via** a standalone scratch file (`#define USE_TEST` +
`#include "core/core.hpp"` + `RUN_ALL_TESTS()`, no SDL link needed since
`core.hpp`'s dependency chain is SDL-free) — all 18 `StringTest` cases
pass — plus the full `make run-tests` (20 smoke tests incl. `smoke_test_9`
which exercises the hidapi.hpp call sites).
