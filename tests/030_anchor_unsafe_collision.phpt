--TEST--
headingAnchors: byte-fingerprint collision when raw HTML matches a Markdown heading
--EXTENSIONS--
mdparser
--FILE--
<?php

/* The heading-anchor postprocess locates each Markdown heading by
 * searching the rendered HTML for the heading's standalone byte
 * rendering. Raw HTML inside the document (only possible under
 * unsafe:true with tagfilter:false) can produce identical bytes to a
 * later Markdown heading. The first match wins, so the raw HTML
 * absorbs the id and the real heading is left without one.
 *
 * This test pins that current behavior so a future fix is forced to
 * update the assertions deliberately. The non-colliding case
 * (different text) already works correctly and is covered by
 * tests/027_heading_anchors.phpt. */

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

$opts = new MdParser\Options(
    headingAnchors: true,
    unsafe: true,
    tagfilter: false,
);
$p = new MdParser\Parser($opts);

// Raw HTML <h1> precedes a Markdown heading with identical visible
// text. The fingerprint search hits the raw HTML first; the real
// Markdown heading inherits no id.
$h = $p->toHtml("<h1>same</h1>\n\n# same\n");
check("raw <h1> absorbs the id (current behavior)",
    str_contains($h, '<h1 id="same">same</h1>'));
check("real Markdown heading is left without an id",
    substr_count($h, '<h1>same</h1>') === 1);

// Sanity: when the visible text differs, the raw heading is not
// confused for the Markdown heading and slugging works as expected.
$h = $p->toHtml("<h1>raw</h1>\n\n# real\n");
check("distinct text -- raw stays plain, real gets slug",
    str_contains($h, '<h1>raw</h1>') &&
    str_contains($h, '<h1 id="real">real</h1>'));

?>
--EXPECT--
OK: raw <h1> absorbs the id (current behavior)
OK: real Markdown heading is left without an id
OK: distinct text -- raw stays plain, real gets slug
