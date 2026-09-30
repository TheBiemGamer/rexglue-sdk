#include <rex/graphics/edram_layout.h>

#include <atomic>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(edram_tile_count, 2048, "GPU",
                     "Emulated EDRAM size in 80x16-sample tiles: 2048 (the real Xbox 360) or 4096 "
                     "for render sizes whose surfaces don't fit. 4096 is only supported by the "
                     "D3D12 backend.");

namespace rex::graphics {

namespace {
std::atomic<uint32_t> active_tile_count{xenos::kEdramTileCount};
}  // namespace

EdramLayout RequestedEdramLayout() {
  int32_t requested = REXCVAR_GET(edram_tile_count);
  EdramLayout layout = EdramLayout::FromRequested(requested);
  if (int32_t(layout.tile_count) != requested) {
    REXLOG_WARN("edram_tile_count {} is not supported, using {}", requested, layout.tile_count);
  }
  return layout;
}

void SetActiveEdramLayout(EdramLayout layout) {
  active_tile_count.store(layout.tile_count, std::memory_order_release);
}

EdramLayout ActiveEdramLayout() {
  return EdramLayout{active_tile_count.load(std::memory_order_acquire)};
}

}  // namespace rex::graphics
