#pragma once
#include <forge/AnimationClip.h>
#include <forge/AnimGraph.h>
#include <glm/glm.hpp>

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

namespace forge {

constexpr int MAX_BONES = 128;

class Animator {
public:
  Animator() = default;

  // Load skeleton definition (from SkeletalMesh)
  void setSkeleton(const std::vector<Bone>& skeleton);

  void setOwnerName(const std::string& name) { m_ownerName = name; }

  // Animation Graph (replaces play())
  // Attach a graph to drive all animation via the param table
  void setGraph(std::shared_ptr<AnimGraphNode> graph);

  // Load each clip in clipPaths and swap it into any ClipNode in the graph
  // where key == map key Called by EquipmentComponent on equip
  // clipPaths: action key("r1") -> asset-root-relative path to anim
  void swapMovesetClips(const std::unordered_map<std::string, std::string>& clipPaths);

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

  // Seek the active clip to explicit time t and recompute bone matrices
  // Does not advance frame state or fire TAE events
  // The next call to update(t) resumes normal playback from this pos
  void scrubTo(float t);

  std::string getCurrentStateName() const;
  std::string getStateInfo() const; 

  // root motion delta for curr frame (in anim local space). zeroed on update().
  // Y is already masked per the active clips extractRootY flag
  glm::vec3 getRootMotionDelta() const { return m_rootMotionDelta; }

  // true when current active leaf clip has useRootMotion == true
  // use to decide whether to drive the character from the delta
  // or from the physics engine
  bool isRootMotionActive() const { return m_rootMotionActive; }
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

  glm::vec3 m_rootMotionDelta = glm::vec3(0.0f);
  bool m_rootMotionActive = false;
};

}
