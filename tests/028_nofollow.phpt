--TEST--
Options::nofollowLinks injects rel="nofollow noopener noreferrer" on anchors
--SKIPIF--
<?php if (!extension_loaded("mdparser")) print "skip"; ?>
--FILE--
<?php

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

$REL = 'rel="nofollow noopener noreferrer"';

// default off
$d = (new MdParser\Parser())->toHtml("[x](https://example.com)\n");
check("default: no rel attribute", !str_contains($d, "rel="));

// flag on
$opts = new MdParser\Options(nofollowLinks: true);
$p = new MdParser\Parser($opts);

check("flag readable on Options", $opts->nofollowLinks === true);
check("default false on default Options", (new MdParser\Options())->nofollowLinks === false);

// inline link
$h = $p->toHtml("[hello](https://example.com)\n");
check("inline link gets rel",
    str_contains($h, "<a $REL href=\"https://example.com\">hello</a>"));

// reference link
$h = $p->toHtml("[hello][ref]\n\n[ref]: https://example.com\n");
check("reference link gets rel",
    str_contains($h, "<a $REL href=\"https://example.com\">hello</a>"));

// autolink (extension)
$h = $p->toHtml("see https://example.com today\n");
check("autolink gets rel",
    str_contains($h, "<a $REL href=\"https://example.com\""));

// multiple links
$h = $p->toHtml("[a](https://a.com) and [b](https://b.com)\n");
check("first link gets rel",
    str_contains($h, "<a $REL href=\"https://a.com\""));
check("second link gets rel",
    str_contains($h, "<a $REL href=\"https://b.com\""));

// link in code block: <a href= is escaped, no transform
$h = $p->toHtml("```\n<a href=\"x\">y</a>\n```\n");
check("code-fenced anchor not touched",
    !str_contains($h, "rel=") &&
    str_contains($h, '&lt;a href=&quot;x&quot;'));

// link in inline code: also escaped
$h = $p->toHtml("use `<a href=\"x\">` syntax\n");
check("inline-code anchor not touched", !str_contains($h, "rel="));

// toInlineHtml path
$h = (new MdParser\Parser($opts))->toInlineHtml("[hi](https://x.com)");
check("toInlineHtml gets rel on link",
    str_contains($h, "<a $REL href=\"https://x.com\">hi</a>"));

// toInlineHtml: no link, no rel
$h = (new MdParser\Parser($opts))->toInlineHtml("plain text");
check("toInlineHtml: plain text passes through", $h === "plain text");

// toXml ignores
$x = $p->toXml("[x](https://example.com)\n");
check("toXml ignores nofollow flag", !str_contains($x, "nofollow"));

// both flags together
$both = new MdParser\Options(headingAnchors: true, nofollowLinks: true);
$pb = new MdParser\Parser($both);
$h = $pb->toHtml("# Hello\n\n[x](https://e.com)\n");
check("both: heading id present",
    str_contains($h, '<h1 id="hello">Hello</h1>'));
check("both: link rel present",
    str_contains($h, "<a $REL href=\"https://e.com\">x</a>"));

// static html() shortcut: defaults off
$h = MdParser\Parser::html("[x](https://e.com)\n");
check("static html() default has no rel", !str_contains($h, "rel="));

// fragment anchors (href="#...") skip rel injection
$opts_fn = new MdParser\Options(footnotes: true, nofollowLinks: true);
$h = (new MdParser\Parser($opts_fn))->toHtml("Hello[^1]\n\n[^1]: world\n");
check("footnote ref (#fn-) keeps no rel",
    str_contains($h, '<a href="#fn-1"'));
check("footnote backref (#fnref-) keeps no rel",
    str_contains($h, '<a href="#fnref-1"'));
check("fragment anchors: no rel anywhere", !str_contains($h, "rel="));

// fragment + external mix: external still gets rel
$h = (new MdParser\Parser($opts))->toHtml("[in](#anchor) and [out](https://e.com)\n");
check("fragment in mixed doc: no rel",
    str_contains($h, '<a href="#anchor">in</a>'));
check("external in mixed doc: gets rel",
    str_contains($h, "<a $REL href=\"https://e.com\">out</a>"));

// raw <script> under unsafe:true: contents must not be rewritten,
// real anchors outside still get rel
$opts_unsafe = new MdParser\Options(unsafe: true, tagfilter: false, nofollowLinks: true);
$pu = new MdParser\Parser($opts_unsafe);

$h = $pu->toHtml("<script>var s = \"<a href=\\\"x\\\">\";</script>\n\n[real](https://e.com)\n");
check("script body untouched",
    str_contains($h, 'var s = "<a href=\"x\">"') &&
    !str_contains($h, "rel=\"nofollow noopener noreferrer\" href=\"x\""));
check("real anchor after script still gets rel",
    str_contains($h, "<a $REL href=\"https://e.com\">real</a>"));

// raw <style>
$h = $pu->toHtml("<style>/* <a href=\"x\"> */</style>\n\n[real](https://e.com)\n");
check("style body untouched",
    str_contains($h, '/* <a href="x"> */') &&
    substr_count($h, "rel=\"nofollow") === 1);

// uppercase <SCRIPT> (raw HTML preserves case)
$h = $pu->toHtml("<SCRIPT>var a = \"<a href=\\\"x\\\">\";</SCRIPT>\n");
check("uppercase SCRIPT body untouched",
    str_contains($h, 'var a = "<a href=\"x\">"') &&
    !str_contains($h, "rel=\"nofollow"));

// real anchors both sides of a script
$h = $pu->toHtml("[before](https://b.com)\n\n<script>x = \"<a href=\\\"y\\\">\";</script>\n\n[after](https://a.com)\n");
check("real anchors both sides keep rel",
    str_contains($h, "<a $REL href=\"https://b.com\">before</a>") &&
    str_contains($h, "<a $REL href=\"https://a.com\">after</a>"));

?>
--EXPECT--
OK: default: no rel attribute
OK: flag readable on Options
OK: default false on default Options
OK: inline link gets rel
OK: reference link gets rel
OK: autolink gets rel
OK: first link gets rel
OK: second link gets rel
OK: code-fenced anchor not touched
OK: inline-code anchor not touched
OK: toInlineHtml gets rel on link
OK: toInlineHtml: plain text passes through
OK: toXml ignores nofollow flag
OK: both: heading id present
OK: both: link rel present
OK: static html() default has no rel
OK: footnote ref (#fn-) keeps no rel
OK: footnote backref (#fnref-) keeps no rel
OK: fragment anchors: no rel anywhere
OK: fragment in mixed doc: no rel
OK: external in mixed doc: gets rel
OK: script body untouched
OK: real anchor after script still gets rel
OK: style body untouched
OK: uppercase SCRIPT body untouched
OK: real anchors both sides keep rel
