#include <forge/LuaState.h>
#include <forge/Logger.h>
#include <forge/Transform.h>

#include <glm/glm.hpp>
#include <fstream>

namespace forge {

LuaState::LuaState() {
  // Open std lua libs - base (print, type, etc.)
  // math (sin, cos, etc.), string, table
  // We deliberately exlude io and os for sandboxing later
  m_lua.open_libraries(
    sol::lib::base,
    sol::lib::math,
    sol::lib::string,
    sol::lib::table
  );

  registerBindings();
  LOG_INFO("[Lua] VM Initialized");
}

bool LuaState::loadScript(const std::string& path) {
  auto result = m_lua.safe_script_file(path, sol::script_pass_on_error);
  if (!result.valid()) {
    sol::error err = result;
    LOG_ERROR("[Lua] Failed to load '{}': {}", path, err.what());
    return false;
  }
  LOG_INFO("[Lua] Loaded script: {}", path);
  return true;
}

// Bindings
void LuaState::registerBindings() {
  registerMathTypes();
  registerTransform();

  // Expose the logger to Lua so scripts can log properly
  // usage in lua: Log.info("hello from script")
  auto logTable = m_lua.create_named_table("Log");
  logTable.set_function("info", [](const std::string& msg){ LOG_INFO("[Lua] {}", msg); });
  logTable.set_function("warn", [](const std::string& msg){ LOG_WARN("[Lua] {}", msg); });
  logTable.set_function("error", [](const std::string& msg){ LOG_ERROR("[Lua] {}", msg); });

  LOG_INFO("[Lua] Bindings registered");
}

void LuaState::registerMathTypes() {
  // expose glm::vec3 to lua
  // Lua usage: local v = Vec3.new(1.0, 2.0, 3.0)
  m_lua.new_usertype<glm::vec3>("Vec3",
                                sol::constructors<glm::vec3(), glm::vec3(float, float, float)>(),
                                "x", &glm::vec3::x,
                                "y", &glm::vec3::y,
                                "z", &glm::vec3::z,

                                // Operator overloads so v1 + v2 works in lua
                                sol::meta_function::addition,
                                  [](const glm::vec3& a, const glm::vec3& b){ return a + b; },
                                sol::meta_function::subtraction,
                                  [](const glm::vec3& a, const glm::vec3& b){ return a - b; },
                                sol::meta_function::multiplication,
                                  [](const glm::vec3& v, float s){ return v * s; },

                                // __tostring to print(v) works in Lua
                                sol::meta_function::to_string,
                                  [](const glm::vec3& v){
                                    return "Vec3(" + std::to_string(v.x) + ", "
                                                   + std::to_string(v.y) + ", "
                                                   + std::to_string(v.z) + ")";
                                  }
                                );
  
}

void LuaState::registerTransform() {
  // Expose Transform to Lua
  // Lua usage: transform:setPosition(Vec3.new(0, 1, 0))
  m_lua.new_usertype<Transform>("Transform",
                                // No constructor - Transforms are created by C++ not Lua
                                sol::no_constructor,
                                "setPosition", &Transform::setPosition,
                                "setScale", &Transform::setScale,
                                "setEulerAngles", &Transform::setEulerAngles,
                                "getPosition", &Transform::getPosition
                                );  
}

}
