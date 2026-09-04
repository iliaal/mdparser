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

#include "php_mdparser.h"
#include "mdparser_arginfo.h"

#include "md4c.h"
#include "mdparser_md4c_html.h"

zend_class_entry *mdparser_options_ce;

unsigned mdparser_default_md4c_pflags = 0;
int mdparser_default_md4c_ropts = 0;

/* Each Options property maps to an md4c parser flag (MD_FLAG_*), a renderer
 * behavior bit (MDPARSER_RF_*), or neither (accepted-but-inert legacy options).
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
    MDOPT_NO_INDENTED_CODE_BLOCKS,
    MDOPT_PERMISSIVE_ATX_HEADINGS,
    MDOPT_COLLAPSE_WHITESPACE,
    MDOPT_UNDERLINE,
    MDOPT_HIGHLIGHT,
    MDOPT_SUPERSCRIPT,
    MDOPT_SUBSCRIPT,
    MDOPT_SPOILERS,
    MDOPT_LATEX_MATH,
    MDOPT_WIKILINKS,
    MDOPT_ADMONITIONS,
    MDOPT_INSERT,
    MDOPT_PRESERVE_BLANK_LINES,
    MDOPT_COUNT_
};

typedef struct {
    const char *name;
    size_t name_len;
    unsigned md4c_pflag;    /* md4c MD_FLAG_* contribution (parser flag) */
    int md4c_ropt;          /* md4c MDPARSER_RF_* contribution (renderer) */
    bool default_value;
} mdparser_options_field;

/* Fields with md4c_pflag==0 && md4c_ropt==0 have no md4c analog and are
 * accepted-but-inert (kept for API compatibility): sourcepos (md4c exposes
 * no source positions), githubPreLang, liberalHtmlTag, strikethroughDoubleTilde,
 * tablePreferStyleAttributes, and fullInfoString (legacy renderer options with
 * no md4c equivalent). */
#define F(name_, mpf_, mro_, def_) \
    { name_, sizeof(name_) - 1, mpf_, mro_, def_ }

static const mdparser_options_field mdparser_options_fields[] = {
    F("sourcepos",                  0, 0, false),
    F("hardbreaks",                 MD_FLAG_HARD_SOFT_BREAKS, 0, false),
    F("nobreaks",                   0, MDPARSER_RF_NOBREAKS, false),
    F("smart",                      0, MDPARSER_RF_SMART, false),
    F("unsafe",                     0, MDPARSER_RF_UNSAFE, false),
    F("validateUtf8",               0, MDPARSER_RF_VALIDATE_UTF8, true),
    F("githubPreLang",              0, 0, true),
    F("liberalHtmlTag",             0, 0, false),
    F("footnotes",                  MD_FLAG_FOOTNOTES, 0, false),
    F("strikethroughDoubleTilde",   0, 0, false),
    F("tablePreferStyleAttributes", 0, 0, false),
    F("fullInfoString",             0, 0, false),
    F("tables",                     MD_FLAG_TABLES, 0, true),
    F("strikethrough",              MD_FLAG_STRIKETHROUGH, 0, true),
    F("tasklist",                   MD_FLAG_TASKLISTS, 0, true),
    F("autolink",                   MD_FLAG_PERMISSIVEAUTOLINKS, 0, true),
    F("tagfilter",                  0, MDPARSER_RF_TAGFILTER, true),
    F("headingAnchors",             0, MDPARSER_RF_HEADING_ANCHORS, false),
    F("nofollowLinks",              0, MDPARSER_RF_NOFOLLOW, false),
    F("noIndentedCodeBlocks",       MD_FLAG_NOINDENTEDCODEBLOCKS, 0, false),
    F("permissiveAtxHeadings",      MD_FLAG_PERMISSIVEATXHEADERS, 0, false),
    F("collapseWhitespace",         MD_FLAG_COLLAPSEWHITESPACE, 0, false),
    F("underline",                  MD_FLAG_UNDERLINE, 0, false),
    F("highlight",                  MD_FLAG_HIGHLIGHT, 0, false),
    F("superscript",                MD_FLAG_SUPERSCRIPTS, 0, false),
    F("subscript",                  MD_FLAG_SUBSCRIPTS, 0, false),
    F("spoilers",                   MD_FLAG_SPOILERS, 0, false),
    F("latexMath",                  MD_FLAG_LATEXMATHSPANS, 0, false),
    F("wikiLinks",                  MD_FLAG_WIKILINKS, 0, false),
    F("admonitions",                MD_FLAG_ADMONITIONS, 0, false),
    F("insert",                     MD_FLAG_INSERT, 0, false),
    F("preserveBlankLines",         MD_FLAG_PRESERVEBLANKLINES, 0, false),
};

