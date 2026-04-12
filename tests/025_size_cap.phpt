--TEST--
size cap: inputs over MDPARSER_MAX_INPUT_SIZE (256 MB) throw MdParser\Exception
--SKIPIF--
<?php
if (!extension_loaded("mdparser")) print "skip";
// 257 MB input + PHP overhead needs headroom; skip on tight memory_limit hosts.
if (PHP_INT_SIZE < 8) print "skip 32-bit unable to allocate 257 MB string";
?>
--INI--
memory_limit=768M
--FILE--
<?php

$parser = new MdParser\Parser();

// Construct a 257 MB input: 1 MB over the hard cap. str_repeat allocates
// efficiently as a single zend_string, and on 64-bit PHP with a 768 MB
// memory_limit there's room for this plus the PHP runtime.
$mb = 1024 * 1024;
$over = str_repeat("a", 257 * $mb);
echo "built oversized input: ", strlen($over), " bytes\n";

foreach (["toHtml", "toXml", "toAst"] as $method) {
    try {
        $parser->$method($over);
        echo "FAIL: $method did not throw on oversized input\n";
    } catch (MdParser\Exception $e) {
        $ok = str_contains($e->getMessage(), "exceeds maximum")
            && str_contains($e->getMessage(), (string) strlen($over));
        echo ($ok ? "OK" : "FAIL"), ": $method threw MdParser\\Exception with size message\n";
    }
}

// Free the big buffer before building the next one.
unset($over);

// Just under the cap should still work. 256 MB exactly is the boundary
// (MDPARSER_MAX_INPUT_SIZE == 256 * 1024 * 1024), so 256 MB - 1 byte is
// valid. We only verify that the wrapper accepts the size; the resulting
// render time on a 256 MB input is ~seconds, so we use a 1 MB input here
// as a cheap sanity case and trust the cap boundary is a constant
// comparison.
$under = str_repeat("word ", intdiv($mb, 5)); // ~1 MB of plain text
$html = $parser->toHtml($under);
echo "1 MB input renders: ", (str_starts_with($html, "<p>word ") ? "ok" : "FAIL"), "\n";

?>
--EXPECTF--
built oversized input: 269484032 bytes
OK: toHtml threw MdParser\Exception with size message
OK: toXml threw MdParser\Exception with size message
OK: toAst threw MdParser\Exception with size message
1 MB input renders: ok
