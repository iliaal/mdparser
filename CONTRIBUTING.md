# Contributing to mdparser

## Requirements

- PHP 8.2 or later (debug build recommended for development:
  `--enable-debug`)
- C compiler: GCC 11+, Clang 14+, or MSVC 2019+
- `phpize` and `php-config` (from `php-dev` or `php8.x-dev`)
- GNU Make (Unix) or Visual Studio (Windows)

mdparser embeds md4c directly. No external libraries are required
to build or run.

## Bug reports

Use the [GitHub issue tracker](https://github.com/iliaal/mdparser/issues).
Include:

- PHP version (`php -v`)
- mdparser version (`php -r 'echo phpversion("mdparser");'`)
- Operating system and compiler version
- Minimal reproducing code: the markdown input, the `MdParser\Options`
  you passed, and the rendered output you got vs. expected
- Any error messages, exceptions, or crash output

Before filing, try to reproduce against the latest `master` branch.

**For security issues, do not file a public issue.** See
[SECURITY.md](SECURITY.md) for the private reporting process.

## Pull requests

1. Fork and clone the repo.
2. Create a topic branch off `master`.
3. Make your changes.
4. Add or update tests in `tests/` (PHPT format).
5. Build and run the full suite:

   ```sh
   phpize
   ./configure --enable-mdparser --enable-mdparser-dev
   make -j$(nproc)

   TEST_PHP_EXECUTABLE=$(which php) \
     TEST_PHP_ARGS="-d extension=$(pwd)/modules/mdparser.so" \
     NO_INTERACTION=1 \
     php run-tests.php tests/
   ```

6. Verify zero compiler warnings (`--enable-mdparser-dev` sets
   `-Wall -Wextra`; the CI treats any warning as a build failure) and
   that the full PHPT suite passes.
7. Validate the package manifest didn't regress:

   ```sh
   composer validate --strict
   curl -fsSL -o /tmp/pie.phar \
     https://github.com/php/pie/releases/latest/download/pie.phar
   php /tmp/pie.phar repository:add path .
   php /tmp/pie.phar build iliaal/mdparser:*@dev
   ```

8. Push and open a PR against `master`.

### Commit message conventions

- Short imperative subject line (≤ 72 chars): "Add foo", "Fix bar",
  "Update baz".
- Body wraps at 72 columns, explains **why** not **what**.
- No `Co-Authored-By` lines. No AI attribution.
- Audit the message against `git show --stat HEAD` before pushing —
  if the subject claims a fix is in X file, the diff had better show
  X.

### Test guidelines

- Tests use PHPT format. See existing tests for examples.
- Prefer exact-byte expectations via `--EXPECT--`. Only use
  `--EXPECTF--` when the output legitimately varies (e.g. line
  numbers in exception traces, object IDs).
- When a test depends on CommonMark spec behavior, put the input in
  `tests/fixtures/` and add a byte-match test against the parser's
  output, comparing to the expected output in
  `tests/fixtures/commonmark-spec.txt` (that file is the CommonMark
  0.31 `spec.txt`; do not modify it).
- Parity tests under `tests/parity/` hold fixture corpora from other
  PHP markdown libraries (Parsedown, cebe, michelf). Divergences
  are pinned by exact counts and file lists so any unintended drift
  is visible. If your change moves a parity number, explain why in
  the commit body.

### Code style

- Hand-written wrapper `.c` files use 4-space indentation and the file
  header block from `mdparser.c`. BSD 3-Clause license. Generated and
  vendored files keep their native style.
- Method implementations use `PHP_METHOD(MdParser_Class, name)` and
  `ZEND_PARSE_PARAMETERS_NONE()` for zero-arg methods.
- Class registration goes through `mdparser_arginfo.h` (generated
  from `mdparser.stub.php` by `php $PHP_SRC/build/gen_stub.php`).
  Do not hand-edit `mdparser_arginfo.h`.
- Memory: use PHP's `emalloc`/`efree` in the wrapper code at the Zend
  boundary. md4c uses libc `malloc`/`free` internally — don't route
  md4c-internal allocation through Zend MM, and don't mix the two.
- In `zend_try` / `catch` blocks, don't duplicate cleanup in the
  catch arm; set a `bool bailout` flag, fall through to the shared
  cleanup, then bailout at the end if the flag was set.

### Vendored md4c

`vendor/md4c/` is a mostly clean upstream copy of md4c. It carries the
single local code-span whitespace patch documented in `vendor/VENDOR.md`;
do not add cherry-picks or hand-edited build shims. md4c targets
CommonMark 0.31 natively, so there's no spec gap to bridge in the
vendored sources.

Refreshing md4c is a drop-in file swap:

1. Copy `md4c.c`, `md4c.h`, `md4c-html.c`, `md4c-html.h`,
   `entity.c`, `entity.h`, and `LICENSE.md` from the new md4c
   release into `vendor/md4c/`.
2. Bump `MDPARSER_MD4C_VERSION` in `php_mdparser.h`.
3. Rebuild and re-check the spec baseline. If
   `tests/005_commonmark_spec.phpt` moves, explain the delta in the
   commit message.

To surface a new md4c block or span type, add cases to all three
renderers (`mdparser_md4c_html.c`, `mdparser_md4c_xml.c`,
`mdparser_md4c_ast.c`) plus a matching `MdParser\Options` flag. See
`vendor/VENDOR.md` for the full layout and refresh details.

## Release workflow

For maintainers cutting a new version:

1. Bump `PHP_MDPARSER_VERSION` in `php_mdparser.h` to the new
   semver and update the top section of `CHANGELOG.md`. The
   current `[Unreleased]` entries become the new version
   section with a release date and a compare link.
2. Sanity-check the bump:

   ```sh
   scripts/check_version.sh
   ```

   This verifies `PHP_MDPARSER_VERSION` in `php_mdparser.h` matches
   the top section of `CHANGELOG.md` and that the version is a
   valid SemVer 2.0.0 string.

3. Commit + push to master. CI (Tests + Windows Build) must be
   green on the resulting commit before tagging.
4. Create and push the annotated tag. Use bare semver (`0.4.4`, not
   `v0.4.4`) to match the existing convention:

   ```sh
   git tag -a X.Y.Z -m "mdparser X.Y.Z"
   git push origin X.Y.Z
   ```

5. Publish the GitHub release for that existing tag. A tag push alone does
   not start the binary workflows; both `release-linux.yml` and the release
   lane in `windows.yml` trigger on the published-release event.

   ```sh
   gh release create X.Y.Z --verify-tag \
     --title "mdparser X.Y.Z" --notes-file /path/to/release-notes.md
   ```

6. Confirm both release workflows finish successfully. `windows.yml` runs
   PHP 8.2-8.5 across TS/NTS and x86/x64 and uploads the tested DLL archives.
   `release-linux.yml` builds Linux x86_64/arm64 and macOS arm64 for PHP 8.4
   and 8.5, unpacks and loads each exact `.so`, then uploads it. If a Unix
   lane needs recovery, dispatch `release-linux.yml` manually with the same
   tag; the workflow verifies that it checked out the tag commit before it
   builds.
7. Packagist's GitHub webhook (configured on the repo) fires on
   the tag push and re-scans versions. `pie install
   iliaal/mdparser` resolves to the new tag within a minute or
   two. If Packagist hasn't indexed the tag yet, users can fall
   back to `pie install iliaal/mdparser:@dev` or hit the
   `api/update-package` endpoint with your Packagist API token
   to force a re-crawl. See
   `~/ai/wiki/tools/packagist-quirks.md` for the full list of
   Packagist indexing gotchas.
8. Before the first tag of any new release cycle, double-check
   that `composer.json` exists in the tree at HEAD (`git ls-tree
   HEAD | grep composer.json`). Packagist silently skips tags
   whose commit doesn't contain `composer.json` at the root —
   mdparser's 0.1.0 release hit this trap.

### License

By submitting a patch you agree to license your contribution under
the same license as the project (BSD 3-Clause).
