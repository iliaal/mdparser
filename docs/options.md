# MdParser\Options

`final readonly class MdParser\Options`

Holds 26 bool toggles that control parser and renderer behavior:
core parser options, GFM extension toggles, HTML postprocess flags,
and md4c dialect extensions. All fields are readonly after
construction, and the class is `final` so it can't be subclassed.
Use named arguments to set only the fields you care about.

## Defaults

```php
new Options(
    // Core cmark options
    sourcepos: false,
    hardbreaks: false,
    nobreaks: false,
    smart: false,
    unsafe: false,
    validateUtf8: true,
    githubPreLang: true,
    liberalHtmlTag: false,
    footnotes: false,
    strikethroughDoubleTilde: false,
    tablePreferStyleAttributes: false,
    fullInfoString: false,

    // GFM extension toggles
    tables: true,
    strikethrough: true,
    tasklist: true,
    autolink: true,
    tagfilter: true,

    // HTML postprocess flags
    headingAnchors: false,
    nofollowLinks: false,

    // Parser behavior toggles
    noIndentedCodeBlocks: false,
    permissiveAtxHeadings: false,
    collapseWhitespace: false,

    // md4c dialect extensions (non-CommonMark, non-GFM)
    underline: false,
    highlight: false,
    superscript: false,
    subscript: false,
);
```

The defaults are tuned for rendering untrusted input as GitHub-style
markdown: safe URL filtering, tag filter, UTF-8 validation, GFM
extensions enabled, GitHub-style code block class attribute, no
postprocess passes.

## Core cmark options

### `sourcepos: bool = false`

When `true`, every rendered HTML element gets a `data-sourcepos` attribute
pointing at the source line/column range it came from. Useful for
round-tripping edits or building editor integrations.

```php
echo (new Parser(new Options(sourcepos: true)))->toHtml("# hi\n");
// <h1 data-sourcepos="1:1-1:4">hi</h1>
```

Adds per-node overhead; leave off unless you need the positions.

### `hardbreaks: bool = false`

When `true`, a single newline inside a paragraph becomes a `<br />`.
When `false` (default, spec-compliant), single newlines are soft breaks
rendered as a space.

```php
$md = "line one\nline two";
echo (new Parser(new Options(hardbreaks: false)))->toHtml($md);
// <p>line one
// line two</p>

echo (new Parser(new Options(hardbreaks: true)))->toHtml($md);
// <p>line one<br />
// line two</p>
```

### `nobreaks: bool = false`

When `true`, single newlines inside paragraphs become literal spaces
instead of passing through as newlines. Mutually exclusive with
`hardbreaks`.

### `smart: bool = false`

Smart punctuation. Converts:

- `--` → en-dash (`–`)
- `---` → em-dash (`—`)
- `...` → ellipsis (`…`)
- `"quoted"` → curly double quotes (`"quoted"`)
- `'quoted'` → curly single quotes (`'quoted'`)
- Apostrophes (`it's`) → curly right quote (`it's`)

```php
echo (new Parser(new Options(smart: true)))
    ->toHtml("--dashes-- and \"quoted\"");
// <p>–dashes– and "quoted"</p>
```

### `unsafe: bool = false`

**Security-relevant.** When `false` (default), dangerous URL schemes in
links and images are stripped to empty, and raw HTML in markdown is
replaced with `<!-- raw HTML omitted -->`. When `true`, raw HTML and
all URL schemes pass through verbatim.

Use `true` only for input you fully trust. See `docs/security.md` for
the threat model.

### `validateUtf8: bool = true`

When `true` (default), invalid UTF-8 byte sequences are replaced with
U+FFFD (�) before parsing. When `false`, invalid bytes are passed
through to the parser, which will eventually reject them with an
exception.

Leave on unless you know your input is pre-validated UTF-8.

### `githubPreLang: bool = true`

Controls the HTML form of fenced code blocks with a language info
string.

