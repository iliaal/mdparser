# mdparser md4c backend — security findings

- **Date:** 2026-06-17
- **Target:** mdparser md4c rendering backend and vendored md4c core.
  - mdparser repo `0dd3166` (HEAD 2026-06-12), version `0.3.0`.
  - Audited glue: `mdparser_md4c_html.c`, `mdparser_md4c_ast.c`, `mdparser_md4c_xml.c`, `mdparser_ast.c`, `mdparser_arena.c` / `mdparser_arena.h`, `mdparser_options.c`, `mdparser_parser.c`.
  - Vendored core: `~/sec/md4c` `755ce49` (== `vendor/md4c/`): `md4c.c`, `md4c-html.c`, `entity.c`.
- **Auditor:** Claude Code `security-scan` skill (orchestrator + 2 discovery subagents + standalone md4c ASAN/UBSAN harness).
- **Build under test:** `/home/ilia/php-install-84/bin/php` (8.4 debug, NTS), `modules/mdparser.so` (debug), default `new MdParser\Parser()` (safe mode, `unsafe:false`).
- **Verification summary:** SS-001 verified RUN (live extension PoC + md4c standalone). SS-002 verified RUN (live extension PoC), graded hardening (no demonstrated script execution). Memory candidates: MS-07 re-traced and refuted; MS-05 latent/gated; all other glue candidates bounded by the 256 MB input cap or md4c callback invariants (recorded non-findings). md4c core ASAN+UBSAN-clean on adversarial smoke.
- **Methodology:** Orchestrator built the threat model and shared context, then dispatched two blind discovery subagents (sanitizer-bypass hunt over `mdparser_md4c_html.c`; memory-safety over the glue + arena). Every HIGH candidate was re-traced by the orchestrator with a concrete trigger or runtime PoC before reporting; the data-flow gate and (for MS-07) an independent re-trace replaced the blind-verifier vote because direct runtime PoCs were available. md4c core was fuzzed via a standalone `md_html()` harness compiled with `-fsanitize=address,undefined`.

## Threat model (from mdparser SECURITY.md / docs/security.md)

mdparser renders untrusted Markdown "safe by default" (`unsafe:false`). The md4c backend powers `Parser::toInlineHtml()` and `Parser::html()` and carries its own URL-scheme filter (`mdm_url_is_safe`), GFM tagfilter (`mdm_tagfilter_blocked`), and raw-HTML escaping — a separate implementation from the cmark path's `mdparser_html_postprocess.c`. In scope: XSS via markdown in default mode, URL-scheme filter bypass, tag-filter bypass, memory corruption. `toXml`/`toAst` are documented as not HTML-sanitized and are out of XSS scope by design.

## Findings index

| ID | Title | Severity | Confidence | Verification | Policy route |
|----|-------|----------|-----------|--------------|--------------|
| SS-001 | Safe-mode URL-scheme filter bypass via entity-encoded colon → XSS | CRITICAL | HIGH | RUN | PRIVATE-ADVISORY |
| SS-002 | `data:` URL allowlist defects (prefix match, raw-vs-decode MIME, allowed in link href) | MEDIUM | HIGH | RUN | SECURITY-LOW / PUBLIC-HARDENING |
| MS-07 | Arena `mdp_free` leaves `last_payload`/`last_chunk` stale | INFO (refuted) | HIGH | re-traced | PUBLIC-HARDENING |
| MS-05 | `(size_t)w` over-read after a truncating `snprintf` on heading tags | INFO (latent) | MEDIUM | gated | PUBLIC-HARDENING |

Runtime transcript: `evidence/ss001-ss002-poc.txt`.

---

## [SS-001] Safe-mode URL-scheme filter bypass via entity-encoded colon → XSS
Severity: CRITICAL
Confidence: HIGH
Verification status: RUN
Policy route: PRIVATE-ADVISORY (own repo `iliaal/mdparser`; published 0.3.0 → warrants a patch release + advisory)
Category: Input validation / sanitizer bypass (XSS)
Location: `mdparser_md4c_html.c:692` (link), `mdparser_md4c_html.c:705` (image); filter `mdparser_md4c_html.c:304-338`; renderer `mdparser_md4c_html.c:628-654`; decode `mdparser_md4c_html.c:597-626`; url-escape `mdparser_md4c_html.c:204-228`
Pattern: check-on-raw / render-on-decoded mismatch (sanitize before canonicalization)
Reachability: `MdParser\Parser::toInlineHtml($md)` and md4c-backed `Parser::html($md)`, default options (`unsafe:false`). Untrusted input is the markdown string.
Primitive: XSS (stored/reflected)

