--TEST--
toXml depth cap (output-amplification guard); entity text feeds heading slugs
--EXTENSIONS--
mdparser
--FILE--
<?php

function ok(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

$p = new MdParser\Parser();

// --- toXml depth cap: deep nesting must throw, not amplify to OOM -------
// 2-spaces-per-level indentation makes deep nesting produce quadratic XML.
ok("shallow nesting renders",
   str_contains($p->toXml(str_repeat("> ", 10) . "x\n"), "<block_quote>"));

$threw = false; $msg = "";
try { $p->toXml(str_repeat("> ", 1500) . "x\n"); }
catch (MdParser\Exception $e) { $threw = true; $msg = $e->getMessage(); }
ok("deep block nesting throws", $threw);
ok("depth error message", str_contains($msg, "nesting exceeds maximum depth"));

// Deeply nested inline spans hit the same cap.
$threw = false;
try { $p->toXml(str_repeat("*", 3000) . "x" . str_repeat("*", 3000)); }
catch (MdParser\Exception $e) { $threw = true; }
ok("deep inline nesting throws", $threw);

// toHtml is linear (no indentation) and is NOT depth-capped.
ok("toHtml renders deep nesting without throwing",
   str_contains($p->toHtml(str_repeat("> ", 1500) . "x\n"), "<blockquote>"));

// --- entity text contributes to heading slugs --------------------------
$a = new MdParser\Parser(new MdParser\Options(headingAnchors: true));
ok("named entity heading gets a slug",
   str_contains($a->toHtml("# &copy;"), '<h1 id="©">'));
ok("mixed entity+text heading slugs both",
   str_contains($a->toHtml("# Caf&eacute;"), '<h1 id="café">'));
ok("numeric entity heading slugs",
   str_contains($a->toHtml("# &#65;&#66;"), '<h1 id="ab">'));
// A heading whose only content slugifies to nothing still gets no id.
ok("punctuation-only heading has no id",
   str_contains($a->toHtml("# ..."), '<h1>') && !str_contains($a->toHtml("# ..."), 'id='));

?>
--EXPECT--
OK: shallow nesting renders
OK: deep block nesting throws
OK: depth error message
OK: deep inline nesting throws
OK: toHtml renders deep nesting without throwing
OK: named entity heading gets a slug
OK: mixed entity+text heading slugs both
OK: numeric entity heading slugs
OK: punctuation-only heading has no id
