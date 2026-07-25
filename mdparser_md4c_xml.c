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
#define MDX_MAX_INDENT_DEPTH 32

const char *mdparser_md4c_xml_status_message(int status)
{
    switch (status) {
        case MDX_ERR_PARSE: return "mdparser: md4c parser failed";
        case MDX_ERR_DEPTH: return "mdparser: XML nesting exceeds maximum depth";
        default: return "mdparser: unknown error";
    }
}

typedef struct {
    smart_str out;
    int depth;
    bool collecting;   /* code/code_block/html literal */
    int error;
} mdx_ctx;

static void mdx_indent(mdx_ctx *c)
{
    /* 32 levels × 2 spaces; MDX_MAX_INDENT_DEPTH is 32. */
    static const char spaces[64] =
        "                                                                ";
    int indent_depth = c->depth < MDX_MAX_INDENT_DEPTH
        ? c->depth : MDX_MAX_INDENT_DEPTH;
    smart_str_appendl(&c->out, spaces, (size_t)indent_depth * 2);
}

#define X_LIT(c, lit) smart_str_appendl(&(c)->out, "" lit, sizeof(lit) - 1)

/* XML-escape into the output buffer (& < > and, for attrs, "). C0 control
 * bytes other than tab/LF/CR are illegal in XML 1.0 character data, so they
 * are replaced with U+FFFD (matching the NULLCHAR policy) -- emitting them
 * raw would make the whole document unparseable. */
static void mdx_escape(smart_str *b, const char *s, size_t n, bool attr)
{
    size_t beg = 0, i = 0;
    for (; i < n; i++) {
        const char *rep = NULL;
        size_t consumed = 1;
        const unsigned char *u = (const unsigned char *)s;

        if (i + 2 < n && u[i] == 0xEF && u[i + 1] == 0xBF
            && (u[i + 2] == 0xBE || u[i + 2] == 0xBF)) {
            rep = "\xef\xbf\xbd";
            consumed = 3;
        } else {
            switch (s[i]) {
                case '&': rep = "&amp;"; break;
                case '<': rep = "&lt;"; break;
                case '>': rep = "&gt;"; break;
                case '"': if (attr) rep = "&quot;"; break;
                case '\t': if (attr) rep = "&#x9;"; break;
                case '\n': if (attr) rep = "&#xA;"; break;
                case '\r': if (attr) rep = "&#xD;"; break;
                default:
                    if ((unsigned char)s[i] < 0x20) rep = "\xef\xbf\xbd";
                    break;
            }
        }
        if (rep) {
            if (i > beg) smart_str_appendl(b, s + beg, i - beg);
            smart_str_appends(b, rep);
            beg = i + consumed;
            i += consumed - 1;
        }
    }
    if (i > beg) smart_str_appendl(b, s + beg, i - beg);
}

/* Decode an entity reference into `tmp` (raw UTF-8), then XML-escape it
 * into the output. */
static void mdx_emit_entity(mdx_ctx *c, const char *text, MD_SIZE size)
{
    smart_str tmp = {0};
    mdparser_md4c_decode_entity(&tmp, text, size);
    smart_str_0(&tmp);
    if (tmp.s) mdx_escape(&c->out, ZSTR_VAL(tmp.s), ZSTR_LEN(tmp.s), false);
    smart_str_free(&tmp);
}

static void mdx_open(mdx_ctx *c, const char *tag)
{
    mdx_indent(c);
    smart_str_appendc(&c->out, '<');
    smart_str_appends(&c->out, tag);
    smart_str_appendl(&c->out, ">\n", 2);
    c->depth++;
}

static void mdx_close(mdx_ctx *c, const char *tag)
{
    if (c->depth <= 1) {
        c->error = MDX_ERR_PARSE;
        return;
    }
    c->depth--;
    mdx_indent(c);
    smart_str_appendl(&c->out, "</", 2);
    smart_str_appends(&c->out, tag);
    smart_str_appendl(&c->out, ">\n", 2);
}

static void mdx_attr(mdx_ctx *c, const char *name, const MD_ATTRIBUTE *a)
{
    smart_str_appendc(&c->out, ' ');
    smart_str_appends(&c->out, name);
    X_LIT(c, "=\"");
    /* md4c hands attributes out entity-undecoded across typed substrings;
     * resolve them to raw bytes first, then XML-escape once. Escaping the
     * raw bytes directly would double-encode entities (&amp; -> &amp;amp;). */
    mdparser_md4c_attr_view value;
    mdparser_md4c_attr_view_init(&value, a);
    mdx_escape(&c->out, value.text, value.size, true);
    mdparser_md4c_attr_view_destroy(&value);
    smart_str_appendc(&c->out, '"');
}

