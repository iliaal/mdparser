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
#include "zend_exceptions.h"
#include "zend_smart_str.h"

#include "php_mdparser.h"
#include "mdparser_arginfo.h"

#include "cmark-gfm.h"
#include "cmark-gfm-core-extensions.h"
#include "cmark-gfm-extension_api.h"

#include "mdparser_ast.h"
#include "mdparser_html_postprocess.h"

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
    obj->postprocess_mask = mdparser_default_postprocess_mask;
    obj->cmark_parser = NULL;
    obj->parser_dirty = false;

    return &obj->std;
}

static void mdparser_parser_free(zend_object *object)
{
    mdparser_parser_obj *obj = mdparser_parser_from_obj(object);
    if (obj->cmark_parser) {
        cmark_parser_free(obj->cmark_parser);
        obj->cmark_parser = NULL;
    }
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
    mdparser_parser_handlers.offset = offsetof(mdparser_parser_obj, std);
    mdparser_parser_handlers.free_obj = mdparser_parser_free;
    mdparser_parser_handlers.clone_obj = NULL;
}

static cmark_parser *mdparser_build_cmark_parser(int cmark_options, int extension_mask)
{
    cmark_parser *parser = cmark_parser_new_with_mem(cmark_options, &mdparser_zend_mem);
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

/* Get the per-instance cmark_parser, building it on first use.
 * cmark_parser_finish calls cmark_parser_reset internally before
 * returning, so a parser that completed a prior render is already in
 * a clean state with all extensions still attached. If the prior
 * render did NOT complete cleanly (cmark_parser_finish returned NULL,
 * exception thrown between feed and finish, etc.), the parser is
 * dirty and must be rebuilt -- cmark_parser_reset is static in
 * vendor/cmark and not callable from here.
 *
 * Caller MUST set obj->parser_dirty = true before calling
 * cmark_parser_feed and clear it only after cmark_parser_finish
 * returns a non-NULL document. */
static cmark_parser *mdparser_get_cmark_parser(mdparser_parser_obj *obj)
{
    if (obj->cmark_parser && obj->parser_dirty) {
        cmark_parser_free(obj->cmark_parser);
        obj->cmark_parser = NULL;
    }
    if (!obj->cmark_parser) {
        obj->cmark_parser = mdparser_build_cmark_parser(
            obj->cmark_options, obj->extension_mask);
    }
    return obj->cmark_parser;
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

    zval default_options;
    bool default_owned = false;

    if (!options_zv) {
        /* Build a default Options and stash it so $parser->options is
         * never null. mdparser_parser_create already seeded the cached
         * default masks on obj; nothing else to compute here. */
        object_init_ex(&default_options, mdparser_options_ce);
        zend_call_known_instance_method_with_0_params(
            mdparser_options_ce->constructor, Z_OBJ(default_options), NULL);
        if (EG(exception)) {
            zval_ptr_dtor(&default_options);
            RETURN_THROWS();
        }
        options_zv = &default_options;
        default_owned = true;
    }

    /* Read masks into locals first, then publish the readonly $options
     * property, and only commit the cached masks on success. A second
     * __construct() call throws on the readonly write below; without
     * this ordering the cached masks would have already been replaced,
     * leaving the public $options out of sync with rendering behavior
     * (security-relevant: an unsafe-mask object reporting safe options).
     */
    int new_cmark_options;
    int new_extension_mask;
    int new_postprocess_mask;
    mdparser_options_read_masks(options_zv,
        &new_cmark_options, &new_extension_mask, &new_postprocess_mask);

    /* read_masks throws if the Options object skipped __construct
     * (uninitialized typed properties). Bail before publishing
     * $options so the parser never holds a reference to a
     * half-constructed Options. */
    if (EG(exception)) {
        if (default_owned) {
            zval_ptr_dtor(&default_options);
        }
        RETURN_THROWS();
    }

    zend_update_property(mdparser_parser_ce, this_obj, "options", sizeof("options") - 1, options_zv);

    if (default_owned) {
        zval_ptr_dtor(&default_options);
    }

    if (EG(exception)) {
        RETURN_THROWS();
    }

    obj->cmark_options = new_cmark_options;
    obj->extension_mask = new_extension_mask;
    obj->postprocess_mask = new_postprocess_mask;
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

/* Core HTML/XML render path. Caller supplies a cmark_parser (either
 * from the per-instance cache or freshly built for the static path)
 * and is responsible for its lifetime; this helper does NOT free the
 * parser. Caller is also responsible for ZPP and the input-size cap.
 *
 * `obj_or_null` is the per-instance Parser object when the parser
 * came from the cache, or NULL for the static path. We use it to
 * track parser_dirty across feed/finish: dirty stays set if finish
 * fails, so the next render builds a fresh parser instead of feeding
 * dirty state. */
static void mdparser_do_render_string(
    mdparser_parser_obj *obj_or_null,
    cmark_parser *parser, int cmark_options, int postprocess_mask,
    zend_string *source, mdparser_renderer_fn renderer,
    zval *return_value)
{
    if (obj_or_null) obj_or_null->parser_dirty = true;
    cmark_parser_feed(parser, ZSTR_VAL(source), ZSTR_LEN(source));
    cmark_node *document = cmark_parser_finish(parser);

    if (!document) {
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark_parser_finish returned null (source length %zu)",
            ZSTR_LEN(source));
        return;
    }
    /* finish() returned a document, which means cmark_parser_reset
     * was called internally. Parser is clean again. */
    if (obj_or_null) obj_or_null->parser_dirty = false;

    char *rendered = renderer(document, cmark_options,
        cmark_parser_get_syntax_extensions(parser), &mdparser_zend_mem);

    if (!rendered) {
        cmark_node_free(document);
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark renderer returned null (source length %zu)",
            ZSTR_LEN(source));
        return;
    }

    if (postprocess_mask) {
        int pp_status = 0;
        zend_string *processed = mdparser_html_postprocess_ex(
            rendered, strlen(rendered), document, cmark_options,
            cmark_parser_get_syntax_extensions(parser), postprocess_mask,
            &pp_status);
        if (!processed) {
            mdparser_zend_mem.free(rendered);
            cmark_node_free(document);
            zend_throw_exception(mdparser_exception_ce,
                mdparser_pp_status_message(pp_status), 0);
            return;
        }
        RETVAL_STR(processed);
    } else {
        RETVAL_STRING(rendered);
    }

    mdparser_zend_mem.free(rendered);
    cmark_node_free(document);
}

