#include "support/AnimTestUtils.h"
#include "support/GlmMatchers.h"
#include "support/GraphBuilder.h"
#include "support/LogCapture.h"

#include <forge/AnimGraph.h>
#include <forge/Animator.h>

#include <glm/gtc/matrix_transform.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace forge;
using namespace forge::test;

TEST_CASE("Animator exposes the frame's sampled events and clears them every frame",
          "[component][anim][animator]") {
  Animator a;
  a.setSkeleton(makeSkeleton({ "root" }));
  a.setPreviewClip(ClipBuilder().duration(1.0f).hitbox(0.0f, 0.5f).build(), true);

  a.update(0.125f);
  CHECK(countEvents(a.sampledEvents(), AnimEventType::SpawnHitbox) == 1);
  a.update(0.5f);
  CHECK(countEvents(a.sampledEvents(), AnimEventType::SpawnHitbox) == 0);
}

// The capsule moves by the delta, so the pose must not also carry it or the character double-steps
TEST_CASE("Animator reports root motion as a delta and strips it from the root bone pose",
          "[component][anim][animator][rootmotion]") {
  Animator a;
  a.setSkeleton(makeSkeleton({ "root" }));
  a.setPreviewClip(ClipBuilder().duration(1.0f).rootMotion({ 0, 0, 1 }).build(), true);

  a.update(0.125f);
  CHECK_THAT(a.getRootMotionDelta(), Near({ 0, 0, 0.125f }));
  CHECK(a.isRootMotionActive());
  CHECK_THAT(glm::vec3(a.getGlobalTransforms()[0][3]), Near({ 0, 0, 0 }));
}

TEST_CASE("Animator composes bone matrices from parent to child",
          "[component][anim][animator]") {
  std::vector<Bone> skeleton = makeSkeleton({ "root", "child" });
  skeleton[0].localTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1, 0, 0));
  skeleton[1].localTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0));

  Animator a;
  a.setSkeleton(skeleton);
  a.setPreviewClip(ClipBuilder().duration(1.0f).build(), true);
  a.update(0.1f);

  CHECK_THAT(glm::vec3(a.getGlobalTransforms()[1][3]), Near({ 1, 1, 0 }));
  CHECK_THAT(glm::vec3(a.getBoneMatrices()[1][3]), Near({ 1, 1, 0 }));
}

TEST_CASE("Animator returns every pose buffer to the pool on every frame of a blended graph",
          "[component][anim][animator][tasks]") {
#ifndef FORGE_DEBUG
  SKIP("the pool leak check only exists with FORGE_DEBUG_OVERLAYS=ON");
#endif
  GraphBuilder b;
  const auto walk = b.clip("walk");
  const auto run = b.clip("run");
  const auto blend = b.blend1D("speed", { 0.0f, 1.0f }, { walk, run });
  auto def = b.root(blend).build();
  GraphInstance inst = GraphInstance::Create(def);
  nodeAt<ClipNode>(inst, walk).setClip(ClipBuilder("walk").duration(1.0f).build());
  nodeAt<ClipNode>(inst, run).setClip(ClipBuilder("run").duration(0.7f).build());

  Animator a;
  a.setSkeleton(makeSkeleton({ "root", "spine", "head" }));
  a.setGraph(std::move(inst));
  a.getParams().setFloat("speed", 0.5f);

  for (int f = 0; f < 120; ++f) a.update(1.0f / 60.0f);
  CHECK(LogCapture::count(spdlog::level::err, "pose buffer leak") == 0);
}

TEST_CASE("Animator reports the root state machine's current state",
          "[component][anim][animator]") {
  GraphBuilder b;
  const auto idle = b.clip("idle");
  const auto run = b.clip("run");
  const auto sm = b.stateMachine("Idle", { { "Idle", idle }, { "Run", run } },
                                 { transition("Idle", "Run", 0.0f, { whenTrue("moving") }) });
  auto def = b.root(sm).build();
  GraphInstance inst = GraphInstance::Create(def);
  nodeAt<ClipNode>(inst, idle).setClip(ClipBuilder("idle").build());
  nodeAt<ClipNode>(inst, run).setClip(ClipBuilder("run").build());

  Animator a;
  a.setSkeleton(makeSkeleton({ "root" }));
  a.setGraph(std::move(inst));
  CHECK(a.getCurrentStateName() == "Idle");
  a.getParams().setBool("moving", true);
  a.update(1.0f / 60.0f);
  CHECK(a.getCurrentStateName() == "Run");

  SECTION("a graph whose root is not a state machine reports an empty name") {
    a.setPreviewClip(ClipBuilder().build());
    CHECK(a.getCurrentStateName().empty());
  }
}
