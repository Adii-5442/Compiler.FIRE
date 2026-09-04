// SPDX-License-Identifier: MIT
#include "fire/codegen.hpp"

#include "fire/natives.hpp"

#include <cassert>

namespace fire {

// ---------------------------------------------------------------------------
// Emit helpers
// ---------------------------------------------------------------------------

void CodeGenerator::emit(OpCode op, Span span) { chunk().write_op(op, span); }

void CodeGenerator::emit_constant(const Value& value, Span span)
{
    const std::uint16_t index = chunk().add_constant(value);
    emit(OpCode::Constant, span);
    chunk().write_u16(index, span);
}

std::size_t CodeGenerator::emit_jump(OpCode op, Span span)
{
    emit(op, span);
    const std::size_t operand = chunk().size();
    chunk().write_u32(0xFFFFFFFF, span); // patched once the target is known
    return operand;
}

std::uint32_t CodeGenerator::here() const
{
    return static_cast<std::uint32_t>(m_chunk->size());
}

void CodeGenerator::patch_to_here(std::size_t operand_offset)
{
    chunk().patch_u32(operand_offset, here());
}

void CodeGenerator::patch_to(std::size_t operand_offset, std::uint32_t target)
{
    chunk().patch_u32(operand_offset, target);
}

void CodeGenerator::emit_store(Storage storage, std::uint32_t slot, Span span)
{
    emit(storage == Storage::Global ? OpCode::SetGlobal : OpCode::SetLocal, span);
    chunk().write_u16(static_cast<std::uint16_t>(slot), span);
}

void CodeGenerator::emit_load(Storage storage, std::uint32_t slot, Span span)
{
    emit(storage == Storage::Global ? OpCode::GetGlobal : OpCode::GetLocal, span);
    chunk().write_u16(static_cast<std::uint16_t>(slot), span);
}

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

Module CodeGenerator::generate(const Program& program)
{
    Module module;
    module.global_count = program.global_count;

    module.script.name = "<script>";
    module.script.arity = 0;
    module.script.local_count = program.script_local_count;
    m_chunk = &module.script.chunk;
    m_loops.clear();
    for (const StmtPtr& statement : program.top_level) {
        if (statement) {
            gen_statement(*statement);
        }
    }
    emit(OpCode::Halt, Span {});

    module.functions.reserve(program.functions.size());
    for (const std::unique_ptr<FunctionDecl>& function : program.functions) {
        CompiledFunction compiled;
        compiled.name = function->name;
        compiled.arity = static_cast<std::uint32_t>(function->params.size());
        compiled.local_count = function->local_count;
        compiled.span = function->name_span;

        m_chunk = &compiled.chunk;
        m_loops.clear();
        gen_block(*function->body);
        // Falling off the end of a `void` function is a normal return; sema
        // has already rejected it for any other return type.
        emit(OpCode::ReturnVoid, function->name_span);

        module.functions.push_back(std::move(compiled));
    }

    m_chunk = nullptr;
    return module;
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

void CodeGenerator::gen_statement(const Stmt& statement)
{
    switch (statement.kind) {
    case StmtKind::VarDecl:
        gen_var_decl(static_cast<const VarDeclStmt&>(statement));
        break;
    case StmtKind::ExprStmt: {
        const auto& expression = static_cast<const ExprStmt&>(statement);
        gen_expression(*expression.expr);
        // Every expression leaves exactly one value, `void` calls included, so
        // a statement always discards exactly one.
        emit(OpCode::Pop, statement.span);
        break;
    }
    case StmtKind::Assign:
        gen_assign(static_cast<const AssignStmt&>(statement));
        break;
    case StmtKind::Block:
        gen_block(static_cast<const BlockStmt&>(statement));
        break;
    case StmtKind::If:
        gen_if(static_cast<const IfStmt&>(statement));
        break;
    case StmtKind::While:
        gen_while(static_cast<const WhileStmt&>(statement));
        break;
    case StmtKind::ForRange:
        gen_for_range(static_cast<const ForRangeStmt&>(statement));
        break;
    case StmtKind::ForIn:
        gen_for_in(static_cast<const ForInStmt&>(statement));
        break;
    case StmtKind::Break: {
        assert(!m_loops.empty() && "break outside a loop should have been rejected by sema");
        m_loops.back().break_patches.push_back(emit_jump(OpCode::Jump, statement.span));
        break;
    }
    case StmtKind::Continue: {
        assert(!m_loops.empty() && "continue outside a loop should have been rejected by sema");
        m_loops.back().continue_patches.push_back(emit_jump(OpCode::Jump, statement.span));
        break;
    }
    case StmtKind::Return:
        gen_return(static_cast<const ReturnStmt&>(statement));
        break;
    }
}

void CodeGenerator::gen_block(const BlockStmt& block)
{
    for (const StmtPtr& statement : block.statements) {
        if (statement) {
            gen_statement(*statement);
        }
    }
}

void CodeGenerator::gen_var_decl(const VarDeclStmt& statement)
{
    gen_expression(*statement.init);
    emit_store(statement.storage, statement.slot, statement.span);
}

void CodeGenerator::gen_assign(const AssignStmt& statement)
{
    if (statement.target->kind == ExprKind::Name) {
        const auto& name = static_cast<const NameExpr&>(*statement.target);
        if (statement.compound.has_value()) {
            emit_load(name.storage, name.slot, name.span);
            gen_expression(*statement.value);
            emit(arithmetic_opcode(*statement.compound, name.type), statement.op_span);
        } else {
            gen_expression(*statement.value);
        }
        emit_store(name.storage, name.slot, statement.span);
        return;
    }

    const auto& index = static_cast<const IndexExpr&>(*statement.target);
    gen_expression(*index.target);
    gen_expression(*index.index);
    if (statement.compound.has_value()) {
        // Keep one copy of (array, index) to store through after reading the
        // old element, so neither subexpression is evaluated twice.
        emit(OpCode::Dup2, statement.span);
        emit(OpCode::IndexGet, statement.span);
        gen_expression(*statement.value);
        emit(arithmetic_opcode(*statement.compound, index.type), statement.op_span);
    } else {
        gen_expression(*statement.value);
    }
    emit(OpCode::IndexSet, statement.span);
}

void CodeGenerator::gen_if(const IfStmt& statement)
{
    gen_expression(*statement.condition);
    const std::size_t to_else = emit_jump(OpCode::JumpIfFalse, statement.condition->span);
    gen_statement(*statement.then_branch);

    if (statement.else_branch) {
        const std::size_t to_end = emit_jump(OpCode::Jump, statement.span);
        patch_to_here(to_else);
        gen_statement(*statement.else_branch);
        patch_to_here(to_end);
    } else {
        patch_to_here(to_else);
    }
}

void CodeGenerator::close_loop(std::uint32_t continue_target, std::uint32_t exit_target)
{
    for (const std::size_t patch : m_loops.back().break_patches) {
        patch_to(patch, exit_target);
    }
    for (const std::size_t patch : m_loops.back().continue_patches) {
        patch_to(patch, continue_target);
    }
    m_loops.pop_back();
}

void CodeGenerator::gen_while(const WhileStmt& statement)
{
    const std::uint32_t condition_at = here();
    gen_expression(*statement.condition);
    const std::size_t to_end = emit_jump(OpCode::JumpIfFalse, statement.condition->span);

    m_loops.push_back(LoopContext {});
    gen_statement(*statement.body);
    const std::size_t back = emit_jump(OpCode::Jump, statement.span);
    patch_to(back, condition_at);

    patch_to_here(to_end);
    close_loop(condition_at, here());
}

void CodeGenerator::gen_for_range(const ForRangeStmt& statement)
{
    // i = start; limit = end;   (the limit is evaluated once)
    gen_expression(*statement.start);
    emit_store(Storage::Local, statement.slot, statement.span);
    gen_expression(*statement.end);
    emit_store(Storage::Local, statement.limit_slot, statement.span);

    const std::uint32_t condition_at = here();
    emit_load(Storage::Local, statement.slot, statement.var_span);
    emit_load(Storage::Local, statement.limit_slot, statement.var_span);
    emit(OpCode::LessInt, statement.span);
    const std::size_t to_end = emit_jump(OpCode::JumpIfFalse, statement.span);

    // `continue` must run the increment, which is only emitted below, so its
    // jumps are collected and patched by close_loop.
    m_loops.push_back(LoopContext {});
    gen_statement(*statement.body);

    const std::uint32_t increment_at = here();
    emit_load(Storage::Local, statement.slot, statement.var_span);
    emit_constant(Value::integer(1), statement.span);
    emit(OpCode::AddInt, statement.span);
    emit_store(Storage::Local, statement.slot, statement.var_span);
    const std::size_t back = emit_jump(OpCode::Jump, statement.span);
    patch_to(back, condition_at);

    patch_to_here(to_end);
    close_loop(increment_at, here());
}

void CodeGenerator::gen_for_in(const ForInStmt& statement)
{
    // seq = iterable; cursor = 0;
    gen_expression(*statement.iterable);
    emit_store(Storage::Local, statement.seq_slot, statement.span);
    emit_constant(Value::integer(0), statement.span);
    emit_store(Storage::Local, statement.index_slot, statement.span);

    const std::uint32_t condition_at = here();
    emit_load(Storage::Local, statement.index_slot, statement.span);
    // The length is recomputed each iteration on purpose: mutating an array
    // while iterating it is legal, and the loop should see the change.
    emit_load(Storage::Local, statement.seq_slot, statement.span);
    emit(OpCode::CallNative, statement.span);
    chunk().write_u16(static_cast<std::uint16_t>(NativeId::Len), statement.span);
    chunk().write_u8(1, statement.span);
    emit(OpCode::LessInt, statement.span);
    const std::size_t to_end = emit_jump(OpCode::JumpIfFalse, statement.span);

    emit_load(Storage::Local, statement.seq_slot, statement.span);
    emit_load(Storage::Local, statement.index_slot, statement.span);
    emit(OpCode::IndexGet, statement.span);
    emit_store(Storage::Local, statement.slot, statement.var_span);

    m_loops.push_back(LoopContext {});
    gen_statement(*statement.body);

    const std::uint32_t increment_at = here();
    emit_load(Storage::Local, statement.index_slot, statement.span);
    emit_constant(Value::integer(1), statement.span);
    emit(OpCode::AddInt, statement.span);
    emit_store(Storage::Local, statement.index_slot, statement.span);
    const std::size_t back = emit_jump(OpCode::Jump, statement.span);
    patch_to(back, condition_at);

    patch_to_here(to_end);
    close_loop(increment_at, here());
}

void CodeGenerator::gen_return(const ReturnStmt& statement)
{
    if (statement.value) {
        gen_expression(*statement.value);
        emit(OpCode::Return, statement.span);
    } else {
        emit(OpCode::ReturnVoid, statement.span);
    }
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

OpCode CodeGenerator::arithmetic_opcode(BinaryOp op, const Type* operand)
{
    const bool is_float = operand != nullptr && operand->is(TypeKind::Float);
    const bool is_str = operand != nullptr && operand->is(TypeKind::Str);
    switch (op) {
    case BinaryOp::Add:
        return is_float ? OpCode::AddFloat : (is_str ? OpCode::ConcatStr : OpCode::AddInt);
    case BinaryOp::Subtract:
        return is_float ? OpCode::SubFloat : OpCode::SubInt;
    case BinaryOp::Multiply:
        return is_float ? OpCode::MulFloat : OpCode::MulInt;
    case BinaryOp::Divide:
        return is_float ? OpCode::DivFloat : OpCode::DivInt;
    case BinaryOp::Modulo:
        return OpCode::ModInt;
    case BinaryOp::Equal:
        return OpCode::Equal;
    case BinaryOp::NotEqual:
        return OpCode::NotEqual;
    case BinaryOp::Less:
        return is_float ? OpCode::LessFloat : (is_str ? OpCode::LessStr : OpCode::LessInt);
    case BinaryOp::LessEqual:
        return is_float ? OpCode::LessEqualFloat
                        : (is_str ? OpCode::LessEqualStr : OpCode::LessEqualInt);
    case BinaryOp::Greater:
        return is_float ? OpCode::GreaterFloat : (is_str ? OpCode::GreaterStr : OpCode::GreaterInt);
    case BinaryOp::GreaterEqual:
        return is_float ? OpCode::GreaterEqualFloat
                        : (is_str ? OpCode::GreaterEqualStr : OpCode::GreaterEqualInt);
    case BinaryOp::BitAnd:
        return OpCode::BitAnd;
    case BinaryOp::BitOr:
        return OpCode::BitOr;
    case BinaryOp::BitXor:
        return OpCode::BitXor;
    case BinaryOp::ShiftLeft:
        return OpCode::ShiftLeft;
    case BinaryOp::ShiftRight:
        return OpCode::ShiftRight;
    }
    return OpCode::Halt;
}

void CodeGenerator::gen_binary(const BinaryExpr& expr)
{
    gen_expression(*expr.left);
    gen_expression(*expr.right);
    // Comparisons yield bool, so the opcode is chosen from the *operand* type,
    // not from the type of the expression.
    emit(arithmetic_opcode(expr.op, expr.left->type), expr.op_span);
}

void CodeGenerator::gen_logical(const LogicalExpr& expr)
{
    gen_expression(*expr.left);
    // The peeking jumps leave the left operand on the stack when they are
    // taken, which is exactly the short-circuit result.
    const OpCode jump = expr.op == LogicalOp::And ? OpCode::JumpIfFalsePeek : OpCode::JumpIfTruePeek;
    const std::size_t to_end = emit_jump(jump, expr.span);
    emit(OpCode::Pop, expr.span);
    gen_expression(*expr.right);
    patch_to_here(to_end);
}

void CodeGenerator::gen_call(const CallExpr& expr)
{
    for (const ExprPtr& argument : expr.arguments) {
        gen_expression(*argument);
    }
    const auto argument_count = static_cast<std::uint8_t>(expr.arguments.size());
    if (expr.target == CallTarget::Native) {
        emit(OpCode::CallNative, expr.span);
        chunk().write_u16(static_cast<std::uint16_t>(expr.index), expr.span);
        chunk().write_u8(argument_count, expr.span);
    } else {
        emit(OpCode::Call, expr.span);
        chunk().write_u16(static_cast<std::uint16_t>(expr.index), expr.span);
        chunk().write_u8(argument_count, expr.span);
    }
}

void CodeGenerator::gen_expression(const Expr& expr)
{
    switch (expr.kind) {
    case ExprKind::IntLiteral:
        emit_constant(Value::integer(static_cast<const IntLiteralExpr&>(expr).value), expr.span);
        break;
    case ExprKind::FloatLiteral:
        emit_constant(Value::floating(static_cast<const FloatLiteralExpr&>(expr).value), expr.span);
        break;
    case ExprKind::StringLiteral:
        emit_constant(Value::string(static_cast<const StringLiteralExpr&>(expr).value), expr.span);
        break;
    case ExprKind::BoolLiteral:
        emit(static_cast<const BoolLiteralExpr&>(expr).value ? OpCode::PushTrue : OpCode::PushFalse,
            expr.span);
        break;
    case ExprKind::ArrayLiteral: {
        const auto& array = static_cast<const ArrayLiteralExpr&>(expr);
        for (const ExprPtr& element : array.elements) {
            gen_expression(*element);
        }
        emit(OpCode::MakeArray, expr.span);
        chunk().write_u16(static_cast<std::uint16_t>(array.elements.size()), expr.span);
        break;
    }
    case ExprKind::Name: {
        const auto& name = static_cast<const NameExpr&>(expr);
        emit_load(name.storage, name.slot, expr.span);
        break;
    }
    case ExprKind::Unary: {
        const auto& unary = static_cast<const UnaryExpr&>(expr);
        gen_expression(*unary.operand);
        switch (unary.op) {
        case UnaryOp::Negate:
            emit(unary.operand->type->is(TypeKind::Float) ? OpCode::NegFloat : OpCode::NegInt,
                expr.span);
            break;
        case UnaryOp::Not:
            emit(OpCode::Not, expr.span);
            break;
        case UnaryOp::BitNot:
            emit(OpCode::BitNot, expr.span);
            break;
        }
        break;
    }
    case ExprKind::Binary:
        gen_binary(static_cast<const BinaryExpr&>(expr));
        break;
    case ExprKind::Logical:
        gen_logical(static_cast<const LogicalExpr&>(expr));
        break;
    case ExprKind::Call:
        gen_call(static_cast<const CallExpr&>(expr));
        break;
    case ExprKind::Index: {
        const auto& index = static_cast<const IndexExpr&>(expr);
        gen_expression(*index.target);
        gen_expression(*index.index);
        emit(OpCode::IndexGet, expr.span);
        break;
    }
    }
}

} // namespace fire
