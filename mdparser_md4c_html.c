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
#include "zend_smart_str.h"

#include <string.h>

#include "md4c.h"
#include "entity.h"
#include "mdparser_md4c_html.h"
#include "mdparser_md4c_util.h"

/* Status codes returned via the *status out-param. */
#define MDM_OK            0
#define MDM_ERR_PARSE     1   /* md_parse returned non-zero */

const char *mdparser_md4c_status_message(int status)
{
    switch (status) {
        case MDM_ERR_PARSE:
            return "mdparser: md4c parser failed";
        default:
            return "mdparser: unknown error";
    }
}

/* ===================================================================
 * Render context
 * =================================================================== */

/* Dedupe state for heading anchors: a set of taken slugs plus a
 * base -> next-suffix cache so repeated collisions stay near O(1). */
typedef struct {
    HashTable taken;        /* slug string -> (dummy) */
    HashTable next_suffix;  /* base slug -> zend_long next suffix */
    bool active;
} mdm_slugs;

typedef struct {
    smart_str *cur;         /* active output target */
    smart_str main;         /* document buffer */
    int render_opts;        /* MDPARSER_RF_* */
    int image_nesting_level;

    /* heading-anchor streaming state */
    bool in_heading;
    int heading_level;
    smart_str heading_html; /* buffered inner HTML of current heading */
    smart_str heading_text; /* plain text of current heading, for the slug */
    mdm_slugs slugs;

    /* SmartyPants quote context: last normal-text byte emitted (0 at
     * start / after a structural boundary we treat as left-context). */
    unsigned char prev_char;
} mdm_ctx;

#define NEED_HTML_ESC_FLAG 0x1
#define NEED_URL_ESC_FLAG  0x2

#define ISDIGIT(ch) ('0' <= (ch) && (ch) <= '9')
#define ISLOWER(ch) ('a' <= (ch) && (ch) <= 'z')
#define ISUPPER(ch) ('A' <= (ch) && (ch) <= 'Z')
#define ISALNUM(ch) (ISLOWER(ch) || ISUPPER(ch) || ISDIGIT(ch))

static zend_always_inline void out_append(mdm_ctx *r, const char *s, size_t n)
{
    smart_str_appendl(r->cur, s, n);
}
#define OUT_LIT(r, lit) out_append((r), "" lit, sizeof(lit) - 1)

/* Escape-classification map. It depends only on fixed character sets, so it
 * is built once at MINIT instead of rebuilt (256 iterations + strchr scans)
 * on every render; each render memcpy's it into the per-call context. */
static char mdm_escape_map_template[256];

void mdparser_md4c_html_minit(void)
{
    for (int i = 0; i < 256; i++) {
        unsigned char ch = (unsigned char)i;
        mdm_escape_map_template[i] = 0;
        if (strchr("\"&<>", ch) != NULL)
            mdm_escape_map_template[i] |= NEED_HTML_ESC_FLAG;
        if (!ISALNUM(ch) && strchr("~-_.+!*(),%#@?=;:/$", ch) == NULL)
            mdm_escape_map_template[i] |= NEED_URL_ESC_FLAG;
    }
}

/* Conditional newline: append '\n' only if the current buffer is non-empty
 * and does not already end in one. This is the CommonMark reference
 * renderer's `cr()` -- emitting it before every block-element open
 * reproduces its exact inter-block whitespace (e.g. `<li>\n<pre>` when a
 * block follows a bare `<li>`). */
static void mdm_cr(mdm_ctx *r)
{
    zend_string *s = r->cur->s;
    if (s && ZSTR_LEN(s) > 0 && ZSTR_VAL(s)[ZSTR_LEN(s) - 1] != '\n')
        smart_str_appendc(r->cur, '\n');
}

/* Locale-independent ASCII case-insensitive compare (scheme/tag names). */
static int mdm_ascii_ncasecmp(const char *a, const char *b, size_t n);

/* ===================================================================
 * Escaping helpers (ported from md4c-html.c, output to smart_str)
 * =================================================================== */

static void mdm_escape_html(mdm_ctx *r, const char *data, size_t size)
{
    size_t beg = 0, off = 0;
    #define NEED_HTML_ESC(ch) (mdm_escape_map_template[(unsigned char)(ch)] & NEED_HTML_ESC_FLAG)
    while (1) {
        while (off + 3 < size && !NEED_HTML_ESC(data[off]) && !NEED_HTML_ESC(data[off+1])
               && !NEED_HTML_ESC(data[off+2]) && !NEED_HTML_ESC(data[off+3]))
            off += 4;
        while (off < size && !NEED_HTML_ESC(data[off])) off++;
        if (off > beg) out_append(r, data + beg, off - beg);
        if (off < size) {
            switch (data[off]) {
                case '"':  OUT_LIT(r, "&quot;"); break;
                case '&':  OUT_LIT(r, "&amp;");  break;
                case '<':  OUT_LIT(r, "&lt;");   break;
                case '>':  OUT_LIT(r, "&gt;");   break;
            }
            off++;
        } else break;
        beg = off;
    }
    #undef NEED_HTML_ESC
}

static void mdm_escape_url(mdm_ctx *r, const char *data, size_t size)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    size_t beg = 0, off = 0;
    #define NEED_URL_ESC(ch) (mdm_escape_map_template[(unsigned char)(ch)] & NEED_URL_ESC_FLAG)
    while (1) {
        while (off < size && !NEED_URL_ESC(data[off])) off++;
        if (off > beg) out_append(r, data + beg, off - beg);
        if (off < size) {
            char hex[3];
            switch (data[off]) {
                case '&': OUT_LIT(r, "&amp;"); break;
                default:
                    hex[0] = '%';
                    hex[1] = hex_chars[((unsigned)data[off] >> 4) & 0xf];
                    hex[2] = hex_chars[((unsigned)data[off] >> 0) & 0xf];
                    out_append(r, hex, 3);
                    break;
            }
            off++;
        } else break;
        beg = off;
    }
    #undef NEED_URL_ESC
}

