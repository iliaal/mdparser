--TEST--
headingAnchors does not reserve triple heading length for a valid ASCII slug
--EXTENSIONS--
mdparser
--INI--
memory_limit=128M
--FILE--
<?php

$text = str_repeat('a', 16 * 1024 * 1024);
$html = (new MdParser\Parser(new MdParser\Options(headingAnchors: true)))
    ->toHtml("# $text\n");

$ok = str_starts_with($html, '<h1 id="')
    && str_ends_with($html, "</h1>\n");
echo ($ok ? 'OK' : 'FAIL'), ": large valid heading renders under memory limit\n";

?>
--EXPECT--
OK: large valid heading renders under memory limit
