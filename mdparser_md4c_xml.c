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

#include <stdint.h>
#include <string.h>

#if !defined(MDPARSER_DISABLE_SIMD) && (defined(__x86_64__) || defined(_M_X64))
# define MDX_SIMD_SSE2 1
# include <emmintrin.h>
# if defined(_MSC_VER) && !defined(__clang__)
#  include <intrin.h>
static zend_always_inline unsigned mdx_ctz32(uint32_t m)
{
    unsigned long idx;
    _BitScanForward(&idx, m);
    return (unsigned)idx;
}
# else
static zend_always_inline unsigned mdx_ctz32(uint32_t m)
{
    return (unsigned)__builtin_ctz(m);
}
# endif
#elif !defined(MDPARSER_DISABLE_SIMD) && defined(__aarch64__) \
    && (defined(__ARM_NEON) || defined(__ARM_NEON__)) \
    && (defined(__AARCH64EL__) || defined(_M_ARM64) \
        || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
# define MDX_SIMD_NEON 1
# include <arm_neon.h>
#endif

#include "md4c.h"
#include "php_mdparser.h"
#include "mdparser_md4c_util.h"
#include "mdparser_md4c_vendor.h"
#include "mdparser_md4c_xml.h"

/* Streaming md4c -> CommonMark-XML emitter. Mirrors the CommonMark
 * reference XML layout (2-space indent, xml:space="preserve" on
 * text/code/html) over the same node names the AST builder uses.
 * Source positions are not emitted. */

#define MDX_OK        0
#define MDX_ERR_PARSE 1
#define MDX_ERR_DEPTH 2
#define MDX_ERR_MEMORY 3
#define MDX_MAX_INDENT_DEPTH 32
#define MDX_ERR_CELLS 4
#define MDX_MAX_TABLE_CELLS 65536

const char *mdparser_md4c_xml_status_message(int status)
{
    switch (status) {
        case MDX_ERR_PARSE: return "mdparser: md4c parser failed";
        case MDX_ERR_DEPTH: return "mdparser: XML nesting exceeds maximum depth";
        case MDX_ERR_MEMORY: return "mdparser: parse out of memory (allocation failed or mdparser.parse_memory_limit exceeded)";
        case MDX_ERR_CELLS: return "mdparser: table cell limit exceeded";
        default: return "mdparser: unknown error";
    }
}

typedef struct {
    smart_str out;
    int depth;
    size_t table_cells;
    bool wikilinks_enabled;
    bool collecting;   /* code/code_block/html literal */
    int error;
} mdx_ctx;

#define X_LIT(c, lit) smart_str_appendl(&(c)->out, "" lit, sizeof(lit) - 1)

static zend_always_inline void mdx_indent_appendl(mdx_ctx *c, const char *s, size_t n)
{
    /* 32 levels × 2 spaces; MDX_MAX_INDENT_DEPTH is 32. */
    static const char spaces[MDX_MAX_INDENT_DEPTH * 2 + 1] =
        "                                                                ";
    size_t ind = (size_t)(c->depth < MDX_MAX_INDENT_DEPTH
        ? c->depth : MDX_MAX_INDENT_DEPTH) * 2;
    char *p = smart_str_extend(&c->out, ind + n);
    if (ind) {
        memcpy(p, spaces, ind);
    }
    memcpy(p + ind, s, n);
}

#define X_IND_LIT(c, lit) mdx_indent_appendl((c), "" lit, sizeof(lit) - 1)
#define MDX_OPEN(c, tag) do { \
    mdx_indent_appendl((c), "<" tag ">\n", sizeof("<" tag ">\n") - 1); \
    (c)->depth++; \
} while (0)
#define MDX_CLOSE(c, tag) do { \
    if (UNEXPECTED((c)->depth <= 1)) { \
        (c)->error = MDX_ERR_PARSE; \
    } else { \
        (c)->depth--; \
        mdx_indent_appendl((c), "</" tag ">\n", sizeof("</" tag ">\n") - 1); \
    } \
} while (0)

