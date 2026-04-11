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
schema), downloads the source from the matching GitHub release, and
handles the configure + build + install cycle plus writing the INI
fragment automatically. You'll see output like:

```
🥧 PHP Installer for Extensions (PIE) 1.4.0, from The PHP Foundation
Found package: iliaal/mdparser:0.1.0 which provides ext-mdparser
Extracted iliaal/mdparser:0.1.0 source to: /root/.pie/.../vendor/iliaal/mdparser
phpize complete.
Configure complete with options: --with-php-config=/usr/local/bin/php-config
Build complete: /root/.pie/.../modules/mdparser.so
Install complete: /usr/local/lib/php/extensions/.../mdparser.so
✅ Extension is enabled and loaded in /usr/local/bin/php
```

### PIE build-tool requirements

PIE 1.4.0 requires a few build tools beyond what a minimal PHP
install ships with. On Debian/Ubuntu:

```bash
sudo apt-get install -y bison libtool-bin
```

On macOS:

```bash
brew install bison libtool
```

The PHP image `php:8.x-cli` from Docker Hub does not ship with these
pre-installed. `scripts/pie-smoke.sh` in this repo installs them for
you before running PIE and is the reference end-to-end verification
harness.

### Before the first stable tag is indexed

If you install mdparser immediately after a new release tag is
pushed, Packagist may not have crawled the tag yet. In that window
`pie install iliaal/mdparser` will fail with "Unable to find an
installable package ... with minimum stability stable". You can
either:

- Force Packagist to re-crawl from the package page on
  packagist.org (logged-in maintainer gets a "Force Update" button),
  then retry `pie install iliaal/mdparser`.
- Install the master-branch development version explicitly:

  ```bash
  pie install iliaal/mdparser:@dev
  ```

  This resolves to `dev-master` and installs the current HEAD. Once
  the stable tag is indexed, switch back to the plain form.

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
