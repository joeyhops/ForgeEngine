#pragma once

#include <forge/AnimationClip.h>
#include <forge/Bone.h>
#include <forge/HitboxTypes.h>
#include <forge/SampledEventsBuffer.h>
#include <forge/SyncTrack.h>

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace forge::test {

//Last enum of AnimEventType -> update if new event type added
// test_TypeSystem cross checks this against reflected EnumRegistry
constexpr AnimEventType kLastAnimEventType = AnimEventType::SpawnImpactVFX;
constexpr int kAnimEventTypeCount = static_cast<int>(kLastAnimEventType) + 1;

std::string eventTypeName(AnimEventType t);

class ClipBuilder {
public:
  explicit ClipBuilder(std::string name = "test_clip") : m_name(std::move(name)) {}

  ClipBuilder& duration(float seconds) { m_duration = seconds; return *this; }

  ClipBuilder& window(AnimEventType type, float start, float end, std::string payload = {});

  ClipBuilder& oneShot(AnimEventType type, float at, std::string payload = {}) {
    return window(type, at, at, std::move(payload));
  }

  ClipBuilder& hitbox(float start, float end,
                      std::vector<CapsuleSegment> capsules = { CapsuleSegment{} },
                      std::string bone = "weapon_r");

  ClipBuilder& sync(std::vector<std::pair<std::string, float>> markers) {
    m_sync = std::move(markers);
    return *this;
  }

  ClipBuilder& rootMotion(glm::vec3 metresPerSecond, std::string bone = "root",
                          bool extractY = false);

  std::shared_ptr<AnimationClip> build() const;

private:
  std::string m_name;
  float m_duration = 1.0f;
  std::vector<AnimEvent> m_events;
  std::vector<std::pair<std::string, float>> m_sync;

  struct RootMotionSpec { glm::vec3 velocity; std::string bone; bool extractY; };
  std::optional<RootMotionSpec> m_root;
};

std::vector<Bone> makeSkeleton(const std::vector<std::string>& names);

const SampledEvent* findEvent(const SampledEventsBuffer& buff, AnimEventType type,
                              std::string_view payload = {});

int countEvents(const SampledEventsBuffer& buff, AnimEventType type);
int countEvents(const SampledEventsBuffer& buff, AnimEventType type, std::string_view payload);

}
