// SPDX-License-Identifier: MIT
#include "fire/lexer.hpp"

#include <cerrno>
#include <cstdlib>
#include <limits>

namespace fire {
namespace {

    bool is_ident_start(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    bool is_ident_continue(char c)
    {
        return is_ident_start(c) || (c >= '0' && c <= '9');
    }

    bool is_decimal_digit(char c)
    {
        return c >= '0' && c <= '9';
    }

    bool is_hex_digit(char c)
    {
        return is_decimal_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    bool is_binary_digit(char c)
    {
        return c == '0' || c == '1';
    }

    bool is_octal_digit(char c)
    {
        return c >= '0' && c <= '7';
    }

    bool is_space(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
    }

    int hex_value(char c)
    {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return c - 'A' + 10;
    }

    /// Append the UTF-8 encoding of a code point.
    void append_utf8(std::string& out, std::uint32_t code_point)
    {
        if (code_point <= 0x7F) {
            out.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else if (code_point <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
    }

} // namespace

std::optional<char> Lexer::peek(std::uint32_t offset) const
{
    const std::uint32_t at = m_index + offset;
    if (at >= m_source.size()) {
        return std::nullopt;
    }
    return m_source.text()[at];
}

char Lexer::peek_or(char fallback, std::uint32_t offset) const
{
    return peek(offset).value_or(fallback);
}

char Lexer::advance()
{
    return m_source.text()[m_index++];
}

bool Lexer::match(char expected)
{
    if (peek_or('\0') != expected) {
        return false;
    }
    ++m_index;
    return true;
}

void Lexer::push(std::vector<Token>& out, TokenType type, std::uint32_t start)
{
    Token token;
    token.type = type;
    token.span = span_from(start);
    out.push_back(std::move(token));
}

void Lexer::skip_trivia()
{
    while (!at_end()) {
        const char c = peek_or('\0');
        if (is_space(c)) {
            ++m_index;
        } else if (c == '/' && peek_or('\0', 1) == '/') {
            while (!at_end() && peek_or('\n') != '\n') {
                ++m_index;
            }
        } else if (c == '/' && peek_or('\0', 1) == '*') {
            skip_block_comment();
        } else {
            return;
        }
    }
}

void Lexer::skip_block_comment()
{
    const std::uint32_t start = m_index;
    m_index += 2; // consume "/*"
    // Fire's block comments nest, so a commented-out region containing another
    // comment does not end early.
    int depth = 1;
    while (!at_end() && depth > 0) {
        if (peek_or('\0') == '/' && peek_or('\0', 1) == '*') {
            m_index += 2;
            ++depth;
        } else if (peek_or('\0') == '*' && peek_or('\0', 1) == '/') {
            m_index += 2;
            --depth;
        } else {
            ++m_index;
        }
    }
    if (depth > 0) {
        m_diagnostics.error("E0004", "unterminated block comment", Span { start, start + 2 })
            .label("this comment is never closed")
            .note("Fire block comments nest, so every `/*` needs a matching `*/`");
    }
}

void Lexer::lex_identifier_or_keyword(std::vector<Token>& out)
{
    const std::uint32_t start = m_index;
    while (!at_end() && is_ident_continue(peek_or('\0'))) {
        ++m_index;
    }
    Token token;
    token.span = span_from(start);
    token.text = std::string { m_source.snippet(token.span) };
    token.type = keyword_from_text(token.text).value_or(TokenType::Identifier);
    out.push_back(std::move(token));
}

bool Lexer::parse_integer(const std::string& text, int base, Span span, std::int64_t& value)
{
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, base);
    const auto limit = static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max());
    if (errno == ERANGE || parsed > limit) {
        m_diagnostics.error("E0005", "integer literal is out of range", span)
            .label("does not fit in `int`")
            .note("`int` is a signed 64-bit integer, so the maximum is 9223372036854775807");
        value = 0;
        return false;
    }
    value = static_cast<std::int64_t>(parsed);
    return true;
}

void Lexer::lex_number(std::vector<Token>& out)
{
    const std::uint32_t start = m_index;

    // Radix prefixes: 0x / 0b / 0o. `_` may separate digits anywhere after the
    // first one, which makes 1_000_000 and 0b1010_1010 readable.
    int base = 10;
    if (peek_or('\0') == '0') {
        const char marker = peek_or('\0', 1);
        if (marker == 'x' || marker == 'X') {
            base = 16;
        } else if (marker == 'b' || marker == 'B') {
            base = 2;
        } else if (marker == 'o' || marker == 'O') {
            base = 8;
        }
    }

    std::string digits;
    if (base != 10) {
        m_index += 2; // consume the prefix
        auto accepts = base == 16 ? is_hex_digit : (base == 2 ? is_binary_digit : is_octal_digit);
        while (!at_end() && (accepts(peek_or('\0')) || peek_or('\0') == '_')) {
            const char c = advance();
            if (c != '_') {
                digits.push_back(c);
            }
        }
        Token token;
        token.span = span_from(start);
        token.type = TokenType::IntLiteral;
        if (digits.empty()) {
            m_diagnostics
                .error("E0006", "numeric literal has a base prefix but no digits", token.span)
                .label("expected at least one digit after the prefix");
        } else {
            parse_integer(digits, base, token.span, token.int_value);
        }
        token.text = digits;
        out.push_back(std::move(token));
        return;
    }

    while (!at_end() && (is_decimal_digit(peek_or('\0')) || peek_or('\0') == '_')) {
        const char c = advance();
        if (c != '_') {
            digits.push_back(c);
        }
    }

    // A `.` only starts a fractional part when a digit follows it. That keeps
    // `0..10` lexing as `0`, `..`, `10` rather than as a malformed float.
    bool is_float = false;
    if (peek_or('\0') == '.' && is_decimal_digit(peek_or('\0', 1))) {
        is_float = true;
        digits.push_back(advance()); // '.'
        while (!at_end() && (is_decimal_digit(peek_or('\0')) || peek_or('\0') == '_')) {
            const char c = advance();
            if (c != '_') {
                digits.push_back(c);
            }
        }
    }

    if (peek_or('\0') == 'e' || peek_or('\0') == 'E') {
        const char sign = peek_or('\0', 1);
        const bool has_sign = sign == '+' || sign == '-';
        if (is_decimal_digit(has_sign ? peek_or('\0', 2) : sign)) {
            is_float = true;
            digits.push_back(advance()); // 'e'
            if (has_sign) {
                digits.push_back(advance());
            }
            while (!at_end() && is_decimal_digit(peek_or('\0'))) {
                digits.push_back(advance());
            }
        }
    }

    Token token;
    token.span = span_from(start);
    token.text = digits;
    if (is_float) {
        token.type = TokenType::FloatLiteral;
        token.float_value = std::strtod(digits.c_str(), nullptr);
    } else {
        token.type = TokenType::IntLiteral;
        parse_integer(digits, 10, token.span, token.int_value);
    }
    out.push_back(std::move(token));
}

bool Lexer::lex_escape(std::string& out)
{
    const std::uint32_t escape_start = m_index - 1; // the backslash
    if (at_end()) {
        return false;
    }
    const char c = advance();
    switch (c) {
    case 'n':
        out.push_back('\n');
        return true;
    case 't':
        out.push_back('\t');
        return true;
    case 'r':
        out.push_back('\r');
        return true;
    case '0':
        out.push_back('\0');
        return true;
    case '\\':
        out.push_back('\\');
        return true;
    case '"':
        out.push_back('"');
        return true;
    case 'e':
        out.push_back('\033');
        return true;
    case 'x': {
        if (!is_hex_digit(peek_or('\0')) || !is_hex_digit(peek_or('\0', 1))) {
            m_diagnostics
                .error(
                    "E0007", "`\\x` escape needs exactly two hex digits", span_from(escape_start))
                .help("write `\\x41` for the byte 0x41");
            return false;
        }
        const int high = hex_value(advance());
        const int low = hex_value(advance());
        out.push_back(static_cast<char>(high * 16 + low));
        return true;
    }
    case 'u': {
        if (!match('{')) {
            m_diagnostics
                .error("E0007", "`\\u` escape must be followed by `{`", span_from(escape_start))
                .help("write `\\u{1F525}` for a code point");
            return false;
        }
        std::uint32_t code_point = 0;
        int digit_count = 0;
        while (!at_end() && is_hex_digit(peek_or('\0'))) {
            code_point = code_point * 16 + static_cast<std::uint32_t>(hex_value(advance()));
            ++digit_count;
        }
        if (!match('}') || digit_count == 0 || digit_count > 6 || code_point > 0x10FFFF) {
            m_diagnostics.error("E0007", "malformed `\\u{...}` escape", span_from(escape_start))
                .label("expected 1-6 hex digits naming a Unicode code point")
                .help("the largest valid code point is `\\u{10FFFF}`");
            return false;
        }
        append_utf8(out, code_point);
        return true;
    }
    default:
        m_diagnostics
            .error("E0007", std::string { "unknown escape sequence `\\" } + c + '`',
                span_from(escape_start))
            .note("valid escapes are \\n \\t \\r \\0 \\e \\\\ \\\" \\xNN and \\u{...}");
        return false;
    }
}

void Lexer::lex_string(std::vector<Token>& out)
{
    const std::uint32_t start = m_index;
    advance(); // opening quote

    std::string value;
    bool terminated = false;
    while (!at_end()) {
        const char c = peek_or('\0');
        if (c == '"') {
            advance();
            terminated = true;
            break;
        }
        if (c == '\n') {
            break; // report below; strings do not span lines
        }
        advance();
        if (c == '\\') {
            lex_escape(value);
        } else {
            value.push_back(c);
        }
    }

    Token token;
    token.span = span_from(start);
    token.type = TokenType::StringLiteral;
    token.text = std::move(value);
    if (!terminated) {
        m_diagnostics.error("E0008", "unterminated string literal", token.span)
            .label("this string is missing its closing `\"`")
            .note("a string literal may not contain a raw newline; use `\\n` instead");
    }
    out.push_back(std::move(token));
}

std::vector<Token> Lexer::tokenize()
{
    std::vector<Token> tokens;
    // Most source files average a token every four or five bytes.
    tokens.reserve(m_source.size() / 4 + 8);

    while (true) {
        skip_trivia();
        if (at_end()) {
            break;
        }

        const std::uint32_t start = m_index;
        const char c = peek_or('\0');

        if (is_ident_start(c)) {
            lex_identifier_or_keyword(tokens);
            continue;
        }
        if (is_decimal_digit(c)) {
            lex_number(tokens);
            continue;
        }
        if (c == '"') {
            lex_string(tokens);
            continue;
        }

        advance();
        switch (c) {
        case '(':
            push(tokens, TokenType::LParen, start);
            break;
        case ')':
            push(tokens, TokenType::RParen, start);
            break;
        case '{':
            push(tokens, TokenType::LBrace, start);
            break;
        case '}':
            push(tokens, TokenType::RBrace, start);
            break;
        case '[':
            push(tokens, TokenType::LBracket, start);
            break;
        case ']':
            push(tokens, TokenType::RBracket, start);
            break;
        case ',':
            push(tokens, TokenType::Comma, start);
            break;
        case ';':
            push(tokens, TokenType::Semicolon, start);
            break;
        case ':':
            push(tokens, TokenType::Colon, start);
            break;
        case '~':
            push(tokens, TokenType::Tilde, start);
            break;
        case '^':
            push(tokens, TokenType::Caret, start);
            break;
        case '+':
            push(tokens, match('=') ? TokenType::PlusAssign : TokenType::Plus, start);
            break;
        case '-':
            if (match('>')) {
                push(tokens, TokenType::Arrow, start);
            } else {
                push(tokens, match('=') ? TokenType::MinusAssign : TokenType::Minus, start);
            }
            break;
        case '*':
            push(tokens, match('=') ? TokenType::StarAssign : TokenType::Star, start);
            break;
        case '/':
            push(tokens, match('=') ? TokenType::SlashAssign : TokenType::Slash, start);
            break;
        case '%':
            push(tokens, match('=') ? TokenType::PercentAssign : TokenType::Percent, start);
            break;
        case '=':
            push(tokens, match('=') ? TokenType::EqualEqual : TokenType::Assign, start);
            break;
        case '!':
            push(tokens, match('=') ? TokenType::BangEqual : TokenType::Bang, start);
            break;
        case '<':
            if (match('<')) {
                push(tokens, TokenType::LessLess, start);
            } else {
                push(tokens, match('=') ? TokenType::LessEqual : TokenType::Less, start);
            }
            break;
        case '>':
            if (match('>')) {
                push(tokens, TokenType::GreaterGreater, start);
            } else {
                push(tokens, match('=') ? TokenType::GreaterEqual : TokenType::Greater, start);
            }
            break;
        case '&':
            push(tokens, match('&') ? TokenType::AmpAmp : TokenType::Amp, start);
            break;
        case '|':
            push(tokens, match('|') ? TokenType::PipePipe : TokenType::Pipe, start);
            break;
        case '.':
            if (match('.')) {
                push(tokens, TokenType::DotDot, start);
            } else {
                m_diagnostics.error("E0009", "unexpected `.`", span_from(start))
                    .label("Fire has no member access; did you mean the range operator `..`?");
            }
            break;
        default: {
            // Report one diagnostic per run of identical junk so that a binary
            // file fed to the compiler does not produce thousands of errors.
            while (!at_end() && peek_or('\0') == c) {
                advance();
            }
            std::string what =
                c >= 32 && c < 127 ? std::string { '`', c, '`' } : "non-printable byte";
            m_diagnostics.error("E0010", "unexpected character " + what, span_from(start))
                .label("this character is not part of any Fire token");
            break;
        }
        }

        if (m_diagnostics.at_error_limit()) {
            break;
        }
    }

    Token eof;
    eof.type = TokenType::EndOfFile;
    const auto size = static_cast<std::uint32_t>(m_source.size());
    eof.span = Span { size, size };
    tokens.push_back(std::move(eof));
    return tokens;
}

} // namespace fire
