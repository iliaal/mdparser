--TEST--
toInlineHtml: sourcepos option must not leak the <p> wrapper into inline output
--EXTENSIONS--
mdparser
--FILE--
<?php

/* toInlineHtml strips the paragraph wrapper by matching an exact
 * "<p>\xE2\x80\x8B" prefix. Historically Options(sourcepos: true) made the
 * backend emit "<p data-sourcepos=...>", the prefix match failed, and the
 * wrapper leaked into the output. Under md4c sourcepos is inert (no source
 * positions exist), so this is now a guard that the option never perturbs
 * inline output. */

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

$p = new MdParser\Parser(new MdParser\Options(sourcepos: true));

$r = $p->toInlineHtml("hello");
check("no <p wrapper",       !str_contains($r, '<p'));
check("no data-sourcepos",   !str_contains($r, 'data-sourcepos'));
check("exact inline body",   $r === "hello");

$r = $p->toInlineHtml("a *b* c");
check("emphasis preserved",        str_contains($r, '<em>b</em>'));
check("formatted body wrapper-free", $r === "a <em>b</em> c");

// Parity: a sourcepos parser and a plain parser produce identical inline HTML.
$plain = new MdParser\Parser();
check("matches non-sourcepos parser",
    $plain->toInlineHtml("a *b* c") === $p->toInlineHtml("a *b* c"));

?>
--EXPECT--
OK: no <p wrapper
OK: no data-sourcepos
OK: exact inline body
OK: emphasis preserved
OK: formatted body wrapper-free
OK: matches non-sourcepos parser
