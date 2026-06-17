--TEST--
Slug encoding: valid UTF-8 passes through, invalid bytes percent-encoded
--EXTENSIONS--
mdparser
--FILE--
<?php
function check(string $name, bool $cond): void {
    echo ($cond ? 'PASS' : 'FAIL') . " $name\n";
}

// Valid UTF-8 (default validateUtf8=true forces this) round-trips.
$p = new MdParser\Parser(new MdParser\Options(headingAnchors: true));
$out = $p->toHtml("# 日本語\n");
check('valid UTF-8 multibyte preserved', strpos($out, 'id="日本語"') !== false);

// Mixed valid + dropped punctuation.
$out = $p->toHtml("# Café résumé!\n");
check('valid 2-byte UTF-8 preserved',    strpos($out, 'id="café-résumé"') !== false);

// validateUtf8=false: malformed bytes survive the parser and reach the slug.
// Lone continuation bytes get percent-encoded; otherwise we'd land
// invalid HTML id values that browsers handle inconsistently.
$pUnsafe = new MdParser\Parser(new MdParser\Options(
    validateUtf8: false,
    headingAnchors: true,
));
$out = $pUnsafe->toHtml("# \x80\x81text\n");
check('lone continuation bytes percent-encoded', strpos($out, 'id="%80%81text"') !== false);

$out = $pUnsafe->toHtml("# good \xC3\xA9 bad \xFF text\n");
check('valid kept alongside invalid',    strpos($out, 'id="good-é-bad-%ff-text"') !== false);

// Truncated 3-byte sequence at end-of-string: the lead byte is invalid
// (no continuation follows), so it's percent-encoded.
$out = $pUnsafe->toHtml("# abc\xE3\n");
check('truncated multi-byte sequence encoded', strpos($out, 'id="abc%e3"') !== false);

// Strict RFC 3629 invalids: overlong / surrogates / > U+10FFFF must
// be percent-encoded byte-by-byte rather than passing through.
$out = $pUnsafe->toHtml("# x\xE0\x80\x80y\n");
check('overlong 3-byte encoding rejected',     strpos($out, 'id="x%e0%80%80y"') !== false);

$out = $pUnsafe->toHtml("# x\xED\xA0\x80y\n");
check('UTF-16 surrogate codepoint rejected',   strpos($out, 'id="x%ed%a0%80y"') !== false);

$out = $pUnsafe->toHtml("# x\xF4\x90\x80\x80y\n");
check('codepoint above U+10FFFF rejected',     strpos($out, 'id="x%f4%90%80%80y"') !== false);

// Boundary: \xC0/\xC1 are NEVER valid UTF-8 leads (they only encode
// overlong forms of ASCII).
$out = $pUnsafe->toHtml("# x\xC0\x80y\n");
check('0xC0 lead rejected',                    strpos($out, 'id="x%c0%80y"') !== false);

echo "done\n";
?>
--EXPECT--
PASS valid UTF-8 multibyte preserved
PASS valid 2-byte UTF-8 preserved
PASS lone continuation bytes percent-encoded
PASS valid kept alongside invalid
PASS truncated multi-byte sequence encoded
PASS overlong 3-byte encoding rejected
PASS UTF-16 surrogate codepoint rejected
PASS codepoint above U+10FFFF rejected
PASS 0xC0 lead rejected
done
