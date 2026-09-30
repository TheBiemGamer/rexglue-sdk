#include <catch2/catch_test_macros.hpp>

#include <rex/cvar.h>
#include <rex/graphics/edram_layout.h>
#include <rex/graphics/util/draw.h>

using rex::graphics::EdramLayout;

TEST_CASE("requested tile count is validated", "[edram_layout]") {
  REQUIRE(EdramLayout::FromRequested(2048).tile_count == 2048);
  REQUIRE(EdramLayout::FromRequested(4096).tile_count == 4096);
  REQUIRE(EdramLayout::FromRequested(0).tile_count == 2048);
  REQUIRE(EdramLayout::FromRequested(3000).tile_count == 2048);
  REQUIRE(EdramLayout::FromRequested(8192).tile_count == 2048);
  REQUIRE(EdramLayout::FromRequested(-4096).tile_count == 2048);
}

TEST_CASE("layout derived values", "[edram_layout]") {
  constexpr EdramLayout stock = EdramLayout::FromRequested(2048);
  constexpr EdramLayout big = EdramLayout::FromRequested(4096);
  STATIC_REQUIRE(!stock.is_extended());
  STATIC_REQUIRE(big.is_extended());
  STATIC_REQUIRE(stock.tile_mask() == 0x7FF);
  STATIC_REQUIRE(big.tile_mask() == 0xFFF);
  STATIC_REQUIRE(stock.base_tiles_bits() == 11);
  STATIC_REQUIRE(big.base_tiles_bits() == 12);
  STATIC_REQUIRE(stock.size_bytes() == 10 * 1024 * 1024);
  STATIC_REQUIRE(big.size_bytes() == 20 * 1024 * 1024);
}

TEST_CASE("base decode keeps hardware aliasing only at 2048", "[edram_layout]") {
  // RB_COLOR_INFO with color_base 0x123, bit 11 set, and a format in bits 16+.
  const uint32_t info = 0x00050000u | 0x800u | 0x123u;
  REQUIRE(EdramLayout::FromRequested(2048).DecodeBase(info) == 0x123);
  REQUIRE(EdramLayout::FromRequested(4096).DecodeBase(info) == 0x923);
}

TEST_CASE("wrap and split at the end of EDRAM", "[edram_layout]") {
  constexpr EdramLayout stock = EdramLayout::FromRequested(2048);
  constexpr EdramLayout big = EdramLayout::FromRequested(4096);
  REQUIRE(stock.Wrap(2048 + 5) == 5);
  REQUIRE(big.Wrap(2048 + 5) == 2048 + 5);
  REQUIRE(big.Wrap(4096 + 5) == 5);

  uint32_t first_end = 0, second_end = 0;
  // 4000 + 200 crosses the end of a 4096-tile EDRAM: [4000, 4096) then [0, 104).
  REQUIRE(big.SplitRange(4000, 200, first_end, second_end) == 2);
  REQUIRE(first_end == 4096);
  REQUIRE(second_end == 104);
  // The same range fits without wrapping below the end.
  REQUIRE(big.SplitRange(1900, 200, first_end, second_end) == 1);
  REQUIRE(first_end == 2100);
  // At 2048 tiles, 1900 + 200 wraps: [1900, 2048) then [0, 52).
  REQUIRE(stock.SplitRange(1900, 200, first_end, second_end) == 2);
  REQUIRE(first_end == 2048);
  REQUIRE(second_end == 52);
  // A range covering all of EDRAM never produces a second part overlapping the first.
  REQUIRE(big.SplitRange(10, 4096, first_end, second_end) == 2);
  REQUIRE(first_end == 4096);
  REQUIRE(second_end == 10);
}

TEST_CASE("cvar selects the requested layout", "[edram_layout]") {
  REQUIRE(rex::cvar::SetFlagByName("edram_tile_count", "4096"));
  REQUIRE(rex::graphics::RequestedEdramLayout().tile_count == 4096);
  REQUIRE(rex::cvar::SetFlagByName("edram_tile_count", "1234"));
  REQUIRE(rex::graphics::RequestedEdramLayout().tile_count == 2048);
  REQUIRE(rex::cvar::SetFlagByName("edram_tile_count", "2048"));
  REQUIRE(rex::graphics::RequestedEdramLayout().tile_count == 2048);
}

