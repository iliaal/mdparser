# md4c core library — security findings

- **Date:** 2026-06-17
- **Target:** standalone md4c CommonMark parser, `~/sec/md4c` HEAD `755ce49` (== `vendor/md4c/` in mdparser). Files: `src/md4c.c` (7235 L), `src/entity.c` (2185 L), `src/md4c-html.c` (637 L).
- **Scope:** the parser/library itself (input = untrusted markdown via `md_parse()` / `md_html()`), distinct from the mdparser extension glue (see `../md4c-2026-06-17/findings.md`).
- **Auditor:** Claude Code `security-scan` skill — orchestrator + 3 discovery subagents (inline/mark/emphasis/bracket; block/container/GFM extensions; entity/UTF-8/refs/integer-overflow) + md4c's own libFuzzer harness under ASAN+UBSAN + a complexity/DoS timing track + orchestrator re-trace/PoC of every HIGH candidate.

## Verdict

**No memory-safety or denial-of-service finding is reachable at practical input sizes on a 64-bit build.** md4c is well-hardened (it ships an OSS-Fuzz-style harness and is continuously fuzzed). The substantive output is a **large-input / 32-bit integer-overflow hardening cluster** plus minor robustness and one cosmetic logic note. Every HIGH candidate raised by the discovery subagents was re-traced and either runtime-refuted or shown to require multi-GB input / a 32-bit `size_t`.

## Validation performed

- **libFuzzer** (`test/fuzzers/fuzz-mdhtml.c`, all dialect flags, ASAN+UBSAN, 300s, spec-file seed corpus): 137,584 runs, cov 9213, **no crash / leak / OOM / sanitizer error**. Log: `evidence/libfuzzer-300s.log`.
- **Complexity/DoS timing track** (optimized `md_html` harness, doubling inputs): emphasis/bracket/link runs, backtick-span mismatch, reference floods, mixed emphasis, entity floods, deep blockquote/list nesting, HTML-tag-dense fragments, and autolink-candidate-interleaved-with-resolved-marks — **all linear, no timeout, no crash**. The historical quadratic-emphasis class is retired; deep container nesting is iterative (no C-stack overflow).
- **ASAN+UBSAN adversarial smoke** on the standalone parser: deep nesting, entity/emphasis/table bombs, unclosed HTML, multi-line code spans / raw HTML straddling line boundaries — all clean.

## Findings index

| ID | Title | Severity | Confidence | Verification | Policy route |
|----|-------|----------|-----------|--------------|--------------|
| MC-01 | `int` size counters overflow → heap OOB write (`n_block_bytes`, `n_marks`) | HIGH-if-reachable | MEDIUM | SOURCE-ONLY (>2 GB / 32-bit gate) | PUBLIC-HARDENING |
| MC-02 | `uint32`/`int` count×size wraps in alloc math (buckets, table cols/rows, attr substrs, TEMP_BUFFER) | MEDIUM-if-reachable | MEDIUM | SOURCE-ONLY (>2 GB / 32-bit gate) | PUBLIC-HARDENING |
| MC-03 | `md_analyze_table_alignment` dash scan unbounded against `end` | LOW (latent) | MEDIUM | SOURCE-ONLY | PUBLIC-HARDENING |
| MC-04 | `md_collect_marks` derefs `md_lookup_line` result without NULL check | LOW (latent) | LOW | NOT-REPRODUCED | INFORMATIONAL |
| MC-05 | `md_resolve_bracket_link` tests `first_nested->ch` while decrementing `last_nested` (dead loop) | INFO (cosmetic) | HIGH | RUN (no behavioral delta) | PUBLIC-BUG |

## MC-01 — `int` size counters can overflow to a heap OOB write
Severity: HIGH if reachable; Confidence: MEDIUM; Verification: SOURCE-ONLY; Route: PUBLIC-HARDENING
Locations: `md4c.c:5644,5665` (`md_push_block_bytes`; `n_block_bytes`/`alloc_block_bytes`/`n_bytes` are `int`, decl :275-276); `md4c.c:2841,2856` (`md_add_mark`; `n_marks`/`alloc_marks` are `int`, decl :215-216).
`md_parse` accepts a full `MD_SIZE size` (uint32, ~4 GB) with no cap (`:7204`). The grow-guards `n_block_bytes + n_bytes > alloc_block_bytes` and `n_marks >= alloc_marks` are signed-`int` arithmetic; once the counter passes `INT_MAX` (>~2 GB of mark-dense / many-line input) it wraps negative, the realloc is skipped, and the subsequent `block_bytes[n_block_bytes]` / `marks[n_marks++]` write lands past a smaller allocation = heap OOB write (also signed-overflow UB before that). Independently found by two subagents. Not reachable below ~2 GB; consumers that cap input (e.g. mdparser caps at 256 MB) are not exposed. Fix: widen these counters to `MD_SIZE`/`size_t` with overflow-checked growth, or have `md_parse` reject `size` near `INT_MAX`.

