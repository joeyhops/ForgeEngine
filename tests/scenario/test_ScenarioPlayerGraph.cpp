#include "support/AnimTestUtils.h"
#include "support/Golden.h"
#include "support/LogCapture.h"
#include "support/TestPaths.h"

#include <forge/AnimEventReader.h>
#include <forge/AnimGraph.h>
#include <forge/AssetManager.h>
#include <forge/Animator.h>
#include <forge/EquipmentComponent.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace forge;
using namespace forge::test;

namespace {

struct Step {
  const char* label;
  int frames;
  std::function<void(AnimParamTable&)> enter;
};

std::string line(const char* fmt, int frame, const std::string& a, const std::string& b = {}) {
  char buff[256];
  std::snprintf(buff, sizeof(buff), fmt, frame, a.c_str(), b.c_str());
  return buff;
}

}

TEST_CASE("Scenario: player graph basic moveset timeline matches the golden",
          "[scenario][anim][lfs][slow]") {
  FORGE_REQUIRE_REAL_ASSET("anim/manny/idle/am_Stand_Idle_01.glb");
  FORGE_REQUIRE_REAL_ASSET("anim/manny/attacks/Anim_SwordV2_Slash1.glb");

  AssetManager::loadWeaponDefs("data/weapons.json");
  AssetManager::loadMovesetDefs("data/movesets.json");

  auto def = GraphDefinition::LoadFromJson("anim/player_graph.json");
  REQUIRE(def);

  Animator animator;
  animator.setOwnerName("player");
  animator.setSkeleton(makeSkeleton({ "root" }));
  animator.setGraph(GraphInstance::Create(def));

  // Same path the game uses: weapon def -> moveset -> swapMovesetClips (+ TAE sidecars)
  EquipmentComponent equipment("player");
  equipment.setAnimator(&animator);
  REQUIRE(equipment.equip(EquipmentComponent::RIGHT_HAND, "longsword"));

  const std::vector<Step> script = {
    { "idle",           30, [](AnimParamTable& p) { p.setFloat("moveSpeed", 0.0f); } },
    { "walk",           60, [](AnimParamTable& p) { p.setFloat("moveSpeed", 1.0f); } },
    { "sprint",         60, [](AnimParamTable& p) { p.setFloat("moveSpeed", 2.0f); } },
    { "attack_r1",      90, [](AnimParamTable& p) { p.setFloat("moveSpeed", 0.0f); p.setTrigger("attackR1"); } },
    { "dodge_forward",  75, [](AnimParamTable& p) { p.setInt("dodgeDir", 0); p.setTrigger("dodge"); } },
    { "lock_on_strafe", 60, [](AnimParamTable& p) {
        p.setBool("isLockedOn", true); p.setFloat("moveX", 0.71f); p.setFloat("moveZ", 0.71f); } },
    { "lock_off",       20, [](AnimParamTable& p) {
        p.setBool("isLockedOn", false); p.setFloat("moveX", 0.0f); p.setFloat("moveZ", 0.0f); } },
    { "hit_reaction",   60, [](AnimParamTable& p) { p.setTrigger("hitReaction"); } },
    { "death",          90, [](AnimParamTable& p) { p.setBool("isDead", true); } },
    { "revive",         60, [](AnimParamTable& p) { p.setBool("isDead", false); } },
  };

  constexpr float kDt = 1.0f / 60.0f;
  AnimEventReader reader;
  std::string timeline = "# scenario: player_graph + longsword (sword2), dt = 1/60\n"
                         "# frame  what     detail\n";
  std::string lastState;
  int frame = 0;

  for (const auto& step : script) {
    step.enter(animator.getParams());
    timeline += line("%04d  step     %s\n", frame, step.label);
    glm::vec3 rootMotion(0.0f);

    for (int f = 0; f < step.frames; ++f, ++frame) {
      animator.update(kDt);
      reader.refresh(animator.sampledEvents());
      rootMotion += animator.getRootMotionDelta();

      const std::string state = animator.getCurrentStateName();
      if (state != lastState) {
        timeline += line("%04d  state    %s\n", frame, state);
        lastState = state;
      }
      for (int t = 0; t < kAnimEventTypeCount; ++t) {
        const auto type = static_cast<AnimEventType>(t);
        if (reader.openedType(type)) timeline += line("%04d  open     %s\n", frame, eventTypeName(type));
        if (reader.closedType(type)) timeline += line("%04d  close    %s\n", frame, eventTypeName(type));
      }
    }

    // Rounded to mm, the only float in the golden
    char rm[128];
    std::snprintf(rm, sizeof(rm), "(%.3f, %.3f, %.3f)", rootMotion.x, rootMotion.y, rootMotion.z);
    timeline += line("%04d  rm       %s %s\n", frame, step.label, rm);
  }

  INFO("captured errors:\n" << LogCapture::dump());
  CHECK(LogCapture::count(spdlog::level::err) == 0);
  checkGolden("scenario_player_basic.txt", timeline);
}
