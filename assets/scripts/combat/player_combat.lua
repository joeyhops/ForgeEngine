Log.info("Player combat script loaded")

function onCombatUpdate(dt, input)
  if not playerCombat:isAlive() then return end

  if input.attackLight and not playerCombat:isAttacking() then
    -- getAttackData("r1") returns longsword R1 if equipped, bare-hand fallback otherwise.
    if playerCombat:tryAttack(playerCombat:getAttackData("r1")) then
      local wep = playerEquipment:getEquipped(EquipSlot.RIGHT)
      Log.info("R1 Swing! [" .. (wep ~= "" and wep or "bare hand") .. "]")
    end
  end

  if input.attackHeavy and not playerCombat:isAttacking() then
    if playerCombat:tryAttack(playerCombat:getAttackData("r2")) then
      local wep = playerEquipment:getEquipped(EquipSlot.RIGHT)
      Log.info("R2 Heavy! [" .. (wep ~= "" and wep or "bare hand") .. "]")
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
    local wep = playerEquipment:getEquipped(EquipSlot.RIGHT)
    Log.info(string.format(
      "Player - HP: %.0f STA: %.0f State: %s Weapon: %s",
      playerCombat:getHp(),
      playerCombat:getStamina(),
      playerCombat:getStateName(),
      wep ~= "" and wep or "none"
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
