// SPDX-License-Identifier: MIT
#include "fire/disasm.hpp"

#include "fire/natives.hpp"

#include <iomanip>
#include <ostream>

namespace fire {
namespace {

    void print_offset(std::ostream& out, std::size_t offset)
    {
        out << std::setw(6) << std::setfill('0') << offset << std::setfill(' ') << "  ";
    }

} // namespace

std::size_t disassemble_instruction(
    std::ostream& out, const Chunk& chunk, std::size_t offset, const SourceFile* source)
{
    print_offset(out, offset);

    if (source != nullptr) {
        const LineCol at = source->locate(chunk.span_at(offset).begin);
        // Repeat the line number only when it changes, the way objdump does.
        static thread_local std::uint32_t previous_line = 0;
        static thread_local const Chunk* previous_chunk = nullptr;
        if (previous_chunk != &chunk || previous_line != at.line) {
            out << std::setw(5) << at.line << "  ";
            previous_line = at.line;
            previous_chunk = &chunk;
        } else {
            out << "    |  ";
        }
    }

    const auto op = static_cast<OpCode>(chunk.byte_at(offset));
    out << std::left << std::setw(20) << opcode_name(op) << std::right;

    switch (op) {
    case OpCode::Constant: {
        const std::uint16_t index = chunk.read_u16(offset + 1);
        out << std::setw(6) << index << "  ; " << chunk.constants()[index].to_repr();
        break;
    }
    case OpCode::GetLocal:
    case OpCode::SetLocal:
    case OpCode::GetGlobal:
    case OpCode::SetGlobal:
    case OpCode::MakeArray:
        out << std::setw(6) << chunk.read_u16(offset + 1);
        break;
    case OpCode::Jump:
    case OpCode::JumpIfFalse:
    case OpCode::JumpIfFalsePeek:
    case OpCode::JumpIfTruePeek:
        out << std::setw(6) << "-> " << chunk.read_u32(offset + 1);
        break;
    case OpCode::Call:
        out << std::setw(6) << chunk.read_u16(offset + 1) << "  ; "
            << static_cast<int>(chunk.byte_at(offset + 3)) << " args";
        break;
    case OpCode::CallNative: {
        const std::uint16_t id = chunk.read_u16(offset + 1);
        out << std::setw(6) << id << "  ; " << native_table()[id].name << ", "
            << static_cast<int>(chunk.byte_at(offset + 3)) << " args";
        break;
    }
    default:
        break;
    }
    out << '\n';
    return offset + opcode_length(op);
}

void disassemble_function(
    std::ostream& out, const CompiledFunction& function, const SourceFile* source)
{
    out << "== " << function.name << " (" << function.arity << " params, " << function.local_count
        << " slots, " << function.chunk.size() << " bytes) ==\n";
    std::size_t offset = 0;
    while (offset < function.chunk.size()) {
        offset = disassemble_instruction(out, function.chunk, offset, source);
    }
    out << '\n';
}

void disassemble_module(std::ostream& out, const Module& module, const SourceFile* source)
{
    out << "; Fire bytecode module: " << module.global_count << " globals, "
        << module.functions.size() << " functions\n\n";
    disassemble_function(out, module.script, source);
    for (const CompiledFunction& function : module.functions) {
        disassemble_function(out, function, source);
    }
}

} // namespace fire
