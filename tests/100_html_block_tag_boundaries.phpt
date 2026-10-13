--TEST--
HTML block tag names require a boundary and longer type 6 names remain eligible
--EXTENSIONS--
mdparser
--FILE--
<?php

$parser = new MdParser\Parser(new MdParser\Options(unsafe: true, tagfilter: false));

foreach (['<prefix>', '<scripts>', '<pre-x>', '<stylex>', '<textareas>'] as $tag) {
    $markdown = "$tag\n\n# Heading\n";
    $expected = "$tag\n<h1>Heading</h1>\n";
    echo "type 1 $tag: ", $parser->toHtml($markdown) === $expected ? 'OK' : 'FAIL', "\n";
}

$selfClosingType1 = "<pre/>\n\n# Heading\n";
echo 'type 1 <pre/>: ', $parser->toHtml($selfClosingType1) === "<p><pre/></p>\n<h1>Heading</h1>\n" ? 'OK' : 'FAIL', "\n";

$markdown = "<pre>\n\n# Heading\n";
echo 'type 1 valid: ', $parser->toHtml($markdown) === $markdown ? 'OK' : 'FAIL', "\n";

foreach (['pre', 'script', 'style', 'textarea', 'PRE'] as $tag) {
    $markdown = "<$tag/>\n*emphasis*\n";
    $expected = "<p><$tag/>\n<em>emphasis</em></p>\n";
    echo "type 7 excluded <$tag/>: ", $parser->toHtml($markdown) === $expected ? 'OK' : 'FAIL', "\n";
}

foreach (['thead', 'header', 'link', 'param', 'track', 'colgroup', 'basefont', 'frameset', 'menuitem'] as $tag) {
    $markdown = "para\n<$tag>\n*x*\n";
    $expected = "<p>para</p>\n<$tag>\n*x*\n";
    echo "type 6 $tag: ", $parser->toHtml($markdown) === $expected ? 'OK' : 'FAIL', "\n";
}

$closing = "para\n</thead>\n*x*\n";
echo 'type 6 closing: ', $parser->toHtml($closing) === "<p>para</p>\n</thead>\n*x*\n" ? 'OK' : 'FAIL', "\n";

$selfClosing = "para\n<thead/>\n*x*\n";
echo 'type 6 self-closing: ', $parser->toHtml($selfClosing) === "<p>para</p>\n<thead/>\n*x*\n" ? 'OK' : 'FAIL', "\n";

$unlisted = "para\n<theader>\n*x*\n";
echo 'type 6 nonlisted: ', $parser->toHtml($unlisted) === "<p>para\n<theader>\n<em>x</em></p>\n" ? 'OK' : 'FAIL', "\n";

$prefix = "<prefix>\n\n# Heading\n";
echo 'safe mode type 1: ', (new MdParser\Parser())->toHtml($prefix) === "&lt;prefix&gt;\n<h1>Heading</h1>\n" ? 'OK' : 'FAIL', "\n";
echo 'AST type 1: ', array_column($parser->toAst($prefix)['children'], 'type') === ['html_block', 'heading'] ? 'OK' : 'FAIL', "\n";
echo 'XML type 1: ', str_contains($parser->toXml($prefix), '<heading level="1">') ? 'OK' : 'FAIL', "\n";

$thead = "para\n<thead>\n*x*\n";
echo 'AST type 6: ', array_column($parser->toAst($thead)['children'], 'type') === ['paragraph', 'html_block'] ? 'OK' : 'FAIL', "\n";
echo 'XML type 6: ', str_contains($parser->toXml($thead), '<html_block xml:space="preserve">') ? 'OK' : 'FAIL', "\n";

?>
--EXPECT--
type 1 <prefix>: OK
type 1 <scripts>: OK
type 1 <pre-x>: OK
type 1 <stylex>: OK
type 1 <textareas>: OK
type 1 <pre/>: OK
type 1 valid: OK
type 7 excluded <pre/>: OK
type 7 excluded <script/>: OK
type 7 excluded <style/>: OK
type 7 excluded <textarea/>: OK
type 7 excluded <PRE/>: OK
type 6 thead: OK
type 6 header: OK
type 6 link: OK
type 6 param: OK
type 6 track: OK
type 6 colgroup: OK
type 6 basefont: OK
type 6 frameset: OK
type 6 menuitem: OK
type 6 closing: OK
type 6 self-closing: OK
type 6 nonlisted: OK
safe mode type 1: OK
AST type 1: OK
XML type 1: OK
AST type 6: OK
XML type 6: OK