static unsigned mdm_hex_val(char ch)
{
    if ('0' <= ch && ch <= '9') return ch - '0';
    if ('A' <= ch && ch <= 'Z') return ch - 'A' + 10;
    return ch - 'a' + 10;
}

static void mdm_append_codepoint(mdm_ctx *r, unsigned cp)
{
    static const char repl[] = { (char)0xef, (char)0xbf, (char)0xbd };
    unsigned char u[4];
    size_t n;
    if (cp <= 0x7f)        { n = 1; u[0] = cp; }
    else if (cp <= 0x7ff)  { n = 2; u[0] = 0xc0 | ((cp >> 6) & 0x1f); u[1] = 0x80 | (cp & 0x3f); }
    else if (cp <= 0xffff) { n = 3; u[0] = 0xe0 | ((cp >> 12) & 0xf); u[1] = 0x80 | ((cp >> 6) & 0x3f); u[2] = 0x80 | (cp & 0x3f); }
    else                   { n = 4; u[0] = 0xf0 | ((cp >> 18) & 0x7); u[1] = 0x80 | ((cp >> 12) & 0x3f); u[2] = 0x80 | ((cp >> 6) & 0x3f); u[3] = 0x80 | (cp & 0x3f); }
    if (0 < cp && cp <= 0x10ffff && (cp < 0xd800 || cp > 0xdfff))
        out_append(r, (char *)u, n);
    else
        out_append(r, repl, sizeof(repl));
}

/* Append a codepoint's UTF-8 with HTML-escaping applied. Used for entity
 * references: a decoded `&amp;`/`&lt;` must re-escape to `&amp;`/`&lt;`,
 * never emit a raw & < > (which would be an injection vector). */
static void mdm_append_codepoint_escaped(mdm_ctx *r, unsigned cp)
{
    static const char repl[] = { (char)0xef, (char)0xbf, (char)0xbd };
    unsigned char u[4];
    size_t n;
    if (cp <= 0x7f)        { n = 1; u[0] = cp; }
    else if (cp <= 0x7ff)  { n = 2; u[0] = 0xc0 | ((cp >> 6) & 0x1f); u[1] = 0x80 | (cp & 0x3f); }
    else if (cp <= 0xffff) { n = 3; u[0] = 0xe0 | ((cp >> 12) & 0xf); u[1] = 0x80 | ((cp >> 6) & 0x3f); u[2] = 0x80 | (cp & 0x3f); }
    else                   { n = 4; u[0] = 0xf0 | ((cp >> 18) & 0x7); u[1] = 0x80 | ((cp >> 12) & 0x3f); u[2] = 0x80 | ((cp >> 6) & 0x3f); u[3] = 0x80 | (cp & 0x3f); }
    if (0 < cp && cp <= 0x10ffff && (cp < 0xd800 || cp > 0xdfff))
        mdm_escape_html(r, (char *)u, n);
    else
        out_append(r, repl, sizeof(repl));
}

/* Render an entity reference (text like "&amp;" / "&#x41;") as its UTF-8
 * codepoint(s) with HTML-escaping, or escaped-verbatim if unknown. Always
 * HTML-safe output. */
static void mdm_render_entity(mdm_ctx *r, const char *text, size_t size)
{
    if (size > 3 && text[1] == '#') {
        unsigned cp = 0;
        if (text[2] == 'x' || text[2] == 'X') {
            for (size_t i = 3; i < size - 1; i++) cp = 16 * cp + mdm_hex_val(text[i]);
        } else {
            for (size_t i = 2; i < size - 1; i++) cp = 10 * cp + (text[i] - '0');
        }
        mdm_append_codepoint_escaped(r, cp);
        return;
    }
    const ENTITY *ent = entity_lookup(text, size);
    if (ent != NULL) {
        mdm_append_codepoint_escaped(r, ent->codepoints[0]);
        if (ent->codepoints[1]) mdm_append_codepoint_escaped(r, ent->codepoints[1]);
        return;
    }
    /* Unknown entity: escape the raw text (never emit a bare '&'). */
    mdm_escape_html(r, text, size);
}

/* ===================================================================
 * Safe-mode URL scheme filter
 *
 * md4c does no URL sanitization. In safe mode (default), neutralize
 * dangerous schemes the way the CommonMark reference renderer does: block javascript:, vbscript:,
 * file:, and data: (except data:image/{gif,png,jpeg,webp}, image src only).
 * An unsafe URL renders as an empty attribute value.
 *
 * SECURITY: the caller MUST pass the fully ENTITY-DECODED url bytes. md4c
 * hands attributes out entity-undecoded, so checking the raw bytes lets an
 * entity-encoded colon (`javascript&colon;`) hide the scheme from this scan
 * while the rendered (decoded) attribute is a live `javascript:` link.
 * Always decode -> check -> emit. `image_context` is true only for
 * <img src>; data: is rejected in <a href> (a navigable data: document).
 * =================================================================== */

static bool mdm_url_is_safe(const char *url, size_t len, bool image_context)
{
    size_t i = 0;
    /* Skip leading control/space bytes, mirroring browser tolerance. */
    while (i < len && (unsigned char)url[i] <= 0x20) i++;

    /* Find a scheme: letters up to ':'. No ':' before a path/query/frag
     * delimiter means a relative URL, which is safe. */
    size_t s = i;
    while (i < len && url[i] != ':' && url[i] != '/' && url[i] != '?' && url[i] != '#')
        i++;
    if (i >= len || url[i] != ':') return true;  /* no scheme -> relative */

    size_t scheme_len = i - s;
    #define SCHEME_IS(lit) (scheme_len == sizeof(lit) - 1 && \
        mdm_ascii_ncasecmp(url + s, lit, sizeof(lit) - 1) == 0)

    if (SCHEME_IS("javascript") || SCHEME_IS("vbscript") || SCHEME_IS("file"))
        return false;

    if (SCHEME_IS("data")) {
        /* data: is only ever safe as an inline image source, and only for
         * the four raster image types. Reject it in link context outright. */
        if (!image_context) return false;
        const char *rest = url + i + 1;
        size_t rest_len = len - (i + 1);
        static const char *ok[] = { "image/gif", "image/png", "image/jpeg", "image/webp" };
        for (size_t k = 0; k < sizeof(ok) / sizeof(ok[0]); k++) {
            size_t ol = strlen(ok[k]);
            /* Require an exact MIME match terminated by ';' or ',' (or the
             * whole URL): a bare prefix check would pass image/png+xml,
             * image/pnghtml, etc. */
            if (rest_len >= ol && mdm_ascii_ncasecmp(rest, ok[k], ol) == 0 &&
                (rest_len == ol || rest[ol] == ';' || rest[ol] == ','))
                return true;
        }
        return false;
    }
    #undef SCHEME_IS
    return true;
}

