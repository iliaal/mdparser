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
#include "php_mdparser.h"
#include "mdparser_md4c_util.h"
#include "mdparser_md4c_ast.h"

/* Stack-based md4c -> PHP-array AST builder. Mirrors the legacy cmark AST
 * shape (type names + per-node fields) MINUS source positions, which md4c
 * does not expose. Each open block/span is a node array on `stack`; on
 * leave the node is appended to its parent's "children". Leaf literals
 * (code/code_block/html) accumulate from text callbacks into `litbuf`. */

#define MDA_OK         0
#define MDA_ERR_PARSE  1
#define MDA_ERR_DEPTH  2

const char *mdparser_md4c_ast_status_message(int status)
{
    switch (status) {
        case MDA_ERR_PARSE: return "mdparser: md4c parser failed";
        case MDA_ERR_DEPTH: return "mdparser: AST nesting exceeds maximum depth";
        default: return NULL;
    }
}

typedef struct {
    zval stack[MDPARSER_MAX_AST_DEPTH];
    int depth;                 /* index of current open node */
    int in_thead;              /* >0 while inside MD_BLOCK_THEAD */
    int th_col;                /* column index within the header row */
    bool collecting;           /* accumulating a leaf literal */
    smart_str litbuf;
    int error;
} mda_ctx;

/* Nearest enclosing table node's "alignments" array, or NULL. */
static zval *mda_table_alignments(mda_ctx *c)
{
    for (int i = c->depth; i >= 0; i--) {
        zval *t = zend_hash_str_find(Z_ARRVAL(c->stack[i]), "type", sizeof("type") - 1);
        if (t && Z_TYPE_P(t) == IS_STRING && strcmp(Z_STRVAL_P(t), "table") == 0)
            return zend_hash_str_find(Z_ARRVAL(c->stack[i]), "alignments", sizeof("alignments") - 1);
    }
    return NULL;
}

/* ---- node helpers ---------------------------------------------------- */

static zval *mda_top(mda_ctx *c) { return &c->stack[c->depth]; }

/* Append `child` (ownership transferred) to the current top node's
 * "children" array, creating it on demand. */
static void mda_append_child(mda_ctx *c, zval *child)
{
    zval *parent = mda_top(c);
    zval *kids = zend_hash_str_find(Z_ARRVAL_P(parent), "children", sizeof("children") - 1);
    if (!kids) {
        zval arr;
        array_init(&arr);
        kids = zend_hash_str_add_new(Z_ARRVAL_P(parent), "children", sizeof("children") - 1, &arr);
    }
    add_next_index_zval(kids, child);
}

/* Create a node array with "type" set; returns it by value in *out. */
static void mda_new_node(zval *out, const char *type)
{
    array_init(out);
    add_assoc_string(out, "type", type);
}

/* Push a freshly-created container node. Returns false on depth overflow. */
static bool mda_push(mda_ctx *c, zval *node)
{
    if (c->depth + 1 >= MDPARSER_MAX_AST_DEPTH) {
        zval_ptr_dtor(node);
        c->error = MDA_ERR_DEPTH;
        return false;
    }
    c->stack[++c->depth] = *node;
    return true;
}

/* Pop the top node and append it to its parent. */
static void mda_pop(mda_ctx *c)
{
    zval node = c->stack[c->depth];
    ZVAL_UNDEF(&c->stack[c->depth]);
    c->depth--;
    mda_append_child(c, &node);
}

/* ---- entity decoding for text leaves --------------------------------- */

static void mda_append_cp(smart_str *b, unsigned cp)
{
    if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        smart_str_appendl(b, "\xef\xbf\xbd", 3);
        return;
    }
    if (cp <= 0x7f) { smart_str_appendc(b, (char)cp); }
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

