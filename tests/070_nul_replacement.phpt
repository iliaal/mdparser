--TEST--
embedded NUL is replaced exactly once in HTML, XML, and AST output
--EXTENSIONS--
mdparser
--FILE--
<?php

$p = new MdParser\Parser();
$replacement = "\u{FFFD}";

function ok(string $label, bool $condition): void {
    echo ($condition ? "OK" : "FAIL"), ": $label\n";
}

$inline = "`a\0b`";
$fenced = "```\na\0b\n```\n";

ok("inline HTML replaces NUL once",
    $p->toHtml($inline) === "<p><code>a{$replacement}b</code></p>\n");
ok("fenced HTML replaces NUL once",
    $p->toHtml($fenced) === "<pre><code>a{$replacement}b\n</code></pre>\n");

$inlineAst = $p->toAst($inline);
$fencedAst = $p->toAst($fenced);
ok("inline AST replaces NUL once",
    $inlineAst['children'][0]['children'][0]['literal'] === "a{$replacement}b");
ok("fenced AST replaces NUL once",
    $fencedAst['children'][0]['literal'] === "a{$replacement}b\n");

foreach (["inline" => $inline, "fenced" => $fenced] as $label => $source) {
    $xml = $p->toXml($source);
    ok("{$label} XML replaces NUL once",
        substr_count($xml, $replacement) === 1 && !str_contains($xml, "\0"));
}

?>
--EXPECT--
OK: inline HTML replaces NUL once
OK: fenced HTML replaces NUL once
OK: inline AST replaces NUL once
OK: fenced AST replaces NUL once
OK: inline XML replaces NUL once
OK: fenced XML replaces NUL once
