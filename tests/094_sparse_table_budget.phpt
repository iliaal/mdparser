--TEST--
Sparse GFM tables stop before AST and XML cell amplification exhausts memory
--EXTENSIONS--
mdparser
--INI--
memory_limit=128M
--FILE--
<?php

$parser = new MdParser\Parser();
$header = '|' . str_repeat('h|', 32) . "\n|" . str_repeat(':-:|', 32) . "\n";
$sparse = $header . str_repeat("a\n", 5000);

foreach (['toAst', 'toXml'] as $method) {
    memory_reset_peak_usage();
    try {
        $parser->$method($sparse);
        echo "FAIL: $method accepted oversized sparse table\n";
    } catch (MdParser\Exception $e) {
        echo ($e->getMessage() === 'mdparser: table cell limit exceeded' ? 'OK' : 'FAIL'),
            ": $method raises table cell exception\n";
    }
    $bound = $method === 'toAst' ? 64 * 1024 * 1024 : 16 * 1024 * 1024;
    echo (memory_get_peak_usage() < $bound ? 'OK' : 'FAIL'),
        ": $method stays within memory bound\n";
}

$ordinary = "| a | b |\n|---|---|\n| x |\n";
$ast = $parser->toAst($ordinary);
$rows = $ast['children'][0]['children'];
echo (count($rows) === 2 && count($rows[1]['children']) === 2
    && $rows[1]['children'][0]['children'][0]['literal'] === 'x'
    && !isset($rows[1]['children'][1]['children']) ? 'OK' : 'FAIL'),
    ": ordinary sparse AST cells preserved\n";

$xml = $parser->toXml($ordinary);
echo (substr_count($xml, '<table_cell') === 4
    && str_contains($xml, '<text xml:space="preserve">x</text>') ? 'OK' : 'FAIL'),
    ": ordinary sparse XML cells preserved\n";

$dense = $header . '|' . str_repeat('x|', 32) . "\n";
$ast = $parser->toAst($dense);
$xml = $parser->toXml($dense);
echo (count($ast['children'][0]['children'][1]['children']) === 32
    && substr_count($xml, '<table_cell') === 64 ? 'OK' : 'FAIL'),
    ": dense 32-column table preserved\n";

$wide = '|' . str_repeat('h|', 33) . "\n|" . str_repeat(':-:|', 33) . "\n"
    . str_repeat("a\n", 5000);
$ast = $parser->toAst($wide);
$xml = $parser->toXml($wide);
echo ($ast['children'][0]['type'] === 'paragraph'
    && !str_contains($xml, '<table>') ? 'OK' : 'FAIL'),
    ": 33-column density control preserved\n";

?>
--EXPECT--
OK: toAst raises table cell exception
OK: toAst stays within memory bound
OK: toXml raises table cell exception
OK: toXml stays within memory bound
OK: ordinary sparse AST cells preserved
OK: ordinary sparse XML cells preserved
OK: dense 32-column table preserved
OK: 33-column density control preserved