static int mdx_enter_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    mdx_ctx *c = userdata;
    /* Depth counts open XML containers, not the AST zval-stack model.
     * XML increments for table_header/table_body/footnote_section and does
     * not for leaf code_block/html_block/thematic_break; AST does the reverse
     * for those. Same Markdown can therefore hit MDPARSER_MAX_AST_DEPTH on
     * only one path. Indentation is capped independently (MDX_MAX_INDENT_DEPTH)
     * so valid near-limit trees remain linear-sized. */
    if (c->depth >= MDPARSER_MAX_AST_DEPTH) { c->error = MDX_ERR_DEPTH; return 1; }
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_QUOTE: mdx_open(c, "block_quote"); break;
        case MD_BLOCK_UL:
            mdx_indent(c);
            X_LIT(c, "<list type=\"bullet\" tight=\"");
            smart_str_appends(&c->out, ((MD_BLOCK_UL_DETAIL *)detail)->is_tight ? "true" : "false");
            X_LIT(c, "\">\n");
            c->depth++;
            break;
        case MD_BLOCK_OL: {
            MD_BLOCK_OL_DETAIL *d = detail;
            mdx_indent(c);
            X_LIT(c, "<list type=\"ordered\" start=\"");
            smart_str_append_unsigned(&c->out, d->start);
            X_LIT(c, "\" delim=\"");
            smart_str_appends(&c->out, d->mark_delimiter == ')' ? "paren" : "period");
            X_LIT(c, "\" tight=\"");
            smart_str_appends(&c->out, d->is_tight ? "true" : "false");
            X_LIT(c, "\">\n");
            c->depth++;
            break;
        }
        case MD_BLOCK_LI: {
            MD_BLOCK_LI_DETAIL *d = detail;
            mdx_indent(c);
            if (d->is_task) {
                X_LIT(c, "<item checked=\"");
                smart_str_appends(&c->out,
                    (d->task_mark == 'x' || d->task_mark == 'X') ? "true" : "false");
                X_LIT(c, "\">\n");
            } else {
                X_LIT(c, "<item>\n");
            }
            c->depth++;
            break;
        }
        case MD_BLOCK_HR: mdx_indent(c); X_LIT(c, "<thematic_break />\n"); break;
        case MD_BLOCK_H: {
            mdx_indent(c);
            X_LIT(c, "<heading level=\"");
            smart_str_append_unsigned(&c->out, ((MD_BLOCK_H_DETAIL *)detail)->level);
            X_LIT(c, "\">\n");
            c->depth++;
            break;
        }
        case MD_BLOCK_CODE: {
            MD_BLOCK_CODE_DETAIL *d = detail;
            mdx_indent(c);
            X_LIT(c, "<code_block");
            if (d->info.text) mdx_attr(c, "info", &d->info);
            X_LIT(c, " xml:space=\"preserve\">");
            c->collecting = true;
            break;
        }
        case MD_BLOCK_HTML:
            mdx_indent(c);
            X_LIT(c, "<html_block xml:space=\"preserve\">");
            c->collecting = true;
            break;
        case MD_BLOCK_P: mdx_open(c, "paragraph"); break;
        case MD_BLOCK_TABLE: mdx_open(c, "table"); break;
        case MD_BLOCK_THEAD: mdx_open(c, "table_header"); break;
        case MD_BLOCK_TBODY: mdx_open(c, "table_body"); break;
        case MD_BLOCK_TR: mdx_open(c, "table_row"); break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            MD_ALIGN al = ((MD_BLOCK_TD_DETAIL *)detail)->align;
            const char *name = al == MD_ALIGN_LEFT ? "left"
                : al == MD_ALIGN_CENTER ? "center"
                : al == MD_ALIGN_RIGHT ? "right" : NULL;
            mdx_indent(c);
            X_LIT(c, "<table_cell");
            if (name) { X_LIT(c, " align=\""); smart_str_appends(&c->out, name); smart_str_appendc(&c->out, '"'); }
            X_LIT(c, ">\n");
            c->depth++;
            break;
        }
        case MD_BLOCK_FOOTNOTE_DEF_SECTION: mdx_open(c, "footnote_section"); break;
        case MD_BLOCK_FOOTNOTE_DEF: {
            mdx_indent(c);
            X_LIT(c, "<footnote_definition id=\"");
            smart_str_append_unsigned(&c->out, ((MD_BLOCK_FOOTNOTE_DEF_DETAIL *)detail)->id);
            X_LIT(c, "\">\n");
            c->depth++;
            break;
        }
        case MD_BLOCK_ADMONITION:
            mdx_indent(c);
            X_LIT(c, "<admonition");
            mdx_attr(c, "type", &((MD_BLOCK_ADMONITION_DETAIL *)detail)->type);
            X_LIT(c, ">\n");
            c->depth++;
            break;
        default: mdx_open(c, "unknown"); break;
    }
    return c->error ? 1 : 0;
}

