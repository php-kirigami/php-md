# php-md

## Context

Part of the Kirigami ecosystem. Companion to `php-wasm-compiler`
(`../php-wasm-compiler`, github.com/php-kirigami/php-wasm-compiler), which
builds the `php.wasm` runtime and vendors third-party PHP extensions for it.

Purpose: replace the CommonMark/GFM *structural* parsing currently done by
hand-rolled regexes in `kirigami/packages/php-prepros/src/libraries/md.class.php`
(the `MD` class — see "Relationship to other repos" below) with a real, fast
C implementation, while keeping `MD::toHtml()`'s output identical.

This repo exists because of a specific dead end found in `php-wasm-compiler`
(2026-09-12, see its CLAUDE.md decisions 30-35 for the full investigation):
the only PECL extension providing CommonMark parsing to PHP,
`krakjoe/cmark`, is unmaintained since 2019, wraps plain `cmark` (no GFM
tables/tasklists/strikethrough/autolink), and hand-mimics PHP's internal
`zend_object` struct with hardcoded offsets — a pattern that silently
breaks (heap corruption, crash on object destruction) whenever PHP's real
`zend_object` layout changes, which it did between 2019 and PHP 8.5 (a new
`extra_flags` field). Patching that fork further would mean repeating the
same fragile hand-rolled-struct trick for every new GFM node type
(Table, TableCell, TaskItem, Strikethrough) we'd want to add. Writing a
small, purpose-built extension against `cmark-gfm` directly, with an
idiomatic (not hand-rolled) object model, was judged less total work and
much less fragile.

## Decided architecture (2026-09-12)

1. **New extension from scratch, not a fork of `krakjoe/cmark`.** No
   hand-rolled `zend_object` mimicry — use standard, idiomatic PHP
   extension object patterns (`zend_object_alloc()` +
   `object_properties_init()`) for any object this extension ever needs to
   expose, if any (see point 4).
2. **Built on `cmark-gfm`** (github.com/github/cmark-gfm), not vanilla
   `cmark` (commonmark.org / github.com/commonmark/cmark) — gets tables,
   tasklists, strikethrough, tagfilter, and autolink natively via its
   syntax-extension attachment API (`cmark_parser_attach_syntax_extension`).
   Verified 2026-09-12 by listing `github/cmark-gfm`'s `extensions/`
   directory: `autolink`, `strikethrough`, `table`, `tagfilter`,
   `tasklist` — **no footnotes extension** (GitHub's footnote rendering is
   server-side, never shipped in the C library). `MD::`'s existing
   footnote pre/post-processing is not something this extension can
   subsume — see point 3.
3. **Must reproduce `MD::toHtml()`'s exact behavior/output** — this is the
   compatibility bar, not a green-field design. Concretely, this extension
   covers *only* the CommonMark+GFM structural core: headings (ATX +
   Setext), emphasis/strong, lists, blockquotes, code spans/blocks, links,
   images, thematic breaks, tables, tasklists, strikethrough, autolinks.
   Everything `MD::` does that `cmark-gfm` has no concept of stays
   implemented in PHP, as pre/post-processing around this extension's
   render call, exactly as it works today:
   - the `{% plugin_name args %}` plugin system (inline and block forms,
     `MD::registerPlugin()`)
   - `:shortcode:` emoji substitution
   - definition lists (`Term` / `: Definition`)
   - footnote definitions/references (`[^label]`)
   - GFM-style alerts (`> [!NOTE]`, `[!TIP]`, etc. — GitHub's own
     proprietary convention, not part of cmark-gfm)
   - the custom raw-HTML whitelist sanitizer (stricter/different from
     cmark-gfm's `tagfilter` extension, which only escapes a fixed
     dangerous-tag list rather than whitelisting)
   - heading slug id generation (`slugify()`)
4. **API surface (proposed, not yet implemented):** a namespaced function,
   `MD\Render(string $markdown, int $options = 0): string`. Mirrors
   `krakjoe/cmark`'s own working pattern of a class `CommonMark\Node`
   coexisting with a function `CommonMark\Parse()` in the same namespace
   without collision — here the pre-existing *global* PHP class `\MD`
   (kirigami/php-prepros, unrelated file) coexists with this extension's
   *namespaced* function `\MD\Render()`; `MD::toHtml()` will call
   `\MD\Render()` internally for the structural core.
   - **Deliberately no mutable Node/Document object tree exposed to PHP.**
     Parse and render both happen on the C side; the function returns a
     finished HTML string. This is the specific simplification that avoids
     `krakjoe/cmark`'s entire class of hand-rolled-object bugs. `MD::`
     never needs programmatic tree mutation (append/insert/replace/clone) —
     it's a one-shot markdown-to-HTML pipeline — so there is no known use
     case to justify the risk of reintroducing that object model. Revisit
     only if a real, concrete need for tree mutation shows up.
   - **Plugin/callback hooks: not yet designed, two options on the table.**
     (a) Keep plugin extraction entirely in PHP via placeholder
     substitution, exactly as `MD::` already does today, unrelated to this
     extension's C core. (b) Expose a real C-level custom-block/
     custom-inline callback hook (cmark's own `CMARK_NODE_CUSTOM_BLOCK` /
     `CUSTOM_INLINE` node types with `on_enter`/`on_exit` strings, or a
     `zend_call_function()` callback invoked mid-render-walk). Leaning
     towards (a) for v1: simpler, already proven correct by `MD::`'s
     current implementation, and keeps the C core dumb and fast. Revisit
     if placeholder-based extraction proves insufficient in practice
     (e.g. a plugin needing to influence surrounding Markdown parsing
     rather than just injecting opaque HTML).
