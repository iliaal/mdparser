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
#include "mdparser_md4c_vendor.h"

typedef union mdparser_md4c_alloc_header mdparser_md4c_alloc_header;
typedef struct mdparser_md4c_alloc_tracker mdparser_md4c_alloc_tracker;

struct mdparser_md4c_alloc_tracker {
    mdparser_md4c_alloc_header *head;
    mdparser_md4c_alloc_tracker *previous;
};

union mdparser_md4c_alloc_header {
    struct {
        mdparser_md4c_alloc_header *previous;
        mdparser_md4c_alloc_header *next;
        mdparser_md4c_alloc_tracker *owner;
    } links;
    long double alignment;
    void *pointer_alignment;
};

ZEND_TLS mdparser_md4c_alloc_tracker *mdparser_md4c_active_tracker;

static zend_always_inline void *mdparser_md4c_malloc(size_t size)
{
    mdparser_md4c_alloc_header *header;
    mdparser_md4c_alloc_tracker *owner = mdparser_md4c_active_tracker;

    if (size > SIZE_MAX - sizeof(*header)) {
        return NULL;
    }
    header = malloc(sizeof(*header) + size);
    if (!header) {
        return NULL;
    }

    header->links.previous = NULL;
    header->links.next = owner ? owner->head : NULL;
    header->links.owner = owner;
    if (owner) {
        if (owner->head) {
            owner->head->links.previous = header;
        }
        owner->head = header;
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

    if (!ptr) {
        return mdparser_md4c_malloc(size);
    }
    if (size > SIZE_MAX - sizeof(*header)) {
        return NULL;
    }

    header = (mdparser_md4c_alloc_header *)ptr - 1;
    previous = header->links.previous;
    next = header->links.next;
    owner = header->links.owner;
    new_header = realloc(header, sizeof(*header) + size);
    if (!new_header) {
        return NULL;
    }

    new_header->links.previous = previous;
    new_header->links.next = next;
    new_header->links.owner = owner;
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
    const MD_PARSER *parser, void *userdata, bool *bailed_out)
{
    mdparser_md4c_alloc_tracker tracker = {0};
    int result;

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
    return result;
}
