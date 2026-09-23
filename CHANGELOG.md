# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.6.1] - 2026-09-03

### Added

- `tests/084_review_followups.phpt` pins the empty-input contracts of all four entry points, the `githubPreLang` true/false identical-output invariant, and a dynamic stub-vs-runtime Options default agreement check with no hard-coded counts.
- The input-size suite now renders an exactly-256MB input on all four entry points instead of trusting the cap comparison; each `029_regressions.phpt` section names its originating commit.
- The OOM sweep fault-injects the callback layer too: a refused callback allocation discards partial state and aborts with a distinct sentinel, covered over the full corpus.

### Changed

- AST text coalescing stages fragments in a geometric buffer, linearizing fragment-dense input; entity decoding avoids per-entity temp allocations in AST/XML; the XML escaper shares the HTML unrolled scan. Measured entity-dense AST −18%, literal-heavy XML −25%.
- `toInlineHtml()` normalization is extracted to `mdparser_inline_normalize()` and the HTML slug code to `mdparser_md4c_slug.c`, with byte-identical output proven by differential runs. No behavior change.
- Genuine libc allocation failures now report the memory error instead of the parse error; all three memory-error messages share one wording naming the failed allocation or the `parse_memory_limit` budget.
- The benchmark harness records its environment (PHP build, OPcache/JIT), defaults to 300 iterations for sub-KB corpora, and reports median/p95 with an empty-closure baseline subtracted.
- Docs refresh: supported-versions table, vendor-patch inventory, spec-baseline attribution, parity counts, and the prior `.review/` notes against the md4c tree; inert Options flags are annotated in the stub.

### Fixed

- `config.w32` gains `mdparser_md4c_slug.c`, matching `config.m4` after the renderer split.
- CI: the Linux build step no longer masks make failures; per-job skip budgets name their test; the ZTS canary uses the documented selector with an in-job assert; the OOM sweep is a blocking job; Windows lanes smoke-test the packaged DLL.

### For contributors

- md4c refresh deferred: upstream `master` is one no-op commit ahead (`bed011f`, drops the C89 `inline` shim); see `.upstream/md4c.yml` for the audit and re-check date.


## [0.6.0] - 2026-08-30

### Added

- `Options::$insert` maps md4c's `MD_FLAG_INSERT`: `++text++` renders as `<ins>`, the counterpart to strikethrough's `<del>`. Off by default.
- `Options::$preserveBlankLines` maps md4c's `MD_FLAG_PRESERVEBLANKLINES`: blank-line runs between blocks are reported rather than discarded, as `<blank />` in `toXml()` and a `blank` node in `toAst()`. HTML output is unchanged. Off by default.
- `mdparser.parse_memory_limit` (default `128M`, `PHP_INI_ALL`, `0` or negative for unlimited) caps the libc working set of a single parse. md4c's memory is invisible to `memory_limit`, and markdown amplifies it (about 72 bytes per `[` byte, 40 to 56 per `>` byte), so a few megabytes of hostile input could ask for gigabytes. Crossing the limit throws `MdParser\Exception`.

### Security

- Fixed a double free in md4c's attribute builder: `md_free_attribute()` keyed its frees on `substr_alloc`, so a failed growth realloc had `md_build_attribute()` and its caller both free the same three buffers. The same test also leaked when the first growth realloc failed.
- Fixed a double free of a link reference definition's label when the multiline-title merge ran out of memory; the hashtable already owns the label and frees it at parse end.
- Out-of-memory errors from `md_add_label_def()` and both `md_end_current_block()` call sites are now propagated instead of being reported as "not a reference definition" or dropped outright.

### Changed

- `validateUtf8` now replaces invalid UTF-8 with one U+FFFD per maximal subpart rather than one per byte, matching the Unicode, W3C, and WHATWG policy. A truncated `E2 82` or `F0 9F` yields one replacement character instead of several; out-of-range second bytes such as `E0 9F` still split per offending byte. Applies to `toHtml()`, `toXml()`, and `toAst()` alike.
- Refreshed vendored md4c from `0.5.3+git10c0158` to `0.5.3+git61f5ce7` (21 commits). CommonMark conformance is unchanged at 652/652.

