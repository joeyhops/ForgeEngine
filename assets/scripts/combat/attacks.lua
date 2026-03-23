Attacks = {}

Attacks.R1 = AttackData.new()
Attacks.R1.name = "R1_Slash"
Attacks.R1.damage = 120.0
Attacks.R1.poiseDamage = 20.0
Attacks.R1.staminaCost = 18.0
Attacks.R1.startupTime = 0.15 -- 9 frames
Attacks.R1.activeTime = 0.10 -- ~6 frames
Attacks.R1.recoveryTime = 0.30 -- 18 frames
Attacks.R1.range = 1.8
Attacks.R1.angle = 90.0
Attacks.R1.damageType = "slash"


Attacks.R2 = AttackData.new()
Attacks.R2.name = "R2_Overhead"
Attacks.R2.damage = 260.0
Attacks.R2.poiseDamage = 55.0
Attacks.R2.staminaCost = 35.0
Attacks.R2.startupTime = 0.35 -- 21 frames
Attacks.R2.activeTime = 0.12 -- ~6 frames
Attacks.R2.recoveryTime = 0.55 -- 33 frames
Attacks.R2.range = 2.2
Attacks.R2.angle = 70.0
Attacks.R2.damageType = "strike"


Attacks.Thrust = AttackData.new()
Attacks.Thrust.name = "R2_Overhead"
Attacks.Thrust.damage = 140.0
Attacks.Thrust.poiseDamage = 15.0
Attacks.Thrust.staminaCost = 22.0
Attacks.Thrust.startupTime = 0.10 
Attacks.Thrust.activeTime = 0.08 
Attacks.Thrust.recoveryTime = 0.25
Attacks.Thrust.range = 2.8 -- Long reach
Attacks.Thrust.angle = 40.0 -- Very narrow - must face target directly
Attacks.Thrust.damageType = "thrust"

Log.info("Attack definitions loaded")

