--TEST--
headingAnchors: an unfindable heading must not strip ids from later headings (review #1)
--EXTENSIONS--
mdparser
--FILE--
<?php

/* resolve_heading_offsets shares one search cursor across all headings
 * to enforce source order. A heading whose standalone fingerprint can't
 * be located (here: it contains an inline raw-text element, so the
 * fingerprint straddles a skip region carved from its own body) used to
 * run that cursor to end-of-document, leaving every later heading with
 * an empty search range and silently no id. The miss is now contained:
 * the cursor is restored so subsequent headings still resolve. */

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

$opts = new MdParser\Options(headingAnchors: true, unsafe: true, tagfilter: false);
$p = new MdParser\Parser($opts);

$h = $p->toHtml("# h1 <script>z</script>\n\n# alpha\n\n# beta\n");
check("alpha keeps id after unfindable heading", str_contains($h, '<h1 id="alpha">alpha</h1>'));
check("beta keeps id after unfindable heading",  str_contains($h, '<h1 id="beta">beta</h1>'));

// Several headings after the unfindable one all keep their ids.
$h = $p->toHtml("# x <style>q</style>\n\n# a\n\n# b\n\n# c\n\n# d\n");
check("cascade cleared: a,b,c,d all slugged",
    str_contains($h, 'id="a"') && str_contains($h, 'id="b"') &&
    str_contains($h, 'id="c"') && str_contains($h, 'id="d"'));

// Control: a plain leading heading, everything still slugs.
$h = $p->toHtml("# h0\n\n# alpha\n\n# beta\n");
check("control: all slugged",
    str_contains($h, 'id="h0"') && str_contains($h, 'id="alpha"') && str_contains($h, 'id="beta"'));

?>
--EXPECT--
OK: alpha keeps id after unfindable heading
OK: beta keeps id after unfindable heading
OK: cascade cleared: a,b,c,d all slugged
OK: control: all slugged
