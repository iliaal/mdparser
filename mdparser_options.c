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
typedef struct {
    const char *name;
    size_t name_len;
    int cmark_bit;          /* 0 if this maps to an extension instead */
    int extension_bit;      /* 0 if this maps to a cmark option instead */
    bool default_value;
} mdparser_options_field;

#define F(name_, cmark_, ext_, def_) \
    { name_, sizeof(name_) - 1, cmark_, ext_, def_ }

static const mdparser_options_field mdparser_options_fields[] = {
    F("sourcepos",                  CMARK_OPT_SOURCEPOS,                   0, false),
    F("hardbreaks",                 CMARK_OPT_HARDBREAKS,                  0, false),
    F("nobreaks",                   CMARK_OPT_NOBREAKS,                    0, false),
    F("smart",                      CMARK_OPT_SMART,                       0, false),
    F("unsafe",                     CMARK_OPT_UNSAFE,                      0, false),
    F("validateUtf8",               CMARK_OPT_VALIDATE_UTF8,               0, true),
    F("githubPreLang",              CMARK_OPT_GITHUB_PRE_LANG,             0, true),
    F("liberalHtmlTag",             CMARK_OPT_LIBERAL_HTML_TAG,            0, false),
    F("footnotes",                  CMARK_OPT_FOOTNOTES,                   0, false),
    F("strikethroughDoubleTilde",   CMARK_OPT_STRIKETHROUGH_DOUBLE_TILDE,  0, false),
    F("tablePreferStyleAttributes", CMARK_OPT_TABLE_PREFER_STYLE_ATTRIBUTES, 0, false),
    F("fullInfoString",             CMARK_OPT_FULL_INFO_STRING,            0, false),
    F("tables",                     0, MDPARSER_EXT_TABLES,                true),
    F("strikethrough",              0, MDPARSER_EXT_STRIKETHROUGH,         true),
    F("tasklist",                   0, MDPARSER_EXT_TASKLIST,              true),
    F("autolink",                   0, MDPARSER_EXT_AUTOLINK,              true),
    F("tagfilter",                  0, MDPARSER_EXT_TAGFILTER,             true),
};

#undef F

#define MDPARSER_OPTIONS_FIELD_COUNT \
    (sizeof(mdparser_options_fields) / sizeof(mdparser_options_fields[0]))

void mdparser_options_init_defaults(void)
{
    int c = 0;
    int e = 0;

    for (size_t i = 0; i < MDPARSER_OPTIONS_FIELD_COUNT; i++) {
        const mdparser_options_field *f = &mdparser_options_fields[i];
        if (!f->default_value) {
            continue;
        }
        if (f->cmark_bit) {
            c |= f->cmark_bit;
        } else {
            e |= f->extension_bit;
        }
    }

    mdparser_default_cmark_options = c;
    mdparser_default_extension_mask = e;
}

void mdparser_options_default_masks(int *cmark_options, int *extension_mask)
{
    *cmark_options = mdparser_default_cmark_options;
    *extension_mask = mdparser_default_extension_mask;
}

void mdparser_options_read_masks(zval *options_zv, int *cmark_options, int *extension_mask)
{
    int c = 0;
    int e = 0;
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
        } else {
            e |= f->extension_bit;
        }
    }

    *cmark_options = c;
    *extension_mask = e;
}

void mdparser_options_register_class(void)
{
    mdparser_options_ce = register_class_MdParser_Options();
}

PHP_METHOD(MdParser_Options, __construct)
{
    bool values[MDPARSER_OPTIONS_FIELD_COUNT];

    /* Populate defaults so ZPP doesn't overwrite unspecified fields. */
    for (size_t i = 0; i < MDPARSER_OPTIONS_FIELD_COUNT; i++) {
        values[i] = mdparser_options_fields[i].default_value;
    }

    ZEND_PARSE_PARAMETERS_START(0, MDPARSER_OPTIONS_FIELD_COUNT)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(values[0])  /* sourcepos */
        Z_PARAM_BOOL(values[1])  /* hardbreaks */
        Z_PARAM_BOOL(values[2])  /* nobreaks */
        Z_PARAM_BOOL(values[3])  /* smart */
        Z_PARAM_BOOL(values[4])  /* unsafe */
        Z_PARAM_BOOL(values[5])  /* validateUtf8 */
        Z_PARAM_BOOL(values[6])  /* githubPreLang */
        Z_PARAM_BOOL(values[7])  /* liberalHtmlTag */
        Z_PARAM_BOOL(values[8])  /* footnotes */
        Z_PARAM_BOOL(values[9])  /* strikethroughDoubleTilde */
        Z_PARAM_BOOL(values[10]) /* tablePreferStyleAttributes */
        Z_PARAM_BOOL(values[11]) /* fullInfoString */
        Z_PARAM_BOOL(values[12]) /* tables */
        Z_PARAM_BOOL(values[13]) /* strikethrough */
        Z_PARAM_BOOL(values[14]) /* tasklist */
        Z_PARAM_BOOL(values[15]) /* autolink */
        Z_PARAM_BOOL(values[16]) /* tagfilter */
    ZEND_PARSE_PARAMETERS_END();

    zend_object *this_obj = Z_OBJ_P(ZEND_THIS);

    for (size_t i = 0; i < MDPARSER_OPTIONS_FIELD_COUNT; i++) {
        const mdparser_options_field *f = &mdparser_options_fields[i];
        zval tmp;
        ZVAL_BOOL(&tmp, values[i]);
        zend_update_property(mdparser_options_ce, this_obj,
            f->name, f->name_len, &tmp);
    }
}
