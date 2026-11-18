#include "support/AnimTestUtils.h"
#include "support/LogCapture.h"
#include "support/TempDir.h"

#include <forge/TAESerialization.h>

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace forge;
using namespace forge::test;
using Catch::Matchers::WithinAbs;

namespace {

AnimEvent makeEvent(AnimEventType type, float start, float end, std::string payload = {}) {
  AnimEvent e{};
  e.type = type;
  e.startTime = start;
  e.endTime = end;
  e.payload = std::move(payload);
  return e;
}

AnimationClip clipWith(std::vector<AnimEvent> events) {
  AnimationClip c;
  c.name = "test_clip";
  c.duration = 2.0f;
  c.events = std::move(events);
  return c;
}

}

TEST_CASE("TAESerialization round-trips every event type except RecoveryBegin",
          "[component][tae][io]") {
  TempDir dir;
  std::vector<AnimEvent> events;
  for (int i = 0; i < kAnimEventTypeCount; ++i) {
    const auto t = static_cast<AnimEventType>(i);
    // N1 is pinned separately, folding it in here would mask regressions in the other types
    if (t == AnimEventType::RecoveryBegin) continue;
    events.push_back(makeEvent(t, 0.01f * i, 0.01f * i + 0.005f));
  }
  const AnimationClip clip = clipWith(events);
  const auto path = dir.file("all_types.tae.json");

  TAESerialization::save(path, clip);
  const auto loaded = TAESerialization::load(path);

  REQUIRE(loaded.size() == clip.events.size());
  for (size_t i = 0; i < loaded.size(); ++i) {
    INFO(eventTypeName(clip.events[i].type));
    CHECK(loaded[i].type == clip.events[i].type);
    CHECK_THAT(loaded[i].startTime, WithinAbs(clip.events[i].startTime, 1e-6));
    CHECK_THAT(loaded[i].endTime, WithinAbs(clip.events[i].endTime, 1e-6));
  }
  CHECK(LogCapture::count(spdlog::level::warn, "event type") == 0);
}

// RecoveryBegin is missing from both string tables: saves as "SpawnHitbox" via
// the switch fall-through, and "RecoveryBegin" loads as SpawnHitbox
TEST_CASE("TAESerialization round-trips RecoveryBegin",
          "[component][tae][io][!shouldfail][bug:N1]") {
  TempDir dir;
  const auto path = dir.file("recovery.tae.json");
  TAESerialization::save(path, clipWith({ makeEvent(AnimEventType::RecoveryBegin, 0.6f, 0.6f) }));
  const auto loaded = TAESerialization::load(path);
  REQUIRE(loaded.size() == 1);
  CHECK(loaded[0].type == AnimEventType::RecoveryBegin);
}

TEST_CASE("TAESerialization round-trips hitbox capsules, including point capsules",
          "[component][tae][io]") {
  CapsuleSegment blade;
  blade.localP0 = { 0.0f, 0.245f, 0.03f };
  blade.localP1 = { 0.0f, 1.4f, -0.025f };
  blade.radius = 0.122f;
  CapsuleSegment tip;
  tip.localP0 = tip.localP1 = { 0.0f, 1.5f, 0.0f };
  tip.radius = 0.1f;

  auto ev = makeEvent(AnimEventType::SpawnHitbox, 0.35f, 0.68f, "weapon_h_r");
  ev.capsules = { blade, tip };

  TempDir dir;
  const auto path = dir.file("r1.tae.json");
  TAESerialization::save(path, clipWith({ ev }));
  const auto loaded = TAESerialization::load(path);

  REQUIRE(loaded.size() == 1);
  CHECK(loaded[0].payload == "weapon_h_r");
  REQUIRE(loaded[0].capsules.size() == 2);
  CHECK_THAT(loaded[0].capsules[0].localP1.y, WithinAbs(1.4, 1e-6));
  CHECK_THAT(loaded[0].capsules[0].radius, WithinAbs(0.122, 1e-6));
  CHECK(loaded[0].capsules[1].localP0 == loaded[0].capsules[1].localP1);
}

TEST_CASE("TAESerialization writes payloads only when non-empty",
          "[component][tae][io]") {
  TempDir dir;
  TAESerialization::save(dir.file("p.tae.json"),
                         clipWith({ makeEvent(AnimEventType::IFrame, 0.1f, 0.3f),
                                    makeEvent(AnimEventType::ComboWindow, 0.6f, 0.7f, "r1_2") }));
  const auto j = nlohmann::json::parse(dir.read("p.tae.json"));
  REQUIRE(j["events"].size() == 2);
  CHECK_FALSE(j["events"][0].contains("payload"));
  CHECK(j["events"][1]["payload"] == "r1_2");
}

