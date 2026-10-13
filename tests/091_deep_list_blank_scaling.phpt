--TEST--
deep list blank-line parsing has bounded scaling
--EXTENSIONS--
mdparser
--FILE--
<?php

$parser = new MdParser\Parser();
$depth = 25000;
$markdown = str_repeat("- ", $depth) . "x\n";
$withBlanks = $markdown . str_repeat("\n", 50000);

function measure(MdParser\Parser $parser, string $markdown): array
{
    $start = hrtime(true);
    $html = $parser->toHtml($markdown);
    return [max(hrtime(true) - $start, 1000000), $html];
}

/* Hold list construction and output constant to isolate trailing blank-line cost. */
measure($parser, "- x\n\n");
[$baseTime, $baseHtml] = measure($parser, $markdown);
[$blankTime, $blankHtml] = measure($parser, $withBlanks);
$ratio = $blankTime / $baseTime;

echo 'nested lists preserved: ',
    $baseHtml === $blankHtml
    && substr_count($baseHtml, '<ul>') === $depth
    && substr_count($baseHtml, '<li>') === $depth
    && str_contains($baseHtml, 'x') ? 'yes' : 'no', "\n";
printf("blank-line overhead bounded: %s\n", $ratio < 8.0 ? 'yes' : sprintf('no (%.1fx)', $ratio));

?>
--EXPECT--
nested lists preserved: yes
blank-line overhead bounded: yes