static int mdx_leave_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    mdx_ctx *c = userdata;
    (void)detail;
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_QUOTE: mdx_close(c, "block_quote"); break;
        case MD_BLOCK_UL:
        case MD_BLOCK_OL: mdx_close(c, "list"); break;
        case MD_BLOCK_LI: mdx_close(c, "item"); break;
        case MD_BLOCK_HR: break;
        case MD_BLOCK_H: mdx_close(c, "heading"); break;
        case MD_BLOCK_CODE:
            X_LIT(c, "</code_block>\n");
            c->collecting = false;
            break;
        case MD_BLOCK_HTML:
            X_LIT(c, "</html_block>\n");
            c->collecting = false;
            break;
        case MD_BLOCK_P: mdx_close(c, "paragraph"); break;
        case MD_BLOCK_TABLE: mdx_close(c, "table"); break;
        case MD_BLOCK_THEAD: mdx_close(c, "table_header"); break;
        case MD_BLOCK_TBODY: mdx_close(c, "table_body"); break;
        case MD_BLOCK_TR: mdx_close(c, "table_row"); break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: mdx_close(c, "table_cell"); break;
        case MD_BLOCK_FOOTNOTE_DEF_SECTION: mdx_close(c, "footnote_section"); break;
        case MD_BLOCK_FOOTNOTE_DEF: mdx_close(c, "footnote_definition"); break;
        case MD_BLOCK_ADMONITION: mdx_close(c, "admonition"); break;
        default: mdx_close(c, "unknown"); break;
    }
    return c->error ? 1 : 0;
}

static int mdx_enter_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    mdx_ctx *c = userdata;
    if (c->depth >= MDPARSER_MAX_AST_DEPTH) { c->error = MDX_ERR_DEPTH; return 1; }
    switch (type) {
        case MD_SPAN_EM: mdx_open(c, "emph"); break;
        case MD_SPAN_STRONG: mdx_open(c, "strong"); break;
        case MD_SPAN_A: {
            MD_SPAN_A_DETAIL *d = detail;
            mdx_indent(c);
            X_LIT(c, "<link");
            mdx_attr(c, "destination", &d->href);
            mdx_attr(c, "title", &d->title);
            X_LIT(c, ">\n");
            c->depth++;
            break;
        }
        case MD_SPAN_IMG: {
            MD_SPAN_IMG_DETAIL *d = detail;
            mdx_indent(c);
            X_LIT(c, "<image");
            mdx_attr(c, "destination", &d->src);
            mdx_attr(c, "title", &d->title);
            X_LIT(c, ">\n");
            c->depth++;
            break;
        }
        case MD_SPAN_CODE:
            mdx_indent(c);
            X_LIT(c, "<code xml:space=\"preserve\">");
            c->collecting = true;
            break;
        case MD_SPAN_DEL: mdx_open(c, "strikethrough"); break;
        case MD_SPAN_U: mdx_open(c, "underline"); break;
        case MD_SPAN_SUPERSCRIPT: mdx_open(c, "superscript"); break;
        case MD_SPAN_SUBSCRIPT: mdx_open(c, "subscript"); break;
        case MD_SPAN_MARK: mdx_open(c, "highlight"); break;
        case MD_SPAN_SPOILER: mdx_open(c, "spoiler"); break;
        case MD_SPAN_LATEXMATH: mdx_open(c, "latex_math"); break;
        case MD_SPAN_LATEXMATH_DISPLAY: mdx_open(c, "latex_math_display"); break;
        case MD_SPAN_WIKILINK: {
            MD_SPAN_WIKILINK_DETAIL *d = detail;
            mdx_indent(c);
            X_LIT(c, "<wikilink");
            mdx_attr(c, "destination", &d->target);
            X_LIT(c, ">\n");
            c->depth++;
            break;
        }
        case MD_SPAN_FOOTNOTE_REF: {
            mdx_indent(c);
            X_LIT(c, "<footnote_reference id=\"");
            smart_str_append_unsigned(&c->out, ((MD_SPAN_FOOTNOTE_REF_DETAIL *)detail)->id);
            X_LIT(c, "\">\n");
            c->depth++;
            break;
        }
        default: mdx_open(c, "unknown"); break;
    }
    return 0;
}

