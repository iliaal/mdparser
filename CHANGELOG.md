# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Removed

- `package.xml`, `scripts/gen_package_xml.php`, and
  `scripts/validate_package.php`. PECL is deprecated (the PHP
  Foundation's PIE is its successor and we publish via
  Packagist + PIE), so the 422-line PECL manifest and its
  generator/validator tooling were dead weight. If you need a
  PECL-style tarball against a local mirror, run `pecl package`
  on an older checkout at 0.1.1 or earlier.
- `PECL` keyword dropped from `composer.json`.
- PECL install instructions removed from `README.md` and
  `docs/installation.md`. `PECL` still appears as a one-line
  description of what PIE is ("the PHP Foundation's PECL
  successor") for readers familiar with the older tool.

### Added

- `scripts/check_version.sh` — minimal bash version cross-check
  between `PHP_MDPARSER_VERSION` in `php_mdparser.h` and the top
  `## [X.Y.Z]` section of `CHANGELOG.md`, plus a SemVer 2.0.0
  sanity check. Replaces the PECL-focused
  `scripts/validate_package.php`. Used by the release checklist
  in `CONTRIBUTING.md`.

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

[Unreleased]: https://github.com/iliaal/mdparser/compare/0.1.1...HEAD
[0.1.0]: https://github.com/iliaal/mdparser/releases/tag/0.1.0
