# Security

mdparser is designed to be safe by default when rendering **untrusted
input**. The default options strip dangerous URL schemes, escape raw
HTML, and apply GitHub's tag filter. You can opt out of each layer
individually, but the defaults assume you're rendering user content.

## Threat model

The primary threat is **XSS via user-authored markdown**: an attacker
submits markdown that, when rendered, produces HTML executing
JavaScript in other users' browsers. The typical attack vectors are:

1. `[text](javascript:alert(1))` — dangerous URL scheme in a link
2. `![alt](data:text/html,<script>...</script>)` — dangerous URL in an
   image
3. Raw `<script>`, `<iframe>`, `<style>` tags embedded in the markdown
4. HTML attribute injection via unquoted or partially-quoted URLs

mdparser blocks all four in default mode. The `tests/020_security.phpt`
suite is the regression gate for these vectors.

## Default behavior

`new Parser()` (or explicitly `new Parser(new Options(unsafe: false,
tagfilter: true))`) does the following:

### URL scheme filter

Link `href` and image `src` are rewritten to empty string (`""`) when
the scheme matches any of:

- `javascript:` (any case, with or without leading whitespace)
- `vbscript:`
- `file:`
- `data:` when the MIME type is NOT one of the allowlisted image types
  (see below)

```php
$p = new Parser();
echo $p->toHtml('[xss](javascript:alert(1))');
// <p><a href="">xss</a></p>

echo $p->toHtml('[xss](JaVaScRiPt:alert(1))');  // case-insensitive
// <p><a href="">xss</a></p>

echo $p->toHtml('[xss](data:text/html,<b>boom</b>)');
// <p><a href="">xss</a></p>
```

### Allowed URL schemes

These schemes pass through untouched:

- `http:`, `https:`
- `mailto:`, `tel:`
- `ftp:`, `ftps:`
- `data:image/png`, `data:image/jpeg`, `data:image/gif`, `data:image/webp`
  (`image/svg+xml` is NOT allowed because SVG can execute JavaScript)
- Relative URLs (no scheme)

```php
echo $p->toHtml('[docs](https://example.com/docs)');
// <p><a href="https://example.com/docs">docs</a></p>

echo $p->toHtml('![logo](data:image/png;base64,iVBORw0KGgo=)');
// <p><img src="data:image/png;base64,iVBORw0KGgo=" alt="logo" /></p>
```

### Raw HTML replacement

Any raw HTML block or inline tag gets replaced with
`<!-- raw HTML omitted -->`. The text content inside the tag is
preserved (because stripping just the tags could still leak useful
information), but script tags don't execute.

```php
echo $p->toHtml('<script>alert(1)</script>');
// <!-- raw HTML omitted -->

echo $p->toHtml('before <script>alert(1)</script> after');
// <p>before <!-- raw HTML omitted -->alert(1)<!-- raw HTML omitted --> after</p>
```

This is more aggressive than just escaping `<` — the whole tag is
removed so even a clever scheme that targets a specific parser quirk
can't get through.

## Tag filter (GFM, default-on)

The `tagfilter` option adds a second layer that escapes specific
dangerous tags even when raw HTML is otherwise allowed. It covers:

- `<title>`
- `<textarea>`
- `<style>`
- `<xmp>`
- `<iframe>`
- `<noembed>`
- `<noframes>`
- `<script>`
- `<plaintext>`

The filter fires even when `unsafe: true`, so you get defense in depth
when you've explicitly allowed raw HTML:

```php
$p = new Parser(new Options(unsafe: true));
echo $p->toHtml('<b>bold</b>');
// <p><b>bold</b></p>                      (allowed through unsafe mode)

echo $p->toHtml('<script>alert(1)</script>');
// &lt;script>alert(1)&lt;/script>         (still escaped by tagfilter)
```

To disable the tag filter as well, pass `tagfilter: false` explicitly.
This is the maximum-permissive configuration and should only be used
for input you fully control.

## Unsafe mode

`Options(unsafe: true)` disables URL and raw-HTML sanitization. Use it
only for content you trust — your own release notes, internal wiki
content, markdown from a vetted author, etc. Never pass user input
through a parser with `unsafe: true` unless you have a separate sanitizer
downstream.

```php
$parser = new Parser(new Options(unsafe: true));

echo $parser->toHtml('[xss](javascript:alert(1))');
// <p><a href="javascript:alert(1)">xss</a></p>   ← unsafe, XSS vector
```

## Recommendations

| Input source                     | Recommended options                                          |
|----------------------------------|--------------------------------------------------------------|
| User comments, reviews, bios     | defaults (`unsafe: false`, `tagfilter: true`)                |
| User-authored wiki pages         | defaults                                                     |
| Your own release notes / CHANGELOGs | `unsafe: true` for raw HTML if you need it                |
| Internal project docs            | `unsafe: true, tagfilter: false` if fully trusted            |
| Imported content from third-party APIs | defaults, treat as untrusted                            |

## Not in scope

mdparser does NOT:

- Sanitize allowed HTML attributes (if you pass `unsafe: true` and
  embed `<img onerror="...">`, that passes through). Use a separate
  HTML sanitizer like HTML Purifier for attribute-level filtering.
- Enforce a Content Security Policy. Set your own CSP headers at the
  HTTP layer.
- Validate that links point at same-origin resources. If you need to
  block external links or add `rel="nofollow"`, post-process the
  output with `DOMDocument` or a regex.
- Detect malicious Unicode homographs, invisible characters, or
  right-to-left override tricks. Markdown is rendered as-is; if you're
  worried about spoofed links you need a separate normalization pass.

## Reporting security issues

If you find an XSS or similar security bug, please open a private
security advisory on GitHub at `iliaal/mdparser` rather than a public
issue. Include:

- Minimal reproduction (markdown input + parser options)
- Expected behavior vs actual output
- Affected mdparser versions

Public issues are fine for everything else.
