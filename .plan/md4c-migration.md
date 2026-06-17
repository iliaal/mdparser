# Migration: cmark-gfm → md4c (full single-engine swap)

Status: cmark FULLY REMOVED. md4c is the only backend. Suite green (36/0/1),
dev -Werror clean, .so 568KB (was 1.35MB). Security review done + fixed.
Remaining: Phase 6 (bench numbers, README/VENDOR/CHANGELOG docs).

## Phase 4 done — cmark removed
Deleted vendor/cmark, mdparser_ast.{c,h}, mdparser_html_postprocess.{c,h},
mdparser_arena.{c,h}, obsolete test 043 (cmark-registry). Stripped cmark from
config.m4 (+ dropped -DMDPARSER_ARENA / CMARK defines), php_mdparser.h (obj is
now just md4c masks + std), mdparser.c (MINIT only precomputes masks +
registers classes; no MSHUTDOWN), mdparser_options.c (md4c-only field table),
mdparser_parser.c (dead cmark render fns gone). MINFO shows md4c version
(MDPARSER_MD4C_VERSION = "0.5.3+git755ce49").

## Security review (docs/security/md4c-2026-06-17/findings.md) — addressed
- SS-001 CRITICAL XSS (entity-colon scheme bypass `[x](javascript&colon;..)`):
  FIXED. URL attrs decode→check→emit (mdm_attr_decode_raw + mdm_render_url_value);
  filter runs on decoded bytes. Regression vectors in 020.
- SS-002 MEDIUM data: allowlist (prefix match, raw-vs-decode, link context):
  FIXED. Exact MIME + terminator, decoded-byte check, image-context gate.
- MS-05 latent snprintf over-read: HARDENED (strlen not (size_t)w).
- MS-07 arena: MOOT (arena deleted with cmark).

## Phase 5 done (this block) — 38/38 green

Renderer hardening (real bugs found by exact-match spec testing):
- FIXED XSS: entity references (`&lt;`/`&amp;`) were decoded to RAW `<`/`&` in
  HTML output -> injection. Now re-HTML-escaped (mdm_append_codepoint_escaped).
- FIXED: `'` was escaped to `&#x27;` (cmark leaves it raw; attrs are double-quoted).
- FIXED: entity-encoded non-ASCII in URLs now percent-escaped (was raw UTF-8).
- ADDED cmark `cr()` conditional-newline model -> matches cmark inter-block
  whitespace (`<li>\n<block>`); replaced the wrong per-list-looseness hack.
- nofollow now skips `href="#..."` fragment anchors (matches prior behavior).
CommonMark spec: 649/652 exact (was cmark 630/649). The 3 (335/337/640) are
md4c code-span whitespace, conformant under the spec's own normalizer.

Test re-baselines (all deliberate md4c behavior, verified correct):
- 000/002/006/021: regenerated (GFM task-list classes, no-op dropped options,
  AST without positions + non-paragraph-wrapped footnotes, footnote markup).
- 005: 649/652 with 3 documented code-span failures.
- 010/011 parity: divergence sets vs pure-PHP parsers updated.
- 020 security: raw HTML now ESCAPED (was cmark suppression) -- all XSS-safe.
- 023/026/027/028: footnote HTML format, sourcepos-inert, escape-not-suppress,
  fragment-skip-rel, footnote backref href `#fnref-{id}-{ref}`.
- 031/044: the postprocess-rescan threat model is GONE (anchors/nofollow are
  in-stream); raw-HTML anchors are never rel-rewritten. The in-stream design
  also FIXED the old CR-003 raw/markdown slug-collision limitation.

Dropped options kept as accepted-but-inert (no API break): sourcepos,
githubPreLang, liberalHtmlTag, strikethroughDoubleTilde,
tablePreferStyleAttributes, fullInfoString.

## Remaining

1. Delete cmark: vendor/cmark, mdparser_ast.{c,h}, mdparser_html_postprocess.{c,h},
   mdparser_arena.{c,h}; strip cmark from config.m4, php_mdparser.h (obj fields,
   cmark_mem, cached_extensions, EXT/PP masks, defaults), mdparser.c MINIT (ext
   reg + ast-strings + zend_mem), mdparser_options.c (cmark columns), and the
   dead cmark render fns in mdparser_parser.c (~120 refs). Green suite is the
   oracle -- rebuild + retest after.
2. Bench on clean opt build; update README/bench; changelog breaking changes;
   rewrite VENDOR.md for md4c.
3. SECURITY Codex cycle on the renderer (run on the now-complete artifact).

## Phase 4 progress (this block)

- toAst/ast -> mdparser_md4c_ast.c (zval-stack builder; matches cmark array
  shape minus source positions; tables flatten thead/tbody, tasklist type,
  literal accumulation for code/html, per-column alignments from header cells).
  Verified: 047 + 036 pass; 006 fails only on dropped sourcepos + md4c's
  non-paragraph-wrapped footnote content (both expected divergences).
- toXml/xml -> mdparser_md4c_xml.c (streaming CommonMark-XML emitter, cmark
  layout: 2-space indent, xml:space="preserve", destination/title attrs).
