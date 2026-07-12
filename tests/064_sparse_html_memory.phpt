--TEST--
toHtml does not reserve output capacity proportional to sparse input
--EXTENSIONS--
mdparser
--INI--
memory_limit=64M
--FILE--
<?php

$markdown = str_repeat("\n", 40 * 1024 * 1024);
$html = (new MdParser\Parser())->toHtml($markdown);
echo ($html === '' ? 'OK' : 'FAIL'), ": sparse HTML renders under memory limit\n";

?>
--EXPECT--
OK: sparse HTML renders under memory limit
