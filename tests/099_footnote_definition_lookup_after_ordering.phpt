--TEST--
Footnote references inside definitions resolve after definitions are ordered by first reference
--EXTENSIONS--
mdparser
--FILE--
<?php
$parser = new MdParser\Parser(new MdParser\Options(footnotes: true));
$reordered = "x[^b] y[^a]\n\n[^a]: alpha\n[^b]: beta see [^a]\n";
$alreadyOrdered = "x[^a] y[^b]\n\n[^a]: alpha\n[^b]: beta see [^a]\n";

$html = $parser->toHtml($reordered);
echo 'reordered HTML nested reference: ',
    str_contains($html, 'beta see <sup><a href="#fn-2" id="fnref-2-2">2</a></sup>') ? 'yes' : 'no', "\n";
echo 'reordered HTML definition order: ',
    strpos($html, '<li id="fn-1">') < strpos($html, '<li id="fn-2">') ? 'yes' : 'no', "\n";

$xml = $parser->toXml($reordered);
echo 'reordered XML nested reference: ',
    substr_count($xml, '<footnote_reference id="2">') === 2 ? 'yes' : 'no', "\n";

$ast = $parser->toAst($reordered);
$firstDefinition = $ast['children'][1] ?? [];
echo 'reordered AST nested reference: ',
    ($firstDefinition['type'] ?? null) === 'footnote_definition'
    && ($firstDefinition['literal'] ?? null) === '1'
    && ($firstDefinition['children'][0]['literal'] ?? null) === 'beta see '
    && ($firstDefinition['children'][1]['type'] ?? null) === 'footnote_reference'
    && ($firstDefinition['children'][1]['literal'] ?? null) === '2' ? 'yes' : 'no', "\n";

$control = $parser->toHtml($alreadyOrdered);
echo 'already ordered HTML nested reference: ',
    str_contains($control, 'beta see <sup><a href="#fn-1" id="fnref-1-2">1</a></sup>') ? 'yes' : 'no', "\n";

$nestedOnly = "x[^b]\n\n[^a]: unused\n[^b]: beta see [^c]\n[^c]: gamma\n";
$html = $parser->toHtml($nestedOnly);
echo 'nested-only HTML target emitted: ',
    str_contains($html, 'href="#fn-2"') && str_contains($html, '<li id="fn-2">') ? 'yes' : 'no', "\n";

$xml = $parser->toXml($nestedOnly);
echo 'nested-only XML definition emitted: ',
    str_contains($xml, '<footnote_definition id="2">') ? 'yes' : 'no', "\n";

$ast = $parser->toAst($nestedOnly);
echo 'nested-only AST definition emitted: ',
    ($ast['children'][2]['type'] ?? null) === 'footnote_definition'
    && ($ast['children'][2]['literal'] ?? null) === '2'
    && ($ast['children'][2]['children'][0]['literal'] ?? null) === 'gamma' ? 'yes' : 'no', "\n";
?>
--EXPECT--
reordered HTML nested reference: yes
reordered HTML definition order: yes
reordered XML nested reference: yes
reordered AST nested reference: yes
already ordered HTML nested reference: yes
nested-only HTML target emitted: yes
nested-only XML definition emitted: yes
nested-only AST definition emitted: yes
