#pragma once
#include <forge/AnimationClip.h>
#include <forge/AnimGraph.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <unordered_set>

namespace forge {

constexpr int MAX_BONES = 100;

class Animator {
public:
  Animator() = default;

  // Load skeleton definition (from SkeletalMesh)
  void setSkeleton(const std::vector<Bone>& skeleton);

  void setOwnerName(const std::string& name) { m_ownerName = name; }

  // Animation Graph (replaces play())
  // Attach a graph to drive all animation via the param table
  void setGraph(std::shared_ptr<AnimGraphNode> graph);

  AnimParamTable& getParams() { return m_params; }
  const AnimParamTable& getParams() const { return m_params; }

  // Play Clip
  void play(std::shared_ptr<AnimationClip> clip,
            bool loop = true, float blendTime = 0.2f);

  // Advance animation - called every frame
  void update(float dt);

  bool isFinished() const;

  // Returns final bone matrix palette ready for the shader
  const std::vector<glm::mat4>& getBoneMatrices() const { return m_boneMatrices; }
  const std::vector<glm::mat4>& getGlobalTransforms() const { return m_globalTransforms; }

  float getCurrentTime() const { return m_currentTime; }
  float getDuration() const { return m_currentClip ? m_currentClip->duration : 0.0f; }
  bool isPlaying() const; 
  std::string getClipName() const; 
  std::string getGraphStateInfo() const;
private:
  void computeBoneMatrices();
  
  void tickLegacyTAEEvents(float prevTime, float curTime);
  void deactivateAllLegacyEvents();

  std::string m_ownerName;

  std::vector<Bone> m_skeleton;
  std::vector<glm::mat4> m_boneMatrices; // Final palette for GPU
  std::vector<glm::mat4> m_localTransforms; // Per-bone local TRS
  std::vector<glm::mat4> m_globalTransforms;

  // Graph
  std::shared_ptr<AnimGraphNode> m_graph;
  AnimParamTable m_params;

  std::shared_ptr<AnimationClip> m_currentClip;
  float m_currentTime = 0.0f;
  float m_prevTime = 0.0f; // Used by TAE to detect boundary crossings

  // Blend state (basic crossfade as example)
  std::shared_ptr<AnimationClip> m_prevClip;
  float m_blendTime = 0.0f;
  float m_blendElapsed = 0.0f;

  // TAE: indices into m_currentClip->events that are current "active"
  std::unordered_set<int> m_activeEventIndices;
};

}
