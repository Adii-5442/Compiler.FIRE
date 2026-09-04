// SPDX-License-Identifier: MIT
//
// The Fire virtual machine: a stack machine over the bytecode in `chunk.hpp`.
//
// Because the type checker has already proved the program well-typed, the
// interpreter loop does no type dispatch: `ADD_I` adds two ints because it can
// only ever have been emitted where both operands are ints. The remaining
// checks are the ones static typing cannot make — division by zero, index
// bounds, stack depth — and each of those produces a diagnostic with the
// source span of the instruction that failed, plus a call stack.
#pragma once

#include "fire/chunk.hpp"
#include "fire/diagnostics.hpp"
#include "fire/source.hpp"
#include "fire/value.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace fire {

/// Thrown by a failing instruction or builtin and caught by VM::run.
struct RuntimeError {
    std::string message;
    std::string help;
};

/// Thrown by the `exit` builtin.
struct ExitSignal {
    int code;
};

class VM {
public:
    struct Options {
        /// Used to render runtime diagnostics; may be null.
        const SourceFile* source = nullptr;
        /// What the `args()` builtin returns.
        std::vector<std::string> program_args;
        std::ostream* out = nullptr; ///< defaults to std::cout
        std::ostream* err = nullptr; ///< defaults to std::cerr
        std::istream* in = nullptr; ///< defaults to std::cin
        /// Disassemble every instruction before executing it.
        bool trace = false;
        bool color = false;
        /// Guard against runaway recursion.
        std::uint32_t max_call_depth = 4096;
    };

    VM(const Module& module, Options options);

    /// Execute the module. Returns the process exit status: 0 normally, the
    /// argument of `exit(n)`, or 70 after an unhandled runtime error.
    int run();

    // -- interface used by the builtins ------------------------------------
    std::ostream& out();
    std::ostream& err();
    std::istream& in();
    [[nodiscard]] const std::vector<std::string>& program_args() const
    {
        return m_options.program_args;
    }
    /// Deterministic xorshift state behind `seed` and `rand_int`.
    std::uint64_t& rng_state() { return m_rng; }

    /// Global storage, exposed so the REPL can carry it from one fragment to
    /// the next.
    [[nodiscard]] const std::vector<Value>& globals() const { return m_globals; }
    void adopt_globals(std::vector<Value> globals);

    [[noreturn]] static void fail(std::string message, std::string help = { });

private:
    struct Frame {
        const CompiledFunction* function = nullptr;
        std::size_t ip = 0;
        /// Index in the value stack where this frame's slot 0 lives.
        std::size_t base = 0;
    };

    // -- stack --------------------------------------------------------------
    void push(Value value) { m_stack.push_back(std::move(value)); }
    Value pop();
    [[nodiscard]] Value& peek(std::size_t distance = 0);

    // -- decoding -----------------------------------------------------------
    [[nodiscard]] std::uint8_t read_u8();
    [[nodiscard]] std::uint16_t read_u16();
    [[nodiscard]] std::uint32_t read_u32();

    void call_function(std::uint16_t index, std::uint8_t argument_count);
    void call_native(std::uint16_t id, std::uint8_t argument_count);
    /// Main loop. Throws RuntimeError or ExitSignal.
    void execute();

    /// Render a runtime error with the current span and a call stack.
    void report(const RuntimeError& error) const;

    const Module& m_module;
    Options m_options;
    std::vector<Value> m_stack;
    std::vector<Frame> m_frames;
    std::vector<Value> m_globals;
    std::uint64_t m_rng = 0x2545F4914F6CDD1DULL;
};

/// Execute one builtin. Declared here so the VM and the builtin
/// implementations can live in separate translation units.
Value invoke_native(VM& vm, std::uint16_t id, std::vector<Value>& arguments);

} // namespace fire