static void mda_append_entity(smart_str *b, const char *text, MD_SIZE size)
{
    if (size > 3 && text[1] == '#') {
        unsigned cp = 0;
        if (text[2] == 'x' || text[2] == 'X') {
            for (MD_SIZE i = 3; i < size - 1; i++) {
                char ch = text[i];
                cp = cp * 16 + (ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10);
            }
        } else {
            for (MD_SIZE i = 2; i < size - 1; i++) cp = cp * 10 + (text[i] - '0');
        }
        mda_append_cp(b, cp);
        return;
    }
    const ENTITY *e = entity_lookup(text, size);
    if (e) {
        mda_append_cp(b, e->codepoints[0]);
        if (e->codepoints[1]) mda_append_cp(b, e->codepoints[1]);
    } else {
        smart_str_appendl(b, text, size);
    }
}

/* ---- callbacks ------------------------------------------------------- */

static const char *mda_align_name(MD_ALIGN a)
{
    switch (a) {
        case MD_ALIGN_LEFT: return "left";
        case MD_ALIGN_CENTER: return "center";
        case MD_ALIGN_RIGHT: return "right";
        default: return "none";
    }
}

static int mda_enter_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    mda_ctx *c = userdata;
    zval n;

    switch (type) {
        case MD_BLOCK_DOC:
            /* document is stack[0], created in the entry function. */
            return 0;
        case MD_BLOCK_QUOTE: mda_new_node(&n, "block_quote"); break;
        case MD_BLOCK_UL: {
            mda_new_node(&n, "list");
            add_assoc_string(&n, "list_type", "bullet");
            add_assoc_long(&n, "list_start", 0);
            add_assoc_bool(&n, "list_tight", ((MD_BLOCK_UL_DETAIL *)detail)->is_tight);
            add_assoc_string(&n, "list_delim", "none");
            break;
        }
        case MD_BLOCK_OL: {
            MD_BLOCK_OL_DETAIL *d = detail;
            mda_new_node(&n, "list");
            add_assoc_string(&n, "list_type", "ordered");
            add_assoc_long(&n, "list_start", d->start);
            add_assoc_bool(&n, "list_tight", d->is_tight);
            add_assoc_string(&n, "list_delim", d->mark_delimiter == ')' ? "paren" : "period");
            break;
        }
        case MD_BLOCK_LI: {
            MD_BLOCK_LI_DETAIL *d = detail;
            if (d->is_task) {
                mda_new_node(&n, "tasklist");
                add_assoc_bool(&n, "checked", d->task_mark == 'x' || d->task_mark == 'X');
            } else {
                mda_new_node(&n, "item");
            }
            break;
        }
        case MD_BLOCK_HR: mda_new_node(&n, "thematic_break"); break;
        case MD_BLOCK_H:
            mda_new_node(&n, "heading");
            add_assoc_long(&n, "level", ((MD_BLOCK_H_DETAIL *)detail)->level);
            break;
        case MD_BLOCK_CODE: {
            MD_BLOCK_CODE_DETAIL *d = detail;
            mda_new_node(&n, "code_block");
            if (d->lang.text)
                add_assoc_stringl(&n, "info", d->lang.text, d->lang.size);
            else
                add_assoc_string(&n, "info", "");
            c->collecting = true;
            smart_str_free(&c->litbuf);
            break;
        }
        case MD_BLOCK_HTML:
            mda_new_node(&n, "html_block");
            c->collecting = true;
            smart_str_free(&c->litbuf);
            break;
        case MD_BLOCK_P: mda_new_node(&n, "paragraph"); break;
        case MD_BLOCK_TABLE: {
            mda_new_node(&n, "table");
            zval aligns;
            array_init(&aligns);
            for (unsigned i = 0; i < ((MD_BLOCK_TABLE_DETAIL *)detail)->col_count; i++)
                add_next_index_string(&aligns, "none"); /* refined from header cells */
            add_assoc_zval(&n, "alignments", &aligns);
            break;
        }
        case MD_BLOCK_THEAD: c->in_thead++; return 0;  /* structural; flattened */
        case MD_BLOCK_TBODY: return 0;
        case MD_BLOCK_TR:
            if (c->in_thead) c->th_col = 0;
            mda_new_node(&n, c->in_thead ? "table_header" : "table_row");
            add_assoc_bool(&n, "is_header", c->in_thead ? 1 : 0);
            break;
        case MD_BLOCK_TH: {
            /* Header cells define the per-column alignment for the table. */
            zval *aligns = mda_table_alignments(c);
            if (aligns) {
                zval av;
                ZVAL_STRING(&av, mda_align_name(((MD_BLOCK_TD_DETAIL *)detail)->align));
                zend_hash_index_update(Z_ARRVAL_P(aligns), c->th_col, &av);
            }
            c->th_col++;
            mda_new_node(&n, "table_cell");
            break;
        }
        case MD_BLOCK_TD:
            mda_new_node(&n, "table_cell");
            break;
        case MD_BLOCK_FOOTNOTE_DEF: {
            MD_BLOCK_FOOTNOTE_DEF_DETAIL *d = detail;
            mda_new_node(&n, "footnote_definition");
            char buf[16];
            int w = snprintf(buf, sizeof(buf), "%u", d->id);
            add_assoc_stringl(&n, "literal", buf, (size_t)w);
            break;
        }
        case MD_BLOCK_FOOTNOTE_DEF_SECTION: return 0;  /* structural */
        default:
            mda_new_node(&n, "unknown");
            break;
    }
    if (!mda_push(c, &n)) return 1;
    return 0;
}

