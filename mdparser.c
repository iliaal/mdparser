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
#include "ext/standard/info.h"

#include "php_mdparser.h"
#include "mdparser_md4c_html.h"
#include "mdparser_md4c_ast.h"
#include "mdparser_arginfo.h"

ZEND_DECLARE_MODULE_GLOBALS(mdparser)

/* ZEND_INI_GET_ADDR() rather than MDPARSER_G(): this handler runs from
 * zend_register_ini_entries_ex() inside MINIT, and under ZTS the accessor
 * would dereference the globals id before the engine has bound it. */
static ZEND_INI_MH(mdparser_on_update_parse_memory_limit)
{
    zend_string *errstr = NULL;
    zend_long bytes = zend_ini_parse_quantity(new_value, &errstr);
    zend_long *target;

    if (errstr) {
        zend_string_release(errstr);
        return FAILURE;
    }

    target = (zend_long *) ZEND_INI_GET_ADDR();
    /* Negative reads as "no limit", matching memory_limit=-1. */
    *target = bytes > 0 ? bytes : 0;
    return SUCCESS;
}

PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("mdparser.parse_memory_limit", "128M", PHP_INI_ALL,
        mdparser_on_update_parse_memory_limit, parse_memory_limit,
        zend_mdparser_globals, mdparser_globals)
PHP_INI_END()

static PHP_GINIT_FUNCTION(mdparser)
{
#if defined(COMPILE_DL_MDPARSER) && defined(ZTS)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif
    memset(mdparser_globals, 0, sizeof(*mdparser_globals));
}

/* md4c is a stateless push parser with no global registry and no allocator
 * hook, so MINIT only registers the INI entry, precomputes the default option
 * masks, and registers the classes. */
PHP_MINIT_FUNCTION(mdparser)
{
    REGISTER_INI_ENTRIES();

    mdparser_options_init_defaults();
    mdparser_md4c_html_minit();
    mdparser_md4c_ast_minit();

    mdparser_exception_register_class();
    mdparser_options_register_class();
    mdparser_parser_register_class();

    /* Reject dynamic properties: Options is readonly and Parser exposes only
     * a readonly $options, so a typo'd `$o->headingAnchor = true` should be a
     * hard Error, not a silently-ignored dynamic property the parser never
     * reads. (gen_stub's readonly-class flag does not imply this for internal
     * classes.) */
    mdparser_options_ce->ce_flags |= ZEND_ACC_NO_DYNAMIC_PROPERTIES;
    mdparser_parser_ce->ce_flags |= ZEND_ACC_NO_DYNAMIC_PROPERTIES;
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(mdparser)
{
    UNREGISTER_INI_ENTRIES();
    return SUCCESS;
}

PHP_MINFO_FUNCTION(mdparser)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "mdparser support", "enabled");
    php_info_print_table_row(2, "mdparser version", PHP_MDPARSER_VERSION);
    php_info_print_table_row(2, "backend", "md4c");
    php_info_print_table_row(2, "md4c version", MDPARSER_MD4C_VERSION);
    php_info_print_table_end();

    DISPLAY_INI_ENTRIES();
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
    PHP_MODULE_GLOBALS(mdparser),
    PHP_GINIT(mdparser),
    NULL, /* GSHUTDOWN */
    NULL, /* post deactivate */
    STANDARD_MODULE_PROPERTIES_EX
};

#ifdef COMPILE_DL_MDPARSER
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(mdparser)
#endif
