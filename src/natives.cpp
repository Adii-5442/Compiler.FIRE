// SPDX-License-Identifier: MIT
#include "fire/natives.hpp"

#include <cassert>
#include <string>

namespace fire {
namespace {

    const Type* error_of(const NativeCallCheck& check) { return check.types.error_type(); }

    /// True when the type is already poisoned, in which case a checker should
    /// stay quiet: the real error has been reported somewhere upstream.
    bool poisoned(const Type* type) { return type == nullptr || type->is_error(); }

    // -- individual signature checkers --------------------------------------

    const Type* check_print(const NativeCallCheck& check)
    {
        for (std::size_t i = 0; i < check.arguments.size(); ++i) {
            if (!poisoned(check.arguments[i]) && check.arguments[i]->is_void()) {
                check.diagnostics
                    .error("E0301", "cannot print a `void` value", check.argument_spans[i])
                    .label("this call returns nothing");
            }
        }
        return check.types.void_type();
    }

    const Type* check_len(const NativeCallCheck& check)
    {
        const Type* argument = check.arguments[0];
        if (poisoned(argument)) {
            return check.types.int_type();
        }
        if (argument->is(TypeKind::Str) || argument->is(TypeKind::Array)) {
            return check.types.int_type();
        }
        return check.mismatch(0, "`str` or an array");
    }

    const Type* check_push(const NativeCallCheck& check)
    {
        const Type* array = check.arguments[0];
        if (poisoned(array)) {
            return check.types.void_type();
        }
        if (!array->is(TypeKind::Array)) {
            return check.mismatch(0, "an array");
        }
        const Type* element = check.arguments[1];
        if (!poisoned(element) && element != array->element()) {
            check.diagnostics
                .error("E0302",
                    "cannot push `" + element->to_string() + "` onto `" + array->to_string() + "`",
                    check.argument_spans[1])
                .label("expected `" + array->element()->to_string() + "`");
        }
        return check.types.void_type();
    }

    const Type* check_pop(const NativeCallCheck& check)
    {
        const Type* array = check.arguments[0];
        if (poisoned(array)) {
            return error_of(check);
        }
        if (!array->is(TypeKind::Array)) {
            return check.mismatch(0, "an array");
        }
        return array->element();
    }

    const Type* check_insert(const NativeCallCheck& check)
    {
        const Type* array = check.arguments[0];
        if (poisoned(array)) {
            return check.types.void_type();
        }
        if (!array->is(TypeKind::Array)) {
            return check.mismatch(0, "an array");
        }
        if (!poisoned(check.arguments[1]) && !check.arguments[1]->is(TypeKind::Int)) {
            check.mismatch(1, "`int`");
        }
        if (!poisoned(check.arguments[2]) && check.arguments[2] != array->element()) {
            check.mismatch(2, "`" + array->element()->to_string() + "`");
        }
        return check.types.void_type();
    }

    const Type* check_remove(const NativeCallCheck& check)
    {
        const Type* array = check.arguments[0];
        if (poisoned(array)) {
            return error_of(check);
        }
        if (!array->is(TypeKind::Array)) {
            return check.mismatch(0, "an array");
        }
        if (!poisoned(check.arguments[1]) && !check.arguments[1]->is(TypeKind::Int)) {
            check.mismatch(1, "`int`");
        }
        return array->element();
    }

    const Type* check_slice(const NativeCallCheck& check)
    {
        const Type* subject = check.arguments[0];
        for (std::size_t i = 1; i < 3; ++i) {
            if (!poisoned(check.arguments[i]) && !check.arguments[i]->is(TypeKind::Int)) {
                check.mismatch(i, "`int`");
            }
        }
        if (poisoned(subject)) {
            return error_of(check);
        }
        if (subject->is(TypeKind::Str) || subject->is(TypeKind::Array)) {
            return subject;
        }
        return check.mismatch(0, "`str` or an array");
    }

    /// Shared shape for the one-argument conversions.
    template <TypeKind kResult> const Type* check_conversion(const NativeCallCheck& check)
    {
        const Type* result = kResult == TypeKind::Int ? check.types.int_type()
            : kResult == TypeKind::Float              ? check.types.float_type()
            : kResult == TypeKind::Bool               ? check.types.bool_type()
                                                      : check.types.str_type();
        const Type* argument = check.arguments[0];
        if (poisoned(argument)) {
            return result;
        }
        if (argument->is_void() || argument->is(TypeKind::Array)) {
            if (kResult == TypeKind::Str && argument->is(TypeKind::Array)) {
                return result; // str([1,2]) renders the array
            }
            check.mismatch(0, "a scalar value");
            return result;
        }
        if (kResult == TypeKind::Float && argument->is(TypeKind::Bool)) {
            check.mismatch(0, "`int`, `float` or `str`");
        }
        return result;
    }

    const Type* check_numeric_same(const NativeCallCheck& check)
    {
        const Type* argument = check.arguments[0];
        if (poisoned(argument)) {
            return error_of(check);
        }
        if (!argument->is_numeric()) {
            return check.mismatch(0, "`int` or `float`");
        }
        return argument;
    }

