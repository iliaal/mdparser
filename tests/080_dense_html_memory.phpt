--TEST--
toHtml bounds peak memory for dense input whose output matches the input size
--EXTENSIONS--
mdparser
--INI--
memory_limit=64M
--FILE--
<?php

/* Counterpart to 064/065: those pin the sparse side of the capped output
 * reserve (tiny output, huge input). This pins the dense side, where the cap
 * hands the growth back to smart_str's doubling and each step transiently
 * holds the old buffer alongside the new one. */

$markdown = str_repeat("| aaaaaaaa | bbbbbbbb | cccccccc |\n", 120000) . "x\n";

$before = memory_get_peak_usage(true);
$html = (new MdParser\Parser(new MdParser\Options(tables: true)))->toHtml($markdown);
$delta = memory_get_peak_usage(true) - $before;

$budget = 3 * strlen($html);
echo ($delta <= $budget ? "OK" : sprintf("FAIL: %d > %d", $delta, $budget)),
    ": dense render stays within 3x the output size\n";

?>
--EXPECT--
OK: dense render stays within 3x the output size
