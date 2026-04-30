# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `MdParser\Options::headingAnchors` — when true, every rendered
  `<hN>` gets an `id` attribute holding a GitHub-style slug of the
  heading's text. Slugs lowercase ASCII, replace whitespace runs with
  a single `-`, drop other ASCII punctuation, preserve UTF-8
  multibyte bytes, and dedupe collisions with `-1`, `-2`, ...
  Headings whose text slugifies to nothing (pure punctuation) emit
  `<hN>` with no id rather than `id=""`. Coexists with `sourcepos`:
  the `id` lands before `data-sourcepos`.
- `MdParser\Options::nofollowLinks` — when true, every emitted
  `<a href="...">` gets `rel="nofollow noopener noreferrer"` injected
  for inline links, reference links, and autolinks. Applies to
  `toHtml()` and `toInlineHtml()`. Anchors inside fenced or inline
  code are left untouched because cmark escapes them before reaching
  the postprocess step. In-document fragment anchors (`href="#..."`,
  i.e. footnote references and backrefs) are intentionally skipped.
  Raw `<script>` / `<style>` regions under `unsafe: true` are emitted
  verbatim so anchor-shaped substrings inside JavaScript or CSS are
  not corrupted.

Both flags default to `false`. They are pure HTML post-passes; XML
and AST output are unaffected. The static `Parser::html()` /
`Parser::xml()` shortcuts use the module defaults and so do not
apply either transform.

Heading anchors are positioned by rendering each AST heading
standalone and locating its exact byte sequence in the document
HTML, rather than by counting line-start `<hN>` tags. Under
`unsafe: true`, raw HTML headings written directly in the markdown
source are therefore left alone and do not consume slugs intended
for real headings.

### Fixed

- `config.w32` now lists `mdparser_html_postprocess.c` so Windows
  builds link successfully.

## [0.2.0] - 2026-04-11

### Added

- `MdParser\Parser::html(string)`, `MdParser\Parser::xml(string)`,
  and `MdParser\Parser::ast(string)` — static one-shot shortcuts
  that parse with the default Options and return the corresponding
  output without the `new Parser()` boilerplate. Mirrors
  `Markdown::defaultTransform()` from michelf/php-markdown. Use for
  simple scripts and migration from libraries with a static API.
- `MdParser\Parser::toInlineHtml(string)` — renders inline-only
  HTML with no `<p>` wrapper. Block-level markers (`#`, `-`, `>`,
  `1.`, 4-space indents) become literal text rather than being
  parsed as headings / lists / blockquotes / code blocks. Matches
  the semantics of Parsedown's `line()` and
  cebe/markdown's `parseParagraph()` so users migrating from
  those libraries have a drop-in path for rendering short strings
  (chat messages, table cells, user display names) without the
  surrounding paragraph tags. Implemented by prepending a
  zero-width space (U+200B) before feeding cmark, forcing the
  entire input into paragraph context, then stripping the
  sentinel plus the `<p>` / `</p>` wrappers from the output.
- `MdParser\Options::strict()`, `MdParser\Options::github()`, and
  `MdParser\Options::permissive()` — static factory presets for
  common deployment patterns. `strict()` is the standard defaults
  plus `autolink: false` so bare URLs in untrusted input stay inert
  text. `github()` adds `footnotes: true` to match github.com's
  rendered feature set. `permissive()` sets `unsafe: true`,
  `tagfilter: false`, `liberalHtmlTag: true` for trusted input where
  raw HTML should pass through. All three coexist with the full
  17-bool named-argument constructor.
- Hard cap on input size (`MDPARSER_MAX_INPUT_SIZE`, 256 MB). Inputs
  past the cap throw `MdParser\Exception` at the wrapper boundary
  rather than depending on cmark's `int32_t` `bufsize_t` edge.
- Hard cap on `toAst()` recursion depth (`MDPARSER_MAX_AST_DEPTH`,
  1000 levels). Deeply-nested markdown like `> ` × 50000 now throws
  `MdParser\Exception` instead of smashing the C stack during the
  recursive AST walk. `toHtml()` and `toXml()` were already safe
  because cmark's own renderers are iterative.

### Changed

- Per-parse extension attachment is now a bitmask loop over cached
  `cmark_syntax_extension*` pointers resolved once at MINIT, instead
  of five `cmark_find_syntax_extension()` linked-list walks plus
  `strcmp`s on every `toHtml()`/`toXml()`/`toAst()` call. Also
  hard-fails MINIT if any default-on extension (notably `tagfilter`)
  is missing from the cmark-gfm registry rather than silently running
  without the safety net.
