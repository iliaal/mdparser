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

/* Single entity-dispatch primitive behind the decode family. Stores up to
 * two codepoints in `cps` and returns how many (1 or 2); returns 0 for
 * unknown entities, whose raw text the caller passes through for its own
 * output context. Raw sinks (AST/XML/attrs) feed the codepoints to
 * mdparser_md4c_append_cp via mdparser_md4c_decode_entity; the HTML sink
 * feeds them to its escaping appender instead (a decoded `&amp;` must
 * re-escape, never emit a bare `&`), and additionally returns the last
 * codepoint to seed SmartyPants quote context -- so the dispatch is shared
 * but each sink keeps its own policy function. */
int mdparser_md4c_decode_entity_cps(const char *text, MD_SIZE size, unsigned cps[2]);

/* validateUtf8 pre-pass shared by every md4c render path (HTML/XML/AST).
 * md4c never validates UTF-8; this restores the U+FFFD substitution for
 * invalid sequences. If `src` is already clean, returns `src` and sets
 * *owned=false. Otherwise returns an emalloc'd sanitized copy (invalid bytes
 * -> U+FFFD), sets *owned=true, and writes the new length to *out_len. The
 * caller must efree the result iff *owned is true. */
const char *mdparser_md4c_validate_utf8(const char *src, size_t len,
    size_t *out_len, bool *owned);

/* Entity-decoded attribute bytes. Plain attributes borrow md4c's storage;
 * attributes containing entities or NULs use `storage`. Always destroy the
 * view after consuming `text`, whether or not the fast path allocated. */
typedef struct {
    const char *text;
    size_t size;
    smart_str storage;
} mdparser_md4c_attr_view;

void mdparser_md4c_attr_view_init(mdparser_md4c_attr_view *view,
    const MD_ATTRIBUTE *attr);
void mdparser_md4c_attr_view_destroy(mdparser_md4c_attr_view *view);

/* Length of the leading run of bytes needing no escaping: the unrolled
 * 4-wide scan shared by the HTML and XML escapers. `map` marks attention
 * bytes (nonzero & `mask`); the caller handles the first flagged byte with
 * its own output-context policy (HTML entities vs XML+C0 rules). */
static inline size_t mdparser_md4c_scan_plain(const unsigned char *map,
    unsigned char mask, const char *s, size_t n)
{
    size_t off = 0;
    while (off + 3 < n && !(map[(unsigned char)s[off]] & mask)
        && !(map[(unsigned char)s[off + 1]] & mask)
        && !(map[(unsigned char)s[off + 2]] & mask)
        && !(map[(unsigned char)s[off + 3]] & mask)) {
        off += 4;
    }
    while (off < n && !(map[(unsigned char)s[off]] & mask)) {
        off++;
    }
    return off;
}

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
