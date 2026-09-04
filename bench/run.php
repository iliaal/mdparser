<?php
/**
 * mdparser benchmark harness.
 *
 * Measures wall time for each parser on each corpus over N iterations.
 * Reports trimmed-mean, median, and p95 ms/op, ops/sec, and relative
 * speed vs mdparser. An empty-closure baseline is subtracted from every
 * latency stat so harness overhead does not pollute small-corpus means.
 *
 * Usage:
 *   php -d extension=../modules/mdparser.so bench/run.php [--iters=N] [--warmup=N]
 *       [--parsers=mdparser,parsedown] [--format=table|json|md] [--help]
 *   php -d opcache.enable_cli=1 -d opcache.jit_buffer_size=64M \
 *       -d extension=../modules/mdparser.so bench/run.php
 *
 * Defaults: 300 iters for sub-KB corpora, 50 otherwise; 5 warm-up iters.
 * --iters overrides the default for every corpus.
 *
 * Output columns:
 *   parser | corpus | size | iters | mean_ms | median_ms | p95_ms | ops/sec | speedup
 * JSON output wraps rows as {"env": ..., "results": [...]} where env
 * records the PHP version, NTS/ZTS build, and OPcache configuration.
 */
declare(strict_types=1);

$opts = getopt('', ['iters::', 'parsers::', 'format::', 'warmup::', 'league', 'help']);

if (array_key_exists('help', $opts)) {
    echo <<<'USAGE'
mdparser benchmark harness.

Usage:
  php -d extension=../modules/mdparser.so bench/run.php [options]

Options:
  --iters=N       Iterations per measurement (default: 300 for sub-KB
                  corpora, 50 otherwise; overrides every corpus).
  --warmup=N      Warm-up iterations, not counted (default: 5).
  --parsers=a,b   Comma-separated subset of parsers (default: all).
  --format=F      table, json, or md (default: table).
  --league        Include league/commonmark (needs mbstring).
  --help          Print this message and exit.

USAGE;
    exit(0);
}

require __DIR__ . '/vendor/autoload.php';

if (!extension_loaded('mdparser')) {
    fwrite(STDERR, "mdparser extension not loaded. Run with -d extension=...\n");
    exit(1);
}

$hasItersOpt = array_key_exists('iters', $opts);
$itersOpt    = $hasItersOpt ? (int)$opts['iters'] : null;
$warmup      = (int)($opts['warmup']  ?? 5);
$parserList = isset($opts['parsers']) ? explode(',', $opts['parsers']) : null;
$format     = $opts['format'] ?? 'table';

if ($hasItersOpt && $itersOpt < 1) {
    fwrite(STDERR, "--iters must be at least 1\n");
    exit(2);
}
if ($warmup < 0) {
    fwrite(STDERR, "--warmup must be at least 0\n");
    exit(2);
}
if (!in_array($format, ['table', 'json', 'md'], true)) {
    fwrite(STDERR, "--format must be one of: table, json, md\n");
    exit(2);
}

$parsers = [
    'mdparser' => function (string $md): string {
        static $p = null;
        $p ??= new MdParser\Parser();
        return $p->toHtml($md);
    },
    'mdparser-inline' => function (string $md): string {
        static $p = null;
        $p ??= new MdParser\Parser();
        return $p->toInlineHtml($md);
    },
    'mdparser-anchors' => function (string $md): string {
        static $p = null;
        $p ??= new MdParser\Parser(new MdParser\Options(headingAnchors: true));
        return $p->toHtml($md);
    },
    'mdparser-nofollow' => function (string $md): string {
        static $p = null;
        $p ??= new MdParser\Parser(new MdParser\Options(nofollowLinks: true));
        return $p->toHtml($md);
    },
    'mdparser-ast' => function (string $md): string {
        static $p = null;
        $p ??= new MdParser\Parser();
        // Serialize to a stable sink so the bench measures full AST build cost.
        return serialize($p->toAst($md));
    },
    'parsedown' => function (string $md): string {
        static $p = null;
        $p ??= new Parsedown();
        return $p->text($md);
    },
    'cebe/markdown' => function (string $md): string {
        static $p = null;
        $p ??= new cebe\markdown\GithubMarkdown();
        return $p->parse($md);
    },
    'michelf' => function (string $md): string {
        static $p = null;
        $p ??= new Michelf\MarkdownExtra();
        return $p->transform($md);
    },
];

