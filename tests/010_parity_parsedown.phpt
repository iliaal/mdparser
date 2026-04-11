--TEST--
parity: mdparser vs Parsedown on Parsedown's own fixture corpus
--SKIPIF--
<?php
if (!extension_loaded("mdparser")) print "skip mdparser not loaded";
if (!is_dir(__DIR__ . "/parity/parsedown/fixtures")) print "skip no parsedown fixtures";
?>
--FILE--
<?php

$dir = __DIR__ . "/parity/parsedown/fixtures";
$files = glob("$dir/*.md");
sort($files);

$parser = new MdParser\Parser(new MdParser\Options(
    tables: true,
    strikethrough: true,
    tasklist: true,
    autolink: true,
    tagfilter: false,
    unsafe: true,
));

$total = 0;
$match = 0;
$diverge = [];

foreach ($files as $md_path) {
    $name = basename($md_path, ".md");
    $html_path = "$dir/$name.html";
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
total: 64
match: 40
diverge: 24
diverging:
  - aligned_table
  - atx_heading
  - code_block
  - deeply_nested_list
  - em_strong
  - email
  - emphasis
  - escaping
  - fenced_code_block
  - html_entity
  - image_title
  - inline_link
  - inline_link_title
  - ordered_list
  - simple_table
  - strikethrough
  - tab-indented_code_block
  - table_inline_markdown
  - text_reference
  - url_autolinking
  - whitespace
  - xss_attribute_encoding
  - xss_bad_url
  - xss_text_encoding
