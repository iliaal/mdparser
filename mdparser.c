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
#include "php_ini.h"
#include "ext/standard/info.h"

#include "php_mdparser.h"
#include "mdparser_arginfo.h"

#include "cmark-gfm.h"
#include "cmark-gfm-core-extensions.h"
#include "registry.h"

mdparser_cached_extension mdparser_cached_extensions[MDPARSER_EXT_COUNT];

zend_string *mdparser_str_type;
zend_string *mdparser_str_children;
zend_string *mdparser_str_literal;
zend_string *mdparser_str_info;
zend_string *mdparser_str_url;
zend_string *mdparser_str_title;
zend_string *mdparser_str_level;
zend_string *mdparser_str_list_type;
zend_string *mdparser_str_list_start;
zend_string *mdparser_str_list_tight;
zend_string *mdparser_str_list_delim;
zend_string *mdparser_str_alignments;
zend_string *mdparser_str_is_header;
zend_string *mdparser_str_checked;
zend_string *mdparser_str_start_line;
zend_string *mdparser_str_start_column;
zend_string *mdparser_str_end_line;
zend_string *mdparser_str_end_column;

static int mdparser_resolve_extensions(void)
{
    static const struct {
        int bit;
        const char *name;
    } wanted[MDPARSER_EXT_COUNT] = {
        { MDPARSER_EXT_TABLES,        "table" },
        { MDPARSER_EXT_STRIKETHROUGH, "strikethrough" },
        { MDPARSER_EXT_TASKLIST,      "tasklist" },
        { MDPARSER_EXT_AUTOLINK,      "autolink" },
        { MDPARSER_EXT_TAGFILTER,     "tagfilter" },
    };

    for (int i = 0; i < MDPARSER_EXT_COUNT; i++) {
        cmark_syntax_extension *ext = cmark_find_syntax_extension(wanted[i].name);
        if (!ext) {
            php_error_docref(NULL, E_CORE_ERROR,
                "mdparser: required cmark-gfm extension '%s' missing from registry",
                wanted[i].name);
            return FAILURE;
        }
        mdparser_cached_extensions[i].bit = wanted[i].bit;
        mdparser_cached_extensions[i].ptr = ext;
    }
    return SUCCESS;
}

static void mdparser_init_interned_strings(void)
{
    mdparser_str_type         = zend_string_init_interned("type",         sizeof("type") - 1,         1);
    mdparser_str_children     = zend_string_init_interned("children",     sizeof("children") - 1,     1);
    mdparser_str_literal      = zend_string_init_interned("literal",      sizeof("literal") - 1,      1);
    mdparser_str_info         = zend_string_init_interned("info",         sizeof("info") - 1,         1);
    mdparser_str_url          = zend_string_init_interned("url",          sizeof("url") - 1,          1);
    mdparser_str_title        = zend_string_init_interned("title",        sizeof("title") - 1,        1);
    mdparser_str_level        = zend_string_init_interned("level",        sizeof("level") - 1,        1);
    mdparser_str_list_type    = zend_string_init_interned("list_type",    sizeof("list_type") - 1,    1);
    mdparser_str_list_start   = zend_string_init_interned("list_start",   sizeof("list_start") - 1,   1);
    mdparser_str_list_tight   = zend_string_init_interned("list_tight",   sizeof("list_tight") - 1,   1);
    mdparser_str_list_delim   = zend_string_init_interned("list_delim",   sizeof("list_delim") - 1,   1);
    mdparser_str_alignments   = zend_string_init_interned("alignments",   sizeof("alignments") - 1,   1);
    mdparser_str_is_header    = zend_string_init_interned("is_header",    sizeof("is_header") - 1,    1);
    mdparser_str_checked      = zend_string_init_interned("checked",      sizeof("checked") - 1,      1);
    mdparser_str_start_line   = zend_string_init_interned("start_line",   sizeof("start_line") - 1,   1);
    mdparser_str_start_column = zend_string_init_interned("start_column", sizeof("start_column") - 1, 1);
    mdparser_str_end_line     = zend_string_init_interned("end_line",     sizeof("end_line") - 1,     1);
    mdparser_str_end_column   = zend_string_init_interned("end_column",   sizeof("end_column") - 1,   1);
}

PHP_MINIT_FUNCTION(mdparser)
{
    cmark_gfm_core_extensions_ensure_registered();

    if (mdparser_resolve_extensions() == FAILURE) {
        return FAILURE;
    }

    mdparser_options_init_defaults();
    mdparser_init_interned_strings();

    mdparser_exception_register_class();
    mdparser_options_register_class();
    mdparser_parser_register_class();
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(mdparser)
{
    cmark_release_plugins();
    return SUCCESS;
}

PHP_MINFO_FUNCTION(mdparser)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "mdparser support", "enabled");
    php_info_print_table_row(2, "mdparser version", PHP_MDPARSER_VERSION);
    php_info_print_table_row(2, "cmark-gfm version", CMARK_GFM_VERSION_STRING);
    php_info_print_table_end();
}

static const zend_function_entry mdparser_functions[] = {
    PHP_FE_END
};

zend_module_entry mdparser_module_entry = {
    STANDARD_MODULE_HEADER,
    "mdparser",
    mdparser_functions,
    PHP_MINIT(mdparser),
    PHP_MSHUTDOWN(mdparser),
    NULL, /* RINIT */
    NULL, /* RSHUTDOWN */
    PHP_MINFO(mdparser),
    PHP_MDPARSER_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_MDPARSER
ZEND_GET_MODULE(mdparser)
#endif