#undef F

#define MDPARSER_OPTIONS_FIELD_COUNT \
    (sizeof(mdparser_options_fields) / sizeof(mdparser_options_fields[0]))

/* If a new option is inserted in either the enum or the field table
 * without updating its sibling, the preset factories silently target
 * the wrong bit. A misaligned MDOPT_UNSAFE would flip the XSS safety
 * default for permissive() / strict() / github(); pin the alignment
 * at compile time.
 *
 * MSVC-cl in default C mode rejects _Static_assert (C11) without
 * /std:c11, and the PHP Windows build harness doesn't pass that flag,
 * so use the portable negative-array-size idiom instead. */
typedef char mdparser_options_field_count_assert[
    (MDOPT_COUNT_ == MDPARSER_OPTIONS_FIELD_COUNT) ? 1 : -1];
/* The ZPP block in MdParser_Options::__construct takes exactly one
 * Z_PARAM_BOOL per option (32 today). ZEND_PARSE_PARAMETERS_START's max
 * arg is MDPARSER_OPTIONS_FIELD_COUNT, so adding option 33 without adding
 * its Z_PARAM_BOOL line would still compile and silently leave the new
 * option unsettable. Pin the arity here: bump the 32 alongside the ZPP
 * block when adding an option. */
typedef char mdparser_options_zpp_arity_assert[
    (MDPARSER_OPTIONS_FIELD_COUNT == 32) ? 1 : -1];

void mdparser_options_init_defaults(void)
{
    unsigned mpf = 0;
    int mro = 0;

    for (size_t i = 0; i < MDPARSER_OPTIONS_FIELD_COUNT; i++) {
        const mdparser_options_field *f = &mdparser_options_fields[i];
        if (!f->default_value) {
            continue;
        }
        mpf |= f->md4c_pflag;
        mro |= f->md4c_ropt;
    }

    mdparser_default_md4c_pflags = mpf;
    mdparser_default_md4c_ropts = mro;
}

/* Write the value vector into a freshly-allocated Options object's
 * properties (one bool per MDOPT_* index, sized via
 * MDPARSER_OPTIONS_FIELD_COUNT so additions don't drift this
 * signature). Used by __construct and by the static preset factories.
 * Safe to call only on an object whose properties are still in their
 * post-object_init_ex (IS_UNDEF) state, because readonly enforcement
 * allows first-writes within the declaring class scope but rejects
 * any subsequent assignment. */
static void mdparser_options_populate_object(zend_object *this_obj,
    const bool values[MDPARSER_OPTIONS_FIELD_COUNT])
{
    for (size_t i = 0; i < MDPARSER_OPTIONS_FIELD_COUNT; i++) {
        const mdparser_options_field *f = &mdparser_options_fields[i];
        zval tmp;
        ZVAL_BOOL(&tmp, values[i]);
        zend_update_property(mdparser_options_ce, this_obj,
            f->name, f->name_len, &tmp);
        /* On a readonly-reentry violation the first write throws; stop so the
         * reported property is the first failure, not the last field. */
        if (EG(exception)) return;
    }
}

static void mdparser_options_seed_defaults(bool values[MDPARSER_OPTIONS_FIELD_COUNT])
{
    for (size_t i = 0; i < MDPARSER_OPTIONS_FIELD_COUNT; i++) {
        values[i] = mdparser_options_fields[i].default_value;
    }
}

typedef void (*mdparser_options_modifier)(bool v[MDPARSER_OPTIONS_FIELD_COUNT]);

static void mdparser_options_strict_mod(bool v[MDPARSER_OPTIONS_FIELD_COUNT]);
static void mdparser_options_github_mod(bool v[MDPARSER_OPTIONS_FIELD_COUNT]);
static void mdparser_options_permissive_mod(bool v[MDPARSER_OPTIONS_FIELD_COUNT]);

static void mdparser_options_preset(zval *return_value, mdparser_options_modifier modify)
{
    bool v[MDPARSER_OPTIONS_FIELD_COUNT];
    mdparser_options_seed_defaults(v);
    modify(v);
    object_init_ex(return_value, mdparser_options_ce);
    mdparser_options_populate_object(Z_OBJ_P(return_value), v);
}

