// SPDX-License-Identifier: MIT
//
// Bytecode generation: analysed AST in, Module out.
//
// A single walk with no intermediate representation. Fire's semantics map onto
// the instruction set closely enough that an IR would add a translation step
// without buying an optimisation the language needs.
//
// Everything this pass needs to know was decided by semantic analysis: slots
// are assigned, call targets resolved, types attached. Codegen therefore never
// reports a diagnostic — it cannot fail on well-formed input, and running it
// on ill-formed input is a bug in the driver, not a user error.
#pragma once

#include "fire/ast.hpp"
#include "fire/chunk.hpp"

#include <cstdint>
#include <vector>

namespace fire {

class CodeGenerator {
public:
    /// Compile an analysed program. `program` must have passed `Analyzer`.
    [[nodiscard]] Module generate(const Program& program);

private:
    /// Where `break` and `continue` should jump inside the current loop.
    struct LoopContext {
        /// Jump operands to patch with the loop's exit once it is known.
        std::vector<std::size_t> break_patches;
        /// Jump operands to patch with the loop's continue target. `for`
        /// continues at the increment, which is only emitted after the body,
        /// so these are collected rather than resolved on the spot.
        std::vector<std::size_t> continue_patches;
    };

    /// Patch every break to `exit_target` and every continue to
    /// `continue_target`, then leave the loop.
    void close_loop(std::uint32_t continue_target, std::uint32_t exit_target);

    // -- emit helpers -------------------------------------------------------
    Chunk& chunk() { return *m_chunk; }
    void emit(OpCode op, Span span);
    void emit_constant(const Value& value, Span span);
    /// Emit a jump with a placeholder target; returns the operand offset.
    [[nodiscard]] std::size_t emit_jump(OpCode op, Span span);
    void patch_to_here(std::size_t operand_offset);
    void patch_to(std::size_t operand_offset, std::uint32_t target);
    [[nodiscard]] std::uint32_t here() const;

    // -- statements ---------------------------------------------------------
    void gen_statement(const Stmt& statement);
    void gen_var_decl(const VarDeclStmt& statement);
    void gen_assign(const AssignStmt& statement);
    void gen_if(const IfStmt& statement);
    void gen_while(const WhileStmt& statement);
    void gen_for_range(const ForRangeStmt& statement);
    void gen_for_in(const ForInStmt& statement);
    void gen_return(const ReturnStmt& statement);
    void gen_block(const BlockStmt& block);

    // -- expressions --------------------------------------------------------
    void gen_expression(const Expr& expr);
    void gen_binary(const BinaryExpr& expr);
    void gen_logical(const LogicalExpr& expr);
    void gen_call(const CallExpr& expr);

    /// Store to the slot a name or declaration resolved to.
    void emit_store(Storage storage, std::uint32_t slot, Span span);
    void emit_load(Storage storage, std::uint32_t slot, Span span);
    /// Pick the opcode for `op` given the type its operands have.
    [[nodiscard]] static OpCode arithmetic_opcode(BinaryOp op, const Type* operand);

    Chunk* m_chunk = nullptr;
    std::vector<LoopContext> m_loops;
};

} // namespace fire
