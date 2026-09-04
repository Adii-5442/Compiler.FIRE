// SPDX-License-Identifier: MIT
#include "fire/diagnostics.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <string_view>

#if defined(_WIN32)
#    include <io.h>
#    define FIRE_ISATTY _isatty
#    define FIRE_FILENO _fileno
#else
#    include <unistd.h>
#    define FIRE_ISATTY isatty
#    define FIRE_FILENO fileno
#endif

namespace fire {
namespace {

    struct Palette {
        const char* reset;
        const char* bold;
        const char* red;
        const char* yellow;
        const char* blue;
        const char* cyan;
    };

    constexpr Palette kPlain { "", "", "", "", "", "" };
    constexpr Palette kAnsi { "\033[0m", "\033[1m", "\033[1;31m", "\033[1;33m", "\033[1;34m",
        "\033[1;36m" };

    /// Expand tabs so the caret line lines up with the rendered source line.
    /// A tab is shown as four spaces, which is what `fire fmt` would emit.
    std::string expand_tabs(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (const char c : text) {
            if (c == '\t') {
                out.append(4, ' ');
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    /// Column of `offset` after tab expansion, 0-based.
    std::uint32_t visual_column(std::string_view line, std::uint32_t column_1_based)
    {
        std::uint32_t visual = 0;
        const std::uint32_t limit = column_1_based > 0 ? column_1_based - 1 : 0;
        for (std::uint32_t i = 0; i < limit; ++i) {
            visual += (i < line.size() && line[i] == '\t') ? 4U : 1U;
        }
        return visual;
    }

} // namespace

const char* severity_name(Severity severity)
{
    switch (severity) {
    case Severity::Error:
        return "error";
    case Severity::Warning:
        return "warning";
    case Severity::Note:
        return "note";
    }
    return "error";
}

bool stderr_supports_color()
{
    if (const char* no_color = std::getenv("NO_COLOR"); no_color != nullptr && *no_color != '\0') {
        return false;
    }
    if (const char* term = std::getenv("TERM");
        term != nullptr && std::string_view { term } == "dumb") {
        return false;
    }
    return FIRE_ISATTY(FIRE_FILENO(stderr)) != 0;
}

DiagnosticBuilder DiagnosticEngine::push(
    Severity severity, std::string code, std::string message, Span span)
{
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    diagnostic.primary = span;
    m_all.push_back(std::move(diagnostic));

    if (severity == Severity::Error) {
        ++m_error_count;
    } else if (severity == Severity::Warning) {
        ++m_warning_count;
    }
    return DiagnosticBuilder { m_all, m_all.size() - 1 };
}

DiagnosticBuilder DiagnosticEngine::error(std::string code, std::string message, Span span)
{
    return push(Severity::Error, std::move(code), std::move(message), span);
}

DiagnosticBuilder DiagnosticEngine::warning(std::string code, std::string message, Span span)
{
    return push(Severity::Warning, std::move(code), std::move(message), span);
}

void DiagnosticEngine::render_one(std::ostream& out, const Diagnostic& diagnostic) const
{
    const Palette& p = m_color ? kAnsi : kPlain;
    const char* accent = diagnostic.severity == Severity::Error ? p.red
        : diagnostic.severity == Severity::Warning              ? p.yellow
                                                                : p.cyan;

    const LineCol at = m_source->locate(diagnostic.primary.begin);
    const std::string_view raw_line = m_source->line_text(at.line);

    // Header: `error[E0201]: message`
    out << accent << severity_name(diagnostic.severity);
    if (!diagnostic.code.empty()) {
        out << '[' << diagnostic.code << ']';
    }
    out << p.reset << p.bold << ": " << diagnostic.message << p.reset << '\n';

    const std::string gutter(std::to_string(at.line).size(), ' ');

    // Location: ` --> path:line:col`
    out << gutter << p.blue << "--> " << p.reset << m_source->path() << ':' << at.line << ':'
        << at.column << '\n';

    out << gutter << p.blue << " |" << p.reset << '\n';
    out << p.blue << at.line << " | " << p.reset << expand_tabs(raw_line) << '\n';

    // Caret run, clamped to the primary line so a multi-line span still points
    // at something sensible.
    const std::uint32_t start = visual_column(raw_line, at.column);
    const LineCol end_at = m_source->locate(diagnostic.primary.end);
    std::uint32_t width = 1;
    if (end_at.line == at.line) {
        const std::uint32_t stop = visual_column(raw_line, end_at.column);
        width = stop > start ? stop - start : 1;
    } else {
        const auto line_width = static_cast<std::uint32_t>(expand_tabs(raw_line).size());
        width = line_width > start ? line_width - start : 1;
    }

    out << gutter << p.blue << " | " << p.reset << std::string(start, ' ') << accent
        << std::string(width, '^');
    if (!diagnostic.primary_label.empty()) {
        out << ' ' << diagnostic.primary_label;
    }
    out << p.reset << '\n';

    for (const Label& label : diagnostic.secondary) {
        const LineCol sec = m_source->locate(label.span.begin);
        const std::string_view sec_line = m_source->line_text(sec.line);
        out << gutter << p.blue << " |" << p.reset << '\n';
        out << p.blue << sec.line << " | " << p.reset << expand_tabs(sec_line) << '\n';
        const std::uint32_t sec_start = visual_column(sec_line, sec.column);
        const LineCol sec_end = m_source->locate(label.span.end);
        const std::uint32_t sec_width = sec_end.line == sec.line
            ? std::max<std::uint32_t>(1, visual_column(sec_line, sec_end.column) - sec_start)
            : 1;
        out << gutter << p.blue << " | " << p.reset << std::string(sec_start, ' ') << p.cyan
            << std::string(sec_width, '-') << ' ' << label.message << p.reset << '\n';
    }

    if (!diagnostic.notes.empty() || !diagnostic.helps.empty()) {
        out << gutter << p.blue << " |" << p.reset << '\n';
    }
    for (const std::string& note : diagnostic.notes) {
        out << gutter << p.blue << " = " << p.reset << p.bold << "note" << p.reset << ": " << note
            << '\n';
    }
    for (const std::string& help : diagnostic.helps) {
        out << gutter << p.blue << " = " << p.reset << p.cyan << "help" << p.reset << ": " << help
            << '\n';
    }
    out << '\n';
}

void DiagnosticEngine::render(std::ostream& out) const
{
    const Palette& p = m_color ? kAnsi : kPlain;
    for (const Diagnostic& diagnostic : m_all) {
        render_one(out, diagnostic);
    }
    if (m_error_count > 0) {
        out << p.red << "error" << p.reset << ": aborting due to " << m_error_count
            << (m_error_count == 1 ? " previous error" : " previous errors");
        if (m_warning_count > 0) {
            out << "; " << m_warning_count
                << (m_warning_count == 1 ? " warning emitted" : " warnings emitted");
        }
        out << '\n';
    } else if (m_warning_count > 0) {
        out << p.yellow << "warning" << p.reset << ": " << m_warning_count
            << (m_warning_count == 1 ? " warning emitted" : " warnings emitted") << '\n';
    }
}

} // namespace fire
