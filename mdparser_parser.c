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
#include "mdparser_md4c_util.h"
#include "mdparser_md4c_vendor.h"

zend_class_entry *mdparser_parser_ce;

/* Each render path casts the post-validation byte length to md4c's MD_SIZE
 * (a 32-bit unsigned) for md_parse(). The input-size cap keeps every length
 * far inside that range; assert it at compile time so a future cap bump can't
 * silently reintroduce truncation. Portable negative-array idiom stands in
 * for _Static_assert, which MSVC rejects in default C mode. */
typedef char mdparser_input_cap_fits_md_size[
    (MDPARSER_MAX_INPUT_SIZE <= (size_t)(MD_SIZE)-1) ? 1 : -1];

static zend_object_handlers mdparser_parser_handlers;
static uintptr_t mdparser_parser_options_offset = 0;
static bool mdparser_parser_options_offset_valid = false;

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
    /* Parser caches parser/render-option masks that default serialization
     * never captures, so unserialize() would silently yield a parser running
     * on defaults regardless of the constructed Options. Block serialization
     * entirely; clone is already blocked below. */
    mdparser_parser_ce->ce_flags |= ZEND_ACC_NOT_SERIALIZABLE;

    memcpy(&mdparser_parser_handlers, &std_object_handlers, sizeof(zend_object_handlers));
    mdparser_parser_handlers.offset = offsetof(mdparser_parser_obj, std);
    mdparser_parser_handlers.free_obj = mdparser_parser_free;
    mdparser_parser_handlers.clone_obj = NULL;

    zend_property_info *info = zend_hash_str_find_ptr(
        &mdparser_parser_ce->properties_info, "options", sizeof("options") - 1);
    if (info && IS_VALID_PROPERTY_OFFSET(info->offset)) {
        mdparser_parser_options_offset = info->offset;
        mdparser_parser_options_offset_valid = true;
    }
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
    unsigned new_md4c_pflags;
    int new_md4c_ropts;

    if (!options_zv) {
        /* Build a default Options directly without a PHP method call frame;
         * masks are already constant defaults. */
        mdparser_options_create_default(&default_options);
        if (UNEXPECTED(EG(exception))) {
            zval_ptr_dtor(&default_options);
            RETURN_THROWS();
        }
        options_zv = &default_options;
        default_owned = true;
        new_md4c_pflags = mdparser_default_md4c_pflags;
        new_md4c_ropts = mdparser_default_md4c_ropts;
    } else {
        /* Commit the cached masks only after the readonly $options write
         * succeeds. A second __construct() throws on that write; committing
         * first would leave $options reporting safe options while rendering
         * with an unsafe mask. */
        mdparser_options_read_masks(options_zv, &new_md4c_pflags, &new_md4c_ropts);

        /* read_masks throws if the Options object skipped __construct; bail
         * before publishing a half-constructed Options. */
        if (UNEXPECTED(EG(exception))) {
            RETURN_THROWS();
        }
    }

    bool wrote_direct = false;
    if (EXPECTED(mdparser_parser_options_offset_valid)) {
#if PHP_VERSION_ID >= 80400
        if (EXPECTED(!zend_object_is_lazy(this_obj)))
#endif
        {
            zval *slot = OBJ_PROP(this_obj, mdparser_parser_options_offset);
            if (EXPECTED(Z_TYPE_P(slot) == IS_UNDEF && Z_PROP_FLAG_P(slot) == IS_PROP_UNINIT)) {
                if (default_owned) {
                    ZVAL_COPY_VALUE(slot, &default_options);
                    default_owned = false;
                } else {
                    ZVAL_COPY(slot, options_zv);
                }
                Z_PROP_FLAG_P(slot) = 0;
                wrote_direct = true;
            }
        }
    }

    if (UNEXPECTED(!wrote_direct)) {
        zend_update_property(mdparser_parser_ce, this_obj, "options", sizeof("options") - 1, options_zv);
    }

    if (default_owned) {
        zval_ptr_dtor(&default_options);
    }

    if (UNEXPECTED(EG(exception))) {
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
        if (mdparser_md4c_oom_pending()) {
            return;
        }
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
        if (mdparser_md4c_oom_pending()) {
            return;
        }
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

static void mdparser_md4c_ast_emit(INTERNAL_FUNCTION_PARAMETERS,
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
    mdparser_md4c_render_ast(ZSTR_VAL(source), ZSTR_LEN(source),
        parser_flags, validate_utf8, return_value, &status);
    if (status != 0) {
        if (mdparser_md4c_oom_pending()) {
            return;
        }
        zend_throw_exception(mdparser_exception_ce,
            mdparser_md4c_ast_status_message(status), 0);
        RETURN_THROWS();
    }
}

PHP_METHOD(MdParser_Parser, toAst)
{
    mdparser_parser_obj *obj = Z_MDPARSER_PARSER_P(ZEND_THIS);
    mdparser_md4c_ast_emit(INTERNAL_FUNCTION_PARAM_PASSTHRU,
        obj->md4c_pflags, (obj->md4c_ropts & MDPARSER_RF_VALIDATE_UTF8) != 0);
}

/* Build the normalized inline buffer: every retained physical line starts
 * with an ordinary punctuation sentinel (`;`), so md4c sees a single
 * paragraph whose every line begins with punctuation and block-level
 * constructs cannot fire on any line.
 *
 *   - \r\n and lone \r are normalized to \n (md4c normalizes too,
 *     but doing it here lets us know exactly where line breaks are);
 *   - runs of \n are collapsed to one (blank lines would otherwise
 *     end the paragraph and start a new one);
 *   - leading, trailing, and whitespace-only physical lines are dropped;
 *   - `;` is prepended at the start, and after every retained \n.
 *
 * Unlike a zero-width-space prefix, punctuation preserves delimiter flanking
 * for line-leading `_`, `~`, and `=` spans. The HTML callback consumes
 * exactly the inserted sentinel at each line start; literal source bytes
 * are not searched or removed afterward. */
static void mdparser_inline_normalize(smart_str *norm, const char *src, size_t src_len)
{
    static const char sentinel = ';';

    /* Grow on demand rather than reserving the 2*src_len + 1 worst case:
     * newline-heavy input normalizes to almost nothing, and an eager
     * reserve can cross memory_limit (40 MB of `\n` would reserve ~168 MB). */
    /* Single-line fast path: the normalized buffer is sentinel+input, or
     * empty when the line is all blanks. */
    bool single_line = (memchr(src, '\n', src_len) == NULL &&
                        memchr(src, '\r', src_len) == NULL);
    if (single_line) {
        size_t s = 0;
        while (s < src_len && (src[s] == ' ' || src[s] == '\t')) s++;
        if (s < src_len) {
            smart_str_appendc(norm, sentinel);
            smart_str_appendl(norm, src, src_len);
        }
    }

    bool need_sentinel = true;
    size_t pending_indent_start = 0;
    size_t pending_indent_len = 0;

    for (size_t i = 0; !single_line && i < src_len
        && !mdparser_md4c_oom_pending(); ) {
        char c = src[i];
        if (c == '\r') {
            if (i + 1 < src_len && src[i + 1] == '\n') {
                i++;
            }
            c = '\n';
        }
        if (c == '\n') {
            pending_indent_len = 0;
            i++;
            if (need_sentinel) {
                /* leading newline, or run of newlines; drop. */
                continue;
            }
            smart_str_appendc(norm, '\n');
            need_sentinel = true;
            continue;
        }
        if (need_sentinel && (c == ' ' || c == '\t')) {
            if (pending_indent_len == 0) {
                pending_indent_start = i;
            }
            pending_indent_len++;
            i++;
            continue;
        }
        if (need_sentinel) {
            smart_str_appendc(norm, sentinel);
            if (pending_indent_len != 0) {
                smart_str_appendl(norm, src + pending_indent_start, pending_indent_len);
                pending_indent_len = 0;
            }
            need_sentinel = false;
        }
        /* Bulk-append the rest of the physical line (content until CR/LF). */
        size_t run = i;
        while (run < src_len && src[run] != '\n' && src[run] != '\r') {
            run++;
        }
        smart_str_appendl(norm, src + i, run - i);
        i = run;
    }
    /* A trailing newline after retained content stays in norm; no sentinel
     * is added for the non-existent next line. */
}

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

    /* One sentinel-prefixed paragraph (Parsedown::line() semantics). */
    const char *src = ZSTR_VAL(source);
    size_t src_len = ZSTR_LEN(source);
    char stack_buf[1024];
    smart_str norm = {0};
    const char *buf = "";
    size_t buf_len = 0;

    /* render_html owns the BOM skip for the plain paths, but the sentinel
     * prefix added below would hide a BOM from it, so skip exactly one here. */
    mdparser_md4c_skip_bom(&src, &src_len);
    if (src_len > 0) {
        if (memchr(src, '\n', src_len) == NULL && memchr(src, '\r', src_len) == NULL) {
            size_t s = 0;
            while (s < src_len && (src[s] == ' ' || src[s] == '\t')) s++;
            if (s < src_len) {
                if (EXPECTED(src_len < sizeof(stack_buf))) {
                    stack_buf[0] = ';';
                    memcpy(stack_buf + 1, src, src_len);
                    buf = stack_buf;
                    buf_len = src_len + 1;
                } else {
                    smart_str_alloc(&norm, src_len + 2, 0);
                    if (!mdparser_md4c_oom_pending()) {
                        smart_str_appendc(&norm, ';');
                        smart_str_appendl(&norm, src, src_len);
                        buf = ZSTR_VAL(norm.s);
                        buf_len = ZSTR_LEN(norm.s);
                    }
                }
            }
        } else {
            mdparser_inline_normalize(&norm, src, src_len);
            if (norm.s) {
                buf = ZSTR_VAL(norm.s);
                buf_len = ZSTR_LEN(norm.s);
            }
        }
    }

    if (mdparser_md4c_oom_pending()) {
        smart_str_free(&norm);
        return;
    }

    if (buf_len == 0) {
        smart_str_free(&norm);
        RETURN_EMPTY_STRING();
    }

    /* The sentinel normalization suppresses headings, so heading anchors
     * are dropped; nofollow still applies to inline links. The HTML renderer
     * suppresses the outer <p>...</p>\n directly under INLINE_SENTINEL. */
    int inline_status = 0;
    int inline_ropts = (obj->md4c_ropts & ~MDPARSER_RF_HEADING_ANCHORS)
        | MDPARSER_RF_INLINE_SENTINEL;
    zend_string *rendered_zs = mdparser_md4c_render_html(buf, buf_len,
        obj->md4c_pflags, inline_ropts, &inline_status);
    smart_str_free(&norm);
    if (!rendered_zs) {
        if (mdparser_md4c_oom_pending()) {
            return;
        }
        zend_throw_exception(mdparser_exception_ce,
            mdparser_md4c_status_message(inline_status), 0);
        RETURN_THROWS();
    }

    RETVAL_STR(rendered_zs);
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
    mdparser_md4c_ast_emit(INTERNAL_FUNCTION_PARAM_PASSTHRU,
        mdparser_default_md4c_pflags,
        (mdparser_default_md4c_ropts & MDPARSER_RF_VALIDATE_UTF8) != 0);
}
