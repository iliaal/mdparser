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

#ifndef MDPARSER_MD4C_SLUG_H
#define MDPARSER_MD4C_SLUG_H

#include "php.h"
#include "zend_smart_str.h"

struct mdm_slug_node;

/* Dedupe state for heading anchors. The balanced tree stores every assigned
 * slug and the next suffix for each base, so adversarial hash collisions do
 * not affect lookup cost. */
typedef struct {
    struct mdm_slug_node *root;
    bool active;
} mdm_slugs;

/* Lowercase/percent-encode `text` into a URL-fragment slug appended to `out`
 * (caller owns and may reuse `out`). */
void mdm_slugify_into(smart_str *out, const char *text, size_t len);

void mdm_slugs_init(mdm_slugs *s);
void mdm_slugs_destroy(mdm_slugs *s);

/* Record and return a unique slug for `base[0..base_len)`. The returned
 * pointer is owned by `s` (valid until mdm_slugs_destroy) and its length is
 * stored in `*out_len`. Empty base or suffix exhaustion returns "" (*out_len=0). */
const char *mdm_slug_unique_str(mdm_slugs *s, const char *base, size_t base_len,
    size_t *out_len);

#endif
