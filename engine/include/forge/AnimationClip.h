#pragma once
#include <forge/Bone.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace forge {

// TAE - Time Act Events
// A typed event timeline baked into each AnimationClip
// The animator fires AnimEventActivated / AnimEventDeactivated through event bus
// as m_currentTime crosses each windows boundary
// CombatComponent, audio, and VFX systems subscribe - meaning
// they never have to manage state regardng animation timing

enum class AnimEventType {
  SpawnHitbox, // hitbox window - CombatComponent subscribes
  IFrame, // invincibility frame - CombatCo subscribes
  SoundOneShot, // fire and forget sound cue (payload = sfx path)
  SFXSpawn,// particle / VFX spawn (payload = effect name)

  ComboWindow, // cancel window; payload = next action key ("r1_2")
  ParryWindow, // receptive parry window, no payload
  DeflectWindow, // receptive deflect window, no payload
  PerilousMark, // marks unblockable attack, no payload

  // Root motion
  RootMotionBegin, // enable root motion, payload = "scale;axisX;axisZ;maxVel"
  RootMotionEnd, // close root motion window, no payload
  RootMotionScale, // mid-window scale change; payload = float string
  SetKinematic, // hand physics to kinematic mode; no payload
  RestorePhysics, // return to dynamic mode, no payload

  // combat feel
  LockInput, // commitment window begins, no payload
  UnlockInput, // commitment window ends, no payload
  HyperArmorBegin, // incoming poise damage ignored, no payload
  HyperArmorEnd, // hyper armor ends, no payload

  ScriptedSequence, // trigger multiactor choreography; payload = sequence ID

  // VFX
  StartTrail, // begin weapon swing trail, no payload
  StopTrail, // end weapon swing trail, no payload
  SpawnImpactVFX // spawn impact effect, payload = effect name
};

struct AnimEvent {
  float startTime; // seconds into clip
  float endTime; // == startTime for one-shot events?
  AnimEventType type;
  std::string payload; // e.g. "weapon_r" for hitbox bone, sfx path for sounds
glm::vec3 hitboxExtents = { 0.3f, 0.8f, 0.15f };
};

class AnimationClip {
public:
  std::string name;
  float duration = 0.0f; // Total length of clip in seconds
  float ticksPerSec = 60.0f; // Source FPS (est. for now)
  bool looping = true;

  bool useRootMotion = false;
  std::string rootBoneName = "root";
  bool extractRootY = false;

  // One track per bone that has animation data
  std::vector<BoneTrack> tracks;

  // Fast lookup: bonename -> track idx
  std::unordered_map<std::string, int> trackIndex;

  //TAE events - must be kept sorted by start time
  //Animator assumes sorted order when scanning boundaries each frame
  std::vector<AnimEvent> events;

  // Sample all bones at time t, returns local transform per bone
  // boneCount is the total number of bones in model skeleton
  void sample(float t,
              const std::vector<Bone>& skeleton,
              std::vector<glm::mat4>& outLocalTransforms) const;
};

}
