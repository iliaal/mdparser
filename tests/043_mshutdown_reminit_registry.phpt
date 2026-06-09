--TEST--
MSHUTDOWN then re-MINIT must leave the cmark extension registry usable
--EXTENSIONS--
mdparser
--SKIPIF--
<?php
/* The harness link relies on GNU ld's --unresolved-symbols=ignore-all;
 * macOS ld64 rejects the flag and its -undefined dynamic_lookup
 * equivalent binds eagerly under chained fixups, so the technique is
 * ELF/GNU-ld only. */
if (PHP_OS !== 'Linux') die('skip GNU ld + build-tree objects required');
if (!is_file(dirname(__DIR__) . '/.libs/mdparser.o')) die('skip build-tree .libs objects not present');
$cc = trim((string)shell_exec('command -v cc 2>/dev/null'));
if ($cc === '') die('skip no cc in PATH');
?>
--FILE--
<?php

/* MSHUTDOWN calls cmark_release_plugins(), which empties the syntax-
 * extension registry. The guard inside
 * cmark_gfm_core_extensions_ensure_registered() must be reset alongside
 * it: if it stays latched, a second MINIT in the same process image
 * (embedded SAPI init/shutdown/init, a dlclose that doesn't unload)
 * finds an empty registry, cmark_find_syntax_extension("table")
 * returns NULL, and module startup dies with E_CORE_ERROR.
 *
 * A second MINIT can't be triggered from a phpt, so this drives the
 * real PHP_MSHUTDOWN body (zm_shutdown_mdparser) plus the MINIT-side
 * registration step directly, in a tiny harness statically linked
 * against the build-tree objects. Zend symbols mdparser.o references
 * on paths the harness never calls are left unresolved on purpose. */

$build = dirname(__DIR__);
$tmp = tempnam(sys_get_temp_dir(), 'mdp043');
$src = $tmp . '.c';
$bin = $tmp . '.bin';

file_put_contents($src, <<<'C'
#include <stdio.h>

typedef struct cmark_syntax_extension cmark_syntax_extension;
void cmark_gfm_core_extensions_ensure_registered(void);
void cmark_release_plugins(void);
cmark_syntax_extension *cmark_find_syntax_extension(const char *name);
int zm_shutdown_mdparser(int type, int module_number);
/* process-global node-type ids (enum in cmark; int-compatible) */
extern int CMARK_NODE_TABLE;
extern int CMARK_NODE_STRIKETHROUGH;

int main(void)
{
    /* MINIT's registration step */
    cmark_gfm_core_extensions_ensure_registered();
    if (!cmark_find_syntax_extension("table")) {
        printf("FAIL: first registration did not expose 'table'\n");
        return 1;
    }
    int table_id = CMARK_NODE_TABLE;
    int strike_id = CMARK_NODE_STRIKETHROUGH;

    /* the real MSHUTDOWN body */
    zm_shutdown_mdparser(0, 0);

    /* second MINIT's registration step; resolve_extensions does
     * cmark_find_syntax_extension and E_CORE_ERRORs on NULL */
    cmark_gfm_core_extensions_ensure_registered();
    if (!cmark_find_syntax_extension("table")) {
        printf("FAIL: registry empty after MSHUTDOWN + re-MINIT registration\n");
        return 1;
    }
    if (CMARK_NODE_TABLE != table_id || CMARK_NODE_STRIKETHROUGH != strike_id) {
        printf("FAIL: extension node-type ids drifted across the cycle\n");
        return 1;
    }

    /* release again: the process-shutdown path must stay clean too */
    cmark_release_plugins();

    printf("OK: extensions re-register across MSHUTDOWN/MINIT cycle\n");
    return 0;
}
C);

$objs = array_merge(
    [$build . '/.libs/mdparser.o'],
    glob($build . '/vendor/cmark/src/.libs/*.o'),
    glob($build . '/vendor/cmark/extensions/.libs/*.o')
);

$cmd = 'cc -o ' . escapeshellarg($bin) . ' ' . escapeshellarg($src) . ' '
     . implode(' ', array_map('escapeshellarg', $objs))
     . ' -Wl,--unresolved-symbols=ignore-all 2>&1';
$out = (string)shell_exec($cmd);
if (!is_file($bin)) {
    echo "FAIL: harness did not compile:\n$out";
    exit(1);
}

passthru(escapeshellarg($bin), $rc);
@unlink($src);
@unlink($bin);
@unlink($tmp);

?>
--EXPECT--
OK: extensions re-register across MSHUTDOWN/MINIT cycle
