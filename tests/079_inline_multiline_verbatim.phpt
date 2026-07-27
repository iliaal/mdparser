--TEST--
toInlineHtml does not leak the line sentinel into verbatim spans
--EXTENSIONS--
mdparser
--FILE--
<?php

function eq(string $label, string $got, string $want): void {
    echo ($got === $want ? "OK" : "FAIL: got " . var_export($got, true)), ": $label\n";
}

$p = new MdParser\Parser();

eq("code span over two lines", $p->toInlineHtml("`a\nb`"), "<code>a b</code>");
eq("code span over three lines", $p->toInlineHtml("`a\nb\nc`"), "<code>a b c</code>");
eq("code span after a text line", $p->toInlineHtml("x\n`a\nb`"), "x\n<code>a b</code>");
eq("text after a multiline code span", $p->toInlineHtml("`a\nb`;c"), "<code>a b</code>;c");
eq("code span nested in emphasis", $p->toInlineHtml("*a\n`b\nc`*"),
    "<em>a\n<code>b c</code></em>");
eq("backtick fence collapses to a code span", $p->toInlineHtml("```\nnope\n```"),
    "<code> nope </code>");
eq("literal semicolon after a code span", $p->toInlineHtml("`a\nb`\n;lit"),
    "<code>a b</code>\n;lit");

$latex = new MdParser\Parser(new MdParser\Options(latexMath: true));
eq("latex span over two lines", $latex->toInlineHtml("\$x\ny\$"),
    '<span class="math">x y</span>');
eq("display latex over two lines", $latex->toInlineHtml("\$\$x\ny\$\$"),
    '<span class="math display">x y</span>');

?>
--EXPECT--
OK: code span over two lines
OK: code span over three lines
OK: code span after a text line
OK: text after a multiline code span
OK: code span nested in emphasis
OK: backtick fence collapses to a code span
OK: literal semicolon after a code span
OK: latex span over two lines
OK: display latex over two lines
