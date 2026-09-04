// SPDX-License-Identifier: MIT
//
// Bytecode disassembler.
//
// Used by `fire emit --bytecode` and by the VM's `--trace` mode. It decodes
// with the same operand widths the VM does, so a disagreement between them
// shows up as garbled output rather than as a silent misexecution.
#pragma once

#include "fire/chunk.hpp"
#include "fire/source.hpp"

#include <cstddef>
#include <iosfwd>

namespace fire {

/// Disassemble one instruction and return the offset of the next.
std::size_t disassemble_instruction(
    std::ostream& out, const Chunk& chunk, std::size_t offset, const SourceFile* source);

/// Disassemble a whole function, with a header naming it.
void disassemble_function(
    std::ostream& out, const CompiledFunction& function, const SourceFile* source);

/// Disassemble the script body and every function in the module.
void disassemble_module(std::ostream& out, const Module& module, const SourceFile* source);

} // namespace fire
