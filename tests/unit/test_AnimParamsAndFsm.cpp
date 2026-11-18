#include <forge/AnimGraph.h>
#include <forge/StateMachine.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace forge;

TEST_CASE("AnimParamTable returns the caller's default for unset params",
          "[unit][anim][params]") {
  AnimParamTable p;
  CHECK(p.getFloat("speed", 2.5f) == 2.5f);
  CHECK(p.getBool("isDead", true));
  CHECK(p.getInt("dodgeDir", 7) == 7);

  p.setFloat("speed", 1.0f);
  p.setBool("isDead", false);
  p.setInt("dodgeDir", 3);
  CHECK(p.getFloat("speed", 2.5f) == 1.0f);
  CHECK_FALSE(p.getBool("isDead", true));
  CHECK(p.getInt("dodgeDir", 7) == 3);
}

TEST_CASE("AnimParamTable triggers are consumed exactly once and hasTrigger only peeks",
          "[unit][anim][params]") {
  AnimParamTable p;
  p.setTrigger("attackR1");
  CHECK(p.hasTrigger("attackR1"));
  CHECK(p.hasTrigger("attackR1")); // peeking does not consume
  CHECK(p.consumeTrigger("attackR1"));
  CHECK_FALSE(p.consumeTrigger("attackR1"));
  CHECK_FALSE(p.hasTrigger("attackR1"));
}

namespace {

enum class S { A, B, C };

struct FsmFixture {
  StateMachine<S> fsm;
  std::vector<std::string> log;

  FsmFixture() {
    fsm.addState(S::A, { [this] { log.push_back("enterA"); },
                         [this](float) { log.push_back("updateA"); },
                         [this] { log.push_back("exitA"); } });
    fsm.addState(S::B, { [this] { log.push_back("enterB"); },
                         [this](float) { log.push_back("updateB"); },
                         [this] { log.push_back("exitB"); } });
    fsm.setInitialState(S::A);
  }
};

}

TEST_CASE("StateMachine setInitialState does not fire onEnter",
          "[unit][fsm]") {
  FsmFixture f;
  CHECK(f.fsm.current() == S::A);
  CHECK(f.log.empty());
}

TEST_CASE("StateMachine transition fires exit then enter and records the previous state",
          "[unit][fsm]") {
  FsmFixture f;
  f.fsm.transition(S::B);
  CHECK(f.log == std::vector<std::string>{ "exitA", "enterB" });
  CHECK(f.fsm.current() == S::B);
  CHECK(f.fsm.previous() == S::A);
  CHECK(f.fsm.isIn(S::B));
}

TEST_CASE("StateMachine transition to the current state is a no-op",
          "[unit][fsm]") {
  FsmFixture f;
  f.fsm.transition(S::A);
  CHECK(f.log.empty());
}

TEST_CASE("StateMachine update runs only the current state's onUpdate",
          "[unit][fsm]") {
  FsmFixture f;
  f.fsm.update(0.016f);
  f.fsm.transition(S::B);
  f.fsm.update(0.016f);
  CHECK(f.log == std::vector<std::string>{ "updateA", "exitA", "enterB", "updateB" });
}

TEST_CASE("StateMachine allows a state to transition from inside its own onUpdate",
          "[unit][fsm]") {
  StateMachine<S> fsm;
  std::vector<std::string> log;
  fsm.addState(S::A, { nullptr, [&](float) { fsm.transition(S::B); }, nullptr });
  fsm.addState(S::B, { [&] { log.push_back("enterB"); }, nullptr, nullptr });
  fsm.setInitialState(S::A);

  fsm.update(0.016f);
  CHECK(fsm.current() == S::B);
  CHECK(log == std::vector<std::string>{ "enterB" });
}

TEST_CASE("StateMachine tolerates states with no registered callbacks",
          "[unit][fsm]") {
  FsmFixture f;
  f.fsm.transition(S::C); 
  f.fsm.update(0.016f);
  CHECK(f.fsm.current() == S::C);
  CHECK(f.log == std::vector<std::string>{ "exitA" });
}
