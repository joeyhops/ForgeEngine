#include "support/AnimTestUtils.h"
#include "support/GlmMatchers.h"
#include "support/GraphBuilder.h"
#include "support/NodeTicker.h"

#include <forge/AnimEventReader.h>
#include <forge/AnimGraph.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>

using namespace forge;
using namespace forge::test;
using Catch::Matchers::WithinAbs;

namespace {

// Full length window whose payload names the clip, so tests can see whose events survived the blend
std::shared_ptr<AnimationClip> taggedClip(const std::string& tag, float duration = 1.0f) {
  return ClipBuilder(tag).duration(duration).window(AnimEventType::SFXSpawn, 0.0f, duration, tag).build();
}

struct LocoRig {
  GraphBuilder builder;
  int16_t idle, walk, run, blend;
  std::shared_ptr<GraphDefinition> def;
  GraphInstance inst;
  NodeTicker ticker;

  LocoRig()
    : idle(builder.clip("idle"))
    , walk(builder.clip("walk"))
    , run(builder.clip("run"))
    , blend(builder.blend1D("speed", { 0.0f, 1.0f, 2.0f }, { idle, walk, run }))
    , def(builder.root(blend).build())
    , inst(GraphInstance::Create(def)) {
    nodeAt<ClipNode>(inst, idle).setClip(taggedClip("idle"));
    nodeAt<ClipNode>(inst, walk).setClip(taggedClip("walk"));
    nodeAt<ClipNode>(inst, run).setClip(taggedClip("run"));
  }

  Blend1DNode& node() { return nodeAt<Blend1DNode>(inst, blend); }
  UpdateResult tick(float speed, float dt = 0.125f) {
    ticker.params.setFloat("speed", speed);
    return ticker.tick(node(), dt);
  }
  // -1 when the clip's event is absent from the buffer entirely
  float weightOf(const std::string& tag) const {
    const SampledEvent* e = findEvent(ticker.events(), AnimEventType::SFXSpawn, tag);
    return e ? e->weight : -1.0f;
  }
};

struct PairRig {
  GraphBuilder builder;
  int16_t a, b, blend;
  std::shared_ptr<GraphDefinition> def;
  GraphInstance inst;
  NodeTicker ticker;

  PairRig(std::shared_ptr<AnimationClip> clipA, std::shared_ptr<AnimationClip> clipB)
    : a(builder.clip("a"))
    , b(builder.clip("b"))
    , blend(builder.blend1D("alpha", { 0.0f, 1.0f }, { a, b }))
    , def(builder.root(blend).build())
    , inst(GraphInstance::Create(def)) {
    nodeAt<ClipNode>(inst, a).setClip(std::move(clipA));
    nodeAt<ClipNode>(inst, b).setClip(std::move(clipB));
  }

  ClipNode& clipA() { return nodeAt<ClipNode>(inst, a); }
  ClipNode& clipB() { return nodeAt<ClipNode>(inst, b); }
  UpdateResult tick(float alpha, float dt) {
    ticker.params.setFloat("alpha", alpha);
    return ticker.tick(nodeAt<Blend1DNode>(inst, blend), dt);
  }
};

float syncPosOf(ClipNode& n) {
  return n.m_clip->syncTrack.PercentageToSyncPos(n.m_time / n.m_clip->duration);
}

}

TEST_CASE("Blend1D brackets the param between thresholds and computes local alpha",
          "[component][anim][blend1d]") {
  struct Case { float param; int lower; float alpha; };
  const Case c = GENERATE(values<Case>({
    { -1.0f, 0, 0.0f },
    {  0.0f, 0, 0.0f },
    { 0.25f, 0, 0.25f },
    {  1.0f, 1, 0.0f },
    {  1.5f, 1, 0.5f },
    {  2.0f, 1, 1.0f },
    {  9.0f, 1, 1.0f },
  }));
  CAPTURE(c.param);

  LocoRig rig;
  rig.tick(c.param);
  CHECK(rig.node().m_lowerIdx == c.lower);
  CHECK_THAT(rig.node().m_localAlpha, WithinAbs(c.alpha, 1e-6));
}

