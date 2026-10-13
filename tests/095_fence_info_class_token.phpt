--TEST--
Code-fence class uses the first token after entity decoding
--EXTENSIONS--
mdparser
--FILE--
<?php

$parser = new MdParser\Parser();

foreach ([
    'numeric space' => "x&#32;hidden&#32;js-autosubmit",
    'numeric tab' => "x&#9;hidden",
] as $label => $info) {
    $html = $parser->toHtml("```$info\ncode\n```\n");
    echo $label, ': ', $html;
}

$html = $parser->toHtml("```x&nbsp;hidden\ncode\n```\n");
echo 'named non-breaking space: ', bin2hex($html), "\n";

?>
--EXPECT--
numeric space: <pre><code class="language-x">code
</code></pre>
numeric tab: <pre><code class="language-x">code
</code></pre>
named non-breaking space: 3c7072653e3c636f646520636c6173733d226c616e67756167652d78c2a068696464656e223e636f64650a3c2f636f64653e3c2f7072653e0a
