# mdparser documentation

Native C CommonMark + GitHub Flavored Markdown parser for PHP, available
via PECL and PIE. 100% CommonMark 0.31 spec compliance, GFM extensions
(tables, strikethrough, task lists, autolinks, tag filter), three output
formats (HTML, XML, AST), zero external runtime dependencies.

## Reference

- **[installation.md](installation.md)** — build from source, install
  from PECL, install via PIE, platform notes
- **[parser.md](parser.md)** — `MdParser\Parser` class: `toHtml`,
  `toXml`, `toAst`, constructor, error model
- **[options.md](options.md)** — `MdParser\Options` class: all 17 bool
  fields documented, per-option examples of the output change
- **[ast.md](ast.md)** — `toAst()` output format: node types, fields
  per type, sourcepos behavior, walking the tree
- **[security.md](security.md)** — safe mode guarantees, XSS
  considerations, when `unsafe: true` is appropriate, tag filter
- **[spec-coverage.md](spec-coverage.md)** — CommonMark 0.31 conformance
  notes, GFM extension notes, cherry-picks applied over cmark-gfm

## Examples

Runnable PHP scripts in [../examples/](../examples/) showing concrete
use cases. All are self-contained and can be run directly:

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
    sourcepos: true,
    footnotes: true,
));
echo $parser->toHtml($markdown);
```

## Versioning and stability

mdparser follows semver from 1.0.0 onward. During 0.x, minor version
bumps may introduce breaking changes; those are always called out in
`CHANGELOG.md`.

The CommonMark spec itself is frozen at 0.31 and mdparser's spec
compliance is pinned by a full 652-example test suite in
`tests/005_commonmark_spec.phpt`. Any future change that breaks spec
conformance will fail that test. GFM extensions are pinned the same way
by `tests/002_option_effects.phpt` and the parity corpus under
`tests/parity/`.

## License

Wrapper code is under the PHP License 3.01. The vendored cmark-gfm
sources are under BSD-2-Clause and MIT. See `LICENSE` at the repo root.