Description: `mdm_render_a_open` (and `mdm_render_img_open`) decide whether to emit the URL by calling `mdm_url_is_safe(d->href.text, d->href.size)` on md4c's **raw, entity-undecoded** attribute bytes (L692/L705). `mdm_url_is_safe` finds a scheme by scanning to the first *literal* `:` / `/` / `?` / `#` (L313) and length-compares the scheme name. An entity-encoded colon (`&colon;`, `&#58;`, `&#x3a;`) contains no literal `:` — and `&#58;` even contains a `#` that L313 treats as a path delimiter — so the scan concludes "no scheme → relative URL → safe" and returns true. The href is then rendered by `mdm_render_attribute(MDM_ATTR_URL)`, which for an `MD_TEXT_ENTITY` substring calls `mdm_decode_entity_raw` (decoding the entity to a literal `:`, L641) and `mdm_escape_url`, which does not percent-escape `:` (cmark `HREF_SAFE` parity, L204-228). The emitted attribute is a live `javascript:` scheme.

Trigger: `[x](javascript&colon;alert(1))` — also `[x](javascript&#58;alert(1))`, `[x](javascript&#x3a;alert(1))`.

Verification: RUN against `modules/mdparser.so` under `/home/ilia/php-install-84/bin/php`, default `new MdParser\Parser()`:
```
[x](javascript&colon;alert(1))  => <a href="javascript:alert(1)">x</a>
[x](javascript&#58;alert(1))    => <a href="javascript:alert(1)">x</a>
[x](javascript&#x3a;alert(1))   => <a href="javascript:alert(1)">x</a>
[x](javascript:alert(1))        => <a href="">x</a>     (literal correctly blocked)
```
md4c standalone (`cc src/md4c.c src/md4c-html.c src/entity.c` + harness) confirms md4c decodes the entity colon in href and leaves `:` unescaped: `[x](javascript&colon;alert(1))` → `<a href="javascript:alert(1)">x</a>`. Full transcript in `evidence/ss001-ss002-poc.txt`.

Impact: any application rendering untrusted markdown with `toInlineHtml`/`html` in default safe mode emits a clickable `javascript:` link; a victim clicking it executes attacker JavaScript in their session. This is in-scope vector #1 in `docs/security.md`. The `<img src="javascript:...">` variant (L705) is also produced but is inert in browsers (img never executes `javascript:`); the executable primitive is the `<a href>` link.

Constraints: default configuration, trivial trigger, no special build. The cmark path (`toHtml`) is unaffected (it sanitizes in `mdparser_html_postprocess.c`); the bug is specific to the md4c HTML renderer's re-implemented filter, which checks the wrong (pre-decode) byte stream.

Escalation path: attacker submits markdown `[innocent text](javascript&colon;FETCH_AND_EXFIL)` → stored/rendered by a victim app via `toInlineHtml` → recipient's browser receives `<a href="javascript:...">innocent text</a>` → click runs arbitrary JS in the victim's origin (session theft, CSRF-token exfil, account takeover).

Fix direction: assemble the decoded URL first, then run `mdm_url_is_safe` on the **decoded** bytes (decode → check → emit), instead of check-raw → decode-emit. Concretely, build the decoded href into a `smart_str` once (the same decode `mdm_render_attribute` already performs), run the scheme filter against that buffer, and only `mdm_escape_url` it if it passes. Mirror the order used by the cmark `mdparser_html_postprocess.c` path so the two safe renderers cannot diverge again. Add `[x](javascript&colon;alert(1))`, `&#58;`, `&#x3a;`, and the image form to `tests/020_security.phpt`.

---

## [SS-002] `data:` URL allowlist defects
Severity: MEDIUM
Confidence: HIGH
Verification status: RUN
Policy route: SECURITY-LOW / PUBLIC-HARDENING (filter-correctness; no demonstrated script execution)
Category: Input validation (allowlist correctness)
Location: `mdparser_md4c_html.c:324-335`, reached from `:692` (link) and `:705` (image)
Pattern: prefix-match allowlist without separator; comparison on raw pre-decode bytes; scheme allowed in navigable context
Reachability: `toInlineHtml`/`html`, default safe mode
Primitive: filter bypass (data-URL surface widening)

