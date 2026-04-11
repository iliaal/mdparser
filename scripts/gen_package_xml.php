<?php
/**
 * Generate package.xml's <contents> listing from the current file tree.
 *
 * Walks the repo root and emits PECL v2 <dir>/<file> entries for every
 * file that ships in a release tarball. Skips build artifacts, CI state,
 * upstream snapshots, and the .git tree.
 *
 * Usage: php scripts/gen_package_xml.php > /tmp/contents.xml
 *
 * Paste the result into package.xml between the <contents> markers.
 */

$root = dirname(__DIR__);

/* These directories are part of the repo but not part of the PECL
 * tarball. docs/, examples/, and bench/ are dev/reference material
 * that lives on GitHub; PECL users install via `pecl install mdparser`
 * and get a minimal source+tests+vendor tree. */
$ignore_path_prefixes = [
    '.git',
    '.github',
    '.libs',
    'build',
    'modules',
    'autom4te.cache',
    'vendor/upstream',
    'scripts',
    'bench',
    'docs',
    'examples',
];

$ignore_names = [
    'configure', 'configure.ac', 'libtool', 'run-tests.php', 'Makefile',
    'Makefile.fragments', 'Makefile.global', 'Makefile.objects',
    'mdparser_arginfo.h', 'config.h', 'config.h.in', 'config.status',
    'config.log', 'config.nice', 'tmp-php.ini', '.gitignore',
    'SECURITY.md',
];

$ignore_suffixes = ['.la', '.lai', '.lo', '.o', '.so', '.diff', '.exp', '.out', '.log', '.dep'];

function should_include(string $rel): bool {
    global $ignore_path_prefixes, $ignore_names, $ignore_suffixes;
    foreach ($ignore_path_prefixes as $p) {
        if ($rel === $p || str_starts_with($rel, "$p/")) return false;
    }
    $base = basename($rel);
    if (in_array($base, $ignore_names, true)) return false;
    if ($base[0] === '.' && $base !== '.gitignore') return false;
    foreach ($ignore_suffixes as $s) {
        if (str_ends_with($rel, $s)) return false;
    }
    return true;
}

function role_for(string $rel): string {
    $base = basename($rel);
    if (str_ends_with($base, '.phpt')) return 'test';
    if (str_starts_with($rel, 'tests/')) return 'test';
    if (in_array($base, ['README.md', 'CHANGELOG.md', 'LICENSE', 'CREDITS', 'CLAUDE.md'], true)) return 'doc';
    if (str_ends_with($base, '.md') && !str_starts_with($rel, 'vendor/')) return 'doc';
    return 'src';
}

$files = [];
$it = new RecursiveIteratorIterator(
    new RecursiveDirectoryIterator($root, RecursiveDirectoryIterator::SKIP_DOTS)
);
foreach ($it as $path => $info) {
    if (!$info->isFile()) continue;
    $rel = substr($path, strlen($root) + 1);
    if (!should_include($rel)) continue;
    $files[] = $rel;
}
sort($files);

// Group files into a nested tree by directory.
$tree = [];
foreach ($files as $rel) {
    $parts = explode('/', $rel);
    $file = array_pop($parts);
    $node = &$tree;
    foreach ($parts as $p) {
        if (!isset($node[$p])) $node[$p] = [];
        $node = &$node[$p];
    }
    $node['__files'][] = $file;
    unset($node);
}

function xml_attr(string $s): string {
    return htmlspecialchars($s, ENT_XML1 | ENT_QUOTES, 'UTF-8');
}

function emit(array $tree, string $indent, string $path_prefix = ''): void {
    $files = $tree['__files'] ?? [];
    unset($tree['__files']);
    sort($files);
    foreach ($files as $f) {
        $rel = $path_prefix === '' ? $f : "$path_prefix/$f";
        $role = role_for($rel);
        printf("%s<file name=\"%s\" role=\"%s\" />\n", $indent, xml_attr($f), $role);
    }
    ksort($tree);
    foreach ($tree as $dir => $sub) {
        printf("%s<dir name=\"%s\">\n", $indent, xml_attr($dir));
        emit($sub, $indent . '  ', $path_prefix === '' ? $dir : "$path_prefix/$dir");
        echo "$indent</dir>\n";
    }
}

echo "<contents>\n";
echo "  <dir name=\"/\">\n";
emit($tree, '    ');
echo "  </dir>\n";
echo "</contents>\n";
