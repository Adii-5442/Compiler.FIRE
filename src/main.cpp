// SPDX-License-Identifier: MIT
//
// The `fire` command-line driver.
#include "fire/backend_x86_64.hpp"
#include "fire/disasm.hpp"
#include "fire/driver.hpp"
#include "fire/natives.hpp"
#include "fire/repl.hpp"
#include "fire/version.hpp"
#include "fire/vm.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace fire;

constexpr int kExitOk = 0;
constexpr int kExitCompileError = 1;
constexpr int kExitUsage = 64; // EX_USAGE

void print_usage(std::ostream& out)
{
    out << kLanguageName << " " << version_string() << " — a compiler for the " << kSourceExtension
        << " language\n\n"
           "USAGE\n"
           "  fire <command> [options] <file"
        << kSourceExtension
        << "> [-- program args]\n"
           "  fire <file"
        << kSourceExtension
        << ">            shorthand for `fire run`\n\n"
           "COMMANDS\n"
           "  run <file>       compile and execute on the Fire VM\n"
           "  build <file>     compile to a native x86-64 executable\n"
           "  check <file>     type-check only, produce no output\n"
           "  emit <file>      print an intermediate form (see --tokens/--ast/...)\n"
           "  repl             start an interactive session\n"
           "  builtins         list the builtin functions\n"
           "  version          print the version\n"
           "  help             print this message\n\n"
           "EMIT FORMS\n"
           "  --tokens         the token stream\n"
           "  --ast            the typed syntax tree\n"
           "  --bytecode       disassembled Fire bytecode (default)\n"
           "  --asm            x86-64 assembly, NASM syntax\n\n"
           "OPTIONS\n"
           "  -o <path>        output path for `build` and `emit`\n"
           "  --trace          disassemble each instruction as the VM runs it\n"
           "  --color/--no-color\n"
           "                   force diagnostic colouring on or off\n"
           "  -S               with `build`, stop after writing the assembly\n"
           "  -h, --help       print this message\n\n"
           "EXAMPLES\n"
           "  fire run examples/fizzbuzz.fire\n"
           "  fire build examples/native.fire -o collatz && ./collatz\n"
           "  fire emit --ast examples/hello.fire\n";
}

void print_builtins(std::ostream& out)
{
    out << "Fire builtin functions\n\n";
    for (const NativeInfo& native : native_table()) {
        std::string arity = std::to_string(native.min_arity);
        if (native.max_arity == NativeInfo::kVariadic) {
            arity += "+";
        } else if (native.max_arity != native.min_arity) {
            arity += ".." + std::to_string(native.max_arity);
        }
        out << "  " << std::left << std::setw(12) << native.name << std::setw(6) << arity
            << native.summary << '\n';
    }
    out << "\n"
        << native_table().size()
        << " builtins. See docs/language-reference.md for "
           "signatures.\n";
}

struct Options {
    std::string command;
    std::string input;
    std::string output;
    std::vector<std::string> program_args;
    bool trace = false;
    bool assembly_only = false;
    enum class EmitForm { Bytecode, Tokens, Ast, Assembly } emit = EmitForm::Bytecode;
    bool color = stderr_supports_color();
};

/// Read a source file, reporting a clean error rather than a stack trace.
std::optional<SourceFile> load(const std::string& path)
{
    if (auto source = SourceFile::from_disk(path)) {
        return source;
    }
    std::cerr << "fire: cannot read '" << path << "': no such file, or it is not readable\n";
    return std::nullopt;
}

/// Warn when a file does not carry the language's extension. Not fatal: the
/// tests and one-off scratch files legitimately do not.
void warn_extension(const std::string& path)
{
    const std::string_view name { path };
    if (name.size() < 5 || name.substr(name.size() - 5) != kSourceExtension) {
        std::cerr << "fire: warning: '" << path << "' does not end in " << kSourceExtension << '\n';
    }
}

int command_run(const Options& options)
{
    auto source = load(options.input);
    if (!source) {
        return kExitUsage;
    }
    warn_extension(options.input);

    Compilation compilation { std::move(*source), options.color };
    const bool ok = compilation.compile();
    compilation.report(std::cerr);
    if (!ok) {
        return kExitCompileError;
    }

    VM::Options vm_options;
    vm_options.source = &compilation.source();
    vm_options.program_args = options.program_args;
    vm_options.trace = options.trace;
    vm_options.color = options.color;

    VM vm { compilation.module(), std::move(vm_options) };
    const int status = vm.run();
    std::cout.flush();
    return status;
}

int command_check(const Options& options)
{
    auto source = load(options.input);
    if (!source) {
        return kExitUsage;
    }
    Compilation compilation { std::move(*source), options.color };
    const bool ok = compilation.analyze();
    compilation.report(std::cerr);
    if (ok) {
        std::cerr << "ok: " << options.input << " type-checks\n";
    }
    return ok ? kExitOk : kExitCompileError;
}

