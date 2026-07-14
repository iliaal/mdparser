# Vendored parser sources

mdparser embeds [md4c](https://github.com/mity/md4c) built directly into
the extension's shared object. There is no external runtime dependency:
users install the PECL/PIE package and everything needed to parse
CommonMark + GFM ships inside `mdparser.so`.

md4c is a single-pass streaming parser. It does not build a document
tree; it emits block/span/text events through callbacks. mdparser's
HTML, XML, and AST output paths are stateless consumers of those events
(`mdparser_md4c_html.c`, `mdparser_md4c_xml.c`, `mdparser_md4c_ast.c`).

## Layout

```
vendor/
├── VENDOR.md      (this file)
└── md4c/          md4c source, built by config.m4
    ├── md4c.c / md4c.h          the parser
    ├── md4c-html.c / md4c-html.h  md4c's own HTML renderer (see note)
    ├── entity.c / entity.h      HTML named-entity table
    └── LICENSE.md               MIT
```

`config.m4` compiles `entity.c` directly and compiles `md4c.c` through
`mdparser_md4c_vendor.c`. The wrapper supplies a Zend-bailout guard plus an
intrusive per-parse registry for md4c's libc allocations. The guard lets md4c
run its normal cleanup; the registry catches function-local temporary buffers
that a longjmp necessarily bypasses. `md4c-html.c` is vendored for refresh
parity but is not compiled. mdparser uses its **own**
callback renderer for `toHtml()` (safe-mode URL filtering, heading anchors,
nofollow, SmartyPants). `entity.c` is used for named-entity decoding in our
renderers.

## Pins

| Component | Version | Notes |
|---|---|---|
| mity/md4c | `0.5.3+git755ce49` | The C source compiled into `mdparser.so`. Tracked in `MDPARSER_MD4C_VERSION` (`php_mdparser.h`); reported by `php --ri mdparser`. |
| CommonMark spec fixture | 0.31 `spec.txt` | Shipped at `tests/fixtures/commonmark-spec.txt`; `tests/005_commonmark_spec.phpt` pins md4c's conformance against it. |

md4c targets CommonMark 0.31 natively, so the parser pin and the spec
fixture are on the same spec version. There is no version gap to bridge.

If you refresh the fixture (drop in a newer `spec.txt`), update the row
above, the baseline in `tests/005_commonmark_spec.phpt`, and the version
statement in `docs/spec-coverage.md`.

## Local modifications

Two behavior patches and one embedding hook are carried in `md4c/md4c.c`.

The first is in `md_process_inlines` (code-span line-break handling). Stock
md4c renders an interior code-span line that ends in
whitespace one space short, because it emits the line-break space only
`if(off == line->end)` and the trailing-whitespace loop just above has
already advanced `off` past `line->end`. This fails CommonMark 0.31
examples 335, 337, and 640. The patch emits the space whenever `off`
rests on an interior newline still preceding the closer
(`off < mark->beg && ISNEWLINE(off)`), which fixes those three without
regressing the boundary cases (121, 336). The change site is marked
with an `mdparser local patch` comment.

Submitted upstream to mity/md4c. **Remove this patch on the next vendor
refresh that includes the upstream fix** — when copying in a new md4c
release (see Refresh below), diff `md_process_inlines` against this note
and drop the local change if upstream now carries it.

The second is in `md_text_with_null_replacement`. Stock md4c emits the
`MD_TEXT_NULLCHAR` callback but advances only the local offset, leaving the
input pointer and remaining size on the same NUL. The following callback then
receives that byte a second time. The patch consumes one character from
`str`/`size` after the replacement callback, so every embedded NUL produces
exactly one replacement event. Drop this patch when a refreshed md4c contains
the equivalent pointer/size advance.

The embedding hook is in `md_parse`. When `MD_PARSER_BAILOUT_GUARD` is
defined, the call to `md_process_doc` runs inside the wrapper-provided guard.
A Zend memory-limit bailout can otherwise jump past md4c's cleanup and leak
its libc buffers. Catching at this exact frame keeps the stack-owned `MD_CTX`
valid while the unchanged cleanup frees reference definitions, footnotes,
buffers, marks, block storage, and containers. Standalone md4c builds do not
define the macro and compile the stock path.

No other vendored files are modified; md4c.c is otherwise self-contained C
(no CMake, no re2c, no generated headers to maintain).

## Refresh

md4c is a small, self-contained library, so a refresh is a drop-in:

1. Copy `md4c.c`, `md4c.h`, `md4c-html.c`, `md4c-html.h`, `entity.c`,
   `entity.h`, and `LICENSE.md` from the new md4c release into
   `vendor/md4c/`.
2. Update `MDPARSER_MD4C_VERSION` in `php_mdparser.h`.
3. Rebuild and run `make test`.
4. Re-apply or drop both behavior patches and the embedding hook (see Local
   modifications). If the new
   release already carries an upstream fix, the copy in step 1 removes that
   patch for free. Confirm the code-span behavior still leaves 005 at 652/652
   and run `tests/070_nul_replacement.phpt` for the NUL behavior.
5. If `tests/005_commonmark_spec.phpt` moves, explain the delta in the
   commit message (a new md4c release may change conformance in either
   direction). Re-baseline the pinned list only after confirming the
   change is an intentional upstream behavior shift.

No 3-way merges, no per-file conflict resolution. If md4c ever grows a
new block or span type you want to surface, add the case to all three
renderers (`mdparser_md4c_html.c`, `mdparser_md4c_xml.c`,
`mdparser_md4c_ast.c`) and a matching `Options` flag — the parser flags
live in `MD_FLAG_*` (`md4c.h`).

## History

This extension previously embedded cmark-gfm, which built a heap AST and
walked it to render, and was migrated to md4c (a streaming parser). The
cmark-gfm rebase postmortem that motivated leaving that ecosystem is at
`~/ai/wiki/debugging/cmark-gfm-rebase.md`.
