--TEST--
toInlineHtml preserves line-leading inline delimiters and literal U+200B
--EXTENSIONS--
mdparser
--FILE--
<?php

$p = new MdParser\Parser();

function eq(string $label, string $got, string $want): void {
    echo ($got === $want ? "OK" : "FAIL"), ": $label\n";
}

eq("underscore emphasis", $p->toInlineHtml('_em_'), '<em>em</em>');
eq("underscore strong", $p->toInlineHtml('__strong__'), '<strong>strong</strong>');
eq("strikethrough", $p->toInlineHtml('~~del~~'), '<del>del</del>');
eq("literal U+200B", $p->toInlineHtml("a\u{200B}b"), "a\u{200B}b");
eq("sentinel-looking source", $p->toInlineHtml(';_em_'), ';<em>em</em>');
eq("block starts remain literal",
    $p->toInlineHtml("# h\n- item\n> quote\n~~~\ncode\n~~~"),
    "# h\n- item\n&gt; quote\n~~~\ncode\n~~~");

$highlight = new MdParser\Parser(new MdParser\Options(highlight: true));
eq("highlight", $highlight->toInlineHtml('==mark=='), '<mark>mark</mark>');

?>
--EXPECT--
OK: underscore emphasis
OK: underscore strong
OK: strikethrough
OK: literal U+200B
OK: sentinel-looking source
OK: block starts remain literal
OK: highlight
