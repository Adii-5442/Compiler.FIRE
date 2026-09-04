// SPDX-License-Identifier: MIT
#include "fire/token.hpp"

#include <array>
#include <utility>

namespace fire {
namespace {

    // Keyword table. Kept as a sorted-by-nothing flat array because it has
    // twenty entries: a linear scan beats a hash map at this size and keeps
    // the lexer free of allocations.
    constexpr std::array<std::pair<std::string_view, TokenType>, 19> kKeywords { {
        { "let", TokenType::KwLet },
        { "const", TokenType::KwConst },
        { "fn", TokenType::KwFn },
        { "return", TokenType::KwReturn },
        { "if", TokenType::KwIf },
        { "elif", TokenType::KwElif },
        { "else", TokenType::KwElse },
        { "while", TokenType::KwWhile },
        { "for", TokenType::KwFor },
        { "in", TokenType::KwIn },
        { "break", TokenType::KwBreak },
        { "continue", TokenType::KwContinue },
        { "true", TokenType::KwTrue },
        { "false", TokenType::KwFalse },
        { "int", TokenType::KwInt },
        { "float", TokenType::KwFloat },
        { "bool", TokenType::KwBool },
        { "str", TokenType::KwStr },
        { "void", TokenType::KwVoid },
    } };

} // namespace

std::optional<TokenType> keyword_from_text(std::string_view text)
{
    for (const auto& [spelling, type] : kKeywords) {
        if (spelling == text) {
            return type;
        }
    }
    return std::nullopt;
}

bool is_type_keyword(TokenType type)
{
    switch (type) {
    case TokenType::KwInt:
    case TokenType::KwFloat:
    case TokenType::KwBool:
    case TokenType::KwStr:
    case TokenType::KwVoid:
        return true;
    default:
        return false;
    }
}

const char* token_type_name(TokenType type)
{
    switch (type) {
    case TokenType::Identifier:
        return "identifier";
    case TokenType::IntLiteral:
        return "integer literal";
    case TokenType::FloatLiteral:
        return "float literal";
    case TokenType::StringLiteral:
        return "string literal";
    case TokenType::KwLet:
        return "let";
    case TokenType::KwConst:
        return "const";
    case TokenType::KwFn:
        return "fn";
    case TokenType::KwReturn:
        return "return";
    case TokenType::KwIf:
        return "if";
    case TokenType::KwElif:
        return "elif";
    case TokenType::KwElse:
        return "else";
    case TokenType::KwWhile:
        return "while";
    case TokenType::KwFor:
        return "for";
    case TokenType::KwIn:
        return "in";
    case TokenType::KwBreak:
        return "break";
    case TokenType::KwContinue:
        return "continue";
    case TokenType::KwTrue:
        return "true";
    case TokenType::KwFalse:
        return "false";
    case TokenType::KwInt:
        return "int";
    case TokenType::KwFloat:
        return "float";
    case TokenType::KwBool:
        return "bool";
    case TokenType::KwStr:
        return "str";
    case TokenType::KwVoid:
        return "void";
    case TokenType::LParen:
        return "(";
    case TokenType::RParen:
        return ")";
    case TokenType::LBrace:
        return "{";
    case TokenType::RBrace:
        return "}";
    case TokenType::LBracket:
        return "[";
    case TokenType::RBracket:
        return "]";
    case TokenType::Comma:
        return ",";
    case TokenType::Semicolon:
        return ";";
    case TokenType::Colon:
        return ":";
    case TokenType::Arrow:
        return "->";
    case TokenType::DotDot:
        return "..";
    case TokenType::Assign:
        return "=";
    case TokenType::PlusAssign:
        return "+=";
    case TokenType::MinusAssign:
        return "-=";
    case TokenType::StarAssign:
        return "*=";
    case TokenType::SlashAssign:
        return "/=";
    case TokenType::PercentAssign:
        return "%=";
    case TokenType::Plus:
        return "+";
    case TokenType::Minus:
        return "-";
    case TokenType::Star:
        return "*";
    case TokenType::Slash:
        return "/";
    case TokenType::Percent:
        return "%";
    case TokenType::EqualEqual:
        return "==";
    case TokenType::BangEqual:
        return "!=";
    case TokenType::Less:
        return "<";
    case TokenType::LessEqual:
        return "<=";
    case TokenType::Greater:
        return ">";
    case TokenType::GreaterEqual:
        return ">=";
    case TokenType::AmpAmp:
        return "&&";
    case TokenType::PipePipe:
        return "||";
    case TokenType::Bang:
        return "!";
    case TokenType::Amp:
        return "&";
    case TokenType::Pipe:
        return "|";
    case TokenType::Caret:
        return "^";
    case TokenType::Tilde:
        return "~";
    case TokenType::LessLess:
        return "<<";
    case TokenType::GreaterGreater:
        return ">>";
    case TokenType::EndOfFile:
        return "end of file";
    }
    return "<unknown>";
}

const char* token_type_tag(TokenType type)
{
    switch (type) {
    case TokenType::Identifier:
        return "IDENT";
    case TokenType::IntLiteral:
        return "INT";
    case TokenType::FloatLiteral:
        return "FLOAT";
    case TokenType::StringLiteral:
        return "STR";
    case TokenType::EndOfFile:
        return "EOF";
    default:
        break;
    }
    // The keyword tokens are contiguous in the enum, from KwLet to KwVoid.
    if (type >= TokenType::KwLet && type <= TokenType::KwVoid) {
        return "KEYWORD";
    }
    return "PUNCT";
}

} // namespace fire
