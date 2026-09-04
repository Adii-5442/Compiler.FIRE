// SPDX-License-Identifier: MIT
//
// The Fire abstract syntax tree.
//
// Nodes are a tagged hierarchy rather than a visitor: each pass is one switch
// over `kind`, which keeps the passes readable and adding a node a compile
// error everywhere it must be handled. Nodes own their children through
// unique_ptr, so a Program frees its entire tree.
//
// Two fields are written after parsing. Semantic analysis fills in `Expr::type`
// and the storage/target resolutions on names and calls; code generation only
// reads them. That is why the parser can stay purely syntactic.
#pragma once

#include "fire/source.hpp"
#include "fire/type.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fire {

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------

enum class UnaryOp : std::uint8_t {
    Negate,     // -x
    Not,        // !x
    BitNot,     // ~x
};

enum class BinaryOp : std::uint8_t {
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    BitAnd,
    BitOr,
    BitXor,
    ShiftLeft,
    ShiftRight,
};

enum class LogicalOp : std::uint8_t {
    And, // &&
    Or,  // ||
};

[[nodiscard]] const char* unary_op_spelling(UnaryOp op);
[[nodiscard]] const char* binary_op_spelling(BinaryOp op);
[[nodiscard]] const char* logical_op_spelling(LogicalOp op);
/// True for operators that always yield `bool` regardless of operand type.
[[nodiscard]] bool is_comparison(BinaryOp op);
/// True for operators defined only on `int`.
[[nodiscard]] bool is_bitwise(BinaryOp op);

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

enum class ExprKind : std::uint8_t {
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    BoolLiteral,
    ArrayLiteral,
    Name,
    Unary,
    Binary,
    Logical,
    Call,
    Index,
};

struct Expr {
    ExprKind kind;
    Span span;
    /// Filled by semantic analysis; null until then.
    const Type* type = nullptr;

    virtual ~Expr() = default;

protected:
    Expr(ExprKind k, Span s)
        : kind(k)
        , span(s)
    {
    }
};

using ExprPtr = std::unique_ptr<Expr>;

struct IntLiteralExpr : Expr {
    std::int64_t value = 0;
    IntLiteralExpr(Span s, std::int64_t v)
        : Expr(ExprKind::IntLiteral, s)
        , value(v)
    {
    }
};

struct FloatLiteralExpr : Expr {
    double value = 0.0;
    FloatLiteralExpr(Span s, double v)
        : Expr(ExprKind::FloatLiteral, s)
        , value(v)
    {
    }
};

struct StringLiteralExpr : Expr {
    std::string value;
    StringLiteralExpr(Span s, std::string v)
        : Expr(ExprKind::StringLiteral, s)
        , value(std::move(v))
    {
    }
};

struct BoolLiteralExpr : Expr {
    bool value = false;
    BoolLiteralExpr(Span s, bool v)
        : Expr(ExprKind::BoolLiteral, s)
        , value(v)
    {
    }
};

struct ArrayLiteralExpr : Expr {
    std::vector<ExprPtr> elements;
    explicit ArrayLiteralExpr(Span s)
        : Expr(ExprKind::ArrayLiteral, s)
    {
    }
};

/// Where a resolved name lives at run time.
enum class Storage : std::uint8_t {
    Unresolved,
    Local,  ///< slot in the current call frame
    Global, ///< slot in the VM's global array
};

struct NameExpr : Expr {
    std::string name;
    Storage storage = Storage::Unresolved;
    std::uint32_t slot = 0;
    NameExpr(Span s, std::string n)
        : Expr(ExprKind::Name, s)
        , name(std::move(n))
    {
    }
};

struct UnaryExpr : Expr {
    UnaryOp op;
    ExprPtr operand;
    UnaryExpr(Span s, UnaryOp o, ExprPtr e)
        : Expr(ExprKind::Unary, s)
        , op(o)
        , operand(std::move(e))
    {
    }
};

struct BinaryExpr : Expr {
    BinaryOp op;
    Span op_span;
    ExprPtr left;
    ExprPtr right;
    BinaryExpr(Span s, BinaryOp o, Span os, ExprPtr l, ExprPtr r)
        : Expr(ExprKind::Binary, s)
        , op(o)
        , op_span(os)
        , left(std::move(l))
        , right(std::move(r))
    {
    }
};

/// `&&` and `||` are separate from Binary because they do not evaluate their
/// right operand unconditionally, and so compile to jumps rather than to an
/// arithmetic opcode.
struct LogicalExpr : Expr {
    LogicalOp op;
    ExprPtr left;
    ExprPtr right;
    LogicalExpr(Span s, LogicalOp o, ExprPtr l, ExprPtr r)
        : Expr(ExprKind::Logical, s)
        , op(o)
        , left(std::move(l))
        , right(std::move(r))
    {
    }
};

/// What a call resolved to. Fire has no first-class functions, so a callee is
/// always a name known at compile time.
enum class CallTarget : std::uint8_t {
    Unresolved,
    UserFunction,
    Native,
};

struct CallExpr : Expr {
    std::string callee;
    Span callee_span;
    std::vector<ExprPtr> arguments;
    CallTarget target = CallTarget::Unresolved;
    std::uint32_t index = 0;
    CallExpr(Span s, std::string name, Span name_span)
        : Expr(ExprKind::Call, s)
        , callee(std::move(name))
        , callee_span(name_span)
    {
    }
};

