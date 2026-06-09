--TEST--
nofollowLinks: skip-region opener inside an href must not eat later transforms
--EXTENSIONS--
mdparser
--FILE--
<?php

/* After injecting rel into `<a href="`, apply_transforms used to
 * advance only past the "<a " bytes, leaving the cursor inside the
 * href value. A `<!--` or raw-text tag name embedded there (reachable
 * under unsafe; the comment form survives the default tagfilter) was
 * then probed as a region opener, and the closerless region ran to
 * end-of-document, stripping rel from every later link and id from
 * every later heading. The cursor now jumps the whole anchor tag,
 * mirroring compute_skip_list's quote-aware walk. */

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

$p = new MdParser\Parser(new MdParser\Options(unsafe: true, nofollowLinks: true));

$h = $p->toHtml("<a href=\"x<!--y\">l</a>\n\n[z](http://example.com/)\n");
check("comment opener in href: later link keeps rel",
    str_contains($h, '<a rel="nofollow noopener noreferrer" href="http://example.com/">z</a>'));
check("comment opener in href: decoy anchor itself got rel",
    str_contains($h, '<a rel="nofollow noopener noreferrer" href="x<!--y">l</a>'));

// Control: same document without the embedded opener.
$h = $p->toHtml("<a href=\"xy\">l</a>\n\n[z](http://example.com/)\n");
check("control: later link keeps rel",
    str_contains($h, '<a rel="nofollow noopener noreferrer" href="http://example.com/">z</a>'));

/* Raw-text tag name in the href needs tagfilter off to survive. */
$p = new MdParser\Parser(new MdParser\Options(unsafe: true, tagfilter: false, nofollowLinks: true));
$h = $p->toHtml("<a href=\"x<script>y\">l</a>\n\n[z](http://example.com/)\n");
check("script opener in href: later link keeps rel",
    str_contains($h, '<a rel="nofollow noopener noreferrer" href="http://example.com/">z</a>'));

/* Heading anchors after the decoy must survive too. */
$p = new MdParser\Parser(new MdParser\Options(unsafe: true, nofollowLinks: true, headingAnchors: true));
$h = $p->toHtml("<a href=\"x<!--y\">l</a>\n\n# foo\n");
check("comment opener in href: later heading keeps id",
    str_contains($h, '<h1 id="foo">foo</h1>'));

/* A real region after the decoy anchor is still honored. */
$h = $p->toHtml("<a href=\"x<!--y\">l</a>\n\n<!-- <a href=\"http://spam.example/\">s</a> -->\n\n[z](http://example.com/)\n");
check("real comment after decoy: body untouched",
    str_contains($h, '<!-- <a href="http://spam.example/">s</a> -->'));
check("real comment after decoy: later link keeps rel",
    str_contains($h, '<a rel="nofollow noopener noreferrer" href="http://example.com/">z</a>'));

?>
--EXPECT--
OK: comment opener in href: later link keeps rel
OK: comment opener in href: decoy anchor itself got rel
OK: control: later link keeps rel
OK: script opener in href: later link keeps rel
OK: comment opener in href: later heading keeps id
OK: real comment after decoy: body untouched
OK: real comment after decoy: later link keeps rel
