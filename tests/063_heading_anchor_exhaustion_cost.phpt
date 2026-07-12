--TEST--
headingAnchors suffix exhaustion does not rescan 100000 occupied slugs per heading
--EXTENSIONS--
mdparser
--INI--
memory_limit=256M
--FILE--
<?php

$occupied = "# x\n";
for ($i = 1; $i <= 100000; $i++) {
    $occupied .= "# x-$i\n";
}

$p = new MdParser\Parser(new MdParser\Options(headingAnchors: true));

$start = hrtime(true);
$baselineHtml = $p->toHtml($occupied . "# x\n");
$baselineNs = hrtime(true) - $start;
unset($baselineHtml);

$start = hrtime(true);
$html = $p->toHtml($occupied . str_repeat("# x\n", 500));
$stressNs = hrtime(true) - $start;

echo ($stressNs < $baselineNs * 5 ? 'OK' : 'FAIL'), ": exhaustion stays bounded\n";
// The exact <h1> token counts headings with no attributes, not <h1 id="...">.
echo (substr_count($html, '<h1>') === 500 ? 'OK' : 'FAIL'), ": documented no-id fallback preserved\n";

?>
--EXPECT--
OK: exhaustion stays bounded
OK: documented no-id fallback preserved
