# mdparser documentation

Native C CommonMark + GitHub Flavored Markdown parser for PHP,
installable via PIE (the PHP Foundation's PECL successor). It's built on
[md4c](https://github.com/mity/md4c), a single-pass parser that targets
CommonMark 0.31 natively, and supports the GFM extensions (tables,
strikethrough, task lists, autolinks, tag filter) and three output
formats (HTML, XML, AST). No external runtime dependencies.

## Reference

- [installation.md](installation.md): install via PIE, build from
  source, platform notes, Windows binaries
- [parser.md](parser.md): the `MdParser\Parser` class (`toHtml`,
  `toXml`, `toAst`, constructor, error model)
- [options.md](options.md): all 32 bool fields of `MdParser\Options`
  (core parser toggles, GFM extension toggles, two HTML output flags,
  parser-behavior toggles, md4c dialect extensions), with examples of
  each output change
- [ast.md](ast.md): the `toAst()` output format (node types, fields per
  type, sourcepos behavior, walking the tree)
- [security.md](security.md): safe mode guarantees, XSS considerations,
  when `unsafe: true` is appropriate, tag filter
- [spec-coverage.md](spec-coverage.md): CommonMark 0.31 conformance
  baseline, GFM extension notes, md4c dialect extensions

## Examples

[../examples/](../examples/) holds self-contained PHP scripts you can
run directly:

```bash
php -d extension=mdparser.so examples/01-basic.php
```

See [`examples/README.md`](../examples/README.md) for the full list.

## Quick start

```php
use MdParser\Parser;
use MdParser\Options;

// Default: safe mode on, GFM extensions on, CommonMark-compliant output.
$parser = new Parser();
echo $parser->toHtml('# Hello');    // <h1>Hello</h1>

// With custom options (named arguments).
$parser = new Parser(new Options(
    smart: true,
    footnotes: true,
));
echo $parser->toHtml($markdown);
```

## Versioning and stability

mdparser follows semver from 1.0.0 onward. During 0.x, minor version
bumps may introduce breaking changes; `CHANGELOG.md` calls them out.

mdparser targets CommonMark 0.31. `tests/005_commonmark_spec.phpt` runs
every example in `spec.txt` against md4c's output and fails if the
baseline moves. GFM extensions are pinned the
same way by `tests/002_option_effects.phpt` and the parity corpus under
`tests/parity/`. See `spec-coverage.md` for the current baseline.

## License

Wrapper code is under the BSD 3-Clause License. The vendored md4c parser
is under the MIT License. See `LICENSE` at the repo root and
`vendor/md4c/LICENSE.md`.
