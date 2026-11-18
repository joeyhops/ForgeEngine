#include "support/AnimTestUtils.h"
#include "support/GraphBuilder.h"
#include "support/LogCapture.h"

#include <forge/AnimGraph.h>
#include <forge/Animator.h>
#include <forge/CombatComponent.h>
#include <forge/WeaponDef.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace forge;
using namespace forge::test;
using Catch::Matchers::WithinAbs;

namespace {

HitEvent hit(float damage, float poiseDamage = 0.0f) {
  return HitEvent{ damage, poiseDamage, glm::vec3(0.0f, 0.0f, 1.0f), "slash" };
}

AttackData attack(const std::string& name, float staminaCost = 10.0f) {
  AttackData a;
  a.name = name;
  a.staminaCost = staminaCost;
  return a;
}

// Never copied or returned by value: CombatComponent's FSM callbacks capture `this`
struct AnimatedFighter {
  Animator animator;
  CombatComponent combat{ "fighter", 100.0f, 100.0f, 50.0f };

  explicit AnimatedFighter(std::shared_ptr<AnimationClip> clip, bool loop = true) {
    animator.setSkeleton(makeSkeleton({ "root" }));
    animator.setPreviewClip(std::move(clip), loop);
    combat.setAnimator(&animator);
    combat.setParamTable(&animator.getParams());
  }

  void step(float dt = 0.125f) {
    animator.update(dt);
    combat.update(dt);
  }
};

}

TEST_CASE("CombatComponent starts Idle with full stats",
          "[component][combat]") {
  CombatComponent c("fighter", 100.0f, 80.0f, 50.0f);
  CHECK(c.getState() == CombatState::Idle);
  CHECK(c.getStateName() == "Idle");
  CHECK(c.getHp() == 100.0f);
  CHECK(c.getStamina() == 80.0f);
  CHECK(c.getPoise() == 50.0f);
  CHECK(c.isAlive());
  CHECK_FALSE(c.isVulnerable());
  CHECK_FALSE(c.hasActiveHitbox());
}

TEST_CASE("CombatComponent hits remove hp and poise",
          "[component][combat]") {
  CombatComponent c("fighter", 100.0f, 100.0f, 50.0f);
  c.applyHit(hit(30.0f, 20.0f));
  CHECK(c.getHp() == 70.0f);
  CHECK(c.getPoise() == 30.0f);
  CHECK(c.getState() == CombatState::Idle);
}

TEST_CASE("CombatComponent breaking poise staggers and requests a hit reaction",
          "[component][combat]") {
  AnimParamTable params;
  CombatComponent c("fighter", 100.0f, 100.0f, 10.0f);
  c.setParamTable(&params);
  c.applyHit(hit(5.0f, 20.0f));
  CHECK(c.getState() == CombatState::Staggered);
  CHECK(params.hasTrigger("hitReaction"));
  CHECK(c.isVulnerable());
}

TEST_CASE("CombatComponent lethal damage kills exactly once",
          "[component][combat]") {
  AnimParamTable params;
  CombatComponent c("fighter", 100.0f, 100.0f, 50.0f);
  c.setParamTable(&params);
  int deaths = 0;
  c.onDeath = [&] { ++deaths; };

  c.applyHit(hit(250.0f));
  CHECK(c.getState() == CombatState::Dead);
  CHECK(c.getHp() == 0.0f);
  CHECK_FALSE(c.isAlive());
  CHECK(params.getBool("isDead"));
  CHECK(deaths == 1);

  c.applyHit(hit(10.0f));
  CHECK(deaths == 1);
  CHECK(c.getHp() == 0.0f);
}

TEST_CASE("CombatComponent guarding blocks 90 percent of damage and costs stamina",
          "[component][combat][guard]") {
  CombatComponent c("fighter", 100.0f, 100.0f, 50.0f);
  REQUIRE(c.tryGuard());
  int hits = 0;
  c.onHit = [&](const HitEvent&) { ++hits; };

  c.applyHit(hit(100.0f, 50.0f));
  CHECK_THAT(c.getHp(), WithinAbs(90.0, 1e-4));
  CHECK(c.getStamina() == 50.0f);
  CHECK(c.getPoise() == 50.0f);
  CHECK(c.getState() == CombatState::Guarding);
  CHECK(hits == 1);
}

TEST_CASE("CombatComponent guard breaks when a block costs more stamina than remains",
          "[component][combat][guard]") {
  CombatComponent c("fighter", 100.0f, 30.0f, 50.0f);
  REQUIRE(c.tryGuard());
  c.applyHit(hit(100.0f));
  CHECK(c.getState() == CombatState::GuardBroken);
  CHECK_THAT(c.getHp(), WithinAbs(90.0, 1e-4));
}

