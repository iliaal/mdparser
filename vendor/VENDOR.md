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
| mity/md4c | `0.5.3+git10c0158` | The C source compiled into `mdparser.so`. Tracked in `MDPARSER_MD4C_VERSION` (`php_mdparser.h`); reported by `php --ri mdparser`. |
| CommonMark spec fixture | 0.31 `spec.txt` | Shipped at `tests/fixtures/commonmark-spec.txt`; `tests/005_commonmark_spec.phpt` pins md4c's conformance against it. |

md4c targets CommonMark 0.31 natively, so the parser pin and the spec
fixture are on the same spec version. There is no version gap to bridge.

If you refresh the fixture (drop in a newer `spec.txt`), update the row
above, the baseline in `tests/005_commonmark_spec.phpt`, and the version
statement in `docs/spec-coverage.md`.

## Local modifications

Six behavior patches and one embedding hook are carried in `md4c/md4c.c`.
Every change site is marked with an `mdparser local patch` comment.

The code-span line-break patch that used to live here was accepted upstream
as `10e96ad4` and is no longer local; the 2026-07-27 refresh picked it up and
dropped the local copy.

### Out-of-memory error paths

Five patches fix upstream defects on md4c's allocation-failure paths. They
matter more here than in standalone md4c: `mdparser_md4c_vendor.c` routes every
md4c allocation through an intrusive registry whose header is unlinked before
`free()`, so a double free writes through an already-freed header rather than
just tripping the allocator. Reproduced with an ASAN build of `md4c.c` plus a
fault injector that fails the *n*-th allocation, swept over every injection
point of a small Markdown corpus; all three are hit by plain CommonMark input.

`md_free_attribute` keyed its frees on `build->substr_alloc > 0`. That is wrong
in both directions. When `md_build_attr_append_substr` failed a growth realloc
after the array had already grown, `md_build_attribute` freed the three buffers
on its own `abort` path but left `substr_alloc` non-zero, so the caller's
`abort` label — `md_enter_leave_span_a`, `md_enter_leave_span_wikilink`,
`md_enter_leave_span_footnote_ref`, `md_process_leaf_block`,
`md_process_footnote_def` — freed them a second time. When the *first* growth
realloc failed, `substr_alloc` was still 0 and the already-allocated text and
type buffers leaked. The patch keys on `build->substr_types !=
build->trivial_types` instead, which is exactly the "this build owns its
storage" condition, and clears the pointers so the function is idempotent.

`md_is_link_reference_definition` freed `def->entry.label` and `def->title` at
its `abort` label. A non-NULL `def` is already committed to
`ctx->ref_def_hashtable`, which owns both and frees them in `md_free_ref_defs`,
so the label was freed twice. Reaching it needs a definition whose label *and*
title are both multiline — `label_needs_free` is only set on the multiline-label
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

All five are still present in md4c master (checked 2026-08-30 against
`raw.githubusercontent.com/mity/md4c/master/src/md4c.c`: `md_free_attribute`
still keys on `substr_alloc`, the reference-definition abort label still frees,
both `md_add_label_def` branches still fall through with `ret` at 0, and both
`md_end_current_block` call sites are still bare at upstream lines 6431 and
7244). A refresh will therefore NOT drop them — re-apply all five and re-check
upstream before assuming otherwise. They are worth reporting to mity/md4c.

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

No other vendored files are modified; md4c.c is otherwise self-contained C
(no CMake, no re2c, no generated headers to maintain).

## Refresh

md4c is a small, self-contained library, so a refresh is a drop-in:

1. Copy `md4c.c`, `md4c.h`, `md4c-html.c`, `md4c-html.h`, `entity.c`,
   `entity.h`, and `LICENSE.md` from the new md4c release into
   `vendor/md4c/`.
2. Update `MDPARSER_MD4C_VERSION` in `php_mdparser.h`.
3. Rebuild and run `make test`.
4. Re-apply or drop each behavior patch and the embedding hook (see Local
   modifications). If the new
   release already carries an upstream fix, the copy in step 1 removes that
   patch for free. Confirm the code-span behavior still leaves 005 at 652/652,
   run `tests/070_nul_replacement.phpt` for the NUL behavior, and run
   `tests/oom/run.sh` for the out-of-memory error paths — that sweep is the
   only thing that exercises them, since no PHP-side setting can make an md4c
   allocation fail.
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
