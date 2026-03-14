Log.info("cube_controller loaded")

function onUpdate(dt, totalTime)
  local pos = transform:getPosition();

  if pos.y < 0.6 and pos.y > 0.4 then
    Log.info("Cube landed!");
  end
end
