#!/usr/bin/env bash
#
# Times every program in bench/ on the Fire VM, and on the native backend where
# the program is inside its subset.
#
# These are microbenchmarks: they say whether a change to the interpreter loop
# helped, not how fast Fire is against any other language. Run them on an
# otherwise idle machine and compare runs, not absolute numbers.
#
# Usage: tools/bench.sh [repetitions]
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fire="${FIRE_BIN:-$root/bin/fire}"
repetitions="${1:-5}"

if [[ ! -x "$fire" ]]; then
    echo "bench: $fire not found; run 'make' first" >&2
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Best of N. The minimum is the least noisy statistic here: interference from
# other processes can only ever make a run slower.
best_of() {
    local best=""
    for _ in $(seq "$repetitions"); do
        local start elapsed
        start=$(date +%s.%N)
        "$@" > /dev/null 2>&1
        elapsed=$(echo "$(date +%s.%N) - $start" | bc)
        if [[ -z "$best" ]] || (( $(echo "$elapsed < $best" | bc -l) )); then
            best="$elapsed"
        fi
    done
    printf '%.3f' "$best"
}

printf '%-16s %10s %10s   %s\n' "program" "vm" "native" "notes"
printf -- '---------------------------------------------------------------\n'

for program in "$root"/bench/*.fire; do
    [[ -e "$program" ]] || continue
    name="$(basename "$program" .fire)"

    vm_time="$(best_of "$fire" run "$program")"

    native_time="n/a"
    note=""
    if command -v nasm > /dev/null 2>&1 \
        && "$fire" build "$program" -o "$work/$name" > /dev/null 2>&1; then
        native_time="$(best_of "$work/$name")"
    else
        note="outside the native backend's subset"
    fi

    if [[ "$native_time" == "n/a" ]]; then
        printf '%-16s %9ss %10s   %s\n' "$name" "$vm_time" "n/a" "$note"
    else
        printf '%-16s %9ss %9ss   %s\n' "$name" "$vm_time" "$native_time" "$note"
    fi
done

printf -- '---------------------------------------------------------------\n'
printf 'best of %s runs each\n' "$repetitions"
