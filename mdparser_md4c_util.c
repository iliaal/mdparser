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

#include "mdparser_md4c_util.h"

size_t mdparser_md4c_utf8_seqlen(const unsigned char *p, size_t avail)
{
    unsigned char c = p[0];
    if (c < 0x80) return 1;
    size_t expect;
    unsigned char min2 = 0x80, max2 = 0xBF;
    if (c >= 0xC2 && c <= 0xDF)      expect = 2;
    else if (c == 0xE0)              { expect = 3; min2 = 0xA0; }
    else if (c >= 0xE1 && c <= 0xEC) expect = 3;
    else if (c == 0xED)              { expect = 3; max2 = 0x9F; }
    else if (c >= 0xEE && c <= 0xEF) expect = 3;
    else if (c == 0xF0)              { expect = 4; min2 = 0x90; }
    else if (c >= 0xF1 && c <= 0xF3) expect = 4;
    else if (c == 0xF4)              { expect = 4; max2 = 0x8F; }
    else return 0;
    if (avail < expect) return 0;
    if (p[1] < min2 || p[1] > max2) return 0;
    for (size_t k = 2; k < expect; k++) {
        if (p[k] < 0x80 || p[k] > 0xBF) return 0;
    }
    return expect;
}

const char *mdparser_md4c_validate_utf8(const char *src, size_t len,
    size_t *out_len, bool *owned)
{
    const unsigned char *p = (const unsigned char *)src;
    size_t i = 0;
    bool clean = true;
    while (i < len) {
        size_t n = mdparser_md4c_utf8_seqlen(p + i, len - i);
        if (n == 0) { clean = false; break; }
        i += n;
    }
    if (clean) {
        *owned = false;
        *out_len = len;
        return src;
    }
    /* Worst case: every byte becomes a 3-byte U+FFFD. */
    char *dst = emalloc(len * 3 + 1);
    size_t o = 0;
    i = 0;
    while (i < len) {
        size_t n = mdparser_md4c_utf8_seqlen(p + i, len - i);
        if (n == 0) {
            dst[o++] = (char)0xEF; dst[o++] = (char)0xBF; dst[o++] = (char)0xBD;
            i++;
        } else {
            memcpy(dst + o, p + i, n);
            o += n;
            i += n;
        }
    }
    dst[o] = '\0';
    *owned = true;
    *out_len = o;
    return dst;
}
