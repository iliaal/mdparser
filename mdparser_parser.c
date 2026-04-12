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

static cmark_parser *mdparser_build_cmark_parser(int cmark_options, int extension_mask)
{
    cmark_mem *mem = cmark_get_default_mem_allocator();
    cmark_parser *parser = cmark_parser_new_with_mem(cmark_options, mem);
    if (!parser) {
        return NULL;
    }

    for (int i = 0; i < MDPARSER_EXT_COUNT; i++) {
        if (extension_mask & mdparser_cached_extensions[i].bit) {
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

/* Core HTML/XML render path: takes explicit masks (so both instance
 * methods and static shortcuts can call it with either the Parser's
 * cached masks or the module-level defaults). Caller is responsible
 * for ZPP and the input-size cap; this helper owns the cmark lifecycle
 * and writes the result into return_value or throws. */
static void mdparser_do_render_string(
    int cmark_options, int extension_mask,
    zend_string *source, mdparser_renderer_fn renderer,
    zval *return_value)
{
    cmark_mem *mem = cmark_get_default_mem_allocator();
    cmark_parser *parser = mdparser_build_cmark_parser(cmark_options, extension_mask);
    if (!parser) {
        zend_throw_exception(mdparser_exception_ce,
            "mdparser: failed to allocate cmark parser", 0);
        return;
    }

    cmark_parser_feed(parser, ZSTR_VAL(source), ZSTR_LEN(source));
    cmark_node *document = cmark_parser_finish(parser);

    if (!document) {
        cmark_parser_free(parser);
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark_parser_finish returned null (source length %zu)",
            ZSTR_LEN(source));
        return;
    }

    char *rendered = renderer(document, cmark_options,
        cmark_parser_get_syntax_extensions(parser), mem);

    if (!rendered) {
        cmark_node_free(document);
        cmark_parser_free(parser);
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark renderer returned null (source length %zu)",
            ZSTR_LEN(source));
        return;
    }

    RETVAL_STRING(rendered);

    mem->free(rendered);
    cmark_node_free(document);
    cmark_parser_free(parser);
}

/* Core AST render path, same factoring as mdparser_do_render_string. */
static void mdparser_do_render_ast(
    int cmark_options, int extension_mask,
    zend_string *source, zval *return_value)
{
    cmark_parser *parser = mdparser_build_cmark_parser(cmark_options, extension_mask);
    if (!parser) {
        zend_throw_exception(mdparser_exception_ce,
            "mdparser: failed to allocate cmark parser", 0);
        return;
    }

    cmark_parser_feed(parser, ZSTR_VAL(source), ZSTR_LEN(source));
    cmark_node *document = cmark_parser_finish(parser);

    if (!document) {
        cmark_parser_free(parser);
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark_parser_finish returned null (source length %zu)",
            ZSTR_LEN(source));
        return;
    }

    /* The walker can throw MdParser\Exception when AST nesting exceeds
     * MDPARSER_MAX_AST_DEPTH. Free the cmark side regardless and let
     * the VM reclaim any partial return_value as part of its own
     * exception cleanup -- calling zval_ptr_dtor on return_value here
     * would race the VM's own dtor and double-free. */
    mdparser_render_ast(document, cmark_options, return_value);

    cmark_node_free(document);
    cmark_parser_free(parser);
}

static void mdparser_render_string_method(INTERNAL_FUNCTION_PARAMETERS, mdparser_renderer_fn renderer)
{
    zend_string *source;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(source)
    ZEND_PARSE_PARAMETERS_END();

    if (!mdparser_check_input_size(ZSTR_LEN(source))) {
        RETURN_THROWS();
    }

    mdparser_parser_obj *obj = Z_MDPARSER_PARSER_P(ZEND_THIS);
    mdparser_do_render_string(obj->cmark_options, obj->extension_mask,
        source, renderer, return_value);

    if (EG(exception)) {
        RETURN_THROWS();
    }
}

PHP_METHOD(MdParser_Parser, toHtml)
{
    mdparser_render_string_method(INTERNAL_FUNCTION_PARAM_PASSTHRU, cmark_render_html_with_mem);
}

PHP_METHOD(MdParser_Parser, toXml)
{
    mdparser_render_string_method(INTERNAL_FUNCTION_PARAM_PASSTHRU, mdparser_render_xml_adapter);
}

PHP_METHOD(MdParser_Parser, toAst)
{
    zend_string *source;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(source)
    ZEND_PARSE_PARAMETERS_END();

    if (!mdparser_check_input_size(ZSTR_LEN(source))) {
        RETURN_THROWS();
    }

    mdparser_parser_obj *obj = Z_MDPARSER_PARSER_P(ZEND_THIS);
    mdparser_do_render_ast(obj->cmark_options, obj->extension_mask,
        source, return_value);

    if (EG(exception)) {
        RETURN_THROWS();
    }
}