- toInlineHtml -> md4c HTML renderer (ZWSP normalization kept; nofollow now
  in-stream, dropped the postprocess pass). 4/5 inline tests pass.

Test status: 24/38 pass. 14 failures, ALL deliberate-behavior re-baselines
(verified, no known bugs): 000/002/005/010/011/023/026 (output shape: safe
escape, void-element style already matched, static xml format), 006 (sourcepos
+ footnote wrap), 020 (escape vs suppress -- SECURITY GATE, needs careful
re-baseline), 021 (md4c footnote markup), 027 (sourcepos sub-check), 028
(footnote-ref sub-checks), 031/044 (postprocess-scanner tests now MOOT --
anchors/nofollow are in-stream; rework or remove).

## Remaining (next focused block)

1. cmark REMOVAL (do AFTER re-baseline so green baseline detects breakage):
   delete vendor/cmark, mdparser_ast.{c,h}, mdparser_html_postprocess.{c,h},
   mdparser_arena.{c,h}; strip cmark from config.m4, php_mdparser.h (obj
   cmark fields, cmark_mem, cached_extensions, EXT/PP masks), mdparser.c MINIT
   (ext registration + ast-strings init), mdparser_options.c (cmark columns +
   read_masks cmark outputs), mdparser_parser.c (dead cmark render fns).
2. Phase 5: re-baseline the 14 tests (careful verify each; 020 security hard;
   rework 031/044; add UTF-8 validation test). Decide dropped-option handling
   (sourcepos etc. -- remove fields vs no-op) + update stub/arginfo.
3. Phase 6: re-bench on clean opt build, update README/bench, changelog the
   breaking changes, rewrite VENDOR.md.
4. SECURITY Codex cycle on the safe-mode renderer (workflow mandate).

## Progress log

- Phase 1 DONE: md4c vendored additively (vendor/md4c/, MIT, builds clean alongside
  cmark, .so loads, original 38 tests green before cutover).
- Phase 2 DONE (additive): Options computes md4c parser-flags + render-opts masks
  alongside the cmark masks (mdparser_options.c field table extended with md4c
  columns; read_masks + init_defaults output both; parser obj caches both).
- Phase 3 DONE & verified: mdparser_md4c_html.{c,h} — own MD_PARSER HTML renderer.
  toHtml + static html() routed to it. Verified by direct exercise:
  CommonMark structure, GFM tables/tasks/strike/autolink, safe-mode raw-HTML
  ESCAPING, dangerous-URL filter (javascript/vbscript/data: minus image), GFM
  tagfilter (unsafe mode), streaming heading anchors w/ dedup, nofollow (rel-first
  order matched), full SmartyPants (curly quotes + en/em dash + ellipsis),
  UTF-8 validation pre-pass, footnotes.

Test status: 26/38 pass. The 12 failures are ALL deliberate behavior changes
needing re-baseline (Phase 5), no known bugs:
  - safe-mode escape (chosen) vs cmark suppression: 020 (security gate), 005 (spec
    runner uses default/safe; md4c engine is 652/652 with unsafe), 002, 010, 011,
    023, 000.
  - sourcepos dropped: 027 (one sub-check), AST positions later.
  - md4c footnote HTML differs from cmark: 021, 023.
  - postprocess-scanner tests moot now (anchors/nofollow are in-stream): 031, 044.

NOT yet verified: my renderer's full CommonMark-spec pass rate (only md4c-engine
652/652 confirmed). The 005 re-baseline in Phase 5 will measure the renderer itself.

Open: md4c uses libc malloc (not emalloc) — memory_limit shim deferred. cmark NOT
yet removed (XML/AST still use it).

---


## Phase 0 results (spike, standalone C drivers + spec runner)

Engine-to-engine, both `-O2`, same corpora, comparable defaults (md4c GFM dialect;
cmark validateUtf8+githubPreLang+GFM exts):

| Corpus | cmark | md4c | md4c speedup |
|---|--:|--:|--:|
| small 200 B | 0.0056 ms | 0.0026 ms | 2.2x |
| medium 1.9 KB | 0.0325 ms | 0.0091 ms | 3.6x |
| large 205 KB | 2.546 ms | 0.976 ms | 2.6x |

Conformance: **md4c 652/652 (100%) on official CommonMark 0.31.2** (md4c-vendored
spec.txt, official example count). Current cmark-gfm backend pins 630/649 (0.29+picks),
so md4c *improves* spec compliance. Spike lives in /tmp/md4c-spike (throwaway).

Gate verdict: both unproven premises (perf win, 0.31 conformance) confirmed favorable.
md4c wins at every size, not just large. GO.

---

(original plan below)

Decision (2026-06-17): full single-engine swap; source positions are expendable.
Origin: issue #3 (md4c ~2-3x faster on medium/large; gap is cmark AST vs md4c SAX).

## Goal & honest scope