TEST_CASE("Blend1D weights each bracketing child's events by its contribution",
          "[component][anim][blend1d][events]") {
  LocoRig rig;
  rig.tick(0.25f);
  CHECK_THAT(rig.weightOf("idle"), WithinAbs(0.75, 1e-6));
  CHECK_THAT(rig.weightOf("walk"), WithinAbs(0.25, 1e-6));
  CHECK(rig.weightOf("run") == -1.0f);
}

// Regression test for the e20e98f post-mortem fix: ScaleWeights must run before
// the endpoint passthrough returns, otherwise the silent child keeps weight 1.0
TEST_CASE("Blend1D at an endpoint keeps the silent child's events at weight zero",
          "[component][anim][blend1d][events][regression]") {
  LocoRig rig;
  rig.tick(0.0f);
  CHECK(rig.weightOf("idle") == 1.0f);
  CHECK(rig.weightOf("walk") == 0.0f);

  AnimEventReader reader;
  reader.refresh(rig.ticker.events());
  CHECK(reader.active(AnimEventType::SFXSpawn, "idle"));
  CHECK_FALSE(reader.active(AnimEventType::SFXSpawn, "walk"));

  rig.tick(2.0f);
  CHECK(rig.weightOf("walk") == 0.0f);
  CHECK(rig.weightOf("run") == 1.0f);
}

TEST_CASE("Blend1D mixes children's root motion by alpha",
          "[component][anim][blend1d][rootmotion]") {
  PairRig rig(ClipBuilder().duration(1.0f).rootMotion({ 0, 0, 0 }).build(),
              ClipBuilder().duration(1.0f).rootMotion({ 0, 0, 2 }).build());
  CHECK_THAT(rig.tick(0.5f, 0.125f).rootMotion, Near({ 0, 0, 0.125f }));
}

TEST_CASE("Blend1D keeps sync-authored children at the same sync position",
          "[component][anim][blend1d][sync]") {
  PairRig rig(ClipBuilder("walk").duration(1.0f).sync({ { "L", 0.0f }, { "R", 0.5f } }).build(),
              ClipBuilder("run").duration(2.0f).sync({ { "L", 0.0f }, { "R", 0.6f } }).build());

  for (int f = 0; f < 8; ++f) {
    rig.tick(0.5f, 0.1f);
    const float sa = syncPosOf(rig.clipA());
    const float sb = syncPosOf(rig.clipB());
    INFO("frame " << f << ": walk t=" << rig.clipA().m_time << " sync=" << sa
                  << ", run t=" << rig.clipB().m_time << " sync=" << sb);
    // Compared on the sync circle (count = 2) so a same-frame wrap still matches
    const float d = std::fmod(std::abs(sa - sb), 2.0f);
    CHECK(std::min(d, 2.0f - d) < 1e-3f);
  }
  CHECK(rig.clipA().m_time != rig.clipB().m_time);
}

TEST_CASE("Blend1D falls back to dt when a child lacks a sync track",
          "[component][anim][blend1d][sync]") {
  PairRig rig(ClipBuilder().duration(1.0f).sync({ { "L", 0.0f }, { "R", 0.5f } }).build(),
              ClipBuilder().duration(2.0f).build());
  for (int f = 0; f < 3; ++f) rig.tick(0.5f, 0.125f);
  CHECK(rig.clipA().m_time == 0.375f);
  CHECK(rig.clipB().m_time == 0.375f);
}

TEST_CASE("Blend1D with a single child passes it straight through",
          "[component][anim][blend1d]") {
  GraphBuilder b;
  const auto only = b.clip("only");
  const auto blend = b.blend1D("p", { 0.0f }, { only });
  auto def = b.root(blend).build();
  GraphInstance inst = GraphInstance::Create(def);
  nodeAt<ClipNode>(inst, only).setClip(taggedClip("only"));

  NodeTicker ticker;
  ticker.params.setFloat("p", 5.0f);
  ticker.tick(nodeAt<Blend1DNode>(inst, blend), 0.125f);
  CHECK(nodeAt<ClipNode>(inst, only).m_time == 0.125f);
  CHECK(nodeAt<Blend1DNode>(inst, blend).m_localAlpha == 0.0f);
  const SampledEvent* e = findEvent(ticker.events(), AnimEventType::SFXSpawn, "only");
  REQUIRE(e != nullptr);
  CHECK(e->weight == 1.0f);
}
