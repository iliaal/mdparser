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

zend_class_entry *mdparser_options_ce;

int mdparser_default_cmark_options = 0;
int mdparser_default_extension_mask = 0;
int mdparser_default_postprocess_mask = 0;

/* Each Options property corresponds to either a CMARK_OPT_* flag or a
 * GFM extension toggle.
 *
 * IMPORTANT: default_value MUST match the constructor default in
 * mdparser.stub.php. ZPP does not auto-apply arginfo defaults to
 * internal methods, so the C-side __construct seeds values[] from this
 * table before ZPP runs. If the two drift, `$opts->tagfilter` will
 * lie about whether tagfilter is actually enabled. The MINIT init
 * step caches the result of walking this table once, so runtime cost
 * is zero. */
/* Field indices, used by the preset factories and by __construct to
 * address individual entries in the field table without hard-coding
 * positions. MUST stay in sync with mdparser_options_fields[] below. */
enum {
    MDOPT_SOURCEPOS = 0,
    MDOPT_HARDBREAKS,
    MDOPT_NOBREAKS,
    MDOPT_SMART,
    MDOPT_UNSAFE,
    MDOPT_VALIDATE_UTF8,
    MDOPT_GITHUB_PRE_LANG,
    MDOPT_LIBERAL_HTML_TAG,
    MDOPT_FOOTNOTES,
    MDOPT_STRIKETHROUGH_DOUBLE_TILDE,
    MDOPT_TABLE_PREFER_STYLE_ATTRIBUTES,
    MDOPT_FULL_INFO_STRING,
    MDOPT_TABLES,
    MDOPT_STRIKETHROUGH,
    MDOPT_TASKLIST,
    MDOPT_AUTOLINK,
    MDOPT_TAGFILTER,
    MDOPT_HEADING_ANCHORS,
    MDOPT_NOFOLLOW_LINKS,
    MDOPT_COUNT_
};

typedef struct {
    const char *name;
    size_t name_len;
    int cmark_bit;          /* nonzero if this maps to a cmark option */
    int extension_bit;      /* nonzero if this maps to a GFM extension */
    int postprocess_bit;    /* nonzero if this drives an HTML post-pass */
    bool default_value;
} mdparser_options_field;

#define F(name_, cmark_, ext_, pp_, def_) \
    { name_, sizeof(name_) - 1, cmark_, ext_, pp_, def_ }

static const mdparser_options_field mdparser_options_fields[] = {
    F("sourcepos",                  CMARK_OPT_SOURCEPOS,                     0, 0, false),
    F("hardbreaks",                 CMARK_OPT_HARDBREAKS,                    0, 0, false),
    F("nobreaks",                   CMARK_OPT_NOBREAKS,                      0, 0, false),
    F("smart",                      CMARK_OPT_SMART,                         0, 0, false),
    F("unsafe",                     CMARK_OPT_UNSAFE,                        0, 0, false),
    F("validateUtf8",               CMARK_OPT_VALIDATE_UTF8,                 0, 0, true),
    F("githubPreLang",              CMARK_OPT_GITHUB_PRE_LANG,               0, 0, true),
    F("liberalHtmlTag",             CMARK_OPT_LIBERAL_HTML_TAG,              0, 0, false),
    F("footnotes",                  CMARK_OPT_FOOTNOTES,                     0, 0, false),
    F("strikethroughDoubleTilde",   CMARK_OPT_STRIKETHROUGH_DOUBLE_TILDE,    0, 0, false),
    F("tablePreferStyleAttributes", CMARK_OPT_TABLE_PREFER_STYLE_ATTRIBUTES, 0, 0, false),
    F("fullInfoString",             CMARK_OPT_FULL_INFO_STRING,              0, 0, false),
    F("tables",                     0, MDPARSER_EXT_TABLES,                  0, true),
    F("strikethrough",              0, MDPARSER_EXT_STRIKETHROUGH,           0, true),
    F("tasklist",                   0, MDPARSER_EXT_TASKLIST,                0, true),
    F("autolink",                   0, MDPARSER_EXT_AUTOLINK,                0, true),
    F("tagfilter",                  0, MDPARSER_EXT_TAGFILTER,               0, true),
    F("headingAnchors",             0, 0, MDPARSER_PP_HEADING_ANCHORS,          false),
    F("nofollowLinks",              0, 0, MDPARSER_PP_NOFOLLOW_LINKS,           false),
};

