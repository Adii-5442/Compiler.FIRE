// SPDX-License-Identifier: MIT
//
// End-to-end tests through the whole pipeline: source in, program output out.
#include "framework.hpp"

using namespace fire;
using namespace fire::test;

FIRE_TEST(vm, integer_arithmetic_wraps_rather_than_trapping)
{
    const RunResult result = run_source(R"(
        let big = 9223372036854775807;
        println(big + 1);
        println(-9223372036854775807 - 1);
    )");
    FIRE_CHECK_EQ(result.output, std::string { "-9223372036854775808\n-9223372036854775808\n" });
}

FIRE_TEST(vm, integer_division_truncates_toward_zero)
{
    const RunResult result = run_source("println(7 / 2, -7 / 2, 7 % 3, -7 % 3, 7 % -3);");
    FIRE_CHECK_EQ(result.output, std::string { "3 -3 1 -1 1\n" });
}

FIRE_TEST(vm, division_by_zero_is_a_runtime_error)
{
    const RunResult result = run_source("let d = 0; println(1 / d);");
    FIRE_CHECK(result.compiled);
    FIRE_CHECK_EQ(result.status, 70);
    FIRE_CHECK(contains(result.diagnostics, "division by zero"));
}

FIRE_TEST(vm, index_out_of_bounds_is_a_runtime_error)
{
    const RunResult result = run_source("let xs = [1, 2]; println(xs[5]);");
    FIRE_CHECK_EQ(result.status, 70);
    FIRE_CHECK(contains(result.diagnostics, "out of bounds"));
}

FIRE_TEST(vm, runtime_errors_carry_a_backtrace)
{
    const RunResult result = run_source(R"(
        fn inner(n: int) -> int { return 1 / n; }
        fn outer() -> int { return inner(0); }
        println(outer());
    )");
    FIRE_CHECK(contains(result.diagnostics, "stack backtrace"));
    FIRE_CHECK(contains(result.diagnostics, "inner"));
    FIRE_CHECK(contains(result.diagnostics, "outer"));
}

FIRE_TEST(vm, recursion_depth_is_bounded)
{
    const RunResult result = run_source(R"(
        fn forever(n: int) -> int { return forever(n + 1); }
        println(forever(0));
    )");
    FIRE_CHECK_EQ(result.status, 70);
    FIRE_CHECK(contains(result.diagnostics, "call stack overflow"));
}

FIRE_TEST(vm, exit_sets_the_process_status)
{
    const RunResult result = run_source(R"(println("before"); exit(12); println("after");)");
    FIRE_CHECK_EQ(result.status, 12);
    FIRE_CHECK_EQ(result.output, std::string { "before\n" });
}

FIRE_TEST(vm, short_circuit_skips_the_right_operand)
{
    const RunResult result = run_source(R"(
        fn boom() -> bool { panic("evaluated"); return true; }
        println(false && boom());
        println(true || boom());
    )");
    FIRE_CHECK_EQ(result.status, 0);
    FIRE_CHECK_EQ(result.output, std::string { "false\ntrue\n" });
}

FIRE_TEST(vm, arrays_have_reference_semantics)
{
    const RunResult result = run_source(R"(
        fn add(xs: [int]) { push(xs, 3); }
        let a = [1, 2];
        let b = a;
        add(a);
        println(a, b, a == b);
    )");
    FIRE_CHECK_EQ(result.output, std::string { "[1, 2, 3] [1, 2, 3] true\n" });
}

FIRE_TEST(vm, array_equality_is_structural)
{
    const RunResult result = run_source(R"(
        println([1, 2] == [1, 2], [1, 2] == [2, 1], [[1]] == [[1]]);
    )");
    FIRE_CHECK_EQ(result.output, std::string { "true false true\n" });
}