```php
$md = "```php\necho 1;\n```";

// githubPreLang: true (default) — matches GitHub's rendering
echo (new Parser())->toHtml($md);
// <pre lang="php"><code>echo 1;
// </code></pre>

// githubPreLang: false — matches the CommonMark spec form
echo (new Parser(new Options(githubPreLang: false)))->toHtml($md);
// <pre><code class="language-php">echo 1;
// </code></pre>
```

Both forms are valid; the difference is presentation. The CommonMark
spec examples use the second form, so the mdparser spec conformance
test passes `false`. For real output consumed by GitHub-style
stylesheets, leave it on.

### `liberalHtmlTag: bool = false`

When `true`, the HTML tag scanner is more permissive — accepts malformed
tags that a strict parser would reject. Off by default.

### `footnotes: bool = false`

Enables the `[^ref]` / `[^ref]: body` footnote syntax (not part of
CommonMark core, inherited from cmark-gfm's footnote extension).

```php
$md = "A claim[^1] follows.\n\n[^1]: See source.\n";
echo (new Parser(new Options(footnotes: true)))->toHtml($md);
// <p>A claim<sup class="footnote-ref">...</sup> follows.</p>
// <section class="footnotes" data-footnotes>
//   <ol>
//     <li id="fn-1"><p>See source. <a href="#fnref-1" ...>↩</a></p></li>
//   </ol>
// </section>
```

When `false`, `[^1]` and `[^1]: ...` parse as literal text.

### `strikethroughDoubleTilde: bool = false`

By default, single tildes are allowed to delimit strikethrough
(`~strike~`). When `true`, only double tildes are allowed (`~~strike~~`).
Use for stricter GFM compliance.

### `tablePreferStyleAttributes: bool = false`

When `true`, table cell alignment uses `style="text-align: left"`
attributes instead of the older `align="left"`. Relevant if your
downstream HTML consumer is picky.

### `fullInfoString: bool = false`

When `true`, the full code fence info string (everything after the
language tag) is preserved in a `data-meta` attribute. Default drops it.

## GFM extension toggles

Each of these enables or disables a specific GFM feature. All default
to `true` because the dominant use case for mdparser is GitHub-style
rendering.

### `tables: bool = true`

GFM pipe tables (`| a | b |`...).

### `strikethrough: bool = true`

`~~strike~~` → `<del>strike</del>`.

### `tasklist: bool = true`

`- [ ] todo` and `- [x] done` become `<li><input type="checkbox" .../>
todo</li>`.

### `autolink: bool = true`

Bare URLs like `https://example.com` become `<a>` links without
requiring `<angle bracket>` wrapping.

### `tagfilter: bool = true`

GitHub's tag filter: escapes `<title>`, `<textarea>`, `<style>`,
`<xmp>`, `<iframe>`, `<noembed>`, `<noframes>`, `<script>`, and
`<plaintext>` tags even when raw HTML is otherwise allowed. This is a
defense-in-depth layer when `unsafe: true` — the filter still prevents
the most dangerous tags from passing through.

## HTML postprocess flags

These two flags trigger string-level transforms applied *after* cmark
finishes rendering. They affect `toHtml()` (and `toInlineHtml()` where
applicable). XML and AST output are unaffected. The static
`Parser::html()` / `Parser::xml()` shortcuts use the module defaults
and do not apply either transform.

### `headingAnchors: bool = false`

When `true`, every rendered `<hN>` gets an `id` attribute holding a
GitHub-style slug derived from the heading's text. Slugs lowercase
ASCII, replace whitespace runs with a single `-`, drop other ASCII
punctuation, preserve UTF-8 multibyte bytes, and dedupe collisions
with `-1`, `-2`, ... Headings whose text slugifies to nothing (pure
punctuation) emit `<hN>` with no id rather than `id=""`. Coexists
with `sourcepos`: the `id` lands before `data-sourcepos`.

```php
echo (new Parser(new Options(headingAnchors: true)))->toHtml("# Hello World\n");
// <h1 id="hello-world">Hello World</h1>

echo (new Parser(new Options(headingAnchors: true)))->toHtml("# Foo\n## Foo\n");
// <h1 id="foo">Foo</h1>
// <h2 id="foo-1">Foo</h2>
```

Known limitation under `unsafe: true, tagfilter: false`: a raw HTML
heading whose rendered bytes match a later Markdown heading (e.g.
`<h1>same</h1>` followed by `# same`) absorbs the `id`, leaving the
real Markdown heading without one. Pinned in
`tests/030_anchor_unsafe_collision.phpt`. A renderer-level fix is
planned. For trusted-input pipelines that need stable heading ids,
avoid raw HTML headings whose text matches Markdown ones.

### `nofollowLinks: bool = false`

When `true`, every emitted `<a href="...">` gets
`rel="nofollow noopener noreferrer"` injected. Applies to inline
links, reference links, and autolinks across `toHtml()` and
`toInlineHtml()`. In-document fragment anchors (`href="#..."`,
including footnote references and backrefs) are intentionally
skipped. Anchors inside fenced or inline code are untouched because
cmark escapes them before the postprocess runs. Raw `<script>` /
`<style>` regions under `unsafe: true` are emitted verbatim so
anchor-shaped substrings inside JavaScript or CSS are not corrupted.

```php
echo (new Parser(new Options(nofollowLinks: true)))
    ->toHtml("[ext](https://example.com)");
// <p><a rel="nofollow noopener noreferrer" href="https://example.com">ext</a></p>
```

## Parser behavior toggles

These map directly to md4c parser flags. They change how the source is
parsed (not how nodes render), add no new HTML tags, and default off so
the standard CommonMark + GFM parse is unaffected.

### `noIndentedCodeBlocks: bool = false`

When `true`, 4-space-indented blocks are parsed as regular text instead
of code blocks. Only fenced code (```` ``` ````) produces `<pre><code>`.
Useful for content where leading indentation is common prose, not code.

### `permissiveAtxHeadings: bool = false`

When `true`, ATX headings no longer require a space after the `#`, so
`###hi` becomes `<h3>hi</h3>`. Standard CommonMark requires the space
and renders `###hi` as a paragraph.

### `collapseWhitespace: bool = false`

When `true`, runs of non-trivial whitespace inside normal text collapse
to a single space (`a      b` → `a b`). Does not affect code spans or
code blocks.

## Dialect extensions

These are md4c extensions outside the CommonMark and GFM specs. They are
opt-in and default off; mdparser's spec-conformance contract holds only
with all four off. Each renders as a standard semantic HTML tag, so no
custom elements or scripts are introduced. In `toXml()` / `toAst()` they
surface as the node types `underline`, `highlight`, `superscript`, and
`subscript`.

### `underline: bool = false`

When `true`, `_text_` renders as `<u>text</u>`. Note this **disables `_`
as an emphasis delimiter** (md4c semantics); use `*text*` and `**text**`
for emphasis and strong when underline is on. `__text__` becomes nested
underline (`<u><u>text</u></u>`), not strong emphasis.

### `highlight: bool = false`

When `true`, `==text==` renders as `<mark>text</mark>`.

### `superscript: bool = false`

When `true`, `^text^` renders as `<sup>text</sup>`.

### `subscript: bool = false`

When `true`, `~text~` renders as `<sub>text</sub>`. This coexists with
GFM strikethrough: `~~text~~` still renders as `<del>`, while a single
`~` pair is subscript.

## Patterns

### GitHub comment rendering (GFM)

```php
new Options(); // defaults are already right for this
```

### Strict spec compliance

```php
new Options(
    unsafe: true,           // spec tests exercise raw HTML
    githubPreLang: false,   // spec uses <pre><code class="language-X">
    tables: false,          // strip GFM extras for pure spec
    strikethrough: false,
    tasklist: false,
    autolink: false,
    tagfilter: false,
);
```

### Editor with live preview

```php
new Options(
    sourcepos: true,        // enable click-to-jump
    smart: false,           // keep authored text byte-exact
    hardbreaks: false,
);
```

### Rendering trusted internal docs

```php
new Options(
    unsafe: true,           // raw HTML OK in our own content
    smart: true,
    footnotes: true,
);
```
