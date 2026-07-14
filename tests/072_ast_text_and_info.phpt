--TEST--
toAst coalesces adjacent text fragments; AST/XML retain the full code info string
--EXTENSIONS--
mdparser
--FILE--
<?php

$p = new MdParser\Parser();

function ok(string $label, bool $condition): void {
    echo ($condition ? "OK" : "FAIL"), ": $label\n";
}

$ast = $p->toAst('a&amp;b&#33;');
$children = $ast['children'][0]['children'];
ok("adjacent text callbacks coalesce",
    count($children) === 1
    && $children[0]['type'] === 'text'
    && $children[0]['literal'] === 'a&b!');

$source = "```php key=value\nx\n```\n";
$ast = $p->toAst($source);
ok("AST retains full info string",
    $ast['children'][0]['info'] === 'php key=value');
ok("XML retains full info string",
    str_contains($p->toXml($source), 'info="php key=value"'));

?>
--EXPECT--
OK: adjacent text callbacks coalesce
OK: AST retains full info string
OK: XML retains full info string
