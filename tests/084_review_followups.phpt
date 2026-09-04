--TEST--
review followups: githubPreLang inert, empty-input contract, stub/runtime defaults agreement
--EXTENSIONS--
mdparser
--FILE--
<?php

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

// ---------------------------------------------------------------------
// githubPreLang is accepted but inert: md4c always renders a fenced
// code block with a language in the CommonMark spec
// `<pre><code class="language-X">` form, so the flag must not change
// a byte of output (see docs/spec-coverage.md).
// ---------------------------------------------------------------------
$fenced = "```php\necho 1;\n```\n";
$withPrelang = (new MdParser\Parser(new MdParser\Options(githubPreLang: true)))->toHtml($fenced);
$withoutPrelang = (new MdParser\Parser(new MdParser\Options(githubPreLang: false)))->toHtml($fenced);
check("fenced code uses spec <pre><code class> form",
    str_contains($withPrelang, '<pre><code class="language-php">'));
check("githubPreLang:true/false output byte-identical",
    $withPrelang === $withoutPrelang);

// ---------------------------------------------------------------------
// Empty-input contract, pinned explicitly per entry point: toHtml('')
// is the empty string (also visible in 000_smoke), toXml('') is the
// bare document envelope, toAst('') the bare document node.
// ---------------------------------------------------------------------
$p = new MdParser\Parser();
check("toHtml('') is empty", $p->toHtml('') === '');
check("toXml('') is bare document envelope",
    $p->toXml('') === "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        . "<!DOCTYPE document SYSTEM \"CommonMark.dtd\">\n"
        . "<document xmlns=\"http://commonmark.org/xml/1.0\">\n</document>\n");
$emptyAst = $p->toAst('');
check("toAst('') is bare document node",
    $emptyAst['type'] === 'document' && !array_key_exists('children', $emptyAst));

// ---------------------------------------------------------------------
// Stub/runtime defaults agreement: every bool default declared in
// mdparser.stub.php must equal the runtime constructor default and
// the live property value on `new Options()`, in both directions.
// Fully dynamic — the stub file supplies the parameter list, so
// adding an option cannot silently desync the three spellings.
// ---------------------------------------------------------------------
$stub = file_get_contents(__DIR__ . "/../mdparser.stub.php");
$ctorSrc = substr($stub, strpos($stub, "public function __construct("));
$ctorSrc = substr($ctorSrc, 0, strpos($ctorSrc, ") {}") + 1);
preg_match_all('/bool\s+\$(\w+)\s*=\s*(true|false)/', $ctorSrc, $stubParams);
$stubDecls = [];
foreach ($stubParams[1] as $i => $name) {
    $stubDecls[$name] = $stubParams[2][$i] === "true";
}
$rc = new ReflectionClass(MdParser\Options::class);
$ctor = $rc->getConstructor();
$runtimeDefaults = [];
foreach ($ctor->getParameters() as $param) {
    $runtimeDefaults[$param->getName()] = $param->isDefaultValueAvailable()
        ? $param->getDefaultValue() : "NODEFAULT";
}
$live = new MdParser\Options();
$mismatches = [];
foreach ($stubDecls as $name => $stubDefault) {
    if (!array_key_exists($name, $runtimeDefaults)) {
        $mismatches[] = "$name: stub declares it, no runtime ctor parameter";
        continue;
    }
    if ($runtimeDefaults[$name] !== $stubDefault) {
        $mismatches[] = "$name: stub=" . var_export($stubDefault, true)
            . " runtime=" . var_export($runtimeDefaults[$name], true);
    }
    if (!$rc->hasProperty($name)) {
        $mismatches[] = "$name: stub declares it, no property";
        continue;
    }
    $propValue = $rc->getProperty($name)->getValue($live);
    if ($propValue !== $stubDefault) {
        $mismatches[] = "$name: stub=" . var_export($stubDefault, true)
            . " property=" . var_export($propValue, true);
    }
}
foreach ($runtimeDefaults as $name => $runtimeDefault) {
    if (!array_key_exists($name, $stubDecls)) {
        $mismatches[] = "$name: runtime ctor has it, stub does not declare it";
    }
}
check("stub defaults agree with runtime ctor and property defaults",
    $mismatches === []);
if ($mismatches) {
    foreach ($mismatches as $m) echo "  - $m\n";
}

?>
--EXPECT--
OK: fenced code uses spec <pre><code class> form
OK: githubPreLang:true/false output byte-identical
OK: toHtml('') is empty
OK: toXml('') is bare document envelope
OK: toAst('') is bare document node
OK: stub defaults agree with runtime ctor and property defaults
