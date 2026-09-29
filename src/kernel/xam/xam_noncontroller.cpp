/**
 * @file        kernel/xam/xam_noncontroller.cpp
 * @brief       XamInputNonControllerGetRaw/SetRaw(Ex), forwarded to an app-provided handler
 *
 * Signatures follow Xenia Canary's xam_input.cc:
 *   GetRaw(u16* state, u32* length, u8* buffer)        == GetRawEx(5, buffer, length, state)
 *   SetRaw(u32 length, u8* buffer)                     == SetRawEx(5, buffer, length)
 */

#include <rex/kernel/xam/noncontroller.h>

#include <atomic>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/xtypes.h>

namespace rex::kernel::xam {

namespace {

std::atomic<NonControllerHandler*> g_handler{nullptr};

constexpr uint32_t kMaxReadLength = 0x20;
constexpr uint32_t kMaxWriteLength = 0x40;  // 0x40 carries speaker audio

bool ValidDevice(uint32_t device_id) { return device_id == 5 || device_id == 6; }

}  // namespace

void RegisterNonControllerHandler(NonControllerHandler* handler) { g_handler.store(handler); }

std::optional<uint32_t> DispatchNonControllerGetRaw(uint8_t* base, uint32_t device_id,
                                                    uint32_t buffer_addr, uint32_t length_addr,
                                                    uint32_t state_addr) {
  NonControllerHandler* handler = g_handler.load();
  if (!handler) return std::nullopt;
  if (!ValidDevice(device_id) || !buffer_addr || !length_addr || !state_addr) {
    return X_ERROR_INVALID_PARAMETER;
  }
  const uint32_t length = rex::memory::load_and_swap<uint32_t>(base + length_addr);
  if (length == 0 || length > kMaxReadLength) return X_ERROR_INVALID_PARAMETER;

  uint32_t bytes_read = 0;
  uint16_t state = 0;
  const uint32_t status =
      handler->Read(device_id, std::span<uint8_t>(base + buffer_addr, length), bytes_read, state);
  if (XSUCCEEDED(status)) {
    rex::memory::store_and_swap<uint32_t>(base + length_addr, bytes_read);
    rex::memory::store_and_swap<uint16_t>(base + state_addr, state);
  }
  return status;
}

std::optional<uint32_t> DispatchNonControllerSetRaw(uint8_t* base, uint32_t device_id,
                                                    uint32_t buffer_addr, uint32_t length) {
  NonControllerHandler* handler = g_handler.load();
  if (!handler) return std::nullopt;
  if (!ValidDevice(device_id) || !buffer_addr || length == 0 || length > kMaxWriteLength) {
    return X_ERROR_INVALID_PARAMETER;
  }
  return handler->Write(device_id, std::span<const uint8_t>(base + buffer_addr, length));
}

}  // namespace rex::kernel::xam

namespace {

// Without a handler the exports behave as the old REX_EXPORT_STUBs did (r3 untouched), but warn
// once rather than on every call: games poll these hundreds of times a second.
void WarnStubOnce(std::atomic<bool>& warned, const char* name) {
  if (!warned.exchange(true)) REXKRNL_WARN("{} STUB (no NonControllerHandler registered)", name);
}

std::atomic<bool> g_warned_get{false}, g_warned_get_ex{false}, g_warned_set{false},
    g_warned_set_ex{false};

}  // namespace

using rex::kernel::xam::DispatchNonControllerGetRaw;
using rex::kernel::xam::DispatchNonControllerSetRaw;

// GetRaw(r3 = state*, r4 = length*, r5 = buffer)
extern "C" REX_FUNC(__imp__XamInputNonControllerGetRaw) {
  auto r = DispatchNonControllerGetRaw(base, 5, ctx.r5.u32, ctx.r4.u32, ctx.r3.u32);
  if (!r) return WarnStubOnce(g_warned_get, "__imp__XamInputNonControllerGetRaw");
  ctx.r3.u64 = *r;
}
// GetRawEx(r3 = device, r4 = buffer, r5 = length*, r6 = state*)
extern "C" REX_FUNC(__imp__XamInputNonControllerGetRawEx) {
  auto r = DispatchNonControllerGetRaw(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
  if (!r) return WarnStubOnce(g_warned_get_ex, "__imp__XamInputNonControllerGetRawEx");
  ctx.r3.u64 = *r;
}
// SetRaw(r3 = length, r4 = buffer)
extern "C" REX_FUNC(__imp__XamInputNonControllerSetRaw) {
  auto r = DispatchNonControllerSetRaw(base, 5, ctx.r4.u32, ctx.r3.u32);
  if (!r) return WarnStubOnce(g_warned_set, "__imp__XamInputNonControllerSetRaw");
  ctx.r3.u64 = *r;
}
// SetRawEx(r3 = device, r4 = buffer, r5 = length)
extern "C" REX_FUNC(__imp__XamInputNonControllerSetRawEx) {
  auto r = DispatchNonControllerSetRaw(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
  if (!r) return WarnStubOnce(g_warned_set_ex, "__imp__XamInputNonControllerSetRawEx");
  ctx.r3.u64 = *r;
}

static rex::ppc::detail::PPCFuncRegistrar _ppc_reg___imp__XamInputNonControllerGetRaw(
    "__imp__XamInputNonControllerGetRaw", &__imp__XamInputNonControllerGetRaw);
static rex::ppc::detail::PPCFuncRegistrar _ppc_reg___imp__XamInputNonControllerGetRawEx(
    "__imp__XamInputNonControllerGetRawEx", &__imp__XamInputNonControllerGetRawEx);
static rex::ppc::detail::PPCFuncRegistrar _ppc_reg___imp__XamInputNonControllerSetRaw(
    "__imp__XamInputNonControllerSetRaw", &__imp__XamInputNonControllerSetRaw);
static rex::ppc::detail::PPCFuncRegistrar _ppc_reg___imp__XamInputNonControllerSetRawEx(
    "__imp__XamInputNonControllerSetRawEx", &__imp__XamInputNonControllerSetRawEx);
