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
#include "mdparser_arginfo.h"

#include "cmark-gfm.h"
#include "cmark-gfm-core-extensions.h"
#include "cmark-gfm-extension_api.h"

#include "mdparser_ast.h"

zend_class_entry *mdparser_parser_ce;

static zend_object_handlers mdparser_parser_handlers;

static zend_object *mdparser_parser_create(zend_class_entry *ce)
{
    mdparser_parser_obj *obj = zend_object_alloc(sizeof(mdparser_parser_obj), ce);

    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &mdparser_parser_handlers;

    obj->cmark_options = mdparser_default_cmark_options;
    obj->extension_mask = mdparser_default_extension_mask;

    return &obj->std;
}

static void mdparser_parser_free(zend_object *object)
{
    mdparser_parser_obj *obj = mdparser_parser_from_obj(object);
    zend_object_std_dtor(&obj->std);
}

void mdparser_parser_register_class(void)
{
    mdparser_parser_ce = register_class_MdParser_Parser();
    mdparser_parser_ce->create_object = mdparser_parser_create;
    mdparser_parser_ce->default_object_handlers = &mdparser_parser_handlers;
    /* Parser caches a mask/extension_mask pair that default serialization
     * never captures, so unserialize() would silently yield a parser
     * running on defaults regardless of the constructed Options. Block
     * serialize entirely; clone is already blocked below. */
    mdparser_parser_ce->ce_flags |= ZEND_ACC_NOT_SERIALIZABLE;

    memcpy(&mdparser_parser_handlers, &std_object_handlers, sizeof(zend_object_handlers));
    mdparser_parser_handlers.offset = XtOffsetOf(mdparser_parser_obj, std);
    mdparser_parser_handlers.free_obj = mdparser_parser_free;
    mdparser_parser_handlers.clone_obj = NULL;
}

static cmark_parser *mdparser_build_cmark_parser(mdparser_parser_obj *obj)
{
    cmark_mem *mem = cmark_get_default_mem_allocator();
    cmark_parser *parser = cmark_parser_new_with_mem(obj->cmark_options, mem);
    if (!parser) {
        return NULL;
    }

    for (int i = 0; i < MDPARSER_EXT_COUNT; i++) {
        if (obj->extension_mask & mdparser_cached_extensions[i].bit) {
            cmark_parser_attach_syntax_extension(parser, mdparser_cached_extensions[i].ptr);
        }
    }

    return parser;
}

PHP_METHOD(MdParser_Parser, __construct)
{
    zval *options_zv = NULL;
    mdparser_parser_obj *obj;
    zend_object *this_obj;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_OBJECT_OF_CLASS_OR_NULL(options_zv, mdparser_options_ce)
    ZEND_PARSE_PARAMETERS_END();

    obj = Z_MDPARSER_PARSER_P(ZEND_THIS);
    this_obj = Z_OBJ_P(ZEND_THIS);

    if (options_zv) {
        mdparser_options_read_masks(options_zv, &obj->cmark_options, &obj->extension_mask);
        zend_update_property(mdparser_parser_ce, this_obj, "options", sizeof("options") - 1, options_zv);
    } else {
        /* Build a default Options and stash it so $parser->options is
         * never null. mdparser_parser_create already seeded the cached
         * default masks on obj; nothing else to compute here. */
        zval default_options;
        object_init_ex(&default_options, mdparser_options_ce);
        zend_call_known_instance_method_with_0_params(
            mdparser_options_ce->constructor, Z_OBJ(default_options), NULL);
        if (EG(exception)) {
            zval_ptr_dtor(&default_options);
            RETURN_THROWS();
        }
        zend_update_property(mdparser_parser_ce, this_obj, "options", sizeof("options") - 1, &default_options);
        zval_ptr_dtor(&default_options);
    }
}

typedef char *(*mdparser_renderer_fn)(cmark_node *root, int options, cmark_llist *extensions, cmark_mem *mem);

