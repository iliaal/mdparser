# AST output format

`Parser::toAst(string $source): array` returns a nested PHP array
representation of the parsed document tree. Each node is an associative
array with at minimum a `type` key. Container nodes (document, block
quote, list, etc.) add a `children` key lazily when their first child is
emitted; an empty container omits the key. When present, `children` holds an
ordered array of child nodes. Leaf nodes carry type-specific fields such as
`literal`, `url`, or `level`.

The AST is assembled in C directly from md4c's parser callbacks: as
md4c emits enter/leave block, enter/leave span, and text events, the
builder pushes and pops nodes on a zval stack and fills in each node's
fields as the event arrives. There is no intermediate document tree and
no string re-parsing, so building the array is roughly as fast as
`toHtml`. Adjacent normal/entity/NUL text callbacks are coalesced into one
`text` node so parser callback fragmentation does not multiply PHP array
overhead.

> **Security: the AST is unsanitized.** `html_block` / `html_inline`
> `literal` fields are preserved byte-for-byte. Link / image `url` and
> `title` fields are **entity-decoded** (so `&amp;` becomes `&`) but are
> **not** scheme-filtered. `Options::unsafe`, `Options::tagfilter`, and
> the URL-scheme defenses apply only to the HTML paths (`toHtml` /
> `toInlineHtml`), NOT to `toAst` or `toXml`. If you build HTML out of
> the AST yourself, you own the sanitization: apply a URL scheme
> allowlist (`http`, `https`, `mailto`, `tel`, …) before emitting
> `href`, and run HTML through a sanitizer before emitting raw
> `html_block` / `html_inline` literal text.
>
> Examples of what survives in the AST:
> - `[click](javascript:alert(1))` → `link` node with
>   `url => "javascript:alert(1)"`.
> - `[x](http://a.com?a=1&amp;b=2)` → `url => "http://a.com?a=1&b=2"`.
> - `<script>alert(1)</script>` → `html_block` with the literal text.
> - `<b onclick="x">y</b>` → an `html_inline` carrying the attribute
>   verbatim.

## Top-level structure

```php
[
    'type' => 'document',
    'children' => [
        /* top-level blocks */
    ],
]
```

## Block node types

### `document`

Root container. Only appears once, at the top level.

An empty document is `['type' => 'document']`; the `children` key shown below
appears when the document contains at least one block.

```php
['type' => 'document', 'children' => [...]]
```

### `heading`

```php
[
    'type' => 'heading',
    'level' => 1,           // 1..6
    'children' => [
        ['type' => 'text', 'literal' => 'Hello'],
    ],
]
```

### `paragraph`

```php
['type' => 'paragraph', 'children' => [...]]
```

### `block_quote`

```php
['type' => 'block_quote', 'children' => [...]]
```

### `list`

```php
[
    'type' => 'list',
    'list_type' => 'bullet',     // 'bullet' | 'ordered' | 'none'
    'list_start' => 1,           // start number for ordered lists, 0 for bullet
    'list_tight' => true,
    'list_delim' => 'period',    // 'period' | 'paren' | 'none'
    'children' => [
        ['type' => 'item', 'children' => [...]],
        ['type' => 'item', 'children' => [...]],
    ],
]
```

### `item`

List item. Container for whatever blocks appear inside it (usually a
paragraph, sometimes nested lists or code blocks).

```php
['type' => 'item', 'children' => [...]]
```

### `code_block`

Fenced or indented code block.

```php
[
    'type' => 'code_block',
    'info' => 'php',             // fence info string, empty for indented blocks
    'literal' => "echo 1;\n",    // code text including trailing newline
]
```

### `html_block`

Raw HTML block.

```php
['type' => 'html_block', 'literal' => "<div>...</div>\n"]
```

### `thematic_break`

Horizontal rule (`---`, `***`, `___`). No children, no fields.

```php
['type' => 'thematic_break']
```

## Inline node types

### `text`

Plain text content.

```php
['type' => 'text', 'literal' => 'hello']
```

### `code`

Inline code span (`` `code` ``).

```php
['type' => 'code', 'literal' => 'code']
```

### `emph`

Emphasis (`*em*`, `_em_`) → `<em>`.

```php
['type' => 'emph', 'children' => [['type' => 'text', 'literal' => 'em']]]
```

### `strong`

Strong emphasis (`**strong**`, `__strong__`) → `<strong>`.

```php
['type' => 'strong', 'children' => [...]]
```

### `link`

```php
[
    'type' => 'link',
    'url' => 'https://example.com',
    'title' => 'optional title',      // empty string if no title
    'children' => [
        ['type' => 'text', 'literal' => 'link text'],
    ],
]
```

### `image`

Same shape as `link` but with `type => 'image'`. The `children` array
contains the alt-text nodes.

### `html_inline`

Inline raw HTML.

```php
['type' => 'html_inline', 'literal' => '<br>']
```

### `softbreak`, `linebreak`

Line breaks. No fields, no children.

```php
['type' => 'softbreak']
['type' => 'linebreak']
```

## GFM extension node types

### `table`

```php
[
    'type' => 'table',
    'alignments' => ['left', 'right', 'center', 'none'],  // one per column
    'children' => [
        /* first child is table_header, rest are table_row */
    ],
]
```

