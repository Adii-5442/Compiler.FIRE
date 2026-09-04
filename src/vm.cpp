// SPDX-License-Identifier: MIT
#include "fire/vm.hpp"

#include "fire/disasm.hpp"
#include "fire/natives.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <variant>

namespace fire {

VM::VM(const Module& module, Options options)
    : m_module(module)
    , m_options(std::move(options))
{
    m_globals.assign(m_module.global_count, Value::integer(0));
    m_stack.reserve(1024);
    m_frames.reserve(64);
    m_native_args.reserve(8);
}

void VM::adopt_globals(std::vector<Value> globals)
{
    m_globals = std::move(globals);
    // A fragment may have declared new globals since the last run.
    m_globals.resize(m_module.global_count, Value::integer(0));
}

std::ostream& VM::out()
{
    return m_options.out != nullptr ? *m_options.out : std::cout;
}
std::ostream& VM::err()
{
    return m_options.err != nullptr ? *m_options.err : std::cerr;
}
std::istream& VM::in()
{
    return m_options.in != nullptr ? *m_options.in : std::cin;
}

void VM::fail(std::string message, std::string help)
{
    throw RuntimeError { std::move(message), std::move(help) };
}

Value VM::pop()
{
    Value value = std::move(m_stack.back());
    m_stack.pop_back();
    return value;
}

Value& VM::peek(std::size_t distance)
{
    return m_stack[m_stack.size() - 1 - distance];
}

std::uint8_t VM::read_u8(Frame& frame) { return frame.function->chunk.byte_at(frame.ip++); }

std::uint16_t VM::read_u16(Frame& frame)
{
    const std::uint16_t value = frame.function->chunk.read_u16(frame.ip);
    frame.ip += 2;
    return value;
}

std::uint32_t VM::read_u32(Frame& frame)
{
    const std::uint32_t value = frame.function->chunk.read_u32(frame.ip);
    frame.ip += 4;
    return value;
}

void VM::call_function(std::uint16_t index, std::uint8_t argument_count)
{
    if (m_frames.size() >= m_options.max_call_depth) {
        fail("call stack overflow after " + std::to_string(m_frames.size()) + " nested calls",
            "this usually means a recursion that never reaches its base case");
    }
    const CompiledFunction& function = m_module.functions[index];

    Frame frame;
    frame.function = &function;
    frame.ip = 0;
    frame.base = m_stack.size() - argument_count;
    // Slots beyond the parameters start life as 0; the compiler always writes
    // a local before reading it, so the value is never observable.
    m_stack.resize(frame.base + function.local_count);
    m_frames.push_back(frame);
}

void VM::call_native(std::uint16_t id, std::uint8_t argument_count)
{
    m_native_args.clear();
    for (std::size_t i = 0; i < argument_count; ++i) {
        m_native_args.push_back(std::move(m_stack[m_stack.size() - argument_count + i]));
    }
    m_stack.resize(m_stack.size() - argument_count);
    push(invoke_native(*this, id, m_native_args));
}

void VM::execute()
{
    // Cached for the duration of one frame; refreshed wherever the frame stack
    // changes, which is only CALL, RET and RET_VOID.
    Frame* frame = &m_frames.back();

    while (true) {
        if (m_options.trace) {
            disassemble_instruction(err(), frame->function->chunk, frame->ip, m_options.source);
        }

        const auto op = static_cast<OpCode>(read_u8(*frame));
        switch (op) {
        case OpCode::Constant:
            push(frame->function->chunk.constants()[read_u16(*frame)]);
            break;
        case OpCode::PushTrue:
            push(Value::boolean(true));
            break;
        case OpCode::PushFalse:
            push(Value::boolean(false));
            break;
        case OpCode::Pop:
            m_stack.pop_back();
            break;
        case OpCode::Dup:
            push(peek());
            break;
        case OpCode::Dup2: {
            const Value first = peek(1);
            const Value second = peek(0);
            push(first);
            push(second);
            break;
        }

        case OpCode::GetLocal:
            push(m_stack[frame->base + read_u16(*frame)]);
            break;
        case OpCode::SetLocal: {
            const std::uint16_t slot = read_u16(*frame);
            m_stack[frame->base + slot] = pop();
            break;
        }
        case OpCode::GetGlobal:
            push(m_globals[read_u16(*frame)]);
            break;
        case OpCode::SetGlobal: {
            const std::uint16_t slot = read_u16(*frame);
            m_globals[slot] = pop();
            break;
        }

        case OpCode::AddInt: {
            const std::int64_t right = pop().as_int();
            peek() = Value::integer(static_cast<std::int64_t>(
                static_cast<std::uint64_t>(peek().as_int()) + static_cast<std::uint64_t>(right)));
            break;
        }
        case OpCode::SubInt: {
            const std::int64_t right = pop().as_int();
            peek() = Value::integer(static_cast<std::int64_t>(
                static_cast<std::uint64_t>(peek().as_int()) - static_cast<std::uint64_t>(right)));
            break;
        }
        case OpCode::MulInt: {
            const std::int64_t right = pop().as_int();
            peek() = Value::integer(static_cast<std::int64_t>(
                static_cast<std::uint64_t>(peek().as_int()) * static_cast<std::uint64_t>(right)));
            break;
        }
        case OpCode::DivInt: {
            const std::int64_t right = pop().as_int();
            if (right == 0) {
                fail("division by zero", "guard the divisor with `if d != 0 { }`");
            }
            const std::int64_t left = peek().as_int();
            if (left == INT64_MIN && right == -1) {
                fail("integer overflow in division",
                    "-9223372036854775808 / -1 has no representable result");
            }
            peek() = Value::integer(left / right);
            break;
        }
        case OpCode::ModInt: {
            const std::int64_t right = pop().as_int();
            if (right == 0) {
                fail("remainder by zero", "guard the divisor with `if d != 0 { }`");
            }
            const std::int64_t left = peek().as_int();
            peek() = Value::integer(left == INT64_MIN && right == -1 ? 0 : left % right);
            break;
        }
        case OpCode::NegInt:
            peek() = Value::integer(
                static_cast<std::int64_t>(0ULL - static_cast<std::uint64_t>(peek().as_int())));
            break;

        case OpCode::AddFloat: {
            const double right = pop().as_float();
            peek() = Value::floating(peek().as_float() + right);
            break;
        }
        case OpCode::SubFloat: {
            const double right = pop().as_float();
            peek() = Value::floating(peek().as_float() - right);
            break;
        }
        case OpCode::MulFloat: {
            const double right = pop().as_float();
            peek() = Value::floating(peek().as_float() * right);
            break;
        }
        case OpCode::DivFloat: {
            const double right = pop().as_float();
            // Float division by zero yields inf/nan, as IEEE-754 specifies.
            peek() = Value::floating(peek().as_float() / right);
            break;
        }
        case OpCode::NegFloat:
            peek() = Value::floating(-peek().as_float());
            break;

        case OpCode::ConcatStr: {
            const Value right = pop();
            peek() = Value::string(peek().as_str() + right.as_str());
            break;
        }

        case OpCode::BitAnd: {
            const std::int64_t right = pop().as_int();
            peek() = Value::integer(peek().as_int() & right);
            break;
        }
        case OpCode::BitOr: {
            const std::int64_t right = pop().as_int();
            peek() = Value::integer(peek().as_int() | right);
            break;
        }
        case OpCode::BitXor: {
            const std::int64_t right = pop().as_int();
            peek() = Value::integer(peek().as_int() ^ right);
            break;
        }
        case OpCode::ShiftLeft: {
            const std::int64_t right = pop().as_int();
            if (right < 0 || right > 63) {
                fail("shift amount " + std::to_string(right) + " is out of range",
                    "a shift of an `int` must move between 0 and 63 bits");
            }
            peek() = Value::integer(
                static_cast<std::int64_t>(static_cast<std::uint64_t>(peek().as_int()) << right));
            break;
        }
        case OpCode::ShiftRight: {
            const std::int64_t right = pop().as_int();
            if (right < 0 || right > 63) {
                fail("shift amount " + std::to_string(right) + " is out of range",
                    "a shift of an `int` must move between 0 and 63 bits");
            }
            // Arithmetic shift: the sign bit is replicated.
            peek() = Value::integer(peek().as_int() >> right);
            break;
        }
        case OpCode::BitNot:
            peek() = Value::integer(~peek().as_int());
            break;

        case OpCode::Not:
            peek() = Value::boolean(!peek().as_bool());
            break;

        case OpCode::Equal: {
            const Value right = pop();
            peek() = Value::boolean(peek().equals(right));
            break;
        }
        case OpCode::NotEqual: {
            const Value right = pop();
            peek() = Value::boolean(!peek().equals(right));
            break;
        }

#define FIRE_COMPARE(opcode, accessor, comparison)                                                 \
    case OpCode::opcode: {                                                                         \
        const auto right = pop().accessor();                                                       \
        peek() = Value::boolean(peek().accessor() comparison right);                               \
        break;                                                                                     \
    }
            FIRE_COMPARE(LessInt, as_int, <)
            FIRE_COMPARE(LessEqualInt, as_int, <=)
            FIRE_COMPARE(GreaterInt, as_int, >)
            FIRE_COMPARE(GreaterEqualInt, as_int, >=)
            FIRE_COMPARE(LessFloat, as_float, <)
            FIRE_COMPARE(LessEqualFloat, as_float, <=)
            FIRE_COMPARE(GreaterFloat, as_float, >)
            FIRE_COMPARE(GreaterEqualFloat, as_float, >=)
#undef FIRE_COMPARE

        case OpCode::LessStr: {
            const Value right = pop();
            peek() = Value::boolean(peek().as_str() < right.as_str());
            break;
        }
        case OpCode::LessEqualStr: {
            const Value right = pop();
            peek() = Value::boolean(peek().as_str() <= right.as_str());
            break;
        }
        case OpCode::GreaterStr: {
            const Value right = pop();
            peek() = Value::boolean(peek().as_str() > right.as_str());
            break;
        }
        case OpCode::GreaterEqualStr: {
            const Value right = pop();
            peek() = Value::boolean(peek().as_str() >= right.as_str());
            break;
        }

        case OpCode::MakeArray: {
            const std::uint16_t count = read_u16(*frame);
            std::vector<Value> elements;
            elements.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                elements.push_back(std::move(m_stack[m_stack.size() - count + i]));
            }
            m_stack.resize(m_stack.size() - count);
            push(Value::array(std::move(elements)));
            break;
        }
        case OpCode::IndexGet: {
            const std::int64_t index = pop().as_int();
            const Value target = pop();
            if (target.is_str()) {
                const std::string& text = target.as_str();
                if (index < 0 || static_cast<std::size_t>(index) >= text.size()) {
                    fail("string index " + std::to_string(index)
                            + " is out of bounds for a string of length "
                            + std::to_string(text.size()),
                        "valid indices run from 0 to len(s) - 1");
                }
                push(Value::string(std::string(1, text[static_cast<std::size_t>(index)])));
            } else {
                const std::vector<Value>& elements = target.as_array();
                if (index < 0 || static_cast<std::size_t>(index) >= elements.size()) {
                    fail("index " + std::to_string(index)
                            + " is out of bounds for an array of length "
                            + std::to_string(elements.size()),
                        "valid indices run from 0 to len(xs) - 1");
                }
                push(elements[static_cast<std::size_t>(index)]);
            }
            break;
        }
        case OpCode::IndexSet: {
            Value value = pop();
            const std::int64_t index = pop().as_int();
            const Value target = pop();
            std::vector<Value>& elements = target.as_array();
            if (index < 0 || static_cast<std::size_t>(index) >= elements.size()) {
                fail("index " + std::to_string(index) + " is out of bounds for an array of length "
                        + std::to_string(elements.size()),
                    "use `push(xs, v)` to grow an array");
            }
            elements[static_cast<std::size_t>(index)] = std::move(value);
            break;
        }

        case OpCode::Jump:
            frame->ip = read_u32(*frame);
            break;
        case OpCode::JumpIfFalse: {
            const std::uint32_t target = read_u32(*frame);
            if (!pop().as_bool()) {
                frame->ip = target;
            }
            break;
        }
        case OpCode::JumpIfFalsePeek: {
            const std::uint32_t target = read_u32(*frame);
            if (!peek().as_bool()) {
                frame->ip = target;
            }
            break;
        }
        case OpCode::JumpIfTruePeek: {
            const std::uint32_t target = read_u32(*frame);
            if (peek().as_bool()) {
                frame->ip = target;
            }
            break;
        }

        case OpCode::Call: {
            const std::uint16_t index = read_u16(*frame);
            const std::uint8_t argument_count = read_u8(*frame);
            call_function(index, argument_count);
            frame = &m_frames.back(); // the frame stack moved
            break;
        }
        case OpCode::CallNative: {
            const std::uint16_t id = read_u16(*frame);
            const std::uint8_t argument_count = read_u8(*frame);
            call_native(id, argument_count);
            break;
        }
        case OpCode::Return: {
            Value result = pop();
            const std::size_t base = frame->base;
            m_frames.pop_back();
            m_stack.resize(base);
            push(std::move(result));
            frame = &m_frames.back();
            break;
        }
        case OpCode::ReturnVoid: {
            const std::size_t base = frame->base;
            m_frames.pop_back();
            m_stack.resize(base);
            // A void call still leaves one value, so every call site can pop
            // unconditionally.
            push(Value::integer(0));
            frame = &m_frames.back();
            break;
        }
        case OpCode::Halt:
            return;
        }
    }
}

