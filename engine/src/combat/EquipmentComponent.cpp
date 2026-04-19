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
      const std::string& socketName = m_equipped[s]->attachSocket;
      auto it = model.sockets.find(socketName)    ;

      if (it != model.sockets.end()) {
        b.boneIndex = it->second.boneIndex;
        b.localOffset = it->second.localOffset;
      } else if (!b.logged) {
        // Build a compact summary of available sockets so the error log
        // is directly actionable — tells you which sockets DO exist.
        std::string available;
        available.reserve(model.sockets.size() * 12);
        bool first = true;
        for (const auto& kv : model.sockets) {
          if (!first) available += ", ";
          available += kv.first;
          first = false;
        }
        if (available.empty()) available = "<none>";

        LOG_ERROR("[Equipment] '{}' socket '{}' not found on skeleton "
                  "(available sockets: {})",
                  m_ownerName, socketName, available);
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

      m_weaponTransforms[s] = boneAttach * b.localOffset;
    }
    // visible failure
  }
}

}
