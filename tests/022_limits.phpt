--TEST--
limits: AST depth cap and input size cap both throw MdParser\Exception
--EXTENSIONS--
mdparser
--FILE--
<?php

$p = new MdParser\Parser();

// === AST depth cap: toHtml/toXml stream md4c's callbacks and are
// unaffected, but toAst builds a node array on a fixed stack and must cap
// at MDPARSER_MAX_AST_DEPTH (1000) to keep a one-line input from
// overflowing it. ===

// Deeply nested blockquotes: `> > > ...` with >1000 levels.
$deep = str_repeat('> ', 2000) . "x\n";

// toHtml streams md4c callbacks (no per-level C recursion), so it works fine.
$html = $p->toHtml($deep);
echo "toHtml on 2000-level blockquote: ", (str_contains($html, 'x') ? "ok" : "FAIL"), "\n";

// toAst must refuse with a clean exception, not a segfault.
try {
    $p->toAst($deep);
    echo "toAst depth cap: FAIL (no exception)\n";
} catch (MdParser\Exception $e) {
    echo "toAst depth cap: exception thrown\n";
    echo "  message contains 'depth': ", (str_contains($e->getMessage(), 'depth') ? "ok" : "FAIL"), "\n";
}

// A tree well below the cap still builds normally.
$shallow = str_repeat('> ', 50) . "x\n";
$ast = $p->toAst($shallow);
echo "toAst on 50-level blockquote: ", (is_array($ast) && $ast['type'] === 'document' ? "ok" : "FAIL"), "\n";

// === Input size cap: the wrapper rejects >256MB inputs up front with a
// documented exception rather than handing arbitrary sizes to md4c's
// 32-bit MD_SIZE / MD_OFFSET internals. The throw/accept boundary itself
// is pinned in 025_size_cap.phpt (257 MB throws, exactly 256 MB passes),
// so here just verify the normal path passes. ===

$normal = str_repeat("hello world\n", 100);
$out = $p->toHtml($normal);
echo "normal input: ", (str_contains($out, '<p>') ? "ok" : "FAIL"), "\n";

?>
--EXPECT--
toHtml on 2000-level blockquote: ok
toAst depth cap: exception thrown
  message contains 'depth': ok
toAst on 50-level blockquote: ok
normal input: ok
