#include "support/AnimTestUtils.h"
#include "support/GlmMatchers.h"
#include "support/GraphBuilder.h"
#include "support/NodeTicker.h"

#include <forge/AnimGraph.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace forge;
using namespace forge::test;
using Catch::Matchers::WithinAbs;

namespace {

struct ClipRig {
  GraphBuilder builder;
  int16_t idx;
  std::shared_ptr<GraphDefinition> def;
  GraphInstance inst;
  NodeTicker ticker;

  explicit ClipRig(std::shared_ptr<AnimationClip> clip, bool loop = true)
    : idx(builder.clip("test", loop))
    , def(builder.root(idx).build())
    , inst(GraphInstance::Create(def)) {
    node().setClip(std::move(clip));
  }

  ClipNode& node() { return nodeAt<ClipNode>(inst, idx); }
  UpdateResult tick(float dt) { return ticker.tick(node(), dt); }
  UpdateResult tick(float dt, SyncTrackTimeRange r) { return ticker.tick(node(), dt, r); }
  const SampledEventsBuffer& events() const { return ticker.events(); }
};

SyncTrackTimeRange syncRange(float start, float end) { return SyncTrackTimeRange{ start, end, true }; }

}

TEST_CASE("ClipNode advances local time by dt and wraps looping clips",
          "[component][anim][clip]") {
  ClipRig rig(ClipBuilder().duration(1.0f).build());
  rig.tick(0.375f);
  CHECK(rig.node().m_time == 0.375f);
  rig.tick(0.375f);
  CHECK(rig.node().m_time == 0.75f);
  rig.tick(0.375f);
  CHECK(rig.node().m_time == 0.125f);
}

TEST_CASE("ClipNode samples a window event on every frame the window contains the playhead",
          "[component][anim][clip][events]") {
  CapsuleSegment cap;
  cap.radius = 0.2f;
  ClipRig rig(ClipBuilder().duration(1.0f).hitbox(0.3f, 0.6f, { cap }).build());

  const bool expected[] = { false,    false, true, true, false };
  for (int f = 0; f < 5; ++f) {
    rig.tick(0.125f);
    INFO("frame " << f << ", t = " << rig.node().m_time);
    const SampledEvent* e = findEvent(rig.events(), AnimEventType::SpawnHitbox, "weapon_r");
    CHECK((e != nullptr) == expected[f]);
    if (e) {
      CHECK(e->weight == 1.0f);
      REQUIRE(e->capsules.size() == 1);
      CHECK(e->capsules[0].radius == 0.2f);
    }
  }
}

TEST_CASE("ClipNode fires a one-shot exactly once, on the frame the playhead moves past it",
          "[component][anim][clip][events]") {
  ClipRig rig(ClipBuilder().duration(1.0f)
                .oneShot(AnimEventType::SoundOneShot, 0.5f, "whoosh")
                .build());

  int total = 0, firedOnFrame = -1;
  for (int f = 0; f < 7; ++f) {
    rig.tick(0.125f);
    const int n = countEvents(rig.events(), AnimEventType::SoundOneShot);
    if (n > 0) firedOnFrame = f;
    total += n;
  }
  CHECK(total == 1);
  CHECK(firedOnFrame == 4);
}

TEST_CASE("ClipNode stamps sampled events with clip position, node index and branch flag",
          "[component][anim][clip][events]") {
  ClipRig rig(ClipBuilder().duration(2.0f).window(AnimEventType::IFrame, 0.0f, 2.0f).build());
  rig.tick(0.5f);
  const SampledEvent* e = findEvent(rig.events(), AnimEventType::IFrame);
  REQUIRE(e != nullptr);
  CHECK(e->startPct == 0.25f);
  CHECK(e->sourceNodeIdx == rig.idx);
  CHECK(e->source == SampledEvent::Source::Clip);
  CHECK(HasFlag(e->flags, SampledEvent::Flags::FromActiveBranch));
}

TEST_CASE("ClipNode non-looping clips finish at their duration",
          "[component][anim][clip]") {
  ClipRig rig(ClipBuilder().duration(0.5f).build(), /*loop=*/false);
  for (int f = 0; f < 3; ++f) {
    rig.tick(0.125f);
    CHECK_FALSE(rig.node().isFinished());
  }
  rig.tick(0.125f);
  CHECK(rig.node().isFinished());

  rig.tick(0.125f);
  CHECK(rig.node().isFinished());
  CHECK(rig.node().m_time == 0.625f);
}

// On wrap ClipNode sets m_prevTime = 0 before scanning events, so
// it scans (0, 0.125] instead of (0.75, 1.0) + [0, 0.125]. Anything authored
// in the tail of a looping clip (a footstep at 95%) never fires.
TEST_CASE("ClipNode fires a one-shot in the tail of a looping clip on the wrap frame",
          "[component][anim][clip][events][!shouldfail][bug:A3]") {
  ClipRig rig(ClipBuilder().duration(1.0f).oneShot(AnimEventType::SoundOneShot, 0.95f, "footstep").build());
  int total = 0;
  for (int f = 0; f < 3; ++f) {
    rig.tick(0.375f);
    total += countEvents(rig.events(), AnimEventType::SoundOneShot);
  }
  CHECK(total == 1);
}

