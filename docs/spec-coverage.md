# Spec coverage

mdparser targets CommonMark 0.31 through
[md4c](https://github.com/mity/md4c), which implements 0.31 natively.
mdparser's local md4c patches (the NUL-replacement fix and five
out-of-memory error-path fixes, listed in `vendor/VENDOR.md`) aren't
exercised by the spec run.

The conformance test lives at `tests/005_commonmark_spec.phpt` and reads
every example from `tests/fixtures/commonmark-spec.txt` (the 0.31
`spec.txt`). It runs as part of `make test` and pins md4c's
conformance.

## Current baseline

The suite parses all 652 spec examples and pins the result at 652 pass,
0 fail. Stock md4c at the pinned revision passes all 652; upstream
`10e96ad4` carries the code-span fix for examples 335, 337, and 640
(interior line ending in whitespace). The test pins the pass/fail counts
in its `--EXPECT--` block, so a regression or an unexpected improvement
from an md4c update shows up in a diff.

The spec examples use the `<pre><code class="language-X">` form for
fenced code, the only form md4c renders. `githubPreLang` is inert, so
the spec test's `githubPreLang: false` has no effect. The spec test also
enables `unsafe` and disables the GFM extensions so the input matches
plain CommonMark.

## GFM extensions

md4c exposes the GitHub Flavored Markdown extensions through parser
flags, and mdparser enables the core set by default. They aren't part of
the CommonMark spec:

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

The divergences come from the other libraries departing from CommonMark,
not from mdparser bugs. The parity counts are pinned, so movement in
either direction shows up in a diff.

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

Turn them on only when your input expects them. See `docs/options.md` for behavior and edge cases.

## HTML output flags (heading anchors, nofollow)

The HTML renderer can add two things md4c doesn't emit. It applies them
in-stream to Markdown-derived nodes only; raw HTML passed through under
`unsafe` is untouched:

| Feature | Option | Behavior |
|---|---|---|
| Heading permalinks / anchors | `headingAnchors: true` | Every Markdown heading gains a GitHub-style slug `id`; collisions deduped with `-1`, `-2`, ...; raw HTML headings are left as-is |
| External link nofollow | `nofollowLinks: true` | Every Markdown link (inline, reference, autolink) gets `rel="nofollow noopener noreferrer"`; fragment anchors and raw HTML `<a>` are skipped |

Both default to `false`. See `docs/options.md` for behavior and edge
cases.

## What's not covered

Features mdparser doesn't implement, and where to find them:

- Definition lists (`Term :: definition`): Parsedown Extra, michelf
  Extra, cebe Extra
- Abbreviations (`*[HTML]: ...`): Parsedown Extra, michelf Extra
- Attribute syntax (`{.class #id}`): league/commonmark extension
- Table of contents generation: league/commonmark extension
- YAML front matter: league/commonmark extension
- Mentions (`@user`): league/commonmark extension, Ciconia
- Emoji (`:smile:`): league/commonmark extension
- Custom admonition containers (`::: warning`)

If you need any of these, `thephpleague/commonmark` is the most
actively maintained pure-PHP option with an extension system.

## Verifying conformance yourself

```bash
# Run just the spec conformance test
make test TESTS=tests/005_commonmark_spec.phpt

# Run the full suite
make test
```

To run against a newer `spec.txt`, drop it into
`tests/fixtures/commonmark-spec.txt`, update the baseline in
`tests/005_commonmark_spec.phpt`, and re-run. Any failures will be listed
by example number and source line.
