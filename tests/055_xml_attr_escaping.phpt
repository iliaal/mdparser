--TEST--
toXml: link/image attribute escaping (attr_plain fast path vs entity slow path)
--EXTENSIONS--
mdparser
--FILE--
<?php
function ok(string $l, bool $c): void { echo ($c ? "OK" : "FAIL"), ": $l\n"; }

$p = new MdParser\Parser();

// Plain destination with a literal & (single NORMAL substring -> attr_plain
// fast path): the bytes are XML-escaped, so & becomes &amp;.
$x = $p->toXml("[x](http://e.com/?a=1&b=2)");
ok("fast path: literal & in url escaped",
   str_contains($x, 'destination="http://e.com/?a=1&amp;b=2"'));

// Entity-encoded & in the destination (NORMAL+ENTITY substrings -> decode slow
// path): decoded to & then XML-escaped, byte-identical to the fast-path result.
$x = $p->toXml("[x](http://e.com/?a=1&amp;b=2)");
ok("slow path: &amp; in url decoded then escaped (single-encoded)",
   str_contains($x, 'destination="http://e.com/?a=1&amp;b=2"'));

// A named entity in the destination decodes to its UTF-8 codepoint.
$x = $p->toXml("[x](http://e.com/&copy;path)");
ok("slow path: named entity in url decoded to UTF-8",
   str_contains($x, 'destination="http://e.com/©path"'));

// Title with XML-special chars (fast path) is escaped.
$x = $p->toXml('[x](http://e "a<b>c")');
ok("fast path: <> in title escaped",
   str_contains($x, 'title="a&lt;b&gt;c"'));

// Title mixing a literal special char and an entity (slow path).
$x = $p->toXml('[x](http://e "a<b&amp;c")');
ok("slow path: mixed title decoded and escaped",
   str_contains($x, 'title="a&lt;b&amp;c"'));

// Image src routes through the same fast path.
$x = $p->toXml('![a](http://e.com/i.png?w=1&h=2)');
ok("fast path: image src & escaped",
   str_contains($x, 'destination="http://e.com/i.png?w=1&amp;h=2"'));
?>
--EXPECT--
OK: fast path: literal & in url escaped
OK: slow path: &amp; in url decoded then escaped (single-encoded)
OK: slow path: named entity in url decoded to UTF-8
OK: fast path: <> in title escaped
OK: slow path: mixed title decoded and escaped
OK: fast path: image src & escaped
