--TEST--
toInlineHtml keeps retained multiline input below the duplicate-buffer memory bound
--EXTENSIONS--
mdparser
--INI--
memory_limit=64M
--FILE--
<?php

$source = str_repeat("a\n", 5 * 1024 * 1024);
$html = (new MdParser\Parser())->toInlineHtml($source);

echo (strlen($html) === strlen($source) - 1 ? "OK" : "FAIL"),
    ": multiline inline output preserved\n";

?>
--EXPECT--
OK: multiline inline output preserved