Description: three facets of the `data:` branch of `mdm_url_is_safe`, all confirmed live:
1. The MIME check (`rest_len >= ol && mdm_ascii_ncasecmp(rest, ok[k], ol) == 0`, L329-332) is a prefix match with no separator check, so any MIME beginning with an allowlisted string passes: `data:image/png+xml;…`, `data:image/pnghtml;…`. The documented guarantee (only `image/{gif,png,jpeg,webp}`) is violated.
2. The MIME is compared on md4c's raw pre-decode bytes, while the emitted URL is entity-decoded (same root-cause class as SS-001): `data:image/png&#59;x,…` passes the check as `image/png&#59;x…` then renders as `data:image/png;x,…`.
3. The `data:` allowlist authorizes `data:` in link `href` (L692), not just image `src`, producing a navigable `data:` document on click.

Trigger / Verification (RUN, default safe mode):
```
![x](data:image/png+xml;base64,AAAA)  => <img src="data:image/png+xml;base64,AAAA" alt="x" />
[c](data:image/png,x)                 => <a href="data:image/png,x">c</a>
![a](data:image/png&#59;x,AAAA)        => <img src="data:image/png;x,AAAA" alt="a" />
![x](data:image/svg+xml;base64,AAAA)  => <img src="" alt="x" />   (svg correctly blocked)
```

Impact: widens the accepted `data:` surface beyond the four intended image types. Not escalated to script execution: a working XSS would need `text/html` or `image/svg+xml`, and neither matches an allowlisted prefix, so the browser renders matched payloads as images. Treated as filter-correctness / hardening, not XSS.

Constraints: default config, trivial trigger. Lower bound on severity because no script-execution path was demonstrated.

Fix direction: after a prefix match, require the next byte to be a MIME terminator (`;` or `,`); run the MIME comparison on the decoded URL (folds into the SS-001 decode-then-check fix); and gate the `data:` allowlist to image context only (reject `data:` in link `href`).

---

## Memory safety

No live memory-safety finding. md4c core (`md4c.c`/`md4c-html.c`/`entity.c`) was ASAN+UBSAN-clean on adversarial smoke inputs (deeply nested brackets/blockquotes, entity floods, emphasis/table bombs, unclosed HTML — all rc=0). The glue and arena were audited in full; candidates below.

### [MS-07] Arena `mdp_free` leaves `last_payload`/`last_chunk` stale — REFUTED as a live bug
Location: `mdparser_arena.c:157-171` (free), `:200-209` (realloc grow-in-place)
`mdp_free` does not clear `a->last_payload`/`a->last_chunk`. The grow-in-place path (L200) fires only when `ptr == a->last_payload && a->last_chunk == a->head`. Any `mdp_alloc` between a block's bump and a realloc either sets `last_payload` to the new bumped block (L136) or clears it on reuse (L121-122); therefore `last_payload == B` implies no intervening allocation, which implies B is still the physical chunk tail, so the rewind cannot land mid-chunk. The only corrupting sequence is `realloc(B)` after `free(B)` with no allocation between — a caller use-after-free. cmark's `cmark_strbuf_free` NULLs the buffer pointer before any later grow, so cmark never reallocs a freed pointer. Not reachable.
Hardening (optional, defense-in-depth): in `mdp_free`, if `ptr == a->last_payload` set `a->last_payload = a->last_chunk = NULL`, so a future grow-in-place can never observe a freed block even if a caller misbehaves.

### [MS-05] `(size_t)w` over-read after a truncating `snprintf` on heading tags — LATENT (gated)
Location: `mdparser_md4c_html.c` heading emit (`open[16]`, `tag[5]`, `close[7]` with `out_append(r, buf, (size_t)w)`)
`snprintf` returns the would-be length; if a heading level were large enough to truncate, `(size_t)w` would over-read past the stack buffer. md4c caps heading level at 6 (`<h1>`..`<h6>`), so no buffer truncates and the path is unreachable today. Latent pattern only.
Hardening: use `strlen(buf)` consistently (as the sibling heading-tag emit already does) instead of `(size_t)w`, or assert `w < sizeof buf`.

