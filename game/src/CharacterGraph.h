#pragma once
#include <forge/AnimationClip.h>
#include <forge/AnimGraph.h>
#include <forge/Logger.h>

#include <memory>
#include <string>

std::shared_ptr<forge::StateMachineNode> buildCharacterGraph(
  std::shared_ptr<forge::AnimationClip> idleClip,
  std::shared_ptr<forge::AnimationClip> walkClip,
  std::shared_ptr<forge::AnimationClip> sprintClip,
  std::shared_ptr<forge::AnimationClip> attackClip,
  std::shared_ptr<forge::AnimationClip> dodgeClip,
  std::shared_ptr<forge::AnimationClip> deathClip,
  const std::string& ownerName);