TEST_CASE("active layout defaults to stock and can be set", "[edram_layout]") {
  REQUIRE(rex::graphics::ActiveEdramLayout().tile_count == 2048);
  rex::graphics::SetActiveEdramLayout(EdramLayout::FromRequested(4096));
  REQUIRE(rex::graphics::ActiveEdramLayout().tile_count == 4096);
  rex::graphics::SetActiveEdramLayout(EdramLayout::FromRequested(2048));
}

TEST_CASE("resolve EDRAM info holds a 12-bit base", "[edram_layout]") {
  rex::graphics::draw_util::ResolveEdramInfo info;
  info.pitch_tiles = 1023;
  info.base_tiles = 4095;
  info.format = 15;
  info.format_is_64bpp = 1;
  info.fill_half_pixel_offset = 1;
  REQUIRE(info.base_tiles == 4095);
  REQUIRE(info.pitch_tiles == 1023);
  REQUIRE(info.format == 15);
  REQUIRE(info.format_is_64bpp == 1);
  REQUIRE(info.fill_half_pixel_offset == 1);
  STATIC_REQUIRE(sizeof(info) == sizeof(uint32_t));
}

TEST_CASE("resolve EDRAM info bit positions match the 4096-tile resolve shaders", "[edram_layout]") {
  // src/graphics/shaders/xesl/resolve.xesli with XE_EDRAM_BASE_TILES_BITS 12: base at 13 (12 bits),
  // format at 25, format_ints_log2 at 29, fill_half_pixel_offset at 30.
  rex::graphics::draw_util::ResolveEdramInfo info;
  info.base_tiles = 0xABC;
  REQUIRE(((info.packed >> 13) & 0xFFF) == 0xABC);
  info.packed = 0;
  info.format = 0x9;
  REQUIRE(info.packed == (0x9u << 25));
  info.packed = 0;
  info.format_is_64bpp = 1;
  REQUIRE(info.packed == (1u << 29));
  info.packed = 0;
  info.fill_half_pixel_offset = 1;
  REQUIRE(info.packed == (1u << 30));
}

TEST_CASE("resolve EDRAM info repacks to the stock 11-bit shader layout", "[edram_layout]") {
  // Stock resolve shaders (XE_EDRAM_BASE_TILES_BITS 11): pitch 0..9, msaa 10..11, is_depth 12,
  // base 13..23, format 24..27, format_ints_log2 28, fill_half_pixel_offset 29.
  using rex::graphics::draw_util::ResolveEdramInfo;
  using rex::graphics::draw_util::ResolveEdramInfoForShaders;
  ResolveEdramInfo info;
  info.pitch_tiles = 0x2A5;
  info.msaa_samples = rex::graphics::xenos::MsaaSamples::k4X;
  info.is_depth = 1;
  info.base_tiles = 0x5A5;
  info.format = 0xB;
  info.format_is_64bpp = 1;
  info.fill_half_pixel_offset = 1;
  const uint32_t stock = ResolveEdramInfoForShaders(info, false).packed;
  REQUIRE(stock == (0x2A5u | (2u << 10) | (1u << 12) | (0x5A5u << 13) | (0xBu << 24) | (1u << 28) |
                    (1u << 29)));
  REQUIRE(ResolveEdramInfoForShaders(info, true).packed == info.packed);
}

TEST_CASE("pipeline storage file names keep stock caches separate", "[edram_layout]") {
  using rex::graphics::PipelineStorageFileName;
  const auto stock = EdramLayout::FromRequested(2048);
  const auto big = EdramLayout::FromRequested(4096);
  REQUIRE(PipelineStorageFileName(0x4156089E, true, stock) == "4156089E.rov.d3d12.xpso");
  REQUIRE(PipelineStorageFileName(0x4156089E, false, stock) == "4156089E.rtv.d3d12.xpso");
  REQUIRE(PipelineStorageFileName(0x4156089E, true, big) == "4156089E.rov.4096.d3d12.xpso");
  REQUIRE(PipelineStorageFileName(0x4156089E, false, big) == "4156089E.rtv.d3d12.xpso");
}
