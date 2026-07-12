--TEST--
toAst transfers a large code-block literal without duplicating it
--EXTENSIONS--
mdparser
--INI--
memory_limit=40M
--FILE--
<?php

$literalLength = 16 * 1024 * 1024 + 1;
$markdown = "```\n" . str_repeat('x', 16 * 1024 * 1024) . "\n```\n";
$ast = (new MdParser\Parser())->toAst($markdown);

echo (strlen($ast['children'][0]['literal']) === $literalLength ? 'OK' : 'FAIL'),
    ": large AST literal renders under memory limit\n";

?>
--EXPECT--
OK: large AST literal renders under memory limit
