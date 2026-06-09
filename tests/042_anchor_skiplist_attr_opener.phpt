--TEST--
headingAnchors: skip-region opener inside a quoted attribute must not eat later ids
--EXTENSIONS--
mdparser
--FILE--
<?php

/* compute_skip_list walks the rendered document looking for raw-text /
 * comment / CDATA openers. A `<!--` or `<script>` embedded inside a
 * quoted attribute value of an ordinary tag (reachable under unsafe)
 * is not a real region opener, but a scanner that doesn't step over
 * tag interiors the way apply_transforms does treats it as one. The
 * bogus region has no closer, extends to end-of-document, and every
 * heading after it silently loses its id. */

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

/* Comment opener in an attribute. The default tagfilter does not touch
 * <div> or comment syntax, so this needs only unsafe: true. */
$p = new MdParser\Parser(new MdParser\Options(headingAnchors: true, unsafe: true));

$h = $p->toHtml("<div title=\"<!--\">d</div>\n\n# foo\n\n# bar\n");
check("comment opener in attr: foo keeps id", str_contains($h, '<h1 id="foo">foo</h1>'));
check("comment opener in attr: bar keeps id", str_contains($h, '<h1 id="bar">bar</h1>'));

// Control: same document without the embedded opener.
$h = $p->toHtml("<div title=\"x\">d</div>\n\n# foo\n\n# bar\n");
check("control: foo keeps id", str_contains($h, '<h1 id="foo">foo</h1>'));
check("control: bar keeps id", str_contains($h, '<h1 id="bar">bar</h1>'));

/* Raw-text tag name in an attribute needs tagfilter off to survive. */
$p = new MdParser\Parser(new MdParser\Options(headingAnchors: true, unsafe: true, tagfilter: false));

$h = $p->toHtml("<div title=\"<script>\">d</div>\n\n# foo\n\n# bar\n");
check("script opener in attr: foo keeps id", str_contains($h, '<h1 id="foo">foo</h1>'));
check("script opener in attr: bar keeps id", str_contains($h, '<h1 id="bar">bar</h1>'));

/* A real skip region after the decoy must still be honored: the <h1>
 * inside the real <script> body must not hijack the heading's slug. */
$h = $p->toHtml("<div title=\"<script>\">d</div>\n\n<script><h1>foo</h1></script>\n\n# foo\n");
check("real region after decoy: heading keeps id", str_contains($h, '<h1 id="foo">foo</h1>'));
check("real region after decoy: script body untouched", str_contains($h, '<script><h1>foo</h1></script>'));

?>
--EXPECT--
OK: comment opener in attr: foo keeps id
OK: comment opener in attr: bar keeps id
OK: control: foo keeps id
OK: control: bar keeps id
OK: script opener in attr: foo keeps id
OK: script opener in attr: bar keeps id
OK: real region after decoy: heading keeps id
OK: real region after decoy: script body untouched
