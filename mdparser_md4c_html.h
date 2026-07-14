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

#ifndef MDPARSER_MD4C_HTML_H
#define MDPARSER_MD4C_HTML_H

#include "php.h"

/* Render-option bits for the md4c HTML renderer. These are the binding-side
 * behaviors layered on top of md4c's parse (md4c parser flags are a separate
 * int passed alongside). */
#define MDPARSER_RF_UNSAFE           0x01  /* pass raw HTML through (else escape) */
#define MDPARSER_RF_TAGFILTER        0x02  /* GFM blocklist escape in unsafe mode */
#define MDPARSER_RF_NOFOLLOW         0x04  /* rel="nofollow" on <a> */
#define MDPARSER_RF_HEADING_ANCHORS  0x08  /* id="slug" on <h1..6> */
#define MDPARSER_RF_SMART            0x10  /* SmartyPants on normal text */
#define MDPARSER_RF_NOBREAKS         0x20  /* softbreak -> space, not newline */
#define MDPARSER_RF_VALIDATE_UTF8    0x40  /* rewrite invalid UTF-8 to U+FFFD */
#define MDPARSER_RF_INLINE_SENTINEL  0x80  /* consume one internal ';' per line */

/* Render `src` (length `len`) to HTML using md4c with `parser_flags`
 * (MD_FLAG_* / MD_DIALECT_*) and the MDPARSER_RF_* behaviors in `render_opts`.
 * On success returns a freshly-allocated zend_string the caller owns and
 * sets *status to 0. On failure returns NULL and sets *status to a non-zero
 * code (see mdparser_md4c_status_message). */
zend_string *mdparser_md4c_render_html(const char *src, size_t len,
    unsigned parser_flags, int render_opts, int *status);

/* Map a non-zero status from mdparser_md4c_render_html to a static message. */
const char *mdparser_md4c_status_message(int status);

/* Build the process-global escape-classification map. Call once at MINIT. */
void mdparser_md4c_html_minit(void);

#endif
