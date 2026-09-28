# Vendored parser sources

mdparser compiles [md4c](https://github.com/mity/md4c) into the
extension's shared object, so everything needed to parse CommonMark +
GFM ships inside `mdparser.so` with no external runtime dependency.

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
that a longjmp bypasses. `md4c-html.c` is vendored for refresh parity but
not compiled; mdparser uses its own callback renderer for `toHtml()`
(safe-mode URL filtering, heading anchors, nofollow, SmartyPants).
`entity.c` provides named-entity decoding for the mdparser renderers.

## Pins

| Component | Version | Notes |
|---|---|---|
| mity/md4c | `0.5.3+git61f5ce7` | The C source compiled into `mdparser.so`. Tracked in `MDPARSER_MD4C_VERSION` (`php_mdparser.h`); reported by `php --ri mdparser`. |
| CommonMark spec fixture | 0.31 `spec.txt` | Shipped at `tests/fixtures/commonmark-spec.txt`; `tests/005_commonmark_spec.phpt` pins md4c's conformance against it. |

md4c targets CommonMark 0.31 natively, so the parser pin and the spec
fixture are on the same spec version.

If you refresh the fixture (drop in a newer `spec.txt`), update the row
above, the baseline in `tests/005_commonmark_spec.phpt`, and the version
statement in `docs/spec-coverage.md`.

## Local modifications

Seven behavior patches, two performance patches, and one embedding hook are
carried in `md4c/md4c.c`. Every change site is marked with an
`mdparser local patch` comment.

### Out-of-memory error paths

Five patches fix upstream defects on md4c's allocation-failure paths. They
matter more here than in standalone md4c: `mdparser_md4c_vendor.c` routes every
md4c allocation through an intrusive registry whose header is unlinked before
`free()`, so a double free writes through an already-freed header rather than
just tripping the allocator. Reproduced with an ASAN build of `md4c.c` plus a
fault injector that fails the *n*-th allocation, swept over every injection
point of a small Markdown corpus; all three are hit by plain CommonMark input.

`md_free_attribute` keyed its frees on `build->substr_alloc > 0`, which is
wrong in both directions. When `md_build_attr_append_substr` failed a growth realloc
after the array had already grown, `md_build_attribute` freed the three buffers
on its own `abort` path but left `substr_alloc` non-zero, so the caller's
`abort` label (in `md_enter_leave_span_a`, `md_enter_leave_span_wikilink`,
`md_enter_leave_span_footnote_ref`, `md_process_leaf_block`, and
`md_process_footnote_def`) freed them a second time. When the *first* growth
realloc failed, `substr_alloc` was still 0 and the already-allocated text and
type buffers leaked. The patch keys on `build->substr_types !=
build->trivial_types` instead, the "this build owns its storage"
condition, and clears the pointers so the function is idempotent.

`md_is_link_reference_definition` freed `def->entry.label` and `def->title` at
its `abort` label. A non-NULL `def` is already committed to
`ctx->ref_def_hashtable`, which owns both and frees them in `md_free_ref_defs`,
so the label was freed twice. Reaching it needs a definition whose label *and*
title are both multiline: `label_needs_free` is only set on the multiline-label
branch, and the title merge is the allocation that has to fail. The
`def == NULL` path frees its local label itself and is unaffected. The patch
drops both frees.

The same function left `ret` at 0 on both `md_add_label_def` out-of-memory
branches. Its own contract is "returns -1 in case of an error (out of memory)",
so an allocation failure was reported to `md_consume_link_reference_definitions`
as "this is not a reference definition" and the parse silently continued with
the definition dropped. The patch sets `ret = -1` on both branches.

Two callers dropped `md_end_current_block()`'s return value: `md_process_doc`
after the line loop, and `md_enter_child_containers` before it records
`c->block_byte_off` for loose-list revisiting. In the first, an allocation
failure while consuming the document's last reference definition was swallowed
and phase 2 then ran over a half-committed definition; in the second, the
recorded offset was taken against a block that had not finished ending. Both
patches wrap the call in `MD_CHECK`.

All five are still present in md4c master as of the `61f5ce7` pin, so a
refresh does not drop them. Re-apply all five and re-check upstream first.
They are proposed upstream as separate pull requests.

### Table column-count guard

`md_is_table_underline` counts GFM table columns before the count is copied
into `MD_BLOCK::data`, a 16-bit bit-field. The patch rejects underlines with
more than `UINT16_MAX` columns before that narrowing can wrap the count to
zero or a small value. Tables at the 16-bit boundary remain supported; wider
underlines are treated as ordinary text instead of rendering an empty or
mis-shaped table skeleton. Re-apply this guard on refresh and run
`tests/085_table_column_count.phpt` at the 65,535/65,536/65,537 boundaries.

### Behavior

The NUL patch is in `md_text_with_null_replacement`. Stock md4c emits the
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

### Performance

Two patches replace per-byte character tests on md4c's scan paths. A
callback-trace diff against stock md4c shows neither changes the event
stream: 108 inputs (bench corpora, the CommonMark spec, the parity fixtures,
CR-only, CRLF, and mixed CR/LF variants of the corpora and spec, and
NUL/control-byte inputs) under three flag sets are byte-identical. That diff
checks event identity only, not running time; the CR-only complexity case
below is covered by `tests/088_cr_only_line_endings.phpt`.

The end-of-line scan in `md_analyze_line` tested every byte against `'\n'`
and `'\r'`, about 20% of md4c's instructions on the spec corpus (x86
callgrind). The patch finds the first `'\n'` with `memchr` and then searches
for `'\r'` only before that hit, so a lone `'\r'` or a `"\r\n"` pair still
ends the line at its first byte, exactly as `ISNEWLINE` did. Both searches
are bounded by `ctx->size`. The `'\n'` result is cached in `MD_CTX`
(`newline_cache_*`, zeroed by `md_parse`) together with the offset it was
searched from, and reused while the scan offset stays inside that range.
Without the cache, a CR-only document searches from every line to the end of
the input, which is quadratic: 250,000 `"a\r"` lines took 2.2 s in `toHtml()`
against 0.02 s stock. `md_process_doc` is the only caller and its offset only
grows, but the range check keeps the cache correct without relying on that.
The stock loop is kept under `MD4C_USE_UTF16`, where `CHAR` is wider than a
byte.

