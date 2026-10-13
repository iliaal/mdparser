--TEST--
full-reference link labels cannot pair span marks with surrounding text
--EXTENSIONS--
mdparser
--FILE--
<?php

$cases = [
    [new MdParser\Parser(), "[a][*b] c*\n\n[*b]: /url", '<p><a href="/url">a</a> c*</p>', ' c*'],
    [new MdParser\Parser(new MdParser\Options(highlight: true)), "[a][==b] c==\n\n[==b]: /url", '<p><a href="/url">a</a> c==</p>', ' c=='],
];

foreach ($cases as [$parser, $markdown, $expectedHtml, $expectedLiteral]) {
    echo $parser->toHtml($markdown) === "$expectedHtml\n" ? "html: OK\n" : "html: FAIL\n";

    $xml = $parser->toXml($markdown);
    $xmlOk = str_contains($xml, '>' . htmlspecialchars($expectedLiteral) . '</text>');
    if (class_exists(DOMDocument::class)) {
        $document = new DOMDocument();
        $xmlOk = $xmlOk && $document->loadXML($xml);
    }
    echo $xmlOk ? "xml: OK\n" : "xml: FAIL\n";

    $ast = $parser->toAst($markdown);
    echo $ast['children'][0]['children'][1]['literal'] === $expectedLiteral
        ? "ast: OK\n"
        : "ast: FAIL\n";
}

?>
--EXPECT--
html: OK
xml: OK
ast: OK
html: OK
xml: OK
ast: OK