5. **`libcmark-gfm` vendoring location: not yet decided.** Two options:
   - **(A) — leaning towards this.** php-md vendors and cross-compiles
     `libcmark-gfm` + `libcmark-gfm-extensions` itself, as part of its own
     build, following `@php-wasm/compile-extension`'s documented
     dependency pattern (vendor the source, build it with Emscripten
     yourself, pass `--extra-cflags`/`--extra-ldflags` pointing at the
     result — see `php-wasm-compiler` CLAUDE.md decision 32's `sodium`
     pilot for the exact mechanism, `vendorLib`/`pkgConfigVar`). Keeps
     php-md fully self-contained, consistent with the
     `@kirigami/ext-<name>` standalone-package philosophy
     (`php-wasm-compiler` CLAUDE.md decision 5).
   - (B) `php-wasm-compiler`'s `compile/libcmark/Dockerfile` (currently
     vendors plain `cmark` for the now-likely-to-be-retired
     `krakjoe/cmark` static-mode extension) gets replaced/repointed at
     `cmark-gfm` instead, and php-md's `config.m4` consumes it exactly the
     way `ext/sodium` does today.
   - Not decided yet — revisit once php-md compiles and passes output-
     parity tests natively (point 6), before touching Emscripten/WASM at
     all.
6. **Native build first, WASM/JSPI second.** Target is ultimately PHP 8.5
   under Emscripten/JSPI (matching `php-wasm-compiler`'s own `php.wasm`),
   but the extension should build as a normal `.so` against a regular
   native PHP 8.5 CLI first — much faster iteration (no Docker/Emscripten
   round-trip) while the C code and the output-parity tests against
   `MD::toHtml()` are still being worked out. Port to Emscripten/WASM only
   once the native build is solid.
7. **License: GPL-2.0-or-later**, matching every other repo in the
   Kirigami ecosystem.
8. **English for all repo content** (code, comments, docs, commit
   messages); conversation with the maintainer stays in French — same
   convention as `php-wasm-compiler` (see its CLAUDE.md decision 14).

## Relationship to other repos

- **`php-wasm-compiler`** (github.com/php-kirigami/php-wasm-compiler,
  sibling checkout at `../php-wasm-compiler`): builds the core `php.wasm`
  (JSPI, Node-only) and vendors third-party PHP extensions for it. Its
  `cli.mjs compile-extension` subcommand is a fast, disposable smoke-test
  harness (compiles just one extension against already-built PHP headers,
  no full `php.wasm` rebuild) — used there to validate `krakjoe/cmark` and
  `sodium` during development, and the natural mechanism to validate
  php-md's WASM build too, once we get there (point 6). Its CLAUDE.md
  decisions 30-35 document the full `krakjoe/cmark` object-layout crash
  investigation that motivated this repo.
- **`kirigami`** (github.com/php-kirigami/kirigami, sibling checkout at
  `../kirigami`): the framework itself.
  `packages/php-prepros/src/libraries/md.class.php` is the `MD` class this
  extension accelerates — the single source of truth for exactly what
  output is expected. Any behavior change on either side must be checked
  against it. `packages/php-prepros/src/libraries/md.plugins.php` has the
  default plugins (`codepen`, `checklist`, `callout`, `img-asset`)
  registered through `MD::registerPlugin()` — unaffected by this
  extension (point 3).

## Status (2026-09-12)

Repo just created, nothing implemented yet. Next steps, in order:

1. Scaffold a minimal, compilable extension stub (`config.m4` + one
   placeholder function) buildable against a *native* PHP 8.5 (no
   Emscripten yet) for fast local iteration.
2. Vendor + build `libcmark-gfm` / `libcmark-gfm-extensions` natively (not
   WASM yet) and implement the real `MD\Render()`: parse via
   `cmark_parser_new()` with the GFM extensions attached, render via
   `cmark_render_html()`.
3. Diff-test against `MD::toHtml()`'s current output on a representative
   Markdown corpus (decision 3's compatibility bar) — CommonMark/GFM
   structural features only; the PHP-side pre/post-processing in `MD::`
   stays untouched and is not part of this repo.
4. Only once the native build and output parity are solid: port the build
   to Emscripten/WASM (JSPI), following `php-wasm-compiler`'s own
   Dockerfile conventions, and settle the vendoring question (point 5).
