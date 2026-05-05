/*
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2026 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,     |
  | that is bundled with this package in the file LICENSE, and is       |
  | available through the world-wide-web at the following url:          |
  | http://www.php.net/license/3_01.txt                                 |
  +----------------------------------------------------------------------+
  | Author: Ilia Alshanetsky <ilia@ilia.ws>                              |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "zend_exceptions.h"

#include "php_mdparser.h"
#include "mdparser_ast.h"

#include "cmark-gfm.h"
#include "cmark-gfm-core-extensions.h"
#include "cmark-gfm-extension_api.h"

/* AST key strings are populated at MINIT (mdparser_init_ast_strings).
 * They were once lazy-initialized on the first toAst() call to skip
 * the ~4 µs setup for callers that never needed them, but lazy init
 * is not safe under ZTS: two threads entering toAst() concurrently
 * could observe a partially-initialized table.
 *
 * MINIT runs before request startup, so passing permanent=true to
 * zend_string_init_interned places these in the engine-wide permanent
 * intern table. Permanent interned strings are flagged IS_STR_INTERNED
 * and their refcount is left untouched by zend_hash_add_new -- which
 * is exactly the property we need under ZTS, where two threads insert
 * AST nodes concurrently and would otherwise race the (non-atomic)
 * refcount of a shared persistent zend_string. They live for the
 * lifetime of the engine, so no MSHUTDOWN release is needed. */
static zend_string *md_str_type;
static zend_string *md_str_children;
static zend_string *md_str_literal;
static zend_string *md_str_info;
static zend_string *md_str_url;
static zend_string *md_str_title;
static zend_string *md_str_level;
static zend_string *md_str_list_type;
static zend_string *md_str_list_start;
static zend_string *md_str_list_tight;
static zend_string *md_str_list_delim;
static zend_string *md_str_alignments;
static zend_string *md_str_is_header;
static zend_string *md_str_checked;
static zend_string *md_str_start_line;
static zend_string *md_str_start_column;
static zend_string *md_str_end_line;
static zend_string *md_str_end_column;

/* Static value strings used as the "type" / list_type / list_delim /
 * table_align fields. The set is closed (~25 names); interning at
 * MINIT eliminates ~1 emalloc + memcpy per AST node on toAst. */
static zend_string *md_v_document;
static zend_string *md_v_block_quote;
static zend_string *md_v_list;
static zend_string *md_v_item;
static zend_string *md_v_code_block;
static zend_string *md_v_html_block;
static zend_string *md_v_custom_block;
static zend_string *md_v_paragraph;
static zend_string *md_v_heading;
static zend_string *md_v_thematic_break;
static zend_string *md_v_text;
static zend_string *md_v_softbreak;
static zend_string *md_v_linebreak;
static zend_string *md_v_code;
static zend_string *md_v_html_inline;
static zend_string *md_v_custom_inline;
static zend_string *md_v_emph;
static zend_string *md_v_strong;
static zend_string *md_v_link;
static zend_string *md_v_image;
static zend_string *md_v_footnote_reference;
static zend_string *md_v_footnote_definition;
static zend_string *md_v_table;
static zend_string *md_v_table_row;
static zend_string *md_v_table_cell;
static zend_string *md_v_strikethrough;
static zend_string *md_v_tasklist;
static zend_string *md_v_unknown;
static zend_string *md_v_bullet;
static zend_string *md_v_ordered;
static zend_string *md_v_period;
static zend_string *md_v_paren;
static zend_string *md_v_left;
static zend_string *md_v_center;
static zend_string *md_v_right;
static zend_string *md_v_none;

