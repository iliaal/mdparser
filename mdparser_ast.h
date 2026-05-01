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

#ifndef PHP_MDPARSER_AST_H
#define PHP_MDPARSER_AST_H

#include "php.h"
#include "cmark-gfm.h"

/* Render the cmark document tree as a nested PHP array into return_value.
 * Throws MdParser\Exception on AST nesting beyond MDPARSER_MAX_AST_DEPTH. */
void mdparser_render_ast(cmark_node *document, int cmark_options, zval *return_value);

/* Populate the permanent interned key/value strings used by the AST
 * walker. Called from MINIT. */
void mdparser_init_ast_strings(void);

#endif
