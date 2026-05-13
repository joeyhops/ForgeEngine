#include <forge/LuaState.h>
#include <forge/Logger.h>
#include <forge/Transform.h>
#include <forge/CombatComponent.h>
#include <forge/AttackData.h>
#include <forge/AIComponent.h>
#include <forge/AnimGraph.h>
#include <forge/FlagManager.h>
#include <forge/EventBus.h>
#include <forge/Events.h>
#include <forge/EquipmentComponent.h>
#include <forge/WeaponDef.h>

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
  registerCombat();
  registerEvents();
  registerAI();
  registerAnimGraph();
  registerEquipment();

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

void LuaState::registerCombat() {
  // AttackData - constructible from Lua as a table
  m_lua.new_usertype<AttackData>("AttackData",
                                 sol::constructors<AttackData()>(),
                                 "name", &AttackData::name,
                                 "damage", &AttackData::damage,
                                 "poiseDamage", &AttackData::poiseDamage,
                                 "staminaCost", &AttackData::staminaCost,
                                 "startupTime", &AttackData::startupTime,
                                 "activeTime", &AttackData::activeTime,
                                 "recoveryTime", &AttackData::recoveryTime,
                                 "range", &AttackData::range,
                                 "angle", &AttackData::angle,
                                 "damageType", &AttackData::damageType
                                 );

  // CombatComponent - Created by C++, accessed by Lua
  m_lua.new_usertype<CombatComponent>("CombatComponent",
                                      sol::no_constructor,
                                      "getHp",  &CombatComponent::getHp,
                                      "getMaxHp", &CombatComponent::getMaxHp,
                                      "getStamina", &CombatComponent::getStamina,
                                      "getPoise", &CombatComponent::getPoise,
                                      "getStateName", &CombatComponent::getStateName,
                                      "isAlive", &CombatComponent::isAlive,
                                      "isVulnerable", &CombatComponent::isVulnerable,
                                      "isAttacking", &CombatComponent::isAttacking,
                                      "isGuarding", &CombatComponent::isGuarding,
                                      "tryAttack", &CombatComponent::tryAttack,
                                      "tryGuard", &CombatComponent::tryGuard,
                                      "releaseGuard", &CombatComponent::releaseGuard,
                                      "takeDamage", &CombatComponent::takeDamage,
                                      "getAttackData", &CombatComponent::getAttackData,
                                      "isInComboWindow", &CombatComponent::isInComboWindow,
                                      "getComboWindowAction", &CombatComponent::getComboWindowAction,
                                      "bufferCombo", &CombatComponent::bufferCombo,
                                      "isInReceptiveWindow", &CombatComponent::isInReceptiveWindow
                                      );
}

void LuaState::registerEvents() {
  // FlagManager - Exposed as global Flags table in Lua
  m_lua.new_usertype<FlagManager>("FlagManager",
                                  sol::no_constructor,
                                  "set", &FlagManager::set,
                                  "get", &FlagManager::get,
                                  "toggle", &FlagManager::toggle
                                  );

  // Event bus publishing from Lua
  // Lua usage: Events.publish("bonefireLit", "highwall")
  auto eventsTable = m_lua.create_named_table("Events");
  eventsTable.set_function("publish",
                            [](const std::string& name, const std::string& data) {
                              EventBus::publish(ScriptEvent{ .name = name, .data = data });
                            }
                           );
}

void LuaState::registerAI() {
  m_lua.new_usertype<AIComponent>("AIComponent",
                                  sol::no_constructor,
                                  "getStateName", &AIComponent::getStateName,
                                  "getMoveSpeed", &AIComponent::getMoveSpeed,
                                  "getChaseSpeed", &AIComponent::getChaseSpeed,
                                  "canSeePlayer", &AIComponent::canSeePlayer,
                                  "inAttackRange", &AIComponent::inAttackRange,
                                  "getDistToPlayer", &AIComponent::getDistToPlayer,
                                  "setDetectionRadius", &AIComponent::setDetectionRadius,
                                  "setAttackRadius", &AIComponent::setAttackRadius,
                                  "setMoveSpeed", &AIComponent::setMoveSpeed,
                                  "setChaseSpeed", &AIComponent::setChaseSpeed,
                                  "addWaypoint", &AIComponent::addWaypoint
                                  );
}

void LuaState::registerAnimGraph() {
  m_lua.new_usertype<AnimParamTable>("AnimParamTable",
                                            sol::no_constructor,
                                            "setFloat", &forge::AnimParamTable::setFloat,
                                            "setBool", &forge::AnimParamTable::setBool,
                                            "setTrigger", &forge::AnimParamTable::setTrigger,
                                            "setInt", &forge::AnimParamTable::setInt,
                                            "getFloat", &forge::AnimParamTable::getFloat,
                                            "getBool", &forge::AnimParamTable::getBool,
                                            "getInt", &forge::AnimParamTable::getInt
                                            );
}

void LuaState::registerEquipment() {
  m_lua.new_usertype<EquipmentComponent>("EquipmentComponent", sol::no_constructor,
                                         "equip", [](EquipmentComponent& ec, int slot, const std::string& id)-> bool {
                                            return ec.equip(static_cast<EquipmentComponent::Slot>(slot), id);
                                         },

                                         "unequip", [](EquipmentComponent& ec, int slot) {
                                            ec.unequip(static_cast<EquipmentComponent::Slot>(slot));
                                         },

                                         "hasWeapon", [](EquipmentComponent& ec, int slot) -> bool {
                                            return ec.hasWeapon(static_cast<EquipmentComponent::Slot>(slot));
                                         },

                                         "getEquipped", [](EquipmentComponent& ec, int slot) -> std::string {
                                            const WeaponDef* def = ec.getEquipped(
                                                                        static_cast<EquipmentComponent::Slot>(slot));
                                            return def ? def->id : "";
                                         });
  auto slotTable = m_lua.create_named_table("EquipSlot");
  slotTable["RIGHT"] = static_cast<int>(EquipmentComponent::RIGHT_HAND);
  slotTable["LEFT"] = static_cast<int>(EquipmentComponent::LEFT_HAND);
}

}
