--TEST--
headingAnchors includes LaTeX text in heading slugs
--EXTENSIONS--
mdparser
--FILE--
<?php

$parser = new MdParser\Parser(new MdParser\Options(
    headingAnchors: true,
    latexMath: true,
));

$cases = [
    'math-only' => ['# $x$', 'x', '<span class="math">x</span>'],
    'display-math' => ['## $$E=mc^2$$', 'emc2', '<span class="math display">E=mc^2</span>'],
    'mixed' => ['### alpha $x^2$ beta', 'alpha-x2-beta', '<span class="math">x^2</span>'],
    'plain' => ['# Plain', 'plain', 'Plain'],
    'code' => ['# `code`', 'code', '<code>code</code>'],
    'empty-slug' => ['# $!!!$', null, '<span class="math">!!!</span>'],
];

foreach ($cases as $name => [$input, $expectedId, $expectedBody]) {
    $html = $parser->toHtml($input);
    $hasExpectedId = $expectedId === null
        ? !str_contains($html, ' id=')
        : str_contains($html, ' id="' . $expectedId . '"');
    printf("%s: %s\n", $name,
        $hasExpectedId && str_contains($html, $expectedBody) ? 'ok' : 'fail');
}

?>
--EXPECT--
math-only: ok
display-math: ok
mixed: ok
plain: ok
code: ok
empty-slug: ok
