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

#ifndef MDPARSER_HTML_POSTPROCESS_H
#define MDPARSER_HTML_POSTPROCESS_H

#include "php.h"
#include "cmark-gfm.h"

/* Apply postprocess transforms (heading anchors, nofollow links) to a
 * cmark-rendered HTML buffer. Returns a freshly-allocated zend_string
 * with the result, or NULL on allocation failure. The caller owns the
 * returned string. `pp_mask` is a bitmask of MDPARSER_PP_* flags and
 * MUST be non-zero; passing 0 violates the contract (see ZEND_ASSERT).
 *
 * When the heading-anchor flag is in pp_mask, `document`, `cmark_options`,
 * and `extensions` are consulted to build a per-heading rendering
 * fingerprint that locates each heading's byte position in `html_in`.
 * The fingerprint scan now skips raw-text/escapable-raw-text element
 * bodies (script, style, title, textarea, iframe, noscript, xmp,
 * noembed, noframes), HTML comments, and CDATA, so attacker-controlled
 * bytes inside those regions cannot hijack a slug or splice the
 * nofollow rewrite. NOTE: byte-identical raw `<hN>` blocks in the
 * document body still collide with real Markdown headings; that case
 * remains accepted behavior pending renderer-level heading IDs (see
 * tests/030_anchor_unsafe_collision.phpt).
 *
 * `document` may be NULL when `MDPARSER_PP_HEADING_ANCHORS` is not in
 * pp_mask (e.g. nofollow-only callers like toInlineHtml). Passing
 * NULL with `MDPARSER_PP_HEADING_ANCHORS` set is a caller bug and
 * returns NULL. */
zend_string *mdparser_html_postprocess(
    const char *html_in, size_t html_len,
    cmark_node *document, int cmark_options,
    cmark_llist *extensions, int pp_mask);

/* Variant that reports the specific failure reason via *status_out
 * (see mdparser_pp_status_message). On NULL return the caller can map
 * status to a precise exception message instead of the catch-all
 * "HTML postprocess allocation failure". On success status_out is set
 * to 0. */
zend_string *mdparser_html_postprocess_ex(
    const char *html_in, size_t html_len,
    cmark_node *document, int cmark_options,
    cmark_llist *extensions, int pp_mask,
    int *status_out);

/* Map a status code from mdparser_html_postprocess_ex to an exception
 * message. Returns NULL on success status. The returned pointer is to
 * a static literal; the caller must not free it. */
const char *mdparser_pp_status_message(int status);

#endif
