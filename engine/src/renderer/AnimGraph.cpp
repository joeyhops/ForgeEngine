#include <forge/AnimGraph.h>
#include <forge/Events.h>
#include <forge/EventBus.h>
#include <forge/Logger.h>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <algorithm>

namespace forge {

float AnimParamTable::getFloat(const std::string& k, float def) const {
  auto it = m_floats.find(k);
  return (it != m_floats.end()) ? it->second : def;
}

bool AnimParamTable::getBool(const std::string& k, bool def) const {
  auto it = m_bools.find(k);
  return (it != m_bools.end()) ? it->second : def;
}
  
bool AnimParamTable::consumeTrigger(const std::string& k) {
  auto it = m_triggers.find(k);
  if (it == m_triggers.end()) return false;
  m_triggers.erase(it);
  return true;
}

ClipNode::ClipNode(std::shared_ptr<AnimationClip> clip, bool loop) 
  : m_clip(std::move(clip)), m_loop(loop)
{}

void ClipNode::reset() {
  deactivateAllEvents();
  m_time = 0.0f;
  m_prevTime = 0.0f;
  m_activeEventIndices.clear();
}

void ClipNode::update(float dt, AnimParamTable& params) {
  if (!m_clip) return;

  m_prevTime = m_time;
  m_time += dt;

  if (m_loop && m_time >= m_clip->duration) {
    deactivateAllEvents();
    m_time = fmodf(m_time, m_clip->duration);
    m_prevTime = 0.0f;
  } else if (!m_loop && m_time >= m_clip->duration) {
    m_time = m_clip->duration;
  }

  tickTAEEvents(m_prevTime, m_time);

  // cache pose so evaluate can return it without resampling
  if (m_skeleton && !m_skeleton->empty()) {
    m_clip->sample(m_time, *m_skeleton, m_pose);
  }
}

void ClipNode::evaluate(std::vector<glm::mat4>& outPose) const {
  if (m_pose.empty() && m_skeleton && m_clip) {
    m_clip->sample(m_time, *m_skeleton, m_pose);
  }
  outPose = m_pose;
}

bool ClipNode::isFinished() const {
  if (!m_clip || m_loop) return false;
  return m_time >= m_clip->duration;
}

std::string ClipNode::getDebugStateInfo() const {
  if (!m_clip) return "ClipNode(null)";
  return m_clip->name + " [" + std::to_string(m_time).substr(0, 4) + "s]";
}

void ClipNode::tickTAEEvents(float prevTime, float curTime) {
  if (!m_clip) return;
  const auto& events = m_clip->events;

  for (int i = 0; i < (int)events.size(); i++) {
    const AnimEvent& ev = events[i];
    bool wasActive = m_activeEventIndices.count(i) > 0;

    if (ev.startTime == ev.endTime) {
      // One-shot: fire both activated + deactived on first crossing
      if (!wasActive && prevTime <= ev.startTime && curTime > ev.startTime) {
        EventBus::publish(AnimEventActivated{ m_ownerName, ev.type, ev.payload });
        EventBus::publish(AnimEventDeactivated{ m_ownerName, ev.type, ev.payload });
        m_activeEventIndices.insert(i);
      }
    } else {
      bool shouldBeActive = (curTime >= ev.startTime && curTime < ev.endTime);
      if (!wasActive && shouldBeActive) {
        m_activeEventIndices.insert(i);
        EventBus::publish(AnimEventActivated{ m_ownerName, ev.type, ev.payload });
      } else if (wasActive && !shouldBeActive) {
        m_activeEventIndices.erase(i);
        EventBus::publish(AnimEventDeactivated{ m_ownerName, ev.type, ev.payload });
      }
    }
  }
}

void ClipNode::deactivateAllEvents() {
  if (!m_clip) return;
  const auto& events = m_clip->events;
  for (int i : m_activeEventIndices) {
    if (i < (int)events.size())
        EventBus::publish(AnimEventDeactivated{ m_ownerName, events[i].type, events[i].payload });
  }
  m_activeEventIndices.clear();
}

Blend1DNode::Blend1DNode(std::string paramName)
  : m_paramName(std::move(paramName))
{}

void Blend1DNode::addEntry(float threshold, std::shared_ptr<AnimGraphNode> node) {
  m_entries.push_back({ threshold, std::move(node) });
  std::sort(m_entries.begin(), m_entries.end(),
            [](const Entry& a, const Entry& b){ return a.threshold < b.threshold; });
}

void Blend1DNode::setSkeleton(const std::vector<Bone>* skeleton) {
  for (auto& e : m_entries)
    if (e.node) e.node->setSkeleton(skeleton);
}

void Blend1DNode::update(float dt, AnimParamTable& params) {
  if (m_entries.empty()) return;

  m_param = params.getFloat(m_paramName, 0.0f);

  if (m_entries.size() == 1) {
    m_lowerIdx = 0;
    m_localAlpha = 0.0f;
    if (m_entries[0].node) m_entries[0].node->update(dt, params);
    return;
  }

  // Clamp param to declared range and find bracketing entries
  if (m_param <= m_entries.front().threshold) {
    m_lowerIdx = 0;
    m_localAlpha = 0.0f;
  } else if (m_param >= m_entries.back().threshold) {
    m_lowerIdx = static_cast<int>(m_entries.size()) - 2;
    m_localAlpha = 1.0f;
  } else {
    m_lowerIdx = 0;
    for (int i = 0; i < static_cast<int>(m_entries.size()) - 1; ++i) {
      if (m_param >= m_entries[i].threshold && m_param < m_entries[i + 1].threshold) {
        m_lowerIdx = i;
        break;
      }
    }
    float range = m_entries[m_lowerIdx + 1].threshold - m_entries[m_lowerIdx].threshold;
    m_localAlpha = (range > 0.0f)
      ? (m_param - m_entries[m_lowerIdx].threshold) / range
      : 0.0f;
  }

  if (m_entries[m_lowerIdx].node)
    m_entries[m_lowerIdx].node->update(dt, params);

  int upperIdx = m_lowerIdx + 1;
  if (upperIdx < static_cast<int>(m_entries.size()) && m_entries[upperIdx].node)
    m_entries[upperIdx].node->update(dt, params);
}

void Blend1DNode::evaluate(std::vector<glm::mat4>& outPose) const {
  if (m_entries.empty()) return;

  if (m_entries.size() == 1) {
    if (m_entries[0].node) m_entries[0].node->evaluate(outPose);
    return;
  }

  const auto& lower = m_entries[m_lowerIdx];
  const auto& upper = m_entries[m_lowerIdx + 1];

  if (!lower.node && !upper.node) return;
  if (!lower.node) {
    upper.node->evaluate(outPose);
    return;
  }
  if (!upper.node) {
    lower.node->evaluate(outPose);
    return;
  }

  // Fully at lower bound
  if (m_localAlpha <= 0.0f) {
    lower.node->evaluate(outPose);
    return;
  }
  // Fully at upper bound
  if (m_localAlpha >= 1.0f) {
    upper.node->evaluate(outPose);
    return;
  }

  std::vector<glm::mat4> fromPose, toPose;
  lower.node->evaluate(fromPose);
  upper.node->evaluate(toPose);

  size_t count = std::min(fromPose.size(), toPose.size());
  outPose.resize(count);
  for (size_t i = 0; i < count; ++i)
    outPose[i] = fromPose[i] * (1.0f - m_localAlpha) + toPose[i] * m_localAlpha;
}

std::string Blend1DNode::getDebugStateInfo() const {
  std::string lower = (m_lowerIdx < (int)m_entries.size())
    ? (m_entries[m_lowerIdx].node ? m_entries[m_lowerIdx].node->getDebugStateInfo() : "?")
    : "?";
  std::string upper = (m_lowerIdx + 1 < (int)m_entries.size())
    ? (m_entries[m_lowerIdx + 1].node ? m_entries[m_lowerIdx + 1].node->getDebugStateInfo() : "?")
    : "?";
  int pct = static_cast<int>(m_localAlpha * 100.0f);
  return "Blend1D(" + m_paramName + "=" + std::to_string(m_param).substr(0, 4) + " "
          + lower + "->" + upper + " " + std::to_string(pct) + "%)";
}

void StateMachineNode::addState(const std::string& name, std::shared_ptr<AnimGraphNode> node) {
  m_states[name] = std::move(node);
}

void StateMachineNode::addTransition(AnimTransition transition) {
  m_transitions.push_back(std::move(transition));
}

void StateMachineNode::setInitialState(const std::string& name) {
  m_currentState = name;
  m_blendAlpha = 1.0f;
  LOG_INFO("[AnimGraph] Initial state: '{}'", name);
}

void StateMachineNode::setSkeleton(const std::vector<Bone>* skeleton) {
  for (auto& [name, node] : m_states)
    if (node) node->setSkeleton(skeleton);
}

void StateMachineNode::transitionTo(const std::string& toState, float blendTime, AnimParamTable& params) {
  if (m_states.find(toState) == m_states.end()) {
    LOG_WARN("[AnimGraph] Transition to unknown state '{}'", toState);
    return;
  }

  LOG_INFO("[AnimGraph] {} -> {}", m_currentState, toState);

  m_previousState = m_currentState;
  m_currentState = toState;
  m_blendTime = blendTime;
  m_blendTimer = 0.0f;
  m_blendAlpha = (blendTime > 0.0f) ? 0.0f : 1.0f;

  // Reset incoming node so it plays from beginning
  auto& incomingNode = m_states[m_currentState];
  if (incomingNode) {
    if (auto* clip = dynamic_cast<ClipNode*>(incomingNode.get()))
      clip->reset();
  }
}

void StateMachineNode::update(float dt, AnimParamTable& params) {
  if (m_currentState.empty()) return;

  // Advance cross fade
  if (m_blendAlpha < 1.0f && m_blendTime > 0.0f) {
    m_blendTimer += dt;
    m_blendAlpha = std::min(m_blendTimer / m_blendTime, 1.0f);
  }

  if (m_blendAlpha < 1.0f && !m_previousState.empty()) {
    auto it = m_states.find(m_previousState);
    if (it != m_states.end() && it->second)
      it->second->update(dt, params);
  }

  auto it = m_states.find(m_currentState);
  if (it != m_states.end() && it->second)
    it->second->update(dt, params);

  auto tryTransitions = [&](bool anyOnly){
    for (auto& t : m_transitions) {
      bool isAny = t.fromState.empty();
      if (isAny != anyOnly) continue;
      if (!isAny && t.fromState != m_currentState) continue;
      if (t.toState == m_currentState) continue;
      if (t.condition && t.condition(params)) {
        transitionTo(t.toState, t.blendTime, params);
        return true;
      }
    }
    return false;
  };

  if (!tryTransitions(false))
    tryTransitions(true);
}

void StateMachineNode::evaluate(std::vector<glm::mat4>& outPose) const {
  if (m_currentState.empty()) return;

  auto it = m_states.find(m_currentState);
  if (it == m_states.end() || !it->second) return;

  if (m_blendAlpha >= 1.0f || m_previousState.empty()) {
    it->second->evaluate(outPose);
    return;
  }

  auto prevIt = m_states.find(m_previousState);
  if (prevIt == m_states.end() || !prevIt->second) {
    it->second->evaluate(outPose);
    return;
  }

  prevIt->second->evaluate(m_prevPose);
  it->second->evaluate(m_currPose);

  size_t count = std::min(m_prevPose.size(), m_currPose.size());
  outPose.resize(count);
  for (size_t i = 0; i < count; i++)
    outPose[i] = m_prevPose[i] * (1.0f - m_blendAlpha) + m_currPose[i] * m_blendAlpha;
}

bool StateMachineNode::isFinished() const {
  auto it = m_states.find(m_currentState);
  if (it == m_states.end() || !it->second) return false;
  return it->second->isFinished();
}

std::string StateMachineNode::getDebugStateInfo() const {
  if (m_blendAlpha < 1.0f && !m_previousState.empty())
    return m_previousState + " -> " + m_currentState + " ("
      + std::to_string((int)(m_blendAlpha * 100)) + "%)";
  auto it = m_states.find(m_currentState);
  if (it != m_states.end() && it->second)
    return m_currentState + " | " + it->second->getDebugStateInfo();
  return m_currentState;
}

}
