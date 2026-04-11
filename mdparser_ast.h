/*
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2026 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,     |
  | that is bundled with this package in the file LICENSE, and is       |
  | available through the world-wide-web at the following url:          |
  | http://www.php.net/license/3_01.txt                                 |
  +----------------------------------------------------------------------+
*/

#ifndef PHP_MDPARSER_AST_H
#define PHP_MDPARSER_AST_H

#include "php.h"
#include "cmark-gfm.h"

void mdparser_render_ast(cmark_node *document, int cmark_options, zval *return_value);

/* Called from MSHUTDOWN to release the lazily-populated AST key
 * strings. No-op if toAst was never invoked this process. */
void mdparser_release_ast_strings(void);

#endif
