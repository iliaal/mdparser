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
