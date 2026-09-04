// SPDX-License-Identifier: MIT
//
// Fire's type representation.
//
// Fire is statically typed with local inference: every expression has a type
// known at compile time, but `let x = 1;` does not make you write it down.
// Types are interned by a `TypeContext`, so two types are identical exactly
// when their pointers are — no structural comparison at any use site.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fire {

enum class TypeKind : std::uint8_t {
    Int, ///< signed 64-bit integer
    Float, ///< IEEE-754 double
    Bool,
    Str, ///< immutable UTF-8 string
    Void, ///< the result of a function that returns nothing
    Array, ///< [T], a growable, reference-semantics sequence
    Error, ///< poison type; already-reported errors propagate as this
};

class Type {
public:
    Type(TypeKind kind, const Type* element)
        : m_kind(kind)
        , m_element(element)
    {
    }

    [[nodiscard]] TypeKind kind() const { return m_kind; }
    /// Element type of an array; null for every other kind.
    [[nodiscard]] const Type* element() const { return m_element; }

    [[nodiscard]] bool is(TypeKind kind) const { return m_kind == kind; }
    [[nodiscard]] bool is_numeric() const
    {
        return m_kind == TypeKind::Int || m_kind == TypeKind::Float;
    }
    [[nodiscard]] bool is_error() const { return m_kind == TypeKind::Error; }
    [[nodiscard]] bool is_void() const { return m_kind == TypeKind::Void; }
    /// Types whose values are ordered by `<`, `<=`, `>` and `>=`.
    [[nodiscard]] bool is_ordered() const { return is_numeric() || m_kind == TypeKind::Str; }

    /// Source-level spelling: `int`, `[str]`, `[[int]]`, …
    [[nodiscard]] std::string to_string() const;

private:
    TypeKind m_kind;
    const Type* m_element;
};

/// Owns every `Type` instance for one compilation and hands out interned
/// pointers. Cheap to construct; one lives inside each compiler run.
class TypeContext {
public:
    TypeContext();

    [[nodiscard]] const Type* int_type() const { return m_int; }
    [[nodiscard]] const Type* float_type() const { return m_float; }
    [[nodiscard]] const Type* bool_type() const { return m_bool; }
    [[nodiscard]] const Type* str_type() const { return m_str; }
    [[nodiscard]] const Type* void_type() const { return m_void; }
    [[nodiscard]] const Type* error_type() const { return m_error; }

    /// Interned `[element]`.
    [[nodiscard]] const Type* array_of(const Type* element);

private:
    const Type* intern(TypeKind kind, const Type* element);

    std::vector<std::unique_ptr<Type>> m_owned;
    std::unordered_map<const Type*, const Type*> m_arrays;
    const Type* m_int;
    const Type* m_float;
    const Type* m_bool;
    const Type* m_str;
    const Type* m_void;
    const Type* m_error;
};

} // namespace fire
