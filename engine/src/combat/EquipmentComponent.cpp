#include <forge/EquipmentComponent.h>
#include <forge/AssetManager.h>
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

  m_equipped[slot] = def;
  m_boneIndex[slot] = -1; // Invalidate cached index - resolved on first update()

  if (onEquip) onEquip(slot, def);

  LOG_INFO("[Equipment] '{}' equipped '{}' in slot {}", m_ownerName, weaponId, static_cast<int>(slot));
}

void EquipmentComponent::unequip(Slot slot) {
  if (!m_equipped[slot]) return;
  const std::string id = m_equipped[slot]->id;
  m_equipped[slot] = nullptr;
  m_boneIndex[slot] = -1;
  m_weaponTransforms[slot] = glm::mat4(1.0f);

  if (onUnequip) onUnequip(slot);

  LOG_INFO("[Equipment] '{}' unequipped slot {} (was '{}')", m_ownerName, static_cast<int>(slot), id);
}

const WeaponDef* EquipmentComponent::getEquipped(Slot slot) const {
  return m_equipped[slot];
}

void EquipmentComponent::update(const std::vector<glm::mat4>& globalTransforms,
                                const std::vector<Bone>& skeleton)
  {
  for (int s = 0; s < SLOT_COUNT; ++s) {
    if (!m_equipped[s]) continue;

    if (m_boneIndex[s] < 0) {
      const std::string& boneName = m_equipped[s]->attachBone;
      for (int i = 0; i < static_cast<int>(skeleton.size()); ++i) {
        if (skeleton[i].name == boneName) {
          m_boneIndex[s] = i;
          break;
        }
      }
      if (m_boneIndex[s] < 0) {
        LOG_ERROR("[Equipment] '{}' bone '{}' not found in skeleton", m_ownerName, boneName);
        continue;
      }
    }

    if (m_boneIndex[s] < static_cast<int>(globalTransforms.size())) {
      m_weaponTransforms[s] = globalTransforms[m_boneIndex[s]];
    }
  }
}
}