/* Static attention maps for XML text and attribute escaping: C0 controls
 * (except \t/\n/\r in text mode), &, <, >, 0xEF (U+FFFE/U+FFFF lead byte),
 * and " / \t / \n / \r in attribute mode. */
#define MDX_ESC_COMMON_C0 \
    1,1,1,1,1,1,1,1, 1,0,0,1,1,0,1,1, \
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1
#define MDX_ESC_ATTR_C0 \
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, \
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1

static const unsigned char mdx_esc_text[256] = {
    MDX_ESC_COMMON_C0,
    /* 0x20..0x2F: '&'(0x26) */
    0,0,0,0,0,0,1,0, 0,0,0,0,0,0,0,0,
    /* 0x30..0x3F: '<'(0x3C), '>'(0x3E) */
    0,0,0,0,0,0,0,0, 0,0,0,0,1,0,1,0,
    /* 0x40..0xDF */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    /* 0xE0..0xEF: 0xEF */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,1,
    /* 0xF0..0xFF */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0
};

static const unsigned char mdx_esc_attr[256] = {
    MDX_ESC_ATTR_C0,
    /* 0x20..0x2F: '"'(0x22), '&'(0x26) */
    0,0,1,0,0,0,1,0, 0,0,0,0,0,0,0,0,
    /* 0x30..0x3F: '<'(0x3C), '>'(0x3E) */
    0,0,0,0,0,0,0,0, 0,0,0,0,1,0,1,0,
    /* 0x40..0xDF */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    /* 0xE0..0xEF: 0xEF */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,1,
    /* 0xF0..0xFF */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0
};

static zend_always_inline char *mdx_emit_escaped_byte(
    char *dst, const unsigned char *p, const unsigned char *end,
    const bool attr, size_t *consumed)
{
    unsigned char ch = p[0];
    *consumed = 1;
    if (ch == 0xEF && (size_t)(end - p) >= 3 && p[1] == 0xBF
        && (p[2] == 0xBE || p[2] == 0xBF)) {
        memcpy(dst, "\xef\xbf\xbd", 3);
        *consumed = 3;
        return dst + 3;
    }
    switch (ch) {
        case '&': memcpy(dst, "&amp;", 5); return dst + 5;
        case '<': memcpy(dst, "&lt;", 4); return dst + 4;
        case '>': memcpy(dst, "&gt;", 4); return dst + 4;
        case '"':
            if (attr) { memcpy(dst, "&quot;", 6); return dst + 6; }
            break;
        case '\t':
            if (attr) { memcpy(dst, "&#x9;", 5); return dst + 5; }
            break;
        case '\n':
            if (attr) { memcpy(dst, "&#xA;", 5); return dst + 5; }
            break;
        case '\r':
            if (attr) { memcpy(dst, "&#xD;", 5); return dst + 5; }
            break;
        default:
            if (ch < 0x20) {
                memcpy(dst, "\xef\xbf\xbd", 3);
                return dst + 3;
            }
            break;
    }
    *dst++ = (char)ch;
    return dst;
}

