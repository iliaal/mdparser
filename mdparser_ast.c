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

#include "php_mdparser.h"
#include "mdparser_ast.h"

#include "cmark-gfm.h"
#include "cmark-gfm-core-extensions.h"

static void mdparser_node_to_array(cmark_node *node, int cmark_options, zval *out);

static void mdparser_add_string_field(zval *target, const char *key, const char *value)
{
    if (value) {
        add_assoc_string(target, key, value);
    } else {
        add_assoc_string(target, key, "");
    }
}

static void mdparser_add_children(cmark_node *parent, int cmark_options, zval *parent_arr)
{
    zval children;
    array_init(&children);

    for (cmark_node *child = cmark_node_first_child(parent); child != NULL;
         child = cmark_node_next(child)) {
        zval child_arr;
        mdparser_node_to_array(child, cmark_options, &child_arr);
        add_next_index_zval(&children, &child_arr);
    }

    add_assoc_zval(parent_arr, "children", &children);
}

static void mdparser_add_sourcepos(cmark_node *node, zval *target)
{
    add_assoc_long(target, "start_line", cmark_node_get_start_line(node));
    add_assoc_long(target, "start_column", cmark_node_get_start_column(node));
    add_assoc_long(target, "end_line", cmark_node_get_end_line(node));
    add_assoc_long(target, "end_column", cmark_node_get_end_column(node));
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

static void mdparser_node_to_array(cmark_node *node, int cmark_options, zval *out)
{
    array_init(out);

    const char *type_string = cmark_node_get_type_string(node);
    add_assoc_string(out, "type", (char *)type_string);

    if (cmark_options & CMARK_OPT_SOURCEPOS) {
        mdparser_add_sourcepos(node, out);
    }

    cmark_node_type type = cmark_node_get_type(node);
    bool is_extension = (strcmp(type_string, "table") == 0) ||
                        (strcmp(type_string, "table_header") == 0) ||
                        (strcmp(type_string, "table_row") == 0) ||
                        (strcmp(type_string, "table_cell") == 0) ||
                        (strcmp(type_string, "strikethrough") == 0) ||
                        (strcmp(type_string, "tasklist") == 0);

    if (is_extension) {
        if (strcmp(type_string, "table") == 0) {
            uint16_t n_columns = cmark_gfm_extensions_get_table_columns(node);
            uint8_t *alignments = cmark_gfm_extensions_get_table_alignments(node);

            zval alignments_arr;
            array_init(&alignments_arr);
            for (uint16_t i = 0; i < n_columns; i++) {
                add_next_index_string(&alignments_arr,
                    (char *)mdparser_table_align_string(alignments ? alignments[i] : 0));
            }
            add_assoc_zval(out, "alignments", &alignments_arr);
        } else if (strcmp(type_string, "table_row") == 0) {
            add_assoc_bool(out, "is_header",
                cmark_gfm_extensions_get_table_row_is_header(node) ? 1 : 0);
        } else if (strcmp(type_string, "tasklist") == 0) {
            add_assoc_bool(out, "checked",
                cmark_gfm_extensions_get_tasklist_item_checked(node) ? 1 : 0);
        }

        mdparser_add_children(node, cmark_options, out);
        return;
    }

    switch (type) {
        case CMARK_NODE_HEADING:
            add_assoc_long(out, "level", cmark_node_get_heading_level(node));
            mdparser_add_children(node, cmark_options, out);
            break;

        case CMARK_NODE_CODE_BLOCK: {
            const char *info = cmark_node_get_fence_info(node);
            mdparser_add_string_field(out, "info", info);
            mdparser_add_string_field(out, "literal", cmark_node_get_literal(node));
            break;
        }

        case CMARK_NODE_HTML_BLOCK:
        case CMARK_NODE_HTML_INLINE:
        case CMARK_NODE_TEXT:
        case CMARK_NODE_CODE:
            mdparser_add_string_field(out, "literal", cmark_node_get_literal(node));
            break;

        case CMARK_NODE_LIST:
            add_assoc_string(out, "list_type",
                (char *)mdparser_list_type_string(cmark_node_get_list_type(node)));
            add_assoc_long(out, "list_start", cmark_node_get_list_start(node));
            add_assoc_bool(out, "list_tight", cmark_node_get_list_tight(node));
            add_assoc_string(out, "list_delim",
                (char *)mdparser_list_delim_string(cmark_node_get_list_delim(node)));
            mdparser_add_children(node, cmark_options, out);
            break;

        case CMARK_NODE_LINK:
        case CMARK_NODE_IMAGE:
            mdparser_add_string_field(out, "url", cmark_node_get_url(node));
            mdparser_add_string_field(out, "title", cmark_node_get_title(node));
            mdparser_add_children(node, cmark_options, out);
            break;

        case CMARK_NODE_SOFTBREAK:
        case CMARK_NODE_LINEBREAK:
        case CMARK_NODE_THEMATIC_BREAK:
            /* No fields, no children. */
            break;

        default:
            /* document, block_quote, item, paragraph, emph, strong,
             * custom_block, custom_inline — just recurse. */
            mdparser_add_children(node, cmark_options, out);
            break;
    }
}

void mdparser_render_ast(cmark_node *document, int cmark_options, zval *return_value)
{
    mdparser_node_to_array(document, cmark_options, return_value);
}