void mdparser_init_ast_strings(void)
{
    md_str_type         = zend_string_init_interned("type",         sizeof("type") - 1,         1);
    md_str_children     = zend_string_init_interned("children",     sizeof("children") - 1,     1);
    md_str_literal      = zend_string_init_interned("literal",      sizeof("literal") - 1,      1);
    md_str_info         = zend_string_init_interned("info",         sizeof("info") - 1,         1);
    md_str_url          = zend_string_init_interned("url",          sizeof("url") - 1,          1);
    md_str_title        = zend_string_init_interned("title",        sizeof("title") - 1,        1);
    md_str_level        = zend_string_init_interned("level",        sizeof("level") - 1,        1);
    md_str_list_type    = zend_string_init_interned("list_type",    sizeof("list_type") - 1,    1);
    md_str_list_start   = zend_string_init_interned("list_start",   sizeof("list_start") - 1,   1);
    md_str_list_tight   = zend_string_init_interned("list_tight",   sizeof("list_tight") - 1,   1);
    md_str_list_delim   = zend_string_init_interned("list_delim",   sizeof("list_delim") - 1,   1);
    md_str_alignments   = zend_string_init_interned("alignments",   sizeof("alignments") - 1,   1);
    md_str_is_header    = zend_string_init_interned("is_header",    sizeof("is_header") - 1,    1);
    md_str_checked      = zend_string_init_interned("checked",      sizeof("checked") - 1,      1);
    md_str_start_line   = zend_string_init_interned("start_line",   sizeof("start_line") - 1,   1);
    md_str_start_column = zend_string_init_interned("start_column", sizeof("start_column") - 1, 1);
    md_str_end_line     = zend_string_init_interned("end_line",     sizeof("end_line") - 1,     1);
    md_str_end_column   = zend_string_init_interned("end_column",   sizeof("end_column") - 1,   1);

#define INTERN_V(name, lit) name = zend_string_init_interned(lit, sizeof(lit) - 1, 1)
    INTERN_V(md_v_document,             "document");
    INTERN_V(md_v_block_quote,          "block_quote");
    INTERN_V(md_v_list,                 "list");
    INTERN_V(md_v_item,                 "item");
    INTERN_V(md_v_code_block,           "code_block");
    INTERN_V(md_v_html_block,           "html_block");
    INTERN_V(md_v_custom_block,         "custom_block");
    INTERN_V(md_v_paragraph,            "paragraph");
    INTERN_V(md_v_heading,              "heading");
    INTERN_V(md_v_thematic_break,       "thematic_break");
    INTERN_V(md_v_text,                 "text");
    INTERN_V(md_v_softbreak,            "softbreak");
    INTERN_V(md_v_linebreak,            "linebreak");
    INTERN_V(md_v_code,                 "code");
    INTERN_V(md_v_html_inline,          "html_inline");
    INTERN_V(md_v_custom_inline,        "custom_inline");
    INTERN_V(md_v_emph,                 "emph");
    INTERN_V(md_v_strong,               "strong");
    INTERN_V(md_v_link,                 "link");
    INTERN_V(md_v_image,                "image");
    INTERN_V(md_v_footnote_reference,   "footnote_reference");
    INTERN_V(md_v_footnote_definition,  "footnote_definition");
    INTERN_V(md_v_table,                "table");
    INTERN_V(md_v_table_row,            "table_row");
    INTERN_V(md_v_table_cell,           "table_cell");
    INTERN_V(md_v_strikethrough,        "strikethrough");
    INTERN_V(md_v_tasklist,             "tasklist");
    INTERN_V(md_v_unknown,              "<unknown>");
    INTERN_V(md_v_bullet,               "bullet");
    INTERN_V(md_v_ordered,              "ordered");
    INTERN_V(md_v_period,               "period");
    INTERN_V(md_v_paren,                "paren");
    INTERN_V(md_v_left,                 "left");
    INTERN_V(md_v_center,               "center");
    INTERN_V(md_v_right,                "right");
    INTERN_V(md_v_none,                 "none");
#undef INTERN_V
}

/* Map a cmark node-type string (with footnote-name overrides) to its
 * pre-interned zend_string. Returns NULL for any string outside the
 * pre-interned set so the caller can fall back to a per-call alloc;
 * defensive for future cmark releases that introduce new types. */
