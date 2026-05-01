# MdParser\Parser

`final class MdParser\Parser`

The main entry point. Holds a precomputed cmark options bitmask plus an
extension mask, then offers four rendering methods (`toHtml`, `toXml`,
`toAst`, `toInlineHtml`) and three static shortcuts that render with
the default Options. The cached cmark_parser is reused across calls
on the same instance. Options are parsed once at construction time so
parse-time is pure native work with no per-call option walking.

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
cmark's internal bitmask/extension-mask once, then frozen.

The `options` property is readonly — it can't be reassigned after
construction. Create a new `Parser` if you need different options.

```php
$default = new Parser();
$strict  = new Parser(new Options(unsafe: false, smart: false));
$github  = new Parser(new Options(
    smart: true,
    sourcepos: true,
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
is replaced with `<!-- raw HTML omitted -->`. This is the right default
for rendering untrusted input.

```php
$parser = new Parser();
echo $parser->toHtml('[xss](javascript:alert(1))');
// <p><a href="">xss</a></p>

echo $parser->toHtml('<script>alert(1)</script>');
// <!-- raw HTML omitted -->
```

Safe mode lets through `http:`, `https:`, `mailto:`, `tel:`, `ftp:`,
and `data:image/{png,jpeg,gif,webp}` URLs. See `docs/security.md` for
the full list and reasoning.

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
XSLT or other external tooling, or for capturing sourcepos.

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

The XML format is cmark's native tree serialization, matching what
`cmark-gfm --to xml` would emit.

## `toAst(string $source): array`

Parses `$source` and returns a nested PHP array representation of the
document tree. See `docs/ast.md` for the full shape — every node type
has a documented set of fields.

> **Security note: AST output is not sanitized.** Link / image URLs and
> raw HTML literals are preserved byte-for-byte. The `unsafe`,
> `tagfilter`, and URL-scheme defenses operate on the rendering paths
> (`toHtml`, `toXml`, `toInlineHtml`); `toAst` is a structural view
> and applies none of them. A consumer that emits HTML from the AST
> must apply its own URL scheme allowlist and HTML sanitization. For
> example, a `link` node's `url` field can hold `javascript:alert(1)`,
> and an `html_block` node's `literal` can hold a literal `<script>`.

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

## Error model

All render methods can throw `MdParser\Exception` (final, extends
`\RuntimeException`). The throw cases are deliberately narrow:

- **Wrapper validation guards.** Inputs over `MDPARSER_MAX_INPUT_SIZE`
  (256 MB) throw before cmark ever sees them. `toAst()` walks the
  document recursively and throws if nesting exceeds
  `MDPARSER_MAX_AST_DEPTH` (1000) — adversarial inputs like `> ` × 50000
  hit this. `toHtml()` and `toXml()` use cmark's iterative renderer
  and are unaffected by AST depth.
- **cmark null returns.** Parser construction, `cmark_parser_finish`,
  the renderer, or the postprocess pass returning `NULL` raises an
  exception with the source length included for triage.
- **Reflection-bypassed Options.** Constructing a Parser with an
  `Options` object built via
  `ReflectionClass::newInstanceWithoutConstructor()` (uninitialized
  typed properties) throws before any parser state is cached.
- **Cloning / serializing.** Parser blocks both via Zend ACC flags;
  `clone $parser` and `serialize($parser)` raise the engine's standard
  Error.

cmark is extremely tolerant of malformed markdown by design — any byte
sequence parses to something — so normal rendering of well-formed or
malformed input does not need a try/catch. The exception path covers
hostile inputs and resource limits.

```php
try {
    $html = $parser->toHtml($source);
} catch (\MdParser\Exception $e) {
    // input-size cap, AST depth cap, or rare cmark/render null path
    error_log("mdparser failed: " . $e->getMessage());
}
```

### Memory exhaustion is a fatal, not an exception

cmark allocations now route through Zend MM (`ecalloc` / `erealloc` /
`efree`), so cmark-side memory is accounted by `memory_limit` and
visible to `memory_get_usage()`. Hitting the limit triggers PHP's
standard `Allowed memory size of X bytes exhausted` fatal error, not
`MdParser\Exception`. This is the normal Zend MM bailout path and is
**not catchable** with `try/catch`. The previous behavior (cmark's
default allocator calling `abort()` on OOM and tearing down the
process) is gone.

If you need to defend against runaway markdown allocating beyond a
budget, the right tool is PHP's `memory_limit` — set it appropriately
for the request and let the engine bail.

## Reusing parsers

Parsers are cheap to construct, but if you're rendering many documents
with the same options it's more efficient to reuse one instance — the
cmark options bitmask is computed once at construction and reused on
every `toHtml`/`toXml`/`toAst` call.

```php
$parser = new Parser(new Options(smart: true));
foreach ($documents as $doc) {
    $out[$doc->id] = $parser->toHtml($doc->body);
}
```

Thread safety: each `Parser` instance is single-threaded, but different
instances in different threads (ZTS builds) are safe. The cmark-gfm
extension registry is process-global and initialized once at module
startup.
