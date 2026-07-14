--TEST--
toAst bounds memory for entity-fragmented text by coalescing adjacent nodes
--EXTENSIONS--
mdparser
--INI--
memory_limit=32M
--FILE--
<?php

$ast = (new MdParser\Parser())->toAst(str_repeat('a&amp;', 50000));
$children = $ast['children'][0]['children'];

echo (count($children) === 1 ? "OK" : "FAIL"), ": one text node\n";
echo ($children[0]['literal'] === str_repeat('a&', 50000) ? "OK" : "FAIL"),
    ": literal preserved\n";

?>
--EXPECT--
OK: one text node
OK: literal preserved