/* Core AST render path. Same parser-lifetime + parser_dirty contract
 * as mdparser_do_render_string. */
static void mdparser_do_render_ast(
    mdparser_parser_obj *obj_or_null,
    cmark_parser *parser, int cmark_options,
    zend_string *source, zval *return_value)
{
    if (obj_or_null) obj_or_null->parser_dirty = true;
    cmark_parser_feed(parser, ZSTR_VAL(source), ZSTR_LEN(source));
    cmark_node *document = cmark_parser_finish(parser);

    if (!document) {
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark_parser_finish returned null (source length %zu)",
            ZSTR_LEN(source));
        return;
    }
    if (obj_or_null) obj_or_null->parser_dirty = false;

    /* The walker can throw MdParser\Exception when AST nesting exceeds
     * MDPARSER_MAX_AST_DEPTH. Free the cmark side regardless and let
     * the VM reclaim any partial return_value as part of its own
     * exception cleanup -- calling zval_ptr_dtor on return_value here
     * would race the VM's own dtor and double-free. */
    mdparser_render_ast(document, cmark_options, return_value);

    cmark_node_free(document);
}

static void mdparser_render_string_method(INTERNAL_FUNCTION_PARAMETERS,
    mdparser_renderer_fn renderer, bool html_path)
{
    zend_string *source;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(source)
    ZEND_PARSE_PARAMETERS_END();

    if (!mdparser_check_input_size(ZSTR_LEN(source))) {
        RETURN_THROWS();
    }

    mdparser_parser_obj *obj = Z_MDPARSER_PARSER_P(ZEND_THIS);
    cmark_parser *parser = mdparser_get_cmark_parser(obj);
    if (!parser) {
        zend_throw_exception(mdparser_exception_ce,
            "mdparser: failed to allocate cmark parser", 0);
        RETURN_THROWS();
    }
    int pp_mask = html_path ? obj->postprocess_mask : 0;
    mdparser_do_render_string(obj, parser, obj->cmark_options, pp_mask,
        source, renderer, return_value);
}

PHP_METHOD(MdParser_Parser, toHtml)
{
    mdparser_render_string_method(INTERNAL_FUNCTION_PARAM_PASSTHRU, cmark_render_html_with_mem, true);
}

PHP_METHOD(MdParser_Parser, toXml)
{
    mdparser_render_string_method(INTERNAL_FUNCTION_PARAM_PASSTHRU, mdparser_render_xml_adapter, false);
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
    cmark_parser *parser = mdparser_get_cmark_parser(obj);
    if (!parser) {
        zend_throw_exception(mdparser_exception_ce,
            "mdparser: failed to allocate cmark parser", 0);
        RETURN_THROWS();
    }
    mdparser_do_render_ast(obj, parser, obj->cmark_options, source, return_value);
}

