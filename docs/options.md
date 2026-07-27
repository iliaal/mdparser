# MdParser\Options

`final readonly class MdParser\Options`

Holds 30 bool toggles that control parser and renderer behavior:
core parser options, GFM extension toggles, HTML output flags (heading
anchors, nofollow), and md4c dialect extensions. All fields are readonly after
construction, and the class is `final` so it can't be subclassed.
Use named arguments to set only the fields you care about.

## Defaults

```php
new Options(
    // Core parser options
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

    // HTML output flags (heading anchors, nofollow)
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
    spoilers: false,
    latexMath: false,
    wikiLinks: false,
    admonitions: false,
);
```

The defaults are tuned for rendering untrusted input as GitHub-style
markdown: safe URL filtering, tag filter, UTF-8 validation, GFM
extensions enabled, GitHub-style code block class attribute, no heading
anchors or nofollow.

## Core parser options

### `sourcepos: bool = false`

**Accepted but inert.** The md4c backend does not expose source
positions, so this option has no effect — no `data-sourcepos` attributes
are emitted regardless of its value. It is retained for API
compatibility and may be removed in a future major version.

```php
echo (new Parser(new Options(sourcepos: true)))->toHtml("# hi\n");
// <h1>hi</h1>   (sourcepos has no effect)
```

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

When `true`, soft line breaks inside paragraphs become literal spaces
instead of passing through as newlines. If both `hardbreaks` and
`nobreaks` are true, `hardbreaks` wins because md4c emits hard line
break callbacks, not soft break callbacks.

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
HTML-escaped (rendered as visible text). When `true`, raw HTML and all
URL schemes pass through verbatim.

Use `true` only for input you fully trust. See `docs/security.md` for
the threat model.

### `validateUtf8: bool = true`

When `true` (default), invalid UTF-8 byte sequences are replaced with
U+FFFD (�) before parsing. When `false`, invalid bytes are passed
through to the parser and can appear unchanged in output.

Leave on unless you know your input is pre-validated UTF-8.

### `githubPreLang: bool = true`

**Accepted but inert.** The md4c backend always renders a fenced code
block with a language as `<pre><code class="language-X">` (the
CommonMark spec form); this option does not change that. Retained for
API compatibility.

```php
echo (new Parser())->toHtml("```php\necho 1;\n```");
// <pre><code class="language-php">echo 1;
// </code></pre>
```

### `liberalHtmlTag: bool = false`

**Accepted but inert.** Had no md4c equivalent after the backend
migration; the value is ignored. Retained for API compatibility.

### `footnotes: bool = false`

Enables the `[^ref]` / `[^ref]: body` footnote syntax (not part of
CommonMark core, an md4c extension via `MD_FLAG_FOOTNOTES`).

```php
$md = "A claim[^1] follows.\n\n[^1]: See source.\n";
echo (new Parser(new Options(footnotes: true)))->toHtml($md);
// <p>A claim<sup><a href="#fn-1" id="fnref-1-1">1</a></sup> follows.</p>
// <section class="footnotes">
// <ol>
// <li id="fn-1">
// See source.<a href="#fnref-1-1" class="footnote-backref">↩</a>
// </li>
// </ol>
// </section>
```

Definition bodies are emitted tight — no `<p>` wrapper — with the
backref anchor appended directly to the body text.

When `false`, `[^1]` and `[^1]: ...` parse as literal text.

### `strikethroughDoubleTilde: bool = false`

**Accepted but inert.** md4c's strikethrough does not expose a
single-vs-double-tilde toggle; the value is ignored. Retained for API
compatibility.

### `tablePreferStyleAttributes: bool = false`

**Accepted but inert.** Table cell alignment always renders as
`align="..."`; this option does not switch it to `style`. Retained for
API compatibility.

### `fullInfoString: bool = false`

**Accepted but inert.** The full info string is not exposed as a
`data-meta` attribute; the value is ignored. Retained for API
compatibility.

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

## HTML output flags (heading anchors, nofollow)

These two flags are applied in-stream by the HTML renderer as md4c emits
its events, not as a separate string pass over the finished HTML. They
act only on nodes md4c parses from the Markdown source; raw HTML written
directly in the document (under `unsafe: true`) is passed through
verbatim and never rewritten. They affect `toHtml()` (and
`toInlineHtml()` where applicable). XML and AST output are unaffected.
The static `Parser::html()` / `Parser::xml()` shortcuts use the module
defaults and do not apply either transform.

### `headingAnchors: bool = false`

