#pragma once
#include <forge/Bone.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace forge {

class AnimationClip {
public:
  std::string name;
  float duration = 0.0f; // Total length of clip in seconds
  float ticksPerSec = 24.0f; // Source FPS (est. for now)
  bool looping = true;

  // One track per bone that has animation data
  std::vector<BoneTrack> tracks;

  // Fast lookup: bonename -> track idx
  std::unordered_map<std::string, int> trackIndex;

  // Sample all bones at time t, returns local transform per bone
  // boneCount is the total number of bones in model skeleton
  void sample(float t,
              const std::vector<Bone>& skeleton,
              std::vector<glm::mat4>& outLocalTransforms) const;
};

}
