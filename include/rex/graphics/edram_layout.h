// EDRAM geometry: how many 80x16-sample tiles the emulated EDRAM has. Real hardware has 2048 and
// wraps tile addresses at 2048 (its colour/depth base fields are 12 bits, but bit 11 aliases).
// Games whose bound surfaces need more room than that can ask for 4096 tiles through the
// edram_tile_count setting; the GPU backend then publishes the layout it actually uses as the
// active layout, which game code can read after the GPU has started.

#pragma once

#include <cstdint>

#include <rex/graphics/xenos.h>

namespace rex::graphics {

struct EdramLayout {
  uint32_t tile_count = xenos::kEdramTileCount;

  static constexpr EdramLayout FromRequested(int32_t requested) {
    return EdramLayout{requested == int32_t(xenos::kEdramTileCountMax) ? xenos::kEdramTileCountMax
                                                                       : xenos::kEdramTileCount};
  }

  constexpr bool is_extended() const { return tile_count == xenos::kEdramTileCountMax; }
  constexpr uint32_t tile_mask() const { return tile_count - 1; }
  constexpr uint32_t base_tiles_bits() const {
    return is_extended() ? xenos::kEdramBaseTilesBitsMax : xenos::kEdramBaseTilesBits;
  }
  constexpr uint32_t size_bytes() const {
    return tile_count * xenos::kEdramTileHeightSamples * xenos::kEdramTileWidthSamples *
           uint32_t(sizeof(uint32_t));
  }
  // Base tile of an RB_COLOR_INFO / RB_DEPTH_INFO register value (the low 12 bits are the base).
  constexpr uint32_t DecodeBase(uint32_t info_register_value) const {
    return info_register_value & tile_mask();
  }
  constexpr uint32_t Wrap(uint32_t tiles) const { return tiles & tile_mask(); }
  // Splits [start, start + length) into at most two ranges inside [0, tile_count): the first ends
  // at first_end, the optional second is [0, second_end). start must be below tile_count and length
  // at most tile_count. Returns the number of ranges.
  constexpr uint32_t SplitRange(uint32_t start, uint32_t length, uint32_t& first_end,
                                uint32_t& second_end) const {
    uint32_t end = start + length;
    if (end <= tile_count) {
      first_end = end;
      second_end = 0;
      return 1;
    }
    first_end = tile_count;
    second_end = end - tile_count;
    if (second_end > start) {
      second_end = start;
    }
    return 2;
  }
};

// The layout the edram_tile_count setting asks for (invalid values give 2048 with a warning).
EdramLayout RequestedEdramLayout();
// Set by the GPU backend once it has decided which layout it uses.
void SetActiveEdramLayout(EdramLayout layout);
// The layout in use; 2048 tiles until a backend sets it.
EdramLayout ActiveEdramLayout();

}  // namespace rex::graphics
