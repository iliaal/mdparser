--TEST--
MdParser\Options: readonly class, defaults, named arguments
--SKIPIF--
<?php if (!extension_loaded("mdparser")) print "skip"; ?>
--FILE--
<?php

$o = new MdParser\Options();
var_dump($o->sourcepos);
var_dump($o->hardbreaks);
var_dump($o->unsafe);
var_dump($o->validateUtf8);
var_dump($o->githubPreLang);
var_dump($o->tables);
var_dump($o->strikethrough);
var_dump($o->tasklist);
var_dump($o->autolink);
var_dump($o->tagfilter);

$o2 = new MdParser\Options(
    smart: true,
    unsafe: true,
    tables: false,
    tasklist: false,
);
var_dump($o2->smart);
var_dump($o2->unsafe);
var_dump($o2->tables);
var_dump($o2->tasklist);
var_dump($o2->autolink);

try {
    $o->sourcepos = true;
    echo "NO EXCEPTION\n";
} catch (Error $e) {
    echo "readonly: ", $e->getMessage(), "\n";
}
?>
--EXPECTF--
bool(false)
bool(false)
bool(false)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(false)
bool(false)
bool(true)
readonly: Cannot modify readonly property MdParser\Options::$sourcepos
