// SPDX-License-Identifier: MIT
#include "fire/driver.hpp"

#include "fire/codegen.hpp"
#include "fire/lexer.hpp"
#include "fire/natives.hpp"
#include "fire/parser.hpp"

#include <iomanip>
#include <ostream>

namespace fire {

Compilation::Compilation(SourceFile source, bool color)
    : m_source(std::move(source))
    , m_diagnostics(m_source, color)
{
}

bool Compilation::analyze()
{
    if (m_analyzed) {
        return !m_diagnostics.has_errors();
    }
    m_analyzed = true;

    Lexer lexer { m_source, m_diagnostics };
    m_tokens = lexer.tokenize();

    Parser parser { m_tokens, m_diagnostics, m_types };
    m_program = parser.parse_program();

    // Analysis still runs after a syntax error: the tree is a valid, if
    // partial, program, and type errors in the parts that did parse are worth
    // reporting in the same run.
    Analyzer analyzer { m_diagnostics, m_types };
    analyzer.analyze(m_program);

    return !m_diagnostics.has_errors();
}

bool Compilation::compile()
{
    if (!analyze()) {
        return false;
    }
    CodeGenerator generator;
    m_module = generator.generate(m_program);
    return true;
}

void Compilation::report(std::ostream& out) const { m_diagnostics.render(out); }

// ---------------------------------------------------------------------------
// Debug dumps
// ---------------------------------------------------------------------------

void dump_tokens(std::ostream& out, const SourceFile& source, const std::vector<Token>& tokens)
{
    out << std::left << std::setw(10) << "LINE:COL" << std::setw(10) << "KIND"
        << std::setw(22) << "TOKEN" << "VALUE\n";
    out << std::string(60, '-') << '\n';
    for (const Token& token : tokens) {
        const LineCol at = source.locate(token.span.begin);
        out << std::left << std::setw(10) << (std::to_string(at.line) + ':' + std::to_string(at.column))
            << std::setw(10) << token_type_tag(token.type) << std::setw(22)
            << token_type_name(token.type);
        switch (token.type) {
        case TokenType::Identifier:
        case TokenType::StringLiteral:
            out << '"' << token.text << '"';
            break;
        case TokenType::IntLiteral:
            out << token.int_value;
            break;
        case TokenType::FloatLiteral:
            out << format_float(token.float_value);
            break;
        default:
            break;
        }
        out << '\n';
    }
}

namespace {

    void indent(std::ostream& out, int depth) { out << std::string(static_cast<std::size_t>(depth) * 2, ' '); }

    void dump_expr(std::ostream& out, const Expr& expr, int depth);

    void dump_stmt(std::ostream& out, const Stmt& statement, int depth);

    void dump_type(std::ostream& out, const Type* type)
    {
        out << " :" << (type != nullptr ? type->to_string() : std::string { "?" });
    }