### For contributors

- Added `tests/oom/`, an ASAN sweep that fails md4c's n-th allocation over a corpus. It reaches every out-of-memory path a document can hit, rather than only the ones near a `mdparser.parse_memory_limit` boundary.

## [0.5.0] - 2026-07-27

### Changed

- Enabled GitHub-style alerts as well as footnotes in `Options::github()`.
- Bare URLs containing a percent sign now autolink (`http://a.com/x%20y`), matching GFM; previously the percent ended the scan and the URL stayed literal text.
- Adjacent AST text fragments are now coalesced into one `text` node, reducing callback-driven array overhead.
- HTML output now seeds a ~1.25× smart_str reserve (still capped at 1 MiB) so ordinary documents reallocate less; sparse input no longer reserves output it never produces, while dense input past the cap grows by doubling and can peak above an exact reserve.
- Documented that AST/XML link URLs are entity-decoded (not source-byte-verbatim), that AST flattens `footnote_section` while XML keeps it, and that AST/XML depth caps count different nesting units.
- `toInlineHtml()` multiline normalization bulk-appends content runs instead of appending one byte at a time.
- Heading-anchor side buffers are released as soon as each heading is flushed into the main output.

### Fixed

- Fixed SmartyPants quote context after decoded entities.
- Avoided repeated 100,000-suffix scans after heading ID exhaustion and reduced scratch memory for large headings.
- Reduced peak HTML, XML, and AST memory for sparse input and large code or HTML literals.
- `toInlineHtml()` now preserves line-leading inline delimiters and literal U+200B while using less memory on multiline input; its per-line sentinel no longer surfaces inside code or LaTeX spans that cross a line break.
- HTML, XML, and AST output now replace embedded NUL exactly once; XML also preserves attribute whitespace and replaces forbidden XML 1.0 scalars.
- AST and XML code blocks retain full info strings, and deeply nested XML bounds indentation without truncating structure.
- Zend memory-limit bailouts now release md4c's libc allocations before the request aborts.
- Fenced code info that entity-decodes to a `language-` prefix no longer gets a second `language-` prepended (`language&#45;php` → `class="language-php"`).
- `nofollowLinks` fragment exception now trims the same leading C0/space bytes as the URL scheme filter.
- HTML footnote/ol open tags append via `strlen` of the snprintf buffer (same truncation-safe pattern as headings).
- Body HTML hex-entity nibble decoding now matches the shared util alphabet (`0-9A-Fa-f` only).

### For contributors

- Refreshed vendored md4c to master `10c0158`, dropping the local code-span line-break patch now that upstream carries it and picking up a bounds guard on the table-alignment dash scan.
- Made CI reject crashed, unloadable, and all-skipped PHPT runs.
- Made benchmark and release helpers reject invalid or inconsistent inputs with a nonzero status.
- Added the PHP 8.2 floor and warnings-as-errors development mode to Windows builds.
- Unix release jobs now verify the requested tag and load-test each packaged binary before upload.
- CI now rejects warned, incomplete, or unexpectedly skipped PHPT runs, and PIE smoke failures no longer fall back to manual builds.

## [0.4.3] - 2026-07-09

### Changed

- `Options::permissive()` no longer sets the inert `liberalHtmlTag` flag;
  the preset is now just `unsafe: true` plus `tagfilter: false`.

### Fixed

- `toInlineHtml()` no longer leaks block-level tables or swallows link
  reference definitions on continuation lines.
- Raw HTML in image alt text is now attribute-escaped under `unsafe: true`,
  closing an `alt="..."` breakout.
- Heading-anchor slugs now treat soft and hard line breaks as word
  separators.
- The AST and XML renderers fail closed on a broken md4c enter/leave
  callback contract instead of emitting a partial tree.
