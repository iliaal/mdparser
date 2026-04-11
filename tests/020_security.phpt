--TEST--
security: safe mode strips dangerous URLs and raw HTML (XSS regression gate)
--SKIPIF--
<?php if (!extension_loaded("mdparser")) print "skip"; ?>
--FILE--
<?php

$safe = new MdParser\Parser();
$unsafe = new MdParser\Parser(new MdParser\Options(unsafe: true));
$unsafeNoFilter = new MdParser\Parser(new MdParser\Options(unsafe: true, tagfilter: false));

function assertHtml(string $label, string $expected, string $actual): void {
    $actual = rtrim($actual, "\n");
    $expected = rtrim($expected, "\n");
    if ($actual !== $expected) {
        echo "FAIL: $label\n  expected: $expected\n  actual:   $actual\n";
        return;
    }
    echo "OK: $label\n";
}

// === default (safe) mode: dangerous URL schemes get stripped to empty ===

assertHtml(
    "safe: javascript URL in link",
    '<p><a href="">x</a></p>',
    $safe->toHtml("[x](javascript:alert(1))")
);

assertHtml(
    "safe: mixed-case javascript",
    '<p><a href="">x</a></p>',
    $safe->toHtml("[x](JaVaScRiPt:alert(1))")
);

assertHtml(
    "safe: javascript with leading whitespace",
    '<p><a href="">x</a></p>',
    $safe->toHtml("[x]( javascript:alert(1))")
);

assertHtml(
    "safe: data:text/html URL in link",
    '<p><a href="">x</a></p>',
    $safe->toHtml("[x](data:text/html,<b>x</b>)")
);

assertHtml(
    "safe: vbscript URL in link",
    '<p><a href="">x</a></p>',
    $safe->toHtml("[x](vbscript:msgbox(1))")
);

assertHtml(
    "safe: javascript URL in image",
    '<p><img src="" alt="x" /></p>',
    $safe->toHtml("![x](javascript:alert(1))")
);

assertHtml(
    "safe: image nested inside link with bad URL",
    '<p><a href=""><img src="https://example.com/x.png" alt="x" /></a></p>',
    $safe->toHtml("[![x](https://example.com/x.png)](javascript:alert(1))")
);

// === default (safe) mode: legit URL schemes pass through ===

assertHtml(
    "safe: mailto URL passes",
    '<p><a href="mailto:a@b.c">x</a></p>',
    $safe->toHtml("[x](mailto:a@b.c)")
);

assertHtml(
    "safe: https URL passes",
    '<p><a href="https://example.com/">x</a></p>',
    $safe->toHtml("[x](https://example.com/)")
);

assertHtml(
    "safe: data:image/png base64 image passes",
    '<p><img src="data:image/png;base64,iVBORw0KGgo=" alt="x" /></p>',
    $safe->toHtml("![x](data:image/png;base64,iVBORw0KGgo=)")
);

// === default (safe) mode: raw HTML gets stripped to comments ===

assertHtml(
    "safe: raw script block stripped",
    "<p>before</p>\n<!-- raw HTML omitted -->\n<p>after</p>",
    $safe->toHtml("before\n\n<script>alert(1)</script>\n\nafter")
);

assertHtml(
    "safe: inline script tags stripped, text content remains",
    '<p>before <!-- raw HTML omitted -->alert(1)<!-- raw HTML omitted --> after</p>',
    $safe->toHtml("before <script>alert(1)</script> after")
);

assertHtml(
    "safe: iframe block stripped",
    '<!-- raw HTML omitted -->',
    $safe->toHtml("<iframe src=javascript:alert(1)></iframe>")
);

// === unsafe=true: dangerous URLs pass through (user explicitly opted in) ===

assertHtml(
    "unsafe: javascript URL in link passes",
    '<p><a href="javascript:alert(1)">x</a></p>',
    $unsafe->toHtml("[x](javascript:alert(1))")
);

// === unsafe=true + tagfilter=true (default): GFM tagfilter still escapes
// specific dangerous tags (script, iframe, form, etc.) as a defense layer ===

assertHtml(
    "unsafe+tagfilter: script tag escaped",
    '&lt;script>alert(1)&lt;/script>',
    $unsafe->toHtml("<script>alert(1)</script>")
);

// === unsafe=true + tagfilter=false: all raw HTML passes verbatim ===

assertHtml(
    "unsafe+no_tagfilter: raw script passes verbatim",
    '<script>alert(1)</script>',
    $unsafeNoFilter->toHtml("<script>alert(1)</script>")
);
?>
--EXPECT--
OK: safe: javascript URL in link
OK: safe: mixed-case javascript
OK: safe: javascript with leading whitespace
OK: safe: data:text/html URL in link
OK: safe: vbscript URL in link
OK: safe: javascript URL in image
OK: safe: image nested inside link with bad URL
OK: safe: mailto URL passes
OK: safe: https URL passes
OK: safe: data:image/png base64 image passes
OK: safe: raw script block stripped
OK: safe: inline script tags stripped, text content remains
OK: safe: iframe block stripped
OK: unsafe: javascript URL in link passes
OK: unsafe+tagfilter: script tag escaped
OK: unsafe+no_tagfilter: raw script passes verbatim
