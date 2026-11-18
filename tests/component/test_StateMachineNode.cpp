#include "support/AnimTestUtils.h"
#include "support/GraphBuilder.h"
#include "support/NodeTicker.h"

#include <forge/AnimEventReader.h>
#include <forge/AnimGraph.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>
#include <vector>

using namespace forge;
using namespace forge::test;
using Catch::Matchers::WithinAbs;

namespace {

std::shared_ptr<AnimationClip> stateClip(const std::string& name, float duration = 1.0f) {
  return ClipBuilder(name).duration(duration).window(AnimEventType::SFXSpawn, 0.0f, duration, name).build();
}

struct StateSpec {
  std::string name;
  std::shared_ptr<AnimationClip> clip;
  bool loop = true;
};

struct SMRig {
  GraphBuilder builder;
  std::vector<int16_t> clipIdx;
  int16_t sm = -1;
  std::shared_ptr<GraphDefinition> def;
  GraphInstance inst;
  NodeTicker ticker;

  SMRig(const std::vector<StateSpec>& states,
        std::vector<StateMachineNode::TransitionDef> transitions) {
    std::vector<StateMachineNode::StateDef> stateDefs;
    for (const auto& s : states) {
      clipIdx.push_back(builder.clip(s.name, s.loop));
      stateDefs.push_back({ s.name, clipIdx.back() });
    }
    sm = builder.stateMachine(states.front().name, stateDefs, std::move(transitions));
    def = builder.root(sm).build();
    inst = GraphInstance::Create(def);
    for (size_t i = 0; i < states.size(); ++i)
      nodeAt<ClipNode>(inst, clipIdx[i]).setClip(states[i].clip);
  }

  StateMachineNode& node() { return nodeAt<StateMachineNode>(inst, sm); }
  const std::string& state() { return node().getCurrentState(); }
  UpdateResult tick(float dt = 0.125f) { return ticker.tick(node(), dt); }
  const SampledEvent* event(const std::string& tag) const {
    return findEvent(ticker.events(), AnimEventType::SFXSpawn, tag);
  }
};

}

TEST_CASE("StateMachineNode starts in its initial state",
          "[component][anim][sm]") {
  SMRig rig({ { "Idle", stateClip("Idle") }, { "Run", stateClip("Run") } }, {});
  CHECK(rig.state() == "Idle");
  rig.tick();
  CHECK(rig.event("Idle") != nullptr);
  CHECK(rig.event("Run") == nullptr);
}

TEST_CASE("StateMachineNode trigger transitions fire once and consume the trigger",
          "[component][anim][sm]") {
  SMRig rig({ { "A", stateClip("A") }, { "B", stateClip("B") } },
            { transition("A", "B", 0.0f, { onTrigger("go") }) });
  rig.tick();
  CHECK(rig.state() == "A");
  rig.ticker.params.setTrigger("go");
  rig.tick();
  CHECK(rig.state() == "B");
  CHECK_FALSE(rig.ticker.params.hasTrigger("go"));
}

TEST_CASE("StateMachineNode crossfade alpha ramps linearly from the frame after the transition",
          "[component][anim][sm][crossfade]") {
  SMRig rig({ { "A", stateClip("A") }, { "B", stateClip("B") } },
            { transition("A", "B", 0.5f, { onTrigger("go") }) });
  rig.ticker.params.setTrigger("go");
  rig.tick();
  REQUIRE(rig.state() == "B");
  CHECK(rig.node().m_blendAlpha == 0.0f);

  const float expected[] = { 0.25f, 0.5f, 0.75f, 1.0f };
  for (float a : expected) {
    rig.tick();
    CHECK_THAT(rig.node().m_blendAlpha, WithinAbs(a, 1e-6));
  }
}

TEST_CASE("StateMachineNode crossfade down-weights the outgoing state and marks it as fading",
          "[component][anim][sm][crossfade][events]") {
  SMRig rig({ { "A", stateClip("A") }, { "B", stateClip("B") } },
            { transition("A", "B", 0.5f, { onTrigger("go") }) });
  rig.ticker.params.setTrigger("go");
  rig.tick();
  rig.tick();

  const SampledEvent* a = rig.event("A");
  const SampledEvent* b = rig.event("B");
  REQUIRE(a != nullptr);
  REQUIRE(b != nullptr);
  CHECK_THAT(a->weight, WithinAbs(0.75, 1e-6));
  CHECK_FALSE(HasFlag(a->flags, SampledEvent::Flags::FromActiveBranch));
  CHECK_THAT(b->weight, WithinAbs(0.25, 1e-6));
  CHECK(HasFlag(b->flags, SampledEvent::Flags::FromActiveBranch));
}