static int mdm_ascii_ncasecmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return (int)x - (int)y;
    }
    return 0;
}

/* ===================================================================
 * GFM tagfilter (unsafe mode only)
 *
 * In safe mode all raw HTML is escaped, so the blocklist is moot. In
 * unsafe mode, escape the leading '<' of any open/close tag whose name
 * is one of the GFM-dangerous set, leaving everything else verbatim.
 * =================================================================== */

static bool mdm_tagfilter_blocked(const char *p, size_t avail)
{
    static const char *tags[] = {
        "title", "textarea", "style", "xmp", "iframe",
        "noembed", "noframes", "script", "plaintext"
    };
    /* p points just past '<' (and past '/' for a close tag). */
    for (size_t k = 0; k < sizeof(tags) / sizeof(tags[0]); k++) {
        size_t tl = strlen(tags[k]);
        if (avail < tl) continue;
        if (mdm_ascii_ncasecmp(p, tags[k], tl) != 0) continue;
        /* Must be followed by a tag-name terminator. */
        char d = (avail > tl) ? p[tl] : '>';
        if (d == ' ' || d == '\t' || d == '\n' || d == '\r' || d == '\f'
            || d == '>' || d == '/')
            return true;
    }
    return false;
}

static void mdm_render_raw_html_unsafe(mdm_ctx *r, const char *text, size_t size)
{
    if (!(r->render_opts & MDPARSER_RF_TAGFILTER)) {
        out_append(r, text, size);
        return;
    }
    size_t beg = 0, i = 0;
    while (i < size) {
        if (text[i] == '<') {
            const char *name = text + i + 1;
            size_t avail = size - i - 1;
            if (avail > 0 && name[0] == '/') { name++; avail--; }
            if (mdm_tagfilter_blocked(name, avail)) {
                if (i > beg) out_append(r, text + beg, i - beg);
                OUT_LIT(r, "&lt;");
                i++;
                beg = i;
                continue;
            }
        }
        i++;
    }
    if (i > beg) out_append(r, text + beg, i - beg);
}

/* Emit raw HTML text per the current safety posture. */
static void mdm_render_raw_html(mdm_ctx *r, const char *text, size_t size)
{
    if (r->render_opts & MDPARSER_RF_UNSAFE)
        mdm_render_raw_html_unsafe(r, text, size);
    else
        mdm_escape_html(r, text, size);  /* safe: escape it */
}

/* ===================================================================
 * SmartyPants (smart option), applied to MD_TEXT_NORMAL runs
 * =================================================================== */

static bool mdm_is_left_quote_context(unsigned char before)
{
    return before == 0 || before == ' ' || before == '\t' || before == '\n'
        || before == '\r' || before == '\f' || before == '(' || before == '['
        || before == '{' || before == '<' || before == '"' || before == '\'';
}

/* Unicode White_Space members above U+007F (Zs category + line/paragraph
 * separators). Quote direction must treat these as left context, but
 * prev_char only carries one byte, so a trailing multibyte space would
 * otherwise present its 0x80-0xBF tail byte and read as right context. */
static bool mdm_is_unicode_space(unsigned cp)
{
    switch (cp) {
        case 0x00A0: case 0x1680:
        case 0x2000: case 0x2001: case 0x2002: case 0x2003:
        case 0x2004: case 0x2005: case 0x2006: case 0x2007:
        case 0x2008: case 0x2009: case 0x200A:
        case 0x2028: case 0x2029: case 0x202F: case 0x205F:
        case 0x3000:
            return true;
        default:
            return false;
    }
}

/* Quote-context byte for the trailing codepoint of a just-emitted run. For
 * ASCII the byte is the context directly. For a trailing multibyte sequence
 * the raw tail byte (0x80-0xBF) always reads as right context, which is right
 * for letters/symbols but wrong for Unicode spaces; decode the final
 * codepoint and map those to ' ' so a following quote still opens. */
static unsigned char mdm_trailing_quote_char(const char *p, size_t n)
{
    if (n == 0) return 0;
    unsigned char last = (unsigned char)p[n - 1];
    if (last < 0x80) return last;
    size_t i = n - 1;
    while (i > 0 && ((unsigned char)p[i] & 0xC0) == 0x80) i--;
    unsigned char lead = (unsigned char)p[i];
    size_t seqlen = n - i;
    unsigned cp = 0;
    if ((lead & 0xE0) == 0xC0 && seqlen >= 2)
        cp = ((lead & 0x1Fu) << 6) | ((unsigned char)p[i + 1] & 0x3Fu);
    else if ((lead & 0xF0) == 0xE0 && seqlen >= 3)
        cp = ((lead & 0x0Fu) << 12) | (((unsigned char)p[i + 1] & 0x3Fu) << 6)
            | ((unsigned char)p[i + 2] & 0x3Fu);
    else if ((lead & 0xF8) == 0xF0 && seqlen >= 4)
        cp = ((lead & 0x07u) << 18) | (((unsigned char)p[i + 1] & 0x3Fu) << 12)
            | (((unsigned char)p[i + 2] & 0x3Fu) << 6) | ((unsigned char)p[i + 3] & 0x3Fu);
    /* U+200B (ZWSP) is category Cf, not White_Space, so it is absent from
     * mdm_is_unicode_space; but toInlineHtml prefixes each physical line with a
     * ZWSP as a block-suppression sentinel, and a quote right after it should
     * still open, so treat it as left context here. */
    if (cp == 0x200B) return ' ';
    return mdm_is_unicode_space(cp) ? ' ' : last;
}

