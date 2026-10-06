--TEST--
NUL replacements preserve heading anchors and smart quote context
--EXTENSIONS--
mdparser
--FILE--
<?php
$p = new MdParser\Parser(new MdParser\Options(smart: true, headingAnchors: true));

/* All three spellings render U+FFFD and must supply the same context. */
foreach (["\0", "\u{FFFD}", '&#0;'] as $replacement) {
    echo $p->toHtml("# a{$replacement}b\n\n# a\u{FFFD}b");
    echo $p->toHtml("{$replacement}\"q\" and ({$replacement}'q'");
    echo $p->toInlineHtml("{$replacement}\"q\""), "\n";
}

/* Verbatim NUL bytes also contribute to the heading's plain text. */
echo $p->toHtml("# `a\0b`\n\n# a\u{FFFD}b");

echo $p->toHtml("`a\0`\"q\"");

/* Whitespace following a replacement still provides opening context. */
echo $p->toHtml("\0 \"q\"");
?>
--EXPECT--
<h1 id="a�b">a�b</h1>
<h1 id="a�b-1">a�b</h1>
<p>�”q” and (�’q’</p>
�”q”
<h1 id="a�b">a�b</h1>
<h1 id="a�b-1">a�b</h1>
<p>�”q” and (�’q’</p>
�”q”
<h1 id="a�b">a�b</h1>
<h1 id="a�b-1">a�b</h1>
<p>�”q” and (�’q’</p>
�”q”
<h1 id="a�b"><code>a�b</code></h1>
<h1 id="a�b-1">a�b</h1>
<p><code>a�</code>”q”</p>
<p>� “q”</p>
