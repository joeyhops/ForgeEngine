#pragma once

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include <string>

namespace forge {

class LuaState {
public:
  LuaState();
  ~LuaState() = default;

  // Non-copyable - one VM per engine
  LuaState(const LuaState&) = delete;
  LuaState& operator=(const LuaState&) = delete;

  // exec script file. returns false and logs on error
  bool loadScript(const std::string& path);

  // call a global lua function by name with any # of args
  // returns false if the function doesn't exist or throws
  template<typename... Args>
  bool callFunction(const std::string& name, Args&&... args) {
    sol::protected_function fn = m_lua[name];
    if(!fn.valid()) return false;

    auto result = fn(std::forward<Args>(args)...);
    if (!result.valid()) {
      sol::error err = result;
      LOG_ERROR("[Lua] Error in '{}': {}", name, err.what());
      return false;
    }
    return true;
  }

  // Direct access to underlying state for binding registration
  sol::state& get() { return m_lua; }

private:
  void registerBindings(); // Registers all C++ types/functions with Lua
  void registerMathTypes(); // glm::vec3 etc.
  void registerTransform(); // transform class
  void registerCombat();  // Register combat funcs

  sol::state m_lua;
};

}
