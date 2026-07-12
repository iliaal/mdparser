/*
  +----------------------------------------------------------------------+
  | Copyright (c) 2025-2026, Ilia Alshanetsky                            |
  | Copyright (c) 2025-2026, Advanced Internet Designs Inc.              |
  +----------------------------------------------------------------------+
  | This source file is subject to the BSD 3-Clause license that is      |
  | bundled with this package in the file LICENSE.                       |
  +----------------------------------------------------------------------+
  | Author: Ilia Alshanetsky <ilia@ilia.ws>                              |
  +----------------------------------------------------------------------+
*/

#ifndef MDPARSER_MD4C_UTIL_H
#define MDPARSER_MD4C_UTIL_H

#include "php.h"
#include "zend_smart_str.h"
#include "md4c.h"

/* Speculative output capacity helps ordinary documents but must not scale to
 * the full size of sparse input whose rendered form is tiny. */
#define MDPARSER_INITIAL_OUTPUT_RESERVE_MAX ((size_t)(1024UL * 1024UL))

/* Expected length (1..4) of the UTF-8 sequence starting at p, 0 if invalid.
 * Validates continuation bytes, overlong forms, surrogates, and the
 * U+10FFFF ceiling (RFC 3629). */
size_t mdparser_md4c_utf8_seqlen(const unsigned char *p, size_t avail);

/* Append codepoint `cp` to `out` as UTF-8, substituting U+FFFD for NUL,
 * surrogates, and out-of-range values. Shared by the raw entity decoder. */
void mdparser_md4c_append_cp(smart_str *out, unsigned cp);

/* Decode one MD_TEXT_ENTITY token (e.g. "&amp;", "&#65;", "&#x41;") into
 * raw UTF-8 bytes appended to `out`. Unknown entities pass through as their
 * original text. Callers still escape or filter the decoded bytes for their
 * own output context. */
void mdparser_md4c_decode_entity(smart_str *out, const char *text, MD_SIZE size);

/* validateUtf8 pre-pass shared by every md4c render path (HTML/XML/AST).
 * md4c never validates UTF-8; this restores the U+FFFD substitution for
 * invalid sequences. If `src` is already clean, returns `src` and sets
 * *owned=false. Otherwise returns an emalloc'd sanitized copy (invalid bytes
 * -> U+FFFD), sets *owned=true, and writes the new length to *out_len. The
 * caller must efree the result iff *owned is true. */
const char *mdparser_md4c_validate_utf8(const char *src, size_t len,
    size_t *out_len, bool *owned);

/* Decode an MD_ATTRIBUTE (link/image destination, title, code-fence info)
 * into raw UTF-8 bytes appended to `out`. md4c hands attributes out as a
 * run of typed substrings; this resolves MD_TEXT_ENTITY substrings to their
 * codepoints and MD_TEXT_NULLCHAR to U+FFFD, copying NORMAL substrings
 * verbatim. The result is undecoded-of-markup but entity-resolved, matching
 * what the HTML renderer feeds its escapers; callers still escape for their
 * own context (XML-escape, HTML-escape, or store raw in the AST). */
void mdparser_md4c_decode_attr(smart_str *out, const MD_ATTRIBUTE *attr);

/* Fast-path probe for mdparser_md4c_decode_attr. Most attributes (link/image
 * URLs especially) are a single plain substring (no entity, no NUL) spanning
 * the whole value, so the decoded bytes equal attr->text and the
 * scratch-buffer copy can be skipped. Returns true and points *p and *n at
 * the verbatim bytes on the fast path (including the empty-attribute case);
 * returns false when the caller must decode. */
bool mdparser_md4c_attr_plain(const MD_ATTRIBUTE *attr, const char **p, size_t *n);

/* Skip a single leading UTF-8 BOM (EF BB BF) on the (src,len) pair in place.
 * md4c does not strip a BOM: left in, it leaks into output verbatim and also
 * displaces the first physical line's start, breaking line-leading recognition
 * (ATX headings, list markers, blockquotes, ...). Mirrors md4c-html's
 * MD_HTML_FLAG_SKIP_UTF8_BOM. A BOM is never semantically meaningful in
 * Markdown, so this is unconditional. */
static inline void mdparser_md4c_skip_bom(const char **src, size_t *len)
{
    const unsigned char *p = (const unsigned char *)*src;
    if (*len >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        *src += 3;
        *len -= 3;
    }
}

#endif
