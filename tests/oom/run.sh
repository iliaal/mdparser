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

trap 'rm -rf "$tmp"' EXIT

"$cc" -g -O0 -fsanitize=address,undefined -I "$vendor" \
    -o "$bin" "$here/oom_sweep.c" "$vendor/entity.c" || exit 2

status=0
for doc in "$here"/corpus/*.md; do
    total="$("$bin" "$doc" -1 | sed -n 's/.*allocs=\([0-9]*\).*/\1/p')"
    if [ -z "$total" ] || [ "$total" -eq 0 ]; then
        echo "FAIL $(basename "$doc"): baseline parse allocated nothing to sweep"
        status=1
        continue
    fi

    bad=0
    for n in $(seq 1 "$total"); do
        out="$("$bin" "$doc" "$n" 2>&1)"
        rc=$?
        if printf '%s' "$out" | grep -q 'ERROR: \(Address\|Leak\)Sanitizer'; then
            bad=$((bad + 1))
            echo "  $(basename "$doc") fail_at=$n -> $(printf '%s' "$out" | grep -m1 'ERROR:')"
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
