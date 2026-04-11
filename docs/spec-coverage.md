# Spec coverage

mdparser targets **CommonMark 0.31** (the spec.txt at `cmark` commit
`eec0eeb`, version 0.31.2 released February 2026) and achieves
**100% conformance** — all 652 examples in spec.txt pass exactly.

The conformance test lives at `tests/005_commonmark_spec.phpt` and reads
`tests/fixtures/commonmark-spec.txt`. It runs as part of the standard
`make test` and blocks any change that regresses spec behavior.

## How we got here

mdparser embeds `cmark-gfm 0.29.0.gfm.13` (commit `587a12b`) as the
core parser. cmark-gfm targets CommonMark 0.29 (May 2019). CommonMark
0.30 and 0.31 added several spec clarifications that cmark-gfm never
incorporated — the original release of mdparser had 19 failing
examples against 0.31's `spec.txt`.

To close the gap we applied four focused cherry-picks from cmark
upstream, documented in `vendor/VENDOR.md` under "Local modifications":

| Cherry-pick | cmark commit | Files | Fixes |
|---|---|---|---|
| Emphasis rule 13 (`openers_bottom` split by `can_open`) | `34250e1` + `8bafc33` | `inlines.c` | 9 emphasis-nesting examples (e.g. `****foo****`, `*foo **bar *baz* bim** bop*`) |
| Don't flatten nested `<strong>` in HTML output | reverts a cmark-gfm GitHub-compat tweak | `html.c` | Dependency of the emphasis fix |
| Treat Unicode Symbols as Punctuation for emphasis flanking | `82969a8` | `inlines.c`, `utf8.c`, `utf8.h` | `*£*text.`-style cases (Unicode symbol class) |
| Numeric entity max-digit limit | `7b35d4b` | `houdini_html_u.c` | `&#87654321;` overflow case |

A fifth non-code change: the spec test runs with `githubPreLang: false`
because the spec examples use `<pre><code class="language-X">` form,
while mdparser's default is `<pre lang="X"><code>` (matching GitHub's
rendering). Both forms are valid; the difference is presentation. The
spec test measures spec compliance, not GitHub-parity.

## GFM extensions

mdparser also supports the five GFM core extensions inherited from
cmark-gfm. These are *not* part of the CommonMark spec itself, but are
widely used and enabled by default:

| Extension | Spec | Test coverage |
|---|---|---|
| Tables | [GFM §4.10](https://github.github.com/gfm/#tables-extension-) | `tests/002_option_effects.phpt` |
| Strikethrough | [GFM §6.5](https://github.github.com/gfm/#strikethrough-extension-) | `tests/000_smoke.phpt`, `tests/002_option_effects.phpt` |
| Task lists | [GFM §5.3](https://github.github.com/gfm/#task-list-items-extension-) | `tests/000_smoke.phpt` |
| Autolinks | [GFM §6.9](https://github.github.com/gfm/#autolinks-extension-) | `tests/000_smoke.phpt` |
| Tag filter | GFM security feature | `tests/020_security.phpt` |

Each extension can be toggled independently via `Options`. See
`docs/options.md` for the full matrix.

## Parity with other PHP libraries

`tests/parity/` holds fixture corpora from five pure-PHP Markdown
libraries (Parsedown, cebe/markdown, michelf/php-markdown, Ciconia,
league/commonmark) to measure where mdparser's output differs from
theirs. Pinned baselines:

| Library | Fixtures | Match | Why divergences exist |
|---|---|---|---|
| Parsedown | 64 | 40 (63%) | Parsedown diverges from CommonMark on escaping, nested lists, whitespace |
| cebe/markdown (GFM) | 15 | 4 (27%) | cebe's GFM implementation diverges on tables, dense list markers |
| michelf (Gruber 1.0.3) | 23 | 1 (4%) | Different spec era entirely (Gruber 2004); kept as documentation |

None of the divergences are mdparser bugs; they're the other libraries
diverging from the CommonMark spec in their own ways. The parity tests
are pinned so any unexpected movement (either direction) becomes
visible in a diff.

## What's NOT covered

Features mdparser does not implement, because cmark-gfm doesn't:

- Definition lists (`Term :: definition`) — Parsedown Extra, michelf
  Extra, cebe Extra
- Abbreviations (`*[HTML]: ...`) — Parsedown Extra, michelf Extra
- Attribute syntax (`{.class #id}`) — league/commonmark extension
- Heading permalinks / anchors — league/commonmark extension
- Table of contents generation — league/commonmark extension
- YAML front matter — league/commonmark extension
- Mentions (`@user`) — league/commonmark extension, Ciconia
- External link postprocessing (`rel="nofollow"`)
- LaTeX math (`$$...$$`) — md4c has it
- Emoji (`:smile:`) — league/commonmark extension
- Custom admonition containers (`::: warning`)

If you need any of these, `thephpleague/commonmark` is the most
actively-maintained pure-PHP option with a plug-in extension system.

## Verifying conformance yourself

```bash
# Run just the spec conformance test
make test TESTS=tests/005_commonmark_spec.phpt

# Run the full suite (12 tests, ~1.5 seconds)
make test
```

If you want to run against a newer `spec.txt`, drop it into
`tests/fixtures/commonmark-spec.txt` and re-run. Any failures will be
listed by example number and source line.