    const Type* check_numeric_pair(const NativeCallCheck& check)
    {
        const Type* left = check.arguments[0];
        const Type* right = check.arguments[1];
        if (poisoned(left) || poisoned(right)) {
            return error_of(check);
        }
        if (!left->is_numeric()) {
            return check.mismatch(0, "`int` or `float`");
        }
        if (!right->is_numeric()) {
            return check.mismatch(1, "`int` or `float`");
        }
        if (left != right) {
            check.diagnostics
                .error("E0303",
                    std::string { "`" } + std::string { check.name }
                        + "` needs both arguments to have the same type",
                    check.call_span)
                .label("found `" + left->to_string() + "` and `" + right->to_string() + "`")
                .help("convert one side with `int(x)` or `float(x)`");
            return error_of(check);
        }
        return left;
    }

    /// pow / sqrt accept any numeric and always produce a float.
    const Type* check_float_math(const NativeCallCheck& check)
    {
        for (std::size_t i = 0; i < check.arguments.size(); ++i) {
            if (!poisoned(check.arguments[i]) && !check.arguments[i]->is_numeric()) {
                check.mismatch(i, "`int` or `float`");
            }
        }
        return check.types.float_type();
    }

    /// floor / ceil / round: numeric in, int out.
    const Type* check_rounding(const NativeCallCheck& check)
    {
        if (!poisoned(check.arguments[0]) && !check.arguments[0]->is_numeric()) {
            check.mismatch(0, "`int` or `float`");
        }
        return check.types.int_type();
    }

    /// Every argument must be `str`; the result type is supplied by kResult.
    template <TypeKind kResult> const Type* check_all_str(const NativeCallCheck& check)
    {
        for (std::size_t i = 0; i < check.arguments.size(); ++i) {
            if (!poisoned(check.arguments[i]) && !check.arguments[i]->is(TypeKind::Str)) {
                check.mismatch(i, "`str`");
            }
        }
        if (kResult == TypeKind::Int) {
            return check.types.int_type();
        }
        if (kResult == TypeKind::Array) {
            return check.types.array_of(check.types.str_type());
        }
        return check.types.str_type();
    }

    const Type* check_chr(const NativeCallCheck& check)
    {
        if (!poisoned(check.arguments[0]) && !check.arguments[0]->is(TypeKind::Int)) {
            check.mismatch(0, "`int`");
        }
        return check.types.str_type();
    }

    const Type* check_join(const NativeCallCheck& check)
    {
        const Type* array = check.arguments[0];
        if (!poisoned(array)
            && !(array->is(TypeKind::Array) && array->element()->is(TypeKind::Str))) {
            check.mismatch(0, "`[str]`");
        }
        if (!poisoned(check.arguments[1]) && !check.arguments[1]->is(TypeKind::Str)) {
            check.mismatch(1, "`str`");
        }
        return check.types.str_type();
    }

    const Type* check_repeat(const NativeCallCheck& check)
    {
        if (!poisoned(check.arguments[0]) && !check.arguments[0]->is(TypeKind::Str)) {
            check.mismatch(0, "`str`");
        }
        if (!poisoned(check.arguments[1]) && !check.arguments[1]->is(TypeKind::Int)) {
            check.mismatch(1, "`int`");
        }
        return check.types.str_type();
    }

    const Type* check_nullary_str(const NativeCallCheck& check) { return check.types.str_type(); }

    const Type* check_nullary_float(const NativeCallCheck& check)
    {
        return check.types.float_type();
    }

    const Type* check_args(const NativeCallCheck& check)
    {
        return check.types.array_of(check.types.str_type());
    }

    const Type* check_exit(const NativeCallCheck& check)
    {
        if (!poisoned(check.arguments[0]) && !check.arguments[0]->is(TypeKind::Int)) {
            check.mismatch(0, "`int`");
        }
        return check.types.void_type();
    }

    const Type* check_assert(const NativeCallCheck& check)
    {
        if (!poisoned(check.arguments[0]) && !check.arguments[0]->is(TypeKind::Bool)) {
            check.mismatch(0, "`bool`");
        }
        if (check.arguments.size() == 2 && !poisoned(check.arguments[1])
            && !check.arguments[1]->is(TypeKind::Str)) {
            check.mismatch(1, "`str`");
        }
        return check.types.void_type();
    }

    const Type* check_panic(const NativeCallCheck& check)
    {
        if (!poisoned(check.arguments[0]) && !check.arguments[0]->is(TypeKind::Str)) {
            check.mismatch(0, "`str`");
        }
        return check.types.void_type();
    }

    const Type* check_seed(const NativeCallCheck& check)
    {
        if (!poisoned(check.arguments[0]) && !check.arguments[0]->is(TypeKind::Int)) {
            check.mismatch(0, "`int`");
        }
        return check.types.void_type();
    }

