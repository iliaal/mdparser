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
| mity/md4c | `0.6.0+gitc7ba975` | The C source compiled into `mdparser.so`. Tracked in `MDPARSER_MD4C_VERSION` (`php_mdparser.h`); reported by `php --ri mdparser`. |
| CommonMark spec fixture | 0.31 `spec.txt` | Shipped at `tests/fixtures/commonmark-spec.txt`; `tests/005_commonmark_spec.phpt` pins md4c's conformance against it. |

md4c targets CommonMark 0.31 natively, so the parser pin and the spec
fixture are on the same spec version.

If you refresh the fixture (drop in a newer `spec.txt`), update the row
above, the baseline in `tests/005_commonmark_spec.phpt`, and the version
statement in `docs/spec-coverage.md`.

## Local modifications

Three behavior patches, one performance patch, and one embedding hook are
carried in `md4c/md4c.c`. Every change site is marked with an
`mdparser local patch` or `mdparser local integration hook` comment.

### Table column-count guard

`md_is_table_underline` counts GFM table columns before the count is copied
into `MD_BLOCK::data`, a 16-bit bit-field. The patch rejects underlines with
more than `UINT16_MAX` columns before that narrowing can wrap the count to
zero or a small value. Tables at the 16-bit boundary remain supported; wider
underlines are treated as ordinary text instead of rendering an empty or
mis-shaped table skeleton. Upstream's pipe-handling rework (mity/md4c#419)
changed how body rows split into cells but left `md_is_table_underline` and
its `unsigned` counter as they were, so the guard still applies. Re-apply it
on refresh and run `tests/085_table_column_count.phpt` at the
65,535/65,536/65,537 boundaries.

### Behavior

The NUL patch is in `md_text_with_null_replacement`. Stock md4c emits the
`MD_TEXT_NULLCHAR` callback but advances only the local offset, leaving the
input pointer and remaining size on the same NUL. The following callback then
receives that byte a second time. The patch consumes one character from
`str`/`size` after the replacement callback, so every embedded NUL produces
exactly one replacement event. Still unfixed upstream as of `c7ba975`. Drop
this patch when a refreshed md4c contains the equivalent pointer/size
advance.

The spoiler patch is in `md_analyze_marks`, on the `'|'` case. Upstream
`c933a91` folded `md_analyze_spoiler` into `md_analyze_generic` and dropped
its "only a `||` run is a spoiler mark" length test. Since mity/md4c#419,
`MD_FLAG_WIKILINKS` collects every single `|` as a potential opener and
closer, so with `MD_FLAG_SPOILERS` also set, `md_analyze_generic` pairs a
lone `|` with the next `|` or `||`. The pipes then vanish from the output
(`a | b | c` renders as `a  b  c`) and a spoiler span is entered or left
without its partner (`x | y ||` emits a bare `MD_SPAN_SPOILER` leave), which
breaks `toXml()` and `toAst()` outright. Stock md2html at `c7ba975`
reproduces it; the parent of `c933a91` does not. The patch restores the
length test before the call. The `MD_MARK_RESOLVED` test that went with it
is not needed, because `md_analyze_marks` already skips resolved marks. With
the patch, md4c's own test suite (`test/*.txt`, 987 examples) still passes in
full. Drop the patch once upstream restores the check.
`tests/089_spoiler_wikilink_single_pipe.phpt` covers it.

The embedding hook is in `md_parse`. When `MD_PARSER_BAILOUT_GUARD` is
defined, the call to `md_process_doc` runs inside the wrapper-provided guard.
A Zend memory-limit bailout can otherwise jump past md4c's cleanup and leak
its libc buffers. Catching at this exact frame keeps the stack-owned `MD_CTX`
valid while the unchanged cleanup frees reference definitions, footnotes,
buffers, marks, block storage, and containers. Standalone md4c builds do not
define the macro and compile the stock path.

### Performance

