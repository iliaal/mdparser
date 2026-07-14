#!/usr/bin/env bash
set -euo pipefail

echo "======================================================================"
echo " PIE install smoke test for iliaal/mdparser"
echo "======================================================================"
echo
echo "PHP:"
php --version | head -1
echo "phpize:"
phpize --version 2>&1 | head -2
echo

echo "---- 1. System build tools ----"
apt-get update -qq >/dev/null
# PIE 1.4 on a bare php:8.x-cli image needs five extras beyond what
# phpize expects: git (PIE clones source via git clone, not a tarball),
# bison and libtoolize (PIE's build-tools check insists on both even
# though phpize itself doesn't), ca-certificates (for the HTTPS clone
# from github), and `unzip` — composer shells out to /usr/bin/unzip
# when extracting the prebuilt-binary zip PIE sets via setDistUrl().
# If unzip is missing, composer silently falls back to PHP's ZipArchive
# which lays out the file at a path PIE's prePackagedBinary check
# doesn't look at, and install fails with ExtensionBinaryNotFound even
# though the zip downloaded fine. `php:8.x-cli` Debian images do not
# ship unzip. See ~/ai/wiki/debugging/php-ext-release-traps.md.
apt-get install -y -qq git ca-certificates bison libtool-bin unzip >/dev/null
git --version
bison --version | head -1
libtoolize --version | head -1 || echo "libtoolize not found"
echo

echo "---- 2. Fresh clone from mounted source (avoids host build artifacts) ----"
git config --global --add safe.directory /mdparser
git config --global --add safe.directory /mdparser/.git
git clone -q file:///mdparser /tmp/src
cd /tmp/src
HEAD_DESCRIPTION=$(git log --oneline -1)
TAG_DESCRIPTION=$(git describe --tags --always)
echo "HEAD: ${HEAD_DESCRIPTION}"
echo "tag:  ${TAG_DESCRIPTION}"
for required_file in composer.json config.m4 php_mdparser.h; do
	test -f "${required_file}"
	echo "${required_file}"
done
echo

echo "---- 3. Install Composer ----"
curl -sS https://getcomposer.org/installer | php -- --quiet
mv composer.phar /usr/local/bin/composer
composer --version | head -1
echo

echo "---- 4. Download PIE ----"
curl -sSL https://github.com/php/pie/releases/latest/download/pie.phar -o /usr/local/bin/pie
chmod +x /usr/local/bin/pie
ls -la /usr/local/bin/pie
pie --version 2>&1 | head -3
echo

echo "---- 5. pie install (against Packagist, real-user path) ----"
# Smoke runs from /release-ext after the tag is published, so Packagist
# already serves the new version. This is the canonical user install:
# `pie install iliaal/mdparser` resolves to the freshly-tagged release,
# picks up the prebuilt zip when a matching <php-ver, arch, libc> lane
# exists on the release, and falls back to PIE's composer-default source
# build otherwise. Either path must succeed through PIE itself.
echo "   pie install iliaal/mdparser"
set +e
pie install \
	--with-php-config=/usr/local/bin/php-config \
	--auto-install-build-tools \
	iliaal/mdparser >/tmp/pie.out 2>&1
PIE_STATUS=$?
set -e
tail -25 /tmp/pie.out
if ((PIE_STATUS != 0)); then
	echo "   PIE install failed with status ${PIE_STATUS}" >&2
	exit "${PIE_STATUS}"
fi
if ! php -m | grep -qi mdparser; then
	echo "   PIE exited successfully but mdparser is not loaded" >&2
	exit 1
fi
echo "   PIE install: success"
echo

echo "---- 6. Verify PIE-installed extension loads ----"
php -m | grep -i mdparser
php -r 'echo "mdparser version: ", phpversion("mdparser"), PHP_EOL;'
echo

echo "---- 7. Functional smoke test ----"
# The PHP code is intentionally literal.
# shellcheck disable=SC2016
php -r '
$p = new MdParser\Parser();
$out = $p->toHtml("# Hello");
if ($out !== "<h1>Hello</h1>\n") { echo "heading FAIL: ", var_export($out, true), "\n"; exit(1); }
echo "heading OK\n";

$out = $p->toHtml("~~strike~~");
if ($out !== "<p><del>strike</del></p>\n") { echo "strike FAIL: ", var_export($out, true), "\n"; exit(1); }
echo "strike OK\n";

$ast = $p->toAst("# hi");
if (!is_array($ast) || $ast["type"] !== "document") { echo "ast FAIL\n"; exit(1); }
echo "ast OK\n";

$o = new MdParser\Options(smart: true);
$p2 = new MdParser\Parser($o);
$out = $p2->toHtml("---");
if ($out !== "<hr />\n") { echo "hr FAIL: ", var_export($out, true), "\n"; exit(1); }
echo "hr OK\n";

$out = $p->toHtml("| a | b |\n|---|---|\n| 1 | 2 |\n");
if (!str_contains($out, "<table>") || !str_contains($out, "<td>1</td>")) {
    echo "table FAIL: ", var_export($out, true), "\n"; exit(1);
}
echo "table OK\n";
'
echo
echo "======================================================================"
echo " PIE install smoke test: PASSED"
echo "======================================================================"
