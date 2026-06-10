#pragma once
#include <forge/AnimationClip.h>
#include <forge/Bone.h>
#include <forge/TypeSystem.h>
#include <forge/AssetManager.h>

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace forge {

// AnimParamTable
// Flat kv store of named floats, bools, and triggers
// Gameplay C++ and Lua scripts write to it each frame
// The graph reads it, nothing else affects the graph directly
//
// Triggers are consumed on read: setTrigger("attack") is set by
// CombatComponent and consumed the first time the graph evaluates
// a transition condition the calls consumeTrigger("attack")
// This prevents transitions from firing repeatedly while a condition is true

class AnimParamTable {
public:
  void setFloat(const std::string& k, float v) { m_floats[k] = v; }
  void setBool(const std::string& k, bool v) { m_bools[k] = v; }
  void setInt(const std::string& k, int v) { m_ints[k] = v; }
  void setTrigger(const std::string& k) { m_triggers.insert(k); }

  float getFloat(const std::string& k, float def = 0.0f) const;
  bool getBool(const std::string& k, bool def = false) const;
  int getInt(const std::string& k, int def = 0) const;

  // Return true and remove trigger - called only once per eval
  bool consumeTrigger(const std::string& k);

  // Peek without consuming
  bool hasTrigger(const std::string& k) const {
    return m_triggers.count(k) > 0;
  }

  // Read-only accessors for DebugUI
  const std::unordered_map<std::string, float>& getFloats() const { return m_floats; }
  const std::unordered_map<std::string, bool>& getBools() const { return m_bools; }
  const std::unordered_map<std::string, int>& getInts() const { return m_ints; }
  const std::unordered_set<std::string>& getTriggers() const { return m_triggers; }

private:
  std::unordered_map<std::string, float> m_floats;
  std::unordered_map<std::string, bool> m_bools;
  std::unordered_map<std::string, int> m_ints;
  std::unordered_set<std::string> m_triggers;

};

struct IAnimNodeDefinition : public IReflectedType {
  FORGE_REFLECT_TYPE(IAnimNodeDefinition)

  TypeInfo const* GetTypeInfo() const override;

  virtual ~IAnimNodeDefinition() = default;
};

// AnimGraphNode
// All nodes implement this interface
//
// update() - advance internal time, read params, check transitions
// evaluate() - writes the resulting local bone transforms into outPose
//
// Skeleton pointer is injected top-down via setSkeleton() when graph
// is atached to an animator. All nodes must forward it to their children

class AnimGraphNode {
public:
  virtual ~AnimGraphNode() = default;

  virtual void update(float dt, AnimParamTable& params) = 0;
  virtual void evaluate(std::vector<glm::mat4>& outPose) const = 0;
  virtual bool isFinished() const { return false; }

  virtual float getActiveClipTime() const { return 0.0f; }
  virtual float getActiveClipDuration() const { return 0.0f; }

  // propagate the skeleton to leaf ClipNodes
  // Called once when the graph attaches to an animator
  virtual void setSkeleton(const std::vector<Bone>* skeleton) { (void)skeleton; }

  // debugui
  virtual std::string getDebugStateInfo() const { return "AnimGraphNode"; }

  // Traverse graph and replace clip in clip node when action key matches
  virtual void swapClipByKey(const std::string& key, std::shared_ptr<AnimationClip> clip) {
    (void)key; (void)clip;
  }

  // Seek the active leaf clip to time t without advancing internal state
  // or firing TAE events. Called by Animator::scrubTo(), default no-op
  // Concrete overrides propagate to their active children
  virtual void setActiveTime(float) {}

  // returns this nodes root motion displacement for the current frame
  // non-root-motion node return vec3(0) blend nodes return weighted avg
  virtual glm::vec3 getRootMotionDelta() const { return glm::vec3(0.0f); }
};

// ClipNode
// Leaf node. Wraps an AnimationClip, advances its own local time, samples
// bone transforms, fires TAE events through the event bus
// (inspo: HavokClipGenerator)

class ClipNode : public AnimGraphNode {
public:
  struct Definition : public IAnimNodeDefinition {
    FORGE_REFLECT_TYPE(ClipNode::Definition)

    TypeInfo const* GetTypeInfo() const override;

    FORGE_REFLECT() std::string clipFile; // file-relative path
    FORGE_REFLECT() std::string clipName; // track name within file: "Armature|Idle"
    FORGE_REFLECT() std::string taeKey; // sidecar key, empty == no TAE events
    FORGE_REFLECT() std::string actionKey; // moveset swap key: "r1" "idle" "walk"
    FORGE_REFLECT() std::string ownerName; // debug/TAE context: "player", "hollowSoldier"
    FORGE_REFLECT() bool loop = true;
  };

  // Created by GraphInstance::Create() never by direct construction in game code
  ClipNode() = default;

  // Restart from time 0
  void reset();
  void update(float dt, AnimParamTable& params) override;
  void evaluate(std::vector<glm::mat4>& outPose) const override;
  bool isFinished() const override;
  void setActiveTime(float t) override;
  void setSkeleton(const std::vector<Bone>* skeleton) override { m_skeleton = skeleton; }
  // Direct clip replacement -- resets playback state
  void swapClipByKey(const std::string& key, std::shared_ptr<AnimationClip> clip) override;
  void setClip(std::shared_ptr<AnimationClip> clip);
  std::string getDebugStateInfo() const override;
  float getActiveClipTime() const override { return m_time; }
  float getActiveClipDuration() const override { return m_clip ? m_clip->duration : 0.0f; }
  glm::vec3 getRootMotionDelta() const override { return m_rootDelta; }

  const Definition* m_pDef = nullptr; // Points to GraphDefinition::m_nodeDefs[i]
  std::shared_ptr<AnimationClip> m_clip; // Resolved at GraphInstance::Create() time
  
  // Per-character state
  float m_time = 0.0f;
  float m_prevTime = 0.0f;
  uint64_t m_activeEventMask = 0; // replaces unordered_set<int>; bit i = event i active
  glm::vec3 m_rootDelta = glm::vec3(0.0f);
  
  // Injected not owned
  const std::vector<Bone>* m_skeleton = nullptr;
  mutable std::vector<glm::mat4> m_pose;
private:
  void tickTAEEvents(float prevTime, float curTime);
  void deactivateAllEvents();
};

// Blend1DNode
// Reads a named float param and lineraly blends between two child nodes
// param == 0.0 -> fully m_from
// param == 1.0 -> fully m_to

class Blend1DNode : public AnimGraphNode {
public:

  struct Definition : public IAnimNodeDefinition {
    FORGE_REFLECT_TYPE(Blend1DNode::Definition)

    TypeInfo const* GetTypeInfo() const override;

    FORGE_REFLECT() std::string paramName;
    FORGE_REFLECT() std::vector<float> thresholds;
    FORGE_REFLECT() std::vector<int16_t> childNodeIndices; // parallel to thresholds
  };

  Blend1DNode() = default;

  void update(float dt, AnimParamTable& params) override;
  void evaluate(std::vector<glm::mat4>& outPose) const  override;
  void setSkeleton(const std::vector<Bone>* skeleton) override;
  void setActiveTime(float t) override;
  glm::vec3 getRootMotionDelta() const override { return m_rootDelta; }
  std::string getDebugStateInfo() const override;
  float getActiveClipTime() const override;
  float getActiveClipDuration() const override;

  const Definition* m_pDef = nullptr;
  std::vector<AnimGraphNode*> m_children; // raw ptrs into GraphInstance Buffer

  // per-character state
  float m_param = 0.0f;
  int m_lowerIdx = 0; // Index of lower bracketing entry
  float m_localAlpha = 0.0f; 
  glm::vec3 m_rootDelta = glm::vec3(0.0f);
private:
  void swapClipByKey(const std::string& key, std::shared_ptr<AnimationClip> clip) override;
};

// Blend2DNode
// 2D blended animation: clips are registered at 2D positions in a param
// space (e.g. moveX/moveZ for locked locomotion). At build() time, a
// Delaunay triangulation ofthose positions is computed, along with the
// convex-hull edges. Each frame, the current parameter point is located
// in the Triangulation and the three surrounding clips are blended using
// barycentric coordinates. Both poses and root motion deltas are blended
// by the same weights.
//
// All child clip nodes advance simultaneously every frame to keep looping
// clips phase-synced across triangle edge crossings.
//
// Outside convex-hull behavior: the parameter is clamped to the closest
// point on the hull boundary; the blend is computed for the triangle
// adjacent to that hull edge.

class Blend2DNode : public AnimGraphNode {
public:
  struct ClipEntry {
    FORGE_REFLECT() float x;
    FORGE_REFLECT() float z;
    FORGE_REFLECT() int16_t nodeIndex; // index into GraphDefinition::m_nodeDefs
  };

  struct Definition : public IAnimNodeDefinition {
    FORGE_REFLECT_TYPE(Blend2DNode::Definition)

    TypeInfo const* GetTypeInfo() const override;

    FORGE_REFLECT() std::string xParamName;
    FORGE_REFLECT() std::string zParamName;
    FORGE_REFLECT() std::vector<ClipEntry> clips;
  };

  Blend2DNode() = default;

  // computer delaunay triangulation and hull edges from registered positions
  // Must be called once after all addClip() calls, before first update()
  void build();
  void update(float dt, AnimParamTable& params) override;
  void evaluate(std::vector<glm::mat4>& outPose) const override;
  void setSkeleton(const std::vector<Bone>* skeleton) override;
  void setActiveTime(float t) override;
  glm::vec3 getRootMotionDelta() const override { return m_rootDelta; }
  float getActiveClipTime() const override;
  float getActiveClipDuration() const override;
  std::string getDebugStateInfo() const override;

  const Definition* m_pDef = nullptr;
  std::vector<ClipNode*> m_children;

  // Triangulation
  struct Triangle { int a, b, c; }; // indices into m_clips
  struct HullEdge { int u, v, tri; };
  std::vector<Triangle> m_triangles; // computed once in build()
  std::vector<HullEdge> m_hullEdges;

  // Per character state
  int m_idxA = 0, m_idxB = 0, m_idxC = 0;
  glm::vec3 m_weights { 1.0f, 0.0f, 0.0f };
  glm::vec2 m_currentParam { 0.0f, 0.0f };
  glm::vec3 m_rootDelta = glm::vec3(0.0f);
private:
  void swapClipByKey(const std::string& key,
                     std::shared_ptr<AnimationClip> clip) override;
};

// ClipSelectorNode
// holds N child 'ClipNodes' keyed by int index. Each frame reads a named int
// param from ANimParamTable and forwards all operations to the active child
// when the selected index changes, the incoming 'ClipNode' is reset() so it
// plays from t = 0. Identical in intent to a zero-blend-time StateMachineNode
// transition but simpler and cheaper to store (no crossfade)
class ClipSelectorNode : public AnimGraphNode {
public:
  struct Entry {
    FORGE_REFLECT() int selectorIndex; // int param that activates entry
    FORGE_REFLECT() int16_t nodeIndex;
  };

  struct Definition : public IAnimNodeDefinition {
    FORGE_REFLECT_TYPE(ClipSelectorNode::Definition)

    TypeInfo const* GetTypeInfo() const override;

    FORGE_REFLECT() std::string paramName;
    FORGE_REFLECT() std::string ownerName;
    FORGE_REFLECT() std::vector<Entry> entries;
  };

  ClipSelectorNode() = default;

  void update(float dt, AnimParamTable& params) override;
  void evaluate(std::vector<glm::mat4>& outPose) const override;
  bool isFinished() const override;
  void setSkeleton(const std::vector<Bone>* skeleton) override;
  void setActiveTime(float t) override;
  glm::vec3 getRootMotionDelta()const override;
  float getActiveClipTime() const override;
  float getActiveClipDuration() const override;
  std::string getDebugStateInfo() const override;

  const Definition* m_pDef = nullptr;
  std::vector<ClipNode*> m_children;
  std::unordered_map<int, ClipNode*> m_byIndex; // Built at create time for O(1) lookup

  // Per character state
  int m_selected = 0;
  int m_prevSelected = -1;
private:
  void swapClipByKey(const std::string& key,
                     std::shared_ptr<AnimationClip> clip) override;
};

// StateMachineNode
// Owns a set of named states, each pointing to a child AnimGraphNode
// Transitions are declared as (fromState, toState, condition, blendTime) tuples
// fromState == "" means "from any state" - checked after named transitions
//
// when transition fires:
//  - incoming node is reset() if it is a clip node
//  - A crossfade blend starts over m_blendTime seconds
//  - evaluate() lerps between the outgoing and incoming node poses

FORGE_REFLECT_ENUM(ConditionType)
enum class ConditionType : uint8_t {
  TriggerConsumed,
  BoolTrue,
  BoolFalse,
  SourceNodeFinished
};

struct TransitionConditionDef {
  FORGE_REFLECT() ConditionType type = ConditionType::BoolTrue;
  FORGE_REFLECT() std::string param;
};

class StateMachineNode : public AnimGraphNode {
public:
  struct StateDef {
    FORGE_REFLECT() std::string name;
    FORGE_REFLECT() int16_t nodeIndex;
  };

  struct TransitionDef {
    FORGE_REFLECT() std::string fromState; // "" == any state
    FORGE_REFLECT() std::string toState;
    FORGE_REFLECT() float blendTime = 0.2f;
    FORGE_REFLECT() std::vector<TransitionConditionDef> conditions;
    FORGE_REFLECT() bool requireAll = true; // true=AND false=OR
  };

  struct Definition : public IAnimNodeDefinition {
    FORGE_REFLECT_TYPE(StateMachineNode::Definition)

    TypeInfo const* GetTypeInfo() const override;

    FORGE_REFLECT() std::string initialState;
    FORGE_REFLECT() std::vector<StateDef> states;
    FORGE_REFLECT() std::vector<TransitionDef> transitions;
  };

  StateMachineNode() = default;

  void update(float dt, AnimParamTable& params) override;
  void evaluate(std::vector<glm::mat4>& outPose) const override;
  bool isFinished() const override;
  void setActiveTime(float t) override;
  void setSkeleton(const std::vector<Bone>* skeleton) override;
  glm::vec3 getRootMotionDelta() const override;
  float getActiveClipTime() const override;
  float getActiveClipDuration() const override;
  std::string getDebugStateInfo() const override;
  const std::string& getCurrentState() const { return m_currentState; }

  const Definition* m_pDef = nullptr;
  std::vector<AnimGraphNode*> m_stateNodes;

  // per character state
  std::string m_currentState;
  std::string m_previousState;
  float m_blendAlpha = 1.0f; // 1.0 = fully in current state
  float m_blendTime = 0.0f;
  float m_blendTimer = 0.0f;
  mutable std::vector<glm::mat4> m_prevPose;
  mutable std::vector<glm::mat4> m_currPose;

private:
  int findStateIndex(const std::string& name) const;
  void transitionTo(const std::string& toState, float blendTime, AnimParamTable& params);
  bool evaluateConditions(const StateMachineNode::TransitionDef& td, AnimParamTable& params) const;
};

struct GraphDefinition {
  std::vector<std::unique_ptr<IAnimNodeDefinition>> m_nodeDefs;
  std::vector<TypeID> m_nodeTypeIDs;
  int16_t m_rootNodeIndex = 0;

  static std::shared_ptr<GraphDefinition> LoadFromJson(const std::string& path);
};

struct GraphInstance {
  std::shared_ptr<const GraphDefinition> m_pDefinition;
  std::unique_ptr<std::byte[]> m_nodeBuffer;
  std::vector<AnimGraphNode*> m_nodes; // Ptrs into m_nodeBuffer
  std::unordered_map<std::string, ClipNode*> m_clipsByActionKey; // for swapMovesetClips

  static GraphInstance Create(std::shared_ptr<const GraphDefinition> def);
  GraphInstance Clone() const;
  AnimGraphNode* GetRootNode() const;
  ClipNode* FindClipByActionKey(const std::string& key) const;

  GraphInstance() = default;
  GraphInstance(GraphInstance&&) = default;
  GraphInstance& operator=(GraphInstance&&) = default;
  GraphInstance(const GraphInstance&) = delete;
  GraphInstance& operator=(const GraphInstance&) = delete;
  ~GraphInstance();
};

}
