# Security policy

mdparser is designed to render untrusted Markdown input safely by
default. See `docs/security.md` for the full threat model, allowed
URL schemes, and tag-filter behavior.

## Supported versions

| Version | Supported          |
|---------|--------------------|
| 0.1.x   | :white_check_mark: |

Once 1.0 ships, the two most recent minor versions will receive
security fixes.

## Reporting a vulnerability

**Do not file a public GitHub issue for security vulnerabilities.**

Please use GitHub's private security advisory feature at
https://github.com/iliaal/mdparser/security/advisories/new and include:

- A minimal reproduction: the markdown input, the `MdParser\Options`
  passed to the `Parser`, and the observed output.
- The affected mdparser version (`phpversion('mdparser')`).
- The PHP version and OS.
- Your assessment of the impact (e.g. XSS bypass, DoS, information
  disclosure).

You can expect an initial acknowledgment within 72 hours. I'll work
with you on a fix and coordinate disclosure timing.

## Scope

In scope:

- XSS via markdown input rendered with default options (`unsafe: false`)
- URL scheme filter bypasses (a `javascript:` URL or similar landing
  in rendered output)
- Tag filter bypasses (`<script>` or similar landing in rendered output
  when `unsafe: true, tagfilter: true`)
- Crashes or memory corruption in the parser C code
- Buffer overflows or read-after-free in `vendor/cmark/`

Out of scope:

- Behavior when `unsafe: true` and `tagfilter: false` — this
  configuration explicitly disables sanitization and is only for
  trusted input.
- Attacks requiring write access to the PHP source or extension binary.
- Third-party applications that use mdparser incorrectly (e.g. pass
  mdparser's output through another unsafe template engine).
