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
#include "mdparser_md4c_ast.h"

/* Stack-based md4c -> PHP-array AST builder. Node type names and per-node
 * fields are documented in docs/ast.md; there are no source positions (md4c
 * does not expose any). Each open block/span is a node array on `stack`; on
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
        default: return "mdparser: unknown error";
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

/* Interned keys for the two hot per-node inserts ("type" on every node,
 * "children" on every container) and the interned node-type *values*. All
 * created once at MINIT and reused, so the builder neither re-allocates nor
 * re-hashes (nor re-interns) them per node. The node-type set is defined once
 * here; the enum, the interned-string table, and the MINIT interning are all
 * generated from it, so adding a node type is a one-line change. */
#define MDA_NODE_TYPES(_) \
    _(document) _(block_quote) _(list) _(tasklist) _(item) \
    _(thematic_break) _(heading) _(code_block) _(html_block) _(paragraph) \
    _(table) _(table_header) _(table_row) _(table_cell) _(footnote_definition) \
    _(admonition) \
    _(emph) _(strong) _(link) _(image) _(code) \
    _(strikethrough) _(underline) _(superscript) _(subscript) _(highlight) \
    _(spoiler) _(latex_math) _(latex_math_display) _(wikilink) _(footnote_reference) \
    _(softbreak) _(linebreak) _(html_inline) _(text) _(unknown)

enum {
#define MDA_T_ENUM(id) MDA_T_##id,
    MDA_NODE_TYPES(MDA_T_ENUM)
#undef MDA_T_ENUM
    MDA_T__COUNT
};

static zend_string *mda_k_type;
static zend_string *mda_k_children;
static zend_string *mda_types[MDA_T__COUNT];
static zend_string *mda_s_list_bullet;
static zend_string *mda_s_list_ordered;
static zend_string *mda_s_delim_none;
static zend_string *mda_s_delim_period;
static zend_string *mda_s_delim_paren;
static zend_string *mda_s_align_left;
static zend_string *mda_s_align_center;
static zend_string *mda_s_align_right;
static zend_string *mda_s_align_none;

void mdparser_md4c_ast_minit(void)
{
    mda_k_type = zend_string_init_interned("type", sizeof("type") - 1, 1);
    mda_k_children = zend_string_init_interned("children", sizeof("children") - 1, 1);
#define MDA_T_INIT(id) mda_types[MDA_T_##id] = zend_string_init_interned(#id, sizeof(#id) - 1, 1);
    MDA_NODE_TYPES(MDA_T_INIT)
#undef MDA_T_INIT
    mda_s_list_bullet = zend_string_init_interned("bullet", sizeof("bullet") - 1, 1);
    mda_s_list_ordered = zend_string_init_interned("ordered", sizeof("ordered") - 1, 1);
    mda_s_delim_none = zend_string_init_interned("none", sizeof("none") - 1, 1);
    mda_s_delim_period = zend_string_init_interned("period", sizeof("period") - 1, 1);
    mda_s_delim_paren = zend_string_init_interned("paren", sizeof("paren") - 1, 1);
    mda_s_align_left = zend_string_init_interned("left", sizeof("left") - 1, 1);
    mda_s_align_center = zend_string_init_interned("center", sizeof("center") - 1, 1);
    mda_s_align_right = zend_string_init_interned("right", sizeof("right") - 1, 1);
    mda_s_align_none = zend_string_init_interned("none", sizeof("none") - 1, 1);
}

/* ---- node helpers ---------------------------------------------------- */

static zval *mda_top(mda_ctx *c) { return &c->stack[c->depth]; }

/* Append `child` (ownership transferred) to the current top node's
 * "children" array, creating it on demand. */
static void mda_append_child(mda_ctx *c, zval *child)
{
    zval *parent = mda_top(c);
    zval *kids = zend_hash_find(Z_ARRVAL_P(parent), mda_k_children);
    if (!kids) {
        zval arr;
        array_init(&arr);
        kids = zend_hash_add_new(Z_ARRVAL_P(parent), mda_k_children, &arr);
    }
    add_next_index_zval(kids, child);
}

