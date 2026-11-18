#include <forge/EventBus.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace forge;

namespace {
struct PingEvent { int value; };
struct PongEvent { std::string text; };
}

TEST_CASE("EventBus delivers events to subscribers in subscription order",
          "[unit][events]") {
  std::vector<int> calls;
  EventBus::subscribe<PingEvent>([&](const PingEvent& e) { calls.push_back(e.value); });
  EventBus::subscribe<PingEvent>([&](const PingEvent& e) { calls.push_back(e.value * 10); });

  EventBus::publish(PingEvent{ 3 });
  CHECK(calls == std::vector<int>{ 3, 30 });
}

TEST_CASE("EventBus keeps event types separate",
          "[unit][events]") {
  int pings = 0, pongs = 0;
  EventBus::subscribe<PingEvent>([&](const PingEvent&) { ++pings; });
  EventBus::subscribe<PongEvent>([&](const PongEvent&) { ++pongs; });

  EventBus::publish(PongEvent{ "hi" });
  CHECK(pings == 0);
  CHECK(pongs == 1);
}

TEST_CASE("EventBus publishing with no subscribers is a no-op",
          "[unit][events]") {
  EventBus::publish(PingEvent{ 1 });
  SUCCEED();
}

TEST_CASE("EventBus clear removes every subscription",
          "[unit][events]") {
  int calls = 0;
  EventBus::subscribe<PingEvent>([&](const PingEvent&) { ++calls; });
  EventBus::clear();
  EventBus::publish(PingEvent{ 1 });
  CHECK(calls == 0);
}

