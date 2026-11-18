#include "support/AnimTestUtils.h"
#include "support/GlmMatchers.h"

#include <forge/AnimationClip.h>
#include <forge/Bone.h>

#include <glm/gtc/quaternion.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace forge;
using namespace forge::test;
using Catch::Matchers::WithinAbs;

TEST_CASE("BoneTrack interpolates between keyframes and clamps outside them",
          "[unit][anim][sampling]") {
  BoneTrack track;
  track.boneName = "b";
  track.keyframes = {
    BoneKeyFrame{ 0.0f, glm::vec3(0, 0, 0), glm::quat(1, 0, 0, 0), glm::vec3(1) },
    BoneKeyFrame{ 1.0f, glm::vec3(0, 0, 2),
                  glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)), glm::vec3(1) },
  };

  SECTION("position is linear") {
    CHECK_THAT(track.sample(0.25f).position, Near({ 0, 0, 0.5f }));
  }
  SECTION("rotation slerps") {
    CHECK_THAT(glm::angle(track.sample(0.5f).rotation), WithinAbs(glm::radians(45.0f), 1e-4));
  }
  SECTION("before the first key returns the first key") {
    CHECK_THAT(track.sample(-1.0f).position, Near({ 0, 0, 0 }));
  }
  SECTION("after the last key returns the last key") {
    CHECK_THAT(track.sample(5.0f).position, Near({ 0, 0, 2 }));
  }
}

TEST_CASE("BoneTrack with one keyframe always returns it",
          "[unit][anim][sampling]") {
  BoneTrack track;
  track.keyframes = { BoneKeyFrame{ 0.3f, glm::vec3(1, 2, 3), glm::quat(1, 0, 0, 0), glm::vec3(1) } };
  CHECK_THAT(track.sample(0.0f).position, Near({ 1, 2, 3 }));
  CHECK_THAT(track.sample(9.0f).position, Near({ 1, 2, 3 }));
}

TEST_CASE("AnimationClip root delta is the root bone's displacement between two times",
          "[unit][anim][sampling][rootmotion]") {
  auto clip = ClipBuilder().duration(1.0f).rootMotion({ 0, 0, 2 }).build();
  CHECK_THAT(clip->sampleRootDelta(0.25f, 0.5f), Near({ 0, 0, 0.5f }));
  CHECK_THAT(clip->sampleRootDelta(0.5f, 0.5f), Near({ 0, 0, 0 }));
}

TEST_CASE("AnimationClip root delta is split across the loop seam",
          "[unit][anim][sampling][rootmotion]") {
  auto clip = ClipBuilder().duration(1.0f).rootMotion({ 0, 0, 2 }).build();
  CHECK_THAT(clip->sampleRootDelta(0.9f, 0.1f), Near({ 0, 0, 0.4f }, 1e-3f));
}

TEST_CASE("AnimationClip without root motion reports zero delta",
          "[unit][anim][sampling][rootmotion]") {
  auto clip = ClipBuilder().duration(1.0f).build();
  CHECK_THAT(clip->sampleRootDelta(0.0f, 0.5f), Near({ 0, 0, 0 }));
}

