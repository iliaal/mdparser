--TEST--
toAst table header rows include is_header metadata
--EXTENSIONS--
mdparser
--FILE--
<?php

$p = new MdParser\Parser();
$ast = $p->toAst("| a | b |\n|---|---|\n| 1 | 2 |\n");
$table = $ast['children'][0];
$header = $table['children'][0];
$body = $table['children'][1];

var_dump($header['type']);
var_dump(array_key_exists('is_header', $header));
var_dump($header['is_header']);
var_dump($body['type']);
var_dump(array_key_exists('is_header', $body));
var_dump($body['is_header']);

?>
--EXPECT--
string(12) "table_header"
bool(true)
bool(true)
string(9) "table_row"
bool(true)
bool(false)
