#!/usr/bin/env bash
#
# End-to-end tests.
#
# Each case in tests/cases/ is a real Fire program that carries its own
# expectations as comments, so the test and the thing being tested cannot drift
# apart in separate files:
#
#     //exit 3               expected process status      (default 0)
#     //in <text>            a line written to stdin      (repeatable)
#     //= <text>             an expected line of stdout   (in order)
#     //~ <text>             a substring expected in stderr
#
# Cases under tests/cases/errors/ must fail to compile, and assert diagnostic
# codes rather than output:
#
#     //error E0201          this code must appear in the diagnostics
#
# Usage: tests/run_e2e.sh [name-filter]
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fire="${FIRE_BIN:-$root/bin/fire}"
filter="${1:-}"

if [[ ! -x "$fire" ]]; then
    echo "run_e2e: $fire not found; run 'make' first" >&2
    exit 1
fi

if [[ -t 1 ]]; then red=$'\033[1;31m'; green=$'\033[1;32m'; dim=$'\033[2m'; off=$'\033[0m'
else red=""; green=""; dim=""; off=""; fi

passed=0
failed=0
failures=()

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

report_failure() {
    local name="$1" reason="$2"
    failed=$((failed + 1))
    failures+=("$name")
    printf '%sFAIL%s  %s\n' "$red" "$off" "$name"
    printf '%s\n' "$reason" | sed 's/^/        /'
}

# ---------------------------------------------------------------------------
# Programs that must run
# ---------------------------------------------------------------------------
for case_file in "$root"/tests/cases/*.fire; do
    [[ -e "$case_file" ]] || continue
    name="cases/$(basename "$case_file")"
    [[ -n "$filter" && "$name" != *"$filter"* ]] && continue

    expected_exit="$(sed -n 's|^//exit  *\([0-9][0-9]*\).*|\1|p' "$case_file" | head -n1)"
    expected_exit="${expected_exit:-0}"
    sed -n 's|^//= \{0,1\}||p' "$case_file" > "$work/expected"
    sed -n 's|^//in \{0,1\}||p' "$case_file" > "$work/stdin"

    "$fire" run "$case_file" < "$work/stdin" > "$work/stdout" 2> "$work/stderr"
    status=$?

    problems=""
    if [[ "$status" != "$expected_exit" ]]; then
        problems+="exit status: got $status, expected $expected_exit"$'\n'
    fi
    if ! diff -u "$work/expected" "$work/stdout" > "$work/diff" 2>&1; then
        problems+="stdout differs (- expected, + actual):"$'\n'"$(tail -n +3 "$work/diff")"$'\n'
    fi
    while IFS= read -r needle; do
        [[ -z "$needle" ]] && continue
        if ! grep -qF -- "$needle" "$work/stderr"; then
            problems+="stderr is missing: $needle"$'\n'
        fi
    done < <(sed -n 's|^//~ \{0,1\}||p' "$case_file")

    if [[ -z "$problems" ]]; then
        passed=$((passed + 1))
    else
        report_failure "$name" "$problems"
    fi
done

# ---------------------------------------------------------------------------
# Programs that must not compile
# ---------------------------------------------------------------------------
for case_file in "$root"/tests/cases/errors/*.fire; do
    [[ -e "$case_file" ]] || continue
    name="errors/$(basename "$case_file")"
    [[ -n "$filter" && "$name" != *"$filter"* ]] && continue

    "$fire" check "$case_file" > "$work/stdout" 2> "$work/stderr"
    status=$?

    problems=""
    if [[ "$status" == "0" ]]; then
        problems+="expected compilation to fail, but it succeeded"$'\n'
    fi
    while IFS= read -r code; do
        [[ -z "$code" ]] && continue
        if ! grep -qF -- "$code" "$work/stderr"; then
            problems+="expected diagnostic $code, got:"$'\n'"$(grep -o 'error\[[A-Z0-9]*\]' "$work/stderr" | sort -u | tr '\n' ' ')"$'\n'
        fi
    done < <(sed -n 's|^//error  *||p' "$case_file")

    if [[ -z "$problems" ]]; then
        passed=$((passed + 1))
    else
        report_failure "$name" "$problems"
    fi
done

# ---------------------------------------------------------------------------
# The native backend must agree with the VM
# ---------------------------------------------------------------------------
if command -v nasm > /dev/null 2>&1 && command -v ld > /dev/null 2>&1; then
    for case_file in "$root"/tests/native/*.fire; do
        [[ -e "$case_file" ]] || continue
        name="native/$(basename "$case_file")"
        [[ -n "$filter" && "$name" != *"$filter"* ]] && continue

        "$fire" run "$case_file" > "$work/vm.out" 2>/dev/null
        vm_status=$?
        if ! "$fire" build "$case_file" -o "$work/native" > "$work/build.log" 2>&1; then
            report_failure "$name" "build failed:"$'\n'"$(cat "$work/build.log")"
            continue
        fi
        "$work/native" > "$work/native.out" 2>/dev/null
        native_status=$?

        problems=""
        if [[ "$vm_status" != "$native_status" ]]; then
            problems+="exit status: vm $vm_status, native $native_status"$'\n'
        fi
        if ! diff -u "$work/vm.out" "$work/native.out" > "$work/diff" 2>&1; then
            problems+="native output differs from the VM (- vm, + native):"$'\n'"$(tail -n +3 "$work/diff")"$'\n'
        fi
        if [[ -z "$problems" ]]; then
            passed=$((passed + 1))
        else
            report_failure "$name" "$problems"
        fi
    done
else
    printf '%sskip%s  native backend tests (nasm or ld not installed)\n' "$dim" "$off"
fi

printf '\n%d passed, %d failed\n' "$passed" "$failed"
if (( failed > 0 )); then
    printf 'failing: %s\n' "${failures[*]}"
    exit 1
fi
printf '%sall end-to-end tests passed%s\n' "$green" "$off"
