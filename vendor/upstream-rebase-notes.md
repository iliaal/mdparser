# Rebase attempt notes: cmark 0.29.0 -> 0.31.2 (2026-04-11)

**Outcome: abandoned.** See `vendor/VENDOR.md` "Rebase attempt" section
and `~/ai/wiki/debugging/cmark-gfm-rebase.md` for the architectural
reason. This file preserves the per-file resolutions reached during the
attempt, so if anyone starts down the same path in the future they can
either confirm they've re-derived the same answers or skip the grunt
work.

**Useful only as reference.** These resolutions alone do not produce a
compiling tree — the deeper AST-shape incompatibilities (cmark_link,
cmark_code, cmark_custom, cmark_list, cmark_heading) are not addressed
here and still need full-file rewrites that cross into every consumer.

## Methodology

For each cmark/cmark-gfm shared file:

1. `base` = cmark 0.29.0 at tag (commit 8daa6b1), in `/tmp/cmark-029/src`
2. `ours` = cmark-gfm 0.29.0.gfm.13 at commit 587a12b, our vendor/cmark/
3. `theirs` = cmark 0.31.2 at tag eec0eeb, in `/tmp/cmark-tagged/src`
4. `git merge-file --marker-size=7 ours base theirs`
5. Hand-resolve conflicts with the decision table below.

The re2c-generated `scanners.c` was to be regenerated from merged
`scanners.re`, never reached. `entities.inc`, `case_fold.inc`,
`utf8.c`, `houdini_html_u.c`, `cmark_ctype.c`, `case_fold_switch.inc`
were "Group A" replacements where cmark-gfm made zero changes vs 0.29 so
we could take 0.31.2 wholesale.

## Per-file resolution table

Format: `<file>` — `<resolution-kind>` — `<notes>`

### Group A (take 0.31 wholesale, zero cmark-gfm modifications)

| File | Notes |
|---|---|
| `entities.inc` | HTML entity table rework (cmark 0.31.1); data file, no API surface |
| `utf8.c` | Case-folding rework from switch to table; API signature preserved |
| `houdini_html_u.c` | HTML entity table rework knock-on |
| `cmark_ctype.c` | 0 delta vs 0.29 but worth refreshing |
| `case_fold.inc` (new) | Replaces `case_fold_switch.inc`; internal format change |
| `case_fold_switch.inc` | **Delete** — replaced by `case_fold.inc` |

### Group B (small conflicts, decision table below)

| File | Conflicts | Resolution |
|---|---|---|
| `houdini_href_e.c` | 1 | Whitespace (tabs vs spaces in comment). Take theirs. |
| `iterator.h` | 0 | Clean merge. |
| `buffer.c` | 2 | **Keep ours.** cmark-gfm added `cmark_strbuf_sets`, `cmark_strbuf_copy_cstr`, `cmark_strbuf_swap` which other cmark-gfm code depends on. 0.31 removed them as unused but they're not unused for us. |
| `cmark_ctype.h` | 1 | **Keep ours.** cmark-gfm added `CMARK_GFM_EXPORT` on `cmark_isalnum`; no-op in our static build but harmless. |
| `utf8.h` | 1 | **Union.** Keep ours' `cmark_utf8proc_is_punctuation` and also add 0.31's `cmark_utf8proc_is_punctuation_or_symbol`. |
| `inlines.h` | 1 | **Keep ours.** `cmark_clean_url/title` return `cmark_chunk` in cmark-gfm; 0.31 changed to `unsigned char *`. Changing this ripples through every caller in inlines.c. |
| `xml.c` | 2 | **Keep ours** (includes: config.h, cmark-gfm.h, syntax_extension.h). Also keep `CMARK_BUF_INIT(mem)` instead of `CMARK_BUF_INIT(root->mem)` (equivalent for us, preserves explicit mem param). |
| `scanners.h` | 2 | **Keep ours.** cmark-gfm added `_scan_liberal_html_tag` for CMARK_OPT_LIBERAL_HTML_TAG. |
| `chunk.h` | 1 | **Keep ours.** `struct cmark_chunk { unsigned char *data; }` (mutable). 0.31's `const unsigned char *data` breaks cmark-gfm's ltrim/rtrim ops. |
| `houdini.h` | 1 | **Keep ours.** cmark-gfm has both `houdini_escape_html` (3-arg) and `houdini_escape_html0` (4-arg with secure flag). 0.31 merged them. Keeping ours avoids renaming all callers. |
| `cmark.c` | 1 + 1 silent dup | **Keep ours** for `CMARK_NODE_LAST_BLOCK/INLINE` and `CMARK_GFM_VERSION`. Also delete the silent duplicate `cmark_get_default_mem_allocator` that git merge-file added at line ~51 (merge accepted both cmark-gfm's `&CMARK_DEFAULT_MEM_ALLOCATOR` and 0.31's `&DEFAULT_MEM_ALLOCATOR` without flagging). |
| `render.h` | 1 | **Union.** Keep ours' `outc` signature with the `cmark_node *` parameter, add 0.31's `block_number_in_list_item` field. The struct field order matters for positional init — update render.c struct init to match. |
| `references.h` | 2 | **Keep ours.** cmark-gfm's struct uses `cmark_map_entry` and `cmark_chunk`; 0.31's uses linked-list + sorted array. Consistent with keeping references.c as-is. |
| `buffer.h` | 5 | **Keep ours** for all 5. cmark-gfm added `cmark_strbuf_swap`, `cmark_strbuf_len`, `cmark_strbuf_cmp`, `cmark_strbuf_copy_cstr`, `cmark_strbuf_sets`, `cmark_strbuf_strchr`, `cmark_strbuf_strrchr`. Used elsewhere in cmark-gfm. |
| `parser.h` | 1 | **Union.** Add 0.31's `cmark_strbuf content` field and change `total_size` to `unsigned int`. Keep cmark-gfm's `syntax_extensions`, `inline_syntax_extensions`, `backslash_ispunct`. |

