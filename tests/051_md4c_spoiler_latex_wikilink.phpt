--TEST--
md4c dialect: spoilers, LaTeX math, wiki links (incl. wikilink XSS filter)
--EXTENSIONS--
mdparser
--FILE--
<?php

function eq(string $label, string $got, string $want): void {
    echo ($got === $want ? "OK" : "FAIL"), ": $label\n";
    if ($got !== $want) echo "  got:  $got\n  want: $want\n";
}

$opt  = fn(array $a) => new MdParser\Options(...$a);
$html = fn(MdParser\Options $o, string $md) => trim((new MdParser\Parser($o))->toHtml($md));

// ---- rendering: standard HTML + class --------------------------------
eq("spoiler -> span.spoiler",
   $html($opt(['spoilers' => true]), "||hidden||"),
   '<p><span class="spoiler">hidden</span></p>');
eq("inline math -> span.math",
   $html($opt(['latexMath' => true]), '$x^2$'),
   '<p><span class="math">x^2</span></p>');
eq("display math -> span.math.display",
   $html($opt(['latexMath' => true]), '$$E=mc^2$$'),
   '<p><span class="math display">E=mc^2</span></p>');
eq("wikilink bare (target percent-escaped, label = text)",
   $html($opt(['wikiLinks' => true]), "[[Page Name]]"),
   '<p><a class="wikilink" href="Page%20Name">Page Name</a></p>');
eq("wikilink labeled",
   $html($opt(['wikiLinks' => true]), "[[target|label]]"),
   '<p><a class="wikilink" href="target">label</a></p>');

// ---- defaults OFF: input stays literal CommonMark --------------------
eq("spoiler default literal",  $html($opt([]), "||x||"),    "<p>||x||</p>");
eq("latex default literal",    $html($opt([]), '$x$'),      '<p>$x$</p>');
eq("wikilink default literal", $html($opt([]), "[[Page]]"), "<p>[[Page]]</p>");

// ---- SECURITY: wikilink target runs the same scheme filter as <a> ----
// (decode -> check -> emit; this scheme-filter defense must not regress.)
eq("wikilink javascript: rejected",
   $html($opt(['wikiLinks' => true]), "[[javascript:alert(1)]]"),
   '<p><a class="wikilink" href="">javascript:alert(1)</a></p>');
eq("wikilink entity-encoded javascript: rejected",
   $html($opt(['wikiLinks' => true]), "[[javascript&colon;alert(1)]]"),
   '<p><a class="wikilink" href="">javascript&amp;colon;alert(1)</a></p>');
eq("wikilink vbscript: rejected",
   $html($opt(['wikiLinks' => true]), "[[vbscript:x]]"),
   '<p><a class="wikilink" href="">vbscript:x</a></p>');
eq("wikilink safe external + nofollow gets rel",
   $html($opt(['wikiLinks' => true, 'nofollowLinks' => true]), "[[https://e.com]]"),
   '<p><a rel="nofollow noopener noreferrer" class="wikilink" href="https://e.com">https://e.com</a></p>');

// ---- latex content is verbatim TeX: HTML-escaped, never SmartyPants --
eq("latex content escaped, no smart punctuation",
   $html($opt(['latexMath' => true, 'smart' => true]), '$a > b & c$'),
   '<p><span class="math">a &gt; b &amp; c</span></p>');

// ---- XML + AST node names --------------------------------------------
$p = new MdParser\Parser($opt(['spoilers' => true, 'latexMath' => true, 'wikiLinks' => true]));

$xml = $p->toXml("[[Page]]");
echo (str_contains($xml, '<wikilink destination="Page">') ? "OK" : "FAIL"),
     ": XML wikilink element with destination\n";

$ast = $p->toAst("[[Page|lbl]]");
$node = $ast['children'][0]['children'][0];
echo ($node['type'] === 'wikilink' && $node['url'] === 'Page' ? "OK" : "FAIL"),
     ": AST wikilink node carries url=Page\n";

$astSp = $p->toAst("||s||");
echo ($astSp['children'][0]['children'][0]['type'] === 'spoiler' ? "OK" : "FAIL"),
     ": AST spoiler node\n";

?>
--EXPECT--
OK: spoiler -> span.spoiler
OK: inline math -> span.math
OK: display math -> span.math.display
OK: wikilink bare (target percent-escaped, label = text)
OK: wikilink labeled
OK: spoiler default literal
OK: latex default literal
OK: wikilink default literal
OK: wikilink javascript: rejected
OK: wikilink entity-encoded javascript: rejected
OK: wikilink vbscript: rejected
OK: wikilink safe external + nofollow gets rel
OK: latex content escaped, no smart punctuation
OK: XML wikilink element with destination
OK: AST wikilink node carries url=Page
OK: AST spoiler node
