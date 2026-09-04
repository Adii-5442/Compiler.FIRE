// SPDX-License-Identifier: MIT
//
// The interactive session.
#pragma once

#include <iosfwd>

namespace fire {

/// Read-eval-print until end of input. Returns the process exit status, which
/// is whatever `exit(n)` was called with, or 0.
int run_repl(std::istream& in, std::ostream& out, bool color);

} // namespace fire
