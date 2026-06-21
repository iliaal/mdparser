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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"

#include <string.h>
#include <stdint.h>

#include "entity.h"
#include "mdparser_md4c_util.h"

static unsigned mdu_hex_val(char ch)
{
    if ('0' <= ch && ch <= '9') return ch - '0';
    if ('A' <= ch && ch <= 'F') return ch - 'A' + 10;
    if ('a' <= ch && ch <= 'f') return ch - 'a' + 10;
    return 0;
}

/* Append a codepoint as UTF-8, substituting U+FFFD for NUL / surrogates /
 * out-of-range values. */
void mdparser_md4c_append_cp(smart_str *out, unsigned cp)
{
    unsigned char u[4];
    if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        smart_str_appendl(out, "\xef\xbf\xbd", 3);
        return;
    }
    if (cp <= 0x7f)        { u[0] = (unsigned char)cp; smart_str_appendl(out, (char *)u, 1); }
    else if (cp <= 0x7ff)  { u[0] = 0xc0 | (cp >> 6); u[1] = 0x80 | (cp & 0x3f);
                             smart_str_appendl(out, (char *)u, 2); }
    else if (cp <= 0xffff) { u[0] = 0xe0 | (cp >> 12); u[1] = 0x80 | ((cp >> 6) & 0x3f);
                             u[2] = 0x80 | (cp & 0x3f); smart_str_appendl(out, (char *)u, 3); }
    else                   { u[0] = 0xf0 | (cp >> 18); u[1] = 0x80 | ((cp >> 12) & 0x3f);
                             u[2] = 0x80 | ((cp >> 6) & 0x3f); u[3] = 0x80 | (cp & 0x3f);
                             smart_str_appendl(out, (char *)u, 4); }
}

void mdparser_md4c_decode_attr(smart_str *out, const MD_ATTRIBUTE *attr)
{
    if (attr == NULL || attr->text == NULL) return;
    for (int i = 0; attr->substr_offsets[i] < attr->size; i++) {
        MD_TEXTTYPE type = attr->substr_types[i];
        MD_OFFSET off = attr->substr_offsets[i];
        MD_SIZE sz = attr->substr_offsets[i + 1] - off;
        const char *text = attr->text + off;
        if (type == MD_TEXT_NULLCHAR) {
            smart_str_appendl(out, "\xef\xbf\xbd", 3);
        } else if (type == MD_TEXT_ENTITY) {
            unsigned cps[2] = {0, 0};
            if (sz > 3 && text[1] == '#') {
                if (text[2] == 'x' || text[2] == 'X')
                    for (MD_SIZE k = 3; k < sz - 1; k++) cps[0] = 16 * cps[0] + mdu_hex_val(text[k]);
                else
                    for (MD_SIZE k = 2; k < sz - 1; k++) cps[0] = 10 * cps[0] + (text[k] - '0');
            } else {
                const ENTITY *e = entity_lookup(text, sz);
                if (e == NULL) { smart_str_appendl(out, text, sz); continue; }
                cps[0] = e->codepoints[0];
                cps[1] = e->codepoints[1];
            }
            for (int k = 0; k < 2; k++) {
                if (k == 1 && cps[k] == 0) break;
                mdparser_md4c_append_cp(out, cps[k]);
            }
        } else {
            smart_str_appendl(out, text, sz);
        }
    }
}

bool mdparser_md4c_attr_plain(const MD_ATTRIBUTE *attr, const char **p, size_t *n)
{
    if (attr == NULL || attr->text == NULL || attr->size == 0) {
        *p = "";
        *n = 0;
        return true;
    }
    /* A single substring spans the whole value when substr_offsets[1]
     * already reaches attr->size. Entity / NUL substrings still need real
     * decoding; a plain substring decodes to itself. */
    if (attr->substr_offsets[1] >= attr->size &&
        attr->substr_types[0] != MD_TEXT_ENTITY &&
        attr->substr_types[0] != MD_TEXT_NULLCHAR) {
        *p = attr->text;
        *n = attr->size;
        return true;
    }
    *p = "";
    *n = 0;
    return false;
}

