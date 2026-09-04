// SPDX-License-Identifier: MIT
//
// Semantic analysis: name resolution, storage assignment and type checking.
//
// This is the pass that turns a syntax tree into something a backend can walk
// without asking questions. Afterwards every expression has a type, every name
// knows whether it is a local slot or a global slot and which one, every call
// knows whether it reaches a user function or a builtin, and every function
// knows how large its frame is.
//
// Errors never stop the walk. An expression whose type could not be determined
// takes the poison `Error` type, and operators accept poison silently, so one
// mistake produces one diagnostic.
#pragma once

#include "fire/ast.hpp"
#include "fire/diagnostics.hpp"
#include "fire/type.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace fire {

class Analyzer {
public:
    Analyzer(DiagnosticEngine& diagnostics, TypeContext& types)
        : m_diagnostics(&diagnostics)
        , m_types(types)
    {
    }

    /// Point the analyzer at a different diagnostic engine. The REPL keeps one
    /// analyzer alive across fragments but each fragment has its own source
    /// file, and therefore its own engine.
    void set_diagnostics(DiagnosticEngine& diagnostics) { m_diagnostics = &diagnostics; }

    /// Analyse a whole program in place. Returns false if any error was
    /// reported; the tree is still fully formed either way.
    bool analyze(Program& program);

    /// Analyse a REPL fragment against the globals accumulated so far. The
    /// analyzer keeps its global scope between calls, so `let x = 1;` on one
    /// line is visible on the next.
    bool analyze_fragment(Program& program);

    /// Number of globals allocated so far, across every fragment.
    [[nodiscard]] std::uint32_t global_count() const { return m_global_count; }

private:
    struct Variable {
        std::string name;
        const Type* type = nullptr;
        bool is_const = false;
        Storage storage = Storage::Local;
        std::uint32_t slot = 0;
        Span span;
        bool used = false;
        /// Parameters are never reported as unused.
        bool is_parameter = false;
    };

    struct Scope {
        std::vector<Variable> variables;
        /// Frame cursor on entry, restored on exit so sibling scopes share
        /// slots.
        std::uint32_t saved_next_slot = 0;
    };

    /// Per-function state: frame layout and what `return`/`break` mean here.
    struct FunctionContext {
        const FunctionDecl* declaration = nullptr; ///< null inside the script body
        const Type* return_type = nullptr;
        std::uint32_t next_slot = 0;
        std::uint32_t high_water = 0;
        int loop_depth = 0;
    };

    // -- scopes and variables ----------------------------------------------
    void push_scope();
    void pop_scope();
    [[nodiscard]] bool at_global_scope() const { return m_scopes.size() == 1; }
    Variable* declare(const std::string& name, const Type* type, bool is_const, Span span,
        bool is_parameter = false);
    Variable* lookup(const std::string& name);
    /// Allocate a compiler-introduced slot (loop cursors, and so on).
    std::uint32_t allocate_hidden_slot();

    // -- statements ---------------------------------------------------------
    void analyze_statement(Stmt& statement);
    void analyze_var_decl(VarDeclStmt& statement);
    void analyze_assign(AssignStmt& statement);
    void analyze_block(BlockStmt& block, bool own_scope = true);
    void analyze_if(IfStmt& statement);
    void analyze_while(WhileStmt& statement);
    void analyze_for_range(ForRangeStmt& statement);
    void analyze_for_in(ForInStmt& statement);
    void analyze_return(ReturnStmt& statement);
    void analyze_function(FunctionDecl& function);

    // -- expressions --------------------------------------------------------
    /// `expected` is a hint used to type otherwise-ambiguous literals such as
    /// the empty array. Never used to coerce.
    const Type* analyze_expression(Expr& expr, const Type* expected = nullptr);
    const Type* analyze_array_literal(ArrayLiteralExpr& expr, const Type* expected);
    const Type* analyze_name(NameExpr& expr);
    const Type* analyze_unary(UnaryExpr& expr);
    const Type* analyze_binary(BinaryExpr& expr);
    const Type* analyze_logical(LogicalExpr& expr);
    const Type* analyze_call(CallExpr& expr);
    const Type* analyze_index(IndexExpr& expr);

    // -- helpers ------------------------------------------------------------
    void require_bool(Expr& expr, const char* construct);
    /// Emits E0203 unless `actual` is assignable to `expected`.
    void expect_type(const Type* expected, const Type* actual, Span span, const std::string& what);
    /// Closest known name to `name`, for "did you mean" hints.
    [[nodiscard]] std::string suggest_name(const std::string& name, bool functions) const;
    /// Conservative "control cannot fall off the end of this statement".
    [[nodiscard]] static bool always_diverges(const Stmt* statement);
    void warn_unreachable(const std::vector<StmtPtr>& statements);

    /// The engine in use for the fragment being analysed.
    [[nodiscard]] DiagnosticEngine& diags() const { return *m_diagnostics; }

    DiagnosticEngine* m_diagnostics;
    TypeContext& m_types;

    std::vector<Scope> m_scopes;
    std::vector<FunctionContext> m_functions;
    std::unordered_map<std::string, FunctionDecl*> m_function_table;
    std::uint32_t m_global_count = 0;
};

} // namespace fire