struct IndexExpr : Expr {
    ExprPtr target;
    ExprPtr index;
    IndexExpr(Span s, ExprPtr t, ExprPtr i)
        : Expr(ExprKind::Index, s)
        , target(std::move(t))
        , index(std::move(i))
    {
    }
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

enum class StmtKind : std::uint8_t {
    VarDecl,
    ExprStmt,
    Assign,
    Block,
    If,
    While,
    ForRange,
    ForIn,
    Break,
    Continue,
    Return,
};

struct Stmt {
    StmtKind kind;
    Span span;

    virtual ~Stmt() = default;

protected:
    Stmt(StmtKind k, Span s)
        : kind(k)
        , span(s)
    {
    }
};

using StmtPtr = std::unique_ptr<Stmt>;

struct VarDeclStmt : Stmt {
    std::string name;
    Span name_span;
    bool is_const = false;
    /// Explicit annotation, or null when the type is inferred from `init`.
    const Type* annotation = nullptr;
    ExprPtr init;
    // Resolved storage.
    Storage storage = Storage::Unresolved;
    std::uint32_t slot = 0;
    VarDeclStmt(Span s, std::string n, Span ns, bool constant)
        : Stmt(StmtKind::VarDecl, s)
        , name(std::move(n))
        , name_span(ns)
        , is_const(constant)
    {
    }
};

struct ExprStmt : Stmt {
    ExprPtr expr;
    ExprStmt(Span s, ExprPtr e)
        : Stmt(StmtKind::ExprStmt, s)
        , expr(std::move(e))
    {
    }
};

struct AssignStmt : Stmt {
    /// A NameExpr or an IndexExpr; anything else is rejected by the parser.
    ExprPtr target;
    /// Set for compound assignment: `x += 1` carries BinaryOp::Add.
    std::optional<BinaryOp> compound;
    Span op_span;
    ExprPtr value;
    AssignStmt(Span s, ExprPtr t, std::optional<BinaryOp> c, Span os, ExprPtr v)
        : Stmt(StmtKind::Assign, s)
        , target(std::move(t))
        , compound(c)
        , op_span(os)
        , value(std::move(v))
    {
    }
};

struct BlockStmt : Stmt {
    std::vector<StmtPtr> statements;
    explicit BlockStmt(Span s)
        : Stmt(StmtKind::Block, s)
    {
    }
};

struct IfStmt : Stmt {
    ExprPtr condition;
    StmtPtr then_branch;
    /// `elif` chains desugar to a nested IfStmt in this slot.
    StmtPtr else_branch;
    explicit IfStmt(Span s)
        : Stmt(StmtKind::If, s)
    {
    }
};

struct WhileStmt : Stmt {
    ExprPtr condition;
    StmtPtr body;
    explicit WhileStmt(Span s)
        : Stmt(StmtKind::While, s)
    {
    }
};

/// `for i in a..b { }` — the loop variable is an `int` scoped to the body.
struct ForRangeStmt : Stmt {
    std::string var;
    Span var_span;
    ExprPtr start;
    ExprPtr end;
    StmtPtr body;
    std::uint32_t slot = 0;
    /// Hidden slot holding the (evaluated once) end of the range.
    std::uint32_t limit_slot = 0;
    explicit ForRangeStmt(Span s)
        : Stmt(StmtKind::ForRange, s)
    {
    }
};

/// `for x in xs { }` over an array.
struct ForInStmt : Stmt {
    std::string var;
    Span var_span;
    ExprPtr iterable;
    StmtPtr body;
    std::uint32_t slot = 0;
    /// Hidden slots holding the sequence and the cursor.
    std::uint32_t seq_slot = 0;
    std::uint32_t index_slot = 0;
    explicit ForInStmt(Span s)
        : Stmt(StmtKind::ForIn, s)
    {
    }
};

struct BreakStmt : Stmt {
    explicit BreakStmt(Span s)
        : Stmt(StmtKind::Break, s)
    {
    }
};

struct ContinueStmt : Stmt {
    explicit ContinueStmt(Span s)
        : Stmt(StmtKind::Continue, s)
    {
    }
};

struct ReturnStmt : Stmt {
    /// Null for a bare `return;`.
    ExprPtr value;
    explicit ReturnStmt(Span s)
        : Stmt(StmtKind::Return, s)
    {
    }
};

// ---------------------------------------------------------------------------
// Declarations and the program
// ---------------------------------------------------------------------------

struct Param {
    std::string name;
    Span span;
    const Type* type = nullptr;
    std::uint32_t slot = 0;
};

struct FunctionDecl {
    std::string name;
    Span name_span;
    Span span;
    std::vector<Param> params;
    const Type* return_type = nullptr;
    std::unique_ptr<BlockStmt> body;
    /// Frame size, computed by semantic analysis.
    std::uint32_t local_count = 0;
    /// Index in Program::functions, used by CallExpr.
    std::uint32_t index = 0;
};

/// A parsed file: its functions (visible anywhere, so order does not matter)
/// and its top-level statements, which run in order.
struct Program {
    std::vector<std::unique_ptr<FunctionDecl>> functions;
    std::vector<StmtPtr> top_level;
    /// File-scope `let`/`const` count; these live in the VM's global array.
    std::uint32_t global_count = 0;
    /// Frame size of the implicit script function.
    std::uint32_t script_local_count = 0;
};

} // namespace fire
