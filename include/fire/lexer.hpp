// SPDX-License-Identifier: MIT
//
// Hand-written lexer for Fire.
//
// The lexer never aborts. Anything it cannot make sense of is reported to the
// DiagnosticEngine and skipped, so a file with a stray `@` still produces a
// full token stream and the parser can report its own errors in the same run.
#pragma once

#include "fire/diagnostics.hpp"
#include "fire/source.hpp"
#include "fire/token.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fire {

class Lexer {
public:
    Lexer(const SourceFile& source, DiagnosticEngine& diagnostics)
        : m_source(source)
        , m_diagnostics(diagnostics)
    {
    }

    /// Scan the whole file. The returned vector always ends with an
    /// `EndOfFile` token, which lets the parser peek without bounds checks.
    [[nodiscard]] std::vector<Token> tokenize();

private:
    [[nodiscard]] bool at_end() const { return m_index >= m_source.size(); }
    [[nodiscard]] std::optional<char> peek(std::uint32_t offset = 0) const;
    [[nodiscard]] char peek_or(char fallback, std::uint32_t offset = 0) const;
    char advance();
    /// Consume `expected` and return true if it is next.
    bool match(char expected);

    void skip_trivia();
    void skip_block_comment();

    void lex_identifier_or_keyword(std::vector<Token>& out);
    void lex_number(std::vector<Token>& out);
    void lex_string(std::vector<Token>& out);

    /// Decode one escape sequence, with the backslash already consumed.
    /// Appends to `out`; reports and returns false on an unknown escape.
    bool lex_escape(std::string& out);

    /// Parse `text` in `base` into `value`, reporting on overflow.
    bool parse_integer(const std::string& text, int base, Span span, std::int64_t& value);

    [[nodiscard]] Span span_from(std::uint32_t start) const
    {
        return Span { start, m_index };
    }

    void push(std::vector<Token>& out, TokenType type, std::uint32_t start);

    const SourceFile& m_source;
    DiagnosticEngine& m_diagnostics;
    std::uint32_t m_index = 0;
};

} // namespace fire
