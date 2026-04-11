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

#define PHP_MDPARSER_VERSION "0.1.1"

extern zend_module_entry mdparser_module_entry;
#define phpext_mdparser_ptr &mdparser_module_entry

#ifdef PHP_WIN32
#define PHP_MDPARSER_API __declspec(dllexport)
#else
#define PHP_MDPARSER_API
#endif

#include "php.h"
#include "cmark-gfm.h"

/* PHP 8.3 compat shim for zend_register_internal_class_with_flags
 * (added in 8.4). gen_stub.php emits the 8.4+ variant when it sees
 * `final readonly class` in the stub, but we still target 8.3.
 * Providing a static inline fallback keeps the generated arginfo.h
 * unchanged and lets 8.3 builds compile and link. */
#if PHP_VERSION_ID < 80400
static inline zend_class_entry *zend_register_internal_class_with_flags(
    zend_class_entry *class_entry,
    zend_class_entry *parent_ce,
    uint32_t flags)
{
    zend_class_entry *registered = zend_register_internal_class_ex(class_entry, parent_ce);
    registered->ce_flags |= flags;
    return registered;
}
#endif

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

#define MDPARSER_EXT_COUNT 5

/* Hard cap on input size. 256 MB is far above any realistic document
 * and well below cmark's internal bufsize_t (int32) overflow edge. */
#define MDPARSER_MAX_INPUT_SIZE ((size_t)(256UL * 1024UL * 1024UL))

/* Hard cap on AST walker recursion depth. cmark's own parser is
 * iterative and happily produces trees thousands of levels deep from
 * one-byte-per-level input like `>` × N. Walking such a tree with
 * a recursive PHP-array builder would smash the C stack. */
#define MDPARSER_MAX_AST_DEPTH 1000

/* Extension pointers resolved once at MINIT so per-parse attachment
 * is a bitmask loop instead of 5 registry walks with strcmp. */
typedef struct {
    int bit;
    cmark_syntax_extension *ptr;
} mdparser_cached_extension;

extern mdparser_cached_extension mdparser_cached_extensions[MDPARSER_EXT_COUNT];

/* Default masks, computed once from mdparser_options_fields at MINIT. */
extern int mdparser_default_cmark_options;
extern int mdparser_default_extension_mask;

/* Registration entry points (defined in the respective .c files) */
void mdparser_parser_register_class(void);
void mdparser_options_register_class(void);
void mdparser_exception_register_class(void);

/* Default-options helpers (defined in mdparser_options.c) */
void mdparser_options_init_defaults(void);
void mdparser_options_default_masks(int *cmark_options, int *extension_mask);
void mdparser_options_read_masks(zval *options_zv, int *cmark_options, int *extension_mask);

/* PHP method declarations */
PHP_METHOD(MdParser_Parser, __construct);
PHP_METHOD(MdParser_Parser, toHtml);
PHP_METHOD(MdParser_Parser, toXml);
PHP_METHOD(MdParser_Parser, toAst);
PHP_METHOD(MdParser_Options, __construct);

#endif /* PHP_MDPARSER_H */
