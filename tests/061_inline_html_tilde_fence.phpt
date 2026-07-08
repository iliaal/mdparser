--TEST--
toInlineHtml suppresses block starts on continuation lines (per-line ZWSP)
--EXTENSIONS--
mdparser
--FILE--
<?php

/* Regression: md4c has no inline-only mode, so toInlineHtml normalizes
 * multiline input by putting a ZWSP sentinel on every retained line. This keeps
 * continuation-line block starts literal and prevents later paragraphs from
 * consuming reference definitions. */

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

// A colon-led GFM table underline on a continuation line also opens a block in
// md4c unless the line gets its own ZWSP sentinel.
eq("colon-led table underline continuation stays inline",
   trim($p->toInlineHtml("a | b\n:--|:--\nc | d")),
   "a | b\n:--|:--\nc | d");

eq("blank-line reference definition stays inline",
   trim($p->toInlineHtml("hello\n\n[label]: /evil\n\nsee [x][label]")),
   "hello\n[label]: /evil\nsee [x][label]");

?>
--EXPECT--
OK: tilde fence continuation stays inline
OK: backtick fence continuation stays inline
OK: tilde fence first line stays inline
OK: colon-led table underline continuation stays inline
OK: blank-line reference definition stays inline
