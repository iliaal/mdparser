<?php
/**
 * The simplest possible use of mdparser: parse a markdown string and
 * print the HTML.
 *
 * Run it:
 *     php -d extension=mdparser.so examples/01-basic.php
 */

declare(strict_types=1);

if (!extension_loaded('mdparser')) {
    fwrite(STDERR, "mdparser extension not loaded. Use -d extension=mdparser.so\n");
    exit(1);
}

$parser = new MdParser\Parser();

$markdown = <<<MD
# Hello, mdparser!

A short paragraph with *em* and **strong** and `code`.

- a bullet
- another one
MD;

echo $parser->toHtml($markdown);
