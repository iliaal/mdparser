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
#include "php_ini.h"
#include "ext/standard/info.h"

#include "php_mdparser.h"
#include "mdparser_arginfo.h"

#include "cmark-gfm.h"
#include "cmark-gfm-core-extensions.h"
#include "registry.h"

#include "mdparser_ast.h"

mdparser_cached_extension mdparser_cached_extensions[MDPARSER_EXT_COUNT];

/* cmark's default allocator wraps system calloc/realloc/free and
 * abort()s on allocation failure. That is unacceptable for a server
 * extension: hostile or oversized input can pin memory outside Zend's
 * memory_limit and tear down the worker on OOM. Routing every cmark
 * allocation through Zend MM gives us memory_limit accounting, debug
 * tracking, and a controlled bailout (longjmp via E_ERROR) in place
 * of abort. Allocations are request-scoped because every call site
 * lives inside a PHP method invocation; Zend MM cleans them up at
 * request shutdown if a bailout fires mid-parse. */
static void *mdparser_zend_calloc(size_t nmemb, size_t size)
{
    return ecalloc(nmemb, size);
}

static void *mdparser_zend_realloc(void *ptr, size_t size)
{
    return erealloc(ptr, size);
}

static void mdparser_zend_free(void *ptr)
{
    /* efree handles NULL safely in modern Zend MM (zend_mm_free_heap
     * page-offset check; verified against php-src). cmark's free
     * callback contract also says NULL is never passed. No guard. */
    efree(ptr);
}

cmark_mem mdparser_zend_mem = {
    mdparser_zend_calloc,
    mdparser_zend_realloc,
    mdparser_zend_free,
};

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
    mdparser_init_ast_strings();

    mdparser_exception_register_class();
    mdparser_options_register_class();
    mdparser_parser_register_class();
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(mdparser)
{
    cmark_release_plugins();
    /* cmark_release_plugins frees the syntax_extension structs the
     * cached pointers reference. Zero the cache so a hypothetical
     * re-MINIT (embedded SAPI / test harness) starts clean instead of
     * dereferencing freed memory before mdparser_resolve_extensions
     * reseats the array. */
    memset(mdparser_cached_extensions, 0, sizeof(mdparser_cached_extensions));
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
