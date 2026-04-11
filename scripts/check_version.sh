#!/usr/bin/env bash
#
# Verify PHP_MDPARSER_VERSION in php_mdparser.h matches the top
# section of CHANGELOG.md. Run before cutting a new release tag.
#
# Exits 0 on match, 1 on mismatch or parse failure.
#
# Usage:   scripts/check_version.sh

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"

header_version=$(
    grep -E '^#define[[:space:]]+PHP_MDPARSER_VERSION' "$root/php_mdparser.h" \
    | sed -E 's/.*"([^"]+)".*/\1/'
)

if [[ -z "$header_version" ]]; then
    echo "ERROR: could not parse PHP_MDPARSER_VERSION from php_mdparser.h" >&2
    exit 1
fi

changelog_version=$(
    grep -E '^## \[([0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.]+)?)\]' "$root/CHANGELOG.md" \
    | head -1 \
    | sed -E 's/^## \[([^]]+)\].*/\1/'
)

if [[ -z "$changelog_version" ]]; then
    echo "ERROR: could not find a '## [X.Y.Z] - YYYY-MM-DD' section in CHANGELOG.md" >&2
    exit 1
fi

# SemVer sanity.
if ! [[ "$header_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$ ]]; then
    echo "ERROR: PHP_MDPARSER_VERSION '$header_version' is not a valid SemVer 2.0.0 string" >&2
    exit 1
fi

if [[ "$header_version" != "$changelog_version" ]]; then
    echo "FAIL: version mismatch"
    echo "  php_mdparser.h: $header_version"
    echo "  CHANGELOG.md:   $changelog_version"
    exit 1
fi

echo "OK: version $header_version consistent across php_mdparser.h and CHANGELOG.md"
