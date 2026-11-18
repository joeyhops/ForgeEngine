#include "support/GraphBuilder.h"
#include "support/LogCapture.h"
#include "support/TestPaths.h"

#include <forge/AnimGraph.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

using namespace forge;
using namespace forge::test;
using Catch::Matchers::WithinAbs;

namespace {

template <typename TDef>
const TDef* defAt(const GraphDefinition& d, size_t i) {
  return dynamic_cast<const TDef*>(d.m_nodeDefs.at(i).get());
}

}

TEST_CASE("GraphDefinition loads a minimal one-clip graph",
          "[component][anim][loader]") {
  auto def = GraphDefinition::LoadFromJson(dataFile("graphs/minimal_clip.json"));
  REQUIRE(def);
  REQUIRE(def->m_nodeDefs.size() == 1);
  REQUIRE(def->m_nodeTypeIDs.size() == 1);
  CHECK(def->m_nodeTypeIDs[0] == fnv1a("ClipNode::Definition"));

  const auto* clip = defAt<ClipNode::Definition>(*def, 0);
  REQUIRE(clip != nullptr);
  CHECK(clip->actionKey == "idle");
  CHECK(clip->loop);
  CHECK(clip->clipFile.empty());

  GraphInstance inst = GraphInstance::Create(def);
  CHECK(inst.GetRootNode() != nullptr);
}

TEST_CASE("GraphDefinition parses every field of every node type, including defaults",
          "[component][anim][loader]") {
  auto def = GraphDefinition::LoadFromJson(dataFile("graphs/all_node_types.json"));
  REQUIRE(def);
  REQUIRE(def->m_nodeDefs.size() == 12);
  REQUIRE(def->m_nodeTypeIDs.size() == 12);

  const auto* sm = defAt<StateMachineNode::Definition>(*def, 0);
  REQUIRE(sm != nullptr);
  CHECK(sm->initialState == "Loco");
  REQUIRE(sm->states.size() == 4);
  CHECK(sm->states[3].name == "Strafe");
  CHECK(sm->states[3].nodeIndex == 8);
  REQUIRE(sm->transitions.size() == 3);

  const auto& t0 = sm->transitions[0];
  CHECK_THAT(t0.blendTime, WithinAbs(0.1, 1e-6));
  CHECK_FALSE(t0.requireAll);
  REQUIRE(t0.conditions.size() == 2);
  CHECK(t0.conditions[0].type == ConditionType::TriggerConsumed);
  CHECK(t0.conditions[0].param == "attackR1");
  CHECK(t0.conditions[1].type == ConditionType::BoolTrue);

  CHECK(sm->transitions[1].conditions.at(0).type == ConditionType::SourceNodeFinished);
  CHECK(sm->transitions[1].conditions.at(0).param.empty());

  const auto& anyState = sm->transitions[2];
  CHECK(anyState.fromState.empty());
  CHECK_THAT(anyState.blendTime, WithinAbs(0.2, 1e-6));
  CHECK(anyState.requireAll);
  CHECK(anyState.conditions.at(0).type == ConditionType::BoolFalse);

  const auto* b1 = defAt<Blend1DNode::Definition>(*def, 1);
  REQUIRE(b1 != nullptr);
  CHECK(b1->paramName == "moveSpeed");
  CHECK(b1->thresholds == std::vector<float>{ 0.0f, 1.0f });
  CHECK(b1->childNodeIndices == std::vector<int16_t>{ 2, 3 });

  REQUIRE(defAt<ClipNode::Definition>(*def, 2) != nullptr);
  CHECK(defAt<ClipNode::Definition>(*def, 2)->loop);
  REQUIRE(defAt<ClipNode::Definition>(*def, 4) != nullptr);
  CHECK_FALSE(defAt<ClipNode::Definition>(*def, 4)->loop);

  const auto* sel = defAt<ClipSelectorNode::Definition>(*def, 5);
  REQUIRE(sel != nullptr);
  CHECK(sel->paramName == "dodgeDir");
  REQUIRE(sel->entries.size() == 2);
  CHECK(sel->entries[1].selectorIndex == 1);
  CHECK(sel->entries[1].nodeIndex == 7);

  const auto* b2 = defAt<Blend2DNode::Definition>(*def, 8);
  REQUIRE(b2 != nullptr);
  CHECK(b2->xParamName == "moveX");
  REQUIRE(b2->clips.size() == 3);
  CHECK(b2->clips[1].x == 1.0f);
  CHECK(b2->clips[2].nodeIndex == 11);

  SECTION("and the result instantiates") {
    GraphInstance inst = GraphInstance::Create(def);
    REQUIRE(inst.GetRootNode() != nullptr);
    CHECK(nodeAt<StateMachineNode>(inst, 0).getCurrentState() == "Loco");
    CHECK(nodeAt<Blend2DNode>(inst, 8).m_triangles.size() == 1);
  }
}

TEST_CASE("GraphDefinition rejects unreadable input with an error",
          "[component][anim][loader]") {
  SECTION("missing file") {
    CHECK(GraphDefinition::LoadFromJson(dataFile("graphs/does_not_exist.json")) == nullptr);
    CHECK(LogCapture::contains(spdlog::level::err, "cannot open"));
  }
  SECTION("malformed JSON") {
    CHECK(GraphDefinition::LoadFromJson(dataFile("graphs/malformed.json")) == nullptr);
    CHECK(LogCapture::contains(spdlog::level::err, "parse error"));
  }
  SECTION("unsupported version") {
    CHECK(GraphDefinition::LoadFromJson(dataFile("graphs/bad_version.json")) == nullptr);
    CHECK(LogCapture::contains(spdlog::level::err, "unsupported version"));
  }
}

// root["nodes"] on a non-const json inserts null, null.size() == 0
TEST_CASE("GraphDefinition rejects a graph with no nodes array",
          "[component][anim][loader][!shouldfail][bug:N6]") {
  CHECK(GraphDefinition::LoadFromJson(dataFile("graphs/no_nodes.json")) == nullptr);
}

// Never call Create() on these definitions: the type table / child indices are
// broken and Create() would walk off the end of m_nodes (UB, not a test failure)
TEST_CASE("GraphDefinition rejects unknown node types instead of misaligning its type table",
          "[component][anim][loader][!shouldfail][bug:A7]") {
  auto def = GraphDefinition::LoadFromJson(dataFile("graphs/unknown_node_type.json"));
  CHECK((def == nullptr || def->m_nodeDefs.size() == def->m_nodeTypeIDs.size()));
}

TEST_CASE("GraphDefinition rejects child indices that point outside the node array",
          "[component][anim][loader][!shouldfail][bug:A7]") {
  CHECK(GraphDefinition::LoadFromJson(dataFile("graphs/child_out_of_range.json")) == nullptr);
}
