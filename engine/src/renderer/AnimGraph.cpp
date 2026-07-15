#include <forge/AnimGraph.h>
#include <forge/AssetManager.h>
#include <forge/Events.h>
#include <forge/EventBus.h>
#include <forge/TypeSystem.h>
#include <forge/TaskSystem.h>
#include <forge/Logger.h>

#ifdef FORGE_DEBUG
#include <forge/RootMotionDebugger.h>
#endif

#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <fstream>

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

void ClipNode::reset() {
  m_time = 0.0f;
  m_prevTime = 0.0f;
}

UpdateResult ClipNode::update(const UpdateContext& ctx) {
  m_prevTime = m_time;
  m_time += ctx.dt;

  if (m_pDef->loop && m_clip && m_time >= m_clip->duration) {
    m_time = fmodf(m_time, m_clip->duration);
    m_prevTime = 0.0f;
  }

  SampledEventRange evRange = tickTAEEvents(ctx, m_prevTime, m_time);

  // Root motion: math stays same but returned instead of cached
  glm::vec3 rootDelta(0.0f);
  if (m_clip) {
    rootDelta = m_clip->sampleRootDelta(m_prevTime, m_time);
    if (!m_clip->extractRootY) rootDelta.y = 0.0f;
  }

  TaskIndex idx = k_invalidTask;
  if (m_clip) {
    idx = ctx.taskSystem.RegisterTask<SampleTask>(TaskPhase::PrePhysics, m_clip.get(), m_time, ctx.skeleton);
  }

#ifdef FORGE_DEBUG
  if (ctx.debugger) ctx.debugger->Record(this, "ClipNode", rootDelta, 1.0f);
#endif

  return UpdateResult{ idx, rootDelta, evRange };
}

UpdateResult ClipNode::update(const UpdateContext& ctx, SyncTrackTimeRange range) {
  // fall back to dt if no usable range or clip is not sync-authored
  if (!range.valid || !m_clip || m_clip->syncTrack.empty())
    return update(ctx);

  m_prevTime = m_time;

  const float dur = m_clip->duration;
  const float endPct = m_clip->syncTrack.SyncPosToPercentage(range.endSyncPos);
  m_time = endPct * dur;

  // detect loop wrap: parents sync clock wrapped so endSyncPos < startSyncPos
  const bool wrapped = range.endSyncPos < range.startSyncPos;
  if (wrapped) m_prevTime = 0.0f;

  SampledEventRange evRange = tickTAEEvents(ctx, m_prevTime, m_time);

  glm::vec3 rootDelta(0.0f);
  if (m_clip) {
    rootDelta = m_clip->sampleRootDelta(m_prevTime, m_time);
    if (!m_clip->extractRootY) rootDelta.y = 0.0f;
  }

  TaskIndex idx = ctx.taskSystem.RegisterTask<SampleTask>(TaskPhase::PrePhysics, m_clip.get(), m_time, ctx.skeleton);

#ifdef FORGE_DEBUG
  if (ctx.debugger) ctx.debugger->Record(this, "ClipNode(sync)", rootDelta, 1.0f);
#endif
  return UpdateResult{ idx, rootDelta, evRange };
}

const AnimationClip* ClipNode::getActiveClip() const { return m_clip.get(); }

bool ClipNode::isFinished() const {
  if (!m_clip || m_pDef->loop) return false;
  return m_time >= m_clip->duration;
}

void ClipNode::setActiveTime(float t) {
  if (!m_clip) return;
  m_time = std::clamp(t, 0.0f, m_clip->duration);
  m_prevTime = m_time;
}

std::string ClipNode::getDebugStateInfo() const {
  if (!m_clip) return "ClipNode(null)";
  return m_clip->name + " [" + std::to_string(m_time).substr(0, 4) + "s]";
}

SampledEventRange ClipNode::tickTAEEvents(const UpdateContext& ctx, float prevTime, float curTime) {
  SampledEventRange range{ ctx.events.CurrentSize(), ctx.events.CurrentSize() };
  if (!m_clip) return range;
  const auto& events = m_clip->events;
  const float dur = m_clip->duration > 1e-6f ? m_clip->duration : 1.0f;

  for (int i = 0; i < (int)events.size(); i++) {
    const AnimEvent& ev = events[i];
    const bool oneShot = (ev.startTime == ev.endTime);
    const bool active = oneShot
        ? (prevTime <= ev.startTime && curTime > ev.startTime) // crossed this frame
        : (curTime >= ev.startTime && curTime < ev.endTime); // window contains now
    if (!active) continue;

    SampledEvent se;
    se.type = ev.type;
    se.startPct = curTime / dur;
    se.weight = 1.0f; // lead weight; blend nodes scale as recursion unwinds
    se.payload = ev.payload;
    se.capsules = ev.capsules;
    se.source = SampledEvent::Source::Clip;
    se.flags = SampledEvent::Flags::FromActiveBranch;
    se.sourceNodeIdx = m_pDef ? m_pDef->debugNodeIdx : -1;
    ctx.events.Append(std::move(se));
  }

  range.end = ctx.events.CurrentSize();
  return range;
}

void ClipNode::setClip(std::shared_ptr<AnimationClip> clip) {
  m_clip = std::move(clip);
  m_time = 0.0f;
  m_prevTime = 0.0f;
}

void ClipNode::swapClipByKey(const std::string& key, std::shared_ptr<AnimationClip> clip) {
  if (m_pDef->actionKey == key)
    setClip(std::move(clip));
}

void Blend1DNode::setSkeleton(const std::vector<Bone>* skeleton) {
  for (auto& e : m_children)
    if (e) e->setSkeleton(skeleton);
}

void Blend1DNode::setActiveTime(float t) {
  m_syncPct = 0.0f;
  m_prevSyncPct = 0.0f;
  for (auto& entry : m_children)
    if (entry) entry->setActiveTime(t);
}

void Blend1DNode::resolveBracket() {
  const auto& thresholds = m_pDef->thresholds;
  // Clamp param to declared range and find bracketing entries
  if (m_param <= thresholds.front()) {
    m_lowerIdx = 0;
    m_localAlpha = 0.0f;
  } else if (m_param >= thresholds.back()) {
    m_lowerIdx = (int)thresholds.size() - 2;
    m_localAlpha = 1.0f;
  } else {
    m_lowerIdx = 0;
    for (int i = 0; i < (int)thresholds.size() - 1; ++i) {
      if (m_param >= thresholds[i] && m_param < thresholds[i + 1]) {
        m_lowerIdx = i;
        break;
      }
    }
    float range = thresholds[m_lowerIdx + 1] - thresholds[m_lowerIdx];
    m_localAlpha = (range > 0.0f)
      ? (m_param - thresholds[m_lowerIdx]) / range
      : 0.0f;
  }
}

