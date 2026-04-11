# Contributing to mdparser

## Requirements

- PHP 8.3 or later (debug build recommended for development:
  `--enable-debug`)
- C compiler: GCC 11+, Clang 14+, or MSVC 2019+
- `phpize` and `php-config` (from `php-dev` or `php8.x-dev`)
- GNU Make (Unix) or Visual Studio (Windows)

mdparser embeds cmark-gfm directly. No external libraries are required
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
   that all 12+ tests pass.
7. Validate the package manifest didn't regress:

   ```sh
   php scripts/validate_package.php
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
  `tests/fixtures/` and add a byte-match test against cmark's own
  expected output from `tests/fixtures/commonmark-spec.txt` (that
  file is the cmark 0.31.2 spec; do not modify it).
- Parity tests under `tests/parity/` hold fixture corpora from other
  PHP markdown libraries (Parsedown, cebe, michelf). Divergences
  are pinned by exact counts and file lists so any unintended drift
  is visible. If your change moves a parity number, explain why in
  the commit body.

### Code style

- Wrapper `.c` files use tab indentation and the file header block
  from `mdparser.c`. PHP License 3.01.
- Method implementations use `PHP_METHOD(MdParser_Class, name)` and
  `ZEND_PARSE_PARAMETERS_NONE()` for zero-arg methods.
- Class registration goes through `mdparser_arginfo.h` (generated
  from `mdparser.stub.php` by `php $PHP_SRC/build/gen_stub.php`).
  Do not hand-edit `mdparser_arginfo.h`.
- Memory: use PHP's `emalloc`/`efree` at the Zend boundary. The
  vendored cmark sources use their own allocator — don't mix.
- In `zend_try` / `catch` blocks, don't duplicate cleanup in the
  catch arm; set a `bool bailout` flag, fall through to the shared
  cleanup, then bailout at the end if the flag was set.

### Vendored cmark changes

`vendor/cmark/` is a patched cmark-gfm tree. If your change needs
to touch cmark sources, follow the existing cherry-pick pattern:

1. Find the cmark upstream commit that fixes the same issue.
2. Adapt it to cmark-gfm's AST shape by hand (don't bulk-rebase —
   see `vendor/upstream-rebase-notes.md` for why).
3. Add an entry to the "Local modifications" table in
   `vendor/VENDOR.md` with the upstream commit reference and a
   one-line description of what it fixes.
4. Add a comment at the change site in the vendor file naming the
   upstream commit, so the next refresh can find it.

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
4. `git tag -a X.Y.Z -m "mdparser X.Y.Z"` with a release-note
   body, then `git push origin X.Y.Z`. Use bare semver
   (`0.1.1`, not `v0.1.1`) to match the existing tag convention.
5. The `windows.yml` workflow picks up the tag, runs the full
   build matrix (PHP 8.3-8.5 x TS/NTS x x86/x64), and uses
   `php-windows-builder/release@v1` to create the GitHub release
   and attach the 12 DLL zips.
6. Packagist's GitHub webhook (configured on the repo) fires on
   the tag push and re-scans versions. `pie install
   iliaal/mdparser` resolves to the new tag within a minute or
   two. If Packagist hasn't indexed the tag yet, users can fall
   back to `pie install iliaal/mdparser:@dev` or hit the
   `api/update-package` endpoint with your Packagist API token
   to force a re-crawl. See
   `~/ai/wiki/tools/packagist-quirks.md` for the full list of
   Packagist indexing gotchas.
7. Before the first tag of any new release cycle, double-check
   that `composer.json` exists in the tree at HEAD (`git ls-tree
   HEAD | grep composer.json`). Packagist silently skips tags
   whose commit doesn't contain `composer.json` at the root —
   mdparser's 0.1.0 release hit this trap.

### License

By submitting a patch you agree to license your contribution under
the same license as the project (PHP License 3.01).
