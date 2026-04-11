# Vendored parser sources

mdparser embeds a modified copy of [cmark-gfm](https://github.com/github/cmark-gfm)
built directly into the extension's shared object. There is no external
runtime dependency: users install the PECL/PIE package and everything
needed to parse CommonMark + GFM ships inside `mdparser.so`.

## Layout

```
vendor/
├── VENDOR.md                 (this file)
├── upstream-rebase-notes.md  (rebase postmortem — historical reference)
└── cmark/                    cmark-gfm source, built by config.m4
    ├── src/                  core parser + extension framework
    ├── extensions/           GFM extensions (tables, strikethrough, ...)
    └── COPYING
```

The only thing we ship is the working tree that actually gets compiled.
We do **not** carry pristine upstream snapshots. The fork points are
recorded as commit SHAs below — anyone who needs to recreate a pristine
tree can `git clone` and `git checkout`.

## Pins

| Component        | Commit   | Version          | Date       |
|------------------|----------|------------------|------------|
| github/cmark-gfm | 587a12b  | 0.29.0.gfm.13    | 2023-07-21 |
| commonmark/cmark | 8daa6b1  | 0.29.0 (fork point) | 2019-05-22 |

The cmark 0.29.0 pin is the point at which cmark-gfm forked from cmark
upstream. It's a historical reference, not something we ship — use it to
seed the base for a 3-way merge if you ever need to cherry-pick a cmark
bug fix. The real working tree is cmark-gfm at 587a12b.

The CommonMark spec file used by the conformance test ships at
`tests/fixtures/commonmark-spec.txt`, taken from cmark 0.29.0's
`test/spec.txt`. It's the spec that cmark-gfm 0.29.0.gfm.13 targets.

## Generated files

Three files in `cmark/src/` are hand-written replacements for CMake
outputs, since we don't run CMake at build time:

| File                  | Purpose                                                |
|-----------------------|--------------------------------------------------------|
| `config.h`            | Feature macros (HAVE_STDBOOL_H, HAVE___ATTRIBUTE__)    |
| `cmark-gfm_version.h` | Version integer (0.29.0.gfm.13)                        |
| `cmark-gfm_export.h`  | Empty export macros (static build, no visibility)     |

Two `.in` templates (`config.h.in`, `cmark-gfm_version.h.in`,
`libcmark-gfm.pc.in`) are left in place as upstream references but not
consumed by our build.

Two files are committed in their re2c-generated form rather than being
regenerated at build time, so users don't need re2c installed:

- `src/scanners.c` — generated from `src/scanners.re`
- `extensions/ext_scanners.c` — generated from `extensions/ext_scanners.re`

Regenerate via `re2c -i --no-generation-date -W -Werror -o scanners.c scanners.re`
from each directory if the `.re` files ever change.

`src/main.c` (the cmark-gfm CLI driver) is stripped — we don't want a
`main` symbol in the PHP extension.

## Refresh: cherry-pick only, no bulk rebase

The original plan proposed a `refresh-cmark.sh` workflow that would
track cmark upstream's 0.30/0.31 improvements and forward-port them
wholesale. **Bulk rebase is not viable.** cmark and cmark-gfm diverged
on the internal AST data model (link/code/custom/list/heading fields
use `cmark_chunk` in cmark-gfm and heap-allocated strings in cmark
0.31), and a bulk rebase produces silently-broken files — not merge
conflicts, actual corrupt code where one side's assumptions are no
longer valid.

Full postmortem and per-file conflict resolutions in
`upstream-rebase-notes.md`. Cross-repo writeup in
`~/ai/wiki/debugging/cmark-gfm-rebase.md`.

**Practical path that worked:** keep cmark-gfm 587a12b as the base
tree, cherry-pick *individual* cmark upstream commits by file and
adapt each one by hand to the cmark-gfm AST shape. The four cherry-
picks listed under "Local modifications" below were enough to close
all 19 spec gaps and reach 100% conformance on cmark 0.31.2.

Future bug-fix commits from cmark upstream (post-0.31.2) can be
incorporated the same way: read the diff, understand which cmark-gfm
files and lines are affected, apply the edit by hand, and add an
entry to the "Local modifications" table. Do not attempt bulk rebases.

## Local modifications

Any edit to `vendor/cmark/` that is not a straight copy of cmark-gfm's
tree at 587a12b goes here:

### Build-related

- Removed `src/main.c` (cmark-gfm CLI driver; conflicts with PHP
  extension main symbol).
- Added `src/config.h` (hand-written replacement for CMake-generated
  feature macros).
- Added `src/cmark-gfm_version.h` (filled in from
  `cmark-gfm_version.h.in`).
- Added `src/cmark-gfm_export.h` (empty macros; static build has no
  visibility attributes).

### CommonMark 0.31 spec conformance cherry-picks

cmark-gfm at 587a12b ships the cmark 0.29 CommonMark spec. Bulk
rebasing to 0.31 is infeasible because of the AST-shape divergence
(see "Refresh: don't"). Instead we cherry-picked four focused commits
from cmark upstream to close the spec gap. All four apply cleanly to
our tree and bring us to 652/652 on cmark 0.31.2's `spec.txt`.

| Cherry-pick | Upstream commit | Files | Fixes |
|---|---|---|---|
| Emphasis rule 13 (openers_bottom split by can_open) | 34250e1 + 8bafc33 | `src/inlines.c` | 9 spec examples in emphasis section (#388, #416, #424-426, #463-465, #467) |
| Don't flatten nested `<strong>` in HTML renderer | reverts cmark-gfm's GitHub-compat tweak | `src/html.c` | Dependency of the emphasis fix; cmark-gfm had a custom "collapse adjacent strong" tweak that diverged from the spec. Removed it. |
| Treat Unicode Symbols as Punctuation for emphasis flanking | 82969a8 | `src/inlines.c`, `src/utf8.c`, `src/utf8.h` | `*£*text.` etc. (#354 and siblings) |
| Numeric entity max-digit limit in houdini_unescape_ent | 7b35d4b | `src/houdini_html_u.c` | `&#87654321;` overflow case (#28) |

The `<pre lang="X"><code>` vs `<pre><code class="language-X">` difference
is handled as an `Options::githubPreLang` runtime toggle, not a
vendor-tree change. Default stays `true` for GitHub parity; the spec
conformance test passes `false`.

When refreshing to a future cmark version, these cherry-picks need to
be re-applied manually. Every one has a `/* cmark upstream <commit>,
adapted... */` comment near the change site so you can find them.