void VM::report(const RuntimeError& error) const
{
    std::ostream& stream = m_options.err != nullptr ? *m_options.err : std::cerr;
    if (m_options.source == nullptr || m_frames.empty()) {
        stream << "runtime error: " << error.message << '\n';
        return;
    }

    DiagnosticEngine engine { *m_options.source, m_options.color };
    const Frame& frame = m_frames.back();
    // ip has already advanced past the operands of the failing instruction, so
    // step back to the start of the instruction where possible.
    const std::size_t at = frame.ip > 0 ? frame.ip - 1 : 0;
    auto builder =
        engine.error("R0001", "runtime error: " + error.message, frame.function->chunk.span_at(at));
    builder.label("while evaluating this");
    if (!error.help.empty()) {
        builder.help(error.help);
    }
    engine.render_one(stream, engine.diagnostics().front());

    if (m_frames.size() > 1) {
        stream << "stack backtrace:\n";
        std::size_t depth = 0;
        for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it, ++depth) {
            const std::size_t offset = it->ip > 0 ? it->ip - 1 : 0;
            const LineCol location =
                m_options.source->locate(it->function->chunk.span_at(offset).begin);
            stream << "  " << depth << ": " << it->function->name << " at "
                   << m_options.source->path() << ':' << location.line << ':' << location.column
                   << '\n';
        }
        stream << '\n';
    }
}

int VM::run()
{
    m_stack.clear();
    m_frames.clear();

    Frame script;
    script.function = &m_module.script;
    script.ip = 0;
    script.base = 0;
    m_stack.resize(m_module.script.local_count);
    m_frames.push_back(script);

    try {
        execute();
    } catch (const ExitSignal& signal) {
        return signal.code;
    } catch (const RuntimeError& error) {
        report(error);
        return 70; // EX_SOFTWARE
    } catch (const std::bad_variant_access&) {
        // Reaching here means an opcode found a value whose tag the type
        // checker said was impossible: a compiler bug, not a program error.
        report(RuntimeError { "internal error: a value had an unexpected runtime type",
            "this is a bug in the Fire compiler; please report it with the program that "
            "triggered it" });
        return 70;
    }
    return 0;
}

} // namespace fire
