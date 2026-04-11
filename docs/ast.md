# AST output format

`Parser::toAst(string $source): array` returns a nested PHP array
representation of the parsed document tree. Each node is an associative
array with at minimum a `type` key. Container nodes (document, block
quote, list, etc.) have a `children` key holding an ordered array of
child nodes. Leaf nodes carry type-specific fields such as `literal`,
`url`, or `level`.

The AST is built in pure C by walking cmark's internal `cmark_node`
tree via its public accessor API (`cmark_node_get_literal`, etc.), so
there's no string parsing or reflection overhead — it's roughly as fast
as `toHtml`.

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

```php
['type' => 'table_header', 'children' => [/* table_cell nodes */]]
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

## Sourcepos

If the parser was constructed with `Options(sourcepos: true)`, every
node in the AST gains four additional keys:

```php
[
    'type' => 'heading',
    'start_line' => 1,
    'start_column' => 1,
    'end_line' => 1,
    'end_column' => 4,
    'level' => 1,
    'children' => [...],
]
```

Line and column numbers are 1-based, following cmark's convention.

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

- `custom_block` and `custom_inline` types — cmark-gfm supports these
  via syntax extensions, but mdparser doesn't expose custom extensions.
  These node types won't appear in output from the standard parser.
- `footnote_definition` and `footnote_reference` — these appear when
  `Options(footnotes: true)` is set, as block-level and inline nodes
  respectively. The shape is `['type' => 'footnote_reference',
  'children' => [['type' => 'text', 'literal' => '1']]]` etc.
- Extension-owned node types that cmark-gfm third-party extensions
  might add — only the built-in GFM types (table, strikethrough,
  tasklist) are reachable through mdparser's API.

## Performance

Building the AST is slightly slower than `toHtml` because we allocate
PHP arrays for every node. On typical GitHub-comment-sized documents
(~1-5 KB), the overhead is negligible (tens of microseconds). For very
large documents (100+ KB) consider using `toHtml` directly if you don't
need to walk the tree.