/* Parsedown::line() semantics: render `source` as inline-only HTML
 * without the `<p>` wrapper, and suppress all block-level constructs
 * so `# h` / `- a` / `> q` / `1. x` render as literal text.
 *
 * cmark-gfm does not expose an inline-only parse mode. Block parsing
 * is line-oriented and triggers off the first byte of every physical
 * line, so a single sentinel before the source only protects the first
 * line. We instead build a normalized buffer where every line starts
 * with a zero-width space (U+200B, UTF-8 E2 80 8B):
 *
 *   - \r\n and lone \r are normalized to \n (cmark normalizes too,
 *     but doing it here lets us know exactly where line breaks are);
 *   - runs of \n are collapsed to one (blank lines would otherwise
 *     end the paragraph and start a new one);
 *   - leading and trailing \n are dropped;
 *   - ZWSP is prepended at the start, and after every retained \n.
 *
 * cmark sees a single paragraph whose every line begins with ZWSP, so
 * ATX headings, list markers, blockquotes, indented code, thematic
 * breaks, fenced code, and HTML blocks cannot fire on any line. The
 * rendered HTML is `<p>\xE2\x80\x8B...</p>\n`; we strip the wrapper
 * and any remaining ZWSPs (the per-line sentinels) from the body.
 * Literal U+200B in source is collateral and gets stripped too.
 */
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

    static const char zwsp[3] = { (char)0xE2, (char)0x80, (char)0x8B };

    /* Build the normalized buffer incrementally with smart_str. The
     * worst-case pre-pass bound is 4*src_len + 3 (every byte becomes a
     * newline that gains a 3-byte ZWSP prefix), which is mathematically
     * safe under MDPARSER_MAX_INPUT_SIZE = 256 MB on 64-bit size_t.
     * Pre-allocating the worst-case eagerly, however, can blow past
     * memory_limit on newline-heavy inputs whose normalized form is
     * tiny: 40 MB of `\n` would emalloc ~168 MB even though the
     * normalized buffer is empty. smart_str grows on demand, so the
     * peak allocation tracks the actual normalized size. */
    const char *src = ZSTR_VAL(source);
    size_t src_len = ZSTR_LEN(source);
    smart_str norm = {0};
    bool need_zwsp = true;

    for (size_t i = 0; i < src_len; i++) {
        char c = src[i];
        if (c == '\r') {
            if (i + 1 < src_len && src[i + 1] == '\n') {
                i++;
            }
            c = '\n';
        }
        if (c == '\n') {
            if (need_zwsp) {
                /* leading newline, or run of newlines; drop. */
                continue;
            }
            smart_str_appendc(&norm, '\n');
            need_zwsp = true;
            continue;
        }
        if (need_zwsp) {
            smart_str_appendl(&norm, zwsp, sizeof(zwsp));
            need_zwsp = false;
        }
        smart_str_appendc(&norm, c);
    }
    /* If the input ended on a \n, norm already has no trailing newline
     * (we deferred the ZWSP for a non-existent next line). */

    const char *buf = norm.s ? ZSTR_VAL(norm.s) : "";
    size_t buf_len = norm.s ? ZSTR_LEN(norm.s) : 0;

    cmark_parser *parser = mdparser_get_cmark_parser(obj);
    if (!parser) {
        smart_str_free(&norm);
        zend_throw_exception(mdparser_exception_ce,
            "mdparser: failed to allocate cmark parser", 0);
        RETURN_THROWS();
    }

    obj->parser_dirty = true;
    cmark_parser_feed(parser, buf, buf_len);
    smart_str_free(&norm);
    cmark_node *document = cmark_parser_finish(parser);

    if (!document) {
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark_parser_finish returned null (source length %zu)",
            ZSTR_LEN(source));
        RETURN_THROWS();
    }
    obj->parser_dirty = false;

    char *rendered = cmark_render_html_with_mem(document, obj->cmark_options,
        cmark_parser_get_syntax_extensions(parser), &mdparser_zend_mem);

    if (!rendered) {
        cmark_node_free(document);
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark renderer returned null (source length %zu)",
            ZSTR_LEN(source));
        RETURN_THROWS();
    }

    /* Expect exact prefix `<p>\xE2\x80\x8B` and exact suffix `</p>\n`.
     * If the sentinel trick failed (e.g. some future cmark change that
     * normalizes ZWSP, or input that ended up empty after stripping)
     * the prefix won't match and we fall back to the full rendered
     * HTML, so the caller never gets a corrupted half-stripped output.
     */
    size_t out_len = strlen(rendered);
    static const char prefix[] = "<p>\xE2\x80\x8B";
    static const size_t prefix_len = sizeof(prefix) - 1;
    static const char suffix[] = "</p>\n";
    static const size_t suffix_len = sizeof(suffix) - 1;

    const char *body_src;
    size_t body_src_len;
    if (out_len >= prefix_len + suffix_len &&
        memcmp(rendered, prefix, prefix_len) == 0 &&
        memcmp(rendered + out_len - suffix_len, suffix, suffix_len) == 0)
    {
        body_src = rendered + prefix_len;
        body_src_len = out_len - prefix_len - suffix_len;
    } else {
        body_src = rendered;
        body_src_len = out_len;
    }

    /* Strip remaining ZWSPs (the per-line sentinels we inserted).
     * Always allocate a fresh buffer: even when no ZWSPs are left, the
     * uniform path keeps the postprocess branch simple. */
    zend_string *body_str = zend_string_alloc(body_src_len, 0);
    char *out = ZSTR_VAL(body_str);
    size_t out_idx = 0;
    for (size_t i = 0; i < body_src_len; i++) {
        if (i + 2 < body_src_len &&
            (unsigned char)body_src[i] == 0xE2 &&
            (unsigned char)body_src[i + 1] == 0x80 &&
            (unsigned char)body_src[i + 2] == 0x8B)
        {
            i += 2;
            continue;
        }
        out[out_idx++] = body_src[i];
    }
    out[out_idx] = '\0';
    ZSTR_LEN(body_str) = out_idx;

    /* nofollow applies to inline HTML (links can appear in inline
     * snippets); heading-anchors does not (block markers are suppressed
     * by toInlineHtml's design, so no headings are emitted). Mask off
     * the heading bit to avoid touching the AST when only nofollow is
     * set. */
    int pp = obj->postprocess_mask & MDPARSER_PP_NOFOLLOW_LINKS;
    if (pp) {
        int pp_status = 0;
        zend_string *processed = mdparser_html_postprocess_ex(
            ZSTR_VAL(body_str), ZSTR_LEN(body_str), NULL, 0, NULL, pp,
            &pp_status);
        zend_string_release(body_str);
        if (!processed) {
            mdparser_zend_mem.free(rendered);
            cmark_node_free(document);
            zend_throw_exception(mdparser_exception_ce,
                mdparser_pp_status_message(pp_status), 0);
            RETURN_THROWS();
        }
        RETVAL_STR(processed);
    } else {
        RETVAL_STR(body_str);
    }

    mdparser_zend_mem.free(rendered);
    cmark_node_free(document);
}

