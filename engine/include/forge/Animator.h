#pragma once
#include <forge/AnimationClip.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>

namespace forge {

constexpr int MAX_BONES = 100;

class Animator {
public:
  Animator() = default;

  // Load skeleton definition (from SkeletalMesh)
  void setSkeleton(const std::vector<Bone>& skeleton);

  // Play Clip
  void play(std::shared_ptr<AnimationClip> clip,
            bool loop = true, float blendTime = 0.2f);

  // Advance animation - called every frame
  void update(float dt);

  // Returns final bone matrix palette ready for the shader
  const std::vector<glm::mat4>& getBoneMatrices() const { return m_boneMatrices; }
  const std::vector<glm::mat4>& getGlobalTransforms() const { return m_globalTransforms; }

  float getCurrentTime() const { return m_currentTime; }
  bool isPlaying() const { return m_currentClip != nullptr; }
private:
  void computeBoneMatrices();

  std::vector<Bone> m_skeleton;
  std::vector<glm::mat4> m_boneMatrices; // Final palette for GPU
  std::vector<glm::mat4> m_localTransforms; // Per-bone local TRS
  std::vector<glm::mat4> m_globalTransforms;

  std::shared_ptr<AnimationClip> m_currentClip;
  float m_currentTime = 0.0f;

  // Blend state (basic crossfade as example)
  std::shared_ptr<AnimationClip> m_prevClip;
  float m_blendTime = 0.0f;
  float m_blendElapsed = 0.0f;
};

}
