#pragma once
#include <string>

namespace forge {

struct AttackData {
  std::string name = "unnamed";

  // Dmg Vals
  float damage = 100.0f; // Raw dmg before def
  float poiseDamage = 25.0f; // How much poise this breaks
  float staminaCost = 20.0f; // Stamina consumed in swing
  
  // Timing (in seconds, multiply by 60 to think in frames)
  float startupTime = 0.15f; // Before hitbox active (windup)
  float activeTime = 0.10f; // Hitbox is live
  float recoveryTime = 0.30f; // After hitbox closes (vulnerable)

  // Hit detection geometry
  float range = 1.8f; // Attack reach
  float angle = 90.0f; // Cone anlge (degrees) in front of attacker
  
  // damage kind
  std::string damageType = "slash";

  float totalTime() const { return startupTime + activeTime + recoveryTime; }
};

}
