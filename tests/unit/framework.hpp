// SPDX-License-Identifier: MIT
//
// A very small test framework.
//
// Fire has no third-party dependencies, and a test runner is not a good reason
// to acquire the first one. This is about eighty lines and does the four
// things the suite actually needs: register cases, compare values, report the
// first failure in each case with a file and line, and exit non-zero.
#pragma once

#include "fire/driver.hpp"
#include "fire/value.hpp"

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace fire::test {

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> body;
};

std::vector<TestCase>& registry();

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> body);
};

/// Records a failure against the running case and continues, so one test can
/// report several problems at once.
void record_failure(const std::string& message, const char* file, int line);

/// Runs every registered case. Returns a process exit status.
int run_all(const std::string& filter);

// -- assertions --------------------------------------------------------------

template <typename T> std::string show(const T& value)
{
    std::ostringstream out;
    out << value;
    return out.str();
}

inline std::string show(const std::string& value) { return '"' + value + '"'; }
inline std::string show(bool value) { return value ? "true" : "false"; }

#define FIRE_CHECK(condition)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            ::fire::test::record_failure("expected: " #condition, __FILE__, __LINE__);             \
        }                                                                                          \
    } while (false)

#define FIRE_CHECK_EQ(actual, expected)                                                            \
    do {                                                                                           \
        const auto& fire_actual = (actual);                                                        \
        const auto& fire_expected = (expected);                                                    \
        if (!(fire_actual == fire_expected)) {                                                     \
            ::fire::test::record_failure("expected " #actual " == " #expected                      \
                                         "\n      actual:   "                                      \
                    + ::fire::test::show(fire_actual)                                              \
                    + "\n      expected: " + ::fire::test::show(fire_expected),                    \
                __FILE__, __LINE__);                                                               \
        }                                                                                          \
    } while (false)

#define FIRE_TEST(suite, name)                                                                     \
    static void fire_test_##suite##_##name();                                                      \
    static const ::fire::test::Registrar fire_registrar_##suite##_##name {                         \
        #suite, #name, fire_test_##suite##_##name                                                  \
    };                                                                                             \
    static void fire_test_##suite##_##name()

// -- helpers shared by the suites --------------------------------------------

/// Result of compiling and running a snippet through the whole pipeline.
struct RunResult {
    bool compiled = false;
    std::string output;      ///< everything the program wrote to stdout
    std::string diagnostics; ///< everything the compiler reported
    int status = 0;
};

/// Compile and run `source` on the VM, capturing its output.
RunResult run_source(const std::string& source, const std::string& stdin_text = {});

/// Compile `source` and return only the diagnostics, for error tests.
std::string diagnose(const std::string& source);

/// True when `text` contains `needle`.
bool contains(const std::string& text, const std::string& needle);

} // namespace fire::test
