--TEST--
Returned HTML/XML strings carry no reserve slack; repeated AST leaves share storage
--EXTENSIONS--
mdparser
--SKIPIF--
<?php
/* run-tests --asan keeps byte-exact accounting through the tracked allocator;
 * plain USE_ZEND_ALLOC=0 (valgrind -m) reports no usage at all. */
if (getenv('USE_ZEND_ALLOC') === '0' && getenv('USE_TRACKED_ALLOC') !== '1') {
    print 'skip needs Zend MM or the tracked allocator';
}
?>
--FILE--
<?php
$p = new MdParser\Parser();

/* A code block renders to roughly its input size, well under the 1.25x (HTML)
 * and 2x (XML) output reserves. Before trimming, the returned strings pinned
 * the whole reserve (~28 KB and ~100 KB here). */
$code = "```\n" . str_repeat("plain code line without specials\n", 3000) . "```\n";
foreach (['toHtml', 'toXml'] as $m) {
    $before = memory_get_usage();
    $out = $p->$m($code);
    $slack = memory_get_usage() - $before - strlen($out);
    echo $m, ': ', $slack < 8192 ? 'trimmed' : "slack $slack", "\n";
    unset($out);
}

/* 10,000 one-word lines: 10,000 text nodes and 9,999 softbreaks. With a
 * separate array per softbreak this retained ~870 B per line; sharing one
 * refcounted leaf keeps it near 460 B (~600 B on a debug build). */
$lines = implode("\n", array_fill(0, 10000, 'word'));
$before = memory_get_usage();
$ast = $p->toAst($lines);
$per_line = (memory_get_usage() - $before) / 10000;
echo 'ast: ', $per_line < 750 ? 'compact' : "$per_line B/line", "\n";

$kids = $ast['children'][0]['children'];
var_dump(count($kids), $kids[1], $kids[1] === $kids[3]);

/* A shared leaf still behaves as an independent value. */
$kids[1]['type'] = 'changed';
var_dump($kids[3]['type'], $ast['children'][0]['children'][1]['type']);
?>
--EXPECT--
toHtml: trimmed
toXml: trimmed
ast: compact
int(19999)
array(1) {
  ["type"]=>
  string(9) "softbreak"
}
bool(true)
string(9) "softbreak"
string(9) "softbreak"