// league/commonmark is opt-in via --parsers=... because it requires
// the native mbstring extension (not just symfony/polyfill-mbstring).
// On minimal PHP builds without mbstring it errors on large inputs.
if (extension_loaded('mbstring') || array_key_exists('league', $opts)) {
    $parsers['league/commonmark'] = function (string $md): string {
        static $p = null;
        if ($p === null) {
            $env = new League\CommonMark\Environment\Environment();
            $env->addExtension(new League\CommonMark\Extension\CommonMark\CommonMarkCoreExtension());
            $env->addExtension(new League\CommonMark\Extension\GithubFlavoredMarkdownExtension());
            $p = new League\CommonMark\MarkdownConverter($env);
        }
        return (string)$p->convert($md);
    };
}

if ($parserList !== null) {
    $unknown = array_diff($parserList, array_keys($parsers));
    if ($unknown !== []) {
        fwrite(STDERR, "Unknown parser(s): " . implode(', ', $unknown) . "\n");
        exit(2);
    }
    $parsers = array_intersect_key($parsers, array_flip($parserList));
}

if ($parsers === []) {
    fwrite(STDERR, "No parsers selected\n");
    exit(2);
}

$corpora = [];
foreach (glob(__DIR__ . '/corpora/*.md') as $path) {
    $corpora[basename($path, '.md')] = file_get_contents($path);
}
ksort($corpora);

// Runtime environment record: emitted with --format=json and as a footer
// on table/md output, so interpreted-PHP parsers are never compared
// against the native ext without OPcache and build context.
function bench_env(): array {
    $ini = static function (string $name): string {
        $v = ini_get($name);
        return is_string($v) && $v !== '' ? $v : 'n/a';
    };
    return [
        'php_version' => PHP_VERSION,
        'sapi' => PHP_SAPI,
        'thread_safety' => (defined('PHP_ZTS') && PHP_ZTS) ? 'ZTS' : 'NTS',
        'debug_build' => (bool)PHP_DEBUG,
        'opcache' => [
            'loaded' => extension_loaded('Zend OPcache') || extension_loaded('opcache'),
            'enable_cli' => $ini('opcache.enable_cli'),
            'jit' => $ini('opcache.jit'),
            'jit_buffer_size' => $ini('opcache.jit_buffer_size'),
        ],
    ];
}

function env_summary(array $env): string {
    return sprintf('PHP %s (%s%s), OPcache enable_cli=%s, jit=%s, jit_buffer=%s',
        $env['php_version'],
        $env['thread_safety'],
        $env['debug_build'] ? ', debug' : '',
        $env['opcache']['enable_cli'],
        $env['opcache']['jit'],
        $env['opcache']['jit_buffer_size']);
}

// Corpora under 1 KB default to 300 iters; small means otherwise sit in
// harness noise. An explicit --iters overrides every corpus.
function default_iters(int $bytes, ?int $override): int {
    if ($override !== null) {
        return $override;
    }
    return $bytes < 1024 ? 300 : 50;
}

function bench(callable $fn, string $md, int $iters, int $warmup): array {
    // Warm-up (JIT, opcache, object caches).
    for ($i = 0; $i < $warmup; $i++) {
        $fn($md);
    }

    $times = [];
    for ($i = 0; $i < $iters; $i++) {
        $t0 = hrtime(true);
        $out = $fn($md);
        $t1 = hrtime(true);
        $times[] = ($t1 - $t0) / 1e6; // ms
    }

    sort($times);
    // Trim 10% tails (simple outlier handling). Cap trim at (iters-1)/2
    // per side so at least one sample always remains -- otherwise low
    // --iters values (1, 2, 3) produced an empty trimmed slice and a
    // DivisionByZeroError on the mean computation.
    $trim = min((int)($iters * 0.1), intdiv(max(0, $iters - 1), 2));
    $trimmed = $trim > 0 ? array_slice($times, $trim, -$trim) : $times;
    $mean = array_sum($trimmed) / count($trimmed);

    $n = count($times);
    $mid = intdiv($n, 2);
    $median = ($n % 2 === 1) ? $times[$mid] : ($times[$mid - 1] + $times[$mid]) / 2;
    $p95 = $times[min($n - 1, (int)ceil(0.95 * $n) - 1)];

    return [
        'mean_ms' => $mean,
        'median_ms' => $median,
        'p95_ms' => $p95,
        'ops_sec' => $mean > 0 ? 1000 / $mean : 0,
        'min_ms'  => $times[0],
        'max_ms'  => $times[$n - 1],
    ];
}

$env = bench_env();
$noop = static function (string $md): string {
    return '';
};

