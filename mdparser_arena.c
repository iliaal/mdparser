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

#include <string.h>
#include <stdint.h>

#include "mdparser_arena.h"

struct mdparser_arena_chunk {
    mdparser_arena_chunk *prev;
    size_t size;   /* usable bytes following the header */
    size_t used;
};

#define MDP_HDR    16u
#define MDP_ALIGN  16u
#define MDP_DEFAULT_CHUNK (256u * 1024u)
#define MDP_SMALL_MAX (MDP_SMALL_CLASSES * MDP_ALIGN)  /* 4096 */

#define CHUNK_DATA(c) ((unsigned char *)(c) + sizeof(mdparser_arena_chunk))

/* The 16-byte header stores the block's reserved (aligned) payload capacity:
 * free() reads it to pick a reuse bin, realloc() reads it to decide
 * grow-in-place vs move. A freed block keeps its next-free pointer in the
 * first bytes of its own (>=16-byte) payload, so the free-list needs no side
 * storage. */

ZEND_TLS mdparser_arena *mdp_current = NULL;

static zend_always_inline size_t mdp_round(size_t n) {
    return (n + (MDP_ALIGN - 1)) & ~((size_t)MDP_ALIGN - 1);
}

void mdparser_arena_init(mdparser_arena *a) {
    memset(a, 0, sizeof(*a));
}

void mdparser_arena_activate(mdparser_arena *a) { mdp_current = a; }
void mdparser_arena_deactivate(void) { mdp_current = NULL; }

void mdparser_arena_destroy(mdparser_arena *a) {
    mdparser_arena_chunk *c = a->head;
    while (c) {
        mdparser_arena_chunk *prev = c->prev;
        efree(c);
        c = prev;
    }
    /* Bins point into the just-freed chunks; init clears them too. */
    mdparser_arena_init(a);
}

static mdparser_arena_chunk *mdp_new_chunk(mdparser_arena *a, size_t need) {
    size_t cap = need > MDP_DEFAULT_CHUNK ? need : MDP_DEFAULT_CHUNK;
    mdparser_arena_chunk *c = emalloc(sizeof(mdparser_arena_chunk) + cap);
    c->prev = a->head;
    c->size = cap;
    c->used = 0;
    a->head = c;
    return c;
}

/* Pull a reusable block of at least `cap` payload bytes from the free-list,
 * or NULL. Small requests hit an exact 16-byte size class; larger ones scan
 * the single oversized list first-fit. */
static void *mdp_take_free(mdparser_arena *a, size_t cap) {
    if (cap <= MDP_SMALL_MAX) {
        unsigned idx = (unsigned)(cap / MDP_ALIGN) - 1u;
        void *b = a->small_bins[idx];
        if (b) {
            a->small_bins[idx] = *(void **)b;
            return b;
        }
        return NULL;
    }
    void **link = &a->large_bin;
    while (*link) {
        void *b = *link;
        size_t bcap = *((size_t *)((unsigned char *)b - MDP_HDR));
        if (bcap >= cap) {
            *link = *(void **)b;
            return b;
        }
        link = (void **)b;
    }
    return NULL;
}

/* Reserve `n` payload bytes (uninitialized). Reuses a freed block when one
 * fits; otherwise bumps from the head chunk and records the block as the most
 * recent bump so realloc can try to grow it in place. */
static void *mdp_alloc(mdparser_arena *a, size_t n) {
    /* Self-defending against size wrap: mdp_round() and the MDP_HDR + cap
     * reservation must not overflow even if an upstream cap is ever raised
     * or a future cmark change requests a pathological size. cmark already
     * rejects buffers past INT32_MAX/2, so this never fires today. */
    if (UNEXPECTED(n > SIZE_MAX - MDP_HDR - MDP_ALIGN)) {
        zend_error_noreturn(E_ERROR, "mdparser arena: allocation size overflow");
    }
    size_t cap = mdp_round(n);

    void *reused = mdp_take_free(a, cap);
    if (reused) {
        /* A reused block sits mid-chunk: not safe to grow in place. */
        a->last_chunk = NULL;
        a->last_payload = NULL;
        return reused;
    }

    size_t need = MDP_HDR + cap;
    mdparser_arena_chunk *c = a->head;
    if (!c || need > c->size - c->used) {
        c = mdp_new_chunk(a, need);
    }
    unsigned char *raw = CHUNK_DATA(c) + c->used;
    c->used += need;
    *((size_t *)raw) = cap;
    void *payload = raw + MDP_HDR;
    a->last_chunk = c;
    a->last_payload = payload;
    return payload;
}

static void *mdp_calloc(size_t nmemb, size_t size) {
    mdparser_arena *a = mdp_current;
    if (UNEXPECTED(!a)) {
        return ecalloc(nmemb, size);
    }
    if (UNEXPECTED(size && nmemb > SIZE_MAX / size)) {
        zend_error_noreturn(E_ERROR, "mdparser arena: allocation size overflow");
    }
    size_t n = nmemb * size;
    if (n == 0) {
        n = 1;
    }
    void *p = mdp_alloc(a, n);
    memset(p, 0, n);
    return p;
}

static void mdp_free(void *ptr) {
    mdparser_arena *a = mdp_current;
    if (!ptr || UNEXPECTED(!a)) {
        return;
    }
    size_t cap = *((size_t *)((unsigned char *)ptr - MDP_HDR));
    if (cap <= MDP_SMALL_MAX) {
        unsigned idx = (unsigned)(cap / MDP_ALIGN) - 1u;
        *(void **)ptr = a->small_bins[idx];
        a->small_bins[idx] = ptr;
    } else {
        *(void **)ptr = a->large_bin;
        a->large_bin = ptr;
    }
}

static void *mdp_realloc(void *ptr, size_t size) {
    mdparser_arena *a = mdp_current;
    if (UNEXPECTED(!a)) {
        return erealloc(ptr, size);
    }
    if (!ptr) {
        return mdp_alloc(a, size ? size : 1);
    }
    if (size == 0) {
        size = 1;
    }
    if (UNEXPECTED(size > SIZE_MAX - MDP_HDR - MDP_ALIGN)) {
        zend_error_noreturn(E_ERROR, "mdparser arena: allocation size overflow");
    }
    size_t *hdr = (size_t *)((unsigned char *)ptr - MDP_HDR);
    size_t cap = *hdr;
    size_t want = mdp_round(size);

    /* Already fits the reserved region. */
    if (want <= cap) {
        return ptr;
    }

    /* Grow in place when this block is the last fresh bump in the head chunk
     * -- covers tight strbuf append-grow loops with nothing allocated
     * between. realloc carries no zero-fill contract, so the freshly exposed
     * bytes are left as-is (cmark tracks its own lengths). */
    if (ptr == a->last_payload && a->last_chunk == a->head) {
        mdparser_arena_chunk *c = a->head;
        size_t start_off = (size_t)((unsigned char *)ptr - MDP_HDR - CHUNK_DATA(c));
        size_t new_used = start_off + MDP_HDR + want;
        if (new_used <= c->size) {
            c->used = new_used;
            *hdr = want;
            return ptr;
        }
    }

    /* Move: take a new block (reusing a freed one if possible), copy, and
     * return the old block to its reuse bin so peak tracks the live set. */
    void *np = mdp_alloc(a, size);
    memcpy(np, ptr, cap < size ? cap : size);
    mdp_free(ptr);
    return np;
}

cmark_mem mdparser_arena_mem = { mdp_calloc, mdp_realloc, mdp_free };
