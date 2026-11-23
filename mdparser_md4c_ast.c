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
#define MDA_ERR_MEMORY 3
#define MDA_ERR_CELLS  4
#define MDA_MAX_TABLE_CELLS 65536

const char *mdparser_md4c_ast_status_message(int status)
{
    switch (status) {
        case MDA_ERR_PARSE: return "mdparser: md4c parser failed";
        case MDA_ERR_DEPTH: return "mdparser: AST nesting exceeds maximum depth";
        case MDA_ERR_MEMORY: return "mdparser: parse out of memory (allocation failed or mdparser.parse_memory_limit exceeded)";
        case MDA_ERR_CELLS: return "mdparser: table cell limit exceeded";
        default: return "mdparser: unknown error";
    }
}

typedef struct {
    zval node;
    HashTable *children;       /* cached Z_ARRVAL(node["children"]), NULL until first child */
    zval *last_text_lit;       /* trailing text child's "literal" zval, or NULL */
    zval *table_aligns;        /* non-NULL only on MDA_T_table frames */
} mda_frame;

typedef struct {
    int depth;                 /* index of current open node */
    int in_thead;              /* >0 while inside MD_BLOCK_THEAD */
    int th_col;                /* column index within the header row */
    size_t table_cells;
    bool wikilinks_enabled;
    bool collecting;           /* accumulating a leaf literal */
    const char *lit_ptr;       /* borrowed single-chunk leaf literal */
    size_t lit_len;
    smart_str litbuf;
    const char *text_ptr;      /* borrowed single-chunk text run */
    size_t text_len;
    smart_str textrun;         /* staged multi-chunk text fragments */
    /* Field-less leaves are value-identical, so every occurrence shares one
     * refcounted array; PHP copy-on-write separates any later write. */
    zval leaf_softbreak;
    zval leaf_linebreak;
    int error;
    /* Placed last so entry initializes only the small header via offsetof
     * instead of zeroing the full 1000-frame stack on every parse. */
    mda_frame stack[MDPARSER_MAX_AST_DEPTH];
} mda_ctx;

/* "type", "children", and the node-type values are interned once at MINIT
 * so the builder never allocates or hashes them per node. The enum, the
 * interned-string table, and the MINIT interning are all generated from
 * this list. */
#define MDA_NODE_TYPES(_) \
    _(document) _(block_quote) _(list) _(tasklist) _(item) \
    _(thematic_break) _(blank) _(heading) _(code_block) _(html_block) _(paragraph) \
    _(table) _(table_header) _(table_row) _(table_cell) _(footnote_definition) \
    _(admonition) \
    _(emph) _(strong) _(link) _(image) _(code) \
    _(strikethrough) _(insert) _(underline) _(superscript) _(subscript) _(highlight) \
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
/* Node field keys, interned at MINIT like the type names: add_assoc_*() with
 * a C-string key allocates a fresh key string on every insert. */
#define MDA_NODE_KEYS(_) \
    _(literal) _(level) _(url) _(title) _(info) \
    _(list_type) _(list_start) _(list_tight) _(list_delim) \
    _(checked) _(is_header) _(alignments) _(admonition_type)

#define MDA_KEY_DECL(id) static zend_string *mda_key_##id;
MDA_NODE_KEYS(MDA_KEY_DECL)
#undef MDA_KEY_DECL
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
#define MDA_KEY_INIT(id) mda_key_##id = zend_string_init_interned(#id, sizeof(#id) - 1, 1);
    MDA_NODE_KEYS(MDA_KEY_INIT)
#undef MDA_KEY_INIT
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

/* Field inserts into a freshly built node: the key is never already present,
 * so skip the symtable numeric-key probe and update path. */
static zend_always_inline void mda_kzval(zval *node, zend_string *key, zval *v)
{
    zend_hash_add_new(Z_ARRVAL_P(node), key, v);
}

static zend_always_inline void mda_kstr(zval *node, zend_string *key, zend_string *v)
{
    zval z;
    ZVAL_STR(&z, v);
    mda_kzval(node, key, &z);
}

