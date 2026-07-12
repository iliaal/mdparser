--TEST--
SmartyPants uses decoded entity characters as quote context
--EXTENSIONS--
mdparser
--FILE--
<?php

$p = new MdParser\Parser(new MdParser\Options(smart: true));

$cases = [
    'decimal less-than' => ['&#60;"hello"', '<p>&lt;“hello”</p>'],
    'hex less-than' => ['&#x3c;"hello"', '<p>&lt;“hello”</p>'],
    'named less-than' => ['&lt;"hello"', '<p>&lt;“hello”</p>'],
    'named quote' => ['&quot;"hello"', '<p>&quot;“hello”</p>'],
    'decimal space' => ['&#32;"hello"', '<p> “hello”</p>'],
];

foreach ($cases as $label => [$markdown, $expected]) {
    $html = trim($p->toHtml($markdown));
    echo ($html === $expected ? 'OK' : 'FAIL'), ": $label\n";
}

?>
--EXPECT--
OK: decimal less-than
OK: hex less-than
OK: named less-than
OK: named quote
OK: decimal space
