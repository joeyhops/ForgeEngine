-- demo_quest.lua
-- Demonstrates the flag-based quest system
-- Reacts to dummy enemy dying, and drives simple two stage quest

Log.info("Demo quest script loaded")

-- track whether we've already reacted to this flag this session
local questStage = 0

function onQuestUpdate(dt)
  if questStage == 0 then
    if Flags:get(FLAGS.BOSS_DUMMY_DEAD) then
      questStage = 1
      onEnemyDefeated()
    end
    return
  end

  if questStage == 1 then
    -- TODO
  end
end

-- AFTER
function onEnemyDefeated()
  Log.warn("The enemy has fallen! Seek the bonfire!")

  Events.publish("enemyDefeated", "dummy")

  Flags:set(FLAGS.GATE_HIGHWALL_OPEN, true)
  Log.info("The gate has opened!")

  -- If the enemy was armed, their weapon was dropped as a pickup by C++.
  -- getEnemyWeapon() returns the last known weapon ID before the unequip callback.
  -- Note: onEnemyUnequip clears the cache; if the enemy was NOT explicitly unequipped
  -- on death (which is Phase 12's default), getEnemyWeapon() still returns the weapon ID.
  local w = getEnemyWeapon()
  if w ~= "" then
    Log.info(string.format("The enemy dropped their %s. Go claim it.", w))
  end
end