SyncTrackTimeRange Blend1DNode::computeSyncRange(const UpdateContext& ctx,
                                                 AnimGraphNode* lower, AnimGraphNode* upper,
                                                 float alpha) {
  SyncTrackTimeRange range;

  const AnimationClip* lc = clipOf(lower);
  const AnimationClip* uc = clipOf(upper);
  if (!lc || !uc || lc->syncTrack.empty() || uc->syncTrack.empty()
          || !lc->syncTrack.CompatibleWith(uc->syncTrack)) {
    return range; // dt fallback
  }

  const SyncTrack blended = SyncTrack::Blend(lc->syncTrack, uc->syncTrack, alpha);
  const float blendedDur = glm::mix(lc->duration, uc->duration, alpha);
  const float advance = blendedDur > 1e-6f ? ctx.dt / blendedDur : 0.0f;

  m_prevSyncPct = m_syncPct;
  m_syncPct += advance;
  const bool wrapped = m_syncPct >= 1.0f;
  if (wrapped) m_syncPct -= std::floor(m_syncPct);

  range.startSyncPos = blended.PercentageToSyncPos(m_prevSyncPct);
  range.endSyncPos = blended.PercentageToSyncPos(m_syncPct);
  if (wrapped) range.endSyncPos += blended.count(); // keep monotone across wrap for child
  range.valid = true;
  return range;
}

const AnimationClip* Blend1DNode::clipOf(AnimGraphNode* n) {
  return n ? n->getActiveClip() : nullptr;
}

const AnimationClip* Blend1DNode::getActiveClip() const {
  if (m_children.empty()) return nullptr;
  const int idx = (m_localAlpha < 0.5f) ? m_lowerIdx : m_lowerIdx + 1;
  return (idx >= 0 && idx < (int)m_children.size() && m_children[idx])
        ? m_children[idx]->getActiveClip()
        : nullptr;
}

UpdateResult Blend1DNode::update(const UpdateContext& ctx) {
  if (!m_pDef || m_children.empty()) return {};
  m_param = ctx.params.getFloat(m_pDef->paramName, 0.0f);

  if (m_children.size() == 1) {
    m_lowerIdx = 0;
    m_localAlpha = 0.0f;
    return m_children[0] ? m_children[0]->update(ctx) : UpdateResult{};
  }

  resolveBracket(); // sets m_lowerIdx + m_localAlpha

  AnimGraphNode* lower = m_children[m_lowerIdx];
  const int upperIdx = m_lowerIdx + 1;
  AnimGraphNode* upper = (upperIdx < (int)m_children.size()) ? m_children[upperIdx] : nullptr;

  SyncTrackTimeRange range = computeSyncRange(ctx, lower, upper, m_localAlpha);

  // advance BOTH bracketing children each frame - preserves loop-sync exactly
  // as previous update() did. A non-contributing child still registers a
  // SampleTask, but the root never references it so pass 2 skips it
  UpdateResult a = lower ? lower->update(ctx, range) : UpdateResult{};
  UpdateResult b = upper ? upper->update(ctx, range) : UpdateResult{};

  // Endpoint passthrough (was the alpha<=0/alpha>=1 early outs in evaluate())
  // no blendtask just return the contributing childs result
  if (!upper || m_localAlpha <= 0.0f) return a;
  if (!lower || m_localAlpha >= 1.0f) return b;

  // Scale each childs sampled events by its contribution as recursion unwinds
  ctx.events.ScaleWeights(a.eventRange.begin, a.eventRange.end, 1.0f - m_localAlpha);
  ctx.events.ScaleWeights(b.eventRange.begin, b.eventRange.end, m_localAlpha);

  TaskIndex idx = ctx.taskSystem.RegisterTask<BlendTask>(TaskPhase::PrePhysics, a.taskIdx, b.taskIdx, m_localAlpha);
  glm::vec3 rootDelta = glm::mix(a.rootMotion, b.rootMotion, m_localAlpha);

#ifdef FORGE_DEBUG
  if (ctx.debugger) ctx.debugger->Record(this, "Blend1D", rootDelta, 1.0f);
#endif
  // The two childrens ranges are contiguous, so merged range is [a.begin, b.end]
  return UpdateResult{ idx, rootDelta, SampledEventRange{ a.eventRange.begin, b.eventRange.end } };
}

UpdateResult Blend1DNode::update(const UpdateContext& ctx, SyncTrackTimeRange parentRange) {
  // no usable parent range? fall back to using dt as our own clock
  if (!parentRange.valid) return update(ctx);
  if (!m_pDef || m_children.empty()) return {};

  m_param = ctx.params.getFloat(m_pDef->paramName, 0.0f);

  if (m_children.size() == 1) {
    m_lowerIdx = 0;
    m_localAlpha = 0.0f;
    return m_children[0] ? m_children[0]->update(ctx, parentRange) : UpdateResult{};
  }

  resolveBracket();
  AnimGraphNode* lower = m_children[m_lowerIdx];
  const int upperIdx = m_lowerIdx + 1;
  AnimGraphNode* upper = (upperIdx < (int)m_children.size()) ? m_children[upperIdx] : nullptr;

  // Keep our own normalized clock in step with parents for reporting
  // Sync position is a shared coordinate so we adopt the parents end pos
  // (folded back into [0, count) via our blended track) rather than
  // advancing by dt
  const AnimationClip* lc = clipOf(lower);
  const AnimationClip* uc = clipOf(upper);
  if (lc && uc && !lc->syncTrack.empty() && !uc->syncTrack.empty()
          && lc->syncTrack.CompatibleWith(uc->syncTrack)) {
    const SyncTrack blended = SyncTrack::Blend(lc->syncTrack, uc->syncTrack, m_localAlpha);
    m_prevSyncPct = m_syncPct;
    m_syncPct = blended.SyncPosToPercentage(parentRange.endSyncPos);
  }

  // Fwd parents range directly; each child resolves against its own track
  UpdateResult a = lower ? lower->update(ctx, parentRange) : UpdateResult{};
  UpdateResult b = upper ? upper->update(ctx, parentRange) : UpdateResult{};

  if (!upper || m_localAlpha <= 0.0f) return a;
  if (!lower || m_localAlpha >= 1.0f) return b;

  ctx.events.ScaleWeights(a.eventRange.begin, a.eventRange.end, 1.0f - m_localAlpha);
  ctx.events.ScaleWeights(b.eventRange.begin, b.eventRange.end, m_localAlpha);

  TaskIndex idx = ctx.taskSystem.RegisterTask<BlendTask>(TaskPhase::PrePhysics, a.taskIdx,
                                                         b.taskIdx, m_localAlpha);
  glm::vec3 rootDelta = glm::mix(a.rootMotion, b.rootMotion, m_localAlpha);

#ifdef FORGE_DEBUG
  if (ctx.debugger) ctx.debugger->Record(this, "Blend1D(sync)", rootDelta, 1.0f);
#endif
return UpdateResult{ idx, rootDelta, SampledEventRange{ a.eventRange.begin, b.eventRange.end }};
}

