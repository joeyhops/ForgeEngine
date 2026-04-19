#pragma once
#include <string>
#include <glm/glm.hpp>
#include <forge/AnimationClip.h>

namespace forge {

// Fired by CombatComponent when an entitys HP reaches zero
struct EntityDiedEvent{
  std::string entityName;
  glm::vec3 position;
};

// Fired by CombatComponent when a hit lands
struct EntityHitEvent {
  std::string attackerName;
  std::string defenderName;
  float damage;
  std::string damageType;
};

// Fired by FlagManager when any flag changes
struct FlagChangeEvent{
  uint32_t flagId;
  bool newValue;
};

// Fired by application at window resize
struct WindowResizedEvent {
  int width;
  int height;
};

// Fired from Lua Scripts to trigger engine-side effects
// Usage: EventBus::publish(ScriptEvent{ .name = "bonfireLit", .data = "highwall" })
struct ScriptEvent {
  std::string name;
  std::string data; // Options payload string - parse as needed
};

// TAE Animation events
// Fired by the animators as m_currentTime crosses AnimEvent window boundaries.
// ownerName matches the name passed to Animator::setOwnerName so
// subscribers with multiple entries can filter to only their own events
struct AnimEventActivated {
  std::string ownerName;
  AnimEventType type;
  std::string payload;
};

struct AnimEventDeactivated {
  std::string ownerName;
  AnimEventType type;
  std::string payload;
};

struct RestEvent {
  int bonfireId;
};

}
