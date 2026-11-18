#include "support/LogCapture.h"
#include "support/TempDir.h"

#include <forge/EventBus.h>
#include <forge/Events.h>
#include <forge/FlagManager.h>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

using namespace forge;
using namespace forge::test;

TEST_CASE("FlagManager reads unset flags as false and stores only true flags",
          "[unit][flags]") {
  FlagManager f;
  CHECK_FALSE(f.get(10000));
  f.set(10000, false);
  CHECK(f.count() == 0);
  f.set(10000, true);
  CHECK(f.count() == 1);
  f.set(10000, false);
  CHECK(f.count() == 0);
}

TEST_CASE("FlagManager publishes FlagChangeEvent only when a value changes",
          "[unit][flags]") {
  std::vector<FlagChangeEvent> seen;
  EventBus::subscribe<FlagChangeEvent>([&](const FlagChangeEvent& e) { seen.push_back(e); });

  FlagManager f;
  f.set(1, true);
  f.set(1, true);
  REQUIRE(seen.size() == 1);
  CHECK(seen[0].flagId == 1);
  CHECK(seen[0].newValue);

  CHECK(f.toggle(1) == false);
  REQUIRE(seen.size() == 2);
  CHECK_FALSE(seen[1].newValue);
}

TEST_CASE("FlagManager save and load round-trip, replacing existing state",
          "[unit][flags][io]") {
  TempDir dir;
  const std::string path = dir.file("saves/slot0.json");

  {
    FlagManager f;
    f.set(10000, true);
    f.set(20002, true);
    f.saveToFile(path);
  }

  FlagManager g;
  g.set(5, true);
  g.loadFromFile(path);
  CHECK(g.count() == 2);
  CHECK(g.get(10000));
  CHECK(g.get(20002));
  CHECK_FALSE(g.get(5));
}

TEST_CASE("FlagManager loading a missing file warns and keeps current flags",
          "[unit][flags][io][characterization]") {
  TempDir dir;
  FlagManager f;
  f.set(5, true);
  f.loadFromFile(dir.file("nope.json"));
  CHECK(f.get(5));
  CHECK(LogCapture::contains(spdlog::level::warn, "No save file"));
}

TEST_CASE("FlagManager loading a corrupt file throws std::runtime_error",
          "[unit][flags][io][characterization]") {
  TempDir dir;
  const auto path = dir.write("corrupt.json", "{ this is not json");
  FlagManager f;
  CHECK_THROWS_AS(f.loadFromFile(path), std::runtime_error);
}

TEST_CASE("FlagManager tolerates a save file with a non-numeric key",
          "[unit][flags][io][!shouldfail][bug:F3]") {
  TempDir dir;
  const auto path = dir.write("odd.json", R"({ "flags": { "10000": true, "abc": true } })");
  FlagManager f;
  CHECK_NOTHROW(f.loadFromFile(path));
  CHECK(f.get(10000));
}
