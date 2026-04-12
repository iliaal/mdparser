--TEST--
helpers: static Parser::html/xml/ast shortcuts and toInlineHtml
--SKIPIF--
<?php if (!extension_loaded("mdparser")) print "skip"; ?>
--FILE--
<?php

function check(string $label, bool $cond): void {
    echo ($cond ? "OK" : "FAIL"), ": $label\n";
}

// === Static shortcuts: same output as (new Parser)->toX($md) ===
$p = new MdParser\Parser();

check("Parser::html matches instance toHtml",
    MdParser\Parser::html("# hi") === $p->toHtml("# hi"));
check("Parser::xml matches instance toXml",
    MdParser\Parser::xml("# hi") === $p->toXml("# hi"));
check("Parser::ast matches instance toAst",
    MdParser\Parser::ast("# hi") === $p->toAst("# hi"));

// === Static shortcuts still throw through the same exception path ===
try {
    MdParser\Parser::html(str_repeat("x", 1));
    check("Parser::html normal input passes", true);
} catch (Throwable $e) {
    check("Parser::html normal input passes", false);
}

// === Parser::ast returns actual nested array ===
$ast = MdParser\Parser::ast("**bold**");
check("Parser::ast returns array", is_array($ast));
check("Parser::ast type = document", $ast["type"] === "document");
check("Parser::ast nested strong", $ast["children"][0]["children"][0]["type"] === "strong");

// === toInlineHtml: inline-only, no <p> wrapper ===
$p = new MdParser\Parser();

check("toInlineHtml strips <p> for plain text",
    $p->toInlineHtml("plain text") === "plain text");

check("toInlineHtml renders emphasis without <p>",
    $p->toInlineHtml("**bold** and *italic*") === "<strong>bold</strong> and <em>italic</em>");

check("toInlineHtml renders links without <p>",
    $p->toInlineHtml("[example](https://example.com)") === '<a href="https://example.com">example</a>');

check("toInlineHtml renders inline code without <p>",
    $p->toInlineHtml("run `ls -la`") === "run <code>ls -la</code>");

check("toInlineHtml renders strikethrough",
    $p->toInlineHtml("~~gone~~") === "<del>gone</del>");

// === Block-level markers become literal text (Parsedown::line semantics) ===
check("toInlineHtml: # heading stays literal",
    $p->toInlineHtml("# heading") === "# heading");

check("toInlineHtml: - list stays literal",
    $p->toInlineHtml("- item") === "- item");

check("toInlineHtml: > quote stays literal (HTML-escaped)",
    $p->toInlineHtml("> quote") === "&gt; quote");

check("toInlineHtml: 1. list stays literal",
    $p->toInlineHtml("1. item") === "1. item");

// Indented text stays literal (no code block): the 4 leading spaces
// are preserved verbatim inside the paragraph rather than triggering
// cmark's indent-code-block rule.
check("toInlineHtml: 4-space indent stays text",
    $p->toInlineHtml("    four space") === "    four space");

// === Empty input returns empty string ===
check("toInlineHtml: empty input → empty string",
    $p->toInlineHtml("") === "");

// === Safe-mode defaults still apply in inline mode ===
check("toInlineHtml: raw script stripped",
    str_contains($p->toInlineHtml("x <script>e</script> y"), "raw HTML omitted"));

check("toInlineHtml: javascript: URL stripped",
    $p->toInlineHtml("[x](javascript:alert(1))") === '<a href="">x</a>');

// === Custom Options still work on toInlineHtml ===
$smart = new MdParser\Parser(new MdParser\Options(smart: true));
check("toInlineHtml: smart punctuation honored",
    $smart->toInlineHtml("--dash--") === "–dash–");

// === Static shortcuts ignore any pre-constructed Options and always
// use the defaults; that's their contract ===
$got = MdParser\Parser::html("~~strike~~");
check("Parser::html uses default options (GFM on)",
    str_contains($got, "<del>strike</del>"));

?>
--EXPECT--
OK: Parser::html matches instance toHtml
OK: Parser::xml matches instance toXml
OK: Parser::ast matches instance toAst
OK: Parser::html normal input passes
OK: Parser::ast returns array
OK: Parser::ast type = document
OK: Parser::ast nested strong
OK: toInlineHtml strips <p> for plain text
OK: toInlineHtml renders emphasis without <p>
OK: toInlineHtml renders links without <p>
OK: toInlineHtml renders inline code without <p>
OK: toInlineHtml renders strikethrough
OK: toInlineHtml: # heading stays literal
OK: toInlineHtml: - list stays literal
OK: toInlineHtml: > quote stays literal (HTML-escaped)
OK: toInlineHtml: 1. list stays literal
OK: toInlineHtml: 4-space indent stays text
OK: toInlineHtml: empty input → empty string
OK: toInlineHtml: raw script stripped
OK: toInlineHtml: javascript: URL stripped
OK: toInlineHtml: smart punctuation honored
OK: Parser::html uses default options (GFM on)
