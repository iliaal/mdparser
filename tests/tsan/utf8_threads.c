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
/* Standalone ThreadSanitizer harness for mdparser_utf8.c; see
 * tests/tsan/run.sh.
 *
 * Under ZTS every request thread can reach the SIMD front end at once,
 * including on the very first parse after startup. Eight threads start
 * together behind a barrier and validate the same multibyte buffer, so any
 * unsynchronized shared state the front end touches (such as the x86-64
 * CPUID cache) is accessed concurrently and TSan reports it. */

#include <pthread.h>
#include <stdio.h>
#include <stddef.h>

#include "mdparser_utf8.h"

#define THREADS 8
#define ROUNDS 50

static pthread_barrier_t start;
static unsigned char buf[4096];

static void *worker(void *arg)
{
    size_t sum = 0;

    (void)arg;
    pthread_barrier_wait(&start);
    for (int i = 0; i < ROUNDS; i++) {
        sum += mdparser_utf8_valid_prefix(buf, sizeof buf);
    }
    return (void *)sum;
}

int main(void)
{
    pthread_t t[THREADS];
    size_t total = 0;

    for (size_t i = 0; i + 2 <= sizeof buf; i += 2) {
        buf[i] = 0xC3;
        buf[i + 1] = 0xBC;
    }
    if (pthread_barrier_init(&start, NULL, THREADS) != 0) {
        return 2;
    }
    for (int i = 0; i < THREADS; i++) {
        if (pthread_create(&t[i], NULL, worker, NULL) != 0) {
            return 2;
        }
    }
    for (int i = 0; i < THREADS; i++) {
        void *r;
        pthread_join(t[i], &r);
        total += (size_t)r;
    }
    printf("prefix_total=%zu\n", total);
    return 0;
}