static zend_always_inline void mdx_escape_impl(smart_str *b, const char *s,
    size_t n, const bool attr)
{
    if (n == 0) {
        return;
    }
    const unsigned char *map = attr ? mdx_esc_attr : mdx_esc_text;
    const unsigned char *p = (const unsigned char *)s;
    const unsigned char *end = p + n;

#ifdef MDX_SIMD_SSE2
    const __m128i v_sign = _mm_set1_epi8((char)0x80);
    const __m128i v_c0   = _mm_set1_epi8((char)(0x20 ^ 0x80));
    const __m128i v_amp  = _mm_set1_epi8('&');
    const __m128i v_lt   = _mm_set1_epi8('<');
    const __m128i v_gt   = _mm_set1_epi8('>');
    const __m128i v_ef   = _mm_set1_epi8((char)0xEF);
    const __m128i v_quot = _mm_set1_epi8('"');
    const __m128i v_tab  = _mm_set1_epi8('\t');
    const __m128i v_lf   = _mm_set1_epi8('\n');
    const __m128i v_cr   = _mm_set1_epi8('\r');
#elif defined(MDX_SIMD_NEON)
    const uint8x16_t v_c0   = vdupq_n_u8(0x20);
    const uint8x16_t v_amp  = vdupq_n_u8('&');
    const uint8x16_t v_lt   = vdupq_n_u8('<');
    const uint8x16_t v_gt   = vdupq_n_u8('>');
    const uint8x16_t v_ef   = vdupq_n_u8(0xEF);
    const uint8x16_t v_quot = vdupq_n_u8('"');
    const uint8x16_t v_tab  = vdupq_n_u8('\t');
    const uint8x16_t v_lf   = vdupq_n_u8('\n');
    const uint8x16_t v_cr   = vdupq_n_u8('\r');
#endif

    bool grew_once = false;
    while (p < end) {
        if (UNEXPECTED(mdparser_md4c_oom_pending())) {
            return;
        }
        size_t rem = (size_t)(end - p);
        size_t min_avail = rem < 24 ? rem + 6 : 24;
        if (UNEXPECTED(!b->s || b->a - ZSTR_LEN(b->s) < min_avail)) {
            size_t grow = (!grew_once && rem <= (16U << 20))
                ? rem + 6
                : (rem < (2U << 20) ? rem + 6 : (2U << 20) + 6);
            grew_once = true;
            smart_str_alloc(b, grow, 0);
            if (UNEXPECTED(mdparser_md4c_oom_pending())) {
                return;
            }
        }
        size_t len = ZSTR_LEN(b->s);
        char *dst = ZSTR_VAL(b->s) + len;
        size_t avail = b->a - len;

#ifdef MDX_SIMD_SSE2
        while (p + 16 <= end && avail >= 22) {
            __m128i v = _mm_loadu_si128((const __m128i *)p);
            __m128i c0 = _mm_cmplt_epi8(_mm_xor_si128(v, v_sign), v_c0);
            if (!attr) {
                __m128i ws = _mm_or_si128(_mm_cmpeq_epi8(v, v_lf),
                    _mm_or_si128(_mm_cmpeq_epi8(v, v_tab), _mm_cmpeq_epi8(v, v_cr)));
                c0 = _mm_andnot_si128(ws, c0);
            }
            __m128i m = _mm_or_si128(c0,
                _mm_or_si128(_mm_cmpeq_epi8(v, v_amp),
                _mm_or_si128(_mm_cmpeq_epi8(v, v_lt),
                _mm_or_si128(_mm_cmpeq_epi8(v, v_gt), _mm_cmpeq_epi8(v, v_ef)))));
            if (attr) {
                m = _mm_or_si128(m, _mm_cmpeq_epi8(v, v_quot));
            }
            uint32_t mask = (uint32_t)_mm_movemask_epi8(m);
            if (EXPECTED(mask == 0)) {
                _mm_storeu_si128((__m128i *)dst, v);
                p += 16;
                dst += 16;
                avail -= 16;
            } else {
                size_t k = mdx_ctz32(mask);
                if (k > 0) {
                    memcpy(dst, p, k);
                    p += k;
                    dst += k;
                    avail -= k;
                }
                size_t consumed;
                char *next_dst = mdx_emit_escaped_byte(dst, p, end, attr, &consumed);
                avail -= (size_t)(next_dst - dst);
                dst = next_dst;
                p += consumed;
            }
        }
#elif defined(MDX_SIMD_NEON)
        while (p + 16 <= end && avail >= 22) {
            uint8x16_t v = vld1q_u8(p);
            uint8x16_t c0 = vcltq_u8(v, v_c0);
            if (!attr) {
                uint8x16_t ws = vorrq_u8(vceqq_u8(v, v_lf),
                    vorrq_u8(vceqq_u8(v, v_tab), vceqq_u8(v, v_cr)));
                c0 = vbicq_u8(c0, ws);
            }
            uint8x16_t m = vorrq_u8(c0,
                vorrq_u8(vceqq_u8(v, v_amp),
                vorrq_u8(vceqq_u8(v, v_lt),
                vorrq_u8(vceqq_u8(v, v_gt), vceqq_u8(v, v_ef)))));
            if (attr) {
                m = vorrq_u8(m, vceqq_u8(v, v_quot));
            }
            uint64x2_t m64 = vreinterpretq_u64_u8(m);
            uint64_t lo = vgetq_lane_u64(m64, 0);
            uint64_t hi = vgetq_lane_u64(m64, 1);
            if (EXPECTED((lo | hi) == 0)) {
                vst1q_u8((uint8_t *)dst, v);
                p += 16;
                dst += 16;
                avail -= 16;
            } else {
                size_t k = lo ? (size_t)(__builtin_ctzll(lo) >> 3)
                              : 8 + (size_t)(__builtin_ctzll(hi) >> 3);
                if (k > 0) {
                    memcpy(dst, p, k);
                    p += k;
                    dst += k;
                    avail -= k;
                }
                size_t consumed;
                char *next_dst = mdx_emit_escaped_byte(dst, p, end, attr, &consumed);
                avail -= (size_t)(next_dst - dst);
                dst = next_dst;
                p += consumed;
            }
        }
#endif

        while (p < end && avail >= 6) {
            if (EXPECTED(!map[*p])) {
                *dst++ = (char)*p++;
                avail--;
            } else {
                size_t consumed;
                char *next_dst = mdx_emit_escaped_byte(dst, p, end, attr, &consumed);
                avail -= (size_t)(next_dst - dst);
                dst = next_dst;
                p += consumed;
            }
        }
        ZSTR_LEN(b->s) = (size_t)(dst - ZSTR_VAL(b->s));
    }
}

