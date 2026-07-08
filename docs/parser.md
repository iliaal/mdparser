# MdParser\Parser

`final class MdParser\Parser`

The main entry point. Holds a precomputed md4c parser-flags bitmask
plus a renderer-options bitmask, then offers four rendering methods
(`toHtml`, `toXml`, `toAst`, `toInlineHtml`) and three static shortcuts
that render with the default Options. Options are translated to those
two bitmasks once at construction time, so each parse runs md4c with
the flags already resolved — no per-call option walking.

## Synopsis

```php
namespace MdParser;

final class Parser
{
    public readonly Options $options;

    public function __construct(?Options $options = null);

    public function toHtml(string $source): string;
    public function toXml(string $source): string;
    public function toAst(string $source): array;
    public function toInlineHtml(string $source): string;

    public static function html(string $source): string;
    public static function xml(string $source): string;
    public static function ast(string $source): array;
}
```

## Constructor

```php
public function __construct(?Options $options = null);
```

Creates a parser. If `$options` is `null`, an `Options` instance with
default values is created and attached. The options are translated to
md4c's parser-flags and renderer-options bitmasks once, then frozen.

The `options` property is readonly — it can't be reassigned after
construction. Create a new `Parser` if you need different options.

```php
$default = new Parser();
$strict  = new Parser(new Options(unsafe: false, smart: false));
$github  = new Parser(new Options(
    smart: true,
    footnotes: true,
));
```

## `toHtml(string $source): string`

Renders `$source` (CommonMark + GFM markdown) to HTML. Returns a
UTF-8 string including trailing newline.

```php
$parser = new Parser();

echo $parser->toHtml('# Hello');
// <h1>Hello</h1>

echo $parser->toHtml("a *b* c");
// <p>a <em>b</em> c</p>

echo $parser->toHtml("| a | b |\n|---|---|\n| 1 | 2 |\n");
// <table>
// <thead><tr><th>a</th><th>b</th></tr></thead>
// <tbody><tr><td>1</td><td>2</td></tr></tbody>
// </table>
```

### Safe mode (default)

By default, dangerous URL schemes (`javascript:`, `vbscript:`,
`data:text/html`, ...) are stripped to empty `href`/`src`, and raw HTML
is HTML-escaped so it renders as visible text rather than live markup.
This is the right default for rendering untrusted input.

```php
$parser = new Parser();
echo $parser->toHtml('[xss](javascript:alert(1))');
// <p><a href="">xss</a></p>

echo $parser->toHtml('<script>alert(1)</script>');
// &lt;script&gt;alert(1)&lt;/script&gt;
```

Safe mode uses a blocklist, not an allowlist: `javascript:`,
`vbscript:`, `file:`, and non-image `data:` URLs are stripped. Other
schemes, including custom application schemes, pass through. The only
`data:` URLs accepted are `data:image/{png,jpeg,gif,webp}` image
sources (`<img src>`), never link (`<a href>`) destinations. See
`docs/security.md` for the full policy and residual risk.

### Unsafe mode

Pass `Options(unsafe: true)` to disable the URL and raw-HTML
sanitization. Use this only for input you trust.

```php
$parser = new Parser(new Options(unsafe: true));
echo $parser->toHtml('<b>bold</b>');
// <p><b>bold</b></p>
```

The `tagfilter` option remains active even in unsafe mode (unless you
explicitly pass `tagfilter: false`), which escapes `<script>`,
`<iframe>`, and a handful of other dangerous tags as a defense layer.

## `toXml(string $source): string`

Renders the same parse result as CommonMark XML. Useful for piping into
XSLT or other external tooling, or for inspecting the document structure
as a serialized tree.

```php
$parser = new Parser();

echo $parser->toXml("# hi");
// <?xml version="1.0" encoding="UTF-8"?>
// <!DOCTYPE document SYSTEM "CommonMark.dtd">
// <document xmlns="http://commonmark.org/xml/1.0">
//   <heading level="1">
//     <text xml:space="preserve">hi</text>
//   </heading>
// </document>
```

The XML is CommonMark XML (the `commonmark.org/xml/1.0` schema shown
above), emitted by a streaming serializer in this extension. md4c
exposes no source positions, so the `<document>` tree carries no
`sourcepos` attributes.

## `toAst(string $source): array`

Parses `$source` and returns a nested PHP array representation of the
document tree. See `docs/ast.md` for the full shape — every node type
has a documented set of fields.

> **Security note: AST and XML output are not sanitized.** Link / image
> URLs and raw HTML literals are preserved byte-for-byte. The `unsafe`,
> `tagfilter`, and URL-scheme defenses operate only on the HTML paths
> (`toHtml`, `toInlineHtml`); `toAst` and `toXml` are structural views
> and apply none of them. A consumer that emits HTML from the AST or XML
> must apply its own URL scheme allowlist and HTML sanitization. For
> example, a `link` node's `url` (and the XML `<link destination>`) can
> hold `javascript:alert(1)`, and an `html_block` `literal` can hold a
> literal `<script>`.

