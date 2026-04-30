#pragma once
#include <forge/WeaponDef.h>

#include <glm/glm.hpp>

#include <functional>
#include <vector>
#include <string>

namespace forge {

struct SkinnedModelData;

class AssetManager;
class Animator;

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
  const glm::mat4& getWeaponTransform(Slot slot) { return m_weaponTransforms[slot]; }

  void update(const glm::mat4& ownerModelMatrix,
              const std::vector<glm::mat4>& globalTransforms,
              const SkinnedModelData& model);

  void setAnimator(Animator* animator) { m_animator = animator; }

  // Callbacks - wired into main game class to notify combat component
  std::function<void(Slot, const WeaponDef*)> onEquip;
  std::function<void(Slot)> onUnequip;

  void setMeshOffsetOverride(Slot slot, const glm::mat4& offset);
  void clearMeshOffsetOverride(Slot slot);
  bool hasMeshOffsetOverride(Slot slot) const { return m_overrideActive[slot]; }

private:
  struct SlotBinding {
    int boneIndex = -1;
    glm::mat4 localOffset = glm::mat4(1.0f);
    bool resolved = false;
    bool logged = false;
  };

  std::string m_ownerName;
  Animator* m_animator = nullptr;
  const WeaponDef* m_equipped[SLOT_COUNT] = {};
  SlotBinding m_binding[SLOT_COUNT] = {};
  glm::mat4 m_weaponTransforms[SLOT_COUNT] = {};
  glm::mat4 m_meshOffsetOverride[SLOT_COUNT];
  bool m_overrideActive[SLOT_COUNT] = {};
};

}