- Raw entity and attribute decoding is shared across the HTML, XML, and
  AST paths, unifying URL filtering on one decoder.
- Docs corrected to match the md4c backend: URL scheme filtering,
  `validateUtf8: false`, spoiler syntax, footnotes, PHP version, license.

## [0.4.2] - 2026-07-03

### Changed

- `toAst` and `toInlineHtml` are faster on multi-line input (interned AST metadata strings; per-line zero-width-space insertion is skipped when no line can open a block). Output is byte-identical.

### Fixed

- The CommonMark XML serializer (`toXml`) no longer overflows a fixed 64-byte stack buffer on an ordered list with a wide start number; the `<list>`, `<heading>`, and footnote tags now stream directly instead of through a truncating `snprintf`. Wide starts previously truncated the tag, embedded a NUL byte, and (at the widest md4c parses) read past the buffer into stack memory.
- SmartyPants (`smart` option) now opens a quote at the start of a block instead of inheriting the previous block's trailing character.
- SmartyPants (`smart` option) now treats a trailing multibyte Unicode space (U+00A0 and friends) before a quote as a space, so the quote opens.
- The UTF-8 validation pre-pass sizes its sanitized buffer to the exact output instead of up to 3x the input length.
- A hard line break inside image alt text now renders as a space instead of literal `<br />` markup in the attribute value.
- SmartyPants (`smart` option) now opens a quote at the start of a line under `toInlineHtml`; the zero-width-space line sentinel is treated as a space rather than flipping the quote to a closing one.
- `toInlineHtml` now renders a tilde-fenced code block (`~~~`) on a continuation line as literal text instead of leaking a block-level `<pre><code>`; backtick fences were already suppressed.

## [0.4.1] - 2026-06-17

### Added

- New `admonitions` option enables md4c GitHub-style alert blocks (`> [!NOTE]` through `> [!CAUTION]`); off by default.

### Fixed

- Strip a leading UTF-8 BOM before parsing; it was leaking into output verbatim and displacing the first line, so `# Heading` rendered as text instead of `<h1>`.

## [0.4.0] - 2026-06-17

### Changed

- Parsing backend swapped from cmark-gfm to md4c, a single-file
  streaming CommonMark + GFM parser compiled into the extension. The
  public API (`MdParser\Parser`, `MdParser\Options`, and the four
  render methods) is unchanged. md4c brings native CommonMark 0.31
  conformance and removes the cmark-gfm dependency.
- `headingAnchors` and `nofollowLinks` are now applied in-stream as the
  renderer emits each node instead of in a string pass over the
  finished HTML. They act only on Markdown-derived headings and links;
  raw HTML passed through under `unsafe: true` is emitted verbatim and
  never rewritten. This resolves the 0.3.0 byte-collision limitation: a
  raw `<h1>x</h1>` and a later Markdown `# x` no longer fight over the
  `id`.
- Source positions are gone (md4c exposes none). `sourcepos` and a few
  former cmark renderer options (`githubPreLang`, `liberalHtmlTag`,
  `strikethroughDoubleTilde`, `tablePreferStyleAttributes`,
  `fullInfoString`) are accepted for API compatibility but inert.

### Added

- md4c dialect options, each opt-in and default off: `latexMath`
  (`$inline$` and `$$block$$`), `wikiLinks` (`[[target]]`), `spoilers`
  (`||text||`), `underline`, `highlight` (`==text==`), `superscript`
  (`^text^`), and `subscript` (`~text~`). Each surfaces in `toXml()` and
  `toAst()` as its own node type.
- Parser-behavior toggles mapping to md4c flags: `noIndentedCodeBlocks`,
  `permissiveAtxHeadings`, and `collapseWhitespace`.
- PHP 8.2 support (lowered the minimum from 8.3).

### Fixed

- Code spans whose interior line ends in whitespace now render the
  correct number of spaces, bringing CommonMark 0.31 conformance to a
  clean 652/652. Carried as a local md4c patch (`vendor/VENDOR.md`),
  submitted upstream as mity/md4c#378.
