// SPDX-License-Identifier: MIT
#include "fire/backend_x86_64.hpp"

#include "fire/natives.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace fire {
namespace {

    /// The freestanding runtime, emitted ahead of the compiled code.
    ///
    /// Six helpers, all callee-clobbering only what they document: write a
    /// byte range, render a signed 64-bit integer in decimal, render a bool,
    /// and abort with a message. There is no libc here — the program's entry
    /// point is `_start` and its only interaction with the kernel is `write`
    /// and `exit_group`.
    constexpr const char* kRuntime = R"ASM(
; ---------------------------------------------------------------------------
; Fire runtime — freestanding, Linux x86-64
; ---------------------------------------------------------------------------

; fire_write: rsi = buffer, rdx = length. Clobbers rax, rcx, r11, rdi.
fire_write:
    mov     rax, 1                  ; SYS_write
    mov     rdi, 1                  ; stdout
    syscall
    ret

; fire_print_int: rax = value. Renders decimal into a 24-byte scratch buffer,
; filling it from the back because digits come out least significant first.
fire_print_int:
    push    rbx
    lea     rcx, [fire_numbuf + 24] ; one past the end
    mov     rbx, rax
    test    rax, rax
    jns     .positive
    neg     rax
    ; INT64_MIN negates to itself; the digit loop below still produces the
    ; correct magnitude because the division is unsigned-safe for that value.
.positive:
    mov     r8, 10
.digit:
    xor     rdx, rdx
    div     r8                      ; unsigned: rax was made non-negative above
    add     dl, '0'
    dec     rcx
    mov     [rcx], dl
    test    rax, rax
    jnz     .digit
    test    rbx, rbx
    jns     .no_sign
    dec     rcx
    mov     byte [rcx], '-'
.no_sign:
    mov     rsi, rcx
    lea     rdx, [fire_numbuf + 24]
    sub     rdx, rcx
    call    fire_write
    pop     rbx
    ret

; fire_print_bool: rax = 0 or 1.
fire_print_bool:
    test    rax, rax
    jz      .false
    lea     rsi, [fire_str_true]
    mov     rdx, 4
    jmp     fire_write
.false:
    lea     rsi, [fire_str_false]
    mov     rdx, 5
    jmp     fire_write

; fire_print_nl: writes a single newline.
fire_print_nl:
    lea     rsi, [fire_str_nl]
    mov     rdx, 1
    jmp     fire_write

; fire_print_sp: writes a single space, used between print arguments.
fire_print_sp:
    lea     rsi, [fire_str_sp]
    mov     rdx, 1
    jmp     fire_write

; fire_abort: rsi = message, rdx = length. Writes to stderr and exits 70.
fire_abort:
    mov     rax, 1
    mov     rdi, 2                  ; stderr
    syscall
    mov     rax, 231                ; SYS_exit_group
    mov     rdi, 70
    syscall

fire_div_zero:
    lea     rsi, [fire_msg_div]
    mov     rdx, fire_msg_div_len
    jmp     fire_abort
)ASM";

    /// Names that must not collide with a Fire identifier once mangled.
    std::string mangle(const std::string& name, std::uint32_t index)
    {
        std::string out = "fire_fn_" + std::to_string(index) + "_";
        for (const char c : name) {
            out.push_back((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    ? c
                    : '_');
        }
        return out;
    }

    class X86Emitter {
    public:
        X86Emitter(const Program& program, DiagnosticEngine& diagnostics)
            : m_program(program)
            , m_diagnostics(diagnostics)
        {
        }

        std::string emit();

    private:
        struct LoopLabels {
            std::string continue_label;
            std::string break_label;
        };

        // -- output helpers -------------------------------------------------
        void line(const std::string& text) { m_text << "    " << text << '\n'; }
        void label(const std::string& name) { m_text << name << ":\n"; }
        void comment(const std::string& text) { m_text << "    ; " << text << '\n'; }
        [[nodiscard]] std::string fresh_label(const char* prefix)
        {
            return std::string { "L" } + prefix + std::to_string(m_label_counter++);
        }

        /// Report a construct the backend cannot lower, once per feature.
        void unsupported(Span span, const std::string& what, const std::string& detail = { });

        [[nodiscard]] bool supported_scalar(const Type* type) const
        {
            return type != nullptr
                && (type->is(TypeKind::Int) || type->is(TypeKind::Bool) || type->is_error());
        }

        [[nodiscard]] std::string string_constant(const std::string& text);
        [[nodiscard]] static std::string slot_address(Storage storage, std::uint32_t slot);

        // -- generation -----------------------------------------------------
        void gen_function(const FunctionDecl& function);
        void gen_statement(const Stmt& statement);
        void gen_block(const BlockStmt& block);
        void gen_if(const IfStmt& statement);
        void gen_while(const WhileStmt& statement);
        void gen_for_range(const ForRangeStmt& statement);
        void gen_assign(const AssignStmt& statement);

        /// Leaves the value of `expr` on the machine stack.
        void gen_expression(const Expr& expr);
        void gen_binary(const BinaryExpr& expr);
        void gen_logical(const LogicalExpr& expr);
        void gen_call(const CallExpr& expr);
        void gen_print(const CallExpr& expr, bool newline);
        /// Emit the binary operator with the left operand in rax and the right
        /// in rcx, leaving the result in rax.
        void gen_binary_operator(BinaryOp op, Span span);

        const Program& m_program;
        DiagnosticEngine& m_diagnostics;

        std::ostringstream m_text;
        std::ostringstream m_rodata;
        std::vector<std::string> m_string_labels;
        std::unordered_map<std::string, std::string> m_string_pool;
        std::vector<LoopLabels> m_loops;
        std::uint32_t m_label_counter = 0;
        /// Set once a feature has been rejected, to keep the report short.
        std::unordered_map<std::string, bool> m_reported;
    };

    void X86Emitter::unsupported(Span span, const std::string& what, const std::string& detail)
    {
        if (m_reported.count(what) != 0) {
            return;
        }
        m_reported[what] = true;
        auto builder =
            m_diagnostics.error("N0001", "the native backend does not support " + what, span);
        builder.label("not available when compiling to a native executable");
        if (!detail.empty()) {
            builder.note(detail);
        }
        builder.help("run this program on the Fire VM instead: `fire run <file.fire>`");
    }

    std::string X86Emitter::string_constant(const std::string& text)
    {
        if (const auto it = m_string_pool.find(text); it != m_string_pool.end()) {
            return it->second;
        }
        const std::string name = "Lstr" + std::to_string(m_string_labels.size());
        m_string_labels.push_back(name);
        m_string_pool.emplace(text, name);

        // Emit as explicit bytes: a Fire string may contain quotes, newlines
        // or NULs, none of which survive NASM's quoted form intact.
        m_rodata << name << ":";
        if (text.empty()) {
            m_rodata << " db 0";
        } else {
            m_rodata << " db ";
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (i > 0) {
                    m_rodata << ',';
                }
                m_rodata << static_cast<int>(static_cast<unsigned char>(text[i]));
            }
        }
        m_rodata << '\n' << name << "_len equ " << text.size() << '\n';
        return name;
    }

    std::string X86Emitter::slot_address(Storage storage, std::uint32_t slot)
    {
        if (storage == Storage::Global) {
            return "[fire_globals + " + std::to_string(slot * 8) + "]";
        }
        // Slot 0 lives at rbp-8, slot 1 at rbp-16, and so on.
        return "[rbp - " + std::to_string((slot + 1) * 8) + "]";
    }

    void X86Emitter::gen_binary_operator(BinaryOp op, Span span)
    {
        switch (op) {
        case BinaryOp::Add:
            line("add     rax, rcx");
            return;
        case BinaryOp::Subtract:
            line("sub     rax, rcx");
            return;
        case BinaryOp::Multiply:
            line("imul    rax, rcx");
            return;
        case BinaryOp::Divide:
        case BinaryOp::Modulo:
            line("test    rcx, rcx");
            line("jz      fire_div_zero");
            line("cqo");
            line("idiv    rcx");
            if (op == BinaryOp::Modulo) {
                line("mov     rax, rdx");
            }
            return;
        case BinaryOp::BitAnd:
            line("and     rax, rcx");
            return;
        case BinaryOp::BitOr:
            line("or      rax, rcx");
            return;
        case BinaryOp::BitXor:
            line("xor     rax, rcx");
            return;
        case BinaryOp::ShiftLeft:
            line("shl     rax, cl");
            return;
        case BinaryOp::ShiftRight:
            line("sar     rax, cl");
            return;
        default:
            break;
        }

        // Comparisons: compute the flags, then materialise 0 or 1.
        const char* setcc = "sete";
        switch (op) {
        case BinaryOp::Equal:
            setcc = "sete";
            break;
        case BinaryOp::NotEqual:
            setcc = "setne";
            break;
        case BinaryOp::Less:
            setcc = "setl";
            break;
        case BinaryOp::LessEqual:
            setcc = "setle";
            break;
        case BinaryOp::Greater:
            setcc = "setg";
            break;
        case BinaryOp::GreaterEqual:
            setcc = "setge";
            break;
        default:
            unsupported(span, "this operator");
            return;
        }
        line("cmp     rax, rcx");
        line(std::string { setcc } + "     al");
        line("movzx   rax, al");
    }

    void X86Emitter::gen_binary(const BinaryExpr& expr)
    {
        if (!supported_scalar(expr.left->type)) {
            unsupported(expr.span,
                "`"
                    + (expr.left->type != nullptr ? expr.left->type->to_string()
                                                  : std::string { "?" })
                    + "` arithmetic",
                "the native backend handles `int` and `bool` only");
            return;
        }
        gen_expression(*expr.left);
        gen_expression(*expr.right);
        line("pop     rcx");
        line("pop     rax");
        gen_binary_operator(expr.op, expr.op_span);
        line("push    rax");
    }

    void X86Emitter::gen_logical(const LogicalExpr& expr)
    {
        const std::string done = fresh_label("logic");
        gen_expression(*expr.left);
        line("pop     rax");
        line("test    rax, rax");
        // `&&` stops on false and `||` on true; in both cases the left
        // operand already in rax is the value of the whole expression.
        line(std::string { expr.op == LogicalOp::And ? "jz " : "jnz" } + "     " + done);
        gen_expression(*expr.right);
        line("pop     rax");
        label(done);
        line("push    rax");
    }

    void X86Emitter::gen_print(const CallExpr& expr, bool newline)
    {
        for (std::size_t i = 0; i < expr.arguments.size(); ++i) {
            if (i > 0) {
                line("call    fire_print_sp");
            }
            const Expr& argument = *expr.arguments[i];
            if (argument.type != nullptr && argument.type->is(TypeKind::Str)) {
                if (argument.kind != ExprKind::StringLiteral) {
                    unsupported(argument.span, "printing a computed `str`",
                        "the native backend can print string literals, but has no heap to build "
                        "new strings on");
                    continue;
                }
                const std::string name =
                    string_constant(static_cast<const StringLiteralExpr&>(argument).value);
                line("lea     rsi, [" + name + "]");
                line("mov     rdx, " + name + "_len");
                line("call    fire_write");
                continue;
            }
            gen_expression(argument);
            line("pop     rax");
            if (argument.type != nullptr && argument.type->is(TypeKind::Bool)) {
                line("call    fire_print_bool");
            } else {
                line("call    fire_print_int");
            }
        }
        if (newline) {
            line("call    fire_print_nl");
        }
        // Every expression must leave a value; a void call leaves a zero.
        line("push    0");
    }

    void X86Emitter::gen_call(const CallExpr& expr)
    {
        if (expr.target == CallTarget::Native) {
            const auto id = static_cast<NativeId>(expr.index);
            switch (id) {
            case NativeId::Print:
                gen_print(expr, false);
                return;
            case NativeId::Println:
                gen_print(expr, true);
                return;
            case NativeId::Exit:
                gen_expression(*expr.arguments[0]);
                line("pop     rdi");
                line("and     rdi, 255");
                line("mov     rax, 231");
                line("syscall");
                line("push    0");
                return;
            case NativeId::Abs: {
                gen_expression(*expr.arguments[0]);
                line("pop     rax");
                line("mov     rcx, rax");
                line("neg     rcx");
                line("cmovl   rcx, rax"); // if -x < 0 then x was positive
                line("push    rcx");
                return;
            }
            case NativeId::Min:
            case NativeId::Max: {
                gen_expression(*expr.arguments[0]);
                gen_expression(*expr.arguments[1]);
                line("pop     rcx");
                line("pop     rax");
                line("cmp     rax, rcx");
                line(id == NativeId::Min ? "cmovg   rax, rcx" : "cmovl   rax, rcx");
                line("push    rax");
                return;
            }
            default:
                unsupported(expr.span, "the builtin `" + expr.callee + "`",
                    "natively available builtins are print, println, exit, abs, min and max");
                line("push    0");
                return;
            }
        }

        const FunctionDecl& callee = *m_program.functions[expr.index];
        // Arguments go on the stack right to left, so argument 0 ends up at
        // [rbp + 16] in the callee.
        for (std::size_t i = expr.arguments.size(); i > 0; --i) {
            gen_expression(*expr.arguments[i - 1]);
        }
        line("call    " + mangle(callee.name, callee.index));
        if (!expr.arguments.empty()) {
            line("add     rsp, " + std::to_string(expr.arguments.size() * 8));
        }
        line("push    rax");
    }

    void X86Emitter::gen_expression(const Expr& expr)
    {
        switch (expr.kind) {
        case ExprKind::IntLiteral:
            line("mov     rax, " + std::to_string(static_cast<const IntLiteralExpr&>(expr).value));
            line("push    rax");
            return;
        case ExprKind::BoolLiteral:
            line(std::string { "push    " }
                + (static_cast<const BoolLiteralExpr&>(expr).value ? "1" : "0"));
            return;
        case ExprKind::FloatLiteral:
            unsupported(expr.span, "`float`",
                "floating point needs SSE register allocation the bytecode VM already does");
            line("push    0");
            return;
        case ExprKind::StringLiteral:
            unsupported(expr.span, "`str` values outside `print`",
                "string literals may be passed to print and println");
            line("push    0");
            return;
        case ExprKind::ArrayLiteral:
            unsupported(
                expr.span, "arrays", "arrays need a heap allocator this backend has none of");
            line("push    0");
            return;
        case ExprKind::Name: {
            const auto& name = static_cast<const NameExpr&>(expr);
            if (!supported_scalar(name.type)) {
                unsupported(expr.span,
                    "variables of type `"
                        + (name.type != nullptr ? name.type->to_string() : std::string { "?" })
                        + '`');
                line("push    0");
                return;
            }
            line("mov     rax, qword " + slot_address(name.storage, name.slot));
            line("push    rax");
            return;
        }
        case ExprKind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            gen_expression(*unary.operand);
            line("pop     rax");
            switch (unary.op) {
            case UnaryOp::Negate:
                line("neg     rax");
                break;
            case UnaryOp::Not:
                line("xor     rax, 1");
                break;
            case UnaryOp::BitNot:
                line("not     rax");
                break;
            }
            line("push    rax");
            return;
        }
        case ExprKind::Binary:
            gen_binary(static_cast<const BinaryExpr&>(expr));
            return;
        case ExprKind::Logical:
            gen_logical(static_cast<const LogicalExpr&>(expr));
            return;
        case ExprKind::Call:
            gen_call(static_cast<const CallExpr&>(expr));
            return;
        case ExprKind::Index:
            unsupported(expr.span, "indexing");
            line("push    0");
            return;
        }
    }

    void X86Emitter::gen_assign(const AssignStmt& statement)
    {
        if (statement.target->kind != ExprKind::Name) {
            unsupported(statement.span, "assignment to an array element");
            return;
        }
        const auto& name = static_cast<const NameExpr&>(*statement.target);
        if (statement.compound.has_value()) {
            line("mov     rax, qword " + slot_address(name.storage, name.slot));
            line("push    rax");
            gen_expression(*statement.value);
            line("pop     rcx");
            line("pop     rax");
            gen_binary_operator(*statement.compound, statement.op_span);
        } else {
            gen_expression(*statement.value);
            line("pop     rax");
        }
        line("mov     qword " + slot_address(name.storage, name.slot) + ", rax");
    }

    void X86Emitter::gen_if(const IfStmt& statement)
    {
        const std::string otherwise = fresh_label("else");
        const std::string done = fresh_label("endif");
        gen_expression(*statement.condition);
        line("pop     rax");
        line("test    rax, rax");
        line("jz      " + (statement.else_branch ? otherwise : done));
        gen_statement(*statement.then_branch);
        if (statement.else_branch) {
            line("jmp     " + done);
            label(otherwise);
            gen_statement(*statement.else_branch);
        }
        label(done);
    }

    void X86Emitter::gen_while(const WhileStmt& statement)
    {
        const std::string top = fresh_label("while");
        const std::string done = fresh_label("endwhile");
        label(top);
        gen_expression(*statement.condition);
        line("pop     rax");
        line("test    rax, rax");
        line("jz      " + done);
        m_loops.push_back(LoopLabels { top, done });
        gen_statement(*statement.body);
        m_loops.pop_back();
        line("jmp     " + top);
        label(done);
    }

    void X86Emitter::gen_for_range(const ForRangeStmt& statement)
    {
        const std::string top = fresh_label("for");
        const std::string next = fresh_label("fornext");
        const std::string done = fresh_label("endfor");
        const std::string counter = slot_address(Storage::Local, statement.slot);
        const std::string limit = slot_address(Storage::Local, statement.limit_slot);

        gen_expression(*statement.start);
        line("pop     rax");
        line("mov     qword " + counter + ", rax");
        gen_expression(*statement.end);
        line("pop     rax");
        line("mov     qword " + limit + ", rax");

        label(top);
        line("mov     rax, qword " + counter);
        line("cmp     rax, qword " + limit);
        line("jge     " + done);

        m_loops.push_back(LoopLabels { next, done });
        gen_statement(*statement.body);
        m_loops.pop_back();

        label(next);
        line("mov     rax, qword " + counter);
        line("inc     rax");
        line("mov     qword " + counter + ", rax");
        line("jmp     " + top);
        label(done);
    }

    void X86Emitter::gen_block(const BlockStmt& block)
    {
        for (const StmtPtr& statement : block.statements) {
            if (statement) {
                gen_statement(*statement);
            }
        }
    }

    void X86Emitter::gen_statement(const Stmt& statement)
    {
        switch (statement.kind) {
        case StmtKind::VarDecl: {
            const auto& declaration = static_cast<const VarDeclStmt&>(statement);
            gen_expression(*declaration.init);
            line("pop     rax");
            line("mov     qword " + slot_address(declaration.storage, declaration.slot) + ", rax");
            return;
        }
        case StmtKind::ExprStmt:
            gen_expression(*static_cast<const ExprStmt&>(statement).expr);
            line("add     rsp, 8");
            return;
        case StmtKind::Assign:
            gen_assign(static_cast<const AssignStmt&>(statement));
            return;
        case StmtKind::Block:
            gen_block(static_cast<const BlockStmt&>(statement));
            return;
        case StmtKind::If:
            gen_if(static_cast<const IfStmt&>(statement));
            return;
        case StmtKind::While:
            gen_while(static_cast<const WhileStmt&>(statement));
            return;
        case StmtKind::ForRange:
            gen_for_range(static_cast<const ForRangeStmt&>(statement));
            return;
        case StmtKind::ForIn:
            unsupported(
                statement.span, "`for ... in` over a sequence", "`for i in 0..n` is supported");
            return;
        case StmtKind::Break:
            if (!m_loops.empty()) {
                line("jmp     " + m_loops.back().break_label);
            }
            return;
        case StmtKind::Continue:
            if (!m_loops.empty()) {
                line("jmp     " + m_loops.back().continue_label);
            }
            return;
        case StmtKind::Return: {
            const auto& returned = static_cast<const ReturnStmt&>(statement);
            if (returned.value) {
                gen_expression(*returned.value);
                line("pop     rax");
            } else {
                line("xor     rax, rax");
            }
            line("leave");
            line("ret");
            return;
        }
        }
    }

    void X86Emitter::gen_function(const FunctionDecl& function)
    {
        for (const Param& param : function.params) {
            if (!supported_scalar(param.type)) {
                unsupported(param.span, "parameters of type `" + param.type->to_string() + '`');
            }
        }
        if (!function.return_type->is_void() && !supported_scalar(function.return_type)) {
            unsupported(
                function.name_span, "returning `" + function.return_type->to_string() + '`');
        }

        m_text << '\n';
        comment("fn " + function.name + '/' + std::to_string(function.params.size()));
        label(mangle(function.name, function.index));
        line("push    rbp");
        line("mov     rbp, rsp");
        // Round the frame up to a multiple of 16 so the stack stays aligned
        // for anything that later expects the ABI's guarantee.
        const std::uint32_t frame = ((function.local_count * 8) + 15) & ~15U;
        if (frame > 0) {
            line("sub     rsp, " + std::to_string(frame));
        }
        // Copy incoming arguments from the caller's frame into local slots, so
        // the rest of codegen can treat parameters exactly like other locals.
        for (std::size_t i = 0; i < function.params.size(); ++i) {
            line("mov     rax, qword [rbp + " + std::to_string(16 + i * 8) + ']');
            line(
                "mov     qword " + slot_address(Storage::Local, function.params[i].slot) + ", rax");
        }
        gen_block(*function.body);
        // Falling off the end returns 0; sema guarantees this is only reachable
        // for a `void` function.
        line("xor     rax, rax");
        line("leave");
        line("ret");
    }

    std::string X86Emitter::emit()
    {
        std::ostringstream head;
        head << "; Generated by the Fire compiler — do not edit.\n"
             << "; Target: x86-64 Linux, freestanding (no libc).\n"
             << "; Assemble and link with:\n"
             << ";     nasm -felf64 out.asm -o out.o && ld -o out out.o\n\n"
             << "BITS 64\ndefault rel\n\n"
             << "global _start\n\nsection .text\n";
        head << kRuntime;

        // The script body becomes _start.
        m_text << "\n; top-level program body\n_start:\n";
        line("push    rbp");
        line("mov     rbp, rsp");
        const std::uint32_t frame = ((m_program.script_local_count * 8) + 15) & ~15U;
        if (frame > 0) {
            line("sub     rsp, " + std::to_string(frame));
        }
        for (const StmtPtr& statement : m_program.top_level) {
            if (statement) {
                gen_statement(*statement);
            }
        }
        line("mov     rax, 231"); // SYS_exit_group
        line("xor     rdi, rdi");
        line("syscall");

        for (const std::unique_ptr<FunctionDecl>& function : m_program.functions) {
            gen_function(*function);
        }

        std::ostringstream out;
        out << head.str() << m_text.str();
        out << "\nsection .rodata\n";
        out << "fire_str_true:  db \"true\"\n"
               "fire_str_false: db \"false\"\n"
               "fire_str_nl:    db 10\n"
               "fire_str_sp:    db 32\n"
               "fire_msg_div:   db \"runtime error: division by zero\", 10\n"
               "fire_msg_div_len equ $ - fire_msg_div\n";
        out << m_rodata.str();
        out << "\nsection .bss\n";
        out << "fire_numbuf: resb 24\n";
        out << "fire_globals: resq " << (m_program.global_count > 0 ? m_program.global_count : 1)
            << '\n';
        return out.str();
    }

} // namespace

