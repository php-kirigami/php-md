# php-mdhtml

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
   `tasklist` — no footnotes *extension*. **Correction, same day**:
   footnotes turned out not to need one — `cmark-gfm`'s core library
   (`src/footnotes.c`, not `extensions/`) supports GitHub-style `[^label]`
   footnotes natively via a parser option, `CMARK_OPT_FOOTNOTES`, found
   while building the library and confirmed working end-to-end in the
   first native smoke test (see "Status"). So footnotes *are* covered by
   this extension after all — one less thing `MD::` needs to pre/post-
   process in PHP; point 3's footnote bullet is superseded by this.
3. **Must reproduce `MD::toHtml()`'s exact behavior/output** — this is the
   compatibility bar, not a green-field design. Concretely, this extension
   covers *only* the CommonMark+GFM structural core: headings (ATX +
   Setext), emphasis/strong, lists, blockquotes, code spans/blocks, links,
   images, thematic breaks, tables, tasklists, strikethrough, autolinks,
   and footnotes (point 2's correction).
   Everything `MD::` does that `cmark-gfm` has no concept of stays
   implemented in PHP, as pre/post-processing around this extension's
   render call, exactly as it works today:
   - the `{% plugin_name args %}` plugin system (inline and block forms,
     `MD::registerPlugin()`)
   - `:shortcode:` emoji substitution
   - definition lists (`Term` / `: Definition`)
   - GFM-style alerts (`> [!NOTE]`, `[!TIP]`, etc. — GitHub's own
     proprietary convention, not part of cmark-gfm)
   - the custom raw-HTML whitelist sanitizer (stricter/different from
     cmark-gfm's `tagfilter` extension, which only escapes a fixed
     dangerous-tag list rather than whitelisting)
   - heading slug id generation (`slugify()`)
4. **API surface (proposed, not yet implemented):** a namespaced function,
   `MDHtml\Render(string $markdown, int $options = 0): string`. Mirrors
   `krakjoe/cmark`'s own working pattern of a class `CommonMark\Node`
   coexisting with a function `CommonMark\Parse()` in the same namespace
   without collision — here the pre-existing *global* PHP class `\MD`
   (kirigami/php-prepros, unrelated file) coexists with this extension's
   *namespaced* function `\MDHtml\Render()`; `MD::toHtml()` will call
   `\MDHtml\Render()` internally for the structural core.
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
   - **(A) — leaning towards this.** php-mdhtml vendors and cross-compiles
     `libcmark-gfm` + `libcmark-gfm-extensions` itself, as part of its own
     build, following `@php-wasm/compile-extension`'s documented
     dependency pattern (vendor the source, build it with Emscripten
     yourself, pass `--extra-cflags`/`--extra-ldflags` pointing at the
     result — see `php-wasm-compiler` CLAUDE.md decision 32's `sodium`
     pilot for the exact mechanism, `vendorLib`/`pkgConfigVar`). Keeps
     php-mdhtml fully self-contained, consistent with the
     `@kirigami/ext-<name>` standalone-package philosophy
     (`php-wasm-compiler` CLAUDE.md decision 5).
   - (B) `php-wasm-compiler`'s `compile/libcmark/Dockerfile` (currently
     vendors plain `cmark` for the now-likely-to-be-retired
     `krakjoe/cmark` static-mode extension) gets replaced/repointed at
     `cmark-gfm` instead, and php-mdhtml's `config.m4` consumes it exactly the
     way `ext/sodium` does today.
   - Not decided yet — revisit once php-mdhtml compiles and passes output-
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
9. **Renamed `php-md` → `php-mdhtml`** (repo, directory, GitHub org repo,
   and every internal identifier — module name, `.so` filename, `config.m4`
   `PHP_ARG_ENABLE`, `php_md.h` → `php_mdhtml.h`, module globals, the
   `MINIT`/`RINIT`/etc. family, and the PHP-facing namespace:
   `\MD\Render()` → `\MDHtml\Render()`, `\MDHtml\RegisterEmoji()`) —
   requested by the maintainer, 2026-09-12, full scope confirmed explicitly
   (not just the repo name). `\MDHtml` still coexists cleanly with
   kirigami's own global `\MD` class for the same reason point 4 already
   established for the old `\MD` namespace (class table vs. namespaced
   function table are independent in PHP — same pattern `CommonMark\Node`/
   `CommonMark\Parse` already prove works in krakjoe/cmark).