static int mda_leave_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    mda_ctx *c = userdata;
    (void)detail;
    switch (type) {
        case MD_BLOCK_DOC: return 0;
        case MD_BLOCK_THEAD: c->in_thead--; return 0;
        case MD_BLOCK_TBODY:
        case MD_BLOCK_FOOTNOTE_DEF_SECTION: return 0;
        case MD_BLOCK_CODE:
        case MD_BLOCK_HTML:
            smart_str_0(&c->litbuf);
            add_assoc_stringl(mda_top(c), "literal",
                c->litbuf.s ? ZSTR_VAL(c->litbuf.s) : "",
                c->litbuf.s ? ZSTR_LEN(c->litbuf.s) : 0);
            smart_str_free(&c->litbuf);
            c->collecting = false;
            mda_pop(c);
            return 0;
        default:
            mda_pop(c);
            return 0;
    }
}

static int mda_enter_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    mda_ctx *c = userdata;
    zval n;
    switch (type) {
        case MD_SPAN_EM: mda_new_node(&n, "emph"); break;
        case MD_SPAN_STRONG: mda_new_node(&n, "strong"); break;
        case MD_SPAN_A: {
            MD_SPAN_A_DETAIL *d = detail;
            mda_new_node(&n, "link");
            add_assoc_stringl(&n, "url", d->href.text ? d->href.text : "", d->href.size);
            add_assoc_stringl(&n, "title", d->title.text ? d->title.text : "", d->title.size);
            break;
        }
        case MD_SPAN_IMG: {
            MD_SPAN_IMG_DETAIL *d = detail;
            mda_new_node(&n, "image");
            add_assoc_stringl(&n, "url", d->src.text ? d->src.text : "", d->src.size);
            add_assoc_stringl(&n, "title", d->title.text ? d->title.text : "", d->title.size);
            break;
        }
        case MD_SPAN_CODE:
            mda_new_node(&n, "code");
            c->collecting = true;
            smart_str_free(&c->litbuf);
            break;
        case MD_SPAN_DEL: mda_new_node(&n, "strikethrough"); break;
        case MD_SPAN_U: mda_new_node(&n, "underline"); break;
        case MD_SPAN_SUPERSCRIPT: mda_new_node(&n, "superscript"); break;
        case MD_SPAN_SUBSCRIPT: mda_new_node(&n, "subscript"); break;
        case MD_SPAN_MARK: mda_new_node(&n, "highlight"); break;
        case MD_SPAN_FOOTNOTE_REF: {
            MD_SPAN_FOOTNOTE_REF_DETAIL *d = detail;
            mda_new_node(&n, "footnote_reference");
            char buf[16];
            int w = snprintf(buf, sizeof(buf), "%u", d->id);
            add_assoc_stringl(&n, "literal", buf, (size_t)w);
            break;
        }
        default: mda_new_node(&n, "unknown"); break;
    }
    if (!mda_push(c, &n)) return 1;
    return 0;
}