static zend_always_inline void mda_make_strl(zval *z, const char *v, size_t len)
{
    /* Avoid ZVAL_STRINGL_FAST: on PHP 8.4.28+ it routes through
     * zend_string_finit() -> _safe_emalloc(len, 1, 32), adding +8 B per
     * string and a PLT call. Inline the 0/1-byte fast paths directly. */
    if (EXPECTED(len > 1)) {
        ZVAL_STRINGL(z, v, len);
    } else if (len == 0) {
        ZVAL_EMPTY_STRING(z);
    } else {
        ZVAL_CHAR(z, (unsigned char)v[0]);
    }
}

static zend_always_inline void mda_kstrl(zval *node, zend_string *key, const char *v, size_t len)
{
    zval z;
    mda_make_strl(&z, v, len);
    mda_kzval(node, key, &z);
}

static zend_always_inline void mda_klong(zval *node, zend_string *key, zend_long v)
{
    zval z;
    ZVAL_LONG(&z, v);
    mda_kzval(node, key, &z);
}

static zend_always_inline void mda_kbool(zval *node, zend_string *key, bool v)
{
    zval z;
    ZVAL_BOOL(&z, v);
    mda_kzval(node, key, &z);
}

/* Nearest enclosing table node's "alignments" array, or NULL. */
static zend_always_inline zval *mda_table_alignments(mda_ctx *c)
{
    for (int i = c->depth; i >= 0; i--) {
        if (c->stack[i].table_aligns) {
            return c->stack[i].table_aligns;
        }
    }
    return NULL;
}

/* ---- node helpers ---------------------------------------------------- */

static zend_always_inline zval *mda_top(mda_ctx *c) { return &c->stack[c->depth].node; }

/* Append `child` (ownership transferred) to the current top node's
 * "children" array, creating it on demand. */
static void mda_append_child(mda_ctx *c, zval *child)
{
    mda_frame *top = &c->stack[c->depth];
    HashTable *kids_ht = top->children;
    if (!kids_ht) {
        zval arr;
        array_init(&arr);
        kids_ht = Z_ARRVAL(arr);
        zend_hash_add_new(Z_ARRVAL(top->node), mda_k_children, &arr);
        top->children = kids_ht;
    }
    zend_hash_next_index_insert_new(kids_ht, child);
    top->last_text_lit = NULL;
}

/* Create a node array with "type" set; returns it by value in *out. */
static zend_always_inline void mda_new_node(zval *out, int type)
{
    array_init(out);
    zval v;
    ZVAL_STR(&v, mda_types[type]);
    zend_hash_add_new(Z_ARRVAL_P(out), mda_k_type, &v);
}

static zend_always_inline void mda_promote_textrun(mda_ctx *c)
{
    if (c->text_ptr) {
        smart_str_appendl(&c->textrun, c->text_ptr, c->text_len);
        c->text_ptr = NULL;
        c->text_len = 0;
    }
}

static void mda_append_text_raw(mda_ctx *c, const char *text, size_t size)
{
    if (size == 0) {
        return;
    }
    /* Single-chunk fast path: borrow the source slice without touching
     * smart_str; promote only if another fragment or entity follows. */
    if (!c->text_ptr && !c->textrun.s) {
        c->text_ptr = text;
        c->text_len = size;
        return;
    }
    mda_promote_textrun(c);
    smart_str_appendl(&c->textrun, text, size);
}

/* Materialize staged text fragments as one literal on the current top node,
 * extending the trailing text literal or creating one. Call before any
 * structural mutation (push/pop/non-text node) and at end of parse. */
static void mda_flush_textrun(mda_ctx *c)
{
    if (!c->text_ptr && (!c->textrun.s || ZSTR_LEN(c->textrun.s) == 0)) {
        return;
    }

    mda_frame *top = &c->stack[c->depth];
    zval *literal = top->last_text_lit;
    if (UNEXPECTED(literal != NULL && Z_TYPE_P(literal) == IS_STRING)) {
        const char *add_ptr = c->text_ptr ? c->text_ptr : ZSTR_VAL(c->textrun.s);
        size_t add = c->text_ptr ? c->text_len : ZSTR_LEN(c->textrun.s);
        size_t old_size = Z_STRLEN_P(literal);
        zend_string *old_str = Z_STR_P(literal);
        zend_string *joined;
        if (UNEXPECTED(ZSTR_IS_INTERNED(old_str) || GC_REFCOUNT(old_str) > 1)) {
            joined = zend_string_alloc(old_size + add, 0);
            memcpy(ZSTR_VAL(joined), ZSTR_VAL(old_str), old_size);
            zend_string_release(old_str);
        } else {
            joined = zend_string_extend(old_str, old_size + add, 0);
        }
        memcpy(ZSTR_VAL(joined) + old_size, add_ptr, add);
        ZSTR_VAL(joined)[old_size + add] = '\0';
        ZVAL_NEW_STR(literal, joined);
        c->text_ptr = NULL;
        c->text_len = 0;
        smart_str_free(&c->textrun);
        return;
    }

    zval node;
    zval lit_zv;
    mda_new_node(&node, MDA_T_text);
    if (c->text_ptr) {
        mda_make_strl(&lit_zv, c->text_ptr, c->text_len);
        c->text_ptr = NULL;
        c->text_len = 0;
    } else {
        ZVAL_STR(&lit_zv, smart_str_extract(&c->textrun));
    }
    zval *lit_slot = zend_hash_add_new(Z_ARRVAL(node), mda_key_literal, &lit_zv);
    mda_append_child(c, &node);
    top->last_text_lit = lit_slot;
}

