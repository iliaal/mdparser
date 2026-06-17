--TEST--
validateUtf8 applies to toXml/toAst; entity-encoded fragment skips nofollow
--EXTENSIONS--
mdparser
--FILE--
<?php

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

$p = new MdParser\Parser();

// validateUtf8 (default true) must rewrite invalid bytes to U+FFFD on the
// XML and AST paths too, not just HTML (review P2).
$xml = $p->toXml("\xFF");
check("toXml replaces invalid byte with U+FFFD", str_contains($xml, "\xEF\xBF\xBD"));
check("toXml emits no raw 0xFF", !str_contains($xml, "\xFF"));

$ast = $p->toAst("bad \xFF byte");
$lit = $ast['children'][0]['children'][0]['literal'];
check("toAst replaces invalid byte with U+FFFD", str_contains($lit, "\xEF\xBF\xBD"));
check("toAst literal has no raw 0xFF", !str_contains($lit, "\xFF"));

// validateUtf8:false leaves the bytes untouched on every path.
$raw = new MdParser\Parser(new MdParser\Options(validateUtf8: false));
check("validateUtf8:false keeps raw byte in toXml", str_contains($raw->toXml("\xFF"), "\xFF"));
check("validateUtf8:false keeps raw byte in toHtml", str_contains($raw->toHtml("\xFF"), "\xFF"));

// Entity-encoded in-document fragment must skip the nofollow rel, decided on
// the DECODED href bytes (review P3).
$nf = new MdParser\Parser(new MdParser\Options(nofollowLinks: true));
$h = $nf->toHtml("[x](&#35;section)");
check("entity-encoded #fragment renders as #", str_contains($h, 'href="#section"'));
check("entity-encoded #fragment skips nofollow rel", !str_contains($h, 'rel='));

// A real external link in the same parser still gets the rel.
$h = $nf->toHtml("[y](https://example.com/)");
check("external link still gets nofollow rel",
    str_contains($h, 'rel="nofollow noopener noreferrer" href="https://example.com/"'));

?>
--EXPECT--
OK: toXml replaces invalid byte with U+FFFD
OK: toXml emits no raw 0xFF
OK: toAst replaces invalid byte with U+FFFD
OK: toAst literal has no raw 0xFF
OK: validateUtf8:false keeps raw byte in toXml
OK: validateUtf8:false keeps raw byte in toHtml
OK: entity-encoded #fragment renders as #
OK: entity-encoded #fragment skips nofollow rel
OK: external link still gets nofollow rel
