// SPDX-License-Identifier: MIT
//
// The compilation pipeline, wrapped so that every entry point — `run`,
// `build`, `check`, `emit`, the REPL and the tests — drives the exact same
// sequence of passes.
#pragma once

#include "fire/ast.hpp"
#include "fire/chunk.hpp"
#include "fire/diagnostics.hpp"
#include "fire/sema.hpp"
#include "fire/source.hpp"
#include "fire/token.hpp"
#include "fire/type.hpp"

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace fire {

/// Owns one compilation: its source, its diagnostics and every intermediate
/// form. Deliberately neither copyable nor movable — the diagnostic engine
/// holds a reference to the source that lives inside this object.
class Compilation {
public:
    explicit Compilation(SourceFile source, bool color = false);

    Compilation(const Compilation&) = delete;
    Compilation& operator=(const Compilation&) = delete;

    /// Lex, parse and analyse. Returns false if any error was reported.
    bool analyze();

    /// Run `analyze` and then generate bytecode. Returns false on error.
    bool compile();

    [[nodiscard]] const SourceFile& source() const { return m_source; }
    [[nodiscard]] DiagnosticEngine& diagnostics() { return m_diagnostics; }
    [[nodiscard]] const std::vector<Token>& tokens() const { return m_tokens; }
    [[nodiscard]] const Program& program() const { return m_program; }
    [[nodiscard]] const Module& module() const { return m_module; }
    [[nodiscard]] TypeContext& types() { return m_types; }

    /// Print every diagnostic collected so far.
    void report(std::ostream& out) const;

private:
    SourceFile m_source;
    TypeContext m_types;
    DiagnosticEngine m_diagnostics;
    std::vector<Token> m_tokens;
    Program m_program;
    Module m_module;
    bool m_analyzed = false;
};

/// Write the token stream in the form `fire emit --tokens` prints.
void dump_tokens(std::ostream& out, const SourceFile& source, const std::vector<Token>& tokens);

/// Write an indented s-expression rendering of the tree.
void dump_ast(std::ostream& out, const Program& program);

} // namespace fire
