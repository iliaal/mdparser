--TEST--
headingAnchors: unterminated raw tag must not truncate the skip list
--EXTENSIONS--
mdparser
--FILE--
<?php

/* scan_tag_close returns html_len for a tag with an unclosed quote.
 * compute_skip_list used to take that as a jump-to-end and stopped
 * recording skip regions, while apply_transforms advances one byte
 * past the same tag and keeps skipping regions. The desync let
 * resolve_heading_offsets place a heading fingerprint inside a region
 * apply_transforms then skipped, so the real heading silently lost
 * its id. compute_skip_list now treats the unterminated tag the same
 * way: advance one byte, keep scanning. */

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

$p = new MdParser\Parser(new MdParser\Options(headingAnchors: true, unsafe: true, tagfilter: false));

$h = $p->toHtml("<div data=\"x\n\n<style>\n<h1>foo</h1>\n</style>\n\n# foo\n");
check("unterminated tag + decoy region: heading keeps id",
    str_contains($h, '<h1 id="foo">foo</h1>'));

// Control: terminated tag, same decoy region.
$h = $p->toHtml("<div data=\"x\">\n\n<style>\n<h1>foo</h1>\n</style>\n\n# foo\n");
check("control: heading keeps id", str_contains($h, '<h1 id="foo">foo</h1>'));

// Comment-region variant works with the default tagfilter.
$p2 = new MdParser\Parser(new MdParser\Options(headingAnchors: true, unsafe: true));
$h = $p2->toHtml("<div data=\"x\n\n<!-- <h1>foo</h1> -->\n\n# foo\n");
check("unterminated tag + decoy comment: heading keeps id",
    str_contains($h, '<h1 id="foo">foo</h1>'));

?>
--EXPECT--
OK: unterminated tag + decoy region: heading keeps id
OK: control: heading keeps id
OK: unterminated tag + decoy comment: heading keeps id