/* Create a node array with "type" set; returns it by value in *out. */
static void mda_new_node(zval *out, int type)
{
    array_init(out);
    zval v;
    ZVAL_STR(&v, mda_types[type]);
    zend_hash_add_new(Z_ARRVAL_P(out), mda_k_type, &v);
}

static zval *mda_last_text_literal(mda_ctx *c)
{
    zval *children = zend_hash_find(Z_ARRVAL_P(mda_top(c)), mda_k_children);
    zval *last;
    zval *type;

    if (!children || zend_hash_num_elements(Z_ARRVAL_P(children)) == 0) {
        return NULL;
    }
    last = zend_hash_index_find(Z_ARRVAL_P(children),
        zend_hash_num_elements(Z_ARRVAL_P(children)) - 1);
    if (!last || Z_TYPE_P(last) != IS_ARRAY) {
        return NULL;
    }
    type = zend_hash_find(Z_ARRVAL_P(last), mda_k_type);
    if (!type || Z_TYPE_P(type) != IS_STRING
        || Z_STR_P(type) != mda_types[MDA_T_text]) {
        return NULL;
    }
    return zend_hash_str_find(Z_ARRVAL_P(last), "literal", sizeof("literal") - 1);
}

static void mda_append_text_raw(mda_ctx *c, const char *text, size_t size)
{
    zval *literal;
    zval node;

    if (size == 0) {
        return;
    }
    literal = mda_last_text_literal(c);
    if (literal && Z_TYPE_P(literal) == IS_STRING) {
        size_t old_size = Z_STRLEN_P(literal);
        zend_string *joined = zend_string_extend(Z_STR_P(literal), old_size + size, 0);
        memcpy(ZSTR_VAL(joined) + old_size, text, size);
        ZSTR_VAL(joined)[old_size + size] = '\0';
        Z_STR_P(literal) = joined;
        return;
    }

    mda_new_node(&node, MDA_T_text);
    add_assoc_stringl(&node, "literal", text, size);
    mda_append_child(c, &node);
}

static void mda_append_text(mda_ctx *c, const char *text, size_t size)
{
    const char *cursor = text;
    size_t remaining = size;

    while (remaining > 0) {
        const char *nul = memchr(cursor, '\0', remaining);
        if (!nul) {
            mda_append_text_raw(c, cursor, remaining);
            return;
        }
        mda_append_text_raw(c, cursor, (size_t)(nul - cursor));
        mda_append_text_raw(c, "\xef\xbf\xbd", 3);
        remaining -= (size_t)(nul - cursor) + 1;
        cursor = nul + 1;
    }
}

static void mda_collect_literal(mda_ctx *c, const char *text, size_t size)
{
    const char *cursor = text;
    size_t remaining = size;

    while (remaining > 0) {
        const char *nul = memchr(cursor, '\0', remaining);
        if (!nul) {
            smart_str_appendl(&c->litbuf, cursor, remaining);
            return;
        }
        if (nul > cursor) {
            smart_str_appendl(&c->litbuf, cursor, (size_t)(nul - cursor));
        }
        smart_str_appendl(&c->litbuf, "\xef\xbf\xbd", 3);
        remaining -= (size_t)(nul - cursor) + 1;
        cursor = nul + 1;
    }
}

/* Store an MD_ATTRIBUTE (destination/title/info) under `key`, entity-decoded.
 * The AST contract exposes decoded URLs/titles (see docs/ast.md), so resolve
 * md4c's typed substrings rather than storing the raw &amp;-encoded bytes. */
