--TEST--
MdParser\Parser::toXml: CommonMark XML output
--SKIPIF--
<?php if (!extension_loaded("mdparser")) print "skip"; ?>
--FILE--
<?php
$p = new MdParser\Parser();
echo $p->toXml("# hi\n\nPara with *em*.\n");
?>
--EXPECT--
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE document SYSTEM "CommonMark.dtd">
<document xmlns="http://commonmark.org/xml/1.0">
  <heading level="1">
    <text xml:space="preserve">hi</text>
  </heading>
  <paragraph>
    <text xml:space="preserve">Para with </text>
    <emph>
      <text xml:space="preserve">em</text>
    </emph>
    <text xml:space="preserve">.</text>
  </paragraph>
</document>