- `toXml()` and `toAst()` cap nesting depth at `MDPARSER_MAX_AST_DEPTH`
  (1000), so a tiny deeply-nested input can no longer amplify into
  multi-megabyte output or exhaust memory; `toHtml()` is linear and
  uncapped.
- `toXml()` and `toAst()` entity-decode attribute bytes (link/image
  URLs, titles) instead of leaking `&amp;`-encoded text, and `toXml()`
  replaces XML-1.0-illegal control characters with U+FFFD so the output
  stays well-formed.
- `headingAnchors` slugs now include entity-decoded heading text, so
  `# &copy;` and `# Caf&eacute;` produce the expected slug.

### Performance

- The HTML, XML, and AST renderers consume md4c's callbacks directly,
  with a precomputed HTML-escape map, an ASCII fast path in UTF-8
  validation, pre-sized output buffers, a scratch-free decode path for
  plain attribute URLs (HTML and XML), and a single-line fast path for
  `toInlineHtml()`.
- `toAst()` interns the recurring node keys (`type`, `children`) and the
  node-type values once at module init instead of allocating and hashing
  them per node (about 15% faster on a 200 KB document on a clean
  optimized build).

## [0.3.0] - 2026-05-06

### Added

- `MdParser\Options::headingAnchors`: when true, every rendered
  `<hN>` gets an `id` attribute holding a GitHub-style slug of the
  heading's text. Slugs lowercase ASCII, replace whitespace runs with
  a single `-`, drop other ASCII punctuation, preserve UTF-8
  multibyte bytes, and dedupe collisions with `-1`, `-2`, ...
  Headings whose text slugifies to nothing (pure punctuation) emit
  `<hN>` with no id rather than `id=""`. Coexists with `sourcepos`:
  the `id` lands before `data-sourcepos`.
- `MdParser\Options::nofollowLinks`: when true, every emitted
  `<a href="...">` gets `rel="nofollow noopener noreferrer"` injected
  for inline links, reference links, and autolinks. Applies to
  `toHtml()` and `toInlineHtml()`. Anchors inside fenced or inline
  code are left untouched because cmark escapes them before reaching
  the postprocess step. In-document fragment anchors (`href="#..."`,
  i.e. footnote references and backrefs) are skipped.
  Raw `<script>` / `<style>` regions under `unsafe: true` are emitted
  verbatim so anchor-shaped substrings inside JavaScript or CSS are
  not corrupted.
- Linux and macOS prebuilt binaries are now attached to every
  GitHub release (x86_64 + arm64 glibc Linux, x86_64 + arm64
  macOS, PHP 8.4 and 8.5, NTS). PIE picks the matching `.so` first
  and only falls back to a source build for combinations not
  covered by an asset (e.g. PHP 8.3, Alpine/musl, ZTS).
  `composer.json` declares
  `download-url-method: ["pre-packaged-binary", "composer-default"]`
  to opt into the prebuilt path.

Both new HTML-postprocess flags default to `false` and don't affect
XML or AST output. The static `Parser::html()` / `Parser::xml()`
shortcuts use the module defaults and apply neither transform.

Heading anchors are placed by rendering each AST heading standalone
and locating its exact bytes in the document HTML. Under
`unsafe: true`, raw HTML headings normally don't consume slugs meant
for Markdown headings. Known limitation: if a raw HTML heading's bytes
match a later Markdown heading (`<h1>same</h1>` then `# same`), the raw
heading takes the `id` and the Markdown heading gets none. Fixing this
needs renderer-level heading ids; until then, `unsafe: true` callers
shouldn't rely on heading-id stability when raw and Markdown headings
can collide. Pinned in `tests/030_anchor_unsafe_collision.phpt`.

### Changed

