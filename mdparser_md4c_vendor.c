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

/* Stack-backed arena for md4c's scratch working set inside mdparser_md4c_parse.
 * Satisfies small/medium parses and LIFO link-attribute allocations without
 * touching libc malloc/free, while preserving exact parse_memory_limit
 * accounting and overflowing cleanly to the tracked libc list. */
#define MDPARSER_MD4C_ARENA_SIZE  16384U
#define MDPARSER_MD4C_ARENA_ALIGN 16U
#define MDPARSER_ARENA_LIVE  ((mdparser_md4c_alloc_header *)(uintptr_t)1)
#define MDPARSER_ARENA_FREED ((mdparser_md4c_alloc_header *)(uintptr_t)2)

struct mdparser_md4c_alloc_tracker {
    mdparser_md4c_alloc_header *head;
    mdparser_md4c_alloc_tracker *previous;
    unsigned char *arena;
    mdparser_md4c_alloc_header *arena_last;
    size_t arena_used;
    size_t arena_cap;
    size_t live;        /* bytes charged to the parse budget, headers included */
    /* 0 = unlimited. With the limit off, 256 MiB of bracket-heavy input
     * can ask libc for about 19-22 GiB; keep a limit for untrusted
     * input in long-lived workers. */
    size_t limit;
    bool exceeded;      /* refused by the parse_memory_limit budget */
    bool alloc_failed;  /* genuine libc NULL (malloc/realloc) without budget refusal */
};

/* The size field rides in the union's existing padding: three pointers plus a
 * size_t is 32 bytes, which is what long double's 16-byte alignment already
 * rounded the header up to. For arena blocks, links.next holds
 * MDPARSER_ARENA_LIVE or MDPARSER_ARENA_FREED and links.previous chains
 * blocks in allocation order for LIFO tail rewind. */
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

typedef char mdparser_alloc_header_align_assert[
    (sizeof(mdparser_md4c_alloc_header) % MDPARSER_MD4C_ARENA_ALIGN == 0) ? 1 : -1];

ZEND_TLS mdparser_md4c_alloc_tracker *mdparser_md4c_active_tracker;

static zend_always_inline size_t mdparser_md4c_arena_round(size_t n)
{
    return (n + (MDPARSER_MD4C_ARENA_ALIGN - 1)) & ~(size_t)(MDPARSER_MD4C_ARENA_ALIGN - 1);
}

static zend_always_inline void mdparser_md4c_arena_rewind(
    mdparser_md4c_alloc_tracker *owner, mdparser_md4c_alloc_header *header)
{
    header->links.next = MDPARSER_ARENA_FREED;
    if (header == owner->arena_last) {
        mdparser_md4c_alloc_header *cur = header;
        while (cur && cur->links.next == MDPARSER_ARENA_FREED) {
            owner->arena_used = (size_t)((unsigned char *)cur - owner->arena);
            cur = cur->links.previous;
        }
        owner->arena_last = cur;
    }
}

#if defined(_MSC_VER)
# define MDPARSER_ALIGN16 __declspec(align(16))
#elif defined(__GNUC__) || defined(__clang__)
# define MDPARSER_ALIGN16 __attribute__((aligned(16)))
#else
# define MDPARSER_ALIGN16
#endif

