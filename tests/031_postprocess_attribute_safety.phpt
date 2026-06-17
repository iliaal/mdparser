--TEST--
Postprocess does not splice into raw-HTML skip regions or quoted attribute values
--EXTENSIONS--
mdparser
--FILE--
<?php
$opts = new MdParser\Options(
    unsafe: true,
    tagfilter: false,
    headingAnchors: true,
    nofollowLinks: true,
);
$p = new MdParser\Parser($opts);

function check(string $name, bool $cond): void {
    echo ($cond ? 'PASS' : 'FAIL') . " $name\n";
}

// SS-101: nofollow injection must not corrupt attribute values inside raw HTML.
$out = $p->toHtml("<!-- <a href=\"x\">y</a> -->\n\nReal [link](https://example.com)\n");
check('comment body untouched',         strpos($out, '<!-- <a href="x">y</a> -->') !== false);
check('real link gets nofollow',        strpos($out, 'rel="nofollow noopener noreferrer" href="https://example.com"') !== false);

$out = $p->toHtml("<textarea><a href=\"x\">y</a></textarea>\n\nReal [link](https://example.com)\n");
check('textarea body untouched',        strpos($out, '<textarea><a href="x">y</a></textarea>') !== false);
check('real link still rewritten',      strpos($out, 'rel="nofollow noopener noreferrer" href="https://example.com"') !== false);

$out = $p->toHtml("<title>see <a href=\"x\">link</a></title>\n");
check('title body untouched',           strpos($out, '<title>see <a href="x">link</a></title>') !== false);

$out = $p->toHtml("<iframe>raw <a href=\"x\">link</a></iframe>\n");
check('iframe body untouched',          strpos($out, '<iframe>raw <a href="x">link</a></iframe>') !== false);

$out = $p->toHtml("<noscript><a href=\"x\">y</a></noscript>\n");
check('noscript body untouched',        strpos($out, '<noscript><a href="x">y</a></noscript>') !== false);

$out = $p->toHtml("<xmp><a href=\"x\">y</a></xmp>\n");
check('xmp body untouched',             strpos($out, '<xmp><a href="x">y</a></xmp>') !== false);

// Quoted attribute value: an inner `<a href="..."` must not pull rel out
// of the surrounding tag.
$out = $p->toHtml("<div title=\"<a href=\\\"x\\\">y</a>\">visible</div>\n\nReal [link](https://example.com)\n");
check('attr value untouched',           strpos($out, '<div title="<a href=\"x\">y</a>">visible</div>') !== false);
check('real link still rewritten (attr)', strpos($out, 'rel="nofollow noopener noreferrer" href="https://example.com"') !== false);

// SS-102: heading-anchor fingerprint matching inside an HTML comment must
// NOT consume the slug intended for the real Markdown heading.
$out = $p->toHtml("<!-- <h1>injected</h1> -->\n\n# injected\n");
check('comment-internal heading not slugged', strpos($out, '<!-- <h1>injected</h1> -->') !== false);
check('real markdown heading slugged',        strpos($out, '<h1 id="injected">injected</h1>') !== false);

// AD-301: under the md4c in-stream renderer, raw-HTML anchors pass through
// verbatim and are NOT rel-rewritten (nofollow applies only to Markdown
// links, never to author-supplied raw HTML). Both <a> here are raw HTML,
// so neither gets rel. Each Markdown heading is slugged from its own text
// in-stream, so the earlier raw/Markdown slug-collision (CR-003) no longer
// applies: the Markdown heading gets its id; the raw <h1> stays untouched.
$out = $p->toHtml("<h1>look <a href=\"evil.com\">x</a></h1>\n\n# look <a href=\"evil.com\">x</a>\n");
check('raw-HTML anchors are not rel-rewritten',
    !str_contains($out, 'rel="nofollow'));
check('markdown heading slugged, raw heading untouched',
    str_contains($out, '<h1 id="look-x">') &&
    str_contains($out, '<h1>look <a href="evil.com">x</a></h1>'));

// CDATA section -- treated as opaque skip region under unsafe.
$out = $p->toHtml("<![CDATA[ <a href=\"x\">y</a> ]]>\n\nReal [link](https://example.com)\n");
check('CDATA body untouched',           strpos($out, '<![CDATA[ <a href="x">y</a> ]]>') !== false);

// Close tag with trailing whitespace before `>` (HTML5 allows it).
// scan_skip_region must not get stuck thinking the rest is body.
$out = $p->toHtml("<title><h1>Real</h1></title >\n\n# Real\n[real](https://example.com)\n");
check('whitespace before close `>` recognized', strpos($out, '<h1 id="real">Real</h1>') !== false);
check('rel injected after whitespace close',    strpos($out, 'rel="nofollow noopener noreferrer" href="https://example.com"') !== false);

// `</tag>` substring inside the OPENING tag's quoted attribute must
// not prematurely terminate the skip region.
$out = $p->toHtml("<title alt=\"</title>\">payload <a href=\"x\">y</a></title>\n\n[real](https://example.com)\n");
check('quoted </title> in opening tag attr ignored',
    strpos($out, '<title alt="</title>">payload <a href="x">y</a></title>') !== false);
check('real link still rewritten after attr-trick title',
    strpos($out, 'rel="nofollow noopener noreferrer" href="https://example.com"') !== false);

// HTML5 ASCII whitespace includes form feed (U+000C). The opening-tag
// delimiter and the closing-tag whitespace tolerance must accept it,
// or an attacker-controlled `\f` lets the scanner mis-classify the tag
// boundary.
$ff = "\x0C";
$out = $p->toHtml("<title{$ff}><h1>Real</h1></title>\n\n# Real\n");
check('FF as opening-tag delimiter recognized', strpos($out, '<h1 id="real">Real</h1>') !== false);

$out = $p->toHtml("<title><h1>Real</h1></title{$ff}>\n\n[real](https://example.com)\n");
check('FF before close `>` recognized',
    strpos($out, 'rel="nofollow noopener noreferrer" href="https://example.com"') !== false);

echo "done\n";
?>
--EXPECT--
PASS comment body untouched
PASS real link gets nofollow
PASS textarea body untouched
PASS real link still rewritten
PASS title body untouched
PASS iframe body untouched
PASS noscript body untouched
PASS xmp body untouched
PASS attr value untouched
PASS real link still rewritten (attr)
PASS comment-internal heading not slugged
PASS real markdown heading slugged
PASS raw-HTML anchors are not rel-rewritten
PASS markdown heading slugged, raw heading untouched
PASS CDATA body untouched
PASS whitespace before close `>` recognized
PASS rel injected after whitespace close
PASS quoted </title> in opening tag attr ignored
PASS real link still rewritten after attr-trick title
PASS FF as opening-tag delimiter recognized
PASS FF before close `>` recognized
done
