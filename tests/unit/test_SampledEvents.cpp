#include <forge/AnimEventReader.h>
#include <forge/SampledEventsBuffer.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace forge;

namespace {

SampledEvent makeEvent(AnimEventType type, float weight = 1.0f, std::string payload = {},
                       SampledEvent::Flags flags = SampledEvent::Flags::FromActiveBranch) {
  SampledEvent e;
  e.type = type;
  e.weight = weight;
  e.payload = std::move(payload);
  e.flags = flags;
  return e;
}

}

TEST_CASE("SampledEventsBuffer Append returns consecutive indices",
          "[unit][anim][events]") {
  SampledEventsBuffer buff;
  CHECK(buff.Append(makeEvent(AnimEventType::SpawnHitbox)) == 0);
  CHECK(buff.Append(makeEvent(AnimEventType::IFrame)) == 1);
  CHECK(buff.CurrentSize() == 2);
  buff.Clear();
  CHECK(buff.CurrentSize() == 0);
}

TEST_CASE("SampledEventsBuffer ScaleWeights scales only [begin,end) and clamps end",
          "[unit][anim][events]") {
  SampledEventsBuffer buff;
  for (int i = 0; i < 3; ++i) buff.Append(makeEvent(AnimEventType::SFXSpawn));

  buff.ScaleWeights(1, 2, 0.5f);
  CHECK(buff.Events()[0].weight == 1.0f);
  CHECK(buff.Events()[1].weight == 0.5f);
  CHECK(buff.Events()[2].weight == 1.0f);

  buff.ScaleWeights(2, 999, 0.25f); // end past size is clamped, not unbounded
  CHECK(buff.Events()[2].weight == 0.25f);

  // Nested blends multiply as recursion unwinds
  buff.ScaleWeights(1, 2, 0.5f);
  CHECK(buff.Events()[1].weight == 0.25f);
}

TEST_CASE("SampledEventsBuffer MarkTransitionSource clears only the active-branch flag",
          "[unit][anim][events]") {
  SampledEventsBuffer buff;
  buff.Append(makeEvent(AnimEventType::SpawnHitbox,
                        1.0f,
                        {},
                        SampledEvent::Flags::FromActiveBranch | SampledEvent::Flags::SampledInReverse));
  buff.Append(makeEvent(AnimEventType::SpawnHitbox));

  buff.MarkTransitionSource(0, 1);

  CHECK_FALSE(HasFlag(buff.Events()[0].flags, SampledEvent::Flags::FromActiveBranch));
  CHECK(HasFlag(buff.Events()[0].flags, SampledEvent::Flags::SampledInReverse)); // untouched
  CHECK(HasFlag(buff.Events()[1].flags, SampledEvent::Flags::FromActiveBranch)); // outside range
}

TEST_CASE("SampledEventsBuffer FilteredRange visits only matching events, in order",
          "[unit][anim][events]") {
  SampledEventsBuffer buff;
  buff.Append(makeEvent(AnimEventType::SpawnHitbox, 1.0f, "a"));
  buff.Append(makeEvent(AnimEventType::IFrame));
  buff.Append(makeEvent(AnimEventType::SpawnHitbox, 1.0f, "b"));

  std::vector<std::string> seen;
  for (const auto& e : buff.FilteredRange(
    [](const SampledEvent& e) { return e.type == AnimEventType::SpawnHitbox; }))
    seen.push_back(e.payload);
  CHECK(seen == std::vector<std::string>{ "a", "b" });

  SampledEventsBuffer empty;
  int n = 0;
  for (const auto& e : empty.FilteredRange([](const SampledEvent&) { return true; })) {
    (void)e;
    ++n;
  }
  CHECK(n == 0);
}

TEST_CASE("AnimEventReader weight threshold is inclusive at 0.5",
          "[unit][anim][events]") {
  SampledEventsBuffer buff;
  AnimEventReader reader;

  buff.Append(makeEvent(AnimEventType::ParryWindow, 0.5f));
  reader.refresh(buff);
  CHECK(reader.activeType(AnimEventType::ParryWindow));

  buff.Clear();
  buff.Append(makeEvent(AnimEventType::ParryWindow, 0.49f));
  reader.refresh(buff);
  CHECK_FALSE(reader.activeType(AnimEventType::ParryWindow));

  reader.refresh(buff, 0.1f); // a consumer may lower the threshold
  CHECK(reader.activeType(AnimEventType::ParryWindow));
}

TEST_CASE("AnimEventReader ignores events fading out of a transition",
          "[unit][anim][events]") {
  SampledEventsBuffer buff;
  AnimEventReader reader;
  buff.Append(makeEvent(AnimEventType::SpawnHitbox, 1.0f, {}, SampledEvent::Flags::None));
  reader.refresh(buff);
  CHECK_FALSE(reader.activeType(AnimEventType::SpawnHitbox));
}

TEST_CASE("AnimEventReader reports opened and closed as edges across refreshes",
          "[unit][anim][events]") {
  SampledEventsBuffer frame;
  AnimEventReader reader;

  frame.Append(makeEvent(AnimEventType::ParryWindow));
  reader.refresh(frame);
  CHECK(reader.openedType(AnimEventType::ParryWindow));
  CHECK(reader.activeType(AnimEventType::ParryWindow));
  CHECK_FALSE(reader.closedType(AnimEventType::ParryWindow));

  reader.refresh(frame); // same contents next frame: still active no new edge
  CHECK_FALSE(reader.openedType(AnimEventType::ParryWindow));
  CHECK(reader.activeType(AnimEventType::ParryWindow));

  frame.Clear();
  reader.refresh(frame);
  CHECK(reader.closedType(AnimEventType::ParryWindow));
  CHECK_FALSE(reader.activeType(AnimEventType::ParryWindow));

  reader.refresh(frame); // a close is a one-frame edge
  CHECK_FALSE(reader.closedType(AnimEventType::ParryWindow));
}

TEST_CASE("AnimEventReader distinguishes payload-specific and payload-agnostic queries",
          "[unit][anim][events]") {
  SampledEventsBuffer frame;
  AnimEventReader reader;

  frame.Append(makeEvent(AnimEventType::ComboWindow, 1.0f, "r1_2"));
  reader.refresh(frame);
  CHECK(reader.active(AnimEventType::ComboWindow, "r1_2"));
  CHECK_FALSE(reader.active(AnimEventType::ComboWindow, "r1_3"));
  CHECK_FALSE(reader.active(AnimEventType::ComboWindow));
  CHECK(reader.activeType(AnimEventType::ComboWindow));

  SECTION("switching payload is an edge per payload but not per type") {
    frame.Clear();
    frame.Append(makeEvent(AnimEventType::ComboWindow, 1.0f, "r1_3"));
    reader.refresh(frame);
    CHECK(reader.opened(AnimEventType::ComboWindow, "r1_3"));
    CHECK(reader.closed(AnimEventType::ComboWindow, "r1_2"));
    CHECK_FALSE(reader.openedType(AnimEventType::ComboWindow));
    CHECK_FALSE(reader.closedType(AnimEventType::ComboWindow));
  }
}

