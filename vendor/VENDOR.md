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
| mity/md4c | `0.6.0+gitf9e5eb23` | The C source compiled into `mdparser.so`. Tracked in `MDPARSER_MD4C_VERSION` (`php_mdparser.h`); reported by `php --ri mdparser`. |
| CommonMark spec fixture | 0.31 `spec.txt` | Shipped at `tests/fixtures/commonmark-spec.txt`; `tests/005_commonmark_spec.phpt` pins md4c's conformance against it. |

md4c targets CommonMark 0.31 natively, so the parser pin and the spec
fixture are on the same spec version.

If you refresh the fixture (drop in a newer `spec.txt`), update the row
above, the baseline in `tests/005_commonmark_spec.phpt`, and the version
statement in `docs/spec-coverage.md`.

## Local modifications

Two behavior patches and two embedding hooks are carried in `md4c/md4c.c`
and `md4c/md4c.h`. Every change site is marked with an `mdparser local patch`
or `mdparser local integration hook` comment.

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
exactly one replacement event. Still unfixed upstream as of `f9e5eb23`. Drop
this patch when a refreshed md4c contains the equivalent pointer/size
advance.

The embedding hook is in `md_parse`. When `MD_PARSER_BAILOUT_GUARD` is
defined, the call to `md_process_doc` runs inside the wrapper-provided guard.
A Zend memory-limit bailout can otherwise jump past md4c's cleanup and leak
its libc buffers. Catching at this exact frame keeps the stack-owned `MD_CTX`
valid while the unchanged cleanup frees reference definitions, footnotes,
buffers, marks, block storage, and containers. Standalone md4c builds do not
define the macro and compile the stock path.

The attribute provenance hook adds `MD_ATTRIBUTE::source_text` in `md4c.h`
and sets it to the pre-copy `raw_text` in `md_build_attribute`. md4c copies
attribute text when it contains an entity or backslash escape; the copied
pointer no longer identifies a line-start sentinel inserted by mdparser's
`toInlineHtml()` normalization. The HTML renderer checks `source_text` only
when it points into the normalized input, so attributes built from separate
scratch buffers are not mistaken for source bytes. The pointer is valid only
during the callback, like the other `MD_ATTRIBUTE` fields. Re-apply the hook
on refresh and run `tests/097_inline_attribute_sentinel.phpt`.

### Dropped at the `f9e5eb23` refresh

Five behavior patches are gone because upstream merged them. With comments
stripped, the upstream code matches the local patch line for line:

- `e2c6766`: `md_resolve_bracket_link` disables every mark in a consumed
  full-reference label. `tests/096_reference_link_label_marks.phpt` covers it.
- `f353ad6`: `md_process_footnote_defs` emits definitions through the
  `footnote_order` pointer array, so the label hash table keeps its targets,
  and `md_resolve_bracket_footnote` appends references first found inside a
  footnote body. `tests/099_footnote_definition_lookup_after_ordering.phpt`
  covers it.
- `ae821cb`: `md_resolve_bracket_footnote` widens the opener over `^` only
  after the definition lookup succeeds. `tests/103_failed_footnote_probe.phpt`
  covers it.
- `e313f46`: `md_is_html_block_start_condition` checks the terminator after a
  type 1 name, excludes self-closing type 1 names from type 7, and keeps
  scanning type 6 names after a shorter prefix fails.
  `tests/100_html_block_tag_boundaries.phpt` covers it.
- `205b7f7`, `157e9fc`, and `60e8065`: `MD_CONTAINER` records
  `li_block_byte_end` and `li_has_content`, and closing an empty item after
  two blank lines adds the item's content indent back to the line.
  `tests/101_two_blank_line_list_item.phpt` covers it.

The linked block-quote lookup (`first_quote_container`,
`last_quote_container`, `next_quote_container`) is gone too. Upstream
`ff1cd9f` and `be8a0fe` (GHSA-582f-rcwc-x4mp) fix the same blank-line
container scan differently: `md_analyze_line` caches the range of implicit
list parents from the previous line and jumps to its end when the next blank
line starts inside that range. Both mechanisms yield the index of the next
block quote, or `n_containers`, for the same input. Only blank lines keep the
cache, and a blank line closes only containers past its own result, so the
cached range still names open list containers. A blank line that starts
outside the range pays for the scan with bytes of its own or of the previous
line, so the total stays linear. `tests/091_deep_list_blank_scaling.phpt`
passes on the upstream code.

This upstream source also reworks the permissive autolink URL tail
(`f895c2b`, `1645761`, `6240bca`, `775e87e`) and stops rescanning a failed
tail (`f9e5eb2`, mity/md4c#464).

### Dropped at the `58c55b69` refresh

Upstream `89a9bf4` restored the `||` length check in `md_analyze_marks` for
spoiler delimiters, so the local patch was removed. The single-pipe cases in
`tests/089_spoiler_wikilink_single_pipe.phpt` protect the behavior.

Upstream `45ec7ac` and `ec89a39` changed `md_is_link_destination_B` to the
same two-comparison test for whitespace and control bytes, so the local
performance patch was removed. The byte-set equivalence was checked for all
256 byte values with signed and unsigned `char` when the local patch landed.

This upstream source also changes the bracket-resolution queue (`c6008fb`),
permissive autolinks (`3da1937`), and verbatim tab indentation (`2671f25`).
`entity.c` has a trailing-whitespace-only upstream line normalized for the
repository's `git diff --check` gate.

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

All local parser changes are confined to `md4c.c` and `md4c.h`; md4c is
otherwise self-contained C (no CMake, no re2c, no generated headers to maintain).

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
5. Re-apply or drop each behavior patch and the two embedding hooks (see
   Local modifications). If the new release already
   carries an upstream fix, the copy in step 1 removes that patch. Confirm
   005 still passes 652/652, run `tests/088_cr_only_line_endings.phpt` for
   the end-of-line scan's CR-only linearity, `tests/070_nul_replacement.phpt`
   for the NUL behavior, `tests/085_table_column_count.phpt` for the column
   guard, `tests/089_spoiler_wikilink_single_pipe.phpt` for the spoiler
   length test, `tests/091_deep_list_blank_scaling.phpt` for bounded
   blank-line container lookup, `tests/096_reference_link_label_marks.phpt`
   for consumed reference-label marks, `tests/097_inline_attribute_sentinel.phpt`
   for attribute source provenance, `tests/100_html_block_tag_boundaries.phpt`
   for HTML block starts, `tests/101_two_blank_line_list_item.phpt`
   for list-item blank-line boundaries, and `tests/oom/run.sh` for the
   out-of-memory error paths. Run
   `tests/099_footnote_definition_lookup_after_ordering.phpt` for footnote
   definition lookup after first-reference ordering, and
   `tests/103_failed_footnote_probe.phpt` for failed footnote probes.
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

mdparser embedded cmark-gfm before 0.4.0; a cmark-gfm rebase prompted the
switch to md4c.
