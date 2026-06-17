# Spec coverage

mdparser targets **CommonMark 0.31**. The backend is
[md4c](https://github.com/mity/md4c), which implements CommonMark 0.31
natively, so there is no version gap to bridge and no patches layered on
top of the parser.

The conformance test lives at `tests/005_commonmark_spec.phpt` and reads
every example from `tests/fixtures/commonmark-spec.txt` (the 0.31
`spec.txt`). It runs as part of the standard `make test` and pins md4c's
conformance, so any change that moves the baseline shows up in a diff.

## Current baseline

The suite parses all 652 spec examples and pins the result at 649 pass,
3 fail. The three failures are enumerated by example number and source
line directly in the test's `--EXPECT--` block. They are md4c's known
deviations from the reference rendering, not mdparser bugs; the test
treats them as a fixed baseline so a surprise improvement is as visible
as a regression.

The spec test runs with `githubPreLang: false` because the spec examples
use the `<pre><code class="language-X">` form, while mdparser's default
is `<pre lang="X"><code>` (matching GitHub's rendering). Both forms are
valid; the difference is presentation, not compliance. The spec test
measures spec compliance, not GitHub parity. It also enables `unsafe`
and disables the GFM extensions so the input matches plain CommonMark.

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
| Spoilers | `spoilers` | `>!text!<` |
| LaTeX math | `latexMath` | `$inline$` and `$$block$$` |
| Wiki links | `wikiLinks` | `[[target]]` |

These are neither CommonMark nor GFM. Turn them on only when your input
expects them. See `docs/options.md` for behavior and edge cases.

## HTML postprocess passes

mdparser ships two HTML postprocess passes that the parser itself does
not produce. They rewrite the rendered HTML:

| Feature | Option | Behavior |
|---|---|---|
| Heading permalinks / anchors | `headingAnchors: true` | Every `<hN>` gains a GitHub-style slug `id`; collisions deduped with `-1`, `-2`, ... |
| External link postprocessing | `nofollowLinks: true` | Every `<a href="...">` gets `rel="nofollow noopener noreferrer"`; in-document fragment anchors are skipped |

Both default to `false`. See `docs/options.md` for behavior, edge cases,
and the documented `unsafe: true` byte-collision limitation on heading
anchors.

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
