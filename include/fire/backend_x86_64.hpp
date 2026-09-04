// SPDX-License-Identifier: MIT
//
// The native x86-64 backend.
//
// Fire's primary target is its own bytecode; this backend exists because a
// compiler that only ever produces bytecode leaves the most interesting half
// of the subject unexplored. It emits freestanding NASM-syntax assembly for
// Linux — no libc, no runtime, just `_start`, the `write` and `exit` syscalls,
// and a handful of hand-written helpers.
//
// It supports a deliberate subset of the language: `int` and `bool`
// throughout, `str` as literals passed to `print`/`println`, all the operators
// and control flow, and functions including recursion. `float`, arrays,
// dynamic strings and most builtins would each need runtime support that the
// bytecode VM already provides properly, so instead of half-implementing them
// the backend reports a precise diagnostic naming the feature and pointing at
// `fire run`.
#pragma once

#include "fire/ast.hpp"
#include "fire/diagnostics.hpp"
#include "fire/type.hpp"

#include <string>

namespace fire {

/// Translate an analysed program to NASM-syntax x86-64 assembly. Unsupported
/// constructs are reported through `diagnostics`; check `has_errors()` before
/// using the result.
[[nodiscard]] std::string emit_x86_64(
    const Program& program, TypeContext& types, DiagnosticEngine& diagnostics);

struct NativeBuildResult {
    bool ok = false;
    /// Path of the file produced (an executable, or the .asm with -S).
    std::string artifact;
    /// Populated when `ok` is false and the cause is not a diagnostic.
    std::string message;
};

/// Emit assembly, then assemble and link it with nasm and ld. With
/// `assembly_only`, stop after writing `<output>.asm`.
NativeBuildResult build_native(const Program& program, TypeContext& types,
    DiagnosticEngine& diagnostics, const std::string& output, bool assembly_only);

} // namespace fire