/* Static-method dispatcher: builds a one-shot cmark_parser, runs the
 * render, frees. Used by Parser::html / Parser::xml / Parser::ast which
 * have no $this. The per-instance reuse path is in
 * mdparser_render_string_method / PHP_METHOD(toAst). */
static void mdparser_static_render_string(INTERNAL_FUNCTION_PARAMETERS,
    int postprocess_mask, mdparser_renderer_fn renderer)
{
    zend_string *source;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(source)
    ZEND_PARSE_PARAMETERS_END();

    if (!mdparser_check_input_size(ZSTR_LEN(source))) {
        RETURN_THROWS();
    }

    cmark_parser *parser = mdparser_build_cmark_parser(
        mdparser_default_cmark_options, mdparser_default_extension_mask);
    if (!parser) {
        zend_throw_exception(mdparser_exception_ce,
            "mdparser: failed to allocate cmark parser", 0);
        RETURN_THROWS();
    }

    mdparser_do_render_string(NULL, parser, mdparser_default_cmark_options,
        postprocess_mask, source, renderer, return_value);

    cmark_parser_free(parser);
}

PHP_METHOD(MdParser_Parser, html)
{
    mdparser_static_render_string(INTERNAL_FUNCTION_PARAM_PASSTHRU,
        mdparser_default_postprocess_mask, cmark_render_html_with_mem);
}

PHP_METHOD(MdParser_Parser, xml)
{
    mdparser_static_render_string(INTERNAL_FUNCTION_PARAM_PASSTHRU,
        0, mdparser_render_xml_adapter);
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

    cmark_parser *parser = mdparser_build_cmark_parser(
        mdparser_default_cmark_options, mdparser_default_extension_mask);
    if (!parser) {
        zend_throw_exception(mdparser_exception_ce,
            "mdparser: failed to allocate cmark parser", 0);
        RETURN_THROWS();
    }

    mdparser_do_render_ast(NULL, parser, mdparser_default_cmark_options,
        source, return_value);

    cmark_parser_free(parser);
}
