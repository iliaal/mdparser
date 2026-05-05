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