/* Parsedown::line() semantics: render `source` as inline-only HTML
 * without the `<p>` wrapper, and suppress all block-level constructs
 * so `# h` / `- a` / `> q` / `1. x` render as literal text.
 *
 * cmark-gfm does not expose an inline-only parse mode, so we prepend
 * a zero-width space (U+200B, UTF-8 E2 80 8B) before feeding the
 * source. The ZWSP is an ordinary text character to cmark's block
 * parser, which means the caller's first character never appears at
 * column 0 and therefore cannot trigger an ATX heading, list marker,
 * blockquote, indented code block, thematic break, or fenced code.
 * cmark treats the whole input as a single paragraph, renders it as
 * `<p>\xE2\x80\x8B...</p>\n`, and we strip the sentinel + the wrapper
 * on the way out. */
PHP_METHOD(MdParser_Parser, toInlineHtml)
{
    zend_string *source;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(source)
    ZEND_PARSE_PARAMETERS_END();

    if (!mdparser_check_input_size(ZSTR_LEN(source))) {
        RETURN_THROWS();
    }

    mdparser_parser_obj *obj = Z_MDPARSER_PARSER_P(ZEND_THIS);

    cmark_mem *mem = cmark_get_default_mem_allocator();
    cmark_parser *parser = mdparser_build_cmark_parser(obj->cmark_options, obj->extension_mask);
    if (!parser) {
        zend_throw_exception(mdparser_exception_ce,
            "mdparser: failed to allocate cmark parser", 0);
        RETURN_THROWS();
    }

    static const char zwsp[3] = { (char)0xE2, (char)0x80, (char)0x8B };
    cmark_parser_feed(parser, zwsp, sizeof(zwsp));
    cmark_parser_feed(parser, ZSTR_VAL(source), ZSTR_LEN(source));
    cmark_node *document = cmark_parser_finish(parser);

    if (!document) {
        cmark_parser_free(parser);
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark_parser_finish returned null (source length %zu)",
            ZSTR_LEN(source));
        RETURN_THROWS();
    }

    char *rendered = cmark_render_html_with_mem(document, obj->cmark_options,
        cmark_parser_get_syntax_extensions(parser), mem);

    if (!rendered) {
        cmark_node_free(document);
        cmark_parser_free(parser);
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark renderer returned null (source length %zu)",
            ZSTR_LEN(source));
        RETURN_THROWS();
    }

    /* Expect exact prefix `<p>\xE2\x80\x8B` and exact suffix `</p>\n`.
     * If the sentinel trick failed (e.g. some future cmark change that
     * normalizes ZWSP) the prefix won't match and we fall back to the
     * full rendered HTML, so the caller never gets a corrupted half-
     * stripped output. */
    size_t out_len = strlen(rendered);
    static const char prefix[] = "<p>\xE2\x80\x8B";
    static const size_t prefix_len = sizeof(prefix) - 1;
    static const char suffix[] = "</p>\n";
    static const size_t suffix_len = sizeof(suffix) - 1;

    if (out_len >= prefix_len + suffix_len &&
        memcmp(rendered, prefix, prefix_len) == 0 &&
        memcmp(rendered + out_len - suffix_len, suffix, suffix_len) == 0)
    {
        size_t inner_len = out_len - prefix_len - suffix_len;
        RETVAL_STRINGL(rendered + prefix_len, inner_len);
    } else {
        RETVAL_STRING(rendered);
    }

    mem->free(rendered);
    cmark_node_free(document);
    cmark_parser_free(parser);
}

PHP_METHOD(MdParser_Parser, html)
{
    zend_string *source;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(source)
    ZEND_PARSE_PARAMETERS_END();

    if (!mdparser_check_input_size(ZSTR_LEN(source))) {
        RETURN_THROWS();
    }

    mdparser_do_render_string(
        mdparser_default_cmark_options, mdparser_default_extension_mask,
        source, cmark_render_html_with_mem, return_value);

    if (EG(exception)) {
        RETURN_THROWS();
    }
}

PHP_METHOD(MdParser_Parser, xml)
{
    zend_string *source;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(source)
    ZEND_PARSE_PARAMETERS_END();

    if (!mdparser_check_input_size(ZSTR_LEN(source))) {
        RETURN_THROWS();
    }

    mdparser_do_render_string(
        mdparser_default_cmark_options, mdparser_default_extension_mask,
        source, mdparser_render_xml_adapter, return_value);

    if (EG(exception)) {
        RETURN_THROWS();
    }
}

PHP_METHOD(MdParser_Parser, ast)
{
    zend_string *source;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(source)
    ZEND_PARSE_PARAMETERS_END();

    if (!mdparser_check_input_size(ZSTR_LEN(source))) {
        RETURN_THROWS();
    }

    mdparser_do_render_ast(
        mdparser_default_cmark_options, mdparser_default_extension_mask,
        source, return_value);

    if (EG(exception)) {
        RETURN_THROWS();
    }
}
