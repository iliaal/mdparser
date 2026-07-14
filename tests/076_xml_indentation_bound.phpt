--TEST--
toXml bounds indentation amplification without truncating the document tree
--EXTENSIONS--
mdparser
--INI--
memory_limit=64M
--FILE--
<?php

$chain = str_repeat('> ', 990) . "x\n\n";
$source = str_repeat($chain, 15);
$xml = (new MdParser\Parser())->toXml($source);

echo (substr_count($xml, '<block_quote>') === 990 * 15 ? "OK" : "FAIL"),
    ": full tree retained\n";
echo (strlen($xml) < strlen($source) * 100 ? "OK" : "FAIL"),
    ": output amplification bounded\n";

?>
--EXPECT--
OK: full tree retained
OK: output amplification bounded
