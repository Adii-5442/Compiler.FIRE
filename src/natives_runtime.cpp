// SPDX-License-Identifier: MIT
//
// Implementations of the builtin functions.
//
// Argument types have already been checked by `natives.cpp` at compile time,
// so these functions assert their shapes rather than validating them. What
// they do check is everything static typing cannot: index bounds, empty
// containers, and strings that do not parse as numbers.
#include "fire/natives.hpp"
#include "fire/value.hpp"
#include "fire/vm.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <string>

namespace fire {
namespace {

    /// xorshift64*. Deterministic, seedable, and good enough for shuffling an
    /// array in an example program.
    std::uint64_t next_random(std::uint64_t& state)
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }

    void write_values(std::ostream& out, const std::vector<Value>& values, bool newline)
    {
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i > 0) {
                out << ' ';
            }
            out << values[i].to_display();
        }
        if (newline) {
            out << '\n';
        }
    }

    std::int64_t length_of(const Value& value)
    {
        if (value.is_str()) {
            return static_cast<std::int64_t>(value.as_str().size());
        }
        return static_cast<std::int64_t>(value.as_array().size());
    }

    std::int64_t parse_int_or_fail(const std::string& text)
    {
        const std::string trimmed = [&] {
            std::size_t begin = 0;
            std::size_t end = text.size();
            while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
                ++begin;
            }
            while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
                --end;
            }
            return text.substr(begin, end - begin);
        }();

        if (trimmed.empty()) {
            VM::fail("cannot convert \"" + text + "\" to `int`", "the string is empty");
        }
        errno = 0;
        char* stop = nullptr;
        const long long parsed = std::strtoll(trimmed.c_str(), &stop, 10);
        if (stop == nullptr || *stop != '\0') {
            VM::fail("cannot convert \"" + text + "\" to `int`",
                "expected an optional sign followed by decimal digits");
        }
        if (errno == ERANGE) {
            VM::fail("\"" + text + "\" does not fit in an `int`");
        }
        return static_cast<std::int64_t>(parsed);
    }

    double parse_float_or_fail(const std::string& text)
    {
        errno = 0;
        char* stop = nullptr;
        const double parsed = std::strtod(text.c_str(), &stop);
        if (stop == text.c_str() || stop == nullptr) {
            VM::fail("cannot convert \"" + text + "\" to `float`");
        }
        while (*stop != '\0' && std::isspace(static_cast<unsigned char>(*stop)) != 0) {
            ++stop;
        }
        if (*stop != '\0') {
            VM::fail("cannot convert \"" + text + "\" to `float`",
                "trailing characters after the number");
        }
        return parsed;
    }

    std::size_t clamp_index(std::int64_t index, std::size_t length)
    {
        if (index < 0) {
            return 0;
        }
        return std::min<std::size_t>(static_cast<std::size_t>(index), length);
    }

    std::vector<Value> split_string(const std::string& text, const std::string& separator)
    {
        std::vector<Value> parts;
        if (separator.empty()) {
            // Splitting on "" yields the individual bytes, which is the useful
            // behaviour for character-by-character work.
            for (const char c : text) {
                parts.push_back(Value::string(std::string(1, c)));
            }
            return parts;
        }
        std::size_t begin = 0;
        while (true) {
            const std::size_t hit = text.find(separator, begin);
            if (hit == std::string::npos) {
                parts.push_back(Value::string(text.substr(begin)));
                return parts;
            }
            parts.push_back(Value::string(text.substr(begin, hit - begin)));
            begin = hit + separator.size();
        }
    }

    std::string replace_all(
        const std::string& text, const std::string& from, const std::string& to)
    {
        if (from.empty()) {
            return text;
        }
        std::string out;
        std::size_t begin = 0;
        while (true) {
            const std::size_t hit = text.find(from, begin);
            if (hit == std::string::npos) {
                out.append(text, begin, std::string::npos);
                return out;
            }
            out.append(text, begin, hit - begin);
            out += to;
            begin = hit + from.size();
        }
    }

} // namespace

