--TEST--
toInlineHtml preserves literal U+200B from user input
--EXTENSIONS--
mdparser
--FILE--
<?php
$p = new MdParser\Parser;

// The internal line sentinel must not collide with user-provided bytes.
$zwsp = "\xE2\x80\x8B";
$src  = "a{$zwsp}b{$zwsp}c";
$out  = $p->toInlineHtml($src);
var_dump($out);

// ASCII text round-trips unchanged.
var_dump($p->toInlineHtml("hello world"));

// Block markers in inline mode render as literal text (the design point).
var_dump($p->toInlineHtml("# not a heading"));
var_dump($p->toInlineHtml("- not a list"));
var_dump($p->toInlineHtml("> not a quote"));
?>
--EXPECT--
string(9) "a​b​c"
string(11) "hello world"
string(15) "# not a heading"
string(12) "- not a list"
string(16) "&gt; not a quote"