FIRE_TEST(vm, loop_variable_slots_are_reused_across_sibling_scopes)
{
    // Two sequential blocks each declaring three locals must not need six
    // slots; if the high-water mark were wrong this would read stale values.
    const RunResult result = run_source(R"(
        fn f() -> int {
            let total = 0;
            { let a = 1; let b = 2; let c = 3; total += a + b + c; }
            { let d = 10; let e = 20; let f2 = 30; total += d + e + f2; }
            return total;
        }
        println(f());
    )");
    FIRE_CHECK_EQ(result.output, std::string { "66\n" });
}

FIRE_TEST(vm, continue_in_a_for_loop_runs_the_increment)
{
    const RunResult result = run_source(R"(
        let seen = 0;
        for i in 0..5 {
            if i % 2 == 0 { continue; }
            seen += i;
        }
        println(seen);
    )");
    FIRE_CHECK_EQ(result.output, std::string { "4\n" }); // 1 + 3
}

FIRE_TEST(vm, break_leaves_the_innermost_loop_only)
{
    const RunResult result = run_source(R"(
        let hits = 0;
        for i in 0..3 {
            for j in 0..3 {
                if j == 1 { break; }
                hits += 1;
            }
        }
        println(hits);
    )");
    FIRE_CHECK_EQ(result.output, std::string { "3\n" });
}

FIRE_TEST(vm, for_range_evaluates_its_limit_once)
{
    const RunResult result = run_source(R"(
        let calls = 0;
        fn limit(counter: [int]) -> int { counter[0] += 1; return 3; }
        let counter = [0];
        for _i in 0..limit(counter) { }
        println(counter[0]);
    )");
    FIRE_CHECK_EQ(result.output, std::string { "1\n" });
    FIRE_CHECK(contains(result.diagnostics, "W0001")); // `calls` is unused
}

FIRE_TEST(vm, compound_index_assignment_evaluates_the_index_once)
{
    const RunResult result = run_source(R"(
        let calls = [0];
        fn which(counter: [int]) -> int { counter[0] += 1; return 1; }
        let xs = [10, 20, 30];
        xs[which(calls)] += 5;
        println(xs, calls[0]);
    )");
    FIRE_CHECK_EQ(result.output, std::string { "[10, 25, 30] 1\n" });
}

FIRE_TEST(vm, mutual_recursion_works)
{
    const RunResult result = run_source(R"(
        fn even(n: int) -> bool { if n == 0 { return true; } return odd(n - 1); }
        fn odd(n: int) -> bool { if n == 0 { return false; } return even(n - 1); }
        println(even(10), odd(10), even(7), odd(7));
    )");
    FIRE_CHECK_EQ(result.output, std::string { "true false false true\n" });
}

FIRE_TEST(vm, globals_are_visible_inside_functions)
{
    const RunResult result = run_source(R"(
        let counter = 0;
        fn bump() { counter += 1; }
        bump(); bump(); bump();
        println(counter);
    )");
    FIRE_CHECK_EQ(result.output, std::string { "3\n" });
}

FIRE_TEST(vm, iterating_a_string_yields_characters)
{
    const RunResult result = run_source(R"(
        let out: [str] = [];
        for c in "abc" { push(out, c); }
        println(join(out, "-"), "abc"[1]);
    )");
    FIRE_CHECK_EQ(result.output, std::string { "a-b-c b\n" });
}

FIRE_TEST(vm, stdin_is_readable)
{
    const RunResult result = run_source(R"(
        while !eof() {
            println("> " + input());
        }
    )",
        "one\ntwo\n");
    FIRE_CHECK_EQ(result.output, std::string { "> one\n> two\n" });
}

FIRE_TEST(vm, assert_failure_stops_the_program)
{
    const RunResult result = run_source(R"(assert(1 == 2, "one is not two");)");
    FIRE_CHECK_EQ(result.status, 70);
    FIRE_CHECK(contains(result.diagnostics, "one is not two"));
}

FIRE_TEST(vm, seeded_random_is_reproducible)
{
    const std::string program = R"(
        seed(42);
        let out: [int] = [];
        for _i in 0..5 { push(out, rand_int(1, 6)); }
        println(out);
    )";
    FIRE_CHECK_EQ(run_source(program).output, run_source(program).output);
}