- `Parser` now caches one cmark_parser per instance and reuses it
  across `toHtml` / `toXml` / `toAst` / `toInlineHtml` calls.
  `cmark_parser_finish` resets it on every successful render, so no
  state carries over from prior input; after an unclean render the
  parser is rebuilt. Pinned in `tests/033_parser_reuse_isolation.phpt`.
- cmark allocations now route through a Zend MM-backed `cmark_mem`
  (`ecalloc` / `erealloc` / `efree`), so `memory_limit` and
  `memory_get_usage()` see them and Zend MM frees them on bailout.
  Out-of-memory now raises PHP's `Allowed memory size exhausted` fatal
  instead of cmark's `abort()`.
- AST node-type, list type / delim, and table alignment values are
  now permanent interned strings created at MINIT, saving ~1 emalloc
  + memcpy per AST node on `toAst()`.
- AST key strings (`type`, `children`, `literal`, `level`, ...) are
  now permanent interned strings created at MINIT via
  `zend_string_init_interned(..., true)` instead of persistent strings
  lazily built on the first `toAst()` call. Interned strings skip
  refcount mutation in `zend_hash_add_new`, so concurrent `toAst()`
  calls on ZTS builds no longer race a non-atomic shared refcount.
- AST node array preallocation raised from `array_init_size(out, 8)`
  to 16. A list with `sourcepos: true` carries 10 keys, so 8 forced a
  rehash on every list; 16 covers every supported node shape.
- HTML postprocess failure messages now distinguish the AST depth cap
  (heading text exceeded `MDPARSER_MAX_AST_DEPTH`) from cmark
  iterator/render allocation failure instead of reporting all three as
  "HTML postprocess allocation failure".

### Fixed

- `Parser::toInlineHtml()` no longer lets block-level markers (`#`,
  `-`, `>`, `1.`, four-space indent, fenced/HTML blocks, thematic
  breaks) fire on lines after the first. The source rewrite
  normalizes `\r\n` and lone `\r` to `\n`, collapses newline runs,
  trims leading/trailing newlines, and prefixes every line with a
  U+200B sentinel that the output stripper removes.
- PHP 8.6 compatibility: replaced `XtOffsetOf` with `offsetof`, since
  php-src master removed the macro from `zend_portability.h`.
- `config.w32` now lists `mdparser_html_postprocess.c` so Windows
  builds link successfully.

### Security

- HTML postprocess no longer splices into raw-HTML attribute values,
  comments, CDATA, or escapable-raw-text element bodies. Under
  `unsafe: true, tagfilter: false, nofollowLinks: true`, an
  `<a href="` inside `<title>`, `<textarea>`, `<iframe>`, `<noscript>`,
  `<xmp>`, `<noembed>`, `<noframes>`, `<plaintext>`, `<!-- … -->`,
  `<![CDATA[ … ]]>`, or a quoted attribute value like
  `<div title='<a href="x">…'>` was rewritten, producing malformed HTML
  that could splice attributes onto the surrounding tag. The skip-region
  scanner now covers all of these, and apply_transforms walks
  tag-by-tag with quoted-attribute awareness. The heading-anchor search
  in `resolve_heading_offsets` uses the same logic, closing the comment
  / CDATA / textarea slug-hijack vector. Pinned in
  `tests/031_postprocess_attribute_safety.phpt`.
- Heading slugs now percent-encode invalid UTF-8 byte sequences
  (lone continuation bytes, overlong leads, truncated multi-byte
  sequences) instead of letting them land verbatim in `id="…"`.
  Valid UTF-8 multi-byte sequences (e.g. `日本語`) still pass
  through. Reachable when callers turn off `validateUtf8`.
- `Parser::toInlineHtml()` no longer pre-allocates `4 * src_len + 3`
  for its scratch buffer, which made newline-heavy input fatal under a
  tight `memory_limit` (40 MB of `\n` allocated ~168 MB for an empty
  result). The buffer now grows on demand via `smart_str`. Pinned in
  `tests/037_toinlinehtml_memory_limit.phpt`.
