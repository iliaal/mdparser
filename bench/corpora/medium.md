# Project README

A typical README-sized document with the features you'd find in a
real-world GitHub project: intro prose, install instructions, a code
block, a GFM table, a bullet list, some inline formatting, and a
couple of links.

## Installation

```bash
git clone https://github.com/example/project.git
cd project
make install
```

Or via the package manager:

```bash
pkg install project
```

## Features

- Fast: native C implementation with no runtime dependencies
- Safe: strips dangerous URL schemes by default
- Standard: 100% CommonMark 0.31 spec conformance
- Extensible: GFM tables, strikethrough, task lists, autolinks
- Modern: PHP 8.2+ only, `readonly` classes, named arguments

## Supported formats

| Format     | Input | Output | Notes                          |
|:-----------|:-----:|:------:|:-------------------------------|
| HTML       |   —   |   ✓    | Primary rendering target       |
| XML        |   —   |   ✓    | CommonMark DTD-wrapped tree    |
| AST        |   —   |   ✓    | Nested PHP arrays              |
| Plain text |   —   |   —    | Not yet supported              |

## Usage

```php
$parser = new Parser();
echo $parser->toHtml('# Hello');
```

See the [full documentation](https://example.com/docs) for advanced
usage patterns.

## Comparison

Relative to pure-PHP parsers:

1. **10-50x faster** on realistic corpora
2. Correct CommonMark spec handling out of the box
3. Same GFM extension set as GitHub's own rendering

> Note: absolute numbers depend heavily on input size and content
> shape. See `bench/README.md` for methodology.

## Contributing

Issues and pull requests welcome. Run `make test` before submitting.

- [x] Write tests
- [x] Document the API
- [ ] Publish to PECL
- [ ] Verify PIE install

## License

PHP License 3.01. See `LICENSE` for the full text including vendored
component notices.
