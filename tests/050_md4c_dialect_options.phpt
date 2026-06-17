--TEST--
md4c parser toggles + dialect spans (underline/highlight/super/subscript)
--EXTENSIONS--
mdparser
--FILE--
<?php

function eq(string $label, string $got, string $want): void {
    echo ($got === $want ? "OK" : "FAIL"), ": $label\n";
    if ($got !== $want) echo "  got:  $got\n  want: $want\n";
}

$opt = fn(array $a) => new MdParser\Options(...$a);
$html = fn(MdParser\Options $o, string $md) => trim((new MdParser\Parser($o))->toHtml($md));

// ---- Tier A: parser toggles (no new rendering) -----------------------
eq("collapseWhitespace collapses runs",
   $html($opt(['collapseWhitespace' => true]), "a      b"), "<p>a b</p>");
eq("permissiveAtxHeadings: ###hi is h3",
   $html($opt(['permissiveAtxHeadings' => true]), "###hi"), "<h3>hi</h3>");
eq("noIndentedCodeBlocks: indent stays prose",
   $html($opt(['noIndentedCodeBlocks' => true]), "    code"), "<p>code</p>");

// Each Tier A toggle is OFF by default.
eq("collapseWhitespace default keeps runs",
   $html($opt([]), "a      b"), "<p>a      b</p>");
eq("permissiveAtxHeadings default: ###hi is paragraph",
   $html($opt([]), "###hi"), "<p>###hi</p>");

// ---- Tier B: dialect spans (standard semantic tags) ------------------
eq("highlight -> <mark>",   $html($opt(['highlight' => true]),   "==hi=="), "<p><mark>hi</mark></p>");
eq("superscript -> <sup>",  $html($opt(['superscript' => true]), "x^2^"),   "<p>x<sup>2</sup></p>");
eq("subscript -> <sub>",    $html($opt(['subscript' => true]),   "H~2~O"),  "<p>H<sub>2</sub>O</p>");
eq("underline -> <u>",      $html($opt(['underline' => true]),   "_hi_"),   "<p><u>hi</u></p>");

// Defaults: dialect spans OFF, CommonMark behavior preserved.
eq("highlight default stays literal",   $html($opt([]), "==hi=="), "<p>==hi==</p>");
eq("superscript default stays literal",  $html($opt([]), "x^2^"),   "<p>x^2^</p>");
eq("underline default is emphasis",      $html($opt([]), "_hi_"),   "<p><em>hi</em></p>");

// ---- Interaction: subscript (~) vs default strikethrough (~~) ---------
eq("subscript on: ~~ still strikethrough, ~ subscript",
   $html($opt(['subscript' => true]), "~~gone~~ H~2~O"),
   "<p><del>gone</del> H<sub>2</sub>O</p>");

// underline ON disables '_' emphasis (md4c semantics).
eq("underline on: _x_ is underline not emphasis",
   $html($opt(['underline' => true]), "_x_"), "<p><u>x</u></p>");

// ---- XML / AST node names for the new spans --------------------------
$p = new MdParser\Parser($opt(['highlight' => true, 'superscript' => true, 'subscript' => true, 'underline' => true]));

$ast = $p->toAst("==a== x^2^ y~3~ _u_");
$inline = array_column($ast['children'][0]['children'], 'type');
$present = fn(string $t) => in_array($t, $inline, true);
echo ($present('highlight') ? "OK" : "FAIL"),    ": AST has 'highlight' node\n";
echo ($present('superscript') ? "OK" : "FAIL"),  ": AST has 'superscript' node\n";
echo ($present('subscript') ? "OK" : "FAIL"),    ": AST has 'subscript' node\n";
echo ($present('underline') ? "OK" : "FAIL"),    ": AST has 'underline' node\n";

$xml = $p->toXml("==a==");
echo (str_contains($xml, "<highlight>") ? "OK" : "FAIL"), ": XML has <highlight> element\n";

?>
--EXPECT--
OK: collapseWhitespace collapses runs
OK: permissiveAtxHeadings: ###hi is h3
OK: noIndentedCodeBlocks: indent stays prose
OK: collapseWhitespace default keeps runs
OK: permissiveAtxHeadings default: ###hi is paragraph
OK: highlight -> <mark>
OK: superscript -> <sup>
OK: subscript -> <sub>
OK: underline -> <u>
OK: highlight default stays literal
OK: superscript default stays literal
OK: underline default is emphasis
OK: subscript on: ~~ still strikethrough, ~ subscript
OK: underline on: _x_ is underline not emphasis
OK: AST has 'highlight' node
OK: AST has 'superscript' node
OK: AST has 'subscript' node
OK: AST has 'underline' node
OK: XML has <highlight> element