size_t mdparser_md4c_utf8_seqlen(const unsigned char *p, size_t avail)
{
    unsigned char c = p[0];
    if (c < 0x80) return 1;
    size_t expect;
    unsigned char min2 = 0x80, max2 = 0xBF;
    if (c >= 0xC2 && c <= 0xDF)      expect = 2;
    else if (c == 0xE0)              { expect = 3; min2 = 0xA0; }
    else if (c >= 0xE1 && c <= 0xEC) expect = 3;
    else if (c == 0xED)              { expect = 3; max2 = 0x9F; }
    else if (c >= 0xEE && c <= 0xEF) expect = 3;
    else if (c == 0xF0)              { expect = 4; min2 = 0x90; }
    else if (c >= 0xF1 && c <= 0xF3) expect = 4;
    else if (c == 0xF4)              { expect = 4; max2 = 0x8F; }
    else return 0;
    if (avail < expect) return 0;
    if (p[1] < min2 || p[1] > max2) return 0;
    for (size_t k = 2; k < expect; k++) {
        if (p[k] < 0x80 || p[k] > 0xBF) return 0;
    }
    return expect;
}

const char *mdparser_md4c_validate_utf8(const char *src, size_t len,
    size_t *out_len, bool *owned)
{
    const unsigned char *p = (const unsigned char *)src;
    size_t i = 0;
    bool clean = true;
    while (i < len) {
        /* ASCII fast path: skip runs of <0x80 bytes 8 at a time, then 1 at a
         * time, before falling back to the per-sequence validator. Markdown
         * is overwhelmingly ASCII, so this avoids a call per byte. */
        while (i + 8 <= len) {
            uint64_t w;
            memcpy(&w, p + i, 8);
            if (w & 0x8080808080808080ULL) break;
            i += 8;
        }
        while (i < len && p[i] < 0x80) i++;
        if (i >= len) break;
        size_t n = mdparser_md4c_utf8_seqlen(p + i, len - i);
        if (n == 0) { clean = false; break; }
        i += n;
    }
    if (clean) {
        *owned = false;
        *out_len = len;
        return src;
    }
    /* Size the rebuild buffer exactly: valid bytes pass through, each invalid
     * byte expands to a 3-byte U+FFFD. This counting pass mirrors the rebuild
     * loop below, so it avoids the len*3 worst case (768 MB at the 256 MB
     * input cap) when only a handful of bytes are invalid. */
    size_t out_size = 0;
    i = 0;
    while (i < len) {
        size_t beg = i;
        while (i + 8 <= len) {
            uint64_t w;
            memcpy(&w, p + i, 8);
            if (w & 0x8080808080808080ULL) break;
            i += 8;
        }
        while (i < len && p[i] < 0x80) i++;
        out_size += i - beg;
        if (i >= len) break;
        size_t n = mdparser_md4c_utf8_seqlen(p + i, len - i);
        if (n == 0) { out_size += 3; i++; }
        else { out_size += n; i += n; }
    }
    char *dst = emalloc(out_size + 1);
    size_t o = 0;
    i = 0;
    while (i < len) {
        /* Bulk-copy ASCII runs (same fast path as the clean scan). */
        size_t beg = i;
        while (i + 8 <= len) {
            uint64_t w;
            memcpy(&w, p + i, 8);
            if (w & 0x8080808080808080ULL) break;
            i += 8;
        }
        while (i < len && p[i] < 0x80) i++;
        if (i > beg) { memcpy(dst + o, p + beg, i - beg); o += i - beg; }
        if (i >= len) break;
        size_t n = mdparser_md4c_utf8_seqlen(p + i, len - i);
        if (n == 0) {
            dst[o++] = (char)0xEF; dst[o++] = (char)0xBF; dst[o++] = (char)0xBD;
            i++;
        } else {
            memcpy(dst + o, p + i, n);
            o += n;
            i += n;
        }
    }
    dst[o] = '\0';
    *owned = true;
    *out_len = o;
    return dst;
}
