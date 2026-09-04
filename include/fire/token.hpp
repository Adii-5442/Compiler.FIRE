// SPDX-License-Identifier: MIT
//
// The Fire token vocabulary.
#pragma once

#include "fire/source.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fire {

enum class TokenType : std::uint8_t {
    // Literals and names
    Identifier,
    IntLiteral,
    FloatLiteral,
    StringLiteral,

    // Keywords
    KwLet,
    KwConst,
    KwFn,
    KwReturn,
    KwIf,
    KwElif,
    KwElse,
    KwWhile,
    KwFor,
    KwIn,
    KwBreak,
    KwContinue,
    KwTrue,
    KwFalse,

    // Type keywords
    KwInt,
    KwFloat,
    KwBool,
    KwStr,
    KwVoid,

    // Grouping
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,

    // Punctuation
    Comma,
    Semicolon,
    Colon,
    Arrow, // ->
    DotDot, // ..

    // Assignment
    Assign, // =
    PlusAssign, // +=
    MinusAssign, // -=
    StarAssign, // *=
    SlashAssign, // /=
    PercentAssign, // %=

    // Arithmetic
    Plus,
    Minus,
    Star,
    Slash,
    Percent,

    // Comparison
    EqualEqual,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,

    // Logical
    AmpAmp,
    PipePipe,
    Bang,

    // Bitwise
    Amp,
    Pipe,
    Caret,
    Tilde,
    LessLess,
    GreaterGreater,

    EndOfFile,
};

/// Spelling used in diagnostics: `+`, `let`, `identifier`, …
[[nodiscard]] const char* token_type_name(TokenType type);

/// Short symbolic name used by `fire emit --tokens`.
[[nodiscard]] const char* token_type_tag(TokenType type);

/// Maps `let`, `fn`, … to their keyword token. Returns nullopt for names.
[[nodiscard]] std::optional<TokenType> keyword_from_text(std::string_view text);

/// True for the type keywords that may head a type annotation.
[[nodiscard]] bool is_type_keyword(TokenType type);

struct Token {
    TokenType type = TokenType::EndOfFile;
    Span span;
    /// Identifier text, or the *decoded* payload of a literal (escape
    /// sequences already resolved for strings, digit separators stripped for
    /// numbers).
    std::string text;
    /// Parsed numeric payload; only meaningful for the matching literal type.
    std::int64_t int_value = 0;
    double float_value = 0.0;

    [[nodiscard]] bool is(TokenType t) const { return type == t; }
};

} // namespace fire
