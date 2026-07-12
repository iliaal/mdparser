#!/usr/bin/env bash

set -euo pipefail

main() {
    if [[ $# -ne 0 ]]; then
        printf 'Usage: %s\n' "${0##*/}" >&2
        return 2
    fi

    local script_dir repo_root version_file changelog
    script_dir=$(cd -- "${BASH_SOURCE[0]%/*}" && pwd -P)
    repo_root=$(cd -- "${script_dir}/.." && pwd -P)
    version_file="${repo_root}/php_mdparser.h"
    changelog="${repo_root}/CHANGELOG.md"

    local version
    version=$(awk '/^#define[[:space:]]+PHP_MDPARSER_VERSION[[:space:]]/ {
        if (match($0, /"[^"]+"/)) {
            print substr($0, RSTART + 1, RLENGTH - 2)
            exit
        }
    }' "${version_file}")

    if [[ -z "${version}" ]]; then
        printf 'ERROR: could not parse PHP_MDPARSER_VERSION from %s\n' "${version_file}" >&2
        return 1
    fi
    if [[ ! "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$ ]]; then
        printf 'ERROR: PHP_MDPARSER_VERSION %s is not SemVer 2.0.0\n' "${version}" >&2
        return 1
    fi

    local top_section
    top_section=$(awk '/^## \[/ { print; exit }' "${changelog}")
    if [[ -z "${top_section}" ]]; then
        printf 'ERROR: no release section found in %s\n' "${changelog}" >&2
        return 1
    fi

    if [[ "${top_section}" == '## [Unreleased]'* ]]; then
        printf 'OK: PHP_MDPARSER_VERSION=%s; CHANGELOG.md starts with [Unreleased]\n' "${version}"
        return 0
    fi

    local changelog_version
    changelog_version="${top_section#*\[}"
    changelog_version="${changelog_version%%\]*}"
    if [[ "${version}" != "${changelog_version}" ]]; then
        printf 'ERROR: PHP_MDPARSER_VERSION=%s but CHANGELOG.md starts with [%s]\n' \
            "${version}" "${changelog_version}" >&2
        return 1
    fi

    printf 'OK: PHP_MDPARSER_VERSION=%s matches CHANGELOG.md\n' "${version}"
}

main "$@"