### `table_header`, `table_row`

Both carry `is_header` (`true` on `table_header`, `false` on body
`table_row`). THEAD/TBODY wrappers are flattened: header and body rows
are direct children of `table` (unlike `toXml()`, which emits
`table_header` / `table_body` section elements).

```php
['type' => 'table_header', 'is_header' => true,  'children' => [/* table_cell nodes */]]
['type' => 'table_row',    'is_header' => false, 'children' => [...]]
```

### `table_cell`

Content of a single cell.

```php
['type' => 'table_cell', 'children' => [/* inline nodes */]]
```

### `strikethrough`

```php
['type' => 'strikethrough', 'children' => [['type' => 'text', 'literal' => 'x']]]
```

### `tasklist`

A GFM task list item. Appears as a child of a `list` node, in place of
a regular `item`.

```php
[
    'type' => 'tasklist',
    'checked' => false,         // or true for `- [x]`
    'children' => [
        ['type' => 'paragraph', 'children' => [
            ['type' => 'text', 'literal' => 'todo'],
        ]],
    ],
]
```

## Dialect extension node types

md4c supports several non-GFM dialect extensions. Each is off by default
and surfaces as its own node type only when you enable the matching
option. None of these are part of CommonMark or GFM.

### `underline`

Appears with `Options(underline: true)`. Inline span for `_text_` when
underscore emphasis is reinterpreted as underline.

```php
['type' => 'underline', 'children' => [['type' => 'text', 'literal' => 'x']]]
```

### `highlight`

Appears with `Options(highlight: true)`. Inline span for `==text==`.

```php
['type' => 'highlight', 'children' => [['type' => 'text', 'literal' => 'x']]]
```

### `superscript`, `subscript`

Appear with `Options(superscript: true)` / `Options(subscript: true)`,
for `^text^` and `~text~`.

```php
['type' => 'superscript', 'children' => [...]]
['type' => 'subscript',   'children' => [...]]
```

### `spoiler`

Appears with `Options(spoilers: true)`, for `||text||`.

```php
['type' => 'spoiler', 'children' => [...]]
```

### `latex_math`, `latex_math_display`

Appear with `Options(latexMath: true)`. Inline `$...$` math is
`latex_math`; display `$$...$$` math is `latex_math_display`. The math
source is carried as a `text` child.

```php
['type' => 'latex_math',         'children' => [['type' => 'text', 'literal' => 'a^2']]]
['type' => 'latex_math_display', 'children' => [...]]
```

### `wikilink`

Appears with `Options(wikiLinks: true)`, for `[[target]]` and
`[[target|label]]`. The `url` field holds the link target; the
`children` hold the label.

```php
[
    'type' => 'wikilink',
    'url' => 'Target Page',
    'children' => [['type' => 'text', 'literal' => 'label']],
]
```

### `footnote_reference`, `footnote_definition`

Appear with `Options(footnotes: true)`. Both nodes carry the numeric
footnote id in `literal`; `footnote_definition` is a block node and
`footnote_reference` is inline. The AST does **not** emit a wrapping
`footnote_section` node — definitions are direct children of
`document` (or their enclosing block). `toXml()` *does* wrap them in
`<footnote_section>`; treat the two structural formats as dual
contracts, not isomorphic serializations.

```php
['type' => 'footnote_reference', 'literal' => '1']
['type' => 'footnote_definition', 'literal' => '1', 'children' => [...]]
```

## Source positions

The AST carries no source positions. md4c exposes no line or column
data, so nodes have no `start_line` / `start_column` / `end_line` /
`end_column` keys. The `Options(sourcepos: true)` flag is accepted for
API compatibility but has no effect on any output path.

## Walking the tree

For most use cases a simple recursive function does the job:

```php
function walk(array $node, callable $visitor): void {
    $visitor($node);
    foreach ($node['children'] ?? [] as $child) {
        walk($child, $visitor);
    }
}

// Extract all headings for a table of contents.
$parser = new MdParser\Parser();
$ast = $parser->toAst($document);
$headings = [];
walk($ast, function (array $node) use (&$headings) {
    if ($node['type'] === 'heading') {
        $text = '';
        walk($node, function ($inner) use (&$text) {
            if ($inner['type'] === 'text') $text .= $inner['literal'];
        });
        $headings[] = ['level' => $node['level'], 'text' => $text];
    }
});
```

See `examples/03-ast-toc.php` for a complete version.

## What's NOT in the AST

- `custom_block` / `custom_inline` types — md4c has no third-party
  extension node system, so there are no caller-defined node types and
  none of these appear in the output.
- Any node type beyond what's documented here. The reachable set is
  fixed: the CommonMark block and inline types, the GFM types (table,
  strikethrough, tasklist), and the md4c dialect extension types above
  when their options are enabled.

## Performance

Building the AST is slightly slower than `toHtml` because we allocate
PHP arrays for every node. On typical GitHub-comment-sized documents
(~1-5 KB), the overhead is negligible (tens of microseconds). For very
large documents (100+ KB) consider using `toHtml` directly if you don't
need to walk the tree.
