--TEST--
parity: mdparser vs michelf/php-markdown Gruber 1.0.3 fixtures
--SKIPIF--
<?php
if (!extension_loaded("mdparser")) print "skip mdparser not loaded";
if (!is_dir(__DIR__ . "/parity/michelf/fixtures")) print "skip no michelf fixtures";
?>
--FILE--
<?php

$dir = __DIR__ . "/parity/michelf/fixtures";
$files = glob("$dir/*.text");
sort($files);

/* Gruber 1.0.3 predates CommonMark and predates GFM. Disable every
 * extension so we compare like-for-like against the original Markdown
 * semantics. */
$parser = new MdParser\Parser(new MdParser\Options(
    tables: false,
    strikethrough: false,
    tasklist: false,
    autolink: false,
    tagfilter: false,
    unsafe: true,
));

$total = 0;
$match = 0;
$diverge = [];

foreach ($files as $md_path) {
    $name = basename($md_path, ".text");
    $html_path = "$dir/$name.xhtml";
    if (!file_exists($html_path)) {
        $html_path = "$dir/$name.html";
    }
    if (!file_exists($html_path)) continue;

    $total++;
    $md = file_get_contents($md_path);
    $expected = rtrim(file_get_contents($html_path), "\n");
    $actual = rtrim($parser->toHtml($md), "\n");

    if ($actual === $expected) {
        $match++;
    } else {
        $diverge[] = $name;
    }
}

printf("total: %d\n", $total);
printf("match: %d\n", $match);
printf("diverge: %d\n", count($diverge));
echo "diverging:\n";
foreach ($diverge as $d) echo "  - $d\n";
?>
--EXPECT--
total: 23
match: 1
diverge: 22
diverging:
  - Amps and angle encoding
  - Auto links
  - Backslash escapes
  - Blockquotes with code blocks
  - Code Blocks
  - Code Spans
  - Hard-wrapped paragraphs with list-like lines
  - Horizontal rules
  - Images
  - Inline HTML (Advanced)
  - Inline HTML (Simple)
  - Inline HTML comments
  - Links, inline style
  - Links, reference style
  - Links, shortcut references
  - Literal quotes in titles
  - Markdown Documentation - Basics
  - Markdown Documentation - Syntax
  - Nested blockquotes
  - Ordered and unordered lists
  - Strong and em together
  - Tabs
