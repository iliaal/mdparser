--TEST--
postprocess: an unterminated raw tag must not drop transforms for the rest of the document
--EXTENSIONS--
mdparser
--FILE--
<?php

/* An unbalanced quote in a raw tag (reachable under unsafe:true) makes
 * scan_tag_close consume to end-of-document and return html_len. The
 * apply_transforms loop used to break there, abandoning nofollow and
 * heading-anchor injection for everything after the malformed tag. It
 * now treats the stray '<' as a literal byte and keeps scanning. */

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

$REL = 'rel="nofollow noopener noreferrer"';
$opts = new MdParser\Options(
    unsafe: true, tagfilter: false, nofollowLinks: true, headingAnchors: true,
);
$p = new MdParser\Parser($opts);

$h = $p->toHtml("<div q=\">\n\n[click](https://e.example)\n\n# head\n");
check("link after unterminated tag gets nofollow",
    str_contains($h, "$REL href=\"https://e.example\""));
check("heading after unterminated tag gets id",
    str_contains($h, '<h1 id="head">head</h1>'));

// Control: a balanced tag, transforms apply as always.
$h = $p->toHtml("<div q=\"x\">\n\n[click](https://e.example)\n");
check("control balanced: nofollow present",
    str_contains($h, "$REL href=\"https://e.example\""));

?>
--EXPECT--
OK: link after unterminated tag gets nofollow
OK: heading after unterminated tag gets id
OK: control balanced: nofollow present
