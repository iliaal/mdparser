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

#include "mdparser_ast.h"

mdparser_cached_extension mdparser_cached_extensions[MDPARSER_EXT_COUNT];

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

PHP_MINIT_FUNCTION(mdparser)
{
    cmark_gfm_core_extensions_ensure_registered();

    if (mdparser_resolve_extensions() == FAILURE) {
        return FAILURE;
    }

    mdparser_options_init_defaults();

    mdparser_exception_register_class();
    mdparser_options_register_class();
    mdparser_parser_register_class();
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(mdparser)
{
    mdparser_release_ast_strings();
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
