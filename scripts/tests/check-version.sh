#!/usr/bin/env bash

set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
fixture=$(mktemp -d)
trap 'rm -rf -- "${fixture}"' EXIT
mkdir "${fixture}/scripts"
cp "${root}/scripts/check_version.sh" "${fixture}/scripts/"

check_version() {
    local version="${1}" expected="${2}" heading="${3:-Unreleased}" status=0
    printf '#define PHP_MDPARSER_VERSION "%s"\n' "${version}" >"${fixture}/php_mdparser.h"
    printf '## [%s]\n' "${heading}" >"${fixture}/CHANGELOG.md"
    bash "${fixture}/scripts/check_version.sh" >"${fixture}/output" 2>&1 || status=$?
    if [[ "${status}" != "${expected}" ]]; then
        printf 'FAIL: %s with [%s]: expected %s, got %s\n' "${version}" "${heading}" "${expected}" "${status}"
        cat "${fixture}/output"
        return 1
    fi
    printf 'OK: %s with [%s]\n' "${version}" "${heading}"
}

for version in 0.0.0 1.2.3 10.20.30 1.2.3-0 1.2.3-alpha.1 1.2.3-01a \
    1.2.3-- 1.2.3+001 1.2.3+build.01 1.2.3-rc.1+build.001; do
    check_version "${version}" 0
    check_version "${version}" 0 "${version}"
done

for version in 01.2.3 1.02.3 1.2.03 1.2.3-01 1.2.3-alpha.01 \
    1.2.3-.alpha 1.2.3-alpha. 1.2.3-alpha..1 1.2.3+.build \
    1.2.3+build. 1.2.3+build..1 1.2.3- 1.2.3+ 1.2 1.2.3.4 \
    v1.2.3 1.2.3-alpha_beta 1.2.3+build_1 1x2x3 \
    1.2.3-é 1.2.3+é 1.2.3-ß 1.2.3+İ; do
    check_version "${version}" 1
    check_version "${version}" 1 "${version}"
done

check_version 1.2.3 1 1.2.4

# Exercise a locale whose collation expands [A-Za-z] beyond ASCII, if installed.
# Minimal systems may have only C locales; report that coverage limitation.
non_c_locale=$(locale -a | awk '!found && tolower($0) ~ /^(de_de|en_us)\.utf-?8$/ { print; found=1 }')
if [[ -n "${non_c_locale}" ]]; then
    for version in 1.2.3-é 1.2.3+é 1.2.3-ß 1.2.3+İ; do
        LC_ALL="${non_c_locale}" check_version "${version}" 1
        LC_ALL="${non_c_locale}" check_version "${version}" 1 "${version}"
    done
    LC_ALL="${non_c_locale}" check_version 1.2.3-rc.1+build.001 0
else
    printf 'SKIP: non-C locale coverage (de_DE/en_US UTF-8 not installed)\n'
fi