TEST_CASE("ClipNode root motion is velocity times dt and masks Y by default",
          "[component][anim][clip][rootmotion]") {
  ClipRig rig(ClipBuilder().duration(1.0f).rootMotion({ 0, 1, 1 }).build());
  CHECK_THAT(rig.tick(0.375f).rootMotion, Near({ 0, 0, 0.375f }));
  CHECK_THAT(rig.tick(0.375f).rootMotion, Near({ 0, 0, 0.375f }));
}

// same cause; the seam-splitting logic in sampleRootDelta (proven
// correct in test_BoneSampling) never runs because prevTime was zeroed first.
// A root-motion loop loses distance on every wrap.
TEST_CASE("ClipNode root motion stays continuous across the loop seam",
          "[component][anim][clip][rootmotion][!shouldfail][bug:A3]") {
  ClipRig rig(ClipBuilder().duration(1.0f).rootMotion({ 0, 0, 1 }).build());
  rig.tick(0.375f);
  rig.tick(0.375f);
  // 0.75 -> 1.125 (wraps to 0.125): should travel 0.375 m; currently 0.125 m
  CHECK_THAT(rig.tick(0.375f).rootMotion, Near({ 0, 0, 0.375f }, 1e-3f));
}

TEST_CASE("ClipNode seeking with setActiveTime never fires one-shots retroactively",
          "[component][anim][clip][events]") {
  ClipRig rig(ClipBuilder().duration(1.0f).oneShot(AnimEventType::SoundOneShot, 0.5f).build());
  rig.node().setActiveTime(0.6f);
  CHECK(rig.node().m_time == 0.6f);
  rig.tick(0.0f);
  CHECK(countEvents(rig.events(), AnimEventType::SoundOneShot) == 0);
  rig.tick(0.125f);
  CHECK(countEvents(rig.events(), AnimEventType::SoundOneShot) == 0);
}

TEST_CASE("ClipNode resolves local time from the sync range instead of dt",
          "[component][anim][clip][sync]") {
  ClipRig rig(ClipBuilder().duration(2.0f).sync({ { "L", 0.0f }, { "R", 0.5f } }).build());
  rig.tick(0.0f, syncRange(0.0f, 0.4f));
  CHECK_THAT(rig.node().m_time, WithinAbs(0.4, 1e-5));
  rig.tick(0.0f, syncRange(0.4f, 1.5f)); // half of segment 1 = pct 0.75 = 1.5 s
  CHECK_THAT(rig.node().m_time, WithinAbs(1.5, 1e-5));
}

TEST_CASE("ClipNode falls back to dt for invalid ranges and unsynced clips",
          "[component][anim][clip][sync]") {
  SECTION("range.valid == false") {
    ClipRig rig(ClipBuilder().duration(2.0f).sync({ { "L", 0.0f }, { "R", 0.5f } }).build());
    rig.tick(0.25f, SyncTrackTimeRange{ 0.0f, 0.4f, false });
    CHECK(rig.node().m_time == 0.25f);
  }
  SECTION("clip has no sync track") {
    ClipRig rig(ClipBuilder().duration(2.0f).build());
    rig.tick(0.25f, syncRange(0.0f, 0.4f));
    CHECK(rig.node().m_time == 0.25f);
  }
}

// wrap detection is `m_time < m_prevTime`. In a synced blend, a change in
// blend alpha shifts the blended track, so the folded position can step back a
// hair WITHOUT a wrap. That resets prevTime to 0 and re-fires every one-shot in
// [0, t] - a re-fired SpawnHitbox/RootMotionBegin would be nasty.
TEST_CASE("ClipNode does not re-fire one-shots when the sync position steps backwards",
          "[component][anim][clip][sync][!shouldfail][bug:A2]") {
  ClipRig rig(ClipBuilder()
                .duration(1.0f)
                .sync({ { "L", 0.0f }, { "R", 0.5f } })
                .oneShot(AnimEventType::SoundOneShot, 0.1f)
                .build());

  rig.tick(0.0f, syncRange(0.0f, 0.4f));
  REQUIRE(countEvents(rig.events(), AnimEventType::SoundOneShot) == 1);
  rig.tick(0.0f, syncRange(0.4f, 1.2f));
  rig.tick(0.0f, syncRange(1.2f, 1.18f));
  CHECK(countEvents(rig.events(), AnimEventType::SoundOneShot) == 0);
}

// A3 (3/3): the sync path has the same wrap handling as the dt path.
TEST_CASE("ClipNode fires a tail one-shot when the sync clock wraps past it",
          "[component][anim][clip][sync][!shouldfail][bug:A3]") {
  ClipRig rig(ClipBuilder()
                .duration(1.0f)
                .sync({ { "L", 0.0f }, { "R", 0.5f } })
                .oneShot(AnimEventType::SoundOneShot, 0.95f)
                .build());
  rig.tick(0.0f, syncRange(0.0f, 1.8f));
  REQUIRE(countEvents(rig.events(), AnimEventType::SoundOneShot) == 0);
  rig.tick(0.0f, syncRange(1.8f, 2.2f));
  CHECK(countEvents(rig.events(), AnimEventType::SoundOneShot) == 1);
}