static zend_string *mdparser_type_interned(const char *s, cmark_node_type ntype)
{
    if (ntype == CMARK_NODE_FOOTNOTE_REFERENCE) return md_v_footnote_reference;
    if (ntype == CMARK_NODE_FOOTNOTE_DEFINITION) return md_v_footnote_definition;
    if (!s) return md_v_unknown;
    /* Ordered roughly by frequency for the common documents. */
    if (strcmp(s, "text") == 0)                return md_v_text;
    if (strcmp(s, "paragraph") == 0)           return md_v_paragraph;
    if (strcmp(s, "softbreak") == 0)           return md_v_softbreak;
    if (strcmp(s, "code") == 0)                return md_v_code;
    if (strcmp(s, "emph") == 0)                return md_v_emph;
    if (strcmp(s, "strong") == 0)              return md_v_strong;
    if (strcmp(s, "link") == 0)                return md_v_link;
    if (strcmp(s, "image") == 0)               return md_v_image;
    if (strcmp(s, "linebreak") == 0)           return md_v_linebreak;
    if (strcmp(s, "heading") == 0)             return md_v_heading;
    if (strcmp(s, "list") == 0)                return md_v_list;
    if (strcmp(s, "item") == 0)                return md_v_item;
    if (strcmp(s, "code_block") == 0)          return md_v_code_block;
    if (strcmp(s, "html_block") == 0)          return md_v_html_block;
    if (strcmp(s, "html_inline") == 0)         return md_v_html_inline;
    if (strcmp(s, "block_quote") == 0)         return md_v_block_quote;
    if (strcmp(s, "thematic_break") == 0)      return md_v_thematic_break;
    if (strcmp(s, "document") == 0)            return md_v_document;
    if (strcmp(s, "table") == 0)               return md_v_table;
    if (strcmp(s, "table_row") == 0)           return md_v_table_row;
    if (strcmp(s, "table_cell") == 0)          return md_v_table_cell;
    if (strcmp(s, "strikethrough") == 0)       return md_v_strikethrough;
    if (strcmp(s, "tasklist") == 0)            return md_v_tasklist;
    if (strcmp(s, "custom_block") == 0)        return md_v_custom_block;
    if (strcmp(s, "custom_inline") == 0)       return md_v_custom_inline;
    if (strcmp(s, "<unknown>") == 0)           return md_v_unknown;
    return NULL;
}

static void mdparser_node_to_array(cmark_node *node, int cmark_options, int depth, zval *out);
static void mdparser_add_children(cmark_node *parent, int cmark_options, int depth, zval *parent_arr);

static inline void md_add_string(zval *arr, zend_string *key, const char *value)
{
    zval tmp;
    if (value) {
        ZVAL_STRING(&tmp, value);
    } else {
        ZVAL_EMPTY_STRING(&tmp);
    }
    zend_hash_add_new(Z_ARRVAL_P(arr), key, &tmp);
}

/* Insert a pre-interned value string into the array. The interned
 * string is permanent (engine-owned) so we copy the pointer into the
 * zval without bumping the refcount; ZVAL_INTERNED_STR sets the right
 * type flags. */
static inline void md_add_interned(zval *arr, zend_string *key, zend_string *value)
{
    zval tmp;
    ZVAL_INTERNED_STR(&tmp, value);
    zend_hash_add_new(Z_ARRVAL_P(arr), key, &tmp);
}

static inline void md_add_long(zval *arr, zend_string *key, zend_long value)
{
    zval tmp;
    ZVAL_LONG(&tmp, value);
    zend_hash_add_new(Z_ARRVAL_P(arr), key, &tmp);
}

static inline void md_add_bool(zval *arr, zend_string *key, bool value)
{
    zval tmp;
    ZVAL_BOOL(&tmp, value);
    zend_hash_add_new(Z_ARRVAL_P(arr), key, &tmp);
}

