#pragma once

#include <forge/AnimGraph.h>

#include <memory>
#include <string>
#include <vector>

namespace forge::test {

class GraphBuilder {
public:
  int16_t clip(std::string actionKey = {}, bool loop = true);
  int16_t blend1D(std::string param, std::vector<float> thresholds,
                  std::vector<int16_t> children);
  int16_t blend2D(std::string xParam, std::string zParam,
                  std::vector<Blend2DNode::ClipEntry> clips);
  int16_t selector(std::string param, std::vector<ClipSelectorNode::Entry> entries);
  int16_t stateMachine(std::string initialState,
                       std::vector<StateMachineNode::StateDef> states,
                       std::vector<StateMachineNode::TransitionDef> transitions);

  GraphBuilder& root(int16_t nodeIndex) { m_def->m_rootNodeIndex = nodeIndex; return *this; }
  std::shared_ptr<GraphDefinition> build() const { return m_def; }

private:
  template <typename TDef>
  int16_t add(std::unique_ptr<TDef> def) {
    m_def->m_nodeTypeIDs.push_back(fnv1a(TDef::kTypeName));
    m_def->m_nodeDefs.push_back(std::move(def));
    return static_cast<int16_t>(m_def->m_nodeDefs.size() - 1);
  }

  std::shared_ptr<GraphDefinition> m_def = std::make_shared<GraphDefinition>();
};

template <typename TNode>
TNode& nodeAt(GraphInstance& inst, int16_t idx) {
  return *static_cast<TNode*>(inst.m_nodes.at(idx));
}

inline TransitionConditionDef onTrigger(std::string p) { return { ConditionType::TriggerConsumed, std::move(p) }; }
inline TransitionConditionDef whenTrue(std::string p) { return { ConditionType::BoolTrue, std::move(p) }; }
inline TransitionConditionDef whenFalse(std::string p) { return { ConditionType::BoolFalse, std::move(p) }; }
inline TransitionConditionDef whenFinished() { return { ConditionType::SourceNodeFinished, {} }; }

inline StateMachineNode::TransitionDef transition(std::string from, std::string to,
                                                  float blendTime,
                                                  std::vector<TransitionConditionDef> conditions,
                                                  bool requireAll = true) {
  StateMachineNode::TransitionDef t;
  t.fromState = std::move(from);
  t.toState = std::move(to);
  t.blendTime = blendTime;
  t.conditions = std::move(conditions);
  t.requireAll = requireAll;
  return t;
}

}
