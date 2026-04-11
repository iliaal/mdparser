# mdparser examples

Runnable PHP scripts showing mdparser in action. Each file is
self-contained and can be executed directly:

```bash
php -d extension=mdparser.so examples/01-basic.php
```

Or if you've built mdparser locally but not installed it:

```bash
php -d extension=./modules/mdparser.so examples/01-basic.php
```

## Index

| File | Shows |
|---|---|
| [`01-basic.php`](01-basic.php) | Simplest usage — `new Parser()` + `toHtml` |
| [`02-options.php`](02-options.php) | Customizing the parser via `Options` (smart punctuation, hard breaks, sourcepos) |
| [`03-ast-toc.php`](03-ast-toc.php) | Walk `toAst()` output to extract a table of contents from headings |
| [`04-gfm-features.php`](04-gfm-features.php) | GFM tables, strikethrough, task lists, autolinks |
| [`05-footnotes.php`](05-footnotes.php) | Enabling footnotes, rendering references and definitions |
| [`06-safe-mode.php`](06-safe-mode.php) | Default safe mode vs `unsafe: true` — XSS vectors handled |

## Verifying your install

If `examples/01-basic.php` prints `<h1>Hello, mdparser!</h1>`, your
extension is installed correctly. If you see `Class MdParser\Parser
not found`, the extension isn't loaded — check your `php.ini` or pass
`-d extension=...` on the command line.
