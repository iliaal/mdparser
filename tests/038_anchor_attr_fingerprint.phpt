--TEST--
headingAnchors: a fingerprint match inside a tag attribute must not freeze the heading cursor
--EXTENSIONS--
mdparser
--FILE--
<?php

/* Regression for the heading-cursor desync (review D1). The anchor pass
 * locates each Markdown heading by searching the rendered HTML for the
 * heading's standalone byte rendering ("<h1>foo</h1>\n", trailing
 * newline included). Under unsafe:true + tagfilter:false, raw HTML can
 * place those exact bytes inside a quoted attribute value of a
 * non-raw-text element (a newline inside the attribute makes the
 * trailing-newline fingerprint match). apply_transforms skips such a
 * tag wholesale via scan_tag_close, stepping over that doc_offset; the
 * heading cursor must resync rather than freeze, or every heading after
 * the collision silently loses its id. */

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

$opts = new MdParser\Options(headingAnchors: true, unsafe: true, tagfilter: false);
$p = new MdParser\Parser($opts);

// The <div> attribute holds the exact fingerprint for `# foo`. The real
// "foo" heading inherits no id (documented collision), but "bar"
// must still be slugged.
$h = $p->toHtml("<div title=\"<h1>foo</h1>\n\">x</div>\n\n# foo\n\n# bar\n");
check("later heading keeps its id after attr-embedded fingerprint",
    str_contains($h, '<h1 id="bar">bar</h1>'));

// Cascade: multiple headings after the collision all keep their ids.
$h = $p->toHtml("<div title=\"<h1>a</h1>\n\">x</div>\n\n# a\n\n# b\n\n# c\n");
check("cascade: b keeps id", str_contains($h, '<h1 id="b">b</h1>'));
check("cascade: c keeps id", str_contains($h, '<h1 id="c">c</h1>'));

// Control: identical structure minus the embedded fingerprint. Both real
// headings get their ids.
$h = $p->toHtml("<div title=\"plain\">x</div>\n\n# foo\n\n# bar\n");
check("control: foo id", str_contains($h, '<h1 id="foo">foo</h1>'));
check("control: bar id", str_contains($h, '<h1 id="bar">bar</h1>'));

?>
--EXPECT--
OK: later heading keeps its id after attr-embedded fingerprint
OK: cascade: b keeps id
OK: cascade: c keeps id
OK: control: foo id
OK: control: bar id
