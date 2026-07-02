--TEST--
Hard line break inside image alt text renders as a space, not literal <br /> markup
--EXTENSIONS--
mdparser
--FILE--
<?php

/* Regression: the MD_TEXT_BR case emitted "<br />\n" unconditionally, with no
 * image_nesting_level guard, so a hard break inside an image description leaked
 * literal "<br />" markup into the alt attribute. The adjacent MD_TEXT_SOFTBR
 * case already collapsed to a space inside images; MD_TEXT_BR now matches. */

$p = new MdParser\Parser();

function eq(string $label, string $got, string $want): void {
    echo ($got === $want ? "OK" : "FAIL"), ": $label\n";
    if ($got !== $want) echo "  got:  $got\n  want: $want\n";
}

// Backslash hard break inside image description.
eq("backslash hard break in alt collapses to space",
   trim($p->toHtml("![foo\\\nbar](/u)")),
   '<p><img src="/u" alt="foo bar" /></p>');

// Two-trailing-spaces hard break inside image description.
eq("two-space hard break in alt collapses to space",
   trim($p->toHtml("![foo  \nbar](/u)")),
   '<p><img src="/u" alt="foo bar" /></p>');

// Non-regression: a hard break in ordinary text still renders <br />.
eq("hard break outside image still emits <br />",
   trim($p->toHtml("foo\\\nbar")),
   "<p>foo<br />\nbar</p>");

?>
--EXPECT--
OK: backslash hard break in alt collapses to space
OK: two-space hard break in alt collapses to space
OK: hard break outside image still emits <br />
