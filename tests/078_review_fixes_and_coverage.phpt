--TEST--
review fixes: fence lang prefix, fragment nofollow, coverage gaps
--EXTENSIONS--
mdparser
--FILE--
<?php

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

// ---- CR-004: fence language- prefix after entity decode -------------
$p = new MdParser\Parser();
$h = $p->toHtml("```language&#45;php\ncode\n```\n");
check("entity language- not double-prefixed",
    str_contains($h, 'class="language-php"') &&
    !str_contains($h, 'language-language-php'));
$h = $p->toHtml("```php\ncode\n```\n");
check("plain php still gets language- prefix",
    str_contains($h, 'class="language-php"'));
$h = $p->toHtml("```language-js\nx\n```\n");
check("already-prefixed language- not doubled",
    str_contains($h, 'class="language-js"') &&
    !str_contains($h, 'language-language-js'));

// ---- CR-008: fragment nofollow trims leading C0/space ----------------
$nf = new MdParser\Parser(new MdParser\Options(nofollowLinks: true));
$REL = 'rel="nofollow noopener noreferrer"';
$h = $nf->toHtml("[x](#section)\n");
check("plain fragment skips nofollow",
    str_contains($h, 'href="#section"') && !str_contains($h, 'nofollow'));
$h = $nf->toHtml("[x]( #section)\n");
// md4c may normalize destination whitespace; either way nofollow must not fire.
check("space-padded fragment skips nofollow",
    !str_contains($h, $REL) && str_contains($h, 'href=') && str_contains($h, '#section'));
$h = $nf->toHtml("[x](https://e.com)\n");
check("normal link still gets nofollow",
    str_contains($h, $REL));

// ---- CR-012: admonition type matrix ---------------------------------
$adm = new MdParser\Parser(new MdParser\Options(admonitions: true));
foreach (['NOTE' => 'note', 'TIP' => 'tip', 'IMPORTANT' => 'important',
          'WARNING' => 'warning', 'CAUTION' => 'caution'] as $src => $cls) {
    $md = "> [!$src]\n> Body.\n";
    $html = $adm->toHtml($md);
    check("admonition HTML class admonition-$cls",
        str_contains($html, "class=\"admonition-$cls\"") &&
        str_contains($html, "admonition-title\">$cls</p>"));
    check("admonition XML type=$cls",
        str_contains($adm->toXml($md), "<admonition type=\"$cls\">"));
    $node = $adm->toAst($md)['children'][0];
    check("admonition AST type $cls",
        ($node['type'] ?? '') === 'admonition' &&
        ($node['admonition_type'] ?? '') === $cls);
}

// ---- CR-012: latex AST/XML nodes ------------------------------------
$math = new MdParser\Parser(new MdParser\Options(latexMath: true));
$md = "see \$x\$ and \$\$y\$\$\n";
$a = $math->toAst($md);
// Walk for latex_math / latex_math_display
function find_types(array $n, array &$acc): void {
    if (isset($n['type'])) $acc[$n['type']] = true;
    foreach ($n['children'] ?? [] as $c) {
        if (is_array($c)) find_types($c, $acc);
    }
}
$types = [];
find_types($a, $types);
check("AST has latex_math", isset($types['latex_math']));
check("AST has latex_math_display", isset($types['latex_math_display']));
$x = $math->toXml($md);
check("XML has latex_math", str_contains($x, '<latex_math'));
check("XML has latex_math_display", str_contains($x, '<latex_math_display'));

// ---- CR-012: data:image gif/jpeg/webp allowlist positives ------------
foreach (['gif', 'jpeg', 'webp'] as $mime) {
    $md = "![x](data:image/$mime;base64,AA==)\n";
    $h = $p->toHtml($md);
    check("data:image/$mime allowed in img",
        str_contains($h, "src=\"data:image/$mime;base64,AA==\""));
}
$h = $p->toHtml("![x](data:image/svg+xml;base64,AA==)\n");
check("data:image/svg+xml still blocked",
    str_contains($h, 'src=""') || !str_contains($h, 'svg+xml'));

// ---- CR-012: strikethrough / tasklist off ---------------------------
$off = new MdParser\Parser(new MdParser\Options(
    strikethrough: false,
    tasklist: false,
));
$h = $off->toHtml("~~x~~\n");
check("strikethrough off keeps tildes",
    str_contains($h, '~~x~~') && !str_contains($h, '<del>'));
$h = $off->toHtml("- [ ] todo\n");
check("tasklist off is normal li",
    str_contains($h, '<li>') &&
    !str_contains($h, 'task-list-item') &&
    !str_contains($h, 'checkbox'));

// ---- CR-012: headingAnchors no-op on toInlineHtml -------------------
$ha = new MdParser\Parser(new MdParser\Options(headingAnchors: true));
$h = $ha->toInlineHtml("# not a heading");
check("toInlineHtml headingAnchors no-op",
    $h === "# not a heading" && !str_contains($h, 'id='));

// ---- CR-011: AST flattens footnote section; XML keeps it ------------
$fn = new MdParser\Parser(new MdParser\Options(footnotes: true));
$md = "A[^1]\n\n[^1]: note\n";
$ast = $fn->toAst($md);
$types = [];
find_types($ast, $types);
check("AST has footnote_definition", isset($types['footnote_definition']));
check("AST has no footnote_section type", !isset($types['footnote_section']));
$x = $fn->toXml($md);
check("XML has footnote_section", str_contains($x, '<footnote_section'));
check("XML has footnote_definition", str_contains($x, '<footnote_definition'));

?>
--EXPECT--
OK: entity language- not double-prefixed
OK: plain php still gets language- prefix
OK: already-prefixed language- not doubled
OK: plain fragment skips nofollow
OK: space-padded fragment skips nofollow
OK: normal link still gets nofollow
OK: admonition HTML class admonition-note
OK: admonition XML type=note
OK: admonition AST type note
OK: admonition HTML class admonition-tip
OK: admonition XML type=tip
OK: admonition AST type tip
OK: admonition HTML class admonition-important
OK: admonition XML type=important
OK: admonition AST type important
OK: admonition HTML class admonition-warning
OK: admonition XML type=warning
OK: admonition AST type warning
OK: admonition HTML class admonition-caution
OK: admonition XML type=caution
OK: admonition AST type caution
OK: AST has latex_math
OK: AST has latex_math_display
OK: XML has latex_math
OK: XML has latex_math_display
OK: data:image/gif allowed in img
OK: data:image/jpeg allowed in img
OK: data:image/webp allowed in img
OK: data:image/svg+xml still blocked
OK: strikethrough off keeps tildes
OK: tasklist off is normal li
OK: toInlineHtml headingAnchors no-op
OK: AST has footnote_definition
OK: AST has no footnote_section type
OK: XML has footnote_section
OK: XML has footnote_definition