static void mdx_escape_text(smart_str *b, const char *s, size_t n)
{
    mdx_escape_impl(b, s, n, false);
}

static void mdx_escape_attr(smart_str *b, const char *s, size_t n)
{
    mdx_escape_impl(b, s, n, true);
}

static size_t mdx_encode_cp(char *out, unsigned cp)
{
    if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        out[0] = (char)0xef; out[1] = (char)0xbf; out[2] = (char)0xbd;
        return 3;
    }
    if (cp <= 0x7f) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp <= 0x7ff) {
        out[0] = (char)(0xc0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }
    if (cp <= 0xffff) {
        out[0] = (char)(0xe0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

/* Decode an entity reference into an 8-byte stack buffer (at most 2 UTF-8
 * codepoints), then XML-escape directly into the output. */
static void mdx_emit_entity(mdx_ctx *c, const char *text, MD_SIZE size)
{
    unsigned cps[2];
    int n = mdparser_md4c_decode_entity_cps(text, size, cps);
    if (n == 0) {
        mdx_escape_text(&c->out, text, size);
        return;
    }
    char buf[8];
    size_t len = 0;
    for (int k = 0; k < n; k++) {
        len += mdx_encode_cp(buf + len, cps[k]);
    }
    mdx_escape_text(&c->out, buf, len);
}

static void mdx_attr(mdx_ctx *c, const char *prefix, size_t prefix_len, const MD_ATTRIBUTE *a)
{
    smart_str_appendl(&c->out, prefix, prefix_len);
    /* md4c hands attributes out entity-undecoded across typed substrings;
     * resolve them to raw bytes first, then XML-escape once. Escaping the
     * raw bytes directly would double-encode entities (&amp; -> &amp;amp;). */
    mdparser_md4c_attr_view value;
    mdparser_md4c_attr_view_init(&value, a);
    mdx_escape_attr(&c->out, value.text, value.size);
    mdparser_md4c_attr_view_destroy(&value);
    smart_str_appendc(&c->out, '"');
}

#define MDX_ATTR(c, name, a) mdx_attr((c), " " name "=\"", sizeof(" " name "=\"") - 1, (a))

static int mdx_enter_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    mdx_ctx *c = userdata;
    /* Depth counts open XML containers, not the AST zval-stack model.
     * XML increments for table_header/table_body/footnote_section and does
     * not for leaf code_block/html_block/thematic_break; AST does the reverse
     * for those. Same Markdown can therefore hit MDPARSER_MAX_AST_DEPTH on
     * only one path. Indentation is capped independently (MDX_MAX_INDENT_DEPTH)
     * so valid near-limit trees remain linear-sized. */
    if (UNEXPECTED(c->depth >= MDPARSER_MAX_AST_DEPTH)) { c->error = MDX_ERR_DEPTH; return 1; }
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_QUOTE: MDX_OPEN(c, "block_quote"); break;
        case MD_BLOCK_UL:
            if (((MD_BLOCK_UL_DETAIL *)detail)->is_tight) {
                X_IND_LIT(c, "<list type=\"bullet\" tight=\"true\">\n");
            } else {
                X_IND_LIT(c, "<list type=\"bullet\" tight=\"false\">\n");
            }
            c->depth++;
            break;
        case MD_BLOCK_OL: {
            MD_BLOCK_OL_DETAIL *d = detail;
            X_IND_LIT(c, "<list type=\"ordered\" start=\"");
            smart_str_append_unsigned(&c->out, d->start);
            if (d->mark_delimiter == ')') {
                if (d->is_tight) X_LIT(c, "\" delim=\"paren\" tight=\"true\">\n");
                else             X_LIT(c, "\" delim=\"paren\" tight=\"false\">\n");
            } else {
                if (d->is_tight) X_LIT(c, "\" delim=\"period\" tight=\"true\">\n");
                else             X_LIT(c, "\" delim=\"period\" tight=\"false\">\n");
            }
            c->depth++;
            break;
        }
        case MD_BLOCK_LI: {
            MD_BLOCK_LI_DETAIL *d = detail;
            if (d->is_task) {
                if (d->task_mark == 'x' || d->task_mark == 'X') {
                    X_IND_LIT(c, "<item checked=\"true\">\n");
                } else {
                    X_IND_LIT(c, "<item checked=\"false\">\n");
                }
            } else {
                X_IND_LIT(c, "<item>\n");
            }
            c->depth++;
            break;
        }
        case MD_BLOCK_HR: X_IND_LIT(c, "<thematic_break />\n"); break;
        case MD_BLOCK_BLANK: X_IND_LIT(c, "<blank />\n"); break;
        case MD_BLOCK_H: {
            static const char *const h_open[6] = {
                "<heading level=\"1\">\n", "<heading level=\"2\">\n",
                "<heading level=\"3\">\n", "<heading level=\"4\">\n",
                "<heading level=\"5\">\n", "<heading level=\"6\">\n"
            };
            unsigned lvl = ((MD_BLOCK_H_DETAIL *)detail)->level;
            if (EXPECTED(lvl >= 1 && lvl <= 6)) {
                mdx_indent_appendl(c, h_open[lvl - 1], sizeof("<heading level=\"1\">\n") - 1);
            } else {
                X_IND_LIT(c, "<heading level=\"");
                smart_str_append_unsigned(&c->out, lvl);
                X_LIT(c, "\">\n");
            }
            c->depth++;
            break;
        }
        case MD_BLOCK_CODE: {
            MD_BLOCK_CODE_DETAIL *d = detail;
            if (d->info.text) {
                X_IND_LIT(c, "<code_block");
                MDX_ATTR(c, "info", &d->info);
                X_LIT(c, " xml:space=\"preserve\">");
            } else {
                X_IND_LIT(c, "<code_block xml:space=\"preserve\">");
            }
            c->collecting = true;
            break;
        }
        case MD_BLOCK_HTML:
            X_IND_LIT(c, "<html_block xml:space=\"preserve\">");
            c->collecting = true;
            break;
        case MD_BLOCK_P: MDX_OPEN(c, "paragraph"); break;
        case MD_BLOCK_TABLE: MDX_OPEN(c, "table"); break;
        case MD_BLOCK_THEAD: MDX_OPEN(c, "table_header"); break;
        case MD_BLOCK_TBODY: MDX_OPEN(c, "table_body"); break;
        case MD_BLOCK_TR: MDX_OPEN(c, "table_row"); break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            if (UNEXPECTED(c->table_cells >= MDX_MAX_TABLE_CELLS)) {
                c->error = MDX_ERR_CELLS;
                return 1;
            }
            c->table_cells++;
            switch (((MD_BLOCK_TD_DETAIL *)detail)->align) {
                case MD_ALIGN_LEFT:   X_IND_LIT(c, "<table_cell align=\"left\">\n"); break;
                case MD_ALIGN_CENTER: X_IND_LIT(c, "<table_cell align=\"center\">\n"); break;
                case MD_ALIGN_RIGHT:  X_IND_LIT(c, "<table_cell align=\"right\">\n"); break;
                default:              X_IND_LIT(c, "<table_cell>\n"); break;
            }
            c->depth++;
            break;
        }
        case MD_BLOCK_FOOTNOTE_DEF_SECTION: MDX_OPEN(c, "footnote_section"); break;
        case MD_BLOCK_FOOTNOTE_DEF: {
            X_IND_LIT(c, "<footnote_definition id=\"");
            smart_str_append_unsigned(&c->out, ((MD_BLOCK_FOOTNOTE_DEF_DETAIL *)detail)->id);
            X_LIT(c, "\">\n");
            c->depth++;
            break;
        }
        case MD_BLOCK_ADMONITION:
            X_IND_LIT(c, "<admonition");
            MDX_ATTR(c, "type", &((MD_BLOCK_ADMONITION_DETAIL *)detail)->type);
            X_LIT(c, ">\n");
            c->depth++;
            break;
        default: MDX_OPEN(c, "unknown"); break;
    }
    return mdparser_md4c_cb_status(c->error ? 1 : 0);
}

