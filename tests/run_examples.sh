#!/usr/bin/env bash
#
# Runs every program in examples/ and fails if any of them errors.
#
# Several examples assert their own results, so this is a real test of the
# compiler and not only a check that nothing crashes. stdin is /dev/null, which
# the interactive examples treat as "no input" and exit cleanly on.
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fire="${FIRE_BIN:-$root/bin/fire}"

if [[ ! -x "$fire" ]]; then
    echo "run_examples: $fire not found; run 'make' first" >&2
    exit 1
fi

failed=0
for example in "$root"/examples/*.fire; do
    name="$(basename "$example")"
    output="$("$fire" run "$example" < /dev/null 2>&1)"
    status=$?
    if [[ "$status" != "0" ]]; then
        printf 'FAIL  %s (exit %d)\n' "$name" "$status"
        printf '%s\n' "$output" | tail -n 20 | sed 's/^/        /'
        failed=$((failed + 1))
        continue
    fi
    if grep -q '^warning\|^error' <<< "$output"; then
        printf 'FAIL  %s produced compiler diagnostics\n' "$name"
        grep '^warning\|^error' <<< "$output" | sed 's/^/        /'
        failed=$((failed + 1))
        continue
    fi
    printf 'ok    %-22s %s lines of output\n' "$name" "$(wc -l <<< "$output")"
done

if (( failed > 0 )); then
    printf '\n%d example(s) failed\n' "$failed"
    exit 1
fi
printf '\nall examples ran cleanly\n'
