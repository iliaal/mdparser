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

#ifndef MDPARSER_HTML_POSTPROCESS_H
#define MDPARSER_HTML_POSTPROCESS_H

#include "php.h"
#include "cmark-gfm.h"

/* Apply postprocess transforms (heading anchors, nofollow links) to a
 * cmark-rendered HTML buffer. Returns a freshly-malloc'd zend_string
 * with the result, or NULL on allocation failure. The caller owns the
 * returned string. `pp_mask` is a bitmask of MDPARSER_PP_* flags;
 * passing 0 returns a zend_string copy of `html_in` unchanged. The
 * `document` AST is consulted for heading text when the anchor flag
 * is set; it may be NULL only when the heading-anchor flag is not in
 * pp_mask. */
zend_string *mdparser_html_postprocess(
    const char *html_in, size_t html_len,
    cmark_node *document, int pp_mask);

#endif
