/**
 * @file        codegen/gapfill_split_test.cpp
 * @brief       Tests for GapFill's region splitting (where uncovered code is cut into functions)
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

#include "codegen/gapfill_split.h"

using namespace rex::codegen;

namespace {

constexpr uint32_t kBase = 0x82000000;

constexpr uint32_t kBlr = 0x4E800020;
constexpr uint32_t kBctr = 0x4E800420;
constexpr uint32_t kBctrl = 0x4E800421;   // indirect call, returns: not a terminator
constexpr uint32_t kBeqctr = 0x4D820420;  // conditional bcctr: not a terminator
constexpr uint32_t kLwz = 0x80630000;     // lwz r3,0(r3)
constexpr uint32_t kMtctr = 0x7D6903A6;   // mtctr r11

struct Words {
  std::vector<uint32_t> words;
  std::unordered_set<uint32_t> tableData;

  CodeRegion region() const {
    return {kBase, kBase + static_cast<uint32_t>(words.size() * 4)};
  }

  std::vector<CodeRegion> split(const std::unordered_set<uint32_t>& known = {}) const {
    return SplitRegionOnTerminators(
        region(),
        [this](uint32_t addr) -> std::optional<uint32_t> {
          size_t i = (addr - kBase) / 4;
          if (addr < kBase || i >= words.size())
            return std::nullopt;
          return words[i];
        },
        known, [this](uint32_t addr) { return tableData.contains(addr); });
  }
};

uint32_t At(size_t index) {
  return kBase + static_cast<uint32_t>(index * 4);
}

}  // namespace

TEST_CASE("GapFill split: blr ends a segment", "[codegen][gapfill]") {
  Words w{{kLwz, kBlr, kLwz, kBlr}, {}};
  auto segs = w.split();
  REQUIRE(segs.size() == 2);
  CHECK(segs[0].start == At(0));
  CHECK(segs[0].end == At(2));
  CHECK(segs[1].start == At(2));
  CHECK(segs[1].end == At(4));
}

TEST_CASE("GapFill split: unconditional bctr (tail call) ends a segment", "[codegen][gapfill]") {
  Words w{{kLwz, kMtctr, kBctr, kLwz, kBlr}, {}};
  auto segs = w.split();
  REQUIRE(segs.size() == 2);
  CHECK(segs[0].end == At(3));
  CHECK(segs[1].start == At(3));
}

TEST_CASE("GapFill split: bctrl and conditional bcctr do not end a segment", "[codegen][gapfill]") {
  Words w{{kLwz, kBctrl, kLwz, kBeqctr, kLwz, kBlr}, {}};
  auto segs = w.split();
  REQUIRE(segs.size() == 1);
  CHECK(segs[0].start == At(0));
  CHECK(segs[0].end == At(6));
}

TEST_CASE("GapFill split: padding after a terminator is skipped", "[codegen][gapfill]") {
  Words w{{kLwz, kBlr, 0, kLwz, kBlr}, {}};
  auto segs = w.split();
  REQUIRE(segs.size() == 2);
  CHECK(segs[1].start == At(3));
}

TEST_CASE("GapFill split: jump table data after bctr is skipped", "[codegen][gapfill]") {
  Words w{{kLwz, kMtctr, kBctr, 0x82000040, 0x82000050, kLwz, kBlr}, {At(3), At(4)}};
  auto segs = w.split();
  REQUIRE(segs.size() == 2);
  CHECK(segs[1].start == At(5));
  CHECK(segs[1].end == At(7));
}

TEST_CASE("GapFill split: trailing padding makes no empty segment", "[codegen][gapfill]") {
  Words w{{kLwz, kBctr, 0, 0}, {}};
  auto segs = w.split();
  REQUIRE(segs.size() == 1);
  CHECK(segs[0].end == At(2));
}

TEST_CASE("GapFill split: b to a known function splits, b elsewhere does not",
          "[codegen][gapfill]") {
  // b +8 (0x48000008) at index 1 targets index 3.
  Words w{{kLwz, 0x48000008, kLwz, kLwz, kBlr}, {}};
  CHECK(w.split().size() == 1);
  auto segs = w.split({At(3)});
  REQUIRE(segs.size() == 2);
  CHECK(segs[1].start == At(2));
}

TEST_CASE("GapFill split: b backwards past the segment start is a tail call", "[codegen][gapfill]") {
  // Two this-adjusting thunks (subi r3,r3,4; b <earlier address>) whose targets are not known
  // yet, followed by a method. 0x4BFFFFF0 at index 1 targets index -3; at index 3, index -1.
  constexpr uint32_t kSubi = 0x3863FFFC;
  Words w{{kSubi, 0x4BFFFFF0, kSubi, 0x4BFFFFF0, kLwz, kBlr}, {}};
  auto segs = w.split();
  REQUIRE(segs.size() == 3);
  CHECK(segs[0].end == At(2));
  CHECK(segs[1].start == At(2));
  CHECK(segs[1].end == At(4));
  CHECK(segs[2].start == At(4));
}

TEST_CASE("GapFill split: b backwards inside the segment (a loop) does not split",
          "[codegen][gapfill]") {
  // b -4 at index 2 targets index 1, inside the segment.
  Words w{{kLwz, kLwz, 0x4BFFFFFC, kLwz, kBlr}, {}};
  CHECK(w.split().size() == 1);
}
