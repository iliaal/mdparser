--TEST--
toXml ordered-list open tag with large start numbers is well-formed (no buffer overflow)
--EXTENSIONS--
mdparser
--FILE--
<?php

/* Regression: the ordered-list <list> open tag was formatted into a fixed
 * 64-byte stack buffer, then appended using snprintf's return value (the
 * would-be length) rather than the truncated length. A start number wide
 * enough to fill the buffer truncated the tag, embedded a NUL, and (for the
 * widest starts md4c parses, 9 digits) read past the buffer into stack memory.
 * The renderer now streams the tag piece-by-piece with no fixed buffer. */

$p = new MdParser\Parser();

function check(string $label, string $xml, string $needle): void {
    $nul = strpos($xml, "\0") !== false;
    $has = strpos($xml, $needle) !== false;
    echo (!$nul && $has ? "OK" : "FAIL"), ": $label\n";
    if ($nul) echo "  NUL byte present in output\n";
    if (!$has) echo "  missing: $needle\n";
}

// 4-digit start: exactly the size that used to truncate + embed a NUL.
check("4-digit start well-formed",
      $p->toXml("1000. a\n\n1001. b"),
      '<list type="ordered" start="1000" delim="period" tight="false">');

// 9-digit start: the widest md4c parses; used to read past the 64-byte buffer.
check("9-digit start well-formed",
      $p->toXml("999999999. c"),
      '<list type="ordered" start="999999999" delim="period" tight="true">');

// Paren delimiter path.
check("paren delimiter well-formed",
      $p->toXml("42) x"),
      '<list type="ordered" start="42" delim="paren" tight="true">');

// The output must parse as XML end-to-end.
$xml = $p->toXml("999999999. c");
$doc = new DOMDocument();
echo ($doc->loadXML($xml) ? "OK" : "FAIL"), ": 9-digit output is DOM-parseable\n";

?>
--EXPECT--
OK: 4-digit start well-formed
OK: 9-digit start well-formed
OK: paren delimiter well-formed
OK: 9-digit output is DOM-parseable
