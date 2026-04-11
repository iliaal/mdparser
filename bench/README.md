# mdparser benchmarks

This directory contains a benchmark harness that compares mdparser's
throughput to the major pure-PHP Markdown libraries on real-world
inputs. Results are reproducible locally with the commands below.

## TL;DR

mdparser is **~15-30x faster** than the fastest pure-PHP CommonMark
libraries on all three corpora we measure (200 B, 1.8 KB, 200 KB).

| Corpus | mdparser ops/sec | Best pure-PHP ops/sec | Speedup |
|---|--:|--:|--:|
| 200 B   | ~30,000 | ~1,650 (Parsedown)   | ~18x |
| 1.8 KB  | ~5,700  | ~370 (cebe/GitHub)   | ~15x |
| 200 KB  | ~105    | ~6 (cebe/GitHub)     | ~16x |

The 200 KB corpus is CommonMark's own `spec.txt` (our
`tests/fixtures/commonmark-spec.txt`). mdparser handles ~100 full
spec-sized documents per second on a single core.

## Full results

Latest measurement, iters=200, warmup=5, PHP 8.4.21-dev on Linux
WSL2, with all parsers at their default configuration:

| Parser | Corpus | Size | Mean (ms) | Ops/sec | Speedup |
|---|---|--:|--:|--:|--:|
| mdparser | small | 200 B | 0.033 | 30447 | — |
| parsedown | small | 200 B | 0.606 | 1651 | 18.4x |
| cebe/markdown | small | 200 B | 0.740 | 1350 | 22.5x |
| michelf | small | 200 B | 0.993 | 1006 | 30.2x |
| mdparser | medium | 1.8 KB | 0.176 | 5697 | — |
| parsedown | medium | 1.8 KB | 3.074 | 325 | 17.5x |
| cebe/markdown | medium | 1.8 KB | 2.674 | 374 | 15.2x |
| michelf | medium | 1.8 KB | 4.770 | 209 | 27.2x |
| mdparser | large | 200.2 KB | 9.460 | 105 | — |
| parsedown | large | 200.2 KB | 164.554 | 6 | 17.4x |
| cebe/markdown | large | 200.2 KB | 148.272 | 6 | 15.7x |
| michelf | large | 200.2 KB | 175.550 | 5 | 18.6x |

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
| `corpora/large.md` | 200.2 KB | A copy of cmark's own `spec.txt` (the CommonMark 0.31 spec document, which is markdown about markdown) |

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
| mdparser | 0.1.0-dev | `new Parser()` | Defaults: GFM extensions on, safe mode on |
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

```bash
# Install the pure-PHP parsers:
cd bench
composer install

# Run with default iteration count (50):
php -d extension=../modules/mdparser.so run.php

# Higher sample count for tighter numbers (200 is good):
php -d extension=../modules/mdparser.so run.php --iters=200

# JSON output for scripted comparison:
php -d extension=../modules/mdparser.so run.php --format=json

# Markdown table output (for README embedding):
php -d extension=../modules/mdparser.so run.php --format=md

# Only a subset of parsers:
php -d extension=../modules/mdparser.so run.php --parsers=mdparser,parsedown
```

The harness does 5 warm-up iterations (not counted) and 10% trim on
both tails before computing the mean, so one unlucky GC pause or
OS-level hiccup doesn't dominate.

## Methodology

Each (parser, corpus) pair is measured with `hrtime(true)` around a
single call, repeated N times. The same parser instance is reused
across iterations (via `static` variable in the closure), so object
construction cost is amortized into warm-up.

Reported `mean_ms` is the mean of the middle 80% of samples after
sorting (10% trimmed from each tail). This is robust to the occasional
GC pause or page fault without throwing away real data. `ops_sec` is
`1000 / mean_ms`.

The `speedup` column is simply `other_mean_ms / mdparser_mean_ms` for
the same corpus. A value of `15.0x` means that parser took 15 times
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
3. **PHP version matters.** OPcache enabled makes the pure-PHP parsers
   faster by 2-3x. These numbers are from PHP 8.4 with default OPcache
   settings, matching what a typical production server would run.
4. **Warm caches.** The harness pre-warms CPU caches, opcode cache,
   and JIT (where applicable) with 5 iterations before measuring.
   First-hit numbers would be substantially higher for all parsers.
5. **Single-threaded.** mdparser is a conventional PHP extension; it
   runs single-threaded per request. Multi-request scaling on a
   process-per-request PHP model (FPM, CLI-server) is roughly linear
   — the wins stack across workers.

## When mdparser is NOT a win

- **Tiny one-off parses.** If you're parsing one ~50-byte string at
  application startup, the extension load cost dwarfs the per-parse
  difference. Use whatever's already in your composer deps.
- **You need features mdparser doesn't support.** Definition lists,
  abbreviations, attribute syntax, heading permalinks, TOC,
  frontmatter, mentions, LaTeX math, custom containers — these are
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
