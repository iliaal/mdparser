--TEST--
CommonMark 0.31 spec conformance (all examples from spec.txt)
--SKIPIF--
<?php
if (!extension_loaded("mdparser")) print "skip mdparser not loaded";
if (!file_exists(__DIR__ . "/fixtures/commonmark-spec.txt")) print "skip spec.txt not present";
?>
--FILE--
<?php

$spec = file_get_contents(__DIR__ . "/fixtures/commonmark-spec.txt");
$lines = explode("\n", $spec);

$examples = [];
$in_example = false;
$in_markdown = false;
$markdown = [];
$html = [];
$start_line = 0;
$fence = str_repeat("`", 32) . " example";

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

    if ($line === "````````````````````````````````") {
        $md_str = empty($markdown) ? "" : implode("\n", $markdown) . "\n";
        $html_str = empty($html) ? "" : implode("\n", $html) . "\n";
        $examples[] = [
            'md' => str_replace('→', "\t", $md_str),
            'html' => str_replace('→', "\t", $html_str),
            'line' => $start_line,
        ];
        $in_example = false;
        $in_markdown = false;
        continue;
    }

    if ($line === ".") {
        $in_markdown = false;
        continue;
    }

    if ($in_markdown) {
        $markdown[] = $line;
    } else {
        $html[] = $line;
    }
}

$parser = new MdParser\Parser(new MdParser\Options(
    tables: false,
    strikethrough: false,
    tasklist: false,
    autolink: false,
    tagfilter: false,
    unsafe: true,          // spec examples exercise raw HTML
    githubPreLang: false,  // spec wants <pre><code class="language-X"> form
));

$total = count($examples);
$pass = 0;
$fail = [];

foreach ($examples as $idx => $ex) {
    $actual = $parser->toHtml($ex['md']);
    if ($actual === $ex['html']) {
        $pass++;
    } else {
        $fail[] = $idx + 1 . " (line {$ex['line']})";
    }
}

printf("total: %d\n", $total);
printf("pass: %d\n", $pass);
printf("fail: %d\n", $total - $pass);
if (count($fail) > 0 && count($fail) <= 30) {
    echo "failures: ", implode(", ", $fail), "\n";
} elseif (count($fail) > 30) {
    echo "failures: ", implode(", ", array_slice($fail, 0, 30)), ", ...\n";
}
?>
--EXPECT--
total: 652
pass: 652
fail: 0