- `Options` default masks are cached in `mdparser_default_cmark_options`
  / `mdparser_default_extension_mask` at MINIT. `mdparser_options_default_masks`
  collapses to a two-word copy.
- `Parser` and `Options` are both marked
  `ZEND_ACC_NOT_SERIALIZABLE`. For `Parser`, default PHP
  serialization would have silently dropped the cached
  `cmark_options` / `extension_mask` ints (they are not exposed as
  PHP properties), so `unserialize($parser)` would have yielded a
  parser running on defaults regardless of the original `Options`.
  `Options` is blocked alongside for consistency -- both carry
  derived state users should not round-trip through serialize.
  `MdParser\Exception` is intentionally left serializable so
  monolog / queue workers / PHPUnit failure reporting can still
  log thrown exceptions normally. Clone was already blocked on
  `Parser`; serialize now matches.
- AST walker: fixed-size `array_init_size(out, 8)` per node, interned
  key strings, `zend_hash_add_new` with precomputed hashes instead of
  re-hashing `"type"`/`"children"`/`"literal"`/... on every node,
  extension detection via `cmark_node_get_syntax_extension()` instead
  of a 6-way `strcmp` chain against the type name. The AST-only key
  strings are lazily initialized on the first `toAst()` call so users
  who only need `toHtml()`/`toXml()` don't pay the setup cost at
  module load.
- `toHtml`/`toXml`/`toAst` migrated from `Z_PARAM_STRING` to
  `Z_PARAM_STR`, matching modern `ext/standard` / `ext/dom` usage.
- Exception messages from `cmark_parser_finish` and renderer null
  returns now include the source length so bug reports land with at
  least the size of the offending input.

### Fixed

- `toAst()` on markdown containing footnotes previously emitted
  `'type' => '<unknown>'` nodes. Root cause: cmark-gfm's
  `cmark_node_get_type_string()` switch does not cover
  `CMARK_NODE_FOOTNOTE_REFERENCE` or `CMARK_NODE_FOOTNOTE_DEFINITION`
  and falls through to `"<unknown>"`. The AST walker now overrides
  both locally to `"footnote_reference"` / `"footnote_definition"`,
  surfaces the label via the `literal` field, and recurses the
  definition's children so the body (paragraphs / lists / etc.)
  is reachable.

### Security

- Parser and Options serialization blocked (see above) — prevents
  silent state loss across a serialize/unserialize round trip on
  Parser, and gives Options the same treatment for consistency.
- `toAst()` on deeply-nested markdown now throws cleanly at
  `MDPARSER_MAX_AST_DEPTH` instead of segfaulting via C stack
  exhaustion. Regression test in `tests/022_limits.phpt`.

## [0.1.1] - 2026-04-11

Release hygiene patch. Zero extension behavior change from 0.1.0 —
all changes are around the release infrastructure. Cut as a new
tag because 0.1.0 predates `composer.json` existing in the repo,
so Packagist silently skipped the 0.1.0 tag and PIE couldn't
resolve `iliaal/mdparser` without the `:@dev` constraint. 0.1.1 is
the first tag that has `composer.json` at the tagged commit.

### Added

- Root-level `composer.json` with `type: "php-ext"` and a full
  `configure-options` schema for PIE resolution. Mirrors the
  `iliaal/php_excel` conventions.
- README badges: Tests workflow, Windows Build workflow, GitHub
  release version, PHP-3.01 license, Follow @iliaa.
- `CONTRIBUTING.md` with requirements, bug-report guidance, PR
  workflow, test guidelines, code-style notes, and a vendored-cmark
  cherry-pick procedure.
- `.github/dependabot.yml` to auto-PR monthly updates for the
  `github-actions` ecosystem and the `bench/` composer dependencies.
- `scripts/pie-smoke.sh` — reproducible end-to-end build+install+
  smoke test in a clean `php:8.4-cli` Docker container, including
  the build-tool dependencies (`bison`, `libtool-bin`) that PIE
  itself requires. Used to verify the install path in
  `docs/installation.md`.

### Documented

- `pie install iliaal/mdparser` verified end-to-end in a clean
  `php:8.4-cli` Docker container: PIE downloads from Packagist,
  runs phpize + configure + make + install, and auto-enables the
  extension. Transcript and working command shown in
  `docs/installation.md`.
- PIE 1.4.0 requires `bison` and `libtool-bin` beyond a minimal
  PHP install; apt-get / brew install commands added to
  `docs/installation.md`.
- Narrow window after a new release tag where Packagist hasn't
  crawled the tag yet: documented the `pie install
  iliaal/mdparser:@dev` fallback that installs the master branch,
  and the Packagist "Force Update" button for manual refresh.

### Removed

