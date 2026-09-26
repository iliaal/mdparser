--TEST--
GFM table column count does not wrap through the 16-bit block bit-field
--EXTENSIONS--
mdparser
--FILE--
<?php

$parser = new MdParser\Parser(new MdParser\Options(tables: true));

function tableInput(int $columns): string
{
    return "|" . str_repeat("x|", $columns) . "\n|"
        . str_repeat("-|", $columns);
}

$ordinary = $parser->toHtml(tableInput(2));
$maximum = $parser->toHtml(tableInput(65535));
$tooWide = $parser->toHtml(tableInput(65536));
$farTooWide = $parser->toHtml(tableInput(65537));

printf("ordinary columns: %d\n", substr_count($ordinary, '<th>'));
printf("UINT16_MAX columns: %d\n", substr_count($maximum, '<th>'));
printf("UINT16_MAX + 1 rejected: %s\n", str_contains($tooWide, '<table>') ? 'no' : 'yes');
printf("UINT16_MAX + 2 rejected: %s\n", str_contains($farTooWide, '<table>') ? 'no' : 'yes');

?>
--EXPECT--
ordinary columns: 2
UINT16_MAX columns: 65535
UINT16_MAX + 1 rejected: yes
UINT16_MAX + 2 rejected: yes
