/**
 * @file        kernel/noncontroller_test.cpp
 * @brief       Tests for the XAM non-controller (raw device) handler dispatch
 */

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include <rex/kernel/xam/noncontroller.h>
#include <rex/system/xtypes.h>

using namespace rex;
using namespace rex::kernel::xam;

namespace {

struct FakeHandler : NonControllerHandler {
  uint32_t last_device = 0;
  std::vector<uint8_t> written;
  uint32_t Read(uint32_t device_id, std::span<uint8_t> buffer, uint32_t& bytes_read,
                uint16_t& state) override {
    last_device = device_id;
    for (size_t i = 0; i < buffer.size(); i++) buffer[i] = uint8_t(i + 1);
    bytes_read = uint32_t(buffer.size());
    state = 1;
    return 0;
  }
  uint32_t Write(uint32_t device_id, std::span<const uint8_t> buffer) override {
    last_device = device_id;
    written.assign(buffer.begin(), buffer.end());
    return 0;
  }
};

// Guest memory: buffer at 0x100, length (u32 BE) at 0x200, state (u16 BE) at 0x210.
struct Guest {
  std::vector<uint8_t> mem = std::vector<uint8_t>(0x1000, 0);
  void SetLength(uint32_t v) {
    mem[0x200] = uint8_t(v >> 24);
    mem[0x201] = uint8_t(v >> 16);
    mem[0x202] = uint8_t(v >> 8);
    mem[0x203] = uint8_t(v);
  }
  uint32_t Length() const {
    return uint32_t(mem[0x200]) << 24 | uint32_t(mem[0x201]) << 16 | uint32_t(mem[0x202]) << 8 |
           mem[0x203];
  }
  uint16_t State() const { return uint16_t(mem[0x210] << 8 | mem[0x211]); }
};

}  // namespace

TEST_CASE("NonController: no handler means no dispatch", "[kernel][xam]") {
  RegisterNonControllerHandler(nullptr);
  Guest g;
  g.SetLength(0x20);
  CHECK_FALSE(DispatchNonControllerGetRaw(g.mem.data(), 5, 0x100, 0x200, 0x210).has_value());
  CHECK_FALSE(DispatchNonControllerSetRaw(g.mem.data(), 5, 0x100, 0x20).has_value());
}

TEST_CASE("NonController: read fills buffer, length and state", "[kernel][xam]") {
  FakeHandler h;
  RegisterNonControllerHandler(&h);
  Guest g;
  g.SetLength(0x20);
  auto r = DispatchNonControllerGetRaw(g.mem.data(), 6, 0x100, 0x200, 0x210);
  REQUIRE(r.has_value());
  CHECK(*r == 0);
  CHECK(h.last_device == 6);
  CHECK(g.mem[0x100] == 1);
  CHECK(g.mem[0x11F] == 0x20);
  CHECK(g.mem[0x120] == 0);  // nothing written past the buffer
  CHECK(g.Length() == 0x20);
  CHECK(g.State() == 1);
  RegisterNonControllerHandler(nullptr);
}

TEST_CASE("NonController: write passes the guest bytes", "[kernel][xam]") {
  FakeHandler h;
  RegisterNonControllerHandler(&h);
  Guest g;
  g.mem[0x100] = 0x0B;
  g.mem[0x13F] = 0x7E;
  auto r = DispatchNonControllerSetRaw(g.mem.data(), 5, 0x100, 0x40);
  REQUIRE(r.has_value());
  CHECK(*r == 0);
  REQUIRE(h.written.size() == 0x40);
  CHECK(h.written[0] == 0x0B);
  CHECK(h.written[0x3F] == 0x7E);
  RegisterNonControllerHandler(nullptr);
}

TEST_CASE("NonController: invalid arguments are rejected", "[kernel][xam]") {
  FakeHandler h;
  RegisterNonControllerHandler(&h);
  Guest g;
  g.SetLength(0x40);  // reads are limited to 0x20
  CHECK(DispatchNonControllerGetRaw(g.mem.data(), 5, 0x100, 0x200, 0x210) ==
        X_ERROR_INVALID_PARAMETER);
  g.SetLength(0);
  CHECK(DispatchNonControllerGetRaw(g.mem.data(), 5, 0x100, 0x200, 0x210) ==
        X_ERROR_INVALID_PARAMETER);
  g.SetLength(0x20);
  CHECK(DispatchNonControllerGetRaw(g.mem.data(), 4, 0x100, 0x200, 0x210) ==
        X_ERROR_INVALID_PARAMETER);
  CHECK(DispatchNonControllerGetRaw(g.mem.data(), 5, 0, 0x200, 0x210) ==
        X_ERROR_INVALID_PARAMETER);
  CHECK(DispatchNonControllerSetRaw(g.mem.data(), 5, 0x100, 0x41) == X_ERROR_INVALID_PARAMETER);
  CHECK(DispatchNonControllerSetRaw(g.mem.data(), 5, 0x100, 0) == X_ERROR_INVALID_PARAMETER);
  CHECK(DispatchNonControllerSetRaw(g.mem.data(), 7, 0x100, 0x20) == X_ERROR_INVALID_PARAMETER);
  CHECK(h.written.empty());
  RegisterNonControllerHandler(nullptr);
}