void mdparser_options_read_masks(zval *options_zv, unsigned *md4c_pflags, int *md4c_ropts)
{
    unsigned mpf = 0;
    int mro = 0;
    zend_object *obj = Z_OBJ_P(options_zv);

    for (size_t i = 0; i < MDPARSER_OPTIONS_FIELD_COUNT; i++) {
        const mdparser_options_field *f = &mdparser_options_fields[i];
        zval *prop;
        zval rv;

        prop = zend_read_property(mdparser_options_ce, obj,
            f->name, f->name_len, 1, &rv);

        /* Options is final + readonly with typed bool properties; the
         * only way to land here with anything other than IS_TRUE /
         * IS_FALSE is to skip __construct (e.g. via
         * ReflectionClass::newInstanceWithoutConstructor). Silent
         * reads of an uninit typed property return &EG(uninitialized_zval)
         * (IS_NULL), so an IS_UNDEF check alone would miss this case.
         * Treating uninit as false would silently flip the safety
         * defaults (validateUtf8 / tagfilter) off while $parser->options
         * remains unreadable. Reject the object outright. */
        if (UNEXPECTED(!prop ||
                (Z_TYPE_P(prop) != IS_TRUE && Z_TYPE_P(prop) != IS_FALSE))) {
            zend_throw_exception_ex(mdparser_exception_ce, 0,
                "mdparser: Options::$%s is uninitialized; "
                "Options instances must be constructed via __construct() "
                "(or one of strict()/github()/permissive())",
                f->name);
            return;
        }

        if (Z_TYPE_P(prop) != IS_TRUE) {
            continue;
        }

        mpf |= f->md4c_pflag;
        mro |= f->md4c_ropt;
    }

    *md4c_pflags = mpf;
    *md4c_ropts = mro;
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
        Z_PARAM_BOOL(values[MDOPT_NO_INDENTED_CODE_BLOCKS])
        Z_PARAM_BOOL(values[MDOPT_PERMISSIVE_ATX_HEADINGS])
        Z_PARAM_BOOL(values[MDOPT_COLLAPSE_WHITESPACE])
        Z_PARAM_BOOL(values[MDOPT_UNDERLINE])
        Z_PARAM_BOOL(values[MDOPT_HIGHLIGHT])
        Z_PARAM_BOOL(values[MDOPT_SUPERSCRIPT])
        Z_PARAM_BOOL(values[MDOPT_SUBSCRIPT])
        Z_PARAM_BOOL(values[MDOPT_SPOILERS])
        Z_PARAM_BOOL(values[MDOPT_LATEX_MATH])
        Z_PARAM_BOOL(values[MDOPT_WIKILINKS])
        Z_PARAM_BOOL(values[MDOPT_ADMONITIONS])
        Z_PARAM_BOOL(values[MDOPT_INSERT])
        Z_PARAM_BOOL(values[MDOPT_PRESERVE_BLANK_LINES])
    ZEND_PARSE_PARAMETERS_END();

    mdparser_options_populate_object(Z_OBJ_P(ZEND_THIS), values);
}

PHP_METHOD(MdParser_Options, strict)
{
    ZEND_PARSE_PARAMETERS_NONE();
    mdparser_options_preset(return_value, mdparser_options_strict_mod);
}

static void mdparser_options_strict_mod(bool v[MDPARSER_OPTIONS_FIELD_COUNT])
{
    /* Bare URLs stay inert text instead of becoming live <a> tags. */
    v[MDOPT_AUTOLINK] = false;
}

PHP_METHOD(MdParser_Options, github)
{
    ZEND_PARSE_PARAMETERS_NONE();
    mdparser_options_preset(return_value, mdparser_options_github_mod);
}

static void mdparser_options_github_mod(bool v[MDPARSER_OPTIONS_FIELD_COUNT])
{
    /* github.com's renderer supports footnotes and [!NOTE]-style alerts;
     * the rest of the default set already matches github. */
    v[MDOPT_FOOTNOTES] = true;
    v[MDOPT_ADMONITIONS] = true;
}

PHP_METHOD(MdParser_Options, permissive)
{
    ZEND_PARSE_PARAMETERS_NONE();
    mdparser_options_preset(return_value, mdparser_options_permissive_mod);
}

static void mdparser_options_permissive_mod(bool v[MDPARSER_OPTIONS_FIELD_COUNT])
{
    /* Trusted-input mode: raw HTML passes through and tagfilter is off.
     * Explicitly disables the XSS safety net -- only for markdown the
     * caller authored themselves. */
    v[MDOPT_UNSAFE] = true;
    v[MDOPT_TAGFILTER] = false;
}