```php
$parser = new Parser();
$ast = $parser->toAst("# Hi\n\n- one\n- two");

// [
//   'type' => 'document',
//   'children' => [
//     ['type' => 'heading', 'level' => 1, 'children' => [
//        ['type' => 'text', 'literal' => 'Hi'],
//     ]],
//     ['type' => 'list', 'list_type' => 'bullet', 'list_start' => 0,
//      'list_tight' => true, 'list_delim' => 'none', 'children' => [...]],
//   ],
// ]
```

This is the most powerful output mode — you can walk the tree yourself
to extract headings for a TOC, collect all links, transform or filter
nodes, or emit your own custom format.

## `toInlineHtml(string $source): string`

Renders `$source` as inline-only HTML: no `<p>` wrapper and no
block-level constructs. Block markers (`#`, `-`, `>`, `1.`) are emitted
as literal text rather than parsed, matching `Parsedown::line()` /
`cebe\markdown::parseParagraph()` semantics.

```php
echo $parser->toInlineHtml("a *b* `c`");
// a <em>b</em> <code>c</code>
```

This is a **snippet renderer**, tuned for short single-line inputs (chat
messages, table cells, display names). It runs a per-line normalization
pass that `toHtml` does not, so for large multi-line documents prefer
`toHtml`. `headingAnchors` is a no-op here (no headings are emitted);
`nofollowLinks` still applies.

## Error model

All render methods can throw `MdParser\Exception` (final, extends
`\RuntimeException`). The throw cases are deliberately narrow:

- **Wrapper validation guards.** Inputs over `MDPARSER_MAX_INPUT_SIZE`
  (256 MB) throw before md4c ever sees them. `toAst()` builds the node
  array on a fixed-depth stack and throws if nesting exceeds
  `MDPARSER_MAX_AST_DEPTH` (1000) — adversarial inputs like `> ` × 50000
  hit this. `toXml()` applies the same depth cap: its 2-spaces-per-level
  indentation makes a tiny deeply-nested input produce quadratic output,
  so it throws past `MDPARSER_MAX_AST_DEPTH` rather than amplify. `toHtml()`
  streams md4c's callbacks straight to output (no indentation, output
  linear in input) and is not depth-capped.
- **md4c / render null path.** The rare case where `md_parse()` reports
  failure, or the renderer returns `NULL`, raises an exception with the
  source length included for triage.
- **Reflection-bypassed Options.** Constructing a Parser with an
  `Options` object built via
  `ReflectionClass::newInstanceWithoutConstructor()` (uninitialized
  typed properties) throws before any parser state is cached.
- **Cloning / serializing.** Parser blocks both via Zend ACC flags;
  `clone $parser` and `serialize($parser)` raise the engine's standard
  Error.

md4c is extremely tolerant of malformed markdown by design — any byte
sequence parses to something — so normal rendering of well-formed or
malformed input does not need a try/catch. The exception path covers
hostile inputs and resource limits.

```php
try {
    $html = $parser->toHtml($source);
} catch (\MdParser\Exception $e) {
    // input-size cap, AST depth cap, or rare md4c/render null path
    error_log("mdparser failed: " . $e->getMessage());
}
```

### Memory: parse-side allocation is outside `memory_limit`

md4c allocates its own working memory with libc `malloc`/`free`, not
Zend MM. That memory is **not** counted against PHP's `memory_limit`
and does not show up in `memory_get_usage()`. The wrapper's own output
buffers — the rendered HTML/XML string, the AST arrays — use Zend MM
(`emalloc`/`efree`) and are accounted normally.

So `memory_limit` will not stop md4c mid-parse on a pathological input.
The guard for that is the 256 MB input-size cap
(`MDPARSER_MAX_INPUT_SIZE`), which throws `MdParser\Exception` before
md4c sees the source. If a libc allocation itself fails, the process
behaves the way any failed `malloc` does in the SAPI; the wrapper
surfaces a clean `MdParser\Exception` on md4c's failure return rather
than letting a partial parse through.

If you accept markdown from untrusted callers, cap the input length in
your application before handing it to the parser; that bounds the
parse-side footprint more directly than `memory_limit` can.

## Reusing parsers

Parsers are cheap to construct, but if you're rendering many documents
with the same options it's more efficient to reuse one instance — the
md4c flag bitmasks are computed once at construction and reused on every
`toHtml`/`toXml`/`toAst` call. Each call still runs an independent
`md_parse()` over its own input; md4c keeps no state between calls, so
reference-link definitions resolve within a single document and never
leak across separate calls.

```php
$parser = new Parser(new Options(smart: true));
foreach ($documents as $doc) {
    $out[$doc->id] = $parser->toHtml($doc->body);
}
```

Thread safety: each `Parser` instance is single-threaded, but different
instances in different threads (ZTS builds) are safe. md4c holds no
global state — every parse runs from the per-instance flag bitmasks and
the input you pass — so there is no shared registry to contend on.
