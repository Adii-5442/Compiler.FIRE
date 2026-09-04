// SPDX-License-Identifier: MIT
//
// Source text ownership and byte-offset <-> line/column mapping.
//
// Every token, AST node and diagnostic in the compiler refers to source
// locations as a `Span` of byte offsets. Keeping spans as plain integers makes
// them cheap to copy and store; turning one back into "line 12, column 5" is
// only needed when a diagnostic is actually rendered, and that is what
// `SourceFile` provides.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fire {

/// A half-open byte range `[begin, end)` inside a single `SourceFile`.
struct Span {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;

    constexpr Span() = default;
    constexpr Span(std::uint32_t b, std::uint32_t e)
        : begin(b)
        , end(e)
    {
    }

    [[nodiscard]] constexpr std::uint32_t length() const { return end - begin; }
    [[nodiscard]] constexpr bool empty() const { return end <= begin; }

    /// The smallest span covering both operands. Used to give a compound
    /// expression the extent of its outermost sub-expressions.
    [[nodiscard]] constexpr Span merge(Span other) const
    {
        return Span { begin < other.begin ? begin : other.begin,
            end > other.end ? end : other.end };
    }
};

/// A 1-based human coordinate, the form used in diagnostics.
struct LineCol {
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

/// An immutable in-memory source file plus a precomputed line index.
class SourceFile {
public:
    SourceFile() = default;
    SourceFile(std::string path, std::string text);

    /// Read a file from disk. Returns `std::nullopt` when it cannot be opened.
    static std::optional<SourceFile> from_disk(const std::string& path);

    /// Wrap text that did not come from disk (the REPL, tests, `--eval`).
    static SourceFile from_string(std::string name, std::string text);

    [[nodiscard]] const std::string& path() const { return m_path; }
    [[nodiscard]] const std::string& text() const { return m_text; }
    [[nodiscard]] std::size_t size() const { return m_text.size(); }

    /// Number of lines; a file always has at least one.
    [[nodiscard]] std::uint32_t line_count() const
    {
        return static_cast<std::uint32_t>(m_line_starts.size());
    }

    /// Convert a byte offset into a 1-based line/column pair. Offsets past the
    /// end of the file clamp to the final position rather than misbehaving,
    /// so a diagnostic about an unexpected EOF still renders.
    [[nodiscard]] LineCol locate(std::uint32_t offset) const;

    /// The text of a 1-based line, without its terminator.
    [[nodiscard]] std::string_view line_text(std::uint32_t line) const;

    /// The text covered by `span`, clamped to the file.
    [[nodiscard]] std::string_view snippet(Span span) const;

private:
    void index_lines();

    std::string m_path;
    std::string m_text;
    /// Byte offset at which each line begins; always starts with 0.
    std::vector<std::uint32_t> m_line_starts;
};

} // namespace fire
