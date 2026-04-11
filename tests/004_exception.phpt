--TEST--
MdParser\Exception: final class extending RuntimeException
--SKIPIF--
<?php if (!extension_loaded("mdparser")) print "skip"; ?>
--FILE--
<?php

$refl = new ReflectionClass(MdParser\Exception::class);
var_dump($refl->isFinal());
var_dump($refl->getParentClass()->getName());
var_dump(is_subclass_of(MdParser\Exception::class, Throwable::class));

$e = new MdParser\Exception("test", 42);
var_dump($e->getMessage());
var_dump($e->getCode());
var_dump($e instanceof RuntimeException);
var_dump($e instanceof Exception);

try {
    throw $e;
} catch (\RuntimeException $caught) {
    echo "caught via RuntimeException\n";
}

try {
    throw new MdParser\Exception("nested");
} catch (\MdParser\Exception $caught) {
    echo "caught via MdParser\\Exception: ", $caught->getMessage(), "\n";
}
?>
--EXPECT--
bool(true)
string(16) "RuntimeException"
bool(true)
string(4) "test"
int(42)
bool(true)
bool(true)
caught via RuntimeException
caught via MdParser\Exception: nested
