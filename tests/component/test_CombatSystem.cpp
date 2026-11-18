#include "support/AnimTestUtils.h"
#include "support/LogCapture.h"

#include <forge/Animator.h>
#include <forge/CombatComponent.h>
#include <forge/CombatSystem.h>
#include <forge/EventBus.h>
#include <forge/Events.h>
#include <forge/LuaState.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

using namespace forge;
using namespace forge::test;
using Catch::Matchers::WithinAbs;

namespace {

// Attacker's whole clip is one SpawnHitbox window so a hitbox is live every step
// and the weapon capsule is placed directly in world space
struct CombatRig {
  static constexpr float kDt = 1.0f / 60.0f;

  Animator attackerAnim, defenderAnim;
  CombatComponent attacker{ "attacker", 1000.0f, 100.0f, 100.0f };
  CombatComponent defender{ "defender", 1000.0f, 100.0f, 100.0f };
  CombatSystem system;
  std::vector<WorldCapsule> weapon{ WorldCapsule{ { -0.5f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, 0.1f } };

  CombatRig() {
    attackerAnim.setSkeleton(makeSkeleton({ "root" }));
    defenderAnim.setSkeleton(makeSkeleton({ "root" }));
    attackerAnim.setPreviewClip(ClipBuilder("swing").duration(2.0f).hitbox(0.0f, 2.0f).build(), true);
    defenderAnim.setPreviewClip(ClipBuilder("stand").duration(1.0f).build(), true);

    attacker.setAnimator(&attackerAnim);
    // Required even for the defender: update() dereferences m_animator unconditionally (N2)
    defender.setAnimator(&defenderAnim);

    attacker.setWorldData({ 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f });
    defender.setWorldData({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f });
    defender.setBodyCapsule(0.3f, 0.9f);

    system.registerCombatant(&attacker);
    system.registerCombatant(&defender);
  }

  void step() {
    attackerAnim.update(kDt);
    defenderAnim.update(kDt);
    attacker.setWorldCapsules(weapon);
    system.update(kDt);
  }

  void useLua(LuaState& lua, const char* script) {
    lua.get().script(script);
    system.setLuaState(&lua);
  }
};

// AttackData's default damage, no tryAttack is issued in these tests
constexpr float kBaseDamage = 100.0f;

}

TEST_CASE("CombatSystem damages an overlapped defender once per active window",
          "[component][combat][resolution]") {
  CombatRig rig;
  rig.step();
  CHECK(rig.defender.getHp() == 1000.0f - kBaseDamage);
  for (int i = 0; i < 5; ++i) rig.step();
  CHECK(rig.defender.getHp() == 1000.0f - kBaseDamage);
  CHECK(rig.attacker.getHitFlash());
}

// Defender body: segment y in [0.3, 1.5] at the origin, radius 0.3
TEST_CASE("CombatSystem hit detection matches capsule-vs-body geometry",
          "[component][combat][geometry]") {
  struct GeometryCase { const char* what; WorldCapsule capsule; bool expectHit; };
  const GeometryCase c = GENERATE(values<GeometryCase>({
    { "blade through the body",       { { -0.5f, 1.00f, 0.000f }, { 0.5f, 1.00f, 0.000f }, 0.1f }, true },
    { "grazing, 1mm inside",          { { -0.5f, 1.00f, 0.399f }, { 0.5f, 1.00f, 0.399f }, 0.1f }, true },
    { "near miss, 1mm outside",       { { -0.5f, 1.00f, 0.401f }, { 0.5f, 1.00f, 0.401f }, 0.1f }, false },
    { "far away",                     { {  4.5f, 1.00f, 0.000f }, { 5.5f, 1.00f, 0.000f }, 0.1f }, false },
    { "degenerate point capsule",     { {  0.0f, 1.00f, 0.350f }, { 0.0f, 1.00f, 0.350f }, 0.1f }, true },
    { "just over the head",           { { -0.5f, 1.91f, 0.000f }, { 0.5f, 1.91f, 0.000f }, 0.1f }, false },
    { "clipping the top of the head", { { -0.5f, 1.89f, 0.000f }, { 0.5f, 1.89f, 0.000f }, 0.1f }, true },
  }));
  CAPTURE(c.what);

  CombatRig rig;
  rig.weapon = { c.capsule };
  rig.step();
  CHECK((rig.defender.getHp() < 1000.0f) == c.expectHit);
}

TEST_CASE("CombatSystem never lets a combatant hit itself",
          "[component][combat][resolution]") {
  CombatRig rig;
  rig.defender.setWorldData({ 50.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f });
  rig.weapon = { WorldCapsule{ { -0.5f, 1.0f, -1.0f }, { 0.5f, 1.0f, -1.0f }, 0.1f } };
  rig.step();
  CHECK(rig.attacker.getHp() == 1000.0f);
  CHECK(rig.defender.getHp() == 1000.0f);
}

TEST_CASE("CombatSystem ignores dead defenders",
          "[component][combat][resolution]") {
  CombatRig rig;
  int hits = 0;
  EventBus::subscribe<EntityHitEvent>([&](const EntityHitEvent&) { ++hits; });
  rig.defender.takeDamage(5000.0f);
  REQUIRE_FALSE(rig.defender.isAlive());
  rig.step();
  CHECK(hits == 0);
}

TEST_CASE("CombatSystem publishes EntityHitEvent for each landed hit",
          "[component][combat][events]") {
  CombatRig rig;
  std::vector<EntityHitEvent> seen;
  EventBus::subscribe<EntityHitEvent>([&](const EntityHitEvent& e) { seen.push_back(e); });
  rig.step();
  REQUIRE(seen.size() == 1);
  CHECK(seen[0].attackerName == "attacker");
  CHECK(seen[0].defenderName == "defender");
  CHECK(seen[0].damage == kBaseDamage);
}

TEST_CASE("CombatSystem Lua parry_success deals no damage",
          "[component][combat][lua]") {
  CombatRig rig;
  LuaState lua;
  rig.useLua(lua, R"lua(
    function onHitResolution(ctx) return { type = "parry_success" } end
  )lua");
  rig.step();
  CHECK(rig.defender.getHp() == 1000.0f);
  CHECK(rig.attacker.getHitFlash());
}

// Both resolution branches test "parry_success", deflect falls through to full damage
TEST_CASE("CombatSystem Lua deflect_success deals no damage",
          "[component][combat][lua][!shouldfail][bug:C1]") {
  CombatRig rig;
  LuaState lua;
  rig.useLua(lua, R"lua(
    function onHitResolution(ctx) return { type = "deflect_success" } end
  )lua");
  rig.step();
  CHECK(rig.defender.getHp() == 1000.0f);
}

// applyResolution copies ctx.hit and never reads result.damage
TEST_CASE("CombatSystem applies the damage Lua returns",
          "[component][combat][lua][!shouldfail][bug:C2]") {
  CombatRig rig;
  LuaState lua;
  rig.useLua(lua, R"lua(
    function onHitResolution(ctx) return { type = "hit", damage = ctx.damage * 0.25 } end
  )lua");
  rig.step();
  CHECK_THAT(rig.defender.getHp(), WithinAbs(1000.0 - kBaseDamage * 0.25, 1e-3));
}

TEST_CASE("CombatSystem falls back to a plain hit when onHitResolution errors",
          "[component][combat][lua]") {
  CombatRig rig;
  LuaState lua;
  rig.useLua(lua, R"lua(
    function onHitResolution(ctx) error("boom") end
  )lua");
  rig.step();
  CHECK(rig.defender.getHp() == 1000.0f - kBaseDamage);
  CHECK(LogCapture::contains(spdlog::level::err, "onHitResolution error"));
}

TEST_CASE("CombatSystem passes the documented fields to onHitResolution",
          "[component][combat][lua][contract]") {
  CombatRig rig;
  LuaState lua;
  rig.useLua(lua, R"lua(
    function onHitResolution(ctx)
      seen = {
        damage = ctx.damage, poiseDamage = ctx.poiseDamage, damageType = ctx.damageType,
        defenderState = ctx.defenderState, inReceptiveWindow = ctx.inReceptiveWindow,
        receptiveType = ctx.receptiveType, facingAngle = ctx.facingAngle, perilous = ctx.perilous
      }
      return { type = "hit" }
    end
  )lua");
  rig.step();

  sol::table seen = lua.get()["seen"];
  REQUIRE(seen.valid());
  CHECK(seen.get<float>("damage") == kBaseDamage);
  CHECK(seen.get<float>("poiseDamage") == AttackData{}.poiseDamage);
  CHECK(seen.get<std::string>("damageType") == "slash");
  CHECK_FALSE(seen.get<bool>("inReceptiveWindow"));
  CHECK(seen.get<std::string>("receptiveType") == "None");
  CHECK_FALSE(seen.get<bool>("perilous"));
  CHECK_THAT(seen.get<float>("facingAngle"), WithinAbs(0.0, 1e-3));
}

// combatStateName() returns "idle" for Idle but capitalised names for every other state
TEST_CASE("CombatSystem state names in the Lua context match CombatComponent::getStateName",
          "[component][combat][lua][contract][!shouldfail][bug:N8]") {
  CombatRig rig;
  LuaState lua;
  rig.useLua(lua, R"lua(
    function onHitResolution(ctx) seenState = ctx.defenderState; return { type = "parry_success" } end
  )lua");
  rig.step();
  CHECK(lua.get()["seenState"].get<std::string>() == rig.defender.getStateName());
}
