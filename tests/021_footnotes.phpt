--TEST--
footnotes: MD_FLAG_FOOTNOTES renders references and backrefs
--EXTENSIONS--
mdparser
--FILE--
<?php

$off = new MdParser\Parser();
$on = new MdParser\Parser(new MdParser\Options(footnotes: true));

// Default: footnote syntax is parsed as literal text (reference definition
// is swallowed, reference is rendered as a bracketed expression).
echo "--- off ---\n";
echo $off->toHtml("Text[^a] body.\n\n[^a]: the note\n");

echo "--- single ---\n";
echo $on->toHtml("Text[^a] body.\n\n[^a]: the note\n");

echo "--- multiple ---\n";
echo $on->toHtml("First[^one], then[^two].\n\n[^one]: alpha\n[^two]: beta\n");

echo "--- reused ref (one def, two references) ---\n";
echo $on->toHtml("Twice[^x] here[^x].\n\n[^x]: once\n");

echo "--- inline emphasis inside note body ---\n";
echo $on->toHtml("a[^n]b\n\n[^n]: *emphasized*\n");

echo "--- unused def is silently dropped ---\n";
echo $on->toHtml("no ref here\n\n[^orphan]: ignored\n");

echo "--- footnote in AST output ---\n";
$ast = $on->toAst("ref[^x]\n\n[^x]: body\n");
var_dump($ast['type']);
var_dump(count($ast['children']));
?>
--EXPECT--
--- off ---
<p>Text[^a] body.</p>
<p>[^a]: the note</p>
--- single ---
<p>Text<sup><a href="#fn-1" id="fnref-1-1">1</a></sup> body.</p>
<section class="footnotes">
<ol>
<li id="fn-1">
the note<a href="#fnref-1-1" class="footnote-backref">&#8617;</a>
</li>
</ol>
</section>
--- multiple ---
<p>First<sup><a href="#fn-1" id="fnref-1-1">1</a></sup>, then<sup><a href="#fn-2" id="fnref-2-1">2</a></sup>.</p>
<section class="footnotes">
<ol>
<li id="fn-1">
alpha<a href="#fnref-1-1" class="footnote-backref">&#8617;</a>
</li>
<li id="fn-2">
beta<a href="#fnref-2-1" class="footnote-backref">&#8617;</a>
</li>
</ol>
</section>
--- reused ref (one def, two references) ---
<p>Twice<sup><a href="#fn-1" id="fnref-1-1">1</a></sup> here<sup><a href="#fn-1" id="fnref-1-2">1</a></sup>.</p>
<section class="footnotes">
<ol>
<li id="fn-1">
once<a href="#fnref-1-1" class="footnote-backref">&#8617;</a> <a href="#fnref-1-2" class="footnote-backref">&#8617;</a>
</li>
</ol>
</section>
--- inline emphasis inside note body ---
<p>a<sup><a href="#fn-1" id="fnref-1-1">1</a></sup>b</p>
<section class="footnotes">
<ol>
<li id="fn-1">
<em>emphasized</em><a href="#fnref-1-1" class="footnote-backref">&#8617;</a>
</li>
</ol>
</section>
--- unused def is silently dropped ---
<p>no ref here</p>
--- footnote in AST output ---
string(8) "document"
int(2)
