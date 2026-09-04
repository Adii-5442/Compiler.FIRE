// SPDX-License-Identifier: MIT
//
// The Fire bytecode: instruction set, chunks and modules.
//
// A chunk is a flat byte vector plus a constant pool. Operands are little
// endian and fixed width per opcode, which keeps the decoder a switch over one
// byte and the disassembler exact.
//
// Jump targets are absolute byte offsets rather than relative displacements.
// Absolute targets cost two extra bytes per jump and remove an entire class of
// off-by-one bugs from the compiler's patching code — a trade worth making in
// a bytecode nobody is trying to fit in a cache line.
//
// Because Fire is statically typed, arithmetic opcodes are type-specialised:
// the VM never inspects a tag to decide what `+` means. That is the main
// performance benefit the type checker buys.
#pragma once

#include "fire/source.hpp"
#include "fire/value.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fire {

enum class OpCode : std::uint8_t {
    // -- stack -------------------------------------------------------------
    Constant, ///< u16 index; push constants[index]
    PushTrue,
    PushFalse,
    Pop,
    Dup,
    Dup2, ///< duplicate the top two values, order preserved

    // -- variables ---------------------------------------------------------
    GetLocal, ///< u16 slot
    SetLocal, ///< u16 slot; pops
    GetGlobal, ///< u16 slot
    SetGlobal, ///< u16 slot; pops

    // -- int arithmetic ----------------------------------------------------
    AddInt,
    SubInt,
    MulInt,
    DivInt,
    ModInt,
    NegInt,

    // -- float arithmetic --------------------------------------------------
    AddFloat,
    SubFloat,
    MulFloat,
    DivFloat,
    NegFloat,

    // -- strings -----------------------------------------------------------
    ConcatStr,

    // -- bitwise -----------------------------------------------------------
    BitAnd,
    BitOr,
    BitXor,
    ShiftLeft,
    ShiftRight,
    BitNot,

    // -- logic -------------------------------------------------------------
    Not,

    // -- comparison --------------------------------------------------------
    Equal,
    NotEqual,
    LessInt,
    LessEqualInt,
    GreaterInt,
    GreaterEqualInt,
    LessFloat,
    LessEqualFloat,
    GreaterFloat,
    GreaterEqualFloat,
    LessStr,
    LessEqualStr,
    GreaterStr,
    GreaterEqualStr,

    // -- aggregates --------------------------------------------------------
    MakeArray, ///< u16 count; pops that many, pushes an array
    IndexGet, ///< pops index and target, pushes the element
    IndexSet, ///< pops value, index and target

    // -- control flow ------------------------------------------------------
    Jump, ///< u32 absolute target
    JumpIfFalse, ///< u32 target; pops the condition
    JumpIfFalsePeek, ///< u32 target; leaves the condition when it jumps
    JumpIfTruePeek, ///< u32 target; leaves the condition when it jumps

    // -- calls -------------------------------------------------------------
    Call, ///< u16 function index, u8 argument count
    CallNative, ///< u16 native id, u8 argument count
    Return, ///< returns the value on top of the stack
    ReturnVoid,
    Halt,
};

[[nodiscard]] const char* opcode_name(OpCode op);
/// Total instruction length in bytes, opcode included.
[[nodiscard]] std::size_t opcode_length(OpCode op);

/// Maps a byte offset back to the source it came from, run-length encoded:
/// one entry per instruction whose span differs from its predecessor's.
struct SpanEntry {
    std::uint32_t offset;
    Span span;
};

class Chunk {
public:
    /// Append one byte. Every emit funnels through here so that span tracking
    /// cannot be forgotten.
    void write(std::uint8_t byte, Span span);
    void write_op(OpCode op, Span span);
    void write_u8(std::uint8_t value, Span span);
    void write_u16(std::uint16_t value, Span span);
    void write_u32(std::uint32_t value, Span span);

    /// Intern a constant, reusing an equal one already in the pool.
    [[nodiscard]] std::uint16_t add_constant(const Value& value);

    /// Overwrite the u32 at `offset`; used to patch forward jumps.
    void patch_u32(std::size_t offset, std::uint32_t value);

    [[nodiscard]] std::size_t size() const { return m_code.size(); }
    [[nodiscard]] const std::vector<std::uint8_t>& code() const { return m_code; }
    [[nodiscard]] const std::vector<Value>& constants() const { return m_constants; }

    [[nodiscard]] std::uint8_t byte_at(std::size_t offset) const { return m_code[offset]; }
    [[nodiscard]] std::uint16_t read_u16(std::size_t offset) const;
    [[nodiscard]] std::uint32_t read_u32(std::size_t offset) const;

    /// Source span of the instruction covering `offset`.
    [[nodiscard]] Span span_at(std::size_t offset) const;

private:
    std::vector<std::uint8_t> m_code;
    std::vector<Value> m_constants;
    std::vector<SpanEntry> m_spans;
};

/// A compiled function. The script body is one of these too, with arity 0.
struct CompiledFunction {
    std::string name;
    std::uint32_t arity = 0;
    /// Frame size, parameters included.
    std::uint32_t local_count = 0;
    Chunk chunk;
    Span span;
};

/// Everything one compilation produces.
struct Module {
    CompiledFunction script;
    std::vector<CompiledFunction> functions;
    std::uint32_t global_count = 0;
};

} // namespace fire
