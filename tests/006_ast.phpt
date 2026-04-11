--TEST--
MdParser\Parser::toAst: nested-array AST output
--SKIPIF--
<?php if (!extension_loaded("mdparser")) print "skip"; ?>
--FILE--
<?php

$p = new MdParser\Parser();

// Heading with text child
$ast = $p->toAst("# Hello");
var_dump($ast['type']);
var_dump($ast['children'][0]['type']);
var_dump($ast['children'][0]['level']);
var_dump($ast['children'][0]['children'][0]['type']);
var_dump($ast['children'][0]['children'][0]['literal']);

// Paragraph with link
$ast = $p->toAst("visit [example](https://example.com \"the site\")");
$link = $ast['children'][0]['children'][1];
var_dump($link['type']);
var_dump($link['url']);
var_dump($link['title']);
var_dump($link['children'][0]['literal']);

// Code block with fence info
$ast = $p->toAst("```php\necho 1;\n```\n");
$code = $ast['children'][0];
var_dump($code['type']);
var_dump($code['info']);
var_dump($code['literal']);

// Ordered list
$ast = $p->toAst("1. one\n2. two\n");
$list = $ast['children'][0];
var_dump($list['type']);
var_dump($list['list_type']);
var_dump($list['list_start']);
var_dump($list['list_tight']);
var_dump($list['list_delim']);

// GFM table with alignments
$ast = $p->toAst("| a | b |\n|:--|--:|\n| 1 | 2 |\n");
$table = $ast['children'][0];
var_dump($table['type']);
var_dump($table['alignments']);
var_dump($table['children'][0]['type']); // table_header
var_dump($table['children'][1]['type']); // table_row
var_dump($table['children'][1]['is_header']);

// Tasklist items
$ast = $p->toAst("- [ ] todo\n- [x] done\n");
$items = $ast['children'][0]['children'];
var_dump($items[0]['type']);
var_dump($items[0]['checked']);
var_dump($items[1]['checked']);

// Sourcepos option adds line/column info
$sp = new MdParser\Parser(new MdParser\Options(sourcepos: true));
$ast = $sp->toAst("# hi\n");
$heading = $ast['children'][0];
var_dump($heading['start_line']);
var_dump($heading['start_column']);
var_dump($heading['end_line']);
var_dump($heading['end_column']);

// Without sourcepos, those keys are absent
$ast = $p->toAst("# hi\n");
var_dump(array_key_exists('start_line', $ast['children'][0]));

// Thematic break: no fields, no children
$ast = $p->toAst("---\n");
$hr = $ast['children'][0];
var_dump($hr['type']);
var_dump(array_key_exists('children', $hr));

// Strikethrough
$ast = $p->toAst("before ~~strike~~ after");
$para = $ast['children'][0];
var_dump($para['children'][1]['type']);
var_dump($para['children'][1]['children'][0]['literal']);
?>
--EXPECT--
string(8) "document"
string(7) "heading"
int(1)
string(4) "text"
string(5) "Hello"
string(4) "link"
string(19) "https://example.com"
string(8) "the site"
string(7) "example"
string(10) "code_block"
string(3) "php"
string(8) "echo 1;
"
string(4) "list"
string(7) "ordered"
int(1)
bool(true)
string(6) "period"
string(5) "table"
array(2) {
  [0]=>
  string(4) "left"
  [1]=>
  string(5) "right"
}
string(12) "table_header"
string(9) "table_row"
bool(false)
string(8) "tasklist"
bool(false)
bool(true)
int(1)
int(1)
int(1)
int(4)
bool(false)
string(14) "thematic_break"
bool(false)
string(13) "strikethrough"
string(6) "strike"
