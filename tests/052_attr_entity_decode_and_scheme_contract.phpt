--TEST--
review: XML/AST decode attribute entities; URL scheme filter is a blocklist
--EXTENSIONS--
mdparser
--FILE--
<?php

function ok(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

$p = new MdParser\Parser();

// --- Attribute entity decoding (review #1) ---------------------------
// md4c hands attributes out entity-undecoded. toXml must decode then
// XML-escape ONCE (no &amp;amp;); toAst must store the decoded bytes
// (matching the legacy cmark AST contract).
$md = "[x](http://a.com?a=1&amp;b=2)";

$xml = $p->toXml($md);
ok("XML link destination single-encoded",
   str_contains($xml, 'destination="http://a.com?a=1&amp;b=2"'));
ok("XML link destination NOT double-encoded",
   !str_contains($xml, '&amp;amp;'));

$ast = $p->toAst($md);
ok("AST link url is entity-decoded",
   $ast['children'][0]['children'][0]['url'] === 'http://a.com?a=1&b=2');

// HTML path must be unchanged (it already decoded correctly).
ok("HTML href unchanged",
   str_contains($p->toHtml($md), 'href="http://a.com?a=1&amp;b=2"'));

// HTML URL filtering, AST, and XML must all see the same entity-decoded
// destination bytes. If any path forgets to decode before its own policy,
// this can become a live javascript: URL or a double-encoded structural value.
$evil = "[x](javascript&colon;alert&lpar;1&rpar;)";
ok("HTML href filter sees entity-decoded scheme",
   str_contains($p->toHtml($evil), '<a href="">x</a>'));
ok("XML destination decodes entities before escaping",
   str_contains($p->toXml($evil), 'destination="javascript:alert(1)"'));
ok("AST url decodes entities",
   $p->toAst($evil)['children'][0]['children'][0]['url'] === 'javascript:alert(1)');

// Heading slugs share the same raw entity decoding; entity and literal text
// produce the same id.
ok("HTML heading slug decodes named and numeric entities",
   str_contains(
       (new MdParser\Parser(new MdParser\Options(headingAnchors: true)))
           ->toHtml("# A&amp;B &#x43;\n"),
       'id="ab-c"'
   ));

// Code-fence info string: same decode path.
$cb = "```c&amp;c\nx\n```";
ok("XML code_block info single-encoded",
   str_contains($p->toXml($cb), 'info="c&amp;c"') && !str_contains($p->toXml($cb), '&amp;amp;'));
ok("AST code_block info decoded",
   $p->toAst($cb)['children'][0]['info'] === 'c&c');

// Wikilink destination: same path.
$w = new MdParser\Parser(new MdParser\Options(wikiLinks: true));
ok("XML wikilink destination single-encoded",
   str_contains($w->toXml("[[A&amp;B]]"), 'destination="A&amp;B"'));

// --- URL scheme filter is a BLOCKLIST, not an allowlist (FS-001) -----
// Pin the contract: the four dangerous schemes are neutralized to an
// empty href; every other scheme (known-safe OR unknown) passes through.
$blocked = ['javascript:alert(1)', 'vbscript:x', 'file:///etc/passwd', 'data:text/html,<b>x</b>'];
foreach ($blocked as $u) {
    ok("blocked: $u", str_contains($p->toHtml("[a]($u)"), 'href=""'));
}

$passed = ['https://e.com', 'mailto:a@b.c', 'tel:+1', 'ftp://h/f',
           'livescript:x', 'intent://scan', 'custom-app:open', 'view-source:http://e'];
foreach ($passed as $u) {
    ok("passes through: $u", str_contains($p->toHtml("[a]($u)"), 'href="' . $u . '"'));
}

?>
--EXPECT--
OK: XML link destination single-encoded
OK: XML link destination NOT double-encoded
OK: AST link url is entity-decoded
OK: HTML href unchanged
OK: HTML href filter sees entity-decoded scheme
OK: XML destination decodes entities before escaping
OK: AST url decodes entities
OK: HTML heading slug decodes named and numeric entities
OK: XML code_block info single-encoded
OK: AST code_block info decoded
OK: XML wikilink destination single-encoded
OK: blocked: javascript:alert(1)
OK: blocked: vbscript:x
OK: blocked: file:///etc/passwd
OK: blocked: data:text/html,<b>x</b>
OK: passes through: https://e.com
OK: passes through: mailto:a@b.c
OK: passes through: tel:+1
OK: passes through: ftp://h/f
OK: passes through: livescript:x
OK: passes through: intent://scan
OK: passes through: custom-app:open
OK: passes through: view-source:http://e
