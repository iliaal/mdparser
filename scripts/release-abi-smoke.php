<?php

declare(strict_types=1);

function fail(string $message): never
{
    fwrite(STDERR, $message . "\n");
    exit(1);
}

$expectedVersion = getenv('MDPARSER_RELEASE_TAG');
$expectedMinor = getenv('MDPARSER_PHP_MINOR');
if ($expectedVersion === false || $expectedMinor === false) {
    fail('MDPARSER_RELEASE_TAG and MDPARSER_PHP_MINOR must be set');
}
if (PHP_MAJOR_VERSION . '.' . PHP_MINOR_VERSION !== $expectedMinor) {
    fail('PHP ' . PHP_VERSION . " is not a {$expectedMinor} release");
}
if (phpversion('mdparser') !== $expectedVersion) {
    fail('mdparser version mismatch: ' . var_export(phpversion('mdparser'), true));
}

$parser = new MdParser\Parser();

// Each output buffer grows through smart_str reallocation well past 64 KiB, and
// the invalid byte routes the input through the validateUtf8 copy.
$paragraphs = 2000;
$source = str_repeat("Paragraph with *emphasis* and `code` \xff here.\n\n", $paragraphs);

$html = $parser->toHtml($source);
if (strlen($html) < 65536
    || substr_count($html, '<p>') !== $paragraphs
    || substr_count($html, "\u{FFFD}") !== $paragraphs) {
    fail('HTML smoke test failed: ' . strlen($html) . ' bytes');
}

$xml = $parser->toXml($source);
if (strlen($xml) < 65536 || substr_count($xml, '<paragraph>') !== $paragraphs) {
    fail('XML smoke test failed: ' . strlen($xml) . ' bytes');
}

$ast = $parser->toAst($source);
if ($ast['type'] !== 'document' || count($ast['children']) !== $paragraphs) {
    fail('AST smoke test failed');
}

printf("OK: mdparser %s on PHP %s (%d HTML bytes)\n", $expectedVersion, PHP_VERSION, strlen($html));
