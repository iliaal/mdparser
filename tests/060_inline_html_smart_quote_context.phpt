--TEST--
toInlineHtml with smart quotes opens a line-leading quote
--EXTENSIONS--
mdparser
--FILE--
<?php

/* The internal punctuation sentinel is consumed before smart rendering and
 * seeds whitespace quote context, so every physical line opens independently. */

$LDQ = "\xE2\x80\x9C"; $RDQ = "\xE2\x80\x9D";
$LSQ = "\xE2\x80\x98"; $RSQ = "\xE2\x80\x99";

$p = new MdParser\Parser(new MdParser\Options(smart: true));

function eq(string $label, string $got, string $want): void {
    echo ($got === $want ? "OK" : "FAIL"), ": $label\n";
    if ($got !== $want) echo "  got:  $got\n  want: $want\n";
}

// Line-leading double quote opens.
eq("line-leading double quote opens",
   trim($p->toInlineHtml("\"hi\" there")),
   "{$LDQ}hi{$RDQ} there");

// Line-leading single quote opens.
eq("line-leading single quote opens",
   trim($p->toInlineHtml("'hi' there")),
   "{$LSQ}hi{$RSQ} there");

// A quote at the start of a *subsequent* line also opens.
eq("quote opens on subsequent line",
   trim($p->toInlineHtml("say\n\"hi\" there")),
   "say\n{$LDQ}hi{$RDQ} there");

// Non-regression: toInlineHtml parity with toHtml for the first case.
eq("toHtml still opens line-leading double quote",
   trim($p->toHtml("\"hi\" there")),
   "<p>{$LDQ}hi{$RDQ} there</p>");

?>
--EXPECT--
OK: line-leading double quote opens
OK: line-leading single quote opens
OK: quote opens on subsequent line
OK: toHtml still opens line-leading double quote