## MC-02 — count×size wraps in other allocation math
Severity: MEDIUM if reachable; Confidence: MEDIUM; Verification: SOURCE-ONLY; Route: PUBLIC-HARDENING
Locations: `md4c.c:1778` (`n_buckets = (n_defs*5)/4`, `unsigned`; wrap at ~859 M ref-defs → `% n_buckets` div-by-zero or undersized table); `:5237` (`malloc(col_count*sizeof(MD_ALIGN))`) and `:5190` (`malloc(n*sizeof(OFF))`, `n` int) — 32-bit `size_t` only; `:1471/:1480` (attr substr realloc), `:1958` (`new_alloc_defs*def_size`), `:2095` (`n_content_lines*sizeof(MD_LINE)`, wraps at ~536 M lines on 32-bit), `:452 MD_TEMP_BUFFER` (`(sz)+(sz)/2+128` wraps near 4 GB → undersized `ctx->buffer` then `memcpy` OOB at `:5022`). On 64-bit the `count*sizeof` multiplies promote to 64-bit `size_t` and don't wrap below 2^32 elements; these are 32-bit-`size_t` or >2 GB-input exposures. Same fix family as MC-01 (overflow-checked sizes), plus a defined-behavior guard on the `n_defs*5` bucket math.

## MC-03 — unbounded dash scan in table-alignment
Severity: LOW (latent); Verification: SOURCE-ONLY; Route: PUBLIC-HARDENING
`md4c.c:5128`: `while(CH(off) != _T('-')) off++;` has no bound against `end`/`ctx->size`. Safe today only by the invariant that `col_count` exactly equals the number of `-`-runs in the underline row, and all consumers read the same value truncated through a 16-bit `MD_BLOCK.data` bitfield (`:5286`, truncation at `:7029`). The fragility: if a future change re-derives a count from the untruncated value while this loop uses the truncated one (or vice versa), the equality breaks and `off` walks off the end. Fix regardless: `while(off < end && CH(off) != '-')`.

## MC-04 — `md_lookup_line` NULL result dereferenced
Severity: LOW (latent); Confidence: LOW; Verification: NOT-REPRODUCED; Route: INFORMATIONAL
`md4c.c:3365,3414` reassign `line = md_lookup_line(...)` after a code span / raw-HTML span advances `off` past `line->end`, then `continue` the loop which derefs `line->beg`/`line->end` (`:3243-3244`) with no NULL check; `md_lookup_line` can return NULL (`:557`) if `off` lands in an inter-line gap. Reachability could not be proven by reading, and targeted runtime triggers (multi-line code spans / raw HTML straddling line boundaries) ran clean under ASAN. Recorded as a defensive NULL-check candidate, not a confirmed bug.

## MC-05 — dead loop from wrong variable (cosmetic)
Severity: INFO; Confidence: HIGH; Verification: RUN; Route: PUBLIC-BUG
`md4c.c:3996`: `while(first_nested->ch == _T('D') && last_nested > opener) last_nested--;` inspects `first_nested->ch` (left over from the loop at `:3992`) while decrementing `last_nested`; after the first loop `first_nested->ch` is non-`'D'`, so this loop never executes and `last_nested` stays at `closer-1`. Confirmed wrong-variable defect by reading. **Patched line 3996 and rebuilt: output is byte-identical** on every constructed trigger (permissive-autolink-in-link-text cases included), so there is **no demonstrated behavioral impact**. Reported only as a maintainer cleanup note.

## Explicitly cleared (negatives)
Entity-table binary-search read bounds; UTF-8/UTF-16 decoders on truncated trailing bytes; numeric char-ref accumulation (md4c's 6-hex / 7-decimal digit caps prevent overflow); unicode fold-info memcpy; signed-`char` map indexing (cast to `unsigned char` / range macros); permissive-autolink backward scan invariant; `md_split_emph_mark` pre-reserved dummies (no realloc invalidation); the new highlight extension (#357, structurally identical to the safe spoiler sibling); container nesting → C-stack (iterative over flat arrays); all `malloc`/`realloc` OOM NULL-checks present; no double-free/UAF on the ref-def/footnote error paths. The quadratic-HTML-tag hypothesis (no scan-horizon memo) was runtime-refuted (linear to 2.8 MB).

## Disclosure

md4c is third-party upstream (`mity/md4c`). None of the above is a vulnerability reachable at practical input sizes on a 64-bit build, so this is not advisory material. If a downstream consumer feeds md4c untrusted markdown larger than ~2 GB without a size cap, or builds md4c on a 32-bit platform, MC-01/MC-02 become real heap OOB writes — in that case route PRIVATE-UPSTREAM to the md4c maintainer. Otherwise MC-01..MC-03 are a normal upstream hardening report (issue/PR): widen `int` size counters to `size_t`/`MD_SIZE` with overflow checks (or cap `size` in `md_parse`), make the bucket-count math overflow-safe, and bound the table-alignment dash scan. mdparser itself caps input well below these thresholds, so it is not exposed.
