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
    size_t size;

    if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        smart_str_appendl(out, "\xef\xbf\xbd", 3);
        return;
    }

    if (cp <= 0x7f) {
        size = 1;
        u[0] = (unsigned char)cp;
    } else if (cp <= 0x7ff) {
        size = 2;
        u[0] = 0xc0 | (cp >> 6);
        u[1] = 0x80 | (cp & 0x3f);
    } else if (cp <= 0xffff) {
        size = 3;
        u[0] = 0xe0 | (cp >> 12);
        u[1] = 0x80 | ((cp >> 6) & 0x3f);
        u[2] = 0x80 | (cp & 0x3f);
    } else {
        size = 4;
        u[0] = 0xf0 | (cp >> 18);
        u[1] = 0x80 | ((cp >> 12) & 0x3f);
        u[2] = 0x80 | ((cp >> 6) & 0x3f);
        u[3] = 0x80 | (cp & 0x3f);
    }
    smart_str_appendl(out, (char *)u, size);
}

void mdparser_md4c_decode_entity(smart_str *out, const char *text, MD_SIZE size)
{
    unsigned cps[2] = {0, 0};
    if (size > 3 && text[1] == '#') {
        if (text[2] == 'x' || text[2] == 'X')
            for (MD_SIZE k = 3; k < size - 1; k++) cps[0] = 16 * cps[0] + mdu_hex_val(text[k]);
        else
            for (MD_SIZE k = 2; k < size - 1; k++) cps[0] = 10 * cps[0] + (text[k] - '0');
    } else {
        const ENTITY *e = entity_lookup(text, size);
        if (e == NULL) {
            smart_str_appendl(out, text, size);
            return;
        }
        cps[0] = e->codepoints[0];
        cps[1] = e->codepoints[1];
    }
    for (int k = 0; k < 2; k++) {
        if (k == 1 && cps[k] == 0) break;
        mdparser_md4c_append_cp(out, cps[k]);
    }
}

static void mdu_decode_attr(smart_str *out, const MD_ATTRIBUTE *attr)
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
            mdparser_md4c_decode_entity(out, text, sz);
        } else {
            smart_str_appendl(out, text, sz);
        }
    }
}

void mdparser_md4c_attr_view_init(mdparser_md4c_attr_view *view,
    const MD_ATTRIBUTE *attr)
{
    memset(view, 0, sizeof(*view));

    if (attr == NULL || attr->text == NULL || attr->size == 0) {
        view->text = "";
        return;
    }
    /* A single substring spans the whole value when substr_offsets[1]
     * already reaches attr->size. Entity / NUL substrings still need real
     * decoding; a plain substring decodes to itself. */
    if (attr->substr_offsets[1] >= attr->size &&
        attr->substr_types[0] != MD_TEXT_ENTITY &&
        attr->substr_types[0] != MD_TEXT_NULLCHAR) {
        view->text = attr->text;
        view->size = attr->size;
        return;
    }

    mdu_decode_attr(&view->storage, attr);
    view->text = view->storage.s ? ZSTR_VAL(view->storage.s) : "";
    view->size = view->storage.s ? ZSTR_LEN(view->storage.s) : 0;
}

void mdparser_md4c_attr_view_destroy(mdparser_md4c_attr_view *view)
{
    smart_str_free(&view->storage);
}

/* RFC 3629 lead-byte classification shared by the sequence validator and
 * the maximal-subpart scanner: expected length plus the allowed range for
 * the second byte (overlong / surrogate / >U+10FFFF guards). Returns false
 * when c cannot start a multi-byte sequence. */
typedef struct {
    size_t expect;
    unsigned char min2, max2;
} mdu_seq_info;

static bool mdu_lead_info(unsigned char c, mdu_seq_info *si)
{
    si->min2 = 0x80;
    si->max2 = 0xBF;
    if (c >= 0xC2 && c <= 0xDF)      { si->expect = 2; return true; }
    if (c == 0xE0)                   { si->expect = 3; si->min2 = 0xA0; return true; }
    if (c >= 0xE1 && c <= 0xEC)      { si->expect = 3; return true; }
    if (c == 0xED)                   { si->expect = 3; si->max2 = 0x9F; return true; }
    if (c >= 0xEE && c <= 0xEF)      { si->expect = 3; return true; }
    if (c == 0xF0)                   { si->expect = 4; si->min2 = 0x90; return true; }
    if (c >= 0xF1 && c <= 0xF3)      { si->expect = 4; return true; }
    if (c == 0xF4)                   { si->expect = 4; si->max2 = 0x8F; return true; }
    return false;
}

size_t mdparser_md4c_utf8_seqlen(const unsigned char *p, size_t avail)
{
    unsigned char c = p[0];
    if (c < 0x80) return 1;
    mdu_seq_info si;
    if (!mdu_lead_info(c, &si)) return 0;
    if (avail < si.expect) return 0;
    if (p[1] < si.min2 || p[1] > si.max2) return 0;
    for (size_t k = 2; k < si.expect; k++) {
        if (p[k] < 0x80 || p[k] > 0xBF) return 0;
    }
    return si.expect;
}

/* Length of the Unicode "maximal subpart" of the invalid sequence starting
 * at p: the longest prefix that is still a prefix of some valid sequence.
 * One U+FFFD replaces the whole subpart (W3C/WHATWG replacement policy),
 * so a truncated E2 82 yields one U+FFFD while an out-of-range second byte
 * (e.g. E0 9F) splits into one U+FFFD per offending byte. Callers invoke
 * this only after mdparser_md4c_utf8_seqlen returned 0. */
static size_t mdu_invalid_subpart_len(const unsigned char *p, size_t avail)
{
    unsigned char c = p[0];
    mdu_seq_info si;
    if (c < 0x80 || !mdu_lead_info(c, &si)) return 1;
    if (avail < 2 || p[1] < si.min2 || p[1] > si.max2) return 1;
    size_t n = 2;
    while (n < si.expect && n < avail && p[n] >= 0x80 && p[n] <= 0xBF) n++;
    return n;
}

/* Skip a run of ASCII (<0x80) bytes starting at p[i], using a 64-bit
 * SWAR scan for the common case. Returns the new index. */
static size_t mdu_skip_ascii(const unsigned char *p, size_t i, size_t len)
{
    while (i + 8 <= len) {
        uint64_t w;
        memcpy(&w, p + i, 8);
        if (w & 0x8080808080808080ULL) break;
        i += 8;
    }
    while (i < len && p[i] < 0x80) i++;
    return i;
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
        i = mdu_skip_ascii(p, i, len);
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
        i = mdu_skip_ascii(p, i, len);
        out_size += i - beg;
        if (i >= len) break;
        size_t n = mdparser_md4c_utf8_seqlen(p + i, len - i);
        if (n == 0) { i += mdu_invalid_subpart_len(p + i, len - i); out_size += 3; }
        else { out_size += n; i += n; }
    }
    char *dst = emalloc(out_size + 1);
    size_t o = 0;
    i = 0;
    while (i < len) {
        /* Bulk-copy ASCII runs (same fast path as the clean scan). */
        size_t beg = i;
        i = mdu_skip_ascii(p, i, len);
        if (i > beg) { memcpy(dst + o, p + beg, i - beg); o += i - beg; }
        if (i >= len) break;
        size_t n = mdparser_md4c_utf8_seqlen(p + i, len - i);
        if (n == 0) {
            dst[o++] = (char)0xEF; dst[o++] = (char)0xBF; dst[o++] = (char)0xBD;
            i += mdu_invalid_subpart_len(p + i, len - i);
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
