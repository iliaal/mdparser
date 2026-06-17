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

/* Expected length (1..4) of the UTF-8 sequence starting at p, 0 if invalid.
 * Validates continuation bytes, overlong forms, surrogates, and the
 * U+10FFFF ceiling (RFC 3629). */
size_t mdparser_md4c_utf8_seqlen(const unsigned char *p, size_t avail);

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

#endif
