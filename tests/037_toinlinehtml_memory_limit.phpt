--TEST--
toInlineHtml does not fatal on newline-heavy input under tight memory_limit
--EXTENSIONS--
mdparser
--INI--
memory_limit=128M
--FILE--
<?php
// pre-rewrite, toInlineHtml allocated `4 * src_len + 3` for the
// scratch buffer, which fatals at ~168 MB for 40 MB of newlines under
// memory_limit=128M even though the normalized buffer is empty. With
// the smart_str rewrite the peak allocation tracks the actual normalized
// size, not the worst-case multiplier.
$p = new MdParser\Parser;

$big = str_repeat("\n", 40 * 1024 * 1024);
$out = $p->toInlineHtml($big);
var_dump(strlen($out));

// Short input still works.
var_dump($p->toInlineHtml("short"));

// Mixed input, mostly text with a few newlines, normalized correctly.
var_dump($p->toInlineHtml("a\nb\nc"));
?>
--EXPECT--
int(0)
string(5) "short"
string(5) "a
b
c"
