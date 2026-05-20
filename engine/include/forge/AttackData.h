#pragma once
#include <string>

namespace forge {

struct AttackData {
  std::string name = "unnamed";

  // Dmg Vals
  float damage = 100.0f; // Raw dmg before def
  float poiseDamage = 25.0f; // How much poise this breaks
  float staminaCost = 20.0f; // Stamina consumed in swing
  
  // damage kind
  std::string damageType = "slash";
};

}
