#pragma once
#include <forge/AnimationClip.h>
#include <forge/AnimGraph.h>
#include <forge/TaskSystem.h>
#include <forge/SampledEventsBuffer.h>
#ifdef FORGE_DEBUG
#include <forge/RootMotionDebugger.h>
#endif
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
  void setGraph(GraphInstance instance);

  // Load each clip in clipPaths and swap it into any ClipNode in the graph
  // where key == map key Called by EquipmentComponent on equip
  // clipPaths: action key("r1") -> asset-root-relative path to anim
  void swapMovesetClips(const std::unordered_map<std::string, std::string>& clipPaths);

  AnimParamTable& getParams() { return m_params; }
  const AnimParamTable& getParams() const { return m_params; }

  // Advance animation - called every frame
  void updatePrePhysics(float dt);
  void updatePostPhysics();
  void update(float dt) { updatePrePhysics(dt); updatePostPhysics(); }

#ifdef FORGE_DEBUG
  RootMotionDebugger& debugger() { return m_debugger; }
  const RootMotionDebugger& debugger() const { return m_debugger; }
#endif

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

  // For tool/preview use: wraps a single already loaded clip in a minimal graph
  void setPreviewClip(std::shared_ptr<AnimationClip> clip, bool loop = false);

  GraphInstance* getGraphInstance() { return &m_graphInstance; }
  const GraphInstance* getGraphInstance() const { return &m_graphInstance; }

  const SampledEventsBuffer& sampledEvents() const { return m_sampledEvents; }
private:
  void computeBoneMatrices();
  
  std::string m_ownerName;

  std::vector<Bone> m_skeleton;
  std::vector<glm::mat4> m_boneMatrices; // Final palette for GPU
  std::vector<glm::mat4> m_localTransforms; // Per-bone local TRS
  std::vector<glm::mat4> m_globalTransforms;
  TaskSystem m_taskSystem;
  TaskIndex m_rootTask = k_invalidTask;

#ifdef FORGE_DEBUG
  RootMotionDebugger m_debugger;
#endif

  // Graph
  GraphInstance m_graphInstance;
  AnimParamTable m_params;

  glm::vec3 m_rootMotionDelta = glm::vec3(0.0f);
  bool m_rootMotionActive = false;

  SampledEventsBuffer m_sampledEvents;
};

}
