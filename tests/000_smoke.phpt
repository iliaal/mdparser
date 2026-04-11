--TEST--
mdparser smoke: module loads, Parser class exists, toHtml round-trips
--SKIPIF--
<?php if (!extension_loaded("mdparser")) print "skip mdparser not loaded"; ?>
--FILE--
<?php

var_dump(class_exists('MdParser\\Parser'));

$parser = new MdParser\Parser();
var_dump($parser instanceof MdParser\Parser);

echo $parser->toHtml(''), "---\n";
echo $parser->toHtml("# Hello"), "---\n";
echo $parser->toHtml("Hello **world**."), "---\n";
echo $parser->toHtml("| a | b |\n|---|---|\n| 1 | 2 |\n"), "---\n";
echo $parser->toHtml("~~strike~~"), "---\n";
echo $parser->toHtml("- [ ] todo\n- [x] done"), "---\n";
echo $parser->toHtml("visit https://example.com today"), "---\n";
?>
--EXPECT--
bool(true)
bool(true)
---
<h1>Hello</h1>
---
<p>Hello <strong>world</strong>.</p>
---
<table>
<thead>
<tr>
<th>a</th>
<th>b</th>
</tr>
</thead>
<tbody>
<tr>
<td>1</td>
<td>2</td>
</tr>
</tbody>
</table>
---
<p><del>strike</del></p>
---
<ul>
<li><input type="checkbox" disabled="" /> todo</li>
<li><input type="checkbox" checked="" disabled="" /> done</li>
</ul>
---
<p>visit <a href="https://example.com">https://example.com</a> today</p>
---
