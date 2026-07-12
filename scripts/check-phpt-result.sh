#!/usr/bin/env bash

set -euo pipefail

usage() {
    printf 'Usage: %s <run-tests-output> <runner-exit-status>\n' "${0##*/}" >&2
}

main() {
    if [[ $# -ne 2 ]]; then
        usage
        return 2
    fi

    local result_file="${1}"
    local runner_status="${2}"

    if [[ ! -f "${result_file}" ]]; then
        printf 'ERROR: PHPT result file not found: %s\n' "${result_file}" >&2
        return 1
    fi
    if [[ ! "${runner_status}" =~ ^[0-9]+$ ]]; then
        printf 'ERROR: invalid runner exit status: %s\n' "${runner_status}" >&2
        return 2
    fi
    if ((runner_status != 0)); then
        printf 'ERROR: PHPT runner exited with status %d\n' "${runner_status}" >&2
        return 1
    fi

    local failed passed
    failed=$(awk '/Tests failed/ { print $4; exit }' "${result_file}")
    passed=$(awk '/Tests passed/ { print $4; exit }' "${result_file}")

    if [[ -z "${failed}" || -z "${passed}" ]]; then
        printf 'ERROR: PHPT summary is missing from %s\n' "${result_file}" >&2
        return 1
    fi
    if [[ ! "${failed}" =~ ^[0-9]+$ || ! "${passed}" =~ ^[0-9]+$ ]]; then
        printf 'ERROR: PHPT summary contains invalid counts\n' >&2
        return 1
    fi
    if ((failed != 0)); then
        printf 'ERROR: %d PHPT test(s) failed\n' "${failed}" >&2
        return 1
    fi
    if ((passed == 0)); then
        printf 'ERROR: PHPT run executed no passing tests\n' >&2
        return 1
    fi

    printf 'PHPT gate: %d passed, 0 failed\n' "${passed}"
}

main "$@"