$results = [];
foreach ($corpora as $corpus => $md) {
    $size = strlen($md);
    $n = default_iters($size, $itersOpt);
    // Harness noise floor: time an empty closure with the same N and
    // subtract its trimmed mean from every latency stat.
    $baseline = bench($noop, $md, $n, $warmup)['mean_ms'];
    foreach ($parsers as $name => $fn) {
        try {
            $r = bench($fn, $md, $n, $warmup);
            foreach (['mean_ms', 'median_ms', 'p95_ms', 'min_ms', 'max_ms'] as $k) {
                $r[$k] = max(0.0, $r[$k] - $baseline);
            }
            $r['ops_sec'] = $r['mean_ms'] > 0 ? 1000 / $r['mean_ms'] : 0;
            $r['baseline_ms'] = $baseline;
        } catch (\Throwable $e) {
            $r = ['error' => $e->getMessage()];
        }
        $results[] = array_merge(['parser' => $name, 'corpus' => $corpus, 'size' => $size, 'iters' => $n], $r);
    }
}

// Compute speedup vs mdparser for each corpus.
$mdparser_ms = [];
foreach ($results as $row) {
    if ($row['parser'] === 'mdparser' && isset($row['mean_ms'])) {
        $mdparser_ms[$row['corpus']] = $row['mean_ms'];
    }
}
foreach ($results as &$row) {
    if (isset($row['mean_ms']) && isset($mdparser_ms[$row['corpus']])) {
        $row['speedup'] = $row['mean_ms'] / $mdparser_ms[$row['corpus']];
    }
}
unset($row);

$hasErrors = false;
foreach ($results as $row) {
    if (isset($row['error'])) {
        $hasErrors = true;
        break;
    }
}

// Output.
if ($format === 'json') {
    echo json_encode(['env' => $env, 'results' => $results], JSON_PRETTY_PRINT), "\n";
    exit($hasErrors ? 1 : 0);
}

function fmt_size(int $n): string {
    if ($n < 1024) return "{$n} B";
    if ($n < 1024 * 1024) return sprintf("%.1f KB", $n / 1024);
    return sprintf("%.1f MB", $n / 1024 / 1024);
}

if ($format === 'md') {
    echo "| Parser | Corpus | Size | Mean (ms) | Median (ms) | p95 (ms) | Ops/sec | Speedup |\n";
    echo "|---|---|--:|--:|--:|--:|--:|--:|\n";
    foreach ($results as $r) {
        if (isset($r['error'])) {
            printf("| %s | %s | %s | — | — | — | — | ERROR |\n",
                $r['parser'], $r['corpus'], fmt_size($r['size']));
            continue;
        }
        $speedup = isset($r['speedup'])
            ? ($r['parser'] === 'mdparser' ? '—' : sprintf('%.1fx', $r['speedup']))
            : '—';
        printf("| %s | %s | %s | %.3f | %.3f | %.3f | %d | %s |\n",
            $r['parser'], $r['corpus'], fmt_size($r['size']),
            $r['mean_ms'], $r['median_ms'], $r['p95_ms'], (int)$r['ops_sec'], $speedup);
    }
    echo "\n*Environment: " . env_summary($env) . ".*\n";
    exit($hasErrors ? 1 : 0);
}

// Plain text table.
$itersLabel = $hasItersOpt ? (string)$itersOpt : '300 (<1 KB) / 50 (>=1 KB)';
printf("mdparser benchmark  (iters=%s, warmup=%d)\n\n", $itersLabel, $warmup);
printf("%-20s %-10s %10s %10s %10s %10s %12s %10s\n",
    'parser', 'corpus', 'size', 'mean(ms)', 'median(ms)', 'p95(ms)', 'ops/sec', 'speedup');
echo str_repeat('-', 98), "\n";
foreach ($results as $r) {
    if (isset($r['error'])) {
        printf("%-20s %-10s %10s   ERROR: %s\n",
            $r['parser'], $r['corpus'], fmt_size($r['size']), $r['error']);
        continue;
    }
    $speedup = isset($r['speedup'])
        ? ($r['parser'] === 'mdparser' ? '   —' : sprintf('%7.1fx', $r['speedup']))
        : '   —';
    printf("%-20s %-10s %10s %10.3f %10.3f %10.3f %12d %10s\n",
        $r['parser'], $r['corpus'], fmt_size($r['size']),
        $r['mean_ms'], $r['median_ms'], $r['p95_ms'], (int)$r['ops_sec'], $speedup);
}
echo "\nEnvironment: " . env_summary($env) . "\n";

exit($hasErrors ? 1 : 0);
