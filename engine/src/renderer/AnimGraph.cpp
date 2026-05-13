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

int AnimParamTable::getInt(const std::string& k, int def) const {
  auto it = m_ints.find(k);
  return (it != m_ints.end()) ? it->second : def;
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

  // Extract root delta and apply per-clip y mask
  m_rootDelta = m_clip->sampleRootDelta(m_prevTime, m_time);
  if (!m_clip->extractRootY)
    m_rootDelta.y = 0.0f;
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

void ClipNode::setActiveTime(float t) {
  if (!m_clip) return;
  m_time = std::clamp(t, 0.0f, m_clip->duration);
  m_prevTime = m_time;
  m_activeEventIndices.clear();
  m_pose.clear();
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
    if (i >= (int)events.size()) continue;
    const auto& ev = events[i];
    if (ev.startTime == ev.endTime) continue;
    EventBus::publish(AnimEventDeactivated{ m_ownerName, ev.type, ev.payload });
  }
  m_activeEventIndices.clear();
}

void ClipNode::setClip(std::shared_ptr<AnimationClip> clip) {
  m_clip = std::move(clip);
  m_time = 0.0f;
  m_prevTime = 0.0f;
  deactivateAllEvents();
}

void ClipNode::swapClipByKey(const std::string& key, std::shared_ptr<AnimationClip> clip) {
  if (m_actionKey == key)
    setClip(std::move(clip));
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

void Blend1DNode::setActiveTime(float t) {
  for (auto& entry : m_entries)
    if (entry.node) entry.node->setActiveTime(t);
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

  glm::vec3 lowerDelta = m_entries[m_lowerIdx].node
    ? m_entries[m_lowerIdx].node->getRootMotionDelta()
    : glm::vec3(0.0f);

  glm::vec3 upperDelta = (upperIdx < static_cast<int>(m_entries.size()) && m_entries[upperIdx].node)
    ? m_entries[upperIdx].node->getRootMotionDelta()
    : glm::vec3(0.0f);

  m_rootDelta = glm::mix(lowerDelta, upperDelta, m_localAlpha);
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

void Blend1DNode::swapClipByKey(const std::string& key, std::shared_ptr<AnimationClip> clip) {
  for (auto& entry : m_entries) {
    if (entry.node)
      entry.node->swapClipByKey(key, clip);
  }
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

void StateMachineNode::setActiveTime(float t) {
  auto it = m_states.find(m_currentState);
  if (it != m_states.end() && it->second)
    it->second->setActiveTime(t);
}

glm::vec3 StateMachineNode::getRootMotionDelta() const {
  auto it = m_states.find(m_currentState);
  if (it == m_states.end() || !it->second) return glm::vec3(0.0f);

  glm::vec3 currDelta = it->second->getRootMotionDelta();

  if (m_blendAlpha >= 1.0f || m_previousState.empty())
    return currDelta;

  auto prevIt = m_states.find(m_previousState);
  glm::vec3 prevDelta = (prevIt != m_states.end() && prevIt->second)
    ? prevIt->second->getRootMotionDelta()
    : glm::vec3(0.0f);

  return glm::mix(prevDelta, currDelta, m_blendAlpha);
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

void StateMachineNode::swapClipByKey(const std::string& key, std::shared_ptr<AnimationClip> clip) {
  for (auto& [name, node] : m_states) {
    if (node)
      node->swapClipByKey(key, clip);
  }
} 

Blend2DNode::Blend2DNode(std::string xParam, std::string zParam)
  : m_xParam(std::move(xParam)), m_zParam(std::move(zParam)) {}

void Blend2DNode::addClip(glm::vec2 position,
                          std::shared_ptr<AnimationClip> clip) {
  auto node = std::make_shared<ClipNode>(std::move(clip), true);
  m_clips.push_back({ position, std::move(node) });
}

void Blend2DNode::build() {
  m_triangles.clear();
  m_hullEdges.clear();
  if (m_clips.size() < 3) return; // degenerate: handle in update()
  
  // step 1 - Bowyer-Watson incremental Delaunay triangulations
  //
  // 1. create super-triangle of all input points
  // 2. for each input point P:
  //    a. find all triangles whose circumcircle contains P ("bad" triangles)
  //    b. Form a polygonal hole by collecting the boundary edges of said triangles
  //      (edges appearing in exactly one bad triangle)
  //    c. Delete the bad triangles
  //    d. Connect P to each edge of the hole, producing new triangles
  // 3. Discard any triangle that uses a super-triangle vertex
  //
  // An edge is on the convex hull if it appears in exactly one
  // triangle. Build a count map keyed by sorted vertex index pair,
  // walk each triangles three edges, increment the count. Any entry
  // with count == 1 is a hull edge; we also remember which triangle it
  // came from (for the nearest edge clamp in update())

  struct EdgeKey { int a, b; bool operator==(const EdgeKey& o) const {
    return a == o.a && b == o.b;
  } };
  struct EdgeHash { size_t operator()(const EdgeKey& k) const {
    return std::hash<int>()(k.a) ^ (std::hash<int>()(k.b) << 1);
  } };

  auto makeKey = [](int u, int v) -> EdgeKey {
    return (u < v) ? EdgeKey{u, v} : EdgeKey{v, u};
  };

  // edge -> (count, last-seen triangle index)
  std::unordered_map<EdgeKey, std::pair<int, int>, EdgeHash> edges;
  for (size_t t = 0; t < m_triangles.size(); ++t) {
    const auto& tri = m_triangles[t];
    int e[3][2] = { {tri.a, tri.b}, {tri.b, tri.c}, {tri.c, tri.a} };
    for (int i = 0; i < 3; i++) {
      EdgeKey k = makeKey(e[i][0], e[i][1]);
      auto& slot = edges[k];
      slot.first++;
      slot.second = static_cast<int>(t);
    }
  }
  for (const auto& [key, val] : edges) {
    if (val.first == 1)
      m_hullEdges.push_back({ key.a, key.b, val.second });
  }
}

static glm::vec3 barycentric(glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
  glm::vec2 v0 = b - a, v1 = c - a, v2 = p - a;
  float d00 = glm::dot(v0, v0);
  float d01 = glm::dot(v0, v1);
  float d11 = glm::dot(v1, v1);
  float d20 = glm::dot(v2, v0);
  float d21 = glm::dot(v2, v1);
  float denom = d00 * d11 - d01 * d01;
  if (std::abs(denom) < 1e-9f) return glm::vec3(1.0f, 0.0f, 0.0f);
  float v = (d11 * d20 - d01 * d21) / denom;
  float w = (d00 * d21 - d01 * d20) / denom;
  float u = 1.0f - v - w;
  return glm::vec3(u, v, w); // u for a, v for b, w for c
}

void Blend2DNode::update(float dt, AnimParamTable& params) {
  if (m_clips.empty()) return;

  m_currentParam.x = params.getFloat(m_xParam, 0.0f);
  m_currentParam.y = params.getFloat(m_zParam, 0.0f);

  // Advance ALL child clips every frame for loop sync
  for (auto& bc : m_clips)
    if (bc.node) bc.node->update(dt, params);

  if (m_clips.size() == 1 || m_triangles.empty()) {
    m_idxA = m_idxB = m_idxC = 0;
    m_weights = glm::vec3(1.0f, 0.0f, 0.0f);
    m_rootDelta = m_clips[0].node->getRootMotionDelta();
    return;
  }

  // locate param in triangulation
  // current linear search is ideally fast for small triangle counts
  // potential to replace with BSP/kd-tree over triangle AABBs
  int found = -1;
  glm::vec3 bary(0.0f);
  for (size_t t = 0; t < m_triangles.size(); ++t) {
    const auto& tri = m_triangles[t];
    bary = barycentric(m_currentParam,
                       m_clips[tri.a].position,
                       m_clips[tri.b].position,
                       m_clips[tri.c].position);
    if (bary.x >= -1e-4f && bary.y >= -1e-4f && bary.z >= -1e-4f) {
      found = static_cast<int>(t);
      break;
    }
  }

  if (found < 0) {
    // Outside hull edge: clamp nearest hull edge
    //
    // 1. for each hull edge, compute the closest point on that
    // edge segment to m_currentParam. Track global min.
    // 2. Use adjacent triangle of winning edge as the
    // clamped point in that triangle (one barycentric will
    // be ~0 - the vertex of the triangle that does not belong
    // to the hulls edge - and the other two will sum to ~1)

    glm::vec2 bestPoint(m_clips[0].position);
    float bestDist2 = std::numeric_limits<float>::max();
    int bestTri = 0;
    for (const auto& he : m_hullEdges) {
      glm::vec2 a = m_clips[he.u].position;
      glm::vec2 b = m_clips[he.v].position;
      glm::vec2 ab = b - a;
      float ab2 = glm::dot(ab, ab);
      float t = (ab2 > 1e-9f)
        ? glm::clamp(glm::dot(m_currentParam - a, ab) / ab2, 0.0f, 1.0f)
        : 0.0f;
      glm::vec2 q = a + t * ab;
      float d2 = glm::length2(q - m_currentParam);
      if (d2 < bestDist2) {
        bestDist2 = d2;
        bestPoint = q;
        bestTri = he.tri;
      }
    }

    const auto& tri = m_triangles[bestTri];
    bary = barycentric(bestPoint,
                       m_clips[tri.a].position,
                       m_clips[tri.b].position,
                       m_clips[tri.c].position);
    found = bestTri;
  }

  const auto& tri = m_triangles[found];
  m_idxA = tri.a; m_idxB = tri.b; m_idxC = tri.c;
  m_weights = bary;

  m_rootDelta =
      bary.x * m_clips[m_idxA].node->getRootMotionDelta()
    + bary.y * m_clips[m_idxB].node->getRootMotionDelta()
    + bary.z * m_clips[m_idxC].node->getRootMotionDelta();
}

void Blend2DNode::evaluate(std::vector<glm::mat4>& outPose) const {
  if (m_clips.empty()) return;

  if (m_triangles.empty() || m_clips.size() == 1) {
    m_clips[0].node->evaluate(outPose);
  }

  std::vector<glm::mat4> poseA, poseB, poseC;
  m_clips[m_idxA].node->evaluate(poseA);
  m_clips[m_idxB].node->evaluate(poseB);
  m_clips[m_idxC].node->evaluate(poseC);

  size_t n = std::min({ poseA.size(), poseB.size(), poseC.size() });
  outPose.resize(n);
  for (size_t i = 0; i < n; ++i) {
    outPose[i] = poseA[i] * m_weights.x
               + poseB[i] * m_weights.y
               + poseC[i] * m_weights.z;
  }
}

void Blend2DNode::setSkeleton(const std::vector<Bone>* skeleton) {
  for (auto& bc : m_clips)
    if (bc.node) bc.node->setSkeleton(skeleton);
}

void Blend2DNode::swapClipByKey(const std::string& key,
                                std::shared_ptr<AnimationClip> clip) {
  for (auto& bc : m_clips)
    if (bc.node) bc.node->swapClipByKey(key, clip);
}

void Blend2DNode::setActiveTime(float t) {
  for (auto& bc : m_clips)
    if (bc.node) bc.node->setActiveTime(t);
}

float Blend2DNode::getActiveClipTime() const {
  return m_clips.empty() ? 0.0f : m_clips[m_idxA].node->getActiveClipTime();
}

float Blend2DNode::getActiveClipDuration() const {
  return m_clips.empty() ? 0.0f : m_clips[m_idxA].node->getActiveClipDuration();
}

std::string Blend2DNode::getDebugStateInfo() const {
  if (m_clips.empty()) return "Blend2D(empty)";
  return "Blend2D(" + m_xParam + "," + m_zParam + " @ "
        + std::to_string(m_currentParam.x).substr(0, 4) + ","
        + std::to_string(m_currentParam.y).substr(0, 4) + ")";
}

}
