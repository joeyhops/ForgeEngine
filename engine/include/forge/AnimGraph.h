#pragma once
#include <forge/AnimationClip.h>
#include <forge/Bone.h>
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
  void setTrigger(const std::string& k) { m_triggers.insert(k); }

  float getFloat(const std::string& k, float def = 0.0f) const;
  bool getBool(const std::string& k, bool def = false) const;

  // Return true and remove trigger - called only once per eval
  bool consumeTrigger(const std::string& k);

  // Peek without consuming
  bool hasTrigger(const std::string& k) const {
    return m_triggers.count(k) > 0;
  }

  // Read-only accessors for DebugUI
  const std::unordered_map<std::string, float>& getFloats() const { return m_floats; }
  const std::unordered_map<std::string, bool>& getBools() const { return m_bools; }
  const std::unordered_set<std::string>& getTriggers() const { return m_triggers; }

private:
  std::unordered_map<std::string, float> m_floats;
  std::unordered_map<std::string, bool> m_bools;
  std::unordered_set<std::string> m_triggers;
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
};

// ClipNode
// Leaf node. Wraps an AnimationClip, advances its own local time, samples
// bone transforms, fires TAE events through the event bus
// (inspo: HavokClipGenerator)

class ClipNode : public AnimGraphNode {
public:
  explicit ClipNode(std::shared_ptr<AnimationClip> clip, bool loop = true);

  void setOwnerName(const std::string& name) { m_ownerName = name; }

  // Restart from time 0
  void reset();

  void update(float dt, AnimParamTable& params) override;
  void evaluate(std::vector<glm::mat4>& outPose) const override;
  bool isFinished() const override;
  void setSkeleton(const std::vector<Bone>* skeleton) override { m_skeleton = skeleton; }

  std::string getDebugStateInfo() const override;

  float getActiveClipTime() const override { return m_time; }
  float getActiveClipDuration() const override { return m_clip ? m_clip->duration : 0.0f; }

  float getCurrentTime() const { return m_time; }
  float getDuration() const { return m_clip ? m_clip->duration : 0.0f; }
  const std::shared_ptr<AnimationClip>& getClip() const { return m_clip; }
private:
  void tickTAEEvents(float prevTime, float curTime);
  void deactivateAllEvents();

  std::shared_ptr<AnimationClip> m_clip;
  bool m_loop = true;
  float m_time = 0.0f;
  float m_prevTime = 0.0f;
  std::string m_ownerName;
  const std::vector<Bone>* m_skeleton = nullptr;

  mutable std::vector<glm::mat4> m_pose;

  std::unordered_set<int> m_activeEventIndices;
};

// Blend1DNode
// Reads a named float param and lineraly blends between two child nodes
// param == 0.0 -> fully m_from
// param == 1.0 -> fully m_to

class Blend1DNode : public AnimGraphNode {
public:
  explicit Blend1DNode(std::string paramName);

  void addEntry(float threshold, std::shared_ptr<AnimGraphNode> node);

  void update(float dt, AnimParamTable& params) override;
  void evaluate(std::vector<glm::mat4>& outPose) const  override;
  void setSkeleton(const std::vector<Bone>* skeleton) override;

  std::string getDebugStateInfo() const override;
  float getActiveClipTime() const override {
    if (m_lowerIdx < (int)m_entries.size() && m_entries[m_lowerIdx].node)
      return m_entries[m_lowerIdx].node->getActiveClipTime();
    return 0.0f;
  }

  float getActiveClipDuration() const override {
    if (m_lowerIdx < (int)m_entries.size() && m_entries[m_lowerIdx].node)
      return m_entries[m_lowerIdx].node->getActiveClipDuration();
    return 0.0f;
  }
  
private:
  struct Entry {
    float threshold = 0.0f;
    std::shared_ptr<AnimGraphNode> node;
  };

  std::string m_paramName;
  std::vector<Entry> m_entries; // sorted ascending by threshold


  float m_param = 0.0f;
  int m_lowerIdx = 0; // Index of lower bracketing entry
  float m_localAlpha = 0.0f; 
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

struct AnimTransition {
  std::string fromState;
  std::string toState;
  std::function<bool(AnimParamTable&)> condition;
  float blendTime = 0.2f;
};

class StateMachineNode : public AnimGraphNode {
public:
  void addState(const std::string& name, std::shared_ptr<AnimGraphNode> node);
  void addTransition(AnimTransition transition);
  void setInitialState(const std::string& name);

  void update(float dt, AnimParamTable& params) override;
  void evaluate(std::vector<glm::mat4>& outPose) const override;
  bool isFinished() const override;
  void setSkeleton(const std::vector<Bone>* skeleton) override;

  std::string getDebugStateInfo() const override;

  float getActiveClipTime() const override {
    auto it = m_states.find(m_currentState);
    if (it != m_states.end() && it->second)
      return it->second->getActiveClipTime();
    return 0.0f;
  }

  float getActiveClipDuration() const override {
    auto it = m_states.find(m_currentState);
    if (it != m_states.end() && it->second)
      return it->second->getActiveClipDuration();
    return 0.0f;
  }

  const std::string& getCurrentState() const { return m_currentState; }
private:
  void transitionTo(const std::string& toState, float blendTime, AnimParamTable& params);

  std::unordered_map<std::string, std::shared_ptr<AnimGraphNode>> m_states;
  std::vector<AnimTransition> m_transitions;

  std::string m_currentState;
  std::string m_previousState;

  // Crossfade
  float m_blendAlpha = 1.0f; // 1.0 = fully in current state
  float m_blendTime = 0.0f;
  float m_blendTimer = 0.0f;

  // Cachjed poses for blend
  mutable std::vector<glm::mat4> m_prevPose;
  mutable std::vector<glm::mat4> m_currPose;
};

}
