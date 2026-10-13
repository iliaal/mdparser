--TEST--
headingAnchors deduplication stays bounded for colliding slugs
--EXTENSIONS--
mdparser
--INI--
memory_limit=256M
--FILE--
<?php

function headings(int $bits, bool $colliding): array {
    $markdown = '';
    $slugs = [];
    for ($i = 0, $count = 1 << $bits; $i < $count; $i++) {
        $slug = '';
        for ($bit = 0; $bit < $bits; $bit++) {
            $slug .= ($i >> $bit) & 1 ? ($colliding ? 'c0' : 'cx') : 'ar';
        }
        $markdown .= "# $slug\n";
        $slugs[] = $slug;
    }
    return [$markdown, $slugs];
}

$parser = new MdParser\Parser(new MdParser\Options(headingAnchors: true));
[$control, ] = headings(13, false);
[$colliding, $expected] = headings(13, true);

$start = hrtime(true);
$parser->toHtml($control);
$controlNs = hrtime(true) - $start;

$start = hrtime(true);
$html = $parser->toHtml($colliding);
$collisionNs = hrtime(true) - $start;

preg_match_all('/<h1 id="([^"]+)">/', $html, $matches);
echo ($matches[1] === array_map(static fn(string $slug): string => 'user-content-' . $slug, $expected) ? 'OK' : 'FAIL'),
    ": colliding headings keep distinct IDs\n";
echo ($collisionNs < $controlNs * 8 + 100_000_000 ? 'OK' : 'FAIL'),
    ": collision cost stays bounded\n";

$html = $parser->toHtml("# x\n# x-1\n# x\n# x\n# 日本語\n# 日本語\n");
preg_match_all('/<h1 id="([^"]+)">/', $html, $matches);
echo ($matches[1] === ['user-content-x', 'user-content-x-1', 'user-content-x-2', 'user-content-x-3', 'user-content-日本語', 'user-content-日本語-1'] ? 'OK' : 'FAIL'),
    ": authored suffixes and UTF-8 headings remain distinct\n";
?>
--EXPECT--
OK: colliding headings keep distinct IDs
OK: collision cost stays bounded
OK: authored suffixes and UTF-8 headings remain distinct
