# Installation

mdparser ships as a PHP extension with zero external runtime
dependencies. The parser (cmark-gfm with targeted cherry-picks) is
embedded directly in the extension shared object.

## Requirements

- PHP 8.3 or later
- A C compiler (GCC, Clang, or MSVC)
- `phpize` and `php-config` (usually from `php-dev` or `php8.x-dev`)
- GNU Make on Unix; Visual Studio on Windows

No external libraries are required. cmark-gfm is bundled under
`vendor/cmark/`.

## PECL

```bash
pecl install mdparser
```

The channel is `pecl.php.net`. After install, enable the extension:

```bash
echo 'extension=mdparser.so' | sudo tee /etc/php/conf.d/mdparser.ini
```

Verify:

```bash
php -r '(new MdParser\Parser)->toHtml("# Hi") === "<h1>Hi</h1>\n" or exit(1);'
echo "OK"
```

## PIE

```bash
pie install iliaal/mdparser
```

PIE resolves the package via the canonical `composer.json` at the repo
root (which declares `type: "php-ext"` and the `configure-options`
schema). It handles the configure + build + install cycle and writes
the INI fragment automatically.

### PIE 1.4 Packagist requirement

PIE 1.4.0 only resolves packages via Packagist.org — there is no
CLI flag or composer-config escape hatch for local paths or private
git URLs. If you're testing `pie install` against a development
clone of mdparser that isn't yet published to Packagist, PIE will
report "Unable to find an installable package iliaal/mdparser".

For that case, bypass PIE and build directly:

```bash
git clone https://github.com/iliaal/mdparser.git
cd mdparser
phpize && ./configure --enable-mdparser
make -j$(nproc)
sudo make install
echo 'extension=mdparser.so' | sudo tee /etc/php/conf.d/mdparser.ini
```

`scripts/pie-smoke.sh` runs the full build + install + smoke test in
a clean `php:8.4-cli` Docker container and is what I use to verify
the install path end-to-end. Needs `bison` and `libtool-bin` on the
container (the script installs both).

## From source

```bash
git clone https://github.com/iliaal/mdparser.git
cd mdparser
phpize
./configure --enable-mdparser
make -j$(nproc)
sudo make install
```

Enable the extension:

```bash
echo 'extension=mdparser.so' | sudo tee /etc/php/conf.d/mdparser.ini
```

## Configure options

- `--enable-mdparser` — required, builds the extension.
- `--enable-mdparser-dev` — optional, adds `-Wall -Wextra
  -Wno-unused-parameter` to the wrapper-code compile line. Use during
  development; don't use for release builds.

## Platform notes

### Linux

Build cleanly with GCC 11+ or Clang 14+. No known platform issues.

### macOS

Xcode command-line tools provide `cc` and `make`. `brew install php`
ships a compatible `phpize`. Intel and Apple Silicon both supported.

### Windows

The Windows build uses `config.w32` and the `php/php-windows-builder`
GitHub Action (see `.github/workflows/windows.yml`). Prebuilt DLLs are
attached to GitHub releases for PHP 8.3-8.5, TS and NTS, x64 and x86.

If you're building Windows from source manually:

```
phpize
configure --enable-mdparser
nmake
```

## Verification

After installing, run a self-test:

```php
<?php
$p = new MdParser\Parser();
assert($p->toHtml("# Hello") === "<h1>Hello</h1>\n");
assert($p->toHtml("~~strike~~") === "<p><del>strike</del></p>\n");
$ast = $p->toAst("# Hi");
assert($ast['type'] === 'document');
assert($ast['children'][0]['type'] === 'heading');
echo "mdparser " . phpversion('mdparser') . " OK\n";
```

## Running the test suite

After building:

```bash
make test
```

The test suite includes a 652-example CommonMark 0.31 spec conformance
check that takes ~1 second to run.
