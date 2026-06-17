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

#include "mdparser_md4c_html.h"
#include "mdparser_md4c_ast.h"
#include "mdparser_md4c_xml.h"

zend_class_entry *mdparser_parser_ce;

static zend_object_handlers mdparser_parser_handlers;

static zend_object *mdparser_parser_create(zend_class_entry *ce)
{
    mdparser_parser_obj *obj = zend_object_alloc(sizeof(mdparser_parser_obj), ce);

    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &mdparser_parser_handlers;

    obj->md4c_pflags = mdparser_default_md4c_pflags;
    obj->md4c_ropts = mdparser_default_md4c_ropts;

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
#if PHP_VERSION_ID >= 80300
    /* default_object_handlers is 8.3+. On 8.2 the per-object
     * obj->std.handlers assignment in mdparser_parser_create() is the
     * correct mechanism. */
    mdparser_parser_ce->default_object_handlers = &mdparser_parser_handlers;
#endif
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
    unsigned new_md4c_pflags;
    int new_md4c_ropts;
    mdparser_options_read_masks(options_zv, &new_md4c_pflags, &new_md4c_ropts);

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

    obj->md4c_pflags = new_md4c_pflags;
    obj->md4c_ropts = new_md4c_ropts;
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

/* md4c HTML render path, shared by instance toHtml and static html(). */
static void mdparser_md4c_html_emit(INTERNAL_FUNCTION_PARAMETERS,
    unsigned parser_flags, int render_opts)
{
    zend_string *source;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(source)
    ZEND_PARSE_PARAMETERS_END();

    if (!mdparser_check_input_size(ZSTR_LEN(source))) {
        RETURN_THROWS();
    }

    int status = 0;
    zend_string *out = mdparser_md4c_render_html(
        ZSTR_VAL(source), ZSTR_LEN(source), parser_flags, render_opts, &status);
    if (!out) {
        zend_throw_exception(mdparser_exception_ce,
            mdparser_md4c_status_message(status), 0);
        RETURN_THROWS();
    }
    RETVAL_STR(out);
}

PHP_METHOD(MdParser_Parser, toHtml)
{
    mdparser_parser_obj *obj = Z_MDPARSER_PARSER_P(ZEND_THIS);
    mdparser_md4c_html_emit(INTERNAL_FUNCTION_PARAM_PASSTHRU,
        obj->md4c_pflags, obj->md4c_ropts);
}

static void mdparser_md4c_xml_emit(INTERNAL_FUNCTION_PARAMETERS,
    unsigned parser_flags, bool validate_utf8)
{
    zend_string *source;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(source)
    ZEND_PARSE_PARAMETERS_END();

    if (!mdparser_check_input_size(ZSTR_LEN(source))) {
        RETURN_THROWS();
    }

    int status = 0;
    zend_string *out = mdparser_md4c_render_xml(
        ZSTR_VAL(source), ZSTR_LEN(source), parser_flags, validate_utf8, &status);
    if (!out) {
        zend_throw_exception(mdparser_exception_ce,
            mdparser_md4c_xml_status_message(status), 0);
        RETURN_THROWS();
    }
    RETVAL_STR(out);
}

PHP_METHOD(MdParser_Parser, toXml)
{
    mdparser_parser_obj *obj = Z_MDPARSER_PARSER_P(ZEND_THIS);
    mdparser_md4c_xml_emit(INTERNAL_FUNCTION_PARAM_PASSTHRU, obj->md4c_pflags,
        (obj->md4c_ropts & MDPARSER_RF_VALIDATE_UTF8) != 0);
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
    int status = 0;
    mdparser_md4c_render_ast(ZSTR_VAL(source), ZSTR_LEN(source),
        obj->md4c_pflags, (obj->md4c_ropts & MDPARSER_RF_VALIDATE_UTF8) != 0,
        return_value, &status);
    if (status != 0) {
        zend_throw_exception(mdparser_exception_ce,
            mdparser_md4c_ast_status_message(status), 0);
        RETURN_THROWS();
    }
}

/* Parsedown::line() semantics: render `source` as inline-only HTML
 * without the `<p>` wrapper, and suppress all block-level constructs
 * so `# h` / `- a` / `> q` / `1. x` render as literal text.
 *
 * md4c does not expose an inline-only parse mode. Block parsing
 * is line-oriented and triggers off the first byte of every physical
 * line, so a single sentinel before the source only protects the first
 * line. We instead build a normalized buffer where every line starts
 * with a zero-width space (U+200B, UTF-8 E2 80 8B):
 *
 *   - \r\n and lone \r are normalized to \n (md4c normalizes too,
 *     but doing it here lets us know exactly where line breaks are);
 *   - runs of \n are collapsed to one (blank lines would otherwise
 *     end the paragraph and start a new one);
 *   - leading, trailing, and whitespace-only physical lines are dropped;
 *   - ZWSP is prepended at the start, and after every retained \n.
 *
 * md4c sees a single paragraph whose every line begins with ZWSP, so
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
    size_t pending_indent_start = 0;
    size_t pending_indent_len = 0;

    for (size_t i = 0; i < src_len; i++) {
        char c = src[i];
        if (c == '\r') {
            if (i + 1 < src_len && src[i + 1] == '\n') {
                i++;
            }
            c = '\n';
        }
        if (c == '\n') {
            pending_indent_len = 0;
            if (need_zwsp) {
                /* leading newline, or run of newlines; drop. */
                continue;
            }
            smart_str_appendc(&norm, '\n');
            need_zwsp = true;
            continue;
        }
        if (need_zwsp && (c == ' ' || c == '\t')) {
            if (pending_indent_len == 0) {
                pending_indent_start = i;
            }
            pending_indent_len++;
            continue;
        }
        if (need_zwsp) {
            smart_str_appendl(&norm, zwsp, sizeof(zwsp));
            if (pending_indent_len != 0) {
                smart_str_appendl(&norm, src + pending_indent_start, pending_indent_len);
                pending_indent_len = 0;
            }
            need_zwsp = false;
        }
        smart_str_appendc(&norm, c);
    }
    /* If the input ended on a \n, norm already has no trailing newline
     * (we deferred the ZWSP for a non-existent next line). */

    const char *buf = norm.s ? ZSTR_VAL(norm.s) : "";
    size_t buf_len = norm.s ? ZSTR_LEN(norm.s) : 0;

    /* Heading anchors are meaningless here -- block markers are suppressed
     * by the ZWSP normalization, so no headings emit. nofollow stays:
     * inline snippets can contain links, applied in-stream by the renderer. */
    int inline_status = 0;
    int inline_ropts = obj->md4c_ropts & ~MDPARSER_RF_HEADING_ANCHORS;
    zend_string *rendered_zs = mdparser_md4c_render_html(buf, buf_len,
        obj->md4c_pflags, inline_ropts, &inline_status);
    smart_str_free(&norm);
    if (!rendered_zs) {
        zend_throw_exception(mdparser_exception_ce,
            mdparser_md4c_status_message(inline_status), 0);
        RETURN_THROWS();
    }
    const char *rendered = ZSTR_VAL(rendered_zs);

    size_t out_len = ZSTR_LEN(rendered_zs);
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

    /* Strip remaining ZWSPs (the per-line sentinels we inserted). */
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

    zend_string_release(rendered_zs);
    RETVAL_STR(body_str);
}

PHP_METHOD(MdParser_Parser, html)
{
    mdparser_md4c_html_emit(INTERNAL_FUNCTION_PARAM_PASSTHRU,
        mdparser_default_md4c_pflags, mdparser_default_md4c_ropts);
}

PHP_METHOD(MdParser_Parser, xml)
{
    mdparser_md4c_xml_emit(INTERNAL_FUNCTION_PARAM_PASSTHRU,
        mdparser_default_md4c_pflags,
        (mdparser_default_md4c_ropts & MDPARSER_RF_VALIDATE_UTF8) != 0);
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

    int status = 0;
    mdparser_md4c_render_ast(ZSTR_VAL(source), ZSTR_LEN(source),
        mdparser_default_md4c_pflags,
        (mdparser_default_md4c_ropts & MDPARSER_RF_VALIDATE_UTF8) != 0,
        return_value, &status);
    if (status != 0) {
        zend_throw_exception(mdparser_exception_ce,
            mdparser_md4c_ast_status_message(status), 0);
        RETURN_THROWS();
    }
}