static void mda_append_text(mda_ctx *c, const char *text, size_t size)
{
    const char *cursor = text;
    size_t remaining = size;

    while (remaining > 0 && !mdparser_md4c_oom_pending()) {
        const char *nul = memchr(cursor, '\0', remaining);
        if (EXPECTED(!nul)) {
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
    if (size == 0) {
        return;
    }
    if (!c->lit_ptr && !c->litbuf.s) {
        const char *nul = memchr(text, '\0', size);
        if (EXPECTED(!nul)) {
            c->lit_ptr = text;
            c->lit_len = size;
            return;
        }
    }
    if (c->lit_ptr) {
        /* Reserve exact combined size on promotion so a large single line
         * followed by "\n" (067_ast_literal_memory) does not double. */
        smart_str_alloc(&c->litbuf, c->lit_len + size, 0);
        if (UNEXPECTED(mdparser_md4c_oom_pending())) {
            c->lit_ptr = NULL;
            c->lit_len = 0;
            return;
        }
        smart_str_appendl(&c->litbuf, c->lit_ptr, c->lit_len);
        c->lit_ptr = NULL;
        c->lit_len = 0;
    }

    const char *cursor = text;
    size_t remaining = size;
    while (remaining > 0 && !mdparser_md4c_oom_pending()) {
        const char *nul = memchr(cursor, '\0', remaining);
        if (EXPECTED(!nul)) {
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
static void mda_add_attr(zval *node, zend_string *key, const MD_ATTRIBUTE *a)
{
    mdparser_md4c_attr_view value;
    mdparser_md4c_attr_view_init(&value, a);
    mda_kstrl(node, key, value.text, value.size);
    mdparser_md4c_attr_view_destroy(&value);
}

/* Push a freshly-created container node. Returns false on depth overflow. */
static bool mda_push_ex(mda_ctx *c, zval *node, zval *table_aligns)
{
    mda_flush_textrun(c);
    if (UNEXPECTED(c->depth + 1 >= MDPARSER_MAX_AST_DEPTH)) {
        zval_ptr_dtor(node);
        c->error = MDA_ERR_DEPTH;
        return false;
    }
    mda_frame *f = &c->stack[++c->depth];
    f->node = *node;
    f->children = NULL;
    f->last_text_lit = NULL;
    f->table_aligns = table_aligns;
    return true;
}

static zend_always_inline bool mda_push(mda_ctx *c, zval *node)
{
    return mda_push_ex(c, node, NULL);
}

/* Pop the top node and append it to its parent. */
static bool mda_pop(mda_ctx *c)
{
    mda_flush_textrun(c);
    /* md4c's balanced enter/leave never pops stack[0]; guard anyway so a
     * contract break can't index stack[-1]. */
    if (UNEXPECTED(c->depth < 1)) {
        c->error = MDA_ERR_PARSE;
        return false;
    }
    zval node = c->stack[c->depth].node;
    ZVAL_UNDEF(&c->stack[c->depth].node);
    c->depth--;
    mda_append_child(c, &node);
    return true;
}

static zend_always_inline void mda_finish_literal(mda_ctx *c)
{
    if (c->lit_ptr) {
        mda_kstrl(mda_top(c), mda_key_literal, c->lit_ptr, c->lit_len);
        c->lit_ptr = NULL;
        c->lit_len = 0;
    } else {
        mda_kstr(mda_top(c), mda_key_literal, smart_str_extract(&c->litbuf));
    }
    c->collecting = false;
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
    zval *table_aligns = NULL;

    switch (type) {
        case MD_BLOCK_DOC:
            /* document is stack[0], created in the entry function. */
            return 0;
        case MD_BLOCK_QUOTE: mda_new_node(&n, MDA_T_block_quote); break;
        case MD_BLOCK_UL: {
            mda_new_node(&n, MDA_T_list);
            mda_kstr(&n, mda_key_list_type, mda_s_list_bullet);
            mda_klong(&n, mda_key_list_start, 0);
            mda_kbool(&n, mda_key_list_tight, ((MD_BLOCK_UL_DETAIL *)detail)->is_tight);
            mda_kstr(&n, mda_key_list_delim, mda_s_delim_none);
            break;
        }
        case MD_BLOCK_OL: {
            MD_BLOCK_OL_DETAIL *d = detail;
            mda_new_node(&n, MDA_T_list);
            mda_kstr(&n, mda_key_list_type, mda_s_list_ordered);
            mda_klong(&n, mda_key_list_start, d->start);
            mda_kbool(&n, mda_key_list_tight, d->is_tight);
            mda_kstr(&n, mda_key_list_delim,
                d->mark_delimiter == ')' ? mda_s_delim_paren : mda_s_delim_period);
            break;
        }
        case MD_BLOCK_LI: {
            MD_BLOCK_LI_DETAIL *d = detail;
            if (d->is_task) {
                mda_new_node(&n, MDA_T_tasklist);
                mda_kbool(&n, mda_key_checked, d->task_mark == 'x' || d->task_mark == 'X');
            } else {
                mda_new_node(&n, MDA_T_item);
            }
            break;
        }
        case MD_BLOCK_HR: mda_new_node(&n, MDA_T_thematic_break); break;
        case MD_BLOCK_BLANK: mda_new_node(&n, MDA_T_blank); break;
        case MD_BLOCK_H:
            mda_new_node(&n, MDA_T_heading);
            mda_klong(&n, mda_key_level, ((MD_BLOCK_H_DETAIL *)detail)->level);
            break;
        case MD_BLOCK_CODE: {
            MD_BLOCK_CODE_DETAIL *d = detail;
            mda_new_node(&n, MDA_T_code_block);
            mda_add_attr(&n, mda_key_info, &d->info);
            c->collecting = true;
            c->lit_ptr = NULL;
            c->lit_len = 0;
            if (UNEXPECTED(c->litbuf.s != NULL)) {
                smart_str_free(&c->litbuf);
            }
            break;
        }
        case MD_BLOCK_HTML:
            mda_new_node(&n, MDA_T_html_block);
            c->collecting = true;
            c->lit_ptr = NULL;
            c->lit_len = 0;
            if (UNEXPECTED(c->litbuf.s != NULL)) {
                smart_str_free(&c->litbuf);
            }
            break;
        case MD_BLOCK_P: mda_new_node(&n, MDA_T_paragraph); break;
        case MD_BLOCK_TABLE: {
            unsigned cols = ((MD_BLOCK_TABLE_DETAIL *)detail)->col_count;
            mda_new_node(&n, MDA_T_table);
            zval aligns;
            array_init_size(&aligns, cols);
            for (unsigned i = 0; i < cols; i++) {
                add_next_index_str(&aligns, mda_s_align_none); /* refined from header cells */
            }
            table_aligns = zend_hash_add_new(Z_ARRVAL(n), mda_key_alignments, &aligns);
            break;
        }
        case MD_BLOCK_THEAD: c->in_thead++; return 0;  /* structural; flattened */
        case MD_BLOCK_TBODY: return 0;
        case MD_BLOCK_TR:
            if (c->in_thead) c->th_col = 0;
            mda_new_node(&n, c->in_thead ? MDA_T_table_header : MDA_T_table_row);
            mda_kbool(&n, mda_key_is_header, c->in_thead ? 1 : 0);
            break;
        case MD_BLOCK_TH: {
            if (UNEXPECTED(c->table_cells >= MDA_MAX_TABLE_CELLS)) {
                c->error = MDA_ERR_CELLS;
                return 1;
            }
            c->table_cells++;
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
            if (UNEXPECTED(c->table_cells >= MDA_MAX_TABLE_CELLS)) {
                c->error = MDA_ERR_CELLS;
                return 1;
            }
            c->table_cells++;
            mda_new_node(&n, MDA_T_table_cell);
            break;
        case MD_BLOCK_FOOTNOTE_DEF: {
            MD_BLOCK_FOOTNOTE_DEF_DETAIL *d = detail;
            mda_new_node(&n, MDA_T_footnote_definition);
            mda_kstr(&n, mda_key_literal, zend_ulong_to_str(d->id));
            break;
        }
        case MD_BLOCK_FOOTNOTE_DEF_SECTION: return 0;  /* structural */
        case MD_BLOCK_ADMONITION:
            mda_new_node(&n, MDA_T_admonition);
            mda_add_attr(&n, mda_key_admonition_type,
                &((MD_BLOCK_ADMONITION_DETAIL *)detail)->type);
            break;
        default:
            mda_new_node(&n, MDA_T_unknown);
            break;
    }
    if (UNEXPECTED(!mda_push_ex(c, &n, table_aligns))) return 1;
    return mdparser_md4c_cb_status(0);
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
            mda_finish_literal(c);
            return mdparser_md4c_cb_status(mda_pop(c) ? 0 : 1);
        default:
            return mdparser_md4c_cb_status(mda_pop(c) ? 0 : 1);
    }
}

static int mda_enter_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    mda_ctx *c = userdata;
    if (type == MD_SPAN_WIKILINK && !c->wikilinks_enabled) return 0;
    zval n;
    switch (type) {
        case MD_SPAN_EM: mda_new_node(&n, MDA_T_emph); break;
        case MD_SPAN_STRONG: mda_new_node(&n, MDA_T_strong); break;
        case MD_SPAN_A: {
            MD_SPAN_A_DETAIL *d = detail;
            mda_new_node(&n, MDA_T_link);
            mda_add_attr(&n, mda_key_url, &d->href);
            mda_add_attr(&n, mda_key_title, &d->title);
            break;
        }
        case MD_SPAN_IMG: {
            MD_SPAN_IMG_DETAIL *d = detail;
            mda_new_node(&n, MDA_T_image);
            mda_add_attr(&n, mda_key_url, &d->src);
            mda_add_attr(&n, mda_key_title, &d->title);
            break;
        }
        case MD_SPAN_CODE:
            mda_new_node(&n, MDA_T_code);
            c->collecting = true;
            c->lit_ptr = NULL;
            c->lit_len = 0;
            if (UNEXPECTED(c->litbuf.s != NULL)) {
                smart_str_free(&c->litbuf);
            }
            break;
        case MD_SPAN_DEL: mda_new_node(&n, MDA_T_strikethrough); break;
        case MD_SPAN_INS: mda_new_node(&n, MDA_T_insert); break;
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
            mda_add_attr(&n, mda_key_url, &d->target);
            break;
        }
        case MD_SPAN_FOOTNOTE_REF: {
            MD_SPAN_FOOTNOTE_REF_DETAIL *d = detail;
            mda_new_node(&n, MDA_T_footnote_reference);
            mda_kstr(&n, mda_key_literal, zend_ulong_to_str(d->id));
            break;
        }
        default: mda_new_node(&n, MDA_T_unknown); break;
    }
    if (UNEXPECTED(!mda_push(c, &n))) return 1;
    return mdparser_md4c_cb_status(0);
}

static int mda_leave_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    mda_ctx *c = userdata;
    (void)detail;
    if (type == MD_SPAN_WIKILINK && !c->wikilinks_enabled) return 0;
    if (type == MD_SPAN_CODE) {
        mda_finish_literal(c);
    }
    return mdparser_md4c_cb_status(mda_pop(c) ? 0 : 1);
}

static int mda_text(MD_TEXTTYPE type, const char *text, MD_SIZE size, void *userdata)
{
    mda_ctx *c = userdata;

    if (c->collecting) {
        /* code/html literal: verbatim bytes (entities not decoded). */
        if (UNEXPECTED(type == MD_TEXT_NULLCHAR)) {
            if (c->lit_ptr) {
                smart_str_appendl(&c->litbuf, c->lit_ptr, c->lit_len);
                c->lit_ptr = NULL;
                c->lit_len = 0;
            }
            smart_str_appendl(&c->litbuf, "\xef\xbf\xbd", 3);
        } else {
            mda_collect_literal(c, text, size);
        }
        return mdparser_md4c_cb_status(0);
    }
    zval n;
    switch (type) {
        case MD_TEXT_SOFTBR:
            mda_flush_textrun(c);
            if (Z_TYPE(c->leaf_softbreak) == IS_UNDEF) {
                mda_new_node(&c->leaf_softbreak, MDA_T_softbreak);
            }
            ZVAL_COPY(&n, &c->leaf_softbreak);
            break;
        case MD_TEXT_BR:
            mda_flush_textrun(c);
            if (Z_TYPE(c->leaf_linebreak) == IS_UNDEF) {
                mda_new_node(&c->leaf_linebreak, MDA_T_linebreak);
            }
            ZVAL_COPY(&n, &c->leaf_linebreak);
            break;
        case MD_TEXT_HTML:
            mda_flush_textrun(c);
            mda_new_node(&n, MDA_T_html_inline);
            mda_kstrl(&n, mda_key_literal, text, size);
            break;
        case MD_TEXT_ENTITY: {
            /* Decoded bytes contain no NUL (append_cp maps 0 to U+FFFD),
             * so decode straight into the staged run. */
            mda_promote_textrun(c);
            mdparser_md4c_decode_entity(&c->textrun, text, size);
            return mdparser_md4c_cb_status(0);
        }
        case MD_TEXT_NULLCHAR:
            mda_append_text(c, "\xef\xbf\xbd", 3);
            return mdparser_md4c_cb_status(0);
        default:  /* MD_TEXT_NORMAL / MD_TEXT_CODE outside a collector */
            mda_append_text(c, text, size);
            return mdparser_md4c_cb_status(0);
    }
    mda_append_child(c, &n);
    return mdparser_md4c_cb_status(0);
}

void mdparser_md4c_render_ast(const char *src, size_t len, unsigned parser_flags,
    bool validate_utf8, zval *return_value, int *status)
{
    mda_ctx c;
    memset(&c, 0, offsetof(mda_ctx, stack));
    c.wikilinks_enabled = (parser_flags & MD_FLAG_WIKILINKS) != 0;

    /* stack[0] is the document root. */
    mda_new_node(&c.stack[0].node, MDA_T_document);
    c.stack[0].children = NULL;
    c.stack[0].last_text_lit = NULL;
    c.stack[0].table_aligns = NULL;

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
    if (validate_utf8) {
        use_src = mdparser_md4c_validate_utf8(use_src, use_len, &use_len, &owned);
        if (!use_src) {
            zval_ptr_dtor(&c.stack[0].node);
            *status = MDA_ERR_MEMORY;
            return;
        }
    }

    bool bailed_out;
    bool limit_exceeded;
    bool alloc_failed;
    int rc = mdparser_md4c_parse(use_src, (MD_SIZE)use_len, &parser, &c,
        &bailed_out, &limit_exceeded, &alloc_failed);

    smart_str_free(&c.litbuf);
    zval_ptr_dtor(&c.leaf_softbreak);
    zval_ptr_dtor(&c.leaf_linebreak);

    if (UNEXPECTED(bailed_out)) {
        if (owned) efree((void *)use_src);
        smart_str_free(&c.textrun);
        for (int i = 0; i <= c.depth; i++) {
            if (Z_TYPE(c.stack[i].node) != IS_UNDEF) zval_ptr_dtor(&c.stack[i].node);
        }
        zend_bailout();
    }

    if (UNEXPECTED(c.error != MDA_OK || rc != 0 || c.depth != 0)) {
        if (owned) efree((void *)use_src);
        if (c.error == MDA_OK) {
            c.error = (limit_exceeded || alloc_failed) ? MDA_ERR_MEMORY : MDA_ERR_PARSE;
        }
        /* Free the whole partial tree (stack[0] plus any still-open nodes). */
        for (int i = 0; i <= c.depth; i++) {
            if (Z_TYPE(c.stack[i].node) != IS_UNDEF) zval_ptr_dtor(&c.stack[i].node);
        }
        smart_str_free(&c.textrun);
        *status = c.error;
        return;
    }
    /* Trailing text run may borrow from use_src; flush before freeing it. */
    mda_flush_textrun(&c);
    if (owned) efree((void *)use_src);
    smart_str_free(&c.textrun);
    *status = MDA_OK;
    ZVAL_COPY_VALUE(return_value, &c.stack[0].node);
}
