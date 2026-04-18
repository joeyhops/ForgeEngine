#include <forge/Animator.h>
#include <forge/Events.h>
#include <forge/EventBus.h>
#include <forge/Logger.h>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <memory>

namespace forge {

void Animator::setSkeleton(const std::vector<Bone>& skeleton) {
  m_skeleton = skeleton;
  m_boneMatrices.assign(skeleton.size(), glm::mat4(1.0f));
  m_localTransforms.assign(skeleton.size(), glm::mat4(1.0f));
  m_globalTransforms.assign(skeleton.size(), glm::mat4(1.0f));
}

void Animator::setGraph(std::shared_ptr<AnimGraphNode> graph) {
  m_graph = std::move(graph);
  if (m_graph && !m_skeleton.empty())
    m_graph->setSkeleton(&m_skeleton);
  LOG_INFO("[Animator] '{}' graph attached", m_ownerName);
}

void Animator::update(float dt) {
  if (!m_graph) {
    LOG_ERROR("[Animator] '{}' update() called without graph attached.", m_ownerName);
    return;
  }
  m_graph->update(dt, m_params);
  m_graph->evaluate(m_localTransforms);
  computeBoneMatrices();
}

bool Animator::isFinished() const {
  if (!m_graph) return false; 
  return m_graph->isFinished();
}

float Animator::getActiveClipTime() const {
  if (!m_graph) return 0.0f; 
  return m_graph->getActiveClipTime();
}

float Animator::getActiveClipDuration() const {
  if (!m_graph) return 0.0f; 
  return m_graph->getActiveClipDuration();
}

std::string Animator::getStateInfo() const {
  if (!m_graph) return "none"; 
  return m_graph->getDebugStateInfo();
}

void Animator::computeBoneMatrices() {
  if (m_skeleton.empty()) return;

  m_boneMatrices.resize(m_skeleton.size(), glm::mat4(1.0f));
  m_localTransforms.resize(m_skeleton.size(), glm::mat4(1.0f));
  m_globalTransforms.resize(m_skeleton.size());

  for (size_t i = 0; i < m_skeleton.size(); i++) {
    const Bone& bone = m_skeleton[i];
    m_globalTransforms[i] = (bone.parentIndex < 0)
      ? m_localTransforms[i]
      : m_globalTransforms[bone.parentIndex] * m_localTransforms[i];

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
