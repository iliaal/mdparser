<?php
/**
 * Enables the footnotes extension and renders a document with
 * inline references and back-references.
 *
 * Run it:
 *     php -d extension=mdparser.so examples/05-footnotes.php
 */

declare(strict_types=1);

use MdParser\Parser;
use MdParser\Options;

if (!extension_loaded('mdparser')) {
    fwrite(STDERR, "mdparser extension not loaded. Use -d extension=mdparser.so\n");
    exit(1);
}

$markdown = <<<MD
# Notes on parsers

CommonMark[^cm] and GFM[^gfm] have different feature sets. Both
descend from the original Markdown[^gruber] but they diverge on
*edge cases*.

mdparser embeds a fork of cmark-gfm[^fork] patched up to 0.31
conformance.

[^cm]: The CommonMark spec, maintained at <https://spec.commonmark.org/>.
[^gfm]: GitHub Flavored Markdown, maintained at <https://github.github.com/gfm/>.
[^gruber]: John Gruber's 2004 original, at <https://daringfireball.net/projects/markdown/>.
[^fork]: See the project README for version pins.
MD;

// Footnotes are off by default. Enable explicitly.
$parser = new Parser(new Options(footnotes: true));

echo $parser->toHtml($markdown);
