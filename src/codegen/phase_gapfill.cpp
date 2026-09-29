/**
 * @file        codegen/phase_gapfill.cpp
 * @brief       GapFill phase: find uncovered code regions and register them as functions
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include "gapfill_split.h"
#include "ppc/instruction.h"

#include <optional>
#include <unordered_set>

#include <rex/codegen/phases.h>
#include "phase_helpers.h"

#include <rex/logging.h>

#include "codegen_logging.h"
#include <rex/memory/utils.h>

#include <ppc.h>

using rex::codegen::ppc::decode_instruction;
using rex::codegen::ppc::Opcode;
using rex::memory::load_and_swap;

namespace rex::codegen {

namespace {

//=============================================================================
// GapFill to register uncovered code regions
//=============================================================================

// True if the word at `addr` is data sitting in a code region rather than an instruction: known
// jump table data, an absolute code pointer (an undetected jump table's entry), or a word that
// doesn't decode as any instruction (an offset table's bytes).
bool isTableData(const BinaryView& binary, const std::unordered_set<uint32_t>& jumpTableWords,
                 uint32_t addr) {
  if (jumpTableWords.contains(addr))
    return true;
  const uint8_t* data = binary.translate(addr);
  if (!data)
    return true;
  uint32_t raw = load_and_swap<uint32_t>(data);
  if ((raw & 3) == 0 && binary.isExecutable(raw))
    return true;
  return decode_instruction(addr, raw).opcode == Opcode::kUnknown;
}

// Check if address looks like exception handler data (handler ptr + rdata ptr)
bool looksLikeExceptionData(const BinaryView& binary, const FunctionGraph& graph, uint32_t addr) {
  const uint8_t* data = binary.translate(addr);
  if (!data)
    return false;

  // Exception handler data pattern:
  // [addr+0]: pointer to __C_specific_handler (entry point)
  // [addr+4]: pointer to scope table in .rdata
  uint32_t firstDword = load_and_swap<uint32_t>(data);
  uint32_t secondDword = load_and_swap<uint32_t>(data + 4);

  // Check if first dword is a known entry point (like __C_specific_handler)
  if (!graph.isEntryPoint(firstDword)) {
    return false;
  }

  // Check if second dword points to .rdata section
  auto* rdataSection = binary.findSectionByName(".rdata");
  if (!rdataSection)
    return false;

  uint32_t rdataStart = rdataSection->baseAddress;
  uint32_t rdataEnd = rdataStart + rdataSection->size;

  if (secondDword >= rdataStart && secondDword < rdataEnd) {
    REXCODEGEN_TRACE(
        "GapFill: 0x{:08X} looks like exception data (handler=0x{:08X}, scope=0x{:08X}), skipping",
        addr, firstDword, secondDword);
    return true;
  }

  return false;
}

// Returns how many new functions were registered.
size_t gapFillCodeRegions(CodegenContext& ctx) {
  REXCODEGEN_TRACE("Analyze: checking for uncovered code regions...");

  auto& graph = ctx.graph;
  auto& binary = ctx.binary();
  auto& scan = ctx.scan;

  // Build set of known callables for tail call detection
  std::unordered_set<uint32_t> knownCallables;
  for (const auto& [addr, node] : graph.functions()) {
    knownCallables.insert(addr);
  }

  size_t gapsFound = 0;
  size_t segmentsCreated = 0;

  // Every word of every jump table analysis found, so GapFill never starts a function in one.
  // Entry width isn't recorded, so each table is assumed to use 4-byte entries: an over-estimate
  // for byte/halfword offset tables only makes a following function start be missed, as before.
  std::unordered_set<uint32_t> jumpTableWords;
  for (const auto& [addr, node] : graph.functions()) {
    for (const auto& jt : node->jumpTables()) {
      for (size_t i = 0; i < jt.targets.size(); i++) {
        jumpTableWords.insert(jt.tableAddress + static_cast<uint32_t>(i * 4));
      }
    }
  }
  auto readWord = [&binary](uint32_t addr) -> std::optional<uint32_t> {
    const uint8_t* data = binary.translate(addr);
    if (!data)
      return std::nullopt;
    return load_and_swap<uint32_t>(data);
  };
  auto tableData = [&binary, &jumpTableWords](uint32_t addr) {
    return isTableData(binary, jumpTableWords, addr);
  };

  for (const auto& region : scan.codeRegions) {
    // Split region on terminators (blr, bctr, tail calls), then check each segment
    auto segments = SplitRegionOnTerminators(region, readWord, knownCallables, tableData);

    for (const auto& segment : segments) {
      // Skip if this segment's start is already a registered function entry
      if (graph.isEntryPoint(segment.start))
        continue;

      // Skip if this segment's start is inside another function
      if (auto* containingFunc = graph.getFunctionContaining(segment.start)) {
        continue;
      }

      // Skip if this looks like exception handler data (handler ptr + rdata ptr)
      if (looksLikeExceptionData(binary, graph, segment.start))
        continue;

      uint32_t segmentSize = segment.size();
      graph.addFunction(segment.start, segmentSize, FunctionAuthority::GAP_FILL, false);

      REXCODEGEN_TRACE("GapFill: registered sub_{:08X} (0x{:08X}-0x{:08X}, {} bytes)",
                       segment.start, segment.start, segment.end, segmentSize);
      segmentsCreated++;
    }

    gapsFound++;
  }

  if (segmentsCreated > 0) {
    REXCODEGEN_TRACE("Analyze: registered {} gap functions from {} regions", segmentsCreated,
                     gapsFound);
  } else {
    REXCODEGEN_TRACE("Analyze: no uncovered regions found");
  }
  return segmentsCreated;
}

//=============================================================================
// Cleanup absorbed GAP_FILL functions
//=============================================================================

void cleanupAbsorbedGapFills(CodegenContext& ctx) {
  auto& graph = ctx.graph;
  std::vector<uint32_t> toRemove;

  for (const auto& [addr, node] : graph.functions()) {
    if (node->authority() != FunctionAuthority::GAP_FILL)
      continue;

    for (const auto& [otherAddr, otherNode] : graph.functions()) {
      if (otherAddr == addr)
        continue;
      if (!otherNode->containsAddress(addr))
        continue;

      // This GAP_FILL is inside another function's blocks
      if (otherNode->authority() != FunctionAuthority::GAP_FILL) {
        // Absorbed by higher authority - remove
        toRemove.push_back(addr);
        break;
      } else if (otherAddr < addr) {
        // Both GAP_FILL, other has lower address - it survives
        toRemove.push_back(addr);
        break;
      }
    }
  }

  for (uint32_t addr : toRemove) {
    graph.removeFunction(addr);
  }

  if (!toRemove.empty()) {
    REXCODEGEN_TRACE("Analyze: removed {} absorbed GAP_FILL functions", toRemove.size());
  }
}

}  // anonymous namespace

namespace phases {

VoidResult GapFill(CodegenContext& ctx, ProgressReporter* reporter) {
  (void)reporter;
  // Repeat until nothing new is found: a pass only splits on a b tail call whose forward target is
  // already known, so a thunk that jumps to a function this same pass discovers is only split off
  // (and registered) on the next pass.
  constexpr int kMaxPasses = 8;
  for (int pass = 0; pass < kMaxPasses; pass++) {
    size_t registered = gapFillCodeRegions(ctx);

    // Discover blocks for gap-filled functions
    auto known = buildKnownFunctions(ctx.graph, /*excludeGapFill=*/true);
    size_t discovered = discoverPendingFunctions(ctx, known);
    REXCODEGEN_TRACE("Analyze: pass {}: discovered blocks for {} gap-filled functions", pass + 1,
                     discovered);

    cleanupAbsorbedGapFills(ctx);
    if (registered == 0)
      break;
  }

  return Ok();
}

}  // namespace phases

}  // namespace rex::codegen
