// SPDX-License-Identifier: MIT
//
// Fire's builtin function library.
//
// Natives are described once, here, by a signature checker. Semantic analysis
// uses the checker to type a call site; the VM uses the matching index to
// dispatch. Keeping both sides keyed by `NativeId` means a builtin cannot be
// added to one half and forgotten in the other.
//
// Several builtins are generic in a way Fire's surface syntax cannot express
// (`len` works on `str` and on every `[T]`; `pop` returns the element type of
// whatever array it was given). Rather than introduce type variables into a
// language that does not need them, each native carries a small C++ function
// that inspects the argument types and reports its own diagnostics.
#pragma once

#include "fire/diagnostics.hpp"
#include "fire/type.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace fire {

enum class NativeId : std::uint32_t {
    Print,
    Println,
    Len,
    Push,
    Pop,
    Insert,
    Remove,
    Slice,
    ToInt,
    ToFloat,
    ToBool,
    ToStr,
    Abs,
    Min,
    Max,
    Pow,
    Sqrt,
    Floor,
    Ceil,
    Round,
    Chr,
    Ord,
    Find,
    Split,
    Join,
    Trim,
    Upper,
    Lower,
    Replace,
    Repeat,
    Input,
    Eof,
    Exit,
    Assert,
    Panic,
    Clock,
    Args,
    Seed,
    RandInt,

    Count,
};

/// Everything a native's signature checker is allowed to look at.
struct NativeCallCheck {
    TypeContext& types;
    DiagnosticEngine& diagnostics;
    /// Types of the evaluated arguments, in order. May contain the error type.
    const std::vector<const Type*>& arguments;
    /// Spans of the argument expressions, parallel to `arguments`.
    const std::vector<Span>& argument_spans;
    Span call_span;
    std::string_view name;

    /// Report a mismatch on argument `index` and return the error type, so a
    /// checker can `return check.mismatch(0, "str");` in one line.
    const Type* mismatch(std::size_t index, std::string_view expected) const;
};

using NativeChecker = const Type* (*)(const NativeCallCheck&);

struct NativeInfo {
    std::string_view name;
    std::uint16_t min_arity;
    /// kVariadic for "any number".
    std::uint16_t max_arity;
    NativeChecker check;
    /// True for `exit` and `panic`: control never comes back, which makes a
    /// function ending in one of them well-formed even without a `return`.
    bool never_returns;
    /// One-line summary shown by `fire builtins`.
    std::string_view summary;

    static constexpr std::uint16_t kVariadic = 0xFFFF;
};

/// The table, indexed by NativeId.
[[nodiscard]] const std::vector<NativeInfo>& native_table();

/// Look up a builtin by source name.
[[nodiscard]] std::optional<NativeId> find_native(std::string_view name);

[[nodiscard]] const NativeInfo& native_info(NativeId id);

} // namespace fire
