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

function onEnemyDefeated()
  Log.warn("The enemy has falled! Seek the bonfire!")

  Events.publish("enemyDefeated", "dummy")

  Flags:set(FLAGS.GATE_HIGHWALL_OPEN, true)
  Log.info("The gate has opened!")
end

function onBonfireReset()
  if questStage ~= 1 then return end

  Log.info("Yes rest at the bonfire, the world resets but your progress remaine.")
  Flags:set(FLAGS.BONFIRE_HIGHWALL_LIT, true)
  questStage = 2

  Log.info(string.format("Quest state: enemy=%s gate=%s bonfire=%s",
      tostring(Flags:get(FLAGS.BOSS_DUMMY_DEAD)),
      tostring(Flags:get(FLAGS.GATE_HIGHWALL_OPEN)),
      tostring(Flags:get(FLAGS.BONFIRE_HIGHWALL_LIT))
  ))
end

