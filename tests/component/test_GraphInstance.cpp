#include "support/AnimTestUtils.h"
#include "support/GraphBuilder.h"
#include "support/LogCapture.h"

#include <forge/AnimGraph.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>

using namespace forge;
using namespace forge::test;

TEST_CASE("GraphInstance wires children and indexes clips by action key",
          "[component][anim][instance]") {
  GraphBuilder b;
  const auto freeIdle = b.clip("idle");
  const auto lockedIdle = b.clip("idle");
  const auto run = b.clip("run");
  const auto blend = b.blend1D("p", { 0.0f, 1.0f, 2.0f }, { freeIdle, lockedIdle, run });
  auto def = b.root(blend).build();

  GraphInstance inst = GraphInstance::Create(def);
  CHECK(inst.GetRootNode() == inst.m_nodes[blend]);
  CHECK(nodeAt<Blend1DNode>(inst, blend).m_children[2] == inst.m_nodes[run]);
  CHECK(inst.FindClipsByActionKey("idle").size() == 2);
  CHECK(inst.FindClipsByActionKey("run").size() == 1);
  CHECK(inst.FindClipsByActionKey("r1").empty());
}

TEST_CASE("GraphInstance places every node in one contiguous, aligned buffer",
          "[component][anim][instance]") {
  GraphBuilder b;
  const auto c0 = b.clip("a");
  const auto c1 = b.clip("b");
  const auto blend = b.blend1D("p", { 0.0f, 1.0f }, { c0, c1 });
  auto def = b.root(blend).build();
  GraphInstance inst = GraphInstance::Create(def);

  REQUIRE(inst.m_nodes.size() == 3);
  const auto* base = reinterpret_cast<const std::byte*>(inst.m_nodeBuffer.get());
  for (size_t i = 0; i < inst.m_nodes.size(); ++i) {
    const auto* p = reinterpret_cast<const std::byte*>(inst.m_nodes[i]);
    CHECK(p >= base);
    if (i > 0) CHECK(p > reinterpret_cast<const std::byte*>(inst.m_nodes[i - 1]));
  }
  CHECK(reinterpret_cast<std::uintptr_t>(inst.m_nodes[blend]) % alignof(Blend1DNode) == 0);
}

TEST_CASE("GraphInstance move keeps node pointers valid and empties the source",
          "[component][anim][instance]") {
  GraphBuilder b;
  const auto c = b.clip("a");
  auto def = b.root(c).build();
  GraphInstance a = GraphInstance::Create(def);
  AnimGraphNode* root = a.GetRootNode();

  GraphInstance moved = std::move(a);
  CHECK(moved.GetRootNode() == root);
  CHECK(a.m_nodes.empty());
}

TEST_CASE("GraphInstance refuses a definition with an unregistered node type",
          "[component][anim][instance]") {
  auto def = std::make_shared<GraphDefinition>();
  def->m_nodeDefs.push_back(std::make_unique<ClipNode::Definition>());
  def->m_nodeTypeIDs.push_back(fnv1a("NotARealNode::Definition"));

  GraphInstance inst = GraphInstance::Create(def);
  CHECK(inst.GetRootNode() == nullptr);
  CHECK(LogCapture::contains(spdlog::level::err, "no runtime type"));
}

// regression test for A1
TEST_CASE("GraphInstance destruction releases the clips its nodes hold",
          "[component][anim][instance][bug:A1]") {
  auto clip = ClipBuilder().build();
  {
    GraphBuilder b;
    const auto c = b.clip("a");
    auto def = b.root(c).build();
    GraphInstance inst = GraphInstance::Create(def);
    nodeAt<ClipNode>(inst, c).setClip(clip);
    REQUIRE(clip.use_count() == 2);
  }
  CHECK(clip.use_count() == 1);
}

// regression test for A1
TEST_CASE("GraphInstance move-assignment releases the overwritten nodes' clips",
          "[component][anim][instance][bug:A1]") {
  auto clip = ClipBuilder().build();
  GraphBuilder b;
  const auto c = b.clip("a");
  auto def = b.root(c).build();

  GraphInstance inst = GraphInstance::Create(def);
  nodeAt<ClipNode>(inst, c).setClip(clip);
  REQUIRE(clip.use_count() == 2);

  inst = GraphInstance::Create(def);
  CHECK(clip.use_count() == 1);
}

TEST_CASE("GraphInstance Clone preserves per-instance clip swaps",
          "[component][anim][instance][!shouldfail][bug:Clone]") {
  GraphBuilder b;
  const auto c = b.clip("r1");
  auto def = b.root(c).build();
  GraphInstance original = GraphInstance::Create(def);

  auto swapped = ClipBuilder("longsword_r1").build();
  nodeAt<ClipNode>(original, c).setClip(swapped);

  GraphInstance copy = original.Clone();
  CHECK(nodeAt<ClipNode>(copy, c).m_clip == swapped);
}