static inline void md_add_zval(zval *arr, zend_string *key, zval *value)
{
    zend_hash_add_new(Z_ARRVAL_P(arr), key, value);
}

static void mdparser_add_sourcepos(cmark_node *node, zval *target)
{
    md_add_long(target, md_str_start_line,   cmark_node_get_start_line(node));
    md_add_long(target, md_str_start_column, cmark_node_get_start_column(node));
    md_add_long(target, md_str_end_line,     cmark_node_get_end_line(node));
    md_add_long(target, md_str_end_column,   cmark_node_get_end_column(node));
}

static void mdparser_add_children(cmark_node *parent, int cmark_options, int depth, zval *parent_arr)
{
    zval children;
    array_init(&children);

    for (cmark_node *child = cmark_node_first_child(parent); child != NULL;
         child = cmark_node_next(child)) {
        zval child_arr;
        ZVAL_UNDEF(&child_arr);
        mdparser_node_to_array(child, cmark_options, depth + 1, &child_arr);
        if (UNEXPECTED(EG(exception))) {
            zval_ptr_dtor(&child_arr);
            zval_ptr_dtor(&children);
            return;
        }
        add_next_index_zval(&children, &child_arr);
    }

    md_add_zval(parent_arr, md_str_children, &children);
}

static zend_string *mdparser_list_type_interned(cmark_list_type t)
{
    switch (t) {
        case CMARK_BULLET_LIST:  return md_v_bullet;
        case CMARK_ORDERED_LIST: return md_v_ordered;
        default:                 return md_v_none;
    }
}

static zend_string *mdparser_list_delim_interned(cmark_delim_type d)
{
    switch (d) {
        case CMARK_PERIOD_DELIM: return md_v_period;
        case CMARK_PAREN_DELIM:  return md_v_paren;
        default:                 return md_v_none;
    }
}

static zend_string *mdparser_table_align_interned(uint8_t align)
{
    switch (align) {
        case 'l': return md_v_left;
        case 'c': return md_v_center;
        case 'r': return md_v_right;
        default:  return md_v_none;
    }
}

