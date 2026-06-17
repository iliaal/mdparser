--TEST--
headingAnchors: in-stream anchors leave raw HTML headings plain, no collision
--EXTENSIONS--
mdparser
--FILE--
<?php

/* Heading ids are attached in-stream as md4c emits each heading node, so
 * they apply only to Markdown headings. A raw HTML <h1> block (possible
 * only under unsafe:true with tagfilter:false) is raw HTML, not a parsed
 * heading, so it is emitted untouched and gets no id. A raw heading and a
 * later Markdown heading with identical text therefore do not collide:
 * the raw one stays plain, the Markdown one gets the id. */

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

$opts = new MdParser\Options(
    headingAnchors: true,
    unsafe: true,
    tagfilter: false,
);
$p = new MdParser\Parser($opts);

// Raw HTML <h1> precedes a Markdown heading with identical visible text.
$h = $p->toHtml("<h1>same</h1>\n\n# same\n");
check("raw <h1> stays plain (no id)",
    str_contains($h, "<h1>same</h1>\n"));
check("Markdown heading gets the id",
    str_contains($h, '<h1 id="same">same</h1>'));
check("exactly one id is emitted",
    substr_count($h, 'id="same"') === 1);

// Sanity: distinct text behaves the same way.
$h = $p->toHtml("<h1>raw</h1>\n\n# real\n");
check("distinct text -- raw stays plain, real gets slug",
    str_contains($h, "<h1>raw</h1>\n") &&
    str_contains($h, '<h1 id="real">real</h1>'));

?>
--EXPECT--
OK: raw <h1> stays plain (no id)
OK: Markdown heading gets the id
OK: exactly one id is emitted
OK: distinct text -- raw stays plain, real gets slug
