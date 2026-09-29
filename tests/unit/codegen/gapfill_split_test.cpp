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
  CHECK(segs[1].start == At(2));
  CHECK(segs[2].start == At(4));
}

TEST_CASE("GapFill split: b backwards inside the segment (a loop) does not split",
          "[codegen][gapfill]") {
  // b -4 at index 2 targets index 1, inside the segment.
  Words w{{kLwz, kLwz, 0x4BFFFFFC, kLwz, kBlr}, {}};
  CHECK(w.split().size() == 1);
}

TEST_CASE("GapFill: conditional branch targets are collected, b and bl targets are not",
          "[codegen][gapfill]") {
  // index 0: bge cr6,+16 (0x40980010) -> index 4; index 1: b +8 -> index 3; index 2: bl +8 -> 4.
  std::vector<uint32_t> words{0x40980010, 0x48000008, 0x48000009, kLwz, kBlr};
  auto targets = CollectConditionalBranchTargets(
      {kBase, kBase + static_cast<uint32_t>(words.size() * 4)},
      [&words](uint32_t addr) -> std::optional<uint32_t> {
        size_t i = (addr - kBase) / 4;
        if (addr < kBase || i >= words.size())
          return std::nullopt;
        return words[i];
      });
  CHECK(targets.contains(At(4)));
  CHECK_FALSE(targets.contains(At(3)));
  CHECK(targets.size() == 1);
}

TEST_CASE("GapFill split: b backwards to a known function is a tail call", "[codegen][gapfill]") {
  constexpr uint32_t kSubi = 0x3863FFFC;
  Words w{{kSubi, 0x4BFFFFF0, kSubi, 0x4BFFFFF0, kLwz, kBlr}, {}};
  auto segs = w.split({At(0) - 12, At(2) - 12});
  REQUIRE(segs.size() == 3);
  CHECK(segs[1].start == At(2));
  CHECK(segs[2].start == At(4));
}

TEST_CASE("GapFill split: bctr dispatching a jump table does not split", "[codegen][gapfill]") {
  // lwzx r0,r12,r0 ; mtctr r0 ; bctr : an absolute jump table dispatch.
  Words a{{kLwz, 0x7C0C002E, 0x7C0903A6, kBctr, kLwz, kBlr}, {}};
  CHECK(a.split().size() == 1);
  // add r12,r12,r0 ; mtctr r12 ; bctr : an offset table dispatch.
  Words b{{kLwz, 0x7D8C0214, 0x7D8903A6, kBctr, kLwz, kBlr}, {}};
  CHECK(b.split().size() == 1);
}

TEST_CASE("GapFill split: bctr through a loaded pointer is a tail call", "[codegen][gapfill]") {
  // lwz r11,0x1C(r11) ; mtctr r11 ; bctr : a virtual call used as a tail call.
  Words w{{kLwz, 0x816B001C, 0x7D6903A6, kBctr, kLwz, kBlr}, {}};
  auto segs = w.split();
  REQUIRE(segs.size() == 2);
  CHECK(segs[1].start == At(4));
}

namespace {

bool TableData(const std::vector<uint32_t>& words, size_t index,
               const std::unordered_set<uint32_t>& jumpTableWords = {}) {
  return IsLikelyTableData(
      At(index),
      [&words](uint32_t addr) -> std::optional<uint32_t> {
        size_t i = (addr - kBase) / 4;
        if (addr < kBase || i >= words.size())
          return std::nullopt;
        return words[i];
      },
      [](uint32_t value) { return value >= 0x82100000 && value < 0x82800000; }, jumpTableWords);
}

}  // namespace

TEST_CASE("GapFill table data: a lone code-pointer-looking word is an instruction",
          "[codegen][gapfill]") {
  // 0x82110008 is also lwz r16,8(r17): alone between instructions, treat it as code.
  CHECK_FALSE(TableData({kLwz, 0x82110008, kLwz}, 1));
}

TEST_CASE("GapFill table data: a run of code pointers is data", "[codegen][gapfill]") {
  std::vector<uint32_t> words{kBctr, 0x82110008, 0x82110020, kLwz};
  CHECK(TableData(words, 1));
  CHECK(TableData(words, 2));
  CHECK_FALSE(TableData(words, 3));
}

TEST_CASE("GapFill table data: known jump table entries and undecodable words are data",
          "[codegen][gapfill]") {
  CHECK(TableData({kBctr, 0x82110008, kLwz}, 1, {At(1)}));
  CHECK(TableData({kBctr, 0x00010203, kLwz}, 1));
  CHECK_FALSE(TableData({kBctr, kLwz, kLwz}, 1, {At(1)}));
}
