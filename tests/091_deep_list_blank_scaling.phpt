--TEST--
deep list blank-line parsing has bounded scaling
--EXTENSIONS--
mdparser
--FILE--
<?php

$parser = new MdParser\Parser();

function measure(MdParser\Parser $parser, int $size): int
{
    $depth = intdiv($size, 4);
    $blanks = intdiv($size, 2);
    $markdown = str_repeat("- ", $depth) . "x\n" . str_repeat("\n", $blanks);

    $start = hrtime(true);
    $parser->toHtml($markdown);
    return max(hrtime(true) - $start, 1000000);
}

/* Doubling both dimensions used to multiply the work by about four. A
 * bounded container lookup keeps the growth close to the doubled input size. */
measure($parser, 20000);
$small = measure($parser, 100000);
$large = measure($parser, 200000);
$ratio = $large / $small;

printf("bounded scaling: %s\n", $ratio < 3.0 ? 'yes' : sprintf('no (%.1fx)', $ratio));

?>
--EXPECT--
bounded scaling: yes
