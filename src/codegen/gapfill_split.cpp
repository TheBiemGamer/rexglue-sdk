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

// False if the bctr at `bctrAddr` is a switch dispatch (the register moved into CTR was last written
// by lwzx, an absolute table load, or add, an offset table's base + offset): its case blocks follow
// and belong to the same function. Anything else, typically lwz rX,d(rY) ; mtctr rX ; bctr at the
// end of a virtual-method thunk, is treated as a tail call.
bool IsTailCallBctr(uint32_t bctrAddr, uint32_t segmentStart,
                    const std::function<std::optional<uint32_t>(uint32_t)>& readWord) {
  constexpr uint32_t kMaxLookback = 8;
  std::optional<uint32_t> ctrSource;
  for (uint32_t i = 1; i <= kMaxLookback && bctrAddr >= segmentStart + i * 4; i++) {
    auto raw = readWord(bctrAddr - i * 4);
    if (!raw)
      return true;
    const uint32_t op = *raw >> 26;
    const uint32_t xo = (*raw >> 1) & 0x3FF;
    const uint32_t rd = (*raw >> 21) & 0x1F;
    if (!ctrSource) {
      // mtspr CTR, rS (mtctr): opcode 31, xo 467, spr field 0x120.
      if (op == 31 && xo == 467 && ((*raw >> 11) & 0x3FF) == 0x120) {
        ctrSource = rd;
      }
      continue;
    }
    if (rd != *ctrSource)
      continue;
    // The instruction that last wrote the register moved into CTR.
    return !(op == 31 && (xo == 23 || xo == 266));  // lwzx, add
  }
  return true;
}

}  // namespace

bool IsLikelyTableData(uint32_t addr,
                       const std::function<std::optional<uint32_t>(uint32_t)>& readWord,
                       const std::function<bool(uint32_t)>& isCodeAddress,
                       const std::unordered_set<uint32_t>& jumpTableWords) {
  auto word = readWord(addr);
  if (!word)
    return true;
  auto isCodePointer = [&](std::optional<uint32_t> w) {
    return w && (*w & 3) == 0 && isCodeAddress(*w);
  };
  if (isCodePointer(word)) {
    // A code pointer can also be a valid lwz r16..r23 instruction; only call it data when it's a
    // known jump table entry or has a neighbouring code pointer (a table has several).
    if (jumpTableWords.contains(addr) || isCodePointer(readWord(addr - 4)) ||
        isCodePointer(readWord(addr + 4))) {
      return true;
    }
    return false;
  }
  return decode_instruction(addr, *word).opcode == Opcode::kUnknown;
}

std::unordered_set<uint32_t> CollectConditionalBranchTargets(
    const CodeRegion& region, const std::function<std::optional<uint32_t>(uint32_t)>& readWord) {
  std::unordered_set<uint32_t> targets;
  for (uint32_t addr = region.start; addr < region.end; addr += 4) {
    auto raw = readWord(addr);
    if (!raw)
      break;
    // bc: opcode 16, relative (AA=0), no link (LK=0).
    if ((*raw >> 26) != 16 || (*raw & 3) != 0)
      continue;
    int32_t bd = static_cast<int16_t>(*raw & 0xFFFC);
    targets.insert(addr + static_cast<uint32_t>(bd));
  }
  return targets;
}

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
    } else if (IsUnconditionalBctr(*raw) && IsTailCallBctr(addr, segmentStart, readWord)) {
      shouldSplit = true;
      reason = "bctr";
    } else if (decoded.opcode == Opcode::b && decoded.branch_target.has_value()) {
      uint32_t target = decoded.branch_target.value();
      // Don't split on tail recursion (branch to own segment start). A branch to before the
      // segment start is a tail call even if its target isn't known yet (it may be another
      // GapFill function found in this same pass). When the segment start is really a block in
      // the middle of a function, GapFill drops it: see CollectConditionalBranchTargets.
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
