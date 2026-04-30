--TEST--
regressions: ctor re-entry, toInlineHtml multiline, Options defaults parity, reflection bypass
--SKIPIF--
<?php if (!extension_loaded("mdparser")) print "skip"; ?>
--FILE--
<?php

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

// ---------------------------------------------------------------------
// CR-001: re-entering Parser::__construct() must not silently swap the
// cached cmark/extension/postprocess masks before the readonly $options
// write throws. A caught error must leave both $options and rendering
// behavior on the original (safe) configuration.
// ---------------------------------------------------------------------
$p = new MdParser\Parser(new MdParser\Options());
$before = $p->toHtml("<script>x</script>\n");
try {
    $p->__construct(new MdParser\Options(unsafe: true, tagfilter: false));
    check("CR-001: __construct re-entry threw", false);
} catch (Error $e) {
    check("CR-001: __construct re-entry threw", str_contains($e->getMessage(), "readonly"));
}
check("CR-001: \$options->unsafe still false", $p->options->unsafe === false);
check("CR-001: rendering still safe (raw <script> not emitted)",
    $p->toHtml("<script>x</script>\n") === $before
    && !str_contains($p->toHtml("<script>x</script>\n"), "<script>x</script>"));

// ---------------------------------------------------------------------
// CR-002: toInlineHtml must suppress block-level constructs on every
// physical line, not just the first one.
// ---------------------------------------------------------------------
$p = new MdParser\Parser();

// Leading newline used to escape the sentinel and re-enable headings/lists/etc.
check("CR-002: leading newline + heading stays inline",
    $p->toInlineHtml("\n# h") === "# h");
check("CR-002: leading newline + list stays inline",
    $p->toInlineHtml("\n- a") === "- a");
check("CR-002: leading newline + blockquote stays inline",
    $p->toInlineHtml("\n> q") === "&gt; q");
check("CR-002: leading newline + thematic break stays inline",
    $p->toInlineHtml("\n---") === "---");

// Internal newlines: text on line 1, block marker on line 2.
check("CR-002: internal newline + heading stays inline",
    $p->toInlineHtml("a\n# h") === "a\n# h");
check("CR-002: blank line collapses (no second paragraph)",
    !str_contains($p->toInlineHtml("a\n\n# h"), "<p>")
    && !str_contains($p->toInlineHtml("a\n\n# h"), "<h1>"));

// CRLF normalization.
check("CR-002: CRLF normalized like LF",
    $p->toInlineHtml("a\r\n# h") === $p->toInlineHtml("a\n# h"));
check("CR-002: lone CR normalized like LF",
    $p->toInlineHtml("a\r# h") === $p->toInlineHtml("a\n# h"));

// Existing single-line semantics still hold.
check("CR-002: single-line block markers stay literal (regression sanity)",
    $p->toInlineHtml("# h") === "# h");
check("CR-002: empty input stays empty",
    $p->toInlineHtml("") === "");
check("CR-002: lone newline stays empty",
    $p->toInlineHtml("\n") === "");

// ---------------------------------------------------------------------
// CR-004: every Options constructor parameter default must match the
// value of the corresponding property on `new Options()`. A reflection
// walk catches drift between the C field table and the stub signature.
// ---------------------------------------------------------------------
$rc = new ReflectionClass(MdParser\Options::class);
$ctor = $rc->getConstructor();
$o = new MdParser\Options();
$mismatches = [];
foreach ($ctor->getParameters() as $param) {
    $name = $param->getName();
    if (!$param->isDefaultValueAvailable()) {
        $mismatches[] = "$name: no default in ctor";
        continue;
    }
    $ctorDefault = $param->getDefaultValue();
    if (!$rc->hasProperty($name)) {
        $mismatches[] = "$name: no property";
        continue;
    }
    $propValue = $rc->getProperty($name)->getValue($o);
    if ($ctorDefault !== $propValue) {
        $mismatches[] = "$name: ctor=" . var_export($ctorDefault, true)
            . " prop=" . var_export($propValue, true);
    }
}
check("CR-004: all 19 ctor parameters present",
    count($ctor->getParameters()) === 19);
check("CR-004: every ctor default matches property default",
    $mismatches === []);
if ($mismatches) {
    foreach ($mismatches as $m) echo "  - $m\n";
}

// ---------------------------------------------------------------------
// CR-005: An Options object built via Reflection without invoking
// __construct has uninitialized typed properties. The parser used to
// silently treat them as false and produce an all-default mask while
// $parser->options remained unreadable. The constructor must instead
// reject the object up front so callers cannot land in that
// half-built state.
// ---------------------------------------------------------------------
$rcOptions = new ReflectionClass(MdParser\Options::class);
$bad = $rcOptions->newInstanceWithoutConstructor();
$threw = false;
$msg = "";
try {
    new MdParser\Parser($bad);
} catch (MdParser\Exception $e) {
    $threw = true;
    $msg = $e->getMessage();
}
check("CR-005: reflection-bypassed Options is rejected", $threw);
check("CR-005: message names the offending property",
    str_contains($msg, "uninitialized")
    && (str_contains($msg, "Options::\$") || str_contains($msg, "Options::$")));

?>
--EXPECT--
OK: CR-001: __construct re-entry threw
OK: CR-001: $options->unsafe still false
OK: CR-001: rendering still safe (raw <script> not emitted)
OK: CR-002: leading newline + heading stays inline
OK: CR-002: leading newline + list stays inline
OK: CR-002: leading newline + blockquote stays inline
OK: CR-002: leading newline + thematic break stays inline
OK: CR-002: internal newline + heading stays inline
OK: CR-002: blank line collapses (no second paragraph)
OK: CR-002: CRLF normalized like LF
OK: CR-002: lone CR normalized like LF
OK: CR-002: single-line block markers stay literal (regression sanity)
OK: CR-002: empty input stays empty
OK: CR-002: lone newline stays empty
OK: CR-004: all 19 ctor parameters present
OK: CR-004: every ctor default matches property default
OK: CR-005: reflection-bypassed Options is rejected
OK: CR-005: message names the offending property