static int mdx_leave_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    mdx_ctx *c = userdata;
    (void)detail;
    switch (type) {
        case MD_SPAN_EM: mdx_close(c, "emph"); break;
        case MD_SPAN_STRONG: mdx_close(c, "strong"); break;
        case MD_SPAN_A: mdx_close(c, "link"); break;
        case MD_SPAN_IMG: mdx_close(c, "image"); break;
        case MD_SPAN_CODE:
            X_LIT(c, "</code>\n");
            c->collecting = false;
            break;
        case MD_SPAN_DEL: mdx_close(c, "strikethrough"); break;
        case MD_SPAN_U: mdx_close(c, "underline"); break;
        case MD_SPAN_SUPERSCRIPT: mdx_close(c, "superscript"); break;
        case MD_SPAN_SUBSCRIPT: mdx_close(c, "subscript"); break;
        case MD_SPAN_MARK: mdx_close(c, "highlight"); break;
        case MD_SPAN_SPOILER: mdx_close(c, "spoiler"); break;
        case MD_SPAN_LATEXMATH: mdx_close(c, "latex_math"); break;
        case MD_SPAN_LATEXMATH_DISPLAY: mdx_close(c, "latex_math_display"); break;
        case MD_SPAN_WIKILINK: mdx_close(c, "wikilink"); break;
        case MD_SPAN_FOOTNOTE_REF: mdx_close(c, "footnote_reference"); break;
        default: mdx_close(c, "unknown"); break;
    }
    return c->error ? 1 : 0;
}

static int mdx_text(MD_TEXTTYPE type, const char *text, MD_SIZE size, void *userdata)
{
    mdx_ctx *c = userdata;
    if (c->collecting) {
        mdx_escape(&c->out, text, size, false);
        return 0;
    }
    switch (type) {
        case MD_TEXT_SOFTBR: mdx_indent(c); X_LIT(c, "<softbreak />\n"); break;
        case MD_TEXT_BR: mdx_indent(c); X_LIT(c, "<linebreak />\n"); break;
        case MD_TEXT_HTML:
            mdx_indent(c);
            X_LIT(c, "<html_inline xml:space=\"preserve\">");
            mdx_escape(&c->out, text, size, false);
            X_LIT(c, "</html_inline>\n");
            break;
        case MD_TEXT_ENTITY:
            mdx_indent(c);
            X_LIT(c, "<text xml:space=\"preserve\">");
            mdx_emit_entity(c, text, size);
            X_LIT(c, "</text>\n");
            break;
        case MD_TEXT_NULLCHAR:
            mdx_indent(c);
            X_LIT(c, "<text xml:space=\"preserve\">\xef\xbf\xbd</text>\n");
            break;
        default:
            mdx_indent(c);
            X_LIT(c, "<text xml:space=\"preserve\">");
            mdx_escape(&c->out, text, size, false);
            X_LIT(c, "</text>\n");
            break;
    }
    return 0;
}

zend_string *mdparser_md4c_render_xml(const char *src, size_t len,
    unsigned parser_flags, bool validate_utf8, int *status)
{
    mdx_ctx c;
    memset(&c, 0, sizeof(c));

    X_LIT(&c, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    X_LIT(&c, "<!DOCTYPE document SYSTEM \"CommonMark.dtd\">\n");
    X_LIT(&c, "<document xmlns=\"http://commonmark.org/xml/1.0\">\n");
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
    if (validate_utf8)
        use_src = mdparser_md4c_validate_utf8(use_src, use_len, &use_len, &owned);

    /* Reserve about twice the input size up to 1 MiB. This covers ordinary
     * markup-heavy output without amplifying sparse large inputs. */
    if (use_len) {
        size_t reserve = use_len <= MDPARSER_INITIAL_OUTPUT_RESERVE_MAX / 2
            ? use_len * 2 : MDPARSER_INITIAL_OUTPUT_RESERVE_MAX;
        smart_str_alloc(&c.out, reserve, 0);
    }

    bool bailed_out;
    int rc = mdparser_md4c_parse(use_src, (MD_SIZE)use_len, &parser, &c,
        &bailed_out);
    if (owned) efree((void *)use_src);

    if (bailed_out) {
        smart_str_free(&c.out);
        zend_bailout();
    }

    if (c.error != 0 || rc != 0 || c.depth != 1) {
        if (c.error == 0) {
            c.error = MDX_ERR_PARSE;
        }
        smart_str_free(&c.out);
        *status = c.error;
        return NULL;
    }

    c.depth = 0;
    X_LIT(&c, "</document>\n");
    *status = MDX_OK;
    smart_str_0(&c.out);
    if (!c.out.s) return ZSTR_EMPTY_ALLOC();
    return c.out.s;
}
