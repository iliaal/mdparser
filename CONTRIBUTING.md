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

### License

By submitting a patch you agree to license your contribution under
the same license as the project (PHP License 3.01).
