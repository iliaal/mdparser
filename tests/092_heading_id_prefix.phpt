--TEST--
headingAnchors prefixes IDs to avoid footnote collisions and DOM named-property clobbering
--EXTENSIONS--
mdparser
--FILE--
<?php
function check(string $name, bool $ok): void {
    echo ($ok ? 'PASS' : 'FAIL') . " $name\n";
}

$parser = new MdParser\Parser(new MdParser\Options(headingAnchors: true, footnotes: true));
$html = $parser->toHtml("# fn-1\n\nA note[^a].\n\n[^a]: Footnote text\n");
check('heading uses prefixed ID', str_contains($html, '<h1 id="user-content-fn-1">fn-1</h1>'));
check('footnote target retains its ID', str_contains($html, '<li id="fn-1">'));
check('footnote ID is unique', substr_count($html, 'id="fn-1"') === 1);
check('footnote link reaches its target', str_contains($html, 'href="#fn-1"'));

$html = $parser->toHtml("# config\n\n# config\n\n[Go to config](#user-content-config)\n");
check('common named property is prefixed', str_contains($html, '<h1 id="user-content-config">config</h1>'));
check('unprefixed config ID is absent', !str_contains($html, 'id="config"'));
check('duplicate heading gets unique prefixed ID', str_contains($html, '<h1 id="user-content-config-1">config</h1>'));
check('ordinary heading link reaches generated ID',
    str_contains($html, 'href="#user-content-config"') &&
    str_contains($html, 'id="user-content-config"'));
?>
--EXPECT--
PASS heading uses prefixed ID
PASS footnote target retains its ID
PASS footnote ID is unique
PASS footnote link reaches its target
PASS common named property is prefixed
PASS unprefixed config ID is absent
PASS duplicate heading gets unique prefixed ID
PASS ordinary heading link reaches generated ID
