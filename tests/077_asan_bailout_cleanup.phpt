--TEST--
md4c libc allocations are released when a Zend memory-limit error interrupts a callback
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

/* Engines with delayed memory-limit errors (ZEND_HAS_OOM) let the allocation
 * that crosses memory_limit succeed and raise the error once control returns
 * to the VM. A parse that ignores the queued error keeps allocating: it either
 * finishes far above the limit or is killed at the hard limit (twice these
 * limits by default) with _exit(), so neither md4c cleanup nor LSan ever runs.
 * The peak printed by the shutdown function shows whether the parse stopped
 * near the limit.
 *
 * use_stacks=0: after a bailout longjmp, md4c's abandoned frames still hold
 * pointers to the blocks it never freed, and LSan would treat those stale
 * stack words as roots and report nothing. */
function runBailout(string $module, int $memoryLimit, string $code): string {
    $code = 'register_shutdown_function(static function () {'
        . ' echo "\npeak=", memory_get_peak_usage(true), "\n"; });' . $code;
    $lsanOptions = implode(':', array_filter(
        [(string) getenv('LSAN_OPTIONS'), 'use_stacks=0'], 'strlen'));
    $command = [
        '/usr/bin/env',
        'USE_ZEND_ALLOC=1',
        'USE_TRACKED_ALLOC=0',
        "LSAN_OPTIONS={$lsanOptions}",
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

function stoppedAtLimit(string $output, int $memoryLimit): bool {
    return preg_match('/^peak=(\d+)$/m', $output, $match) === 1
        && (int) $match[1] <= $memoryLimit + intdiv($memoryLimit, 2);
}

$limit = 128 * 1024 * 1024;
$output = runBailout($module, $limit,
    '$p=new MdParser\\Parser; $p->toAst(str_repeat("*x* ",128*1024));');

echo (str_contains($output, 'Allowed memory size') ? "OK" : "FAIL"),
    ": memory bailout reproduced\n";
echo (stoppedAtLimit($output, $limit) ? "OK" : "FAIL"),
    ": parse stopped at the memory limit\n";
echo (!str_contains($output, 'LeakSanitizer: detected memory leaks') ? "OK" : "FAIL"),
    ": md4c context allocations released\n";

$limit = 32 * 1024 * 1024;
$output = runBailout($module, $limit,
    '$n=5000;$s="|".str_repeat("a|",$n)."\\n|".str_repeat("-|",$n)."\\n"'
    . '.str_repeat("|".str_repeat("x|",$n)."\\n",10);'
    . '(new MdParser\\Parser)->toAst($s);');
echo (str_contains($output, 'Allowed memory size') ? "OK" : "FAIL"),
    ": table memory bailout reproduced\n";
echo (stoppedAtLimit($output, $limit) ? "OK" : "FAIL"),
    ": table parse stopped at the memory limit\n";
echo (!str_contains($output, 'LeakSanitizer: detected memory leaks') ? "OK" : "FAIL"),
    ": md4c temporary allocations released\n";

?>
--EXPECT--
OK: memory bailout reproduced
OK: parse stopped at the memory limit
OK: md4c context allocations released
OK: table memory bailout reproduced
OK: table parse stopped at the memory limit
OK: md4c temporary allocations released
