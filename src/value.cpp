// SPDX-License-Identifier: MIT
#include "fire/value.hpp"

#include <cmath>
#include <cstring>
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
    if (value == 0.0) {
        return std::signbit(value) ? "-0.0" : "0.0";
    }

    // Shortest round-tripping form: the fewest significant digits that parse
    // back to exactly this double.
    char buffer[48];
    int precision = 17;
    for (int candidate = 1; candidate <= 17; ++candidate) {
        std::snprintf(buffer, sizeof buffer, "%.*g", candidate, value);
        if (std::strtod(buffer, nullptr) == value) {
            precision = candidate;
            break;
        }
    }

    // %g switches to scientific notation as soon as the exponent reaches the
    // precision, so 100.0 at precision 1 comes out as "1e+02". Widen the
    // precision to cover the integer part for magnitudes a reader would rather
    // see written out; leave genuinely large and small numbers in exponent
    // form.
    char scientific[48];
    std::snprintf(scientific, sizeof scientific, "%.*e", precision - 1, value);
    int exponent = 0;
    if (const char* marker = std::strchr(scientific, 'e'); marker != nullptr) {
        exponent = std::atoi(marker + 1);
    }
    if (exponent >= 0 && exponent < 17 && exponent + 1 > precision) {
        precision = exponent + 1;
    }
    std::snprintf(buffer, sizeof buffer, "%.*g", precision, value);

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
