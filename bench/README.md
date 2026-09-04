# mdparser benchmarks

This directory contains a benchmark harness that compares mdparser's
throughput to the major pure-PHP Markdown libraries on real-world
inputs. Results are reproducible locally with the commands below.

## TL;DR

mdparser is **~10-20x faster** than the fastest pure-PHP CommonMark
libraries on all three corpora we measure (200 B, 1.8 KB, 200 KB), and
up to ~50x faster than the slowest. (The previous cmark-gfm backend was
~5-9x; the md4c migration roughly doubled throughput.)

| Corpus | mdparser ops/sec | Best pure-PHP ops/sec | Speedup |
|---|--:|--:|--:|
| 200 B   | ~553,000 | ~27,000 (Parsedown)  | ~20x |
| 1.8 KB  | ~114,000 | ~5,600 (cebe/GitHub) | ~20x |
| 200 KB  | ~938     | ~92 (cebe/GitHub)    | ~10x |

The 200 KB corpus is CommonMark's own `spec.txt` (our
`tests/fixtures/commonmark-spec.txt`). mdparser handles ~930 full
spec-sized documents per second on a single core.

## Full results

Latest measurement, iters=300, warmup=30, PHP 8.4.22-dev (NTS,
non-DEBUG, non-ASan) on Linux WSL2, with all parsers at their default
configuration:

| Parser | Corpus | Size | Mean (ms) | Ops/sec | Speedup |
|---|---|--:|--:|--:|--:|
| mdparser | small | 200 B | 0.002 | 553517 | — |
| parsedown | small | 200 B | 0.037 | 26980 | 20.5x |
| cebe/markdown | small | 200 B | 0.041 | 24140 | 22.9x |
| michelf | small | 200 B | 0.092 | 10865 | 50.9x |
| mdparser | medium | 1.8 KB | 0.009 | 114278 | — |
| parsedown | medium | 1.8 KB | 0.242 | 4140 | 27.6x |
| cebe/markdown | medium | 1.8 KB | 0.178 | 5605 | 20.4x |
| michelf | medium | 1.8 KB | 0.427 | 2343 | 48.8x |
| mdparser | large | 200.2 KB | 1.066 | 938 | — |
| parsedown | large | 200.2 KB | 11.962 | 83 | 11.2x |
| cebe/markdown | large | 200.2 KB | 10.842 | 92 | 10.2x |
| michelf | large | 200.2 KB | 22.395 | 44 | 21.0x |

**Speedup column reads as "X times slower than mdparser".** Higher
numbers = mdparser wins more decisively. The comparison is fair in
the sense that all parsers run their default rendering path on the
same UTF-8 input and produce HTML output; it's not fair in the sense
that each library supports a slightly different dialect and does
different amounts of work. See "Methodology caveats" below.

## Corpora

| File | Size | Content |
|---|--:|---|
| `corpora/small.md` | 200 B | Short paragraph, some inline formatting, a 3-item bullet list |
| `corpora/medium.md` | 1.8 KB | Typical README: intro + install + features list + GFM table + task list + code blocks |
| `corpora/large.md` | 200.2 KB | A copy of the CommonMark 0.31 `spec.txt` (the spec document, which is markdown about markdown) |

The three sizes are chosen to cover typical use cases:

- **Small** (~200 B) simulates user comments, chat messages, commit
  descriptions. High-frequency, throughput-bound.
- **Medium** (~2 KB) simulates typical README files and issue bodies.
  The most common real-world size.
- **Large** (~200 KB) simulates a documentation page or long-form
  article. Exercises parser scaling.

## Parsers under test

| Package | Version | Mode | Notes |
|---|---|---|---|
| mdparser | 0.4.1 | `new Parser()` | Defaults: GFM extensions on, safe mode on |
| erusev/parsedown | 1.8.0 | `new Parsedown()` | Simplest and historically fastest pure-PHP; GFM tables + strikethrough |
| cebe/markdown | 1.2.1 | `new GithubMarkdown()` | GFM dialect |
| michelf/php-markdown | 2.0.0 | `new MarkdownExtra()` | Gruber 1.0.3 + Extra (definition lists, footnotes, abbreviations) |

### league/commonmark

