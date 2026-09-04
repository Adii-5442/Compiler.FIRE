// SPDX-License-Identifier: MIT
#include "fire/parser.hpp"

#include <utility>

namespace fire {

const Token& Parser::peek(std::size_t offset) const
{
    const std::size_t at = m_index + offset;
    // The token vector always ends with EndOfFile, so clamping here means no
    // caller ever needs a bounds check.
    return m_tokens[at < m_tokens.size() ? at : m_tokens.size() - 1];
}

const Token& Parser::previous() const { return m_tokens[m_index > 0 ? m_index - 1 : 0]; }

const Token& Parser::advance()
{
    if (!at_end()) {
        ++m_index;
    }
    return previous();
}

bool Parser::match(TokenType type)
{
    if (!check(type)) {
        return false;
    }
    advance();
    return true;
}

void Parser::fail(
    const std::string& code, const std::string& message, Span span, const std::string& label)
{
    auto builder = m_diagnostics.error(code, message, span);
    if (!label.empty()) {
        builder.label(label);
    }
    throw ParseError {};
}

const Token& Parser::expect(TokenType type, const std::string& context)
{
    if (check(type)) {
        return advance();
    }
    fail("E0101",
        std::string { "expected `" } + token_type_name(type) + "` " + context + ", found `"
            + token_type_name(peek().type) + '`',
        peek().span, std::string { "expected `" } + token_type_name(type) + '`');
}

void Parser::synchronize()
{
    // Skip to a point where a fresh statement can plausibly begin. A consumed
    // `;` ends the broken statement; a statement keyword starts the next one.
    while (!at_end()) {
        if (previous().is(TokenType::Semicolon) || previous().is(TokenType::RBrace)) {
            return;
        }
        switch (peek().type) {
        case TokenType::KwFn:
        case TokenType::KwLet:
        case TokenType::KwConst:
        case TokenType::KwIf:
        case TokenType::KwWhile:
        case TokenType::KwFor:
        case TokenType::KwReturn:
        case TokenType::KwBreak:
        case TokenType::KwContinue:
        case TokenType::RBrace:
            return;
        default:
            advance();
            break;
        }
    }
}

std::optional<BinaryOp> Parser::compound_operator(TokenType type)
{
    switch (type) {
    case TokenType::PlusAssign:
        return BinaryOp::Add;
    case TokenType::MinusAssign:
        return BinaryOp::Subtract;
    case TokenType::StarAssign:
        return BinaryOp::Multiply;
    case TokenType::SlashAssign:
        return BinaryOp::Divide;
    case TokenType::PercentAssign:
        return BinaryOp::Modulo;
    default:
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// Program and declarations
// ---------------------------------------------------------------------------

Program Parser::parse_program()
{
    Program program;
    while (!at_end()) {
        if (m_diagnostics.at_error_limit()) {
            break;
        }
        try {
            if (check(TokenType::KwFn)) {
                if (auto function = parse_function()) {
                    function->index = static_cast<std::uint32_t>(program.functions.size());
                    program.functions.push_back(std::move(function));
                }
            } else if (StmtPtr statement = parse_statement()) {
                program.top_level.push_back(std::move(statement));
            }
        } catch (const ParseError&) {
            synchronize();
        }
    }
    return program;
}

const Type* Parser::parse_type()
{
    if (match(TokenType::LBracket)) {
        const Type* element = parse_type();
        expect(TokenType::RBracket, "to close an array type");
        return m_types.array_of(element);
    }
    switch (peek().type) {
    case TokenType::KwInt:
        advance();
        return m_types.int_type();
    case TokenType::KwFloat:
        advance();
        return m_types.float_type();
    case TokenType::KwBool:
        advance();
        return m_types.bool_type();
    case TokenType::KwStr:
        advance();
        return m_types.str_type();
    case TokenType::KwVoid:
        advance();
        return m_types.void_type();
    default:
        fail("E0102",
            std::string { "expected a type, found `" } + token_type_name(peek().type) + '`',
            peek().span, "expected `int`, `float`, `bool`, `str`, `void` or `[T]`");
    }
}

std::unique_ptr<FunctionDecl> Parser::parse_function()
{
    const Span start = peek().span;
    expect(TokenType::KwFn, "to begin a function declaration");

    auto function = std::make_unique<FunctionDecl>();
    const Token& name = expect(TokenType::Identifier, "after `fn`");
    function->name = name.text;
    function->name_span = name.span;

    expect(TokenType::LParen, "after the function name");
    if (!check(TokenType::RParen)) {
        do {
            const Token& param_name = expect(TokenType::Identifier, "in a parameter list");
            Param param;
            param.name = param_name.text;
            param.span = param_name.span;
            expect(TokenType::Colon,
                "after a parameter name (Fire requires parameter type annotations)");
            param.type = parse_type();
            function->params.push_back(std::move(param));
        } while (match(TokenType::Comma));
    }
    expect(TokenType::RParen, "to close the parameter list");

    // A missing `-> T` means the function returns nothing.
    function->return_type = match(TokenType::Arrow) ? parse_type() : m_types.void_type();

    function->body = parse_block("as a function body");
    function->span = start.merge(previous().span);
    return function;
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

std::unique_ptr<BlockStmt> Parser::parse_block(const std::string& context)
{
    const Token& open = expect(TokenType::LBrace, context);
    auto block = std::make_unique<BlockStmt>(open.span);
    while (!check(TokenType::RBrace) && !at_end()) {
        if (m_diagnostics.at_error_limit()) {
            break;
        }
        try {
            if (check(TokenType::KwFn)) {
                fail("E0103", "nested function declarations are not allowed", peek().span,
                    "move this function to the top level of the file");
            }
            if (StmtPtr statement = parse_statement()) {
                block->statements.push_back(std::move(statement));
            }
        } catch (const ParseError&) {
            synchronize();
        }
    }
    const Token& close = expect(TokenType::RBrace, "to close a block");
    block->span = open.span.merge(close.span);
    return block;
}

StmtPtr Parser::parse_statement()
{
    switch (peek().type) {
    case TokenType::KwLet:
        advance();
        return parse_var_decl(false);
    case TokenType::KwConst:
        advance();
        return parse_var_decl(true);
    case TokenType::KwIf:
        return parse_if();
    case TokenType::KwWhile:
        return parse_while();
    case TokenType::KwFor:
        return parse_for();
    case TokenType::KwReturn:
        return parse_return();
    case TokenType::KwBreak: {
        const Span span = advance().span;
        expect(TokenType::Semicolon, "after `break`");
        return std::make_unique<BreakStmt>(span);
    }
    case TokenType::KwContinue: {
        const Span span = advance().span;
        expect(TokenType::Semicolon, "after `continue`");
        return std::make_unique<ContinueStmt>(span);
    }
    case TokenType::LBrace:
        return parse_block("to begin a block");
    case TokenType::Semicolon:
        // A stray `;` is an empty statement; accept and drop it.
        advance();
        return nullptr;
    default:
        return parse_simple_statement();
    }
}

StmtPtr Parser::parse_var_decl(bool is_const)
{
    const Span start = previous().span;
    const Token& name = expect(TokenType::Identifier,
        is_const ? "after `const`" : "after `let`");

    auto decl = std::make_unique<VarDeclStmt>(start, name.text, name.span, is_const);
    if (match(TokenType::Colon)) {
        decl->annotation = parse_type();
    }
    if (!match(TokenType::Assign)) {
        fail("E0104", "a variable must be given an initial value", peek().span,
            "expected `=` here");
    }
    decl->init = parse_expression();
    const Token& semi = expect(TokenType::Semicolon, "after a variable declaration");
    decl->span = start.merge(semi.span);
    return decl;
}

StmtPtr Parser::parse_if()
{
    const Span start = advance().span; // `if` or `elif`
    auto statement = std::make_unique<IfStmt>(start);
    statement->condition = parse_expression();
    statement->then_branch = parse_block("as the body of `if`");

    if (check(TokenType::KwElif)) {
        // Desugar the chain: `elif c { }` becomes `else { if c { } }`, so no
        // later pass has to know that `elif` exists.
        statement->else_branch = parse_if();
    } else if (match(TokenType::KwElse)) {
        statement->else_branch = parse_block("as the body of `else`");
    }
    statement->span = start.merge(previous().span);
    return statement;
}

StmtPtr Parser::parse_while()
{
    const Span start = advance().span;
    auto statement = std::make_unique<WhileStmt>(start);
    statement->condition = parse_expression();
    statement->body = parse_block("as the body of `while`");
    statement->span = start.merge(previous().span);
    return statement;
}

StmtPtr Parser::parse_for()
{
    const Span start = advance().span;
    const Token& var = expect(TokenType::Identifier, "after `for`");
    expect(TokenType::KwIn, "after the loop variable");

    ExprPtr first = parse_expression();
    if (match(TokenType::DotDot)) {
        auto statement = std::make_unique<ForRangeStmt>(start);
        statement->var = var.text;
        statement->var_span = var.span;
        statement->start = std::move(first);
        statement->end = parse_expression();
        statement->body = parse_block("as the body of `for`");
        statement->span = start.merge(previous().span);
        return statement;
    }

    auto statement = std::make_unique<ForInStmt>(start);
    statement->var = var.text;
    statement->var_span = var.span;
    statement->iterable = std::move(first);
    statement->body = parse_block("as the body of `for`");
    statement->span = start.merge(previous().span);
    return statement;
}

StmtPtr Parser::parse_return()
{
    const Span start = advance().span;
    auto statement = std::make_unique<ReturnStmt>(start);
    if (!check(TokenType::Semicolon)) {
        statement->value = parse_expression();
    }
    const Token& semi = expect(TokenType::Semicolon, "after `return`");
    statement->span = start.merge(semi.span);
    return statement;
}

StmtPtr Parser::parse_simple_statement()
{
    const Span start = peek().span;
    ExprPtr expr = parse_expression();

    const std::optional<BinaryOp> compound = compound_operator(peek().type);
    if (check(TokenType::Assign) || compound.has_value()) {
        const Token& op = advance();
        if (expr->kind != ExprKind::Name && expr->kind != ExprKind::Index) {
            fail("E0105", "invalid assignment target", expr->span,
                "only a variable or an element such as `xs[i]` can be assigned to");
        }
        ExprPtr value = parse_expression();
        const Token& semi = expect(TokenType::Semicolon, "after an assignment");
        return std::make_unique<AssignStmt>(
            start.merge(semi.span), std::move(expr), compound, op.span, std::move(value));
    }

    const Token& semi = expect(TokenType::Semicolon, "after an expression statement");
    return std::make_unique<ExprStmt>(start.merge(semi.span), std::move(expr));
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

ExprPtr Parser::parse_expression() { return parse_logical_or(); }

ExprPtr Parser::parse_logical_or()
{
    ExprPtr left = parse_logical_and();
    while (match(TokenType::PipePipe)) {
        ExprPtr right = parse_logical_and();
        const Span span = left->span.merge(right->span);
        left = std::make_unique<LogicalExpr>(span, LogicalOp::Or, std::move(left), std::move(right));
    }
    return left;
}

ExprPtr Parser::parse_logical_and()
{
    ExprPtr left = parse_equality();
    while (match(TokenType::AmpAmp)) {
        ExprPtr right = parse_equality();
        const Span span = left->span.merge(right->span);
        left
            = std::make_unique<LogicalExpr>(span, LogicalOp::And, std::move(left), std::move(right));
    }
    return left;
}

// The remaining binary levels share this shape; each is spelled out rather
// than table-driven so that the precedence order is readable top to bottom.
#define FIRE_BINARY_LEVEL(name, next, ...)                                                         \
    ExprPtr Parser::name()                                                                         \
    {                                                                                              \
        ExprPtr left = next();                                                                     \
        while (true) {                                                                             \
            BinaryOp op {};                                                                        \
            switch (peek().type) {                                                                 \
                __VA_ARGS__                                                                        \
            default:                                                                               \
                return left;                                                                       \
            }                                                                                      \
            const Span op_span = advance().span;                                                   \
            ExprPtr right = next();                                                                \
            const Span span = left->span.merge(right->span);                                       \
            left = std::make_unique<BinaryExpr>(                                                    \
                span, op, op_span, std::move(left), std::move(right));                             \
        }                                                                                          \
    }

#define FIRE_CASE(token, binop)                                                                    \
    case TokenType::token:                                                                         \
        op = BinaryOp::binop;                                                                      \
        break;

FIRE_BINARY_LEVEL(parse_equality, parse_comparison, FIRE_CASE(EqualEqual, Equal)
        FIRE_CASE(BangEqual, NotEqual))

FIRE_BINARY_LEVEL(parse_comparison, parse_bit_or, FIRE_CASE(Less, Less) FIRE_CASE(LessEqual,
        LessEqual) FIRE_CASE(Greater, Greater) FIRE_CASE(GreaterEqual, GreaterEqual))

FIRE_BINARY_LEVEL(parse_bit_or, parse_bit_xor, FIRE_CASE(Pipe, BitOr))

FIRE_BINARY_LEVEL(parse_bit_xor, parse_bit_and, FIRE_CASE(Caret, BitXor))

FIRE_BINARY_LEVEL(parse_bit_and, parse_shift, FIRE_CASE(Amp, BitAnd))

FIRE_BINARY_LEVEL(parse_shift, parse_term, FIRE_CASE(LessLess, ShiftLeft)
        FIRE_CASE(GreaterGreater, ShiftRight))

FIRE_BINARY_LEVEL(parse_term, parse_factor, FIRE_CASE(Plus, Add) FIRE_CASE(Minus, Subtract))

FIRE_BINARY_LEVEL(parse_factor, parse_unary, FIRE_CASE(Star, Multiply) FIRE_CASE(Slash, Divide)
        FIRE_CASE(Percent, Modulo))

#undef FIRE_CASE
#undef FIRE_BINARY_LEVEL

ExprPtr Parser::parse_unary()
{
    UnaryOp op {};
    switch (peek().type) {
    case TokenType::Minus:
        op = UnaryOp::Negate;
        break;
    case TokenType::Bang:
        op = UnaryOp::Not;
        break;
    case TokenType::Tilde:
        op = UnaryOp::BitNot;
        break;
    default:
        return parse_postfix();
    }
    const Span start = advance().span;
    ExprPtr operand = parse_unary();
    const Span span = start.merge(operand->span);
    return std::make_unique<UnaryExpr>(span, op, std::move(operand));
}

ExprPtr Parser::parse_postfix()
{
    ExprPtr expr = parse_primary();
    while (check(TokenType::LBracket)) {
        advance();
        ExprPtr index = parse_expression();
        const Token& close = expect(TokenType::RBracket, "to close an index expression");
        const Span span = expr->span.merge(close.span);
        expr = std::make_unique<IndexExpr>(span, std::move(expr), std::move(index));
    }
    return expr;
}

ExprPtr Parser::parse_primary()
{
    const Token& token = peek();
    switch (token.type) {
    case TokenType::IntLiteral:
        advance();
        return std::make_unique<IntLiteralExpr>(token.span, token.int_value);
    case TokenType::FloatLiteral:
        advance();
        return std::make_unique<FloatLiteralExpr>(token.span, token.float_value);
    case TokenType::StringLiteral:
        advance();
        return std::make_unique<StringLiteralExpr>(token.span, token.text);
    case TokenType::KwTrue:
        advance();
        return std::make_unique<BoolLiteralExpr>(token.span, true);
    case TokenType::KwFalse:
        advance();
        return std::make_unique<BoolLiteralExpr>(token.span, false);

    case TokenType::LParen: {
        advance();
        ExprPtr inner = parse_expression();
        const Token& close = expect(TokenType::RParen, "to close a parenthesised expression");
        // Widen the span so diagnostics underline the parentheses too.
        inner->span = token.span.merge(close.span);
        return inner;
    }

    case TokenType::LBracket: {
        advance();
        auto array = std::make_unique<ArrayLiteralExpr>(token.span);
        if (!check(TokenType::RBracket)) {
            do {
                if (check(TokenType::RBracket)) {
                    break; // tolerate a trailing comma
                }
                array->elements.push_back(parse_expression());
            } while (match(TokenType::Comma));
        }
        const Token& close = expect(TokenType::RBracket, "to close an array literal");
        array->span = token.span.merge(close.span);
        return array;
    }

    case TokenType::Identifier: {
        advance();
        if (check(TokenType::LParen)) {
            advance();
            auto call = std::make_unique<CallExpr>(token.span, token.text, token.span);
            if (!check(TokenType::RParen)) {
                do {
                    if (check(TokenType::RParen)) {
                        break; // tolerate a trailing comma
                    }
                    call->arguments.push_back(parse_expression());
                } while (match(TokenType::Comma));
            }
            const Token& close = expect(TokenType::RParen, "to close an argument list");
            call->span = token.span.merge(close.span);
            return call;
        }
        return std::make_unique<NameExpr>(token.span, token.text);
    }

    default:
        break;
    }

    // `int(x)`, `float(x)`, `bool(x)` and `str(x)` are the conversion
    // builtins. Their names are also type keywords, so the lexer hands them
    // over as keywords and the call form has to be recognised here.
    if (is_type_keyword(token.type) && token.type != TokenType::KwVoid
        && peek(1).is(TokenType::LParen)) {
        advance();
        advance();
        auto call = std::make_unique<CallExpr>(token.span, token_type_name(token.type), token.span);
        if (!check(TokenType::RParen)) {
            do {
                if (check(TokenType::RParen)) {
                    break;
                }
                call->arguments.push_back(parse_expression());
            } while (match(TokenType::Comma));
        }
        const Token& close = expect(TokenType::RParen, "to close a conversion");
        call->span = token.span.merge(close.span);
        return call;
    }

    // A type keyword anywhere else in expression position is almost always a
    // conversion the programmer expected, so say so rather than "unexpected".
    if (is_type_keyword(token.type)) {
        fail("E0106",
            std::string { "`" } + token_type_name(token.type) + "` is a type, not a value",
            token.span,
            std::string { "to convert a value write `" } + token_type_name(token.type) + "(x)`");
    }
    fail("E0107",
        std::string { "expected an expression, found `" } + token_type_name(token.type) + '`',
        token.span, "expected a literal, a name, `(`, `[` or a unary operator");
}

} // namespace fire
