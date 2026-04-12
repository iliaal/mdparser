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

/* AST key strings are lazy-initialized on the first toAst() call so
 * users who only need toHtml/toXml don't pay the ~4 µs setup at module
 * load. zend_string_init_interned after startup creates request-scoped
 * interned strings which would dangle across requests, so we use
 * persistent non-interned zend_strings instead; the hash is cached on
 * first use inside zend_hash_add_new. Released in MSHUTDOWN. */
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

static void mdparser_init_ast_strings(void)
{
    md_str_type         = zend_string_init("type",         sizeof("type") - 1,         1);
    md_str_children     = zend_string_init("children",     sizeof("children") - 1,     1);
    md_str_literal      = zend_string_init("literal",      sizeof("literal") - 1,      1);
    md_str_info         = zend_string_init("info",         sizeof("info") - 1,         1);
    md_str_url          = zend_string_init("url",          sizeof("url") - 1,          1);
    md_str_title        = zend_string_init("title",        sizeof("title") - 1,        1);
    md_str_level        = zend_string_init("level",        sizeof("level") - 1,        1);
    md_str_list_type    = zend_string_init("list_type",    sizeof("list_type") - 1,    1);
    md_str_list_start   = zend_string_init("list_start",   sizeof("list_start") - 1,   1);
    md_str_list_tight   = zend_string_init("list_tight",   sizeof("list_tight") - 1,   1);
    md_str_list_delim   = zend_string_init("list_delim",   sizeof("list_delim") - 1,   1);
    md_str_alignments   = zend_string_init("alignments",   sizeof("alignments") - 1,   1);
    md_str_is_header    = zend_string_init("is_header",    sizeof("is_header") - 1,    1);
    md_str_checked      = zend_string_init("checked",      sizeof("checked") - 1,      1);
    md_str_start_line   = zend_string_init("start_line",   sizeof("start_line") - 1,   1);
    md_str_start_column = zend_string_init("start_column", sizeof("start_column") - 1, 1);
    md_str_end_line     = zend_string_init("end_line",     sizeof("end_line") - 1,     1);
    md_str_end_column   = zend_string_init("end_column",   sizeof("end_column") - 1,   1);
}

void mdparser_release_ast_strings(void)
{
    if (!md_str_type) {
        return;
    }
    zend_string_release(md_str_type);
    zend_string_release(md_str_children);
    zend_string_release(md_str_literal);
    zend_string_release(md_str_info);
    zend_string_release(md_str_url);
    zend_string_release(md_str_title);
    zend_string_release(md_str_level);
    zend_string_release(md_str_list_type);
    zend_string_release(md_str_list_start);
    zend_string_release(md_str_list_tight);
    zend_string_release(md_str_list_delim);
    zend_string_release(md_str_alignments);
    zend_string_release(md_str_is_header);
    zend_string_release(md_str_checked);
    zend_string_release(md_str_start_line);
    zend_string_release(md_str_start_column);
    zend_string_release(md_str_end_line);
    zend_string_release(md_str_end_column);
    md_str_type = NULL;
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

static const char *mdparser_list_type_string(cmark_list_type t)
{
    switch (t) {
        case CMARK_BULLET_LIST:  return "bullet";
        case CMARK_ORDERED_LIST: return "ordered";
        default:                 return "none";
    }
}

static const char *mdparser_list_delim_string(cmark_delim_type d)
{
    switch (d) {
        case CMARK_PERIOD_DELIM: return "period";
        case CMARK_PAREN_DELIM:  return "paren";
        default:                 return "none";
    }
}

static const char *mdparser_table_align_string(uint8_t align)
{
    switch (align) {
        case 'l': return "left";
        case 'c': return "center";
        case 'r': return "right";
        default:  return "none";
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

    /* Max observed key count per node is 7 (list: type, list_type,
     * list_start, list_tight, list_delim, children + optional sourcepos
     * ×4). array_init_size(8) avoids the first rehash. */
    array_init_size(out, 8);

    /* cmark-gfm's get_type_string switch does not cover footnote node
     * types and returns "<unknown>" for them. Override locally so AST
     * consumers get stable, documented names. */
    const char *type_string = cmark_node_get_type_string(node);
    cmark_node_type ntype = cmark_node_get_type(node);
    if (ntype == CMARK_NODE_FOOTNOTE_REFERENCE) {
        type_string = "footnote_reference";
    } else if (ntype == CMARK_NODE_FOOTNOTE_DEFINITION) {
        type_string = "footnote_definition";
    }
    md_add_string(out, md_str_type, type_string);

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
                add_next_index_string(&alignments_arr,
                    mdparser_table_align_string(alignments ? alignments[i] : 0));
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
            md_add_string(out, md_str_list_type,
                mdparser_list_type_string(cmark_node_get_list_type(node)));
            md_add_long(out, md_str_list_start, cmark_node_get_list_start(node));
            md_add_bool(out, md_str_list_tight, cmark_node_get_list_tight(node));
            md_add_string(out, md_str_list_delim,
                mdparser_list_delim_string(cmark_node_get_list_delim(node)));
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
    if (UNEXPECTED(!md_str_type)) {
        mdparser_init_ast_strings();
    }
    mdparser_node_to_array(document, cmark_options, 0, return_value);
}
