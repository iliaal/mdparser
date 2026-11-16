#!/usr/bin/env bash

set -euo pipefail

usage() {
	printf 'Usage: %s <run-tests-output> <runner-exit-status> [expected-skips]\n' \
		"${0##*/}" >&2
}

# Counts are decimal, including zero-padded fields. Strip leading zeroes before
# Bash arithmetic and printf, both of which otherwise interpret them as octal.
decimal_count() {
	local value="${1}"
	value=${value#"${value%%[!0]*}"}
	printf '%s' "${value:-0}"
}

main() {
	# Bash numeric ranges must accept only ASCII digits before arithmetic.
	local LC_ALL=C

	if [[ $# -lt 2 || $# -gt 3 ]]; then
		usage
		return 2
	fi

	local result_file="${1}"
	local runner_status="${2}"
	local expected_skips="${3:-0}"

	if [[ ! -f "${result_file}" ]]; then
		printf 'ERROR: PHPT result file not found: %s\n' "${result_file}" >&2
		return 1
	fi
	if [[ ! "${runner_status}" =~ ^[0-9]+$ ]]; then
		printf 'ERROR: invalid runner exit status: %s\n' "${runner_status}" >&2
		return 2
	fi
	if [[ ! "${expected_skips}" =~ ^[0-9]+$ ]]; then
		printf 'ERROR: invalid expected skip count: %s\n' "${expected_skips}" >&2
		return 2
	fi
	runner_status=$(decimal_count "${runner_status}")
	expected_skips=$(decimal_count "${expected_skips}")
	if ((runner_status != 0)); then
		printf 'ERROR: PHPT runner exited with status %d\n' "${runner_status}" >&2
		return 1
	fi

	local total failed passed skipped warned
	total=$(awk '/Number of tests/ { print $5; exit }' "${result_file}")
	failed=$(awk '/Tests failed/ { print $4; exit }' "${result_file}")
	passed=$(awk '/Tests passed/ { print $4; exit }' "${result_file}")
	skipped=$(awk '/Tests skipped/ { print $4; exit }' "${result_file}")
	warned=$(awk '/Tests warned/ { print $4; exit }' "${result_file}")

	if [[ -z "${total}" || -z "${failed}" || -z "${passed}" ||
		-z "${skipped}" || -z "${warned}" ]]; then
		printf 'ERROR: PHPT summary is missing from %s\n' "${result_file}" >&2
		return 1
	fi
	if [[ ! "${total}" =~ ^[0-9]+$ || ! "${failed}" =~ ^[0-9]+$ ||
		! "${passed}" =~ ^[0-9]+$ || ! "${skipped}" =~ ^[0-9]+$ ||
		! "${warned}" =~ ^[0-9]+$ ]]; then
		printf 'ERROR: PHPT summary contains invalid counts\n' >&2
		return 1
	fi
	local field
	for field in total failed passed skipped warned; do
		printf -v "${field}" '%s' "$(decimal_count "${!field}")"
	done
	if ((failed != 0)); then
		printf 'ERROR: %d PHPT test(s) failed\n' "${failed}" >&2
		return 1
	fi
	if ((warned != 0)); then
		printf 'ERROR: %d PHPT test(s) warned\n' "${warned}" >&2
		return 1
	fi
	if ((skipped != expected_skips)); then
		printf 'ERROR: expected %d skipped PHPT test(s), observed %d\n' \
			"${expected_skips}" "${skipped}" >&2
		return 1
	fi
	if ((passed + skipped != total)); then
		printf 'ERROR: PHPT summary accounts for %d of %d tests\n' \
			"$((passed + skipped))" "${total}" >&2
		return 1
	fi
	if ((passed == 0)); then
		printf 'ERROR: PHPT run executed no passing tests\n' >&2
		return 1
	fi

	printf 'PHPT gate: %d passed, %d skipped, 0 warned, 0 failed\n' \
		"${passed}" "${skipped}"
}

main "$@"
