--TEST--
toInlineHtml strips literal U+200B from user input (documented behavior)
--EXTENSIONS--
mdparser
--FILE--
<?php
$p = new MdParser\Parser;

// AD-801: U+200B (zero-width space) in user input is collateral damage of
// the per-line ZWSP sentinel mechanism in toInlineHtml. Pin the documented
// data-loss so a future change to the strip loop breaks the test
// deliberately rather than silently corrupting user input.
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
string(3) "abc"
string(11) "hello world"
string(15) "# not a heading"
string(12) "- not a list"
string(16) "&gt; not a quote"
