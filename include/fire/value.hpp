// SPDX-License-Identifier: MIT
//
// Runtime values.
//
// Fire is statically typed, so the VM could in principle run on untagged
// machine words. Values are tagged anyway, for three reasons that are worth
// the eight bytes: `print` and `str` need to format a value they were handed
// generically, array equality has to recurse, and a tag turns a codegen bug
// into a clean diagnostic instead of silent memory corruption.
//
// Strings are immutable and shared. Arrays have reference semantics — passing
// one to a function passes the array, not a copy — and are reference counted.
// A cycle (an array pushed into itself) will leak; Fire has no way to observe
// that other than memory use, and the alternative is a tracing collector this
// language does not need.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace fire {

class Value;

using StringRef = std::shared_ptr<const std::string>;
using ArrayRef = std::shared_ptr<std::vector<Value>>;

enum class ValueTag : std::uint8_t {
    Int,
    Float,
    Bool,
    Str,
    Array,
};

class Value {
public:
    /// Default-constructed values are `0`, which is what a fresh frame slot
    /// and an uninitialised global hold.
    Value()
        : m_data(std::int64_t { 0 })
    {
    }

    static Value integer(std::int64_t value) { return Value { value }; }
    static Value floating(double value) { return Value { value }; }
    static Value boolean(bool value) { return Value { value }; }
    static Value string(std::string value)
    {
        return Value { std::make_shared<const std::string>(std::move(value)) };
    }
    static Value string(StringRef value) { return Value { std::move(value) }; }
    static Value array(std::vector<Value> elements)
    {
        return Value { std::make_shared<std::vector<Value>>(std::move(elements)) };
    }
    static Value array(ArrayRef elements) { return Value { std::move(elements) }; }

    [[nodiscard]] ValueTag tag() const { return static_cast<ValueTag>(m_data.index()); }
    [[nodiscard]] bool is_int() const { return tag() == ValueTag::Int; }
    [[nodiscard]] bool is_float() const { return tag() == ValueTag::Float; }
    [[nodiscard]] bool is_bool() const { return tag() == ValueTag::Bool; }
    [[nodiscard]] bool is_str() const { return tag() == ValueTag::Str; }
    [[nodiscard]] bool is_array() const { return tag() == ValueTag::Array; }

    /// Accessors assume the tag has already been checked; the compiler
    /// guarantees it, and a mismatch is a compiler bug rather than a program
    /// error.
    [[nodiscard]] std::int64_t as_int() const { return std::get<std::int64_t>(m_data); }
    [[nodiscard]] double as_float() const { return std::get<double>(m_data); }
    [[nodiscard]] bool as_bool() const { return std::get<bool>(m_data); }
    [[nodiscard]] const std::string& as_str() const { return *std::get<StringRef>(m_data); }
    [[nodiscard]] const StringRef& str_ref() const { return std::get<StringRef>(m_data); }
    [[nodiscard]] std::vector<Value>& as_array() const { return *std::get<ArrayRef>(m_data); }
    [[nodiscard]] const ArrayRef& array_ref() const { return std::get<ArrayRef>(m_data); }

    /// Name of the runtime tag, for VM-level error messages.
    [[nodiscard]] const char* tag_name() const;

    /// What `print` writes: strings bare, floats with a `.0` when integral.
    [[nodiscard]] std::string to_display() const;

    /// What `str()` and nested array rendering produce: strings quoted and
    /// escaped, so `["a b"]` is unambiguous.
    [[nodiscard]] std::string to_repr() const;

    /// Structural equality. Arrays compare element-wise, so two distinct
    /// arrays with equal contents are equal.
    [[nodiscard]] bool equals(const Value& other) const;

private:
    template <typename T>
    explicit Value(T value)
        : m_data(std::move(value))
    {
    }

    // The alternative order must match ValueTag.
    std::variant<std::int64_t, double, bool, StringRef, ArrayRef> m_data;
};

/// Format a double the way Fire prints one: shortest round-tripping form, but
/// always with a fractional part so `float` and `int` never look alike.
[[nodiscard]] std::string format_float(double value);

} // namespace fire
