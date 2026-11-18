#include "support/AnimTestUtils.h"
#include "support/GlmMatchers.h"
#include "support/GraphBuilder.h"
#include "support/NodeTicker.h"

#include <forge/AnimEventReader.h>
#include <forge/AnimGraph.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>
#include <vector>

using namespace forge;
using namespace forge::test;
using Catch::Matchers::WithinAbs;

namespace {

// Copied from assets/anim/player_graph.json node 3 (LockedLoco)
constexpr int kClipCount = 9;
const glm::vec2 kLayout[kClipCount] = {
  {  0.00f,  0.00f }, {  0.00f,  1.00f }, {  0.71f,  0.71f },
  {  1.00f,  0.00f }, {  0.71f, -0.71f }, {  0.00f, -1.00f },
  { -0.71f, -0.71f }, { -1.00f,  0.00f }, { -0.71f,  0.71f },
};

struct StrafeRig {
  GraphBuilder builder;
  std::vector<int16_t> clips;
  int16_t blend = -1;
  std::shared_ptr<GraphDefinition> def;
  GraphInstance inst;
  NodeTicker ticker;

  // rootMotionFromPosition: clip i moves at (x_i, 0, z_i) m/s
  explicit StrafeRig(bool rootMotionFromPosition = false) {
    std::vector<Blend2DNode::ClipEntry> entries;
    for (int i = 0; i < kClipCount; ++i) {
      clips.push_back(builder.clip("dir" + std::to_string(i)));
      entries.push_back({ kLayout[i].x, kLayout[i].y, clips.back() });
    }
    blend = builder.blend2D("moveX", "moveZ", entries);
    def = builder.root(blend).build();
    inst = GraphInstance::Create(def);

    for (int i = 0; i < kClipCount; ++i) {
      ClipBuilder cb("dir" + std::to_string(i));
      cb.duration(1.0f).window(AnimEventType::SFXSpawn, 0.0f, 1.0f, "foot");
      if (rootMotionFromPosition) cb.rootMotion({ kLayout[i].x, 0.0f, kLayout[i].y });
      nodeAt<ClipNode>(inst, clips[i]).setClip(cb.build());
    }
  }

  Blend2DNode& node() { return nodeAt<Blend2DNode>(inst, blend); }

  UpdateResult tick(glm::vec2 p, float dt = 0.125f) {
    ticker.params.setFloat("moveX", p.x);
    ticker.params.setFloat("moveZ", p.y);
    return ticker.tick(node(), dt);
  }

  float weightOf(int k) {
    const auto& n = node();
    float w = 0.0f;
    if (n.m_idxA == k) w += n.m_weights.x;
    if (n.m_idxB == k) w += n.m_weights.y;
    if (n.m_idxC == k) w += n.m_weights.z;
    return w;
  }
};

}

TEST_CASE("Blend2D triangulates the LockedLoco layout into an 8-triangle fan",
          "[component][anim][blend2d]") {
  StrafeRig rig;
  const auto& n = rig.node();
  REQUIRE(n.m_triangles.size() == 8);
  for (const auto& t : n.m_triangles) {
    INFO("triangle " << t.a << "," << t.b << "," << t.c);
    CHECK((t.a == 0 || t.b == 0 || t.c == 0));
  }
  CHECK(n.m_hullEdges.size() == 8);
}

TEST_CASE("Blend2D weights are a convex combination everywhere in parameter space",
          "[component][anim][blend2d]") {
  const int xi = GENERATE(range(-6, 7));
  const int zi = GENERATE(range(-6, 7));
  const glm::vec2 p(xi * 0.25f, zi * 0.25f);

  StrafeRig rig;
  rig.tick(p);
  const auto& n = rig.node();
  CAPTURE(p.x, p.y, n.m_weights.x, n.m_weights.y, n.m_weights.z);

  CHECK_THAT(n.m_weights.x + n.m_weights.y + n.m_weights.z, WithinAbs(1.0, 1e-4));
  CHECK(n.m_weights.x >= -1e-3f);
  CHECK(n.m_weights.y >= -1e-3f);
  CHECK(n.m_weights.z >= -1e-3f);
  for (int idx : { n.m_idxA, n.m_idxB, n.m_idxC }) {
    CHECK(idx >= 0);
    CHECK(idx < kClipCount);
  }
}

TEST_CASE("Blend2D gives full weight to a clip at its own position",
          "[component][anim][blend2d]") {
  const int k = GENERATE(range(0, kClipCount));
  CAPTURE(k);
  StrafeRig rig;
  rig.tick(kLayout[k]);
  CHECK_THAT(rig.weightOf(k), WithinAbs(1.0, 1e-3));
}

TEST_CASE("Blend2D clamps parameters outside the hull to the nearest edge",
          "[component][anim][blend2d]") {
  StrafeRig rig;
  rig.tick({ 3.0f, 0.0f });
  CHECK_THAT(rig.weightOf(3), WithinAbs(1.0, 1e-3));
  rig.tick({ 0.0f, 5.0f });
  CHECK_THAT(rig.weightOf(1), WithinAbs(1.0, 1e-3));
  rig.tick({ -4.0f, -4.0f });
  CHECK_THAT(rig.weightOf(6), WithinAbs(1.0, 1e-3));
}

// Clip velocity == clip position, and barycentric weights reproduce linear
// functions exactly, so blended velocity must equal the param itself
TEST_CASE("Blend2D root motion reproduces linear functions exactly",
          "[component][anim][blend2d][rootmotion]") {
  const glm::vec2 p = GENERATE(glm::vec2(0.3f, 0.2f), glm::vec2(-0.5f, 0.1f),
                               glm::vec2(0.0f, -0.6f), glm::vec2(0.45f, 0.45f));
  CAPTURE(p.x, p.y);
  StrafeRig rig(true);
  const float dt = 0.125f;
  CHECK_THAT(rig.tick(p, dt).rootMotion, Near({ p.x * dt, 0.0f, p.y * dt }, 1e-4f));
}

// AnimEventReader drops anything below 0.5 per event; at a triangle centroid all
// three corners are 1/3, so an event authored on every clip is invisible
TEST_CASE("Blend2D keeps an event authored on every clip visible inside a triangle",
          "[component][anim][blend2d][events][!shouldfail][bug:A8]") {
  StrafeRig rig;
  rig.tick({ 0.2367f, 0.57f });
  INFO("weights " << rig.node().m_weights.x << " " << rig.node().m_weights.y << " "
                  << rig.node().m_weights.z);
  AnimEventReader reader;
  reader.refresh(rig.ticker.events());
  CHECK(reader.activeType(AnimEventType::SFXSpawn));
}
