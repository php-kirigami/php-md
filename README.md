<div align="center">

<img src="https://zmotrin.github.io/assets/kirigami/kirigami-logo-universal.svg" alt="Kirigami" width="400" />

---

# php-mdhtml

A fast CommonMark + GitHub Flavored Markdown PHP extension, built on **cmark-gfm**.

[![License: GPL-2.0-or-later](https://img.shields.io/badge/license-GPL--2.0--or--later-yellow)](./LICENSE)
[![PHP 8.5](https://img.shields.io/badge/php-8.5-777bb4)](https://www.php.net/releases/8.5/)
[![cmark-gfm 0.29.0](https://img.shields.io/badge/cmark--gfm-0.29.0-blue)](https://github.com/github/cmark-gfm)
[![Website](https://img.shields.io/badge/website-php--kirigami.github.io-1f6b4a)](https://php-kirigami.github.io)

</div>

---

## Overview

`php-mdhtml` parses CommonMark + GFM (tables, tasklists, strikethrough,
autolinks, footnotes) via [cmark-gfm](https://github.com/github/cmark-gfm)
and exposes it to PHP as a single namespaced function,
`\MDHtml\Render()`, plus a small set of extras — heading anchors, emoji
shortcodes, a `{% plugin %}` system with real PHP callbacks, GFM-style
alerts, definition lists, and `==highlight==`/`^sup^`/`~sub~` — all done
in C, not PHP.

It exists to accelerate `MD::toHtml()`, the Markdown renderer in
[kirigami/php-prepros](https://github.com/php-kirigami/kirigami/blob/main/packages/php-prepros/src/libraries/md.class.php),
whose CommonMark/GFM structural parsing is currently ~800 lines of
hand-written regex. `php-mdhtml` replaces that structural core with a
real C parser while keeping the same output.

Part of the **Kirigami** project ecosystem.

---

## Table of contents

- [php-mdhtml](#php-mdhtml)
  - [Overview](#overview)
  - [Table of contents](#table-of-contents)
  - [Why not the existing PECL cmark extension?](#why-not-the-existing-pecl-cmark-extension)
  - [Status](#status)
  - [API](#api)
    - [`MDHtml\Render(string $markdown): string`](#mdhtmlrenderstring-markdown-string)
    - [`MDHtml\RegisterEmoji(string $shortcode, string $char): void`](#mdhtmlregisteremojistring-shortcode-string-char-void)
    - [`MDHtml\RegisterPlugin(string $name, callable $callback): void`](#mdhtmlregisterpluginstring-name-callable-callback-void)
    - [`MDHtml\UnregisterPlugin(string $name): void`](#mdhtmlunregisterpluginstring-name-void)
    - [`MDHtml\GetRegisteredPlugins(): array`](#mdhtmlgetregisteredplugins-array)
  - [Extended syntax](#extended-syntax)
  - [Building from source](#building-from-source)
  - [Requirements](#requirements)
  - [Related](#related)
  - [License](#license)
  - [Author](#author)

---

## Why not the existing PECL cmark extension?

[`krakjoe/cmark`](https://github.com/krakjoe/cmark) already wraps plain
`cmark` for PHP, but it's unmaintained since 2019, wraps upstream `cmark`
rather than `cmark-gfm` (no tables/tasklists/strikethrough/autolink), and
hand-mimics PHP's internal `zend_object` struct with hardcoded field
offsets — a pattern that silently breaks (heap corruption, a crash on
object destruction) whenever PHP's real `zend_object` layout changes,
which it did between 2019 and PHP 8.5. Patching that fork further would
mean repeating the same fragile trick for every GFM node type it would
need. `php-mdhtml` is a small extension written from scratch against
`cmark-gfm` directly, with an idiomatic object model (in practice: no
object model at all — see [API](#api)) instead of a hand-rolled one.

See [CLAUDE.md](CLAUDE.md) for the full investigation and every decision
made along the way.

---

## Status

Native build only, works, diff-tested against `MD::toHtml()`'s real
output. **Not yet wired into anything**: `kirigami/php-prepros`'s
`MD::toHtml()` doesn't call `MDHtml\Render()` yet, and there's no
Emscripten/WASM build yet (the eventual target — see
[php-wasm-compiler](https://github.com/php-kirigami/php-wasm-compiler)).
See [CLAUDE.md](CLAUDE.md)'s "Status" section for exactly what's done and
what isn't.

---

## API

### `MDHtml\Render(string $markdown): string`

Parses and renders Markdown to HTML. Covers the CommonMark+GFM structural
core (headings, emphasis, lists, blockquotes, code, links, images,
thematic breaks, tables, tasklists, strikethrough, autolinks, footnotes)
plus everything under [Extended syntax](#extended-syntax).

```php
echo \MDHtml\Render("# Hello\n\n- one\n- two\n");
// <h1 id="hello">Hello</h1>
// <ul>
// <li>one</li>
// <li>two</li>
// </ul>
```

Raw HTML in the source passes through a whitelist sanitizer (same
tag/attribute allow-list as `MD::sanitizeHtmlTag()`) rather than being
escaped or stripped outright.

### `MDHtml\RegisterEmoji(string $shortcode, string $char): void`

Registers (or overrides) a `:shortcode:` emoji for the rest of the
request. Over 300 are built in already, ported from `MD::$emojiMap`.

```php
\MDHtml\RegisterEmoji('kirigami', '📐');
echo \MDHtml\Render('Made with :kirigami:');
// <p>Made with 📐</p>
```

### `MDHtml\RegisterPlugin(string $name, callable $callback): void`

Registers a `{% name args %}` (inline) / `{% name args\nbody\n%}` (block)
tag. The callback receives `(array $args, string $body)` and returns the
HTML to splice in; a block-style plugin's output is correctly unwrapped
from the paragraph cmark would otherwise wrap it in.

```php
\MDHtml\RegisterPlugin('codepen', function (array $args, string $body): string {
    $id = htmlspecialchars($args[0] ?? '', ENT_QUOTES, 'UTF-8');
    return "<iframe src=\"https://codepen.io/embed/{$id}\"></iframe>";
});

echo \MDHtml\Render('{% codepen abc123 %}');
// <p><iframe src="https://codepen.io/embed/abc123"></iframe></p>
```

An unregistered tag name is left as literal text.

### `MDHtml\UnregisterPlugin(string $name): void`

Removes a registered plugin.

### `MDHtml\GetRegisteredPlugins(): array`

Returns every currently registered plugin name.

---

## Extended syntax

None of these are CommonMark or GFM — they're conventions
`kirigami/php-prepros`'s `MD::toHtml()` already supports, reproduced here
in C:

| Syntax | Renders as |
|---|---|
| `` `{#custom-id}` `` after a heading | `<h2 id="custom-id">…</h2>` instead of an auto-generated slug |
| `==highlighted==` | `<mark>highlighted</mark>` |
| `^superscript^` | `<sup>superscript</sup>` |
| `~subscript~` | `<sub>subscript</sub>` |
| `> [!NOTE]` / `[!TIP]` / `[!IMPORTANT]` / `[!WARNING]` / `[!CAUTION]` | GitHub-style `<div class="markdown-alert markdown-alert-*">` |
| `Term`⏎`: Definition` | `<dl><dt>Term</dt><dd>Definition</dd></dl>` |

External links get `target="_blank" rel="noopener noreferrer"`; images
get `loading="lazy"`; a GFM task list's `<ul>` gets `class="task-list"`
and each `<li>` gets `class="task-item"` — matching `MD::toHtml()`'s own
output shape, not cmark-gfm's bare defaults.

---

## Building from source

Native build (fast iteration, no Docker/Emscripten):

```bash
cd vendor/build && bash stage.sh   # downloads + builds libcmark-gfm
cd ../..
phpize
./configure --with-mdhtml
make
```

`vendor/` is gitignored and rebuilt from source every time — nothing
vendored is hand-edited. There is no Emscripten/WASM build yet (see
[Status](#status)).

---

## Requirements

- PHP `>= 8.5`
- A C compiler, `phpize`, `cmake` (to build the vendored `libcmark-gfm`)

---

## Related

- [`cmark-gfm`](https://github.com/github/cmark-gfm) — the C library this
  extension wraps
- [kirigami/php-prepros](https://github.com/php-kirigami/kirigami/tree/main/packages/php-prepros) —
  `MD::toHtml()`, the renderer this extension accelerates
- [php-wasm-compiler](https://github.com/php-kirigami/php-wasm-compiler) —
  builds the `php.wasm` runtime this extension will eventually target

---

## License

GPL-2.0-or-later. See [LICENSE](./LICENSE) for the full text.

## Author

Maxime Larrivée-Roy