static int mdx_leave_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    mdx_ctx *c = userdata;
    (void)detail;
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_QUOTE: MDX_CLOSE(c, "block_quote"); break;
        case MD_BLOCK_UL:
        case MD_BLOCK_OL: MDX_CLOSE(c, "list"); break;
        case MD_BLOCK_LI: MDX_CLOSE(c, "item"); break;
        case MD_BLOCK_HR: break;
        case MD_BLOCK_BLANK: break;
        case MD_BLOCK_H: MDX_CLOSE(c, "heading"); break;
        case MD_BLOCK_CODE:
            X_LIT(c, "</code_block>\n");
            c->collecting = false;
            break;
        case MD_BLOCK_HTML:
            X_LIT(c, "</html_block>\n");
            c->collecting = false;
            break;
        case MD_BLOCK_P: MDX_CLOSE(c, "paragraph"); break;
        case MD_BLOCK_TABLE: MDX_CLOSE(c, "table"); break;
        case MD_BLOCK_THEAD: MDX_CLOSE(c, "table_header"); break;
        case MD_BLOCK_TBODY: MDX_CLOSE(c, "table_body"); break;
        case MD_BLOCK_TR: MDX_CLOSE(c, "table_row"); break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: MDX_CLOSE(c, "table_cell"); break;
        case MD_BLOCK_FOOTNOTE_DEF_SECTION: MDX_CLOSE(c, "footnote_section"); break;
        case MD_BLOCK_FOOTNOTE_DEF: MDX_CLOSE(c, "footnote_definition"); break;
        case MD_BLOCK_ADMONITION: MDX_CLOSE(c, "admonition"); break;
        default: MDX_CLOSE(c, "unknown"); break;
    }
    return mdparser_md4c_cb_status(c->error ? 1 : 0);
}

