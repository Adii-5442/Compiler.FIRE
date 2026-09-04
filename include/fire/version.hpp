// SPDX-License-Identifier: MIT
//
// Compile-time identity of the Fire toolchain.
#pragma once

#include <string>

namespace fire {

inline constexpr int kVersionMajor = 1;
inline constexpr int kVersionMinor = 0;
inline constexpr int kVersionPatch = 0;

/// Human readable version, e.g. "1.0.0".
inline std::string version_string()
{
    return std::to_string(kVersionMajor) + '.' + std::to_string(kVersionMinor) + '.'
        + std::to_string(kVersionPatch);
}

/// The canonical name of the language and of its compiler driver.
inline constexpr const char* kLanguageName = "Fire";
inline constexpr const char* kDriverName = "fire";
inline constexpr const char* kSourceExtension = ".fire";

/// Magic bytes and version stamped into serialised bytecode images.
inline constexpr char kBytecodeMagic[4] = { 'F', 'I', 'R', 'E' };
inline constexpr int kBytecodeVersion = 1;

} // namespace fire
