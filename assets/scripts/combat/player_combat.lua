Log.info("Player combat script loaded")

function onCombatUpdate(dt, input)
  if not playerCombat:isAlive() then return end

  if input.attackLight and not playerCombat:isAttacking() then
    if playerCombat:tryAttack(Attacks.R1) then
      Log.info("R1 Swing!")
    end
  end

  if input.attackHeavy and not playerCombat:isAttacking() then
    if playerCombat:tryAttack(Attacks.R2) then
      Log.info("R2 Overhead!")
    end
  end

  if input.guard then
    playerCombat:tryGuard()
  else
    playerCombat:releaseGuard()
  end

  debugTimer = (debugTimer or 0) + dt
  if debugTimer > 2.0 then
    debugTimer = 0
    Log.info(string.format(
      "Player - HP: %.0f STA: %.0f State: %s",
      playerCombat:getHp(),
      playerCombat:getStamina(),
      playerCombat:getStateName()
    ))
  end
end

-- Fired when player takes hit (set as callback from c++)
function onPlayerHit(damage, damageType)
  Log.warn(string.format("Player hit! %.0f %s damage", damage, damageType))
end

-- Fired when player dies
function onPlayerDeath()
  Log.error("YOU DIED")
end
