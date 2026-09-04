// SPDX-License-Identifier: MIT
#include "fire/source.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace fire {

SourceFile::SourceFile(std::string path, std::string text)
    : m_path(std::move(path))
    , m_text(std::move(text))
{
    index_lines();
}

std::optional<SourceFile> SourceFile::from_disk(const std::string& path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        return std::nullopt;
    }
    return SourceFile { path, buffer.str() };
}

SourceFile SourceFile::from_string(std::string name, std::string text)
{
    return SourceFile { std::move(name), std::move(text) };
}

void SourceFile::index_lines()
{
    m_line_starts.clear();
    m_line_starts.push_back(0);
    for (std::size_t i = 0; i < m_text.size(); ++i) {
        if (m_text[i] == '\n') {
            m_line_starts.push_back(static_cast<std::uint32_t>(i + 1));
        }
    }
}

LineCol SourceFile::locate(std::uint32_t offset) const
{
    if (m_text.empty()) {
        return LineCol { 1, 1 };
    }
    const auto clamped = std::min<std::uint32_t>(offset, static_cast<std::uint32_t>(m_text.size()));

    // The first line start strictly greater than `clamped` is one past the
    // line we want, so step back once.
    auto it = std::upper_bound(m_line_starts.begin(), m_line_starts.end(), clamped);
    const auto index = static_cast<std::uint32_t>(it - m_line_starts.begin() - 1);

    LineCol out;
    out.line = index + 1;
    out.column = clamped - m_line_starts[index] + 1;
    return out;
}

std::string_view SourceFile::line_text(std::uint32_t line) const
{
    if (line == 0 || line > line_count()) {
        return { };
    }
    const std::uint32_t begin = m_line_starts[line - 1];
    const std::uint32_t end = line < line_count()
        ? m_line_starts[line] // includes the '\n' we are about to trim
        : static_cast<std::uint32_t>(m_text.size());

    std::string_view view { m_text.data() + begin, end - begin };
    while (!view.empty() && (view.back() == '\n' || view.back() == '\r')) {
        view.remove_suffix(1);
    }
    return view;
}

std::string_view SourceFile::snippet(Span span) const
{
    const auto size = static_cast<std::uint32_t>(m_text.size());
    const std::uint32_t begin = std::min(span.begin, size);
    const std::uint32_t end = std::clamp(span.end, begin, size);
    return std::string_view { m_text.data() + begin, end - begin };
}

} // namespace fire
