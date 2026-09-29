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

/// Split a code region into function segments at unconditional terminators: blr, an unconditional
/// bctr (a tail call through CTR, as ends many virtual-method thunks), and a b tail call (to a known
/// function, or to anywhere before the segment's own start). After each terminator, zero padding and jump table data (`isTableData`) are skipped
/// so the next segment starts at the next real instruction.
///
/// @param readWord     Big-endian word at a guest address, or nullopt if unmapped.
/// @param isTableData  True for an address holding jump table data rather than code.
std::vector<CodeRegion> SplitRegionOnTerminators(
    const CodeRegion& region, const std::function<std::optional<uint32_t>(uint32_t)>& readWord,
    const std::unordered_set<uint32_t>& knownCallables,
    const std::function<bool(uint32_t)>& isTableData);

}  // namespace rex::codegen