Value invoke_native(VM& vm, std::uint16_t id, std::vector<Value>& arguments)
{
    const auto native = static_cast<NativeId>(id);
    const Value void_result = Value::integer(0);

    switch (native) {
    case NativeId::Print:
        write_values(vm.out(), arguments, false);
        return void_result;
    case NativeId::Println:
        write_values(vm.out(), arguments, true);
        return void_result;

    case NativeId::Len:
        return Value::integer(length_of(arguments[0]));

    case NativeId::Push:
        arguments[0].as_array().push_back(arguments[1]);
        return void_result;

    case NativeId::Pop: {
        std::vector<Value>& elements = arguments[0].as_array();
        if (elements.empty()) {
            VM::fail("pop from an empty array", "check `len(xs) > 0` first");
        }
        Value last = std::move(elements.back());
        elements.pop_back();
        return last;
    }

    case NativeId::Insert: {
        std::vector<Value>& elements = arguments[0].as_array();
        const std::int64_t index = arguments[1].as_int();
        if (index < 0 || static_cast<std::size_t>(index) > elements.size()) {
            VM::fail("insert index " + std::to_string(index)
                    + " is out of bounds for an array of length "
                    + std::to_string(elements.size()),
                "an index equal to the length appends");
        }
        elements.insert(elements.begin() + index, arguments[2]);
        return void_result;
    }

    case NativeId::Remove: {
        std::vector<Value>& elements = arguments[0].as_array();
        const std::int64_t index = arguments[1].as_int();
        if (index < 0 || static_cast<std::size_t>(index) >= elements.size()) {
            VM::fail("remove index " + std::to_string(index)
                + " is out of bounds for an array of length " + std::to_string(elements.size()));
        }
        Value removed = elements[static_cast<std::size_t>(index)];
        elements.erase(elements.begin() + index);
        return removed;
    }

    case NativeId::Slice: {
        const std::int64_t from = arguments[1].as_int();
        const std::int64_t to = arguments[2].as_int();
        if (arguments[0].is_str()) {
            const std::string& text = arguments[0].as_str();
            const std::size_t begin = clamp_index(from, text.size());
            const std::size_t end = std::max(begin, clamp_index(to, text.size()));
            return Value::string(text.substr(begin, end - begin));
        }
        const std::vector<Value>& elements = arguments[0].as_array();
        const std::size_t begin = clamp_index(from, elements.size());
        const std::size_t end = std::max(begin, clamp_index(to, elements.size()));
        return Value::array(std::vector<Value> { elements.begin() + static_cast<long>(begin),
            elements.begin() + static_cast<long>(end) });
    }

    case NativeId::ToInt: {
        const Value& value = arguments[0];
        if (value.is_int()) {
            return value;
        }
        if (value.is_bool()) {
            return Value::integer(value.as_bool() ? 1 : 0);
        }
        if (value.is_str()) {
            return Value::integer(parse_int_or_fail(value.as_str()));
        }
        const double number = value.as_float();
        if (!std::isfinite(number) || number >= 9.2233720368547758e18
            || number <= -9.2233720368547758e18) {
            VM::fail("cannot convert " + format_float(number) + " to `int`",
                "the value is outside the range of a 64-bit integer");
        }
        return Value::integer(static_cast<std::int64_t>(std::trunc(number)));
    }

    case NativeId::ToFloat: {
        const Value& value = arguments[0];
        if (value.is_float()) {
            return value;
        }
        if (value.is_str()) {
            return Value::floating(parse_float_or_fail(value.as_str()));
        }
        return Value::floating(static_cast<double>(value.as_int()));
    }

    case NativeId::ToBool: {
        const Value& value = arguments[0];
        if (value.is_bool()) {
            return value;
        }
        if (value.is_int()) {
            return Value::boolean(value.as_int() != 0);
        }
        if (value.is_float()) {
            return Value::boolean(value.as_float() != 0.0);
        }
        return Value::boolean(!value.as_str().empty());
    }

    case NativeId::ToStr:
        return arguments[0].is_str() ? arguments[0] : Value::string(arguments[0].to_display());

    case NativeId::Abs:
        if (arguments[0].is_float()) {
            return Value::floating(std::fabs(arguments[0].as_float()));
        }
        if (arguments[0].as_int() == INT64_MIN) {
            VM::fail("abs(-9223372036854775808) has no representable result");
        }
        return Value::integer(std::abs(arguments[0].as_int()));

    case NativeId::Min:
        if (arguments[0].is_float()) {
            return Value::floating(std::min(arguments[0].as_float(), arguments[1].as_float()));
        }
        return Value::integer(std::min(arguments[0].as_int(), arguments[1].as_int()));

    case NativeId::Max:
        if (arguments[0].is_float()) {
            return Value::floating(std::max(arguments[0].as_float(), arguments[1].as_float()));
        }
        return Value::integer(std::max(arguments[0].as_int(), arguments[1].as_int()));

    case NativeId::Pow: {
        const double base
            = arguments[0].is_float() ? arguments[0].as_float() : static_cast<double>(arguments[0].as_int());
        const double exponent
            = arguments[1].is_float() ? arguments[1].as_float() : static_cast<double>(arguments[1].as_int());
        return Value::floating(std::pow(base, exponent));
    }

    case NativeId::Sqrt: {
        const double value
            = arguments[0].is_float() ? arguments[0].as_float() : static_cast<double>(arguments[0].as_int());
        if (value < 0.0) {
            VM::fail("sqrt of a negative number", "Fire has no complex numbers");
        }
        return Value::floating(std::sqrt(value));
    }

    case NativeId::Floor:
    case NativeId::Ceil:
    case NativeId::Round: {
        if (arguments[0].is_int()) {
            return arguments[0];
        }
        const double value = arguments[0].as_float();
        const double rounded = native == NativeId::Floor ? std::floor(value)
            : native == NativeId::Ceil                   ? std::ceil(value)
                                                         : std::round(value);
        if (!std::isfinite(rounded) || std::fabs(rounded) >= 9.2233720368547758e18) {
            VM::fail("cannot round " + format_float(value) + " into an `int`",
                "the result is outside the range of a 64-bit integer");
        }
        return Value::integer(static_cast<std::int64_t>(rounded));
    }

    case NativeId::Chr: {
        const std::int64_t code = arguments[0].as_int();
        if (code < 0 || code > 0x10FFFF) {
            VM::fail("chr(" + std::to_string(code) + ") is not a Unicode code point",
                "valid code points run from 0 to 1114111");
        }
        std::string out;
        const auto point = static_cast<std::uint32_t>(code);
        if (point <= 0x7F) {
            out.push_back(static_cast<char>(point));
        } else if (point <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (point >> 6)));
            out.push_back(static_cast<char>(0x80 | (point & 0x3F)));
        } else if (point <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (point >> 12)));
            out.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (point & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (point >> 18)));
            out.push_back(static_cast<char>(0x80 | ((point >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (point & 0x3F)));
        }
        return Value::string(std::move(out));
    }

    case NativeId::Ord: {
        const std::string& text = arguments[0].as_str();
        if (text.empty()) {
            VM::fail("ord(\"\") has no character to take the code point of");
        }
        // Decode one UTF-8 scalar so that ord(chr(n)) == n.
        const auto lead = static_cast<unsigned char>(text[0]);
        int extra = 0;
        std::uint32_t point = lead;
        if (lead >= 0xF0) {
            extra = 3;
            point = lead & 0x07U;
        } else if (lead >= 0xE0) {
            extra = 2;
            point = lead & 0x0FU;
        } else if (lead >= 0xC0) {
            extra = 1;
            point = lead & 0x1FU;
        }
        for (int i = 1; i <= extra && static_cast<std::size_t>(i) < text.size(); ++i) {
            point = (point << 6) | (static_cast<unsigned char>(text[static_cast<std::size_t>(i)]) & 0x3FU);
        }
        return Value::integer(static_cast<std::int64_t>(point));
    }

    case NativeId::Find: {
        const std::size_t hit = arguments[0].as_str().find(arguments[1].as_str());
        return Value::integer(hit == std::string::npos ? -1 : static_cast<std::int64_t>(hit));
    }

    case NativeId::Split:
        return Value::array(split_string(arguments[0].as_str(), arguments[1].as_str()));

    case NativeId::Join: {
        const std::vector<Value>& parts = arguments[0].as_array();
        const std::string& separator = arguments[1].as_str();
        std::string out;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                out += separator;
            }
            out += parts[i].as_str();
        }
        return Value::string(std::move(out));
    }

    case NativeId::Trim: {
        const std::string& text = arguments[0].as_str();
        std::size_t begin = 0;
        std::size_t end = text.size();
        while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
            ++begin;
        }
        while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
            --end;
        }
        return Value::string(text.substr(begin, end - begin));
    }

    case NativeId::Upper:
    case NativeId::Lower: {
        std::string text = arguments[0].as_str();
        for (char& c : text) {
            c = native == NativeId::Upper
                ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return Value::string(std::move(text));
    }

    case NativeId::Replace:
        return Value::string(
            replace_all(arguments[0].as_str(), arguments[1].as_str(), arguments[2].as_str()));

    case NativeId::Repeat: {
        const std::int64_t count = arguments[1].as_int();
        if (count < 0) {
            VM::fail("repeat count " + std::to_string(count) + " is negative");
        }
        const std::string& unit = arguments[0].as_str();
        // Refuse to build something that would exhaust memory; the limit is
        // arbitrary but far past any legitimate use.
        if (unit.size() * static_cast<std::size_t>(count) > (1ULL << 28)) {
            VM::fail("repeat would produce a string larger than 256 MiB");
        }
        std::string out;
        out.reserve(unit.size() * static_cast<std::size_t>(count));
        for (std::int64_t i = 0; i < count; ++i) {
            out += unit;
        }
        return Value::string(std::move(out));
    }

    case NativeId::Input: {
        std::string line;
        if (!std::getline(vm.in(), line)) {
            return Value::string(std::string {});
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        return Value::string(std::move(line));
    }

    case NativeId::Eof: {
        std::istream& stream = vm.in();
        if (!stream.good()) {
            return Value::boolean(true);
        }
        // peek() sets eofbit when there is nothing left, which is the only
        // portable way to answer this before the next read.
        return Value::boolean(stream.peek() == std::char_traits<char>::eof());
    }

    case NativeId::Exit: {
        vm.out().flush();
        throw ExitSignal { static_cast<int>(arguments[0].as_int() & 0xFF) };
    }

    case NativeId::Assert: {
        if (!arguments[0].as_bool()) {
            const std::string detail
                = arguments.size() > 1 ? arguments[1].as_str() : std::string { "assertion failed" };
            VM::fail(detail, "an `assert` in this program did not hold");
        }
        return void_result;
    }

    case NativeId::Panic:
        VM::fail(arguments[0].as_str());

    case NativeId::Clock: {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto nanoseconds
            = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        return Value::floating(static_cast<double>(nanoseconds) / 1e9);
    }

    case NativeId::Args: {
        std::vector<Value> values;
        values.reserve(vm.program_args().size());
        for (const std::string& argument : vm.program_args()) {
            values.push_back(Value::string(argument));
        }
        return Value::array(std::move(values));
    }

    case NativeId::Seed:
        // A zero state would make xorshift produce only zeroes.
        vm.rng_state() = static_cast<std::uint64_t>(arguments[0].as_int()) | 1ULL;
        return void_result;

    case NativeId::RandInt: {
        const std::int64_t low = arguments[0].as_int();
        const std::int64_t high = arguments[1].as_int();
        if (low > high) {
            VM::fail("rand_int(" + std::to_string(low) + ", " + std::to_string(high)
                    + ") has an empty range",
                "the low bound must not exceed the high bound");
        }
        const auto span = static_cast<std::uint64_t>(high - low) + 1ULL;
        const std::uint64_t draw = next_random(vm.rng_state());
        return Value::integer(low + static_cast<std::int64_t>(span == 0 ? draw : draw % span));
    }

    case NativeId::Count:
        break;
    }

    VM::fail("internal error: unknown builtin id " + std::to_string(id));
}

} // namespace fire