/* Render `data` as normal text with smart-punctuation transforms.
 * Anything not transformed is HTML-escaped. */
static void mdm_render_smart(mdm_ctx *r, const char *data, size_t size)
{
    size_t i = 0;
    while (i < size) {
        char c = data[i];
        if (c == '-' && i + 1 < size && data[i + 1] == '-') {
            size_t n = 0;
            while (i + n < size && data[i + n] == '-') n++;
            int en = 0, em = 0;
            if (n % 3 == 0)       { em = (int)(n / 3); }
            else if (n % 2 == 0)  { en = (int)(n / 2); }
            else if (n % 3 == 2)  { en = 1; em = (int)((n - 2) / 3); }
            else                  { en = 2; em = (int)((n - 4) / 3); }
            for (int k = 0; k < em; k++) mdm_append_codepoint(r, 0x2014); /* em dash */
            for (int k = 0; k < en; k++) mdm_append_codepoint(r, 0x2013); /* en dash */
            r->prev_char = '-';
            i += n;
            continue;
        }
        if (c == '.' && i + 2 < size && data[i + 1] == '.' && data[i + 2] == '.') {
            mdm_append_codepoint(r, 0x2026); /* ellipsis */
            r->prev_char = '.';
            i += 3;
            continue;
        }
        if (c == '"') {
            mdm_append_codepoint(r, mdm_is_left_quote_context(r->prev_char) ? 0x201C : 0x201D);
            r->prev_char = '"';
            i++;
            continue;
        }
        if (c == '\'') {
            mdm_append_codepoint(r, mdm_is_left_quote_context(r->prev_char) ? 0x2018 : 0x2019);
            r->prev_char = '\'';
            i++;
            continue;
        }
        /* Plain run up to the next transformable char; HTML-escape it. */
        size_t beg = i;
        while (i < size) {
            char d = data[i];
            if (d == '"' || d == '\'') break;
            if (d == '-' && i + 1 < size && data[i + 1] == '-') break;
            if (d == '.' && i + 2 < size && data[i + 1] == '.' && data[i + 2] == '.') break;
            i++;
        }
        if (i > beg) {
            mdm_escape_html(r, data + beg, i - beg);
            r->prev_char = mdm_trailing_quote_char(data + beg, i - beg);
        }
    }
}

/* ===================================================================
 * Heading-anchor slug (self-contained; matches mdparser_slugify)
 * =================================================================== */

static char *mdm_slugify(const char *text, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    char *out = emalloc(len * 3 + 1);
    size_t o = 0;
    bool prev_dash = true;
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x80) {
            if (c >= 'A' && c <= 'Z') { out[o++] = (char)(c + ('a' - 'A')); prev_dash = false; }
            else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') { out[o++] = (char)c; prev_dash = false; }
            else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '-') {
                if (!prev_dash) { out[o++] = '-'; prev_dash = true; }
            }
            i++;
            continue;
        }
        size_t n = mdparser_md4c_utf8_seqlen((const unsigned char *)text + i, len - i);
        if (n) {
            memcpy(out + o, text + i, n);
            o += n;
            prev_dash = false;
            i += n;
        } else {
            out[o++] = '%'; out[o++] = hex[(c >> 4) & 0xF]; out[o++] = hex[c & 0xF];
            prev_dash = false;
            i++;
        }
    }
    while (o > 0 && out[o - 1] == '-') o--;
    out[o] = '\0';
    return out;
}

static void mdm_slugs_init(mdm_slugs *s)
{
    zend_hash_init(&s->taken, 8, NULL, NULL, 0);
    zend_hash_init(&s->next_suffix, 8, NULL, NULL, 0);
    s->active = true;
}

static void mdm_slugs_destroy(mdm_slugs *s)
{
    if (!s->active) return;
    zend_hash_destroy(&s->taken);
    zend_hash_destroy(&s->next_suffix);
    s->active = false;
}

/* Return a unique slug for `base` (emalloc'd, caller frees), recording it
 * as taken. Empty base returns an empty string (id suppressed at emit). */
static char *mdm_slug_unique(mdm_slugs *s, char *base)
{
    if (base[0] == '\0') return base;
    size_t blen = strlen(base);
    if (!zend_hash_str_exists(&s->taken, base, blen)) {
        zval z; ZVAL_TRUE(&z);
        zend_hash_str_add(&s->taken, base, blen, &z);
        return base;
    }
    zval *nx = zend_hash_str_find(&s->next_suffix, base, blen);
    zend_long start = nx ? Z_LVAL_P(nx) : 1;
    char *cand = emalloc(blen + 24);
    for (zend_long n = start; n <= 100000; n++) {
        int w = snprintf(cand, blen + 24, "%s-" ZEND_LONG_FMT, base, n);
        if (w < 0 || (size_t)w >= blen + 24) continue;
        if (!zend_hash_str_exists(&s->taken, cand, (size_t)w)) {
            zval z; ZVAL_TRUE(&z);
            zend_hash_str_add(&s->taken, cand, (size_t)w, &z);
            zval suf; ZVAL_LONG(&suf, n + 1);
            zend_hash_str_update(&s->next_suffix, base, blen, &suf);
            efree(base);
            return cand;
        }
    }
    efree(cand);
    efree(base);
    char *empty = emalloc(1);
    empty[0] = '\0';
    return empty;
}

/* ===================================================================
 * md4c callbacks
 * =================================================================== */

