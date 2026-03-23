-- Flags.lua
-- Single source of truth for all Flag IDs
-- Naming convention: CATEGORY_DESCRIPTION
-- Ranges:
--   10000-19999 : World/Environment
--   20000-29999 : Boss Kills
--   30000-39999 : NPC Quest Lines
--   40000-49999 : Player Progression
--   50000-59999 : Items/Keys

FLAGS = {
  -- World
  BONFIRE_HIGHWALL_LIT = 10000,
  GATE_HIGHWALL_OPEN = 10001,
  FOGGATE_VORDT_OPEN = 10002,

  -- Bosses
  BOSS_DUMMY_DEAD = 20000, -- TEST ENEMY
  BOSS_VORDT_DEAD = 20001,
  BOSS_DANCER_DEAD = 20002,

  -- NPC Quests
  QUEST_SIEGWARD_MET = 30000,
  QUEST_SIEGWARD_GAVE_ESTUS = 30001,
  QUEST_SIEGWARD_COMPLETE = 30002,

  -- Player
  PLAYER_HAS_LORDVESSEL = 40000,
  PLAYER_KINDLED_FIRST_FLAME = 40001
}

Log.info("Flag IDs loaded")
