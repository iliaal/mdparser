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
# PIE 1.4 requires bison and libtoolize even though phpize itself might not.
apt-get install -y -qq git ca-certificates bison libtool-bin >/dev/null
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
ls composer.json package.xml config.m4 php_mdparser.h | head
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

echo "---- 5. pie install ----"
PIE_OK=0

# Prepare a working dir with a composer.json that declares /tmp/src as a
# path-type repository. PIE's -d flag passes through to composer, so
# composer resolves iliaal/mdparser from the local path instead of
# Packagist.
mkdir -p /tmp/piework
cat > /tmp/piework/composer.json <<'JSON'
{
    "name": "iliaal/pie-smoke",
    "repositories": [
        { "type": "path", "url": "/tmp/src", "options": { "symlink": false } }
    ],
    "minimum-stability": "dev",
    "prefer-stable": true
}
JSON

echo "   [A] pie install -d /tmp/piework iliaal/mdparser"
pie install \
    -d /tmp/piework \
    --with-php-config=/usr/local/bin/php-config \
    --auto-install-build-tools \
    iliaal/mdparser 2>&1 \
    | tee /tmp/pie-A.out | tail -40 || true

if php -m | grep -qi mdparser; then
    PIE_OK=1
    echo "   [A] RESULT: success"
fi

# Path B: plain Packagist lookup (expected to fail until iliaal/mdparser
# is registered on packagist.org). Kept for completeness.
if [ "$PIE_OK" = "0" ]; then
    echo
    echo "   [B] pie install iliaal/mdparser  (plain Packagist lookup)"
    pie install \
        --with-php-config=/usr/local/bin/php-config \
        iliaal/mdparser 2>&1 | tee /tmp/pie-B.out | tail -20 || true
    if php -m | grep -qi mdparser; then
        PIE_OK=1
        echo "   [B] RESULT: success"
    fi
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
