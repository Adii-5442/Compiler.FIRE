// SPDX-License-Identifier: MIT
//
// Value formatting, the constant pool, span mapping and the builtin library.
#include "framework.hpp"

#include "fire/chunk.hpp"
#include "fire/source.hpp"

using namespace fire;
using namespace fire::test;

FIRE_TEST(value, floats_never_print_like_ints)
{
    FIRE_CHECK_EQ(format_float(1.0), std::string { "1.0" });
    FIRE_CHECK_EQ(format_float(-0.5), std::string { "-0.5" });
    FIRE_CHECK_EQ(format_float(100.0), std::string { "100.0" });
}

FIRE_TEST(value, floats_round_trip_through_their_shortest_form)
{
    const double awkward = 0.1 + 0.2;
    FIRE_CHECK_EQ(std::stod(format_float(awkward)), awkward);
    FIRE_CHECK_EQ(std::stod(format_float(1.0 / 3.0)), 1.0 / 3.0);
    FIRE_CHECK_EQ(format_float(1e300).find("1e+300"), std::size_t { 0 });
}

FIRE_TEST(value, strings_are_quoted_inside_arrays_only)
{
    const Value text = Value::string(std::string { "a b" });
    FIRE_CHECK_EQ(text.to_display(), std::string { "a b" });
    FIRE_CHECK_EQ(text.to_repr(), std::string { "\"a b\"" });
    FIRE_CHECK_EQ(
        Value::array(std::vector<Value> { text }).to_display(), std::string { "[\"a b\"]" });
}

FIRE_TEST(value, equality_is_structural_and_type_aware)
{
    FIRE_CHECK(Value::integer(1).equals(Value::integer(1)));
    FIRE_CHECK(!Value::integer(1).equals(Value::floating(1.0)));
    FIRE_CHECK(!Value::integer(1).equals(Value::boolean(true)));

    const Value left = Value::array(std::vector<Value> { Value::integer(1), Value::integer(2) });
    const Value right = Value::array(std::vector<Value> { Value::integer(1), Value::integer(2) });
    FIRE_CHECK(left.equals(right));
}

FIRE_TEST(chunk, constants_are_interned)
{
    Chunk chunk;
    const std::uint16_t first = chunk.add_constant(Value::integer(7));
    const std::uint16_t again = chunk.add_constant(Value::integer(7));
    const std::uint16_t other = chunk.add_constant(Value::integer(8));
    FIRE_CHECK_EQ(first, again);
    FIRE_CHECK(first != other);
    FIRE_CHECK_EQ(chunk.constants().size(), std::size_t { 2 });
}

FIRE_TEST(chunk, operands_round_trip)
{
    Chunk chunk;
    chunk.write_op(OpCode::Jump, Span { 0, 1 });
    chunk.write_u32(0xDEADBEEF, Span { 0, 1 });
    chunk.write_op(OpCode::GetLocal, Span { 0, 1 });
    chunk.write_u16(0xBEEF, Span { 0, 1 });
    FIRE_CHECK_EQ(chunk.read_u32(1), std::uint32_t { 0xDEADBEEF });
    FIRE_CHECK_EQ(chunk.read_u16(6), std::uint16_t { 0xBEEF });
}

FIRE_TEST(chunk, jump_targets_can_be_patched)
{
    Chunk chunk;
    chunk.write_op(OpCode::Jump, Span { });
    chunk.write_u32(0xFFFFFFFF, Span { });
    chunk.patch_u32(1, 42);
    FIRE_CHECK_EQ(chunk.read_u32(1), std::uint32_t { 42 });
}

FIRE_TEST(chunk, spans_map_back_to_the_instruction)
{
    Chunk chunk;
    chunk.write_op(OpCode::Pop, Span { 10, 12 });
    chunk.write_op(OpCode::Pop, Span { 40, 44 });
    FIRE_CHECK_EQ(chunk.span_at(0).begin, std::uint32_t { 10 });
    FIRE_CHECK_EQ(chunk.span_at(1).begin, std::uint32_t { 40 });
}