static void mda_add_attr(zval *node, const char *key, const MD_ATTRIBUTE *a)
{
    const char *p;
    size_t n;
    if (mdparser_md4c_attr_plain(a, &p, &n)) {
        add_assoc_stringl(node, key, (char *) p, n);
        return;
    }
    smart_str dec = {0};
    mdparser_md4c_decode_attr(&dec, a);
    add_assoc_stringl(node, key, dec.s ? ZSTR_VAL(dec.s) : "",
        dec.s ? ZSTR_LEN(dec.s) : 0);
    smart_str_free(&dec);
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
static bool mda_pop(mda_ctx *c)
{
    /* stack[0] is the document root and is never popped under md4c's
     * balanced enter/leave contract; guard the underflow defensively so a
     * future renderer change or contract break can't index stack[-1]. */
    if (c->depth < 1) {
        c->error = MDA_ERR_PARSE;
        return false;
    }
    zval node = c->stack[c->depth];
    ZVAL_UNDEF(&c->stack[c->depth]);
    c->depth--;
    mda_append_child(c, &node);
    return true;
}

/* ---- callbacks ------------------------------------------------------- */

static zend_string *mda_align_str(MD_ALIGN a)
{
    switch (a) {
        case MD_ALIGN_LEFT: return mda_s_align_left;
        case MD_ALIGN_CENTER: return mda_s_align_center;
        case MD_ALIGN_RIGHT: return mda_s_align_right;
        default: return mda_s_align_none;
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
        case MD_BLOCK_QUOTE: mda_new_node(&n, MDA_T_block_quote); break;
        case MD_BLOCK_UL: {
            mda_new_node(&n, MDA_T_list);
            add_assoc_str(&n, "list_type", mda_s_list_bullet);
            add_assoc_long(&n, "list_start", 0);
            add_assoc_bool(&n, "list_tight", ((MD_BLOCK_UL_DETAIL *)detail)->is_tight);
            add_assoc_str(&n, "list_delim", mda_s_delim_none);
            break;
        }
        case MD_BLOCK_OL: {
            MD_BLOCK_OL_DETAIL *d = detail;
            mda_new_node(&n, MDA_T_list);
            add_assoc_str(&n, "list_type", mda_s_list_ordered);
            add_assoc_long(&n, "list_start", d->start);
            add_assoc_bool(&n, "list_tight", d->is_tight);
            add_assoc_str(&n, "list_delim",
                d->mark_delimiter == ')' ? mda_s_delim_paren : mda_s_delim_period);
            break;
        }
        case MD_BLOCK_LI: {
            MD_BLOCK_LI_DETAIL *d = detail;
            if (d->is_task) {
                mda_new_node(&n, MDA_T_tasklist);
                add_assoc_bool(&n, "checked", d->task_mark == 'x' || d->task_mark == 'X');
            } else {
                mda_new_node(&n, MDA_T_item);
            }
            break;
        }
        case MD_BLOCK_HR: mda_new_node(&n, MDA_T_thematic_break); break;
        case MD_BLOCK_H:
            mda_new_node(&n, MDA_T_heading);
            add_assoc_long(&n, "level", ((MD_BLOCK_H_DETAIL *)detail)->level);
            break;
        case MD_BLOCK_CODE: {
            MD_BLOCK_CODE_DETAIL *d = detail;
            mda_new_node(&n, MDA_T_code_block);
            mda_add_attr(&n, "info", &d->info);
            c->collecting = true;
            smart_str_free(&c->litbuf);
            break;
        }
        case MD_BLOCK_HTML:
            mda_new_node(&n, MDA_T_html_block);
            c->collecting = true;
            smart_str_free(&c->litbuf);
            break;
        case MD_BLOCK_P: mda_new_node(&n, MDA_T_paragraph); break;
        case MD_BLOCK_TABLE: {
            mda_new_node(&n, MDA_T_table);
            zval aligns;
            array_init(&aligns);
            for (unsigned i = 0; i < ((MD_BLOCK_TABLE_DETAIL *)detail)->col_count; i++)
                add_next_index_str(&aligns, mda_s_align_none); /* refined from header cells */
            add_assoc_zval(&n, "alignments", &aligns);
            break;
        }
        case MD_BLOCK_THEAD: c->in_thead++; return 0;  /* structural; flattened */
        case MD_BLOCK_TBODY: return 0;
        case MD_BLOCK_TR:
            if (c->in_thead) c->th_col = 0;
            mda_new_node(&n, c->in_thead ? MDA_T_table_header : MDA_T_table_row);
            add_assoc_bool(&n, "is_header", c->in_thead ? 1 : 0);
            break;
        case MD_BLOCK_TH: {
            /* Header cells define the per-column alignment for the table. */
            zval *aligns = mda_table_alignments(c);
            if (aligns) {
                zval av;
                ZVAL_STR(&av, mda_align_str(((MD_BLOCK_TD_DETAIL *)detail)->align));
                zend_hash_index_update(Z_ARRVAL_P(aligns), c->th_col, &av);
            }
            c->th_col++;
            mda_new_node(&n, MDA_T_table_cell);
            break;
        }
        case MD_BLOCK_TD:
            mda_new_node(&n, MDA_T_table_cell);
            break;
        case MD_BLOCK_FOOTNOTE_DEF: {
            MD_BLOCK_FOOTNOTE_DEF_DETAIL *d = detail;
            mda_new_node(&n, MDA_T_footnote_definition);
            char buf[16];
            int w = snprintf(buf, sizeof(buf), "%u", d->id);
            add_assoc_stringl(&n, "literal", buf, (size_t)w);
            break;
        }
        case MD_BLOCK_FOOTNOTE_DEF_SECTION: return 0;  /* structural */
        case MD_BLOCK_ADMONITION:
            mda_new_node(&n, MDA_T_admonition);
            mda_add_attr(&n, "admonition_type",
                &((MD_BLOCK_ADMONITION_DETAIL *)detail)->type);
            break;
        default:
            mda_new_node(&n, MDA_T_unknown);
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
            add_assoc_str(mda_top(c), "literal", smart_str_extract(&c->litbuf));
            c->collecting = false;
            return mda_pop(c) ? 0 : 1;
        default:
            return mda_pop(c) ? 0 : 1;
    }
}

static int mda_enter_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    mda_ctx *c = userdata;
    zval n;
    switch (type) {
        case MD_SPAN_EM: mda_new_node(&n, MDA_T_emph); break;
        case MD_SPAN_STRONG: mda_new_node(&n, MDA_T_strong); break;
        case MD_SPAN_A: {
            MD_SPAN_A_DETAIL *d = detail;
            mda_new_node(&n, MDA_T_link);
            mda_add_attr(&n, "url", &d->href);
            mda_add_attr(&n, "title", &d->title);
            break;
        }
        case MD_SPAN_IMG: {
            MD_SPAN_IMG_DETAIL *d = detail;
            mda_new_node(&n, MDA_T_image);
            mda_add_attr(&n, "url", &d->src);
            mda_add_attr(&n, "title", &d->title);
            break;
        }
        case MD_SPAN_CODE:
            mda_new_node(&n, MDA_T_code);
            c->collecting = true;
            smart_str_free(&c->litbuf);
            break;
        case MD_SPAN_DEL: mda_new_node(&n, MDA_T_strikethrough); break;
        case MD_SPAN_U: mda_new_node(&n, MDA_T_underline); break;
        case MD_SPAN_SUPERSCRIPT: mda_new_node(&n, MDA_T_superscript); break;
        case MD_SPAN_SUBSCRIPT: mda_new_node(&n, MDA_T_subscript); break;
        case MD_SPAN_MARK: mda_new_node(&n, MDA_T_highlight); break;
        case MD_SPAN_SPOILER: mda_new_node(&n, MDA_T_spoiler); break;
        case MD_SPAN_LATEXMATH: mda_new_node(&n, MDA_T_latex_math); break;
        case MD_SPAN_LATEXMATH_DISPLAY: mda_new_node(&n, MDA_T_latex_math_display); break;
        case MD_SPAN_WIKILINK: {
            MD_SPAN_WIKILINK_DETAIL *d = detail;
            mda_new_node(&n, MDA_T_wikilink);
            mda_add_attr(&n, "url", &d->target);
            break;
        }
        case MD_SPAN_FOOTNOTE_REF: {
            MD_SPAN_FOOTNOTE_REF_DETAIL *d = detail;
            mda_new_node(&n, MDA_T_footnote_reference);
            char buf[16];
            int w = snprintf(buf, sizeof(buf), "%u", d->id);
            add_assoc_stringl(&n, "literal", buf, (size_t)w);
            break;
        }
        default: mda_new_node(&n, MDA_T_unknown); break;
    }
    if (!mda_push(c, &n)) return 1;
    return 0;
}

