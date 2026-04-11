--TEST--
MdParser\Options presets: strict(), github(), permissive()
--SKIPIF--
<?php if (!extension_loaded("mdparser")) print "skip"; ?>
--FILE--
<?php

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

// === return types ===
check("strict() returns Options", MdParser\Options::strict() instanceof MdParser\Options);
check("github() returns Options", MdParser\Options::github() instanceof MdParser\Options);
check("permissive() returns Options", MdParser\Options::permissive() instanceof MdParser\Options);

// === strict: defaults + autolink=false ===
$s = MdParser\Options::strict();
check("strict: unsafe=false",    $s->unsafe === false);
check("strict: tagfilter=true",  $s->tagfilter === true);
check("strict: autolink=false",  $s->autolink === false);
check("strict: tables=true",     $s->tables === true);
check("strict: strikethrough=true", $s->strikethrough === true);
check("strict: validateUtf8=true",  $s->validateUtf8 === true);

// autolink-off behavior: bare URL stays as text, not a link
$ps = new MdParser\Parser($s);
$html = $ps->toHtml("visit https://example.com today\n");
check("strict: bare URL not auto-linked",
    !str_contains($html, '<a href="https://example.com"'));

// === github: defaults + footnotes=true ===
$g = MdParser\Options::github();
check("github: footnotes=true",  $g->footnotes === true);
check("github: unsafe=false",    $g->unsafe === false);
check("github: tables=true",     $g->tables === true);
check("github: autolink=true",   $g->autolink === true);
check("github: tagfilter=true",  $g->tagfilter === true);

// footnote-on behavior: [^ref] / [^ref]: renders as a footnote block
$pg = new MdParser\Parser($g);
$html = $pg->toHtml("here[^1]\n\n[^1]: the note\n");
check("github: footnote reference rendered",
    str_contains($html, 'class="footnote-ref"'));

// === permissive: unsafe + no tagfilter + liberal HTML ===
$p = MdParser\Options::permissive();
check("permissive: unsafe=true",        $p->unsafe === true);
check("permissive: tagfilter=false",    $p->tagfilter === false);
check("permissive: liberalHtmlTag=true", $p->liberalHtmlTag === true);
check("permissive: validateUtf8=true (inherited from default)",
    $p->validateUtf8 === true);

// permissive behavior: raw HTML passes untouched
$pp = new MdParser\Parser($p);
$html = $pp->toHtml("<div class=x>raw</div>\n");
check("permissive: raw HTML passes through", str_contains($html, '<div class=x>'));

// === presets return fresh instances each call ===
$a = MdParser\Options::github();
$b = MdParser\Options::github();
check("presets return distinct instances", $a !== $b);

// === readonly enforcement still applies ===
try {
    $s->unsafe = true;
    check("readonly still enforced on preset instance", false);
} catch (Error $e) {
    check("readonly still enforced on preset instance", true);
}

?>
--EXPECT--
OK: strict() returns Options
OK: github() returns Options
OK: permissive() returns Options
OK: strict: unsafe=false
OK: strict: tagfilter=true
OK: strict: autolink=false
OK: strict: tables=true
OK: strict: strikethrough=true
OK: strict: validateUtf8=true
OK: strict: bare URL not auto-linked
OK: github: footnotes=true
OK: github: unsafe=false
OK: github: tables=true
OK: github: autolink=true
OK: github: tagfilter=true
OK: github: footnote reference rendered
OK: permissive: unsafe=true
OK: permissive: tagfilter=false
OK: permissive: liberalHtmlTag=true
OK: permissive: validateUtf8=true (inherited from default)
OK: permissive: raw HTML passes through
OK: presets return distinct instances
OK: readonly still enforced on preset instance
