#include "support/ContentLint.h"
#include "support/TestPaths.h"

#include <forge/LuaState.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <set>
#include <string>

using namespace forge;
using namespace forge::test;

TEST_CASE("Content: every Lua script compiles",
          "[content][lua]") {
  const auto scripts = findAssetFiles("scripts", ".lua", true);
  REQUIRE_FALSE(scripts.empty());

  LuaState lua;
  for (const auto& rel : scripts) {
    // load_file compiles without running, so top-level references to engine
    // globals (FLAGS, Flags, playerCombat) can't produce false failures
    sol::load_result chunk = lua.get().load_file(assetFile(rel));
    INFO(rel);
    if (!chunk.valid()) {
      sol::error err = chunk;
      FAIL_CHECK(rel << ": " << err.what());
    }
  }
}

TEST_CASE("Content: combat_rules.lua returns resolutions the engine understands",
          "[content][lua][combat][contract]") {
  // deflect_success only actually works once C1 is fixed
  const std::set<std::string> known = { "hit", "guard", "guard_broken", "parry_success", "deflect_success" };

  struct Ctx { const char* defenderState; bool receptive; const char* receptiveType; bool perilous; };
  const Ctx c = GENERATE(values<Ctx>({
    { "Idle",      false, "None",    false },
    { "Guarding",  false, "None",    false },
    { "Attacking", true,  "Parry",   false },
    { "Idle",      true,  "Deflect", true  },
  }));
  CAPTURE(c.defenderState, c.receptiveType, c.perilous);

  LuaState lua;
  REQUIRE(lua.loadScript(assetFile("scripts/combat/combat_rules.lua")));
  sol::protected_function fn = lua.get()["onHitResolution"];
  REQUIRE(fn.valid());

  sol::table ctx = lua.get().create_table();
  ctx["damage"] = 120.0f;
  ctx["poiseDamage"] = 30.0f;
  ctx["damageType"] = "slash";
  ctx["defenderState"] = c.defenderState;
  ctx["inReceptiveWindow"] = c.receptive;
  ctx["receptiveType"] = c.receptiveType;
  ctx["facingAngle"] = 10.0f;
  ctx["perilous"] = c.perilous;

  sol::protected_function_result r = fn(ctx);
  REQUIRE(r.valid());
  sol::table res = r;
  const std::string type = res.get_or("type", std::string("<missing>"));
  CHECK(known.count(type) == 1);
  CHECK(res.get_or("damage", 0.0f) >= 0.0f);
}