// Guard branch subtracts chip damage with no clamp and no death check, leaving
// a "guarding zombie" at negative hp that CombatSystem no longer targets
TEST_CASE("CombatComponent chip damage that empties hp while guarding kills",
          "[component][combat][guard][!shouldfail][bug:N3]") {
  CombatComponent c("fighter", 50.0f, 100.0f, 50.0f);
  int deaths = 0;
  c.onDeath = [&] { ++deaths; };
  REQUIRE(c.tryGuard());
  c.applyHit(hit(1000.0f));
  CHECK(c.getState() == CombatState::Dead);
  CHECK(c.getHp() == 0.0f);
  CHECK(deaths == 1);
}

TEST_CASE("CombatComponent tryAttack spends stamina, enters Attacking and writes attackR1",
          "[component][combat][attack]") {
  AnimParamTable params;
  CombatComponent c("fighter", 100.0f, 100.0f, 50.0f);
  c.setParamTable(&params);

  REQUIRE(c.tryAttack(attack("r1", 25.0f)));
  CHECK(c.getState() == CombatState::Attacking);
  CHECK(c.getStamina() == 75.0f);
  CHECK(params.hasTrigger("attackR1"));
  CHECK(c.getCurrentAttack().name == "r1");
  CHECK(c.getAttackPhase() == CombatComponent::AttackPhase::Startup);
}

TEST_CASE("CombatComponent refuses attacks without stamina or outside Idle",
          "[component][combat][attack]") {
  SECTION("not enough stamina") {
    CombatComponent c("fighter", 100.0f, 5.0f, 50.0f);
    CHECK_FALSE(c.tryAttack(attack("r1", 10.0f)));
    CHECK(c.getState() == CombatState::Idle);
    CHECK(c.getStamina() == 5.0f);
  }
  SECTION("already attacking") {
    CombatComponent c("fighter", 100.0f, 100.0f, 50.0f);
    REQUIRE(c.tryAttack(attack("r1")));
    CHECK_FALSE(c.tryAttack(attack("r1")));
    CHECK(c.getStamina() == 90.0f);
  }
  SECTION("dead") {
    CombatComponent c("fighter", 100.0f, 100.0f, 50.0f);
    c.applyHit(hit(500.0f));
    CHECK_FALSE(c.tryAttack(attack("r1")));
  }
}

// Looks for uppercase "R2" but weapon and bare-hand attacks are named "r2"
TEST_CASE("CombatComponent heavy attacks from attack data write the attackR2 trigger",
          "[component][combat][attack][!shouldfail][bug:N7]") {
  AnimParamTable params;
  CombatComponent c("fighter", 100.0f, 100.0f, 50.0f);
  c.setParamTable(&params);
  REQUIRE(c.tryAttack(c.getAttackData("r2")));
  CHECK(params.hasTrigger("attackR2"));
}

TEST_CASE("CombatComponent takes attack data from the weapon, else bare hands",
          "[component][combat][attack]") {
  WeaponDef sword;
  sword.id = "test_sword";
  sword.attacks["r1"] = AttackData{ "r1", 120.0f, 30.0f, 25.0f, "slash" };

  CombatComponent c("fighter", 100.0f, 100.0f, 50.0f);
  c.setWeapon(&sword);
  CHECK(c.getAttackData("r1").damage == 120.0f);
  CHECK(c.getAttackData("r2").damage == 40.0f);
  CHECK(LogCapture::contains(spdlog::level::warn, "no attack 'r2'"));

  c.setWeapon(nullptr);
  CHECK(c.getAttackData("r1").damage == 25.0f);
}

TEST_CASE("CombatComponent hit token allows one resolution per swing",
          "[component][combat]") {
  CombatComponent c("fighter", 100.0f, 100.0f, 50.0f);
  CHECK(c.consumeHitToken());
  CHECK_FALSE(c.consumeHitToken());
}

TEST_CASE("CombatComponent revive restores stats and returns to Idle",
          "[component][combat]") {
  AnimParamTable params;
  CombatComponent c("fighter", 100.0f, 100.0f, 50.0f);
  c.setParamTable(&params);
  c.applyHit(hit(500.0f, 80.0f));
  REQUIRE(c.getState() == CombatState::Dead);

  c.revive();
  CHECK(c.getState() == CombatState::Idle);
  CHECK(c.getHp() == 100.0f);
  CHECK(c.getPoise() == 50.0f);
  CHECK_FALSE(params.getBool("isDead", true));
}

