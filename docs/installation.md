# Installation

mdparser ships as a PHP extension with zero external runtime
dependencies. The parser ([md4c](https://github.com/mity/md4c)) compiles
directly into the extension shared object.

## Requirements

- PHP 8.2 or later
- A C compiler (GCC, Clang, or MSVC)
- `phpize` and `php-config` (usually from `php-dev` or `php8.x-dev`)
- GNU Make on Unix; Visual Studio on Windows

No external libraries are required. md4c is bundled under
`vendor/md4c/`.

## PIE

```bash
pie install iliaal/mdparser
```

PIE resolves the package through the root `composer.json` (with `type` set
to `php-ext`). Its `download-url-method` list tries `pre-packaged-binary`
before `composer-default`: a matching release binary can avoid a source
build. Available binaries depend on the selected release, PHP version,
thread-safety mode, operating system, architecture and libc; check that
release's assets rather than assuming every supported source-build target
has a binary.

For example, [0.8.2](https://github.com/iliaal/mdparser/releases/tag/0.8.2)
provides Unix binaries for PHP 8.4 and 8.5 NTS on glibc Linux x86_64/arm64
and Apple Silicon macOS. Other targets may require a source build and its
build tools.

Starting with 0.9.0, prebuilt Linux and macOS binaries require PHP 8.4.28+
or 8.5.13+ (any PHP 8.6 release). On an older 8.4 or 8.5 patch release the
extension refuses to start with a warning; build from source there.

The following historical PIE 1.4.0 output illustrates the source-build path,
not what every installation prints:

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

The Docker Hub `php:8.x-cli` image doesn't include them.
`scripts/pie-smoke.sh` provisions the release smoke environment (including
`git`, CA certificates and `unzip`) and checks the published-package install.
The Tests workflow instead runs `pie build` against the checkout on pushes
to `master` and pull requests targeting `master`. That build-only check
does not verify downloading a published binary.

### Before the first stable tag is indexed

Right after a release tag is pushed, Packagist may not have crawled it
yet, and `pie install iliaal/mdparser` fails with "Unable to find an
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

- `--enable-mdparser`: required, builds the extension.
- `--enable-mdparser-dev`: optional, treats wrapper compiler warnings as
  errors and enables stricter prototype checks. Use it for development,
  not release builds.

## Platform notes

### Linux

Builds cleanly with GCC 11+ or Clang 14+.

### macOS

Xcode command-line tools provide `cc` and `make`. `brew install php`
ships a compatible `phpize`. Intel and Apple Silicon are both supported.

### Windows

The Windows build uses `config.w32` and the `php/php-windows-builder`
GitHub Action (see `.github/workflows/windows.yml`). Prebuilt DLLs are
attached per release. Release 0.8.2 has PHP 8.2-8.5 DLLs, TS and NTS,
x64 and x86. The workflow also targets PHP 8.6, but a workflow lane alone
does not mean a matching archive has been published; check the selected
release's assets.

To build on Windows from source:

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
$ast = $p->toAst("# Hi");

$ok = $p->toHtml("# Hello") === "<h1>Hello</h1>\n"
    && $p->toHtml("~~strike~~") === "<p><del>strike</del></p>\n"
    && $ast['type'] === 'document'
    && $ast['children'][0]['type'] === 'heading';

if (!$ok) {
    fwrite(STDERR, "mdparser self-test failed\n");
    exit(1);
}

echo "mdparser " . phpversion('mdparser') . " OK\n";
```

## Running the test suite

After building:

```bash
make test
```

The suite includes a CommonMark 0.31 conformance check that runs every
example from `spec.txt` and pins md4c's result. See
`docs/spec-coverage.md` for the current baseline.
