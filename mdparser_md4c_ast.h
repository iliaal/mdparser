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

#ifndef MDPARSER_MD4C_AST_H
#define MDPARSER_MD4C_AST_H

#include "php.h"

/* Build the PHP-array AST for `src` via md4c with `parser_flags`. On success
 * sets *status to 0 and writes the document node array into return_value. On
 * failure sets *status non-zero (see mdparser_md4c_ast_status_message) and
 * leaves return_value untouched. */
void mdparser_md4c_render_ast(const char *src, size_t len, unsigned parser_flags,
    bool validate_utf8, zval *return_value, int *status);

const char *mdparser_md4c_ast_status_message(int status);

/* Intern the recurring per-node hash keys once at module init. */
void mdparser_md4c_ast_minit(void);

#endif
