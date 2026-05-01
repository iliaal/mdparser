--TEST--
Parser reuse across calls does not leak state between renders
--EXTENSIONS--
mdparser
--FILE--
<?php
function check(string $name, bool $cond): void {
    echo ($cond ? 'PASS' : 'FAIL') . " $name\n";
}

// Same Parser instance: link reference defined in render N1 must NOT be
// resolvable in render N2. Confirms cmark_parser_reset (called by
// cmark_parser_finish on the happy path) clears the reference map.
$p = new MdParser\Parser;

$first  = $p->toHtml("[ref]: https://x.example\n[link][ref]\n");
$second = $p->toHtml("[link][ref]\n");

check('N1 resolves ref',           strpos($first, 'href="https://x.example"') !== false);
check('N2 does NOT resolve ref',   strpos($second, 'href="https://x.example"') === false);
check('N2 keeps unresolved markup', strpos($second, '[link][ref]') !== false);

// Repeated identical input through the same Parser must produce identical
// output -- pins idempotence of feed/finish over the cached parser.
$src   = "# Heading\n\nA paragraph with **bold** and *italic*.\n\n[Link](https://example.com)\n";
$once  = $p->toHtml($src);
$twice = $p->toHtml($src);
$thrice = $p->toHtml($src);
check('repeated render is idempotent', $once === $twice && $twice === $thrice);

// Cross-Parser equivalence: renders on instance A must equal renders on a
// fresh instance B for the same input.
$pA = new MdParser\Parser;
$pB = new MdParser\Parser;
// Warm the parser caches by running an unrelated input through each.
$pA->toHtml("warmup A\n");
$pB->toHtml("totally different warmup B\n");
$inA = $pA->toHtml("# same\n\ntext.\n");
$inB = $pB->toHtml("# same\n\ntext.\n");
check('cross-instance idempotence', $inA === $inB);

// Heading anchors with dedupe: multiple calls must each see a fresh slug
// counter, not carry collisions across renders.
$opts = new MdParser\Options(headingAnchors: true);
$ph = new MdParser\Parser($opts);
$out1 = $ph->toHtml("# Same\n# Same\n");
$out2 = $ph->toHtml("# Same\n# Same\n");
check('per-call dedupe state is fresh', $out1 === $out2);

// State after toAst -> toHtml: AST render uses the same parser; confirm
// the next HTML render starts clean.
$pX = new MdParser\Parser;
$pX->toAst("[refX]: https://x.example\nbody\n");
$h = $pX->toHtml("[link][refX]\n");
check('toAst does not leak refmap into next toHtml',
    strpos($h, 'href="https://x.example"') === false);

echo "done\n";
?>
--EXPECT--
PASS N1 resolves ref
PASS N2 does NOT resolve ref
PASS N2 keeps unresolved markup
PASS repeated render is idempotent
PASS cross-instance idempotence
PASS per-call dedupe state is fresh
PASS toAst does not leak refmap into next toHtml
done
