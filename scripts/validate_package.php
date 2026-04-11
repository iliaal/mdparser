<?php
/**
 * Validate package.xml without needing pear/pecl installed.
 *
 * Checks:
 *   1. package.xml is well-formed XML
 *   2. Every <file name="X" /> points at a file that actually exists
 *   3. Every shippable file in the repo is mentioned in package.xml
 *      (catches "added a file but forgot to list it" regressions)
 *   4. <version><release> matches php_mdparser.h's PHP_MDPARSER_VERSION
 *   5. <version><release> matches the top section in CHANGELOG.md
 *
 * Usage (from repo root):
 *     php scripts/validate_package.php
 *
 * Exits 0 on success, 1 on any failure.
 */

declare(strict_types=1);

$root = dirname(__DIR__);
$errors = [];
$warnings = [];

// --- 1. well-formed XML
$dom = new DOMDocument();
$dom->preserveWhiteSpace = false;
libxml_use_internal_errors(true);
$loaded = $dom->load("$root/package.xml");
if (!$loaded) {
    foreach (libxml_get_errors() as $e) {
        $errors[] = "package.xml parse error: " . trim($e->message) . " (line {$e->line})";
    }
    libxml_clear_errors();
    report($errors, $warnings);
    exit(1);
}
libxml_clear_errors();

// --- 2. walk <contents> and check file existence
$xp = new DOMXPath($dom);
$xp->registerNamespace('p', 'http://pear.php.net/dtd/package-2.0');

$listed = [];
$walk = function (DOMNode $node, string $prefix) use (&$walk, &$listed) {
    foreach ($node->childNodes as $child) {
        if (!$child instanceof DOMElement) continue;
        $name = $child->getAttribute('name');
        if ($child->localName === 'dir') {
            $sub = $prefix === '' || $prefix === '/'
                ? ($name === '/' ? '' : $name)
                : "$prefix/$name";
            $walk($child, $sub);
        } elseif ($child->localName === 'file') {
            $path = $prefix === '' ? $name : "$prefix/$name";
            $listed[$path] = $child->getAttribute('role') ?: 'src';
        }
    }
};
$contents = $xp->query('//p:contents')->item(0);
if (!$contents) {
    $errors[] = "no <contents> element found in package.xml";
} else {
    $walk($contents, '');
}

$missing_on_disk = [];
foreach ($listed as $rel => $role) {
    if (!file_exists("$root/$rel")) {
        $missing_on_disk[] = $rel;
    }
}
if ($missing_on_disk) {
    $errors[] = sprintf(
        "package.xml lists %d file(s) not on disk:\n  - %s",
        count($missing_on_disk),
        implode("\n  - ", $missing_on_disk)
    );
}

// --- 3. walk the tree and find shippable files NOT listed
// Mirrors scripts/gen_package_xml.php filter.
$ignore_prefixes = [
    '.git', '.github', '.libs', 'build', 'modules', 'autom4te.cache',
    'vendor/upstream', 'scripts', 'bench', 'docs', 'examples',
];
$ignore_names = [
    'configure', 'configure.ac', 'libtool', 'run-tests.php', 'Makefile',
    'Makefile.fragments', 'Makefile.global', 'Makefile.objects',
    'mdparser_arginfo.h', 'config.h', 'config.h.in', 'config.status',
    'config.log', 'config.nice', 'tmp-php.ini', '.gitignore',
    'SECURITY.md',
];
$ignore_suffixes = ['.la', '.lai', '.lo', '.o', '.so', '.diff', '.exp',
                    '.out', '.log', '.dep'];

function should_ship(string $rel, array $ign_prefix, array $ign_name, array $ign_suf): bool {
    foreach ($ign_prefix as $p) {
        if ($rel === $p || str_starts_with($rel, "$p/")) return false;
    }
    $base = basename($rel);
    if (in_array($base, $ign_name, true)) return false;
    if ($base[0] === '.' && $base !== '.gitignore') return false;
    foreach ($ign_suf as $s) {
        if (str_ends_with($rel, $s)) return false;
    }
    return true;
}

$on_disk = [];
$it = new RecursiveIteratorIterator(
    new RecursiveDirectoryIterator($root, RecursiveDirectoryIterator::SKIP_DOTS)
);
foreach ($it as $path => $info) {
    if (!$info->isFile()) continue;
    $rel = substr($path, strlen($root) + 1);
    if (should_ship($rel, $ignore_prefixes, $ignore_names, $ignore_suffixes)) {
        $on_disk[] = $rel;
    }
}
sort($on_disk);

$missing_in_xml = array_diff($on_disk, array_keys($listed));
if ($missing_in_xml) {
    $errors[] = sprintf(
        "%d file(s) exist on disk but are NOT listed in package.xml:\n  - %s",
        count($missing_in_xml),
        implode("\n  - ", $missing_in_xml)
    );
}

// --- 4. version cross-check: php_mdparser.h
$header = file_get_contents("$root/php_mdparser.h");
preg_match('/#define PHP_MDPARSER_VERSION "([^"]+)"/', $header, $m);
$header_version = $m[1] ?? null;

$release_node = $xp->query('//p:version/p:release')->item(0);
$xml_version = $release_node ? trim($release_node->nodeValue) : null;

if ($header_version === null) {
    $errors[] = "could not parse PHP_MDPARSER_VERSION from php_mdparser.h";
} elseif ($header_version !== $xml_version) {
    $errors[] = "version mismatch: php_mdparser.h says '$header_version', package.xml says '$xml_version'";
}

// --- 5. version cross-check: CHANGELOG.md
$changelog = file_get_contents("$root/CHANGELOG.md");
preg_match('/^## \[(\d+\.\d+\.\d+(?:-[a-zA-Z0-9]+)?)\]/m', $changelog, $m);
$cl_version = $m[1] ?? null;
if ($cl_version === null) {
    $warnings[] = "could not find a '## [X.Y.Z] - YYYY-MM-DD' section in CHANGELOG.md";
} elseif ($cl_version !== $xml_version) {
    $errors[] = "version mismatch: CHANGELOG.md top section says '$cl_version', package.xml says '$xml_version'";
}

// --- 6. semver sanity on $xml_version itself
if ($xml_version !== null &&
    !preg_match('/^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$/', $xml_version)) {
    $errors[] = "package.xml version '$xml_version' is not a valid SemVer 2.0.0 string";
}

// --- 7. required <stability>/<license>/<lead>/etc sanity
foreach (['name', 'channel', 'summary', 'description', 'lead', 'version',
          'stability', 'license', 'contents', 'dependencies',
          'providesextension'] as $elem) {
    if ($xp->query("//p:$elem")->length === 0) {
        $errors[] = "required element <$elem> missing from package.xml";
    }
}

function report(array $errors, array $warnings): void {
    foreach ($warnings as $w) echo "WARN: $w\n";
    foreach ($errors as $e) echo "ERROR: $e\n";
}

report($errors, $warnings);

if ($errors) {
    echo "\nFAIL — " . count($errors) . " error(s), " . count($warnings) . " warning(s)\n";
    exit(1);
}

echo "OK — validated package.xml (" . count($listed) . " files listed, "
   . count($on_disk) . " files on disk, "
   . count($warnings) . " warnings)\n";
echo "version: $xml_version (matches php_mdparser.h and CHANGELOG.md)\n";
exit(0);
