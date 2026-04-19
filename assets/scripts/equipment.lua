-- equipment.lua
-- Equipment event callbacks and helpers.
-- Loaded after attacks.lua and before player_combat.lua.

Log.info("Equipment script loaded")

-- Tracks the currently equipped weapon IDs by slot.
-- Updated through callbacks so other scripts can query without polling C++.
local equipped = {
  player = { ["right"] = "", ["left"] = "" },
  enemy  = { ["right"] = "", ["left"] = "" }
}

local function slotName(slot)
  return slot == EquipSlot.RIGHT and "right" or "left"
end

-- ── Player callbacks ──────────────────────────────────────────────────────────

-- Fired when the player equips a weapon.
-- slot: EquipSlot.RIGHT or EquipSlot.LEFT (integer)
-- weaponId: string matching a WeaponDef id from weapons.json
function onPlayerEquip(slot, weaponId)
  equipped.player[slotName(slot)] = weaponId
  Log.info(string.format("[Equipment] Player equipped '%s' in slot %d", weaponId, slot))
  -- TODO Phase 13+: trigger HUD weapon icon change
  -- TODO Phase 13+: play SFX — Events.publish("weaponEquipped", weaponId)
end

-- Fired when the player unequips a weapon.
function onPlayerUnequip(slot)
  local prev = equipped.player[slotName(slot)]
  equipped.player[slotName(slot)] = ""
  Log.info(string.format("[Equipment] Player unequipped slot %d (was '%s')", slot, prev))
end

-- ── Enemy callbacks ────────────────────────────────────────────────────────────

-- Fired when the enemy equips a weapon (typically at startup from map entity).
function onEnemyEquip(slot, weaponId)
  equipped.enemy[slotName(slot)] = weaponId
  Log.info(string.format("[Equipment] Enemy equipped '%s' in slot %d", weaponId, slot))
end

function onEnemyUnequip(slot)
  local prev = equipped.enemy[slotName(slot)]
  equipped.enemy[slotName(slot)] = ""
  Log.info(string.format("[Equipment] Enemy unequipped slot %d (was '%s')", slot, prev))
end

-- ── Queries ────────────────────────────────────────────────────────────────────

-- Returns the player's current right-hand weapon ID, or "" if unarmed.
-- Prefer this over calling playerEquipment:getEquipped() directly — it uses
-- the locally cached value and avoids a C++ call each frame.
function getPlayerWeapon()
  return equipped.player["right"]
end

-- Returns the enemy's current right-hand weapon ID, or "" if unarmed.
function getEnemyWeapon()
  return equipped.enemy["right"]
end

-- Returns true if the player currently has any weapon equipped.
function playerIsArmed()
  return equipped.player["right"] ~= "" or equipped.player["left"] ~= ""
end