TEST_CASE("TAESerialization round-trips root-motion settings",
          "[component][tae][io]") {
  AnimationClip clip = clipWith({});
  clip.rootBoneName = "pelvis";
  clip.extractRootY = true;

  TempDir dir;
  const auto path = dir.file("rm.tae.json");
  TAESerialization::save(path, clip);

  std::string rootBone;
  bool extractY = false;
  TAESerialization::load(path, rootBone, extractY);
  CHECK(rootBone == "pelvis");
  CHECK(extractY);
}

// load() never reports an empty rootBone and AssetManager enables root motion
// whenever rootBone is non-empty, so every clip with a sidecar is root-motion enabled
TEST_CASE("TAESerialization reports rootBone 'root' when the sidecar names none",
          "[component][tae][io][characterization]") {
  TempDir dir;
  const auto path = dir.write("bare.tae.json", R"({ "events": [] })");
  std::string rootBone;
  bool extractY = true;
  TAESerialization::load(path, rootBone, extractY);
  CHECK(rootBone == "root");
  CHECK_FALSE(extractY);
}

TEST_CASE("TAESerialization sorts sync markers and computes their ids on load",
          "[component][tae][io][sync]") {
  AnimationClip clip = clipWith({});
  clip.syncTrack.events = { SyncEvent{ 0.5f, "RightFootDown", 0 }, SyncEvent{ 0.0f, "LeftFootDown", 0 } };

  TempDir dir;
  const auto path = dir.file("sync.tae.json");
  TAESerialization::save(path, clip);

  std::string rootBone;
  bool extractY = false;
  SyncTrack track;
  TAESerialization::load(path, rootBone, extractY, track);
  REQUIRE(track.count() == 2);
  CHECK(track.events[0].name == "LeftFootDown");
  CHECK(track.events[1].name == "RightFootDown");
  CHECK(track.events[0].id == HashName("LeftFootDown"));
}

TEST_CASE("TAESerialization drops capsules on non-hitbox events",
          "[component][tae][io][characterization]") {
  auto ev = makeEvent(AnimEventType::IFrame, 0.1f, 0.3f);
  ev.capsules = { CapsuleSegment{} };
  TempDir dir;
  const auto path = dir.file("c.tae.json");
  TAESerialization::save(path, clipWith({ ev }));
  const auto loaded = TAESerialization::load(path);
  REQUIRE(loaded.size() == 1);
  CHECK(loaded[0].capsules.empty());
}

TEST_CASE("TAESerialization maps unknown event types to SpawnHitbox with a warning",
          "[component][tae][io][characterization]") {
  TempDir dir;
  const auto path = dir.write("u.tae.json",
    R"({ "events": [ { "type": "Teleport", "startTime": 0.1, "endTime": 0.2 } ] })");
  const auto loaded = TAESerialization::load(path);
  REQUIRE(loaded.size() == 1);
  CHECK(loaded[0].type == AnimEventType::SpawnHitbox);
  CHECK(LogCapture::contains(spdlog::level::warn, "Teleport"));
}

TEST_CASE("TAESerialization load fails soft on missing and malformed files",
          "[component][tae][io]") {
  TempDir dir;
  SECTION("missing file") {
    CHECK(TAESerialization::load(dir.file("nope.tae.json")).empty());
    CHECK(LogCapture::contains(spdlog::level::warn, "Cannot open"));
  }
  SECTION("malformed JSON") {
    CHECK(TAESerialization::load(dir.write("bad.tae.json", "{ \"events\": [")).empty());
    CHECK(LogCapture::contains(spdlog::level::err, "Parse error"));
  }
  SECTION("no events array") {
    CHECK(TAESerialization::load(dir.write("empty.tae.json", R"({ "clip": "x" })")).empty());
    CHECK(LogCapture::contains(spdlog::level::warn, "no events array"));
  }
}

// save() logs the open failure, then writes into the dead stream and logs "Saved"
TEST_CASE("TAESerialization never reports a failed save as successful",
          "[component][tae][io][!shouldfail][bug:T1]") {
  TempDir dir;
  TAESerialization::save(dir.file("missing_dir/x.tae.json"),
                         clipWith({ makeEvent(AnimEventType::IFrame, 0.1f, 0.2f) }));
  REQUIRE(LogCapture::contains(spdlog::level::err, "Could not write"));
  CHECK_FALSE(LogCapture::contains(spdlog::level::info, "[TAE] Saved"));
}
