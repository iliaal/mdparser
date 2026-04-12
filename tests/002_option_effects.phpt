--TEST--
MdParser\Parser: options change rendered HTML
--SKIPIF--
<?php if (!extension_loaded("mdparser")) print "skip"; ?>
--FILE--
<?php

// Default parser: unsafe=false strips raw script
$default = new MdParser\Parser();
echo "-- default (safe) --\n";
echo $default->toHtml("before\n<script>alert(1)</script>\nafter"), "\n";

// unsafe=true lets raw HTML through (tagfilter still blocks script tag by default)
$unsafe = new MdParser\Parser(new MdParser\Options(unsafe: true));
echo "-- unsafe=true, tagfilter=true --\n";
echo $unsafe->toHtml("<b>bold</b> and <script>evil</script>"), "\n";

// Disable tagfilter to see raw script pass through (only unsafe=true lets raw HTML through at all)
$noFilter = new MdParser\Parser(new MdParser\Options(unsafe: true, tagfilter: false));
echo "-- unsafe=true, tagfilter=false --\n";
echo $noFilter->toHtml("<script>evil</script>"), "\n";

// smart punctuation
$smart = new MdParser\Parser(new MdParser\Options(smart: true));
echo "-- smart --\n";
echo $smart->toHtml("--dashes-- and \"quoted\" and it's"), "\n";

// hardbreaks: single newline -> <br />
$hb = new MdParser\Parser(new MdParser\Options(hardbreaks: true));
echo "-- hardbreaks --\n";
echo $hb->toHtml("line one\nline two"), "\n";

// Tables off: pipe syntax rendered as plain text
$noTables = new MdParser\Parser(new MdParser\Options(tables: false));
echo "-- tables=false --\n";
echo $noTables->toHtml("| a | b |\n|---|---|\n| 1 | 2 |\n"), "\n";

// Sourcepos annotations
$sp = new MdParser\Parser(new MdParser\Options(sourcepos: true));
echo "-- sourcepos --\n";
echo $sp->toHtml("# hi\n"), "\n";

// nobreaks: collapses soft line breaks into a single space
$nb = new MdParser\Parser(new MdParser\Options(nobreaks: true));
echo "-- nobreaks --\n";
echo $nb->toHtml("line one\nline two"), "\n";

// strikethroughDoubleTilde=true: single-tilde ~x~ is no longer strike,
// only double-tilde ~~x~~ renders as <del>
$sdt = new MdParser\Parser(new MdParser\Options(strikethroughDoubleTilde: true));
echo "-- strikethroughDoubleTilde --\n";
echo $sdt->toHtml("~one~ and ~~two~~"), "\n";

// tablePreferStyleAttributes: emit style="text-align:..." instead of align=
$tps = new MdParser\Parser(new MdParser\Options(tablePreferStyleAttributes: true));
echo "-- tablePreferStyleAttributes --\n";
echo $tps->toHtml("| a | b |\n|:-:|--:|\n| 1 | 2 |\n"), "\n";

// fullInfoString: surface everything after the language tag as data-meta
$fis = new MdParser\Parser(new MdParser\Options(fullInfoString: true));
echo "-- fullInfoString --\n";
echo $fis->toHtml("```php title=x linenums\nx\n```\n"), "\n";

// liberalHtmlTag: accept non-strict tag names (e.g. <div{x}>) as raw HTML.
// unsafe=true is required for raw HTML to pass through at all.
$lib = new MdParser\Parser(new MdParser\Options(unsafe: true, liberalHtmlTag: true));
$strict = new MdParser\Parser(new MdParser\Options(unsafe: true, liberalHtmlTag: false));
echo "-- liberalHtmlTag=true --\n";
echo $lib->toHtml("<div{onclick}>x</div>"), "\n";
echo "-- liberalHtmlTag=false --\n";
echo $strict->toHtml("<div{onclick}>x</div>"), "\n";
?>
--EXPECTF--
-- default (safe) --
<p>before</p>
<!-- raw HTML omitted -->
<p>after</p>

-- unsafe=true, tagfilter=true --
<p><b>bold</b> and &lt;script>evil&lt;/script></p>

-- unsafe=true, tagfilter=false --
<script>evil</script>

-- smart --
<p>–dashes– and “quoted” and it’s</p>

-- hardbreaks --
<p>line one<br />
line two</p>

-- tables=false --
<p>| a | b |
|---|---|
| 1 | 2 |</p>

-- sourcepos --
<h1 data-sourcepos="1:1-1:4">hi</h1>

-- nobreaks --
<p>line one line two</p>

-- strikethroughDoubleTilde --
<p>~one~ and <del>two</del></p>

-- tablePreferStyleAttributes --
<table>
<thead>
<tr>
<th style="text-align: center">a</th>
<th style="text-align: right">b</th>
</tr>
</thead>
<tbody>
<tr>
<td style="text-align: center">1</td>
<td style="text-align: right">2</td>
</tr>
</tbody>
</table>

-- fullInfoString --
<pre lang="php" data-meta="title=x linenums"><code>x
</code></pre>

-- liberalHtmlTag=true --
<p><div{onclick}>x</div></p>

-- liberalHtmlTag=false --
<p>&lt;div{onclick}&gt;x</div></p>