static void mdm_render_ol_open(mdm_ctx *r, const MD_BLOCK_OL_DETAIL *d)
{
    if (d->start == 1) { OUT_LIT(r, "<ol>\n"); return; }
    char buf[64];
    int w = snprintf(buf, sizeof(buf), "<ol start=\"%u\">\n", d->start);
    out_append(r, buf, (size_t)w);
}

static void mdm_render_li_open(mdm_ctx *r, const MD_BLOCK_LI_DETAIL *d)
{
    if (d->is_task) {
        OUT_LIT(r, "<li class=\"task-list-item\"><input type=\"checkbox\" class=\"task-list-item-checkbox\" disabled");
        if (d->task_mark == 'x' || d->task_mark == 'X') OUT_LIT(r, " checked");
        OUT_LIT(r, " />");
    } else {
        OUT_LIT(r, "<li>");
    }
}

/* Emit already-decoded URL bytes safely: run the scheme filter (safe mode)
 * then percent-escape. `image_context` selects whether data: image URLs are
 * permitted (<img src> only). */
static void mdm_emit_decoded_url(mdm_ctx *r, const char *p, size_t n, bool image_context)
{
    bool safe = !(r->render_opts & MDPARSER_RF_UNSAFE);
    if (!safe || mdm_url_is_safe(p, n, image_context))
        mdm_escape_url(r, p, n);
}

/* Decode an attribute URL to raw bytes, then emit it safely. */
static void mdm_render_url_value(mdm_ctx *r, const MD_ATTRIBUTE *attr, bool image_context)
{
    const char *p;
    size_t n;
    if (mdparser_md4c_attr_plain(attr, &p, &n)) {
        mdm_emit_decoded_url(r, p, n, image_context);
        return;
    }
    smart_str raw = {0};
    mdparser_md4c_decode_attr(&raw, attr);
    mdm_emit_decoded_url(r, raw.s ? ZSTR_VAL(raw.s) : "",
        raw.s ? ZSTR_LEN(raw.s) : 0, image_context);
    smart_str_free(&raw);
}

/* Render an MD_ATTRIBUTE (code-fence lang, link/image title) HTML-escaped:
 * NORMAL substrings are HTML-escaped, entities decoded-then-re-escaped, null
 * chars replaced. URLs do NOT go through here -- they route through
 * mdm_render_url_value, which runs the scheme filter and percent-escapes. */
static void mdm_render_attribute(mdm_ctx *r, const MD_ATTRIBUTE *attr)
{
    for (int i = 0; attr->substr_offsets[i] < attr->size; i++) {
        MD_TEXTTYPE type = attr->substr_types[i];
        MD_OFFSET off = attr->substr_offsets[i];
        MD_SIZE sz = attr->substr_offsets[i + 1] - off;
        const char *text = attr->text + off;
        switch (type) {
            case MD_TEXT_NULLCHAR: mdm_append_codepoint(r, 0x0000); break;
            case MD_TEXT_ENTITY:   mdm_render_entity(r, text, sz); break;
            default:               mdm_escape_html(r, text, sz); break;
        }
    }
}

static void mdm_render_code_open(mdm_ctx *r, const MD_BLOCK_CODE_DETAIL *d)
{
    OUT_LIT(r, "<pre><code");
    if (d->lang.text != NULL) {
        OUT_LIT(r, " class=\"");
        if (d->lang.size < 9 || strncmp(d->lang.text, "language-", 9) != 0)
            OUT_LIT(r, "language-");
        mdm_render_attribute(r, &d->lang);
        OUT_LIT(r, "\"");
    }
    OUT_LIT(r, ">");
}

static void mdm_render_td_open(mdm_ctx *r, const char *cell, const MD_BLOCK_TD_DETAIL *d)
{
    OUT_LIT(r, "<");
    out_append(r, cell, strlen(cell));
    switch (d->align) {
        case MD_ALIGN_LEFT:   OUT_LIT(r, " align=\"left\">"); break;
        case MD_ALIGN_CENTER: OUT_LIT(r, " align=\"center\">"); break;
        case MD_ALIGN_RIGHT:  OUT_LIT(r, " align=\"right\">"); break;
        default:              OUT_LIT(r, ">"); break;
    }
}

static void mdm_render_a_open(mdm_ctx *r, const MD_SPAN_A_DETAIL *d)
{
    /* Decode the href once: both the fragment-nofollow exception and the
     * scheme filter must see the decoded bytes (md4c hands them out
     * entity-undecoded, so `&#35;section` and `javascript&colon;` would
     * otherwise dodge the respective checks). */
    smart_str href = {0};
    const char *hp;
    size_t hn;
    if (!mdparser_md4c_attr_plain(&d->href, &hp, &hn)) {
        mdparser_md4c_decode_attr(&href, &d->href);
        hp = href.s ? ZSTR_VAL(href.s) : "";
        hn = href.s ? ZSTR_LEN(href.s) : 0;
    }
    /* Fragment-only anchors (href="#...") are in-document links: skip nofollow. */
    bool fragment = hn > 0 && hp[0] == '#';

    /* rel before href to match the prior (postprocess-injected) attribute
     * order, keeping output stable for nofollow callers. */
    OUT_LIT(r, "<a");
    if ((r->render_opts & MDPARSER_RF_NOFOLLOW) && !fragment)
        OUT_LIT(r, " rel=\"nofollow noopener noreferrer\"");
    OUT_LIT(r, " href=\"");
    /* Link context: data: URLs are rejected (no navigable data: documents). */
    mdm_emit_decoded_url(r, hp, hn, false);
    smart_str_free(&href);
    if (d->title.text != NULL) {
        OUT_LIT(r, "\" title=\"");
        mdm_render_attribute(r, &d->title);
    }
    OUT_LIT(r, "\">");
}

/* Wiki-link target is URL-like, so route it through the same decode ->
 * scheme-filter -> percent-escape path as a normal <a href>. Without this,
 * [[javascript:alert(1)]] would emit a live javascript: link. nofollow and
 * the fragment exception follow the same rule as mdm_render_a_open. */
