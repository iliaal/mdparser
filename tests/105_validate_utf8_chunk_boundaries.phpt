--TEST--
validateUtf8 substitutes U+FFFD identically wherever the bad bytes fall relative to the vectorized 64-byte chunks
--EXTENSIONS--
mdparser
--FILE--
<?php

function seqlen(string $s, int $i, int $n): int {
    $c = ord($s[$i]);
    if ($c < 0x80) return 1;
    $lo = 0x80; $hi = 0xBF;
    if ($c >= 0xC2 && $c <= 0xDF) $e = 2;
    elseif ($c === 0xE0) { $e = 3; $lo = 0xA0; }
    elseif ($c >= 0xE1 && $c <= 0xEC) $e = 3;
    elseif ($c === 0xED) { $e = 3; $hi = 0x9F; }
    elseif ($c >= 0xEE && $c <= 0xEF) $e = 3;
    elseif ($c === 0xF0) { $e = 4; $lo = 0x90; }
    elseif ($c >= 0xF1 && $c <= 0xF3) $e = 4;
    elseif ($c === 0xF4) { $e = 4; $hi = 0x8F; }
    else return 0;
    if ($n - $i < $e) return 0;
    $b = ord($s[$i + 1]);
    if ($b < $lo || $b > $hi) return 0;
    for ($k = 2; $k < $e; $k++) {
        $b = ord($s[$i + $k]);
        if ($b < 0x80 || $b > 0xBF) return 0;
    }
    return $e;
}

// Maximal-subpart policy: one U+FFFD per longest prefix of a valid sequence.
function subpart(string $s, int $i, int $n): int {
    $c = ord($s[$i]);
    if ($c < 0xC2 || $c > 0xF4) return 1;
    $lo = $c === 0xE0 ? 0xA0 : ($c === 0xF0 ? 0x90 : 0x80);
    $hi = $c === 0xED ? 0x9F : ($c === 0xF4 ? 0x8F : 0xBF);
    $e = $c <= 0xDF ? 2 : ($c <= 0xEF ? 3 : 4);
    if ($n - $i < 2) return 1;
    $b = ord($s[$i + 1]);
    if ($b < $lo || $b > $hi) return 1;
    $k = 2;
    while ($k < $e && $i + $k < $n && ord($s[$i + $k]) >= 0x80 && ord($s[$i + $k]) <= 0xBF) $k++;
    return $k;
}

function sanitize(string $s): string {
    $out = '';
    $n = strlen($s);
    for ($i = 0; $i < $n;) {
        $l = seqlen($s, $i, $n);
        if ($l) { $out .= substr($s, $i, $l); $i += $l; }
        else { $out .= "\u{FFFD}"; $i += subpart($s, $i, $n); }
    }
    return $out;
}

$p = new MdParser\Parser();
$valid = ['x', 'x', 'x', "\u{E9}", "\u{20AC}", "\u{4E2D}", "\u{1F600}", "\u{10FFFF}", "\u{D7FF}", "\u{E000}"];
$bad = ["\x80", "\xBF", "\xC0\x80", "\xC1\xBF", "\xC2", "\xE0\x80\x80", "\xE0\x9F\xBF",
    "\xED\xA0\x80", "\xED\xBF\xBF", "\xE2\x82", "\xF0\x80\x80\x80", "\xF0\x8F\xBF\xBF",
    "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xF8", "\xFF", "\xF0\x9F\x98", "\xC2\xC2"];

$fails = 0;
$cases = 0;
$check = function (string $in) use ($p, &$fails, &$cases) {
    $cases++;
    $want = "<p>" . sanitize($in) . "</p>\n";
    if ($p->toHtml($in) !== $want) {
        if ($fails++ < 5) echo "FAIL: ", bin2hex($in), "\n";
    }
};

// Every bad token at every offset across the first three chunk boundaries,
// in ASCII filler and in multibyte filler.
foreach (['x', "\u{E9}"] as $fill) {
    foreach ($bad as $tok) {
        for ($off = 0; $off <= 200; $off++) {
            $pre = str_repeat('x', $off % strlen($fill)) . str_repeat($fill, intdiv($off, strlen($fill)));
            $check($pre . $tok . str_repeat($fill, 40));
            $check($pre . $tok);
        }
    }
}

// Valid multibyte sequences straddling every boundary must pass through.
foreach ($valid as $tok) {
    for ($off = 0; $off <= 200; $off++) {
        $check(str_repeat('x', $off) . $tok . str_repeat('y', 70));
        $check(str_repeat('x', $off) . $tok);
    }
}

mt_srand(105);
for ($n = 0; $n < 3000; $n++) {
    $s = '';
    $len = mt_rand(0, 400);
    while (strlen($s) < $len) {
        $s .= mt_rand(0, 40) === 0 ? $bad[mt_rand(0, count($bad) - 1)] : $valid[mt_rand(0, count($valid) - 1)];
    }
    if ($s === '') continue;
    $check($s);
}

echo $fails === 0 ? "OK" : "FAILED $fails", "\n";
?>
--EXPECT--
OK
