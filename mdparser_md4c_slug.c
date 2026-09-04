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
 * Heading-anchor slug (self-contained; matches mdparser_slugify)
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
            if (c >= 'A' && c <= 'Z') {
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

void mdm_slugs_init(mdm_slugs *s)
{
    zend_hash_init(&s->taken, 8, NULL, NULL, 0);
    zend_hash_init(&s->next_suffix, 8, NULL, NULL, 0);
    s->active = true;
}

void mdm_slugs_destroy(mdm_slugs *s)
{
    if (!s->active) return;
    zend_hash_destroy(&s->taken);
    zend_hash_destroy(&s->next_suffix);
    s->active = false;
}

char *mdm_slug_unique(mdm_slugs *s, char *base)
{
    if (base[0] == '\0') return base;
    size_t blen = strlen(base);
    if (!zend_hash_str_exists(&s->taken, base, blen)) {
        zval z; ZVAL_TRUE(&z);
        zend_hash_str_add(&s->taken, base, blen, &z);
        return base;
    }
    zval *nx = zend_hash_str_find(&s->next_suffix, base, blen);
    zend_long start = nx ? Z_LVAL_P(nx) : 1;
    if (start < 1) {
        efree(base);
        char *empty = emalloc(1);
        empty[0] = '\0';
        return empty;
    }
    char *cand = emalloc(blen + 24);
    for (zend_long n = start; n <= 100000; n++) {
        int w = snprintf(cand, blen + 24, "%s-" ZEND_LONG_FMT, base, n);
        if (w < 0 || (size_t)w >= blen + 24) continue;
        if (!zend_hash_str_exists(&s->taken, cand, (size_t)w)) {
            zval z; ZVAL_TRUE(&z);
            zend_hash_str_add(&s->taken, cand, (size_t)w, &z);
            zval suf; ZVAL_LONG(&suf, n + 1);
            zend_hash_str_update(&s->next_suffix, base, blen, &suf);
            efree(base);
            return cand;
        }
    }
    zval exhausted;
    ZVAL_LONG(&exhausted, -1);
    zend_hash_str_update(&s->next_suffix, base, blen, &exhausted);
    efree(cand);
    efree(base);
    char *empty = emalloc(1);
    empty[0] = '\0';
    return empty;
}
