--TEST--
toInlineHtml suppresses tilde-fenced code on continuation lines (ZWSP block-start detection)
--EXTENSIONS--
mdparser
--FILE--
<?php

/* Regression: toInlineHtml skips per-line ZWSP insertion when no physical line
 * can open a block rule (a perf fast path). The block-start detector enumerated
 * every fence/list/heading lead byte except '~', so a `~~~` continuation line
 * escaped protection and opened a fenced code block -- breaking the inline-only
 * contract. Backtick fences were already covered; tilde fences must match. */

$p = new MdParser\Parser;

function eq(string $label, string $got, string $want): void {
    echo ($got === $want ? "OK" : "FAIL"), ": $label\n";
    if ($got !== $want) echo "  got:  " . json_encode($got) . "\n  want: " . json_encode($want) . "\n";
}

// Tilde fence on a continuation line stays literal (no <pre><code>).
eq("tilde fence continuation stays inline",
   trim($p->toInlineHtml("hello\n~~~\nworld")),
   "hello\n~~~\nworld");

// Parity: backtick fence continuation was already protected.
eq("backtick fence continuation stays inline",
   trim($p->toInlineHtml("hello\n```\nworld")),
   "hello\n```\nworld");

// Tilde fence as the very first line still opens (leading sentinel handles it,
// so it renders literally too).
eq("tilde fence first line stays inline",
   trim($p->toInlineHtml("~~~\ncode\n~~~")),
   "~~~\ncode\n~~~");

?>
--EXPECT--
OK: tilde fence continuation stays inline
OK: backtick fence continuation stays inline
OK: tilde fence first line stays inline
