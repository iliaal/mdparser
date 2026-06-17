--TEST--
review2: XML control-char escaping + metadata; no dynamic props; ctor re-entry
--EXTENSIONS--
mdparser
--FILE--
<?php

function ok(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

// --- HIGH: XML 1.0-illegal control bytes are replaced, not emitted raw ----
$p = new MdParser\Parser();
$xml = $p->toXml("a\x0cb \x01 \x0b end");
ok("no raw control bytes in XML", !preg_match('/[\x00-\x08\x0b\x0c\x0e-\x1f]/', $xml));
ok("control bytes became U+FFFD", str_contains($xml, "\xEF\xBF\xBD"));
// tab/newline/CR are legal and must survive (here: a literal tab in code)
ok("legal whitespace preserved", str_contains($p->toXml("`a\tb`"), "\t"));

// --- CR-001: XML carries tasklist/align/footnote metadata ----------------
$pf = new MdParser\Parser(new MdParser\Options(footnotes: true));

$tl = $pf->toXml("- [x] a\n- [ ] b\n");
ok("tasklist checked=true", str_contains($tl, '<item checked="true">'));
ok("tasklist checked=false", str_contains($tl, '<item checked="false">'));

$tab = $pf->toXml("| a | b |\n|:--|--:|\n| 1 | 2 |\n");
ok("table cell align=left", str_contains($tab, '<table_cell align="left">'));
ok("table cell align=right", str_contains($tab, '<table_cell align="right">'));

$fn = $pf->toXml("x[^1]\n\n[^1]: note\n");
ok("footnote_reference carries id", str_contains($fn, '<footnote_reference id="1">'));
ok("footnote_definition carries id", str_contains($fn, '<footnote_definition id="1">'));

// A plain (non-task) item still renders without a checked attribute.
ok("plain item has no checked attr",
   str_contains($pf->toXml("- plain\n"), '<item>'));

// --- CR-002: readonly/no-dynamic-property classes reject typos ------------
$o = new MdParser\Options();
try { $o->headingAnchor = true; ok("Options rejects dynamic property", false); }
catch (\Error $e) { ok("Options rejects dynamic property", str_contains($e->getMessage(), "dynamic property")); }

$pp = new MdParser\Parser();
try { $pp->bogus = 1; ok("Parser rejects dynamic property", false); }
catch (\Error $e) { ok("Parser rejects dynamic property", str_contains($e->getMessage(), "dynamic property")); }

// --- CR-004: __construct re-entry reports the FIRST failed property -------
$o2 = new MdParser\Options();
try { $o2->__construct(unsafe: true); ok("re-construct throws", false); }
catch (\Error $e) {
    ok("re-construct throws readonly", str_contains($e->getMessage(), "readonly"));
    ok("re-construct names first property (sourcepos)",
       str_contains($e->getMessage(), 'Options::$sourcepos'));
}

?>
--EXPECT--
OK: no raw control bytes in XML
OK: control bytes became U+FFFD
OK: legal whitespace preserved
OK: tasklist checked=true
OK: tasklist checked=false
OK: table cell align=left
OK: table cell align=right
OK: footnote_reference carries id
OK: footnote_definition carries id
OK: plain item has no checked attr
OK: Options rejects dynamic property
OK: Parser rejects dynamic property
OK: re-construct throws readonly
OK: re-construct names first property (sourcepos)
