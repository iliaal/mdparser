--TEST--
toXml does not reserve output capacity proportional to sparse input
--EXTENSIONS--
mdparser
--INI--
memory_limit=96M
--FILE--
<?php

$markdown = str_repeat("\n", 40 * 1024 * 1024);
$xml = (new MdParser\Parser())->toXml($markdown);
$ok = str_contains($xml, '<document xmlns="http://commonmark.org/xml/1.0">')
    && str_ends_with($xml, "</document>\n");
echo ($ok ? 'OK' : 'FAIL'), ": sparse XML renders under memory limit\n";

?>
--EXPECT--
OK: sparse XML renders under memory limit
