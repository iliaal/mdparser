--TEST--
wikilink callbacks cannot bypass the disabled wikiLinks option
--EXTENSIONS--
mdparser
--FILE--
<?php

function check(string $label, bool $condition): void {
    echo ($condition ? 'OK' : 'FAIL'), ": $label\n";
}

$github = MdParser\Options::github();
$parser = new MdParser\Parser($github);
$source = '[^javascript:alert(1)]()';
$html = $parser->toHtml($source);
$inline = $parser->toInlineHtml($source);
$xml = $parser->toXml($source);
$ast = json_encode($parser->toAst($source), JSON_THROW_ON_ERROR);

check('github preset leaves wikiLinks disabled', $github->wikiLinks === false);
check('HTML omits an unexpected wikilink', !str_contains($html, 'class="wikilink"'));
check('inline HTML omits an unexpected wikilink', !str_contains($inline, 'class="wikilink"'));
check('XML omits an unexpected wikilink', !str_contains($xml, '<wikilink'));
check('AST omits an unexpected wikilink', !str_contains($ast, '"type":"wikilink"'));
check('AST omits the fabricated destination', !str_contains($ast, '"url":"javascript:alert(1)"'));
check('label remains visible', str_contains($html, 'javascript:alert(1)'));

$enabled = new MdParser\Parser(new MdParser\Options(wikiLinks: true));
check('enabled HTML still renders wikilinks',
    str_contains($enabled->toHtml('[[target|label]]'), 'class="wikilink" href="target"'));
check('enabled XML still renders wikilinks',
    str_contains($enabled->toXml('[[target|label]]'), '<wikilink destination="target">'));
check('enabled AST still renders wikilinks',
    $enabled->toAst('[[target|label]]')['children'][0]['children'][0]['type'] === 'wikilink');

?>
--EXPECT--
OK: github preset leaves wikiLinks disabled
OK: HTML omits an unexpected wikilink
OK: inline HTML omits an unexpected wikilink
OK: XML omits an unexpected wikilink
OK: AST omits an unexpected wikilink
OK: AST omits the fabricated destination
OK: label remains visible
OK: enabled HTML still renders wikilinks
OK: enabled XML still renders wikilinks
OK: enabled AST still renders wikilinks
