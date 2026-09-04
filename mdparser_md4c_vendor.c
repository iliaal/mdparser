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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"

#include <stdint.h>
#include <stdlib.h>

#include "md4c.h"
#include "php_mdparser.h"
#include "mdparser_md4c_vendor.h"

typedef union mdparser_md4c_alloc_header mdparser_md4c_alloc_header;
typedef struct mdparser_md4c_alloc_tracker mdparser_md4c_alloc_tracker;

struct mdparser_md4c_alloc_tracker {
    mdparser_md4c_alloc_header *head;
    mdparser_md4c_alloc_tracker *previous;
    size_t live;        /* bytes requested from libc, headers included */
    /* 0 = unlimited. With the limit off, 256MB of the worst-case byte
     * (~72B per '[') can ask libc for ~18GB; keep a limit for untrusted
     * input in long-lived workers. No clamp here (human-gated). */
    size_t limit;
    bool exceeded;      /* refused by the parse_memory_limit budget */
    bool alloc_failed;  /* genuine libc NULL (malloc/realloc) without budget refusal */
};

/* The size field rides in the union's existing padding: three pointers plus a
 * size_t is 32 bytes, which is what long double's 16-byte alignment already
 * rounded the header up to. */
union mdparser_md4c_alloc_header {
    struct {
        mdparser_md4c_alloc_header *previous;
        mdparser_md4c_alloc_header *next;
        mdparser_md4c_alloc_tracker *owner;
        size_t size;
    } links;
    long double alignment;
    void *pointer_alignment;
};

ZEND_TLS mdparser_md4c_alloc_tracker *mdparser_md4c_active_tracker;
/* Small-input parse floor: every md4c allocation pays the 32B registry
 * header (charged to the budget) plus a doubly-linked-list splice on
 * malloc/realloc/free. Negligible next to md4c's own per-byte state, but
 * it is the dominant wrapper cost on tiny inputs. */
static zend_always_inline void *mdparser_md4c_malloc(size_t size)
{
    mdparser_md4c_alloc_header *header;
    mdparser_md4c_alloc_tracker *owner = mdparser_md4c_active_tracker;
    size_t charged;

    /* SIZE_MAX guard: the request can never be satisfied (header would
     * overflow size_t). This is not a budget refusal, so it counts as a
     * genuine allocation failure (alloc_failed), mapping to ERR_MEMORY
     * rather than ERR_PARSE. */
    if (size > SIZE_MAX - sizeof(*header)) {
        if (owner) {
            owner->alloc_failed = true;
        }
        return NULL;
    }
    /* Charge the header as well: the budget is meant to bound what md4c makes
     * us ask libc for, not just the payload md4c sees. */
    charged = sizeof(*header) + size;
    if (owner && owner->limit
            && charged > owner->limit - MIN(owner->live, owner->limit)) {
        owner->exceeded = true;
        return NULL;
    }
    header = malloc(sizeof(*header) + size);
    if (!header) {
        if (owner) {
            owner->alloc_failed = true;
        }
        return NULL;
    }

    header->links.previous = NULL;
    header->links.next = owner ? owner->head : NULL;
    header->links.owner = owner;
    header->links.size = charged;
    if (owner) {
        if (owner->head) {
            owner->head->links.previous = header;
        }
        owner->head = header;
        owner->live += charged;
    }
    return header + 1;
}

