--TEST--
toInlineHtml removes only injected line sentinels from link and image attributes
--EXTENSIONS--
mdparser
--FILE--
<?php

function eq(string $label, string $got, string $want): void {
    echo ($got === $want ? 'OK' : 'FAIL: ' . var_export($got, true)), ": $label\n";
}

$p = new MdParser\Parser();
eq('link destination', $p->toInlineHtml("[a](\n/u)"), '<a href="/u">a</a>');
eq('unsafe scheme after line break', $p->toInlineHtml("[a](\njavascript:alert(1))"), '<a href="">a</a>');
eq('link title', $p->toInlineHtml("[a](/u \"t\nx\")"), "<a href=\"/u\" title=\"t\nx\">a</a>");
eq('image source and title', $p->toInlineHtml("![i](\n/img.png \"t\nx\")"), "<img src=\"/img.png\" alt=\"i\" title=\"t\nx\" />");
eq('literal semicolon in destination', $p->toInlineHtml("[a](\n;/u)"), '<a href=";/u">a</a>');
eq('literal semicolon in title', $p->toInlineHtml("[a](/u \"t\n;x\")"), "<a href=\"/u\" title=\"t\n;x\">a</a>");
eq('multiline title starts with literal semicolon', $p->toInlineHtml("[a](/u \";x\ny\")"), "<a href=\"/u\" title=\";x\ny\">a</a>");
eq('entity semicolon in destination', $p->toInlineHtml("[a](\n&#59;/u)"), '<a href=";/u">a</a>');
eq('literal semicolon with entity', $p->toInlineHtml("[a](;&#59;/u)"), '<a href=";;/u">a</a>');
eq('entity in multiline title', $p->toInlineHtml("[a](/u \"t&amp;\nx\")"), "<a href=\"/u\" title=\"t&amp;\nx\">a</a>");

$nofollow = new MdParser\Parser(new MdParser\Options(nofollowLinks: true));
eq('fragment stays local', $nofollow->toInlineHtml("[a](\n#part)"), '<a href="#part">a</a>');

?>
--EXPECT--
OK: link destination
OK: unsafe scheme after line break
OK: link title
OK: image source and title
OK: literal semicolon in destination
OK: literal semicolon in title
OK: multiline title starts with literal semicolon
OK: entity semicolon in destination
OK: literal semicolon with entity
OK: entity in multiline title
OK: fragment stays local
