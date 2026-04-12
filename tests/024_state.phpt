--TEST--
state: serialize/clone blocks, $parser->options readback, reuse hygiene
--SKIPIF--
<?php if (!extension_loaded("mdparser")) print "skip"; ?>
--FILE--
<?php

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

// === Parser is NOT_SERIALIZABLE: cached cmark_options / extension_mask
// ints live on the C object struct, not in PHP properties, so a
// round-trip through serialize() would silently reset them. ===
$parser = new MdParser\Parser(new MdParser\Options(smart: true, footnotes: true));
try {
    serialize($parser);
    check("serialize(Parser) blocked", false);
} catch (Exception $e) {
    check("serialize(Parser) blocked", str_contains($e->getMessage(), "not allowed"));
}

// unserialize path is also blocked
try {
    unserialize('O:15:"MdParser\\Parser":0:{}');
    check("unserialize(Parser) blocked", false);
} catch (Exception $e) {
    check("unserialize(Parser) blocked", str_contains($e->getMessage(), "not allowed"));
}

// === Options is NOT_SERIALIZABLE too (consistency with Parser) ===
$opts = new MdParser\Options(smart: true);
try {
    serialize($opts);
    check("serialize(Options) blocked", false);
} catch (Exception $e) {
    check("serialize(Options) blocked", str_contains($e->getMessage(), "not allowed"));
}

// === Exception IS serializable: we want monolog / queue workers / PHPUnit
// failure reporting to be able to round-trip a thrown MdParser\Exception
// through serialize() just like any other RuntimeException subclass. ===
try {
    throw new MdParser\Exception("test error");
} catch (MdParser\Exception $e) {
    $blob = serialize($e);
    $restored = unserialize($blob);
    check("Exception serializable: round-trips", $restored instanceof MdParser\Exception);
    check("Exception serializable: message preserved", $restored->getMessage() === "test error");
}

// === clone is blocked on Parser ===
try {
    $c = clone $parser;
    check("clone(Parser) blocked", false);
} catch (Error $e) {
    check("clone(Parser) blocked", true);
}

// === $parser->options reads back the passed-in Options instance ===
$custom = new MdParser\Options(smart: true, footnotes: true, tables: false);
$p = new MdParser\Parser($custom);
check("\$parser->options is the same Options instance", $p->options === $custom);
check("\$parser->options->smart reads back", $p->options->smart === true);
check("\$parser->options->footnotes reads back", $p->options->footnotes === true);
check("\$parser->options->tables reads back", $p->options->tables === false);

// === $parser->options with no-arg constructor returns a fresh default Options ===
$pdef = new MdParser\Parser();
check("no-arg Parser has an Options instance", $pdef->options instanceof MdParser\Options);
check("no-arg Parser default options: unsafe=false", $pdef->options->unsafe === false);
check("no-arg Parser default options: tagfilter=true", $pdef->options->tagfilter === true);
check("no-arg Parser default options: tables=true", $pdef->options->tables === true);

// === new Parser(null) is equivalent to no-arg ===
$pnull = new MdParser\Parser(null);
check("new Parser(null) has an Options instance", $pnull->options instanceof MdParser\Options);
check("new Parser(null) defaults match", $pnull->options->unsafe === false && $pnull->options->tables === true);

// === Parser reuse across many calls: no state leak, deterministic output ===
$reuse = new MdParser\Parser();
$expected = $reuse->toHtml("# first");
for ($i = 0; $i < 50; $i++) {
    $got = $reuse->toHtml("# first");
    if ($got !== $expected) {
        check("Parser reuse: deterministic on iteration $i", false);
        break;
    }
}
check("Parser reuse: 50 identical toHtml calls produce identical output", true);

// Mixed toHtml / toXml / toAst on the same instance, then back to toHtml
$mixed = new MdParser\Parser();
$h1 = $mixed->toHtml("# mix");
$x = $mixed->toXml("# mix");
$a = $mixed->toAst("# mix");
$h2 = $mixed->toHtml("# mix");
check("mixed toHtml/toXml/toAst then toHtml still deterministic", $h1 === $h2);
check("mixed path: xml output non-empty", strlen($x) > 0);
check("mixed path: ast output is array", is_array($a) && $a['type'] === 'document');

?>
--EXPECT--
OK: serialize(Parser) blocked
OK: unserialize(Parser) blocked
OK: serialize(Options) blocked
OK: Exception serializable: round-trips
OK: Exception serializable: message preserved
OK: clone(Parser) blocked
OK: $parser->options is the same Options instance
OK: $parser->options->smart reads back
OK: $parser->options->footnotes reads back
OK: $parser->options->tables reads back
OK: no-arg Parser has an Options instance
OK: no-arg Parser default options: unsafe=false
OK: no-arg Parser default options: tagfilter=true
OK: no-arg Parser default options: tables=true
OK: new Parser(null) has an Options instance
OK: new Parser(null) defaults match
OK: Parser reuse: 50 identical toHtml calls produce identical output
OK: mixed toHtml/toXml/toAst then toHtml still deterministic
OK: mixed path: xml output non-empty
OK: mixed path: ast output is array
