/**
 * @file        codegen/gapfill_split.h
 * @brief       GapFill region splitting: where uncovered code is cut into function segments
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_set>
#include <vector>

#include <rex/codegen/code_region.h>

namespace rex::codegen {

/// Split a code region into function segments at unconditional terminators: blr, a bctr tail call
/// (as ends many virtual-method thunks; a switch dispatch bctr doesn't split), and a b tail call (to
/// a known function, or to anywhere before the segment's own start). After each terminator, zero padding and jump table data (`isTableData`) are
/// skipped so the next segment starts at the next real instruction.
///
/// @param readWord     Big-endian word at a guest address, or nullopt if unmapped.
/// @param isTableData  True for an address holding jump table data rather than code.
std::vector<CodeRegion> SplitRegionOnTerminators(
    const CodeRegion& region, const std::function<std::optional<uint32_t>(uint32_t)>& readWord,
    const std::unordered_set<uint32_t>& knownCallables,
    const std::function<bool(uint32_t)>& isTableData);

/// Targets of every relative conditional branch (bc without link) in the region. A conditional
/// branch never leaves its function, so these are blocks inside functions: GapFill must not register
/// a segment that starts on one (for example a shared blr after a loop, reached from inside).
std::unordered_set<uint32_t> CollectConditionalBranchTargets(
    const CodeRegion& region, const std::function<std::optional<uint32_t>(uint32_t)>& readWord);

/// True if the word at `addr` is data sitting in a code region rather than an instruction: a known
/// jump table entry or a run of code pointers (an undetected absolute jump table), or a word that
/// doesn't decode as any instruction (an offset table's bytes). A lone word that looks like a code
/// pointer is treated as an instruction, since it's also a valid lwz encoding.
bool IsLikelyTableData(uint32_t addr,
                       const std::function<std::optional<uint32_t>(uint32_t)>& readWord,
                       const std::function<bool(uint32_t)>& isCodeAddress,
                       const std::unordered_set<uint32_t>& jumpTableWords);

}  // namespace rex::codegen
