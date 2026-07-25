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

/* md4c is a stateless push parser with no global registry and no allocator
 * hook, so MINIT only precomputes the default option masks and registers the
 * classes; there is nothing to tear down at MSHUTDOWN. */
PHP_MINIT_FUNCTION(mdparser)
{
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

PHP_MINFO_FUNCTION(mdparser)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "mdparser support", "enabled");
    php_info_print_table_row(2, "mdparser version", PHP_MDPARSER_VERSION);
    php_info_print_table_row(2, "backend", "md4c");
    php_info_print_table_row(2, "md4c version", MDPARSER_MD4C_VERSION);
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
    NULL, /* MSHUTDOWN: md4c has no global state to release */
    NULL, /* RINIT */
    NULL, /* RSHUTDOWN */
    PHP_MINFO(mdparser),
    PHP_MDPARSER_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_MDPARSER
ZEND_GET_MODULE(mdparser)
#endif
