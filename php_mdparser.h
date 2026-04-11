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

#ifndef PHP_MDPARSER_H
#define PHP_MDPARSER_H

#define PHP_MDPARSER_VERSION "0.1.0"

extern zend_module_entry mdparser_module_entry;
#define phpext_mdparser_ptr &mdparser_module_entry

#ifdef PHP_WIN32
#define PHP_MDPARSER_API __declspec(dllexport)
#else
#define PHP_MDPARSER_API
#endif

#include "php.h"

extern zend_class_entry *mdparser_parser_ce;
extern zend_class_entry *mdparser_options_ce;
extern zend_class_entry *mdparser_exception_ce;

typedef struct _mdparser_parser_obj {
    int cmark_options;
    int extension_mask;
    zend_object std;
} mdparser_parser_obj;

static inline mdparser_parser_obj *mdparser_parser_from_obj(zend_object *obj) {
    return (mdparser_parser_obj *)((char *)(obj) - XtOffsetOf(mdparser_parser_obj, std));
}

#define Z_MDPARSER_PARSER_P(zv) mdparser_parser_from_obj(Z_OBJ_P(zv))

/* Bits in extension_mask matching cmark-gfm core extension names. */
#define MDPARSER_EXT_TABLES        (1 << 0)
#define MDPARSER_EXT_STRIKETHROUGH (1 << 1)
#define MDPARSER_EXT_TASKLIST      (1 << 2)
#define MDPARSER_EXT_AUTOLINK      (1 << 3)
#define MDPARSER_EXT_TAGFILTER     (1 << 4)
#define MDPARSER_EXT_ALL ( \
    MDPARSER_EXT_TABLES | \
    MDPARSER_EXT_STRIKETHROUGH | \
    MDPARSER_EXT_TASKLIST | \
    MDPARSER_EXT_AUTOLINK | \
    MDPARSER_EXT_TAGFILTER)

/* Registration entry points (defined in the respective .c files) */
void mdparser_parser_register_class(void);
void mdparser_options_register_class(void);
void mdparser_exception_register_class(void);

/* Default-options helpers (defined in mdparser_options.c) */
void mdparser_options_default_masks(int *cmark_options, int *extension_mask);
void mdparser_options_read_masks(zval *options_zv, int *cmark_options, int *extension_mask);

/* PHP method declarations */
PHP_METHOD(MdParser_Parser, __construct);
PHP_METHOD(MdParser_Parser, toHtml);
PHP_METHOD(MdParser_Parser, toXml);
PHP_METHOD(MdParser_Options, __construct);

#endif /* PHP_MDPARSER_H */