static int mdx_enter_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    mdx_ctx *c = userdata;
    if (type == MD_SPAN_WIKILINK && !c->wikilinks_enabled) return 0;
    if (UNEXPECTED(c->depth >= MDPARSER_MAX_AST_DEPTH)) { c->error = MDX_ERR_DEPTH; return 1; }
    switch (type) {
        case MD_SPAN_EM: MDX_OPEN(c, "emph"); break;
        case MD_SPAN_STRONG: MDX_OPEN(c, "strong"); break;
        case MD_SPAN_A: {
            MD_SPAN_A_DETAIL *d = detail;
            X_IND_LIT(c, "<link");
            MDX_ATTR(c, "destination", &d->href);
            MDX_ATTR(c, "title", &d->title);
            X_LIT(c, ">\n");
            c->depth++;
            break;
        }
        case MD_SPAN_IMG: {
            MD_SPAN_IMG_DETAIL *d = detail;
            X_IND_LIT(c, "<image");
            MDX_ATTR(c, "destination", &d->src);
            MDX_ATTR(c, "title", &d->title);
            X_LIT(c, ">\n");
            c->depth++;
            break;
        }
        case MD_SPAN_CODE:
            X_IND_LIT(c, "<code xml:space=\"preserve\">");
            c->collecting = true;
            break;
        case MD_SPAN_DEL: MDX_OPEN(c, "strikethrough"); break;
        case MD_SPAN_INS: MDX_OPEN(c, "insert"); break;
        case MD_SPAN_U: MDX_OPEN(c, "underline"); break;
        case MD_SPAN_SUPERSCRIPT: MDX_OPEN(c, "superscript"); break;
        case MD_SPAN_SUBSCRIPT: MDX_OPEN(c, "subscript"); break;
        case MD_SPAN_MARK: MDX_OPEN(c, "highlight"); break;
        case MD_SPAN_SPOILER: MDX_OPEN(c, "spoiler"); break;
        case MD_SPAN_LATEXMATH: MDX_OPEN(c, "latex_math"); break;
        case MD_SPAN_LATEXMATH_DISPLAY: MDX_OPEN(c, "latex_math_display"); break;
        case MD_SPAN_WIKILINK: {
            MD_SPAN_WIKILINK_DETAIL *d = detail;
            X_IND_LIT(c, "<wikilink");
            MDX_ATTR(c, "destination", &d->target);
            X_LIT(c, ">\n");
            c->depth++;
            break;
        }
        case MD_SPAN_FOOTNOTE_REF: {
            X_IND_LIT(c, "<footnote_reference id=\"");
            smart_str_append_unsigned(&c->out, ((MD_SPAN_FOOTNOTE_REF_DETAIL *)detail)->id);
            X_LIT(c, "\">\n");
            c->depth++;
            break;
        }
        default: MDX_OPEN(c, "unknown"); break;
    }
    return mdparser_md4c_cb_status(0);
}