static void mdm_render_wikilink_open(mdm_ctx *r, const MD_SPAN_WIKILINK_DETAIL *d)
{
    smart_str href = {0};
    const char *hp;
    size_t hn;
    if (!mdparser_md4c_attr_plain(&d->target, &hp, &hn)) {
        mdparser_md4c_decode_attr(&href, &d->target);
        hp = href.s ? ZSTR_VAL(href.s) : "";
        hn = href.s ? ZSTR_LEN(href.s) : 0;
    }
    bool fragment = hn > 0 && hp[0] == '#';

    OUT_LIT(r, "<a");
    if ((r->render_opts & MDPARSER_RF_NOFOLLOW) && !fragment)
        OUT_LIT(r, " rel=\"nofollow noopener noreferrer\"");
    OUT_LIT(r, " class=\"wikilink\" href=\"");
    mdm_emit_decoded_url(r, hp, hn, false);
    smart_str_free(&href);
    OUT_LIT(r, "\">");
}

static void mdm_render_img_open(mdm_ctx *r, const MD_SPAN_IMG_DETAIL *d)
{
    OUT_LIT(r, "<img src=\"");
    /* Image context: data:image/{gif,png,jpeg,webp} permitted. */
    mdm_render_url_value(r, &d->src, true);
    OUT_LIT(r, "\" alt=\"");
}

static void mdm_render_img_close(mdm_ctx *r, const MD_SPAN_IMG_DETAIL *d)
{
    if (d->title.text != NULL) {
        OUT_LIT(r, "\" title=\"");
        mdm_render_attribute(r, &d->title);
    }
    OUT_LIT(r, "\" />");
}

/* md4c-html convention: <div class="admonition-TYPE"><p class="admonition-title">TYPE</p>.
 * TYPE is one of note/tip/important/warning/caution (entity-resolved, escaped). */
static void mdm_render_admonition_open(mdm_ctx *r, const MD_BLOCK_ADMONITION_DETAIL *d)
{
    const char *p;
    size_t n;
    smart_str raw = {0};
    if (!mdparser_md4c_attr_plain(&d->type, &p, &n)) {
        mdparser_md4c_decode_attr(&raw, &d->type);
        p = raw.s ? ZSTR_VAL(raw.s) : "";
        n = raw.s ? ZSTR_LEN(raw.s) : 0;
    }
    OUT_LIT(r, "<div class=\"admonition-");
    mdm_escape_html(r, p, n);
    OUT_LIT(r, "\">\n<p class=\"admonition-title\">");
    mdm_escape_html(r, p, n);
    OUT_LIT(r, "</p>\n");
    smart_str_free(&raw);
}

static int mdm_enter_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    mdm_ctx *r = (mdm_ctx *)userdata;
    /* The CommonMark reference renderer emits a conditional newline before
     * every block open; doing the same reproduces its inter-block whitespace
     * (notably `<li>\n<block>`). */
    mdm_cr(r);
    /* A new block starts a fresh SmartyPants quote context; otherwise the
     * previous block's trailing character bleeds in and a leading quote
     * renders as a closing quote (e.g. a paragraph that opens with "..."). */
    r->prev_char = 0;
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_QUOTE: OUT_LIT(r, "<blockquote>\n"); break;
        case MD_BLOCK_UL: OUT_LIT(r, "<ul>\n"); break;
        case MD_BLOCK_OL: mdm_render_ol_open(r, (MD_BLOCK_OL_DETAIL *)detail); break;
        case MD_BLOCK_LI: mdm_render_li_open(r, (MD_BLOCK_LI_DETAIL *)detail); break;
        case MD_BLOCK_HR: OUT_LIT(r, "<hr />\n"); break;
        case MD_BLOCK_H:
            r->heading_level = ((MD_BLOCK_H_DETAIL *)detail)->level;
            if (r->render_opts & MDPARSER_RF_HEADING_ANCHORS) {
                /* Buffer the heading's inner HTML + plain text; emit the
                 * full tag with id="slug" on leave. */
                r->in_heading = true;
                smart_str_free(&r->heading_html);
                smart_str_free(&r->heading_text);
                r->cur = &r->heading_html;
            } else {
                char tag[8];
                snprintf(tag, sizeof(tag), "<h%d>", r->heading_level);
                out_append(r, tag, strlen(tag));
            }
            break;
        case MD_BLOCK_CODE: mdm_render_code_open(r, (MD_BLOCK_CODE_DETAIL *)detail); break;
        case MD_BLOCK_HTML: break;
        case MD_BLOCK_P: OUT_LIT(r, "<p>"); break;
        case MD_BLOCK_TABLE: OUT_LIT(r, "<table>\n"); break;
        case MD_BLOCK_THEAD: OUT_LIT(r, "<thead>\n"); break;
        case MD_BLOCK_TBODY: OUT_LIT(r, "<tbody>\n"); break;
        case MD_BLOCK_TR: OUT_LIT(r, "<tr>\n"); break;
        case MD_BLOCK_TH: mdm_render_td_open(r, "th", (MD_BLOCK_TD_DETAIL *)detail); break;
        case MD_BLOCK_TD: mdm_render_td_open(r, "td", (MD_BLOCK_TD_DETAIL *)detail); break;
        case MD_BLOCK_FOOTNOTE_DEF_SECTION:
            OUT_LIT(r, "<section class=\"footnotes\">\n<ol>\n");
            break;
        case MD_BLOCK_FOOTNOTE_DEF: {
            char buf[64];
            int w = snprintf(buf, sizeof(buf), "<li id=\"fn-%u\">\n",
                ((MD_BLOCK_FOOTNOTE_DEF_DETAIL *)detail)->id);
            out_append(r, buf, (size_t)w);
            break;
        }
        case MD_BLOCK_ADMONITION:
            mdm_render_admonition_open(r, (MD_BLOCK_ADMONITION_DETAIL *)detail);
            break;
        default: break;
    }
    return 0;
}

