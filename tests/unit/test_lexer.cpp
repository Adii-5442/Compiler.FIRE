// SPDX-License-Identifier: MIT
//
// Lexer tests: the shapes that are easy to get subtly wrong.
#include "framework.hpp"

#include "fire/lexer.hpp"

#include <sstream>

using namespace fire;
using namespace fire::test;

namespace {

struct Scan {
    std::vector<Token> tokens;
    std::string diagnostics;
};

Scan scan(const std::string& text)
{
    static std::vector<std::unique_ptr<SourceFile>> keep_alive;
    keep_alive.push_back(std::make_unique<SourceFile>(SourceFile::from_string("<lex>", text)));
    DiagnosticEngine diagnostics { *keep_alive.back(), false };
    Lexer lexer { *keep_alive.back(), diagnostics };

    Scan result;
    result.tokens = lexer.tokenize();
    std::ostringstream out;
    diagnostics.render(out);
    result.diagnostics = out.str();
    return result;
}

} // namespace

FIRE_TEST(lexer, always_ends_with_eof)
{
    const Scan result = scan("");
    FIRE_CHECK_EQ(result.tokens.size(), std::size_t { 1 });
    FIRE_CHECK(result.tokens.back().is(TokenType::EndOfFile));
}

FIRE_TEST(lexer, keywords_are_not_identifiers)
{
    const Scan result = scan("let letter const");
    FIRE_CHECK(result.tokens[0].is(TokenType::KwLet));
    FIRE_CHECK(result.tokens[1].is(TokenType::Identifier));
    FIRE_CHECK_EQ(result.tokens[1].text, std::string { "letter" });
    FIRE_CHECK(result.tokens[2].is(TokenType::KwConst));
}

FIRE_TEST(lexer, integer_bases_and_separators)
{
    const Scan result = scan("42 0xFF 0b1010 0o17 1_000_000");
    FIRE_CHECK_EQ(result.tokens[0].int_value, std::int64_t { 42 });
    FIRE_CHECK_EQ(result.tokens[1].int_value, std::int64_t { 255 });
    FIRE_CHECK_EQ(result.tokens[2].int_value, std::int64_t { 10 });
    FIRE_CHECK_EQ(result.tokens[3].int_value, std::int64_t { 15 });
    FIRE_CHECK_EQ(result.tokens[4].int_value, std::int64_t { 1000000 });
}

FIRE_TEST(lexer, integer_overflow_is_reported)
{
    const Scan result = scan("99999999999999999999");
    FIRE_CHECK(contains(result.diagnostics, "E0005"));
    FIRE_CHECK(contains(result.diagnostics, "out of range"));
}

FIRE_TEST(lexer, float_forms)
{
    const Scan result = scan("1.5 1e3 2.5e-2 7.");
    FIRE_CHECK(result.tokens[0].is(TokenType::FloatLiteral));
    FIRE_CHECK_EQ(result.tokens[0].float_value, 1.5);
    FIRE_CHECK_EQ(result.tokens[1].float_value, 1000.0);
    FIRE_CHECK_EQ(result.tokens[2].float_value, 0.025);
    // `7.` is an int followed by a stray `.`, not a float: a `.` only starts a
    // fraction when a digit follows it.
    FIRE_CHECK(result.tokens[3].is(TokenType::IntLiteral));
}

FIRE_TEST(lexer, range_operator_survives_next_to_integers)
{
    const Scan result = scan("0..10");
    FIRE_CHECK(result.tokens[0].is(TokenType::IntLiteral));
    FIRE_CHECK(result.tokens[1].is(TokenType::DotDot));
    FIRE_CHECK(result.tokens[2].is(TokenType::IntLiteral));
    FIRE_CHECK_EQ(result.tokens[2].int_value, std::int64_t { 10 });
}

FIRE_TEST(lexer, string_escapes_are_decoded)
{
    const Scan result = scan(R"("a\nb\t\"c\\\x41\u{1F525}")");
    FIRE_CHECK(result.tokens[0].is(TokenType::StringLiteral));
    FIRE_CHECK_EQ(result.tokens[0].text, std::string { "a\nb\t\"c\\A\xF0\x9F\x94\xA5" });
}

FIRE_TEST(lexer, unterminated_string_is_reported)
{
    const Scan result = scan("\"open");
    FIRE_CHECK(contains(result.diagnostics, "E0008"));
}

FIRE_TEST(lexer, block_comments_nest)
{
    const Scan result = scan("1 /* outer /* inner */ still a comment */ 2");
    FIRE_CHECK_EQ(result.tokens.size(), std::size_t { 3 }); // 1, 2, EOF
    FIRE_CHECK_EQ(result.tokens[1].int_value, std::int64_t { 2 });
}

FIRE_TEST(lexer, unterminated_block_comment_is_reported)
{
    const Scan result = scan("/* /* */");
    FIRE_CHECK(contains(result.diagnostics, "E0004"));
}

FIRE_TEST(lexer, maximal_munch_on_operators)
{
    const Scan result = scan("< << <= - -> -= > >> >= == = ! != && & || | ^ ~ %= *= /=");
    const std::vector<TokenType> expected {
        TokenType::Less,
        TokenType::LessLess,
        TokenType::LessEqual,
        TokenType::Minus,
        TokenType::Arrow,
        TokenType::MinusAssign,
        TokenType::Greater,
        TokenType::GreaterGreater,
        TokenType::GreaterEqual,
        TokenType::EqualEqual,
        TokenType::Assign,
        TokenType::Bang,
        TokenType::BangEqual,
        TokenType::AmpAmp,
        TokenType::Amp,
        TokenType::PipePipe,
        TokenType::Pipe,
        TokenType::Caret,
        TokenType::Tilde,
        TokenType::PercentAssign,
        TokenType::StarAssign,
        TokenType::SlashAssign,
    };
    FIRE_CHECK_EQ(result.tokens.size(), expected.size() + 1);
    for (std::size_t i = 0; i < expected.size() && i < result.tokens.size(); ++i) {
        FIRE_CHECK_EQ(std::string { token_type_name(result.tokens[i].type) },
            std::string { token_type_name(expected[i]) });
    }
}

FIRE_TEST(lexer, unknown_bytes_collapse_into_one_diagnostic)
{
    const Scan result = scan("@@@@@@@@");
    std::size_t occurrences = 0;
    for (std::size_t at = result.diagnostics.find("E0010"); at != std::string::npos;
        at = result.diagnostics.find("E0010", at + 1)) {
        ++occurrences;
    }
    FIRE_CHECK_EQ(occurrences, std::size_t { 1 });
}

FIRE_TEST(lexer, spans_point_at_the_token)
{
    const Scan result = scan("let x = 12345;");
    const Token& literal = result.tokens[3];
    FIRE_CHECK_EQ(literal.span.begin, std::uint32_t { 8 });
    FIRE_CHECK_EQ(literal.span.end, std::uint32_t { 13 });
}
