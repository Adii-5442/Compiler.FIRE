// SPDX-License-Identifier: MIT
//
// Diagnostic collection and rendering.
//
// Fire never aborts on the first error. Each pass reports into a
// `DiagnosticEngine` and keeps going where it sensibly can, so one run of the
// compiler tells the programmer everything that is wrong with the file. The
// renderer produces the familiar caret-under-the-source form:
//
//     error[E0201]: cannot apply `+` to `int` and `str`
//      --> examples/bad.fire:4:13
//       |
//     4 |     let x = 1 + "two";
//       |             ^^^^^^^^^ operands have incompatible types
//       |
//       = help: convert the left operand with `str(1)`
#pragma once

#include "fire/source.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace fire {

enum class Severity {
    Error,
    Warning,
    Note,
};

[[nodiscard]] const char* severity_name(Severity severity);

/// A secondary span highlighted underneath the primary one, e.g. "this is the
/// declaration that shadows it".
struct Label {
    Span span;
    std::string message;
};

struct Diagnostic {
    Severity severity = Severity::Error;
    /// Stable machine-readable identifier such as "E0201"; empty for notes.
    std::string code;
    std::string message;
    Span primary;
    /// Message rendered directly under the caret run.
    std::string primary_label;
    std::vector<Label> secondary;
    std::vector<std::string> notes;
    std::vector<std::string> helps;
};

/// Fluent builder so call sites read as one expression:
///
///     diags.error("E0201", "cannot apply `+` here", span)
///          .label("operands have incompatible types")
///          .help("convert with `str(x)`");
class DiagnosticBuilder {
public:
    /// Refers to the diagnostic by index rather than by reference: the owning
    /// vector may reallocate if another diagnostic is reported while a builder
    /// is still alive.
    DiagnosticBuilder(std::vector<Diagnostic>& list, std::size_t index)
        : m_list(&list)
        , m_index(index)
    {
    }

    DiagnosticBuilder& label(std::string message)
    {
        target().primary_label = std::move(message);
        return *this;
    }

    DiagnosticBuilder& secondary(Span span, std::string message)
    {
        target().secondary.push_back(Label { span, std::move(message) });
        return *this;
    }

    DiagnosticBuilder& note(std::string message)
    {
        target().notes.push_back(std::move(message));
        return *this;
    }

    DiagnosticBuilder& help(std::string message)
    {
        target().helps.push_back(std::move(message));
        return *this;
    }

private:
    Diagnostic& target() { return (*m_list)[m_index]; }

    std::vector<Diagnostic>* m_list;
    std::size_t m_index;
};

class DiagnosticEngine {
public:
    explicit DiagnosticEngine(const SourceFile& source, bool use_color = false)
        : m_source(&source)
        , m_color(use_color)
    {
    }

    DiagnosticBuilder error(std::string code, std::string message, Span span);
    DiagnosticBuilder warning(std::string code, std::string message, Span span);

    [[nodiscard]] bool has_errors() const { return m_error_count > 0; }
    [[nodiscard]] std::size_t error_count() const { return m_error_count; }
    [[nodiscard]] std::size_t warning_count() const { return m_warning_count; }
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const { return m_all; }

    /// Errors are capped so that a badly desynchronised parse cannot bury the
    /// terminal in cascading noise.
    [[nodiscard]] bool at_error_limit() const { return m_error_count >= kErrorLimit; }

    void set_color(bool enabled) { m_color = enabled; }

    /// Render every diagnostic, in the order reported, followed by a summary.
    void render(std::ostream& out) const;

    /// Render a single diagnostic. Exposed for tests and for the REPL, which
    /// prints as it goes.
    void render_one(std::ostream& out, const Diagnostic& diagnostic) const;

    static constexpr std::size_t kErrorLimit = 25;

private:
    DiagnosticBuilder push(Severity severity, std::string code, std::string message, Span span);

    const SourceFile* m_source;
    bool m_color;
    std::vector<Diagnostic> m_all;
    std::size_t m_error_count = 0;
    std::size_t m_warning_count = 0;
};

/// True when it is safe to emit ANSI colour to the given stream (a TTY and no
/// `NO_COLOR` in the environment).
[[nodiscard]] bool stderr_supports_color();

} // namespace fire
