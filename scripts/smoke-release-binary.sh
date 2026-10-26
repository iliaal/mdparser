#!/usr/bin/env bash

set -euo pipefail

usage() {
	printf 'Usage: %s <package.zip> <release-tag> <php-minor> {image=REF | php=PATH}...\n' \
		"${0##*/}" >&2
}

main() {
	if [[ $# -lt 4 ]]; then
		usage
		return 2
	fi

	local package="${1}" release_tag="${2}" php_minor="${3}"
	shift 3
	local script_dir temporary_directory target output status failures=0

	script_dir=$(cd -- "${BASH_SOURCE[0]%/*}" && pwd -P)
	temporary_directory=$(mktemp -d)
	trap 'rm -rf -- "${temporary_directory}"' RETURN
	unzip -q "${package}" -d "${temporary_directory}"
	cp "${script_dir}/release-abi-smoke.php" "${temporary_directory}/"
	chmod -R a+rX "${temporary_directory}"

	for target in "$@"; do
		set +e
		case "${target}" in
		image=*)
			output=$(docker run --rm --network none \
				-v "${temporary_directory}:/ext:ro" \
				-e MDPARSER_RELEASE_TAG="${release_tag}" \
				-e MDPARSER_PHP_MINOR="${php_minor}" \
				"${target#image=}" \
				php -n -d extension=/ext/mdparser.so /ext/release-abi-smoke.php 2>&1)
			status=$?
			;;
		php=*)
			output=$(MDPARSER_RELEASE_TAG="${release_tag}" \
				MDPARSER_PHP_MINOR="${php_minor}" \
				"${target#php=}" -n \
				-d "extension=${temporary_directory}/mdparser.so" \
				"${temporary_directory}/release-abi-smoke.php" 2>&1)
			status=$?
			;;
		*)
			set -e
			usage
			return 2
			;;
		esac
		set -e

		printf '%s: exit %d\n%s\n' "${target}" "${status}" "${output}"
		if ((status != 0)) || [[ "${output}" == *'undefined symbol'* ]] ||
			[[ "${output}" != *'OK: mdparser '* ]]; then
			printf 'ERROR: %s failed the release ABI smoke test\n' "${target}" >&2
			failures=$((failures + 1))
		fi
	done

	if ((failures != 0)); then
		return 1
	fi
}

main "$@"
