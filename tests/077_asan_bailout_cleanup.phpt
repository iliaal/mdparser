--TEST--
md4c libc allocations are released when a Zend memory-limit bailout interrupts a callback
--EXTENSIONS--
mdparser
--SKIPIF--
<?php
if (!getenv('ASAN_OPTIONS')) print 'skip ASAN/LSan only';
if (!is_readable('/proc/self/maps')) print 'skip requires Linux procfs';
if (!function_exists('proc_open')) print 'skip proc_open unavailable';
?>
--FILE--
<?php

$module = null;
foreach (file('/proc/self/maps', FILE_IGNORE_NEW_LINES) as $mapping) {
    if (preg_match('~\s(/\S+/mdparser\.so)$~', $mapping, $match)) {
        $module = $match[1];
        break;
    }
}

if ($module === null) {
    echo "FAIL: located module\nFAIL: bailout cleanup\n";
    exit;
}

function runBailout(string $module, string $memoryLimit, string $code): string {
    $command = [
        '/usr/bin/env',
        'USE_ZEND_ALLOC=1',
        'USE_TRACKED_ALLOC=0',
        PHP_BINARY,
        '-n',
        '-d', "extension={$module}",
        '-d', "memory_limit={$memoryLimit}",
        '-r', $code,
    ];
    $spec = [1 => ['pipe', 'w'], 2 => ['pipe', 'w']];
    $process = proc_open($command, $spec, $pipes);
    $stdout = stream_get_contents($pipes[1]);
    $stderr = stream_get_contents($pipes[2]);
    fclose($pipes[1]);
    fclose($pipes[2]);
    proc_close($process);
    return $stdout . $stderr;
}

$output = runBailout($module, '128M',
    '$p=new MdParser\\Parser; $p->toAst(str_repeat("*x* ",128*1024));');

echo (str_contains($output, 'Allowed memory size') ? "OK" : "FAIL"),
    ": memory bailout reproduced\n";
echo (!str_contains($output, 'LeakSanitizer: detected memory leaks') ? "OK" : "FAIL"),
    ": md4c context allocations released\n";

$output = runBailout($module, '32M',
    '$n=5000;$s="|".str_repeat("a|",$n)."\\n|".str_repeat("-|",$n)."\\n"'
    . '.str_repeat("|".str_repeat("x|",$n)."\\n",10);'
    . '(new MdParser\\Parser)->toAst($s);');
echo (str_contains($output, 'Allowed memory size') ? "OK" : "FAIL"),
    ": table memory bailout reproduced\n";
echo (!str_contains($output, 'LeakSanitizer: detected memory leaks') ? "OK" : "FAIL"),
    ": md4c temporary allocations released\n";

?>
--EXPECT--
OK: memory bailout reproduced
OK: md4c context allocations released
OK: table memory bailout reproduced
OK: md4c temporary allocations released
