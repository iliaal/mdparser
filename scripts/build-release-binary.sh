#!/usr/bin/env bash

set -euo pipefail

usage() {
	printf 'Usage: %s <release-tag>\n' "${0##*/}" >&2
}

main() {
	if [[ $# -ne 1 ]]; then
		usage
		return 2
	fi

	local release_tag="${1}"
	local phpize_cmd="${PHPIZE:-phpize}"
	local php_config="${PHP_CONFIG:-php-config}"
	local php_bin="${PHP_BIN:-}"
	local jobs="${JOBS:-}"
	local version php_version architecture operating_system libc suffix package
	local machine operating_system_name
	local temporary_directory

	scripts/check_version.sh
	version=$(sed -n 's/^#define PHP_MDPARSER_VERSION "\([^"]*\)"/\1/p' php_mdparser.h)
	if [[ "${version}" != "${release_tag}" ]]; then
		printf 'ERROR: release tag %s does not match extension version %s\n' \
			"${release_tag}" "${version}" >&2
		return 1
	fi

	if [[ -z "${php_bin}" ]]; then
		php_bin=$("${php_config}" --php-binary)
		if [[ "${php_bin}" == "NONE" || ! -x "${php_bin}" ]]; then
			php_bin=$(command -v php)
		fi
	fi
	if [[ -z "${jobs}" ]]; then
		if command -v nproc >/dev/null 2>&1; then
			jobs=$(nproc)
		else
			jobs=$(sysctl -n hw.ncpu)
		fi
	fi

	"${phpize_cmd}"
	./configure \
		--with-php-config="${php_config}" \
		--enable-mdparser \
		--enable-mdparser-dev
	make clean
	make -j"${jobs}"

	php_version=$("${php_config}" --version | awk -F. '{print $1 "." $2}')
	machine=$(uname -m)
	case "${machine}" in
	x86_64 | amd64) architecture=x86_64 ;;
	arm64 | aarch64) architecture=arm64 ;;
	*)
		printf 'ERROR: unsupported release architecture: %s\n' "${machine}" >&2
		return 1
		;;
	esac
	operating_system_name=$(uname -s)
	case "${operating_system_name}" in
	Linux)
		operating_system=linux
		if ldd --version 2>&1 | grep -qi musl; then
			libc=musl
		else
			libc=glibc
		fi
		;;
	Darwin)
		operating_system=darwin
		libc=bsdlibc
		;;
	*)
		printf 'ERROR: unsupported release operating system: %s\n' \
			"${operating_system_name}" >&2
		return 1
		;;
	esac

	suffix=$("${php_bin}" -n -r \
		'echo PHP_DEBUG ? "-debug" : ""; echo PHP_ZTS ? "-zts" : "";')
	package="php_mdparser-${release_tag}_php${php_version}-${architecture}-${operating_system}-${libc}${suffix}.zip"
	rm -f -- "${package}"
	zip -jq "${package}" modules/mdparser.so

	temporary_directory=$(mktemp -d)
	trap 'rm -rf -- "${temporary_directory}"' RETURN
	unzip -q "${package}" -d "${temporary_directory}"
	# The PHP code is intentionally literal.
	# shellcheck disable=SC2016
	MDPARSER_RELEASE_TAG="${release_tag}" "${php_bin}" -n \
		-d "extension=${temporary_directory}/mdparser.so" -r '
$version = phpversion("mdparser");
if ($version !== getenv("MDPARSER_RELEASE_TAG")) {
    fwrite(STDERR, "version mismatch: {$version}\n");
    exit(1);
}
$parser = new MdParser\Parser();
if ($parser->toHtml("# Release") !== "<h1>Release</h1>\n") {
    fwrite(STDERR, "HTML smoke test failed\n");
    exit(1);
}
$ast = $parser->toAst("a &amp; b");
if ($ast["type"] !== "document") {
    fwrite(STDERR, "AST smoke test failed\n");
    exit(1);
}
'

	if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
		printf 'package-path=%s\n' "${package}" >>"${GITHUB_OUTPUT}"
	else
		printf '%s\n' "${package}"
	fi
}

main "$@"