Replace the cmark-gfm backend with md4c (https://github.com/mity/md4c, MIT).
- Perf win is **HTML-path-only and concentrated on large documents**. `toXml`/`toAst`
  stream from callbacks (no speedup claim there, but no slowdown either).
- **Lost capability (accepted):** source positions — `sourcepos` option and the
  `start_line`/`start_column`/`end_line`/`end_column` fields in `toAst`. md4c exposes
  no line/column info; unrecoverable without forking the parser.
- **Output is not byte-identical to cmark.** Different engine, different 0.31 edge
  cases. A compat break; acceptable pre-1.0, signal with a hard version bump.

## Key design decision

None of the three output paths need a persistent C AST. md4c enter/leave callbacks
feed small state-stack consumers:
- HTML: append to a buffer, tiny state stack.
- XML: append with depth tracking.
- AST: stack of zval arrays, append child on `leave` (no position fields).

So we keep md4c's no-tree speed on every path. We **own the renderer as project
source** built on md4c's stable callback API rather than forking vendored
`md4c-html.c` — preserves the "no bulk vendor edits" rule. (Stock `md4c-html` can't
do safe-mode raw-HTML escaping, which is the whole reason we need our own.)

## md4c facts that drive the work (source-verified, master)

- SAX push parser, no AST/DOM. Text delivered by pointer into the input (no copy).
- **No source positions** (only `task_mark_offset` on list items).
- Ships HTML renderer (`md4c-html.c` + needs `entity.c/.h`); **no XML renderer**.
- Flags present: tables, strikethrough, tasklists, permissive autolinks (URL/email/WWW),
  footnotes, latex, wikilinks, underline, spoilers, sub/super, highlight, admonitions,
  noindentedcode, collapsewhitespace, permissiveatx, hard-soft-breaks, granular NOHTML.
- **Missing vs cmark:** GFM tagfilter, smart punctuation, UTF-8 validation, an "unsafe"
  concept (raw HTML is passthrough-by-default; only lever is wholesale NOHTML).
- HTML renderer flags: DEBUG, VERBATIM_ENTITIES, SKIP_UTF8_BOM, XHTML. Escapes the
  5 HTML chars; **no sanitization / no tagfilter analog**.
- MIT license. No deps beyond libc. Vendor footprint ~6 files if using md4c-html
  (md4c.c/.h + md4c-html.c/.h + entity.c/.h); md4c.c/.h + entity.c/.h if we own the renderer.
- md4c uses libc malloc internally with **no allocator hook** → bypasses PHP
  `memory_limit` unless we shim malloc→emalloc in md4c.c.

## Options remap

| Keep / remap to md4c | Drop (no md4c analog) |
|---|---|
| tables/strikethrough/tasklist → MD_FLAG_*; autolink → MD_FLAG_PERMISSIVEAUTOLINKS | sourcepos, githubPreLang, liberalHtmlTag, strikethroughDoubleTilde, tablePreferStyleAttributes, fullInfoString |
| footnotes → MD_FLAG_FOOTNOTES; hardbreaks → MD_FLAG_HARD_SOFT_BREAKS | |
| Reimplemented in our code: validateUtf8 (pre-pass), tagfilter (HTML postprocess), smart (text cb), nobreaks (softbreak cb), unsafe/safe-mode (escape vs passthrough), headingAnchors (at heading emit), nofollowLinks (at link emit) | |

Open decision: drop removed fields outright (breaks named-arg ctor) vs keep as no-ops.
Leaning drop (pre-1.0 + expendable).

## Phases

- **Phase 0 — spike (gate):** measure md4c vs cmark HTML render on small/medium/large
  (clean opt build); get a real CommonMark 0.31 pass count. Kill if no large-doc win
  or worse conformance.
- **Phase 1 — vendor + build:** vendor md4c, malloc→emalloc shim, config.m4 swap,
  remove vendor/cmark, rewrite VENDOR.md + CLAUDE.md.
- **Phase 2 — options remap:** new field table, stub + arginfo + presets, tests 001/002/023.
- **Phase 3 — HTML path (bulk):** own MD_PARSER HTML callbacks: safe-mode escape vs
  unsafe passthrough (XSS boundary), tagfilter blocklist, heading anchors, nofollow,
  smart, breaks, UTF-8 pre-pass. toHtml/toInlineHtml/static html.
- **Phase 4 — XML + AST:** streaming XML emitter + zval-stack AST builder (no positions).
  toXml/toAst/static xml/ast. tests 003/006.
- **Phase 5 — conformance + security:** rebuild 005 spec baseline (explain in commit),
  re-verify 020 security (XSS) hard, re-baseline parity/footnotes/anchors/nofollow/limits,
  add UTF-8 validation test. Full make test green.
- **Phase 6 — bench + docs:** re-run bench, confirm large-doc win, update README/bench,
  changelog breaking changes, VENDOR.md.

## Risks

- **XSS via safe-mode inversion** — md4c is passthrough-by-default, opposite of cmark.
  Top risk. Flow A codex-cycle before merge (multi-file + security + parser).
- **memory_limit** stops bounding parse memory unless the malloc shim lands.
- **Output compat break** — unavoidable; hard version bump + loud changelog.
- Renderer correctness (matching CommonMark HTML) is the hardest code; use md4c-html.c
  as reference.