static int mdm_leave_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    mdm_ctx *r = (mdm_ctx *)userdata;
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_QUOTE: OUT_LIT(r, "</blockquote>\n"); break;
        case MD_BLOCK_UL: OUT_LIT(r, "</ul>\n"); break;
        case MD_BLOCK_OL: OUT_LIT(r, "</ol>\n"); break;
        case MD_BLOCK_LI: OUT_LIT(r, "</li>\n"); break;
        case MD_BLOCK_HR: break;
        case MD_BLOCK_H:
            if (r->in_heading) {
                /* Compute the slug, emit the full heading to the main buffer. */
                r->in_heading = false;
                r->cur = &r->main;
                char *base = mdm_slugify(
                    r->heading_text.s ? ZSTR_VAL(r->heading_text.s) : "",
                    r->heading_text.s ? ZSTR_LEN(r->heading_text.s) : 0);
                char *slug = mdm_slug_unique(&r->slugs, base);
                /* strlen, not snprintf's return value: a truncating snprintf
                 * returns the would-be length, and (size_t)w would then
                 * over-read past the buffer. md4c caps the level at 6 so no
                 * truncation happens, but strlen is over-read-proof regardless. */
                if (slug[0] != '\0') {
                    char open[16];
                    snprintf(open, sizeof(open), "<h%d id=\"", r->heading_level);
                    out_append(r, open, strlen(open));
                    mdm_escape_html(r, slug, strlen(slug));
                    OUT_LIT(r, "\">");
                } else {
                    char tag[8];
                    snprintf(tag, sizeof(tag), "<h%d>", r->heading_level);
                    out_append(r, tag, strlen(tag));
                }
                efree(slug);
                if (r->heading_html.s)
                    out_append(r, ZSTR_VAL(r->heading_html.s), ZSTR_LEN(r->heading_html.s));
                char close[8];
                snprintf(close, sizeof(close), "</h%d>\n", r->heading_level);
                out_append(r, close, strlen(close));
            } else {
                char tag[8];
                snprintf(tag, sizeof(tag), "</h%d>\n", r->heading_level);
                out_append(r, tag, strlen(tag));
            }
            break;
        case MD_BLOCK_CODE: OUT_LIT(r, "</code></pre>\n"); break;
        case MD_BLOCK_HTML: break;
        case MD_BLOCK_P: OUT_LIT(r, "</p>\n"); break;
        case MD_BLOCK_TABLE: OUT_LIT(r, "</table>\n"); break;
        case MD_BLOCK_THEAD: OUT_LIT(r, "</thead>\n"); break;
        case MD_BLOCK_TBODY: OUT_LIT(r, "</tbody>\n"); break;
        case MD_BLOCK_TR: OUT_LIT(r, "</tr>\n"); break;
        case MD_BLOCK_TH: OUT_LIT(r, "</th>\n"); break;
        case MD_BLOCK_TD: OUT_LIT(r, "</td>\n"); break;
        case MD_BLOCK_FOOTNOTE_DEF_SECTION:
            OUT_LIT(r, "</ol>\n</section>\n");
            break;
        case MD_BLOCK_FOOTNOTE_DEF: {
            const MD_BLOCK_FOOTNOTE_DEF_DETAIL *d = detail;
            char buf[128];
            for (unsigned ref = 1; ref <= d->ref_count; ref++) {
                if (ref > 1) OUT_LIT(r, " ");
                int w = snprintf(buf, sizeof(buf),
                    "<a href=\"#fnref-%u-%u\" class=\"footnote-backref\">&#8617;</a>",
                    d->id, ref);
                out_append(r, buf, (size_t)w);
            }
            OUT_LIT(r, "\n</li>\n");
            break;
        }
        case MD_BLOCK_ADMONITION: OUT_LIT(r, "</div>\n"); break;
        default: break;
    }
    return 0;
}

static int mdm_enter_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    mdm_ctx *r = (mdm_ctx *)userdata;
    int inside_img = (r->image_nesting_level > 0);
    if (type == MD_SPAN_IMG) r->image_nesting_level++;
    if (inside_img) return 0;
    switch (type) {
        case MD_SPAN_EM: OUT_LIT(r, "<em>"); break;
        case MD_SPAN_STRONG: OUT_LIT(r, "<strong>"); break;
        case MD_SPAN_U: OUT_LIT(r, "<u>"); break;
        case MD_SPAN_A: mdm_render_a_open(r, (MD_SPAN_A_DETAIL *)detail); break;
        case MD_SPAN_IMG: mdm_render_img_open(r, (MD_SPAN_IMG_DETAIL *)detail); break;
        case MD_SPAN_CODE: OUT_LIT(r, "<code>"); break;
        case MD_SPAN_DEL: OUT_LIT(r, "<del>"); break;
        case MD_SPAN_SUPERSCRIPT: OUT_LIT(r, "<sup>"); break;
        case MD_SPAN_SUBSCRIPT: OUT_LIT(r, "<sub>"); break;
        case MD_SPAN_MARK: OUT_LIT(r, "<mark>"); break;
        case MD_SPAN_SPOILER: OUT_LIT(r, "<span class=\"spoiler\">"); break;
        case MD_SPAN_LATEXMATH: OUT_LIT(r, "<span class=\"math\">"); break;
        case MD_SPAN_LATEXMATH_DISPLAY: OUT_LIT(r, "<span class=\"math display\">"); break;
        case MD_SPAN_WIKILINK: mdm_render_wikilink_open(r, (MD_SPAN_WIKILINK_DETAIL *)detail); break;
        case MD_SPAN_FOOTNOTE_REF: {
            const MD_SPAN_FOOTNOTE_REF_DETAIL *d = detail;
            char buf[128];
            int w = snprintf(buf, sizeof(buf),
                "<sup><a href=\"#fn-%u\" id=\"fnref-%u-%u\">%u</a></sup>",
                d->id, d->id, d->ref_id, d->id);
            out_append(r, buf, (size_t)w);
            break;
        }
        default: break;
    }
    return 0;
}