    void dump_expr(std::ostream& out, const Expr& expr, int depth)
    {
        indent(out, depth);
        switch (expr.kind) {
        case ExprKind::IntLiteral:
            out << "(int " << static_cast<const IntLiteralExpr&>(expr).value;
            break;
        case ExprKind::FloatLiteral:
            out << "(float " << format_float(static_cast<const FloatLiteralExpr&>(expr).value);
            break;
        case ExprKind::StringLiteral:
            out << "(str " << Value::string(static_cast<const StringLiteralExpr&>(expr).value).to_repr();
            break;
        case ExprKind::BoolLiteral:
            out << "(bool " << (static_cast<const BoolLiteralExpr&>(expr).value ? "true" : "false");
            break;
        case ExprKind::Name: {
            const auto& name = static_cast<const NameExpr&>(expr);
            out << "(name " << name.name << ' '
                << (name.storage == Storage::Global ? "global#" : "local#") << name.slot;
            break;
        }
        case ExprKind::ArrayLiteral: {
            const auto& array = static_cast<const ArrayLiteralExpr&>(expr);
            out << "(array";
            dump_type(out, expr.type);
            out << '\n';
            for (const ExprPtr& element : array.elements) {
                dump_expr(out, *element, depth + 1);
            }
            indent(out, depth);
            out << ")\n";
            return;
        }
        case ExprKind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            out << "(unary " << unary_op_spelling(unary.op);
            dump_type(out, expr.type);
            out << '\n';
            dump_expr(out, *unary.operand, depth + 1);
            indent(out, depth);
            out << ")\n";
            return;
        }
        case ExprKind::Binary: {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            out << "(binary " << binary_op_spelling(binary.op);
            dump_type(out, expr.type);
            out << '\n';
            dump_expr(out, *binary.left, depth + 1);
            dump_expr(out, *binary.right, depth + 1);
            indent(out, depth);
            out << ")\n";
            return;
        }
        case ExprKind::Logical: {
            const auto& logical = static_cast<const LogicalExpr&>(expr);
            out << "(logical " << logical_op_spelling(logical.op) << '\n';
            dump_expr(out, *logical.left, depth + 1);
            dump_expr(out, *logical.right, depth + 1);
            indent(out, depth);
            out << ")\n";
            return;
        }
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            out << "(call " << call.callee << ' '
                << (call.target == CallTarget::Native ? "native" : "fn") << '#' << call.index;
            dump_type(out, expr.type);
            out << '\n';
            for (const ExprPtr& argument : call.arguments) {
                dump_expr(out, *argument, depth + 1);
            }
            indent(out, depth);
            out << ")\n";
            return;
        }
        case ExprKind::Index: {
            const auto& index = static_cast<const IndexExpr&>(expr);
            out << "(index";
            dump_type(out, expr.type);
            out << '\n';
            dump_expr(out, *index.target, depth + 1);
            dump_expr(out, *index.index, depth + 1);
            indent(out, depth);
            out << ")\n";
            return;
        }
        }
        dump_type(out, expr.type);
        out << ")\n";
    }

    void dump_body(std::ostream& out, const Stmt* statement, int depth)
    {
        if (statement != nullptr) {
            dump_stmt(out, *statement, depth);
        }
    }

    void dump_stmt(std::ostream& out, const Stmt& statement, int depth)
    {
        indent(out, depth);
        switch (statement.kind) {
        case StmtKind::VarDecl: {
            const auto& declaration = static_cast<const VarDeclStmt&>(statement);
            out << '(' << (declaration.is_const ? "const " : "let ") << declaration.name << ' '
                << (declaration.storage == Storage::Global ? "global#" : "local#")
                << declaration.slot << '\n';
            dump_expr(out, *declaration.init, depth + 1);
            break;
        }
        case StmtKind::ExprStmt:
            out << "(expr\n";
            dump_expr(out, *static_cast<const ExprStmt&>(statement).expr, depth + 1);
            break;
        case StmtKind::Assign: {
            const auto& assign = static_cast<const AssignStmt&>(statement);
            out << "(assign"
                << (assign.compound.has_value() ? std::string { " " }
                            + binary_op_spelling(*assign.compound) + "="
                                                : std::string {})
                << '\n';
            dump_expr(out, *assign.target, depth + 1);
            dump_expr(out, *assign.value, depth + 1);
            break;
        }
        case StmtKind::Block: {
            out << "(block\n";
            for (const StmtPtr& child : static_cast<const BlockStmt&>(statement).statements) {
                dump_body(out, child.get(), depth + 1);
            }
            break;
        }
        case StmtKind::If: {
            const auto& branch = static_cast<const IfStmt&>(statement);
            out << "(if\n";
            dump_expr(out, *branch.condition, depth + 1);
            dump_body(out, branch.then_branch.get(), depth + 1);
            dump_body(out, branch.else_branch.get(), depth + 1);
            break;
        }
        case StmtKind::While: {
            const auto& loop = static_cast<const WhileStmt&>(statement);
            out << "(while\n";
            dump_expr(out, *loop.condition, depth + 1);
            dump_body(out, loop.body.get(), depth + 1);
            break;
        }
        case StmtKind::ForRange: {
            const auto& loop = static_cast<const ForRangeStmt&>(statement);
            out << "(for-range " << loop.var << " local#" << loop.slot << '\n';
            dump_expr(out, *loop.start, depth + 1);
            dump_expr(out, *loop.end, depth + 1);
            dump_body(out, loop.body.get(), depth + 1);
            break;
        }
        case StmtKind::ForIn: {
            const auto& loop = static_cast<const ForInStmt&>(statement);
            out << "(for-in " << loop.var << " local#" << loop.slot << '\n';
            dump_expr(out, *loop.iterable, depth + 1);
            dump_body(out, loop.body.get(), depth + 1);
            break;
        }
        case StmtKind::Break:
            out << "(break)\n";
            return;
        case StmtKind::Continue:
            out << "(continue)\n";
            return;
        case StmtKind::Return: {
            const auto& returned = static_cast<const ReturnStmt&>(statement);
            out << "(return\n";
            if (returned.value) {
                dump_expr(out, *returned.value, depth + 1);
            }
            break;
        }
        }
        indent(out, depth);
        out << ")\n";
    }

} // namespace

void dump_ast(std::ostream& out, const Program& program)
{
    out << "(program globals=" << program.global_count
        << " script-slots=" << program.script_local_count << '\n';
    for (const std::unique_ptr<FunctionDecl>& function : program.functions) {
        indent(out, 1);
        out << "(fn " << function->name << " (";
        for (std::size_t i = 0; i < function->params.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << function->params[i].name << ": " << function->params[i].type->to_string();
        }
        out << ") -> " << function->return_type->to_string() << " slots=" << function->local_count
            << '\n';
        for (const StmtPtr& statement : function->body->statements) {
            if (statement) {
                dump_stmt(out, *statement, 2);
            }
        }
        indent(out, 1);
        out << ")\n";
    }
    for (const StmtPtr& statement : program.top_level) {
        if (statement) {
            dump_stmt(out, *statement, 1);
        }
    }
    out << ")\n";
}

} // namespace fire
