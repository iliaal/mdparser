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
#include "mdparser_arena.h"

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

static cmark_parser *mdparser_build_cmark_parser_mem(int cmark_options, int extension_mask, cmark_mem *mem)
{
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

#ifndef MDPARSER_ARENA
static cmark_parser *mdparser_build_cmark_parser(int cmark_options, int extension_mask)
{
    return mdparser_build_cmark_parser_mem(cmark_options, extension_mask, &mdparser_zend_mem);
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
#endif /* !MDPARSER_ARENA */

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

/* One render run's parser + memory lifecycle, centralized so every entry
 * point (instance + static html/xml/ast, inline) shares one allocator.
 *
 * Arena build (default): each call gets a fresh Zend-backed bump arena.
 * Nodes and render buffers come from emalloc'd slabs (memory_limit still
 * applies); free() recycles into a per-call free-list and the whole parse is
 * reclaimed in one mdparser_arena_destroy(). The parser is built fresh on the
 * arena and dies with it -- no caching. RETVAL_STR{,ING} / zend_string output
 * is copied out before destroy, so results survive.
 *
 * Non-arena build: the instance path reuses obj's cached parser (cleared via
 * parser_dirty on a failed finish); the static path (obj == NULL) builds a
 * one-shot parser this run owns and frees. */
typedef struct {
#ifdef MDPARSER_ARENA
    mdparser_arena arena;
#endif
    cmark_parser *parser;
    bool owns_parser;
} mdparser_run;

static cmark_parser *mdparser_run_begin(mdparser_run *r,
    mdparser_parser_obj *obj, int cmark_options, int extension_mask)
{
    r->owns_parser = false;
#ifdef MDPARSER_ARENA
    (void)obj;
    mdparser_arena_init(&r->arena);
    mdparser_arena_activate(&r->arena);
    r->parser = mdparser_build_cmark_parser_mem(cmark_options, extension_mask,
        &mdparser_arena_mem);
    if (!r->parser) {
        mdparser_arena_deactivate();
        mdparser_arena_destroy(&r->arena);
    }
#else
    if (obj) {
        r->parser = mdparser_get_cmark_parser(obj);
    } else {
        r->parser = mdparser_build_cmark_parser(cmark_options, extension_mask);
        r->owns_parser = true;
    }
#endif
    return r->parser;
}

static zend_always_inline cmark_mem *mdparser_run_mem(void)
{
#ifdef MDPARSER_ARENA
    return &mdparser_arena_mem;
#else
    return &mdparser_zend_mem;
#endif
}

/* `document`/`rendered` are the cmark-owned outputs to release on the
 * non-arena path; the arena path reclaims everything in bulk. */
static void mdparser_run_end(mdparser_run *r, cmark_node *document, char *rendered)
{
#ifdef MDPARSER_ARENA
    (void)document; (void)rendered;
    mdparser_arena_deactivate();
    mdparser_arena_destroy(&r->arena);
#else
    if (rendered) mdparser_zend_mem.free(rendered);
    if (document) cmark_node_free(document);
    if (r->owns_parser && r->parser) cmark_parser_free(r->parser);
#endif
}

/* Core HTML/XML render path. Owns the parser via mdparser_run_*; caller is
 * responsible only for ZPP and the input-size cap. `obj_or_null` is the
 * per-instance Parser object (cached-parser + parser_dirty tracking) or NULL
 * for the static path. */
static void mdparser_do_render_string(
    mdparser_parser_obj *obj_or_null,
    int cmark_options, int extension_mask, int postprocess_mask,
    zend_string *source, mdparser_renderer_fn renderer,
    zval *return_value)
{
    cmark_node *document = NULL;
    char *rendered = NULL;
    mdparser_run run;

    cmark_parser *parser = mdparser_run_begin(&run, obj_or_null,
        cmark_options, extension_mask);
    if (!parser) {
        zend_throw_exception(mdparser_exception_ce,
            "mdparser: failed to allocate cmark parser", 0);
        return;
    }
    cmark_mem *mem = mdparser_run_mem();

    if (obj_or_null) obj_or_null->parser_dirty = true;
    cmark_parser_feed(parser, ZSTR_VAL(source), ZSTR_LEN(source));
    document = cmark_parser_finish(parser);

    if (!document) {
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark_parser_finish returned null (source length %zu)",
            ZSTR_LEN(source));
        goto cleanup;
    }
    /* finish() returned a document, which means cmark_parser_reset
     * was called internally. Parser is clean again. */
    if (obj_or_null) obj_or_null->parser_dirty = false;

    rendered = renderer(document, cmark_options,
        cmark_parser_get_syntax_extensions(parser), mem);

    if (!rendered) {
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark renderer returned null (source length %zu)",
            ZSTR_LEN(source));
        goto cleanup;
    }

    if (postprocess_mask) {
        int pp_status = 0;
        zend_string *processed = mdparser_html_postprocess_ex(
            rendered, strlen(rendered), document, cmark_options,
            cmark_parser_get_syntax_extensions(parser), postprocess_mask,
            &pp_status);
        if (!processed) {
            zend_throw_exception(mdparser_exception_ce,
                mdparser_pp_status_message(pp_status), 0);
            goto cleanup;
        }
        RETVAL_STR(processed);
    } else {
        RETVAL_STRING(rendered);
    }

cleanup:
    mdparser_run_end(&run, document, rendered);
}

