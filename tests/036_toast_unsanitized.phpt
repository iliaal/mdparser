--TEST--
toAst() returns raw URLs and raw HTML literals; consumers must sanitize
--EXTENSIONS--
mdparser
--FILE--
<?php
function check(string $name, bool $cond): void {
    echo ($cond ? 'PASS' : 'FAIL') . " $name\n";
}

$p = new MdParser\Parser;

// toAst returns the link URL byte-for-byte. The render path
// strips dangerous schemes; the AST does NOT. Documented contract.
$ast = $p->toAst("[click](javascript:alert(1))\n");
$paragraph = $ast['children'][0];
$link = $paragraph['children'][0];
check('link node type', $link['type'] === 'link');
check('javascript: URL preserved verbatim', $link['url'] === 'javascript:alert(1)');

// Raw HTML survives as html_block / html_inline literals regardless of
// unsafe / tagfilter, because those only affect rendering.
$ast = $p->toAst("<script>alert(1)</script>\n");
check('html_block emitted',                 $ast['children'][0]['type'] === 'html_block');
check('html_block literal preserved',       trim($ast['children'][0]['literal']) === '<script>alert(1)</script>');

$ast = $p->toAst("inline <b onclick=\"x\">x</b> text\n");
$nodes = $ast['children'][0]['children'];
$found_html_inline = false;
foreach ($nodes as $n) {
    if ($n['type'] === 'html_inline' && str_contains($n['literal'], 'onclick=')) {
        $found_html_inline = true;
    }
}
check('html_inline preserved with attr',    $found_html_inline);

// Image alt-text URLs same story.
$ast = $p->toAst("![alt](javascript:alert(1))\n");
$image = $ast['children'][0]['children'][0];
check('image url preserved verbatim',       $image['url'] === 'javascript:alert(1)');

// data: URL on an image.
$ast = $p->toAst("![x](data:text/html,<script>alert(1)</script>)\n");
$image = $ast['children'][0]['children'][0];
check('data: image url preserved',          str_starts_with($image['url'], 'data:text/html'));

echo "done\n";
?>
--EXPECT--
PASS link node type
PASS javascript: URL preserved verbatim
PASS html_block emitted
PASS html_block literal preserved
PASS html_inline preserved with attr
PASS image url preserved verbatim
PASS data: image url preserved
done
