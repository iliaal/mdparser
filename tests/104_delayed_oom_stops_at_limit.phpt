--TEST--
Renders stop near memory_limit when the engine defers memory-limit errors
--EXTENSIONS--
mdparser
--SKIPIF--
<?php
if (!is_readable('/proc/self/maps')) die('skip requires Linux procfs');
if (!function_exists('proc_open')) die('skip proc_open unavailable');
$module = null;
foreach (file('/proc/self/maps', FILE_IGNORE_NEW_LINES) as $mapping) {
    if (preg_match('~\s(/\S+/mdparser\.so)$~', $mapping, $match)) {
        $module = $match[1];
        break;
    }
}
if ($module === null) die('skip mdparser is not loaded as a shared module');
/* Engines with ZEND_HAS_OOM() let the allocation that crosses memory_limit
 * succeed and raise the error later, so the peak exceeds the limit. Older
 * engines bail out of the allocation and never exceed it. */
$env = getenv();
$env['USE_ZEND_ALLOC'] = '1';
$env['USE_TRACKED_ALLOC'] = '0';
$process = proc_open([PHP_BINARY, '-n', '-d', 'memory_limit=16M', '-r',
    'register_shutdown_function(static function () {'
    . ' echo "peak=", memory_get_peak_usage(true); });'
    . '$s = str_repeat("x", 6 << 20); $t = $s . $s;'],
    [1 => ['pipe', 'w'], 2 => ['pipe', 'w']], $pipes, null, $env);
$out = stream_get_contents($pipes[1]);
stream_get_contents($pipes[2]);
proc_close($process);
if (!preg_match('/peak=(\d+)/', $out, $match) || (int) $match[1] <= 16 << 20) {
    die('skip engine bails out at memory_limit instead of deferring the error');
}
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

/* A render that ignores the queued error keeps allocating until the engine's
 * hard limit (twice memory_limit by default) ends the process with exit code
 * 137 and no shutdown functions. Each case runs in a child so that exit is
 * observed instead of killing this test. */
function runChild(string $module, int $memoryLimit, string $code): array {
    $code = 'register_shutdown_function(static function () {'
        . ' echo "\npeak=", memory_get_peak_usage(true), "\n"; });' . $code;
    $command = [
        PHP_BINARY,
        '-n',
        '-d', "extension={$module}",
        '-d', "memory_limit={$memoryLimit}",
        '-r', $code,
    ];
    $env = getenv();
    $env['USE_ZEND_ALLOC'] = '1';
    $env['USE_TRACKED_ALLOC'] = '0';
    $spec = [1 => ['pipe', 'w'], 2 => ['pipe', 'w']];
    $process = proc_open($command, $spec, $pipes, null, $env);
    $stdout = stream_get_contents($pipes[1]);
    $stderr = stream_get_contents($pipes[2]);
    fclose($pipes[1]);
    fclose($pipes[2]);
    $rc = proc_close($process);
    $peak = preg_match('/^peak=(\d+)$/m', $stdout, $match) === 1
        ? (int) $match[1] : null;
    return [$rc, $peak, $stdout . $stderr];
}

$mb = 1024 * 1024;
$cases = [
    'toAst stops at the next callback' => [128 * $mb,
        '(new MdParser\\Parser)->toAst(str_repeat("*x* ", 256 * 1024));'],
    /* md4c's own footnote bookkeeping reaches mdparser.parse_memory_limit
     * before the output reaches twice a 128M memory_limit, so this case uses
     * a lower memory_limit. */
    'toHtml stops at the next callback' => [32 * $mb,
        '(new MdParser\\Parser(new MdParser\\Options(footnotes: true)))'
        . '->toHtml(str_repeat("[^1]", 1 << 20) . "\\n\\n[^1]: x\\n");'],
    'html escape of one text run' => [128 * $mb,
        '(new MdParser\\Parser)->html(str_repeat("<", 40 << 20));'],
    'xml escape of one text run' => [128 * $mb,
        '(new MdParser\\Parser)->xml(str_repeat("<", 40 << 20));'],
    'toInlineHtml escape of one text run' => [128 * $mb,
        '(new MdParser\\Parser)->toInlineHtml(str_repeat("<", 40 << 20));'],
    'html escape of one code line' => [128 * $mb,
        '(new MdParser\\Parser)->html("```\\n" . str_repeat("\\"", 32 << 20) . "\\n```\\n");'],
    'html invalid UTF-8 rebuild' => [128 * $mb,
        '(new MdParser\\Parser)->html(str_repeat("\\xff", 70 << 20));'],
];

foreach ($cases as $name => [$limit, $code]) {
    [$rc, $peak, $output] = runChild($module, $limit, $code);
    $ok = $rc === 255
        && str_contains($output, 'Allowed memory size')
        && $peak !== null
        && $peak <= $limit + intdiv($limit, 2);
    echo $ok ? "OK: {$name}\n"
        : "FAIL: {$name} (rc={$rc}, peak=" . ($peak ?? 'none') . ")\n";
}

?>
--EXPECT--
OK: toAst stops at the next callback
OK: toHtml stops at the next callback
OK: html escape of one text run
OK: xml escape of one text run
OK: toInlineHtml escape of one text run
OK: html escape of one code line
OK: html invalid UTF-8 rebuild
