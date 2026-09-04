--TEST--
mdparser.parse_memory_limit bounds md4c's libc working set on every entry point
--EXTENSIONS--
mdparser
--INI--
memory_limit=512M
mdparser.parse_memory_limit=4M
--FILE--
<?php

$parser = new MdParser\Parser();

var_dump(ini_get("mdparser.parse_memory_limit"));

// One byte of '[' commits three 24-byte marks, none of which memory_limit
// can see. 400k of them is far past 4M, on block and inline paths alike.
$amplifying = str_repeat("[", 400000) . " x\n";

foreach (["toHtml", "toInlineHtml", "toXml", "toAst"] as $method) {
    try {
        $parser->$method($amplifying);
        echo "$method: no exception\n";
    } catch (MdParser\Exception $e) {
        echo "$method: ", $e->getMessage(), "\n";
    }
}

// The container vector amplifies too, but only where block starts are
// recognised, so toInlineHtml is exempt by construction.
$blockquotes = str_repeat(">", 400000) . " x\n";
try {
    $parser->toHtml($blockquotes);
    echo "blockquotes: no exception\n";
} catch (MdParser\Exception $e) {
    echo "blockquotes: ", $e->getMessage(), "\n";
}

// A document that fits stays unaffected.
echo $parser->toHtml("# Hi\n\n> quote\n\n[l]: /u \"t\"\n\n[l]\n");

// The limit is PHP_INI_ALL, so raising it at runtime takes effect.
ini_set("mdparser.parse_memory_limit", "256M");
echo "raised: ", strlen($parser->toHtml($amplifying)) > 0 ? "parsed" : "empty", "\n";

// 0 and -1 both mean unlimited.
foreach (["0", "-1"] as $off) {
    ini_set("mdparser.parse_memory_limit", $off);
    echo "limit=$off: ", strlen($parser->toHtml($amplifying)) > 0 ? "parsed" : "empty", "\n";
}

// Lowering it again re-arms the bound.
ini_set("mdparser.parse_memory_limit", "1M");
try {
    $parser->toHtml($amplifying);
    echo "relowered: no exception\n";
} catch (MdParser\Exception $e) {
    echo "relowered: ", $e->getMessage(), "\n";
}

?>
--EXPECT--
string(2) "4M"
toHtml: mdparser: parse out of memory (allocation failed or mdparser.parse_memory_limit exceeded)
toInlineHtml: mdparser: parse out of memory (allocation failed or mdparser.parse_memory_limit exceeded)
toXml: mdparser: parse out of memory (allocation failed or mdparser.parse_memory_limit exceeded)
toAst: mdparser: parse out of memory (allocation failed or mdparser.parse_memory_limit exceeded)
blockquotes: mdparser: parse out of memory (allocation failed or mdparser.parse_memory_limit exceeded)
<h1>Hi</h1>
<blockquote>
<p>quote</p>
</blockquote>
<p><a href="/u" title="t">l</a></p>
raised: parsed
limit=0: parsed
limit=-1: parsed
relowered: mdparser: parse out of memory (allocation failed or mdparser.parse_memory_limit exceeded)
