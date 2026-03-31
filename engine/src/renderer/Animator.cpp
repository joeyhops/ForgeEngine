#include <forge/Animator.h>
#include <forge/Events.h>
#include <forge/EventBus.h>
#include <forge/Logger.h>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <memory>
#include <unordered_set>

namespace forge {

void Animator::setSkeleton(const std::vector<Bone>& skeleton) {
  m_skeleton = skeleton;
  m_boneMatrices.assign(skeleton.size(), glm::mat4(1.0f));
  m_localTransforms.assign(skeleton.size(), glm::mat4(1.0f));
  m_globalTransforms.assign(skeleton.size(), glm::mat4(1.0f));
}

void Animator::play(std::shared_ptr<AnimationClip> clip,
                    bool loop, float blendTime)
{
  if (!clip) return;

  // Deactivate any currently active TAE events before switching clips
  deactivateAllEvents();

  m_prevClip = m_currentClip;
  m_currentClip = clip;
  m_currentClip->looping = loop;
  m_prevTime = 0.0f;
  m_currentTime = 0.0f;
  m_blendTime = blendTime;
  m_blendElapsed = 0.0f;
  m_activeEventIndices.clear();

  LOG_INFO("[Animator] '{}' Playing '{}'", m_ownerName, clip->name);
}

void Animator::update(float dt) {
  if (!m_currentClip) return;

  m_prevTime = m_currentTime;
  m_currentTime += dt;

  if (m_currentClip->looping && m_currentTime >= m_currentClip->duration) {
    // Loop wrap - deactivate any still-active events then reset
    deactivateAllEvents();
    m_currentTime = fmodf(m_currentTime, m_currentClip->duration);
    m_prevTime = 0.0f; // Scan from the beginning of the new loop iteration
  } else if (!m_currentClip->looping && m_currentTime >= m_currentClip->duration) {
    // Non-looping clip will finish (shocker) - clamp and deactivate remaining events
    m_currentTime = m_currentClip->duration;
  }

  // Fire TAE events for any boundary crossings this frame
  tickTAEEvents(m_prevTime, m_currentTime);

  m_blendElapsed += dt;
  computeBoneMatrices();
}

bool Animator::isFinished() const {
  if (!m_currentClip || m_currentClip->looping) return false;
  return m_currentTime >= m_currentClip->duration;
}

// TAE Event firing

void Animator::tickTAEEvents(float prevTime, float curTime) {
  if (!m_currentClip) return;
  const auto& events = m_currentClip->events;

  for (int i = 0; i < (int)events.size(); i++) {
    const AnimEvent& ev = events[i];
    bool wasActive = m_activeEventIndices.count(i) > 0;

    if (ev.startTime == ev.endTime) {
      // One-shot: fire activated + deactivated on first crossing
      if (!wasActive && prevTime <= ev.startTime && curTime > ev.startTime) {
        EventBus::publish(AnimEventActivated{
          m_ownerName, ev.type, ev.payload
        });
        EventBus::publish(AnimEventDeactivated{
          m_ownerName, ev.type, ev.payload
        });
        // Mark as 'seen' so we don't re-fire; 
        m_activeEventIndices.insert(i);
      }
    } else {
      // Window event: fire Activated on entry, deactivated on exit
      bool shouldbeActive = (curTime >= ev.startTime && curTime < ev.endTime);

      if (!wasActive && shouldbeActive) {
        m_activeEventIndices.insert(i);
        EventBus::publish(AnimEventActivated{
          m_ownerName, ev.type, ev.payload
        });

      } else if (wasActive && !shouldbeActive) {
        m_activeEventIndices.erase(i);
        EventBus::publish(AnimEventDeactivated{
          m_ownerName, ev.type, ev.payload
        });
      }
    }
  }
}

void Animator::deactivateAllEvents() {
  if (!m_currentClip) return;
  const auto& events = m_currentClip->events;

  for (int i : m_activeEventIndices) {
    if (i < (int)events.size()) {
      EventBus::publish(AnimEventDeactivated{
        m_ownerName, events[i].type, events[i].payload
      });
    }
  }
  m_activeEventIndices.clear();
}

void Animator::computeBoneMatrices() {
  if (m_skeleton.empty() || !m_currentClip) return;

  m_boneMatrices.resize(m_skeleton.size(), glm::mat4(1.0f));
  m_localTransforms.resize(m_skeleton.size(), glm::mat4(1.0f));
  m_globalTransforms.resize(m_skeleton.size());

  // Sample primary clip into local transform
  m_currentClip->sample(m_currentTime, m_skeleton, m_localTransforms);

  // if cross-fading, blend with previous clips pose
  if (m_prevClip && m_blendElapsed < m_blendTime) {
    std::vector<glm::mat4> prevLocal(m_skeleton.size(), glm::mat4(1.0f));
    m_prevClip->sample(m_blendElapsed, m_skeleton, prevLocal);

    float alpha = m_blendElapsed / m_blendTime; // 0 = all prev, 1= all current
    for (size_t i = 0; i < m_skeleton.size(); i++) {
      // Lerp the raw matrices - fine for short blends
      // a proper implementation would decompose to TRS and slerp
      // the quaternion. but matrix lerp produces acceptable results
      // at blend times <= 0.3s
      m_localTransforms[i] = prevLocal[i] * (1.0f - alpha) + m_localTransforms[i] * alpha;
    }
  }

  // Walk bone hierarchy: each bones global transform =
  // parents global transform * this bones local transform
  for (size_t i = 0; i < m_skeleton.size(); i++) {
    const Bone& bone = m_skeleton[i];

    if (bone.parentIndex < 0)
      // Root bone - local transform IS global transform
      m_globalTransforms[i] = m_localTransforms[i];
    else
      m_globalTransforms[i] = m_globalTransforms[bone.parentIndex] * m_localTransforms[i];

    // Final bone matrix = globalTransform * offsetMatrix
    // offsetMatrix transforms from model space into bone space
    m_boneMatrices[i] = m_globalTransforms[i] * bone.offsetMatrix;
  }
}

// BoneTrack::sample - interpolate between keyframes
BoneKeyFrame BoneTrack::sample(float t) const {
  if (keyframes.empty()) return {};
  if (keyframes.size() == 1) return keyframes[0];

  // Find surrounding keyframes
  size_t next = 0;
  while (next < keyframes.size() && keyframes[next].time < t)
    next++;

  if (next == 0) return keyframes.front();
  if (next >= keyframes.size()) return keyframes.back();

  size_t prev = next - 1;
  float range = keyframes[next].time - keyframes[prev].time;
  float alpha = (range > 0.0001f)
    ? (t - keyframes[prev].time) / range
    : 0.0f;

  BoneKeyFrame out;
  out.time = t;
  out.position = glm::mix(keyframes[prev].position,
                          keyframes[next].position,
                          alpha);
  out.rotation = glm::slerp(keyframes[prev].rotation,
                          keyframes[next].rotation,
                          alpha);
  out.scale = glm::mix(keyframes[prev].scale,
                          keyframes[next].scale,
                          alpha);

  return out;
}

void AnimationClip::sample(float t,
                           const std::vector<Bone>& skeleton,
                           std::vector<glm::mat4>& out) const
{
  out.resize(skeleton.size(), glm::mat4(1.0f));

  for (size_t i = 0; i < skeleton.size(); i++) {
    // Start with the bines default (bindpose) local transform
    glm::mat4 local = skeleton[i].localTransform;

    // Find bones track (if animated)
    auto it = trackIndex.find(skeleton[i].name);
    if (it != trackIndex.end()) {
      const BoneTrack& track = tracks[it->second];
      BoneKeyFrame frame = track.sample(t);

      // Rebuild TRS matrix from keyframe
      glm::mat4 T = glm::translate(glm::mat4(1.0f), frame.position);
      glm::mat4 R = glm::toMat4(frame.rotation);
      glm::mat4 S = glm::scale(glm::mat4(1.0f), frame.scale);
      local = T * R * S;
    }

    out[i] = local;
  }
}

}
