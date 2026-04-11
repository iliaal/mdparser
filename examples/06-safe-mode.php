<?php
/**
 * Demonstrates safe mode (default) vs unsafe mode. The markdown
 * input contains several XSS attack vectors; the default parser
 * neutralizes all of them. `unsafe: true` lets them through, which
 * you should only use for trusted input.
 *
 * Run it:
 *     php -d extension=mdparser.so examples/06-safe-mode.php
 */

declare(strict_types=1);

use MdParser\Parser;
use MdParser\Options;

if (!extension_loaded('mdparser')) {
    fwrite(STDERR, "mdparser extension not loaded. Use -d extension=mdparser.so\n");
    exit(1);
}

$attackMarkdown = <<<MD
## Link vectors

[Basic](javascript:alert(1))

[Mixed case](JaVaScRiPt:alert(1))

[data text/html](data:text/html,<script>alert(1)</script>)

[data image/png — legit](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABAQMAAAAl21bKAAAAA1BMVEUAAACnej3aAAAAAXRSTlMAQObYZgAAAApJREFUCNdjYAAAAAIAAeIhvDMAAAAASUVORK5CYII=)

## Inline HTML

before <script>alert(1)</script> after

<iframe src=javascript:alert(1)></iframe>

## Legit content

A [good link](https://example.com) and a [mailto](mailto:x@y.z).
MD;

$safe = new Parser();
$unsafe = new Parser(new Options(unsafe: true));
$unsafeNoFilter = new Parser(new Options(unsafe: true, tagfilter: false));

echo "=== DEFAULT SAFE MODE ===\n";
echo $safe->toHtml($attackMarkdown);

echo "\n=== UNSAFE MODE (tagfilter still active) ===\n";
echo $unsafe->toHtml($attackMarkdown);

echo "\n=== UNSAFE + NO TAGFILTER (maximum trust, minimum safety) ===\n";
echo $unsafeNoFilter->toHtml($attackMarkdown);

echo "\n---\n";
echo "If you're rendering untrusted user content, stick with the default.\n";
echo "If you're rendering your own content, unsafe=true is fine.\n";
echo "Only disable the tag filter if you fully control the input.\n";