static zend_always_inline void *mdparser_md4c_realloc(void *ptr, size_t size)
{
    mdparser_md4c_alloc_header *header;
    mdparser_md4c_alloc_header *new_header;
    mdparser_md4c_alloc_header *previous;
    mdparser_md4c_alloc_header *next;
    mdparser_md4c_alloc_tracker *owner;
    size_t old_charged;
    size_t charged;

    if (!ptr) {
        return mdparser_md4c_malloc(size);
    }
    /* Same SIZE_MAX policy as malloc: unsatisfiable, not budget-refused. */
    if (size > SIZE_MAX - sizeof(*header)) {
        mdparser_md4c_alloc_header *h = (mdparser_md4c_alloc_header *)ptr - 1;
        mdparser_md4c_alloc_tracker *o = h->links.owner;
        if (o) {
            o->alloc_failed = true;
        }
        return NULL;
    }

    header = (mdparser_md4c_alloc_header *)ptr - 1;
    previous = header->links.previous;
    next = header->links.next;
    owner = header->links.owner;
    old_charged = header->links.size;
    charged = sizeof(*header) + size;
    if (owner && owner->limit && charged > old_charged
            && charged - old_charged
                > owner->limit - MIN(owner->live, owner->limit)) {
        owner->exceeded = true;
        return NULL;
    }
    new_header = realloc(header, sizeof(*header) + size);
    if (!new_header) {
        if (owner) {
            owner->alloc_failed = true;
        }
        return NULL;
    }
    new_header->links.previous = previous;
    new_header->links.next = next;
    new_header->links.owner = owner;
    new_header->links.size = charged;
    if (owner) {
        owner->live = owner->live - old_charged + charged;
    }
    if (previous) {
        previous->links.next = new_header;
    } else if (owner) {
        owner->head = new_header;
    }
    if (next) {
        next->links.previous = new_header;
    }
    return new_header + 1;
}

static zend_always_inline void mdparser_md4c_free(void *ptr)
{
    mdparser_md4c_alloc_header *header;
    mdparser_md4c_alloc_tracker *owner;

    if (!ptr) {
        return;
    }

    header = (mdparser_md4c_alloc_header *)ptr - 1;
    owner = header->links.owner;
    if (owner) {
        owner->live -= header->links.size;
    }
    if (header->links.previous) {
        header->links.previous->links.next = header->links.next;
    } else if (owner) {
        owner->head = header->links.next;
    }
    if (header->links.next) {
        header->links.next->links.previous = header->links.previous;
    }
    free(header);
}

/* The malloc/realloc/free macros below rewrite every allocation call inside
 * md4c.c -- including any appearing in system headers that md4c.c includes
 * itself. That is safe only because every header md4c.c includes is already
 * fully included here (include guards suppress re-declaration), so the macro
 * expansion never rewrites a system declaration. md4c.c includes: limits.h,
 * stdbool.h, stdint.h, stdio.h, stdlib.h, stddef.h, string.h. If md4c gains a
 * new system include at a refresh, add it to this list. */
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#define MDPARSER_MD4C_BAILOUT_STATUS (-2)
#define MD_PARSER_BAILOUT_GUARD(result, expression)                       \
    do {                                                                  \
        zend_try {                                                        \
            (result) = (expression);                                      \
        } zend_catch {                                                    \
            (result) = MDPARSER_MD4C_BAILOUT_STATUS;                      \
        } zend_end_try();                                                 \
    } while (0)
#define malloc mdparser_md4c_malloc
#define realloc mdparser_md4c_realloc
#define free mdparser_md4c_free

#ifdef MAX
#undef MAX
#endif
#ifdef MIN
#undef MIN
#endif
#include "vendor/md4c/md4c.c"

#undef free
#undef realloc
#undef malloc
#undef MD_PARSER_BAILOUT_GUARD

int mdparser_md4c_parse(const MD_CHAR *text, MD_SIZE size,
    const MD_PARSER *parser, void *userdata, bool *bailed_out,
    bool *limit_exceeded, bool *alloc_failed)
{
    mdparser_md4c_alloc_tracker tracker = {0};
    zend_long limit = MDPARSER_G(parse_memory_limit);
    int result;

    tracker.limit = limit > 0 ? (size_t)limit : 0;
    tracker.previous = mdparser_md4c_active_tracker;
    mdparser_md4c_active_tracker = &tracker;
    result = md_parse(text, size, parser, userdata);
    mdparser_md4c_active_tracker = tracker.previous;

    while (tracker.head) {
        mdparser_md4c_alloc_header *header = tracker.head;
        tracker.head = header->links.next;
        free(header);
    }

    *bailed_out = (result == MDPARSER_MD4C_BAILOUT_STATUS);
    *limit_exceeded = tracker.exceeded;
    *alloc_failed = tracker.alloc_failed;
    return result;
}
