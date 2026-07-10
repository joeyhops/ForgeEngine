#include <forge/Animator.h>
#include <forge/Events.h>
#include <forge/EventBus.h>
#include <forge/Logger.h>
#include <forge/AssetManager.h>

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
  m_taskSystem.Init((int)skeleton.size(), 16);
}

void Animator::setGraph(GraphInstance instance) {
  m_graphInstance = std::move(instance);
  AnimGraphNode* root = m_graphInstance.GetRootNode();
  if (root && !m_skeleton.empty())
    root->setSkeleton(&m_skeleton);
}

void Animator::swapMovesetClips(const std::unordered_map<std::string, std::string>& clipPaths) {
  for (const auto& [actionKey, path] : clipPaths) {
    ClipNode* node = m_graphInstance.FindClipByActionKey(actionKey);
    if (!node) {
      LOG_WARN("[Animator] '{}' swapMovesetClips: no node with actionKey '{}' in graph",
               m_ownerName, actionKey);
      continue;
    }
    auto clip = AssetManager::loadAnimationClip(path, "", actionKey);
    if (!clip) {
      LOG_WARN("[Animator] swapMovesetClips: failed to load clip '{}' for key '{}'",
               path, actionKey);
      continue;
    }
    node->setClip(std::move(clip));
    LOG_INFO("[Animator] '{}' swapped clip for action key: '{}'", m_ownerName, actionKey);
  }
}

void Animator::updatePrePhysics(float dt) {
  AnimGraphNode* root = m_graphInstance.GetRootNode();
  if (!root) {
    LOG_ERROR("[Animator] '{}' updatePrePhysics() called without graph", m_ownerName);
    return;
  }

  m_taskSystem.Reset();
#ifdef FORGE_DEBUG
  m_debugger.BeginFrame();
#endif

  UpdateContext ctx{
    dt, m_params, m_taskSystem, &m_skeleton
#ifdef FORGE_DEBUG
    , &m_debugger
#endif
  };

  UpdateResult result = root->update(ctx);
  m_rootTask = result.taskIdx;
  m_rootMotionDelta = result.rootMotion;
  m_rootMotionActive = glm::length(m_rootMotionDelta) > 0.001f;

  PoseBuffer* pose = m_taskSystem.ExecutePrePhysicsTasks(m_rootTask);
  if (pose) {
    m_localTransforms = pose->localTransforms;
    m_taskSystem.Pool().Release(pose);
  }
}

void Animator::updatePostPhysics() {
  PoseBuffer* pose = m_taskSystem.ExecutePostPhysicsTasks(m_rootTask);
  if (pose && pose->refCount > 0) {
    m_localTransforms = pose->localTransforms;
    m_taskSystem.Pool().Release(pose);
  }

  computeBoneMatrices();

#ifdef FORGE_DEBUG
  if (m_taskSystem.Pool().InUse() != 0) // leak check, DAG must collapse to zero live buffers
    LOG_ERROR("[Animator] '{}' pose buffer leak: {} in use after frame", m_ownerName, m_taskSystem.Pool().InUse());
#endif
}

bool Animator::isFinished() const {
  AnimGraphNode* root = m_graphInstance.GetRootNode();
  return root ? root->isFinished() : false;
}

float Animator::getActiveClipTime() const {
  AnimGraphNode* root = m_graphInstance.GetRootNode();
  return root ? root->getActiveClipTime() : 0.0f;
}

float Animator::getActiveClipDuration() const {
  AnimGraphNode* root = m_graphInstance.GetRootNode();
  return root ? root->getActiveClipDuration() : 0.0f;
}

void Animator::scrubTo(float t) {
  AnimGraphNode* root = m_graphInstance.GetRootNode();
  if (!root) {
    LOG_ERROR("[Animator] '{}' scrubTo() called without graph", m_ownerName);
    return;
  }
  root->setActiveTime(t);
  m_taskSystem.Reset();
  UpdateContext ctx{ 0.0f, m_params, m_taskSystem, &m_skeleton
#ifdef FORGE_DEBUG
    , &m_debugger
#endif
  };
  UpdateResult result = root->update(ctx);
  PoseBuffer* pose = m_taskSystem.ExecutePrePhysicsTasks(result.taskIdx);
  if (pose) {
    m_localTransforms = pose->localTransforms;
    m_taskSystem.Pool().Release(pose);
  }
  computeBoneMatrices();
}

std::string Animator::getCurrentStateName() const {
  AnimGraphNode* root = m_graphInstance.GetRootNode();
  if (!root) return "";
  auto* sm = dynamic_cast<StateMachineNode*>(root);
  return sm->getCurrentState();
}

std::string Animator::getStateInfo() const {
  AnimGraphNode* root = m_graphInstance.GetRootNode();
  if (!root) return "none"; 
  return root->getDebugStateInfo();
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

      if (useRootMotion && skeleton[i].name == rootBoneName) {
        glm::mat4 RS = R * S;
        RS[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        local = RS;
      }
    }

    out[i] = local;
  }
}

glm::vec3 AnimationClip::sampleRootDelta(float tPrev, float tNow) const {
  if (!useRootMotion) return glm::vec3(0.0f);

  auto it = trackIndex.find(rootBoneName);
  if (it == trackIndex.end()) return glm::vec3(0.0f);

  const BoneTrack& track = tracks[it->second];

  glm::vec3 delta;
  if (tNow >= tPrev) {
    delta = track.sample(tNow).position - track.sample(tPrev).position;
    if (glm::length(delta) > 0.001f)
      LOG_INFO("[RM] delta: ({:.3f},{:.3f},{:.3f})", delta.x, delta.y, delta.z);
    return delta;
  }

  // Loop seam, clips wraps around to this frame
  // Delta = (end of clip pos - prev pos) + (curr pos + start of clip pos)
  float endT = duration - 0.0001;
  glm::vec3 toEnd = track.sample(endT).position - track.sample(tPrev).position;
  glm::vec3 fromStart = track.sample(tNow).position - track.sample(0.0f).position;
  delta = toEnd + fromStart;
  if (glm::length(delta) > 0.001f)
    LOG_INFO("[RM] delta: ({:.3f},{:.3f},{:.3f})", delta.x, delta.y, delta.z);

  return delta;
}

void Animator::setPreviewClip(std::shared_ptr<AnimationClip> clip, bool loop) {
  auto def = std::make_shared<GraphDefinition>();
  auto clipDef = std::make_unique<ClipNode::Definition>();
  clipDef->loop = loop;
  clipDef->actionKey = "preview";
  clipDef->ownerName = m_ownerName;

  // clipFile intentionally empty, we wire it directly below
  def->m_nodeDefs.push_back(std::move(clipDef));
  def->m_rootNodeIndex = 0;

  auto inst = GraphInstance::Create(def);
  static_cast<ClipNode*>(inst.m_nodes[0])->m_clip = std::move(clip);
  setGraph(std::move(inst));
}

}
