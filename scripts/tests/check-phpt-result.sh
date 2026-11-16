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

# Require the intended validation error, not an incidental arithmetic/printf error.
expect_invalid_count() {
	local label="${1}" expected_status="${2}" diagnostic="${3}" status=0
	shift 3
	"$@" >"${temporary_directory}/output" 2>&1 || status=$?
	if [[ "${status}" != "${expected_status}" ]] ||
		! grep -Fq "${diagnostic}" "${temporary_directory}/output" ||
		grep -Fq 'PHPT gate:' "${temporary_directory}/output"; then
		printf 'FAIL: %s (status %s)\n' "${label}" "${status}"
		cat "${temporary_directory}/output"
		return 1
	fi
	printf 'OK: %s\n' "${label}"
}

check_non_ascii_counts() {
	local field total skipped warned failed passed
	write_summary "${summary}" 2 0 0 0 2
	expect_invalid_count 'non-ASCII runner status rejected' 2 'ERROR: invalid runner exit status:' \
		"${gate}" "${summary}" '²'
	expect_invalid_count 'non-ASCII skip argument rejected' 2 'ERROR: invalid expected skip count:' \
		"${gate}" "${summary}" 0 '²'
	for field in total skipped warned failed passed; do
		total=2 skipped=0 warned=0 failed=0 passed=2
		printf -v "${field}" '%s' '²'
		write_summary "${summary}" "${total}" "${skipped}" "${warned}" "${failed}" "${passed}"
		expect_invalid_count "non-ASCII ${field} rejected" 1 'ERROR: PHPT summary contains invalid counts' \
			"${gate}" "${summary}" 0
	done
}

LC_ALL=C check_non_ascii_counts
non_c_locale=$(locale -a | awk '!found && tolower($0) ~ /^(de_de|en_us)\.utf-?8$/ { print; found=1 }')
if [[ -n "${non_c_locale}" ]]; then
	LC_ALL="${non_c_locale}" check_non_ascii_counts
	write_summary "${summary}" 2 0 0 0 2
	expect_success 'clean non-C run accepted' env LC_ALL="${non_c_locale}" "${gate}" "${summary}" 0
else
	printf 'SKIP: non-C locale coverage (de_DE/en_US UTF-8 not installed)\n'
fi


write_summary "${summary}" 2 0 0 0 2
expect_invalid_count 'zero-padded runner status rejected as nonzero' 1 'ERROR: PHPT runner exited with status 8' \
	"${gate}" "${summary}" 08
expect_invalid_count 'zero-padded skip mismatch rejected' 1 'ERROR: expected 8 skipped PHPT test(s), observed 0' \
	"${gate}" "${summary}" 0 08
write_summary "${summary}" 010 08 00 00 02
expect_success 'zero-padded decimal summary and arguments accepted' "${gate}" "${summary}" 00 08
write_summary "${summary}" 010 00 00 00 08
expect_invalid_count 'zero-padded total mismatch rejected' 1 'ERROR: PHPT summary accounts for 8 of 10 tests' \
	"${gate}" "${summary}" 0
for field in failed warned; do
	failed=0 warned=0
	printf -v "${field}" '%s' 08
	write_summary "${summary}" 10 0 "${warned}" "${failed}" 2
	expect_invalid_count "zero-padded ${field} rejected" 1 "PHPT test(s) ${field}" \
		"${gate}" "${summary}" 0
done
write_summary "${summary}" 08 08 00 00 00
expect_invalid_count 'zero-padded no-passing run rejected' 1 'ERROR: PHPT run executed no passing tests' \
	"${gate}" "${summary}" 0 08
