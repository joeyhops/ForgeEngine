#include <forge/RootMotionPayload.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace forge;

TEST_CASE("ParseRootMotionPayload reads scale and axes",
          "[unit][rootmotion][parse]") {
  RootMotionParams p;
  REQUIRE(ParseRootMotionPayload("0.5;xz", p));
  CHECK(p.scale == 0.5f);
  CHECK(p.axes == glm::bvec3(true, false, true));
  CHECK_FALSE(p.maxVelocity.has_value());
}

TEST_CASE("ParseRootMotionPayload accepts every payload shape authored in assets",
          "[unit][rootmotion][parse]") {
  const char* payload = GENERATE("0.5;xz", "2.0;xz;", "2.0;xz;6.0", "0.6;xz;6.0");
  CAPTURE(payload);
  RootMotionParams p;
  CHECK(ParseRootMotionPayload(payload, p));
  CHECK(p.axes == glm::bvec3(true, false, true));
}

TEST_CASE("ParseRootMotionPayload treats every token as optional",
          "[unit][rootmotion][parse]") {
  RootMotionParams p;
  SECTION("empty payload keeps every default") {
    REQUIRE(ParseRootMotionPayload("", p));
    CHECK(p.scale == 1.0f);
    CHECK(p.axes == glm::bvec3(true, false, true));
  }
  SECTION("scale only") {
    REQUIRE(ParseRootMotionPayload("1.25", p));
    CHECK(p.scale == 1.25f);
    CHECK(p.axes == glm::bvec3(true, false, true));
  }
  SECTION("axes only") {
    REQUIRE(ParseRootMotionPayload(";y", p));
    CHECK(p.scale == 1.0f);
    CHECK(p.axes == glm::bvec3(false, true, false));
  }
  SECTION("max velocity") {
    REQUIRE(ParseRootMotionPayload("1;xz;6", p));
    REQUIRE(p.maxVelocity.has_value());
    CHECK(*p.maxVelocity == 6.0f);
  }
  SECTION("whitespace around tokens") {
    REQUIRE(ParseRootMotionPayload(" 0.75 ; xyz ", p));
    CHECK(p.scale == 0.75f);
    CHECK(p.axes == glm::bvec3(true, true, true));
  }
}

TEST_CASE("ParseRootMotionPayload reports malformed tokens without throwing",
          "[unit][rootmotion][parse]") {
  RootMotionParams p;
  SECTION("bad scale - valid axes still apply") {
    CHECK_FALSE(ParseRootMotionPayload("abc;y", p));
    CHECK(p.scale == 1.0f);
    CHECK(p.axes == glm::bvec3(false, true, false));
  }
  SECTION("bad axes - valid scale still applies") {
    CHECK_FALSE(ParseRootMotionPayload("1.5;xq", p));
    CHECK(p.scale == 1.5f);
    CHECK(p.axes == glm::bvec3(true, false, true));
  }
  SECTION("non-finite numbers") {
    CHECK_FALSE(ParseRootMotionPayload("nan", p));
    CHECK_FALSE(ParseRootMotionPayload("inf;xz", p));
  }
  SECTION("trailing garbage") {
    CHECK_FALSE(ParseRootMotionPayload("0.5x;xz", p));
  }
  SECTION("non-positive max velocity") {
    CHECK_FALSE(ParseRootMotionPayload("1;xz;-2", p));
    CHECK_FALSE(p.maxVelocity.has_value());
  }
  SECTION("too many tokens") {
    CHECK_FALSE(ParseRootMotionPayload("1;xz;6;7", p));
  }
}

TEST_CASE("ParseFloatStrict accepts whole finite numbers only",
          "[unit][rootmotion][parse]") {
  float v = -1.0f;
  CHECK(ParseFloatStrict("2.5", v));
  CHECK(v == 2.5f);
  CHECK(ParseFloatStrict("  -0.25 ", v));
  CHECK(v == -0.25f);
  CHECK_FALSE(ParseFloatStrict("", v));
  CHECK_FALSE(ParseFloatStrict("2.5x", v));
  CHECK_FALSE(ParseFloatStrict("fast", v));
  CHECK_FALSE(ParseFloatStrict("1e999", v));
}
