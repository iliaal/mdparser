# MdParser\Parser

`final class MdParser\Parser`

The main entry point. Holds a precomputed cmark options bitmask plus an
extension mask, then offers three rendering methods. Options are parsed
once at construction time so parse-time is pure native work with no
per-call option walking.

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

All three render methods can throw `MdParser\Exception` (final, extends
`\RuntimeException`) on:

- Allocation failure inside cmark (returns `null` parser or document)
- Renderer returning `null`

cmark is extremely tolerant of malformed markdown by design — any byte
sequence parses to something — so the exception path is narrow. You
don't need try/catch for normal rendering of well-formed or even
malformed input. Only OOM-style failures trigger it.

```php
try {
    $html = $parser->toHtml($source);
} catch (\MdParser\Exception $e) {
    // extremely rare: parser allocation or render failure
    error_log("mdparser failed: " . $e->getMessage());
}
```

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
