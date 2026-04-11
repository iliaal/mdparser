<?php
/**
 * Walk the AST to extract a table of contents from a markdown
 * document. Demonstrates `toAst()` output and a recursive walker.
 *
 * Run it:
 *     php -d extension=mdparser.so examples/03-ast-toc.php
 */

declare(strict_types=1);

use MdParser\Parser;

if (!extension_loaded('mdparser')) {
    fwrite(STDERR, "mdparser extension not loaded. Use -d extension=mdparser.so\n");
    exit(1);
}

/** Recursively visit every node in the tree, bottom up. */
function walk(array $node, callable $visitor): void {
    $visitor($node);
    foreach ($node['children'] ?? [] as $child) {
        walk($child, $visitor);
    }
}

/** Concatenate all text descendants of a node into a single string. */
function nodeText(array $node): string {
    $out = '';
    walk($node, function (array $inner) use (&$out) {
        if ($inner['type'] === 'text' || $inner['type'] === 'code') {
            $out .= $inner['literal'];
        }
    });
    return $out;
}

$markdown = <<<MD
# mdparser

## Installation

Prose about installation.

### From source

More prose.

### From PECL

Even more prose.

## Usage

Prose about usage.

### toHtml

Details.

### toAst

Details.
MD;

$parser = new Parser();
$ast = $parser->toAst($markdown);

// Collect headings in document order.
$headings = [];
walk($ast, function (array $node) use (&$headings) {
    if ($node['type'] === 'heading') {
        $headings[] = [
            'level' => $node['level'],
            'text'  => nodeText($node),
        ];
    }
});

// Render as a nested bullet list.
echo "# Table of contents\n\n";
foreach ($headings as $h) {
    $indent = str_repeat('  ', max(0, $h['level'] - 1));
    $slug = strtolower(preg_replace('/[^a-z0-9]+/i', '-', $h['text']));
    $slug = trim($slug, '-');
    echo "{$indent}- [{$h['text']}](#{$slug})\n";
}
