<?php
/**
 * Demonstrates Options: smart punctuation, hard breaks, sourcepos.
 *
 * Run it:
 *     php -d extension=mdparser.so examples/02-options.php
 */

declare(strict_types=1);

use MdParser\Parser;
use MdParser\Options;

if (!extension_loaded('mdparser')) {
    fwrite(STDERR, "mdparser extension not loaded. Use -d extension=mdparser.so\n");
    exit(1);
}

$markdown = "He said \"hello\" -- that's --- unusual.\n";

echo "=== default (no smart) ===\n";
echo (new Parser())->toHtml($markdown);

echo "\n=== smart: true ===\n";
echo (new Parser(new Options(smart: true)))->toHtml($markdown);

echo "\n=== hardbreaks: true ===\n";
echo (new Parser(new Options(hardbreaks: true)))
    ->toHtml("line one\nline two");

echo "\n=== sourcepos: true ===\n";
echo (new Parser(new Options(sourcepos: true)))
    ->toHtml("# heading\n\nparagraph");

echo "\n=== all-off (disable every GFM extension) ===\n";
$strict = new Parser(new Options(
    tables: false,
    strikethrough: false,
    tasklist: false,
    autolink: false,
    tagfilter: false,
));
echo $strict->toHtml("| a | b |\n|---|---|\n| 1 | 2 |\n");
// Without tables, the pipe syntax becomes plain text.