static int mdx_leave_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    mdx_ctx *c = userdata;
    (void)detail;
    if (type == MD_SPAN_WIKILINK && !c->wikilinks_enabled) return 0;
    switch (type) {
        case MD_SPAN_EM: MDX_CLOSE(c, "emph"); break;
        case MD_SPAN_STRONG: MDX_CLOSE(c, "strong"); break;
        case MD_SPAN_A: MDX_CLOSE(c, "link"); break;
        case MD_SPAN_IMG: MDX_CLOSE(c, "image"); break;
        case MD_SPAN_CODE:
            X_LIT(c, "</code>\n");
            c->collecting = false;
            break;
        case MD_SPAN_DEL: MDX_CLOSE(c, "strikethrough"); break;
        case MD_SPAN_INS: MDX_CLOSE(c, "insert"); break;
        case MD_SPAN_U: MDX_CLOSE(c, "underline"); break;
        case MD_SPAN_SUPERSCRIPT: MDX_CLOSE(c, "superscript"); break;
        case MD_SPAN_SUBSCRIPT: MDX_CLOSE(c, "subscript"); break;
        case MD_SPAN_MARK: MDX_CLOSE(c, "highlight"); break;
        case MD_SPAN_SPOILER: MDX_CLOSE(c, "spoiler"); break;
        case MD_SPAN_LATEXMATH: MDX_CLOSE(c, "latex_math"); break;
        case MD_SPAN_LATEXMATH_DISPLAY: MDX_CLOSE(c, "latex_math_display"); break;
        case MD_SPAN_WIKILINK: MDX_CLOSE(c, "wikilink"); break;
        case MD_SPAN_FOOTNOTE_REF: MDX_CLOSE(c, "footnote_reference"); break;
        default: MDX_CLOSE(c, "unknown"); break;
    }
    return mdparser_md4c_cb_status(c->error ? 1 : 0);
}

