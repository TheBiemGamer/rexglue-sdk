/**
 * @file        codegen/gapfill_split.cpp
 * @brief       GapFill region splitting: where uncovered code is cut into function segments
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include "gapfill_split.h"

#include "ppc/instruction.h"

#include <rex/logging.h>

#include "codegen_logging.h"

using rex::codegen::ppc::decode_instruction;
using rex::codegen::ppc::Opcode;

namespace rex::codegen {

namespace {

// bcctr with BO "branch always" (BO & 0x14 == 0x14) and LK=0: bctr, a jump that never returns here.
// bctrl (LK=1) returns, and a conditional bcctr may fall through, so neither ends a function.
bool IsUnconditionalBctr(uint32_t raw) {
  const bool isBcctr = (raw >> 26) == 19 && ((raw >> 1) & 0x3FF) == 528;
  const uint32_t bo = (raw >> 21) & 0x1F;
  return isBcctr && (bo & 0x14) == 0x14 && (raw & 1) == 0;
}

}  // namespace

std::vector<CodeRegion> SplitRegionOnTerminators(
    const CodeRegion& region, const std::function<std::optional<uint32_t>(uint32_t)>& readWord,
    const std::unordered_set<uint32_t>& knownCallables,
    const std::function<bool(uint32_t)>& isTableData) {
  std::vector<CodeRegion> segments;

  // Skip padding and jump table data, so a segment never starts on either.
  auto skipNonCode = [&](uint32_t addr) {
    while (addr < region.end) {
      auto word = readWord(addr);
      if (!word)
        return region.end;
      if (*word != 0 && !isTableData(addr))
        break;
      addr += 4;
    }
    return addr;
  };

  uint32_t segmentStart = region.start;

  for (uint32_t addr = region.start; addr < region.end; addr += 4) {
    auto raw = readWord(addr);
    if (!raw)
      break;

    auto decoded = decode_instruction(addr, *raw);
    bool shouldSplit = false;
    const char* reason = nullptr;

    if (decoded.is_return()) {
      shouldSplit = true;
      reason = "blr";
    } else if (IsUnconditionalBctr(*raw)) {
      shouldSplit = true;
      reason = "bctr";
    } else if (decoded.opcode == Opcode::b && decoded.branch_target.has_value()) {
      uint32_t target = decoded.branch_target.value();
      // Don't split on tail recursion (branch to own segment start). A branch to before the
      // segment start is a tail call even if its target isn't known yet (it may be another
      // GapFill function found in this same pass): a function never jumps back past its entry.
      if (target != segmentStart && (knownCallables.contains(target) || target < segmentStart)) {
        shouldSplit = true;
        reason = "tail call";
      }
    }

    if (shouldSplit) {
      uint32_t segmentEnd = addr + 4;
      if (segmentEnd > segmentStart) {
        segments.push_back({segmentStart, segmentEnd});
        REXCODEGEN_TRACE("GapFill: split segment 0x{:08X}-0x{:08X} ({} at 0x{:08X})", segmentStart,
                         segmentEnd, reason, addr);
      }
      segmentStart = skipNonCode(segmentEnd);
      addr = segmentStart - 4;  // loop increment lands on the new segment start
    }
  }

  // Handle remaining code after last terminator
  if (segmentStart < region.end) {
    segments.push_back({segmentStart, region.end});
  }

  return segments;
}

}  // namespace rex::codegen
