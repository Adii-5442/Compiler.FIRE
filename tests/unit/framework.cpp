// SPDX-License-Identifier: MIT
#include "framework.hpp"

#include "fire/vm.hpp"

#include <iostream>

namespace fire::test {
namespace {

    struct Failure {
        std::string message;
        std::string file;
        int line;
    };

    std::vector<Failure>& current_failures()
    {
        static std::vector<Failure> failures;
        return failures;
    }

} // namespace

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> cases;
    return cases;
}

Registrar::Registrar(const char* suite, const char* name, std::function<void()> body)
{
    registry().push_back(TestCase { suite, name, std::move(body) });
}

void record_failure(const std::string& message, const char* file, int line)
{
    current_failures().push_back(Failure { message, file, line });
}

bool contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

RunResult run_source(const std::string& source, const std::string& stdin_text)
{
    RunResult result;
    Compilation compilation { SourceFile::from_string("<test>", source), false };

    std::ostringstream diagnostics;
    result.compiled = compilation.compile();
    compilation.diagnostics().render(diagnostics);
    result.diagnostics = diagnostics.str();
    if (!result.compiled) {
        return result;
    }

    std::ostringstream out;
    std::istringstream in { stdin_text };
    VM::Options options;
    options.source = &compilation.source();
    options.out = &out;
    options.err = &diagnostics;
    options.in = &in;

    VM vm { compilation.module(), std::move(options) };
    result.status = vm.run();
    result.output = out.str();
    result.diagnostics = diagnostics.str();
    return result;
}

std::string diagnose(const std::string& source)
{
    Compilation compilation { SourceFile::from_string("<test>", source), false };
    compilation.analyze();
    std::ostringstream out;
    compilation.diagnostics().render(out);
    return out.str();
}

int run_all(const std::string& filter)
{
    std::size_t passed = 0;
    std::vector<std::string> failed;

    for (const TestCase& test : registry()) {
        const std::string full = test.suite + '.' + test.name;
        if (!filter.empty() && !contains(full, filter)) {
            continue;
        }
        current_failures().clear();
        test.body();

        if (current_failures().empty()) {
            ++passed;
            continue;
        }
        failed.push_back(full);
        std::cout << "FAIL  " << full << '\n';
        for (const Failure& failure : current_failures()) {
            std::cout << "    " << failure.file << ':' << failure.line << ": " << failure.message
                      << '\n';
        }
    }

    std::cout << '\n'
              << passed << " passed, " << failed.size() << " failed, " << registry().size()
              << " registered\n";
    return failed.empty() ? 0 : 1;
}

} // namespace fire::test

int main(int argc, char** argv)
{
    const std::string filter = argc > 1 ? argv[1] : "";
    return fire::test::run_all(filter);
}
