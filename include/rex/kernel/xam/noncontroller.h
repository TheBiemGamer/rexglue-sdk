/**
 * @file        rex/kernel/xam/noncontroller.h
 * @brief       App-provided handler for XamInputNonControllerGetRaw/SetRaw(Ex)
 */

#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace rex::kernel::xam {

// Raw access to a "non-controller" input device (Xenia calls these device IDs 5 and 6), such as a
// toys-to-life portal. An app registers one handler; the four XamInputNonController* exports call
// it. Without a handler they behave as the old stubs did.
class NonControllerHandler {
 public:
  virtual ~NonControllerHandler() = default;
  // Fill `buffer` (at most 0x20 bytes) and set how many bytes are valid and the device state.
  // Returns an X_STATUS / X_ERROR_* code.
  virtual uint32_t Read(uint32_t device_id, std::span<uint8_t> buffer, uint32_t& bytes_read,
                        uint16_t& state) = 0;
  // `buffer` is 1..0x40 bytes. Returns an X_STATUS / X_ERROR_* code.
  virtual uint32_t Write(uint32_t device_id, std::span<const uint8_t> buffer) = 0;
};

// Not owned. Must stay alive while any game thread may call the exports. nullptr unregisters.
void RegisterNonControllerHandler(NonControllerHandler* handler);

// Export bodies, split out for testing. `base` is the guest memory base; addresses are guest
// addresses. The length (u32) and state (u16) are big-endian in guest memory. Return nullopt when
// no handler is registered, so the export can keep the stub's behaviour.
std::optional<uint32_t> DispatchNonControllerGetRaw(uint8_t* base, uint32_t device_id,
                                                    uint32_t buffer_addr, uint32_t length_addr,
                                                    uint32_t state_addr);
std::optional<uint32_t> DispatchNonControllerSetRaw(uint8_t* base, uint32_t device_id,
                                                    uint32_t buffer_addr, uint32_t length);

}  // namespace rex::kernel::xam
