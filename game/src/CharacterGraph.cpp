#include "CharacterGraph.h"

std::shared_ptr<forge::StateMachineNode> buildCharacterGraph(
  std::shared_ptr<forge::AnimationClip> idleClip,
  std::shared_ptr<forge::AnimationClip> walkClip, 
  std::shared_ptr<forge::AnimationClip> sprintClip, // Optional - no sprint if null
  std::shared_ptr<forge::AnimationClip> attackClip,
  std::shared_ptr<forge::AnimationClip> dodgeClip, // optional - no dodge if null
  std::shared_ptr<forge::AnimationClip> deathClip,
  const std::string& ownerName)
{
  using namespace forge;

  if (!idleClip || !walkClip || !attackClip || !deathClip) {
    LOG_ERROR("[Game] buildCharacterGraph('{}') - required clip null (need idle/walk/attack/death)", ownerName);
    return nullptr;
  }
  auto idleNode = std::make_shared<ClipNode>(idleClip, true);
  auto walkNode = std::make_shared<ClipNode>(walkClip, true); //todo walk clip
  idleNode->setOwnerName(ownerName);
  walkNode->setOwnerName(ownerName);

  auto locomotionNode = std::make_shared<Blend1DNode>("moveSpeed");
  locomotionNode->addEntry(0.0f, idleNode);
  locomotionNode->addEntry(1.0f, walkNode);

  if (sprintClip) {
    auto sprintNode = std::make_shared<ClipNode>(sprintClip, true);
    sprintNode->setOwnerName(ownerName);
    locomotionNode->addEntry(2.0f, sprintNode);
  }

  // atk
  auto attackNode = std::make_shared<ClipNode>(attackClip, false);
  attackNode->setOwnerName(ownerName);
  attackNode->setActionKey("r1");

  // death
  auto deathNode = std::make_shared<ClipNode>(deathClip, false);
  deathNode->setOwnerName(ownerName);

  // graph root
  auto root = std::make_shared<StateMachineNode>();
  root->addState("Locomotion", locomotionNode);
  root->addState("Attacking", attackNode);
  root->addState("Dead", deathNode);

  // Locomotion-> attacking
  root->addTransition({
    "Locomotion", "Attacking",
    [](AnimParamTable& p) {
      return p.consumeTrigger("attackR1") || p.consumeTrigger("attackR2");
    },
    0.1f
  });

  // attacking -> locomotion
  root->addTransition({
    "Attacking", "Locomotion",
    [attackNode](AnimParamTable&) {
      return attackNode->isFinished();
    },
    0.2f
  });

  if (dodgeClip) {
    auto dodgeNode = std::make_shared<ClipNode>(dodgeClip, false);
    dodgeNode->setOwnerName(ownerName);
    root->addState("Dodging", dodgeNode);

    root->addTransition({
      "Locomotion", "Dodging",
      [](AnimParamTable& p) { return p.consumeTrigger("dodge"); },
      0.0f
    });

    root->addTransition({
      "Dodging", "Locomotion",
      [dodgeNode](AnimParamTable&) { return dodgeNode->isFinished(); },
      0.5f 
    });
  }

  // Any -> dead
  root->addTransition({
    "", "Dead",
    [](AnimParamTable& p) { return p.getBool("isDead"); },
    0.3f
  });

  root->addTransition({
    "Dead", "Locomotion",
    [](AnimParamTable& p) { return !p.getBool("isDead"); },
    0.5f
  });

  root->setInitialState("Locomotion");
  return root;
}
