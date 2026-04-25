#include <forge/EquipmentComponent.h>
#include <forge/AssetManager.h>
#include <forge/Animator.h>
#include <forge/MovesetDef.h>
#include <forge/Logger.h>

#include <glm/glm.hpp>

namespace forge {

EquipmentComponent::EquipmentComponent(const std::string& ownerName)
  : m_ownerName(ownerName)
{
  for (auto& t : m_weaponTransforms) t = glm::mat4(1.0f);
}

bool EquipmentComponent::equip(Slot slot, const std::string& weaponId) {
  const WeaponDef* def = AssetManager::getWeaponDef(weaponId);
  if (!def) {
    LOG_ERROR("[Equipment] '{}' equip('{}') - weapon not found in loaded defs",
              m_ownerName, weaponId);
    return false;
  }

  // Swap attacks in the AnimGraph before notifying game
  if (m_animator && !def->movesetId.empty()) {
    const MovesetDef* moveset = AssetManager::getMovesetDef(def->movesetId);
    if (moveset)
      m_animator->swapMovesetClips(moveset->clips);
    else
      LOG_WARN("[Equipment] '{}': weapon '{}' references unknown moveset '{}'",
               m_ownerName, weaponId, def->movesetId);
  }

  m_equipped[slot] = def;
  m_binding[slot] = {};

  if (onEquip) onEquip(slot, def);

  LOG_INFO("[Equipment] '{}' equipped '{}' in slot {}", m_ownerName, weaponId, static_cast<int>(slot));
  return true;
}

void EquipmentComponent::unequip(Slot slot) {
  if (!m_equipped[slot]) return;
  const std::string id = m_equipped[slot]->id;
  m_equipped[slot] = nullptr;
  m_binding[slot] = {};
  m_weaponTransforms[slot] = glm::mat4(1.0f);

  if (onUnequip) onUnequip(slot);

  LOG_INFO("[Equipment] '{}' unequipped slot {} (was '{}')", m_ownerName, static_cast<int>(slot), id);
}

const WeaponDef* EquipmentComponent::getEquipped(Slot slot) const {
  return m_equipped[slot];
}

void EquipmentComponent::update(const glm::mat4& ownerModelMatrix, 
                                const std::vector<glm::mat4>& globalTransforms,
                                const SkinnedModelData& model)
  {
  for (int s = 0; s < SLOT_COUNT; ++s) {
    if (!m_equipped[s]) continue;
    SlotBinding& b = m_binding[s];

    if (!b.resolved) {
      const std::string& boneName = m_equipped[s]->boneAttach;
      const auto& skeleton = model.skeleton;
      
      for (int i = 0; i < static_cast<int>(skeleton.size()); ++i) {
        if (skeleton[i].name == boneName) {
          b.boneIndex = i;
          break;
        }
      }

      if (b.boneIndex < 0 && !b.logged) {
        LOG_ERROR("[Equipment] '{}' attach bone '{}' not found in skeleton ({} bones)",
                  m_ownerName, boneName, skeleton.size());
        b.logged = true;
      }
      b.resolved = true;
    }

    if (b.boneIndex >= 0 &&
        b.boneIndex < static_cast<int>(globalTransforms.size())) {
      glm::mat4 boneWorld = ownerModelMatrix * globalTransforms[b.boneIndex];

      const float sx = glm::length(glm::vec3(boneWorld[0]));
      const float sy = glm::length(glm::vec3(boneWorld[1]));
      const float sz = glm::length(glm::vec3(boneWorld[2]));

      glm::mat4 boneAttach = boneWorld;
      if (sx > 1e-6f) boneAttach[0] /= sx;
      if (sy > 1e-6f) boneAttach[1] /= sy;
      if (sz > 1e-6f) boneAttach[2] /= sz;

      m_weaponTransforms[s] = boneAttach * m_equipped[s]->meshOffset;
    }
    // visible failure
  }
}

}
