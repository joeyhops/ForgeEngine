#pragma once
#include <forge/AnimationClip.h>
#include <forge/AnimGraph.h>
#include <forge/Logger.h>

#include <array>
#include <memory>
#include <string>

enum class DodgeDir {
  Forward = 0,
  ForwardRight = 1,
  Right = 2,
  BackwardRight = 3,
  Backward = 4,
  BackwardLeft = 5,
  Left = 6,
  ForwardLeft = 7
};

struct CharacterClipSet {
  // required
  std::shared_ptr<forge::AnimationClip> idle;
  std::shared_ptr<forge::AnimationClip> walkFwd;
  std::shared_ptr<forge::AnimationClip> attack;
  std::shared_ptr<forge::AnimationClip> death;

  // Optional

  // free locomotion sprint
  std::shared_ptr<forge::AnimationClip> sprintFwd;

  // Hit reaction (stagger on poise break)
  std::shared_ptr<forge::AnimationClip> hitReaction;

  // Standstill only dodge Null = standstill dodge plays the forward roll instead
  // when present, triggered by the "backstep" anim param trigger
  std::shared_ptr<forge::AnimationClip> backstep;

  // Directional rolls, indexed by DodgeDir
  // all null = no dodge state
  // individual nulls fallback to the next closest non-null entry
  std::array<std::shared_ptr<forge::AnimationClip>, 8> dodgeClips = {};

  // Locked on directional walk, indexed by DodgeDir
  std::array<std::shared_ptr<forge::AnimationClip>, 8> walkLocked = {};
  std::array<std::shared_ptr<forge::AnimationClip>, 8> sprintLocked = {};

};

std::shared_ptr<forge::StateMachineNode> buildCharacterGraph(
  const CharacterClipSet& clips,
  const std::string& ownerName
);
