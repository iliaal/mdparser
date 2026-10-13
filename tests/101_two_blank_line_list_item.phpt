--TEST--
Two blank lines do not eject content from a nonempty list item
--EXTENSIONS--
mdparser
--FILE--
<?php

$parser = new MdParser\Parser();
$inside = "<ul>\n<li>\n<p>a</p>\n<p>b</p>\n</li>\n</ul>\n";
$xmlItem = "  <list type=\"bullet\" tight=\"false\">\n    <item>\n"
    . "      <paragraph>\n        <text xml:space=\"preserve\">a</text>\n      </paragraph>\n"
    . "      <paragraph>\n        <text xml:space=\"preserve\">b</text>\n      </paragraph>\n"
    . "    </item>\n  </list>\n";

function astHasItemParagraphs(array $ast, int $listIndex): bool
{
    $nodes = $ast['children'] ?? [];
    if (count($nodes) !== $listIndex + 1) {
        return false;
    }
    $list = $nodes[$listIndex];
    $item = $list['children'][0] ?? [];
    $blocks = $item['children'] ?? [];
    return ($list['type'] ?? null) === 'list'
        && ($list['list_tight'] ?? null) === false
        && count($list['children'] ?? []) === 1
        && ($item['type'] ?? null) === 'item'
        && count($blocks) === 2
        && ($blocks[0]['type'] ?? null) === 'paragraph'
        && count($blocks[0]['children'] ?? []) === 1
        && ($blocks[0]['children'][0]['literal'] ?? null) === 'a'
        && ($blocks[1]['type'] ?? null) === 'paragraph'
        && count($blocks[1]['children'] ?? []) === 1
        && ($blocks[1]['children'][0]['literal'] ?? null) === 'b';
}

foreach ([258, 259, 260, 516, 1028] as $offset) {
    $padding = str_repeat('x', $offset - 4);
    $markdown = $padding . "\n\n- a\n\n\n  b\n";
    $prefix = "<p>$padding</p>\n";
    $html = $parser->toHtml($markdown);
    echo "$offset html: ", str_starts_with($html, $prefix)
        && substr($html, strlen($prefix)) === $inside ? 'inside' : 'outside', "\n";
    echo "$offset xml: ", str_contains($parser->toXml($markdown), $xmlItem) ? 'inside' : 'outside', "\n";
    echo "$offset ast: ", astHasItemParagraphs($parser->toAst($markdown), 1) ? 'inside' : 'outside', "\n";
}

$minimal = "\n\n- a\n\n\n  b\n";
echo 'minimal html: ', $parser->toHtml($minimal) === $inside ? 'inside' : 'outside', "\n";
echo 'minimal xml: ', str_contains($parser->toXml($minimal), $xmlItem) ? 'inside' : 'outside', "\n";
echo 'minimal ast: ', astHasItemParagraphs($parser->toAst($minimal), 0) ? 'inside' : 'outside', "\n";

$markdown = str_repeat('x', 256) . "\n\n- a\n\n\n  b\n";

$empty = "<ul>\n<li></li>\n</ul>\n<p>b</p>\n";
echo 'empty: ', $parser->toHtml("-\n\n\n  b\n") === $empty ? 'ended' : 'continued', "\n";
$secondEmpty = "<ul>\n<li>a</li>\n<li></li>\n</ul>\n<p>b</p>\n";
echo 'second empty: ', $parser->toHtml("- a\n-\n\n\n  b\n") === $secondEmpty ? 'ended' : 'continued', "\n";

$referenceOnly = "-\n  [ref]: /url\n\n\n  b\n";
$referenceHtml = "<ul>\n<li>\n<p>b</p>\n</li>\n</ul>\n";
$referenceXmlItem = "    <item>\n      <paragraph>\n"
    . "        <text xml:space=\"preserve\">b</text>\n      </paragraph>\n    </item>\n";
$referenceAst = $parser->toAst($referenceOnly);
$referenceList = $referenceAst['children'][0] ?? [];
$referenceItem = $referenceList['children'][0] ?? [];
$referenceParagraph = $referenceItem['children'][0] ?? [];
echo 'reference-only html: ', $parser->toHtml($referenceOnly) === $referenceHtml ? 'inside' : 'outside', "\n";
echo 'reference-only xml: ', str_contains($parser->toXml($referenceOnly), $referenceXmlItem)
    ? 'inside' : 'outside', "\n";
echo 'reference-only ast: ', count($referenceAst['children'] ?? []) === 1
    && ($referenceList['type'] ?? null) === 'list'
    && count($referenceList['children'] ?? []) === 1
    && ($referenceItem['type'] ?? null) === 'item'
    && count($referenceItem['children'] ?? []) === 1
    && ($referenceParagraph['type'] ?? null) === 'paragraph'
    && ($referenceParagraph['children'][0]['literal'] ?? null) === 'b' ? 'inside' : 'outside', "\n";

$preserving = new MdParser\Parser(new MdParser\Options(preserveBlankLines: true));
$ast = $preserving->toAst($markdown);
$nodes = $ast['children'] ?? [];
$list = $nodes[2] ?? [];
$item = $list['children'][0] ?? [];
$blocks = $item['children'] ?? [];
echo 'preserve blanks: ', count($nodes) === 3
    && ($nodes[1]['type'] ?? null) === 'blank'
    && ($list['type'] ?? null) === 'list'
    && count($list['children'] ?? []) === 1
    && ($item['type'] ?? null) === 'item'
    && count($blocks) === 3
    && ($blocks[0]['literal'] ?? null) === 'a'
    && ($blocks[1]['type'] ?? null) === 'blank'
    && ($blocks[2]['literal'] ?? null) === 'b' ? 'inside' : 'outside', "\n";

