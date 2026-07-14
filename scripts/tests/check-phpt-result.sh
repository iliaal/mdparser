#!/usr/bin/env bash

set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
gate="${root}/scripts/check-phpt-result.sh"
temporary_directory=$(mktemp -d)
trap 'rm -rf -- "${temporary_directory}"' EXIT

write_summary() {
	local file="${1}"
	local total="${2}"
	local skipped="${3}"
	local warned="${4}"
	local failed="${5}"
	local passed="${6}"

	printf '%s\n' \
		"Number of tests : ${total} ${total}" \
		"Tests skipped   : ${skipped}" \
		"Tests warned    : ${warned}" \
		"Tests failed    : ${failed}" \
		"Tests passed    : ${passed}" >"${file}"
}

expect_success() {
	local label="${1}"
	shift
	if "$@" >/dev/null 2>&1; then
		printf 'OK: %s\n' "${label}"
	else
		printf 'FAIL: %s\n' "${label}"
		return 1
	fi
}

expect_failure() {
	local label="${1}"
	shift
	if "$@" >/dev/null 2>&1; then
		printf 'FAIL: %s\n' "${label}"
		return 1
	fi
	printf 'OK: %s\n' "${label}"
}

summary="${temporary_directory}/summary.txt"

write_summary "${summary}" 2 0 0 0 2
expect_success "clean run accepted" "${gate}" "${summary}" 0

write_summary "${summary}" 2 1 0 0 1
expect_success "declared skip accepted" "${gate}" "${summary}" 0 1
expect_failure "unexpected skip rejected" "${gate}" "${summary}" 0 0

write_summary "${summary}" 2 0 1 0 1
expect_failure "warning rejected" "${gate}" "${summary}" 0

write_summary "${summary}" 3 0 0 0 2
expect_failure "incomplete summary rejected" "${gate}" "${summary}" 0

write_summary "${summary}" 2 0 0 0 2
expect_failure "nonzero runner status rejected" "${gate}" "${summary}" 139
