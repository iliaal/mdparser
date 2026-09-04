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

/* Dedupe state for heading anchors: a set of taken slugs plus a
 * base -> next-suffix cache so repeated collisions stay near O(1). */
typedef struct {
    HashTable taken;        /* slug string -> (dummy) */
    HashTable next_suffix;  /* base slug -> zend_long next suffix */
    bool active;
} mdm_slugs;

/* Lowercase/percent-encode `text` into a URL-fragment slug (emalloc'd,
 * caller frees). Matches the legacy mdparser_slugify behavior. */
char *mdm_slugify(const char *text, size_t len);

void mdm_slugs_init(mdm_slugs *s);
void mdm_slugs_destroy(mdm_slugs *s);

/* Return a unique slug for `base` (emalloc'd, caller frees), recording it
 * as taken. Empty base returns an empty string (id suppressed at emit). */
char *mdm_slug_unique(mdm_slugs *s, char *base);

#endif
