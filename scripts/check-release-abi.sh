#!/usr/bin/env bash

set -euo pipefail

usage() {
	printf 'Usage: %s <extension.so> [php-binary]\n' "${0##*/}" >&2
}

dynamic_symbols() {
	nm -D "$@" | awk '{ print $NF }' | sed 's/@.*//' | sort -u
}

shared_libraries() {
	ldd "$1" | awk '{
		for (i = 1; i <= NF; i++) {
			if (substr($i, 1, 1) == "/") {
				print $i
				break
			}
		}
	}'
}

main() {
	if [[ $# -lt 1 || $# -gt 2 ]]; then
		usage
		return 2
	fi

	local extension="${1}"
	local php_bin="${2:-$(command -v php)}"
	local temporary_directory library
	local -a providers

	temporary_directory=$(mktemp -d)
	trap 'rm -rf -- "${temporary_directory}"' RETURN

	nm -D --undefined-only "${extension}" |
		awk '$1 == "U" { print $2 }' | sed 's/@.*//' | sort -u \
		>"${temporary_directory}/imports"

	providers=("${php_bin}")
	while IFS= read -r library; do
		providers+=("${library}")
	done < <({
		shared_libraries "${php_bin}"
		shared_libraries "${extension}"
	} | sort -u)
	for library in "${providers[@]}"; do
		dynamic_symbols --defined-only "${library}"
	done | sort -u >"${temporary_directory}/exports"

	if [[ ! -s "${temporary_directory}/imports" ]]; then
		printf 'ERROR: no undefined dynamic symbols read from %s\n' "${extension}" >&2
		return 1
	fi
	if ! grep -qx 'zend_parse_parameters' "${temporary_directory}/exports"; then
		printf 'ERROR: %s does not export the Zend API\n' "${php_bin}" >&2
		return 1
	fi

	comm -23 "${temporary_directory}/imports" "${temporary_directory}/exports" \
		>"${temporary_directory}/missing"
	if [[ -s "${temporary_directory}/missing" ]]; then
		printf 'ERROR: %s imports symbols that %s (%s) and its libraries do not export:\n' \
			"${extension}" "${php_bin}" "$("${php_bin}" -n -r 'echo PHP_VERSION;')" >&2
		sed 's/^/  /' "${temporary_directory}/missing" >&2
		return 1
	fi

	printf 'OK: all %d imports of %s resolve against PHP %s\n' \
		"$(wc -l <"${temporary_directory}/imports")" "${extension}" \
		"$("${php_bin}" -n -r 'echo PHP_VERSION;')"
}

main "$@"