### Checked non-findings (bounded; recorded so they are not re-chased)
- `mdm_render_attribute` `substr_offsets[i+1]` read: sound — md4c guarantees `substr_offsets[LAST+1] == size`.
- Entity numeric decode `unsigned cp` overflow: bounded; out-of-range `cp` maps to U+FFFD before encoding. Correctness-only.
- `mdm_render_a_open` control-char-mid-scheme (`java<TAB>script:`): md4c angle-bracket dest allows TAB, but `mdm_escape_url` percent-encodes it (`java%09script:`), which browsers do not decode in the scheme → inert. CR/LF rejected by md4c as newlines.
- `int i` substring index vs uint32 offsets, `mdm_validate_utf8` `len*3+1`, AST recursion depth, table-cell index growth, `language-` `strncmp` short-circuit, ZWSP-strip bound, title/alt `"`-escaping, GFM tagfilter terminator, autolink dangerous-scheme (`href=""`), NUL→U+FFFD: all bounded by the 256 MB input cap, md4c callback invariants, or correct short-circuit/`strlen` usage.

---

## Consolidated remediation

A single change closes SS-001 and the SS-002 raw-vs-decode facet: **decode the URL, then run `mdm_url_is_safe` on the decoded bytes, then escape** (decode → check → emit) in `mdm_render_a_open`/`mdm_render_img_open`. Add, separately: a MIME-terminator check in the `data:` branch and an image-context gate for `data:`. Add the SS-001/SS-002 vectors to `tests/020_security.phpt` and confirm red-before / green-after against `php-install-84`. Optional hardening: MS-07 `last_payload` clear and MS-05 `strlen`/assert. Because 0.3.0 is published, ship SS-001 as a patch release with a private advisory.

---

## Resolution (2026-06-17, applied to the md4c-migration branch)

All findings addressed in `mdparser_md4c_html.c`; full suite green (36/0/1),
dev `-Werror` clean.

- **SS-001 (CRITICAL) — FIXED.** URL attributes are now fully entity-decoded
  to raw bytes (`mdm_attr_decode_raw`) *before* the scheme filter runs, then
  percent-escaped: decode → check → emit (`mdm_render_url_value`). The filter
  (`mdm_url_is_safe`) operates on the decoded form, so `javascript&colon;`,
  `&#58;`, `&#x3a;` (link and image) are all neutralized to an empty attribute.
  Verified live; regression vectors added to `tests/020_security.phpt`.
- **SS-002 (MEDIUM) — FIXED.** `mdm_url_is_safe` now (a) requires an exact MIME
  match terminated by `;`/`,`/end (rejects `image/png+xml`, `image/pnghtml`),
  (b) runs on decoded bytes (folded into the SS-001 fix), and (c) gates `data:`
  to image context only via an `image_context` parameter (`data:` rejected in
  `<a href>`). Regression vectors added.
- **MS-05 (latent) — HARDENED.** Heading/footnote tag emits use `strlen(buf)`
  instead of `(size_t)w`, removing the truncating-snprintf over-read pattern.
- **MS-07 (refuted) — MOOT.** The arena (`mdparser_arena.c`) was removed with
  the cmark backend; md4c uses libc malloc and our renderers use smart_str.

Note: the line numbers in the findings above reference pre-migration HEAD
`0dd3166` (cmark backend present). `mdparser_ast.c`, `mdparser_arena.c`, and
`mdparser_html_postprocess.c` no longer exist; the live safe renderer is
`mdparser_md4c_html.c`.

## Codex review follow-up (2026-06-17)

A second-reviewer (Codex) pass on the migration diff surfaced three more items,
all fixed:
- **P1 — config.w32 stale.** The Windows build script still listed `vendor/cmark/*`
  and the deleted `mdparser_ast.c`/`mdparser_html_postprocess.c`. Rewritten for the
  md4c sources (mirrors config.m4).
- **P2 — validateUtf8 not applied to toXml/toAst.** The non-HTML paths passed only
  parser flags; invalid input bytes reached md4c unsanitized. The UTF-8 validation
  pre-pass is now a shared helper (`mdparser_md4c_util.c`) run by all three render
  paths; XML/AST take a `validate_utf8` argument.
- **P3 — fragment nofollow exception checked raw bytes.** `[x](&#35;section)` got
  `rel="nofollow"` because the `#` test ran before entity decoding. The href is now
  decoded once and both the fragment exception and the scheme filter read the
  decoded bytes. Regression coverage: tests/049_validate_utf8_and_entity_fragment.phpt.
