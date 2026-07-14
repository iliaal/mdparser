--TEST--
toXml emits XML 1.0-safe characters and round-tripping attribute whitespace
--EXTENSIONS--
mdparser
--FILE--
<?php

$p = new MdParser\Parser();

function ok(string $label, bool $condition): void {
    echo ($condition ? "OK" : "FAIL"), ": $label\n";
}

$xml = $p->toXml("\u{FFFE}\u{FFFF}");
ok("forbidden XML scalars become replacements",
    !str_contains($xml, "\u{FFFE}")
    && !str_contains($xml, "\u{FFFF}")
    && substr_count($xml, "\u{FFFD}") === 2);

$xml = $p->toXml('[x](target "a&#9;b&#10;c&#13;d")');
ok("attribute whitespace uses character references",
    str_contains($xml, 'title="a&#x9;b&#xA;c&#xD;d"'));

?>
--EXPECT--
OK: forbidden XML scalars become replacements
OK: attribute whitespace uses character references
