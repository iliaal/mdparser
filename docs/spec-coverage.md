# Spec coverage

mdparser targets **CommonMark 0.31**. The backend is
[md4c](https://github.com/mity/md4c), which implements CommonMark 0.31
natively, so there is no version gap to bridge. The code-span whitespace
handling that once needed a local patch now comes from upstream
`10e96ad4`; mdparser's remaining local patches are the NUL-replacement
fix and five out-of-memory error-path fixes (inventoried in
`vendor/VENDOR.md`, which is the authority). Neither local patch is
exercised by the spec run.

The conformance test lives at `tests/005_commonmark_spec.phpt` and reads
every example from `tests/fixtures/commonmark-spec.txt` (the 0.31
`spec.txt`). It runs as part of the standard `make test` and pins md4c's
conformance, so any change that moves the baseline shows up in a diff.

## Current baseline

The suite parses all 652 spec examples and pins the result at 652 pass,
0 fail. Stock md4c at the pinned revision passes all 652 because
upstream `10e96ad4` already carries the code-span fix for examples 335,
337, and 640 (interior line ending in whitespace). The test pins the
pass/fail counts in its `--EXPECT--` block, so any movement (a
regression, or a surprise improvement from an md4c update) shows up in
a diff.

The spec examples use the `<pre><code class="language-X">` form for
fenced code, which is exactly what mdparser emits — md4c renders only
that form. (`githubPreLang` is accepted for API compatibility but inert;
md4c exposes no `<pre lang="X">` variant, so the spec test's
`githubPreLang: false` has no effect on output.) The spec test also
enables `unsafe` and disables the GFM extensions so the input matches
plain CommonMark.

## GFM extensions

md4c exposes the GitHub Flavored Markdown extensions through parser
flags. mdparser enables the core set by default. These are *not* part of
the CommonMark spec itself, but are widely used:

| Extension | Spec | Test coverage |
|---|---|---|
| Tables | [GFM §4.10](https://github.github.com/gfm/#tables-extension-) | `tests/002_option_effects.phpt` |
| Strikethrough | [GFM §6.5](https://github.github.com/gfm/#strikethrough-extension-) | `tests/000_smoke.phpt`, `tests/002_option_effects.phpt` |
| Task lists | [GFM §5.3](https://github.github.com/gfm/#task-list-items-extension-) | `tests/000_smoke.phpt` |
| Autolinks | [GFM §6.9](https://github.github.com/gfm/#autolinks-extension-) | `tests/000_smoke.phpt` |
| Tag filter | GFM security feature | `tests/020_security.phpt` |

md4c also implements footnotes. Each extension toggles independently via
`Options`. See `docs/options.md` for the full matrix.

## Parity with other PHP libraries

`tests/parity/` holds fixture corpora from three pure-PHP Markdown
libraries (Parsedown, cebe/markdown, michelf/php-markdown) to measure
where mdparser's output differs from theirs. Pinned baselines:

| Library | Fixtures | Match | Why divergences exist |
|---|---|---|---|
| Parsedown | 64 | 42 (66%) | Parsedown diverges from CommonMark on escaping, nested lists, whitespace. Moved from 40 with the md4c backend swap; the test was regenerated in `b5a491b`. |
| cebe/markdown (GFM) | 15 | 4 (27%) | cebe's GFM implementation diverges on tables, dense list markers |
| michelf (Gruber 1.0.3) | 23 | 1 (4%) | Different spec era entirely (Gruber 2004); kept as documentation |

None of the divergences are mdparser bugs; they're the other libraries
diverging from the CommonMark spec in their own ways. The parity tests
are pinned so any unexpected movement (either direction) becomes visible
in a diff.

## md4c dialect extensions

Beyond CommonMark and GFM, md4c ships several dialect extensions.
mdparser exposes each as an opt-in `Options` flag, all defaulting to
`false`:

| Feature | Option | Syntax |
|---|---|---|
| Underline | `underline` | `_text_` renders as `<u>` instead of emphasis |
| Highlight | `highlight` | `==text==` |
| Superscript | `superscript` | `^text^` |
| Subscript | `subscript` | `~text~` |
| Spoilers | `spoilers` | `||text||` |
| LaTeX math | `latexMath` | `$inline$` and `$$block$$` |
| Wiki links | `wikiLinks` | `[[target]]` |

These are neither CommonMark nor GFM. Turn them on only when your input
expects them. See `docs/options.md` for behavior and edge cases.

## HTML output flags (heading anchors, nofollow)

mdparser can add two things the parser itself does not emit. The HTML
renderer applies them in-stream as md4c fires its events, to
Markdown-derived nodes only; raw HTML you pass through under `unsafe`
is untouched:

| Feature | Option | Behavior |
|---|---|---|
| Heading permalinks / anchors | `headingAnchors: true` | Every Markdown heading gains a GitHub-style slug `id`; collisions deduped with `-1`, `-2`, ...; raw HTML headings are left as-is |
| External link nofollow | `nofollowLinks: true` | Every Markdown link (inline, reference, autolink) gets `rel="nofollow noopener noreferrer"`; fragment anchors and raw HTML `<a>` are skipped |

Both default to `false`. See `docs/options.md` for behavior and edge
cases.

## What's NOT covered

Features mdparser does not implement:

- Definition lists (`Term :: definition`) — Parsedown Extra, michelf
  Extra, cebe Extra
- Abbreviations (`*[HTML]: ...`) — Parsedown Extra, michelf Extra
- Attribute syntax (`{.class #id}`) — league/commonmark extension
- Table of contents generation — league/commonmark extension
- YAML front matter — league/commonmark extension
- Mentions (`@user`) — league/commonmark extension, Ciconia
- Emoji (`:smile:`) — league/commonmark extension
- Custom admonition containers (`::: warning`)

If you need any of these, `thephpleague/commonmark` is the most
actively-maintained pure-PHP option with a plug-in extension system.

## Verifying conformance yourself

```bash
# Run just the spec conformance test
make test TESTS=tests/005_commonmark_spec.phpt

# Run the full suite
make test
```

If you want to run against a newer `spec.txt`, drop it into
`tests/fixtures/commonmark-spec.txt`, update the baseline in
`tests/005_commonmark_spec.phpt`, and re-run. Any failures will be listed
by example number and source line.
