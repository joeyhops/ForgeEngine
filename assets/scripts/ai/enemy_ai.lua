-- Enemy_ai.lua
-- Controls the enemy AI. All behavior tuning lives here.
-- The c++ AIComponent handles the state machine;
-- these callbacks let Lua override decisions

Log.info("Enemy AI Script loaded")

-- Config
local DETECTION_RADIUS = 7.0
local ATTACK_RADIUS = 2.0
local PATROL_SPEED = 2.0
local CHASE_SPEED = 4.5

-- Phase 2 rage mode - triggered when HP < 30%
local enraged = false

-- Called once on startup
function onAIInit()
  enemyAI:setDetectionRadius(DETECTION_RADIUS)
  enemyAI:setAttackRadius(ATTACK_RADIUS)
  enemyAI:setMoveSpeed(PATROL_SPEED)

  Log.info("Enemy AI Initialized")
end

-- Atk selection - Called by c++ when entering attack state
function onChooseAttack()
  local hp = enemyCombat:getHp()
  local maxHp = enemyCombat:getMaxHp()
  local ratio = hp / maxHp

  -- > 30%? ENRAGE!
  if ratio < 0.30 and not enraged then
    enraged = true
    enemyAI:setMoveSpeed(CHASE_SPEED)
    Log.warn("Enemy enraged!")
    Events.publish("enemyEnraged", "phase2")
  end

  -- Enraged enemy favors heavy attack
  if enraged then
    -- 60% chance heavy, 40% light
    if math.random() < 0.60 then
      return "R2"
    end
  end

  -- Normal: 70% light, 30% Heavy
  if math.random() < 0.70 then
    return "R1"
  else
    return "R2"
  end
end

-- Player detected callback
function onPlayerDetected()
  Log.warn("Enemy spotted the player!")
  Events.publish("enemyAlert", "spotted")
end

-- Player lost CB
function onPlayerLost()
  Log.info("Enemy lost sight of player, returning to patrol")
end

-- Per frame hook (optional, for custom behavior)
local debugTimer = 0
function onAIUpdate(dt)
  debugTimer = debugTimer + dt
  if debugTimer >= 3.0 then
    debugTimer = 0
    Log.info(string.format(
      "Enemy AI: %s dist=%.1f HP=%.0f enraged=%s",
      enemyAI:getStateName(),
      enemyAI:getDistToPlayer(),
      enemyCombat:getHp(),
      tostring(enraged)
    ))
  end
end