- Legacy `pie.json` manifest. PIE now resolves via the canonical
  `composer.json` at the repo root.

### Fixed

- Windows release workflow's tag trigger was `['v*']`, which did not
  match SemVer tags without a leading `v` prefix. Widened to accept
  both `[0-9]*.[0-9]*.[0-9]*` and `v[0-9]*.[0-9]*.[0-9]*` forms.
- `release` job in `windows.yml` now has an explicit
  `permissions: contents: write` block. The default `GITHUB_TOKEN` on
  new GitHub repos has read-only contents scope, which blocked
  `php-windows-builder/release@v1` from creating the GitHub release.
- Dropped PHP 8.2 from the matrix. 8.2 lacks
  `zend_class_entry.default_object_handlers`, which `mdparser_parser.c`
  uses. `php_excel` already targets 8.3+; mdparser now matches.
- Added a static-inline compat shim for
  `zend_register_internal_class_with_flags` (added in PHP 8.4) so
  gen_stub's emitted arginfo compiles cleanly on 8.3.
- `.gitattributes` forcing LF on source files and `binary` on
  `tests/fixtures/commonmark-spec.txt` and every
  `tests/parity/**/fixtures/*` file so Windows runners don't
  autocrlf-convert the exact-byte comparison corpora.
- Windows tag trigger widened from `['v*']` to
  `['[0-9]*.[0-9]*.[0-9]*', 'v[0-9]*.[0-9]*.[0-9]*']` so both bare
  SemVer and v-prefixed tags fire the release build.

[0.1.1]: https://github.com/iliaal/mdparser/releases/tag/0.1.1

## [0.1.0] - 2026-04-11

First release. Native C CommonMark + GFM parser for PHP 8.3+.

### Added

- `MdParser\Parser` — final class, holds a precomputed cmark options
  bitmask plus extension mask, offers three entry points:
  - `toHtml(string $source): string`
  - `toXml(string $source): string`
  - `toAst(string $source): array`
- `MdParser\Options` — final readonly class, 17 bool fields:
  - Core cmark options: `sourcepos`, `hardbreaks`, `nobreaks`, `smart`,
    `unsafe`, `validateUtf8`, `githubPreLang`, `liberalHtmlTag`,
    `footnotes`, `strikethroughDoubleTilde`, `tablePreferStyleAttributes`,
    `fullInfoString`
  - GFM extension toggles: `tables`, `strikethrough`, `tasklist`,
    `autolink`, `tagfilter`
  - Safe defaults: `unsafe = false`, `validateUtf8 = true`,
    `tagfilter = true`, all GFM extensions on.
- `MdParser\Exception` — final, extends `\RuntimeException`.
- AST output: nested PHP arrays keyed by node type, with per-type fields
  (level, url, title, literal, list_type, list_start, list_tight,
  list_delim, alignments, is_header, checked) and optional sourcepos
  (start_line, start_column, end_line, end_column).
- Embedded cmark-gfm 0.29.0.gfm.13 (commit 587a12b) plus four targeted
  cherry-picks from cmark upstream for CommonMark 0.31 spec compliance
  (see `vendor/VENDOR.md` "Local modifications"). Compiled directly into
  the extension shared object. No external runtime dependency.
- 12 test suites covering smoke, options, option effects, XML, exception
  hierarchy, CommonMark 0.31 spec conformance (652/652, 100%), AST
  walker, parity against Parsedown / cebe-markdown / michelf-markdown,
  XSS/security regression, and footnotes.
- GitHub Actions CI: Linux matrix (PHP 8.3-8.5), macOS (8.3-8.4), ASAN
  job on 8.4, Windows build matrix via php/php-windows-builder.
- PECL `package.xml` manifest (PIE manifest added in 0.1.1-dev via
  the canonical `composer.json`).
- Full reference documentation under `docs/` (installation, parser,
  options, AST format, security, spec coverage).
- Runnable examples under `examples/` covering basic usage, options,
  AST walking, GFM features, footnotes, and safe-mode XSS handling.
- Benchmark harness under `bench/` comparing against Parsedown,
  cebe/markdown, and michelf/php-markdown — mdparser measures ~15-30x
  faster across 200 B / 1.8 KB / 200 KB corpora.

### Known limitations

- No `toCommonmark()` round-trip renderer yet.
- No streaming parse API. Source is parsed as a single buffer.
- No custom userland render hooks. Use `toAst()` if you need to walk
  the tree and emit custom output.

[Unreleased]: https://github.com/iliaal/mdparser/compare/0.2.0...HEAD
[0.2.0]: https://github.com/iliaal/mdparser/releases/tag/0.2.0
[0.1.0]: https://github.com/iliaal/mdparser/releases/tag/0.1.0
