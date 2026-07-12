--TEST--
toXml ordered-list output is DOM-parseable
--EXTENSIONS--
mdparser
dom
--FILE--
<?php

$xml = (new MdParser\Parser())->toXml("999999999. c");
$doc = new DOMDocument();
echo ($doc->loadXML($xml) ? "OK" : "FAIL"), ": 9-digit output is DOM-parseable\n";

?>
--EXPECT--
OK: 9-digit output is DOM-parseable