The link-destination scan in `md_is_link_destination_B` stopped on
`ISWHITESPACE(off) || ISCNTRL(off)`, six comparisons per byte. The union of
those two sets is exactly bytes 0..32 plus 127 (space is the only whitespace
member above 31), so the patch tests
`(unsigned) CH(off) <= 32 || CH(off) == 127`. An exhaustive check over all
256 byte values, under both signed and unsigned `char`, agrees with the
stock macros. It is proposed upstream as mity/md4c#443, still open. Measured
alone in 0.6.2 on the gir bench host (aarch64, release PHP 8.4, `-O2`, median
paired delta over 20 interleaved rounds, A/A control within 0.1%): -1.5%
`toHtml()` on `links.md`, whose md4c instruction count drops 4.8%. The patch
is optional on refresh: drop it if upstream rewrites the loop, and
re-measure before re-applying it to changed code.

### Dropped at the `c7ba975` refresh

The end-of-line scan patch in `md_analyze_line` (a cached `memchr` for `'\n'`
plus a bounded `'\r'` search, with `newline_cache_*` fields in `MD_CTX`) is
gone. Upstream `a25ed43` and `11e0b30` (mity/md4c#442) replace the same loop
with two `memchr` scans that keep a `cr_horizon` and an `lf_horizon` in
`MD_CTX` and look at most 2,048 bytes ahead. Each horizon only moves forward,
so CR-only input stays linear; `tests/088_cr_only_line_endings.phpt` passes
on the upstream code.

The five out-of-memory error-path patches are gone as well. Upstream merged
each one, and the merged code matches the local patch line for line apart
from comments:

- `b630227`: `md_free_attribute` keys its frees on `build->substr_types !=
  build->trivial_types` and clears all five fields afterwards, which fixes
  both the double free after a failed growth realloc and the leak when the
  first growth realloc fails.
- `fa0efb8`: the `abort` label of `md_is_link_reference_definition` no longer
  frees the label and title of a `def` that `ctx->ref_def_hashtable` already
  owns.
- `47f8f4c`: both `md_add_label_def` out-of-memory branches set `ret = -1`,
  and `md_process_doc` and `md_enter_child_containers` wrap
  `md_end_current_block()` in `MD_CHECK`.

`tests/oom/run.sh` passes on the refreshed tree over all 16 corpus documents.
`16-refdef-hr-and-pipe-runs.md` covers the failure path of the per-pipe
`cell_begs` buffer that mity/md4c#419 added to `md_process_table_row` (gcov:
3 of its 11 fault points land there). It does not reach the `return -1` after
the `MD_BLOCK_HR` push that mity/md4c#414 added to
`md_consume_link_reference_definitions`, and no document can: the push
follows the removal of at least two `MD_LINE` records (8 bytes each) and adds
one 8-byte `MD_BLOCK`, so `md_push_block_bytes` never has to grow the buffer
there.

No other vendored files are modified; md4c.c is otherwise self-contained C
(no CMake, no re2c, no generated headers to maintain).

## Refresh

md4c is a small, self-contained library, so a refresh is a drop-in:

1. Copy `md4c.c`, `md4c.h`, `md4c-html.c`, `md4c-html.h`, `entity.c`,
   `entity.h`, and `LICENSE.md` from the new md4c release into
   `vendor/md4c/`.
2. Update `MDPARSER_MD4C_VERSION` in `php_mdparser.h`.
3. Check the system headers `md4c.c` includes against the pre-include list
   in `mdparser_md4c_vendor.c`; its `malloc`/`realloc`/`free` macros are safe
   only while every one of them is included first.
4. Rebuild and run `make test`.
5. Re-apply or drop each behavior patch, the performance patch, and the
   embedding hook (see Local modifications). If the new release already
   carries an upstream fix, the copy in step 1 removes that patch. Confirm
   005 still passes 652/652, run `tests/088_cr_only_line_endings.phpt` for
   the end-of-line scan's CR-only linearity, `tests/070_nul_replacement.phpt`
   for the NUL behavior, `tests/085_table_column_count.phpt` for the column
   guard, `tests/089_spoiler_wikilink_single_pipe.phpt` for the spoiler
   length test, and `tests/oom/run.sh` for the out-of-memory error paths.
   That sweep is the only gate on most of them;
   `mdparser.parse_memory_limit` reaches only the paths near its own
   boundary.
6. If `tests/005_commonmark_spec.phpt` moves, explain the delta in the
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