The link-destination scan in `md_is_link_destination_B` stopped on
`ISWHITESPACE(off) || ISCNTRL(off)`, six comparisons per byte. The union of
those two sets is exactly bytes 0..32 plus 127 (space is the only whitespace
member above 31), so the patch tests
`(unsigned) CH(off) <= 32 || CH(off) == 127`. An exhaustive check over all
256 byte values, under both signed and unsigned `char`, agrees with the
stock macros.

Measured with `toHtml()` on the gir bench host (aarch64, release PHP 8.4,
`-O2`): median paired delta over 20 interleaved rounds against an unpatched
build, with an A/A control within 0.1%. The end-of-line patch alone is
-3.8% on `links.md`, -2.4% on `large.md`, and -2.6% on `medium.md`; the
link-destination patch alone is -1.5% on `links.md` (its md4c instruction
count there drops 4.8%); together -5.3%, -2.7%, and -1.4%. Corpora that
parse almost no link destinations still move by up to 1% between builds,
which is code-layout noise rather than either patch. CR-only input with the
cache runs at stock speed (1,000,000 lines: 0.073 s against 0.070 s stock).
Both patches are optional on refresh: drop either one if upstream rewrites
the loop, and re-measure before re-applying it to changed code.

No other vendored files are modified; md4c.c is otherwise self-contained C
(no CMake, no re2c, no generated headers to maintain).

## Refresh

md4c is a small, self-contained library, so a refresh is a drop-in:

1. Copy `md4c.c`, `md4c.h`, `md4c-html.c`, `md4c-html.h`, `entity.c`,
   `entity.h`, and `LICENSE.md` from the new md4c release into
   `vendor/md4c/`.
2. Update `MDPARSER_MD4C_VERSION` in `php_mdparser.h`.
3. Rebuild and run `make test`.
4. Re-apply or drop each behavior patch, performance patch, and the
   embedding hook (see Local modifications). If the new release already
   carries an upstream fix, the copy in step 1 removes that patch. Confirm
   005 still passes 652/652, run `tests/088_cr_only_line_endings.phpt` for
   the end-of-line scan, run `tests/070_nul_replacement.phpt` for the NUL
   behavior, and run `tests/oom/run.sh` for the out-of-memory error paths.
   That sweep is the only gate on most of them;
   `mdparser.parse_memory_limit` reaches only the paths near its own
   boundary.
5. If `tests/005_commonmark_spec.phpt` moves, explain the delta in the
   commit message (a new md4c release may change conformance in either
   direction). Re-baseline the pinned list only after confirming the
   change is an intentional upstream behavior shift.

To surface a new md4c block or span type, add the case to all three
renderers (`mdparser_md4c_html.c`, `mdparser_md4c_xml.c`,
`mdparser_md4c_ast.c`) and a matching `Options` flag; the parser flags
live in `MD_FLAG_*` (`md4c.h`).

## History

mdparser embedded cmark-gfm before 0.4.0. The rebase postmortem that
motivated the switch to md4c is at `~/ai/wiki/debugging/cmark-gfm-rebase.md`.
