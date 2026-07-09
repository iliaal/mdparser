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

#ifndef PHP_MDPARSER_H
#define PHP_MDPARSER_H

#define PHP_MDPARSER_VERSION "0.4.2"

/* Bundled md4c version: release-0.5.3 plus master commit 755ce49
 * (vendored 2026-06-17). Keep in sync with vendor/VENDOR.md. */
#define MDPARSER_MD4C_VERSION "0.5.3+git755ce49"

extern zend_module_entry mdparser_module_entry;
#define phpext_mdparser_ptr &mdparser_module_entry

#ifdef PHP_WIN32
#define PHP_MDPARSER_API __declspec(dllexport)
#else
#define PHP_MDPARSER_API
#endif

#include "php.h"

/* Pre-8.4 compat shim for zend_register_internal_class_with_flags
 * (added in 8.4). gen_stub.php emits the 8.4+ variant when it sees
 * `final readonly class` in the stub, but we still target 8.2+.
 * Providing a static inline fallback keeps the generated arginfo.h
 * unchanged and lets 8.2/8.3 builds compile and link. */
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
    /* md4c backend masks: parser flags (MD_FLAG_*) and render-behavior bits
     * (MDPARSER_RF_*), precomputed from the constructed Options. md4c is a
     * stateless push parser, so there is no per-instance parser to cache;
     * each render is a one-shot md_parse over a fresh callback consumer. */
    unsigned md4c_pflags;
    int md4c_ropts;
    zend_object std;
} mdparser_parser_obj;

static inline mdparser_parser_obj *mdparser_parser_from_obj(zend_object *obj) {
    return (mdparser_parser_obj *)((char *)(obj) - offsetof(mdparser_parser_obj, std));
}

#define Z_MDPARSER_PARSER_P(zv) mdparser_parser_from_obj(Z_OBJ_P(zv))

/* Hard cap on input size. 256 MB is far above any realistic document. */
#define MDPARSER_MAX_INPUT_SIZE ((size_t)(256UL * 1024UL * 1024UL))

/* Hard cap on AST builder nesting depth. md4c parses iteratively and can
 * emit trees thousands of levels deep from one-byte-per-level input like
 * `>` × N; the zval-stack AST builder caps depth to avoid a C-stack smash. */
#define MDPARSER_MAX_AST_DEPTH 1000

/* Default md4c masks, computed once from mdparser_options_fields at MINIT. */
extern unsigned mdparser_default_md4c_pflags;
extern int mdparser_default_md4c_ropts;

/* Registration entry points (defined in the respective .c files) */
void mdparser_parser_register_class(void);
void mdparser_options_register_class(void);
void mdparser_exception_register_class(void);

/* Default-options helpers (defined in mdparser_options.c) */
void mdparser_options_init_defaults(void);
void mdparser_options_read_masks(zval *options_zv, unsigned *md4c_pflags, int *md4c_ropts);

#endif /* PHP_MDPARSER_H */
