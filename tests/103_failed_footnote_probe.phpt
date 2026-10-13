--TEST--
failed footnote lookup leaves ordinary links and nested spans intact
--EXTENSIONS--
mdparser
--FILE--
<?php

function check(string $label, bool $condition): void {
    echo ($condition ? 'OK' : 'FAIL'), ": $label\n";
}

$github = new MdParser\Parser(MdParser\Options::github());
$source = '[^x](https://example.test/path)';
check('HTML keeps the supplied destination and caret',
    $github->toHtml($source) === "<p><a href=\"https://example.test/path\">^x</a></p>\n");
check('inline HTML keeps the supplied destination and caret',
    $github->toInlineHtml($source) === '<a href="https://example.test/path">^x</a>');

$xml = $github->toXml($source);
check('XML emits the original link destination',
    str_contains($xml, '<link destination="https://example.test/path" title="">')
    && str_contains($xml, '<text xml:space="preserve">^x</text>'));

$ast = $github->toAst($source);
$link = $ast['children'][0]['children'][0] ?? [];
check('AST emits an ordinary link with the original destination',
    ($link['type'] ?? null) === 'link'
    && ($link['url'] ?? null) === 'https://example.test/path'
    && ($link['children'][0]['literal'] ?? null) === '^x');

$superscript = new MdParser\Parser(new MdParser\Options(footnotes: true, superscript: true));
$source = '[^a^]()';
check('superscript HTML remains balanced',
    $superscript->toHtml($source) === "<p><a href=\"\"><sup>a</sup></a></p>\n");
try {
    $xml = $superscript->toXml($source);
} catch (MdParser\Exception $e) {
    $xml = '';
}
check('superscript XML remains balanced',
    str_contains($xml, '<link destination="" title="">')
    && str_contains($xml, '<superscript>')
    && str_contains($xml, '</superscript>'));
try {
    $ast = $superscript->toAst($source);
} catch (MdParser\Exception $e) {
    $ast = [];
}
$link = $ast['children'][0]['children'][0] ?? [];
check('superscript AST remains balanced',
    ($link['type'] ?? null) === 'link'
    && ($link['children'][0]['type'] ?? null) === 'superscript'
    && ($link['children'][0]['children'][0]['literal'] ?? null) === 'a');

$valid = $github->toHtml("note[^ok]\n\n[^ok]: body\n");
check('valid footnote references still resolve',
    str_contains($valid, 'href="#fn-1"') && str_contains($valid, 'id="fn-1"'));

$nested = "[[[^|]]]\n[^|]:";
$wikiOnly = new MdParser\Parser(new MdParser\Options(wikiLinks: true));
$wikiAndFootnotes = new MdParser\Parser(new MdParser\Options(wikiLinks: true, footnotes: true));
$expected = "<p>[<a class=\"wikilink\" href=\"%5E\">^</a>]\n[^|]:</p>\n";
check('failed probe inside wiki markup preserves bracket pairing',
    $wikiOnly->toHtml($nested) === $expected
    && $wikiAndFootnotes->toHtml($nested) === $expected);

?>
--EXPECT--
OK: HTML keeps the supplied destination and caret
OK: inline HTML keeps the supplied destination and caret
OK: XML emits the original link destination
OK: AST emits an ordinary link with the original destination
OK: superscript HTML remains balanced
OK: superscript XML remains balanced
OK: superscript AST remains balanced
OK: valid footnote references still resolve
OK: failed probe inside wiki markup preserves bracket pairing
