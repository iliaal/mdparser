--TEST--
toInlineHtml treats whitespace-only physical lines as blank lines
--EXTENSIONS--
mdparser
--FILE--
<?php

$p = new MdParser\Parser();

var_dump($p->toInlineHtml("\n  \n  "));
var_dump($p->toInlineHtml("a\n  \n# h"));
var_dump($p->toInlineHtml("a\n\t\n# h"));

?>
--EXPECT--
string(0) ""
string(5) "a
# h"
string(5) "a
# h"
