#!/usr/bin/env bash
# Sweep every md4c allocation-failure point of tests/oom/corpus under ASAN.
#
# Not part of `make test`: it needs its own ASAN build of the vendored parser
# rather than the extension .so, and it reaches every error path rather than
# only those near a parse_memory_limit boundary. Run it after touching
# vendor/md4c/md4c.c and as step 4 of a vendor refresh (see vendor/VENDOR.md).
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
vendor="$here/../../vendor/md4c"
cc="${CC:-gcc}"
tmp="$(mktemp -d)"
bin="$tmp/oom_sweep"
ubsan_bin="$tmp/ubsan_signed_overflow"

# UBSan uses the first value for duplicate keys, so inherited exitcode=0 or
# halt_on_error=0 must be removed before the effective options are exported.
ubsan_options="${UBSAN_OPTIONS:-}"
normalized_ubsan_options=""
old_ifs="$IFS"
IFS=:
set -f
set -- $ubsan_options
set +f
IFS="$old_ifs"
for option in "$@"; do
    case "${option%%=*}" in
        halt_on_error|exitcode) continue ;;
    esac
    if [ -n "$normalized_ubsan_options" ]; then
        normalized_ubsan_options="$normalized_ubsan_options:$option"
    else
        normalized_ubsan_options="$option"
    fi
done
if [ -n "$normalized_ubsan_options" ]; then
    export UBSAN_OPTIONS="$normalized_ubsan_options:halt_on_error=1:exitcode=1"
else
    export UBSAN_OPTIONS="halt_on_error=1:exitcode=1"
fi

trap 'rm -rf "$tmp"' EXIT

"$cc" -g -O0 -fsanitize=address,undefined -I "$vendor" \
    -o "$bin" "$here/oom_sweep.c" "$vendor/entity.c" || exit 2

"$cc" -g -O0 -fsanitize=address,undefined -o "$ubsan_bin" \
    "$here/ubsan_signed_overflow.c" || exit 2

status=0
gate_out="$("$ubsan_bin" 2>&1)"
gate_rc=$?
if [ "$gate_rc" -eq 0 ] || ! printf '%s' "$gate_out" | grep -q 'runtime error: signed integer overflow'; then
    echo "FAIL UBSan gate: signed overflow was not reported as fatal"
    status=1
else
    echo "PASS UBSan gate (signed overflow is fatal)"
fi

for doc in "$here"/corpus/*.md; do
    baseline="$("$bin" "$doc" -1)"
    total="$(printf '%s\n' "$baseline" | sed -n 's/.*allocs=\([0-9]*\).*/\1/p')"
    if [ "$(basename "$doc")" = "06-wikilink.md" ] && \
       ! printf '%s\n' "$baseline" | grep -Eq 'wikilinks=[1-9][0-9]*'; then
        echo "FAIL $(basename "$doc"): MD_SPAN_WIKILINK was not reached"
        status=1
    fi
    if [ -z "$total" ] || [ "$total" -eq 0 ]; then
        echo "FAIL $(basename "$doc"): baseline parse allocated nothing to sweep"
        status=1
        continue
    fi

    bad=0
    for n in $(seq 1 "$total"); do
        out="$("$bin" "$doc" "$n" 2>&1)"
        rc=$?
        if printf '%s' "$out" | grep -Eq 'ERROR: (Address|Leak)Sanitizer|UndefinedBehaviorSanitizer|runtime error:'; then
            bad=$((bad + 1))
            echo "  $(basename "$doc") fail_at=$n -> $(printf '%s' "$out" | grep -m1 -E 'ERROR:|UndefinedBehaviorSanitizer|runtime error:')"
        elif [ "$rc" -ne 0 ]; then
            bad=$((bad + 1))
            echo "  $(basename "$doc") fail_at=$n -> $(printf '%s' "$out" | tail -1)"
        fi
    done

    if [ "$bad" -eq 0 ]; then
        echo "PASS $(basename "$doc") ($total allocation points)"
    else
        echo "FAIL $(basename "$doc") ($bad of $total allocation points)"
        status=1
    fi
done

exit "$status"
