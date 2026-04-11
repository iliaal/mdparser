<?php
/**
 * mdparser benchmark harness.
 *
 * Measures wall time for each parser on each corpus over N iterations.
 * Reports mean ms/op, ops/sec, and relative speed vs mdparser.
 *
 * Usage:
 *   php -d extension=../modules/mdparser.so bench/run.php
 *   php -d extension=../modules/mdparser.so bench/run.php --iters=200
 *   php -d extension=../modules/mdparser.so bench/run.php --parsers=mdparser,parsedown
 *   php -d extension=../modules/mdparser.so bench/run.php --format=md    # for README embedding
 *
 * Output columns:
 *   parser | corpus | size | iters | mean_ms | ops/sec | speedup
 */

declare(strict_types=1);

require __DIR__ . '/vendor/autoload.php';

if (!extension_loaded('mdparser')) {
    fwrite(STDERR, "mdparser extension not loaded. Run with -d extension=...\n");
    exit(1);
}

$opts = getopt('', ['iters::', 'parsers::', 'format::', 'warmup::']);
$iters      = (int)($opts['iters']   ?? 50);
$warmup     = (int)($opts['warmup']  ?? 5);
$parserList = isset($opts['parsers']) ? explode(',', $opts['parsers']) : null;
$format     = $opts['format'] ?? 'table';

$parsers = [
    'mdparser' => function (string $md): string {
        static $p = null;
        $p ??= new MdParser\Parser();
        return $p->toHtml($md);
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
if (extension_loaded('mbstring') || ($opts['league'] ?? false)) {
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
    $parsers = array_intersect_key($parsers, array_flip($parserList));
}

$corpora = [];
foreach (glob(__DIR__ . '/corpora/*.md') as $path) {
    $corpora[basename($path, '.md')] = file_get_contents($path);
}
ksort($corpora);

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
    // Trim 10% tails (simple outlier handling).
    $trim = max(1, (int)($iters * 0.1));
    $trimmed = array_slice($times, $trim, -$trim);
    $mean = array_sum($trimmed) / count($trimmed);

    return [
        'mean_ms' => $mean,
        'ops_sec' => 1000 / $mean,
        'min_ms'  => $times[0],
        'max_ms'  => $times[count($times) - 1],
    ];
}

$results = [];
foreach ($corpora as $corpus => $md) {
    $size = strlen($md);
    foreach ($parsers as $name => $fn) {
        try {
            $r = bench($fn, $md, $iters, $warmup);
        } catch (\Throwable $e) {
            $r = ['error' => $e->getMessage()];
        }
        $results[] = array_merge(['parser' => $name, 'corpus' => $corpus, 'size' => $size, 'iters' => $iters], $r);
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

// Output.
if ($format === 'json') {
    echo json_encode($results, JSON_PRETTY_PRINT), "\n";
    exit(0);
}

function fmt_size(int $n): string {
    if ($n < 1024) return "{$n} B";
    if ($n < 1024 * 1024) return sprintf("%.1f KB", $n / 1024);
    return sprintf("%.1f MB", $n / 1024 / 1024);
}

if ($format === 'md') {
    echo "| Parser | Corpus | Size | Mean (ms) | Ops/sec | Speedup |\n";
    echo "|---|---|--:|--:|--:|--:|\n";
    foreach ($results as $r) {
        if (isset($r['error'])) {
            printf("| %s | %s | %s | — | — | ERROR |\n",
                $r['parser'], $r['corpus'], fmt_size($r['size']));
            continue;
        }
        $speedup = isset($r['speedup'])
            ? ($r['parser'] === 'mdparser' ? '—' : sprintf('%.1fx', $r['speedup']))
            : '—';
        printf("| %s | %s | %s | %.3f | %d | %s |\n",
            $r['parser'], $r['corpus'], fmt_size($r['size']),
            $r['mean_ms'], (int)$r['ops_sec'], $speedup);
    }
    exit(0);
}

// Plain text table.
printf("mdparser benchmark  (iters=%d, warmup=%d, PHP %s)\n\n", $iters, $warmup, PHP_VERSION);
printf("%-20s %-10s %10s %10s %12s %10s\n",
    'parser', 'corpus', 'size', 'mean(ms)', 'ops/sec', 'speedup');
echo str_repeat('-', 76), "\n";
foreach ($results as $r) {
    if (isset($r['error'])) {
        printf("%-20s %-10s %10s   ERROR: %s\n",
            $r['parser'], $r['corpus'], fmt_size($r['size']), $r['error']);
        continue;
    }
    $speedup = isset($r['speedup'])
        ? ($r['parser'] === 'mdparser' ? '   —' : sprintf('%7.1fx', $r['speedup']))
        : '   —';
    printf("%-20s %-10s %10s %10.3f %12d %10s\n",
        $r['parser'], $r['corpus'], fmt_size($r['size']),
        $r['mean_ms'], (int)$r['ops_sec'], $speedup);
}