std::string emit_x86_64(const Program& program, TypeContext& types, DiagnosticEngine& diagnostics)
{
    // The type context is part of the interface because a future backend will
    // need it to synthesise types; nothing here does yet.
    (void)types;
    X86Emitter emitter { program, diagnostics };
    return emitter.emit();
}

NativeBuildResult build_native(const Program& program, TypeContext& types,
    DiagnosticEngine& diagnostics, const std::string& output, bool assembly_only)
{
    NativeBuildResult result;
    const std::string assembly = emit_x86_64(program, types, diagnostics);
    if (diagnostics.has_errors()) {
        return result;
    }

    const std::string asm_path = output + ".asm";
    {
        std::ofstream file { asm_path };
        if (!file) {
            result.message = "cannot write '" + asm_path + "'";
            return result;
        }
        file << assembly;
    }
    if (assembly_only) {
        result.ok = true;
        result.artifact = asm_path;
        return result;
    }

    const std::string object_path = output + ".o";
    const std::string assemble = "nasm -felf64 '" + asm_path + "' -o '" + object_path + "'";
    if (std::system(assemble.c_str()) != 0) {
        result.message = "nasm failed. Is it installed? (apt install nasm / brew install nasm)\n"
                         "       the generated assembly was kept at "
            + asm_path;
        return result;
    }

    const std::string link = "ld -o '" + output + "' '" + object_path + "'";
    if (std::system(link.c_str()) != 0) {
        result.message = "ld failed while linking " + object_path;
        return result;
    }
    // Keep only the executable; the assembly is reproducible with
    // `fire build -S` or `fire emit --asm` whenever it is wanted.
    std::remove(object_path.c_str());
    std::remove(asm_path.c_str());

    result.ok = true;
    result.artifact = output;
    return result;
}

} // namespace fire
