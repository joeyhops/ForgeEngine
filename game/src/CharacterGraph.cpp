#include "CharacterGraph.h"
#include "forge/Logger.h"

std::shared_ptr<forge::StateMachineNode> buildCharacterGraph(
  const CharacterClipSet& clips,
  const std::string& ownerName)
{
  using namespace forge;

  if (!clips.idle || !clips.walkFwd || !clips.attack || !clips.death) {
    LOG_ERROR("[Game] buildCharacterGraph('{}') - required clip null", ownerName);
    return nullptr;
  }

  // Locomotion state
  // Build locomotion state as a nested StateMachineNode
  // Containing FreeLoco (Blend1DNode) and optionally 
  // LockedLoco (Blend2DNode)

  auto freeLoco = std::make_shared<Blend1DNode>("moveSpeed");
  auto idleNode = std::make_shared<ClipNode>(clips.idle, true);
  auto walkNode = std::make_shared<ClipNode>(clips.walkFwd, true);
  idleNode->setOwnerName(ownerName);
  idleNode->setActionKey("idle");
  walkNode->setOwnerName(ownerName);
  walkNode->setActionKey("walk");
  freeLoco->addEntry(0.0f, idleNode);
  freeLoco->addEntry(1.0f, walkNode);
  if (clips.sprintFwd) {
    auto sprintNode = std::make_shared<ClipNode>(clips.sprintFwd, true);
    sprintNode->setOwnerName(ownerName);
    sprintNode->setActionKey("sprint");
    freeLoco->addEntry(2.0f, sprintNode);
  }

  // LockedLoco is Blend2DNode on moveX/moveZ
  bool hasLockedLoco = (clips.walkLocked[0] != nullptr);
  std::shared_ptr<Blend2DNode> lockedLoco;
  if (hasLockedLoco) {
    lockedLoco = std::make_shared<Blend2DNode>("moveX", "moveZ");
    lockedLoco->addClip({ 0.0f, 0.0f }, clips.idle);

    // Walk ring - magnitude 1.0 in 8 dirs
    // DodgeDir indices map to these 2D positions:
    // F=+z, R=+X, B=-Z, L=-X, diagonals at 0.71
    static const glm::vec2 k_walkPos[8] = {
      { 0.00f, 1.00f }, // Forward
      { 0.71f, 0.71f }, // ForwardRight
      { 1.00f, 0.00f }, // Right
      { 0.71f, -0.71f }, // BackwardRight
      { 0.00f, -1.00f }, // Backward
      { -0.71f, -0.71f }, // BackwardLeft
      { -1.00f, 0.00f }, // Left
      { -0.71f, 0.71f }, // ForwardLeft
    };
    static const glm::vec2 k_sprintPos[8] = {
      { 0.00f, 2.00f },
      { 1.41f, 1.41f },
      { 2.00f, 0.00f },
      { 1.41f, -1.41f },
      { 0.00f, -2.00f },
      { -1.41f, -1.41f },
      { -2.00f, 0.00f },
      { -1.41f, 1.41f },
    };
    for (int i = 0; i < 8; i++) {
      if (clips.walkLocked[i])
        lockedLoco->addClip(k_walkPos[i], clips.walkLocked[i]);
      if (clips.sprintLocked[i])
        lockedLoco->addClip(k_sprintPos[i], clips.sprintLocked[i]);
    }
    lockedLoco->build();
  }

  auto locoNode = std::make_shared<StateMachineNode>();
  locoNode->addState("FreeLoco", freeLoco);
  locoNode->setInitialState("FreeLoco");
  if (hasLockedLoco) {
    locoNode->addState("LockedLoco", lockedLoco);
    locoNode->addTransition({ "FreeLoco", "LockedLoco",
                            [](AnimParamTable& p) { return p.getBool("isLockedOn"); }, 0.0f });
    locoNode->addTransition({ "LockedLoco", "FreeLoco",
                            [](AnimParamTable& p) { return !p.getBool("isLockedOn"); }, 0.0f });
  }

  // Attack State
  auto attackNode = std::make_shared<ClipNode>(clips.attack, false);
  attackNode->setOwnerName(ownerName);
  attackNode->setActionKey("r1");

  // death state
  auto deathNode = std::make_shared<ClipNode>(clips.death, false);
  deathNode->setOwnerName(ownerName);
  deathNode->setActionKey("death");

  // Root State Machine
  auto root = std::make_shared<StateMachineNode>();
  root->addState("Locomotion", locoNode);
  root->addState("Attacking", attackNode);
  root->addState("Dead", deathNode);
  root->setInitialState("Locomotion");

  // Locomotion <-> attacking
  root->addTransition({ "Locomotion", "Attacking",
                      [](AnimParamTable& p) {
      return p.consumeTrigger("attackR1") || p.consumeTrigger("attackR2");
    }, 0.1f });
  root->addTransition({ "Attacking", "Locomotion",
                          [attackNode](AnimParamTable& p) { return attackNode->isFinished(); }, 0.2f });

  // Optional: dodging state
  bool hasDodge = false;
  for (auto& c : clips.dodgeClips) if (c) { hasDodge = true; break; }
  if (hasDodge) {
    auto dodgeSelector = std::make_shared<ClipSelectorNode>("dodgeDir");
    for (int i = 0; i < 8; i++) {
      if (clips.dodgeClips[i])
        dodgeSelector->addClip(i, clips.dodgeClips[i]);
      else if (clips.dodgeClips[0])
          dodgeSelector->addClip(i, clips.dodgeClips[0]);
    }
    root->addState("Dodging", dodgeSelector);
    root->addTransition({ "Locomotion", "Dodging", 
        [](AnimParamTable& p) { return p.consumeTrigger("dodge"); }, 0.0f });
    root->addTransition({ "Dodging", "Locomotion",
                            [dodgeSelector](AnimParamTable& p) { return dodgeSelector->isFinished(); }, 0.3f });
  }

  // Optional HitReaction
  if (clips.hitReaction) {
    auto hitNode = std::make_shared<ClipNode>(clips.hitReaction, false);
    hitNode->setOwnerName(ownerName);
    hitNode->setActionKey("hitReaction");
    root->addState("HitReaction", hitNode);
    root->addTransition({ "", "HitReaction",
          [](AnimParamTable& p) { return p.consumeTrigger("hitReaction"); }, 0.5f });
    root->addTransition({ "HitReaction", "Locomotion",
          [hitNode](AnimParamTable&) { return hitNode->isFinished(); }, 0.25f });
  }

  // Any -> Dead
  root->addTransition({ "", "Dead", 
        [](AnimParamTable& p) { return p.getBool("isDead"); }, 0.3f });
  root->addTransition({ "Dead", "Locomotion", 
        [](AnimParamTable& p) { return !p.getBool("isDead"); }, 0.5f });

  return root;
}
