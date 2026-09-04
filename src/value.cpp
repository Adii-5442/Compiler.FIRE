// SPDX-License-Identifier: MIT
#include "fire/value.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace fire {

const char* Value::tag_name() const
{
    switch (tag()) {
    case ValueTag::Int:
        return "int";
    case ValueTag::Float:
        return "float";
    case ValueTag::Bool:
        return "bool";
    case ValueTag::Str:
        return "str";
    case ValueTag::Array:
        return "array";
    }
    return "?";
}

std::string format_float(double value)
{
    if (std::isnan(value)) {
        return "nan";
    }
    if (std::isinf(value)) {
        return value > 0 ? "inf" : "-inf";
    }

    // %.17g always round-trips but is ugly; try increasing precision until the
    // text parses back to the same double, which yields the shortest exact
    // rendering.
    char buffer[40];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof buffer, "%.*g", precision, value);
        if (std::strtod(buffer, nullptr) == value) {
            break;
        }
    }
    std::string text { buffer };

    // Make it obvious that this is a float and not an int.
    if (text.find_first_of(".eEn") == std::string::npos) {
        text += ".0";
    }
    return text;
}

namespace {

    void escape_into(std::string& out, const std::string& text)
    {
        out.push_back('"');
        for (const char c : text) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\r':
                out += "\\r";
                break;
            default:
                out.push_back(c);
                break;
            }
        }
        out.push_back('"');
    }

    std::string render_array(const std::vector<Value>& elements)
    {
        std::string out = "[";
        for (std::size_t i = 0; i < elements.size(); ++i) {
            if (i > 0) {
                out += ", ";
            }
            out += elements[i].to_repr();
        }
        out += ']';
        return out;
    }

} // namespace

std::string Value::to_display() const
{
    switch (tag()) {
    case ValueTag::Int:
        return std::to_string(as_int());
    case ValueTag::Float:
        return format_float(as_float());
    case ValueTag::Bool:
        return as_bool() ? "true" : "false";
    case ValueTag::Str:
        return as_str();
    case ValueTag::Array:
        return render_array(as_array());
    }
    return {};
}

std::string Value::to_repr() const
{
    if (is_str()) {
        std::string out;
        escape_into(out, as_str());
        return out;
    }
    return to_display();
}

bool Value::equals(const Value& other) const
{
    if (tag() != other.tag()) {
        return false;
    }
    switch (tag()) {
    case ValueTag::Int:
        return as_int() == other.as_int();
    case ValueTag::Float:
        return as_float() == other.as_float();
    case ValueTag::Bool:
        return as_bool() == other.as_bool();
    case ValueTag::Str:
        // Identical shared strings are common after a copy; check first.
        return str_ref() == other.str_ref() || as_str() == other.as_str();
    case ValueTag::Array: {
        if (array_ref() == other.array_ref()) {
            return true;
        }
        const std::vector<Value>& left = as_array();
        const std::vector<Value>& right = other.as_array();
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t i = 0; i < left.size(); ++i) {
            if (!left[i].equals(right[i])) {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

} // namespace fire
