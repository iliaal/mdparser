--TEST--
SmartyPants quote context resets at block boundaries and reads multibyte spaces
--EXTENSIONS--
mdparser
--FILE--
<?php

/* U+201C/201D = curly double quotes, U+2018/2019 = curly single quotes. */
$LDQ = "\xE2\x80\x9C"; $RDQ = "\xE2\x80\x9D";
$RSQ = "\xE2\x80\x99";
$NBSP = "\xC2\xA0";

$p = new MdParser\Parser(new MdParser\Options(smart: true));

function eq(string $label, string $got, string $want): void {
    echo ($got === $want ? "OK" : "FAIL"), ": $label\n";
    if ($got !== $want) echo "  got:  $got\n  want: $want\n";
}

// ---- CR-009: a new block starts a fresh quote context ----------------
// Without the reset the heading's trailing 'e' / paragraph's trailing '.'
// bleeds in and the leading quote renders as a *closing* quote.
eq("quote opens after heading",
   trim($p->toHtml("# Title\n\n\"Quote\"")),
   "<h1>Title</h1>\n<p>{$LDQ}Quote{$RDQ}</p>");
eq("quote opens after paragraph",
   trim($p->toHtml("End one.\n\n\"Start\"")),
   "<p>End one.</p>\n<p>{$LDQ}Start{$RDQ}</p>");

// ---- CR-003: a trailing multibyte Unicode space reads as left context -
// A non-breaking space (literal or via numeric entity) before a quote must
// open it; its UTF-8 tail byte alone would otherwise read as right context.
eq("literal NBSP before quote opens",
   trim($p->toHtml("a{$NBSP}\"q\"")),
   "<p>a{$NBSP}{$LDQ}q{$RDQ}</p>");
eq("entity NBSP (&#160;) before quote opens",
   trim($p->toHtml("a&#160;\"q\"")),
   "<p>a{$NBSP}{$LDQ}q{$RDQ}</p>");

// ---- Non-regressions -------------------------------------------------
// A multibyte *symbol* (not whitespace) before a quote still closes it.
$COPY = "\xC2\xA9";
eq("symbol before quote still closes",
   trim($p->toHtml("&copy;\"q\"")),
   "<p>{$COPY}{$RDQ}q{$RDQ}</p>");
// Mid-word apostrophe stays a closing single quote.
eq("mid-word apostrophe unchanged",
   trim($p->toHtml("don't")),
   "<p>don{$RSQ}t</p>");
// ASCII space still opens; bare letter still closes.
eq("ASCII space before quote opens",
   trim($p->toHtml("a \"q\"")),
   "<p>a {$LDQ}q{$RDQ}</p>");

?>
--EXPECT--
OK: quote opens after heading
OK: quote opens after paragraph
OK: literal NBSP before quote opens
OK: entity NBSP (&#160;) before quote opens
OK: symbol before quote still closes
OK: mid-word apostrophe unchanged
OK: ASCII space before quote opens