- `Options` objects built via
  `ReflectionClass::newInstanceWithoutConstructor()` are now
  rejected at `Parser::__construct()` with
  `MdParser\Exception`. Uninitialized typed properties read as
  `IS_NULL`, so the parser cached an all-false mask (including
  `validateUtf8: false` and `tagfilter: false`). The constructor now
  bails before publishing `$options`. Regression test in
  `tests/029_regressions.phpt`.
- Linux build compiled with `-fvisibility=hidden`. Vendored cmark
  symbols (`cmark_parser_new`, `cmark_release_plugins`,
  `CMARK_DEFAULT_MEM_ALLOCATOR`, ...) and wrapper internals no
  longer appear in `mdparser.so`'s dynamic symbol table; only
  `get_module` is exported, avoiding collisions with other extensions
  that vendor or link cmark.
- Windows release workflow pins `php/php-windows-builder/*`
  references to a commit SHA instead of the mutable `@v1` tag, so a
  moved or compromised tag can't push DLLs into a release.

## [0.2.0] - 2026-04-11

### Added

- `MdParser\Parser::html(string)`, `MdParser\Parser::xml(string)`,
  and `MdParser\Parser::ast(string)`: static one-shot shortcuts that
  parse with the default Options, like michelf/php-markdown's
  `Markdown::defaultTransform()`.
- `MdParser\Parser::toInlineHtml(string)`: renders inline-only HTML
  with no `<p>` wrapper, for short strings such as chat messages, table
  cells, and display names. Block-level markers (`#`, `-`, `>`, `1.`,
  4-space indents) become literal text. Matches Parsedown's `line()`
  and cebe/markdown's `parseParagraph()`. A leading U+200B forces
  paragraph context and is stripped with the `<p>` wrapper.
- `MdParser\Options::strict()`, `MdParser\Options::github()`, and
  `MdParser\Options::permissive()`: static presets. `strict()` is the
  defaults plus `autolink: false`, so bare URLs stay plain text.
  `github()` adds `footnotes: true` to match github.com. `permissive()`
  sets `unsafe: true` and `tagfilter: false` for trusted input.
- Hard cap on input size (`MDPARSER_MAX_INPUT_SIZE`, 256 MB). Inputs
  past the cap throw `MdParser\Exception` at the wrapper boundary
  instead of reaching cmark's `int32_t` `bufsize_t` limit.
- Hard cap on `toAst()` recursion depth (`MDPARSER_MAX_AST_DEPTH`,
  1000 levels). Deeply nested markdown like `> ` × 50000 now throws
  `MdParser\Exception` instead of overflowing the C stack.
  `toHtml()` and `toXml()` were already safe because cmark's renderers
  are iterative.

### Changed

- Per-parse extension attachment is now a bitmask loop over
  `cmark_syntax_extension*` pointers resolved once at MINIT, replacing
  five `cmark_find_syntax_extension()` lookups per call. MINIT now
  fails if a default-on extension (notably `tagfilter`) is missing from
  the cmark-gfm registry.
- `Options` default masks are cached in `mdparser_default_cmark_options`
  / `mdparser_default_extension_mask` at MINIT. `mdparser_options_default_masks`
  collapses to a two-word copy.
- `Parser` and `Options` are both marked `ZEND_ACC_NOT_SERIALIZABLE`.
  Default serialization dropped `Parser`'s cached `cmark_options` /
  `extension_mask` ints, so `unserialize($parser)` ran on defaults
  regardless of the original `Options`. `Options` is blocked for
  consistency. `MdParser\Exception` stays serializable so monolog,
  queue workers, and PHPUnit can log it. Clone was already blocked on
  `Parser`.
- AST walker: `array_init_size(out, 8)` per node, interned key strings
  inserted with precomputed hashes via `zend_hash_add_new`, and
  extension detection via `cmark_node_get_syntax_extension()` instead
  of a 6-way `strcmp` chain. The key strings are built on the first
  `toAst()` call, so `toHtml()`/`toXml()`-only users skip the setup.