static void mdparser_node_to_array(cmark_node *node, int cmark_options, int depth, zval *out)
{
    ZVAL_UNDEF(out);

    if (UNEXPECTED(depth > MDPARSER_MAX_AST_DEPTH)) {
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: AST nesting exceeds maximum depth (%d)",
            MDPARSER_MAX_AST_DEPTH);
        return;
    }

    /* Worst case is a list node with sourcepos enabled: type,
     * start_line, start_column, end_line, end_column, list_type,
     * list_start, list_tight, list_delim, children = 10 keys.
     * array_init_size(16) lands on the next power-of-two HT bucket
     * size, so even the worst-case node finishes without a rehash;
     * smaller nodes pay one extra bucket-row of memory which is
     * negligible against per-array overhead. */
    array_init_size(out, 16);

    /* cmark-gfm's get_type_string switch does not cover footnote node
     * types and returns "<unknown>" for them. mdparser_type_interned
     * resolves footnote types from `ntype` directly and returns the
     * pre-interned string; fallback to per-call allocation for
     * unrecognized types. */
    const char *type_string = cmark_node_get_type_string(node);
    cmark_node_type ntype = cmark_node_get_type(node);
    zend_string *type_interned = mdparser_type_interned(type_string, ntype);
    if (type_interned) {
        md_add_interned(out, md_str_type, type_interned);
    } else {
        md_add_string(out, md_str_type, type_string);
    }

    if (cmark_options & CMARK_OPT_SOURCEPOS) {
        mdparser_add_sourcepos(node, out);
    }

    /* Fast-path extension detection: cmark-gfm sets a non-NULL
     * syntax_extension pointer on nodes created by an extension.
     * Replaces the previous 6-way strcmp chain against the type
     * string in the common (non-extension) case. */
    if (cmark_node_get_syntax_extension(node) != NULL) {
        if (strcmp(type_string, "table") == 0) {
            uint16_t n_columns = cmark_gfm_extensions_get_table_columns(node);
            uint8_t *alignments = cmark_gfm_extensions_get_table_alignments(node);

            zval alignments_arr;
            array_init_size(&alignments_arr, n_columns);
            for (uint16_t i = 0; i < n_columns; i++) {
                zval av;
                ZVAL_INTERNED_STR(&av,
                    mdparser_table_align_interned(alignments ? alignments[i] : 0));
                zend_hash_next_index_insert_new(Z_ARRVAL(alignments_arr), &av);
            }
            md_add_zval(out, md_str_alignments, &alignments_arr);
        } else if (strcmp(type_string, "table_row") == 0) {
            md_add_bool(out, md_str_is_header,
                cmark_gfm_extensions_get_table_row_is_header(node) ? 1 : 0);
        } else if (strcmp(type_string, "tasklist") == 0) {
            md_add_bool(out, md_str_checked,
                cmark_gfm_extensions_get_tasklist_item_checked(node) ? 1 : 0);
        }

        mdparser_add_children(node, cmark_options, depth, out);
        return;
    }

    switch (cmark_node_get_type(node)) {
        case CMARK_NODE_HEADING:
            md_add_long(out, md_str_level, cmark_node_get_heading_level(node));
            mdparser_add_children(node, cmark_options, depth, out);
            break;

        case CMARK_NODE_CODE_BLOCK:
            md_add_string(out, md_str_info,    cmark_node_get_fence_info(node));
            md_add_string(out, md_str_literal, cmark_node_get_literal(node));
            break;

        case CMARK_NODE_HTML_BLOCK:
        case CMARK_NODE_HTML_INLINE:
        case CMARK_NODE_TEXT:
        case CMARK_NODE_CODE:
            md_add_string(out, md_str_literal, cmark_node_get_literal(node));
            break;

        case CMARK_NODE_LIST:
            md_add_interned(out, md_str_list_type,
                mdparser_list_type_interned(cmark_node_get_list_type(node)));
            md_add_long(out, md_str_list_start, cmark_node_get_list_start(node));
            md_add_bool(out, md_str_list_tight, cmark_node_get_list_tight(node));
            md_add_interned(out, md_str_list_delim,
                mdparser_list_delim_interned(cmark_node_get_list_delim(node)));
            mdparser_add_children(node, cmark_options, depth, out);
            break;

        case CMARK_NODE_LINK:
        case CMARK_NODE_IMAGE:
            md_add_string(out, md_str_url,   cmark_node_get_url(node));
            md_add_string(out, md_str_title, cmark_node_get_title(node));
            mdparser_add_children(node, cmark_options, depth, out);
            break;

        case CMARK_NODE_SOFTBREAK:
        case CMARK_NODE_LINEBREAK:
        case CMARK_NODE_THEMATIC_BREAK:
            /* No fields, no children. */
            break;

        case CMARK_NODE_FOOTNOTE_REFERENCE:
            /* Inline node whose literal is the label ("1" for [^1]). */
            md_add_string(out, md_str_literal, cmark_node_get_literal(node));
            break;

        case CMARK_NODE_FOOTNOTE_DEFINITION:
            /* Block node: the literal is the label, children are the
             * definition body (paragraphs, lists, etc.). */
            md_add_string(out, md_str_literal, cmark_node_get_literal(node));
            mdparser_add_children(node, cmark_options, depth, out);
            break;

        default:
            /* document, block_quote, item, paragraph, emph, strong,
             * custom_block, custom_inline — just recurse. */
            mdparser_add_children(node, cmark_options, depth, out);
            break;
    }
}

void mdparser_render_ast(cmark_node *document, int cmark_options, zval *return_value)
{
    mdparser_node_to_array(document, cmark_options, 0, return_value);
}