When `true`, every Markdown heading gets an `id` attribute holding a
GitHub-style slug derived from the heading's text. Slugs lowercase
ASCII, replace whitespace runs with a single `-`, drop other ASCII
punctuation, preserve UTF-8 multibyte bytes, and dedupe collisions
with `-1`, `-2`, ... (up to 100,000 collisions for a single base slug;
beyond that the heading falls back to no id). Headings whose text
slugifies to nothing (pure punctuation), and entity-only headings that
decode to nothing, emit `<hN>` with no id rather than `id=""`. A raw
`<hN>` block written directly in the source (only possible under
`unsafe: true, tagfilter: false`) is raw HTML, not a parsed heading
node, so it is emitted untouched and gets no id.

```php
echo (new Parser(new Options(headingAnchors: true)))->toHtml("# Hello World\n");
// <h1 id="hello-world">Hello World</h1>

echo (new Parser(new Options(headingAnchors: true)))->toHtml("# Foo\n## Foo\n");
// <h1 id="foo">Foo</h1>
// <h2 id="foo-1">Foo</h2>
```

Because ids are attached as md4c emits each heading node, a raw HTML
heading and a later Markdown heading with the same text no longer
collide: the raw one (`<h1>same</h1>`) stays plain and the Markdown one
(`# same`) gets `id="same"`. This is the in-stream behavior pinned by
`tests/030_anchor_unsafe_collision.phpt`. (Earlier versions ran the
anchor pass as a byte search over the finished HTML, where the raw block
could absorb the id; the in-stream renderer resolves that.)

### `nofollowLinks: bool = false`

When `true`, every link md4c parses from the Markdown source gets
`rel="nofollow noopener noreferrer"`. Applies to inline links, reference
links, and autolinks across `toHtml()` and `toInlineHtml()`. In-document
fragment anchors (`href="#..."`, including footnote references and
backrefs) are intentionally skipped. Anchors inside fenced or inline
code never become links, so they are untouched. A raw `<a href="...">`
written directly in the source (under `unsafe: true`) is raw HTML, not a
parsed link node, so it is emitted verbatim and is not rewritten.

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
with all of them off. Each renders as standard HTML (a semantic tag, or
an element carrying a `class` hook), so no custom elements or scripts are
introduced. In `toXml()` / `toAst()` they surface as the node types
`underline`, `highlight`, `superscript`, `subscript`, `spoiler`,
`latex_math`, `latex_math_display`, `wikilink`, and the block-level
`admonition`.

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

### `spoilers: bool = false`

When `true`, `||text||` renders as `<span class="spoiler">text</span>`.
Style or script the `.spoiler` class downstream to hide/reveal the
content.

### `latexMath: bool = false`

When `true`, `$x$` renders as `<span class="math">x</span>` and `$$x$$`
as `<span class="math display">x</span>`. The TeX source is emitted
verbatim (HTML-escaped, never smart-punctuated) inside the span; wire up
KaTeX or MathJax against the `.math` class to typeset it.

### `wikiLinks: bool = false`

When `true`, `[[target]]` renders as
`<a class="wikilink" href="target">target</a>`, and `[[target|label]]`
uses `label` as the link text. The target runs through the **same URL
scheme filter as a normal link** (decode → check → emit), so a
`[[javascript:...]]` target is neutralized to an empty `href` in safe
mode. `nofollowLinks` applies to wiki links as it does to other links.

### `admonitions: bool = false`

When `true`, GitHub-style alert blocks are recognized:

```
> [!NOTE]
> Body text.
```

The block type is one of `note`, `tip`, `important`, `warning`, or
`caution`. It renders as
`<div class="admonition-note"><p class="admonition-title">note</p>…</div>`
(the type in the `class` and the title); style the `.admonition-*`
classes downstream. In `toXml()` it is `<admonition type="note">`; in
`toAst()` it is an `admonition` node carrying an `admonition_type`
field. With the option off, `> [!NOTE]` stays a plain blockquote.

## Patterns

### GitHub comment rendering (GFM)

```php
Options::github();
```

### Strict spec compliance

```php
new Options(
    unsafe: true,           // spec tests exercise raw HTML
    tables: false,          // strip GFM extras for pure spec
    strikethrough: false,
    tasklist: false,
    autolink: false,
    tagfilter: false,
    // githubPreLang is accepted-but-inert; omit it
);
```

### Editor with live preview

```php
new Options(
    smart: false,           // keep authored text byte-exact
    hardbreaks: false,
);
```

Note: md4c exposes no source positions, so mdparser cannot emit
`data-sourcepos` for editor click-to-jump (the `sourcepos` option is
inert). If you need source mapping, a different parser is the better
fit.

### Rendering trusted internal docs

```php
new Options(
    unsafe: true,           // raw HTML OK in our own content
    smart: true,
    footnotes: true,
);
```