static int mda_leave_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    mda_ctx *c = userdata;
    (void)detail;
    if (type == MD_SPAN_CODE) {
        add_assoc_str(mda_top(c), "literal", smart_str_extract(&c->litbuf));
        c->collecting = false;
    }
    return mda_pop(c) ? 0 : 1;
}

static int mda_text(MD_TEXTTYPE type, const char *text, MD_SIZE size, void *userdata)
{
    mda_ctx *c = userdata;

    if (c->collecting) {
        /* code/html literal: verbatim bytes (entities not decoded). */
        if (type == MD_TEXT_NULLCHAR) {
            smart_str_appendl(&c->litbuf, "\xef\xbf\xbd", 3);
        } else {
            mda_collect_literal(c, text, size);
        }
        return 0;
    }

    zval n;
    switch (type) {
        case MD_TEXT_SOFTBR: mda_new_node(&n, MDA_T_softbreak); break;
        case MD_TEXT_BR: mda_new_node(&n, MDA_T_linebreak); break;
        case MD_TEXT_HTML:
            mda_new_node(&n, MDA_T_html_inline);
            add_assoc_stringl(&n, "literal", text, size);
            break;
        case MD_TEXT_ENTITY: {
            smart_str b = {0};
            mdparser_md4c_decode_entity(&b, text, size);
            smart_str_0(&b);
            if (b.s) mda_append_text(c, ZSTR_VAL(b.s), ZSTR_LEN(b.s));
            smart_str_free(&b);
            return 0;
        }
        case MD_TEXT_NULLCHAR:
            mda_append_text(c, "\xef\xbf\xbd", 3);
            return 0;
        default:  /* MD_TEXT_NORMAL / MD_TEXT_CODE outside a collector */
            mda_append_text(c, text, size);
            return 0;
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
    mda_new_node(&c.stack[0], MDA_T_document);

    MD_PARSER parser = {
        0, parser_flags,
        mda_enter_block, mda_leave_block,
        mda_enter_span, mda_leave_span,
        mda_text, NULL, NULL
    };

    /* validateUtf8: invalid input bytes -> U+FFFD before parsing, so AST text
     * literals match the HTML path. */
    bool owned = false;
    size_t use_len = len;
    const char *use_src = src;
    mdparser_md4c_skip_bom(&use_src, &use_len);
    if (validate_utf8)
        use_src = mdparser_md4c_validate_utf8(use_src, use_len, &use_len, &owned);

    bool bailed_out;
    int rc = mdparser_md4c_parse(use_src, (MD_SIZE)use_len, &parser, &c,
        &bailed_out);

    if (owned) efree((void *)use_src);
    smart_str_free(&c.litbuf);

    if (bailed_out) {
        for (int i = 0; i <= c.depth; i++) {
            if (Z_TYPE(c.stack[i]) != IS_UNDEF) zval_ptr_dtor(&c.stack[i]);
        }
        zend_bailout();
    }

    if (c.error != MDA_OK || rc != 0 || c.depth != 0) {
        if (c.error == MDA_OK) {
            c.error = MDA_ERR_PARSE;
        }
        /* Free the whole partial tree (stack[0] plus any still-open nodes). */
        for (int i = 0; i <= c.depth; i++) {
            if (Z_TYPE(c.stack[i]) != IS_UNDEF) zval_ptr_dtor(&c.stack[i]);
        }
        *status = c.error;
        return;
    }

    *status = MDA_OK;
    ZVAL_COPY_VALUE(return_value, &c.stack[0]);
}
