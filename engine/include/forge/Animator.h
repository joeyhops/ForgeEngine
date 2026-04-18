#pragma once
#include <forge/AnimationClip.h>
#include <forge/AnimGraph.h>
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

  void setOwnerName(const std::string& name) { m_ownerName = name; }

  // Animation Graph (replaces play())
  // Attach a graph to drive all animation via the param table
  void setGraph(std::shared_ptr<AnimGraphNode> graph);

  AnimParamTable& getParams() { return m_params; }
  const AnimParamTable& getParams() const { return m_params; }

  // Advance animation - called every frame
  void update(float dt);

  bool isFinished() const;

  // Returns final bone matrix palette ready for the shader
  const std::vector<glm::mat4>& getBoneMatrices() const { return m_boneMatrices; }
  const std::vector<glm::mat4>& getGlobalTransforms() const { return m_globalTransforms; }

  float getActiveClipTime() const;
  float getActiveClipDuration() const;
  std::string getStateInfo() const; 
private:
  void computeBoneMatrices();
  
  std::string m_ownerName;

  std::vector<Bone> m_skeleton;
  std::vector<glm::mat4> m_boneMatrices; // Final palette for GPU
  std::vector<glm::mat4> m_localTransforms; // Per-bone local TRS
  std::vector<glm::mat4> m_globalTransforms;

  // Graph
  std::shared_ptr<AnimGraphNode> m_graph;
  AnimParamTable m_params;
};

}
