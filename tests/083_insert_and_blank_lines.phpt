--TEST--
md4c dialect: insert spans (++ins++) and MD_FLAG_PRESERVEBLANKLINES
--EXTENSIONS--
mdparser
--FILE--
<?php

// Both flags are off by default, so the markup stays literal.
$default = new MdParser\Parser();
echo $default->toHtml("a ++ins++ b\n");
echo $default->toXml("x\n\n\ny\n");

$opts = new MdParser\Options(insert: true, preserveBlankLines: true);
$parser = new MdParser\Parser($opts);

echo $parser->toHtml("a ++ins++ b\n");
echo $parser->toHtml("nested ++ins with *em*++ done\n");
echo $parser->toXml("x\n\n\ny\n");

$ast = $parser->toAst("a ++ins++ b\n\n\nc\n");
$types = [];
$walk = function ($node) use (&$walk, &$types) {
    $types[] = $node["type"];
    foreach ($node["children"] ?? [] as $child) {
        $walk($child);
    }
};
$walk($ast);
echo implode(",", $types), "\n";

// Insert spans are escaped like any other span content.
echo $parser->toHtml("++<script>++\n");

?>
--EXPECT--
<p>a ++ins++ b</p>
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE document SYSTEM "CommonMark.dtd">
<document xmlns="http://commonmark.org/xml/1.0">
  <paragraph>
    <text xml:space="preserve">x</text>
  </paragraph>
  <paragraph>
    <text xml:space="preserve">y</text>
  </paragraph>
</document>
<p>a <ins>ins</ins> b</p>
<p>nested <ins>ins with <em>em</em></ins> done</p>
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE document SYSTEM "CommonMark.dtd">
<document xmlns="http://commonmark.org/xml/1.0">
  <paragraph>
    <text xml:space="preserve">x</text>
  </paragraph>
  <blank />
  <paragraph>
    <text xml:space="preserve">y</text>
  </paragraph>
</document>
document,paragraph,text,insert,text,text,blank,paragraph,text
<p><ins>&lt;script&gt;</ins></p>
