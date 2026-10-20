---
name: project-data-scripts-module
description: "New src/data/ module — JSON/INI/CSV/XML/YAML/TOML/CSS parsers+encoders on a common Properties-backed tree, plus the Properties wrapper rework that enabled it."
metadata: 
  node_type: memory
  type: project
  originSessionId: 4007b853-281f-4ca2-a318-41aaecee4cae
---

**2026-07-03 session.** Added a new top-level `src/data/` module (parallel
to `src/core`, `src/ecs`, `src/math`, `src/sdl3`, `src/ui`) implementing a
unified data-script parser/encoder API, inspired by SDL3pp's
`SDL3pp_dataScripts.h` (`~/Documents/Programs/Programs_C/SDL/Video_Projects/SDL3pp`)
but with the tree backed by `sdl3::Properties` instead of a plain
`shared_ptr`/`LinkedMap` structure, per explicit request.

**Why Properties needed rework first:** `sdl3::Properties` (in
`src/sdl3/misc.hpp`) was improved based on
`github.com/talesm/SDL3pp/.../SDL3pp_properties.h` — split into a shared
`PropertiesBase` (get/set/enumerate/lock/count) with `Properties` (owning,
RAII) and a new `PropertiesRef` (non-owning view, mirrors the
[[feedback-wrap-c-pointers]] Wrapper/Borrowed split but for a non-pointer
`SDL_PropertiesID` m_handle) deriving from it. `globalProperties()` now
returns `PropertiesRef` instead of a raw `SDL_PropertiesID`. Added
`count()` and (guarded by `#ifdef SDL_PROP_NAME_STRING`) the `PROP_NAME_STRING` constant.

**Key architectural finding:** `SDL_Properties` is a hash table — enumeration
order does NOT match insertion order. This makes it a poor natural fit for
an ordered document tree (JSON key order, XML child order, CSV column
order all matter). Fixed by having `data::Node` (in `node.hpp`) hold both
an owning `sdl3::Properties` (real storage: every Object/Array child is a
pointer-property with a cleanup callback that deletes the child's
`shared_ptr<Node>`, so lifetime is genuinely driven by Properties, not a
side member) AND a small `std::vector<std::string> order` purely to fix
enumeration order. Arrays reuse the same Object machinery with string keys
"0", "1", "2", ... — `push()` is just `set(String::From(order.size()), ...)`.

**Format coverage, all in `src/data/*.hpp` (header-only), auto-registered
in a `DocumentFactory` singleton by name + file extension:**
- `json.hpp` — full JSON (RFC 8259), pretty-printed output.
- `ini.hpp` — 2-level (sections -> key=value), leading keys before any
  `[section]` go to an implicit global section.
- `csv.hpp` — RFC 4180 (quoted fields, doubled-quote escaping, embedded
  newlines/commas). Same one-level constraint as SDL3pp's own CSV: root
  Object where every value is an Array (named columns).
- `xml.hpp` — subset (elements/attributes/text/comments, no DTD/CDATA/
  namespaces). Same convention as SDL3pp: attributes under `"@attributes"`,
  text under `"#text"`, same-tag siblings merge into an Array — **this loses
  interleaving order between different tag names** (documented, inherent
  XML/tree impedance mismatch, matches SDL3pp's own tradeoff exactly).
- `yaml.hpp` — **deliberately a basic block-style subset**, not full YAML
  1.2 (flow style `[a,b]`/`{k:v}`, anchors/aliases, tags, literal/folded
  block scalars, multi-doc `---` handling are all out of scope — full YAML
  is one of the most complex formats to parse correctly). Supports nested
  block mappings/sequences, `- key: value` compact list-of-maps form on
  read, quoted scalars kept as strings (never type-coerced), auto-typed
  bare scalars. Encoder always emits the expanded `-\n  key: value` form
  (never the compact one-liner) for simplicity.
- `toml.hpp` — **also a deliberately basic subset**: `[table]` and
  `[table.nested]` headers, `key = value`, quoted strings, bool/int/float,
  single-line inline arrays of scalars `[1, 2, 3]`. NOT supported:
  `[[array-of-tables]]`, inline tables `{k=v}`, dotted keys in assignments,
  multi-line strings, datetimes (kept as opaque strings if not numeric).
- `css.hpp` (added 2026-07-06) — style rules + common at-rules
  (`@media`/`@supports`/`@document`/`@layer` and `@keyframes` nest a rule
  list; `@font-face`/`@page`/`@property`/`@counter-style`/`@viewport`/
  `@font-palette-values`/`@font-feature-values` hold a declaration block
  directly; anything else with a block defaults to nested-rule-list;
  `@import`/`@charset`/`@namespace` are bare statements ending in `;`).
  **Root is an Array, not an Object** — the only format-specific tree
  shape in this module that deviates from "root is the top container
  object": CSS legally repeats the same selector or the same at-rule
  multiple times, and an Object root (like INI/TOML) would silently
  overwrite earlier occurrences. Each array element is `{selector,
  declarations}` for a style rule or `{at, params?, body?|declarations?}`
  for an at-rule. Declaration values go through the shared
  `scalar::parseNode`/`toString` like the other text formats, but
  **quotes are kept as literal part of the string value** (`"Times New
  Roman"` round-trips with its quotes intact) rather than being
  stripped/re-added like TOML — CSS string values need the exact quoting
  style preserved to stay valid on re-encode (unlike TOML where quoting is
  purely syntactic noise around an already-known string type). Comments
  `/* ... */` are stripped at decode and not preserved. Value/selector
  scanning is quote- and paren-depth-aware (`readUntilAny`) specifically
  so `url(data:image/png;base64,...)` doesn't get cut at the embedded
  `;`/`:` — a real, not hypothetical, edge case for data-URI values.

**Bug found and fixed during testing:** a lone `-` list marker (nothing
after it on the line, nested content on following lines) miscalculated its
child block's expected indent as `column+1` instead of `column+2` — caused
every YAML round-trip through a list-of-objects to segfault (the re-parse
after our own encoder's output, which emits bare `-` then indents the
mapping 2 columns under it). Fixed in `yaml.hpp`'s `preprocess()`. Lesson:
**always test decode(encode(decode(x)))**, not just decode(x) — the
encoder's own output is the input most likely to hit codec edge cases,
and it's exactly what [[project-sdl3-wrapper-gaps]]'s smoke-test habit of
"never trust the first successful compile" caught here too, just at
runtime instead of compile time.

`smoke_test_11.cpp` covers per-format round-trip (decode -> encode ->
re-decode) for all 7 formats (CSS added 2026-07-06: style rule + nested
`@media` + `@font-face` declaration-block + bare `@import` statement),
plus a JSON -> YAML -> TOML -> JSON chain proving data survives multiple
cross-format hops, plus `DocumentFactory::createByFilename()`. 11 smoke
tests total now, all passing.

See also [[project-sdl3-wrapper-gaps]] for the sdl3:: wrapper track this
data module builds on top of (Properties, IOStream).
