#include <catch2/catch_message.hpp>
#include <forge/SyncTrack.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

using namespace forge;
using Catch::Matchers::WithinAbs;

namespace {

SyncTrack makeTrack(std::initializer_list<std::pair<const char*, float>> markers) {
  SyncTrack t;
  for (const auto& [name, pct] : markers) t.events.push_back(SyncEvent{ pct, name, 0 });
  t.rehash();
  return t;
}

float wrappedDistance(float a, float b) {
  const float d = std::fmod(std::abs(a - b), 1.0f);
  return std::min(d, 1.0f - d);
}

}

TEST_CASE("SyncTrack maps percentages to sync positions when the first event is at 0",
          "[unit][anim][sync]") {
  const SyncTrack t = makeTrack({ { "L", 0.0f }, { "R", 0.5f } });
  CHECK_THAT(t.PercentageToSyncPos(0.00f), WithinAbs(0.0, 1e-6));
  CHECK_THAT(t.PercentageToSyncPos(0.25f), WithinAbs(0.5, 1e-6));
  CHECK_THAT(t.PercentageToSyncPos(0.50f), WithinAbs(1.0, 1e-6));
  CHECK_THAT(t.PercentageToSyncPos(0.75f), WithinAbs(1.5, 1e-6));
  CHECK_THAT(t.PercentageToSyncPos(1.00f), WithinAbs(2.0, 1e-6));
}

TEST_CASE("SyncTrack places percentages before the first event in the wrap segment",
          "[unit][anim][sync]") {
  const SyncTrack t = makeTrack({ { "L", 0.25f}, { "R", 0.75f} });
  CHECK_THAT(t.PercentageToSyncPos(0.00f), WithinAbs(1.5, 1e-6));
  CHECK_THAT(t.PercentageToSyncPos(0.25f), WithinAbs(0.0, 1e-6));
  CHECK_THAT(t.PercentageToSyncPos(0.50f), WithinAbs(0.5, 1e-6));
  CHECK_THAT(t.PercentageToSyncPos(0.90f), WithinAbs(1.3, 1e-6));
}

TEST_CASE("SyncTrack SyncPosToPercentage inverts PercentageToSyncPos",
          "[unit][anim][sync]") {
  const SyncTrack t = makeTrack({ { "L", 0.1f }, { "R", 0.45f }, { "X", 0.8f } });
  const float pct = GENERATE(0.0f, 0.05f, 0.1f, 0.3f, 0.45f, 0.6f, 0.8f, 0.95f, 0.999f);

  const float sp = t.PercentageToSyncPos(pct);
  CAPTURE(pct, sp);
  CHECK(sp >= 0.0f);
  CHECK(sp <= static_cast<float>(t.count()));
  CHECK(wrappedDistance(t.SyncPosToPercentage(sp), pct) < 1e-5f);
}

TEST_CASE("SyncTrack SyncPosToPercentage is periodic in the event count",
          "[unit][anim][sync]") {
  const SyncTrack t = makeTrack({ { "L", 0.0f }, { "R", 0.5f} });
  const float sp = GENERATE(0.0f, 0.3f, 1.0f, 1.7f);
  CAPTURE(sp);
  CHECK_THAT(t.SyncPosToPercentage(sp + 2.0f), WithinAbs(t.SyncPosToPercentage(sp), 1e-5));
  CHECK_THAT(t.SyncPosToPercentage(sp - 2.0f), WithinAbs(t.SyncPosToPercentage(sp), 1e-5));
}

TEST_CASE("SyncTrack with a single event still folds sync positions into [0,1)",
          "[unit][anim][sync][!shouldfail][bug:N5]") {
  const SyncTrack t = makeTrack({ { "Plant", 0.3f } });
  const float pct = t.SyncPosToPercentage(1.25f);
  CHECK(pct >= 0.0f);
  CHECK(pct < 1.0f);
}

TEST_CASE("SyncTrack compatibility requires the same event names in the same order",
          "[unit][anim][sync]") {
  const SyncTrack walk = makeTrack({ { "L", 0.0f }, { "R", 0.5f } });
  const SyncTrack run = makeTrack({ { "L", 0.1f }, { "R", 0.6f } });
  const SyncTrack swapped = makeTrack({ { "R", 0.0f }, { "L", 0.5f } });
  const SyncTrack single = makeTrack({ { "L", 0.0f } });

  CHECK(walk.CompatibleWith(run));
  CHECK_FALSE(walk.CompatibleWith(swapped));
  CHECK_FALSE(walk.CompatibleWith(single));
}

// Not a bug, a sharp edge: SyncEvent::id defaults to 0, so two tracks built by
// hand compare "compatible" until rehash() is called. TAESerialization::load
// and ClipBuilder both rehash; if anything new builds tracks, it must rehash as well
TEST_CASE("SyncTrack ids are only meaningful after rehash()",
          "[unit][anim][sync][characterization]") {
  SyncTrack a, b;
  a.events = { SyncEvent{ 0.0f, "L", 0 }, SyncEvent{ 0.5f, "R", 0 } };
  b.events = { SyncEvent{ 0.0f, "Jump", 0 }, SyncEvent{ 0.5f, "Land", 0 } };

  CHECK(a.CompatibleWith(b));
  a.rehash();
  b.rehash();
  CHECK_FALSE(a.CompatibleWith(b));
  CHECK(a.events[0].id == HashName("L"));
}

TEST_CASE("SyncTrack::Blend lerps percentages and keeps names and ids",
          "[unit][anim][sync]") {
  const SyncTrack a = makeTrack({ { "L", 0.0f }, { "R", 0.4f } });
  const SyncTrack b = makeTrack({ { "L", 0.2f }, { "R", 0.6f } });

  const SyncTrack mid = SyncTrack::Blend(a, b, 0.5f);
  REQUIRE(mid.count() == 2);
  CHECK_THAT(mid.events[0].percentage, WithinAbs(0.1, 1e-6));
  CHECK_THAT(mid.events[1].percentage, WithinAbs(0.5, 1e-6));
  CHECK(mid.events[1].name == "R");
  CHECK(mid.events[1].id == HashName("R"));

  CHECK(SyncTrack::Blend(a, b, 0.0f).events[1].percentage == a.events[1].percentage);
  CHECK(SyncTrack::Blend(a, b, 1.0f).events[1].percentage == b.events[1].percentage);

  SECTION("incompatible tracks return a copy of the first argument") {
    const SyncTrack other = makeTrack({ { "X", 0.3f } });
    const SyncTrack out = SyncTrack::Blend(a, other, 0.5f);
    REQUIRE(out.count() == a.count());
    CHECK(out.events[1].percentage == a.events[1].percentage);
  }
}


