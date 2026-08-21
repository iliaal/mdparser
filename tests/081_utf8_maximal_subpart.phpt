--TEST--
validateUtf8 replaces one U+FFFD per Unicode maximal subpart (not per byte)
--EXTENSIONS--
mdparser
--FILE--
<?php

// W3C/WHATWG replacement policy: an invalid sequence is replaced by exactly
// one U+FFFD per maximal subpart -- the longest prefix of the bytes that is
// still a prefix of some valid sequence. A truncated sequence therefore
// yields ONE U+FFFD, while an out-of-range continuation byte splits the
// input into multiple subparts (one U+FFFD each).

function hexlit(string $bytes): string {
    return strtolower(bin2hex($bytes));
}

$p = new MdParser\Parser();
$fffd = "\xEF\xBF\xBD";

$cases = [
    // [input, expected body bytes]
    // truncated 3-byte sequence: E2 82 is a prefix of a valid sequence -> one FFFD
    ["a\xe2\x82b", "a{$fffd}b"],
    // truncated 4-byte sequence: F0 9F is a prefix of a valid sequence -> one FFFD
    ["a\xf0\x9fb", "a{$fffd}b"],
    // out-of-range second byte (E0 requires A0-BF): lead alone, then lone continuation
    ["a\xe0\x9fb", "a{$fffd}{$fffd}b"],
    // surrogate encoding ED A0 80: ED's range excludes A0 -> three subparts
    ["a\xed\xa0\x80b", "a{$fffd}{$fffd}{$fffd}b"],
    // truncation at end of input
    ["abc\xe3", "abc{$fffd}"],
    // C0 can never start a sequence; following 80 is a lone continuation
    ["a\xc0\x80b", "a{$fffd}{$fffd}b"],
    // fully valid input passes through byte-for-byte
    ["valid \xc3\xa9 \xe6\x97\xa5 ok", "valid \xc3\xa9 \xe6\x97\xa5 ok"],
];

foreach ($cases as $i => [$in, $want]) {
    $got = $p->toHtml($in);
    $got_body = preg_replace('/^<p>|<\/p>\n$/', '', $got);
    echo ($got_body === $want ? "OK" : "FAIL got=" . hexlit($got_body)), ": html case $i\n";
}

// The policy is shared by all three render paths.
$xml = $p->toXml("a\xe2\x82b");
echo (substr_count($xml, $fffd) === 1 ? "OK" : "FAIL"), ": xml emits exactly one U+FFFD\n";
$ast = $p->toAst("a\xe2\x82b");
echo ($ast['children'][0]['children'][0]['literal'] === "a{$fffd}b" ? "OK" : "FAIL"),
    ": ast literal has exactly one U+FFFD\n";

// validateUtf8:false still leaves raw bytes untouched.
$raw = new MdParser\Parser(new MdParser\Options(validateUtf8: false));
echo (str_contains($raw->toHtml("a\xe2\x82b"), "\xe2\x82") ? "OK" : "FAIL"),
    ": validateUtf8:false keeps raw bytes\n";

?>
--EXPECT--
OK: html case 0
OK: html case 1
OK: html case 2
OK: html case 3
OK: html case 4
OK: html case 5
OK: html case 6
OK: xml emits exactly one U+FFFD
OK: ast literal has exactly one U+FFFD
OK: validateUtf8:false keeps raw bytes
