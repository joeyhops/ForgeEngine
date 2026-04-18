#pragma once
#include <forge/WeaponDef.h>
#include <forge/Bone.h>

#include <glm/glm.hpp>

#include <functional>
#include <vector>
#include <string>

namespace forge {

class AssetManager;

// Per entity weapon slot manager.
// Tracks which WeaponDef is equipped in each slot, computes the
// weapon bones world transform each frame, and notifies the CombatComponent
// on equip/unequip
//
// EquipmentComponent does not own any WeaponDef. Points to AssetManagers
// loaded weapon table
//
// Rendering: the component computes the world transform; ForgeGame performs the
// actual draw call. Keeps OpenGL state in game layer
class EquipmentComponent {
public:
  enum Slot { RIGHT_HAND = 0, LEFT_HAND = 1, SLOT_COUNT = 2 };

  EquipmentComponent(const std::string& ownerName);

  // Lookup weapon id in AssetManager, store pointer, resolve attach bone
  // index. returns false if weaponId is not found.
  bool equip(Slot slot, const std::string& weaponId);

  void unequip(Slot slot);

  const WeaponDef* getEquipped(Slot slot) const;
  bool hasWeapon(Slot slot) const { return m_equipped[slot] != nullptr; }

  void update(const std::vector<glm::mat4>& globalTransforms,
              const std::vector<Bone>& skeleton);

  // Callbacks - wired into main game class to notify combat component
  std::function<void(Slot, const WeaponDef*)> onEquip;
  std::function<void(Slot)> onUnequip;

private:
  std::string m_ownerName;
  const WeaponDef* m_equipped[SLOT_COUNT] = {};
  int m_boneIndex[SLOT_COUNT] = { -1, -1 };
  glm::mat4 m_weaponTransforms[SLOT_COUNT];
};

}
