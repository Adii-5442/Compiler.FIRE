// SPDX-License-Identifier: MIT
//
// Semantic analysis tests. Each one asserts the diagnostic *code*, not its
// wording, so the messages can be improved without breaking the suite.
#include "framework.hpp"

using namespace fire;
using namespace fire::test;

FIRE_TEST(sema, mixed_numeric_arithmetic_is_rejected)
{
    const std::string diagnostics = diagnose("let x = 1 + 2.0;");
    FIRE_CHECK(contains(diagnostics, "E0201"));
    FIRE_CHECK(contains(diagnostics, "does not convert between `int` and `float`"));
}

FIRE_TEST(sema, string_concatenation_needs_two_strings)
{
    FIRE_CHECK(contains(diagnose(R"(let x = "n = " + 1;)"), "E0201"));
    FIRE_CHECK(contains(diagnose(R"(let x = "n = " + str(1);)"), ""));
    FIRE_CHECK(run_source(R"(println("n = " + str(1));)").compiled);
}

FIRE_TEST(sema, annotation_mismatch_is_reported)
{
    FIRE_CHECK(contains(diagnose("let x: int = 1.5;"), "E0203"));
    FIRE_CHECK(contains(diagnose("let x: [str] = [1];"), "E0203"));
}

FIRE_TEST(sema, redeclaration_in_one_scope_is_rejected)
{
    FIRE_CHECK(contains(diagnose("let x = 1; let x = 2;"), "E0202"));
}

FIRE_TEST(sema, shadowing_in_a_nested_scope_is_allowed)
{
    const RunResult result = run_source(R"(
        let x = 1;
        {
            let x = 2;
            println(x);
        }
        println(x);
    )");
    FIRE_CHECK_EQ(result.output, std::string { "2\n1\n" });
}

FIRE_TEST(sema, const_cannot_be_reassigned)
{
    FIRE_CHECK(contains(diagnose("const x = 1; x = 2;"), "E0209"));
    FIRE_CHECK(contains(diagnose("const x = 1; x += 2;"), "E0209"));
}

FIRE_TEST(sema, functions_are_visible_before_declaration)
{
    const RunResult result = run_source(R"(
        println(later());
        fn later() -> int { return 7; }
    )");
    FIRE_CHECK_EQ(result.output, std::string { "7\n" });
}

FIRE_TEST(sema, arity_mismatch_is_reported)
{
    FIRE_CHECK(contains(diagnose("fn f(a: int) {} f();"), "E0219"));
    FIRE_CHECK(contains(diagnose("fn f(a: int) {} f(1, 2);"), "E0219"));
    FIRE_CHECK(contains(diagnose("len();"), "E0220"));
}

FIRE_TEST(sema, missing_return_on_some_path_is_reported)
{
    FIRE_CHECK(contains(diagnose("fn f(n: int) -> int { if n > 0 { return 1; } }"), "E0206"));
}

FIRE_TEST(sema, exit_and_panic_count_as_returning)
{
    FIRE_CHECK(run_source("fn f() -> int { exit(0); }").compiled);
    FIRE_CHECK(run_source(R"(fn f() -> int { panic("no"); })").compiled);
    FIRE_CHECK(run_source("fn f() -> int { while true { } }").compiled);
}

FIRE_TEST(sema, break_outside_a_loop_is_rejected)
{
    FIRE_CHECK(contains(diagnose("break;"), "E0207"));
    FIRE_CHECK(contains(diagnose("fn f() { continue; }"), "E0207"));
}

FIRE_TEST(sema, return_outside_a_function_is_rejected)
{
    FIRE_CHECK(contains(diagnose("return 1;"), "E0212"));
}

FIRE_TEST(sema, conditions_must_be_bool)
{
    const std::string diagnostics = diagnose("let n = 1; if n { }");
    FIRE_CHECK(contains(diagnostics, "E0223"));
    FIRE_CHECK(contains(diagnostics, "x != 0"));
}

FIRE_TEST(sema, empty_array_needs_an_annotation)
{
    FIRE_CHECK(contains(diagnose("let xs = [];"), "E0215"));
    FIRE_CHECK(run_source("let xs: [int] = []; println(len(xs));").compiled);
}

FIRE_TEST(sema, heterogeneous_array_is_rejected)
{
    FIRE_CHECK(contains(diagnose(R"(let xs = [1, "two"];)"), "E0216"));
}

FIRE_TEST(sema, unknown_name_suggests_a_close_one)
{
    FIRE_CHECK(contains(diagnose("let value = 1; println(valeu);"), "did you mean `value`"));
    FIRE_CHECK(contains(diagnose("prinln(1);"), "did you mean `println`"));
}

FIRE_TEST(sema, builtins_cannot_be_redefined)
{
    FIRE_CHECK(contains(diagnose("fn len(x: int) -> int { return x; }"), "E0204"));
}

FIRE_TEST(sema, duplicate_functions_are_rejected)
{
    FIRE_CHECK(contains(diagnose("fn f() {} fn f() {}"), "E0205"));
}

FIRE_TEST(sema, unused_local_warns_but_underscore_does_not)
{
    FIRE_CHECK(contains(diagnose("fn f() { let unused = 1; }"), "W0001"));
    FIRE_CHECK(!contains(diagnose("fn f() { let _unused = 1; }"), "W0001"));
}

FIRE_TEST(sema, unreachable_code_warns)
{
    FIRE_CHECK(contains(diagnose("fn f() -> int { return 1; return 2; }"), "W0002"));
}

FIRE_TEST(sema, one_bad_subexpression_produces_one_error)
{
    const std::string diagnostics = diagnose(R"(let x = (1 + "a") * 2 + 3;)");
    std::size_t errors = 0;
    for (std::size_t at = diagnostics.find("error["); at != std::string::npos;
        at = diagnostics.find("error[", at + 1)) {
        ++errors;
    }
    FIRE_CHECK_EQ(errors, std::size_t { 1 });
}

FIRE_TEST(sema, indexing_a_non_sequence_is_rejected)
{
    FIRE_CHECK(contains(diagnose("let n = 1; println(n[0]);"), "E0222"));
}

FIRE_TEST(sema, iterating_a_non_sequence_is_rejected)
{
    FIRE_CHECK(contains(diagnose("for x in 1 { }"), "E0211"));
}

FIRE_TEST(sema, void_calls_cannot_be_bound_or_printed)
{
    FIRE_CHECK(contains(diagnose("fn f() {} let x = f();"), "E0208"));
    FIRE_CHECK(contains(diagnose("fn f() {} println(f());"), "E0301"));
}
