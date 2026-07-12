--TEST--
toXml streams a large code-block literal without retaining a second copy
--EXTENSIONS--
mdparser
--INI--
memory_limit=48M
--FILE--
<?php

$markdown = "```\n" . str_repeat('x', 16 * 1024 * 1024) . "\n```\n";
$xml = (new MdParser\Parser())->toXml($markdown);

$ok = str_starts_with($xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
    && str_contains($xml, '<document xmlns="http://commonmark.org/xml/1.0">')
    && str_contains($xml, '<code_block xml:space="preserve">')
    && str_ends_with($xml, "</code_block>\n</document>\n");
echo ($ok ? 'OK' : 'FAIL'), ": large XML literal renders under memory limit\n";

?>
--EXPECT--
OK: large XML literal renders under memory limit
