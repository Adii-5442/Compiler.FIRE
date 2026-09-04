// SPDX-License-Identifier: MIT
//
// Recursive-descent parser with precedence climbing for the binary operators.
//
// Recovery is panic mode: a syntax error throws internally, the statement is
// abandoned, and the parser resynchronises at the next `;` or statement
// keyword. That way one missing semicolon does not turn into forty errors, but
// a genuinely broken file still reports every independent mistake.
#pragma once

#include "fire/ast.hpp"
#include "fire/diagnostics.hpp"
#include "fire/token.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fire {

class Parser {
public:
    Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics, TypeContext& types)
        : m_tokens(std::move(tokens))
        , m_diagnostics(diagnostics)
        , m_types(types)
    {
    }

    /// Parse a whole file. Always returns a Program; check the diagnostic
    /// engine to find out whether it is trustworthy.
    [[nodiscard]] Program parse_program();

    /// Parse a fragment as if it were the body of a file. Used by the REPL.
    [[nodiscard]] Program parse_repl_fragment() { return parse_program(); }

private:
    /// Thrown on a syntax error and caught at the nearest statement boundary.
    struct ParseError { };

    // -- token access -------------------------------------------------------
    [[nodiscard]] const Token& peek(std::size_t offset = 0) const;
    [[nodiscard]] const Token& previous() const;
    [[nodiscard]] bool check(TokenType type) const { return peek().is(type); }
    [[nodiscard]] bool at_end() const { return peek().is(TokenType::EndOfFile); }
    const Token& advance();
    bool match(TokenType type);
    const Token& expect(TokenType type, const std::string& context);

    [[noreturn]] void fail(const std::string& code, const std::string& message, Span span,
        const std::string& label = {});
    void synchronize();

    // -- grammar ------------------------------------------------------------
    std::unique_ptr<FunctionDecl> parse_function();
    const Type* parse_type();

    StmtPtr parse_statement();
    StmtPtr parse_var_decl(bool is_const);
    StmtPtr parse_if();
    StmtPtr parse_while();
    StmtPtr parse_for();
    StmtPtr parse_return();
    StmtPtr parse_simple_statement();
    std::unique_ptr<BlockStmt> parse_block(const std::string& context);

    ExprPtr parse_expression();
    ExprPtr parse_logical_or();
    ExprPtr parse_logical_and();
    ExprPtr parse_equality();
    ExprPtr parse_comparison();
    ExprPtr parse_bit_or();
    ExprPtr parse_bit_xor();
    ExprPtr parse_bit_and();
    ExprPtr parse_shift();
    ExprPtr parse_term();
    ExprPtr parse_factor();
    ExprPtr parse_unary();
    ExprPtr parse_postfix();
    ExprPtr parse_primary();

    /// Maps an assignment token to the operator it compounds, if any.
    static std::optional<BinaryOp> compound_operator(TokenType type);

    std::vector<Token> m_tokens;
    DiagnosticEngine& m_diagnostics;
    TypeContext& m_types;
    std::size_t m_index = 0;
};

} // namespace fire
