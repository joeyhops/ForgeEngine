#include <forge/Animator.h>
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
}

void Animator::play(std::shared_ptr<AnimationClip> clip,
                    bool loop, float blendTime)
{
  if (!clip) return;
  m_prevClip = m_currentClip;
  m_currentClip = clip;
  m_currentClip->looping = loop;
  m_currentTime = 0.0f;
  m_blendTime = blendTime;
  m_blendElapsed = 0.0f;
  LOG_INFO("[Animator] Playing '{}'", clip->name);
}

void Animator::update(float dt) {
  if (!m_currentClip) return;

  m_currentTime += dt;
  if (m_currentClip->looping && m_currentTime > m_currentClip->duration)
    m_currentTime = fmodf(m_currentTime, m_currentClip->duration);

  m_blendElapsed += dt;
  computeBoneMatrices();
}

void Animator::computeBoneMatrices() {
  if (m_skeleton.empty()) return;

  // Sample the animation clip into local transforms
  //m_currentClip->sample(m_currentTime, m_skeleton, m_localTransforms);


  // Walk bone hierarchy - each bones global transform == parent
  // global transform * this bones local transform
//  std::vector<glm::mat4> globalTransforms(m_skeleton.size());

  m_globalTransforms.resize(m_skeleton.size());

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
