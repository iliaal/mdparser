# mdparser

Native C CommonMark + GitHub Flavored Markdown parser for PHP, packaged as
a PECL/PIE extension. Wraps a vendored copy of `cmark-gfm` in a thin Zend
binding; no external runtime dependencies.

## Status

Under development. Not yet released.

## Build

```bash
~/php-install-84/bin/phpize
./configure --with-php-config=$HOME/php-install-84/bin/php-config --enable-mdparser
make -j
make test
```

Dev builds (strict warnings): `./configure ... --enable-mdparser-dev`.

Runtime-check:

```bash
~/php-install-84/bin/php -d extension=./modules/mdparser.so \
  -r 'echo (new MdParser\Parser)->toHtml("# Hi");'
```

## Public API

All classes live in the `MdParser\` namespace.

- `MdParser\Options` — final readonly, 17 bool fields covering cmark
  core options plus per-extension toggles. Named-argument constructor.
  Defaults: `unsafe = false`, `validateUtf8 = true`, `tagfilter = true`,
  GFM extensions (tables, strikethrough, tasklist, autolink) all on.
- `MdParser\Parser` — final, holds one precomputed cmark-options bitmask
  plus extension mask. `__construct(?Options)`, `toHtml(string)`,
  `toXml(string)`. `toAst(string)` arrives in Phase 3.
- `MdParser\Exception` — final, extends `\RuntimeException`. Narrow
  throw sites: allocation failure and NULL return from cmark internals.

The `Parser` object caches the translated bitmask at construction time,
so parse-time is pure cmark work with no per-call option re-walking.

## Vendored parser

`vendor/cmark/` is **cmark-gfm 0.29.0.gfm.13** (commit `587a12b`) plus
four cherry-picked commits from cmark upstream that close the 0.29 →
0.31 spec gap. Compiles directly into the extension .so.
main.c (the CLI driver) is stripped so we don't end up with a
`main` symbol in the extension.

The cherry-picks are documented in `vendor/VENDOR.md` "Local
modifications". They touch `inlines.c` (emphasis rule 13 + Unicode
Symbols flanking), `html.c` (reverts cmark-gfm's nested-strong
flattening), `utf8.c`+`utf8.h` (adds `is_punctuation_or_symbol`), and
`houdini_html_u.c` (entity max-digit limit). Every change site has
a comment identifying the upstream commit so a future refresh can
re-apply them.

Three files in `vendor/cmark/src/` are hand-written replacements for CMake
outputs since we don't run cmake at build time:

- `config.h` — feature macros (`HAVE_STDBOOL_H`, `HAVE___ATTRIBUTE__`)
- `cmark-gfm_version.h` — cmark-gfm version integer
- `cmark-gfm_export.h` — empty export macros (static build, no visibility)

## Vendor refresh: cherry-pick only

Full postmortem: `vendor/VENDOR.md` and `~/ai/wiki/debugging/cmark-gfm-rebase.md`.

**Short version:** cmark upstream and cmark-gfm diverged on the internal
AST data model (link/code/custom/list/heading fields are chunks vs heap
strings). Bulk rebase via `patch` or `git merge-file` produces
silently-broken files that reference removed struct fields. Don't
attempt bulk rebases.

But **surgical cherry-picks of individual cmark upstream commits work
fine** when the commit doesn't cross the AST-shape boundary. That's how
we closed the 0.29 → 0.31 spec gap for 0.1.0 — four targeted commits
documented in `vendor/VENDOR.md`. When a future cmark bug fix matters,
do the same: read the cmark diff for that commit, apply the edit by
hand, add an entry to the "Local modifications" table.

## Testing

Tests live under `tests/` as flat `.phpt` files, matching the `php_excel`
convention. Current suite:

- `000_smoke.phpt` — module loads, round-trip `# Hi`, GFM features
- `001_options.phpt` — Options readonly class, defaults, named args
- `002_option_effects.phpt` — option-by-option rendering verification
- `003_xml.phpt` — `toXml()` output
- `004_exception.phpt` — MdParser\\Exception hierarchy
- `005_commonmark_spec.phpt` — CommonMark 0.29 spec conformance
  (630/649 pinned, failures enumerated by line)

Any change that moves the `005_commonmark_spec.phpt` baseline must be
explained in the commit message. The pinned failure list is the
cmark-gfm-specific baseline; a surprise improvement is as noteworthy as
a regression because it means something merged upstream and rippled
through.

## Code style

- Wrapper `.c` files get the same header block as `php_excel.c`: PHP
  License 3.01 + author line. Never add `Co-Authored-By`.
- Generated `mdparser_arginfo.h` comes from `mdparser.stub.php` via
  `php ~/php-src/build/gen_stub.php mdparser.stub.php`. Never hand-edit
  the arginfo header. The stub uses explicit property declarations (not
  promoted properties); gen_stub doesn't accept promoted properties.
- `MDPARSER_CFLAGS` includes `-DCMARK_GFM_STATIC_DEFINE` and
  `-DCMARK_GFM_EXTENSIONS_STATIC_DEFINE` — required for the embedded
  build so cmark-gfm headers emit empty visibility attributes.
- Class entries are owned by one translation unit. Do not redeclare
  `mdparser_parser_ce` outside `mdparser_parser.c`; the header declares
  it `extern` and that's the only reference anyone else gets.

## Git discipline

- Never add `Co-Authored-By` or AI attribution lines to commit messages.
- After every commit, `git log -1` to confirm no `Co-Authored-By` slipped in.
- Audit commit message claims against `git show --stat HEAD` before
  finalizing. If the message says "updated X and Y", the diff had
  better show X and Y.

## Repository

Local-only for now. When published, intended for `iliaal/mdparser`
(private until first release, then public on PECL and PIE).
