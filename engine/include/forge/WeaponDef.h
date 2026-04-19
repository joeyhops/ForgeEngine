#pragma once
#include <forge/AttackData.h>
#include <string>
#include <unordered_map>

namespace forge {

// WeaponDef is a pure data struct
// Instances are owned by asset manager (loaded from weapons.json) and
// reffed by a non-owning pointer elsewhere
//
// AttachBone and hitboxBone must exactly match a bone name present
// in the character skeleton. CombatSystem reads this string to look
// up the bone index in m_globalTransform when positioning the hitbox
struct WeaponDef {
  std::string id;
  std::string meshPath;
  std::string attachSocket; // bone name in chars skeleton
  std::string hitSocket;
  std::unordered_map<std::string, AttackData> attacks;
};

}
