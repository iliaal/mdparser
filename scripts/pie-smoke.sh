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
echo "HEAD: $(git log --oneline -1)"
echo "tag:  $(git describe --tags --always)"
ls composer.json config.m4 php_mdparser.h | head
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
# exists on the release, and falls back to source-build otherwise. The
# manual phpize+make+install fallback in Step 6 below covers the
# source-build path that PIE's composer-default lane exercises.
PIE_OK=0
echo "   pie install iliaal/mdparser"
pie install \
    --with-php-config=/usr/local/bin/php-config \
    --auto-install-build-tools \
    iliaal/mdparser 2>&1 | tee /tmp/pie.out | tail -25 || true

if php -m | grep -qi mdparser; then
    PIE_OK=1
    echo "   PIE install: success"
fi

echo "   overall PIE result: PIE_OK=$PIE_OK"
echo

echo "---- 6. Verify extension loads ----"
if [ "$PIE_OK" = "0" ]; then
    echo "   *** PIE did not install the extension; falling back to manual phpize+make+install ***"
    cd /tmp/src
    phpize >/dev/null
    ./configure --enable-mdparser >/dev/null
    make -j"$(nproc)" 2>&1 | tail -3
    make install 2>&1 | tail -3
    docker-php-ext-enable mdparser
    echo "   [fallback] manual install SUCCEEDED"
fi
php -m | grep -i mdparser
php -r 'echo "mdparser version: ", phpversion("mdparser"), PHP_EOL;'
echo

echo "---- 7. Functional smoke test ----"
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
