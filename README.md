# php-md

A fast CommonMark + GitHub Flavored Markdown (tables, tasklists,
strikethrough, autolinks) PHP extension, built on
[cmark-gfm](https://github.com/github/cmark-gfm).

**Status: under active development, not usable yet.** See
[CLAUDE.md](CLAUDE.md) for the full architecture context and current
progress.

## Why

Part of the [Kirigami](https://github.com/php-kirigami) ecosystem. It
exists to accelerate the `MD::toHtml()` Markdown renderer in
[kirigami/php-prepros](https://github.com/php-kirigami/kirigami/blob/main/packages/php-prepros/src/libraries/md.class.php),
whose CommonMark/GFM structural parsing (headings, lists, emphasis, links,
tables, tasklists, etc.) is currently done with hand-written regexes.
php-md replaces that structural core with a real C parser while keeping
`MD::toHtml()`'s output — and its Kirigami-specific extensions (plugins,
emoji, footnotes, definition lists, GFM-style alerts, the raw-HTML
whitelist) — exactly as they are today.

It also targets [php-wasm-compiler](https://github.com/php-kirigami/php-wasm-compiler)'s
`php.wasm` runtime (Emscripten/JSPI), so Kirigami sites can render Markdown
without a native PHP install.

## License

GPL-2.0-or-later.

## Author

Maxime Larrivée-Roy
