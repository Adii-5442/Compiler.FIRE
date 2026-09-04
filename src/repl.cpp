// SPDX-License-Identifier: MIT
#include "fire/repl.hpp"

#include "fire/codegen.hpp"
#include "fire/driver.hpp"
#include "fire/lexer.hpp"
#include "fire/natives.hpp"
#include "fire/parser.hpp"
#include "fire/sema.hpp"
#include "fire/version.hpp"
#include "fire/vm.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fire {
namespace {

    /// Incremental REPL state.
    ///
    /// Each line is compiled as its own *fragment*: a small program with its
    /// own script chunk. What persists between fragments is everything a later
    /// line can refer to — the type context, the analyzer's global scope and
    /// function table, the compiled function table, and the values of the
    /// globals themselves.
    ///
    /// The alternative, re-running the whole session on every line, would
    /// repeat side effects. Compiling fragments avoids that at the cost of
    /// keeping every parsed Program alive, since the analyzer's function table
    /// points into them.
    class Session {
    public:
        Session(std::ostream& out, bool color)
            : m_out(out)
            , m_color(color)
            , m_analyzer(m_null_diagnostics_placeholder(), m_types)
        {
            m_module.script.name = "<repl>";
        }

        /// Compile and run one fragment. Returns false if the session should
        /// end (an `exit` call), storing the status in `exit_status`.
        bool evaluate(const std::string& text, int& exit_status);

    private:
        /// The analyzer needs an engine at construction; it is replaced before
        /// every fragment by one bound to that fragment's source.
        DiagnosticEngine& m_null_diagnostics_placeholder()
        {
            static SourceFile empty = SourceFile::from_string("<repl>", "");
            static DiagnosticEngine engine { empty, false };
            return engine;
        }

        std::ostream& m_out;
        bool m_color;
        TypeContext m_types;
        Analyzer m_analyzer;
        /// Parsed fragments, kept alive because the analyzer's function table
        /// and the AST of every declared function point into them.
        std::vector<std::unique_ptr<Program>> m_history;
        Module m_module;
        std::vector<Value> m_globals;
        std::uint32_t m_fragment = 0;
    };

    /// If the fragment is a single expression statement with a value, wrap it
    /// in `println(...)` so the session echoes results the way a REPL should.
    /// Done after analysis so the type is known; the node is built by hand
    /// rather than by re-parsing.
    void wrap_last_expression(Program& program)
    {
        if (program.top_level.size() != 1 || !program.top_level.front()) {
            return;
        }
        Stmt* statement = program.top_level.front().get();
        if (statement->kind != StmtKind::ExprStmt) {
            return;
        }
        auto& expression = static_cast<ExprStmt&>(*statement);
        if (expression.expr->type == nullptr || expression.expr->type->is_void()
            || expression.expr->type->is_error()) {
            return;
        }

        auto call = std::make_unique<CallExpr>(
            expression.expr->span, "println", expression.expr->span);
        call->target = CallTarget::Native;
        call->index = static_cast<std::uint32_t>(NativeId::Println);
        call->type = expression.expr->type; // any non-void type; only voidness matters
        call->arguments.push_back(std::move(expression.expr));
        expression.expr = std::move(call);
    }

    bool Session::evaluate(const std::string& text, int& exit_status)
    {
        const std::string name = "<repl:" + std::to_string(++m_fragment) + '>';
        auto source = std::make_unique<SourceFile>(SourceFile::from_string(name, text));
        DiagnosticEngine diagnostics { *source, m_color };

        Lexer lexer { *source, diagnostics };
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser { std::move(tokens), diagnostics, m_types };
        auto program = std::make_unique<Program>(parser.parse_program());

        // Function indices are global across the session, not per fragment.
        const auto base = static_cast<std::uint32_t>(m_module.functions.size());
        for (std::size_t i = 0; i < program->functions.size(); ++i) {
            program->functions[i]->index = base + static_cast<std::uint32_t>(i);
        }

        m_analyzer.set_diagnostics(diagnostics);
        m_analyzer.analyze_fragment(*program);

        if (diagnostics.has_errors()) {
            diagnostics.render(m_out);
            return true;
        }
        wrap_last_expression(*program);
        diagnostics.render(m_out); // warnings, if any

        CodeGenerator generator;
        Module fragment = generator.generate(*program);

        m_module.script = std::move(fragment.script);
        for (CompiledFunction& function : fragment.functions) {
            m_module.functions.push_back(std::move(function));
        }
        m_module.global_count = m_analyzer.global_count();

        VM::Options options;
        options.source = source.get();
        options.out = &m_out;
        options.color = m_color;
        VM vm { m_module, std::move(options) };
        vm.adopt_globals(std::move(m_globals));

        // The VM catches ExitSignal itself and turns it into a status, so a
        // non-zero result here means either `exit` or a runtime error.
        const int status = vm.run();
        m_globals = vm.globals();

        // Keep the fragment's source and tree alive: later fragments may call
        // a function declared here, and diagnostics may point back at it.
        m_history.push_back(std::move(program));
        static std::vector<std::unique_ptr<SourceFile>> sources;
        sources.push_back(std::move(source));

        if (status != 0) {
            exit_status = status;
            return false;
        }
        return true;
    }

    /// True when the text has more openers than closers, i.e. the user is
    /// mid-definition and the prompt should continue.
    bool is_incomplete(const std::string& text)
    {
        int depth = 0;
        bool in_string = false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (in_string) {
                if (c == '\\') {
                    ++i;
                } else if (c == '"') {
                    in_string = false;
                }
                continue;
            }
            if (c == '"') {
                in_string = true;
            } else if (c == '{' || c == '(' || c == '[') {
                ++depth;
            } else if (c == '}' || c == ')' || c == ']') {
                --depth;
            }
        }
        return depth > 0;
    }

} // namespace

int run_repl(std::istream& in, std::ostream& out, bool color)
{
    out << kLanguageName << ' ' << version_string() << " — interactive session\n"
        << "type an expression to see its value, `:help` for commands, Ctrl-D to leave.\n\n";

    Session session { out, color };
    std::string pending;
    int exit_status = 0;

    while (true) {
        out << (pending.empty() ? "fire> " : "  ... ") << std::flush;
        std::string line;
        if (!std::getline(in, line)) {
            out << '\n';
            break;
        }

        if (pending.empty()) {
            if (line == ":quit" || line == ":q") {
                break;
            }
            if (line == ":help" || line == ":h") {
                out << "  :help          this message\n"
                       "  :builtins      list the builtin functions\n"
                       "  :quit          leave the session\n\n"
                       "Declarations persist across lines. A bare expression is printed.\n"
                       "Statements need their semicolon, exactly as in a file.\n\n";
                continue;
            }
            if (line == ":builtins") {
                for (const NativeInfo& native : native_table()) {
                    out << "  " << native.name << " — " << native.summary << '\n';
                }
                out << '\n';
                continue;
            }
            if (line.empty()) {
                continue;
            }
        }

        pending += line;
        pending += '\n';
        if (is_incomplete(pending)) {
            continue;
        }

        std::string fragment = pending;
        pending.clear();

        // A bare expression is far more convenient to type without the
        // semicolon, so supply one when the line plainly needs it.
        const std::size_t last = fragment.find_last_not_of(" \t\r\n");
        if (last != std::string::npos && fragment[last] != ';' && fragment[last] != '}') {
            fragment.insert(last + 1, ";");
        }

        if (!session.evaluate(fragment, exit_status)) {
            return exit_status;
        }
    }
    return exit_status;
}

} // namespace fire
