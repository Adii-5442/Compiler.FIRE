// SPDX-License-Identifier: MIT
#include "fire/ast.hpp"

namespace fire {

const char* unary_op_spelling(UnaryOp op)
{
    switch (op) {
    case UnaryOp::Negate:
        return "-";
    case UnaryOp::Not:
        return "!";
    case UnaryOp::BitNot:
        return "~";
    }
    return "?";
}

const char* binary_op_spelling(BinaryOp op)
{
    switch (op) {
    case BinaryOp::Add:
        return "+";
    case BinaryOp::Subtract:
        return "-";
    case BinaryOp::Multiply:
        return "*";
    case BinaryOp::Divide:
        return "/";
    case BinaryOp::Modulo:
        return "%";
    case BinaryOp::Equal:
        return "==";
    case BinaryOp::NotEqual:
        return "!=";
    case BinaryOp::Less:
        return "<";
    case BinaryOp::LessEqual:
        return "<=";
    case BinaryOp::Greater:
        return ">";
    case BinaryOp::GreaterEqual:
        return ">=";
    case BinaryOp::BitAnd:
        return "&";
    case BinaryOp::BitOr:
        return "|";
    case BinaryOp::BitXor:
        return "^";
    case BinaryOp::ShiftLeft:
        return "<<";
    case BinaryOp::ShiftRight:
        return ">>";
    }
    return "?";
}

const char* logical_op_spelling(LogicalOp op)
{
    return op == LogicalOp::And ? "&&" : "||";
}

bool is_comparison(BinaryOp op)
{
    switch (op) {
    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual:
        return true;
    default:
        return false;
    }
}

bool is_bitwise(BinaryOp op)
{
    switch (op) {
    case BinaryOp::BitAnd:
    case BinaryOp::BitOr:
    case BinaryOp::BitXor:
    case BinaryOp::ShiftLeft:
    case BinaryOp::ShiftRight:
        return true;
    default:
        return false;
    }
}

} // namespace fire