int command_emit(const Options& options)
{
    auto source = load(options.input);
    if (!source) {
        return kExitUsage;
    }
    Compilation compilation { std::move(*source), options.color };

    const bool needs_codegen =
        options.emit == Options::EmitForm::Bytecode || options.emit == Options::EmitForm::Assembly;
    const bool ok = needs_codegen ? compilation.compile() : compilation.analyze();
    compilation.report(std::cerr);
    if (!ok) {
        return kExitCompileError;
    }

    std::ofstream file;
    std::ostream* out = &std::cout;
    if (!options.output.empty()) {
        file.open(options.output);
        if (!file) {
            std::cerr << "fire: cannot write '" << options.output << "'\n";
            return kExitUsage;
        }
        out = &file;
    }

    switch (options.emit) {
    case Options::EmitForm::Tokens:
        dump_tokens(*out, compilation.source(), compilation.tokens());
        break;
    case Options::EmitForm::Ast:
        dump_ast(*out, compilation.program());
        break;
    case Options::EmitForm::Bytecode:
        disassemble_module(*out, compilation.module(), &compilation.source());
        break;
    case Options::EmitForm::Assembly: {
        DiagnosticEngine& diagnostics = compilation.diagnostics();
        const std::string assembly =
            emit_x86_64(compilation.program(), compilation.types(), diagnostics);
        if (diagnostics.has_errors()) {
            compilation.report(std::cerr);
            return kExitCompileError;
        }
        *out << assembly;
        break;
    }
    }
    return kExitOk;
}

int command_build(const Options& options)
{
    auto source = load(options.input);
    if (!source) {
        return kExitUsage;
    }
    warn_extension(options.input);

    Compilation compilation { std::move(*source), options.color };
    if (!compilation.analyze()) {
        compilation.report(std::cerr);
        return kExitCompileError;
    }

    std::string stem = options.input;
    if (const std::size_t dot = stem.rfind('.'); dot != std::string::npos) {
        stem = stem.substr(0, dot);
    }
    if (const std::size_t slash = stem.rfind('/'); slash != std::string::npos) {
        stem = stem.substr(slash + 1);
    }
    const std::string output = options.output.empty() ? stem : options.output;

    NativeBuildResult result = build_native(compilation.program(), compilation.types(),
        compilation.diagnostics(), output, options.assembly_only);
    compilation.report(std::cerr);
    if (!result.ok) {
        if (!result.message.empty()) {
            std::cerr << "fire: " << result.message << '\n';
        }
        return kExitCompileError;
    }
    std::cerr << "wrote " << result.artifact << '\n';
    return kExitOk;
}

bool parse_arguments(int argc, char** argv, Options& options)
{
    std::vector<std::string> raw;
    raw.reserve(static_cast<std::size_t>(argc));
    for (int i = 1; i < argc; ++i) {
        raw.emplace_back(argv[i]);
    }

    bool after_separator = false;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const std::string& argument = raw[i];
        if (after_separator) {
            options.program_args.push_back(argument);
            continue;
        }
        if (argument == "--") {
            after_separator = true;
        } else if (argument == "-h" || argument == "--help") {
            options.command = "help";
        } else if (argument == "-v" || argument == "--version") {
            options.command = "version";
        } else if (argument == "--trace") {
            options.trace = true;
        } else if (argument == "--color") {
            options.color = true;
        } else if (argument == "--no-color") {
            options.color = false;
        } else if (argument == "-S") {
            options.assembly_only = true;
        } else if (argument == "--tokens") {
            options.emit = Options::EmitForm::Tokens;
        } else if (argument == "--ast") {
            options.emit = Options::EmitForm::Ast;
        } else if (argument == "--bytecode") {
            options.emit = Options::EmitForm::Bytecode;
        } else if (argument == "--asm") {
            options.emit = Options::EmitForm::Assembly;
        } else if (argument == "-o") {
            if (i + 1 >= raw.size()) {
                std::cerr << "fire: -o needs a path\n";
                return false;
            }
            options.output = raw[++i];
        } else if (!argument.empty() && argument[0] == '-') {
            std::cerr << "fire: unknown option '" << argument << "'\n"
                      << "try 'fire help'\n";
            return false;
        } else if (options.command.empty()
            && (argument == "run" || argument == "build" || argument == "check"
                || argument == "emit" || argument == "repl" || argument == "help"
                || argument == "version" || argument == "builtins")) {
            options.command = argument;
        } else if (options.input.empty()) {
            options.input = argument;
            // `fire foo.fire` with no verb means run.
            if (options.command.empty()) {
                options.command = "run";
            }
        } else {
            // Extra bare words after the input are the program's own.
            options.program_args.push_back(argument);
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse_arguments(argc, argv, options)) {
        return kExitUsage;
    }

    if (options.command.empty()) {
        print_usage(std::cerr);
        return kExitUsage;
    }
    if (options.command == "help") {
        print_usage(std::cout);
        return kExitOk;
    }
    if (options.command == "version") {
        std::cout << kDriverName << ' ' << version_string() << '\n';
        return kExitOk;
    }
    if (options.command == "builtins") {
        print_builtins(std::cout);
        return kExitOk;
    }
    if (options.command == "repl") {
        return run_repl(std::cin, std::cout, options.color);
    }

    if (options.input.empty()) {
        std::cerr << "fire: `" << options.command << "` needs an input file\n"
                  << "try 'fire help'\n";
        return kExitUsage;
    }

    if (options.command == "run") {
        return command_run(options);
    }
    if (options.command == "check") {
        return command_check(options);
    }
    if (options.command == "emit") {
        return command_emit(options);
    }
    if (options.command == "build") {
        return command_build(options);
    }

    std::cerr << "fire: unknown command '" << options.command << "'\n";
    return kExitUsage;
}