static int mdx_text(MD_TEXTTYPE type, const char *text, MD_SIZE size, void *userdata)
{
    mdx_ctx *c = userdata;
    if (c->collecting) {
        mdx_escape_text(&c->out, text, size);
        return mdparser_md4c_cb_status(0);
    }
    switch (type) {
        case MD_TEXT_SOFTBR: X_IND_LIT(c, "<softbreak />\n"); break;
        case MD_TEXT_BR: X_IND_LIT(c, "<linebreak />\n"); break;
        case MD_TEXT_HTML:
            X_IND_LIT(c, "<html_inline xml:space=\"preserve\">");
            mdx_escape_text(&c->out, text, size);
            X_LIT(c, "</html_inline>\n");
            break;
        case MD_TEXT_ENTITY:
            X_IND_LIT(c, "<text xml:space=\"preserve\">");
            mdx_emit_entity(c, text, size);
            X_LIT(c, "</text>\n");
            break;
        case MD_TEXT_NULLCHAR:
            X_IND_LIT(c, "<text xml:space=\"preserve\">\xef\xbf\xbd</text>\n");
            break;
        default:
            X_IND_LIT(c, "<text xml:space=\"preserve\">");
            mdx_escape_text(&c->out, text, size);
            X_LIT(c, "</text>\n");
            break;
    }
    return mdparser_md4c_cb_status(0);
}

zend_string *mdparser_md4c_render_xml(const char *src, size_t len,
    unsigned parser_flags, bool validate_utf8, int *status)
{
    static const char xml_prolog[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE document SYSTEM \"CommonMark.dtd\">\n"
        "<document xmlns=\"http://commonmark.org/xml/1.0\">\n";
    const size_t prolog_len = sizeof(xml_prolog) - 1;

    mdx_ctx c;
    memset(&c, 0, sizeof(c));
    c.wikilinks_enabled = (parser_flags & MD_FLAG_WIKILINKS) != 0;
    c.depth = 1;

    MD_PARSER parser = {
        0, parser_flags,
        mdx_enter_block, mdx_leave_block,
        mdx_enter_span, mdx_leave_span,
        mdx_text, NULL, NULL
    };

    /* validateUtf8: rewrite invalid input bytes to U+FFFD before parsing, so
     * the XML declaration's UTF-8 claim holds (parity with the HTML path). */
    bool owned = false;
    size_t use_len = len;
    const char *use_src = src;
    mdparser_md4c_skip_bom(&use_src, &use_len);
    if (validate_utf8) {
        use_src = mdparser_md4c_validate_utf8(use_src, use_len, &use_len, &owned);
        if (!use_src) {
            *status = MDX_ERR_MEMORY;
            return NULL;
        }
    }

    /* Reserve about twice the input size (plus prolog) up to 1 MiB before
     * appending the XML prolog so ordinary docs skip an initial realloc. */
    size_t reserve = use_len <= (MDPARSER_INITIAL_OUTPUT_RESERVE_MAX - prolog_len) / 2
        ? use_len * 2 + prolog_len
        : MDPARSER_INITIAL_OUTPUT_RESERVE_MAX;
    smart_str_alloc(&c.out, reserve, 0);
    if (UNEXPECTED(mdparser_md4c_oom_pending())) {
        if (owned) efree((void *)use_src);
        smart_str_free(&c.out);
        *status = MDX_ERR_MEMORY;
        return NULL;
    }
    smart_str_appendl(&c.out, xml_prolog, prolog_len);

    bool bailed_out;
    bool limit_exceeded;
    bool alloc_failed;
    int rc = mdparser_md4c_parse(use_src, (MD_SIZE)use_len, &parser, &c,
        &bailed_out, &limit_exceeded, &alloc_failed);
    if (owned) efree((void *)use_src);

    if (UNEXPECTED(bailed_out)) {
        smart_str_free(&c.out);
        zend_bailout();
    }

    if (UNEXPECTED(c.error != 0 || rc != 0 || c.depth != 1)) {
        if (c.error == 0) {
            c.error = (limit_exceeded || alloc_failed) ? MDX_ERR_MEMORY : MDX_ERR_PARSE;
        }
        smart_str_free(&c.out);
        *status = c.error;
        return NULL;
    }
    c.depth = 0;
    X_LIT(&c, "</document>\n");
    *status = MDX_OK;
    return smart_str_extract(&c.out);
}
