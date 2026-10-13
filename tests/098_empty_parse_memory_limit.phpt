--TEST--
An empty parse memory limit restores the default budget
--EXTENSIONS--
mdparser
--INI--
memory_limit=512M
mdparser.parse_memory_limit=1M
--FILE--
<?php
$parser = new MdParser\Parser();
$small = str_repeat('[', 65536);
$large = str_repeat('[', 2000000);

try {
    $parser->toHtml($small);
    echo "1M small: parsed\n";
} catch (MdParser\Exception $e) {
    echo "1M small: refused\n";
}

ini_set('mdparser.parse_memory_limit', '');
echo 'blank accepted: ', ini_get('mdparser.parse_memory_limit') === '' ? 'yes' : 'no', "\n";
try {
    $parser->toHtml($small);
    echo "blank small: parsed\n";
} catch (MdParser\Exception $e) {
    echo "blank small: refused\n";
}
try {
    $parser->toHtml($large);
    echo "blank large: parsed\n";
} catch (MdParser\Exception $e) {
    echo "blank large: refused\n";
}

ini_set('mdparser.parse_memory_limit', '0');
try {
    $parser->toHtml($large);
    echo "zero large: parsed\n";
} catch (MdParser\Exception $e) {
    echo "zero large: refused\n";
}
?>
--EXPECT--
1M small: refused
blank accepted: yes
blank small: parsed
blank large: refused
zero large: parsed
