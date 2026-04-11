--TEST--
parity: mdparser vs cebe/markdown GFM fixture corpus
--SKIPIF--
<?php
if (!extension_loaded("mdparser")) print "skip mdparser not loaded";
if (!is_dir(__DIR__ . "/parity/cebe/fixtures")) print "skip no cebe fixtures";
?>
--FILE--
<?php

$dir = __DIR__ . "/parity/cebe/fixtures";
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
total: 15
match: 4
diverge: 11
diverging:
  - code_in_lists
  - dense-block-markers
  - dense-block-markers2
  - github-basics
  - github-code-in-numbered-list
  - github-sample
  - issue-38
  - issue-50
  - lists
  - tables
  - url
