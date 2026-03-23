#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace forge {

// Single keyframe for one bone
struct BoneKeyFrame {
  float time; // Seconds into animation
  glm::vec3 position;
  glm::quat rotation;
  glm::vec3 scale;
};

// All keyframes for one bone across entire clip
struct BoneTrack {
  std::string boneName;
  std::vector<BoneKeyFrame> keyframes;

  // Sample this track at time t, interpolating between keyframes
  BoneKeyFrame sample(float t) const;
};

// Static bone data loaded from model
struct Bone {
  std::string name;
  int parentIndex = -1; // -1 = root bone
  glm::mat4 offsetMatrix; // Bindpose inverse (model->bone space)
  glm::mat4 localTransform; // Default pose transform
};

}