    const Type* check_rand_int(const NativeCallCheck& check)
    {
        for (std::size_t i = 0; i < check.arguments.size(); ++i) {
            if (!poisoned(check.arguments[i]) && !check.arguments[i]->is(TypeKind::Int)) {
                check.mismatch(i, "`int`");
            }
        }
        return check.types.int_type();
    }

    constexpr std::uint16_t kVar = NativeInfo::kVariadic;

    const std::vector<NativeInfo>& build_table()
    {
        static const std::vector<NativeInfo> table = {
            { "print", 0, kVar, check_print, false, "write values to stdout" },
            { "println", 0, kVar, check_print, false, "write values to stdout with a newline" },
            { "len", 1, 1, check_len, false, "length of a string or array" },
            { "push", 2, 2, check_push, false, "append an element to an array" },
            { "pop", 1, 1, check_pop, false, "remove and return the last element" },
            { "insert", 3, 3, check_insert, false, "insert an element at an index" },
            { "remove", 2, 2, check_remove, false, "remove and return the element at an index" },
            { "slice", 3, 3, check_slice, false, "sub-range of a string or array" },
            { "int", 1, 1, check_conversion<TypeKind::Int>, false, "convert to int" },
            { "float", 1, 1, check_conversion<TypeKind::Float>, false, "convert to float" },
            { "bool", 1, 1, check_conversion<TypeKind::Bool>, false, "convert to bool" },
            { "str", 1, 1, check_conversion<TypeKind::Str>, false, "convert to str" },
            { "abs", 1, 1, check_numeric_same, false, "absolute value" },
            { "min", 2, 2, check_numeric_pair, false, "smaller of two numbers" },
            { "max", 2, 2, check_numeric_pair, false, "larger of two numbers" },
            { "pow", 2, 2, check_float_math, false, "raise to a power" },
            { "sqrt", 1, 1, check_float_math, false, "square root" },
            { "floor", 1, 1, check_rounding, false, "round down to an int" },
            { "ceil", 1, 1, check_rounding, false, "round up to an int" },
            { "round", 1, 1, check_rounding, false, "round to the nearest int" },
            { "chr", 1, 1, check_chr, false, "one-character string from a code point" },
            { "ord", 1, 1, check_all_str<TypeKind::Int>, false, "code point of the first character" },
            { "find", 2, 2, check_all_str<TypeKind::Int>, false, "index of a substring, or -1" },
            { "split", 2, 2, check_all_str<TypeKind::Array>, false, "split a string on a separator" },
            { "join", 2, 2, check_join, false, "join `[str]` with a separator" },
            { "trim", 1, 1, check_all_str<TypeKind::Str>, false, "strip leading/trailing space" },
            { "upper", 1, 1, check_all_str<TypeKind::Str>, false, "uppercase a string" },
            { "lower", 1, 1, check_all_str<TypeKind::Str>, false, "lowercase a string" },
            { "replace", 3, 3, check_all_str<TypeKind::Str>, false, "replace every occurrence" },
            { "repeat", 2, 2, check_repeat, false, "concatenate a string with itself n times" },
            { "input", 0, 0, check_nullary_str, false, "read one line from stdin" },
            { "exit", 1, 1, check_exit, true, "stop the program with a status code" },
            { "assert", 1, 2, check_assert, false, "abort unless a condition holds" },
            { "panic", 1, 1, check_panic, true, "abort with a message" },
            { "clock", 0, 0, check_nullary_float, false, "seconds of CPU time used" },
            { "args", 0, 0, check_args, false, "command-line arguments" },
            { "seed", 1, 1, check_seed, false, "seed the random number generator" },
            { "rand_int", 2, 2, check_rand_int, false, "uniform integer in [lo, hi]" },
        };
        // Keeps the enum and the table honest: adding a NativeId without a
        // row here (or the reverse) fails on the first call instead of
        // dispatching to the wrong builtin at run time.
        assert(table.size() == static_cast<std::size_t>(NativeId::Count)
            && "native table is out of sync with NativeId");
        return table;
    }

} // namespace

const Type* NativeCallCheck::mismatch(std::size_t index, std::string_view expected) const
{
    const Span span = index < argument_spans.size() ? argument_spans[index] : call_span;
    const Type* actual = index < arguments.size() ? arguments[index] : nullptr;
    diagnostics
        .error("E0304",
            "argument " + std::to_string(index + 1) + " of `" + std::string { name }
                + "` has the wrong type",
            span)
        .label("expected " + std::string { expected } + ", found `"
            + (actual != nullptr ? actual->to_string() : std::string { "?" }) + '`');
    return types.error_type();
}

const std::vector<NativeInfo>& native_table() { return build_table(); }

const NativeInfo& native_info(NativeId id)
{
    return native_table()[static_cast<std::size_t>(id)];
}

std::optional<NativeId> find_native(std::string_view name)
{
    const std::vector<NativeInfo>& table = native_table();
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i].name == name) {
            return static_cast<NativeId>(i);
        }
    }
    return std::nullopt;
}

} // namespace fire
