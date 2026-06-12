--TEST--
arena allocator returns max_align_t-aligned payloads
--EXTENSIONS--
mdparser
--SKIPIF--
<?php
if (PHP_OS !== 'Linux') die('skip GNU ld + build-tree objects required');
if (!is_file(dirname(__DIR__) . '/.libs/mdparser_arena.o')) die('skip build-tree .libs object not present');
$cc = trim((string)shell_exec('command -v cc 2>/dev/null'));
if ($cc === '') die('skip no cc in PATH');
?>
--FILE--
<?php

$build = dirname(__DIR__);
$tmp = tempnam(sys_get_temp_dir(), 'mdp046');
$src = $tmp . '.c';
$bin = $tmp . '.bin';

file_put_contents($src, <<<'C'
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct mdparser_arena_chunk mdparser_arena_chunk;

#define MDP_SMALL_CLASSES 256u

typedef struct mdparser_arena {
    mdparser_arena_chunk *head;
    mdparser_arena_chunk *last_chunk;
    void  *last_payload;
    void  *small_bins[MDP_SMALL_CLASSES];
    void  *large_bin;
} mdparser_arena;

typedef struct cmark_mem {
    void *(*calloc)(size_t, size_t);
    void *(*realloc)(void *, size_t);
    void (*free)(void *);
} cmark_mem;

extern cmark_mem mdparser_arena_mem;

void mdparser_arena_init(mdparser_arena *a);
void mdparser_arena_destroy(mdparser_arena *a);
void mdparser_arena_activate(mdparser_arena *a);
void mdparser_arena_deactivate(void);

void *_emalloc(size_t size) { return malloc(size); }
void *_ecalloc(size_t nmemb, size_t size) { return calloc(nmemb, size); }
void *_erealloc(void *ptr, size_t size) { return realloc(ptr, size); }
void _efree(void *ptr) { free(ptr); }
void zend_error_noreturn(int type, const char *format, ...)
{
    (void)type;
    (void)format;
    exit(99);
}

static int check_alloc(const char *label, void *p)
{
    size_t align = alignof(max_align_t);
    uintptr_t addr = (uintptr_t)p;
    if (addr % align != 0) {
        printf("FAIL: %s address %% alignof(max_align_t) = %zu\n",
            label, (size_t)(addr % align));
        return 1;
    }
    return 0;
}

int main(void)
{
    mdparser_arena arena;
    mdparser_arena_init(&arena);
    mdparser_arena_activate(&arena);

    void *first = mdparser_arena_mem.calloc(1, 1);
    void *second = mdparser_arena_mem.calloc(1, 31);

    int failed = check_alloc("first", first) || check_alloc("second", second);

    mdparser_arena_deactivate();
    mdparser_arena_destroy(&arena);

    if (failed) {
        return 1;
    }
    printf("OK: arena payloads are max_align_t-aligned\n");
    return 0;
}
C);

$cmd = 'cc -std=c11 -o ' . escapeshellarg($bin) . ' '
     . escapeshellarg($src) . ' '
     . escapeshellarg($build . '/.libs/mdparser_arena.o') . ' 2>&1';
$out = (string)shell_exec($cmd);
if (!is_file($bin)) {
    echo "FAIL: harness did not compile:\n$out";
    exit(1);
}

passthru(escapeshellarg($bin), $rc);
@unlink($src);
@unlink($bin);
@unlink($tmp);
exit($rc);

?>
--EXPECT--
OK: arena payloads are max_align_t-aligned
