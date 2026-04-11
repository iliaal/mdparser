<?php
/**
 * Print diffs for a selection of failing CommonMark spec examples. Usage:
 *     php -d extension=./modules/mdparser.so scripts/diag_spec_failures.php
 * Run from the repo root.
 */

$spec_path = __DIR__ . "/../tests/fixtures/commonmark-spec.txt";
$spec = file_get_contents($spec_path);
if ($spec === false) {
    fwrite(STDERR, "cannot read $spec_path\n");
    exit(1);
}

$lines = explode("\n", $spec);

$examples = [];
$in_example = false;
$in_markdown = false;
$markdown = [];
$html = [];
$start_line = 0;
$fence = str_repeat("`", 32) . " example";
$close = str_repeat("`", 32);

foreach ($lines as $i => $line) {
    if (!$in_example) {
        if ($line === $fence) {
            $in_example = true;
            $in_markdown = true;
            $markdown = [];
            $html = [];
            $start_line = $i + 1;
        }
        continue;
    }
    if ($line === $close) {
        $md_str = empty($markdown) ? "" : implode("\n", $markdown) . "\n";
        $html_str = empty($html) ? "" : implode("\n", $html) . "\n";
        $examples[] = [
            'md' => str_replace('→', "\t", $md_str),
            'html' => str_replace('→', "\t", $html_str),
            'line' => $start_line,
        ];
        $in_example = false;
        continue;
    }
    if ($line === ".") { $in_markdown = false; continue; }
    if ($in_markdown) $markdown[] = $line;
    else $html[] = $line;
}

echo "loaded " . count($examples) . " examples\n";

$parser = new MdParser\Parser(new MdParser\Options(
    tables: false, strikethrough: false, tasklist: false,
    autolink: false, tagfilter: false, unsafe: true,
    githubPreLang: false,
));

$target = array_slice($argv, 1);
if (empty($target)) {
    // Find all failures and report.
    foreach ($examples as $i => $ex) {
        $actual = $parser->toHtml($ex['md']);
        if ($actual !== $ex['html']) {
            $target[] = $i + 1;
        }
    }
}

foreach ($target as $n) {
    $n = (int) $n;
    if (!isset($examples[$n - 1])) {
        echo "no example #$n\n"; continue;
    }
    $ex = $examples[$n - 1];
    $actual = $parser->toHtml($ex['md']);
    echo "\n=========== #$n (line {$ex['line']}) ===========\n";
    echo "--- MD ---\n";
    echo $ex['md'];
    echo "--- EXPECTED ---\n";
    echo $ex['html'];
    echo "--- ACTUAL ---\n";
    echo $actual;
    echo "--- match: " . ($actual === $ex['html'] ? "YES" : "NO") . " ---\n";
}