static int mdm_leave_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    mdm_ctx *r = (mdm_ctx *)userdata;
    if (type == MD_SPAN_IMG) r->image_nesting_level--;
    if (r->image_nesting_level > 0) return 0;
    switch (type) {
        case MD_SPAN_EM: OUT_LIT(r, "</em>"); break;
        case MD_SPAN_STRONG: OUT_LIT(r, "</strong>"); break;
        case MD_SPAN_U: OUT_LIT(r, "</u>"); break;
        case MD_SPAN_A: OUT_LIT(r, "</a>"); break;
        case MD_SPAN_IMG: mdm_render_img_close(r, (MD_SPAN_IMG_DETAIL *)detail); break;
        case MD_SPAN_CODE: OUT_LIT(r, "</code>"); break;
        case MD_SPAN_DEL: OUT_LIT(r, "</del>"); break;
        case MD_SPAN_SUPERSCRIPT: OUT_LIT(r, "</sup>"); break;
        case MD_SPAN_SUBSCRIPT: OUT_LIT(r, "</sub>"); break;
        case MD_SPAN_MARK: OUT_LIT(r, "</mark>"); break;
        case MD_SPAN_SPOILER:
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY: OUT_LIT(r, "</span>"); break;
        case MD_SPAN_WIKILINK: OUT_LIT(r, "</a>"); break;
        default: break;
    }
    return 0;
}

static int mdm_text(MD_TEXTTYPE type, const char *text, MD_SIZE size, void *userdata)
{
    mdm_ctx *r = (mdm_ctx *)userdata;

    /* While buffering a heading, also accumulate plain text for the slug.
     * Entities are decoded to their raw bytes so `# &copy;` slugs the same
     * as a literal `# ©` (otherwise the heading would get no id at all). */
    if (r->in_heading) {
        if (type == MD_TEXT_NORMAL || type == MD_TEXT_CODE)
            smart_str_appendl(&r->heading_text, text, size);
        else if (type == MD_TEXT_ENTITY)
            mdparser_md4c_decode_entity(&r->heading_text, text, size);
        else if (type == MD_TEXT_SOFTBR || type == MD_TEXT_BR)
            smart_str_appendc(&r->heading_text, ' ');
    }

    switch (type) {
        case MD_TEXT_NULLCHAR: mdm_append_codepoint(r, 0x0000); break;
        case MD_TEXT_BR:
            if (r->image_nesting_level > 0) OUT_LIT(r, " ");
            else OUT_LIT(r, "<br />\n");
            r->prev_char = 0;
            break;
        case MD_TEXT_SOFTBR:
            if (r->image_nesting_level > 0) OUT_LIT(r, " ");
            else if (r->render_opts & MDPARSER_RF_NOBREAKS) OUT_LIT(r, " ");
            else OUT_LIT(r, "\n");
            r->prev_char = 0;
            break;
        case MD_TEXT_HTML:
            if (r->image_nesting_level > 0) mdm_escape_html(r, text, size);
            else mdm_render_raw_html(r, text, size);
            break;
        case MD_TEXT_ENTITY:
            mdm_render_entity(r, text, size);
            /* Seed quote context from the byte the entity actually rendered
             * (e.g. `&amp;` -> '&' is right-context; `&#32;` -> ' ' is left),
             * not a blanket 0 which would force an opening quote. */
            if (r->cur->s && ZSTR_LEN(r->cur->s) > 0)
                r->prev_char = mdm_trailing_quote_char(ZSTR_VAL(r->cur->s), ZSTR_LEN(r->cur->s));
            break;
        case MD_TEXT_CODE:
        case MD_TEXT_LATEXMATH:
            /* Verbatim TeX / code: HTML-escape, never SmartyPants. */
            mdm_escape_html(r, text, size);
            if (size) r->prev_char = mdm_trailing_quote_char(text, size);
            break;
        default:  /* MD_TEXT_NORMAL */
            if (r->render_opts & MDPARSER_RF_SMART) {
                mdm_render_smart(r, text, size);
            } else {
                mdm_escape_html(r, text, size);
                if (size) r->prev_char = mdm_trailing_quote_char(text, size);
            }
            break;
    }
    return 0;
}

/* ===================================================================
 * Entry point
 * =================================================================== */

zend_string *mdparser_md4c_render_html(const char *src, size_t len,
    unsigned parser_flags, int render_opts, int *status)
{
    mdm_ctx r;
    memset(&r, 0, sizeof(r));
    r.render_opts = render_opts;
    r.cur = &r.main;
    r.prev_char = 0;

    if (render_opts & MDPARSER_RF_HEADING_ANCHORS)
        mdm_slugs_init(&r.slugs);

    bool owned = false;
    size_t use_len = len;
    const char *use_src = src;
    mdparser_md4c_skip_bom(&use_src, &use_len);
    if (render_opts & MDPARSER_RF_VALIDATE_UTF8)
        use_src = mdparser_md4c_validate_utf8(use_src, use_len, &use_len, &owned);

    /* Reserve roughly the input size up front: HTML output is typically
     * 1-1.5x the markdown, so this skips the early smart_str doublings (and
     * their cumulative memcpy) on medium/large documents. */
    if (use_len)
        smart_str_alloc(&r.main, use_len, 0);

    MD_PARSER parser = {
        0,
        parser_flags,
        mdm_enter_block,
        mdm_leave_block,
        mdm_enter_span,
        mdm_leave_span,
        mdm_text,
        NULL,
        NULL
    };

    int rc = md_parse(use_src, (MD_SIZE)use_len, &parser, &r);

    if (owned) efree((void *)use_src);
    smart_str_free(&r.heading_html);
    smart_str_free(&r.heading_text);
    mdm_slugs_destroy(&r.slugs);

    if (rc != 0) {
        smart_str_free(&r.main);
        *status = MDM_ERR_PARSE;
        return NULL;
    }

    *status = MDM_OK;
    smart_str_0(&r.main);
    if (!r.main.s)
        return ZSTR_EMPTY_ALLOC();
    return r.main.s;  /* ownership transfers to caller */
}
