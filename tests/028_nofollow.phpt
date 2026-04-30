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
