#include "support/AnimTestUtils.h"
#include "forge/Bone.h"

#include <forge/TypeSystem.h>

#include <algorithm>

namespace forge::test {

std::string eventTypeName(AnimEventType t) {
  const char* name = EnumRegistry::Get().GetValueName(fnv1a("forge::AnimEventType"), static_cast<int64_t>(t));
  return name ? std::string(name) : "AnimEventType(" + std::to_string(static_cast<int>(t)) + ")";
}

ClipBuilder& ClipBuilder::window(AnimEventType type, float start, float end, std::string payload) {
  AnimEvent e{};
  e.startTime = start;
  e.endTime = end;
  e.type = type;
  e.payload = std::move(payload);
  m_events.push_back(std::move(e));
  return *this;
}

ClipBuilder& ClipBuilder::hitbox(float start, float end, std::vector<CapsuleSegment> capsules, std::string bone) {
  window(AnimEventType::SpawnHitbox, start, end, std::move(bone));
  m_events.back().capsules = std::move(capsules);
  return *this;
}

ClipBuilder& ClipBuilder::rootMotion(glm::vec3 metresPerSecond, std::string bone, bool extractY) {
  m_root = RootMotionSpec{ metresPerSecond, std::move(bone), extractY };
  return *this;
}

std::shared_ptr<AnimationClip> ClipBuilder::build() const {
  auto clip = std::make_shared<AnimationClip>();
  clip->name = m_name;
  clip->duration = m_duration;
  clip->ticksPerSec = 30.0f;

  clip->events = m_events;
  std::stable_sort(clip->events.begin(), clip->events.end(),
                    [](const AnimEvent& a, const AnimEvent& b) { return a.startTime < b.startTime; });

  for (const auto& [name, pct] : m_sync)
    clip->syncTrack.events.push_back(SyncEvent{ pct, name, 0 });
  std::sort(clip->syncTrack.events.begin(), clip->syncTrack.events.end(),
            [](const SyncEvent& a, const SyncEvent& b) { return a.percentage < b.percentage; });
  clip->syncTrack.rehash();

  if (m_root) {
    BoneTrack track;
    track.boneName = m_root->bone;
    const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    track.keyframes = {
      BoneKeyFrame{ 0.0f, glm::vec3(0.0f), identity, glm::vec3(1.0f) },
      BoneKeyFrame{ m_duration, m_root->velocity * m_duration, identity, glm::vec3(1.0f) },
    };
    clip->trackIndex[track.boneName] = static_cast<int>(clip->tracks.size());
    clip->tracks.push_back(std::move(track));

    clip->useRootMotion = true;
    clip->rootBoneName = m_root->bone;
    clip->extractRootY = m_root->extractY;
  }
  return clip;
}

std::vector<Bone> makeSkeleton(const std::vector<std::string>& names) {
  std::vector<Bone> bones;
  bones.reserve(names.size());
  for (size_t i = 0; i < names.size(); ++i) {
    Bone b;
    b.name = names[i];
    b.parentIndex = (i == 0) ? -1 : 0;
    b.offsetMatrix = glm::mat4(1.0f);
    b.localTransform = glm::mat4(1.0f);
    bones.push_back(std::move(b));
  }
  return bones;
}

const SampledEvent* findEvent(const SampledEventsBuffer& buff, AnimEventType type,
                              std::string_view payload) {
  for (const auto& e : buff.Events())
    if (e.type == type && e.payload == payload) return &e;
  return nullptr;
}

int countEvents(const SampledEventsBuffer& buff, AnimEventType type) {
  return static_cast<int>(std::count_if(buff.Events().begin(), buff.Events().end(),
                                        [&](const SampledEvent& e) { return e.type == type; }));
}

int countEvents(const SampledEventsBuffer& buff, AnimEventType type, std::string_view payload) {
  return static_cast<int>(std::count_if(buff.Events().begin(), buff.Events().end(),
                                        [&](const SampledEvent& e) { return e.type == type && e.payload == payload; }));
}

}
