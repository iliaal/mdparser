--TEST--
CR-only line endings parse in linear time and match LF output
--EXTENSIONS--
mdparser
--FILE--
<?php

$parser = new MdParser\Parser();

$lf = "para one\nstill one\n\n# Heading\n\n- item\n- item two\n\n```\ncode\n  indented\n```\n\n> quote\n> more\n\n| a | b |\n|---|---|\n| 1 | 2 |\n";
$cr = str_replace("\n", "\r", $lf);
$crlf = str_replace("\n", "\r\n", $lf);
$mixed = "one\rtwo\r\nthree\nfour\r\r\n# h\r";

printf("CR html matches LF: %s\n", $parser->toHtml($cr) === $parser->toHtml($lf) ? 'yes' : 'no');
printf("CRLF html matches LF: %s\n", $parser->toHtml($crlf) === $parser->toHtml($lf) ? 'yes' : 'no');
printf("CR ast matches LF: %s\n", $parser->toAst($cr) === $parser->toAst($lf) ? 'yes' : 'no');
printf("mixed html matches LF: %s\n",
    $parser->toHtml($mixed) === $parser->toHtml(str_replace(["\r\n", "\r"], "\n", $mixed)) ? 'yes' : 'no');

/* A '\n' search that restarts at every line and runs to the end of input is
 * quadratic here: 500,000 CR-terminated lines and no '\n' anywhere. The
 * same document with LF endings is the linear baseline, so the check compares
 * the two in-process and stays independent of machine and sanitizer speed.
 * Linear parsing keeps the ratio near 1; the quadratic scan ran hundreds of
 * times slower at twice this size. */
$lines = 500000;
$docs = [
    'html' => [str_repeat("a\r", $lines), 'toHtml'],
    'ast' => ["```\r" . str_repeat("a\r", $lines) . "```\r", 'toAst'],
];

foreach ($docs as $label => [$crDoc, $method]) {
    $lfDoc = str_replace("\r", "\n", $crDoc);

    $start = hrtime(true);
    $lfOut = $parser->$method($lfDoc);
    $lfNs = max(hrtime(true) - $start, 1000000);

    $start = hrtime(true);
    $crOut = $parser->$method($crDoc);
    $ratio = (hrtime(true) - $start) / $lfNs;

    printf("%s matches LF: %s\n", $label, $crOut === $lfOut ? 'yes' : 'no');
    printf("%s linear: %s\n", $label, $ratio < 20 ? 'yes' : sprintf('no (%.1fx LF)', $ratio));
    if ($label === 'html') {
        printf("html soft breaks: %d\n", substr_count($crOut, "\n"));
    }
    unset($lfOut, $crOut);
}

?>
--EXPECT--
CR html matches LF: yes
CRLF html matches LF: yes
CR ast matches LF: yes
mixed html matches LF: yes
html matches LF: yes
html linear: yes
html soft breaks: 500000
ast matches LF: yes
ast linear: yes