static int mda_leave_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    mda_ctx *c = userdata;
    (void)detail;
    if (type == MD_SPAN_CODE) {
        smart_str_0(&c->litbuf);
        add_assoc_stringl(mda_top(c), "literal",
            c->litbuf.s ? ZSTR_VAL(c->litbuf.s) : "",
            c->litbuf.s ? ZSTR_LEN(c->litbuf.s) : 0);
        smart_str_free(&c->litbuf);
        c->collecting = false;
    }
    mda_pop(c);
    return 0;
}

static int mda_text(MD_TEXTTYPE type, const char *text, MD_SIZE size, void *userdata)
{
    mda_ctx *c = userdata;

    if (c->collecting) {
        /* code/html literal: verbatim bytes (entities not decoded). */
        smart_str_appendl(&c->litbuf, text, size);
        return 0;
    }

    zval n;
    switch (type) {
        case MD_TEXT_SOFTBR: mda_new_node(&n, "softbreak"); break;
        case MD_TEXT_BR: mda_new_node(&n, "linebreak"); break;
        case MD_TEXT_HTML:
            mda_new_node(&n, "html_inline");
            add_assoc_stringl(&n, "literal", text, size);
            break;
        case MD_TEXT_ENTITY: {
            smart_str b = {0};
            mda_append_entity(&b, text, size);
            smart_str_0(&b);
            mda_new_node(&n, "text");
            add_assoc_stringl(&n, "literal", b.s ? ZSTR_VAL(b.s) : "", b.s ? ZSTR_LEN(b.s) : 0);
            smart_str_free(&b);
            break;
        }
        case MD_TEXT_NULLCHAR:
            mda_new_node(&n, "text");
            add_assoc_stringl(&n, "literal", "\xef\xbf\xbd", 3);
            break;
        default:  /* MD_TEXT_NORMAL / MD_TEXT_CODE outside a collector */
            mda_new_node(&n, "text");
            add_assoc_stringl(&n, "literal", text, size);
            break;
    }
    mda_append_child(c, &n);
    return 0;
}

void mdparser_md4c_render_ast(const char *src, size_t len, unsigned parser_flags,
    bool validate_utf8, zval *return_value, int *status)
{
    mda_ctx c;
    memset(&c, 0, sizeof(c));
    c.depth = 0;
    c.error = 0;

    /* stack[0] is the document root. */
    mda_new_node(&c.stack[0], "document");

    MD_PARSER parser = {
        0, parser_flags,
        mda_enter_block, mda_leave_block,
        mda_enter_span, mda_leave_span,
        mda_text, NULL, NULL
    };

    /* validateUtf8: invalid input bytes -> U+FFFD before parsing, so AST text
     * literals match the HTML path (parity with cmark's CMARK_OPT_VALIDATE_UTF8). */
    bool owned = false;
    size_t use_len = len;
    const char *use_src = src;
    if (validate_utf8)
        use_src = mdparser_md4c_validate_utf8(src, len, &use_len, &owned);

    int rc = md_parse(use_src, (MD_SIZE)use_len, &parser, &c);

    if (owned) efree((void *)use_src);
    smart_str_free(&c.litbuf);

    if (c.error == MDA_ERR_DEPTH || rc != 0) {
        /* Free the whole partial tree (stack[0] plus any still-open nodes). */
        for (int i = 0; i <= c.depth; i++) {
            if (Z_TYPE(c.stack[i]) != IS_UNDEF) zval_ptr_dtor(&c.stack[i]);
        }
        *status = c.error ? c.error : MDA_ERR_PARSE;
        return;
    }

    *status = MDA_OK;
    ZVAL_COPY_VALUE(return_value, &c.stack[0]);
}
