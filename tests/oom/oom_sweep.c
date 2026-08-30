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

/* Standalone md4c allocation-failure sweep; see tests/oom/run.sh.
 *
 * mdparser.parse_memory_limit can refuse a real allocation, but only once a
 * parse crosses the budget, which reaches the error paths near that boundary
 * and no others. This harness compiles md4c.c with malloc/realloc redirected
 * through a counter that fails the n-th call, exactly as
 * mdparser_md4c_vendor.c redirects them through the allocation registry.
 * Sweeping n over a document's whole allocation count under ASAN visits every
 * error path that document can reach, not just the ones a budget happens to
 * land on. */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "md4c.h"

static long oom_counter;
static long oom_fail_at;
static int oom_fired;

static void *oom_malloc(size_t size)
{
    if (++oom_counter == oom_fail_at) {
        oom_fired = 1;
        return NULL;
    }
    return malloc(size);
}

static void *oom_realloc(void *ptr, size_t size)
{
    if (++oom_counter == oom_fail_at) {
        oom_fired = 1;
        return NULL;
    }
    return realloc(ptr, size);
}

#define malloc  oom_malloc
#define realloc oom_realloc
#ifdef MAX
#undef MAX
#endif
#ifdef MIN
#undef MIN
#endif
#include "md4c.c"
#undef realloc
#undef malloc

static int on_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    (void) type; (void) detail; (void) userdata;
    return 0;
}

static int on_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    (void) type; (void) detail; (void) userdata;
    return 0;
}

static int on_text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    (void) type; (void) text; (void) size; (void) userdata;
    return 0;
}

int main(int argc, char **argv)
{
    static const MD_PARSER parser = {
        0, MD_DIALECT_GITHUB | MD_FLAG_FOOTNOTES,
        on_block, on_block, on_span, on_span, on_text, NULL, NULL
    };
    char *doc;
    long len;
    FILE *fp;
    int ret;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <document> <fail-at|-1>\n", argv[0]);
        return 2;
    }

    fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        perror(argv[1]);
        return 2;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (len = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        perror(argv[1]);
        fclose(fp);
        return 2;
    }
    doc = (char *) malloc((size_t) len + 1);
    if (doc == NULL || (len > 0 && fread(doc, 1, (size_t) len, fp) != (size_t) len)) {
        fprintf(stderr, "%s: read failed\n", argv[1]);
        fclose(fp);
        free(doc);
        return 2;
    }
    fclose(fp);

    oom_fail_at = atol(argv[2]);
    oom_counter = 0;
    oom_fired = 0;
    /* Sequenced deliberately: oom_counter must be read after md_parse() runs,
     * which an argument list would not guarantee. */
    ret = md_parse(doc, (MD_SIZE) len, &parser, NULL);
    printf("fail_at=%ld ret=%d allocs=%ld fired=%d\n",
        oom_fail_at, ret, oom_counter, oom_fired);
    free(doc);

    /* Sanitizers cover memory safety. This covers the other half of the
     * contract: once an allocation has been refused, md_parse() must report
     * failure rather than hand back a document rendered from partial state. */
    if (oom_fired && ret == 0) {
        fprintf(stderr, "refused allocation %ld but md_parse() returned 0\n",
            oom_fail_at);
        return 1;
    }
    return 0;
}
