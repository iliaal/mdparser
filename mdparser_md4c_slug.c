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
#include "zend_smart_str.h"

#include <stdio.h>
#include <string.h>

#include "mdparser_md4c_util.h"
#include "mdparser_md4c_slug.h"

/* ===================================================================
 * Heading-anchor slug
 * =================================================================== */

char *mdm_slugify(const char *text, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    /* Single-pass into smart_str: avoids a full UTF-8 sizing walk. Invalid
     * bytes expand to %xx (3 bytes); smart_str grows on demand so pathological
     * invalid input does not need a len*3 preallocation. */
    smart_str s = {0};
    if (len) {
        smart_str_alloc(&s, len + 1, 0);
    }
    bool prev_dash = true;
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x80) {
            if (c == 0) {
                /* Verbatim code/math runs can still contain raw NUL bytes. */
                smart_str_appendl(&s, "\xef\xbf\xbd", 3);
                prev_dash = false;
            } else if (c >= 'A' && c <= 'Z') {
                smart_str_appendc(&s, (char)(c + ('a' - 'A')));
                prev_dash = false;
            } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
                smart_str_appendc(&s, (char)c);
                prev_dash = false;
            } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '-') {
                if (!prev_dash) {
                    smart_str_appendc(&s, '-');
                    prev_dash = true;
                }
            }
            i++;
            continue;
        }
        size_t n = mdparser_md4c_utf8_seqlen((const unsigned char *)text + i, len - i);
        if (n) {
            smart_str_appendl(&s, text + i, n);
            prev_dash = false;
            i += n;
        } else {
            char enc[3] = { '%', hex[(c >> 4) & 0xF], hex[c & 0xF] };
            smart_str_appendl(&s, enc, 3);
            prev_dash = false;
            i++;
        }
    }
    /* Trim trailing dashes. */
    if (s.s) {
        while (ZSTR_LEN(s.s) > 0 && ZSTR_VAL(s.s)[ZSTR_LEN(s.s) - 1] == '-') {
            ZSTR_LEN(s.s)--;
        }
        ZSTR_VAL(s.s)[ZSTR_LEN(s.s)] = '\0';
    }
    size_t out_len = s.s ? ZSTR_LEN(s.s) : 0;
    char *out = emalloc(out_len + 1);
    if (out_len) {
        memcpy(out, ZSTR_VAL(s.s), out_len);
    }
    out[out_len] = '\0';
    smart_str_free(&s);
    return out;
}

typedef struct mdm_slug_node {
    struct mdm_slug_node *left;
    struct mdm_slug_node *right;
    struct mdm_slug_node *parent;
    zend_long next_suffix;
    bool red;
    char slug[];
} mdm_slug_node;

static void mdm_slug_rotate_left(mdm_slug_node **root, mdm_slug_node *node)
{
    mdm_slug_node *child = node->right;
    node->right = child->left;
    if (child->left) child->left->parent = node;
    child->parent = node->parent;
    if (!node->parent) *root = child;
    else if (node == node->parent->left) node->parent->left = child;
    else node->parent->right = child;
    child->left = node;
    node->parent = child;
}

static void mdm_slug_rotate_right(mdm_slug_node **root, mdm_slug_node *node)
{
    mdm_slug_node *child = node->left;
    node->left = child->right;
    if (child->right) child->right->parent = node;
    child->parent = node->parent;
    if (!node->parent) *root = child;
    else if (node == node->parent->left) node->parent->left = child;
    else node->parent->right = child;
    child->right = node;
    node->parent = child;
}

static mdm_slug_node *mdm_slug_insert(mdm_slug_node **root, const char *slug,
    bool *inserted)
{
    mdm_slug_node *parent = NULL;
    mdm_slug_node *node = *root;
    int cmp = 0;
    while (node) {
        parent = node;
        cmp = strcmp(slug, node->slug);
        if (cmp == 0) {
            *inserted = false;
            return node;
        }
        node = cmp < 0 ? node->left : node->right;
    }

    size_t len = strlen(slug);
    node = emalloc(sizeof(*node) + len + 1);
    node->left = node->right = NULL;
    node->parent = parent;
    node->next_suffix = 1;
    node->red = true;
    memcpy(node->slug, slug, len + 1);
    if (!parent) *root = node;
    else if (cmp < 0) parent->left = node;
    else parent->right = node;

    while (node->parent && node->parent->red) {
        mdm_slug_node *grandparent = node->parent->parent;
        if (node->parent == grandparent->left) {
            mdm_slug_node *uncle = grandparent->right;
            if (uncle && uncle->red) {
                node->parent->red = false;
                uncle->red = false;
                grandparent->red = true;
                node = grandparent;
            } else {
                if (node == node->parent->right) {
                    node = node->parent;
                    mdm_slug_rotate_left(root, node);
                }
                node->parent->red = false;
                grandparent->red = true;
                mdm_slug_rotate_right(root, grandparent);
            }
        } else {
            mdm_slug_node *uncle = grandparent->left;
            if (uncle && uncle->red) {
                node->parent->red = false;
                uncle->red = false;
                grandparent->red = true;
                node = grandparent;
            } else {
                if (node == node->parent->left) {
                    node = node->parent;
                    mdm_slug_rotate_right(root, node);
                }
                node->parent->red = false;
                grandparent->red = true;
                mdm_slug_rotate_left(root, grandparent);
            }
        }
    }
    (*root)->red = false;
    *inserted = true;
    return node;
}

void mdm_slugs_init(mdm_slugs *s)
{
    s->root = NULL;
    s->active = true;
}

void mdm_slugs_destroy(mdm_slugs *s)
{
    if (!s->active) return;
    mdm_slug_node *node = s->root;
    while (node) {
        if (node->left) {
            node = node->left;
        } else if (node->right) {
            node = node->right;
        } else {
            mdm_slug_node *parent = node->parent;
            if (parent) {
                if (parent->left == node) parent->left = NULL;
                else parent->right = NULL;
            }
            efree(node);
            node = parent;
        }
    }
    s->root = NULL;
    s->active = false;
}

char *mdm_slug_unique(mdm_slugs *s, char *base)
{
    if (base[0] == '\0') return base;
    bool inserted;
    mdm_slug_node *base_node = mdm_slug_insert(&s->root, base, &inserted);
    if (inserted) return base;
    zend_long start = base_node->next_suffix;
    if (start < 1) {
        efree(base);
        char *empty = emalloc(1);
        empty[0] = '\0';
        return empty;
    }
    size_t blen = strlen(base);
    char *cand = emalloc(blen + 24);
    for (zend_long n = start; n <= 100000; n++) {
        int w = snprintf(cand, blen + 24, "%s-" ZEND_LONG_FMT, base, n);
        if (w < 0 || (size_t)w >= blen + 24) continue;
        mdm_slug_insert(&s->root, cand, &inserted);
        if (inserted) {
            base_node->next_suffix = n + 1;
            efree(base);
            return cand;
        }
    }
    base_node->next_suffix = -1;
    efree(cand);
    efree(base);
    char *empty = emalloc(1);
    empty[0] = '\0';
    return empty;
}