#undef F

#define MDPARSER_OPTIONS_FIELD_COUNT \
    (sizeof(mdparser_options_fields) / sizeof(mdparser_options_fields[0]))

void mdparser_options_init_defaults(void)
{
    int c = 0;
    int e = 0;
    int p = 0;

    for (size_t i = 0; i < MDPARSER_OPTIONS_FIELD_COUNT; i++) {
        const mdparser_options_field *f = &mdparser_options_fields[i];
        if (!f->default_value) {
            continue;
        }
        if (f->cmark_bit) {
            c |= f->cmark_bit;
        } else if (f->extension_bit) {
            e |= f->extension_bit;
        } else {
            p |= f->postprocess_bit;
        }
    }

    mdparser_default_cmark_options = c;
    mdparser_default_extension_mask = e;
    mdparser_default_postprocess_mask = p;
}

/* Write a 17-bool value vector into a freshly-allocated Options
 * object's properties. Used by __construct and by the static preset
 * factories. Safe to call only on an object whose properties are
 * still in their post-object_init_ex (IS_UNDEF) state, because
 * readonly enforcement allows first-writes within the declaring
 * class scope but rejects any subsequent assignment. */
static void mdparser_options_populate_object(zend_object *this_obj,
    const bool values[MDPARSER_OPTIONS_FIELD_COUNT])
{
    for (size_t i = 0; i < MDPARSER_OPTIONS_FIELD_COUNT; i++) {
        const mdparser_options_field *f = &mdparser_options_fields[i];
        zval tmp;
        ZVAL_BOOL(&tmp, values[i]);
        zend_update_property(mdparser_options_ce, this_obj,
            f->name, f->name_len, &tmp);
    }
}

static void mdparser_options_seed_defaults(bool values[MDPARSER_OPTIONS_FIELD_COUNT])
{
    for (size_t i = 0; i < MDPARSER_OPTIONS_FIELD_COUNT; i++) {
        values[i] = mdparser_options_fields[i].default_value;
    }
}

void mdparser_options_read_masks(zval *options_zv, int *cmark_options, int *extension_mask, int *postprocess_mask)
{
    int c = 0;
    int e = 0;
    int p = 0;
    zend_object *obj = Z_OBJ_P(options_zv);

    for (size_t i = 0; i < MDPARSER_OPTIONS_FIELD_COUNT; i++) {
        const mdparser_options_field *f = &mdparser_options_fields[i];
        zval *prop;
        zval rv;

        prop = zend_read_property(mdparser_options_ce, obj,
            f->name, f->name_len, 1, &rv);

        if (!prop || Z_TYPE_P(prop) != IS_TRUE) {
            continue;
        }

        if (f->cmark_bit) {
            c |= f->cmark_bit;
        } else if (f->extension_bit) {
            e |= f->extension_bit;
        } else {
            p |= f->postprocess_bit;
        }
    }

    *cmark_options = c;
    *extension_mask = e;
    *postprocess_mask = p;
}

void mdparser_options_register_class(void)
{
    mdparser_options_ce = register_class_MdParser_Options();
    mdparser_options_ce->ce_flags |= ZEND_ACC_NOT_SERIALIZABLE;
}