static char *mdparser_render_xml_adapter(cmark_node *root, int options, cmark_llist *extensions, cmark_mem *mem)
{
    (void) extensions;
    return cmark_render_xml_with_mem(root, options, mem);
}

static bool mdparser_check_input_size(size_t source_len)
{
    if (source_len > MDPARSER_MAX_INPUT_SIZE) {
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: input size %zu exceeds maximum %zu bytes",
            source_len, MDPARSER_MAX_INPUT_SIZE);
        return false;
    }
    return true;
}

static void mdparser_render_string(INTERNAL_FUNCTION_PARAMETERS, mdparser_renderer_fn renderer)
{
    zend_string *source;
    mdparser_parser_obj *obj;
    cmark_parser *parser = NULL;
    cmark_node *document = NULL;
    cmark_mem *mem;
    char *rendered = NULL;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(source)
    ZEND_PARSE_PARAMETERS_END();

    if (!mdparser_check_input_size(ZSTR_LEN(source))) {
        RETURN_THROWS();
    }

    obj = Z_MDPARSER_PARSER_P(ZEND_THIS);
    mem = cmark_get_default_mem_allocator();

    parser = mdparser_build_cmark_parser(obj);
    if (!parser) {
        zend_throw_exception(mdparser_exception_ce,
            "mdparser: failed to allocate cmark parser", 0);
        RETURN_THROWS();
    }

    cmark_parser_feed(parser, ZSTR_VAL(source), ZSTR_LEN(source));
    document = cmark_parser_finish(parser);

    if (!document) {
        cmark_parser_free(parser);
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark_parser_finish returned null (source length %zu)",
            ZSTR_LEN(source));
        RETURN_THROWS();
    }

    rendered = renderer(document, obj->cmark_options,
        cmark_parser_get_syntax_extensions(parser), mem);

    if (!rendered) {
        cmark_node_free(document);
        cmark_parser_free(parser);
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark renderer returned null (source length %zu)",
            ZSTR_LEN(source));
        RETURN_THROWS();
    }

    RETVAL_STRING(rendered);

    mem->free(rendered);
    cmark_node_free(document);
    cmark_parser_free(parser);
}

PHP_METHOD(MdParser_Parser, toHtml)
{
    mdparser_render_string(INTERNAL_FUNCTION_PARAM_PASSTHRU, cmark_render_html_with_mem);
}

PHP_METHOD(MdParser_Parser, toXml)
{
    mdparser_render_string(INTERNAL_FUNCTION_PARAM_PASSTHRU, mdparser_render_xml_adapter);
}

PHP_METHOD(MdParser_Parser, toAst)
{
    zend_string *source;
    mdparser_parser_obj *obj;
    cmark_parser *parser = NULL;
    cmark_node *document = NULL;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(source)
    ZEND_PARSE_PARAMETERS_END();

    if (!mdparser_check_input_size(ZSTR_LEN(source))) {
        RETURN_THROWS();
    }

    obj = Z_MDPARSER_PARSER_P(ZEND_THIS);

    parser = mdparser_build_cmark_parser(obj);
    if (!parser) {
        zend_throw_exception(mdparser_exception_ce,
            "mdparser: failed to allocate cmark parser", 0);
        RETURN_THROWS();
    }

    cmark_parser_feed(parser, ZSTR_VAL(source), ZSTR_LEN(source));
    document = cmark_parser_finish(parser);

    if (!document) {
        cmark_parser_free(parser);
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark_parser_finish returned null (source length %zu)",
            ZSTR_LEN(source));
        RETURN_THROWS();
    }

    /* The walker can throw MdParser\Exception when AST nesting exceeds
     * MDPARSER_MAX_AST_DEPTH. Free the cmark side regardless and let
     * the VM reclaim any partial return_value as part of its own
     * exception cleanup -- calling zval_ptr_dtor on return_value here
     * would race the VM's own dtor and double-free. */
    mdparser_render_ast(document, obj->cmark_options, return_value);

    cmark_node_free(document);
    cmark_parser_free(parser);

    if (EG(exception)) {
        RETURN_THROWS();
    }
}