TEST_CASE("StateMachineNode instant transitions drop the old state's events that frame",
          "[component][anim][sm][events]") {
  SMRig rig({ { "A", stateClip("A") }, { "B", stateClip("B") } },
            { transition("A", "B", 0.0f, { onTrigger("go") }) });
  rig.ticker.params.setTrigger("go");
  rig.tick();
  REQUIRE(rig.state() == "B");
  REQUIRE(rig.event("A") != nullptr);
  CHECK(rig.event("A")->weight == 0.0f);
  REQUIRE(rig.event("B") != nullptr);
  CHECK(rig.event("B")->weight == 1.0f);
}

TEST_CASE("StateMachineNode checks named transitions before any-state transitions",
          "[component][anim][sm]") {
  SMRig rig({ { "A", stateClip("A") }, { "B", stateClip("B") }, { "C", stateClip("C") } },
            { transition("", "C", 0.0f, { whenTrue("go") }),
              transition("A", "B", 0.0f, { whenTrue("go") }) });
  rig.ticker.params.setBool("go", true);
  rig.tick();
  CHECK(rig.state() == "B");

  SECTION("any-state transitions fire from states without a named match") {
    rig.tick();
    CHECK(rig.state() == "C");
  }
}

TEST_CASE("StateMachineNode SourceNodeFinished waits for a non-looping state to end",
          "[component][anim][sm]") {
  SMRig rig({ { "Attack", stateClip("Attack", 0.5f), false }, { "Idle", stateClip("Idle") } },
            { transition("Attack", "Idle", 0.0f, { whenFinished() }) });
  for (int f = 0; f < 3; ++f) {
    rig.tick();
    CHECK(rig.state() == "Attack");
  }
  rig.tick();
  CHECK(rig.state() == "Idle");
}

TEST_CASE("StateMachineNode bool transitions follow the param both ways",
          "[component][anim][sm]") {
  SMRig rig({ { "Free", stateClip("Free") }, { "Locked", stateClip("Locked") } },
            { transition("Free", "Locked", 0.0f, { whenTrue("isLockedOn") }),
              transition("Locked", "Free", 0.0f, { whenFalse("isLockedOn") }) });
  rig.ticker.params.setBool("isLockedOn", true);
  rig.tick();
  CHECK(rig.state() == "Locked");
  rig.tick();
  CHECK(rig.state() == "Locked");
  rig.ticker.params.setBool("isLockedOn", false);
  rig.tick();
  CHECK(rig.state() == "Free");
}

// evaluateConditions consumes triggers while walking an AND list, so a later
// failing condition eats the trigger without the transition firing
TEST_CASE("StateMachineNode keeps a trigger when the rest of an AND-transition fails",
          "[component][anim][sm][!shouldfail][bug:A4]") {
  SMRig rig({ { "Idle", stateClip("Idle") }, { "Attack", stateClip("Attack") } },
            { transition("Idle", "Attack", 0.0f, { onTrigger("attack"), whenTrue("canAttack") }) });
  rig.ticker.params.setTrigger("attack");
  rig.ticker.params.setBool("canAttack", false);
  rig.tick();
  REQUIRE(rig.state() == "Idle");
  CHECK(rig.ticker.params.hasTrigger("attack"));
}

TEST_CASE("StateMachineNode does not let a failed transition steal a later one's trigger",
          "[component][anim][sm][!shouldfail][bug:A4]") {
  SMRig rig({ { "Idle", stateClip("Idle") }, { "Heavy", stateClip("Heavy") }, { "Light", stateClip("Light") } },
            { transition("Idle", "Heavy", 0.0f, { onTrigger("attack"), whenTrue("charged") }),
              transition("Idle", "Light", 0.0f, { onTrigger("attack") }) });
  rig.ticker.params.setTrigger("attack");
  rig.tick();
  CHECK(rig.state() == "Light");
}

// The frame that lands exactly on 1.0 fails `cur > at` and starts the fade; the
// next frame crosses 1.0 but by then the state is the fading source and
// MarkTransitionSource hides the event from AnimEventReader
TEST_CASE("StateMachineNode lets gameplay observe a one-shot authored at a non-looping state's end",
          "[component][anim][sm][events][!shouldfail][bug:S3-end-event]") {
  auto roll = ClipBuilder("Roll").duration(1.0f).oneShot(AnimEventType::RootMotionEnd, 1.0f).build();
  SMRig rig({ { "Roll", roll, false }, { "Loco", stateClip("Loco") } },
            { transition("Roll", "Loco", 0.25f, { whenFinished() }) });

  AnimEventReader reader;
  bool observed = false;
  for (int f = 0; f < 16; ++f) {
    rig.tick();
    reader.refresh(rig.ticker.events());
    observed = observed || reader.openedType(AnimEventType::RootMotionEnd);
  }
  CHECK(observed);
}