PHP_METHOD(MdParser_Options, __construct)
{
    bool values[MDPARSER_OPTIONS_FIELD_COUNT];

    /* Seed with stub defaults; ZPP only overwrites args that were
     * actually provided by the caller (Z_PARAM_BOOL is a no-op for
     * missing optional args), so unspecified fields keep their
     * table-driven default. */
    mdparser_options_seed_defaults(values);

    ZEND_PARSE_PARAMETERS_START(0, MDPARSER_OPTIONS_FIELD_COUNT)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(values[MDOPT_SOURCEPOS])
        Z_PARAM_BOOL(values[MDOPT_HARDBREAKS])
        Z_PARAM_BOOL(values[MDOPT_NOBREAKS])
        Z_PARAM_BOOL(values[MDOPT_SMART])
        Z_PARAM_BOOL(values[MDOPT_UNSAFE])
        Z_PARAM_BOOL(values[MDOPT_VALIDATE_UTF8])
        Z_PARAM_BOOL(values[MDOPT_GITHUB_PRE_LANG])
        Z_PARAM_BOOL(values[MDOPT_LIBERAL_HTML_TAG])
        Z_PARAM_BOOL(values[MDOPT_FOOTNOTES])
        Z_PARAM_BOOL(values[MDOPT_STRIKETHROUGH_DOUBLE_TILDE])
        Z_PARAM_BOOL(values[MDOPT_TABLE_PREFER_STYLE_ATTRIBUTES])
        Z_PARAM_BOOL(values[MDOPT_FULL_INFO_STRING])
        Z_PARAM_BOOL(values[MDOPT_TABLES])
        Z_PARAM_BOOL(values[MDOPT_STRIKETHROUGH])
        Z_PARAM_BOOL(values[MDOPT_TASKLIST])
        Z_PARAM_BOOL(values[MDOPT_AUTOLINK])
        Z_PARAM_BOOL(values[MDOPT_TAGFILTER])
        Z_PARAM_BOOL(values[MDOPT_HEADING_ANCHORS])
        Z_PARAM_BOOL(values[MDOPT_NOFOLLOW_LINKS])
    ZEND_PARSE_PARAMETERS_END();

    mdparser_options_populate_object(Z_OBJ_P(ZEND_THIS), values);
}

PHP_METHOD(MdParser_Options, strict)
{
    ZEND_PARSE_PARAMETERS_NONE();

    bool v[MDPARSER_OPTIONS_FIELD_COUNT];
    mdparser_options_seed_defaults(v);
    /* Bare URLs stay inert text instead of becoming live <a> tags. */
    v[MDOPT_AUTOLINK] = false;

    object_init_ex(return_value, mdparser_options_ce);
    mdparser_options_populate_object(Z_OBJ_P(return_value), v);
}

PHP_METHOD(MdParser_Options, github)
{
    ZEND_PARSE_PARAMETERS_NONE();

    bool v[MDPARSER_OPTIONS_FIELD_COUNT];
    mdparser_options_seed_defaults(v);
    /* github.com's renderer supports [^ref] / [^ref]: syntax; the
     * rest of the default set (tables/strike/tasklist/autolink/
     * tagfilter/githubPreLang) already matches github. */
    v[MDOPT_FOOTNOTES] = true;

    object_init_ex(return_value, mdparser_options_ce);
    mdparser_options_populate_object(Z_OBJ_P(return_value), v);
}

PHP_METHOD(MdParser_Options, permissive)
{
    ZEND_PARSE_PARAMETERS_NONE();

    bool v[MDPARSER_OPTIONS_FIELD_COUNT];
    mdparser_options_seed_defaults(v);
    /* Trusted-input mode: raw HTML passes through, tagfilter is off,
     * liberal tag parsing is on. Explicitly disables the XSS safety
     * net -- only for markdown the caller authored themselves. */
    v[MDOPT_UNSAFE] = true;
    v[MDOPT_TAGFILTER] = false;
    v[MDOPT_LIBERAL_HTML_TAG] = true;

    object_init_ex(return_value, mdparser_options_ce);
    mdparser_options_populate_object(Z_OBJ_P(return_value), v);
}
