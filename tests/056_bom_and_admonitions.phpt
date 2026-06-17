--TEST--
leading UTF-8 BOM is stripped on every path; admonitions dialect extension
--EXTENSIONS--
mdparser
--FILE--
<?php

function eq(string $label, string $got, string $want): void {
    echo ($got === $want ? "OK" : "FAIL"), ": $label\n";
    if ($got !== $want) echo "  got:  $got\n  want: $want\n";
}

$BOM = "\xEF\xBB\xBF";

// ---- BOM: stripped, and the first block parses correctly ------------
$p = new MdParser\Parser();

// Without stripping, the BOM displaces the line start and "# H" renders
// as a literal paragraph instead of a heading; the BOM also leaks verbatim.
eq("BOM + heading -> <h1>, no leak", trim($p->toHtml($BOM . "# H")), "<h1>H</h1>");
echo (str_contains($p->toHtml($BOM . "# H"), $BOM) ? "FAIL" : "OK"), ": toHtml drops BOM bytes\n";
echo (str_contains($p->toXml($BOM . "# H"), "<heading") ? "OK" : "FAIL"), ": toXml parses heading after BOM\n";
echo (str_contains($p->toXml($BOM . "# H"), $BOM) ? "FAIL" : "OK"), ": toXml drops BOM bytes\n";

$ast = $p->toAst($BOM . "# H");
eq("toAst first child after BOM is heading", $ast["children"][0]["type"], "heading");

echo (str_contains($p->toInlineHtml($BOM . "hi"), $BOM) ? "FAIL" : "OK"), ": toInlineHtml drops BOM bytes\n";

// Only a *leading* BOM is stripped; a mid-text U+FEFF is left alone.
echo (str_contains($p->toHtml("a" . $BOM . "b"), $BOM) ? "OK" : "FAIL"), ": non-leading BOM preserved\n";

// ---- Admonitions (MD_FLAG_ADMONITIONS), opt-in ----------------------
$adm = new MdParser\Parser(new MdParser\Options(admonitions: true));
$md = "> [!NOTE]\n> Body.\n";

eq("HTML admonition wrapper",
   trim($adm->toHtml($md)),
   "<div class=\"admonition-note\">\n<p class=\"admonition-title\">note</p>\n<p>Body.</p>\n</div>");

echo (str_contains($adm->toXml($md), "<admonition type=\"note\">") ? "OK" : "FAIL"), ": XML <admonition type>\n";

$aast = $adm->toAst($md);
$node = $aast["children"][0];
eq("AST admonition node type", $node["type"], "admonition");
eq("AST admonition_type", $node["admonition_type"], "note");

// Default (off): GitHub alert syntax stays a plain blockquote.
$def = new MdParser\Parser();
echo (str_contains($def->toHtml($md), "admonition") ? "FAIL" : "OK"), ": admonitions off by default (blockquote)\n";

?>
--EXPECT--
OK: BOM + heading -> <h1>, no leak
OK: toHtml drops BOM bytes
OK: toXml parses heading after BOM
OK: toXml drops BOM bytes
OK: toAst first child after BOM is heading
OK: toInlineHtml drops BOM bytes
OK: non-leading BOM preserved
OK: HTML admonition wrapper
OK: XML <admonition type>
OK: AST admonition node type
OK: AST admonition_type
OK: admonitions off by default (blockquote)
