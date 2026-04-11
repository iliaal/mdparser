<?php
/**
 * Exercises the five GFM extensions mdparser ships with:
 * tables, strikethrough, task lists, autolinks, tag filter.
 *
 * Run it:
 *     php -d extension=mdparser.so examples/04-gfm-features.php
 */

declare(strict_types=1);

use MdParser\Parser;

if (!extension_loaded('mdparser')) {
    fwrite(STDERR, "mdparser extension not loaded. Use -d extension=mdparser.so\n");
    exit(1);
}

$parser = new Parser();

echo "=== GFM pipe table ===\n";
echo $parser->toHtml(<<<MD
| Feature       | Status |
|:--------------|-------:|
| Tables        | ✓      |
| Strikethrough | ✓      |
| Task lists    | ✓      |
MD);

echo "\n=== Strikethrough ===\n";
echo $parser->toHtml("Old text ~~crossed out~~, new text stays.");

echo "\n=== Task lists ===\n";
echo $parser->toHtml(<<<MD
- [x] Ship 0.1.0
- [ ] Write docs
- [ ] Submit to PECL
MD);

echo "\n=== Bare URL autolink ===\n";
echo $parser->toHtml("See https://example.com for details.");

echo "\n=== Tag filter (safe by default) ===\n";
echo $parser->toHtml("Hello <script>alert(1)</script> world.");

echo "\n=== Tag filter with unsafe=true ===\n";
$unsafe = new Parser(new MdParser\Options(unsafe: true));
echo $unsafe->toHtml("<b>OK</b> but <script>alert(1)</script> still escaped");
