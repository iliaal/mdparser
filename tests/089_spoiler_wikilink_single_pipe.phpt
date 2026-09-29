--TEST--
spoilers + wikiLinks: a single '|' is literal text and never opens or closes a spoiler
--EXTENSIONS--
mdparser
--FILE--
<?php

$parser = new MdParser\Parser(new MdParser\Options(spoilers: true, wikiLinks: true));

$cases = [
    "a | b | c\n",
    "x | y ||\n",
    "x || y |\n",
    "a | b || c || d |\n",
    "[[w|l]] | q |\n",
];

foreach ($cases as $md) {
    echo json_encode($md), "\n";
    echo "  html: ", $parser->toHtml($md);
    echo "  inline: ", $parser->toInlineHtml($md), "\n";
    $xml = $parser->toXml($md);
    echo "  xml spoilers: ", substr_count($xml, '<spoiler>'), "/", substr_count($xml, '</spoiler>'), "\n";
    $ast = $parser->toAst($md);
    echo "  ast top-level: ", implode(',', array_column($ast['children'], 'type')), "\n";
}
?>
--EXPECT--
"a | b | c\n"
  html: <p>a | b | c</p>
  inline: a | b | c
  xml spoilers: 0/0
  ast top-level: paragraph
"x | y ||\n"
  html: <p>x | y ||</p>
  inline: x | y ||
  xml spoilers: 0/0
  ast top-level: paragraph
"x || y |\n"
  html: <p>x || y |</p>
  inline: x || y |
  xml spoilers: 0/0
  ast top-level: paragraph
"a | b || c || d |\n"
  html: <p>a | b <span class="spoiler"> c </span> d |</p>
  inline: a | b <span class="spoiler"> c </span> d |
  xml spoilers: 1/1
  ast top-level: paragraph
"[[w|l]] | q |\n"
  html: <p><a class="wikilink" href="w">l</a> | q |</p>
  inline: <a class="wikilink" href="w">l</a> | q |
  xml spoilers: 0/0
  ast top-level: paragraph
