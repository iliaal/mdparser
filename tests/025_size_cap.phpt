--TEST--
size cap: inputs over MDPARSER_MAX_INPUT_SIZE (256 MB) throw MdParser\Exception
--EXTENSIONS--
mdparser
--SKIPIF--
<?php
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

foreach (["toHtml", "toInlineHtml", "toXml", "toAst"] as $method) {
    try {
        $parser->$method($over);
        echo "FAIL: $method did not throw on oversized input\n";
    } catch (MdParser\Exception $e) {
        $ok = str_contains($e->getMessage(), "exceeds maximum")
            && str_contains($e->getMessage(), (string) strlen($over));
        echo ($ok ? "OK" : "FAIL"), ": $method threw MdParser\\Exception with size message\n";
    }
}

// Free the big buffer before building the next one. unset($e) matters as
// much as unset($over): each caught exception's trace still references
// the 257 MB argument, keeping it alive past unset($over) alone.
unset($over, $e);

// The boundary itself is proven below, not trusted: 256 MB exactly
// (MDPARSER_MAX_INPUT_SIZE == 256 * 1024 * 1024) is accepted by all
// four entries, while the 257 MB input above throws. The input is one
// short heading padded with blank lines: exactly-cap length with tiny
// output, so the four-way accept proof needs no more headroom than the
// 257 MB throw case above and stays inside the 768 MB limit. The 1 MB
// render stays as a cheap sanity case for the normal path.
$exact = "# hi\n" . str_repeat("\n", 256 * $mb - 5);
echo "built boundary input: ", strlen($exact), " bytes\n";
foreach (["toHtml", "toInlineHtml", "toXml", "toAst"] as $method) {
    try {
        $result = $parser->$method($exact);
        unset($result);
        echo "OK: $method accepted exactly-256MB input\n";
    } catch (MdParser\Exception $e) {
        echo "FAIL: $method threw on exactly-256MB input: ", $e->getMessage(), "\n";
    }
}
unset($exact, $e);

$under = str_repeat("word ", intdiv($mb, 5)); // ~1 MB of plain text
$html = $parser->toHtml($under);
echo "1 MB input renders: ", (str_starts_with($html, "<p>word ") ? "ok" : "FAIL"), "\n";

?>
--EXPECTF--
built oversized input: 269484032 bytes
OK: toHtml threw MdParser\Exception with size message
OK: toInlineHtml threw MdParser\Exception with size message
OK: toXml threw MdParser\Exception with size message
OK: toAst threw MdParser\Exception with size message
built boundary input: 268435456 bytes
OK: toHtml accepted exactly-256MB input
OK: toInlineHtml accepted exactly-256MB input
OK: toXml accepted exactly-256MB input
OK: toAst accepted exactly-256MB input
1 MB input renders: ok