std::string Blend1DNode::getDebugStateInfo() const {
  std::string lower = (m_lowerIdx < (int)m_children.size())
    ? (m_children[m_lowerIdx] ? m_children[m_lowerIdx]->getDebugStateInfo() : "?")
    : "?";
  std::string upper = (m_lowerIdx + 1 < (int)m_children.size())
    ? (m_children[m_lowerIdx + 1] ? m_children[m_lowerIdx + 1]->getDebugStateInfo() : "?")
    : "?";
  int pct = static_cast<int>(m_localAlpha * 100.0f);
  return "Blend1D(" + m_pDef->paramName + "=" + std::to_string(m_param).substr(0, 4) + " "
          + lower + "->" + upper + " " + std::to_string(pct) + "%)";
}

void Blend1DNode::swapClipByKey(const std::string& key, std::shared_ptr<AnimationClip> clip) {
  for (auto* n : m_children)
    if (n) n->swapClipByKey(key, clip);
}

float Blend1DNode::getActiveClipTime() const {
  if (m_lowerIdx < (int)m_children.size() && m_children[m_lowerIdx])
    return m_children[m_lowerIdx]->getActiveClipTime();
  return 0.0f;
}

float Blend1DNode::getActiveClipDuration() const {
  if (m_lowerIdx < (int)m_children.size() && m_children[m_lowerIdx])
    return m_children[m_lowerIdx]->getActiveClipDuration();
  return 0.0f;
}

const AnimationClip* StateMachineNode::getActiveClip() const {
  const int idx = findStateIndex(m_currentState);
  return (idx >= 0 && m_stateNodes[idx]) ? m_stateNodes[idx]->getActiveClip() : nullptr;
}

bool StateMachineNode::evaluateConditions(const TransitionDef& td,
                                          AnimParamTable& params) const {
  bool result = td.requireAll;

  for (const auto& cond : td.conditions) {
    bool condResult = false;
    switch (cond.type) {
      case ConditionType::TriggerConsumed:
        condResult = params.consumeTrigger(cond.param);
        break;
      case ConditionType::BoolTrue:
        condResult = params.getBool(cond.param);
        break;
      case ConditionType::BoolFalse:
        condResult = !params.getBool(cond.param);
        break;
      case ConditionType::SourceNodeFinished: {
        int srcIdx = findStateIndex(td.fromState);
        if (srcIdx >= 0 && srcIdx < (int)m_stateNodes.size() && m_stateNodes[srcIdx])
          condResult = m_stateNodes[srcIdx]->isFinished();
        break;
      }
    }
    if (td.requireAll) result = result && condResult;
    else result = result || condResult;

    if (td.requireAll && !result) return false;
    if (!td.requireAll && result) return true;
  }
  return result;
}

int StateMachineNode::findStateIndex(const std::string& name) const {
  if (!m_pDef) return -1;
  for (int i = 0; i < (int)m_pDef->states.size(); ++i)
    if (m_pDef->states[i].name == name) return i;
  return -1;
}

void StateMachineNode::setSkeleton(const std::vector<Bone>* skeleton) {
  for (auto* n : m_stateNodes)
    if (n) n->setSkeleton(skeleton);
}

void StateMachineNode::transitionTo(const std::string& toState, float blendTime, AnimParamTable& params) {
  int toIdx = findStateIndex(toState);
  if (toIdx < 0) {
    LOG_WARN("[AnimGraph] Transition to unknown state '{}'", toState);
    return;
  }

  LOG_INFO("[AnimGraph] {} -> {}", m_currentState, toState);

  m_previousState = m_currentState;
  m_currentState = toState;
  m_blendTime = blendTime;
  m_blendTimer = 0.0f;
  m_blendAlpha = (blendTime > 0.0f) ? 0.0f : 1.0f;

  AnimGraphNode* incoming = m_stateNodes[toIdx];
  if (incoming) {
    if (auto* clip = dynamic_cast<ClipNode*>(incoming))
      clip->reset();
    else
      incoming->setActiveTime(0.0f);
  }
}

UpdateResult StateMachineNode::update(const UpdateContext& ctx) {
  return updateInternal(ctx, nullptr);
}

UpdateResult StateMachineNode::update(const UpdateContext& ctx, SyncTrackTimeRange range) {
  return updateInternal(ctx, range.valid ? &range : nullptr);
}

