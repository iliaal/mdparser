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

#ifndef MDPARSER_ARENA_H
#define MDPARSER_ARENA_H

#include <stddef.h>
#include "cmark-gfm.h"

/* Zend-backed bump arena exposed to cmark as a cmark_mem. Slabs are
 * emalloc'd so they stay inside PHP's memory_limit; free() is a no-op and
 * the whole parse is reclaimed in bulk by mdparser_arena_destroy(). cmark
 * allocator callbacks carry no context pointer, so the active arena is
 * held in a thread-local set by mdparser_arena_activate(). A parse is not
 * reentrant on a thread, so one current-arena pointer is sufficient. */

typedef struct mdparser_arena_chunk mdparser_arena_chunk;

/* Segregated free-list: cmark frees heavily mid-parse (tens of thousands of
 * blocks on large inputs), so honoring free() into reuse bins instead of
 * holding everything until destroy keeps peak RSS near the live set rather
 * than the parse-lifetime allocation total. small_bins are exact 16-byte
 * size classes; oversized blocks land in one first-fit large list. */
#define MDP_SMALL_CLASSES 256u

typedef struct mdparser_arena {
    mdparser_arena_chunk *head;
    mdparser_arena_chunk *last_chunk;
    void  *last_payload;
    void  *small_bins[MDP_SMALL_CLASSES];
    void  *large_bin;
} mdparser_arena;

extern cmark_mem mdparser_arena_mem;

void mdparser_arena_init(mdparser_arena *a);
void mdparser_arena_destroy(mdparser_arena *a);
void mdparser_arena_activate(mdparser_arena *a);
void mdparser_arena_deactivate(void);

#endif /* MDPARSER_ARENA_H */