- `toHtml`/`toXml`/`toAst` migrated from `Z_PARAM_STRING` to
  `Z_PARAM_STR`, matching modern `ext/standard` / `ext/dom` usage.
- Exception messages from `cmark_parser_finish` and renderer null
  returns now include the source length.

### Fixed

- `toAst()` no longer emits `'type' => '<unknown>'` for footnotes.
  cmark-gfm's `cmark_node_get_type_string()` doesn't cover
  `CMARK_NODE_FOOTNOTE_REFERENCE` or `CMARK_NODE_FOOTNOTE_DEFINITION`;
  the walker now maps them to `"footnote_reference"` /
  `"footnote_definition"`, puts the label in `literal`, and recurses
  into the definition body.

### Security

- Parser and Options serialization blocked (see above), preventing
  silent state loss across a serialize/unserialize round trip.
- `toAst()` on deeply-nested markdown now throws cleanly at
  `MDPARSER_MAX_AST_DEPTH` instead of segfaulting via C stack
  exhaustion. Regression test in `tests/022_limits.phpt`.

## [0.1.1] - 2026-04-11

Release infrastructure only; no extension behavior change from 0.1.0.
The 0.1.0 tag predates `composer.json`, so Packagist skipped it and PIE
couldn't resolve `iliaal/mdparser` without `:@dev`. 0.1.1 is the first
tag with `composer.json`.

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
- `scripts/pie-smoke.sh`: end-to-end build, install, and smoke test in
  a clean `php:8.4-cli` Docker container, including the `bison` and
  `libtool-bin` packages PIE needs. Verifies the install path in
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

- `MdParser\Parser`: final class holding a precomputed cmark options
  bitmask and extension mask, with three entry points:
  - `toHtml(string $source): string`
  - `toXml(string $source): string`
  - `toAst(string $source): array`
- `MdParser\Options`: final readonly class, 17 bool fields:
  - Core cmark options: `sourcepos`, `hardbreaks`, `nobreaks`, `smart`,
    `unsafe`, `validateUtf8`, `githubPreLang`, `liberalHtmlTag`,
    `footnotes`, `strikethroughDoubleTilde`, `tablePreferStyleAttributes`,
    `fullInfoString`
  - GFM extension toggles: `tables`, `strikethrough`, `tasklist`,
    `autolink`, `tagfilter`
  - Safe defaults: `unsafe = false`, `validateUtf8 = true`,
    `tagfilter = true`, all GFM extensions on.
- `MdParser\Exception`: final, extends `\RuntimeException`.
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
  cebe/markdown, and michelf/php-markdown; mdparser measures ~15-30x
  faster across 200 B / 1.8 KB / 200 KB corpora.

### Known limitations

- No `toCommonmark()` round-trip renderer yet.
- No streaming parse API. Source is parsed as a single buffer.
- No custom userland render hooks. Use `toAst()` if you need to walk
  the tree and emit custom output.

[Unreleased]: https://github.com/iliaal/mdparser/compare/0.6.1...HEAD
[0.6.1]: https://github.com/iliaal/mdparser/releases/tag/0.6.1
[0.6.0]: https://github.com/iliaal/mdparser/releases/tag/0.6.0
[0.5.0]: https://github.com/iliaal/mdparser/releases/tag/0.5.0
[0.4.3]: https://github.com/iliaal/mdparser/releases/tag/0.4.3
[0.4.2]: https://github.com/iliaal/mdparser/releases/tag/0.4.2
[0.4.1]: https://github.com/iliaal/mdparser/releases/tag/0.4.1
[0.4.0]: https://github.com/iliaal/mdparser/releases/tag/0.4.0
[0.3.0]: https://github.com/iliaal/mdparser/releases/tag/0.3.0
[0.2.0]: https://github.com/iliaal/mdparser/releases/tag/0.2.0
[0.1.0]: https://github.com/iliaal/mdparser/releases/tag/0.1.0
