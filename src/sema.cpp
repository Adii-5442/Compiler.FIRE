// SPDX-License-Identifier: MIT
#include "fire/sema.hpp"

#include "fire/natives.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace fire {
namespace {

    /// Levenshtein distance, capped: only used to decide whether two
    /// identifiers are close enough to suggest one for the other.
    std::size_t edit_distance(const std::string& a, const std::string& b)
    {
        std::vector<std::size_t> previous(b.size() + 1);
        std::vector<std::size_t> current(b.size() + 1);
        for (std::size_t j = 0; j <= b.size(); ++j) {
            previous[j] = j;
        }
        for (std::size_t i = 1; i <= a.size(); ++i) {
            current[0] = i;
            for (std::size_t j = 1; j <= b.size(); ++j) {
                const std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
                current[j] = std::min({ previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost });
            }
            previous = current;
        }
        return previous[b.size()];
    }

    bool is_truthy_literal(const Expr* expr)
    {
        return expr != nullptr && expr->kind == ExprKind::BoolLiteral
            && static_cast<const BoolLiteralExpr*>(expr)->value;
    }

    /// Does this statement contain a `break` that would leave `depth`-th
    /// enclosing loop? Used to decide whether `while true { }` diverges.
    bool contains_break(const Stmt* statement)
    {
        if (statement == nullptr) {
            return false;
        }
        switch (statement->kind) {
        case StmtKind::Break:
            return true;
        case StmtKind::Block: {
            const auto* block = static_cast<const BlockStmt*>(statement);
            return std::any_of(block->statements.begin(), block->statements.end(),
                [](const StmtPtr& s) { return contains_break(s.get()); });
        }
        case StmtKind::If: {
            const auto* branch = static_cast<const IfStmt*>(statement);
            return contains_break(branch->then_branch.get())
                || contains_break(branch->else_branch.get());
        }
        // A break inside a nested loop belongs to that loop, not to this one.
        case StmtKind::While:
        case StmtKind::ForRange:
        case StmtKind::ForIn:
            return false;
        default:
            return false;
        }
    }

    /// True when the expression is a call to a builtin that never returns.
    bool is_diverging_call(const Expr* expr)
    {
        if (expr == nullptr || expr->kind != ExprKind::Call) {
            return false;
        }
        const auto* call = static_cast<const CallExpr*>(expr);
        return call->target == CallTarget::Native
            && native_info(static_cast<NativeId>(call->index)).never_returns;
    }

} // namespace

// ---------------------------------------------------------------------------
// Scopes
// ---------------------------------------------------------------------------

void Analyzer::push_scope()
{
    Scope scope;
    scope.saved_next_slot = m_functions.empty() ? 0 : m_functions.back().next_slot;
    m_scopes.push_back(std::move(scope));
}

void Analyzer::pop_scope()
{
    const Scope& scope = m_scopes.back();
    for (const Variable& variable : scope.variables) {
        if (!variable.used && !variable.is_parameter && variable.name.rfind('_', 0) != 0) {
            diags()
                .warning("W0001", "unused variable `" + variable.name + "`", variable.span)
                .label("never read after this declaration")
                .help("prefix the name with `_` to silence this warning");
        }
    }
    if (!m_functions.empty()) {
        // Sibling scopes reuse the same slots; the frame only has to be as
        // large as the deepest nesting, not as long as the whole function.
        m_functions.back().next_slot = scope.saved_next_slot;
    }
    m_scopes.pop_back();
}

Analyzer::Variable* Analyzer::declare(
    const std::string& name, const Type* type, bool is_const, Span span, bool is_parameter)
{
    Scope& scope = m_scopes.back();
    for (const Variable& existing : scope.variables) {
        if (existing.name == name) {
            diags().error("E0202", "`" + name + "` is already declared in this scope", span)
                .label("redeclared here")
                .secondary(existing.span, "first declared here")
                .help("shadowing is allowed in a nested block, but not twice in one scope");
            break;
        }
    }

    Variable variable;
    variable.name = name;
    variable.type = type;
    variable.is_const = is_const;
    variable.span = span;
    variable.is_parameter = is_parameter;

    if (at_global_scope()) {
        variable.storage = Storage::Global;
        variable.slot = m_global_count++;
    } else {
        variable.storage = Storage::Local;
        FunctionContext& context = m_functions.back();
        variable.slot = context.next_slot++;
        context.high_water = std::max(context.high_water, context.next_slot);
    }

    scope.variables.push_back(std::move(variable));
    return &scope.variables.back();
}

