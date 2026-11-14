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
    v1.2.3 1.2.3-alpha_beta; do
    check_version "${version}" 1
    check_version "${version}" 1 "${version}"
done

check_version 1.2.3 1 1.2.4
