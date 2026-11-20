#!/usr/bin/env bash
# Run the UTF-8 SIMD front end from several threads at once under
# ThreadSanitizer.
#
# Not part of `make test`: phpt runs one request per process, so it never
# has two threads inside the extension. Run it after touching
# mdparser_utf8.c. TSAN_RUNS sets the run count (default 5). The canary first proves TSan reports a known race, so a
# toolchain that silently drops the instrumentation fails instead of passing,
# and a zero prefix total means the SIMD path never ran, which also fails.
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$here/../.."
cc="${CC:-gcc}"
runs="${TSAN_RUNS:-5}"
tmp="$(mktemp -d)"
bin="$tmp/utf8_threads"
canary="$tmp/race_canary"

trap 'rm -rf "$tmp"' EXIT

# Older TSan runtimes abort with "unexpected memory mapping" on kernels with
# vm.mmap_rnd_bits above 28; running without ASLR avoids that.
norandom=()
if command -v setarch >/dev/null 2>&1 && setarch "$(uname -m)" -R true 2>/dev/null; then
    norandom=(setarch "$(uname -m)" -R)
fi

case "$runs" in
    ''|*[!0-9]*|0*)
        echo "FAIL: TSAN_RUNS must be a positive integer, got '$runs'"
        exit 2
        ;;
esac

# Fixed rather than inherited: a suppression or report_bugs=0 in the caller's
# TSAN_OPTIONS could hide the race under test while the canary still fires.
export TSAN_OPTIONS="halt_on_error=1:exitcode=66"

"$cc" -g -O1 -fsanitize=thread -pthread -I "$root" \
    -o "$bin" "$here/utf8_threads.c" "$root/mdparser_utf8.c" || exit 2
"$cc" -g -O1 -fsanitize=thread -pthread -o "$canary" "$here/race_canary.c" || exit 2

status=0
gate_out="$("${norandom[@]}" "$canary" 2>&1)"
gate_rc=$?
if [ "$gate_rc" -eq 0 ] || ! printf '%s' "$gate_out" | grep -q 'WARNING: ThreadSanitizer: data race'; then
    echo "FAIL TSan gate: the canary race was not reported"
    printf '%s\n' "$gate_out"
    exit 2
fi
echo "PASS TSan gate (canary race is reported)"

ran=0
for ((n = 1; n <= runs; n++)); do
    ran=$((ran + 1))
    out="$("${norandom[@]}" "$bin" 2>&1)"
    rc=$?
    if [ "$rc" -ne 0 ] || printf '%s' "$out" | grep -q 'ThreadSanitizer' \
       || ! printf '%s' "$out" | grep -q '^prefix_total=[1-9]'; then
        echo "FAIL run $n (exit $rc)"
        printf '%s\n' "$out"
        status=1
    else
        echo "PASS run $n ($(printf '%s' "$out" | grep '^prefix_total='))"
    fi
done

if [ "$ran" -ne "$runs" ]; then
    echo "FAIL: ran $ran of $runs runs"
    exit 2
fi

exit "$status"
