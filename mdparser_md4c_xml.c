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
#include "mdparser_md4c_util.h"
#include "mdparser_md4c_xml.h"

/* Streaming md4c -> CommonMark-XML emitter. Mirrors cmark's XML layout
 * (2-space indent, xml:space="preserve" on text/code/html) over the same
 * node names the AST builder uses. Source positions are not emitted. */

#define MDX_OK        0
#define MDX_ERR_PARSE 1

const char *mdparser_md4c_xml_status_message(int status)
{
    return status == MDX_ERR_PARSE ? "mdparser: md4c parser failed" : NULL;
}

typedef struct {
    smart_str out;
    int depth;
    int in_thead;
    bool collecting;   /* code/code_block/html literal */
    smart_str lit;
} mdx_ctx;

static void mdx_indent(mdx_ctx *c)
{
    for (int i = 0; i < c->depth; i++) smart_str_appendl(&c->out, "  ", 2);
}

#define X_LIT(c, lit) smart_str_appendl(&(c)->out, "" lit, sizeof(lit) - 1)

/* XML-escape into the output buffer (& < > and, for attrs, "). */
static void mdx_escape(smart_str *b, const char *s, size_t n, bool attr)
{
    size_t beg = 0, i = 0;
    for (; i < n; i++) {
        const char *rep = NULL;
        switch (s[i]) {
            case '&': rep = "&amp;"; break;
            case '<': rep = "&lt;"; break;
            case '>': rep = "&gt;"; break;
            case '"': if (attr) rep = "&quot;"; break;
            default: break;
        }
        if (rep) {
            if (i > beg) smart_str_appendl(b, s + beg, i - beg);
            smart_str_appends(b, rep);
            beg = i + 1;
        }
    }
    if (i > beg) smart_str_appendl(b, s + beg, i - beg);
}

static void mdx_append_cp(smart_str *b, unsigned cp)
{
    if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        smart_str_appendl(b, "\xef\xbf\xbd", 3);
        return;
    }
    if (cp <= 0x7f) smart_str_appendc(b, (char)cp);
    else if (cp <= 0x7ff) {
        smart_str_appendc(b, (char)(0xc0 | (cp >> 6)));
        smart_str_appendc(b, (char)(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        smart_str_appendc(b, (char)(0xe0 | (cp >> 12)));
        smart_str_appendc(b, (char)(0x80 | ((cp >> 6) & 0x3f)));
        smart_str_appendc(b, (char)(0x80 | (cp & 0x3f)));
    } else {
        smart_str_appendc(b, (char)(0xf0 | (cp >> 18)));
        smart_str_appendc(b, (char)(0x80 | ((cp >> 12) & 0x3f)));
        smart_str_appendc(b, (char)(0x80 | ((cp >> 6) & 0x3f)));
        smart_str_appendc(b, (char)(0x80 | (cp & 0x3f)));
    }
}

/* Decode an entity reference into `tmp` (raw UTF-8), then XML-escape it
 * into the output. */
static void mdx_emit_entity(mdx_ctx *c, const char *text, MD_SIZE size)
{
    smart_str tmp = {0};
    if (size > 3 && text[1] == '#') {
        unsigned cp = 0;
        if (text[2] == 'x' || text[2] == 'X')
            for (MD_SIZE i = 3; i < size - 1; i++) {
                char ch = text[i];
                cp = cp * 16 + (ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10);
            }
        else
            for (MD_SIZE i = 2; i < size - 1; i++) cp = cp * 10 + (text[i] - '0');
        mdx_append_cp(&tmp, cp);
    } else {
        const ENTITY *e = entity_lookup(text, size);
        if (e) {
            mdx_append_cp(&tmp, e->codepoints[0]);
            if (e->codepoints[1]) mdx_append_cp(&tmp, e->codepoints[1]);
        } else {
            smart_str_appendl(&tmp, text, size);
        }
    }
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
    if (a && a->text) mdx_escape(&c->out, a->text, a->size, true);
    smart_str_appendc(&c->out, '"');
}

static int mdx_enter_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    mdx_ctx *c = userdata;
    char buf[64];
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
            int w = snprintf(buf, sizeof(buf),
                "<list type=\"ordered\" start=\"%u\" delim=\"%s\" tight=\"%s\">\n",
                d->start, d->mark_delimiter == ')' ? "paren" : "period",
                d->is_tight ? "true" : "false");
            smart_str_appendl(&c->out, buf, (size_t)w);
            c->depth++;
            break;
        }
        case MD_BLOCK_LI: mdx_open(c, "item"); break;
        case MD_BLOCK_HR: mdx_indent(c); X_LIT(c, "<thematic_break />\n"); break;
        case MD_BLOCK_H: {
            int w = snprintf(buf, sizeof(buf), "<heading level=\"%u\">\n",
                ((MD_BLOCK_H_DETAIL *)detail)->level);
            mdx_indent(c);
            smart_str_appendl(&c->out, buf, (size_t)w);
            c->depth++;
            break;
        }
        case MD_BLOCK_CODE: {
            MD_BLOCK_CODE_DETAIL *d = detail;
            mdx_indent(c);
            X_LIT(c, "<code_block");
            if (d->lang.text) mdx_attr(c, "info", &d->lang);
            X_LIT(c, " xml:space=\"preserve\">");
            c->collecting = true;
            smart_str_free(&c->lit);
            break;
        }
        case MD_BLOCK_HTML:
            mdx_indent(c);
            X_LIT(c, "<html_block xml:space=\"preserve\">");
            c->collecting = true;
            smart_str_free(&c->lit);
            break;
        case MD_BLOCK_P: mdx_open(c, "paragraph"); break;
        case MD_BLOCK_TABLE: mdx_open(c, "table"); break;
        case MD_BLOCK_THEAD: c->in_thead++; mdx_open(c, "table_header"); break;
        case MD_BLOCK_TBODY: mdx_open(c, "table_body"); break;
        case MD_BLOCK_TR: mdx_open(c, "table_row"); break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: mdx_open(c, "table_cell"); break;
        case MD_BLOCK_FOOTNOTE_DEF_SECTION: mdx_open(c, "footnote_section"); break;
        case MD_BLOCK_FOOTNOTE_DEF: mdx_open(c, "footnote_definition"); break;
        default: mdx_open(c, "unknown"); break;
    }
    return 0;
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
            smart_str_0(&c->lit);
            if (c->lit.s) mdx_escape(&c->out, ZSTR_VAL(c->lit.s), ZSTR_LEN(c->lit.s), false);
            X_LIT(c, "</code_block>\n");
            smart_str_free(&c->lit);
            c->collecting = false;
            break;
        case MD_BLOCK_HTML:
            smart_str_0(&c->lit);
            if (c->lit.s) mdx_escape(&c->out, ZSTR_VAL(c->lit.s), ZSTR_LEN(c->lit.s), false);
            X_LIT(c, "</html_block>\n");
            smart_str_free(&c->lit);
            c->collecting = false;
            break;
        case MD_BLOCK_P: mdx_close(c, "paragraph"); break;
        case MD_BLOCK_TABLE: mdx_close(c, "table"); break;
        case MD_BLOCK_THEAD: c->in_thead--; mdx_close(c, "table_header"); break;
        case MD_BLOCK_TBODY: mdx_close(c, "table_body"); break;
        case MD_BLOCK_TR: mdx_close(c, "table_row"); break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: mdx_close(c, "table_cell"); break;
        case MD_BLOCK_FOOTNOTE_DEF_SECTION: mdx_close(c, "footnote_section"); break;
        case MD_BLOCK_FOOTNOTE_DEF: mdx_close(c, "footnote_definition"); break;
        default: mdx_close(c, "unknown"); break;
    }
    return 0;
}

