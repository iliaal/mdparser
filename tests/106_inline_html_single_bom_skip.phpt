--TEST--
toInlineHtml() strips exactly one leading BOM on single-line and multi-line input
--EXTENSIONS--
mdparser
--FILE--
<?php
$BOM = "\xEF\xBB\xBF";
$p = new MdParser\Parser();
echo bin2hex($p->toInlineHtml($BOM . $BOM . "ab")), "\n";
echo bin2hex($p->toInlineHtml($BOM . $BOM . "a\nb")), "\n";
echo bin2hex($p->toInlineHtml($BOM . "a\nb")), "\n";
?>
--EXPECT--
efbbbf6162
efbbbf610a62
610a62