UpdateResult StateMachineNode::updateInternal(const UpdateContext& ctx, const SyncTrackTimeRange* range) {
  if (m_currentState.empty() || !m_pDef) return {};

  if (m_blendAlpha < 1.0f && m_blendTime > 0.0f) {
    m_blendTimer += ctx.dt;
    m_blendAlpha = std::min(m_blendTimer / m_blendTime, 1.0f);
  }

  // Anchor for orphan cleanup: everything appended from here belongs to this SMs states
  const uint16_t smBase = ctx.events.CurrentSize();

  const bool blending = (m_blendAlpha < 1.0f && !m_previousState.empty());
  UpdateResult prevResult, currResult;
  const int prevIdx = blending ? findStateIndex(m_previousState) : -1;
  if (prevIdx >= 0 && m_stateNodes[prevIdx])
    prevResult = m_stateNodes[prevIdx]->update(ctx);
  const int currIdx = findStateIndex(m_currentState);
  if (currIdx >= 0 && m_stateNodes[currIdx])
    currResult = range ? m_stateNodes[currIdx]->update(ctx, *range)
                       : m_stateNodes[currIdx]->update(ctx);

  // Transition eval
  const std::string stateBefore = m_currentState;
  auto tryTransitions = [&](bool anyOnly) {
    for (const auto& t : m_pDef->transitions) {
      bool isAny = t.fromState.empty();
      if (isAny != anyOnly) continue;
      if (!isAny && t.fromState != m_currentState) continue;
      if (t.toState == m_currentState) continue;
      if (evaluateConditions(t, ctx.params)) {
        transitionTo(t.toState, t.blendTime, ctx.params);
        return true;
      }
    }
    return false;
  };

  if (!tryTransitions(false))
    tryTransitions(true);

  if (m_currentState != stateBefore) {
    // A transition fired THIS frame. Everything appended above (old prev + old curr) is now
    // partly stale. Two sub cases:
    if (m_blendAlpha >= 1.0f) {
      // Instant transition: re-sample the freshly entered state at dt=0 just keep that
      const uint16_t reBase = ctx.events.CurrentSize();
      UpdateContext ctx0 = ctx; ctx0.dt = 0.0f;
      const int ni = findStateIndex(m_currentState);
      currResult = (ni >= 0 && m_stateNodes[ni]) ? m_stateNodes[ni]->update(ctx0) : UpdateResult{};

      // Drop every event appended before the resample
      ctx.events.ScaleWeights(smBase, reBase, 0.0f);
#ifdef FORGE_DEBUG
      if (ctx.debugger) ctx.debugger->Record(this, "StateMachine", currResult.rootMotion, 1.0f);
#endif
      return currResult;
    }
    // Crossfase start: this frame still shows the outgoing state (old curr) at full weight.
    // The fade begins next frame. Keep currResult, but zero any older prev-state events
    // so a chained transition (combo interrupt) cant leak the state we're fading FROM
    if (prevIdx >= 0)
      ctx.events.ScaleWeights(prevResult.eventRange.begin, prevResult.eventRange.end, 0.0f);
    return currResult;
  }

  if (!blending)
    return currResult;

  // Crossfade in progress: scale each states events by the crossfade weight, and mark
  // the outgoing states events transition-source so consumers can ignore fading-out hitboxes
  ctx.events.ScaleWeights(prevResult.eventRange.begin, prevResult.eventRange.end, 1.0f - m_blendAlpha);
  ctx.events.ScaleWeights(currResult.eventRange.begin, currResult.eventRange.end, m_blendAlpha);
  ctx.events.MarkTransitionSource(prevResult.eventRange.begin, prevResult.eventRange.end);

  TaskIndex idx = ctx.taskSystem.RegisterTask<BlendTask>(
    TaskPhase::PrePhysics, prevResult.taskIdx, currResult.taskIdx, m_blendAlpha);
  glm::vec3 rootDelta = glm::mix(prevResult.rootMotion, currResult.rootMotion, m_blendAlpha);
#ifdef FORGE_DEBUG
  if (ctx.debugger) ctx.debugger->Record(this, "StateMachine", rootDelta, 1.0f);
#endif
  // prevResult appended before currResult so their ranges are contiguous. Guard the case
  // where prev is absent (prevIdx < 0) so the merge does not swallow unrelated leading events
  const SampledEventRange merged = (prevIdx >= 0)
    ? SampledEventRange{ prevResult.eventRange.begin, currResult.eventRange.end }
    : currResult.eventRange;
  return UpdateResult{ idx, rootDelta, merged };
}

bool StateMachineNode::isFinished() const {
  int idx = findStateIndex(m_currentState);
  if (idx < 0 || !m_stateNodes[idx]) return false;
  return m_stateNodes[idx]->isFinished();
}

void StateMachineNode::setActiveTime(float t) {
  int idx = findStateIndex(m_currentState);
  if (idx >= 0 && m_stateNodes[idx])
    m_stateNodes[idx]->setActiveTime(t);
}

float StateMachineNode::getActiveClipTime() const {
  int idx = findStateIndex(m_currentState);
  if (idx >= 0 && m_stateNodes[idx])
    return m_stateNodes[idx]->getActiveClipTime();
  return 0.0f;
}

float StateMachineNode::getActiveClipDuration() const {
  int idx = findStateIndex(m_currentState);
  if (idx >= 0 && m_stateNodes[idx])
    return m_stateNodes[idx]->getActiveClipDuration();
  return 0.0f;
}

std::string StateMachineNode::getDebugStateInfo() const {
  if (m_blendAlpha < 1.0f && !m_previousState.empty())
    return m_previousState + " -> " + m_currentState + " ("
      + std::to_string((int)(m_blendAlpha * 100)) + "%)";
  int idx = findStateIndex(m_currentState);
  if (idx >= 0 && m_stateNodes[idx])
    return m_currentState + "  |  "  + m_stateNodes[idx]->getDebugStateInfo();
  return m_currentState;
}

