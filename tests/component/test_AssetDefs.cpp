#include "support/TestPaths.h"

#include <forge/AssetManager.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace forge;
using namespace forge::test;
using Catch::Matchers::WithinAbs;

namespace {
// These tests write into AssetManager's global tables. clear() is safe here
// because the test process never creates GL objects.
struct ClearAssetsOnExit {
  ~ClearAssetsOnExit() { AssetManager::clear(); }
};
}

TEST_CASE("AssetManager loads weapon definitions with attacks and mesh offset",
          "[component][assets][defs]") {
  ClearAssetsOnExit guard;
  AssetManager::loadWeaponDefs(dataFile("defs/weapons_ok.json"));

  const WeaponDef* w = AssetManager::getWeaponDef("test_blade");
  REQUIRE(w != nullptr);
  CHECK(w->boneAttach == "weapon_h_r");
  CHECK(w->movesetId == "test_moveset");
  CHECK_THAT(w->meshOffset[3].y, WithinAbs(0.1, 1e-6));

  REQUIRE(w->attacks.count("r1") == 1);
  CHECK(w->attacks.at("r1").damage == 120.0f);
  CHECK(w->attacks.at("r1").name == "r1");
  REQUIRE(w->attacks.count("r2") == 1);
  CHECK(w->attacks.at("r2").damage == 200.0f);
  CHECK(w->attacks.at("r2").poiseDamage == AttackData{}.poiseDamage);

  CHECK(AssetManager::getWeaponDef("nope") == nullptr);
}

TEST_CASE("AssetManager loads moveset definitions",
          "[component][assets][defs]") {
  ClearAssetsOnExit guard;
  AssetManager::loadMovesetDefs(dataFile("defs/movesets_ok.json"));
  const MovesetDef* m = AssetManager::getMovesetDef("test_moveset");
  REQUIRE(m != nullptr);
  CHECK(m->clips.size() == 2);
  CHECK(m->clips.at("r1") == "anim/test/r1.glb");
}

// parseEntry throws on a missing boneAttach inside the loop, uncaught
TEST_CASE("AssetManager skips a malformed weapon entry instead of throwing",
          "[component][assets][defs][!shouldfail][bug:F4]") {
  ClearAssetsOnExit guard;
  CHECK_NOTHROW(AssetManager::loadWeaponDefs(dataFile("defs/weapons_missing_bone.json")));
  CHECK(AssetManager::getWeaponDef("fine") != nullptr);
}