static zend_never_inline void *mdparser_md4c_malloc(size_t size)
{
    mdparser_md4c_alloc_header *header;
    mdparser_md4c_alloc_tracker *owner = mdparser_md4c_active_tracker;
    size_t charged;

    /* SIZE_MAX guard: the request can never be satisfied (header would
     * overflow size_t). This is not a budget refusal, so it counts as a
     * genuine allocation failure (alloc_failed), mapping to ERR_MEMORY
     * rather than ERR_PARSE. */
    if (UNEXPECTED(size > SIZE_MAX - sizeof(*header))) {
        if (owner) {
            owner->alloc_failed = true;
        }
        return NULL;
    }
    /* Charge the header as well: the budget is meant to bound md4c's working
     * set consistently regardless of arena vs libc backing. */
    charged = sizeof(*header) + size;
    if (owner && UNEXPECTED(owner->limit
            && charged > owner->limit - MIN(owner->live, owner->limit))) {
        owner->exceeded = true;
        return NULL;
    }
    /* arena_cap and arena_used are multiples of MDPARSER_MD4C_ARENA_ALIGN, so
     * charged <= avail implies arena_round(charged) <= avail without overflow. */
    if (EXPECTED(owner && charged <= owner->arena_cap - owner->arena_used)) {
        header = (mdparser_md4c_alloc_header *)(owner->arena + owner->arena_used);
        owner->arena_used += mdparser_md4c_arena_round(charged);
        header->links.previous = owner->arena_last;
        header->links.next = MDPARSER_ARENA_LIVE;
        header->links.owner = owner;
        header->links.size = charged;
        owner->arena_last = header;
        owner->live += charged;
        return header + 1;
    }

    header = malloc(charged);
    if (UNEXPECTED(!header)) {
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

static zend_never_inline void *mdparser_md4c_realloc(void *ptr, size_t size)
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
    header = (mdparser_md4c_alloc_header *)ptr - 1;
    owner = header->links.owner;
    /* Same SIZE_MAX policy as malloc: unsatisfiable, not budget-refused. */
    if (UNEXPECTED(size > SIZE_MAX - sizeof(*header))) {
        if (owner) {
            owner->alloc_failed = true;
        }
        return NULL;
    }

    old_charged = header->links.size;
    charged = sizeof(*header) + size;
    if (owner && UNEXPECTED(owner->limit && charged > old_charged
            && charged - old_charged
                > owner->limit - MIN(owner->live, owner->limit))) {
        owner->exceeded = true;
        return NULL;
    }

    if (header->links.next == MDPARSER_ARENA_LIVE) {
        size_t old_payload = old_charged - sizeof(*header);
        if (header == owner->arena_last) {
            size_t start = (size_t)((unsigned char *)header - owner->arena);
            if (charged <= owner->arena_cap - start) {
                owner->arena_used = start + mdparser_md4c_arena_round(charged);
                header->links.size = charged;
                owner->live = owner->live - old_charged + charged;
                return ptr;
            }
        } else if (charged <= old_charged) {
            header->links.size = charged;
            owner->live = owner->live - old_charged + charged;
            return ptr;
        }

        if (charged <= owner->arena_cap - owner->arena_used) {
            new_header = (mdparser_md4c_alloc_header *)(owner->arena + owner->arena_used);
            owner->arena_used += mdparser_md4c_arena_round(charged);
            new_header->links.previous = owner->arena_last;
            new_header->links.next = MDPARSER_ARENA_LIVE;
            new_header->links.owner = owner;
            new_header->links.size = charged;
            owner->arena_last = new_header;
            if (old_payload) {
                memcpy(new_header + 1, ptr, MIN(old_payload, size));
            }
            header->links.next = MDPARSER_ARENA_FREED;
            owner->live = owner->live - old_charged + charged;
            return new_header + 1;
        }

        new_header = malloc(charged);
        if (UNEXPECTED(!new_header)) {
            owner->alloc_failed = true;
            return NULL;
        }
        if (old_payload) {
            memcpy(new_header + 1, ptr, MIN(old_payload, size));
        }
        mdparser_md4c_arena_rewind(owner, header);
        new_header->links.previous = NULL;
        new_header->links.next = owner->head;
        new_header->links.owner = owner;
        new_header->links.size = charged;
        if (owner->head) {
            owner->head->links.previous = new_header;
        }
        owner->head = new_header;
        owner->live = owner->live - old_charged + charged;
        return new_header + 1;
    }

    previous = header->links.previous;
    next = header->links.next;
    new_header = realloc(header, charged);
    if (UNEXPECTED(!new_header)) {
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

static zend_never_inline void mdparser_md4c_free(void *ptr)
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
    if (EXPECTED(header->links.next == MDPARSER_ARENA_LIVE)) {
        mdparser_md4c_arena_rewind(owner, header);
        return;
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
 * stdbool.h, stdint.h, stdio.h, stdlib.h, stddef.h, string.h, and wchar.h
 * (the last only under MD4C_USE_UTF16, which this build never defines; it is
 * included anyway so the invariant does not depend on that). If md4c gains a
 * new system include at a refresh, add it to this list. */
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>

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
    MDPARSER_ALIGN16 union {
        long double alignment;
        unsigned char bytes[MDPARSER_MD4C_ARENA_SIZE];
    } arena_storage;
    mdparser_md4c_alloc_tracker tracker = {0};
    zend_long limit = MDPARSER_G(parse_memory_limit);
    int result;

    tracker.arena = arena_storage.bytes;
    tracker.arena_cap = sizeof(arena_storage.bytes);
    tracker.limit = limit > 0 ? (size_t)limit : 0;
    tracker.previous = mdparser_md4c_active_tracker;
    mdparser_md4c_active_tracker = &tracker;
#ifdef ZEND_HAS_OOM
    if (UNEXPECTED(ZEND_HAS_OOM())) {
        result = MDPARSER_MD4C_OOM_STATUS;
    } else {
        result = md_parse(text, size, parser, userdata);
    }
#else
    result = md_parse(text, size, parser, userdata);
#endif
    mdparser_md4c_active_tracker = tracker.previous;

    while (UNEXPECTED(tracker.head != NULL)) {
        mdparser_md4c_alloc_header *header = tracker.head;
        tracker.head = header->links.next;
        free(header);
    }

    *bailed_out = (result == MDPARSER_MD4C_BAILOUT_STATUS);
    *limit_exceeded = tracker.exceeded;
    *alloc_failed = tracker.alloc_failed;
    return result;
}