[thephpleague/commonmark](https://github.com/thephpleague/commonmark)
is intentionally absent from the default comparison because it
requires the native `mbstring` extension (it calls `mb_strcut()`,
which `symfony/polyfill-mbstring` does not implement). The minimal
PHP build used for CI benchmarking doesn't ship `mbstring`, so it
would error out on the large corpus.

To include it locally if your PHP has mbstring:

```bash
php -d extension=./modules/mdparser.so bench/run.php --league
```

Based on comparable feature sets and spec-compliance class,
league/commonmark is expected to benchmark within the same order of
magnitude as `cebe/markdown` (both are spec-compliant or near-compliant
CommonMark parsers). Run it yourself on a PHP with mbstring if you
want concrete numbers.

## Running the benchmark

> **Build against an optimized PHP.** Unoptimized debug builds inflate
> wall time several-fold and distort the speedup ratios. Point
> `--with-php-config` at an optimized build for benchmarking.

```bash
# Install the pure-PHP parsers:
cd bench
composer install

# Run with default iteration counts (300 for sub-KB corpora, 50 otherwise):
php -d extension=../modules/mdparser.so run.php

# Enable OPcache with JIT so the pure-PHP parsers run at full speed:
php -d opcache.enable_cli=1 -d opcache.jit_buffer_size=64M \
    -d extension=../modules/mdparser.so run.php

# Override the iteration count for every corpus:
php -d extension=../modules/mdparser.so run.php --iters=300

# JSON output for scripted comparison:
php -d extension=../modules/mdparser.so run.php --format=json

# Markdown table output (for README embedding):
php -d extension=../modules/mdparser.so run.php --format=md

# Only a subset of parsers:
php -d extension=../modules/mdparser.so run.php --parsers=mdparser,parsedown
```

Sub-KB corpora default to 300 iterations, larger ones to 50;
`--iters` overrides every corpus. The harness does 5 warm-up iterations
(not counted), subtracts an empty-closure baseline from every latency
stat, and trims 10% from both tails before computing the mean, so one
unlucky GC pause or OS-level hiccup doesn't dominate. Median and p95
are reported alongside the trimmed mean in every output format.

## Methodology

Each (parser, corpus) pair is measured with `hrtime(true)` around a
single call, repeated N times. The same parser instance is reused
across iterations (via `static` variable in the closure), so object
construction cost is amortized into warm-up.

Reported `mean_ms` is the mean of the middle 80% of samples after
sorting (10% trimmed from each tail). This is robust to the occasional
GC pause or page fault without throwing away real data. `ops_sec` is
`1000 / mean_ms`. `median_ms` and `p95_ms` come from the full sorted
sample set. Every latency stat is net of an empty-closure baseline
measured with the same iteration count, so `hrtime()` and closure-call
overhead doesn't inflate small-corpus means.

`--format=json` wraps rows as `{"env": ..., "results": [...]}`. `env`
records the PHP version, NTS/ZTS thread safety, debug flag, and the
OPcache settings (`enable_cli`, `jit`, `jit_buffer_size`); table and md
output carry the same record as a footer. Enable OPcache with JIT when
comparing against interpreted parsers — without it the pure-PHP
baseline runs unoptimized and the speedup flatters mdparser.

The `speedup` column is simply `other_mean_ms / mdparser_mean_ms` for
the same corpus. A value of `5.0x` means that parser took 5 times
longer than mdparser on the same input.

## Methodology caveats

1. **Feature parity is approximate.** Every parser runs in its default
   mode. Parsedown's base class handles fewer features than GFM,
   while michelf's `MarkdownExtra` handles definition lists and
   abbreviations that mdparser doesn't support. The absolute times
   are what you'd see in real use; the absolute features list is not
   identical across the column.
2. **Output isn't byte-identical across parsers.** See
   `tests/parity/` for the divergence patterns. This benchmark
   measures throughput on shared inputs, not behavioral compatibility.
3. **Warm caches.** The harness pre-warms CPU caches with 5 iterations
   before measuring. First-hit numbers would be substantially higher
   for all parsers.
4. **Single-threaded.** mdparser is a conventional PHP extension; it
   runs single-threaded per request. Multi-request scaling on a
   process-per-request PHP model (FPM, CLI-server) is roughly linear
   — the wins stack across workers.

## When mdparser is NOT a win

- **Tiny one-off parses.** If you're parsing one ~50-byte string at
  application startup, the extension load cost dwarfs the per-parse
  difference. Use whatever's already in your composer deps.
- **You need features mdparser doesn't support.** Definition lists,
  abbreviations, attribute syntax, heading permalinks, TOC,
  frontmatter, mentions, custom containers — these are
  all handled by league/commonmark's extensions and not by mdparser.
  Raw speed isn't useful if the feature you need isn't there.
- **You can't install C extensions.** Shared hosting that ships
  pre-compiled PHP without PECL/PIE support rules out mdparser entirely.

For those cases, [league/commonmark](https://github.com/thephpleague/commonmark)
is the most featureful pure-PHP option and is actively maintained.

## Reproducing on your machine

```bash
git clone https://github.com/iliaal/mdparser.git
cd mdparser
phpize && ./configure --enable-mdparser && make -j
cd bench
composer install
php -d extension=../modules/mdparser.so run.php --iters=200
```

Your absolute numbers will differ from the table above — Linux
laptops with high turbo clocks can double these throughput figures,
constrained cloud VMs can halve them. The **ratios** are what matters
and are stable across hardware.
