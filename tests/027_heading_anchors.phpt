--TEST--
Options::headingAnchors injects GitHub-style id slugs on headings
--SKIPIF--
<?php if (!extension_loaded("mdparser")) print "skip"; ?>
--FILE--
<?php

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

// default off: existing behavior unchanged
$d = (new MdParser\Parser())->toHtml("# Hello\n");
check("default: no id attribute", !str_contains($d, "id="));

// flag on: simple slug
$opts = new MdParser\Options(headingAnchors: true);
$p = new MdParser\Parser($opts);
$h = $p->toHtml("# Hello World\n");
check("simple: lowercase + hyphen", str_contains($h, '<h1 id="hello-world">Hello World</h1>'));

// flag stored on Options
check("flag readable on Options", $opts->headingAnchors === true);
check("default false on default Options", (new MdParser\Options())->headingAnchors === false);

// dedup
$h = $p->toHtml("# Foo\n## Foo\n### Foo\n");
check("dedup -1", str_contains($h, '<h2 id="foo-1">Foo</h2>'));
check("dedup -2", str_contains($h, '<h3 id="foo-2">Foo</h3>'));

// formatting in headings: emph/strong/code stripped from slug, rendered in body
$h = $p->toHtml("## Hello, **bold** `code` *italic*\n");
check("formatting stripped from slug",
    str_contains($h, 'id="hello-bold-code-italic"'));
check("formatting preserved in body",
    str_contains($h, '<strong>bold</strong>') &&
    str_contains($h, '<code>code</code>') &&
    str_contains($h, '<em>italic</em>'));

// punctuation dropped, runs of spaces collapsed
$h = $p->toHtml("# Foo, Bar -- Baz!\n");
check("punctuation dropped, spaces collapsed",
    str_contains($h, 'id="foo-bar-baz"'));

// non-ASCII bytes pass through
$h = $p->toHtml("# 日本語\n");
check("UTF-8 multibyte preserved", str_contains($h, 'id="日本語"'));

// empty slug after slugify: id attr omitted, no id=""
$h = $p->toHtml("# !!!\n");
check("all-punctuation: no id attr", !str_contains($h, 'id='));

// empty + non-empty: empty stays empty, normal one still slugged
$h = $p->toHtml("# !!!\n# Real\n");
check("empty + real: real still gets slug", str_contains($h, 'id="real"'));

// code block <a> not affected
$h = $p->toHtml("# Heading\n\n```\n<h1>raw</h1>\n```\n");
check("code-fenced <h1> not slug-injected",
    str_contains($h, '<h1 id="heading">Heading</h1>') &&
    str_contains($h, '&lt;h1&gt;raw&lt;/h1&gt;'));

// sourcepos coexists: id comes before data-sourcepos
$opts2 = new MdParser\Options(headingAnchors: true, sourcepos: true);
$h = (new MdParser\Parser($opts2))->toHtml("# X\n");
check("sourcepos coexists",
    str_contains($h, 'id="x" data-sourcepos='));

// static html() shortcut path: defaults off, so no anchors
$h = MdParser\Parser::html("# Hi\n");
check("static html() default has no anchors", !str_contains($h, "id="));

// toXml ignores postprocess flag
$x = $p->toXml("# Hello\n");
check("toXml ignores anchor flag", !str_contains($x, 'id="hello"'));

// toAst ignores postprocess flag
$a = $p->toAst("# Hi\n");
check("toAst returns array regardless", is_array($a));

// raw HTML <hN> under unsafe:true does not steal slugs from real headings
$opts3 = new MdParser\Options(headingAnchors: true, unsafe: true);
$p3 = new MdParser\Parser($opts3);
$h = $p3->toHtml("<h1>raw attacker</h1>\n\n# real heading\n");
check("unsafe: raw <h1> stays without id",
    str_contains($h, '<h1>raw attacker</h1>'));
check("unsafe: real heading still gets correct slug",
    str_contains($h, '<h1 id="real-heading">real heading</h1>'));

// interleaved raw + real
$h = $p3->toHtml("<h2>raw1</h2>\n\n# real1\n\n<h3>raw2</h3>\n\n# real2\n");
check("interleaved: raw1 unchanged",   str_contains($h, '<h2>raw1</h2>'));
check("interleaved: real1 slugged",    str_contains($h, '<h1 id="real1">real1</h1>'));
check("interleaved: raw2 unchanged",   str_contains($h, '<h3>raw2</h3>'));
check("interleaved: real2 slugged",    str_contains($h, '<h1 id="real2">real2</h1>'));

?>
--EXPECT--
OK: default: no id attribute
OK: simple: lowercase + hyphen
OK: flag readable on Options
OK: default false on default Options
OK: dedup -1
OK: dedup -2
OK: formatting stripped from slug
OK: formatting preserved in body
OK: punctuation dropped, spaces collapsed
OK: UTF-8 multibyte preserved
OK: all-punctuation: no id attr
OK: empty + real: real still gets slug
OK: code-fenced <h1> not slug-injected
OK: sourcepos coexists
OK: static html() default has no anchors
OK: toXml ignores anchor flag
OK: toAst returns array regardless
OK: unsafe: raw <h1> stays without id
OK: unsafe: real heading still gets correct slug
OK: interleaved: raw1 unchanged
OK: interleaved: real1 slugged
OK: interleaved: raw2 unchanged
OK: interleaved: real2 slugged