TEST_CASE("CombatComponent hitbox state and capsules follow the SpawnHitbox window",
          "[component][combat][hitbox]") {
  CapsuleSegment guard, blade;
  blade.localP0 = { 0.0f, 1.0f, 0.0f };
  blade.localP1 = { 0.0f, 1.5f, 0.0f };
  AnimatedFighter f(ClipBuilder().duration(1.0f).hitbox(0.25f, 0.5f, { guard, blade }).build());

  const bool expected[] = { false, true, true, false };
  for (int i = 0; i < 4; ++i) {
    f.combat.setWorldCapsules({ WorldCapsule{} });
    f.step();
    INFO("frame " << i);
    CHECK(f.combat.hasActiveHitbox() == expected[i]);
    CHECK(f.combat.getLocalCapsules().size() == (expected[i] ? 2u : 0u));
    if (!expected[i]) CHECK(f.combat.getWorldCapsules().empty());
  }
}

TEST_CASE("CombatComponent re-arms the hit token each time a hitbox window opens",
          "[component][combat][hitbox]") {
  AnimatedFighter f(ClipBuilder().duration(1.0f).hitbox(0.25f, 0.5f).build());
  f.step();
  f.step();
  REQUIRE(f.combat.hasActiveHitbox());
  CHECK(f.combat.consumeHitToken());
  f.step();
  CHECK_FALSE(f.combat.consumeHitToken());

  for (int k = 4; k <= 9; ++k) f.step();
  CHECK_FALSE(f.combat.hasActiveHitbox());
  f.step();
  REQUIRE(f.combat.hasActiveHitbox());
  CHECK(f.combat.consumeHitToken());
}

TEST_CASE("CombatComponent parry and deflect windows set the receptive state",
          "[component][combat][receptive]") {
  AnimatedFighter f(ClipBuilder()
                      .duration(1.0f)
                      .window(AnimEventType::ParryWindow, 0.0f, 0.5f)
                      .window(AnimEventType::DeflectWindow, 0.5f, 1.0f)
                      .build());
  f.step();
  CHECK(f.combat.isInReceptiveWindow());
  CHECK(f.combat.getReceptiveWindowType() == ReceptiveHitWindowType::Parry);
  for (int i = 0; i < 4; ++i) f.step();
  CHECK(f.combat.getReceptiveWindowType() == ReceptiveHitWindowType::Deflect);
}

// Recovering drops straight to Idle unless the root state is named "Attacking",
// so a preview clip (no state machine) can't exercise this path
TEST_CASE("CombatComponent RecoveryBegin moves an attacking combatant into Recovering",
          "[component][combat][attack]") {
  GraphBuilder b;
  const auto clip = b.clip("r1", false);
  const auto sm = b.stateMachine("Attacking", { { "Attacking", clip } }, {});
  auto def = b.root(sm).build();
  GraphInstance inst = GraphInstance::Create(def);
  nodeAt<ClipNode>(inst, clip).setClip(
    ClipBuilder("r1").duration(1.0f).oneShot(AnimEventType::RecoveryBegin, 0.5f).build());

  Animator animator;
  animator.setSkeleton(makeSkeleton({ "root" }));
  animator.setGraph(std::move(inst));

  CombatComponent c("player", 100.0f, 100.0f, 50.0f);
  c.setAnimator(&animator);
  c.setParamTable(&animator.getParams());
  REQUIRE(c.tryAttack(attack("r1")));

  for (int f = 0; f < 4; ++f) {
    animator.update(0.125f);
    c.update(0.125f);
    CHECK(c.getState() == CombatState::Attacking);
  }
  animator.update(0.125f);
  c.update(0.125f);
  CHECK(c.getState() == CombatState::Recovering);
  CHECK(c.isVulnerable());
  CHECK(c.getAttackPhase() == CombatComponent::AttackPhase::Recovery);
}

TEST_CASE("CombatComponent hits landing inside an IFrame window deal no damage",
          "[component][combat][iframe][!shouldfail][bug:G2]") {
  AnimatedFighter f(ClipBuilder("roll").duration(1.0f).window(AnimEventType::IFrame, 0.0f, 0.6f).build());
  f.step();
  REQUIRE(countEvents(f.animator.sampledEvents(), AnimEventType::IFrame) == 1);
  f.combat.applyHit(hit(40.0f, 10.0f));
  CHECK(f.combat.getHp() == 100.0f);
}