FIRE_TEST(source, line_and_column_are_one_based)
{
    const SourceFile source = SourceFile::from_string("<t>", "abc\ndefg\n\nhi");
    FIRE_CHECK_EQ(source.locate(0).line, std::uint32_t { 1 });
    FIRE_CHECK_EQ(source.locate(0).column, std::uint32_t { 1 });
    FIRE_CHECK_EQ(source.locate(4).line, std::uint32_t { 2 });
    FIRE_CHECK_EQ(source.locate(4).column, std::uint32_t { 1 });
    FIRE_CHECK_EQ(source.locate(6).column, std::uint32_t { 3 });
    FIRE_CHECK_EQ(source.locate(11).line, std::uint32_t { 4 });
    FIRE_CHECK_EQ(std::string { source.line_text(2) }, std::string { "defg" });
}

FIRE_TEST(source, out_of_range_offsets_clamp)
{
    const SourceFile source = SourceFile::from_string("<t>", "ab");
    FIRE_CHECK_EQ(source.locate(9999).line, std::uint32_t { 1 });
    FIRE_CHECK_EQ(source.locate(9999).column, std::uint32_t { 3 });
}

FIRE_TEST(builtins, conversions)
{
    const RunResult result = run_source(R"(
        println(int("42"), int(3.9), int(-3.9), int(true));
        println(float("2.5"), float(3));
        println(str(1), str(1.5), str(true), str([1, 2]));
        println(bool(0), bool(1), bool(""), bool("x"));
    )");
    FIRE_CHECK_EQ(result.output,
        std::string { "42 3 -3 1\n2.5 3.0\n1 1.5 true [1, 2]\nfalse true false true\n" });
}

FIRE_TEST(builtins, bad_conversions_fail_loudly)
{
    FIRE_CHECK(contains(run_source(R"(println(int("nope"));)").diagnostics, "cannot convert"));
    FIRE_CHECK(contains(run_source("println(sqrt(-1.0));").diagnostics, "sqrt of a negative"));
    FIRE_CHECK(contains(run_source("let xs: [int] = []; println(pop(xs));").diagnostics,
        "pop from an empty array"));
}

FIRE_TEST(builtins, array_operations)
{
    const RunResult result = run_source(R"(
        let xs = [1, 2, 3];
        insert(xs, 0, 0);
        push(xs, 4);
        println(xs, len(xs));
        println(remove(xs, 2), pop(xs), xs);
        println(slice([1,2,3,4,5], 1, 4), slice([1,2,3], 0, 99));
    )");
    FIRE_CHECK_EQ(
        result.output, std::string { "[0, 1, 2, 3, 4] 5\n2 4 [0, 1, 3]\n[2, 3, 4] [1, 2, 3]\n" });
}

FIRE_TEST(builtins, string_operations)
{
    const RunResult result = run_source(R"(
        println(upper("aB"), lower("aB"), trim("  x "), repeat("ab", 3));
        println(find("haystack", "stack"), find("haystack", "zz"));
        println(split("a,b,,c", ","), split("abc", ""));
        println(join(["x", "y"], "+"), replace("aaa", "a", "b"));
        println(slice("abcdef", 1, 4), len("abcdef"));
    )");
    FIRE_CHECK_EQ(result.output,
        std::string { "AB ab x ababab\n"
                      "3 -1\n"
                      R"(["a", "b", "", "c"] ["a", "b", "c"])"
                      "\n"
                      "x+y bbb\n"
                      "bcd 6\n" });
}

FIRE_TEST(builtins, chr_and_ord_are_inverses_across_planes)
{
    const RunResult result = run_source(R"(
        for code in [65, 233, 8364, 128293] {
            print(ord(chr(code)) == code, "");
        }
        println("");
    )");
    FIRE_CHECK_EQ(result.output, std::string { "true true true true \n" });
}

FIRE_TEST(builtins, maths)
{
    const RunResult result = run_source(R"(
        println(abs(-3), abs(-3.5), min(1, 2), max(1.5, 2.5));
        println(pow(2.0, 10.0), sqrt(9.0));
        println(floor(2.7), ceil(2.1), round(2.5), floor(-2.1));
    )");
    FIRE_CHECK_EQ(result.output, std::string { "3 3.5 1 2.5\n1024.0 3.0\n2 3 3 -3\n" });
}
