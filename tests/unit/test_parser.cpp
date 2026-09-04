// SPDX-License-Identifier: MIT
//
// Parser tests. Precedence is checked by evaluating expressions rather than by
// inspecting the tree: a wrong tree that still evaluates correctly is not a
// bug worth failing a build over, and a right tree that evaluates wrongly is.
#include "framework.hpp"

using namespace fire;
using namespace fire::test;

namespace {

std::string evaluate(const std::string& expression)
{
    const RunResult result = run_source("println(" + expression + ");");
    if (!result.compiled) {
        return "<compile error>\n" + result.diagnostics;
    }
    return result.output;
}

} // namespace

FIRE_TEST(parser, arithmetic_precedence)
{
    FIRE_CHECK_EQ(evaluate("1 + 2 * 3"), std::string { "7\n" });
    FIRE_CHECK_EQ(evaluate("(1 + 2) * 3"), std::string { "9\n" });
    FIRE_CHECK_EQ(evaluate("2 * 3 % 4"), std::string { "2\n" });
    FIRE_CHECK_EQ(evaluate("10 - 4 - 3"), std::string { "3\n" }); // left associative
}

FIRE_TEST(parser, unary_binds_tighter_than_binary)
{
    FIRE_CHECK_EQ(evaluate("-2 + 3"), std::string { "1\n" });
    FIRE_CHECK_EQ(evaluate("-(2 + 3)"), std::string { "-5\n" });
    FIRE_CHECK_EQ(evaluate("- -5"), std::string { "5\n" });
    FIRE_CHECK_EQ(evaluate("!(1 == 2)"), std::string { "true\n" });
    FIRE_CHECK_EQ(evaluate("~0"), std::string { "-1\n" });
}

FIRE_TEST(parser, comparison_binds_looser_than_shift_and_arithmetic)
{
    FIRE_CHECK_EQ(evaluate("1 + 1 == 2"), std::string { "true\n" });
    FIRE_CHECK_EQ(evaluate("1 << 3 > 4"), std::string { "true\n" });
}

FIRE_TEST(parser, bitwise_precedence_runs_or_xor_and)
{
    // 1 | 2 ^ 3 & 3  ==  1 | (2 ^ (3 & 3))  ==  1 | 1  ==  1
    FIRE_CHECK_EQ(evaluate("1 | 2 ^ 3 & 3"), std::string { "1\n" });
}

FIRE_TEST(parser, logical_precedence_and_before_or)
{
    FIRE_CHECK_EQ(evaluate("true || false && false"), std::string { "true\n" });
    FIRE_CHECK_EQ(evaluate("(true || false) && false"), std::string { "false\n" });
}

FIRE_TEST(parser, indexing_chains)
{
    const RunResult result = run_source(R"(
        let grid: [[int]] = [[1, 2], [3, 4]];
        println(grid[1][0]);
    )");
    FIRE_CHECK_EQ(result.output, std::string { "3\n" });
}

FIRE_TEST(parser, elif_chains_desugar_to_nested_ifs)
{
    const RunResult result = run_source(R"(
        fn label(n: int) -> str {
            if n < 0 { return "negative"; }
            elif n == 0 { return "zero"; }
            elif n < 10 { return "small"; }
            else { return "large"; }
        }
        println(label(-1), label(0), label(5), label(50));
    )");
    FIRE_CHECK_EQ(result.output, std::string { "negative zero small large\n" });
}

FIRE_TEST(parser, trailing_commas_are_accepted)
{
    const RunResult result = run_source(R"(
        fn add(a: int, b: int) -> int { return a + b; }
        println([1, 2, 3,], add(1, 2,));
    )");
    FIRE_CHECK_EQ(result.output, std::string { "[1, 2, 3] 3\n" });
}

FIRE_TEST(parser, missing_semicolon_reports_once)
{
    const std::string diagnostics = diagnose("let x = 1\nlet y = 2;\nlet z = 3;\n");
    std::size_t errors = 0;
    for (std::size_t at = diagnostics.find("error["); at != std::string::npos;
        at = diagnostics.find("error[", at + 1)) {
        ++errors;
    }
    FIRE_CHECK_EQ(errors, std::size_t { 1 });
}

FIRE_TEST(parser, type_keyword_in_value_position_is_explained)
{
    FIRE_CHECK(contains(diagnose("let x = int;"), "is a type, not a value"));
}

FIRE_TEST(parser, conversions_named_after_types_still_parse)
{
    const RunResult result = run_source(R"(println(int("7"), float(2), str(3), bool(0));)");
    FIRE_CHECK_EQ(result.output, std::string { "7 2.0 3 false\n" });
}

FIRE_TEST(parser, invalid_assignment_target_is_rejected)
{
    FIRE_CHECK(contains(diagnose("1 + 1 = 2;"), "invalid assignment target"));
}

FIRE_TEST(parser, nested_functions_are_rejected)
{
    FIRE_CHECK(contains(diagnose("fn outer() { fn inner() {} }"), "E0103"));
}