void Blend2DNode::build() {
  m_triangles.clear();
  m_hullEdges.clear();
  if (!m_pDef || m_pDef->clips.size() < 3) return;

  // --- Bowyer-Watson incremental Delaunay triangulation ---
  int n = (int)m_pDef->clips.size();

  float maxC = 0.0f;
  for (auto& ce : m_pDef->clips)
    maxC = std::max(maxC,
                    std::max(std::abs(ce.x), std::abs(ce.z)));
  float M = maxC * 4.0f + 2.0f;

  // Real points 0..n-1; super-triangle vertices n, n+1, n+2
  std::vector<glm::vec2> pts;
  pts.reserve(n + 3);
  for (auto& ce : m_pDef->clips) pts.push_back({ ce.x, ce.z });
  pts.push_back({0.0f, 3.0f * M});
  pts.push_back({-3.0f * M, -M});
  pts.push_back({3.0f * M, -M});

  struct Tri {
    int a, b, c;
  };
  std::vector<Tri> tris;
  tris.push_back({n, n + 1, n + 2});

  // True if p is inside the circumcircle of (pts[ia], pts[ib], pts[ic])
  auto inCircumcircle = [&](glm::vec2 p, int ia, int ib, int ic) -> bool {
    glm::vec2 a = pts[ia] - p, b = pts[ib] - p, c = pts[ic] - p;
    return (a.x * a.x + a.y * a.y) * (b.x * c.y - b.y * c.x) -
               (b.x * b.x + b.y * b.y) * (a.x * c.y - a.y * c.x) +
               (c.x * c.x + c.y * c.y) * (a.x * b.y - a.y * b.x) >
           1e-7f;
  };

  for (int pi = 0; pi < n; pi++) {
    glm::vec2 p = pts[pi];

    std::vector<Tri> bad, good;
    for (auto& t : tris)
      (inCircumcircle(p, t.a, t.b, t.c) ? bad : good).push_back(t);

    // Boundary: edges belonging to exactly one bad triangle
    using Edge = std::pair<int, int>;
    std::vector<Edge> boundary;
    for (auto& t : bad) {
      Edge es[3] = { {t.a, t.b}, {t.b, t.c}, {t.c, t.a} };
      for (auto& e : es) {
        Edge rev = { e.second, e.first };
        auto it = std::find(boundary.begin(), boundary.end(), rev);
        if (it == boundary.end())
          boundary.push_back(e);
        else
          boundary.erase(it);
      }
    }

    tris = good;
    for (auto& e : boundary) tris.push_back({e.first, e.second, pi});
  }

  // Discard triangles touching super-triangle vertices
  for (auto& t : tris)
    if (t.a < n && t.b < n && t.c < n) m_triangles.push_back({t.a, t.b, t.c});

  // --- Hull edge extraction (unchanged) ---
  struct EdgeKey {
    int a, b;
    bool operator==(const EdgeKey& o) const { return a == o.a && b == o.b; }
  };
  struct EdgeHash {
    size_t operator()(const EdgeKey& k) const {
      return std::hash<int>()(k.a) ^ (std::hash<int>()(k.b) << 1);
    }
  };

  auto makeKey = [](int u, int v) -> EdgeKey {
    return (u < v) ? EdgeKey{u, v} : EdgeKey{v, u};
  };

  std::unordered_map<EdgeKey, std::pair<int, int>, EdgeHash> edges;
  for (size_t t = 0; t < m_triangles.size(); ++t) {
    const auto& tri = m_triangles[t];
    int e[3][2] = {{tri.a, tri.b}, {tri.b, tri.c}, {tri.c, tri.a}};
    for (int i = 0; i < 3; i++) {
      EdgeKey k = makeKey(e[i][0], e[i][1]);
      auto& slot = edges[k];
      slot.first++;
      slot.second = static_cast<int>(t);
    }
  }
  for (const auto& [key, val] : edges) {
    if (val.first == 1) m_hullEdges.push_back({key.a, key.b, val.second});
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

UpdateResult Blend2DNode::update(const UpdateContext& ctx) {
  if (!m_pDef || m_children.empty()) return {};
  m_currentParam.x = ctx.params.getFloat(m_pDef->xParamName, 0.0f);
  m_currentParam.y = ctx.params.getFloat(m_pDef->zParamName, 0.0f);

  if (m_children.size() == 1 || m_triangles.empty()) {
    m_idxA = m_idxB = m_idxC = 0;
    m_weights = glm::vec3(1.0f, 0.0f, 0.0f);
    UpdateResult r = m_children[0] ? m_children[0]->update(ctx) : UpdateResult{};
#ifdef FORGE_DEBUG
    if (ctx.debugger) ctx.debugger->Record(this, "Blend2D", r.rootMotion, 1.0f);
#endif
    return r;
  }

  locateTriangle(); // sets m_idxA/B/C + m_weights

  SyncTrackTimeRange range = computeSyncRange2D(ctx); // valid if all three corner tracks compatible

  if (!range.valid) {
    // dt fallback: advance ALL children but keep only the three corners events
    const uint16_t base = ctx.events.CurrentSize();
    std::vector<UpdateResult> childResults(m_children.size());
    for (size_t i = 0; i < m_children.size(); ++i)
      if (m_children[i]) childResults[i] = m_children[i]->update(ctx);

    for (size_t i = 0; i < m_children.size(); ++i) {
      const float w = ((int)i == m_idxA) ? m_weights.x
                    : ((int)i == m_idxB) ? m_weights.y
                    : ((int)i == m_idxC) ? m_weights.z
                                        : 0.0f;
      ctx.events.ScaleWeights(childResults[i].eventRange.begin, childResults[i].eventRange.end, w);
    }

    return blendTriangle(ctx,
                         childResults[m_idxA], childResults[m_idxB],
                         childResults[m_idxC], SampledEventRange{ base, ctx.events.CurrentSize() });
  }

  UpdateResult ra = m_children[m_idxA] ? m_children[m_idxA]->update(ctx, range) : UpdateResult{};
  UpdateResult rb = m_children[m_idxB] ? m_children[m_idxB]->update(ctx, range) : UpdateResult{};
  UpdateResult rc = m_children[m_idxC] ? m_children[m_idxC]->update(ctx, range) : UpdateResult{};

  ctx.events.ScaleWeights(ra.eventRange.begin, ra.eventRange.end, m_weights.x);
  ctx.events.ScaleWeights(rb.eventRange.begin, rb.eventRange.end, m_weights.y);
  ctx.events.ScaleWeights(rc.eventRange.begin, rc.eventRange.end, m_weights.z);

  return blendTriangle(ctx, ra, rb, rc, SampledEventRange{ ra.eventRange.begin, rc.eventRange.end });
}

UpdateResult Blend2DNode::update(const UpdateContext& ctx, SyncTrackTimeRange parentRange) {
  if (!parentRange.valid) return update(ctx);
  if (!m_pDef || m_children.empty()) return {};
  m_currentParam.x = ctx.params.getFloat(m_pDef->xParamName, 0.0f);
  m_currentParam.y = ctx.params.getFloat(m_pDef->zParamName, 0.0f);

  if (m_children.size() == 1 || m_triangles.empty()) {
    m_idxA = m_idxB = m_idxC = 0;
    m_weights = glm::vec3(1.0f, 0.0f, 0.0f);
    return m_children[0] ? m_children[0]->update(ctx, parentRange) : UpdateResult{};
  }

  locateTriangle();

  UpdateResult ra = m_children[m_idxA] ? m_children[m_idxA]->update(ctx, parentRange) : UpdateResult{};
  UpdateResult rb = m_children[m_idxB] ? m_children[m_idxB]->update(ctx, parentRange) : UpdateResult{};
  UpdateResult rc = m_children[m_idxC] ? m_children[m_idxC]->update(ctx, parentRange) : UpdateResult{};

  ctx.events.ScaleWeights(ra.eventRange.begin, ra.eventRange.end, m_weights.x);
  ctx.events.ScaleWeights(rb.eventRange.begin, rb.eventRange.end, m_weights.y);
  ctx.events.ScaleWeights(rc.eventRange.begin, rc.eventRange.end, m_weights.z);

  return blendTriangle(ctx, ra, rb, rc, SampledEventRange{ ra.eventRange.begin, rc.eventRange.end });
}

UpdateResult Blend2DNode::blendTriangle(const UpdateContext& ctx,
                                        const UpdateResult& a, const UpdateResult& b,
                                        const UpdateResult& c, SampledEventRange merged) {
  const float wab = m_weights.x + m_weights.y;
  TaskIndex blendAB = (wab > 1e-6f)
    ? ctx.taskSystem.RegisterTask<BlendTask>(TaskPhase::PrePhysics, a.taskIdx, b.taskIdx, m_weights.y / wab)
    : a.taskIdx;
  TaskIndex idx = ctx.taskSystem.RegisterTask<BlendTask>(TaskPhase::PrePhysics, blendAB, c.taskIdx, m_weights.z);

  glm::vec3 rootDelta = m_weights.x * a.rootMotion + m_weights.y * b.rootMotion + m_weights.z * c.rootMotion;
#ifdef FORGE_DEBUG
  if (ctx.debugger) ctx.debugger->Record(this, "Blend2D", rootDelta, 1.0f);
#endif
  return UpdateResult{ idx, rootDelta, merged };
}

SyncTrackTimeRange Blend2DNode::computeSyncRange2D(const UpdateContext& ctx) {
  SyncTrackTimeRange range;

  ClipNode* A = m_children[m_idxA];
  ClipNode* B = m_children[m_idxB];
  ClipNode* C = m_children[m_idxC];
  const AnimationClip* ca = A ? A->m_clip.get() : nullptr;
  const AnimationClip* cb = B ? B->m_clip.get() : nullptr;
  const AnimationClip* cc = C ? C->m_clip.get() : nullptr;
  if (!ca || !cb || !cc) return range;
  if (ca->syncTrack.empty() || cb->syncTrack.empty()|| cc->syncTrack.empty()) return range;
  if (!ca->syncTrack.CompatibleWith(cb->syncTrack) ||
      !ca->syncTrack.CompatibleWith(cc->syncTrack)) return range;

  // Blend the corner tracks pairwise, matching the pose blend weighting
  const float wab = m_weights.x + m_weights.y;
  const float abW = (wab > 1e-6f) ? (m_weights.y / wab) : 0.0f;
  const SyncTrack ab = SyncTrack::Blend(ca->syncTrack, cb->syncTrack, abW);
  const SyncTrack blended = SyncTrack::Blend(ab, cc->syncTrack, m_weights.z);

  const float durAB = glm::mix(ca->duration, cb->duration, abW);
  const float blendedDur = glm::mix(durAB, cc->duration, m_weights.z);
  const float advance = blendedDur > 1e-6f ? ctx.dt / blendedDur : 0.0f;

  m_prevSyncPct = m_syncPct;
  m_syncPct += advance;
  const bool wrapped = m_syncPct >= 1.0f;
  if (wrapped) m_syncPct -= std::floor(m_syncPct);
  
  range.startSyncPos = blended.PercentageToSyncPos(m_prevSyncPct);
  range.endSyncPos = blended.PercentageToSyncPos(m_syncPct);
  if (wrapped) range.endSyncPos += blended.count();
  range.valid = true;
  return range;
}

void Blend2DNode::locateTriangle() {
  auto pos = [&](int i) -> glm::vec2 {
    return { m_pDef->clips[i].x, m_pDef->clips[i].z };
  };

  int found = -1;
  glm::vec3 bary(0.0f);
  for (size_t t = 0; t < m_triangles.size(); ++t) {
    const auto& tri = m_triangles[t];
    bary = barycentric(m_currentParam,
                       pos(tri.a),
                       pos(tri.b),
                       pos(tri.c));
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

    glm::vec2 bestPoint = pos(0);
    float bestDist2 = std::numeric_limits<float>::max();
    int bestTri = 0;
    for (const auto& he : m_hullEdges) {
      glm::vec2 a = pos(he.u);
      glm::vec2 b = pos(he.v);
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
                       pos(tri.a),
                       pos(tri.b),
                       pos(tri.c));
    found = bestTri;
  }

  const auto& tri = m_triangles[found];
  m_idxA = tri.a; m_idxB = tri.b; m_idxC = tri.c;
  m_weights = bary;
}

void Blend2DNode::setSkeleton(const std::vector<Bone>* skeleton) {
  for (auto& n : m_children)
    if (n) n->setSkeleton(skeleton);
}

void Blend2DNode::swapClipByKey(const std::string& key,
                                std::shared_ptr<AnimationClip> clip) {
  for (auto& n : m_children)
    if (n) n->swapClipByKey(key, clip);
}

void Blend2DNode::setActiveTime(float t) {
  for (auto& n : m_children)
    if (n) n->setActiveTime(t);
}

float Blend2DNode::getActiveClipTime() const {
  return (m_children.empty() || !m_children[m_idxA]) ? 0.0f : m_children[m_idxA]->getActiveClipTime();
}

float Blend2DNode::getActiveClipDuration() const {
  return (m_children.empty() || !m_children[m_idxA]) ? 0.0f : m_children[m_idxA]->getActiveClipDuration();
}

const AnimationClip* Blend2DNode::getActiveClip() const {
  if (m_children.empty()) return nullptr;
  int idx = m_idxA;
  if (m_weights.y >= m_weights.x && m_weights.y >= m_weights.z) idx = m_idxB;
  else if (m_weights.z >= m_weights.x && m_weights.z >= m_weights.y) idx = m_idxC;
  return (idx < (int)m_children.size() && m_children[idx]) ? m_children[idx]->getActiveClip() : nullptr;
}

std::string Blend2DNode::getDebugStateInfo() const {
  if (!m_pDef || m_children.empty()) return "Blend2D(empty)";
    return "Blend2D(" + m_pDef->xParamName + "," + m_pDef->zParamName + " @ "
        + std::to_string(m_currentParam.x).substr(0, 4) + ","
        + std::to_string(m_currentParam.y).substr(0, 4) + ")";
}

UpdateResult ClipSelectorNode::update(const UpdateContext& ctx) {
  if (!m_pDef) return {};
  m_selected = ctx.params.getInt(m_pDef->paramName, 0);

  if (m_selected != m_prevSelected) {
    auto it = m_byIndex.find(m_selected);
    if (it != m_byIndex.end() && it->second)
      it->second->reset();
    m_prevSelected = m_selected;
  }

  auto it = m_byIndex.find(m_selected);
  if (it == m_byIndex.end() || !it->second) return {};
  return it->second->update(ctx);
}

UpdateResult ClipSelectorNode::update(const UpdateContext& ctx, SyncTrackTimeRange range) {
  if (!m_pDef) return {};
  m_selected = ctx.params.getInt(m_pDef->paramName, 0);

  if (m_selected != m_prevSelected) {
    auto it = m_byIndex.find(m_selected);
    if (it != m_byIndex.end() && it->second)
      it->second->reset();
    m_prevSelected = m_selected;
  }

  auto it = m_byIndex.find(m_selected);
  if (it == m_byIndex.end() || !it->second) return {};
  return it->second->update(ctx, range);
}

bool ClipSelectorNode::isFinished() const {
  auto it = m_byIndex.find(m_selected);
  if (it != m_byIndex.end() && it->second)
    return it->second->isFinished();
  return true;
}

//glm::vec3 ClipSelectorNode::getRootMotionDelta() const {
//  auto it = m_byIndex.find(m_selected);
//  if (it != m_byIndex.end() && it->second)
//    return it->second->getRootMotionDelta();
//  return glm::vec3(0.0f);
//}

float ClipSelectorNode::getActiveClipTime() const {
  auto it = m_byIndex.find(m_selected);
  if (it != m_byIndex.end() && it->second)
    return it->second->getActiveClipTime();
  return 0.0f;
}

float ClipSelectorNode::getActiveClipDuration() const {
  auto it = m_byIndex.find(m_selected);
  if (it != m_byIndex.end() && it->second)
    return it->second->getActiveClipDuration();
  return 0.0f;
}

const AnimationClip* ClipSelectorNode::getActiveClip() const {
  auto it = m_byIndex.find(m_selected);
  return (it != m_byIndex.end() && it->second) ? it->second->getActiveClip() : nullptr;
}

void ClipSelectorNode::setSkeleton(const std::vector<Bone>* skeleton) {
  for (auto* n : m_children)
    if (n) n->setSkeleton(skeleton);
}

void ClipSelectorNode::swapClipByKey(const std::string& key,
                                     std::shared_ptr<AnimationClip> clip) {
  for (auto* n : m_children)
    if (n) n->swapClipByKey(key, clip);
}

void ClipSelectorNode::setActiveTime(float t) {
  //m_prevSelected = -1;
  auto it = m_byIndex.find(m_selected);
  if (it != m_byIndex.end() && it->second)
    it->second->setActiveTime(t);
}

std::string ClipSelectorNode::getDebugStateInfo() const {
  auto it = m_byIndex.find(m_selected);
  if (it != m_byIndex.end() && it->second)
    return "ClipSelectorNode[" + std::to_string(m_selected) + "]:"
            + it->second->getDebugStateInfo();
  return "ClipSelectorNode[" + std::to_string(m_selected) + ":empty]";
}

std::shared_ptr<GraphDefinition> GraphDefinition::LoadFromJson(const std::string& path) {
  std::string absPath = forge::AssetManager::resolvePath(path);
  std::ifstream f(absPath);
  if (!f.is_open()) {
    LOG_ERROR("[AnimGraph] GraphDefinition::LoadFromJson - cannot open '{}'", path);
    return nullptr;
  }

  nlohmann::json root;
  try {
    root = nlohmann::json::parse(f);
  } catch (const nlohmann::json::parse_error& e) {
    LOG_ERROR("[AnimGraph] JSON parse error in '{}': {}", path, e.what());
    return nullptr;
  }

  int version = root.value("version", 0);
  if(version != 1) {
    LOG_ERROR("[AnimGraph] '{}' has unsupported version {}", path, version);
    return nullptr;
  }
  
  auto def = std::make_shared<GraphDefinition>();
  def->m_rootNodeIndex = static_cast<int16_t>(root.value("rootNodeIndex", 0));

  const auto& nodesJson = root["nodes"];
  def->m_nodeDefs.resize(nodesJson.size());

  // Pass 1: allocate each definition by type name via type registry
  for (int i = 0; i < (int)nodesJson.size(); ++i) {
    const auto& n = nodesJson[i];
    const std::string typeStr = n["type"].get<std::string>().c_str();
    TypeID tid = forge::fnv1a(typeStr);

    if (tid == forge::fnv1a("ClipNode::Definition")) {
      auto d = std::make_unique<ClipNode::Definition>();
      d->clipFile = n.value("clipFile", "");
      d->clipName = n.value("clipName", "");
      d->taeKey = n.value("taeKey", "");
      d->actionKey = n.value("actionKey", "");
      d->ownerName = n.value("ownerName", "");
      d->loop = n.value("loop", true);
      def->m_nodeDefs[i] = std::move(d);
      def->m_nodeTypeIDs.push_back(tid);
    } else if (tid == forge::fnv1a("Blend1DNode::Definition")) {
      auto d = std::make_unique<Blend1DNode::Definition>();
      d->paramName = n.value("paramName", "");
      d->thresholds = n["thresholds"].get<std::vector<float>>();
      d->childNodeIndices = n ["childNodeIndices"].get<std::vector<int16_t>>();
      def->m_nodeDefs[i] = std::move(d);
      def->m_nodeTypeIDs.push_back(tid);
    } else if (tid == forge::fnv1a("Blend2DNode::Definition")) {
      auto d = std::make_unique<Blend2DNode::Definition>();
      d->xParamName = n.value("xParamName", "");
      d->zParamName = n.value("zParamName", "");
      for (const auto& c : n["clips"])
        d->clips.push_back({ c["x"].get<float>(), c["z"].get<float>(), 
                             c["nodeIndex"].get<int16_t>() });
      def->m_nodeDefs[i] = std::move(d);
      def->m_nodeTypeIDs.push_back(tid);
    } else if (tid == forge::fnv1a("ClipSelectorNode::Definition")) {
      auto d = std::make_unique<ClipSelectorNode::Definition>();
      d->paramName = n.value("paramName", "");
      d->ownerName = n.value("ownerName", "");
      for (const auto& e : n["entries"])
        d->entries.push_back({ e["selectorIndex"].get<int>(),
                               e["nodeIndex"].get<int16_t>() });
      def->m_nodeDefs[i] = std::move(d);
      def->m_nodeTypeIDs.push_back(tid);
    } else if (tid == forge::fnv1a("StateMachineNode::Definition")) {
      auto d = std::make_unique<StateMachineNode::Definition>();
      d->initialState = n.value("initialState", "");
      for (const auto& s : n["states"])
        d->states.push_back({ s["name"].get<std::string>(),
                              s["nodeIndex"].get<int16_t>() });
      for (const auto& t : n["transitions"]) {
        StateMachineNode::TransitionDef td;
        td.fromState = t.value("fromState", "");
        td.toState = t.value("toState", "");
        td.blendTime = t.value("blendTime", 0.2f);
        td.requireAll = t.value("requireAll", true);
        for (const auto& c : t["conditions"]) {
          TransitionConditionDef cond;
          std::string typeStr = c["type"].get<std::string>();
          if (typeStr == "TriggerConsumed") cond.type = ConditionType::TriggerConsumed;
          else if (typeStr == "BoolTrue") cond.type = ConditionType::BoolTrue;
          else if (typeStr == "BoolFalse") cond.type = ConditionType::BoolFalse;
          else if (typeStr == "SourceNodeFinished") cond.type = ConditionType::SourceNodeFinished;
          cond.param = c.value("param", "");
          td.conditions.push_back(cond);
        }
        d->transitions.push_back(std::move(td));
      }
      def->m_nodeDefs[i] = std::move(d);
      def->m_nodeTypeIDs.push_back(tid);
    } else {
      LOG_WARN("[AnimGraph] LoadFromJson: unhandled node type at index {}", i);
    }
  }

  return def;
}

struct NodeRuntimeInfo {
  size_t size;
  size_t alignment;
  void (*pfnConstruct)(void* mem, const IAnimNodeDefinition* def);
  void (*pfnDestruct)(void* mem);
};

static std::unordered_map<TypeID, NodeRuntimeInfo>& GetNodeRegistry() {
  static std::unordered_map<TypeID, NodeRuntimeInfo> s_registry;
  return s_registry;
}

template<typename TDef, typename TRuntime>
struct NodeRuntimeRegistrar {
  NodeRuntimeRegistrar() {
    TypeID tid = forge::fnv1a(TDef::kTypeName);
    GetNodeRegistry()[tid] = {
      sizeof(TRuntime),
      alignof(TRuntime),
      [](void* mem, const IAnimNodeDefinition* def) {
        auto* node = new (mem) TRuntime();
        node->m_pDef = static_cast<const TDef*>(def);
      },
      [](void* mem) {
        static_cast<TRuntime*>(mem)->~TRuntime();
      }
    };
  }
};

static NodeRuntimeRegistrar<ClipNode::Definition, ClipNode> s_clipNodeReg;
static NodeRuntimeRegistrar<Blend1DNode::Definition, Blend1DNode> s_blend1DReg;
static NodeRuntimeRegistrar<Blend2DNode::Definition, Blend2DNode> s_blend2DReg;
static NodeRuntimeRegistrar<ClipSelectorNode::Definition, ClipSelectorNode> s_clipSelReg;
static NodeRuntimeRegistrar<StateMachineNode::Definition, StateMachineNode> s_smReg;

GraphInstance::~GraphInstance() {
  auto& reg = GetNodeRegistry();
  for (int i = 0; i < (int)m_nodes.size(); ++i) {
    if (!m_nodes[i]) continue;
    TypeID tid = forge::fnv1a(m_pDefinition->m_nodeDefs[i]->kTypeName);
    auto it = reg.find(tid);
    if (it != reg.end()) it->second.pfnDestruct(m_nodes[i]);
  }
}

GraphInstance GraphInstance::Create(std::shared_ptr<const GraphDefinition> def) {
  auto& reg = GetNodeRegistry();
  int N = (int)def->m_nodeDefs.size();

  // Step 1: compute buffer layout
  std::vector<size_t> offsets(N);
  size_t totalSize = 0;
  for (int i = 0; i < N; ++i) {
    TypeID tid = def->m_nodeTypeIDs[i]; 
    auto it = reg.find(tid);
    if (it == reg.end()) {
      LOG_ERROR("[AnimGraph] GraphInstance::Create - no runtime type for '{}'",
                tid);
      return {};
    }
    size_t align = it->second.alignment;
    totalSize = (totalSize + align - 1) & ~(align - 1); // align up
    offsets[i] = totalSize;
    totalSize += it->second.size;
  }

  // Step 2: allocate buffer and placement-new all nodes
  GraphInstance inst;
  inst.m_pDefinition = def;
  inst.m_nodeBuffer = std::make_unique<std::byte[]>(totalSize);
  inst.m_nodes.resize(N, nullptr);

  for (int i = 0; i < N; ++i) {
    TypeID tid = def->m_nodeTypeIDs[i]; 
    auto& info = reg.at(tid);
    void* mem = inst.m_nodeBuffer.get() + offsets[i];
    info.pfnConstruct(mem, def->m_nodeDefs[i].get());
    inst.m_nodes[i] = static_cast<AnimGraphNode*>(mem);
  }

  // Step 3: Wire child pointers
  // Each node type needs its m_children/m_stateNodes populated from indices in its Definition.
  for (int i = 0; i < N; i++) {
    IAnimNodeDefinition* nodeDef = def->m_nodeDefs[i].get();
    nodeDef->debugNodeIdx = i;
    AnimGraphNode* node = inst.m_nodes[i];
    TypeID tid = def->m_nodeTypeIDs[i]; 

    if (tid == forge::fnv1a("ClipNode::Definition")) {
      auto* cn = static_cast<ClipNode*>(node);
      auto* cnd = static_cast<const ClipNode::Definition*>(nodeDef);
      if (!cnd->clipFile.empty())
        cn->m_clip = forge::AssetManager::loadAnimationClip(cnd->clipFile, cnd->clipName, cnd->taeKey);
    } else if (tid == forge::fnv1a("Blend1DNode::Definition")) {
      auto* bn = static_cast<Blend1DNode*>(node);
      auto* bnd = static_cast<Blend1DNode::Definition*>(nodeDef);
      bn->m_children.reserve(bnd->childNodeIndices.size());
      for (int16_t ci : bnd->childNodeIndices)
        bn->m_children.push_back(inst.m_nodes[ci]);
    } else if (tid == forge::fnv1a("Blend2DNode::Definition")) {
      auto* bn = static_cast<Blend2DNode*>(node);
      auto* bnd = static_cast<Blend2DNode::Definition*>(nodeDef);
      bn->m_children.reserve(bnd->clips.size());
      for (const auto& ce : bnd->clips)
        bn->m_children.push_back(static_cast<ClipNode*>(inst.m_nodes[ce.nodeIndex]));
      bn->build(); // Compute Delaunay triangulation from m_pDef->clips pos
    } else if (tid == forge::fnv1a("ClipSelectorNode::Definition")) {
      auto* sn = static_cast<ClipSelectorNode*>(node);
      auto* snd = static_cast<ClipSelectorNode::Definition*>(nodeDef);
      sn->m_children.reserve(snd->entries.size());
      for (const auto& e : snd->entries) {
        ClipNode* cn = static_cast<ClipNode*>(inst.m_nodes[e.nodeIndex]);
        sn->m_children.push_back(cn);
        sn->m_byIndex[e.selectorIndex] = cn;
      }
    } else if (tid == forge::fnv1a("StateMachineNode::Definition")) {
      auto* sm = static_cast<StateMachineNode*>(node);
      auto* smd = static_cast<StateMachineNode::Definition*>(nodeDef);
      sm->m_stateNodes.reserve(smd->states.size());
      for (const auto& s : smd->states)
        sm->m_stateNodes.push_back(inst.m_nodes[s.nodeIndex]);
      sm->m_currentState = smd->initialState;
    }
  }

  // Step 4: Inject skeleton (deferred to Animator::setGraph when skeleton is known).
  // Not set here; Propagated to Animator::setGraph() -> GetRootNode() -> setSkeleton()

  // Step 5: build m_clipsByActionKey index for swapMovesetClips
  for (int i = 0; i < N; ++i) {
    TypeID tid = def->m_nodeTypeIDs[i]; 
    if (tid == forge::fnv1a("ClipNode::Definition")) {
      auto* cn = static_cast<ClipNode*>(inst.m_nodes[i]);
      auto* cnd = static_cast<const ClipNode::Definition*>(def->m_nodeDefs[i].get());
      if (!cnd->actionKey.empty())
        inst.m_clipsByActionKey[cnd->actionKey].push_back(cn);
    }
  }

  return inst;
}

GraphInstance GraphInstance::Clone() const {
  return GraphInstance::Create(m_pDefinition);
}

AnimGraphNode* GraphInstance::GetRootNode() const {
  if (m_nodes.empty()) return nullptr;
  return m_nodes[m_pDefinition->m_rootNodeIndex];
}

const std::vector<ClipNode*>& GraphInstance::FindClipsByActionKey(const std::string& key) const {
  static const std::vector<ClipNode*> kEmpty;
  auto it = m_clipsByActionKey.find(key);
  return (it != m_clipsByActionKey.end()) ? it->second : kEmpty;
}

}