std::uint32_t Analyzer::allocate_hidden_slot()
{
    FunctionContext& context = m_functions.back();
    const std::uint32_t slot = context.next_slot++;
    context.high_water = std::max(context.high_water, context.next_slot);
    return slot;
}

Analyzer::Variable* Analyzer::lookup(const std::string& name)
{
    for (auto scope = m_scopes.rbegin(); scope != m_scopes.rend(); ++scope) {
        for (auto variable = scope->variables.rbegin(); variable != scope->variables.rend();
            ++variable) {
            if (variable->name == name) {
                return &*variable;
            }
        }
    }
    return nullptr;
}

std::string Analyzer::suggest_name(const std::string& name, bool functions) const
{
    std::string best;
    std::size_t best_distance = 0;
    // Anything more than a third of the name away is not a typo.
    const std::size_t threshold = std::max<std::size_t>(1, name.size() / 3 + 1);

    const auto consider = [&](const std::string& candidate) {
        const std::size_t distance = edit_distance(name, candidate);
        if (distance <= threshold && (best.empty() || distance < best_distance)) {
            best = candidate;
            best_distance = distance;
        }
    };

    if (functions) {
        for (const auto& [function_name, unused] : m_function_table) {
            (void)unused;
            consider(function_name);
        }
        for (const NativeInfo& native : native_table()) {
            consider(std::string { native.name });
        }
    } else {
        for (const Scope& scope : m_scopes) {
            for (const Variable& variable : scope.variables) {
                consider(variable.name);
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Program
// ---------------------------------------------------------------------------

bool Analyzer::analyze(Program& program)
{
    m_scopes.clear();
    m_functions.clear();
    m_function_table.clear();
    m_global_count = 0;
    return analyze_fragment(program);
}

bool Analyzer::analyze_fragment(Program& program)
{
    const std::size_t errors_before = diags().error_count();

    if (m_scopes.empty()) {
        push_scope(); // the global scope, kept alive between REPL fragments
    }

    // Functions are visible before their declaration, so collect them all
    // before analysing any body.
    for (std::unique_ptr<FunctionDecl>& function : program.functions) {
        if (const auto native = find_native(function->name); native.has_value()) {
            diags()
                .error("E0204", "`" + function->name + "` is a builtin and cannot be redefined",
                    function->name_span)
                .label("this name is already taken")
                .note(std::string { "`" } + function->name + "` "
                    + std::string { native_info(*native).summary });
            continue;
        }
        const auto [it, inserted] = m_function_table.emplace(function->name, function.get());
        if (!inserted) {
            diags()
                .error("E0205", "function `" + function->name + "` is defined more than once",
                    function->name_span)
                .label("redefined here")
                .secondary(it->second->name_span, "first defined here");
        }
    }

    // The top-level statements form an implicit function with its own frame;
    // file-scope declarations still land in the global array.
    FunctionContext script;
    script.return_type = m_types.void_type();
    m_functions.push_back(script);
    for (StmtPtr& statement : program.top_level) {
        if (statement) {
            analyze_statement(*statement);
        }
    }
    warn_unreachable(program.top_level);
    program.script_local_count = m_functions.back().high_water;
    m_functions.pop_back();

    for (std::unique_ptr<FunctionDecl>& function : program.functions) {
        analyze_function(*function);
    }

    program.global_count = m_global_count;
    return diags().error_count() == errors_before;
}

void Analyzer::analyze_function(FunctionDecl& function)
{
    FunctionContext context;
    context.declaration = &function;
    context.return_type = function.return_type;
    m_functions.push_back(context);

    push_scope();
    for (Param& param : function.params) {
        Variable* variable = declare(param.name, param.type, false, param.span, true);
        param.slot = variable->slot;
    }
    analyze_block(*function.body, /*own_scope=*/false);
    pop_scope();

    function.local_count = m_functions.back().high_water;
    m_functions.pop_back();

    if (!function.return_type->is_void() && !always_diverges(function.body.get())) {
        diags()
            .error("E0206",
                "not every path through `" + function.name + "` returns a value",
                function.name_span)
            .label("declared to return `" + function.return_type->to_string() + "`")
            .help("add a `return` at the end of the function, or an `else` branch that returns");
    }
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

void Analyzer::analyze_statement(Stmt& statement)
{
    switch (statement.kind) {
    case StmtKind::VarDecl:
        analyze_var_decl(static_cast<VarDeclStmt&>(statement));
        break;
    case StmtKind::ExprStmt:
        analyze_expression(*static_cast<ExprStmt&>(statement).expr);
        break;
    case StmtKind::Assign:
        analyze_assign(static_cast<AssignStmt&>(statement));
        break;
    case StmtKind::Block:
        analyze_block(static_cast<BlockStmt&>(statement));
        break;
    case StmtKind::If:
        analyze_if(static_cast<IfStmt&>(statement));
        break;
    case StmtKind::While:
        analyze_while(static_cast<WhileStmt&>(statement));
        break;
    case StmtKind::ForRange:
        analyze_for_range(static_cast<ForRangeStmt&>(statement));
        break;
    case StmtKind::ForIn:
        analyze_for_in(static_cast<ForInStmt&>(statement));
        break;
    case StmtKind::Break:
    case StmtKind::Continue:
        if (m_functions.back().loop_depth == 0) {
            const char* keyword = statement.kind == StmtKind::Break ? "break" : "continue";
            diags()
                .error("E0207", std::string { "`" } + keyword + "` outside of a loop",
                    statement.span)
                .label("there is no enclosing `while` or `for`");
        }
        break;
    case StmtKind::Return:
        analyze_return(static_cast<ReturnStmt&>(statement));
        break;
    }
}

void Analyzer::analyze_var_decl(VarDeclStmt& statement)
{
    const Type* initial = analyze_expression(*statement.init, statement.annotation);

    const Type* declared = statement.annotation != nullptr ? statement.annotation : initial;
    if (statement.annotation != nullptr) {
        expect_type(statement.annotation, initial, statement.init->span,
            "initialiser for `" + statement.name + "`");
    } else if (initial->is_void()) {
        diags()
            .error("E0208", "cannot bind `" + statement.name + "` to a `void` value",
                statement.init->span)
            .label("this call does not produce a value");
        declared = m_types.error_type();
    }

    Variable* variable
        = declare(statement.name, declared, statement.is_const, statement.name_span);
    statement.storage = variable->storage;
    statement.slot = variable->slot;
}

void Analyzer::analyze_assign(AssignStmt& statement)
{
    const Type* target = analyze_expression(*statement.target);

    if (statement.target->kind == ExprKind::Name) {
        auto& name = static_cast<NameExpr&>(*statement.target);
        if (Variable* variable = lookup(name.name); variable != nullptr && variable->is_const) {
            diags()
                .error("E0209", "cannot assign to `" + name.name + "`, it is a `const`",
                    statement.span)
                .label("assignment to a constant")
                .secondary(variable->span, "declared `const` here")
                .help("declare it with `let` if it needs to change");
        }
    }

    const Type* value = analyze_expression(*statement.value, target);

    if (statement.compound.has_value()) {
        // `x += e` must type-check exactly as `x = x + e` would.
        const BinaryOp op = *statement.compound;
        const bool ok = !target->is_error() && !value->is_error()
            && ((target->is_numeric() && target == value)
                || (op == BinaryOp::Add && target->is(TypeKind::Str)
                    && value->is(TypeKind::Str)));
        if (!ok && !target->is_error() && !value->is_error()) {
            diags()
                .error("E0210",
                    std::string { "cannot apply `" } + binary_op_spelling(op) + "=` to `"
                        + target->to_string() + "` and `" + value->to_string() + '`',
                    statement.op_span)
                .label("unsupported operand types");
        }
    } else {
        expect_type(target, value, statement.value->span, "assigned value");
    }
}

void Analyzer::analyze_block(BlockStmt& block, bool own_scope)
{
    if (own_scope) {
        push_scope();
    }
    for (StmtPtr& statement : block.statements) {
        if (statement) {
            analyze_statement(*statement);
        }
    }
    warn_unreachable(block.statements);
    if (own_scope) {
        pop_scope();
    }
}

void Analyzer::analyze_if(IfStmt& statement)
{
    analyze_expression(*statement.condition);
    require_bool(*statement.condition, "if");
    analyze_statement(*statement.then_branch);
    if (statement.else_branch) {
        analyze_statement(*statement.else_branch);
    }
}

void Analyzer::analyze_while(WhileStmt& statement)
{
    analyze_expression(*statement.condition);
    require_bool(*statement.condition, "while");
    ++m_functions.back().loop_depth;
    analyze_statement(*statement.body);
    --m_functions.back().loop_depth;
}

void Analyzer::analyze_for_range(ForRangeStmt& statement)
{
    const Type* start = analyze_expression(*statement.start, m_types.int_type());
    const Type* end = analyze_expression(*statement.end, m_types.int_type());
    expect_type(m_types.int_type(), start, statement.start->span, "start of a range");
    expect_type(m_types.int_type(), end, statement.end->span, "end of a range");

    push_scope();
    // The limit is evaluated once, before the first iteration, and lives in a
    // slot the programmer cannot name.
    statement.limit_slot = allocate_hidden_slot();
    Variable* variable = declare(statement.var, m_types.int_type(), false, statement.var_span);
    variable->used = true; // a loop variable that is never read is idiomatic
    statement.slot = variable->slot;

    ++m_functions.back().loop_depth;
    analyze_statement(*statement.body);
    --m_functions.back().loop_depth;
    pop_scope();
}

void Analyzer::analyze_for_in(ForInStmt& statement)
{
    const Type* sequence = analyze_expression(*statement.iterable);
    const Type* element = m_types.error_type();
    if (sequence->is(TypeKind::Array)) {
        element = sequence->element();
    } else if (sequence->is(TypeKind::Str)) {
        element = m_types.str_type();
    } else if (!sequence->is_error()) {
        diags()
            .error("E0211", "cannot iterate over `" + sequence->to_string() + '`',
                statement.iterable->span)
            .label("expected an array or a `str`")
            .help("to count, write `for i in 0..n { }`");
    }

    push_scope();
    statement.seq_slot = allocate_hidden_slot();
    statement.index_slot = allocate_hidden_slot();
    Variable* variable = declare(statement.var, element, false, statement.var_span);
    variable->used = true;
    statement.slot = variable->slot;

    ++m_functions.back().loop_depth;
    analyze_statement(*statement.body);
    --m_functions.back().loop_depth;
    pop_scope();
}

void Analyzer::analyze_return(ReturnStmt& statement)
{
    FunctionContext& context = m_functions.back();
    if (context.declaration == nullptr) {
        diags().error("E0212", "`return` outside of a function", statement.span)
            .label("top-level code cannot return")
            .help("use `exit(code)` to stop the program");
        if (statement.value) {
            analyze_expression(*statement.value);
        }
        return;
    }

    if (!statement.value) {
        if (!context.return_type->is_void()) {
            diags()
                .error("E0213", "`return` without a value in a function that returns `"
                        + context.return_type->to_string() + '`',
                    statement.span)
                .label("expected a value here");
        }
        return;
    }

    const Type* value = analyze_expression(*statement.value, context.return_type);
    if (context.return_type->is_void()) {
        diags()
            .error("E0214", "returning a value from a function declared `void`",
                statement.value->span)
            .label("this function has no return type")
            .help("add `-> " + value->to_string() + "` to the signature");
        return;
    }
    expect_type(context.return_type, value, statement.value->span, "returned value");
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

const Type* Analyzer::analyze_expression(Expr& expr, const Type* expected)
{
    const Type* result = m_types.error_type();
    switch (expr.kind) {
    case ExprKind::IntLiteral:
        result = m_types.int_type();
        break;
    case ExprKind::FloatLiteral:
        result = m_types.float_type();
        break;
    case ExprKind::StringLiteral:
        result = m_types.str_type();
        break;
    case ExprKind::BoolLiteral:
        result = m_types.bool_type();
        break;
    case ExprKind::ArrayLiteral:
        result = analyze_array_literal(static_cast<ArrayLiteralExpr&>(expr), expected);
        break;
    case ExprKind::Name:
        result = analyze_name(static_cast<NameExpr&>(expr));
        break;
    case ExprKind::Unary:
        result = analyze_unary(static_cast<UnaryExpr&>(expr));
        break;
    case ExprKind::Binary:
        result = analyze_binary(static_cast<BinaryExpr&>(expr));
        break;
    case ExprKind::Logical:
        result = analyze_logical(static_cast<LogicalExpr&>(expr));
        break;
    case ExprKind::Call:
        result = analyze_call(static_cast<CallExpr&>(expr));
        break;
    case ExprKind::Index:
        result = analyze_index(static_cast<IndexExpr&>(expr));
        break;
    }
    expr.type = result;
    return result;
}

const Type* Analyzer::analyze_array_literal(ArrayLiteralExpr& expr, const Type* expected)
{
    const Type* hint = expected != nullptr && expected->is(TypeKind::Array) ? expected->element()
                                                                           : nullptr;
    if (expr.elements.empty()) {
        if (hint == nullptr) {
            diags().error("E0215", "cannot infer the element type of an empty array", expr.span)
                .label("no elements to infer from")
                .help("write the type: `let xs: [int] = [];`");
            return m_types.error_type();
        }
        return m_types.array_of(hint);
    }

    const Type* element = analyze_expression(*expr.elements[0], hint);
    for (std::size_t i = 1; i < expr.elements.size(); ++i) {
        const Type* other = analyze_expression(*expr.elements[i], element);
        if (!other->is_error() && !element->is_error() && other != element) {
            diags()
                .error("E0216", "array elements must all have the same type",
                    expr.elements[i]->span)
                .label("this element is `" + other->to_string() + '`')
                .secondary(expr.elements[0]->span,
                    "the first element is `" + element->to_string() + '`');
            return m_types.error_type();
        }
    }
    if (element->is_void() || element->is_error()) {
        return m_types.error_type();
    }
    return m_types.array_of(element);
}

const Type* Analyzer::analyze_name(NameExpr& expr)
{
    Variable* variable = lookup(expr.name);
    if (variable == nullptr) {
        auto builder = diags().error("E0217", "cannot find `" + expr.name + "` in this scope",
            expr.span);
        builder.label("not found");
        if (m_function_table.count(expr.name) != 0 || find_native(expr.name).has_value()) {
            builder.help("`" + expr.name + "` is a function; call it with `" + expr.name + "(...)`");
        } else if (const std::string suggestion = suggest_name(expr.name, false);
            !suggestion.empty()) {
            builder.help("did you mean `" + suggestion + "`?");
        }
        return m_types.error_type();
    }
    variable->used = true;
    expr.storage = variable->storage;
    expr.slot = variable->slot;
    return variable->type;
}

const Type* Analyzer::analyze_unary(UnaryExpr& expr)
{
    const Type* operand = analyze_expression(*expr.operand);
    if (operand->is_error()) {
        return operand;
    }
    switch (expr.op) {
    case UnaryOp::Negate:
        if (!operand->is_numeric()) {
            break;
        }
        return operand;
    case UnaryOp::Not:
        if (!operand->is(TypeKind::Bool)) {
            break;
        }
        return operand;
    case UnaryOp::BitNot:
        if (!operand->is(TypeKind::Int)) {
            break;
        }
        return operand;
    }
    diags()
        .error("E0218",
            std::string { "cannot apply `" } + unary_op_spelling(expr.op) + "` to `"
                + operand->to_string() + '`',
            expr.span)
        .label("unsupported operand type");
    return m_types.error_type();
}

const Type* Analyzer::analyze_binary(BinaryExpr& expr)
{
    const Type* left = analyze_expression(*expr.left);
    const Type* right = analyze_expression(*expr.right, left);
    if (left->is_error() || right->is_error()) {
        return m_types.error_type();
    }

    const auto reject = [&](const char* why) {
        auto builder = diags().error("E0201",
            std::string { "cannot apply `" } + binary_op_spelling(expr.op) + "` to `"
                + left->to_string() + "` and `" + right->to_string() + '`',
            expr.span);
        builder.label(why);
        if (left->is_numeric() && right->is_numeric() && left != right) {
            builder.help("Fire does not convert between `int` and `float` implicitly; write `float("
                         + std::string { left->is(TypeKind::Int) ? "left" : "right" }
                + " operand)`");
        }
        if ((left->is(TypeKind::Str) || right->is(TypeKind::Str)) && expr.op == BinaryOp::Add) {
            builder.help("convert the other operand with `str(x)` to concatenate");
        }
        return m_types.error_type();
    };

    if (is_bitwise(expr.op)) {
        if (!left->is(TypeKind::Int) || !right->is(TypeKind::Int)) {
            return reject("bitwise and shift operators are defined only on `int`");
        }
        return m_types.int_type();
    }

    if (expr.op == BinaryOp::Equal || expr.op == BinaryOp::NotEqual) {
        if (left != right) {
            return reject("both sides of a comparison must have the same type");
        }
        if (left->is_void()) {
            return reject("`void` has no values to compare");
        }
        return m_types.bool_type();
    }

    if (is_comparison(expr.op)) {
        if (left != right || !left->is_ordered()) {
            return reject("`<`, `<=`, `>` and `>=` compare two `int`, two `float` or two `str`");
        }
        return m_types.bool_type();
    }

    // Arithmetic. `+` additionally concatenates strings.
    if (expr.op == BinaryOp::Add && left->is(TypeKind::Str) && right->is(TypeKind::Str)) {
        return m_types.str_type();
    }
    if (left->is_numeric() && left == right) {
        return left;
    }
    return reject("arithmetic needs two `int` or two `float`");
}

const Type* Analyzer::analyze_logical(LogicalExpr& expr)
{
    analyze_expression(*expr.left);
    analyze_expression(*expr.right);
    require_bool(*expr.left, logical_op_spelling(expr.op));
    require_bool(*expr.right, logical_op_spelling(expr.op));
    return m_types.bool_type();
}

const Type* Analyzer::analyze_call(CallExpr& expr)
{
    std::vector<const Type*> argument_types;
    std::vector<Span> argument_spans;
    argument_types.reserve(expr.arguments.size());
    argument_spans.reserve(expr.arguments.size());

    const auto declared = m_function_table.find(expr.callee);
    const FunctionDecl* function = declared != m_function_table.end() ? declared->second : nullptr;

    for (std::size_t i = 0; i < expr.arguments.size(); ++i) {
        // Pass the declared parameter type down so an empty array literal in
        // argument position can still be typed.
        const Type* hint = (function != nullptr && i < function->params.size())
            ? function->params[i].type
            : nullptr;
        argument_types.push_back(analyze_expression(*expr.arguments[i], hint));
        argument_spans.push_back(expr.arguments[i]->span);
    }

    if (function != nullptr) {
        expr.target = CallTarget::UserFunction;
        expr.index = function->index;
        if (argument_types.size() != function->params.size()) {
            diags()
                .error("E0219",
                    "`" + expr.callee + "` takes " + std::to_string(function->params.size())
                        + (function->params.size() == 1 ? " argument" : " arguments") + ", but "
                        + std::to_string(argument_types.size())
                        + (argument_types.size() == 1 ? " was" : " were") + " supplied",
                    expr.span)
                .label("wrong number of arguments")
                .secondary(function->name_span, "defined here");
            return function->return_type;
        }
        for (std::size_t i = 0; i < argument_types.size(); ++i) {
            expect_type(function->params[i].type, argument_types[i], argument_spans[i],
                "argument " + std::to_string(i + 1) + " of `" + expr.callee + '`');
        }
        return function->return_type;
    }

    if (const auto native = find_native(expr.callee); native.has_value()) {
        expr.target = CallTarget::Native;
        expr.index = static_cast<std::uint32_t>(*native);
        const NativeInfo& info = native_info(*native);
        const auto count = static_cast<std::uint16_t>(argument_types.size());
        if (count < info.min_arity
            || (info.max_arity != NativeInfo::kVariadic && count > info.max_arity)) {
            std::string expected = std::to_string(info.min_arity);
            if (info.max_arity == NativeInfo::kVariadic) {
                expected += " or more";
            } else if (info.max_arity != info.min_arity) {
                expected += " to " + std::to_string(info.max_arity);
            }
            diags()
                .error("E0220",
                    "`" + expr.callee + "` takes " + expected + " arguments, but "
                        + std::to_string(count) + (count == 1 ? " was" : " were") + " supplied",
                    expr.span)
                .label("wrong number of arguments")
                .note(std::string { info.name } + ": " + std::string { info.summary });
            return m_types.error_type();
        }
        const NativeCallCheck check { m_types, diags(), argument_types, argument_spans,
            expr.span, info.name };
        return info.check(check);
    }

    auto builder
        = diags().error("E0221", "cannot find function `" + expr.callee + '`', expr.callee_span);
    builder.label("not found in this scope");
    if (lookup(expr.callee) != nullptr) {
        builder.help("`" + expr.callee + "` is a variable, not a function");
    } else if (const std::string suggestion = suggest_name(expr.callee, true); !suggestion.empty()) {
        builder.help("did you mean `" + suggestion + "`?");
    }
    return m_types.error_type();
}

const Type* Analyzer::analyze_index(IndexExpr& expr)
{
    const Type* target = analyze_expression(*expr.target);
    const Type* index = analyze_expression(*expr.index, m_types.int_type());
    expect_type(m_types.int_type(), index, expr.index->span, "index");

    if (target->is_error()) {
        return target;
    }
    if (target->is(TypeKind::Array)) {
        return target->element();
    }
    if (target->is(TypeKind::Str)) {
        // Indexing a string yields a one-character string, not a code point;
        // `ord(s[i])` is the way to get a number.
        return m_types.str_type();
    }
    diags().error("E0222", "cannot index into `" + target->to_string() + '`', expr.span)
        .label("expected an array or a `str`");
    return m_types.error_type();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void Analyzer::require_bool(Expr& expr, const char* construct)
{
    if (expr.type == nullptr || expr.type->is_error() || expr.type->is(TypeKind::Bool)) {
        return;
    }
    auto builder = diags().error("E0223",
        std::string { "`" } + construct + "` needs a `bool`, found `" + expr.type->to_string() + '`',
        expr.span);
    builder.label("expected `bool`");
    if (expr.type->is_numeric()) {
        builder.help("Fire has no truthiness; write an explicit comparison such as `x != 0`");
    } else if (expr.type->is(TypeKind::Str) || expr.type->is(TypeKind::Array)) {
        builder.help("compare the length: `len(x) > 0`");
    }
}

void Analyzer::expect_type(
    const Type* expected, const Type* actual, Span span, const std::string& what)
{
    if (expected == nullptr || actual == nullptr || expected->is_error() || actual->is_error()
        || expected == actual) {
        return;
    }
    auto builder = diags().error("E0203",
        "mismatched types: " + what + " has type `" + actual->to_string() + "` but `"
            + expected->to_string() + "` was expected",
        span);
    builder.label("expected `" + expected->to_string() + "`, found `" + actual->to_string() + '`');
    if (expected->is_numeric() && actual->is_numeric()) {
        builder.help("convert explicitly with `" + expected->to_string() + "(x)`");
    } else if (expected->is(TypeKind::Str)) {
        builder.help("convert with `str(x)`");
    }
}

bool Analyzer::always_diverges(const Stmt* statement)
{
    if (statement == nullptr) {
        return false;
    }
    switch (statement->kind) {
    case StmtKind::Return:
        return true;
    case StmtKind::ExprStmt:
        return is_diverging_call(static_cast<const ExprStmt*>(statement)->expr.get());
    case StmtKind::Block: {
        const auto* block = static_cast<const BlockStmt*>(statement);
        return std::any_of(block->statements.begin(), block->statements.end(),
            [](const StmtPtr& s) { return always_diverges(s.get()); });
    }
    case StmtKind::If: {
        const auto* branch = static_cast<const IfStmt*>(statement);
        return branch->else_branch != nullptr && always_diverges(branch->then_branch.get())
            && always_diverges(branch->else_branch.get());
    }
    case StmtKind::While: {
        // `while true { ... }` only ends by returning or diverging, unless it
        // contains a `break` that belongs to it.
        const auto* loop = static_cast<const WhileStmt*>(statement);
        return is_truthy_literal(loop->condition.get()) && !contains_break(loop->body.get());
    }
    default:
        return false;
    }
}

void Analyzer::warn_unreachable(const std::vector<StmtPtr>& statements)
{
    for (std::size_t i = 0; i + 1 < statements.size(); ++i) {
        if (statements[i] && always_diverges(statements[i].get()) && statements[i + 1]) {
            diags()
                .warning("W0002", "unreachable code", statements[i + 1]->span)
                .label("this statement can never run")
                .secondary(statements[i]->span, "control leaves the function here");
            return; // one warning per block is enough
        }
    }
}

} // namespace fire