static int mdx_enter_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    mdx_ctx *c = userdata;
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
            smart_str_free(&c->lit);
            break;
        case MD_SPAN_DEL: mdx_open(c, "strikethrough"); break;
        case MD_SPAN_U: mdx_open(c, "underline"); break;
        case MD_SPAN_SUPERSCRIPT: mdx_open(c, "superscript"); break;
        case MD_SPAN_SUBSCRIPT: mdx_open(c, "subscript"); break;
        case MD_SPAN_MARK: mdx_open(c, "highlight"); break;
        case MD_SPAN_FOOTNOTE_REF: mdx_open(c, "footnote_reference"); break;
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
            smart_str_0(&c->lit);
            if (c->lit.s) mdx_escape(&c->out, ZSTR_VAL(c->lit.s), ZSTR_LEN(c->lit.s), false);
            X_LIT(c, "</code>\n");
            smart_str_free(&c->lit);
            c->collecting = false;
            break;
        case MD_SPAN_DEL: mdx_close(c, "strikethrough"); break;
        case MD_SPAN_U: mdx_close(c, "underline"); break;
        case MD_SPAN_SUPERSCRIPT: mdx_close(c, "superscript"); break;
        case MD_SPAN_SUBSCRIPT: mdx_close(c, "subscript"); break;
        case MD_SPAN_MARK: mdx_close(c, "highlight"); break;
        case MD_SPAN_FOOTNOTE_REF: mdx_close(c, "footnote_reference"); break;
        default: mdx_close(c, "unknown"); break;
    }
    return 0;
}

static int mdx_text(MD_TEXTTYPE type, const char *text, MD_SIZE size, void *userdata)
{
    mdx_ctx *c = userdata;
    if (c->collecting) {
        smart_str_appendl(&c->lit, text, size);
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
    if (validate_utf8)
        use_src = mdparser_md4c_validate_utf8(src, len, &use_len, &owned);

    int rc = md_parse(use_src, (MD_SIZE)use_len, &parser, &c);
    smart_str_free(&c.lit);
    if (owned) efree((void *)use_src);

    if (rc != 0) {
        smart_str_free(&c.out);
        *status = MDX_ERR_PARSE;
        return NULL;
    }

    c.depth = 0;
    X_LIT(&c, "</document>\n");
    *status = MDX_OK;
    smart_str_0(&c.out);
    if (!c.out.s) return ZSTR_EMPTY_ALLOC();
    return c.out.s;
}
