--TEST--
regressions: ctor re-entry, toInlineHtml multiline, Options defaults parity, reflection bypass
--EXTENSIONS--
mdparser
--FILE--
<?php

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

// ---------------------------------------------------------------------
// re-entering Parser::__construct() must not silently swap the
// cached md4c parser-flag / renderer-option masks before the readonly
// $options write throws. A caught error must leave both $options and
// rendering behavior on the original (safe) configuration.
// ---------------------------------------------------------------------
$p = new MdParser\Parser(new MdParser\Options());
$before = $p->toHtml("<script>x</script>\n");
try {
    $p->__construct(new MdParser\Options(unsafe: true, tagfilter: false));
    check("__construct re-entry threw", false);
} catch (Error $e) {
    check("__construct re-entry threw", str_contains($e->getMessage(), "readonly"));
}
check("\$options->unsafe still false", $p->options->unsafe === false);
check("rendering still safe (raw <script> not emitted)",
    $p->toHtml("<script>x</script>\n") === $before
    && !str_contains($p->toHtml("<script>x</script>\n"), "<script>x</script>"));

// ---------------------------------------------------------------------
// toInlineHtml must suppress block-level constructs on every
// physical line, not just the first one.
// ---------------------------------------------------------------------
$p = new MdParser\Parser();

// Leading newline used to escape the sentinel and re-enable headings/lists/etc.
check("leading newline + heading stays inline",
    $p->toInlineHtml("\n# h") === "# h");
check("leading newline + list stays inline",
    $p->toInlineHtml("\n- a") === "- a");
check("leading newline + blockquote stays inline",
    $p->toInlineHtml("\n> q") === "&gt; q");
check("leading newline + thematic break stays inline",
    $p->toInlineHtml("\n---") === "---");

// Internal newlines: text on line 1, block marker on line 2.
check("internal newline + heading stays inline",
    $p->toInlineHtml("a\n# h") === "a\n# h");
check("blank line collapses (no second paragraph)",
    !str_contains($p->toInlineHtml("a\n\n# h"), "<p>")
    && !str_contains($p->toInlineHtml("a\n\n# h"), "<h1>"));

// CRLF normalization.
check("CRLF normalized like LF",
    $p->toInlineHtml("a\r\n# h") === $p->toInlineHtml("a\n# h"));
check("lone CR normalized like LF",
    $p->toInlineHtml("a\r# h") === $p->toInlineHtml("a\n# h"));

// Existing single-line semantics still hold.
check("single-line block markers stay literal (regression sanity)",
    $p->toInlineHtml("# h") === "# h");
check("empty input stays empty",
    $p->toInlineHtml("") === "");
check("lone newline stays empty",
    $p->toInlineHtml("\n") === "");

// ---------------------------------------------------------------------
// every Options constructor parameter default must match the
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
check("all 32 ctor parameters present",
    count($ctor->getParameters()) === 32);
check("every ctor default matches property default",
    $mismatches === []);
if ($mismatches) {
    foreach ($mismatches as $m) echo "  - $m\n";
}

// ---------------------------------------------------------------------
// An Options object built via Reflection without invoking
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
check("reflection-bypassed Options is rejected", $threw);
check("message names the offending property",
    str_contains($msg, "uninitialized")
    && (str_contains($msg, "Options::\$") || str_contains($msg, "Options::$")));

?>
--EXPECT--
OK: __construct re-entry threw
OK: $options->unsafe still false
OK: rendering still safe (raw <script> not emitted)
OK: leading newline + heading stays inline
OK: leading newline + list stays inline
OK: leading newline + blockquote stays inline
OK: leading newline + thematic break stays inline
OK: internal newline + heading stays inline
OK: blank line collapses (no second paragraph)
OK: CRLF normalized like LF
OK: lone CR normalized like LF
OK: single-line block markers stay literal (regression sanity)
OK: empty input stays empty
OK: lone newline stays empty
OK: all 32 ctor parameters present
OK: every ctor default matches property default
OK: reflection-bypassed Options is rejected
OK: message names the offending property
