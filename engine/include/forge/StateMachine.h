#pragma once
#include <functional>
#include <unordered_map>
#include <forge/Logger.h>

namespace forge {

// Callbacks fired on state transitions
struct StateCallbacks {
  std::function<void()> onEnter; // Fired once when entering this state
  std::function<void(float)> onUpdate; // Fired ever frame while in this state
  std::function<void()> onExit; // Fired once when leaving this state
};

// T must be an enum class - e.g. StateMachine<CombatState>
template<typename T>
class StateMachine {
public:
  StateMachine() = default;

  // Register a state and its callbacks
  // Any callback can be nullptr - we check before calling
  void addState(T state, StateCallbacks callbacks) {
    m_states[state] = std::move(callbacks);
  }

  // Set starting state without firing onEnter
  void setInitialState(T state) {
    m_current = state;
  }

  // Transition to a new stae - fires onExit and onEnter
  void transition(T next) {
    if (next == m_current) return; // Already in this state

    // Exit current state
    auto& curr = m_states[m_current];
    if (curr.onExit) curr.onExit();

    m_previous = m_current;
    m_current = next;

    // Enter the new state
    auto& nextState = m_states[m_current];
    if (nextState.onEnter) nextState.onEnter();
  }

  // Call every frame - fires the current states onUpdate
  void update(float dt) {
    auto it = m_states.find(m_current);
    if (it != m_states.end() && it->second.onUpdate)
      it->second.onUpdate(dt);
  }

  T current() const { return m_current; }
  T previous() const { return m_previous; }
  bool isIn(T state) const { return m_current == state; }

private:
  T m_current = {};
  T m_previous = {};
  std::unordered_map<T, StateCallbacks> m_states;
};

}
