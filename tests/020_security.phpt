--TEST--
security: safe mode strips dangerous URLs and raw HTML (XSS regression gate)
--EXTENSIONS--
mdparser
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

// === SS-001: entity-encoded colon must not smuggle a scheme past the
// filter (the filter runs on decoded bytes: decode -> check -> emit) ===

assertHtml(
    "safe: entity-colon javascript (&colon;)",
    '<p><a href="">x</a></p>',
    $safe->toHtml("[x](javascript&colon;alert(1))")
);

assertHtml(
    "safe: numeric entity-colon javascript (&#58;)",
    '<p><a href="">x</a></p>',
    $safe->toHtml("[x](javascript&#58;alert(1))")
);

assertHtml(
    "safe: hex entity-colon javascript (&#x3a;)",
    '<p><a href="">x</a></p>',
    $safe->toHtml("[x](javascript&#x3a;alert(1))")
);

assertHtml(
    "safe: entity-colon javascript in image src",
    '<p><img src="" alt="x" /></p>',
    $safe->toHtml("![x](javascript&colon;alert(1))")
);

// === SS-002: data: allowlist is image-context only, exact MIME + terminator ===

assertHtml(
    "safe: data: rejected in link href",
    '<p><a href="">c</a></p>',
    $safe->toHtml("[c](data:image/png,x)")
);

assertHtml(
    "safe: data:image/png+xml prefix not accepted",
    '<p><img src="" alt="x" /></p>',
    $safe->toHtml("![x](data:image/png+xml;base64,AAAA)")
);

assertHtml(
    "safe: data:image/svg+xml rejected",
    '<p><img src="" alt="x" /></p>',
    $safe->toHtml("![x](data:image/svg+xml;base64,AAAA)")
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

// === default (safe) mode: raw HTML is HTML-escaped, not executable ===

assertHtml(
    "safe: raw script block escaped",
    "<p>before</p>\n&lt;script&gt;alert(1)&lt;/script&gt;\n<p>after</p>",
    $safe->toHtml("before\n\n<script>alert(1)</script>\n\nafter")
);

assertHtml(
    "safe: inline script tags escaped, text content remains",
    '<p>before &lt;script&gt;alert(1)&lt;/script&gt; after</p>',
    $safe->toHtml("before <script>alert(1)</script> after")
);

assertHtml(
    "safe: iframe escaped",
    '&lt;iframe src=javascript:alert(1)&gt;&lt;/iframe&gt;',
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
OK: safe: entity-colon javascript (&colon;)
OK: safe: numeric entity-colon javascript (&#58;)
OK: safe: hex entity-colon javascript (&#x3a;)
OK: safe: entity-colon javascript in image src
OK: safe: data: rejected in link href
OK: safe: data:image/png+xml prefix not accepted
OK: safe: data:image/svg+xml rejected
OK: safe: mailto URL passes
OK: safe: https URL passes
OK: safe: data:image/png base64 image passes
OK: safe: raw script block escaped
OK: safe: inline script tags escaped, text content remains
OK: safe: iframe escaped
OK: unsafe: javascript URL in link passes
OK: unsafe+tagfilter: script tag escaped
OK: unsafe+no_tagfilter: raw script passes verbatim