/* Core AST render path. Same parser-lifetime contract as
 * mdparser_do_render_string -- owns the parser via mdparser_run_*. */
static void mdparser_do_render_ast(
    mdparser_parser_obj *obj_or_null,
    int cmark_options, int extension_mask,
    zend_string *source, zval *return_value)
{
    mdparser_run run;

    cmark_parser *parser = mdparser_run_begin(&run, obj_or_null,
        cmark_options, extension_mask);
    if (!parser) {
        zend_throw_exception(mdparser_exception_ce,
            "mdparser: failed to allocate cmark parser", 0);
        return;
    }

    if (obj_or_null) obj_or_null->parser_dirty = true;
    cmark_parser_feed(parser, ZSTR_VAL(source), ZSTR_LEN(source));
    cmark_node *document = cmark_parser_finish(parser);

    if (!document) {
        zend_throw_exception_ex(mdparser_exception_ce, 0,
            "mdparser: cmark_parser_finish returned null (source length %zu)",
            ZSTR_LEN(source));
        mdparser_run_end(&run, NULL, NULL);
        return;
    }
    if (obj_or_null) obj_or_null->parser_dirty = false;

    /* The walker can throw MdParser\Exception when AST nesting exceeds
     * MDPARSER_MAX_AST_DEPTH. Free the cmark side regardless and let
     * the VM reclaim any partial return_value as part of its own
     * exception cleanup -- calling zval_ptr_dtor on return_value here
     * would race the VM's own dtor and double-free. The walker reads the
     * cmark tree (arena memory) before run_end reclaims it. */
    mdparser_render_ast(document, cmark_options, return_value);

    mdparser_run_end(&run, document, NULL);
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
    int pp_mask = html_path ? obj->postprocess_mask : 0;
    mdparser_do_render_string(obj, obj->cmark_options, obj->extension_mask,
        pp_mask, source, renderer, return_value);
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
    mdparser_do_render_ast(obj, obj->cmark_options, obj->extension_mask,
        source, return_value);
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

    mdparser_run run;
    cmark_parser *parser = mdparser_run_begin(&run, obj,
        obj->cmark_options, obj->extension_mask);
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
        mdparser_run_end(&run, NULL, NULL);
        RETURN_THROWS();
    }
    obj->parser_dirty = false;

    /* Render without sourcepos. The wrapper-strip below matches an exact
     * `<p>\xE2\x80\x8B` prefix; a `data-sourcepos` attribute on the <p>
     * (emitted when the instance was built with Options(sourcepos:true))
     * would fail that match and fall back to returning the full HTML,
     * leaking the <p> wrapper into the supposedly wrapper-free inline
     * output. sourcepos is meaningless for stripped inline content. */
    int inline_options = obj->cmark_options & ~CMARK_OPT_SOURCEPOS;
    char *rendered = cmark_render_html_with_mem(document, inline_options,
        cmark_parser_get_syntax_extensions(parser), mdparser_run_mem());

    if (!rendered) {
        mdparser_run_end(&run, document, NULL);
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
            mdparser_run_end(&run, document, rendered);
            zend_throw_exception(mdparser_exception_ce,
                mdparser_pp_status_message(pp_status), 0);
            RETURN_THROWS();
        }
        RETVAL_STR(processed);
    } else {
        RETVAL_STR(body_str);
    }

    mdparser_run_end(&run, document, rendered);
}

/* Static-method dispatcher for Parser::html / Parser::xml (no $this).
 * Parser lifetime is owned by mdparser_do_render_string via mdparser_run_*
 * (a one-shot parser on the non-arena path, a fresh arena parser otherwise).
 * The per-instance path is mdparser_render_string_method. */
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

    mdparser_do_render_string(NULL, mdparser_default_cmark_options,
        mdparser_default_extension_mask, postprocess_mask, source, renderer,
        return_value);
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

    mdparser_do_render_ast(NULL, mdparser_default_cmark_options,
        mdparser_default_extension_mask, source, return_value);
}
