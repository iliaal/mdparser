--TEST--
footnotes: CMARK_OPT_FOOTNOTES renders references and backrefs
--SKIPIF--
<?php if (!extension_loaded("mdparser")) print "skip"; ?>
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
<p>Text<sup class="footnote-ref"><a href="#fn-a" id="fnref-a" data-footnote-ref>1</a></sup> body.</p>
<section class="footnotes" data-footnotes>
<ol>
<li id="fn-a">
<p>the note <a href="#fnref-a" class="footnote-backref" data-footnote-backref data-footnote-backref-idx="1" aria-label="Back to reference 1">↩</a></p>
</li>
</ol>
</section>
--- multiple ---
<p>First<sup class="footnote-ref"><a href="#fn-one" id="fnref-one" data-footnote-ref>1</a></sup>, then<sup class="footnote-ref"><a href="#fn-two" id="fnref-two" data-footnote-ref>2</a></sup>.</p>
<section class="footnotes" data-footnotes>
<ol>
<li id="fn-one">
<p>alpha <a href="#fnref-one" class="footnote-backref" data-footnote-backref data-footnote-backref-idx="1" aria-label="Back to reference 1">↩</a></p>
</li>
<li id="fn-two">
<p>beta <a href="#fnref-two" class="footnote-backref" data-footnote-backref data-footnote-backref-idx="2" aria-label="Back to reference 2">↩</a></p>
</li>
</ol>
</section>
--- reused ref (one def, two references) ---
<p>Twice<sup class="footnote-ref"><a href="#fn-x" id="fnref-x" data-footnote-ref>1</a></sup> here<sup class="footnote-ref"><a href="#fn-x" id="fnref-x-2" data-footnote-ref>1</a></sup>.</p>
<section class="footnotes" data-footnotes>
<ol>
<li id="fn-x">
<p>once <a href="#fnref-x" class="footnote-backref" data-footnote-backref data-footnote-backref-idx="1" aria-label="Back to reference 1">↩</a> <a href="#fnref-x-2" class="footnote-backref" data-footnote-backref data-footnote-backref-idx="1-2" aria-label="Back to reference 1-2">↩<sup class="footnote-ref">2</sup></a></p>
</li>
</ol>
</section>
--- inline emphasis inside note body ---
<p>a<sup class="footnote-ref"><a href="#fn-n" id="fnref-n" data-footnote-ref>1</a></sup>b</p>
<section class="footnotes" data-footnotes>
<ol>
<li id="fn-n">
<p><em>emphasized</em> <a href="#fnref-n" class="footnote-backref" data-footnote-backref data-footnote-backref-idx="1" aria-label="Back to reference 1">↩</a></p>
</li>
</ol>
</section>
--- unused def is silently dropped ---
<p>no ref here</p>
--- footnote in AST output ---
string(8) "document"
int(2)