$nestedEmpty = "- -\n\n\n    b\n";
$nestedHtml = "<ul>\n<li>\n<ul>\n<li></li>\n</ul>\n<p>b</p>\n</li>\n</ul>\n";
$nestedXmlItem = "    <item>\n      <list type=\"bullet\" tight=\"true\">\n"
    . "        <item>\n        </item>\n      </list>\n"
    . "      <paragraph>\n        <text xml:space=\"preserve\">b</text>\n"
    . "      </paragraph>\n    </item>\n";
$nestedAst = $parser->toAst($nestedEmpty);
$outerList = $nestedAst['children'][0] ?? [];
$outerItem = $outerList['children'][0] ?? [];
$nestedBlocks = $outerItem['children'] ?? [];
$innerList = $nestedBlocks[0] ?? [];
$innerItem = $innerList['children'][0] ?? [];
$outerParagraph = $nestedBlocks[1] ?? [];
echo 'nested empty html: ', $parser->toHtml($nestedEmpty) === $nestedHtml ? 'inside outer item' : 'changed', "\n";
echo 'nested empty xml: ', str_contains($parser->toXml($nestedEmpty), $nestedXmlItem)
    ? 'inside outer item' : 'changed', "\n";
echo 'nested empty ast: ', count($nestedAst['children'] ?? []) === 1
    && ($outerList['type'] ?? null) === 'list'
    && ($outerList['list_tight'] ?? null) === false
    && count($outerList['children'] ?? []) === 1
    && ($outerItem['type'] ?? null) === 'item'
    && count($nestedBlocks) === 2
    && ($innerList['type'] ?? null) === 'list'
    && ($innerList['list_tight'] ?? null) === true
    && count($innerList['children'] ?? []) === 1
    && ($innerItem['type'] ?? null) === 'item'
    && count($innerItem['children'] ?? []) === 0
    && ($outerParagraph['type'] ?? null) === 'paragraph'
    && count($outerParagraph['children'] ?? []) === 1
    && ($outerParagraph['children'][0]['literal'] ?? null) === 'b'
    ? 'inside outer item' : 'changed', "\n";

foreach ([
    'quote-list' => ["> - a\n>\n>\n>   b\n", "<blockquote>\n<ul>\n<li>\n<p>a</p>\n<p>b</p>\n</li>\n</ul>\n</blockquote>\n"],
    'nested-list' => ["- - a\n\n\n    b\n", "<ul>\n<li>\n<ul>\n<li>\n<p>a</p>\n<p>b</p>\n</li>\n</ul>\n</li>\n</ul>\n"],
    'list-quote' => ["- > a\n  >\n  >\n  > b\n", "<ul>\n<li>\n<blockquote>\n<p>a</p>\n<p>b</p>\n</blockquote>\n</li>\n</ul>\n"],
    'sibling-after-nested' => ["- - a\n-\n\n\n  b\n", "<ul>\n<li>\n<ul>\n<li>a</li>\n</ul>\n</li>\n<li></li>\n</ul>\n<p>b</p>\n"],
] as $name => [$source, $expected]) {
    echo "$name: ", $parser->toHtml($source) === $expected ? 'stable' : 'changed', "\n";
}

foreach ([4, 5] as $depth) {
    $prefix = str_repeat('> ', $depth);
    $source = $prefix . "-\n" . str_repeat(rtrim($prefix) . "\n", 2) . $prefix . "  b\n";
    $expected = str_repeat("<blockquote>\n", $depth)
        . "<ul>\n<li></li>\n</ul>\n<p>b</p>\n"
        . str_repeat("</blockquote>\n", $depth);
    echo "nested quote $depth html: ", $parser->toHtml($source) === $expected ? 'stable' : 'changed', "\n";

    $xml = $parser->toXml($source);
    echo "nested quote $depth xml: ", substr_count($xml, '<block_quote>') === $depth
        && str_contains($xml, '<text xml:space="preserve">b</text>')
        && !str_contains($xml, '<code_block') ? 'stable' : 'changed', "\n";

    $node = $parser->toAst($source);
    for ($i = 0; $i < $depth; $i++) {
        $children = $node['children'] ?? [];
        if (count($children) !== 1 || ($children[0]['type'] ?? null) !== 'block_quote') {
            break;
        }
        $node = $children[0];
    }
    $children = $node['children'] ?? [];
    echo "nested quote $depth ast: ", $i === $depth
        && count($children) === 2
        && ($children[0]['type'] ?? null) === 'list'
        && ($children[0]['children'][0]['type'] ?? null) === 'item'
        && ($children[1]['type'] ?? null) === 'paragraph'
        && ($children[1]['children'][0]['literal'] ?? null) === 'b'
        ? 'stable' : 'changed', "\n";
}

?>
--EXPECT--
258 html: inside
258 xml: inside
258 ast: inside
259 html: inside
259 xml: inside
259 ast: inside
260 html: inside
260 xml: inside
260 ast: inside
516 html: inside
516 xml: inside
516 ast: inside
1028 html: inside
1028 xml: inside
1028 ast: inside
minimal html: inside
minimal xml: inside
minimal ast: inside
empty: ended
second empty: ended
reference-only html: inside
reference-only xml: inside
reference-only ast: inside
preserve blanks: inside
nested empty html: inside outer item
nested empty xml: inside outer item
nested empty ast: inside outer item
quote-list: stable
nested-list: stable
list-quote: stable
sibling-after-nested: stable
nested quote 4 html: stable
nested quote 4 xml: stable
nested quote 4 ast: stable
nested quote 5 html: stable
nested quote 5 xml: stable
nested quote 5 ast: stable