### Group C (moderate conflicts, partial resolutions)

| File | Conflicts | Resolution |
|---|---|---|
| `latex.c` | 2 | **Keep ours** for includes (config.h, cmark-gfm.h, syntax_extension.h). **Keep ours** for `outc` signature (takes `cmark_node *node`). |
| `render.c` | 3 | **Keep ours** for includes (adds chunk.h, cmark-gfm.h, syntax_extension.h). **Discard** the 0.31 `cmark_mem *mem = root->mem` reassignment — we already take `mem` as a parameter. **Merge** the renderer struct initializer: must contain `options, mem, &buf, &pref, 0, width, 0, 0, true, true, false, false, NULL /* block_number_in_list_item */, outc, S_cr, S_blankline, S_out, 0 /* footnote_ix */` — 18 values to match the merged render.h field count. |
| `iterator.c` | 0 | Clean merge. |
| `man.c` | 1 | **Keep ours** for includes. |
| `node.h` | 2 | **Flag bit union.** Add 0.31's `CMARK_NODE__LIST_LAST_LINE_BLANK` at `(1 << 3)`, shift cmark-gfm's `CMARK_NODE__REGISTER_FIRST` to `(1 << 4)`. Extensions use `cmark_register_node_flag` which counts from `REGISTER_FIRST` so the shift is safe. **Fix `cmark_node_mem`** to return `node->mem` directly — it was referencing a `content` field that git merge-file removed via the struct merge. |
| `commonmark.c` | 5 | **Includes:** keep ours. **outc signature:** keep ours (takes `cmark_node *node`). **`needs_escaping` conditional:** keep ours' `c == '~'` (cmark-gfm strikethrough), merge in 0.31's stricter `(c == '!' && (!nextc || nextc == '['))`. **`is_autolink`:** keep ours' chunk-based path. **`S_render_node` dispatch:** keep ours' `if (node->extension && node->extension->commonmark_render_func) { ... }` dispatch. |
| `references.c` | 3 | **Not mergeable.** Restore ours wholesale. cmark-gfm rewrote this file to use `cmark_map_entry` and the `map.c` hashtable module; 0.31 kept the original linked-list + qsort approach. The whole file is one architectural decision, not three independent conflicts. |

### Group D (structural blockers — not attempted)

These files also have conflicts but were never reached because Group C
already revealed the AST-shape incompatibilities that make the whole
exercise moot:

- `node.c` (237 LOC cmark-gfm delta, ~40 hunks against 0.31)
- `html.c` (268 LOC, extension render hooks)
- `blocks.c` (359 LOC, cmark_parser_dispose/reset, extension hooks)
- `inlines.c` (609 LOC, cmark_chunk-based link/image parsing)

Every one of these touches the AST shape. Until the link/code/custom/
list/heading field types are unified across cmark-gfm and cmark 0.31,
these files cannot coexist in a single source tree.

## Scanners.c / scanners.re

cmark 0.31's `scanners.re` was never merged. The generated `scanners.c`
(~13K LOC, 20/20 hunks reported as failed by patch) is reproducible from
`scanners.re` via `re2c -i --no-generation-date -W -Werror -o scanners.c scanners.re`.
A correct rebase would merge the `.re` source first, then regenerate
`.c` with re2c. Same for `extensions/ext_scanners.c` from
`extensions/ext_scanners.re`.

## What would make this mergeable

Option A: **port cmark-gfm's extension framework onto cmark 0.31's
AST shape.** New project, essentially. Fork cmark 0.31 into a new tree,
reintroduce the plugin/registry/syntax_extension infrastructure, and
re-implement each of the five GFM extensions against the string-based
link/code/custom/list/heading types. This would be several weeks of
careful work and produces something that looks like what cmark-gfm
maintainers should have been doing for the last three years.

Option B: **port cmark 0.30/0.31's spec fixes back onto cmark-gfm's
AST shape.** Hunt down the specific cmark commits that landed each spec
clarification (entity table, tab handling in tight lists, HTML block
boundary tightening, etc.) and hand-adapt each to the chunk-based AST.
Scopes to exactly the behavior we want without the architectural
upheaval, but requires identifying and cherry-picking ~20-30 individual
commits from cmark's 0.30/0.31 history.

Neither is worth doing for a 19-example spec delta unless a specific
user hits a bug that bites them in production. At that point do Option B
for exactly that bug.
