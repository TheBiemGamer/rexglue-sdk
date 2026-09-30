#include <catch2/catch_test_macros.hpp>

#include <rex/graphics/shader_constant_overrides.h>

using namespace rex::graphics;

namespace {
void Zero(uint32_t index, float xyzw[4], void* user) {
  if (index == 3) xyzw[1] = 0.0f;
  ++*static_cast<int*>(user);
}
}  // namespace

TEST_CASE("unregistered hash leaves constants untouched", "[pixel_overrides]") {
  ClearPixelConstantOverrides();
  float c[4] = {1, 2, 3, 4};
  REQUIRE_FALSE(HasPixelConstantOverrides(0x1234));
  ApplyPixelConstantOverrides(0x1234, 3, c);
  REQUIRE(c[1] == 2.0f);
}

TEST_CASE("registered callback runs for its hash only", "[pixel_overrides]") {
  ClearPixelConstantOverrides();
  int calls = 0;
  RegisterPixelConstantOverride(0xABCD, &Zero, &calls);
  REQUIRE(HasPixelConstantOverrides(0xABCD));
  float c[4] = {1, 2, 3, 4};
  ApplyPixelConstantOverrides(0xABCD, 3, c);
  REQUIRE(c[1] == 0.0f);
  float d[4] = {1, 2, 3, 4};
  ApplyPixelConstantOverrides(0xABCD, 4, d);
  REQUIRE(d[1] == 2.0f);
  float e[4] = {1, 2, 3, 4};
  ApplyPixelConstantOverrides(0x9999, 3, e);
  REQUIRE(e[1] == 2.0f);
  REQUIRE(calls == 2);
}

TEST_CASE("generation changes on register, clear and invalidate", "[pixel_overrides]") {
  const uint64_t g0 = PixelConstantOverrideGeneration();
  int calls = 0;
  RegisterPixelConstantOverride(0x1, &Zero, &calls);
  const uint64_t g1 = PixelConstantOverrideGeneration();
  InvalidatePixelConstantOverrides();
  const uint64_t g2 = PixelConstantOverrideGeneration();
  ClearPixelConstantOverrides();
  const uint64_t g3 = PixelConstantOverrideGeneration();
  REQUIRE(g1 != g0);
  REQUIRE(g2 != g1);
  REQUIRE(g3 != g2);
  REQUIRE_FALSE(HasPixelConstantOverrides(0x1));
}

TEST_CASE("debug override strings parse", "[pixel_overrides]") {
  auto v = ParseDebugPixelConstantOverrides("0123456789ABCDEF:12:2=0.5;ff:0:0=-1");
  REQUIRE(v.size() == 2);
  REQUIRE(v[0].hash == 0x0123456789ABCDEFull);
  REQUIRE(v[0].index == 12);
  REQUIRE(v[0].component == 2);
  REQUIRE(v[0].value == 0.5f);
  REQUIRE(v[1].hash == 0xFF);
  REQUIRE(v[1].value == -1.0f);
}

TEST_CASE("malformed debug override entries are skipped", "[pixel_overrides]") {
  REQUIRE(ParseDebugPixelConstantOverrides("").empty());
  REQUIRE(ParseDebugPixelConstantOverrides("zz:1:0=1").empty());
  REQUIRE(ParseDebugPixelConstantOverrides("ff:1:4=1").empty());   // component out of range
  REQUIRE(ParseDebugPixelConstantOverrides("ff:256:0=1").empty()); // index out of range
  REQUIRE(ParseDebugPixelConstantOverrides("ff:1:0").empty());     // no value
  auto v = ParseDebugPixelConstantOverrides("ff:1:0=1;broken;ee:2:1=3");
  REQUIRE(v.size() == 2);
}