10. **"As much as possible lives in the extension, as little PHP
    post-processing as possible"** (maintainer, 2026-09-12, refining point
    3 rather than replacing it: point 3's *compatibility bar* — matching
    `MD::toHtml()`'s behavior — still stands, but wherever a feature CAN be
    done as a tree/HTML transform in C without a PHP callback round-trip,
    it should be, even if `MD::` currently does it as PHP-side pre/post-
    processing). Implemented this round, all as tree pre-passes or a
    single linear HTML post-process pass (`mdhtml.c`) — none of it needs
    PHP:
    - **Heading ids**, including the `{#custom-id}` override syntax:
      computed in a tree pre-pass (`php_mdhtml_compute_heading_ids()` —
      collects each heading's plain text, detects a trailing `{#id}`
      marker or falls back to an ASCII slug) and injected into the
      rendered `<h1>`-`<h6>` tags by occurrence order during the HTML
      post-process pass, which also strips the visible `{#id}` marker text
      cmark has no idea is special. **Real bug found and fixed**: the
      first version of the backward-scan for `{#id}` kept scanning through
      the `#` character itself looking for `{`, so a "not a valid id char"
      check on `#` always failed first and the whole detection silently
      never fired (`Title Two {#custom-id}` produced id
      `"title-two-custom-id"`, not `"custom-id"`, and left the marker
      visible in the heading text). Fixed by scanning back to `#`
      specifically, then separately checking the preceding char is `{`.
    - **External link `target="_blank" rel="noopener noreferrer"`** and
      **image `loading="lazy"`**, matching `MD::`'s `buildLink()`/image
      output — simple pattern-matched insertions in the HTML post-process
      pass.
    - **Task-list normalization**: cmark-gfm's tasklist extension renders
      bare `<ul><li><input type="checkbox" checked="" disabled=""/>...`;
      the post-process pass adds `class="task-item"` to each such `<li>`,
      normalizes `checked=""`/`disabled=""` to bare `checked`/`disabled`,
      and — found only *after* checking `../kirigami/packages/canva/src/
      styles/prose.scss`, which has real, load-bearing CSS
      (`.task-list { list-style: none; padding-left: 0; }`) — adds
      `class="task-list"` to the wrapping `<ul>` too (a one-token
      lookahead: peek past the `<ul>` tag for an immediately-following
      task-item `<li>`, without consuming it). Without this last part the
      browser's default bullet would show *next to* the checkbox — a real
      visual regression, not just a cosmetic gap, which is why this ended
      up implemented instead of staying on the "deferred, cosmetic" list
      point 3 originally put it on. **Real off-by-one bug found and
      fixed**: the task-item `<li>` detection's `memcmp` compared 27 bytes
      against a 26-byte string literal, silently reading one byte into the
      literal's NUL terminator and comparing it against real HTML bytes
      that are never `\0` — so the whole branch never matched and
      `class="task-item"` silently never got added, even though the
      replacement logic itself was correct. Caught by actually running the
      test script and noticing the class was missing, not by inspection.
    - **The raw-HTML whitelist sanitizer**, ported from
      `MD::sanitizeHtmlTag()`/`isSafeUrl()` line-for-line into C
      (`php_mdhtml_sanitize_tag()`/`php_mdhtml_sanitize_html_literal()`/
      `php_mdhtml_is_safe_url()`) — same tag/attribute whitelists, same
      `onXXX`-handler rejection, same `data:image/*;base64` allowance
      excluding svg+xml. Applied as a tree pre-pass
      (`php_mdhtml_sanitize_raw_html_nodes()`) directly on
      `CMARK_NODE_HTML_BLOCK`/`CMARK_NODE_HTML_INLINE` literals via
      `cmark_node_set_literal()`, *before* `cmark_render_html()` — more
      precise than a whole-document regex pass (MD::'s only option, since
      its own parser never separates "raw HTML the user wrote" from other
      content the way cmark's node tree already does): it can only ever
      touch nodes that genuinely hold raw user-written HTML, never HTML
      cmark itself emits for other constructs.
    - **Emoji shortcodes**, full parity with `MD::`'s ~300-entry
      `$emojiMap`, ported verbatim into a static C array
      (`php_mdhtml_builtin_emoji[]`) loaded once into a `HashTable` in
      `MINIT` (module-lifetime, read-only). Substitution is a tree
      pre-pass over `CMARK_NODE_TEXT` literals only — structurally unable
      to touch code spans/blocks or raw HTML the way a whole-document
      regex has to be careful about, since those are different node
      types cmark already separated during parsing. Unknown shortcodes
      are left as-is, matching `MD::emojiFor()`.
    - **`MDHtml\RegisterEmoji(string $shortcode, string $char): void`**,
      mirroring `MD::registerEmoji()` — a second, request-scoped
      (`RINIT`/`RSHUTDOWN`) `HashTable` so registrations from one request
      never leak into the next in a long-running SAPI (module globals
      alone would persist for the process's whole lifetime, which is
      correct for the read-only builtin table but wrong for
      request-mutable overrides).
    - **Known, deliberate limitation**: `php_mdhtml_slugify()` only
      strips to an ASCII `\w` equivalent (letters/digits/underscore),
      unlike `MD::slugify()`'s PCRE Unicode `\w`. Replicating full Unicode
      word-character classification in C without going through PHP's own
      PCRE internals was judged out of scope for this round — revisit if
      a real Kirigami site needs non-ASCII heading anchors.
    - **Still PHP-side, not migrated this round** (needs either a PHP
      callback round-trip from C, or a genuine new block/inline
      `cmark_syntax_extension`, both bigger undertakings deferred rather
      than rushed): the `{% plugin %}` system, definition lists, GFM-style
      `> [!NOTE]` alerts, and the extended `==highlight==`/`^sup^`/`~sub~`
      inline syntax (the last three could become real `CMARK_NODE_CUSTOM_INLINE`
      splices — a text node split around the delimiter, wrapping the
      captured content in a custom node whose `on_enter`/`on_exit` are
      literal `<mark>`/`</mark>` etc. — cmark's built-in "custom" node type
      needs no new `cmark_syntax_extension` machinery for this; not
      attempted yet).
11. **Diff-tested against `MD::toHtml()`'s actual current output**
    (2026-09-12, a real corpus — headings incl. `{#id}`, emphasis, lists,
    links incl. reference-style, images, blockquote, fenced/indented code,
    tables, tasklists, footnotes, hr, autolinks, raw HTML passthrough — not
    just "looks right by eye"). After the fixes in point 10: headings and
    autolinks are byte-identical; links/images/tables only differ in HTML
    attribute *order* (semantically identical, not worth chasing); code
    blocks keep a trailing newline before `</code>` (harmless — a single
    trailing newline inside `<pre>` doesn't render as a visible blank
    line); blockquote soft-wraps render as a literal `\n` instead of a
    space (also visually identical once a browser collapses HTML
    whitespace); `***bold+italic***` nests `<em><strong>` instead of
    `MD::`'s `<strong><em>` (equivalent rendering, different tag order,
    not worth chasing); the `lists` test case exposed a *pre-existing bug
    in `MD::` itself* (its single-pass regex list parser drops a
    `1.`/`2.` ordered list entirely when it immediately follows an
    unordered one with no blank line between — `MDHtml\Render()` renders
    both correctly) — noted here, not something to "fix" on the
    cmark-gfm side. **One real, structural difference, decided rather
    than silently picked**: footnote HTML — `MD::` emits
    `<div class="footnotes"><ol><li id="fn:label">`, cmark-gfm emits
    GitHub's own real markup, `<section class="footnotes" data-footnotes><ol><li id="fn-1">`
    plus an inner `<p>` and `aria-label`/`data-footnote-backref*` on the
    backref link. Checked `../kirigami/packages/canva/src/styles/
    prose.scss` first: its `.footnotes`/`.footnote-backref` rules are
    plain class selectors, so they keep matching cmark-gfm's output
    regardless of the element tag or the extra `data-*` attributes —
    nothing there breaks. **Decided: adopt cmark-gfm's convention**
    (GitHub's real, more accessible markup) rather than spend C effort
    forcing a byte-for-byte replica of `MD::`'s custom one; revisit only
    if some other part of kirigami turns out to depend on the exact
    `fn:label`/`div` shape (not found so far).
12. **The `{% plugin %}` system moved into the extension too**
    (maintainer, 2026-09-12 — "on peut pas faire de registerPlugin?" — a
    direct follow-up to point 10's "as much as possible in C" mandate,
    which had left this on the "needs a PHP callback round-trip, bigger
    undertaking" pile). Implemented as a synchronous PHP callback
    round-trip from C, same overall shape as points 10/11 already
    established, not a real `cmark_syntax_extension`:
    - `MDHtml\RegisterPlugin(string $name, callable $callback): void`,
      `MDHtml\UnregisterPlugin(string $name): void`, and
      `MDHtml\GetRegisteredPlugins(): array` mirror `MD::registerPlugin()`/
      `unregisterPlugin()`/`getRegisteredPlugins()` exactly. Callbacks are
      stored as plain `zval`s (refcounted via `ZVAL_COPY`) in a third,
      request-scoped `HashTable` (`MDHTML_G(plugins)`, same `RINIT`/
      `RSHUTDOWN` pattern as `emoji_custom`) — not a `zend_fcall_info`/
      `_cache` pair, since those reference transient call-frame state and
      this callback must survive to be invoked much later, well after
      `RegisterPlugin()`'s own call frame is gone. Validated with
      `zend_is_callable()` at registration time (throws a `TypeError` via
      `zend_argument_type_error()` otherwise, matching what a real
      `callable`-typed engine parameter would do).
    - `php_mdhtml_extract_plugins()` scans the *raw Markdown string* for
      `{% name args %}` / `{% name args\nbody\n%}` spans — before any
      cmark parsing happens, same as `MD::`'s own STEP 2, and for the same
      reason: there is no tree yet to hook a real syntax extension into,
      and a real one would still need this same callback-into-PHP
      machinery for the body. For each registered name, calls the stored
      callback via `call_user_function()` with `($args, $body)`
      (`$args` built by `php_mdhtml_parse_plugin_args()`, a C port of
      `MD::`'s `$parseArgs` closure — bare words, `"double"`/`'single'`
      quoted with backslash escapes), stringifies the return value
      (`zval_get_string()`), and replaces the matched span with an
      `\x02PLG<n>\x03` placeholder (same STX/ETX control-byte convention
      `MD::` uses — bytes with no Markdown meaning of their own, so cmark
      carries them through its own text-escaping completely unchanged).
      An **unregistered** name is left completely untouched in the
      Markdown — unlike `MD::`, which explicitly `htmlspecialchars()`s an
      unknown tag, this doesn't need to: cmark's own text-node HTML
      escaping already makes a literal `{% unknown %}` safe once it's
      just ordinary paragraph text.
    - `php_mdhtml_inject_plugins()` runs *after* `cmark_render_html()` and
      `php_mdhtml_postprocess()`, substituting each placeholder for its
      real output. **The one genuinely tricky part**: a block-style
      plugin used on its own line ends up as the *entire* content of a
      `<p>...</p>` once cmark renders it — if left alone, a plugin
      returning block-level HTML (a `<div>`, an `<iframe>`, ...) would end
      up illegally nested inside that `<p>`. `MD::` avoids this via its
      own STEP 13 paragraph-wrapping logic, which special-cases a line
      starting with `\x02PLG` as a "block line" to never wrap. Since
      `php_mdhtml_inject_plugins()` runs on the *already-rendered* HTML
      (cmark did the wrapping already, unaware any of this was coming),
      the fix has to happen the other way around: detect a placeholder
      immediately preceded by `<p>` (checking the last 3 bytes already
      written to the output buffer) and immediately followed by `</p>`,
      and in that case emit the plugin's raw output in place of the whole
      `<p>PLACEHOLDER</p>`, not just the placeholder. Verified directly —
      a block-style `checklist` plugin (multi-line body) comes out
      correctly unwrapped (`<div class="checklist">...</div>`, no
      surrounding `<p>`), while an inline `{% codepen %}` used mid-sentence
      stays correctly inline inside its paragraph.
    - **Known, deliberate limitation, unlike `MD::`**: a `{% %}` tag
      written *inside* a code span/block in the source markdown is only
      protected from being treated as a live plugin invocation for
      ```` ``` ```` fenced blocks (tracked via a simple "does this line
      start with ```` ``` ````" toggle maintained during the same scan).
      A single-backtick inline code span containing literal `{% %}` text
      is **not** currently excluded and would still fire the plugin.
      `MD::` protects against this unconditionally (it extracts first,
      then swaps back to literal text if the placeholder later turns out
      to have landed inside extracted code). Verified the fenced-block
      case works (`` ``` \n{% codepen abc %}\n``` `` renders the tag
      literally, `{% codepen abc %}` used for real right after still
      renders the plugin) — the inline-span gap is undocumented-but-real,
      revisit if it matters in practice (e.g. documentation *about* the
      plugin syntax written in a single-backtick span).
    - Verified end-to-end with inline usage embedded mid-sentence, a
      multi-line block usage, an unregistered name (stays literal),
      `GetRegisteredPlugins()` before/after `UnregisterPlugin()`, and the
      fenced-code-block protection above — all via a throwaway
      `vendor/build/test.php`/`fence-test.php` (scratch, not committed).
13. **The rest of point 10's "still PHP-side" list — `==highlight==`/
    `^sup^`/`~sub~`, GFM-style `> [!NOTE]` alerts, and definition lists —
    moved into the extension too**, closing out the migration point 10
    started. Three different mechanisms, matched to what each syntax
    actually needs:
    - **`==highlight==`/`^sup^`/`~sub~`**: the `CMARK_NODE_CUSTOM_INLINE`
      splice sketched in point 10 — `php_mdhtml_wrap_delim()` splits a
      `TEXT` node into `[before, wrap, after...]` around each delimited
      span, where `wrap` is a `CMARK_NODE_CUSTOM_INLINE` (cmark's own
      "opaque literal HTML fragment" node — no `cmark_syntax_extension`
      needed) with `on_enter`/`on_exit` set to the literal `<mark>`/
      `</mark>` etc. and a `TEXT` child holding the captured content.
      Generalized to accept a delimiter *string* (not just a char) so the
      same function handles `^`/`~` (1 byte) and `==` (2 bytes). Three
      separate tree walks (fresh `cmark_iter` each, per the established
      "collect then mutate" pattern), one per delimiter, run after
      emoji/before heading-id computation. **Real bug found and fixed**:
      `H~2~O` rendered as `H<del>2</del>O`, not `H<sub>2</sub>O` — cmark-gfm's
      own strikethrough extension accepts a *single* tilde as
      strikethrough by default (only real GFM/GitHub restricts it to
      `~~double~~`), so it was consuming `~2~` during parsing before the
      subscript pass ever saw a literal `~`. Fixed by adding
      `CMARK_OPT_STRIKETHROUGH_DOUBLE_TILDE` to the parser options —
      caught by testing subscript directly against strikethrough in the
      same input, not by inspection.
    - **GFM-style alerts** (`> [!NOTE]`, `[!TIP]`, `[!IMPORTANT]`,
      `[!WARNING]`, `[!CAUTION]`): a real tree transform
      (`php_mdhtml_process_alerts()`), since — unlike headings/links/
      tasklist, which are cheap string patches on cmark's *own* rendered
      output — a blockquote's rendering has to change from `<blockquote>`
      to `<div class="markdown-alert ...">`, and cmark's HTML renderer has
      no per-node-instance hook for a built-in type like
      `CMARK_NODE_BLOCK_QUOTE`. Solved by: detecting a `BLOCK_QUOTE` whose
      first paragraph's first child is a `TEXT` node whose literal is
      *exactly* one of the five markers (matching `MD::`'s own
      `[!TYPE]`-alone-on-its-line requirement); stripping that marker
      `TEXT` node and the `SOFTBREAK`/`LINEBREAK` after it; rendering the
      (now marker-free) blockquote's remaining children early, via a
      scratch `CMARK_NODE_DOCUMENT` that steals them
      (`php_mdhtml_render_alert_body()`, passing the *same*
      `cmark_parser_get_syntax_extensions(parser)` list the real render
      uses, so nested tables/tasklists/etc. inside an alert still work);
      building the final `<div class="markdown-alert markdown-alert-{type}">
      <p class="markdown-alert-title">{LABEL}</p>{content}</div>` by hand;
      and swapping the whole blockquote for a single
      `CMARK_NODE_HTML_BLOCK` holding that literal via
      `cmark_node_replace()` — which, under `CMARK_OPT_UNSAFE`, survives
      into the real render completely untouched. **Processed in *reverse*
      document order, deliberately** (collected via one top-down
      `cmark_iter` pass first, then walked back-to-front): a nested
      blockquote (an alert inside another blockquote, or an alert inside
      an alert) has to be converted *before* its ancestor, or converting
      the ancestor first would steal the nested one into a scratch
      document, render it, and free it — leaving the *already-collected*
      array holding a dangling pointer to it, a real use-after-free the
      first time the loop reached that entry. Verified: a plain alert, an
      alert containing a real GFM list, and — the actual nesting case
      forward order would have crashed on — an alert containing a
      genuine, non-alert nested blockquote (correctly stays a real
      `<blockquote>`, not converted).
      **Heading-id ordering note**: `php_mdhtml_compute_heading_ids()`
      must run *before* `php_mdhtml_process_alerts()`, not after — a
      heading inside an about-to-become-an-alert blockquote still needs
      counting in the tree-walk (it's a real `CMARK_NODE_HEADING` at that
      point), even though by the time the *string* post-process later
      scans for `<h1>`-`<h6>` tags, that heading only exists as inert text
      inside the alert's pre-baked `HTML_BLOCK` literal — the id still
      gets assigned correctly since both passes count/scan headings in
      the same left-to-right document order regardless of which one ends
      up literal-vs-real by the time the final string exists.
    - **Definition lists** (`Term` / `: Definition`): the only one of the
      three that *can't* be a tree transform, because the syntax isn't
      CommonMark at all — with no blank line between them, "Term" and
      ": Definition" are just two soft-wrapped lines of the *same*
      paragraph as far as cmark is concerned (nothing about a leading `:`
      makes a new block start), so by the time there's a tree, cmark has
      already merged them into one `<p>Term\n: Definition</p>`. Implemented
      as a post-render *string* pass instead
      (`php_mdhtml_process_definition_lists()`, run right after
      `php_mdhtml_postprocess()`): finds a `<p>` whose content, split on
      literal `\n`, is a "term" line followed by one-or-more `:`-prefixed
      "definition" lines, and rewrites the whole thing to a `<dl>`;
      consecutive such `<p>`s separated only by whitespace merge into one
      `<dl>`, the same continuation behavior `MD::extractDefinitionLists()`
      implements. Unlike that PHP version, this one does **not** skip a
      `<p>` just because it contains inline HTML tags (`<strong>`, `<code>`,
      a link, ...) — since cmark only ever wraps genuine inline-level
      paragraph text in `<p>` in the first place (block content never ends
      up there), any tags found inside are safe to carry straight into
      `<dt>`/`<dd>` verbatim; verified with a bold term and an *emphasized*
      `code`-containing definition.
    - Full diff-test corpus (from point 11) re-run after all three — no
      regressions.

## Relationship to other repos

- **`php-wasm-compiler`** (github.com/php-kirigami/php-wasm-compiler,
  sibling checkout at `../php-wasm-compiler`): builds the core `php.wasm`
  (JSPI, Node-only) and vendors third-party PHP extensions for it. Its
  `cli.mjs compile-extension` subcommand is a fast, disposable smoke-test
  harness (compiles just one extension against already-built PHP headers,
  no full `php.wasm` rebuild) — used there to validate `krakjoe/cmark` and
  `sodium` during development, and the natural mechanism to validate
  php-mdhtml's WASM build too, once we get there (point 6). Its CLAUDE.md
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

**✅ First native build succeeds and produces correct output.** Built and
tested end-to-end natively (WSL Ubuntu 26.04, PHP 8.5.4, cc 15.2.0,
cmake 4.2.3 — no Emscripten/Docker involved, per point 6):

- `vendor/build/stage.sh` (not committed, regenerated on demand — see
  `.gitignore`): downloads `github/cmark-gfm` tag `0.29.0.gfm.13`, builds
  `libcmark-gfm_static` + `libcmark-gfm-extensions_static` via CMake
  (needed `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` — the upstream
  `CMakeLists.txt`'s own minimum predates CMake 4.x, which dropped that
  compatibility), and stages the two `.a` files plus the public headers
  into `vendor/libcmark-gfm/{include,lib}`.
- `config.m4` links against the staged `vendor/libcmark-gfm` via
  `PHP_ADD_INCLUDE`/`PHP_ADD_LIBRARY_WITH_PATH` (relative paths — an
  earlier attempt using `$ext_srcdir` failed the `test -f` guard, since
  that variable isn't set yet at the point `PHP_ARG_ENABLE` runs in a
  standalone `phpize` build; config.m4 already executes with cwd at the
  extension root, so plain relative paths are correct here).
- `md.c` now implements the real `MDHtml\Render()`: registers the GFM
  extensions once (`cmark_gfm_core_extensions_ensure_registered()`),
  attaches `table`/`strikethrough`/`autolink`/`tagfilter`/`tasklist` to a
  `cmark_parser_new(CMARK_OPT_UNSAFE | CMARK_OPT_FOOTNOTES)`, feeds it the
  input, and renders via `cmark_render_html()` — passing
  `cmark_parser_get_syntax_extensions(parser)` so extension node types
  (tables, tasklists, strikethrough) actually get rendered rather than
  silently dropped. `CMARK_OPT_UNSAFE` is deliberate: raw HTML passes
  through untouched for `MD::`'s own stricter whitelist sanitizer to
  handle afterward, matching what `MD::toHtml()` already does today.
  cmark's returned buffer is freed with plain `free()` (not `efree()`) —
  it came from the default libc-based allocator, since `cmark_parser_new()`
  (not `_with_mem()`) was used.
- Smoke-tested with a single Markdown sample covering every structural
  feature in scope (headings, bold/italic, links, nested lists, task
  list checkboxes, a table, strikethrough, an autolink, a blockquote, a
  fenced code block with a language class, and a footnote
  definition/reference): every feature rendered correctly, footnotes
  included (see point 2's correction — no PHP-side handling needed for
  those after all).

**✅ Round 2 (2026-09-12, points 9-11 above): renamed to php-mdhtml, moved
heading ids/`{#id}`/link+image attrs/task-list shape/raw-HTML whitelist/
emoji into the extension, diff-tested against `MD::toHtml()`, decided the
footnote-markup question.** `mdhtml.c` is ~1300 lines now. Verified with
both the feature smoke test (`vendor/build/test.php`) and the full diff
corpus (`vendor/build/diff-test.php`, both scratch files, not committed)
against a real checkout of `../kirigami`.

**✅ Round 3 (2026-09-12, point 12 above): the `{% plugin %}` system moved
into the extension.** `MDHtml\RegisterPlugin()`/`UnregisterPlugin()`/
`GetRegisteredPlugins()`, a raw-Markdown-string pre-scan calling
registered callbacks via `call_user_function()`, and a post-render
placeholder substitution with a paragraph-unwrap fix-up for block-style
plugins. Verified: inline usage, block usage (correctly unwrapped from
its `<p>`), an unregistered name (stays literal), register/unregister
round-trip, and the ```` ``` ```` fenced-code-block protection.

**✅ Round 4 (2026-09-12, point 13 above): `==highlight==`/`^sup^`/`~sub~`,
GFM alerts, and definition lists moved into the extension.** This closes
out point 10's entire "still PHP-side" migration list — every
`MD::toHtml()` post-processing step now has a C-side equivalent in
`mdhtml.c` (~1850 lines) except the two deliberate, documented gaps below.
Full diff-test corpus re-run, no regressions.

**MD::toHtml() itself has not been changed to actually call
`MDHtml\Render()` yet** — that integration (making `kirigami/php-prepros`
depend on this extension and rewiring `MD::toHtml()` to delegate its
structural core to it) is a `kirigami`-repo change, not a `php-mdhtml`
one, and hasn't been started.

**Not done yet:**

1. Point 12's known limitation: a `{% %}` written inside a single-
   backtick inline code span isn't protected from being treated as a live
   plugin invocation (only ```` ``` ```` fenced blocks are).
2. `php_mdhtml_slugify()`'s ASCII-only limitation (point 10) — revisit if
   a real non-ASCII heading anchor is needed.
3. Wiring `kirigami/php-prepros`'s `MD::toHtml()` to actually call
   `MDHtml\Render()` for its structural core, replacing the ~800 lines of
   regex it currently uses for that part — a `kirigami` repo change, not
   started.
4. Only once that integration is done and validated against a real site:
   port the build to Emscripten/WASM (JSPI), following
   `php-wasm-compiler`'s own Dockerfile conventions, and settle the
   vendoring question (point 5). Not started — everything so far is
   native-only (point 6).
