// Lets game code adjust the float constants a specific pixel shader receives, identified by the
// XXH3 hash of its microcode (Shader::ucode_data_hash()). Used for per-game post-processing
// options (for example turning off a game's FXAA by zeroing its texel-size constant). With nothing
// registered, the command processors skip this entirely.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace rex::graphics {

// Called for each float constant a registered pixel shader reads, after it is copied from the guest
// registers into the constant buffer. `index` is the guest register (c0..c255); `xyzw` may be
// modified. Runs on the GPU command thread; must be quick and must not block.
using PixelConstantOverride = void (*)(uint32_t index, float xyzw[4], void* user);

void RegisterPixelConstantOverride(uint64_t ucode_hash, PixelConstantOverride fn, void* user);
void ClearPixelConstantOverrides();
// Makes the next draw re-upload pixel constants, for example after a setting changed.
void InvalidatePixelConstantOverrides();

// Changes whenever registered overrides, the debug override setting, or an invalidation change what
// the constants should be. Command processors compare it to decide whether to re-upload.
uint64_t PixelConstantOverrideGeneration();

// True when a callback or a debug override exists for this hash.
bool HasPixelConstantOverrides(uint64_t ucode_hash);
// Applies the registered callback and any debug overrides for this hash to one constant.
void ApplyPixelConstantOverrides(uint64_t ucode_hash, uint32_t index, float xyzw[4]);

struct DebugPixelConstantOverride {
  uint64_t hash;
  uint32_t index;      // 0..255
  uint32_t component;  // 0..3 (x, y, z, w)
  float value;
};
// Parses "<hash hex>:<index>:<component>=<value>" entries joined by ';'. Malformed entries are
// skipped.
std::vector<DebugPixelConstantOverride> ParseDebugPixelConstantOverrides(std::string_view text);

}  // namespace rex::graphics
