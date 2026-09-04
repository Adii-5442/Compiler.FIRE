// SPDX-License-Identifier: MIT
#include "fire/chunk.hpp"

#include <algorithm>
#include <cassert>

namespace fire {

const char* opcode_name(OpCode op)
{
    switch (op) {
    case OpCode::Constant:
        return "CONST";
    case OpCode::PushTrue:
        return "TRUE";
    case OpCode::PushFalse:
        return "FALSE";
    case OpCode::Pop:
        return "POP";
    case OpCode::Dup:
        return "DUP";
    case OpCode::GetLocal:
        return "GET_LOCAL";
    case OpCode::SetLocal:
        return "SET_LOCAL";
    case OpCode::GetGlobal:
        return "GET_GLOBAL";
    case OpCode::SetGlobal:
        return "SET_GLOBAL";
    case OpCode::AddInt:
        return "ADD_I";
    case OpCode::SubInt:
        return "SUB_I";
    case OpCode::MulInt:
        return "MUL_I";
    case OpCode::DivInt:
        return "DIV_I";
    case OpCode::ModInt:
        return "MOD_I";
    case OpCode::NegInt:
        return "NEG_I";
    case OpCode::AddFloat:
        return "ADD_F";
    case OpCode::SubFloat:
        return "SUB_F";
    case OpCode::MulFloat:
        return "MUL_F";
    case OpCode::DivFloat:
        return "DIV_F";
    case OpCode::NegFloat:
        return "NEG_F";
    case OpCode::ConcatStr:
        return "CONCAT";
    case OpCode::BitAnd:
        return "BIT_AND";
    case OpCode::BitOr:
        return "BIT_OR";
    case OpCode::BitXor:
        return "BIT_XOR";
    case OpCode::ShiftLeft:
        return "SHL";
    case OpCode::ShiftRight:
        return "SHR";
    case OpCode::BitNot:
        return "BIT_NOT";
    case OpCode::Not:
        return "NOT";
    case OpCode::Equal:
        return "EQ";
    case OpCode::NotEqual:
        return "NE";
    case OpCode::LessInt:
        return "LT_I";
    case OpCode::LessEqualInt:
        return "LE_I";
    case OpCode::GreaterInt:
        return "GT_I";
    case OpCode::GreaterEqualInt:
        return "GE_I";
    case OpCode::LessFloat:
        return "LT_F";
    case OpCode::LessEqualFloat:
        return "LE_F";
    case OpCode::GreaterFloat:
        return "GT_F";
    case OpCode::GreaterEqualFloat:
        return "GE_F";
    case OpCode::LessStr:
        return "LT_S";
    case OpCode::LessEqualStr:
        return "LE_S";
    case OpCode::GreaterStr:
        return "GT_S";
    case OpCode::GreaterEqualStr:
        return "GE_S";
    case OpCode::MakeArray:
        return "MAKE_ARRAY";
    case OpCode::IndexGet:
        return "INDEX_GET";
    case OpCode::IndexSet:
        return "INDEX_SET";
    case OpCode::Jump:
        return "JUMP";
    case OpCode::JumpIfFalse:
        return "JUMP_IF_FALSE";
    case OpCode::JumpIfFalsePeek:
        return "JUMP_IF_FALSE_PEEK";
    case OpCode::JumpIfTruePeek:
        return "JUMP_IF_TRUE_PEEK";
    case OpCode::Call:
        return "CALL";
    case OpCode::CallNative:
        return "CALL_NATIVE";
    case OpCode::Return:
        return "RET";
    case OpCode::ReturnVoid:
        return "RET_VOID";
    case OpCode::Halt:
        return "HALT";
    }
    return "???";
}

std::size_t opcode_length(OpCode op)
{
    switch (op) {
    case OpCode::Constant:
    case OpCode::GetLocal:
    case OpCode::SetLocal:
    case OpCode::GetGlobal:
    case OpCode::SetGlobal:
    case OpCode::MakeArray:
        return 1 + 2;
    case OpCode::Jump:
    case OpCode::JumpIfFalse:
    case OpCode::JumpIfFalsePeek:
    case OpCode::JumpIfTruePeek:
        return 1 + 4;
    case OpCode::Call:
    case OpCode::CallNative:
        return 1 + 2 + 1;
    default:
        return 1;
    }
}

void Chunk::write(std::uint8_t byte, Span span)
{
    if (m_spans.empty() || m_spans.back().span.begin != span.begin
        || m_spans.back().span.end != span.end) {
        m_spans.push_back(SpanEntry { static_cast<std::uint32_t>(m_code.size()), span });
    }
    m_code.push_back(byte);
}

void Chunk::write_op(OpCode op, Span span) { write(static_cast<std::uint8_t>(op), span); }

void Chunk::write_u8(std::uint8_t value, Span span) { write(value, span); }

void Chunk::write_u16(std::uint16_t value, Span span)
{
    write(static_cast<std::uint8_t>(value & 0xFF), span);
    write(static_cast<std::uint8_t>((value >> 8) & 0xFF), span);
}

void Chunk::write_u32(std::uint32_t value, Span span)
{
    for (int shift = 0; shift < 32; shift += 8) {
        write(static_cast<std::uint8_t>((value >> shift) & 0xFF), span);
    }
}

std::uint16_t Chunk::add_constant(const Value& value)
{
    for (std::size_t i = 0; i < m_constants.size(); ++i) {
        if (m_constants[i].equals(value)) {
            return static_cast<std::uint16_t>(i);
        }
    }
    m_constants.push_back(value);
    return static_cast<std::uint16_t>(m_constants.size() - 1);
}

void Chunk::patch_u32(std::size_t offset, std::uint32_t value)
{
    assert(offset + 4 <= m_code.size() && "patch target is outside the chunk");
    for (int i = 0; i < 4; ++i) {
        m_code[offset + static_cast<std::size_t>(i)]
            = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
    }
}

std::uint16_t Chunk::read_u16(std::size_t offset) const
{
    return static_cast<std::uint16_t>(
        m_code[offset] | (static_cast<std::uint16_t>(m_code[offset + 1]) << 8));
}

std::uint32_t Chunk::read_u32(std::size_t offset) const
{
    std::uint32_t value = 0;
    for (int i = 3; i >= 0; --i) {
        value = (value << 8) | m_code[offset + static_cast<std::size_t>(i)];
    }
    return value;
}

Span Chunk::span_at(std::size_t offset) const
{
    if (m_spans.empty()) {
        return Span {};
    }
    // Last entry whose offset is <= the one asked for.
    auto it = std::upper_bound(m_spans.begin(), m_spans.end(), offset,
        [](std::size_t value, const SpanEntry& entry) { return value < entry.offset; });
    if (it == m_spans.begin()) {
        return m_spans.front().span;
    }
    return (it - 1)->span;
}

} // namespace fire
